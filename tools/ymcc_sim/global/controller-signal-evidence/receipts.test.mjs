import './managed-terminal.test.mjs';
import './current-registration.test.mjs';
import './primary-thread.test.mjs';
import {validateExitFence,verifyFenceFunctionIdentity,exitFenceArtifactRows} from './exit-fence-receipts.mjs';
import {currentFragments} from './source-fragments.mjs';
import {validateAdapters,MAX_ARTIFACT_INPUTS} from '../runner.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';
import {validateOwnedCleanupComposition, validateCleanupRuntime, compositionContract, cleanupFunction, prepareCompositionToolchain, verifyNestedToolFiles, validateCompositionNestedClosure, validateCompositionFunctions} from './owned-cleanup-composition-receipts.mjs';
import {validateSensorEnsure, sensorEnsureFragments} from './sensor-ensure-receipts.mjs';
import {schema as consumerSchema} from './consumer-runner.mjs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {test, after} from 'node:test';
import {CHILDREN, validateReceipts} from './receipts.mjs';
import {REPO, sourceInputs, getSignalEvidenceAdapters, lifecycleContract, toolInputs, deduplicateArtifactRows, signalArtifactRows} from './adapter.mjs';
import {consumerEnvironment} from './environment.mjs';
import {normalizeLifecycleContract} from '../capability-lifecycle/index.mjs';

const registrationOnly=process.env.YMCC_SIGNAL_EVIDENCE_REGISTRATION_ONLY==='1';
const receipts = process.env.YMCC_SIGNAL_EVIDENCE_RECEIPTS;
const binding = process.env.YMCC_SIGNAL_EVIDENCE_SOURCE_FILE;
assert(registrationOnly || receipts && binding && path.isAbsolute(receipts) && path.isAbsolute(binding), 'Fresh actual-consumer receipts and source snapshot required; no default old result is admitted');
const tools = toolInputs(REPO);
const sourceHashes = registrationOnly ? undefined : JSON.parse(fs.readFileSync(binding, 'utf8'));
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'ymcc-controller-diagnostic-rejection-'));
after(() => fs.rmSync(temporary, {recursive: true, force: true}));
let sequence = 0;
function fixture(change) {
  const output = path.join(temporary, String(++sequence));
  for (const name of [...CHILDREN, ...['default', 'partial', 'explicit'].map(x => x + '-result.txt')]) {
    const target = path.join(output, name); fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(path.join(receipts, name), target);
  }
  const edit = (name, mutate) => {
    const file = path.join(output, name), value = JSON.parse(fs.readFileSync(file, 'utf8'));
    mutate(value); fs.writeFileSync(file, JSON.stringify(value));
  };
  change?.(output, edit);
  return () => validateReceipts({repo: REPO, output, sourceHashes, tools});
}

