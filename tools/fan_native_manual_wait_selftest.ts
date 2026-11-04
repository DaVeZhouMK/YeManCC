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
 owner=1;closedOwners:number[]=[];enabledOwners:number[]=[];presetOwners:number[]=[];trace:string[]=[];writtenCurves:FanNode[][]=[];
 settledMode:'ready'|'idle'|null=null;
 settled(mode:'ready'|'idle'){this.settledMode=mode;this.generation=6;this.attempt=1;this.powerState='On';this.stateName=mode==='ready'?'Ready':'AwaitingControl';this.opened=this.events=mode==='ready';this.closed=false;this.lease=null;}
 override snapshot(){return {...super.snapshot(),...(this.settledMode?{powerOperationOutcome:this.settledMode==='idle'?'await-user':'ready'}:{})};}
 constructor(){super();this.factoryType='HandheldCompanion.Devices.MSI.ClawCG3EM';this.stateName='Stopped';}
 half(){this.opened=true;this.events=false;this.closed=false;this.stateName='Open';}
 reset(){this.owner++;this.settledMode=null;this.opened=this.events=this.closed=false;this.lease=null;this.generation=this.attempt=0;this.stateName='Stopped';this.powerState='Unknown';}
 override async enable(nodes?:readonly FanNode[]){assert(this.opened&&this.events&&this.lease);this.enabledOwners.push(this.owner);this.writtenCurves.push(nodes?.map(n=>({...n}))??[]);return super.enable();}
 override async applyPreset(_name?:'soft'|'balanced'|'aggressive',_leaseId?:string,nodes?:readonly FanNode[]){assert(this.opened&&this.events&&this.lease);this.presetOwners.push(this.owner);this.writtenCurves.push(nodes?.map(n=>({...n}))??[]);return super.applyPreset();}
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
let checks=0;const failures:string[]=[],observations:unknown[]=[],selectedNames:string[]=[],executedNames:string[]=[];const caseFilter=process.env.YMCC_FAN_NATIVE_WAIT_CASE??'';async function check(name:string,run:()=>Promise<void>){if(caseFilter&&!name.includes(caseFilter))return;selectedNames.push(name);checks++;executedNames.push(name);try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
// The user had already been awake for 19m40s. These cases model only a few
// milliseconds of residual native cleanup, never a new wake or that elapsed wait.
type SettledMode='ready'|'idle';type SettledAction='start'|'apply'|'preset';
async function settledFixture(mode:SettledMode,budget=800,family='XboxROGAllyX'){
 const f=make(budget);f.host.factoryType='HandheldCompanion.Devices.'+family;await f.life.start({manualRecovery:true});f.host.settled(mode);
 // Seed the retained renderer sleep mirror; no real sleep or new Host is issued.
 f.life.observePowerBoundary('suspending',6);f.set(state('resuming'));f.host.calls.length=0;
 const admission=defer(),reasons:string[]=[],owner=f.life as any,original=owner.requireNativeWriteReadyForAdmission.bind(f.life);
 owner.requireNativeWriteReadyForAdmission=(context:unknown,reason:string)=>{reasons.push(reason);admission.resolve();return original(context,reason);};
 return {...f,admission,reasons,mode};
}
function settledAction(f:Awaited<ReturnType<typeof settledFixture>>,action:SettledAction){return action==='start'?f.life.start({manualRecovery:true}).then(()=>f.life.apply(curve)):action==='apply'?f.life.apply(curve):f.life.applyPreset('aggressive',curve);}
function noSettledWrites(f:Awaited<ReturnType<typeof settledFixture>>){assert.deepEqual(f.host.calls.filter(call=>['open','events','acquire','enable','preset','resume','close'].includes(call)),[],'native cleanup is not hardware write permission');assert.equal(f.starts(),1);assert.equal(f.stops(),0);assert.deepEqual(f.host.closedOwners,[]);}
function sameSettledOwner(f:Awaited<ReturnType<typeof settledFixture>>){assert.equal(f.starts(),1);assert.equal(f.stops(),0);assert.deepEqual(f.host.closedOwners,[]);assert(!f.host.calls.includes('resume'),'settled current receipt must not mint another Host resume');}
async function main(){
 for(const [mode,action,family] of [['ready','start','XboxROGAllyX'],['ready','apply','MSI.ClawCG3EM'],['ready','preset','Lenovo.LegionGo'],['idle','start','GPD.GPDWin5'],['idle','apply','OneXPlayerX1Mini'],['idle','preset','AYANEO2021']] as const)await check('settled current '+mode+' first '+action+' waits through residual native cleanup '+family,async()=>{
  const f=await settledFixture(mode,800,family);let pending:Promise<unknown>|undefined,finished=false,failure='';
  try{
   pending=settledAction(f,action).then(()=>{finished=true;return null;},error=>{finished=true;failure=String(error);return error;});
   await bounded(f.admission.promise);await delay(15);assert.equal(finished,false,'first action failed instead of waiting: '+failure);noSettledWrites(f);
   assert(f.reasons.includes(mode==='idle'?'idle-resume':'ready-resume'),'case missed the required native admission branch');
   f.set(state());const error=await bounded(pending);assert.equal(error,null,String(error));sameSettledOwner(f);
   observations.push({scenario:'settled-current-residual-cleanup',mode,action,family,admission:[...f.reasons],calls:[...f.host.calls],starts:f.starts(),stops:f.stops()});
   assert.equal(f.host.calls.filter(call=>call==='open').length,mode==='idle'?1:0);assert.equal(f.host.calls.filter(call=>call==='events').length,mode==='idle'?1:0);assert.equal(f.host.calls.filter(call=>call==='acquire').length,1);
   assert.deepEqual([...f.host.enabledOwners,...f.host.presetOwners],[1]);assert.deepEqual(f.host.writtenCurves,[curve],'recovery receipt must represent the exact requested curve');assert.equal(f.host.calls.filter(call=>call==='enable'||call==='preset').length,1,'one user action wrote the curve more than once');
  }finally{f.set(state());await pending;await cleanup(f);}
 });
 for(const mode of ['ready','idle'] as const)for(const denial of ['suspending','ready-no-write'] as const)await check('settled current '+mode+' rejects '+denial+' before hardware admission',async()=>{
  const f=await settledFixture(mode);try{
   f.set(denial==='suspending'?state('suspending'):state('ready',6,false));const error=await bounded(settledAction(f,mode==='ready'?'start':'preset').then(()=>null,error=>error));
   assert(error,'native write prohibition was treated as a usable session');noSettledWrites(f);
  }finally{await cleanup(f);}
 });
 for(const [mode,status] of [['ready',401],['idle',403]] as const)await check('settled current '+mode+' native wait preserves terminal '+status,async()=>{
  const f=await settledFixture(mode);const getState=f.host.getState.bind(f.host);let pending:Promise<unknown>|undefined;
  try{
   pending=settledAction(f,mode==='ready'?'start':'apply').then(()=>null,error=>error);await bounded(f.admission.promise);await delay(15);noSettledWrites(f);
   const refusal=new FanApiError('fixture settled authentication refusal',status,'API_SESSION_REQUIRED');f.host.getState=async()=>{f.host.calls.push('state');throw refusal;};f.set(state());
   const error=await bounded(pending);assert(error instanceof FanApiError&&error.status===status,'authentication refusal was retried or reclassified: '+String(error));noSettledWrites(f);
  }finally{f.host.getState=getState;f.set(state());await pending;await cleanup(f);}
 });
 for(const [mode,action,cancel] of [['ready','start','off'],['idle','apply','new-curve'],['ready','preset','new-generation']] as const)await check('settled current '+mode+' '+action+' wait is canceled by '+cancel,async()=>{
  const f=await settledFixture(mode,2000);let pending:Promise<unknown>|undefined;
  try{
   pending=settledAction(f,action).then(()=>null,error=>error);await bounded(f.admission.promise);await delay(15);noSettledWrites(f);
   if(cancel==='off')await bounded(f.life.disable(),150);else if(cancel==='new-curve')f.life.stagePendingControlCurve(curve.map(node=>({...node,dutyPercent:45})));
   else{f.set(state('suspending',7));f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   if(cancel!=='new-generation')f.set(state());const error=await bounded(pending,200);assert.match(String(error),/FAN_CONTROL_INTENT_SUPERSEDED/);noSettledWrites(f);
  }finally{f.set(state());await pending;await cleanup(f);}
 });
 for(const mode of ['ready','idle'] as const)await check('settled current '+mode+' native wait keeps one absolute deadline',async()=>{
  const f=await settledFixture(mode,300);let pending:Promise<unknown>|undefined,peer:Promise<unknown>|undefined;
  try{
   const started=performance.now();pending=f.life.start({manualRecovery:true}).then(()=>null,error=>error);await bounded(f.admission.promise);await delay(100);
   peer=f.life.start({manualRecovery:true}).then(()=>null,error=>error);const errors=await bounded(Promise.all([pending,peer]),800),elapsed=performance.now()-started;
   assert(errors.every(error=>/FAN_MANUAL_NATIVE_WAIT_DEADLINE/.test(String(error))),'pending admission renewed or lost its original deadline: '+errors.map(String).join(','));assert(elapsed>=250&&elapsed<550,'shared current Host wait renewed its absolute deadline');assert(f.reads()<12);noSettledWrites(f);
  }finally{f.set(state());await pending;await peer;await cleanup(f);}
 });
 await check('new boundary cold physical wake keeps first manual start',async()=>{
  const f=make(1000);f.host.factoryType='HandheldCompanion.Devices.XboxROGAllyX';let pending:Promise<{value:unknown,error:unknown}>|undefined;
  try{
   // Inactive matched OS sleep/wake facts retain resumeReady, but the cold Host
   // has no sleep receipt and native still owes its own serialized cleanup.
   f.set({...state('resuming'),resumeReady:true,physicalWakeObserved:true} as PowerLifecycleState);
   pending=f.life.start({manualRecovery:true}).then(()=>f.life.apply(curve)).then(value=>({value,error:null}),error=>({value:null,error}));
   await bounded(Promise.race([f.wake.promise,pending.then(out=>{throw out.error??Error('cold control completed before native cleanup');})]));await delay(15);
   assert.equal(f.life.hasControlIntent,false,'startup invented an already applied curve');assert.equal(f.host.calls.filter(call=>['open','events','acquire','enable','preset','resume','close'].includes(call)).length,0);
   f.set(state());const out=await bounded(pending);assert.equal(out.error,null,String(out.error));assert.equal(f.starts(),1);assert.equal(f.stops(),0);assert.deepEqual(f.host.closedOwners,[]);assert(!f.host.calls.includes('resume'),'cold Host was asked for a nonexistent sleep receipt');
   for(const call of ['open','events','acquire','enable'])assert.equal(f.host.calls.filter(value=>value===call).length,1);assert.deepEqual(f.host.writtenCurves,[curve]);
   observations.push({scenario:'cold-physical-native-cleanup',calls:[...f.host.calls],wakeCalls:f.wakeCalls(),starts:f.starts()});
  }finally{f.set(state());await pending;await cleanup(f);}
 });
 await check('new boundary completed recovery receipt cannot cross same generation wake epoch',async()=>{
  const f=await settledFixture('ready',1600),front=defer(),release=defer(),completed=defer(),owner=f.life as any;
  const wait=owner.enqueueWithManualNativeWakeWait.bind(f.life),settle=owner.settleRecoveryTaskIfAny.bind(f.life);let firstDirect=true,pending:Promise<{value:unknown,error:unknown}>|undefined;
  owner.enqueueWithManualNativeWakeWait=async(work:()=>Promise<unknown>,queued=true)=>{const park=!queued&&firstDirect;if(park)firstDirect=false;const value=await wait(work,queued);if(park){front.resolve();await release.promise;}return value;};
  owner.settleRecoveryTaskIfAny=(reason?:string)=>{const value=settle(reason);if(reason==='curve-written')completed.resolve();return value;};
  try{
   pending=f.life.apply(curve).then(value=>({value,error:null}),error=>({value:null,error}));await bounded(f.admission.promise);f.set(state());await bounded(front.promise);await bounded(completed.promise);
   const receipt=owner.completedRecoveryWrite;assert(receipt,'the real recovery owner did not produce its completion');assert.deepEqual(f.host.writtenCurves,[curve]);
   f.set(state('resuming'));f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.host.stateName='AwaitingControl';const writes=f.host.writtenCurves.length;let earlySuccess=false;void pending.then(out=>{earlySuccess=out.error===null;});release.resolve();await delay(15);
   assert.equal(f.host.writtenCurves.length,writes,'same-generation wake bypassed native write admission');assert.equal(earlySuccess,false,'old completion settled a request before current native readiness');
   f.set(state());f.life.observePowerBoundary('resume-ready',6);const out=await bounded(pending);assert(out.error!==null||out.value!==receipt.response,'old epoch response was reused as current success');
   if(out.error===null){assert.equal(f.host.writtenCurves.length,2);assert.deepEqual(f.host.writtenCurves,[curve,curve]);}else assert.match(String(out.error),/SUPERSEDED|RESULT_UNKNOWN|POWER_RESUMING|^Error: native-resume-ready-not-observed:generation=6:ready=none$/);
   sameSettledOwner(f);observations.push({scenario:'completed-recovery-new-epoch',error:out.error?String(out.error):'',writes:f.host.writtenCurves.length});
  }finally{release.resolve();f.set(state());await pending;owner.enqueueWithManualNativeWakeWait=wait;owner.settleRecoveryTaskIfAny=settle;await cleanup(f);}
 });
 await check('new boundary idle close rejects captured native ready after next generation starts',async()=>{
  const f=await settledFixture('idle',1600),captured=defer(),release=defer(),owner=f.life as any,read=owner.readNativePowerState.bind(f.life);let hold=true,pending:Promise<unknown>|undefined,latest:Promise<unknown>|undefined;
  try{
   pending=f.life.apply(curve).then(()=>null,error=>error);await bounded(f.admission.promise);await delay(15);noSettledWrites(f);
   owner.readNativePowerState=async(timeoutMs?:number)=>{const snapshot=await read(timeoutMs);if(hold&&snapshot?.phase==='ready'&&snapshot.generation===6){hold=false;captured.resolve();await release.promise;}return snapshot;};
   f.set(state());await bounded(captured.promise);await bounded(f.life.close(),200);f.set(state('ready',7));f.life.setPowerGeneration(7);f.life.observePowerBoundary('resume-ready',7);
   const next=curve.map(node=>({...node,dutyPercent:45}));latest=f.life.start({manualRecovery:true}).then(()=>f.life.apply(next)).then(()=>null,error=>error);const current=await bounded(latest);assert.equal(current,null,String(current));
   release.resolve();const error=await bounded(pending,200);assert.match(String(error),/FAN_CONTROL_INTENT_SUPERSEDED/);assert.equal(f.starts(),2);assert.equal(f.stops(),1);assert.deepEqual(f.host.closedOwners,[1]);assert.deepEqual(f.host.enabledOwners,[2]);assert.deepEqual(f.host.writtenCurves,[next]);
   observations.push({scenario:'close-captured-native-ready',oldGeneration:6,currentGeneration:7,calls:[...f.host.calls],trace:[...f.host.trace]});
  }finally{release.resolve();owner.readNativePowerState=read;f.set(state());await pending;await latest;await cleanup(f);}
 });
 await check('new boundary new curve retries manual wake after same generation deadline',async()=>{
  const f=await settledFixture('ready',300),owner=f.life as any,wake=owner.requestManualWake,requested=defer(),calls:{generation:number|undefined,revision:number}[]=[];let old:Promise<unknown>|undefined,latest:Promise<unknown>|undefined;
  owner.requestManualWake=async(generation?:number)=>{calls.push({generation,revision:f.life.controlIntentRevision});const ready=calls.length>1;f.set(state(ready?'ready':'resuming'));requested.resolve();return {ok:true,generation,phase:ready?'ready':'resuming'};};
  try{
   // Acknowledgement is only intent admission. The first native scheduling
   // attempt remains pending until this request's original deadline expires.
   old=f.life.apply(curve).then(()=>null,error=>error);const firstRevision=f.life.controlIntentRevision;await bounded(requested.promise);const expired=await bounded(old,900);
   assert.match(String(expired),/FAN_MANUAL_NATIVE_WAIT_DEADLINE/);assert.deepEqual(calls,[{generation:6,revision:firstRevision}]);noSettledWrites(f);
   const next=curve.map(node=>({...node,dutyPercent:45}));latest=f.life.apply(next).then(()=>null,error=>error);const nextRevision=f.life.controlIntentRevision;assert(nextRevision>firstRevision);
   const error=await bounded(latest,900);assert.equal(error,null,'new explicit curve did not retry native wake: '+String(error));assert.deepEqual(calls,[{generation:6,revision:firstRevision},{generation:6,revision:nextRevision}],'each explicit intent owns exactly one manual native request');
   assert.equal(await old,expired,'expired request was revived by the next wake acknowledgement');sameSettledOwner(f);assert.deepEqual(f.host.writtenCurves,[next]);assert.deepEqual(f.host.enabledOwners,[1]);assert.equal(f.host.calls.filter(call=>call==='acquire').length,1);assert.equal(f.host.calls.filter(call=>call==='enable'||call==='preset').length,1);
   observations.push({scenario:'new-intent-manual-wake-retry',calls,firstError:String(expired),writtenCurves:f.host.writtenCurves,starts:f.starts(),stops:f.stops()});
  }finally{f.set(state());await old;await latest;owner.requestManualWake=wake;await cleanup(f);}
 });
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
 if(selectedNames.length===0)failures.push('FAN_NATIVE_WAIT_EMPTY_SELECTION:'+caseFilter);
 assert.deepEqual(executedNames,selectedNames,'selected scenarios and actual execution differ');
 console.log(JSON.stringify({ok:!failures.length,checks,caseFilter,selectedNames,executedNames,failures,observations,hardwareCalls:0,scope:'actual-production-FanHostLifecycle; native-pending-Close/readiness/queue/deadline-fixtures; no hardware'}));if(failures.length)process.exitCode=1;
}
void main();
