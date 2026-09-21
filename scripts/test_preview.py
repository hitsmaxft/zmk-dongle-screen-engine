#!/usr/bin/env python3
"""Engine-level deterministic replay: compare native and WASM RGB565 frames."""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess

class Rect(C.Structure):
    _fields_=[('x',C.c_int16),('y',C.c_int16),('width',C.c_uint16),('height',C.c_uint16)]
class Frame(C.Structure):
    _fields_=[('abi_version',C.c_uint16),('struct_size',C.c_uint16),('flags',C.c_uint32),('next_frame_at_ms',C.c_uint32),('dirty_count',C.c_uint16),('reserved0',C.c_uint16),('dirty',Rect*18),('reserved',C.c_uint32*4)]
class Canvas(C.Structure):
    _fields_=[('abi_version',C.c_uint16),('struct_size',C.c_uint16),('scene_width',C.c_uint16),('scene_height',C.c_uint16),('origin_x',C.c_uint16),('origin_y',C.c_uint16),('width',C.c_uint16),('height',C.c_uint16),('stride_pixels',C.c_uint16),('pixel_format',C.c_uint8),('reserved0',C.c_uint8),('buffer_size',C.c_uint32),('pixels',C.POINTER(C.c_uint16)),('reserved',C.c_uint32*4)]
class Snapshot(C.Structure):
    _fields_=[('abi_version',C.c_uint16),('struct_size',C.c_uint16),('reserved0',C.c_uint32),('valid_mask',C.c_uint64),('battery_count',C.c_int32),('wpm',C.c_int32),('layer',C.c_int32),('endpoint',C.c_int32),('profile',C.c_int32),('modifiers',C.c_int32),('startup_phase',C.c_int32),('backlight',C.c_int32),('refresh_rate_x10',C.c_int32),('battery_left',C.c_int32),('battery_right',C.c_int32),('battery_dongle',C.c_int32),('connected_left',C.c_int32),('connected_right',C.c_int32),('layer_name',C.c_char*24),('reserved',C.c_uint32*4)]

