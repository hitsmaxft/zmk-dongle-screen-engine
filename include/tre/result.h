/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>

typedef int32_t tre_result_t;

#define TRE_OK ((tre_result_t)0)
#define TRE_ERR_INVALID_ARGUMENT ((tre_result_t)-1)
#define TRE_ERR_UNSUPPORTED_ABI ((tre_result_t)-2)
#define TRE_ERR_STRUCT_TOO_SMALL ((tre_result_t)-3)
#define TRE_ERR_UNSUPPORTED_FORMAT ((tre_result_t)-4)
#define TRE_ERR_OUT_OF_BOUNDS ((tre_result_t)-5)
#define TRE_ERR_CAPACITY ((tre_result_t)-6)
#define TRE_ERR_ALIGNMENT ((tre_result_t)-7)

#define TRE_ABI_VERSION UINT16_C(0x0100)
#define TRE_ABI_VERSION_MAJOR(version) ((uint8_t)((version) >> 8))
#define TRE_ABI_VERSION_MINOR(version) ((uint8_t)((version) & UINT16_C(0xff)))
