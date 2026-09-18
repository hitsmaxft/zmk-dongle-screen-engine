/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define DTE_ENGINE_VERSION_MAJOR 1u
#define DTE_ENGINE_VERSION_MINOR 1u
#define DTE_ENGINE_VERSION_PATCH 2u
#define DTE_ENGINE_VERSION_STRING "1.1.2"

#define DTE_ABI_VERSION 1
#define DTE_ABI_VERSION_V1 1u
#define DTE_ABI_VERSION_V1_1 0x0101u
#define DTE_MAX_PIXELS (280 * 240)
enum dte_gesture {
  DTE_TAP = 1,
  DTE_LEFT,
  DTE_RIGHT,
  DTE_UP,
  DTE_DOWN,
  DTE_LONG_PRESS
};
struct dte_snapshot {
  int battery_count;
  int wpm, layer, endpoint, profile, modifiers;
  /* Host-defined startup sequence: 0 normal, 1 splash, 2 reveal. */
  int startup_phase;
  /* Display brightness percent and measured presented frames/s x10. */
  int backlight, refresh_rate_x10;
  /* Battery: -1 means unknown. Connection: -1 unknown, 0 disconnected, 1
   * connected. */
  int battery_left, battery_right, battery_dongle, connected_left,
      connected_right;
  char layer_name[24];
};
struct dte_theme {
  uint32_t abi_version;
  const char *id;
  void (*mount)(int width, int height, uint32_t now);
  void (*gesture)(int kind, uint32_t now);
  /* Borrowed RGB565 buffer; host never frees it. Return nonzero while animation
   * is active. */
  int (*render)(const struct dte_snapshot *snapshot, uint32_t now,
                uint16_t *pixels);
};

/* ABI 1.1 is additive. The v1 structures and entry points above remain frozen. */
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
};

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

struct dte_snapshot_v1_1 {
  uint16_t abi_version;
  uint16_t struct_size;
  uint32_t reserved0;
  uint64_t valid_mask;
  int32_t battery_count;
  int32_t wpm;
  int32_t layer;
  int32_t endpoint;
  int32_t profile;
  int32_t modifiers;
  int32_t startup_phase;
  int32_t backlight;
  int32_t refresh_rate_x10;
  int32_t battery_left;
  int32_t battery_right;
  int32_t battery_dongle;
  int32_t connected_left;
  int32_t connected_right;
  char layer_name[24];
  uint32_t reserved[4];
};
#define DTE_SNAPSHOT_V1_1_REQUIRED_SIZE \
  ((uint16_t)offsetof(struct dte_snapshot_v1_1, reserved))
#define DTE_SNAPSHOT_V1_1_INIT                                                   \
  { .abi_version = DTE_ABI_VERSION_V1_1,                                         \
    .struct_size = (uint16_t)sizeof(struct dte_snapshot_v1_1),                   \
    .valid_mask = DTE_SNAPSHOT_VALID_ALL }

#define DTE_RENDER_FRAME_CHANGED (UINT32_C(1) << 0)
#define DTE_RENDER_CONTINUOUS (UINT32_C(1) << 1)
#define DTE_RENDER_DEADLINE_VALID (UINT32_C(1) << 2)
#define DTE_RENDER_KNOWN_FLAGS                                                 \
  (DTE_RENDER_FRAME_CHANGED | DTE_RENDER_CONTINUOUS |                         \
   DTE_RENDER_DEADLINE_VALID)

struct dte_render_result_v1_1 {
  uint16_t abi_version;
  uint16_t struct_size;
  uint32_t flags;
  uint32_t next_frame_at_ms;
  uint32_t reserved[4];
};
#define DTE_RENDER_RESULT_V1_1_REQUIRED_SIZE \
  ((uint16_t)offsetof(struct dte_render_result_v1_1, reserved))
#define DTE_RENDER_RESULT_V1_1_INIT                                              \
  { .abi_version = DTE_ABI_VERSION_V1_1,                                         \
    .struct_size = (uint16_t)sizeof(struct dte_render_result_v1_1) }

#define DTE_THEME_CAP_GESTURE (UINT32_C(1) << 0)

