/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define DTE_ENGINE_VERSION_MAJOR 1u
#define DTE_ENGINE_VERSION_MINOR 3u
#define DTE_ENGINE_VERSION_PATCH 0u
#define DTE_ENGINE_VERSION_STRING "1.3.0"
#define DTE_ABI_VERSION 0x0103u
#define DTE_ABI_VERSION_V1_2 0x0102u
#define DTE_ABI_VERSION_V1_3 DTE_ABI_VERSION
#define DTE_MAX_PIXELS (280 * 240)
#define DTE_MAX_DIRTY_RECTS 18u
#define DTE_PIXEL_FORMAT_RGB565_LE 1u

enum dte_gesture {
  DTE_TAP = 1,
  DTE_LEFT,
  DTE_RIGHT,
  DTE_UP,
  DTE_DOWN,
  DTE_LONG_PRESS
};
enum dte_animation {
  DTE_ANIMATION_RADIAL = 1,
  DTE_ANIMATION_DENSITY = 2,
  DTE_ANIMATION_CLASSIC = 3,
  DTE_ANIMATION_RADIAL_DENSITY = 4,
  DTE_ANIMATION_LINEAR_DENSITY = 5
};
enum dte_status {
  DTE_STATUS_OK = 0,
  DTE_STATUS_INVALID_ARGUMENT = -1,
  DTE_STATUS_UNSUPPORTED_ABI = -2,
  DTE_STATUS_STRUCT_TOO_SMALL = -3,
  DTE_STATUS_MISSING_CALLBACK = -4,
  DTE_STATUS_UNSUPPORTED_DISPLAY = -5,
  DTE_STATUS_THEME_NOT_FOUND = -6,
  DTE_STATUS_NOT_INITIALIZED = -7,
  DTE_STATUS_RENDER_FAILED = -8,
  DTE_STATUS_CAPACITY = -9,
};
typedef int32_t dte_result_t;

#define DTE_SNAPSHOT_VALID_BATTERY_COUNT (UINT64_C(1) << 0)
#define DTE_SNAPSHOT_VALID_WPM (UINT64_C(1) << 1)
#define DTE_SNAPSHOT_VALID_LAYER (UINT64_C(1) << 2)
#define DTE_SNAPSHOT_VALID_ENDPOINT (UINT64_C(1) << 3)
#define DTE_SNAPSHOT_VALID_PROFILE (UINT64_C(1) << 4)
#define DTE_SNAPSHOT_VALID_MODIFIERS (UINT64_C(1) << 5)
#define DTE_SNAPSHOT_VALID_STARTUP_PHASE (UINT64_C(1) << 6)
#define DTE_SNAPSHOT_VALID_BACKLIGHT (UINT64_C(1) << 7)
#define DTE_SNAPSHOT_VALID_REFRESH_RATE (UINT64_C(1) << 8)
#define DTE_SNAPSHOT_VALID_BATTERY_LEFT (UINT64_C(1) << 9)
#define DTE_SNAPSHOT_VALID_BATTERY_RIGHT (UINT64_C(1) << 10)
#define DTE_SNAPSHOT_VALID_BATTERY_DONGLE (UINT64_C(1) << 11)
#define DTE_SNAPSHOT_VALID_CONNECTED_LEFT (UINT64_C(1) << 12)
#define DTE_SNAPSHOT_VALID_CONNECTED_RIGHT (UINT64_C(1) << 13)
#define DTE_SNAPSHOT_VALID_LAYER_NAME (UINT64_C(1) << 14)
#define DTE_SNAPSHOT_VALID_ALL ((UINT64_C(1) << 15) - 1)

/* ABI 1.2 replaced the full-frame v1/v1.1 Theme contract. ABI 1.3 retains
 * that region contract while adding the independent TRE render core. */
struct dte_snapshot {
  uint16_t abi_version, struct_size;
  uint32_t reserved0;
  uint64_t valid_mask;
  int32_t battery_count, wpm, layer, endpoint, profile, modifiers,
      startup_phase;
  int32_t backlight, refresh_rate_x10, battery_left, battery_right,
      battery_dongle;
  int32_t connected_left, connected_right;
  char layer_name[24];
  uint32_t reserved[4];
};
#define DTE_SNAPSHOT_REQUIRED_SIZE                                             \
  ((uint16_t)offsetof(struct dte_snapshot, reserved))
#define DTE_SNAPSHOT_INIT                                                      \
  {.abi_version = DTE_ABI_VERSION_V1_3,                                        \
   .struct_size = (uint16_t)sizeof(struct dte_snapshot),                       \
   .valid_mask = DTE_SNAPSHOT_VALID_ALL}

struct dte_rect {
  int16_t x, y;
  uint16_t width, height;
};
#define DTE_RENDER_FRAME_CHANGED (UINT32_C(1) << 0)
#define DTE_RENDER_CONTINUOUS (UINT32_C(1) << 1)
#define DTE_RENDER_DEADLINE_VALID (UINT32_C(1) << 2)
#define DTE_RENDER_KNOWN_FLAGS                                                 \
  (DTE_RENDER_FRAME_CHANGED | DTE_RENDER_CONTINUOUS | DTE_RENDER_DEADLINE_VALID)
