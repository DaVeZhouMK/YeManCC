import vm from 'node:vm';import {createRequire} from 'node:module';
// Exact client -> relay -> host -> fan/global adapters. External IO is memory only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {root,task} from './fixtures/decky_task_paths.mjs';import {load,seed} from './fixtures/decky_game_memory_store.mjs';import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const {MirrorClient}=load(fs.readFileSync(path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'),{'./steamRunningContext':load(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8')),'../../src/bridge/gameSteamContextModel':load(fs.readFileSync(path.join(root,'src/bridge/gameSteamContextModel.ts'),'utf8'))});
const flush=async()=>{for(let i=0;i<300;i++)await Promise.resolve();};
const deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};};
const results=[];async function check(name,run){await run();results.push(name);}
async function fixture(options={}){
 const start=deferred(),apply=deferred(),calls=[],fan={active:false,preset:'balanced'},timers=new Map();let counter=0,now=0,host;
 const runId='f'.repeat(32),peer={connection:'1',runId};
 const listeners=new Map(),socket={sent:[],addEventListener:(name,fn)=>listeners.set(name,fn),emit:(name,value)=>listeners.get(name)?.({data:typeof value==='string'?value:JSON.stringify(value)}),send:text=>{const request=JSON.parse(text);socket.sent.push(request);host.emit('deckySidebar.mirrorRequest',{...peer,request});},close:()=>{listeners.get('close')?.();host?.emit('deckySidebar.mirrorPeer',{...peer,connected:false});}};
 const life={controlReady:false,state:'starting',hasControlIntent:false,recoveryActive:false,start:async()=>{calls.push('start');return start.promise;},apply:async()=>{calls.push('apply');life.hasControlIntent=true;await apply.promise;life.state='ready';life.controlReady=true;return {state:'Ready'};},applyPreset:async()=>{calls.push('preset');life.hasControlIntent=true;life.state='ready';life.controlReady=true;return {state:'Ready'};},disable:async()=>{calls.push('disable');await options.disableWait;life.hasControlIntent=false;life.controlReady=false;life.state='awaiting-control';return {state:'AwaitingControl'};}};
 const feature={FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},fanControlActive:{get value(){return fan.active;}},getFanFeatureSettings:()=>({preset:fan.preset}),getFanPresetCurve:()=>[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:90}],saveFanCurve:async(_,preset)=>{fan.preset=preset;},setFanControlActive:value=>{fan.active=value;},setFanNavigationDuty:()=>{},recordFanHandshake:async()=>{}};
 host=await createMirrorHostFixture({initial:options.initial??{version:1,entries:{},rtss:{}},readHooks:options.readHooks??{},fanOwner:{lifecycle:life,featureBindings:feature,close:async()=>{}},invoke:async(command,args)=>{if(command==='deckySidebar.mirrorAttach')return {connected:false};if(command==='deckySidebar.mirrorReply'){socket.emit('message',args.message);return true;}if(command==='input.shortcutRuntime.clear'){host.counts.shortcutClears=(host.counts.shortcutClears||0)+1;return options.clearReply??true;}throw Error('Unmocked IO '+command);}});
 if(!options.withGame)host.state.game=null;host.state.input.gyroMotion={enabled:false,preset:'fps'};host.ready();
 const client=new MirrorClient({bootstrap:()=>({runId,endpoint:'ws://127.0.0.1:12345/mirror',token:'INERT-TEST-ONLY'}),socket:()=>socket,clientId:'controls34',schedule:(action,delay)=>{timers.set(++counter,{action,due:now+delay});return counter;},cancel:id=>timers.delete(id)});
 client.setVisible(true);socket.emit('open');await flush();
 const advance=async ms=>{now+=ms;for(const[id,t]of[...timers])if(t.due<=now){timers.delete(id);t.action();}await flush();};
 return {host,client,socket,start,apply,life,fan,calls,timers,advance,stop:()=>{client.dispose();host.stop();}};
}
await check('slow fan ON returns real accepted intent, cancels 4s client deadline and publishes pending snapshot without blocking other controls',async()=>{const f=await fixture();const on=f.client.mutate('fan.setEnabled',{enabled:true},f.client.snapshot().snapshot);await flush();const r=await on;assert.equal(r.pending,true);assert.equal(r.applied,false);assert.equal(f.client.snapshot().busy,false);assert.equal(f.client.snapshot().snapshot.fan.enabled,true);assert.equal(f.client.snapshot().snapshot.fan.pending,true);assert.equal(f.client.snapshot().snapshot.fan.supported,false);assert.equal(f.timers.size,0);await f.advance(60000);assert.equal(f.client.snapshot().snapshot.fan.enabled,true);assert.equal(f.calls.filter(c=>c==='start').length,1);assert.equal(f.client.snapshot().connected,true);
 const gyro=f.client.mutate('global.setField',{field:'gyroEnabled',value:'on'},f.client.snapshot().snapshot);await flush();await gyro;assert.equal(f.host.state.input.gyroMotion.enabled,true);assert.equal(f.client.snapshot().snapshot.fan.enabled,true);assert.equal(f.client.snapshot().busy,false);
 f.start.resolve({allowed:true,writeReady:false});await flush();assert.equal(f.client.snapshot().snapshot.fan.pending,true);f.apply.resolve({});await flush();assert.equal(f.client.snapshot().snapshot.fan.pending,false);assert.equal(f.client.snapshot().snapshot.fan.supported,true);assert.equal(f.client.snapshot().snapshot.fan.enabled,true);assert.deepEqual(f.calls,['start','apply']);f.stop();});
