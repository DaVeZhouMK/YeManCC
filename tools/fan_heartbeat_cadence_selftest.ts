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
async function fixture(family='ClawCG3EM'){
 const clock=new Clock(),h=new LeaseHost(clock,family);let gen=3,starts=0,stops=0;
 const oldSet=globalThis.setInterval,oldClear=globalThis.clearInterval;
 (globalThis as any).setInterval=(fn:()=>void,ms:number)=>clock.add(fn,ms,ms);(globalThis as any).clearInterval=(handle:any)=>clock.remove(handle);
 const life=new FanHostLifecycle({enabled:true,adapter:h,heartbeatIntervalMs:5000,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:gen,phase:'ready',resumeReady:true,inputReady:true,hardwareWritesAllowed:true,hibernateAvailable:true}),launcher:{start:async()=>{starts++;return{pid:1066,executable:'inert.exe'};},stop:async()=>{stops++;}}});
 try{life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);await life.start();await life.apply(curve);}catch(e){globalThis.setInterval=oldSet;globalThis.clearInterval=oldClear;throw e;}
 return{clock,h,life,setGen:(n:number)=>{gen=n;},counts:()=>({starts,stops}),async done(){try{await life.close();}finally{h.cancelExpiry();globalThis.setInterval=oldSet;globalThis.clearInterval=oldClear;}}};
}
const cases:{name:string,ok:boolean,error?:string,evidence?:unknown}[]=[];let failureEvidence:unknown;async function check(name:string,fn:()=>Promise<unknown>){failureEvidence=undefined;try{const evidence=await fn();cases.push({name,ok:true,evidence});}catch(e){cases.push({name,ok:false,error:String(e),evidence:failureEvidence});}}
for(const family of ['ROGAlly','ClawCG3EM','GPDWin5'])for(const action of ['apply','preset','reattach','read'])await check(family+': 2s '+action+' cannot postpone the existing 5s renewal',async()=>{const f=await fixture(family);try{const lease=f.life.currentLease!.leaseId;for(let i=1;i<=7;i++){await f.clock.advance(2000);if(action==='apply')await f.life.apply(curve.map(n=>({...n,dutyPercent:Math.min(100,n.dutyPercent+i)})));else if(action==='preset')await f.life.applyPreset('aggressive',curve);else if(action==='reattach')assert.equal(await f.life.reattachExistingControlSession(),true);else await f.life.getState();}await f.clock.advance(2000);failureEvidence={heartbeatAt:[...f.h.heartbeatAt],expirations:f.h.expirations,writes:f.h.writes,intervalsCreated:f.clock.intervalsCreated,intervalsCleared:f.clock.intervalsCleared};assert.equal(f.h.expirations,0,'frequent control work starved heartbeat and caused an OEM handoff');assert.equal(f.h.writes,true);assert.equal(f.life.currentLease?.leaseId,lease,'normal work must not expire/reacquire the lease');assert.deepEqual(f.h.heartbeatAt,[5000,10000,15000]);assert.equal(f.clock.intervalsCreated,1,'same live lease repeatedly recreated its renewal timer');assert.equal(f.clock.maxIntervals,1);return{heartbeatAt:f.h.heartbeatAt,expirations:f.h.expirations,intervalsCreated:f.clock.intervalsCreated,counts:f.counts()};}finally{await f.done();}});
for(const action of ['off','close','sleep'])await check(action+' stops future timer renewal and does not manufacture control',async()=>{const f=await fixture();try{await f.clock.advance(2000);if(action==='off')await f.life.disable();else if(action==='close')await f.life.close();else{f.setGen(4);f.life.setPowerGeneration(4);f.life.observePowerBoundary('suspending',4);await f.h.suspend();await f.life.suspend();}const calls=f.h.calls.length;await f.clock.advance(30000);assert.equal(f.h.heartbeatAt.length,0);assert.equal(f.clock.intervals(),0);assert.equal(f.h.writes,false);assert.equal(f.h.calls.length,calls,'stopped owner performed new maintenance');if(action!=='sleep')assert.equal(f.life.hasControlIntent,false);}finally{await f.done();}});
await check('quiet established control keeps the same 5s cadence',async()=>{const f=await fixture();try{await f.clock.advance(16000);assert.deepEqual(f.h.heartbeatAt,[5000,10000,15000]);assert.equal(f.h.expirations,0);assert.equal(f.clock.intervalsCreated,1);}finally{await f.done();}});
await check('one held renewal does not accumulate maintenance requests',async()=>{const f=await fixture();try{f.h.holdHeartbeat=true;await f.clock.advance(14000);assert(f.h.heartbeatEntered);assert.deepEqual(f.h.heartbeatAt,[5000]);f.h.releaseHeartbeat!();await f.clock.flush();await f.clock.advance(2000);assert.deepEqual(f.h.heartbeatAt,[5000,15000]);assert.equal(f.h.expirations,0);assert.equal(f.clock.maxIntervals,1);}finally{f.h.releaseHeartbeat?.();await f.done();}});
await check('OFF then manual ON creates a fresh timer only after the new lease',async()=>{const f=await fixture();try{await f.clock.advance(4000);await f.life.disable();await f.clock.advance(5000);await f.life.apply(curve);await f.clock.advance(10000);assert.deepEqual(f.h.heartbeatAt,[14000,19000]);assert.equal(f.clock.intervalsCreated,2);assert.equal(f.clock.maxIntervals,1);assert.equal(f.h.expirations,0);assert.equal(f.h.writes,true);}finally{await f.done();}});
await check('failed renewal follows original reacquire/write recovery and restarts once',async()=>{const f=await fixture();try{f.h.failHeartbeatOnce=true;await f.clock.advance(11000);assert.deepEqual(f.h.heartbeatAt,[5000,10000]);assert.equal(f.h.calls.filter(c=>c==='acquire').length,2);assert.equal(f.h.enabledAt.length,2);assert.equal(f.h.writes,true);assert.equal(f.clock.intervalsCreated,2);assert.equal(f.clock.maxIntervals,1);}finally{await f.done();}});
await check('sleep then confirmed wake starts a new cadence after actual control recovery',async()=>{const f=await fixture();try{await f.clock.advance(2000);f.setGen(4);f.life.setPowerGeneration(4);f.life.observePowerBoundary('suspending',4);await f.h.suspend();await f.life.suspend();await f.clock.advance(20000);await f.h.resume({generation:4});f.life.observePowerBoundary('resuming',4);f.life.observePowerBoundary('resume-ready',4);await f.life.resume();await f.clock.advance(6000);assert.deepEqual(f.h.heartbeatAt,[27000]);assert.equal(f.clock.intervalsCreated,2);assert.equal(f.clock.maxIntervals,1);assert.equal(f.h.expirations,0);assert.equal(f.h.writes,true);}finally{await f.done();}});
function deferred<T>() { let resolve!: (value: T | PromiseLike<T>) => void; const promise = new Promise<T>(r => { resolve = r; }); return {promise, resolve}; }
for (const family of ['ROGAlly','ClawCG3EM','GPDWin5']) {
  for (const outcome of ['success','transport-failure','lease-rejected']) {
    for (const action of ['off','close','off-on','sleep-wake','new-curve']) {
      await check(family+': late '+outcome+' across '+action, async () => {
        const f=await fixture(family); const held=deferred<void>(); const entered=deferred<void>();
        const realHeartbeat=f.h.heartbeat.bind(f.h); let first=true;
        const beforeLease=f.life.currentLease!.leaseId; const beforeWrites=f.h.enabledAt.length;
        const changed=curve.map(n=>({...n,dutyPercent:Math.min(100,n.dutyPercent+7)}));
        f.h.heartbeat=async(id?:string)=>{
          if(!first)return realHeartbeat(id); first=false;
          if(outcome==='lease-rejected')f.h.failHeartbeatOnce=true;
          let receipt:FanLease|undefined; let fault:unknown;
          try {receipt=await realHeartbeat(id);} catch(e) {fault=e;}
          entered.resolve(); await held.promise;
          if(fault)throw fault;
          if(outcome==='transport-failure')throw Error('IPC command timed out: heartbeat result unknown');
          return receipt!;
        };
        try {
          await f.clock.advance(5000); await entered.promise;
          const pending:Promise<unknown>[]=[];
          if(action==='off'||action==='off-on')pending.push(f.life.disable());
          else if(action==='close')pending.push(f.life.close());
          else if(action==='sleep-wake'){
            f.setGen(4); f.life.setPowerGeneration(4); f.life.observePowerBoundary('suspending',4);
            await f.h.suspend(); pending.push(f.life.suspend());
          }
          if(action==='off-on'||action==='new-curve')pending.push(f.life.apply(changed));
          await f.clock.flush();
          if(action==='off'||action==='close')assert.equal(f.life.hasControlIntent,false,'terminal cancellation must be synchronous before old reply');
          await f.clock.advance(2000); held.resolve();
          const settled=await Promise.allSettled(pending); await f.clock.flush();
          if(settled.some(r=>r.status==='rejected'))throw Error('new control failed: '+settled.map(r=>r.status==='rejected'?String(r.reason):'ok').join(','));
          if(action==='sleep-wake'){
            assert.equal(f.h.enabledAt.length,beforeWrites,'late heartbeat wrote across sleep boundary');
            assert.equal(f.clock.intervals(),0);
            await f.h.resume({generation:4}); f.life.observePowerBoundary('resuming',4); f.life.observePowerBoundary('resume-ready',4); await f.life.resume();
          }
          if(action==='off'||action==='close'){
            const retiredCalls=f.h.calls.length; await f.clock.advance(30000);
            assert.equal(f.h.calls.length,retiredCalls,'retired owner performed background requests');
            assert.equal(f.h.enabledAt.length,beforeWrites,'late completion replayed canceled curve');
            assert.equal(f.clock.intervals(),0); assert.equal(f.h.writes,false);
            await f.life.apply(changed);
          }
          assert(f.life.currentLease,'manual/wake owner did not establish lease');
          if(action!=='new-curve'||outcome==='lease-rejected')assert.notEqual(f.life.currentLease!.leaseId,beforeLease,'new owner retained retired lease');
          const afterWrites=f.h.enabledAt.length; const afterRequests=f.h.heartbeatAt.length;
          await f.clock.advance(11000);
          assert.equal(f.h.writes,true,'control lost after the delayed old completion');
          assert.equal(f.h.expirations,0); assert.equal(f.clock.intervals(),1); assert.equal(f.clock.maxIntervals,1);
          assert.equal(f.h.enabledAt.length,afterWrites,'stale continuation caused extra enable');
          assert(f.h.heartbeatAt.length>=afterRequests+2,'new owner did not receive regular renewal');
          assert.equal(f.counts().starts, action==='close'?2:1,'a second Host was started unnecessarily');
          return {outcome,action,heartbeatAt:f.h.heartbeatAt,writes:f.h.enabledAt.length,intervalsCreated:f.clock.intervalsCreated,maxIntervals:f.clock.maxIntervals,starts:f.counts().starts};
        } finally {held.resolve(); await f.done();}
      });
    }
  }
}

