/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/theme.h>
static uint16_t pixels[DTE_MAX_PIXELS];
static struct dte_snapshot state;
static struct dte_snapshot_v1_1 state_v1_1;
static int width = 280, height = 240;
static char name_buffer[24];
static int touch_down, touch_x, touch_y, touch_moved, touch_long;
static int gesture_x = -1, gesture_y = -1;
static uint32_t touch_start;
static enum dte_status last_status = DTE_STATUS_NOT_INITIALIZED;
static uint32_t active_abi_version;

/* Weak empty descriptors let a firmware select either ABI without requiring
 * both selection symbols. A strong Theme definition overrides the matching
 * empty descriptor at static link time. */
__attribute__((weak)) const struct dte_theme dte_selected_theme = {0};
__attribute__((weak)) const struct dte_theme_v1_1 dte_selected_theme_v1_1 = {0};
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
__attribute__((weak)) void dte_host_backlight_set(int percent) { (void)percent; }
char *dte_name_buffer(void) { return name_buffer; }
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

static void sync_v1_1_from_v1(void) {
  state_v1_1.abi_version = DTE_ABI_VERSION_V1_1;
  state_v1_1.struct_size = (uint16_t)sizeof(state_v1_1);
  state_v1_1.reserved0 = 0;
  state_v1_1.valid_mask = DTE_SNAPSHOT_VALID_ALL;
  state_v1_1.battery_count = state.battery_count;
  state_v1_1.wpm = state.wpm;
  state_v1_1.layer = state.layer;
  state_v1_1.endpoint = state.endpoint;
  state_v1_1.profile = state.profile;
  state_v1_1.modifiers = state.modifiers;
  state_v1_1.startup_phase = state.startup_phase;
  state_v1_1.backlight = state.backlight;
  state_v1_1.refresh_rate_x10 = state.refresh_rate_x10;
  state_v1_1.battery_left = state.battery_left;
  state_v1_1.battery_right = state.battery_right;
  state_v1_1.battery_dongle = state.battery_dongle;
  state_v1_1.connected_left = state.connected_left;
  state_v1_1.connected_right = state.connected_right;
  copy_name(state_v1_1.layer_name, state.layer_name);
  for (int i = 0; i < 4; i++)
    state_v1_1.reserved[i] = 0;
}

static void sync_v1_from_v1_1(void) {
  uint64_t valid_mask = state_v1_1.valid_mask;
  state.battery_count = clamp(state_v1_1.battery_count, 2, 3);
  state.wpm = clamp(state_v1_1.wpm, 0, 999);
  state.layer = clamp(state_v1_1.layer, 0, 99);
  state.endpoint = clamp(state_v1_1.endpoint, 0, 2);
  state.profile = clamp(state_v1_1.profile, 0, 99);
  state.modifiers = state_v1_1.modifiers & 255;
  state.startup_phase = clamp(state_v1_1.startup_phase, 0, 2);
  state.backlight = clamp(state_v1_1.backlight, 0, 100);
  state.refresh_rate_x10 = clamp(state_v1_1.refresh_rate_x10, -1, 9999);
  state.battery_left = clamp(state_v1_1.battery_left, -1, 100);
  state.battery_right = clamp(state_v1_1.battery_right, -1, 100);
  state.battery_dongle = clamp(state_v1_1.battery_dongle, -1, 100);
  state.connected_left = clamp(state_v1_1.connected_left, -1, 1);
  state.connected_right = clamp(state_v1_1.connected_right, -1, 1);
  copy_name(state.layer_name, state_v1_1.layer_name);
  sync_v1_1_from_v1();
  state_v1_1.valid_mask = valid_mask;
}

static void reset_state(void) {
  state = (struct dte_snapshot){.battery_left = -1,
                                .battery_right = -1,
                                .battery_dongle = -1,
                                .refresh_rate_x10 = -1,
                                .connected_left = -1,
                                .connected_right = -1};
  state.battery_count = 3;
  copy_name(state.layer_name, "BASE");
  sync_v1_1_from_v1();
}

enum dte_status dte_validate_theme_v1_1(const struct dte_theme_v1_1 *theme) {
  if (!theme)
    return DTE_STATUS_INVALID_ARGUMENT;
  if (theme->abi_version != DTE_ABI_VERSION_V1_1)
    return DTE_STATUS_UNSUPPORTED_ABI;
  if (theme->struct_size < DTE_THEME_V1_1_REQUIRED_SIZE)
    return DTE_STATUS_STRUCT_TOO_SMALL;
  if (!theme->id || !theme->id[0])
    return DTE_STATUS_INVALID_ARGUMENT;
  if (!theme->mount || !theme->render ||
      ((theme->capabilities & DTE_THEME_CAP_GESTURE) && !theme->gesture))
    return DTE_STATUS_MISSING_CALLBACK;
  return DTE_STATUS_OK;
}