await check('pending fan preset remains an interactive save, not a second startup',async()=>{const f=await fixture();const on=f.client.mutate('fan.setEnabled',{enabled:true});await flush();await on;const save=f.client.mutate('fan.setPreset',{preset:'soft'},f.client.snapshot().snapshot);await flush();await save;assert.equal(f.fan.preset,'soft');assert.equal(f.host.counts.fanSaves,1);f.start.resolve({allowed:true,writeReady:false});await flush();assert.deepEqual(f.calls,['start','preset']);assert.equal(f.client.snapshot().snapshot.fan.pending,false);f.stop();});
await check('client explicit OFF cancels pending original startup and ignores its late confirmation',async()=>{const f=await fixture();const on=f.client.mutate('fan.setEnabled',{enabled:true});await flush();await on;const off=f.client.mutate('fan.setEnabled',{enabled:false},f.client.snapshot().snapshot);await flush();await off;f.start.resolve({allowed:true,writeReady:true});await flush();assert.equal(f.fan.active,false);assert.equal(f.client.snapshot().snapshot.fan.enabled,false);assert.deepEqual(f.calls,['start','disable']);f.stop();});

await check('slow OEM release also frees the client and keeps explicit OFF without claiming hardware release',async()=>{const release=deferred(),f=await fixture({disableWait:release.promise});const on=f.client.mutate('fan.setEnabled',{enabled:true});await flush();await on;f.start.resolve({allowed:true,writeReady:true});f.apply.resolve({});await flush();const off=f.client.mutate('fan.setEnabled',{enabled:false},f.client.snapshot().snapshot);await flush();const r=await off;assert.equal(r.pending,true);assert.equal(r.applied,false);assert.equal(f.client.snapshot().busy,false);assert.equal(f.client.snapshot().snapshot.fan.enabled,false);assert.equal(f.life.hasControlIntent,true);await f.advance(60000);assert.equal(f.client.snapshot().snapshot.fan.enabled,false);assert.equal(f.timers.size,0);release.resolve();await flush();assert.equal(f.life.hasControlIntent,false);assert.equal(f.client.snapshot().snapshot.fan.enabled,false);f.stop();});
await check('all four global preset IDs use the original CAS and keep game entries untouched',async()=>{const f=await fixture();const on=f.client.mutate('global.setField',{field:'gyroEnabled',value:'on'});await flush();await on;for(const preset of ['racing','custom','steam','fps']){const p=f.client.mutate('global.setField',{field:'gyroPreset',value:preset},f.client.snapshot().snapshot);await flush();await p;assert.equal(f.host.state.input.gyroMotion.preset,preset);}assert.equal(f.host.counts.saves,0);assert.equal(f.host.counts.inputSaves,5);assert.deepEqual(f.host.memory.raw().entries,{});assert.equal(f.host.state.input.outputTarget.persona,'steamdeck');f.stop();});
await check('four-position cold choice saves the selected preset before one original startup',async()=>{const f=await fixture();const p=f.client.mutate('fan.setChoice',{choice:'soft'},f.client.snapshot().snapshot);await flush();const r=await p;assert.equal(r.saved,true);assert.equal(r.pending,true);assert.equal(f.fan.preset,'soft');assert.equal(f.host.counts.fanSaves,1);assert.equal(f.client.snapshot().busy,false);assert.deepEqual(f.calls,['start']);f.start.resolve({allowed:true,writeReady:false});f.apply.resolve({});await flush();assert.equal(f.client.snapshot().snapshot.fan.preset,'soft');assert.equal(f.client.snapshot().snapshot.fan.pending,false);f.stop();});
await check('four-position pending choice changes preset without a second startup',async()=>{const f=await fixture();let p=f.client.mutate('fan.setChoice',{choice:'soft'});await flush();await p;p=f.client.mutate('fan.setChoice',{choice:'aggressive'},f.client.snapshot().snapshot);await flush();const r=await p;assert.equal(r.pending,true);assert.equal(f.fan.preset,'aggressive');assert.equal(f.host.counts.fanSaves,2);assert.deepEqual(f.calls,['start']);f.start.resolve({allowed:true,writeReady:false});await flush();assert.deepEqual(f.calls,['start','preset']);assert.equal(f.client.snapshot().snapshot.fan.preset,'aggressive');f.stop();});
await check('four-position OFF cancels a slow startup and its late confirmation',async()=>{const f=await fixture();let p=f.client.mutate('fan.setChoice',{choice:'soft'});await flush();await p;p=f.client.mutate('fan.setChoice',{choice:'off'},f.client.snapshot().snapshot);await flush();await p;f.start.resolve({allowed:true,writeReady:true});await flush();assert.equal(f.fan.active,false);assert.deepEqual(f.calls,['start','disable']);assert.equal(f.client.snapshot().snapshot.fan.enabled,false);f.stop();});
await check('malformed combined fan choices are rejected before saving or starting an owner',async()=>{const f=await fixture();for(const args of [{choice:'unknown'},{choice:'soft',enabled:true},{}]){const p=f.client.mutate('fan.setChoice',args,f.client.snapshot().snapshot);await flush();await assert.rejects(p);}assert.equal(f.host.counts.fanSaves,0);assert.deepEqual(f.calls,[]);f.stop();});
for(const command of ['performance.setControls','frame.setField','fan.setChoice'])await check(command+' crosses real client/relay/owner when unrelated monitor refresh races slider release',async()=>{
 const f=await fixture();try{
  const expected=f.client.snapshot().snapshot;f.host.state.monitor.enabled=!f.host.state.monitor.enabled;f.host.monitorChanged();
  const args=command==='performance.setControls'?{patch:{tdp:25}}:command==='frame.setField'?{field:'acFps',value:'90'}:{choice:'soft'};
  const sent=f.client.mutate(command,args,expected);await flush();await sent;await flush();
  assert.equal(f.client.snapshot().busy,false);assert.equal(f.socket.sent.filter(r=>r.command===command).length,1);
  if(command==='performance.setControls'){assert.equal(f.host.counts.performanceUpdates,1);assert.equal(f.client.snapshot().snapshot.performance.controls.find(c=>c.key==='tdp').value,25);}
  else if(command==='frame.setField'){assert.equal(f.host.counts.frameApplies,1);assert.equal(f.client.snapshot().snapshot.frames.pair.ac.fps,90);}
  else{assert.deepEqual(f.calls,['start']);assert.equal(f.fan.preset,'soft');assert.equal(f.client.snapshot().snapshot.fan.enabled,true);}
 }finally{f.stop();assert.equal(f.timers.size,0);}
});

