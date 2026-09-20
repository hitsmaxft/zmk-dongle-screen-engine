/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>

#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/ui.h>

int main(void) {
  uint16_t pixels[4] = {0};
  struct dte_canvas canvas = DTE_CANVAS_INIT;
  canvas.scene_width = 4;
  canvas.scene_height = 4;
  canvas.origin_x = 1;
  canvas.origin_y = 1;
  canvas.width = 2;
  canvas.height = 2;
  canvas.stride_pixels = 2;
  canvas.buffer_size = sizeof(pixels);
  canvas.pixels = pixels;
  dtr_begin_canvas(&canvas);

  const uint16_t keyed_pixels[4] = {0x1111, 0x0001, 0x2222, 0x3333};
  const struct dte_sprite keyed = {2, 2, keyed_pixels};
  dte_sprite_blit(&keyed, 1, 1, 255);
  assert(pixels[0] == 0x1111 && pixels[1] == 0);
  assert(pixels[2] == 0x2222 && pixels[3] == 0x3333);

  const uint16_t opaque_pixels[4] = {1, 2, 3, 4};
  const struct dte_sprite opaque = {2, 2, opaque_pixels};
  dte_sprite_blit_opaque(&opaque, 1, 1);
  assert(pixels[0] == 1 && pixels[1] == 2);
  assert(pixels[2] == 3 && pixels[3] == 4);
  dtr_end_canvas();

  static uint16_t full[64*64];
  static const struct dtr_metal_texel metal[]={{0,-20,220,255},{-20,0,180,255},
                                               {20,0,120,255},{0,20,60,255}};
  dtr_begin(full,64,64);dtr_clear(8,10,12);
  dtr_metal_ring_cached(32,32,20,3,metal,4);
  uint16_t region[32*8+2];region[0]=0x55aa;region[32*8+1]=0xaa55;
  struct dte_canvas strip=DTE_CANVAS_INIT;
  strip.scene_width=64;strip.scene_height=64;strip.origin_x=16;strip.origin_y=20;
  strip.width=32;strip.height=8;strip.stride_pixels=32;
  strip.buffer_size=32*8*2;strip.pixels=region+1;
  dtr_begin_canvas(&strip);dtr_clear(8,10,12);
  dtr_metal_ring_cached(32,32,20,3,metal,4);dtr_end_canvas();
  assert(region[0]==0x55aa&&region[32*8+1]==0xaa55);
  for(int y=0;y<8;y++)for(int x=0;x<32;x++)
    assert(region[1+y*32+x]==full[(20+y)*64+16+x]);
  return 0;
}
