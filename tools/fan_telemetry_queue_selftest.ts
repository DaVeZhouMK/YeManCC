/** Production FanHostLifecycle; suspended read replies are fixtures, never device/IPC calls. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import type { FanState } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';
const curve=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
class Host extends NoIoHost {
  enableHold:Promise<void>|null=null;enableEntered:()=>void=()=>{};
  override async enable(){if(this.enableHold){this.enableEntered();await this.enableHold;}return super.enable();}
  delayedRead: { promise:Promise<FanState>; resolve:(value:FanState)=>void; reject:(error:Error)=>void; entered:Promise<void>; markEntered:()=>void } | null=null;
  armRead() {
    let resolve!:(v:FanState)=>void, reject!:(e:Error)=>void, markEntered!:()=>void;
    const promise=new Promise<FanState>((a,b)=>{resolve=a;reject=b;});
    const entered=new Promise<void>(a=>{markEntered=a;});
    return this.delayedRead={promise,resolve,reject,entered,markEntered};
  }
  override async getState() {
    this.calls.push('state'); const read=this.delayedRead;
    if (read) {this.delayedRead=null;read.markEntered();return read.promise;}
    return this.snapshot();
  }
}
function bounded<T>(promise:Promise<T>, message:string):Promise<T> {
  let timer:ReturnType<typeof setTimeout>;
  return Promise.race([promise,new Promise<never>((_,reject)=>{timer=setTimeout(()=>reject(new Error(message)),700);})]).finally(()=>clearTimeout(timer));
}
async function setup(controlled=true) {
  const h=new Host(), displayed:boolean[]=[];
  const life=new FanHostLifecycle({enabled:true,adapter:h,launcher:{async start(){return {pid:100000,executable:'no-io-fixture.exe'};},async stop(){}},
    heartbeatIntervalMs:60000,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
    onControlDisplay:active=>displayed.push(active)});
  life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
  await life.start({manualRecovery:true});if(controlled){await life.apply(curve);assert.equal(life.state,'ready');assert.equal(displayed.at(-1),true);}
  return {h,life,displayed};
}
let checks=0;const failures:string[]=[];const observations:unknown[]=[];
async function check(name:string,work:()=>Promise<void>) {++checks;try{await work();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
type QueueControl = { enqueue<T>(work:()=>Promise<T>):Promise<T> };
async function holdOwner(life:FanHostLifecycle) {
  let release!:()=>void,entered!:()=>void;
  const held=new Promise<void>(resolve=>{release=resolve;}),entry=new Promise<void>(resolve=>{entered=resolve;});
  const task=(life as unknown as QueueControl).enqueue(async()=>{entered();await held;});
  await bounded(entry,'prior owner did not enter');return {release,task};
}
async function queuedBoundary(action:'disable'|'close'|'new-curve-intent'|'new-power-generation',readMode:'none'|'optional'|'mandatory') {
  const {h,life}=await setup(),owner=await holdOwner(life),start=h.calls.length;
  const read=readMode==='none'?null:(readMode==='optional'?life.getState({discardSuperseded:true}):life.getState());
  let operation:Promise<unknown>=Promise.resolve();
  try {
    if(action==='disable')operation=life.disable();
    else if(action==='close')operation=life.close();
    else if(action==='new-curve-intent')life.stagePendingControlCurve(curve.map(n=>({...n,dutyPercent:65})));
    else {life.setPowerGeneration(7);life.observePowerBoundary('suspending',7);}
    for(let i=0;i<12;i++)await Promise.resolve();
    assert.equal(h.calls.slice(start).filter(x=>x==='state').length,0,'queued observation entered inside the held owner');
    owner.release();await bounded(operation,'new control did not settle after owner release');await owner.task;
    const result=read?await bounded(read,'queued read did not settle'):undefined;
    const trace=h.calls.slice(start),stateCalls=trace.filter(x=>x==='state').length;
    const writes=trace.filter(x=>['open','events','acquire','enable','preset','disable','release','restore','close','shutdown'].includes(x));
    if(action!=='new-power-generation'){
      const prior=h.calls.filter(x=>x==='enable').length;await bounded(life.apply(curve.map(n=>({...n,dutyPercent:65}))),'fresh manual apply was blocked by a retired observation');
      assert.equal(h.calls.filter(x=>x==='enable').length,prior+1,'fresh manual request did not write once');
    }
    return {result,stateCalls,writes};
  } finally {owner.release();await owner.task;await operation.catch(()=>{});if(read)await read.catch(()=>{});await life.close().catch(()=>{});}
}
async function main() {
  for(const action of ['apply','preset','heartbeat','disable','close'] as const) await check('pending telemetry read does not own the '+action+' lifecycle queue',async()=>{
    const {h,life,displayed}=await setup(),slot=h.armRead(),stale=h.snapshot();
    const observation=life.getState().then(x=>x,e=>e);await bounded(slot.entered,'telemetry read never entered');
    const writes=h.calls.filter(x=>x==='enable'||x==='preset').length;
    const operation=(action==='apply'?life.apply(curve.map(n=>({...n,dutyPercent:75}))):action==='preset'?life.applyPreset('balanced',curve.map(n=>({...n,dutyPercent:75}))):action==='heartbeat'?life.heartbeat():action==='disable'?life.disable():life.close()).then(x=>x,e=>e);
    try {
      const result=await bounded(operation,'read-only GET blocks '+action+' before its reply is released');
      if(result instanceof Error)throw result;
      assert.equal(h.calls.filter(x=>x==='enable'||x==='preset').length,writes+(action==='apply'||action==='preset'?1:0));
      const beforeLate=displayed.at(-1);slot.resolve(stale);await observation;
      assert.equal(displayed.at(-1),beforeLate,'old GET must not change current control display');
      if(action==='disable'||action==='close')assert.equal(displayed.at(-1),false);
    } finally {slot.resolve(stale);await observation;await operation;await life.close().catch(()=>{});}
  });
  await check('a newer read can publish revoked control before an older reply and the late reply cannot revive it',async()=>{
    const {h,life,displayed}=await setup(),slot=h.armRead(),stale=h.snapshot();
    const old=life.getState().then(x=>x,e=>e);await bounded(slot.entered,'old read never entered');
    h.lease=null;
    try {
      const latest=await bounded(life.getState(),'new telemetry blocked behind an obsolete read');
      assert.equal(latest.hardwareWritesEnabled,false);assert.equal(displayed.at(-1),false);
      slot.resolve(stale);await old;assert.equal(displayed.at(-1),false,'older same-session Ready must not revive the display');
    } finally {slot.resolve(stale);await old;await life.close().catch(()=>{});}
  });
  await check('a failed mandatory read cannot reject or poison the next admitted mutation',async()=>{
    const {h,life}=await setup(),slot=h.armRead();const old=life.getState().then(()=> 'unexpected-success',e=>String(e));await bounded(slot.entered,'read never entered');
    const operation=life.apply(curve.map(n=>({...n,dutyPercent:60}))).then(x=>x,e=>e);
    try {const result=await bounded(operation,'mutation must finish independently of a read failure');if(result instanceof Error)throw result;slot.reject(new Error('fixture telemetry transport failure'));assert.match(await old,/fixture telemetry transport failure/);assert.equal(life.state,'ready');}
    finally {slot.reject(new Error('fixture cleanup'));await old;await operation;await life.close().catch(()=>{});}
  });
  for(const action of ['apply','disable','close','newer-read','power-boundary'] as const) await check('superseded failed page read retires after '+action+' without replacing current control',async()=>{
    const {h,life,displayed}=await setup(),slot=h.armRead();
    const old=life.getState({discardSuperseded:true}).then(x=>x,e=>e);await bounded(slot.entered,'held page read never entered');
    try {
      if(action==='apply') await bounded(life.apply(curve.map(n=>({...n,dutyPercent:65}))),'new manual apply blocked by read');
      else if(action==='disable') await bounded(life.disable(),'OFF blocked by read');
      else if(action==='close') await bounded(life.close(),'Close blocked by read');
      else if(action==='newer-read') await bounded(life.getState({discardSuperseded:true}),'newer observation blocked');
      else {life.setPowerGeneration(7);life.observePowerBoundary('suspending',7);}
      const before=displayed.at(-1),calls=h.calls.length;
      slot.reject(new Error('fixture superseded telemetry timeout'));const result=await old;
      assert.equal(result,null,'retired page failure must return null instead of entering page recovery catch');
      assert.equal(displayed.at(-1),before,'obsolete failure changed latest control display');assert.equal(h.calls.length,calls,'obsolete read failure queued a new operation');
    } finally {slot.reject(new Error('fixture cleanup'));await old;await life.close().catch(()=>{});}
  });
  for(const discard of [false,true]) await check('a current failed read still reports its real error (discard='+discard+')',async()=>{
    const {h,life,displayed}=await setup(),slot=h.armRead(),error=new Error('fixture current telemetry failure');
    const read=(discard?life.getState({discardSuperseded:true}):life.getState()).then(x=>x,e=>e);await bounded(slot.entered,'current read never entered');
    try {slot.reject(error);assert.equal(await read,error,'current transport error was hidden');assert.equal(displayed.at(-1),true,'failed GET itself revoked confirmed control');}
    finally {slot.reject(error);await read;await life.close().catch(()=>{});}
  });
  await check('mandatory confirmation retains a stale failure for the caller intent checks',async()=>{
    const {h,life}=await setup(),slot=h.armRead(),error=new Error('fixture mandatory confirmation failure');
    const read=life.getState().then(x=>x,e=>e);await bounded(slot.entered,'confirmation never entered');
    try {await bounded(life.apply(curve.map(n=>({...n,dutyPercent:50}))),'new apply blocked');slot.reject(error);assert.equal(await read,error,'mandatory caller error contract changed');}
    finally {slot.reject(error);await read;await life.close().catch(()=>{});}
  });
  for(const discardSuperseded of [false,true]) await check('a read requested during enable waits for existing controls without joining their queue (discard='+discardSuperseded+')',async()=>{
    const {h,life,displayed}=await setup(false);let release!:()=>void,entered!:()=>void;
    h.enableHold=new Promise<void>(r=>{release=r;});const called=new Promise<void>(r=>{entered=r;});h.enableEntered=entered;
    const write=life.apply(curve).then(x=>x,e=>e);await bounded(called,'enable did not enter');
    const slot=h.armRead();let readEntered=false;void slot.entered.then(()=>{readEntered=true;});
    const read=(discardSuperseded?life.getState({discardSuperseded:true}):life.getState()).then(x=>x,e=>e);
    const heartbeat=life.heartbeat().then(x=>x,e=>e);
    try {
      for(let i=0;i<12;i++)await Promise.resolve();assert.equal(readEntered,false,'GET must not start inside admitted enable');
      release();const result=await bounded(write,'enable blocked');if(result instanceof Error)throw result;
      const renewed=await bounded(heartbeat,'read must not occupy the queue ahead of a later heartbeat');if(renewed instanceof Error)throw renewed;
      await bounded(slot.entered,'read did not proceed after queued controls settled');assert.equal(displayed.at(-1),true);
      slot.resolve(h.snapshot());const observed=await read;if(observed instanceof Error)throw observed;assert.equal(observed?.state,'Ready');assert.equal(displayed.at(-1),true);
    } finally {release();slot.resolve(h.snapshot());await write;await heartbeat;await read;await life.close().catch(()=>{});}
  });
  await check('a delayed pre-sleep Ready cannot re-enable the display after the real power boundary',async()=>{
    const {h,life,displayed}=await setup(),slot=h.armRead(),stale=h.snapshot();const old=life.getState().then(x=>x,e=>e);await bounded(slot.entered,'read never entered');
    try {life.setPowerGeneration(7);life.observePowerBoundary('suspending',7);assert.equal(displayed.at(-1),false);const count=h.calls.filter(x=>['enable','preset','acquire'].includes(x)).length;slot.resolve(stale);await old;assert.equal(displayed.at(-1),false);assert.equal(h.calls.filter(x=>['enable','preset','acquire'].includes(x)).length,count);}
    finally {slot.resolve(stale);await old;await life.close().catch(()=>{});}
  });
  for(const action of ['disable','close','new-curve-intent','new-power-generation'] as const) await check('queued optional observation retires before sending after '+action,async()=>{
    const control=await queuedBoundary(action,'none'),observed=await queuedBoundary(action,'optional');
    observations.push({scenario:'queued-optional-'+action,controlStateCalls:control.stateCalls,observedStateCalls:observed.stateCalls,controlWrites:control.writes,observedWrites:observed.writes,retired:observed.result===null});
    assert.deepEqual(observed.writes,control.writes,'dropping an old observation changed control actions');
    assert.equal(observed.result,null,'a pre-boundary queued page read was rebound as a new observation');
    assert.equal(observed.stateCalls,control.stateCalls,'retired page observation still sent an unnecessary state request');
  });
  for(const action of ['disable','close'] as const) await check('queued mandatory confirmation still sends after '+action,async()=>{
    const control=await queuedBoundary(action,'none'),observed=await queuedBoundary(action,'mandatory');
    assert(observed.result&&typeof observed.result.state==='string','mandatory confirmation was silently retired');
    assert.equal(observed.stateCalls,control.stateCalls+1,'mandatory state request disappeared');assert.deepEqual(observed.writes,control.writes);
  });
  await check('only the newest optional observer waiting on the same owner sends a state request',async()=>{
    const {h,life}=await setup(),owner=await holdOwner(life),before=h.calls.filter(x=>x==='state').length;
    const first=life.getState({discardSuperseded:true}),latest=life.getState({discardSuperseded:true});
    try {owner.release();await owner.task;const [old,current]=await bounded(Promise.all([first,latest]),'queued observers did not settle');
      observations.push({scenario:'two-queued-optional-observers',stateCalls:h.calls.filter(x=>x==='state').length-before,oldRetired:old===null});
      assert.equal(old,null,'older queued observer still sent after a newer request');assert.equal(current?.state,'Ready');assert.equal(h.calls.filter(x=>x==='state').length,before+1,'both queued observations transmitted');
    }finally{owner.release();await owner.task;await Promise.allSettled([first,latest]);await life.close().catch(()=>{});}
  });
  for(const optional of [false,true]) await check('a current observation waiting on a prior owner still reports its own transport error (optional='+optional+')',async()=>{
    const {h,life}=await setup(),owner=await holdOwner(life),slot=h.armRead(),failure=new Error('fixture queued current state error');
    const read=(optional?life.getState({discardSuperseded:true}):life.getState()).then(x=>x,e=>e);
    try{owner.release();await owner.task;await bounded(slot.entered,'current queued read did not enter');slot.reject(failure);assert.equal(await read,failure,'current queued error was swallowed');}
    finally{owner.release();slot.reject(failure);await owner.task;await read;await life.close().catch(()=>{});}
  });
  console.log(JSON.stringify({ok:failures.length===0,checks,failures,observations,hardwareCalls:0,scope:'actual public bridge; inert Host/native/process; held reply is a sequencing oracle, not a CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();