struct dte_theme_v1_1 {
  uint16_t abi_version;
  uint16_t struct_size;
  uint32_t capabilities;
  const char *id;
  enum dte_status (*mount)(int32_t width, int32_t height, uint32_t now);
  void (*gesture)(int32_t kind, uint32_t now);
  enum dte_status (*render)(const struct dte_snapshot_v1_1 *snapshot,
                            uint32_t now, uint16_t *pixels,
                            struct dte_render_result_v1_1 *result);
  uint32_t reserved[8];
};
#define DTE_THEME_V1_1_REQUIRED_SIZE \
  ((uint16_t)offsetof(struct dte_theme_v1_1, reserved))
#define DTE_THEME_V1_1_INIT(theme_id, theme_capabilities, mount_fn, gesture_fn,  \
                          render_fn)                                           \
  { .abi_version = DTE_ABI_VERSION_V1_1,                                         \
    .struct_size = (uint16_t)sizeof(struct dte_theme_v1_1),                      \
    .capabilities = (theme_capabilities), .id = (theme_id),                    \
    .mount = (mount_fn), .gesture = (gesture_fn), .render = (render_fn) }

/* v1 bootstrap: exactly one statically selected theme. Runtime multi-theme
 * registry deferred. */
extern const struct dte_theme dte_selected_theme;
/* Optional ABI 1.1 selection. A present 1.1 descriptor takes precedence over
 * v1 and is never silently downgraded when validation fails. */
extern const struct dte_theme_v1_1 dte_selected_theme_v1_1;
enum dte_status dte_validate_theme_v1_1(const struct dte_theme_v1_1 *theme);
enum dte_status dte_init_ex(int32_t width, int32_t height);
enum dte_status dte_set_snapshot_v1_1(const struct dte_snapshot_v1_1 *snapshot);
enum dte_status dte_render_v1_1(uint32_t now,
                              struct dte_render_result_v1_1 *result);
enum dte_status dte_last_status(void);
uint32_t dte_active_abi_version(void);
void dte_init(int width, int height);
void dte_set_state(int wpm, int layer, int endpoint, int profile, int modifiers,
                   int left, int right, int dongle, int lc, int rc);
void dte_set_layer_name(const char *name);
void dte_set_battery_count(int count);
void dte_set_display_stats(int backlight, int refresh_rate_x10);
void dte_set_startup_phase(int phase);
/* Writable 24-byte bridge used by host/WASM callers before set_layer_name(). */
char *dte_name_buffer(void);
void dte_gesture(int kind, uint32_t now);
/* Landscape-space origin of the gesture currently being dispatched. Direct
 * button/API gestures have no touch origin and return -1 for both axes. */
int dte_gesture_x(void);
int dte_gesture_y(void);
/* Runtime backlight command shared by native, WASM and the ZMK host. */
int dte_backlight_get(void);
int dte_backlight_adjust(int delta, int minimum, int maximum);
/* Optional theme animation extension; zero mask means original fixed animation. */
enum dte_animation { DTE_ANIMATION_RADIAL=1, DTE_ANIMATION_DENSITY=2, DTE_ANIMATION_CLASSIC=3,
                     DTE_ANIMATION_RADIAL_DENSITY=4, DTE_ANIMATION_LINEAR_DENSITY=5 };
int dte_animation_options(void);
int dte_set_animation(int kind);
int dte_get_animation(void);
int dte_set_animation_duration(int milliseconds);
int dte_get_animation_duration(void);
/* Optional retained theme hook; used by deterministic full-repaint tests. */
void dte_force_redraw(void);
void dte_touch(int x, int y, int down, uint32_t now);
/* Nonzero while a physical or preview touch contact remains down. */
int dte_touch_active(void);
/* Keep sensor durations accurate without aging a newly dispatched animation. */
void dte_touch_at(int x, int y, int down, uint32_t event_time, uint32_t animation_time);
void dte_touch_cancel(void);
/* Controller hint while a contact/release is pending. Returns 1 once per contact. */
int dte_touch_hint(int kind, uint32_t now);
int dte_render(uint32_t now);
uint16_t *dte_pixels(void);
int dte_width(void);
int dte_height(void);
uint32_t dte_hash(void);
