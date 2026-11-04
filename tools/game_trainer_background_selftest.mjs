import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
const startedAt=Date.now(),digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const actualInputs=new Map(),mockInputs=new Map();
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const actualProducerContracts=(process.env.YMCC_TRAINER_PRODUCER_CONTRACTS||'').split(';').filter(Boolean).map(name=>{
 const artifactPath=path.resolve(name),artifactBytes=fs.readFileSync(artifactPath),contract=JSON.parse(artifactBytes.toString('utf8').replace(/^\uFEFF/,''));
 const receiptPath=path.join(path.dirname(artifactPath),'result.json'),receiptBytes=fs.readFileSync(receiptPath),receipt=JSON.parse(receiptBytes.toString('utf8').replace(/^\uFEFF/,''));
 assert.equal(receipt.status,'PASS');assert.equal(receipt.failed,0);assert.deepEqual(receipt.sourceDrift,[]);assert.equal(contract.networkCalls,0);
 const backend=receipt.source.find(input=>input.path.replaceAll('\\','/').endsWith('/PowerControl/Trainer/TrainerBackend.psm1'));
 assert(backend);assert.equal(backend.sha256.toLowerCase(),contract.producerSha256.toLowerCase());
 for(const input of receipt.source){assert.equal(digest(fs.readFileSync(input.path)),input.sha256.toLowerCase());actualInputs.set(input.path,{path:input.path,sha256:input.sha256.toLowerCase(),kind:'declared source actually consumed by upstream writer selftest'});}
 assert.equal(digest(fs.readFileSync(receipt.processPath)),receipt.runtimeSha256.toLowerCase());
 actualInputs.set(receipt.processPath,{path:receipt.processPath,sha256:receipt.runtimeSha256.toLowerCase(),kind:'upstream actual PowerShell runtime'});
 actualInputs.set(artifactPath,{path:artifactPath,sha256:digest(artifactBytes),kind:'actual persisted producer output'});
 actualInputs.set(receiptPath,{path:receiptPath,sha256:digest(receiptBytes),kind:'upstream producer consumer receipt'});
 return {artifactPath,contract};
});

const negativeOldCompletion = process.env.YMCC_TRAINER_OLD_COMPLETION_NEGATIVE === '1';
const mocks = {
  'frontendDiagnostics.ts': `export const reportFrontendError=(where,error)=>{(globalThis.fx.diagnostics||=[]).push({where,error:String(error)})};`,
  'api.ts': `const f=globalThis.fx;export const app={powerControlDir:async()=>f.resolvePowerDir?f.resolvePowerDir():f.powerDir,dataDir:async()=>f.dataDir,exeDir:async()=>f.appDir||'E:/YMCC'};export const fs={exists:async p=>f.resolveExists?f.resolveExists(p):f.files.has(p.replaceAll('/',String.fromCharCode(92))),mkdir:async p=>f.resolveMkdir?f.resolveMkdir(p):true,readTextFile:async p=>{if(f.readHook)await f.readHook(p);if(f.readError)throw Error('fixture read denied');if(!f.files.has(p))throw Error('missing');return f.files.get(p)},writeTextFileAtomic:async(p,s)=>{if(f.writeError)throw Error('pending denied');if(f.falseWrite)return;f.files.set(p,s)},remove:async p=>f.resolveRemove?f.resolveRemove(p):f.files.delete(p),sha256File:async p=>f.resolveHash?f.resolveHash(p):f.hash};export const shell={hidden:async(p,a)=>{return f.runHidden(p,a)},execute:async(p)=>{f.launches.push(p);return f.executeOk}};`,
  'settingsRepository.ts': `export const getSettingsGeneration=()=>globalThis.fx.generation;`,
  'gamedetect.ts': `export const cleanGameTitle=s=>s.trim();export const detectedGameName=g=>g?.title||g?.name||'';`,
  'gamePolicyTarget.ts': `const f=globalThis.fx;export const getPolicyGame=()=>f.game;export const subscribePolicyGameStatus=cb=>{f.gameListeners.add(cb);f.gameListener=game=>{f.game=game;for(const listener of [...f.gameListeners])listener(game)};return()=>{f.gameListeners.delete(cb);if(!f.gameListeners.size)f.gameListener=null}};`,
  'uiSettings.ts': `export const getUiSetting=()=>globalThis.fx.enabled;export const loadUiSettings=async()=>{if(globalThis.fx.settingsError)throw Error('settings denied')};`,
};
const bundle = await build({
 stdin:{contents:`export * as trainer from './src/bridge/gameTrainer';export * as auto from './src/bridge/gameTrainerAuto';`,resolveDir:root,loader:'ts'},
 bundle:true,format:'cjs',platform:'node',write:false,metafile:true,
 plugins:[{name:'trainer-fixtures',setup(b){
  if(negativeOldCompletion)b.onLoad({filter:/gameTrainerAuto[.]ts$/},a=>{
   const original=fs.readFileSync(a.path,'utf8'),guard='!stopped && targetEpoch === gameEpoch && generation === getSettingsGeneration()) schedule();';assert(original.includes(guard));
   const contents=original.replace(guard,'!stopped && generation === getSettingsGeneration()) schedule();');
   actualInputs.set(a.path,{path:a.path,sha256:digest(original),compiledSha256:digest(contents),mutation:'original old-completion negative'});
   return{contents,loader:'ts'};
  });
  b.onLoad({filter:/[\\/](api|gamedetect|gamePolicyTarget|uiSettings|settingsRepository|frontendDiagnostics)\.ts$/},a=>{
   const contents=mocks[path.basename(a.path)];mockInputs.set(a.path,{path:a.path,sha256:digest(contents),kind:'inert memory IPC/source fixture'});return{contents,loader:'ts'};
  });
  b.onLoad({filter:/\.(ts|json|[mc]?js)$/},a=>{
   const bytes=fs.readFileSync(a.path);actualInputs.set(a.path,{path:a.path,sha256:digest(bytes)});
   return{contents:bytes,loader:a.path.endsWith('.ts')?'ts':a.path.endsWith('.json')?'json':'js'};
  });
 }}],
});
const self=fileURLToPath(import.meta.url);actualInputs.set(self,{path:self,sha256:digest(fs.readFileSync(self))});

