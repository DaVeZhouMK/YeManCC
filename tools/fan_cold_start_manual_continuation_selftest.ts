import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { PowerLifecycleState } from '../src/bridge/api';
const curve=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
function gate<T>(){let resolve!:(v:T)=>void,entered!:()=>void;const ready=new Promise<void>(r=>entered=r),pending=new Promise<T>(r=>resolve=r);return {resolve,entered,ready,pending};}
let checks=0;const failures:string[]=[];
async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function main(){
 for(const generation of [0,1,6])for(const action of ['curve','preset'] as const)await check('ordinary cold startup generation='+generation+' keeps first manual '+action,async()=>{
  const host=new NoIoHost(),held=gate<void>();let launches=0;
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
   readNativePowerState:async()=>({generation,phase:'ready',resumeReady:generation>1,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true} as PowerLifecycleState),
   launcher:{async start(){launches++;held.entered();await held.pending;return {pid:101010,executable:'fixture-only.exe'};},async stop(){}}});
  if(generation>0){life.setPowerGeneration(generation);life.observePowerBoundary('resume-ready',generation);}
  const start=life.start();await held.ready;
  const pending=action==='curve'?life.apply(curve):life.applyPreset('aggressive',curve);
  try{held.resolve();await start;await pending;assert.equal(launches,1);assert.equal(host.calls.filter(c=>c==='enable'||c==='preset').length,1);assert.equal(life.hasControlIntent,true);assert(life.currentLease);}
  finally{held.resolve();await start.catch(()=>{});await pending.catch(()=>{});await life.close().catch(()=>{});}
 });
 for(const action of ['off','close','new-power'] as const)await check('ordinary startup manual wait retires after '+action,async()=>{
  const host=new NoIoHost(),held=gate<void>();let launches=0;
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
   readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
   launcher:{async start(){launches++;held.entered();await held.pending;return {pid:101011,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const start=life.start();const startup=start.then(value=>({value,error:null as unknown}),error=>({value:null,error}));await held.ready;
  const pending=life.apply(curve),outcome=pending.then(()=>({ok:true,error:''}),e=>({ok:false,error:String(e)}));let newer:Promise<unknown>=Promise.resolve();
  try{
   if(action==='off')newer=life.disable();else if(action==='close')newer=life.close();else{life.setPowerGeneration(7);life.observePowerBoundary('suspending',7);}
   held.resolve();const started=await startup;if(action==='new-power')assert.equal(started.error,null);else assert(started.error instanceof Error&&started.error.message==='FAN_CONTROL_INTENT_SUPERSEDED:startup-read','OFF/Close must retire its interrupted startup explicitly');await newer;assert.equal((await outcome).ok,false);assert(!host.calls.includes('enable'));assert.equal(life.hasControlIntent,action==='new-power','new sleep retains the current user intent; OFF/Close cancels it');assert.equal(launches,1);
   if(action==='close'){assert.equal(life.processId,null);assert.equal(life.state,'stopped');}
   if(action!=='new-power'){await life.apply(curve);assert.equal(host.calls.filter(c=>c==='enable').length,1);}
  }finally{held.resolve();await start.catch(()=>{});await pending.catch(()=>{});await newer.catch(()=>{});await life.close().catch(()=>{});}
 });
 for(const action of ['curve','preset'] as const)await check('newest manual '+action+' alone continues the ordinary startup',async()=>{
  const host=new NoIoHost(),held=gate<void>(),next=curve.map(n=>({...n,dutyPercent:65}));let launches=0;
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
   readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
   launcher:{async start(){launches++;held.entered();await held.pending;return {pid:101016,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const start=life.start();await held.ready;
  const first=life.apply(curve),retired=first.then(()=>true,()=>false),latest=action==='curve'?life.apply(next):life.applyPreset('aggressive',next);
  try{held.resolve();await start;assert.equal(await retired,false);await latest;assert.equal(launches,1);assert.equal(host.calls.filter(c=>c==='enable'||c==='preset').length,1);assert.deepEqual((life as unknown as {desiredCurve:typeof curve}).desiredCurve,next);}
  finally{held.resolve();await start.catch(()=>{});await first.catch(()=>{});await latest.catch(()=>{});await life.close().catch(()=>{});}
 });
 for(const replacement of ['process','session'] as const)await check('ordinary manual continuation rejects a different '+replacement,async()=>{
  const host=new NoIoHost(),held=gate<void>();
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
   readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
   launcher:{async start(){held.entered();await held.pending;return {pid:101012,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const start=life.start();await held.ready;
  const state=life as unknown as {intentRecoveryStartup:Promise<unknown>;process:{pid:number}|null;sessionGeneration:number};
  const startup=state.intentRecoveryStartup;
  const changed=startup?.then(()=>{if(replacement==='process')state.process={...state.process!,pid:101013};else state.sessionGeneration++;})??Promise.resolve();
  const manual=life.apply(curve),outcome=manual.then(()=>({ok:true,error:''}),e=>({ok:false,error:String(e)}));
  try{assert(startup,'ordinary start must expose its exact queued result');held.resolve();await start;await changed;const result=await outcome;assert.equal(result.ok,false);assert.match(result.error,/SUPERSEDED/);assert(!host.calls.includes('enable'));await life.apply(curve);assert.equal(host.calls.filter(c=>c==='enable').length,1);}
  finally{held.resolve();await start.catch(()=>{});await manual.catch(()=>{});await life.close().catch(()=>{});}
 });
 for(const action of ['curve','preset'] as const)await check('manual-upgrade retry startup retains first '+action+' after original launch failure',async()=>{
  const host=new NoIoHost(),first=gate<void>(),second=gate<void>();let attempts=0;
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
   readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
   launcher:{async start(){attempts++;if(attempts===1){first.entered();await first.pending;throw new Error('fixture initial launch failed before process creation');}second.entered();await second.pending;return {pid:101014,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const start=life.start();const old=start.catch(()=>null);await first.ready;
  const upgrade=life.start({manualRecovery:true}),joined=life.start({manualRecovery:true});first.resolve();await second.ready;
  const pending=action==='curve'?life.apply(curve):life.applyPreset('aggressive',curve);
  try{second.resolve();await old;await upgrade;await joined;await pending;assert.equal(attempts,2,'failed launch made no process; only one retry may launch');assert.equal(host.calls.filter(c=>c==='enable'||c==='preset').length,1);}
  finally{first.resolve();second.resolve();await old;await upgrade.catch(()=>{});await joined.catch(()=>{});await pending.catch(()=>{});await life.close().catch(()=>{});}
 });
 await check('a current startup auth failure never becomes control admission',async()=>{
  const host=new NoIoHost(),held=gate<void>();let launches=0;const observations:Array<Record<string,unknown>>=[];const observe=(event:string)=>observations.push({sequence:observations.length+1,event});host.handshake=async()=>{host.calls.push('handshake');observe('handshake-enter');held.entered();await held.pending;observe('handshake-auth-refusal');throw new FanApiError('fixture unauthorized',401,'AUTH_REQUIRED');};
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
   readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
   launcher:{async start(){launches++;observe('launcher-start');return {pid:101015,executable:'fixture-only.exe'};},async stop(){observe('launcher-stop');}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const start=life.start(),old=start.catch(()=>null);await held.ready;
  observe('explicit-manual-request');const manual=life.apply(curve),result=manual.then(()=>true,()=>false);
  try{held.resolve();await old;assert.equal(await result,false);assert(!host.calls.includes('enable'));assert(!host.calls.includes('preset'));console.log('AUTH_REFUSAL_OBSERVATION '+JSON.stringify({launcherCalls:launches,handshakeCalls:host.calls.filter(c=>c==='handshake').length,enableCalls:host.calls.filter(c=>c==='enable').length,presetCalls:host.calls.filter(c=>c==='preset').length,state:life.state,timeline:observations}));}
  finally{held.resolve();await old;await manual.catch(()=>{});await life.close().catch(()=>{});}
 });
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,hardwareCalls:0,scope:'actual public ordinary start + first manual curve/preset; held inert launcher, native and Host; process/session counterexamples inject identities; no hardware/CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();
