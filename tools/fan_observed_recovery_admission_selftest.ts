/** Actual observation-to-recovery entry; native waits, Host and process are inert. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import type { PowerLifecycleState } from '../src/bridge/api';
import { NoIoHost } from './fixtures/fan_no_io_host';
const curve=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const awake=(generation=6):PowerLifecycleState=>({generation,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true});
function gate(){let resolve!:(value:PowerLifecycleState|null)=>void,entered!:()=>void;const ready=new Promise<void>(r=>{entered=r;});const pending=new Promise<PowerLifecycleState|null>(r=>{resolve=r;});return {resolve,entered,ready,pending};}
async function setup(controlled=true){
 const host=new NoIoHost();let next:ReturnType<typeof gate>|null=null;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,readNativeActivity:async()=>null,
  launcher:{async start(){return {pid:100008,executable:'fixture-only.exe'};},async stop(){}},
  readNativePowerState:async()=>{const held=next;if(held){next=null;held.entered();return held.pending;}return awake();}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});if(controlled)await life.apply(curve);
 return {life,host,hold(){const held=gate();next=held;return held;}};
}
const inspect=(life:FanHostLifecycle)=>life as unknown as {recoveryIntent:{generation:number;reason:string;manual:boolean}|null;recoveryState:string;recoveryAttempts:number;sessionGeneration:number;process:{pid:number}|null};
let checks=0;const failures:string[]=[];
async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function main(){
 for(const action of ['disable','close','new-curve','new-power','new-session','new-host','cancel-owner'] as const)for(const discard of [false,true])await check('held native observation retires after '+action+' (page discard='+discard+')',async()=>{
  const f=await setup(),held=f.hold();const pending=discard?f.life.requestRecoveryAfterObservedHostState('old-observation',{discardSuperseded:true}):f.life.requestRecoveryAfterObservedHostState('old-observation');await held.ready;
  try{
   if(action==='disable')await f.life.disable();else if(action==='close')await f.life.close();else if(action==='new-curve')await f.life.apply(curve.map(n=>({...n,dutyPercent:65})));
   else if(action==='new-power'){f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
   else if(action==='new-session')inspect(f.life).sessionGeneration++;
   else if(action==='new-host'){const current=inspect(f.life).process!;inspect(f.life).process={...current,pid:100009};}
   else (f.life as unknown as {cancelRecovery(reason:string):void}).cancelRecovery('fixture-cancel-current-owner');
   const calls=f.host.calls.length,state=f.life.state,generation=f.life.coordinatorSnapshot.powerGeneration;
   held.resolve(awake());assert.equal(await pending,discard?null:false,'obsolete observation registered a new recovery owner or lost retirement status');
   assert.equal(f.life.recoveryActive,false,'obsolete observation restarted recovery');assert.equal(f.host.calls.length,calls,'old native result caused hardware entry');
   assert.equal(f.life.state,state);assert.equal(f.life.coordinatorSnapshot.powerGeneration,generation,'obsolete result changed newer power state');
  }finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('current native observation can register its existing recovery responsibility',async()=>{
  const f=await setup(),held=f.hold();const pending=f.life.requestRecoveryAfterObservedHostState('current-observation');await held.ready;
  try{held.resolve(awake());assert.equal(await pending,true);assert.equal(f.life.recoveryActive,true);assert.equal(inspect(f.life).recoveryIntent?.reason,'current-observation');}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('two same-generation observations cannot overwrite an owner accepted during the wait',async()=>{
  const f=await setup(),a=f.hold(),first=f.life.requestRecoveryAfterObservedHostState('older-request');await a.ready;
  const b=f.hold(),second=f.life.requestRecoveryAfterObservedHostState('accepted-owner');await b.ready;
  try{b.resolve(awake());assert.equal(await second,true);const owner=inspect(f.life).recoveryIntent,attempts=inspect(f.life).recoveryAttempts,state=inspect(f.life).recoveryState;a.resolve(awake());assert.equal(await first,true);assert.equal(inspect(f.life).recoveryIntent,owner);assert.equal(owner?.reason,'accepted-owner','late request rewrote current owner reason');assert.equal(inspect(f.life).recoveryAttempts,attempts);assert.equal(inspect(f.life).recoveryState,state);}
  finally{a.resolve(null);b.resolve(null);await first.catch(()=>{});await second.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('native result older than current power generation cannot create an old recovery owner',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('old-native-generation');await held.ready;
  try{held.resolve(awake(5));assert.equal(await pending,false);assert.equal(f.life.recoveryActive,false);assert.equal(f.life.coordinatorSnapshot.powerGeneration,6);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('a current newer native wake is still adopted rather than treated as a stale request',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('new-real-wake');await held.ready;
  try{held.resolve(awake(7));assert.equal(await pending,true);assert.equal(f.life.coordinatorSnapshot.powerGeneration,7);assert.equal(inspect(f.life).recoveryIntent?.generation,7);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('actual current sleep retains recovery responsibility but issues no immediate hardware command',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('current-sleep');await held.ready;const count=f.host.calls.length;
  try{held.resolve({...awake(7),phase:'suspended',resumeReady:false,hardwareWritesAllowed:false});assert.equal(await pending,true);assert.equal(f.life.recoveryActive,true);assert.equal(f.host.calls.length,count);assert.equal(f.life.coordinatorSnapshot.powerGeneration,7);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('ordinary current awake state without a wake boundary cannot create recovery',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('ordinary-awake');await held.ready;
  try{held.resolve({...awake(),resumeReady:false});assert.equal(await pending,false);assert.equal(f.life.recoveryActive,false);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('current null native read does not become a successful recovery',async()=>{
  const f=await setup(),held=f.hold(),pending=f.life.requestRecoveryAfterObservedHostState('current-null');await held.ready;
  try{held.resolve(null);assert.equal(await pending,false);assert.equal(f.life.recoveryActive,false);}
  finally{held.resolve(null);await pending.catch(()=>{});await f.life.close().catch(()=>{});}
 });
 await check('an idle session without established control intent cannot request recovery',async()=>{
  const f=await setup(false);try{assert.equal(await f.life.requestRecoveryAfterObservedHostState('no-user-intent'),false);assert.equal(f.life.recoveryActive,false);}finally{await f.life.close().catch(()=>{});}
 });
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,hardwareCalls:0,scope:'actual public observation recovery ingress; held native read; inert Host/process; session/PID/cancel-token counterexamples inject identity only; no device or CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();