for (const outcome of ['success','transport-failure','lease-rejected']) {
  await check('staged-only new curve retires late heartbeat '+outcome+' before any replacement submit', async () => {
    const f=await fixture(); const held=deferred<void>(); const entered=deferred<void>();
    const realHeartbeat=f.h.heartbeat.bind(f.h); let first=true;
    const originalLease={...f.life.currentLease!}; const originalWrites=f.h.enabledAt.length;
    const changed=curve.map(n=>({...n,dutyPercent:Math.min(100,n.dutyPercent+9)}));
    f.h.heartbeat=async(id?:string)=>{
      if(!first)return realHeartbeat(id); first=false;
      if(outcome==='lease-rejected')f.h.failHeartbeatOnce=true;
      let receipt:FanLease|undefined; let fault:unknown;
      try {receipt=await realHeartbeat(id);} catch(e) {fault=e;}
      entered.resolve(); await held.promise;
      if(fault)throw fault;
      if(outcome==='transport-failure')throw Error('heartbeat result unknown after commit');
      return receipt!;
    };
    try {
      await f.clock.advance(5000); await entered.promise;
      f.life.stagePendingControlCurve(changed);
      const reads=f.h.calls.filter(c=>c==='state').length;
      held.resolve(); await f.clock.flush();
      assert.equal(f.h.enabledAt.length,originalWrites,'retired heartbeat replayed the old curve before new explicit submit');
      assert.equal(f.h.calls.filter(c=>c==='state').length,reads,'retired heartbeat initiated session reconciliation');
      assert.deepEqual(f.life.currentLease,originalLease,'retired heartbeat changed local lease before newer intent submitted');
      await f.life.apply(changed); await f.clock.advance(11000);
      assert.equal(f.h.writes,true); assert.equal(f.h.expirations,0); assert.equal(f.clock.intervals(),1);
      return {outcome,writes:f.h.enabledAt.length,heartbeatAt:f.h.heartbeatAt,manualAfterStagedIntent:true};
    } finally {held.resolve();await f.done();}
  });
}
// Close is terminal before it enters the hardware queue. A slow/failed cleanup
// must not retain periodic maintenance; explicit next ON still owns restart.
await check('terminal Close stops future renewal synchronously while the original Close is held', async () => {
  const f = await fixture();
  const entered = deferred<void>(), release = deferred<void>();
  const originalClose = f.h.close.bind(f.h);
  let closing: Promise<void> | undefined;
  f.h.close = async () => { entered.resolve(); await release.promise; return originalClose(); };
  try {
    closing = f.life.close();
    await entered.promise;
    assert.equal(f.life.hasControlIntent, false);
    assert.equal(f.clock.intervals(), 0, 'terminal Close retained its periodic lease timer');
    const heartbeatCount = f.h.heartbeatAt.length;
    await f.clock.advance(12000);
    assert.equal(f.h.heartbeatAt.length, heartbeatCount, 'Close must not enqueue future renewals');
    assert.equal(f.h.enabledAt.length, 1, 'Close must not replay the cancelled curve');
    release.resolve(); await closing;
    await f.life.start({ manualRecovery: true, reason: 'manual-after-held-close' });
    await f.life.apply(curve);
    await f.clock.advance(6000);
    assert.equal(f.clock.intervals(), 1);
    assert.equal(f.h.writes, true);
    assert.equal(f.h.expirations, 0);
    return {intervalsAfterNextOn:f.clock.intervals(),enables:f.h.enabledAt.length,counts:f.counts()};
  } finally {
    release.resolve(); if (closing) await closing.catch(() => {});
    f.h.close = originalClose; await f.done();
  }
});
await check('unconfirmed Close leaves no renewal owner, lets the existing Host TTL retire cancelled control, and accepts the next manual ON', async () => {
  const f = await fixture();
  const originalClose = f.h.close.bind(f.h);
  f.h.close = async () => {
    f.h.calls.push('close-unconfirmed');
    // Shape-valid legacy reply is not evidence that virtual HC Close returned.
    return {...f.h.snapshot(),oemRestoreConfirmed:false,hcVirtualCloseReturned:false,hcDeviceManagerStopCompleted:false};
  };
  try {
    await assert.rejects(f.life.close(), /HC|Close|close|未/);
    assert.equal(f.life.hasControlIntent, false);
    await f.clock.advance(30000);
    failureEvidence={heartbeatAt:[...f.h.heartbeatAt],expirations:f.h.expirations,writes:f.h.writes,intervals:f.clock.intervals()};
    assert.equal(f.h.heartbeatAt.length, 0, 'cancelled Close intent kept extending the old Host lease');
    assert.equal(f.clock.intervals(), 0);
    assert.equal(f.h.expirations, 1, 'the existing Host expiry, not a new writer, retires the old cancelled lease');
    assert.equal(f.h.writes, false);
    await f.life.start({manualRecovery:true,reason:'manual-after-unconfirmed-close'});
    await f.life.apply(curve);
    await f.clock.advance(6000);
    assert.equal(f.h.writes, true); assert.equal(f.clock.intervals(), 1);
    assert.equal(f.counts().starts, 1, 'an open, authenticated original Host remains the single writer');
    return {expiredCancelledLease:true,nextManualOn:true,heartbeatAt:[...f.h.heartbeatAt],counts:f.counts()};
  } finally {f.h.close=originalClose;await f.done();}
});
await check('failed exact exit retains no renewal timer and the next manual ON retries the same old owner before one new start', async () => {
  const f = await fixture();
  const launcher = (f.life as any).launcher;
  const originalStop = launcher.stop.bind(launcher);
  let rejectExit = true, stopAttempts = 0;
  launcher.stop = async (process: unknown) => {
    stopAttempts++;
    if (rejectExit) throw Error('fixture exact process still alive');
    await originalStop(process);
  };
  try {
    await assert.rejects(f.life.close(), /fixture exact process still alive/);
    assert.equal(f.life.hasControlIntent, false);
    assert.equal(f.clock.intervals(), 0, 'failed process exit left a permanent no-intent maintenance timer');
    const calls = f.h.calls.length;
    await f.clock.advance(30000);
    assert.equal(f.h.calls.length, calls, 'failed exit must not keep trying heartbeat against the closed session');
    assert.equal(f.counts().starts, 1, 'an unconfirmed old process still owns the only slot');
    await assert.rejects(f.life.start({manualRecovery:true,reason:'still-unconfirmed-old-owner'}), /fixture exact process still alive/);
    assert.equal(f.counts().starts, 1, 'failed explicit exit retry must not spawn another writer');
    rejectExit = false;
    await f.life.start({manualRecovery:true,reason:'manual-after-confirmed-owner-exit'});
    await f.life.apply(curve);
    await f.clock.advance(6000);
    assert.equal(f.counts().starts, 2, 'one successor is allowed only after the exact old owner exited');
    assert.equal(f.h.calls.filter(c=>c==='close').length, 1, 'exit retry must not repeat virtual HC Close');
    assert.equal(f.clock.intervals(), 1);
    assert.equal(f.h.writes, true); assert.equal(f.h.expirations, 0);
    return {stopAttempts,starts:f.counts().starts,intervals:f.clock.intervals(),enables:f.h.enabledAt.length};
  } finally {rejectExit=false;launcher.stop=originalStop;await f.done();}
});
for (const mutation of ['apply', 'preset'] as const) for (const action of ['off', 'close'] as const) await check(mutation+': committed acquisition reply delayed across '+action+' cannot rearm maintenance while terminal cleanup is held', async () => {
  const f = await fixture();
  const entered = deferred<void>(), releaseAcquire = deferred<void>();
  const cleanupEntered = deferred<void>(), releaseCleanup = deferred<void>();
  const originalAcquire = f.h.acquireControl.bind(f.h);
  const originalClose = f.h.close.bind(f.h), originalRestore = f.h.restoreOem.bind(f.h);
  let oldApply: Promise<unknown> | undefined, terminal: Promise<unknown> | undefined;
  let first = true;
  try {
    await f.life.disable();
    f.h.acquireControl = async () => {
      const receipt = await originalAcquire();
      if (first) { first = false; entered.resolve(); await releaseAcquire.promise; }
      return receipt;
    };
    f.h.close = async () => { cleanupEntered.resolve(); await releaseCleanup.promise; return originalClose(); };
    f.h.restoreOem = async () => { cleanupEntered.resolve(); await releaseCleanup.promise; return originalRestore(); };
    oldApply = (mutation === 'apply' ? f.life.apply(curve) : f.life.applyPreset('aggressive', curve)).then(value=>value, error=>error);
    await entered.promise;
    terminal = action === 'off' ? f.life.disable() : f.life.close();
    const terminalSettled = terminal.then(value=>value, error=>error);
    assert.equal(f.clock.intervals(), 0);
    releaseAcquire.resolve(); await cleanupEntered.promise;
    failureEvidence={action,intervals:f.clock.intervals(),intervalsCreated:f.clock.intervalsCreated,heartbeatAt:[...f.h.heartbeatAt]};
    assert.equal(f.clock.intervals(), 0, 'late retired acquisition resurrected periodic renewal');
    await f.clock.advance(6000);
    assert.equal(f.h.heartbeatAt.length, 0);
    assert.equal(f.h.enabledAt.length, 1, 'cancelled first acquisition may not submit another curve');
    releaseCleanup.resolve(); await terminalSettled; await oldApply;
    await f.life.start({manualRecovery:true,reason:'manual-after-late-acquisition-'+action});
    await f.life.apply(curve); await f.clock.advance(6000);
    assert.equal(f.h.writes, true); assert.equal(f.clock.intervals(), 1); assert.equal(f.h.expirations, 0);
    return {action,heartbeatAt:[...f.h.heartbeatAt],enables:f.h.enabledAt.length,counts:f.counts()};
  } finally {
    releaseAcquire.resolve(); releaseCleanup.resolve();
    if (oldApply) await oldApply;
    if (terminal) await terminal.catch(()=>{});
    f.h.acquireControl=originalAcquire; f.h.close=originalClose; f.h.restoreOem=originalRestore;
    await f.done();
  }
});
await check('late acquired lease from a prior sleep generation is not adopted and next awake manual control uses a fresh lease', async () => {
  const f=await fixture();
  const entered=deferred<void>(),releaseAcquire=deferred<void>();
  const originalAcquire=f.h.acquireControl.bind(f.h);
  let first=true,oldApply:Promise<unknown>|undefined;
  try {
    await f.life.disable();
    f.h.acquireControl=async()=>{
      const receipt=await originalAcquire();
      if(first){first=false;entered.resolve();await releaseAcquire.promise;}
      return receipt;
    };
    oldApply=f.life.apply(curve).then(value=>value,error=>error);
    await entered.promise;
    f.setGen(4);f.life.setPowerGeneration(4);f.life.observePowerBoundary('suspending',4);
    await f.h.suspend();
    releaseAcquire.resolve();await oldApply;await f.clock.flush();
    failureEvidence={localLease:f.life.currentLease,intervals:f.clock.intervals(),heartbeatAt:[...f.h.heartbeatAt]};
    assert.equal(f.life.currentLease,null,'old-generation acquisition was adopted after the sleep boundary');
    assert.equal(f.clock.intervals(),0,'old-generation acquisition recreated a periodic timer');
    assert.equal(f.h.enabledAt.length,1);
    await f.h.resume({generation:4});f.life.observePowerBoundary('resuming',4);f.life.observePowerBoundary('resume-ready',4);
    await f.life.start({manualRecovery:true,reason:'manual-after-retired-sleep-acquisition'});
    await f.life.apply(curve);await f.clock.advance(6000);
    assert.equal(f.h.calls.filter(c=>c==='acquire').length,3,'wake must perform one fresh acquisition');
    assert.equal(f.h.writes,true);assert.equal(f.clock.intervals(),1);assert.equal(f.h.expirations,0);
    return {freshAcquisitions:3,enables:f.h.enabledAt.length,heartbeatAt:[...f.h.heartbeatAt]};
  } finally {releaseAcquire.resolve();if(oldApply)await oldApply;f.h.acquireControl=originalAcquire;await f.done();}
});
await check('newer explicit curve can reuse a same-session committed acquisition without a duplicate acquire or stale write', async () => {
  const f=await fixture();
  const entered=deferred<void>(),releaseAcquire=deferred<void>();
  const originalAcquire=f.h.acquireControl.bind(f.h),originalEnable=f.h.enable.bind(f.h);
  const changed=curve.map(node=>({...node,dutyPercent:Math.min(100,node.dutyPercent+9)}));
  const written:FanNode[][]=[];
  let first=true,oldApply:Promise<unknown>|undefined,newApply:Promise<unknown>|undefined;
  try {
    await f.life.disable();
    f.h.acquireControl=async()=>{
      const receipt=await originalAcquire();
      if(first){first=false;entered.resolve();await releaseAcquire.promise;}
      return receipt;
    };
    f.h.enable=async(nodes,id)=>{written.push((nodes??[]).map(n=>({...n})));return originalEnable(nodes,id);};
    oldApply=f.life.apply(curve).then(value=>value,error=>error);
    await entered.promise;
    newApply=f.life.apply(changed);
    releaseAcquire.resolve();await oldApply;await newApply;await f.clock.advance(6000);
    assert.deepEqual(written,[changed],'only the latest explicit curve may reach enable');
    assert.equal(f.h.calls.filter(c=>c==='acquire').length,2,'initial lease plus one shared acquisition, no third request');
    assert.equal(f.h.writes,true);assert.equal(f.clock.intervals(),1);assert.equal(f.h.expirations,0);
    return {acquisitionsIncludingInitial:2,writtenCurves:written.length,intervals:f.clock.intervals()};
  } finally {
    releaseAcquire.resolve();if(oldApply)await oldApply;if(newApply)await newApply.catch(()=>{});
    f.h.acquireControl=originalAcquire;f.h.enable=originalEnable;await f.done();
  }
});
for(const family of ['ROGAlly','ClawCG3EM','GPDWin5','OneXPlayerX1Mini','LegionGo'])for(const mode of ['renewed-reply-lost','never-arrived','manual-after-loss','three-no-arrivals','public-rejection'] as const)await check(`${family}: unknown heartbeat ${mode} retains only its existing token`,async()=>{
 const f=await fixture(family),original=f.h.heartbeat.bind(f.h),originalEnable=f.h.enable.bind(f.h);
 let remaining=mode==='three-no-arrivals'?3:1;const writes:FanNode[][]=[];
 const lost=mode==='never-arrived'?new Error('IPC command timed out: http.request'):mode==='manual-after-loss'?new Error('HTTP request deadline exceeded'):mode==='public-rejection'?new Error('WinHTTP 12002'):new FanApiError('heartbeat reply unavailable',0,'FAN_HTTP_RESULT_UNKNOWN');
 f.h.enable=async(nodes,id)=>{writes.push((nodes??[]).map(n=>({...n})));return originalEnable(nodes,id);};
 f.h.heartbeat=async(id?:string)=>{
  if(remaining-->0){
   if(mode==='renewed-reply-lost'||mode==='public-rejection')await original(id);
   else{f.h.calls.push('heartbeat');f.h.heartbeatAt.push(f.clock.now);}
   throw lost;
  }
  return original(id);
 };
 const begin=f.h.calls.length,lease=f.life.currentLease!;let firstLossCalls:string[]=[];
 const observation=()=>({mode,calls:f.h.calls.slice(begin),writtenCurves:writes.map(nodes=>nodes.map(n=>({...n}))),heartbeatAt:[...f.h.heartbeatAt],sameIssuedToken:f.life.currentLease?.leaseId===lease.leaseId,localExpires:f.life.currentLease?.expiresAtMonoMs??null,hostExpires:f.h.lease?.expiresAtMonoMs??null,intervalsCreated:f.clock.intervalsCreated,expirations:f.h.expirations});
 try{
  if(mode==='public-rejection'){
   await f.clock.advance(4000);await assert.rejects(f.life.heartbeat(),e=>e===lost);firstLossCalls=f.h.calls.slice(begin);failureEvidence=observation();
   assert.deepEqual(f.life.currentLease,lease,'unknown receipt changed or discarded the prior lease');
   assert.equal(f.h.lease!.expiresAtMonoMs,19000);assert.equal(f.life.currentLease!.expiresAtMonoMs,15000,'unknown receipt invented a local renewal deadline');
   await f.clock.advance(7000);
  }else{
   await f.clock.advance(6000);firstLossCalls=f.h.calls.slice(begin);failureEvidence=observation();
   if(mode!=='manual-after-loss'){
    assert.equal(f.life.currentLease?.leaseId,lease.leaseId,'unknown heartbeat discarded its original token');
    const automatic=f.h.calls.slice(begin);
    assert.equal(automatic.filter(x=>x==='acquire'||x==='enable'||x==='open'||x==='close'||x==='state').length,0,'unknown renewal caused fresh ownership/curve work');
   }
   if(mode==='manual-after-loss'){
    const changed=curve.map(n=>({...n,dutyPercent:70}));assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(changed);failureEvidence=observation();
    assert.deepEqual(writes,[changed],'manual control waited for old lease expiry or rewrote another curve');
   }
   await f.clock.advance(mode==='three-no-arrivals'?16000:10000);
  }
  failureEvidence=observation();
  assert(f.h.writes,'normal cadence did not retain or restore active control');
  assert.equal(f.clock.intervalsCreated,mode==='three-no-arrivals'?2:1,'unknown acknowledgement restarted maintenance');assert.equal(f.clock.maxIntervals,1);
  assert.equal(f.h.calls.filter(x=>x==='acquire').length,mode==='three-no-arrivals'?2:1,'unknown acknowledgement attempted a competing lease acquisition');
  assert.equal(writes.length,mode==='manual-after-loss'||mode==='three-no-arrivals'?1:0,'unknown heartbeat blindly replayed a curve');
  assert.equal(f.h.expirations,mode==='three-no-arrivals'?1:0);
  return{mode,firstLossCalls,heartbeatAt:f.h.heartbeatAt,extraCurveWrites:writes.length,acquisitions:f.h.calls.filter(x=>x==='acquire').length,expirations:f.h.expirations,intervalsCreated:f.clock.intervalsCreated,maxIntervals:f.clock.maxIntervals,terminal:f.life.state};
 }finally{f.h.heartbeat=original;f.h.enable=originalEnable;await f.done();}
});

