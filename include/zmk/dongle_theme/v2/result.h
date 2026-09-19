/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>

typedef int32_t zdse_result_t;

#define ZDSE_OK ((zdse_result_t)0)
#define ZDSE_ERR_INVALID_ARGUMENT ((zdse_result_t)-1)
#define ZDSE_ERR_BAD_PACK ((zdse_result_t)-2)
#define ZDSE_ERR_UNSUPPORTED_FORMAT ((zdse_result_t)-3)
#define ZDSE_ERR_UNSUPPORTED_FEATURE ((zdse_result_t)-4)
#define ZDSE_ERR_CRC ((zdse_result_t)-5)
#define ZDSE_ERR_CAPACITY ((zdse_result_t)-6)
#define ZDSE_ERR_ALIGNMENT ((zdse_result_t)-7)
#define ZDSE_ERR_CALLBACK ((zdse_result_t)-8)
#define ZDSE_ERR_SOURCE_CHANGED ((zdse_result_t)-9)
#define ZDSE_ERR_STRUCT_TOO_SMALL ((zdse_result_t)-10)
#define ZDSE_ERR_UNSUPPORTED_ABI ((zdse_result_t)-11)

#define ZDSE_ABI_VERSION_V2_0 UINT16_C(0x0200)
#define ZDSE_ABI_VERSION_MAJOR(version) ((uint8_t)((version) >> 8))
#define ZDSE_ABI_VERSION_MINOR(version) ((uint8_t)((version) & UINT16_C(0xff)))
