/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zmk/dongle_theme/v2/pack.h>

#define PACK_SIZE 108u

struct memory_source {
  const uint8_t *bytes;
  uint32_t size;
  uint32_t reads;
  uint32_t maps;
  uint32_t unmaps;
};

static void write_u16_le(uint8_t *bytes, uint16_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32(const uint8_t *bytes, uint32_t length) {
  uint32_t crc = UINT32_MAX;
  for (uint32_t index = 0; index < length; index++) {
    uint8_t value = index >= ZDSE_PACK_CRC_OFFSET &&
                            index < ZDSE_PACK_CRC_OFFSET + 4
                        ? 0
                        : bytes[index];
    crc ^= value;
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
    }
  }
  return crc ^ UINT32_MAX;
}

static void finish_crc(uint8_t *pack) {
  write_u32_le(pack + ZDSE_PACK_CRC_OFFSET, 0);
  write_u32_le(pack + ZDSE_PACK_CRC_OFFSET, crc32(pack, PACK_SIZE));
}

static void make_pack(uint8_t *pack) {
  memset(pack, 0, PACK_SIZE);
  memcpy(pack, "ZDS1", 4);
  write_u16_le(pack + 4, ZDSE_PACK_FORMAT_MAJOR);
  write_u16_le(pack + 6, ZDSE_PACK_FORMAT_MINOR);
  write_u16_le(pack + 8, ZDSE_PACK_HEADER_SIZE);
  write_u16_le(pack + 10, ZDSE_PACK_SECTION_SIZE);
  write_u32_le(pack + 12, PACK_SIZE);
  write_u32_le(pack + 16, ZDSE_PACK_HEADER_SIZE);
  write_u32_le(pack + 20, 2);
  write_u32_le(pack + 24, ZDSE_PACK_FEATURE_RGB565_KEYED);
  write_u32_le(pack + 28, UINT32_C(1) << 31);
  write_u32_le(pack + 32, UINT32_C(0x12345678));

  write_u16_le(pack + 48, ZDSE_PACK_SECTION_SCENES);
  write_u16_le(pack + 50, ZDSE_PACK_SECTION_REQUIRED);
  write_u32_le(pack + 52, 96);
  write_u32_le(pack + 56, 8);
  write_u32_le(pack + 60, 8);
  write_u32_le(pack + 64, 1);
  write_u16_le(pack + 68, 4);

  write_u16_le(pack + 72, ZDSE_PACK_SECTION_DATA);
  write_u16_le(pack + 74, 0);
  write_u32_le(pack + 76, 104);
  write_u32_le(pack + 80, 4);
  write_u32_le(pack + 84, 0);
  write_u32_le(pack + 88, 0);
  write_u16_le(pack + 92, 1);

  for (uint32_t index = 96; index < PACK_SIZE; index++) {
    pack[index] = (uint8_t)(index * 17u);
  }
  finish_crc(pack);
}

static zdse_result_t memory_read(void *user_data, uint32_t offset,
                                 void *destination, uint32_t length) {
  struct memory_source *source = user_data;
  source->reads++;
  if (offset > source->size || length > source->size - offset) {
    return ZDSE_ERR_CALLBACK;
  }
  memcpy(destination, source->bytes + offset, length);
  return ZDSE_OK;
}

static const uint8_t *memory_map(void *user_data, uint32_t offset,
                                 uint32_t length) {
  struct memory_source *source = user_data;
  source->maps++;
  if (offset > source->size || length > source->size - offset) {
    return NULL;
  }
  return source->bytes + offset;
}

static void memory_unmap(void *user_data, const uint8_t *pointer,
                         uint32_t length) {
  struct memory_source *source = user_data;
  (void)pointer;
  (void)length;
  source->unmaps++;
}

static zdse_result_t validate(uint8_t *pack, int map,
                              struct zdse_pack_info_v2 *info,
                              struct memory_source *memory) {
  _Alignas(4) uint8_t scratch[ZDSE_PACK_QUERY_SCRATCH_BYTES];
  struct zdse_asset_source_v2 source = ZDSE_ASSET_SOURCE_V2_INIT;
  *memory = (struct memory_source){.bytes = pack, .size = PACK_SIZE};
  source.user_data = memory;
  source.read = memory_read;
  if (map) {
    source.optional_capabilities = ZDSE_ASSET_SOURCE_CAP_MAP;
    source.map = memory_map;
    source.unmap = memory_unmap;
  }
  return zdse_pack_validate(&source, scratch, sizeof(scratch), info);
}

