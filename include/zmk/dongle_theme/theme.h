/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

#define DTE_ABI_VERSION 1
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
/* v1 bootstrap: exactly one statically selected theme. Runtime multi-theme
 * registry deferred. */
extern const struct dte_theme dte_selected_theme;
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
