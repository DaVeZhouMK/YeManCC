/** FAN956: real public bridge + cold Stopped Host receipt after S4/new process.
 * Adapter/native/process fixtures only. No device, real Host, filesystem or hardware writes. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanState, type FanLease, type FanNode, type FanHandshake } from '../src/bridge/fanApi';
import { http, type PowerLifecycleState } from '../src/bridge/api';
import { HttpFanApiAdapter } from '../src/bridge/fanApi';
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
 retainedIntent=true; cancelFailure=false;
 async disable(_id?:string,options?:{controlIntentOnly?:boolean}):Promise<FanState>{
  if(options?.controlIntentOnly){this.calls.push('cancel-intent');if(this.cancelFailure)throw new FanApiError('HTTP_ERROR',503,'fixture cancellation failed');this.retainedIntent=false;return this.snapshot();}
  this.calls.push('physical-disable');this.writes=false;return this.snapshot();}
 async releaseControl():Promise<FanState>{this.writes=false;this.lease=null;return {...this.snapshot(),oemRestoreConfirmed:true};}
 async restoreOem():Promise<FanState>{this.writes=false;return {...this.snapshot(),oemRestoreConfirmed:true};}
 async suspend():Promise<FanState>{this.opened=false;this.events=false;return {...this.snapshot(),state:'Suspended',powerState:'Suspended'};}
 async resume():Promise<FanState>{this.calls.push('resume');throw new FanApiError('HC_RESUME_RESULT_EXPIRED',409,'cold Host has no earlier resume receipt');}
 async close():Promise<FanState>{this.calls.push('close');this.opened=false;this.events=false;this.closed=true;this.writes=false;this.lease=null;
  return {...this.snapshot(),hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true,oemRestoreConfirmed:true};}
 async shutdown():Promise<void>{this.calls.push('shutdown');}
}

let checks=0;const failures:string[]=[];const check=(name:string,ok:boolean)=>{checks++;if(!ok)failures.push(name);console.log((ok?'PASS ':'FAIL ')+name);};
async function make(adapter:ColdAdapter){let current=native(2);const lifecycle=new FanHostLifecycle({enabled:true,adapter,
 launcher:{async start(){return {pid:30001,executable:'fixture.exe'};},async stop(){}},
 readNativeActivity:async()=>null,readNativePowerState:async()=>({...current}),heartbeatIntervalMs:60000,resumeWaitDeadlineMs:500});
 lifecycle.setPowerGeneration(2);lifecycle.observePowerBoundary('resume-ready',2);
 await lifecycle.start({manualRecovery:true});await lifecycle.apply(curve);
 current=native(3,'suspended');lifecycle.setPowerGeneration(3);
 lifecycle.observePowerBoundary('suspending',3);
 adapter.opened=false;adapter.events=false;adapter.writes=false;adapter.lease=null;
 adapter.overrides={state:'Suspended',powerState:'Suspended',hardwareWritesEnabled:false,hardwareWrites:false,openCalled:false,openEventsCalled:false};
 adapter.calls=[];return lifecycle;}
async function run(){
 for(const family of ['HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.ROGAlly','HandheldCompanion.Devices.GPD.GPDWin5','HandheldCompanion.Devices.OneXPlayer.OneXPlayerMini']){
  const a=new ColdAdapter(family);const lifecycle=await make(a);
  await lifecycle.disable();
  check(family+' asleep OFF reaches Host administrative cancellation',a.calls.includes('cancel-intent')&&!a.retainedIntent);
  check(family+' asleep OFF performs no restore/writer',!a.calls.some(x=>['physical-disable','enable','open','acquire'].includes(x)));
  check(family+' local intent removed',!lifecycle.hasControlIntent);
  a.overrides={};await lifecycle.close().catch(()=>{});
 }
 const failed=new ColdAdapter('HandheldCompanion.Devices.MSI.ClawCG3EM');const failedLife=await make(failed);failed.cancelFailure=true;let rejected=false;
 try{await failedLife.disable();}catch{rejected=true;}
 check('failed remote cancellation cannot report successful OFF',rejected&&failed.retainedIntent);
 failed.cancelFailure=false;failed.overrides={};await failedLife.close().catch(()=>{});

 const race=new ColdAdapter('HandheldCompanion.Devices.MSI.ClawCG3EM');const raceLife=await make(race);
 race.opened=true;race.events=true;race.writes=true;
 race.overrides={state:'Ready',powerState:'On',hardwareWritesEnabled:true,hardwareWrites:true,openCalled:true,openEventsCalled:true};
 const previousRestore=race.restoreOem.bind(race);
 race.restoreOem=async()=>{race.calls.push('restore');return previousRestore();};
 await raceLife.disable();
 check('late Host resume after local Suspended still receives cancellation',race.calls.includes('cancel-intent'));
 check('already-live control follows real release contract',race.calls.includes('restore')&&!race.writes);
 race.overrides={};await raceLife.close().catch(()=>{});

 const originalHttp=http.request;const requests:Array<{url:string;options:any}>=[];let mode='accepted';
 http.request=async(url:string,options:any)=>{requests.push({url,options});
  if(mode==='legacy')return {status:404,headers:'',body:JSON.stringify({ok:false,code:'NOT_FOUND'})};
  return {status:200,headers:'',body:JSON.stringify({ok:true,...(mode==='accepted'?{controlIntentCancelled:true}:{}),state:{state:'Suspended',powerState:'Suspended',hardwareWrites:false}})};};
 try{
  const httpAdapter=new HttpFanApiAdapter('http://127.0.0.1:8765','fixture-session-token');
  await httpAdapter.disable(undefined,{controlIntentOnly:true});
  check('actual HTTP adapter uses separate administrative route',requests[0].url.endsWith('/api/cancel-control-intent'));
  check('administrative route retains session authentication',requests[0].options.headers['X-YeMan-Fan-Session']==='fixture-session-token');
  mode='missing';let missing=false;try{await httpAdapter.disable(undefined,{controlIntentOnly:true});}catch{missing=true;}
  check('missing explicit cancellation receipt is rejected',missing);
  mode='legacy';let old=false;const start=requests.length;try{await httpAdapter.disable(undefined,{controlIntentOnly:true});}catch{old=true;}
  check('old Host 404 has no physical-disable fallback',old&&requests.slice(start).every(x=>x.url.endsWith('/api/cancel-control-intent')));
  mode='accepted';await httpAdapter.disable('fixture-lease');
  check('ordinary disable still uses original physical release route',requests.at(-1)!.url.endsWith('/api/disable'));
 }finally{http.request=originalHttp;}

 console.log(JSON.stringify({ok:!failures.length,checks,failures,scope:'production FanHostLifecycle with adapter/native/process fixtures; no hardware'}));
 if(failures.length)process.exitCode=1;
}
void run();
