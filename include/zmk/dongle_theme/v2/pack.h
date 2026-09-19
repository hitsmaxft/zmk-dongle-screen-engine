/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zmk/dongle_theme/v2/result.h>
#include <zmk/dongle_theme/v2/source.h>

#define ZDSE_PACK_MAGIC UINT32_C(0x3153445a)
#define ZDSE_PACK_FORMAT_MAJOR UINT16_C(1)
#define ZDSE_PACK_FORMAT_MINOR UINT16_C(0)
#define ZDSE_PACK_HEADER_SIZE UINT16_C(48)
#define ZDSE_PACK_SECTION_SIZE UINT16_C(24)
#define ZDSE_PACK_CRC_OFFSET UINT32_C(36)
#define ZDSE_PACK_MAX_SECTIONS UINT32_C(4096)

#define ZDSE_PACK_FEATURE_WIDE_INDEX (UINT32_C(1) << 0)
#define ZDSE_PACK_FEATURE_A4_MSB (UINT32_C(1) << 1)
#define ZDSE_PACK_FEATURE_RGB565_KEYED (UINT32_C(1) << 2)
#define ZDSE_PACK_FEATURE_RGB565_A1_MSB (UINT32_C(1) << 3)
#define ZDSE_PACK_FEATURE_MONO1_MSB (UINT32_C(1) << 4)
#define ZDSE_PACK_FEATURE_A8 (UINT32_C(1) << 5)
#define ZDSE_PACK_FEATURE_EXPRESSIONS (UINT32_C(1) << 6)
#define ZDSE_PACK_FEATURE_EVENT_ACTIONS (UINT32_C(1) << 7)
#define ZDSE_PACK_FEATURE_CUSTOM_NODES (UINT32_C(1) << 8)
#define ZDSE_PACK_KNOWN_FEATURES                                             \
  (ZDSE_PACK_FEATURE_WIDE_INDEX | ZDSE_PACK_FEATURE_A4_MSB |                 \
   ZDSE_PACK_FEATURE_RGB565_KEYED | ZDSE_PACK_FEATURE_RGB565_A1_MSB |        \
   ZDSE_PACK_FEATURE_MONO1_MSB | ZDSE_PACK_FEATURE_A8 |                      \
   ZDSE_PACK_FEATURE_EXPRESSIONS | ZDSE_PACK_FEATURE_EVENT_ACTIONS |         \
   ZDSE_PACK_FEATURE_CUSTOM_NODES)

#define ZDSE_PACK_SECTION_REQUIRED UINT16_C(0x0001)
#define ZDSE_PACK_SECTION_KNOWN_FLAGS ZDSE_PACK_SECTION_REQUIRED

#define ZDSE_PACK_SECTION_STRINGS UINT16_C(1)
#define ZDSE_PACK_SECTION_SYMBOLS UINT16_C(2)
#define ZDSE_PACK_SECTION_SCENES UINT16_C(3)
#define ZDSE_PACK_SECTION_NODES UINT16_C(4)
#define ZDSE_PACK_SECTION_PROPERTIES UINT16_C(5)
#define ZDSE_PACK_SECTION_INPUTS UINT16_C(6)
#define ZDSE_PACK_SECTION_EXPRESSIONS UINT16_C(7)
#define ZDSE_PACK_SECTION_ACTIONS UINT16_C(8)
#define ZDSE_PACK_SECTION_TIMELINES UINT16_C(9)
#define ZDSE_PACK_SECTION_KEYFRAMES UINT16_C(10)
#define ZDSE_PACK_SECTION_ASSETS UINT16_C(11)
#define ZDSE_PACK_SECTION_FONTS UINT16_C(12)
#define ZDSE_PACK_SECTION_GLYPHS UINT16_C(13)
#define ZDSE_PACK_SECTION_CUSTOM_PARAMETERS UINT16_C(14)
#define ZDSE_PACK_SECTION_DATA UINT16_C(15)
#define ZDSE_PACK_SECTION_DEBUG UINT16_C(0x8000)

#define ZDSE_PACK_QUERY_SCRATCH_BYTES ((size_t)256)
#define ZDSE_PACK_QUERY_SCRATCH_ALIGN ((size_t)4)

struct zdse_pack_info_v2 {
  uint16_t struct_size;
  uint16_t abi_version;
  uint64_t required_capabilities;
  uint64_t optional_capabilities;
  uint16_t format_major;
  uint16_t format_minor;
  uint32_t total_size;
  uint32_t section_count;
  uint32_t required_features;
  uint32_t optional_features;
  uint32_t root_scene_id;
  uint32_t crc32;
  uint32_t read_calls;
  uint32_t map_calls;
  uint32_t bytes_read;
  uint32_t reserved[4];
};

#define ZDSE_PACK_INFO_V2_REQUIRED_SIZE                                      \
  ((uint16_t)offsetof(struct zdse_pack_info_v2, reserved))
#define ZDSE_PACK_INFO_V2_INIT                                               \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct zdse_pack_info_v2),                \
    .abi_version = ZDSE_ABI_VERSION_V2_0                                     \
  }

size_t zdse_pack_query_scratch_size(void);
size_t zdse_pack_query_scratch_align(void);

zdse_result_t zdse_pack_validate(const struct zdse_asset_source_v2 *source,
                                 void *query_scratch,
                                 size_t query_scratch_size,
                                 struct zdse_pack_info_v2 *out_info);
