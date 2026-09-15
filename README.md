# zmk-dongle-screen-engine

A small static theme host and RGB565 renderer for ZMK dongles. It separates
display ownership, ZMK state projection, touch dispatch and frame transport
from the linked visual theme. Theme code stays in its own Zephyr module and
defines one `dte_selected_theme` descriptor.

This repository contains the engine only. It does not contain downstream
themes, artwork, generated theme atlases, or product-specific animation labs.

## Current contract

The bootstrap ABI supports one compile-time theme, 240x240, 240x280 and
280x240 RGB565 surfaces, WPM/layer/endpoint/modifier/battery snapshots, tap,
long press and four swipe directions, a 60 Hz deadline, fixed Bayer RGB565
dithering, retained tile damage and optional direct display writes.

The host shield owns `zmk_display_status_screen()`. Do not combine it with
another custom status-screen shield. The present touch adapter targets the
Prospector-style portrait CST816S to landscape transform; other panel
orientations should provide an adapted host transform. Runtime theme registry,
safe fallback and arbitrary touch transforms are planned, not claimed by this
bootstrap release.

## ZMK integration

Add this repository and a theme repository as Zephyr modules in the ZMK west
manifest, then include these shields in the dongle build:

    <display-and-touch-hardware> dongle_theme_host <your-theme-shield>

The theme must include `zmk/dongle_theme/theme.h` and define exactly one
`const struct dte_theme dte_selected_theme`. See `examples/minimal-theme` for a
complete theme and preview manifest.

Useful Kconfig options include `ZMK_DONGLE_THEME_FPS`,
`ZMK_DONGLE_THEME_BRIGHTNESS`, `ZMK_DONGLE_THEME_DIRECT_RGB565`,
`ZMK_DONGLE_THEME_PACKED_RECTS` and `ZMK_DONGLE_THEME_DONGLE_BATTERY`.

## Native and WASM preview

The preview compiles the same engine, rasterizer and theme C source for native
execution and WASM. Point `--lvgl` at an LVGL source tree containing the
Montserrat font sources:

    python3 scripts/build_preview.py \
      --lvgl /path/to/lvgl \
      --theme examples/minimal-theme \
      --output .build/minimal-preview
    python3 scripts/test_preview.py .build/minimal-preview

Open `.build/minimal-preview/index.html` locally. The replay test requires
Clang with wasm32 support and Node.js; it verifies matching native/WASM RGB565
frame hashes, long-press de-duplication and stable spatial dithering.

## License

Engine and example code are MIT licensed. Generated preview font data retains
the LVGL MIT and Montserrat OFL notices from its source tree.
