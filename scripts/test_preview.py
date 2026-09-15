#!/usr/bin/env python3
"""Engine-level deterministic replay: compare native and WASM RGB565 frames."""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess

def verify(output):
    manifest=json.loads((output/'preview-manifest.json').read_text())
    api=C.CDLL(str((output/manifest['native_library']).resolve()));api.dte_hash.restype=C.c_uint32
    calls=[]
    for w,h in ((280,240),(240,280),(240,240)):
        calls += [['dte_init',[w,h]],['dte_set_state',[72,0,1,1,0,87,64,93,1,1]],['dte_render',[0]]]
        calls += [['dte_gesture',[2,100]]]+[['dte_render',[t]] for t in (100,116,180,250,330)]
        calls += [['dte_gesture',[3,330]]]+[['dte_render',[t]] for t in (330,400,600,830)]
        calls += [['dte_set_state',[128,3,2,1,5,8,64,93,1,0]]]+[['dte_render',[t]] for t in (900,980,1040,1080,1160,1260,1300)]
        calls += [['dte_gesture',[2,1400]],['dte_render',[1900]],['dte_gesture',[4,1900]],['dte_render',[2050]],['dte_render',[2200]]]
        calls += [['dte_set_state',[999,0,0,0,255,-1,-1,-1,-1,-1]],['dte_render',[2300]]]
        calls += [['dte_touch',[100,100,1,2400]],['dte_render',[2999]],['dte_render',[3000]],['dte_touch',[100,100,0,3010]],['dte_render',[3600]]]
        calls += [['dte_touch',[150,100,1,3700]],['dte_touch',[100,100,1,3750]],['dte_touch',[80,100,0,3800]],['dte_render',[4400]]]
        calls += [['dte_set_battery_count',[2]],['dte_gesture',[3,4500]],['dte_render',[5100]],['dte_set_battery_count',[3]],['dte_render',[5200]]]
    expected=[]
    for name,args in calls:
        getattr(api,name)(*args)
        if name=='dte_render':expected.append(api.dte_hash())
    js="""const fs=require('fs'); (async()=>{const input=JSON.parse(fs.readFileSync(0,'utf8'));const {instance}=await WebAssembly.instantiate(fs.readFileSync(input.wasm));const e=instance.exports,out=[];for(const [name,args] of input.calls){e[name](...args);if(name==='dte_render')out.push(e.dte_hash()>>>0);}process.stdout.write(JSON.stringify(out));})().catch(e=>{console.error(e);process.exit(1)});"""
    actual=json.loads(subprocess.check_output(['node','-e',js],input=json.dumps({'wasm':str(output/'theme.wasm'),'calls':calls}).encode()))
    assert actual==expected,[(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:8]
    # Release after long press must not also trigger tap (and re-enable Sport).
    def hold(release):
        api.dte_init(280,240);api.dte_set_state(72,0,1,0,0,87,64,93,1,1);api.dte_gesture(1,0);api.dte_render(500)
        api.dte_touch(100,100,1,600);api.dte_render(1200)
        if release:api.dte_touch(100,100,0,1210)
        api.dte_render(1700);return api.dte_hash()
    assert hold(True)==hold(False)
    # Stationary rendered output must not flicker as a result of ordered dithering.
    api.dte_init(280,240);api.dte_render(1000);first=api.dte_hash();api.dte_render(3000);assert first==api.dte_hash()
    result={'native_wasm_equal_frames':len(actual),'viewports':3,'long_press_no_tap':True,'static_dither_no_shimmer':True}
    (output/'test-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();verify(a.output)
