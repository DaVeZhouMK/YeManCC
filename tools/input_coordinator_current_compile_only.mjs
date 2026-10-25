import {assertManagedManifestBinding} from './fixtures/input_coordinator_lifecycle/managed_manifest_binding.mjs';
import {observeCurrent105Progress} from './fixtures/input_coordinator_lifecycle/check_progress.mjs';
import {observeFinalStages} from './fixtures/input_coordinator_lifecycle/final_stages.mjs';
import {isolateCurrentFixtureSettingsMutex,privateSettingsMutexDefinitionForRuntimeRoot} from './fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';
import {readLifecycleLogData,LIFECYCLE_LOG_PRODUCER,LIFECYCLE_LOG_DATA_ROLE} from './fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs';
import {requireCurrentCompileBatch} from './ymcc_sim/global/controller-coordinator-pipe/r3-current-compile-batch.mjs';
import {guardCurrentSteam as guardCurrentSteam,observeCurrent105Fixture} from './input_coordinator_current105_source_adapters.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{spawnSync}from'node:child_process';import{fileURLToPath}from'node:url';import{extract}from'./fixtures/legion_motion_consumer/extract_main.mjs';import{calibrationToolchainPreamble}from'./fixtures/hc_reviewed_calibration/toolchain.mjs';import{nativeCompilerInputClosure}from'./fixtures/input_coordinator_lifecycle/compiler_inputs.mjs';
import {readVirtualControllerSource,replaceUniqueSourceText} from './virtual_controller_runtime_source.mjs';
// Copy only bare, repository-owned tool headers named by the reviewed fixture.
// observeCurrent105Fixture still requires its exact approved SHA before compile.
export function currentFixtureToolHeaders(repo){
 const fixture=path.join(repo,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp');
 const text=fs.readFileSync(fixture,'utf8');
 if(Buffer.byteLength(text)>4*1024*1024)throw Error('CURRENT_FIXTURE_HEADER_INPUT_TOO_LARGE');
 const toolRoot=fs.realpathSync(path.join(repo,'tools')),result=[],seen=new Set();
 for(const match of text.matchAll(/^\s*#\s*include\s*"([^\"]+)"/gm)){
  const name=match[1];if(!/\.(?:h|hpp|hxx)$/i.test(name))continue;
  if(name!==path.basename(name)||!/^[-A-Za-z0-9_.]+$/.test(name))throw Error('CURRENT_FIXTURE_HEADER_PATH_REFUSED');
  const candidate=path.join(repo,'tools',name);if(!fs.existsSync(candidate))continue;
  const stat=fs.lstatSync(candidate);if(!stat.isFile()||stat.isSymbolicLink())throw Error('CURRENT_FIXTURE_HEADER_NOT_REGULAR');
  const real=fs.realpathSync(candidate),relative=path.relative(toolRoot,real);
  if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('CURRENT_FIXTURE_HEADER_ESCAPED_TOOLS');
  if(fs.existsSync(path.join(repo,'native',name)))throw Error('CURRENT_FIXTURE_HEADER_AMBIGUOUS_ORIGIN');
  const key=real.toLowerCase();if(seen.has(key))continue;seen.add(key);
  result.push('tools/'+path.basename(real));if(result.length>16)throw Error('CURRENT_FIXTURE_HEADER_COUNT_EXCEEDED');
 }
 return result;
}
// Only direct .inc files beside the already reviewed fixture are additional
// compiler inputs. Existing fixed inputs remain; fixture/profile admission is
// still checked independently before compilation.
export function currentFixtureCaseIncludes(repo){
 const fixtureRoot=path.join(repo,'tools/fixtures/input_coordinator_lifecycle');
 const root=fs.realpathSync(fixtureRoot),text=fs.readFileSync(path.join(fixtureRoot,'fixture.cpp'),'utf8');
 if(Buffer.byteLength(text)>4*1024*1024)throw Error('CURRENT_FIXTURE_CASE_INPUT_TOO_LARGE');
 const result=[],seen=new Set();
 for(const match of text.matchAll(/^\s*#\s*include\s*"([^\"]+)"/gm)){
  const name=match[1];if(!/\.inc$/i.test(name))continue;
  if(name!==path.basename(name)||!/^[-A-Za-z0-9_.]+$/.test(name))throw Error('CURRENT_FIXTURE_CASE_PATH_REFUSED');
  const candidate=path.join(fixtureRoot,name),stat=fs.lstatSync(candidate);
  if(!stat.isFile()||stat.isSymbolicLink())throw Error('CURRENT_FIXTURE_CASE_NOT_REGULAR');
  const real=fs.realpathSync(candidate),relative=path.relative(root,real);
  if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('CURRENT_FIXTURE_CASE_ESCAPED_ROOT');
  if(fs.existsSync(path.join(repo,'native',name))||fs.existsSync(path.join(repo,'tools',name)))throw Error('CURRENT_FIXTURE_CASE_AMBIGUOUS_ORIGIN');
  const key=real.toLowerCase();if(seen.has(key))continue;seen.add(key);
  result.push('tools/fixtures/input_coordinator_lifecycle/'+path.basename(real));
  if(result.length>16)throw Error('CURRENT_FIXTURE_CASE_COUNT_EXCEEDED');
 }
 return result;
}
// Derive the fixture log leaf only from the exact producer approved for this
// captured source frame. This parses one quoted data literal and executes no JS.
export function currentLifecycleLogBoundary(repo){
 const profileFile=path.join(repo,'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json');
 const profile=JSON.parse(fs.readFileSync(profileFile,'utf8'));
 if(profile.producer?.relative!==LIFECYCLE_LOG_PRODUCER)throw Error('CURRENT_LOG_PRODUCER_PROFILE_REQUIRED');
 return readLifecycleLogData(repo,profile.producerData).body;
}

export function buildCurrentCompileOnly({argv=[],batchAuthority,runtimeRoot,checkProgress=false,finalStages=false}={}){
 if(typeof checkProgress!=='boolean')throw Error('BOOLEAN_CHECK_PROGRESS_REQUIRED');
 if(typeof finalStages!=='boolean')throw Error('BOOLEAN_FINAL_STAGES_REQUIRED');
 if(runtimeRoot!==undefined)privateSettingsMutexDefinitionForRuntimeRoot(runtimeRoot);

const rawArgs=[...argv];if(rawArgs.filter(arg=>arg==='--compile-only').length!==1)throw Error('Explicit compile-only argument required');argv=rawArgs.filter(arg=>arg!=='--compile-only');
const sourceAt=argv.indexOf('--source-root');const suppliedRoot=sourceAt<0?null:argv[sourceAt+1];if(sourceAt>=0)argv.splice(sourceAt,2);

const layout=requireCurrentCompileBatch(batchAuthority,{output:argv[1]});if(finalStages&&layout.managedInputContract!=='current105-terminal22-v1')throw Error('FINAL_STAGES_TERMINAL22_ONLY');const repo=layout.sourceRoot;const args=[...argv];if(suppliedRoot&&path.resolve(suppliedRoot)!==path.resolve(repo))throw Error('Explicit current sourceRoot differs from authority');if(![2,4,6,8,10].includes(args.length)||args[0]!=="--out")throw Error("Use --out ABS_NEW_ROOT [--mutation ID] [--managed-dll SDK_FREE_DLL] [--suite all|persona-lifecycle]");
let mutation=null,managedDll=null,suite=null,coldRecord=null;const seen=new Set();for(let i=2;i<args.length;i+=2){const key=args[i];if(!["--mutation","--managed-dll","--suite","--cold-record"].includes(key)||seen.has(key)||!args[i+1])throw Error("Invalid or repeated option");seen.add(key);if(key==="--mutation")mutation=args[i+1];else if(key==="--managed-dll")managedDll=path.resolve(args[i+1]);else if(key==="--cold-record")coldRecord=path.resolve(args[i+1]);else{if(!["all","persona-lifecycle","cold-archive"].includes(args[i+1]))throw Error("Unknown suite");suite=args[i+1];}}
if(suite&&(!managedDll||mutation))throw Error("All suites require an owned managed DLL and no directed mutation");
if(mutation&&!["bypass-admission","rollback-recovery-inheritance","rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-switch-return-rearm","rollback-switch-fault-owner","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation))throw Error("Unknown mutation");
if(["rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation)&&!managedDll||managedDll&&mutation&&!["bypass-admission","rollback-recovery-inheritance","rollback-switch-return-rearm","rollback-switch-fault-owner"].includes(mutation)&&!["rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation))throw Error("Cancellation negative requires managed DLL; other negatives remain native-only");
const expectedMutationAssertion=({"bypass-admission":"actual durable request locks before consumer runs","rollback-recovery-inheritance":"single real return refresh restores requested operational target","rollback-cancellation-guards":"actual RECREATE primitive refuses changed admission at send boundary","rollback-request-ownership":"actual RECREATE primitive refuses changed admission at send boundary","rollback-ps-endpoint-ownership":"same-profile peer or retired endpoint cannot prove current owned PS readiness","rollback-ps-binding-revision":"new target generation invalidates a slow observation even when instance names are reused","rollback-switch-return-rearm":"verified late cleanup queues only the continuation of the already charged recovery","rollback-switch-fault-owner":"one current frame fault has exactly one serialized Stop owner","rollback-pipe-stage-budget":"every actual connect write and read receives only the original player's remaining deadline budget","rollback-initial-start-budget":"normal first Start binds every actual lifecycle pipe stage to the original player execution period","rollback-wake-return-continuation":"dead-Host wake consumes proven old return then creates exactly one new owned Host","rollback-wake-retirement-identity":"rejected dead-Host return retirement logs exact original wakeSerial requestId powerGeneration and hostEpoch","rollback-wake-start-budget":"PC dead-Host wake permits at most three actual no-Host Start attempts after proven return","rollback-queued-player-binding":"expired queued first Start cannot create a Host or target after original thirty-second budget","rollback-stale-off-direction":"expired old OFF cannot take generic Start through an also-expired newer ON","rollback-off-return-debt":"unproven MSI return debt cannot finalize OFF success after eleven seconds","rollback-off-start-blocked":"fully returned ROG OFF succeeds with StartBlocked retained and no automatic Start","rollback-prepare-claim":"lost PREPARE ACK must not restore physical visibility while actual target cleanup is still unproven","rollback-unsent-prepare-claim":"unissued PREPARE cannot create a cleanup claim or target ownership","rollback-live-explicit-restore":"explicit OFF cannot override live unknown target return and restore physical visibility","rollback-ps-return-xinput":"old PS owned HID present or unknown must veto XInput-only physical restoration and tuple retirement","rollback-ps-return-pin":"complete owned-PS absence allows the actual deferred restore and exact old tuple retirement","rollback-ps-return-physical":"failed physical restore retains the exact PS endpoint return identity for the next probe","rollback-ps-return-family":"unknown old PS root cannot be discharged by profile presence or incomplete family census","rollback-exit-final-sweep":"final app-exit sweep cannot bypass live Host or unproven target ownership","rollback-target-reservation":"actual PREPARE or RECREATE wire is preceded by persisted targetMayExist for exact current owner","rollback-target-pin-store":"accepted real PS State pin is persisted after current acceptance","rollback-target-settlement":"complete Stop consumes exactly the settled old current target-only origins"})[mutation];
if(suite==="cold-archive"&&(!coldRecord||!managedDll))throw Error("cold-archive requires --cold-record and --managed-dll");
const out=path.resolve(args[1]);if(fs.existsSync(out))throw Error('Fresh out required');fs.mkdirSync(out,{recursive:true});const snapshot=path.join(out,'source'),sha=b=>crypto.createHash('sha256').update(b).digest('hex'),records=[];const managedProof=managedDll?{dll:{path:managedDll,sha256:sha(fs.readFileSync(managedDll))},manifest:path.join(path.dirname(managedDll),'fixture-manifest.json')}:null;if(managedProof){managedProof.manifestSha256=sha(fs.readFileSync(managedProof.manifest));const m=JSON.parse(fs.readFileSync(managedProof.manifest));if(m.realSdkLoaded!==false||m.hardwareIo!==false||m.status!=='BUILT'||path.resolve(m.fixtureDll)!==managedDll)throw Error('Not an owned SDK-free compiled fixture');
 try{assertManagedManifestBinding(fs.readFileSync(managedProof.manifest,'utf8'));const b=m.inputBindings;if(!Array.isArray(b?.sources)||b.sources.length<1||b.sources.length>64||!Array.isArray(b.generatedInputs)||b.generatedInputs.length>64||!Array.isArray(m.runtimeArtifacts)||m.runtimeArtifacts.length>32)throw Error('MANAGED_PREREQUISITE_BINDINGS_INVALID');for(const row of b.sources){if(sha(fs.readFileSync(row.path))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256)throw Error('MANAGED_PREREQUISITE_STALE_SOURCE:'+row.relative);}for(const row of [...b.generatedInputs,...m.runtimeArtifacts])if(sha(fs.readFileSync(row.path))!==row.sha256)throw Error('MANAGED_PREREQUISITE_STALE_ARTIFACT:'+row.path);managedProof.preflight={sourceBindings:b.sources.length,generatedBindings:b.generatedInputs.length,runtimeArtifacts:m.runtimeArtifacts.length,status:'VERIFIED-BEFORE-NATIVE-COMPILE'};}catch(error){fs.writeFileSync(path.join(out,'managed-prerequisite-failure.json'),JSON.stringify({status:'REJECTED',stage:'before-native-compilation',reason:error.message,managedProof,normalHostMainExecuted:false,realDeviceIo:false},null,2));throw error;}
}
function copy(dir,rel){for(const e of fs.readdirSync(dir,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('source link');const file=path.join(dir,e.name),next=path.join(rel,e.name);if(e.isDirectory())copy(file,next);else if(/\.(?:h|hpp|hxx|c|cc|cpp|inc|inl)$/i.test(e.name)){const target=path.join(snapshot,next);fs.mkdirSync(path.dirname(target),{recursive:true});const bytes=fs.readFileSync(file);fs.writeFileSync(target,bytes);records.push({path:next.replaceAll('\\','/'),original:file,snapshot:target,sha256:sha(bytes)});}}}
for(const rel of ['native','capabilities','deps/json','deps/webview2/build/native/include'])copy(path.join(repo,rel),rel);
const fixtureToolHeaders=[...currentFixtureToolHeaders(repo),...currentFixtureCaseIncludes(repo)];
for(const rel of new Set([...fixtureToolHeaders,...(finalStages?['tools/fixtures/input_coordinator_lifecycle/final_stages.mjs']:[]),'tools/fixtures/input_coordinator_lifecycle/fixture.cpp','tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc','tools/fixtures/input_coordinator_lifecycle/target_return_cases.inc','tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc','tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc','tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc','tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc','tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc','tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc','tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc','tools/input_coordinator_lifecycle_selftest.mjs','tools/virtual_controller_runtime_source.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'])){const target=path.join(snapshot,rel);fs.mkdirSync(path.dirname(target),{recursive:true});const bytes=fs.readFileSync(path.join(repo,rel));fs.writeFileSync(target,bytes);records.push({path:rel,original:path.join(repo,rel),snapshot:target,sha256:sha(bytes)});}
const expanded=readVirtualControllerSource(path.join(snapshot,'native/main.cpp'));
const expansionInputs=expanded.inputs.map(input=>{const row=records.find(r=>path.resolve(r.snapshot)===input.file);if(!row||row.sha256!==input.sha256)throw Error('UNBOUND_RUNTIME_EXPANSION_INPUT:'+input.file);return row.path;});
const sourceExpansion=expanded.manifest;
const expandedFile=path.join(snapshot,'native/main.expanded.cpp');fs.writeFileSync(expandedFile,expanded.source,{flag:'wx'});sourceExpansion.expandedSnapshot=expandedFile;
const source=expanded.source;let inert=source;const substitutions=[];
for(const[name,body]of [
 ['inputHostStartProtectionNow','return startProtectionClock;'],
 // The ordinary100 suite stays on its explicit legacy CLI boundary.
 // Any entry to real writer file/process/job leaves is forbidden I/O.
 ['hidHideCliWriterRecordPath','if(fixtureColdArchive)return fixtureTargetReturnRecordPath()+L".cli";++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-record-path");'],
 ['hidHideCliWriterMachineBinding','if(fixtureColdArchive)return fixtureColdMachine;++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-machine-binding");'],
 ['hidHideCliWriterCapture','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-capture");'],
 ['hidHideCliWriterProbe','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-process-probe");'],
 ['hidHideCliWriterProbeJob','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-job-probe");'],
 ['inputHostTargetReturnRecordPath','return fixtureTargetReturnRecordPath();'],
 ['inputHostTargetReturnMachineBinding','return fixtureColdArchive?fixtureColdMachine:"fixture-machine";'],
 ['inputHostTargetReturnCaptureOwner','return fixtureTargetReturnCaptureOwner(process);'],
 // A new CM ancestry OS leaf, separate from the unchanged three environment leaves.
 // Preserve BeforeHide/AfterRestore algorithms; fail on any unexpected real probe.
 ['inputHostTargetReturnVerifyPhysicalPath','++forbiddenIo;throw std::logic_error("unexpected-physical-return-CM-verification");'],
 // Never probe a non-private recorded writer PID from the full-TU fixture.
 ['inputHostTargetReturnProbePhysicalWriter','if(fixtureColdArchive)return fixtureColdWriter(owner);++forbiddenIo;throw std::logic_error("unexpected-physical-return-process-probe");'],
 // The census reader and real CLI --dev-list stay behind these explicit leaves.
 ['inputHostTargetReturnColdAbsence','if(fixtureColdArchive)return fixtureColdAbsence(knownIds);++forbiddenIo;throw std::logic_error("unexpected-cold-return-PnP-absence");'],
 ['inputHostTargetReturnColdHidden','++forbiddenIo;throw std::logic_error("unexpected-cold-return-hidden-read");'],
 ['observeTdpRuntimeIdentity','/* private offline input fixture: no TDP observation */'],
 ['inputCaptureAssetsPresent','return true;'],
 ['oemBackMappingRefreshGate','/* Separate OEM hook/collector setup excluded; no physical keyboard hook in this fixture. */'],
 ['reg_desktop_monitor_hud','++forbiddenIo;throw std::logic_error("unexpected-desktop-HUD-entry");'],
 ['msiDinputOpen','++forbiddenIo;throw std::logic_error("unexpected-physical-DInput-open");'],
 ['msiClawOpenCommandHid','++forbiddenIo;throw std::logic_error("unexpected-physical-command-HID-open");'],
 ['ymccXInputGetState14','return fixtureControlledXinputState(userIndex,state);'],
 ['ymccXInputGetStateSecret14','++inertXinputReads;return ERROR_DEVICE_NOT_CONNECTED;'],
 ['ymccXInputSetState14','++forbiddenIo;return ERROR_DEVICE_NOT_CONNECTED;'],
 ['ymccOpenXInputGetDevicePath','return fixtureControlledXinputPath(userIndex);'],
 ['ymccOpenXInputGetUserIndex','return fixtureControlledXinputIndex(devicePath);'],
 ['ymccOpenXInputAvailable','return fixtureXinputSourceLive;'],
 ['hidHideServiceRunning','if(queriedOk)*queriedOk=true;ymStartSeamTrace("health:service-query");fixtureHealthInventoryRead=true;return true;'],
 ['hidHideRun','if(fixtureExitSweep){if(arguments==L"--dev-list"){++fixtureExitSweepLists;if(output)*output=std::string("--dev-hide ")+char(34)+"HID_TEST_OWNED"+char(34);}else if(arguments.rfind(L"--dev-unhide ",0)==0){++fixtureExitSweepUnhides;}else if(arguments==L"--cloak-off"){++fixtureExitSweepCloakOffs;}else{++forbiddenIo;throw std::logic_error("unadmitted-exit-sweep-operation");}if(exitCodeOut)*exitCodeOut=0;return true;}if(fixtureHealthInventoryRead && arguments==L"--dev-all"){fixtureHealthInventoryRead=false;ymStartSeamTrace("health:readonly-dev-all");if(output)*output="offline-readonly-health-observation";if(exitCodeOut)*exitCodeOut=0;return true;}++forbiddenIo;throw std::logic_error("unexpected-HidHide-CLI-entry:"+W2U(arguments));'],
 ['appendNativeLifecycleLog',currentLifecycleLogBoundary(repo)]]){
 const old=extract(source,name).code,start=old.indexOf('{'),replacement=old.slice(0,start+1)+'\n'+body+'\n}';if(inert.split(old).length!==2)throw Error('nonunique boundary '+name);inert=inert.replace(old,replacement);substitutions.push({symbol:name,beforeSha256:sha(old),afterSha256:sha(replacement),scope:'Explicit external device/discovery/process/log boundary only'});
}
{const safety=guardCurrentSteam(inert,{sourceRoot:repo,managedInputContract:layout.managedInputContract});inert=safety.inert;substitutions.push(...safety.substitutions);}
// Conditional adapters are fixture-only process/transport/hide/diagnostic endpoints. Actual
// inputHostStart/Stop/queue/clear/request/Win32 pipe implementations stay unchanged.
for(const [name,body]of [['ymStartPipeSeamServe','if(fixtureFullPipe)return false;if(fixtureFrameFirstAck && command && std::string(command)=="SUBMIT_FRAME"){if(responseOut)*responseOut=json{{"status",expectedStatus?expectedStatus:""},{"detail",""},{"firstFrame",true}};return true;}if(fixtureStartupTransportError && command && std::string(command)==g_startEntrySeam.pipeFailCommand)g_inputHostSwitchRecoveryTransportError=fixtureStartupTransportError;'],['ymStartHostExitWait','if(fixtureFullPipe)return WaitForSingleObject(process,5000);'],['ymStartCreateProcessW','if(fixtureFullPipe)return fixtureCreateManagedProcess(si,pi);'],['hidHideRefreshPhysicalHide','if(fixtureWakeScenario){++fixtureWakeHideCalls;auto beforeReturn=std::move(fixtureWakeBeforeHideReturn);fixtureWakeBeforeHideReturn={};if(beforeReturn)beforeReturn();return fixtureWakeHideResult;}'],['inputCaptureLogVisibilitySnapshotGated','if(fixtureWakeScenario){++fixtureWakeVisibilityExclusions;return;}']]){const original=extract(source,name).code;const brace=original.indexOf('{');const changed=original.slice(0,brace+1)+'\n'+body+'\n'+original.slice(brace+1);if(inert.split(original).length!==2)throw Error('adapter source not unique');inert=inert.replace(original,changed);substitutions.push({symbol:name,scope:'Explicit conditional fixture process/pipe/hide/diagnostic ends; existing behavior unchanged when scenario flags are off',beforeSha256:sha(original),afterSha256:sha(changed)});}
// Read-only write-ahead evidence is observed immediately before the real OS
// pipe primitive. No create/accept/settlement logic is replaced by this adapter.
{
 const original=extract(inert,'inputHostPipeIo').code;const brace=original.indexOf('{');
 const body='\n    if(fixtureFullPipe && write)fixtureTargetReturnWireObserve(buffer,size);\n';
 const changed=original.slice(0,brace+1)+body+original.slice(brace+1);
 inert=replaceUniqueSourceText(inert,original,changed,'target-return-wire-observer');
 substitutions.push({symbol:'inputHostPipeIo/write-entry-observer',scope:'Read-only real Session/store evidence before actual owned pipe write; no command/receipt/decision substitution',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// The original overlapped owned-child pipe IO still executes. For a directed
// receipt test only, replay one previously observed actual report ACK, or corrupt
// one identity/asset field at this explicit wire-read endpoint. No HostSession,
// SDK ownership, envelope parser, tuple classifier or lifecycle branch is replaced.
{
 const original=extract(inert,'inputHostPipeIo').code;
 const anchor='        return result.ok;';if(original.split(anchor).length!==2)throw Error('wire IO return anchor');
 const body=String.raw`
        if(fixturePipeBudgetEnabled)fixturePipeBudgetPort(write?"write":"read",result.ok,timeoutMs,buffer,size);
        if(fixtureFullPipe && fixturePrepareAckLoss && result.ok && !write && transferredOut && *transferredOut){
            const auto received=json::parse(std::string(static_cast<char*>(buffer),*transferredOut),nullptr,false);
            if(received.is_object() && received.value("command","")=="PREPARE_TARGET" && received.value("status","")=="target-prepared"){
                fixturePrepareAckOriginal=received;fixturePrepareAckLossConsumed=true;fixturePrepareAckLoss=false;
                fixtureTargetReturnAckLostObserve(received);
                if(errorOut)*errorOut=ERROR_NO_DATA;if(transferredOut)*transferredOut=0;return false;
            }
        }
        if(fixtureFullPipe && fixtureWireCapture && result.ok && !write && transferredOut && *transferredOut){
            const std::string received(static_cast<char*>(buffer),*transferredOut);
            const auto reply=json::parse(received,nullptr,false);
            if(reply.is_object() && reply.value("command","")=="SUBMIT_FRAME" && reply.value("status","")=="frame-accepted"){
                if(!fixtureWireFault.empty()){
                    std::string replaced;fixtureWireOriginal=reply;
                    if(fixtureWireFault=="replay")replaced=fixtureWirePrevious;
                    else{auto corrupted=reply;if(fixtureWireFault=="asset")corrupted["assetVerified"]=false;else corrupted["hostInstanceId"]="offline-wrong-owned-instance";replaced=corrupted.dump();}
                    if(replaced.empty() || replaced.size()>size)throw std::logic_error("wire-fault-outside-read-buffer");
                    std::memcpy(buffer,replaced.data(),replaced.size());*transferredOut=static_cast<DWORD>(replaced.size());
                    fixtureWireInjected=json::parse(replaced);fixtureWireFaultConsumed=true;fixtureWireFault.clear();
                }else fixtureWirePrevious=received;
            }
        }
`;
 const changed=original.replace(anchor,body+anchor);inert=inert.replace(original,changed);
 substitutions.push({symbol:'inputHostPipeIo',scope:'Original real owned-child pipe read/write, optional one captured ACK replay or one wire field corruption only. Production parser/classifier/HostSession untouched.',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Original ConnectNamedPipe owner executes. Supply elapsed time only at the
// successful external connect return, exactly like the existing wire observer.
{
 const original=extract(inert,'inputHostWaitClientLocked').code;
 const anchor='        return result.ok;';if(original.split(anchor).length!==2)throw Error('connect budget port anchor');
 const changed=original.replace(anchor,'        if(fixturePipeBudgetEnabled)fixturePipeBudgetPort("connect",result.ok,timeoutMs);\n'+anchor);
 inert=inert.replace(original,changed);substitutions.push({symbol:'inputHostWaitClientLocked',scope:'Original actual owned Connect IO; optional elapsed-clock observation after kernel return only.',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Exact reader control flow, with only four SetupAPI leaf names rebound to
// owned deterministic endpoints. No read/decision branch is replaced.
{
 const original=extract(inert,'psWakeProbeOwnEndpoint').code;
 let adapted=original;for(const [api,endpoint]of [['SetupDiGetClassDevsW','fixturePsGetClassDevs'],['SetupDiEnumDeviceInterfaces','fixturePsEnum'],['SetupDiGetDeviceInterfaceDetailW','fixturePsDetail'],['SetupDiDestroyDeviceInfoList','fixturePsDestroy'],['CM_Get_Device_IDW','fixturePsDeviceId'],['CM_Get_Parent','fixturePsParent']])adapted=adapted.replaceAll(api+'(',endpoint+'(');
 if(adapted===original||inert.split(original).length!==2)throw Error('PS SetupAPI boundary anchor');inert=inert.replace(original,adapted);substitutions.push({symbol:'psWakeProbeOwnEndpoint',beforeSha256:sha(original),afterSha256:sha(adapted),scope:'Only read-only SetupAPI/CM leaf calls rebound; loop/error/classification/ancestry/decision logic unchanged'});
}
for(const [symbol,leaves]of [
 ['inputHostXboxInterfaceIsTarget',[['SetupDiCreateDeviceInfoList','fixtureXboxCreate'],['SetupDiOpenDeviceInterfaceW','fixtureXboxOpen'],['SetupDiGetDeviceInterfaceDetailW','fixtureXboxDetail'],['SetupDiDestroyDeviceInfoList','fixtureXboxDestroy'],['CM_Get_Device_IDW','fixtureXboxDeviceId']]],
 ['inputHostXboxConfirmStartupLocked',[['CM_Locate_DevNodeW','fixtureXboxLocate'],['CM_Get_DevNode_Status','fixtureXboxStatus']]]]){
 const original=extract(inert,symbol).code;let adapted=original;for(const[from,to]of leaves)adapted=adapted.replaceAll(from+'(',to+'(');if(adapted===original||inert.split(original).length!==2)throw Error('Elite identity leaf adapter anchor:'+symbol);inert=inert.replace(original,adapted);substitutions.push({symbol,beforeSha256:sha(original),afterSha256:sha(adapted),scope:'Only exact read-only SetupAPI/ConfigManager leaf calls rebound to owned identity fixture; Xbox native parsing, matching, poll/state/path/index/readback and decision flow unchanged.'});
}
if(mutation==='bypass-admission'){const anchor='if (!targetRequest.admit(oldValid ? old : json::object(), parsed))';if(inert.split(anchor).length!==2)throw Error('Admission mutation anchor changed');inert=inert.replace(anchor,'if (false && !targetRequest.admit(oldValid ? old : json::object(), parsed))');substitutions.push({symbol:'ymSettingsWriteDocumentUnlocked',scope:'Explicit semantic negative: remove request admission; assertions unchanged',negativeControl:true});}
if(mutation==='rollback-recovery-inheritance'){
 const replace=(a,b)=>{inert=replaceUniqueSourceText(inert,a,b,'Recovery mutation anchor changed');};
 replace('        const auto inherited = g_virtualStartProtection.existingTicket(configuredPersona,inputHostStartProtectionNow());\n        const auto admitted = inherited ? ymcc::VirtualStartProtection::Admission{true,inherited,false}\n            : g_virtualStartProtection.consume(configuredPersona,inputHostStartProtectionNow());','        const auto admitted = g_virtualStartProtection.consume(configuredPersona,inputHostStartProtectionNow());');
 replace('    } else if (virtualTargetChanged && !targetEnabled &&\n            !(targetRequested && personaAdmitted && !targetMachineRejection)) {','    } else if (virtualTargetChanged && !targetEnabled) {');
 replace('    auto requestTicket = g_virtualStartProtection.existingTicket(requestedPersona,inputHostStartProtectionNow());','    auto requestTicket = g_virtualStartProtection.pendingTicket(requestedPersona,inputHostStartProtectionNow());');
 substitutions.push({symbol:'inputCaptureRefreshRealStickSettings/inputHostStart',scope:'Explicit semantic negative: reproduce old operational-pause cancellation and loss of inherited recovery ownership; assertions unchanged',negativeControl:true});
}
if(mutation==='rollback-cancellation-guards'){
 const removeRange=(symbol,from,to)=>{const original=extract(inert,symbol).code;const a=original.indexOf(from),b=original.indexOf(to,a);if(a<0||b<=a)throw Error('Cancellation negative anchor drift:'+symbol);const altered=original.slice(0,a)+original.slice(b);if(inert.split(original).length!==2)throw Error('Cancellation symbol not unique');inert=inert.replace(original,altered);};
 removeRange('inputHostPerformPsWakeRearm','    // A complete read is still only an observation.','    const bool rebuiltAlready =');
 const original=extract(inert,'inputHostRecreatePreparedTarget').code,a=original.indexOf('        // The intent/power/exit may change'),b=original.indexOf('            earlyOut = true;',a);if(a<0||b<=a)throw Error('Cancellation recreate anchor');const altered=original.slice(0,a)+'        if (!g_inputHostProcess || !g_inputHostHello || g_inputHostTransportFault) {'+(original.includes('\r\n')?'\r\n':'\n')+original.slice(b);inert=inert.replace(original,altered);
 removeRange('inputHostSubmitPad','    // OFF revokes normal BUS publication immediately,','    const bool powerAdmitted =');
 const actual=extract(inert,'inputHostFrameSubmissionCurrent').code;const newline=actual.includes('\r\n')?'\r\n':'\n';const old='    const bool ready=g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&'+newline+'        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire) &&'+newline+'        g_inputReady.load';if(actual.split(old).length!==2)throw Error('Cancellation frame-ready anchor');inert=inert.replace(actual,actual.replace(old,'    const bool ready=g_inputReady.load'));
 substitutions.push({symbol:'inputHostPerformPsWakeRearm/inputHostRecreatePreparedTarget/inputHostSubmitPad/inputHostFrameSubmissionCurrent',negativeControl:true,scope:'Only revert the four new cancellation admission guards; actual full native worker/transport and managed session remain executed. No device or normal Main.'});
}
if(mutation==='rollback-request-ownership'){
 const newline='\n';
 const replace=(symbol,from,to)=>{const original=extract(inert,symbol).code;inert=inert.replace(original,replaceUniqueSourceText(original,from,to,'Request-ownership negative anchor:'+symbol));};
 replace('inputHostFrameSubmissionCurrent','        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire) &&'+newline,'');
 replace('inputHostRecreatePreparedTarget','        else if (!g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire))'+newline+'            admissionRefusal = "requested-persona-not-bound";'+newline,'');
 const old=extract(inert,'inputHostSubmitPad').code;const a=old.indexOf('    // A fresh frame for the new request must not be relabeled'),b=old.indexOf('    const bool powerAdmitted =',a);if(a<0||b<=a)throw Error('Request-ownership frame-entry anchor');inert=inert.replace(old,old.slice(0,a)+old.slice(b));
 replace('inputHostPerformPsWakeRearm','            g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId != queryRequestId ||'+newline+'            inputHostEpochToken() != queryHostEpoch ||'+newline,'');
 substitutions.push({symbol:'inputHostPerformPsWakeRearm/inputHostRecreatePreparedTarget/inputHostSubmitPad/inputHostFrameSubmissionCurrent',negativeControl:true,scope:'Only bypass new requested-vs-bound target and query request/Host ownership checks. Prior OFF/exit/power fences remain active; actual settings, queue, native pipe and managed child execute.'});
}

if(mutation==='rollback-ps-endpoint-ownership'){
 const original=extract(inert,'psWakeProbeOwnEndpoint').code,a=original.indexOf('        // A marker names a driver/profile'),b=original.indexOf('        if(owned){',a);if(a<0||b<=a)throw Error('PS root-ownership rollback anchor');
 const legacy='        std::wstring legacy=path;for(auto& ch:legacy)ch=(wchar_t)towupper(ch);\n        const bool marker=legacy.find(L"HIDMAESTRO")!=std::wstring::npos||legacy.find(L"HM-CTL")!=std::wstring::npos||legacy.find(L"VIGEM")!=std::wstring::npos;\n        const bool owned=marker&&legacy.find(L"VID_054C")!=std::wstring::npos&&legacy.find(L"PID_0DF2")!=std::wstring::npos;\n        std::wstring instanceId;\n';
 inert=inert.replace(original,original.slice(0,a)+legacy+original.slice(b));substitutions.push({symbol:'psWakeProbeOwnEndpoint',negativeControl:true,scope:'Only replace SDK-root CM ancestry verdict with original profile-marker matching; completeness, session, request, target generation and managed pipe remain unchanged.'});
}
if(mutation==='rollback-ps-binding-revision'){
 const original=extract(inert,'inputHostPerformPsWakeRearm').code,a=original.indexOf('    // A replacement of the same intended target'),b=original.indexOf('    const bool rebuiltAlready =',a);if(a<0||b<=a)throw Error('PS target revision rollback anchor');inert=inert.replace(original,original.slice(0,a)+original.slice(b));substitutions.push({symbol:'inputHostPerformPsWakeRearm',negativeControl:true,scope:'Only bypass per-target binding revision and same-intent bounded fresh-read after slow query; request/Host/power checks and exact endpoint ownership remain.'});
}
if(mutation==='rollback-switch-return-rearm'){
 const anchor='const bool switchRecoveryRearmPreserved=inputHostSwitchRecoveryReturnedLocked(retiredHostEpoch,retiredRunId);';
 if(inert.split(anchor).length!==2)throw Error('Switch return rearm mutation anchor');
 inert=inert.replace(anchor,'const bool switchRecoveryRearmPreserved=false;');
 substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only omit exact same-intent rearm after actual proven quarantine return; original assertions unchanged.'});
}
if(mutation==='rollback-switch-fault-owner'){
 const anchor='} else if (reason.rfind("fault-stop:frame-receipt-fault:",0)==0 &&';
 if(inert.split(anchor).length!==2)throw Error('Switch fault owner mutation anchor');
 inert=inert.replace(anchor,'} else if (false && reason.rfind("fault-stop:frame-receipt-fault:",0)==0 &&');
 substitutions.push({symbol:'inputHostRunQueuedLifecycleWork',negativeControl:true,scope:'Only bypass fault-cleanup coalescing and transfer to admitted recovery; original assertions unchanged.'});
}
if(mutation==='rollback-pipe-stage-budget'){
 const original=extract(inert,'inputHostRequestPreparedLocked').code;
 const anchor='const auto executionDeadline=inputHostRequestHasExecutionDeadline(command)';
 if(original.split(anchor).length!==2)throw Error('Pipe deadline rollback anchor');
 const altered=original.replace(anchor,'const auto executionDeadline=false&&inputHostRequestHasExecutionDeadline(command)');
 inert=inert.replace(original,altered);substitutions.push({symbol:'inputHostRequestPreparedLocked',negativeControl:true,scope:'Only remove per-stage original-period budget; existing entry clamp, actual native pipe/Host, ownership, parser and all cases unchanged.'});
}
if(mutation==='rollback-initial-start-budget'){
 const original=extract(inert,'inputHostStart').code;
 const anchor='if(ticket&&view.requestId==ticket&&view.locked){';
 if(original.split(anchor).length!==2)throw Error('Initial Start period rollback anchor');
 inert=inert.replace(original,original.replace(anchor,'if(false&&ticket&&view.requestId==ticket&&view.locked){'));
 substitutions.push({symbol:'inputHostStart',negativeControl:true,scope:'Only omit normal Start attachment to existing player deadline; per-pipe-stage budget, inherited wake/fallback scopes, actual native/managed IO and original cases remain.'});
}
if(mutation==='rollback-wake-return-continuation'){
 const anchor='const bool wakeExecutionReturnPreserved=inputHostWakeReturnedLocked(retiredHostEpoch,retiredRunId);';
 if(inert.split(anchor).length!==2)throw Error('Wake return rollback anchor');
 inert=inert.replace(anchor,'const bool wakeExecutionReturnPreserved=false;');
 substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only omit original wake continuation after exact common quarantine return; actual Stop/restore/clear, original wake/source cases and real child sessions unchanged.'});
}
// Restore exactly one proven OFF defect in actual production bodies.
if(mutation==='rollback-stale-off-direction'){
 const original=extract(inert,'inputHostRunQueuedLifecycleWork').code;
 const altered=replaceUniqueSourceText(original,'if(reason=="virtual-target-disabled" && g_realStickTest.virtualTargetEnabled &&','if(false && reason=="virtual-target-disabled" && g_realStickTest.virtualTargetEnabled &&',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostRunQueuedLifecycleWork',negativeControl:true,scope:'Only restore stale OFF promotion to later ON; same real dispatcher, queued work, player periods and actual managed Host.'});
}
if(mutation==='rollback-off-return-debt'||mutation==='rollback-off-start-blocked'){
 const original=extract(inert,'inputHostDisabledCleanupProvenLocked').code;
 const altered=mutation==='rollback-off-return-debt'
  ?replaceUniqueSourceText(original,'&& msiReturnProven;','&& true;',mutation)
  :replaceUniqueSourceText(original,'return !g_realStickTest.virtualTargetEnabled','return !g_inputHostStartBlocked && !g_realStickTest.virtualTargetEnabled',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostDisabledCleanupProvenLocked',negativeControl:true,scope:mutation==='rollback-off-return-debt'?'Only omit MSI physical return debt from shared Stop/final proof; no synthetic discharge.':'Only restore future Start prohibition as an OFF ownership debt; original quarantine/restore owner and retained flag unchanged.'});
}
if(mutation==='rollback-prepare-claim'){
 const original=extract(inert,'inputHostStopLocked').code;const altered=replaceUniqueSourceText(original,'const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership || prepareCreationPossible;','const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership;',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStopLocked',negativeControl:true,scope:'Only omit uncertain PREPARE creation from actual Stop target ownership; actual held managed target, original real pipe and assertions unchanged.'});
}
if(mutation==='rollback-unsent-prepare-claim'){
 const original=extract(inert,'inputHostStart').code;const anchor='    const bool prepareSent = prepareAttempted && inputHostRequestLocked(';const altered=replaceUniqueSourceText(original,anchor,'    if(prepareAttempted)g_inputHostPrepareMayHaveReachedHost=true;\n'+anchor,mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStart',negativeControl:true,scope:'Only restore pre-request creation claim before the write boundary; original actual pipe deadline denial and assertions unchanged.'});
}
if(mutation==='rollback-live-explicit-restore'){
 const original=extract(inert,'inputHostStopLocked').code;const altered=replaceUniqueSourceText(original,'const bool explicitRestoreAfterExit = hostExitConfirmed && inputHostStopRestoresVisibilityWithoutProof(reason);','const bool explicitRestoreAfterExit = inputHostStopRestoresVisibilityWithoutProof(reason);',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStopLocked',negativeControl:true,scope:'Only restore explicit OFF/exit override while exact old child is still alive; actual held managed target and original OFF/return assertions unchanged.'});
}
if(mutation==='rollback-ps-return-xinput'){
 const original=extract(inert,'inputHostReconcileQuarantineOnLifecycleThread').code;const altered=replaceUniqueSourceText(original,': psReturnRequired ? psRetiredReturnProvenLocked()',': false ? psRetiredReturnProvenLocked()',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only restore XInput-plane substitution for retired PS PnP proof; actual accepted old pin, managed disposal and controlled Present/Unknown/Absent reader remain.'});
}
if(mutation==='rollback-exit-final-sweep'){
 const original=extract(inert,'hidHideSweepAllHiddenDevices').code;const altered=replaceUniqueSourceText(original,'const bool guardedExitSweep = reason && std::strcmp(reason,"app-exit")==0;','const bool guardedExitSweep = false && reason && std::strcmp(reason,"app-exit")==0;',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'hidHideSweepAllHiddenDevices',negativeControl:true,scope:'Only bypass app-exit ownership admission; real held child, PS return state and existing sweep API consumer stay executed with controlled driver leaves.'});
}
// Directed semantic controls remove exactly one actual runtime duty hook.
// Real Session/store, pipe consumer and all original assertions stay executed.
if(['rollback-target-reservation','rollback-target-pin-store','rollback-target-settlement'].includes(mutation)){
 const symbol=mutation==='rollback-target-reservation'?'inputHostTargetReturnBeforeCreateLocked':mutation==='rollback-target-pin-store'?'inputHostTargetReturnAcceptPsLocked':'inputHostTargetReturnTargetExitedLocked';
 const original=extract(inert,symbol).code,brace=original.indexOf('{');
 const altered=original.slice(0,brace+1)+'\n    '+(mutation==='rollback-target-reservation'?'return true;':'return;')+' // explicit compiled semantic negative\n'+original.slice(brace+1);
 inert=replaceUniqueSourceText(inert,original,altered,mutation);
 substitutions.push({symbol,negativeControl:true,scope:'Only bypass this selected target duty hook in private compiled TU; actual IO/receipts/ownership and assertions unchanged',beforeSha256:sha(original),afterSha256:sha(altered)});
}
const core=['inputHostTargetReturnAcceptedRecreateLocked','inputHostTargetReturnAfterPsQuarantineLocked','hidHideCliReapPendingLocked','inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked','inputHostApplyHelloCapabilities','inputHostRequestPreparedLocked','inputHostRequestHasExecutionDeadline','psWakeCaptureEndpointOwnership','inputHostRefreshPersonaMatchLocked','inputHostSetBoundPersona','inputHostClearBoundPersona','inputHostXboxPreparedHid','inputHostXboxPreparedRoot','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked','inputHostRequestPsWakeRearm','inputHostConsumePsWakeRearm','inputHostPerformPsWakeRearm','psWakeProbeOwnEndpoint','psWakeDecide','inputHostRecreatePreparedTarget','inputHostRequestWakeRescan','inputHostConsumeWakeRescan','inputHostPerformWakeHotReplace','inputHostWakePhysicalHandshakeReady','sgShouldKeepInputDuringStandby','inputHostRequestPowerRelease','inputHostRequestPowerRearm','inputHostConsumePowerRequests','inputCaptureSuspendSensorsForPower','inputCaptureResumeSensorsForPower','inputHostSubmitPad','inputHostFrameSubmissionCurrent','inputHostPhysicalPublicationSourceAdmitted','inputHostFreshNeutralReady','inputHostStart','inputHostStopLocked','inputHostStop','inputHostClearLocked','inputHostRunQueuedLifecycleWork','inputHostReconcileQuarantine','inputHostReconcileQuarantineOnLifecycleThread','msiSteamDeckRecheckOwnedRemovalDebt','ymStartPipeSeamServe','ymStartEnsureRoute','msiClawOpenModeCommand','msiDinputSubmitModeWriteIntent','msiStartOrderEntrySelfTest','inputHostTargetAdmission','inputHostStartProtectionFinish','nativeInputShortcutToggle','clearInputShortcutRuntime','inputCaptureRefreshRealStickSettings','inputHostTargetReturnHistorySettledLocked','inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup'].map(name=>{const actual=extract(source,name).code;const same=extract(inert,name).code===actual;if(!same&&!['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name)&&!(mutation==='rollback-recovery-inheritance'&&['inputHostStart','inputCaptureRefreshRealStickSettings'].includes(name))&&!(['rollback-cancellation-guards','rollback-request-ownership'].includes(mutation)&&['inputHostPerformPsWakeRearm','inputHostRecreatePreparedTarget','inputHostSubmitPad','inputHostFrameSubmissionCurrent'].includes(name))&&!(mutation==='rollback-ps-binding-revision'&&name==='inputHostPerformPsWakeRearm')&&!(mutation==='rollback-switch-return-rearm'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-switch-fault-owner'&&name==='inputHostRunQueuedLifecycleWork')&&!(mutation==='rollback-stale-off-direction'&&name==='inputHostRunQueuedLifecycleWork')&&!(mutation==='rollback-prepare-claim'&&name==='inputHostStopLocked')&&!(mutation==='rollback-live-explicit-restore'&&name==='inputHostStopLocked')&&!(mutation==='rollback-ps-return-xinput'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-unsent-prepare-claim'&&name==='inputHostStart')&&!(mutation==='rollback-pipe-stage-budget'&&name==='inputHostRequestPreparedLocked')&&!(mutation==='rollback-initial-start-budget'&&name==='inputHostStart')&&!(mutation==='rollback-wake-return-continuation'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-target-reservation'&&name==='inputHostTargetReturnBeforeCreateLocked')&&!(mutation==='rollback-target-pin-store'&&name==='inputHostTargetReturnAcceptPsLocked')&&!(mutation==='rollback-target-settlement'&&name==='inputHostTargetReturnTargetExitedLocked'))throw Error('core modified '+name);return {symbol:name,sha256:sha(actual),executedSha256:sha(extract(inert,name).code),unchanged:same,fixtureEndpointAdapter:['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name),expectedSemanticNegative:!!mutation&&!same&&(mutation==='rollback-ps-endpoint-ownership'?name==='psWakeProbeOwnEndpoint':!['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name))};});

// Bind the real environment as a struct, separate from the function count.
const coldEnvironment=extract(source,'ColdEnvironment','struct').code;
if(extract(inert,'ColdEnvironment','struct').code!==coldEnvironment)throw Error('core modified ColdEnvironment');
const structCore=[{symbol:'ColdEnvironment',kind:'struct',source:'native/virtual_target_return_cold_runtime.h',sha256:sha(coldEnvironment),executedSha256:sha(coldEnvironment),unchanged:true}];
// These headers are deliberately not expanded or faked. The full runner must
// observe every one as an actual compiler include, then bind its frozen bytes.
const compilerHeaderBindings=['native/virtual_target_return_cold_execution.h','native/virtual_target_return_pnp_absence.h','native/virtual_target_return_record.h','native/virtual_target_return_session.h','native/virtual_target_return_store.h','native/virtual_switch_execution_return_runtime.h'].map(source=>({source,sha256:sha(fs.readFileSync(path.join(snapshot,source))),compilerObservationRequired:true}));
const continuationSource='native/virtual_switch_execution_return_runtime.h';
const continuation=fs.readFileSync(path.join(snapshot,continuationSource),'utf8');
for(const match of continuation.matchAll(/^static[^\n;{]*?\b([A-Za-z_]\w*)\s*\(/gm)){
 const name=match[1],actual=extract(continuation,name).code;
 if(core.some(row=>row.symbol===name))throw Error('duplicate core '+name);
 core.push({symbol:name,source:continuationSource,sha256:sha(actual),executedSha256:sha(actual),unchanged:true,compilerObservedHeader:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
}
const readSource=extract(source,'gamepadReadStateUnlocked').code,discoveryStart=readSource.indexOf('    const bool boundaryFullScan ='),discoveryEnd=readSource.indexOf('    // L3（需求单）',discoveryStart);if(discoveryStart<0||discoveryEnd<=discoveryStart)throw Error('Exact XInput discovery slice anchor');const discoveryCode=readSource.slice(discoveryStart,discoveryEnd),maskStart=readSource.indexOf('    const DWORD physicalMask = xinputPhysicalConnectedMask();',discoveryEnd),maskEnd=readSource.indexOf('    // R10-R2 M3',maskStart);if(maskStart<0||maskEnd<=maskStart)throw Error('Exact XInput selected-source mask slice anchor');const maskCode=readSource.slice(maskStart,maskEnd),discoveryFile=path.join(out,'xinput_discovery.inc'),discoveryBytes=discoveryCode+'\n'+maskCode;fs.writeFileSync(discoveryFile,discoveryBytes);const fragments=[{symbol:'gamepadReadStateUnlocked/complete-four-slot-discovery',source:'native/main.cpp',sha256:sha(discoveryCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Actual discovery loop, per-slot identity classification and completed-boundary publication. Only XInput/identity API leaves stay inert; no source-admission flag is forged.'},{symbol:'gamepadReadStateUnlocked/selected-source-publication-mask',source:'native/main.cpp',sha256:sha(maskCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Actual physical/external/selected-source mask decision and publication. Drop/reconfirm remainder and whole BUS are not executed or claimed.'}];
fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);const settingsMutexIsolation=isolateCurrentFixtureSettingsMutex(observeCurrent105Fixture(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8')).text,{runtimeRoot,moduleMaintenance:layout.managedInputContract==='current105-terminal22-v1'});const progressObservation=checkProgress?observeCurrent105Progress(settingsMutexIsolation.text):null;const stageObservation=finalStages?observeFinalStages(progressObservation?.text??settingsMutexIsolation.text):null;const fixture=stageObservation?.text??progressObservation?.text??settingsMutexIsolation.text;fs.writeFileSync(path.join(out,'fixture.cpp'),fixture);fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/target_return_cases.inc'),path.join(out,'target_return_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc'),path.join(out,'cold_archive_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),path.join(out,'persona_lifecycle_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc'),path.join(out,'pipe_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc'),path.join(out,'off_lifecycle_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc'),path.join(out,'prepare_ack_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc'),path.join(out,'ps_return_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc'),path.join(out,'tuple_recovery_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc'),path.join(out,'ps_wake_cases.inc'));fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({records,sourceExpansion,core,structCore,compilerHeaderBindings,substitutions,fragments},null,2));
// Directed header negatives compile a changed private header. Raw production
// snapshots stay intact, with their hash bound as transformation inputs.
const mutatedHeaderFiles=[],mutatedHeaderInputs=[];
if(mutation==='rollback-wake-retirement-identity'||mutation==='rollback-wake-start-budget'){
 const rel='native/virtual_wake_execution_runtime.h',original=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const from=mutation==='rollback-wake-retirement-identity'?'static void retireLocked(Work w,const char* reason) {':'if(episode.startAttempts>=kWakeHotReplaceMaxAttempts)';
 const to=mutation==='rollback-wake-retirement-identity'?'static void retireLocked(const Work& w,const char* reason) {':'if(false&&episode.startAttempts>=kWakeHotReplaceMaxAttempts)';
 const altered=replaceUniqueSourceText(original,from,to,mutation),generated=path.join(snapshot,'native/virtual_wake_execution_runtime.mutant.h');
 fs.writeFileSync(generated,altered,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "virtual_wake_execution_runtime.h"','#include "virtual_wake_execution_runtime.mutant.h"',mutation+'-include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_wake_execution_runtime.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Actual changed private production header; restore only the selected proven bug. Raw source, original assertions and actual owned Host path stay unchanged.'});
}
if(mutation==='rollback-queued-player-binding'){
 const rel='native/virtual_controller_queued_start_runtime.h',original=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const altered=replaceUniqueSourceText(original,'if(!startReason(reason))return reason;','if(true||!startReason(reason))return reason;',mutation);
 const generated=path.join(snapshot,'native/virtual_controller_queued_start_runtime.mutant.h');fs.writeFileSync(generated,altered,{flag:'wx'});
 inert=replaceUniqueSourceText(inert,'#include "virtual_controller_queued_start_runtime.h"','#include "virtual_controller_queued_start_runtime.mutant.h"',mutation+'-include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_controller_queued_start_runtime.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only remove admitted player provenance from actual queued initial/persona Start; same real managed pipe and original cases.'});
}
if(['rollback-ps-return-pin','rollback-ps-return-physical','rollback-ps-return-family'].includes(mutation)){
 const rel=mutation==='rollback-ps-return-pin'?'native/ps_endpoint_ownership.h':'native/ps_endpoint_return_runtime.h';const original=fs.readFileSync(path.join(snapshot,rel),'utf8');let altered=original;
 if(mutation==='rollback-ps-return-pin')altered=replaceUniqueSourceText(altered,'retirement_ = {true, expected_, state_};','retirement_ = {true, expected_, Snapshot{}};',mutation);
 if(mutation==='rollback-ps-return-physical')altered=replaceUniqueSourceText(altered,'return absent; // keep the exact debt until physical restore/hold also succeeds','return absent && g_psEndpointOwnership.confirmRetirement(debt.owner.runId,debt.owner.epoch);',mutation);
 if(mutation==='rollback-ps-return-family'){altered=replaceUniqueSourceText(altered,'if(family(detail->DevicePath))return false;','if(false&&family(detail->DevicePath))return false;',mutation+'-path');altered=replaceUniqueSourceText(altered,'if(family(id))return false;','if(false&&family(id))return false;',mutation+'-ancestor');}
 const name=path.basename(rel,'.h')+'.mutant.h',generated=path.join(snapshot,'native',name);fs.writeFileSync(generated,altered,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "'+path.basename(rel)+'"','#include "'+name+'"',mutation+'-include');fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/'+name);mutatedHeaderInputs.push(rel);substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only selected retired PS ownership/proof omission in actual compiled production header; raw sources and actual child/reader cases unchanged.'});
}
// Header-defined return readers must use the SAME admitted SetupAPI/CM ends
// as the existing main.cpp reader. Main-text substitution cannot reach includes.
{
 const rel='native/ps_endpoint_return_runtime.h',raw=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const ports={SetupDiGetClassDevsW:'fixturePsGetClassDevs',SetupDiEnumDeviceInterfaces:'fixturePsEnum',SetupDiGetDeviceInterfaceDetailW:'fixturePsDetail',SetupDiDestroyDeviceInfoList:'fixturePsDestroy',CM_Get_Device_IDW:'fixturePsDeviceId',CM_Get_Parent:'fixturePsParent'};
 const called=[...new Set([...raw.matchAll(/\b(SetupDi\w+|CM_\w+)\s*\(/g)].map(m=>m[1]))];if(called.some(name=>!Object.hasOwn(ports,name)))throw Error('UNADMITTED_PS_RETURN_DEVICE_API:'+called.join(','));
 const target=['rollback-ps-return-physical','rollback-ps-return-family'].includes(mutation)?'ps_endpoint_return_runtime.mutant.h':'ps_endpoint_return_runtime.h';
 const wrapper=Object.entries(ports).map(([api,port])=>'#define '+api+' '+port).join('\n')+'\n#include "'+target+'"\n'+Object.keys(ports).map(api=>'#undef '+api).join('\n')+'\n';
 const generated=path.join(snapshot,'native/ps_endpoint_return_runtime.fixture.h');fs.writeFileSync(generated,wrapper,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "'+target+'"','#include "ps_endpoint_return_runtime.fixture.h"','ps-return-explicit-device-ends');fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/ps_endpoint_return_runtime.fixture.h');
 substitutions.push({symbol:rel,scope:'Original production return algorithm compiled verbatim; all six SetupAPI/CM reads redirected only within this include to pre-existing controlled leaf functions. Unknown device API rejects before compile.',originalApiCalls:called,ports,wrapperSha256:sha(wrapper),productionHeaderSha256:sha(raw),controlAlgorithmChanged:false});
}
for(const relative of fixtureToolHeaders)fs.copyFileSync(path.join(snapshot,relative),path.join(out,path.basename(relative)));
const compiledGenerated=[...new Set([...fixtureToolHeaders.map(relative=>path.basename(relative)),'fixture.cpp','cold_archive_cases.inc','target_return_cases.inc','persona_lifecycle_cases.inc','pipe_cases.inc','off_lifecycle_cases.inc','prepare_ack_cases.inc','ps_return_cases.inc','tuple_recovery_cases.inc','ps_wake_cases.inc','xinput_discovery.inc','source/native/main.inert.cpp',...mutatedHeaderFiles])].map(rel=>{const p=path.join(out,rel);return{path:p,sha256:sha(fs.readFileSync(p))};});
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';const native=path.join(snapshot,'native'),json=path.join(snapshot,'deps/json'),webview=path.join(snapshot,'deps/webview2/build/native/include');
const build=calibrationToolchainPreamble(vc)+'chcp 65001 >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /showIncludes /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(out,'fixture.cpp')+'" /I"'+native+'" /I"'+json+'" /I"'+webview+'" /Fo"'+path.join(out,'fixture.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /showIncludes /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(native,'dsu_server.cpp')+'" /Fo"'+path.join(out,'dsu.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /showIncludes /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(native,'steam_original_recovery_engine.cpp')+'" /I"'+json+'" /Fo"'+path.join(out,'steam-original-recovery.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo "'+path.join(out,'fixture.obj')+'" "'+path.join(out,'dsu.obj')+'" "'+path.join(out,'steam-original-recovery.obj')+'" /Fe"'+path.join(out,'CoordinatorFixture.exe')+'" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "'+path.join(repo,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')+'" "%WindowsSdkDir%Lib\\%WindowsSDKVersion%um\\x64\\WindowsApp.lib" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(out,'build.cmd'),build);const p=spawnSync('cmd.exe',['/d','/c',path.join(out,'build.cmd')],{cwd:out,windowsHide:true,encoding:'utf8',timeout:300000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(out,'build.log'),(p.stdout??'')+(p.stderr??''));if(p.status!==0)throw Error('full native fixture compile failed '+p.status+' '+path.join(out,'build.log'));
const closure=nativeCompilerInputClosure({records,buildLog:(p.stdout??'')+(p.stderr??''),snapshotRoot:snapshot,generatedFiles:compiledGenerated.map(r=>r.path),requiredSources:[...expansionInputs,...mutatedHeaderInputs,'native/dsu_server.cpp','native/steam_original_recovery_engine.cpp',...records.filter(r=>r.path.startsWith('tools/')).map(r=>r.path)]});
for(const r of compiledGenerated)if(sha(fs.readFileSync(r.path))!==r.sha256)throw Error('GENERATED_INPUT_CHANGED_DURING_COMPILE:'+r.path);

for(const binding of compilerHeaderBindings){
 const file=path.resolve(path.join(snapshot,binding.source)).toLowerCase();
 if(!closure.observedIncludePaths.includes(file))throw Error('REQUIRED_REAL_COMPILER_HEADER_NOT_OBSERVED:'+binding.source);
 if(sha(fs.readFileSync(file))!==binding.sha256)throw Error('REQUIRED_REAL_COMPILER_HEADER_CHANGED:'+binding.source);
}
const snapshotCopies=[...records];records.splice(0,records.length,...closure.records.map(row=>row.path===LIFECYCLE_LOG_PRODUCER?{...row,inputRole:LIFECYCLE_LOG_DATA_ROLE}:row));fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({records,sourceExpansion,core,structCore,compilerHeaderBindings,substitutions,fragments,settingsMutexIsolation,...(progressObservation?{progressObservation}:{}),...(stageObservation?{stageObservation}:{}),compiledGenerated,snapshotCopies,copiedButUncompiled:closure.copiedButUncompiled,observedIncludePaths:closure.observedIncludePaths,sourceClosureMeaning:closure.meaning+' Expanded raw runtime sources are pinned primary transformation inputs; sourceExpansion binds them to main.expanded.cpp and compiledGenerated binds the executed main.inert.cpp.'},null,2));

const changed=records.filter(row=>sha(fs.readFileSync(row.original))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256).map(row=>row.path);if(changed.length)throw Error('Compile inputs changed');if(managedProof&&(sha(fs.readFileSync(managedDll))!==managedProof.dll.sha256||sha(fs.readFileSync(managedProof.manifest))!==managedProof.manifestSha256))throw Error('Managed prerequisite changed');
const identity=file=>({path:path.resolve(file),sha256:sha(fs.readFileSync(file))});const result={status:'COMPILED_NOT_EXECUTED',compileOnly:true,sourceFullTranslationUnit:true,mutation,compilerExitCode:p.status,compilerSignal:p.signal??null,compilerError:p.error?.message??null,buildScript:identity(path.join(out,'build.cmd')),sourceManifest:identity(path.join(out,'source-manifest.json')),exe:identity(path.join(out,'CoordinatorFixture.exe')),managedProof,changed,fixtureExecuted:false,nativeExecuted:false,normalProductMainExecuted:false,realDeviceIo:false,results:[],runBranchReached:false};fs.writeFileSync(path.join(out,'COMPILE-ONLY.json'),JSON.stringify(result,null,2));return result;

}