if(!registrationOnly){
test('actual fresh positive child receipts are accepted in their diagnostic scope', () => {
  const result = fixture()(); assert.equal(result.cases.length, CHILDREN.length); assert.equal(result.receiptHashes.length, CHILDREN.length + 3);
});
test('missing child receipt cannot inherit parent PASS', () => {
  assert.throws(fixture(output => fs.unlinkSync(path.join(output, 'timer-adapter/RESULT.json'))), /ENOENT/);
});
test('explicit child failure cannot inherit parent PASS', () => {
  assert.throws(fixture((_, edit) => edit('timer-recorder/RESULT.json', value => {value.pass = false;})), /CHILD_NOT_PASS/);
});
test('child source mismatch cannot use the current wrapper source binding', () => {
  assert.throws(fixture((_, edit) => edit('timer-adapter/RESULT.json', value => {value.binding.sources['native/main.cpp'].sha256 = '0'.repeat(64);})), /SOURCE_HASH_MISMATCH/);
});
test('a source omitted from the declared closure is rejected', () => {
  assert.throws(fixture((_, edit) => edit('source-binding.json', value => {value.source[0].path = '../outside.cpp';})), /SOURCE_OUTSIDE_DECLARATION_OR_DUPLICATE/);
});
test('lost witness negative is rejected', () => {
  assert.throws(fixture((_, edit) => edit('semantic-witness/RESULT.json', value => {value.cases.find(row => row.expectedNegative).exit = 0;})), /WITNESS_SPECIFIED_NEGATIVES/);
});
test('lost bounded timer mutant is rejected', () => {
  assert.throws(fixture((_, edit) => edit('timer-recorder/RESULT.json', value => {value.negatives[0].caught = false;})), /TIMER_SPECIFIED_NEGATIVES/);
});
test('removed actual resume hook must remain an expected failure', () => {
  assert.throws(fixture((_, edit) => edit('resume-generation/RESULT.json', value => {value.negativeExpectedFailureDetected = false;})), /RESUME_ACTUAL_HOOK_AND_NEGATIVE/);
});
test('ordinary counter mutation must fail at actual compile consumer', () => {
  assert.throws(fixture((_, edit) => edit('timer-adapter/RESULT.json', value => {value.profiles[0].negativeCompiles[0].exitCode = 0;})), /ACTUAL_ATOMIC_NEGATIVES/);
});
test('runtime evidence text from a failed acquisition cannot pass', () => {
  assert.throws(fixture(output => fs.writeFileSync(path.join(output, 'partial-result.txt'), 'cases=51 failures=1 RESULT=FAIL')), /ACQUISITION_ACTUAL_VARIANT/);
});
test('formal descriptor binds actual main, production helpers and persistent parent', () => {
  const adapter = getSignalEvidenceAdapters(REPO)[0];
  assert.equal(adapter.id, 'controller.signal-evidence');
  assert(sourceInputs.includes('native/main.cpp') && adapter.evidenceInputs.includes('native/input_timer_evidence.h'));
  assert.equal(normalizeLifecycleContract(lifecycleContract).features[0].persistence, 'persistent');
  assert(lifecycleContract.features[0].resources.includes('workers') && lifecycleContract.features[0].resources.includes('handles'), 'Parent exit acceptance must not omit native workers or kernel handles');
  assert(adapter.resultFiles.includes('diagnostic-result.json') && adapter.artifactInputs.length > 600);
  assert(!adapter.resultFiles.includes('result.json'), 'Global runner owns result.json');
  assert.equal(lifecycleContract.features[0].evidenceFile, 'diagnostic-result.json');
});
test('inherited compiler, Node, Python and fixture overrides cannot escape the environment allowlist', () => {
  const poisoned = {SystemRoot: 'C:/Windows', TEMP: 'C:/outside', VCToolsVersion: 'old', VCVARS_USER_VERSION: 'old',
    VC_VC_IncludePath: 'C:/foreign', VSCMD_ARG_TGT_ARCH: 'x86', CL: '/FIforeign.h', _CL_: '/FIforeign.h',
    INCLUDE: 'C:/foreign', LIB: 'C:/foreign', LINK: '/LIBPATH:foreign', NODE_OPTIONS: '--require foreign',
    PYTHONPATH: 'C:/foreign', YMCC_MSI_DECODE_VCVARS: 'C:/foreign.cmd', Path: 'C:/foreign'};
  const env = consumerEnvironment({ambient: poisoned, tools, out: temporary, nodeExecutable: process.execPath});
  for (const key of Object.keys(poisoned).filter(key => !['SystemRoot', 'TEMP'].includes(key)))
    assert(!Object.keys(env).some(actual => actual.toLowerCase() === key.toLowerCase() && env[actual] === poisoned[key]), key);
  assert.equal(env.TEMP, path.join(temporary, 'tool-temp'));
});
test('an undeclared actual compiler path is rejected even when declared tools remain intact', () => {
  assert.throws(fixture((_, edit) => edit('timer-recorder/RESULT.json', value => {value.compiler.path = 'C:/foreign/cl.exe';})), /ACTUAL_TOOL_PATH_MISMATCH/);
});
test('an actual compiler hash mismatch is rejected', () => {
  assert.throws(fixture((_, edit) => edit('timer-adapter/RESULT.json', value => {value.compiler.sha256 = '0'.repeat(64);})), /ACTUAL_TOOL_HASH_MISMATCH/);
});
test('another actual SDK cannot inherit the declared SDK check', () => {
  assert.throws(fixture((_, edit) => edit('descriptor-decode/source-binding.json', value => {value.sdkVersion = '10.0.0.0';})), /ACTUAL_SDK_MISMATCH/);
});
test('foreign include paths in the actual resume consumer are rejected', () => {
  assert.throws(fixture((_, edit) => edit('resume-generation/RESULT.json', value => {value.toolchain.explicitSdkInclude[0] = 'C:/foreign';})), /RESUME_ACTUAL_INCLUDE_LIB_MISMATCH/);
});

test('candidate-only worker result cannot stand in for current production retirement', () => {
  assert.throws(fixture((_, edit) => edit('worker-retirement/RESULT.json', value => {value.productionRetirementAccepted = false;})), /ACTUAL_CURRENT_WORKER_RETIREMENT/);
});
test('retirement must actually execute and accept the current synchronous worker', () => {
  assert.throws(fixture((_, edit) => edit('worker-retirement/RESULT.json', value => {value.profiles.find(row => row.route === 'synchronous').isCurrentProductionRoute = false;})), /RETIREMENT_CURRENT_SOURCE_NOT_ACCEPTED/);
});
test('removing a fault-stop guard must preserve the precise expected failure', () => {
  assert.throws(fixture((_, edit) => edit('worker-retirement/RESULT.json', value => {value.profiles.find(row => row.route === 'negative-sync-without-fault-stop').failures = 0;})), /RETIREMENT_STOP_REMOVAL_NOT_REJECTED/);
});

test('legacy worker must fail the current retirement contract at the observed boundary', () => {
  assert.throws(fixture((_, edit) => edit('worker-retirement/RESULT.json', value => {value.profiles.find(row => row.route === 'legacy').legacyRejectedAsSafe = false;})), /RETIREMENT_LEGACY_DEFECT_NOT_REPRODUCED/);
});

test('candidate-only Stop ownership cannot stand in for current production', () => {
  assert.throws(fixture((_, edit) => edit('stop-wait/RESULT.json', value => {value.productionOwnershipCheckAccepted = false;})), /ACTUAL_CURRENT_STOP_OWNERSHIP/);
});
test('checked Stop may not lose an unverified worker record', () => {
  assert.throws(fixture((_, edit) => edit('stop-wait/RESULT.json', value => {value.profiles.find(row=>row.route==='checked').cases[0].lostUnverifiedOwner = true;})), /STOP_CURRENT_SOURCE_NOT_ACCEPTED/);
});
test('real kernel thread join cannot be replaced by process-exit cleanup', () => {
  assert.throws(fixture((_, edit) => edit('stop-wait/RESULT.json', value => {value.kernelProfiles[0].cases[0].ownedThreadJoined = false;})), /STOP_REAL_KERNEL_OWNERSHIP_UNPROVEN/);
});
test('ordinary stop receipt must survive disabled detail logging', () => {
  assert.throws(fixture((_, edit) => edit('stop-wait/RESULT.json', value => {value.ordinaryStopLogClassified = false;})), /ACTUAL_CURRENT_STOP_OWNERSHIP/);
});

test('candidate-only cooperative startup cannot stand in for current production', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', v => {v.productionStartupWaitAccepted=false;})), /ACTUAL_CURRENT_STARTUP_WAIT_REQUIRED/);
});
test('startup test cannot lose the historical real Stop timeout observation', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', v => {v.profiles.find(r=>r.route==='legacy').cases[0].firstWaitResult=0;})), /STARTUP_STOP_WINDOW_NOT_REPRODUCED_OR_FIXED/);
});
test('startup full-source success requires actual thread join and both owned handle closures', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', v => {v.profiles.find(r=>r.route==='cooperative').cases[0].ownedThreadJoined=false;})), /STARTUP_OWNERSHIP_OR_BOUNDARY_UNPROVEN/);
});
test('normal startup cannot gain acceptance by deleting its 2500ms defer', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', v => {v.profiles.find(r=>r.route==='cooperative').cases.find(c=>c.scenario===2).normalDelayElapsedMs=0;})), /STARTUP_NORMAL_2500MS_DEFER_NOT_PRESERVED/);
});
test('startup header unit requires explicit owned thread joins', () => {
  assert.throws(fixture((_, edit) => edit('startup-wait/result.json', v => {v.profiles[0].positive.threadsJoined=0;})), /STARTUP_UNIT_EXPLICIT_JOINS_REQUIRED/);
});
test('notification and predicate removal negatives may not silently pass', () => {
  assert.throws(fixture((_, edit) => edit('startup-wait/result.json', v => {v.profiles[0].negativeControls[0].exitCode=0;})), /STARTUP_UNIT_SPECIFIED_NEGATIVES_REQUIRED/);
});

test('startup controller failure may not discard the last unjoined ownership context', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', v => {v.negativeProfiles[0].cases[0].contextRetained=true;})), /STARTUP_FAILURE_CLEANUP_UNPROVEN/);
});
test('startup controller failure must remain a failed case even after safe fixture cleanup', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', v => {v.negativeProfiles[0].exitCode=0;})), /STARTUP_CONTROLLER_FAILURE_NOT_REJECTED/);
});

test('candidate-only monotonic fence cannot stand in for current production', () => {
 assert.throws(fixture((_,edit)=>edit('exit-fence/RESULT.json',v=>{v.productionMonotonicFenceAccepted=false;})),/ACTUAL_CURRENT_MONOTONIC_FENCE_REQUIRED/);
});
test('late-init clear and its restored mutant must retain their actual failure', () => {
 assert.throws(fixture((_,edit)=>edit('exit-fence/RESULT.json',v=>{v.profiles.find(r=>r.route==='negative-restore-clear').exitCode=0;})),/FENCE_CLEAR_DEFECT_NOT_REJECTED/);
});
test('fence concurrency must join all owned fixture threads', () => {
 assert.throws(fixture((_,edit)=>edit('exit-fence/RESULT.json',v=>{v.profiles.find(r=>r.route==='monotonic').threadsJoined=0;})),/FENCE_THREADS_OR_BOUNDARY_UNPROVEN/);
});

