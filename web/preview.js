(async()=>{
  const {instance}=await WebAssembly.instantiate(Uint8Array.from(atob(WASM_BASE64),x=>x.charCodeAt(0)));
  const api=instance.exports,canvas=document.querySelector('#screen'),ctx=canvas.getContext('2d',{alpha:false});
  let time=0,last=null,running=true,demo=true,index=0,mods=0,offline=false,unknown=false,statusMode='auto',lastAction=0;
  const sequence=[[900,0],[2000,2],[3400,4],[4300,3],[5600,-1],[7000,2],[8500,3],[9600,6]];
  api.dte_init(280,240);
  function state(){api.dte_set_battery_count(+$('battery-count').value);api.dte_set_display_stats?.(40,600);api.dte_set_state(+$('wpm').value,+$('layer').value,+$('endpoint').value,1,mods,unknown?-1:+$('split0-battery').value,unknown?-1:+$('split1-battery').value,unknown?-1:+$('dongle-battery').value,1,offline?0:1);setName($('layer').selectedOptions[0].text);}
  function setName(s){const ptr=api.dte_name_buffer();const b=new Uint8Array(api.memory.buffer,ptr,24);b.fill(0);b.set(new TextEncoder().encode(s).slice(0,23));api.dte_set_layer_name(ptr);}
  function $(s){return document.getElementById(s);}
  function draw(){api.dte_render(Math.round(time));const w=api.dte_width(),h=api.dte_height();if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}
    const src=new Uint16Array(api.memory.buffer,api.dte_pixels(),w*h),im=ctx.createImageData(w,h);
    for(let i=0;i<src.length;i++){const p=src[i];im.data[i*4]=(p>>11)*255/31;im.data[i*4+1]=((p>>5)&63)*255/63;im.data[i*4+2]=(p&31)*255/31;im.data[i*4+3]=255;}ctx.putImageData(im,0,0);
    $('trace').textContent=`time ${time.toFixed(1)} ms\nframe ${w}×${h} · RGB565\nhash ${api.dte_hash().toString(16)} · simulation`;
  }
  function localizeDynamic(){const t=window.dteI18n.t;$('play').textContent=t(running?'pause':'continue');$('status').textContent=statusMode==='auto'?t('auto'):t('interactive')+' · '+(statusMode==='gesture'?t('shared_gesture'):t('actions')[lastAction]);}
  function action(g){demo=false;statusMode='action';lastAction=g;api.dte_gesture(g,Math.round(time));draw();localizeDynamic();}
  function tick(t){if(last===null)last=t;if(running){time+=Math.min(t-last,100);if(demo){while(index<sequence.length&&time>=sequence[index][0]){const [at,g]=sequence[index];if(g<=0){$('wpm').value=g===0?128:72;$('wpm-value').value=$('wpm').value;state();api.dte_render(at);}else api.dte_gesture(g,at);index++;}if(time>11000)reset();}draw();}last=t;requestAnimationFrame(tick);}
  function reset(){time=0;index=0;$('wpm').value=72;$('wpm-value').value=72;api.dte_init(...$('resolution').value.split(',').map(Number));state();demo=true;running=true;statusMode='auto';draw();localizeDynamic();}
  $('gauge').onclick=()=>action(3);$('information').onclick=()=>action(2);$('style').onclick=()=>action(1);$('page').onclick=()=>action(4);
  for(const id of ['wpm','layer','endpoint'])$(id).oninput=()=>{$('wpm-value').value=$('wpm').value;state();draw();};
  for(const prefix of ['dongle','split0','split1'])$(prefix+'-battery').oninput=()=>{const v=+$(prefix+'-battery').value;$(prefix+'-value').textContent=v<0?'--':v+'%';state();draw();};
  $('battery-count').onchange=()=>{state();draw();};
  $('mods').onclick=e=>{const bit=+e.target.dataset.mod;if(!bit)return;mods^=bit;e.target.classList.toggle('active');state();draw();};
  $('disconnect').onclick=()=>{offline=!offline;$('disconnect').classList.toggle('active',offline);state();draw();};
  $('unknown').onclick=()=>{unknown=!unknown;$('unknown').classList.toggle('active',unknown);state();draw();};
  $('resolution').onchange=reset;$('play').onclick=()=>{running=!running;localizeDynamic();};
  $('step').onclick=()=>{running=false;time+=1000/60;draw();localizeDynamic();};$('replay').onclick=reset;
  let contact=null,hold=null;
  function pointer(e,down){const r=canvas.getBoundingClientRect();api.dte_touch(Math.round((e.clientX-r.left)*canvas.width/r.width),Math.round((e.clientY-r.top)*canvas.height/r.height),down,Math.round(time));draw();}
  canvas.onpointerdown=e=>{demo=false;canvas.setPointerCapture(e.pointerId);contact={time};pointer(e,1);hold=setTimeout(()=>{if(contact){time=Math.max(time,contact.time+600);draw();}},600);};
  canvas.onpointermove=e=>{if(contact)pointer(e,1);};
  canvas.onpointerup=e=>{clearTimeout(hold);if(!contact)return;pointer(e,0);contact=null;statusMode='gesture';localizeDynamic();};
  canvas.onpointercancel=()=>{clearTimeout(hold);contact=null;api.dte_touch_cancel();};
  document.addEventListener('dte-locale-change',localizeDynamic);
  window.dtePreview={api,drawAt(t){time=t;demo=false;running=false;draw();localizeDynamic();},gesture(g,t){api.dte_gesture(g,t);},reset(w=280,h=240){api.dte_init(w,h);state();},hash:()=>api.dte_hash()>>>0};
  reset();requestAnimationFrame(tick);
})().catch(e=>{document.getElementById('error').textContent=window.dteI18n.t('load_error')+e.message;document.getElementById('status').textContent=window.dteI18n.t('reload');});