const drain = async()=>{for(let i=0;i<240;i++)await Promise.resolve()};
function fixture() {
  const fx={files:new Map(),workers:[],launches:[],launchArgs:[],enabled:true,executeOk:true,settingsError:false,writeError:false,hash:'a'.repeat(64),now:1700000000000,timers:new Map(),nextTimer:1,events:new Map(),gameListener:null,game:null,gameListeners:new Set(),generation:0,powerDir:'D:/App/PowerControl'.replaceAll('/',String.fromCharCode(92)),dataDir:'D:/Data'.replaceAll('/',String.fromCharCode(92)),resolvePowerDir:null,resolveHash:null};
  fx.files.set('D:\\App\\PowerControl\\YeManTrainerPrepare.ps1','worker');
  fx.files.set('D:\\App\\PowerControl\\YeManTrainerLaunch.ps1','worker');
  fx.runHidden=async(p,a)=>{if(argument(a,'-File').endsWith('YeManTrainerLaunch.ps1')){fx.launchArgs.push(a);fx.launches.push(argument(a,'-TrainerPath'));fx.files.set(argument(a,'-ResultPath'),JSON.stringify({...requestBinding(fx,a),requestId:argument(a,'-RequestId'),ok:fx.executeOk,error:'launch rejected',...fxx(fx)}));return{ok:true}}fx.workers.push(a);return fx.hidden(p,a)};
  fx.hidden=async(_program,args)=>{respond(fx,args,{state:'unavailable',ok:false,message:'none'});return {ok:true,pid:42}};
  class Clock extends Date { static now(){return fx.now} }
  const module={exports:{}};
  vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,console,Date:Clock,
    setTimeout:(fn,ms)=>{const id=fx.nextTimer++;fx.timers.set(id,{fn,at:fx.now+ms});return id},clearTimeout:id=>fx.timers.delete(id),
    window:{addEventListener:(n,cb)=>{if(!fx.events.has(n))fx.events.set(n,new Set());fx.events.get(n).add(cb)},removeEventListener:(n,cb)=>fx.events.get(n)?.delete(cb)},
  });
  // Settle already-complete mock I/O before advancing its response deadlines.
  fx.advance=async(ms)=>{await drain();fx.now+=ms;for(const[id,timer]of [...fx.timers])if(timer.at<=fx.now){fx.timers.delete(id);timer.fn()}await drain()};
  fx.emit=name=>{for(const cb of fx.events.get(name)||[])cb()};
  return {...module.exports,fx};
}
function fxx(fx){return fx.launchReceipt||{}}
function argument(args,key){return args[args.indexOf(key)+1]}
function requestBinding(fx,args){const stored=JSON.parse(fx.files.get(argument(args,'-ResultPath'))||'{}');return Object.fromEntries(['requestProtocol','requestToken','sourceSession','gameExecutablePath','gameProcessId','gameProcessCreated'].filter(key=>key in stored).map(key=>[key,stored[key]]))}
function respond(fx,args,extra={}){const result={...requestBinding(fx,args),requestId:argument(args,'-RequestId'),gameName:argument(args,'-GameName'),...extra};fx.files.set(argument(args,'-ResultPath'),JSON.stringify(result));return result}
function ready(fx,args,extra={}){const trainerPath='D:\\Data\\trainer-cache\\installed\\fixture\\Trainer.exe';fx.files.set(trainerPath,'binary');return respond(fx,args,{ok:true,state:'ready',trainerPath,trainerFolder:'D:\\Data\\trainer-cache\\installed\\fixture',trainerGameName:argument(args,'-GameName'),origin:'fixture',sha256:fx.hash,identity:{method:'steam-appid',steamAppId:'12345',manifestPath:'D:/Steam/steamapps/appmanifest_12345.acf',aliases:[argument(args,'-GameName')],...(args.includes('-GameProcessId')?{processId:Number(argument(args,'-GameProcessId')),processCreated:argument(args,'-GameProcessCreated'),executablePath:argument(args,'-GameExecutablePath'),executableSha256:fx.hash}:{})},...extra})}
const checks=[];
async function check(name,fn){try{await fn();checks.push({name,ok:true});console.log('PASS '+name)}catch(error){checks.push({name,ok:false,error:error.stack});console.error('FAIL '+name+'\n'+error.stack)}}
await check('exact names preserve sequel numbers and normalize punctuation',async()=>{const{trainer:t}=fixture();assert.notEqual(t.trainerGameKey('Victoria 3'),t.trainerGameKey('Victoria 2'));assert.equal(t.trainerGameKey('Victoria: 3'),t.trainerGameKey('VICTORIA 3'));assert.equal(t.canAutoPrepareTrainer('Mario','D:\\yuzu.exe'),false);assert.equal(t.canAutoPrepareTrainer('Trainer','D:\\GCM Trainers\\game\\play.exe'),false)});
await check('automatic preparation never launches an executable',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};const result=await t.prepareGameTrainer('Victoria 3',{automatic:true});assert.equal(result.state,'ready');assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,0);assert.equal(fx.workers[0].includes('-Force'),false)});
await check('manual click joins preparation and concurrent clicks launch once',async()=>{const{trainer:t,fx}=fixture();let finish;fx.hidden=async(p,a)=>{await new Promise(r=>finish=r);ready(fx,a);return{ok:true}};const pending=t.prepareGameTrainer('Victoria 3',{automatic:true});await drain();const a=t.openOrSearchGameTrainer('Victoria 3');const b=t.openOrSearchGameTrainer('Victoria 3');await drain();finish();await Promise.all([pending,a,b]);assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,1)});
await check('different games serialize workers and isolate result identities',async()=>{const{trainer:t,fx}=fixture();let finish;fx.hidden=async(p,a)=>{if(argument(a,'-GameName')==='first')await new Promise(r=>finish=r);respond(fx,a,{ok:false,state:'unavailable'});return{ok:true}};const a=t.prepareGameTrainer('first');const b=t.prepareGameTrainer('second');await drain();assert.equal(fx.workers.length,1);finish();await Promise.all([a,b]);assert.equal(fx.workers.length,2);assert.notEqual(argument(fx.workers[0],'-ResultPath'),argument(fx.workers[1],'-ResultPath'))});
await check('pending atomic write failure starts no worker',async()=>{const{trainer:t,fx}=fixture();fx.writeError=true;await assert.rejects(t.prepareGameTrainer('fixture'),/pending denied/);assert.equal(fx.workers.length,0)});
await check('worker start failure is surfaced without long polling',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async()=>({ok:false});await assert.rejects(t.prepareGameTrainer('fixture'),/无法启动/);assert.equal(t.getGameTrainerState('fixture').state,'failed')});
await check('stale request result cannot launch another trainer',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a,{requestId:'old-request'});return{ok:true}};const pending=t.openOrSearchGameTrainer('fixture');await drain();assert.equal(fx.launches.length,0);respond(fx,fx.workers[0],{ok:false,state:'unavailable',message:'missing'});await fx.advance(400);await assert.rejects(pending,/missing/);assert.equal(fx.launches.length,0)});
await check('cancel writes a cooperative signal and prevents subsequent launch',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{respond(fx,a,{ok:false,state:'downloading'});return{ok:true}};const pending=t.openOrSearchGameTrainer('fixture');const rejection=assert.rejects(pending,/取消/);await drain();const resultPath=argument(fx.workers[0],'-ResultPath');await t.cancelGameTrainerPreparation('fixture');assert.equal(fx.files.get(resultPath+'.cancel'),'cancel');ready(fx,fx.workers[0]);await fx.advance(400);await rejection;assert.equal(fx.launches.length,0)});
await check('game exit while downloading preserves files and prevents launch',async()=>{const{trainer:t,fx}=fixture();let current=true;fx.hidden=async(p,a)=>{ready(fx,a);current=false;return{ok:true}};await assert.rejects(t.openOrSearchGameTrainer('fixture',undefined,'D:\\game.exe',{isTargetCurrent:async()=>current}),/已退出或更换/);assert.equal(fx.launches.length,0);assert.equal(t.getGameTrainerState('fixture').state,'ready')});
await check('persisted index is revalidated by the shared-library worker before launch',async()=>{const{trainer:t,fx}=fixture();const trainerPath='D:\\Data\\trainer-cache\\installed\\fixture\\Trainer.exe';fx.files.set(trainerPath,'binary');fx.files.set('D:\\Data\\trainer-cache\\trainers.json',JSON.stringify({fixture:{state:'ready',trainerPath,trainerFolder:'D:\\Data\\trainer-cache\\installed\\fixture',trainerGameName:'fixture',sha256:'abc'}}));fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};await t.openOrSearchGameTrainer('fixture');assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,1)});
await check('hash mismatch and missing install both prevent launch',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a,{sha256:'different'});return{ok:true}};await assert.rejects(t.openOrSearchGameTrainer('fixture'),/文件已改变/);assert.equal(fx.launches.length,0)});
await check('automatic watcher respects disabled settings and stays silent',async()=>{const{auto,fx}=fixture();fx.enabled=false;const stop=auto.startGameTrainerAutoPrepare();fx.gameListener({pid:1,processCreated:'134365281498356567',title:'fixture',path:'D:\\game.exe'});await drain();await fx.advance(3000);assert.equal(fx.workers.length,0);fx.enabled=true;fx.emit('ui-settings:changed');await fx.advance(3000);assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,0);await fx.advance(3000);assert.equal(fx.workers.length,1);stop()});
await check('automatic watcher rejects transient game and unloads timers',async()=>{const{auto,fx}=fixture();const stop=auto.startGameTrainerAutoPrepare();await drain();fx.gameListener({pid:1,processCreated:'134365281498356567',title:'fixture',path:'D:\\game.exe'});fx.gameListener(null);await fx.advance(3000);assert.equal(fx.workers.length,0);stop();assert.equal(fx.gameListener,null);assert.equal(fx.timers.size,0)});
await check('unreadable settings do not enable background downloads',async()=>{const{auto,fx}=fixture();fx.settingsError=true;const stop=auto.startGameTrainerAutoPrepare();fx.gameListener({pid:1,processCreated:'134365281498356567',title:'fixture',path:'D:\\game.exe'});await drain();await fx.advance(3000);assert.equal(fx.workers.length,0);stop()});
await check('same display title with different executables has separate jobs and state',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};await Promise.all([t.prepareGameTrainer('Game',{processPath:'D:\\A\\Game.exe'}),t.prepareGameTrainer('Game',{processPath:'D:\\B\\Game.exe'})]);assert.equal(fx.workers.length,2);assert.equal(t.getGameTrainerState('Game'),null);assert.equal(t.getGameTrainerState('Game','D:\\A\\Game.exe').state,'ready');assert.equal(t.trainerTaskKey('Game','d:/a/Game.exe'),t.trainerTaskKey('Game','D:\\A\\game.exe'))});
await check('catalog ready receipt needs no executable and never launches',async()=>{const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{respond(fx,a,{ok:true,state:'ready',catalog:{available:true},retryAfter:fx.now+86400000});return{ok:true}};const result=await t.refreshGameTrainerData();assert.equal(result.state,'ready');assert.equal(argument(fx.workers[0],'-Mode'),'RefreshData');assert.equal(fx.launches.length,0);assert.equal(t.getGameTrainerState('YMCC Trainer Catalog'),null)});
await check('quiet catalog checks work with no running game and respect toggle',async()=>{const{auto,fx}=fixture();fx.hidden=async(p,a)=>{respond(fx,a,{ok:true,state:'ready',retryAfter:fx.now+86400000});return{ok:true}};const stop=auto.startGameTrainerAutoPrepare();await drain();await fx.advance(30000);assert.equal(fx.workers.length,1);await fx.advance(10000);assert.equal(fx.workers.length,1);fx.enabled=false;fx.emit('ui-settings:changed');await fx.advance(86400000);assert.equal(fx.workers.length,1);stop();assert.equal(fx.timers.size,0)});
await check('catalog check defers behind game preparation',async()=>{const{trainer:t,fx}=fixture();let finish;fx.hidden=async(p,a)=>{await new Promise(r=>finish=r);respond(fx,a,{ok:false,state:'unavailable'});return{ok:true}};const game=t.prepareGameTrainer('Game');await drain();const data=await t.refreshGameTrainerData();assert.equal(fx.workers.length,1);assert.ok(data.retryAfter>fx.now);finish();await game});
await check('structured launch failure is reported to the player',async()=>{const{trainer:t,fx}=fixture();fx.executeOk=false;fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};await assert.rejects(t.openOrSearchGameTrainer('fixture'),/launch rejected/)});

