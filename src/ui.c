/* SPDX-License-Identifier: MIT */
#include <zmk/dongle_theme/raster.h>
#include <zmk/dongle_theme/ui.h>

void dte_sprite_blit(const struct dte_sprite *sprite, int x, int y, int alpha) {
  int count = sprite->width * sprite->height;
  for (int i = 0; i < count; i++) {
    if (sprite->pixels[i] == 1)
      continue;
    uint16_t color = sprite->pixels[i];
    if (alpha == 255) {
      dtr_pixel565(x + i % sprite->width, y + i / sprite->width, color);
    } else {
      dtr_pixel(x + i % sprite->width, y + i / sprite->width,
                ((color >> 11) & 31) * 255 / 31,
                ((color >> 5) & 63) * 255 / 63,
                (color & 31) * 255 / 31, alpha);
    }
  }
}

void dte_sprite_blit_opaque(const struct dte_sprite *sprite, int x, int y) {
  int count = sprite->width * sprite->height;
  for (int i = 0; i < count; i++)
    dtr_pixel565(x + i % sprite->width, y + i / sprite->width,
                 sprite->pixels[i]);
}

void dte_ui_rect(int x, int y, int width, int height, int red, int green,
                 int blue, int alpha) {
  if (alpha < 255) {
    dtr_rect(x, y, width, height, red, green, blue, alpha);
    return;
  }
  uint16_t color = dtr_rgb(red, green, blue);
  for (int yy = y; yy < y + height; yy++)
    for (int xx = x; xx < x + width; xx++)
      dtr_pixel565(xx, yy, color);
}

void dte_ui_round_rect(int x, int y, int width, int height, int radius,
                       int red, int green, int blue) {
  uint16_t color = dtr_rgb(red, green, blue);
  for (int yy = 0; yy < height; yy++)
    for (int xx = 0; xx < width; xx++) {
      int edge = yy < height - 1 - yy ? yy : height - 1 - yy;
      int cut = edge < radius ? radius - 1 - edge : 0;
      if (xx >= cut && xx < width - cut)
        dtr_pixel565(x + xx, y + yy, color);
    }
}

void dte_ui_format_number(char *output, int value) {
  if (value < 0) {
    output[0] = '-'; output[1] = '-'; output[2] = 0;
    return;
  }
  if (value > 999)
    value = 999;
  int count = 0;
  if (value >= 100)
    output[count++] = '0' + value / 100;
  if (value >= 10)
    output[count++] = '0' + value / 10 % 10;
  output[count++] = '0' + value % 10;
  output[count] = 0;
}

void dte_ui_format_percent(char *output, int value) {
  dte_ui_format_number(output, value);
  int count = 0;
  while (output[count])
    count++;
  if (value >= 0)
    output[count++] = '%';
  output[count] = 0;
}

uint32_t dte_ui_hash3(int x, int y, int time) {
  uint32_t value = (uint32_t)x * 0x45d9f3bu ^
                   (uint32_t)y * 0x119de1f3u ^
                   (uint32_t)time * 0x3449f5u;
  value ^= value >> 16;
  value *= 0x45d9f3bu;
  value ^= value >> 16;
  return value;
}
