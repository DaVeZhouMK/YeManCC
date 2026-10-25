import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {build} from 'esbuild';
const root=path.resolve(import.meta.dirname,'../..');
export const field=JSON.parse(fs.readFileSync(path.join(import.meta.dirname,'trainer_ready_tmnt_20261022.json'),'utf8'));
export const flush=async()=>{for(let i=0;i<160;i++)await Promise.resolve();};
const mocks={
 'api.ts': `const f=globalThis.fx;export const app={powerControlDir:async()=>f.powerDir,dataDir:async()=>f.dataDir,exeDir:async()=>f.appDir};export const fs={exists:async p=>f.files.has(f.norm(p)),readTextFile:async(p,max)=>{f.reads.push({p,max});if(f.readHook)await f.readHook(p);if(!f.files.has(f.norm(p)))throw Error('missing');return f.files.get(f.norm(p));},sha256File:async p=>{f.hashes.push(p);if(f.hashHook)return f.hashHook(p);if(!f.files.has(f.norm(p)))throw Error('missing hash file');return f.hashValues.get(f.norm(p));},mkdir:async p=>{f.writes.push(['mkdir',p]);return true;},writeTextFileAtomic:async(p,s)=>{f.writes.push(['write',p]);f.put(p,s);},remove:async p=>{f.writes.push(['remove',p]);f.files.delete(f.norm(p));}};export const shell={hidden:async(p,a)=>{f.workers.push(a);if(f.workerHook)return f.workerHook(p,a);throw Error('Unexpected worker');},execute:async p=>{f.launches.push(p);throw Error('Unexpected process');}};`,
 'gamedetect.ts': `export const cleanGameTitle=s=>s.trim();export const detectedGameName=g=>g?.title||g?.name||'';`,
 'gamePolicyTarget.ts': `const f=globalThis.fx;export const getPolicyGame=()=>f.game;export const subscribePolicyGameStatus=cb=>{f.listeners.add(cb);cb(f.game);return()=>f.listeners.delete(cb)};`,
 'settingsRepository.ts': `export const getSettingsGeneration=()=>globalThis.fx.generation;`,
 'frontendDiagnostics.ts': `export const reportFrontendError=(where,error)=>globalThis.fx.errors.push({where,error:String(error)});`,
 'uiSettings.ts': `export const getUiSetting=()=>globalThis.fx.autoEnabled;export const loadUiSettings=async()=>{};`,
};
export async function createTrainerRecoveryFixture({ownerSource=null,recoverySource=null}={}) {
 const bundle=await build({stdin:{contents:`export * as trainer from './src/bridge/gameTrainer';export * as recovery from './src/bridge/gameTrainerRecovery';export * as auto from './src/bridge/gameTrainerAuto';`,resolveDir:root,loader:'ts'},bundle:true,format:'cjs',platform:'node',write:false,plugins:[{name:'inert-trainer-recovery',setup(b){b.onLoad({filter:/[\\/](api|gamedetect|gamePolicyTarget|settingsRepository|frontendDiagnostics|uiSettings)\.ts$/},a=>({contents:mocks[path.basename(a.path)],loader:'ts'}));b.onLoad({filter:/[\\/]gameTrainer\.ts$/},a=>ownerSource===null?undefined:{contents:ownerSource,loader:'ts'});b.onLoad({filter:/[\\/]gameTrainerRecovery\.ts$/},a=>recoverySource===null?undefined:{contents:recoverySource,loader:'ts'});}}]});
 const fx={norm:p=>p.replaceAll('/','\\').toLowerCase(),files:new Map(),hashValues:new Map(),reads:[],hashes:[],writes:[],workers:[],launches:[],listeners:new Set(),events:new Map(),timers:new Map(),game:null,generation:0,now:1792661610000,nextTimer:0,powerDir:'D:\\YMCC\\PowerControl',dataDir:'D:\\YMCC\\Data',appDir:'D:\\YMCC',readHook:null,hashHook:null,workerHook:null,errors:[],autoEnabled:false};
 fx.put=(p,s)=>fx.files.set(fx.norm(p),s);
 fx.indexPath=fx.dataDir+'\\trainer-cache\\trainers.json';
 fx.seedIndex=(index=field.index)=>{fx.put(fx.indexPath,JSON.stringify(index));for(const receipt of Object.values(index)){if(typeof receipt.trainerPath==='string'){fx.put(receipt.trainerPath,'inert trainer bytes');fx.hashValues.set(fx.norm(receipt.trainerPath),receipt.sha256);}if(receipt.identity?.executablePath){fx.put(receipt.identity.executablePath,'inert game bytes');fx.hashValues.set(fx.norm(receipt.identity.executablePath),receipt.identity.executableSha256);}}};
 fx.select=game=>{fx.game=game;for(const cb of [...fx.listeners])cb(game);};
 fx.emit=event=>{for(const cb of [...fx.events.get(event)||[]])cb();};
 fx.advance=async ms=>{fx.now+=ms;const due=[...fx.timers].filter(([,t])=>t.at<=fx.now);for(const[id,t]of due){if(!fx.timers.delete(id))continue;t.fn();}await flush();};
 class Clock extends Date{static now(){return fx.now;}}
 const module={exports:{}};
 vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,Date:Clock,console,setTimeout:(fn,ms)=>{const id=++fx.nextTimer;fx.timers.set(id,{fn,at:fx.now+ms});return id;},clearTimeout:id=>fx.timers.delete(id),window:{addEventListener:(name,cb)=>{if(!fx.events.has(name))fx.events.set(name,new Set());fx.events.get(name).add(cb);},removeEventListener:(name,cb)=>fx.events.get(name)?.delete(cb)}});
 return {...module.exports,fx};
}