// Isolate exact process/session ownership independently of PID, token and power epoch.
// These are in-memory boundary injections, not OS process replacement/device evidence.
for(const family of ['ROGAlly','ClawCG3EM','GPDWin5','OneXPlayerX1Mini','Lenovo.LegionGo']){
 for(const boundary of ['same-pid-process','session'] as const)for(const stage of ['queued','reply-success','reply-failure'] as const){
  await check(`${family}: heartbeat ${stage} retired by ${boundary}`,async()=>{
   const f=await fixture(family),owner=f.life as any,entered=deferred<void>(),release=deferred<void>();
   const original=f.h.heartbeat.bind(f.h),lease=f.life.currentLease!,writes=f.h.enabledAt.length,reads=f.h.calls.filter(x=>x==='state').length;
   const retire=()=>{if(boundary==='session')owner.advanceSessionGeneration();else owner.process={...owner.process,processCreated:'replacement-birth-same-pid'};};
   let pending:Promise<unknown>|null=null,barrier:Promise<unknown>|null=null;
   try{
    await f.clock.advance(1000);
    if(stage==='queued'){
     barrier=owner.enqueue(async()=>{entered.resolve();await release.promise;});await entered.promise;
     pending=f.life.heartbeat().then(value=>({value}),error=>({error:String(error)}));retire();release.resolve();await barrier;
    }else{
     f.h.heartbeat=async(id?:string)=>{let value:FanLease|undefined,error:unknown;if(stage==='reply-failure')f.h.failHeartbeatOnce=true;try{value=await original(id);}catch(e){error=e;}entered.resolve();await release.promise;if(error)throw error;return value!;};
     pending=f.life.heartbeat().then(value=>({value}),error=>({error:String(error)}));await entered.promise;retire();release.resolve();
    }
    const outcome=await pending;await f.clock.flush();
    const evidence={boundary,stage,outcome,beforeLease:lease,afterLease:f.life.currentLease,extraWrites:f.h.enabledAt.length-writes,extraReads:f.h.calls.filter(x=>x==='state').length-reads,heartbeats:f.h.heartbeatAt.slice(),state:f.life.state,intervals:f.clock.intervals()};failureEvidence=evidence;
    if(stage==='queued')assert.equal(f.h.heartbeatAt.length,0,'retired queue entry submitted renewal to another session');
    assert.deepEqual(f.life.currentLease,lease,'old heartbeat changed the successor local lease');
    assert.equal(f.h.enabledAt.length,writes,'retired failure rewrote the predecessor curve');
    assert.equal(f.h.calls.filter(x=>x==='state').length,reads,'retired failure started state reconciliation');
    assert.equal(f.clock.intervals(),1,'retired heartbeat stopped successor maintenance');
    f.h.heartbeat=original;await f.life.apply(curve);assert.equal(f.life.state,'ready','manual control unreachable after retirement');
    return evidence;
   }finally{release.resolve();await pending?.catch(()=>{});await barrier?.catch(()=>{});f.h.heartbeat=original;await f.done();}
  });
 }
 for(const control of ['unchanged','duplicate-resume-ready','current-lease-rejection'] as const)await check(`${family}: heartbeat owner positive control ${control}`,async()=>{
  const f=await fixture(family),held=deferred<void>(),entered=deferred<void>(),original=f.h.heartbeat.bind(f.h),beforeWrites=f.h.enabledAt.length;
  try{
   await f.clock.advance(1000);
   if(control==='current-lease-rejection'){f.h.failHeartbeatOnce=true;await f.life.heartbeat();assert.equal(f.h.enabledAt.length,beforeWrites+1);assert.equal(f.life.state,'ready');}
   else{
    const before=f.life.currentLease!;f.h.heartbeat=async(id?:string)=>{const value=await original(id);entered.resolve();await held.promise;return value;};const pending=f.life.heartbeat();await entered.promise;if(control==='duplicate-resume-ready')f.life.observePowerBoundary('resume-ready',3);held.resolve();await pending;assert.equal(f.life.currentLease?.expiresAtMonoMs,before.expiresAtMonoMs!+1000);assert.equal(f.h.enabledAt.length,beforeWrites);
   }
   f.h.heartbeat=original;await f.life.apply(curve);assert.equal(f.life.state,'ready');assert.equal(f.clock.intervals(),1);return{control,currentRenewalAccepted:true,manualAfterwards:true};
  }finally{held.resolve();f.h.heartbeat=original;await f.done();}
 });
}

