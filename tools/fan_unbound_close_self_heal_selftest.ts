/** Public adopted unbound startup -> original Close -> bounded recovery; inert Host/native and virtual time. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:100}));
class Clock{
 now=0;seq=0;timers=new Map<number,{due:number,period:number,fn:()=>void}>();
 add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return{id};}clear(h:any){this.timers.delete(h?.id);}
 async flush(){for(let i=0;i<160;i++)await Promise.resolve();}
 async advance(ms:number){const target=this.now+ms;for(let i=0;i<1500;i++){const next=[...this.timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=next[1].due;if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=target;await this.flush();}
}
class Host extends NoIoHost{
 owner=1;pending=false;retired=false;authClose=false;nodes:FanNode[]|null=null;controlled=false;
 constructor(family:string){super();this.factoryType='HandheldCompanion.Devices.'+family;this.stateName='Open';this.opened=true;}
 snapshot():FanState{return{...super.snapshot(),hardwareWritesEnabled:this.controlled&&!!this.lease,activeCurve:this.nodes,lastControlWriteEvidence:this.controlled?'compat-route-callback-returned-no-scope':'not-written',
  hcCloseCleanupPending:this.pending,hcVirtualCloseReturned:this.retired,hcDeviceManagerStopCompleted:this.retired,oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'hc-callback-only-physical-unknown'};}
 async open(){this.stateName='Open';return super.open();}async openEvents(){this.stateName='AwaitingControl';return super.openEvents();}
 async enable(nodes:readonly FanNode[]=[]){assert(this.opened&&this.events&&this.lease);this.calls.push('enable');this.nodes=nodes.map(n=>({...n}));this.controlled=true;this.stateName='Ready';return this.snapshot();}
 async close(){if(this.owner===1&&!this.retired){this.calls.push('close');if(this.authClose)throw new FanApiError('inert close auth refused',401,'AUTH_REJECTED');this.pending=true;this.opened=this.events=false;this.stateName='AwaitingCloseCleanup';throw new FanApiError('HC_CLOSE_PENDING',409,'HC_CLOSE_PENDING');}return super.close();}
 settle(){this.pending=false;this.retired=true;this.closed=true;this.opened=this.events=this.controlled=false;this.lease=null;this.stateName='Stopped';}
 reset(){this.owner++;this.pending=this.retired=this.closed=this.opened=this.events=this.controlled=false;this.lease=null;this.nodes=null;this.generation=this.attempt=0;this.stateName='Stopped';}
}
async function fixture(family:string){
 const clock=new Clock(),saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(fn:()=>void,ms:number)=>clock.add(fn,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(fn:()=>void,ms:number)=>clock.add(fn,ms,ms),clearInterval:(h:any)=>clock.clear(h)});Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const host=new Host(family);let starts=0,stops=0;const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:5000,
  readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true}),
  launcher:{async start(){starts++;if(starts>1){assert.equal(stops,1,'successor launched before exact predecessor exit');host.reset();}return{pid:127800+starts,executable:'inert.exe',adopted:starts===1};},async stop(){stops++;}}});life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
 return{clock,host,life,counts:()=>({starts,stops,closes:host.calls.filter(x=>x==='close').length,enables:host.calls.filter(x=>x==='enable').length}),
  begin(){life.stagePendingControlCurve(curve);return life.start({manualRecovery:true}).then(value=>({value,error:null as unknown}),error=>({value:null,error}));},
  async cleanup(){host.settle();await life.close().catch(()=>{});Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
const cases:any[]=[];async function check(name:string,family:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>){const f=await fixture(family);try{cases.push({name,family,ok:true,evidence:await work(f)});}catch(error){cases.push({name,family,ok:false,error:String(error),counts:f.counts()});}finally{await f.cleanup();}}
async function main(){
 for(const family of ['ClawA2VM','ROGAlly','GPDWin5','OneXPlayerX1Mini','Lenovo.LegionGo']){
  await check('first ON survives adopted unbound Close pending without another click',family,async f=>{
   const first=f.begin();await f.clock.flush();await f.clock.advance(17000);const outcome=await first;assert(outcome.error instanceof FanApiError&&outcome.error.errorCode==='HC_CLOSE_PENDING');
   assert(f.life.recoveryActive,'unbound original Close dropped the first explicit ON');assert.deepEqual(f.counts(),{starts:1,stops:0,closes:1,enables:0});
   f.host.settle();await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.nodes,curve);assert.deepEqual(f.counts(),{starts:2,stops:1,closes:1,enables:1});return f.counts();
  });
  await check('persistent cleanup has a finite episode and never replays Close',family,async f=>{
   const first=f.begin();await f.clock.flush();await f.clock.advance(17000);await first;assert(f.life.recoveryActive);await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);
   const reads=f.host.calls.filter(x=>x==='state').length;await f.clock.advance(35000);assert.equal(f.host.calls.filter(x=>x==='state').length,reads);assert.deepEqual(f.counts(),{starts:1,stops:0,closes:1,enables:0});return f.counts();
  });
  await check('OFF during original Close prevents late automatic startup',family,async f=>{
   const first=f.begin();await f.clock.flush();assert(f.host.pending);const off=f.life.disable().catch(()=>{});await f.clock.advance(17000);await first;await off;f.host.settle();await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(!f.life.hasControlIntent);assert.equal(f.counts().starts,1);assert.equal(f.counts().enables,0);return f.counts();
  });
  await check('declared Close auth rejection cannot become automatic recovery',family,async f=>{
   f.host.authClose=true;const first=f.begin();await f.clock.flush();const outcome=await first;assert(outcome.error instanceof FanApiError&&outcome.error.status===401);assert(!f.life.recoveryActive);
   await f.clock.advance(35000);assert.equal(f.counts().starts,1);assert.equal(f.counts().enables,0);return f.counts();
  });
 }
 console.log(JSON.stringify({ok:cases.every(x=>x.ok),checks:cases.length,cases,scope:'real public start and original resource/exit owner; in-memory Host/native/launcher, virtual time, no HC/IPC/hardware or CPU benchmark'}));if(cases.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
