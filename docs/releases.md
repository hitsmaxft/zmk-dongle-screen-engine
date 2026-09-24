# Releases

## 1.4.1

Engine 1.4.1 retains Theme ABI 1.3 and TRE ABI 1.0. Existing ABI 1.3 themes do
not require a descriptor or structure migration.

This release consolidates the post-1.3 renderer and host work:

- explicit raster-theme deadlines and prompt late-keyframe scheduling;
- incremental retained-frame damage, tile hashing and profiled region modes;
- scanline culling, fused radial bands, bounded glyph blending and removal of
  inner-loop division from line, spindle and scaled-ring paths;
- scoped circular exclusion for complete geometry moving behind opaque dials;
- localized WASM controls, screen-mask and filter tuning, hardware-budget
  simulation and pointer-orientation adaptation isolated from firmware input;
- concise English API/development documentation, Cortex-M4F arithmetic audit,
  downstream pin/build gates and warning-free Engine-owned Zephyr output.

Validation covered the complete native Engine suite, native/WASM RGB565 parity,
the minimal-theme CI action, and a pristine nRF52840 consumer firmware build.
Physical animation cadence and panel behavior remain downstream hardware gates.
