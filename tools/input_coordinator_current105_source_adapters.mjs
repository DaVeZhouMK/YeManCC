import {terminal26CompileSourceProfile} from './input_coordinator_terminal26_source_profile.mjs';
import {terminal22SteamCompileSpec,terminal23CompileSourceProfile} from './input_coordinator_terminal22_source_adapters.mjs';
import fs from'node:fs';import assert from'node:assert/strict';import{createHash}from'node:crypto';import{extract}from'./fixtures/legion_motion_consumer/extract_main.mjs';
import {fileURLToPath} from 'node:url';
const explicit26='current105-terminal26-sdk-lifetime-compact-v1';
function requireNativeSourceProfileContract(managedInputContract,nativeSourceProfile){
 if(nativeSourceProfile!==undefined)assert.equal(managedInputContract,explicit26,'NATIVE_SOURCE_PROFILE_REQUIRES_EXPLICIT26');
}

const sha=b=>createHash('sha256').update(b).digest('hex'),repo=fileURLToPath(new URL('../',import.meta.url)),profile=JSON.parse(fs.readFileSync(new URL('./ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json',import.meta.url),'utf8')),spec={...profile,steamRecovery:{...profile.steamRecovery,runtimeHeader:{...profile.steamRecovery.runtimeHeader,path:repo+profile.steamRecovery.runtimeHeader.relative}}};

export function currentCompileSourceDataProfile({sourceRoot,managedInputContract,nativeSourceProfile}={}){
 requireNativeSourceProfileContract(managedInputContract,nativeSourceProfile);
 assert(managedInputContract===undefined||managedInputContract==='current105-exact21-v1'||managedInputContract==='current105-terminal22-v1'||managedInputContract==='current105-terminal23-sd-decoder-v1'||managedInputContract===explicit26,'UNKNOWN_COMPILE_SOURCE_CONTRACT');
 if(managedInputContract===explicit26)return terminal26CompileSourceProfile(sourceRoot,{nativeSourceProfile});
 return managedInputContract==='current105-terminal23-sd-decoder-v1'?terminal23CompileSourceProfile(sourceRoot):spec;
}
export function guardCurrentSteam(original,{sourceRoot,managedInputContract,nativeSourceProfile}={}){
 requireNativeSourceProfileContract(managedInputContract,nativeSourceProfile);
 assert(managedInputContract===undefined||managedInputContract==='current105-exact21-v1'||managedInputContract==='current105-terminal22-v1'||managedInputContract==='current105-terminal23-sd-decoder-v1'||managedInputContract===explicit26,'UNKNOWN_COMPILE_SOURCE_CONTRACT');
 const current23=managedInputContract==='current105-terminal23-sd-decoder-v1'||managedInputContract===explicit26;
 const guardSpec=managedInputContract===explicit26?terminal26CompileSourceProfile(sourceRoot,{nativeSourceProfile}):current23?terminal23CompileSourceProfile(sourceRoot):managedInputContract==='current105-terminal22-v1'?terminal22SteamCompileSpec(sourceRoot):spec;
 let inert=original;const header='#include "steam_original_recovery_runtime.h"';assert.equal(inert.split(header).length,2);
 assert.equal(sha(fs.readFileSync(guardSpec.steamRecovery.runtimeHeader.path)),guardSpec.steamRecovery.runtimeHeader.sha256);
 const actualHeader=fs.readFileSync(guardSpec.steamRecovery.runtimeHeader.path,'utf8');assert.equal(sha(extract(actualHeader,'steamOriginalRecoveryStart').code),guardSpec.steamRecovery.startSha256);
 const startWrapper='static void steamOriginalRecoveryStart(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-start");}';
 const exportWrapper='static std::wstring steamOriginalRecoveryExportSnapshot(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-export");}';
 if(current23)assert.equal(sha(extract(actualHeader,'steamOriginalRecoveryExportSnapshot').code),guardSpec.steamRecovery.exportSnapshotSha256);
 const replacement=current23?['#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart','#define steamOriginalRecoveryExportSnapshot fixtureNeverCallOriginalSteamRecoveryExportSnapshot',header,'#undef steamOriginalRecoveryExportSnapshot','#undef steamOriginalRecoveryStart',startWrapper,exportWrapper].join('\n')
  :['#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart',header,'#undef steamOriginalRecoveryStart',startWrapper].join('\n');
 inert=inert.replace(header,replacement);const bridge=extract(inert,'steamOriginalRecoveryEngineHandle').code;
 assert.equal(sha(bridge.replaceAll('\r\n','\n')),guardSpec.steamRecovery.bridgeSha256);
 const changed=bridge.slice(0,bridge.indexOf('{')+1)+'\n++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-engine");\n}';
 assert.equal(inert.split(bridge).length,2);inert=inert.replace(bridge,changed);
 const inverse=inert.replace(changed,()=>bridge).replace(replacement,()=>header);assert.equal(inverse,original);
 const substitutions=[{symbol:'steamOriginalRecoveryStart',beforeSha256:guardSpec.steamRecovery.startSha256,afterSha256:sha(replacement),rejectOnly:true,scope:'Rename only real included Start definition; public entry throws for offline test'},{symbol:'steamOriginalRecoveryEngineHandle',beforeSha256:sha(bridge),afterSha256:sha(changed),rejectOnly:true,scope:'Every recovery engine bridge call throws in offline test'}];
 if(current23)substitutions.push({symbol:'steamOriginalRecoveryExportSnapshot',beforeSha256:guardSpec.steamRecovery.exportSnapshotSha256,afterSha256:sha(exportWrapper),rejectOnly:true,scope:'Rename the real included snapshot exporter; public entry throws before independent saveJson'});
 return {inert,nativeGuardProvenance:guardSpec.nativeGuardProvenance??null,substitutions};
}

