// Actual host, authenticated relay, profile adapter and original queued store; memory IO only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
import {base,seed,main,performanceControlsModule} from './fixtures/decky_game_memory_store.mjs';
import {validation} from './fixtures/decky_task_paths.mjs';
const same=(a,b)=>assert.deepEqual(structuredClone(a),structuredClone(b));
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true},cases=[];
const flush=async()=>{for(let i=0;i<220;i++)await Promise.resolve();};
async function fixture(initial={version:1,entries:{},rtss:{}},options={}) {
 const f=await createMirrorHostFixture({initial,...options});let sequence=0;f.ready();
 f.read=async()=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();const s=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;assert(s);return s;};
 f.request=async(value,snapshot,args={field:'profile',value},command='game.setField')=>{
  const before=f.replies.length,s=snapshot??await f.read(),n=++sequence;
  const packet={...peer,request:{command,runId,id:'profile:'+n,clientId:'profile',sequence:n,generation:s.generation,revision:s.revision,identity:s.game?.identity??'',args}};
  f.lastPacket=packet;f.emit('deckySidebar.mirrorRequest',packet);await flush();
  const reply=f.replies.slice(before).find(x=>x.message.type==='reply')?.message;assert(reply,'Profile command must produce a receipt');return reply;
 };
 return f;
}
async function check(name,fn){await fn();cases.push(name);}
await check('snapshot exposes a global profile entry with an opaque stable revision, without leaking private admission',async()=>{
 const f=await fixture(),s=await f.read();assert.equal(s.game.fields.profile.value,'global');assert(s.game.fields.profile.sourceRevision);assert.equal(s.game.fields.profile.supported,true);
 assert(!JSON.stringify(s).includes('profileSource'));assert.equal(f.memory.stats().fakeWrites,0);f.stop();
});
await check('setting dedicated imports both current power banks, exact frame settings and current gyro/persona',async()=>{
 const f=await fixture();f.state.schedule.controls=performanceControlsModule.normalizeContinuousControls({ac:{tdp:31,tdpCeiling:40,auto:{bias:10}},dc:{tdp:12,tdpCeiling:25,auto:{bias:75}}});
 f.state.globalFrames={ac:{fps:95,ceiling:120,lastFps:95},dc:{fps:0,ceiling:60,lastFps:45}};
 const controls=structuredClone(f.state.schedule.controls),frames=structuredClone(f.state.globalFrames),s=await f.read(),r=await f.request('dedicated',s);
 assert.equal(r.ok,true,JSON.stringify(r));const c=f.memory.view(),entry=c.entries['game.exe'];
 same(entry.controls,controls);assert.equal(entry.enabled,true);assert.equal(entry.padPersona,'steamdeck');assert.equal(entry.gyroOverride,'off');
 assert.equal(c.rtss['game.exe'].acFps,95);assert.equal(c.rtss['game.exe'].dcFps,0);assert.equal(c.rtss['game.exe'].dcLastFps,45);
 same(f.state.globalFrames,frames);same(f.state.schedule.controls,controls);assert.equal(f.counts.profileImports,1);assert.equal((await f.read()).game.fields.profile.value,'dedicated');f.stop();
});
await check('import replaces a dormant old record with the currently displayed global configuration',async()=>{
 const initial=seed();initial.entries['game.exe'].enabled=false;initial.entries['game.exe'].controls=performanceControlsModule.normalizeContinuousControls({ac:{tdp:7}});
 const f=await fixture(initial),s=await f.read(),r=await f.request('dedicated',s);assert.equal(r.ok,true,JSON.stringify(r));assert.notEqual(f.memory.view().entries['game.exe'].controls.ac.tdp,7);f.stop();
});
await check('import preserves every unrelated EXE record and RTSS record',async()=>{
 const initial=seed();delete initial.entries['game.exe'];const f=await fixture(initial),before=structuredClone(f.memory.view());assert.equal((await f.request('dedicated')).ok,true);
 same(f.memory.view().entries['other.exe'],before.entries['other.exe']);same(f.memory.view().rtss['other.exe'],before.rtss['other.exe']);f.stop();
});
await check('deleting dedicated removes its complete profile and RTSS entry and returns the display to global',async()=>{
 const f=await fixture(seed()),before=structuredClone(f.memory.view()),r=await f.request('global');assert.equal(r.ok,true,JSON.stringify(r));
 assert.equal(f.memory.view().entries['game.exe'],undefined);assert.equal(f.memory.view().rtss['game.exe'],undefined);
 same(f.memory.view().entries['other.exe'],before.entries['other.exe']);same(f.memory.view().rtss['other.exe'],before.rtss['other.exe']);
 assert.equal((await f.read()).game.fields.profile.value,'global');assert.equal(f.counts.profileRemovals,1);assert.equal(f.counts.profileRestores,1);f.stop();
});
await check('deletion remains available when only the performance read has temporarily failed',async()=>{const f=await fixture(seed());f.state.performanceReadError=true;const s=await f.read();assert.equal(s.performance,undefined);assert.equal(s.game.fields.profile.supported,true);assert.equal((await f.request('global',s)).ok,true);assert.equal(f.memory.view().entries['game.exe'],undefined);f.stop();});
await check('replaying the same authenticated import packet returns its receipt without creating twice',async()=>{
 const f=await fixture();assert.equal((await f.request('dedicated')).ok,true);const n=f.replies.length;f.emit('deckySidebar.mirrorRequest',structuredClone(f.lastPacket));await flush();
 assert.equal(f.replies.slice(n).find(x=>x.message.type==='reply')?.message.ok,true);assert.equal(f.counts.profileImports,1);f.stop();
});
await check('a switched current game rejects the captured import instead of creating for the replacement',async()=>{
 const f=await fixture(),s=await f.read();f.state.game={...f.state.game,name:'replacement.exe',path:'C:/Games/Replacement.exe',pid:202,processCreated:'different'};
 const r=await f.request('dedicated',s);assert.equal(r.ok,false);assert.equal(Object.keys(f.memory.view().entries).length,0);f.stop();
});
await check('a change to the inactive DC bank rejects the earlier current-configuration import',async()=>{
 const f=await fixture();f.state.schedule.controls=performanceControlsModule.normalizeContinuousControls({});const s=await f.read();f.state.schedule.controls.dc.tdp=9;
 assert.equal((await f.request('dedicated',s)).ok,false);assert.equal(f.counts.profileImports??0,0);f.stop();
});
await check('a changed global FPS source rejects the earlier import without saving a partial entry',async()=>{
 const f=await fixture(),s=await f.read();f.state.globalFrames.dc.fps=45;assert.equal((await f.request('dedicated',s)).ok,false);assert.equal(f.counts.profileImports??0,0);f.stop();
});
await check('a changed global gyro source rejects the earlier import',async()=>{
 const f=await fixture(),s=await f.read();f.state.gyroDefaults.source='changed-input-source';assert.equal((await f.request('dedicated',s)).ok,false);assert.equal(f.counts.profileImports??0,0);f.stop();
});
await check('power recovery or a new power generation rejects profile writes',async()=>{
 for(const update of [{phase:'resuming'},{generation:2},{hardwareWritesAllowed:false}]){const f=await fixture(),s=await f.read();Object.assign(f.state.power,update);assert.equal((await f.request('dedicated',s)).ok,false);assert.equal(f.counts.profileImports??0,0);f.stop();}
});
await check('blocked native virtual-output policy is honored when importing the current input preset',async()=>{
 const f=await fixture();f.state.virtualPolicy={steamDeckSelfOutputBlocked:true,reason:'fixture-native-policy'};const r=await f.request('dedicated');assert.equal(r.ok,false);assert.equal(f.counts.profileImports??0,0);f.stop();
});
await check('malformed profile values or extra arguments cannot enter the store',async()=>{
 for(const args of [{field:'profile',value:'reset'},{field:'profile',value:'dedicated',exe:'other.exe'}]){const f=await fixture();assert.equal((await f.request(undefined,undefined,args)).ok,false);assert.equal(f.counts.profileImports??0,0);f.stop();}
});
await check('the dedicated import does not overwrite an entry already active in the current snapshot',async()=>{
 const f=await fixture(seed()),before=JSON.stringify(f.memory.view());const r=await f.request('dedicated');assert.equal(r.ok,true);assert.equal(r.result.saved,false);assert.equal(f.counts.profileImports??0,0);assert.equal(JSON.stringify(f.memory.view()),before);f.stop();
});
await check('original store commit failure leaves the authoritative config and global owner unchanged',async()=>{
 const f=await fixture();const s=await f.read();f.memory.failNext();assert.equal((await f.request('dedicated',s)).ok,false);assert.equal(f.memory.view().entries['game.exe'],undefined);assert.equal(f.counts.profileRestores??0,0);f.stop();
});
await check('apply failure after a successful import reports saved and pending accurately',async()=>{
 const f=await fixture();f.state.profileRestoreError=true;const r=await f.request('dedicated');assert.equal(r.ok,true);assert.equal(r.result.saved,true);assert.equal(r.result.applied,false);assert.equal(r.result.pending,true);assert.equal(f.memory.view().entries['game.exe'].enabled,true);f.stop();
});
await check('apply failure after deletion still reports that the dedicated entry has been removed',async()=>{
 const f=await fixture(seed());f.state.profileRestoreError=true;const r=await f.request('global');assert.equal(r.ok,true);assert.equal(r.result.saved,true);assert.equal(r.result.pending,true);assert.equal(f.memory.view().entries['game.exe'],undefined);f.stop();
});
await check('unrelated queued writes are preserved when the original store imports one current game',async()=>{
 const initial=seed();delete initial.entries['game.exe'];const f=await fixture(initial);const current=f.memory.view(),next=structuredClone(current);next.entries['other.exe'].displayName='Concurrent edit';
 let release;f.memory.holdNext(new Promise(r=>release=r));const ahead=f.memory.store.saveGameCustomConfig(next);await flush();
 const controls=performanceControlsModule.normalizeContinuousControls({});
 const importing=f.memory.store.importGameCustomProfile({exe:'game.exe',pid:f.state.game.pid,processCreated:f.state.game.processCreated,generation:1},JSON.stringify([null,null]),{enabled:true,displayName:'Current game',ac:{...base},dc:{...base},controls},f.state.globalFrames);
 release();await ahead;await importing;assert.equal(f.memory.view().entries['other.exe'].displayName,'Concurrent edit');assert(f.memory.view().entries['game.exe']);f.stop();
});
await check('changing the target record rejects a captured deletion through the original owner CAS guard',async()=>{
 const f=await fixture(seed()),c=f.memory.view(),rev=JSON.stringify([c.entries['game.exe'],c.rtss['game.exe']]);c.entries['game.exe'].displayName='Edited after confirmation';await f.memory.store.saveGameCustomConfig(c);
 await assert.rejects(f.memory.store.removeGameCustomProfile({exe:'game.exe',pid:f.state.game.pid,processCreated:f.state.game.processCreated,generation:1},rev));assert(f.memory.view().entries['game.exe']);f.stop();
});

