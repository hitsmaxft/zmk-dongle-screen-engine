/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/v2/pack.h>

#include <stdbool.h>
#include <string.h>

struct pack_header {
  uint16_t format_major;
  uint16_t format_minor;
  uint16_t header_size;
  uint16_t section_size;
  uint32_t total_size;
  uint32_t section_offset;
  uint32_t section_count;
  uint32_t required_features;
  uint32_t optional_features;
  uint32_t root_scene_id;
  uint32_t crc32;
};

struct pack_section {
  uint16_t type;
  uint16_t flags;
  uint32_t offset;
  uint32_t length;
  uint32_t record_size;
  uint32_t record_count;
  uint16_t alignment;
};

struct source_stats {
  uint32_t read_calls;
  uint32_t map_calls;
  uint32_t bytes_read;
};

static uint16_t read_u16_le(const uint8_t *bytes) {
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool add_u32(uint32_t left, uint32_t right, uint32_t *result) {
  if (right > UINT32_MAX - left) {
    return false;
  }
  *result = left + right;
  return true;
}

static bool multiply_u32(uint32_t left, uint32_t right, uint32_t *result) {
  if (left != 0 && right > UINT32_MAX / left) {
    return false;
  }
  *result = left * right;
  return true;
}

static bool is_power_of_two(uint16_t value) {
  return value != 0 && (value & (uint16_t)(value - 1)) == 0;
}

static bool known_section(uint16_t type) {
  return (type >= ZDSE_PACK_SECTION_STRINGS &&
          type <= ZDSE_PACK_SECTION_DATA) ||
         type == ZDSE_PACK_SECTION_DEBUG;
}

static zdse_result_t validate_abi(uint16_t version, uint16_t size,
                                  uint16_t required_size,
                                  uint64_t required_capabilities,
                                  uint64_t known_capabilities) {
  if (ZDSE_ABI_VERSION_MAJOR(version) !=
      ZDSE_ABI_VERSION_MAJOR(ZDSE_ABI_VERSION_V2_0)) {
    return ZDSE_ERR_UNSUPPORTED_ABI;
  }
  if (size < required_size) {
    return ZDSE_ERR_STRUCT_TOO_SMALL;
  }
  if ((required_capabilities & ~known_capabilities) != 0) {
    return ZDSE_ERR_UNSUPPORTED_FEATURE;
  }
  return ZDSE_OK;
}

static zdse_result_t source_read(const struct zdse_asset_source_v2 *source,
                                 uint32_t offset, void *destination,
                                 uint32_t length, struct source_stats *stats) {
  zdse_result_t result = source->read(source->user_data, offset, destination,
                                      length);
  stats->read_calls++;
  if (result != ZDSE_OK) {
    return ZDSE_ERR_CALLBACK;
  }
  if (UINT32_MAX - stats->bytes_read < length) {
    stats->bytes_read = UINT32_MAX;
  } else {
    stats->bytes_read += length;
  }
  return ZDSE_OK;
}

static void decode_header(const uint8_t *bytes, struct pack_header *header) {
  header->format_major = read_u16_le(bytes + 4);
  header->format_minor = read_u16_le(bytes + 6);
  header->header_size = read_u16_le(bytes + 8);
  header->section_size = read_u16_le(bytes + 10);
  header->total_size = read_u32_le(bytes + 12);
  header->section_offset = read_u32_le(bytes + 16);
  header->section_count = read_u32_le(bytes + 20);
  header->required_features = read_u32_le(bytes + 24);
  header->optional_features = read_u32_le(bytes + 28);
  header->root_scene_id = read_u32_le(bytes + 32);
  header->crc32 = read_u32_le(bytes + 36);
}

static void decode_section(const uint8_t *bytes, struct pack_section *section) {
  section->type = read_u16_le(bytes);
  section->flags = read_u16_le(bytes + 2);
  section->offset = read_u32_le(bytes + 4);
  section->length = read_u32_le(bytes + 8);
  section->record_size = read_u32_le(bytes + 12);
  section->record_count = read_u32_le(bytes + 16);
  section->alignment = read_u16_le(bytes + 20);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes,
                             uint32_t length) {
  for (uint32_t index = 0; index < length; index++) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
    }
  }
  return crc;
}