// Explicit26 overlays only reviewed fixture bytes over its immutable product capture.
// Unknown files, changed old inputs, or drifted reviewed outputs are refused.
export function currentFixtureSourceOverride(relative,original,options={}){
 requireNativeSourceProfileContract(options.managedInputContract,options.nativeSourceProfile);
 assert(options.managedInputContract===undefined||['current105-exact21-v1','current105-terminal22-v1','current105-terminal23-sd-decoder-v1',explicit26].includes(options.managedInputContract),'UNKNOWN_COMPILE_SOURCE_CONTRACT');
 assert(Buffer.isBuffer(original),'FIXTURE_OVERRIDE_BYTES_REQUIRED');
 if(options.managedInputContract!==explicit26)return original;
 if(relative==='tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc')return repairCurrent105TupleFixtureInputs(original,options);
 const profile=currentCompileSourceDataProfile(options),row=profile.fixtureSourceOverrides.find(row=>row.relative===relative);assert(row,'UNKNOWN_REVIEWED_FIXTURE_OVERRIDE');
 assert.equal(original.length,row.before.bytes);assert.equal(sha(original),row.before.sha256,'FIXTURE_OVERRIDE_ORIGINAL_CHANGED');
 let text=original.toString('utf8');
 for(const edit of row.changes){assert.equal(text.split(edit.before).length,2,'FIXTURE_OVERRIDE_ANCHOR_CHANGED');text=text.replace(edit.before,()=>edit.after);}
 const bytes=Buffer.from(text);assert.equal(bytes.length,row.after.bytes);assert.equal(sha(bytes),row.after.sha256,'FIXTURE_OVERRIDE_OUTPUT_CHANGED');
 let inverse=text;for(const edit of [...row.changes].reverse()){assert.equal(inverse.split(edit.after).length,2);inverse=inverse.replace(edit.after,()=>edit.before);}assert(Buffer.from(inverse).equals(original));
 return bytes;
}

