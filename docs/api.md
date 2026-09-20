# API manual

## Scope and version

Engine 1.3.0 exposes Theme ABI `0x0103` and TRE ABI `0x0100`. It links one
compile-time Theme and supports 280×240, 240×280 and 240×240 RGB565 scenes.
The Theme region contract remains the bounded ABI 1.2 design; ABI 1.3 adds the
Theme-independent TRE render core shared by firmware, native and WASM builds.

Every cross-module structure starts with `abi_version` and `struct_size`, uses
fixed-width integers, and defines a `*_REQUIRED_SIZE`. ABI functions return the
fixed-width `int32_t` alias `dte_result_t`; C enums are constants only. The Engine reads or
writes only that required prefix and ignores a compatible caller tail. A wrong
ABI is rejected; it is never silently downgraded.

## TRE render core

TRE is the allocation-free, integer-first layer beneath Theme code. Its public
headers are independent of ZMK state and live under `include/tre/`:

- `surface.h` defines a bounded caller-owned RGB565 LE surface/view;
- `render.h` defines an explicit render context, clip, pixel, fill and line;
- `image.h` defines opaque/keyed RGB565, RGB565+A1, MONO1 and A4 image views,
  source rectangles, X/Y flips and minimal coverage glyphs;
- `damage.h` accepts caller-owned storage while keeping its representation
  private;
- `tile.h` draws fixed-size atlas cells without introducing a scene graph.

All exposed records use a sized prefix and TRE's independent ABI version.
`tre_damage_storage_size()` and `tre_damage_storage_align()` let callers reserve
bounded storage without a heap. If a rectangle list cannot represent collected
damage, TRE reports one full-scene rectangle rather than truncating damage.

The existing `dtr_*` and `dte_ui_*` APIs remain compatibility/effects facades.
Opaque/keyed sprite draws borrow a short-lived TRE surface/context view, so the
facade adds no persistent renderer state. Theme 1.x, future `.zds` execution
and later frontends thereby converge on the same surface and image rules.
TRE deliberately contains no WPM, battery, BLE, animation, scene-graph or VM
semantics.

### Initial firmware footprint

The ABI 1.3 development branch was linked with section garbage collection for
two nRF52840 Cornix builds. The compatibility facade keeps no persistent TRE
context; its surface/context view is bounded stack state during a sprite draw.
Unused image, damage, tile and glyph routines are discarded independently.
Radar and Neon Cat each matched ABI 1.2 framebuffer hashes exactly over a
36-frame, three-viewport compatibility replay.

Final whole-image measurements are recorded from the release-candidate build,
rather than inferred from individual symbols. Physical panel throughput remains
outside this software/link validation.

With identical nRF52840 Cornix configurations, Radar remained exactly 522,076
Flash bytes and 126,888 RAM bytes from ABI 1.2 to 1.3 because unused TRE code
was link-collected. Neon Cat changed from 708,284 to 710,332 Flash bytes
(+2,048) while RAM remained exactly 124,840 bytes. Its opaque/keyed row fast
path reduced a 60-frame WASM replay median from 580.2 to 404.4 microseconds per
frame (-30.3%); this is a software comparison, not an nRF52840 cycle count.

Framebuffer and dirty hashes remained exact, so ABI 1.3 changes no SPI payload.
At 280×240 RGB565, 24 FPS and 20% dirty area, the declared model is 26,880
bytes/frame, 645,120 bytes/s and 6.72 ms transfer time at 32 MHz (16.13% of the
frame budget). Window commands, DMA gaps and physical-panel timing are excluded.

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

Before each strip is sent, the host hashes its 16×16 RGB565 output tiles against
the last successfully produced tile hash. Only changed tiles are packed into
bounded display rectangles. This preserves the ABI's caller-owned strip RAM
model while avoiding an SPI full-frame transfer merely because a Theme supplied
a conservative damage bound. A failed write invalidates the optimization and
forces the next presentation to cover the full scene.

Presentation policy follows frame semantics. Non-continuous state updates use
tile-hash packing to minimize payload. `DTE_RENDER_CONTINUOUS` animation frames
merge their dirty rectangles into one conservative scene region and submit its
strips in top-to-bottom order. This avoids exposing a single animation frame as
temporally scattered tile writes on panels without a TE/vsync signal. Themes
still need realistic damage bounds; this policy improves coherence but cannot
make an SPI update atomic.

`dte_render()` and preview-only `dte_pixels()`/`dte_hash()` assemble the same
regions into a full buffer for native/WASM tools. They are not firmware storage
contracts.

The preview-only strip controls and counters replay the firmware order
`frame → region → strip → draw`, including the configured strip height and
tile-hash payload filtering. They exist for hardware simulation; Theme code
must not depend on them.

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

`zmk/dongle_theme/raster.h` provides ABI 1.x compatibility primitives, fixed Bayer dithering,
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
