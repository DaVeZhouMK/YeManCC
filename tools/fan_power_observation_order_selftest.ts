import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type {PowerLifecycleState} from '../src/bridge/api';
const curve=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const awake=(generation=6):PowerLifecycleState=>({generation,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true});
function gate(){let resolve!:(s:PowerLifecycleState|null)=>void,entered!:()=>void;const ready=new Promise<void>(r=>entered=r),pending=new Promise<PowerLifecycleState|null>(r=>resolve=r);return {resolve,entered,ready,pending};}
async function setup(controlled=true){const host=new NoIoHost();let held:ReturnType<typeof gate>|null=null,native=awake();
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,resumeWaitDeadlineMs:800,readNativeActivity:async()=>null,readNativePowerState:async()=>{const wait=held;if(wait){held=null;wait.entered();return wait.pending;}return native;},launcher:{async start(){return{pid:101414,executable:'fixture-only.exe'};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});if(controlled)await life.apply(curve);
 return {life,host,setNative(s:PowerLifecycleState){native=s;},hold(){const wait=gate();held=wait;return wait;}};}
let checks=0;const failures:string[]=[];async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function main(){
 for(const oldPhase of ['suspending','suspended','resuming'] as const)await check('late native '+oldPhase+' cannot revoke the newer same-cycle wake edge',async()=>{
  const f=await setup();f.life.observePowerBoundary('suspending',6);const held=f.hold();
  const pending=f.life.requestRecoveryAfterObservedHostState('held-before-current-wake',{discardSuperseded:true});await held.ready;
  try{f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);f.setNative(awake());
   // Fixture publishes the Host worker's completed current-cycle rebuild.
   Object.assign(f.host,{generation:6,attempt:1,stateName:'Ready',powerState:'On',opened:true,events:true,lease:null,completeResume:true});const before=f.life.coordinatorSnapshot,calls=f.host.calls.length;
   held.resolve({...awake(),phase:oldPhase,resumeReady:false,hardwareWritesAllowed:false});await pending;
   assert.deepEqual(f.life.coordinatorSnapshot,before,'late same-generation power observation erased the newer native edge');assert.equal(f.life.recoveryActive,false,'retired observer registered another recovery owner');assert.equal(f.host.calls.length,calls,'retired observation touched Host');
   await f.life.start({manualRecovery:true});await f.life.apply(curve);assert.equal(f.life.state,'ready');
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });

 for(const generation of [6,7])await check('new real sleep '+generation+' retires an older ready observation without writes',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('before-new-sleep',{discardSuperseded:true});await held.ready;
  try{f.life.setPowerGeneration(generation);f.life.observePowerBoundary('suspending',generation);const before=f.life.coordinatorSnapshot,calls=f.host.calls.length;
   held.resolve(awake());assert.equal(await pending,null);assert.deepEqual(f.life.coordinatorSnapshot,before);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,null);assert.equal(f.host.calls.length,calls);assert.equal(f.life.recoveryActive,false);
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('identical wake notification does not retire a current observation',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('duplicate-wake',{discardSuperseded:true});await held.ready;
  try{f.life.observePowerBoundary('resume-ready',6);held.resolve(awake());assert.equal(await pending,true);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,6);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('current native wake without renderer event remains adoptable',async()=>{
  const f=await setup();f.life.observePowerBoundary('suspending',6);const held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('missed-wake',{discardSuperseded:true});await held.ready;
  try{held.resolve(awake());assert.equal(await pending,true);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,6);assert.equal(f.life.recoveryActive,true);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('current newer-generation native wake remains adoptable',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('new-wake',{discardSuperseded:true});await held.ready;
  try{held.resolve(awake(7));assert.equal(await pending,true);assert.equal(f.life.coordinatorSnapshot.powerGeneration,7);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,7);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const oldPhase of ['suspending','suspended','resuming'] as const)await check('actual manual startup ignores the pre-wake '+oldPhase+' read and still applies',async()=>{
  const f=await setup();f.life.observePowerBoundary('suspending',6);const held=f.hold(),pending=f.life.start({manualRecovery:true});await held.ready;
  try{f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);f.setNative(awake());
   Object.assign(f.host,{generation:6,attempt:1,stateName:'Ready',powerState:'On',opened:true,events:true,lease:null,completeResume:true});const calls=f.host.calls.length;
   held.resolve({...awake(),phase:oldPhase,resumeReady:false,hardwareWritesAllowed:false});assert.equal((await pending).allowed,true);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,6);
   assert(!f.host.calls.slice(calls).some(x=>['close','suspend','resume'].includes(x)),'obsolete observation caused a redundant lifecycle transaction');
   await f.life.apply(curve);assert.equal(f.life.state,'ready');assert(f.host.calls.includes('enable'));
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 for(const oldPhase of ['suspended','resuming'] as const)await check('actual cold-open admission survives a same-cycle wake during '+oldPhase+' read',async()=>{
  const f=await setup(false),held=f.hold(),pending=f.life.apply(curve);await held.ready;
  try{f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);f.setNative(awake());
   held.resolve({...awake(),phase:oldPhase,resumeReady:false,hardwareWritesAllowed:false});await pending;assert.equal(f.life.state,'ready');assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,6);
   assert.equal(f.host.calls.filter(x=>x==='open').length,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert(!f.host.calls.includes('resume'));
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('a second real edge while startup re-reads terminates safely and later manual remains reachable',async()=>{
  const f=await setup();f.life.observePowerBoundary('suspending',6);const held=f.hold(),pending=f.life.start({manualRecovery:true}).then(()=>null,e=>String(e));await held.ready;
  let second:ReturnType<typeof gate>|null=null;
  try{f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);second=f.hold();held.resolve({...awake(),phase:'suspended',resumeReady:false,hardwareWritesAllowed:false});await Promise.race([second.ready,pending.then(()=>{throw new Error('startup did not re-read the obsolete native observation');})]);
   f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);f.setNative({...awake(7),phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});const calls=f.host.calls.length;
   second.resolve(awake());await pending;assert.equal(f.life.coordinatorSnapshot.powerGeneration,7);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,null);assert(!f.host.calls.slice(calls).includes('enable'));
   f.setNative(awake(7));f.life.observePowerBoundary('resuming',7);f.life.observePowerBoundary('resume-ready',7);Object.assign(f.host,{generation:7,attempt:1,stateName:'Ready',powerState:'On',opened:true,events:true,lease:null,completeResume:true});
   await f.life.start({manualRecovery:true});await f.life.apply(curve);assert.equal(f.life.state,'ready');
  }finally{held.resolve(null);second?.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });

 await check('resume-ready alone retires an earlier same-generation resuming read',async()=>{
  const f=await setup();f.life.observePowerBoundary('suspending',6);f.life.observePowerBoundary('resuming',6);const held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('before-ready-only',{discardSuperseded:true});await held.ready;
  try{f.life.observePowerBoundary('resume-ready',6);const before=f.life.coordinatorSnapshot,calls=f.host.calls.length;held.resolve({...awake(),phase:'resuming',resumeReady:false,hardwareWritesAllowed:false});assert.equal(await pending,null);assert.deepEqual(f.life.coordinatorSnapshot,before);assert.equal(f.host.calls.length,calls);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('a newer accepted native wake snapshot also retires the older sleep reply',async()=>{
  const f=await setup();f.life.observePowerBoundary('suspending',6);const held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('old-sleep-query',{discardSuperseded:true});await held.ready;
  try{assert.equal(await f.life.requestRecoveryAfterObservedHostState('new-native-wake'),true);const before=f.life.coordinatorSnapshot,calls=f.host.calls.length;held.resolve({...awake(),phase:'suspended',resumeReady:false,hardwareWritesAllowed:false});assert.equal(await pending,null);assert.deepEqual(f.life.coordinatorSnapshot,before);assert.equal(f.host.calls.length,calls);assert.equal(f.life.recoveryActive,true);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('current suspended read still closes the write boundary and retains recovery',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('current-real-sleep',{discardSuperseded:true});await held.ready;const calls=f.host.calls.length;
  try{held.resolve({...awake(7),phase:'suspended',resumeReady:false,hardwareWritesAllowed:false});assert.equal(await pending,true);assert.equal(f.life.coordinatorSnapshot.powerGeneration,7);assert.equal(f.life.coordinatorSnapshot.wakeReadyGeneration,null);assert.equal(f.host.calls.length,calls);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,hardwareCalls:0,scope:'actual public observed-recovery + same-cycle native edges; held native snapshot; Host/process/power fixtures; no device/CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();
