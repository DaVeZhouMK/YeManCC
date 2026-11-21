/** Actual FanHostLifecycle and public native-edge sequence; inert Host only.
 * Frozen curve telemetry models HostState.ActiveCurve after successful Enable.
 */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve:FanNode[]=[{tempC:0,dutyPercent:25},{tempC:40,dutyPercent:40},{tempC:70,dutyPercent:70},{tempC:100,dutyPercent:100}],newer=curve.map(n=>({...n,dutyPercent:80}));
function makeReplyGate() {
  let release!: () => void;
  const pending = new Promise<void>(resolve => { release = resolve; });
  return { entered: false, returned: false, release, pending };
}
class ReceiptHost extends NoIoHost{
 active:FanNode[]|null=null;writes=false;writeEvidence:unknown='transported';readShape='full';fault=false;enableFailure=false;written:FanNode[][]=[];writtenGenerations:number[]=[];holdNextState=false;stateEntered=false;releaseState:(()=>void)|null=null;
 private stateGates = new Map<number, ReturnType<typeof makeReplyGate>>();
 private enableReceiptGate: ReturnType<typeof makeReplyGate> | null = null;
 holdGenerationState(generation: number) {
   const gate = makeReplyGate();
   assert(!this.stateGates.has(generation), 'only one held Host read per generation');
   this.stateGates.set(generation, gate);
   return gate;
 }
 holdNextEnableReceipt() {
   assert.equal(this.enableReceiptGate,null);
   return this.enableReceiptGate=makeReplyGate();
 }
 override snapshot():FanState{const state:FanState={...super.snapshot(),hardwareWritesEnabled:this.writes,hardwareWrites:this.writes,hardwareWritesObserved:true,lastControlWriteEvidence:this.writeEvidence,activeCurve:this.active?.map(n=>({...n}))??null,oemRestoreConfirmed:false,unknownState:this.fault};if(this.readShape==='missing')delete state.activeCurve;if(this.readShape==='malformed')state.activeCurve=[null];if(this.readShape==='changed')state.activeCurve=newer.map(n=>({...n}));return state;}
 override async getState(){
   this.calls.push('state');
   const state = this.snapshot(), gate = this.stateGates.get(this.generation);
   if (gate) {
     this.stateGates.delete(this.generation);
     gate.entered = true;
     await gate.pending;
     gate.returned = true;
   } else if(this.holdNextState){this.holdNextState=false;this.stateEntered=true;await new Promise<void>(r=>this.releaseState=r);}
   return state;
 }
 override async enable(nodes?:readonly FanNode[],id?:string){
   this.calls.push('enable');if(this.enableFailure||this.fault)throw new FanApiError('HC_FAULT_LOCKED',409,'FAULT_LOCKED');if(this.lease?.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');this.active=nodes?.map(n=>({...n}))??null;this.writes=true;this.stateName='Ready';this.written.push(this.active??[]);this.writtenGenerations.push(this.generation);
   const receipt=this.snapshot(),gate=this.enableReceiptGate;
   if(gate){this.enableReceiptGate=null;gate.entered=true;await gate.pending;gate.returned=true;}
   return receipt;
 }
 override async applyPreset(_name?:string,id?:string,nodes?:readonly FanNode[]){return this.enable(nodes,id);}
 override async suspend(){this.active=null;this.writes=false;this.lease=null;return super.suspend();}
 override async restoreOem(){this.active=null;this.writes=false;return super.restoreOem();}
 override async close(){this.writes=false;this.active=null;return super.close();}
}
async function setup(family='ROGAlly'){
 const h=new ReceiptHost();h.factoryType='HandheldCompanion.Devices.'+family;h.completeResume=true;let generation=1,starts=0,stops=0;
 const l=new FanHostLifecycle({enabled:true,adapter:h,heartbeatIntervalMs:0,resumeWaitDeadlineMs:500,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation,phase:'ready',resumeReady:true,inputReady:true,hardwareWritesAllowed:true,hibernateAvailable:true}),launcher:{start:async()=>{starts++;return{pid:1064,executable:'inert.exe'};},stop:async()=>{stops++;}}});
 l.setPowerGeneration(1);l.observePowerBoundary('resume-ready',1);await l.start();await l.apply(curve);
 const publishReady=async(g:number)=>{generation=g;l.setPowerGeneration(g);l.observePowerBoundary('suspending',g);await h.suspend();await h.resume({generation:g});l.observePowerBoundary('resuming',g);l.observePowerBoundary('resume-ready',g);};
 const wake=async(g:number,entry:'resume'|'committed'='resume')=>{generation=g;l.setPowerGeneration(g);l.observePowerBoundary('suspending',g);await h.suspend();await l.suspend();await h.resume({generation:g});l.observePowerBoundary('resuming',g);l.observePowerBoundary('resume-ready',g);if(entry==='committed')await l.resumeAfterCommittedPowerWake(g);else await l.resume();};
 await wake(2);return{h,l,wake,publishReady,count:()=>h.calls.filter(c=>c==='enable').length,counts:()=>({starts,stops})};
}
const cases:{name:string,ok:boolean,error?:string,evidence?:unknown}[]=[];async function check(name:string,run:()=>Promise<unknown>){try{const evidence=await run();cases.push({name,ok:true,evidence});}catch(e){cases.push({name,ok:false,error:String(e)});}}
for(const family of ['ROGAlly','ClawCG3EM','GPDWin5','OneXPlayerX1Mini'])await check(family+': F5, committed wake and late Ready replay exactly once',async()=>{const f=await setup(family);try{assert.equal(f.count(),2,'one initial enable and one actual wake replay');const reads=f.h.calls.filter(c=>c==='state').length;f.l.observePowerBoundary('resumed',2);await f.l.resumeAfterCommittedPowerWake(2);assert.equal(f.count(),2,'committed fallback rewrote an already proven current curve');f.l.observePowerBoundary('resume-ready',2);await f.l.resume();assert.equal(f.count(),2,'late Ready rewrote the same current curve');assert.equal(f.l.state,'ready');assert.equal(f.l.coordinatorSnapshot.phase,'ready');assert.equal(f.h.writes,true);assert.equal(f.counts().starts,1);return{enables:f.count(),additionalStateReads:f.h.calls.filter(c=>c==='state').length-reads,counts:f.counts()};}finally{await f.l.close();}});
for(const action of ['apply','preset'])await check('same-curve manual '+action+' still reaches a real write after duplicate wakes',async()=>{const f=await setup();try{f.l.observePowerBoundary('resume-ready',2);await f.l.resume();const before=f.count();if(action==='apply')await f.l.apply(curve);else await f.l.applyPreset('aggressive',curve);assert.equal(f.count(),before+1,'manual operation was incorrectly deduplicated');assert.deepEqual(f.h.written.at(-1),curve);}finally{await f.l.close();}});
await check('new real sleep needs a new replay even with the identical curve',async()=>{const f=await setup();try{const before=f.count();await f.wake(3);assert.equal(f.count(),before+1,'earlier successful generation blocked the next real recovery');assert.equal(f.l.coordinatorSnapshot.powerGeneration,3);}finally{await f.l.close();}});
for(const shape of ['missing','malformed','changed','writes-off','lease-lost','lease-replaced','unknown'])await check('incomplete '+shape+' evidence cannot suppress recovery',async()=>{const f=await setup();try{const before=f.count();if(shape==='writes-off')f.h.writes=false;else if(shape==='lease-lost')f.h.lease=null;else if(shape==='lease-replaced')f.h.lease={leaseId:'different-current-lease',generation:999};else if(shape==='unknown')f.h.fault=true;else f.h.readShape=shape;let error:unknown;f.l.observePowerBoundary('resume-ready',2);try{await f.l.resume();}catch(e){error=e;}if(shape==='unknown'){assert(error,'known fault cannot be declared recovered');assert.equal(f.h.written.length,before,'known fault may not accept another write');}else{assert.equal(error,undefined);assert.equal(f.count(),before+1,'unproven curve was treated as already effective');if(shape==='lease-lost')assert(f.l.currentLease,'missing lease must be reacquired');}}finally{f.h.fault=false;f.h.readShape='full';await f.l.close().catch(()=>{});}});
await check('a staged newer intent still applies when only the remote curve was updated',async()=>{const f=await setup();try{const before=f.count();f.l.stagePendingControlCurve(newer);f.h.active=newer.map(n=>({...n}));f.l.observePowerBoundary('resume-ready',2);await f.l.resume();assert.equal(f.count(),before+1,'old local control receipt was borrowed by a staged new intent');assert.deepEqual(f.h.written.at(-1),newer);}finally{await f.l.close();}});
await check('a new generation without completed local control cannot borrow old success',async()=>{const f=await setup();try{const before=f.count();f.l.setPowerGeneration(3);f.h.generation=3;f.l.observePowerBoundary('resume-ready',3);await f.l.resume();assert.equal(f.count(),before+1,'old generation success suppressed current generation replay');}finally{await f.l.close();}});
await check('a newer manual curve survives late committed wake without another identical write',async()=>{const f=await setup();try{await f.l.apply(newer);const before=f.count();f.l.observePowerBoundary('resumed',2);await f.l.resumeAfterCommittedPowerWake(2);assert.equal(f.count(),before);assert.deepEqual(f.h.active,newer);}finally{await f.l.close();}});
for(const evidence of ['partial-channel-evidence-not-success','unknown-not-verified','unknown-not-verified-no-receipts','unknown-not-verified-compat-precondition','unknown-not-verified-route-undeclared','unknown-not-verified-receipt-api-fault','failed-write-not-established','unsupported-no-scope-capability','none',undefined,null,17,'future-unknown-verdict'])await check('current write evidence '+String(evidence)+' cannot suppress recovery',async()=>{const f=await setup();try{f.h.writeEvidence=evidence;const before=f.count();f.l.observePowerBoundary('resume-ready',2);await f.l.resume();assert.equal(f.count(),before+1,'diagnostic/incomplete write evidence was promoted to a completed automatic replay');assert.equal(f.h.writes,true,'incomplete evidence must not cancel the control intent');assert.equal(f.l.hasControlIntent,true);}finally{await f.l.close();}});
for(const evidence of ['transported','compat-route-callback-returned-no-scope'])await check('qualified '+evidence+' write evidence keeps duplicate notifications read-only',async()=>{const f=await setup(evidence==='transported'?'ROGAlly':'ClawCG3EM');try{f.h.writeEvidence=evidence;await f.l.apply(curve);const before=f.count();f.l.observePowerBoundary('resume-ready',2);await f.l.resume();assert.equal(f.count(),before);assert.equal(f.h.writes,true);}finally{await f.l.close();}});
for(const evidence of ['partial-channel-evidence-not-success','unknown-not-verified',undefined])await check('earlier '+String(evidence)+' local receipt cannot borrow later complete readback',async()=>{const f=await setup();try{f.h.writeEvidence=evidence;await f.l.apply(curve);f.h.writeEvidence='transported';const before=f.count();f.l.observePowerBoundary('resume-ready',2);await f.l.resume();assert.equal(f.count(),before+1,'local incomplete write was upgraded by an unrelated later evidence field');}finally{await f.l.close();}});
for(const evidence of ['partial-channel-evidence-not-success','unknown-not-verified',undefined])await check('explicit manual write stays reachable with '+String(evidence)+' evidence',async()=>{const f=await setup();try{f.h.writeEvidence=evidence;const before=f.count();await f.l.apply(curve);assert.equal(f.count(),before+1);assert.equal(f.l.hasControlIntent,true);assert.equal(f.h.writes,true);}finally{await f.l.close();}});
for(const action of ['off','new-curve','sleep'])await check('late duplicate wake GET cannot cross newer '+action,async()=>{const f=await setup();try{f.h.holdNextState=true;const old=f.l.resume().then(()=> 'accepted',e=>String(e));for(let i=0;i<80&&!f.h.stateEntered;i++)await Promise.resolve();assert(f.h.stateEntered);let newerTask:Promise<unknown>=Promise.resolve();if(action==='off')newerTask=f.l.disable();else if(action==='new-curve')newerTask=f.l.apply(newer);else{f.l.setPowerGeneration(3);f.l.observePowerBoundary('suspending',3);await f.h.suspend();newerTask=f.l.suspend();}f.h.releaseState!();await old;await newerTask;if(action==='off'){assert.equal(f.l.hasControlIntent,false);assert.equal(f.h.lease,null);}else if(action==='new-curve'){assert.deepEqual(f.h.written.at(-1),newer);assert.equal(f.h.writes,true);}else{assert.equal(f.h.writes,false);assert.equal(f.l.coordinatorSnapshot.wakeReadyGeneration,null);}assert.equal(f.counts().starts,1);}finally{await f.l.close().catch(()=>{});}});
async function until(condition:()=>boolean,message:string){for(let i=0;i<300&&!condition();i++)await Promise.resolve();assert(condition(),message);}
for(const family of ['ROGAlly','ClawCG3EM','GPDWin5','OneXPlayerX1Mini'])for(const entry of ['resume','committed'] as const)await check(family+': a new '+entry+' wake cannot inherit the previous generation in-flight result',async()=>{
 const f=await setup(family), held=f.h.holdGenerationState(3);
 const old=f.wake(3).then(()=> 'accepted',e=>String(e));
 let latest:Promise<string>|undefined, newOutcome:string|undefined;
 try {
   await until(()=>held.entered,'old resume must enter its own held Host read');
   latest=f.wake(4,entry).then(()=> 'accepted',e=>String(e)).then(outcome => newOutcome=outcome);
   await until(()=>newOutcome!==undefined,'new authorized wake must finish without the retired Host reply');
   assert.equal(newOutcome,'accepted');
   assert.equal(held.returned,false);
   const oldOutcome=await old;
   assert.match(oldOutcome,/FAN_CONTROL_INTENT_SUPERSEDED/);
   const lease=f.l.currentLease;
   assert(lease,'current wake must hold its own lease');
   held.release();
   await until(()=>held.returned,'deliver the obsolete Host reply after the new curve');
   assert.deepEqual(f.l.currentLease,lease,'obsolete reply must not replace the current lease');
   assert.equal(f.l.state,'ready');
   assert.equal(f.h.writes,true);
   assert.deepEqual(f.h.writtenGenerations,[1,2,4]);
   assert.equal(f.counts().starts,1);
   return {oldOutcome,newOutcome,writtenGenerations:f.h.writtenGenerations,hostCount:f.counts(),oldReplyPendingAtCurrentCompletion:true};
 }finally{held.release();await old;await latest?.catch(()=>{});await f.l.close().catch(()=>{});}
});
for(const cancel of ['off','new-curve'] as const)await check('cross-generation resume handoff respects a newer '+cancel,async()=>{
 const f=await setup(), third=f.h.holdGenerationState(3), fourth=f.h.holdGenerationState(4);
 const old=f.wake(3).then(()=> 'accepted',e=>String(e));
 let latest:Promise<string>|undefined, outcome:string|undefined;
 try {
   await until(()=>third.entered,'third generation must hold its Host read');
   latest=f.wake(4).then(()=> 'accepted',e=>String(e)).then(value => outcome=value);
   await until(()=>fourth.entered,'fourth generation must independently reach its own admission');
   assert.equal(outcome,undefined,'automatic fourth generation must still be waiting before explicit input');
   if(cancel==='off') await f.l.disable();
   else { await f.l.start({manualRecovery:true,reason:'fixture-current-explicit-curve'}); await f.l.apply(newer); }
   await until(()=>outcome!==undefined,'explicit input did not retire the automatic Host observation');
   assert.match(outcome!,/FAN_CONTROL_INTENT_SUPERSEDED/);
   assert.match(await old,/FAN_CONTROL_INTENT_SUPERSEDED/);
   assert.equal(third.returned||fourth.returned,false);
   third.release(); fourth.release();
   await until(()=>third.returned&&fourth.returned,'deliver both obsolete Host replies');
   assert.deepEqual(f.h.writtenGenerations,cancel==='off'?[1,2]:[1,2,4]);
   assert.equal(f.counts().starts,1);
   if(cancel==='off'){assert.equal(f.l.hasControlIntent,false);assert.equal(f.l.currentLease,null);assert.equal(f.h.writes,false);}
   else{assert.deepEqual(f.h.active,newer);assert.deepEqual(f.h.written.at(-1),newer);assert.equal(f.h.writes,true);}
   return {outcome,writtenGenerations:f.h.writtenGenerations};
 }finally{third.release();fourth.release();await old;await latest?.catch(()=>{});await f.l.close().catch(()=>{});}
});
await check('three overlapping wake generations retain only the newest valid resume request',async()=>{
 const f=await setup(), third=f.h.holdGenerationState(3), fourth=f.h.holdGenerationState(4);
 const old=f.wake(3).then(()=> 'accepted',e=>String(e));
 let middle:Promise<string>|undefined, latest:Promise<string>|undefined, finalOutcome:string|undefined;
 try {
   await until(()=>third.entered,'third generation must enter its held Host read');
   middle=f.wake(4).then(()=> 'accepted',e=>String(e));
   await until(()=>fourth.entered,'fourth generation must reach its own admission before sleep five');
   latest=f.wake(5,'committed').then(()=> 'accepted',e=>String(e)).then(value => finalOutcome=value);
   await until(()=>finalOutcome!==undefined,'fifth generation must finish despite both retired Host reads');
   const outcomes=await Promise.all([old,middle,latest]);
   assert.match(outcomes[0],/FAN_CONTROL_INTENT_SUPERSEDED/);
   assert.match(outcomes[1],/FAN_CONTROL_INTENT_SUPERSEDED/);
   assert.equal(outcomes[2],'accepted');
   assert.equal(third.returned||fourth.returned,false);
   fourth.release(); third.release();
   await until(()=>third.returned&&fourth.returned,'deliver obsolete Host replies in reverse generation order');
   assert.deepEqual(f.h.writtenGenerations,[1,2,5]);
   assert.equal(f.h.writes,true);
   assert.equal(f.l.state,'ready');
   assert.equal(f.counts().starts,1);
   return {outcomes,writtenGenerations:f.h.writtenGenerations};
 }finally{third.release();fourth.release();await old;await middle?.catch(()=>{});await latest?.catch(()=>{});await f.l.close().catch(()=>{});}
});
await check('same-generation in-flight notifications still share one original resume',async()=>{
 const f=await setup();f.h.holdNextState=true;const first=f.wake(3).then(()=> 'accepted',e=>String(e));let duplicate:Promise<string>|null=null;
 try{await until(()=>f.h.stateEntered,'first read not held');const before=f.count();duplicate=f.l.resume().then(()=> 'accepted',e=>String(e));for(let i=0;i<10;i++)await Promise.resolve();f.h.releaseState!();assert.deepEqual(await Promise.all([first,duplicate]),['accepted','accepted']);assert.equal(f.count(),before+1);assert.equal(f.h.writtenGenerations.at(-1),3);return {writtenGenerations:f.h.writtenGenerations};}
 finally{f.h.releaseState?.();await first;await duplicate?.catch(()=>{});await f.l.close().catch(()=>{});}
});
await check('new authorized wake waits for a submitted write receipt before dispatching a successor',async()=>{
 const f=await setup(), receipt=f.h.holdNextEnableReceipt();
 const old=f.wake(3).then(()=> 'accepted',e=>String(e));
 let next:Promise<string>|undefined, outcome:string|undefined;
 try {
   await until(()=>receipt.entered,'third generation must have submitted the actual curve before sleep four');
   // Model the App's Native notifications without enqueuing renderer suspend().
   await f.publishReady(4);
   const before=f.h.calls.length;
   next=f.l.resumeAfterCommittedPowerWake(4).then(()=> 'accepted',e=>String(e)).then(value => outcome=value);
   for(let i=0;i<100;i++)await Promise.resolve();
   assert.equal(outcome,undefined,'old write receipt must not be reused by the newer wake');
   assert.equal(receipt.returned,false);
   assert.deepEqual(f.h.calls.slice(before),[],'successor admission must not overlap the submitted writer');
   assert.deepEqual(f.h.writtenGenerations,[1,2,3]);
   receipt.release();
   const oldOutcome=await old;
   assert.match(oldOutcome,/FAN_CONTROL_INTENT_SUPERSEDED/);
   assert.equal(await next,'accepted');
   assert.deepEqual(f.h.writtenGenerations,[1,2,3,4]);
   assert.equal(f.l.state,'ready');
   assert.equal(f.counts().starts,1);
   return {oldOutcome,outcome,writtenGenerations:f.h.writtenGenerations};
 }finally{receipt.release();await old;await next?.catch(()=>{});await f.l.close().catch(()=>{});}
});
const result={ok:cases.every(c=>c.ok),checks:cases.length,cases,hardwareCalls:0,cpuBenchmark:false,scope:'real bridge public resume/commit/Ready/apply/preset/OFF/sleep; inert Host receipts with real-style activeCurve; no physical proof'};console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