export function observeCurrent105Fixture(original,options={}){const observationSpec=currentCompileSourceDataProfile(options);assert.equal(sha(original),observationSpec.fixture.sha256,'Exact captured current fixture');const review={changes:observationSpec.fixtureObservationChanges};let text=original;const applied=[];for(const row of review.changes){const lfBefore=row.before.replaceAll('\r\n','\n'),lfAfter=row.after.replaceAll('\r\n','\n'),beforeCandidates=[...new Set([lfBefore,lfBefore.replaceAll('\n','\r\n')])],hits=beforeCandidates.flatMap(before=>Array.from({length:text.split(before).length-1},()=>before));assert.equal(hits.length,row.count,'Actual unchanged observation anchor '+row.label);for(const before of hits){const after=before.includes('\r\n')?lfAfter.replaceAll('\n','\r\n'):lfAfter;text=text.replace(before,()=>after);applied.push({before,after});}}let inverse=text;for(const row of [...applied].reverse())inverse=inverse.replace(row.after,()=>row.before);assert.equal(inverse,original);if(observationSpec.nativeSourceProfile){assert.equal(Buffer.byteLength(text),observationSpec.fixtureObservedOutput.bytes,'CURRENT_CAPTURE_OBSERVATION_OUTPUT_SIZE_REQUIRED');assert.equal(sha(text),observationSpec.fixtureObservedOutput.sha256,'CURRENT_CAPTURE_OBSERVATION_OUTPUT_PIN_REQUIRED');}return {text,originalSha256:sha(original),observedSha256:sha(text),originalBusinessChecksChanged:false,...(observationSpec.nativeSourceProfile?{nativeSourceProfile:observationSpec.nativeSourceProfile}:{})};}