test('native selftest restoration must stay source-bound and separate from production init', () => {
 assert.throws(fixture((_,edit)=>edit('exit-fence/RESULT.json',v=>{v.nativeSelftestFixtureResetBound=false;})),/NATIVE_SELFTEST_FIXTURE_RESTORATION_NOT_BOUND/);
});

test('partial-init cannot omit a specified failing resource route',()=>{
 assert.throws(fixture((_,edit)=>edit('partial-init/RESULT.json',v=>{v.results.pop();})),/PARTIAL_GROUP_BOUNDARY/);
});
test('partial-init cannot claim full Init was executed',()=>{
 assert.throws(fixture((_,edit)=>edit('partial-init/RESULT.json',v=>{v.results[0].result.fullInitExecuted=true;})),/PARTIAL_ACTUAL_CONSUMER_SCOPE/);
});
test('partial-init cannot relabel a live sensor owner as disposed',()=>{
 assert.throws(fixture((_,edit)=>edit('partial-init/RESULT.json',v=>{const c=v.results[0].result.profiles[0].cases.find(c=>c.name==='partial-sensor-shutdown-fails-then-settles');c.observations[0].sensorDisposalProven=true;})),/PARTIAL_SENSOR_RETENTION_IS_NOT_DISPOSAL/);
});
test('partial-init rejects a negative declaration without actual leaked resource counters',()=>{
 assert.throws(fixture((_,edit)=>edit('partial-init/RESULT.json',v=>{const r=v.results.find(r=>r.variant==='skip-gm-delete-mutant');r.result.profiles[0].cases.find(c=>c.name==='normal-ready').observations[0].gmObjectsAlive=0;})),/PARTIAL_NEGATIVE_RESOURCE_WITNESS_REQUIRED/);
});
test('partial-init current consumer must match its captured fragment identity',()=>{
 assert.throws(fixture((_,edit)=>edit('partial-init/RESULT.json',v=>{v.results[0].result.selectedFragments.shutdown.sha256='0'.repeat(64);})),/PARTIAL_CURRENT_FRAGMENT_BINDING/);
});


test('startup kernel event-delay evidence cannot be replaced by a Sleep claim', () => {
  assert.throws(fixture((_, edit) => edit('startup-lifecycle/RESULT.json', value => {
    value.realOwnedKernelThreadWaitCloseAndEventDelay=false;
    value.realOwnedKernelThreadWaitCloseAndSleep=true;
  })), /ACTUAL_CURRENT_STARTUP_WAIT_REQUIRED/);
});
test('current partial cleanup cannot borrow the historical main identity', () => {
  assert.throws(fixture((_, edit) => edit('partial-init/RESULT.json', value => {
    const old=value.results.find(r=>r.variant==='baseline').result.sourceHashes['native/main.cpp'];
    value.results.find(r=>r.variant==='current').result.sourceHashes['native/main.cpp']=old;
  })), /SOURCE_HASH_MISMATCH/);
});
test('historical partial cleanup requires its exact preserved dependency pin', () => {
  assert.throws(fixture((_, edit) => edit('partial-init/RESULT.json', value => {
    value.results.find(r=>r.variant==='baseline').result.sourceHashes['baseline-dependencies/native/main.cpp']='0'.repeat(64);
  })), /PARTIAL_HISTORICAL_SOURCE_PIN_MISMATCH/);
});
test('missing admission must retain its witnessed premature cleanup failure', () => {
  assert.throws(fixture((_, edit) => edit('stop-receipt/RESULT.json', value => {
    value.profiles.find(r=>r.route==='negative-without-admission').cases[0].hostCleanupWhileWorkerLive=0;
  })), /STOP_RECEIPT_LOST_OWNER/);
});

