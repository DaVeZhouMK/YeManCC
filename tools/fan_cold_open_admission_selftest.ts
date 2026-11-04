/** FAN956: real public bridge + cold Stopped Host receipt after S4/new process.
 * Adapter/native/process fixtures only. No device, real Host, filesystem or hardware writes. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, HttpFanApiAdapter, type FanApiAdapter, type FanState, type FanLease, type FanNode, type FanHandshake } from '../src/bridge/fanApi';
import { http, type PowerLifecycleState } from '../src/bridge/api';
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
 async resume():Promise<FanState>{this.calls.push('resume');throw new FanApiError('cold Host has no earlier resume receipt',409,'HC_RESUME_RESULT_EXPIRED');}
 async close():Promise<FanState>{this.calls.push('close');this.opened=false;this.events=false;this.closed=true;this.writes=false;this.lease=null;
  return {...this.snapshot(),hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true,oemRestoreConfirmed:true};}
 async shutdown():Promise<void>{this.calls.push('shutdown');}
}
let checks=0;const failures:string[]=[];const check=(name:string,ok:boolean)=>{checks++;if(!ok)failures.push(name);console.log((ok?'PASS ':'FAIL ')+name);};
async function make(adapter:FanApiAdapter,g=2,initializeReady=true){let current=native(g);let starts=0,stops=0;const lifecycle=new FanHostLifecycle({enabled:true,adapter,
 launcher:{async start(){starts++;return {pid:30000+starts,executable:'fixture.exe'};},async stop(){stops++;}},
 readNativeActivity:async()=>null,readNativePowerState:async()=>({...current}),heartbeatIntervalMs:60000,resumeWaitDeadlineMs:500,
 requestManualWake:async generation=>({ok:current.phase==='ready',generation,phase:current.phase})});
 if(initializeReady){lifecycle.setPowerGeneration(g);lifecycle.observePowerBoundary('resume-ready',g);}
 return {lifecycle,setNative:(v:PowerLifecycleState)=>{current=v;},starts:()=>starts,stops:()=>stops};}
async function run(){
 for(const family of ['HandheldCompanion.Devices.MSI.ClawCG3EM','HandheldCompanion.Devices.MSI.ClawA2VM','HandheldCompanion.Devices.ROGAlly','HandheldCompanion.Devices.GPD.GPDWin5']){
  const a=new ColdAdapter(family),fx=await make(a);let error='';
  try{const first=await fx.lifecycle.start({manualRecovery:true,reason:'after-s4-first-click'});
   check(family+' first start independently returns an allowed gate',first.allowed);
   if(!first.allowed)throw new Error('first manual start was not admitted');
   await fx.lifecycle.apply(curve);
   const written=a.calls.slice(),again=await fx.lifecycle.start({manualRecovery:true,reason:'already-controlled-idempotent-start'});
   check(family+' start after the first write is idempotent',again.allowed&&a.calls.filter(x=>['open','open-events','acquire','enable','resume','close'].includes(x)).join(',')===written.filter(x=>['open','open-events','acquire','enable','resume','close'].includes(x)).join(','));
  }catch(e){error=String(e);}
  check(family+' first click crosses cold Stopped to actual Open/Events/lease/enable',!error&&a.calls.filter(x=>x==='enable').length===1);
  check(family+' cold Host does not require a fictitious resume receipt',!a.calls.includes('resume'));
  check(family+' exactly one process owner',fx.starts()===1);
  if(error)console.log('ERROR '+error);
  await fx.lifecycle.close().catch(()=>{});
 }
 // One real HTTP adapter consumes inert JSON envelopes after the exact idle
 // power history. No resume-ready is seeded before these public event entries.
 {
  const family='HandheldCompanion.Devices.XboxROGAllyX',a=new ColdAdapter(family),request=http.request,paths:string[]=[];let error='';
  http.request=async(url,options={})=>{const path=new URL(url).pathname,body=options.body?JSON.parse(options.body):{};paths.push(path);let value:unknown;
   try{
    if(path==='/api/handshake')value={...await a.handshake(),protocolVersion:2,hostMode:'real-hc-authorized',hardwareCapable:true,hardwareWritesEnabled:false,hardwareWritesObserved:false};
    else if(path==='/api/state')value={ok:true,state:await a.getState()};
    else if(path==='/api/open')value={ok:true,state:await a.open()};
    else if(path==='/api/open-events')value={ok:true,state:await a.openEvents()};
    else if(path==='/api/acquire-control')value={ok:true,lease:await a.acquireControl()};
    else if(path==='/api/enable')value={ok:true,state:await a.enable(body.nodes,body.leaseId)};
    else if(path==='/api/resume')value={ok:true,state:await a.resume()};
    else if(path==='/api/close')value={ok:true,state:await a.close()};
    else if(path==='/api/release-control')value={ok:true,state:await a.releaseControl()};
    else if(path==='/api/restore')value={ok:true,state:await a.restoreOem()};
    else if(path==='/api/shutdown'){await a.shutdown();value={ok:true};}
    else throw new Error('unexpected inert Fan HTTP route: '+path);
    // Host's null policy omits nullable optional fields but explicitly retains
    // the lease:null receipt. Keep false values and the nested State envelope.
    return {status:200,headers:'',body:JSON.stringify(value,(key,item)=>item===null&&key!=='lease'?undefined:item),bodyComplete:true};
   }catch(failure){if(!(failure instanceof FanApiError))throw failure;return {status:failure.status,headers:'',body:JSON.stringify({ok:false,error:{code:failure.errorCode,message:failure.message},state:a.snapshot()}),bodyComplete:true};}
  };
  const fx=await make(new HttpFanApiAdapter('http://127.0.0.1:8765'),37594165,false);
  try{
   for(const generation of [37594163,37594164,37594165]){
    fx.setNative(native(generation,'suspending'));fx.lifecycle.setPowerGeneration(generation);fx.lifecycle.observePowerBoundary('suspending',generation);
    fx.setNative(native(generation,'resuming'));fx.lifecycle.observePowerBoundary('resuming',generation);
    fx.setNative(native(generation));fx.lifecycle.observePowerBoundary('resume-ready',generation);await fx.lifecycle.resume();
    fx.lifecycle.observePowerBoundary('resumed',generation);await fx.lifecycle.resumeAfterCommittedPowerWake(generation);
   }
   check('XboxROGAllyX idle sleep history invokes real resume no-work without a Host or active intent',fx.starts()===0&&paths.length===0&&!fx.lifecycle.hasControlIntent&&fx.lifecycle.state==='stopped');
   const first=await fx.lifecycle.start({manualRecovery:true,reason:'20261104-idle-sleep-first-enable'});
   check('XboxROGAllyX first start after idle sleep independently returns an allowed gate',first.allowed);
   if(!first.allowed)throw new Error('ROG Xbox first start was not admitted');
   await fx.lifecycle.apply(curve);
   check('XboxROGAllyX idle sleep first action reaches one original Open Events acquire and enable',['open','open-events','acquire','enable'].every(call=>a.calls.filter(value=>value===call).length===1));
   check('XboxROGAllyX idle sleep cold Host never receives resume or Close',!a.calls.includes('resume')&&!a.calls.includes('close')&&!paths.includes('/api/resume'));
   check('XboxROGAllyX idle sleep first action retains one process without replacement',fx.starts()===1&&fx.stops()===0&&fx.lifecycle.state==='ready');
   check('XboxROGAllyX idle sleep control uses real HTTP JSON response parsing',paths.includes('/api/handshake')&&paths.includes('/api/state')&&paths.filter(path=>path==='/api/enable').length===1);
  }catch(failure){error=String(failure);check('XboxROGAllyX idle sleep first action must complete without a second start',false);}
  finally{await fx.lifecycle.close().catch(()=>{});http.request=request;}
  if(error)console.log('ERROR '+error);
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
