/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct dte_dirty_rect {int x,y,width,height;};
/* Consume non-overlapping tile rectangles, fitting 2048 RGB565 pixels / 64 rows. */
static inline bool dte_next_dirty_rect(uint32_t *rows,int width,int height,
                                       struct dte_dirty_rect *rect) {
  int nrows=(height+15)/16,ncols=(width+15)/16,y=0;
  while(y<nrows&&!rows[y])y++;
  if(y==nrows)return false;
  int x=0;while(x<ncols&&!(rows[y]&(1u<<x)))x++;
  if(x==ncols){rows[y]=0;return dte_next_dirty_rect(rows,width,height,rect);}
  int end=x;while(end<ncols&&end-x<8&&(rows[y]&(1u<<end)))end++;
  uint32_t mask=((1u<<end)-1)^((1u<<x)-1);
  int right=end*16;if(right>width)right=width;
  int w=right-x*16,last=y+1;
  while(last<nrows&&last-y<4&&(rows[last]&mask)==mask&&w*((last-y+1)*16)<=2048)last++;
  for(int row=y;row<last;row++)rows[row]&=~mask;
  int bottom=last*16;if(bottom>height)bottom=height;
  *rect=(struct dte_dirty_rect){x*16,y*16,w,bottom-y*16};
  return true;
}
/* At 280px RGB565, 64 rows = 35840 bytes: even pixel boundary and below
 * nRF EasyDMA's 65535-byte transfer limit. Each band gets a fresh LCD window. */
static inline bool dte_next_dirty_band(const bool *rows, int height, int *cursor,
                                       int *y, int *count) {
  int bands=(height+15)/16;
  while(*cursor<bands&&!rows[*cursor])(*cursor)++;
  if(*cursor>=bands)return false;
  int first=*cursor;
  while(*cursor<bands&&rows[*cursor]&&*cursor-first<4)(*cursor)++;
  *y=first*16;
  int end=*cursor*16;if(end>height)end=height;
  *count=end-*y;
  return true;
}
