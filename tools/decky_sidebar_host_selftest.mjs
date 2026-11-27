import assert from 'node:assert/strict';
import {root,seed,main} from './fixtures/decky_game_memory_store.mjs';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));await new Promise(resolve=>setImmediate(resolve));};
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true},cases=[];
async function fixture(initial=seed(),options={}){const f=await createMirrorHostFixture({...options,initial});let sequence=0;
 f.read=async()=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;};
 f.request=async(command,args,snapshot)=>{const before=f.replies.length,s=snapshot??await f.read();sequence++;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command,runId,id:'host:'+sequence,clientId:'host',sequence,generation:s.generation,revision:s.revision,identity:command==='performance.setControls'?s.game?.identity??'':s.game?.identity??'',args}});await flush();return f.replies.slice(before).find(x=>x.message.type==='reply')?.message;};
 return f;}
const selected=new Set(),caseArgs=process.argv.slice(2);
const globalInputCase='unknown versus confirmed empty target cannot authorize a global input write from an old receipt';
const freshUnknownFlag='--global-input-receipt=fresh-notified-unknown';
const freshUnknownReceipt=caseArgs.includes(freshUnknownFlag);
if(freshUnknownReceipt){
 assert.deepEqual(caseArgs,['--case',globalInputCase,freshUnknownFlag],'A fresh UNKNOWN receipt diagnostic requires the original global input case');
 caseArgs.pop();
}
const readinessCase='readiness-only failure and same-target recovery repaint once per edge without repeated reads or writes';
const readinessTailFlag='--diagnose-readiness-tail';
const diagnoseReadinessTail=caseArgs.includes(readinessTailFlag);
if(diagnoseReadinessTail){
 assert.deepEqual(caseArgs,['--case',readinessCase,'--readiness-target=confirmed-empty',readinessTailFlag],'Tail diagnostics require the original confirmed-empty readiness case');
 caseArgs.pop();
}
const readinessVariants=caseArgs.filter(arg=>arg==='--readiness-target'||arg.startsWith('--readiness-target='));
const confirmedEmptyOnly=readinessVariants.length>0;
if(confirmedEmptyOnly){
 assert.deepEqual(caseArgs,['--case',readinessCase,'--readiness-target=confirmed-empty'],'The readiness target requires the exact original readiness case');
 caseArgs.pop();
}
for(let i=0;i<caseArgs.length;i+=2){const name=caseArgs[i+1];assert(caseArgs[i]==='--case'&&name&&!selected.has(name),'Use --case with a unique exact name');selected.add(name);}
const registered=new Set();
async function check(name,fn){assert(!registered.has(name),name);registered.add(name);if(selected.size&&!selected.has(name))return;try{await fn();cases.push(name);}catch(error){console.error(JSON.stringify({failedCase:name,passedBeforeFailure:cases.length,error:String(error)},null,2));throw error;}}
await check('ready lifecycle has no eager reads or saves',async()=>{const f=await fixture();assert.equal(f.counts.power,0);assert.equal(f.counts.saves,0);f.ready();await flush();assert.equal(f.counts.power,0);assert.equal(f.counts.saves,0);f.stop();});
await check('snapshot exposes continuous performance owner and no old six-tier/core mirror fields',async()=>{const f=await fixture();f.ready();const s=await f.read();assert.equal(s.ready,true);assert.equal(s.actions.performance,true);assert.equal(s.performance.scope,'game');assert.equal(s.performance.side,'ac');assert.equal(s.performance.controls.find(c=>c.key==='tdp').value,20);assert(!Object.hasOwn(s.game.fields,'acMode'));assert(!Object.hasOwn(s.game.fields,'dcMode'));assert(!Object.hasOwn(s.game.fields,'corePolicyMode'));assert(!Object.hasOwn(s.game.fields,'hyperThreadPolicy'));assert.equal(f.counts.capabilities,0);assert.equal(f.counts.saves,0);f.stop();});
await check('old game mode/core commands are rejected without a save',async()=>{const f=await fixture();f.ready();const s=await f.read();for(const [field,value] of [['acMode','performance'],['dcMode','extreme'],['corePolicyMode','only-small'],['hyperThreadPolicy','off']]){const r=await f.request('game.setField',{field,value},s);assert.equal(r.ok,false,field);}assert.equal(f.counts.saves,0);f.stop();});
const ownerTdpDiagnostics=f=>f.diagnostics.filter(row=>row.event==='tdp-owner-settled');
await check('performance patch crosses host relay and saves one shared owner bank',async()=>{const f=await fixture();f.ready();const s=await f.read();const r=await f.request('performance.setControls',{patch:{tdp:22}},s);assert.equal(r.ok,true);assert.equal(f.memory.raw().entries['game.exe'].controls.ac.tdp,22);assert.equal(f.memory.raw().entries['game.exe'].controls.dc.tdp,10);assert.equal(f.counts.performanceUpdates,1);assert.equal(r.result.applied,false);assert.equal(r.result.pending,true);assert.deepEqual(ownerTdpDiagnostics(f),[{event:'tdp-owner-settled',generation:s.generation,requestedW:22,outcome:'saved-unconfirmed'}]);f.stop();});
await check('performance TDP owner receipt does not await diagnostics or record a replay',async()=>{
 let failDiagnostic;
 const f=await fixture(seed(),{readHooks:{performanceReceipt:patch=>patch.tdp===undefined?undefined:{ok:true,proof:'write-ack',requested:patch.tdp,accepted:patch.tdp},diagnostic:row=>row.event==='tdp-owner-settled'?new Promise((_,reject)=>{failDiagnostic=reject;}):undefined}});
 f.ready();const s=await f.read(),args={patch:{tdp:23}};
 const reply=await f.request('performance.setControls',args,s);
 assert.equal(reply.ok,true);assert.equal(reply.result.applied,true);assert.equal(reply.result.pending,false);
 const record={event:'tdp-owner-settled',generation:s.generation,requestedW:23,outcome:'write-ack'};
 assert.deepEqual(ownerTdpDiagnostics(f),[record]);assert.equal(typeof failDiagnostic,'function');
 failDiagnostic(Error('private diagnostic failure'));await flush();
 const before=f.replies.length;
 f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'performance.setControls',runId,id:'host:1',clientId:'host',sequence:1,generation:s.generation,revision:s.revision,identity:s.game?.identity??'',args}});
 await flush();assert.deepEqual(f.replies.slice(before).find(x=>x.message.type==='reply')?.message,reply);
 assert.equal(f.counts.performanceUpdates,1);assert.deepEqual(ownerTdpDiagnostics(f),[record]);
 const other=await f.request('performance.setControls',{patch:{bias:35}});assert.equal(other.ok,true);
 assert.equal(f.counts.performanceUpdates,2);assert.deepEqual(ownerTdpDiagnostics(f),[record]);f.stop();
});
await check('input edit preserves full continuous controls',async()=>{const f=await fixture();f.ready();const s=await f.read();await f.request('performance.setControls',{patch:{tdp:24}},s);const fresh=await f.read();const before=structuredClone(f.memory.raw().entries['game.exe'].controls);const r=await f.request('game.setField',{field:'padPersona',value:'elite'},fresh);assert.equal(r.ok,true);assert.deepEqual(f.memory.raw().entries['game.exe'].controls,before);f.stop();});
await check('performance source and power generation reject stale writes',async()=>{const f=await fixture();f.ready();const s=await f.read();f.state.power={...f.state.power,generation:2};f.powerChanged();await flush();const r=await f.request('performance.setControls',{patch:{tdp:25}},s);assert.equal(r.ok,false);assert.equal(f.counts.performanceUpdates??0,0);assert.deepEqual(ownerTdpDiagnostics(f),[]);f.stop();});
await check('wake held by native power stays connected read-only then recovers after committed resume',async()=>{
 const f=await fixture(seed(),{invoke:async command=>command==='deckySidebar.mirrorAttach'?{connected:false,owner:'YMCC-native',pid:8612}:true});
 try{
 f.ready();const before=await f.read();
 f.state.power={...f.state.power,generation:2,phase:'suspended',inputReady:false,resumeReady:false,hardwareWritesAllowed:false};
 f.emit('power.lifecycleChanged',{...f.state.power,known:true,source:'shared-power-owner-current'});const held=await f.read();
 assert.equal(held.ready,false);assert.match(held.notice,/已连接 YMCC.*等待唤醒完成/);assert.equal(held.provenance.kind,'ymcc-native');
 const rejected=await f.request('performance.setControls',{patch:{tdp:25}},held);assert.equal(rejected.ok,false);assert.equal(f.counts.performanceUpdates??0,0);
 f.emit('power.resumed',{generation:2});const hinted=await f.read();assert.equal(hinted.ready,false);assert.equal(hinted.generation,2);
 f.state.power={...f.state.power,phase:'ready',inputReady:false,resumeReady:true,hardwareWritesAllowed:true};
 f.emit('power.lifecycleChanged',{...f.state.power,known:true,source:'shared-power-owner-current'});
 const after=await f.read();assert.equal(after.ready,true);assert.equal(after.generation,2);assert.equal(f.counts.performanceUpdates??0,0);
 // Native can publish input recovery after the current power gate is already open.
 const reads=()=>({power:f.counts.power,schedule:f.counts.schedule,packets:f.replies.filter(x=>x.message.type==='snapshot').length});
 await flush();const steady=reads();
 f.state.power={...f.state.power,inputReady:true};
 f.emit('power.lifecycleChanged',{...f.state.power,known:true,source:'wake-input-recovered'});await flush();
 const inputOnly=reads();console.log(JSON.stringify({wakeInputOnly:{before:steady,after:inputOnly}}));
 assert.deepEqual(inputOnly,steady,'input-only recovery must not read or publish the entire Decky sidebar');
 assert.equal(f.counts.performanceUpdates??0,0);
 }finally{f.stop();}
 const merged=await fixture();
 try{
  Object.assign(merged.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false});
  merged.ready();const before=await merged.read();assert.equal(before.ready,true);
  const until=merged.replies.length,priorReads=merged.counts.schedule;
  // The coordinator knows a newer scope is pending, while both Native reads still return old Ready.
  merged.emit('power.resumed',{generation:1,readbackScope:{suspendSerial:4,notificationSequence:22}});
  const held=await merged.read();
  assert.equal(held.ready,false,'scoped public readback must fence a stale direct Ready');
  assert.equal(held.actions.performance,false);
  assert.equal(merged.counts.schedule,priorReads,'a fenced snapshot must not start business reads');
  assert.equal(merged.replies.slice(until).some(x=>x.message.type==='snapshot'&&x.message.snapshot.ready),false);
  assert.notEqual((await merged.request('performance.setControls',{patch:{tdp:25}},before))?.ok,true);
  assert.equal(merged.counts.performanceUpdates??0,0);
  Object.assign(merged.state.power,{suspendSerial:4,observedNotificationSequence:22,publicationRevision:11});
  const resumed=merged.replies.length;
  merged.emit('power.lifecycleChanged',{known:true,...merged.state.power});await flush();
  assert.equal(merged.replies.slice(resumed).filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot.ready,true,
    'current public Ready should restore the sidebar without a new request');
 }finally{merged.stop();}
 let hold=false,releaseRead;
 const delayed=await fixture(seed(),{readHooks:{power:()=>{
  if(!hold)return;hold=false;return new Promise(resolve=>{releaseRead=resolve;});
 }}});
 try{
  Object.assign(delayed.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false});
  const before=structuredClone(delayed.state.power);
  delayed.ready();assert.equal((await delayed.read()).ready,true);
  const first=delayed.replies.length,priorReads=delayed.counts.schedule;
  hold=true;delayed.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,probeId:'probe:late-power:1'}});
  await flush();assert.equal(typeof releaseRead,'function');
  delayed.emit('power.resumed',{generation:1,readbackScope:{suspendSerial:3,notificationSequence:22}});await flush();
  Object.assign(delayed.state.power,{phase:'resuming',hardwareWritesAllowed:false,observedNotificationSequence:22,publicationRevision:11});
  delayed.emit('power.lifecycleChanged',{known:true,...delayed.state.power});await flush();
  releaseRead(before);await flush();
  const held=delayed.replies.slice(first).filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
  assert.equal(held?.ready,false,'a late direct Ready cannot replace the confirmed current non-Ready fact');
  assert.equal(delayed.counts.schedule,priorReads,'a superseded in-flight read must not start business reads');
  Object.assign(delayed.state.power,{phase:'ready',hardwareWritesAllowed:true,publicationRevision:12});
  const resumed=delayed.replies.length;
  delayed.emit('power.lifecycleChanged',{known:true,...delayed.state.power});await flush();
  assert.equal(delayed.replies.slice(resumed).filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot.ready,true);
 }finally{releaseRead?.();delayed.stop();}
});
await check('global performance owner is exposed when no game is confirmed',async()=>{const f=await fixture();f.state.game=null;f.ready();const s=await f.read();assert.equal(s.game,null);assert.equal(s.performance.scope,'global');assert.equal(s.actions.performance,true);f.stop();});
await check('stop removes observers and late requests cannot save',async()=>{const f=await fixture();f.ready();const s=await f.read();f.stop();assert.equal(f.observerStats().watchers,0);const r=await f.request('performance.setControls',{patch:{tdp:25}},s);assert.equal(r,undefined);assert.equal(f.counts.performanceUpdates??0,0);});
// Slider requests can race an unrelated host refresh before its next snapshot
// reaches Decky. Execute the actual host, relay and original owner in this suite.
for(const [command,args,count]of [
 ['performance.setControls',{patch:{tdp:25}},'performanceUpdates'],
 ['frame.setField',{field:'acFps',value:'45'},'frameApplies'],
])await check(command+' accepts one delivered unchanged control receipt during a monitor refresh',async()=>{
 const f=await fixture();f.ready();const s=await f.read();
 f.state.monitor.enabled=!f.state.monitor.enabled;f.monitorChanged();
 const r=await f.request(command,args,s);assert.equal(r.ok,true,'unrelated refresh must not discard a valid slider request');
 assert.equal(f.counts[count],1,'slider reaches its original apply owner once');
 if(command==='performance.setControls')assert.equal(f.memory.raw().entries['game.exe'].controls.ac.tdp,25);
 else assert.equal(f.memory.raw().rtss['game.exe'].acFps,45);
 f.stop();
});
for(const [name,change]of [
 ['performance bank',async f=>{const v=structuredClone(f.memory.store.getGameCustomConfig());v.entries['game.exe'].controls={...v.entries['game.exe'].controls,ac:{...v.entries['game.exe'].controls?.ac,tdp:24}};await f.memory.store.saveGameCustomConfig(v);}],
 ['power source',f=>f.state.powerSource='dc'],
 ['power generation',f=>f.state.power.generation++],
 ['process birth',f=>f.state.game.processCreated='reused-pid'],
])await check('slider receipt cannot cross changed '+name+' during the same refresh race',async()=>{
 const f=await fixture();f.ready();const s=await f.read();await change(f);f.monitorChanged();
 const r=await f.request('performance.setControls',{patch:{tdp:25}},s);
 assert.equal(r.ok,false);assert.equal(f.counts.performanceUpdates??0,0);f.stop();
});


