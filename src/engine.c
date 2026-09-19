/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/theme.h>
#include <zmk/dongle_theme/transport.h>

#if defined(__wasm__)
void *memset(void *dst, int value, size_t count) {
  unsigned char *p = dst;
  for (size_t i = 0; i < count; i++)
    p[i] = (unsigned char)value;
  return dst;
}
void *memcpy(void *dst, const void *src, size_t count) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  for (size_t i = 0; i < count; i++)
    d[i] = s[i];
  return dst;
}
#endif

static void dte_memzero(void *dst, size_t count) {
  unsigned char *p = dst;
  for (size_t i = 0; i < count; i++)
    p[i] = 0;
}
static void dte_memcopy(void *dst, const void *src, size_t count) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  for (size_t i = 0; i < count; i++)
    d[i] = s[i];
}

#if !defined(__ZEPHYR__)
static uint16_t preview_pixels[DTE_MAX_PIXELS];
#endif
static struct dte_snapshot state;
static int width = 280, height = 240;
static char name_buffer[24];
static int touch_down, touch_x, touch_y, touch_moved, touch_long;
static int gesture_x = -1, gesture_y = -1;
static uint32_t touch_start, prepared_now;
static int frame_prepared;
static dte_result_t last_status = DTE_STATUS_NOT_INITIALIZED;
static uint32_t active_abi_version;
static void dispatch_pending_long_press(uint32_t now);

__attribute__((weak)) const struct dte_theme dte_selected_theme = {0};
__attribute__((weak)) int dte_theme_animation_options(void) { return 0; }
__attribute__((weak)) int dte_theme_animation_set(int kind) {
  (void)kind;
  return 0;
}
__attribute__((weak)) int dte_theme_animation_get(void) { return 0; }
int dte_animation_options(void) { return dte_theme_animation_options(); }
int dte_set_animation(int kind) { return dte_theme_animation_set(kind); }
int dte_get_animation(void) { return dte_theme_animation_get(); }
__attribute__((weak)) int dte_theme_duration_set(int ms) {
  (void)ms;
  return 0;
}
__attribute__((weak)) int dte_theme_duration_get(void) { return 400; }
int dte_set_animation_duration(int ms) { return dte_theme_duration_set(ms); }
int dte_get_animation_duration(void) { return dte_theme_duration_get(); }
__attribute__((weak)) void dte_theme_force_redraw(void) {}
void dte_force_redraw(void) { dte_theme_force_redraw(); }
__attribute__((weak)) void dte_host_backlight_set(int percent) {
  (void)percent;
}

static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}
static void copy_name(char dst[24], const char *src) {
  int i = 0;
  if (src)
    for (; src[i] && i < 23; i++)
      dst[i] = src[i];
  dst[i] = 0;
}
static void reset_state(void) {
  state = (struct dte_snapshot)DTE_SNAPSHOT_INIT;
  state.battery_left = state.battery_right = state.battery_dongle = -1;
  state.refresh_rate_x10 = -1;
  state.connected_left = state.connected_right = -1;
  state.battery_count = 3;
  copy_name(state.layer_name, "BASE");
}

