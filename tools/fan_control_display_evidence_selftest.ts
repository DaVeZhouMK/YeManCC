/** Public state observations, exact resolver counts and display semantics; no device I/O. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
const curve:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:75}));
async function fixture(family:typeof families[number]=families[1],sink=true){
 const host=new NoIoHost();host.factoryType=family[1];host.generation=6;host.attempt=1;host.powerState='On';host.completeResume=true;let nativeGeneration=6,processStarts=0;
 const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});
 const displays:Array<{active:boolean;curve:FanNode[]}>=[];let displayHook:((active:boolean,nodes:readonly FanNode[])=>void)|undefined;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:60000,resumeWaitDeadlineMs:800,readNativeActivity:async()=>null,
  readNativePowerState:async()=>({generation:nativeGeneration,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true}),
  ...(sink?{onControlDisplay:(active:boolean,nodes:readonly FanNode[])=>{displayHook?.(active,nodes);displays.push({active,curve:nodes.map(n=>({...n}))});}}:{}),
  launcher:{async start(){processStarts++;return{pid:118501,executable:'inert-display-host.exe',processCreated:'inert-display-birth',adopted:true};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});await life.apply(curve);
 const owner=life as unknown as {snapshotGenerationEvidence:(state:FanState)=>unknown},resolve=owner.snapshotGenerationEvidence.bind(owner),originalRead=host.getState.bind(host);
 let resolutions=0,reads=0;owner.snapshotGenerationEvidence=(state:FanState)=>{resolutions++;return resolve(state);};
 let value:FanState|null=null;host.getState=async()=>{reads++;return value??originalRead();};
 return{host,life,displays,setDisplayHook:(hook:typeof displayHook)=>{displayHook=hook;},set:(state:FanState|null)=>value=state,counts:()=>({resolutions,reads}),processStarts:()=>processStarts,
  generation(value:number){nativeGeneration=value;life.setPowerGeneration(value);life.observePowerBoundary('resuming',value);life.observePowerBoundary('resume-ready',value);},
  async cleanup(){owner.snapshotGenerationEvidence=resolve;host.getState=originalRead;await life.close().catch(()=>{});}};
}
const results:any[]=[];
async function check(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]=families[1],sink=true){const f=await fixture(family,sink);try{results.push({name,ok:true,evidence:await work(f)});}catch(error){results.push({name,ok:false,error:String(error),counts:f.counts(),displays:f.displays});}finally{await f.cleanup();}}
for(const family of families)await check(`${family[0]} current ready page snapshot resolves once and suppresses duplicate display callbacks`,async f=>{
 const baseline=f.displays.length,calls=f.host.calls.length,before=f.counts();const state=await f.life.getState();assert.equal(state.state,'Ready');assert.deepEqual(f.counts(),{resolutions:before.resolutions+1,reads:before.reads+1});assert.equal(f.displays.length,baseline);assert.equal(f.host.calls.length,calls+1);return{generationResolutions:1,HTTPReads:1,extraDisplayCallbacks:0};
},family);
for(const kind of ['stale','future','malformed','missing'] as const)await check(`${kind} observation retains its original display semantics and mandatory return`,async f=>{
 const snapshot={...f.host.snapshot(),hardwareWritesEnabled:false},baseline=f.displays.length,before=f.counts();
 if(kind==='stale'){snapshot.resumePhaseGeneration=5;snapshot.powerOperationGeneration=5;}else if(kind==='future'){snapshot.resumePhaseGeneration=7;snapshot.powerOperationGeneration=7;}else if(kind==='malformed'){snapshot.resumePhaseGeneration='invalid';}else{delete snapshot.resumePhaseGeneration;delete snapshot.powerOperationGeneration;delete snapshot.powerOperationAttempt;delete snapshot.powerOperationStatus;}
 f.set(snapshot);assert.equal(await f.life.getState(),snapshot);assert.equal(f.counts().reads,before.reads+1);
 if(kind!=='missing')assert.equal(f.displays.length,baseline,'untrusted snapshot changed the established display');
 return{kind,callbackDelta:f.displays.length-baseline,resolutions:f.counts().resolutions-before.resolutions};
});
for(const kind of ['no-write','unknown','cleanup','missing-open','wrong-lease','sleeping','not-accepting'] as const)await check(`current ${kind} snapshot cannot display active control`,async f=>{
 const snapshot={...f.host.snapshot()};if(kind==='no-write')snapshot.hardwareWritesEnabled=false;if(kind==='unknown')snapshot.unknownState=true;if(kind==='cleanup')snapshot.hcCloseCleanupPending=true;if(kind==='missing-open')snapshot.openEventsCalled=false;if(kind==='wrong-lease')snapshot.lease={leaseId:'foreign-lease',generation:999};if(kind==='sleeping')snapshot.powerState='Suspended';if(kind==='not-accepting')snapshot.controlAccepting=false;
 f.set(snapshot);const before=f.counts();await f.life.getState();assert.equal(f.displays.at(-1)?.active,false);assert.equal(f.counts().reads,before.reads+1);return{kind,active:false,resolutions:f.counts().resolutions-before.resolutions};
});
await check('same snapshot object is re-resolved on each public observation',async f=>{
 const snapshot=f.host.snapshot(),baseline=f.displays.length,before=f.counts();f.set(snapshot);await f.life.getState();
 snapshot.resumePhaseGeneration=7;snapshot.powerOperationGeneration=7;snapshot.hardwareWritesEnabled=false;await f.life.getState();assert.equal(f.displays.length,baseline,'stale cached generation accepted a future no-write reply');
 snapshot.resumePhaseGeneration=6;snapshot.powerOperationGeneration=6;await f.life.getState();assert.equal(f.displays.at(-1)?.active,false);snapshot.hardwareWritesEnabled=true;await f.life.getState();assert.equal(f.displays.at(-1)?.active,true);
 assert.equal(f.counts().resolutions,before.resolutions+4);return{sameObjectObservations:4,freshResolutions:4};
});
await check('local generation changes never reuse a previous observation verdict',async f=>{
 const snapshot=f.host.snapshot(),before=f.counts();f.set(snapshot);await f.life.getState();f.generation(7);const baseline=f.displays.length;await f.life.getState();assert.equal(f.displays.length,baseline,'old generation became current through cached evidence');snapshot.resumePhaseGeneration=7;snapshot.powerOperationGeneration=7;await f.life.getState();assert.equal(f.displays.at(-1)?.active,false,'a new generation with no lease became active');assert(f.counts().resolutions>=before.resolutions+3);return{localGenerationRechecked:true};
});
await check('changed curve display is copied once, then repeated snapshots remain quiet',async f=>{
 const next=curve.map(n=>({...n,dutyPercent:65}));await f.life.apply(next);const baseline=f.displays.length,before=f.counts();await f.life.getState();await f.life.getState();assert.equal(f.displays.length,baseline);assert.deepEqual(f.displays.at(-1)?.curve,next);assert.equal(f.counts().resolutions,before.resolutions+2);return{freshObservations:2,resolutions:2,duplicateCallbacks:0};
});
await check('no display sink adds no generation parsing to a plain state read',async f=>{
 const before=f.counts();await f.life.getState();assert.deepEqual(f.counts(),{resolutions:before.resolutions,reads:before.reads+1});return{displayParsing:0};
},families[1],false);
await check('current discardable page observation also resolves its receipt only once',async f=>{
 const before=f.counts();assert((await f.life.getState({discardSuperseded:true}))?.state==='Ready');assert.deepEqual(f.counts(),{resolutions:before.resolutions+1,reads:before.reads+1});return{discardableReadResolutions:1};
});
for(const discard of [false,true])await check(`${discard?'discardable':'mandatory'} old page read cannot overwrite a newer manual curve`,async f=>{
 let entered!:()=>void,resolve!:(state:FanState)=>void;const ready=new Promise<void>(r=>entered=r),held=new Promise<FanState>(r=>resolve=r),original=f.host.getState.bind(f.host);let first=true;
 const old=f.host.snapshot();f.host.getState=async()=>{if(first){first=false;entered();return held;}return original();};
 const pending=discard?f.life.getState({discardSuperseded:true}):f.life.getState();await ready;const next=curve.map(n=>({...n,dutyPercent:65}));
 try{await f.life.apply(next);const before=f.counts(),baseline=f.displays.length;resolve(old);const result=await pending;assert.equal(result,discard?null:old);assert.equal(f.displays.length,baseline);assert.deepEqual(f.displays.at(-1)?.curve,next);assert.equal(f.counts().resolutions,before.resolutions,'retired page reply performed generation work');return{discard,oldSnapshotNotPublished:true,currentManualCurvePreserved:true};}
 finally{resolve(old);f.host.getState=original;await pending;}
});
// FAN1210: failed delivery must not poison successful-value deduplication.
for(const family of families)for(const action of ['on','off']as const)await check(family[0]+' failed '+action+' display retries on the next existing observation',async f=>{
 const next=curve.map(n=>({...n,dutyPercent:65}));let fail=true,attempts=0;const active=action==='on';
 f.setDisplayHook((shown,nodes)=>{if(shown===active&&(!shown||nodes[0]?.dutyPercent===65)){attempts++;if(fail)throw Error('inert display subscriber unavailable');}});
 if(active)await f.life.apply(next);else await f.life.disable();assert(attempts>0);assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,75,'throwing subscriber was recorded as delivered');
 fail=false;const attemptsBefore=attempts,readsBefore=f.counts().reads,writes=f.host.calls.filter(c=>c==='enable').length;
 await f.life.getState();assert.equal(attempts,attemptsBefore+1,'failed display was permanently deduplicated');assert.equal(f.displays.at(-1)?.active,active);assert.deepEqual(f.displays.at(-1)?.curve,active?next:[]);
 assert.equal(f.counts().reads,readsBefore+1);assert.equal(f.host.calls.filter(c=>c==='enable').length,writes,'display recovery replayed hardware');assert(!f.life.recoveryActive);
 const settled=attempts;for(let i=0;i<10;i++)await f.life.getState();assert.equal(attempts,settled,'successful retry disabled normal display deduplication');
 const manual=curve.map(n=>({...n,dutyPercent:85}));await f.life.apply(manual);assert.equal(f.life.state,'ready');assert.deepEqual(f.displays.at(-1)?.curve,manual);
 return{action,existingReadForRetry:1,additionalWritesForRetry:0,steadyDuplicateCallbacks:0,manualReachable:true};
},family);
await check('older callback failure cannot invalidate a newer synchronously published power state',async f=>{
 let armed=true;f.setDisplayHook((active,nodes)=>{if(armed&&active&&nodes[0]?.dutyPercent===65){armed=false;f.generation(7);throw Error('old projection failed after newer state');}});
 await f.life.apply(curve.map(n=>({...n,dutyPercent:65}))).catch(()=>{});assert.equal(f.displays.at(-1)?.active,false);
 f.set({...f.host.snapshot(),hardwareWritesEnabled:false,lease:null,leaseGeneration:null,resumePhaseGeneration:7,powerOperationGeneration:7});
 const before=f.displays.length;await f.life.getState();assert.equal(f.displays.length,before,'old catch dirtied the newer successful display');return{newerInactiveDisplayPreserved:true};
});
await check('retry delivery marks its cache before an identical synchronous power publication',async f=>{
 f.set({...f.host.snapshot(),hardwareWritesEnabled:false});f.setDisplayHook(active=>{if(!active)throw Error('inert first failure');});await f.life.getState();
 let calls=0;f.setDisplayHook(active=>{if(!active){calls++;if(calls>1)throw Error('recursive display delivery');f.life.observePowerBoundary('resuming',6);}});
 await f.life.getState();assert.equal(calls,1,'retry recursed or never ran');assert.equal(f.displays.at(-1)?.active,false);const before=f.displays.length;await f.life.getState();assert.equal(f.displays.length,before);return{retryCalls:1,recursiveDuplicates:0};
});

// FAN1211: a subscriber may synchronously supersede a page read before it returns.
for(const family of families)for(const edge of ['power','new-curve','off','new-read']as const)for(const discard of [false,true])await check(family[0]+' '+(discard?'discardable':'mandatory')+' read ownership after display '+edge,async f=>{
 const snapshot={...f.host.snapshot(),hardwareWritesEnabled:false};f.set(snapshot);const manual=curve.map(n=>({...n,dutyPercent:85}));let armed=true,pending:Promise<unknown>|null=null;
 f.setDisplayHook(active=>{if(armed&&!active){armed=false;if(edge==='new-read')pending=f.life.getState({discardSuperseded:true});else{f.set(null);if(edge==='power')f.generation(7);else if(edge==='new-curve')pending=f.life.apply(manual);else pending=f.life.disable();}}});
 const result=discard?await f.life.getState({discardSuperseded:true}):await f.life.getState();assert(!armed,'fixture never entered its actual display callback');
 if(pending){const next=await pending;if(edge==='new-read')assert.equal(next,snapshot,'newer observation lost its current packet');}
 f.setDisplayHook(undefined);f.set(null);if(edge==='power')await f.host.resume({generation:7});if(edge!=='new-curve')await f.life.apply(manual);
 assert.equal(f.life.state,'ready');assert.deepEqual(f.displays.at(-1)?.curve,manual);
 assert.equal(result,discard?null:snapshot,'page returned a packet superseded inside its display subscriber');
 return{edge,discard,manualReachable:true,retiredPagePacketDiscarded:discard,mandatoryPacketPreserved:!discard};
},family);
await check('discardable read remains current after only its own display revision and a subscriber exception',async f=>{
 const packet={...f.host.snapshot(),hardwareWritesEnabled:false};f.set(packet);let fail=true;f.setDisplayHook(active=>{if(!active&&fail)throw Error('inert projection fault without ownership change');});
 assert.equal(await f.life.getState({discardSuperseded:true}),packet,'own publication was mistaken for a newer owner');fail=false;assert.equal(await f.life.getState({discardSuperseded:true}),packet);assert.equal(f.displays.at(-1)?.active,false);return{ownRevisionAccepted:true};
});
await check('discardable current read without a display subscriber keeps its packet and zero resolver work',async f=>{
 const before=f.counts();assert.equal((await f.life.getState({discardSuperseded:true}))?.state,'Ready');assert.deepEqual(f.counts(),{reads:before.reads+1,resolutions:before.resolutions});return{noSinkResolutions:0,currentPacketPreserved:true};
},families[1],false);

// FAN1212: revoke only stale control-display proof on a current declared refusal.
function installRefusal(f:Awaited<ReturnType<typeof fixture>>,error:FanApiError){
 let denied=true;const host=f.host as any,originals:Array<[string,any]>=[];
 for(const name of ['handshake','getState','open','openEvents','acquireControl','enable','applyPreset','heartbeat','restoreOem','releaseControl','disable','resume','close','shutdown']){
  const original=host[name];if(typeof original!=='function')continue;originals.push([name,original]);host[name]=async(...args:any[])=>{if(denied){host.calls.push('refused:'+name);throw error;}return original.apply(host,args);};
 }
 return{allow(){denied=false;},restore(){denied=false;for(const[name,fn]of originals)host[name]=fn;}};
}
const refusalOutcome=(p:Promise<unknown>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
const refusalSpecs=[['state',401,'AUTH_REJECTED'],['heartbeat',401,'AUTH_REJECTED'],['apply',401,'AUTH_REJECTED'],['preset',401,'AUTH_REJECTED'],['state',403,'API_SESSION_REQUIRED'],['state',409,'FAN_UNSUPPORTED'],['state',409,'EXTERNAL_FAN_OWNER']]as const;
for(const family of families)for(const[channel,status,code]of refusalSpecs)await check(family[0]+' current '+channel+'/'+code+' clears display proof while keeping manual retry',async f=>{
 const error=new FanApiError('inert declared refusal',status,code),guard=installRefusal(f,error),next=curve.map(n=>({...n,dutyPercent:85}));const writes=f.host.calls.filter(c=>c==='enable'||c==='preset').length;
 try{
  const request=channel==='state'?f.life.getState():channel==='heartbeat'?f.life.heartbeat():channel==='apply'?f.life.apply(next):f.life.applyPreset('aggressive',next);
  await assert.rejects(request,e=>e===error);assert.equal(f.displays.at(-1)?.active,false,'declared refusal left the prior control display active');assert.deepEqual(f.displays.at(-1)?.curve,[]);
  assert(f.life.hasControlIntent);assert(f.life.recoveryNeedsAttention);assert(!f.life.recoveryActive);assert.equal(f.host.calls.filter(c=>c==='enable'||c==='preset').length,writes);assert(!f.host.calls.includes('restore'));assert(!f.host.calls.includes('release'));
  const displayed=f.displays.length;await assert.rejects(f.life.apply(next),e=>e===error);assert.equal(f.displays.length,displayed,'same refusal repeatedly published the same inactive display');
  guard.allow();assert((await f.host.handshake()).supported);assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(next);assert.equal(f.life.state,'ready');assert.equal(f.displays.at(-1)?.active,true);assert.deepEqual(f.displays.at(-1)?.curve,next);assert(!f.life.recoveryNeedsAttention);
  return{channel,code,status,automaticHardwareWrites:0,intentRetained:true,currentRefusalPreserved:true,authorizedManualReachable:true};
 }finally{guard.restore();}
},family);
for(const family of [families[0],families[2]])for(const edge of ['new-curve','off','power','same-generation-wake','new-read']as const)for(const discard of [false,true])await check(family[0]+' '+(discard?'page':'mandatory')+' refusal callback ownership after '+edge,async f=>{
 const error=new FanApiError('inert old permission refusal',401,'AUTH_REJECTED'),guard=installRefusal(f,error),next=curve.map(n=>({...n,dutyPercent:85}));let armed=true,pending:Promise<unknown>|null=null;
 try{
  f.setDisplayHook(active=>{if(armed&&!active){armed=false;guard.allow();if(edge==='new-curve')pending=f.life.apply(next);else if(edge==='off')pending=f.life.disable();else if(edge==='power')f.generation(7);else if(edge==='same-generation-wake'){f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);}else pending=f.life.getState({discardSuperseded:true});}});
  const result=await refusalOutcome(discard?f.life.getState({discardSuperseded:true}):f.life.getState());assert(!armed,'refusal never revoked its prior display proof');if(pending)await pending;
  f.setDisplayHook(undefined);if(edge==='power')await f.host.resume({generation:7});if(edge!=='new-curve')await f.life.apply(next);
  assert.equal(f.displays.at(-1)?.active,true);assert.deepEqual(f.displays.at(-1)?.curve,next);assert.equal(result.error,discard?null:error,'old refusal replaced a newer page or changed mandatory error');assert.equal(result.value,null);
  return{edge,discard,authorizedManualReachable:true,oldPageRefusalDiscarded:discard,mandatoryErrorPreserved:!discard};
 }finally{f.setDisplayHook(undefined);guard.restore();}
},family);
for(const[status,code]of [[401,'AUTH_REJECTED'],[403,'API_SESSION_REQUIRED'],[409,'FAN_UNSUPPORTED'],[409,'EXTERNAL_FAN_OWNER']]as const)await check('current discardable '+code+' keeps its real error after own refusal bookkeeping',async f=>{
 const error=new FanApiError('inert current refusal',status,code),guard=installRefusal(f,error);try{await assert.rejects(f.life.getState({discardSuperseded:true}),e=>e===error);assert.equal(f.displays.at(-1)?.active,false);return{currentErrorNotHidden:true};}finally{guard.restore();}
});
for(const family of [families[0],families[2]])await check(family[0]+' old refused read cannot revoke a newer manual display',async f=>{
 const original=f.host.getState.bind(f.host);let enter!:()=>void,reject!:(error:Error)=>void,resolve!:(value:FanState)=>void,first=true;const ready=new Promise<void>(r=>enter=r),held=new Promise<FanState>((r,j)=>{resolve=r;reject=j;});f.host.getState=async()=>{if(first){first=false;enter();return held;}return original();};
 const pending=refusalOutcome(f.life.getState({discardSuperseded:true}));try{await ready;const next=curve.map(n=>({...n,dutyPercent:85}));await f.life.apply(next);const displayed=f.displays.length;reject(new FanApiError('old refused packet',401,'AUTH_REJECTED'));assert.deepEqual(await pending,{value:null,error:null});assert.equal(f.displays.length,displayed);assert.equal(f.displays.at(-1)?.active,true);assert.deepEqual(f.displays.at(-1)?.curve,next);return{oldErrorNotPublished:true};}finally{resolve(f.host.snapshot());await pending;f.host.getState=original;}
},family);
for(const[channel,status,code]of [['state',500,'HOST_ERROR'],['state',0,'FAN_HTTP_RESULT_UNKNOWN'],['heartbeat',409,'HC_OPERATION_BUSY']]as const)await check('uncertain '+channel+'/'+code+' keeps its existing control projection',async f=>{
 const error=new FanApiError('inert uncertainty',status,code),guard=installRefusal(f,error),before=f.displays.length;try{await assert.rejects(channel==='state'?f.life.getState():f.life.heartbeat(),e=>e===error);assert.equal(f.displays.length,before);assert.equal(f.displays.at(-1)?.active,true);assert(!f.life.recoveryNeedsAttention);return{uncertaintyNotPromotedToLoss:true};}finally{guard.restore();}
});

for(const discard of [false,true])await check((discard?'page':'mandatory')+' refusal preserves its error while an inactive display delivery fails then recovers',async f=>{
 const error=new FanApiError('inert current authorization refusal',401,'AUTH_REJECTED'),guard=installRefusal(f,error);let fail=true;
 try{f.setDisplayHook(active=>{if(!active&&fail)throw Error('inert projection delivery failure');});await assert.rejects(discard?f.life.getState({discardSuperseded:true}):f.life.getState(),e=>e===error);
  fail=false;await assert.rejects(discard?f.life.getState({discardSuperseded:true}):f.life.getState(),e=>e===error);assert.equal(f.displays.at(-1)?.active,false,'failed refusal display never retried');assert(f.life.hasControlIntent);
  guard.allow();f.setDisplayHook(undefined);await f.life.apply(curve);assert.equal(f.displays.at(-1)?.active,true);return{refusalNotHidden:true,projectionRetrySucceeded:true};
 }finally{f.setDisplayHook(undefined);guard.restore();}
});

// A synchronous display subscriber is an authority boundary for the write's
// return value too. It can cancel or start a newer wake before the old caller resumes.
for(const family of families)for(const operation of ['apply','preset-explicit','preset-host'] as const)for(const boundary of ['new-curve','off','same-wake','same-wake-ready','new-generation','page-read','ready-notice','projection-failure'] as const)await check(`${family[0]} ${operation} write display callback after ${boundary}`,async f=>{
 const edited=curve.map(n=>({...n,dutyPercent:65})),latest=curve.map(n=>({...n,dutyPercent:45}));
 const calls=f.host.calls.length;let fired=0,pending:Promise<{value?:unknown;error?:unknown}>|null=null;
 if(operation==='preset-host'){
  const preset=f.host.applyPreset.bind(f.host);
  f.host.applyPreset=async()=>({...await preset(),activeCurve:edited.map(n=>({...n}))});
 }
 const follow=(p:Promise<unknown>)=>p.then(value=>({value}),error=>({error}));
 f.setDisplayHook((active,nodes)=>{
  if(!active||nodes[0]?.dutyPercent!==65||fired)return;
  fired++;f.setDisplayHook(undefined);
  if(boundary==='new-curve')pending=follow(f.life.apply(latest));
  else if(boundary==='off')pending=follow(f.life.disable());
  else if(boundary==='new-generation'){f.host.generation=7;f.generation(7);}
  else if(boundary==='same-wake'||boundary==='same-wake-ready'){
   f.life.observePowerBoundary('resuming',6);f.host.lease=null;
   if(boundary==='same-wake-ready')f.life.observePowerBoundary('resume-ready',6);
  }else if(boundary==='page-read')pending=follow(f.life.getState());
  else if(boundary==='ready-notice'){f.life.observePowerBoundary('resume-ready',6);f.life.observePowerBoundary('resumed',6);}
  else throw Error('inert write display delivery failed');
 });
 try{
  const result=await follow(operation==='apply'?f.life.apply(edited):operation==='preset-explicit'?f.life.applyPreset('soft',edited):f.life.applyPreset('soft'));
  assert.equal(fired,1,'the successful write display callback was not exercised');
  const retired=['new-curve','off','same-wake','same-wake-ready','new-generation'].includes(boundary);
  if(retired){
   assert(result.error,'retired successful write was returned as current success');
   if(boundary==='same-wake'||boundary==='same-wake-ready'){
    assert(result.error instanceof FanApiError&&result.error.errorCode==='FAN_HTTP_RESULT_UNKNOWN','same-generation wake must keep the old committed write uncertain');
    const owner=f.life as any;assert.equal(owner.recoveryIntent?.selfHeal?.observeOnly,true,'uncertain committed write was granted an automatic rewrite');
    assert.equal(f.host.calls.slice(calls).filter(x=>x==='enable'||x==='preset').length,1,'uncertain old write was replayed immediately');
   }else assert(String(result.error).includes('FAN_CONTROL_INTENT_SUPERSEDED'),'new authority must retire the old write result');
   if(pending){const newer=await pending;assert.equal(newer.error,undefined,'the new manual owner did not finish');}
   if(boundary==='off'){assert.equal(f.life.hasControlIntent,false,'OFF intent was resurrected');assert.equal(f.life.currentLease,null,'OFF left its released lease active');assert.equal(f.life.state,'awaiting-control');}
   else if(boundary==='new-curve')assert.deepEqual(f.displays.at(-1)?.curve,latest,'old write overrode the newer manual curve');
   else{
    f.life.observePowerBoundary('resume-ready',boundary==='new-generation'?7:6);
    await f.life.apply(latest);assert.deepEqual(f.displays.at(-1)?.curve,latest,'one new explicit request did not recover current control');
   }
  }else{
   assert.equal(result.error,undefined,'an observation-only callback invalidated the real write');
   if(pending){const observed=await pending;assert.equal(observed.error,undefined);}
   if(boundary==='projection-failure')await f.life.getState();
   assert.deepEqual(f.displays.at(-1)?.curve,edited);
   assert.equal(f.host.calls.slice(calls).filter(x=>x==='enable'||x==='preset').length,1,'display/read callback caused a duplicate hardware request');
   assert.equal(f.life.recoveryActive,false);
  }
  assert.equal(f.processStarts(),1,'post-display reconciliation launched another Host');
  return{boundary,operation,retired,callbackCount:fired,writes:f.host.calls.slice(calls).filter(x=>x==='enable'||x==='preset').length,processStarts:f.processStarts()};
 }finally{f.setDisplayHook(undefined);}
},family);


for(const family of families)await check(`${family[0]} unresolved name-only preset cannot borrow an older curve receipt for automatic healing`,async f=>{
 const original=f.host.applyPreset.bind(f.host),edited=curve.map(n=>({...n,dutyPercent:65}));
 const error=new FanApiError('inert unresolved preset result',0,'FAN_HTTP_RESULT_UNKNOWN');
 f.host.applyPreset=async()=>{f.host.calls.push('preset-unknown');throw error;};
 try{
  await assert.rejects(f.life.applyPreset('soft'),e=>e===error);
  assert.equal(f.life.recoveryActive,false,'old successful curve was registered as the unresolved new preset');
  f.host.applyPreset=async()=>({...await original(),activeCurve:edited.map(n=>({...n}))});
  await f.life.applyPreset('soft');assert.deepEqual(f.displays.at(-1)?.curve,edited,'fresh name-only preset remained blocked');
  assert.equal(f.processStarts(),1);return{oldReceiptNotReused:true,nextExplicitPresetSucceeded:true};
 }finally{f.host.applyPreset=original;}
},family);

// FAN1223: the same authenticated resume snapshot crosses display subscribers.
async function prepareReplayReceipt(f:Awaited<ReturnType<typeof fixture>>){
 const snapshot=f.host.snapshot.bind(f.host),enable=f.host.enable.bind(f.host);let hosted:readonly FanNode[]=curve;
 f.host.snapshot=()=>({...snapshot(),activeCurve:hosted,lastControlWriteEvidence:'compat-route-callback-returned-no-scope'});
 f.host.enable=async(nodes:readonly FanNode[]=curve)=>{hosted=nodes.map(n=>({...n}));return enable();};
 await f.life.disable();let deliveryFailed=0;
 f.setDisplayHook(active=>{if(active){deliveryFailed++;throw Error('inert temporarily unavailable display subscriber');}});
 await f.life.apply(curve);assert(deliveryFailed>0,'fixture failed to leave a genuine display retry');
 return{snapshot,enable};
}
for(const family of families)for(const boundary of ['new-generation','same-wake','ready-notice','projection-failure','page-read']as const)await check(`${family[0]} resume snapshot display ownership after ${boundary}`,async f=>{
 const saved=await prepareReplayReceipt(f);let fired=0;let pending:Promise<unknown>|undefined;
 f.setDisplayHook(active=>{
  if(!active||fired)return;fired++;
  if(boundary==='new-generation')f.generation(7);
  else if(boundary==='same-wake'){f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.life.observePowerBoundary('resume-ready',6);}
  else if(boundary==='ready-notice'){f.life.observePowerBoundary('resume-ready',6);f.life.observePowerBoundary('resumed',6);}
  else if(boundary==='page-read')pending=f.life.getState({discardSuperseded:true});
  else throw Error('inert display projection failure');
 });
 const beforeCalls=f.host.calls.length;
 try{
  await f.life.resume();await pending;assert.equal(fired,1,'resume never entered its actual display retry');
  const writes=f.host.calls.slice(beforeCalls).filter(c=>c==='enable').length;
  assert.equal(writes,0,'already applied curve was rewritten during display reconciliation');
  if(boundary==='new-generation'||boundary==='same-wake')assert.notEqual(f.life.coordinatorSnapshot.phase,'ready','old display-crossing resume admitted a newer wake as HC Ready');
  else assert.equal(f.life.state,'ready','observation-only display work cancelled a current resume');
  f.setDisplayHook(undefined);if(boundary==='new-generation'||boundary==='same-wake')await f.host.resume({generation:boundary==='new-generation'?7:6});
  const latest=curve.map(n=>({...n,dutyPercent:85}));await f.life.apply(latest);assert.equal(f.life.state,'ready');assert.deepEqual(f.displays.at(-1)?.curve,latest);
  return{boundary,oldReplayWrites:writes,manualReachable:true};
 }finally{f.setDisplayHook(undefined);f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
},family);
for(const boundary of ['same-wake','process','session']as const)await check('held resume snapshot cannot adopt a newer '+boundary,async f=>{
 const saved=await prepareReplayReceipt(f);f.setDisplayHook(undefined);
 const getState=f.host.getState.bind(f.host),old=f.host.snapshot();let release!:(v:FanState)=>void,entered!:()=>void;
 const held=new Promise<FanState>(r=>release=r),ready=new Promise<void>(r=>entered=r);let first=true;
 f.host.getState=async()=>{if(first){first=false;entered();return held;}return getState();};
 const owner=f.life as any,pending=f.life.resume();await ready;
 if(boundary==='same-wake'){f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.life.observePowerBoundary('resume-ready',6);}
 else if(boundary==='process')owner.process={...owner.process};
 else owner.sessionGeneration+=1;
 const beforeCalls=f.host.calls.length;release(old);
 try{
  await pending;assert.equal(f.host.calls.slice(beforeCalls).filter(c=>c==='enable'||c==='open'||c==='acquire').length,0,'retired resume snapshot entered control of the newer boundary');
  if(boundary==='same-wake')assert.notEqual(f.life.coordinatorSnapshot.phase,'ready','old held snapshot admitted a newer wake');
  f.host.getState=getState;if(boundary==='same-wake')await f.host.resume({generation:6});
  const manual=curve.map(n=>({...n,dutyPercent:85}));await f.life.apply(manual);assert.equal(f.life.state,'ready');assert.deepEqual(f.displays.at(-1)?.curve,manual);
  return{boundary,staleSnapshotIgnored:true,manualReachable:true};
 }finally{release(old);f.host.getState=getState;f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;f.setDisplayHook(undefined);}
});
// FAN1225: probe -> display -> admission must keep the accepted read's owner.
for(const family of families)for(const boundary of ['new-generation','same-wake','off','new-curve','ready-notice','page-read','projection-failure']as const)await check(`${family[0]} committed wake admission display after ${boundary}`,async f=>{
 const saved=await prepareReplayReceipt(f);let fired=0,pending:Promise<unknown>|undefined;
 f.setDisplayHook(active=>{
  if(!active||fired)return;fired++;
  if(boundary==='new-generation')f.generation(7);
  else if(boundary==='same-wake'){f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);}
  else if(boundary==='off')pending=f.life.disable();
  else if(boundary==='new-curve')pending=f.life.apply(curve.map(n=>({...n,dutyPercent:85})));
  else if(boundary==='ready-notice'){f.life.observePowerBoundary('resume-ready',6);f.life.observePowerBoundary('resumed',6);}
  else if(boundary==='page-read')pending=f.life.getState({discardSuperseded:true});
  else throw Error('inert projection failure during rearm');
 });
 const start=f.host.calls.length,baselineReads=f.counts().reads;let failure:unknown;
 const owner=f.life as any,admit=owner.admitCoordinatorHcReady.bind(owner),readsAtAdmission:number[]=[];
 owner.admitCoordinatorHcReady=()=>{if(fired)readsAtAdmission.push(f.counts().reads-baselineReads);return admit();};
 try{
  try{await f.life.resumeAfterCommittedPowerWake(6);}catch(error){failure=error;}
  await pending;assert.equal(fired,1,'committed rearm never exercised display callback');
  const retired=['new-generation','off','new-curve'].includes(boundary);
  if(retired){
   assert(failure&&(failure instanceof FanApiError&&failure.errorCode==='POWER_RESUMING'||failure instanceof Error&&failure.message.startsWith('FAN_CONTROL_INTENT_SUPERSEDED:')),'retired admission did not settle as its own cancellation/wake handoff');
   assert.equal(f.host.calls.slice(start).filter(c=>c==='resume').length,0,'old rearm sent a resume after its display subscriber advanced control');
   if(boundary==='off')assert(!f.life.hasControlIntent);
   if(boundary==='new-curve')assert.deepEqual(f.displays.at(-1)?.curve,curve.map(n=>({...n,dutyPercent:85})));
  }else{assert.equal(failure,undefined,'ordinary observation cancelled committed rearm');assert.equal(f.host.calls.slice(start).filter(c=>c==='enable').length,0,'healthy existing curve was replayed');}
  if(boundary==='same-wake')assert(readsAtAdmission.length>0&&readsAtAdmission[0]>=2,'HC readiness was admitted before replacing the display-crossed state read');
  f.setDisplayHook(undefined);if(boundary==='new-generation'||boundary==='same-wake')await f.host.resume({generation:boundary==='new-generation'?7:6});
  await f.life.apply(curve.map(n=>({...n,dutyPercent:90})));assert.equal(f.life.state,'ready');assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,90);
  return{boundary,retired,readsAtAdmission,manualReachable:true};
 }finally{owner.admitCoordinatorHcReady=admit;f.setDisplayHook(undefined);f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
},family);
for(const phase of ['initial-probe','bounded-probe','microtask-initial-probe','microtask-bounded-probe']as const)await check('direct manual admission refreshes a display-crossed '+phase,async f=>{
 const saved=await prepareReplayReceipt(f);f.setDisplayHook(undefined);f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);
 let fired=0,reads=0,readsAtWrite=0;const getState=f.host.getState.bind(f.host),enable=f.host.enable.bind(f.host);
 f.host.getState=async()=>{reads++;if(phase.endsWith('bounded-probe')&&reads===1)return{...f.host.snapshot(),state:'Resuming',resumePhase:'Resuming',powerState:'Resuming',controlAccepting:false,hardwareWritesEnabled:false};return getState();};
 f.host.enable=async(nodes:readonly FanNode[]=curve)=>{readsAtWrite=reads;return enable(nodes);};
 f.setDisplayHook(active=>{if(!active||fired)return;fired++;const wave=()=>{f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);};if(phase.startsWith('microtask-'))queueMicrotask(wave);else wave();});
 try{
  try{await f.life.apply(curve.map(n=>({...n,dutyPercent:85})));}
  catch(error){
   assert(phase.startsWith('microtask-')&&error instanceof FanApiError&&error.errorCode==='POWER_RESUMING'&&f.life.recoveryActive,'late wake failed without retaining this one manual request');
   const deadline=Date.now()+3000;while(f.life.recoveryActive&&Date.now()<deadline)await new Promise(r=>setTimeout(r,20));
   assert(!f.life.recoveryActive&&f.life.state==='ready','the retained manual request needed another user click');
  }
  assert.equal(fired,1,'admission never exercised its actual display callback');
  assert(readsAtWrite>=(phase.endsWith('bounded-probe')?3:2),'direct control wrote before replacing the display-crossed state read');assert.equal(f.life.state,'ready');assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,85);
  return{phase,readsAtWrite,manualCompletedOnce:true};
 }finally{f.setDisplayHook(undefined);f.host.getState=getState;f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
});
await check('queued wake during manual admission never reuses its retired packet',async f=>{
 const saved=await prepareReplayReceipt(f);f.setDisplayHook(undefined);f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);
 const owner=f.life as any,classify=owner.classifyControlAdmission.bind(owner),getState=f.host.getState.bind(f.host);let reads=0,fired=0,badClassifications=0,lastRead:FanState|null=null,retired:FanState|null=null;
 f.host.getState=async()=>{reads++;lastRead=reads===1?{...f.host.snapshot(),state:'Resuming',resumePhase:'Resuming',powerState:'Resuming',controlAccepting:false,hardwareWritesEnabled:false}:await getState();return lastRead;};
 owner.classifyControlAdmission=(remote:FanState,...args:any[])=>{if(retired&&remote===retired)badClassifications++;return classify(remote,...args);};
 f.setDisplayHook(active=>{if(!active||fired)return;fired++;queueMicrotask(()=>{const packet=lastRead;f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);retired=packet;});});
 try{
  try{await f.life.apply(curve.map(n=>({...n,dutyPercent:85})));}
  catch(error){
   assert(error instanceof FanApiError&&error.errorCode==='POWER_RESUMING'&&f.life.recoveryActive,'retired wait did not preserve the manual request');
   const deadline=Date.now()+3000;while(f.life.recoveryActive&&Date.now()<deadline)await new Promise(r=>setTimeout(r,20));
  }
  assert.equal(fired,1);assert(retired,'queued event did not retire a real state packet');assert.equal(badClassifications,0,'bounded wait classified a packet retired after its protected display callback');
  assert(!f.life.recoveryActive&&f.life.state==='ready','one manual request failed to complete automatically');assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,85);
  return{badClassifications,reads,oneManualRequest:true};
 }finally{owner.classifyControlAdmission=classify;f.setDisplayHook(undefined);f.host.getState=getState;f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
});
for(const boundary of ['same-wake','session','process']as const)await check('queued display event fences committed admission '+boundary,async f=>{
 const saved=await prepareReplayReceipt(f),owner=f.life as any,admit=owner.admitCoordinatorHcReady.bind(owner);let queued=false,fired=0,readsAtCrossing=0,badGrants=0;
 owner.admitCoordinatorHcReady=()=>{if(queued&&f.counts().reads<=readsAtCrossing)badGrants++;return admit();};
 f.setDisplayHook(active=>{if(!active||fired)return;fired++;queueMicrotask(()=>{
  readsAtCrossing=f.counts().reads;queued=true;
  if(boundary==='same-wake'){f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);}
  else if(boundary==='session')owner.sessionGeneration+=1;else owner.process={...owner.process};
 });});
 try{
  await f.life.resumeAfterCommittedPowerWake(6).catch(error=>{assert(error instanceof FanApiError&&error.errorCode==='POWER_RESUMING'||error instanceof Error&&error.message.startsWith('FAN_CONTROL_INTENT_SUPERSEDED:'));});
  assert(queued&&fired===1);assert.equal(badGrants,0,'queued event crossed the helper return before HC readiness reused its old read');
  f.setDisplayHook(undefined);await f.life.apply(curve.map(n=>({...n,dutyPercent:90})));assert.equal(f.life.state,'ready');assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,90);
  return{boundary,badGrants,manualReachable:true};
 }finally{owner.admitCoordinatorHcReady=admit;f.setDisplayHook(undefined);f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
});
await check('display-crossed recovery probe keeps its original two-read budget',async f=>{
 const saved=await prepareReplayReceipt(f);let crossed=0;const reads=f.counts().reads;
 f.setDisplayHook(active=>{if(!active||crossed>=2)return;crossed++;f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);});
 try{
  await assert.rejects(f.life.resumeAfterCommittedPowerWake(6),e=>e instanceof FanApiError&&e.errorCode==='POWER_RESUMING');
  assert.equal(crossed,2);assert.equal(f.counts().reads-reads,2,'projection refresh created an unbounded probe loop');assert(f.life.recoveryActive,'probe exhaustion lost the original control intent');
  f.setDisplayHook(undefined);await f.life.apply(curve.map(n=>({...n,dutyPercent:90})));assert.equal(f.life.state,'ready');assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,90);
  return{reads:2,manualReachable:true};
 }finally{f.setDisplayHook(undefined);f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
});
await check('display-crossed recovery probe does not extend its absolute deadline',async f=>{
 const saved=await prepareReplayReceipt(f),descriptor=Object.getOwnPropertyDescriptor(globalThis,'performance')!,now=performance.now.bind(performance);let offset=0,crossed=0;const reads=f.counts().reads;
 Object.defineProperty(globalThis,'performance',{value:{now:()=>now()+offset},configurable:true});
 f.setDisplayHook(active=>{if(!active||crossed)return;crossed++;offset+=2000;f.life.observePowerBoundary('resuming',6);f.life.observePowerBoundary('resume-ready',6);});
 try{
  await assert.rejects(f.life.resumeAfterCommittedPowerWake(6),e=>e instanceof FanApiError&&e.errorCode==='POWER_RESUMING');
  assert.equal(crossed,1);assert.equal(f.counts().reads-reads,1,'expired probe minted a fresh deadline for its display refresh');assert(f.life.recoveryActive);
  return{virtualClockJumpMs:2000,reads:1,deadlinePreserved:true};
 }finally{Object.defineProperty(globalThis,'performance',descriptor);f.setDisplayHook(undefined);f.host.snapshot=saved.snapshot;f.host.enable=saved.enable;}
});
// FAN1228: a queued subscriber event can run after the mutation's final fence.
for(const family of families)for(const operation of ['apply','preset-explicit','preset-host']as const)for(const edge of ['off','new-curve','same-wake','process','session']as const)await check(`${family[0]} ${operation} completion crosses queued ${edge}`,async f=>{
 const edited=curve.map(n=>({...n,dutyPercent:65})),latest=curve.map(n=>({...n,dutyPercent:45}));let fired=0,pending:Promise<{value?:unknown;error?:unknown}>|undefined;
 const owner=f.life as any,preset=f.host.applyPreset.bind(f.host);if(operation==='preset-host')f.host.applyPreset=async()=>({...await preset(),activeCurve:edited.map(n=>({...n}))});
 const follow=(p:Promise<unknown>)=>p.then(value=>({value}),error=>({error}));
 f.setDisplayHook((active,nodes)=>{if(!active||nodes[0]?.dutyPercent!==65||fired)return;fired++;queueMicrotask(()=>{
  if(edge==='off')pending=follow(f.life.disable());
  else if(edge==='new-curve')pending=follow(f.life.apply(latest));
  else if(edge==='same-wake'){f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.life.observePowerBoundary('resume-ready',6);}
  else if(edge==='process')owner.process={...owner.process};else owner.sessionGeneration++;
 });});
 const start=f.host.calls.length;
 try{
  const result=await follow(operation==='apply'?f.life.apply(edited):operation==='preset-explicit'?f.life.applyPreset('soft',edited):f.life.applyPreset('soft'));
  assert.equal(fired,1);assert(result.error,'queued ownership change returned an old committed write as current success');
  if(edge==='same-wake'){
   assert(result.error instanceof FanApiError&&result.error.errorCode==='FAN_HTTP_RESULT_UNKNOWN','committed pre-wake write must be reconciled without replay');
   assert.equal(owner.recoveryIntent?.selfHeal?.observeOnly,true);assert.equal(f.host.calls.slice(start).filter(c=>c==='enable'||c==='preset').length,1,'uncertain old write was retried immediately');
  }else assert(String(result.error).includes('FAN_CONTROL_INTENT_SUPERSEDED'),'owner replacement must retire its old completion');
  if(pending){const next=await pending;assert.equal(next.error,undefined,'new queued user action was blocked by the old completion');}
  if(edge==='off')assert(!f.life.hasControlIntent);
  if(edge==='new-curve')assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,45);
  f.setDisplayHook(undefined);if(edge==='same-wake')await f.host.resume({generation:6});
  await f.life.apply(curve.map(n=>({...n,dutyPercent:80})));assert.equal(f.life.state,'ready');assert.equal(f.displays.at(-1)?.curve[0]?.dutyPercent,80);
  assert.equal(f.processStarts(),1);return{operation,edge,oldCompletionRetired:true,newManualReachable:true};
 }finally{f.setDisplayHook(undefined);f.host.applyPreset=preset;}
},family);
console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,cpuBenchmark:false,scope:'public state read/display with actual generation resolver; inert Host/native/process, call counters only, no time/allocation benchmark'}));
if(results.some(x=>!x.ok))process.exitCode=1;
