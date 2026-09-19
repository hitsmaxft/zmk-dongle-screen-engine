#!/usr/bin/env python3
"""Generate the deterministic cross-language Format 1 fixture corpus."""

from __future__ import annotations

import argparse
import binascii
from pathlib import Path
import struct

HEADER_SIZE = 48
SECTION_SIZE = 24
PACK_SIZE = 108


def put16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def put32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def finish_crc(data: bytearray) -> None:
    put32(data, 36, 0)
    put32(data, 36, binascii.crc32(data[: struct.unpack_from("<I", data, 12)[0]]))


def base_pack() -> bytearray:
    data = bytearray(PACK_SIZE)
    data[:4] = b"ZDS1"
    put16(data, 4, 1)
    put16(data, 6, 0)
    put16(data, 8, HEADER_SIZE)
    put16(data, 10, SECTION_SIZE)
    put32(data, 12, PACK_SIZE)
    put32(data, 16, HEADER_SIZE)
    put32(data, 20, 2)
    put32(data, 24, 1 << 2)
    put32(data, 28, 1 << 31)
    put32(data, 32, 0x12345678)

    put16(data, 48, 3)
    put16(data, 50, 1)
    put32(data, 52, 96)
    put32(data, 56, 8)
    put32(data, 60, 8)
    put32(data, 64, 1)
    put16(data, 68, 4)

    put16(data, 72, 15)
    put32(data, 76, 104)
    put32(data, 80, 4)
    put16(data, 92, 1)
    for offset in range(96, PACK_SIZE):
        data[offset] = (offset * 17) & 0xFF
    finish_crc(data)
    return data


def changed(mutator) -> bytes:
    data = base_pack()
    mutator(data)
    finish_crc(data)
    return bytes(data)


def generate(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    valid = base_pack()
    fixtures = {
        "valid-minimal.zds": bytes(valid),
        "valid-unknown-optional.zds": changed(lambda d: put16(d, 72, 0x7777)),
        "bad-truncated.zds": bytes(valid[:-1]),
        "bad-crc.zds": bytes(valid[:-1] + bytes([valid[-1] ^ 1])),
        "bad-section-overflow.zds": changed(
            lambda d: (put32(d, 76, 0xFFFFFFF0), put32(d, 80, 64))
        ),
        "bad-section-order.zds": changed(lambda d: put32(d, 76, 96)),
        "bad-required-feature.zds": changed(lambda d: put32(d, 24, 1 << 31)),
        "bad-required-section.zds": changed(
            lambda d: (put16(d, 72, 0x7777), put16(d, 74, 1))
        ),
        "bad-record-product.zds": changed(lambda d: put32(d, 64, 2)),
        "bad-alignment.zds": changed(
            lambda d: (put32(d, 76, 106), put32(d, 80, 2), put16(d, 92, 4))
        ),
        "bad-reserved.zds": changed(lambda d: d.__setitem__(40, 1)),
        "bad-duplicate-section.zds": changed(lambda d: put16(d, 72, 3)),
    }
    for name, payload in fixtures.items():
        (output / name).write_bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("tests/fixtures/zds-format1"),
    )
    generate(parser.parse_args().output)


if __name__ == "__main__":
    main()
