import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {build} from 'esbuild';
import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'../..');
let graphSerial=0,timerFixtureSerial=0;
export const field=JSON.parse(fs.readFileSync(path.join(import.meta.dirname,'trainer_ready_tmnt_20261022.json'),'utf8'));
export const flush=async()=>{for(let i=0;i<160;i++)await Promise.resolve();};
const mocks={
 'api.ts': `const f=globalThis.fx;export const powerLifecycle={get:async()=>{if(f.powerHook)await f.powerHook();return {...f.power};}};export const app={powerControlDir:async()=>f.powerDir,dataDir:async()=>f.dataDir,exeDir:async(timeoutMs)=>{f.metadataTimeouts.push(timeoutMs);return f.appDirectoryHook?f.appDirectoryHook():f.appDir;}};export const fs={exists:async p=>f.files.has(f.norm(p)),readTextFile:async(p,max)=>{f.reads.push({p,max});if(f.readHook)await f.readHook(p);if(!f.files.has(f.norm(p)))throw Error('missing');return f.files.get(f.norm(p));},sha256File:async p=>{f.hashes.push(p);if(f.hashHook)return f.hashHook(p);if(!f.files.has(f.norm(p)))throw Error('missing hash file');return f.hashValues.get(f.norm(p));},mkdir:async p=>{f.writes.push(['mkdir',p]);return true;},writeTextFileAtomic:async(p,s)=>{f.writes.push(['write',p]);f.put(p,s);},remove:async p=>{f.writes.push(['remove',p]);f.files.delete(f.norm(p));}};export const shell={hidden:async(p,a,timeoutMs)=>{f.dispatchTimeouts.push(timeoutMs);f.workers.push(a);if(f.workerHook)return f.workerHook(p,a);throw Error('Unexpected worker');},execute:async p=>{f.launches.push(p);throw Error('Unexpected process');}};`,
 'gamedetect.ts': `const f=globalThis.fx;export const cleanGameTitle=s=>s.trim();export const detectedGameName=g=>g?.title||g?.name||'';export const isGameStatusCurrent=()=>f.gameCurrent&&f.game!==null;export const isCurrentGameTarget=t=>isGameStatusCurrent()&&!!t&&t.pid===f.game.pid&&String(t.processCreated)===String(f.game.processCreated)&&(!t.path||t.path.toLowerCase()===f.game.path.toLowerCase())&&(!t.identityGeneration||!f.game.identityGeneration||t.identityGeneration===f.game.identityGeneration);export const subscribeGameTargetReadiness=cb=>{f.readinessListeners.add(cb);cb(isGameStatusCurrent());return()=>f.readinessListeners.delete(cb);};`,
 'gamePolicyTarget.ts': `const f=globalThis.fx;export const getPolicyGame=()=>f.game;export const subscribePolicyGameStatus=cb=>{f.listeners.add(cb);cb(f.game);return()=>f.listeners.delete(cb)};`,
 'settingsRepository.ts': `export const getSettingsGeneration=()=>globalThis.fx.generation;`,
 'frontendDiagnostics.ts': `export const reportFrontendError=(where,error)=>globalThis.fx.errors.push({where,error:String(error)});`,
 'uiSettings.ts': `export const getUiSetting=()=>globalThis.fx.autoEnabled;export const loadUiSettings=async()=>{};`,
};
export async function createTrainerRecoveryFixture({ownerSource=null,recoverySource=null,powerSource=null,lifecycleSource=null,realGameDetection=false,separatePowerClock=false,storage=new Map()}={}) {
 const bundle=await build({stdin:{contents:`export * as trainer from './src/bridge/gameTrainer';export * as recovery from './src/bridge/gameTrainerRecovery';export * as lifecycle from './src/bridge/trainerLifecycle';export * as auto from './src/bridge/gameTrainerAuto';export * as sharedPower from './src/bridge/powerLifecycleState';${realGameDetection?"export * as detector from './src/bridge/gamedetect';export * as policy from './src/bridge/gamePolicyTarget';":''}`,resolveDir:root,loader:'ts'},bundle:true,format:'cjs',platform:'node',write:false,metafile:true,plugins:[{name:'inert-trainer-recovery',setup(b){b.onLoad({filter:/[\\/](api|gamedetect|gamePolicyTarget|settingsRepository|frontendDiagnostics|uiSettings)\.ts$/},a=>{const name=path.basename(a.path);if(realGameDetection&&(name==='gamedetect.ts'||name==='gamePolicyTarget.ts'))return;return {contents:mocks[name],loader:'ts'};});
 b.onLoad({filter:/[\\/]ipc\.ts$/},()=>({contents:'export class IpcRemoteError extends Error {}\nexport const invoke=(...args)=>globalThis.fx.invoke(...args);export const on=(name,listener)=>{const f=globalThis.fx,key="ipc:"+name,fn=e=>listener(e.detail);if(!f.events.has(key))f.events.set(key,new Set());f.events.get(key).add(fn);return()=>f.events.get(key)?.delete(fn);};',loader:'ts'}));
 b.onLoad({filter:/[\\/]powerLifecycleState\.ts$/},args=>{
  if(powerSource===null&&!separatePowerClock)return;
  let contents=powerSource??fs.readFileSync(args.path,'utf8');
  if(separatePowerClock){const marker='const shared=createSharedPowerLifecycleSource({';assert.equal(contents.split(marker).length,2,'original singleton clock injection');contents=contents.replace(marker,marker+'\n  clock:globalThis.fx.sharedPowerClock,');}
  return {contents,loader:'ts'};
 });
 b.onLoad({filter:/[\\/]trainerLifecycle\.ts$/},()=>lifecycleSource===null?undefined:{contents:lifecycleSource,loader:'ts'});
 b.onResolve({filter:/^@\/scheduler$/},()=>({path:'trainer-wire-scheduler',namespace:'trainer-wire'}));
 b.onLoad({filter:/.*/,namespace:'trainer-wire'},()=>({contents:'export const registerScheduledTask=(...args)=>globalThis.fx.registerTask(...args);',loader:'ts'}));b.onLoad({filter:/[\\/]gameTrainer\.ts$/},a=>ownerSource===null?undefined:{contents:ownerSource,loader:'ts'});b.onLoad({filter:/[\\/]gameTrainerRecovery\.ts$/},a=>recoverySource===null?undefined:{contents:recoverySource,loader:'ts'});}}]});
 if(process.env.YMCC_TRAINER_RECOVERY_GRAPH_DIR){const directory=process.env.YMCC_TRAINER_RECOVERY_GRAPH_DIR;fs.mkdirSync(directory,{recursive:true});const stem=path.join(directory,String(++graphSerial).padStart(4,'0'));fs.writeFileSync(stem+'.cjs',bundle.outputFiles[0].text,{flag:'wx'});fs.writeFileSync(stem+'.metafile.json',JSON.stringify(bundle.metafile,null,2),{flag:'wx'});fs.writeFileSync(stem+'.ports.json',JSON.stringify({root,mocks,ownerSource,recoverySource,powerSource,lifecycleSource,realGameDetection,separatePowerClock},null,2),{flag:'wx'});}
 const timerFixture=++timerFixtureSerial,timerTrace=[];const traceTimers=!!process.env.YMCC_TRAINER_TIMER_OWNER_DIR;
 const fx={norm:p=>p.replaceAll('/','\\').toLowerCase(),files:new Map(),hashValues:new Map(),reads:[],hashes:[],writes:[],workers:[],launches:[],listeners:new Set(),readinessListeners:new Set(),events:new Map(),timers:new Map(),sharedTimers:new Map(),scheduled:new Map(),nativeReads:[],nativeSnapshot:null,disposed:false,power:{phase:'ready',generation:1,hardwareWritesAllowed:true,inputReady:true},gameCurrent:true,game:null,generation:0,now:1792661610000,nextTimer:0,powerDir:'D:\\YMCC\\PowerControl',dataDir:'D:\\YMCC\\Data',appDir:'D:\\YMCC',readHook:null,hashHook:null,workerHook:null,appDirectoryHook:null,metadataTimeouts:[],dispatchTimeouts:[],errors:[],autoEnabled:false};
 // The same virtual time/id order drives both owners;the public dependency clock
 // records observer ownership without changing source facts or business timers.
 fx.sharedPowerClock={setTimeout(fn,ms){const id=++fx.nextTimer;fx.sharedTimers.set(id,{fn,at:fx.now+ms});if(traceTimers)timerTrace.push({operation:'create',owner:'public-observer',id,ms,at:fx.now+ms,stack:new Error().stack,callback:String(fn)});return id;},clearTimeout(id){if(traceTimers)timerTrace.push({operation:'clear',owner:'public-observer',id,existed:fx.sharedTimers.has(id)});fx.sharedTimers.delete(id);}};
 fx.put=(p,s)=>fx.files.set(fx.norm(p),s);
 fx.invoke=async(command,args,deadline)=>{if(fx.disposed)throw Error('Fixture IPC after disposal');fx.nativeReads.push({command,args,deadline});if(command==='power.lifecycle')return fx.powerReadHook?fx.powerReadHook():structuredClone(fx.power);if(command!=='game.detect')throw Error('Unexpected fixture IPC '+command);return structuredClone(fx.nativeSnapshot);};
 fx.registerTask=(key,interval,run,options)=>{if(fx.scheduled.has(key))throw Error('Duplicate shared scheduler owner '+key);const task={interval,run,next:fx.now+interval};fx.scheduled.set(key,task);if(options?.runImmediately)void Promise.resolve().then(run);return()=>fx.scheduled.delete(key);};
 fx.dispose=()=>{if(traceTimers&&!fx.disposed){const directory=process.env.YMCC_TRAINER_TIMER_OWNER_DIR;fs.mkdirSync(directory,{recursive:true});fs.writeFileSync(path.join(directory,String(timerFixture).padStart(4,'0')+'.json'),JSON.stringify({timerTrace,remaining:[...fx.timers].map(([id,row])=>({id,at:row.at})),remainingShared:[...fx.sharedTimers].map(([id,row])=>({id,at:row.at})),nativeReads:fx.nativeReads,power:fx.power},null,2),{flag:'wx'});}fx.disposed=true;fx.timers.clear();fx.sharedTimers.clear();fx.scheduled.clear();fx.events.clear();fx.listeners.clear();fx.readinessListeners.clear();};

 // Match the real worker's echo of the caller's durable pending binding.
 // Installed-cache samples predate this transient protocol and cannot supply it.
 fx.requestReceipt=(args,value)=>{
  const at=key=>{const index=args.indexOf(key);if(index<0||typeof args[index+1]!=='string')throw Error('Fixture missing worker argument '+key);return args[index+1];};
  const raw=fx.files.get(fx.norm(at('-ResultPath')));if(typeof raw!=='string')throw Error('Fixture worker has no durable pending request');
  const pending=JSON.parse(raw),keys=['requestProtocol','requestToken','sourceSession','gameExecutablePath','gameProcessId','gameProcessCreated'];
  if(pending.requestProtocol!==1||pending.requestId!==at('-RequestId')||typeof pending.requestToken!=='string'||typeof pending.sourceSession!=='string')throw Error('Fixture pending request identity invalid');
  for(const key of keys)if(!Object.hasOwn(pending,key))throw Error('Fixture pending binding incomplete: '+key);
  if((args.includes('-GameExecutablePath')&&pending.gameExecutablePath!==at('-GameExecutablePath'))||(args.includes('-GameProcessId')&&pending.gameProcessId!==Number(at('-GameProcessId')))||(args.includes('-GameProcessCreated')&&pending.gameProcessCreated!==at('-GameProcessCreated')))throw Error('Fixture pending target differs from actual worker arguments');
  return {...value,...Object.fromEntries(keys.map(key=>[key,pending[key]])),requestId:pending.requestId};
 };
 fx.indexPath=fx.dataDir+'\\trainer-cache\\trainers.json';
 fx.seedIndex=(index=field.index)=>{fx.put(fx.indexPath,JSON.stringify(index));for(const receipt of Object.values(index)){if(typeof receipt.trainerPath==='string'){fx.put(receipt.trainerPath,'inert trainer bytes');fx.hashValues.set(fx.norm(receipt.trainerPath),receipt.sha256);}if(receipt.identity?.executablePath){fx.put(receipt.identity.executablePath,'inert game bytes');fx.hashValues.set(fx.norm(receipt.identity.executablePath),receipt.identity.executableSha256);}}};
 fx.select=game=>{fx.game=game;for(const cb of [...fx.listeners])cb(game);for(const cb of [...fx.readinessListeners])cb(fx.gameCurrent&&fx.game!==null);};
 fx.emit=(event,generation)=>{for(const cb of [...fx.events.get(event)||[]])cb({type:event,detail:{generation}});};
 fx.advance=async ms=>{fx.now+=ms;for(const task of [...fx.scheduled.values()])if(task.next<=fx.now){task.next=fx.now+task.interval;void task.run();}await flush();const maps=separatePowerClock?[fx.timers,fx.sharedTimers]:[fx.timers];const due=maps.flatMap(timers=>[...timers].filter(([,t])=>t.at<=fx.now).map(([id,t])=>({timers,id,t}))).sort((a,b)=>a.id-b.id);for(const{timers,id,t}of due){if(!timers.delete(id))continue;t.fn();}await flush();};
 class Clock extends Date{static now(){return fx.now;}}
 const module={exports:{}};
 const localStorage={getItem:key=>storage.get(key)??null,setItem:(key,value)=>storage.set(key,String(value)),removeItem:key=>storage.delete(key)};
 vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,Date:Clock,performance:{now:()=>fx.now},console,queueMicrotask,structuredClone,AbortController,localStorage,setTimeout:(fn,ms)=>{const id=++fx.nextTimer;fx.timers.set(id,{fn,at:fx.now+ms});if(traceTimers)timerTrace.push({operation:'create',id,ms,at:fx.now+ms,stack:new Error().stack,callback:String(fn)});return id;},clearTimeout:id=>{if(traceTimers)timerTrace.push({operation:'clear',id,existed:fx.timers.has(id)});fx.timers.delete(id);},window:{addEventListener:(name,cb)=>{if(!fx.events.has(name))fx.events.set(name,new Set());fx.events.get(name).add(cb);},removeEventListener:(name,cb)=>fx.events.get(name)?.delete(cb)}});
 const observeRaw=async(snapshot,preferredPid=0)=>{if(!realGameDetection)throw Error('Real detector mode required');fx.nativeSnapshot=structuredClone(snapshot);let selected=null,error=null;try{selected=await module.exports.detector.refreshGameStatusStrict(preferredPid);}catch(e){error=String(e);}await flush();fx.game=module.exports.detector.getCurrentGame();return {selected,error,current:module.exports.detector.isGameStatusCurrent()};};
 return {...module.exports,fx,observeRaw};
}


