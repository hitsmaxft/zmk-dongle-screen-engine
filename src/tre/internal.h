/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tre/render.h>

tre_result_t tre_validate_record(uint16_t version, uint16_t size,
                                 uint16_t required_size);
bool tre_context_pixel_offset(const struct tre_render_ctx *ctx, int32_t x,
                              int32_t y, uint32_t *offset);
uint16_t tre_load565(const uint8_t *bytes);
void tre_store565(uint8_t *bytes, uint16_t color);
uint16_t tre_blend565(uint16_t destination, uint16_t source, uint8_t opacity);
