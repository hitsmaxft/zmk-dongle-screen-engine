---
name: ui-design
description: Design and review ZMK dongle-screen theme UI, including typography, iconography, render-contract selection and framebuffer tuning for small RGB565 displays.
---

# Dongle-screen UI typography

Choose fonts by rendered size and content rather than applying one family to the whole theme. Keep optional font payloads in the theme that uses them; this guidance does not make them part of the default Engine build.

## Recommended families

| Use | Recommended font | Guidance |
| --- | --- | --- |
| Small Latin text, compact numbers, debug/status labels | Spleen | Use a native bitmap strike at an exact integer position. Do not rescale or antialias it. |
| Small multilingual or CJK pixel text | Fusion Pixel Font | Select a fixed-size strike and subset the required language glyphs. Preserve its pixel grid with nearest-neighbor rendering. |
| Large labels, headings, counters and prominent values | LVGL Montserrat | Generate or enable only the sizes and glyph ranges the theme uses. Judge weight and spacing on the physical RGB565 panel. |
| Modifier and status icons | JetBrains Mono NL Nerd Font Mono | Extract only the required Nerd Font codepoints into a dedicated icon font. Do not ship the complete Nerd Font. |

As a starting point, prefer bitmap fonts around 8-16 px and Montserrat from roughly 18 px upward. These are selection guides, not hard cutoffs: choose from native-size screenshots and panel tests.

## Composition rules

- Keep text and icon fonts separate. Give each an explicit size-matched fallback instead of relying on an accidental global LVGL default.
- Place bitmap glyphs on integer coordinates and use integer scaling only. Fractional transforms and filtering destroy their intended strokes.
- Use Montserrat for hierarchy and legibility, not for dense microtext. Enable only the LVGL sizes, weights, and Unicode ranges actually referenced.
- Treat Nerd Font glyphs as icons: name their codepoints in source, keep a visible text or semantic label where the preview UI needs one, and verify their baseline beside text.
- For localized canvas text, decide the required scripts before subsetting. Fusion Pixel Font is the preferred pixel-style CJK option; do not silently fall back to missing-glyph boxes.
- Preserve each upstream font's license and attribution with generated C data or the theme's third-party notices.

## Asset and build boundary

Convert fonts to generated C assets at build or release preparation time; firmware must not parse TTF/OTF files at runtime. Store theme-specific generated glyph data in the theme module and compile it only when that theme is selected. Shared conversion helpers may live in Engine, but full font payloads and theme-specific subsets must remain optional.

For LVGL conversion, constrain `--size`, `--bpp`, `--range` or `--symbols`, and define a deliberate fallback of the same nominal size. For bitmap families, prefer their supplied strike over rasterizing a scalable approximation.

## Validation

Review typography in an exact-size RGB565 framebuffer before browser zoom. Check:

- missing glyphs and fallback behavior;
- baseline, line height and mixed text/icon alignment;
- clipping at every supported display size and locale;
- small-stroke survival after RGB565 conversion;
- native/WASM framebuffer parity;
- Flash and RAM deltas for every newly enabled size or glyph range.

Browser enlargement is useful for inspection, but it is not evidence of physical-panel legibility. Record panel validation separately when hardware is available.

## Rendering contract and backend

Every ABI 1.4 theme must choose its drawing contract deliberately:

- `DTE_THEME_RENDER_FULL_SCENE_ONLY` is the simple default for theme code. Its draw callback may assume a full-canvas target and must use the full-framebuffer backend.
- `DTE_THEME_RENDER_REGION_CAPABLE` promises correct drawing into every valid canvas region, including the full canvas. It therefore runs unchanged on the default full framebuffer and may also opt into coherent-strip or dirty-region backends.

Choose the firmware backend separately. Prefer `CONFIG_ZMK_DONGLE_SCREEN_BACKEND_FULL_FRAMEBUFFER` for full-screen animation, abundant overlapping effects, or themes intended for casual reuse. Choose coherent strips when RAM is constrained but a continuous animation still needs visually coherent updates. Choose dirty regions only for sparse, independently redrawable elements when the measured SPI saving justifies the extra implementation and tuning work.

For a region-capable theme, clip every pixel loop to the supplied canvas; derive scene coordinates from `origin_x` and `origin_y`; redraw background beneath moving or hidden elements; report previous bounds union current bounds; and never depend on unreported prior pixel contents. Validate arbitrary regions as well as the full-canvas case in native and WASM tests.

Tune in this order: measure draw calls, rendered pixels and SPI bytes; merge overlapping continuous damage; increase strip height only within RAM and EasyDMA limits; simplify oversized moving bounds; then reduce refresh cadence for slow timelines. Do not claim a low-RAM backend is faster merely because it allocates less memory.