await check('sidebar entry asks shared game owner once and repeated snapshots stay passive',async()=>{
 const f=await fixture();f.ready();await f.read();assert.equal(f.counts.gameReconciles,1);for(let i=0;i<5;i++)await f.read();assert.equal(f.counts.gameReconciles,1);
 f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();await f.read();assert.equal(f.counts.gameReconciles,2);assert.equal(f.counts.saves,0);f.stop();
});
await check('sidebar entry recovers an empty cached game through the shared owner and rejects old-game controls',async()=>{
 let f,release;f=await fixture(seed(),{policyRefresh:async()=>{await new Promise(resolve=>release=resolve);f.state.game={pid:11752,processCreated:'134372974230246308',name:'tmnt',title:"Teenage Mutant Ninja Turtles: Shredder's Revenge",path:'E:/GAME/TMNT.exe',ts:1792823823460};f.gameChanged();return f.state.game;}});
 f.state.game=null;f.ready();const before=await f.read();assert.equal(before.game,null);release();await flush();const after=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
 assert(after.game.identity.includes('11752'));assert.equal(after.game.label,"Teenage Mutant Ninja Turtles: Shredder's Revenge");assert.equal(f.counts.gameReconciles,1);assert.equal(f.counts.saves,0);f.stop();
});
await check('an unavailable game refresh does not break the connected read-only sidebar',async()=>{
 const f=await fixture(seed(),{policyRefresh:async()=>{throw Error('inert game detect timeout');}});f.state.game=null;f.ready();const s=await f.read();assert.equal(s.ready,true);assert.equal(s.game,null);assert.equal(f.counts.gameReconciles,1);assert.equal(f.counts.saves,0);f.stop();
});


