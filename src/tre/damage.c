/* SPDX-License-Identifier: MIT */
#include "internal.h"

#include <stdint.h>

#include <tre/damage.h>

#define TRE_DAMAGE_CELL_SIZE 16u

static void clear_storage(void *storage, size_t bytes) {
  uint32_t *words = storage;
  for (size_t index = 0; index < bytes / sizeof(*words); index++)
    words[index] = 0;
}

static uint32_t columns_for(uint16_t width) {
  return ((uint32_t)width + TRE_DAMAGE_CELL_SIZE - 1u) / TRE_DAMAGE_CELL_SIZE;
}

static uint32_t rows_for(uint16_t height) {
  return ((uint32_t)height + TRE_DAMAGE_CELL_SIZE - 1u) / TRE_DAMAGE_CELL_SIZE;
}

size_t tre_damage_storage_size(uint16_t width, uint16_t height) {
  if (!width || !height)
    return 0;
  uint64_t cells = (uint64_t)columns_for(width) * rows_for(height);
  uint64_t bytes = ((cells + 31u) / 32u) * 4u;
  return bytes > SIZE_MAX ? 0 : (size_t)bytes;
}

size_t tre_damage_storage_align(void) { return 4; }

static tre_result_t validate_damage(const struct tre_damage *damage) {
  if (!damage)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      damage->abi_version, damage->struct_size, TRE_DAMAGE_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  size_t required =
      tre_damage_storage_size(damage->scene_width, damage->scene_height);
  if (!required || !damage->storage || damage->storage_size < required)
    return TRE_ERR_CAPACITY;
  if ((uintptr_t)damage->storage % tre_damage_storage_align())
    return TRE_ERR_ALIGNMENT;
  return TRE_OK;
}

tre_result_t tre_damage_init(struct tre_damage *damage, uint16_t width,
                             uint16_t height, void *storage,
                             size_t storage_size) {
  if (!damage || !storage || !width || !height)
    return TRE_ERR_INVALID_ARGUMENT;
  tre_result_t result = tre_validate_record(
      damage->abi_version, damage->struct_size, TRE_DAMAGE_REQUIRED_SIZE);
  if (result != TRE_OK)
    return result;
  size_t required = tre_damage_storage_size(width, height);
  if (storage_size < required || storage_size > UINT32_MAX)
    return TRE_ERR_CAPACITY;
  if ((uintptr_t)storage % tre_damage_storage_align())
    return TRE_ERR_ALIGNMENT;
  damage->scene_width = width;
  damage->scene_height = height;
  damage->storage_size = (uint32_t)storage_size;
  damage->storage = storage;
  damage->private_state[0] = columns_for(width);
  damage->private_state[1] = rows_for(height);
  damage->private_state[2] = 0;
  damage->private_state[3] = 0;
  clear_storage(storage, required);
  return TRE_OK;
}

tre_result_t tre_damage_reset(struct tre_damage *damage) {
  tre_result_t result = validate_damage(damage);
  if (result != TRE_OK)
    return result;
  clear_storage(
      damage->storage,
      tre_damage_storage_size(damage->scene_width, damage->scene_height));
  damage->private_state[2] = 0;
  return TRE_OK;
}

tre_result_t tre_damage_all(struct tre_damage *damage) {
  tre_result_t result = validate_damage(damage);
  if (result != TRE_OK)
    return result;
  damage->private_state[2] = 1;
  return TRE_OK;
}

static void set_cell(struct tre_damage *damage, uint32_t x, uint32_t y) {
  uint32_t bit = y * damage->private_state[0] + x;
  ((uint32_t *)damage->storage)[bit / 32u] |= UINT32_C(1) << (bit % 32u);
}

static int get_cell(const struct tre_damage *damage, uint32_t x, uint32_t y) {
  uint32_t bit = y * damage->private_state[0] + x;
  return ((((const uint32_t *)damage->storage)[bit / 32u] >> (bit % 32u)) &
           1u) != 0;
}