await check('invalid ready receipt is never published as ready to UI observers',async()=>{
 const{trainer:t,fx}=fixture(),seen=[];t.subscribeGameTrainerState(()=>seen.push(t.getGameTrainerState('fixture','D:\\game.exe')?.state));
 fx.hidden=async(p,a)=>{const r=ready(fx,a);fx.files.delete(r.trainerPath);return{ok:true}};
 await assert.rejects(t.prepareGameTrainer('fixture',{processPath:'D:\\game.exe'}),/安装核验/);
 assert.equal(seen.includes('ready'),false);assert.equal(t.getReadyGameTrainer('fixture','D:\\game.exe'),null);
});
async function preparedFixture(native=false){
 const f=fixture();f.fx.files.set('D:\\game.exe','game');f.fx.hidden=async(p,a)=>{ready(f.fx,a,native?{identity:{method:'native-window-title',steamAppId:'1361510',processId:42,processCreated:'134365281498356567',executablePath:'D:/game.exe',executableSha256:f.fx.hash,windowTitle:'fixture'}}:{});return{ok:true}};
 await f.trainer.prepareGameTrainer('fixture',{processPath:'D:\\game.exe',automatic:true});return f;
}
await check('read-only ready query requires exact EXE and has no worker or launch side effects',async()=>{
 const{trainer:t,fx}=await preparedFixture();assert.ok(t.getReadyGameTrainer('fixture','D:/GAME.exe'));
 for(const [n,p]of [['other','D:\\game.exe'],['fixture','D:\\other.exe'],['fixture',''],['fixture','game.exe']])assert.equal(t.getReadyGameTrainer(n,p),null);
 const copy=t.getReadyGameTrainer('fixture','D:\\game.exe');copy.path='wrong';assert.notEqual(t.getReadyGameTrainer('fixture','D:\\game.exe').path,'wrong');
 assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,0);
});
await check('ready-only launch rejects all unavailable states without preparing or downloading',async()=>{
 for(const state of ['queued','searching','downloading','unavailable','failed','cancelled','budget-limited']){
  const{trainer:t,fx}=fixture();let pending;
  if(['queued','searching','downloading'].includes(state)){
   fx.hidden=async(p,a)=>{respond(fx,a,{ok:false,state});return{ok:true}};pending=t.prepareGameTrainer('fixture',{processPath:'D:\\game.exe'});pending.catch(()=>{});await drain();
  }else{fx.hidden=async(p,a)=>{respond(fx,a,{ok:false,state});return{ok:true}};await t.prepareGameTrainer('fixture',{processPath:'D:\\game.exe'}).catch(()=>{})}
  const workers=fx.workers.length;assert.equal(t.getReadyGameTrainer('fixture','D:\\game.exe'),null);
  await assert.rejects(t.launchReadyGameTrainer('fixture','D:\\game.exe',{isTargetCurrent:async()=>true}),/未找到/);
  assert.equal(fx.workers.length,workers);assert.equal(fx.launches.length,0);
  if(pending){await t.cancelGameTrainerPreparation('fixture','D:\\game.exe');await fx.advance(400);await pending.catch(()=>{})}
 }
});
await check('ready-only concurrent clicks launch once without a new preparation worker',async()=>{
 const{trainer:t,fx}=await preparedFixture();const options={isTargetCurrent:async()=>true};
 const a=t.launchReadyGameTrainer('fixture','D:\\game.exe',options),b=t.launchReadyGameTrainer('fixture','D:\\game.exe',options);
 assert.equal(a,b);assert.equal((await a).action,'opened');await b;assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,1);
});
await check('ready-only click rejects missing game EXE and unguarded target before worker creation',async()=>{
 for(const kind of ['exe','unguarded','switched']){
  const{trainer:t,fx}=await preparedFixture();const local=t.getReadyGameTrainer('fixture','D:\\game.exe');
  if(kind==='exe')fx.files.delete('D:\\game.exe');if(kind==='trainer')fx.files.delete(local.path);if(kind==='hash')fx.hash='b'.repeat(64);
  const options=kind==='unguarded'?{}:{isTargetCurrent:async()=>kind!=='switched'};
  await assert.rejects(t.launchReadyGameTrainer('fixture','D:\\game.exe',options));assert.equal(fx.launches.length,0);assert.equal(fx.workers.length,1);
 }
});
await check('ready-only launch checks target again immediately before process creation',async()=>{
 const{trainer:t,fx}=await preparedFixture();let calls=0;
 await assert.rejects(t.launchReadyGameTrainer('fixture','D:\\game.exe',{isTargetCurrent:async()=>++calls===1}),/已退出或更换/);
 assert.equal(calls,2);assert.equal(fx.launches.length,0);assert.equal([...fx.files.keys()].some(p=>/requests\\launch-/.test(p)),false);
});
await check('ready-only launch rejects revoked readiness and cancellation before process creation',async()=>{
 for(const kind of ['state','cancel']){
  const{trainer:t,fx}=await preparedFixture();let calls=0,cancelled=false;
  await assert.rejects(t.launchReadyGameTrainer('fixture','D:\\game.exe',{isCancelled:()=>cancelled,isTargetCurrent:async()=>{
   if(++calls===2){if(kind==='cancel')cancelled=true;else{fx.hidden=async(p,a)=>{respond(fx,a,{state:'unavailable',ok:false});return{ok:true}};await t.prepareGameTrainer('fixture',{processPath:'D:\\game.exe'})}}return true;
  }}),kind==='state'?/就绪状态已改变/:/取消/);assert.equal(fx.launches.length,0);
 }
});
await check('ready-only launch receipt failures remain visible and do not start preparation',async()=>{
 const{trainer:t,fx}=await preparedFixture();fx.executeOk=false;
 await assert.rejects(t.launchReadyGameTrainer('fixture','D:\\game.exe',{isTargetCurrent:async()=>true}),/launch rejected/);
 assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,1);
});

await check('confirmed readiness requires Steam/custom-library evidence, not title or legacy EXE fallback',async()=>{
 for(const method of ['exact-title','exe-binding','unknown','steam-appid','steam-manifest','custom-library-appid','custom-library-install-metadata']){
  const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a,{identity:{method,steamAppId:'12345',manifestPath:'D:/steam/appmanifest_12345.acf',librarySourcePath:'D:/library/plan.json'}});return{ok:true}};
  await t.prepareGameTrainer('fixture',{processPath:'D:/Game.exe',automatic:true});
  assert.equal(!!t.getReadyGameTrainer('fixture','D:/Game.exe'),!['exact-title','exe-binding','unknown'].includes(method));
 }
 for(const identity of [undefined,{method:'steam-appid'},{method:'steam-appid',steamAppId:'12345'},{method:'custom-library-appid',steamAppId:'12345',librarySourcePath:'relative.json'}]){
  const{trainer:t,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a,{identity});return{ok:true}};await t.prepareGameTrainer('fixture',{processPath:'D:/Game.exe'});
  assert.equal(t.getReadyGameTrainer('fixture','D:/Game.exe'),null);
  await assert.rejects(t.launchReadyGameTrainer('fixture','D:/Game.exe',{isTargetCurrent:async()=>true}),/未找到/);assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,0);
 }
});
await check('exposed identity snapshots cannot mutate the internal confirmed evidence',async()=>{
 const{trainer:t}=await preparedFixture();const first=t.getGameTrainerState('fixture','D:/game.exe');first.identity.method='exact-title';first.identity.aliases[0]='wrong';
 const ready=t.getReadyGameTrainer('fixture','D:/game.exe');assert.equal(ready.identity.method,'steam-appid');assert.equal(ready.identity.aliases[0],'fixture');
 ready.identity.method='exact-title';assert.ok(t.getReadyGameTrainer('fixture','D:/game.exe'));
});
function newSource(fx){fx.generation++;fx.powerDir='E:/New/PowerControl'.replaceAll('/',String.fromCharCode(92));fx.dataDir='E:/NewData'.replaceAll('/',String.fromCharCode(92));fx.files.set('E:/New/PowerControl/YeManTrainerPrepare.ps1'.replaceAll('/',String.fromCharCode(92)),'worker');fx.files.set('E:/New/PowerControl/YeManTrainerLaunch.ps1'.replaceAll('/',String.fromCharCode(92)),'worker')}
await check('source generation clears readiness, notifies observers and re-resolves runtime paths',async()=>{
 const{trainer:t,fx}=await preparedFixture();let changed=0;t.subscribeGameTrainerState(()=>changed++);newSource(fx);
 assert.equal(t.getReadyGameTrainer('fixture','D:/game.exe'),null);assert.equal(changed,1);
 await assert.rejects(t.launchReadyGameTrainer('fixture','D:/game.exe',{isTargetCurrent:async()=>true}),/未找到/);
 assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,0);
 await t.prepareGameTrainer('fixture',{processPath:'D:/game.exe'});assert.ok(t.getReadyGameTrainer('fixture','D:/game.exe'));
 assert.equal(argument(fx.workers[1],'-File').replaceAll(String.fromCharCode(92),'/'),'E:/New/PowerControl/YeManTrainerPrepare.ps1');
 assert.equal(argument(fx.workers[1],'-CacheRoot').replaceAll(String.fromCharCode(92),'/'),'E:/NewData/trainer-cache');
});
await check('late preparation after a source switch never restores the old ready state',async()=>{
 const{trainer:t,fx}=fixture();let finish;fx.hidden=async(p,a)=>{await new Promise(r=>finish=r);ready(fx,a);return{ok:true}};
 const old=t.prepareGameTrainer('fixture',{processPath:'D:/game.exe'}),rejected=assert.rejects(old,/取消/);await drain();newSource(fx);assert.equal(t.getReadyGameTrainer('fixture','D:/game.exe'),null);
 finish();await rejected;assert.equal(t.getGameTrainerState('fixture','D:/game.exe'),null);
 fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};await t.prepareGameTrainer('fixture',{processPath:'D:/game.exe'});assert.ok(t.getReadyGameTrainer('fixture','D:/game.exe'));
});
await check('late runtime path discovery cannot overwrite the new source path promise',async()=>{
 const{trainer:t,fx}=fixture();let resolve;fx.resolvePowerDir=()=>new Promise(r=>resolve=r);
 const old=t.prepareGameTrainer('fixture',{processPath:'D:/game.exe'}),rejected=assert.rejects(old,/取消/);await drain();newSource(fx);t.getGameTrainerState('fixture','D:/game.exe');fx.resolvePowerDir=null;
 await t.openGameTrainerManager();resolve('D:/Old/PowerControl');await rejected;
 fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};await t.prepareGameTrainer('fixture',{processPath:'D:/game.exe'});
 assert.equal(argument(fx.workers[0],'-File').replaceAll(String.fromCharCode(92),'/'),'E:/New/PowerControl/YeManTrainerPrepare.ps1');
});
await check('game validation and worker file failures retire readiness without downloading',async()=>{
 for(const kind of ['game','trainer','hash','read-error']){
  const{trainer:t,fx}=await preparedFixture(true);let changed=0;t.subscribeGameTrainerState(()=>changed++);
  if(kind==='game')fx.files.delete('D:'+String.fromCharCode(92)+'game.exe');
  if(kind==='trainer'){fx.executeOk=false;fx.launchReceipt={code:'trainer-unavailable'}}
  if(kind==='hash')fx.hash='b'.repeat(64);if(kind==='read-error')fx.resolveHash=async()=>{throw Error('read denied')};
  await assert.rejects(t.launchReadyGameTrainer('fixture','D:/game.exe',{isTargetCurrent:async()=>true}));
  assert.equal(t.getReadyGameTrainer('fixture','D:/game.exe'),null);assert.equal(changed,1);assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,kind==='trainer'?1:0);
 }
});
await check('late failed hash check cannot invalidate a newer source or preparation result',async()=>{
 for(const switchSource of [false,true]){
  const{trainer:t,fx}=await preparedFixture(true);let fail;fx.resolveHash=()=>new Promise((_,reject)=>fail=reject);
  const old=t.launchReadyGameTrainer('fixture','D:'+String.fromCharCode(92)+'game.exe',{isTargetCurrent:async()=>true}),rejected=assert.rejects(old,/read denied/);await drain();
  if(switchSource){newSource(fx);t.getGameTrainerState('fixture','D:/game.exe')}
  fx.resolveHash=null;await t.prepareGameTrainer('fixture',{processPath:'D:/game.exe'});assert.ok(t.getReadyGameTrainer('fixture','D:/game.exe'));
  fail(Error('read denied'));await rejected;assert.ok(t.getReadyGameTrainer('fixture','D:/game.exe'));assert.equal(fx.launches.length,0);
 }
});
await check('source switch during a ready-only launch prevents process creation',async()=>{
 const{trainer:t,fx}=await preparedFixture(true);let finish;fx.resolveHash=()=>new Promise(resolve=>finish=resolve);
 const old=t.launchReadyGameTrainer('fixture','D:'+String.fromCharCode(92)+'game.exe',{isTargetCurrent:async()=>true}),rejected=assert.rejects(old,/取消/);await drain();
 newSource(fx);t.getGameTrainerState('fixture','D:/game.exe');finish(fx.hash);await rejected;assert.equal(fx.launches.length,0);assert.equal(fx.workers.length,1);
});
await check('source switch resets automatic retry delay and ignores the old completion',async()=>{
 const{trainer:t,auto,fx}=fixture();let finish;fx.hidden=async(p,a)=>{await new Promise(r=>finish=r);ready(fx,a);return{ok:true}};
 const stop=auto.startGameTrainerAutoPrepare();await drain();const target={pid:42,processCreated:'134365281498356567',name:'fixture',path:'D:/game.exe'};fx.gameListener(target);await fx.advance(2500);assert.equal(fx.workers.length,1);
 newSource(fx);t.getReadyGameTrainer('fixture',target.path);await fx.advance(2500);
 fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};finish();await drain();assert.equal(fx.workers.length,2);assert.ok(t.getReadyGameTrainer('fixture',target.path));assert.equal(fx.launches.length,0);stop();
});