await check('same PID and birth with a new native lease rejects an old sidebar receipt without a source notification',async()=>{
 const f=await fixture();f.state.game={...f.state.game,generation:10,identityGeneration:4};f.ready();const old=await f.read();f.state.game={...f.state.game,generation:11,identityGeneration:5};
 const rejected=await f.request('game.setField',{field:'padPersona',value:'elite'},old);assert.equal(rejected.ok,false);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);
 f.gameChanged();const fresh=await f.read();assert.notEqual(fresh.game.identity,old.game.identity);const accepted=await f.request('game.setField',{field:'padPersona',value:'elite'},fresh);
 assert.equal(accepted.ok,true);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,1);assert.equal(f.state.input.outputTarget.persona,'elite');f.stop();
});
await check('pending shared recognition rejects game-dependent controls while pure monitor and audio remain usable',async()=>{
 const f=await fixture();f.ready();const old=await f.read();f.state.gameCurrent=false;
 for(const [command,args] of [['performance.setControls',{patch:{tdp:25}}],['game.setField',{field:'padPersona',value:'elite'}],['game.setField',{field:'profile',value:'global'}],['game.setField',{field:'speedFactor',value:'2'}],['game.setField',{field:'trainer',value:'launch'}],['game.setField',{field:'losslessScaling',value:'on'}],['frame.setField',{field:'acFps',value:'45'}]]){
  const r=await f.request(command,args,old);assert.equal(r.ok,false,command+':'+JSON.stringify(args));
 }
 assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.counts.frameApplies,0);assert.equal(f.counts.speedApply,0);assert.equal(f.counts.trainerLaunches,0);
 const pending=await f.read();assert.equal(pending.actions.game,false);assert.equal(pending.actions.performance,false);assert.equal(pending.actions.frames,false);assert.equal(pending.actions.monitor,true);assert.equal(pending.actions.audioOptimization,true);
 const monitor=await f.request('monitor.setEnabled',{enabled:false},pending);assert.equal(monitor.ok,true);assert.equal(f.counts.monitorSaves,1);
 const audio=await f.request('audioOptimization.setEnabled',{enabled:true},await f.read());assert.equal(audio.ok,true);assert.equal(f.counts.fxSoundWrites,1);f.stop();
});
await check(globalInputCase,async()=>{
 const f=await fixture();
 try{
  f.state.game=null;f.ready();const old=await f.read();assert.equal(old.actions.global,true);
  if(freshUnknownReceipt){
   const snapshots=()=>f.replies.filter(row=>row.message.type==='snapshot'&&row.message.displayOnly!==true);
   const start=snapshots().length;
   assert(f.state.gameCurrent&&old.ready&&old.game===null);
   f.setGameReadiness(false);await flush();
   assert.equal(f.state.gameCurrent,false);
   assert.equal(snapshots().length,start+1,'Expected one actionable snapshot pushed by the UNKNOWN edge');
   const {snapshot}=snapshots().at(-1).message;
   assert(snapshot.ready&&snapshot.game===null&&snapshot.generation===old.generation&&snapshot.revision>old.revision,'Require a newly issued receipt in the same generation');
   const state=()=>({inputSaves:f.counts.inputSaves,gameSaves:f.counts.saves,inputRevision:f.state.input.revision,persona:f.state.input.outputTarget.persona,shortcutClears:f.counts.shortcutClears??0});
   const before=state();
   const reply=await f.request('global.setField',{field:'padPersona',value:'elite'},snapshot);
   const replyFacts=value=>({ok:value?.ok??null,saved:value?.result?.saved??null,applied:value?.result?.applied??null,pending:value?.result?.pending??null});
   const receiptFacts=value=>({generation:value.generation,revision:value.revision,globalAllowed:value.actions.global});
   const after=state(),result=replyFacts(reply);
   console.log(JSON.stringify({freshUnknownGlobalInput:{old:{generation:old.generation,revision:old.revision},receipt:receiptFacts(snapshot),
    targetCurrent:f.state.gameCurrent,before,after,reply:result}}));
   // Observe the recovery arm even when the unknown arm has a known failure.
   const beforeRecovery=snapshots().length;
   f.setGameReadiness(true);await flush();
   const recovered=snapshots().at(-1)?.message.snapshot,recoveredState=state();
   assert.equal(snapshots().length,beforeRecovery+1,'Confirmed empty recovery must publish exactly one actionable receipt');
   assert(recovered?.ready&&recovered.game===null&&recovered.generation===snapshot.generation&&recovered.revision>snapshot.revision,
    'Recovery must re-authorize the same generation with a fresh confirmed-empty receipt');
   const persona=after.persona==='elite'?'steamdeck':'elite';
   const patch={field:'padPersona',value:persona};
   const stale=replyFacts(await f.request('global.setField',patch,snapshot)),staleState=state();
   const positive=replyFacts(await f.request('global.setField',patch,recovered)),positiveState=state();
   console.log(JSON.stringify({confirmedEmptyGlobalInputRecovery:{receipt:receiptFacts(recovered),targetCurrent:f.state.gameCurrent,
    persona,recoveredState,stale:{reply:stale,after:staleState},current:{reply:positive,after:positiveState}}}));
   assert.deepEqual(recoveredState,after,'Target readiness itself must not persist or invoke the input runtime');
   assert.equal(stale.ok,false,'The UNKNOWN receipt must not regain write authority after recovery');
   assert.deepEqual(staleState,after,'A stale receipt must not persist or invoke the input runtime');
   assert.equal(recovered.actions.global,true,'Confirmed empty must restore global input admission');
   assert.deepEqual(positive,{ok:true,saved:true,applied:false,pending:true});
   assert.deepEqual(positiveState,{...after,inputSaves:after.inputSaves+1,inputRevision:after.inputRevision+1,persona,shortcutClears:after.shortcutClears+1},
    'The current recovered receipt must save exactly once through the original owner without claiming hardware application');
   assert.deepEqual({globalAllowed:snapshot.actions.global,ok:result.ok,after},{globalAllowed:false,ok:false,after:before},'Fresh UNKNOWN receipt must not advertise or save global input under the original fixture contract');
   return;
  }
  f.state.gameCurrent=false;
  const inputState=()=>({inputSaves:f.counts.inputSaves,gameSaves:f.counts.saves,inputRevision:f.state.input.revision,persona:f.state.input.outputTarget.persona});
  const before=inputState();
  const r=await f.request('global.setField',{field:'padPersona',value:'elite'},old);
  const after=inputState(),fresh=await f.read();
  // The unfiltered parent consumes stdout as one JSON document.
  const logObservation=selected.size?console.log:console.error;
  logObservation(JSON.stringify({staleGlobalInput:{receipt:{generation:old.generation,revision:old.revision},targetCurrent:f.state.gameCurrent,
   before,after,reply:{ok:r?.ok??null,saved:r?.result?.saved??null,applied:r?.result?.applied??null,pending:r?.result?.pending??null},
   fresh:{generation:fresh?.generation??null,revision:fresh?.revision??null,globalAllowed:fresh?.actions?.global??null}}}));
  assert.deepEqual({ok:r?.ok,inputSaves:after.inputSaves,globalAllowed:fresh?.actions?.global},{ok:false,inputSaves:0,globalAllowed:false});
 }finally{f.stop();}
});
await check('global input refuses a stale power receipt across a new public suspend scope',async()=>{
 for(const [boundary,hasGame] of [['readback-pending',false],['coalesced-ready',false],['coalesced-ready',true]]){
  let release,reads=0,holdAt=0;
  const gate=new Promise(resolve=>release=resolve);
  const f=await fixture(seed(),{readHooks:{power:()=>holdAt&&++reads===holdAt?gate:undefined}});
  try{
   if(!hasGame)f.state.game=null;
   Object.assign(f.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false});
   const original=structuredClone(f.state.power),before=f.replies.length;
   f.ready();const old=await f.read();assert.equal(old.actions.global,!hasGame);assert.equal(old.actions.game,hasGame);
   holdAt=boundary==='coalesced-ready'?2:1;
   const command=hasGame?'game.setField':'global.setField',patch={field:'padPersona',value:'elite'};
   assert.equal(await f.request(command,patch,old),undefined);
   assert.equal(reads,holdAt,'The original input owner must be waiting on its power read');
   assert.equal(f.counts.inputSaves,0);
   f.emit('power.resumed',{generation:old.generation,readbackScope:{suspendSerial:4,notificationSequence:22}});
   const current={...original,suspendSerial:4,observedNotificationSequence:22,publicationRevision:11};
   if(boundary==='coalesced-ready'){
    Object.assign(f.state.power,current);
    f.emit('power.lifecycleChanged',{known:true,...current});
   }
   await flush();holdAt=0;release(original);await flush();
   const reply=f.replies.slice(before).find(x=>x.message.type==='reply')?.message;
   console.log(JSON.stringify({globalInputPowerBoundary:boundary,hasGame,ok:reply?.ok,inputSaves:f.counts.inputSaves}));
   assert.equal(reply?.ok,false,boundary);assert.equal(f.counts.inputSaves,0);
   if(boundary==='readback-pending'){
    Object.assign(f.state.power,current);
    f.emit('power.lifecycleChanged',{known:true,...current});await flush();
   }
   const fresh=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
   assert(fresh?.ready&&(hasGame?fresh.actions.game:fresh.actions.global)&&fresh.revision>old.revision);
   assert.equal((await f.request(command,patch,old))?.ok,false);
   assert.equal(f.counts.inputSaves,0);
   assert.equal((await f.request(command,patch,fresh))?.ok,true);
   assert.equal(f.counts.inputSaves,1);
  }finally{release();f.stop();}
 }
 // The input owner may already have performed its durable CAS. Finish that
 // request without replaying its save or clearing InputHost in the new scope.
 let releaseCommit;
 const commitGate=new Promise(resolve=>releaseCommit=resolve);
 const f=await fixture(seed(),{readHooks:{inputCommit:()=>commitGate}});
 try{
  f.state.game=null;
  Object.assign(f.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false});
  f.ready();const old=await f.read(),before=f.replies.length;
  const request={command:'global.setField',args:{field:'padPersona',value:'elite'},runId,id:'committed:1',clientId:'committed',sequence:1,generation:old.generation,revision:old.revision,identity:''};
  const send=()=>f.emit('deckySidebar.mirrorRequest',{...peer,request});
  send();await flush();assert.equal(f.counts.inputSaves,1);assert.equal(f.counts.shortcutClears??0,0);
  Object.assign(f.state.power,{suspendSerial:4,observedNotificationSequence:22,publicationRevision:11});
  f.emit('power.lifecycleChanged',{known:true,...f.state.power});
  await flush();releaseCommit();await flush();
  const replies=()=>f.replies.slice(before).filter(x=>x.message.type==='reply').map(x=>x.message);
  assert.equal(replies().length,1);
  const settled=replies()[0];
  assert.equal(settled.ok,true);assert.equal(settled.result.saved,true);assert.equal(settled.result.applied,false);
  send();await flush();assert.deepEqual(replies(),[settled,settled]);
  assert.equal(f.counts.inputSaves,1);assert.equal(f.counts.shortcutClears??0,0);
 }finally{releaseCommit();f.stop();}
});

for(const edge of ['coalesced suspend','same-suspend notification'])await check(edge==='coalesced suspend'?
 'sidebar writes require the public scope that admitted the request':'same-suspend notification retires old sidebar writes',async()=>{
 const outcomes=[];
 for(const [command,args,action,count] of [
  ['performance.setControls',{patch:{tdp:25}},'performance','performanceUpdates'],
  ['frame.setField',{field:'acFps',value:'45'},'frames','saves'],
  ['touchpad.setField',{field:'layout',value:'single'},'touchpads','touchpadSaves'],
  ['monitor.setEnabled',{enabled:false},'monitor','monitorSaves'],
  ['audioOptimization.setEnabled',{enabled:true},'audioOptimization','fxSoundWrites'],
 ]){
  let release,hold=false,entered=false;
  const gate=new Promise(resolve=>release=resolve);
  const f=await fixture(seed(),{readHooks:{power:()=>{if(hold&&!entered){entered=true;return gate;}}}});
  try{
   Object.assign(f.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false});
   const original=structuredClone(f.state.power);
   f.ready();const old=await f.read();assert.equal(old.actions[action],true,command);
   const start=f.replies.length;
   hold=true;assert.equal(await f.request(command,args,old),undefined);assert(entered);
   assert.equal(f.counts[count]??0,0);
   // Publication metadata alone should neither retire work nor repaint.
   const snapshots=()=>f.replies.filter(x=>x.message.type==='snapshot').length;
   const offered=snapshots();
   Object.assign(f.state.power,{publicationRevision:11});
   f.emit('power.lifecycleChanged',{known:true,...f.state.power});await flush();
   assert.equal(snapshots(),offered);
   assert.equal(f.counts[count]??0,0);
   const nextPower={suspendSerial:edge==='coalesced suspend'?4:3,observedNotificationSequence:22,publicationRevision:12};
   if(edge==='coalesced suspend')f.emit('power.resumed',{generation:old.generation,readbackScope:{suspendSerial:4,notificationSequence:22}});
   else {
    Object.assign(f.state.power,nextPower);
    f.emit('power.lifecycleChanged',{known:true,...f.state.power});
   }
   await flush();hold=false;release(original);await flush();
   const settled=f.replies.slice(start).find(x=>x.message.type==='reply')?.message;
   const oldWrites=f.counts[count]??0;
   Object.assign(f.state.power,nextPower);
   f.emit('power.lifecycleChanged',{known:true,...f.state.power});await flush();
   const fresh=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
   assert(fresh?.ready&&fresh.actions[action]&&fresh.revision>old.revision,command);
   const next=await f.request(command,args,fresh);
   outcomes.push({command,oldOk:settled?.ok,oldWrites,newOk:next?.ok,newWrites:(f.counts[count]??0)-oldWrites});
  }finally{release();f.stop();}
 }
 (selected.size?console.log:console.error)(JSON.stringify({edge,sidebarPublicMutationScopes:outcomes}));
 assert.deepEqual(outcomes,outcomes.map(({command})=>({command,oldOk:false,oldWrites:0,newOk:true,newWrites:1})));
});

await check('direct native readback retires the old sidebar scope without a public broadcast',async()=>{
 const outcomes=[];
 for(const [command,args,nextArgs,action,count,suspendSerial] of [
  ['performance.setControls',{patch:{tdp:25}},{patch:{tdp:26}},'performance','performanceUpdates',3],
  ['frame.setField',{field:'acFps',value:'45'},{field:'acFps',value:'50'},'frames','saves',4],
  ['touchpad.setField',{field:'layout',value:'single'},{field:'layout',value:'single'},'touchpads','touchpadSaves',3],
  ['monitor.setEnabled',{enabled:false},{enabled:false},'monitor','monitorSaves',4],
  ['audioOptimization.setEnabled',{enabled:true},{enabled:true},'audioOptimization','fxSoundWrites',3],
 ]){
  let armed=false,hold=false,release;
  const pending=new Promise(resolve=>release=resolve);
  const f=await fixture(seed(),{readHooks:{power:signal=>{if(signal&&armed){armed=false;return hold?pending:next;}},sharedPower:()=>hold?pending:undefined}});
  const oldPower={...f.state.power,known:true,suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false};
  const next={...oldPower,suspendSerial,observedNotificationSequence:22,publicationRevision:11};
  try{
   Object.assign(f.state.power,oldPower);f.ready();const old=await f.read();assert.equal(old.actions[action],true,command);
   Object.assign(f.state.power,next);armed=true;
   const stale=await f.request(command,args,old),oldWrites=f.counts[count]??0;
   const shared=f.sharedPowerDiagnostics().snapshot?.observedNotificationSequence;
   const fresh=await f.read();
   assert.equal('powerAdmission' in fresh,false,'sidebar wire must not export private power admission');
   // The old read/publication can arrive after the direct current receipt.
   f.emit('power.lifecycleChanged',{...oldPower,publicationRevision:11});await flush();
   const afterLate=f.sharedPowerDiagnostics().snapshot?.observedNotificationSequence;
   const observed=f.replies.filter(x=>x.message.type==='snapshot').length;
   if(action==='frames'){
    armed=true;hold=true;assert.equal(await f.request(command,nextArgs,fresh),undefined);
    f.emit('power.lifecycleChanged',next);await flush();
    assert.equal(f.replies.filter(x=>x.message.type==='snapshot').length,observed);
    hold=false;release(next);await flush();
   }else await f.request(command,nextArgs,fresh);
   const recovered=f.replies.filter(x=>x.message.type==='reply').at(-1)?.message.ok===true;
   outcomes.push({action,stale:stale?.ok,oldWrites,shared,afterLate,recovered,newWrites:(f.counts[count]??0)-oldWrites});
  }finally{release(next);f.stop();}
 }
 console.log(JSON.stringify({nativePowerReadbackBeforeBroadcast:outcomes}));
 assert.deepEqual(outcomes,outcomes.map(({action})=>({action,stale:false,oldWrites:0,shared:22,afterLate:22,recovered:true,newWrites:1})));
});

