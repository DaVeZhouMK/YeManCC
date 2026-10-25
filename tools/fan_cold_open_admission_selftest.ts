/** FAN956: real public bridge + cold Stopped Host receipt after S4/new process.
 * Adapter/native/process fixtures only. No device, real Host, filesystem or hardware writes. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanState, type FanLease, type FanNode, type FanHandshake } from '../src/bridge/fanApi';
import type { PowerLifecycleState } from '../src/bridge/api';
const curve:FanNode[]=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const native=(g:number,phase:PowerLifecycleState['phase']='ready'):PowerLifecycleState=>({generation:g,phase,resumeReady:phase==='ready',hardwareWritesAllowed:phase==='ready',inputReady:true,hibernateAvailable:true});
class ColdAdapter implements FanApiAdapter {
 enabled=true; calls:string[]=[]; opened=false; events=false; writes=false; history=false; closed=false;
 lease:FanLease|null=null; overrides:Partial<FanState>={}; beforeNativeProbe: (()=>void)|null=null;
 family:string; constructor(family:string){this.family=family;}
 snapshot():FanState {const state=this.writes?'Ready':this.opened?'AwaitingControl':'Stopped';return {
  state,powerState:'Unknown',powerOperationGeneration:0,powerOperationAttempt:0,resumePhaseGeneration:0,resumePhase:state,
  powerOperationStatus:null,fanCapabilitySupported:true,controlAccepting:this.opened,
  openCalled:this.opened,openEventsCalled:this.events,closeCalled:this.closed,hardwareWritesEnabled:this.writes,
  hardwareWrites:this.writes,hardwareWritesObserved:this.history,unknownState:false,hcCloseCleanupPending:false,
  oemRestoreConfirmed:false,hcVirtualCloseReturned:false,hcDeviceManagerStopCompleted:false,lease:this.lease,
  leaseGeneration:this.lease?.generation??null,...this.overrides};}
 async handshake():Promise<FanHandshake>{this.calls.push('handshake');return {ok:true,supported:true,fanRouteWriteReady:true,
  deviceClass:this.family,fanRoute:this.family.includes('GPD')?'GenericDuty':'ProfileCurve',
  deviceIdentity:{manufacturer:'fixture',model:this.family,product:this.family}};}
 async getState():Promise<FanState>{this.calls.push('state');return this.snapshot();}
 async open():Promise<FanState>{this.calls.push('open');this.opened=true;return this.snapshot();}
 async openEvents():Promise<FanState>{this.calls.push('open-events');this.events=true;return this.snapshot();}
 async acquireControl():Promise<FanLease>{this.calls.push('acquire');return this.lease={leaseId:'fixture-cold-lease',generation:1,expiresAt:Date.now()+15000};}
 async heartbeat():Promise<FanLease>{assert(this.lease);return this.lease;}
 async enable(nodes:readonly FanNode[],leaseId?:string):Promise<FanState>{assert(this.opened&&this.events&&leaseId===this.lease?.leaseId);this.calls.push('enable');this.writes=true;this.history=true;return this.snapshot();}
 async applyPreset(_n:'soft'|'balanced'|'aggressive',id?:string,nodes?:readonly FanNode[]):Promise<FanState>{return this.enable(nodes??curve,id);}
 async disable():Promise<FanState>{this.writes=false;return this.snapshot();}
 async releaseControl():Promise<FanState>{this.writes=false;this.lease=null;return {...this.snapshot(),oemRestoreConfirmed:true};}
 async restoreOem():Promise<FanState>{this.writes=false;return {...this.snapshot(),oemRestoreConfirmed:true};}
 async suspend():Promise<FanState>{this.opened=false;this.events=false;return {...this.snapshot(),state:'Suspended',powerState:'Suspended'};}
 async resume():Promise<FanState>{this.calls.push('resume');throw new FanApiError('HC_RESUME_RESULT_EXPIRED',409,'cold Host has no earlier resume receipt');}
 async close():Promise<FanState>{this.calls.push('close');this.opened=false;this.events=false;this.closed=true;this.writes=false;this.lease=null;
  return {...this.snapshot(),hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true,oemRestoreConfirmed:true};}
 async shutdown():Promise<void>{this.calls.push('shutdown');}
}
let checks=0;const failures:string[]=[];const check=(name:string,ok:boolean)=>{checks++;if(!ok)failures.push(name);console.log((ok?'PASS ':'FAIL ')+name);};
async function make(adapter:ColdAdapter,g=2){let current=native(g);let starts=0;const lifecycle=new FanHostLifecycle({enabled:true,adapter,
 launcher:{async start(){starts++;return {pid:30000+starts,executable:'fixture.exe'};},async stop(){}},
 readNativeActivity:async()=>null,readNativePowerState:async()=>({...current}),heartbeatIntervalMs:60000,resumeWaitDeadlineMs:500,
 requestManualWake:async generation=>({ok:current.phase==='ready',generation,phase:current.phase})});
 lifecycle.setPowerGeneration(g);lifecycle.observePowerBoundary('resume-ready',g);
 return {lifecycle,setNative:(v:PowerLifecycleState)=>{current=v;},starts:()=>starts};}
async function run(){
 for(const family of ['HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.MSI.ClawA2VM','HandheldCompanion.Devices.ROGAlly','HandheldCompanion.Devices.GPD.GPDWin5']){
  const a=new ColdAdapter(family),fx=await make(a);let error='';
  try{await fx.lifecycle.start({manualRecovery:true,reason:'after-s4-first-click'});
   await fx.lifecycle.start({manualRecovery:true,reason:'before-current-write'});
   await fx.lifecycle.apply(curve);}catch(e){error=String(e);}
  check(family+' first click crosses cold Stopped to actual Open/Events/lease/enable',!error&&a.calls.filter(x=>x==='enable').length===1);
  check(family+' cold Host does not require a fictitious resume receipt',!a.calls.includes('resume'));
  check(family+' exactly one process owner',fx.starts()===1);
  if(error)console.log('ERROR '+error);
  await fx.lifecycle.close().catch(()=>{});
 }
 // A native sleep or newer generation after deferred cold admission invalidates it.
 for(const invalid of ['suspending','new-generation'] as const){const a=new ColdAdapter('HandheldCompanion.Devices.MSI.ClawCG3EM'),fx=await make(a);
  try{await fx.lifecycle.start({manualRecovery:true});fx.setNative(invalid==='suspending'?native(2,'suspending'):native(3));
   await fx.lifecycle.apply(curve).catch(()=>{});check(invalid+' cannot borrow prior cold admission to open or write',!a.calls.includes('open')&&!a.calls.includes('enable'));
  }finally{await fx.lifecycle.close().catch(()=>{});}
 }
 // Dirty/closed/unknown sessions are never equivalent to a brand-new Host.
 for(const [label,fields] of [['old-close',{closeCalled:true}],['old-write',{hardwareWritesObserved:true}],['unknown',{unknownState:true}],['pending-cleanup',{hcCloseCleanupPending:true}]] as const){
  const a=new ColdAdapter('HandheldCompanion.Devices.MSI.ClawCG3EM');a.overrides={...fields};const fx=await make(a);
  try{await fx.lifecycle.start({manualRecovery:true}).then(()=>fx.lifecycle.apply(curve)).catch(()=>{});check(label+' does not obtain cold Open/write permission',!a.calls.includes('open')&&!a.calls.includes('enable'));}
  finally{a.overrides={};await fx.lifecycle.close().catch(()=>{});}
 }
 // Private Fan generations may lead a still-filtered shared generation.
 // Late shared events must not drop the current real lease or state.
 {const a=new ColdAdapter('HandheldCompanion.Devices.MSI.ClawCG3EM'),fx=await make(a,3);
  try{await fx.lifecycle.start({manualRecovery:true});await fx.lifecycle.apply(curve);
   fx.lifecycle.observePowerBoundary('resuming',2);fx.lifecycle.observePowerBoundary('suspending',2);
   check('stale shared power events cannot suspend the current Fan generation',fx.lifecycle.state==='ready');
   await fx.lifecycle.apply(curve);
   check('next real adjustment retains the lease after stale shared events',a.calls.filter(x=>x==='acquire').length===1);
  }finally{await fx.lifecycle.close().catch(()=>{});}}
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,hardwareCalls:0,scope:'real-production-FanHostLifecycle; exact-cold-Stopped-receipt; native/Host/process-fixtures'}));
 if(failures.length)process.exitCode=1;
}
void run();
