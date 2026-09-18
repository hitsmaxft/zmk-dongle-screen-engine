/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>

/* Generic RGB565 sprite view. Pixel value 0x0001 is transparent for keyed blits. */
struct dte_sprite {
  uint16_t width;
  uint16_t height;
  const uint16_t *pixels;
};

void dte_sprite_blit(const struct dte_sprite *sprite, int x, int y, int alpha);
void dte_sprite_blit_opaque(const struct dte_sprite *sprite, int x, int y);

void dte_ui_rect(int x, int y, int width, int height, int red, int green,
                 int blue, int alpha);
void dte_ui_round_rect(int x, int y, int width, int height, int radius,
                       int red, int green, int blue);
void dte_ui_format_number(char *output, int value);
void dte_ui_format_percent(char *output, int value);
uint32_t dte_ui_hash3(int x, int y, int time);
