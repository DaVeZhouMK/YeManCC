/** FAN982: production bridge, inert Host/native/process fixtures only. No device I/O. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { FanNode, FanState } from '../src/bridge/fanApi';
import type { PowerLifecycleState } from '../src/bridge/api';
const curve: FanNode[] = [{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const next = curve.map(n=>({...n,dutyPercent:75}));
const native = (g=6, phase:PowerLifecycleState['phase']='ready'):PowerLifecycleState => ({generation:g,phase,resumeReady:phase==='ready',hardwareWritesAllowed:phase==='ready',inputReady:true,hibernateAvailable:true});
const families = ['MSI.ClawCG3EM','MSI.ClawA2VM','ROGAlly','GPD.GPDWin5','AYANEO2021','OneXPlayerX1Mini','Lenovo.LegionGo','Ayn.LokiMax'];
class Host extends NoIoHost {
 idleResume=false; releaseOpen:(()=>void)|null=null; openEntered=false; gate:Promise<void>|null=null;
 eventGate:Promise<void>|null=null; releaseEvents:(()=>void)|null=null; eventsEntered=false; eventsInFlight=0; maxEventsInFlight=0;
 history=false; applied:FanNode[][]=[]; overrides:Partial<FanState>={}; resumeReads=0; delayedIdle=false;
 constructor(family:string){super();this.factoryType='HandheldCompanion.Devices.'+family;this.stateName='Stopped';}
 override snapshot():FanState {return {...super.snapshot(),hardwareWritesObserved:this.history,
  ...(this.attempt?{powerOperationOutcome:'await-user'}:{}),...this.overrides};}
 override async open(){this.calls.push('open');this.openEntered=true;if(this.gate)await this.gate;
  this.opened=true;this.stateName='Open';return this.snapshot();}
 override async openEvents(){this.calls.push('events');this.eventsEntered=true;this.maxEventsInFlight=Math.max(this.maxEventsInFlight,++this.eventsInFlight);
  try{if(this.eventGate)await this.eventGate;this.events=true;this.stateName='AwaitingControl';return this.snapshot();}finally{this.eventsInFlight--;}}
 override async enable(nodes?:readonly FanNode[]){assert(this.opened&&this.events&&this.lease);this.applied.push((nodes??curve).map(n=>({...n})));return super.enable();}
 override async applyPreset(_name?:string,_lease?:string,nodes?:readonly FanNode[]){assert(this.opened&&this.events&&this.lease);this.applied.push((nodes??curve).map(n=>({...n})));return super.applyPreset();}
 override async restoreOem(){this.history=true;return super.restoreOem();}
 override async resume(request:{generation:number}){
  if(!this.idleResume)return super.resume(request);
  this.calls.push('resume');this.generation=request.generation;this.attempt++;
  this.stateName=this.delayedIdle?'Resuming':'AwaitingControl';this.powerState=this.delayedIdle?'Resuming':'On';
  this.opened=this.events=false;return this.snapshot();
 }
 override async getState(){if(this.delayedIdle&&this.calls.includes('resume')&&++this.resumeReads>=3){this.stateName='AwaitingControl';this.powerState='On';}return super.getState();}
 armOpen(){this.gate=new Promise<void>(resolve=>{this.releaseOpen=resolve;});}
}
let checks=0;const failures:string[]=[];
async function check(name:string,work:()=>Promise<void>){checks++;try{await work();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
function make(host:Host){let current=native();let starts=0;const life=new FanHostLifecycle({enabled:true,adapter:host,
 launcher:{async start(){starts++;return {pid:98200+starts,executable:'fixture-fan.exe'};},async stop(){}},readNativeActivity:async()=>null,
 readNativePowerState:async()=>({...current}),heartbeatIntervalMs:0,resumeWaitDeadlineMs:800,
 requestManualWake:async generation=>({ok:current.phase==='ready',generation,phase:current.phase})});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
 return {life,starts:()=>starts,setNative:(state:PowerLifecycleState)=>{current=state;}};}
async function finish(life:FanHostLifecycle,host:Host){host.overrides={};host.releaseOpen?.();await life.close().catch(()=>{});}
async function interrupted(host:Host,fx:ReturnType<typeof make>,kind:'off'|'new-curve'){
 await fx.life.start({manualRecovery:true});host.armOpen();
 const old=fx.life.apply(curve).then(()=> 'success',e=>String(e));
 for(let spin=0;spin<100&&!host.openEntered;spin++)await Promise.resolve();assert(host.openEntered);
 const interrupt=kind==='off'?fx.life.disable():fx.life.apply(next);
 const pending=interrupt.then(()=> 'success',e=>String(e));
 host.releaseOpen?.();assert.match(await old,/FAN_CONTROL_INTENT_SUPERSEDED/);
 const terminal=await pending;
 assert(!host.applied.some(nodes=>nodes[0].dutyPercent===100),'retired curve cannot write');
 return terminal;
}
async function main(){
 for (const family of families) await check(family+' never-enabled sleep/wake remains zero-work then one manual ON reaches control',async()=>{
  const h=new Host(family),fx=make(h);
  try{
   fx.life.observePowerBoundary('suspending',6);
   fx.life.observePowerBoundary('resuming',6);fx.life.observePowerBoundary('resume-ready',6);
   await fx.life.resume();
   assert.equal(fx.starts(),0);assert.equal(h.calls.length,0);assert.equal(fx.life.state,'stopped');
   await fx.life.start({manualRecovery:true});await fx.life.apply(curve);
   assert.equal(fx.starts(),1);
   assert.deepEqual(h.calls.filter(x=>['resume','open','events','acquire','enable'].includes(x)),['open','events','acquire','enable']);
   assert.equal(h.applied.length,1);assert.deepEqual(h.applied[0],curve);
  }finally{await finish(fx.life,h);}
 });

 for(const family of families){
  for(const delayed of [false,true])for(const resumeShape of ['await-user','completed-half-open'] as const)await check(family+' '+resumeShape+' during resume reaches actual Open/Events on first request'+(delayed?' (polled)':''),async()=>{
   const h=new Host(family),fx=make(h);h.generation=6;h.stateName='Suspended';h.powerState='Suspended';h.idleResume=true;h.delayedIdle=delayed;
   if(resumeShape==='completed-half-open') h.overrides={powerOperationOutcome:null,recoveryReadbackRequired:true};
   try{await fx.life.start({manualRecovery:true});await fx.life.apply(curve);
    assert.deepEqual(h.calls.filter(x=>['resume','open','events','acquire','enable'].includes(x)),['resume','open','events','acquire','enable']);assert.equal(fx.starts(),1);
   }finally{await finish(fx.life,h);}
  });
  for(const kind of ['off','new-curve'] as const)await check(family+' acknowledged cold Open interrupted by '+kind+' remains manually usable',async()=>{
   const h=new Host(family),fx=make(h);
   try{const result=await interrupted(h,fx,kind);assert.equal(result,'success');
    await fx.life.start({manualRecovery:true});await fx.life.apply(next);
    assert(h.applied.length>0);assert(h.applied.every(nodes=>nodes[0].dutyPercent===75));assert(!h.calls.includes('resume'));
    assert.equal(h.calls.filter(x=>x==='open').length,1);assert.equal(h.calls.filter(x=>x==='events').length,1);assert.equal(fx.starts(),1);
   }finally{await finish(fx.life,h);}
  });
 }
 // A new request while the previous Events call awaits must remain on the one hardware queue.
 for(const kind of ['new-curve','preset','off'] as const)await check('in-flight OpenEvents '+kind+' never starts a second HC call or revives the old curve',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.eventGate=new Promise<void>(resolve=>{h.releaseEvents=resolve;});
   const old=fx.life.apply(curve).then(()=> 'success',e=>String(e));
   for(let spin=0;spin<100&&!h.eventsEntered;spin++)await Promise.resolve();assert(h.eventsEntered);
   const latest=(kind==='off'?fx.life.disable():kind==='preset'?fx.life.applyPreset('balanced',next):fx.life.apply(next)).then(()=> 'success',e=>String(e));
   for(let spin=0;spin<40;spin++)await Promise.resolve();assert.equal(h.maxEventsInFlight,1);
   h.releaseEvents?.();assert.match(await old,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.equal(await latest,'success');
   assert.equal(h.maxEventsInFlight,1);assert.equal(h.calls.filter(x=>x==='events').length,1);
   assert(h.applied.every(nodes=>nodes[0].dutyPercent===75));if(kind==='off')assert.equal(h.applied.length,0);
  }finally{h.releaseEvents?.();await finish(fx.life,h);}
 });
 // Lost/cancelled replies cannot prove a fresh session across a real power boundary.
 for(const kind of ['suspending','new-generation'] as const)await check('Open acknowledgement arriving after '+kind+' cannot bind itself to the new epoch',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.armOpen();const old=fx.life.apply(curve).then(()=> 'success',e=>String(e));
   for(let spin=0;spin<100&&!h.openEntered;spin++)await Promise.resolve();assert(h.openEntered);
   if(kind==='new-generation'){fx.setNative(native(7,'suspending'));fx.life.setPowerGeneration(7);fx.life.observePowerBoundary('suspending',7);}
   else {fx.setNative(native(6,'suspending'));fx.life.observePowerBoundary('suspending',6);}
   h.releaseOpen?.();assert.match(await old,/FAN_CONTROL_INTENT_SUPERSEDED/);assert.equal(h.applied.length,0);
   fx.setNative(native(kind==='new-generation'?7:6));fx.life.observePowerBoundary('resume-ready',kind==='new-generation'?7:6);
   await fx.life.start({manualRecovery:true}).then(()=>fx.life.apply(next)).catch(()=>{});
   assert.equal(h.applied.length,0);assert.equal(h.calls.filter(x=>x==='events').length,0);
  }finally{await finish(fx.life,h);}
 });
 // Different wait contracts cannot share a verdict: accepting an idle shell is
 // not proof that its HC session is open. Exercise actual waiters, not key text.
 await check('a rearm waiter cannot borrow the ordinary control waiter acceptance of an unopened shell',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{await fx.life.start({manualRecovery:true});h.generation=6;h.attempt=1;h.powerState='On';h.stateName='AwaitingControl';
   const internal=fx.life as any,context=internal.admissionContext('apply',internal.controlIntentRevision);
   const first=internal.runBoundedResumeWait(context,performance.now()+800);
   const rearm=internal.runBoundedResumeWait({...context,action:'rearm-after-wake',requireHcOpen:true,allowIdleHcOpen:true},performance.now()+800);
   const result=await rearm;await first;assert.equal(result.reason,'hc-open-required');assert.equal(result.ok,false);
   assert(!h.calls.includes('events')&&!h.calls.includes('acquire')&&!h.calls.includes('enable'));
  }finally{await finish(fx.life,h);}
 });
 // Provenance and native permission remain mandatory for a half-open zero receipt.
 for(const kind of ['foreign','new-generation','same-generation-sleep','native-sleep','native-unknown','lease-owner','cleanup','fault','closed','positive-stale','invalid'] as const)await check('counterexample '+kind+' cannot borrow an old partial Open to write',async()=>{
  const h=new Host('MSI.ClawCG3EM'),fx=make(h);
  try{
   if(kind==='foreign'){await fx.life.start({manualRecovery:true});h.opened=true;h.stateName='Ready';}
   else {await interrupted(h,fx,'off');}
   if(kind==='new-generation'){fx.setNative(native(7));fx.life.setPowerGeneration(7);fx.life.observePowerBoundary('resume-ready',7);}
   if(kind==='same-generation-sleep'){fx.life.observePowerBoundary('suspending',6);fx.life.observePowerBoundary('resuming',6);fx.life.observePowerBoundary('resume-ready',6);}
   if(kind==='native-sleep')fx.setNative(native(6,'suspending'));
   if(kind==='native-unknown')fx.setNative({...native(),hardwareWritesAllowed:false});
   if(kind==='lease-owner')h.lease={leaseId:'other-owner',generation:99};
   if(kind==='cleanup')h.overrides={hcCloseCleanupPending:true};
   if(kind==='fault')h.overrides={unknownState:true};
   if(kind==='closed')h.closed=true;
   if(kind==='positive-stale'){h.generation=5;h.attempt=1;}
   if(kind==='invalid')h.overrides={resumePhaseGeneration:6,powerOperationGeneration:0};
   const count=h.applied.length,events=h.calls.filter(x=>x==='events').length;
   await fx.life.start({manualRecovery:true}).then(()=>fx.life.apply(next)).catch(()=>{});
   if(kind==='fault'){assert.equal(fx.starts(),2);assert(h.calls.includes('close'));}
   else {assert.equal(h.applied.length,count);assert.equal(h.calls.filter(x=>x==='events').length,events);}
  }finally{await finish(fx.life,h);}
 });
 console.log(JSON.stringify({ok:!failures.length,checks,failures,hardwareCalls:0,scope:'actual-FanHostLifecycle-public-entries; native/Host/process-fixtures'}));if(failures.length)process.exitCode=1;
}
void main();
