/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>
#include <zmk/dongle_theme/filter.h>

static uint16_t full[280 * 240], partitioned[280 * 240];

int main(void) {
  for (int i = 0; i < 280 * 240; i++) full[i] = partitioned[i] = (uint16_t)(i * 73u);
  struct dte_canvas canvas = DTE_CANVAS_INIT;
  canvas.scene_width=280;canvas.scene_height=240;canvas.width=280;canvas.height=240;
  canvas.stride_pixels=280;canvas.buffer_size=sizeof(full);canvas.pixels=full;
  assert(dte_filter_apply(DTE_FILTER_NONE,&canvas)==0);
  uint16_t center=full[120*280+140];
  assert(dte_filter_apply(DTE_FILTER_CRT,&canvas)>0);
  assert(full[0]==0&&full[120*280+140]==center);
  for(int y=0;y<240;y+=16){
    canvas.origin_y=y;canvas.height=y+16>240?240-y:16;
    canvas.buffer_size=(uint32_t)canvas.height*280*2u;
    canvas.pixels=partitioned+y*280;
    uint32_t inspected=dte_filter_apply(DTE_FILTER_CRT,&canvas);
    assert(inspected>0||(y>16&&y<208));
  }
  assert(memcmp(full,partitioned,sizeof(full))==0);
  return 0;
}
