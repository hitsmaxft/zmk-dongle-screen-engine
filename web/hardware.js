/* Preview-only hardware budget model. It never mutates theme state. */
(()=>{
  const profiles={
    unlimited:{fps:60,spiMHz:32,cpuScale:1,transport:'full',unlimited:true},
    nrf52840:{fps:60,spiMHz:32,cpuScale:260,transport:'bands',unlimited:false},
    custom:{fps:60,spiMHz:16,cpuScale:80,transport:'bands',unlimited:false},
  };
  function dirtyStats(rows,width,height,mode){
    const tileRows=Math.ceil(height/16),tileCols=Math.ceil(width/16);
    let tiles=0,any=false;
    for(let y=0;y<tileRows;y++)for(let x=0;x<tileCols;x++)if(rows[y]&(1<<x)){tiles++;any=true;}
    if(!any)return {tiles:0,bytes:0,rects:0};
    if(mode==='full')return {tiles,bytes:width*height*2,rects:1};
    if(mode==='bands'){
      let cursor=0,bytes=0,rects=0;
      while(cursor<tileRows){
        while(cursor<tileRows&&!rows[cursor])cursor++;
        if(cursor>=tileRows)break;
        const first=cursor;while(cursor<tileRows&&rows[cursor]&&cursor-first<4)cursor++;
        bytes+=width*(Math.min(height,cursor*16)-first*16)*2;rects++;
      }
      return {tiles,bytes,rects};
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
    return {tiles,bytes:pixels*2,rects};
  }
  function estimate(wasmMs,profile,transfer){
    if(profile.unlimited)return {renderMs:wasmMs,transferMs:0,totalMs:wasmMs};
    const renderMs=wasmMs*profile.cpuScale;
    const transferMs=transfer.bytes*8/(profile.spiMHz*1000)+transfer.rects*.04;
    return {renderMs,transferMs,totalMs:renderMs+transferMs};
  }
  window.dteHardwareModel={profiles,dirtyStats,estimate};
})();