struct dte_frame_result {
  uint16_t abi_version, struct_size;
  uint32_t flags, next_frame_at_ms;
  uint16_t dirty_count, reserved0;
  struct dte_rect dirty[DTE_MAX_DIRTY_RECTS];
  uint32_t reserved[4];
};
#define DTE_FRAME_RESULT_REQUIRED_SIZE                                         \
  ((uint16_t)offsetof(struct dte_frame_result, reserved))
#define DTE_FRAME_RESULT_INIT                                                  \
  {.abi_version = DTE_ABI_VERSION_V1_3,                                        \
   .struct_size = (uint16_t)sizeof(struct dte_frame_result)}

struct dte_canvas {
  uint16_t abi_version, struct_size, scene_width, scene_height, origin_x,
      origin_y;
  uint16_t width, height, stride_pixels;
  uint8_t pixel_format, reserved0;
  uint32_t buffer_size;
  uint16_t *pixels;
  uint32_t reserved[4];
};
#define DTE_CANVAS_REQUIRED_SIZE                                               \
  ((uint16_t)offsetof(struct dte_canvas, reserved))
#define DTE_CANVAS_INIT                                                        \
  {.abi_version = DTE_ABI_VERSION_V1_3,                                        \
   .struct_size = (uint16_t)sizeof(struct dte_canvas),                         \
   .pixel_format = DTE_PIXEL_FORMAT_RGB565_LE}

#define DTE_THEME_CAP_GESTURE (UINT32_C(1) << 0)
struct dte_theme {
  uint16_t abi_version, struct_size;
  uint32_t capabilities;
  const char *id;
  dte_result_t (*mount)(int32_t width, int32_t height, uint32_t now);
  void (*gesture)(int32_t kind, uint32_t now);
  dte_result_t (*frame)(const struct dte_snapshot *, uint32_t,
                        struct dte_frame_result *);
  dte_result_t (*draw)(const struct dte_snapshot *, uint32_t,
                       const struct dte_canvas *);
  uint32_t reserved[8];
};
#define DTE_THEME_REQUIRED_SIZE ((uint16_t)offsetof(struct dte_theme, reserved))
#define DTE_THEME_INIT(theme_id, theme_capabilities, mount_fn, gesture_fn,     \
                       frame_fn, draw_fn)                                      \
  {.abi_version = DTE_ABI_VERSION_V1_3,                                        \
   .struct_size = (uint16_t)sizeof(struct dte_theme),                          \
   .capabilities = (theme_capabilities),                                       \
   .id = (theme_id),                                                           \
   .mount = (mount_fn),                                                        \
   .gesture = (gesture_fn),                                                    \
   .frame = (frame_fn),                                                        \
   .draw = (draw_fn)}

extern const struct dte_theme dte_selected_theme;
dte_result_t dte_validate_theme(const struct dte_theme *theme);
dte_result_t dte_init_ex(int32_t width, int32_t height);
dte_result_t dte_set_snapshot(const struct dte_snapshot *snapshot);
dte_result_t dte_frame(uint32_t now, struct dte_frame_result *result);
dte_result_t dte_draw(uint32_t now, const struct dte_canvas *canvas);
dte_result_t dte_last_status(void);
uint32_t dte_active_abi_version(void);
void dte_init(int width, int height);
int dte_render(uint32_t now);
#if !defined(__ZEPHYR__)
uint16_t *dte_pixels(void);
uint32_t dte_hash(void);
int dte_preview_set_strip_pixels(int pixels);
int dte_preview_set_filter(int filter);
int dte_preview_filter(void);
uint32_t dte_preview_filter_pixels(void);
int dte_preview_set_filter_corner_radius(int radius);
int dte_preview_filter_corner_radius(void);
uint32_t dte_preview_transfer_bytes(void);
uint32_t dte_preview_dirty_rects(void);
uint32_t dte_preview_draw_calls(void);
#endif
int dte_width(void);
int dte_height(void);
void dte_set_state(int wpm, int layer, int endpoint, int profile, int modifiers,
                   int left, int right, int dongle, int lc, int rc);
void dte_set_layer_name(const char *name);
void dte_set_battery_count(int count);
void dte_set_display_stats(int backlight, int refresh_rate_x10);
void dte_set_startup_phase(int phase);
char *dte_name_buffer(void);
void dte_gesture(int kind, uint32_t now);
int dte_gesture_x(void);
int dte_gesture_y(void);
int dte_backlight_get(void);
int dte_backlight_adjust(int delta, int minimum, int maximum);
void dte_touch(int x, int y, int down, uint32_t now);
void dte_touch_at(int x, int y, int down, uint32_t now,
                  uint32_t animation_time);
int dte_touch_hint(int kind, uint32_t now);
int dte_touch_active(void);
void dte_touch_cancel(void);
int dte_animation_options(void);
int dte_set_animation(int kind);
int dte_get_animation(void);
int dte_set_animation_duration(int ms);
int dte_get_animation_duration(void);
void dte_force_redraw(void);
void dte_frame_dirty_all(struct dte_frame_result *result, int width,
                         int height);
void dte_frame_from_raster(struct dte_frame_result *result, int width,
                           int height, int active);
void dte_frame_from_raster_deadline(struct dte_frame_result *result,
                                    int width, int height,
                                    uint32_t next_frame_at_ms);
