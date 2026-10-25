/** Real public Fan lifecycle + inert Host/native boundaries; no device I/O. */
import assert from 'node:assert/strict';
import { FanHostLifecycle } from '../src/bridge/fanHost';
import { FanApiError, type FanNode, type FanState } from '../src/bridge/fanApi';
import { NoIoHost } from './fixtures/fan_no_io_host';
const CURVE:FanNode[]=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
class Clock {
 now=0; seq=0; timers=new Map<number,{due:number,period:number,fn:()=>void}>();
 add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return {id};}
 clear(h:any){this.timers.delete(h?.id);}
 async flush(){for(let i=0;i<120;i++)await Promise.resolve();}
 async advance(ms:number){const target=this.now+ms;for(let i=0;i<1000;i++){const next=[...this.timers.entries()].filter(([,v])=>v.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=next[1].due;if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=target;await this.flush();}
}
function hold<T>(){let resolve!:(value:T)=>void;const promise=new Promise<T>(r=>resolve=r);return {resolve,promise};}
class Host extends NoIoHost {
 route='ProfileCurve'; applied:FanNode[]|null=null; controlled=false; refuses=0; unknown:'commit'|'refuse'|null=null;
 snapshotErrors=0; snapshotError:Error|null=null; snapshotGeneration:number|null=null; held:ReturnType<typeof hold<FanState>>|null=null;
 async handshake(){return {...await super.handshake(),fanRoute:this.route};}
 snapshot(){return {...super.snapshot(),hardwareWritesEnabled:this.controlled && this.lease!==null,activeCurve:this.applied,
  lastControlWriteEvidence:this.controlled?'compat-route-callback-returned-no-scope':'not-written',
  oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'hc-callback-only-physical-unknown',
  ...(this.snapshotGeneration===null?{}:{resumePhaseGeneration:this.snapshotGeneration,powerOperationGeneration:this.snapshotGeneration})};}
 async enable(nodes:readonly FanNode[]=[]){
  this.calls.push('enable');
  if(this.refuses-->0)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');
  const unknown=this.unknown;this.unknown=null;
  if(unknown!=='refuse'){this.applied=nodes.map(n=>({...n}));this.controlled=true;this.stateName='Ready';}
  if(unknown)throw new FanApiError('reply lost',0,'FAN_HTTP_RESULT_UNKNOWN');
  return this.snapshot();
 }
 async applyPreset(_name?:string,_lease?:string,nodes?:readonly FanNode[]){this.calls.push('preset');return this.enable(nodes);}
 async getState(){this.calls.push('state');if(this.held){const held=this.held;this.held=null;return held.promise;}if(this.snapshotErrors-->0)throw this.snapshotError??new Error('HTTP request timed out');return this.snapshot();}
 async disable(){this.controlled=false;return super.disable();}
}
const results:any[]=[];
async function scenario(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<void>,model='MSI',generation=6,projection=false){
 if(process.env.YMCC_FAN_SELF_HEAL_CASE && !name.includes(process.env.YMCC_FAN_SELF_HEAL_CASE))return;
 const f=await fixture(model,generation,projection);try{await work(f);results.push({name,model,ok:true,calls:f.host.calls.slice(),observations:f.observations});}
 catch(e){results.push({name,model,ok:false,error:String(e),calls:f.host.calls.slice(),observations:f.observations});}
 finally{await f.cleanup();}
}
async function fixture(model:string,generation=6,projection=false){
 const clock=new Clock(),saved={setTimeout,clearTimeout,setInterval,clearInterval},performanceDescriptor=Object.getOwnPropertyDescriptor(globalThis,'performance')!;
 Object.assign(globalThis,{setTimeout:(fn:()=>void,ms:number)=>clock.add(fn,ms),clearTimeout:(h:any)=>clock.clear(h),
  setInterval:(fn:()=>void,ms:number)=>clock.add(fn,ms,ms),clearInterval:(h:any)=>clock.clear(h)});
 Object.defineProperty(globalThis,'performance',{value:{now:()=>clock.now},configurable:true});
 const host=new Host();const types:Record<string,string>={MSI:'HandheldCompanion.Devices.ClawA2VM',ROG:'HandheldCompanion.Devices.ROGAlly',GPD:'HandheldCompanion.Devices.GPDWin5',OneX:'HandheldCompanion.Devices.OneXPlayerX1Mini',Lenovo:'HandheldCompanion.Devices.Lenovo.LegionGo'};host.factoryType=types[model]??types.GPD;host.route=model==='GPD'||model==='OneX'?'GenericDuty':'ProfileCurve';host.generation=0;host.powerState='Unknown';
 let native:any={generation,phase:'ready',resumeReady:generation>0,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true};
 let displayHook:((active:boolean)=>void)|undefined,stateHook:((state:string)=>void)|undefined;
 let activity:any=null;let nativeReads=0;let nativeHold:ReturnType<typeof hold<any>>|null=null;const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:5000,readNativeActivity:async()=>activity,
   ...(projection?{onControlDisplay:(active:boolean)=>displayHook?.(active),onState:(state:string)=>stateHook?.(state)}:{}),
  readNativePowerState:async()=>{nativeReads++;const held=nativeHold;nativeHold=null;return held?held.promise:native;},launcher:{async start(){return {pid:50100,executable:'inert.exe'};},async stop(){}}});
 life.setPowerGeneration(generation);if(generation>0)life.observePowerBoundary('resume-ready',generation);await life.start({manualRecovery:true});
 return {clock,host,life,setActivity:(value:any)=>{activity=value;},setDisplayHook:(hook:typeof displayHook)=>{displayHook=hook;},setStateHook:(hook:typeof stateHook)=>{stateHook=hook;},observations:[] as unknown[],readCount:()=>nativeReads,setNative:(v:any)=>native=v,holdNative(){const held=hold<any>();nativeHold=held;return held;},
  async fail(){host.refuses=2;await assert.rejects(life.apply(CURVE));},
  async cleanup(){host.held?.resolve(host.snapshot());host.held=null;host.snapshotErrors=0;host.snapshotError=null;await life.close().catch(()=>{});Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',performanceDescriptor);}};
}
async function main(){
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo'])for(const stage of ['events','acquire']as const)await scenario(`FAN1291 actual preparation stage ${stage} resource refusal is not replayed`,async f=>{
  const code=stage==='events'?'HC_SESSION_ROUTE_LOST':'OEM_RESTORE_FAILED';let failed=false;
  const original=stage==='events'?f.host.openEvents.bind(f.host):f.host.acquireControl.bind(f.host);
  const reject=async()=>{if(failed)return original();failed=true;f.host.calls.push(stage);throw new FanApiError('inert exact preparation refusal',409,code);};
  if(stage==='events')f.host.openEvents=reject as typeof f.host.openEvents;else f.host.acquireControl=reject as typeof f.host.acquireControl;
  await assert.rejects(f.life.apply(CURVE),e=>e instanceof FanApiError&&e.errorCode===code);
  assert.equal(f.host.calls.filter(c=>c===stage).length,1);assert.equal(f.host.calls.filter(c=>c==='enable').length,0);
  assert(f.life.recoveryActive);await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);
  assert.equal(f.host.calls.filter(c=>c===stage).length,1,'uncertain preparation was submitted twice');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,0,'preparation failure authorized curve submission');
  await f.life.apply(CURVE);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
 },model,0);
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo'])for(const code of ['HC_OPEN_EVENTS_SUPERSEDED','HC_OPEN_EVENTS_POWER_SUPERSEDED'])await scenario(`OpenEvents admission ${code} ${model} is observed without replay`,async f=>{
  let refused=false;const original=f.host.openEvents.bind(f.host);
  f.host.openEvents=async()=>{if(refused)return original();refused=true;f.host.calls.push('events-admission-refusal');throw new FanApiError('obsolete event preparation',409,code,'/api/open-events');};
  await assert.rejects(f.life.apply(CURVE),e=>e instanceof FanApiError&&e.errorCode===code);
  assert.equal(f.host.calls.filter(c=>c==='events-admission-refusal').length,1);assert.equal(f.host.calls.filter(c=>c==='enable').length,0);
  assert(f.life.recoveryActive,'event-admission refusal must enter existing bounded observation');
  const eventCalls=f.host.calls.filter(c=>c==='events').length;await f.clock.advance(35000);
  assert(!f.life.recoveryActive&&f.life.recoveryNeedsAttention,'unconfirmed admission observation must settle into a bounded manual state');
  assert.equal(f.host.calls.filter(c=>c==='events').length,eventCalls,'obsolete initialization was silently resubmitted');
  assert.equal(f.host.calls.filter(c=>c==='enable').length,0,'obsolete initialization authorized a curve write');
  await f.life.apply(CURVE);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'current explicit manual intent remains reachable');
 },model,0);
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo'])for(const code of ['HC_SESSION_ROUTE_LOST','OEM_RESTORE_FAILED']){
  for(const outcome of ['confirmed','absent','unconfirmed','off']as const)await scenario(`FAN1291 OEM resource error ${code} ${outcome} is observed without immediate replay`,async f=>{
   const enable=f.host.enable.bind(f.host);let failed=false;
   f.host.enable=async(nodes:readonly FanNode[]=[])=>{
    if(failed)return enable(nodes);failed=true;f.host.calls.push('enable');
    // The exact 409 remains an unconfirmed resource boundary. Only a later
    // current software receipt may say the requested curve is already active.
    throw new FanApiError('inert resource lifecycle error',409,code);
   };
   await assert.rejects(f.life.apply(CURVE),e=>e instanceof FanApiError&&e.errorCode===code);
   assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'resource error caused an immediate duplicate control request');
   assert(f.life.recoveryActive,'resource error did not enter bounded recovery');
   if(outcome==='confirmed'){
    f.host.applied=CURVE.map(n=>({...n}));f.host.controlled=true;f.host.stateName='Ready';
    await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');
    assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'confirmation replayed the curve');
    assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);
   }else if(outcome==='absent'){
    f.host.snapshotErrors=99;const before=f.host.calls.filter(c=>c==='state').length;
    await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);
    assert.equal(f.host.calls.filter(c=>c==='state').length-before,10);
    const ended=f.host.calls.filter(c=>c==='state').length;await f.clock.advance(35000);assert.equal(f.host.calls.filter(c=>c==='state').length,ended,'exhausted error restarted itself');
    assert.equal(f.host.calls.filter(c=>c==='enable').length,1);f.host.snapshotErrors=0;
    await f.life.apply(CURVE);assert.equal(f.life.state,'ready');
   }else if(outcome==='unconfirmed'){
    await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);
    assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'unconfirmed resource error gained automatic replay permission');
    await f.life.apply(CURVE);assert.equal(f.life.state,'ready');
   }else{
    await f.life.disable();const writes=f.host.calls.filter(c=>c==='enable').length;
    await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(!f.life.hasControlIntent);
    assert.equal(f.host.calls.filter(c=>c==='enable').length,writes,'OFF was overridden by old recovery');
   }
   assert.equal(f.host.calls.filter(c=>c==='close').length,0,'missing OEM feedback started a second Close');
   f.observations.push({code,outcome,automaticWrites:0,physicalConfirmed:false});
  },model,0);
 }
 for (const model of ['MSI','ROG','GPD','OneX','Lenovo']) {
  await scenario('FAN1276 post-write lost confirmation recovers with no second enable',async f=>{
   await f.life.apply(CURVE);f.host.snapshotErrors=1;f.host.snapshotError=new FanApiError('inert OEM status unavailable',503,'OEM_STATUS_UNAVAILABLE');
   await assert.rejects(f.life.getState({discardSuperseded:true}));assert(f.life.recoveryActive,'completed curve lost its follow-up read without a recovery owner');
   const writes=f.host.calls.filter(x=>x==='enable').length;await f.clock.advance(1000);
   assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,writes);
   assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);assert.equal(f.host.calls.filter(x=>x==='close').length,0);
  },model,0);
  await scenario('FAN1276 missing feedback stops after one finite episode and manual remains reachable',async f=>{
   await f.life.apply(CURVE);f.host.snapshotErrors=99;await assert.rejects(f.life.getState({discardSuperseded:true}));
   assert(f.life.recoveryActive);const reads=f.host.calls.filter(x=>x==='state').length;
   await f.clock.advance(35000);assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert.equal(f.host.calls.filter(x=>x==='state').length-reads,10);
   const end=f.host.calls.filter(x=>x==='state').length;
   await assert.rejects(f.life.getState({discardSuperseded:true}));await f.clock.advance(35000);
   assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='state').length,end+1,'a failed display read reset the exhausted budget');
   assert.equal(f.host.calls.filter(x=>x==='enable').length,1);f.host.snapshotErrors=0;await f.life.apply(CURVE);assert.equal(f.life.state,'ready');
  },model);
  await scenario('FAN1276 read refusal never grants curve replay',async f=>{
   await f.life.apply(CURVE);f.host.controlled=false;f.host.stateName='AwaitingControl';
   f.host.snapshotErrors=1;f.host.snapshotError=new FanApiError('read admission busy',409,'HC_OPERATION_BUSY');
   await assert.rejects(f.life.getState({discardSuperseded:true}));assert(f.life.recoveryActive);
   await f.clock.advance(5000);assert.equal(f.host.calls.filter(x=>x==='enable').length,1,'read-only failure replayed a curve');
   f.host.controlled=true;f.host.stateName='Ready';await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
  },model);
  await scenario('FAN1276 a state read before any acknowledged curve cannot start control',async f=>{
   f.host.snapshotErrors=1;await assert.rejects(f.life.getState({discardSuperseded:true}));
   assert(!f.life.recoveryActive);await f.clock.advance(35000);assert.equal(f.host.calls.filter(x=>x==='enable').length,0);
  },model);
  await scenario('FAN1276 weak write evidence cannot be promoted by a failed read',async f=>{
   const snapshot=f.host.snapshot.bind(f.host);f.host.snapshot=()=>({...snapshot(),lastControlWriteEvidence:'unknown'});
   await f.life.apply(CURVE);f.host.snapshotErrors=1;await assert.rejects(f.life.getState({discardSuperseded:true}));
   assert(!f.life.recoveryActive);await f.clock.advance(35000);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
  },model);
  for(const status of [401,403]) await scenario('FAN1276 declared auth refusal stops automatic work '+status,async f=>{
   await f.life.apply(CURVE);f.host.snapshotErrors=1;f.host.snapshotError=new FanApiError('auth rejected',status,'AUTH_REJECTED');
   await assert.rejects(f.life.getState({discardSuperseded:true}));assert(!f.life.recoveryActive);
   const before=f.host.calls.length;await f.clock.advance(35000);assert.equal(f.host.calls.length,before);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
  },model);
  await scenario('FAN1276 OFF cancels a pending read-only repair',async f=>{
   await f.life.apply(CURVE);f.host.snapshotErrors=1;await assert.rejects(f.life.getState({discardSuperseded:true}));assert(f.life.recoveryActive);
   const gate=hold<FanState>();f.host.held=gate;await f.clock.advance(250);const old=f.host.snapshot();
   await f.life.disable();gate.resolve(old);await f.clock.flush();await f.clock.advance(35000);
   assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert(!f.life.hasControlIntent);
  },model);
  await scenario('FAN1276 superseded read failure cannot recover over a newer curve',async f=>{
   await f.life.apply(CURVE);let reject!:(error:Error)=>void;const original=f.host.getState.bind(f.host);
   f.host.getState=()=>new Promise((_resolve,r)=>{reject=r;});const pending=f.life.getState({discardSuperseded:true});await f.clock.flush();
   f.host.getState=original;const next=CURVE.map(n=>({...n,dutyPercent:80}));await f.life.apply(next);
   reject(new FanApiError('late reply lost',503,'OEM_STATUS_UNAVAILABLE'));assert.equal(await pending,null);assert(!f.life.recoveryActive);assert.deepEqual(f.host.applied,next);
  },model);
 }

 // Generation 0 is the initial awake session, not evidence that native power is ready.
 // Every self-heal still obtains a current ready/native-write grant before any retry.
 for(const model of ['MSI','ROG','GPD','OneX','Lenovo']){
  await scenario('cold session: known pre-write refusal heals the original request',async f=>{await f.fail();assert(f.life.recoveryActive,'cold session excluded from self-heal');await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.applied,CURVE);assert.equal(f.host.calls.filter(x=>x==='enable').length,3);assert.equal(f.host.calls.filter(x=>x==='close').length,0);},model,0);
  await scenario('cold session: committed write with lost receipt reconciles despite unknown OEM proof',async f=>{f.host.unknown='commit';await assert.rejects(f.life.apply(CURVE));assert(f.life.recoveryActive,'lost cold acknowledgement dropped the intent');await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);},model,0);
  await scenario('cold session: unconfirmed write never replays and manual retry remains reachable',async f=>{f.host.unknown='refuse';await assert.rejects(f.life.apply(CURVE));assert(f.life.recoveryActive,'cold unknown write was never observed');await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);await f.life.apply(CURVE);assert.equal(f.life.state,'ready');},model,0);
  await scenario('cold session: transient OEM status errors recover without a second click',async f=>{await f.fail();assert(f.life.recoveryActive);f.host.snapshotErrors=2;f.host.snapshotError=new FanApiError('inert OEM status unavailable',503,'OEM_STATUS_UNAVAILABLE');await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.applied,CURVE);assert.equal(f.host.calls.filter(x=>x==='enable').length,3);},model,0);
  await scenario('cold session: missing status ends with finite reads and manual control survives',async f=>{await f.fail();assert(f.life.recoveryActive);f.host.snapshotErrors=99;const begin=f.host.calls.length;await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.slice(begin).filter(x=>x==='state').length,10);const end=f.host.calls.length;await f.clock.advance(20000);assert.equal(f.host.calls.length,end);f.host.snapshotErrors=0;await f.life.apply(CURVE);assert.equal(f.life.state,'ready');},model,0);
  await scenario('cold session: absent native write permission never becomes an automatic write',async f=>{await f.fail();assert(f.life.recoveryActive);const begin=f.host.calls.filter(x=>x==='enable').length;f.setNative(null);await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,begin);f.setNative({generation:0,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true});await f.life.apply(CURVE);assert.equal(f.life.state,'ready');},model,0);
  await scenario('cold session: current authentication refusal parks recovery without replay',async f=>{await f.fail();assert(f.life.recoveryActive);f.host.snapshotErrors=1;f.host.snapshotError=new FanApiError('inert credential rejected',401,'AUTH_REJECTED');const before=f.host.calls.filter(x=>x==='enable').length;await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,before);const end=f.host.calls.length;await f.clock.advance(20000);assert.equal(f.host.calls.length,end);f.host.snapshotError=null;await f.life.apply(CURVE);assert.equal(f.life.state,'ready');},model,0);
  await scenario('cold session: OFF cancels a held recovery read before any new write',async f=>{await f.fail();assert(f.life.recoveryActive);const gate=hold<FanState>();f.host.held=gate;await f.clock.advance(250);const reply=f.host.snapshot();await f.life.disable();const before=f.host.calls.filter(x=>x==='enable').length;gate.resolve(reply);await f.clock.flush();await f.clock.advance(20000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,before);},model,0);
 }

 for(const model of ['MSI','ROG','GPD']){
  await scenario('explicit known refusal automatically resumes without a second click',async f=>{await f.fail();assert(f.life.recoveryActive);await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.applied,CURVE);assert.equal(f.host.calls.filter(x=>x==='enable').length,3);assert.equal(f.host.calls.filter(x=>x==='close').length,0);},model);
  await scenario('lost write acknowledgement reconciles without repeating the write or waiting for OEM physical proof',async f=>{f.host.unknown='commit';await assert.rejects(f.life.apply(CURVE));assert(f.life.recoveryActive);await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,1);assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);const calls=f.host.calls.length,reads=f.readCount();await f.clock.advance(1000);assert.equal(f.host.calls.length,calls);assert.equal(f.readCount(),reads);},model);
  await scenario('unknown write without confirming state never blindly replays and leaves manual control usable',async f=>{f.host.unknown='refuse';await assert.rejects(f.life.apply(CURVE));await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);f.host.unknown=null;await f.life.apply(CURVE);assert.equal(f.life.state,'ready');},model);
 }
 await scenario('transient status timeout does not consume permission to retry a known refused write',async f=>{await f.fail();f.host.snapshotErrors=1;await f.clock.advance(2000);assert.equal(f.life.state,'ready');assert.deepEqual(f.host.applied,CURVE);});
 await scenario('all status responses absent: finite probes, no write, manual entry remains usable',async f=>{await f.fail();f.host.snapshotErrors=99;const begin=f.host.calls.length;await f.clock.advance(35000);assert(!f.life.recoveryActive);const stateReads=f.host.calls.slice(begin).filter(x=>x==='state').length;assert.equal(stateReads,10);const end=f.host.calls.length;await f.clock.advance(20000);assert.equal(f.host.calls.length,end);f.host.snapshotErrors=0;await f.life.apply(CURVE);assert.equal(f.life.state,'ready');});
 for(const kind of ['disable','close','curve','sleep','same-generation-sleep','host','session'])await scenario('held recovery read is retired by '+kind,async f=>{
  await f.fail();assert(f.life.recoveryActive,'failure must retain a progressing recovery owner');const gate=hold<FanState>();f.host.held=gate;await f.clock.advance(250);const old=f.host.snapshot();
  if(kind==='disable')await f.life.disable();else if(kind==='close')await f.life.close();else if(kind==='curve')await f.life.apply(CURVE.map(n=>({...n,dutyPercent:75})));
  else if(kind==='sleep'||kind==='same-generation-sleep'){const generation=kind==='sleep'?7:6;f.setNative({generation,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(generation);f.life.observePowerBoundary('suspending',generation);}
  else if(kind==='host')(f.life as any).process={pid:50100,executable:'other-owner.exe'};else (f.life as any).sessionGeneration++;
  const enables=f.host.calls.filter(x=>x==='enable').length;gate.resolve(old);await f.clock.advance(1500);assert.equal(f.host.calls.filter(x=>x==='enable').length,enables);if(kind==='curve')assert.equal(f.host.applied?.[0].dutyPercent,75);
 });
 await scenario('new explicitly staged curve supersedes a held old read without reusing its captured nodes',async f=>{await f.fail();assert(f.life.recoveryActive,'failure must retain a progressing recovery owner');const gate=hold<FanState>();f.host.held=gate;await f.clock.advance(250);const old=f.host.snapshot();assert(f.life.stageRecoveryCurve(CURVE.map(n=>({...n,dutyPercent:65}))));gate.resolve(old);await f.clock.advance(2000);assert.equal(f.host.applied?.[0].dutyPercent,65);assert.equal(f.host.calls.filter(x=>x==='enable').length,3);});
 await scenario('stale generation snapshot never permits automatic write',async f=>{await f.fail();f.host.snapshotGeneration=5;await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);});
 await scenario('native readiness absent never permits automatic write',async f=>{await f.fail();f.setNative(null);await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);});
 await scenario('read reply beyond monotonic recovery deadline never permits write',async f=>{await f.fail();assert(f.life.recoveryActive,'failure must retain a progressing recovery owner');const gate=hold<FanState>();f.host.held=gate;await f.clock.advance(250);f.clock.now+=31000;gate.resolve(f.host.snapshot());await f.clock.flush();assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);});
 await scenario('hard authentication refusal stops automatically and is not reset by page observations',async f=>{await f.fail();f.host.snapshotErrors=99;f.host.snapshotError=new FanApiError('unauthorized',401,'AUTH_REJECTED');await f.clock.advance(1000);assert(!f.life.recoveryActive);const calls=f.host.calls.length;await f.life.requestRecoveryAfterObservedHostState('page');await f.clock.advance(10000);assert.equal(f.host.calls.length,calls);});
 await scenario('automatic retry loses its write result: switch to read-only reconciliation',async f=>{await f.fail();f.host.unknown='refuse';await f.clock.advance(35000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,3);});
 await scenario('write retry budget stops repeated explicit refusals before creating a new worker',async f=>{await f.fail();f.host.refuses=99;await f.clock.advance(10000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='enable').length,4);assert.equal(f.host.calls.filter(x=>x==='handshake').length,1);});
 await scenario('heartbeat and its immediate repair both fail: existing owner performs the deferred repair',async f=>{await f.life.apply(CURVE);f.host.heartbeat=async()=>{f.host.calls.push('heartbeat');f.host.lease=null;f.host.controlled=false;f.host.stateName='AwaitingControl';throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};f.host.refuses=1;await assert.rejects(f.life.heartbeat());assert(f.life.recoveryActive);await f.clock.advance(1000);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,3);});
 await scenario('unsupported device refusal does not start any automatic hardware recovery',async f=>{f.host.enable=async()=>{f.host.calls.push('enable');throw new FanApiError('not supported',409,'FAN_UNSUPPORTED');};await assert.rejects(f.life.apply(CURVE));assert(!f.life.recoveryActive);const enables=f.host.calls.filter(x=>x==='enable').length;await f.clock.advance(10000);assert.equal(f.host.calls.filter(x=>x==='enable').length,enables);});
 await scenario('preset request with a lost acknowledgement uses the same reconciliation owner',async f=>{f.host.unknown='commit';await assert.rejects(f.life.applyPreset('aggressive',CURVE));assert(f.life.recoveryActive);await f.clock.advance(1000);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(x=>x==='preset').length,1);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);});
 for(const weak of ['wrong-curve','missing-curve','weak-evidence','pending'])await scenario('lost acknowledgement cannot be confirmed by '+weak,async f=>{
  f.host.unknown='commit';await assert.rejects(f.life.apply(CURVE));const snapshot=f.host.snapshot.bind(f.host);f.host.snapshot=()=>({...snapshot(),...(weak==='wrong-curve'?{activeCurve:CURVE.map(n=>({...n,dutyPercent:20}))}:weak==='missing-curve'?{activeCurve:null}:weak==='weak-evidence'?{lastControlWriteEvidence:'unknown'}:{resumePhaseGeneration:6,powerOperationGeneration:6,powerOperationStatus:'executing'})});
  await f.clock.advance(35000);assert(f.life.recoveryNeedsAttention);assert.equal(f.host.calls.filter(x=>x==='enable').length,1);
 });
 await scenario('queued automatic write is cancelled by OFF before the queue admits it',async f=>{await f.fail();const gate=hold<void>();const blocked=(f.life as any).enqueue(()=>gate.promise);await f.clock.advance(250);const off=f.life.disable();gate.resolve();await blocked;await off;await f.clock.advance(1000);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);assert(!f.life.hasControlIntent);});
 await scenario('existing wake owner loses a write acknowledgement and hands off to bounded read-only reconciliation',async f=>{
  await f.life.apply(CURVE);f.setNative({generation:7,phase:'suspending',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);await f.host.suspend();await f.life.suspend();
  f.setNative({generation:7,phase:'ready',hardwareWritesAllowed:true,resumeReady:true});f.host.completeResume=true;await f.host.resume({generation:7});f.host.unknown='commit';f.life.observePowerBoundary('resuming',7);f.life.observePowerBoundary('resume-ready',7);await f.life.requestRecoveryAfterObservedHostState('wake-fixture');assert(f.life.recoveryActive);await f.clock.advance(3000);
  assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);
 });
 await scenario('direct wake request loses its write result and reconciles without another user click',async f=>{
  await f.life.apply(CURVE);f.setNative({generation:7,phase:'suspending',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);await f.host.suspend();await f.life.suspend();
  f.setNative({generation:7,phase:'ready',hardwareWritesAllowed:true,resumeReady:true});f.host.completeResume=true;await f.host.resume({generation:7});f.host.unknown='commit';f.life.observePowerBoundary('resuming',7);f.life.observePowerBoundary('resume-ready',7);await assert.rejects(f.life.resume());assert(f.life.recoveryActive);await f.clock.advance(3000);
  assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,2);
 });
 await scenario('native-only sleep discovery transfers to original lifecycle owner and recovers after a missed wake event',async f=>{
  await f.fail();await f.host.suspend();f.host.lease=null;f.host.completeResume=true;
  f.setNative({generation:7,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});await f.clock.advance(1000);assert(f.life.recoveryActive);assert.equal(f.life.coordinatorSnapshot.powerGeneration,7);assert.equal(f.host.calls.filter(x=>x==='enable').length,2);
  f.setNative({generation:7,phase:'ready',hardwareWritesAllowed:true,resumeReady:true});await f.clock.advance(5000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(x=>x==='enable').length,3);
 });
 await scenario('healthy heartbeat creates no unused recovery admission context',async f=>{
  await f.life.apply(CURVE);const life=f.life as any,admission=life.admissionContext.bind(life);let recoveryContextCalls=0;
  life.admissionContext=(action:string,...args:any[])=>{if(action==='heartbeat-self-heal')recoveryContextCalls++;return admission(action,...args);};
  const start=f.host.calls.length;for(let i=0;i<5;i++)await f.life.heartbeat();
  assert.equal(f.host.calls.slice(start).filter(x=>x==='heartbeat').length,5);assert.equal(f.host.calls.slice(start).filter(x=>x==='enable'||x==='state').length,0);
  assert.equal(recoveryContextCalls,0,'healthy maintenance needlessly constructed error-only admission objects');
 });
 await scenario('native reply after recovery deadline issues no extra Host request',async f=>{
  await f.fail();const held=f.holdNative();const start=f.host.calls.length;await f.clock.advance(250);f.clock.now+=31000;
  held.resolve({generation:6,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true});await f.clock.flush();
  assert(f.life.recoveryNeedsAttention);assert.equal(f.host.calls.slice(start).filter(x=>x==='state').length,0,'expired episode still submitted another Host request');
  assert.equal(f.host.calls.filter(x=>x==='enable').length,2);await f.life.apply(CURVE);assert.equal(f.life.state,'ready');
 });
 await scenario('heartbeat repair handoff retains the original process object even if the PID is reused',async f=>{
  await f.life.apply(CURVE);const held=hold<any>();const host=f.host;host.heartbeat=async()=>{host.calls.push('heartbeat');host.lease=null;host.controlled=false;host.stateName='AwaitingControl';throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
  host.enable=async()=>{host.calls.push('enable');await held.promise;throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};
  const heartbeat=f.life.heartbeat().catch(()=>undefined);await f.clock.flush();const life=f.life as any;life.process={...life.process};held.resolve(null);await heartbeat;
  assert(!f.life.recoveryActive,'old repair was incorrectly rebound to a replacement process with the same PID');
 });
 for (const model of ['MSI', 'ROG', 'GPD']) for (const stage of ['native', 'host'] as const) for (const mode of ['known-refusal', 'lost-write-receipt'] as const) {
    await scenario(`same-generation wake retires a successful ${stage} self-heal read (${mode})`, async f => {
      if (mode === 'known-refusal') await f.fail();
      else { f.host.unknown = 'commit'; await assert.rejects(f.life.apply(CURVE)); }
      f.host.generation = 6; f.host.attempt = 1; f.host.powerState = 'On';
      assert(f.life.recoveryActive);
      const oldSnapshot = f.host.snapshot();
      const stateReads = f.host.calls.filter(c => c === 'state').length;
      const writes = f.host.calls.filter(c => c === 'enable').length;
      const native = stage === 'native' ? f.holdNative() : null;
      const remote = stage === 'host' ? hold<FanState>() : null;
      if (remote) f.host.held = remote;
      await f.clock.advance(250);
      f.setNative({generation: 6, phase: 'resuming', resumeReady: false, hardwareWritesAllowed: false, inputReady: false, hibernateAvailable: true});
      f.life.observePowerBoundary('resuming', 6);
      const epoch = (f.life as any).heartbeatEpoch;
      assert.equal((f.life as any).lease, null);
      if (native) native.resolve({generation: 6, phase: 'ready', resumeReady: true, hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true});
      if (remote) remote.resolve(oldSnapshot);
      await f.clock.flush();
      f.observations.push({point: 'old-success-returned', stage, mode, lease: (f.life as any).lease,
        recoveryActive: f.life.recoveryActive, state: f.life.state,
        heartbeatActive: (f.life as any).heartbeatTimer !== null,
        stateReadsAfterHold: f.host.calls.filter(c => c === 'state').length - stateReads,
        enableAfterHold: f.host.calls.filter(c => c === 'enable').length - writes});
      assert.equal((f.life as any).heartbeatEpoch, epoch);
      assert.equal((f.life as any).lease, null, 'late successful read restored a pre-wake lease');
      assert.equal((f.life as any).heartbeatTimer, null, 'late successful read restarted pre-wake maintenance');
      assert(f.life.recoveryActive, 'late successful read incorrectly finished the new wake recovery');
      assert.equal(f.host.calls.filter(c => c === 'enable').length, writes, 'retired observation caused an extra curve write');
      assert.equal(f.host.calls.filter(c => c === 'state').length, stateReads + (stage === 'host' ? 1 : 0), 'stale native readiness issued another Host request');
      await f.clock.advance(500);
      f.observations.push({point: 'native-still-resuming', stage, mode,
        recoveryActive: f.life.recoveryActive, state: f.life.state,
        enableAfterHold: f.host.calls.filter(c => c === 'enable').length - writes});
      assert.equal(f.host.calls.filter(c => c === 'enable').length, writes, 'old readiness caused an early Enable while native is still resuming');
      assert(f.life.recoveryActive, 'handoff left no progressing lifecycle owner');
      f.setNative({generation: 6, phase: 'ready', resumeReady: true, hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true});
      f.host.completeResume = true;
      await f.host.resume({generation: 6});
      f.life.observePowerBoundary('resume-ready', 6);
      await f.clock.advance(3000);
      assert(!f.life.recoveryActive, 'confirmed current wake did not complete recovery');
      assert.equal(f.life.state, 'ready');
      const manualCurve = CURVE.map(node => ({...node, dutyPercent: 75}));
      await f.life.apply(manualCurve);
      assert.deepEqual(f.host.applied, manualCurve);
      f.observations.push({point: 'current-wake-and-manual-completed', stage, mode, manualOnce: true,
        recoveryActive: f.life.recoveryActive, state: f.life.state});
    }, model);
  }
  for (const stage of ['native', 'host'] as const) await scenario(`committed resumed notice preserves a valid successful ${stage} self-heal read`, async f => {
    f.host.unknown = 'commit'; await assert.rejects(f.life.apply(CURVE));
    f.host.generation = 6; f.host.attempt = 1; f.host.powerState = 'On';
    const snapshot = f.host.snapshot(), writes = f.host.calls.filter(c => c === 'enable').length;
    const native = stage === 'native' ? f.holdNative() : null;
    const remote = stage === 'host' ? hold<FanState>() : null;
    if (remote) f.host.held = remote;
    await f.clock.advance(250);
    const epoch = (f.life as any).heartbeatEpoch;
    f.life.observePowerBoundary('resumed', 6);
    if (native) native.resolve({generation: 6, phase: 'ready', resumeReady: true, hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true});
    if (remote) remote.resolve(snapshot);
    await f.clock.flush();
    assert.equal((f.life as any).heartbeatEpoch, epoch);
    f.observations.push({point:'committed-resumed-after-return', stage, recoveryActive:f.life.recoveryActive, state:f.life.state, epoch, currentEpoch:(f.life as any).heartbeatEpoch, token:(f.life as any).recoveryToken, heal:(f.life as any).recoveryIntent?.selfHeal, session:(f.life as any).sessionGeneration, readCount:f.readCount(), timerActive:(f.life as any).recoveryRetryTimer!==null});
    assert(!f.life.recoveryActive, 'unchanged epoch needlessly delayed current confirmation');
    assert.equal(f.life.state, 'ready');
    assert.notEqual((f.life as any).lease, null);
    assert.notEqual((f.life as any).heartbeatTimer, null);
    assert.equal(f.host.calls.filter(c => c === 'enable').length, writes, 'current confirmation replayed an already committed curve');
  });
  for (const model of ['MSI', 'ROG', 'GPD']) for (const ownerKind of ['self-heal', 'lifecycle'] as const) for (const edge of ['resuming', 'resuming-then-ready'] as const) {
    await scenario(`queued ${ownerKind} retires its admitted epoch after ${edge}`, async f => {
      if (ownerKind === 'self-heal') await f.fail();
      else {
        await f.life.apply(CURVE);
        f.host.lease = null; f.host.controlled = false; f.host.stateName = 'AwaitingControl';
      }
      f.host.generation = 6; f.host.attempt = 1; f.host.powerState = 'On'; f.host.completeResume = true;
      const owner = f.life as any, release = hold<void>(), originalEnqueue = owner.enqueue.bind(owner);
      let queued = 0;
      owner.enqueue = (work: () => Promise<unknown>) => { queued++; return originalEnqueue(work); };
      const blocked = owner.enqueue(() => release.promise);
      try {
        if (ownerKind === 'lifecycle') assert(await f.life.requestRecoveryAfterObservedHostState('queued-current-wake'));
        const writeBudget = owner.recoveryIntent?.selfHeal?.writesLeft;
        await f.clock.advance(250);
        assert(queued >= 2, 'test did not reach the automatic write queue');
        const index = f.host.calls.length;
        f.setNative({generation: 6, phase: 'resuming', resumeReady: false, hardwareWritesAllowed: false, inputReady: false, hibernateAvailable: true});
        f.life.observePowerBoundary('resuming', 6);
        if (edge === 'resuming-then-ready') {
          f.setNative({generation: 6, phase: 'ready', resumeReady: true, hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true});
          f.life.observePowerBoundary('resume-ready', 6);
        }
        release.resolve(); await blocked; await f.clock.flush();
        const afterRelease = f.host.calls.slice(index);
        f.observations.push({point:'old-queue-released', ownerKind, edge, afterRelease, lease:owner.lease,
          heartbeatActive:owner.heartbeatTimer !== null, recoveryActive:f.life.recoveryActive});
        assert.deepEqual(afterRelease, [], 'retired automatic queue entry still entered HC or lease operations');
        assert.equal(owner.lease, null, 'retired queue entry adopted a lease');
        assert.equal(owner.heartbeatTimer, null, 'retired queue entry restarted maintenance');
        assert(f.life.recoveryActive, 'retired queue entry lost the retained recovery intent');
        if (ownerKind === 'self-heal') assert.equal(owner.recoveryIntent?.selfHeal?.writesLeft, writeBudget, 'retired queue entry consumed a write opportunity without attempting a write');
        f.setNative({generation: 6, phase: 'ready', resumeReady: true, hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true});
        await f.host.resume({generation:6}); f.life.observePowerBoundary('resume-ready',6);
        await f.clock.advance(3000);
        assert(!f.life.recoveryActive, 'current wake did not continue after retiring queued work');
        assert.equal(f.life.state, 'ready');
        const manualCurve = CURVE.map(node => ({...node, dutyPercent:75}));
        await f.life.apply(manualCurve); assert.deepEqual(f.host.applied, manualCurve);
        f.observations.push({point:'current-owner-and-manual-completed', ownerKind, edge, manualOnce:true});
      } finally { release.resolve(); await blocked; owner.enqueue = originalEnqueue; }
    }, model);
  }
  for (const ownerKind of ['self-heal', 'lifecycle'] as const) await scenario(`unchanged queued ${ownerKind} writes once without another power edge`, async f => {
    if (ownerKind === 'self-heal') await f.fail();
    else { await f.life.apply(CURVE); f.host.lease=null; f.host.controlled=false; f.host.stateName='AwaitingControl'; }
    f.host.generation=6; f.host.attempt=1; f.host.powerState='On'; f.host.completeResume=true;
    const release=hold<void>(), blocked=(f.life as any).enqueue(()=>release.promise);
    try {
      if(ownerKind==='lifecycle') assert(await f.life.requestRecoveryAfterObservedHostState('queued-valid-wake'));
      await f.clock.advance(250); const writes=f.host.calls.filter(c=>c==='enable').length;
      release.resolve(); await blocked; await f.clock.flush();
      assert(!f.life.recoveryActive); assert.equal(f.life.state,'ready');
      assert.equal(f.host.calls.filter(c=>c==='enable').length,writes+1);
    } finally { release.resolve(); await blocked; }
  });
  for (const model of ['MSI', 'ROG', 'GPD', 'OneX', 'Lenovo']) await scenario('one immutable self-heal status snapshot resolves generation evidence once', async f => {
    await f.fail();
    const owner=f.life as any, resolve=owner.snapshotGenerationEvidence.bind(owner),read=f.host.getState.bind(f.host);
    const counts=new Map<FanState,number>(),snapshots:FanState[]=[];
    owner.snapshotGenerationEvidence=(remote:FanState)=>{counts.set(remote,(counts.get(remote)??0)+1);return resolve(remote);};
    f.host.getState=async()=>{const remote=await read();snapshots.push(remote);return remote;};
    try {
      await f.clock.advance(1000);
      f.observations.push({point:'generation-resolution-count',snapshots:snapshots.length,resolutions:snapshots.map(remote=>counts.get(remote)??0)});
      assert.equal(snapshots.length,1,'evidence reuse added an extra status query');
      assert.equal(counts.get(snapshots[0]),1,'self-heal parsed the same immutable generation receipt again');
      assert(!f.life.recoveryActive);assert.deepEqual(f.host.applied,CURVE);
      const state=f.host.calls.length,native=f.readCount();await f.clock.advance(1000);
      assert.equal(f.host.calls.length,state);assert.equal(f.readCount(),native);
      await f.life.apply(CURVE.map(node=>({...node,dutyPercent:75})));assert.equal(f.host.applied?.[0].dutyPercent,75);
    } finally {owner.snapshotGenerationEvidence=resolve;f.host.getState=read;}
  }, model);
  for (const invalid of ['stale','future','missing-open','explicit-no-control','cleanup'] as const) await scenario(`self-heal evidence reuse preserves ${invalid} rejection`, async f => {
    await f.fail();
    f.host.generation=6;f.host.attempt=1;f.host.powerState='On';
    if(invalid==='stale')f.host.snapshotGeneration=5;
    else if(invalid==='future')f.host.snapshotGeneration=7;
    else if(invalid==='missing-open')f.host.opened=f.host.events=false;
    const snapshot=f.host.snapshot.bind(f.host);
    if(invalid==='cleanup'||invalid==='explicit-no-control')f.host.snapshot=()=>({...snapshot(),...(invalid==='cleanup'?{hcCloseCleanupPending:true}:{controlAccepting:false})});
    try {
      const count=f.host.calls.filter(c=>c==='enable').length;await f.clock.advance(35000);
      assert.equal(f.host.calls.filter(c=>c==='enable').length,count,'evidence reuse changed write eligibility');assert(!f.life.recoveryActive);
    } finally {f.host.snapshot=snapshot;}
  });
  // FAN1177: real public lifecycle, synthetic OEM/server responses only.
  for(const model of ['MSI','ROG','GPD','OneX','Lenovo']) {
    await scenario('OEM busy admission defers once and heals without another click',async f=>{
      const enable=f.host.enable.bind(f.host);let busy=true;
      f.host.enable=async(nodes=[])=>{if(busy){busy=false;f.host.calls.push('enable');throw new FanApiError('not submitted',409,'HC_OPERATION_BUSY');}return enable(nodes);};
      await assert.rejects(f.life.apply(CURVE));assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'busy must not spin in the immediate retry');
      assert(f.life.recoveryActive,'busy must retain the existing recovery owner');await f.clock.advance(2000);
      assert.equal(f.host.calls.filter(c=>c==='enable').length,2);assert.deepEqual(f.host.applied,CURVE);assert(!f.life.recoveryActive);
      assert.equal(f.host.calls.filter(c=>c==='close').length,0);assert.equal(f.host.snapshot().oemPhysicalOwnershipConfirmed,false);
    },model);
    await scenario('OEM HOST_ERROR after commit reconciles without immediate duplicate or physical feedback',async f=>{
      const enable=f.host.enable.bind(f.host);let fail=true;
      const snapshot=f.host.snapshot.bind(f.host);f.host.snapshot=()=>({...snapshot(),oemPhysicalOwnershipConfirmed:undefined,oemOwnershipStatus:undefined});
      f.host.enable=async(nodes=[])=>{const response=await enable(nodes);if(fail){fail=false;throw new FanApiError('callback reply failed',500,'HOST_ERROR');}return response;};
      await assert.rejects(f.life.apply(CURVE));assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert(f.life.recoveryActive);
      await f.clock.advance(2000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'a server error is not evidence of an unexecuted write');
      await f.life.apply(CURVE.map(n=>({...n,dutyPercent:75})));assert.equal(f.host.applied?.[0].dutyPercent,75);
    },model);
    for(const [status,code] of [[500,'HOST_ERROR'],[504,'HC_OPERATION_TIMEOUT'],[409,'FAN_CURVE_WRITE_FAILED']] as const) {
      await scenario(`OEM ${code} without confirming control has bounded observation and reachable manual retry`,async f=>{
        const enable=f.host.enable.bind(f.host);let fail=true;
        f.host.enable=async(nodes=[])=>{if(fail){f.host.calls.push('enable');throw new FanApiError('no reliable OEM outcome',status,code);}return enable(nodes);};
        await assert.rejects(f.life.apply(CURVE));assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert(f.life.recoveryActive);
        const reads=f.host.calls.filter(c=>c==='state').length;await f.clock.advance(35000);
        assert(!f.life.recoveryActive);assert(f.life.recoveryNeedsAttention);assert(f.life.hasControlIntent);assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'unknown hardware outcome must not be blindly resubmitted');
        assert.equal(f.host.calls.filter(c=>c==='state').length-reads,10);const endReads=f.host.calls.filter(c=>c==='state').length;
        await f.clock.advance(20000);assert.equal(f.host.calls.filter(c=>c==='state').length,endReads,'finished episode must stop probing');
        fail=false;await f.life.apply(CURVE);assert.equal(f.host.calls.filter(c=>c==='enable').length,2);assert.equal(f.life.state,'ready');
      },model);
    }
  }
  await scenario('persistent OEM busy consumes only the original finite write budget',async f=>{
    const enable=f.host.enable.bind(f.host);let busy=true;
    f.host.enable=async(nodes=[])=>{if(busy){f.host.calls.push('enable');throw new FanApiError('not submitted',409,'HC_OPERATION_BUSY');}return enable(nodes);};
    await assert.rejects(f.life.apply(CURVE));await f.clock.advance(20000);assert(f.life.recoveryNeedsAttention);
    assert.equal(f.host.calls.filter(c=>c==='enable').length,3,'one user submission plus at most two deferred submissions');
    assert.equal(f.host.calls.filter(c=>c==='handshake').length,1);busy=false;await f.life.apply(CURVE);assert.equal(f.life.state,'ready');
  });
  await scenario('busy heartbeat retains its issued lease and never rewrites the curve',async f=>{
    await f.life.apply(CURVE);const lease=f.life.currentLease;const writes=f.host.calls.filter(c=>c==='enable').length,reads=f.host.calls.filter(c=>c==='state').length;
    const heartbeat=f.host.heartbeat.bind(f.host);f.host.heartbeat=async()=>{f.host.calls.push('heartbeat');throw new FanApiError('not submitted',409,'HC_OPERATION_BUSY');};
    await assert.rejects(f.life.heartbeat());assert.deepEqual(f.life.currentLease,lease);assert.equal(f.host.calls.filter(c=>c==='enable').length,writes);assert.equal(f.host.calls.filter(c=>c==='state').length,reads);
    f.host.heartbeat=heartbeat;await f.life.heartbeat();assert.equal(f.host.calls.filter(c=>c==='enable').length,writes);
  });
  await scenario('preset HOST_ERROR uses the same read-only reconciliation instead of duplicate preset',async f=>{
    const enable=f.host.enable.bind(f.host);let fail=true;
    f.host.enable=async(nodes=[])=>{const response=await enable(nodes);if(fail){fail=false;throw new FanApiError('callback reply failed',500,'HOST_ERROR');}return response;};
    await assert.rejects(f.life.applyPreset('aggressive',CURVE));await f.clock.advance(2000);assert(!f.life.recoveryActive);
    assert.equal(f.host.calls.filter(c=>c==='preset').length,1);assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
  });
  for(const edge of ['off','sleep'] as const)await scenario('deferred OEM busy is cancelled by '+edge,async f=>{
    f.host.enable=async()=>{f.host.calls.push('enable');throw new FanApiError('not submitted',409,'HC_OPERATION_BUSY');};
    await assert.rejects(f.life.apply(CURVE));assert(f.life.recoveryActive);const writes=f.host.calls.filter(c=>c==='enable').length;
    if(edge==='off')await f.life.disable();else{f.setNative({generation:7,phase:'suspended',hardwareWritesAllowed:false,resumeReady:false});f.life.setPowerGeneration(7);f.life.observePowerBoundary('suspending',7);}
    await f.clock.advance(2000);assert.equal(f.host.calls.filter(c=>c==='enable').length,writes);
  });
  for(const code of ['FAN_UNSUPPORTED','API_SESSION_REQUIRED','EXTERNAL_FAN_OWNER'])await scenario('server status never overrides explicit '+code,async f=>{
    f.host.enable=async()=>{f.host.calls.push('enable');throw new FanApiError('declared refusal',503,code);};
    await assert.rejects(f.life.apply(CURVE));assert(!f.life.recoveryActive);const calls=f.host.calls.length;await f.clock.advance(10000);assert.equal(f.host.calls.length,calls);
  });
  for(const [status,code] of [[502,undefined],[503,'HC_OPERATION_BUSY']] as const)await scenario(`uncertain server ${status} takes priority over a retry-looking code`,async f=>{
    const enable=f.host.enable.bind(f.host);let fail=true;
    f.host.enable=async(nodes=[])=>{if(fail){f.host.calls.push('enable');throw new FanApiError('unconfirmed',status,code);}return enable(nodes);};
    await assert.rejects(f.life.apply(CURVE));await f.clock.advance(35000);assert(f.life.recoveryNeedsAttention);assert.equal(f.host.calls.filter(c=>c==='enable').length,1);
    fail=false;await f.life.apply(CURVE);assert.equal(f.host.calls.filter(c=>c==='enable').length,2);
  });
  await scenario('read-only HOST_ERROR preserves a known refused write retry opportunity',async f=>{
    await f.fail();f.host.snapshotErrors=1;f.host.snapshotError=new FanApiError('status callback failed',500,'HOST_ERROR');
    await f.clock.advance(3000);assert(!f.life.recoveryActive);assert.deepEqual(f.host.applied,CURVE);assert.equal(f.host.calls.filter(c=>c==='enable').length,3);
  });
  await scenario('deferred write HOST_ERROR switches to observation instead of another write',async f=>{
    await f.fail();const enable=f.host.enable.bind(f.host);let fail=true;
    f.host.enable=async(nodes=[])=>{const response=await enable(nodes);if(fail){fail=false;throw new FanApiError('callback failed after dispatch',500,'HOST_ERROR');}return response;};
    await f.clock.advance(3000);assert(!f.life.recoveryActive);assert.equal(f.life.state,'ready');assert.equal(f.host.calls.filter(c=>c==='enable').length,3);
  });
  // FAN1226: user callbacks run inside readback reconciliation, too.
  for(const model of ['MSI','ROG','GPD','OneX','Lenovo'])for(const stage of ['display','state']as const)for(const edge of ['off','same-wake','new-generation','new-curve','ready-notice','page-read','projection-failure']as const){
   await scenario(`self-heal ${stage} callback ownership after ${edge}`,async f=>{
    // Real failed publication leaves its next delivery opportunity pending.
    f.setDisplayHook(()=>{throw Error('inert display delivery temporarily unavailable');});
    f.host.unknown='commit';await assert.rejects(f.life.apply(CURVE));assert(f.life.recoveryActive);
    f.host.generation=6;f.host.attempt=1;f.host.powerState='On';
    f.setDisplayHook(undefined);let fired=0,pending:Promise<unknown>|undefined,boundaryEpoch=-1;
    const owner=f.life as any,writes=f.host.calls.filter(c=>c==='enable').length;
    const hook=()=>{
     if(fired)return;fired++;
     if(edge==='off')pending=f.life.disable();
     else if(edge==='same-wake'||edge==='new-generation'){
      const generation=edge==='same-wake'?6:7;f.setNative({generation,phase:'resuming',resumeReady:false,hardwareWritesAllowed:false,inputReady:false,hibernateAvailable:true});
      f.life.setPowerGeneration(generation);f.life.observePowerBoundary('resuming',generation);boundaryEpoch=owner.heartbeatEpoch;
     }else if(edge==='new-curve')pending=f.life.apply(CURVE.map(n=>({...n,dutyPercent:75})));
     else if(edge==='ready-notice'){f.life.observePowerBoundary('resume-ready',6);f.life.observePowerBoundary('resumed',6);}
     else if(edge==='page-read')pending=f.life.getState({discardSuperseded:true});
     else throw Error('inert projection-only failure');
    };
    if(stage==='display')f.setDisplayHook(hook);else f.setStateHook(state=>{if(state==='ready')hook();});
    await f.clock.advance(250);await pending;
    assert.equal(fired,1,'self-heal never crossed its real '+stage+' subscriber');
    if(edge==='same-wake'||edge==='new-generation'){
     assert.equal(owner.heartbeatEpoch,boundaryEpoch);assert.equal(owner.heartbeatTimer,null,'retired readback restarted heartbeat in a newer sleep/wake');
     assert.notEqual(owner.recoveryState,'controlled','old readback settled a newer wake as controlled');if(edge==='same-wake')assert(f.life.recoveryActive,'same-wake lost its retained recovery');
     assert.equal(f.host.calls.filter(c=>c==='enable').length,writes,'retired readback replayed control');
    }else if(edge==='off'){
     assert(!f.life.hasControlIntent);assert.equal(owner.heartbeatTimer,null);assert.notEqual(f.life.state,'ready');
    }else if(edge==='new-curve'){
     assert.equal(f.host.applied?.[0].dutyPercent,75);assert.equal(f.life.state,'ready');
    }else{
     assert.equal(f.life.state,'ready');assert(!f.life.recoveryActive,'observation-only callback cancelled valid reconciliation');
     assert.equal(f.host.calls.filter(c=>c==='enable').length,writes,'already committed control was written again');
    }
    f.setDisplayHook(undefined);f.setStateHook(undefined);
    if(edge==='same-wake'||edge==='new-generation'){
     const generation=edge==='same-wake'?6:7;f.setNative({generation,phase:'ready',resumeReady:true,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true});
     f.host.completeResume=true;f.life.observePowerBoundary('resume-ready',generation);await f.host.resume({generation});await f.life.resumeAfterCommittedPowerWake(generation);
    }
    if(edge!=='new-curve')await f.life.apply(CURVE.map(n=>({...n,dutyPercent:80})));
    assert.equal(f.life.state,'ready');assert.equal(f.host.applied?.[0].dutyPercent,edge==='new-curve'?75:80);
    f.observations.push({stage,edge,oldWrites:writes,manualReachable:true});
   },model,6,true);
  }
  for(const edge of ['process','session']as const)await scenario(`self-heal display readback cannot bind an old write to a replacement ${edge}`,async f=>{
   f.setDisplayHook(()=>{throw Error('inert unavailable display');});f.host.unknown='commit';await assert.rejects(f.life.apply(CURVE));
   f.host.generation=6;f.host.attempt=1;f.host.powerState='On';const owner=f.life as any,receipt=owner.activeCurveReceipt;let fired=0;
   f.setDisplayHook(()=>{if(fired)return;fired++;if(edge==='process')owner.process={...owner.process};else owner.sessionGeneration++;});
   await f.clock.advance(250);assert.equal(fired,1);assert.equal(owner.activeCurveReceipt,receipt,'old write was certified for a replacement '+edge);
   assert(!f.life.recoveryActive,'obsolete self-heal owner kept a background loop');f.setDisplayHook(undefined);
   await f.life.apply(CURVE.map(n=>({...n,dutyPercent:80})));assert.equal(f.life.state,'ready');assert.equal(f.host.applied?.[0].dutyPercent,80);
  },'MSI',6,true);
  // FAN1227: state publication occurs before the actual enable/preset RPC.
  for(const model of ['MSI','ROG','GPD','OneX','Lenovo'])for(const operation of ['apply','preset-explicit','preset-host']as const)for(const edge of ['new-curve','off','process','session','ready-notice','page-read','projection-failure']as const){
   await scenario(`prewrite ${operation} state subscriber after ${edge}`,async f=>{
    const edited=CURVE.map(n=>({...n,dutyPercent:65})),latest=CURVE.map(n=>({...n,dutyPercent:45}));let fired=0,pending:Promise<unknown>|undefined;
    const owner=f.life as any,enable=f.host.enable.bind(f.host),preset=f.host.applyPreset.bind(f.host),submitted:number[]=[];
    f.host.enable=async(nodes=[])=>{submitted.push(nodes[0]?.dutyPercent??-1);return enable(nodes);};
    f.host.applyPreset=async(name,lease,nodes)=>preset(name,lease,nodes??edited);
    f.setStateHook(state=>{if(state!=='ready'||fired)return;fired++;
     if(edge==='new-curve')pending=f.life.apply(latest);
     else if(edge==='off')pending=f.life.disable();
     else if(edge==='process')owner.process={...owner.process};
     else if(edge==='session')owner.sessionGeneration++;
     else if(edge==='ready-notice'){f.life.observePowerBoundary('resume-ready',6);f.life.observePowerBoundary('resumed',6);}
     else if(edge==='page-read')pending=f.life.getState({discardSuperseded:true});
     else throw Error('inert state projection failure before dispatch');
    });
    const result=await(operation==='apply'?f.life.apply(edited):operation==='preset-explicit'?f.life.applyPreset('soft',edited):f.life.applyPreset('soft')).then(value=>({value,error:null as unknown}),error=>({value:null,error}));
    await pending;assert.equal(fired,1,'real Ready publication before dispatch was not exercised');
    const retired=['new-curve','off','process','session'].includes(edge);
    if(retired){assert(result.error,'retired request was returned as successful');assert(!submitted.includes(65),'superseded curve reached the hardware RPC');}
    else{assert.equal(result.error,null,'ordinary state observer blocked manual control');assert.deepEqual(submitted,[65],'current request was duplicated or skipped');assert.equal(f.life.state,'ready');}
    if(edge==='new-curve')assert.equal(f.host.applied?.[0].dutyPercent,45);
    if(edge==='off')assert(!f.life.hasControlIntent);
    f.setStateHook(undefined);await f.life.apply(CURVE.map(n=>({...n,dutyPercent:80})));assert.equal(f.life.state,'ready');assert.equal(f.host.applied?.[0].dutyPercent,80);
    f.observations.push({operation,edge,retired,submitted,manualReachable:true});
   },model,6,true);
  }
  for(const operation of ['apply','preset-explicit','preset-host']as const)for(const edge of ['resuming','resuming-ready']as const)await scenario(`prewrite ${operation} same-wake remains an unsubmitted recoverable request (${edge})`,async f=>{
   const edited=CURVE.map(n=>({...n,dutyPercent:65}));let fired=0;
   const preset=f.host.applyPreset.bind(f.host);f.host.applyPreset=async(name,lease,nodes)=>preset(name,lease,nodes??edited);
   f.host.completeResume=true;f.host.generation=6;f.host.attempt=1;f.host.powerState='On';
   const native=(ready:boolean)=>({generation:6,phase:ready?'ready':'resuming',resumeReady:ready,hardwareWritesAllowed:ready,inputReady:ready,hibernateAvailable:true});
   f.setStateHook(state=>{if(state!=='ready'||fired)return;fired++;f.setNative(native(false));f.life.observePowerBoundary('resuming',6);
    if(edge==='resuming-ready'){f.setNative(native(true));void f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);}
   });
   const request=(operation==='apply'?f.life.apply(edited):operation==='preset-explicit'?f.life.applyPreset('soft',edited):f.life.applyPreset('soft')).then(value=>({value,error:null as unknown}),error=>({value:null,error}));
   await f.clock.flush();assert.equal(fired,1);
   if(edge==='resuming'){assert.equal(f.host.calls.filter(c=>c==='enable').length,0,'a prewrite wake submitted control before Ready');f.setNative(native(true));await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);}
   await f.clock.advance(5000);const result=await request;
   f.observations.push({point:'after-prewrite-wake',error:result.error instanceof Error?result.error.message:result.error,state:f.life.state,recovery:(f.life as any).recoveryState,hasIntent:f.life.hasControlIntent,desired:(f.life as any).desiredCurve});
   if(result.error)assert(!(result.error instanceof FanApiError)||result.error.errorCode!=='FAN_HTTP_RESULT_UNKNOWN','unsubmitted curve was treated as an uncertain committed write');
   assert.equal(f.life.state,'ready','one request did not continue after current wake became ready');assert(!f.life.recoveryActive);assert.equal(f.host.applied?.[0].dutyPercent,65);
   assert.equal(f.host.calls.filter(c=>c==='enable').length,1,'prewrite recovery duplicated or lost the curve');f.setStateHook(undefined);
   f.observations.push({operation,edge,oneRequest:true,physicalFeedbackRequired:false});
  },'MSI',6,true);
  for(const operation of ['apply','preset-host']as const)for(const edge of ['off','new-curve','deadline']as const)await scenario(`prewrite native wait ${operation} retires on ${edge}`,async f=>{
   const edited=CURVE.map(n=>({...n,dutyPercent:65})),latest=CURVE.map(n=>({...n,dutyPercent:45}));let fired=0;
   const preset=f.host.applyPreset.bind(f.host);f.host.applyPreset=async(name,lease,nodes)=>preset(name,lease,nodes??edited);
   const native=(ready:boolean)=>({generation:6,phase:ready?'ready':'resuming',resumeReady:ready,hardwareWritesAllowed:ready,inputReady:ready,hibernateAvailable:true});
   f.host.completeResume=true;f.host.generation=6;f.host.attempt=1;f.host.powerState='On';
   f.setStateHook(state=>{if(state!=='ready'||fired)return;fired++;f.setNative(native(false));f.life.observePowerBoundary('resuming',6);});
   const request=(operation==='apply'?f.life.apply(edited):f.life.applyPreset('soft')).then(value=>({value,error:null as unknown}),error=>({value:null,error}));
   await f.clock.flush();assert.equal(fired,1);assert.equal(f.host.calls.filter(c=>c==='enable').length,0);f.setStateHook(undefined);
   let newer:Promise<unknown>|undefined;
   if(edge==='off')newer=f.life.disable();
   else if(edge==='new-curve'){f.setNative(native(true));await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);newer=f.life.apply(latest);}
   await f.clock.advance(edge==='deadline'?20000:5000);const result=await request;await newer;
   assert(result.error,'retired native wait unexpectedly reported success');assert.notEqual(f.host.applied?.[0].dutyPercent,65,'retired wait submitted its old curve');
   if(edge==='off')assert(!f.life.hasControlIntent);
   if(edge==='new-curve')assert.equal(f.host.applied?.[0].dutyPercent,45);
   if(edge==='deadline'){assert(String(result.error).includes('FAN_MANUAL_NATIVE_WAIT_DEADLINE'),'wait ended for a different reason than its finite budget');const reads=f.readCount();await f.clock.advance(20000);assert.equal(f.readCount(),reads,'expired native wait kept polling');}
   f.setNative(native(true));await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);await f.life.apply(CURVE.map(n=>({...n,dutyPercent:80})));
   assert.equal(f.life.state,'ready');assert.equal(f.host.applied?.[0].dutyPercent,80);
   f.observations.push({operation,edge,manualReachable:true,retiredRequestNotWritten:true});
  },'MSI',6,true);
  // FAN1229: all automatic consumers must inspect an awaited write's owner.
  for(const model of ['MSI','ROG','GPD','OneX','Lenovo'])for(const consumer of ['self-heal','lifecycle','heartbeat','replay','crash']as const)for(const edge of ['none','same-wake','process','session','off','new-curve']as const){
   await scenario(`automatic ${consumer} completion after queued ${edge}`,async f=>{
    const edited=CURVE.map(n=>({...n,dutyPercent:65})),latest=CURVE.map(n=>({...n,dutyPercent:80})),owner=f.life as any;
    const follow=(p:Promise<unknown>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
    let fired=0,pending:Promise<{value:unknown;error:unknown}>|undefined,request:Promise<{value:unknown;error:unknown}>|undefined;
    if(consumer==='self-heal'){await f.fail();assert(f.life.stageRecoveryCurve(edited));}
    else if(consumer==='lifecycle'){await f.life.apply(CURVE);assert(await f.life.requestRecoveryAfterObservedHostState('completion-consumer'));assert(f.life.stageRecoveryCurve(edited));}
    else if(consumer==='heartbeat'){await f.life.apply(CURVE);f.host.lease=null;f.host.heartbeat=async()=>{f.host.calls.push('heartbeat');throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');};}
    else if(consumer==='replay'){
     await f.life.apply(CURVE);await f.life.suspend();f.host.controlled=false;f.host.lease=null;f.host.completeResume=true;await f.host.resume({generation:6});f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);
    }else f.setActivity({recoverable:true,active:false,revision:1,curve:edited});
    f.host.generation=6;f.host.attempt=1;f.host.powerState='On';f.host.completeResume=true;
    const beforeWrites=f.host.calls.filter(c=>c==='enable').length;
    f.setDisplayHook(active=>{if(!active||fired||f.host.calls.filter(c=>c==='enable').length<=beforeWrites)return;fired++;queueMicrotask(()=>{
     if(edge==='same-wake'){f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.life.observePowerBoundary('resume-ready',6);}
     else if(edge==='process')owner.process={...owner.process};else if(edge==='session')owner.sessionGeneration++;
     else if(edge==='off')pending=follow(f.life.disable());else if(edge==='new-curve')pending=follow(f.life.apply(latest));
    });});
    if(consumer==='heartbeat')request=follow(f.life.heartbeat());else if(consumer==='replay')request=follow(f.life.resume());else if(consumer==='crash')request=follow(f.life.recoverAfterUncleanExit());
    await f.clock.advance(250);const result=request?await request:undefined;await pending;assert.equal(fired,1,'automatic consumer never crossed its real successful write display');
    if(edge==='none'){
     if(consumer==='self-heal'||consumer==='lifecycle')assert(!f.life.recoveryActive,'healthy automatic control did not settle');
     if(consumer==='crash')assert.equal(result?.value,true);else if(result)assert.equal(result.error,null);
    }else{
     if(consumer==='self-heal'||consumer==='lifecycle')assert(owner.completedRecoveryWrite===null,'old automatic write cached a completion for the new owner');
     if(consumer==='crash')assert.equal(result?.value,false,'crash recovery reported an old write as restored');
     if(consumer==='heartbeat'||consumer==='replay')assert(result?.error,'automatic consumer returned an old write as current success');
     if(edge==='same-wake'&&(consumer==='self-heal'||consumer==='lifecycle')){
      assert(f.life.recoveryActive,'old write settled the current wake recovery');assert.equal(owner.recoveryIntent?.selfHeal?.observeOnly,true,'possibly committed write retained a blind rewrite opportunity');
      assert.equal(f.host.calls.filter(c=>c==='enable').length,beforeWrites+1,'old committed write was repeated immediately');
     }
     if(edge==='off')assert(!f.life.hasControlIntent);if(edge==='new-curve')assert.equal(f.host.applied?.[0].dutyPercent,80);
    }
    f.setDisplayHook(undefined);f.setActivity(null);if(edge==='same-wake')await f.host.resume({generation:6});
    await f.life.apply(latest);assert.equal(f.life.state,'ready');assert.equal(f.host.applied?.[0].dutyPercent,80);
    f.observations.push({consumer,edge,oldWrites:beforeWrites,newManualReachable:true});
   },model,6,true);
  }
  console.log(JSON.stringify({ok:results.every(r=>r.ok),checks:results.length,hardwareCalls:0,results}));if(results.some(r=>!r.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
