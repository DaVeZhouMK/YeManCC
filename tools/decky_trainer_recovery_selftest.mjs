import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createTrainerRecoveryFixture,field,flush} from './fixtures/game_trainer_recovery_fixture.mjs';
const root=path.resolve(import.meta.dirname,'..'),clone=structuredClone;
const game=()=>clone(field.game),receipt=()=>clone(Object.values(field.index)[0]);
const options=g=>({processTarget:g,isTargetCurrent:()=>true});
const ready=(f,g=f.fx.game)=>f.trainer.getReadyGameTrainer(g.title,g.path,g);
const noSideEffects=f=>{assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.writes.length,0);};
const setReceipt=(f,g,r)=>f.fx.seedIndex({[f.trainer.trainerGameKey(g.title)]:r});
const runId='b'.repeat(32),peer={connection:'1',runId,connected:true};
async function hostFor(f,hostSource=null){
 const {createMirrorHostFixture}=await import('./fixtures/decky_mirror_host_fixture.mjs');
 const host=await createMirrorHostFixture({trainerOwner:f.trainer,trainerRecovery:f.recovery,hostSource});
 host.state.game=f.fx.game;host.ready();
 host.open=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.latest();};
 host.latest=()=>host.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;
 host.pause=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();};
 host.select=g=>{host.state.game=g;f.fx.select(g);host.gameChanged();};
 await host.open();return host;
}
export async function trainerRecoveryTests(out,{caseFilter=null}={}){
 if(caseFilter!==null&&(typeof caseFilter!=='string'||!caseFilter.trim()))throw Error('A nonempty exact case name fragment is required');
 fs.mkdirSync(out,{recursive:true});const cases=[],skipped=[],started=performance.now(),wireVariants=[];
 const inputs=['src/bridge/gameTrainer.ts','src/bridge/gameTrainerRecovery.ts','src/bridge/gameTrainerAuto.ts','tools/decky_trainer_recovery_selftest.mjs','tools/fixtures/game_trainer_recovery_fixture.mjs','tools/fixtures/trainer_ready_tmnt_20261022.json','tools/fixtures/decky_mirror_host_fixture.mjs','tsconfig.json','package.json','node_modules/esbuild/lib/main.js','node_modules/@esbuild/win32-x64/esbuild.exe'];
 if(caseFilter===null||'a hydrated local trainer prevents automatic download work when automatic preparation is enabled'.includes(caseFilter))inputs.push('src/bridge/gamedetect.ts','src/bridge/gamePolicyTarget.ts','src/bridge/gamePolicyHysteresis.ts','PowerControl/GameTitlePolicy.json');
 const pin=p=>({path:p,sha256:createHash('sha256').update(fs.readFileSync(path.resolve(root,p))).digest('hex')});
 const before=inputs.map(pin),runtime={path:process.execPath,version:process.version,sha256:pin(process.execPath).sha256};
 const finish=extra=>{
  const after=inputs.map(pin),drift=after.filter((v,i)=>v.sha256!==before[i].sha256);
  const report={wireVariants,actualNativeRuleElectionExecuted:false,completeRegression:caseFilter===null,passed:cases.every(c=>c.passed)&&cases.length>0&&drift.length===0,cases:cases.length,checks:cases,caseFilter,skipped,elapsedMs:performance.now()-started,inputs:before,sourceDrift:drift,runtime,realProcessLaunches:0,realGameMemoryReads:0,userSettingsWrites:0,realSteamActions:0,...extra};
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));return report;
 };
 const check=async(name,fn)=>{
  if(caseFilter!==null&&!name.includes(caseFilter)){skipped.push(name);return;}
  try{await fn();cases.push({name,passed:true});}catch(error){cases.push({name,passed:false,error:String(error),stack:error.stack});finish({passed:false});throw error;}
 };
 const indexReads=f=>f.fx.reads.filter(r=>r.p===f.fx.indexPath).length;
 const installFault=(f,kind)=>{
  f.fx.seedIndex();
  if(kind==='read')f.fx.readHook=async()=>{throw Error('owned IPC read denied');};
  else if(kind==='timeout')f.fx.readHook=()=>new Promise(()=>{});
  else if(kind==='native-oversize')f.fx.readHook=async()=>{throw Error('FILE_READ_LIMIT_EXCEEDED');};
  else if(kind==='future')f.fx.put(f.fx.indexPath,JSON.stringify({schemaVersion:2}));
  else if(kind==='entry-future'){const r=receipt();r.storageVersion=3;setReceipt(f,game(),r);}
  else if(kind==='entry-type'){const r=receipt();r.identity.processId='42';setReceipt(f,game(),r);}
  else if(kind==='oversize')f.fx.put(f.fx.indexPath,' '.repeat((8<<20)+1));
  else if(kind==='entry-limit')f.fx.put(f.fx.indexPath,JSON.stringify(Object.fromEntries(Array.from({length:4097},(_,i)=>['other'+i,{}]))));
  else f.fx.put(f.fx.indexPath,({empty:'',truncated:'{',array:'[]',null:'null',nonfinite:'{"n":1e999}'})[kind]);
 };
 const expectStormQuiet=async(overrides={})=>{
  const f=await createTrainerRecoveryFixture(overrides);f.fx.game=game();installFault(f,'read');
  const stop=f.recovery.startGameTrainerRecovery();
  try{await flush();assert.equal(indexReads(f),1);assert.equal(f.fx.timers.size,0,'cache read fault must stop timer polling');
   for(let i=0;i<12;i++){f.fx.select(clone(f.fx.game));await f.fx.advance(60000);}
   assert.equal(indexReads(f),1,'same-target events and time must not retry a faulted cache');assert.equal(ready(f),null);noSideEffects(f);
  }finally{stop();}
 };
 const expectHealthyAbsence=async(overrides={})=>{
  const f=await createTrainerRecoveryFixture(overrides);f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery();
  try{await flush();assert.equal(f.fx.timers.size,1,'healthy absence must retain late-result recovery');f.fx.seedIndex();await f.fx.advance(5000);assert(ready(f),'healthy late receipt must recover');assert.equal(f.fx.hashes.length,2);noSideEffects(f);}finally{stop();}
 };
 await check('cache-fault: read parse future oversize and timeout stop current-attempt timer storms',async()=>{
  for(const kind of ['read','timeout','native-oversize','empty','truncated','array','null','nonfinite','future','entry-future','entry-type','oversize','entry-limit']){
   const f=await createTrainerRecoveryFixture();f.fx.game=game();installFault(f,kind);const original=f.fx.files.get(f.fx.norm(f.fx.indexPath)),stop=f.recovery.startGameTrainerRecovery();
   try{await flush();if(kind==='timeout')await f.fx.advance(5001);await flush();assert.equal(indexReads(f),1,kind);assert.equal(f.fx.timers.size,0,kind+': fault must stop polling');
    for(let i=0;i<12;i++){f.fx.select(clone(f.fx.game));await f.fx.advance(60000);}
    assert.equal(indexReads(f),1,kind+': later time/same target cannot retry');assert.equal(ready(f),null,kind);assert.equal(f.fx.hashes.length,0,kind);assert.equal(f.fx.files.get(f.fx.norm(f.fx.indexPath)),original,kind);noSideEffects(f);
   }finally{stop();}
  }
 });
 await check('cache-fault: optional callbacks classify faults and reach all deduplicated current callers',async()=>{
  for(const [kind,expected] of [['read','read'],['timeout','read'],['native-oversize','oversize'],['empty','corrupt'],['array','corrupt'],['future','future'],['entry-future','future'],['entry-type','corrupt'],['oversize','oversize'],['entry-limit','oversize']]){
   const f=await createTrainerRecoveryFixture(),g=game(),seen=[];installFault(f,kind);
   const attempts=Array.from({length:4},(_,i)=>f.trainer.restoreReadyGameTrainer(g.title,g.path,{...options(g),...(i?{onCacheFault:value=>seen.push([i,value])}:{})}));
   await flush();if(kind==='timeout')await f.fx.advance(5001);assert((await Promise.all(attempts)).every(v=>v===null));
   assert.equal(indexReads(f),1,kind);assert.deepEqual(seen,[[1,expected],[2,expected],[3,expected]],kind);assert.equal(f.fx.timers.size,0);noSideEffects(f);
  }
 });
 await check('cache-fault: explicit recheck retries once and restored IO permits verified ready',async()=>{
  for(const kind of ['read','empty','future','oversize']){
   const f=await createTrainerRecoveryFixture();f.fx.game=game();installFault(f,kind);const stop=f.recovery.startGameTrainerRecovery();
   try{await flush();assert.equal(f.fx.timers.size,0,kind);f.recovery.refreshGameTrainerRecovery();await flush();assert.equal(indexReads(f),2,kind);assert.equal(f.fx.timers.size,0,kind);
    f.fx.readHook=null;f.fx.seedIndex();f.recovery.refreshGameTrainerRecovery();await flush();assert.equal(indexReads(f),3,kind);assert(ready(f),kind);assert.equal(f.fx.hashes.length,2);assert.equal(f.fx.timers.size,0);noSideEffects(f);
   }finally{stop();}
  }
 });
 await check('cache-fault: owned resume and settings source events clear faults for one attempt',async()=>{
  for(const event of ['ipc:power.resumed','ui-settings:changed']){
   const f=await createTrainerRecoveryFixture();f.fx.game=game();installFault(f,'read');const stop=f.recovery.startGameTrainerRecovery();
   try{await flush();f.fx.emit(event);await flush();assert.equal(indexReads(f),2,event);assert.equal(f.fx.timers.size,0);f.fx.readHook=null;f.fx.seedIndex();if(event==='ui-settings:changed')f.fx.generation++;f.fx.emit(event);await flush();assert(ready(f),event);assert.equal(indexReads(f),3);assert.equal(f.fx.hashes.length,2);noSideEffects(f);}finally{stop();}
  }
 });
 await check('cache-fault: target or source identity change permits a fresh verified attempt',async()=>{
  for(const sourceChange of [false,true]){
   const f=await createTrainerRecoveryFixture();f.fx.game=game();installFault(f,'read');const stop=f.recovery.startGameTrainerRecovery();
   try{await flush();f.fx.readHook=null;f.fx.seedIndex();const g=game();if(sourceChange)f.fx.generation++;else{g.pid++;g.processCreated=String(BigInt(g.processCreated)+1n);const r=receipt();r.identity.processId=g.pid;r.identity.processCreated=g.processCreated;setReceipt(f,g,r);}
    f.fx.select(g);await flush();assert(ready(f),sourceChange?'source':'target');assert.equal(indexReads(f),2);assert.equal(f.fx.hashes.length,2);assert.equal(f.fx.timers.size,0);noSideEffects(f);}finally{stop();}
  }
 });
 await check('cache-fault: healthy missing or empty cache retains the existing late-result retry',async()=>{
  await expectHealthyAbsence();
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.put(f.fx.indexPath,'{}');const stop=f.recovery.startGameTrainerRecovery();
  try{await flush();assert.equal(indexReads(f),1);assert.equal(f.fx.timers.size,1);f.fx.seedIndex();await f.fx.advance(5000);assert(ready(f));assert.equal(f.fx.hashes.length,2);assert.equal(f.fx.timers.size,0);noSideEffects(f);}finally{stop();}
 });
 await check('cache-fault: identity path and dual-hash rejections remain healthy null results',async()=>{
  for(const kind of ['pid','birth','path','trainer-hash','game-hash']){
   const f=await createTrainerRecoveryFixture(),g=game(),r=receipt(),seen=[];f.fx.game=g;
   if(kind==='pid')r.identity.processId++;if(kind==='birth')r.identity.processCreated=String(BigInt(r.identity.processCreated)+1n);if(kind==='path')r.identity.executablePath='D:\Other\other.exe';setReceipt(f,g,r);
   if(kind==='trainer-hash')f.fx.hashValues.set(f.fx.norm(r.trainerPath),'0'.repeat(64));if(kind==='game-hash')f.fx.hashValues.set(f.fx.norm(g.path),'0'.repeat(64));
   assert.equal(await f.trainer.restoreReadyGameTrainer(g.title,g.path,{...options(g),onCacheFault:value=>seen.push(value)}),null,kind);assert.deepEqual(seen,[],kind);
   const stop=f.recovery.startGameTrainerRecovery();try{await flush();assert.equal(ready(f),null);assert.equal(f.fx.timers.size,1,kind);noSideEffects(f);}finally{stop();}
  }
 });
 await check('cache-fault: corrupt alias does not report a fault when a separate valid receipt restores',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game(),r=receipt(),k=f.trainer.trainerGameKey(g.title),seen=[];f.fx.seedIndex();f.fx.put(f.fx.indexPath,JSON.stringify({[k]:{...r,storageVersion:3},[k+'|valid']:r}));
  assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,{...options(g),onCacheFault:value=>seen.push(value)}));assert.deepEqual(seen,[]);assert.equal(f.fx.hashes.length,2);noSideEffects(f);
 });
 await check('cache-fault: stale old-target read failure cannot fault or hydrate the replacement target',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.seedIndex();let failOld;f.fx.readHook=()=>new Promise((_,reject)=>failOld=reject);const stop=f.recovery.startGameTrainerRecovery();
  try{await flush();assert(failOld);const g=game(),r=receipt();g.pid++;g.processCreated=String(BigInt(g.processCreated)+1n);r.identity.processId=g.pid;r.identity.processCreated=g.processCreated;setReceipt(f,g,r);f.fx.readHook=null;f.fx.select(g);await flush();assert(ready(f));failOld(Error('late old-target IO fault'));await flush();assert(ready(f));assert.equal(indexReads(f),2);assert.equal(f.fx.timers.size,0);noSideEffects(f);}finally{stop();}
 });
 await check('cache-fault: shutdown during a timed out read releases timers and forbids late publication',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();installFault(f,'timeout');const stop=f.recovery.startGameTrainerRecovery();await flush();stop();await f.fx.advance(5001);assert.equal(f.fx.timers.size,0);assert.equal(f.fx.listeners.size,0);assert.equal(ready(f),null);noSideEffects(f);
 });
 await check('cache-fault: cancelled first subscriber cannot hide a shared fault from the resident owner',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;f.fx.seedIndex();let rejectRead,callerCurrent=true;
  f.fx.readHook=()=>new Promise((_,reject)=>rejectRead=reject);
  const first=f.trainer.restoreReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:()=>callerCurrent,onCacheFault:()=>{throw Error('stale caller must not be notified');}});
  await flush();const stop=f.recovery.startGameTrainerRecovery();try{await flush();callerCurrent=false;rejectRead(Error('shared read refused'));await first;await flush();assert.equal(indexReads(f),1);assert.equal(f.fx.timers.size,0,'resident owner must observe the shared read fault');assert.equal(ready(f),null);noSideEffects(f);}finally{stop();}
 });
 await check('cache-fault: existence rejection timeout and invalid receipt are read faults never missing',async()=>{
  for(const kind of ['rejected','timeout','invalid']){
   const f=await createTrainerRecoveryFixture(),g=game(),seen=[];f.fx.game=g;f.fx.seedIndex();const has=f.fx.files.has.bind(f.fx.files);
   f.fx.files.has=p=>p===f.fx.norm(f.fx.indexPath)?kind==='rejected'?(()=>{throw Error('owned existence access denied');})():kind==='timeout'?new Promise(()=>{}):undefined:has(p);
   const attempt=f.trainer.restoreReadyGameTrainer(g.title,g.path,{...options(g),onCacheFault:value=>seen.push(value)});const stop=f.recovery.startGameTrainerRecovery();
   try{await flush();if(kind==='timeout')await f.fx.advance(5001);assert.equal(await attempt,null);await flush();assert.deepEqual(seen,['read'],kind);assert.equal(indexReads(f),0,kind);assert.equal(f.fx.timers.size,0,kind);assert.equal(ready(f),null);noSideEffects(f);}finally{stop();}
  }
 });
 await check('cache-fault: semantic negative removing fault gates reproduces repeated reads',async()=>{
  const source=fs.readFileSync(path.join(root,'src/bridge/gameTrainerRecovery.ts'),'utf8'),anchor='if (cacheFaulted) return;';assert(source.includes(anchor));
  await assert.rejects(expectStormQuiet({recoverySource:source.replaceAll(anchor,'')}),/cache read fault must stop timer polling|must not retry/);
 });
 await check('cache-fault: semantic negative treating every null as fault loses healthy late ready',async()=>{
  const source=fs.readFileSync(path.join(root,'src/bridge/gameTrainerRecovery.ts'),'utf8'),anchor='checking = null;\n          if (cacheFaulted) return;';assert(source.includes(anchor));
  await assert.rejects(expectHealthyAbsence({recoverySource:source.replace(anchor,'checking = null;\n          cacheFaulted = true;\n          if (cacheFaulted) return;')}),/healthy absence must retain late-result recovery/);
 });
 await check('field TMNT persisted ready receipt restores a cold owner and deduplicates aliases without writes',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.seedIndex();f.fx.game=g;
  const value=await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g));assert(value);assert.equal(value.path,receipt().trainerPath);assert.equal(value.gcmResourceId,receipt().gcmResourceId);assert.equal(f.fx.hashes.length,2);assert.equal(f.fx.reads.length,1);noSideEffects(f);
 });
 await check('late local result pushes Decky available without reopening or another snapshot request',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f);
  assert.equal(h.latest().trainer.available,false);assert.equal(f.fx.listeners.size,1);assert.equal(f.fx.timers.size,1);
  f.fx.seedIndex();await f.fx.advance(5000);assert.equal(h.latest().trainer.available,true);assert(ready(f));assert.equal(f.fx.timers.size,0);noSideEffects(f);h.stop();stop();
 });
 await check('confirmed ready state is static and repeated Decky checks perform no disk IO or timer work',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.seedIndex();const stop=f.recovery.startGameTrainerRecovery();await flush();const h=await hostFor(f),reads=f.fx.reads.length,hashes=f.fx.hashes.length;
  for(let i=0;i<10;i++){f.recovery.refreshGameTrainerRecovery();await f.fx.advance(5000);}
  assert.equal(f.fx.timers.size,0);assert.equal(f.fx.reads.length,reads);assert.equal(f.fx.hashes.length,hashes);assert.equal(f.fx.listeners.size,1);assert.equal(h.latest().trainer.available,true);noSideEffects(f);h.stop();stop();
 });
 await check('five successive game identities each get their own late result and never use the previous game trainer',async()=>{
  const f=await createTrainerRecoveryFixture(),stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f);
  for(const [idx,[title,appid,exe]]of [['Cyberpunk 2077','1091500','Cyberpunk2077.exe'],['Crusader Kings III','1158310','ck3.exe'],['ELDEN RING','1245620','eldenring.exe'],['Stardew Valley','413150','Stardew Valley.exe'],['God of War','1593500','GoW.exe']].entries()){
   const g={title,name:exe,path:'D:/Inert Games/'+idx+'/'+exe,pid:2000+idx,processCreated:String(134371333868323260n+BigInt(idx))},r=receipt();
   Object.assign(r,{gameName:title,trainerGameName:title,trainerFolder:'D:/Inert GCM/'+idx,trainerPath:'D:/Inert GCM/'+idx+'/Trainer.exe',gcmResourceId:'inert-resource-'+appid,sha256:String(idx+1).repeat(64)});
   Object.assign(r.identity,{processId:g.pid,processCreated:g.processCreated,executablePath:g.path,executableSha256:String(idx+5).repeat(64),gameName:title,windowTitle:title,steamAppId:appid,aliases:[title]});
   h.select(g);await flush();assert.equal(ready(f,g),null);assert.equal(h.latest().trainer.available,false,title);
   setReceipt(f,g,r);await f.fx.advance(5000);assert.equal(ready(f,g).gcmResourceId,r.gcmResourceId);assert.equal(h.latest().trainer.available,true,title);assert.equal(f.fx.timers.size,0);
  }
  noSideEffects(f);h.stop();stop();
 });
 await check('same EXE game restart rejects old PID and process birth then accepts a newly bound receipt',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;f.fx.seedIndex();const stop=f.recovery.startGameTrainerRecovery();await flush();assert(ready(f));
  const next={...g,pid:g.pid+1,processCreated:String(BigInt(g.processCreated)+1n)};f.fx.select(next);await flush();assert.equal(ready(f,next),null);
  const r=receipt();Object.assign(r.identity,{processId:next.pid,processCreated:next.processCreated});setReceipt(f,next,r);await f.fx.advance(5000);assert(ready(f,next));assert.equal(f.fx.timers.size,0);noSideEffects(f);stop();
 });
 await check('wrong PID birth EXE identity and missing identity fields never restore ready',async()=>{
  for(const patch of [{processId:13485},{processCreated:'134371333868323260'},{executablePath:'D:/OtherGame.exe'},{executableSha256:''},{snapshotPath:''},{gameName:'Another Game'}]){
   const f=await createTrainerRecoveryFixture(),g=game(),r=receipt();Object.assign(r.identity,patch);setReceipt(f,g,r);assert.equal(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)),null,JSON.stringify(patch));noSideEffects(f);
  }
 });
 await check('missing trainer changed trainer bytes and changed game bytes all reject the cached receipt',async()=>{
  for(const kind of ['missing','trainer-hash','game-hash']){
   const f=await createTrainerRecoveryFixture(),g=game(),r=receipt();f.fx.seedIndex();
   if(kind==='missing')f.fx.files.delete(f.fx.norm(r.trainerPath));else f.fx.hashValues.set(f.fx.norm(kind==='trainer-hash'?r.trainerPath:g.path),'0'.repeat(64));
   assert.equal(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)),null,kind);assert.equal(ready(f,g),null);noSideEffects(f);
  }
 });
 await check('corrupt index malformed ready paths and conflicting resources fail closed',async()=>{
  for(const kind of ['broken-json','array','malformed-path','ambiguous']){
   const f=await createTrainerRecoveryFixture(),g=game(),r=receipt();f.fx.seedIndex();
   if(kind==='broken-json')f.fx.put(f.fx.indexPath,'{');
   if(kind==='array')f.fx.put(f.fx.indexPath,'[]');
   if(kind==='malformed-path'){r.trainerPath=42;f.fx.put(f.fx.indexPath,JSON.stringify({[f.trainer.trainerGameKey(g.title)]:r}));}
   if(kind==='ambiguous'){const k=f.trainer.trainerGameKey(g.title);f.fx.put(f.fx.indexPath,JSON.stringify({[k]:r,[k+'|another-hash']:{...r,gcmResourceId:'different-resource'}}));}
   assert.equal(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)),null,kind);noSideEffects(f);
  }
 });
 await check('concurrent consistency checks share a single index read and validation',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.seedIndex();const results=await Promise.all(Array.from({length:12},()=>f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g))));assert(results.every(Boolean));assert.equal(f.fx.reads.length,1);assert.equal(f.fx.hashes.length,2);noSideEffects(f);
 });
 await check('game switch during hashing discards the previous game completion',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.seedIndex();let release;f.fx.hashHook=()=>new Promise(resolve=>release=resolve);
  const stop=f.recovery.startGameTrainerRecovery();await flush();assert(release);const old=game();f.fx.select(null);release(receipt().sha256);await flush();assert.equal(ready(f,old),null);assert.equal(f.fx.timers.size,0);noSideEffects(f);stop();
 });
 await check('settings source switch during hashing cannot publish the old source',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.seedIndex();let release;f.fx.hashHook=()=>new Promise(resolve=>release=resolve);
  const p=f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g));await flush();f.fx.generation++;release(receipt().sha256);assert.equal(await p,null);assert.equal(ready(f,g),null);noSideEffects(f);
 });
 await check('a newer preparation owns readiness and cannot be overwritten by a concurrent disk restore',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.seedIndex();f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');let release;
  f.fx.workerHook=async(_p,args)=>{await new Promise(resolve=>release=resolve);const at=key=>args[args.indexOf(key)+1];f.fx.put(at('-ResultPath'),JSON.stringify(f.fx.requestReceipt(args,{...receipt(),ok:true})));return{ok:true};};
  const preparation=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});await flush();const reads=f.fx.reads.length;assert.equal(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)),null);assert.equal(f.fx.reads.length,reads);release();await preparation;assert(ready(f,g));assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);
  // Missing identity stays rejected by the unchanged production client.
  for(const key of ['requestProtocol','requestToken','sourceSession','gameExecutablePath','gameProcessId','gameProcessCreated']){
   const n=await createTrainerRecoveryFixture();n.fx.seedIndex();n.fx.put(n.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
   n.fx.workerHook=async(_p,args)=>{const result=n.fx.requestReceipt(args,{...receipt(),ok:true});delete result[key];const at=name=>args[args.indexOf(name)+1];n.fx.put(at('-ResultPath'),JSON.stringify(result));return {ok:true};};
   await assert.rejects(n.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g}),error=>String(error.message).endsWith(key),'Missing '+key+' must remain rejected');
   assert.equal(ready(n,g),null);assert.equal(n.fx.workers.length,1);assert.equal(n.fx.launches.length,0);assert.equal(n.fx.timers.size,0);
  }
 });
 await check('timeout followed by a later persisted ready receipt recovers without a second preparation worker',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async()=>({ok:true});
  const preparation=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g}),rejected=assert.rejects(preparation,/超时/);await flush();await f.fx.advance(200001);await rejected;
  assert.equal(f.trainer.getGameTrainerState(g.title,g.path).state,'failed');const stop=f.recovery.startGameTrainerRecovery();await flush();f.fx.seedIndex();await f.fx.advance(5000);assert(ready(f,g));assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.timers.size,0);stop();
 });
 await check('ordinary preparation success pushes Decky ready immediately without waiting for the recovery poll',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f);f.fx.seedIndex();f.fx.files.delete(f.fx.norm(f.fx.indexPath));f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
  f.fx.workerHook=async(_p,args)=>{const at=key=>args[args.indexOf(key)+1];f.fx.put(at('-ResultPath'),JSON.stringify(f.fx.requestReceipt(args,{...receipt(),ok:true})));return{ok:true};};
  await f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});await flush();assert.equal(h.latest().trainer.available,true);assert.equal(f.fx.timers.size,0);assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);h.stop();stop();
 });
 await check('active preparation owns the only request poller while recovery waits for its push',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;const stop=f.recovery.startGameTrainerRecovery();await flush();const reads=f.fx.reads.filter(r=>r.p===f.fx.indexPath).length;
  f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async()=>({ok:true});
  const p=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g}),rejected=assert.rejects(p,/取消/);await flush();
  assert.equal(f.fx.timers.size,1);assert.equal([...f.fx.timers.values()][0].at-f.fx.now,400);await f.fx.advance(5000);
  assert.equal(f.fx.reads.filter(r=>r.p===f.fx.indexPath).length,reads);assert.equal(f.fx.timers.size,1);
  await f.trainer.cancelGameTrainerPreparation(g.title,g.path);await f.fx.advance(400);await rejected;stop();assert.equal(f.fx.timers.size,0);assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);
 });
 await check('automatic download off still restores local readiness and does not start a catalog or download',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.seedIndex();const stop=f.auto.startGameTrainerAutoPrepare();await flush();await f.fx.advance(30000);assert(ready(f));assert.equal(f.fx.timers.size,0);noSideEffects(f);stop();
 });
 await check('Decky close and reentry reconcile from the owner without a second active poller',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f);await h.pause();assert.equal(f.fx.listeners.size,1);assert.equal(f.fx.timers.size,1);
  f.fx.seedIndex();await f.fx.advance(5000);assert(ready(f));assert.equal(f.fx.timers.size,0);const reads=f.fx.reads.length;await h.open();assert.equal(h.latest().trainer.available,true);assert.equal(f.fx.reads.length,reads);assert.equal(f.fx.timers.size,0);noSideEffects(f);h.stop();stop();
 });
 await check('wake triggers a single owner check and resolves a late local result',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery();await flush();f.fx.seedIndex();f.fx.emit('ipc:power.resumed');await flush();assert(ready(f));assert.equal(f.fx.timers.size,0);noSideEffects(f);stop();
 });
 await check('owner shutdown removes subscriptions and timers and forbids a late hydration',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.seedIndex();let release;f.fx.hashHook=()=>new Promise(resolve=>release=resolve);const stop=f.recovery.startGameTrainerRecovery();await flush();stop();stop();release(receipt().sha256);await flush();assert.equal(ready(f),null);assert.equal(f.fx.listeners.size,0);assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));noSideEffects(f);
 });
 await check('duplicate resident starts share one timer and release it only when the last lease stops',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();const a=f.recovery.startGameTrainerRecovery(),b=f.recovery.startGameTrainerRecovery();await flush();assert.equal(f.fx.listeners.size,1);assert.equal(f.fx.timers.size,1);a();assert.equal(f.fx.timers.size,1);b();assert.equal(f.fx.timers.size,0);assert.equal(f.fx.listeners.size,0);noSideEffects(f);
 });
 await check('unresolved local checks back off to one per minute and a confirmed terminal answer stops polling',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery();await flush();
  const intervals=[];for(let i=0;i<6;i++){const timer=[...f.fx.timers.values()][0];intervals.push(timer.at-f.fx.now);await f.fx.advance(timer.at-f.fx.now);}
  assert.deepEqual(intervals,[5000,10000,20000,40000,60000,60000]);noSideEffects(f);stop();
  const b=await createTrainerRecoveryFixture(),g=game();b.fx.game=g;b.fx.put(b.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
  b.fx.workerHook=async(_p,args)=>{const at=key=>args[args.indexOf(key)+1];b.fx.put(at('-ResultPath'),JSON.stringify(b.fx.requestReceipt(args,{gameName:g.title,state:'unavailable',ok:false})));return{ok:true};};
  const end=b.recovery.startGameTrainerRecovery();await flush();await b.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});await flush();assert.equal(b.fx.timers.size,0);end();
 });
 await check('one corrupt matching alias cannot hide the other valid receipt',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game(),r=receipt(),k=f.trainer.trainerGameKey(g.title);f.fx.seedIndex();
  f.fx.put(f.fx.indexPath,JSON.stringify({[k]:{...r,identity:{...r.identity,gameName:42}},[k+'|valid']:r}));
  assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));noSideEffects(f);
 });
 await check('a hydrated local trainer prevents automatic download work when automatic preparation is enabled',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.autoEnabled=true;f.fx.seedIndex();const stop=f.auto.startGameTrainerAutoPrepare();await flush();await f.fx.advance(2500);assert(ready(f));noSideEffects(f);stop();assert.equal(f.fx.timers.size,0);
  // Consume synthetic native wire observations through the real decoder and
  // policy facade. This proves consumer admission, not native process election.
  const raw=(source='memory',target=game())=>({...target,valid:true,observationState:'complete',generation:1,identityGeneration:1,source,...(source==='whitelist'?{whitelistRule:'fixture-only'}:{})});
  for(const variant of ['memory','whitelist','custom','pending','unavailable','empty','strict-pid-miss','off','hydrated']){
   const wire=await createTrainerRecoveryFixture({realGameDetection:true});let stopWire=null;
   try{
    const target=variant==='hydrated'?game():{...game(),name:'git',title:'git',path:'C:\\Program Files\\Git\\mingw64\\bin\\git.exe'};
    const source=['memory','whitelist','custom'].includes(variant)?variant:'memory';
    const initial=await wire.observeRaw(raw(source,target));assert.equal(initial.error,null);assert.equal(initial.current,true);assert.equal(wire.detector.getCurrentGame().source,source);
    wire.fx.autoEnabled=variant!=='off';if(variant==='hydrated')wire.fx.seedIndex();
    wire.fx.put(wire.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert fixture worker');
    wire.fx.workerHook=async(_program,args)=>{const document=wire.fx.requestReceipt(args,{ok:false,state:'unavailable',code:'identity-unconfirmed',gameName:target.title,retryable:false,message:'Synthetic wire consumer fixture; no trainer launch'});const at=args.indexOf('-ResultPath');wire.fx.put(args[at+1],JSON.stringify(document));return {ok:true};};
    stopWire=wire.auto.startGameTrainerAutoPrepare();await flush();
    if(['pending','unavailable','empty','strict-pid-miss'].includes(variant)){
     const strict=variant==='strict-pid-miss',state=variant==='empty'?'complete':strict?'unavailable':variant;
     const observation={valid:false,pid:0,observationState:state,generation:2,identityGeneration:['empty','strict-pid-miss'].includes(variant)?2:1,...(strict?{requestedPidRejected:target.pid+1}:{})};
     const read=await wire.observeRaw(observation,strict?target.pid+1:0);
     if(variant==='empty'){assert.equal(wire.detector.getCurrentGame(),null);assert.equal(read.current,true);}
     else{assert.equal(wire.detector.getCurrentGame().path,target.path);assert.equal(read.current,false);if(strict){assert.equal(read.selected,null);assert.equal(read.error,null);}else assert.match(read.error,new RegExp(state));}
    }
    await wire.fx.advance(2500);await flush();
    const expected=['memory','whitelist','custom'].includes(variant)?1:0;assert.equal(wire.fx.workers.length,expected,variant+' Prepare count');assert.equal(wire.fx.launches.length,0,variant+' executable launches');
    if(expected){const args=wire.fx.workers[0];const value=key=>args[args.indexOf(key)+1];assert.equal(value('-GameName'),'git');assert.equal(value('-GameExecutablePath'),target.path);assert.equal(value('-GameProcessId'),String(target.pid));assert.equal(value('-GameProcessCreated'),target.processCreated);}
    if(['pending','unavailable'].includes(variant)){
     const recovered=await wire.observeRaw({...raw(source,target),generation:3,identityGeneration:3});assert.equal(recovered.error,null);assert.equal(recovered.current,true);
     await wire.fx.advance(2500);assert.equal(wire.fx.workers.length,1,variant+' must resume preparation after a fresh complete observation');
     await wire.observeRaw({...raw(source,target),generation:3,identityGeneration:3});await wire.fx.advance(2500);assert.equal(wire.fx.workers.length,1,variant+' duplicate readiness cannot start another preparation');
    }
    if(variant==='hydrated')assert(wire.trainer.getReadyGameTrainer(target.title,target.path,target));
    stopWire();stopWire=null;await flush();const workersAtStop=wire.fx.workers.length;
    const sharedDetectorSchedules=wire.fx.scheduled.size;assert.equal(sharedDetectorSchedules,1,'The real policy facade has its process-lifetime detector subscription');
    wire.fx.dispose();await wire.fx.advance(5000);assert.equal(wire.fx.workers.length,workersAtStop);assert.equal(wire.fx.timers.size,0);assert.equal(wire.fx.scheduled.size,0);assert.equal(wire.fx.events.size,0);
    wireVariants.push({variant,sampleOrigin:'SYNTHETIC_WIRE_INPUT',source,prepareRequestsBeforeRecovery:expected,prepareRequestsAtStop:workersAtStop,recoveredFromUnknown:['pending','unavailable'].includes(variant),trainerLaunches:0,sharedDetectorSchedulesBeforeFixtureDispose:sharedDetectorSchedules,fixturePortsDisposed:true,nativeRuleElectionExecuted:false});
   }finally{if(stopWire)stopWire();wire.fx.dispose();}
  }
 });
 async function lateAssertion(overrides={},hostSource=null){const f=await createTrainerRecoveryFixture(overrides);f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f,hostSource);f.fx.seedIndex();await f.fx.advance(5000);try{assert.equal(h.latest().trainer.available,true,'late ready must push the Decky button');}finally{h.stop();stop();}}
 await check('negative old memory-only behavior reproduces unavailable button after persisted ready',async()=>{
  const owner=fs.readFileSync(path.join(root,'src/bridge/gameTrainer.ts'),'utf8'),anchor='  const generation = syncTrainerSource(), title = cleanGameTitle(gameName).trim();';assert(owner.includes(anchor));
  await assert.rejects(lateAssertion({ownerSource:owner.replace(anchor,'  return Promise.resolve(null);\n'+anchor)}),/late ready must push/);
 });
 await check('negative removed unresolved-result fallback reproduces late ready loss',async()=>{
  const recovery=fs.readFileSync(path.join(root,'src/bridge/gameTrainerRecovery.ts'),'utf8'),anchor='timer = setTimeout(recover, delay);';assert(recovery.includes(anchor));
  await assert.rejects(lateAssertion({recoverySource:recovery.replace(anchor,'timer = null;')}),/late ready must push/);
 });
 await check('negative removed owner push leaves an open Decky sidebar stale',async()=>{
  const host=fs.readFileSync(path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8'),anchor='stops.push(subscribeGameTrainerState(() => {';assert(host.includes(anchor));
  await assert.rejects(lateAssertion({},host.replace(anchor,anchor+' return;')),/late ready must push/);
 });
 if(!cases.length)throw Error('No recovery cases match the requested filter');
 return finish({simulatedSuccessiveGames:caseFilter===null?5:0,fieldGame:field.game.title,physicalFseAcceptance:false});
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const idx=process.argv.indexOf('--out'),out=idx>=0?path.resolve(process.argv[idx+1]):path.join(root,'Build/Validation/TrainerRecovery');
 const selected=process.argv.indexOf('--case-filter');
 const r=await trainerRecoveryTests(out,{caseFilter:selected<0?null:process.argv[selected+1]??''});console.log(JSON.stringify(r));if(!r.passed)process.exitCode=1;
}