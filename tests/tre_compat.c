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
  return 0;
}
