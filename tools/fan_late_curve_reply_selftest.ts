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


const deferred=<T>()=>{let resolve!:(value:T)=>void;const promise=new Promise<T>(r=>{resolve=r;});return {promise,resolve};};
let checks=0;const failures:string[]=[];
const check=(name:string,passed:boolean)=>{checks++;if(!passed)failures.push(name);console.log((passed?'PASS ':'FAIL ')+name);};
const newer:FanNode[]=[{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:35},{tempC:70,dutyPercent:60},{tempC:100,dutyPercent:85}];
async function make(a:ColdAdapter){const lifecycle=new FanHostLifecycle({enabled:true,adapter:a,
 launcher:{async start(){return {pid:30001,executable:'fixture.exe'};},async stop(){}},
 readNativeActivity:async()=>null,readNativePowerState:async()=>native(2),heartbeatIntervalMs:60000,resumeWaitDeadlineMs:300});
 lifecycle.setPowerGeneration(2);lifecycle.observePowerBoundary('resume-ready',2);
 await lifecycle.start({manualRecovery:true});return lifecycle;}
async function run(){
 for(const family of ['HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.ROGAlly','HandheldCompanion.Devices.GPD.GPDWin5','HandheldCompanion.Devices.OneXPlayer.OneXPlayerMini']){
  for(const action of ['curve','preset'] as const){
   for(const boundary of ['none','off-release-failed','new-curve','new-sleep']){
    const label=family+'/'+action+'/'+boundary;
    const a=new ColdAdapter(family);const lifecycle=await make(a);
    const written=deferred<void>();const returned=deferred<void>();const submitted:FanNode[][]=[];
    const originalEnable=a.enable.bind(a);
    a.enable=async(nodes,id)=>{submitted.push(nodes.map(n=>({...n})));const reply=await originalEnable(nodes,id);if(submitted.length===1){written.resolve();await returned.promise;}return reply;};
    const oldRequest=(action==='curve'?lifecycle.apply(curve):lifecycle.applyPreset('aggressive',curve)).then(()=>({ok:true,error:''}),e=>({ok:false,error:String(e)}));
    await written.promise;let next:Promise<{ok:boolean;error:string}>|null=null;
    if(boundary==='off-release-failed'){
     const releaseCode=family.includes('ROG')?'HTTP_REQUEST_TIMEOUT':family.includes('GPD')?'HTTP_REQUEST_FAILED':'OEM_RESTORE_FAILED';
     a.releaseControl=async()=>{throw new FanApiError(releaseCode,releaseCode==='OEM_RESTORE_FAILED'?409:0,'fixture release result unconfirmed');};
     next=lifecycle.disable().then(()=>({ok:true,error:''}),e=>({ok:false,error:String(e)}));
     check(label+' OFF synchronously retires local control intent',!lifecycle.hasControlIntent);
    }else if(boundary==='new-curve'){
     next=lifecycle.apply(newer).then(()=>({ok:true,error:''}),e=>({ok:false,error:String(e)}));
    }else if(boundary==='new-sleep'){
     lifecycle.setPowerGeneration(3);lifecycle.observePowerBoundary('suspending',3);
    }
    returned.resolve();const old=await oldRequest;const following=next?await next:null;
    check(label+' old result reports its actual current ownership',boundary==='none'?old.ok:!old.ok&&old.error.includes('FAN_CONTROL_INTENT_SUPERSEDED'));
    if(boundary==='off-release-failed'){
     check(label+' unconfirmed release remains honest',following?.ok===false&&/OEM_RESTORE_FAILED|HTTP_REQUEST_TIMEOUT|HTTP_REQUEST_FAILED/.test(following.error));
     check(label+' late reply cannot recreate cancelled local intent',!lifecycle.hasControlIntent);
    }else if(boundary==='new-curve'){
     check(label+' newer curve succeeds through normal queue',following?.ok===true&&JSON.stringify(submitted.at(-1))===JSON.stringify(newer));
     check(label+' newest local intent is retained',lifecycle.hasControlIntent);
    }else if(boundary==='new-sleep'){
     check(label+' late reply cannot replace suspended phase',lifecycle.state==='suspended');
    }else{
     check(label+' current reply retains user control intent',lifecycle.hasControlIntent);
    }
    check(label+' no duplicated hardware write',submitted.length===(boundary==='new-curve'?2:1));
    a.releaseControl=async()=>{a.writes=false;a.lease=null;return {...a.snapshot(),oemRestoreConfirmed:true};};
    await lifecycle.close().catch(()=>{});
   }
  }
 }
 console.log(JSON.stringify({ok:!failures.length,checks,failures,scope:'production FanHostLifecycle; delayed adapter reply boundary; four family metadata fixtures; no actual hardware/native power/process proof'}));
 if(failures.length)process.exitCode=1;
}
void run();
