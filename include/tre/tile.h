/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tre/image.h>

struct tre_tileset {
  uint16_t struct_size;
  uint16_t abi_version;
  const struct tre_image *image;
  uint16_t tile_width;
  uint16_t tile_height;
  uint32_t reserved[4];
};

#define TRE_TILESET_REQUIRED_SIZE                                             \
  ((uint16_t)offsetof(struct tre_tileset, reserved))
#define TRE_TILESET_INIT                                                      \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct tre_tileset),                      \
    .abi_version = TRE_ABI_VERSION                                            \
  }

tre_result_t tre_tileset_validate(const struct tre_tileset *tileset);
tre_result_t tre_draw_tile(struct tre_render_ctx *ctx,
                           const struct tre_tileset *tileset,
                           uint32_t tile_index, int32_t destination_x,
                           int32_t destination_y, uint16_t color,
                           uint8_t opacity, uint32_t flags);
