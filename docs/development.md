# Theme development

Compile the same C renderer for firmware, native tests and WASM. Keep layout,
assets and animation policy in the theme. See the [API manual](api.md) for
interfaces and [Pixel-art porting](pixel-art-porting.md) for asset acceptance.

## Build and verify

```sh
python3 scripts/build_preview.py \
  --lvgl /path/to/lvgl --theme /path/to/theme \
  --variant default --output .build/default-v001
python3 scripts/test_preview.py .build/default-v001
node scripts/render_wasm.cjs .build/default-v001 \
  --time 1000 --output .build/default-v001/frame-1000.png
```

The builder checks ABI versions, structure boundaries, dirty rectangles,
frame/draw timestamps and buffer capacity before emitting HTML. Replay then
checks native/WASM pixels, determinism and touch semantics at three sizes.
Save each preview in a new directory. Inspect raw framebuffer PNGs for pixel
quality; use browser tests for controls, localization and responsive layout.
CSS-scaled screenshots do not prove framebuffer accuracy.

## Compare reference pixels

`compare_reference.py` compares artwork with PNG/PPM exported from native or
WASM memory. It maps the reference with nearest-neighbor sampling and compares
both images in RGB565.

```sh
python3 scripts/compare_reference.py \
  --reference /path/to/theme/arts/reference.jpg \
  --rendered .build/default-v001/idle.png \
  --output .build/default-v001/visual-diff.png \
  --report .build/default-v001/visual-diff.json \
  --region function_area:0,151,280,240 --strict function_area
```

Repeat `--region NAME:X0,Y0,X1,Y1` as needed; `full_frame` is always included.
Reports contain changed pixels, ratios, mean absolute error, generated-only
white speckles and local brightness spikes. Heatmaps use red/blue for channel
differences, yellow for speckles, cyan for spikes and black for equal pixels.
Any nonzero metric fails a strict region.

Speckle detection defaults to every channel being at least 170. A spike must
have brightness at least 205 and exceed its 3×3 neighborhood median by 96.
Adjust with `--white-floor`, `--brightness-floor` and `--spike-delta`. Tests may
call `compare()` directly. References, regions and tolerances belong to themes.

## Animation and performance

Compare fixed snapshots and timestamps. Configured FPS is a scheduling target;
native/WASM timings are not nRF52840 measurements. The preview estimates CPU
and transport cost through the strip path (`frame → region → strip → draw`),
not every firmware mode or physical panel scanout.

Manual preview interactions under hardware profiles currently serialize four
phases: 25%, 50%, 75% and 100%. This may extend wall-clock duration and must not
be treated as firmware timing evidence. Unlimited mode uses its configured
cadence; new interactions clear prior presentation deadlines.

`DTE_THEME_RASTER_ADAPTER` replays a legacy renderer for each requested region.
High-cost themes should separate frame planning from drawing. Declare old and
new bounds only for visible components that change. Validate reverse motion,
all animation variants and clipped rendering against full redraws.

The low-RAM host sends continuous animation as ordered strips over a merged
bound; discrete updates use tile-hash packing. Full-framebuffer mode draws
the merged bound once, retains a raster tile mask and sends changed tiles.
Neither path guarantees tear-free output without panel synchronization.

Arcs and spindle pointers use conservative scanline bounds. The fixed-point
arc path also rejects pixels outside angular coverage before distance lookup.
Use `dtr_arc_bands()` for adjacent one-pixel bands instead of repeating geometry
work. Speed builds store symmetric Q8 distances in a 141-axis triangular table
(20,022 bytes), covering side arcs beyond the older 128-axis square table.
Background fills process contiguous damaged spans; per-pixel color and damage
decisions are unnecessary for a solid run. Both optimizations preserve RGB565
output and need no additional framebuffer.
Two optional firmware settings affect performance:

- `CONFIG_ZMK_DONGLE_SCREEN_OPTIMIZE_SPEED` selects Engine `-O3` and Q8/Q15
  arc coverage. Defaults retain floating-point coverage. Compare pixel error
  and hardware timing. Preview the same coverage path with
  `DTE_PREVIEW_DEFINES=CONFIG_ZMK_DONGLE_SCREEN_OPTIMIZE_SPEED=1`; the manifest
  records it. Theme manifests may also declare a `defines` list.
- `CONFIG_ZMK_DONGLE_SCREEN_MAX_THROUGHPUT` continues over-budget animation
  after a 1 ms queue yield instead of another FPS grid boundary. Measure
  keyboard, BLE and touch latency alongside frame rate.

## Filters and physical mask

Filters run after drawing and before hashing, in scene coordinates. CRT adds
edge darkening, rounded corners and subtle highlights without geometry warp
or an extra framebuffer. Enable `CONFIG_ZMK_DONGLE_SCREEN_FILTER_CRT`; set
its inner radius with `CONFIG_ZMK_DONGLE_SCREEN_FILTER_CRT_CORNER_RADIUS`.

The generic 1.69-inch R43 screen mask affects browser presentation only and is
independent of the CRT radius. Filter controls preserve theme time, input and
locale. Export filter evidence directly:

```sh
node scripts/render_wasm.cjs .build/default-v001 \
  --time 1000 --filter crt --filter-radius 43 --output .build/default-v001/crt.png
```

## Budgets and CI

Use `.github/actions/build-preview` for verified HTML and budget reports;
see the [README](../README.md#github-action) for integration. Pin Engine and
LVGL revisions for releases.

```sh
python3 scripts/report_budget.py \
  --preview-manifest .build/default/preview-manifest.json \
  --firmware-map /path/to/zephyr/zmk.map \
  --baseline .build/baseline/budget-report.json \
  --output .build/default/budget-report.json \
  --markdown .build/default/budget-summary.md \
  --width 280 --height 240 --fps 24 --spi-mhz 32 \
  --dirty-percent 20 --max-spi-utilization-percent 85
```

Defaults model 280×240 RGB565, 24 FPS, 32 MHz SPI, 20% dirty area and a
4,480-pixel strip. Override them for the target. Firmware Flash/RAM come from
`_flash_used` and `_image_ram_size` in the final map; without it they are
unavailable. WASM size is not firmware Flash usage.

Compare the same board, shield, Kconfig and toolchain. SPI figures estimate
payload, transfer time and budget utilization. Measure command overhead, DMA
gaps, task contention and panel timing on hardware.