tre_result_t tre_damage_rect(struct tre_damage *damage,
                             const struct tre_rect *rect) {
  tre_result_t result = validate_damage(damage);
  if (result != TRE_OK)
    return result;
  if (!rect || !rect->width || !rect->height)
    return TRE_ERR_INVALID_ARGUMENT;
  int32_t left = rect->x < 0 ? 0 : rect->x;
  int32_t top = rect->y < 0 ? 0 : rect->y;
  int32_t right = (int32_t)rect->x + rect->width;
  int32_t bottom = (int32_t)rect->y + rect->height;
  if (right > damage->scene_width)
    right = damage->scene_width;
  if (bottom > damage->scene_height)
    bottom = damage->scene_height;
  if (right <= left || bottom <= top)
    return TRE_OK;
  uint32_t first_x = (uint32_t)left / TRE_DAMAGE_CELL_SIZE;
  uint32_t last_x = (uint32_t)(right - 1) / TRE_DAMAGE_CELL_SIZE;
  uint32_t first_y = (uint32_t)top / TRE_DAMAGE_CELL_SIZE;
  uint32_t last_y = (uint32_t)(bottom - 1) / TRE_DAMAGE_CELL_SIZE;
  for (uint32_t y = first_y; y <= last_y; y++)
    for (uint32_t x = first_x; x <= last_x; x++)
      set_cell(damage, x, y);
  return TRE_OK;
}

int tre_damage_any(const struct tre_damage *damage) {
  if (validate_damage(damage) != TRE_OK)
    return 0;
  if (damage->private_state[2])
    return 1;
  uint32_t words = (damage->private_state[0] * damage->private_state[1] + 31u) /
                   32u;
  for (uint32_t index = 0; index < words; index++)
    if (((const uint32_t *)damage->storage)[index])
      return 1;
  return 0;
}

static uint16_t count_row_runs(const struct tre_damage *damage) {
  uint32_t count = 0;
  for (uint32_t y = 0; y < damage->private_state[1]; y++) {
    uint32_t x = 0;
    while (x < damage->private_state[0]) {
      while (x < damage->private_state[0] && !get_cell(damage, x, y))
        x++;
      if (x == damage->private_state[0])
        break;
      count++;
      while (x < damage->private_state[0] && get_cell(damage, x, y))
        x++;
    }
  }
  return count > UINT16_MAX ? UINT16_MAX : (uint16_t)count;
}

tre_result_t tre_damage_collect(struct tre_damage *damage,
                                struct tre_rect *rectangles,
                                uint16_t capacity, uint16_t *out_count,
                                uint32_t *out_flags) {
  tre_result_t result = validate_damage(damage);
  if (result != TRE_OK)
    return result;
  if (!out_count || !out_flags || (capacity && !rectangles))
    return TRE_ERR_INVALID_ARGUMENT;
  *out_count = 0;
  *out_flags = 0;
  if (!tre_damage_any(damage))
    return TRE_OK;
  uint16_t count = damage->private_state[2] ? 1 : count_row_runs(damage);
  if (!capacity)
    return TRE_ERR_CAPACITY;
  if (damage->private_state[2] || count > capacity) {
    rectangles[0] = (struct tre_rect){0, 0, damage->scene_width,
                                     damage->scene_height};
    *out_count = 1;
    *out_flags = TRE_DAMAGE_FULL;
    return tre_damage_reset(damage);
  }
  uint16_t output = 0;
  for (uint32_t y = 0; y < damage->private_state[1]; y++) {
    uint32_t x = 0;
    while (x < damage->private_state[0]) {
      while (x < damage->private_state[0] && !get_cell(damage, x, y))
        x++;
      if (x == damage->private_state[0])
        break;
      uint32_t first = x;
      while (x < damage->private_state[0] && get_cell(damage, x, y))
        x++;
      uint32_t right = x * TRE_DAMAGE_CELL_SIZE;
      uint32_t bottom = (y + 1u) * TRE_DAMAGE_CELL_SIZE;
      if (right > damage->scene_width)
        right = damage->scene_width;
      if (bottom > damage->scene_height)
        bottom = damage->scene_height;
      rectangles[output++] = (struct tre_rect){
          (int16_t)(first * TRE_DAMAGE_CELL_SIZE),
          (int16_t)(y * TRE_DAMAGE_CELL_SIZE),
          (uint16_t)(right - first * TRE_DAMAGE_CELL_SIZE),
          (uint16_t)(bottom - y * TRE_DAMAGE_CELL_SIZE)};
    }
  }
  *out_count = output;
  return tre_damage_reset(damage);
}