static enum dte_status validate_v1(void) {
  if (dte_selected_theme.abi_version == 0)
    return DTE_STATUS_THEME_NOT_FOUND;
  if (dte_selected_theme.abi_version != DTE_ABI_VERSION_V1)
    return DTE_STATUS_UNSUPPORTED_ABI;
  if (!dte_selected_theme.id || !dte_selected_theme.id[0])
    return DTE_STATUS_INVALID_ARGUMENT;
  if (!dte_selected_theme.mount || !dte_selected_theme.gesture ||
      !dte_selected_theme.render)
    return DTE_STATUS_MISSING_CALLBACK;
  return DTE_STATUS_OK;
}

enum dte_status dte_init_ex(int32_t w, int32_t h) {
  active_abi_version = 0;
  if (!((w == 280 && h == 240) || (w == 240 && (h == 280 || h == 240)))) {
    last_status = DTE_STATUS_UNSUPPORTED_DISPLAY;
    return last_status;
  }
  width = w;
  height = h;
  dte_touch_cancel();
  gesture_x=gesture_y=-1;
  reset_state();

  if (dte_selected_theme_v1_1.abi_version || dte_selected_theme_v1_1.struct_size ||
      dte_selected_theme_v1_1.id) {
    last_status = dte_validate_theme_v1_1(&dte_selected_theme_v1_1);
    if (last_status != DTE_STATUS_OK)
      return last_status;
    last_status = dte_selected_theme_v1_1.mount(w, h, 0);
    if (last_status == DTE_STATUS_OK)
      active_abi_version = DTE_ABI_VERSION_V1_1;
    return last_status;
  }

