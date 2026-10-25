/** Actual mirror actions and public lifecycle; inert protocol boundaries, no device I/O. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {createFanMirrorActions} from '../src/bridge/deckyFanActions';
import {FanApiError,type FanNode} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const CURVE:FanNode[]=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const NEXT=CURVE.map(n=>({...n,dutyPercent:75}));
class Clock {
 now=0;seq=0;timers=new Map<number,{due:number,period:number,fn:()=>void}>();
 add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return {id};}
 clear(h:any){this.timers.delete(h?.id);}
 async flush(){for(let i=0;i<150;i++)await Promise.resolve();}
 async advance(ms:number){const target=this.now+ms;for(let i=0;i<1000;i++){const next=[...this.timers.entries()].filter(([,v])=>v.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=next[1].due;if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=target;await this.flush();}
}
class Host extends NoIoHost {
 applied:FanNode[]|null=null;failure:FanApiError|null=null;
 snapshot(){return {...super.snapshot(),activeCurve:this.applied,hardwareWritesEnabled:!!this.lease&&this.applied!==null,lastControlWriteEvidence:this.applied?'compat-route-callback-returned-no-scope':'not-written',oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'fixture-no-physical-proof'};}
 async enable(nodes:readonly FanNode[]=[]){this.calls.push('enable');if(this.failure)throw this.failure;this.applied=nodes.map(n=>({...n}));this.stateName='Ready';return this.snapshot();}
 async applyPreset(_name?:string,_lease?:string,nodes?:readonly FanNode[]){return this.enable(nodes);}
 async disable(){this.applied=null;return super.disable();}
}
const results:any[]=[];
async function fixture(model:string){
 const clock=new Clock(),saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(f:()=>void,ms:number)=>clock.add(f,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(f:()=>void,ms:number)=>clock.add(f,ms,ms),clearInterval:(h:any)=>clock.clear(h)});
 Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const host=new Host();host.factoryType=({MSI:'HandheldCompanion.Devices.ClawA2VM',ROG:'HandheldCompanion.Devices.ROGAlly',GPD:'HandheldCompanion.Devices.GPDWin5',OneX:'HandheldCompanion.Devices.OneXPlayerX1Mini',Lenovo:'HandheldCompanion.Devices.Lenovo.LegionGo'} as Record<string,string>)[model];
 const native:any={generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
 const life=new FanHostLifecycle({enabled:true,adapter:host,readNativeActivity:async()=>null,readNativePowerState:async()=>native,launcher:{async start(){return {pid:50100,executable:'inert.exe'};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});await life.apply(CURVE);
 let enabled=true;const displays:any[]=[];let starts=0;const start=life.start.bind(life);life.start=async(...args)=>{starts++;return start(...args);};
 const actions=createFanMirrorActions({owner:life,enabled:()=>enabled,currentPreset:()=> 'balanced',curve:()=>NEXT.map(n=>({...n})),save:async()=>{},updateActive:v=>enabled=v,updateDuty:()=>{},power:async()=>native,featureAllowed:()=>true,uiBusy:()=>false,display:v=>displays.push(v)});
 return {clock,host,life,actions,displays,getStarts:()=>starts,setEnabled:(v:boolean)=>enabled=v,async attention(){host.failure=new FanApiError('callback failed',500,'HOST_ERROR');await assert.rejects(life.apply(NEXT));await clock.advance(10000);assert(life.recoveryNeedsAttention);assert.equal(life.state,'ready');assert(life.hasControlIntent);assert.deepEqual(host.applied,CURVE);},async cleanup(){host.failure=null;actions.stop();await life.close().catch(()=>{});Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
async function check(name:string,model:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<void>){const f=await fixture(model);try{await work(f);results.push({name,model,ok:true,calls:f.host.calls.slice(),displays:f.displays});}catch(error){results.push({name,model,ok:false,error:String(error),calls:f.host.calls.slice(),displays:f.displays});}finally{await f.cleanup();}}
async function main(){
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo']) {
  await check('request ON after exhausted recovery must reach original apply instead of already-enabled',model,async f=>{
   await f.attention();f.host.failure=null;const writes=f.host.calls.filter(c=>c==='enable').length;
   const r=await f.actions.request('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});await f.clock.flush();
   assert.equal(r.pending,true,'new ON must be admitted, not deduplicated as a successful old curve');assert.equal(f.host.calls.filter(c=>c==='enable').length,writes+1);assert.deepEqual(f.host.applied,NEXT);assert(!f.life.recoveryNeedsAttention);assert.equal(f.getStarts(),1);
  });
  await check('execute ON after exhausted recovery must reach original apply',model,async f=>{
   await f.attention();f.host.failure=null;const writes=f.host.calls.filter(c=>c==='enable').length;
   const r=await f.actions.execute('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});assert(r.applied);assert.equal(f.host.calls.filter(c=>c==='enable').length,writes+1);assert.deepEqual(f.host.applied,NEXT);assert(!f.life.recoveryNeedsAttention);
  });
  await check('terminal refusal keeps intent but cannot report an active recovery',model,async f=>{
   f.setEnabled(false);f.host.failure=new FanApiError('unauthorized',403,'API_PERMISSION_DENIED');
   await f.actions.request('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});await f.clock.flush();assert(f.life.recoveryNeedsAttention);assert(!f.life.recoveryActive);assert(!f.actions.pendingIntent);
   const last=f.displays.at(-1);assert.equal(last.pending,false,'intent retention must not invent ongoing recovery');assert.doesNotMatch(last.notice,/正在恢复/);
   f.host.failure=null;const writes=f.host.calls.filter(c=>c==='enable').length;await f.actions.request('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});await f.clock.flush();assert.equal(f.host.calls.filter(c=>c==='enable').length,writes+1);assert(!f.life.recoveryNeedsAttention);
  });
 }
 await check('healthy current control still deduplicates both mirror entry points','MSI',async f=>{
  const writes=f.host.calls.filter(c=>c==='enable').length;
  const a=await f.actions.request('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});const b=await f.actions.execute('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});assert(a.applied&&b.applied);assert.equal(f.host.calls.filter(c=>c==='enable').length,writes);assert.equal(f.getStarts(),0);
 });
 await check('an actually active limited recovery still reports pending','MSI',async f=>{
  f.setEnabled(false);f.host.failure=new FanApiError('not submitted',409,'HC_OPERATION_BUSY');const r=await f.actions.execute('fan.setEnabled',{enabled:true},{generation:6,checkpoint(){}});assert.equal(r.pending,true);assert(f.life.recoveryActive);assert.equal(f.displays.at(-1).pending,true);
 });
 console.log(JSON.stringify({ok:results.every(r=>r.ok),checks:results.length,results,scope:'actual mirror actions + public lifecycle + bounded virtual timers; inert adapters, no hardware, no CPU benchmark'}));if(results.some(r=>!r.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
