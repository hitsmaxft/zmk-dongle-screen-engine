#include "dongle_raster_assets.h"
#include "tre_compat.h"
#include <tre/render.h>
#include <zmk/dongle_theme/metal_sample.h>
#include <zmk/dongle_theme/raster.h>
uint32_t dtr_profile_cycles[3];
#if defined(__ZEPHYR__) && defined(CONFIG_LOG)
#include <zephyr/kernel.h>
#define PROFILE_BEGIN uint32_t profile_start = k_cycle_get_32()
#define PROFILE_END(i)                                                         \
  (dtr_profile_cycles[i] += k_cycle_get_32() - profile_start)
#else
#define PROFILE_BEGIN
#define PROFILE_END(i)
#endif
static int W, H, OX, OY, TW, TH, STRIDE, region_override;
static uint16_t *fb;
static uint32_t damage[18];
static const uint32_t *canvas_damage;
static int canvas_damage_rows;
static int damage_enabled, hide_text;
static int density_mask = 255;
static int density_pattern, density_cx, density_cy, density_inner,
    density_outer;
struct dtr_clip_rect dtr_clip;
void dtr_begin(uint16_t *pixels, int w, int h) {
  if (region_override) {
    hide_text = 0;
    return;
  }
  fb = pixels;
  W = w;
  H = h;
  OX = OY = 0;
  TW = w;
  TH = h;
  STRIDE = w;
  canvas_damage = NULL;
  canvas_damage_rows = 0;
  damage_enabled = hide_text = 0;
  dtr_clip = (struct dtr_clip_rect){0, 0, w, h};
}
void dtr_set_canvas_damage(const uint32_t *rows, int row_count) {
  canvas_damage = rows;
  canvas_damage_rows = rows && row_count > 0 ? row_count : 0;
}
void dtr_begin_canvas(const struct dte_canvas *canvas) {
  fb = canvas->pixels;
  W = canvas->scene_width;
  H = canvas->scene_height;
  OX = canvas->origin_x;
  OY = canvas->origin_y;
  TW = canvas->width;
  TH = canvas->height;
  STRIDE = canvas->stride_pixels;
  region_override = 1;
  hide_text = 0;
  dtr_clip = (struct dtr_clip_rect){OX, OY, OX + TW, OY + TH};
}
void dtr_end_canvas(void) {
  region_override = 0;
  canvas_damage = NULL;
  canvas_damage_rows = 0;
  fb = NULL;
  TW = TH = STRIDE = 0;
}
int dtr_is_dry_run(void) { return fb == NULL; }
int dtr_compat_begin_image(struct tre_surface *surface,
                           struct tre_render_ctx *ctx) {
  if (!surface || !ctx || !region_override || !fb)
    return 0;
  *surface = (struct tre_surface)TRE_SURFACE_INIT;
  surface->scene_width = (uint16_t)W;
  surface->scene_height = (uint16_t)H;
  surface->origin_x = (int16_t)OX;
  surface->origin_y = (int16_t)OY;
  surface->width = (uint16_t)TW;
  surface->height = (uint16_t)TH;
  surface->stride_bytes = (uint32_t)STRIDE * 2u;
  surface->buffer_size = (uint32_t)(TH - 1) * STRIDE * 2u +
                         (uint32_t)TW * 2u;
  surface->pixels = (uint8_t *)fb;
  *ctx = (struct tre_render_ctx)TRE_RENDER_CTX_INIT;
  return tre_render_begin(ctx, surface) == TRE_OK;
}
void dtr_damage_begin(void) {
  if (region_override)
    return;
  damage_enabled = 1;
  for (int i = 0; i < 18; i++)
    damage[i] = 0;
}
void dtr_damage_all(void) {
  if (region_override)
    return;
  for (int y = 0; y < (H + 15) / 16; y++)
    damage[y] = (1u << ((W + 15) / 16)) - 1;
}
const uint32_t *dtr_dirty_tiles(void) { return damage_enabled ? damage : 0; }
int dtr_damage_any(void) {
  if (region_override)
    return 1;
  for (int y = 0; y < 18; y++)
    if (damage[y])
      return 1;
  return 0;
}
static int dirty_pixel(int x, int y) {
  if (region_override && canvas_damage)
    return y >= 0 && (y >> 4) < canvas_damage_rows &&
           (canvas_damage[y >> 4] & (1u << (x >> 4)));
  return region_override || !damage_enabled ||
         (damage[y >> 4] & (1u << (x >> 4)));
}
static int dirty_rect(int x, int y, int w, int h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W)
    w = W - x;
  if (y + h > H)
    h = H - y;
  if (w <= 0 || h <= 0)
    return 0;
  if (region_override && !canvas_damage)
    return 1;
  if (!region_override && !damage_enabled)
    return 1;
  const uint32_t *rows = region_override ? canvas_damage : damage;
  int row_count = region_override ? canvas_damage_rows : 18;
  uint32_t mask = ((1u << ((x + w - 1) / 16 + 1)) - 1) ^ ((1u << (x / 16)) - 1);
  int first = y / 16, last = (y + h - 1) / 16;
  if (first < 0)
    first = 0;
  if (last >= row_count)
    last = row_count - 1;
  for (int row = first; row <= last; row++)
    if (rows[row] & mask)
      return 1;
  return 0;
}
void dtr_damage_rect(int x, int y, int w, int h) {
  if (region_override)
    return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W)
    w = W - x;
  if (y + h > H)
    h = H - y;
  if (w <= 0 || h <= 0)
    return;
  uint32_t mask = ((1u << ((x + w - 1) / 16 + 1)) - 1) ^ ((1u << (x / 16)) - 1);
  for (int row = y / 16; row <= (y + h - 1) / 16; row++)
    damage[row] |= mask;
}
void dtr_damage_ring(int cx, int cy, int inner, int outer) {
  if (region_override)
    return;
  for (int ty = 0; ty < (H + 15) / 16; ty++)
    for (int tx = 0; tx < (W + 15) / 16; tx++) {
      int x0 = tx * 16 - cx, x1 = x0 + 15, y0 = ty * 16 - cy, y1 = y0 + 15;
      int nx = x0 > 0   ? x0
               : x1 < 0 ? x1
                        : 0,
          ny = y0 > 0   ? y0
               : y1 < 0 ? y1
                        : 0;
      int fx = x0 * x0 > x1 * x1 ? x0 : x1, fy = y0 * y0 > y1 * y1 ? y0 : y1;
      if (nx * nx + ny * ny <= outer * outer &&
          fx * fx + fy * fy >= inner * inner)
        damage[ty] |= 1u << tx;
    }
}
void dtr_damage_arc(int cx,int cy,int inner,int outer,int first,int last){
  if(region_override||outer<=inner)return;
  while(last<=first)last+=360;
  if(last-first>=360){dtr_damage_ring(cx,cy,inner,outer);return;}
  for(int begin=first;begin<last;){
    int end=begin+15;if(end>last)end=last;
    int left=cx+outer+2,right=cx-outer-2,top=cy+outer+2,bottom=cy-outer-2;
    for(int sample=0;sample<3;sample++){
      int angle=sample==0?begin:sample==1?(begin+end)/2:end;
      for(int edge=0;edge<2;edge++){
        int radius=edge?outer:inner;
        int x=cx+radius*dtr_trig(angle+90)/32767;
        int y=cy+radius*dtr_trig(angle)/32767;
        if(x<left)left=x;if(x>right)right=x;
        if(y<top)top=y;if(y>bottom)bottom=y;
      }
    }
    dtr_damage_rect(left-3,top-3,right-left+7,bottom-top+7);
    begin=end;
  }
}
void dtr_clear(int r, int g, int b) {
  if (!fb)
    return;
  uint16_t color = dtr_rgb(r, g, b);
  int left = dtr_clip.left > OX ? dtr_clip.left : OX,
      top = dtr_clip.top > OY ? dtr_clip.top : OY;
  int right = dtr_clip.right < OX + TW ? dtr_clip.right : OX + TW;
  int bottom = dtr_clip.bottom < OY + TH ? dtr_clip.bottom : OY + TH;
  if (region_override && canvas_damage) {
    for (int y = top; y < bottom; y++) {
      uint32_t bits = (y >> 4) < canvas_damage_rows ? canvas_damage[y >> 4] : 0;
      for (int x = left; x < right;) {
        int edge = ((x >> 4) + 1) << 4;
        if (edge > right)
          edge = right;
        if (bits & (1u << (x >> 4)))
          for (; x < edge; x++)
            fb[(y - OY) * STRIDE + x - OX] = color;
        else
          x = edge;
      }
    }
    return;
  }
  for (int y = top; y < bottom; y++)
    for (int x = left; x < right; x++)
      if (dirty_pixel(x, y))
        fb[(y - OY) * STRIDE + x - OX] = color;
}
void dtr_clear_disc_background(int cx,int cy,int radius,
                               int outer_r,int outer_g,int outer_b,
                               int inner_r,int inner_g,int inner_b){
  if(!fb||radius<0)return;
  uint16_t outer=dtr_rgb(outer_r,outer_g,outer_b);
  uint16_t inner=dtr_rgb(inner_r,inner_g,inner_b);
  int left=dtr_clip.left>OX?dtr_clip.left:OX;
  int top=dtr_clip.top>OY?dtr_clip.top:OY;
  int right=dtr_clip.right<OX+TW?dtr_clip.right:OX+TW;
  int bottom=dtr_clip.bottom<OY+TH?dtr_clip.bottom:OY+TH;
  int radius2=radius*radius;
  for(int y=top;y<bottom;y++){
    int dy=y-cy,dx=left-cx,d2=dx*dx+dy*dy;
    for(int x=left;x<right;){
      int edge=((x>>4)+1)<<4;if(edge>right)edge=right;
      if(!dirty_pixel(x,y)){
        int jump=edge-x;d2+=jump*(2*dx+jump);dx+=jump;x=edge;continue;
      }
      fb[(y-OY)*STRIDE+x-OX]=d2<radius2?inner:outer;
      d2+=2*dx+1;dx++;x++;
    }
  }
}
void dtr_hide_text(int hidden) { hide_text = hidden; }
#if defined(CONFIG_ZMK_DONGLE_SCREEN_OPTIMIZE_SPEED)
static int distance_q8_xy(int x,int y){
  unsigned ax=x<0?-x:x,ay=y<0?-y:y;
  if(ax<128&&ay<128)return dtr_distance_q8[ay*128+ax];
  return (int)(dtr_root((float)(x*x+y*y))*256+.5f);
}
#else
static float distance_xy(int x, int y) {
#if !defined(__ZEPHYR__) || defined(CONFIG_ZMK_DONGLE_SCREEN_DISTANCE_LUT)
  unsigned ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
  if (ax < 128 && ay < 128) {
    union {
      uint32_t bits;
      float value;
    } v = {.bits = dtr_distance_bits[ay * 128 + ax]};
    return v.value;
  }
#endif
  return dtr_root((float)(x * x + y * y));
}
#endif
float dtr_limit(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
uint16_t dtr_rgb(int r, int g, int b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
/* Ordered RGB565 quantization: a fixed matrix avoids shimmer between frames. */
static uint16_t dither565(int x, int y, int r, int g, int b) {
  static const uint8_t bayer[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                    3, 11, 1, 9,  15, 7, 13, 5};
  int t = bayer[(y & 3) * 4 + (x & 3)];
  if ((unsigned)r <= 255 && (unsigned)g <= 255 && (unsigned)b <= 255) {
    if (r == g && g == b)
      return dtr_grey[r * 16 + t];
    return (dtr_q5[r * 16 + t] << 11) | (dtr_q6[g * 16 + t] << 5) |
           dtr_q5[b * 16 + t];
  }
  int rr = (r * 31 * 16 / 255 + t) / 16, gg = (g * 63 * 16 / 255 + t) / 16,
      bb = (b * 31 * 16 / 255 + t) / 16;
  if (r == g && g == b) {
    gg = (rr * 63 + 15) / 31;
    bb = rr;
  }
  if (rr > 31)
    rr = 31;
  if (gg > 63)
    gg = 63;
  if (bb > 31)
    bb = 31;
  return (rr << 11) | (gg << 5) | bb;
}
void dtr_pixel(int x, int y, int r, int g, int b, int alpha) {
  if (x < dtr_clip.left || x >= dtr_clip.right || y < dtr_clip.top ||
      y >= dtr_clip.bottom || x < OX || x >= OX + TW || y < OY ||
      y >= OY + TH || alpha <= 0 || !fb)
    return;
  if (!dirty_pixel(x, y))
    return;
  if (density_mask < 255) {
    int rank = dtr_density_rank[(y & 15) * 16 + (x & 15)];
    if (density_pattern) {
      int position;
      if (density_pattern == 1) {
        int dx = x - density_cx, dy = y - density_cy;
        position =
            (dx * dx + dy * dy - density_inner * density_inner) * 255 /
            (density_outer * density_outer - density_inner * density_inner);
      } else
        position = (x - density_cx + density_outer) * 255 / (2 * density_outer);
      if (position < 0)
        position = 0;
      if (position > 255)
        position = 255;
      rank = (rank * 90 + position * 166) / 256;
    }
    int cover = density_mask * 256 - rank * 255;
    if (cover <= 0)
      return;
    if (cover < 255)
      alpha = alpha * cover / 255;
    if (alpha <= 0)
      return;
  }
  uint16_t *p = &fb[(y - OY) * STRIDE + x - OX];
  if (alpha < 255) {
    uint16_t old = *p;
    r = (r * alpha + dtr_expand5[old >> 11] * (255 - alpha)) / 255;
    g = (g * alpha + dtr_expand6[(old >> 5) & 63] * (255 - alpha)) / 255;
    b = (b * alpha + dtr_expand5[old & 31] * (255 - alpha)) / 255;
  }
  *p = dither565(x, y, r, g, b);
}
void dtr_pixel565(int x, int y, uint16_t color) {
  if (!fb || x < dtr_clip.left || x >= dtr_clip.right || y < dtr_clip.top ||
      y >= dtr_clip.bottom || x < OX || x >= OX + TW || y < OY ||
      y >= OY + TH || !dirty_pixel(x, y))
    return;
  fb[(y - OY) * STRIDE + x - OX] = color;
}
void dtr_rect(int x, int y, int w, int h, int r, int g, int b, int a) {
  if (!dirty_rect(x, y, w, h) || a <= 0)
    return;
  int left=x>dtr_clip.left?x:dtr_clip.left;
  int top=y>dtr_clip.top?y:dtr_clip.top;
  int right=x+w<dtr_clip.right?x+w:dtr_clip.right;
  int bottom=y+h<dtr_clip.bottom?y+h:dtr_clip.bottom;
  if(left<OX)left=OX;if(top<OY)top=OY;
  if(right>OX+TW)right=OX+TW;if(bottom>OY+TH)bottom=OY+TH;
  for (int j = top; j < bottom; j++)
    for (int i = left; i < right; i++)
      dtr_pixel(i, j, r, g, b, a);
}
void dtr_disc(int cx,int cy,int radius,int r,int g,int b){
  if(!fb||radius<=0)return;
  uint16_t color=dtr_rgb(r,g,b);
  int top=cy-radius+1>dtr_clip.top?cy-radius+1:dtr_clip.top;
  int bottom=cy+radius<dtr_clip.bottom?cy+radius:dtr_clip.bottom;
  if(top<OY)top=OY;if(bottom>OY+TH)bottom=OY+TH;
  if(top<0)top=0;if(bottom>H)bottom=H;
  for(int y=top;y<bottom;y++){
    int dy=y-cy;
    int edge=(int)dtr_root(radius*radius-dy*dy-1);
    int left=cx-edge,right=cx+edge;
    if(left<dtr_clip.left)left=dtr_clip.left;
    if(right>=dtr_clip.right)right=dtr_clip.right-1;
    if(left<OX)left=OX;if(right>=OX+TW)right=OX+TW-1;
    if(left<0)left=0;if(right>=W)right=W-1;
    for(int x=left;x<=right;x++)if(dirty_pixel(x,y))dtr_pixel565(x,y,color);
  }
}
float dtr_root(float n) { return __builtin_sqrtf(n); }
void dtr_line(int x, int y, int xx, int yy, int weight, int r, int g, int b,
              int a) {
  if (!fb)
    return;
  int minx = (x < xx ? x : xx) - weight, maxx = (x > xx ? x : xx) + weight;
  int miny = (y < yy ? y : yy) - weight, maxy = (y > yy ? y : yy) + weight;
  float vx = xx - x, vy = yy - y, len = vx * vx + vy * vy;
  float maxdist = weight * .5f + .65f;
  if (minx < dtr_clip.left)
    minx = dtr_clip.left;
  if (maxx >= dtr_clip.right)
    maxx = dtr_clip.right - 1;
  if (miny < dtr_clip.top)
    miny = dtr_clip.top;
  if (maxy >= dtr_clip.bottom)
    maxy = dtr_clip.bottom - 1;
  if (minx < OX)
    minx = OX;
  if (maxx >= OX + TW)
    maxx = OX + TW - 1;
  if (miny < OY)
    miny = OY;
  if (maxy >= OY + TH)
    maxy = OY + TH - 1;
  for (int j = miny; j <= maxy; j++)
    for (int i = minx; i <= maxx; i++) {
      if (!dirty_pixel(i, j)) {
        i = ((i / 16) + 1) * 16 - 1;
        continue;
      }
      float t = len ? dtr_limit(((i - x) * vx + (j - y) * vy) / len) : 0;
      float dx = i - x - t * vx, dy = j - y - t * vy;
      if (dx * dx + dy * dy > maxdist * maxdist)
        continue;
      float dist = dtr_root(dx * dx + dy * dy);
      int aa = (int)(dtr_limit(weight * .5f + .65f - dist) * a);
      dtr_pixel(i, j, r, g, b, aa);
    }
}
int dtr_trig(int angle) {
  angle %= 360;
  if (angle < 0)
    angle += 360;
  return dtr_sin[angle];
}
void dtr_radial(int cx, int cy, int r1, int r2, int angle, int weight, int r,
                int g, int b, int alpha) {
  int sx = dtr_trig(angle + 90), sy = dtr_trig(angle);
  dtr_line(cx + r1 * sx / 32767, cy + r1 * sy / 32767, cx + r2 * sx / 32767,
           cy + r2 * sy / 32767, weight, r, g, b, alpha);
}
void dtr_spindle(int cx, int cy, int inner, int outer, int angle,
                 int half_width, int r, int g, int b, int alpha) {
  if (!fb || outer <= inner || half_width <= 0 || alpha <= 0)
    return;
  float ux = dtr_trig(angle + 90) / 32767.f, uy = dtr_trig(angle) / 32767.f;
  int x1 = cx + (int)(inner * ux), y1 = cy + (int)(inner * uy);
  int x2 = cx + (int)(outer * ux), y2 = cy + (int)(outer * uy);
  int xa = (x1 < x2 ? x1 : x2) - half_width - 1,
      xb = (x1 > x2 ? x1 : x2) + half_width + 1;
  int ya = (y1 < y2 ? y1 : y2) - half_width - 1,
      yb = (y1 > y2 ? y1 : y2) + half_width + 1;
  if (xa < dtr_clip.left)
    xa = dtr_clip.left;
  if (xb >= dtr_clip.right)
    xb = dtr_clip.right - 1;
  if (ya < dtr_clip.top)
    ya = dtr_clip.top;
  if (yb >= dtr_clip.bottom)
    yb = dtr_clip.bottom - 1;
  if (xa < OX)
    xa = OX;
  if (xb >= OX + TW)
    xb = OX + TW - 1;
  if (ya < OY)
    ya = OY;
  if (yb >= OY + TH)
    yb = OY + TH - 1;
  if (!dirty_rect(xa, ya, xb - xa + 1, yb - ya + 1))
    return;
  float length = outer - inner;
  /* The rotated spindle occupies a narrow strip, not its enclosing rectangle.
   * Bound each scanline by the maximum possible perpendicular coverage. Keep
   * the original coverage arithmetic below, including endpoint antialiasing. */
  float abs_uy = uy < 0 ? -uy : uy;
  float slope = abs_uy > .01f ? ux / uy : 0;
  float reach = abs_uy > .01f ? (half_width + .85f) / abs_uy : 0;
  for (int y = ya; y <= yb; y++) {
    int left = xa, right = xb;
    if (abs_uy > .01f) {
      float center = cx + (y - cy) * slope;
      int a = (int)(center - reach) - 2;
      int bnd = (int)(center + reach) + 2;
      if (left < a) left = a;
      if (right > bnd) right = bnd;
    }
    for (int x = left; x <= right; x++) {
      if (!dirty_pixel(x, y)) {
        x = ((x / 16) + 1) * 16 - 1;
        continue;
      }
      float dx = x - cx, dy = y - cy, along = dx * ux + dy * uy;
      if (along < inner - .65f || along > outer + .65f)
        continue;
      float u = dtr_limit((along - inner) / length);
      /* 0.2px endpoint leaves one AA pixel; 4u(1-u) makes a true spindle. */
      float width = .20f + half_width * (4 * u * (1 - u));
      float across = dx * (-uy) + dy * ux;
      if (across < 0)
        across = -across;
      int coverage = (int)(dtr_limit(width + .65f - across) * alpha);
      if (coverage)
        dtr_pixel(x, y, r, g, b, coverage);
    }
  }
}
/* Analytic annular sector: smooth circular edges without dtr_radial-spoke
 * seams. */
void dtr_arc(int cx, int cy, int inner, int outer, int first, int last, int r,
             int g, int b, int alpha) {
  dtr_arc_f(cx, cy, inner, outer, first, last, r, g, b, alpha);
}
void dtr_arc_density(int cx, int cy, float inner, float outer, int first,
                     int last, int r, int g, int b, int alpha, int density) {
  dtr_arc_reveal(cx, cy, inner, outer, first, last, r, g, b, alpha, density, 0);
}
void dtr_arc_reveal(int cx, int cy, float inner, float outer, int first,
                    int last, int r, int g, int b, int alpha, int density,
                    int pattern) {
  if (outer <= inner || inner < 0)
    return;
  int saved = density_mask;
  int prior_pattern = density_pattern;
  density_pattern = pattern >= 1 && pattern <= 2 ? pattern : 0;
  density_cx = cx;
  density_cy = cy;
  density_inner = (int)inner;
  density_outer = (int)outer;
  if (density_outer <= density_inner)
    density_pattern = 0;
  density_mask = density < 0 ? 0 : density > 255 ? 255 : density;
  dtr_arc_f(cx, cy, inner, outer, first, last, r, g, b, alpha);
  density_mask = saved;
  density_pattern = prior_pattern;
}
void dtr_arc_f(int cx, int cy, float inner, float outer, int first, int last,
               int r, int g, int b, int alpha) {
  if (!fb || alpha <= 0)
    return;
  PROFILE_BEGIN;
#if defined(CONFIG_ZMK_DONGLE_SCREEN_OPTIMIZE_SPEED)
  int sx=dtr_trig(first+90),sy=dtr_trig(first);
  int ex=dtr_trig(last+90),ey=dtr_trig(last);
  int inner_q8=(int)(inner*256+.5f),outer_q8=(int)(outer*256+.5f);
#else
  float sx = dtr_trig(first + 90) / 32767.f, sy = dtr_trig(first) / 32767.f,
        ex = dtr_trig(last + 90) / 32767.f, ey = dtr_trig(last) / 32767.f;
#endif
  int xa = cx - outer - 1, xb = cx + outer + 1, ya = cy - outer - 1,
      yb = cy + outer + 1;
  /* Bound the SECTOR, not its entire enclosing disc. Especially important for
   * the six thin battery bands: identical coverage, far fewer empty pixels. */
  if (last > first && last - first < 360) {
    float minx = outer, maxx = -outer, miny = outer, maxy = -outer;
    for (int i = 0; i < 6; i++) {
      int angle = i == 0 ? first : i == 1 ? last : (i - 2) * 90;
      int delta = (angle - first) % 360;
      if (delta < 0)
        delta += 360;
      if (i >= 2 && delta > last - first)
        continue;
      for (int j = 0; j < 2; j++) {
        float radius = j ? outer : inner;
        float xx = radius * (dtr_trig(angle + 90) / 32767.f);
        float yy = radius * (dtr_trig(angle) / 32767.f);
        if (xx < minx)
          minx = xx;
        if (xx > maxx)
          maxx = xx;
        if (yy < miny)
          miny = yy;
        if (yy > maxy)
          maxy = yy;
      }
    }
    xa = cx + (int)minx - 2;
    xb = cx + (int)maxx + 2;
    ya = cy + (int)miny - 2;
    yb = cy + (int)maxy + 2;
  }
  if (xa < 0)
    xa = 0;
  if (xb >= W)
    xb = W - 1;
  if (ya < 0)
    ya = 0;
  if (yb >= H)
    yb = H - 1;
  if (xa < dtr_clip.left)
    xa = dtr_clip.left;
  if (xb >= dtr_clip.right)
    xb = dtr_clip.right - 1;
  if (ya < dtr_clip.top)
    ya = dtr_clip.top;
  if (yb >= dtr_clip.bottom)
    yb = dtr_clip.bottom - 1;
  if (xa < OX)
    xa = OX;
  if (xb >= OX + TW)
    xb = OX + TW - 1;
  if (ya < OY)
    ya = OY;
  if (yb >= OY + TH)
    yb = OY + TH - 1;
  if (!dirty_rect(xa, ya, xb - xa + 1, yb - ya + 1)) {
    PROFILE_END(1);
    return;
  }
  /* Most annulus interior pixels cover the uniform dial background. Blend that
   * color once per arc, not three multiplies/divides for every covered pixel.
   * Edges, overlaps and density effects still take the exact general path. */
  uint16_t flat[16], base = dtr_rgb(8, 10, 12);
  if (density_mask == 255 && alpha <= 255) {
    int rr = (r * alpha + dtr_expand5[base >> 11] * (255 - alpha)) / 255;
    int gg = (g * alpha + dtr_expand6[(base >> 5) & 63] * (255 - alpha)) / 255;
    int bb = (b * alpha + dtr_expand5[base & 31] * (255 - alpha)) / 255;
    for (int i = 0; i < 16; i++)
      flat[i] = dither565(i & 3, i >> 2, rr, gg, bb);
  }
  for (int y = ya; y <= yb; y++) {
    /* Skip the hollow centre a whole scanline at a time. No coverage values
     * change: the original squared-radius test below remains authoritative. */
    int dy_i = y - cy;
    float hole2 = (inner - 1) * (inner - 1) - dy_i * dy_i;
    int hole = hole2 > 0 ? (int)dtr_root(hole2) : 0;
    /* Cull the two corners outside the circle before visiting pixels. The
     * extra pixel guards rounding; the old radial predicate stays decisive. */
    float edge2 = (outer + 1) * (outer + 1) - dy_i * dy_i;
    if (edge2 < 0) continue;
    int edge = (int)dtr_root(edge2) + 1;
    int left = xa > cx - edge ? xa : cx - edge;
    int right = xb < cx + edge ? xb : cx + edge;
    for (int x = left; x <= right; x++) {
      if (!dirty_pixel(x, y)) {
        x = ((x / 16) + 1) * 16 - 1;
        continue;
      }
      if (hole > 0 && x > cx - hole && x < cx + hole) {
        x = cx + hole - 1;
        continue;
      }
      int dx=x-cx,dy=y-cy;
      float d2=(float)(dx*dx+dy*dy);
      if (d2 < (inner - 1) * (inner - 1) || d2 > (outer + 1) * (outer + 1))
        continue;
#if defined(CONFIG_ZMK_DONGLE_SCREEN_OPTIMIZE_SPEED)
      int64_t cross1=(int64_t)sx*dy-(int64_t)sy*dx;
      int64_t cross2=(int64_t)dx*ey-(int64_t)dy*ex;
      /* Outside the angular AA fringe, coverage quantizes to zero. Reject
       * before distance lookup and the radial/coverage products. */
      if (last-first<=180) {
        if (cross1<=-16384 || cross2<=-16384) continue;
      } else if (cross1<=-16384 && cross2<=-16384) continue;
      int distance=distance_q8_xy(dx,dy);
      int radial_in=distance-inner_q8+128;
      int radial_out=outer_q8+128-distance;
      if(radial_in<=0||radial_out<=0)continue;
      if(radial_in>256)radial_in=256;if(radial_out>256)radial_out=256;
      int coverage=(radial_in*radial_out*32767)>>16;
      int c1=(int)(cross1+16384),c2=(int)(cross2+16384);
      if(c1<0)c1=0;else if(c1>32767)c1=32767;
      if(c2<0)c2=0;else if(c2>32767)c2=32767;
      if(last-first<=180){
        coverage=(coverage*c1+16384)>>15;
        coverage=(coverage*c2+16384)>>15;
      }
      else{
        int o1=(int)(-cross1+16384),o2=(int)(-cross2+16384);
        if(o1<0)o1=0;else if(o1>32767)o1=32767;
        if(o2<0)o2=0;else if(o2>32767)o2=32767;
        int outside=(o1*o2+16384)>>15;
        coverage=(coverage*(32767-outside)+16384)>>15;
      }
      int opacity=(coverage*alpha+16384)>>15;
#else
      float d = distance_xy(x - cx, y - cy),
            aa = dtr_limit(d - inner + .5f) * dtr_limit(outer + .5f - d);
      float cross1 = sx * dy - sy * dx, cross2 = dx * ey - dy * ex;
      if (last - first <= 180)
        aa *= dtr_limit(cross1 + .5f) * dtr_limit(cross2 + .5f);
      else
        aa *= 1 - dtr_limit(-cross1 + .5f) * dtr_limit(-cross2 + .5f);
      int opacity = (int)(aa * alpha);
#endif
      uint16_t *target = &fb[(y - OY) * STRIDE + x - OX];
      if (density_mask == 255 && alpha <= 255 && opacity == alpha &&
          *target == base)
        *target = flat[(y & 3) * 4 + (x & 3)];
      else
        dtr_pixel(x, y, r, g, b, opacity);
    }
  }
  PROFILE_END(1);
}
void dtr_text(const char *s, int x, int y, int size, int r, int g, int b,
              int alpha, int centered) {
  if (!fb || alpha <= 0 || hide_text)
    return;
  PROFILE_BEGIN;
  int fi = 0;
  for (int i = 0; i < DTR_FONT_COUNT; i++)
    if (dtr_fonts[i].size <= size)
      fi = i;
  const struct dtr_font *f = &dtr_fonts[fi];
  float scale = size / (float)f->size;
  int width = 0;
  for (int i = 0; s[i]; i++) {
    int c = (unsigned char)s[i];
    if (c < 32 || c > 126)
      c = '?';
    width += (int)(f->glyph[c - 32].advance * scale + .5f);
  }
  int pen = centered ? x - width / 2 : x, base = y + (int)(size * .36f);
  for (int i = 0; s[i]; i++) {
    int c = (unsigned char)s[i];
    if (c < 32 || c > 126)
      c = '?';
    const struct dtr_glyph *gl = &f->glyph[c - 32];
    int gw = (int)(gl->w * scale + .5f), gh = (int)(gl->h * scale + .5f),
        ox = (int)(gl->ox * scale), oy = (int)(gl->oy * scale);
    if (!dirty_rect(pen + ox, base - gh - oy, gw, gh)) {
      pen += (int)(gl->advance * scale + .5f);
      continue;
    }
    if (size == f->size) {
      for (int yy = 0; yy < gl->h; yy++)
        for (int xx = 0; xx < gl->w; xx++) {
          int idx = yy * gl->w + xx, v = f->bits[gl->offset + idx / 2];
          v = (idx & 1) ? v & 15 : v >> 4;
          if (v)
            dtr_pixel(pen + gl->ox + xx, base - gl->h - gl->oy + yy, r, g, b,
                      v * alpha / 15);
        }
      pen += gl->advance;
      continue;
    }
    for (int yy = 0; yy < gh; yy++)
      for (int xx = 0; xx < gw; xx++) {
        int px = (int)(xx / scale), py = (int)(yy / scale),
            idx = py * gl->w + px;
        int v = f->bits[gl->offset + idx / 2];
        v = (idx & 1) ? v & 15 : v >> 4;
        dtr_pixel(pen + ox + xx, base - gh - oy + yy, r, g, b, v * alpha / 15);
      }
    pen += (int)(gl->advance * scale + .5f);
  }
  PROFILE_END(2);
}

void dtr_metal_ring(int cx, int cy, int R, int thickness,
                    float (*metal)(float)) {
  if (!fb)
    return;
  PROFILE_BEGIN;
  /* The moving assembly retains a dark disc and metal rim, with no overshoot.
   */
  int top=cy-R-1>dtr_clip.top?cy-R-1:dtr_clip.top;
  int bottom=cy+R+2<dtr_clip.bottom?cy+R+2:dtr_clip.bottom;
  int left=cx-R-1>dtr_clip.left?cx-R-1:dtr_clip.left;
  int right=cx+R+2<dtr_clip.right?cx+R+2:dtr_clip.right;
  if(top<OY)top=OY;if(bottom>OY+TH)bottom=OY+TH;
  if(left<OX)left=OX;if(right>OX+TW)right=OX+TW;
  for (int y = top; y < bottom; y++)
    for (int x = left; x < right; x++) {
      int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
      if (d2 > (R + 1) * (R + 1))
        continue;
      if (d2 < (R - thickness - 1) * (R - thickness - 1)) {
        if (x >= 0 && x < W && y >= 0 && y < H)
          dtr_pixel565(x, y, dtr_rgb(8, 10, 12));
        continue;
      }
      uint8_t c, a;
      if (dtr_metal_sample(dx, dy, R, metal, &c, &a))
        dtr_pixel(x, y, c, c, c, a);
    }
  PROFILE_END(0);
}

static void fill_flat_disc(int cx,int cy,int radius,uint16_t color){
  int top=cy-radius+1>dtr_clip.top?cy-radius+1:dtr_clip.top;
  int bottom=cy+radius<dtr_clip.bottom?cy+radius:dtr_clip.bottom;
  if(top<OY)top=OY;if(bottom>OY+TH)bottom=OY+TH;
  if(top<0)top=0;if(bottom>H)bottom=H;
  for(int y=top;y<bottom;y++){
    int dy=y-cy,edge=(int)dtr_root(radius*radius-dy*dy-1);
    int left=cx-edge,right=cx+edge;
    if(left<dtr_clip.left)left=dtr_clip.left;
    if(right>=dtr_clip.right)right=dtr_clip.right-1;
    if(left<OX)left=OX;if(right>=OX+TW)right=OX+TW-1;
    if(left<0)left=0;if(right>=W)right=W-1;
    for(int x=left;x<=right;){
      if(!dirty_pixel(x,y)){x=((x>>4)+1)<<4;continue;}
      fb[(y-OY)*STRIDE+x-OX]=color;x++;
    }
  }
}
static void metal_ring_cached(int cx, int cy, int R, int thickness,
                              const struct dtr_metal_texel *atlas, size_t count,
                              int clear_disc) {
  if (!fb)
    return;
  PROFILE_BEGIN;
  if(clear_disc)fill_flat_disc(cx,cy,R-thickness-1,dtr_rgb(8,10,12));
  /* Build-time coverage/grey; quantization remains at destination coordinates
   * so moving the ring preserves the fixed Bayer matrix without shimmer. */
  int first_dy=dtr_clip.top-cy,last_dy=dtr_clip.bottom-1-cy;
  if(first_dy<OY-cy)first_dy=OY-cy;
  if(last_dy>OY+TH-1-cy)last_dy=OY+TH-1-cy;
  size_t lo=0,hi=count;
  while(lo<hi){size_t mid=lo+(hi-lo)/2;if(atlas[mid].y<first_dy)lo=mid+1;else hi=mid;}
  for (size_t i = lo; i < count && atlas[i].y<=last_dy; i++) {
    const struct dtr_metal_texel *t = &atlas[i];
    int x = cx + t->x, y = cy + t->y;
    if (x < dtr_clip.left || x >= dtr_clip.right || y < dtr_clip.top ||
        y >= dtr_clip.bottom || !dirty_pixel(x, y))
      continue;
    if (t->alpha == 255 && density_mask == 255) {
      static const uint8_t bayer[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                        3, 11, 1, 9,  15, 7, 13, 5};
      dtr_pixel565(x, y, dtr_grey[t->grey * 16 + bayer[(y & 3) * 4 + (x & 3)]]);
    } else
      dtr_pixel(x, y, t->grey, t->grey, t->grey, t->alpha);
  }
  PROFILE_END(0);
}
void dtr_metal_ring_cached(int cx, int cy, int R, int thickness,
                           const struct dtr_metal_texel *atlas, size_t count) {
  metal_ring_cached(cx,cy,R,thickness,atlas,count,1);
}
void dtr_metal_rim_cached(int cx, int cy, int R, int thickness,
                          const struct dtr_metal_texel *atlas, size_t count) {
  metal_ring_cached(cx,cy,R,thickness,atlas,count,0);
}

static int sector_contains(int x,int y,int first,int last){
  int sx=dtr_trig(first+90),sy=dtr_trig(first);
  int ex=dtr_trig(last+90),ey=dtr_trig(last);
  int64_t cross1=(int64_t)sx*y-(int64_t)sy*x;
  int64_t cross2=(int64_t)x*ey-(int64_t)y*ex;
  int span=last-first;while(span<=0)span+=360;
  return span<=180?cross1>=0&&cross2>0:!(cross1<0&&cross2<=0);
}
void dtr_metal_ring_cached_sector(int cx,int cy,int R,int thickness,
                                  const struct dtr_metal_texel *atlas,
                                  size_t count,int first,int last){
  (void)R;(void)thickness;
  if(!fb)return;
  PROFILE_BEGIN;
  int first_y=dtr_clip.top;if(first_y<OY)first_y=OY;
  int last_y=dtr_clip.bottom-1;if(last_y>OY+TH-1)last_y=OY+TH-1;
  size_t lo=0,hi=count;
  while(lo<hi){size_t mid=lo+(hi-lo)/2;
    if(cy+atlas[mid].y<first_y)lo=mid+1;else hi=mid;}
  for(size_t i=lo;i<count&&cy+atlas[i].y<=last_y;i++){
    const struct dtr_metal_texel *t=&atlas[i];
    int x=cx+t->x,y=cy+t->y;
    if(x<dtr_clip.left||x>=dtr_clip.right||y<dtr_clip.top||
       y>=dtr_clip.bottom||x<OX||x>=OX+TW||y<OY||y>=OY+TH||
       !dirty_pixel(x,y))continue;
    if(!sector_contains(t->x,t->y,first,last))continue;
    if(t->alpha==255&&density_mask==255){
      static const uint8_t bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
      dtr_pixel565(x,y,dtr_grey[t->grey*16+bayer[(y&3)*4+(x&3)]]);
    }else dtr_pixel(x,y,t->grey,t->grey,t->grey,t->alpha);
  }
  PROFILE_END(0);
}
static int dtr_scale_offset(int value,int source_radius,int target_radius){
  int scaled=value*target_radius;
  return scaled>=0?(scaled+source_radius/2)/source_radius:
                   (scaled-source_radius/2)/source_radius;
}

static void metal_ring_scaled(int cx, int cy, int source_radius,
                              int target_radius, int thickness,
                              const struct dtr_metal_texel *atlas, size_t count,
                              int clear_disc) {
  if (!fb)
    return;
  PROFILE_BEGIN;
  if(clear_disc)
    fill_flat_disc(cx,cy,target_radius-thickness-1,dtr_rgb(8,10,12));
  int first_y=dtr_clip.top;if(first_y<OY)first_y=OY;
  int last_y=dtr_clip.bottom-1;if(last_y>OY+TH-1)last_y=OY+TH-1;
  size_t lo=0,hi=count;
  while(lo<hi){size_t mid=lo+(hi-lo)/2;
    if(cy+dtr_scale_offset(atlas[mid].y,source_radius,target_radius)<first_y)lo=mid+1;else hi=mid;}
  for (size_t i = lo; i < count; i++) {
    const struct dtr_metal_texel *t = &atlas[i];
    int x=cx+dtr_scale_offset(t->x,source_radius,target_radius);
    int y=cy+dtr_scale_offset(t->y,source_radius,target_radius);
    if(y>last_y)break;
    if (x < dtr_clip.left || x >= dtr_clip.right || y < dtr_clip.top ||
        y >= dtr_clip.bottom || !dirty_pixel(x, y))
      continue;
    dtr_pixel(x, y, t->grey, t->grey, t->grey, t->alpha);
  }
  PROFILE_END(0);
}
void dtr_metal_ring_scaled(int cx, int cy, int source_radius, int target_radius,
                           int thickness, const struct dtr_metal_texel *atlas,
                           size_t count) {
  metal_ring_scaled(cx,cy,source_radius,target_radius,thickness,atlas,count,1);
}
void dtr_metal_rim_scaled(int cx, int cy, int source_radius, int target_radius,
                          int thickness, const struct dtr_metal_texel *atlas,
                          size_t count) {
  metal_ring_scaled(cx,cy,source_radius,target_radius,thickness,atlas,count,0);
}
void dtr_metal_ring_scaled_sector(int cx,int cy,int source_radius,
                                  int target_radius,int thickness,
                                  const struct dtr_metal_texel *atlas,
                                  size_t count,int first,int last){
  (void)thickness;
  if(!fb)return;
  PROFILE_BEGIN;
  for(size_t i=0;i<count;i++){
    const struct dtr_metal_texel *t=&atlas[i];
    int ox=dtr_scale_offset(t->x,source_radius,target_radius);
    int oy=dtr_scale_offset(t->y,source_radius,target_radius);
    int x=cx+ox,y=cy+oy;
    if(x<dtr_clip.left||x>=dtr_clip.right||y<dtr_clip.top||
       y>=dtr_clip.bottom||x<OX||x>=OX+TW||y<OY||y>=OY+TH||
       !dirty_pixel(x,y))continue;
    if(!sector_contains(ox,oy,first,last))continue;
    dtr_pixel(x,y,t->grey,t->grey,t->grey,t->alpha);
  }
  PROFILE_END(0);
}