await check('a direct pending receipt fences old Ready until current Native admission',async()=>{
 let override;
 const f=await fixture(seed(),{readHooks:{power:()=>override}});
 const old={...f.state.power,known:true,suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false};
 const pending={...old,suspendSerial:4,observedNotificationSequence:22,phase:'suspending',notificationPending:true,hardwareWritesAllowed:false};
 try{
  Object.assign(f.state.power,old);f.ready();const snapshot=await f.read();
  Object.assign(f.state.power,pending);
  const result=await f.request('frame.setField',{field:'acFps',value:'45'},snapshot);
  assert.equal(result?.ok,false);assert.equal(f.counts.saves,0);
  const observed=f.sharedPowerDiagnostics().snapshot;
  assert.equal(observed.observedNotificationSequence,22);assert.equal(observed.notificationPending,true);
  // Even an old-cohort receipt with a matching publication revision cannot reopen the view.
  override=old;f.emit('power.lifecycleChanged',old);await flush();
  const denied=await f.read();assert.equal(denied.ready,false);
  assert.equal(f.counts.saves,0);assert.equal(f.memory.lock.isQuickActionBusy(),false);
  override=undefined;
  Object.assign(f.state.power,old,{suspendSerial:4,observedNotificationSequence:22,publicationRevision:11});
  f.advanceSharedPower(1000);await flush();
  const current=await f.read();assert.equal(current.ready,true);
  const next=await f.request('frame.setField',{field:'acFps',value:'50'},current);
  assert.equal(next?.result?.saved,true);assert.equal(f.counts.saves,1);assert.equal(f.counts.frameApplies,1);
  assert.equal(f.sharedPowerDiagnostics().snapshot?.notificationPending,false);
 }finally{f.stop();}
});

await check('retired power reads release the sidebar action and original frame lock before a late reply',async()=>{
 let armed=false,held,release,aborts=0;
 const late=new Promise(resolve=>release=resolve);
 const f=await fixture(seed(),{readHooks:{power:signal=>{
  if(!armed)return;
  armed=false;held=structuredClone(f.state.power);
  return new Promise((resolve,reject)=>{
   const abort=()=>{aborts++;reject(Error('fixture power IPC retired'));};
   signal?.addEventListener('abort',abort,{once:true});
   void late.then(()=>{signal?.removeEventListener('abort',abort);resolve(held);});
  });
 }}});
 try{
  Object.assign(f.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10});
  f.ready();const old=await f.read();assert.equal(old.actions.frames,true);
  armed=true;assert.equal(await f.request('frame.setField',{field:'acFps',value:'45'},old),undefined);
  assert(held&&f.memory.lock.isQuickActionBusy());
  const replies=()=>f.replies.filter(x=>x.message.type==='reply').map(({message})=>[message.id,message.ok]);
  Object.assign(f.state.power,{observedNotificationSequence:22,publicationRevision:11});
  f.emit('power.lifecycleChanged',{known:true,...f.state.power});await flush();
  const oldSettled=replies().some(([id,ok])=>id==='host:1'&&ok===false),locked=f.memory.lock.isQuickActionBusy();
  const fresh=await f.read();
  const next=await f.request('frame.setField',{field:'acFps',value:'50'},fresh);
  const beforeLate={aborts,oldSettled,locked,newSaved:next?.result?.saved===true,saves:f.counts.saves,applies:f.counts.frameApplies};
  release();await flush();
  (selected.size?console.log:console.error)(JSON.stringify({retiredFramePowerRead:beforeLate,replies:replies(),saves:f.counts.saves}));
  assert.deepEqual(beforeLate,{aborts:1,oldSettled:true,locked:false,newSaved:true,saves:1,applies:1});
  assert.deepEqual(replies(),[['host:1',false],['host:2',true]]);
  assert.equal(f.counts.saves,1);
 }finally{release();f.stop();}
});

await check('saved monitor switch waits for the current public scope before applying',async()=>{
 const outcomes=[];
 for(const [enabled,ending,expected] of [[false,'ready',1],[true,'ready',1],[true,'hide',1],[true,'superseded',0],[true,'stop',0]]){
  let release,entered=false;
  const gate=new Promise(resolve=>release=resolve);
  const f=await fixture(seed(),{readHooks:{monitor:()=>{if(f.counts.monitorSaves===1&&!entered){entered=true;return gate;}}}});
  try{
   f.state.monitor.enabled=!enabled;
   Object.assign(f.state.power,{suspendSerial:3,observedNotificationSequence:21,publicationRevision:10,notificationPending:false});
   f.ready();const old=await f.read();assert.equal(old.actions.monitor,true);
   const start=f.replies.length;
   assert.equal(await f.request('monitor.setEnabled',{enabled},old),undefined);assert(entered);
   assert.equal(f.counts.monitorSaves,1);
   f.emit('power.resumed',{generation:old.generation,readbackScope:{suspendSerial:4,notificationSequence:22}});
   await flush();release();await flush();
   const reply=f.replies.slice(start).find(x=>x.message.type==='reply')?.message;
   const beforeReady=f.counts.monitorApplies;
   if(ending==='hide')f.emit('deckySidebar.mirrorPeer',{...peer,connected:false});
   if(ending==='superseded'){f.state.monitor.enabled=false;f.monitorChanged();}
   if(ending==='stop')f.stop();
   Object.assign(f.state.power,{suspendSerial:4,observedNotificationSequence:22,publicationRevision:11});
   f.emit('power.lifecycleChanged',{known:true,...f.state.power});await flush();
   f.emit('power.lifecycleChanged',{known:true,...f.state.power});await flush();
   outcomes.push({enabled,ending,saved:reply?.result?.saved,pending:reply?.result?.pending,beforeReady,afterReady:f.counts.monitorApplies,saves:f.counts.monitorSaves});
   assert.equal(f.counts.monitorApplies,expected,ending);
  }finally{release();f.stop();}
 }
 (selected.size?console.log:console.error)(JSON.stringify({monitorAfterSave:outcomes}));
 assert.deepEqual(outcomes.map(({enabled,ending,afterReady})=>({enabled,ending,afterReady})),[
  {enabled:false,ending:'ready',afterReady:1},{enabled:true,ending:'ready',afterReady:1},{enabled:true,ending:'hide',afterReady:1},
  {enabled:true,ending:'superseded',afterReady:0},{enabled:true,ending:'stop',afterReady:0},
 ]);
 for(const row of outcomes)assert.deepEqual([row.saved,row.pending,row.beforeReady,row.saves],[true,true,0,1],row.ending);
});

await check('native target becomes pending during an awaited power read and cancels before the performance write',async()=>{
 let blocking=false,release;const f=await fixture(seed(),{readHooks:{power:async()=>{if(blocking)await new Promise(resolve=>release=resolve);}}});f.ready();const old=await f.read(),before=f.replies.length;blocking=true;
 await f.request('performance.setControls',{patch:{tdp:25}},old);assert.equal(typeof release,'function');f.state.gameCurrent=false;blocking=false;release();await flush();
 const reply=f.replies.slice(before).find(x=>x.message.type==='reply')?.message;assert.equal(reply?.ok,false);assert.equal(f.counts.performanceUpdates??0,0);f.stop();
});


await check(readinessCase,async()=>{
 for(const hasGame of confirmedEmptyOnly?[false]:[true,false]){
  const f=await fixture();
  const diagnostic=diagnoseReadinessTail?{checkpoints:[],globalAdmissionFailure:null,tailError:null}:null;
  let admissionError;
  try{
  if(!hasGame)f.state.game=null;f.ready();const healthy=await f.read();await flush();
  const latest=()=>f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
  const snapshotCount=()=>f.replies.filter(x=>x.message.type==='snapshot').length;
  const identity=healthy.game?.identity??null;assert.equal(f.readinessSubscriberCount(),1);assert.equal(healthy.actions.performance,true);assert.equal(healthy.actions.frames,true);assert.equal(healthy.actions.game,hasGame);assert.equal(healthy.actions.global,!hasGame,'Healthy initial global admission must match the original target case');
  let counts=structuredClone(f.counts),snapshots=snapshotCount();for(let i=0;i<1000;i++)f.setGameReadiness(true);await flush();assert.deepEqual(f.counts,counts);assert.equal(snapshotCount(),snapshots);
  f.setGameReadiness(false);await flush();const failed=latest();assert.equal(snapshotCount(),snapshots+1);assert.equal(failed.game?.identity??null,identity);assert.equal(failed.actions.performance,false);assert.equal(failed.actions.frames,false);assert.equal(failed.actions.game,false);
  try{assert.equal(failed.actions.global,false,'Notified unknown target must withdraw global admission under the original fixture contract');}
  catch(error){if(!diagnostic)throw error;admissionError=error;diagnostic.globalAdmissionFailure={actual:failed.actions.global,expected:false,error:String(error)};}
  assert.equal(failed.actions.monitor,true);assert.equal(failed.actions.audioOptimization,true);assert.equal(f.counts.power,counts.power+1);
  diagnostic?.checkpoints.push('unknown-withdraws-game-actions-and-keeps-unrelated-actions');
  counts=structuredClone(f.counts);snapshots=snapshotCount();for(let i=0;i<1000;i++)f.setGameReadiness(false);await flush();assert.deepEqual(f.counts,counts);assert.equal(snapshotCount(),snapshots);
  diagnostic?.checkpoints.push('duplicate-unknown-does-not-read-or-repaint');
  // No gameChanged call: the same PID/birth/native lease simply becomes usable.
  f.setGameReadiness(true);await flush();const recovered=latest();assert.equal(snapshotCount(),snapshots+1,'Same-target readiness recovery must push a refreshed snapshot');assert.equal(recovered.game?.identity??null,identity);assert.equal(recovered.actions.performance,true);assert.equal(recovered.actions.frames,true);assert.equal(recovered.actions.game,hasGame);assert.equal(recovered.actions.global,!hasGame);assert.equal(f.counts.power,counts.power+1);
  diagnostic?.checkpoints.push('same-target-recovers-with-one-snapshot-and-read');
  assert.equal(f.counts.gameReconciles,1);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.counts.globalFrameSaves,0);assert.equal(f.counts.frameApplies,0);assert.equal(f.counts.monitorSaves,0);assert.equal(f.counts.fxSoundWrites,0);
  diagnostic?.checkpoints.push('recovery-performs-no-writes');
  counts=structuredClone(f.counts);snapshots=snapshotCount();for(let i=0;i<1000;i++)f.setGameReadiness(true);await flush();assert.deepEqual(f.counts,counts);assert.equal(snapshotCount(),snapshots);
  diagnostic?.checkpoints.push('duplicate-ready-does-not-read-or-repaint');
  f.stop();assert.equal(f.readinessSubscriberCount(),0);assert.equal(f.observerStats().game,0);assert.equal(f.observerStats().watchers,0);const replies=f.replies.length;counts=structuredClone(f.counts);
  for(let i=0;i<1000;i++){f.setGameReadiness(false);f.setGameReadiness(true);}await flush();assert.equal(f.replies.length,replies);assert.deepEqual(f.counts,counts);
  diagnostic?.checkpoints.push('stop-unsubscribes-and-late-edges-stay-quiet');
  }catch(error){if(diagnostic)diagnostic.tailError=String(error);throw error;}
  finally{
   f.stop();
   if(diagnostic)console.error(JSON.stringify({readinessTailDiagnostic:diagnostic}));
  }
  if(admissionError)throw admissionError;
 }
});

