/** Real failed mutations/heartbeat + delayed readback; no production Host or device. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve=(n:number)=>[0,40,70,100].map(tempC=>({tempC,dutyPercent:n}));
const held=<T>()=>{let resolve!:(v:T)=>void,enter!:()=>void;return {promise:new Promise<T>(r=>resolve=r),ready:new Promise<void>(r=>enter=r),resolve:(v:T)=>resolve(v),enter:()=>enter()};};
const settled=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
const flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();};
async function fixture(cold=false){
 const host=new NoIoHost();let native:any={generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
 const writes:FanNode[][]=[];const originalEnable=host.enable.bind(host);host.enable=async(nodes:readonly FanNode[]=[])=>{writes.push(nodes.map(n=>({...n})));return originalEnable();};
 host.applyPreset=async(_name?:string,_lease?:string,nodes?:readonly FanNode[])=>{host.calls.push('preset');return host.enable(nodes);};
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,readNativePowerState:async()=>native,launcher:{async start(){return{pid:116401,executable:'inert.exe'};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});if(!cold){await life.apply(curve(100));host.generation=6;host.attempt=1;host.powerState='On';}
 return{life,host,writes,observations:[] as unknown[],setNative:(v:any)=>native=v,async cleanup(){await life.close().catch(()=>{});}};
}
const results:any[]=[];
async function check(name:string,run:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>){if(process.env.YMCC_FAN_FAILED_READ_CASE&&!name.includes(process.env.YMCC_FAN_FAILED_READ_CASE))return;const f=await fixture();try{results.push({name,ok:true,evidence:await run(f)});}catch(e){results.push({name,ok:false,error:String(e),calls:f.host.calls.slice(),observations:f.observations});}finally{await f.cleanup();}}
async function main(){
 for(const op of ['apply','preset','heartbeat'] as const)for(const edge of ['curve','off','close','sleep','generation','resuming'] as const)for(const when of ['during-write','during-state'] as const)await check(`${op}: retired ${when} by ${edge}`,async f=>{
  const write=held<void>(),read=held<FanState>();let retiredAdoptions=0,inResync=false,failureReads=0,resyncOrdinal=0,activeResyncOrdinal=0;
  const originalRead=f.host.getState.bind(f.host);
  const stale={...f.host.snapshot(),state:'AwaitingControl',openCalled:false,openEventsCalled:false,lease:null,leaseGeneration:null};
  const owner=f.life as any,originalSync=owner.syncRemoteSessionState.bind(owner);
  owner.syncRemoteSessionState=(remote:any,...args:any[])=>{const before=owner.stateObservationRevision;const result=originalSync(remote,...args);if(remote===stale&&owner.stateObservationRevision!==before)retiredAdoptions++;return result;};
  const originalResync=owner.resyncSessionFromHost.bind(owner);
  owner.resyncSessionFromHost=async(...args:any[])=>{inResync=true;activeResyncOrdinal=++resyncOrdinal;try{return await originalResync(...args);}finally{inResync=false;}};
  f.host.getState=async()=>{if(inResync&&activeResyncOrdinal===1){failureReads++;f.host.calls.push('state');read.enter();return when==='during-state'?read.promise:stale;}return originalRead();};
  const original=f.host[op==='apply'?'enable':op==='preset'?'applyPreset':'heartbeat'].bind(f.host);
  const fail=async(...args:any[])=>{f.host.calls.push('failed-'+op);if(op==='apply')f.host.enable=original;else if(op==='preset')f.host.applyPreset=original;else f.host.heartbeat=original;write.enter();if(when==='during-write')await write.promise;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
  if(op==='apply')f.host.enable=fail;else if(op==='preset')f.host.applyPreset=fail;else f.host.heartbeat=fail;
  const operation=settled(op==='apply'?f.life.apply(curve(55)):op==='preset'?f.life.applyPreset('soft',curve(55)):f.life.heartbeat());
  await (when==='during-write'?write.ready:read.ready);
  let terminal:ReturnType<typeof settled>|null=null;
  if(edge==='curve')f.life.stagePendingControlCurve(curve(75));
  else if(edge==='off')terminal=settled(f.life.disable());
  else if(edge==='close')terminal=settled(f.life.close());
  else if(edge==='sleep')f.life.observePowerBoundary('suspending',6);
  else if(edge==='generation')f.life.setPowerGeneration(7);
  else {f.setNative({generation:6,phase:'resuming',hardwareWritesAllowed:false,resumeReady:false});f.life.observePowerBoundary('resuming',6);}
  write.resolve();read.resolve(stale);
  if(edge==='resuming'){await flush();f.life.stagePendingControlCurve(curve(75));}
  await operation;if(terminal)await terminal;
  f.observations.push({op,edge,when,retiredAdoptions,failureReads,writes:f.writes.map(nodes=>nodes[0]?.dutyPercent)});
  try{
   assert(!f.writes.some(nodes=>nodes[0]?.dutyPercent===55),'cancelled old curve was retried after the user/power boundary');
   assert.equal(retiredAdoptions,0,'retired failure snapshot changed current session/display');
   if(when==='during-write')assert.equal(failureReads,0,'already-retired failure still queried Host');
   if(edge==='curve'){
    if(op==='apply')f.host.enable=original;else if(op==='preset')f.host.applyPreset=original;else f.host.heartbeat=original;
    const begin=f.host.calls.length;await f.life.apply(curve(75));assert.equal(f.life.state,'ready');assert.deepEqual(f.writes.at(-1),curve(75));
    assert.equal(f.host.calls.slice(begin).filter(x=>x==='open'||x==='events').length,0,'stale failed read forced unnecessary HC Open');
   }
   return{retiredAdoptions,failureReads};
  }finally{write.resolve();read.resolve(stale);}
 });
 for(const op of ['apply','preset'] as const)for(const edge of ['curve','off','close'] as const)await check(`${op}: retry queued before ${edge} cannot borrow the new intent`,async f=>{
  const read=held<FanState>(),block=held<void>();const owner=f.life as any;
  const originalRead=f.host.getState.bind(f.host),originalResync=owner.resyncSessionFromHost.bind(owner);let inResync=false,readOnce=true;
  owner.resyncSessionFromHost=async(...args:any[])=>{inResync=true;try{return await originalResync(...args);}finally{inResync=false;}};
  f.host.getState=async()=>{if(inResync&&readOnce){readOnce=false;read.enter();return read.promise;}return originalRead();};
  const original=f.host[op==='apply'?'enable':'applyPreset'].bind(f.host);
  const fail=async()=>{if(op==='apply')f.host.enable=original;else f.host.applyPreset=original;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
  if(op==='apply')f.host.enable=fail;else f.host.applyPreset=fail;
  const work=settled(op==='apply'?f.life.apply(curve(55)):f.life.applyPreset('soft',curve(55)));
  try{
   await read.ready;const blocker=owner.enqueue(async()=>{block.enter();await block.promise;});read.resolve(f.host.snapshot());await block.ready;await flush();
   let terminal:ReturnType<typeof settled>|null=null;
   if(edge==='curve')f.life.stagePendingControlCurve(curve(75));else if(edge==='off')terminal=settled(f.life.disable());else terminal=settled(f.life.close());
   block.resolve();await blocker;await work;if(terminal)await terminal;
   assert(!f.writes.some(nodes=>nodes[0]?.dutyPercent===55),'queued retry rewrote a retired curve');
   if(edge==='curve'){await f.life.apply(curve(75));assert.deepEqual(f.writes.at(-1),curve(75));}
   return{writes:f.writes.map(nodes=>nodes[0]?.dutyPercent)};
  }finally{read.resolve(f.host.snapshot());block.resolve();await work;}
 });
 for(const op of ['apply','preset','heartbeat'] as const)await check(`${op}: current failure still synchronizes a really closed session`,async f=>{
  let fail=true;const key=op==='apply'?'enable':op==='preset'?'applyPreset':'heartbeat',original=f.host[key].bind(f.host);
  f.host[key]=async(...args:any[])=>{if(fail){fail=false;f.host.opened=f.host.events=false;f.host.lease=null;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');}return original(...args);};
  const begin=f.host.calls.length;await settled(op==='apply'?f.life.apply(curve(60)):op==='preset'?f.life.applyPreset('soft',curve(60)):f.life.heartbeat());
  const calls=f.host.calls.slice(begin);assert.equal(f.life.state,'ready');assert.equal(calls.filter(x=>x==='open').length,1);assert.equal(calls.filter(x=>x==='events').length,1);return{calls,...(op==='apply'?{coldSource:await checkColdSourceReentry(),settledSource:await checkSettledSourceRecovery()}: {})};
 });
 console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,scope:'real public mutation/preset/heartbeat with inert Host/native/launcher; delayed readback identity, no hardware/CPU benchmark'}));if(results.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});

/** Extend the original current-closed-session case; keep the45 identities and assertion49. */
async function checkColdSourceReentry(){
 const boundaries=['current','acknowledged','source-pending','written-without-return','other-pid','other-source','unknown-state','lease-present'] as const;
 const evidence:{boundary:typeof boundaries[number],opened:boolean}[]=[];
 for(const boundary of boundaries){
  const f=await fixture(),owner=f.life as any,originalSnapshot=f.host.snapshot();
  const retired={...originalSnapshot,state:'AwaitingControl',resumePhase:'AwaitingControl',powerState:'Unknown',
   powerOperationGeneration:0,resumePhaseGeneration:0,powerOperationAttempt:0,powerOperationStatus:undefined,
   openCalled:false,openEventsCalled:false,hardwareWrites:false,hardwareWritesEnabled:false,hardwareWritesObserved:false,
   lease:null,leaseGeneration:null,sourceFanMode:true,sourceFanOwnerEntered:true,sourceFanGeneration:41,
   sourceFanClosed:true,sourceFanClosePending:false,sourceFanAutomaticReturned:false,
   recoveryClaimEvidence:{sourceFanGeneration:41,oemReleaseActionKind:'not-applicable-no-write-history',oemReleaseActionClass:'not-applicable'}} as FanState;
  owner.opened=true;owner.eventsOpened=false;owner.sourceSessionGeneration=41;
  owner.explicitOpenOrigin={sessionGeneration:owner.sessionGeneration,powerGeneration:owner.powerGeneration,
   processId:owner.process.pid+(boundary==='other-pid'?1:0),acknowledged:boundary==='acknowledged'};
  if(boundary==='source-pending')retired.sourceFanClosePending=true;
  if(boundary==='written-without-return')retired.hardwareWritesObserved=true;
  if(boundary==='other-source'){retired.sourceFanGeneration=42;(retired.recoveryClaimEvidence as Record<string,unknown>).sourceFanGeneration=42;}
  if(boundary==='unknown-state')retired.unknownState=true;
  if(boundary==='lease-present')retired.lease=originalSnapshot.lease;
  const read=f.host.getState.bind(f.host);f.host.getState=async()=>{f.host.calls.push('state');return retired;};
  const generation=owner.powerGeneration,lease=owner.lease,begin=f.host.calls.length;
  try{
   await owner.resyncSessionFromHost(owner.controlIntentRevisionValue,generation,owner.process,owner.heartbeatEpoch);
   assert.equal(owner.opened,boundary!=='current','Only exact unacknowledged failed source Open can withdraw its placeholder');
   assert.equal(owner.powerGeneration,generation,'Zero receipt cannot grant a current power generation');
   assert.equal(owner.lease,lease,'Stale source retirement cannot grant or replace a lease');
   assert.deepEqual(f.host.calls.slice(begin),['state'],'Source retirement readback cannot issue control or cleanup writes');
   evidence.push({boundary,opened:owner.opened});
  }finally{f.host.getState=read;await f.cleanup();}
 }
 const f=await fixture(true),owner=f.life as any,snapshot=f.host.snapshot.bind(f.host),originalOpen=f.host.open.bind(f.host),originalEvents=f.host.openEvents.bind(f.host);
 let opens=0;
 f.host.snapshot=()=>({...snapshot(),sourceFanMode:true,sourceFanOwnerEntered:true,sourceFanGeneration:41,
  sourceFanClosed:!f.host.opened,sourceFanClosePending:false,sourceFanAutomaticReturned:false,
  recoveryClaimEvidence:{sourceFanGeneration:41,oemReleaseActionKind:'not-applicable-no-write-history',oemReleaseActionClass:'not-applicable'}});
 owner.sourceSessionGeneration=41;
 f.host.open=async()=>{opens++;if(opens===1){f.host.calls.push('open');f.host.opened=f.host.events=false;
   throw new FanApiError('injected unsubmitted cold Open failure',409,'LEASE_INVALID');}
  f.host.stateName='Open';return originalOpen();};
 f.host.openEvents=async()=>{assert.equal(f.host.opened,true,'OpenEvents must never replace a missing Open');f.host.stateName='AwaitingControl';return originalEvents();};
 const begin=f.host.calls.length;
 try{
  await f.life.apply(curve(60));
  const order=f.host.calls.slice(begin).filter(call=>['open','events','acquire','enable'].includes(call));
  assert.deepEqual(order,['open','open','events','acquire','enable'],'One public request must reopen after its failed Open before Events/lease/write');
  assert.equal(f.life.state,'ready');assert.equal(f.writes.length,1);assert.deepEqual(f.writes[0],curve(60));
  return{boundaries:evidence,publicReopenOrder:order,writes:1};
 }finally{f.host.snapshot=snapshot;await f.cleanup();}
}