// These extend the 45 existing rejection/acceptance gates only when fresh full
// receipts are supplied. Registration-only mode never admits a receipt PASS.
for(const [label,change,pattern] of [
 ['current Ensure fragment mismatch',v=>{v.extractedFunctions.current.inputCaptureSensorThreadEnsure.sha256='0'.repeat(64);},/SENSOR_ENSURE_FRAGMENT_HASH/],
 ['missing complete Post fragment',v=>{delete v.extractedFunctions.current.inputCaptureSensorThreadPost;},/SENSOR_ENSURE_FOUR_COMPLETE_FRAGMENTS/],
 ['historical function pin rewritten',v=>{v.extractedFunctions.historical.inputCaptureSensorThreadEnsure.sha256='0'.repeat(64);},/SENSOR_ENSURE_FRAGMENT_HASH|SENSOR_ENSURE_HISTORICAL_FUNCTION_PIN/],
 ['lost kernel join',v=>{v.rows.find(row=>row.mode==='kernel'&&row.variant==='current').summary.threadsJoined=0;},/SENSOR_ENSURE_PREEXIT_SUMMARY_LOG|SENSOR_ENSURE_REAL_JOIN_CLOSE_PRIVATE_ZERO/],
 ['private kernel resource left live',v=>{v.rows.find(row=>row.mode==='kernel'&&row.variant==='current').summary.ownedBeforeExit=1;},/SENSOR_ENSURE_PREEXIT_SUMMARY_LOG|SENSOR_ENSURE_REAL_JOIN_CLOSE_PRIVATE_ZERO/],
 ['lost specified WaitFailed negative build',v=>{v.rows=v.rows.filter(row=>!(row.profile==='Checked'&&row.mode==='kernel'&&row.variant==='wait-failed-treated-alive'));},/SENSOR_ENSURE_4_POSITIVE_12_NEGATIVE_BUILDS/],
 ['unchecked-close mutant returned success',v=>{v.rows.find(row=>row.variant==='restore-unchecked-close').exit=0;},/SENSOR_ENSURE_BUILD_OR_RUNTIME_OUTCOME/],
 ['tool path changed',v=>{v.compiler.path='C:/foreign/cl.exe';},/ACTUAL_TOOL_PATH_MISMATCH/],
 ['SDK identity changed',v=>{v.compiler.sdkVersion='10.0.0.0';},/ACTUAL_SDK_MISMATCH/],
 ['runtime source closure omitted runner',v=>{delete v.runtime_sourceClosure['tools/input_capture_sensor_ensure_selftest.py'];},/SENSOR_ENSURE_RUNTIME_CLOSURE_INCOMPLETE/],
 ['intermediate old main relabelled current',v=>{v.actualMainSha256='d7d3884307855dc7a12ad1296df4e4e37b3b69be55fabd1e8a2d8da807f5feca';v.mainInputPath='C:/old/main.cpp';},/SENSOR_ENSURE_CURRENT_MAIN_NOT_APPROVED|SENSOR_ENSURE_OLD_SNAPSHOT_AS_CURRENT/],
 ['parent lifecycle forged complete',v=>{v.wholeLifecycleAcceptance=true;},/SENSOR_ENSURE_SCOPE/],
]) test('sensor Ensure rejects '+label,()=>assert.throws(fixture((_,edit)=>edit('sensor-ensure/RESULT.json',change)),pattern));
for(const [label,change,pattern] of [
 ['provisional wrapper masquerades as production',v=>{v.wrapperIntegrated=false;},/PIPE_COMPOSITION_ACTUAL_WRAPPER_REQUIRED/],
 ['source changed during execution',v=>{v.sourceChanged=['native/main.cpp'];},/PIPE_COMPOSITION_ACTUAL_WRAPPER_REQUIRED/],
 ['helper-only status claims integrated PASS',v=>{v.status='HELPER_COMPOSITION_PASS_PARENT_WRAPPER_NOT_INTEGRATED';},/CHILD_NOT_PASS/],
 ['missing actual Owned predicate',v=>{delete v.functions.inputHostOwnedCleanupProvenLocked;},/PIPE_COMPOSITION_FUNCTION_SET/],
 ['main wrapper function hash mismatch',v=>{v.functions.inputHostPipeRetirementProvenLocked.sha256='0'.repeat(64);},/PIPE_COMPOSITION_CURRENT_FUNCTION_HASH/],
 ['runtime header omitted from source closure',v=>{v.sources=v.sources.filter(row=>row.relative!=='native/input_host_pipe_transport.h');},/PIPE_COMPOSITION_SOURCE_CLOSURE/],
 ['current receipt borrows historical whole-main pin',v=>{v.sources.find(row=>row.relative==='native/main.cpp').sha256='4765796827adbb08419aede64a6c0f844c5d725e122fd2c102ed31920abd35d9';},/SOURCE_HASH_MISMATCH/],
 ['one specified negative build omitted',v=>{v.runs=v.runs.filter(row=>!(row.profile==='Checked'&&row.variant==='unchecked-raw-slot-close'));},/PIPE_COMPOSITION_TEN_BUILD_MATRIX/],
 ['pending-only mutant returned success',v=>{v.runs.find(row=>row.variant==='pending-only-adapter').exit=0;},/PIPE_COMPOSITION_ACTUAL_BUILD_OUTCOME/],
 ['wrong precise negative label',v=>{v.runs.find(row=>row.variant==='ignore-pipe-gate').failedAssertions=['unspecified error'];},/PIPE_COMPOSITION_NEGATIVE_ASSERTIONS/],
 ['runtime path points outside fresh owner',v=>{v.runs.find(row=>row.variant==='current').result='C:/foreign/result.json';},/PIPE_COMPOSITION_RUNTIME_BINARY_IDENTITY/],
 ['binary differs from executed fixture',v=>{v.runs.find(row=>row.variant==='current').binarySha256='0'.repeat(64);},/PIPE_COMPOSITION_RUNTIME_BINARY_IDENTITY/],
 ['captured toolchain pin changed',v=>{v.toolchain.manifestSha256='0'.repeat(64);},/PIPE_COMPOSITION_CAPTURE_IDENTITY/],
 ['actual compiler path changed',v=>{v.toolchain.tools[0].path='C:/foreign/cl.exe';},/ACTUAL_TOOL_PATH_MISMATCH/],
 ['child claims complete hardware acceptance',v=>{v.productionOrHardwareAccepted=true;},/PIPE_COMPOSITION_SCOPE/],
]) test('pipe owned-cleanup composition rejects '+label,()=>assert.throws(fixture((_,edit)=>edit('owned-cleanup-composition/RESULT.json',change)),pattern));

}

if(registrationOnly){
 const contract=consumerSchema.sensorEnsure;
 const digest=value=>createHash('sha256').update(value).digest('hex');
 test('registration retains the exact original eighteen children and appends Ensure nineteenth',()=>{
  assert.deepEqual(CHILDREN.slice(0,18),['source-binding.json','descriptor-decode/source-binding.json','semantic-witness/RESULT.json','timer-recorder/RESULT.json','resume-generation/RESULT.json','timer-adapter/RESULT.json','worker-retirement/RESULT.json','stop-wait/RESULT.json','startup-wait/result.json','startup-lifecycle/RESULT.json','exit-fence/RESULT.json','partial-init/RESULT.json','stop-receipt/RESULT.json','exit-continuation/RESULT.json','dsu-owner/RESULT.json','sensor-owners/RESULT.json','x2-owner/RESULT.json','gamebar-owner/RESULT.json']);
  assert.equal(CHILDREN.length,22);assert.equal(CHILDREN[18],contract.child);assert.equal(CHILDREN[19],consumerSchema.ownedCleanupComposition.child);assert.equal(new Set(CHILDREN).size,22);
 });
 test('registration preserves the 731 existing native/tool gates and all their pins',()=>{
  assert.equal(tools.files.length,731);
  for(const row of tools.files)assert.equal(digest(fs.readFileSync(row.path)),row.sha256);
 });
 test('registration includes all executable Ensure source and supplemental identity dependencies',()=>{
  assert.equal(new Set(sourceInputs).size,sourceInputs.length);
  for(const name of [...contract.requiredSources,'tools/ymcc_sim/global/controller-signal-evidence/sensor-ensure-receipts.mjs'])assert(sourceInputs.includes(name),name);
  assert(sourceInputs.every(name=>fs.existsSync(path.join(REPO,name))));
  const supplemental=path.join(REPO,contract.supplementalAuthority.path);assert.equal(digest(fs.readFileSync(supplemental)),contract.supplementalAuthority.sha256);
 });
 test('registration fixes historical main path/hash and complete historical function pins',()=>{
  assert.equal(contract.historicalMain.sha256,'50ade4c88674fac353bf2445033fc6b34becc76ad7815da4ea0d28e8d6a307db');
  assert.equal(digest(fs.readFileSync(contract.historicalMain.path)),contract.historicalMain.sha256);
  const parts=sensorEnsureFragments(fs.readFileSync(contract.historicalMain.path,'utf8').replace(/^\uFEFF/,''));
  assert.deepEqual(Object.fromEntries(Object.entries(parts).map(([name,row])=>[name,row.sha256])),contract.historicalFunctions);
 });
 test('registration has four positive and twelve specifically named negative builds',()=>{
  assert.deepEqual(contract.profiles,['Release','Checked']);assert.deepEqual(contract.modes,['memory','kernel']);
  assert.deepEqual(contract.variants,['current','historical','restore-unchecked-close','wait-failed-treated-alive']);
  assert.equal(contract.profiles.length*contract.modes.length,4);assert.equal(contract.profiles.length*contract.modes.length*(contract.variants.length-1),12);
  assert.equal(contract.scenarioNames.length,13);assert.equal(contract.parentLifecycleAcceptance,'INCOMPLETE');
 });
 test('registration approves identity through a pinned parent manifest rather than observed main SHA',()=>{
  assert(path.isAbsolute(contract.currentMainManifest.path));assert.equal(digest(fs.readFileSync(contract.currentMainManifest.path)),contract.currentMainManifest.sha256);
  const approval=JSON.parse(fs.readFileSync(contract.currentMainManifest.path,'utf8'));assert.equal(approval.mainSha256,approval.sourceBindings['native/main.cpp']);
  const runner=fs.readFileSync(new URL('./consumer-runner.mjs',import.meta.url),'utf8');assert(runner.includes('SENSOR_ENSURE_PARENT_FINAL_FREEZE_REQUIRED'));assert(runner.includes('runtime_sourceClosure.json'));assert(runner.includes("workspace.allocate('sensor-ensure')"));
 });
 test('registration strict validator rejects partial or interim PASS before current identity can be inferred',()=>{
  assert.throws(()=>validateSensorEnsure({status:'INTERMEDIATE_TARGETED_PASS',pass:true},{}),/SENSOR_ENSURE_SCOPE/);
  assert.throws(()=>validateSensorEnsure({status:'PASS',pass:true,targetedAcceptance:true,sourceCurrent:true,sourceDrift:[],productionAcceptance:false,wholeLifecycleAcceptance:false,processExitUsedAsCleanup:false,parentLifecycleAcceptance:'INCOMPLETE'},{}),/SENSOR_ENSURE_EXPLICIT_APPROVED_IDENTITY_REQUIRED/);
 });
 test('registration retains parent persistent capability and INCOMPLETE boundary',()=>{
  assert.equal(normalizeLifecycleContract(lifecycleContract).features[0].persistence,'persistent');
  const adapter=getSignalEvidenceAdapters(REPO)[0];assert.equal(adapter.id,'controller.signal-evidence');assert.equal(adapter.timeoutMs,1800000);
  assert(adapter.evidenceInputs.includes('tools/input_capture_sensor_ensure_selftest.py'));assert(adapter.artifactInputs.includes(path.win32.resolve(contract.historicalMain.path)));assert(adapter.artifactInputs.includes(path.win32.resolve(contract.currentMainManifest.path)));
 });
}

