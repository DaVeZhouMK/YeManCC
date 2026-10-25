/** Real production timeout loops, virtual clocks and inert I/O only.
 * Date rollback/forward is an input fault, never a measured CPU benchmark.
 */
import assert from 'node:assert/strict';
import type {FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
import * as api from '../src/bridge/api';
let mono=0,wall=1_800_000_000_000,wallOrigin=wall,mode='stable',readyAt=Infinity,ceiling=40000;
let probes=0,terminates=0,shutdowns=0,closes=0,delays=0;
const PID=1063,EXE='C:\\inert\\YeManFanHost.exe',CREATED='133001111111111111';
let purpose='';
const stopped=():FanState=>({state:'Stopped',powerState:'On',hardwareWrites:false,hardwareWritesEnabled:false,hardwareWritesObserved:false,unknownState:false,openCalled:false,openEventsCalled:false,closeCalled:true,hcCloseCleanupPending:false,hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true,oemRestoreConfirmed:true});
const pending=():FanState=>({...stopped(),state:'AwaitingCloseCleanup',hcCloseCleanupPending:true,hcVirtualCloseReturned:false,hcDeviceManagerStopCompleted:false});
(api.http as any).request=async(url:string)=>{probes++;if(url.endsWith('/api/close')){closes++;return{status:200,body:JSON.stringify({ok:true,state:stopped()})};}if(url.endsWith('/api/shutdown')){shutdowns++;return{status:200,body:'{}'};}if(url.endsWith('/api/state'))return{status:200,body:JSON.stringify({ok:true,state:mono>=readyAt?stopped():pending()})};if(purpose==='legacy'&&mono>=readyAt)throw new Error('fixture listener exited');return{status:200,body:JSON.stringify(purpose==='ready'&&mono>=readyAt?{host:'YeManFanHost',protocolVersion:2}:{host:'not-ready',protocolVersion:0})};};
(api.proc as any).identity=async()=>{probes++;return{valid:true,pid:PID,path:EXE,processCreated:(purpose==='exact'||purpose==='legacy')&&mono>=readyAt?'133009999999999999':CREATED};};
(api.proc as any).terminateExact=async()=>{terminates++;return{ok:false,matched:true,exited:false,reason:'still-running'};};
(api.proc as any).findExact=async()=>{probes++;return{found:mono<readyAt,pid:PID};};
(api.shell as any).run=async()=>({exitCode:0,stdout:' TCP 127.0.0.1:8765 0.0.0.0:0 LISTENING '+PID,stderr:''});
(api.app as any).aiFanMockSession=async()=>{probes++;const sessionId='01234567-89ab-cdef-0123-456789abcdef',stateDirectory='C:\\inert\\ai-fan-sessions\\'+sessionId+'\\fan-host';return{schemaVersion:1,enabled:true,mode:'mock-handshake',sessionId,parentPid:1,stateDirectory,requestPath:stateDirectory+'\\ai-request.json',responsePath:stateDirectory+'\\ai-response.json',hostPid:mono>=readyAt?0:PID,hostCreationTime100ns:mono>=readyAt?'0':CREATED};};
const {FanHostLifecycle,NativeFanHostLauncher,resolveFanHostConfig}=await import('../src/bridge/fanHost');
const cases:{name:string,ok:boolean,error?:string,evidence?:unknown}[]=[];
const realTimeout=globalThis.setTimeout,originalDateNow=Date.now,perfDescriptor=Object.getOwnPropertyDescriptor(globalThis,'performance');
async function virtual(name:string,clock:string,budget:number,available:boolean,run:()=>Promise<unknown>){
 mono=0;wallOrigin=1_800_000_000_000;wall=wallOrigin;mode=clock;readyAt=available?Math.floor(budget/2):Infinity;ceiling=budget*2+1000;probes=terminates=shutdowns=closes=delays=0;
 Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>mono}});Date.now=()=>wall;
 (globalThis as any).setTimeout=(fn:()=>void,ms=0)=>{if(mono+ms>ceiling)throw new Error('VIRTUAL_DURATION_EXCEEDED');const handle={unref(){}};queueMicrotask(()=>{mono+=Number(ms);delays++;wall=mode==='backward'?wallOrigin-mono:mode==='forward'?wallOrigin+(mono?86_400_000:0)+mono:wallOrigin+mono;fn();});return handle;};
 let result:unknown,error:unknown;try{result=await run();}catch(e){error=e;}finally{globalThis.setTimeout=realTimeout;Date.now=originalDateNow;if(perfDescriptor)Object.defineProperty(globalThis,'performance',perfDescriptor);}
 const measuredWindow=name==='manual-after-close'?(result as {closeObservation?:{start:number;end:number}}|undefined)?.closeObservation:undefined;
 const observedDuration=measuredWindow?measuredWindow.end-measuredWindow.start:mono;
 const evidence={clock,budget,available,mono,observedDuration,measuredWindow,probes,delays,terminates,shutdowns,closes,error:error?String(error):null,result};
 try{
  assert(!String(error??'').includes('VIRTUAL_DURATION_EXCEEDED'),'wall clock extended the finite observer past its original budget');
  if(available){assert.equal(error,undefined,'ready/exit/cleanup arriving inside the original duration must not be prematurely abandoned');assert(mono>=readyAt&&mono<budget,'current proof must settle inside the original budget');}
  else{if(name==='manual-after-close')assert(measuredWindow&&Number.isFinite(measuredWindow.start)&&Number.isFinite(measuredWindow.end),'manual scenario must identify the actual Close observer window');assert(observedDuration>=budget&&observedDuration<=budget+250,'fixed duration must neither expire early nor extend under clock adjustment');if(['ready','exact','legacy','remote-cleanup','mock-stop','hc-cleanup'].includes(name))assert(error,'missing proof must remain a failure');if(name==='observe-close')assert.equal(result,null,'deadline without proof cannot manufacture a Close receipt');if(name==='exact'||name==='legacy'||name==='mock-stop')assert.equal(terminates,1,'bounded observation must reach original exact-exit confirmation exactly once');}
  if(name==='legacy'){assert.equal(closes,1);assert.equal(shutdowns,1,'loop may only observe; no repeated shutdown');}
  if(name==='manual-after-close')assert.equal(error,undefined,'queued manual must finish after the old bounded observer');
  cases.push({name:name+'/'+clock+'/'+(available?'proof':'deadline'),ok:true,evidence});
 }catch(e){cases.push({name:name+'/'+clock+'/'+(available?'proof':'deadline'),ok:false,error:String(e),evidence});}
}
for(const clock of ['stable','backward','forward'])for(const available of [true,false]){
 for(const name of ['ready','exact','legacy','remote-cleanup','mock-stop','stale-resident','observe-close','hc-cleanup']){
  purpose=name;const launcher:any=new NativeFanHostLauncher(),config=resolveFanHostConfig('C:\\inert');config.hostExecutable=EXE;
  const host=new NoIoHost();host.getState=async()=>{probes++;if(name==='stale-resident')throw new Error('fixture old listener closed');return mono>=readyAt?stopped():pending();};
  const life:any=new FanHostLifecycle({enabled:true,adapter:host,launcher:{start:async()=>{throw new Error('NO_REAL_LAUNCH');},stop:async()=>{throw new Error('NO_REAL_STOP');}},heartbeatIntervalMs:0});
  let budget=16000,run:()=>Promise<unknown>;
  if(name==='ready'){purpose='ready';budget=5000;run=()=>launcher.waitForReady(config);}
  else if(name==='exact'){purpose='exact';budget=4000;run=()=>launcher.confirmExactPreviousHostExit({pid:PID,processCreated:CREATED,path:EXE});}
  else if(name==='legacy'){purpose='legacy';budget=4000;run=()=>launcher.recoverLegacyUnauthenticatedHost(config);}
  else if(name==='remote-cleanup'){run=()=>launcher.waitForRemoteHcCloseCleanup(config);}
  else if(name==='mock-stop'){purpose='mock-stop';budget=11000;launcher.aiOwnedPids.add(PID);run=()=>launcher.stop({pid:PID,processCreated:CREATED,executable:EXE});}
  else if(name==='stale-resident'){budget=5000;life.process={pid:PID,executable:EXE};life.stateValue='ready';run=()=>life.assertReachableBeforeShortCircuit();}
  else if(name==='observe-close'){run=()=>life.observeHostCloseAfterFailure(new Error('fixture lost Close reply'));}
  else {run=()=>life.waitForHcCloseCleanup();}
  await virtual(name,clock,budget,available,run);
 }
}
for(const clock of ['stable','backward','forward'])await virtual('manual-after-close',clock,16000,false,async()=>{
 purpose='manual-after-close';const host=new NoIoHost();host.generation=3;host.attempt=1;host.powerState='On';host.stateName='Ready';host.opened=host.events=true;let starts=0,stops=0,closeAttempts=0;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:0,launcher:{start:async()=>{starts++;return{pid:PID,executable:EXE,adopted:true};},stop:async()=>{stops++;}},readNativePowerState:async()=>({generation:3,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:true,hibernateAvailable:true})});
 life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);const a=[{tempC:0,dutyPercent:20},{tempC:100,dutyPercent:90}],b=a.map(n=>({...n,dutyPercent:80}));await life.start();await life.apply(a);
 const close=host.close.bind(host);host.close=async()=>{closeAttempts++;throw new Error('fixture Close reply unavailable');};
 let closeObservation:{start:number;end:number}|undefined;
 const observe=(life as unknown as {observeHostCloseAfterFailure(error:unknown):Promise<FanState|null>}).observeHostCloseAfterFailure.bind(life);
 (life as unknown as {observeHostCloseAfterFailure(error:unknown):Promise<FanState|null>}).observeHostCloseAfterFailure=async(error:unknown)=>{const start=mono;try{return await observe(error);}finally{closeObservation={start,end:mono};}};
 let later:Promise<unknown>|undefined,manualScheduled=false;host.getState=async()=>{probes++;if(!manualScheduled){manualScheduled=true;later=life.apply(b);void later.catch(()=>{});}return mono<16000?{...pending(),hardwareWritesObserved:true}:host.snapshot();};
 try{await assert.rejects(life.close(),/fixture Close reply unavailable/);assert(later,'must actually admit new manual while Close waits');await later;assert(closeObservation,'the real Close observer must have run');assert.equal(mono,closeObservation.end,'current manual request must proceed when the old bounded observer settles');assert.equal(closeAttempts,1,'observer may not resend Close');assert.equal(starts,1,'old cleanup cannot create a second process');assert.equal(stops,0,'unproven Close cannot stop owned process');assert.equal(host.calls.filter(c=>c==='enable').length,2,'new manual must write once after original enable');assert.equal(life.hasControlIntent,true);return{manualAccepted:true,closeAttempts,starts,stops,closeObservation,manualCompletedAt:mono};}
 finally{host.close=close;await life.close().catch(()=>{});}
});
const result={ok:cases.every(c=>c.ok),checks:cases.length,cases,hardwareCalls:0,scope:'actual eight production wait helpers, virtual monotonic/wall clocks and timer durations; inert API/Host, no launch/kill/device/CPU benchmark'};console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
