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
  dtr_begin(pixels,280,240);dtr_damage_begin();
  dtr_damage_arc(140,120,88,118,-90,-45);
  const uint32_t *damage=dtr_dirty_tiles();int tiles=0;
  for(int row=0;row<15;row++)for(int col=0;col<18;col++)
    tiles+=(damage[row]>>col)&1u;
  assert(tiles>0&&tiles<30);

  memset(pixels,0,sizeof(pixels));dtr_begin(pixels,280,240);
  dtr_exclude_disc(140,120,20);
  dtr_line(100,120,180,120,3,255,255,255,255);
  dtr_exclude_none();
  assert(pixels[120*280+110]!=0&&pixels[120*280+140]==0&&
         pixels[120*280+170]!=0);
  dtr_line(135,120,145,120,1,255,255,255,255);
  assert(pixels[120*280+140]!=0); /* exclusion is scoped */

  for(int i=0;i<280*240;i++)pixels[i]=0x1234;
  uint32_t canvas_damage[15]={0};canvas_damage[5]=1u<<6;
  struct dte_canvas canvas=DTE_CANVAS_INIT;
  canvas.scene_width=canvas.width=canvas.stride_pixels=280;
  canvas.scene_height=canvas.height=240;
  canvas.buffer_size=sizeof(pixels);canvas.pixels=pixels;
  dtr_set_canvas_damage(canvas_damage,15);dtr_begin_canvas(&canvas);
  dtr_clear(1,2,3);dtr_end_canvas();
  uint16_t cleared=dtr_rgb(1,2,3);
  for(int y=0;y<240;y++)for(int x=0;x<280;x++)
    assert(pixels[y*280+x]==((x>=96&&x<112&&y>=80&&y<96)?cleared:0x1234));
  return 0;
}