for(const family of ['ROGAlly','ClawCG3EM','GPDWin5','OneXPlayerX1Mini','Lenovo.LegionGo'])await check(`${family}: current heartbeat repair owns its original HC Open session increment`,async()=>{
 const f=await fixture(family),owner=f.life as any,originalHeartbeat=f.h.heartbeat.bind(f.h),originalEnable=f.h.enable.bind(f.h),beforeWrites=f.h.enabledAt.length;
 const beforeSession=owner.sessionGeneration;
 try{
  f.h.failHeartbeatOnce=true;f.h.heartbeat=async(id?:string)=>{try{return await originalHeartbeat(id);}catch(error){f.h.opened=false;f.h.events=false;throw error;}};
  f.h.enable=async()=>{f.h.calls.push('enable-busy');throw new FanApiError('HC_OPERATION_BUSY',409,'HC_OPERATION_BUSY');};
  await assert.rejects(f.life.heartbeat());
  failureEvidence={beforeSession,afterSession:owner.sessionGeneration,recoveryActive:f.life.recoveryActive,calls:f.h.calls.slice()};
  assert.equal(owner.sessionGeneration,beforeSession+1,'fixture did not follow the original HC Open increment');
  assert(f.life.recoveryActive,'legitimate repair lost self-heal after its own Open increment');
  f.h.heartbeat=originalHeartbeat;f.h.enable=originalEnable;
  await owner.runRecoveryTick(owner.recoveryToken);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.h.enabledAt.length,beforeWrites+1);await f.life.apply(curve);assert.equal(f.life.state,'ready');
  return{originalOpenIncrement:1,selfHealCompleted:true,manualStillReachable:true};
 }finally{f.h.heartbeat=originalHeartbeat;f.h.enable=originalEnable;await f.done();}
});

