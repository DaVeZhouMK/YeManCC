/** Real failed mutations/heartbeat + delayed readback; no production Host or device. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve=(n:number)=>[0,40,70,100].map(tempC=>({tempC,dutyPercent:n}));
const held=<T>()=>{let resolve!:(v:T)=>void,enter!:()=>void;return {promise:new Promise<T>(r=>resolve=r),ready:new Promise<void>(r=>enter=r),resolve:(v:T)=>resolve(v),enter:()=>enter()};};
const settled=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
const flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();};
async function fixture(){
 const host=new NoIoHost();let native:any={generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
 const writes:FanNode[][]=[];const originalEnable=host.enable.bind(host);host.enable=async(nodes:readonly FanNode[]=[])=>{writes.push(nodes.map(n=>({...n})));return originalEnable();};
 host.applyPreset=async(_name?:string,_lease?:string,nodes?:readonly FanNode[])=>{host.calls.push('preset');return host.enable(nodes);};
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,readNativePowerState:async()=>native,launcher:{async start(){return{pid:116401,executable:'inert.exe'};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});await life.apply(curve(100));host.generation=6;host.attempt=1;host.powerState='On';
 return{life,host,writes,observations:[] as unknown[],setNative:(v:any)=>native=v,async cleanup(){await life.close().catch(()=>{});}};
}
const results:any[]=[];
async function check(name:string,run:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>){if(process.env.YMCC_FAN_FAILED_READ_CASE&&!name.includes(process.env.YMCC_FAN_FAILED_READ_CASE))return;const f=await fixture();try{results.push({name,ok:true,evidence:await run(f)});}catch(e){results.push({name,ok:false,error:String(e),calls:f.host.calls.slice(),observations:f.observations});}finally{await f.cleanup();}}
async function main(){
 for(const op of ['apply','preset','heartbeat'] as const)for(const edge of ['curve','off','close','sleep','generation','resuming'] as const)for(const when of ['during-write','during-state'] as const)await check(`${op}: retired ${when} by ${edge}`,async f=>{
  const write=held<void>(),read=held<FanState>();let retiredAdoptions=0,inResync=false,failureReads=0,resyncOrdinal=0,activeResyncOrdinal=0;
  const originalRead=f.host.getState.bind(f.host);
  const stale={...f.host.snapshot(),state:'AwaitingControl',openCalled:false,openEventsCalled:false,lease:null,leaseGeneration:null};
  const owner=f.life as any,originalSync=owner.syncRemoteSessionState.bind(owner);
  owner.syncRemoteSessionState=(remote:any,...args:any[])=>{const before=owner.stateObservationRevision;const result=originalSync(remote,...args);if(remote===stale&&owner.stateObservationRevision!==before)retiredAdoptions++;return result;};
  const originalResync=owner.resyncSessionFromHost.bind(owner);
  owner.resyncSessionFromHost=async(...args:any[])=>{inResync=true;activeResyncOrdinal=++resyncOrdinal;try{return await originalResync(...args);}finally{inResync=false;}};
  f.host.getState=async()=>{if(inResync&&activeResyncOrdinal===1){failureReads++;f.host.calls.push('state');read.enter();return when==='during-state'?read.promise:stale;}return originalRead();};
  const original=f.host[op==='apply'?'enable':op==='preset'?'applyPreset':'heartbeat'].bind(f.host);
  const fail=async(...args:any[])=>{f.host.calls.push('failed-'+op);if(op==='apply')f.host.enable=original;else if(op==='preset')f.host.applyPreset=original;else f.host.heartbeat=original;write.enter();if(when==='during-write')await write.promise;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
  if(op==='apply')f.host.enable=fail;else if(op==='preset')f.host.applyPreset=fail;else f.host.heartbeat=fail;
  const operation=settled(op==='apply'?f.life.apply(curve(55)):op==='preset'?f.life.applyPreset('soft',curve(55)):f.life.heartbeat());
  await (when==='during-write'?write.ready:read.ready);
  let terminal:ReturnType<typeof settled>|null=null;
  if(edge==='curve')f.life.stagePendingControlCurve(curve(75));
  else if(edge==='off')terminal=settled(f.life.disable());
  else if(edge==='close')terminal=settled(f.life.close());
  else if(edge==='sleep')f.life.observePowerBoundary('suspending',6);
  else if(edge==='generation')f.life.setPowerGeneration(7);
  else {f.setNative({generation:6,phase:'resuming',hardwareWritesAllowed:false,resumeReady:false});f.life.observePowerBoundary('resuming',6);}
  write.resolve();read.resolve(stale);
  if(edge==='resuming'){await flush();f.life.stagePendingControlCurve(curve(75));}
  await operation;if(terminal)await terminal;
  f.observations.push({op,edge,when,retiredAdoptions,failureReads,writes:f.writes.map(nodes=>nodes[0]?.dutyPercent)});
  try{
   assert(!f.writes.some(nodes=>nodes[0]?.dutyPercent===55),'cancelled old curve was retried after the user/power boundary');
   assert.equal(retiredAdoptions,0,'retired failure snapshot changed current session/display');
   if(when==='during-write')assert.equal(failureReads,0,'already-retired failure still queried Host');
   if(edge==='curve'){
    if(op==='apply')f.host.enable=original;else if(op==='preset')f.host.applyPreset=original;else f.host.heartbeat=original;
    const begin=f.host.calls.length;await f.life.apply(curve(75));assert.equal(f.life.state,'ready');assert.deepEqual(f.writes.at(-1),curve(75));
    assert.equal(f.host.calls.slice(begin).filter(x=>x==='open'||x==='events').length,0,'stale failed read forced unnecessary HC Open');
   }
   return{retiredAdoptions,failureReads};
  }finally{write.resolve();read.resolve(stale);}
 });
 for(const op of ['apply','preset'] as const)for(const edge of ['curve','off','close'] as const)await check(`${op}: retry queued before ${edge} cannot borrow the new intent`,async f=>{
  const read=held<FanState>(),block=held<void>();const owner=f.life as any;
  const originalRead=f.host.getState.bind(f.host),originalResync=owner.resyncSessionFromHost.bind(owner);let inResync=false,readOnce=true;
  owner.resyncSessionFromHost=async(...args:any[])=>{inResync=true;try{return await originalResync(...args);}finally{inResync=false;}};
  f.host.getState=async()=>{if(inResync&&readOnce){readOnce=false;read.enter();return read.promise;}return originalRead();};
  const original=f.host[op==='apply'?'enable':'applyPreset'].bind(f.host);
  const fail=async()=>{if(op==='apply')f.host.enable=original;else f.host.applyPreset=original;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
  if(op==='apply')f.host.enable=fail;else f.host.applyPreset=fail;
  const work=settled(op==='apply'?f.life.apply(curve(55)):f.life.applyPreset('soft',curve(55)));
  try{
   await read.ready;const blocker=owner.enqueue(async()=>{block.enter();await block.promise;});read.resolve(f.host.snapshot());await block.ready;await flush();
   let terminal:ReturnType<typeof settled>|null=null;
   if(edge==='curve')f.life.stagePendingControlCurve(curve(75));else if(edge==='off')terminal=settled(f.life.disable());else terminal=settled(f.life.close());
   block.resolve();await blocker;await work;if(terminal)await terminal;
   assert(!f.writes.some(nodes=>nodes[0]?.dutyPercent===55),'queued retry rewrote a retired curve');
   if(edge==='curve'){await f.life.apply(curve(75));assert.deepEqual(f.writes.at(-1),curve(75));}
   return{writes:f.writes.map(nodes=>nodes[0]?.dutyPercent)};
  }finally{read.resolve(f.host.snapshot());block.resolve();await work;}
 });
 for(const op of ['apply','preset','heartbeat'] as const)await check(`${op}: current failure still synchronizes a really closed session`,async f=>{
  let fail=true;const key=op==='apply'?'enable':op==='preset'?'applyPreset':'heartbeat',original=f.host[key].bind(f.host);
  f.host[key]=async(...args:any[])=>{if(fail){fail=false;f.host.opened=f.host.events=false;f.host.lease=null;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');}return original(...args);};
  const begin=f.host.calls.length;await settled(op==='apply'?f.life.apply(curve(60)):op==='preset'?f.life.applyPreset('soft',curve(60)):f.life.heartbeat());
  const calls=f.host.calls.slice(begin);assert.equal(f.life.state,'ready');assert.equal(calls.filter(x=>x==='open').length,1);assert.equal(calls.filter(x=>x==='events').length,1);return{calls};
 });
 console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,scope:'real public mutation/preset/heartbeat with inert Host/native/launcher; delayed readback identity, no hardware/CPU benchmark'}));if(results.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
