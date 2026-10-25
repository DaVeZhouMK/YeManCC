/** FAN983: production manual start and original shutdown/exit owner; no HC/hardware/IPC. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, type FanState, type FanNode } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { PowerLifecycleState } from '../src/bridge/api';
const curve:FanNode[]=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const native=(generation=6,phase:PowerLifecycleState['phase']='ready',allowed=true):PowerLifecycleState=>({generation,phase,hardwareWritesAllowed:allowed&&phase==='ready',resumeReady:phase==='ready',inputReady:true,hibernateAvailable:true});
class Host extends NoIoHost {
 owner=1;closedOwners:number[]=[];enabledOwners:number[]=[];presetOwners:number[]=[];eventsOwners:number[]=[];trace:string[]=[];
 overrideState:Partial<FanState>={};closeComplete=true;stateFailure=false;onClose:(()=>void)|null=null;
 closeHold:Promise<void>|null=null;submitted:{kind:string;nodes:FanNode[]}[]=[];
 constructor(family:string){super();this.factoryType='HandheldCompanion.Devices.'+family;this.stateName='Stopped';}
 override snapshot(){return {...super.snapshot(),...this.overrideState};}
 override async getState(){this.trace.push(this.owner+':state');if(this.stateFailure)throw new FanApiError('session rejected',401,'API_SESSION_REQUIRED');return super.getState();}
 override async open(){this.trace.push(this.owner+':open');this.stateName='Open';return super.open();}
 override async openEvents(){this.trace.push(this.owner+':events');this.eventsOwners.push(this.owner);this.stateName='AwaitingControl';return super.openEvents();}
 override async enable(nodes:readonly FanNode[]=[]){assert(this.opened&&this.events&&this.lease);this.submitted.push({kind:'apply',nodes:nodes.map(n=>({...n}))});this.trace.push(this.owner+':enable');this.enabledOwners.push(this.owner);return super.enable();}
 override async applyPreset(_name?:string,_lease?:string,nodes:readonly FanNode[]=[]){assert(this.opened&&this.events&&this.lease);this.submitted.push({kind:'preset',nodes:nodes.map(n=>({...n}))});this.trace.push(this.owner+':preset');this.presetOwners.push(this.owner);return super.applyPreset();}
 override async close(){this.trace.push(this.owner+':close');this.closedOwners.push(this.owner);if(this.closeHold)await this.closeHold;const state=await super.close();this.onClose?.();return {...state,
  oemRestoreConfirmed:false,oemPhysicalOwnershipConfirmed:false,controlAccepting:false,hcCloseCleanupPending:false,
  hcVirtualCloseReturned:this.closeComplete,hcDeviceManagerStopCompleted:this.closeComplete};}
 reset(){this.owner++;this.opened=this.events=this.closed=false;this.lease=null;this.generation=this.attempt=0;this.stateName='Stopped';this.powerState='Unknown';this.overrideState={};}
 halfOpen(events=false){this.opened=true;this.events=events;this.closed=false;this.stateName=events?'Ready':'Open';this.overrideState={hardwareWritesObserved:true};}
}
function make(host:Host,adopt=false,repeatUnbound=false){let current=native(),starts=0,stops=0,stopFail=false,rescueEnabled=false,wakeCalls=0;let launchHold:Promise<void>|null=null;let launchReached:(()=>void)|null=null;const life=new FanHostLifecycle({enabled:true,adapter:host,
 launcher:{async start(){starts++;assert(starts<=3,'unbounded replacement count');if(starts>1){host.reset();if(repeatUnbound)host.halfOpen();}host.trace.push(host.owner+':launch');if(launchHold){launchReached?.();await launchHold;}return {pid:98300+starts,executable:'fixture-fan.exe',adopted:adopt&&(starts===1||repeatUnbound)};},
 async stop(){stops++;host.trace.push(host.owner+':exit');if(stopFail){stopFail=false;throw new Error('exact-exit-unconfirmed');}}},
 readNativeActivity:async()=>null,readNativePowerState:async()=>({...current}),requestManualWake:async generation=>{wakeCalls++;if(rescueEnabled&&current.phase==='suspended')current=native(current.generation);return {ok:current.phase==='ready'&&current.hardwareWritesAllowed===true,generation,phase:current.phase};},
 heartbeatIntervalMs:0,resumeWaitDeadlineMs:500});life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
 return {life,starts:()=>starts,stops:()=>stops,setNative:(v:PowerLifecycleState)=>{current=v;},failExit:()=>{stopFail=true;},allowStaleWake:()=>{rescueEnabled=true;},wakeCalls:()=>wakeCalls,holdLaunch:()=>{let reached!:()=>void,release!:()=>void;const ready=new Promise<void>(resolve=>{reached=resolve;});launchHold=new Promise<void>(resolve=>{release=resolve;});launchReached=reached;return {ready,release:()=>{launchHold=null;release();}};}};}
let checks=0;const failures:string[]=[];async function check(name:string,work:()=>Promise<void>){checks++;try{await work();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function cleanup(h:Host,life:FanHostLifecycle){h.overrideState={};h.stateFailure=false;h.closeComplete=true;h.onClose=null;await life.close().catch(()=>{});}
async function main(){
 for(const family of ['MSI.ClawCG3EM','MSI.ClawA2VM','ROGAlly','GPD.GPDWin5','AYANEO2021','OneXPlayerX1Mini','Lenovo.LegionGo','Ayn.LokiMax']){
  for(const adopted of [false,true])for(const completeEvents of [false,true])await check(family+' unbound zero-generation '+(completeEvents?'opened':'half-open')+' '+(adopted?'adopted':'resident')+' manual ON recycles then writes to successor',async()=>{
   const h=new Host(family),fx=make(h,adopted);
   try{if(adopted)h.halfOpen(completeEvents);else{await fx.life.start({manualRecovery:true});h.halfOpen(completeEvents);}
    const gate=await fx.life.start({manualRecovery:true});assert(gate.allowed);await fx.life.apply(curve);
    assert.deepEqual(h.closedOwners,[1]);assert.deepEqual(h.enabledOwners,[2]);assert.deepEqual(h.eventsOwners,[2]);assert.equal(fx.starts(),2);assert.equal(fx.stops(),1);
    assert(h.trace.indexOf('1:exit')<h.trace.indexOf('2:launch'));assert(!h.calls.includes('resume'),'no fictitious resume receipt');
    await fx.life.start({manualRecovery:true});await fx.life.apply(curve);assert.equal(fx.starts(),2,'healthy successor remains resident');
   }finally{await cleanup(h,fx.life);}
  });
 }
 for(const blocked of ['native-suspending','native-suspended','native-no-write','new-generation','auth','cleanup','active-lease','unsupported','bad-generation','automatic'] as const)await check(blocked+' cannot retire a session or write from an invalid manual boundary',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.halfOpen();
   if(blocked==='native-suspending')fx.setNative(native(6,'suspending'));
   if(blocked==='native-suspended')fx.setNative(native(6,'suspended'));
   if(blocked==='native-no-write')fx.setNative(native(6,'ready',false));
   if(blocked==='new-generation')fx.setNative(native(7,'suspending'));
   if(blocked==='auth')h.stateFailure=true;
   if(blocked==='cleanup')h.overrideState={hcCloseCleanupPending:true};
   if(blocked==='active-lease')h.lease={leaseId:'other-owner',generation:7};
   if(blocked==='unsupported')h.overrideState={fanCapabilitySupported:false};
   if(blocked==='bad-generation')h.overrideState={resumePhaseGeneration:6,powerOperationGeneration:0};
   await fx.life.start({manualRecovery:blocked!=='automatic'}).catch(()=>{});
   assert.equal(h.closedOwners.length,0);assert.equal(fx.starts(),1);assert.equal(h.enabledOwners.length,0);
  }finally{await cleanup(h,fx.life);}
 });
 await check('unproven Close cannot launch a successor despite handshake success',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.halfOpen();h.closeComplete=false;
   const error=await fx.life.start({manualRecovery:true}).then(()=>'',e=>String(e));assert(error);assert.equal(h.closedOwners.length,1);assert.equal(fx.starts(),1);assert.equal(h.enabledOwners.length,0);
  }finally{await cleanup(h,fx.life);}
 });
 await check('failed exact exit keeps owner; the next click retries exit without another Close',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.halfOpen();fx.failExit();
   const first=await fx.life.start({manualRecovery:true}).then(()=>'',e=>String(e));assert.match(first,/exact-exit-unconfirmed/);assert.equal(fx.starts(),1);
   await fx.life.start({manualRecovery:true});await fx.life.apply(curve);assert.deepEqual(h.closedOwners,[1]);assert.deepEqual(h.enabledOwners,[2]);assert.equal(fx.stops(),2);
  }finally{await cleanup(h,fx.life);}
 });
 await check('a real new sleep while Close returns prevents successor launch',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.halfOpen();h.onClose=()=>{fx.setNative(native(7,'suspending'));fx.life.setPowerGeneration(7);fx.life.observePowerBoundary('suspending',7);};
   const error=await fx.life.start({manualRecovery:true}).then(()=>'',e=>String(e));assert.match(error,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.equal(fx.starts(),1);assert.equal(h.enabledOwners.length,0);
  }finally{await cleanup(h,fx.life);}
 });
 await check('one manual operation performs at most one session replacement',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true,true);h.halfOpen();
  try{const error=await fx.life.start({manualRecovery:true}).then(()=>'',e=>String(e));
   assert.match(error,/FAN_MANUAL_HC_SESSION_RESTART_UNCONFIRMED/);assert.equal(fx.starts(),2);assert.deepEqual(h.closedOwners,[1]);assert.equal(h.enabledOwners.length,0);
  }finally{await cleanup(h,fx.life);}
 });
 for(const events of [false,true])await check('native accepts stale Suspended rescue before unbound session replacement '+events,async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.halfOpen(events);fx.setNative(native(6,'suspended'));fx.allowStaleWake();
   await fx.life.start({manualRecovery:true});await fx.life.apply(curve);
   assert.equal(fx.wakeCalls(),1);assert.deepEqual(h.closedOwners,[1]);assert.deepEqual(h.enabledOwners,[2]);
  }finally{await cleanup(h,fx.life);}
 });
 for(const action of ['apply','preset'] as const)for(const events of [false,true])await check('direct '+action+' retires adopted unbound session before admission '+events,async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true);h.halfOpen(events);
  try{await fx.life.start().catch(()=>{});
   if(action==='apply')await fx.life.apply(curve);else await fx.life.applyPreset('aggressive',curve);
   // FAN-996: this is the 2026-10-15 field shape after an expired resume
   // receipt: an already-open, generation-zero HC shell must be retired and
   // reopened directly. It must never manufacture a fictional /api/resume.
   assert.equal(h.calls.includes('resume'),false,'expired/unbound shell must not call resume');
   assert.deepEqual(h.closedOwners,[1]);assert.equal(fx.starts(),2);assert.equal(fx.stops(),1);
   assert.deepEqual(h.eventsOwners,[2]);assert(h.trace.indexOf('1:exit')<h.trace.indexOf('2:launch'));
   assert.equal(h.owner,2);assert.deepEqual(action==='apply'?h.enabledOwners:h.presetOwners,[2]);
  }finally{await cleanup(h,fx.life);}
 });
 for(const blocked of ['sleep','auth','cleanup','active-lease','bad-generation'] as const)await check('direct control refuses invalid retirement boundary '+blocked,async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true);h.halfOpen();
  try{await fx.life.start().catch(()=>{});
   if(blocked==='sleep')fx.setNative(native(6,'suspending'));
   if(blocked==='auth')h.stateFailure=true;
   if(blocked==='cleanup')h.overrideState={hcCloseCleanupPending:true};
   if(blocked==='active-lease')h.lease={leaseId:'other-owner',generation:7};
   if(blocked==='bad-generation')h.overrideState={resumePhaseGeneration:6,powerOperationGeneration:0};
   await fx.life.apply(curve).catch(()=>{});assert.equal(h.closedOwners.length,0);assert.equal(fx.starts(),1);assert.equal(h.enabledOwners.length,0);
  }finally{await cleanup(h,fx.life);}
 });
 await check('OFF while direct control retires the old owner cancels successor and curve',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true);h.halfOpen();let off:Promise<unknown>|null=null;
  try{await fx.life.start().catch(()=>{});h.onClose=()=>{h.onClose=null;off=fx.life.disable();};
   const error=await fx.life.apply(curve).then(()=>'',e=>String(e));assert.match(error,/FAN_CONTROL_INTENT_SUPERSEDED/);if(off)await off;
   assert.equal(fx.starts(),1);assert.deepEqual(h.closedOwners,[1]);assert.equal(h.enabledOwners.length,0);
  }finally{await cleanup(h,fx.life);}
 });
 // FAN1042: a newer explicit request must join the same direct unbound-retirement owner.
 const latestCurve=curve.map(node=>({...node,dutyPercent:30}));
 for(const mode of ['known-wake','missed-wake'])for(const action of ['apply','preset'] as const)for(const events of [false,true])await check(mode+' latest '+action+' joins direct unbound Close '+events+' and writes once without another click',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true);h.halfOpen(events);let release!:()=>void;
  h.closeHold=new Promise<void>(resolve=>{release=resolve;});
  try{await fx.life.start().catch(()=>{});if(mode==='missed-wake')fx.life.observePowerBoundary('suspending',6);
   const old=fx.life.apply(curve).then(()=> 'accepted',error=>String(error));
   const deadline=Date.now()+1500;while(!h.closedOwners.length&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,5));
   assert.equal(h.closedOwners.length,1,'first explicit request must own the one Close');
   const latest=(action==='apply'?fx.life.apply(latestCurve):fx.life.applyPreset('soft',latestCurve)).then(()=> 'accepted',error=>String(error));
   // Let a new request enter its real asynchronous admission before the old Close returns.
   await new Promise(resolve=>setTimeout(resolve,20));release();
   const observed={old:await old,latest:await latest,starts:fx.starts(),stops:fx.stops(),trace:h.trace,submitted:h.submitted,stateReads:h.calls.filter(call=>call==='state').length};
   console.log('FAN1042_TRACE '+JSON.stringify(observed));
   assert.match(observed.old,/FAN_CONTROL_INTENT_SUPERSEDED/,'old curve must be cancelled');
   assert.equal(observed.latest,'accepted','latest request must continue after exact predecessor exit');
   assert.equal(fx.starts(),2);assert.equal(fx.stops(),1);assert.deepEqual(h.closedOwners,[1]);
   assert.deepEqual(h.submitted,[{kind:action,nodes:latestCurve}],'only the latest requested curve may write');
   assert(!h.calls.includes('resume'),'unbound original session has no resume receipt');
  }finally{release();h.closeHold=null;await cleanup(h,fx.life);}
 });
 for(const cancel of ['off','sleep'])await check('latest direct unbound request still cancels on '+cancel+' before old Close returns',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true);h.halfOpen();let release!:()=>void;
  h.closeHold=new Promise<void>(resolve=>{release=resolve;});let cancelling:Promise<unknown>|null=null;
  try{await fx.life.start().catch(()=>{});const old=fx.life.apply(curve).then(()=> 'accepted',error=>String(error));
   const deadline=Date.now()+1500;while(!h.closedOwners.length&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,5));assert.equal(h.closedOwners.length,1);
   const latest=fx.life.apply(latestCurve).then(()=> 'accepted',error=>String(error));await new Promise(resolve=>setTimeout(resolve,20));
   if(cancel==='off')cancelling=fx.life.disable();else{fx.setNative(native(7,'suspending'));fx.life.setPowerGeneration(7);fx.life.observePowerBoundary('suspending',7);}
   release();assert.match(await old,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.match(await latest,/FAN_CONTROL_INTENT_SUPERSEDED/);if(cancelling)await cancelling;
   assert.equal(fx.starts(),1,'cancelled continuation must not launch');assert.equal(h.submitted.length,0,'cancelled request must not write');assert.deepEqual(h.closedOwners,[1]);
  }finally{release();h.closeHold=null;await cleanup(h,fx.life);}
 });
 for(const action of ['apply','preset'] as const)await check('latest '+action+' during unbound successor launch still writes only the latest curve',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h,true);h.halfOpen();let hold:ReturnType<typeof fx.holdLaunch>|null=null;
  try{await fx.life.start().catch(()=>{});hold=fx.holdLaunch();const old=fx.life.apply(curve).then(()=> 'accepted',error=>String(error));
   let timer:ReturnType<typeof setTimeout>|undefined;try{await Promise.race([hold.ready,new Promise<never>((_resolve,reject)=>{timer=setTimeout(()=>reject(new Error('successor launch not reached')),1500);})]);}finally{clearTimeout(timer);}
   const latest=(action==='apply'?fx.life.apply(latestCurve):fx.life.applyPreset('soft',latestCurve)).then(()=> 'accepted',error=>String(error));
   await new Promise(resolve=>setTimeout(resolve,20));hold.release();
   assert.match(await old,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.equal(await latest,'accepted','latest launch-stage request must not require another click');
   assert.equal(fx.starts(),2);assert.equal(fx.stops(),1);assert.deepEqual(h.closedOwners,[1]);assert.deepEqual(h.submitted,[{kind:action,nodes:latestCurve}]);
  }finally{hold?.release();await cleanup(h,fx.life);}
 });
 console.log(JSON.stringify({ok:!failures.length,checks,failures,hardwareCalls:0,scope:'production-public-manual-start/apply/preset; exact-session-exit-owner-fixtures'}));if(failures.length)process.exitCode=1;
}
void main();
