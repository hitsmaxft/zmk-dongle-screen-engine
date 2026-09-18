# Theme examples

## Minimal theme

[`examples/minimal-theme`](../examples/minimal-theme) is the smallest complete
theme in this repository. Start there for the descriptor, render callback, and
native/WASM preview manifest.

## ZDSE Themes

[`hitsmaxft/zdse-themes`](https://github.com/hitsmaxft/zdse-themes) is a compact
visual gallery and code reference:

- **Neon Cat** shows deterministic character and HUD animation timing.
- **Phosphor Radar** is a complete procedural theme with retained redraws,
  gestures, and custom preview controls.

The gallery publishes final preview images and theme behavior only. Original
artwork, fonts, sprite sheets, and generated atlas payloads are not public.

Use the Engine for shared raster, sprite, touch, and pixel-UI utilities. Keep
motion, layout, palette, composition, and private assets in the theme or its
consumer repository.