// Exact fixture input repairs applied after the immutable V2 observation.
// No current mainline fixture is imported and no captured/source profile pin changes.
export function repairCurrent105LifecycleFixtureInputs(observation,options={}){
 requireNativeSourceProfileContract(options.managedInputContract,options.nativeSourceProfile);
 if(options.managedInputContract!==explicit26)return {...observation,lifecycleFixtureInputRepair:null};
 const profile=currentCompileSourceDataProfile(options);
 assert.equal(observation.originalSha256,profile.fixture.sha256,'LIFECYCLE_INPUT_REPAIR_ORIGINAL_PIN_REQUIRED');
 if(profile.nativeSourceProfile){
  const identity=profile.lifecycleFixtureInputRepair;
  assert.equal(Buffer.byteLength(observation.text),identity.input.bytes,'CURRENT_CAPTURE_LIFECYCLE_INPUT_SIZE_REQUIRED');
  assert.equal(sha(observation.text),identity.input.sha256,'CURRENT_CAPTURE_LIFECYCLE_INPUT_PIN_REQUIRED');
  assert.deepEqual(identity.input,identity.output);assert.deepEqual(identity.changes,[]);
  return {...observation,lifecycleFixtureInputRepair:{...identity,inputSha256:identity.input.sha256,outputSha256:identity.output.sha256,
   currentMainlineFixtureImported:false,nativeSourceProfile:profile.nativeSourceProfile}};
 }

 const expectedBefore="fc8652a65c4c5722aa0f56d888ae7484059554108d667336b0425a6d5226378c",expectedAfter="52f45d6fdd5c0e95869d2539cc2f63efa100053f8034084c50f1c20c5453fb2a";
 assert.equal(sha(observation.text),expectedBefore,'LIFECYCLE_INPUT_REPAIR_EXACT_OBSERVED_SOURCE_REQUIRED');
 const changes=[
 {
  "label": "actual cached radial isolation bound after actual Host start",
  "before": "  // This existing cached UI-owner route makes gamepadUiInputEligible return\n  // before any HWND/window checks. Never show/close the radial UI or send input.\n  struct CachedRadialOwnerScope{bool previous=g_radialUiOwnsInput.exchange(true,std::memory_order_acq_rel);~CachedRadialOwnerScope(){g_radialUiOwnsInput.store(previous,std::memory_order_release);}} radialOwner;\n  check(radialHasInputOwner()&&gamepadUiInputEligible(),\"cached radial ownership supplies actual foreground-neutral eligibility without window operations\");\n  check(inputHostStart(),\"foreground-neutral scenario establishes its actual owned Host\");",
  "after": "  // Establish the actual Host first, then acquire the existing cached isolation\n  // owner bound to that Host epoch/source/power. No HWND/UI or input operations.\n  check(inputHostStart(),\"foreground-neutral scenario establishes its actual owned Host\");\n  struct CachedRadialOwnerScope{CachedRadialOwnerScope(){radialInputIsolationOwnership(true);}~CachedRadialOwnerScope(){radialInputIsolationInvalidate();}} radialOwner;\n  check(radialHasInputOwner()&&gamepadUiInputEligible(),\"cached radial ownership supplies actual foreground-neutral eligibility without window operations\");"
 },
 {
  "label": "empty owned-readback fails before invalid iterator",
  "before": " ++fixturePowerClockOffset;const auto newer=captureLocalReadback();auto hidden=newer.targets;hidden.erase(hidden.begin());",
  "after": " ++fixturePowerClockOffset;const auto newer=captureLocalReadback();auto hidden=newer.targets;\n if(hidden.empty())throw std::runtime_error(\"owned-readback-newer-query-targets-missing\");\n hidden.erase(hidden.begin());"
 },
 {
  "label": "preserve real MSI policy-return start diagnostics before cleanup",
  "before": "const bool started=inputHostStart();check(started,\"same-intent recovery can execute its serialized real start during user cooldown\");",
  "after": "const bool started=inputHostStart();cases.back()[\"startEvents\"]=fixtureEvents;cases.back()[\"startTrace\"]=g_startEntrySeam.trace;check(started,\"same-intent recovery can execute its serialized real start during user cooldown\");"
 },
 {
  "label": "native ordinary switch owns only its per-case CLI bookkeeping scope",
  "before": "static std::wstring fixtureTargetReturnPath;",
  "after": "static bool fixtureOrdinarySwitchCliScope=false;\nstatic std::wstring fixtureTargetReturnPath;"
 },
 {
  "label": "shared private CLI path admits explicit native switch scope without claiming full pipe",
  "before": " if(!fixtureFullPipe)throw std::logic_error(\"ordinary-full-pipe-cli-scope-required\");",
  "after": " if(!fixtureFullPipe && !fixtureOrdinarySwitchCliScope)throw std::logic_error(\"ordinary-full-pipe-cli-scope-required\");\n if(fixtureOrdinarySwitchCliScope && (fixtureFullPipe || fixtureColdArchive || fixtureRuntimeRecoveryOsEnabled))\n  throw std::logic_error(\"ordinary-native-switch-cli-role-conflict\");"
 },
 {
  "label": "native switch CLI ledger has a separate validated per-case filename",
  "before": " return record.wstring()+L\".full-pipe-cli\";",
  "after": " return record.wstring()+(fixtureOrdinarySwitchCliScope?L\".native-switch-cli\":L\".full-pipe-cli\");"
 },
 {
  "label": "switch scope follows original function lifetime across seed and exceptions",
  "before": "static void switchRecoveryCases(int cliBoundaryOnly=0){",
  "after": "static void switchRecoveryCases(int cliBoundaryOnly=0){\n struct OrdinarySwitchCliScope {\n  bool previous=fixtureOrdinarySwitchCliScope;\n  OrdinarySwitchCliScope(){if(fixtureFullPipe || fixtureColdArchive || fixtureRuntimeRecoveryOsEnabled)throw std::logic_error(\"ordinary-switch-cli-scope-role\");fixtureOrdinarySwitchCliScope=true;}\n  ~OrdinarySwitchCliScope(){fixtureOrdinarySwitchCliScope=previous;}\n } ordinarySwitchCliScope;"
 },
 {
  "label": "explicit remaining-scene selector does not enter old CLI-only branch",
  "before": "if(cliBoundaryOnly){\n for(const std::string variant:",
  "after": "if(cliBoundaryOnly && cliBoundaryOnly!=4){\n for(const std::string variant:"
 },
 {
  "label": "unselected prefix original scenes remain unchanged and do not execute in directed mode",
  "before": " {\n cases.push_back({{\"name\",\"central-switch-lifecycle-version-and-single-fallback-clock\"}});",
  "after": " if(cliBoundaryOnly!=4){\n {\n cases.push_back({{\"name\",\"central-switch-lifecycle-version-and-single-fallback-clock\"}});"
 },
 {
  "label": "remaining selection takes only original MSI resume parameter",
  "before": " for(const int family:{1,2,0})for(const std::string condition:{\"resume\",\"return-hook-guards\",",
  "after": " }\n for(const int family:{1,2,0})for(const std::string condition:{\"resume\",\"return-hook-guards\","
 },
 {
  "label": "skip only unselected family condition before original branch",
  "before": " if(family!=1&&condition!=\"resume\")continue;",
  "after": " if(cliBoundaryOnly==4 && (family!=0 || condition!=\"resume\"))continue;\n if(family!=1&&condition!=\"resume\")continue;"
 },
 {
  "label": "unselected local scenes and first root fallback are excluded without copying",
  "before": " {\n cases.push_back({{\"name\",\"actual-local-owned-hidden-missing-single-fallback\"}});",
  "after": " if(cliBoundaryOnly!=4){\n {\n cases.push_back({{\"name\",\"actual-local-owned-hidden-missing-single-fallback\"}});"
 },
 {
  "label": "original ordinary root repair body remains selected verbatim",
  "before": " {\n cases.push_back({{\"name\",\"actual-owned-root-refresh-ordinary-repair-revokes-unspent-offer\"}});",
  "after": " }\n {\n cases.push_back({{\"name\",\"actual-owned-root-refresh-ordinary-repair-revokes-unspent-offer\"}});"
 },
 {
  "label": "unselected root negative and following normal sets keep one unchanged implementation",
  "before": " for(const std::string condition:{\"refresh-issued\",\"refresh-result-unknown\",\"configuration-unapplied\",",
  "after": " if(cliBoundaryOnly!=4){\n for(const std::string condition:{\"refresh-issued\",\"refresh-result-unknown\",\"configuration-unapplied\","
 },
 {
  "label": "original OFF case remains selected after remaining guard group closes",
  "before": " cases.push_back({{\"name\",\"actual-unique-OFF-verification-exit\"}});prepare();",
  "after": " }\n cases.push_back({{\"name\",\"actual-unique-OFF-verification-exit\"}});prepare();"
 },
 {
  "label": "original native parser accepts only the explicit known remaining case",
  "before": "selectedCase!=L\"case:serial-config-action\"))return 2;",
  "after": "selectedCase!=L\"case:serial-config-action\"&&selectedCase!=L\"case:switch-return-boundaries\"))return 2;"
 },
 {
  "label": "existing switch mode dispatches one parameterization instead of default plus selection",
  "before": "if(selectedCase==L\"case:source-reconfirm-collection\")sourceReconfirmCollectionCases();",
  "after": "if(selectedCase==L\"case:switch-return-boundaries\")switchRecoveryCases(4);else if(selectedCase==L\"case:source-reconfirm-collection\")sourceReconfirmCollectionCases();"
 }
];
 let text=observation.text;for(const edit of changes){assert.equal(text.split(edit.before).length,2);text=text.replace(edit.before,()=>edit.after);}
 assert.equal(sha(text),expectedAfter,'LIFECYCLE_INPUT_REPAIR_OUTPUT_PIN_REQUIRED');
 let inverse=text;for(const edit of [...changes].reverse())inverse=inverse.replace(edit.after,()=>edit.before);assert.equal(inverse,observation.text);
 return {...observation,text,lifecycleFixtureInputRepair:{kind:'EXACT_CAPTURED_LIFECYCLE_INPUT_REPAIR_V1',inputSha256:expectedBefore,outputSha256:expectedAfter,changes:changes.map(({label,before,after})=>({label,beforeSha256:sha(before),afterSha256:sha(after)})),sourceCaptureModified:false,currentMainlineFixtureImported:false,assertionsRemoved:0,originalAssertionExpressionsAndNamesPreserved:true,fullParentAccepted:false}};
}

