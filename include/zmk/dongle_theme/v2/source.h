/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zmk/dongle_theme/v2/result.h>

#define ZDSE_ASSET_SOURCE_CAP_MAP (UINT64_C(1) << 0)
#define ZDSE_ASSET_SOURCE_KNOWN_CAPABILITIES ZDSE_ASSET_SOURCE_CAP_MAP

typedef zdse_result_t (*zdse_asset_read_fn)(void *user_data, uint32_t offset,
                                            void *destination,
                                            uint32_t length);
typedef const uint8_t *(*zdse_asset_map_fn)(void *user_data, uint32_t offset,
                                            uint32_t length);
typedef void (*zdse_asset_unmap_fn)(void *user_data, const uint8_t *pointer,
                                    uint32_t length);

struct zdse_asset_source_v2 {
  uint16_t struct_size;
  uint16_t abi_version;
  uint64_t required_capabilities;
  uint64_t optional_capabilities;
  void *user_data;
  zdse_asset_read_fn read;
  zdse_asset_map_fn map;
  zdse_asset_unmap_fn unmap;
  uint32_t reserved[4];
};

#define ZDSE_ASSET_SOURCE_V2_REQUIRED_SIZE                                    \
  ((uint16_t)offsetof(struct zdse_asset_source_v2, reserved))
#define ZDSE_ASSET_SOURCE_V2_INIT                                             \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct zdse_asset_source_v2),             \
    .abi_version = ZDSE_ABI_VERSION_V2_0                                     \
  }
