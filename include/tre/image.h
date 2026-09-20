/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tre/render.h>

#define TRE_IMAGE_RGB565_LE UINT8_C(1)
#define TRE_IMAGE_RGB565_KEYED_LE UINT8_C(2)
#define TRE_IMAGE_RGB565_A1_MSB UINT8_C(3)
#define TRE_IMAGE_MONO1_MSB UINT8_C(4)
#define TRE_IMAGE_A4_MSB UINT8_C(5)

#define TRE_DRAW_FLIP_X (UINT32_C(1) << 0)
#define TRE_DRAW_FLIP_Y (UINT32_C(1) << 1)
#define TRE_DRAW_KNOWN_FLAGS (TRE_DRAW_FLIP_X | TRE_DRAW_FLIP_Y)

struct tre_image {
  uint16_t struct_size;
  uint16_t abi_version;
  uint16_t width;
  uint16_t height;
  uint32_t stride_bytes;
  uint8_t format;
  uint8_t flags;
  uint16_t transparent_key;
  uint32_t data_size;
  const uint8_t *data;
  uint32_t mask_stride_bytes;
  uint32_t mask_size;
  const uint8_t *mask;
  uint32_t reserved[4];
};

#define TRE_IMAGE_REQUIRED_SIZE                                               \
  ((uint16_t)offsetof(struct tre_image, reserved))
#define TRE_IMAGE_INIT                                                        \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct tre_image),                        \
    .abi_version = TRE_ABI_VERSION                                            \
  }

struct tre_glyph {
  uint16_t struct_size;
  uint16_t abi_version;
  const struct tre_image *coverage;
  int16_t bearing_x;
  int16_t bearing_y;
  int16_t advance;
  uint16_t reserved0;
  uint32_t reserved[4];
};

#define TRE_GLYPH_REQUIRED_SIZE                                               \
  ((uint16_t)offsetof(struct tre_glyph, reserved))
#define TRE_GLYPH_INIT                                                        \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct tre_glyph),                        \
    .abi_version = TRE_ABI_VERSION                                            \
  }

tre_result_t tre_image_validate(const struct tre_image *image);
tre_result_t tre_draw_image(struct tre_render_ctx *ctx,
                            const struct tre_image *image,
                            const struct tre_rect *source, int32_t destination_x,
                            int32_t destination_y, uint16_t color,
                            uint8_t opacity, uint32_t flags);
tre_result_t tre_draw_glyph(struct tre_render_ctx *ctx,
                            const struct tre_glyph *glyph, int32_t pen_x,
                            int32_t baseline_y, uint16_t color,
                            uint8_t opacity);
