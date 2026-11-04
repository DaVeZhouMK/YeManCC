import {performanceModelModule} from './fixtures/decky_game_memory_store.mjs';
// Production mirror client; inert sockets, virtual clocks and no real processes.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {createRequire} from 'node:module';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const {transformSync}=createRequire(path.join(root,'package.json'))('esbuild');
const contextModule={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module:contextModule,exports:contextModule.exports});
const module={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(process.env.YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,require:name=>{if(name==='./performanceModel')return performanceModelModule;assert.equal(name,'./steamRunningContext');return contextModule.exports;}});
const {MirrorClient}=module.exports;const cases=[];
function fixture(){
 const timers=new Map(),sockets=[];let now=0,next=0;
 let bootstrap={endpoint:'ws://127.0.0.1:23456/mirror',token:'inert-token',runId:'session1',hudProtocol:1};
 class Socket{
  listeners=new Map();sent=[];closed=false;
  constructor(){sockets.push(this);}
  addEventListener(event,fn){const list=this.listeners.get(event)||[];list.push(fn);this.listeners.set(event,list);}
  emit(event,value){for(const fn of this.listeners.get(event)||[])fn({data:typeof value==='string'?value:JSON.stringify(value)});}
  send(value){if(this.throwSend)throw Error('inert send failure');if(this.closed)throw Error('closed inert socket');this.sent.push(JSON.parse(value));}
  close(){if(this.throwClose)throw Error('inert close failure');this.closed=true;this.emit('close');}
 }
 const client=new MirrorClient({bootstrap:()=>bootstrap,socket:()=>new Socket(),schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay,due:now+delay});return id;},cancel:id=>timers.delete(id),clientId:'fault-fixture',now:()=>now});
 const snap=(revision=1)=>({runId:bootstrap.runId,generation:1,revision,ready:true,provenance:{kind:'ymcc-native',pid:123},game:{label:'inert-game',identity:'inert:pid1',fields:{acMode:{value:'balanced',supported:true,choices:[{data:'balanced',label:'Balanced'}]}}},fan:{supported:true,enabled:true,preset:'soft',choices:[{data:'soft',label:'Soft'}]}});
 const publish=(socket=sockets.at(-1),revision=1)=>socket.emit('message',{type:'snapshot',runId:bootstrap.runId,snapshot:snap(revision)});
 const connect=()=>{client.setVisible(true);sockets.at(-1).emit('open');publish();assert.equal(client.snapshot().connected,true);};
 const advance=ms=>{const end=now+ms;let steps=0;for(;;){let found=null;for(const item of timers)if(item[1].due<=end&&(!found||item[1].due<found[1].due))found=item;if(!found)break;if(++steps>500)throw Error('retry storm');now=found[1].due;timers.delete(found[0]);found[1].action();}now=end;};
 const late=ms=>{now+=ms;const ready=[...timers].filter(([,t])=>t.due<=now);for(const[id,t]of ready){if(!timers.has(id))continue;timers.delete(id);t.action();}};
 return{client,sockets,timers,snap,publish,connect,advance,late,setBootstrap:value=>{bootstrap=value;},bootstrap:()=>bootstrap};
}
async function check(name,fn){await fn();cases.push(name);}
await check('an OPEN HUD socket with a silent business backend times out its requested snapshot and reconnects',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);const old=f.sockets[0];old.emit('open');assert.equal(f.client.snapshot().connected,true);
 f.advance(15000);assert.equal(old.closed,true);assert.equal(f.client.snapshot().connected,false);f.advance(250);assert.equal(f.sockets.length,2);f.sockets[1].emit('open');f.publish();assert.equal(f.client.snapshot().snapshot.ready,true);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('an unconfirmed mutation is never replayed when a silent backend later recovers',async()=>{
 const f=fixture();f.connect();const pending=f.client.mutate('fan.setPreset',{preset:'soft'});const rejected=assert.rejects(pending);f.advance(4000);await rejected;f.advance(15000);assert.equal(f.sockets[0].closed,true);
 f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.client.snapshot().busy,false);assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setPreset').length,1);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('a frozen renderer which later delivers a valid snapshot clears the deadline without replay',async()=>{
 const f=fixture();f.connect();f.client.setVisible(false);f.client.setVisible(true);f.publish();f.late(120000);assert.equal(f.sockets.length,2);assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);f.client.dispose();
 // A cancelled callback may already have been dispatched by the scheduling
 // boundary. Keep the same authenticated socket and protect a NEW probe too.
 const measurements=[];
 for(const boundary of ['connect','snapshot','probe']){
  const x=fixture();let pending;
  try{
   x.client.setPassiveVisible(true);x.client.setVisible(true);const socket=x.sockets[0];
   const connectDeadline=[...x.timers.values()].find(t=>t.delay===10000).action;
   socket.emit('open');const snapshotDeadline=[...x.timers.values()].find(t=>t.delay===15000).action;x.publish();
   const answerProbe=revision=>socket.emit('message',{type:'snapshot',runId:x.bootstrap().runId,probeId:socket.sent.at(-1).probeId,snapshot:x.snap(revision)});
   let stale=boundary==='connect'?connectDeadline:snapshotDeadline;
   if(boundary==='probe'){
    const first=x.client.testConnection();stale=[...x.timers.values()][0].action;
    answerProbe(2);await first;
   }
   pending=x.client.testConnection().then(value=>({value}),error=>({error:String(error)}));
   const before=x.timers.size;assert.equal(before,2);stale();
   const state=x.client.snapshot(),preserved=state.connected&&state.connectionTest.pending&&x.timers.size===before&&!socket.closed;
   measurements.push({boundary,preserved,connected:state.connected,probePending:state.connectionTest.pending,timersBefore:before,timersAfter:x.timers.size});
   if(preserved){answerProbe(3);const result=await pending;assert(result.value);assert.equal(x.timers.size,0);}
   assert(socket.sent.every(message=>message.command==='snapshot'),'deadline recovery tests never send business writes');
  }finally{x.client.dispose();if(pending)await pending;measurements.at(-1).remainingAfterDispose=x.timers.size;}
 }
 console.log(JSON.stringify({sameSessionDeadlineMeasurements:measurements}));
 assert(measurements.every(x=>x.preserved&&x.remainingAfterDispose===0),'retired same-session deadline cannot close a healthy socket or settle a new probe');

});
await check('healthy visible and passive states add no periodic liveness traffic',()=>{
 const f=fixture();f.connect();f.client.setPassiveVisible(true);const sent=f.sockets[0].sent.length;f.advance(3600000);assert.equal(f.sockets[0].sent.length,sent);assert.equal(f.timers.size,0);f.client.setVisible(false);const hidden=f.sockets[0].sent.length;f.advance(3600000);assert.equal(f.sockets[0].sent.length,hidden);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('hide and dispose cancel outstanding state deadlines without closing healthy HUD on hidden demand',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');f.client.setVisible(false);assert.equal(f.timers.size,0);f.advance(60000);assert.equal(f.sockets.length,1);assert.equal(f.sockets[0].closed,false);f.client.dispose();assert.equal(f.timers.size,0);
});
const compoundOrderingMeasurements=[];
await check('stale packets and timeout callbacks cannot damage a renewed mirror session',async()=>{
 const f=fixture();f.connect();const old=f.sockets[0];f.setBootstrap({...f.bootstrap(),token:'new-inert-token',runId:'session2'});f.client.bootstrapChanged();f.sockets[1].emit('open');f.publish();old.emit('message',{type:'snapshot',runId:'session1',snapshot:{...f.snap(),runId:'session1'}});f.advance(60000);assert.equal(f.client.snapshot().snapshot.runId,'session2');assert.equal(f.sockets.length,2);assert.equal(f.timers.size,0);f.client.dispose();
 // Parameterize this original case with event order, rather than treating sleep,
 // reconnect and bootstrap/account replacement as independent successful checks.
 // These are production-client boundary signals, not physical S3/S0 or Steam updates.
 const permutations=items=>items.length?items.flatMap((item,index)=>permutations(items.filter((_,i)=>i!==index)).map(rest=>[item,...rest])):[[]];
 for(const order of permutations(['transport-drop','clock-resume','bootstrap-absent','bootstrap-renewed']))for(const hidden of [false,true])for(const gapMs of [3600000,86400000]){
  const x=fixture(),late=new Set();let result,prior,retired,first;
  try{
   x.setBootstrap({...x.bootstrap(),wallpaperProtocol:1,updaterProtocol:1});
   for(const consumer of ['hud','wallpaper','updater'])x.client.setPassiveDemand(consumer,true);
   x.connect();first=x.sockets[0];retired=x.snap();prior=x.bootstrap();
   const uncertain=x.client.mutate('fan.setPreset',{preset:'soft'}).then(value=>({ok:true,value}),error=>({ok:false,error}));
   if(hidden)x.client.setVisible(false);
   const renewed={...prior,runId:'session2',token:'renewed-inert-token',endpoint:'ws://127.0.0.1:23457/mirror'};
   const capture=()=>{for(const t of x.timers.values())late.add(t.action);};
   capture();
   for(const event of order){
    capture();
    if(event==='transport-drop')x.sockets.at(-1)?.close();
    if(event==='clock-resume')x.late(gapMs);
    if(event==='bootstrap-absent'){x.setBootstrap(undefined);x.client.bootstrapChanged();}
    if(event==='bootstrap-renewed'){x.setBootstrap(renewed);x.client.bootstrapChanged();}
    capture();assert(x.timers.size<=3,'Compound recovery must keep bounded owned deadlines');
   }
   x.setBootstrap(renewed);x.client.bootstrapChanged();
   // Honor the existing queued backoff when the same bootstrap returns.
   // A bootstrap event must not bypass or reset that capped retry.
   if(x.sockets.at(-1)?.closed){assert.equal(x.timers.size,1);x.advance([...x.timers.values()][0].delay);}
   const current=x.sockets.at(-1);assert.notEqual(current,first);assert.equal(current.closed,false,JSON.stringify({order,hidden,gapMs}));current.emit('open');
   if(hidden)x.client.setVisible(true);
   x.publish(current,9);
   assert.equal(x.client.snapshot().snapshot.runId,renewed.runId);assert.equal(x.client.snapshot().snapshot.ready,true);
   assert.equal(x.timers.size,0);
   for(const callback of late)callback();
   first.emit('open');first.emit('message',{type:'snapshot',runId:prior.runId,snapshot:retired});first.close();
   assert.equal(x.client.snapshot().snapshot.runId,renewed.runId);assert.equal(x.client.snapshot().connected,true);assert.equal(x.timers.size,0);
   const rejected=await uncertain;assert.equal(rejected.ok,false,'A lost mutation cannot become an acknowledged success');
   assert.equal(x.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setPreset').length,1,'No unconfirmed write may be replayed');
   const operation=x.client.mutate('fan.setPreset',{preset:'soft'}),request=current.sent.at(-1);
   current.emit('message',{type:'reply',runId:renewed.runId,id:request.id,ok:true,result:{applied:true}});
   assert.equal((await operation).applied,true,'Fresh explicit intent must work after recovery');
   const sent=current.sent.length;x.advance(3600000);assert.equal(current.sent.length,sent,'Healthy recovery adds no polling');assert.equal(x.timers.size,0);
   result={order,hidden,gapMs,recoveredRunId:renewed.runId,lateCallbacks:late.size,unconfirmedReplayed:0,freshOperationAcknowledged:true,idleRequests:0};
   x.client.dispose();const sockets=x.sockets.length;
   for(const callback of late)callback();first.emit('open');current.emit('open');current.emit('close');x.client.bootstrapChanged();x.client.setVisible(true);x.client.setPassiveDemand('wallpaper',true);x.advance(86400000);
   assert.equal(x.sockets.length,sockets,'Disposed client cannot revive');assert.equal(x.timers.size,0);result.resourcesAfterDispose=0;
   compoundOrderingMeasurements.push(result);
  }finally{x.client.dispose();}
 }
 assert.equal(compoundOrderingMeasurements.length,96);
});
await check('repeated HUD-ready signals cannot postpone an existing missing-snapshot deadline',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');f.advance(14000);for(let i=0;i<100;i++)f.sockets[0].emit('message',{type:'hud-ready',runId:'session1'});assert.equal(f.timers.size,1);f.advance(1000);assert.equal(f.sockets[0].closed,true);assert.equal(f.timers.size,1);f.client.dispose();
});
await check('a stale dispatched deadline cannot clear the next session deadline handle',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');const stale=[...f.timers.values()][0].action;f.setBootstrap({...f.bootstrap(),token:'next-token',runId:'session2'});f.client.bootstrapChanged();f.sockets[1].emit('open');stale();f.publish();assert.equal(f.timers.size,0);f.client.dispose();
});
await check('throwing close does not block deadline cleanup or recovery scheduling',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');f.sockets[0].throwClose=true;assert.doesNotThrow(()=>f.advance(15000));assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.timers.size,0);f.client.dispose();
});
await check('mutation send exceptions reconnect without replaying the unconfirmed write',async()=>{
 const f=fixture();f.connect();f.sockets[0].throwSend=true;await assert.rejects(f.client.mutate('fan.setPreset',{preset:'soft'}));assert.equal(f.client.snapshot().connected,false);assert.equal(f.client.snapshot().busy,false);f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.sockets[1].sent.filter(r=>r.command==='fan.setPreset').length,0);assert.equal(f.timers.size,0);f.client.dispose();
});
// Passive consumers share the same authenticated transport and original backoff.
for(const [consumer,command,send] of [
 ['wallpaper','wallpaper.context',(c,d)=>c.sendWallpaper('wallpaper.context',d)],
 ['wallpaper','wallpaper.status',(c,d)=>c.sendWallpaper('wallpaper.status',d)],
 ['hud','hud.status',(c,d)=>c.sendHud('hud.status',d)],
 ['hud','hud.ack',(c,d)=>c.sendHud('hud.ack',d,true)],
 ['updater','updater.status',(c,d)=>c.sendUpdater('updater.status',d)],
 ['updater','updater.request',(c,d)=>c.sendUpdater('updater.request',d)],
])await check(command+' send failure renews one transport without replay under1000 repeated demands',()=>{
 const f=fixture();f.setBootstrap({...f.bootstrap(),wallpaperProtocol:1,updaterProtocol:1});f.client.setPassiveDemand(consumer,true);const old=f.sockets[0];old.emit('open');assert.equal(f.client.snapshot().connected,true);old.throwSend=true;
 try{assert.equal(send(f.client,{contextSeq:1}),false);assert.equal(f.client.snapshot().connected,false,'broken passive transport cannot remain reported connected');assert.equal(old.closed,true);assert.equal(f.timers.size,1);for(let n=0;n<1000;n++)assert.equal(send(f.client,{contextSeq:n+2}),false);assert.equal(f.timers.size,1);assert.equal(f.sockets.length,1);f.advance(249);assert.equal(f.sockets.length,1);f.advance(1);assert.equal(f.sockets.length,2);f.sockets[1].emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);assert.equal(f.sockets[1].sent.length,0,'uncertain passive request must not replay on the new transport');}finally{f.client.dispose();assert.equal(f.timers.size,0);}
});
await check('a local passive serialization failure retains a healthy socket without a recovery storm',()=>{
 const f=fixture();f.setBootstrap({...f.bootstrap(),wallpaperProtocol:1});f.client.setPassiveDemand('wallpaper',true);const socket=f.sockets[0];socket.emit('open');const cyclic={};cyclic.self=cyclic;
 try{assert.equal(f.client.sendWallpaper('wallpaper.status',cyclic),false);assert.equal(f.client.snapshot().connected,true);assert.equal(socket.closed,false);assert.equal(f.timers.size,0);assert.equal(f.client.sendWallpaper('wallpaper.neighbors',{}),false,'unadvertised prefetch stays disabled');assert.equal(socket.sent.length,0);}finally{f.client.dispose();}
});
await check('passive send failure rejects an in-flight business mutation without replay or false acknowledgement',async()=>{
 const f=fixture();f.setBootstrap({...f.bootstrap(),wallpaperProtocol:1});f.client.setPassiveDemand('wallpaper',true);f.connect();const pending=f.client.mutate('fan.setPreset',{preset:'soft'});const rejected=assert.rejects(pending);f.sockets[0].throwSend=true;
 try{assert.equal(f.client.sendWallpaper('wallpaper.status',{}),false);await rejected;assert.equal(f.client.snapshot().busy,false);f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setPreset').length,1);assert.equal(f.client.snapshot().snapshot.ready,true);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
// A recovered connection must age out OLD outage debt through every existing
// transport failure route, while brief connections retain capped backoff.
const healthyRecoveryMeasurements=[];
for(const route of ['close','hud-send','wallpaper-send','updater-send','pause-send','snapshot-send','snapshot-deadline','wallpaper-deadline','mutation-send'])for(const stableMs of [0,30000])await check(route+' uses original stable-health threshold after '+stableMs+'ms',async()=>{
 const f=fixture();let pending;
 try{
  f.setBootstrap({...f.bootstrap(),wallpaperProtocol:1,updaterProtocol:1});
  f.client.setPassiveDemand('wallpaper',true);f.client.setPassiveDemand('hud',true);f.client.setPassiveDemand('updater',true);
  const sidebar=['pause-send','snapshot-send','snapshot-deadline','mutation-send'].includes(route);
  if(sidebar)f.client.setVisible(true);
  for(const delay of [250,1000,3000,10000]){
   f.sockets.at(-1).close();assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].delay,delay);f.advance(delay);
  }
  const socket=f.sockets.at(-1);socket.emit('open');if(sidebar)f.publish();
  assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
  const idleSent=socket.sent.length;f.advance(stableMs);assert.equal(socket.sent.length,idleSent);assert.equal(f.timers.size,0);
  if(route.endsWith('-send'))socket.throwSend=true;
  if(route==='close')socket.close();
  if(route==='hud-send')assert.equal(f.client.sendHud('hud.status',{}),false);
  if(route==='wallpaper-send')assert.equal(f.client.sendWallpaper('wallpaper.status',{contextSeq:1}),false);
  if(route==='updater-send')assert.equal(f.client.sendUpdater('updater.status',{}),false);
  if(route==='pause-send')f.client.setVisible(false);
  if(route==='snapshot-send'||route==='snapshot-deadline')socket.emit('message',{type:'hud-ready',runId:f.bootstrap().runId,protocol:1});
  if(route==='snapshot-deadline')f.advance(15000);
  if(route==='wallpaper-deadline'){assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:1,appId:'480',visible:true}),true);f.advance(10000);}
  if(route==='mutation-send')pending=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,error=>error);
  if(pending)assert.equal(typeof (await pending)?.message,'string');
  assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);
  const actual=[...f.timers.values()][0].delay,expected=stableMs>=30000?250:30000;
  healthyRecoveryMeasurements.push({route,stableMs,retryMs:actual,expectedRetryMs:expected});
  assert.equal(actual,expected,JSON.stringify(healthyRecoveryMeasurements.at(-1)));
  const writes=socket.sent.filter(m=>m.command!=='snapshot'&&!m.command.startsWith('wallpaper.')).length;
  f.advance(actual);const next=f.sockets.at(-1);next.emit('open');if(sidebar&&route!=='pause-send')f.publish();
  assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
  assert.equal(next.sent.some(m=>m.command!=='snapshot'),false,'Recovery must not replay uncertain passive/business payloads');
  assert.equal(socket.sent.filter(m=>m.command!=='snapshot'&&!m.command.startsWith('wallpaper.')).length,writes);
 }finally{f.client.dispose();}
 assert.equal(f.timers.size,0);
});
await check('a passive connection below thirty seconds cannot reset earlier outage backoff',()=>{
 const f=fixture();try{f.setBootstrap({...f.bootstrap(),wallpaperProtocol:1});f.client.setPassiveDemand('wallpaper',true);for(const delay of [250,1000,3000,10000]){f.sockets.at(-1).close();f.advance(delay);}f.sockets.at(-1).emit('open');f.advance(29999);f.sockets.at(-1).throwSend=true;assert.equal(f.client.sendWallpaper('wallpaper.status',{}),false);assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].delay,30000);}finally{f.client.dispose();}
});

