#!/usr/bin/env python3
"""Build single-variant or self-contained multi-profile Native/WASM previews."""
import argparse, base64, hashlib, html, json, os, platform, shutil, subprocess, sys
from pathlib import Path

def source_list(manifest, variant):
    if 'variants' not in manifest: return manifest['sources']
    return [*manifest.get('common_sources',[]),*manifest['variants'][variant]]

def compile_variant(engine, lvgl, theme, output, manifest, variant):
    output.mkdir(parents=True,exist_ok=True)
    raster=output/'dongle_raster_assets.h'
    if not raster.exists(): subprocess.run([sys.executable,str(engine/'scripts/generate_raster_assets.py'),'--lvgl',str(lvgl.resolve()),'--output',str(raster)],check=True)
    sources=[engine/'src/engine.c',engine/'src/raster.c',engine/'src/ui.c']+[theme/s for s in source_list(manifest,variant)]
    common=['-O2','-fno-builtin','-ffp-contract=off','-I',str(engine/'include'),'-I',str(theme/'include'),'-I',str(output),*[str(s) for s in sources]]
    exports=['dte_init','dte_init_ex','dte_last_status','dte_active_abi_version','dte_name_buffer','dte_set_state','dte_set_snapshot','dte_set_battery_count','dte_set_display_stats','dte_set_startup_phase','dte_set_layer_name','dte_gesture','dte_gesture_x','dte_gesture_y','dte_backlight_get','dte_backlight_adjust','dte_touch','dte_touch_hint','dte_touch_active','dte_touch_cancel','dte_frame','dte_draw','dte_render','dte_pixels','dte_width','dte_height','dte_hash','dte_animation_options','dte_set_animation','dte_get_animation','dte_set_animation_duration','dte_get_animation_duration','dte_force_redraw','dtr_dirty_tiles',*manifest.get('exports',[]),*manifest.get('variant_exports',{}).get(variant,[])]
    clang=shutil.which('clang')
    if not clang or not shutil.which('wasm-ld'): raise RuntimeError('Clang with wasm-ld is required; enter a toolchain environment that provides both')
    env=os.environ.copy();env['NIX_HARDENING_ENABLE']='';env['NIX_LDFLAGS']=''
    subprocess.run([clang,'--target=wasm32','-nostdlib','-Wl,--no-entry','-Wl,--export-memory',*[f'-Wl,--export={e}' for e in exports],*common,'-o',str(output/'theme.wasm')],check=True,env=env)
    native_name='theme.dylib' if platform.system()=='Darwin' else 'theme.so'; native_flags=['-dynamiclib'] if platform.system()=='Darwin' else ['-shared','-fPIC']
    subprocess.run([clang,*native_flags,*common,'-o',str(output/native_name)],check=True,env=env)
    metadata={'theme':manifest['id'],'variant':variant,'fps':60,'continuous_animation':bool(manifest.get('continuous_animation',False)),'hardware_simulation':{'profiles':['unlimited','nrf52840','custom'],'default':'nrf52840','estimate_only':True},'native_library':native_name,'shared_sources':[str(s) for s in sources],'render':'same C RGB565 rasterizer on native, WASM and firmware'}
    (output/'preview-manifest.json').write_text(json.dumps(metadata,indent=2))
    # Do not emit an apparently usable preview until both binaries have passed
    # the ABI probe. Full deterministic replay remains a separate test gate.
    subprocess.run([sys.executable,str(engine/'scripts/test_preview.py'),'--api-only',str(output)],check=True)
    return metadata

def render_page(engine, theme, output, manifest, variant, bundle=None):
    template=(engine/'web/index.html').read_text()
    if manifest.get('pixel_art'): template=template.replace('</style>','canvas{image-rendering:pixelated}.screen{width:294px;border-radius:0}.device-stage{border-radius:0}</style>')
    titles=manifest.get('variant_display_names',{}); title=html.escape(manifest.get('display_name',manifest['id']) if bundle else titles.get(variant,manifest.get('display_name',manifest['id'])))
    def optional_text(key):
        path=manifest.get(key)
        if isinstance(path,dict): path=path.get(variant)
        return (theme/path).read_text() if path else ''
    wasm=base64.b64encode((output/'theme.wasm').read_bytes()).decode() if (output/'theme.wasm').exists() else bundle['profiles'][bundle['default']]['wasm']
    standalone=template.replace('/*__THEME_NAME__*/',title).replace('/*__WASM__*/',wasm).replace('/*__WASM_PROFILE_BUNDLE__*/',json.dumps(bundle,separators=(',',':')) if bundle else 'null').replace('/*__I18N_JS__*/',(engine/'web/i18n.js').read_text()).replace('/*__HARDWARE_JS__*/',(engine/'web/hardware.js').read_text()).replace('/*__THEME_I18N__*/',optional_text('preview_i18n')).replace('/*__PREVIEW_JS__*/',(engine/'web/preview.js').read_text()).replace('/*__THEME_CONTROLS__*/',optional_text('preview_controls')).replace('/*__THEME_SCRIPT__*/',optional_text('preview_script'))
    (output/'index.html').write_text(standalone)

def build(lvgl,theme,output,variant=None,all_variants=False):
    engine=Path(__file__).resolve().parents[1];theme=theme.resolve();output=output.resolve()
    if (output/'index.html').exists(): raise FileExistsError(f'Preview already exists: {output}')
    manifest=json.loads((theme/'preview.json').read_text()); variant=variant or manifest.get('default_variant')
    if not all_variants:
        compile_variant(engine,lvgl,theme,output,manifest,variant);render_page(engine,theme,output,manifest,variant);print(output/'index.html');return
    variants=manifest.get('profile_variants') or list(manifest.get('variants',{}))
    if not variants: raise ValueError('Multi-profile preview requires manifest variants')
    titles=manifest.get('variant_display_names',{});bundle={'default':variant or variants[0],'profiles':{}}
    output.mkdir(parents=True,exist_ok=True)
    for name in variants:
        target=output/'profiles'/name;compile_variant(engine,lvgl,theme,target,manifest,name)
        subprocess.run([sys.executable,str(engine/'scripts/test_preview.py'),str(target)],check=True)
        data=(target/'theme.wasm').read_bytes();bundle['profiles'][name]={'name':titles.get(name,name),'wasm':base64.b64encode(data).decode(),'sha256':hashlib.sha256(data).hexdigest()}
    render_page(engine,theme,output,manifest,bundle['default'],bundle)
    (output/'preview-manifest.json').write_text(json.dumps({'theme':manifest['id'],'variant':'bundle','default_variant':bundle['default'],'profiles':{k:{x:y for x,y in v.items() if x!='wasm'} for k,v in bundle['profiles'].items()}},indent=2))
    print(output/'index.html')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--lvgl',type=Path,required=True);p.add_argument('--theme',type=Path,required=True);p.add_argument('--variant');p.add_argument('--all-variants',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.lvgl,a.theme,a.output,a.variant,a.all_variants)