await check('backend launch rejection retires a concurrently changed file but keeps an intact trainer',async()=>{
 for(const changed of [false,true]){
  const{trainer:t,fx}=await preparedFixture();fx.executeOk=false;const launch=fx.runHidden;
  fx.runHidden=async(p,a)=>{if(changed&&argument(a,'-File').endsWith('YeManTrainerLaunch.ps1'))fx.launchReceipt={code:'trainer-changed'};return launch(p,a)};
  await assert.rejects(t.launchReadyGameTrainer('fixture','D:/game.exe',{isTargetCurrent:async()=>true}),/launch rejected/);
  assert.equal(!!t.getReadyGameTrainer('fixture','D:/game.exe'),!changed);assert.equal(fx.workers.length,1);
 }
});

await check('semantic ampersands keep combined editions distinct from sequels in frontend task keys',async()=>{
 const{trainer:t,fx}=fixture();
 const combined='Dragon Quest I & II HD-2D Remake',sequel='DRAGON QUEST III HD-2D Remake';
 assert.notEqual(t.trainerGameKey(combined),t.trainerGameKey(sequel));
 for(const spelling of ['Dragon Quest I and II HD-2D Remake','Dragon Quest I &amp; II HD-2D Remake','Dragon Quest I &#38; II HD-2D Remake','Dragon Quest I &#x26; II HD-2D Remake'])assert.equal(t.trainerGameKey(combined),t.trainerGameKey(spelling));
 assert.notEqual(t.trainerTaskKey(combined,'D:/Game.exe'),t.trainerTaskKey(sequel,'D:/Game.exe'));
 fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};
 await t.prepareGameTrainer(combined,{processPath:'D:/Game.exe'});
 assert.ok(t.getReadyGameTrainer(combined,'D:/Game.exe'));
 assert.equal(t.getReadyGameTrainer(sequel,'D:/Game.exe'),null);
 assert.equal(fx.workers.length,1);assert.equal(fx.launches.length,0);
});

await check('automatic watcher forwards the same top game PID and creation time',async()=>{
 const{auto,fx}=fixture();const stop=auto.startGameTrainerAutoPrepare();await drain();
 fx.gameListener({pid:9132,processCreated:'134365281498356567',title:"Teenage Mutant Ninja Turtles: Shredder's Revenge",path:'D:/TMNT.exe'});await fx.advance(2500);
 assert.equal(fx.workers.length,1);assert.equal(argument(fx.workers[0],'-GameProcessId'),'9132');assert.equal(argument(fx.workers[0],'-GameProcessCreated'),'134365281498356567');stop();
});
await check('observed non-Steam window identity enables CT readiness and rejects another EXE/title',async()=>{
 const{trainer:t,fx}=fixture();const title="Teenage Mutant Ninja Turtles: Shredder's Revenge",game='D:'+String.fromCharCode(92)+'TMNT.exe';fx.files.set(game,'game');
 fx.hidden=async(p,a)=>{ready(fx,a,{trainerGameName:title,identity:{method:'native-window-title',steamAppId:'1361510',processId:9132,processCreated:'134365281498356567',executablePath:game,executableSha256:fx.hash,windowTitle:title},origin:'ct_other'});return{ok:true}};
 await t.prepareGameTrainer(title,{processPath:game,processTarget:{pid:9132,processCreated:'134365281498356567'}});
 assert.ok(t.getReadyGameTrainer(title,game));assert.equal(t.getReadyGameTrainer('TMNT',game),null);assert.equal(t.getReadyGameTrainer(title,'D:/Another.exe'),null);
 await t.launchReadyGameTrainer(title,game,{isTargetCurrent:async()=>true});assert.equal(fx.launches.length,1);assert.equal(fx.workers.length,1);
 assert.equal(argument(fx.launchArgs[0],'-GameProcessId'),'9132');assert.equal(argument(fx.launchArgs[0],'-GameProcessCreated'),'134365281498356567');assert.equal(argument(fx.launchArgs[0],'-ExpectedGameSha256'),fx.hash);
});
await check('full title alone cannot replace missing fields of a native window identity',async()=>{
 const identity={method:'native-window-title',steamAppId:'1361510',processId:9132,processCreated:'134365281498356567',executablePath:'D:/TMNT.exe',executableSha256:'a'.repeat(64),windowTitle:'TMNT Full Game'};
 for(const field of ['processId','processCreated','executablePath','executableSha256','windowTitle']){
  const{trainer:t,fx}=fixture();const incomplete={...identity};delete incomplete[field];fx.hidden=async(p,a)=>{ready(fx,a,{identity:incomplete});return{ok:true}};
  await t.prepareGameTrainer('TMNT Full Game',{processPath:'D:/TMNT.exe'});assert.equal(t.getReadyGameTrainer('TMNT Full Game','D:/TMNT.exe'),null,field);
 }
});
await check('changed non-Steam game executable retires confirmed readiness before CT launch',async()=>{
 const{trainer:t,fx}=fixture();const game='D:'+String.fromCharCode(92)+'TMNT.exe';fx.files.set(game,'game');
 fx.hidden=async(p,a)=>{ready(fx,a,{identity:{method:'native-window-title',steamAppId:'1361510',processId:9132,processCreated:'134365281498356567',executablePath:game,executableSha256:'b'.repeat(64),windowTitle:'TMNT Full Game'}});return{ok:true}};
 await t.prepareGameTrainer('TMNT Full Game',{processPath:game});assert.ok(t.getReadyGameTrainer('TMNT Full Game',game));
 await assert.rejects(t.launchReadyGameTrainer('TMNT Full Game',game,{isTargetCurrent:async()=>true}),/游戏 EXE 已改变/);assert.equal(fx.launches.length,0);assert.equal(t.getReadyGameTrainer('TMNT Full Game',game),null);
});

await check('another PID in the native title receipt is rejected before observers can see ready',async()=>{
 const{trainer:t,fx}=fixture();const seen=[];t.subscribeGameTrainerState(()=>seen.push(t.getGameTrainerState('TMNT Full Game','D:/TMNT.exe')?.state));
 fx.hidden=async(p,a)=>{ready(fx,a,{identity:{method:'native-window-title',steamAppId:'1361510',processId:9999,processCreated:'134365281498356567',executablePath:'D:/TMNT.exe',executableSha256:fx.hash,windowTitle:'TMNT Full Game'}});return{ok:true}};
 await assert.rejects(t.prepareGameTrainer('TMNT Full Game',{processPath:'D:/TMNT.exe',processTarget:{pid:9132,processCreated:'134365281498356567'}}),/进程已改变/);
 assert.equal(seen.includes('ready'),false);assert.equal(t.getReadyGameTrainer('TMNT Full Game','D:/TMNT.exe'),null);assert.equal(fx.launches.length,0);
});
if(process.argv[2]){
 const corpus=JSON.parse(fs.readFileSync(process.argv[2],'utf8').replace(/^\uFEFF/,''));
 await check('PowerShell backend and frontend agree on every recorded name-key spelling',async()=>{
  const{trainer:t}=fixture();for(const row of corpus.nameKeys)assert.equal(t.trainerGameKey(row.input),row.key,row.input);
 });
}

