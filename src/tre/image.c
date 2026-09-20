/* SPDX-License-Identifier: MIT */
#include "internal.h"

#include <stdint.h>

#include <tre/image.h>

static uint32_t minimum_stride(const struct tre_image *image) {
  switch (image->format) {
  case TRE_IMAGE_RGB565_LE:
  case TRE_IMAGE_RGB565_KEYED_LE:
  case TRE_IMAGE_RGB565_A1_MSB:
    return (uint32_t)image->width * 2u;
  case TRE_IMAGE_MONO1_MSB:
    return ((uint32_t)image->width + 7u) / 8u;
  case TRE_IMAGE_A4_MSB:
    return ((uint32_t)image->width + 1u) / 2u;
  default:
    return 0;
  }
}

static int storage_fits(uint16_t height, uint32_t stride, uint32_t row_bytes,
                        uint32_t size) {
  if (!height || stride < row_bytes)
    return 0;
  uint64_t required = (uint64_t)(height - 1u) * stride + row_bytes;
  return required <= size;
}

tre_result_t tre_image_validate(const struct tre_image *image) {
  if (!image)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      image->abi_version, image->struct_size, TRE_IMAGE_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  uint32_t row_bytes = minimum_stride(image);
  if (!image->width || !image->height || !row_bytes || !image->data)
    return row_bytes ? TRE_ERR_INVALID_ARGUMENT : TRE_ERR_UNSUPPORTED_FORMAT;
  if (image->width > INT16_MAX || image->height > INT16_MAX)
    return TRE_ERR_OUT_OF_BOUNDS;
  if (image->flags ||
      !storage_fits(image->height, image->stride_bytes, row_bytes,
                    image->data_size))
    return TRE_ERR_OUT_OF_BOUNDS;
  if (image->format == TRE_IMAGE_RGB565_A1_MSB) {
    uint32_t mask_row = ((uint32_t)image->width + 7u) / 8u;
    if (!image->mask ||
        !storage_fits(image->height, image->mask_stride_bytes, mask_row,
                      image->mask_size))
      return TRE_ERR_OUT_OF_BOUNDS;
  } else if (image->mask || image->mask_size || image->mask_stride_bytes) {
    return TRE_ERR_INVALID_ARGUMENT;
  }
  return TRE_OK;
}

static uint8_t sample_bit(const uint8_t *data, uint32_t stride, uint32_t x,
                          uint32_t y) {
  return (uint8_t)((data[y * stride + x / 8u] >> (7u - x % 8u)) & 1u);
}

static uint8_t sample_a4(const struct tre_image *image, uint32_t x,
                         uint32_t y) {
  uint8_t packed = image->data[y * image->stride_bytes + x / 2u];
  uint8_t value = (x & 1u) ? packed & 15u : packed >> 4;
  return (uint8_t)(value * 17u);
}

static tre_result_t draw_rgb565_opaque(struct tre_render_ctx *ctx,
                                       const struct tre_image *image,
                                       const struct tre_rect *source,
                                       int32_t destination_x,
                                       int32_t destination_y,
                                       uint32_t flags) {
  int64_t first_x = (int64_t)ctx->clip.x - destination_x;
  int64_t first_y = (int64_t)ctx->clip.y - destination_y;
  int64_t last_x =
      (int64_t)ctx->clip.x + ctx->clip.width - destination_x;
  int64_t last_y =
      (int64_t)ctx->clip.y + ctx->clip.height - destination_y;
  if (first_x < 0)
    first_x = 0;
  if (first_y < 0)
    first_y = 0;
  if (last_x > source->width)
    last_x = source->width;
  if (last_y > source->height)
    last_y = source->height;
  if (first_x >= last_x || first_y >= last_y)
    return TRE_OK;

  const struct tre_surface *surface = ctx->surface;
  for (int32_t dy = (int32_t)first_y; dy < (int32_t)last_y; dy++) {
    uint32_t relative_y =
        (flags & TRE_DRAW_FLIP_Y) ? source->height - 1u - (uint32_t)dy
                                  : (uint32_t)dy;
    uint32_t sy = (uint32_t)(uint16_t)source->y + relative_y;
    uint8_t *destination =
        surface->pixels +
        (uint32_t)(destination_y + dy - surface->origin_y) *
            surface->stride_bytes +
        (uint32_t)(destination_x + first_x - surface->origin_x) * 2u;
    for (int32_t dx = (int32_t)first_x; dx < (int32_t)last_x;
         dx++, destination += 2) {
      uint32_t relative_x =
          (flags & TRE_DRAW_FLIP_X) ? source->width - 1u - (uint32_t)dx
                                    : (uint32_t)dx;
      uint32_t sx = (uint32_t)(uint16_t)source->x + relative_x;
      uint16_t sample =
          tre_load565(image->data + sy * image->stride_bytes + sx * 2u);
      if (image->format != TRE_IMAGE_RGB565_KEYED_LE ||
          sample != image->transparent_key)
        tre_store565(destination, sample);
    }
  }
  return TRE_OK;
}

