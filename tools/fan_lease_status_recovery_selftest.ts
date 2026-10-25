/** Real bridge + virtual interval scheduling; inert Host models the existing
 * 15s monotonic lease, same-token heartbeat renewal and expiry handoff only.
 */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState,type FanLease} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve:FanNode[]=[{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:35},{tempC:70,dutyPercent:65},{tempC:100,dutyPercent:100}];
class Clock{
 now=0;sequence=0;intervalsCreated=0;intervalsCleared=0;maxIntervals=0;
 timers=new Map<number,{id:number,due:number,period:number,fn:()=>void}>();
 once(fn:()=>void,ms:number){return this.add(fn,ms,0);}
 add(fn:()=>void,ms:number,period:number){const id=++this.sequence;this.timers.set(id,{id,due:this.now+ms,period,fn});if(period){this.intervalsCreated++;this.maxIntervals=Math.max(this.maxIntervals,this.intervals());}return{id};}
 remove(h:any){if(!h)return;const row=this.timers.get(h.id);if(row?.period)this.intervalsCleared++;this.timers.delete(h.id);}
 intervals(){return [...this.timers.values()].filter(t=>t.period>0).length;}
 async flush(){for(let i=0;i<100;i++)await Promise.resolve();}
 async advance(ms:number){const target=this.now+ms;for(let step=0;step<1000;step++){const next=[...this.timers.values()].filter(t=>t.due<=target).sort((a,b)=>a.due-b.due||a.id-b.id)[0];if(!next)break;this.now=next.due;if(next.period)next.due+=next.period;else this.timers.delete(next.id);next.fn();await this.flush();}this.now=target;await this.flush();}
}
class LeaseHost extends NoIoHost{
 writes=false;observed=false;restored=false;expirations=0;heartbeatAt:number[]=[];enabledAt:number[]=[];expiry:any=null;failHeartbeatOnce=false;holdHeartbeat=false;heartbeatEntered=false;releaseHeartbeat:(()=>void)|null=null;
 constructor(readonly clock:Clock,family:string){super();this.factoryType='HandheldCompanion.Devices.'+family;this.generation=3;this.attempt=1;this.powerState='On';this.completeResume=true;}
 override snapshot():FanState{return{...super.snapshot(),hardwareWrites:this.writes,hardwareWritesEnabled:this.writes,hardwareWritesObserved:this.observed,oemRestoreConfirmed:this.restored,lastControlWriteEvidence:'compat-route-callback-returned-no-scope'};}
 cancelExpiry(){this.clock.remove(this.expiry);this.expiry=null;}
 arm(){this.cancelExpiry();const id=this.lease!.leaseId,generation=this.lease!.generation;this.expiry=this.clock.once(()=>{if(this.lease?.leaseId!==id||this.lease.generation!==generation)return;this.calls.push('auto-restore');this.expirations++;this.lease=null;this.writes=false;this.restored=true;this.stateName='AwaitingControl';},15000);}
 override async acquireControl(){this.calls.push('acquire');if(this.lease)throw new FanApiError('LEASE_HELD',409,'LEASE_HELD');this.lease={leaseId:'lease-'+ ++this.sequence,generation:this.sequence,expiresAtMonoMs:this.clock.now+15000};this.arm();return{...this.lease};}
 override async heartbeat(id?:string){this.calls.push('heartbeat');this.heartbeatAt.push(this.clock.now);if(this.holdHeartbeat){this.holdHeartbeat=false;this.heartbeatEntered=true;await new Promise<void>(r=>this.releaseHeartbeat=r);}if(this.failHeartbeatOnce){this.failHeartbeatOnce=false;this.cancelExpiry();this.lease=null;this.writes=false;this.stateName='AwaitingControl';throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');}if(!this.lease||this.lease.leaseId!==id||this.clock.now>=this.lease.expiresAtMonoMs!)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');this.lease={...this.lease!,expiresAtMonoMs:this.clock.now+15000};this.arm();return{...this.lease};}
 override async enable(nodes?:readonly FanNode[],id?:string){this.calls.push('enable');if(!this.lease||this.lease.leaseId!==id||this.clock.now>=this.lease.expiresAtMonoMs!)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');this.writes=true;this.observed=true;this.restored=false;this.stateName='Ready';this.enabledAt.push(this.clock.now);return this.snapshot();}
 override async applyPreset(_name?:string,id?:string,nodes?:readonly FanNode[]){return this.enable(nodes,id);}
 override async restoreOem(){this.calls.push('restore');this.writes=false;this.restored=true;return this.snapshot();}
 override async releaseControl(id?:string){this.calls.push('release');if(this.lease&&this.lease.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');this.cancelExpiry();this.lease=null;this.writes=false;this.restored=true;this.stateName='AwaitingControl';return this.snapshot();}
 override async suspend(){this.cancelExpiry();this.lease=null;this.writes=false;return super.suspend();}
 override async close(){this.cancelExpiry();this.writes=false;this.restored=true;return super.close();}
}
import { readFileSync } from 'node:fs';
const domain=JSON.parse(readFileSync('tools/fixtures/fan_registered_route_domain_20261017.json','utf8')).models;
const matrixNames=[...readFileSync('FanLab/evidence/HC-DEVICE-MATRIX-BATCH03-20260817.md','utf8').matchAll(/^\| `([^`]+)` .*\| `SourceMappedFan` \|$/gm)].map(m=>m[1]).sort();
assert.deepEqual(domain.map((m:any)=>m.name).sort(),matrixNames,'frozen route fixture must cover the full maintained mapped domain');
const cases:{model:string;scenario:string;ok:boolean;error?:string;evidence?:unknown}[]=[];
class SleepHost extends LeaseHost {
 nodes:FanNode[]|null=null;
 constructor(clock:Clock,readonly model:any){super(clock,model.name);this.factoryType=model.factory;}
 override snapshot():FanState{return{...super.snapshot(),fanCapabilitySupported:this.model.supported,fanControlCapabilityDeclared:this.model.effectiveFanControl,activeCurve:this.nodes?.map(n=>({...n}))??null,lastControlWriteEvidence:this.model.evidence==='AsusScopeReceipts'?'transported':'compat-route-callback-returned-no-scope'};}
 override async handshake(){return{...await super.handshake(),supported:this.model.supported,fanRoute:this.model.kind??'unsupported',fanRouteWriteReady:this.model.supported};}
 override async enable(nodes?:readonly FanNode[],id?:string){await super.enable(nodes,id);this.nodes=nodes?.map(n=>({...n}))??null;return this.snapshot();}
 override async suspend(){this.nodes=null;return super.suspend();}
}
async function make(model:any,controlled=true){
 const clock=new Clock(),h=new SleepHost(clock,model);let gen=3,phase='ready',starts=0,stops=0;
 const interval=globalThis.setInterval,clearInterval0=globalThis.clearInterval,timeout=globalThis.setTimeout,clearTimeout0=globalThis.clearTimeout;
 const perf=Object.getOwnPropertyDescriptor(globalThis,'performance');
 (globalThis as any).setInterval=(fn:()=>void,ms:number)=>clock.add(fn,ms,ms);(globalThis as any).clearInterval=(v:any)=>clock.remove(v);
 (globalThis as any).setTimeout=(fn:()=>void,ms=0)=>clock.once(fn,Math.max(0,ms));(globalThis as any).clearTimeout=(v:any)=>clock.remove(v);
 Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>clock.now}});
 const life=new FanHostLifecycle({enabled:true,adapter:h,heartbeatIntervalMs:5000,resumeWaitDeadlineMs:800,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:gen,phase:phase as any,resumeReady:phase==='ready',inputReady:phase==='ready',hardwareWritesAllowed:phase==='ready',hibernateAvailable:true}),launcher:{start:async()=>{starts++;return{pid:1069,executable:'inert-fan.exe'};},stop:async()=>{stops++;}}});
 async function settle<T>(task:Promise<T>,budget=20000):Promise<T>{let done=false,value:T|undefined,error:unknown;task.then(v=>{done=true;value=v;},e=>{done=true;error=e;});const until=clock.now+budget;await clock.flush();while(!done&&clock.now<until)await clock.advance(Math.min(50,until-clock.now));if(!done)throw Error('virtual operation exceeded bounded observation '+JSON.stringify({state:life.state,calls:h.calls.slice(-12),gen,phase}));if(error)throw error;return value!;}
 async function sleep(next=4){gen=next;phase='suspending';life.setPowerGeneration(gen);life.observePowerBoundary('suspending',gen);await h.suspend();await settle(life.suspend());phase='suspended';}
 async function wake(){phase='ready';await h.resume({generation:gen});life.observePowerBoundary('resuming',gen);life.observePowerBoundary('resume-ready',gen);await settle(life.resume());}
 function native(next:number,value:string){gen=next;phase=value;}
 async function done(){try{await settle(life.close());}finally{h.cancelExpiry();globalThis.setInterval=interval;globalThis.clearInterval=clearInterval0;globalThis.setTimeout=timeout;globalThis.clearTimeout=clearTimeout0;if(perf)Object.defineProperty(globalThis,'performance',perf);}}
 try{life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);if(!controlled){h.generation=0;h.attempt=0;h.powerState='Unknown';}if(model.supported){await settle(life.start());if(controlled)await settle(life.apply(curve));}else{await assert.rejects(()=>settle(life.start()),(error:any)=>error instanceof FanApiError && error.errorCode==='FAN_DEVICE_GATE_DENIED');}}
 catch(error){await done();throw error;}
 return{clock,h,life,settle,sleep,wake,native,done,counts:()=>({starts,stops})};
}
async function check(model:any,scenario:string,work:(f:Awaited<ReturnType<typeof make>>)=>Promise<unknown>,controlled=true){let f:Awaited<ReturnType<typeof make>>|undefined;try{f=await make(model,controlled);const evidence=await work(f);cases.push({model:model.name,scenario,ok:true,evidence});}catch(error){cases.push({model:model.name,scenario,ok:false,error:String(error)});}finally{if(f)await f.done();}}

const adjusted=curve.map(n=>({...n,dutyPercent:Math.min(100,n.dutyPercent+7)}));
for(const model of domain.filter((m:any)=>m.supported)){
 for(const mode of ['wake-resume','manual-curve','manual-preset'])await check(model,mode+': known lease rejection plus failed status read self-recovers in one request',async f=>{
  if(mode==='wake-resume')await f.sleep();
  const enable=f.h.enable.bind(f.h),getState=f.h.getState.bind(f.h);let rejected=false,blockedReads=0,armed=false;
  f.h.enable=async(nodes,id)=>{if(!rejected){rejected=true;f.h.cancelExpiry();f.h.lease=null;f.h.writes=false;armed=true;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');}return enable(nodes,id);};
  f.h.getState=async()=>{if(armed){armed=false;blockedReads++;throw new Error('inert state read failed immediately after known rejection');}return getState();};
  const acquireBefore=f.h.calls.filter(x=>x==='acquire').length;
  if(mode==='wake-resume')await f.wake();else if(mode==='manual-preset')await f.settle(f.life.applyPreset('aggressive',adjusted));else await f.settle(f.life.apply(adjusted));
  assert(rejected);assert.equal(blockedReads,1);assert(f.h.writes);assert.equal(f.h.calls.filter(x=>x==='acquire').length-acquireBefore,mode==='wake-resume'?2:1);
  await f.clock.advance(11000);assert.equal(f.h.expirations,0);assert.equal(f.clock.intervals(),1);assert.equal(f.counts().starts,1);
  return {knownRejectionRetried:true,statusReadFailures:blockedReads,oneUserRequest:true,oneTimer:true,oneHost:true};
 });
}
const report={ok:cases.every(c=>c.ok),models:domain.length,checks:cases.length,cases,hardwareCalls:0,HCAssemblyLoaded:false,scope:'actual bridge; inert Host with known rejected write and immediate failed status read; no native IO, OS sleep or CPU benchmark'};console.log(JSON.stringify(report));if(!report.ok)process.exitCode=1;