  last_status = validate_v1();
  if (last_status == DTE_STATUS_OK) {
    dte_selected_theme.mount(w, h, 0);
    active_abi_version = DTE_ABI_VERSION_V1;
  }
  return last_status;
}
void dte_init(int w, int h) { (void)dte_init_ex(w, h); }
enum dte_status dte_last_status(void) { return last_status; }
uint32_t dte_active_abi_version(void) { return active_abi_version; }
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
  sync_v1_1_from_v1();
}
void dte_set_layer_name(const char *name) {
  copy_name(state.layer_name, name);
  sync_v1_1_from_v1();
}
void dte_set_battery_count(int count){if(count==2||count==3)state.battery_count=count;sync_v1_1_from_v1();}
void dte_set_display_stats(int backlight, int refresh_rate_x10) {
  state.backlight=clamp(backlight,0,100);
  state.refresh_rate_x10=clamp(refresh_rate_x10,-1,9999);
  sync_v1_1_from_v1();
}
void dte_set_startup_phase(int phase) { state.startup_phase=clamp(phase,0,2);sync_v1_1_from_v1(); }
enum dte_status dte_set_snapshot_v1_1(const struct dte_snapshot_v1_1 *snapshot) {
  if (!snapshot)
    return last_status = DTE_STATUS_INVALID_ARGUMENT;
  if (snapshot->abi_version != DTE_ABI_VERSION_V1_1)
    return last_status = DTE_STATUS_UNSUPPORTED_ABI;
  if (snapshot->struct_size < DTE_SNAPSHOT_V1_1_REQUIRED_SIZE)
    return last_status = DTE_STATUS_STRUCT_TOO_SMALL;
  state_v1_1.abi_version = DTE_ABI_VERSION_V1_1;
  state_v1_1.struct_size = (uint16_t)sizeof(state_v1_1);
  state_v1_1.reserved0 = 0;
  state_v1_1.valid_mask = snapshot->valid_mask & DTE_SNAPSHOT_VALID_ALL;
  state_v1_1.battery_count = snapshot->battery_count;
  state_v1_1.wpm = snapshot->wpm;
  state_v1_1.layer = snapshot->layer;
  state_v1_1.endpoint = snapshot->endpoint;
  state_v1_1.profile = snapshot->profile;
  state_v1_1.modifiers = snapshot->modifiers;
  state_v1_1.startup_phase = snapshot->startup_phase;
  state_v1_1.backlight = snapshot->backlight;
  state_v1_1.refresh_rate_x10 = snapshot->refresh_rate_x10;
  state_v1_1.battery_left = snapshot->battery_left;
  state_v1_1.battery_right = snapshot->battery_right;
  state_v1_1.battery_dongle = snapshot->battery_dongle;
  state_v1_1.connected_left = snapshot->connected_left;
  state_v1_1.connected_right = snapshot->connected_right;
  copy_name(state_v1_1.layer_name, snapshot->layer_name);
  sync_v1_from_v1_1();
  return last_status = DTE_STATUS_OK;
}
int dte_gesture_x(void) { return gesture_x; }
int dte_gesture_y(void) { return gesture_y; }
int dte_backlight_get(void) { return state.backlight; }
int dte_backlight_adjust(int delta, int minimum, int maximum) {
  minimum=clamp(minimum,0,100);maximum=clamp(maximum,minimum,100);
  state.backlight=clamp(state.backlight+delta,minimum,maximum);
  sync_v1_1_from_v1();
  dte_host_backlight_set(state.backlight);
  return state.backlight;
}
static void dispatch_gesture(int kind, uint32_t now, int x, int y) {
  if (kind < DTE_TAP || kind > DTE_LONG_PRESS || !active_abi_version)
    return;
  gesture_x=x;gesture_y=y;
  if (active_abi_version == DTE_ABI_VERSION_V1_1) {
    if ((dte_selected_theme_v1_1.capabilities & DTE_THEME_CAP_GESTURE) &&
        dte_selected_theme_v1_1.gesture)
      dte_selected_theme_v1_1.gesture(kind, now);
  } else {
    dte_selected_theme.gesture(kind, now);
  }
  gesture_x=gesture_y=-1;
}
void dte_gesture(int kind, uint32_t now) { dispatch_gesture(kind,now,-1,-1); }
void dte_touch_cancel(void) { touch_down = touch_moved = touch_long = 0; }
int dte_touch_hint(int kind, uint32_t now) {
  if (kind < DTE_TAP || kind > DTE_LONG_PRESS || touch_long)
    return 0;
  touch_long = 1; /* Suppress software recognition and duplicate controller hints. */
  dispatch_gesture(kind,now,touch_x,touch_y);
  return 1;
}
void dte_touch(int x, int y, int down, uint32_t now) {
  dte_touch_at(x,y,down,now,now);
}
int dte_touch_active(void) { return touch_down; }
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
      dispatch_gesture(dx < 0 ? DTE_LEFT : DTE_RIGHT,animation_time,touch_x,touch_y);
    else if (ay > 28 && ay > ax)
      dispatch_gesture(dy < 0 ? DTE_UP : DTE_DOWN,animation_time,touch_x,touch_y);
    else if (!touch_moved && (uint32_t)(now - touch_start) < 600)
      dispatch_gesture(DTE_TAP,animation_time,touch_x,touch_y);
    else if (!touch_moved)
      dispatch_gesture(DTE_LONG_PRESS,animation_time,touch_x,touch_y);
  }
}
static void dispatch_pending_long_press(uint32_t now) {
  if (touch_down && !touch_moved && !touch_long &&
      (uint32_t)(now - touch_start) >= 600) {
    touch_long = 1;
    dispatch_gesture(DTE_LONG_PRESS,now,touch_x,touch_y);
  }
}
enum dte_status dte_render_v1_1(uint32_t now,
                              struct dte_render_result_v1_1 *result) {
  if (!result)
    return last_status = DTE_STATUS_INVALID_ARGUMENT;
  if (result->abi_version != DTE_ABI_VERSION_V1_1)
    return last_status = DTE_STATUS_UNSUPPORTED_ABI;
  if (result->struct_size < DTE_RENDER_RESULT_V1_1_REQUIRED_SIZE)
    return last_status = DTE_STATUS_STRUCT_TOO_SMALL;
  result->flags = 0;
  result->next_frame_at_ms = 0;
  if (!active_abi_version)
    return last_status = DTE_STATUS_NOT_INITIALIZED;
  dispatch_pending_long_press(now);
  if (active_abi_version == DTE_ABI_VERSION_V1_1) {
    struct dte_render_result_v1_1 theme_result = DTE_RENDER_RESULT_V1_1_INIT;
    enum dte_status status = dte_selected_theme_v1_1.render(
        &state_v1_1, now, pixels, &theme_result);
    if (status != DTE_STATUS_OK)
      return last_status = status;
    if (theme_result.abi_version != DTE_ABI_VERSION_V1_1)
      return last_status = DTE_STATUS_UNSUPPORTED_ABI;
    if (theme_result.struct_size < DTE_RENDER_RESULT_V1_1_REQUIRED_SIZE)
      return last_status = DTE_STATUS_STRUCT_TOO_SMALL;
    result->flags = theme_result.flags & DTE_RENDER_KNOWN_FLAGS;
    result->next_frame_at_ms = theme_result.next_frame_at_ms;
  } else {
    int active = dte_selected_theme.render(&state, now, pixels);
    result->flags = DTE_RENDER_FRAME_CHANGED |
                    (active ? DTE_RENDER_CONTINUOUS : 0);
  }
  return last_status = DTE_STATUS_OK;
}
int dte_render(uint32_t now) {
  struct dte_render_result_v1_1 result = DTE_RENDER_RESULT_V1_1_INIT;
  if (dte_render_v1_1(now, &result) != DTE_STATUS_OK)
    return 0;
  return (result.flags & (DTE_RENDER_CONTINUOUS |
                          DTE_RENDER_DEADLINE_VALID)) != 0;
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
