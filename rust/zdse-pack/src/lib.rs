#![no_std]
#![forbid(unsafe_code)]

pub const MAGIC: u32 = 0x3153_445a;
pub const FORMAT_MAJOR: u16 = 1;
pub const FORMAT_MINOR: u16 = 0;
pub const HEADER_SIZE: usize = 48;
pub const SECTION_SIZE: usize = 24;
pub const MAX_SECTIONS: u32 = 4096;
pub const KNOWN_FEATURES: u32 = (1 << 9) - 1;

const SECTION_REQUIRED: u16 = 1;
const KNOWN_SECTION_FLAGS: u16 = SECTION_REQUIRED;
const SECTION_DEBUG: u16 = 0x8000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    BadPack,
    UnsupportedFormat,
    UnsupportedFeature,
    Crc,
    Capacity,
    Callback,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PackInfo {
    pub format_major: u16,
    pub format_minor: u16,
    pub total_size: u32,
    pub section_count: u32,
    pub required_features: u32,
    pub optional_features: u32,
    pub root_scene_id: u32,
    pub crc32: u32,
}

fn u16_le(bytes: &[u8], offset: usize) -> Result<u16, Error> {
    let tail = bytes
        .get(offset..offset.checked_add(2).ok_or(Error::BadPack)?)
        .ok_or(Error::Callback)?;
    Ok(u16::from_le_bytes([tail[0], tail[1]]))
}

fn u32_le(bytes: &[u8], offset: usize) -> Result<u32, Error> {
    let tail = bytes
        .get(offset..offset.checked_add(4).ok_or(Error::BadPack)?)
        .ok_or(Error::Callback)?;
    Ok(u32::from_le_bytes([tail[0], tail[1], tail[2], tail[3]]))
}

fn crc32(pack: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for (offset, original) in pack.iter().copied().enumerate() {
        let value = if (36..40).contains(&offset) {
            0
        } else {
            original
        };
        crc ^= u32::from(value);
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xedb8_8320 & mask);
        }
    }
    crc ^ u32::MAX
}

fn known_section(section_type: u16) -> bool {
    (1..=15).contains(&section_type) || section_type == SECTION_DEBUG
}

pub fn validate(bytes: &[u8]) -> Result<PackInfo, Error> {
    if bytes.len() < HEADER_SIZE {
        return Err(Error::Callback);
    }
    if u32_le(bytes, 0)? != MAGIC {
        return Err(Error::BadPack);
    }

    let format_major = u16_le(bytes, 4)?;
    let format_minor = u16_le(bytes, 6)?;
    let header_size = u16_le(bytes, 8)?;
    let section_size = u16_le(bytes, 10)?;
    let total_size = u32_le(bytes, 12)?;
    let section_offset = u32_le(bytes, 16)?;
    let section_count = u32_le(bytes, 20)?;
    let required_features = u32_le(bytes, 24)?;
    let optional_features = u32_le(bytes, 28)?;
    let root_scene_id = u32_le(bytes, 32)?;
    let expected_crc = u32_le(bytes, 36)?;

    if format_major != FORMAT_MAJOR {
        return Err(Error::UnsupportedFormat);
    }
    if format_minor > FORMAT_MINOR
        || usize::from(header_size) != HEADER_SIZE
        || usize::from(section_size) != SECTION_SIZE
    {
        return Err(Error::UnsupportedFormat);
    }
    if total_size < HEADER_SIZE as u32 || section_count > MAX_SECTIONS {
        return Err(Error::Capacity);
    }
    if required_features & !KNOWN_FEATURES != 0 {
        return Err(Error::UnsupportedFeature);
    }
    if bytes[40..HEADER_SIZE].iter().any(|byte| *byte != 0) {
        return Err(Error::BadPack);
    }

    let pack = bytes.get(..total_size as usize).ok_or(Error::Callback)?;
    if crc32(pack) != expected_crc {
        return Err(Error::Crc);
    }

    let directory_bytes = section_count
        .checked_mul(SECTION_SIZE as u32)
        .ok_or(Error::BadPack)?;
    let directory_end = section_offset
        .checked_add(directory_bytes)
        .ok_or(Error::BadPack)?;
    if section_offset < HEADER_SIZE as u32 || section_offset % 4 != 0 || directory_end > total_size
    {
        return Err(Error::BadPack);
    }

    let mut previous_end = directory_end;
    let mut seen_types = 0u32;
    let mut seen_debug = false;
    for index in 0..section_count {
        let relative = index
            .checked_mul(SECTION_SIZE as u32)
            .ok_or(Error::BadPack)?;
        let offset = section_offset.checked_add(relative).ok_or(Error::BadPack)? as usize;
        let section_type = u16_le(pack, offset)?;
        let flags = u16_le(pack, offset + 2)?;
        let payload_offset = u32_le(pack, offset + 4)?;
        let length = u32_le(pack, offset + 8)?;
        let record_size = u32_le(pack, offset + 12)?;
        let record_count = u32_le(pack, offset + 16)?;
        let alignment = u16_le(pack, offset + 20)?;
        let reserved = u16_le(pack, offset + 22)?;
        let payload_end = payload_offset.checked_add(length).ok_or(Error::BadPack)?;

        if reserved != 0
            || flags & !KNOWN_SECTION_FLAGS != 0
            || alignment == 0
            || !alignment.is_power_of_two()
            || alignment > 16
            || payload_offset % u32::from(alignment) != 0
            || payload_offset < previous_end
            || payload_end > total_size
        {
            return Err(Error::BadPack);
        }
        if record_size == 0 {
            if record_count != 0 {
                return Err(Error::BadPack);
            }
        } else if record_size.checked_mul(record_count) != Some(length) {
            return Err(Error::BadPack);
        }

        if !known_section(section_type) {
            if flags & SECTION_REQUIRED != 0 {
                return Err(Error::UnsupportedFeature);
            }
        } else if section_type <= 31 {
            let bit = 1u32 << section_type;
            if seen_types & bit != 0 {
                return Err(Error::BadPack);
            }
            seen_types |= bit;
        } else if section_type == SECTION_DEBUG {
            if seen_debug {
                return Err(Error::BadPack);
            }
            seen_debug = true;
        }
        previous_end = payload_end;
    }

    Ok(PackInfo {
        format_major,
        format_minor,
        total_size,
        section_count,
        required_features,
        optional_features,
        root_scene_id,
        crc32: expected_crc,
    })
}
