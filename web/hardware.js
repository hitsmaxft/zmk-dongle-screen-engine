/* Preview-only hardware budget model. It never mutates theme state. */
(()=>{
  const profiles={
    unlimited:{fps:60,spiMHz:32,cpuScale:1,transport:'full',unlimited:true},
    nrf52840:{fps:24,spiMHz:32,cpuScale:260,transport:'abi',stripPixels:4480,sramBytes:256*1024,flashBytes:1024*1024,unlimited:false},
    custom:{fps:24,spiMHz:16,cpuScale:80,transport:'abi',stripPixels:4480,unlimited:false},
  };
  function dirtyStats(rows,width,height,mode){
    const tileRows=Math.ceil(height/16),tileCols=Math.ceil(width/16);
    const fullBytes=width*height*2;
    const result=(tiles,bytes,rects,writes=rects)=>({tiles,bytes,rects,writes,fullBytes,dirtyPercent:fullBytes?bytes*100/fullBytes:0});
    let tiles=0,any=false;
    for(let y=0;y<tileRows;y++)for(let x=0;x<tileCols;x++)if(rows[y]&(1<<x)){tiles++;any=true;}
    if(!any)return result(0,0,0);
    if(mode==='full')return result(tiles,fullBytes,1);
    if(mode==='bands'){
      let cursor=0,bytes=0,rects=0;
      while(cursor<tileRows){
        while(cursor<tileRows&&!rows[cursor])cursor++;
        if(cursor>=tileRows)break;
        const first=cursor;while(cursor<tileRows&&rows[cursor]&&cursor-first<4)cursor++;
        bytes+=width*(Math.min(height,cursor*16)-first*16)*2;rects++;
      }
      return result(tiles,bytes,rects);
    }
    let pixels=0,rects=0;
    for(let y=0;y<tileRows;y++){
      let run=false;
      for(let x=0;x<tileCols;x++){
        const dirty=!!(rows[y]&(1<<x));
        if(dirty){pixels+=(Math.min(width,(x+1)*16)-x*16)*(Math.min(height,(y+1)*16)-y*16);if(!run)rects++;}
        run=dirty;
      }
    }
    return result(tiles,pixels*2,rects);
  }
  function actualStats(rows,width,height,bytes,rects,writes){
    const tileRows=Math.ceil(height/16),tileCols=Math.ceil(width/16);let tiles=0;
    for(let y=0;y<tileRows;y++)for(let x=0;x<tileCols;x++)if(rows[y]&(1<<x))tiles++;
    const fullBytes=width*height*2;
    return {tiles,bytes,rects,writes,fullBytes,dirtyPercent:fullBytes?bytes*100/fullBytes:0};
  }
  function estimate(wasmMs,profile,transfer){
    const frameBudgetMs=1000/profile.fps;
    const bytesPerSecond=transfer.bytes*profile.fps;
    const stripRamBytes=(profile.stripPixels||0)*2;
    if(profile.unlimited)return {renderMs:wasmMs,payloadTransferMs:0,transferMs:0,totalMs:wasmMs,frameBudgetMs,bytesPerSecond,spiUtilization:0,stripRamBytes};
    const renderMs=wasmMs*profile.cpuScale;
    const payloadTransferMs=transfer.bytes*8/(profile.spiMHz*1000);
    const transferMs=payloadTransferMs+transfer.writes*.04;
    return {renderMs,payloadTransferMs,transferMs,totalMs:renderMs+transferMs,frameBudgetMs,bytesPerSecond,spiUtilization:payloadTransferMs*100/frameBudgetMs,stripRamBytes};
  }
  window.dteHardwareModel={profiles,dirtyStats,actualStats,estimate};
})();
