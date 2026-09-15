/* Shared reference geometry for live sampling and build-time flash atlases. */
/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
static inline float dtr_sample_limit(float x) { return x<0?0:x>1?1:x; }
static inline int dtr_metal_sample(int dx, int dy, int radius,
                                    float (*metal)(float), uint8_t *c, uint8_t *alpha) {
  float tone=0,coverage=0;
  for(int yy=0;yy<2;yy++) for(int xx=0;xx<2;xx++) {
    float sx=dx+(xx?.25f:-.25f),sy=dy+(yy?.25f:-.25f);
    float band=radius-__builtin_sqrtf(sx*sx+sy*sy);
    float a=dtr_sample_limit(band+.5f);
    coverage+=a;
    tone+=(metal(band)+(-sx-sy)*28/(radius*2))*a;
  }
  if(coverage<=0) return 0;
  int value=(int)(tone/coverage);
  *c=value<0?0:value>250?250:value;
  *alpha=(int)(coverage*255/4);
  return 1;
}