await check('same path native readiness excludes a different PID or birth',async()=>{
 const {trainer:t,fx}=fixture(),name='TMNT Full Game',game='D:\\TMNT.exe';
 const target={pid:9132,processCreated:'134365281498356567'};
 fx.files.set(game,'game');fx.hidden=async(p,a)=>{ready(fx,a,{identity:{method:'native-window-title',steamAppId:'1361510',processId:target.pid,processCreated:target.processCreated,executablePath:game,executableSha256:fx.hash,windowTitle:name}});return{ok:true}};
 await t.prepareGameTrainer(name,{processPath:game,processTarget:target});
 assert.ok(t.getReadyGameTrainer(name,game,target));
 assert.equal(t.getReadyGameTrainer(name,game,{...target,pid:9133}),null);
 assert.equal(t.getReadyGameTrainer(name,game,{...target,processCreated:'134365281498356568'}),null);
 await assert.rejects(t.launchReadyGameTrainer(name,game,{processTarget:{...target,pid:9133},isTargetCurrent:async()=>true}));
 assert.equal(fx.launches.length,0);
});
await check('same path concurrent preparations never join a previous process instance',async()=>{
 const {trainer:t,fx}=fixture(),name='TMNT Full Game',game='D:\\TMNT.exe';
 let release;const seen=[];t.subscribeGameTrainerState(()=>seen.push(t.getGameTrainerState(name,game)?.identity?.processId));
 fx.hidden=async(p,a)=>{if(fx.workers.length===1)await new Promise(r=>release=r);ready(fx,a,{identity:{method:'native-window-title',steamAppId:'1361510',processId:Number(argument(a,'-GameProcessId')),processCreated:argument(a,'-GameProcessCreated'),executablePath:game,executableSha256:fx.hash,windowTitle:name}});return{ok:true}};
 const first=t.prepareGameTrainer(name,{processPath:game,processTarget:{pid:9132,processCreated:'134365281498356567'}});
 const firstResult=first.then(()=> 'ready',()=> 'cancelled');await drain();
 const second=t.prepareGameTrainer(name,{processPath:game,processTarget:{pid:9133,processCreated:'134365281498356568'}});
 release();await drain();assert.equal(await firstResult,'cancelled');
 const result=await second;assert.equal(result.identity.processId,9133);assert.equal(fx.workers.length,2);
 assert.equal(seen.includes(9132),false);assert.equal(t.getGameTrainerState(name,game).identity.processId,9133);
});
await check('automatic same game restart rechecks the new process before the old daily cooldown',async()=>{
 const {auto,fx}=fixture();fx.hidden=async(p,a)=>{ready(fx,a);return{ok:true}};
 const stop=auto.startGameTrainerAutoPrepare();await drain();
 fx.gameListener({title:'TMNT Full Game',name:'TMNT',path:'D:\\TMNT.exe',pid:9132,processCreated:'134365281498356567'});
 await fx.advance(2500);assert.equal(fx.workers.length,1);
 fx.gameListener({title:'TMNT Full Game',name:'TMNT',path:'D:\\TMNT.exe',pid:9133,processCreated:'134365281498356568'});
 await fx.advance(2500);assert.equal(fx.workers.length,2);
 assert.equal(argument(fx.workers[1],'-GameProcessId'),'9133');assert.equal(fx.launches.length,0);stop();
});


await check('shared top target identity accepts copyright titles without a second title recognizer',async()=>{
 const {trainer:t,fx}=fixture(),name='cyberpunk2077',game='D:\\Game\\Cyberpunk2077.exe',target={pid:35788,processCreated:'134365342855297510'};fx.files.set(game,'game');
 fx.hidden=async(p,a)=>{ready(fx,a,{identity:{method:'ymcc-game-target',gameName:name,snapshotPath:'D:/PowerControl/game-target.json',steamAppId:'1091500',processId:target.pid,processCreated:target.processCreated,executablePath:game,executableSha256:fx.hash,windowTitle:'Cyberpunk 2077 (C) 2020 by CD Projekt RED'}});return{ok:true}};
 await t.prepareGameTrainer(name,{processPath:game,processTarget:target});assert.ok(t.getReadyGameTrainer(name,game,target));
 await t.launchReadyGameTrainer(name,game,{processTarget:target,isTargetCurrent:async()=>true});assert.equal(fx.launches.length,1);assert.equal(argument(fx.launchArgs[0],'-ExpectedGameSha256'),fx.hash);
 assert.equal(t.getReadyGameTrainer(name,game,{...target,pid:35789}),null);
});
await check('shared top receipt requires its source snapshot and exact resolved label',async()=>{
 for(const change of [{snapshotPath:''},{gameName:'Cyberpunk 2078'}]){
  const {trainer:t,fx}=fixture(),name='cyberpunk2077',game='D:\\Game\\Cyberpunk2077.exe';
  fx.hidden=async(p,a)=>{ready(fx,a,{identity:{method:'ymcc-game-target',gameName:name,snapshotPath:'D:/PowerControl/game-target.json',steamAppId:'1091500',processId:35788,processCreated:'134365342855297510',executablePath:game,executableSha256:fx.hash,...change}});return{ok:true}};
  await t.prepareGameTrainer(name,{processPath:game});assert.equal(t.getReadyGameTrainer(name,game),null);assert.equal(fx.launches.length,0);
 }
});