dte_result_t dte_validate_theme(const struct dte_theme *theme) {
  if (!theme)
    return DTE_STATUS_INVALID_ARGUMENT;
  if (theme->abi_version != DTE_ABI_VERSION_V1_2)
    return DTE_STATUS_UNSUPPORTED_ABI;
  if (theme->struct_size < DTE_THEME_REQUIRED_SIZE)
    return DTE_STATUS_STRUCT_TOO_SMALL;
  if (!theme->id || !theme->id[0])
    return DTE_STATUS_INVALID_ARGUMENT;
  if (!theme->mount || !theme->frame || !theme->draw ||
      ((theme->capabilities & DTE_THEME_CAP_GESTURE) && !theme->gesture))
    return DTE_STATUS_MISSING_CALLBACK;
  return DTE_STATUS_OK;
}
dte_result_t dte_init_ex(int32_t w, int32_t h) {
  active_abi_version = 0;
  frame_prepared = 0;
  if (!((w == 280 && h == 240) || (w == 240 && (h == 280 || h == 240))))
    return last_status = DTE_STATUS_UNSUPPORTED_DISPLAY;
  width = w;
  height = h;
  dte_touch_cancel();
  gesture_x = gesture_y = -1;
  reset_state();
#if !defined(__ZEPHYR__)
  for (int i = 0; i < DTE_MAX_PIXELS; i++)
    preview_pixels[i] = 0;
#endif
  last_status = dte_validate_theme(&dte_selected_theme);
  if (last_status != DTE_STATUS_OK)
    return last_status;
  last_status = dte_selected_theme.mount(w, h, 0);
  if (last_status == DTE_STATUS_OK)
    active_abi_version = DTE_ABI_VERSION_V1_2;
  return last_status;
}
void dte_init(int w, int h) { (void)dte_init_ex(w, h); }
dte_result_t dte_last_status(void) { return last_status; }
uint32_t dte_active_abi_version(void) { return active_abi_version; }
int dte_width(void) { return width; }
int dte_height(void) { return height; }
char *dte_name_buffer(void) { return name_buffer; }