tre_result_t tre_draw_image(struct tre_render_ctx *ctx,
                            const struct tre_image *image,
                            const struct tre_rect *source, int32_t destination_x,
                            int32_t destination_y, uint16_t color,
                            uint8_t opacity, uint32_t flags) {
  if (!ctx || !ctx->surface || !source)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_image_validate(image);
  if (result != TRE_OK)
    return result;
  if ((flags & ~TRE_DRAW_KNOWN_FLAGS) || !source->width || !source->height ||
      source->x < 0 || source->y < 0 ||
      (uint32_t)(uint16_t)source->x + source->width > image->width ||
      (uint32_t)(uint16_t)source->y + source->height > image->height)
    return TRE_ERR_OUT_OF_BOUNDS;
  if ((int64_t)destination_x + source->width > INT32_MAX ||
      (int64_t)destination_y + source->height > INT32_MAX)
    return TRE_ERR_OUT_OF_BOUNDS;

  if (opacity == 255 && (image->format == TRE_IMAGE_RGB565_LE ||
                         image->format == TRE_IMAGE_RGB565_KEYED_LE))
    return draw_rgb565_opaque(ctx, image, source, destination_x, destination_y,
                              flags);

  for (uint32_t dy = 0; dy < source->height; dy++) {
    uint32_t relative_y = (flags & TRE_DRAW_FLIP_Y)
                              ? source->height - 1u - dy
                              : dy;
    uint32_t sy = (uint32_t)(uint16_t)source->y + relative_y;
    for (uint32_t dx = 0; dx < source->width; dx++) {
      uint32_t relative_x = (flags & TRE_DRAW_FLIP_X)
                                ? source->width - 1u - dx
                                : dx;
      uint32_t sx = (uint32_t)(uint16_t)source->x + relative_x;
      uint16_t sample = color;
      uint8_t coverage = opacity;
      if (image->format == TRE_IMAGE_RGB565_LE ||
          image->format == TRE_IMAGE_RGB565_KEYED_LE ||
          image->format == TRE_IMAGE_RGB565_A1_MSB) {
        sample = tre_load565(image->data + sy * image->stride_bytes + sx * 2u);
        if (image->format == TRE_IMAGE_RGB565_KEYED_LE &&
            sample == image->transparent_key)
          coverage = 0;
        if (image->format == TRE_IMAGE_RGB565_A1_MSB &&
            !sample_bit(image->mask, image->mask_stride_bytes, sx, sy))
          coverage = 0;
      } else if (image->format == TRE_IMAGE_MONO1_MSB) {
        if (!sample_bit(image->data, image->stride_bytes, sx, sy))
          coverage = 0;
      } else {
        uint8_t a4 = sample_a4(image, sx, sy);
        coverage = (uint8_t)(((uint16_t)coverage * a4 + 127u) / 255u);
      }
      if (coverage)
        tre_draw_pixel565(ctx, destination_x + (int32_t)dx,
                          destination_y + (int32_t)dy, sample, coverage);
    }
  }
  return TRE_OK;
}

tre_result_t tre_draw_glyph(struct tre_render_ctx *ctx,
                            const struct tre_glyph *glyph, int32_t pen_x,
                            int32_t baseline_y, uint16_t color,
                            uint8_t opacity) {
  if (!glyph)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      glyph->abi_version, glyph->struct_size, TRE_GLYPH_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  result = tre_image_validate(glyph->coverage);
  if (result != TRE_OK)
    return result;
  if (glyph->coverage->format != TRE_IMAGE_MONO1_MSB &&
      glyph->coverage->format != TRE_IMAGE_A4_MSB)
    return TRE_ERR_UNSUPPORTED_FORMAT;
  struct tre_rect source = {0, 0, glyph->coverage->width,
                            glyph->coverage->height};
  return tre_draw_image(ctx, glyph->coverage, &source,
                        pen_x + glyph->bearing_x,
                        baseline_y - glyph->bearing_y, color, opacity, 0);
}