await check('held TDP slider sends once after release across unrelated owner refresh and reads its saved value back',async()=>{
 const f=await fixture(),{PerformanceIntentQueue}=load(fs.readFileSync(path.join(root,'decky-plugin/src/performanceIntentQueue.ts'),'utf8'));
 const current=()=>{const s=f.client.snapshot();return s.connected&&!s.busy&&s.snapshot?.ready?s.snapshot:null;};
 const queue=new PerformanceIntentQueue({current,confirmOnSnapshot:true,
  sameSource:(a,b)=>a.runId===b.runId&&a.generation===b.generation&&a.performance?.sourceRevision===b.performance?.sourceRevision,
  allowed:(patch,s)=>s.actions.performance===true,value:(key,s)=>s.performance.controls.find(c=>c.key===key)?.value,
  send:intent=>f.client.mutate('performance.setControls',{patch:intent.patch},intent.view),
  schedule:(action,delay)=>{const id=Symbol('slider');f.timers.set(id,{action,due:3000});return id;},cancel:id=>f.timers.delete(id)});
 const stop=f.client.subscribe(state=>queue.observe(state.connected&&state.snapshot?.ready?state.snapshot:null));
 try{
  queue.begin();queue.choose('tdp',25,current());
  assert.equal(f.socket.sent.filter(r=>r.command==='performance.setControls').length,0,'held slider never writes');
  queue.end();f.host.state.monitor.enabled=!f.host.state.monitor.enabled;f.host.monitorChanged();
  await f.advance(3000);await flush();
  assert.equal(f.socket.sent.filter(r=>r.command==='performance.setControls').length,1);
  assert.equal(f.host.counts.performanceUpdates,1);assert.equal(f.client.snapshot().snapshot.performance.controls.find(c=>c.key==='tdp').value,25);
  assert.equal(queue.snapshot(),null,'saved value readback clears the optimistic draft');
 }finally{stop();queue.dispose();f.stop();assert.equal(f.timers.size,0);}
});


await check('recognized-game global input travels through the actual client/host and input CAS without creating an EXE profile',async()=>{
 const f=await fixture({withGame:true});try{
  const snap=f.client.snapshot().snapshot;assert.equal(snap.game.fields.profile.value,'global');
  const result=f.client.mutate('game.setField',{field:'padPersona',value:'elite'},snap);await flush();assert.equal((await result).saved,true);await flush();
  assert.equal(f.host.counts.inputSaves,1);assert.equal(f.host.counts.saves,0);assert.equal(f.host.state.input.outputTarget.persona,'elite');
  assert.equal(f.client.snapshot().snapshot.game.fields.profile.value,'global');assert.equal(f.client.snapshot().snapshot.game.fields.padPersona.value,'elite');assert.equal(f.host.memory.view().entries['game.exe'],undefined);
 }finally{f.stop();assert.equal(f.timers.size,0);}
});
for(const [command,args,count]of [
 ['performance.setControls',{patch:{tdp:25}},'performanceUpdates'],
 ['frame.setField',{field:'acFps',value:'95'},'globalFrameSaves'],
])await check(command+' reaches the global owner once in a recognized game during an unrelated refresh',async()=>{
 const f=await fixture({withGame:true});try{
  const snap=f.client.snapshot().snapshot;f.host.state.monitor.enabled=!f.host.state.monitor.enabled;f.host.monitorChanged();
  const result=f.client.mutate(command,args,snap);await flush();assert.equal((await result).saved,true);await flush();
  assert.equal(f.host.counts[count],1);assert.equal(f.host.counts.saves,0);assert.equal(f.host.memory.view().entries['game.exe'],undefined);
  assert.equal(f.client.snapshot().snapshot.game.fields.profile.value,'global');
  if(command==='frame.setField')assert.equal(f.client.snapshot().snapshot.frames.pair.ac.fps,95);
  else assert.equal(f.client.snapshot().snapshot.performance.controls.find(c=>c.key==='tdp').value,25);
 }finally{f.stop();assert.equal(f.timers.size,0);}
});


