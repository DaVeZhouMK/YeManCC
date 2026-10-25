/** Actual Fan lifecycle; inert authenticated Host/launcher/power boundaries only. */
import fs from 'node:fs';
import { FanHostLifecycle, type FanHostProcess } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanNode, type FanState } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';

const curve: FanNode[] = [{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:35},{tempC:100,dutyPercent:100}];
type Trace = {op:string;pid:number};
function fixture() {
  const trace: Trace[] = [];
  let nextPid=10410, process: FanHostProcess|null=null, alive=false, stopFails=false, proof=true, pending=false;
  let stopGate: {reached:()=>void;wait:Promise<void>}|null=null;
  class Host extends NoIoHost {
    constructor(readonly pid:number) { super(); }
    note(op:string) { trace.push({op,pid:this.pid}); }
    override snapshot(): FanState { return {...super.snapshot(),oemRestoreConfirmed:this.closed,
      oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'fixture-callback-only-physical-unknown',
      hcVirtualCloseReturned:this.closed&&proof,hcDeviceManagerStopCompleted:this.closed&&proof,
      hcCloseCleanupPending:pending,hardwareWrites:false}; }
    assertLive() { if(this.stateName==='FaultLocked')throw new FanApiError('FAULT_LOCKED',409,'FAULT_LOCKED');if(this.closed) {this.note('rejected-stopped-engine');throw new FanApiError('HOST_STOPPED',409,'HOST_STOPPED');} }
    retire() {this.closed=true;this.opened=this.events=false;this.lease=null;this.stateName='Stopped';}
    override async getState() {this.note('state');return this.snapshot();}
    override async open() {this.assertLive();this.note('Open');return super.open();}
    override async openEvents() {this.assertLive();this.note('OpenEvents');return super.openEvents();}
    override async acquireControl() {this.assertLive();this.note('acquire');return super.acquireControl();}
    override async enable() {this.assertLive();if(!this.opened||!this.events)throw new Error('write-before-session');this.note('enable');return super.enable();}
    override async applyPreset() {this.assertLive();if(!this.opened||!this.events)throw new Error('write-before-session');this.note('preset');return super.applyPreset();}
    override async close() {this.note('Close');return super.close();}
    override async shutdown() {this.note('shutdown');}
  }
  let host=new Host(nextPid);
  const adapter=new Proxy({enabled:true} as FanApiAdapter,{get(_target,key){const value=Reflect.get(host,key);return typeof value==='function'?value.bind(host):value;}});
  const life=new FanHostLifecycle({enabled:true,adapter,heartbeatIntervalMs:0,resumeWaitDeadlineMs:600,
    readNativeActivity:async()=>null,
    readNativePowerState:async()=>({generation:0,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:true,hibernateAvailable:true}),
    launcher:{async start(){
      if(alive&&process){trace.push({op:'adopt',pid:process.pid});return {...process,adopted:true};}
      host=new Host(++nextPid);process={pid:host.pid,executable:'inert-FanHost.exe',processCreated:'fixture-'+host.pid};alive=true;
      trace.push({op:'launch',pid:host.pid});return process;
    },async stop(target){trace.push({op:'stop',pid:target.pid});if(stopGate){const gate=stopGate;stopGate=null;gate.reached();await gate.wait;}if(stopFails)throw new Error('exact-exit-not-confirmed');
      if(!process||target.pid!==process.pid||target.executable!==process.executable||target.processCreated!==process.processCreated||!host.closed)throw new Error('exit-before-exact-retirement');alive=false;}}});
  return {life,trace,get host(){return host;},set stopFails(value:boolean){stopFails=value;},set proof(value:boolean){proof=value;},set pending(value:boolean){pending=value;},
    holdStop(){let signal!:()=>void,release!:()=>void;const reached=new Promise<void>(r=>{signal=r;}),wait=new Promise<void>(r=>{release=r;});stopGate={reached:signal,wait};return {reached,release};}};
}
const results:Array<{name:string;passed:boolean;error?:string;trace:Trace[]}>=[];
async function run(name:string,work:(f:ReturnType<typeof fixture>)=>Promise<void>){
 const f=fixture();try{await f.life.start({manualRecovery:true});await f.life.apply(curve);f.trace.length=0;await work(f);
 results.push({name,passed:true,trace:[...f.trace]});}catch(error){results.push({name,passed:false,error:String(error),trace:[...f.trace]});}
 finally{f.stopFails=false;f.proof=true;f.pending=false;await f.life.close().catch(()=>undefined);}
}
function check(value:unknown,reason:string):asserts value{if(!value)throw new Error(reason);}
function continued(f:ReturnType<typeof fixture>){
 check(f.life.state==='ready'&&f.host.stateName==='Ready','same explicit request must write on a ready successor');
 check(f.trace.filter(t=>t.op==='launch').length===1,'exactly one successor');
 check(f.trace.filter(t=>t.op==='stop').length===1,'exact predecessor must exit once');
 check(!f.trace.some(t=>t.op==='Close'),'already retired HC must not receive another virtual Close');
 check(!f.trace.some(t=>t.op==='rejected-stopped-engine'),'never reopen or write the terminal engine');
 const stop=f.trace.findIndex(t=>t.op==='stop'),launch=f.trace.findIndex(t=>t.op==='launch');check(stop>=0&&stop<launch,'exit must precede launch');
}
await run('retired remote Stopped + stale local Ready: direct manual curve continues once',async f=>{f.host.retire();await f.life.apply(curve);continued(f);check(f.trace.filter(t=>t.op==='enable').length===1,'one curve write');});
await run('retired remote Stopped + stale local Ready: explicit start then preset continues once',async f=>{f.host.retire();const gate=await f.life.start({manualRecovery:true});check(gate.allowed&&gate.writeReady,'manual start must complete admission');await f.life.applyPreset('aggressive',curve);continued(f);check(f.trace.filter(t=>t.op==='preset').length===1,'one preset write');});
await run('retired remote Stopped + terminal local mirror: same manual curve continues',async f=>{f.host.retire();(f.life as unknown as {setState(s:string):void}).setState('fault-locked');await f.life.apply(curve);continued(f);});
await run('exact exit failure blocks successor; later manual request can continue',async f=>{f.host.retire();f.stopFails=true;let rejected=false;try{await f.life.apply(curve);}catch{rejected=true;}check(rejected,'unconfirmed exit must reject');check(!f.trace.some(t=>t.op==='launch'||t.op==='enable'||t.op==='preset'),'no successor/writes without exit');f.stopFails=false;f.trace.length=0;await f.life.apply(curve);continued(f);});
await run('missing HC cleanup proof never authorizes retirement or a second Host',async f=>{f.host.retire();f.proof=false;try{await f.life.apply(curve);}catch{}check(!f.trace.some(t=>t.op==='stop'||t.op==='launch'||t.op==='enable'||t.op==='preset'),'missing proof must block retirement');});
await run('pending HC cleanup never authorizes retirement or a second Host',async f=>{f.host.retire();f.pending=true;try{await f.life.apply(curve);}catch{}check(!f.trace.some(t=>t.op==='stop'||t.op==='launch'||t.op==='enable'||t.op==='preset'),'pending cleanup must block retirement');});
await run('healthy established session retains one existing probe and one write',async f=>{await f.life.apply(curve);check(f.trace.filter(t=>t.op==='state').length===1,'no new healthy-session probes');check(f.trace.filter(t=>t.op==='enable').length===1,'one normal write');check(!f.trace.some(t=>['stop','launch','Close','Open','OpenEvents'].includes(t.op)),'healthy session must not rebuild');});
for(const mirror of ['unknown','conflict-locked'])await run('retired owner + '+mirror+' local mirror follows the same exact retirement',async f=>{f.host.retire();(f.life as unknown as {setState(s:string):void}).setState(mirror);await f.life.apply(curve);continued(f);});
for(const cancel of ['off','sleep'])await run('new '+cancel+' during retired-owner exit cancels successor and curve',async f=>{
 f.host.retire();const gate=f.holdStop();const requested=f.life.apply(curve).then(()=> 'accepted',error=>String(error));
 let cancellation:Promise<unknown>=Promise.resolve();
 try{
  let timeout:ReturnType<typeof setTimeout>|undefined;
  try{await Promise.race([gate.reached,new Promise<never>((_resolve,reject)=>{timeout=setTimeout(()=>reject(new Error('retirement exit owner not reached')),1500);})]);}finally{clearTimeout(timeout);}
  if(cancel==='off')cancellation=f.life.disable();
  else{f.life.setPowerGeneration(1);f.life.observePowerBoundary('suspending',1);}
 }finally{gate.release();}
 const outcome=await requested;await cancellation;
 check(outcome.includes('FAN_CONTROL_INTENT_SUPERSEDED'),'cancelled intent cannot continue after exit');
 check(!f.trace.some(t=>['launch','Open','OpenEvents','enable','preset'].includes(t.op)),'cancelled request cannot launch or write');
});
await run('local fault with an unretired remote fault completes one real Close before replacement',async f=>{
 f.host.stateName='FaultLocked';(f.life as unknown as {setState(s:string):void}).setState('fault-locked');await f.life.apply(curve);
 check(f.life.state==='ready'&&f.host.stateName==='Ready','same request recovers a live terminal engine');
 check(f.trace.filter(t=>t.op==='Close').length===1,'an unretired session requires one Close');
 check(f.trace.filter(t=>t.op==='stop').length===1&&f.trace.filter(t=>t.op==='launch').length===1,'one exact exit and one successor');
 check(f.trace.findIndex(t=>t.op==='Close')<f.trace.findIndex(t=>t.op==='stop'),'Close must precede exit');
});
await run('authentication refusal on a retained terminal owner never becomes a replacement',async f=>{
 (f.life as unknown as {setState(s:string):void}).setState('unknown');
 f.host.getState=async()=>{f.trace.push({op:'auth-rejected',pid:f.host.pid});throw new FanApiError('API_SESSION_REQUIRED',401,'API_SESSION_REQUIRED');};
 let rejected=false;try{await f.life.start({manualRecovery:true});}catch(error){rejected=error instanceof FanApiError&&error.status===401;}
 check(rejected,'preserve authenticated refusal');check(f.trace.length===1&&f.trace[0]?.op==='auth-rejected','no adoption, Close, exit, launch or write after refusal');
});
const report={ok:results.every(r=>r.passed),checks:results.length,hardwareCalls:0,scope:'real FanHostLifecycle; inert authenticated HTTP adapter, native power and exact-process launcher fixtures; not UI/device proof',results};
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));process.exitCode=report.ok?0:1;
