import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {createTrainerRecoveryFixture,field,flush} from './fixtures/game_trainer_recovery_fixture.mjs';
const root=path.resolve(import.meta.dirname,'..'),clone=structuredClone;
const game=()=>clone(field.game),receipt=()=>clone(Object.values(field.index)[0]);
const options=g=>({processTarget:g,isTargetCurrent:()=>true});
const ready=(f,g=f.fx.game)=>f.trainer.getReadyGameTrainer(g.title,g.path,g);
const advanceExistingPublicRetry=async f=>{
 await flush();assert(f.fx.sharedTimers.size<=1,'one public retry owner');
 if(f.fx.sharedTimers.size){const next=[...f.fx.sharedTimers.values()][0].at;assert(next>=f.fx.now,'existing retry deadline');await f.fx.advance(next-f.fx.now);}
 await flush();
};
const noSideEffects=f=>{assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.writes.length,0);};
const setReceipt=(f,g,r)=>f.fx.seedIndex({[f.trainer.trainerGameKey(g.title)]:r});
const runId='b'.repeat(32),peer={connection:'1',runId,connected:true};
// Read admission does not execute mirror/store helpers or create a fixture.
export function prepareTrainerRecoveryAdmission(selected){
 const snapshots=new Map(),require=createRequire(import.meta.url);
 const read=relative=>{const file=path.resolve(root,relative);if(!snapshots.has(file))snapshots.set(file,fs.readFileSync(file));return snapshots.get(file).toString('utf8');};
 const esbuildEntry=require.resolve('esbuild');
 const esbuildBinary=process.env.ESBUILD_BINARY_PATH||createRequire(esbuildEntry).resolve('@esbuild/win32-x64/esbuild.exe');
 for(const relative of ['src/bridge/gameTrainer.ts','src/bridge/trainerLifecycle.ts','src/bridge/powerLifecycleState.ts','src/bridge/gameTrainerRecovery.ts','src/bridge/gameTrainerAuto.ts','tools/decky_trainer_recovery_selftest.mjs','tools/fixtures/game_trainer_recovery_fixture.mjs','tools/fixtures/trainer_ready_tmnt_20261022.json','tsconfig.json','package.json',esbuildEntry,esbuildBinary])read(relative);
 const prepared=new Map();for(const item of selected)if(item.prepare)prepared.set(item,item.prepare(read));
 if(selected.some(item=>item.realGameDetection))for(const relative of ['src/bridge/gamedetect.ts','src/bridge/gamePolicyTarget.ts','src/bridge/gamePolicyHysteresis.ts','PowerControl/GameTitlePolicy.json'])read(relative);
 if(selected.some(item=>item.mirror)){
  const helper='tools/fixtures/decky_mirror_host_fixture.mjs',store='tools/fixtures/decky_game_memory_store.mjs';
  const mirrorSource=read(helper),storeSource=read(store),compiler=require.resolve('typescript');
  read(require.resolve('typescript/package.json'));read(compiler);
  const ts=require(compiler);
  // Parse actual read/extraction expressions. Do not import/execute the helpers,
  // bundle their code, or inspect unrelated exported fixture entry points.
  const inspect=(relative,source,entry)=>{
   const tree=ts.createSourceFile(relative,source,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
   const visit=node=>{
    if(ts.isCallExpression(node)){
     const args=node.arguments;
     if(ts.isPropertyAccessExpression(node.expression)&&node.expression.expression.getText(tree)==='path'&&node.expression.name.text==='join'&&args.length===2&&args[0].getText(tree)==='root'&&ts.isStringLiteral(args[1])){
      const file=args[1].text;if(file!=='src/bridge/deckyMirrorHost.ts'||!process.env.YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE)read(file);
     }
     if(ts.isIdentifier(node.expression)&&node.expression.text==='functions'&&args.length===2&&ts.isStringLiteral(args[0])&&ts.isArrayLiteralExpression(args[1])){
      const names=args[1].elements.map(value=>{assert(ts.isStringLiteral(value),'Expected literal extracted function names');return value.text;});
      const original=read(args[0].text),parsed=ts.createSourceFile(args[0].text,original,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
      assert.equal(parsed.statements.filter(value=>ts.isFunctionDeclaration(value)&&names.includes(value.name?.text)).length,names.length,'Missing original extracted function: '+args[0].text);
     }
    }
    ts.forEachChild(node,visit);
   };
   for(const statement of tree.statements){if(ts.isFunctionDeclaration(statement)&&statement.name?.text!==entry)continue;visit(statement);}
  };
  inspect(helper,mirrorSource,'createMirrorHostFixture');inspect(store,storeSource,'fixture');
  if(process.env.YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE)read(process.env.YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE);
 }
 const before=[...snapshots].map(([file,bytes])=>({path:path.relative(root,file).startsWith('..')?file:path.relative(root,file).replaceAll('\\','/'),sha256:createHash('sha256').update(bytes).digest('hex')}));
 const runtime={path:process.execPath,version:process.version,sha256:createHash('sha256').update(fs.readFileSync(process.execPath)).digest('hex')};
 return {inputs:before.map(value=>value.path),before,runtime,prepared};
}
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
 const cases=[],skipped=[],selected=[],started=performance.now(),wireVariants=[];
 let inputs=[],before=[],runtime;
 const pin=p=>({path:p,sha256:createHash('sha256').update(fs.readFileSync(path.resolve(root,p))).digest('hex')});
 const finish=extra=>{
  const after=inputs.map(pin),drift=after.filter((v,i)=>v.sha256!==before[i].sha256);
  const report={wireVariants,actualNativeRuleElectionExecuted:false,completeRegression:caseFilter===null,passed:cases.every(c=>c.passed)&&cases.length>0&&drift.length===0,cases:cases.length,checks:cases,caseFilter,skipped,elapsedMs:performance.now()-started,inputs:before,sourceDrift:drift,runtime,realProcessLaunches:0,realGameMemoryReads:0,userSettingsWrites:0,realSteamActions:0,...extra};
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));return report;
 };
 const check=async(name,fn,requirements={})=>{
  if(caseFilter!==null&&!name.includes(caseFilter)){skipped.push(name);return;}
  selected.push({name,fn,...requirements});
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
   const ownedEvent=async()=>{
    if(event==='ipc:power.resumed'){
     // A duplicate hint is not a new native wake. Exercise the actual shared
     // source through a changed generation and its authoritative Ready read.
     f.fx.power={...f.fx.power,generation:f.fx.power.generation+1,phase:'suspending'};f.fx.emit('ipc:power.suspending',f.fx.power.generation);await flush();
     f.fx.power.phase='ready';f.fx.emit(event,f.fx.power.generation);await flush();await f.fx.advance(1000);
    }else{f.fx.emit(event);await flush();}
   };
   try{await flush();await ownedEvent();assert.equal(indexReads(f),2,event);assert.equal(f.fx.timers.size,0);f.fx.readHook=null;f.fx.seedIndex();if(event==='ui-settings:changed')f.fx.generation++;await ownedEvent();assert(ready(f),event);assert.equal(indexReads(f),3);assert.equal(f.fx.hashes.length,2);noSideEffects(f);}finally{stop();}
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
 await check('cache-fault: semantic negative removing fault gates reproduces repeated reads',async(preparedSource)=>{

  await assert.rejects(expectStormQuiet({recoverySource:preparedSource}),/cache read fault must stop timer polling|must not retry/);
 },{prepare:read=>{const source=read('src/bridge/gameTrainerRecovery.ts'),anchor='if (cacheFaulted) return;';assert(source.includes(anchor));return source.replaceAll(anchor,'');}});
 await check('cache-fault: semantic negative treating every null as fault loses healthy late ready',async(preparedSource)=>{

  await assert.rejects(expectHealthyAbsence({recoverySource:preparedSource}),/healthy absence must retain late-result recovery/);
 },{prepare:read=>{const source=read('src/bridge/gameTrainerRecovery.ts'),anchor='checking = null;\n          if (cacheFaulted) return;';assert(source.includes(anchor));return source.replace(anchor,'checking = null;\n          cacheFaulted = true;\n          if (cacheFaulted) return;');}});
 await check('field TMNT persisted ready receipt restores a cold owner and deduplicates aliases without writes',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.seedIndex();f.fx.game=g;
  const value=await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g));assert(value);assert.equal(value.path,receipt().trainerPath);assert.equal(value.gcmResourceId,receipt().gcmResourceId);assert.equal(f.fx.hashes.length,2);assert.equal(f.fx.reads.length,1);noSideEffects(f);
 });
 await check('late local result pushes Decky available without reopening or another snapshot request',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f);
  assert.equal(h.latest().trainer.available,false);assert.equal(f.fx.listeners.size,1);assert.equal(f.fx.timers.size,1);
  f.fx.seedIndex();await f.fx.advance(5000);assert.equal(h.latest().trainer.available,true);assert(ready(f));assert.equal(f.fx.timers.size,0);noSideEffects(f);h.stop();stop();
 },{mirror:true});
 await check('confirmed ready state is static and repeated Decky checks perform no disk IO or timer work',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.seedIndex();const stop=f.recovery.startGameTrainerRecovery();await flush();const h=await hostFor(f),reads=f.fx.reads.length,hashes=f.fx.hashes.length;
  for(let i=0;i<10;i++){f.recovery.refreshGameTrainerRecovery();await f.fx.advance(5000);}
  assert.equal(f.fx.timers.size,0);assert.equal(f.fx.reads.length,reads);assert.equal(f.fx.hashes.length,hashes);assert.equal(f.fx.listeners.size,1);assert.equal(h.latest().trainer.available,true);noSideEffects(f);h.stop();stop();
 },{mirror:true});
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
 },{mirror:true});
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
 await check('lifecycle: recovery rejects a late hash during sleep and refreshes on resume',async()=>{
  const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();f.fx.seedIndex();let release;f.fx.hashHook=()=>new Promise(resolve=>release=resolve);
  const stop=f.recovery.startGameTrainerRecovery();try{
   await flush();assert(release);f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);f.fx.hashHook=null;release(receipt().sha256);await flush();
   assert.equal(ready(f),null,'a pre-sleep hash cannot publish ready');assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,1,'the same public observer retains its one original retry');const reads=indexReads(f);f.fx.select(game());f.recovery.refreshGameTrainerRecovery();await f.fx.advance(60000);assert.equal(indexReads(f),reads,'suspend events must keep recovery passive');
   f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',2);await advanceExistingPublicRetry(f);assert(ready(f));assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,0);noSideEffects(f);
  }finally{stop();assert([...f.fx.events.values()].every(s=>s.size===0));f.fx.dispose();}
 });
 await check('lifecycle: WebView recovery cancels cached readiness despite a newer power admission',async()=>{
  for(const webviewGeneration of [1,1000]){
   const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();f.fx.seedIndex();f.fx.power.generation=100;let release;const publicReplies=[];f.fx.hashHook=()=>new Promise(resolve=>release=resolve);const stop=f.recovery.startGameTrainerRecovery();try{
    await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch(),100);await flush();assert(release);const reads=indexReads(f);
    f.fx.powerReadHook=()=>new Promise(resolve=>publicReplies.push(resolve));
    f.fx.emit('ipc:webview.recovering',webviewGeneration);await flush();assert(publicReplies.length>0,'recovery must request a fresh public read');
    f.fx.hashHook=null;release(receipt().sha256);await flush();assert.equal(ready(f),null,'WebView loss cancels its pending hash independently of the power generation');assert.equal(f.fx.timers.size,0);
    f.fx.powerReadHook=async()=>null;for(const reply of publicReplies.splice(0))reply(null);await flush();
    f.fx.select(game());f.recovery.refreshGameTrainerRecovery();await f.fx.advance(90000);assert.equal(ready(f),null);assert.equal(indexReads(f),reads,'unconfirmed public reads cannot restart a cache check');assert.equal(f.fx.timers.size,0);noSideEffects(f);
    f.fx.powerReadHook=null;await advanceExistingPublicRetry(f);assert(ready(f),'fresh public admission must resume the original owner');assert(indexReads(f)>reads);assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,0);noSideEffects(f);
    await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch(),100);
   }finally{stop();f.fx.powerReadHook=null;for(const reply of publicReplies.splice(0))reply({...f.fx.power});await flush();assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));f.fx.dispose();}
  }
 });
 await check('lifecycle: direct trainer read cannot bypass the current shared power hold',async()=>{
  for(const reason of ['pending','unknown']){
   const f=await createTrainerRecoveryFixture({separatePowerClock:true});
   const ready={phase:'ready',generation:7,known:true,hardwareWritesAllowed:true,notificationPending:false,observedNotificationSequence:12,suspendSerial:3,publicationRevision:1};
   f.fx.power={...ready};let projected={...ready};f.fx.powerReadHook=async()=>projected;
   const stop=f.lifecycle.subscribeTrainerLifecycle(()=>{});
   const publish=async state=>{projected=state;for(const listener of [...f.fx.events.get('ipc:power.lifecycleChanged')])listener({detail:state});await flush();};
   try{
    await flush();
    await publish({...ready,publicationRevision:2,known:reason!=='unknown',notificationPending:reason==='pending',hardwareWritesAllowed:false});
    assert(!f.sharedPower.isSharedPowerLifecycleAdmitted(f.sharedPower.getSharedPowerLifecycle()),'public observer retains the hold');
    await assert.rejects(f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch()),/电源|取消/,reason+': same-scope direct Ready has no admission');
    const successor={...ready,publicationRevision:3};f.fx.power=successor;await publish(successor);
    assert.equal(await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch()),7,'direct check continues after the actual public Ready');
    f.fx.power={...successor,generation:8};
    await assert.rejects(f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch()),/电源|取消/,'direct read cannot invent a new public generation');
    noSideEffects(f);
   }finally{stop();await flush();assert.equal(f.fx.sharedTimers.size,0);f.fx.dispose();}
  }
 });
 await check('lifecycle: a superseded power read releases queued preparation',async()=>{
  const f=await createTrainerRecoveryFixture({separatePowerClock:true}),g=game();
  const initial={...f.fx.power,known:true,notificationPending:false,generation:7,suspendSerial:3,observedNotificationSequence:10,publicationRevision:20};
  f.fx.power=initial;
  const publish=async state=>{f.fx.power=state;for(const listener of [...f.fx.events.get('ipc:power.lifecycleChanged')])listener({detail:state});await flush();};
  const stop=f.lifecycle.subscribeTrainerLifecycle(()=>{});
  let release,old,next;
  try{
   await publish(initial);
   assert(f.sharedPower.isSharedPowerLifecycleAdmitted(f.sharedPower.getSharedPowerLifecycle()));
   f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
   f.fx.workerHook=async(_program,args)=>{
    const output=args[args.indexOf('-ResultPath')+1];
    f.fx.put(output,JSON.stringify(f.fx.requestReceipt(args,{ok:false,gameName:g.title,state:'unavailable'})));
    return {ok:true};
   };
   let held=true,retired=false,successor=false;
   f.fx.powerHook=()=>{if(held){held=false;return new Promise(resolve=>release=resolve);}};
   old=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});
   void old.then(()=>{},error=>{retired=error.name==='GameTrainerCancelledError';});
   await flush();assert.equal(typeof release,'function');noSideEffects(f);
   await publish({...initial,publicationRevision:21});
   assert.equal(f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g}),old,'metadata alone must not retire a live request');
   // A coalesced Ready can replace the scope without changing generation.
   await publish({...initial,publicationRevision:22,suspendSerial:4,observedNotificationSequence:11});
   next=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});
   void next.then(value=>{successor=value.state==='unavailable';},()=>{});
   await flush();
   const beforeOldReply={retired,successor,workers:f.fx.workers.length};
   release();await Promise.allSettled([old,next]);
   assert.deepEqual(beforeOldReply,{retired:true,successor:true,workers:1},'a read from a retired scope must not hold the preparation queue');
   assert.equal(f.fx.workers.length,1,'late read cannot submit a second worker');
  }finally{release?.();await Promise.allSettled([old,next]);stop();f.fx.dispose();}
 });
 await check('lifecycle: recovery timer retires on power or page loss without an automatic replay',async()=>{
  for(const event of ['ipc:power.pending','ipc:power.resuming','ipc:webview.recovering','pagehide']){
   const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();const publicReplies=[],stop=f.recovery.startGameTrainerRecovery();try{
    await flush();assert.equal(f.fx.timers.size,1);const reads=indexReads(f);
    if(event==='ipc:webview.recovering')f.fx.powerReadHook=()=>new Promise(resolve=>publicReplies.push(resolve));
    if(event==='ipc:power.pending'){
     // Pending is the adapter's cancellation label;actual Native admission is
     // published through the same public lifecycleChanged subscription.
     f.fx.power={...f.fx.power,generation:2,known:true,publicationRevision:1,hardwareWritesAllowed:false,notificationPending:true};
     for(const cb of [...f.fx.events.get('ipc:power.lifecycleChanged')||[]])cb({type:'ipc:power.lifecycleChanged',detail:{...f.fx.power}});
    }else f.fx.emit(event,2);
    assert.equal(f.fx.timers.size,0,event);await flush();
    if(event==='ipc:webview.recovering'){assert(publicReplies.length>0);f.fx.powerReadHook=async()=>null;for(const reply of publicReplies.splice(0))reply(null);await flush();}
    f.fx.seedIndex();f.fx.select(game());f.recovery.refreshGameTrainerRecovery();await f.fx.advance(90000);assert.equal(indexReads(f),reads,event);assert.equal(ready(f),null);assert.equal(f.fx.timers.size,0);noSideEffects(f);
    if(event==='ipc:webview.recovering'){f.fx.powerReadHook=null;await advanceExistingPublicRetry(f);assert(ready(f),'only fresh public confirmation can resume a new consistency check');assert(indexReads(f)>reads);assert.equal(f.fx.timers.size,0);noSideEffects(f);}
   }finally{stop();f.fx.powerReadHook=null;for(const reply of publicReplies.splice(0))reply({...f.fx.power});await flush();assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));f.fx.dispose();}
  }
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
 },{mirror:true});
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
 },{mirror:true});
 await check('wake triggers a single owner check and resolves a late local result',async()=>{
  const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();
  const initial={known:true,phase:'ready',generation:1,publicationRevision:1,observedNotificationSequence:1,hardwareWritesAllowed:true,notificationPending:false};
  f.fx.power=initial;const stop=f.recovery.startGameTrainerRecovery();
  const observe=async state=>{f.fx.power=state;for(const listener of [...f.fx.events.get('ipc:power.lifecycleChanged')])listener({type:'ipc:power.lifecycleChanged',detail:state});await flush();};
  try{
   await flush();assert.equal(f.fx.timers.size,1);
   await observe({...initial,phase:'suspending',generation:2,publicationRevision:2,observedNotificationSequence:2,hardwareWritesAllowed:false});
   f.fx.seedIndex();f.fx.emit('ipc:power.resumed');await flush();assert.equal(ready(f),null,'untagged resume cannot admit recovery');
   const current={...initial,generation:2,publicationRevision:3,observedNotificationSequence:3};
   await observe(current);assert(ready(f));await observe(current);
   assert.equal(f.fx.reads.filter(r=>r.p===f.fx.indexPath).length,1,'duplicate current Ready cannot repeat the cache check');
   assert.equal(f.fx.timers.size,0);noSideEffects(f);
  }finally{stop();assert.equal(f.fx.sharedTimers.size,0);f.fx.dispose();}
 });
 await check('wake replaces a retired same-instance cache read before the old reply',async()=>{
  for(const mode of ['late-success','late-fault','suspend-again','target-cleared']){
   const f=await createTrainerRecoveryFixture({separatePowerClock:true}),g=game();
   const initial={known:true,phase:'ready',generation:1,publicationRevision:1,observedNotificationSequence:1,suspendSerial:1,hardwareWritesAllowed:true,notificationPending:false};
   f.fx.game=g;f.fx.power=initial;f.fx.seedIndex();
   const reads=[];
   f.fx.readHook=p=>p!==f.fx.indexPath||(mode==='late-success'&&reads.length>0)?undefined:new Promise((resolve,reject)=>reads.push({resolve,reject}));
   const observe=async state=>{f.fx.power=state;for(const listener of [...f.fx.events.get('ipc:power.lifecycleChanged')])listener({type:'ipc:power.lifecycleChanged',detail:state});await flush();};
   const stop=f.recovery.startGameTrainerRecovery();
   try{
    await flush();assert.equal(indexReads(f),1);assert.equal(reads.length,1);
    const suspended={...initial,phase:'suspending',publicationRevision:2,observedNotificationSequence:2,suspendSerial:2,hardwareWritesAllowed:false};
    await observe(suspended);assert.equal(ready(f),null);assert.equal(indexReads(f),1,'a hold cannot start a new cache read');
    const current={...initial,publicationRevision:3,observedNotificationSequence:3,suspendSerial:2};
    await observe(current);
    assert.equal(indexReads(f),2,mode+': current Ready joined the retired same-instance read');
    if(mode==='late-success'){
     const restored=ready(f);assert(restored,'the new read must finish without the old reply or a retry tick');
     reads[0].resolve();await flush();assert.deepEqual(ready(f),restored);assert.equal(f.fx.hashes.length,2);
    }else{
     reads[0].reject(Error('late retired cache read failure'));await flush();assert.equal(ready(f),null);
     if(mode==='late-fault'){
      const faults=[],joined=f.trainer.restoreReadyGameTrainer(g.title,g.path,{...options(g),onCacheFault:fault=>faults.push(fault)});
      await flush();assert.equal(indexReads(f),2,'retirement must not delete the replacement read or duplicate a healthy read');
      reads[1].resolve();assert(await joined);await flush();assert(ready(f));assert.deepEqual(faults,[]);assert.equal(f.fx.hashes.length,2);
     }else{
      if(mode==='suspend-again')await observe({...suspended,publicationRevision:4,observedNotificationSequence:4,suspendSerial:3});
      else f.fx.select(null);
      reads[1].resolve();await flush();assert.equal(ready(f,g),null,'new suspension or target loss must revoke the replacement');assert.equal(f.fx.hashes.length,0);
     }
    }
    assert.equal(indexReads(f),2);assert.equal(f.fx.timers.size,0);noSideEffects(f);
   }finally{stop();for(const read of reads)read.resolve();await flush();assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,0);f.fx.dispose();}
  }
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
   // The main renderer keeps shared power subscribed across target reads.
   const stopPower=wire.sharedPower.onSharedPowerLifecycle(()=>{});
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
   }finally{if(stopWire)stopWire();stopPower();wire.fx.dispose();}
  }
 },{realGameDetection:true});
 async function lateAssertion(overrides={},hostSource=null){const f=await createTrainerRecoveryFixture(overrides);f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f,hostSource);f.fx.seedIndex();await f.fx.advance(5000);try{assert.equal(h.latest().trainer.available,true,'late ready must push the Decky button');}finally{h.stop();stop();}}
 await check('negative old memory-only behavior reproduces unavailable button after persisted ready',async(preparedSource)=>{

  await assert.rejects(lateAssertion({ownerSource:preparedSource}),/late ready must push/);
 },{mirror:true,prepare:read=>{const owner=read('src/bridge/gameTrainer.ts'),anchor='  const generation = syncTrainerSource(), title = cleanGameTitle(gameName).trim();';assert(owner.includes(anchor));return owner.replace(anchor,'  return Promise.resolve(null);\n'+anchor);}});
 await check('negative removed unresolved-result fallback reproduces late ready loss',async(preparedSource)=>{

  await assert.rejects(lateAssertion({recoverySource:preparedSource}),/late ready must push/);
 },{mirror:true,prepare:read=>{const recovery=read('src/bridge/gameTrainerRecovery.ts'),anchor='timer = setTimeout(recover, delay);';assert(recovery.includes(anchor));return recovery.replace(anchor,'timer = null;');}});
 await check('negative removed owner push leaves an open Decky sidebar stale',async(preparedSource)=>{

  await assert.rejects(lateAssertion({},preparedSource),/late ready must push/);
 },{mirror:true,prepare:read=>{const host=read('src/bridge/deckyMirrorHost.ts'),anchor='stops.push(subscribeGameTrainerState(() => {';assert(host.includes(anchor));return host.replace(anchor,anchor+' return;');}});
 // The November field sample was a non-Steam shortcut with a verified store ID,
 // a plain ready alias, and a later cancelled EXE-keyed preparation. External
 // files, hashes and worker receipts remain explicit inert boundaries here.
 const launchFixture=async()=>{
  const f=await createTrainerRecoveryFixture(),g={...game(),title:"Dragon's Dogma 2",name:'DD2.exe',path:'D:\\GAME\\DD2\\DD2.exe',pid:11076,processCreated:'134386330588489025'};
  const r={...receipt(),storageVersion:2,gameName:g.title,trainerGameName:'Dragon’s Dogma 2',origin:'fling_main',gcmResourceId:'',trainerFolder:'C:\\GCM Trainers\\龙之信条2',trainerPath:'C:\\GCM Trainers\\龙之信条2\\Dragons Dogma 2 Trainer.exe',downloaded:false,downloadOutcome:'reused-local',
   identity:{method:'ymcc-game-target',steamAppId:'2054970',processId:g.pid,processCreated:g.processCreated,executablePath:g.path,executableSha256:'53d0fa7093ca1b560e0c881c84a6451091f87d3ab678d25f3efff08c22128127',gameName:g.title,windowTitle:g.title,snapshotPath:'C:\\YMCC\\PowerControl\\game-target.json',aliases:[g.title,'龙之信条2']}};
  delete r.ok;f.fx.game=g;f.fx.seedIndex({dragonsdogma2:r,['dragonsdogma2|'+'f'.repeat(64)]:{...r,ok:false,state:'cancelled',message:'已取消修改器准备'}});
  for(const name of ['YeManTrainerPrepare.ps1','YeManTrainerLaunch.ps1'])f.fx.put(f.fx.powerDir+'\\'+name,'inert worker');
  const at=(args,key)=>{assert(args.includes(key),'Missing actual launch argument '+key);return args[args.indexOf(key)+1];};
  const respond=(args,value)=>f.fx.put(at(args,'-ResultPath'),JSON.stringify(f.fx.requestReceipt(args,value)));
  const launch=(extra={})=>f.trainer.openOrSearchGameTrainer(g.title,undefined,g.path,{processTarget:g,isTargetCurrent:async()=>true,interactiveShell:true,...extra});
  return {f,g,r,at,respond,launch};
 };
 await check('field-1114: exact identity aliases restore an executable-stem receipt without a new preparation',async()=>{
  for(const variant of ['valid','wrong-alias','wrong-birth','changed-file']){
   const {f,g,r}=await launchFixture();
   try{
    r.gameName='dd2';r.identity.method='custom-library-appid';r.identity.librarySourcePath='D:\\YeMan\\CustomSteamLibrary\\data\\state\\steam-add-plan.json';
    r.identity.aliases=variant==='wrong-alias'?['Other Game']:[g.title,'龙之信条2'];
    if(variant==='wrong-birth')r.identity.processCreated='1';
    f.fx.seedIndex({dragonsdogma2:r,['dragonsdogma2|'+'f'.repeat(64)]:{gameName:g.title,ok:false,state:'cancelled',message:'已取消修改器准备'}});
    if(variant==='changed-file')f.fx.hashValues.set(f.fx.norm(r.trainerPath),'0'.repeat(64));
    const restored=await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g));
    assert.equal(!!restored,variant==='valid','same-instance executable-stem receipt '+variant);
    if(restored){assert.equal(ready(f).path,r.trainerPath);assert.equal(f.trainer.getGameTrainerState(g.title,g.path).gameName,g.title);}
    noSideEffects(f);assert.equal(f.fx.timers.size,0);
   }finally{f.fx.dispose();}
  }
 });
 await check('field-1114: a sleep-crossed preparation deadline cancels before reporting a timeout',async()=>{
  for(const boundary of ['new-generation','same-generation','read-failure']){
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;
  try{
   f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async()=>({ok:true});
   const pending=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});
   const rejected=assert.rejects(pending,error=>boundary==='new-generation'?error.name==='GameTrainerCancelledError':error.message.includes(boundary==='same-generation'?'后台准备修改器超时':'authoritative power read failed'));await flush();assert.equal(f.fx.workers.length,1);
   if(boundary==='new-generation')f.fx.power={...f.fx.power,generation:f.fx.power.generation+1};
   if(boundary==='read-failure')f.fx.powerHook=async()=>{throw Error('authoritative power read failed');};
   await f.fx.advance(240000);await rejected;
   assert.equal(f.trainer.getGameTrainerState(g.title,g.path).state,boundary==='new-generation'?'cancelled':'failed');assert.equal(f.fx.errors.length,boundary==='new-generation'?0:1);
   const args=f.fx.workers[0],resultPath=args[args.indexOf('-ResultPath')+1];assert.equal(f.fx.files.get(f.fx.norm(resultPath+'.cancel')),'cancel');
   assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));assert.equal(f.fx.launches.length,0);
  }finally{f.fx.dispose();}
  }
 });
 await check('field-1114: interrupted automatic preparation releases only its provisional retry reservation',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;f.fx.autoEnabled=true;
  f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async()=>({ok:true});
  const stop=f.auto.startGameTrainerAutoPrepare();
  try{
   await flush();await f.fx.advance(2500);assert.equal(f.fx.workers.length,1);
   f.fx.power={...f.fx.power,generation:f.fx.power.generation+1,phase:'suspending'};f.fx.emit('ipc:power.suspending',f.fx.power.generation);await flush();await f.fx.advance(400);
   f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',f.fx.power.generation);await flush();await f.fx.advance(2500);
   assert([...f.fx.timers.values()].every(t=>t.at-f.fx.now<=30000),'an interrupted in-flight reservation cannot become an hour-long cooldown');
   // The existing shared power source may confirm Ready in this first timer
   // batch. Give the original 2500ms preparation debounce its own next turn.
   await f.fx.advance(2500);
   assert.equal(f.fx.workers.length,2,'same-instance wake must not wait behind the abandoned one-hour placeholder');
   assert.equal(f.fx.launches.length,0,'wake prepares only; it never replays a launch');
  }finally{stop();await f.fx.advance(400);assert.equal(f.fx.timers.size,0);assert.equal(f.fx.listeners.size,0);assert.equal(f.fx.readinessListeners.size,0);f.fx.dispose();}
 });
 await check('field-1114: trainer preparation uses public Ready admission without requiring a connected controller',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;
  try{
   f.fx.power.inputReady=false;f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
   f.fx.workerHook=async(_program,args)=>{f.fx.seedIndex();f.fx.put(args[args.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(args,{...receipt(),ok:true,state:'ready'})));return{ok:true};};
   const prepared=await f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});assert.equal(prepared.state,'ready');assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.timers.size,0);
  }finally{f.fx.dispose();}
 });
 await check('field-1114: completed failure and download-budget cooldowns survive a wake unchanged',async()=>{
  for(const state of ['failed','budget-limited']){
   const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;f.fx.autoEnabled=true;
   f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
   f.fx.workerHook=async(_program,args)=>{f.fx.put(args[args.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(args,{ok:false,gameName:g.title,state,retryAfter:f.fx.now+3600000})));return{ok:true};};
   const stop=f.auto.startGameTrainerAutoPrepare();
   try{
    await flush();await f.fx.advance(2500);assert.equal(f.fx.workers.length,1);assert.equal(f.trainer.getGameTrainerState(g.title,g.path).state,state);
    f.fx.power={...f.fx.power,generation:f.fx.power.generation+1,phase:'suspending'};f.fx.emit('ipc:power.suspending',f.fx.power.generation);await flush();
    f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',f.fx.power.generation);await flush();await f.fx.advance(2500);await f.fx.advance(2500);
    assert.equal(f.fx.workers.length,1,state+' must keep its completed cooldown');assert.equal(f.fx.launches.length,0);
   }finally{stop();await flush();assert.equal(f.fx.timers.size,0);f.fx.dispose();}
  }
 });
 await check('field-1114: unknown or pending power blocks preparation and a late old-generation ready receipt',async()=>{
  for(const fault of ['unknown','pending','late-ready']){
   const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;
   try{
    f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
    if(fault==='unknown')f.fx.power.known=false;
    if(fault==='pending')f.fx.power.notificationPending=true;
    f.fx.workerHook=async(_program,args)=>{f.fx.power.generation++;f.fx.seedIndex();f.fx.put(args[args.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(args,{...receipt(),ok:true,state:'ready'})));return{ok:true};};
    await assert.rejects(f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g}),error=>error.name==='GameTrainerCancelledError');
    assert.equal(f.fx.workers.length,fault==='late-ready'?1:0);assert.equal(ready(f),null);assert.equal(f.fx.errors.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.timers.size,0);
   }finally{f.fx.dispose();}
  }
 });
 await check('launch-entry: top restores non-Steam DD2 ready alias and bypasses cancelled preparation',async()=>{
  const {f,g,r,at,respond,launch}=await launchFixture();
  try{
   f.fx.workerHook=async(_program,args)=>{assert.match(at(args,'-File'),/YeManTrainerLaunch\.ps1$/);assert.equal(at(args,'-Mode'),'Trainer');respond(args,{ok:true,pending:false,notice:'启动请求已发送，请确认修改器窗口'});return{ok:true};};
   const result=await launch();assert.equal(result.notice,'启动请求已发送，请确认修改器窗口');assert.equal(f.fx.workers.length,1);
   const args=f.fx.workers[0];assert(args.includes('-InteractiveShell'));assert.equal(at(args,'-TrainerPath'),r.trainerPath);assert.equal(at(args,'-GameExecutablePath'),g.path);assert.equal(at(args,'-GameProcessId'),String(g.pid));assert.equal(at(args,'-GameProcessCreated'),g.processCreated);assert.equal(at(args,'-ExpectedGameSha256'),r.identity.executableSha256);assert.equal(at(args,'-ExpectedSha256'),r.sha256);assert.equal(indexReads(f),1);assert(!args.includes('-CheatEnginePath'));assert.equal(f.fx.launches.length,0);
  }finally{f.fx.dispose();}
 });
 await check('launch-entry: top and Decky share one interactive launch and the same receipt in either order',async()=>{
  for(const first of ['top','decky']){
   const {f,g,at,respond,launch}=await launchFixture();let h,finish;
   try{
    assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));h=await hostFor(f);assert.equal(h.latest().trainer.available,true);
    f.fx.workerHook=async(_program,args)=>{assert.match(at(args,'-File'),/YeManTrainerLaunch\.ps1$/);assert(args.includes('-InteractiveShell'));await new Promise(resolve=>finish=()=>{respond(args,{ok:true,pending:false,notice:'已发送启动请求，请确认修改器窗口'});resolve();});return{ok:true};};
    const edit=async()=>{const snapshot=h.latest();h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'game.setField',runId,id:'launch:1',clientId:'launch',sequence:1,generation:snapshot.generation,revision:snapshot.revision,identity:snapshot.game.identity,args:{field:'trainer',value:'open'}}});await flush();const reply=h.replies.filter(r=>r.message.type==='reply').at(-1).message;assert.equal(reply.ok,true,JSON.stringify(reply));};
    let top;if(first==='top'){top=launch();await flush();await edit();}else{await edit();top=launch();await flush();}
    assert.equal(f.fx.workers.length,1);assert.equal(typeof finish,'function');finish();const result=await top;await flush();await h.open();assert.equal(h.latest().trainer.pending,false);assert.equal(h.latest().trainer.notice,result.notice);assert.equal(f.fx.workers.length,1);assert.equal(f.fx.workers.filter(args=>at(args,'-File').endsWith('YeManTrainerPrepare.ps1')).length,0);
   }finally{h?.stop();f.fx.dispose();}
  }
 },{mirror:true});
 await check('launch-entry: both entry points retain a GCM-resolved moved path and surface a denied receipt',async()=>{
  for(const entry of ['top','decky'])for(const denied of [false,true]){
   const {f,g,at,respond,launch}=await launchFixture();
   try{
    assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));
    f.fx.workerHook=async(_program,args)=>{assert.match(at(args,'-File'),/YeManTrainerLaunch\.ps1$/);respond(args,denied?{ok:false,error:'系统拒绝启动，请确认权限窗口',code:'launch-denied'}:{ok:true,trainerPath:'E:\\GCM\\DD2\\Moved.exe',trainerFolder:'E:\\GCM\\DD2'});return{ok:true};};
    const run=()=>entry==='top'?launch():f.trainer.launchReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:async()=>true,interactiveShell:true});
    if(denied){await assert.rejects(run(),/系统拒绝启动/);assert(ready(f),'an OS denial must preserve intact ready files');}
    else{const result=await run();assert.equal(result.trainer.path,'E:\\GCM\\DD2\\Moved.exe');assert.equal(ready(f).path,result.trainer.path);}
    assert.equal(f.fx.workers.length,1);assert(f.fx.workers[0].includes('-InteractiveShell'));
   }finally{f.fx.dispose();}
  }
 });
 await check('launch-entry: cancellation target switch source switch and changed game prevent dispatch',async()=>{
  for(const kind of ['cancel','target','source','game-hash']){
   const {f,g,r,launch}=await launchFixture();let cancelled=false,current=true;
   try{
    if(kind==='game-hash'){assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));f.fx.hashValues.set(f.fx.norm(g.path),'0'.repeat(64));}
    else f.fx.hashHook=async p=>{if(kind==='cancel')cancelled=true;if(kind==='target')current=false;if(kind==='source')f.fx.generation++;return f.fx.hashValues.get(f.fx.norm(p));};
    await assert.rejects(launch({isCancelled:()=>cancelled,isTargetCurrent:async()=>current}));assert.equal(f.fx.workers.length,0,kind);assert.equal(f.fx.files.get(f.fx.norm(r.trainerPath)),'inert trainer bytes');
   }finally{f.fx.dispose();}
  }
 });
 await check('launch-entry: a missing ready cache prepares once then uses the same interactive launcher',async()=>{
  const {f,g,r,at,respond,launch}=await launchFixture();
  try{
   f.fx.files.delete(f.fx.norm(f.fx.indexPath));
   f.fx.workerHook=async(_program,args)=>{const script=at(args,'-File');if(script.endsWith('YeManTrainerPrepare.ps1'))respond(args,{...r,ok:true,state:'ready'});else{assert.match(script,/YeManTrainerLaunch\.ps1$/);assert(args.includes('-InteractiveShell'));respond(args,{ok:true,notice:'已发送启动请求'});}return{ok:true};};
   const result=await launch();assert.equal(result.notice,'已发送启动请求');assert.equal(f.fx.workers.length,2);assert.equal(f.fx.workers.filter(args=>at(args,'-File').endsWith('YeManTrainerPrepare.ps1')).length,1);assert.equal(f.fx.workers.filter(args=>at(args,'-File').endsWith('YeManTrainerLaunch.ps1')).length,1);
  }finally{f.fx.dispose();}
 });
 await check('lifecycle: a reopened game never joins the previous process launch',async()=>{
  for(const entry of ['top','decky']){
   const{f,g,r,respond,at,launch}=await launchFixture();let finish;
   try{
    assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));
    f.fx.workerHook=async(_p,args)=>{if(Number(at(args,'-GameProcessId'))===g.pid)await new Promise(resolve=>finish=resolve);respond(args,{ok:true,notice:'owned inert launch receipt'});return{ok:true};};
    const first=launch({isTargetCurrent:async()=>f.fx.game.pid===g.pid}),rejected=assert.rejects(first,/退出|更换|就绪/);await flush();assert(finish);
    const next={...g,pid:g.pid+1,processCreated:'134386638757313030'};f.fx.select(next);
    f.fx.seedIndex({dragonsdogma2:{...r,identity:{...r.identity,processId:next.pid,processCreated:next.processCreated}}});
    assert(await f.trainer.restoreReadyGameTrainer(next.title,next.path,options(next)));
    const args={processTarget:next,isTargetCurrent:async()=>f.fx.game.pid===next.pid,interactiveShell:true};
    const second=entry==='top'?f.trainer.openOrSearchGameTrainer(next.title,undefined,next.path,args):f.trainer.launchReadyGameTrainer(next.title,next.path,args);
    assert.notEqual(first,second);const result=await second;assert.equal(result.trainer.identity.processId,next.pid);assert.equal(f.fx.workers.length,2);finish();await rejected;
    assert.equal(ready(f,next).identity.processId,next.pid);assert.equal(f.fx.workers.length,2);
   }finally{f.fx.dispose();}
  }
 });
 await check('lifecycle: power transition during file checks prevents either launch entry from dispatching',async()=>{
  for(const entry of ['top','decky']){
   const{f,g,launch}=await launchFixture();try{
    assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));
    f.fx.hashHook=async p=>{f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);return f.fx.hashValues.get(f.fx.norm(p));};
    const pending=entry==='top'?launch():f.trainer.launchReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:async()=>true});
    await assert.rejects(pending,/电源|取消/);assert.equal(f.fx.workers.length,0);assert(ready(f));
   }finally{f.fx.dispose();}
  }
  // The final async game check also belongs to the original power generation,
  // even though the worker receipt has already arrived. No second dispatch or
  // replay is authorized when this old completion crosses suspend/recovery.
  for(const entry of ['top','decky'])for(const transition of ['power-cycle','ready-without-event','webview']){
   const{f,g,respond,launch}=await launchFixture();let release,held=false;
   try{
    assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));
    f.fx.workerHook=async(_p,args)=>{respond(args,{ok:true,notice:'old action receipt'});return{ok:true};};
    const guard=async()=>{if(f.fx.workers.length>0&&!held){held=true;await new Promise(resolve=>release=resolve);}return true;};
    const pending=entry==='top'?launch({isTargetCurrent:guard}):f.trainer.launchReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:guard});
    await flush();assert(release,'Final target validation was not held after the real owner accepted a launch receipt');assert.equal(f.fx.workers.length,1);
    if(transition==='power-cycle'){f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',2);}
    else if(transition==='ready-without-event')f.fx.power={...f.fx.power,generation:2};
    else f.fx.emit('ipc:webview.recovering',99);
    const rejected=assert.rejects(pending,/电源|取消|改变/);release();await rejected;
    assert.equal(f.fx.workers.length,1,'Old completion replayed a launch');assert(ready(f),'Cancelled old acknowledgement discarded reusable trainer files');
    const fresh=entry==='top'?launch():f.trainer.launchReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:async()=>true});
    assert.equal((await fresh).notice,'old action receipt');assert.equal(f.fx.workers.length,2,'A fresh explicit action could not proceed after the old action retired');
    assert.equal(f.fx.workers.filter(args=>args.some(arg=>arg.endsWith('YeManTrainerPrepare.ps1'))).length,0);assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));
   }finally{f.fx.dispose();}
  }
  // Direct manager/CE actions use this same worker without the game's outer
  // click guard. Their pending-file IO must not hide a complete power cycle.
  for(const mode of ['ce','manager']){
   const{f,at}=await launchFixture();let transitioned=false;
   try{
    const put=f.fx.put;
    f.fx.put=(p,s)=>{
     put(p,s);
     if(!transitioned&&p.includes('launch-')&&p.endsWith('.json')&&JSON.parse(s).pending===true){
      transitioned=true;f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',2);
     }
    };
    f.fx.workerHook=async()=>{throw Error('pre-wake direct action reached the worker');};
    await assert.rejects(mode==='ce'?f.trainer.openCheatEngine():f.trainer.openGameTrainerManager(),/电源|取消/);
    assert(transitioned,mode+' pending file must cross the simulated power cycle');assert.equal(f.fx.workers.length,0,mode+' old click cannot dispatch after wake');
    assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));
   }finally{f.fx.dispose();}
  }
 });
 await check('lifecycle: an uncertain old launch retains cancellation and cannot replay itself after wake',async()=>{
  const{f,g,at,respond,launch}=await launchFixture();let finish;
  try{
   f.fx.workerHook=async(_p,args)=>{await new Promise(resolve=>finish=()=>{respond(args,{ok:true,notice:'late receipt'});resolve();});return{ok:true};};
   const pending=launch(),rejected=assert.rejects(pending,/取消|电源/);
   for(let attempt=0;attempt<4&&!finish;attempt++)await flush();
   assert(finish,'uncertain launch must first reach the held worker dispatch');const resultPath=at(f.fx.workers[0],'-ResultPath');
   f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);await flush();assert.equal(f.fx.files.get(f.fx.norm(resultPath+'.cancel')),'cancel');finish();await rejected;
   f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',2);await f.fx.advance(90000);assert.equal(f.fx.workers.length,1);assert.equal(f.fx.files.get(f.fx.norm(resultPath+'.cancel')),'cancel');
  }finally{f.fx.dispose();}
  // The same request must also be retired when no power notification arrives
  // before the consumer exits. A worker may outlive a lost IPC acknowledgement.
  for(const entry of ['top','decky'])for(const fault of ['ack','ack-timeout','directory-timeout','read','malformed','timeout']){
   const{f,g,at,respond,launch}=await launchFixture();let args,finishLate;
   try{
    await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g));assert(ready(f));
    if(fault==='directory-timeout')f.fx.appDirectoryHook=()=>new Promise(()=>{});
    f.fx.workerHook=async(_p,a)=>{
     args=a;
     if(fault==='ack')throw Error('fixture launch acknowledgement lost after dispatch');
     if(fault==='ack-timeout')return new Promise(resolve=>{finishLate=()=>{respond(a,{ok:true,notice:'late abandoned acknowledgement'});resolve({ok:true});};});
     if(fault==='read')f.fx.readHook=async p=>{if(f.fx.norm(p)===f.fx.norm(at(a,'-ResultPath')))throw Error('fixture launch receipt IO denied');return f.fx.files.get(f.fx.norm(p));};
     if(fault==='malformed')f.fx.put(at(a,'-ResultPath'),'{malformed-launch-receipt');
     return{ok:true};
    };
    const pending=entry==='top'?launch():f.trainer.launchReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:async()=>true});
    let settled=false;pending.then(()=>settled=true,()=>settled=true);
    const rejected=assert.rejects(pending);await flush();
    if(fault==='directory-timeout')assert.equal(args,undefined,'unconfirmed directory cannot dispatch a worker');else assert(args,entry+'/'+fault+' must reach the worker');
    if(fault==='ack-timeout'||fault==='directory-timeout'){
     await f.fx.advance(5001);assert.equal(settled,true,entry+'/'+fault+' must leave its pending operation within the existing IO deadline');
    }
    if(fault==='timeout')await f.fx.advance(90001);
    await rejected;
    if(fault==='directory-timeout'){
     assert.deepEqual(f.fx.metadataTimeouts,[5000]);assert.equal(f.fx.workers.length,0);assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));
     f.fx.appDirectoryHook=null;f.fx.workerHook=async(_p,a)=>{respond(a,{ok:true,notice:'explicit directory recovery'});return{ok:true};};
     await launch();assert.equal(f.fx.workers.length,1,'an explicit new click must recover after directory IO returns');continue;
    }
    assert.equal(f.fx.dispatchTimeouts[0],5000,'the real dispatch IPC also needs its own timeout');
    const resultPath=at(args,'-ResultPath'),cancelPath=resultPath+'.cancel';
    assert.equal(f.fx.files.get(f.fx.norm(cancelPath)),'cancel',entry+'/'+fault+' must cancel its dispatched request on exit');
    assert(f.fx.files.has(f.fx.norm(resultPath)),entry+'/'+fault+' must preserve the uncertain request binding');
    assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0),'exited launch must release its lifecycle observer');
    f.fx.readHook=null;
    f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',2);await f.fx.advance(90000);
    assert.equal(f.fx.workers.length,1,'wake cannot replay the abandoned launch');assert.equal(f.fx.files.get(f.fx.norm(cancelPath)),'cancel');
    f.fx.workerHook=async(_p,a)=>{assert.notEqual(at(a,'-ResultPath'),resultPath);respond(a,{ok:true,notice:'explicit new launch'});return{ok:true};};
    await launch();assert.equal(f.fx.workers.length,2,'an explicit new request can recover');assert.equal(f.fx.files.get(f.fx.norm(cancelPath)),'cancel','new request cannot remove the old cancellation');
    if(finishLate){finishLate();await flush();assert.equal(f.fx.workers.length,2,'late acknowledgement cannot dispatch or revive another launch');assert.equal(f.fx.files.get(f.fx.norm(cancelPath)),'cancel');assert(ready(f));}
   }finally{f.fx.dispose();}
  }
 });
 await check('lifecycle: a ready snapshot before the resume event releases paused owners once',async()=>{
  const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();f.fx.autoEnabled=true;const stop=f.auto.startGameTrainerAutoPrepare();try{
   await flush();f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);assert.equal(f.fx.timers.size,0);
   f.fx.power.phase='ready';f.fx.seedIndex();f.sharedPower.requestSharedPowerLifecycleReconcile('fixture-native-ready-before-resume');await advanceExistingPublicRetry(f);assert.equal(f.sharedPower.getSharedPowerLifecycle().phase,'ready');await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch(),2);const epoch=f.lifecycle.getTrainerLifecycleEpoch();f.fx.emit('ipc:power.resumed',2);await flush();
   assert(ready(f),'a confirmed ready snapshot must not swallow recovery resume');assert(f.fx.timers.size>0);const timerIds=[...f.fx.timers.keys()];f.fx.emit('ipc:power.resumed',2);await flush();assert.deepEqual([...f.fx.timers.keys()],timerIds,'duplicate resume must not reset timers');assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),epoch,'current session admission must survive a late same-generation resume');noSideEffects(f);
  }finally{stop();assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));f.fx.dispose();}
 });
 await check('lifecycle: automatic owner remains paused through game and preference events until resume',async()=>{
  for(const gate of ['notification-pending','writes-blocked','webview-confirmation']){
   const f=await createTrainerRecoveryFixture({separatePowerClock:true});
   const admitted={known:true,phase:'ready',generation:7,publicationRevision:1,observedNotificationSequence:4,hardwareWritesAllowed:true,notificationPending:false,inputReady:false};
   const current={...admitted,...(gate==='notification-pending'?{notificationPending:true}:gate==='writes-blocked'?{hardwareWritesAllowed:false}:{})};
   f.fx.power=current;f.fx.seedIndex();f.fx.game=game();f.fx.autoEnabled=true;
   const detach=gate==='webview-confirmation'?f.lifecycle.subscribeTrainerLifecycle(()=>{}):f.sharedPower.onSharedPowerLifecycle(()=>{});
   await flush();assert.equal(f.sharedPower.getSharedPowerLifecycle().phase,'ready');
   let release;f.fx.powerReadHook=()=>new Promise(resolve=>{release=resolve});
   if(gate==='webview-confirmation'){f.fx.emit('ipc:webview.recovering');await flush();}
   const stop=f.auto.startGameTrainerAutoPrepare();
   try{
    await flush();f.fx.select(game());f.fx.emit('ui-settings:changed');await flush();
    assert.equal(f.fx.timers.size,0,gate+': newly attached Auto must wait for shared admission');
    assert.equal(f.fx.reads.length,0,gate+': newly attached Recovery must wait for shared admission');
    noSideEffects(f);
    const confirmed={...admitted,publicationRevision:2};f.fx.power=confirmed;
    if(gate==='webview-confirmation'){
     assert.equal(typeof release,'function','the existing public confirmation owns the read');release(confirmed);release=null;
    }else{
     for(const listener of [...f.fx.events.get('ipc:power.lifecycleChanged')])listener({type:'ipc:power.lifecycleChanged',detail:confirmed});
    }
    await flush();
    assert(ready(f),gate+': original Recovery restores the retained cache on valid admission');
    assert.equal(f.fx.reads.filter(r=>r.p===f.fx.indexPath).length,1,gate+': one cache read');
    assert([...f.fx.timers.values()].some(t=>t.at-f.fx.now===30000),gate+': original Auto schedule resumes');
    noSideEffects(f);
   }finally{
    stop();detach();release?.(f.fx.power);await flush();
    assert.equal(f.fx.timers.size,0);assert.equal(f.fx.sharedTimers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));f.fx.dispose();
   }
  }
  const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();f.fx.autoEnabled=true;
  f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async(_p,args)=>{f.fx.seedIndex();f.fx.put(args[args.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(args,{...receipt(),ok:true,state:'ready'})));return{ok:true};};
  const stop=f.auto.startGameTrainerAutoPrepare();try{
   await flush();assert(f.fx.timers.size>0);f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);assert.equal(f.fx.timers.size,0);
   for(let i=0;i<3;i++){f.fx.select(game());f.fx.emit('ui-settings:changed');await flush();const shared=f.sharedPower.getSharedPowerLifecycleDiagnostics();assert.equal(f.fx.timers.size,0,'trainer timers stay absent');assert.equal(f.fx.sharedTimers.size,shared.read.retryTimerActive?1:0,'only the same public authority may retain its existing read retry');assert.equal(f.fx.workers.length,0);noSideEffects(f);await f.fx.advance(60000);}
   assert.equal(f.fx.workers.length,0);assert.equal(f.trainer.getGameTrainerState(f.fx.game.title,f.fx.game.path),null);
   f.fx.power={...f.fx.power,phase:'ready',known:true,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:1};
   for(const cb of [...f.fx.events.get('ipc:power.lifecycleChanged')||[]])cb({type:'ipc:power.lifecycleChanged',detail:{...f.fx.power}});
   await flush();await f.fx.advance(2500);assert.equal(f.fx.workers.length,1);assert(ready(f));assert.equal(f.fx.launches.length,0);
  }finally{stop();assert.equal(f.fx.timers.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));f.fx.dispose();}
 });
 await check('lifecycle: unavailable automatic preferences can resume on confirmed Ready',async()=>{
  for(const mode of ['first-read-rejected','old-read-rejects-after-ready','preference-disabled','owner-stopped']){
   const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.autoEnabled=mode!=='preference-disabled';
   let reads=0,rejectOld,finish=()=>{};
   f.fx.trainerPreferencesHook=async()=>{
    if(++reads!==1)return;
    if(mode==='old-read-rejects-after-ready')await new Promise((_resolve,reject)=>{rejectOld=reject;});
    throw Error('fixture interrupted preferences read');
   };
   const stop=f.auto.startGameTrainerAutoPrepare();finish=stop;
   try{
    await flush();assert.equal(reads,1,mode);assert.equal(f.fx.timers.size,0,mode);
    const publish=async snapshot=>{f.fx.power=snapshot;for(const listener of [...f.fx.events.get('ipc:power.lifecycleChanged')||[]])listener({detail:snapshot});await flush();};
    await publish({...f.fx.power,phase:'suspending',known:true,generation:2,suspendSerial:2,observedNotificationSequence:2,hardwareWritesAllowed:false,notificationPending:false,publicationRevision:2});
    assert.equal(f.sharedPower.isSharedPowerLifecycleAdmitted(f.sharedPower.getSharedPowerLifecycle()),false,mode+': public scope is closed');
    await f.fx.advance(30000);assert.equal(reads,1,mode);assert.equal(f.fx.timers.size,0,mode);noSideEffects(f);
    if(mode==='owner-stopped'){stop();finish=()=>{};}
    const admitted={...f.fx.power,phase:'ready',hardwareWritesAllowed:true,observedNotificationSequence:3,publicationRevision:3};
    await publish(admitted);
    if(mode!=='owner-stopped')assert.equal(f.sharedPower.isSharedPowerLifecycleAdmitted(f.sharedPower.getSharedPowerLifecycle()),true,mode+': public scope is confirmed');
    if(rejectOld){assert.equal(reads,1,'do not duplicate an in-flight read');rejectOld(Error('late failure'));rejectOld=null;await flush();}
    await publish(admitted);
    assert.equal(reads,mode==='owner-stopped'?1:2,mode+': confirmed Ready must restore the preference read once');
    assert.equal(f.fx.timers.size,mode==='preference-disabled'||mode==='owner-stopped'?0:1,mode+': only a loaded ON preference may arm the existing timer');
    noSideEffects(f);
   }finally{finish();rejectOld?.(Error('fixture disposed'));await flush();assert.equal(f.fx.timers.size,0);f.fx.dispose();}
  }
 });
 await check('lifecycle: closing the automatic owner cannot rearm from a late catalog completion',async()=>{
  const f=await createTrainerRecoveryFixture({separatePowerClock:true});f.fx.game=game();f.fx.seedIndex();f.fx.autoEnabled=true;let finish;
  try{
   f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
   f.fx.workerHook=async(_p,args)=>{await new Promise(resolve=>finish=()=>{const at=name=>args[args.indexOf(name)+1];f.fx.put(at('-ResultPath'),JSON.stringify(f.fx.requestReceipt(args,{ok:true,state:'unavailable',gameName:'YMCC Trainer Catalog',catalog:{updated:true}})));resolve();});return{ok:true};};
   const stop=f.auto.startGameTrainerAutoPrepare();await flush();await f.fx.advance(30000);assert.equal(f.fx.workers.length,1);assert(finish);stop();finish();await flush();
   assert.equal(f.fx.timers.size,0);assert.equal(f.fx.listeners.size,0);assert([...f.fx.events.values()].every(s=>s.size===0));await f.fx.advance(86400000);assert.equal(f.fx.workers.length,1);
  }finally{f.fx.dispose();}
 });
 await check('catalog: first open defers a due refresh and its receipt survives a renderer restart',async()=>{
  const storage=new Map();
  const start=async (storageForInstance=storage)=>{
   const f=await createTrainerRecoveryFixture({separatePowerClock:true,storage:storageForInstance});
   f.fx.game=null;f.fx.autoEnabled=true;
   f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
   return f;
  };
  const a=await start(),nextCheckAt=a.fx.now+120000;
  const worker=f=>async(_p,args)=>{
   assert.equal(args[args.indexOf('-Mode')+1],'RefreshData');
   const next=Math.max(nextCheckAt,f.fx.now+60000);
   f.fx.put(args[args.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(args,{ok:true,state:'ready',gameName:'YMCC Trainer Catalog',catalog:{available:true,nextCheckAt:next},retryAfter:next})));
   return {ok:true};
  };
  a.fx.workerHook=worker(a);
  const stopA=a.auto.startGameTrainerAutoPrepare();
  try{
   await flush();await a.fx.advance(28000);a.fx.emit('ipc:window.shown');
   await a.fx.advance(2000);assert.equal(a.fx.workers.length,0);
   await a.fx.advance(7999);assert.equal(a.fx.workers.length,0);
   await a.fx.advance(1);assert.equal(a.fx.workers.length,1);
   assert.equal(a.fx.launches.length,0);
  }finally{stopA();a.fx.dispose();}
  // Separate branches both consume the actual previous renderer's receipt.
  for(const restartAt of [a.fx.now+1000,nextCheckAt-5000]){
   const b=await start(new Map(storage));b.fx.now=restartAt;b.fx.workerHook=worker(b);
   const stopB=b.auto.startGameTrainerAutoPrepare();
   try{
    await flush();const expected=Math.max(nextCheckAt,restartAt+30000);
    await b.fx.advance(expected-restartAt-1);assert.equal(b.fx.workers.length,0);
    await b.fx.advance(1);assert.equal(b.fx.workers.length,1);
   }finally{stopB();b.fx.dispose();}
  }
 });
 await check('lifecycle: a lost preparation dispatch acknowledgement retains its pending identity and cancellation',async()=>{
  for(const fault of ['ack','ack-timeout']){
  const f=await createTrainerRecoveryFixture({separatePowerClock:true}),g=game();f.fx.game=g;let finishLate;
  try{
   f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async(_p,args)=>{
    if(fault==='ack')throw Error('fixture acknowledgement lost after dispatch');
    return new Promise(resolve=>{finishLate=()=>{f.fx.put(args[args.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(args,{...receipt(),ok:true,state:'ready'})));resolve({ok:true});};});
   };
   let settled=false;const pending=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});pending.then(()=>settled=true,()=>settled=true);
   const rejected=assert.rejects(pending,fault==='ack'?/acknowledgement lost/:/超时/);await flush();
   if(fault==='ack-timeout'){
    f.fx.power={...f.fx.power,generation:2,phase:'suspending'};f.fx.emit('ipc:power.suspending',2);await f.fx.advance(5001);
    assert.equal(settled,true,'an unacknowledged old dispatch cannot permanently own the preparation queue');
   }
   await rejected;
   assert.equal(f.fx.workers.length,1);const args=f.fx.workers[0],resultPath=args[args.indexOf('-ResultPath')+1];
   assert.equal(JSON.parse(f.fx.files.get(f.fx.norm(resultPath))).requestProtocol,1);assert.equal(f.fx.files.get(f.fx.norm(resultPath+'.cancel')),'cancel');assert.equal(f.fx.timers.size,0);
   assert.equal(f.fx.dispatchTimeouts[0],5000);assert([...f.fx.events.values()].every(s=>s.size===0));
   f.fx.power.phase='ready';f.fx.emit('ipc:power.resumed',2);
   const next={...g,pid:g.pid+1,processCreated:String(BigInt(g.processCreated)+1n),title:'Cyberpunk 2077',name:'Cyberpunk2077.exe',path:'D:\\fixture\\Cyberpunk2077.exe'};
   const nextReceipt={...receipt(),ok:true,state:'ready',gameName:next.title,trainerGameName:next.title,trainerPath:'D:\\fixture-gcm\\Cyberpunk\\Trainer.exe',trainerFolder:'D:\\fixture-gcm\\Cyberpunk',gcmResourceId:'fixture/Cyberpunk/Trainer.exe',identity:{...receipt().identity,steamAppId:'1091500',gameName:next.title,windowTitle:next.title,aliases:[next.title],processId:next.pid,processCreated:next.processCreated,executablePath:next.path}};
   f.fx.game=next;f.fx.seedIndex({[f.trainer.trainerGameKey(next.title)]:nextReceipt});
   f.fx.workerHook=async(_p,a)=>{assert.notEqual(a[a.indexOf('-ResultPath')+1],resultPath);f.fx.put(a[a.indexOf('-ResultPath')+1],JSON.stringify(f.fx.requestReceipt(a,nextReceipt)));return{ok:true};};
   assert.equal((await f.trainer.prepareGameTrainer(next.title,{processPath:next.path,processTarget:next})).state,'ready');assert.equal(f.fx.workers.length,2,'the next game must own a fresh worker after the old dispatch times out');
   if(finishLate){finishLate();await flush();assert.equal(f.fx.workers.length,2);assert.equal(f.trainer.getGameTrainerState(g.title,g.path).state,'failed','late old reply cannot publish ready');}
   assert(f.trainer.getReadyGameTrainer(next.title,next.path,next));assert.equal(f.fx.files.get(f.fx.norm(resultPath+'.cancel')),'cancel');assert.equal(f.fx.timers.size,0);
  }finally{f.fx.dispose();}
  }
 });
 if(!selected.length)throw Error('No recovery cases match the requested filter');
 const admission=prepareTrainerRecoveryAdmission(selected);
 ({inputs,before,runtime}=admission);
 fs.mkdirSync(out,{recursive:true});
 for(const item of selected){
  try{await item.fn(admission.prepared.get(item));cases.push({name:item.name,passed:true});}
  catch(error){cases.push({name:item.name,passed:false,error:String(error),stack:error.stack});finish({passed:false});throw error;}
 }
 return finish({simulatedSuccessiveGames:caseFilter===null?5:0,fieldGame:field.game.title,physicalFseAcceptance:false});
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const idx=process.argv.indexOf('--out'),out=idx>=0?path.resolve(process.argv[idx+1]):path.join(root,'Build/Validation/TrainerRecovery');
 const selected=process.argv.indexOf('--case-filter');
 const r=await trainerRecoveryTests(out,{caseFilter:selected<0?null:process.argv[selected+1]??''});console.log(JSON.stringify(r));if(!r.passed)process.exitCode=1;
}