await check('the base Decky connection survives hidden QAM with zero business polling and reconnects offline',()=>{
 const f=fixture();try{
  f.client.setPassiveDemand('connection',true);let socket=f.sockets.at(-1);socket.emit('open');
  assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.snapshot().snapshot,null);assert.equal(socket.sent.length,0);assert.equal(f.timers.size,0);
  f.advance(7200000);assert.equal(socket.sent.length,0);assert.equal(f.sockets.length,1);
  f.client.setVisible(true);f.publish();f.client.setVisible(false);assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.snapshot().snapshot,null);
  const reads=socket.sent.filter(m=>m.command==='snapshot'&&m.subscribe!==false).length;
  socket.close();f.advance(250);socket=f.sockets.at(-1);socket.emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(socket.sent.length,0);
  f.advance(7200000);assert.equal(f.sockets.length,2);assert.equal(f.sockets[0].sent.filter(m=>m.command==='snapshot'&&m.subscribe!==false).length,reads);assert.equal(f.timers.size,0);
 }finally{f.client.dispose();}assert.equal(f.timers.size,0);
});
await check('malformed bootstrap is safely retried until a corrected local binding arrives even if its event was missed',()=>{
 const f=fixture();try{const boot=f.bootstrap();f.setBootstrap({...boot,endpoint:'ws://remote.invalid/mirror'});f.client.setPassiveDemand('connection',true);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,1);
  f.setBootstrap(boot);f.advance(250);assert.equal(f.sockets.length,1);f.sockets[0].emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
 }finally{f.client.dispose();}
});
await check('an error without a close callback still rejoins the single recovery path',()=>{
 const f=fixture();try{f.client.setPassiveDemand('connection',true);const old=f.sockets[0];old.emit('open');old.close=()=>{old.closed=true;};old.emit('error');assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);f.advance(250);f.sockets[1].emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('unknown protocol failure recovers transport without replaying an unconfirmed operation',async()=>{
 const f=fixture();try{f.client.setPassiveDemand('connection',true);f.connect();const pending=f.client.mutate('fan.setPreset',{preset:'soft'});const rejected=assert.rejects(pending);
  f.sockets[0].emit('message','{invalid');await rejected;assert.equal(f.timers.size,1);f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.client.snapshot().snapshot.ready,true);
  assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setPreset').length,1);
 }finally{f.client.dispose();}
});
await check('native OFF revocation stops the base connection and every queued retry',()=>{
 const f=fixture();try{f.client.setPassiveDemand('connection',true);f.sockets[0].emit('open');f.sockets[0].close();assert.equal(f.timers.size,1);f.setBootstrap({revoked:true});f.client.bootstrapChanged();f.advance(86400000);assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);assert.equal(f.client.snapshot().connected,false);}finally{f.client.dispose();}
});

fs.writeFileSync(path.join(validation,'MIRROR-HANG-RECOVERY.json'),JSON.stringify({status:'passed',cases:cases.length,results:cases,healthyRecoveryMeasurements,compoundOrderingMeasurements,realProcesses:false,realSettings:false,clock:'virtual'},null,2)+'\n');console.log(JSON.stringify({status:'passed',mirrorHangCases:cases.length}));