if(registrationOnly){
 const pipe=consumerSchema.ownedCleanupComposition;
 test('composition registration matches pinned external frontDoorIntegration delivery',()=>{
  const {contract,manifest}=compositionContract(REPO);assert.deepEqual(contract.frontDoorIntegration,pipe.currentFrontDoorIntegration);
  assert.equal(contract.frontDoorIntegration.consumer,'tools/input_host_owned_cleanup_composition_selftest.py');assert(contract.frontDoorIntegration.arguments.includes('--require-wrapper'));
  assert.equal(contract.frontDoorIntegration.requiredStatus,'ACTUAL_WRAPPER_AND_OWNED_PREDICATES_COMPOSED_PASS');
  assert.equal(manifest.unstableAtCopy.length,0);assert(manifest.files.length>5000);
 });
 test('composition registration closes nine current source dependencies and keeps original tool gate',()=>{
  for(const name of pipe.frontDoorIntegration.currentSourceDependencies)assert(sourceInputs.includes(name));
  assert(sourceInputs.includes('tools/ymcc_sim/global/controller-signal-evidence/owned-cleanup-composition-receipts.mjs'));
  assert.equal(sourceInputs.length,585);assert.equal(getSignalEvidenceAdapters(REPO)[0].evidenceInputs.length,584);assert.equal(tools.files.length,731);
 });
 test('composition registration preserves two positive and eight named negative builds',()=>{
  assert.deepEqual(pipe.frontDoorIntegration.profiles,['Release','Checked']);
  assert.deepEqual(pipe.frontDoorIntegration.requiredVariants,['current','ignore-pipe-gate','pending-only-adapter','historical-failure-is-live-debt','unchecked-raw-slot-close']);
  assert.equal(Object.keys(pipe.frontDoorIntegration.strictNegativeLabels).length,4);assert.equal(pipe.parentLifecycleAcceptance,'INCOMPLETE');
 });
 test('composition runner requires wrapper and fresh short owner without rewriting external fixture',()=>{
  const runner=fs.readFileSync(new URL('./consumer-runner.mjs',import.meta.url),'utf8');
  assert(runner.includes("workspace.allocate('owned-cleanup-composition')"));assert(runner.includes("'--require-wrapper','--out',owned.buildOutput"));
  assert(runner.includes('PIPE_COMPOSITION_PARENT_FINAL_FREEZE_REQUIRED'));assert(runner.includes('composeRegistrationContext('));
  const {contract}=compositionContract(REPO);for(const row of contract.deliveredSources)assert(sourceInputs.includes(row.path));
 });
 test('composition capture view changes only TEMP/TMP to the existing short owned workspace',()=>{
  const source=Object.fromEntries(sourceInputs.map(name=>[name,createHash('sha256').update(fs.readFileSync(path.join(REPO,name))).digest('hex')]));
  const workspace=createOwnedWorkspace({repo:REPO,output:temporary,primary:tools.primary,sourceHashes:source}),owned=workspace.allocate('owned-cleanup-composition');
  const before=compositionContract(REPO),view=prepareCompositionToolchain({repo:REPO,owned});
  const derived=JSON.parse(fs.readFileSync(path.join(view.path,'build-environment.json'),'utf8'));
  const expected=structuredClone(before.capture);expected.environment.TEMP=owned.temp;expected.environment.TMP=owned.temp;
  assert.deepEqual(derived,expected);assert.equal(view.manifestSha256,pipe.toolchain.manifestSha256);assert(owned.temp.length<=110);
  workspace.verify(owned);compositionContract(REPO);
 });
 test('composition strict validator rejects a helper-only receipt before reading current identity',()=>{
  assert.throws(()=>validateOwnedCleanupComposition({status:'HELPER_COMPOSITION_PASS_PARENT_WRAPPER_NOT_INTEGRATED',pass:false},{repo:REPO}),/PIPE_COMPOSITION_ACTUAL_WRAPPER_REQUIRED/);
 });
 const delivery=JSON.parse(fs.readFileSync(pipe.delivery.path,'utf8'));
 const oldReceiptBytes=fs.readFileSync(delivery.result.path);
 assert.equal(createHash('sha256').update(oldReceiptBytes).digest('hex'),delivery.result.sha256,'PRESERVED_DELIVERY_CONTRACT_RESULT_PIN');
 const preserved=JSON.parse(oldReceiptBytes);
 // These are validator unit controls over preserved delivery bytes. They are
 // neither a new composition execution nor current-main acceptance.
 const sample=variant=>{
  const row=preserved.runs.find(row=>row.profile==='Release'&&row.variant===variant);
  const saved=delivery.runs.find(row=>row.profile==='Release'&&row.variant===variant).rowReceipt;
  const bytes=fs.readFileSync(row.result);assert.equal(createHash('sha256').update(bytes).digest('hex'),saved.sha256);
  return JSON.parse(bytes);
 };
 test('composition translation units reconstruct exact preserved whole predicates and specified mutants',()=>{
  const savedRoot=path.dirname(delivery.result.path),sourceRoot=path.join(savedRoot,'source');
  const text=file=>fs.readFileSync(path.join(sourceRoot,file),'utf8').replace(/^\uFEFF/,'');
  for(const row of preserved.sources)assert.equal(createHash('sha256').update(fs.readFileSync(path.join(sourceRoot,row.relative))).digest('hex'),row.sha256);
  const parts=Object.fromEntries(Object.entries(pipe.functions).map(([name,file])=>[name,cleanupFunction(text(file),name)]));
  const wrapper=parts.inputHostPipeRetirementProvenLocked.text;
  const predicates=[parts.inputHostOwnedCleanupProvenLocked.text,parts.inputHostDisabledCleanupProvenLocked.text].join('\n');
  const raw=text('native/main.cpp').match(/^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;/gm);assert.equal(raw.length,1);
  const leaf=text('tools/input_pipe_retirement_memory_selftest.cpp'),marker='using json=nlohmann::json;',prefix=leaf.slice(0,leaf.indexOf(marker));
  const template=text('tools/fixtures/input_host_owned_cleanup_composition/suite.cpp.in');
  for(const row of preserved.runs){let w=wrapper,pr=predicates;
   if(row.variant==='ignore-pipe-gate')pr=pr.replace('inputHostPipeRetirementProvenLocked()','true');
   if(row.variant==='pending-only-adapter')w=w.replace('return snapshot.available && snapshot.stopped;','return snapshot.available && snapshot.pending == 0;');
   if(row.variant==='historical-failure-is-live-debt')w=w.replace('return snapshot.available && snapshot.stopped;','return snapshot.available && snapshot.stopped && snapshot.closeFailures == 0;');
   if(row.variant==='unchecked-raw-slot-close')w=w.replace('if (!CloseHandle(g_inputHostUnadoptedPipe)) return false;','(void)CloseHandle(g_inputHostUnadoptedPipe);');
   let generated=template;for(const [tag,value] of [['@@PIPE_LEAVES@@',prefix],['@@RAW_SLOT@@',raw[0]],['@@WRAPPER@@',w],['@@PREDICATES@@',pr]])generated=generated.replace(tag,value);
   const target=path.join(savedRoot,row.profile+'-'+row.variant);
   assert.equal(createHash('sha256').update(generated).digest('hex'),createHash('sha256').update(fs.readFileSync(path.join(target,'fixture.cpp'))).digest('hex'),row.profile+'/'+row.variant);
   const command=JSON.parse(fs.readFileSync(path.join(target,'command.json'),'utf8'));assert.equal(command[0],preserved.toolchain.tools[0].path);
   assert(command.includes(row.profile==='Release'?'/O2':'/RTC1'));assert.equal(command.filter(arg=>arg.startsWith('/I')).length,3);
  }
 });
 test('composition runtime schema accepts preserved delivery controls without granting new-main acceptance',()=>{
  for(const variant of pipe.frontDoorIntegration.requiredVariants)validateCleanupRuntime(sample(variant),variant,pipe);
 });
 test('composition runtime schema rejects private HANDLE leak and process-exit cleanup claims',()=>{
  const value=sample('current');value.oracleLiveBeforeExit=1;assert.throws(()=>validateCleanupRuntime(value,'current',pipe),/PIPE_COMPOSITION_PREEXIT_PRIVATE_ZERO/);
  const count=sample('current');count.handlesAfter++;assert.throws(()=>validateCleanupRuntime(count,'current',pipe),/PIPE_COMPOSITION_PREEXIT_PRIVATE_ZERO/);
  const salvage=sample('current');salvage.negativeOnlyRawSalvage=2;assert.throws(()=>validateCleanupRuntime(salvage,'current',pipe),/PIPE_COMPOSITION_PREEXIT_PRIVATE_ZERO/);
 });
 test('composition runtime schema rejects missing original negative and raw-owner loss witness',()=>{
  const value=sample('pending-only-adapter');value.failures[0]='unspecified failure';value.failures=value.failures.map(row=>row===pipe.frontDoorIntegration.strictNegativeLabels['pending-only-adapter']?'unspecified failure':row);
  assert.throws(()=>validateCleanupRuntime(value,'pending-only-adapter',pipe),/PIPE_COMPOSITION_SPECIFIED_NEGATIVE_LABEL/);
  const raw=sample('unchecked-raw-slot-close');raw.cases.find(row=>row.case==='raw-slot-before-first-runtime').negativeOnlySalvage=0;assert.throws(()=>validateCleanupRuntime(raw,'unchecked-raw-slot-close',pipe),/PIPE_COMPOSITION_RAW_SLOT_LOST_OWNER_WITNESS/);
 });
 test('composition runtime schema rejects pending-only completion and available-on-contention claims',()=>{
  const value=sample('current');value.cases.find(row=>row.case==='actual-upper-predicates-active-cache-close-debt').active.stopped=true;
  assert.throws(()=>validateCleanupRuntime(value,'current',pipe),/PIPE_COMPOSITION_UPPER_PIPE_GATE_WITNESS/);
  const unknown=sample('current');unknown.cases.find(row=>row.case==='actual-wrapper-unavailable-snapshot').unknown.available=true;
  assert.throws(()=>validateCleanupRuntime(unknown,'current',pipe),/PIPE_COMPOSITION_CONTENDED_UNKNOWN/);
 });
}

