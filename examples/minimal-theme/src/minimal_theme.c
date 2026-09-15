/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/theme.h>

static int width, height, page;

static void level_arc(int cx, int cy, int radius, int first, int last,
                      int value, int reverse) {
  dtr_arc(cx, cy, radius, radius + 4, first, last, 38, 43, 47, 255);
  if (value < 0)
    return;
  int span = last - first;
  if (reverse)
    first = last - span * value / 100;
  else
    last = first + span * value / 100;
  dtr_arc(cx, cy, radius, radius + 4, first, last, 221, 226, 230, 255);
}

static void batteries(const struct dte_snapshot *s, int cx, int cy) {
  int radius = height / 2 - 16;
  if (s->battery_count == 2) {
    level_arc(cx, cy, radius, 122, 238, s->battery_left, 0);
    level_arc(cx, cy, radius, 302, 418, s->battery_right, 1);
    dtr_text("0", cx - radius - 7, cy, 10, 133, 143, 150, 255, 1);
    dtr_text("1", cx + radius + 7, cy, 10, 133, 143, 150, 255, 1);
    return;
  }
  level_arc(cx, cy, radius, 122, 238, s->battery_dongle, 0);
  level_arc(cx, cy, radius, 304, 354, s->battery_left, 1);
  level_arc(cx, cy, radius, 6, 56, s->battery_right, 1);
  dtr_text("D", cx - radius - 7, cy, 10, 133, 143, 150, 255, 1);
  dtr_text("0", cx + radius + 7, cy - 26, 10, 133, 143, 150, 255, 1);
  dtr_text("1", cx + radius + 7, cy + 26, 10, 133, 143, 150, 255, 1);
}

static void mount(int w, int h, uint32_t now) {
  (void)now;
  width = w;
  height = h;
  page = 0;
}

static void gesture(int kind, uint32_t now) {
  (void)now;
  if (kind == DTE_LEFT || kind == DTE_RIGHT || kind == DTE_TAP)
    page ^= 1;
}

static int render(const struct dte_snapshot *s, uint32_t now, uint16_t *pixels) {
  (void)now;
  dtr_begin(pixels, width, height);
  dtr_clear(8, 10, 12);
  int cx = width / 2, cy = height / 2;
  batteries(s, cx, cy);
  dtr_arc(cx, cy, 82, 86, 0, 360, 205, 212, 218, 255);
  dtr_arc(cx, cy, 70, 73, 210, 210 + s->wpm, page ? 224 : 108,
          page ? 92 : 174, page ? 72 : 220, 255);
  dtr_text(page ? "STATUS" : s->layer_name, cx, cy - 8, 20, 238, 241, 243,
           255, 1);
  dtr_text("SWIPE OR TAP", cx, cy + 22, 10, 145, 153, 160, 255, 1);
  return 0;
}

const struct dte_theme dte_selected_theme = {
    DTE_ABI_VERSION, "minimal", mount, gesture, render};