static zdse_result_t validate_crc(const struct zdse_asset_source_v2 *source,
                                  const uint8_t *mapped,
                                  const struct pack_header *header,
                                  uint8_t *scratch,
                                  size_t scratch_size,
                                  struct source_stats *stats) {
  uint32_t crc = UINT32_MAX;
  uint32_t offset = 0;
  while (offset < header->total_size) {
    uint32_t remaining = header->total_size - offset;
    uint32_t length = remaining < scratch_size ? remaining : (uint32_t)scratch_size;
    const uint8_t *bytes = mapped == NULL ? scratch : mapped + offset;
    if (mapped == NULL) {
      zdse_result_t result =
          source_read(source, offset, scratch, length, stats);
      if (result != ZDSE_OK) {
        return result;
      }
    }
    for (uint32_t index = 0; index < length; index++) {
      uint32_t absolute = offset + index;
      uint8_t value = bytes[index];
      if (absolute >= ZDSE_PACK_CRC_OFFSET &&
          absolute < ZDSE_PACK_CRC_OFFSET + 4) {
        value = 0;
      }
      crc = crc32_update(crc, &value, 1);
    }
    offset += length;
  }
  return (crc ^ UINT32_MAX) == header->crc32 ? ZDSE_OK : ZDSE_ERR_CRC;
}

static zdse_result_t read_section(const struct zdse_asset_source_v2 *source,
                                  const uint8_t *mapped,
                                  const struct pack_header *header,
                                  uint32_t index, uint8_t *bytes,
                                  struct source_stats *stats) {
  uint32_t relative;
  uint32_t offset;
  if (!multiply_u32(index, ZDSE_PACK_SECTION_SIZE, &relative) ||
      !add_u32(header->section_offset, relative, &offset)) {
    return ZDSE_ERR_BAD_PACK;
  }
  if (mapped != NULL) {
    memcpy(bytes, mapped + offset, ZDSE_PACK_SECTION_SIZE);
    return ZDSE_OK;
  }
  return source_read(source, offset, bytes, ZDSE_PACK_SECTION_SIZE, stats);
}

static zdse_result_t validate_sections(
    const struct zdse_asset_source_v2 *source, const uint8_t *mapped,
    const struct pack_header *header, struct source_stats *stats) {
  uint8_t bytes[ZDSE_PACK_SECTION_SIZE];
  uint32_t directory_bytes;
  uint32_t directory_end;
  uint32_t previous_end;
  uint32_t seen_types = 0;
  bool seen_debug = false;

  if (!multiply_u32(header->section_count, ZDSE_PACK_SECTION_SIZE,
                    &directory_bytes) ||
      !add_u32(header->section_offset, directory_bytes, &directory_end) ||
      directory_end > header->total_size) {
    return ZDSE_ERR_BAD_PACK;
  }
  if (header->section_offset < ZDSE_PACK_HEADER_SIZE ||
      header->section_offset % 4 != 0) {
    return ZDSE_ERR_BAD_PACK;
  }
  previous_end = directory_end;

  for (uint32_t index = 0; index < header->section_count; index++) {
    struct pack_section section;
    uint32_t section_end;
    uint32_t records_length;
    zdse_result_t result =
        read_section(source, mapped, header, index, bytes, stats);
    if (result != ZDSE_OK) {
      return result;
    }
    decode_section(bytes, &section);
    if (read_u16_le(bytes + 22) != 0 ||
        (section.flags & ~ZDSE_PACK_SECTION_KNOWN_FLAGS) != 0 ||
        !is_power_of_two(section.alignment) || section.alignment > 16 ||
        section.offset % section.alignment != 0 ||
        section.offset < previous_end ||
        !add_u32(section.offset, section.length, &section_end) ||
        section_end > header->total_size) {
      return ZDSE_ERR_BAD_PACK;
    }
    if (section.record_size == 0) {
      if (section.record_count != 0) {
        return ZDSE_ERR_BAD_PACK;
      }
    } else if (!multiply_u32(section.record_size, section.record_count,
                             &records_length) ||
               records_length != section.length) {
      return ZDSE_ERR_BAD_PACK;
    }
    if (!known_section(section.type)) {
      if ((section.flags & ZDSE_PACK_SECTION_REQUIRED) != 0) {
        return ZDSE_ERR_UNSUPPORTED_FEATURE;
      }
    } else if (section.type <= 31) {
      uint32_t bit = UINT32_C(1) << section.type;
      if ((seen_types & bit) != 0) {
        return ZDSE_ERR_BAD_PACK;
      }
      seen_types |= bit;
    } else if (section.type == ZDSE_PACK_SECTION_DEBUG) {
      if (seen_debug) {
        return ZDSE_ERR_BAD_PACK;
      }
      seen_debug = true;
    }
    previous_end = section_end;
  }
  return ZDSE_OK;
}

size_t zdse_pack_query_scratch_size(void) {
  return ZDSE_PACK_QUERY_SCRATCH_BYTES;
}

