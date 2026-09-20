/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <tre/damage.h>
#include <tre/tile.h>

static uint16_t pixel(const uint8_t *bytes, uint32_t stride, uint32_t x,
                      uint32_t y) {
  const uint8_t *p = bytes + y * stride + x * 2u;
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void store(uint8_t *bytes, uint32_t stride, uint32_t x, uint32_t y,
                  uint16_t value) {
  uint8_t *p = bytes + y * stride + x * 2u;
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void test_surface_and_primitives(void) {
  uint8_t pixels[4 * 3 * 2] = {0};
  struct tre_surface surface = TRE_SURFACE_INIT;
  surface.scene_width = 8;
  surface.scene_height = 7;
  surface.origin_x = 2;
  surface.origin_y = 3;
  surface.width = 4;
  surface.height = 3;
  surface.stride_bytes = 8;
  surface.buffer_size = sizeof(pixels);
  surface.pixels = pixels;
  assert(tre_surface_validate(&surface) == TRE_OK);

  struct tre_surface malformed = surface;
  malformed.struct_size = TRE_SURFACE_REQUIRED_SIZE - 1;
  assert(tre_surface_validate(&malformed) == TRE_ERR_STRUCT_TOO_SMALL);
  malformed = surface;
  malformed.abi_version = 0x0200;
  assert(tre_surface_validate(&malformed) == TRE_ERR_UNSUPPORTED_ABI);
  malformed = surface;
  malformed.stride_bytes = 7;
  assert(tre_surface_validate(&malformed) == TRE_ERR_OUT_OF_BOUNDS);
  malformed = surface;
  malformed.buffer_size--;
  assert(tre_surface_validate(&malformed) == TRE_ERR_CAPACITY);
  malformed = surface;
  malformed.origin_x = 7;
  assert(tre_surface_validate(&malformed) == TRE_ERR_OUT_OF_BOUNDS);
  malformed = surface;
  malformed.scene_width = 32768;
  assert(tre_surface_validate(&malformed) == TRE_ERR_OUT_OF_BOUNDS);

  struct tre_render_ctx ctx = TRE_RENDER_CTX_INIT;
  assert(tre_render_begin(&ctx, &surface) == TRE_OK);
  assert(tre_draw_pixel565(&ctx, 2, 3, 0xf800, 255) == TRE_OK);
  assert(pixel(pixels, 8, 0, 0) == 0xf800);
  assert(tre_draw_pixel565(&ctx, 1, 3, 0xffff, 255) == TRE_OK);
  assert(pixel(pixels, 8, 0, 0) == 0xf800);

  struct tre_rect clip = {3, 4, 2, 1};
  struct tre_rect fill = {2, 3, 4, 3};
  assert(tre_render_set_clip(&ctx, &clip) == TRE_OK);
  assert(tre_fill_rect(&ctx, &fill, 0x07e0, 255) == TRE_OK);
  assert(pixel(pixels, 8, 1, 1) == 0x07e0);
  assert(pixel(pixels, 8, 2, 1) == 0x07e0);
  assert(pixel(pixels, 8, 3, 1) == 0);
  assert(tre_render_reset_clip(&ctx) == TRE_OK);
  assert(tre_draw_line(&ctx, 2, 5, 5, 5, 1, 0x001f, 255) == TRE_OK);
  for (uint32_t x = 0; x < 4; x++)
    assert(pixel(pixels, 8, x, 2) == 0x001f);

  uint8_t other_pixels[2] = {0};
  struct tre_surface other = TRE_SURFACE_INIT;
  other.scene_width = other.width = 1;
  other.scene_height = other.height = 1;
  other.stride_bytes = other.buffer_size = 2;
  other.pixels = other_pixels;
  struct tre_render_ctx other_ctx = TRE_RENDER_CTX_INIT;
  assert(tre_render_begin(&other_ctx, &other) == TRE_OK);
  assert(tre_draw_pixel565(&other_ctx, 0, 0, 0xabcd, 255) == TRE_OK);
  assert(pixel(other_pixels, 2, 0, 0) == 0xabcd);
  assert(pixel(pixels, 8, 0, 0) == 0xf800);
}

static struct tre_image image(uint8_t format, uint16_t width, uint16_t height,
                              uint32_t stride, const uint8_t *data,
                              uint32_t size) {
  struct tre_image result = TRE_IMAGE_INIT;
  result.width = width;
  result.height = height;
  result.stride_bytes = stride;
  result.format = format;
  result.data_size = size;
  result.data = data;
  return result;
}

static void test_images_tiles_and_glyphs(void) {
  uint8_t target[4 * 3 * 2] = {0};
  struct tre_surface surface = TRE_SURFACE_INIT;
  surface.scene_width = surface.width = 4;
  surface.scene_height = surface.height = 3;
  surface.stride_bytes = 8;
  surface.buffer_size = sizeof(target);
  surface.pixels = target;
  struct tre_render_ctx ctx = TRE_RENDER_CTX_INIT;
  assert(tre_render_begin(&ctx, &surface) == TRE_OK);

  uint8_t rgb[4 * 2 * 2] = {0};
  for (uint32_t y = 0; y < 2; y++)
    for (uint32_t x = 0; x < 4; x++)
      store(rgb, 8, x, y, (uint16_t)(1 + y * 4 + x));
  struct tre_image atlas = image(TRE_IMAGE_RGB565_LE, 4, 2, 8, rgb,
                                 sizeof(rgb));
  struct tre_rect source = {1, 0, 2, 2};
  assert(tre_draw_image(&ctx, &atlas, &source, 0, 0, 0, 255,
                        TRE_DRAW_FLIP_X) == TRE_OK);
  assert(pixel(target, 8, 0, 0) == 3);
  assert(pixel(target, 8, 1, 0) == 2);
  assert(pixel(target, 8, 0, 1) == 7);

  memset(target, 0, sizeof(target));
  assert(tre_draw_image(&ctx, &atlas, &source, 0, 0, 0, 255,
                        TRE_DRAW_FLIP_Y) == TRE_OK);
  assert(pixel(target, 8, 0, 0) == 6);
  assert(pixel(target, 8, 1, 0) == 7);

  struct tre_image bad = atlas;
  bad.stride_bytes = 7;
  assert(tre_image_validate(&bad) == TRE_ERR_OUT_OF_BOUNDS);
  source.x = 3;
  assert(tre_draw_image(&ctx, &atlas, &source, 0, 0, 0, 255, 0) ==
         TRE_ERR_OUT_OF_BOUNDS);
  source = (struct tre_rect){0, 0, 2, 1};
  assert(tre_draw_image(&ctx, &atlas, &source, INT32_MAX, 0, 0, 255, 0) ==
         TRE_ERR_OUT_OF_BOUNDS);

  memset(target, 0, sizeof(target));
  atlas.format = TRE_IMAGE_RGB565_KEYED_LE;
  atlas.transparent_key = 2;
  source = (struct tre_rect){0, 0, 2, 1};
  assert(tre_draw_image(&ctx, &atlas, &source, 0, 0, 0, 255, 0) == TRE_OK);
  assert(pixel(target, 8, 0, 0) == 1);
  assert(pixel(target, 8, 1, 0) == 0);

  memset(target, 0, sizeof(target));
  uint8_t mask[] = {0x80, 0x40};
  atlas.format = TRE_IMAGE_RGB565_A1_MSB;
  atlas.mask = mask;
  atlas.mask_stride_bytes = 1;
  atlas.mask_size = sizeof(mask);
  source = (struct tre_rect){0, 0, 2, 2};
  assert(tre_draw_image(&ctx, &atlas, &source, 0, 0, 0, 255, 0) == TRE_OK);
  assert(pixel(target, 8, 0, 0) == 1);
  assert(pixel(target, 8, 1, 0) == 0);
  assert(pixel(target, 8, 0, 1) == 0);
  assert(pixel(target, 8, 1, 1) == 6);
  bad = atlas;
  bad.mask_size = 1;
  assert(tre_image_validate(&bad) == TRE_ERR_OUT_OF_BOUNDS);

  memset(target, 0, sizeof(target));
  const uint8_t mono[] = {0xa0};
  struct tre_image coverage = image(TRE_IMAGE_MONO1_MSB, 3, 1, 1, mono,
                                    sizeof(mono));
  source = (struct tre_rect){0, 0, 3, 1};
  assert(tre_draw_image(&ctx, &coverage, &source, 0, 0, 0xffff, 255, 0) ==
         TRE_OK);
  assert(pixel(target, 8, 0, 0) == 0xffff);
  assert(pixel(target, 8, 1, 0) == 0);
  assert(pixel(target, 8, 2, 0) == 0xffff);

  memset(target, 0, sizeof(target));
  const uint8_t a4[] = {0xf8, 0x00};
  coverage = image(TRE_IMAGE_A4_MSB, 3, 1, 2, a4, sizeof(a4));
  struct tre_glyph glyph = TRE_GLYPH_INIT;
  glyph.coverage = &coverage;
  glyph.bearing_x = 1;
  glyph.bearing_y = 1;
  assert(tre_draw_glyph(&ctx, &glyph, 0, 1, 0xffff, 255) == TRE_OK);
  assert(pixel(target, 8, 1, 0) == 0xffff);
  assert(pixel(target, 8, 2, 0) != 0);
  assert(pixel(target, 8, 2, 0) != 0xffff);

  memset(target, 0, sizeof(target));
  atlas = image(TRE_IMAGE_RGB565_LE, 4, 2, 8, rgb, sizeof(rgb));
  struct tre_tileset tiles = TRE_TILESET_INIT;
  tiles.image = &atlas;
  tiles.tile_width = 2;
  tiles.tile_height = 1;
  assert(tre_draw_tile(&ctx, &tiles, 3, 1, 1, 0, 255, 0) == TRE_OK);
  assert(pixel(target, 8, 1, 1) == 7);
  assert(pixel(target, 8, 2, 1) == 8);
  assert(tre_draw_tile(&ctx, &tiles, 4, 0, 0, 0, 255, 0) ==
         TRE_ERR_OUT_OF_BOUNDS);
}

static void test_damage(void) {
  _Alignas(4) uint8_t storage[32];
  struct tre_damage damage = TRE_DAMAGE_INIT;
  size_t required = tre_damage_storage_size(40, 33);
  assert(required <= sizeof(storage));
  assert(tre_damage_init(&damage, 40, 33, storage, required) == TRE_OK);
  assert(!tre_damage_any(&damage));

  struct tre_rect previous = {0, 0, 5, 5};
  struct tre_rect current = {31, 16, 5, 5};
  assert(tre_damage_rect(&damage, &previous) == TRE_OK);
  assert(tre_damage_rect(&damage, &current) == TRE_OK);
  assert(tre_damage_any(&damage));
  struct tre_rect rectangles[4];
  uint16_t count = 0;
  uint32_t flags = 0;
  assert(tre_damage_collect(&damage, rectangles, 4, &count, &flags) == TRE_OK);
  assert(count == 2);
  assert(flags == 0);
  assert(!tre_damage_any(&damage));

  /* A hidden object dirties only its previous conservative bounds. */
  assert(tre_damage_rect(&damage, &previous) == TRE_OK);
  assert(tre_damage_collect(&damage, rectangles, 4, &count, &flags) == TRE_OK);
  assert(count == 1 && rectangles[0].x == 0 && rectangles[0].y == 0);

  assert(tre_damage_rect(&damage, &previous) == TRE_OK);
  assert(tre_damage_rect(&damage, &current) == TRE_OK);
  assert(tre_damage_collect(&damage, rectangles, 1, &count, &flags) == TRE_OK);
  assert(count == 1 && flags == TRE_DAMAGE_FULL);
  assert(rectangles[0].width == 40 && rectangles[0].height == 33);

  assert(tre_damage_all(&damage) == TRE_OK);
  assert(tre_damage_collect(&damage, rectangles, 1, &count, &flags) == TRE_OK);
  assert(count == 1 && flags == TRE_DAMAGE_FULL);

  struct tre_damage bad = TRE_DAMAGE_INIT;
  assert(tre_damage_init(&bad, 40, 33, storage + 1, required - 1) ==
         TRE_ERR_CAPACITY);
  assert(tre_damage_init(&bad, 40, 33, storage + 1, required) ==
         TRE_ERR_ALIGNMENT);
}

int main(void) {
  test_surface_and_primitives();
  test_images_tiles_and_glyphs();
  test_damage();
  return 0;
}
