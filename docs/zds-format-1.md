# ZDSE pack format 1 preview

This document freezes the byte envelope implemented on the Engine 2.0 preview
branch. Scene and renderer records will be added behind new required feature
bits; they are not implied by this first validator slice.

`.zds` is a little-endian wire format. Readers must use byte accessors and must
not cast pack bytes to C or Rust structures. Records contain no pointers,
native enums, bitfields, implicit padding, `long`, `size_t`, or C `bool`.

## Header

The 48-byte header has this exact layout:

| Offset | Width | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `ZDS1` |
| 4 | 2 | format major, `1` |
| 6 | 2 | format minor, `0` |
| 8 | 2 | header size, `48` |
| 10 | 2 | section entry size, `24` |
| 12 | 4 | total pack bytes |
| 16 | 4 | section-directory offset |
| 20 | 4 | section count, at most 4096 |
| 24 | 4 | required feature bits |
| 28 | 4 | optional feature bits |
| 32 | 4 | exported root scene ID |
| 36 | 4 | CRC-32/ISO-HDLC |
| 40 | 8 | zero |

The CRC covers exactly `total_size` bytes, with bytes 36 through 39 treated as
zero. Its reflected polynomial is `0xedb88320`, initial state and final XOR are
both `0xffffffff`.

## Section directory

Each 24-byte entry is:

| Offset | Width | Field |
| ---: | ---: | --- |
| 0 | 2 | section type |
| 2 | 2 | flags; bit 0 means required |
| 4 | 4 | absolute pack offset |
| 8 | 4 | byte length |
| 12 | 4 | fixed record size, or zero for byte data |
| 16 | 4 | record count, or zero for byte data |
| 20 | 2 | alignment: power of two from 1 through 16 |
| 22 | 2 | zero |

Entries are sorted by ascending payload offset. Payloads may touch but never
overlap each other, the header, or directory. This ordering permits one-pass,
constant-workspace overlap validation. A nonzero record size requires
`record_size * record_count == length`; byte-data sections use zero for both
record fields. The directory offset is four-byte aligned. Known section types
may occur at most once.

Initial type assignments are strings 1, symbols 2, scenes 3, nodes 4,
properties 5, inputs 6, expressions 7, actions 8, timelines 9, keyframes 10,
assets 11, fonts 12, glyphs 13, custom parameters 14, raw data 15, and optional
debug data `0x8000`. An unknown required type is rejected; an unknown optional
type is bounds-checked and skipped.

## Feature assignments

| Bit | Feature |
| ---: | --- |
| 0 | 32-bit wide internal indices |
| 1 | A4 most-significant-nibble-first coverage |
| 2 | keyed little-endian RGB565 sprites |
| 3 | little-endian RGB565 plus A1 MSB mask |
| 4 | MONO1 MSB bitmap coverage |
| 5 | optional A8 coverage |
| 6 | expression records |
| 7 | event-action records |
| 8 | custom nodes |

Unknown required feature bits are rejected. Unknown optional feature bits do
not change the meaning of known records.

## Bounded validation

The preview validator requires 256 bytes of caller-owned query scratch aligned
to four bytes. It first reads the 48-byte header and then attempts one mapping
of the complete pack. If mapping is unavailable, the exact worst-case read
count is `1 + ceil(total_size / scratch_bytes) + section_count`; bytes read are
`48 + total_size + 24 * section_count`. It uses fixed local header/section
records and no heap allocation.

The public C declarations are in
`include/zmk/dongle_theme/v2/{result,source,pack}.h`. Format and Runtime FFI
versions are separate: this file describes pack format 1.0, while those C
records negotiate ABI 2.x by sized prefixes and required capability masks.