await check('independent CE opens without a game or preparation and coalesces concurrent clicks',async()=>{
 const {trainer:t,fx}=fixture();
 const a=t.openCheatEngine(),b=t.openCheatEngine();assert.equal(a,b);await a;await b;
 assert.equal(fx.workers.length,0);assert.equal(fx.launchArgs.length,1);
 assert.equal(argument(fx.launchArgs[0],'-Mode'),'CheatEngine');
 for(const name of ['-GameExecutablePath','-GameProcessId','-GameProcessCreated','-TrainerPath'])assert.equal(fx.launchArgs[0].includes(name),false);
 await t.openCheatEngine();assert.equal(fx.launchArgs.length,2);
});
await check('independent CE failure clears the pending action and permits a later retry',async()=>{
 const {trainer:t,fx}=fixture();fx.executeOk=false;await assert.rejects(t.openCheatEngine(),/launch rejected/);
 fx.executeOk=true;await t.openCheatEngine();assert.equal(fx.launchArgs.length,2);assert.equal(fx.workers.length,0);
});
await check('moved GCM path reaches the worker and updates readiness from its verified receipt',async()=>{
 const{trainer:t,fx}=await preparedFixture();const before=t.getReadyGameTrainer('fixture','D:\\game.exe');fx.files.delete(before.path);
 fx.launchReceipt={trainerPath:'E:\\GCM\\renamed\\nested\\Game.exe',trainerFolder:'E:\\GCM\\renamed'};
 await t.launchReadyGameTrainer('fixture','D:\\game.exe',{isTargetCurrent:async()=>true});
 assert.equal(fx.workers.length,1);assert.equal(fx.launchArgs.length,1);assert.equal(t.getReadyGameTrainer('fixture','D:\\game.exe').path,fx.launchReceipt.trainerPath);
 assert.equal(argument(fx.launchArgs[0],'-TrainerGameName'),'fixture');assert.ok(argument(fx.launchArgs[0],'-AppDirectory'));
});
await check('worker file rejection invalidates readiness and persists exported error context',async()=>{
 const{trainer:t,fx}=await preparedFixture();fx.executeOk=false;fx.launchReceipt={code:'trainer-changed',error:'trainer entry changed',program:'E:\\GCM\\Game.exe'};
 await assert.rejects(t.launchReadyGameTrainer('fixture','D:\\game.exe',{isTargetCurrent:async()=>true}),/trainer entry changed/);
 assert.equal(t.getReadyGameTrainer('fixture','D:\\game.exe'),null);assert.equal(fx.workers.length,1);assert.match(fx.diagnostics[0].error,/E:.*Game.exe/);
});
function gameWorkerCount(fx){return fx.workers.filter(a=>!a.includes("RefreshData")).length}
await check('temporary shared target errors retry on the bounded backend delay instead of an hour',async()=>{
 const {auto,fx}=fixture();fx.hidden=async(_p,a)=>{respond(fx,a,gameWorkerCount(fx)===1?{state:'failed',ok:false,code:'shared-target-unavailable',retryable:true,retryAfter:fx.now+5000,message:'snapshot temporarily stale'}:{state:'unavailable',ok:false,retryAfter:fx.now+86400000});return{ok:true}};
 const stop=auto.startGameTrainerAutoPrepare();await drain();fx.gameListener({title:'TMNT Full Game',name:'TMNT',path:'D:/TMNT.exe',pid:9132,processCreated:'134365281498356567'});
 await fx.advance(2500);assert.equal(gameWorkerCount(fx),1);await fx.advance(5000);assert.equal(gameWorkerCount(fx),2);assert.equal(fx.launches.length,0);stop();
});
await check('bounded transient retries stop repeated process creation and preserve target switching',async()=>{
 const {auto,fx}=fixture();fx.hidden=async(_p,a)=>{respond(fx,a,{state:'failed',ok:false,code:'shared-target-unavailable',retryable:true,retryAfter:fx.now+5000});return{ok:true}};
 const stop=auto.startGameTrainerAutoPrepare();await drain();const game={title:'TMNT Full Game',name:'TMNT',path:'D:/TMNT.exe',pid:9132,processCreated:'134365281498356567'};fx.gameListener(game);
 for(let i=0;i<5;i++)await fx.advance(10000);assert.equal(gameWorkerCount(fx),4);await fx.advance(10000);assert.equal(gameWorkerCount(fx),4);
 fx.gameListener({...game,pid:9133,processCreated:'134365281498356568'});await fx.advance(2500);assert.equal(gameWorkerCount(fx),5);stop();
});
await check('startup worker transport failure retries with a bounded delay',async()=>{
 const {auto,fx}=fixture();fx.hidden=async(_p,a)=>{if(gameWorkerCount(fx)===1)throw Error('transient IPC failure');respond(fx,a,{state:'unavailable',ok:false,retryAfter:fx.now+86400000});return{ok:true}};
 const stop=auto.startGameTrainerAutoPrepare();await drain();fx.gameListener({title:'TMNT Full Game',name:'TMNT',path:'D:/TMNT.exe',pid:9132,processCreated:'134365281498356567'});await fx.advance(2500);await fx.advance(30000);assert.equal(gameWorkerCount(fx),2);stop();
});
await check('successful catalog refresh rechecks an unsupported game without waiting for its old cooldown',async()=>{
 const {auto,fx}=fixture();fx.hidden=async(_p,a)=>{respond(fx,a,a.includes('RefreshData')?{state:'ready',ok:true,catalog:{updated:true},retryAfter:fx.now+86400000}:{state:'unavailable',ok:false,code:'identity-unconfirmed',retryAfter:fx.now+86400000});return{ok:true}};
 const stop=auto.startGameTrainerAutoPrepare();await drain();fx.gameListener({title:'TMNT Full Game',name:'TMNT',path:'D:/TMNT.exe',pid:9132,processCreated:'134365281498356567'});await fx.advance(2500);assert.equal(gameWorkerCount(fx),1);
 await fx.advance(27500);await fx.advance(2500);assert.equal(gameWorkerCount(fx),2);assert.equal(fx.launches.length,0);stop();
});
await check('a late old-game automatic completion cannot postpone the new-game preparation timer',async()=>{
 const {auto,fx}=fixture();let finishOld,oldArgs;
 fx.hidden=async(_p,args)=>{if(gameWorkerCount(fx)===1){oldArgs=args;return new Promise(resolve=>{finishOld=resolve;});}respond(fx,args,{state:'unavailable',ok:false,retryAfter:fx.now+86400000});return{ok:true};};
 const stop=auto.startGameTrainerAutoPrepare();await drain();
 const first={title:'TMNT Full Game',name:'TMNT',path:'D:/TMNT.exe',pid:9132,processCreated:'134365281498356567',identityGeneration:10};
 fx.gameListener(first);await fx.advance(2500);assert.equal(gameWorkerCount(fx),1);assert.equal(typeof finishOld,'function');
 await fx.advance(1000);fx.gameListener({...first,title:'Bluey',name:'Bluey',path:'D:/Bluey.exe',pid:9133,identityGeneration:11});
 await fx.advance(1000);respond(fx,oldArgs,{state:'unavailable',ok:false,retryAfter:fx.now+86400000});finishOld({ok:true});await drain();
 await fx.advance(1500);assert.equal(gameWorkerCount(fx),2,'B keeps its original 2500ms preparation deadline');assert.equal(fx.launches.length,0);stop();
});
await check('config storm rejects string truthy launch acknowledgement',async()=>{
 const {trainer:t,fx}=fixture();fx.launchReceipt={ok:'false'};
 await assert.rejects(t.openCheatEngine());
});
await check('config storm rejects string truthy ready acknowledgement before publishing ready',async()=>{
 const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a,{ok:'false'});return{ok:true}};
 await assert.rejects(t.prepareGameTrainer('Victoria 3'));
 assert.notEqual(t.getGameTrainerState('Victoria 3')?.state,'ready');
});
await check('config storm future cache document never becomes installed readiness',async()=>{
 const {trainer:t,fx}=fixture();const a=['-RequestId','old-cache-fixture','-GameName','Victoria 3','-ResultPath','old.json'];
 const entry=ready(fx,a);fx.files.set('D:\\Data\\trainer-cache\\trainers.json',JSON.stringify({schemaVersion:99,victoria3:entry}));
 assert.equal(await t.findInstalledGameTrainer('Victoria 3'),null);
});
await check('config storm false cached ok cannot be rewritten into a successful installed result',async()=>{
 const {trainer:t,fx}=fixture();const entry=ready(fx,['-RequestId','old-cache-fixture','-GameName','Victoria 3','-ResultPath','old.json'],{ok:false});
 fx.files.set('D:\\Data\\trainer-cache\\trainers.json',JSON.stringify({victoria3:entry}));
 assert.equal(await t.findInstalledGameTrainer('Victoria 3'),null);
});
await check('config storm old generation launch completion cannot acknowledge the replacement session',async()=>{
 const {trainer:t,fx}=fixture();let finish;fx.runHidden=async(_p,a)=>new Promise(resolve=>{finish=()=>{fx.files.set(argument(a,'-ResultPath'),JSON.stringify({requestId:argument(a,'-RequestId'),ok:true}));resolve({ok:true})}});
 const result=t.openCheatEngine().then(()=>false,()=>true);await drain();assert.equal(typeof finish,'function');fx.generation++;finish();
 assert.equal(await result,true);
});
await check('config storm corrupt empty array wrong-type and oversized cache uses null with no worker writes',async()=>{
 for(const raw of ['', '[]','null','false','{"victoria3":', '{"version":"one"}', ' '.repeat((8<<20)+1), '{"schema":1e999}']){
  const {trainer:t,fx}=fixture();const index='D:\\Data\\trainer-cache\\trainers.json';fx.files.set(index,raw);
  assert.equal(await t.findInstalledGameTrainer('Victoria 3'),null);assert.equal(fx.files.get(index),raw);assert.equal(fx.workers.length,0);assert.equal(fx.launches.length,0);
 }
});
await check('config storm BOM legal cache retains unknown data and validates hash before installed result',async()=>{
 const {trainer:t,fx}=fixture();const entry=ready(fx,['-RequestId','cached-fixture','-GameName','Victoria 3','-ResultPath','old.json'],{extension:{keep:[1,{value:true}]}});
 const index='D:\\Data\\trainer-cache\\trainers.json';const raw='\uFEFF'+JSON.stringify({victoria3:entry});fx.files.set(index,raw);
 assert.equal((await t.findInstalledGameTrainer('Victoria 3')).gameName,'Victoria 3');assert.equal(fx.files.get(index),raw);assert.equal(fx.workers.length,0);
 fx.resolveHash=async()=> 'b'.repeat(64);assert.equal(await t.findInstalledGameTrainer('Victoria 3'),null);
});
await check('config storm cache IO refusal returns unknown and healthy explicit read can recover',async()=>{
 const {trainer:t,fx}=fixture();const entry=ready(fx,['-RequestId','cached-fixture','-GameName','Victoria 3','-ResultPath','old.json']);const index='D:\\Data\\trainer-cache\\trainers.json';fx.files.set(index,JSON.stringify({victoria3:entry}));
 fx.readError=true;assert.equal(await t.findInstalledGameTrainer('Victoria 3'),null);fx.readError=false;assert.ok(await t.findInstalledGameTrainer('Victoria 3'));assert.equal(fx.workers.length,0);
});
await check('config storm pending false success starts no fake worker and explicit retry succeeds',async()=>{
 const {trainer:t,fx}=fixture();fx.falseWrite=true;await assert.rejects(t.prepareGameTrainer('Victoria 3'));assert.equal(fx.workers.length,0);
 fx.falseWrite=false;fx.hidden=async(_p,a)=>{ready(fx,a);return{ok:true}};assert.equal((await t.prepareGameTrainer('Victoria 3')).state,'ready');
});
await check('config storm request read IO refusal preserves unconfirmed receipt and permits a new explicit request',async()=>{
 const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a);fx.readError=true;return{ok:true}};await assert.rejects(t.prepareGameTrainer('Victoria 3'));
 assert.notEqual(t.getGameTrainerState('Victoria 3')?.state,'ready');fx.readError=false;fx.hidden=async(_p,a)=>{ready(fx,a);return{ok:true}};
 assert.equal((await t.prepareGameTrainer('Victoria 3')).ok,true);assert.equal(fx.workers.length,2);assert.equal(fx.launches.length,0);
});
await check('config storm ready known field corruption cannot mark download or readiness success',async()=>{
 for(const extra of [{downloaded:'true'},{retryable:'true'},{bytesDownloaded:'123'},{retryAfter:[]},{trainerPath:[]},{origin:{}},{libraryRoot:[]},{checkedAt:'now'},{downloaded:true,downloadOutcome:'reused-local'},{downloaded:false,downloadOutcome:'downloaded'},{version:2}]){
  const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a,extra);return{ok:true}};await assert.rejects(t.prepareGameTrainer('Victoria 3'));assert.notEqual(t.getGameTrainerState('Victoria 3')?.state,'ready');assert.equal(fx.launches.length,0);
 }
});
await check('config storm PID and birth mismatch never publishes bound readiness',async()=>{
 for(const extra of [{gameProcessId:43},{gameProcessCreated:'99999'}]){
  const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a,extra);return{ok:true}};
  await assert.rejects(t.prepareGameTrainer('Victoria 3',{processPath:'D:\\game.exe',processTarget:{pid:42,processCreated:'12345'}}));assert.notEqual(t.getGameTrainerState('Victoria 3','D:\\game.exe')?.state,'ready');
 }
});
await check('config storm nested unknown receipts are detached from observers and subsequent readers',async()=>{
 const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a,{extension:{list:[{keep:true}]}});return{ok:true}};
 const result=await t.prepareGameTrainer('Victoria 3',{onProgress:p=>{if(p.extension)p.extension.list[0].keep=false}});assert.equal(result.extension.list[0].keep,true);
 const cached=t.getGameTrainerState('Victoria 3');assert.equal(cached.extension.list[0].keep,true);cached.extension.list[0].keep=false;assert.equal(t.getGameTrainerState('Victoria 3').extension.list[0].keep,true);
});
await check('config storm launch pending wrong type cannot acknowledge success and later explicit launch recovers',async()=>{
 const {trainer:t,fx}=fixture();fx.launchReceipt={pending:'false'};await assert.rejects(t.openCheatEngine());fx.launchReceipt={pending:false};await t.openCheatEngine();assert.equal(fx.launches.length,2);
});
await check('config storm cancelled old ready receipt cannot replace a newer request instance',async()=>{
 const {trainer:t,fx}=fixture();let first;fx.hidden=async(_p,a)=>{if(!first){first=a;respond(fx,a,{state:'downloading',ok:false});return{ok:true}}ready(fx,a);return{ok:true}};
 const old=t.prepareGameTrainer('Victoria 3');old.catch(()=>{});await drain();await t.cancelGameTrainerPreparation('Victoria 3');const next=t.prepareGameTrainer('Victoria 3');
 ready(fx,first,{downloaded:true,downloadOutcome:'downloaded'});await fx.advance(400);await assert.rejects(old);assert.equal((await next).state,'ready');assert.notEqual(t.getGameTrainerState('Victoria 3').requestId,argument(first,'-RequestId'));
});
await check('config storm corrupt request buffers reject without polling and a fresh explicit request recovers',async()=>{
 for(const raw of ['', 'null', '[]', 'false', '"text"', '{"requestId":', '{"x":1e999}', '{"x":"\ud800"}', ' '.repeat(65537)]){
  const {trainer:t,fx}=fixture();let resultPath;
  fx.hidden=async(_p,a)=>{resultPath=argument(a,'-ResultPath');fx.files.set(resultPath,raw);return{ok:true}};
  const pending=t.prepareGameTrainer('Victoria 3');let settled=false;pending.then(()=>settled=true,()=>settled=true);
  await fx.advance(5001);assert.equal(settled,true,'corrupt request buffer kept the worker queue waiting');
  await assert.rejects(pending);assert.equal(fx.files.get(resultPath),raw,'frontend overwrote unconfirmed corrupt receipt');
  assert.notEqual(t.getGameTrainerState('Victoria 3')?.state,'ready');assert.equal(fx.launches.length,0);
  fx.hidden=async(_p,a)=>{ready(fx,a);return{ok:true}};assert.equal((await t.prepareGameTrainer('Victoria 3')).ok,true);
  const {trainer:launch,fx:launchFx}=fixture();const healthyLaunch=launchFx.runHidden;let launchPath;
  launchFx.runHidden=async(_p,a)=>{launchPath=argument(a,'-ResultPath');launchFx.files.set(launchPath,raw);return{ok:true}};
  const launchPending=launch.openCheatEngine();let launchSettled=false;launchPending.then(()=>launchSettled=true,()=>launchSettled=true);
  await launchFx.advance(5001);assert.equal(launchSettled,true,'corrupt launch buffer kept the UI waiting');await assert.rejects(launchPending);
  assert.equal(launchFx.files.get(launchPath),raw,'frontend overwrote unconfirmed launch receipt');
  launchFx.runHidden=healthyLaunch;await launch.openCheatEngine();
 }
});
await check('config storm restore hash timeout returns unknown and permits explicit recovery without workers',async()=>{
 const {trainer:t,fx}=fixture();const path='D:/game.exe',target={pid:42,processCreated:'12345'};
 const entry=ready(fx,['-RequestId','persisted','-GameName','Victoria 3','-ResultPath','old.json','-GameProcessId','42','-GameProcessCreated','12345','-GameExecutablePath',path]);
 const index=['D:','Data','trainer-cache','trainers.json'].join(String.fromCharCode(92));fx.files.set(index,JSON.stringify({victoria3:entry}));const raw=fx.files.get(index);
 fx.resolveHash=()=>new Promise(()=>{});let settled=false;
 const pending=t.restoreReadyGameTrainer('Victoria 3',path,{processTarget:target,isTargetCurrent:()=>true});pending.then(()=>settled=true);
 await fx.advance(5001);assert.equal(settled,true,'restore is stuck awaiting the hash IPC');assert.equal(await pending,null);
 assert.equal(t.getReadyGameTrainer('Victoria 3',path,target),null);assert.equal(fx.files.get(index),raw);
 fx.resolveHash=null;assert.ok(await t.restoreReadyGameTrainer('Victoria 3',path,{processTarget:target,isTargetCurrent:()=>true}));
 assert.equal(fx.workers.length,0);assert.equal(fx.launches.length,0);
});
await check('config storm worker metadata timeout starts no worker and explicit retry remains possible',async()=>{
 for(const hook of ['resolveExists','resolveMkdir']){
  const {trainer:t,fx}=fixture();fx[hook]=()=>new Promise(()=>{});const pending=t.prepareGameTrainer('Victoria 3');let settled=false;
  pending.then(()=>settled=true,()=>settled=true);await fx.advance(5001);
  assert.equal(settled,true,hook+' left preparation waiting forever');await assert.rejects(pending);assert.equal(fx.workers.length,0);
  fx[hook]=null;fx.hidden=async(_p,a)=>{ready(fx,a);return{ok:true}};assert.equal((await t.prepareGameTrainer('Victoria 3')).ok,true);
 }
});
await check('config storm cleanup refusal cannot hold a confirmed result or block a later explicit request',async()=>{
 const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a);return{ok:true}};fx.resolveRemove=()=>new Promise(()=>{});
 const pending=t.prepareGameTrainer('Victoria 3');let settled=false;pending.then(()=>settled=true,()=>settled=true);
 await fx.advance(5001);await fx.advance(5001);assert.equal(settled,true,'best effort receipt cleanup held the worker queue');
 assert.equal((await pending).ok,true);fx.resolveRemove=null;assert.equal((await t.prepareGameTrainer('Victoria 3')).ok,true);assert.equal(fx.launches.length,0);
});
await check('config storm legal game names Version and Schema remain cache entries without hiding future schemas',async()=>{
 for(const title of ['Version','Schema']){
  const {trainer:t,fx}=fixture();const entry=ready(fx,['-RequestId','persisted','-GameName',title,'-ResultPath','old.json']);
  const index=['D:','Data','trainer-cache','trainers.json'].join(String.fromCharCode(92));const raw=JSON.stringify({[t.trainerGameKey(title)]:entry});fx.files.set(index,raw);
  assert.equal((await t.findInstalledGameTrainer(title))?.gameName,title,'legal game title collided with root schema field');
  assert.equal(fx.files.get(index),raw);assert.equal(fx.workers.length,0);assert.equal(fx.launches.length,0);
  fx.files.set(index,JSON.stringify({schemaVersion:99,[t.trainerGameKey(title)]:entry}));assert.equal(await t.findInstalledGameTrainer(title),null);
 }
});
// The actual backend writes v2 ready index entries before a worker reply adds ok.
const tmntPath=path.join(root,'tools/fixtures/trainer_ready_tmnt_20261022.json');
const tmntBytes=fs.readFileSync(tmntPath);actualInputs.set(tmntPath,{path:tmntPath,sha256:digest(tmntBytes)});
const tmnt=JSON.parse(tmntBytes.toString('utf8'));
function storedTmnt(patch=()=>{}){
 const {trainer:t,fx}=fixture(),g=structuredClone(tmnt.game),index=structuredClone(tmnt.index),entry=Object.values(index)[0];
 for(const item of Object.values(index)){patch(item);if(typeof item.trainerPath==='string')fx.files.set(item.trainerPath,'inert trainer');}
 const file=['D:','Data','trainer-cache','trainers.json'].join(String.fromCharCode(92));const raw=JSON.stringify(index);fx.files.set(file,raw);fx.files.set(g.path,'inert game');
 const hashes=[];fx.resolveHash=async p=>{hashes.push(p);return p===g.path?entry.identity?.executableSha256:entry.sha256};
 const restore=()=>t.restoreReadyGameTrainer(g.title,g.path,{processTarget:g,isTargetCurrent:()=>true});
 return{t,fx,g,entry,file,raw,hashes,restore};
}
await check('config storm actual production v2 cache missing worker ok waits for both hashes before readiness',async()=>{
 const f=storedTmnt(item=>item.extension={kept:true}),calls=[];
 f.fx.resolveHash=p=>new Promise(resolve=>calls.push({p,resolve}));const pending=f.restore();await drain();
 assert.equal(calls.length,1);assert.equal(f.t.getReadyGameTrainer(f.g.title,f.g.path,f.g),null);assert.equal(f.t.getGameTrainerState(f.g.title,f.g.path),null);
 calls[0].resolve(f.entry.sha256);await drain();assert.equal(calls.length,2);assert.equal(calls[1].p,f.g.path);
 assert.equal(f.t.getReadyGameTrainer(f.g.title,f.g.path,f.g),null,'trainer hash alone must not acknowledge a bound cache');
 calls[1].resolve(f.entry.identity.executableSha256);assert.equal((await pending)?.path,f.entry.trainerPath);
 const state=f.t.getGameTrainerState(f.g.title,f.g.path);assert.equal(state.ok,true);assert.equal(state.downloaded,false);assert.equal(state.extension.kept,true);
 assert.equal(f.fx.files.get(f.file),f.raw);assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.timers.size,0);
});
await check('config storm stored ack failures future types and incomplete identities reject before hashing',async()=>{
 for(const patch of [x=>x.ok=false,x=>x.ok='true',x=>x.ok=null,x=>x.ok=0,x=>x.ok={},x=>x.ok=[],x=>x.storageVersion=3,x=>x.storageVersion='2',x=>x.version=2,x=>x.state='failed',x=>delete x.identity,x=>x.identity.processId++,x=>x.identity.processCreated='999',x=>delete x.identity.executableSha256,x=>delete x.identity.snapshotPath,x=>x.trainerPath=42]){
  const f=storedTmnt(patch);assert.equal(await f.restore(),null);assert.equal(f.hashes.length,0,'malformed or unbound entry reached hashing');assert.equal(f.t.getReadyGameTrainer(f.g.title,f.g.path,f.g),null);
  assert.equal(f.fx.files.get(f.file),f.raw);assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);
 }
 for(const failedSide of ['trainer','game']){
  const f=storedTmnt();f.fx.resolveHash=async p=>{f.hashes.push(p);return (p===f.g.path)===(failedSide==='game')?'0'.repeat(64):p===f.g.path?f.entry.identity.executableSha256:f.entry.sha256};
  assert.equal(await f.restore(),null);assert.equal(f.hashes.length,failedSide==='trainer'?1:2);assert.equal(f.t.getGameTrainerState(f.g.title,f.g.path),null);
 }
});
await check('config storm supported acknowledged cache forms remain compatible without a v2 only gate',async()=>{
 for(const version of [undefined,1,2]){
  const f=storedTmnt(x=>{x.ok=true;if(version===undefined)delete x.storageVersion;else x.storageVersion=version});
  assert.ok(await f.restore());assert.equal(f.hashes.length,2);assert.equal(f.fx.files.get(f.file),f.raw);assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);
 }
});
await check('config storm missing worker ok cannot use the stored v2 cache compatibility path',async()=>{
 const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{const result=ready(fx,a,{storageVersion:2});delete result.ok;fx.files.set(argument(a,'-ResultPath'),JSON.stringify(result));return{ok:true}};
 await assert.rejects(t.prepareGameTrainer('Victoria 3'));assert.notEqual(t.getGameTrainerState('Victoria 3')?.state,'ready');assert.equal(fx.launches.length,0);
});

