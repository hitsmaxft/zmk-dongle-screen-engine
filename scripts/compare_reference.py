#!/usr/bin/env python3
"""RGB565-aware visual diff for native/WASM theme framebuffers."""
import argparse
import json
from pathlib import Path
from PIL import Image

def rgb565_pixel(color):
    r,g,b=color[:3]
    value=((r*31+127)//255<<11)|((g*63+127)//255<<5)|(b*31+127)//255
    return (round((value>>11)*255/31),round(((value>>5)&63)*255/63),round((value&31)*255/31))

def quantize565(image):
    out=Image.new("RGB",image.size)
    out.putdata([rgb565_pixel(c) for c in image.convert("RGB").get_flattened_data()])
    return out

def is_brightness_spike(image,x,y,brightness_floor=205,spike_delta=96):
    r,g,b=image.getpixel((x,y));lum=max(r,g,b)
    if lum<brightness_floor:return False
    neighbors=[]
    for yy in range(max(0,y-1),min(image.height,y+2)):
      for xx in range(max(0,x-1),min(image.width,x+2)):
        if (xx,yy)!=(x,y):neighbors.append(max(image.getpixel((xx,yy))))
    return neighbors and lum-sorted(neighbors)[len(neighbors)//2]>=spike_delta

def compare(reference,rendered,output,regions=None,white_floor=170,
            brightness_floor=205,spike_delta=96,heat_scale=2):
    rendered=rendered.convert("RGB");size=rendered.size
    reference=quantize565(reference.convert("RGB").resize(size,Image.Resampling.NEAREST))
    regions={"full_frame":(0,0,*size),**(regions or {})}
    heat=Image.new("RGB",size,(0,0,0));report={}
    for name,box in regions.items():
      if not (0<=box[0]<box[2]<=size[0] and 0<=box[1]<box[3]<=size[1]):
        raise ValueError(f"region {name} outside {size}: {box}")
      ref=reference.crop(box);got=rendered.crop(box);changed=0;error=0;white=[];spikes=[]
      for y in range(ref.height):
        for x in range(ref.width):
          a=ref.getpixel((x,y));b=got.getpixel((x,y));delta=max(abs(a[i]-b[i]) for i in range(3))
          error+=sum(abs(a[i]-b[i]) for i in range(3))
          if delta:
            changed+=1
            if name=="full_frame":heat.putpixel((x,y),(min(255,delta*3),0,255-min(255,delta*3)))
          if min(b)>=white_floor and min(a)<white_floor:white.append((box[0]+x,box[1]+y))
          if b!=a and is_brightness_spike(got,x,y,brightness_floor,spike_delta) and not is_brightness_spike(ref,x,y,brightness_floor,spike_delta):
            spikes.append((box[0]+x,box[1]+y))
      pixels=ref.width*ref.height
      report[name]={"box":box,"pixels":pixels,"changed_pixels":changed,
                    "changed_ratio":round(changed/pixels,6),"mean_abs_error":round(error/(pixels*3),4),
                    "unexpected_white_point_count":len(white),"unexpected_white_points":white[:32],
                    "unexpected_brightness_spike_count":len(spikes),"unexpected_brightness_spikes":spikes[:32]}
      if name=="full_frame":
        for x,y in white:heat.putpixel((x,y),(255,255,0))
        for x,y in spikes:
          if heat.getpixel((x,y))!=(255,255,0):heat.putpixel((x,y),(0,255,255))
    output.parent.mkdir(parents=True,exist_ok=True)
    heat.resize((size[0]*heat_scale,size[1]*heat_scale),Image.Resampling.NEAREST).save(output)
    return report

def parse_region(value):
    try:name,coords=value.split(":",1);box=tuple(int(v) for v in coords.split(","))
    except ValueError as e:raise argparse.ArgumentTypeError("expected NAME:X0,Y0,X1,Y1") from e
    if not name or len(box)!=4:raise argparse.ArgumentTypeError("expected NAME:X0,Y0,X1,Y1")
    return name,box

if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--reference",type=Path,required=True);p.add_argument("--rendered",type=Path,required=True);p.add_argument("--output",type=Path,required=True);p.add_argument("--report",type=Path)
    p.add_argument("--region",action="append",type=parse_region,default=[]);p.add_argument("--strict",action="append",default=[]);p.add_argument("--heat-scale",type=int,default=2)
    p.add_argument("--white-floor",type=int,default=170);p.add_argument("--brightness-floor",type=int,default=205);p.add_argument("--spike-delta",type=int,default=96);a=p.parse_args()
    regions=dict(a.region);report=compare(Image.open(a.reference),Image.open(a.rendered),a.output,regions,a.white_floor,a.brightness_floor,a.spike_delta,a.heat_scale)
    payload=json.dumps(report,ensure_ascii=False,indent=2);print(payload)
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(payload+"\n")
    missing=[name for name in a.strict if name not in report]
    if missing:p.error(f"strict regions not defined: {', '.join(missing)}")
    failed=[name for name in a.strict if any(report[name][key] for key in ("changed_pixels","unexpected_white_point_count","unexpected_brightness_spike_count"))]
    if failed:raise SystemExit("visual mismatch: "+", ".join(failed))