for(const dedicated of [false,true])await check('reverse controller result proves exactly one '+(dedicated?'EXE profile write':'global input CAS')+' and one original native dispatch',async()=>{
 const initial=seed();if(!dedicated)delete initial.entries['game.exe'];const f=await fixture({withGame:true,initial});try{
  const before=structuredClone(f.host.memory.view()),savedInput=structuredClone(f.host.state.input),p=f.client.mutate('game.setField',{field:'padPersona',value:'elite'},f.client.snapshot().snapshot);await flush();const result=await p;await flush();
  assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.pending,true);assert.equal(f.host.counts.shortcutClears,1);
  assert.equal(f.host.counts.saves,dedicated?1:0);assert.equal(f.host.counts.inputSaves,dedicated?0:1);
  if(dedicated){assert.equal(f.host.memory.view().entries['game.exe'].padPersona,'elite');assert.deepEqual(f.host.state.input,savedInput);}else{assert.equal(f.host.state.input.outputTarget.persona,'elite');assert.equal(f.host.memory.view().entries['game.exe'],undefined);}
  assert.deepEqual(structuredClone(f.host.memory.view().rtss),before.rtss);assert.deepEqual(structuredClone(f.host.memory.view().entries['other.exe']),before.entries['other.exe']);
  assert.equal(f.client.snapshot().snapshot.game.fields.padPersona.value,'elite');
  const packet=f.socket.sent.findLast(r=>r.command==='game.setField');f.host.emit('deckySidebar.mirrorRequest',{connection:'1',runId:packet.runId,request:packet});await flush();
  assert.equal(f.host.counts.shortcutClears,1);assert.equal(f.host.counts.saves,dedicated?1:0);assert.equal(f.host.counts.inputSaves,dedicated?0:1);
 }finally{f.stop();assert.equal(f.timers.size,0);}
});
await check('same dedicated controller reselection dispatches the original YMCC input owner once without another file write',async()=>{
 const initial=seed();Object.assign(initial.entries['game.exe'],{padPersona:'elite',gyroOverride:'off',gyroPreset:'fps'});const f=await fixture({withGame:true,initial});try{
  const before=structuredClone(f.host.memory.view()),p=f.client.mutate('game.setField',{field:'padPersona',value:'elite'},f.client.snapshot().snapshot);await flush();const result=await p;
  assert.equal(result.saved,false);assert.equal(result.pending,true);assert.equal(result.applied,false);assert.equal(f.host.counts.shortcutClears,1);assert.equal(f.host.counts.saves,0);assert.equal(f.host.counts.inputSaves,0);assert.deepEqual(structuredClone(f.host.memory.view()),before);
 }finally{f.stop();assert.equal(f.timers.size,0);}
});
await check('actual client receives saved-pending when its own committed input starts native protection, without a resend',async()=>{
 let f;const hooks={inputCommit:async()=>{f.host.state.gamepadState={virtualOutput:{state:'starting',requestedPersona:'elite',startProtection:{locked:true,requestId:81,phase:'pending',persona:'elite',remainingMs:30000,elapsedMs:0,maxMs:30000,settleMs:10000}}};f.host.emit('gamepad.state',f.host.state.gamepadState);}};
 f=await fixture({readHooks:hooks});try{
  const p=f.client.mutate('global.setField',{field:'padPersona',value:'elite'},f.client.snapshot().snapshot);await flush();const result=await p;await flush();
  assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.pending,true);assert.equal(f.client.snapshot().busy,false);assert.equal(f.client.snapshot().snapshot.virtualController.locked,true);assert.equal(f.host.counts.inputSaves,1);assert.equal(f.socket.sent.filter(r=>r.command==='global.setField').length,1);
 }finally{f.stop();assert.equal(f.timers.size,0);assert.equal(f.host.controllerTimerCount(),0);}
});
await check('target change after a completed save preserves saved truth and cannot dispatch a runtime command into the new game',async()=>{
 let f;const hooks={inputCommit:async()=>{f.host.state.game={pid:999,path:'C:/Other.exe',name:'other.exe',processCreated:'new-birth'};f.host.gameChanged();}};
 f=await fixture({readHooks:hooks});try{
  const p=f.client.mutate('global.setField',{field:'padPersona',value:'elite'},f.client.snapshot().snapshot);await flush();const result=await p;
  assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.pending,true);assert.equal(f.host.counts.inputSaves,1);assert.equal(f.host.counts.shortcutClears??0,0);assert.equal(f.host.memory.view().entries['other.exe'],undefined);
 }finally{f.stop();assert.equal(f.timers.size,0);}
});

