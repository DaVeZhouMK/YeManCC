import assert from 'node:assert/strict';
import {compileSettingsConsumer,createSettingsFixture} from '../fixtures/settings_baseline.mjs';

const idle=()=>new Promise(resolve=>setImmediate(resolve));
const settle=async()=>{for(let n=0;n<12;n++)await idle();};
const writes=q=>q.calls.filter(row=>row.op==='settings.update').length;
const result=q=>q.settings.extensions.bootModesStartupResult;
const evidence=(a,states={})=>{
 const input=a.ports.settings.input,request=a.inputReceipts.inputApplicationRequest(input);
 const proof={protocol:'ymcc.input-application.v1',boot:input.startupObservedBoot,inputRevision:request.revision,
  inputConfigHash:request.hash,persona:request.persona,inputState:request.target?'active':'disabled',
  gyroState:request.gyro?'active':'disabled',runId:request.target?'run-1':'',hostInstanceId:request.target?'host-1':'',
  hostEpoch:request.target?'3':'0',powerGeneration:request.target?'2':'0',outputSequence:request.target?'21':'0',
  sourceSequence:request.target?'20':'0',sourceIdentity:1,sourceTopology:2,
  meaning:'current-config-local-host-frame;not-game-consumption',...states};
 return {virtualOutput:{requestedPersona:request.persona,boundPersona:request.persona,hostEpoch:3,hostAlive:true,
  prepared:request.target,neutralized:true,frameAccepted:true,sourceLive:true,sourceCurrent:true,
  physicalReturnPending:false,physicalReturnFailed:false,inputApplication:proof}};
};
const start=async(a,id,{target=true,gyro=true,delay=0}={})=>{
 const q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
 for(const scene of Object.values(doc.scenes)){delete scene.overrides.input;delete scene.overrides.gyro;}
 doc.global.input={...doc.global.input,persona:target?'elite':'disabled',mouse:'off',
  outputTarget:{...doc.global.input.outputTarget,persona:target?'elite':'disabled',buttonMappingEnabled:target,gyroEnabled:gyro}};
 doc.global.gyro={motion:{...doc.global.gyro.motion,enabled:gyro,outputMode:gyro?'right-stick':'disabled'}};
 if(delay)doc.global.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay};
 doc.boot=id;doc.bootEnabled=true;q.settings.extensions.bootModes=structuredClone(doc);
 await a.boot.restoreSelectedBootMode();return result(q);
};
const emit=async(a,value=evidence(a))=>{await a.ports.emitMemoryIpc('gamepad.state',value);await settle();};
export async function confirmInputReceipt(a){
 const stop=a.boot.observeBootModeRuntimeReceipts();
 try{await emit(a);}finally{stop();}
}