/** Same original case: a fault-retired source owner has no sleep receipt to resume. */
async function checkSettledSourceRecovery(){
 const f=await fixture(),owner=f.life as any,snapshot=f.host.snapshot.bind(f.host);
 f.host.factoryType='HandheldCompanion.Devices.OneXPlayerX2MiniPro';
 f.host.generation=0;f.host.attempt=0;f.host.powerState='Unknown';f.host.stateName='AwaitingControl';
 f.host.opened=f.host.events=false;f.host.lease=null;
 owner.sourceSessionGeneration=41;
 f.host.snapshot=()=>({...snapshot(),sourceFanMode:true,sourceFanOwnerEntered:true,sourceFanGeneration:41,
  sourceFanClosed:!f.host.opened,sourceFanClosePending:false,sourceFanAutomaticReturned:true,hardwareWritesObserved:true,
  recoveryClaimEvidence:{sourceFanGeneration:41,oemReleaseActionKind:'proven-by-source-auto-register-and-terminal-close',
   oemReleaseActionClass:'release-action-proven'}});
 const begin=f.host.calls.length,writes=f.writes.length;
 try{
  owner.noteRecoveryIntentPending('source-fault-retired',owner.powerGeneration);
  if(owner.recoveryRetryTimer){clearTimeout(owner.recoveryRetryTimer);owner.recoveryRetryTimer=null;}
  await owner.runRecoveryTick(owner.recoveryToken);
  const calls=f.host.calls.slice(begin),order=calls.filter(call=>['open','events','acquire','enable','resume'].includes(call));
  assert.deepEqual(order,['open','events','acquire','enable'],'Settled source fault must reopen without inventing a sleep resume receipt');
  assert.equal(f.life.state,'ready');assert.equal(f.writes.length,writes+1);assert.deepEqual(f.writes.at(-1),curve(100));
  // A later explicit click in the same admitted generation must not reuse stale local Open flags.
  f.host.opened=f.host.events=false;f.host.lease=null;f.host.stateName='AwaitingControl';
  const manualBegin=f.host.calls.length,acquire=f.host.acquireControl.bind(f.host);
  f.host.acquireControl=async()=>{assert(f.host.opened&&f.host.events,'Source acquire must follow current Open/OpenEvents');return acquire();};
  await f.life.apply(curve(75));
  const manualOrder=f.host.calls.slice(manualBegin).filter(call=>['open','events','acquire','enable','resume'].includes(call));
  assert.deepEqual(manualOrder,['open','events','acquire','enable'],'Same-generation manual retry must reopen the safely retired source session');
  f.host.opened=f.host.events=false;f.host.lease=null;f.host.stateName='AwaitingControl';
  const failedBegin=f.host.calls.length,open=f.host.open.bind(f.host);
  f.host.open=async()=>{f.host.calls.push('open');throw new FanApiError('PINNED_FAN_OPEN_FAILED',409,'FAULT_LOCKED');};
  const rejected=await settled(f.life.apply(curve(50)));f.host.open=open;
  assert(rejected.error,'A failed fresh source Open must reject the current request');
  assert(owner.opened===false&&owner.eventsOpened===false&&owner.lease===null,
   'Failed fresh source Open must not retain retired local Open/lease mirrors');
  assert.deepEqual(f.host.calls.slice(failedBegin).filter(call=>['open','events','acquire','enable','resume'].includes(call)),['open']);
  return{order,writes:1,sleepResumeRequests:0,manualOrder,manualWrites:1,failedManualOpenRetiredMirrors:true,physicalAcceptance:false,boundaries:await checkSettledSourceBoundaries()};
 }finally{f.host.snapshot=snapshot;await f.cleanup();}
}

