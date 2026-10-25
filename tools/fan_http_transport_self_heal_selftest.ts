/** Real HTTP response decoder and public lifecycle; virtual time, in-memory Host/native only. */
import assert from 'node:assert/strict';
import {http,type HttpResponse} from '../src/bridge/api';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {HttpFanApiAdapter,FanApiError,type FanNode,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:100}));
class Clock {
 now=0;seq=0;timers=new Map<number,{due:number,period:number,fn:()=>void}>();
 add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return{id};}
 clear(h:any){this.timers.delete(h?.id);}
 async flush(){for(let i=0;i<160;i++)await Promise.resolve();}
 async advance(ms:number){const target=this.now+ms;for(let i=0;i<1000;i++){const next=[...this.timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=next[1].due;if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=target;await this.flush();}
}
class Host extends NoIoHost {
 controlled=false;nodes:FanNode[]|null=null;
 override snapshot():FanState{return{...super.snapshot(),hardwareWritesEnabled:this.controlled&&!!this.lease,activeCurve:this.nodes,
  lastControlWriteEvidence:this.controlled?'compat-route-callback-returned-no-scope':'not-written',oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'hc-callback-only-physical-unknown'};}
 override async enable(nodes:readonly FanNode[]=[]){this.calls.push('enable');this.controlled=true;this.nodes=nodes.map(n=>({...n}));this.stateName='Ready';return this.snapshot();}
 override async disable(){this.controlled=false;return super.disable();}
}
type Fault={reply:HttpResponse|Error;remaining:number;endpoint:string};
async function fixture(model:string){
 const clock=new Clock(),originalRequest=http.request,saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(fn:()=>void,ms:number)=>clock.add(fn,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(fn:()=>void,ms:number)=>clock.add(fn,ms,ms),clearInterval:(h:any)=>clock.clear(h)});Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const host=new Host(),paths:string[]=[];const types:Record<string,string>={MSI:'HandheldCompanion.Devices.ClawA2VM',ROG:'HandheldCompanion.Devices.ROGAlly',GPD:'HandheldCompanion.Devices.GPDWin5',OneX:'HandheldCompanion.Devices.OneXPlayerX1Mini',Lenovo:'HandheldCompanion.Devices.Lenovo.LegionGo'};host.factoryType=types[model];let fault:Fault|null=null;
 http.request=async(url,options)=>{
  const endpoint=new URL(url).pathname;paths.push(endpoint);
  const injected=fault&&endpoint===fault.endpoint&&fault.remaining-->0?fault.reply:null;
  const input=options?.body?JSON.parse(options.body):{};let reply:unknown;
  try{switch(endpoint){
   case'/api/handshake':reply={...await host.handshake(),fanRoute:model==='GPD'||model==='OneX'?'GenericDuty':'ProfileCurve'};break;
   case'/api/state':reply={ok:true,state:await host.getState()};break;
   case'/api/open':reply={ok:true,state:await host.open()};break;
   case'/api/open-events':reply={ok:true,state:await host.openEvents()};break;
   case'/api/acquire-control':{const lease=await host.acquireControl();lease.leaseId='inert-state-lease-'+lease.generation;host.lease=lease;reply={ok:true,lease};break;}
   case'/api/heartbeat':reply={ok:true,lease:await host.heartbeat()};break;
   case'/api/enable':reply={ok:true,state:await host.enable(input.nodes)};break;
   case'/api/disable':reply={ok:true,state:await host.disable()};break;
   case'/api/restore':reply={ok:true,state:await host.restoreOem()};break;
   case'/api/release-control':reply={ok:true,state:await host.releaseControl()};break;
   case'/api/close':reply={ok:true,state:await host.close()};break;
   case'/api/shutdown':reply={ok:true};break;
   default:throw Error('unexpected inert endpoint '+endpoint);
  }}catch(error){if(error instanceof FanApiError)return{status:error.status,headers:'',body:JSON.stringify({ok:false,error:{code:error.errorCode,message:error.message}})};throw error;}
  if(injected instanceof Error)throw injected;return injected??{status:200,headers:'Content-Type: application/json',body:JSON.stringify(reply)};
 };
 const life=new FanHostLifecycle({enabled:true,adapter:new HttpFanApiAdapter('http://127.0.0.1:11977','inert-state-feedback-session'),heartbeatIntervalMs:5000,readNativeActivity:async()=>null,
  readNativePowerState:async()=>({generation:0,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),launcher:{async start(){return{pid:127700,executable:'inert.exe'};},async stop(){}}});
 await life.start({manualRecovery:true});
 return{host,life,clock,paths,fault(reply:HttpResponse|Error,remaining=1,endpoint='/api/state'){fault={reply,remaining,endpoint};},async cleanup(){fault=null;await life.close().catch(()=>{});http.request=originalRequest;Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
const cases:any[]=[];
async function check(name:string,model:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>){const f=await fixture(model);try{cases.push({name,model,ok:true,evidence:await work(f)});}catch(error){cases.push({name,model,ok:false,error:String(error),paths:f.paths,calls:f.host.calls});}finally{await f.cleanup();}}

async function main(){
 const errors=[
  'HTTP request failed (WinHTTP 12029, phase=send)',
  'HTTP request failed (WinHTTP 12030, phase=send)',
  'HTTP request failed (WinHTTP 12030, phase=headers)',
  'HTTP request failed (WinHTTP 12030, phase=body)',
  'HTTP request failed (WinHTTP 12031, phase=headers)',
  'HTTP request failed (WinHTTP 12152, phase=headers)',
  'IPC command timed out: http.request','HTTP request deadline exceeded (phase=body)','WinHTTP 12002',
 ];
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo']){
  for(const message of errors)await check('transport state failure reconciles: '+message,model,async f=>{
   await f.life.apply(curve);const original=new Error(message);f.fault(original);
   await assert.rejects(f.life.getState({discardSuperseded:true}),error=>error===original);
   assert(f.life.recoveryActive,'current status connection failure lost the existing curve recovery');
   await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.equal(f.host.calls.filter(x=>x==='close').length,0);
   return{originalErrorPreserved:true,curveWrites:1,physicalOwnership:'unknown'};
  });
  await check('persistent connection resets have one finite episode and manual control survives',model,async f=>{
   await f.life.apply(curve);f.fault(new Error(errors[3]),99);await assert.rejects(f.life.getState());assert(f.life.recoveryActive);const begin=f.paths.filter(x=>x==='/api/state').length;
   await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert.equal(f.paths.filter(x=>x==='/api/state').length-begin,10);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
   await assert.rejects(f.life.getState());await f.clock.advance(35000);assert.equal(f.paths.filter(x=>x==='/api/state').length,begin+11);
   f.fault(new Error(errors[3]),0);await f.life.apply(curve);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{probes:10,manualReachable:true};
  });
  await check('connection failure without an acknowledged curve never starts control',model,async f=>{f.fault(new Error(errors[0]));await assert.rejects(f.life.getState());assert(!f.life.recoveryActive);await f.clock.advance(35000);assert.equal(f.host.calls.filter(x=>x==='enable').length,0);});
  await check('ordinary programming failure is not reclassified as transport',model,async f=>{await f.life.apply(curve);const original=new TypeError('inert unexpected property');f.fault(original);await assert.rejects(f.life.getState(),error=>error===original);assert(!f.life.recoveryActive);await f.clock.advance(1000);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);});
  for(const status of[401,403])await check('explicit status '+status+' keeps priority over a transport-looking message',model,async f=>{
   await f.life.apply(curve);f.fault({status,headers:'',body:JSON.stringify({ok:false,error:{code:'AUTH_REJECTED',message:errors[3]}})});
   await assert.rejects(f.life.getState(),error=>error instanceof FanApiError&&error.status===status&&error.errorCode==='AUTH_REJECTED');assert(!f.life.recoveryActive);const before=f.paths.length;await f.clock.advance(35000);assert.equal(f.paths.length,before);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
  });
  for(const message of errors.slice(1,4))await check('committed POST with '+message+' stays unknown and never repeats',model,async f=>{
   f.fault(new Error(message),1,'/api/enable');await assert.rejects(f.life.apply(curve),error=>error instanceof FanApiError&&error.status===0&&error.errorCode==='FAN_HTTP_RESULT_UNKNOWN'&&error.requestPath==='/api/enable');
   assert(f.life.recoveryActive);await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.paths.filter(x=>x==='/api/enable').length,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);return{committed:true,replayed:false};
  });
 }
 console.log(JSON.stringify({ok:cases.every(x=>x.ok),checks:cases.length,cases,scope:'real public lifecycle + real HTTP adapter; native-format Error at fake HTTP boundary, virtual time/inert Host and native/process, no IPC/network/HC/device or CPU benchmark'}));if(cases.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
