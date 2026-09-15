/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/theme.h>
static uint16_t pixels[DTE_MAX_PIXELS];
static struct dte_snapshot state;
static int width = 280, height = 240;
static char name_buffer[24];
static int touch_down, touch_x, touch_y, touch_moved, touch_long;
static uint32_t touch_start;
/* Weak optional hooks preserve the original static theme descriptor and ABI. */
__attribute__((weak)) int dte_theme_animation_options(void) { return 0; }
__attribute__((weak)) int dte_theme_animation_set(int kind) { (void)kind; return 0; }
__attribute__((weak)) int dte_theme_animation_get(void) { return 0; }
int dte_animation_options(void) { return dte_theme_animation_options(); }
int dte_set_animation(int kind) { return dte_theme_animation_set(kind); }
int dte_get_animation(void) { return dte_theme_animation_get(); }
__attribute__((weak)) int dte_theme_duration_set(int ms) { (void)ms; return 0; }
__attribute__((weak)) int dte_theme_duration_get(void) { return 400; }
int dte_set_animation_duration(int ms) { return dte_theme_duration_set(ms); }
int dte_get_animation_duration(void) { return dte_theme_duration_get(); }
__attribute__((weak)) void dte_theme_force_redraw(void) {}
void dte_force_redraw(void) { dte_theme_force_redraw(); }
char *dte_name_buffer(void) { return name_buffer; }
static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}
void dte_init(int w, int h) {
  if (!((w == 280 && h == 240) || (w == 240 && (h == 280 || h == 240))))
    return;
  width = w;
  height = h;
  dte_touch_cancel();
  state = (struct dte_snapshot){.battery_left = -1,
                                .battery_right = -1,
                                .battery_dongle = -1,
                                .refresh_rate_x10 = -1,
                                .connected_left = -1,
                               .connected_right = -1};
  state.battery_count = 3;
  dte_set_layer_name("BASE");
  if (dte_selected_theme.abi_version == DTE_ABI_VERSION)
    dte_selected_theme.mount(w, h, 0);
}
void dte_set_state(int w, int l, int e, int p, int m, int bl, int br, int bd,
                   int lc, int rc) {
  state.wpm = clamp(w, 0, 999);
  state.layer = clamp(l, 0, 99);
  state.endpoint = clamp(e, 0, 2);
  state.profile = clamp(p, 0, 99);
  state.modifiers = m & 255;
  state.battery_left = clamp(bl, -1, 100);
  state.battery_right = clamp(br, -1, 100);
  state.battery_dongle = clamp(bd, -1, 100);
  state.connected_left = clamp(lc, -1, 1);
  state.connected_right = clamp(rc, -1, 1);
}
void dte_set_layer_name(const char *name) {
  int i = 0;
  if (name)
    for (; name[i] && i < 23; i++)
      state.layer_name[i] = name[i];
  state.layer_name[i] = 0;
}
void dte_set_battery_count(int count){if(count==2||count==3)state.battery_count=count;}
void dte_set_display_stats(int backlight, int refresh_rate_x10) {
  state.backlight=clamp(backlight,0,100);
  state.refresh_rate_x10=clamp(refresh_rate_x10,-1,9999);
}
void dte_set_startup_phase(int phase) { state.startup_phase=clamp(phase,0,2); }
void dte_gesture(int kind, uint32_t now) {
  if (kind >= 1 && kind <= 6)
    dte_selected_theme.gesture(kind, now);
}
void dte_touch_cancel(void) { touch_down = touch_moved = touch_long = 0; }
int dte_touch_hint(int kind, uint32_t now) {
  if (kind < DTE_TAP || kind > DTE_LONG_PRESS || touch_long)
    return 0;
  touch_long = 1; /* Suppress software recognition and duplicate controller hints. */
  dte_gesture(kind, now);
  return 1;
}
void dte_touch(int x, int y, int down, uint32_t now) {
  dte_touch_at(x,y,down,now,now);
}
void dte_touch_at(int x, int y, int down, uint32_t now, uint32_t animation_time) {
  if (down && !touch_down) {
    touch_down = 1;
    touch_x = x;
    touch_y = y;
    touch_start = now;
    touch_moved = touch_long = 0;
    return;
  }
  if (!touch_down)
    return;
  int dx = x - touch_x, dy = y - touch_y, ax = dx < 0 ? -dx : dx,
      ay = dy < 0 ? -dy : dy;
  if (ax > 10 || ay > 10)
    touch_moved = 1;
  if (!down) {
    touch_down = 0;
    if (touch_long)
      return;
    if (ax > 28 && ax > ay)
      dte_gesture(dx < 0 ? DTE_LEFT : DTE_RIGHT, animation_time);
    else if (ay > 28 && ay > ax)
      dte_gesture(dy < 0 ? DTE_UP : DTE_DOWN, animation_time);
    else if (!touch_moved && (uint32_t)(now - touch_start) < 600)
      dte_gesture(DTE_TAP, animation_time);
    else if (!touch_moved)
      dte_gesture(DTE_LONG_PRESS, animation_time);
  }
}
int dte_render(uint32_t now) {
  if (touch_down && !touch_moved && !touch_long &&
      (uint32_t)(now - touch_start) >= 600) {
    touch_long = 1;
    dte_gesture(DTE_LONG_PRESS, now);
  }
  return dte_selected_theme.render(&state, now, pixels);
}
uint16_t *dte_pixels(void) { return pixels; }
int dte_width(void) { return width; }
int dte_height(void) { return height; }
uint32_t dte_hash(void) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < width * height; i++) {
    h = (h ^ (pixels[i] & 255)) * 16777619u;
    h = (h ^ (pixels[i] >> 8)) * 16777619u;
  }
  return h;
}
