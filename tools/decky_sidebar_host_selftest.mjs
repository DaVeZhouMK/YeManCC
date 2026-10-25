import assert from 'node:assert/strict';
import {root,seed,main} from './fixtures/decky_game_memory_store.mjs';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));await new Promise(resolve=>setImmediate(resolve));};
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true},cases=[];
async function fixture(initial=seed(),options={}){const f=await createMirrorHostFixture({...options,initial});let sequence=0;
 f.read=async()=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;};
 f.request=async(command,args,snapshot)=>{const before=f.replies.length,s=snapshot??await f.read();sequence++;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command,runId,id:'host:'+sequence,clientId:'host',sequence,generation:s.generation,revision:s.revision,identity:command==='performance.setControls'?s.game?.identity??'':s.game?.identity??'',args}});await flush();return f.replies.slice(before).find(x=>x.message.type==='reply')?.message;};
 return f;}
async function check(name,fn){try{await fn();cases.push(name);}catch(error){console.error(JSON.stringify({failedCase:name,passedBeforeFailure:cases.length,error:String(error)},null,2));throw error;}}
await check('ready lifecycle has no eager reads or saves',async()=>{const f=await fixture();assert.equal(f.counts.power,0);assert.equal(f.counts.saves,0);f.ready();await flush();assert.equal(f.counts.power,0);assert.equal(f.counts.saves,0);f.stop();});
await check('snapshot exposes continuous performance owner and no old six-tier/core mirror fields',async()=>{const f=await fixture();f.ready();const s=await f.read();assert.equal(s.ready,true);assert.equal(s.actions.performance,true);assert.equal(s.performance.scope,'game');assert.equal(s.performance.side,'ac');assert.equal(s.performance.controls.find(c=>c.key==='tdp').value,20);assert(!Object.hasOwn(s.game.fields,'acMode'));assert(!Object.hasOwn(s.game.fields,'dcMode'));assert(!Object.hasOwn(s.game.fields,'corePolicyMode'));assert(!Object.hasOwn(s.game.fields,'hyperThreadPolicy'));assert.equal(f.counts.capabilities,0);assert.equal(f.counts.saves,0);f.stop();});
await check('old game mode/core commands are rejected without a save',async()=>{const f=await fixture();f.ready();const s=await f.read();for(const [field,value] of [['acMode','performance'],['dcMode','extreme'],['corePolicyMode','only-small'],['hyperThreadPolicy','off']]){const r=await f.request('game.setField',{field,value},s);assert.equal(r.ok,false,field);}assert.equal(f.counts.saves,0);f.stop();});
await check('performance patch crosses host relay and saves one shared owner bank',async()=>{const f=await fixture();f.ready();const s=await f.read();const r=await f.request('performance.setControls',{patch:{tdp:22}},s);assert.equal(r.ok,true);assert.equal(f.memory.raw().entries['game.exe'].controls.ac.tdp,22);assert.equal(f.memory.raw().entries['game.exe'].controls.dc.tdp,10);assert.equal(f.counts.performanceUpdates,1);f.stop();});
await check('input edit preserves full continuous controls',async()=>{const f=await fixture();f.ready();const s=await f.read();await f.request('performance.setControls',{patch:{tdp:24}},s);const fresh=await f.read();const before=structuredClone(f.memory.raw().entries['game.exe'].controls);const r=await f.request('game.setField',{field:'padPersona',value:'elite'},fresh);assert.equal(r.ok,true);assert.deepEqual(f.memory.raw().entries['game.exe'].controls,before);f.stop();});
await check('performance source and power generation reject stale writes',async()=>{const f=await fixture();f.ready();const s=await f.read();f.state.power={...f.state.power,generation:2};f.powerChanged();await flush();const r=await f.request('performance.setControls',{patch:{tdp:25}},s);assert.equal(r.ok,false);assert.equal(f.counts.performanceUpdates??0,0);f.stop();});
await check('wake held by native power stays connected read-only then recovers after committed resume',async()=>{
 const f=await fixture(seed(),{invoke:async command=>command==='deckySidebar.mirrorAttach'?{connected:false,owner:'YMCC-native',pid:8612}:true});f.ready();const before=await f.read();f.state.power={...f.state.power,generation:2,phase:'suspended',hardwareWritesAllowed:false};f.powerChanged();const held=await f.read();
 assert.equal(held.ready,false);assert.match(held.notice,/已连接 YMCC.*等待唤醒完成/);assert.equal(held.provenance.kind,'ymcc-native');
 const rejected=await f.request('performance.setControls',{patch:{tdp:25}},held);assert.equal(rejected.ok,false);assert.equal(f.counts.performanceUpdates??0,0);
 f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.emit('power.resumed',{generation:2});const after=await f.read();assert.equal(after.ready,true);assert.equal(after.generation,2);assert.equal(f.counts.performanceUpdates??0,0);f.stop();
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
 f.gameChanged();const fresh=await f.read();assert.notEqual(fresh.game.identity,old.game.identity);const accepted=await f.request('game.setField',{field:'padPersona',value:'elite'},fresh);assert.equal(accepted.ok,true);assert.equal(f.counts.saves,1);f.stop();
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
await check('unknown versus confirmed empty target cannot authorize a global input write from an old receipt',async()=>{
 const f=await fixture();f.state.game=null;f.ready();const old=await f.read();assert.equal(old.actions.global,true);f.state.gameCurrent=false;
 const r=await f.request('global.setField',{field:'padPersona',value:'elite'},old);assert.equal(r.ok,false);assert.equal(f.counts.inputSaves,0);assert.equal((await f.read()).actions.global,false);f.stop();
});
await check('native target becomes pending during an awaited power read and cancels before the performance write',async()=>{
 let blocking=false,release;const f=await fixture(seed(),{readHooks:{power:async()=>{if(blocking)await new Promise(resolve=>release=resolve);}}});f.ready();const old=await f.read(),before=f.replies.length;blocking=true;
 await f.request('performance.setControls',{patch:{tdp:25}},old);assert.equal(typeof release,'function');f.state.gameCurrent=false;blocking=false;release();await flush();
 const reply=f.replies.slice(before).find(x=>x.message.type==='reply')?.message;assert.equal(reply?.ok,false);assert.equal(f.counts.performanceUpdates??0,0);f.stop();
});


await check('readiness-only failure and same-target recovery repaint once per edge without repeated reads or writes',async()=>{
 for(const hasGame of [true,false]){
  const f=await fixture();if(!hasGame)f.state.game=null;f.ready();const healthy=await f.read();await flush();
  const latest=()=>f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
  const snapshotCount=()=>f.replies.filter(x=>x.message.type==='snapshot').length;
  const identity=healthy.game?.identity??null;assert.equal(f.readinessSubscriberCount(),1);assert.equal(healthy.actions.performance,true);assert.equal(healthy.actions.frames,true);assert.equal(healthy.actions.game,hasGame);assert.equal(healthy.actions.global,!hasGame);
  let counts=structuredClone(f.counts),snapshots=snapshotCount();for(let i=0;i<1000;i++)f.setGameReadiness(true);await flush();assert.deepEqual(f.counts,counts);assert.equal(snapshotCount(),snapshots);
  f.setGameReadiness(false);await flush();const failed=latest();assert.equal(snapshotCount(),snapshots+1);assert.equal(failed.game?.identity??null,identity);assert.equal(failed.actions.performance,false);assert.equal(failed.actions.frames,false);assert.equal(failed.actions.game,false);assert.equal(failed.actions.global,false);assert.equal(failed.actions.monitor,true);assert.equal(failed.actions.audioOptimization,true);assert.equal(f.counts.power,counts.power+1);
  counts=structuredClone(f.counts);snapshots=snapshotCount();for(let i=0;i<1000;i++)f.setGameReadiness(false);await flush();assert.deepEqual(f.counts,counts);assert.equal(snapshotCount(),snapshots);
  // No gameChanged call: the same PID/birth/native lease simply becomes usable.
  f.setGameReadiness(true);await flush();const recovered=latest();assert.equal(snapshotCount(),snapshots+1,'Same-target readiness recovery must push a refreshed snapshot');assert.equal(recovered.game?.identity??null,identity);assert.equal(recovered.actions.performance,true);assert.equal(recovered.actions.frames,true);assert.equal(recovered.actions.game,hasGame);assert.equal(recovered.actions.global,!hasGame);assert.equal(f.counts.power,counts.power+1);
  assert.equal(f.counts.gameReconciles,1);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.counts.globalFrameSaves,0);assert.equal(f.counts.frameApplies,0);assert.equal(f.counts.monitorSaves,0);assert.equal(f.counts.fxSoundWrites,0);
  counts=structuredClone(f.counts);snapshots=snapshotCount();for(let i=0;i<1000;i++)f.setGameReadiness(true);await flush();assert.deepEqual(f.counts,counts);assert.equal(snapshotCount(),snapshots);
  f.stop();assert.equal(f.readinessSubscriberCount(),0);assert.equal(f.observerStats().game,0);assert.equal(f.observerStats().watchers,0);const replies=f.replies.length;counts=structuredClone(f.counts);
  for(let i=0;i<1000;i++){f.setGameReadiness(false);f.setGameReadiness(true);}await flush();assert.equal(f.replies.length,replies);assert.deepEqual(f.counts,counts);
 }
});

const report={status:'passed',cases,productionHostExecutedInVm:true,actualHardwareWrites:0,oldTierCommandsRejected:true,continuousOwnerWired:true};console.log(JSON.stringify(report,null,2));
