// Actual unified hub/HUD demand functions; inert native boundary and deterministic clock.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.env.YMCC_FS_RESOURCE_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/unified-filesystem-demand-20261017');fs.mkdirSync(out,{recursive:true});
const proposed=process.argv.includes('--proposed'),{build}=createRequire(root+'/package.json')('esbuild'),sourcePins={},cases=[],work={};
async function load(side='after'){
 const plugin={name:'actual-production-owner-with-inert-io',setup(b){
  b.onResolve({filter:/monitorNativeDemand$/},()=>({path:path.join(proposed?out+'/proposed':root,'src/bridge/monitorNativeDemand.ts')}));
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]monitorAppliedTdp\.ts$/},()=>({contents:'export function advanceMonitorTdpDisplay(){};export function subscribeMonitorTdpState(){return ()=>{}}',loader:'ts'}));
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/](ipc|api)\.ts$/},a=>({contents:a.path.endsWith('ipc.ts')?'export function invoke(){throw Error("unexpected global native IO")};':'export const fs={readTextFile(){throw Error("unexpected file IO")}};',loader:'ts'}));
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/](monitorData|monitorHudModel|monitorHudSampleSource)\.ts$/},a=>{
   const rel=path.relative(root,a.path),candidate=side==='before'?path.join(out,'before',rel):proposed?path.join(out,'proposed',rel):a.path;
   const text=fs.readFileSync(candidate,'utf8');sourcePins[side]??={};sourcePins[side][rel.replaceAll('\\','/')]=crypto.createHash('sha256').update(text).digest('hex');return{contents:text,loader:'ts',resolveDir:path.dirname(a.path)};
  });
 }};
 const code=await build({stdin:{contents:"export {createMonitorDataHub} from './src/bridge/monitorData';export {hudNativeDemand} from './src/bridge/monitorHudModel';export {createHudSampleSource} from './src/bridge/monitorHudSampleSource';export {monitorFileSystemDemand} from './src/bridge/monitorNativeDemand';",resolveDir:root,loader:'ts'},bundle:true,write:false,format:'esm',platform:'node',plugins:[plugin]});const file=out+'/'+side+'-hub.mjs';fs.writeFileSync(file,code.outputFiles[0].text);return import(pathToFileURL(file).href+'?'+Date.now());
}
const drain=async()=>{for(let i=0;i<50;i++)await Promise.resolve()};
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b});return{promise,resolve,reject};}
const after=await load(),before=await load('before');
function fixture(module=after,legacy=false){
 const s={now:100000,top:false,fps:false,fileSystem:true,explicit:false,generation:1,seq:1,session:1,calls:[],timers:new Map(),timerId:0,startHooks:[],readHooks:[]};
 function packet(){return{schema:1,available:true,session:s.session,relayEpoch:0,generation:s.generation,seq:s.seq,collectedAt:s.now,heartbeatAt:s.now,hwinfoOkAt:s.now,hwDown:false,topActive:s.top,fpsActive:s.fps,...(!legacy&&s.explicit?{fileSystemActive:s.top&&s.fileSystem}:{}),top:s.top?JSON.stringify({ts:s.now,tdpW:30,freqMhz:4000,tempC:65,ac:1,hasBattery:false,batteryPercent:-1,chargeW:0,remainMin:-1,cpuUsage:25,hwDown:false}):null,fps:null};}
 const hub=module.createMonitorDataHub({now:()=>s.now,setTimer:(fn,ms)=>{const id=++s.timerId;s.timers.set(id,{fn,at:s.now+ms});return id},clearTimer:id=>s.timers.delete(id),readTextFile:async()=>{throw Error('unexpected file reader')},invoke:async(cmd,args)=>{
  s.calls.push({cmd,args:structuredClone(args)});
  if(cmd==='monitor.start'){
   ++s.generation;if(args.top){s.top=true;s.fileSystem=legacy?true:args.fileSystem!==false;s.explicit=!legacy&&typeof args.fileSystem==='boolean'}if(args.fps)s.fps=true;
   const hook=s.startHooks.shift();return hook?await hook():{ok:true};
  }
  if(cmd==='monitor.stop'){++s.generation;if(args.top)s.top=false;if(args.fps)s.fps=false;return{ok:true}}
  if(cmd==='monitor.takeover')return{ok:true,cleaned:true};
  if(cmd==='monitor.snapshot'){const value=packet(),hook=s.readHooks.shift();return hook?await hook(value):value}
  throw Error('unexpected command '+cmd);
 }});
 return {s,hub,packet,starts:()=>s.calls.filter(x=>x.cmd==='monitor.start'),stops:()=>s.calls.filter(x=>x.cmd==='monitor.stop')};
}
async function test(name,fn){await fn();cases.push(name)}
async function end(f,...releases){for(const off of releases)off();await drain();assert.equal(f.hub.desired().top,false);assert.equal(f.hub.desired().fps,false);assert.equal(f.s.top,false);assert.equal(f.s.fps,false);assert.equal(f.s.timers.size,0)}
await test('unhinted legacy command bytes/behavior unchanged',async()=>{
 const traces={};for(const [side,module]of[['before',before],['after',after]]){const f=fixture(module),off=f.hub.acquire('legacy',{top:true});await drain();await f.hub.read();await end(f,off);traces[side]=f.s.calls;}assert.deepEqual(traces.after,traces.before);work.legacy=traces.after;
});
await test('internal top demand can omit filesystem without disabling top',async()=>{const f=fixture(),off=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();assert.deepEqual(f.starts()[0].args,{top:true,fps:false,fileSystem:false});assert.equal(f.s.fileSystem,false);assert.equal(f.s.top,true);await end(f,off)});
await test('disk HUD adds union demand while bar remains mounted',async()=>{const f=fixture(),bar=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();const hud=f.hub.acquire('hud',{top:true,fileSystem:true});await drain();assert.equal(f.s.fileSystem,true);assert.deepEqual(f.starts().at(-1).args,{top:true,fps:false,fileSystem:true});assert.equal(f.stops().length,0);await end(f,hud,bar)});
await test('removing disk HUD stops only optional work with bar still active',async()=>{const f=fixture(),bar=f.hub.acquire('bar',{top:true,fileSystem:false}),hud=f.hub.acquire('hud',{top:true,fileSystem:true});await drain();hud();await drain();assert.equal(f.s.top,true);assert.equal(f.s.fileSystem,false);assert.equal(f.stops().length,0);await end(f,bar)});
await test('unknown top consumer keeps complete compatibility data',async()=>{const f=fixture(),bar=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();const old=f.hub.acquire('unknown',{top:true});await drain();assert.equal(f.s.fileSystem,true);old();await drain();assert.equal(f.s.fileSystem,false);await end(f,bar)});
await test('same owner replacement and retired disposer cannot stop new disk request',async()=>{const f=fixture(),old=f.hub.acquire('preview',{top:true,fileSystem:false});await drain();const next=f.hub.acquire('preview',{top:true,fileSystem:true});old();await drain();assert.equal(f.s.fileSystem,true);assert.equal(f.stops().length,0);await end(f,next)});
await test('FPS-only owner never activates optional filesystem',async()=>{const f=fixture(),off=f.hub.acquire('fps',{fps:true,fileSystem:true});await drain();assert.deepEqual(f.starts()[0].args,{top:false,fps:true});assert.equal(f.s.top,false);await end(f,off)});
await test('intent change during delayed false ack converges to latest true union',async()=>{const f=fixture(),gate=deferred();f.s.startHooks.push(()=>gate.promise);const bar=f.hub.acquire('bar',{top:true,fileSystem:false});const hud=f.hub.acquire('hud',{top:true,fileSystem:true});gate.resolve({ok:true});await drain();assert.equal(f.s.fileSystem,true);assert.equal(f.starts().length,2);await end(f,hud,bar)});
await test('unknown false write is compensated even if prior true state was confirmed',async()=>{const f=fixture(),legacy=f.hub.acquire('owner',{top:true});await drain();const gate=deferred();f.s.startHooks.push(()=>gate.promise);const reduced=f.hub.acquire('owner',{top:true,fileSystem:false});const restored=f.hub.acquire('owner',{top:true,fileSystem:true});gate.resolve({ok:false});await drain();f.s.now+=1000;await f.hub.read();await drain();assert.equal(f.s.fileSystem,true);assert.equal(f.starts().at(-1).args.fileSystem,true);legacy();reduced();await end(f,restored)});
await test('readback of applied false converges after lost ack',async()=>{const f=fixture();f.s.startHooks.push(()=>Promise.reject(Error('lost ack')));const off=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();assert(f.s.timers.size<=1);await f.hub.read();await drain();const count=f.starts().length;f.s.now+=9000;await f.hub.read();await drain();assert.equal(f.starts().length,count);assert.equal(f.s.fileSystem,false);await end(f,off)});
await test('read issued before a new hint cannot overwrite its acknowledgement',async()=>{const f=fixture(),old=f.hub.acquire('bar',{top:true});await drain();const gate=deferred();f.s.readHooks.push(value=>{gate.value=value;return gate.promise});const read=f.hub.read();await drain();const next=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();gate.resolve(gate.value);await read;await drain();const count=f.starts().length;await f.hub.read();await drain();assert.equal(f.starts().length,count);assert.equal(f.s.fileSystem,false);old();await end(f,next)});
await test('old native ignoring hint remains compatible without restart storm',async()=>{const f=fixture(after,true),off=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();for(let i=0;i<30;i++){f.s.now+=1000;assert(await f.hub.read());await drain()}assert.equal(f.s.fileSystem,true);assert.equal(f.starts().length,1);await end(f,off)});
await test('actual new session replays optional demand once',async()=>{const f=fixture(),off=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();await f.hub.read();await drain();const count=f.starts().length;f.s.session=2;f.s.generation=1;f.s.seq=0;f.s.top=false;f.s.explicit=false;f.s.fileSystem=true;await f.hub.read();await drain();assert.equal(f.s.fileSystem,false);assert.equal(f.starts().length,count+1);await end(f,off)});
await test('rejected detail writes retain six-attempt retry budget',async()=>{const f=fixture();for(let i=0;i<20;i++)f.s.startHooks.push(()=>({ok:false}));const off=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();for(let i=0;i<20;i++){f.s.now+=10000;f.s.fileSystem=true;f.s.explicit=true;await f.hub.read();await drain();assert(f.s.timers.size<=1)}assert.equal(f.hub.attempts().start,6);assert.equal(f.starts().length,6);await end(f,off)});
await test('invalid snapshot cannot recalibrate optional state',async()=>{const f=fixture(),off=f.hub.acquire('bar',{top:true,fileSystem:false});await drain();f.s.readHooks.push(raw=>({...raw,fileSystemActive:true,top:'{bad'}));assert.equal(await f.hub.read(),null);await drain();assert.equal(f.starts().length,1);await end(f,off)});
await test('pure resource hint preserves unhinted/invalid compatibility',async()=>{for(const [need,expected]of[[{},false],[{top:false,fileSystem:true},false],[{top:true},true],[{top:true,fileSystem:false},false],[{top:true,fileSystem:true},true],[{top:true,fileSystem:'bad'},true]])assert.equal(after.monitorFileSystemDemand(need),expected)});
await test('selected HUD fields derive the existing top/fps channel plus disk need',async()=>{
 assert.deepEqual(after.hudNativeDemand(undefined),{top:true,fps:true});
 assert.deepEqual(after.hudNativeDemand({cpuTemp:true}),{top:true,fps:false,fileSystem:false});
 assert.deepEqual(after.hudNativeDemand({diskRead:true}),{top:true,fps:false,fileSystem:true});
 assert.deepEqual(after.hudNativeDemand({diskWrite:true}),{top:true,fps:false,fileSystem:true});
 assert.deepEqual(after.hudNativeDemand({fps:true}),{top:false,fps:true,fileSystem:false});
 assert.deepEqual(after.hudNativeDemand({clock:true,tdpLimit:true}),{top:false,fps:false,fileSystem:false});
});
await test('sample source replaces resource hint without a zero-demand gap',async()=>{
 const events=[],active=new Set();let seq=0;const source=after.createHudSampleSource({now:()=>100000,subscribeSnapshot:()=>()=>{},subscribeTdp:()=>()=>{},readOnce:async()=>null,acquire:need=>{const id=++seq;active.add(id);events.push({kind:'acquire',need:structuredClone(need),users:active.size});return()=>{active.delete(id);events.push({kind:'release',users:active.size})}}});
 source.setDemand({top:true,fps:false,fileSystem:false});const off=source.acquire();source.setDemand({top:true,fps:false,fileSystem:true});source.setDemand({top:true,fps:false,fileSystem:false});await drain();assert.deepEqual(events.map(e=>e.kind),['acquire','acquire','release','acquire','release']);assert(events.filter(e=>e.kind==='release').every(e=>e.users===1));assert.equal(events.at(-2).need.fileSystem,false);off();assert.equal(active.size,0);work.sourceEvents=events;
});
const result={ok:true,proposed,cases,work,sourcePins,hardwareAccessed:false,realTimers:false,newReaders:false,wholeProcessCpuMeasured:false};fs.writeFileSync(out+'/RESOURCE-DEMAND-RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,cases:cases.length,work},null,2));
