/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <zmk/dongle_theme/theme.h>
static int calls, last, origin_x, origin_y;
static uint32_t gesture_time;
static dte_result_t mount(int32_t w, int32_t h, uint32_t now) {
  (void)w;
  (void)h;
  (void)now;
  calls = last = 0;
  return DTE_STATUS_OK;
}
static void gesture(int kind, uint32_t now) {
  calls++;
  last = kind;
  gesture_time = now;
  origin_x = dte_gesture_x();
  origin_y = dte_gesture_y();
}
static dte_result_t frame(const struct dte_snapshot *s, uint32_t now,
                          struct dte_frame_result *result) {
  (void)s;
  (void)now;
  dte_frame_dirty_all(result, 280, 240);
  return DTE_STATUS_OK;
}
static dte_result_t draw(const struct dte_snapshot *s, uint32_t now,
                         const struct dte_canvas *canvas) {
  (void)s;
  (void)now;
  (void)canvas;
  return DTE_STATUS_OK;
}
const struct dte_theme dte_selected_theme =
    DTE_THEME_INIT("test", DTE_THEME_CAP_GESTURE, mount, gesture, frame, draw);
int main(void) {
  dte_init(280, 240);
  assert(dte_last_status() == DTE_STATUS_OK);
  assert(dte_active_abi_version() == DTE_ABI_VERSION_V1_2);
  dte_touch(180, 120, 1, 0);
  dte_touch(90, 120, 1, 40);
  dte_touch(90, 120, 0, 80);
  assert(calls == 1 && last == DTE_LEFT);
  assert(origin_x == 180 && origin_y == 120);
  assert(dte_gesture_x() == -1 && dte_gesture_y() == -1);
  dte_gesture(DTE_RIGHT, 90);
  assert(origin_x == -1 && origin_y == -1);
  assert(dte_gesture_x() == -1 && dte_gesture_y() == -1);
  dte_init(280, 240);
  dte_touch(120, 120, 1, 0);
  assert(dte_touch_hint(DTE_LEFT, 100));
  assert(!dte_touch_hint(DTE_LEFT, 101));
  dte_touch(120, 120, 0, 105); /* no release coordinates, must not emit a tap */
  assert(calls == 1 && last == DTE_LEFT);
  dte_touch(120, 120, 1, 150);
  dte_touch(120, 120, 0, 200);
  assert(calls == 2 && last == DTE_TAP); /* next contact remains independent */
  dte_init(280, 240);
  dte_touch(100, 100, 1, 0);
  dte_render(600);
  assert(calls == 1 && last == DTE_LONG_PRESS);
  assert(origin_x == 100 && origin_y == 100);
  assert(dte_gesture_x() == -1 && dte_gesture_y() == -1);
  assert(!dte_touch_hint(DTE_LONG_PRESS, 610));
  dte_touch(100, 100, 0, 620);
  assert(calls == 1);
  dte_init(280, 240);
  dte_touch_hint(DTE_RIGHT, 100);
  dte_touch(0, 0, 0, 110);
  assert(calls == 1 && last == DTE_RIGHT); /* release-only hardware gesture */
  dte_touch_cancel();
  dte_touch(100, 100, 1, 200);
  dte_touch_cancel();
  dte_touch(100, 100, 0, 300);
  assert(calls == 1); /* overflow/cancel */
  dte_init(280, 240);
  dte_touch_at(100, 100, 1, 100, 1000);
  dte_touch_at(100, 100, 0, 150, 1600);
  assert(calls == 1 && last == DTE_TAP && gesture_time == 1600);
  /* Classification uses the 50ms physical contact; animation starts at
   * dispatch. */
  return 0;
}
