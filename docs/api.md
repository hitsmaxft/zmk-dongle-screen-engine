# API manual

## Scope

`zmk-dongle-screen-engine` is a compile-time ZMK screen host. The bootstrap ABI
links exactly one theme through `dte_selected_theme`; it does not yet implement
a runtime registry, persistent theme selection, or a safe fallback theme.

The current host accepts 280x240, 240x280 and 240x240 displays. Its validated
raster and transport path is RGB565. Theme-local localization, fonts and copy
remain the theme's responsibility. The WASM preview shell localization is not
part of the firmware ABI.

The preview-only hardware model reads `dtr_dirty_tiles()` after each WASM
render, estimates full-frame, dirty-band or packed-tile transfer bytes, and
throttles presentation by target FPS plus estimated CPU/SPI cost. Profile
changes never reinitialize WASM or mutate the theme snapshot. The nRF52840
preset is deliberately conservative and must be calibrated against physical
display measurements before treating its FPS as a hardware claim.

## Theme descriptor

Include `zmk/dongle_theme/theme.h` and define one descriptor:

    const struct dte_theme dte_selected_theme = {
        DTE_ABI_VERSION, "theme-id", mount, gesture, render};

`mount(width, height, now)` initializes theme-owned state. `gesture(kind, now)`
receives normalized gestures. `render(snapshot, now, pixels)` writes the
borrowed RGB565 framebuffer and returns nonzero while another animation frame
is required. The host owns the buffer; a theme must neither free nor retain a
replacement pointer.

`DTE_ABI_VERSION` is currently 1. A mismatched descriptor is not mounted.

## State snapshot

`struct dte_snapshot` supplies WPM, layer index and name, endpoint, BLE profile,
modifier mask, display brightness, measured refresh rate multiplied by ten,
and dongle/split battery and connection values. Battery `-1` means unknown.
Connection `-1`, `0`, and `1` mean unknown, disconnected, and connected.

`battery_count` is 2 or 3. `startup_phase` is a host sequence value: 0 normal,
1 splash, 2 reveal. A theme may ignore every field it does not need.

Host and preview adapters update snapshots through `dte_set_state()`,
`dte_set_layer_name()`, `dte_set_battery_count()`,
`dte_set_display_stats()` and `dte_set_startup_phase()`. `dte_name_buffer()`
returns a writable 24-byte bridge for environments such as WASM; call
`dte_set_layer_name()` after writing it.

`dte_gesture_x()` and `dte_gesture_y()` expose the landscape-space touch
origin while a theme handles a gesture. Direct button or API gestures return
`-1`, allowing themes to retain their non-touch fallback behavior.
`dte_backlight_adjust()` applies a bounded relative change to the runtime
backlight and returns the new percentage; native/WASM updates the snapshot,
while the ZMK host also applies it to the configured backlight LED.
`dte_backlight_get()` reads the current runtime value.

## Lifecycle and rendering

`dte_init(width, height)` resets engine and touch state, initializes unknown
values, and mounts a compatible selected theme. `dte_render(now)` dispatches a
pending long press, invokes the theme renderer and returns its animation-active
result. `dte_pixels()`, `dte_width()`, `dte_height()` and `dte_hash()` expose the
current frame for hosts and deterministic tests.

The ZMK `dongle_screen_host` shield owns `zmk_display_status_screen()` and must
not be combined with another custom status-screen owner. State and rendering
run on ZMK's display work queue. With direct RGB565 enabled, changed tiles are
hashed and submitted as row bands or packed rectangles through the display
driver.

## Gestures

Gesture values are `DTE_TAP`, `DTE_LEFT`, `DTE_RIGHT`, `DTE_UP`, `DTE_DOWN` and
`DTE_LONG_PRESS`. `dte_gesture()` injects a normalized gesture directly.

`dte_touch(x, y, down, now)` feeds software recognition. A 28-pixel dominant
axis movement becomes a swipe; a stationary contact held for 600 ms becomes a
long press; a shorter stationary contact becomes a tap. `dte_touch_at()` keeps
the physical event timestamp separate from animation dispatch time.
`dte_touch_active()` remains nonzero from contact-down through release, allowing
a theme to provide a reversible hold effect without treating long press as a
latched gesture. It exposes no coordinates and does not bypass gesture dispatch.
`dte_touch_hint()` accepts one hardware-controller gesture per contact and
suppresses its software duplicate. `dte_touch_cancel()` clears incomplete
contact state.

The present ZMK adapter maps a portrait 240x280 CST816S input to a landscape
280x240 display. Other orientations require an adapted host transform.

## Raster API

Include `zmk/dongle_theme/raster.h`. Call `dtr_begin()` before drawing.
Available primitives include clear, pixel, rectangle, anti-aliased line,
radial mark, spindle, arc, density/reveal arc, simple bitmap text, and metallic
ring drawing. The bundled font is a small renderer resource, not a localization
system.

`dtr_pixel565()` writes an exact pre-quantized opaque RGB565 asset pixel without
running the RGB888 Bayer quantizer again. Use it for pixel-perfect sprite atlases;
use `dtr_pixel()` for coverage-blended vector and font edges.

RGB888 inputs are quantized to RGB565 with a fixed 4x4 Bayer matrix. The matrix
is anchored to destination coordinates so stationary frames do not shimmer.
Arc and line edges use coverage alpha rather than whole-screen noise.

Retained themes may call `dtr_damage_begin()`, `dtr_damage_rect()`,
`dtr_damage_ring()` or `dtr_damage_all()` before repainting. Dirty tiles are
16x16 pixels and may be read with `dtr_dirty_tiles()`.

## Transport API

`zmk/dongle_theme/transport.h` provides allocation-free rectangle and row-band
iterators. `dte_next_dirty_rect()` consumes a mutable tile-row bitmap and emits
non-overlapping rectangles capped at 2048 RGB565 pixels. `dte_next_dirty_band()`
groups dirty tile rows into at most 64 physical rows, below the nRF EasyDMA
65535-byte transfer limit at 280 pixels wide.

## Optional animation bridge

The public wrappers `dte_animation_options()`, `dte_set_animation()`,
`dte_get_animation()`, `dte_set_animation_duration()`,
`dte_get_animation_duration()` and `dte_force_redraw()` call weak optional
theme hooks. A theme that does not implement them receives inert defaults.
These controls are intended for preview and settings adapters; they are not a
runtime theme registry.
