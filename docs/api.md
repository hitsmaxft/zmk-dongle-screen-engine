# API manual

## Scope and version

Engine 1.2.0 exposes Theme ABI `0x0102`. It links one compile-time Theme and
supports 280×240, 240×280 and 240×240 RGB565 scenes. ABI 1.2 intentionally
replaces the v1/1.1 full-frame callback: firmware owns only a bounded transfer
strip, while native and WASM builds retain a preview-only full framebuffer.

Every cross-module structure starts with `abi_version` and `struct_size`, uses
fixed-width integers, and defines a `*_REQUIRED_SIZE`. ABI functions return the
fixed-width `int32_t` alias `dte_result_t`; C enums are constants only. The Engine reads or
writes only that required prefix and ignores a compatible caller tail. A wrong
ABI is rejected; it is never silently downgraded.

## Theme descriptor

Include `zmk/dongle_theme/theme.h` and define one descriptor:

```c
const struct dte_theme dte_selected_theme = DTE_THEME_INIT(
    "theme-id", DTE_THEME_CAP_GESTURE,
    mount, gesture, frame, draw);
```

- `mount(width, height, now)` initializes theme-owned state.
- `gesture(kind, now)` receives normalized gestures when the capability is set.
- `frame(snapshot, now, result)` advances state and publishes dirty rectangles.
- `draw(snapshot, now, canvas)` shades exactly the requested scene region.

`dte_validate_theme()` checks the required prefix and callbacks without
mounting. `dte_init_ex()` returns a `dte_status`; `dte_last_status()` reports the
latest status and `dte_active_abi_version()` is zero until mount succeeds.

Existing allocation-free raster themes may use
`zmk/dongle_theme/adapter.h` and `DTE_THEME_RASTER_ADAPTER`. The adapter first
executes the existing renderer without a pixel target to collect damage, then
replays it into each requested region. It is a migration aid, not a second ABI.

## Snapshot

`struct dte_snapshot` carries WPM, layer index/name, endpoint, BLE profile,
modifier mask, startup phase, brightness, measured refresh rate, battery values
and split connection state. Battery `-1` means unknown; connection `-1`, `0`
and `1` mean unknown, disconnected and connected. `valid_mask` states which
fields a caller intentionally supplies.

Initialize it with `DTE_SNAPSHOT_INIT` and submit it through
`dte_set_snapshot()`. Convenience setters remain available to the ZMK host and
preview shell. Input values are bounded before reaching a Theme, and the inline
layer name is always terminated.

## Frame contract

Initialize `struct dte_frame_result` with `DTE_FRAME_RESULT_INIT`, then call:

```c
dte_result_t status = dte_frame(now_ms, &result);
```

Dirty rectangles use scene coordinates and must be non-empty, in bounds, and
no more numerous than `DTE_MAX_DIRTY_RECTS`. The flags are:

- `DTE_RENDER_FRAME_CHANGED`: one or more output regions changed;
- `DTE_RENDER_CONTINUOUS`: schedule by the configured frame grid;
- `DTE_RENDER_DEADLINE_VALID`: use absolute monotonic
  `next_frame_at_ms`.

The host consumes explicit deadlines. A display transfer failure forces a full
scene repaint on the next attempt.

For retained rendering, each changing frame must include both previous and
current conservative bounds. A hidden or moved object therefore restores its
old location. If the rectangle list cannot represent the damage, return one
full-scene rectangle rather than truncating it.

## Region draw contract

After a successful `dte_frame(now, ...)`, call `dte_draw()` with the same
timestamp and an initialized `struct dte_canvas`:

```c
struct dte_canvas canvas = DTE_CANVAS_INIT;
canvas.scene_width = 280;
canvas.scene_height = 240;
canvas.origin_x = rect.x;
canvas.origin_y = rect.y;
canvas.width = rect.width;
canvas.height = strip_height;
canvas.stride_pixels = rect.width;
canvas.buffer_size = rect.width * strip_height * 2u;
canvas.pixels = strip_pixels;
```

The RGB565 buffer is borrowed only for the call. Coordinates remain in scene
space; `origin_x` and `origin_y` map them into the region. The Engine rejects a
wrong pixel format, invalid stride, out-of-bounds region, insufficient buffer,
or a timestamp not prepared by `dte_frame()`.

The ZMK host subdivides dirty rectangles into at most
`ZMK_DONGLE_SCREEN_STRIP_PIXELS` and synchronously passes each strip to the
display driver. The default is 4,480 RGB565 pixels, or 8,960 bytes. Neither the
Engine nor LVGL owns a full animation framebuffer in firmware.

`dte_render()` and preview-only `dte_pixels()`/`dte_hash()` assemble the same
regions into a full buffer for native/WASM tools. They are not firmware storage
contracts.

## Gestures and optional controls

Gesture values are `DTE_TAP`, `DTE_LEFT`, `DTE_RIGHT`, `DTE_UP`, `DTE_DOWN` and
`DTE_LONG_PRESS`. `dte_touch()` performs software recognition;
`dte_touch_hint()` accepts one controller gesture per contact and suppresses
its software duplicate. `dte_touch_at()` separates the physical event time
from animation dispatch time. `dte_gesture_x()` and `dte_gesture_y()` expose a
touch origin only during the callback.

The animation selection, duration, forced-redraw and backlight wrappers remain
optional weak bridges for previews and settings adapters. They do not create a
runtime Theme registry.

## Raster and shared utilities

`zmk/dongle_theme/raster.h` provides RGB565 primitives, fixed Bayer dithering,
text and metallic ring helpers. Retained themes may declare 16×16 tile damage
with `dtr_damage_begin()`, `dtr_damage_rect()`, `dtr_damage_ring()` and
`dtr_damage_all()`.

`zmk/dongle_theme/ui.h` provides allocation-free sprite and small UI helpers.
Theme palettes, layouts, animation policy, fonts and asset payloads remain in
the Theme module.

## Preview reliability gate

`scripts/build_preview.py` compiles native and WASM binaries, then runs an ABI
probe before producing the final HTML. The probe covers:

- active ABI identity and unsupported-version rejection;
- required-prefix structures with protected tail canaries;
- draw-before-frame and timestamp mismatch rejection;
- dirty rectangle count and scene bounds;
- valid region draw and insufficient-buffer rejection;
- exact native/WASM result equality.

`scripts/test_preview.py` adds 81-frame replay over three viewports, touch and
long-press lifecycle checks, deterministic repeat, and full RGB565 hash parity.
This validates software behavior; physical SPI timing and panel output remain
separate hardware gates.
