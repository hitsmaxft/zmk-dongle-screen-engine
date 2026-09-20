/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tre/result.h>

#define TRE_PIXEL_FORMAT_RGB565_LE UINT8_C(1)

struct tre_rect {
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
};

struct tre_surface {
  uint16_t struct_size;
  uint16_t abi_version;
  uint16_t scene_width;
  uint16_t scene_height;
  int16_t origin_x;
  int16_t origin_y;
  uint16_t width;
  uint16_t height;
  uint32_t stride_bytes;
  uint8_t pixel_format;
  uint8_t flags;
  uint16_t reserved0;
  uint32_t buffer_size;
  uint8_t *pixels;
  uint32_t reserved[4];
};

#define TRE_SURFACE_REQUIRED_SIZE                                             \
  ((uint16_t)offsetof(struct tre_surface, reserved))
#define TRE_SURFACE_INIT                                                      \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct tre_surface),                      \
    .abi_version = TRE_ABI_VERSION,                                           \
    .pixel_format = TRE_PIXEL_FORMAT_RGB565_LE                                \
  }

tre_result_t tre_surface_validate(const struct tre_surface *surface);