/** Refuse false retirement evidence and cancel admission across native awaits. */
async function checkSettledSourceBoundaries(){
 const names=['current','pending','unknown','no-return','wrong-source','missing-source','lease','real-power-operation','wrong-state'] as const;
 const rows:any[]=[];
 for(const name of names){
  const f=await fixture(),owner=f.life as any,snapshot=f.host.snapshot();
  owner.sourceSessionGeneration=name==='missing-source'?undefined:41;
  const retired:any={...snapshot,state:'AwaitingControl',resumePhase:'AwaitingControl',powerState:'Unknown',
   powerOperationGeneration:0,resumePhaseGeneration:0,powerOperationAttempt:0,powerOperationStatus:undefined,
   openCalled:false,openEventsCalled:false,closeCalled:false,hardwareWrites:false,hardwareWritesEnabled:false,
   hardwareWritesObserved:true,lease:null,sourceFanMode:true,sourceFanOwnerEntered:true,sourceFanGeneration:41,
   sourceFanClosed:true,sourceFanClosePending:false,sourceFanAutomaticReturned:true,
   recoveryClaimEvidence:{sourceFanGeneration:41,oemReleaseActionKind:'proven-by-source-auto-register-and-terminal-close',oemReleaseActionClass:'release-action-proven'}};
  if(name==='pending')retired.sourceFanClosePending=true;
  if(name==='unknown')retired.unknownState=true;
  if(name==='no-return')retired.sourceFanAutomaticReturned=false;
  if(name==='wrong-source'){retired.sourceFanGeneration=42;retired.recoveryClaimEvidence.sourceFanGeneration=42;}
  if(name==='lease')retired.lease=snapshot.lease;
  if(name==='real-power-operation'){retired.powerOperationGeneration=6;retired.resumePhaseGeneration=6;retired.powerOperationAttempt=1;retired.powerOperationStatus='completed';retired.powerState='On';}
  if(name==='wrong-state')retired.state=retired.resumePhase='FaultLocked';
  const begin=f.host.calls.length;
  try{const allowed=owner.retiredSourceSessionNeedsOpen(retired,owner.powerGeneration);assert.equal(allowed,name==='current','Only exact safe source retirement admits a fresh Open: '+name);
   assert.deepEqual(f.host.calls.slice(begin),[],'Retirement predicate is read-only');rows.push({name,allowed});
  }finally{await f.cleanup();}
 }
 for(const entry of ['automatic','manual'] as const)for(const edge of ['refused','generation','off','close','sleep','process','session','epoch','source'] as const){
  const f=await fixture(),owner=f.life as any,original=f.host.snapshot.bind(f.host),read=held<any>();
  owner.sourceSessionGeneration=41;f.host.generation=0;f.host.attempt=0;f.host.powerState='Unknown';
  f.host.stateName='AwaitingControl';f.host.opened=f.host.events=false;f.host.lease=null;
  f.host.snapshot=()=>({...original(),sourceFanMode:true,sourceFanOwnerEntered:true,sourceFanGeneration:41,
   sourceFanClosed:true,sourceFanClosePending:false,sourceFanAutomaticReturned:true,hardwareWritesObserved:true,
   recoveryClaimEvidence:{sourceFanGeneration:41,oemReleaseActionKind:'proven-by-source-auto-register-and-terminal-close',oemReleaseActionClass:'release-action-proven'}});
  f.setNative(read.promise);const begin=f.host.calls.length;
  const task=settled(entry==='manual'?f.life.apply(curve(75)):
   owner.runRearmAfterWake(owner.powerGeneration,'source-retirement-boundary',false));await flush();
  let terminal:ReturnType<typeof settled>|null=null;
  if(edge==='off')terminal=settled(f.life.disable());
  if(edge==='close')terminal=settled(f.life.close());
  if(edge==='sleep')f.life.observePowerBoundary('suspending',6);
  if(edge==='process')owner.process={...owner.process};
  if(edge==='session')owner.sessionGeneration++;
  if(edge==='epoch')owner.heartbeatEpoch++;
  if(edge==='source')owner.sourceSessionGeneration=42;
  read.resolve({generation:edge==='generation'?7:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:edge!=='refused'});
  try{await task;if(terminal)await terminal;const calls=f.host.calls.slice(begin);
   assert(!calls.some(x=>['open','events','acquire','enable'].includes(x)),'A retired native-admission await must not start or write: '+edge);
   rows.push({name:(entry==='manual'?'manual-native-':'native-')+edge,ordinaryWrites:0});
  }finally{read.resolve(null);f.setNative({generation:6,phase:'ready',hardwareWritesAllowed:true});f.host.snapshot=original;await f.cleanup();}
 }
 return rows;
}
