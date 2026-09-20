/* SPDX-License-Identifier: MIT */
#include "internal.h"

#include <stddef.h>
#include <stdint.h>

#include <tre/surface.h>

tre_result_t tre_validate_record(uint16_t version, uint16_t size,
                                 uint16_t required_size) {
  if (TRE_ABI_VERSION_MAJOR(version) !=
      TRE_ABI_VERSION_MAJOR(TRE_ABI_VERSION))
    return TRE_ERR_UNSUPPORTED_ABI;
  if (size < required_size)
    return TRE_ERR_STRUCT_TOO_SMALL;
  return TRE_OK;
}

tre_result_t tre_surface_validate(const struct tre_surface *surface) {
  if (!surface)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      surface->abi_version, surface->struct_size, TRE_SURFACE_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  if (!surface->pixels || !surface->scene_width || !surface->scene_height ||
      !surface->width || !surface->height)
    return TRE_ERR_INVALID_ARGUMENT;
  if (surface->scene_width > INT16_MAX || surface->scene_height > INT16_MAX)
    return TRE_ERR_OUT_OF_BOUNDS;
  if (surface->pixel_format != TRE_PIXEL_FORMAT_RGB565_LE)
    return TRE_ERR_UNSUPPORTED_FORMAT;
  if (surface->flags || surface->reserved0 || surface->origin_x < 0 ||
      surface->origin_y < 0)
    return TRE_ERR_INVALID_ARGUMENT;
  uint32_t right = (uint32_t)(uint16_t)surface->origin_x + surface->width;
  uint32_t bottom = (uint32_t)(uint16_t)surface->origin_y + surface->height;
  if (right > surface->scene_width || bottom > surface->scene_height)
    return TRE_ERR_OUT_OF_BOUNDS;
  uint32_t row_bytes = (uint32_t)surface->width * 2u;
  if (surface->stride_bytes < row_bytes)
    return TRE_ERR_OUT_OF_BOUNDS;
  uint64_t required = (uint64_t)(surface->height - 1u) * surface->stride_bytes +
                      row_bytes;
  if (required > surface->buffer_size)
    return TRE_ERR_CAPACITY;
  return TRE_OK;
}

uint16_t tre_load565(const uint8_t *bytes) {
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

void tre_store565(uint8_t *bytes, uint16_t color) {
  bytes[0] = (uint8_t)color;
  bytes[1] = (uint8_t)(color >> 8);
}

uint16_t tre_blend565(uint16_t destination, uint16_t source, uint8_t opacity) {
  if (opacity == 255)
    return source;
  if (!opacity)
    return destination;
  uint32_t inverse = 255u - opacity;
  uint32_t red = (((source >> 11) * opacity + (destination >> 11) * inverse +
                   127u) /
                  255u) &
                 31u;
  uint32_t green = ((((source >> 5) & 63u) * opacity +
                     ((destination >> 5) & 63u) * inverse + 127u) /
                    255u) &
                   63u;
  uint32_t blue = (((source & 31u) * opacity + (destination & 31u) * inverse +
                    127u) /
                   255u) &
                  31u;
  return (uint16_t)((red << 11) | (green << 5) | blue);
}

bool tre_context_pixel_offset(const struct tre_render_ctx *ctx, int32_t x,
                              int32_t y, uint32_t *offset) {
  if (!ctx || !ctx->surface || !offset)
    return false;
  const struct tre_surface *surface = ctx->surface;
  int32_t clip_right = (int32_t)ctx->clip.x + ctx->clip.width;
  int32_t clip_bottom = (int32_t)ctx->clip.y + ctx->clip.height;
  int32_t surface_right = (int32_t)surface->origin_x + surface->width;
  int32_t surface_bottom = (int32_t)surface->origin_y + surface->height;
  if (x < ctx->clip.x || x >= clip_right || y < ctx->clip.y ||
      y >= clip_bottom || x < surface->origin_x || x >= surface_right ||
      y < surface->origin_y || y >= surface_bottom)
    return false;
  *offset = (uint32_t)(y - surface->origin_y) * surface->stride_bytes +
            (uint32_t)(x - surface->origin_x) * 2u;
  return true;
}
