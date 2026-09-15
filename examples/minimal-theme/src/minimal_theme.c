/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/theme.h>

static int width, height, page;

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
