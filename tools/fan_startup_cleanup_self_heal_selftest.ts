/** Retained explicit ON -> real startup/recovery/queue; inert HC/native/process boundaries. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanState,type FanNode} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve=(duty:number)=>[0,40,70,100].map(tempC=>({tempC,dutyPercent:duty}));
class Clock {now=0;seq=0;timers=new Map<number,{due:number,period:number,fn:()=>void}>();add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return{id};}clear(h:any){this.timers.delete(h?.id);}async flush(){for(let i=0;i<160;i++)await Promise.resolve();}async advance(ms:number){const target=this.now+ms;for(let i=0;i<1000;i++){const next=[...this.timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=Math.max(this.now,next[1].due);if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=Math.max(this.now,target);await this.flush();}}
const held=<T>()=>{let resolve!:(v:T)=>void,entered!:()=>void;const promise=new Promise<T>(r=>resolve=r),ready=new Promise<void>(r=>entered=r);let enteredFlag=false;return{resolve,entered(){enteredFlag=true;entered();},promise,ready,get wasEntered(){return enteredFlag;}};};
class CleanupHost extends NoIoHost {
 pending=false;retired=false;unknown=false;unknownNextEnable:'commit'|'refuse'|null=null;route='GenericDuty';nodes:FanNode[]|null=null;holdState:ReturnType<typeof held<FanState>>|null=null;denyAuth=false;holdHandshake:ReturnType<typeof held<void>>|null=null;
 snapshot():FanState{return {...super.snapshot(),hcCloseCleanupPending:this.pending,hcVirtualCloseReturned:this.retired,hcDeviceManagerStopCompleted:this.retired,
  unknownState:this.unknown,hardwareWrites:false,hardwareWritesEnabled:this.stateName==='Ready'&&this.lease!==null,
  controlAccepting:!this.pending&&!this.retired&&['Ready','AwaitingControl'].includes(this.stateName),oemRestoreConfirmed:false,oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'hc-callback-only-physical-unknown',
  activeCurve:this.nodes,lastControlWriteEvidence:this.nodes?'compat-route-callback-returned-no-scope':'not-written'};}
 async handshake(){if(this.denyAuth)throw new FanApiError('unauthorized',401,'AUTH_REJECTED');if(this.holdHandshake){const g=this.holdHandshake;this.holdHandshake=null;g.entered();await g.promise;}return {...await super.handshake(),fanRoute:this.route};}
 async getState(){if(this.holdState){this.calls.push('state');const g=this.holdState;this.holdState=null;g.entered();return g.promise;}return super.getState();}
 async open(){if(this.pending)throw new FanApiError('HC_CLOSE_PENDING',409,'HC_CLOSE_PENDING');if(this.retired)throw new FanApiError('HOST_CLOSING',409,'HOST_CLOSING');return super.open();}
 async openEvents(){if(this.pending)throw new FanApiError('HC_CLOSE_PENDING',409,'HC_CLOSE_PENDING');return super.openEvents();}
 async acquireControl(){if(this.stateName==='FaultLocked'){this.calls.push('acquire');throw new FanApiError('fault latch blocks control',409,'FAULT_LOCKED');}if(this.pending)throw new FanApiError('HC_CLOSE_PENDING',409,'HC_CLOSE_PENDING');return super.acquireControl();}
 async enable(nodes:readonly FanNode[]=[]){if(this.pending)throw new FanApiError('HC_CLOSE_PENDING',409,'HC_CLOSE_PENDING');const unknown=this.unknownNextEnable;this.unknownNextEnable=null;if(unknown==='refuse'){this.calls.push('enable');throw new FanApiError('reply lost',0,'FAN_HTTP_RESULT_UNKNOWN');}this.nodes=nodes.map(n=>({...n}));const result=await super.enable();if(unknown)throw new FanApiError('reply lost',0,'FAN_HTTP_RESULT_UNKNOWN');return result;}
 enterPending(){this.pending=true;this.stateName='AwaitingCloseCleanup';this.lease=null;this.opened=this.events=false;this.closed=true;this.generation=6;this.attempt=1;this.powerState='On';this.nodes=null;}
 settle(mode:'open'|'stopped'|'fault'|'cold'){this.pending=false;this.unknown=mode==='fault'||mode==='stopped';this.retired=mode==='fault'||mode==='stopped';this.stateName=mode==='fault'?'FaultLocked':mode==='stopped'?'Stopped':'AwaitingControl';this.closed=this.retired;this.opened=this.events=mode==='open';if(mode==='stopped'||mode==='fault'){this.generation=0;this.attempt=0;this.powerState='Unknown';}}
 reset(){this.pending=this.retired=this.unknown=false;this.closed=this.opened=this.events=false;this.lease=null;this.nodes=null;this.stateName='AwaitingControl';this.generation=0;this.attempt=0;this.powerState='Unknown';}
}
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
async function fixture(family:typeof families[number]=families[1],withoutIntent=false){
 const clock=new Clock(),saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(fn:()=>void,ms:number)=>clock.add(fn,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(fn:()=>void,ms:number)=>clock.add(fn,ms,ms),clearInterval:(h:any)=>clock.clear(h)});Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>clock.now}});
 const host=new CleanupHost();host.factoryType=family[1];host.route=family[2];let native:any={generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true};let starts=0,stops=0,maxLive=0,denyExit=false;const live=new Set<number>(),allHolds:ReturnType<typeof held<void>>[]=[];let stopHold:ReturnType<typeof held<void>>|null=null,startHold:ReturnType<typeof held<void>>|null=null;const trace:string[]=[];
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:5000,readNativeActivity:async()=>null,readNativePowerState:async()=>native,
  launcher:{async start(){assert.equal(live.size,0,'second Host before exact old exit');if(startHold){const g=startHold;startHold=null;g.entered();await g.promise;}const pid=116300+(++starts);host.reset();live.add(pid);maxLive=Math.max(maxLive,live.size);trace.push('launch:'+pid);return{pid,executable:'inert-fan.exe'};},async stop(process){if(stopHold){const g=stopHold;stopHold=null;g.entered();await g.promise;}if(denyExit)throw Error('exact predecessor exit unconfirmed');assert(live.has(process.pid));live.delete(process.pid);stops++;trace.push('exact-exit:'+process.pid);}}});
 if(process.env.YMCC_FAN_CLEANUP_DEBUG){const owner=life as any,original=owner.startInternal.bind(owner);owner.startInternal=async(...args:any[])=>{trace.push('startInternal:'+JSON.stringify({state:owner.state,session:owner.sessionGeneration,phase:owner.coordinatorSnapshot.phase,host:owner.processId,selfHeal:!!args[4],startup:owner.recoveryIntent?.selfHeal?.startupRequired}));try{return await original(...args);}catch(e){trace.push('startError:'+String(e));throw e;}};}
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start();if(!withoutIntent)await life.apply(curve(100));host.enterPending();
 return{life,host,clock,trace,counts:()=>({starts,stops,maxLive}),native:(v:any)=>native=v,denyExit:(v:boolean)=>denyExit=v,
  holdStop(){const g=held<void>();stopHold=g;allHolds.push(g);return g;},holdStart(){const g=held<void>();startHold=g;allHolds.push(g);return g;},holdHandshake(){const g=held<void>();host.holdHandshake=g;allHolds.push(g);return g;},
  async on(nodes=curve(65)){life.stagePendingControlCurve(nodes);await assert.rejects(life.start({manualRecovery:true,reason:'one-user-on'}),e=>e instanceof FanApiError&&e.errorCode==='HC_CLOSE_PENDING');},
  async cleanup(){for(const g of allHolds)g.resolve();await clock.flush();denyExit=false;host.pending=false;await life.close().catch(()=>{});Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
const results:any[]=[];
async function check(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]=families[1],withoutIntent=false){if(process.env.YMCC_FAN_CLEANUP_CASE&&!name.includes(process.env.YMCC_FAN_CLEANUP_CASE))return;const f=await fixture(family,withoutIntent);try{results.push({name,family:family[0],ok:true,evidence:await work(f),calls:f.host.calls.slice(),trace:f.trace.slice()});}catch(e){results.push({name,family:family[0],ok:false,error:String(e),calls:f.host.calls.slice(),trace:f.trace.slice(),state:f.life.state,coordinator:f.life.coordinatorSnapshot,recovery:(f.life as any).recoveryState});}finally{await f.cleanup();}}
async function main(){
 for(const family of families)for(const code of ['HC_SESSION_ROUTE_LOST','OEM_RESTORE_FAILED'])for(const exit of ['confirmed','unconfirmed']as const)await check(`FAN1291 retired resource error ${code} exact exit ${exit}`,async f=>{
  f.host.settle('open');const enable=f.host.enable.bind(f.host);let failed=false;
  f.host.enable=async(nodes:readonly FanNode[]=[])=>{
   if(failed)return enable(nodes);failed=true;f.host.calls.push('enable');f.host.lease=null;f.host.nodes=null;f.host.settle('fault');
   throw new FanApiError('inert failed resource with exact HC retirement facts',409,code);
  };
  f.denyExit(exit==='unconfirmed');const before=f.host.calls.filter(c=>c==='enable').length;
  await assert.rejects(f.life.apply(curve(65)),e=>e instanceof FanApiError&&e.errorCode===code);
  assert(f.life.recoveryActive);assert.equal(f.host.calls.filter(c=>c==='enable').length,before+1,'retired resource caused immediate replay');
  await f.clock.advance(exit==='confirmed'?5000:35000);assert(!f.life.recoveryActive);assert.equal(f.counts().maxLive,1);
  assert.equal(f.host.calls.filter(c=>c==='close').length,0,'already retired HC was closed twice');
  if(exit==='confirmed'){
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));assert.equal(f.counts().starts,2);assert.equal(f.counts().stops,1);
   assert.equal(f.host.calls.filter(c=>c==='enable').length,before+2,'one automatic successor must restore the requested curve once');
   assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false,'OEM physical proof was invented');
  }else{
   assert(f.life.recoveryNeedsAttention);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(c=>c==='enable').length,before+1);
   f.denyExit(false);await f.life.apply(curve(75));assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(75));assert.equal(f.counts().maxLive,1);
  }
  return {counts:f.counts(),oemPhysicalConfirmed:false};
 },family);
 for(const family of families)for(const mode of ['open','stopped','fault','cold'] as const)await check('one ON resumes after pending cleanup -> '+mode,async f=>{await f.on();assert(f.life.recoveryActive,'startup refusal did not retain a recovery owner');const before=f.host.calls.length;await f.clock.advance(250);assert.equal(f.host.calls.slice(before).filter(x=>x==='enable'||x==='close').length,0,'pending cleanup was bypassed');f.host.settle(mode);await f.clock.advance(2500);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));assert.equal(f.host.calls.filter(x=>x==='enable').length,2,'original user request needed a second click or duplicate write');assert.equal(f.counts().maxLive,1);assert.equal(f.counts().starts,mode==='stopped'||mode==='fault'?2:1);assert.equal(f.host.calls.filter(x=>x==='close').length,0,'completed or unnecessary Close was replayed');return {counts:f.counts(),calls:f.host.calls.slice()};},family);
 await check('persistent cleanup reaches finite attention and a fresh first ON remains usable',async f=>{await f.on();assert(f.life.recoveryActive);const begin=f.host.calls.length;await f.clock.advance(15000);assert(f.life.recoveryActive,'slow cleanup lost its bounded owner too early');await f.clock.advance(20000);assert(f.life.recoveryNeedsAttention);assert.equal(f.host.calls.slice(begin).filter(x=>x==='close'||x==='enable').length,0);const end=f.host.calls.length;await f.clock.advance(10000);assert.equal(f.host.calls.length,end);f.host.settle('stopped');assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return f.counts();});
 for(const action of ['off','close'])await check(action+' cancels pending startup recovery and late probes cannot enable',async f=>{await f.on();assert(f.life.recoveryActive);const g=held<FanState>();f.host.holdState=g;await f.clock.advance(250);const task=action==='off'?f.life.disable():f.life.close();f.host.pending=false;g.resolve(f.host.snapshot());await task.catch(()=>{});const enables=f.host.calls.filter(x=>x==='enable').length;await f.clock.advance(5000);assert(!f.life.hasControlIntent);assert.equal(f.host.calls.filter(x=>x==='enable').length,enables);return f.counts();});
 for(const boundary of ['stop','launch','handshake'] as const)await check('latest staged curve during '+boundary+' is restored without another ON',async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');const g=boundary==='stop'?f.holdStop():boundary==='launch'?f.holdStart():f.holdHandshake();await f.clock.advance(250);assert(f.life.stageRecoveryCurve(curve(85)));g.resolve();await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(85));assert.equal(f.counts().starts,2);assert.equal(f.counts().maxLive,1);return f.counts();});
 await check('unconfirmed exact exit never creates a second Host',async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');f.denyExit(true);await f.clock.advance(35000);assert.equal(f.counts().starts,1);assert.equal(f.counts().maxLive,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert(!f.life.recoveryActive);return f.counts();});
 await check('no control intent does not create autonomous curve work',async f=>{await assert.rejects(f.life.start({manualRecovery:true}));assert(!f.life.recoveryActive);await f.clock.advance(5000);assert.equal(f.host.calls.filter(x=>x==='enable').length,0);return f.counts();},families[1],true);
 await check('authentication failure on a replacement does not report success or repeat starts',async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');f.host.denyAuth=true;await f.clock.advance(10000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);const starts=f.counts().starts;await f.clock.advance(10000);assert.equal(f.counts().starts,starts);f.host.denyAuth=false;return f.counts();});
 for(const entry of ['apply','preset','pending'])await check('latest '+entry+' while cleanup waits is restored by the same retained ON',async f=>{await f.on();assert(f.life.recoveryActive);if(entry==='apply')await f.life.apply(curve(85)).catch(()=>{});else if(entry==='preset')await f.life.applyPreset('aggressive',curve(85)).catch(()=>{});else f.life.stagePendingControlCurve(curve(85));assert(f.life.recoveryActive);f.host.settle('stopped');await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(85));return f.counts();});
 for(const mode of ['commit','refuse'] as const)await check('replacement cold Open then lost enable reply: '+mode,async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');f.host.unknownNextEnable=mode;await f.clock.advance(mode==='refuse'?35000:15000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,2,'uncertain write was blindly repeated');if(mode==='commit'){assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));assert(!f.life.recoveryNeedsAttention);}else assert(f.life.recoveryNeedsAttention);return f.counts();});
 await check('native ready with writes denied never starts or writes a replacement',async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');f.native({generation:6,phase:'ready',hardwareWritesAllowed:false,resumeReady:true});await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);return f.counts();});
 await check('real native sleep boundary keeps pending recovery write-closed',async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');f.native({generation:7,phase:'suspending',hardwareWritesAllowed:false,resumeReady:false});await f.clock.advance(3000);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable'||x==='close').length,1,'sleep started a curve or lifecycle write');await f.life.disable();assert(!f.life.hasControlIntent);return f.counts();});
 await check('startup waiting on exact exit cannot launch after its recovery deadline',async f=>{await f.on();assert(f.life.recoveryActive);f.host.settle('stopped');const g=f.holdStop();await f.clock.advance(250);f.clock.now+=31000;g.resolve();await f.clock.advance(1000);assert.equal(f.counts().starts,1,'expired restart launched a new Host');assert(!f.life.recoveryActive);assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return f.counts();});
 await check('ordinary staging without a recovery owner constructs no recovery context',async f=>{const owner=f.life as any,original=owner.admissionContext.bind(owner);let calls=0;owner.admissionContext=(...args:any[])=>{calls++;return original(...args);};const before=f.host.calls.length;f.life.stagePendingControlCurve(curve(85));assert.equal(calls,0);assert.equal(f.host.calls.length,before);assert(!f.life.recoveryActive);return {admissionContexts:calls};});
 await check('cleanup completing after sixteen seconds still continues the first ON',async f=>{await f.on();assert(f.life.recoveryActive);await f.clock.advance(15000);assert(f.life.recoveryActive,'cleanup probes were exhausted before HC could return');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);await f.clock.advance(1000);f.host.settle('stopped');await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(65));assert.equal(f.counts().starts,2);return f.counts();});
 await check('ordinary read failures share ten finite probes and the original deadline',async f=>{
  f.host.settle('open');
  f.host.enable=async()=>{f.host.calls.push('enable');throw new FanApiError('lease refused',409,'LEASE_INVALID');};
  await assert.rejects(f.life.apply(curve(65)),e=>e instanceof FanApiError&&e.errorCode==='LEASE_INVALID');
  assert(f.life.recoveryActive);let probes=0;const deadline=(f.life as any).recoveryIntent.selfHeal.deadlineAt;
  f.host.getState=async()=>{probes++;throw new FanApiError('state unavailable',0,'FAN_HTTP_RESULT_UNKNOWN');};
  await f.clock.advance(15000);assert(f.life.recoveryActive,'slow observation stage was lost');assert(probes<=10);
  assert.equal((f.life as any).recoveryIntent.selfHeal.deadlineAt,deadline,'read failures extended the original deadline');
  await f.clock.advance(20000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert.equal(probes,10);
  await f.clock.advance(35000);assert.equal(probes,10,'exhausted observation kept polling');
  return {stateReads:probes,starts:f.counts().starts};
 });
 await check('repeated cleanup errors upgrade probe allowance once without extending deadline',async f=>{
  f.host.settle('open');
  f.host.enable=async()=>{f.host.calls.push('enable');throw new FanApiError('lease refused',409,'LEASE_INVALID');};
  await assert.rejects(f.life.apply(curve(65)),e=>e instanceof FanApiError&&e.errorCode==='LEASE_INVALID');
  assert(f.life.recoveryActive);let probes=0;
  const deadline=(f.life as any).recoveryIntent.selfHeal.deadlineAt;
  f.host.getState=async()=>{probes++;throw new FanApiError('cleanup pending',409,'HC_CLOSE_PENDING');};
  await f.clock.advance(15000);assert(f.life.recoveryActive,'cleanup upgrade ended too early');
  assert.equal((f.life as any).recoveryIntent.selfHeal.deadlineAt,deadline);
  assert.equal(probes+(f.life as any).recoveryIntent.selfHeal.probesLeft,10,'repeated refusal replenished the probe allowance');
  await f.clock.advance(20000);assert(!f.life.recoveryActive);assert.equal(probes,10);
  assert.equal(f.counts().starts,1,'read refusal started a replacement');
  return {stateReads:probes,starts:f.counts().starts,unchangedDeadline:deadline};
 });
 await check('exhausted known-refusal budget does not read or restart and the next manual ON works',async f=>{
  await f.on();let attempts=0;const original=f.host.enable.bind(f.host);
  f.host.enable=async(nodes:readonly FanNode[]=[])=>{attempts++;f.host.calls.push('enable-refused');f.host.enterPending();throw new FanApiError('cleanup pending',409,'HC_CLOSE_PENDING');};
  f.host.settle('stopped');await f.clock.advance(250);assert.equal(attempts,1);
  f.host.settle('stopped');await f.clock.advance(250);assert.equal(attempts,2);
  const begin=f.host.calls.length,starts=f.counts().starts;
  f.host.settle('stopped');await f.clock.advance(35000);
  const after=f.host.calls.slice(begin);assert.equal(f.counts().starts,starts,'write-exhausted recovery still launched a replacement');
  assert.equal(after.length,0,'write-exhausted recovery still submitted read/cleanup work');
  assert(f.life.recoveryNeedsAttention);assert(!f.life.recoveryActive);
  f.host.enable=original;assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));
  return{attempts,startsAtExhaustion:starts,extraCallsAfterExhaustion:after,manualAfterExhaustion:true};
 });
 for(const result of ['commit','refuse'] as const)await check('last write acknowledgement lost still reconciles without any write allowance: '+result,async f=>{
  await f.on();let attempts=0;const original=f.host.enable.bind(f.host);
  f.host.enable=async(nodes:readonly FanNode[]=[])=>{
   attempts++;
   if(attempts===1){f.host.calls.push('enable-refused');f.host.enterPending();throw new FanApiError('cleanup pending',409,'HC_CLOSE_PENDING');}
   f.host.unknownNextEnable=result;return original(nodes);
  };
  f.host.settle('stopped');await f.clock.advance(250);assert.equal(attempts,1);
  f.host.settle('stopped');await f.clock.advance(250);assert.equal(attempts,2);const begin=f.host.calls.length;
  await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(attempts,2,'uncertain last write was replayed');
  assert(f.host.calls.slice(begin).filter(x=>x==='state').length>0,'zero writes incorrectly disabled read-only reconciliation');
  if(result==='commit'){assert.equal(f.life.state,'ready');assert(!f.life.recoveryNeedsAttention);assert.deepEqual(f.host.nodes,curve(65));}else assert(f.life.recoveryNeedsAttention);
  return{attempts,readbackCalls:f.host.calls.slice(begin),confirmed:result==='commit'};
 });
 // A later exact retired-process boundary is different from absent feedback.
 async function loseThenRetire(f:Awaited<ReturnType<typeof fixture>>, mode:'stopped'|'fault'='fault', code='HOST_ERROR', status=500) {
   f.host.settle('open');
   const original=f.host.enable.bind(f.host);let fail=true;
   f.host.enable=async(nodes:readonly FanNode[]=[])=>{
     if(fail){fail=false;f.host.calls.push('enable');f.host.lease=null;f.host.nodes=null;
       f.host.settle(mode);f.host.closed=false;throw new FanApiError('OEM callback result unavailable',status,code);}
     return original(nodes);
   };
   await assert.rejects(f.life.apply(curve(65)),e=>e instanceof FanApiError&&e.errorCode===code);
   assert(f.life.recoveryActive,'uncertain write lost its bounded observer');
   assert.equal(f.host.snapshot().closeCalled,false,'hardware-failure Close must not be confused with explicit API Close');
   return original;
 }
 for(const family of families)for(const mode of ['stopped','fault'] as const)await check('OEM retired owner heals without a second click: '+mode,async f=>{
   await loseThenRetire(f,mode);await f.clock.advance(5000);
   assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));
   assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});
   assert.equal(f.host.calls.filter(x=>x==='enable').length,3);
   assert.equal(f.host.calls.filter(x=>x==='close').length,0,'retired Close was repeated');
   assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);
   return{counts:f.counts(),trace:f.trace.slice(),calls:f.host.calls.slice(),physicalConfirmed:false};
 },family);
 // FAN1205: FaultLocked starts observation, never grants another write.
 for(const family of families)await check('FAULT_LOCKED retired owner heals with absent OEM physical signals',async f=>{
   const snapshot=f.host.snapshot.bind(f.host);f.host.snapshot=()=>{const value=snapshot();delete value.oemPhysicalOwnershipConfirmed;delete value.oemOwnershipStatus;return value;};
   await loseThenRetire(f,'fault','FAULT_LOCKED',409);await f.clock.advance(5000);
   assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));
   assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});assert.equal(f.host.calls.filter(x=>x==='close').length,0);
   assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,undefined);return f.counts();
 },family);
 for(const weak of ['virtual','pending','open','lease','transition'] as const)await check('FAULT_LOCKED bounded observer rejects incomplete '+weak,async f=>{
   await loseThenRetire(f,'fault','FAULT_LOCKED',409);const snapshot=f.host.snapshot.bind(f.host);
   const patch:Partial<FanState>=weak==='virtual'?{hcVirtualCloseReturned:false}:weak==='pending'?{hcCloseCleanupPending:true}:weak==='open'?{openCalled:true}:
     weak==='lease'?{lease:{leaseId:'lease-still-owned',generation:1,expiresAtMonoMs:15000}}:{powerOperationStatus:'executing'};
   f.host.snapshot=()=>({...snapshot(),...patch});await f.clock.advance(35000);
   assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert(f.life.hasControlIntent);assert.equal(f.counts().starts,1);
   assert.equal(f.host.calls.filter(x=>x==='enable').length,2,'error code authorized a blind curve replay');
   const end=f.host.calls.length;await f.clock.advance(10000);assert.equal(f.host.calls.length,end,'exhausted recovery kept polling');
   f.host.snapshot=snapshot;assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return f.counts();
 });
 for(const status of [401,403])await check('FAULT_LOCKED label never bypasses authentication '+status,async f=>{
   f.host.settle('open');const enable=f.host.enable.bind(f.host);f.host.enable=async()=>{f.host.calls.push('enable');throw new FanApiError('authentication denied',status,'FAULT_LOCKED');};
   await assert.rejects(f.life.apply(curve(65)));assert(!f.life.recoveryActive);const end=f.host.calls.length;await f.clock.advance(10000);assert.equal(f.host.calls.length,end);assert.equal(f.counts().starts,1);f.host.enable=enable;return f.counts();
 });
 for(const action of ['off','sleep'] as const)await check('FAULT_LOCKED observation cancelled before replacement by '+action,async f=>{
   await loseThenRetire(f,'fault','FAULT_LOCKED',409);if(action==='off'){await f.life.disable().catch(()=>{});assert(!f.life.hasControlIntent);}else{f.native({generation:7,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   const writes=f.host.calls.filter(x=>x==='enable').length;await f.clock.advance(5000);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,writes);return f.counts();
 });
 for(const weak of ['virtual','manager','pending','open','events','write-enabled','writes','accepting','lease','transition','nonterminal'] as const)await check('OEM retired owner rejects incomplete '+weak,async f=>{
   await loseThenRetire(f);const snapshot=f.host.snapshot.bind(f.host);
   const patch:Record<string,unknown>=weak==='virtual'?{hcVirtualCloseReturned:false}:weak==='manager'?{hcDeviceManagerStopCompleted:false}:
     weak==='pending'?{hcCloseCleanupPending:true}:weak==='open'?{openCalled:true}:weak==='events'?{openEventsCalled:true}:
     weak==='write-enabled'?{hardwareWritesEnabled:true}:weak==='writes'?{hardwareWrites:true}:weak==='accepting'?{controlAccepting:true}:
     weak==='lease'?{lease:{leaseId:'lease-still-owned',generation:1}}:weak==='transition'?{powerOperationStatus:'executing'}:{state:'AwaitingControl'};
   f.host.snapshot=()=>({...snapshot(),...patch});await f.clock.advance(35000);
   assert.equal(f.counts().starts,1,'weak/contradictory retirement started a second owner');
   assert.equal(f.host.calls.filter(x=>x==='enable').length,2,'unknown write replayed without retirement');
   assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);
   const end=f.host.calls.length;await f.clock.advance(10000);assert.equal(f.host.calls.length,end,'exhausted observer kept running');
   f.host.snapshot=snapshot;return f.counts();
 });
 await check('OEM retired owner waits for exact predecessor exit',async f=>{
   await loseThenRetire(f);const gate=f.holdStop();await f.clock.advance(250);assert(gate.wasEntered,'automatic recovery did not reach exact exit');
   assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);
   gate.resolve();await f.clock.advance(5000);assert.equal(f.life.state,'ready');assert.equal(f.counts().starts,2);
   return{counts:f.counts(),trace:f.trace.slice()};
 });
 await check('OEM retired owner refuses unconfirmed exact exit and manual remains usable',async f=>{
   await loseThenRetire(f);f.denyExit(true);await f.clock.advance(35000);
   assert.equal(f.counts().starts,1);assert(!f.life.recoveryActive);f.denyExit(false);
   assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return f.counts();
 });
 for(const boundary of ['off','close','sleep','same-pid-owner'] as const)await check('OEM retired owner discards a late retired observation after '+boundary,async f=>{
   await loseThenRetire(f);const gate=held<FanState>();const retired=f.host.snapshot();f.host.holdState=gate;await f.clock.advance(250);
   const task=boundary==='off'?f.life.disable():boundary==='close'?f.life.close():Promise.resolve();
   if(boundary==='sleep'){f.native({generation:7,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   if(boundary==='same-pid-owner'){const owner=f.life as any;owner.process={...owner.process};}
   gate.resolve(retired);await task.catch(()=>{});await f.clock.advance(5000);
   assert.equal(f.counts().starts,1,'late old observation restarted control');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);
   return f.counts();
 });
 for(const boundary of ['off','close','sleep'] as const)await check('OEM retired owner cancels while exact exit is pending: '+boundary,async f=>{
   await loseThenRetire(f);const gate=f.holdStop();await f.clock.advance(250);assert(gate.wasEntered,'automatic recovery did not reach exact exit');
   const task=boundary==='off'?f.life.disable():boundary==='close'?f.life.close():Promise.resolve();
   if(boundary==='sleep'){f.native({generation:7,phase:'suspending',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   gate.resolve();await task.catch(()=>{});await f.clock.advance(5000);
   assert.equal(f.counts().starts,1,'cancelled automatic replacement launched');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return f.counts();
 });
 await check('OEM retired owner obeys original deadline during exact exit',async f=>{
   await loseThenRetire(f);const gate=f.holdStop();await f.clock.advance(250);assert(gate.wasEntered,'automatic recovery did not reach exact exit');f.clock.now+=31000;gate.resolve();await f.clock.advance(1000);
   assert.equal(f.counts().starts,1,'expired recovery launched a successor');assert(!f.life.recoveryActive);return f.counts();
 });
 await check('OEM retired owner permits at most one automatic replacement per episode',async f=>{
   await loseThenRetire(f);f.host.enable=async()=>{f.host.calls.push('enable');f.host.lease=null;f.host.settle('fault');throw new FanApiError('new owner callback failed',500,'HOST_ERROR');};
   await f.clock.advance(35000);assert.equal(f.counts().starts,2,'OEM failures created a restart loop');assert.equal(f.counts().maxLive,1);
   assert(f.life.recoveryNeedsAttention);assert(!f.life.recoveryActive);return f.counts();
 });
 await check('OEM retired owner new curve during exact exit supersedes old curve',async f=>{
   await loseThenRetire(f);const gate=f.holdStop();await f.clock.advance(250);assert(gate.wasEntered,'automatic recovery did not reach exact exit');assert(f.life.stageRecoveryCurve(curve(85)));
   gate.resolve();await f.clock.advance(5000);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(85));assert.equal(f.counts().maxLive,1);return f.counts();
 });
 // FAN1207: known heartbeat faults hand off instead of polling the old token forever.
 async function prepareLiveHeartbeat(f:Awaited<ReturnType<typeof fixture>>) {
   f.host.settle('open');f.host.lease=f.life.currentLease;await f.life.apply(curve(65));
   assert(f.life.currentLease&&f.host.lease,'fixture has no live heartbeat lease');
 }
 async function loseHeartbeatThenRetire(f:Awaited<ReturnType<typeof fixture>>,code:string,retired=true) {
   await prepareLiveHeartbeat(f);const heartbeat=f.host.heartbeat.bind(f.host);let fail=true;
   f.host.heartbeat=async()=>{if(fail){fail=false;f.host.calls.push('heartbeat');f.host.lease=null;f.host.nodes=null;f.host.settle('fault');f.host.closed=false;
     if(!retired){f.host.retired=false;f.host.opened=f.host.events=true;}
     throw new FanApiError('current HC session is unavailable',409,code);}return heartbeat();};
   const requestBegin=f.host.calls.length;
   await assert.rejects(f.life.heartbeat(),e=>e instanceof FanApiError&&e.errorCode===code);
   assert.deepEqual(f.host.calls.slice(requestBegin),['heartbeat'],'known fault attempted control before the bounded read owner');
   assert(f.life.recoveryActive,'known heartbeat fault did not hand off to bounded recovery');
   assert.equal(f.life.currentLease,null,'faulted lease retained for periodic renewal');
   assert.equal((f.life as any).heartbeatTimer,null,'faulted owner retained its heartbeat timer');
 }
 for(const family of families)for(const code of ['FAULT_LOCKED','HC_SESSION_UNAVAILABLE'])await check('heartbeat '+code+' retired owner recovers without another click',async f=>{
   await loseHeartbeatThenRetire(f,code);await f.clock.advance(5000);
   assert.equal(f.life.state,'ready');assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);assert.deepEqual(f.host.nodes,curve(65));
   assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});assert.equal(f.host.calls.filter(x=>x==='close').length,0);
   assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);return{counts:f.counts(),calls:f.host.calls.slice(),trace:f.trace.slice(),physicalConfirmed:false};
 },family);
 for(const code of ['FAULT_LOCKED','HC_SESSION_UNAVAILABLE'])await check('heartbeat '+code+' unretired owner stops work finitely and explicit ON stays usable',async f=>{
   await loseHeartbeatThenRetire(f,code,false);await f.clock.advance(35000);
   assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert(f.life.hasControlIntent);assert.equal(f.counts().starts,1);
   const end=f.host.calls.length;await f.clock.advance(20000);assert.equal(f.host.calls.length,end,'dead heartbeat or probes kept running after exhaustion');
   f.host.settle('fault');f.host.closed=false;assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return f.counts();
 });
 for(const [status,code]of [[0,'FAN_HTTP_RESULT_UNKNOWN'],[500,'HOST_ERROR'],[503,'FAULT_LOCKED'],[409,'HC_OPERATION_BUSY']]as const)await check('heartbeat uncertain '+status+'/'+code+' preserves the issued lease',async f=>{
   await prepareLiveHeartbeat(f);const lease=f.life.currentLease,heartbeat=f.host.heartbeat.bind(f.host),end=f.host.calls.length;
   f.host.heartbeat=async()=>{f.host.calls.push('heartbeat');throw new FanApiError('unconfirmed heartbeat outcome',status,code);};
   await assert.rejects(f.life.heartbeat());assert.deepEqual(f.life.currentLease,lease);assert((f.life as any).heartbeatTimer!==null);assert(!f.life.recoveryActive);
   assert.deepEqual(f.host.calls.slice(end),['heartbeat'],'uncertain receipt caused an extra control/read request');f.host.heartbeat=heartbeat;return f.counts();
 });
 for(const code of ['FAULT_LOCKED','HC_SESSION_UNAVAILABLE'])for(const edge of ['off','sleep','new-curve']as const)await check('late heartbeat '+code+' cannot alter '+edge,async f=>{
   await prepareLiveHeartbeat(f);const g=held<void>(),heartbeat=f.host.heartbeat.bind(f.host);let fail=true;
   f.host.heartbeat=async()=>{if(fail){fail=false;f.host.calls.push('heartbeat');g.entered();await g.promise;throw new FanApiError('late retired fault',409,code);}return heartbeat();};
   const pending=f.life.heartbeat();await f.clock.flush();assert(g.wasEntered);let next:Promise<unknown>=Promise.resolve();
   if(edge==='off')next=f.life.disable();else if(edge==='new-curve')next=f.life.apply(curve(85));else{f.native({generation:7,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   g.resolve();await assert.rejects(pending);await next.catch(()=>{});await f.clock.advance(1000);
   assert.equal(f.counts().starts,1,'late heartbeat launched an old replacement');assert(!f.life.recoveryActive,'late heartbeat registered old self-heal');
   if(edge==='off')assert(!f.life.hasControlIntent);if(edge==='new-curve'){assert.deepEqual(f.host.nodes,curve(85));assert(f.life.currentLease);}
   return f.counts();
 });
 await check('faulted heartbeat stops periodic work after its finite observation budget',async f=>{
   await prepareLiveHeartbeat(f);f.host.stateName='FaultLocked';f.host.unknown=true;f.host.retired=false;f.host.lease=null;
   f.host.heartbeat=async()=>{f.host.calls.push('heartbeat');throw new FanApiError('known current fault',409,'FAULT_LOCKED');};
   const begin=f.host.calls.length;await assert.rejects(f.life.heartbeat());await f.clock.advance(55000);
   const evidence={fixtureElapsedMs:55000,heartbeatCalls:f.host.calls.slice(begin).filter(x=>x==='heartbeat').length,stateReads:f.host.calls.slice(begin).filter(x=>x==='state').length};f.trace.push('work-count:'+JSON.stringify(evidence));
   assert.equal(evidence.heartbeatCalls,1,'faulted token kept receiving periodic requests');assert.equal(evidence.stateReads,10,'bounded recovery must stop after its one slow observation stage');
   assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert(f.life.hasControlIntent);return evidence;
 });
 // FAN1219: a committed native wake may fail before resumeInternal is reached.
 async function failingWake(f:Awaited<ReturnType<typeof fixture>>, code='HC_RESUME_REBUILD_FAILED', status=409,
   retire=true, gate?:ReturnType<typeof held<void>>, path:'committed'|'lifecycle'='committed') {
   f.host.pending=false;f.host.retired=false;f.host.closed=false;f.host.opened=f.host.events=false;
   f.host.lease=null;f.host.nodes=null;f.host.stateName='Suspended';f.host.powerState='Suspended';
   f.host.generation=6;f.host.attempt=1;
   f.native({generation:7,phase:'ready',hardwareWritesAllowed:true,resumeReady:true});
   f.life.setPowerGeneration(7);f.life.observePowerBoundary('resuming',7);f.life.observePowerBoundary('resume-ready',7);
   f.host.resume=async()=>{
     f.host.calls.push('resume');if(gate){gate.entered();await gate.promise;}
     f.host.lease=null;f.host.settle('fault');f.host.retired=retire;f.host.closed=false;
     throw new FanApiError('wake rebuild failed without an OEM ownership receipt',status,code);
   };
   if(path==='lifecycle'){await f.life.requestRecoveryAfterObservedHostState('failed-wake-fixture');return;}
   return f.life.resumeAfterCommittedPowerWake(7);
 }
 for(const code of ['HC_RESUME_REBUILD_FAILED','HOST_ERROR'])for(const path of ['committed','lifecycle'] as const)await check(path+' wake '+code+' observes a retired owner then recovers once',async f=>{
   const pending=failingWake(f,code,code==='HOST_ERROR'?500:409,true,undefined,path);
   if(path==='committed')await assert.rejects(pending);else await pending;
   await f.clock.advance(15000);assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(100));
   assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});assert.equal(f.host.calls.filter(x=>x==='resume').length,1,'failed wake retried instead of bounded readback');return f.counts();
 });
 for(const code of ['HC_RESUME_REBUILD_FAILED','HOST_ERROR'])await check('lifecycle wake '+code+' without retirement stops observing',async f=>{
   await failingWake(f,code,code==='HOST_ERROR'?500:409,false,undefined,'lifecycle');
   await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert.equal(f.counts().starts,1);
   assert.equal(f.host.calls.filter(x=>x==='resume').length,1,'unknown OEM outcome kept resubmitting resume');
   const calls=f.host.calls.length;await f.clock.advance(35000);assert.equal(f.host.calls.length,calls);return f.counts();
 });
 for(const family of families)await check('committed wake rebuild failure retains bounded recovery without OEM signals',async f=>{
   const snapshot=f.host.snapshot.bind(f.host);f.host.snapshot=()=>{const v=snapshot();delete v.oemPhysicalOwnershipConfirmed;delete v.oemOwnershipStatus;return v;};
   await assert.rejects(failingWake(f),e=>e instanceof FanApiError&&e.errorCode==='HC_RESUME_REBUILD_FAILED');
   assert(f.life.recoveryActive,'committed wake failure lost its bounded recovery owner');
   await f.clock.advance(5000);assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(100));
   assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});assert.equal(f.host.calls.filter(x=>x==='resume').length,1);
   assert.equal(f.host.calls.filter(x=>x==='close').length,0);return f.counts();
 },family);
 await check('committed wake rebuild failure without retirement stops and permits new manual control',async f=>{
   await assert.rejects(failingWake(f,'HC_RESUME_REBUILD_FAILED',409,false));assert(f.life.recoveryActive);
   const enables=f.host.calls.filter(x=>x==='enable').length;await f.clock.advance(35000);
   assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert(f.life.hasControlIntent);
   assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,enables);
   const calls=f.host.calls.length;await f.clock.advance(35000);assert.equal(f.host.calls.length,calls,'finite observer became a permanent poller');
   f.host.settle('open');f.host.generation=7;f.host.attempt=1;f.host.powerState='On';
   await f.life.apply(curve(75));assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(75));return f.counts();
 });
 for(const status of [401,403])await check('committed wake failure label preserves authentication '+status,async f=>{
   await assert.rejects(failingWake(f,'HC_RESUME_REBUILD_FAILED',status));assert(!f.life.recoveryActive);
   const calls=f.host.calls.length;await f.clock.advance(35000);assert.equal(f.host.calls.length,calls);assert.equal(f.counts().starts,1);return f.counts();
 });
 for(const weak of ['no-native','writes-denied','virtual-close','pending','lease'] as const)await check('failed wake observation cannot replace with '+weak,async f=>{
   await assert.rejects(failingWake(f));const snapshot=f.host.snapshot.bind(f.host);
   if(weak==='no-native')f.native(null);
   else if(weak==='writes-denied')f.native({generation:7,phase:'ready',hardwareWritesAllowed:false,resumeReady:true});
   else f.host.snapshot=()=>({...snapshot(),...(weak==='virtual-close'?{hcVirtualCloseReturned:false}:weak==='pending'?{hcCloseCleanupPending:true}:{lease:{leaseId:'unretired-fixture-owner',generation:9}})});
   const end=f.host.calls.length;await f.clock.advance(35000);f.host.snapshot=snapshot;
   assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert.equal(f.counts().starts,1);assert.equal(f.counts().maxLive,1);
   assert(!f.host.calls.slice(end).some(x=>['enable','acquire','open','resume'].includes(x)),'observation crossed an unproven write/startup boundary');return f.counts();
 });
 await check('failed wake observes temporary OEM status errors then retires once',async f=>{
   await assert.rejects(failingWake(f));const getState=f.host.getState.bind(f.host);let failures=2;
   f.host.getState=async()=>{if(failures-->0){f.host.calls.push('state');throw new FanApiError('OEM status unavailable',503,'HOST_ERROR');}return getState();};
   await f.clock.advance(5000);assert.equal(f.life.state,'ready');assert(!f.life.recoveryActive);assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});return f.counts();
 });
 await check('failed wake cannot replace before exact predecessor exit',async f=>{
   await assert.rejects(failingWake(f));f.denyExit(true);await f.clock.advance(35000);
   assert.equal(f.counts().starts,1);assert.equal(f.counts().maxLive,1);assert(!f.life.recoveryActive);assert(f.life.hasControlIntent);
   f.denyExit(false);await f.life.apply(curve(75));assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(75));assert.equal(f.counts().maxLive,1);return f.counts();
 });
 for(const code of ['HC_RESUME_SUPERSEDED','HC_RESUME_RESULT_EXPIRED'])await check('committed wake does not treat '+code+' as a failed current rebuild',async f=>{
   await assert.rejects(failingWake(f,code));assert(!f.life.recoveryActive);await f.clock.advance(35000);assert.equal(f.counts().starts,1);return f.counts();
 });
 for(const edge of ['off','new-sleep','new-curve','same-pid-owner','new-session'] as const)await check('late committed wake failure cannot recover old '+edge,async f=>{
   const g=held<void>(),pending=failingWake(f,'HC_RESUME_REBUILD_FAILED',409,true,g);await f.clock.flush();assert(g.wasEntered);
   let next:Promise<unknown>=Promise.resolve();
   if(edge==='off')next=f.life.disable();
   else if(edge==='new-sleep'){f.native({generation:8,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(8);f.life.observePowerBoundary('suspending',8);}
   else if(edge==='new-curve')f.life.stagePendingControlCurve(curve(85));
   else if(edge==='new-session'){(f.life as any).sessionGeneration+=1;}
   else {const owner=f.life as any;owner.process={...owner.process};}
   g.resolve();await assert.rejects(pending);await next.catch(()=>{});await f.clock.advance(2000);
   assert.equal(f.counts().starts,1,'late failed rebuild launched another Host');assert(!(f.life as any).recoveryIntent?.selfHeal,'late failure bound itself to a newer owner');
   if(edge==='off')assert(!f.life.hasControlIntent);return f.counts();
 });
 // FAN1220: the latest explicit curve must keep a reachable recovery path.
 for(const family of families)await check('new curve during failed wake recovers latest intent after exact retirement',async f=>{
   await assert.rejects(failingWake(f));assert(f.life.stageRecoveryCurve(curve(80)));
   await f.clock.advance(5000);assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(80));assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});
   assert.equal(f.host.calls.filter(x=>x==='resume').length,1,'new curve restarted the failed wake instead of continuing recovery');return f.counts();
 },family);
 await check('latest staged curve supersedes a held failed-wake retirement read',async f=>{
   await assert.rejects(failingWake(f));const gate=held<FanState>(),old=f.host.snapshot();f.host.holdState=gate;
   await f.clock.advance(250);assert(gate.wasEntered);assert(f.life.stageRecoveryCurve(curve(70)));assert(f.life.stageRecoveryCurve(curve(85)));
   gate.resolve(old);await f.clock.advance(5000);assert.deepEqual(f.host.nodes,curve(85));assert(!f.life.recoveryActive);assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});return f.counts();
 });
 await check('new curve restarts an exhausted failed-wake observer without resetting the old episode',async f=>{
   await assert.rejects(failingWake(f,'HC_RESUME_REBUILD_FAILED',409,false));await f.clock.advance(35000);assert(f.life.recoveryNeedsAttention);
   f.host.retired=true;assert(f.life.stageRecoveryCurve(curve(60)));await f.clock.advance(500);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(60));assert(!f.life.recoveryActive);assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});return f.counts();
 });
 for(const family of families)await check('known unsubmitted busy observes a later retired owner and resumes latest curve',async f=>{
   f.host.settle('open');const enable=f.host.enable.bind(f.host);let refuse=true;
   f.host.enable=async(nodes:readonly FanNode[]=[])=>{if(refuse){refuse=false;f.host.calls.push('enable');f.host.lease=null;f.host.settle('fault');f.host.closed=false;throw new FanApiError('not submitted before retirement',409,'HC_OPERATION_BUSY');}return enable(nodes);};
   await assert.rejects(f.life.apply(curve(65)));assert(f.life.recoveryActive);await f.clock.advance(5000);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));assert(!f.life.recoveryActive);assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});return f.counts();
 },family);
 await check('new explicit curve replaces an old long backoff with one short recovery tick',async f=>{
   await assert.rejects(failingWake(f,'HC_RESUME_REBUILD_FAILED',409,false));await f.clock.advance(2000);
   f.host.retired=true;assert(f.life.stageRecoveryCurve(curve(75)));await f.clock.advance(500);
   assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(75));assert(!f.life.recoveryActive);assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});return f.counts();
 });
 for(const weak of ['virtual','manager','pending','lease','transition','native-denied'] as const)await check('known busy still requires complete retirement: '+weak,async f=>{
   f.host.settle('open');const enable=f.host.enable.bind(f.host);let refuse=true;
   f.host.enable=async(nodes:readonly FanNode[]=[])=>{if(refuse){refuse=false;f.host.calls.push('enable');f.host.lease=null;f.host.settle('fault');f.host.closed=false;throw new FanApiError('not submitted',409,'HC_OPERATION_BUSY');}return enable(nodes);};
   await assert.rejects(f.life.apply(curve(65)));const snapshot=f.host.snapshot.bind(f.host),end=f.host.calls.length;
   if(weak==='native-denied')f.native({generation:6,phase:'ready',hardwareWritesAllowed:false,resumeReady:true});
   else f.host.snapshot=()=>({...snapshot(),...(weak==='virtual'?{hcVirtualCloseReturned:false}:weak==='manager'?{hcDeviceManagerStopCompleted:false}:weak==='pending'?{hcCloseCleanupPending:true}:weak==='lease'?{lease:{leaseId:'still-owned',generation:9}}:{powerOperationStatus:'executing'})});
   await f.clock.advance(35000);f.host.snapshot=snapshot;
   assert.equal(f.counts().starts,1);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);
   assert(!f.host.calls.slice(end).some(x=>['enable','open','acquire'].includes(x)),'weak resource evidence granted a new write');return f.counts();
 });
 for(const edge of ['off','sleep'] as const)await check('latest failed-wake curve remains cancelled by '+edge,async f=>{
   await assert.rejects(failingWake(f));assert(f.life.stageRecoveryCurve(curve(80)));
   if(edge==='off')await f.life.disable().catch(()=>{});
   else{f.native({generation:8,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(8);f.life.observePowerBoundary('suspending',8);}
   const end=f.host.calls.length;await f.clock.advance(2000);assert.equal(f.counts().starts,1);assert(!f.host.calls.slice(end).some(x=>['enable','open','acquire'].includes(x)));return f.counts();
 });
 // FAN1224: the local Close wait can fail after an explicit start owns it.
 async function manualCloseWait(f:Awaited<ReturnType<typeof fixture>>){
   f.host.settle('fault');f.host.retired=false;f.host.closed=false;f.life.stagePendingControlCurve(curve(65));
   const close=f.host.close.bind(f.host);f.host.close=async()=>{f.host.calls.push('close');f.host.enterPending();return f.host.snapshot();};
   const result=f.life.start({manualRecovery:true,reason:'one-user-start-close-wait'}).then(value=>({value}),error=>({error}));
   await f.clock.flush();return{result,close};
 }
 for(const family of families)for(const terminal of ['fault','stopped']as const)await check('manual start close wait resumes one retained request after '+terminal,async f=>{
   const first=await manualCloseWait(f);
   try{
    await f.clock.advance(17000);const result=await first.result;assert('error'in result&&result.error instanceof Error&&result.error.message.startsWith('HC_CLOSE_PENDING:'),'fixture did not exercise local Close cleanup deadline');
    assert(f.life.recoveryActive,'local manual Close wait lost its bounded recovery owner');
    f.host.settle(terminal);await f.clock.advance(5000);
    assert(!f.life.recoveryActive);assert(!f.life.recoveryNeedsAttention);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve(65));
    assert.equal(f.host.calls.filter(x=>x==='close').length,1,'manual cleanup was submitted twice');assert.deepEqual(f.counts(),{starts:2,stops:1,maxLive:1});return f.counts();
   }finally{f.host.close=first.close;}
 },family);
 await check('manual Close wait stays finite when HC never confirms resource cleanup',async f=>{
   const first=await manualCloseWait(f);
   try{
    await f.clock.advance(17000);await first.result;assert(f.life.recoveryActive);const writes=f.host.calls.filter(x=>x==='enable').length;
    await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert(f.life.hasControlIntent);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,writes);assert.equal(f.host.calls.filter(x=>x==='close').length,1);
    const end=f.host.calls.length;await f.clock.advance(10000);assert.equal(f.host.calls.length,end);
    f.host.settle('stopped');await f.life.start({manualRecovery:true});await f.life.apply(curve(80));assert.deepEqual(f.host.nodes,curve(80));return f.counts();
   }finally{f.host.close=first.close;}
 });
 for(const edge of ['off','new-curve','new-sleep','same-wake','new-session']as const)await check('late local manual Close wait cannot attach to '+edge,async f=>{
   const first=await manualCloseWait(f);
   try{
    await f.clock.advance(1000);let pending:Promise<unknown>=Promise.resolve();
    if(edge==='off')pending=f.life.disable();else if(edge==='new-curve')f.life.stagePendingControlCurve(curve(85));
    else if(edge==='new-sleep'){f.native({generation:7,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
    else if(edge==='new-session'){(f.life as any).sessionGeneration+=1;}
    else{f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);}
    await f.clock.advance(17000);await first.result;await pending.catch(()=>{});
    assert(!(f.life as any).recoveryIntent?.selfHeal,'old cleanup failure registered a newer boundary observer');assert.equal(f.counts().starts,1);return f.counts();
   }finally{f.host.close=first.close;}
 });
 await check('local manual Close wait never retries Close from missing resource receipts',async f=>{
   const first=await manualCloseWait(f);
   try{
    await f.clock.advance(17000);await first.result;assert(f.life.recoveryActive);
    f.host.settle('fault');f.host.retired=false;await f.clock.advance(35000);
    assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='close').length,1,'unknown original Close was resubmitted');return f.counts();
   }finally{f.host.close=first.close;}
 });
 await check('local manual Close wait still requires exact predecessor exit',async f=>{
   const first=await manualCloseWait(f);
   try{
    await f.clock.advance(17000);await first.result;assert(f.life.recoveryActive);f.host.settle('stopped');f.denyExit(true);await f.clock.advance(35000);
    assert.equal(f.counts().starts,1);assert.equal(f.counts().maxLive,1);assert(!f.life.recoveryActive);
    f.denyExit(false);await f.life.start({manualRecovery:true});await f.life.apply(curve(80));assert.deepEqual(f.host.nodes,curve(80));assert.equal(f.counts().maxLive,1);return f.counts();
   }finally{f.host.close=first.close;}
 });
 for(const status of [401,403])await check('manual Close failure preserves authentication '+status,async f=>{
   f.host.settle('fault');f.host.retired=false;f.host.closed=false;f.life.stagePendingControlCurve(curve(65));const close=f.host.close.bind(f.host);
   f.host.close=async()=>{f.host.calls.push('close');throw new FanApiError('HC_CLOSE_PENDING: declared auth refusal',status,'AUTH_REJECTED');};
   try{await assert.rejects(f.life.start({manualRecovery:true}));assert(!f.life.recoveryActive);assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='close').length,1);return f.counts();}finally{f.host.close=close;}
 });
 console.log(JSON.stringify({ok:results.every(r=>r.ok),checks:results.length,hardwareCalls:0,results,scope:'real startup + original recovery owner/queue/HC admission; inert Host/native/exact-exit launcher, no real hardware/OS sleep/CPU benchmark'}));if(results.some(r=>!r.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
