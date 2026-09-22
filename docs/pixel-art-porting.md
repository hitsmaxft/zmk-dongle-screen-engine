# Pixel-art porting

Animation quality, stable pixels and source fidelity come first. Native/WASM
parity proves that implementations agree, not that they match the artwork.
Use independent source comparisons before optimizing memory or transport.

## Required sequence

Complete each stage before starting the next:

1. Freeze the logical canvas and validate the background alone.
2. Add static layers back to front, isolating each against black or transparency.
3. Build foreground atlases with fixed bounds, anchors, palettes and masks.
4. Validate the full static composition at `t=0`, region by region.
5. Animate by selecting verified atlas frame indices.
6. Validate every animation frame against its source contract.
7. Optimize retained rendering, caching, compression and transport.

Fix failures at their source. Do not hide asset defects with renderer patches,
smaller test regions or looser thresholds.

## Source and layout

Keep original files in the theme's `arts/` directory and record SHA-256 hashes.
Distinguish final display samples, glyph specimens and layer diagrams. Prefer
final solid-color or layered assets over composite mockups. Removing effects
from a large specimen does not produce the intended small font.

Record coordinates, crops, cells, baselines, advances and sampling rules.
Resolve overlapping regions before extraction. Isolated previews must not
contain neighboring content. Within each region, validate background, static
frames, sprites, text and interaction overlays in order. Save raw pixels and
a diff after each addition. Fix dynamic snapshots and timestamps initially;
static regions should not request continuous animation.

## Extraction and scaling

Use tight crops and preserve intended color coverage, including dark edges
in JPEG references. Exclude compression noise from the final palette.
Connected-component filtering needs evidence that the source contains only
the expected components; record pixel counts before and after filtering.

Build the reference mask independently of the generator. At source size,
`missed = reference - actual` and `extra = actual - reference` must both be
zero. Do not use one function to produce expected and actual masks. For
resized assets, independently apply the documented sampling rule, then XOR
the target cells. Save source/generated/XOR evidence; XOR must be black.

Use nearest-neighbor sampling. Noninteger scaling requires an explicit
contract and target-pixel validation. Do not use implicit `thumbnail()`, BOX,
bilinear, bicubic or CSS interpolation. All animation frames share a canvas,
bounding box and anchor; never crop, resize or center frames individually.
Extraction, scaling and color clustering belong offline.

Do not discard legitimate pixels through generic brightness thresholds,
automatic white-background removal or largest-component selection. When clean
assets exist, do not extract them from composite effect images.

## Text

Glyphs share a cell, baseline and advance contract. Measure the whole string,
choose one starting pen position and advance through it; never center digits
individually. If a three-digit value collides with an equalizer, move the
equalizer rather than an individual digit.

Treat fill, outline and shadow separately. Remove unwanted effects in asset
generation, not by renderer overpaint. Display and status fonts need separate
atlases or a declared integer scaling rule.

## Atlas acceptance

Produce a machine-readable atlas and annotated preview before animation code.
Declare dimensions, frame count, anchor, palette, transparency, allowed-pixel
mask and source checksum. Use one immutable background and separate masked
overlays for characters, icons, battery fills and equalizers.

Require zero background changes outside the union of animation alpha masks;
no battery/progress fill outside its mask; complete terminals, corners and
edges; no dynamic fill residue in static frames; no off-grid pixels in grid
animations; and no unintended speckles, isolated pixels or undeclared colors.

## RGB565 comparison

Compare artwork and output in the same RGB565 domain. Write prequantized
sprites through `dtr_pixel565()` or equivalent without another Bayer pass.
Use coverage and dithering for vector edges. Packing may round whereas
`dtr_rgb()` truncates; tests must match the actual conversion.

Use keyed blits for transparent sprites and opaque blits for opaque images.
Opaque pixels equal to a transparency key must not disappear.

Use [the comparison tool](development.md#compare-reference-pixels) on raw
framebuffers. Check changed pixels, mean error, generated-only white speckles,
local brightness spikes and black hidden regions. Compare static frames with
UI crops and glyphs with independent specimens. Preserve valid glyph/frame
overlap rather than erasing it during validation.

## Animation acceptance

After static acceptance, animation selects verified indices and updates
declared positions or palette states; it must not generate images in its loop.
Record raw RGB565 pixels, frame index, timestamp and snapshot for every frame.
Check changed-pixel bounds, background stability outside masks and anchor drift
against the intended motion path.

Do not substitute opacity flicker for sprite animation or blur frames through
alpha blending. Record exact video FPS, frame count and duration; GIF is only
a viewing aid. Cover idle, state changes, maximum input speed, longest numbers,
all buttons and loop boundaries. Static pixels must survive loop restart.

## Code and optimization

Keep offline generators in `scripts/`, immutable sprites/masks/palettes in
assets, backgrounds in scene code and batteries/text/equalizers/buttons in
widgets. Animation maps time and state to indices; the theme composes modules
in a stable draw order. Tests use independent references. Share explicit
state and coordinates, not framebuffer sampling or hidden cross-layer globals.

Establish a deterministic full-composition baseline before retained buffers,
dirty rectangles, tile hashes, caches or compression. Compare every optimization
with saved frames: pixels must remain identical unless a change was explicitly
approved. Never preserve noisy screenshots to hide extraction defects or leave
background patches, old sprites or out-of-bounds fills in retained RAM.

## Known failures

| Symptom | Cause | Correction |
| --- | --- | --- |
| Background changes with cat frames | Composite idle scene mixed with unrelated patches | One background and transparent frames |
| Green residue or missing battery terminal | Fill baked into a cropped static frame | Complete frame and separate fill mask |
| Blurred or flashing equalizer | JPEG extraction with alpha fade | Grid-aligned palette frames |
| Missing digit edges or bottom fragments | Wrong composite font specimen | Use the intended glyph source |
| Tests always pass | Generator also creates the expected mask | Independent reference extraction |
| Flash unexpectedly grows | Full screenshots preserve background noise | Correct extraction and reuse verified assets |

Before committing, require zero source-mask omissions/additions, black target
XOR, strict regional differences, native/WASM parity, stable static dithering
and `git diff --check`.