if(registrationOnly){
 test('artifact identities deduplicate Windows absolute aliases and retain every origin',()=>{
  const hash='a'.repeat(64),rows=deduplicateArtifactRows([{path:'C:/Tools/one/../CL.exe',sha256:hash,origin:'first'},{path:'c:\\TOOLS\\cl.EXE',sha256:hash,origin:'second'},{path:'C:/captured/CL.exe',sha256:hash,origin:'captured'}]);
  assert.equal(rows.length,2);assert.deepEqual(rows[0].origins,['first','second']);assert.deepEqual(rows[1].origins,['captured']);
  assert.throws(()=>deduplicateArtifactRows([{path:'C:/Tools/cl.exe',sha256:hash,origin:'one'},{path:'c:/TOOLS/CL.EXE',sha256:'b'.repeat(64),origin:'two'}]),/CONTROLLER_ARTIFACT_PATH_HASH_CONFLICT/);
 });
 test('complete nested authority keeps all frozen files with top-level artifact count within unchanged global bound',()=>{
  const adapter=getSignalEvidenceAdapters(REPO)[0],rows=signalArtifactRows(REPO);
  assert.equal(MAX_ARTIFACT_INPUTS,3072);assert.equal(rows.length,1417);assert.equal(adapter.artifactInputs.length,1417);assert.equal(new Set(rows.map(row=>path.win32.resolve(row.path).toLowerCase())).size,1417);
  validateAdapters([adapter],REPO);
  const policy=consumerSchema.ownedCleanupComposition.nestedToolClosure,{manifest}=compositionContract(REPO);
  assert.equal(policy.expectedManifestFiles,5772);assert.equal(manifest.files.length,5772);assert.equal(policy.fullFileHashBeforeAndAfter,true);assert.equal(policy.sourceAndReceiptBound,true);assert.equal(policy.coverageReduced,false);assert.deepEqual(policy.phases,['before','after']);
 });
 test('nested closure rejects deletion or mutation of even a non-compiler-used frozen member',()=>{
  const folder=fs.mkdtempSync(path.join(temporary,'nested-files-')),one=path.join(folder,'used.h'),two=path.join(folder,'unused.lib');
  fs.writeFileSync(one,'header');fs.writeFileSync(two,'library');
  const rows=[one,two].map(file=>({frozen:file,sha256:createHash('sha256').update(fs.readFileSync(file)).digest('hex'),sizeBytes:fs.statSync(file).size}));
  assert.equal(verifyNestedToolFiles(rows).length,2);
  fs.writeFileSync(two,'mutated');assert.throws(()=>verifyNestedToolFiles(rows),/PIPE_COMPOSITION_NESTED_FILE_HASH/);
  fs.writeFileSync(two,'library');assert.equal(verifyNestedToolFiles(rows).length,2);
  fs.unlinkSync(two);assert.throws(()=>verifyNestedToolFiles(rows),/PIPE_COMPOSITION_NESTED_FILE_MISSING_OR_UNREADABLE/);
 });
 test('nested closure rejects missing before/after receipts without silently accepting observed hashes',()=>{
  const {contract,manifest}=compositionContract(REPO);
  assert.throws(()=>validateCompositionNestedClosure({nestedToolClosure:{}},{repo:REPO,owned:{},sourceHashes:{},manifest,contract}),/PIPE_COMPOSITION_NESTED_PHASES/);
 });
 test('registered runner hashes all manifest members on both sides of execution and binds nested receipts',()=>{
  const runner=fs.readFileSync(new URL('./consumer-runner.mjs',import.meta.url),'utf8');
  assert(runner.includes("phase:'before'"));assert(runner.includes("finally{nestedToolClosure.after=sealCompositionNestedClosure"));assert(runner.includes('toolchainView,nestedToolClosure}'));
  const validator=fs.readFileSync(new URL('./owned-cleanup-composition-receipts.mjs',import.meta.url),'utf8');
  assert(validator.includes('PIPE_COMPOSITION_NESTED_SOURCE_OWNER_BINDING'));assert(validator.includes('PIPE_COMPOSITION_NESTED_SNAPSHOT_PIN'));assert(validator.includes('PIPE_COMPOSITION_NESTED_EXECUTION_WINDOW'));assert(validator.includes('PIPE_COMPOSITION_ORIGINAL_RECEIPT_BYTES'));
 });
}

