/** Real public startup + OFF/Close with held read-only replies; all boundaries inert. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve=(duty:number)=>[0,40,70,100].map(tempC=>({tempC,dutyPercent:duty}));
const gate=<T>()=>{let resolve!:(v:T)=>void,entered!:()=>void;const promise=new Promise<T>(r=>resolve=r),ready=new Promise<void>(r=>entered=r);return {resolve,entered,promise,ready};};
const outcomes=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
async function bounded<T>(p:Promise<T>){let timer:ReturnType<typeof setTimeout>|undefined;try{return await Promise.race([p,new Promise<never>((_,reject)=>timer=setTimeout(()=>reject(Error('inert operation did not settle')),2500))]);}finally{clearTimeout(timer);}}
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
async function fixture(family:typeof families[number]=families[0],cold=false){
 const host=new NoIoHost();const writes:any[]=[];const enable=host.enable.bind(host);host.enable=async(nodes:readonly {tempC:number;dutyPercent:number}[]=[])=>{writes.push(nodes.map(n=>({...n})));return enable();};host.factoryType=family[1];let native:any={generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
 let nativeHold:ReturnType<typeof gate<any>>|null=null,stateHold:ReturnType<typeof gate<FanState>>|null=null,starts=0,stops=0,maxLive=0;const live=new Set<number>(),trace:string[]=[];
 const read=host.getState.bind(host);host.getState=async()=>{const held=stateHold;if(held){stateHold=null;trace.push('state-held');held.entered();return held.promise;}return read();};
 const close=host.close.bind(host);host.close=async()=>{trace.push('close');return close();};
 const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
  readNativePowerState:async()=>{const held=nativeHold;if(held){nativeHold=null;trace.push('native-held');held.entered();return held.promise;}return native;},
  launcher:{async start(){assert.equal(live.size,0,'new Host before exact predecessor exit');const pid=116200+(++starts);live.add(pid);maxLive=Math.max(maxLive,live.size);trace.push('launch:'+pid);if(starts>1){host.stateName='AwaitingControl';host.closed=false;host.opened=host.events=false;host.lease=null;host.generation=0;host.attempt=0;host.powerState='Unknown';}return {pid,executable:'inert-fan.exe'};},async stop(process){assert(live.has(process.pid));live.delete(process.pid);stops++;trace.push('exact-exit:'+process.pid);}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);if(!cold){await life.start();await life.apply(curve(100));}
 return {host,life,trace,writes,native:()=>({...native}),setNative:(s:any)=>native=s,counts:()=>({starts,stops,maxLive}),holdNative(){const held=gate<any>();nativeHold=held;return held;},holdState(){const held=gate<FanState>();stateHold=held;return held;},async cleanup(){await life.close().catch(()=>{});}};
}
const results:any[]=[],observations:any[]=[];
async function check(name:string,work:()=>Promise<unknown>){if(process.env.YMCC_FAN_START_CANCEL_CASE&&!name.includes(process.env.YMCC_FAN_START_CANCEL_CASE))return;try{results.push({name,ok:true,evidence:await work()});}catch(error){results.push({name,ok:false,error:String(error)});}}
async function main(){
 for(const family of families)for(const boundary of ['native','state'])for(const action of ['off','close'])await check(family[0]+': '+action+' retires manual startup held at '+boundary,async()=>{
  const f=await fixture(family);f.host.stateName='FaultLocked';const native=boundary==='native'?f.holdNative():null,state=boundary==='state'?f.holdState():null;
  const old=outcomes(f.life.start({manualRecovery:true,reason:'held-manual-start'}));await bounded((native??state)!.ready);
  const end=outcomes(action==='off'?f.life.disable():f.life.close());const startCount=f.counts().starts;
  f.trace.push('cancel-'+action);native?.resolve(f.native());state?.resolve(f.host.snapshot());
  try{const result=await bounded(old);await bounded(end);observations.push({family:family[0],boundary,action,startBefore:startCount,...f.counts(),trace:f.trace.slice()});assert.equal(result.value,null,'cancelled old startup reported success');assert.match(String(result.error),/FAN_CONTROL_INTENT_SUPERSEDED:/);assert.equal(f.counts().starts,startCount,'old read caused a successor after cancellation');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
   const gate=await bounded(f.life.start({manualRecovery:true,reason:'fresh-on-after-cancellation'}));assert(gate.allowed);await bounded(f.life.apply(curve(65)));assert.equal(f.life.state,'ready');assert.equal(f.counts().maxLive,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);assert.deepEqual(f.writes.at(-1),curve(65),'first current manual adjustment was not submitted');return {counts:f.counts(),trace:f.trace.slice(),lastNodes:f.writes.at(-1)};
  }finally{native?.resolve(null);state?.resolve(f.host.snapshot());await old;await end;await f.cleanup();}
 });
 for(const action of ['off','close'])await check('cold manual startup queued before '+action+' performs zero launches',async()=>{
  const f=await fixture(families[0],true),held=gate<void>();const blocked=(f.life as any).enqueue(()=>held.promise);const old=outcomes(f.life.start({manualRecovery:true}));const end=outcomes(action==='off'?f.life.disable():f.life.close());held.resolve();
  try{await blocked;const result=await bounded(old);await bounded(end);assert.equal(result.value,null);assert.match(String(result.error),/FAN_CONTROL_INTENT_SUPERSEDED:/);assert.equal(f.counts().starts,0);assert.equal(f.life.hasControlIntent,false);assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(70));assert.equal(f.counts().starts,1);return f.counts();}finally{held.resolve();await old;await end;await f.cleanup();}
 });
 for(const family of families)await check(family[0]+': actual suspending refusal has structured code and later manual ON succeeds',async()=>{
  const f=await fixture(family);f.host.stateName='FaultLocked';f.setNative({...f.native(),phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
  try{const result=await outcomes(f.life.start({manualRecovery:true}));assert.equal(result.value,null);assert(result.error instanceof FanApiError);assert.equal(result.error.errorCode,'POWER_SUSPENDING');assert.equal(result.error.status,409);assert.match(result.error.message,/系统正在进入睡眠/);assert.equal(f.trace.filter(x=>x==='close').length,0);assert.equal(f.counts().starts,1);
   f.setNative({...f.native(),phase:'ready',resumeReady:true,hardwareWritesAllowed:true});f.life.observePowerBoundary('resume-ready',6);assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(80));assert.deepEqual(f.writes.at(-1),curve(80));assert.equal(f.life.state,'ready');assert.equal(f.counts().maxLive,1);return f.counts();
  }finally{await f.cleanup();}
 });
 for(const action of ['off','close'])await check(action+' prevents old native facts from changing current coordinator',async()=>{
  const control=await fixture();control.host.stateName='FaultLocked';let expectedPhase:string;
  try{await (action==='off'?control.life.disable():control.life.close()).catch(()=>{});expectedPhase=control.life.coordinatorSnapshot.phase;}finally{await control.cleanup();}
  const f=await fixture(),held=f.holdNative();f.host.stateName='FaultLocked';const old=outcomes(f.life.start({manualRecovery:true}));await bounded(held.ready);const end=outcomes(action==='off'?f.life.disable():f.life.close());
  held.resolve({...f.native(),phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});
  try{await bounded(old);await bounded(end);assert.equal(f.life.coordinatorSnapshot.phase,expectedPhase,'cancelled native observation installed an old sleep boundary');return f.counts();}finally{held.resolve(null);await old;await end;await f.cleanup();}
 });
 for(const boundary of ['native','state'])await check('new curve during '+boundary+' read keeps useful startup and latest manual curve',async()=>{
  const f=await fixture(),native=boundary==='native'?f.holdNative():null,state=boundary==='state'?f.holdState():null;const start=outcomes(f.life.start({manualRecovery:true}));await bounded((native??state)!.ready);f.life.stagePendingControlCurve(curve(55));native?.resolve(f.native());state?.resolve(f.host.snapshot());
  try{const result=await bounded(start);assert.equal(result.value?.allowed,true,'new curve was treated as OFF cancellation');await f.life.apply(curve(55));assert.equal(f.counts().starts,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);assert.deepEqual(f.writes.at(-1),curve(55));return f.counts();}finally{native?.resolve(null);state?.resolve(f.host.snapshot());await start;await f.cleanup();}
 });
 console.log(JSON.stringify({ok:results.every(r=>r.ok),checks:results.length,results,observations,scope:'real public lifecycle/start/disable/close/apply and native/state reads; inert adapter and exact-exit launcher; family identities exercise common protocol, not hardware proof; no CPU benchmark'}));if(results.some(r=>!r.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
