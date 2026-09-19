use zdse_pack::{validate, Error};

const CORPUS: &[(&str, &[u8])] = &[
    (
        "valid-minimal.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/valid-minimal.zds"),
    ),
    (
        "valid-unknown-optional.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/valid-unknown-optional.zds"),
    ),
    (
        "bad-truncated.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-truncated.zds"),
    ),
    (
        "bad-crc.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-crc.zds"),
    ),
    (
        "bad-section-overflow.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-section-overflow.zds"),
    ),
    (
        "bad-section-order.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-section-order.zds"),
    ),
    (
        "bad-required-feature.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-required-feature.zds"),
    ),
    (
        "bad-required-section.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-required-section.zds"),
    ),
    (
        "bad-record-product.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-record-product.zds"),
    ),
    (
        "bad-alignment.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-alignment.zds"),
    ),
    (
        "bad-reserved.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-reserved.zds"),
    ),
    (
        "bad-duplicate-section.zds",
        include_bytes!("../../../tests/fixtures/zds-format1/bad-duplicate-section.zds"),
    ),
];

fn error_code(error: Error) -> i32 {
    match error {
        Error::BadPack => -2,
        Error::UnsupportedFormat => -3,
        Error::UnsupportedFeature => -4,
        Error::Crc => -5,
        Error::Capacity => -6,
        Error::Callback => -8,
    }
}

fn main() {
    for (name, bytes) in CORPUS {
        match validate(bytes) {
            Ok(info) => println!(
                "{name}\t0\t{}\t{}\t{:08x}\t{:08x}\t{:08x}\t{:08x}",
                info.total_size,
                info.section_count,
                info.required_features,
                info.optional_features,
                info.root_scene_id,
                info.crc32
            ),
            Err(error) => println!(
                "{name}\t{}\t0\t0\t00000000\t00000000\t00000000\t00000000",
                error_code(error)
            ),
        }
    }
}
