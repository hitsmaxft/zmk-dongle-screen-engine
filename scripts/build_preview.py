#!/usr/bin/env python3
"""Shared engine preview builder. Compiles the same theme/core into WASM and a native library."""
import argparse
import base64
import html
import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys

def build(lvgl, theme, output, variant=None):
    engine=Path(__file__).resolve().parents[1];theme=theme.resolve();output=output.resolve()
    if (output/'index.html').exists():
        raise FileExistsError(f'Preview already exists: {output}')
    manifest=json.loads((theme/'preview.json').read_text())
    output.mkdir(parents=True,exist_ok=True)
    subprocess.run([sys.executable,str(engine/'scripts/generate_raster_assets.py'),'--lvgl',str(lvgl.resolve()),'--output',str(output/'dongle_raster_assets.h')],check=True)
    variant=variant or manifest.get('default_variant')
    theme_sources=manifest['variants'][variant] if 'variants' in manifest else manifest['sources']
    sources=[engine/'src/engine.c',engine/'src/raster.c']+[theme/s for s in theme_sources]
    common=['-O2','-fno-builtin','-ffp-contract=off','-I',str(engine/'include'),'-I',str(theme/'include'),'-I',str(output),*[str(s) for s in sources]]
    exports=['dte_init','dte_name_buffer','dte_set_state','dte_set_battery_count','dte_set_display_stats','dte_set_startup_phase','dte_set_layer_name','dte_gesture','dte_gesture_x','dte_gesture_y','dte_backlight_get','dte_backlight_adjust','dte_touch','dte_touch_hint','dte_touch_cancel','dte_render','dte_pixels','dte_width','dte_height','dte_hash','dte_animation_options','dte_set_animation','dte_get_animation','dte_set_animation_duration','dte_get_animation_duration','dte_force_redraw','dtr_dirty_tiles']
    clang=shutil.which('clang')
    if not clang or not shutil.which('wasm-ld'):
        raise RuntimeError('Clang with wasm-ld is required; enter a toolchain environment that provides both')
    env=os.environ.copy();env['NIX_HARDENING_ENABLE']='';env['NIX_LDFLAGS']=''
    subprocess.run([clang,'--target=wasm32','-nostdlib','-Wl,--no-entry','-Wl,--export-memory',*[f'-Wl,--export={e}' for e in exports],*common,'-o',str(output/'theme.wasm')],check=True,env=env)
    native_name='theme.dylib' if platform.system()=='Darwin' else 'theme.so'
    native_flags=['-dynamiclib'] if platform.system()=='Darwin' else ['-shared','-fPIC']
    subprocess.run([clang,*native_flags,*common,'-o',str(output/native_name)],check=True,env=env)
    template=(engine/'web/index.html').read_text()
    if manifest.get('pixel_art'):
        template=template.replace('</style>','canvas{image-rendering:pixelated}.screen{width:294px;border-radius:0}.device-stage{border-radius:0}</style>')
    i18n=(engine/'web/i18n.js').read_text()
    hardware=(engine/'web/hardware.js').read_text()
    js=(engine/'web/preview.js').read_text()
    title=html.escape(manifest.get('display_name',manifest['id']))
    standalone=template.replace('/*__THEME_NAME__*/',title).replace('/*__WASM__*/',base64.b64encode((output/'theme.wasm').read_bytes()).decode()).replace('/*__I18N_JS__*/',i18n).replace('/*__HARDWARE_JS__*/',hardware).replace('/*__PREVIEW_JS__*/',js)
    (output/'index.html').write_text(standalone)
    (output/'preview-manifest.json').write_text(json.dumps({'theme':manifest['id'],'variant':variant,'fps':60,'continuous_animation':bool(manifest.get('continuous_animation',False)),'hardware_simulation':{'profiles':['unlimited','nrf52840','custom'],'default':'nrf52840','estimate_only':True},'native_library':native_name,'shared_sources':[str(s) for s in sources],'render':'same C RGB565 rasterizer on native, WASM and firmware'},indent=2))
    print(output/'index.html')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--lvgl',type=Path,required=True,help='Path to an LVGL source tree');p.add_argument('--theme',type=Path,required=True);p.add_argument('--variant');p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.lvgl,a.theme,a.output,a.variant)
