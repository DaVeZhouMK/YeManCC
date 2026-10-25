/** Public page reads + current manual apply; no native transport, HC or device I/O. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState,type FanLease} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
const curve:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:75}));
function held<T>(){let resolve!:(v:T)=>void,reject!:(e:Error)=>void,enter!:()=>void;return{promise:new Promise<T>((r,j)=>{resolve=r;reject=j;}),ready:new Promise<void>(r=>enter=r),enter:()=>enter(),resolve:(v:T)=>resolve(v),reject:(e:Error)=>reject(e)};}
const outcome=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
async function fixture(family:typeof families[number]){
 const host=new NoIoHost();host.factoryType=family[1];host.generation=6;host.attempt=1;host.powerState='On';host.completeResume=true;
 const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,resumeWaitDeadlineMs:800,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true}),onControlDisplay:()=>{},launcher:{async start(){return{pid:118601,executable:'inert-page-host.exe',processCreated:'page-birth-A',adopted:true};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});await life.apply(curve);
 let reads=0,published=0,stateHold:ReturnType<typeof held<FanState>>|null=null;
 const read=host.getState.bind(host);host.getState=async()=>{reads++;if(stateHold){const current=stateHold;stateHold=null;current.enter();return current.promise;}return read();};
 const owner=life as unknown as {observeControlDisplay:(...args:any[])=>void;process:Record<string,unknown>};const display=owner.observeControlDisplay.bind(owner);owner.observeControlDisplay=(...args:any[])=>{published++;display(...args);};
 return{host,life,owner,counts:()=>({reads,published}),holdRead(){const pending=held<FanState>();stateHold=pending;return pending;},wake(){life.observePowerBoundary('resuming',6);life.observePowerBoundary('resume-ready',6);},async cleanup(){owner.observeControlDisplay=display;host.getState=read;await life.close().catch(()=>{});}};
}
const results:any[]=[];
async function check(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]){const f=await fixture(family);try{results.push({name,ok:true,evidence:await work(f)});}catch(error){results.push({name,ok:false,error:String(error),counts:f.counts(),calls:[...f.host.calls]});}finally{await f.cleanup();}}
for(const family of families)for(const boundary of ['same-generation-wake','same-pid-new-object'] as const)for(const shape of ['success','error'] as const)for(const discard of [false,true])await check(`${family[0]} ${discard?'page':'mandatory'} ${shape} crossing ${boundary}`,async f=>{
 const snapshot=f.host.snapshot(),error=new FanApiError('old page authentication refused',401,'AUTH_REJECTED'),wait=f.holdRead();
 const pending=outcome(discard?f.life.getState({discardSuperseded:true}):f.life.getState());await wait.ready;
 const process=f.owner.process;if(boundary==='same-generation-wake')f.wake();else f.owner.process={...process,processCreated:'page-birth-B'};
 const before=f.counts();
 try{if(shape==='error')wait.reject(error);else wait.resolve(snapshot);const reply=await pending;
  if(discard){assert.equal(reply.error,null,'retired page error escaped into the current view');assert.equal(reply.value,null,'retired page success was delivered as current');}
  else if(shape==='error')assert.equal(reply.error,error,'mandatory old error contract changed');else{assert.equal(reply.error,null);assert.equal(reply.value,snapshot,'mandatory old snapshot contract changed');}
  assert.equal(f.counts().published,before.published,'retired reply updated the current display observation');assert.equal(f.counts().reads,before.reads,'retired completion started a follow-up read');
  if(boundary==='same-pid-new-object')f.owner.process=process;
  await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2,'one current manual apply must remain reachable');
  return{boundary,shape,discard,identitySeamOnly:boundary==='same-pid-new-object',oldReplyNotPublished:true,currentManualWrites:1};
 }finally{if(boundary==='same-pid-new-object')f.owner.process=process;wait.resolve(snapshot);await pending;}
},family);
for(const family of families)for(const shape of ['success','error'] as const)for(const discard of [false,true])await check(`${family[0]} repeated resume-ready preserves current ${discard?'page':'mandatory'} ${shape}`,async f=>{
 const snapshot=f.host.snapshot(),error=new FanApiError('current permission refused',401,'AUTH_REJECTED'),wait=f.holdRead();const pending=outcome(discard?f.life.getState({discardSuperseded:true}):f.life.getState());await wait.ready;
 f.life.observePowerBoundary('resume-ready',6);const before=f.counts();if(shape==='error')wait.reject(error);else wait.resolve(snapshot);const reply=await pending;
 if(shape==='error')assert.equal(reply.error,error);else{assert.equal(reply.error,null);assert.equal(reply.value,snapshot);assert.equal(f.counts().published,before.published+1);}
 await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);return{duplicateWakeIsNotNewBoundary:true,shape,discard,currentManualWrites:1};
},family);
for(const family of families)for(const boundary of ['same-generation-wake','repeat-resume-ready'] as const)for(const discard of [false,true])await check(`${family[0]} queued ${discard?'page':'mandatory'} read across ${boundary}`,async f=>{
 const beat=held<FanLease>(),original=f.host.heartbeat.bind(f.host),lease={...f.life.currentLease!};f.host.heartbeat=async()=>{beat.enter();return beat.promise;};
 const renewal=outcome(f.life.heartbeat());await beat.ready;const pending=outcome(discard?f.life.getState({discardSuperseded:true}):f.life.getState());
 if(boundary==='same-generation-wake')f.wake();else f.life.observePowerBoundary('resume-ready',6);const before=f.counts();beat.resolve(lease);await renewal;const reply=await pending;
 const retired=discard&&boundary==='same-generation-wake';assert.equal(reply.error,null);if(retired)assert.equal(reply.value,null);else assert(reply.value,'valid or mandatory observation was dropped');
 assert.equal(f.counts().reads,before.reads+(retired?0:1),'queued retired page observation started an unnecessary GET');f.host.heartbeat=original;await f.life.apply(curve);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);
 return{boundary,discard,GETs:retired?0:1,currentManualWrites:1};
},family);
console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,cpuBenchmark:false,scope:'actual public getState/heartbeat/observePowerBoundary/apply; inert Host/native/process, same-PID process object is an identity seam, no OS replacement or CPU benchmark'}));
if(results.some(x=>!x.ok))process.exitCode=1;