await check('request binding rejects missing or mismatched token and renderer session before ready',async()=>{
 for(const extra of [{requestToken:'other'},{requestToken:undefined},{requestToken:null},{sourceSession:'other'},{sourceSession:undefined},{sourceSession:3},{requestProtocol:2}]){
  const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a,extra);return{ok:true}};
  await assert.rejects(t.prepareGameTrainer('fixture'),/身份|回执|请求/);assert.equal(t.getGameTrainerState('fixture').state,'failed');assert.equal(fx.launches.length,0);
 }
});
await check('request binding requires exact target PID birth and executable fields',async()=>{
 for(const extra of [{gameProcessId:43},{gameProcessId:undefined},{gameProcessCreated:'999'},{gameProcessCreated:undefined},{gameExecutablePath:'D:\\other.exe'},{gameExecutablePath:undefined}]){
  const {trainer:t,fx}=fixture();fx.hidden=async(_p,a)=>{ready(fx,a,extra);return{ok:true}};
  await assert.rejects(t.prepareGameTrainer('fixture',{processPath:'D:\\game.exe',processTarget:{pid:42,processCreated:'100'}}),/身份|回执|请求/);
  assert.equal(fx.launches.length,0);
 }
});
await check('launch success requires the current token session and target acknowledgement',async()=>{
 for(const extra of [{requestToken:'old'},{sourceSession:undefined},{gameProcessId:88},{gameProcessCreated:'other'},{gameExecutablePath:undefined}]){
  const {trainer:t,fx}=fixture();fx.launchReceipt=extra;
  await assert.rejects(t.openGameTrainerManager(),/身份|回执|请求/);
 }
});

