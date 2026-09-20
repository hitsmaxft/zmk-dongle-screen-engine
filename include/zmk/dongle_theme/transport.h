/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct dte_dirty_rect {int x,y,width,height;};

/* Convert 16px damage rows into at most capacity scene rectangles. Unlike the
 * legacy packet iterator below, rectangles are not constrained by a transport
 * scratch size: the ABI 1.3 host subdivides them into strips later. If the
 * exact one-row runs do not fit, progressively group adjacent tile rows and
 * take their union. This trades a bounded amount of conservative redraw for
 * never collapsing an annulus or sparse HUD update to the whole scene. */
static inline int dte_collect_dirty_rects(const uint32_t *rows, int width,
                                          int height,
                                          struct dte_dirty_rect *rects,
                                          int capacity) {
  if (!rows || !rects || width <= 0 || height <= 0 || capacity <= 0)
    return -1;
  int nrows = (height + 15) / 16, ncols = (width + 15) / 16;
  uint32_t valid = ncols == 32 ? UINT32_MAX : (1u << ncols) - 1u;
  int any = 0;
  for (int y = 0; y < nrows; y++)
    any |= (rows[y] & valid) != 0;
  if (!any)
    return 0;
  for (int band_rows = 1; band_rows <= nrows; band_rows++) {
    int count = 0, overflow = 0;
    for (int first_y = 0; first_y < nrows && !overflow;
         first_y += band_rows) {
      int last_y = first_y + band_rows;
      if (last_y > nrows)
        last_y = nrows;
      uint32_t mask = 0;
      for (int y = first_y; y < last_y; y++)
        mask |= rows[y];
      mask &= valid;
      for (int x = 0; x < ncols && !overflow;) {
        while (x < ncols && !(mask & (1u << x)))
          x++;
        if (x == ncols)
          break;
        int first_x = x;
        while (x < ncols && (mask & (1u << x)))
          x++;
        int right = x * 16, bottom = last_y * 16;
        if (right > width)
          right = width;
        if (bottom > height)
          bottom = height;
        struct dte_dirty_rect next = {first_x * 16, first_y * 16,
                                      right - first_x * 16,
                                      bottom - first_y * 16};
        int merged = 0;
        for (int i = 0; i < count; i++)
          if (rects[i].x == next.x && rects[i].width == next.width &&
              rects[i].y + rects[i].height == next.y) {
            rects[i].height += next.height;
            merged = 1;
            break;
          }
        if (!merged) {
          if (count == capacity)
            overflow = 1;
          else
            rects[count++] = next;
        }
      }
    }
    if (!overflow)
      return count;
  }
  return -1;
}

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
