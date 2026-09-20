/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>
#include <zmk/dongle_theme/transport.h>
int main(void) {
  for(int h=240;h<=280;h+=40) {
    bool rows[18]={0};for(int i=0;i<(h+15)/16;i++)rows[i]=true;
    int cursor=0,y,n,expected=0;
    while(dte_next_dirty_band(rows,h,&cursor,&y,&n)) {
      assert(y==expected&&n>0&&n<=64&&n*280*2<65535);
      expected+=n;
    }
    assert(expected==h);
    for(int i=0;i<18;i++)rows[i]=false;
    rows[2]=rows[3]=rows[(h+15)/16-1]=true;cursor=0;
    assert(dte_next_dirty_band(rows,h,&cursor,&y,&n)&&y==32&&n==32);
    assert(dte_next_dirty_band(rows,h,&cursor,&y,&n)&&y==((h+15)/16-1)*16&&y+n==h);
    assert(!dte_next_dirty_band(rows,h,&cursor,&y,&n));
  }
  uint32_t seed=941;
  for(int test=0;test<300;test++){
    int w=test%2?280:240,h=test%3?240:280;
    uint32_t dirty[18]={0},original[18]={0};unsigned char covered[280*280]={0};
    for(int row=0;row<(h+15)/16;row++){
      seed=seed*1664525u+1013904223u;dirty[row]=original[row]=seed&((1u<<((w+15)/16))-1);
    }
    struct dte_dirty_rect rect;
    while(dte_next_dirty_rect(dirty,w,h,&rect)){
      assert(rect.x>=0&&rect.y>=0&&rect.x+rect.width<=w&&rect.y+rect.height<=h);
      assert(rect.width*rect.height<=2048&&rect.height<=64);
      for(int y=rect.y;y<rect.y+rect.height;y++)for(int x=rect.x;x<rect.x+rect.width;x++){
        assert(original[y/16]&(1u<<(x/16)));assert(!covered[y*w+x]);covered[y*w+x]=1;
      }
    }
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)assert(covered[y*w+x]==!!(original[y/16]&(1u<<(x/16))));

    memset(covered,0,sizeof(covered));
    struct dte_dirty_rect packed[18];
    int count=dte_collect_dirty_rects(original,w,h,packed,18);
    assert(count>=0&&count<=18);
    for(int i=0;i<count;i++){
      rect=packed[i];
      assert(rect.x>=0&&rect.y>=0&&rect.x+rect.width<=w&&rect.y+rect.height<=h);
      for(int y=rect.y;y<rect.y+rect.height;y++)for(int x=rect.x;x<rect.x+rect.width;x++){
        assert(!covered[y*w+x]);covered[y*w+x]=1;
      }
    }
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)
      if(original[y/16]&(1u<<(x/16)))assert(covered[y*w+x]);
  }

  uint32_t column[18]={0};
  for(int y=0;y<15;y++)column[y]=0x3fu;
  struct dte_dirty_rect packed[18];
  int count=dte_collect_dirty_rects(column,280,240,packed,18);
  assert(count==1&&packed[0].x==0&&packed[0].y==0&&
         packed[0].width==96&&packed[0].height==240);

  uint32_t ring[18]={0};
  for(int y=0;y<15;y++){
    int inset=y<8?7-y:y-7;
    ring[y]=(1u<<inset)|(1u<<(17-inset));
  }
  count=dte_collect_dirty_rects(ring,280,240,packed,18);
  assert(count>0&&count<=18);
  int area=0;for(int i=0;i<count;i++)area+=packed[i].width*packed[i].height;
  assert(area<280*240);
  return 0;
}
