/* SPDX-License-Identifier: MIT */
#include "internal.h"

#include <stdint.h>

#include <tre/render.h>

static int32_t maximum(int32_t left, int32_t right) {
  return left > right ? left : right;
}

static int32_t minimum(int32_t left, int32_t right) {
  return left < right ? left : right;
}

tre_result_t tre_render_begin(struct tre_render_ctx *ctx,
                              struct tre_surface *surface) {
  if (!ctx || !surface)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      ctx->abi_version, ctx->struct_size, TRE_RENDER_CTX_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  result = tre_surface_validate(surface);
  if (result != TRE_OK)
    return result;
  ctx->surface = surface;
  ctx->clip = (struct tre_rect){surface->origin_x, surface->origin_y,
                               surface->width, surface->height};
  ctx->flags = 0;
  return TRE_OK;
}
tre_result_t tre_render_set_clip(struct tre_render_ctx *ctx,
                                 const struct tre_rect *clip) {
  if (!ctx || !ctx->surface || !clip)
    return TRE_ERR_INVALID_ARGUMENT;
  int32_t left = maximum(clip->x, ctx->surface->origin_x);
  int32_t top = maximum(clip->y, ctx->surface->origin_y);
  int32_t right = minimum((int32_t)clip->x + clip->width,
                          (int32_t)ctx->surface->origin_x +
                              ctx->surface->width);
  int32_t bottom = minimum((int32_t)clip->y + clip->height,
                           (int32_t)ctx->surface->origin_y +
                               ctx->surface->height);
  if (right <= left || bottom <= top) {
    ctx->clip = (struct tre_rect){(int16_t)left, (int16_t)top, 0, 0};
    return TRE_OK;
  }
  ctx->clip = (struct tre_rect){(int16_t)left, (int16_t)top,
                               (uint16_t)(right - left),
                               (uint16_t)(bottom - top)};
  return TRE_OK;
}

tre_result_t tre_render_reset_clip(struct tre_render_ctx *ctx) {
  if (!ctx || !ctx->surface)
    return TRE_ERR_INVALID_ARGUMENT;
  ctx->clip = (struct tre_rect){ctx->surface->origin_x, ctx->surface->origin_y,
                               ctx->surface->width, ctx->surface->height};
  return TRE_OK;
}

tre_result_t tre_draw_pixel565(struct tre_render_ctx *ctx, int32_t x,
                               int32_t y, uint16_t color, uint8_t opacity) {
  if (!ctx || !ctx->surface)
    return TRE_ERR_INVALID_ARGUMENT;
  uint32_t offset;
  if (!tre_context_pixel_offset(ctx, x, y, &offset) || !opacity)
    return TRE_OK;
  uint8_t *pixel = ctx->surface->pixels + offset;
  uint16_t output =
      tre_blend565(tre_load565(pixel), color, opacity);
  tre_store565(pixel, output);
  return TRE_OK;
}

tre_result_t tre_fill_rect(struct tre_render_ctx *ctx,
                           const struct tre_rect *rect, uint16_t color,
                           uint8_t opacity) {
  if (!ctx || !ctx->surface || !rect)
    return TRE_ERR_INVALID_ARGUMENT;
  int32_t left = maximum(rect->x, ctx->clip.x);
  int32_t top = maximum(rect->y, ctx->clip.y);
  int32_t right = minimum((int32_t)rect->x + rect->width,
                          (int32_t)ctx->clip.x + ctx->clip.width);
  int32_t bottom = minimum((int32_t)rect->y + rect->height,
                           (int32_t)ctx->clip.y + ctx->clip.height);
  for (int32_t y = top; y < bottom; y++)
    for (int32_t x = left; x < right; x++)
      tre_draw_pixel565(ctx, x, y, color, opacity);
  return TRE_OK;
}

tre_result_t tre_draw_line(struct tre_render_ctx *ctx, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, uint16_t thickness,
                           uint16_t color, uint8_t opacity) {
  if (!ctx || !ctx->surface || !thickness)
    return TRE_ERR_INVALID_ARGUMENT;
  int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int32_t sx = x0 < x1 ? 1 : -1;
  int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int32_t sy = y0 < y1 ? 1 : -1;
  int32_t error = dx + dy;
  int32_t before = (int32_t)(thickness - 1u) / 2;
  int32_t after = (int32_t)thickness - before;
  for (;;) {
    struct tre_rect brush = {(int16_t)(x0 - before), (int16_t)(y0 - before),
                             (uint16_t)(before + after),
                             (uint16_t)(before + after)};
    tre_fill_rect(ctx, &brush, color, opacity);
    if (x0 == x1 && y0 == y1)
      break;
    int32_t doubled = error * 2;
    if (doubled >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled <= dx) {
      error += dx;
      y0 += sy;
    }
  }
  return TRE_OK;
}