// Exact original tuple fixture repair; fixed capture and original45 cases remain.
export function repairCurrent105TupleFixtureInputs(original,options={}){
 requireNativeSourceProfileContract(options.managedInputContract,options.nativeSourceProfile);
 if(options.managedInputContract!==explicit26)return original;
 const profile=currentCompileSourceDataProfile(options);
 assert(Buffer.isBuffer(original),'CURRENT_TUPLE_FIXTURE_BUFFER_REQUIRED');
 if(profile.nativeSourceProfile){
  const tuple=profile.tupleFixture;
  assert.equal(tuple.mode,'exact-original13-after-identity-v1');
  assert.equal(original.length,tuple.before.bytes,'CURRENT_CAPTURE_TUPLE_INPUT_SIZE_REQUIRED');
  assert.equal(sha(original),tuple.before.sha256,'CURRENT_CAPTURE_TUPLE_INPUT_PIN_REQUIRED');
  assert.deepEqual(tuple.before,tuple.after);assert.deepEqual(tuple.changes,[]);
  return original;
 }
 assert.equal(original.length,59089,'CURRENT_TUPLE_FIXTURE_INPUT_SIZE_REQUIRED');
 assert.equal(sha(original),"977e11bf6ea13a56e2ce2b1260a2dd915f8c403d29e03fa8ac940b5488122ccb",'CURRENT_TUPLE_FIXTURE_INPUT_PIN_REQUIRED');
 const changes=[
 {
  "label": "single original nextProbe observation helper with existing clocks and fresh source",
  "before": "    const auto runtimeTick = [&]() { inputHostRecoveryMaintenanceTick(); drain(); };",
  "after": "    const auto runtimeTick = [&]() { inputHostRecoveryMaintenanceTick(); drain(); };\n    // Simulate one already-scheduled production observation after exact return.\n    // Do not roll nextProbe, request/cooldown deadlines, owner state or quotas.\n    const auto observeNextRuntimeProbe = [&]() {\n        const auto& r=ymcc::inputHost::runtimeRecovery::state;\n        if(!r.pending || r.queued || r.spent)return;\n        const auto now=GetTickCount64();\n        if(!r.nextProbe || r.nextProbe<=now)return;\n        const auto delta=r.nextProbe-now;\n        if(delta>1000)throw std::logic_error(\"runtime-fixture-probe-outside-original-cadence\");\n        cases.back()[\"nextScheduledProbe\"]={{\"beforeTick\",now},{\"originalNextProbe\",r.nextProbe},\n            {\"advanceMs\",delta},{\"oldRequestId\",r.requestId},{\"recoveryRequestBefore\",r.recoveryRequestId}};\n        fixturePowerClockOffset+=delta;startProtectionClock+=delta;\n        // Existing verified source reader supplies the sample at the new time;\n        // this does not create a frame receipt, health credit or device proof.\n        readCurrent();runtimeTick();\n        cases.back()[\"nextScheduledProbe\"][\"pendingAfter\"]=r.pending;\n        cases.back()[\"nextScheduledProbe\"][\"recoveryRequestAfter\"]=r.recoveryRequestId;\n    };"
 },
 {
  "label": "after intentional pause observations, before original positive successor assertions",
  "before": "                    const auto newTicket=runtime.recoveryRequestId;",
  "after": "                    observeNextRuntimeProbe();\n                    const auto newTicket=runtime.recoveryRequestId;"
 },
 {
  "label": "actual demand revocation API under original persona mutex",
  "before": "    const auto runtimeTick = [&]() { inputHostRecoveryMaintenanceTick(); drain(); };",
  "after": "    const auto runtimeTick = [&]() { inputHostRecoveryMaintenanceTick(); drain(); };\n    const auto revokeRecoveryDemand = []() {\n        std::lock_guard<std::mutex> personaLock(g_inputHostPersonaMx);\n        g_inputHostRecoveryIntent.invalidate();\n    };"
 },
 {
  "label": "cancel-intent supplies actual one-way demand revocation in addition to old snapshot publication",
  "before": "                if(condition==\"cancel-intent\")g_inputHostVirtualIntentRevision.fetch_add(1);",
  "after": "                if(condition==\"cancel-intent\"){g_inputHostVirtualIntentRevision.fetch_add(1);revokeRecoveryDemand();}"
 },
 {
  "label": "last quiescence writer invalidates actual demand while preserving original publication counter observation",
  "before": "                        else g_inputHostVirtualIntentRevision.fetch_add(1);",
  "after": "                        else {g_inputHostVirtualIntentRevision.fetch_add(1);revokeRecoveryDemand();}"
 },
 {
  "label": "successful bounded recovery retains real monitoring publication; no new pending action",
  "before": "                        !g_inputHostRuntimeRecoveryPending.load() && createCount()==2 && stopCount()==1 &&",
  "after": "                        g_inputHostRuntimeRecoveryPending.load() && runtime.monitoring && createCount()==2 && stopCount()==1 &&"
 },
 {
  "label": "capture original creation counter and terminal serial before fault",
  "before": "        const auto now=GetTickCount64();\n        if(condition==\"episode-exhausted\")",
  "after": "        const auto oldEpochCounter=g_inputHostEpochCounter.load();\n        const auto terminalSerialBefore=ymcc::inputHost::recoveryFallback::nextSerial;\n        const auto now=GetTickCount64();\n        if(condition==\"episode-exhausted\")"
 },
 {
  "label": "source-defined terminal policy keeps all seven budget fields",
  "before": "        const auto budgetBefore=budgetView();\n        g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;",
  "after": "        const auto budgetBefore=budgetView();\n        const bool sharedTerminalCase=std::strcmp(parameter.command,\"SUBMIT_FRAME\")==0 &&\n            (condition==\"episode-exhausted\" || condition==\"rolling-exhausted\");\n        auto expectedBudget=budgetBefore;\n        if(sharedTerminalCase){expectedBudget[\"terminal\"]=true;expectedBudget[\"fallbackState\"]=3;}\n        g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;"
 },
 {
  "label": "observe exact terminal Stop owner and immutable no-replay identity under original lock",
  "before": "            cases.back()[\"offeredRuntime\"]=runtimeView();\n            runtimeTick();",
  "after": "            const auto terminalFactSnapshot=[&]() {\n                std::lock_guard<InputHostMutex> lock(g_inputHostMx);\n                const auto& f=ymcc::inputHost::recoveryFallback::request;\n                return json{{\"serial\",f.serial},{\"nextSerial\",ymcc::inputHost::recoveryFallback::nextSerial},\n                    {\"request\",f.request},{\"power\",f.power},{\"demand\",f.demand},{\"persona\",f.persona},\n                    {\"epoch\",f.epoch},{\"epochCounter\",f.epochCounter},{\"run\",f.run},{\"instance\",f.instance},\n                    {\"stopInvoked\",f.stopInvoked},{\"deadline\",f.deadline},{\"stoppedAt\",f.stoppedAt}};\n            };\n            const auto terminalStoppedForOldOwner=[&]() {\n                std::lock_guard<InputHostMutex> lock(g_inputHostMx);\n                const auto& f=ymcc::inputHost::recoveryFallback::request;\n                return f.serial==terminalSerialBefore+1 && f.serial &&\n                    ymcc::inputHost::recoveryFallback::nextSerial==f.serial &&\n                    f.request==oldTicket && f.power==oldPower && f.demand==runtime.demandRevision &&\n                    f.persona==oldPersona && f.epoch==oldEpoch && f.epochCounter==oldEpochCounter &&\n                    g_inputHostEpochCounter.load()==oldEpochCounter && f.run==oldRun &&\n                    f.instance==oldInstance && f.stopInvoked && runtime.serial==serial &&\n                    !runtime.pending && !runtime.queued && runtime.spent && !runtime.stopInvoked &&\n                    !g_inputHostRuntimeRecoveryPending.load();\n            };\n            cases.back()[\"offeredRuntime\"]=runtimeView();\n            runtimeTick();\n            const auto terminalWaiting=sharedTerminalCase?terminalFactSnapshot():json();"
 },
 {
  "label": "same one Stop assertion dispatches to exact selected cleanup owner",
  "before": "            check(stopCount()==1 && traced(\"stop:runtime-transport-recovery\") && createCount()==1 &&\n                runtime.stopInvoked && !runtime.spent && !runtime.recoveryRequestId && budgetView()==budgetBefore,",
  "after": "            check(stopCount()==1 && traced(sharedTerminalCase?\"stop:recovery-exhausted-physical-fallback\":\n                \"stop:runtime-transport-recovery\") && createCount()==1 &&\n                (sharedTerminalCase?terminalStoppedForOldOwner():(runtime.stopInvoked && !runtime.spent)) &&\n                !runtime.recoveryRequestId && budgetView()==expectedBudget,"
 },
 {
  "label": "duplicate proof keeps exact counts and captured terminal identity",
  "before": "            check(runtimeView()==waiting && stopCount()==1 && createCount()==1 && budgetView()==budgetBefore &&\n                fixtureRuntimeRecoveryCensusCalls==pnpBefore && fixtureRuntimeRecoveryWriterCalls==writerBefore,",
  "after": "            check(runtimeView()==waiting && stopCount()==1 && createCount()==1 && budgetView()==expectedBudget &&\n                (!sharedTerminalCase || (terminalStoppedForOldOwner() && terminalFactSnapshot()==terminalWaiting)) &&\n                fixtureRuntimeRecoveryCensusCalls==pnpBefore && fixtureRuntimeRecoveryWriterCalls==writerBefore,"
 },
 {
  "label": "exhaustion retains no ticket charge restart or second Stop and original terminal deadline",
  "before": "                    check(!runtime.recoveryRequestId && createCount()==1 && stopCount()==1 && budgetView()==budgetBefore,",
  "after": "                    check(!runtime.recoveryRequestId && createCount()==1 && stopCount()==1 && budgetView()==expectedBudget &&\n                        (!sharedTerminalCase || (terminalStoppedForOldOwner() && terminalFactSnapshot()==terminalWaiting)),"
 },
 {
  "label": "record actual terminal observations without changing product state",
  "before": "            cases.back()[\"observedRuntime\"]=runtimeView();",
  "after": "            cases.back()[\"observedRuntime\"]=runtimeView();\n            if(sharedTerminalCase){cases.back()[\"terminalBeforeDuplicates\"]=terminalWaiting;cases.back()[\"observedTerminal\"]=terminalFactSnapshot();cases.back()[\"expectedTerminalBudget\"]=expectedBudget;}"
 }
];
 let text=original.toString('utf8');for(const edit of changes){assert.equal(text.split(edit.before).length,2,'CURRENT_TUPLE_FIXTURE_REPAIR_ANCHOR_REQUIRED');text=text.replace(edit.before,()=>edit.after);}
 const output=Buffer.from(text);assert.equal(output.length,63272,'CURRENT_TUPLE_FIXTURE_OUTPUT_SIZE_REQUIRED');
 assert.equal(sha(output),"98a0f8f817c5c48399272b308fe79c3290b7b5c9531ff8b8f948914dfab99104",'CURRENT_TUPLE_FIXTURE_OUTPUT_PIN_REQUIRED');
 let inverse=text;for(const edit of [...changes].reverse())inverse=inverse.replace(edit.after,()=>edit.before);assert(Buffer.from(inverse).equals(original));
 return output;
}