// Original trainer consumer composition: same-generation public notification scope.
export async function runTrainerNotificationSequenceCases({test,caseOnly=null}) {
 const kinds=['scope','read-newer','read-missing','held-launch','held-recovery','cycles'];
 assert(caseOnly===null||kinds.includes(caseOnly),'Unknown trainer notification case');
 for(const kind of caseOnly?[caseOnly]:kinds)await test('trainer notification: '+kind,async()=>{
  const f=await createTrainerRecoveryFixture(),events=[];
  const ready={known:true,phase:'ready',generation:8,publicationRevision:10,observedNotificationSequence:21,suspendSerial:4,inputReady:false,hardwareWritesAllowed:true,notificationPending:false};
  f.fx.power={...ready};const stop=f.lifecycle.subscribeTrainerLifecycle(event=>events.push(event));await flush();
  const emit=async state=>{f.fx.power={...state};for(const cb of [...(f.fx.events.get('ipc:power.lifecycleChanged')||[])])cb({type:'ipc:power.lifecycleChanged',detail:{...state}});await flush();};
  let release,stopRecovery,pending;
  try{
   assert.equal(f.sharedPower.getSharedPowerLifecycle().observedNotificationSequence,21);
   const initial=f.lifecycle.getTrainerLifecycleEpoch();
   if(kind==='scope'){
    for(let i=0;i<100;i++)await emit({...ready,publicationRevision:11});assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),initial,'metadata/duplicate publication revoked current trainer work');
    await emit({...ready,publicationRevision:12,observedNotificationSequence:22});
    assert(f.lifecycle.getTrainerLifecycleEpoch()>initial,'same-generation notification retained the old trainer epoch');
    await assert.rejects(f.lifecycle.assertTrainerPowerReady(initial,8),/电源|取消/);
    assert.equal(await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch(),8),8);
    const after=f.lifecycle.getTrainerLifecycleEpoch();for(let i=0;i<100;i++)await emit({...f.fx.power});assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),after);
    const mergedReady={...f.fx.power,publicationRevision:13,suspendSerial:5};
    f.fx.power=mergedReady;
    await assert.rejects(f.lifecycle.assertTrainerPowerReady(after,8),/电源|取消/,'a fresh read with a newer suspend serial cannot admit an old trainer ticket');
    assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),after,'a direct read cannot impersonate a public lifecycle event');
    await emit(mergedReady);
    const current=f.lifecycle.getTrainerLifecycleEpoch();assert.equal(current,after+1,'same-generation Ready with only a new suspend serial must retire the old scope');
    await assert.rejects(f.lifecycle.assertTrainerPowerReady(after,8),/电源|取消/);
    assert.equal(await f.lifecycle.assertTrainerPowerReady(current,8),8);
    await emit({...mergedReady,publicationRevision:14,suspendSerial:4});
    assert.equal(f.sharedPower.getSharedPowerLifecycle().suspendSerial,5);
    assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),current,'a late old suspend scope cannot retire the current ticket');
   }else if(kind==='read-newer'||kind==='read-missing'){
    const read={...ready,publicationRevision:11,observedNotificationSequence:22};if(kind==='read-missing')delete read.observedNotificationSequence;
    f.fx.power=read;
    await assert.rejects(f.lifecycle.assertTrainerPowerReady(initial,8),/电源|取消/,'fresh API read with another notification scope accepted an old trainer ticket');
    assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),initial,'read cannot invent a lifecycle event');
    await emit({...ready,publicationRevision:12,observedNotificationSequence:22});assert.equal(await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch(),8),8);
   }else if(kind==='held-launch'){
    f.fx.seedIndex();const game=structuredClone(field.game);f.fx.select(game);
    assert(await f.trainer.restoreReadyGameTrainer(game.title,game.path,{processTarget:game,isTargetCurrent:()=>true}));
    f.fx.put(f.fx.powerDir+'\\YeManTrainerLaunch.ps1','inert worker');
    let entered;const entry=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>release=resolve);
    f.fx.workerHook=async(_program,args)=>{const result=args[args.indexOf('-ResultPath')+1];f.fx.put(result,JSON.stringify(f.fx.requestReceipt(args,{ok:true,pending:false,code:'started'})));return{ok:true};};
    pending=f.trainer.launchReadyGameTrainer(game.title,game.path,{processTarget:game,isTargetCurrent:async()=>{entered();await held;return true;}}).then(value=>({value}),error=>({error:String(error)}));
    await entry;await emit({...ready,publicationRevision:11,observedNotificationSequence:22});release();const old=await pending;pending=null;
    assert(old.error,'late old trainer launch was accepted after same-generation notification');assert.equal(f.fx.workers.length,0,'old trainer scope reached actual worker dispatch');
    const next=await f.trainer.launchReadyGameTrainer(game.title,game.path,{processTarget:game,isTargetCurrent:async()=>true});assert.equal(next.action,'opened');assert.equal(f.fx.workers.length,1,'fresh trainer scope failed to regain its original launch route');
   }else if(kind==='held-recovery'){
    f.fx.seedIndex();const game=structuredClone(field.game);f.fx.select(game);let entered,readCount=0;
    const entry=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>release=resolve);
    f.fx.readHook=async p=>{if(p===f.fx.indexPath&&++readCount===1){entered();await held;}};
    stopRecovery=f.recovery.startGameTrainerRecovery();await entry;
    await emit({...ready,publicationRevision:11,observedNotificationSequence:22});assert(f.lifecycle.getTrainerLifecycleEpoch()>initial,'notification did not retire actual Recovery work');
    release();await flush();f.fx.readHook=null;await f.fx.advance(5000);await flush();
    assert(f.trainer.getReadyGameTrainer(game.title,game.path,game),'original bounded Recovery did not heal after the retired local read returned');assert.equal(f.fx.workers.length,0);
   }else if(kind==='cycles'){
    let sequence=21,revision=10;
    for(let n=0;n<32;n++){
     const epoch=f.lifecycle.getTrainerLifecycleEpoch();await emit({...ready,publicationRevision:++revision,observedNotificationSequence:++sequence});assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),epoch+1,'one accepted notification scope must retire work once');
     assert.equal(await f.lifecycle.assertTrainerPowerReady(f.lifecycle.getTrainerLifecycleEpoch(),8),8);
     await emit({...ready,publicationRevision:++revision,observedNotificationSequence:sequence-1});assert.equal(f.sharedPower.getSharedPowerLifecycle().observedNotificationSequence,sequence,'stale notification was not rejected');
    }
    assert.equal(f.fx.workers.length,0);assert.equal(f.fx.writes.length,0);assert.equal(f.fx.timers.size,0);
   }
  }finally{
   release?.();if(pending)await pending;stopRecovery?.();stop();await flush();
   assert.equal(f.fx.timers.size,0,'trainer timers returned before fixture disposal');assert.equal(f.fx.listeners.size,0);assert.equal(f.fx.readinessListeners.size,0);assert.equal([...f.fx.events.values()].reduce((n,s)=>n+s.size,0),0,'trainer subscriptions returned before fixture disposal');
   f.fx.dispose();
  }
 });
 return {actualSharedSource:true,actualLifecycle:true,actualOwner:true,actualRecovery:true,cases:caseOnly?1:kinds.length};
}

