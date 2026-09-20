use zdse_pack::{validate, Error, PackInfo};

const VALID: &[u8] = include_bytes!("../../../tests/fixtures/zds-format1/valid-minimal.zds");
const VALID_OPTIONAL: &[u8] =
    include_bytes!("../../../tests/fixtures/zds-format1/valid-unknown-optional.zds");

#[test]
fn decodes_canonical_valid_packs() {
    let info = validate(VALID).expect("minimal fixture");
    assert_eq!(
        info,
        PackInfo {
            format_major: 1,
            format_minor: 0,
            total_size: 108,
            section_count: 2,
            required_features: 1 << 2,
            optional_features: 1 << 31,
            root_scene_id: 0x1234_5678,
            crc32: info.crc32,
        }
    );
    assert!(validate(VALID_OPTIONAL).is_ok());
}

#[test]
fn rejects_corpus_with_stable_classes() {
    let cases: &[(&[u8], Error)] = &[
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-truncated.zds"),
            Error::Callback,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-crc.zds"),
            Error::Crc,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-section-overflow.zds"),
            Error::BadPack,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-section-order.zds"),
            Error::BadPack,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-required-feature.zds"),
            Error::UnsupportedFeature,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-required-section.zds"),
            Error::UnsupportedFeature,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-record-product.zds"),
            Error::BadPack,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-alignment.zds"),
            Error::BadPack,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-reserved.zds"),
            Error::BadPack,
        ),
        (
            include_bytes!("../../../tests/fixtures/zds-format1/bad-duplicate-section.zds"),
            Error::BadPack,
        ),
    ];
    for (pack, expected) in cases {
        assert_eq!(validate(pack), Err(*expected));
    }
}

#[test]
fn ignores_trailing_wrapper_bytes() {
    let mut wrapped = [0u8; 112];
    wrapped[..VALID.len()].copy_from_slice(VALID);
    wrapped[108..].copy_from_slice(&[0xde, 0xad, 0xbe, 0xef]);
    assert_eq!(validate(&wrapped), validate(VALID));
}
