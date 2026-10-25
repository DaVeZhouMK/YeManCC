/** FAN985: actual Fan bridge with native async-Close timing contract fixtures. No device/IPC. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, type FanNode } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { PowerLifecycleState } from '../src/bridge/api';
const curve:FanNode[]=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const state=(phase:PowerLifecycleState['phase']='ready',generation=6,allowed=phase==='ready'):PowerLifecycleState=>({phase,generation,hardwareWritesAllowed:allowed,resumeReady:phase==='ready',inputReady:true,hibernateAvailable:true});
const defer=()=>{let resolve!:()=>void;const promise=new Promise<void>(r=>{resolve=r;});return {promise,resolve};};
const delay=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
const bounded=async<T>(p:Promise<T>,ms=1200)=>{let timer:ReturnType<typeof setTimeout>|undefined;try{return await Promise.race([p,new Promise<never>((_,reject)=>{timer=setTimeout(()=>reject(Error('fixture operation did not settle')),ms);})]);}finally{clearTimeout(timer);}};
class Host extends NoIoHost {
 owner=1;closedOwners:number[]=[];enabledOwners:number[]=[];presetOwners:number[]=[];trace:string[]=[];
 constructor(){super();this.factoryType='HandheldCompanion.Devices.MSI.ClawCG3EM';this.stateName='Stopped';}
 half(){this.opened=true;this.events=false;this.closed=false;this.stateName='Open';}
 reset(){this.owner++;this.opened=this.events=this.closed=false;this.lease=null;this.generation=this.attempt=0;this.stateName='Stopped';this.powerState='Unknown';}
 override async enable(){assert(this.opened&&this.events&&this.lease);this.enabledOwners.push(this.owner);return super.enable();}
 override async applyPreset(){assert(this.opened&&this.events&&this.lease);this.presetOwners.push(this.owner);return super.applyPreset();}
 override async close(){this.closedOwners.push(this.owner);this.trace.push(this.owner+':close');const s=await super.close();return {...s,oemRestoreConfirmed:false,oemPhysicalOwnershipConfirmed:false};}
}
function make(budget=800,adopt=false){const host=new Host(),wake=defer(),read=defer(),nativeProbe=defer();let current:PowerLifecycleState|null=state(),starts=0,stops=0,wakeCalls=0,reads=0,readTimes:number[]=[];
 let afterRead:(()=>void)|null=null,holdRead:Promise<void>|null=null,readyAfterWake=false;const timeouts:(number|undefined)[]=[];
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:0,resumeWaitDeadlineMs:budget,
  launcher:{async start(){starts++;assert(starts<=2);if(starts>1)host.reset();host.trace.push(host.owner+':launch');return {pid:98500+starts,executable:'fixture-fan.exe',adopted:adopt&&starts===1};},async stop(){stops++;host.trace.push(host.owner+':exit');}},
  readNativeActivity:async()=>null,readNativePowerState:async(timeoutMs?:number)=>{reads++;readTimes.push(performance.now());timeouts.push(timeoutMs);if(current?.phase==='resuming')read.resolve();const pendingWakeRead=timeoutMs!==undefined&&wakeCalls>0;if(pendingWakeRead)nativeProbe.resolve();if(holdRead&&pendingWakeRead)await holdRead;afterRead?.();return current?{...current}:null;},
  requestManualWake:async generation=>{wakeCalls++;const ok=current?.generation===generation&&current?.phase!=='suspending';if(ok&&current?.phase!=='ready')current=state(readyAfterWake?'ready':'resuming');wake.resolve();return {ok,generation,phase:current?.phase??'unknown'};},
 });life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
 return {host,life,wake,read,nativeProbe,set:(v:PowerLifecycleState|null)=>{current=v;},starts:()=>starts,stops:()=>stops,reads:()=>reads,times:()=>readTimes,timeouts,immediateWake:()=>{readyAfterWake=true;},wakeCalls:()=>wakeCalls,afterRead:(v:(()=>void)|null)=>{afterRead=v;},holdRead:(v:Promise<void>|null)=>{holdRead=v;}};
}
async function cleanup(f:ReturnType<typeof make>){f.set(state());f.afterRead(null);f.holdRead(null);await f.life.close().catch(()=>{});}
let checks=0;const failures:string[]=[],observations:unknown[]=[];async function check(name:string,run:()=>Promise<void>){if(process.env.YMCC_FAN_NATIVE_WAIT_CASE&&!name.includes(process.env.YMCC_FAN_NATIVE_WAIT_CASE))return;checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function main(){
 for(const action of ['start','apply','preset'] as const)await check('first '+action+' survives accepted native wake while Close is still pending',async()=>{
  const f=make(1000,action!=='start');try{if(action!=='start')f.host.half();await f.life.start(action==='start'?{manualRecovery:true}:{}).catch(()=>{});if(action==='start')f.host.half();f.set(state('suspended'));
   const p=(action==='start'?f.life.start({manualRecovery:true}):action==='apply'?f.life.apply(curve):f.life.applyPreset('aggressive',curve));const outcome=p.then(v=>({v,error:''}),e=>({v:null,error:String(e)}));
   await bounded(f.wake.promise);await delay(20);assert.equal(f.host.closedOwners.length,0,'pending native Close is not write permission');assert.equal(f.starts(),1);
   f.set(state());const result=await bounded(outcome);assert.equal(result.error,'');if(action==='start')await f.life.apply(curve);
   assert.deepEqual(f.host.closedOwners,[1]);assert.equal(f.starts(),2);assert.equal(f.stops(),1);assert.deepEqual(action==='preset'?f.host.presetOwners:f.host.enabledOwners,[2]);assert.equal(f.wakeCalls(),1);
   assert(f.host.trace.indexOf('1:exit')<f.host.trace.indexOf('2:launch'));
  }finally{await cleanup(f);}
 });
 await check('native resuming with no new manual promotion waits then continues',async()=>{
  const f=make();try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('resuming'));const p=f.life.start({manualRecovery:true}).then(v=>({v,error:''}),e=>({v:null,error:String(e)}));
   await delay(30);f.set(state());const result=await bounded(p);assert.equal(result.error,'');await f.life.apply(curve);assert.deepEqual(f.host.enabledOwners,[2]);assert.equal(f.wakeCalls(),0);
  }finally{await cleanup(f);}
 });
 await check('native completing its real sleep Close is rechecked before replacement',async()=>{
  const f=make();try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));const p=f.life.start({manualRecovery:true}).then(v=>({v,error:''}),e=>({v:null,error:String(e)}));await bounded(f.wake.promise);
   f.host.opened=f.host.events=false;f.host.stateName='Suspended';f.host.powerState='Suspended';f.host.generation=6;f.host.attempt=1;f.host.completeResume=true;f.set(state());
   const result=await bounded(p);assert.equal(result.error,'');await f.life.apply(curve);assert.equal(f.starts(),1,'completed current native Close allows original resume owner');assert.equal(f.host.closedOwners.length,0);assert.deepEqual(f.host.enabledOwners,[1]);
  }finally{await cleanup(f);}
 });
 for(const action of ['apply','preset'] as const)await check('direct '+action+' rechecks native-completed Close instead of cached original Open',async()=>{
  const f=make(1000,true);try{f.host.half();await f.life.start().catch(()=>{});f.set(state('suspended'));
   const pending=(action==='apply'?f.life.apply(curve):f.life.applyPreset('aggressive',curve)).then(()=>'',e=>String(e));await bounded(f.wake.promise);
   f.host.opened=f.host.events=false;f.host.stateName='Suspended';f.host.powerState='Suspended';f.host.generation=6;f.host.attempt=1;f.host.completeResume=true;f.set(state());
   assert.equal(await bounded(pending),'');assert.equal(f.starts(),1);assert.equal(f.host.closedOwners.length,0);assert.deepEqual(action==='apply'?f.host.enabledOwners:f.host.presetOwners,[1]);
  }finally{await cleanup(f);}
 });
 for(const cancel of ['off','close','new-curve','new-sleep'] as const)await check(cancel+' settles while manual native wait leaves control queue free',async()=>{
  const f=make(2000);try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));const p=f.life.start({manualRecovery:true}).then(()=>'',e=>String(e));await bounded(f.wake.promise);await delay(15);
   if(cancel==='off')await bounded(f.life.disable(),150);
   if(cancel==='close')await bounded(f.life.close(),150);
   if(cancel==='new-curve'){f.set(state());await bounded(f.life.apply(curve),1200).catch(e=>{throw new Error(String(e)+' calls='+f.host.calls.join(',')+' trace='+f.host.trace.join(',')+' life='+f.life.state);});}
   if(cancel==='new-sleep'){f.set(state('suspending',7));f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   const error=await bounded(p,150);assert.match(error,/FAN_CONTROL_INTENT_SUPERSEDED/);
   if(cancel!=='new-curve'){assert.equal(f.starts(),1);assert.equal(f.host.enabledOwners.length,0);}
  }finally{await cleanup(f);}
 });
 await check('native wait expires once, no rapid polling, next manual click can continue',async()=>{
  const f=make(550);try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));const t=performance.now();const error=await bounded(f.life.start({manualRecovery:true}).then(()=>'',e=>String(e)));const elapsed=performance.now()-t;
   assert.match(error,/FAN_MANUAL_NATIVE_WAIT_DEADLINE/);assert(elapsed>=500&&elapsed<900);assert(f.reads()<12);assert(f.timeouts.some(v=>v!==undefined&&v>0&&v<400),'native read inherits remaining window, not full IPC timeout');assert.equal(f.host.closedOwners.length,0);assert.equal(f.starts(),1);assert.equal(f.wakeCalls(),1);
   f.set(state());await f.life.start({manualRecovery:true});await f.life.apply(curve);assert.deepEqual(f.host.enabledOwners,[2]);
  }finally{await cleanup(f);}
 });
 for(const denial of ['suspending','ready-no-write','wrong-generation'] as const)await check('native wait rejects '+denial+' before any Close or successor',async()=>{
  const f=make(1000);try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));const p=f.life.start({manualRecovery:true}).then(()=>'',e=>String(e));await bounded(f.wake.promise);
   f.set(denial==='suspending'?state('suspending'):denial==='ready-no-write'?state('ready',6,false):state('ready',7));
   const error=await bounded(p);assert(error);assert.equal(f.host.closedOwners.length,0);assert.equal(f.starts(),1);assert.equal(f.host.enabledOwners.length,0);
  }finally{await cleanup(f);}
 });
 await check('OFF does not wait behind an in-flight native read',async()=>{
  const f=make(2000),held=defer();try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));f.holdRead(held.promise);
   const pending=f.life.start({manualRecovery:true}).then(()=>'',e=>String(e));await bounded(f.nativeProbe.promise);
   assert.equal(f.wakeCalls(),1,'hold the post-promotion wait read, not initial bounded startup synchronization');
   await bounded(f.life.disable(),150);f.set(state());held.resolve();const error=await bounded(pending,150);
   assert.match(error,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.equal(f.starts(),1);assert.equal(f.host.enabledOwners.length,0);
  }finally{held.resolve();await cleanup(f);}
 });
 await check('same-generation Ready/resuming flaps cannot renew the absolute deadline',async()=>{
  const f=make(650);try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('resuming'));let reads=0;
   f.afterRead(()=>{reads++;f.set(state(reads%3===0?'ready':'resuming'));});const t=performance.now();
   const error=await bounded(f.life.start({manualRecovery:true}).then(()=>'',e=>String(e)),1000);
   assert.match(error,/FAN_MANUAL_NATIVE_WAIT_DEADLINE/);assert(performance.now()-t<950);assert.equal(f.starts(),1);assert.equal(f.host.closedOwners.length,0);
  }finally{await cleanup(f);}
 });
 await check('simultaneous ON retries share one pending owner and one replacement',async()=>{
  const f=make();try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));
   const a=f.life.start({manualRecovery:true}),b=f.life.start({manualRecovery:true});const settled=Promise.all([a,b]).then(()=>'',e=>String(e));
   await bounded(f.wake.promise);await delay(20);f.set(state());assert.equal(await bounded(settled),'');await f.life.apply(curve);
   assert.equal(f.wakeCalls(),1);assert.equal(f.starts(),2);assert.equal(f.stops(),1);assert.deepEqual(f.host.closedOwners,[1]);
  }finally{await cleanup(f);}
 });
 await check('pending native state disappears temporarily, bounded observation later recovers',async()=>{
  const f=make(1200);try{await f.life.start({manualRecovery:true});f.host.half();f.set(state('resuming'));let probes=0;
   f.afterRead(()=>{probes++;if(probes===3)f.set(null);if(probes===4)f.set(state());});
   const error=await bounded(f.life.start({manualRecovery:true}).then(()=>'',e=>String(e)),1600);assert.equal(error,'');await f.life.apply(curve);assert.deepEqual(f.host.enabledOwners,[2]);
  }finally{await cleanup(f);}
 });
 await check('normal ready manual ON and curve edits add no wait or promotion',async()=>{
  const f=make();try{await f.life.start({manualRecovery:true});await f.life.apply(curve);const n=f.reads();await f.life.apply(curve);assert.equal(f.reads(),n);assert.equal(f.wakeCalls(),0);assert.equal(f.starts(),1);assert.equal(f.host.closedOwners.length,0);
  }finally{await cleanup(f);}
 });
 for(const family of ['MSI.ClawCG3EM','ROGAlly','GPDWin5'])await check(family+' current staged manual start must not inherit the superseded native-wait result',async()=>{
  const f=make(2000);f.host.factoryType='HandheldCompanion.Devices.'+family;
  const latestCurve=curve.map(n=>({...n,dutyPercent:65}));let old:Promise<string>|undefined,latest:Promise<{allowed:boolean,error:string}>|undefined,peer:Promise<{allowed:boolean,error:string}>|undefined;
  try{
   await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));
   old=f.life.start({manualRecovery:true,reason:'fixture-old-manual-wake'}).then(()=>'',error=>String(error));
   await bounded(f.wake.promise);await delay(15);
   const retiredIntent=f.life.controlIntentRevision;f.life.stagePendingControlCurve(latestCurve);const currentIntent=f.life.controlIntentRevision;
   latest=f.life.start({manualRecovery:true,reason:'fixture-latest-page-ON'}).then(gate=>({allowed:gate.allowed,error:''}),error=>({allowed:false,error:String(error)}));
   peer=f.life.start({manualRecovery:true,reason:'fixture-duplicate-latest-page-ON'}).then(gate=>({allowed:gate.allowed,error:''}),error=>({allowed:false,error:String(error)}));
   f.set(state());
   const oldOutcome=await bounded(old),newOutcome=await bounded(latest),peerOutcome=await bounded(peer);
   observations.push({scenario:'staged-manual-native-wait',family,retiredIntent,currentIntent,oldOutcome,newOutcome,peerOutcome,starts:f.starts(),stops:f.stops(),trace:[...f.host.trace]});
   assert.match(oldOutcome,/FAN_CONTROL_INTENT_SUPERSEDED/,'old request must remain retired');
   assert.equal(newOutcome.allowed,true,'fresh staged manual start borrowed old cancellation: '+newOutcome.error);
   assert.equal(peerOutcome.allowed,true,'same current request must share the one upgrade');
   await bounded(f.life.apply(latestCurve));assert.equal(f.starts(),2);assert.equal(f.stops(),1);assert.deepEqual(f.host.enabledOwners,[2]);assert.deepEqual(f.host.closedOwners,[1]);
  }finally{f.set(state());await old;await latest;await peer;await cleanup(f);}
 });
 for(const cancel of ['off','close','new-sleep','new-curve'])await check('current manual join canceled by later '+cancel+' cannot launch a successor',async()=>{
  const f=make(2000),held=defer();let old:Promise<string>|undefined,latest:Promise<string>|undefined;
  try{
   await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));
   old=f.life.start({manualRecovery:true}).then(()=>'',error=>String(error));await bounded(f.wake.promise);
   f.holdRead(held.promise);await bounded(f.nativeProbe.promise);
   f.life.stagePendingControlCurve(curve);latest=f.life.start({manualRecovery:true}).then(()=>'',error=>String(error));
   if(cancel==='off')await bounded(f.life.disable());
   else if(cancel==='close')await bounded(f.life.close());
   else if(cancel==='new-sleep'){f.set(state('suspending',7));f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   else f.life.stagePendingControlCurve(curve.map(n=>({...n,dutyPercent:45})));
   // Native may finish its existing Close after the user has canceled. That
   // readiness cannot turn a retired request into a new manual ON.
   if(cancel!=='new-sleep')f.set(state());
   held.resolve();f.holdRead(null);const outcomes=await bounded(Promise.all([old,latest]));
   observations.push({scenario:'retired-current-manual-join',cancel,outcomes,starts:f.starts(),stops:f.stops()});
   assert(outcomes.every(error=>/FAN_CONTROL_INTENT_SUPERSEDED/.test(error)),'late join revived after '+cancel+': '+outcomes.join(','));assert.equal(f.starts(),1);assert.equal(f.host.enabledOwners.length,0);
  }finally{held.resolve();f.holdRead(null);await old;await latest;await cleanup(f);}
 });
 await check('shared manual startup authentication failure stays terminal without an extra launch',async()=>{
  const f=make(),entered=defer(),release=defer();let old:Promise<unknown>|undefined,latest:Promise<unknown>|undefined;
  try{
   f.host.handshake=async()=>{entered.resolve();await release.promise;throw new FanApiError('fixture authentication refusal',401,'API_SESSION_REQUIRED');};
   old=f.life.start({manualRecovery:true}).then(()=>null,error=>error);await bounded(entered.promise);
   latest=f.life.start({manualRecovery:true}).then(()=>null,error=>error);release.resolve();
   const outcomes=await bounded(Promise.all([old,latest]));assert(outcomes.every(error=>error instanceof FanApiError&&error.status===401),'authentication was retried or reclassified');assert.equal(f.starts(),1);assert.equal(f.host.enabledOwners.length,0);
  }finally{release.resolve();await old;await latest;await cleanup(f);}
 });
 await check('shared manual native wait deadline stays bounded without automatically reopening',async()=>{
  const f=make(300);let old:Promise<string>|undefined,latest:Promise<string>|undefined;
  try{
   await f.life.start({manualRecovery:true});f.host.half();f.set(state('suspended'));
   old=f.life.start({manualRecovery:true}).then(()=>'',error=>String(error));await bounded(f.wake.promise);
   latest=f.life.start({manualRecovery:true}).then(()=>'',error=>String(error));const outcomes=await bounded(Promise.all([old,latest]),1000);
   assert(outcomes.every(error=>/FAN_MANUAL_NATIVE_WAIT_DEADLINE/.test(error)),'native deadline was swallowed or renewed');assert.equal(f.starts(),1);assert.equal(f.stops(),0);assert.equal(f.host.enabledOwners.length,0);
  }finally{f.set(state());await old;await latest;await cleanup(f);}
 });
 console.log(JSON.stringify({ok:!failures.length,checks,failures,observations,hardwareCalls:0,scope:'actual-production-FanHostLifecycle; native-pending-Close/readiness/queue/deadline-fixtures; no hardware'}));if(failures.length)process.exitCode=1;
}
void main();
