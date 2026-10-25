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
const modes={
 'broken-json':{status:200,headers:'',body:'{"ok":true,'},
 'missing-state':{status:200,headers:'',body:'{"ok":true}'},
 'wrong-root':{status:200,headers:'',body:'null'},
 'invalid-state':{status:200,headers:'',body:'{"ok":true,"state":[]}'},
 'invalid-write-flag':{status:200,headers:'',body:'{"ok":true,"state":{"state":"Ready","hardwareWritesEnabled":"true"}}'},
 'invalid-lease':{status:200,headers:'',body:'{"ok":true,"state":{"state":"Ready","lease":{"leaseId":""}}}'},
 'incomplete-body':{status:200,headers:'',body:'{"ok":true,"state":{"state":"Ready"}}',bodyComplete:false},
} satisfies Record<string,HttpResponse>;
type Fault={reply:HttpResponse;remaining:number;endpoint:string};
async function fixture(model:string){
 const clock=new Clock(),originalRequest=http.request,saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(fn:()=>void,ms:number)=>clock.add(fn,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(fn:()=>void,ms:number)=>clock.add(fn,ms,ms),clearInterval:(h:any)=>clock.clear(h)});Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const host=new Host(),paths:string[]=[];const types:Record<string,string>={MSI:'HandheldCompanion.Devices.ClawA2VM',ROG:'HandheldCompanion.Devices.ROGAlly',GPD:'HandheldCompanion.Devices.GPDWin5',OneX:'HandheldCompanion.Devices.OneXPlayerX1Mini',Lenovo:'HandheldCompanion.Devices.Lenovo.LegionGo'};host.factoryType=types[model];let fault:Fault|null=null;
 http.request=async(url,options)=>{
  const endpoint=new URL(url).pathname;paths.push(endpoint);
  const injected=fault&&endpoint===fault.endpoint&&fault.remaining-->0?{...fault.reply}:null;
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
  return injected??{status:200,headers:'Content-Type: application/json',body:JSON.stringify(reply)};
 };
 const life=new FanHostLifecycle({enabled:true,adapter:new HttpFanApiAdapter('http://127.0.0.1:11977','inert-state-feedback-session'),heartbeatIntervalMs:5000,readNativeActivity:async()=>null,
  readNativePowerState:async()=>({generation:0,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),launcher:{async start(){return{pid:127700,executable:'inert.exe'};},async stop(){}}});
 await life.start({manualRecovery:true});
 return{host,life,clock,paths,fault(reply:HttpResponse,remaining=1,endpoint='/api/state'){fault={reply,remaining,endpoint};},async cleanup(){fault=null;await life.close().catch(()=>{});http.request=originalRequest;Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
const cases:any[]=[];
async function check(name:string,model:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>){const f=await fixture(model);try{cases.push({name,model,ok:true,evidence:await work(f)});}catch(error){cases.push({name,model,ok:false,error:String(error),paths:f.paths,calls:f.host.calls});}finally{await f.cleanup();}}
async function main(){
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo']){
  for(const [name,reply,code] of [
   ['broken-json',{status:200,headers:'',body:'{"ok":true,'},'FAN_API_RESPONSE_INVALID'],
   ['wrong-capability-type',{status:200,headers:'',body:'{"ok":true,"supported":true,"hardwareCapable":"true"}'},'FAN_API_RESPONSE_INVALID'],
   ['incompatible-protocol',{status:200,headers:'',body:'{"ok":true,"supported":true,"protocolVersion":99}'},'FAN_PROTOCOL_INCOMPATIBLE'],
  ] as const)await check('FAN1287 invalid handshake '+name+' stops automatic work and a corrected manual ON remains reachable',model,async f=>{
   await f.life.close();f.fault(reply,99,'/api/handshake');
   const denied=assert.rejects(f.life.apply(curve),error=>error instanceof FanApiError&&error.errorCode===code&&error.requestPath==='/api/handshake');await f.clock.flush();await f.clock.advance(1000);await denied;
   assert(!f.life.recoveryActive,'bad handshake created an autonomous repair owner');assert.equal(f.host.calls.filter(x=>x==='enable'||x==='open'||x==='acquire').length,0,'bad handshake reached a device call');
   const requests=f.paths.length;await f.clock.advance(35000);assert.equal(f.paths.length,requests,'bad handshake silently relaunched or re-handshook');
   f.fault(reply,0,'/api/handshake');await f.life.apply(curve);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,1,'corrected current manual request did not write once');
   return{errorCode:code,invalidHandshakeAutomaticRequests:0,correctedManualEnable:1};
  });
  for(const[name,reply]of Object.entries(modes))await check('invalid state '+name+' reconciles without another control write',model,async f=>{
   await f.life.apply(curve);f.fault(reply);let failure:unknown;try{await f.life.getState({discardSuperseded:true});}catch(error){failure=error;}
   assert(failure instanceof FanApiError);assert.equal(failure.errorCode,'FAN_API_RESPONSE_INVALID');assert.equal(failure.requestPath,'/api/state');assert.equal(failure.status,200);
   assert(f.life.recoveryActive,'invalid current state reply did not retain bounded reconciliation');
   await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.equal(f.host.calls.filter(x=>x==='close').length,0);assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);
   return{errorCode:failure.errorCode,path:failure.requestPath,writes:1,physicalOwnership:'unknown'};
  });
  await check('persistent invalid state has one finite budget and later manual ON works',model,async f=>{
   await f.life.apply(curve);f.fault(modes['broken-json'],99);await assert.rejects(f.life.getState({discardSuperseded:true}));assert(f.life.recoveryActive);const before=f.paths.filter(x=>x==='/api/state').length;
   await f.clock.advance(35000);assert(f.life.recoveryNeedsAttention);assert(!f.life.recoveryActive);assert.equal(f.paths.filter(x=>x==='/api/state').length-before,10);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
   await assert.rejects(f.life.getState({discardSuperseded:true}));const stopped=f.paths.filter(x=>x==='/api/state').length;await f.clock.advance(35000);assert.equal(f.paths.filter(x=>x==='/api/state').length,stopped);
   f.fault(modes['broken-json'],0);await f.life.apply(curve);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{automaticProbes:10,manualReachable:true};
  });
  for(const mode of ['broken-json','incomplete-body'] as const)await check('lost enable '+mode+' remains unknown and never repeats a committed write',model,async f=>{
   f.fault(modes[mode],1,'/api/enable');await assert.rejects(f.life.apply(curve),error=>error instanceof FanApiError&&error.errorCode==='FAN_HTTP_RESULT_UNKNOWN'&&error.requestPath==='/api/enable');
   assert(f.life.recoveryActive);await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.paths.filter(x=>x==='/api/enable').length,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);return{committed:true,replayed:false};
  });
  await check('invalid reply before any acknowledged curve cannot start control',model,async f=>{f.fault(modes['missing-state']);await assert.rejects(f.life.getState());assert(!f.life.recoveryActive);await f.clock.advance(35000);assert.equal(f.host.calls.filter(x=>x==='enable').length,0);});
  for(const status of[401,403])await check('incomplete body keeps explicit auth refusal '+status,model,async f=>{
   await f.life.apply(curve);f.fault({...modes['incomplete-body'],status});await assert.rejects(f.life.getState(),error=>error instanceof FanApiError&&error.status===status);assert(!f.life.recoveryActive);const before=f.paths.length;await f.clock.advance(35000);assert.equal(f.paths.length,before);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
  });
 }
 console.log(JSON.stringify({ok:cases.every(x=>x.ok),checks:cases.length,cases,scope:'real public lifecycle + real HTTP response decoding, in-memory responses/native/process, virtual time; no IPC/network/HC/device or CPU benchmark'}));if(cases.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
