import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createTrainerRecoveryFixture,field,flush} from './fixtures/game_trainer_recovery_fixture.mjs';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const root=path.resolve(import.meta.dirname,'..'),clone=structuredClone;
const game=()=>clone(field.game),receipt=()=>clone(Object.values(field.index)[0]);
const options=g=>({processTarget:g,isTargetCurrent:()=>true});
const ready=(f,g=f.fx.game)=>f.trainer.getReadyGameTrainer(g.title,g.path,g);
const noSideEffects=f=>{assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.writes.length,0);};
const setReceipt=(f,g,r)=>f.fx.seedIndex({[f.trainer.trainerGameKey(g.title)]:r});
const runId='b'.repeat(32),peer={connection:'1',runId,connected:true};
async function hostFor(f,hostSource=null){
 const host=await createMirrorHostFixture({trainerOwner:f.trainer,trainerRecovery:f.recovery,hostSource});
 host.state.game=f.fx.game;host.ready();
 host.open=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.latest();};
 host.latest=()=>host.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;
 host.pause=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();};
 host.select=g=>{host.state.game=g;f.fx.select(g);host.gameChanged();};
 await host.open();return host;
}
export async function trainerRecoveryTests(out){
 fs.mkdirSync(out,{recursive:true});const cases=[];
 const check=async(name,fn)=>{await fn();cases.push({name,passed:true});};
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
  f.fx.workerHook=async(_p,args)=>{await new Promise(resolve=>release=resolve);const at=key=>args[args.indexOf(key)+1];f.fx.put(at('-ResultPath'),JSON.stringify({...receipt(),ok:true,requestId:at('-RequestId')}));return{ok:true};};
  const preparation=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});await flush();const reads=f.fx.reads.length;assert.equal(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)),null);assert.equal(f.fx.reads.length,reads);release();await preparation;assert(ready(f,g));assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);
 });
 await check('timeout followed by a later persisted ready receipt recovers without a second preparation worker',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');f.fx.workerHook=async()=>({ok:true});
  const preparation=f.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g}),rejected=assert.rejects(preparation,/超时/);await flush();await f.fx.advance(200001);await rejected;
  assert.equal(f.trainer.getGameTrainerState(g.title,g.path).state,'failed');const stop=f.recovery.startGameTrainerRecovery();await flush();f.fx.seedIndex();await f.fx.advance(5000);assert(ready(f,g));assert.equal(f.fx.workers.length,1);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.timers.size,0);stop();
 });
 await check('ordinary preparation success pushes Decky ready immediately without waiting for the recovery poll',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game();f.fx.game=g;const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f);f.fx.seedIndex();f.fx.files.delete(f.fx.norm(f.fx.indexPath));f.fx.put(f.fx.powerDir+'\\YeManTrainerPrepare.ps1','inert worker');
  f.fx.workerHook=async(_p,args)=>{const at=key=>args[args.indexOf(key)+1];f.fx.put(at('-ResultPath'),JSON.stringify({...receipt(),ok:true,requestId:at('-RequestId')}));return{ok:true};};
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
  b.fx.workerHook=async(_p,args)=>{const at=key=>args[args.indexOf(key)+1];b.fx.put(at('-ResultPath'),JSON.stringify({gameName:g.title,requestId:at('-RequestId'),state:'unavailable',ok:false}));return{ok:true};};
  const end=b.recovery.startGameTrainerRecovery();await flush();await b.trainer.prepareGameTrainer(g.title,{processPath:g.path,processTarget:g});await flush();assert.equal(b.fx.timers.size,0);end();
 });
 await check('one corrupt matching alias cannot hide the other valid receipt',async()=>{
  const f=await createTrainerRecoveryFixture(),g=game(),r=receipt(),k=f.trainer.trainerGameKey(g.title);f.fx.seedIndex();
  f.fx.put(f.fx.indexPath,JSON.stringify({[k]:{...r,identity:{...r.identity,gameName:42}},[k+'|valid']:r}));
  assert(await f.trainer.restoreReadyGameTrainer(g.title,g.path,options(g)));noSideEffects(f);
 });
 await check('a hydrated local trainer prevents automatic download work when automatic preparation is enabled',async()=>{
  const f=await createTrainerRecoveryFixture();f.fx.game=game();f.fx.autoEnabled=true;f.fx.seedIndex();const stop=f.auto.startGameTrainerAutoPrepare();await flush();await f.fx.advance(2500);assert(ready(f));noSideEffects(f);stop();assert.equal(f.fx.timers.size,0);
 });
 const owner=fs.readFileSync(path.join(root,'src/bridge/gameTrainer.ts'),'utf8'),recovery=fs.readFileSync(path.join(root,'src/bridge/gameTrainerRecovery.ts'),'utf8'),host=fs.readFileSync(path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8');
 async function lateAssertion(overrides={},hostSource=null){const f=await createTrainerRecoveryFixture(overrides);f.fx.game=game();const stop=f.recovery.startGameTrainerRecovery(),h=await hostFor(f,hostSource);f.fx.seedIndex();await f.fx.advance(5000);try{assert.equal(h.latest().trainer.available,true,'late ready must push the Decky button');}finally{h.stop();stop();}}
 const ownerAnchor='  const generation = syncTrainerSource(), title = cleanGameTitle(gameName).trim();';assert(owner.includes(ownerAnchor));
 await assert.rejects(lateAssertion({ownerSource:owner.replace(ownerAnchor,'  return Promise.resolve(null);\n'+ownerAnchor)}),/late ready must push/);cases.push({name:'negative old memory-only behavior reproduces unavailable button after persisted ready',passed:true});
 const pollAnchor='timer = setTimeout(recover, delay);';assert(recovery.includes(pollAnchor));await assert.rejects(lateAssertion({recoverySource:recovery.replace(pollAnchor,'timer = null;')}),/late ready must push/);cases.push({name:'negative removed unresolved-result fallback reproduces late ready loss',passed:true});
 const pushAnchor='stops.push(subscribeGameTrainerState(() => {';assert(host.includes(pushAnchor));await assert.rejects(lateAssertion({},host.replace(pushAnchor,pushAnchor+' return;')),/late ready must push/);cases.push({name:'negative removed owner push leaves an open Decky sidebar stale',passed:true});
 const report={passed:true,cases:cases.length,checks:cases,simulatedSuccessiveGames:5,fieldGame:field.game.title,physicalFseAcceptance:false,realProcessLaunches:0,realGameMemoryReads:0,userSettingsWrites:0,realSteamActions:0};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));return report;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const idx=process.argv.indexOf('--out'),out=idx>=0?path.resolve(process.argv[idx+1]):path.join(root,'Build/Validation/TrainerRecovery');
 const r=await trainerRecoveryTests(out);console.log(JSON.stringify(r));
}