dte_result_t dte_set_snapshot(const struct dte_snapshot *snapshot) {
  if (!snapshot)
    return last_status = DTE_STATUS_INVALID_ARGUMENT;
  if (snapshot->abi_version != DTE_ABI_VERSION_V1_2)
    return last_status = DTE_STATUS_UNSUPPORTED_ABI;
  if (snapshot->struct_size < DTE_SNAPSHOT_REQUIRED_SIZE)
    return last_status = DTE_STATUS_STRUCT_TOO_SMALL;
  uint64_t valid = snapshot->valid_mask & DTE_SNAPSHOT_VALID_ALL;
  dte_memzero(&state, sizeof(state));
  dte_memcopy(&state, snapshot, DTE_SNAPSHOT_REQUIRED_SIZE);
  state.abi_version = DTE_ABI_VERSION_V1_2;
  state.struct_size = sizeof(state);
  state.valid_mask = valid;
  state.battery_count = clamp(state.battery_count, 2, 3);
  state.wpm = clamp(state.wpm, 0, 999);
  state.layer = clamp(state.layer, 0, 99);
  state.endpoint = clamp(state.endpoint, 0, 2);
  state.profile = clamp(state.profile, 0, 99);
  state.modifiers &= 255;
  state.startup_phase = clamp(state.startup_phase, 0, 2);
  state.backlight = clamp(state.backlight, 0, 100);
  state.refresh_rate_x10 = clamp(state.refresh_rate_x10, -1, 9999);
  state.battery_left = clamp(state.battery_left, -1, 100);
  state.battery_right = clamp(state.battery_right, -1, 100);
  state.battery_dongle = clamp(state.battery_dongle, -1, 100);
  state.connected_left = clamp(state.connected_left, -1, 1);
  state.connected_right = clamp(state.connected_right, -1, 1);
  state.layer_name[23] = 0;
  return last_status = DTE_STATUS_OK;
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
void dte_set_layer_name(const char *name) { copy_name(state.layer_name, name); }
void dte_set_battery_count(int count) {
  if (count == 2 || count == 3)
    state.battery_count = count;
}
void dte_set_display_stats(int backlight, int refresh_rate_x10) {
  state.backlight = clamp(backlight, 0, 100);
  state.refresh_rate_x10 = clamp(refresh_rate_x10, -1, 9999);
}
void dte_set_startup_phase(int phase) {
  state.startup_phase = clamp(phase, 0, 2);
}

void dte_frame_dirty_all(struct dte_frame_result *result, int w, int h) {
  result->flags |= DTE_RENDER_FRAME_CHANGED;
  result->dirty_count = 1;
  result->dirty[0] = (struct dte_rect){0, 0, (uint16_t)w, (uint16_t)h};
}
void dte_frame_from_raster(struct dte_frame_result *result, int w, int h,
                           int active) {
  result->flags = active ? DTE_RENDER_CONTINUOUS : 0;
  result->dirty_count = 0;
  const uint32_t *damage = dtr_dirty_tiles();
  if (!damage) {
    dte_frame_dirty_all(result, w, h);
    return;
  }
  uint32_t rows[18] = {0};
  for (int i = 0; i < (h + 15) / 16; i++)
    rows[i] = damage[i];
  struct dte_dirty_rect r;
  while (dte_next_dirty_rect(rows, w, h, &r)) {
    if (result->dirty_count >= DTE_MAX_DIRTY_RECTS) {
      dte_frame_dirty_all(result, w, h);
      return;
    }
    result->dirty[result->dirty_count++] = (struct dte_rect){
        (int16_t)r.x, (int16_t)r.y, (uint16_t)r.width, (uint16_t)r.height};
  }
  if (result->dirty_count)
    result->flags |= DTE_RENDER_FRAME_CHANGED;
}

static int rect_valid(const struct dte_rect *r) {
  return r->width && r->height && r->x >= 0 && r->y >= 0 &&
         r->x + r->width <= width && r->y + r->height <= height;
}
dte_result_t dte_frame(uint32_t now, struct dte_frame_result *result) {
  if (!result)
    return last_status = DTE_STATUS_INVALID_ARGUMENT;
  if (result->abi_version != DTE_ABI_VERSION_V1_2)
    return last_status = DTE_STATUS_UNSUPPORTED_ABI;
  if (result->struct_size < DTE_FRAME_RESULT_REQUIRED_SIZE)
    return last_status = DTE_STATUS_STRUCT_TOO_SMALL;
  if (!active_abi_version)
    return last_status = DTE_STATUS_NOT_INITIALIZED;
  dispatch_pending_long_press(now);
  struct dte_frame_result out = DTE_FRAME_RESULT_INIT;
  dte_result_t status = dte_selected_theme.frame(&state, now, &out);
  if (status != DTE_STATUS_OK)
    return last_status = status;
  if (out.abi_version != DTE_ABI_VERSION_V1_2 ||
      out.struct_size < DTE_FRAME_RESULT_REQUIRED_SIZE)
    return last_status = DTE_STATUS_UNSUPPORTED_ABI;
  if (out.dirty_count > DTE_MAX_DIRTY_RECTS)
    return last_status = DTE_STATUS_CAPACITY;
  for (unsigned i = 0; i < out.dirty_count; i++)
    if (!rect_valid(&out.dirty[i]))
      return last_status = DTE_STATUS_INVALID_ARGUMENT;
  uint16_t caller_size = result->struct_size;
  dte_memcopy(result, &out, DTE_FRAME_RESULT_REQUIRED_SIZE);
  result->struct_size = caller_size;
  result->flags &= DTE_RENDER_KNOWN_FLAGS;
  if (result->dirty_count)
    result->flags |= DTE_RENDER_FRAME_CHANGED;
  prepared_now = now;
  frame_prepared = 1;
  return last_status = DTE_STATUS_OK;
}
dte_result_t dte_draw(uint32_t now, const struct dte_canvas *canvas) {
  if (!canvas || !canvas->pixels)
    return last_status = DTE_STATUS_INVALID_ARGUMENT;
  if (canvas->abi_version != DTE_ABI_VERSION_V1_2)
    return last_status = DTE_STATUS_UNSUPPORTED_ABI;
  if (canvas->struct_size < DTE_CANVAS_REQUIRED_SIZE)
    return last_status = DTE_STATUS_STRUCT_TOO_SMALL;
  if (!frame_prepared || now != prepared_now)
    return last_status = DTE_STATUS_RENDER_FAILED;
  if (canvas->pixel_format != DTE_PIXEL_FORMAT_RGB565_LE ||
      canvas->scene_width != width || canvas->scene_height != height ||
      !canvas->width || !canvas->height ||
      canvas->origin_x + canvas->width > width ||
      canvas->origin_y + canvas->height > height ||
      canvas->stride_pixels < canvas->width)
    return last_status = DTE_STATUS_INVALID_ARGUMENT;
  uint32_t needed =
      ((uint32_t)(canvas->height - 1) * canvas->stride_pixels + canvas->width) *
      2u;
  if (canvas->buffer_size < needed)
    return last_status = DTE_STATUS_CAPACITY;
  return last_status = dte_selected_theme.draw(&state, now, canvas);
}

int dte_gesture_x(void) { return gesture_x; }
int dte_gesture_y(void) { return gesture_y; }
int dte_backlight_get(void) { return state.backlight; }
int dte_backlight_adjust(int delta, int minimum, int maximum) {
  minimum = clamp(minimum, 0, 100);
  maximum = clamp(maximum, minimum, 100);
  state.backlight = clamp(state.backlight + delta, minimum, maximum);
  dte_host_backlight_set(state.backlight);
  return state.backlight;
}
static void dispatch_gesture(int kind, uint32_t now, int x, int y) {
  if (kind < DTE_TAP || kind > DTE_LONG_PRESS || !active_abi_version)
    return;
  gesture_x = x;
  gesture_y = y;
  if ((dte_selected_theme.capabilities & DTE_THEME_CAP_GESTURE) &&
      dte_selected_theme.gesture)
    dte_selected_theme.gesture(kind, now);
  gesture_x = gesture_y = -1;
}
void dte_gesture(int kind, uint32_t now) {
  dispatch_gesture(kind, now, -1, -1);
}
void dte_touch_cancel(void) { touch_down = touch_moved = touch_long = 0; }
int dte_touch_hint(int kind, uint32_t now) {
  if (kind < DTE_TAP || kind > DTE_LONG_PRESS || touch_long)
    return 0;
  touch_long = 1;
  dispatch_gesture(kind, now, touch_x, touch_y);
  return 1;
}
void dte_touch(int x, int y, int down, uint32_t now) {
  dte_touch_at(x, y, down, now, now);
}
int dte_touch_active(void) { return touch_down; }
void dte_touch_at(int x, int y, int down, uint32_t now,
                  uint32_t animation_time) {
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
      dispatch_gesture(dx < 0 ? DTE_LEFT : DTE_RIGHT, animation_time, touch_x,
                       touch_y);
    else if (ay > 28 && ay > ax)
      dispatch_gesture(dy < 0 ? DTE_UP : DTE_DOWN, animation_time, touch_x,
                       touch_y);
    else if (!touch_moved && (uint32_t)(now - touch_start) < 600)
      dispatch_gesture(DTE_TAP, animation_time, touch_x, touch_y);
    else if (!touch_moved)
      dispatch_gesture(DTE_LONG_PRESS, animation_time, touch_x, touch_y);
  }
}
static void dispatch_pending_long_press(uint32_t now) {
  if (touch_down && !touch_moved && !touch_long &&
      (uint32_t)(now - touch_start) >= 600) {
    touch_long = 1;
    dispatch_gesture(DTE_LONG_PRESS, now, touch_x, touch_y);
  }
}

