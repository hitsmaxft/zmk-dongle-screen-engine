#!/usr/bin/env python3
"""Shared renderer assets: existing LVGL Montserrat glyphs and a sine lookup table."""
import argparse
import math
from pathlib import Path
import re
import struct

def generate(lvgl, output):
    lines=['/* Generated from LVGL Montserrat font data; retain LVGL MIT and Montserrat OFL notices. */',
           '#pragma once', '#include <stdint.h>',
           'struct dtr_glyph { uint32_t offset; int16_t w,h,advance,ox,oy; };',
           'struct dtr_font { int size; const uint8_t *bits; const struct dtr_glyph *glyph; };']
    sizes=(10,12,14,18,20,28,48)
    for size in sizes:
        source=(lvgl/f'src/font/lv_font_montserrat_{size}.c').read_text()
        assert '.bitmap_format = 0' in source
        section=source.split('glyph_bitmap[] = {',1)[1].split('};',1)[0]
        section=re.sub(r'/\*.*?\*/','',section,flags=re.S)
        bits=bytes(int(n,16) for n in re.findall(r'0x([0-9a-fA-F]+)',section))
        section=source.split('glyph_dsc[] = {',1)[1].split('};',1)[0]
        glyphs=[]
        for row in re.findall(r'\{([^{}]+)\}',section):
            d={k:int(v) for k,v in re.findall(r'\.(\w+)\s*=\s*(-?\d+)',row)}
            if 'bitmap_index' in d:glyphs.append(d)
        packed=bytearray(); entries=[]
        for g in glyphs[1:96]:
            n=(g['box_w']*g['box_h']+1)//2
            entries.append((len(packed),g['box_w'],g['box_h'],(g['adv_w']+8)//16,g['ofs_x'],g['ofs_y']))
            packed.extend(bits[g['bitmap_index']:g['bitmap_index']+n])
        assert len(entries)==95
        lines.append(f'static const uint8_t dtr_bits_{size}[]={{'+','.join(str(n) for n in packed)+'};')
        lines.append(f'static const struct dtr_glyph dtr_glyphs_{size}[]={{'+','.join('{'+','.join(map(str,g))+'}' for g in entries)+'};')
    lines.append('static const struct dtr_font dtr_fonts[]={'+','.join('{'+f'{s},dtr_bits_{s},dtr_glyphs_{s}'+'}' for s in sizes)+'};')
    lines.append(f'#define DTR_FONT_COUNT {len(sizes)}')
    lines.append('static const int16_t dtr_sin[360]={'+','.join(str(round(math.sin(math.radians(n))*32767)) for n in range(360))+'};')
    radii=[struct.unpack('<I',struct.pack('<f',math.sqrt(x*x+y*y)))[0] for y in range(128) for x in range(128)]
    lines.append('static const uint32_t dtr_distance_bits[128*128]={'+','.join(hex(v) for v in radii)+'};')
    # Exact original integer formulas, indexed by intensity and Bayer threshold.
    q5=[min(31,(v*31*16//255+t)//16) for v in range(256) for t in range(16)]
    # Progressive dispersed ranks on a toroidal 16x16 tile. Fixed tie-breaks;
    # no per-frame RNG, no global noise overlay, every full-density pixel kept.
    distance=[10000]*256; rank=[0]*256; remaining=set(range(256)); chosen=117
    for n in range(256):
        rank[chosen]=n; remaining.remove(chosen)
        for p in remaining:
            dx=abs(p%16-chosen%16);dy=abs(p//16-chosen//16)
            distance[p]=min(distance[p],min(dx,16-dx)**2+min(dy,16-dy)**2)
        if remaining:chosen=max(remaining,key=lambda p:(distance[p],((p*1103515245+12345)&0xffffffff)))
    lines.append('static const uint8_t dtr_density_rank[256]={'+','.join(map(str,rank))+'};')
    q6=[min(63,(v*63*16//255+t)//16) for v in range(256) for t in range(16)]
    grey=[(r<<11)|(((r*63+15)//31)<<5)|r for r in q5]
    for name,typ,values in (('dtr_q5','uint8_t',q5),('dtr_q6','uint8_t',q6),('dtr_grey','uint16_t',grey),
                            ('dtr_expand5','uint8_t',[v*255//31 for v in range(32)]),
                            ('dtr_expand6','uint8_t',[v*255//63 for v in range(64)])):
        lines.append(f'static const {typ} {name}[]={{'+','.join(map(str,values))+'};')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('\n'.join(lines)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--lvgl',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();generate(a.lvgl,a.output)