// Keep the real Host snapshotDemand callback: isolated relay fixtures without it
// cannot detect duplicate requests invalidating the production in-flight read.
const demandStormMeasurements=[];
async function verifyReopenedLosslessRead(){
 let releaseOld,reads=0,active=0,peak=0,enabled=true;
 const old=new Promise(resolve=>releaseOld=resolve);
 const owner={
  owner:{
   async getLosslessGameState(){
    const captured=enabled,serial=++reads;active++;peak=Math.max(peak,active);
    try{if(serial===1)await old;return {xml:captured?'old-xml':'current-xml',enabled:captured,configuredEnabled:captured,running:true,controlAvailable:true};}
    finally{active--;}
   },
   setLosslessScalingEnabled(){assert.fail('Observation must never execute a Lossless action');},
   onLosslessScalingChanged:()=>()=>{},
  },
  readLosslessProgram:async()=>({known:true,running:true}),stats:()=>({}),
 };
 const f=await fixture(seed(),{losslessOwner:owner});
 const field=()=>f.replies.filter(x=>x.message.type==='snapshot'&&x.message.displayOnly!==true).at(-1)?.message.snapshot.game?.fields.losslessScaling;
 try{
  f.ready();const initial=await f.read();assert.equal(initial.ready,true);assert.equal(active,1);assert.equal(field().value,'unknown');
  f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});
  const beforeHidden=f.counts.inputReads;
  enabled=false;
  for(let n=0;n<32;n++){f.scheduleChanged();f.powerChanged();}
  await flush();assert.equal(f.counts.inputReads,beforeHidden);assert.equal(reads,1);
  const reopened=await f.read();const beforeOld=field().value,readsBeforeOld=reads,controlReadsBeforeOld=f.counts.inputReads;
  assert.equal(reopened.generation,initial.generation);assert.equal(reopened.game.identity,initial.game.identity);
  const firstNewReadWasUsable=field().supported;
  releaseOld();await flush();
  const result={case:'same-game-lossless-reopen',readsBeforeOld,beforeOld,firstNewReadWasUsable,afterOld:field().value,lateControlReads:f.counts.inputReads-controlReadsBeforeOld,peak};
  console.log(JSON.stringify(result));
  assert.equal(readsBeforeOld,2,'reopened demand must get its own read while the old result is held');
  assert.equal(beforeOld,'off');assert.equal(firstNewReadWasUsable,true);assert.equal(result.afterOld,'off');
  assert.equal(result.lateControlReads,0);assert.equal(peak,2);assert.equal(reads,2);assert.equal(active,0);
  assert.equal(f.counts.saves,0);
 }finally{f.stop();releaseOld();await flush();}
 assert.deepEqual(f.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});
}
async function measureSteamObservationOwnership(){
 let hold=null,release;
 const f=await fixture(seed(),{readHooks:{input:async()=>{if(hold)await hold;}}});
 const latest=()=>f.replies.filter(x=>x.message.type==='snapshot'&&!x.message.displayOnly).at(-1)?.message.snapshot;
 const send=steam=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,steam}});
 const previous={availability:'observed',appId:'480',gameId:'480',runningCount:1};
 try{
  f.ready();const initial={...previous};send(initial);initial.appId='777';await flush();
  assert.deepEqual(latest().steam,previous,'the Host must own the received values');
  const before=f.counts.inputReads;
  hold=new Promise(resolve=>release=resolve);void f.read();await flush();
  assert.equal(f.counts.inputReads,before+1);
  send({runningCount:1,gameId:'480',appId:'480',availability:'observed'});
  hold=null;release();await flush();
  assert.deepEqual(latest().steam,previous);
  const reorderedReads=f.counts.inputReads-before;
  const realStart=f.counts.inputReads;
  hold=new Promise(resolve=>release=resolve);void f.read();await flush();
  assert.equal(f.counts.inputReads,realStart+1);
  const current={...previous,appId:'481'};
  const received={...current};send(received);received.appId='999';hold=null;release();await flush();
  assert.deepEqual(latest().steam,current);
  const changedReads=f.counts.inputReads-realStart;
  assert.equal(changedReads,2,'a real observation change must still retire a held read');
  const unchanged=f.counts.inputReads;
  send({...current,appId:'0481'});send({...current,unexpected:true});await flush();
  assert.equal(f.counts.inputReads,unchanged,'invalid observations must not request a read');
  assert.deepEqual(latest().steam,current);
  for(const value of [{...current,gameId:'482'},{...current,runningCount:2},{...current,availability:'ambiguous'},{availability:'none',runningCount:0},{availability:'unavailable'}]){
   send({...value});await flush();assert.deepEqual(latest().steam,value);
  }
  assert.equal(f.counts.saves,0);
  return {reorderedReads,changedReads,invalidReads:0,owned:true};
 }finally{hold=null;release?.();f.stop();await flush();}
}
for(const boundary of ['ordinary','source-change','manual-probe','reopen'])await check('in-flight snapshot storm preserves '+boundary+' freshness',async()=>{
 let releaseInput,releaseLossless,held=false,blockedInput=null;
 const inputGate=new Promise(resolve=>releaseInput=resolve),losslessGate=new Promise(resolve=>releaseLossless=resolve);
 const burst=new WeakSet();let burstCopies=0,steamCopies=0,steamSerializations=0;
 const f=await fixture(seed(),{readHooks:{input:async()=>{if(held)await inputGate;else if(blockedInput)await blockedInput;},lossless:()=>losslessGate,losslessProgram:()=>losslessGate,hostStringify:(value,...args)=>{
  if(value?.availability==='unavailable')steamSerializations++;
  return JSON.stringify(value,...args);
 },relayClone:value=>{
  if(burst.has(value))burstCopies++;
  if(value?.availability==='unavailable')steamCopies++;
  return structuredClone(value);
 }}});
 try{
  f.ready();const initial=await f.read();assert.equal(initial.ready,true);
  const before=structuredClone(f.counts),replyStart=f.replies.length;
  held=true;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
  assert.equal(f.counts.inputReads,before.inputReads+1,'Read must actually be held in original owner');
  for(let i=0;i<1024;i++){
   const request={command:'snapshot',runId,...(boundary==='ordinary'?{steam:{availability:'unavailable'}}:{})};
   burst.add(request);f.emit('deckySidebar.mirrorRequest',{...peer,request});
  }
  if(boundary==='source-change'){f.state.power.generation++;f.powerChanged();}
  if(boundary==='manual-probe')f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,probeId:'probe:host-storm:1'}});
  if(boundary==='reopen'){
   f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});
   f.state.power.generation++;
   f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});
  }
  held=false;releaseInput();await flush();
  const delivered=f.replies.slice(replyStart).filter(x=>x.message.type==='snapshot').map(x=>x.message),last=delivered.at(-1)?.snapshot;
  // Capture the burst before follow-up probes and writes exercise other paths.
  const reads=f.counts.inputReads-before.inputReads,expected=boundary==='ordinary'?1:2;
  const measurement={boundary,duplicateRequests:1024,businessReads:reads,expected,deliveries:delivered.length,burstCopies,steamCopies,steamSerializations};
  demandStormMeasurements.push(measurement);
  assert.equal(last?.ready,true);assert.equal(last.generation,f.state.power.generation);
  assert.equal(reads,expected,JSON.stringify(demandStormMeasurements.at(-1)));
  if(boundary==='manual-probe'){
   assert.ok(delivered.some(x=>x.probeId==='probe:host-storm:1'&&x.snapshot.ready));
   const probe=id=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,probeId:'probe:host-storm:'+id}});
   for(const mode of ['source-changed-no-resend','same-nonce','new-nonce','source-changed']){
    let unlock;const gate=new Promise(resolve=>unlock=resolve);
    const count=f.counts.inputReads,start=f.replies.length;
    // Hold a new probe's owner read until after a second packet arrives.
    blockedInput=gate;probe(2);await flush();
    try{
     assert.equal(f.counts.inputReads,count+1);
     if(mode.startsWith('source-changed')){f.state.power.generation++;f.powerChanged();}
     if(mode!=='source-changed-no-resend')for(let i=0;i<(mode==='same-nonce'?64:1);i++)probe(mode==='new-nonce'?3:2);
    }finally{blockedInput=null;unlock();}
    await flush();
    const packets=f.replies.slice(start).map(x=>x.message).filter(x=>x.type==='snapshot'&&x.probeId);
    const actual=f.counts.inputReads-count,expected=mode==='same-nonce'?1:2;
    demandStormMeasurements.push({probeOverlap:mode,businessReads:actual,expected,correlatedReplies:packets.length});
    assert.deepEqual(packets.map(x=>x.probeId).sort(),(mode==='new-nonce'?[2,3]:[2]).map(x=>'probe:host-storm:'+x),JSON.stringify({mode,actual,expected,packets:packets.map(({probeId,snapshot})=>({probeId,ready:snapshot.ready,generation:snapshot.generation,revision:snapshot.revision}))}));
    assert(packets.every(x=>x.snapshot.ready&&x.snapshot.generation===f.state.power.generation));
    assert.equal(actual,expected,JSON.stringify(demandStormMeasurements.at(-1)));
   }
    for(const mode of ['same-nonce-after-retry','new-nonce-after-retry','source-changed-again','reopen-before-retry']){
     let unlockFirst,unlockRetry;
     const firstGate=new Promise(resolve=>unlockFirst=resolve),retryGate=new Promise(resolve=>unlockRetry=resolve);
     const count=f.counts.inputReads,start=f.replies.length;
     const demand=subscribe=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});
     try{
      blockedInput=firstGate;probe(4);await flush();
      assert.equal(f.counts.inputReads,count+1);
      f.state.power.generation++;f.powerChanged();
      if(mode==='reopen-before-retry'){demand(false);demand(true);}
      blockedInput=retryGate;unlockFirst();await flush();
      assert.equal(f.counts.inputReads,count+2,'The replacement owner read must already have started');
      assert.equal(f.replies.slice(start).filter(x=>x.message.probeId).length,0);
      if(mode==='source-changed-again'){f.state.power.generation++;f.powerChanged();}
      probe(mode==='new-nonce-after-retry'?5:4);await flush();
     }finally{blockedInput=null;unlockFirst();unlockRetry();}
     await flush();
     const packets=f.replies.slice(start).map(x=>x.message).filter(x=>x.type==='snapshot'&&x.probeId);
     const actual=f.counts.inputReads-count,expected=mode==='same-nonce-after-retry'?2:3;
     const measurement={probeOverlap:mode,businessReads:actual,expected,correlatedReplies:packets.length};
     demandStormMeasurements.push(measurement);
     assert.deepEqual(packets.map(x=>x.probeId).sort(),(mode==='new-nonce-after-retry'?[4,5]:[4]).map(x=>'probe:host-storm:'+x));
     assert(packets.every(x=>x.snapshot.ready&&x.snapshot.generation===f.state.power.generation));
     assert.equal(actual,expected,JSON.stringify(measurement));
    }
  }
  // A later explicit request must still refresh, including external file edits.
  const settled=f.counts.inputReads;await f.read();assert.equal(f.counts.inputReads,settled+1);
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
  if(boundary==='ordinary'){
   const observation=await measureSteamObservationOwnership();
   console.log(JSON.stringify({case:'steam-observation-hot-path',...measurement,...observation}));
   assert.equal(observation.reorderedReads,1,'the same scalar facts must not retire a held read');
  }
  assert.equal(steamCopies,0,'no request-time clone for repeated Steam facts; changed-value ownership is verified separately');
  assert.equal(steamSerializations,0,'compare Steam sidebar observations as scalar fields');
  assert.equal(burstCopies,0,JSON.stringify(measurement));
 }finally{f.stop();releaseInput();releaseLossless();await flush();}
 assert.equal(f.observerStats().watchers,0);assert.equal(f.observerStats().game,0);assert.equal(f.fxTimerCount(),0);
 if(boundary==='reopen')await verifyReopenedLosslessRead();
});