def verify_abi(api,wasm):
    api.dte_active_abi_version.restype=C.c_uint32
    api.dte_frame.argtypes=[C.c_uint32,C.POINTER(Frame)]
    api.dte_draw.argtypes=[C.c_uint32,C.POINTER(Canvas)]
    api.dte_set_snapshot.argtypes=[C.POINTER(Snapshot)]
    api.dte_init(280,240);assert api.dte_active_abi_version()==0x0103
    pixels=(C.c_uint16*(280*240))()
    canvas=Canvas(0x0103,C.sizeof(Canvas),280,240,0,0,280,240,280,1,0,C.sizeof(pixels),pixels)
    assert api.dte_draw(777,C.byref(canvas))==-8
    snapshot_bytes=C.create_string_buffer(100)
    snapshot=C.cast(snapshot_bytes,C.POINTER(Snapshot))
    snapshot.contents.abi_version=0x0103;snapshot.contents.struct_size=96
    snapshot.contents.valid_mask=2;snapshot.contents.wpm=123
    C.c_uint32.from_buffer(snapshot_bytes,96).value=0xa5a55a5a
    assert api.dte_set_snapshot(snapshot)==0
    snapshot_canary=C.c_uint32.from_buffer(snapshot_bytes,96).value==0xa5a55a5a
    short=Frame(0x0103,4);assert api.dte_frame(777,C.byref(short))==-3
    wrong=Frame(0x0104,C.sizeof(Frame));wrong_abi=api.dte_frame(777,C.byref(wrong));assert wrong_abi==-2
    frame_bytes=C.create_string_buffer(164);frame=C.cast(frame_bytes,C.POINTER(Frame))
    frame.contents.abi_version=0x0103;frame.contents.struct_size=160
    C.c_uint32.from_buffer(frame_bytes,160).value=0x5aa5a55a
    assert api.dte_frame(777,frame)==0
    frame_canary=C.c_uint32.from_buffer(frame_bytes,160).value==0x5aa5a55a
    frame=frame.contents
    assert frame.dirty_count>0 and frame.dirty_count<=18
    bounds=all(r.width and r.height and r.x>=0 and r.y>=0 and r.x+r.width<=280 and r.y+r.height<=240 for r in frame.dirty[:frame.dirty_count]);assert bounds
    rect=frame.dirty[0];canvas.origin_x=rect.x;canvas.origin_y=rect.y
    canvas.width=rect.width;canvas.height=rect.height;canvas.stride_pixels=rect.width
    canvas.buffer_size=rect.width*rect.height*2
    assert api.dte_draw(777,C.byref(canvas))==0
    wrong_time=api.dte_draw(778,C.byref(canvas));assert wrong_time==-8
    canvas.buffer_size=2;assert api.dte_draw(777,C.byref(canvas))==-9
    js="""const fs=require('fs');(async()=>{const {instance}=await WebAssembly.instantiate(fs.readFileSync(process.argv[1]));const e=instance.exports,m=e.memory;e.dte_init(280,240);const old=m.buffer.byteLength;m.grow(3);const v=new DataView(m.buffer),sp=old,fp=old+128,cp=old+320,pix=old+512;new Uint8Array(m.buffer,old,196608).fill(0);v.setUint16(cp,0x0103,true);v.setUint16(cp+2,28,true);for(const [o,n] of [[4,280],[6,240],[8,0],[10,0],[12,280],[14,240],[16,280]])v.setUint16(cp+o,n,true);v.setUint8(cp+18,1);v.setUint32(cp+20,134400,true);v.setUint32(cp+24,pix,true);const before=e.dte_draw(777,cp);v.setUint16(sp,0x0103,true);v.setUint16(sp+2,96,true);v.setUint32(sp+8,2,true);v.setInt32(sp+20,123,true);v.setUint32(sp+96,0xa5a55a5a,true);const snapshot=e.dte_set_snapshot(sp),snapshotCanary=v.getUint32(sp+96,true)===0xa5a55a5a;v.setUint16(fp,0x0103,true);v.setUint16(fp+2,4,true);const short=e.dte_frame(777,fp);v.setUint16(fp,0x0104,true);v.setUint16(fp+2,160,true);const wrongAbi=e.dte_frame(777,fp);v.setUint16(fp,0x0103,true);v.setUint16(fp+2,160,true);v.setUint32(fp+160,0x5aa5a55a,true);const frame=e.dte_frame(777,fp),frameCanary=v.getUint32(fp+160,true)===0x5aa5a55a,count=v.getUint16(fp+12,true);let bounds=count>0&&count<=18;for(let i=0;i<count;i++){const o=fp+16+i*8,x=v.getInt16(o,true),y=v.getInt16(o+2,true),w=v.getUint16(o+4,true),h=v.getUint16(o+6,true);bounds=bounds&&w>0&&h>0&&x>=0&&y>=0&&x+w<=280&&y+h<=240;}const x=v.getInt16(fp+16,true),y=v.getInt16(fp+18,true),w=v.getUint16(fp+20,true),h=v.getUint16(fp+22,true);for(const [o,n] of [[8,x],[10,y],[12,w],[14,h],[16,w]])v.setUint16(cp+o,n,true);v.setUint32(cp+20,w*h*2,true);const draw=e.dte_draw(777,cp),wrongTime=e.dte_draw(778,cp);v.setUint32(cp+20,2,true);const small=e.dte_draw(777,cp);process.stdout.write(JSON.stringify({abi:e.dte_active_abi_version(),before,snapshot,snapshot_canary:snapshotCanary,short,wrong_abi:wrongAbi,frame,count,frame_canary:frameCanary,bounds,draw,wrong_time:wrongTime,small}));})().catch(e=>{console.error(e);process.exit(1)});"""
    result=json.loads(subprocess.check_output(['node','-e',js,str(wasm)]))
    expected={'abi':0x0103,'before':-8,'snapshot':0,'snapshot_canary':snapshot_canary,'short':-3,'wrong_abi':wrong_abi,'frame':0,'count':frame.dirty_count,'frame_canary':frame_canary,'bounds':bounds,'draw':0,'wrong_time':wrong_time,'small':-9}
    assert snapshot_canary and frame_canary and result==expected,(result,expected)
    return result

def verify(output,api_only=False):
    manifest=json.loads((output/'preview-manifest.json').read_text())
    api=C.CDLL(str((output/manifest['native_library']).resolve()));api.dte_hash.restype=C.c_uint32
    api.dte_touch_active.restype=C.c_int
    abi_probe=verify_abi(api,output/'theme.wasm')
    if api_only:
        result={'abi_1_3_native_wasm':abi_probe}
        (output/'api-test-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result));return
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
        calls += [['dte_preview_set_filter',[1]],['dte_render',[5300]],['dte_render',[5400]],['dte_preview_set_filter',[0]],['dte_render',[5500]]]
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
        else:api.dte_touch_cancel()
        api.dte_render(1700);return api.dte_hash()
    assert hold(True)==hold(False)
    api.dte_init(280,240);assert api.dte_touch_active()==0
    api.dte_touch(100,100,1,0);assert api.dte_touch_active()==1
    api.dte_touch(100,100,0,1);assert api.dte_touch_active()==0
    # Stationary rendered output must not flicker as a result of ordered dithering.
    api.dte_init(280,240);api.dte_render(1000);first=api.dte_hash()
    api.dte_init(280,240);api.dte_render(1000);assert first==api.dte_hash()
    static=not manifest.get('continuous_animation',False)
    if static:
        api.dte_render(3000);assert first==api.dte_hash()
    result={'abi_1_3_native_wasm':abi_probe,'native_wasm_equal_frames':len(actual),'viewports':3,'long_press_no_tap':True,'touch_active_lifecycle':True,'deterministic_repeat':True,'static_dither_no_shimmer':static}
    (output/'test-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--api-only',action='store_true');p.add_argument('output',type=Path);a=p.parse_args();verify(a.output,a.api_only)