for(const boundary of ['extra-session','same-pid-process'] as const)await check(`heartbeat original Open does not adopt unrelated later ${boundary}`,async()=>{
 const f=await fixture(),owner=f.life as any,originalHeartbeat=f.h.heartbeat.bind(f.h),originalEnable=f.h.enable.bind(f.h);
 try{
  f.h.failHeartbeatOnce=true;f.h.heartbeat=async(id?:string)=>{try{return await originalHeartbeat(id);}catch(error){f.h.opened=false;f.h.events=false;throw error;}};
  f.h.enable=async()=>{f.h.calls.push('enable-busy');if(boundary==='extra-session')owner.advanceSessionGeneration();else owner.process={...owner.process,processCreated:'other-birth-during-original-open'};throw new FanApiError('HC_OPERATION_BUSY',409,'HC_OPERATION_BUSY');};
  await assert.rejects(f.life.heartbeat());assert(!f.life.recoveryActive,'old repair adopted an unrelated later owner');
  f.h.heartbeat=originalHeartbeat;f.h.enable=originalEnable;await f.life.apply(curve);assert.equal(f.life.state,'ready');return{boundary,unrelatedOwnerNotAdopted:true,manualStillReachable:true};
 }finally{f.h.heartbeat=originalHeartbeat;f.h.enable=originalEnable;await f.done();}
});

