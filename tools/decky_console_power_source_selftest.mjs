import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {root,task} from './fixtures/decky_task_paths.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const nativeEvents=new Map(),windowEvents=new Map();let visible=false,visibilityChange,reads=0,read=async()=>({known:true,acLine:1});
const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/powerSource.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,queueMicrotask,window:{addEventListener:(k,v)=>windowEvents.set(k,v),removeEventListener:k=>windowEvents.delete(k)},require:name=>{
 if(name==='vue')return {ref:value=>({value}),readonly:value=>value};
 if(name==='./ipc')return {invoke:async command=>{assert.equal(command,'power.sourceSnapshot');reads++;return read();},on:(key,action)=>{nativeEvents.set(key,action);return()=>nativeEvents.delete(key);}};
 if(name==='./yeman')return {detectPowerModeReliable:()=>{throw Error('No hardware probe fallback in native snapshot test');}};
 if(name==='./uiLifecycle')return {isUiVisible:()=>visible,onUiVisibilityChange:action=>{visibilityChange=action;return()=>{visibilityChange=null};}};
 throw Error('Unexpected import '+name);
}});
const api=module.exports,cases=[];async function check(name,action){await action();cases.push(name);}
const flush=async()=>{for(let i=0;i<25;i++)await Promise.resolve();};
const stop=api.startForegroundPowerSourceRefresh(()=>{});
await check('binding hidden source events creates zero IO/timers or mutation',()=>{assert.equal(reads,0);assert.equal(api.powerSourceMode.value,null);assert.equal(nativeEvents.size,2);});
await check('plug event updates AC immediately with no polling/read',()=>{nativeEvents.get('power.sourceChanged')({ac:true});assert.equal(api.powerSourceMode.value,'ac');assert.equal(reads,0);});
await check('unplug event updates DC immediately with no polling/read',()=>{nativeEvents.get('power.sourceChanged')({ac:false});assert.equal(api.powerSourceMode.value,'dc');assert.equal(reads,0);});
await check('invalid unknown-source notification cannot fabricate AC or DC',()=>{nativeEvents.get('power.sourceChanged')({ac:null});assert.equal(api.powerSourceMode.value,'dc');});
await check('settled original power event repairs current source without writes',()=>{nativeEvents.get('power.acChanged')({ac:true});assert.equal(api.powerSourceMode.value,'ac');assert.equal(reads,0);});
await check('mirror-open read cannot roll back a newer DC native event',async()=>{let resolve;read=()=>new Promise(done=>resolve=done);const promise=api.refreshPowerSourceSnapshot();nativeEvents.get('power.sourceChanged')({ac:false});resolve({known:true,acLine:1});await promise;assert.equal(api.powerSourceMode.value,'dc');});
await check('foreground delayed read cannot roll back a newer native unplug event',async()=>{let resolve;read=()=>new Promise(done=>resolve=done);visible=true;windowEvents.get('focus')();await flush();nativeEvents.get('power.sourceChanged')({ac:false});resolve({known:true,acLine:1});await flush();assert.equal(api.powerSourceMode.value,'dc');});
await check('unknown one-shot native snapshot retains known source instead of inventing a side',async()=>{read=async()=>({known:false,acLine:255});await api.refreshPowerSourceSnapshot();assert.equal(api.powerSourceMode.value,'dc');});
await check('hidden window focus never creates a resident read loop',async()=>{visible=false;const before=reads;windowEvents.get('focus')();await flush();assert.equal(reads,before);});
await check('stop removes every native/visibility/window subscription',()=>{stop();assert.equal(nativeEvents.size,0);assert.equal(windowEvents.size,0);assert.equal(visibilityChange,null);});
fs.writeFileSync(path.join(task,'validation/MAINLINE20-POWER-SOURCE.json'),JSON.stringify({stage:'Mainline20',cases:cases.length,cases,exactOriginalSourceExecuted:true,realHardwareWrites:0,realConfigWrites:0,newPolling:0},null,2));console.log('Original AC/DC event and stale-read tests passed: '+cases.length);