// Directed continuation of the original actual Auto/Recovery/lifecycle consumers.
export async function runTrainerPowerHardeningCases({test,lifecycleSource=null}) {
 const ready={known:true,phase:'ready',generation:7,publicationRevision:1,inputReady:true,hardwareWritesAllowed:true};
 async function fixture(){
  const f=await createTrainerRecoveryFixture({lifecycleSource}),pending=[];
  f.fx.power={...ready};f.fx.autoEnabled=true;
  f.fx.powerReadHook=()=>new Promise((resolve,reject)=>pending.push({resolve,reject}));
  const stop=f.auto.startGameTrainerAutoPrepare();await flush();
  const reply=async value=>{assert(pending.length,'owned power read expected');pending.shift().resolve(value);await flush();};
  await reply({...ready});
  const cleanup=async()=>{stop();while(pending.length)pending.shift().resolve({...ready});await flush();
   assert.equal(f.fx.timers.size,0,'timers returned before fixture disposal');assert.equal(f.fx.listeners.size,0);assert.equal(f.fx.readinessListeners.size,0);
   assert.equal([...f.fx.events.values()].reduce((n,s)=>n+s.size,0),0,'all actual consumer subscriptions returned');
   assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.writes.length,0);f.fx.dispose();};
  return {f,pending,reply,cleanup};
 }
 await test('hardening trainer: same-generation fresh confirmation resumes actual Auto and Recovery without business Ready replay',async()=>{
  const x=await fixture(),{f}=x;
  try{
   assert([...f.fx.timers.values()].some(t=>t.at-f.fx.now===30000),'actual Auto catalog scheduled');
   const oldEpoch=f.lifecycle.getTrainerLifecycleEpoch();f.fx.emit('ipc:webview.recovering');await flush();
   assert(f.lifecycle.getTrainerLifecycleEpoch()>oldEpoch);assert.equal(f.fx.timers.size,0,'cached Ready must not unpause Auto');
   f.fx.seedIndex();f.fx.select(structuredClone(field.game));await flush();assert.equal(f.fx.reads.length,0,'Recovery remains paused before fresh reply');
   await x.reply({...ready});
   assert([...f.fx.timers.values()].some(t=>t.at-f.fx.now===30000),'actual Auto catalog resumed');
   assert(f.fx.reads.some(r=>r.p===f.fx.indexPath),'actual Recovery consumed its original cache after confirmation');
   assert.equal(f.lifecycle.getTrainerLifecycleEpoch(),oldEpoch+1,'targeted confirmation does not replay global resumed cancellation');
   assert(f.fx.nativeReads.every(r=>r.command==='power.lifecycle'));
  }finally{await x.cleanup();}
 });
 await test('hardening trainer: earlier recovery reply cannot resume its successor; dispose cancels confirmation',async()=>{
  const x=await fixture(),{f}=x;
  try{
   const catalogDueAt=f.fx.now+30000;
   assert([...f.fx.timers.values()].some(t=>t.at===catalogDueAt),'original Auto catalog deadline exists');
   f.fx.emit('ipc:webview.recovering');await flush();
   f.fx.emit('ipc:webview.recovering');await flush();
   await x.reply({...ready});assert.equal(f.fx.timers.size,1,'superseded read only leaves the shared retry');
   assert.equal([...f.fx.timers.values()][0].at-f.fx.now,1000);
   await f.fx.advance(1000);await x.reply({...ready});
   assert([...f.fx.timers.values()].some(t=>t.at===catalogDueAt),'recovery preserves the original catalog deadline');
   f.fx.emit('ipc:webview.recovering');await flush();
  }finally{await x.cleanup();}
 });
 await test('hardening trainer: a Ready publication during recovery cannot substitute for this read',async()=>{
  const x=await fixture(),{f}=x;
  try{
   const catalogDueAt=f.fx.now+30000;
   assert([...f.fx.timers.values()].some(t=>t.at===catalogDueAt),'original Auto catalog deadline exists');
   f.fx.emit('ipc:webview.recovering');await flush();
   const state={...ready,publicationRevision:2,inputReady:false};
   for(const cb of [...f.fx.events.get('ipc:power.lifecycleChanged')])cb({type:'ipc:power.lifecycleChanged',detail:state});await flush();
   assert.equal(f.fx.timers.size,0,'publication cannot resume Auto before authoritative read');
   await x.reply({...ready});assert.equal(f.fx.timers.size,1);
   await f.fx.advance(1000);await x.reply(state);
   assert([...f.fx.timers.values()].some(t=>t.at===catalogDueAt),'recovery preserves the original catalog deadline');
  }finally{await x.cleanup();}
 });
 return {actualAuto:true,actualRecovery:true,actualLifecycle:true,cases:3};
}

