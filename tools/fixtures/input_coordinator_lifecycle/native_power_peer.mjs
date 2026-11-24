import {extract as extractFixtureSource} from '../legion_motion_consumer/extract_main.mjs';
import {sharedInputOwnerObservationSources,verifyMaterializedSharedInputPowerOwners} from './public_power_ownership.mjs';
// Exact fixture-only source pairing. This grants no runtime/source authority.
import fs from'node:fs';import path from'node:path';import assert from'node:assert/strict';import{createHash}from'node:crypto';
const contract='current105-terminal29-shared-input-power-v1',header='tools/native_power_peer_fixture.h',headerSha256='1ff196cd02f318bdfb9cb459d268de8670a4c6532bd19c04ee399a1b5c31b601',tupleSha256='98a0f8f817c5c48399272b308fe79c3290b7b5c9531ff8b8f948914dfab99104',sha=b=>createHash('sha256').update(b).digest('hex');
export function nativePowerPeerSourceFiles(){return[header];}
function source(root){const p=path.join(root,header),st=fs.lstatSync(p);assert(st.isFile()&&!st.isSymbolicLink());const b=fs.readFileSync(p);assert.equal(sha(b),headerSha256,'NATIVE_PEER_HEADER_CHANGED');return b;}
export function nativePowerPeerInertPorts(){return[
 ['ymStartCreateProcessW','if(fixtureNativePeerEnabled&&!fixtureFullPipe)return fixtureCreateNativePeer(si,pi);'],
 ['ymStartHostExitWait','if(fixtureNativePeerEnabled&&!fixtureFullPipe)return fixtureNativePeerExitWait(process);'],
 ['ymStartPipeSeamServe','if(fixtureNativePeerEnabled&&!fixtureFullPipe&&ymStartSeamActive()&&command){if(std::strcmp(command,"HELLO")==0&&g_startEntrySeam.pipeFailCommand!="HELLO"&&g_startEntrySeam.pipeThrowCommand!="HELLO"){if(!fixtureFindNativePeer(g_inputHostProcess))throw std::logic_error("private-HELLO-without-held-real-peer");g_inputHostHostInstanceId="fixture-native-peer-"+std::to_string(g_inputHostEpoch)+"-"+g_inputHostRunId;}if(!fixtureRuntimeRecoveryFaultEnabled&&std::string(command)==g_startEntrySeam.pipeFailCommand)fixtureNativePeerTransportFaultPending=true;}'],
 ['inputHostWaitClientLocked',"if(fixtureRuntimeRecoveryFaultEnabled && !fixtureRuntimeRecoveryFaultConsumed && (!fixtureRuntimeRecoveryFirstHelloOnly || g_inputHostSwitchRecoveryStartRequestId!=0)){fixtureRuntimeRecoveryFaultConsumed=true;++fixtureRuntimeRecoveryFaultCalls;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError;return false;}if(fixtureNativePeerEnabled&&fixtureNativePeerTransportFaultPending){fixtureNativePeerTransportFaultPending=false;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError?fixtureStartupTransportError:ERROR_BROKEN_PIPE;return false;}"],
 ['inputHostPipeIo',"if(fixtureRuntimeRecoveryFaultEnabled && !fixtureRuntimeRecoveryFaultConsumed && !fixtureRuntimeRecoveryFirstHelloOnly){fixtureRuntimeRecoveryFaultConsumed=true;++fixtureRuntimeRecoveryFaultCalls;if(transferredOut)*transferredOut=0;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError;return false;}"],
 ['inputHostTargetReturnProbePhysicalWriter','if(fixtureNativePeerEnabled)return fixtureRuntimeRecoveryWriter(owner);'],
 ['inputHostTargetReturnColdAbsence','if(fixtureNativePeerEnabled)return fixtureRuntimeRecoveryAbsence(knownIds);'],
 ['hidHideCliWriterRecordPath','if(fixtureNativePeerEnabled){if(fixtureRuntimeRecoveryOsEnabled){fixtureRuntimeRecoveryWriterPathBoundary();return fixtureTargetReturnRecordPath()+L".runtime-cli";}return fixtureTargetReturnRecordPath()+L".native-peer-cli";}'],
 ['hidHideCliWriterMachineBinding','if(fixtureNativePeerEnabled)return std::string("fixture-machine");'],
 ['ymStartAwaitMsiSource','if(fixtureMsiSourceEntry){auto input=std::move(fixtureMsiSourceEntry);fixtureMsiSourceEntry={};input();}','    const auto requestPower=t_inputHostRequestPower;']
];}
export function observeNativePowerPeerFixture({fixture,sourceRoot,managedInputContract,extract}={}){
 assert.equal(managedInputContract,contract);source(sourceRoot);assert.equal(typeof extract,'function');assert.equal(typeof fixture,'string');assert(!fixture.includes('fixtureNativePeerEnabled'),'NATIVE_PEER_ALREADY_APPLIED');let text=fixture;const changes=[];
 function change(before,after,label,count=1){assert.equal(text.split(before).length-1,count,'NATIVE_PEER_SOURCE_ANCHOR:'+label);text=text.split(before).join(after);changes.push({label,before,after,count});}
 function functionPrefix(name,body){const before=extract(text,name).code,brace=before.indexOf('{');change(before,before.slice(0,brace+1)+'\n'+body+'\n'+before.slice(brace+1),name);}
 change('static bool fixtureFullPipe=false;','static bool fixtureFullPipe=false;\nstatic bool fixtureNativePeerEnabled=false;\nstatic bool fixtureNativePeerTransportFaultPending=false;\nstatic int fixtureMsiZeroWaitGuard=0;\nstatic std::function<void()> fixtureMsiSourceEntry;\nstatic BOOL fixtureCreateNativePeer(STARTUPINFOW&,PROCESS_INFORMATION&);\nstatic DWORD fixtureNativePeerExitWait(HANDLE);\nstruct FixtureNativePeer;\nstatic FixtureNativePeer* fixtureFindNativePeer(HANDLE) noexcept;\nstatic HANDLE fixtureNativePeerRecordedHandle(const ymcc::targetReturn::ProcessIdentity&) noexcept;','declarations');
 change('#undef GetExitCodeProcess','#undef GetExitCodeProcess\n#include "native_power_peer_fixture.h"','owned-source-header');
 functionPrefix('fixtureTargetReturnCaptureOwner','if(fixtureNativePeerEnabled&&fixtureFindNativePeer(process))return ymcc::targetReturn::process::capture(process);');
 functionPrefix('fixtureRuntimeRecoveryWriter','if(fixtureNativePeerEnabled){using namespace ymcc::targetReturn;const auto held=fixtureNativePeerRecordedHandle(owner);if(!held){fixtureCountForbiddenIo("private-native-peer-writer-identity-unbound");throw std::logic_error("private-native-peer-writer-identity-unbound");}if(fixtureRuntimeRecoveryOsEnabled){if(!fixtureRuntimeRecoveryOldHost||!fixtureRuntimeRecoveryOldOwner||!ymcc::targetReturn::detail::sameProcess(owner,*fixtureRuntimeRecoveryOldOwner)){fixtureCountForbiddenIo("runtime-return-probe-not-exact-owned-Host");throw std::logic_error("runtime-return-probe-not-exact-owned-Host");}++fixtureRuntimeRecoveryWriterCalls;}const auto wait=WaitForSingleObject(held,0);fixtureRuntimeRecoveryOsObservations.push_back({{"leaf","exact-private-native-peer-exit"},{"pid",owner.pid},{"birth",owner.creationTime},{"wait",wait},{"fakeDead",false}});if(wait==WAIT_OBJECT_0)return {process::State::Dead,process::Reason::MatchingExited};if(wait==WAIT_TIMEOUT)return {process::State::Alive,process::Reason::MatchingRunning};return {process::State::Unknown,process::Reason::WaitFailed,GetLastError()};}');
 functionPrefix('seed','fixtureNativePeersBeginCase();fixtureNativePeerTransportFaultPending=false;');

 change('static void recoveryProtectionCases(){','static void recoveryProtectionCases(){\n const bool msiGuardDirected=fixtureMsiZeroWaitGuard!=0;','existing-MSI-case-opt-in');
 change('for(const int family:{0,1,2})for(const auto delay:{0ULL,3100ULL,10100ULL,31000ULL})for(const bool terminal:{false,true}){','for(const int family:{0,1,2})for(const auto delay:{0ULL,3100ULL,10100ULL,31000ULL})for(const bool terminal:{false,true}){\n if(msiGuardDirected&&(family!=0||delay!=31000||terminal!=(fixtureMsiZeroWaitGuard==2)))continue;','exact-existing-MSI-case-selection');
 change("if(g_realStickTest.virtualTargetEnabled){const bool started=inputHostStart();","if(g_realStickTest.virtualTargetEnabled){\n  std::shared_ptr<ymcc::inputHost::sharedPower::Scope> cancelledPermission;bool cancelInputIssued=false;\n  if(fixtureMsiZeroWaitGuard==1){g_msiDinputSourceValid=false;g_startEntrySeam.confirmRouteAfterEnsure=false;}\n  if(fixtureMsiZeroWaitGuard==2)fixtureMsiSourceEntry=[&](){cancelInputIssued=true;cancelledPermission=t_inputHostRequestPower;if(cancelledPermission)cancelledPermission->cancel();g_powerLifecycle=PowerLifecycle::Ready;};\n  const auto sourceNegativeTrace=g_startEntrySeam.trace.size(),sourceNegativeClock=startProtectionClock;\n  const bool started=inputHostStart();\n  if(fixtureMsiZeroWaitGuard){\n   json results=json::array();for(const auto& e:fixtureEvents)if(e.value(\"event\",\"\")==\"input-host-msi-source-before-target\"&&e[\"detail\"].value(\"stage\",\"\")==\"result\")results.push_back(e[\"detail\"]);\n   cases.back()[\"negativeInput\"]=fixtureMsiZeroWaitGuard==1?\"current-source-missing-with-zero-remaining-wait\":\"cancel-original-permission-then-scalar-Ready\";\n   cases.back()[\"sourceAwaitResults\"]=results;\n   cases.back()[\"retiredPermission\"]={{\"cancelled\",cancelInputIssued},{\"sameObjectStillRetired\",cancelledPermission&&!cancelledPermission->current()},{\"publicInputReady\",sgVirtualWriterAllowedNow()}};\n   check(!started&&std::none_of(g_startEntrySeam.trace.begin()+sourceNegativeTrace,g_startEntrySeam.trace.end(),[](const auto& t){return t==\"pipe:PREPARE_TARGET\"||t==\"pipe:SUBMIT_NEUTRAL\";}),\"original invalid source or cancelled permission cannot PREPARE or publish a target\");\n   check(startProtectionClock==sourceNegativeClock&&g_virtualStartProtection.view(startProtectionClock).requestId==before.requestId,\"zero-wait negative preserves the original request clock and identity\");\n   check(!results.empty()&&results.back().value(\"existingModeTicketsServiced\",1u)==0&&results.back().value(\"elapsedMs\",1ULL)==0,\"zero-wait negative reaches the actual source-await result without sleep or queued mode consumption\");\n   if(fixtureMsiZeroWaitGuard==2)check(cancelInputIssued&&cancelledPermission&&!cancelledPermission->current()&&sgVirtualWriterAllowedNow()&&!results.empty()&&results.back().value(\"reason\",\"\")==\"original-public-power-retired\",\"scalar Ready cannot revive the exact cancelled original permission\");\n   else check(!results.empty()&&results.back().value(\"checks\",0u)==1&&results.back().value(\"reason\",\"\")==\"original-request-deadline\",\"missing current source is checked once and rejected before any zero-allowance wait\");\n  }\n",'actual-MSI-negative-inputs');
 change(' seed();g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_realStickTest.virtualTargetEnabled=false;reg_fs();cases.push_back({{"name","explicit-OFF-during-policy-pause-cannot-auto-resurrect"}});',' if(msiGuardDirected)return;\n seed();g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_realStickTest.virtualTargetEnabled=false;reg_fs();cases.push_back({{"name","explicit-OFF-during-policy-pause-cannot-auto-resurrect"}});','directed-default-mutual-exclusion');

 functionPrefix('cleanup','if(fixtureNativePeerEnabled&&!fixtureFullPipe)fixtureRuntimeRecoveryCensus="absent";');
 {const original=extract(text,'cleanup').code,tail='return result;}',input='if(fixtureNativePeerEnabled&&!fixtureFullPipe&&g_inputHostReleaseQuarantined){fixtureRuntimeRecoveryCensus="absent";g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;g_inputHostLastQuarantineProbeTick=0;inputHostReconcileQuarantine();inputHostRunQueuedLifecycleWork();}\n ';assert(original.endsWith(tail));change(original,original.slice(0,-tail.length)+input+tail,'fixture-only-post-observation-return');}

 change('if(!fixtureRuntimeRecoveryOsEnabled){++forbiddenIo;throw std::logic_error("runtime-PnP-leaf-not-enabled");}','if(!fixtureRuntimeRecoveryOsEnabled&&!fixtureNativePeerEnabled){++forbiddenIo;throw std::logic_error("runtime-PnP-leaf-not-enabled");}','existing-pnp-reader-scope');
 change('g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;fullPipeQuarantineProbe();','fixtureRuntimeRecoveryCensus="absent";g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_wakeHoldPhysicalUnhide=false;fullPipeQuarantineProbe();','original-explicit-return-input',3);
 change('SetEvent(oldHandle)','fixtureNativePeerSignal(oldHandle)','old-peer-exit-input');
 change('int wmain(int argc,wchar_t**argv){try{','int wmain(int argc,wchar_t**argv){try{\n if(const int child=fixtureNativePeerChild(argc,argv);child>=0)return child;','bounded-child-entry');
 change('const std::wstring mode=argv[2];','const std::wstring mode=argv[2];fixtureNativePeerEnabled=mode==L"switch-recovery"||mode==L"tuple-protection";FixtureNativePeerOwner fixtureNativePeerOwner;if(argc==4&&mode==L"recovery-protection"){const std::wstring selected=argv[3];if(selected==L"case:msi-zero-wait:missing-source"){fixtureMsiZeroWaitGuard=1;argc=3;}else if(selected==L"case:msi-zero-wait:cancel-original-permission"){fixtureMsiZeroWaitGuard=2;argc=3;}}','finite-original-mode-owner');
 change('const auto report=json{{"status"','if(!fixtureNativePeersFinish())throw std::runtime_error("native-private-peer-terminal-not-returned");const auto report=json{{"status"','terminal-before-original-report',2);
 change('{"realDeviceIo",false}}','{"nativePrivatePeers",fixtureNativePeerReport},{"nativePrivatePeerOsObservations",fixtureRuntimeRecoveryOsObservations},{"realDeviceIo",false}}','explicit-private-peer-evidence',2);
 change('full-pipe: real107 parent+managed-child; native-only: event Host has explicit controlled PID/birth','full-pipe: real managed child; selected native-only modes: real held private child, controlled Host business; other native modes: event Host','scope-label');
 let inverse=text;for(const row of [...changes].reverse()){assert.equal(inverse.split(row.after).length-1,row.count);inverse=inverse.split(row.after).join(row.before);}assert.equal(inverse,fixture);
 return{fixture:text,receipt:{kind:'FINITE_REAL_NATIVE_PEER_ORIGINAL_LIFECYCLE_INPUTS_V1',header,headerSha256,fixtureBeforeSha256:sha(fixture),fixtureAfterSha256:sha(text),changes:changes.map(({before,after,...row})=>({...row,beforeSha256:sha(before),afterSha256:sha(after)})),inverseExact:true,assertionsRemoved:0,originalDefaultCasesPreserved:true,productAuthenticationChanged:false,realInputHostBusinessProven:false}};
}
export function observeNativePowerPeerTuple(original,{managedInputContract}={}){
 assert.equal(managedInputContract,contract);assert.equal(sha(original),tupleSha256,'NATIVE_PEER_ORIGINAL_TUPLE_CHANGED');let text=Buffer.from(original).toString('utf8');const changes=[['SetEvent(g_inputHostProcess)','fixtureNativePeerSignal(g_inputHostProcess)',1],['SetEvent(observation)','fixtureNativePeerSignal(observation)',2],["        const auto admitted = g_virtualStartProtection.admit(\"steamdeck\", startProtectionClock);","        if(runtimeIncident)inputCaptureRefreshRealStickSettings(true);\n        cases.back()[\"preStartDemand\"]={{\"revision\",g_inputHostRecoveryIntent.revision()},{\"inputConsumerExecuted\",runtimeIncident}};\n        const auto admitted = g_virtualStartProtection.admit(\"steamdeck\", startProtectionClock);",1]];for(const[before,after,count]of changes){assert.equal(text.split(before).length-1,count);text=text.split(before).join(after);}let inverse=text;for(const[before,after]of [...changes].reverse())inverse=inverse.split(after).join(before);assert.equal(inverse,Buffer.from(original).toString('utf8'));return Buffer.from(text);
}