await check('FxSound toggle reaches production client relay host owner and preserves game profile',async()=>{
 // Display subscribers cannot turn a successful original native receipt into
 // an operation failure, or stop the next subscriber from seeing new facts.
 const {FxSoundOwner}=load(fs.readFileSync(path.join(root,'src/bridge/fxSound.ts'),'utf8'),{'./ipc':{invoke:()=>{throw Error('Use only the explicit original audio port');}}});
 const timers=new Map();let timerId=0,reads=0,writes=0,badCalls=0,goodCalls=0,failRead=false;
 let current={known:true,installed:true,running:false,enabled:false,selected:true,preset:'inert',output:'inert',reason:''},lastReceipt;
 const owner=new FxSoundOwner({read:async()=>{reads++;if(failRead)throw Error('inert audio read unavailable');return structuredClone(current);},set:async enabled=>{writes++;current={...current,running:enabled,enabled};return lastReceipt={ok:true,copied:false,reused:false,repaired:false,reason:'inert original native receipt',state:structuredClone(current)};}},(fn,delay)=>{const id=++timerId;timers.set(id,{fn,delay});return id;},id=>timers.delete(id));
 await owner.read();const bad=owner.retain(()=>{badCalls++;throw Error('inert display listener failed');}),good=owner.retain(()=>{goodCalls++;});
 try{
  await flush();let result,error=null;try{result=await owner.setEnabled(true);}catch(e){error=String(e);}
  console.log(JSON.stringify({audioObserverReceipt:{error,writes,badCalls,goodCalls}}));
  assert.equal(error,null,'A successful audio native receipt must survive a display observer failure');assert.equal(result,lastReceipt);assert.equal(result.ok,true);assert.equal(writes,1);assert.equal(badCalls,1);assert.equal(goodCalls,1);
  assert.equal((await owner.read()).enabled,true);assert.equal(writes,1);
  failRead=true;assert.equal((await owner.read()).known,false);assert.equal(goodCalls,2);assert.equal(badCalls,2);
  failRead=false;assert.equal((await owner.read()).enabled,true);assert.equal(goodCalls,3);assert.equal(badCalls,3);
  assert.equal(timers.size,1);assert.equal([...timers.values()][0].delay,1500);
  const first=[...timers.entries()][0];timers.delete(first[0]);first[1].fn();await flush();
  assert.equal(timers.size,1);assert.equal([...timers.values()][0].delay,1500);assert.equal(writes,1);
  console.log(JSON.stringify({audioObserverRead:{badCalls,goodCalls,writes,timers:timers.size,delay:[...timers.values()][0].delay}}));
 }finally{bad();good();assert.equal(timers.size,0);}
 // A real native failure still rejects. The next explicit intent can proceed.
 let rejectWrite=true,rejectedWrites=0;
 const nativeFailure=new FxSoundOwner({read:async()=>structuredClone(current),set:async enabled=>{rejectedWrites++;if(rejectWrite)throw Error('inert native set failed');return {ok:true,copied:false,reused:false,repaired:false,reason:'',state:{...current,enabled}};}});
 await assert.rejects(nativeFailure.setEnabled(false),/inert native set failed/);rejectWrite=false;
 assert.equal((await nativeFailure.setEnabled(false)).ok,true);assert.equal(rejectedWrites,2);
 // Retiring observation before a pending read settles cannot revive its timer.
 const pendingRead=deferred(),lateTimers=new Map();let lateCalls=0,lateTimerId=0;
 const late=new FxSoundOwner({read:()=>pendingRead.promise,set:async()=>{throw Error('No late mutation');}},(fn,delay)=>{const id=++lateTimerId;lateTimers.set(id,{fn,delay});return id;},id=>lateTimers.delete(id));
 const end=late.retain(()=>{lateCalls++;throw Error('Retired observer must never execute');});end();pendingRead.resolve(structuredClone(current));await flush();
 assert.equal(lateCalls,0);assert.equal(lateTimers.size,0);
 console.log(JSON.stringify({audioObserverBoundaries:{nativeFailurePreserved:true,nextExplicitWriteSucceeded:true,rejectedWrites,lateCalls,lateTimers:lateTimers.size}}));

 const f=await fixture();try{
  assert.equal(f.client.snapshot().snapshot.audioOptimization.enabled,false);
  const before=JSON.stringify(f.host.memory.view());
  const on=await f.client.mutate('audioOptimization.setEnabled',{enabled:true});await flush();
  assert.equal(on.applied,true);assert.equal(f.host.counts.fxSoundWrites,1);assert.equal(f.client.snapshot().snapshot.audioOptimization.enabled,true);
  const off=await f.client.mutate('audioOptimization.setEnabled',{enabled:false});await flush();
  assert.equal(off.applied,true);assert.equal(f.host.counts.fxSoundWrites,2);assert.equal(f.client.snapshot().snapshot.audioOptimization.enabled,false);
  assert.equal(JSON.stringify(f.host.memory.view()),before);
 }finally{f.stop();assert.equal(f.host.fxTimerCount(),0);assert.equal(f.timers.size,0);}
});
await check('FxSound manual/startup state refreshes only while visible and never replays writes',async()=>{
 const verifyAudioObservationReuse=async(selected=['changed','unchanged','read-failure','explicit-demand','hidden-reopen','power-change','action-freshness'])=>{
  const measurements=[];
  for(const boundary of selected){
   const runId='c'.repeat(32),peer={connection:'1',runId,connected:true};let failRead=false,powerGate=null;
   let audio={known:true,installed:true,running:false,enabled:false,selected:true,preset:'inert observation',output:'inert output',reason:''};
   const h=await createMirrorHostFixture({readHooks:{power:async()=>{if(powerGate)await powerGate.promise;}},fxSoundNative:{read:async()=>{if(failRead)throw Error('inert original audio read failed');return structuredClone(audio);},set:async enabled=>{audio={...audio,running:enabled,enabled};return {ok:true,copied:false,reused:false,repaired:false,reason:'inert native set',state:structuredClone(audio)};}}});h.state.game=null;
   const request=(subscribe=true)=>h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}}),latest=()=>h.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
   try{
    h.ready();h.emit('deckySidebar.mirrorPeer',peer);request();await flush();assert.equal(latest().ready,true);assert.equal(h.counts.fxSoundReads,1);
    const before=h.counts.fxSoundReads;audio={...audio,running:true,enabled:true};h.advanceFx();await flush();
    console.log(JSON.stringify({audioNotificationRead:{boundary,before,after:h.counts.fxSoundReads,enabled:latest().audioOptimization.enabled,known:latest().audioOptimization.known}}));
    assert.equal(h.counts.fxSoundReads,before+1,'Audio state notification and its next snapshot must share the original native read');
    assert.equal(latest().audioOptimization.enabled,true);assert.equal(latest().audioOptimization.known,true);assert.equal(h.counts.fxSoundWrites,0);assert.equal(h.fxTimerCount(),1);

    if(boundary==='unchanged'){
     const reads=h.counts.fxSoundReads,views=h.replies.filter(row=>row.message.type==='snapshot').length;h.advanceFx();await flush();
     assert.equal(h.counts.fxSoundReads,reads+1);assert.equal(h.replies.filter(row=>row.message.type==='snapshot').length,views);
    }else if(boundary==='read-failure'){
     let reads=h.counts.fxSoundReads;failRead=true;h.advanceFx();await flush();
     assert.equal(h.counts.fxSoundReads,reads+1);assert.equal(latest().audioOptimization.known,false);assert.equal(latest().actions.audioOptimization,false);
     reads=h.counts.fxSoundReads;failRead=false;h.advanceFx();await flush();assert.equal(h.counts.fxSoundReads,reads+1);assert.equal(latest().audioOptimization.known,true);assert.equal(latest().actions.audioOptimization,true);
    }else if(boundary==='explicit-demand'){
     const reads=h.counts.fxSoundReads;audio={...audio,running:false,enabled:false};request();await flush();
     assert.equal(h.counts.fxSoundReads,reads+1);assert.equal(latest().audioOptimization.enabled,false);
    }else if(boundary==='hidden-reopen'||boundary==='power-change'){
     powerGate=deferred();const reads=h.counts.fxSoundReads;audio={...audio,running:false,enabled:false};h.advanceFx();await flush();assert.equal(h.counts.fxSoundReads,reads+1);
     audio={...audio,running:true,enabled:true};
     if(boundary==='hidden-reopen'){request(false);await flush();assert.equal(h.fxTimerCount(),0);}
     else{h.state.power={...h.state.power,generation:2};h.emit('power.lifecycleChanged',{...h.state.power,known:true,publicationRevision:2});}
     const gate=powerGate;powerGate=null;gate.resolve();await flush();
     if(boundary==='hidden-reopen'){const settledReads=h.counts.fxSoundReads;request();await flush();assert.equal(h.counts.fxSoundReads,settledReads+1);}
     assert.equal(latest().audioOptimization.enabled,true);assert.equal(latest().generation,boundary==='power-change'?2:1);assert.equal(h.fxTimerCount(),1);
    }else if(boundary==='action-freshness'){
     const admitted=latest(),reads=h.counts.fxSoundReads;audio={...audio,running:false,enabled:false};
     h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'audioOptimization.setEnabled',runId,id:'audio-observation:1',clientId:'audio-observation',sequence:1,revision:admitted.revision,generation:admitted.generation,args:{enabled:false}}});await flush();
     const reply=h.replies.find(row=>row.message.type==='reply'&&row.message.id==='audio-observation:1')?.message;
     assert(reply&&reply.ok===false);assert.equal(reply.error,'镜像操作未确认；请查看 YMCC 当前状态，不自动重放');assert(h.counts.fxSoundReads>reads);assert.equal(h.counts.fxSoundWrites,0);
    }
    measurements.push({boundary,before,after:h.counts.fxSoundReads,enabled:latest().audioOptimization.enabled,known:latest().audioOptimization.known,audioWrites:h.counts.fxSoundWrites});
   }finally{h.stop();await flush();assert.equal(h.fxTimerCount(),0);assert.equal(h.controllerTimerCount(),0);assert.equal(h.sharedPowerTimerCount(),0);}
  }
  console.log(JSON.stringify({audioNotificationMeasurements:measurements}));
 };
 await verifyAudioObservationReuse();

 const verifyFirstAudioRead=async(selected=['ready','pending-then-ready','disposed-before-power-return'])=>{
  const rows=[];
  for(const boundary of selected){
   const power=deferred(),runId='b'.repeat(32),peer={connection:'1',runId,connected:true};
   const h=await createMirrorHostFixture({readHooks:{power:()=>power.promise}});h.state.game=null;
   try{
    h.ready();h.emit('deckySidebar.mirrorPeer',peer);h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:true}});await flush();
    assert.equal(h.counts.fxSoundReads,0,'Audio observer must wait for the first admitted snapshot read');assert.equal(h.fxTimerCount(),0);
    if(boundary==='disposed-before-power-return'){h.stop();power.resolve();await flush();assert.equal(h.counts.fxSoundReads,0);assert.equal(h.fxTimerCount(),0);}
    else{
     if(boundary==='pending-then-ready'){h.state.power={...h.state.power,generation:2,phase:'resuming',hardwareWritesAllowed:false};h.emit('power.lifecycleChanged',{...h.state.power,known:true,publicationRevision:2});}
     power.resolve();await flush();
     if(boundary==='pending-then-ready'){
      assert.equal(h.counts.fxSoundReads,0);assert.equal(h.fxTimerCount(),0);
      assert.equal(h.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot.ready,false);
      h.state.power={...h.state.power,phase:'ready',hardwareWritesAllowed:true};h.emit('power.lifecycleChanged',{...h.state.power,known:true,publicationRevision:3});await flush();
     }
     assert.equal(h.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot.ready,true);
     assert.equal(h.counts.fxSoundReads,1,'The first snapshot and retained observation share the same native read');assert.equal(h.fxTimerCount(),1);
     h.state.fxSound={...h.state.fxSound,running:true,enabled:true};h.advanceFx();await flush();
     assert.equal(h.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot.audioOptimization.enabled,true);assert.equal(h.fxTimerCount(),1);
    }
    assert.equal(h.counts.fxSoundWrites,0);assert.equal(h.counts.saves,0);
    rows.push({boundary,audioReads:h.counts.fxSoundReads,audioWrites:h.counts.fxSoundWrites,timers:h.fxTimerCount()});
   }finally{power.resolve();h.stop();await flush();assert.equal(h.fxTimerCount(),0);assert.equal(h.controllerTimerCount(),0);assert.equal(h.sharedPowerTimerCount(),0);}
  }
  console.log(JSON.stringify({firstAudioReadBoundaries:rows}));
 };
 await verifyFirstAudioRead();

 // Reopening must not borrow the retired observation's stale result, or wait
 // for the ordinary1500ms poll before it can observe the current native facts.
 const {FxSoundOwner}=load(fs.readFileSync(path.join(root,'src/bridge/fxSound.ts'),'utf8'),{'./ipc':{invoke:()=>{throw Error('Use only the original explicit audio port');}}});
 const firstAudio=deferred(),nextAudio=deferred(),audioTimers=new Map();let audioReads=0,audioWrites=0,audioTimerId=0;
 const baseAudio={known:true,installed:true,running:false,enabled:false,selected:true,preset:'inert',output:'inert',reason:''};
 const audioOwner=new FxSoundOwner({read:()=>{audioReads++;return audioReads===1?firstAudio.promise:nextAudio.promise;},set:async()=>{audioWrites++;throw Error('No audio writes in reopen observation');}},(fn,delay)=>{const id=++audioTimerId;audioTimers.set(id,{fn,delay});return id;},id=>audioTimers.delete(id));
 const oldEnd=audioOwner.retain(()=>{});oldEnd();const newEnd=audioOwner.retain(()=>{}),visibleAudio=audioOwner.read();
 try{
  firstAudio.resolve(baseAudio);await flush();
  console.log(JSON.stringify({audioReopenBeforeFresh:{audioReads,audioWrites,timers:audioTimers.size}}));
  assert.equal(audioReads,2,'A reopened audio observation must refresh its retired read immediately');
  assert.equal(audioTimers.size,0,'A retired observer cannot schedule the new observation timer');
  nextAudio.resolve({...baseAudio,running:true,enabled:true});const latestAudio=await visibleAudio;await flush();
  assert.equal(latestAudio.known,true);assert.equal(latestAudio.enabled,true);assert.equal(audioReads,2);assert.equal(audioWrites,0);
  assert.equal(audioTimers.size,1);assert.equal([...audioTimers.values()][0].delay,1500);
  console.log(JSON.stringify({audioReopenAfterFresh:{audioReads,audioWrites,known:latestAudio.known,enabled:latestAudio.enabled,timers:audioTimers.size,delay:[...audioTimers.values()][0].delay}}));
 }finally{newEnd();nextAudio.resolve(baseAudio);await flush();assert.equal(audioTimers.size,0);}
 const reopenBoundaries=[];
 for(const boundary of ['same-epoch','retired-before-settle','rapid-reopen','failed-retired-read','write-in-flight']){
  const prior=deferred(),fresh=deferred(),write=deferred(),timers=new Map();let reads=0,writes=0,id=0,activeReads=0,maximumActive=0;
  const owner=new FxSoundOwner({read:()=>{reads++;activeReads++;maximumActive=Math.max(maximumActive,activeReads);return (reads===1?prior.promise:fresh.promise).finally(()=>{activeReads--;});},set:async()=>{writes++;await write.promise;return {ok:true,copied:false,reused:false,repaired:false,reason:'',state:{...baseAudio,running:true,enabled:true}};}},(fn,delay)=>{const key=++id;timers.set(key,{fn,delay});return key;},key=>timers.delete(key));
  let end=owner.retain(()=>{}),pending=owner.read();let second;
  try{
   if(boundary==='same-epoch'){
    second=owner.read();assert.equal(reads,1);prior.resolve(baseAudio);assert.equal((await pending).known,true);await second;await flush();assert.equal(reads,1);assert.equal(timers.size,1);
   }else if(boundary==='write-in-flight'){
    const mutation=owner.setEnabled(true);second=owner.read();prior.resolve(baseAudio);await pending;await second;await flush();assert.equal(reads,1);assert.equal(timers.size,1);
    write.resolve();assert.equal((await mutation).ok,true);assert.equal(writes,1);
    const timer=[...timers.entries()][0];timers.delete(timer[0]);timer[1].fn();await flush();assert.equal(reads,2);const observed=owner.read();fresh.resolve({...baseAudio,running:true,enabled:true});assert.equal((await observed).enabled,true);await flush();assert.equal(timers.size,1);assert.equal(reads,2);
   }else{
    end();end=owner.retain(()=>{});pending=owner.read();
    if(boundary==='rapid-reopen'){end();end=owner.retain(()=>{});pending=owner.read();second=owner.read();}
    if(boundary==='retired-before-settle')end();
    if(boundary==='failed-retired-read')prior.reject(Error('inert retired audio read failed'));else prior.resolve(baseAudio);
    await flush();
    if(boundary==='retired-before-settle'){await pending;assert.equal(reads,1);assert.equal(timers.size,0);}
    else{assert.equal(reads,2);assert.equal(timers.size,0);fresh.resolve({...baseAudio,running:true,enabled:true});assert.equal((await pending).enabled,true);if(second)assert.equal((await second).enabled,true);await flush();assert.equal(reads,2);assert.equal(timers.size,1);}
   }
   assert.equal(maximumActive,1);assert.equal(writes,boundary==='write-in-flight'?1:0);
   if(timers.size)assert.equal([...timers.values()][0].delay,1500);
   reopenBoundaries.push({boundary,reads,writes,maximumActive,timers:timers.size});
  }finally{end();prior.resolve(baseAudio);fresh.resolve(baseAudio);write.resolve();await flush();assert.equal(timers.size,0);}
 }
 console.log(JSON.stringify({audioReopenBoundaries:reopenBoundaries}));

 const f=await fixture();try{
  f.host.state.fxSound={...f.host.state.fxSound,running:true,enabled:true};f.host.advanceFx();await flush();
  assert.equal(f.client.snapshot().snapshot.audioOptimization.enabled,true);assert.equal(f.host.counts.fxSoundWrites,0);
  f.host.state.fxSound={...f.host.state.fxSound,running:false,enabled:false};f.host.advanceFx();await flush();
  assert.equal(f.client.snapshot().snapshot.audioOptimization.enabled,false);assert.equal(f.host.counts.fxSoundWrites,0);
 }finally{f.stop();assert.equal(f.host.fxTimerCount(),0);}
});
await check('FxSound rejects malformed commands and does not claim failed application',async()=>{
 const f=await fixture();try{
  await assert.rejects(f.client.mutate('audioOptimization.setEnabled',{enabled:'true'}));assert.equal(f.host.counts.fxSoundWrites,0);
  f.host.state.fxSoundSetHook=async()=>({ok:false,copied:false,reused:false,repaired:false,reason:'系统输出待确认',state:f.host.state.fxSound});
  const result=await f.client.mutate('audioOptimization.setEnabled',{enabled:true});await flush();assert.equal(result.applied,false);assert.equal(result.pending,true);assert.equal(f.client.snapshot().snapshot.audioOptimization.enabled,false);
 }finally{f.stop();assert.equal(f.host.fxTimerCount(),0);}
});
await check('FxSound accepted action outlives sidebar close without starting observation again',async()=>{
 for(const boundary of ['hidden','disconnected']){
  const f=await fixture(),pending=deferred();
  try{
   f.host.state.fxSoundSetHook=async()=>pending.promise;
   const operation=f.client.mutate('audioOptimization.setEnabled',{enabled:true}).catch(()=>null);await flush();assert.equal(f.host.counts.fxSoundWrites,1);
   f.client.setVisible(false);
   // Hiding retains the accepted command and its existing observation lease.
   // A real lost socket must notify BOTH ends; host-only fake OFF formerly
   // stranded the client promise outside its virtual timeout clock.
   if(boundary==='disconnected'){f.socket.emit('close');f.host.emit('deckySidebar.mirrorPeer',{connection:'1',runId:'f'.repeat(32),connected:false});}
   await flush();assert.equal(f.host.fxTimerCount(),boundary==='hidden'?1:0);
   pending.resolve({ok:true,copied:false,reused:false,repaired:false,reason:'已开启',state:{...f.host.state.fxSound,running:true,enabled:true}});await operation;await flush();
   assert.equal(f.host.fxTimerCount(),0);assert.equal(f.host.counts.fxSoundWrites,1);
   assert.equal(f.socket.sent.filter(r=>r.command==='audioOptimization.setEnabled').length,1);
  }finally{f.stop();}
 }
});

