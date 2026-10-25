import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';import {seed} from './fixtures/decky_game_memory_store.mjs';
export async function wakeChainTests(root,out){
 const native=JSON.parse(fs.readFileSync(path.join(out,'native-wake-commit.json'),'utf8'));assert.equal(native.passed,true);assert.equal(native.productionBlockExtracted,true);assert.equal(native.bindingCalls,2);assert.equal(native.preflights,1);
 const req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),compiled=await build({entryPoints:[path.join(root,'decky-plugin/src/mirrorClient.ts')],bundle:true,write:false,platform:'node',format:'esm'}),{MirrorClient}=await import('data:text/javascript;base64,'+Buffer.from(compiled.outputFiles[0].contents).toString('base64'));
 const source=fs.readFileSync(path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8'),flush=async()=>{for(let i=0;i<260;i++)await Promise.resolve();};
 let checks=0;const check=(value,name)=>{checks++;assert(value,name);};
 async function exercise(hostSource=source){
  let connectionSequence=0,current=null,client=null;const sockets=[],timers=new Map;let timerId=0;
  let bootstrap={endpoint:'ws://127.0.0.1:18223/mirror',token:'a'.repeat(64),runId:'b'.repeat(32)};
  const f=await createMirrorHostFixture({initial:seed(),hostSource,invoke:async(command,args)=>{
    if(command==='deckySidebar.mirrorAttach')return {connected:false,owner:'YMCC-native',pid:8612};
    if(command==='deckySidebar.mirrorReply'){
      if(current&&!current.closed&&args.connection===current.connection&&args.message.runId===current.runId)current.receive('message',args.message);return true;
    }
    if(command==='input.shortcutRuntime.clear')return true;throw Error('Unexpected wake-chain IO '+command);
  }});
  f.state.power={...f.state.power,generation:176,phase:'ready',hardwareWritesAllowed:true};
  class Socket{
    handlers=new Map;sent=[];closed=false;connection=String(++connectionSequence);runId=bootstrap.runId;
    constructor(url){this.url=url;sockets.push(this);current=this;}
    addEventListener(name,fn){const list=this.handlers.get(name)||[];list.push(fn);this.handlers.set(name,list);}
    receive(name,value){for(const fn of this.handlers.get(name)||[])fn({data:JSON.stringify(value)});}
    open(){f.emit('deckySidebar.mirrorPeer',{connection:this.connection,runId:this.runId,connected:true,revision:0});this.receive('open');}
    send(text){const request=JSON.parse(text);this.sent.push(request);if(request.command!=='snapshot')return;f.emit('deckySidebar.mirrorRequest',{connection:this.connection,runId:this.runId,request});}
    close(){if(this.closed)return;this.closed=true;f.emit('deckySidebar.mirrorPeer',{connection:this.connection,runId:this.runId,connected:false,revision:0});this.receive('close');}
  }
  client=new MirrorClient({bootstrap:()=>bootstrap,socket:url=>new Socket(url),schedule:(fn,delay)=>{const id=++timerId;timers.set(id,{fn,delay});return id;},cancel:id=>timers.delete(id),clientId:'wake-chain',buildId:'c'.repeat(64)});
  try{
   f.ready();await flush();client.setVisible(true);sockets[0].open();await flush();
   check(client.snapshot().connected&&client.snapshot().snapshot?.ready===true,'initial production Host and Client reach ready snapshot');
   const pending=client.mutate('performance.setControls',{patch:{tdp:25}}).then(()=>null,e=>e);await flush();
   check(sockets[0].sent.filter(m=>m.command==='performance.setControls').length===1,'one uncertain pre-wake user intent exists in old transport');
   f.state.power={...f.state.power,generation:177,phase:'suspended',hardwareWritesAllowed:false};f.powerChanged(true);await flush();
   check(client.snapshot().connected&&client.snapshot().snapshot?.ready===false,'native suspended snapshot keeps authenticated transport but closes controls');
   check(/已连接 YMCC.*等待唤醒完成/.test(client.snapshot().snapshot.notice),'actual held notice distinguishes power gate from disconnected transport');
   for(const event of ['power.resume-ready','power.resuming']){f.emit(event,{generation:177});await flush();}
   check(client.snapshot().snapshot.ready===false,'noncommitted resume events cannot open production controls');
   const nativeReady=native.events.find(e=>e.event==='power.resumed'&&e.data.generation===177);assert(nativeReady);
   f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.emit(nativeReady.event,nativeReady.data);await flush();
   check(client.snapshot().snapshot?.ready===true&&client.snapshot().snapshot.generation===177,'native committed event must release the existing held production snapshot');
   check(f.counts.performanceUpdates===undefined&&f.counts.saves===0,'uncertain user write has not executed or replayed while waking');
   const old=sockets[0];bootstrap={...bootstrap,endpoint:'ws://127.0.0.1:18224/mirror',token:'d'.repeat(64),runId:'e'.repeat(32)};client.bootstrapChanged();sockets[1].open();await flush();
   check(old.closed&&await pending instanceof Error,'new native bootstrap fences and rejects the old uncertain request');
   check(sockets[1].sent.every(m=>m.command==='snapshot'),'replacement transport requests only current state');
   check(client.snapshot().connected&&client.snapshot().snapshot.ready&&client.snapshot().snapshot.runId===bootstrap.runId,'production client completes authenticated generation-matched new snapshot');
   for(const event of native.events.slice(1)){
     if(event.data.generation>f.state.power.generation)f.state.power={...f.state.power,generation:event.data.generation};
     f.emit(event.event,event.data);await flush();
   }
   check(client.snapshot().snapshot.generation===178&&client.snapshot().snapshot.ready,'duplicate and old native notifications read current power generation instead of regressing it');
   check(sockets.length===2&&sockets.flatMap(s=>s.sent).filter(m=>m.command==='performance.setControls').length===1,'wake duplicates create no extra socket and never resend uncertain writes');
   check(f.counts.saves===0&&(f.counts.performanceUpdates??0)===0,'full native-to-Host-to-Client wake chain executes no business write');
   check(timers.size===0,'healthy wake completion leaves no client polling or deadline timer');
   return {nativeGeneration:178,authenticated:true,ready:true,userIntentsSent:1,userIntentsReplayed:0,actualWrites:0};
  }finally{client?.dispose();f.stop();}
 }
 const positive=await exercise();const negative=source.replace("stops.push(on('power.resumed',invalidate));",'/* negative: lost committed wake subscription */');assert.notEqual(negative,source);
 let failure='';try{await exercise(negative);}catch(e){failure=e.message;}
 assert.match(failure,/native committed event must release the existing held production snapshot/,'missing production wake event subscription must fail at original held controls');
 const result={passed:true,checks,productionNativeWakeBlock:true,productionHost:true,productionClient:true,committedEventNegativeDetected:true,negativeFailure:failure,positive,actualWindowsSleep:false,actualSteamActions:0,physicalHardwareWrites:0};fs.writeFileSync(path.join(out,'wake-chain.json'),JSON.stringify(result,null,2));return result;
}