size_t zdse_pack_query_scratch_align(void) {
  return ZDSE_PACK_QUERY_SCRATCH_ALIGN;
}

zdse_result_t zdse_pack_validate(const struct zdse_asset_source_v2 *source,
                                 void *query_scratch,
                                 size_t query_scratch_size,
                                 struct zdse_pack_info_v2 *out_info) {
  uint8_t header_bytes[ZDSE_PACK_HEADER_SIZE];
  struct pack_header header;
  struct source_stats stats = {0};
  const uint8_t *mapped = NULL;
  zdse_result_t result;

  if (source == NULL || query_scratch == NULL || out_info == NULL) {
    return ZDSE_ERR_INVALID_ARGUMENT;
  }
  result = validate_abi(source->abi_version, source->struct_size,
                        ZDSE_ASSET_SOURCE_V2_REQUIRED_SIZE,
                        source->required_capabilities,
                        ZDSE_ASSET_SOURCE_KNOWN_CAPABILITIES);
  if (result != ZDSE_OK) {
    return result;
  }
  result = validate_abi(out_info->abi_version, out_info->struct_size,
                        ZDSE_PACK_INFO_V2_REQUIRED_SIZE,
                        out_info->required_capabilities, 0);
  if (result != ZDSE_OK) {
    return result;
  }
  if (source->read == NULL || (source->map == NULL) != (source->unmap == NULL)) {
    return ZDSE_ERR_INVALID_ARGUMENT;
  }
  if ((source->required_capabilities & ZDSE_ASSET_SOURCE_CAP_MAP) != 0 &&
      source->map == NULL) {
    return ZDSE_ERR_UNSUPPORTED_FEATURE;
  }
  if (query_scratch_size < ZDSE_PACK_QUERY_SCRATCH_BYTES ||
      ((uintptr_t)query_scratch % ZDSE_PACK_QUERY_SCRATCH_ALIGN) != 0) {
    return query_scratch_size < ZDSE_PACK_QUERY_SCRATCH_BYTES
               ? ZDSE_ERR_CAPACITY
               : ZDSE_ERR_ALIGNMENT;
  }
  result = source_read(source, 0, header_bytes, ZDSE_PACK_HEADER_SIZE, &stats);
  if (result != ZDSE_OK) {
    return result;
  }
  if (read_u32_le(header_bytes) != ZDSE_PACK_MAGIC) {
    return ZDSE_ERR_BAD_PACK;
  }
  decode_header(header_bytes, &header);
  if (header.format_major != ZDSE_PACK_FORMAT_MAJOR) {
    return ZDSE_ERR_UNSUPPORTED_FORMAT;
  }
  if (header.format_minor > ZDSE_PACK_FORMAT_MINOR ||
      header.header_size != ZDSE_PACK_HEADER_SIZE ||
      header.section_size != ZDSE_PACK_SECTION_SIZE) {
    return ZDSE_ERR_UNSUPPORTED_FORMAT;
  }
  if (header.total_size < ZDSE_PACK_HEADER_SIZE ||
      header.section_count > ZDSE_PACK_MAX_SECTIONS) {
    return ZDSE_ERR_CAPACITY;
  }
  if ((header.required_features & ~ZDSE_PACK_KNOWN_FEATURES) != 0) {
    return ZDSE_ERR_UNSUPPORTED_FEATURE;
  }
  for (uint32_t index = 40; index < ZDSE_PACK_HEADER_SIZE; index++) {
    if (header_bytes[index] != 0) {
      return ZDSE_ERR_BAD_PACK;
    }
  }

  if (source->map != NULL) {
    stats.map_calls++;
    mapped = source->map(source->user_data, 0, header.total_size);
  }
  result = validate_crc(source, mapped, &header, query_scratch,
                        query_scratch_size, &stats);
  if (result == ZDSE_OK) {
    result = validate_sections(source, mapped, &header, &stats);
  }
  if (mapped != NULL) {
    source->unmap(source->user_data, mapped, header.total_size);
  }
  if (result != ZDSE_OK) {
    return result;
  }

  out_info->format_major = header.format_major;
  out_info->format_minor = header.format_minor;
  out_info->total_size = header.total_size;
  out_info->section_count = header.section_count;
  out_info->required_features = header.required_features;
  out_info->optional_features = header.optional_features;
  out_info->root_scene_id = header.root_scene_id;
  out_info->crc32 = header.crc32;
  out_info->read_calls = stats.read_calls;
  out_info->map_calls = stats.map_calls;
  out_info->bytes_read = stats.bytes_read;
  return ZDSE_OK;
}
