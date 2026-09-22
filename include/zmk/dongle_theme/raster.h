/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <zmk/dongle_theme/theme.h>
/* Serialized single-surface raster services shared by all compile-time theme
 * variants. */
struct dtr_clip_rect {
  int left, top, right, bottom;
};
extern struct dtr_clip_rect dtr_clip;
void dtr_begin(uint16_t *pixels, int w, int h);
void dtr_begin_canvas(const struct dte_canvas *canvas);
void dtr_end_canvas(void);
int dtr_is_dry_run(void);
/* Optional retained-frame tile damage. NULL dirty tiles means legacy full draw.
 */
void dtr_damage_begin(void);
void dtr_damage_all(void);
void dtr_damage_rect(int x, int y, int width, int height);
void dtr_damage_ring(int cx, int cy, int inner, int outer);
void dtr_damage_arc(int cx, int cy, int inner, int outer, int first, int last);
int dtr_damage_any(void);
const uint32_t *dtr_dirty_tiles(void);
void dtr_clear(int r, int g, int b);
void dtr_hide_text(int hidden);
float dtr_limit(float x) __attribute__((const));
float dtr_root(float x);
int dtr_trig(int angle) __attribute__((const));
uint16_t dtr_rgb(int r, int g, int b) __attribute__((const));
void dtr_pixel(int x, int y, int r, int g, int b, int alpha);
/* Exact pre-quantized asset pixel; bypasses RGB888 blending/dithering. */
void dtr_pixel565(int x, int y, uint16_t color);
void dtr_rect(int x, int y, int w, int h, int r, int g, int b, int alpha);
void dtr_line(int x, int y, int xx, int yy, int weight, int r, int g, int b,
              int alpha);
void dtr_radial(int cx, int cy, int r1, int r2, int angle, int weight, int r,
                int g, int b, int alpha);
/* Long double-pointed spindle with parabolic shoulders and analytic AA. */
void dtr_spindle(int cx, int cy, int inner, int outer, int angle,
                 int half_width, int r, int g, int b, int alpha);
void dtr_arc(int cx, int cy, int inner, int outer, int first, int last, int r,
             int g, int b, int alpha);
void dtr_arc_f(int cx, int cy, float inner, float outer, int first, int last,
               int r, int g, int b, int alpha);
/* 0..255 stable spatial density, limited to this arc and reset on return. */
void dtr_arc_density(int cx, int cy, float inner, float outer, int first,
                     int last, int r, int g, int b, int alpha, int density);
/* Pattern: 0 uniform, 1 inside-out density, 2 left-to-right density. */
void dtr_arc_reveal(int cx, int cy, float inner, float outer, int first,
                    int last, int r, int g, int b, int alpha, int density,
                    int pattern);
void dtr_text(const char *s, int x, int y, int size, int r, int g, int b,
              int alpha, int centered);
void dtr_metal_ring(int cx, int cy, int radius, int thickness,
                    float (*profile)(float));
struct dtr_metal_texel {
  int8_t x, y;
  uint8_t grey, alpha;
};
extern uint32_t
    dtr_profile_cycles[3]; /* ring / arcs / text, diagnostics only */
void dtr_metal_ring_cached(int cx, int cy, int radius, int thickness,
                           const struct dtr_metal_texel *atlas, size_t count);
void dtr_metal_ring_scaled(int cx, int cy, int source_radius, int target_radius,
                           int thickness, const struct dtr_metal_texel *atlas,
                           size_t count);
