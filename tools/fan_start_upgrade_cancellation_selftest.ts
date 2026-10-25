import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { NoIoHost } from './fixtures/fan_no_io_host';
function gate(){let resolve!:()=>void,entered!:()=>void;const ready=new Promise<void>(r=>entered=r),pending=new Promise<void>(r=>resolve=r);return {resolve,entered,ready,pending};}
async function setup(){const host=new NoIoHost(),held=gate();let launches=0,stops=0,maxLive=0,startCalls=0;const livePids=new Set<number>();let liveProcess:{pid:number;executable:string}|null=null;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
 readNativePowerState:async()=>({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),
 launcher:{async start(){startCalls++;if(liveProcess)return {...liveProcess,adopted:true};launches++;const process={pid:101200+launches,executable:'fixture-only.exe'};liveProcess=process;livePids.add(process.pid);maxLive=Math.max(maxLive,livePids.size);if(launches===1){held.entered();await held.pending;}else{host.closed=false;host.stateName='AwaitingControl';host.opened=host.events=false;host.lease=null;}return process;},async stop(process:{pid:number}){stops++;livePids.delete(process.pid);if(liveProcess?.pid===process.pid)liveProcess=null;}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);const ordinary=life.start();await held.ready;return {host,life,held,ordinary,launches:()=>launches,stops:()=>stops,maxLive:()=>maxLive,startCalls:()=>startCalls};}
let checks=0;const observations:Array<Record<string,unknown>>=[];const failures:string[]=[];async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function main(){
 for(const action of ['off','close','new-power'] as const)await check('manual upgrade waiting for initial start retires after '+action,async()=>{
  const f=await setup(),upgrade=f.life.start({manualRecovery:true}),outcome=upgrade.then(v=>({allowed:v.allowed,error:''}),e=>({allowed:false,error:String(e)}));let terminal:Promise<unknown>=Promise.resolve();
  try{if(action==='off')terminal=f.life.disable();else if(action==='close')terminal=f.life.close();else{f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   f.held.resolve();if(action==='off'||action==='close')await assert.rejects(f.ordinary,/FAN_CONTROL_INTENT_SUPERSEDED:startup-read/);else await f.ordinary;await terminal;const result=await outcome;
   observations.push({scenario:'cancel-waiting-upgrade',action,allowed:result.allowed,launches:f.launches(),stops:f.stops(),state:f.life.state,pid:f.life.processId});
   assert.equal(result.allowed,false,'cancelled upgrade still reported an allowed startup');assert.equal(f.host.calls.filter(x=>x==='enable').length,0);
   assert.equal(f.launches(),1,'old manual upgrade launched a successor after cancellation');
   if(action==='close'){assert.equal(f.life.processId,null,'close left a later spawned Host');assert.equal(f.life.state,'stopped');}
  }finally{f.held.resolve();await f.ordinary.catch(()=>{});await upgrade.catch(()=>{});await terminal.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const action of ['off','close'] as const)await check('a newer manual start after '+action+' does not share the retired upgrade',async()=>{
  const f=await setup(),old=f.life.start({manualRecovery:true}),retired=old.then(v=>v.allowed,()=>false),terminal=action==='off'?f.life.disable():f.life.close();
  const latest=f.life.start({manualRecovery:true,reason:'latest-after-terminal'});
  try{f.held.resolve();await assert.rejects(f.ordinary,/FAN_CONTROL_INTENT_SUPERSEDED:startup-read/);await terminal;assert.equal(await retired,false);assert.equal((await latest).allowed,true);assert.equal(f.launches(),action==='close'?2:1,'only Close requires a fresh child; OFF may adopt the current exact owner');assert.equal(f.stops(),action==='close'?1:0,'a replacement must retire its predecessor; a live OFF owner remains resident');assert.equal(f.maxLive(),1,'new manual start must never overlap the old Host');assert.equal(f.life.hasControlIntent,false);assert(!f.host.calls.some(x=>x==='enable'||x==='preset'));observations.push({scenario:'latest-start-after-terminal',action,startCalls:f.startCalls(),launches:f.launches(),stops:f.stops(),maxLive:f.maxLive(),pid:f.life.processId});}
  finally{f.held.resolve();await f.ordinary.catch(()=>{});await old.catch(()=>{});await latest.catch(()=>{});await terminal.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const action of ['off','close','new-power'] as const)await check('already queued upgrade rechecks cancellation before start work: '+action,async()=>{
  const f=await setup(),firstStartup=(f.life as unknown as {intentRecoveryStartup:unknown}).intentRecoveryStartup;
  const old=f.life.start({manualRecovery:true}),outcome=old.then(v=>v.allowed,()=>false),held=gate();
  const blocker=(f.life as unknown as {enqueue(work:()=>Promise<void>):Promise<void>}).enqueue(async()=>{held.entered();await held.pending;});let terminal:Promise<unknown>=Promise.resolve();
  try{
   f.held.resolve();await held.ready;
   let queued:unknown;
   for(let i=0;i<100;i++){queued=(f.life as unknown as {intentRecoveryStartup:unknown}).intentRecoveryStartup;if(queued&&queued!==firstStartup)break;await Promise.resolve();}
   assert(queued&&queued!==firstStartup,'upgrade has not reached its own non-null queued startup boundary');
   if(action==='off')terminal=f.life.disable();else if(action==='close')terminal=f.life.close();else{f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   held.resolve();await blocker;await f.ordinary;await terminal;assert.equal(await outcome,false,'queued old upgrade borrowed new terminal intent');assert.equal(f.launches(),1);assert(!f.host.calls.includes('enable'));
   if(action==='close'){assert.equal(f.life.state,'stopped');assert.equal(f.life.processId,null);}
  }finally{f.held.resolve();held.resolve();await f.ordinary.catch(()=>{});await blocker.catch(()=>{});await old.catch(()=>{});await terminal.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const action of ['curve','preset'] as const)await check('latest '+action+' retires waiting upgrade but remains manually actionable',async()=>{
  const f=await setup(),old=f.life.start({manualRecovery:true}),retired=old.then(v=>v.allowed,()=>false),curve=[{tempC:0,dutyPercent:65},{tempC:40,dutyPercent:65},{tempC:70,dutyPercent:65},{tempC:100,dutyPercent:65}];
  const latest=action==='curve'?f.life.apply(curve):f.life.applyPreset('balanced',curve);
  try{f.held.resolve();await f.ordinary;await latest;assert.equal(await retired,false);assert.equal(f.host.calls.filter(x=>x==='enable'||x==='preset').length,1);assert.equal(f.launches(),1);assert.deepEqual((f.life as unknown as {desiredCurve:typeof curve}).desiredCurve,curve);}
  finally{f.held.resolve();await f.ordinary.catch(()=>{});await old.catch(()=>{});await latest.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('same current manual upgrade remains deduplicated and handshake-only',async()=>{
  const f=await setup(),a=f.life.start({manualRecovery:true}),b=f.life.start({manualRecovery:true});
  try{f.held.resolve();await f.ordinary;assert.equal((await a).allowed,true);assert.equal((await b).allowed,true);assert.equal(f.launches(),1);assert(!f.host.calls.includes('enable'));}
  finally{f.held.resolve();await f.ordinary.catch(()=>{});await a.catch(()=>{});await b.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,observations,hardwareCalls:0,scope:'actual public ordinary/manual start and terminal entries; held inert launcher, Host/native fixtures; three queue-entry cases insert an inert private queue blocker; no device/CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();