const queuedProbeMeasurements=[];
await check('snapshot probe does not borrow an ordinary push from retired demand',async()=>{
 const original={...peer,connectionRevision:1},successor={...peer,connection:'2',connectionRevision:2};
 const probeId='probe:queue-scope:1';
 for(const boundary of ['idle','reopen','replace-peer','current-source-change']){
  let releaseInput,releaseLossless,holdInput=false;
  const inputGate=new Promise(resolve=>releaseInput=resolve),losslessGate=new Promise(resolve=>releaseLossless=resolve);
  const f=await fixture(seed(),{readHooks:{input:async()=>{if(holdInput)await inputGate;},lossless:()=>losslessGate,losslessProgram:()=>losslessGate}});
  const demand=(p,overrides={})=>f.emit('deckySidebar.mirrorRequest',{...p,request:{command:'snapshot',runId:p.runId,...overrides}});
  const changePower=()=>{f.state.power.generation++;f.powerChanged(true);};
  try{
   f.ready();demand(original);await flush();
   assert.equal(f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot.ready,true);
   const start=f.replies.length,readsBefore=f.counts.inputReads;
   let current=original;
   if(boundary==='reopen'||boundary==='replace-peer'){
    changePower(); // Queue the original demand's ordinary push without draining microtasks.
    if(boundary==='reopen')demand(original,{subscribe:false});
    else {current=successor;f.emit('deckySidebar.mirrorPeer',successor);}
   }
   holdInput=boundary==='current-source-change';
   demand(current,{probeId});
   if(holdInput){
    await flush();assert.equal(f.counts.inputReads,readsBefore+1);
    changePower();holdInput=false;releaseInput();
   }
   await flush();
   const packets=f.replies.slice(start).filter(x=>x.message.type==='snapshot');
   const reads=f.counts.inputReads-readsBefore;
   const ordinary=packets.filter(x=>!x.message.probeId).length,correlated=packets.filter(x=>x.message.probeId===probeId).length;
   queuedProbeMeasurements.push({boundary,reads,ordinary,correlated});
   assert(packets.every(x=>x.connection===current.connection&&x.message.snapshot.ready&&x.message.snapshot.generation===f.state.power.generation));
   assert.equal(correlated,1);
   assert.equal(reads,boundary==='current-source-change'?2:1);
   const later=f.counts.inputReads;demand(current);await flush();assert.equal(f.counts.inputReads,later+1);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{holdInput=false;releaseInput();releaseLossless();f.stop();await flush();}
  assert.deepEqual(f.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});
  assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert.equal(f.memory.stats().pendingDeadlineTimers,0);
 }
 assert.deepEqual(queuedProbeMeasurements.map(x=>x.ordinary),[0,0,0,1],JSON.stringify(queuedProbeMeasurements));
});

// Reproduce retained-frontend Host.ready()->attach with the authenticated
// sidebar pause already delivered. A passive peer must not restart business IO.
const attachDemandMeasurements=[];
const attachReadMeasurements=[];
const retiredPowerSourceMeasurements=[];
const readFollowupMeasurements=[];
for(const boundary of ['explicit-pause','pause-during-attach','visible','unknown','invalid-pause','new-peer'])await check('renderer reattach respects '+boundary+' sidebar demand',async()=>{
 let attachPeer=null,releaseAttach,holdPowerSource=null,releasePowerSource,powerSourceReads=0;
 const attachGate=new Promise(resolve=>releaseAttach=resolve);
 const f=await fixture(seed(),{readHooks:{powerSource:async()=>{powerSourceReads++;if(holdPowerSource)await holdPowerSource;}},invoke:async command=>{
  if(command!=='deckySidebar.mirrorAttach')return true;
  if(attachPeer&&boundary==='pause-during-attach')await attachGate;
  return attachPeer?{...attachPeer,owner:'YMCC-native',pid:8612}:{connected:false,owner:'YMCC-native',pid:8612};
 }});
 try{
  await flush();f.emit('deckySidebar.mirrorPeer',peer);
  if(boundary==='explicit-pause'||boundary==='new-peer')f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});
  else if(boundary==='visible')f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});
  else if(boundary==='invalid-pause')f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId:'b'.repeat(32),subscribe:false}});
  await flush();assert.equal(f.counts.power,0,'Initializing Host must not read original business owners');
  const before=structuredClone(f.counts),replyStart=f.replies.length;
  attachPeer=boundary==='new-peer'?{connection:'2',runId:'b'.repeat(32),connected:true}:peer;
  f.ready();
  if(boundary==='pause-during-attach'){await flush();f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});releaseAttach();}
  await flush();
  const snapshots=f.replies.slice(replyStart).filter(x=>x.message.type==='snapshot').map(x=>x.message.snapshot);
  attachDemandMeasurements.push({boundary,powerReads:f.counts.power-before.power,inputReads:f.counts.inputReads-before.inputReads,deliveredSnapshots:snapshots.length});
  if(boundary==='explicit-pause'||boundary==='pause-during-attach'){
   assert.equal(f.counts.power,before.power,JSON.stringify(attachDemandMeasurements.at(-1)));
   assert.equal(f.counts.inputReads,before.inputReads);assert.equal(snapshots.length,0);
   assert.equal(f.observerStats().watchers,0);assert.equal(f.readinessSubscriberCount(),0);
   const reopened=await f.read();assert.equal(reopened.ready,true);assert(f.counts.power>before.power,'Explicit visible reopen must remain usable');
   if(boundary==='explicit-pause')for(const ending of ['hide','reopen','stop']){
    const demand=subscribe=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});
    const beforePower=powerSourceReads,replyCount=f.replies.length;
    holdPowerSource=new Promise(resolve=>releasePowerSource=resolve);
    demand(true);await flush();
    assert.equal(powerSourceReads,beforePower+1,'Original snapshot must be blocked in power source read');
    const counts=structuredClone(f.counts);
    if(ending==='stop')f.stop();else demand(false);
    if(ending==='reopen'){f.state.power.generation++;demand(true);}
    holdPowerSource=null;releasePowerSource();await flush();
    const row={ending,schedule:f.counts.schedule-counts.schedule,rtss:(f.counts.rtssReads??0)-(counts.rtssReads??0),
     input:f.counts.inputReads-counts.inputReads,packets:f.replies.slice(replyCount).filter(x=>x.message.type==='snapshot').length};
    retiredPowerSourceMeasurements.push(row);
    const expected=ending==='reopen'?1:0;
    assert.deepEqual([row.schedule,row.rtss,row.input,row.packets],[expected,expected,expected,expected],JSON.stringify(row));
    if(ending==='reopen'){
     const current=f.replies.at(-1)?.message.snapshot;
     assert(current?.ready&&current.generation===f.state.power.generation,'Reopened demand must deliver a fresh Ready snapshot');
    }
   }
  }else{
   assert(f.counts.power>before.power);assert.equal(snapshots.at(-1)?.ready,true);assert.equal(snapshots.at(-1).runId,attachPeer.runId);
   if(boundary==='new-peer'){
    // A delayed pause from the previous authenticated peer cannot pause the new one.
    f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});
    const reads=f.counts.power;f.emit('deckySidebar.mirrorRequest',{...attachPeer,request:{command:'snapshot',runId:attachPeer.runId}});await flush();assert(f.counts.power>reads);
   }
  }
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
 }finally{holdPowerSource=null;releasePowerSource?.();releaseAttach();f.stop();await flush();}
 assert.equal(f.observerStats().watchers,0);assert.equal(f.observerStats().game,0);assert.equal(f.fxTimerCount(),0);assert.equal(f.controllerTimerCount(),0);
 if(boundary==='explicit-pause'){
  await checkRetiredReadFollowups();
  await checkRetiredInitialReads();
  await checkRetiredProfileAssembly();
 }
 if(boundary==='visible')await checkVisibleAttachRead();
});

async function checkVisibleAttachRead(){
 let pendingAttach,holdInput,releaseInput,attachedPid=8612,attachRevision=0;
 const f=await fixture(seed(),{readHooks:{input:async()=>{if(holdInput)await holdInput;}},invoke:async command=>{
  if(command!=='deckySidebar.mirrorAttach')return true;
  if(pendingAttach)await pendingAttach;
  return {...peer,connectionRevision:1,revision:attachRevision,owner:'YMCC-native',pid:attachedPid};
 }});
 let releaseAttach;
 const reads=()=>[f.counts.power,f.counts.schedule,f.counts.inputReads];
 const freshReattach=async()=>{const before=reads();f.ready();await flush();return reads().map((count,index)=>count-before[index]);};
 const current=()=>f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
 try{
  await flush();assert.equal(current()?.ready,false,'Retained peer was attached before Host Ready');
  const before=reads();const dismissalsBefore=f.diagnostics.filter(x=>x.discardReason==='source-changed').length;
  pendingAttach=new Promise(resolve=>releaseAttach=resolve);
  holdInput=new Promise(resolve=>releaseInput=resolve);
  f.ready();await flush();
  assert.deepEqual(reads().map((count,index)=>count-before[index]),[1,1,1],'The actual Ready read reached the business owner');
  pendingAttach=null;releaseAttach();await flush();
  holdInput=null;releaseInput();await flush();
  const startup={reads:reads().map((count,index)=>count-before[index]),discarded:f.diagnostics.filter(x=>x.discardReason==='source-changed').length-dismissalsBefore};
  assert.equal(current()?.ready,true);assert.equal(current()?.provenance?.pid,8612);
  const beforeStable=f.replies.length,stable=await freshReattach(),stableOutputs=f.replies.length-beforeStable;
  attachRevision=100;const advanced=await freshReattach();const advancedRevision=current()?.revision;
  attachedPid=8613;const owner=await freshReattach();
  const row={startup,stable,stableOutputs,advanced,advancedRevision,owner,newPid:current()?.provenance?.pid};
  attachReadMeasurements.push(row);
  assert.deepEqual(startup,{reads:[1,1,1],discarded:0},JSON.stringify(row));
  assert.deepEqual(stable,[0,0,0]);assert.equal(stableOutputs,0);
  assert.deepEqual(advanced,[1,1,1]);assert(advancedRevision>100,'A genuine broker revision change must be published');
  assert.deepEqual(owner,[1,1,1]);assert.equal(current()?.ready,true);assert.equal(row.newPid,8613);
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
 }catch(error){if(attachReadMeasurements.length)console.error(JSON.stringify({attachReadMeasurements}));throw error;}
 finally{holdInput=null;pendingAttach=null;releaseInput?.();releaseAttach?.();f.stop();await flush();}
 assert.equal(f.observerStats().watchers,0);assert.equal(f.observerStats().ipc,0);
}

