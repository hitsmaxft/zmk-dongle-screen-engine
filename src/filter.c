/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/filter.h>
#include <stdbool.h>

static const uint8_t edge_opa[12] = {150,112,82,60,44,32,24,18,13,9,5,3};

static uint16_t darken(uint16_t pixel, uint8_t opacity) {
  uint32_t keep = 255u - opacity;
  uint32_t r = ((pixel >> 11) & 31u) * keep / 255u;
  uint32_t g = ((pixel >> 5) & 63u) * keep / 255u;
  uint32_t b = (pixel & 31u) * keep / 255u;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t mix565(uint16_t pixel, uint16_t target, uint8_t opacity) {
  uint32_t keep = 255u - opacity;
  uint32_t r = ((((pixel >> 11) & 31u) * keep) +
                (((target >> 11) & 31u) * opacity) + 127u) / 255u;
  uint32_t g = ((((pixel >> 5) & 63u) * keep) +
                (((target >> 5) & 63u) * opacity) + 127u) / 255u;
  uint32_t b = (((pixel & 31u) * keep) + ((target & 31u) * opacity) + 127u) /
               255u;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static int scale(int value, int short_side) {
  int scaled = (value * short_side + 120) / 240;
  return scaled < 1 ? 1 : scaled;
}

static uint8_t edge_opacity(int x, int y, int width, int height, int side,
                            int top, int bottom) {
  int left_d = x, right_d = width - 1 - x;
  int top_d = y, bottom_d = height - 1 - y;
  uint8_t opacity = 0;
  if (left_d < side && left_d < 12) opacity = edge_opa[left_d];
  if (right_d < side && right_d < 12 && edge_opa[right_d] > opacity)
    opacity = edge_opa[right_d];
  if (top_d < top && top_d < 12 && edge_opa[top_d] > opacity)
    opacity = edge_opa[top_d];
  if (bottom_d < bottom && bottom_d < 12) {
    uint32_t boosted = (uint32_t)edge_opa[bottom_d] * 115u / 100u;
    if (boosted > 255u) boosted = 255u;
    if (boosted > opacity) opacity = (uint8_t)boosted;
  }
  return opacity;
}

uint32_t dte_filter_apply(uint8_t type, const struct dte_canvas *canvas) {
  if (type == DTE_FILTER_NONE || !canvas || !canvas->pixels) return 0;
  if (type != DTE_FILTER_CRT) return 0;
  int width = canvas->scene_width, height = canvas->scene_height;
  int short_side = width < height ? width : height;
  int side = scale(10, short_side), top = scale(8, short_side);
  int bottom = scale(12, short_side), radius = scale(18, short_side);
  int highlight_y0 = scale(3, short_side), highlight_y1 = scale(4, short_side);
  int highlight_inset = scale(22, short_side);
  uint16_t highlight = (uint16_t)(((0xa8u >> 3) << 11) |
                                  ((0xffu >> 2) << 5) | (0xd0u >> 3));
  uint32_t inspected = 0;
  for (int ly = 0; ly < canvas->height; ly++) {
    int y = canvas->origin_y + ly;
    for (int lx = 0; lx < canvas->width; lx++) {
      int x = canvas->origin_x + lx;
      bool in_highlight = y >= highlight_y0 && y <= highlight_y1 &&
                          x >= highlight_inset && x < width - highlight_inset;
      bool in_edge = x < side || x >= width - side || y < top ||
                     y >= height - bottom;
      bool in_corner = (x < radius || x >= width - radius) &&
                       (y < radius || y >= height - radius);
      if (!in_edge && !in_corner && !in_highlight) continue;
      inspected++;
      uint16_t *pixel = &canvas->pixels[ly * canvas->stride_pixels + lx];
      if (in_corner) {
        int cx = x < radius ? radius - 1 : width - radius;
        int cy = y < radius ? radius - 1 : height - radius;
        int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > (radius - 1) * (radius - 1)) {
          *pixel = 0;
          continue;
        }
      }
      uint8_t opacity = edge_opacity(x, y, width, height, side, top, bottom);
      if (opacity) *pixel = darken(*pixel, opacity);
      if (in_highlight) {
        int span = width - 2 * highlight_inset;
        int from_end = x - highlight_inset;
        int other = width - highlight_inset - 1 - x;
        if (other < from_end) from_end = other;
        int ramp = scale(24, short_side);
        uint8_t light_opa = (uint8_t)(18 * (from_end < ramp ? from_end : ramp) /
                                      (ramp ? ramp : 1));
        if (span > 0 && light_opa) *pixel = mix565(*pixel, highlight, light_opa);
      }
    }
  }
  return inspected;
}