for(const family of ['ROGAlly','ClawCG3EM','GPDWin5']){
 for(const phase of ['awaiting-control','starting'] as const)for(const boundary of ['intent','epoch','session','process','lease'] as const)await check(`${family}: retired queued heartbeat cannot stop ${phase} owner (${boundary})`,async()=>{
  const f=await fixture(family),owner=f.life as any,entered=deferred<void>(),release=deferred<void>();let pending:Promise<unknown>|null=null,barrier:Promise<unknown>|null=null;
  try{
   barrier=owner.enqueue(async()=>{entered.resolve();await release.promise;});await entered.promise;pending=f.life.heartbeat().then(value=>({value}),error=>({error:String(error)}));
   if(boundary==='intent')f.life.stagePendingControlCurve(curve);else if(boundary==='epoch')owner.invalidateHeartbeatEpoch();else if(boundary==='session')owner.advanceSessionGeneration();else if(boundary==='process')owner.process={...owner.process,processCreated:'next-owner-same-pid'};else{f.h.lease={...f.h.lease!,leaseId:'next-fixture-lease',generation:f.h.lease!.generation+1};f.h.arm();owner.lease={...f.h.lease};}
   // The successor is still preparing; this is a phase/identity boundary seam,
   // not an unbounded production startup or an actual process replacement.
   owner.setState(phase);const timer=owner.heartbeatTimer,lease=f.life.currentLease,calls=f.h.calls.length;
   release.resolve();await barrier;const outcome=await pending;await f.clock.flush();
   const evidence={phase,boundary,outcome,timerBefore:timer?.id??null,timerAfter:owner.heartbeatTimer?.id??null,lease,afterLease:f.life.currentLease,extraHostCalls:f.h.calls.length-calls,intervals:f.clock.intervals()};failureEvidence=evidence;
   owner.setState('ready');await f.clock.advance(16000);
   Object.assign(evidence,{expirationsAfterSettled:f.h.expirations,controlledAfterSettled:f.h.writes,heartbeatAtAfterSettled:f.h.heartbeatAt.slice()});
   assert.equal(evidence.timerAfter,timer?.id,'retired queued callback stopped successor maintenance');assert.equal(evidence.intervals,1);assert.deepEqual(evidence.afterLease,lease);assert.equal(evidence.extraHostCalls,0,'retired callback submitted new Host work');
   assert.equal(f.h.expirations,0,'lost maintenance expired the successor control lease');assert(f.h.writes);assert.equal(f.clock.intervals(),1);
   await f.life.apply(curve);assert.equal(f.life.state,'ready');return{...evidence,manualAfterSuccessorSettles:true};
  }finally{release.resolve();await pending?.catch(()=>{});await barrier?.catch(()=>{});await f.done();}
 });
 for(const missing of ['lease','ready-phase'] as const)await check(`${family}: current queued heartbeat still stops invalid ${missing}`,async()=>{
  const f=await fixture(family),owner=f.life as any;
  try{if(missing==='lease'){f.h.cancelExpiry();f.h.lease=null;f.h.writes=false;f.h.stateName='AwaitingControl';owner.lease=null;}else owner.setState('awaiting-control');const calls=f.h.calls.length;
   await assert.rejects(f.life.heartbeat(),/Fan lease 不可用/);assert.equal(owner.heartbeatTimer,null);assert.equal(f.clock.intervals(),0);assert.equal(f.h.calls.length,calls);
   owner.setState('ready');await f.life.apply(curve);assert.equal(f.life.state,'ready');assert.equal(f.clock.intervals(),1);return{missing,currentInvalidStopped:true,manualStillReachable:true};
  }finally{await f.done();}
 });
}
const result={ok:cases.every(c=>c.ok),checks:cases.length,cases,hardwareCalls:0,cpuBenchmark:false,scope:'actual bridge; virtual timer phase + inert 15s Host lease and expiry; repeated public apply/preset/reattach/read, terminal stops and real queue; no device or CPU measurement'};console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
