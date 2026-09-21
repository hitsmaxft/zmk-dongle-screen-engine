/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <zmk/dongle_theme/theme.h>

#define DTE_FILTER_NONE 0u
#define DTE_FILTER_CRT 1u

/* Apply one deterministic RGB565 post-process in scene coordinates.
 * Returns the number of pixels inspected by the enabled filter. */
uint32_t dte_filter_apply(uint8_t type, const struct dte_canvas *canvas);