if(registrationOnly){
 const pipe=consumerSchema.ownedCleanupComposition;
 const {contract:currentPipe}=compositionContract(REPO),delivery=JSON.parse(fs.readFileSync(currentPipe.delivery.path,'utf8'));
 const currentReceipt=JSON.parse(fs.readFileSync(delivery.basisResult.path,'utf8'));
 const captured=path.join(currentReceipt.out,'source'),parts=Object.fromEntries(Object.entries(currentPipe.functions).map(([name,file])=>[name,cleanupFunction(fs.readFileSync(path.join(captured,file),'utf8').replace(/^\uFEFF/,''),name)]));
 test('current composition version keeps historical delivery and all original assertions/negative contracts',()=>{
  assert.equal(pipe.delivery.sha256,'ea97b1967967f8230c40ea4af205517458fcdc6e3fea4b0ade727a8f5fc74725');
  assert.deepEqual(delivery.historicalDelivery,pipe.delivery);assert.equal(currentPipe.frontDoorIntegration.currentSourceDependencies.length,10);
  assert.deepEqual(currentPipe.frontDoorIntegration.requiredVariants,pipe.frontDoorIntegration.requiredVariants);assert.deepEqual(currentPipe.frontDoorIntegration.strictNegativeLabels,pipe.frontDoorIntegration.strictNegativeLabels);
  for(const row of currentReceipt.runs)validateCleanupRuntime(JSON.parse(fs.readFileSync(row.result,'utf8')),row.variant,pipe);
  assert.equal(currentReceipt.runs.length,10);assert.equal(currentPipe.primaryThreadScope.newPrimaryAcceptance,false);
 });
 test('current composition compiles all four exact functions including primary and rejects missing/tampered primary',()=>{
  validateCompositionFunctions(currentReceipt,{parts,contract:currentPipe});
  const missing=structuredClone(currentReceipt);delete missing.functions.inputHostPrimaryThreadRetirementProvenLocked;assert.throws(()=>validateCompositionFunctions(missing,{parts,contract:currentPipe}),/PIPE_COMPOSITION_FUNCTION_SET/);
  const corrupt=structuredClone(currentReceipt);corrupt.functions.inputHostPrimaryThreadRetirementProvenLocked.sha256='0'.repeat(64);assert.throws(()=>validateCompositionFunctions(corrupt,{parts,contract:currentPipe}),/PIPE_COMPOSITION_CURRENT_FUNCTION_HASH/);
  const overclaim=structuredClone(currentReceipt);overclaim.primaryThreadScope.newPrimaryAcceptance=true;assert.throws(()=>validateCompositionFunctions(overclaim,{parts,contract:currentPipe}),/PIPE_COMPOSITION_PRIMARY_NULL_BOUNDARY/);
 });
 test('current composition reconstructs all ten exact generated units with actual primary globals/helper',()=>{
  const text=file=>fs.readFileSync(path.join(captured,file),'utf8').replace(/^\uFEFF/,'');const main=text('native/main.cpp');
  const globals=['g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch'].map(name=>main.match(new RegExp('^static[^\\n;{}]*\\b'+name+'\\b[^\\n;]*;','m'))[0]).join('\n');
  const raw=main.match(/^static HANDLE g_inputHostUnadoptedPipe\s*=\s*nullptr\s*;/m)[0],leaf=text('tools/input_pipe_retirement_memory_selftest.cpp'),prefix=leaf.slice(0,leaf.indexOf('using json=nlohmann::json;')),template=text('tools/fixtures/input_host_owned_cleanup_composition/suite.cpp.in');
  for(const row of currentReceipt.runs){let w=parts.inputHostPipeRetirementProvenLocked.text,pr=[parts.inputHostOwnedCleanupProvenLocked.text,parts.inputHostDisabledCleanupProvenLocked.text].join('\n');
   if(row.variant==='ignore-pipe-gate')pr=pr.replace('inputHostPipeRetirementProvenLocked()','true');
   if(row.variant==='pending-only-adapter')w=w.replace('return snapshot.available && snapshot.stopped;','return snapshot.available && snapshot.pending == 0;');
   if(row.variant==='historical-failure-is-live-debt')w=w.replace('return snapshot.available && snapshot.stopped;','return snapshot.available && snapshot.stopped && snapshot.closeFailures == 0;');
   if(row.variant==='unchecked-raw-slot-close')w=w.replace('if (!CloseHandle(g_inputHostUnadoptedPipe)) return false;','(void)CloseHandle(g_inputHostUnadoptedPipe);');
   let cpp=template;for(const [tag,value] of [['@@PIPE_LEAVES@@',prefix],['@@HOST_GLOBALS@@',globals],['@@PRIMARY_HELPER@@',parts.inputHostPrimaryThreadRetirementProvenLocked.text],['@@RAW_SLOT@@',raw],['@@WRAPPER@@',w],['@@PREDICATES@@',pr]])cpp=cpp.replace(tag,value);
   const actual=fs.readFileSync(path.join(path.dirname(row.result),'fixture.cpp'));assert.equal(createHash('sha256').update(cpp).digest('hex'),createHash('sha256').update(actual).digest('hex'));
  }
 });
 const reg=consumerSchema.exitFenceCurrent,oldFence=JSON.parse(fs.readFileSync(reg.preservedConsumerReceipt.path,'utf8'));
 const fenceSnapshot=path.join(oldFence.run,'source');
 const source=(rows,label)=>{const seen=new Set();for(const [name,pin] of Object.entries(rows)){assert(!seen.has(name));assert.equal(createHash('sha256').update(fs.readFileSync(path.join(fenceSnapshot,name))).digest('hex'),pin,label+':snapshot');seen.add(name);}return seen;};
 const tool=(actual,expected,pin,label)=>{assert.equal(path.resolve(actual).toLowerCase(),path.resolve(expected).toLowerCase(),label);const row=tools.files.find(row=>path.resolve(row.path).toLowerCase()===path.resolve(expected).toLowerCase());assert(row);const actualHash=createHash('sha256').update(fs.readFileSync(actual)).digest('hex');assert.equal(actualHash,row.sha256);if(pin!==undefined)assert.equal(pin,actualHash);};
 const context={repo:fenceSnapshot,registration:reg,current:currentFragments(fenceSnapshot),source,tool,tools,sdk:(value)=>assert.equal(value.replace(/[\\/]$/,''),tools.sdkVersion)};
 test('current fence validates six primary and six preserved control rows from pinned delivery snapshot',()=>{
  assert.equal(createHash('sha256').update(fs.readFileSync(reg.preservedConsumerReceipt.path)).digest('hex'),reg.preservedConsumerReceipt.sha256);
  const result=validateExitFence(oldFence,context);assert.equal(result.checksPerRoute,148);assert.equal(result.explicitlyJoinedThreads,108);assert.equal(result.wholeLifecycleAcceptance,false);
 });
 test('fence whole-function identity admits CRLF/LF only and rejects new statements or restored clear',()=>{
  const contract=JSON.parse(fs.readFileSync(path.join(REPO,reg.sourceContract),'utf8')),scope=JSON.parse(fs.readFileSync(path.join(oldFence.run,'SOURCE-SCOPE-START.json'),'utf8')),functions=scope.completeFunctions;
  const crlf=Object.fromEntries(Object.entries(functions).map(([name,text])=>[name,text.replace(/\r\n/g,'\n').replace(/\n/g,'\r\n')]));assert.deepEqual(verifyFenceFunctionIdentity(functions,contract),verifyFenceFunctionIdentity(crlf,contract));
  const bad=structuredClone(functions);bad.inputHostResetExitFence=bad.inputHostResetExitFence.replace('    const bool previousRaised','    throw 1;\n    const bool previousRaised');assert.throws(()=>verifyFenceFunctionIdentity(bad,contract),/FENCE_UNREVIEWED_COMPLETE_FUNCTIONS/);
  const clear=structuredClone(functions);clear.inputHostResetExitFence=clear.inputHostResetExitFence.replace('g_inputHostExitFence.load(std::memory_order_acquire)','g_inputHostExitFence.exchange(false, std::memory_order_acq_rel)');assert.throws(()=>verifyFenceFunctionIdentity(clear,contract),/FENCE_UNREVIEWED_COMPLETE_FUNCTIONS/);
 });
 for(const [name,change,error] of [
  ['lost historical clear negative',r=>{r.profiles.find(row=>row.route==='legacy').exitCode=0;},/FENCE_CLEAR_DEFECT_NOT_REJECTED/],
  ['lost restore-clear negative',r=>{r.profiles.find(row=>row.route==='negative-restore-clear').exitCode=0;},/FENCE_CLEAR_DEFECT_NOT_REJECTED/],
  ['unjoined current worker',r=>{r.profiles.find(row=>row.route==='monotonic').threadsJoined=0;},/FENCE_THREADS_OR_BOUNDARY_UNPROVEN/],
  ['startup dependency omitted',r=>{delete r.sourceHashes['native/input_startup_policy.h'];},/FENCE_ACTUAL_DEPENDENCY_PINS/],
  ['startup dependency identity changed',r=>{r.dependencyHashes['native/input_startup_policy.h']='0'.repeat(64);},/FENCE_ACTUAL_DEPENDENCY_PINS/],
  ['source contract negative erased',r=>{r.sourceGateControls.find(row=>row.accepted===false).accepted=true;},/FENCE_SOURCE_CONTRACT_NEGATIVE/],
  ['real startup policy cases erased',r=>{r.profiles[0].dependencyCases=[];},/FENCE_REAL_STARTUP_POLICY_EXECUTION/],
  ['preserved candidate controls omitted',r=>{r.controlProfiles.pop();},/FENCE_PRESERVED_CONTROL_ROUTES/],
 ])test('current fence registration validator rejects '+name,()=>{const copy=structuredClone(oldFence);change(copy);assert.throws(()=>validateExitFence(copy,context),error);});
 test('Sagan complete CLI contract evidence retains all six actual rejected runs and reviewed extension',()=>{
  const bytes=fs.readFileSync(reg.cliControls.path);assert.equal(createHash('sha256').update(bytes).digest('hex'),reg.cliControls.sha256);
  const cli=JSON.parse(bytes);const rows=cli.results??cli.runs??cli.cases;assert(Array.isArray(rows));const rejected=rows.filter(row=>row.expectedFailureCaught);assert.equal(rejected.length,6);assert(rejected.every(row=>row.exitCode===1&&row.complete===true&&typeof row.error==='string'));assert(rows.some(row=>row.name==='reviewed-source-extension'&&row.exitCode===0&&row.complete===true));
 });
}