export async function runInputReceiptCases({test,out,runtimeBundle}){
 await test('input receipt validates native schema, exact counters, enclosing Host and immutable request',async()=>{
  const a=await runtimeBundle(out),p=a.inputReceipts,raw=evidence(a);
  assert(p.inputApplicationFromGamepad(raw));
  for(const patch of [{protocol:'old'},{inputRevision:1.1},{inputConfigHash:'sha256:no'},{boot:''},{hostEpoch:'03'},
   {hostEpoch:'18446744073709551616'},{powerGeneration:'0'},{sourceSequence:1},{sourceIdentity:-1},
   {sourceTopology:4294967296},{runId:''},{hostInstanceId:''},{persona:'disabled'},
   {inputState:'waiting',gyroState:'active'},{meaning:'game-consumed'}])
   assert.equal(p.inputApplicationFromGamepad({virtualOutput:{...raw.virtualOutput,inputApplication:{...raw.virtualOutput.inputApplication,...patch}}}),undefined,JSON.stringify(patch));
  for(const patch of [{boundPersona:'steamdeck'},{requestedPersona:'steamdeck'},{hostEpoch:4},{prepared:false},
   {hostAlive:false},{neutralized:false},{sourceLive:false},{sourceCurrent:false},{frameAccepted:false},
   {physicalReturnPending:true},{physicalReturnFailed:true}])
   assert.equal(p.inputApplicationFromGamepad({virtualOutput:{...raw.virtualOutput,...patch}}),undefined,JSON.stringify(patch));
  const proof=p.inputApplicationFromGamepad(raw);raw.virtualOutput.inputApplication.hostEpoch='99';assert.equal(proof.hostEpoch,'3');
  const request=p.inputApplicationRequest(a.ports.settings.input);assert.equal(p.readInputApplicationRequest(request).hash,request.hash);
  assert.equal(p.readInputApplicationRequest({...request,revision:-1}),undefined);
  // A contradictory durable gyro request is still pending; never invent active output.
  a.ports.settings.input.outputTarget={persona:'disabled',gyroEnabled:true};a.ports.settings.input.gyroMotion.enabled=true;
  const contradictory=p.inputApplicationRequest(a.ports.settings.input);assert.equal(contradictory.target,false);assert.equal(contradictory.gyro,true);
  assert(p.readInputApplicationRequest(contradictory));
  return {malformedProofs:15,staleHostEnvelopes:11,primitiveCopy:true,contradictoryIntentNotActivated:true};
 });
 await test('input receipt confirms input and gyro separately in all four modes without replay or duplicate writes',async()=>{
  const rows=[];
  for(const id of ['global','xbox','steam','xbox-steam']){
   const a=await runtimeBundle(out),q=a.ports;await start(a,id);const stop=a.boot.observeBootModeRuntimeReceipts();
   try{
    await settle();assert.equal(result(q).modules.input.state,'saved');assert.equal(result(q).modules.gyro.state,'saved');
    const legacy=structuredClone(result(q));
    for(const module of ['input','gyro']){legacy.modules[module].state='active';delete legacy.modules[module].proof;}
    legacy.status='applied';legacy.pending=[];legacy.applied=['虚拟手柄','陀螺仪'];
    const visible=a.model.normalizeBootStartupResult(legacy,legacy.boot);assert.equal(visible.status,'partial');
    assert.equal(visible.modules.input.state,'saved');assert.equal(visible.modules.gyro.state,'saved');
    q.settings.extensions.bootModesStartupResult=visible;
    const actions=q.calls.filter(row=>!['settings.update','ipc.input.shortcutRuntime.get','ipc.input.gameOverride.session'].includes(row.op)).length;
    await emit(a,evidence(a,{gyroState:'waiting'}));
    assert.equal(result(q).modules.input.state,'active');assert.equal(result(q).modules.gyro.state,'saved');
    assert.equal(q.settings.input.applyStatus,'host-acknowledged');assert.notEqual(q.settings.input.appliedRevision,q.settings.input.revision);
    assert.equal(result(q).pending.filter(row=>row.includes('本次配置的原生')).length,1);
    await emit(a);assert.equal(result(q).modules.gyro.state,'active');
    assert.equal(q.settings.input.applyStatus,'active');assert.equal(q.settings.input.appliedRevision,q.settings.input.revision);
    assert.equal(q.settings.input.appliedConfigHash,result(q).modules.input.requested.inputApplication.hash);
    assert(!result(q).pending.some(row=>row.includes('本次配置的原生')));
    const count=writes(q);for(let n=0;n<5;n++)await emit(a);assert.equal(writes(q),count);
    assert.equal(q.calls.filter(row=>!['settings.update','ipc.input.shortcutRuntime.get','ipc.input.gameOverride.session'].includes(row.op)).length,actions);
    rows.push({mode:id,separateInputGyro:true,duplicateWrites:0,businessReplays:0});
   }finally{stop();a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();}
   assert.equal(q.ipcListenerCount('gamepad.state'),0);assert.equal(q.ipcListenerCount('input.shortcutRuntime'),0);
  }
  return rows;
 });
 await test('input receipt OFF waits for real stop and never invents Host acknowledgement in four modes',async()=>{
  const rows=[];
  for(const id of ['global','xbox','steam','xbox-steam']){
   const a=await runtimeBundle(out),q=a.ports;await start(a,id,{target:false,gyro:false});const stop=a.boot.observeBootModeRuntimeReceipts();
   try{
    await settle();await emit(a,evidence(a,{inputState:'waiting'}));
    assert.equal(result(q).modules.input.state,'saved');assert.equal(result(q).modules.gyro.state,'disabled');
    await emit(a);assert.equal(result(q).modules.input.state,'disabled');assert.equal(q.settings.input.applyStatus,'safe-stop');
    assert.equal(q.settings.input.hostAcknowledgedRevision,null);assert.equal(q.settings.input.hostAcknowledgedConfigHash,null);
    assert.notEqual(q.settings.input.appliedRevision,q.settings.input.revision);rows.push({mode:id,offConfirmed:true,hostAckInvented:false});
   }finally{stop();}
  }
  return rows;
 });
 await test('input receipt rejects stale boot revision hash persona and accepts next-boot draft without adopting it',async()=>{
  if(process.argv.includes('--future-schema-boundary')){
   const rows=[];
   for(const id of ['global','xbox','steam','xbox-steam']){
    const a=await runtimeBundle(out),q=a.ports;await start(a,id);const stop=a.boot.observeBootModeRuntimeReceipts();
    try{
     await settle();q.settings.extensions.bootModes.schema=99;const before=structuredClone(q.settings),count=writes(q);
     await emit(a);assert.deepEqual(q.settings,before,'input owner must preserve future mode schema');assert.equal(writes(q),count);
     q.settings.extensions.bootModes.schema=1;await emit(a,evidence(a,{outputSequence:'22'}));
     assert.equal(result(q).modules.input.state,'active');assert.equal(result(q).modules.gyro.state,'active');
     rows.push({mode:id,futureSchemaUnchanged:true,currentSchemaRecovery:true});
    }finally{stop();stop();}
   }
   return rows;
  }
  const a=await runtimeBundle(out),q=a.ports;await start(a,'steam');const stop=a.boot.observeBootModeRuntimeReceipts();
  try{
   await settle();const count=writes(q);
   for(const patch of [{boot:'old'},{inputRevision:q.settings.input.revision-1},{inputConfigHash:'sha256:'+'a'.repeat(64)},{persona:'steamdeck'}])await emit(a,evidence(a,patch));
   assert.equal(writes(q),count);const mode=q.settings.extensions.bootModes;mode.current='xbox';await emit(a);assert.equal(writes(q),count);
   mode.current='steam';await emit(a,evidence(a,{outputSequence:'22'}));
   assert.equal(result(q).modules.input.state,'active');
   const before=structuredClone(result(q));before.modules.input={...before.modules.input,state:'saved'};delete before.modules.input.proof;
   q.settings.extensions.bootModesStartupResult=before;
   mode.revision++;mode.boot='xbox';await emit(a,evidence(a,{outputSequence:'23'}));
   assert.equal(result(q).modules.input.state,'active');assert.equal(result(q).mode,'steam');assert.equal(mode.boot,'xbox');
   return {staleProofsRejected:4,currentModeFence:true,nextBootDraftIndependent:true};
  }finally{stop();}
 });
 await test('input receipt suppresses unknown temporary owners while ignoring retired game overlays',async()=>{
  const a=await runtimeBundle(out),q=a.ports;await start(a,'global');
  let release;const held=new Promise(resolve=>release=resolve);q.hooks.shortcutRead=()=>held;
  const stop=a.boot.observeBootModeRuntimeReceipts();
  try{
   await emit(a);assert.equal(result(q).modules.input.state,'saved');
   const normal={active:false,persona:'elite',virtualEnabled:true,gyroEnabled:true};
   await q.emitMemoryIpc('input.shortcutRuntime',{...normal,active:true});release();await settle();
   await emit(a,evidence(a,{outputSequence:'22'}));assert.equal(result(q).modules.input.state,'saved');
   await q.emitMemoryIpc('input.shortcutRuntime',normal);await settle();assert.equal(result(q).modules.input.state,'saved');
   q.settings.input.gameOverride={sessionId:'fixture-native-session-1',identity:'game-1',outputTarget:{persona:'elite'},gyroMotion:{enabled:true}};
   const request=a.inputReceipts.inputApplicationRequest(q.settings.input);
   for(const row of ['input','gyro'])result(q).modules[row].requested.inputApplication=structuredClone(request);
   await emit(a);assert.equal(result(q).modules.input.state,'active');
   q.settings.input.gameOverride.sessionId='old-session';const changed=a.inputReceipts.inputApplicationRequest(q.settings.input);
   for(const row of ['input','gyro'])result(q).modules[row].requested.inputApplication=structuredClone(changed);
   await emit(a);assert.equal(result(q).modules.input.state,'active');
   assert.equal(q.calls.filter(row=>row.op==='ipc.input.gameOverride.session').length,0);
   return {unknownOwnerRejected:true,lateInitialReadRejected:true,temporaryRejected:true,matchingSessionOverlayIgnored:true,staleGameNotOwner:true};
  }finally{release();stop();}
 });
 await test('input receipt caches early proof until startup result exists and renderer restart never replays actions',async()=>{
  const a=await runtimeBundle(out),q=a.ports,stop=a.boot.observeBootModeRuntimeReceipts();let early=0;
  q.hooks.inputReceiptCommit=async()=>{
   // Claiming startup is also a receipt CAS, but still precedes input apply.
   if(early||!q.calls.some(call=>call.op==='input.cas'))return;
   assert.equal(result(q)?.modules?.input,undefined);
   early++;await q.emitMemoryIpc('gamepad.state',evidence(a));
  };
  try{
   await start(a,'global');await settle();assert.equal(early,1);assert.equal(result(q).modules.input.state,'active');
   const durable=structuredClone(q.settings);for(const module of ['input','gyro']){durable.extensions.bootModesStartupResult.modules[module].state='saved';delete durable.extensions.bootModesStartupResult.modules[module].proof;}
   const fresh=await runtimeBundle(out);Object.assign(fresh.ports.settings,durable);
   await fresh.ports.emitMemoryIpc('gamepad.state',evidence(fresh));
   const stopFresh=fresh.boot.observeBootModeRuntimeReceipts();
   try{await fresh.boot.restoreSelectedBootMode();await settle();assert.equal(result(fresh.ports).modules.gyro.state,'active');
    assert.equal(fresh.ports.calls.filter(row=>['input.cas','app.launch','monitor.apply'].includes(row.op)).length,0);
   }finally{stopFresh();}
   return {earlyProofRetained:true,rendererRestartReplays:0};
  }finally{stop();}
 });
 await test('input receipt proof survives another renderer and later startup app publication',async()=>{
  const a=await runtimeBundle(out),q=a.ports;await start(a,'steam',{delay:1});const other=await runtimeBundle(out);
  Object.assign(other.ports.settings,structuredClone(q.settings));const stop=other.boot.observeBootModeRuntimeReceipts();
  try{
   await emit(other);assert.equal(result(other.ports).modules.gyro.state,'active');
   q.settings.extensions.bootModesStartupResult=structuredClone(result(other.ports));q.settings.input=structuredClone(other.ports.settings.input);
   await new Promise(resolve=>setTimeout(resolve,1100));
   assert.equal(result(q).modules.input.state,'active');assert.equal(result(q).modules.gyro.state,'active');
   assert(!result(q).pending.some(row=>row.includes('本次配置的原生')));assert.equal(q.calls.filter(row=>row.op==='app.launch').length,1);
   return {otherRendererProofPreserved:true,delayedLaunchRequests:1,inputReplays:0};
  }finally{stop();a.boot.cancelBootModeLaunches();}
 });
 await test('input receipt commit race and disposal cannot acknowledge a newer input revision',async()=>{
  const a=await runtimeBundle(out),q=a.ports;await start(a,'global');const stop=a.boot.observeBootModeRuntimeReceipts();
  try{
   await settle();q.hooks.inputReceiptCommit=()=>{q.settings.input.revision++;};await emit(a);
   assert.equal(result(q).modules.input.state,'saved');assert.notEqual(q.settings.input.applyStatus,'active');
   const before=structuredClone(q.settings);stop();await emit(a);assert.deepEqual(q.settings,before);
   return {revisionRaceRejected:true,disposedCallbacksIgnored:true};
  }finally{stop();}
 });
 await test('input receipt late initial reads and old settings generations stay disposed',async()=>{
  const rows=[];
  for(const kind of ['dispose','generation']){
   const a=await runtimeBundle(out),q=a.ports;await start(a,'global');
   let release;const held=new Promise(resolve=>release=resolve);q.hooks.shortcutRead=()=>held;
   const stop=a.boot.observeBootModeRuntimeReceipts();
   try{
    await emit(a);const before=writes(q);
    if(kind==='dispose')stop();else q.invalidate();
    release();await settle();await emit(a,evidence(a,{outputSequence:'42'}));
    assert.equal(writes(q),before);assert.equal(result(q).modules.input.state,'saved');
    rows.push({kind,lateWrites:0});
   }finally{release();stop();}
  }
  return rows;
 });
 await test('input receipt persisted proof belongs to the same boot request and confirmed module state',async()=>{
  const a=await runtimeBundle(out),q=a.ports;await start(a,'global');await confirmInputReceipt(a);
  const valid=structuredClone(result(q));assert(a.model.normalizeBootStartupResult(valid,valid.boot));
  for(const module of ['input','gyro'])for(const fault of ['boot','hash','state','witness']){
   const broken=structuredClone(valid),row=broken.modules[module];
   if(fault==='boot')row.proof.boot='old-boot';
   if(fault==='hash')row.requested.inputApplication.hash='sha256:'+'a'.repeat(64);
   if(fault==='state')row.state='saved';
   if(fault==='witness')row.proof[module==='input'?'inputState':'gyroState']='waiting';
   assert.equal(a.model.normalizeBootStartupResult(broken,valid.boot),undefined,module+' '+fault);
  }
  return {invalidPersistedProofsRejected:8};
 });
 await test('input receipt actual repository atomically checks native revision and committed receipt delta',async()=>{
  const nativeFiles=process.argv.includes('--input-receipt-native-files');
  assert(!nativeFiles||!process.argv.some(arg=>arg==='--optional-preference-ui'||arg==='--optional-preference-owner'),'Native receipt files and optional preference scopes are mutually exclusive');
  const fixtureFactory=nativeFiles?globalThis.__bootInputReceiptFixtureFactory:createSettingsFixture;
  assert.equal(typeof fixtureFactory,'function','Explicit native receipt files require the owned native storage provider');
  if(process.argv.includes('--optional-preference-ui')){
   const {runPreferenceUiOwner}=await import('./preferenceOwner.mjs');return await runPreferenceUiOwner(out,process.argv.slice(2));
  }
  if(process.argv.includes('--optional-preference-owner')){
   const {runPreferenceOwnerCases}=await import('./preferenceOwner.mjs');return await runPreferenceOwnerCases(out,process.argv.slice(2));
  }
  const code=await compileSettingsConsumer(process.cwd()),a=await runtimeBundle(out),rows=[];
  const make=async()=>{const f=fixtureFactory(code,{});await f.repo.loadSettings();await f.repo.updateSettings(s=>{s.extensions.seeded=true;});const value=await f.repo.loadSettings();return {f,identity:a.inputReceipts.inputApplicationRequest(value.input)};};
  {
   const {f,identity}=await make(),before=f.state.writes.length;
   const saved=await f.repo.compareAndSwapInputSettings(identity.revision,{gyroMotion:{aimingSightsTrigger:''}});
   assert.equal(saved.ok,true,'A trigger normalized during readback cannot invent a conflict after the native commit');
   assert.equal(saved.value.gyroMotion.aimingSightsTrigger,null);
   assert.equal(f.document().input.gyroMotion.aimingSightsTrigger,null,'Canonical input is written before the native receipt is compared');
   const current=a.inputReceipts.inputApplicationRequest(saved.value);
   assert.equal(saved.value.pendingConfigHash,current.hash);
   assert.equal(f.document().input.pendingConfigHash,current.hash);
   assert.equal(f.state.writes.length,before+1,'Normalization must not retry an already committed CAS');
   rows.push({commit:'canonical-empty-trigger',atomicWrites:1,revision:saved.revision,readbackConflict:false});
  }
  {
   const {f,identity}=await make(),writes=f.state.writes.length;
   assert.equal(await f.repo.updateSettingsForInputReceipt(identity,s=>{s.extensions.receipt='accepted';s.input.hostAcknowledgedRevision=identity.revision;return true;}),true);
   assert.equal(f.document().extensions.receipt,'accepted');assert.equal(f.document().input.hostAcknowledgedRevision,identity.revision);
   assert.equal(f.state.writes.length,writes+1);assert.equal(f.state.writes.at(-1).expectedInputRevision,identity.revision);
   assert.equal(f.state.writes.at(-1).guardBootMode,undefined,'ordinary receipt must not acquire boot ownership');
   rows.push({commit:'same-version',atomicWrites:1});
  }
  {
   const {f,identity}=await make(),before=f.state.writes.length;
   f.state.beforeWrite=()=>{const next=f.document();next.input.revision++;f.seed(f.state.file,next);};
   assert.equal(await f.repo.updateSettingsForInputReceipt(identity,s=>{s.extensions.receipt='stale';return true;}),false);
   assert.equal(f.document().extensions.receipt,undefined);assert.equal(f.document().input.revision,identity.revision+1);
   assert.equal(f.state.writes.length,before);rows.push({commit:'native-revision-race',rejectedBeforeWrite:true});
  }
  for(const field of ['extensions','input']){
   const {f,identity}=await make();
   f.state.beforeWrite=args=>{const draft=JSON.parse(args.content);if(field==='extensions')delete draft.extensions.receipt;else draft.input.hostAcknowledgedRevision=null;args.content=JSON.stringify(draft);};
   await assert.rejects(f.repo.updateSettingsForInputReceipt(identity,s=>{s.extensions.receipt='expected';s.input.hostAcknowledgedRevision=identity.revision;return true;}),/未确认/);
   rows.push({commit:'missing-'+field+'-delta',notAccepted:true});
  }
  for(const kind of ['hash','mode','ack']){
   const {f,identity}=await make(),before=f.state.writes.length;let observed;
   f.state.beforeWrite=()=>{const next=f.document();if(kind==='hash')next.input.diagnostics.inputLoggingEnabled=!next.input.diagnostics.inputLoggingEnabled;if(kind==='mode')next.extensions.bootModes={schema:1,revision:99,current:'xbox'};if(kind==='ack')next.input.hostAcknowledgedRevision=identity.revision;f.seed(f.state.file,next);};
   const accepted=await f.repo.updateSettingsForInputReceipt(identity,s=>{s.extensions.receipt='boot-owned';return true;},current=>{if(current)observed=structuredClone(current);return true;},{origin:'boot-mode'});
   assert.equal(accepted,kind==='ack');assert.equal(f.state.writes.length,before+(kind==='ack'?1:0));
   if(kind==='ack')assert.equal(f.state.writes.at(-1).guardBootMode,true);else{assert(observed,'native conflict document must reach current owner callback');assert.equal(f.document().extensions.receipt,undefined);if(kind==='mode')assert.equal(observed.extensions.bootModes.revision,99);else assert.equal(observed.input.diagnostics.inputLoggingEnabled,true);}
   rows.push({commit:'boot-receipt-'+kind,bootGuardForwarded:kind==='ack',accepted});
  }
  {
   const {f,identity}=await make();await assert.rejects(f.repo.updateSettingsForInputReceipt(identity,s=>{s.input.outputTarget.persona='elite';return true;}),/不得修改配置/);
   const before=f.state.writes.length;assert.equal(await f.repo.updateSettingsForInputReceipt(identity,()=>false),false);assert.equal(f.state.writes.length,before);
  }
  return rows;
 });
}
