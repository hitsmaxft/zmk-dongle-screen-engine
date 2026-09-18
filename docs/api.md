# API manual

## Scope

`zmk-dongle-screen-engine` is a compile-time ZMK screen host. It links exactly
one Theme through the v1 `dte_selected_theme` or ABI 1.1
`dte_selected_theme_v1_1`; it does not yet implement a runtime registry,
persistent theme selection, or a safe fallback theme.

The current host accepts 280x240, 240x280 and 240x240 displays. Its validated
raster and transport path is RGB565. Theme-local localization, fonts and copy
remain the theme's responsibility. The WASM preview shell localization is not
part of the firmware ABI.

The published v1 API is frozen and remains source-compatible. ABI 1.1 is additive:
it uses separately named structures and entry points rather than extending v1
structures in place. This is a static-link firmware ABI; it does not promise
that separately compiled binary plugins can move between toolchains.

These APIs ship in Engine 1.1.1. Engine release 1.1 and Theme ABI 1.1 are
separate version domains; the ABI addition does not make the Engine a 2.0
release. Compile-time Engine version macros are `DTE_ENGINE_VERSION_MAJOR`,
`DTE_ENGINE_VERSION_MINOR`, `DTE_ENGINE_VERSION_PATCH` and
`DTE_ENGINE_VERSION_STRING`.

The preview-only hardware model reads `dtr_dirty_tiles()` after each WASM
render, estimates full-frame, dirty-band or packed-tile transfer bytes, and
throttles presentation by target FPS plus estimated CPU/SPI cost. Profile
changes never reinitialize WASM or mutate the theme snapshot. The nRF52840
preset is deliberately conservative and must be calibrated against physical
display measurements before treating its FPS as a hardware claim.

## Theme descriptors

### Frozen v1

Include `zmk/dongle_theme/theme.h` and define one descriptor:

    const struct dte_theme dte_selected_theme = {
        DTE_ABI_VERSION, "theme-id", mount, gesture, render};

`mount(width, height, now)` initializes theme-owned state. `gesture(kind, now)`
receives normalized gestures. `render(snapshot, now, pixels)` writes the
borrowed RGB565 framebuffer and returns nonzero while another animation frame
is required. The host owns the buffer; a theme must neither free nor retain a
replacement pointer.

`DTE_ABI_VERSION` is currently 1. A mismatched descriptor is not mounted.

### Additive ABI 1.1

An ABI 1.1 Theme defines `dte_selected_theme_v1_1` with the initializer macro:

    const struct dte_theme_v1_1 dte_selected_theme_v1_1 = DTE_THEME_V1_1_INIT(
        "theme-id", DTE_THEME_CAP_GESTURE, mount, gesture, render);

`struct dte_theme_v1_1`, `struct dte_snapshot_v1_1` and
`struct dte_render_result_v1_1` begin with `abi_version` and `struct_size`.
The Engine rejects an unknown major version or a structure shorter than its
required prefix, and ignores a longer compatible tail. All state scalars use
fixed-width integer types. A present but invalid ABI 1.1 descriptor is reported as
an error and is not silently replaced by v1; when both valid descriptors are
linked, ABI 1.1 takes precedence.

`dte_validate_theme_v1_1()` validates a descriptor without mounting it.
`dte_init_ex()` returns a `dte_status`; `dte_last_status()` reports the most
recent status and `dte_active_abi_version()` reports 0, legacy v1 value `1`, or
ABI 1.1 value `0x0101`. The legacy
`dte_init()` wrapper calls the same core and discards the status.

| Linked descriptors | Selected API | Invalid selection behavior |
| --- | --- | --- |
| v1 only | v1 | initialization returns the v1 validation error |
| ABI 1.1 only | ABI 1.1 | initialization returns the ABI 1.1 validation error |
| valid v1 and valid ABI 1.1 | ABI 1.1 | ABI 1.1 deterministically takes precedence |
| valid v1 and invalid, present ABI 1.1 | none | ABI 1.1 error is returned; no silent fallback |

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

ABI 1.1 uses `struct dte_snapshot_v1_1`, `DTE_SNAPSHOT_V1_1_INIT` and a `valid_mask`.
`dte_set_snapshot_v1_1()` validates its prefix, clamps values to the same ranges
as v1, copies the inline layer name, and ignores unknown tail bytes. Existing
v1 setters update the ABI 1.1 snapshot too, so an unchanged ZMK host can feed an ABI 1.1
Theme during migration.

`dte_gesture_x()` and `dte_gesture_y()` expose the landscape-space touch
origin only while a Theme handles that gesture. They return `-1` immediately
after the callback. Direct button or API gestures return `-1` during and after
the callback, allowing themes to retain their non-touch fallback behavior.
`dte_backlight_adjust()` applies a bounded relative change to the runtime
backlight and returns the new percentage; native/WASM updates the snapshot,
while the ZMK host also applies it to the configured backlight LED.
`dte_backlight_get()` reads the current runtime value.

## Lifecycle and rendering

`dte_init(width, height)` resets engine and touch state, initializes unknown
values, and mounts a compatible selected theme. `dte_render(now)` dispatches a
pending long press, invokes the active v1 or ABI 1.1 renderer and retains its legacy
boolean scheduling result. `dte_pixels()`, `dte_width()`, `dte_height()` and
`dte_hash()` expose the current frame for hosts and deterministic tests.

`dte_render_v1_1()` returns status and fills a caller-initialized
`DTE_RENDER_RESULT_V1_1_INIT`. Its flags separately describe framebuffer change,
continuous animation and an absolute monotonic `next_frame_at_ms` deadline.
Unknown result flags are ignored. The present ZMK adapter still consumes the
legacy boolean wrapper and therefore limits active ABI 1.1 themes at the configured
FPS; exact deadline scheduling is not yet claimed by the firmware host.

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

## Shared theme utilities

Include `zmk/dongle_theme/ui.h` for allocation-free utilities that are useful
across themes. `struct dte_sprite` is a borrowed RGB565 view;
`dte_sprite_blit()` treats `0x0001` as a transparent key, while
`dte_sprite_blit_opaque()` copies every pixel exactly. The same header provides
solid or alpha rectangles, a small rounded rectangle, bounded number/percent
formatters, and a deterministic three-input hash. Theme-specific palettes,
widgets, fonts, layouts, and asset payloads do not belong in the Engine.

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
