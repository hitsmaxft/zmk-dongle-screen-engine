/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/adapter.h>
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/theme.h>

static int width, height, page;

static void separator_dot(int cx, int cy, int radius, int angle) {
  int x = cx + radius * dtr_trig(angle + 90) / 32767;
  int y = cy + radius * dtr_trig(angle) / 32767;
  dtr_pixel(x, y, 8, 10, 12, 255);
  dtr_pixel(x - 1, y, 8, 10, 12, 255);
  dtr_pixel(x + 1, y, 8, 10, 12, 255);
  dtr_pixel(x, y - 1, 8, 10, 12, 255);
  dtr_pixel(x, y + 1, 8, 10, 12, 255);
  dtr_pixel(x - 1, y - 1, 8, 10, 12, 150);
  dtr_pixel(x + 1, y - 1, 8, 10, 12, 150);
  dtr_pixel(x - 1, y + 1, 8, 10, 12, 150);
  dtr_pixel(x + 1, y + 1, 8, 10, 12, 150);
}

static void shell_ring(int cx, int cy, int radius) {
  dtr_arc(cx, cy, radius - 4, radius, 0, 360, 104, 111, 117, 255);
  dtr_arc_f(cx, cy, radius - 3, radius - 1.5f, 0, 360, 225, 229, 232, 255);
  dtr_arc_f(cx, cy, radius - 1.5f, radius, 0, 360, 73, 79, 84, 255);
}

static void batteries(const struct dte_snapshot *s, int cx, int cy,
                      int radius) {
  int count = s->battery_count == 2 ? 2 : 3;
  int values[3] = {s->battery_left, s->battery_right, -1};
  if (count == 3) {
    values[0] = s->battery_dongle;
    values[1] = s->battery_left;
    values[2] = s->battery_right;
  }
  int span = 360 / count;
  dtr_arc(cx, cy, radius - 3, radius + 3, 0, 360, 31, 37, 40, 255);
  for (int i = 0; i < count; i++) {
    int first = 270 + i * span + 3;
    int last = 270 + (i + 1) * span - 3;
    if (values[i] >= 0)
      dtr_arc(cx, cy, radius - 3, radius + 3, first,
              first + (last - first) * values[i] / 100, 86, 219, 133, 255);
    separator_dot(cx, cy, radius, 270 + i * span);
  }
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

static int render(const struct dte_snapshot *s, uint32_t now,
                  uint16_t *pixels) {
  (void)now;
  dtr_begin(pixels, width, height);
  dtr_clear(8, 10, 12);
  int cx = width / 2, cy = height / 2;
  int outer = (width < height ? width : height) / 2 - 5;
  shell_ring(cx, cy, outer);
  batteries(s, cx, cy, outer - 11);
  dtr_arc(cx, cy, outer - 23, outer - 17, 0, 360, 29, 35, 39, 255);
  dtr_arc(cx, cy, outer - 23, outer - 17, 210, 210 + s->wpm, page ? 224 : 108,
          page ? 92 : 174, page ? 72 : 220, 255);
  dtr_text(page ? "STATUS" : s->layer_name, cx, cy - 8, 20, 238, 241, 243, 255,
           1);
  dtr_text("SWIPE OR TAP", cx, cy + 22, 10, 145, 153, 160, 255, 1);
  return 0;
}

DTE_THEME_RASTER_ADAPTER("minimal", mount, gesture, render);