export function materializeNativePowerPeerFixture(options){
 const {fixture,outputRoot}=options;assert(path.isAbsolute(outputRoot));
 const observed=observeNativePowerPeerFixture(options),preimage=path.join(outputRoot,'.fixture-inputs','fixture-before-native-peer.cpp');
 assert(!fs.existsSync(preimage),'NATIVE_PEER_PREIMAGE_EXISTS');fs.mkdirSync(path.dirname(preimage),{recursive:true});fs.writeFileSync(preimage,fixture,{flag:'wx'});
 return{fixture:observed.fixture,receipt:{...observed.receipt,preimage:{path:preimage,bytes:Buffer.byteLength(fixture),sha256:sha(fixture),role:'exact-transformation-input-not-a-compiled-TU'}}};
}
const key=file=>path.resolve(file).toLowerCase();
export const sharedNativePowerEvidenceTools=Object.freeze([
 'tools/fixtures/input_coordinator_lifecycle/native_power_peer.mjs',
 'tools/fixtures/input_coordinator_lifecycle/public_power_ownership.mjs',
 'tools/fixtures/input_coordinator_lifecycle/public_power_ownership.json',
 'tools/fixtures/legion_motion_consumer/extract_main.mjs',
]);

export function verifyCompiledSharedNativePower({manifest,buildRoot,sourceRoot,managedInputContract}={}){
 if(managedInputContract!==contract){
  assert(!manifest.nativePeerObservation&&!manifest.publicPowerOwnerObservation,'UNEXPECTED_SHARED_NATIVE_POWER_GENERATION');
  return null;
 }
 assert(path.isAbsolute(buildRoot)&&path.isAbsolute(sourceRoot),'ABSOLUTE_SHARED_NATIVE_POWER_ROOTS_REQUIRED');
 const receipt=manifest.nativePeerObservation;
 assert(receipt,'NATIVE_PEER_GENERATION_RECEIPT_REQUIRED');
 assert(manifest.publicPowerOwnerObservation,'PUBLIC_POWER_OWNER_GENERATION_RECEIPT_REQUIRED');
 const inputs=new Map(),snapshotRoot=path.join(buildRoot,'source');
 const exact=(file,digest)=>{
  assert(path.isAbsolute(file)&&/^[a-f0-9]{64}$/.test(digest),'EXACT_SHARED_NATIVE_POWER_PIN_REQUIRED');
  const stat=fs.lstatSync(file);
  assert(stat.isFile()&&!stat.isSymbolicLink()&&key(fs.realpathSync.native(file))===key(file),'PLAIN_SHARED_NATIVE_POWER_INPUT_REQUIRED');
  const bytes=fs.readFileSync(file);
  assert.equal(sha(bytes),digest,'SHARED_NATIVE_POWER_INPUT_CHANGED: '+file);
  assert(!inputs.has(key(file))||inputs.get(key(file)).sha256===digest,'CONFLICTING_SHARED_NATIVE_POWER_PIN');
  inputs.set(key(file),{path:file,sha256:digest});
  return bytes;
 };
 const original=(relative,archive=path.join(snapshotRoot,relative))=>{
  const rows=manifest.records.filter(row=>row.path===relative);
  assert.equal(rows.length,1,'UNIQUE_SHARED_NATIVE_POWER_SOURCE_REQUIRED: '+relative);
  const [row]=rows;
  assert(key(row.snapshot)===key(archive)&&key(row.original)===key(path.join(sourceRoot,relative)),'SHARED_NATIVE_POWER_SOURCE_ESCAPED_ROOT: '+relative);
  exact(row.original,row.sha256);
  return exact(archive,row.sha256);
 };
 const compiled=(file,include=true)=>{
  const rows=manifest.compiledGenerated.filter(row=>key(row.path)===key(file));
  assert.equal(rows.length,1,'UNIQUE_SHARED_NATIVE_POWER_COMPILE_INPUT_REQUIRED: '+file);
  if(include)assert(manifest.observedIncludePaths.some(row=>key(row)===key(file)),'SHARED_NATIVE_POWER_INCLUDE_NOT_OBSERVED: '+file);
  return exact(file,rows[0].sha256);
 };
 const fixtureFile=path.join(buildRoot,'fixture.cpp');
 const preimage=path.join(buildRoot,'.fixture-inputs','fixture-before-native-peer.cpp');
 assert(receipt.preimage&&key(receipt.preimage.path)===key(preimage),'NATIVE_PEER_EXACT_PREIMAGE_PATH');
 assert.equal(receipt.preimage.role,'exact-transformation-input-not-a-compiled-TU');
 const before=exact(preimage,receipt.preimage.sha256);
 assert.equal(before.length,receipt.preimage.bytes);
 const headerBytes=original(header);
 assert.equal(sha(headerBytes),headerSha256,'NATIVE_PEER_HEADER_CHANGED');
 assert(compiled(path.join(buildRoot,path.basename(header))).equals(headerBytes),'NATIVE_PEER_COMPILED_HEADER_CHANGED');
 const observed=observeNativePowerPeerFixture({fixture:before.toString('utf8'),sourceRoot:snapshotRoot,managedInputContract,extract:extractFixtureSource});
 const {preimage:_,...declared}=receipt;
 assert.deepEqual(declared,observed.receipt,'NATIVE_PEER_RECEIPT_CHANGED');
 assert(compiled(fixtureFile,false).equals(Buffer.from(observed.fixture)),'NATIVE_PEER_COMPILED_FIXTURE_CHANGED');
 const tuple=original('tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc');
 assert(compiled(path.join(buildRoot,'tuple_recovery_cases.inc')).equals(observeNativePowerPeerTuple(tuple,{managedInputContract})),
  'NATIVE_PEER_FINAL_TUPLE_OVERWRITTEN_OR_CHANGED');
 for(const relative of sharedInputOwnerObservationSources()){
  original(relative,path.join(snapshotRoot,'.fixture-original-power-owners',relative));
  compiled(path.join(snapshotRoot,relative));
 }
 const owners=verifyMaterializedSharedInputPowerOwners({manifest,snapshotRoot,fixtureFile:preimage,managedInputContract});
 return {status:'EXACT_SHARED_NATIVE_POWER_GENERATION_VERIFIED',owners,inputs:[...inputs.values()],runtimeAccepted:false};
}
