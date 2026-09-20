# Repository rules

## Agent navigation and skills

- Use `README.md` for repository scope and human-facing orientation. Use this file for repository constraints, and use task-specific `skills/*/SKILL.md` files for the corresponding implementation guidance.
- Skill links are repository-relative. When a task matches a listed trigger, read the linked `SKILL.md` completely before changing source, generated assets or documentation in that area.
- For typography, font generation, text layout or glyph icons, read [the UI design skill](skills/ui-design/SKILL.md). Its font-selection and validation guidance is authoritative; the README contains only a short public summary.
- For API work, read `docs/api.md`; for build and preview work, read `docs/development.md`. Keep their claims synchronized with implemented behavior.

## WASM preview localization

- All user-facing text in the WASM preview shell under `web/` MUST be localized.
- English, Simplified Chinese and Japanese MUST remain available from an in-page language selector. Switching language MUST NOT reload WASM, reset simulation state or restart an animation.
- Static text, dynamic status text, errors, document titles and accessibility labels MUST use the shared dictionary in `web/i18n.js`; do not add user-facing string literals to event or render code.
- Every locale MUST expose the same keys. Missing translations MUST fall back to English and MUST be caught by automated checks.
- Layout changes MUST tolerate CJK text and at least 30 percent text expansion without clipping controls.

## Theme and engine boundary

- Localization inside the simulated device canvas belongs entirely to each theme. The engine MUST NOT translate theme content or prescribe a theme locale model.
- The engine MAY bundle small language-neutral bitmap fonts and raster primitives. Font availability does not imply engine-owned localization.
- Preview-shell language changes MUST NOT mutate the theme framebuffer unless the theme independently defines such an API.
- Keep `docs/api.md` synchronized with exported headers and current implementation limits. Do not document planned registry, fallback or display-format support as implemented.

## UI typography invariants

- Keep theme-specific font payloads and subsets in the theme module. Do not add optional fonts to the default Engine build merely because Engine provides conversion or rendering helpers.
- Validate fonts at native resolution in RGB565; browser-scaled previews are inspection aids, not physical-panel legibility evidence.

## RAM, Flash and SPI budget gate

- Every renderer, transport, asset-format or maintained-theme change MUST report comparable before/after budgets. Do not infer whole-firmware deltas from source size or individual object files.
- Firmware RAM and Flash evidence MUST come from the same board, shield, Kconfig, compiler and linker configuration. Read `_image_ram_size` and `_flash_used` from the final Zephyr map through `scripts/report_budget.py`; distinguish these whole-image values from TRE-only symbol attribution.
- SPI estimates MUST state width, height, RGB565 bytes per pixel, target FPS, SPI clock and dirty-area assumption. Report bytes/frame, bytes/second, transfer milliseconds and frame-budget utilization. Dirty rectangles are conservative transport bounds; DMA overlaps work but does not reduce payload bytes.
- A preview without a firmware map MAY report strip/full-frame RAM models, WASM size and SPI estimates, but MUST label firmware RAM/Flash as unavailable. Never use native or WASM binary size as firmware Flash evidence.
- Keep a checked-in or downloaded prior `budget-report.json` when judging a regression. CI limits are ceilings, not proof of performance; native/WASM replay, Zephyr linkage and physical SPI/display timing remain separate gates.
- The default comparison model is 280×240 RGB565, 24 FPS, 32 MHz SPI, 20% dirty area and a 4,480-pixel strip. Override each input for the actual target rather than silently reusing defaults.