int main(void) {
  uint8_t storage[PACK_SIZE + 1];
  uint8_t *pack = storage + 1; /* Deliberately unaligned pack bytes. */
  struct memory_source memory;
  struct zdse_pack_info_v2 info = ZDSE_PACK_INFO_V2_INIT;

  _Static_assert(ZDSE_ABI_VERSION_MAJOR(UINT16_C(0x02ff)) == 2,
                 "ABI major decoding changed");
  _Static_assert(ZDSE_ABI_VERSION_MINOR(UINT16_C(0x02ff)) == 255,
                 "ABI minor decoding changed");
  _Static_assert(offsetof(struct zdse_asset_source_v2, struct_size) == 0 &&
                     offsetof(struct zdse_asset_source_v2, abi_version) == 2,
                 "asset source ABI prefix changed");
  _Static_assert(offsetof(struct zdse_pack_info_v2, struct_size) == 0 &&
                     offsetof(struct zdse_pack_info_v2, abi_version) == 2,
                 "pack info ABI prefix changed");
  assert(crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
  assert(zdse_pack_query_scratch_size() == ZDSE_PACK_QUERY_SCRATCH_BYTES);
  assert(zdse_pack_query_scratch_align() == ZDSE_PACK_QUERY_SCRATCH_ALIGN);

  make_pack(pack);
  assert(validate(pack, 1, &info, &memory) == ZDSE_OK);
  assert(info.total_size == PACK_SIZE && info.section_count == 2);
  assert(info.root_scene_id == UINT32_C(0x12345678));
  assert(info.required_features == ZDSE_PACK_FEATURE_RGB565_KEYED);
  assert(memory.reads == 1 && memory.maps == 1 && memory.unmaps == 1);
  assert(info.read_calls == 1 && info.map_calls == 1);

  info = (struct zdse_pack_info_v2)ZDSE_PACK_INFO_V2_INIT;
  assert(validate(pack, 0, &info, &memory) == ZDSE_OK);
  assert(info.map_calls == 0 && info.read_calls == 4);
  assert(info.bytes_read == PACK_SIZE + ZDSE_PACK_HEADER_SIZE +
                                2 * ZDSE_PACK_SECTION_SIZE);

  make_pack(pack);
  pack[PACK_SIZE - 1] ^= 1;
  info = (struct zdse_pack_info_v2)ZDSE_PACK_INFO_V2_INIT;
  assert(validate(pack, 0, &info, &memory) == ZDSE_ERR_CRC);

  make_pack(pack);
  write_u16_le(pack + 4, 2);
  finish_crc(pack);
  assert(validate(pack, 0, &info, &memory) ==
         ZDSE_ERR_UNSUPPORTED_FORMAT);

  make_pack(pack);
  write_u32_le(pack + 24, UINT32_C(1) << 31);
  finish_crc(pack);
  assert(validate(pack, 0, &info, &memory) ==
         ZDSE_ERR_UNSUPPORTED_FEATURE);

  make_pack(pack);
  write_u32_le(pack + 76, 100); /* Second section overlaps the first. */
  finish_crc(pack);
  assert(validate(pack, 0, &info, &memory) == ZDSE_ERR_BAD_PACK);

  make_pack(pack);
  write_u16_le(pack + 72, UINT16_C(0x7777));
  write_u16_le(pack + 74, ZDSE_PACK_SECTION_REQUIRED);
  finish_crc(pack);
  assert(validate(pack, 0, &info, &memory) ==
         ZDSE_ERR_UNSUPPORTED_FEATURE);

  make_pack(pack);
  write_u32_le(pack + 64, 2); /* 8 * 2 no longer equals section length 8. */
  finish_crc(pack);
  assert(validate(pack, 0, &info, &memory) == ZDSE_ERR_BAD_PACK);

  make_pack(pack);
  info = (struct zdse_pack_info_v2)ZDSE_PACK_INFO_V2_INIT;
  info.struct_size = ZDSE_PACK_INFO_V2_REQUIRED_SIZE - 1;
  assert(validate(pack, 0, &info, &memory) == ZDSE_ERR_STRUCT_TOO_SMALL);

  info = (struct zdse_pack_info_v2)ZDSE_PACK_INFO_V2_INIT;
  info.abi_version = UINT16_C(0x0201); /* Newer caller minor is accepted. */
  assert(validate(pack, 0, &info, &memory) == ZDSE_OK);

  return 0;
}
