/** Public startup/terminal API regression. All device/native/launcher boundaries are inert. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError } from '../src/bridge/fanApi';
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
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,observations,hardwareCalls:0,scope:'actual public lifecycle start/disable/close/power/apply; inert adapters and exact-exit launcher; family identities exercise shared logic, not vendor hardware proof; no CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
