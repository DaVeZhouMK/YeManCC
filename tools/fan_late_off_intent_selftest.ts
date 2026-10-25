import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { PowerLifecycleState } from '../src/bridge/api';
const A=[{tempC:0,dutyPercent:50},{tempC:40,dutyPercent:50},{tempC:70,dutyPercent:60},{tempC:100,dutyPercent:90}];
const B=A.map(n=>({...n,dutyPercent:100}));
function deferred(){let release!:()=>void,entered!:()=>void;const wait=new Promise<void>(r=>release=r),seen=new Promise<void>(r=>entered=r);return {wait,seen,release,entered};}
class Host extends NoIoHost {
 applied:Array<Array<{tempC:number;dutyPercent:number}>>=[];
 hold:{kind:string;slot:ReturnType<typeof deferred>}|null=null;
 async pause(kind:string){if(this.hold?.kind===kind){const s=this.hold.slot;this.hold=null;s.entered();await s.wait;}}
 override async restoreOem(){await this.pause('restore');return super.restoreOem();}
 override async close(){await this.pause('close');return super.close();}
 override async disable(){await this.pause('disable');return super.disable();}
 override async heartbeat(){await this.pause('heartbeat');return super.heartbeat();}
 override async enable(nodes?:readonly {tempC:number;dutyPercent:number}[]){this.applied.push(Array.from(nodes??[],n=>({...n})));return super.enable();}
}
async function setup(active=true){
 const h=new Host();let starts=0,stops=0;const native:PowerLifecycleState={generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
 const life=new FanHostLifecycle({enabled:true,adapter:h,launcher:{async start(){starts++;if(starts>1){h.stateName='AwaitingControl';h.opened=h.events=h.closed=false;h.lease=null;}return {pid:10600+starts,executable:'no-io-fan1060.exe'};},async stop(){stops++;}},heartbeatIntervalMs:0,readNativeActivity:async()=>null,readNativePowerState:async()=>({...native})});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});if(active)await life.apply(A);
 return {h,life,native,counts:()=>({starts,stops})};
}
const results:Array<{name:string;ok:boolean;error?:string}>=[];
async function check(name:string,work:()=>Promise<void>){try{await work();results.push({name,ok:true});console.log('PASS '+name);}catch(error){results.push({name,ok:false,error:String(error)});console.log('FAIL '+name+': '+error);}}
async function entered(slot:ReturnType<typeof deferred>){let timer:ReturnType<typeof setTimeout>;try{await Promise.race([slot.seen,new Promise<never>((_,reject)=>{timer=setTimeout(()=>reject(Error('held step never entered')),700);})]);}finally{clearTimeout(timer!);}}
function pendingB(life:FanHostLifecycle){assert.equal(life.hasControlIntent,true,'older OFF/Close must not erase later staged ON');assert.deepEqual((life as unknown as {desiredCurve:unknown}).desiredCurve,B,'latest curve content must survive exactly');}
async function main(){
 for(const kind of ['restore','close','disable'])await check('new explicit ON intent survives late '+kind+' completion',async()=>{
  const f=await setup(kind!=='disable'),slot=deferred();
  if(kind==='disable')f.life.observePowerBoundary('suspending',6);
  f.h.hold={kind,slot};const old=(kind==='close'?f.life.close():f.life.disable()).then(()=>null,e=>e);
  try{await entered(slot);f.life.stagePendingControlCurve(B);assert.equal(f.life.hasControlIntent,true,'new ON staged before cleanup returns');slot.release();const result=await old;if(result)throw result;pendingB(f.life);}
  finally{slot.release();await old;await f.life.close().catch(()=>{});}
 });
 for(const action of ['disable','close'] as const)await check('queued '+action+' cannot erase an ON registered before its body enters',async()=>{
  const f=await setup(),slot=deferred();f.h.hold={kind:'heartbeat',slot};const maintenance=f.life.heartbeat().catch(()=>null);await entered(slot);const old=f.life[action]().then(()=>null,e=>e);f.life.stagePendingControlCurve(B);
  try{slot.release();await maintenance;const result=await old;if(result)throw result;pendingB(f.life);assert.equal(f.h.calls.filter(c=>c==='enable').length,1,'staging cannot write');}
  finally{slot.release();await maintenance;await old;await f.life.close().catch(()=>{});}
 });
 for(const action of ['disable','close'] as const)await check('plain '+action+' without a later ON remains terminal',async()=>{
  const f=await setup();await f.life[action]();assert.equal(f.life.hasControlIntent,false);assert.equal(f.life.currentLease,null);await f.life.close().catch(()=>{});
 });
 await check('public Close cancels intent synchronously while hardware exit is still unconfirmed',async()=>{
  const f=await setup(),slot=deferred();f.h.hold={kind:'close',slot};const old=f.life.close().then(()=>null,e=>e);
  try{assert.equal(f.life.hasControlIntent,false,'Close request cancels intention before any await');await entered(slot);assert(f.h.lease,'cancelling intent must not fake hardware handoff');assert.equal(f.counts().stops,0,'exit is not confirmed early');slot.release();const result=await old;if(result)throw result;assert.equal(f.counts().stops,1);}
  finally{slot.release();await old;await f.life.close().catch(()=>{});}
 });
 for(const branch of ['terminal','unbound'])await check('manual '+branch+' session replacement preserves its already staged ON',async()=>{
  const f=await setup(branch==='terminal');
  if(branch==='terminal')f.h.stateName='FaultLocked';else{f.h.stateName='Open';f.h.opened=true;f.h.events=false;}
  f.life.stagePendingControlCurve(B);
  try{const gate=await f.life.start({manualRecovery:true,reason:'new-manual-on'});assert.equal(gate.allowed,true);pendingB(f.life);assert.equal(f.counts().stops,1);assert.equal(f.counts().starts,2);await f.life.apply(B);assert.equal(f.life.state,'ready');assert(f.life.currentLease);}
  finally{await f.life.close().catch(()=>{});}
 });
 await check('a latest OFF still cancels an ON staged during an older Close',async()=>{
  const f=await setup(),slot=deferred();f.h.hold={kind:'close',slot};const old=f.life.close().then(()=>null,e=>e);await entered(slot);f.life.stagePendingControlCurve(B);const latest=f.life.disable().then(()=>null,e=>e);
  try{assert.equal(f.life.hasControlIntent,false);slot.release();await old;await latest;assert.equal(f.life.hasControlIntent,false);assert.equal(f.h.calls.filter(c=>c==='close').length,1);assert.equal(f.counts().starts,1);}
  finally{slot.release();await old;await latest;await f.life.close().catch(()=>{});}
 });
 for(const action of ['disable','close'] as const)for(const newOn of [true,false])await check(action+' then '+(newOn?'new ON':'no ON')+' retains the correct intent through a later reported sleep/wake',async()=>{
  const f=await setup(),slot=deferred();f.h.hold={kind:action==='close'?'close':'restore',slot};const old=f.life[action]().then(()=>null,e=>e);await entered(slot);if(newOn)f.life.stagePendingControlCurve(B);
  try{slot.release();const result=await old;if(result)throw result;await f.life.start({manualRecovery:true});
   f.native.generation=7;f.native.phase='suspending';f.native.resumeReady=false;f.native.hardwareWritesAllowed=false;f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);await f.life.suspend();
   // The inert native/Host boundary reports a completed current-generation HC
   // reopen with NO active curve/lease. Only the real bridge may replay intent.
   f.h.generation=7;f.h.attempt=1;f.h.stateName='AwaitingControl';f.h.powerState='On';f.h.opened=f.h.events=true;f.h.closed=false;f.h.lease=null;
   f.native.phase='ready';f.native.resumeReady=true;f.native.hardwareWritesAllowed=true;f.life.observePowerBoundary('resume-ready',7);const before=f.h.applied.length;await f.life.resume();
   assert.equal(f.h.applied.length,before+(newOn?1:0),'a staged first ON must replay once, while explicit OFF without another ON stays off');if(newOn){assert.deepEqual(f.h.applied.at(-1),B);assert.equal(f.life.state,'ready');}else assert.equal(f.life.hasControlIntent,false);
  }finally{slot.release();await old;await f.life.close().catch(()=>{});}
 });
 const report={ok:results.every(x=>x.ok),checks:results.length,results,scope:'real FanHostLifecycle queue and cleanup; inert Host adapter/launcher/native facts; no device/CPU comparison'};console.log(JSON.stringify(report));process.exitCode=report.ok?0:1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
