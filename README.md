# zmk-dongle-screen-engine

A small static theme host and RGB565 renderer for ZMK dongles. It separates
display ownership, ZMK state projection, touch dispatch and frame transport
from the linked visual theme. Theme code stays in its own Zephyr module and
defines one static ABI 1.3 descriptor.

Current development version: **1.3.0** with Theme ABI **1.3** and TRE ABI
**1.0**. Firmware supports bounded RGB565 strips or an optional retained full
framebuffer. Native and WASM previews assemble pixels for inspection and replay.

This repository contains the engine only. It does not contain downstream
themes, artwork, generated theme atlases, or product-specific animation labs.

## Agent entry points

Agents should begin with [AGENTS.md](AGENTS.md) for repository constraints.
Task-specific guidance lives under `skills/`; typography, font generation and
glyph-icon work must follow the [UI design skill](skills/ui-design/SKILL.md).
The [API manual](docs/api.md) and [development guide](docs/development.md) are
the corresponding entry points for interface and build/preview work.

![Localized WASM theme preview with live state and gesture controls](docs/images/wasm-preview-zh.png)

## Highlights

### Same renderer in firmware and the browser

The preview builder compiles the engine, rasterizer and selected theme C sources
to both a native library and WASM. The generated HTML is self-contained and can
run offline. Live controls inject WPM, layers, endpoints, modifiers, two/three
battery layouts and gestures without a keyboard attached. The preview shell is
localized in English, Simplified Chinese and Japanese; text drawn inside the
simulated display remains entirely theme-owned.

The parity replay compares native and WASM RGB565 hashes across three display
sizes. It also verifies long-press de-duplication and that fixed spatial Bayer
dithering does not shimmer between stationary frames.

