import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const negativeOldCompletion = process.env.YMCC_TRAINER_OLD_COMPLETION_NEGATIVE === '1';
const mocks = {
  'frontendDiagnostics.ts': `export const reportFrontendError=(where,error)=>{(globalThis.fx.diagnostics||=[]).push({where,error:String(error)})};`,
  'api.ts': `const f=globalThis.fx;export const app={powerControlDir:async()=>f.resolvePowerDir?f.resolvePowerDir():f.powerDir,dataDir:async()=>f.dataDir,exeDir:async()=>f.appDir||'E:/YMCC'};export const fs={exists:async p=>f.files.has(p.replaceAll('/',String.fromCharCode(92))),mkdir:async()=>true,readTextFile:async p=>{if(!f.files.has(p))throw Error('missing');return f.files.get(p)},writeTextFileAtomic:async(p,s)=>{if(f.writeError)throw Error('pending denied');f.files.set(p,s)},remove:async p=>f.files.delete(p),sha256File:async()=>f.resolveHash?f.resolveHash():f.hash};export const shell={hidden:async(p,a)=>{return f.runHidden(p,a)},execute:async(p)=>{f.launches.push(p);return f.executeOk}};`,
  'settingsRepository.ts': `export const getSettingsGeneration=()=>globalThis.fx.generation;`,
  'gamedetect.ts': `export const cleanGameTitle=s=>s.trim();export const detectedGameName=g=>g?.title||g?.name||'';`,
  'gamePolicyTarget.ts': `const f=globalThis.fx;export const getPolicyGame=()=>f.game;export const subscribePolicyGameStatus=cb=>{f.gameListeners.add(cb);f.gameListener=game=>{f.game=game;for(const listener of [...f.gameListeners])listener(game)};return()=>{f.gameListeners.delete(cb);if(!f.gameListeners.size)f.gameListener=null}};`,
  'uiSettings.ts': `export const getUiSetting=()=>globalThis.fx.enabled;export const loadUiSettings=async()=>{if(globalThis.fx.settingsError)throw Error('settings denied')};`,
};
const bundle = await build({ stdin:{contents:`export * as trainer from './src/bridge/gameTrainer';export * as auto from './src/bridge/gameTrainerAuto';`,resolveDir:root,loader:'ts'},bundle:true,format:'cjs',platform:'node',write:false,plugins:[{name:'trainer-fixtures',setup(b){if(negativeOldCompletion)b.onLoad({filter:/gameTrainerAuto[.]ts$/},a=>{const source=fs.readFileSync(a.path,'utf8'),guard='!stopped && targetEpoch === gameEpoch && generation === getSettingsGeneration()) schedule();';assert(source.includes(guard));return{contents:source.replace(guard,'!stopped && generation === getSettingsGeneration()) schedule();'),loader:'ts'};});b.onLoad({filter:/[\\/](api|gamedetect|gamePolicyTarget|uiSettings|settingsRepository|frontendDiagnostics)\.ts$/},a=>({contents:mocks[path.basename(a.path)],loader:'ts'}));}}] });
const drain = async()=>{for(let i=0;i<80;i++)await Promise.resolve()};
function fixture() {
  const fx={files:new Map(),workers:[],launches:[],launchArgs:[],enabled:true,executeOk:true,settingsError:false,writeError:false,hash:'a'.repeat(64),now:1700000000000,timers:new Map(),nextTimer:1,events:new Map(),gameListener:null,game:null,gameListeners:new Set(),generation:0,powerDir:'D:/App/PowerControl'.replaceAll('/',String.fromCharCode(92)),dataDir:'D:/Data'.replaceAll('/',String.fromCharCode(92)),resolvePowerDir:null,resolveHash:null};
  fx.files.set('D:\\App\\PowerControl\\YeManTrainerPrepare.ps1','worker');
  fx.files.set('D:\\App\\PowerControl\\YeManTrainerLaunch.ps1','worker');
  fx.runHidden=async(p,a)=>{if(argument(a,'-File').endsWith('YeManTrainerLaunch.ps1')){fx.launchArgs.push(a);fx.launches.push(argument(a,'-TrainerPath'));fx.files.set(argument(a,'-ResultPath'),JSON.stringify({requestId:argument(a,'-RequestId'),ok:fx.executeOk,error:'launch rejected',...fxx(fx)}));return{ok:true}}fx.workers.push(a);return fx.hidden(p,a)};
  fx.hidden=async(_program,args)=>{respond(fx,args,{state:'unavailable',ok:false,message:'none'});return {ok:true,pid:42}};
  class Clock extends Date { static now(){return fx.now} }
  const module={exports:{}};
  vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,console,Date:Clock,
    setTimeout:(fn,ms)=>{const id=fx.nextTimer++;fx.timers.set(id,{fn,at:fx.now+ms});return id},clearTimeout:id=>fx.timers.delete(id),
    window:{addEventListener:(n,cb)=>{if(!fx.events.has(n))fx.events.set(n,new Set());fx.events.get(n).add(cb)},removeEventListener:(n,cb)=>fx.events.get(n)?.delete(cb)},
  });
  fx.advance=async(ms)=>{fx.now+=ms;for(const[id,timer]of [...fx.timers])if(timer.at<=fx.now){fx.timers.delete(id);timer.fn()}await drain()};
  fx.emit=name=>{for(const cb of fx.events.get(name)||[])cb()};
  return {...module.exports,fx};
}
function fxx(fx){return fx.launchReceipt||{}}
function argument(args,key){return args[args.indexOf(key)+1]}
function respond(fx,args,extra={}){const result={requestId:argument(args,'-RequestId'),gameName:argument(args,'-GameName'),...extra};fx.files.set(argument(args,'-ResultPath'),JSON.stringify(result));return result}
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
const failures=checks.filter(c=>!c.ok);console.log(`Trainer frontend: ${checks.length-failures.length}/${checks.length} passed`);if(negativeOldCompletion){assert.equal(failures.length,1);assert.equal(failures[0].name,'a late old-game automatic completion cannot postpone the new-game preparation timer');console.log('EXPECTED NEGATIVE REJECTED: old target completion rescheduled B');}else if(failures.length)process.exitCode=1;