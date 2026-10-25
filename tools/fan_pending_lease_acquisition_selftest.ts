/** Public pending acquisition/control/OFF boundaries; virtual time, inert Host, zero device I/O. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError, type FanLease, type FanNode} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve = (duty:number):FanNode[] => [0,40,70,100].map(tempC=>({tempC,dutyPercent:duty}));
const outcome = <T>(p:Promise<T>) => p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
function hold<T>() { let resolve!:(v:T)=>void, entered!:()=>void;return {promise:new Promise<T>(r=>{resolve=r;}),ready:new Promise<void>(r=>{entered=r;}),resolve:(v:T)=>resolve(v),entered:()=>entered()}; }
class Clock {
 now=0;seq=0;timers=new Map<number,{due:number,period:number,fn:()=>void}>();
 add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return{id};}
 clear(h:any){this.timers.delete(h?.id);}
 async flush(){for(let i=0;i<200;i++)await Promise.resolve();}
 async advance(ms:number){const end=this.now+ms;for(let i=0;i<1000;i++){const next=[...this.timers.entries()].filter(([,x])=>x.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=next[1].due;if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=end;await this.flush();}
}
const families = [
 ['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],
 ['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],
 ['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],
 ['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],
 ['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve'],
] as const;
async function fixture(family:typeof families[number]=families[1]) {
 const clock=new Clock(),saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(fn:()=>void,ms:number)=>clock.add(fn,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(fn:()=>void,ms:number)=>clock.add(fn,ms,ms),clearInterval:(h:any)=>clock.clear(h)});
 Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>clock.now}});
 const host=new NoIoHost();host.factoryType=family[1];host.completeResume=true;
 let native:any={generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true};
 const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});
 const acquired=hold<FanLease>();let holdNext=true,issued:FanLease|null=null;
 const acquire=host.acquireControl.bind(host);host.acquireControl=async()=>{const value=await acquire();if(holdNext){holdNext=false;issued=value;acquired.entered();return acquired.promise;}return value;};
 const releases:Array<string|undefined>=[],restores:Array<string|undefined>=[];
 const writes:Array<{action:string,leaseId:string|undefined,duty:number|undefined}>=[];
 const enable=host.enable.bind(host),preset=host.applyPreset.bind(host);
 const requireLease=(id?:string)=>{if(!host.lease||host.lease.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
 host.enable=async(nodes:readonly FanNode[]=[],id?:string)=>{requireLease(id);writes.push({action:'apply',leaseId:id,duty:nodes[0]?.dutyPercent});return enable();};
 host.applyPreset=async(_name?:string,id?:string,nodes?:readonly FanNode[])=>{requireLease(id);writes.push({action:'preset',leaseId:id,duty:nodes?.[0]?.dutyPercent});return preset();};
 const release=host.releaseControl.bind(host),restore=host.restoreOem.bind(host);
 host.releaseControl=async(id?:string)=>{releases.push(id);return release();};
 host.restoreOem=async(id?:string)=>{restores.push(id);return restore();};
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:5000,readNativeActivity:async()=>null,
  readNativePowerState:async()=>native,launcher:{async start(){return{pid:117201,executable:'inert-pending-lease.exe'};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});
 const owner=life as any,adoptions:Array<FanLease|null>=[];let localLease=owner.lease as FanLease|null;
 Object.defineProperty(owner,'lease',{configurable:true,get:()=>localLease,set:(v:FanLease|null)=>{localLease=v;if(v)adoptions.push({...v});}});
 return {host,life,clock,acquired,releases,restores,writes,adoptions,observations:[] as unknown[],issued:()=>issued!,
  setNative(value:any){native=value;},
  async wake(generation=6){native={generation,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true};host.lease=null;await host.resume({generation});life.observePowerBoundary('resume-ready',generation);},
  async cleanup(){acquired.resolve(issued??{leaseId:'cleanup-only',generation:1});await clock.flush();await life.close().catch(()=>{});Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
const results:any[]=[];
async function check(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]=families[1]) {
 const f=await fixture(family);try{results.push({name,family:family[0],ok:true,evidence:await work(f)});}catch(error){results.push({name,family:family[0],ok:false,error:String(error),calls:[...f.host.calls],adoptions:f.adoptions,observations:f.observations});}finally{await f.cleanup();}
}
async function main(){
 for(const family of families)for(const action of ['apply','preset'] as const)for(const edge of ['resuming','resuming-ready'] as const)await check(`${action} pending lease after ${edge}: retire old receipt and finish the same user request`,async f=>{
  let settled=false;const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65))).then(r=>{settled=true;return r;});
  await f.acquired.ready;const old=f.issued();
  f.setNative({generation:6,phase:'resuming',hardwareWritesAllowed:false,resumeReady:false,inputReady:false,hibernateAvailable:true});f.life.observePowerBoundary('resuming',6);f.host.lease=null;
  if(edge==='resuming-ready')await f.wake();
  const index=f.host.calls.length;f.acquired.resolve(old);await f.clock.flush();
  f.observations.push({point:'old-acquire-returned',settled,adoptions:[...f.adoptions],calls:f.host.calls.slice(index),heartbeatActive:(f.life as any).heartbeatTimer!==null});
  assert(!f.adoptions.some(x=>x?.leaseId===old.leaseId),'pre-wake lease was adopted after the wake boundary');
  if(edge==='resuming') {
    assert.equal((f.life as any).heartbeatTimer,null,'retired acquisition restarted heartbeat maintenance');
    assert(!f.host.calls.slice(index).some(x=>['enable','preset','release','restore'].includes(x)),'retired acquisition submitted control or handoff');
  } else assert(f.life.currentLease?.leaseId!==old.leaseId);
  if(edge==='resuming')await f.wake();
  await f.clock.advance(5000);const reply=await pending;
  assert.equal(reply.error,null,'same manual request needs a second click after a transient wake boundary');
  assert.equal(f.host.calls.filter(x=>x==='enable').length+(action==='preset'?f.host.calls.filter(x=>x==='preset').length:0),1);
  assert(f.life.currentLease?.leaseId!==old.leaseId);assert.equal(f.writes.length,1);assert(f.writes.every(x=>x.leaseId!==old.leaseId));return{edge,oldLeaseNotAdopted:true,sameUserRequestCompleted:true,writes:f.writes};
 },family);
 for(const action of ['apply','preset'] as const)for(const end of ['off','close'] as const)await check(`${action} pending lease with pure ${end}: keep receipt for ordered cleanup without maintenance`,async f=>{
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));await f.acquired.ready;const old=f.issued();
  const terminal=outcome(end==='off'?f.life.disable():f.life.close());f.acquired.resolve(old);await f.clock.flush();const reply=await pending;const cleanup=await terminal;
  assert(reply.error);assert.equal(cleanup.error,null);assert(f.adoptions.some(x=>x?.leaseId===old.leaseId),'same-session cleanup lost the committed lease receipt');
  assert.equal((f.life as any).heartbeatTimer,null);assert(!f.host.calls.some(x=>['enable','preset'].includes(x)));assert(!f.life.hasControlIntent);
  if(end==='off'){assert(f.releases.includes(old.leaseId),'OFF did not release the exact acquired token');assert(f.restores.includes(old.leaseId),'OFF did not retain the token for OEM restore');}
  return{end,cleanupPassed:true,receiptRetainedForCleanup:true};
 });
 for(const action of ['apply','preset'] as const)await check(`${action} ordinary pending acquisition still starts control once`,async f=>{
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));await f.acquired.ready;f.acquired.resolve(f.issued());await f.clock.flush();assert.equal((await pending).error,null);assert.equal(f.life.currentLease?.leaseId,f.issued().leaseId);assert((f.life as any).heartbeatTimer!==null);return{ordinaryReceiptAccepted:true};
 });
 for(const action of ['apply','preset'] as const)for(const order of ['power-off','off-power','off-power-off','power-close','close-power'] as const)await check(`${action} pending lease mixed ${order}: cleanup cannot revive a pre-power receipt`,async f=>{
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));await f.acquired.ready;const old=f.issued();
  const endings:Promise<unknown>[]=[];
  for(const step of order.split('-')){
   if(step==='power'){f.setNative({generation:6,phase:'resuming',hardwareWritesAllowed:false,resumeReady:false,inputReady:false,hibernateAvailable:true});f.life.observePowerBoundary('resuming',6);f.host.lease=null;}
   else endings.push(outcome(step==='off'?f.life.disable():f.life.close()));
  }
  f.acquired.resolve(old);await f.clock.flush();const reply=await pending;await Promise.all(endings);
  assert(reply.error);assert(!f.adoptions.some(x=>x?.leaseId===old.leaseId),'OFF exception masked the crossed power boundary');
  assert.equal((f.life as any).heartbeatTimer,null);assert.equal(f.writes.length,0);assert(!f.life.hasControlIntent);
  assert(!f.releases.includes(old.leaseId)&&!f.restores.includes(old.leaseId),'retired old receipt was sent into the new power owner');
  return{order,oldReceiptRetired:true,manualIntentCancelled:true};
 });
 for(const action of ['apply','preset'] as const)for(const edge of ['generation','same-generation-sleep'] as const)await check(`${action} pending lease retired by ${edge}: next manual remains reachable`,async f=>{
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));await f.acquired.ready;const old=f.issued(),generation=edge==='generation'?7:6;
  f.setNative({generation,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false,inputReady:false,hibernateAvailable:true});
  if(edge==='generation')f.life.setPowerGeneration(generation);f.life.observePowerBoundary('suspending',generation);f.host.lease=null;
  f.acquired.resolve(old);await f.clock.flush();assert((await pending).error);assert(!f.adoptions.some(x=>x?.leaseId===old.leaseId));assert.equal(f.writes.length,0);
  await f.wake(generation);
  // FanView manual enable first rechecks startup/readiness, then commits its curve.
  const fresh=outcome((async()=>{assert((await f.life.start({manualRecovery:true})).allowed);return f.life.apply(curve(75));})());
  await f.clock.advance(5000);assert.equal((await fresh).error,null);assert.equal(f.writes.length,1);assert.equal(f.writes[0].duty,75);
  return{edge,nextManualCompleted:true};
 });
 for(const action of ['apply','preset'] as const)await check(`${action} pending lease with a newer curve: preserve one live receipt for the latest user intent`,async f=>{
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));await f.acquired.ready;const old=f.issued();
  const latest=outcome(f.life.apply(curve(75)));f.acquired.resolve(old);await f.clock.flush();assert((await pending).error);assert.equal((await latest).error,null);
  assert.equal(f.writes.length,1);assert.equal(f.writes[0].duty,75);assert.equal(f.writes[0].leaseId,old.leaseId);assert.equal(f.host.calls.filter(x=>x==='acquire').length,1);
  return{sameSessionReceiptReused:true,latestCurveOnly:true};
 });
 for(const action of ['apply','preset'] as const)await check(`${action} pending lease with repeated pure OFF: cleanup receipt remains usable`,async f=>{
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));await f.acquired.ready;const old=f.issued();
  const first=outcome(f.life.disable()),second=outcome(f.life.disable());f.acquired.resolve(old);await f.clock.flush();assert((await pending).error);assert.equal((await first).error,null);assert.equal((await second).error,null);
  assert(f.releases.includes(old.leaseId));assert(f.adoptions.some(x=>x?.leaseId===old.leaseId));assert.equal(f.writes.length,0);assert.equal((f.life as any).heartbeatTimer,null);
  return{repeatedCleanupCompleted:true};
 });
 console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,scope:'real public apply/preset/OFF/Close and queue; virtual clock, inert Host/native/process; family labels are not hardware proof; no CPU benchmark'}));if(results.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