The nRF52840 preset estimates CPU and SPI budgets while replaying the strip
render path. It reports dirty regions, draw calls and payload bytes. It does
not model every firmware mode or physical scanout; calibrate it against device
logs. Manual interaction playback can extend animation time to show intermediate
phases. See [performance measurement](docs/development.md#animation-and-performance).

### Optional CRT glass filter

`CONFIG_ZMK_DONGLE_SCREEN_FILTER_CRT` enables an allocation-free RGB565
post-process after Theme drawing and before tile hashing. It adds asymmetric
non-linear edge darkening, configurable rounded black corners, a mint top
reflection and a darker bottom edge. Firmware uses
`CONFIG_ZMK_DONGLE_SCREEN_FILTER_CRT_CORNER_RADIUS`; it performs no barrel
warp and adds no framebuffer. The default remains off, so existing Theme
output and ABI 1.3 descriptors are unchanged.

The WASM preview exposes the same filter under **Animation parameters → Frame
filter** and a live corner-radius slider. These controls redraw without
resetting Theme time, input or locale. The default physical mask is a generic
1.69-inch rounded screen at R43; it affects only browser presentation.
`render_wasm.cjs` accepts `--filter crt --filter-radius 43` for browser-free
PNG evidence.

![CRT glass filter preview](docs/images/crt-filter-mac-tiled.png)

### TRE reusable render core

ABI 1.3 extracts TRE (Tiled Render Engine) beneath the Theme API. TRE provides
explicit render contexts, bounded RGB565 surfaces, keyed/masked images, A4 and
MONO1 coverage, atlas tiles, glyph coverage and caller-owned damage tracking.
It has no ZMK or theme-state dependencies and is intended to be shared by Theme
1.x, the `.zds` runtime and later frontends. Existing `dtr_*`/`dte_ui_*` calls
remain compatibility and effects helpers. See the [API manual](docs/api.md).

The preset uses 24 FPS, 32 MHz SPI and one 4,480-pixel strip (8,960 bytes).
Its 256 KiB SRAM and 1 MiB Flash figures are device capacities, not linked
usage. Supply a Zephyr map for firmware measurements. Custom and unlimited
profiles support comparison; logical, estimated and browser FPS are distinct.

![English WASM preview showing the engine controls](docs/images/wasm-preview-en.png)

### Animation without a fixed player

A theme owns its animation state machine. Its `frame(snapshot, now, result)`
callback publishes dirty rectangles and its `draw(snapshot, now, canvas)`
callback shades one requested region. Flags distinguish continuous animation
from an exact absolute deadline, which the ZMK host consumes directly. `now` is explicit, so
native tests, WASM and firmware can replay the same timeline deterministically.
Optional animation selection, duration and forced-redraw hooks are available to
preview/settings adapters without making the engine interpret theme semantics.

The default deadline rate is 60 FPS; builds may select 1–60 FPS. Actual
throughput depends on drawing and transport. Full-framebuffer mode retains
134,400 pixel bytes, draws merged dirty bounds once and hashes changed tiles.
Neither mode guarantees tear-free output without panel synchronization.

### Touch normalization

The engine accepts pointer samples through `dte_touch()` and emits tap, long
press and four directional swipes. `dte_touch_hint()` merges controller-provided
gesture hints with software recognition once per contact, preventing a hardware
swipe followed by a duplicate software swipe. Input callbacks enqueue samples;
theme callbacks and drawing remain serialized on the display work queue.
Themes may inspect the landscape-space gesture origin to partition a screen
without changing the theme descriptor. A bounded runtime backlight command
updates the WASM/native snapshot and delegates physical PWM to the ZMK host.

See [the API manual](docs/api.md) for descriptor, snapshot, animation, touch,
raster and transport contracts. Theme authors should also follow the
[development workflow](docs/development.md) and the
[pixel-art porting guide](docs/pixel-art-porting.md).

### RGB565 visual evidence

`scripts/compare_reference.py` compares a raw native/WASM framebuffer with a
reference after both enter the same RGB565 domain. It reports regional pixel
differences, generated-only bright speckles and local brightness spikes, and
can make selected regions strict CI gates. Pixel-art themes can request
nearest-neighbor preview scaling through their manifest without changing the
firmware renderer.

## Current contract

ABI 1.3 supports one compile-time theme, 240x240, 240x280 and
280x240 RGB565 surfaces, WPM/layer/endpoint/modifier/battery snapshots, tap,
long press and four swipe directions, a configurable frame cadence, fixed Bayer RGB565
dithering, retained tile damage and bounded direct display writes.

The host shield owns `zmk_display_status_screen()`. Do not combine it with
another custom status-screen shield. The present touch adapter targets the
Prospector-style portrait CST816S to landscape transform; other panel
orientations should provide an adapted host transform. Runtime theme registry,
safe fallback and arbitrary touch transforms are planned, not claimed by this
bootstrap release.

## ZMK integration

Add this repository and a theme repository as Zephyr modules in the ZMK west
manifest, then include these shields in the dongle build:

    <display-and-touch-hardware> dongle_screen_host <your-theme-shield>

The theme must include `zmk/dongle_theme/theme.h` and define
`const struct dte_theme dte_selected_theme` with `DTE_THEME_INIT`. Its `frame`
and `draw` callbacks use sized, fixed-width ABI structures. Existing raster
themes can migrate with `DTE_THEME_RASTER_ADAPTER`, then move to native region
callbacks when useful. See the [API manual](docs/api.md), `tests/api_v1_2.c`,
and `examples/minimal-theme`.
The short [theme examples](docs/examples.md) page also links external visual
references.

Reusable RGB565 sprite and small pixel-UI helpers live in
`zmk/dongle_theme/ui.h`; keep theme palettes, layouts, animation policy, and
asset payloads in the theme module.

### Typography and icon fonts

![Recommended ZDSE font specimens](docs/images/font-recommendations.png)

Use [Spleen](https://github.com/fcambus/spleen) for compact Latin status text
and [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) for small
multilingual pixel UI. Use LVGL's Montserrat for large labels and values, and a
strictly subsetted JetBrains Mono NL Nerd Font Mono for modifier/status icons.
The specimen shows Spleen 8x16 and Fusion Pixel Font 12 px at 2x nearest-neighbor
scale, plus Montserrat and Nerd Font at 40 px.

Font payloads remain opt-in theme assets and are not linked into Engine by
default. Theme authors and agents should use the
[UI design skill](skills/ui-design/SKILL.md) for selection, subsetting,
licensing and RGB565 validation rules; this README is only the short overview.

Useful Kconfig options include `ZMK_DONGLE_SCREEN_FPS`,
`ZMK_DONGLE_SCREEN_BRIGHTNESS`, `ZMK_DONGLE_SCREEN_STRIP_PIXELS` and
`ZMK_DONGLE_SCREEN_DONGLE_BATTERY`.

## Native and WASM preview

The preview compiles the same engine, rasterizer and theme C source for native
execution and WASM. Point `--lvgl` at an LVGL source tree containing the
Montserrat font sources:

    python3 scripts/build_preview.py \
      --lvgl /path/to/lvgl \
      --theme examples/minimal-theme \
      --output .build/minimal-preview
    python3 scripts/test_preview.py .build/minimal-preview

Render an exact WASM framebuffer directly to PNG before opening a browser:

    node scripts/render_wasm.cjs .build/minimal-preview \
      --time 1000 --output .build/minimal-preview/frame-1000.png

Theme-specific integer setters may be applied with repeatable `--call
NAME=A,B`; deterministic gestures use `--gesture KIND@MS`. The command prints
the logical size, frame hash and PNG SHA-256. Browser inspection is reserved
for interactive controls, localization and responsive layout rather than
routine framebuffer review.

For animation evidence, `--frames 240 --fps 24 --output frame.png` drives one
WASM instance continuously and writes `frame-0000.png` through
`frame-0239.png`; an external encoder may package that deterministic sequence
as GIF, WebP or video without browser capture.

Open `.build/minimal-preview/index.html` locally. The replay test requires
Clang with wasm32 support and Node.js; it verifies matching native/WASM RGB565
frame hashes, long-press de-duplication and stable spatial dithering.
`build_preview.py` first runs a fast native/WASM ABI gate, so it will not emit a
usable HTML preview when versions, required-prefix handling, dirty bounds,
frame/draw pairing or region capacity checks disagree. `test_preview.py` then
runs the longer 90-frame, three-viewport parity replay.

Themes with `variants` may also declare `common_sources`,
`profile_variants`, and localized-independent `variant_display_names`. Build
all declared profiles into one verified, self-contained page with:

    python3 scripts/build_preview.py \
      --lvgl /path/to/lvgl --theme /path/to/theme \
      --all-variants --output .build/theme-profiles

Each profile is linked and native/WASM parity-tested independently. The page
switches cached modules without reloading, reapplies Engine-visible state and
logical time, and forces a clean redraw. Existing single-source and
`--variant` commands retain their prior output contract.

## GitHub Action

Downstream theme repositories can build and verify the same self-contained HTML
without copying Engine scripts:

```yaml
- uses: actions/checkout@v7
- uses: actions/checkout@v7
  with:
    repository: zmkfirmware/lvgl
    ref: f1db87ee98f1810328a8419572fa42a3b5f352ae
    path: .preview-deps/lvgl
- id: preview
  uses: hitsmaxft/zmk-dongle-screen-engine/.github/actions/build-preview@v1.2.0
  with:
    theme-path: themes/my-theme
    lvgl-path: .preview-deps/lvgl
    output-path: site/my-theme
    # all-variants: 'true'  # optional profile bundle
    target-fps: '24'
    spi-mhz: '32'
    dirty-percent: '20'
    # firmware-map: build/zephyr/zmk.map
    # baseline-budget-report: baseline/budget-report.json
    # max-firmware-ram-bytes: '131072'
```

The Action installs the Linux Clang/WASM toolchain when necessary, builds the
native and WASM renderers, runs parity replay, and exposes `index-path`,
`output-path`, `budget-report` and `budget-summary` only after verification
succeeds. Budget reports estimate RGB565 SPI payload and strip RAM; when given
a final Zephyr map they also report `_flash_used` and `_image_ram_size`, compare
an optional baseline and enforce configured ceilings. Pin the Engine tag and
LVGL revision in release workflows.

## License

Engine and example code are MIT licensed. Generated preview font data retains
the LVGL MIT and Montserrat OFL notices from its source tree.
