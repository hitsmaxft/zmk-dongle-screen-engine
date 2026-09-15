/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>
#include <zmk/dongle_theme/raster.h>
static uint16_t pixels[280*240],final[280*240];
static uint8_t seen[280*240];
static uint32_t hash(void) {uint32_t h=2166136261u;for(int i=0;i<280*240;i++)h=(h^pixels[i])*16777619u;return h;}
int main(void) {
  memset(pixels,0,sizeof(pixels));dtr_begin(pixels,280,240);
  dtr_arc_f(140,120,75,91,150,390,229,234,237,255);
  memcpy(final,pixels,sizeof(final));
  uint32_t signatures[3];
  for(int pattern=0;pattern<3;pattern++) {
    memset(seen,0,sizeof(seen));
    for(int density=0;density<=255;density++) {
      memset(pixels,0,sizeof(pixels));dtr_begin(pixels,280,240);
      dtr_arc_reveal(140,120,75,91,150,390,229,234,237,255,density,pattern);
      for(int i=0;i<280*240;i++){assert(!seen[i]||pixels[i]);seen[i]=pixels[i]!=0;}
      if(density==0)for(int i=0;i<280*240;i++)assert(!pixels[i]);
      if(density==128)signatures[pattern]=hash();
      if(density==255)assert(!memcmp(final,pixels,sizeof(final)));
      dtr_pixel(0,0,255,255,255,255);assert(pixels[0]==0xffff); /* mask is local */
    }
  }
  assert(signatures[0]!=signatures[1]&&signatures[1]!=signatures[2]&&signatures[0]!=signatures[2]);
  return 0;
}