await check('a controller edit in recognized-game global scope saves input CAS without importing a dedicated profile',async()=>{
 const f=await fixture(),s=await f.read(),r=await f.request('elite',s,{field:'padPersona',value:'elite'});
 assert.equal(r.ok,true,JSON.stringify(r));assert.equal(f.state.input.outputTarget.persona,'elite');assert.equal(f.counts.inputSaves,1);
 assert.equal(f.memory.view().entries['game.exe'],undefined);assert.equal(f.memory.stats().fakeWrites,0);
 const next=await f.read();assert.equal(next.game.fields.profile.value,'global');assert.equal(next.game.fields.padPersona.value,'elite');f.stop();
});
await check('global gyro toggle and preset in a recognized game never implicitly create dedicated configuration',async()=>{
 const f=await fixture();assert.equal((await f.request('on',null,{field:'gyroEnabled',value:'on'})).ok,true);
 assert.equal((await f.request('racing',null,{field:'gyroPreset',value:'racing'})).ok,true);
 assert.equal(f.state.input.gyroMotion.preset,'racing');assert.equal(f.counts.inputSaves,2);assert.equal(f.memory.stats().fakeWrites,0);
 assert.equal((await f.read()).game.fields.profile.value,'global');f.stop();
});
await check('dormant input records display global values and stay byte-for-byte dormant after a global edit',async()=>{
 const initial=seed();initial.entries['game.exe'].enabled=false;initial.entries['game.exe'].padPersona='elite';
 const f=await fixture(initial),before=f.memory.view(),s=await f.read();assert.equal(s.game.fields.padPersona.value,'steamdeck');
 assert.equal((await f.request('dualsense-edge',s,{field:'padPersona',value:'dualsense-edge'})).ok,true);
 same(f.memory.view(),before);assert.equal(f.counts.inputSaves,1);assert.equal((await f.read()).game.fields.profile.value,'global');f.stop();
});
await check('active dedicated input still saves only the original EXE profile',async()=>{
 const f=await fixture(seed()),s=await f.read();assert.equal((await f.request('elite',s,{field:'padPersona',value:'elite'})).ok,true);
 assert.equal(f.memory.view().entries['game.exe'].padPersona,'elite');assert.equal(f.counts.inputSaves,0);assert.equal((await f.read()).game.fields.profile.value,'dedicated');f.stop();
});
await check('external deletion without a renderer event refreshes profile value and allows a new explicit import',async()=>{
 const f=await fixture(seed()),before=await f.read();assert.equal(before.game.fields.profile.value,'dedicated');
 const raw=f.memory.raw();delete raw.entries['game.exe'];delete raw.rtss['game.exe'];f.memory.documents.set(main,JSON.stringify(raw));
 const next=await f.read();assert.equal(next.game.fields.profile.value,'global');assert.notEqual(next.game.fields.profile.sourceRevision,before.game.fields.profile.sourceRevision);
 assert.equal((await f.request('dedicated',next)).ok,true);assert.equal(f.counts.profileImports,1);f.stop();
});
await check('deletion during an asynchronous snapshot cannot publish an old dedicated value with a new receipt',async()=>{
 let owner,removed=false;
 const f=await fixture(seed(),{readHooks:{performance:async()=>{if(!removed){removed=true;const raw=owner.memory.raw();delete raw.entries['game.exe'];delete raw.rtss['game.exe'];owner.memory.documents.set(main,JSON.stringify(raw));}}}});owner=f;
 const s=await f.read();assert.equal(s.game.fields.profile.value,'global');assert.equal(f.replies.filter(r=>r.message.type==='snapshot').some(r=>r.message.snapshot.game?.fields.profile.value==='dedicated'),false);f.stop();
});
await check('an unseen external profile import blocks a captured global input edit before input CAS',async()=>{
 const f=await fixture(),s=await f.read();f.memory.documents.set(main,JSON.stringify(seed()));
 assert.equal((await f.request('elite',s,{field:'padPersona',value:'elite'})).ok,false);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.saves,0);f.stop();
});
await check('a profile deleted after a dedicated input receipt cannot be recreated by the stale input command',async()=>{
 const f=await fixture(seed()),s=await f.read(),raw=f.memory.raw();delete raw.entries['game.exe'];delete raw.rtss['game.exe'];f.memory.documents.set(main,JSON.stringify(raw));
 assert.equal((await f.request('elite',s,{field:'padPersona',value:'elite'})).ok,false);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.memory.view().entries['game.exe'],undefined);f.stop();
});


await check('an input-only edit cannot silently migrate or create continuous performance banks in an old active profile',async()=>{
 const f=await fixture(seed()),before=structuredClone(f.memory.view().entries['game.exe']);assert.equal(before.controls,undefined);
 const r=await f.request('elite',null,{field:'padPersona',value:'elite'});assert.equal(r.ok,true);const after=f.memory.view().entries['game.exe'];
 assert.equal(after.padPersona,'elite');assert.equal(after.controls,undefined,'controller intent must not create TDP/performance memory');same(after.ac,before.ac);same(after.dc,before.dc);assert.equal(f.counts.inputSaves,0);f.stop();
});

fs.writeFileSync(path.join(validation,'PROFILE-IMPORT-REMOVE.json'),JSON.stringify({status:'passed',cases:cases.length,results:cases,productionHost:true,productionRelay:true,productionProfileAdapter:true,productionQueuedStore:true,realHardwareWrites:0},null,2)+'\n');
console.log('Profile import/remove PASS '+cases.length);
