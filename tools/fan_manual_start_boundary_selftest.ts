/** Public startup/terminal API regression. All device/native/launcher boundaries are inert. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, type FanNode, type FanPreset } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';
const curve=(duty:number)=>[0,40,70,100].map(tempC=>({tempC,dutyPercent:duty}));
const defer=()=>{let resolve!:()=>void;const promise=new Promise<void>(r=>resolve=r);return {promise,resolve};};
const delay=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
const outcome=<T>(promise:Promise<T>)=>promise.then(value=>({value,error:''}),error=>({value:null,error:String(error)}));
async function bounded<T>(promise:Promise<T>):Promise<T>{let timer:ReturnType<typeof setTimeout>|undefined;try{return await Promise.race([promise,new Promise<never>((_,reject)=>{timer=setTimeout(()=>reject(Error('fixture deadline; operation failed to settle')),2500);})]);}finally{clearTimeout(timer);}}
const families=[
 ['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],
 ['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],
 ['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],
 ['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],
 ['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve'],
] as const;
async function setup(family:typeof families[number]=families[0]){
 const host=new NoIoHost(),entered=defer(),release=defer();host.factoryType=family[1];
 let nativeGeneration=6,starts=0,stops=0,maxLive=0,hold=false,denyExit=false,denyAuth=false;const live=new Set<number>(),trace:string[]=[];
 const baseClose=host.close.bind(host);host.close=async()=>{trace.push('close-enter');if(hold){entered.resolve();await release.promise;hold=false;}const result=await baseClose();trace.push('close-return');return result;};
 const baseHandshake=host.handshake.bind(host);host.handshake=async()=>{if(denyAuth)throw new FanApiError('fixture authentication denied',401,'UNAUTHORIZED');return {...await baseHandshake(),fanRoute:family[2]};};
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
  readNativePowerState:async()=>({generation:nativeGeneration,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
  launcher:{async start(){assert.equal(live.size,0,'second Host before predecessor exact exit');starts++;const pid=104700+starts;live.add(pid);maxLive=Math.max(maxLive,live.size);trace.push('launch:'+pid);if(starts>1){host.closed=false;host.opened=host.events=false;host.lease=null;host.stateName='AwaitingControl';}return {pid,executable:'fixture-only.exe'};},async stop(process){if(denyExit)throw Error('fixture exact exit unconfirmed');stops++;live.delete(process.pid);trace.push('exact-exit:'+process.pid);}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start();await life.apply(curve(100));host.stateName='FaultLocked';hold=true;
 const old=outcome(life.start({manualRecovery:true,reason:'older-manual-recovery'}));await bounded(entered.promise);
 return {host,life,old,release,trace,starts:()=>starts,stops:()=>stops,maxLive:()=>maxLive,denyExit:(v:boolean)=>{denyExit=v;},denyAuth:(v:boolean)=>{denyAuth=v;},advancePower:()=>{nativeGeneration++;life.setPowerGeneration(nativeGeneration);life.observePowerBoundary('resume-ready',nativeGeneration);}};
}
async function cleanup(f:Awaited<ReturnType<typeof setup>>){f.release.resolve();f.denyExit(false);f.denyAuth(false);await f.old;await f.life.close().catch(()=>{});}
type AdmissionOptions={savedIdentity?:Record<string,unknown>;identity?:Record<string,unknown>;supported?:boolean;writeReady?:boolean};
function admissionFixture(family:typeof families[number],options:AdmissionOptions={}){
 const host=new NoIoHost();host.factoryType=family[1];
 const trace:string[]=[],writes:FanNode[][]=[],identities:Record<string,unknown>[]=[],live=new Set<number>();let starts=0,stops=0,maxLive=0,denyAuth=false,denyExit=false,denyClose=false;
 const baseSnapshot=host.snapshot.bind(host);host.snapshot=()=>({...baseSnapshot(),unknownState:host.stateName==='Unknown'});
 const baseHandshake=host.handshake.bind(host);host.handshake=async()=>{trace.push('handshake');if(denyAuth)throw new FanApiError('fixture authentication denied',401,'UNAUTHORIZED');return {...await baseHandshake(),fanRoute:family[2],supported:options.supported??true,fanRouteWriteReady:options.writeReady??true,...(options.identity?{deviceIdentity:options.identity}:{})};};
 const baseClose=host.close.bind(host);host.close=async()=>{trace.push('close-enter');const result=await baseClose();trace.push('close-return');return denyClose?{...result,hcVirtualCloseReturned:false,hcDeviceManagerStopCompleted:false,hcCloseCleanupPending:false,unknownState:false}:result;};
 const baseOpen=host.open.bind(host);host.open=async()=>{trace.push('open');return baseOpen();};const baseEvents=host.openEvents.bind(host);host.openEvents=async()=>{trace.push('events');return baseEvents();};
 const baseEnable=host.enable.bind(host);host.enable=async(nodes?:readonly FanNode[])=>{trace.push('enable');writes.push((nodes??[]).map(node=>({...node})));return baseEnable();};
 const basePreset=host.applyPreset.bind(host);host.applyPreset=async(_name?:FanPreset,_leaseId?:string,nodes?:readonly FanNode[])=>{trace.push('preset');writes.push((nodes??[]).map(node=>({...node})));return basePreset();};
 const life=new FanHostLifecycle({enabled:true,adapter:host,savedIdentity:options.savedIdentity,onDeviceIdentity:identity=>identities.push({...identity}),heartbeatIntervalMs:60000,readNativeActivity:async()=>null,requestManualWake:async()=>null,
  readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
  launcher:{async start(){assert.equal(live.size,0,'successor launched before predecessor exact exit');const pid=104900+(++starts);live.add(pid);maxLive=Math.max(maxLive,live.size);trace.push('launch:'+pid);if(starts>1){host.closed=false;host.opened=host.events=false;host.lease=null;host.stateName='AwaitingControl';}return {pid,executable:'fixture-only.exe'};},async stop(process){assert(live.has(process.pid),'exit must target the exact owned process');trace.push('exit-request:'+process.pid);if(denyExit)throw Error('fixture exact exit unconfirmed');live.delete(process.pid);stops++;trace.push('exact-exit:'+process.pid);}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);
 return {host,life,trace,writes,identities,starts:()=>starts,stops:()=>stops,maxLive:()=>maxLive,denyAuth:(value:boolean)=>{denyAuth=value;},denyExit:(value:boolean)=>{denyExit=value;},denyClose:(value:boolean)=>{denyClose=value;}};
}
async function closeAdmission(f:ReturnType<typeof admissionFixture>){f.denyAuth(false);f.denyExit(false);f.denyClose(false);await f.life.close().catch(()=>{});}
async function suspendedAdmission(remote:'FaultLocked'|'Unknown',intent=true){
 const f=admissionFixture(families[0]);try{await f.life.start();if(intent)await f.life.apply(curve(65));await f.life.suspend();assert.equal(f.life.state,'suspended');f.host.stateName=remote;f.host.powerState='On';f.trace.length=0;f.writes.length=0;f.host.calls.length=0;return f;}catch(error){await closeAdmission(f);throw error;}
}
function assertOrdered(trace:string[],expected:readonly string[]){let previous=-1;for(const event of expected){const index=trace.indexOf(event,previous+1);assert(index>previous,'missing ordered '+event+': '+trace.join(','));previous=index;}}
let checks=0;const failures:string[]=[],observations:unknown[]=[];
async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(error){failures.push(name+': '+String(error));console.log('FAIL '+name+': '+String(error));}}
async function main(){
 for(const family of families)for(const terminal of ['off','close','new-power'] as const)await check(family[0]+': first new manual ON after '+terminal+' survives old pending Close',async()=>{
  const f=await setup(family);let ended:Promise<unknown>=Promise.resolve();
  try{
   if(terminal==='off')ended=f.life.disable();else if(terminal==='close')ended=f.life.close();else f.advancePower();
   // A separate event turn while HC Close remains held, not just synchronous API calls.
   await delay(5);const a=outcome(f.life.start({manualRecovery:true,reason:'new-manual-on'})),b=outcome(f.life.start({manualRecovery:true,reason:'duplicate-new-on'}));
   f.release.resolve();const old=await bounded(f.old);await bounded(ended);const latest=await bounded(a),duplicate=await bounded(b);
   observations.push({family:family[0],terminal,oldError:old.error,latestError:latest.error,starts:f.starts(),stops:f.stops(),maxLive:f.maxLive(),trace:f.trace});
   assert.equal(old.value,null,'retired request revived');assert.equal(latest.value?.allowed,true,'new manual ON inherited cancelled start: '+latest.error);assert.equal(duplicate.value?.allowed,true,'duplicate new ON failed');
   assert.equal(f.starts(),2,'new callers must share one successor');assert.equal(f.maxLive(),1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1,'handshake must not replay the old curve');
   await f.life.apply(curve(65));assert.equal(f.host.calls.filter(x=>x==='enable').length,2,'first subsequent adjustment must be effective');
  }finally{f.release.resolve();await ended.catch(()=>{});await cleanup(f);}
 });
 await check('same boundary: duplicate manual ON shares original Close/start and handshake',async()=>{
  const f=await setup();try{const a=outcome(f.life.start({manualRecovery:true})),b=outcome(f.life.start({manualRecovery:true}));f.release.resolve();assert.equal((await bounded(f.old)).value?.allowed,true);assert.equal((await bounded(a)).value?.allowed,true);assert.equal((await bounded(b)).value?.allowed,true);assert.equal(f.starts(),2);assert.equal(f.host.calls.filter(x=>x==='handshake').length,2,'duplicate control added a handshake');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.equal(f.maxLive(),1);}finally{await cleanup(f);}
 });
 await check('OFF without new ON never launches a successor',async()=>{
  const f=await setup();const ended=f.life.disable();try{f.release.resolve();assert.equal((await bounded(f.old)).value,null);await bounded(ended);assert.equal(f.starts(),1);assert.equal(f.life.processId,null);assert.equal(f.life.hasControlIntent,false);}finally{await ended.catch(()=>{});await cleanup(f);}
 });
 await check('a second OFF cancels the queued replacement before it launches',async()=>{
  const f=await setup();const firstOff=f.life.disable(),latest=outcome(f.life.start({manualRecovery:true})),lastOff=f.life.disable();try{f.release.resolve();await bounded(f.old);await bounded(firstOff);await bounded(lastOff);assert.equal((await bounded(latest)).value,null);assert.equal(f.starts(),1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);}finally{await firstOff.catch(()=>{});await lastOff.catch(()=>{});await cleanup(f);}
 });
 await check('new manual request still rejects actual authentication failure',async()=>{
  const f=await setup();const ended=f.life.disable();f.denyAuth(true);const latest=outcome(f.life.start({manualRecovery:true}));try{f.release.resolve();await bounded(f.old);await bounded(ended);const result=await bounded(latest);assert.equal(result.value,null);assert.match(result.error,/UNAUTHORIZED|authentication denied/);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.equal(f.maxLive(),1);}finally{await ended.catch(()=>{});await cleanup(f);}
 });
 await check('unconfirmed exact exit never allows a second Host or a new write',async()=>{
  const f=await setup();f.denyExit(true);const ended=outcome(f.life.disable()),latest=outcome(f.life.start({manualRecovery:true}));try{f.release.resolve();await bounded(f.old);await bounded(ended);assert.equal((await bounded(latest)).value,null);assert.equal(f.starts(),1);assert.equal(f.maxLive(),1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.notEqual(f.life.processId,null);}finally{await cleanup(f);}
 });
 for(const family of [families[0],families[2]])for(const metadata of ['changed','missing'] as const)for(const intent of ['manual','authorized-auto'] as const)await check(family[0]+': '+intent+' first 100% write accepts '+metadata+' firmware metadata and refreshes saved identity',async()=>{
  const saved={manufacturer:family[0]==='GPD'?'GPD':'ASUSTEK COMPUTER INC.',model:family[0]==='GPD'?'G1618-05':'ROG Xbox Ally X RC73XA_RC73XA',product:family[0]==='GPD'?'G1618-05':'RC73XA',bios:'2.20',ecRevision:'1.00'};
  const identity:Record<string,unknown>={ManufacturerName:' '+saved.manufacturer.toLowerCase()+' ',SystemModel:saved.model,ProductName:saved.product,...(metadata==='changed'?{Version:'2.21',ECRevision:'1.01'}:{})};
  const f=admissionFixture(family,{savedIdentity:saved,identity});try{
   assert.equal(f.life.hasControlIntent,false);assert.equal(f.starts(),0);assert.equal(f.writes.length,0);
   // The automatic branch represents the caller's already-confirmed ON preference, using App.vue's public start/apply sequence.
   const gate=await bounded(f.life.start({manualRecovery:true,reason:intent==='manual'?'first-manual-on':'authorized-auto-boot'}));assert.equal(gate.allowed,true);assert.equal(gate.writeReady,true);assert.equal(f.writes.length,0,'startup alone must not replay a curve');
   assert.deepEqual(f.identities,[identity],'accepted identity must reach the existing persistence sink');await bounded(f.life.apply(curve(100)));
   assert.deepEqual(f.writes,[curve(100)],'the first authorized click must dispatch exactly the requested 100% curve');assert.equal(f.starts(),1);assert.equal(f.stops(),0);assert.equal(f.maxLive(),1);assert(!f.trace.includes('close-enter'),'firmware metadata alone must not require replacement');
   observations.push({family:family[0],intent,metadata,acceptedIdentity:f.identities[0],trace:[...f.trace]});
  }finally{await closeAdmission(f);}
 });
 for(const family of [families[0],families[2]])for(const refusal of ['manufacturer','model','product','unsupported','readonly'] as const)await check(family[0]+': first manual 100% write preserves '+refusal+' refusal',async()=>{
  const saved={manufacturer:family[0]==='GPD'?'GPD':'ASUSTEK COMPUTER INC.',model:family[0]==='GPD'?'G1618-05':'ROG Xbox Ally X RC73XA_RC73XA',product:family[0]==='GPD'?'G1618-05':'RC73XA',bios:'2.20',ecRevision:'1.00'};
  const identity:Record<string,unknown>={ManufacturerName:saved.manufacturer,SystemModel:saved.model,ProductName:saved.product,Version:saved.bios,EcRevision:saved.ecRevision};
  if(refusal==='manufacturer')identity.ManufacturerName='wrong manufacturer';if(refusal==='model')identity.SystemModel='wrong model';if(refusal==='product')identity.ProductName='wrong product';
  const f=admissionFixture(family,{savedIdentity:saved,identity,supported:refusal!=='unsupported',writeReady:refusal!=='readonly'});try{
   const started=await bounded(outcome(f.life.start({manualRecovery:true,reason:'first-manual-on'})));
   if(refusal==='readonly'){assert.equal(started.value?.allowed,true);assert.equal(started.value?.writeReady,false);}else{assert.equal(started.value,null);assert.match(started.error,/FAN_DEVICE_GATE_DENIED|设备门拒绝/);assert.equal(f.identities.length,0,'rejected identity must not replace saved identity');}
   const applied=await bounded(outcome(f.life.apply(curve(100))));assert.equal(applied.value,null,'refused route dispatched a control write');assert.equal(f.writes.length,0);if(refusal!=='readonly')assert(!f.host.calls.some(call=>['open','events','acquire'].includes(call)),'denied identity/support reached a hardware session');assert.equal(f.maxLive(),1);
  }finally{await closeAdmission(f);}
 });
 for(const remote of ['FaultLocked','Unknown'] as const)for(const action of ['apply','preset'] as const)await check('suspended local / '+remote+': first direct '+action+' replaces terminal Host before writing',async()=>{
  const f=await suspendedAdmission(remote);try{
   const result=await bounded(outcome(action==='apply'?f.life.apply(curve(100)):f.life.applyPreset('aggressive',curve(100))));
   observations.push({remote,action,error:result.error,starts:f.starts(),stops:f.stops(),trace:[...f.trace]});assert.notEqual(result.value,null,'first direct control inherited stale suspended/terminal mirror: '+result.error);
   assertOrdered(f.trace,['close-enter','close-return','exact-exit:104901','launch:104902','open','events',action==='apply'?'enable':'preset']);assert.equal(f.starts(),2);assert.equal(f.stops(),1);assert.equal(f.maxLive(),1);assert.deepEqual(f.writes,[curve(100)],'replacement must dispatch only the latest direct request');assert.equal(f.host.calls.filter(call=>call==='open').length,1);assert.equal(f.host.calls.filter(call=>call==='events').length,1);
  }finally{await closeAdmission(f);}
 });
 for(const refusal of ['authentication','exact-exit','Close'] as const)await check('suspended terminal direct control preserves '+refusal+' refusal',async()=>{
  const f=await suspendedAdmission('FaultLocked');try{
   if(refusal==='authentication')f.denyAuth(true);if(refusal==='exact-exit')f.denyExit(true);if(refusal==='Close')f.denyClose(true);
   const result=await bounded(outcome(f.life.apply(curve(100))));assert.equal(result.value,null);assert.match(result.error,refusal==='authentication'?/UNAUTHORIZED|authentication denied/:refusal==='exact-exit'?/exit|退出/:/HC 虚拟 Close 尚未确认返回/);assert.equal(f.writes.length,0);assert.equal(f.maxLive(),1);assert(!f.trace.includes('open')&&!f.trace.includes('events'),'unconfirmed replacement opened a new hardware session');if(refusal!=='authentication')assert.equal(f.starts(),1,'cleanup/exit refusal allowed a successor');
  }finally{await closeAdmission(f);}
 });
 await check('automatic resume without control intent does not replace a suspended terminal Host or write',async()=>{
  const f=await suspendedAdmission('FaultLocked',false);try{assert.equal(f.life.hasControlIntent,false);await bounded(f.life.resume());assert.equal(f.starts(),1);assert.equal(f.stops(),0);assert.equal(f.writes.length,0);assert.deepEqual(f.trace,[],'unrequested automatic resume touched the Host');}finally{await closeAdmission(f);}
 });
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,observations,hardwareCalls:0,scope:'actual public lifecycle start/disable/close/power/apply/preset/resume and saved identity sink; inert adapters and exact-exit launcher; family identities exercise shared logic, not vendor hardware proof; no CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
