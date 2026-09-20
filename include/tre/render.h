/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tre/surface.h>

struct tre_render_ctx {
  uint16_t struct_size;
  uint16_t abi_version;
  struct tre_surface *surface;
  struct tre_rect clip;
  uint32_t flags;
  uint32_t reserved[4];
};

#define TRE_RENDER_CTX_REQUIRED_SIZE                                          \
  ((uint16_t)offsetof(struct tre_render_ctx, reserved))
#define TRE_RENDER_CTX_INIT                                                   \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct tre_render_ctx),                   \
    .abi_version = TRE_ABI_VERSION                                            \
  }

tre_result_t tre_render_begin(struct tre_render_ctx *ctx,
                              struct tre_surface *surface);
tre_result_t tre_render_set_clip(struct tre_render_ctx *ctx,
                                 const struct tre_rect *clip);
tre_result_t tre_render_reset_clip(struct tre_render_ctx *ctx);
tre_result_t tre_draw_pixel565(struct tre_render_ctx *ctx, int32_t x,
                               int32_t y, uint16_t color, uint8_t opacity);
tre_result_t tre_fill_rect(struct tre_render_ctx *ctx,
                           const struct tre_rect *rect, uint16_t color,
                           uint8_t opacity);
tre_result_t tre_draw_line(struct tre_render_ctx *ctx, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, uint16_t thickness,
                           uint16_t color, uint8_t opacity);