if(actualProducerContracts.length){
 function fromActualProducer(contract,mutateIndex,hashFault){
  const {trainer:t,fx}=fixture(),index=JSON.parse(contract.indexRaw),normalize=p=>p.replaceAll('/',String.fromCharCode(92)).toLowerCase();
  if(mutateIndex)for(const entry of Object.values(index))if(entry?.gameName===contract.gameName&&entry.state==='ready')mutateIndex(entry);
  fx.dataDir=contract.dataDir;fx.files.set(contract.cacheRoot+'\\trainers.json',JSON.stringify(index));
  fx.files.set(contract.gamePath,'owned inert game bytes');fx.files.set(contract.trainerPath,'owned inert trainer bytes');
  const hashes=[];fx.resolveHash=async p=>{hashes.push(p);const game=normalize(p)===normalize(contract.gamePath);return hashFault?.(game,p)||(game?contract.gameSha256:contract.trainerSha256)};
  return {t,fx,hashes};
 }
 await check('actual PS5 and pwsh persisted producer cache restores only after exact target and both file hashes',async()=>{
  for(const {contract:c} of actualProducerContracts){
   const original=JSON.parse(c.indexRaw);assert.equal(Object.hasOwn(original.fixture,'ok'),false);assert.equal(Object.hasOwn(original.fixture,'requestToken'),false);
   const {t,fx,hashes}=fromActualProducer(c);
   const restored=await t.restoreReadyGameTrainer(c.gameName,c.gamePath,{processTarget:{pid:c.processId,processCreated:c.processCreated},isTargetCurrent:()=>true});
   assert(restored);assert.equal(restored.path,c.trainerPath);assert.equal(restored.sha256,c.trainerSha256);assert.equal(hashes.length,2);
   assert.equal(t.getReadyGameTrainer(c.gameName,c.gamePath,{pid:c.processId,processCreated:c.processCreated})?.path,c.trainerPath);
   assert.equal(t.getGameTrainerState(c.gameName,c.gamePath)?.downloaded,false);assert.equal(fx.workers.length,0);assert.equal(fx.launches.length,0);
   for(const mutate of [entry=>{entry.ok=false},entry=>{entry.storageVersion=3},entry=>{delete entry.identity.processId},entry=>{entry.identity.processCreated='999'},entry=>{entry.identity.steamAppId=12345}]){
    const bad=fromActualProducer(c,mutate);assert.equal(await bad.t.restoreReadyGameTrainer(c.gameName,c.gamePath,{processTarget:{pid:c.processId,processCreated:c.processCreated},isTargetCurrent:()=>true}),null);assert.equal(bad.hashes.length,0);assert.equal(bad.fx.workers.length,0);
   }
   for(const gameFault of [false,true]){
    const bad=fromActualProducer(c,undefined,game=>game===gameFault?'0'.repeat(64):null);
    assert.equal(await bad.t.restoreReadyGameTrainer(c.gameName,c.gamePath,{processTarget:{pid:c.processId,processCreated:c.processCreated},isTargetCurrent:()=>true}),null);
    assert.equal(bad.t.getGameTrainerState(c.gameName,c.gamePath),null);assert.equal(bad.fx.launches.length,0);
   }
  }
 });
 await check('actual old producer ready receipt cannot acknowledge a new renderer request',async()=>{
  for(const {contract:c} of actualProducerContracts){
   const {t,fx}=fromActualProducer(c);
   fx.hidden=async(_p,args)=>{fx.files.set(argument(args,'-ResultPath'),c.receiptRaw);return{ok:true}};
   const pending=t.prepareGameTrainer(c.gameName,{processPath:c.gamePath,processTarget:{pid:c.processId,processCreated:c.processCreated}});
   const rejected=assert.rejects(pending,/超时/);await drain();await fx.advance(200001);await rejected;
   assert.notEqual(t.getGameTrainerState(c.gameName,c.gamePath)?.state,'ready');assert.equal(fx.launches.length,0);assert.equal(fx.workers.length,1);
  }
 });
}

await check('identity producer text fields reject wrong JSON types before worker readiness',async()=>{
 for(const [key,value] of [['method',[]],['steamAppId',12345],['manifestPath',{}],['librarySourcePath',false],['executablePath',1],['executableSha256',[]],['windowTitle',0],['gameName',null],['snapshotPath',{}]]){
  const {trainer:t,fx}=fixture(),identity={method:'steam-appid',steamAppId:'12345',manifestPath:'D:/owned/appmanifest_12345.acf',[key]:value};
  fx.hidden=async(_p,args)=>{ready(fx,args,{identity});return{ok:true}};
  await assert.rejects(t.prepareGameTrainer('fixture'),/身份|回执/);assert.notEqual(t.getGameTrainerState('fixture')?.state,'ready');assert.equal(fx.launches.length,0);
 }
});
await check('catalog producer fields reject wrong types and inconsistent ready availability',async()=>{
 for(const catalog of ['broken',[],null,true,{available:'true'},{updated:'false'},{updated:1},{count:'1'},{count:1.5},{nextCheckAt:[]},{imports:[]},{imports:{'translations.json':'1'}},{reason:false},{error:{}},{available:false}]){
  const {trainer:t,fx}=fixture();fx.hidden=async(_p,args)=>{respond(fx,args,{ok:true,state:'ready',catalog});return{ok:true}};
  await assert.rejects(t.refreshGameTrainerData(),/目录|回执|状态/);assert.equal(fx.launches.length,0);
 }
 const {trainer:t,fx}=fixture();fx.hidden=async(_p,args)=>{respond(fx,args,{ok:true,state:'ready',catalog:{available:true,updated:false,count:0,nextCheckAt:fx.now,imports:{'translations.json':0,extension:{keep:true}},reason:'cooldown',extension:{keep:true}}});return{ok:true}};
 assert.equal((await t.refreshGameTrainerData()).catalog.extension.keep,true);assert.equal(fx.launches.length,0);
});

const failures=checks.filter(c=>!c.ok);console.log(`Trainer frontend: ${checks.length-failures.length}/${checks.length} passed`);if(negativeOldCompletion){assert.equal(failures.length,1);assert.equal(failures[0].name,'a late old-game automatic completion cannot postpone the new-game preparation timer');console.log('EXPECTED NEGATIVE REJECTED: old target completion rescheduled B');}else if(failures.length)process.exitCode=1;
if(process.env.YMCC_TRAINER_CONFIG_REPORT){
 const require=createRequire(import.meta.url),toolchain=JSON.parse(fs.readFileSync(path.join(root,'tools/ymcc_sim/global/tdp-schedule-consumer/toolchain-inputs.json'),'utf8'));
 const toolPaths=[process.execPath,require.resolve('esbuild'),toolchain.esbuildExecutable];
 const tools=toolPaths.map(p=>({path:p,sha256:digest(fs.readFileSync(p))}));
 const inputs=[...actualInputs.values()];const drift=inputs.filter(input=>digest(fs.readFileSync(input.path))!==input.sha256);assert.deepEqual(drift,[],'trainer test source drift');
 const report={status:negativeOldCompletion?'EXPECTED_NEGATIVE_REJECTED':failures.length?'FAIL':'PASS',passed:checks.length-failures.length,failed:failures.length,checks,durationMs:Date.now()-startedAt,
  negativeOldCompletion,actualProducerArtifacts:actualProducerContracts.map(x=>x.artifactPath),actualInputs:inputs,mockInputs:[...mockInputs.values()],toolInputs:tools,bundleSha256:digest(bundle.outputFiles[0].text),sourceDrift:drift,
  scope:'production frontend and original auto owner with inert IPC, in-memory files, virtual clock and fake workers; no actual worker/process/network/registry/game execution',actualNativeWriterTested:false};
 const target=path.resolve(process.env.YMCC_TRAINER_CONFIG_REPORT);if(fs.existsSync(target))throw Error('fresh trainer report required');fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,JSON.stringify(report,null,2)+'\n');
}
