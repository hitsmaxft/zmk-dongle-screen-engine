/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tre/surface.h>

#define TRE_DAMAGE_FULL (UINT32_C(1) << 0)

struct tre_damage {
  uint16_t struct_size;
  uint16_t abi_version;
  uint16_t scene_width;
  uint16_t scene_height;
  uint32_t storage_size;
  void *storage;
  uint32_t private_state[4];
  uint32_t reserved[4];
};

#define TRE_DAMAGE_REQUIRED_SIZE                                              \
  ((uint16_t)offsetof(struct tre_damage, reserved))
#define TRE_DAMAGE_INIT                                                       \
  {                                                                          \
    .struct_size = (uint16_t)sizeof(struct tre_damage),                       \
    .abi_version = TRE_ABI_VERSION                                            \
  }

size_t tre_damage_storage_size(uint16_t width, uint16_t height);
size_t tre_damage_storage_align(void);
tre_result_t tre_damage_init(struct tre_damage *damage, uint16_t width,
                             uint16_t height, void *storage,
                             size_t storage_size);
tre_result_t tre_damage_reset(struct tre_damage *damage);
tre_result_t tre_damage_all(struct tre_damage *damage);
tre_result_t tre_damage_rect(struct tre_damage *damage,
                             const struct tre_rect *rect);
int tre_damage_any(const struct tre_damage *damage);
tre_result_t tre_damage_collect(struct tre_damage *damage,
                                struct tre_rect *rectangles,
                                uint16_t capacity, uint16_t *out_count,
                                uint32_t *out_flags);
