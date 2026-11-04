import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {build} from 'esbuild';
const root=path.resolve(import.meta.dirname,'../..');
export const field=JSON.parse(fs.readFileSync(path.join(import.meta.dirname,'trainer_ready_tmnt_20261022.json'),'utf8'));
export const flush=async()=>{for(let i=0;i<160;i++)await Promise.resolve();};
const mocks={
 'api.ts': `const f=globalThis.fx;export const app={powerControlDir:async()=>f.powerDir,dataDir:async()=>f.dataDir,exeDir:async()=>f.appDir};export const fs={exists:async p=>f.files.has(f.norm(p)),readTextFile:async(p,max)=>{f.reads.push({p,max});if(f.readHook)await f.readHook(p);if(!f.files.has(f.norm(p)))throw Error('missing');return f.files.get(f.norm(p));},sha256File:async p=>{f.hashes.push(p);if(f.hashHook)return f.hashHook(p);if(!f.files.has(f.norm(p)))throw Error('missing hash file');return f.hashValues.get(f.norm(p));},mkdir:async p=>{f.writes.push(['mkdir',p]);return true;},writeTextFileAtomic:async(p,s)=>{f.writes.push(['write',p]);f.put(p,s);},remove:async p=>{f.writes.push(['remove',p]);f.files.delete(f.norm(p));}};export const shell={hidden:async(p,a)=>{f.workers.push(a);if(f.workerHook)return f.workerHook(p,a);throw Error('Unexpected worker');},execute:async p=>{f.launches.push(p);throw Error('Unexpected process');}};`,
 'gamedetect.ts': `const f=globalThis.fx;export const cleanGameTitle=s=>s.trim();export const detectedGameName=g=>g?.title||g?.name||'';export const isGameStatusCurrent=()=>f.gameCurrent&&f.game!==null;export const isCurrentGameTarget=t=>isGameStatusCurrent()&&!!t&&t.pid===f.game.pid&&String(t.processCreated)===String(f.game.processCreated)&&(!t.path||t.path.toLowerCase()===f.game.path.toLowerCase())&&(!t.identityGeneration||!f.game.identityGeneration||t.identityGeneration===f.game.identityGeneration);export const subscribeGameTargetReadiness=cb=>{f.readinessListeners.add(cb);cb(isGameStatusCurrent());return()=>f.readinessListeners.delete(cb);};`,
 'gamePolicyTarget.ts': `const f=globalThis.fx;export const getPolicyGame=()=>f.game;export const subscribePolicyGameStatus=cb=>{f.listeners.add(cb);cb(f.game);return()=>f.listeners.delete(cb)};`,
 'settingsRepository.ts': `export const getSettingsGeneration=()=>globalThis.fx.generation;`,
 'frontendDiagnostics.ts': `export const reportFrontendError=(where,error)=>globalThis.fx.errors.push({where,error:String(error)});`,
 'uiSettings.ts': `export const getUiSetting=()=>globalThis.fx.autoEnabled;export const loadUiSettings=async()=>{};`,
};
export async function createTrainerRecoveryFixture({ownerSource=null,recoverySource=null,realGameDetection=false}={}) {
 const bundle=await build({stdin:{contents:`export * as trainer from './src/bridge/gameTrainer';export * as recovery from './src/bridge/gameTrainerRecovery';export * as auto from './src/bridge/gameTrainerAuto';${realGameDetection?"export * as detector from './src/bridge/gamedetect';export * as policy from './src/bridge/gamePolicyTarget';":''}`,resolveDir:root,loader:'ts'},bundle:true,format:'cjs',platform:'node',write:false,plugins:[{name:'inert-trainer-recovery',setup(b){b.onLoad({filter:/[\\/](api|gamedetect|gamePolicyTarget|settingsRepository|frontendDiagnostics|uiSettings)\.ts$/},a=>{const name=path.basename(a.path);if(realGameDetection&&(name==='gamedetect.ts'||name==='gamePolicyTarget.ts'))return;return {contents:mocks[name],loader:'ts'};});
 b.onLoad({filter:/[\\/]ipc\.ts$/},()=>({contents:'export const invoke=(...args)=>globalThis.fx.invoke(...args);',loader:'ts'}));
 b.onResolve({filter:/^@\/scheduler$/},()=>({path:'trainer-wire-scheduler',namespace:'trainer-wire'}));
 b.onLoad({filter:/.*/,namespace:'trainer-wire'},()=>({contents:'export const registerScheduledTask=(...args)=>globalThis.fx.registerTask(...args);',loader:'ts'}));b.onLoad({filter:/[\\/]gameTrainer\.ts$/},a=>ownerSource===null?undefined:{contents:ownerSource,loader:'ts'});b.onLoad({filter:/[\\/]gameTrainerRecovery\.ts$/},a=>recoverySource===null?undefined:{contents:recoverySource,loader:'ts'});}}]});
 const fx={norm:p=>p.replaceAll('/','\\').toLowerCase(),files:new Map(),hashValues:new Map(),reads:[],hashes:[],writes:[],workers:[],launches:[],listeners:new Set(),readinessListeners:new Set(),events:new Map(),timers:new Map(),scheduled:new Map(),nativeReads:[],nativeSnapshot:null,disposed:false,gameCurrent:true,game:null,generation:0,now:1792661610000,nextTimer:0,powerDir:'D:\\YMCC\\PowerControl',dataDir:'D:\\YMCC\\Data',appDir:'D:\\YMCC',readHook:null,hashHook:null,workerHook:null,errors:[],autoEnabled:false};
 fx.put=(p,s)=>fx.files.set(fx.norm(p),s);
 fx.invoke=async(command,args,deadline)=>{if(fx.disposed)throw Error('Fixture IPC after disposal');if(command!=='game.detect')throw Error('Unexpected fixture IPC '+command);fx.nativeReads.push({command,args,deadline});return structuredClone(fx.nativeSnapshot);};
 fx.registerTask=(key,interval,run,options)=>{if(fx.scheduled.has(key))throw Error('Duplicate shared scheduler owner '+key);const task={interval,run,next:fx.now+interval};fx.scheduled.set(key,task);if(options?.runImmediately)void Promise.resolve().then(run);return()=>fx.scheduled.delete(key);};
 fx.dispose=()=>{fx.disposed=true;fx.timers.clear();fx.scheduled.clear();fx.events.clear();fx.listeners.clear();fx.readinessListeners.clear();};

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
 fx.emit=event=>{for(const cb of [...fx.events.get(event)||[]])cb();};
 fx.advance=async ms=>{fx.now+=ms;for(const task of [...fx.scheduled.values()])if(task.next<=fx.now){task.next=fx.now+task.interval;void task.run();}await flush();const due=[...fx.timers].filter(([,t])=>t.at<=fx.now);for(const[id,t]of due){if(!fx.timers.delete(id))continue;t.fn();}await flush();};
 class Clock extends Date{static now(){return fx.now;}}
 const module={exports:{}};
 vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,Date:Clock,performance:{now:()=>fx.now},console,setTimeout:(fn,ms)=>{const id=++fx.nextTimer;fx.timers.set(id,{fn,at:fx.now+ms});return id;},clearTimeout:id=>fx.timers.delete(id),window:{addEventListener:(name,cb)=>{if(!fx.events.has(name))fx.events.set(name,new Set());fx.events.get(name).add(cb);},removeEventListener:(name,cb)=>fx.events.get(name)?.delete(cb)}});
 const observeRaw=async(snapshot,preferredPid=0)=>{if(!realGameDetection)throw Error('Real detector mode required');fx.nativeSnapshot=structuredClone(snapshot);let selected=null,error=null;try{selected=await module.exports.detector.refreshGameStatusStrict(preferredPid);}catch(e){error=String(e);}await flush();fx.game=module.exports.detector.getCurrentGame();return {selected,error,current:module.exports.detector.isGameStatusCurrent()};};
 return {...module.exports,fx,observeRaw};
}