int dte_render(uint32_t now) {
  struct dte_frame_result result = DTE_FRAME_RESULT_INIT;
  if (dte_frame(now, &result) != DTE_STATUS_OK)
    return 0;
#if !defined(__ZEPHYR__)
  for (unsigned i = 0; i < result.dirty_count; i++) {
    const struct dte_rect *r = &result.dirty[i];
    struct dte_canvas canvas = DTE_CANVAS_INIT;
    canvas.scene_width = width;
    canvas.scene_height = height;
    canvas.origin_x = r->x;
    canvas.origin_y = r->y;
    canvas.width = r->width;
    canvas.height = r->height;
    canvas.stride_pixels = width;
    canvas.buffer_size = ((uint32_t)(r->height - 1) * width + r->width) * 2u;
    canvas.pixels = preview_pixels + r->y * width + r->x;
    if (dte_draw(now, &canvas) != DTE_STATUS_OK)
      return 0;
  }
#endif
  return (result.flags & (DTE_RENDER_CONTINUOUS | DTE_RENDER_DEADLINE_VALID)) !=
         0;
}
#if !defined(__ZEPHYR__)
uint16_t *dte_pixels(void) { return preview_pixels; }
uint32_t dte_hash(void) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < width * height; i++) {
    hash = (hash ^ (preview_pixels[i] & 255)) * 16777619u;
    hash = (hash ^ (preview_pixels[i] >> 8)) * 16777619u;
  }
  return hash;
}
#endif
