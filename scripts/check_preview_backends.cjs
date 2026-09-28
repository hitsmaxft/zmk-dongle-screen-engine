#!/usr/bin/env node
const assert=require('assert');
const fs=require('fs');

async function run(bytes,backend){
  const {instance}=await WebAssembly.instantiate(bytes);
  const e=instance.exports;
  const width=Number(process.argv[3]||280),height=Number(process.argv[4]||240);
  assert.strictEqual(e.dte_init_ex(width,height),0);
  if(width===480)e.dte_preview_set_strip_pixels(7680);
  const contract=e.dte_theme_render_type();
  const selected=e.dte_preview_set_backend(backend);
  if(contract===1&&backend!==1){
    assert(selected<0);
    return {backend,contract,supported:false};
  }
  assert.strictEqual(selected,backend);
  e.dte_render(0);
  e.dte_render(1000);
  return {backend,contract,supported:true,hash:e.dte_hash()>>>0,
    draws:e.dte_preview_draw_calls(),writes:e.dte_preview_write_calls(),
    bytes:e.dte_preview_transfer_bytes()};
}

(async()=>{
  const bytes=fs.readFileSync(process.argv[2]);
  const results=[];
  for(const backend of [1,2,3])results.push(await run(bytes,backend));
  const supported=results.filter(x=>x.supported);
  assert(supported.length>0);
  assert.strictEqual(new Set(supported.map(x=>x.hash)).size,1,
    `render backends produced different RGB565 framebuffers: ${JSON.stringify(results)}`);
  process.stdout.write(JSON.stringify({backend_framebuffer_parity:results})+'\n');
})().catch(error=>{console.error(error);process.exit(1);});
