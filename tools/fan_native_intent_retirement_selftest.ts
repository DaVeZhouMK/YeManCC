/** Native intent reads use the actual lifecycle; Host/process/power are inert. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import type { FanActivitySnapshot, PowerLifecycleState } from '../src/bridge/api';
import { NoIoHost } from './fixtures/fan_no_io_host';
const curve=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const nextCurve=curve.map(n=>({...n,dutyPercent:65}));
const awake:PowerLifecycleState={generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
const record=(recoverable=false):FanActivitySnapshot=>({schemaVersion:1,active:!recoverable,recoverable,revision:5,curve,deviceClass:'HandheldCompanion.Devices.OneXPlayerX1Mini'});
function gate<T>(){let resolve!:(v:T)=>void,entered!:()=>void;const ready=new Promise<void>(r=>entered=r),pending=new Promise<T>(r=>resolve=r);return {resolve,entered,ready,pending};}
const inspect=(life:FanHostLifecycle)=>life as unknown as {desiredCurve:typeof curve|null;activeCurve:typeof curve|null;resumeCurve:typeof curve|null;sessionGeneration:number;process:{pid:number}|null};
async function setup(){
 const host=new NoIoHost();let next:ReturnType<typeof gate<FanActivitySnapshot|null>>|null=null,reads=0;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativePowerState:async()=>awake,
 launcher:{async start(){return {pid:100009,executable:'fixture-only.exe'};},async stop(){}},
 readNativeActivity:async()=>{reads++;const held=next;if(held){next=null;held.entered();return held.pending;}return record(true);}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});
 return {life,host,reads:()=>reads,hold(){const held=gate<FanActivitySnapshot|null>();next=held;return held;}};
}
const entries=['renderer','restart','crash','resume','committed-wake'] as const;
async function enter(life:FanHostLifecycle,name:typeof entries[number]){if(name==='renderer')return life.reattachRenderer();if(name==='restart')return life.reattachAfterRendererRestart();if(name==='crash')return life.recoverAfterUncleanExit();if(name==='resume')return life.resume();return life.resumeAfterCommittedPowerWake(6);}
let checks=0;const failures:string[]=[];
async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function main(){
 for(const entry of entries)for(const action of ['off','close','new-curve','new-power'] as const)await check('held native intent '+entry+' retires after '+action,async()=>{
  const f=await setup(),held=f.hold(),pending=enter(f.life,entry);await held.ready;
  try{
   if(action==='off')await f.life.disable();else if(action==='close')await f.life.close();else if(action==='new-curve')await f.life.apply(nextCurve);else{f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   const calls=f.host.calls.length,state=f.life.state,intent=f.life.hasControlIntent,desired=JSON.stringify(inspect(f.life).desiredCurve),generation=f.life.coordinatorSnapshot.powerGeneration;
   held.resolve(record(entry==='crash'));await pending.catch(()=>{});
   assert.equal(f.life.hasControlIntent,intent,'retired native record revived or erased current intent');assert.equal(JSON.stringify(inspect(f.life).desiredCurve),desired,'retired native record changed the current curve');
   assert.equal(f.host.calls.length,calls,'retired native record entered the Host');assert.equal(f.life.state,state,'retired native record changed lifecycle state');assert.equal(f.life.coordinatorSnapshot.powerGeneration,generation);
   if(action!=='new-power'){await f.life.apply(nextCurve);assert(f.host.calls.includes('enable'),'manual enable no longer reachable');assert.deepEqual(inspect(f.life).desiredCurve,nextCurve);}
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const entry of entries)for(const action of ['remembered-curve','new-session','new-host','cancel-token'] as const)await check('held native intent '+entry+' retires after '+action,async()=>{
  const f=await setup(),held=f.hold(),pending=enter(f.life,entry);await held.ready;
  try{
   if(action==='remembered-curve')f.life.rememberDesiredCurve(nextCurve);
   else if(action==='new-session')inspect(f.life).sessionGeneration++;
   else if(action==='new-host')inspect(f.life).process={...inspect(f.life).process!,pid:100010};
   else (f.life as unknown as {cancelRecovery(reason:string):void}).cancelRecovery('fixture-token-change');
   const calls=f.host.calls.length,intent=f.life.hasControlIntent,desired=JSON.stringify(inspect(f.life).desiredCurve);
   held.resolve(record(entry==='crash'));await pending.catch(()=>{});
   assert.equal(f.life.hasControlIntent,intent);assert.equal(JSON.stringify(inspect(f.life).desiredCurve),desired);assert.equal(f.host.calls.length,calls,'obsolete activity entered Host');
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('same-generation native observations preserve the first accepted current intent',async()=>{
  const f=await setup(),a=f.hold(),first=f.life.reattachRenderer();await a.ready;
  const b=f.hold(),second=f.life.reattachRenderer();await b.ready;
  try{b.resolve({...record(),curve:nextCurve});assert.equal(await second,true);a.resolve(record());assert.equal(await first,false);assert.deepEqual(inspect(f.life).desiredCurve,nextCurve);}
  finally{a.resolve(null);b.resolve(null);await first.catch(()=>{});await second.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const entry of ['renderer','crash'] as const)await check('current '+entry+' native intent remains actionable',async()=>{
  const f=await setup(),held=f.hold(),pending=enter(f.life,entry);await held.ready;
  try{held.resolve(record(entry==='crash'));assert.equal(await pending,true);assert.equal(f.life.hasControlIntent,true);assert.deepEqual(inspect(f.life).desiredCurve,curve);if(entry==='renderer')assert(!f.host.calls.includes('enable'));else assert.equal(f.host.calls.filter(x=>x==='enable').length,1);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const entry of entries)await check('current null '+entry+' record stays no-work and manual remains reachable',async()=>{
  const f=await setup(),held=f.hold(),pending=enter(f.life,entry);await held.ready;
  try{const calls=f.host.calls.length;held.resolve(null);await pending;assert.equal(f.life.hasControlIntent,false);assert.equal(f.host.calls.length,calls);await f.life.apply(nextCurve);assert(f.host.calls.includes('enable'));}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const action of ['off','close','new-curve','new-preset','new-power'] as const)await check('crash startup belongs to original queue and retires after '+action,async()=>{
  const host=new NoIoHost(),held=gate<void>();let launches=0;
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativePowerState:async()=>awake,readNativeActivity:async()=>record(true),
   launcher:{async start(){launches++;if(launches===1){held.entered();await held.pending;}return {pid:100011,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
  const pending=life.recoverAfterUncleanExit();await held.ready;let newer:Promise<unknown>=Promise.resolve();
  try{
   if(action==='off')newer=life.disable();else if(action==='close')newer=life.close();else if(action==='new-curve')newer=life.apply(nextCurve);else if(action==='new-preset')newer=life.applyPreset('aggressive',nextCurve);else{life.setPowerGeneration(7);life.observePowerBoundary('suspending',7);}
   held.resolve();await pending.catch(()=>{});await newer;
   assert(launches<=1,'unserialized recovery created another launcher owner');
   if(action==='close'){assert.equal(life.state,'stopped');assert.equal(life.processId,null,'late startup escaped the close owner');}
   assert.equal(life.hasControlIntent,action==='new-curve'||action==='new-preset','cancelled startup revived old intent');
   assert.equal(host.calls.filter(c=>c==='enable'||c==='preset').length,action==='new-curve'||action==='new-preset'?1:0,'cancelled startup submitted an old curve');
   if(action==='new-curve'||action==='new-preset')assert.deepEqual(inspect(life).desiredCurve,nextCurve);
   if(action!=='new-power'){await life.apply(nextCurve);assert(host.calls.includes('enable'));}
  }finally{held.resolve();await pending.catch(()=>{});await newer.catch(()=>{});await life.close().catch(()=>{});}
 });
 for(const replacement of ['process','session'] as const)await check('manual continuation cannot borrow a different startup '+replacement,async()=>{
  const host=new NoIoHost(),held=gate<void>();
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativePowerState:async()=>awake,readNativeActivity:async()=>record(true),
   launcher:{async start(){held.entered();await held.pending;return {pid:100012,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const old=life.recoverAfterUncleanExit();await held.ready;
  const startup=(life as unknown as {intentRecoveryStartup:Promise<unknown>}).intentRecoveryStartup;
  const changed=startup?.then(()=>{if(replacement==='process')inspect(life).process={...inspect(life).process!,pid:100013};else inspect(life).sessionGeneration++;})??Promise.resolve();
  const manual=life.apply(nextCurve);const result=manual.then(()=>({ok:true,error:''}),e=>({ok:false,error:String(e)}));
  try{assert(startup,'current cold-start owner is not exposed to its own continuation');held.resolve();await old.catch(()=>{});await changed;const outcome=await result;assert.equal(outcome.ok,false,'unrelated successor was adopted');assert.match(outcome.error,/SUPERSEDED/);assert(!host.calls.includes('enable'));await life.apply(nextCurve);assert.equal(host.calls.filter(x=>x==='enable').length,1);}
  finally{held.resolve();await old.catch(()=>{});await manual.catch(()=>{});await life.close().catch(()=>{});}
 });
 await check('current eligible cold-start recovery still launches and writes once',async()=>{
  const host=new NoIoHost();let launches=0;
  const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativePowerState:async()=>awake,readNativeActivity:async()=>record(true),launcher:{async start(){launches++;return {pid:100014,executable:'fixture-only.exe'};},async stop(){}}});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
  try{assert.equal(await life.recoverAfterUncleanExit(),true);assert.equal(launches,1);assert.equal(host.calls.filter(x=>x==='enable').length,1);assert.deepEqual(inspect(life).desiredCurve,curve);}
  finally{await life.close().catch(()=>{});}
 });
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,hardwareCalls:0,scope:'actual public native-intent entries; delayed native activity; inert Host/process/power; held launcher is fake; session/PID/token cases inject identities; manual continuation; no device/CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();
