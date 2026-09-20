/* SPDX-License-Identifier: MIT */
#include "internal.h"

#include <stdint.h>

#include <tre/tile.h>

tre_result_t tre_tileset_validate(const struct tre_tileset *tileset) {
  if (!tileset)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      tileset->abi_version, tileset->struct_size, TRE_TILESET_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  result = tre_image_validate(tileset->image);
  if (result != TRE_OK)
    return result;
  if (!tileset->tile_width || !tileset->tile_height ||
      tileset->tile_width > tileset->image->width ||
      tileset->tile_height > tileset->image->height)
    return TRE_ERR_OUT_OF_BOUNDS;
  return TRE_OK;
}

tre_result_t tre_draw_tile(struct tre_render_ctx *ctx,
                           const struct tre_tileset *tileset,
                           uint32_t tile_index, int32_t destination_x,
                           int32_t destination_y, uint16_t color,
                           uint8_t opacity, uint32_t flags) {
  tre_result_t result = tre_tileset_validate(tileset);
  if (result != TRE_OK)
    return result;
  uint32_t columns = tileset->image->width / tileset->tile_width;
  uint32_t rows = tileset->image->height / tileset->tile_height;
  if (!columns || tile_index >= columns * rows)
    return TRE_ERR_OUT_OF_BOUNDS;
  struct tre_rect source = {
      (int16_t)((tile_index % columns) * tileset->tile_width),
      (int16_t)((tile_index / columns) * tileset->tile_height),
      tileset->tile_width, tileset->tile_height};
  return tre_draw_image(ctx, tileset->image, &source, destination_x,
                        destination_y, color, opacity, flags);
}