async function checkRetiredInitialReads(){
 const rows=[],expected=[];
 for(const stage of ['power','powerSource'])for(const edge of ['current','display-only','steam','pending']){
  let release,releaseLossless,entered=0,sourceCalls=0,held=true,originalPower;
  const gate=new Promise(resolve=>release=resolve);
  const losslessGate=new Promise(resolve=>releaseLossless=resolve);
  const hold=async at=>{if(at===stage&&held){held=false;entered++;await gate;return originalPower;}};
  const f=await fixture(seed(),{readHooks:{power:()=>hold('power'),powerSource:async()=>{sourceCalls++;await hold('powerSource');},lossless:()=>losslessGate,losslessProgram:()=>losslessGate}});
  const packets=()=>f.replies.filter(x=>x.message.type==='snapshot');
  try{
   originalPower=structuredClone(f.state.power);f.ready();await flush();const demand=f.read();await flush();
   assert.equal(entered,1);assert.equal(f.counts.power,1);assert.equal(f.counts.schedule,0);assert.equal(packets().length,0);
   const audioBefore=f.counts.fxSoundReads,sourceBefore=sourceCalls;
   assert.equal(audioBefore,stage==='power'?0:1);assert.equal(sourceBefore,stage==='power'?0:1);
   if(edge==='steam')f.emit('gameSteam.context.sourceChanged',{pid:900,processCreated:'987654321',processEpoch:'1',accountEpoch:'1'});
   if(edge==='display-only')f.emit('gameSteam.context.changed',{});
   if(edge==='pending'){
    f.state.power={...originalPower,generation:2,phase:'suspended',inputReady:false,resumeReady:false,hardwareWritesAllowed:false};
    f.powerChanged(true);
   }
   const earlySuspend=stage==='powerSource'&&edge==='pending';
   await flush();
   assert.equal(f.counts.power,earlySuspend?2:1,'Only the public power boundary can displace the blocked batch');
   assert.equal(packets().length,earlySuspend?1:0);
   if(earlySuspend){
    const held=packets()[0].message.snapshot;
    assert.equal(held.generation,2);assert.equal(held.ready,false);assert.equal(held.actions.global,false);
    assert.equal(f.counts.schedule,0);
   }
   release();await demand;await flush();
   const view=packets().at(-1)?.message.snapshot;
   const row={stage,edge,power:f.counts.power,source:sourceCalls-sourceBefore,audio:f.counts.fxSoundReads-audioBefore,schedule:f.counts.schedule,packets:packets().length};
   rows.push(row);
   // Before the first power receipt, a confirmed newer public suspend can
   // replace the stale direct result in the same read. Once business reads
   // have started, the old batch must still be retired and read again.
   const adoptedSuspend=stage==='power'&&edge==='pending';
   const retired=edge==='steam'||edge==='pending'&&!adoptedSuspend;
   const currentReads=edge==='pending'||(stage==='powerSource'&&!retired)?0:1;
   expected.push({stage,edge,power:retired?2:1,source:currentReads,audio:currentReads,schedule:edge==='pending'?0:1,packets:1});
   const discarded=f.diagnostics.filter(x=>x.event==='snapshot-read-finished'&&x.discardReason==='source-changed');
   console.log(JSON.stringify({retiredInitialRead:row,discardedBy:discarded.map(x=>x.invalidatedBy)}));
   assert.equal(discarded.length,retired?1:0);
   if(retired)assert([edge==='steam'?'game':'power','multiple'].includes(discarded[0].invalidatedBy));
   assert.equal(view?.ready,edge!=='pending');assert.equal(view?.generation,edge==='pending'?2:1);
   if(edge==='pending'){
    assert.equal(view.actions.performance,false);
    f.state.power={...originalPower,generation:2};
    f.emit('power.lifecycleChanged',{...f.state.power,known:true,source:'shared-power-owner-current'});await flush();
    assert.equal(packets().length,2);assert.equal(packets().at(-1)?.message.snapshot.ready,true);
    assert.equal(packets().at(-1)?.message.snapshot.generation,2);
   }
   assert.equal(f.controllerTimerCount(),0);assert.equal(f.counts.saves,0);assert.equal(f.counts.fxSoundWrites,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{release();f.stop();releaseLossless();await flush();}
  assert.equal(f.observerStats().watchers,0);assert.equal(f.fxTimerCount(),0);assert.equal(f.controllerTimerCount(),0);
 }
 console.log(JSON.stringify({retiredInitialReadMeasurements:rows}));
 assert.deepEqual(rows,expected,JSON.stringify(rows));
}

async function checkRetiredReadFollowups(){
 const rows=[],expected=[];
  for(const config of ['normal','degraded'])for(const ending of ['hide','stop','reopen','visible','steam-source','display-only']){
  let release,rtssReads=0,monitorReads=0,failSchedule=config==='degraded';
  const gate=new Promise(resolve=>release=resolve);
  const f=await fixture(seed(),{
   rtssReader:async()=>{if(++rtssReads===1){await gate;throw Error('inert RTSS section read failed');}return {monitorOutput:structuredClone(f.state.monitor)};},
   frameOwner:{loadGlobalFrameRates:async read=>{await read();return structuredClone(f.state.globalFrames);}},
   monitorOwner:{readMonitorOutputPreferences:async read=>{monitorReads++;return (await read()).monitorOutput;}},
  });
  const schedule=f.state.schedule;
  Object.defineProperty(f.state,'schedule',{get(){if(failSchedule)throw Error('inert schedule read failed');return schedule;}});
  const demand=subscribe=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});
  try{
   f.ready();const at=f.replies.length;demand(true);await flush();
   assert.equal(rtssReads,1);assert.equal(monitorReads,1,'Monitor must be waiting on the original section');
   const power=f.counts.power;
   if(ending==='steam-source'){
    failSchedule=false;
    f.emit('gameSteam.context.sourceChanged',{pid:900,processCreated:'987654321',processEpoch:'1',accountEpoch:'1'});await flush();
    assert.equal(rtssReads,1,'Steam change retires demand without starting a concurrent snapshot');
   }else if(ending==='display-only'){
    f.emit('gameSteam.context.changed',{});await flush();
    assert.equal(rtssReads,1,'Display notification must leave the original monitor read in flight');
   }else if(ending==='stop')f.stop();else if(ending!=='visible')demand(false);
   if(ending==='reopen'){failSchedule=false;f.state.power.generation++;demand(true);}
   release();await flush();
   const packets=f.replies.slice(at).filter(x=>x.message.type==='snapshot');
   const fresh=packets.at(-1)?.message.snapshot;
   const ordinary=ending==='visible'||ending==='display-only';
   const visible=ordinary||ending==='reopen'||ending==='steam-source';
   const row={config,ending,rtss:rtssReads-1,power:f.counts.power-power,packets:packets.length};
   rows.push(row);
   expected.push({config,ending,rtss:visible?1:0,power:ending==='reopen'||ending==='steam-source'||(ordinary&&config==='degraded')?1:0,packets:visible?1:0});
   if(ending==='steam-source'){
    const discarded=f.diagnostics.filter(x=>x.event==='snapshot-read-finished'&&x.discardReason==='source-changed'&&x.invalidatedBy==='game');
    assert.equal(discarded.length,1,'Original snapshot must retire for the Steam source change');
    assert.equal(f.controllerTimerCount(),0,'Steady Steam source retirement must not arm a fault retry');
   }
   if(ending==='display-only')assert(!f.diagnostics.some(x=>x.event==='snapshot-read-finished'&&x.discardReason==='source-changed'&&x.invalidatedBy==='steam'));
   if(visible){
    assert(fresh?.ready&&fresh.generation===f.state.power.generation&&fresh.actions.monitor);
    assert.equal(fresh.actions.performance,!(ordinary&&config==='degraded'));
   }
   assert.equal(f.counts.saves,0);assert.equal(f.counts.monitorSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{release();f.stop();await flush();}
  assert.equal(f.observerStats().watchers,0);assert.equal(f.fxTimerCount(),0);assert.equal(f.controllerTimerCount(),0);
 }
 assert.deepEqual(rows,expected,JSON.stringify(rows));
 readFollowupMeasurements.push(...rows);
}

async function checkRetiredProfileAssembly(){
 const rows=[];
 for(const ending of ['current','monitor','hide','reopen']){
  let release,returned=0,serializations=0,revisionChecks=0;
  const gate=new Promise(resolve=>release=resolve);
  const f=await fixture(seed(),{readHooks:{
   profileReturn:async()=>{if(++returned===1)await gate;},
   hostStringify:value=>{if(value?.version===1&&value?.entries?.['game.exe']&&value?.rtss)serializations++;return JSON.stringify(value);},
   profileRevisionObserved:()=>{revisionChecks++;},
  }});
  const demand=subscribe=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});
  const packets=()=>f.replies.filter(x=>x.message.type==='snapshot');
  try{
   f.ready();await flush();demand(true);await flush();
   assert.equal(returned,1,'original profile owner returned while snapshot assembly is held');
   assert.equal(serializations,0);assert.equal(revisionChecks,0);assert.equal(packets().length,0);
   if(ending==='monitor'){f.state.monitor={...f.state.monitor,enabled:false};f.monitorChanged();}
   if(ending==='hide'||ending==='reopen')demand(false);
   if(ending==='reopen'){f.state.power.generation++;demand(true);}
   await flush();assert.equal(returned,ending==='reopen'?2:1,'Only reopened demand with new public power can bypass the old profile read');
   if(ending==='reopen'){
    assert.equal(packets().length,1,'The reopened sidebar must recover before the old owner returns');
    assert.equal(packets()[0].message.snapshot.generation,2);
    assert.equal(packets()[0].message.snapshot.ready,true);
   }else assert.equal(packets().length,0);
   release();await flush();
   const view=packets().at(-1)?.message.snapshot;
   const row={ending,profileReturns:returned,serializations,revisionChecks,packets:packets().length,generation:view?.generation??null};
   rows.push(row);
   console.log(JSON.stringify({retiredProfileAssembly:row}));
   if(ending!=='hide'){assert.equal(view?.ready,true);assert.equal(view?.monitor?.enabled,ending!=='monitor');}
   assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{release();f.stop();await flush();}
  assert.equal(f.observerStats().watchers,0);assert.equal(f.memory.stats().pendingDeadlineTimers,0);
 }
 assert.deepEqual(rows,[
  {ending:'current',profileReturns:1,serializations:0,revisionChecks:1,packets:1,generation:1},
  {ending:'monitor',profileReturns:2,serializations:0,revisionChecks:1,packets:1,generation:1},
  {ending:'hide',profileReturns:1,serializations:0,revisionChecks:0,packets:0,generation:null},
  {ending:'reopen',profileReturns:2,serializations:0,revisionChecks:1,packets:1,generation:2},
 ]);
}

// The renderer can receive an authenticated native peer event while an older
// mirrorAttach observation is still in flight; registration and ready also race.
const lateAttachMeasurements=[];
for(const boundary of ['new-peer-event','disconnect-event','newer-attach'])await check('late native attach cannot replace '+boundary,async()=>{
 let resolveOld;const oldReply=new Promise(resolve=>resolveOld=resolve);let attaches=0;
 const current={connection:'2',runId:'b'.repeat(32),connected:true},observed=[];
 const f=await fixture(seed(),{invoke:async command=>{
  if(command!=='deckySidebar.mirrorAttach')return true;
  const request=++attaches;
  if(request===(boundary==='newer-attach'?1:2)){await oldReply;return {...peer,owner:'YMCC-native',pid:8612};}
  return {...(boundary==='newer-attach'?current:peer),owner:'YMCC-native',pid:8612};
 }});
 const off=f.registerExtension({prefix:'attach-fixture.',connected:value=>observed.push(structuredClone(value)),request(){}});
 try{
  await flush();f.ready();await flush();
  if(boundary==='new-peer-event')f.emit('deckySidebar.mirrorPeer',current);
  if(boundary==='disconnect-event')f.emit('deckySidebar.mirrorPeer',{...peer,connected:false});
  if(boundary!=='disconnect-event'){
   f.emit('deckySidebar.mirrorRequest',{...current,request:{command:'snapshot',runId:current.runId}});await flush();
   assert.equal(f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot.runId,current.runId);
  }
  const before={power:f.counts.power,peers:observed.length,replies:f.replies.length};
  resolveOld();await flush();
  assert.equal(observed.length,before.peers,'a retired attach receipt cannot dispatch a peer or reconnect an extension');
  assert.equal(f.counts.power,before.power,'a retired attach receipt cannot start another business read');
  if(boundary==='disconnect-event')assert.equal(observed.at(-1).connected,false,'disconnected current peer stays disconnected');
  else{
   f.emit('deckySidebar.mirrorRequest',{...current,request:{command:'snapshot',runId:current.runId}});await flush();
   const snapshots=f.replies.slice(before.replies).filter(x=>x.message.type==='snapshot');
   assert(snapshots.length>0,'current authenticated peer remains usable after late attach');
   assert(snapshots.every(x=>x.message.snapshot.runId===current.runId),'late attach cannot publish an old run snapshot');
  }
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
  lateAttachMeasurements.push({boundary,retiredPeerIgnored:true,oldReads:0,businessWrites:0});
 }finally{resolveOld();off();f.stop();await flush();}
 assert.equal(f.observerStats().ipc,0);assert.equal(f.observerStats().watchers,0);assert.equal(f.fxTimerCount(),0);assert.equal(f.controllerTimerCount(),0);assert.equal(f.memory.stats().pendingDeadlineTimers,0);
});

await check('late bridge owner confirmation establishes current native health without adopting the old peer',async()=>{
 const releases=[],observed=[],current={connection:'2',runId:'b'.repeat(32),connected:true};
 const f=await fixture(seed(),{invoke:async command=>{
  if(command!=='deckySidebar.mirrorAttach')return true;
  return new Promise(resolve=>releases.push(()=>resolve({...peer,owner:'YMCC-native',pid:8612})));
 }});
 const off=f.registerExtension({prefix:'late-owner-fixture.',connected:value=>observed.push(structuredClone(value)),request(){}});
 try{
  f.ready();f.emit('deckySidebar.mirrorPeer',current);f.emit('deckySidebar.mirrorRequest',{...current,request:{command:'snapshot',runId:current.runId}});await flush();
  assert.equal(f.replies.filter(x=>x.message.type==='snapshot').at(-1).message.snapshot.provenance.kind,'unconfirmed');
  const peers=observed.length;releases[0]();await flush();
  const actual=f.replies.filter(x=>x.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(actual.ready,true);assert.equal(actual.runId,current.runId);assert.equal(actual.provenance.kind,'ymcc-native');assert.equal(actual.provenance.pid,8612);
  assert.equal(observed.length,peers,'owner metadata must not adopt the retired transport');
  const reads=f.counts.power;for(const release of releases)release();await flush();
  assert.equal(f.counts.power,reads,'same confirmed owner is not an extra read demand');
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
 }finally{for(const release of releases)release();off();f.stop();await flush();}
 assert.equal(f.observerStats().ipc,0);assert.equal(f.memory.stats().pendingDeadlineTimers,0);
});

async function fanPowerFixture({active=false}={}){
 let enabled=active,preset='balanced',armed=false,held=null,holdApply=null,holdStart=null;
 const saves=[],writes=[],starts=[];
 const lifecycle={get hasControlIntent(){return active||starts.length>0;},controlReady:true,state:active?'ready':'closed',recoveryActive:false,
  async start(){starts.push('start');if(holdStart)await holdStart.promise;return {allowed:true,writeReady:true};},
  async apply(){writes.push(preset);},async applyPreset(name){writes.push(name);if(holdApply)await holdApply.promise;}};
 const defer=()=>{let release;const promise=new Promise(resolve=>{release=resolve;});return {promise,release};};
 const fanOwner={lifecycle,featureBindings:{FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},
  fanControlActive:{get value(){return enabled;}},getFanSelectedPreset:()=>preset,getFanPresetCurve:()=>[],
  saveFanCurve:async(_nodes,name)=>{saves.push(name);preset=name;},setFanControlActive:value=>{enabled=value;},
  setFanNavigationDuty:()=>{},recordFanHandshake:async()=>{}}};
 const f=await fixture(seed(),{fanOwner,readHooks:{power:signal=>{
  if(!armed)return;armed=false;
  const source=structuredClone(f.state.power),io=defer();
  held={release:io.release,aborts:0};const read=held;
  return new Promise((resolve,reject)=>{
   const abort=()=>{read.aborts++;reject(Error('fixture power IPC observer aborted'));};
   signal?.addEventListener('abort',abort,{once:true});
   void io.promise.then(()=>{signal?.removeEventListener('abort',abort);resolve(source);});
  });
 }}});
 Object.assign(f.state.power,{publicationRevision:1,observedNotificationSequence:1,suspendSerial:1,notificationPending:false});
 f.ready();const snapshot=await f.read();assert.equal(snapshot.actions.fan,true);
 const publish=async patch=>{Object.assign(f.state.power,patch);f.emit('power.lifecycleChanged',{known:true,...f.state.power});await flush();};
 const replies=()=>f.replies.filter(x=>x.message.type==='reply').map(x=>x.message);
 return {f,snapshot,publish,replies,saves,writes,starts,arm:()=>{armed=true;},held:()=>held,
  holdApply:()=>holdApply=defer(),holdStart:()=>holdStart=defer()};
}
for(const boundary of ['same-generation notification','coalesced suspend and ready'])await check('fan releases a held power read on '+boundary,async()=>{
 const x=await fanPowerFixture({active:true});const {f,snapshot,publish,replies,saves}=x;
 try{
  x.arm();assert.equal(await f.request('fan.setPreset',{preset:'soft'},snapshot),undefined);assert(x.held());
  const powerReads=f.counts.power;
  await publish({publicationRevision:2});assert.equal(x.held().aborts,0);assert.equal(f.counts.power,powerReads);assert.equal(replies().length,0);
  assert.equal((await f.request('fan.setPreset',{preset:'aggressive'},snapshot)).ok,false,'the original request still owns the slot');
  await publish({publicationRevision:3,observedNotificationSequence:2,...(boundary==='coalesced suspend and ready'?{generation:2,suspendSerial:2}:{})});
  const fresh=await f.read();assert.equal(fresh.generation,f.state.power.generation);
  const current=await f.request('fan.setPreset',{preset:'aggressive'},fresh);
  assert.equal(current?.result?.saved,true,'a new selection must save before the old power reply returns');
  assert.equal(x.held().aborts,1);assert.deepEqual(saves,['aggressive']);
  assert.deepEqual(replies().map(r=>[r.id,r.ok]),[['host:2',false],['host:1',false],['host:3',true]]);
  x.held().release();await flush();assert.deepEqual(saves,['aggressive']);assert.deepEqual(x.writes,['aggressive']);assert.equal(replies().length,3);
 }finally{x.held()?.release();f.stop();await flush();}
 assert.equal(f.observerStats().ipc,0);assert.equal(f.sharedPowerTimerCount(),0);
});
await check('fan write already sent retains the mirror operation slot across a wake',async()=>{
 const x=await fanPowerFixture({active:true});const {f,snapshot,publish,replies,saves,writes}=x;const write=x.holdApply();
 try{
  assert.equal(await f.request('fan.setPreset',{preset:'soft'},snapshot),undefined);assert.deepEqual(writes,['soft']);
  await publish({publicationRevision:2,observedNotificationSequence:2,generation:2,suspendSerial:2});
  const fresh=await f.read();assert.equal(fresh.generation,2);
  assert.equal((await f.request('fan.setPreset',{preset:'aggressive'},fresh)).ok,false);
  assert.deepEqual(saves,['soft']);assert.deepEqual(writes,['soft']);assert(!replies().some(r=>r.id==='host:1'));
  write.release();await flush();assert.equal((await f.request('fan.setPreset',{preset:'aggressive'}))?.result?.applied,true);
  assert.deepEqual(writes,['soft','aggressive']);
 }finally{write.release();f.stop();await flush();}
});
await check('fan intent accepted by the owner continues across a same-generation notification',async()=>{
 const x=await fanPowerFixture();const {f,snapshot,publish,writes,starts}=x;const start=x.holdStart();
 try{
  const accepted=await f.request('fan.setEnabled',{enabled:true},snapshot);
  assert.equal(accepted?.result?.pending,true);assert.deepEqual(starts,['start']);assert.deepEqual(writes,[]);
  await publish({publicationRevision:2,observedNotificationSequence:2});
  start.release();await flush();assert.deepEqual(starts,['start']);assert.deepEqual(writes,['balanced']);
 }finally{start.release();f.stop();await flush();}
});

for(const name of selected)assert(registered.has(name),'Unknown Host case: '+name);
const report={status:'passed',cases,lateAttachMeasurements,demandStormMeasurements,attachDemandMeasurements,attachReadMeasurements,retiredPowerSourceMeasurements,readFollowupMeasurements,queuedProbeMeasurements,productionHostExecutedInVm:true,actualHardwareWrites:0,oldTierCommandsRejected:true,continuousOwnerWired:true};console.log(JSON.stringify(report,null,2));