// Only the newly reported diagnostic-Ready/public-admission boundary.
export async function runTrainerReadyAdmissionCase({test,powerSource=null}) {
 await test('ready admission trainer: actual Auto and Recovery wait for public gates, independently of inputReady',async()=>{
  const f=await createTrainerRecoveryFixture({powerSource}),pending=[];
  const ready={known:true,phase:'ready',generation:8,publicationRevision:10,inputReady:false,hardwareWritesAllowed:true,notificationPending:false};
  f.fx.power={...ready};f.fx.autoEnabled=true;
  f.fx.powerReadHook=()=>new Promise(resolve=>pending.push(resolve));
  const stop=f.auto.startGameTrainerAutoPrepare();
  const reply=async state=>{assert(pending.length,'owned read expected');pending.shift()(state);await flush();};
  try{
   await flush();await reply(ready);
   assert([...f.fx.timers.values()].some(t=>t.at-f.fx.now===30000),'controller absence does not hold Auto');
   f.fx.emit('ipc:webview.recovering');await flush();
   f.fx.seedIndex();f.fx.select(structuredClone(field.game));await flush();
   const blocked={...ready,publicationRevision:11,hardwareWritesAllowed:false,notificationPending:true};
   await reply(blocked);assert.equal(f.fx.reads.length,0,'trainer must not consume cache before public admission');
   for(const delay of [1000,2000,5000,15000,30000]){
    assert.equal(f.fx.timers.size,1,'only the shared retry may remain');
    assert.equal([...f.fx.timers.values()][0].at-f.fx.now,delay);
    await f.fx.advance(delay);await reply(blocked);assert.equal(f.fx.reads.length,0);
   }
   await f.fx.advance(30000);await reply({...ready,publicationRevision:12});
   assert([...f.fx.timers.values()].some(t=>t.at-f.fx.now===30000),'Auto resumes without inputReady');
   assert(f.fx.reads.some(r=>r.p===f.fx.indexPath),'actual Recovery resumes after public admission');
  }finally{
   stop();while(pending.length)pending.shift()(ready);await flush();
   assert.equal(f.fx.timers.size,0);assert.equal(f.fx.listeners.size,0);assert.equal(f.fx.readinessListeners.size,0);
   assert.equal([...f.fx.events.values()].reduce((n,s)=>n+s.size,0),0);
   assert.equal(f.fx.workers.length,0);assert.equal(f.fx.launches.length,0);assert.equal(f.fx.writes.length,0);f.fx.dispose();
  }
 });
 return {actualAuto:true,actualRecovery:true,actualLifecycle:true,cases:1,oldSixExecuted:0,oldTwoNegativesExecuted:0};
}