for(const dedicated of [false,true])await check('the original input action lock remains owned until native completion settles '+dedicated,async()=>{
 const held=deferred(),initial=seed();if(!dedicated)delete initial.entries['game.exe'];const f=await fixture({withGame:true,initial,clearReply:held.promise});try{
  const p=f.client.mutate('game.setField',{field:'padPersona',value:'elite'},f.client.snapshot().snapshot);await flush();assert.equal(f.host.counts.shortcutClears,1);assert.equal(f.host.memory.lock.isQuickActionBusy(),true);assert.equal(f.host.memory.lock.tryAcquireQuickAction('other-YMCC-view'),null);
  held.resolve(true);await flush();assert.equal((await p).saved,true);assert.equal(f.host.memory.lock.isQuickActionBusy(),false);assert.equal(f.host.counts.shortcutClears,1);
 }finally{held.resolve(true);await flush();f.stop();assert.equal(f.timers.size,0);}
});

function renderFxToggle({enabled=false,known=true,connected=true,busy=false}={}) {
 const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
 const source=fs.readFileSync(path.join(root,'decky-plugin/src/index.tsx'),'utf8');
 const start=source.indexOf('const fieldLabels:'),end=source.indexOf('// Steam can reload its shared page before Loader calls onDismount.');
 assert(start>0&&end>start);
 const state={connected,busy,snapshot:{runId:'f'.repeat(32),generation:1,revision:1,ready:true,game:null,fan:{enabled:false,preset:'off',choices:[]},audioOptimization:{known,installed:true,running:enabled,enabled,selected:true,preset:'移动端通用音频优化',output:'fixture',reason:''},actions:{audioOptimization:true,fan:false,game:false}}};
 const sends=[],module={exports:{}},intents={performance:{snapshot:()=>null,cancel(){}},fan:{snapshot:()=>null,hasWork:()=>false,cancel(){}}};
 const React={createElement:(type,props,...children)=>({type,props:props??{},children:children.flat(Infinity)}),useRef:value=>({current:value}),useState:value=>[value,()=>{}],useEffect(){}};
 const globals={module,exports:module.exports,React,SP_REACT:React,client:{snapshot:()=>state,mutate:async(command,args)=>{sends.push({command,args});return {};}},createContentIntents:()=>intents,useQuickAccessVisible:()=>true,PanelSection:'section',PanelSectionRow:'row',ToggleField:'toggle',ButtonItem:'button',SliderField:'slider',DropdownItem:'dropdown',AnchoredDropdown:'anchored',PerformanceControls:'performance',GameProfileControls:'profile',panelStyle:'grouped',FRAME_RATE_CEILINGS:[],FAN_CHOICES:[{data:'off',label:'关闭'}],DECKY_GYRO_CHOICES:[],...load(fs.readFileSync(path.join(root,'src/bridge/losslessProgramState.ts'),'utf8')),activeContentIntents:new Set()};
 vm.runInNewContext(transformSync(source.slice(start,end)+'\nmodule.exports={Content};',{loader:'tsx',format:'cjs',jsxFactory:'React.createElement'}).code,globals);
 const tree=module.exports.Content(),nodes=[];function visit(node){if(!node||typeof node!=='object')return;nodes.push(node);for(const child of node.children??[])visit(child);}visit(tree);
 const label=node=>node.props?.label?.children?.join('')??'';
 const audio=nodes.find(node=>node.type==='toggle'&&label(node)==='掌机专用低音强化');assert(audio,'production TSX must render FxSound ToggleField');
 return {audio,nodes,sends,label};
}
await check('actual Decky TSX places FxSound at software top and sends exactly the toggle boolean',async()=>{
 const f=renderFxToggle();assert.equal(f.audio.props.checked,false);assert.equal(f.audio.props.disabled,false);
 const heading=f.nodes.findIndex(n=>n.props.className==='ymcc-section-heading'&&n.children[0]==='监控/软件'),audio=f.nodes.indexOf(f.audio);
 assert(heading>=0&&heading<audio);const preceding=f.nodes.slice(heading,audio).filter(n=>['toggle','button','slider','dropdown'].includes(n.type));assert.equal(preceding.length,1,'Current requested software group keeps monitor directly above audio');assert.equal(f.label(preceding[0]),'监控总开关');
 f.audio.props.onChange(true);await flush();assert.equal(f.sends.length,1);assert.equal(f.sends[0].command,'audioOptimization.setEnabled');assert.equal(f.sends[0].args.enabled,true);
});
await check('actual FxSound TSX reflects ON and remains disabled for unknown or disconnected state',()=>{
 assert.equal(renderFxToggle({enabled:true}).audio.props.checked,true);
 assert.equal(renderFxToggle({known:false}).audio.props.disabled,true);
 assert.equal(renderFxToggle({connected:false}).audio.props.disabled,true);
 assert.equal(renderFxToggle({busy:true}).audio.props.disabled,true);
});
fs.writeFileSync(path.join(task,'validation/CONTROLS34-CLIENT-HOST.json'),JSON.stringify({status:'passed',cases:results.length,results,actualProductionClientHostRelay:true,memoryIOOnly:true,realFanOrGyroWrites:0,realConfigurationWrites:0},null,2));console.log('Controls34 client/host PASS '+results.length);
