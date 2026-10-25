// Production client/relay with memory-only sockets, stores and clock. No Steam or native access.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {root,task,validation} from './fixtures/decky_task_paths.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true};
const cases=[],check=async(name,fn)=>{await fn();cases.push(name);};
const flush=async()=>{for(let i=0;i<200;i++)await Promise.resolve();};
const data=(revision=1)=>({runId,generation:1,revision,ready:true,game:{label:'Memory game',identity:'memory-game:101',fields:{acMode:{value:'balanced',supported:true,choices:[{data:'balanced',label:'Balanced'},{data:'eco',label:'Eco'}]}}},fan:{supported:true,enabled:false,preset:'balanced',choices:[]},actions:{game:true,fan:true}});
function load(file,{clone=structuredClone,backup=false}={}){
 const source=fs.readFileSync(path.join(backup?path.join(task,'backup'):root,file),'utf8');
 const module={exports:{}};vm.runInNewContext(transformSync(source,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone:clone,queueMicrotask,
  require:name=>{if(name==='./steamRunningContext'||name==='../../decky-plugin/src/steamRunningContext')return load('decky-plugin/src/steamRunningContext.ts');throw Error('Unexpected dependency: '+name);}});
 return module.exports;
}
function clientFixture(backup=false){
 const clones={total:0,withSnapshot:0};const {MirrorClient}=load('decky-plugin/src/mirrorClient.ts',{backup,clone:value=>{clones.total++;if(value?.snapshot)clones.withSnapshot++;return structuredClone(value);}});
 const sockets=[],timers=new Map();let next=0;
 class Socket{listeners=new Map();sent=[];closed=false;constructor(){sockets.push(this)}addEventListener(name,fn){this.listeners.set(name,fn)}send(value){this.sent.push(JSON.parse(value))}close(){this.closed=true;this.listeners.get('close')?.()}emit(type,value){this.listeners.get(type)?.({data:JSON.stringify(value)})}}
 const client=new MirrorClient({bootstrap:()=>({endpoint:'ws://127.0.0.1:23456/mirror',token:'TEST-ONLY',runId,hudProtocol:1}),socket:()=>new Socket(),schedule:(action,delay)=>{timers.set(++next,{action,delay});return next},cancel:id=>timers.delete(id),clientId:'memory-only'});
 return {client,sockets,timers,clones,open:()=>sockets.at(-1).emit('open'),snap:revision=>sockets.at(-1).emit('message',{type:'snapshot',runId,snapshot:data(revision)})};
}
function relayFixture({backup=false,read,onStop}={}){
 const {ReadOnlyMirrorRelay}=load('src/bridge/deckyMirrorRelay.ts',{backup}),listeners=new Set(),captured=[],replies=[],timers=new Map(),stats={reads:0,observes:0,stops:0,callbacks:0};let id=0,relay;
 relay=new ReadOnlyMirrorRelay({read:async()=>{stats.reads++;return read?read(relay):data();},observe:fn=>{stats.observes++;captured.push(fn);listeners.add(fn);return()=>{stats.stops++;listeners.delete(fn);onStop?.();}},reply:async(connection,message)=>{replies.push({connection,message});return true;},schedule:(action,delay)=>{timers.set(++id,{action,delay});return id},cancel:id=>timers.delete(id)});
 const request=subscribe=>relay.request({...peer,request:{command:'snapshot',runId,...(subscribe===undefined?{}:{subscribe})}});
 return {relay,listeners,captured,replies,timers,stats,request,emit:()=>{for(const fn of listeners){stats.callbacks++;fn();}}};
}
async function workload(backup){
 const client=clientFixture(backup);let hudCalls=0;client.client.subscribe(()=>{});client.client.subscribe(()=>hudCalls++,{connectionOnly:true});client.client.setPassiveVisible(true);client.open();client.client.setVisible(true);
 for(let i=1;i<=300;i++)client.snap(i);
 const clientCounts={snapshotPackets:300,fullSnapshotClones:client.clones.withSnapshot,hudStatusCallbacks:hudCalls};client.client.dispose();
 const relay=relayFixture({backup});relay.relay.connected(peer);const observersBeforeOpen=relay.listeners.size;await relay.request();await relay.request(false);
 for(let i=0;i<10000;i++)relay.emit();await flush();const relayCounts={observersBeforeOpen,hiddenObservers:relay.listeners.size,hiddenSourceEvents:10000,hiddenCallbacks:relay.stats.callbacks,businessReads:relay.stats.reads,hiddenTimers:relay.timers.size};relay.relay.dispose();
 return {client:clientCounts,relay:relayCounts};
}
const baseline=await workload(true),optimized=await workload(false);
await check('same 300 snapshot packets preserve controls while HUD full-snapshot cloning is eliminated',()=>{assert.equal(baseline.client.fullSnapshotClones,600);assert.equal(optimized.client.fullSnapshotClones,300);assert.equal(optimized.client.hudStatusCallbacks,2);assert(baseline.client.hudStatusCallbacks>300);});
await check('healthy passive connection has no business observers; 10000 hidden events trigger no callbacks',()=>{assert.equal(baseline.relay.hiddenObservers,1);assert.equal(baseline.relay.hiddenCallbacks,10000);assert.equal(optimized.relay.observersBeforeOpen,0);assert.equal(optimized.relay.hiddenObservers,0);assert.equal(optimized.relay.hiddenCallbacks,0);assert.equal(optimized.relay.businessReads,1);assert.equal(optimized.relay.hiddenTimers,0);});
await check('reopening resubscribes exactly once and reads values changed while hidden',async()=>{let preset='balanced';const f=relayFixture({read:()=>({...data(),fan:{...data().fan,preset}})});try{f.relay.connected(peer);await f.request();await f.request();assert.equal(f.stats.observes,1);await f.request(false);preset='eco';await f.request();assert.equal(f.stats.observes,2);assert.equal(f.stats.reads,2);assert.equal(f.replies.at(-1).message.snapshot.fan.preset,'eco');}finally{f.relay.dispose()}assert.equal(f.stats.stops,2)});
await check('visible source changes still coalesce and propagate immediately without a recurring timer',async()=>{const f=relayFixture();try{await f.request();f.emit();f.emit();f.emit();await flush();assert.equal(f.stats.reads,2);assert.equal(f.replies.length,2);assert.equal(f.replies.at(-1).message.snapshot.revision,2);assert.equal(f.timers.size,0);}finally{f.relay.dispose()}});
await check('repeated pause requests release observers once and do not alter the healthy peer',async()=>{const f=relayFixture();try{await f.request();for(let i=0;i<30;i++)await f.request(false);assert.equal(f.stats.stops,1);assert.equal(f.listeners.size,0);await f.request();assert.equal(f.stats.observes,2);}finally{f.relay.dispose()}});
await check('late callbacks from the paused observation cannot trigger a reread after reopening',async()=>{const f=relayFixture();try{await f.request();const old=f.captured[0];await f.request(false);await f.request();const reads=f.stats.reads;for(let i=0;i<100;i++)old();await flush();assert.equal(f.stats.reads,reads);f.emit();await flush();assert.equal(f.stats.reads,reads+1);}finally{f.relay.dispose()}});
await check('hiding during an async snapshot drops its reply and performs no replacement read',async()=>{let resolve;const pending=new Promise(r=>resolve=r),f=relayFixture({read:()=>pending});try{const opening=f.request();await flush();assert.equal(f.stats.reads,1);await f.request(false);resolve(data());await opening;assert.equal(f.stats.reads,1);assert.equal(f.replies.length,0);assert.equal(f.listeners.size,0);}finally{f.relay.dispose()}});
await check('hide/reopen during an async read only delivers the new demand and its fresh data',async()=>{let resolve,calls=0;const pending=new Promise(r=>resolve=r),f=relayFixture({read:()=>++calls===1?pending:{...data(),notice:'fresh-after-reopen'}});try{const old=f.request();await flush();await f.request(false);const reopened=f.request();resolve(data());await Promise.all([old,reopened]);assert.equal(f.stats.reads,2);assert.equal(f.replies.length,1);assert.equal(f.replies[0].message.snapshot.notice,'fresh-after-reopen');}finally{f.relay.dispose()}});
await check('hiding cancels bounded unstable-read recovery instead of waking a passive HUD',async()=>{const f=relayFixture({read:relay=>{relay.invalidate();return data();}});try{await f.request();assert.equal(f.stats.reads,3);assert.equal(f.timers.size,1);await f.request(false);assert.equal(f.timers.size,0);assert.equal(f.listeners.size,0);}finally{f.relay.dispose()}});
await check('observer teardown exceptions still release peer and all demand',async()=>{const f=relayFixture({onStop:()=>{throw Error('Memory stop failure')}});await f.request();assert.doesNotThrow(()=>f.relay.dispose());assert.equal(f.listeners.size,0);assert.equal(f.timers.size,0);const reads=f.stats.reads;f.captured[0]();await flush();assert.equal(f.stats.reads,reads);});
await check('no-op busy/snapshot transitions do not clone or rerender control state',()=>{const f=clientFixture();try{let calls=0;f.client.subscribe(()=>calls++);f.client.setPassiveVisible(true);f.open();f.client.setVisible(true);const before={calls,clones:f.clones.total};f.client.setVisible(false);assert.equal(calls,before.calls);assert.equal(f.clones.total,before.clones);assert.equal(f.sockets[0].closed,false);assert.equal(f.timers.size,0);}finally{f.client.dispose()}});
await check('full control subscribers retain independent snapshots and cannot modify admitted state',()=>{const f=clientFixture();try{let observed;f.client.subscribe(s=>{if(s.snapshot)s.snapshot.game.fields.acMode.value='foreign'});f.client.subscribe(s=>{if(s.snapshot)observed=s.snapshot.game.fields.acMode.value});f.client.setVisible(true);f.open();f.snap(1);assert.equal(observed,'balanced');assert.equal(f.client.snapshot().snapshot.game.fields.acMode.value,'balanced');}finally{f.client.dispose()}});
await check('HUD connection-only subscribers see reconnect transitions but never control data',()=>{const f=clientFixture(),states=[];try{f.client.subscribe(s=>states.push(s),{connectionOnly:true});f.client.setPassiveVisible(true);f.open();f.client.setVisible(true);f.snap(1);f.snap(2);f.sockets[0].close();assert.deepEqual(states.map(s=>s.connected),[false,true,false]);assert(states.every(s=>s.snapshot===null&&s.busy===false));assert.equal(f.timers.size,1);}finally{f.client.dispose()}});
await check('unsubscribing connection status releases the HUD listener while controls remain live',()=>{const f=clientFixture();let calls=0;try{const stop=f.client.subscribe(()=>calls++,{connectionOnly:true});stop();f.client.setPassiveVisible(true);f.open();f.client.setVisible(true);f.snap(1);assert.equal(calls,1);assert.equal(f.client.snapshot().snapshot.revision,1);}finally{f.client.dispose()}});
await check('invalid snapshot identity still closes transport before a snapshot can reach any subscriber',()=>{const f=clientFixture();try{let snapshots=0;f.client.subscribe(s=>{if(s.snapshot)snapshots++});f.client.setVisible(true);f.open();f.sockets[0].emit('message',{type:'snapshot',runId,snapshot:{...data(),generation:-1}});assert.equal(snapshots,0);assert.equal(f.client.snapshot().connected,false);assert.equal(f.sockets[0].closed,true);}finally{f.client.dispose()}});
await check('a renderer-restored business snapshot while hidden pauses only that stream',()=>{const f=clientFixture();try{let calls=0;f.client.subscribe(()=>calls++);f.client.setPassiveVisible(true);f.open();const before=calls;f.snap(1);assert.equal(f.sockets[0].sent.at(-1).subscribe,false);assert.equal(f.sockets[0].closed,false);assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.snapshot().snapshot,null);assert.equal(calls,before);assert.equal(f.timers.size,0);}finally{f.client.dispose()}});
await check('actual host pauses every business observer and refreshes the global schedule on reopening',async()=>{
 const {createMirrorHostFixture}=await import('./fixtures/decky_mirror_host_fixture.mjs');const f=await createMirrorHostFixture();try{
  f.ready();await flush();f.emit('deckySidebar.mirrorPeer',peer);assert.equal(f.observerStats().watchers,0);assert.equal(f.observerStats().schedule,0);
  const request=subscribe=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,...(subscribe===undefined?{}:{subscribe})}});
  request();await flush();assert.equal(f.observerStats().schedule,1);assert.equal(f.observerStats().watchers,1);const initial=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  request(false);await flush();assert.equal(f.observerStats().watchers,0);assert.equal(f.observerStats().game,0);assert.equal(f.observerStats().schedule,0);const before={...f.counts};
  f.state.schedule.active.ac='extreme';for(let i=0;i<1000;i++){f.scheduleChanged();f.powerChanged();}await flush();assert.equal(f.counts.schedule,before.schedule);assert.equal(f.counts.power,before.power);
  request();await flush();const latest=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;assert.equal(latest.game.fields.acMode.value,'extreme');assert.notEqual(initial.game.fields.acMode.value,latest.game.fields.acMode.value);assert.equal(f.counts.schedule,before.schedule+1);assert.equal(f.observerStats().schedule,1);
 }finally{f.stop()}assert.deepEqual(f.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});
});
const report={status:'passed',checks:cases.length,cases,baseline,optimized,scope:'Executed production client and relay against in-memory sockets/store events/clock. Counts measure eliminated callbacks and structuredClone calls; no measured CPU percentage, Steam, native hardware, deployment or physical button input.'};
fs.writeFileSync(path.join(validation,'DECKY-IDLE-CPU.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));

