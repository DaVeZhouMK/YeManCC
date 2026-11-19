/** Read back two independently approved Vue leaves; never invoke a producer. */
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {readBudgetWorker, runWithWallBudget, validateWallBudget} from './execution-budget.mjs';
import {FIXED_NODE, MODULE, ORIGINAL_ENTRIES, REAL_RENDERER_ID} from '../global/monitor-notice-lifecycle/real-renderer-adapter.mjs';
import {validateRealRendererResult} from '../global/monitor-notice-lifecycle/real-renderer-global.mjs';

const SELF = fileURLToPath(import.meta.url);
const REPO = path.resolve(import.meta.dirname, '../../..');
const CORE = path.resolve(REPO, '../../../Testing/YMCC');
const EVIDENCE = path.join(CORE, 'Evidence');
const WORKSPACE = path.join(CORE, 'Workspace');
const SCOPE = 'ORIGINAL_FROZEN_VUE_LEAVES_ONLY';
const MAX_FILE = 16 * 1024 * 1024;
const digest = data => createHash('sha256').update(data).digest('hex');
const shaPattern = /^[a-f0-9]{64}$/;
const key = file => path.resolve(file).toLowerCase();
const need = (condition, message) => assert(condition, 'VUE_REFERENCE_' + message);
const absolute = file => typeof file === 'string' && /^[a-z]:[\\/]/i.test(file) &&
  file.slice(3).split(/[\\/]/).every(part => part && !/[. ]$/.test(part) && !/[:\x00-\x1f]/.test(part));
const samePath = (a, b) => absolute(a) && absolute(b) && key(a) === key(b);
const below = (root, file) => absolute(file) && key(file).startsWith(key(root) + path.sep);
const relative = file => typeof file === 'string' && !file.includes(':') &&
  file.split(/[\\/]/).every(part => part && !/[. ]$/.test(part) && !/[\x00-\x1f]/.test(part));
const samePin = (a, b) => a && b && samePath(a.path, b.path) && shaPattern.test(a.sha256) &&
  a.sha256 === b.sha256 && (a.bytes === undefined || b.bytes === undefined || a.bytes === b.bytes);
const parse = data => JSON.parse(data.toString('utf8').replace(/^\uFEFF/, ''));
function readFile(file) {
  need(absolute(file) && samePath(fs.realpathSync.native(file), file), 'REDIRECTED_FILE:' + file);
  const stat = fs.lstatSync(file);
  need(stat.isFile() && !stat.isSymbolicLink() && stat.size <= MAX_FILE, 'BOUNDED_FILE:' + file);
  const raw = fs.readFileSync(file);
  need(raw.length === stat.size, 'READ_LENGTH:' + file);
  return {raw, pin:{path:path.resolve(file),bytes:raw.length,sha256:digest(raw)}};
}
function checked(pin, expectedPath = pin?.path) {
  need(pin && samePath(pin.path, expectedPath) && shaPattern.test(pin.sha256), 'PIN_PATH:' + expectedPath);
  const file = readFile(expectedPath);
  need(samePin(file.pin, pin), 'PIN_BYTES_OR_SHA:' + expectedPath);
  return file;
}
const json = (pin, expected) => parse(checked(pin, expected).raw);
function under(pin, root) {
  need(pin && below(root, pin.path), 'ROLE_PATH_ESCAPE');
  return pin.path;
}
function verifyBoundInputs(terminal, request) {
  const declared = [...request.sources.map(pin => ({role:'sources', ...pin})),
    ...request.tools.map(pin => ({role:'tools', ...pin}))];
  need(declared.length === terminal.declaredInputsBefore.length && declared.length === terminal.declaredInputsAfter.length,
    'RAWPROGRAM_DECLARED_INPUT_COUNT');
  const before = new Map(terminal.declaredInputsBefore.map(pin => [key(pin.path), pin]));
  const after = new Map(terminal.declaredInputsAfter.map(pin => [key(pin.path), pin]));
  need(before.size === declared.length && after.size === declared.length &&
    new Set(declared.map(pin => key(pin.path))).size === declared.length, 'RAWPROGRAM_DUPLICATE_INPUT');
  for (const pin of declared) {
    need(samePin(pin,before.get(key(pin.path))) && samePin(pin,after.get(key(pin.path))) &&
      pin.role === before.get(key(pin.path)).role, 'RAWPROGRAM_DECLARED_INPUT_PIN:' + pin.path);
  }
  return before;
}

// Exported separately for in-memory rejection controls; no mutable reader injection.
export function validateRendererReferenceTerminal(terminal) {
  const job = terminal.job;
  need(terminal.kind === 'ymcc-raw-program-terminal-v1' && terminal.status === 'EXITED' && terminal.reason === 'NATURAL' &&
    terminal.collectionComplete === true && terminal.businessSucceeded === true && terminal.businessExitCode === 0 &&
    terminal.timedOut === false && terminal.cancelled === false && terminal.ownerLost === false &&
    terminal.residualDescendants === false && terminal.logsClosedAndFlushed === true && terminal.helperExitCode === 0 &&
    terminal.error === null && terminal.declaredPinsUnchanged === true && terminal.outputLimitExceeded === false &&
    terminal.globalAcceptanceGranted === false && terminal.owner?.handleBound === true &&
    terminal.business?.atomicJobAdmission === true && terminal.declaredToolCleanup?.requested === false,
  'RAWPROGRAM_TERMINAL');
  need(job?.privateUnnamed === true && job.zeroObservedBeforeDispose === true && job.activeProcessesFinal === 0 &&
    job.disposed === true && job.terminateRequested === false && job.terminateSucceeded === false && job.cleanupError === null,
  'RAWPROGRAM_NATURAL_JOB_ZERO');
}

function verifyArm(row, index, root, reviewPair, observation) {
  const negative = index === 1;
  const arm = negative ? 'negative' : 'positive';
  const rawStatus = negative ? 'FAIL' : 'PASS';
  const workerExitCode = negative ? 2 : 0;
  const entry = ORIGINAL_ENTRIES[index];
  need(row.arm === arm && row.rawStatus === rawStatus && row.scopePassed === true &&
    observation.stage === arm && observation.case === entry, 'ORIGINAL_ARM_ORDER');
  const dir = path.join(root, 'vue-run-' + arm);
  const rawDir = path.join(dir, 'raw');
  const approvalPath = path.join(root, 'vue-split-reviews', arm, 'PARENT-REVIEW.json');
  need(samePin(row.review, reviewPair), 'APPROVAL_ARM_BINDING');
  const approval = json(row.review, approvalPath);
  const frozen = approval.frozenRoot;
  const prepared = approval.builds?.[0]?.preparedOut;
  need(approval.approved === true && approval.case === entry && approval.builds?.length === 1 &&
    below(path.join(WORKSPACE,'Mr'), frozen) && below(path.join(WORKSPACE,'Rd'), prepared) &&
    samePath(frozen,observation.root) && samePath(prepared,observation.out), 'FROZEN_LEAF_BOUNDARY');
  const invocation = json(row.invocation, path.join(dir,'INVOCATION.json'));
  need(invocation.mode === arm && invocation.scopePassed === true && invocation.guardExitCode === 0 &&
    invocation.compiledThisRun === false && invocation.combinedGlobalAdapterRun === false &&
    invocation.durationMs === row.milliseconds, 'INVOCATION_ARM_BINDING');
  const runtimeOptions = {negative,runtimeRoot:approval.runtimeRoot,runtimeVersion:approval.runtimeVersion};
  const raw = json(invocation.runtime,path.join(prepared,'real-webview-result.json'));
  const launcher = json(invocation.launcher,path.join(prepared,'real-webview-private-launch.json'));
  const originalValidation = validateRealRendererResult(raw,launcher,runtimeOptions);
  assert.deepEqual(originalValidation,invocation.validation,'VUE_REFERENCE_ORIGINAL_VALIDATION');
  need(originalValidation.checks === row.caseChecks && originalValidation.workerExitCode === workerExitCode &&
    launcher.totalProcessesInJob === row.jobProcesses && launcher.activeProcessesBeforeJobClose === row.activeBeforeClose,
  'ORIGINAL_RESULT_BINDING');
  const tag = negative ? 'REAL-RENDERER-NEGATIVE' : 'REAL-RENDERER';
  const leaf = json(invocation.receipt,path.join(prepared,tag+'-EXECUTE-PROCESS.json'));
  need(leaf.status === (negative ? 'EXACT_PRODUCT_RENDERER_FRAME_ADMISSION_NEGATIVE_FAILS_AT_ACTUAL_VUE_PRESENTATION' :
    'ACTUAL_REAL_RENDERER_NINE_CONTROLLERS_RETIRED_LATE_CALLBACKS_AND_NATURAL_BROWSER_EXIT_VERIFIED'), 'EXECUTE_LEAF_STATUS');
  assert.deepEqual(leaf.actual,raw,'VUE_REFERENCE_EXECUTE_RAW');
  assert.deepEqual(leaf.launch,launcher,'VUE_REFERENCE_EXECUTE_LAUNCHER');
  assert.deepEqual(leaf.scopeReceipt.sourceHashes,approval.sourcePins,'VUE_REFERENCE_EXECUTE_SOURCE_PINS');
  need(samePath(leaf.out,prepared) && samePath(leaf.runtimeRoot,approval.runtimeRoot) &&
    leaf.runtimeVersion === approval.runtimeVersion && samePin(leaf.result,invocation.runtime) &&
    samePin(leaf.launchReceipt,invocation.launcher) && leaf.toolProof.nodeGraphSha256 === approval.nodeGraphSha256 &&
    (!negative || (leaf.execution?.code === 2 && leaf.execution.signal === null && leaf.execution.killed === false)), 'EXECUTE_LEAF_BINDING');

  const terminal = json(invocation.terminal,path.join(rawDir,'terminal.json'));
  validateRendererReferenceTerminal(terminal);
  const request = json(terminal.request,path.join(rawDir,'request.json'));
  const argv = json(terminal.argumentsPin,path.join(rawDir,'arguments.json'));
  const business = terminal.business;
  const expectedArgs = [path.resolve(frozen,entry),'--out',path.resolve(prepared),'--runtime',path.resolve(approval.runtimeRoot),'--runtime-version',approval.runtimeVersion];
  need(Array.isArray(argv) && argv.length === expectedArgs.length && argv.every((arg,i) =>
    [0,2,4].includes(i) ? samePath(arg,expectedArgs[i]) : arg === expectedArgs[i]), 'EXACT_ORIGINAL_ARGV');
  assert.deepEqual(business.arguments,argv,'VUE_REFERENCE_ACTUAL_ARGV');
  assert.deepEqual(business.environment,request.environment,'VUE_REFERENCE_ACTUAL_ENVIRONMENT');
  need(request.kind === 'ymcc-raw-program-v1' && samePin(request.arguments,terminal.argumentsPin) &&
    samePath(business.program.path,FIXED_NODE) && samePath(request.cwd,frozen) && samePath(business.cwd,frozen) &&
    samePath(business.environment.YMCC_RADIAL_EXECUTION_REVIEW,approvalPath) &&
    business.environment.YMCC_RADIAL_EXECUTION_REVIEW_SHA256 === row.review.sha256, 'ACTUAL_REVIEW_BINDING');
  checked(terminal.logs.stdout,path.join(rawDir,'stdout.raw'));
  checked(terminal.logs.stderr,path.join(rawDir,'stderr.raw'));
  const declared = verifyBoundInputs(terminal,request);
  const declaredPin = (pin,name) => need(samePin(pin,declared.get(key(pin.path))), 'OUTER_PIN_' + name);
  const innerPin = pin => need(Object.entries(leaf.scopeReceipt.sourceHashes).some(([file,sha256]) =>
    samePath(file,pin.path) && sha256 === pin.sha256), 'INNER_EXECUTE_PIN:' + pin.path);
  declaredPin(row.review,'APPROVAL');
  declaredPin(business.program,'ORIGINAL_NODE');

  for (const [name,pin,observed] of [
    ['PREPARED',approval.preparedLeaf,observation.leaf],['EXECUTABLE',approval.actualExecutable,observation.exe],
    ['NATIVE_GRAPH',approval.actualNativeBuildGraph,observation.graph],['BUNDLE_META',approval.actualRendererMetafile,observation.bundleMeta],
    ['RUNTIME_MANIFEST',approval.runtimeManifest,observation.runtimeManifest],
  ]) {
    need(samePin(pin,observed), 'INDEPENDENT_BUILD_BINDING:' + name);
    if (name === 'BUNDLE_META') innerPin(pin);
    else declaredPin(pin,name);
  }
  const prepare = json(approval.preparedLeaf,path.join(prepared,tag+'-PREPARE-PROCESS.json'));
  assert.deepEqual(prepare.scopeReceipt.sourceHashes,approval.sourcePins,'VUE_REFERENCE_PREPARE_SOURCE_PINS');
  checked(approval.actualExecutable,under(approval.actualExecutable,prepared));
  need(samePath(prepare.out,prepared) && samePin(prepare.exe,approval.actualExecutable) && samePin(prepare.runtimeManifest,approval.runtimeManifest) &&
    approval.builds[0].executableSha256 === approval.actualExecutable.sha256, 'PREPARED_BUILD_IDENTITY');
  const graph = json(approval.actualNativeBuildGraph,under(approval.actualNativeBuildGraph,prepared));
  need(graph.pass === true && samePath(graph.executable,approval.actualExecutable.path) &&
    graph.executableSha256 === approval.actualExecutable.sha256 && graph.nativeInputs.length === observation.nativeInputs &&
    graph.linkedLibraries.length === observation.linkedLibraries, 'NATIVE_BUILD_GRAPH');
  const metafile = json(approval.actualRendererMetafile,under(approval.actualRendererMetafile,prepared));
  assert.deepEqual(Object.keys(metafile.inputs),observation.bundleInputs,'VUE_REFERENCE_ACTUAL_BUNDLE_INPUTS');
  need(approval.actualBundleFiles.length === observation.bundle.length, 'BUNDLE_FILE_COUNT');
  for (const pin of approval.actualBundleFiles) {
    need(observation.bundle.some(other => samePin(pin,other)), 'BUNDLE_ORIGINAL_ROLE');
    innerPin(pin);
    checked(pin,under(pin,prepared));
  }
  const runtime = json(approval.runtimeManifest,under(approval.runtimeManifest,prepared));
  need(runtime.membership === 'complete-recursive-files-no-reparse' && runtime.runtimeDownloaded === false &&
    samePath(runtime.runtimeRoot,approval.runtimeRoot) && runtime.runtimeVersion === approval.runtimeVersion &&
    runtime.runtimeFiles.length > 0, 'ORIGINAL_RUNTIME_MANIFEST');
  assert.deepEqual(runtime.runtimeFiles,approval.runtimeFiles,'VUE_REFERENCE_REVIEWED_RUNTIME_MEMBERS');
  need(runtime.runtimeFiles.every(member => relative(member.path) && shaPattern.test(member.sha256)), 'ORIGINAL_RUNTIME_MEMBER');
  const browser = runtime.runtimeFiles.find(member => member.path === 'msedgewebview2.exe');
  need(browser,'ORIGINAL_RUNTIME_BROWSER');
  declaredPin({path:path.resolve(runtime.runtimeRoot,browser.path),sha256:browser.sha256},'RUNTIME_BROWSER');

  // Compare checked-in inputs actually consumed by this leaf, not a whole-repo or current tool claim.
  const frozenSet = new Map();
  const addFrozen = (file, sha256) => {
    need(below(frozen,file) && shaPattern.test(sha256), 'FROZEN_SOURCE_PATH');
    const prior = frozenSet.get(key(file));
    need(!prior || prior.sha256 === sha256, 'FROZEN_SOURCE_CONFLICT');
    frozenSet.set(key(file),{path:file,sha256});
  };
  for (const [file,sha256] of Object.entries(approval.sourcePins)) {
    if (absolute(file)) need(below(prepared,file),'PREPARED_SOURCE_ROLE');
    else {
      need(relative(file),'SOURCE_RELATIVE_PATH');
      addFrozen(path.resolve(frozen,file),sha256);
    }
  }
  for (const pin of graph.nativeInputs) if (below(frozen,pin.path)) addFrozen(pin.path,pin.sha256);
  for (const file of Object.keys(metafile.inputs)) if (relative(file)) need(frozenSet.has(key(path.resolve(frozen,file))),'UNBOUND_BUNDLE_INPUT');
  const sourceDiff = [];
  for (const pin of frozenSet.values()) {
    checked(pin);
    const rel = path.relative(frozen,pin.path);
    let current;
    try { current = readFile(path.join(REPO,rel)).pin.sha256; } catch { current = null; }
    if (current !== pin.sha256) sourceDiff.push({relative:rel,frozen:pin.sha256,current});
  }
  return {arm,originalValidation,workerExitCode,guardExitCode:terminal.businessExitCode,originalMilliseconds:row.milliseconds,
    frozenRoot:frozen,preparedOut:prepared,comparedFrozenLocalSourceCount:frozenSet.size,currentSourceDifferences:sourceDiff,
    invocation:row.invocation,approval:row.review,terminal:invocation.terminal,raw:invocation.runtime,launcher:invocation.launcher,
    buildGraph:approval.actualNativeBuildGraph,runtimeManifest:approval.runtimeManifest,runtimeVersion:runtime.runtimeVersion,
    outerNaturalJobZero:true,innerNaturalJobZero:true};
}

export function verifySplitRendererReference(intake) {
  need(below(EVIDENCE,intake.path) && path.basename(intake.path) === 'CLOSING-VUE-SPLIT.json','EXPLICIT_SPLIT_CLOSING');
  const input = checked(intake);
  const closing = parse(input.raw);
  const root = path.dirname(input.pin.path);
  need(closing.status === 'ACTUAL_VUE_POSITIVE_AND_EXPECTED_NEGATIVE_SCOPED_LEAVES_PASSED' &&
    closing.receipts?.length === 2 && closing.combinedGlobalAdapter === 'NOT_RUN' && closing.fullParent === 'NOT_RUN', 'SPLIT_SCOPE');
  const review = json(closing.review,path.join(root,'vue-split-reviews','FINAL-READBACK.json'));
  const readback = json(review.sourceAndActualReadback,path.join(root,'vue-split-reviews','ACTUAL-SPLIT-EVIDENCE-READBACK.json'));
  need(review.combinedPreparedWritten === false && review.approvedLaunchOnly === true && review.approvals?.length === 2 &&
    readback.leafObservations?.length === 2 && readback.mismatches?.length === 0,'INDEPENDENT_SPLIT_REVIEW');
  const arms = closing.receipts.map((row,index) => verifyArm(row,index,root,review.approvals[index],readback.leafObservations[index]));
  need(!samePath(arms[0].frozenRoot,arms[1].frozenRoot) && !samePath(arms[0].preparedOut,arms[1].preparedOut), 'MIXED_APPROVAL_ROOTS');
  return {id:REAL_RENDERER_ID,status:'FROZEN_ORIGINAL_REFERENCE',scope:SCOPE,originalIntake:input.pin,arms,
    executedThisRun:0,actualPassedThisRun:0,nativeProcessesStartedThisRun:0,compilerInvocationsThisRun:0,
    genericCacheQualified:false,currentMainlineAccepted:false,liveRuntimeRehashed:false,fullParentAcceptance:false,
    combinedPrepared:false,originalAggregatePrepareFailure:closing.retainedUnsuccessfulAggregatePrepare};
}

function options(args) {
  need(!process.env.NODE_OPTIONS && !process.env.NODE_PATH && process.execArgv.length === 0 && samePath(process.execPath,FIXED_NODE),'PINNED_NODE');
  need(args.length === 10 && args[0] === '--test' && args[1] === REAL_RENDERER_ID &&
    args[2] === '--intake' && args[4] === '--intake-sha256' && args[6] === '--out-dir' && args[8] === '--budget-ms', 'EXPLICIT_REFERENCE_ARGUMENTS');
  need(below(EVIDENCE,args[3]) && below(EVIDENCE,args[7]) && shaPattern.test(args[5]) && /^\d+$/.test(args[9]),'PATH_SHA_OR_BUDGET');
  let parent = path.resolve(args[7]);
  while (!fs.existsSync(parent)) parent = path.dirname(parent);
  need(samePath(fs.realpathSync.native(parent),parent),'REDIRECTED_OUTPUT');
  return {intake:{path:args[3],sha256:args[5]},outDir:args[7],budgetMs:validateWallBudget(Number(args[9]))};
}
function publish(file,value) { fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx',flush:true}); }
function worker(args) {
  const context = readBudgetWorker(args[1],args[2],{coreRoot:CORE,repoRoot:REPO});
  const opts = options(context.args);
  need(opts.budgetMs === context.budgetMs && context.remainingMs() > 0,'CURRENT_WORKER_BUDGET');
  let reference, reason;
  try {
    reference = verifySplitRendererReference(opts.intake);
    need(context.remainingMs() > 0,'CURRENT_WORKER_BUDGET');
  } catch (error) { reason = error.message; reference = null; }
  // The shared budget envelope accounts for a reference; the public row is not a generic cache hit.
  publish(path.join(context.runDir,'result.json'),{schemaVersion:1,runDir:context.runDir,
    status:reference ? 'COMPLETE_WITH_CACHED_REFERENCES' : 'NOT_RUN_NO_VALID_REFERENCE',
    summary:{selected:1,executedThisRun:0,actualPassedThisRun:0,cachedReferences:reference ? 1 : 0},scope:SCOPE,reference,reason});
  return reference ? 0 : 10;
}
async function main(args) {
  const opts = options(args);
  const verifyInputs = ['real-renderer-global.mjs','real-renderer-adapter.mjs','real-browser-ownership.mjs'].map(name => path.join(REPO,MODULE,name));
  const {result,exitCode} = await runWithWallBudget(args,{repoRoot:REPO,coreRoot:CORE,workerEntry:SELF,
    additionalInputs:[path.join(CORE,'Run.ps1'),...verifyInputs],budgetMs:opts.budgetMs,outDir:opts.outDir});
  const ready = exitCode === 0 && result.status === 'COMPLETE_WITH_CACHED_REFERENCES' && result.resourceZeroVerified === true &&
    result.ownedJob?.terminationRequested === false && result.sourceIdentity.status === 'MATCH';
  const status = ready ? 'FROZEN_ORIGINAL_REFERENCE' : 'NOT_RUN_NO_VALID_REFERENCE';
  const frontdoor = {schemaVersion:1,status,scope:SCOPE,id:REAL_RENDERER_ID,executedThisRun:0,actualPassedThisRun:0,
    nativeProcessesStartedThisRun:0,compilerInvocationsThisRun:0,genericCacheQualified:false,currentMainlineAccepted:false,
    fullParentAcceptance:false,runDir:result.runDir,wallMs:result.wallMs,currentWorkerNaturalJobZero:result.resourceZeroVerified &&
      result.ownedJob?.terminationRequested === false,executionBudget:path.join(result.runDir,'execution-budget.json'),
    workerResult:path.join(result.runDir,'result.json')};
  publish(path.join(result.runDir,'monitor-renderer-reference.json'),frontdoor);
  console.log(JSON.stringify(frontdoor));
  return ready ? 0 : (exitCode || 10);
}
if (process.argv[1] && samePath(process.argv[1],SELF)) {
  const args = process.argv.slice(2);
  try { process.exitCode = args[0] === '--budget-worker' && args.length === 3 ? worker(args) : await main(args); }
  catch (error) { console.error('NOT_RUN_NO_VALID_REFERENCE ' + error.message); process.exitCode = 10; }
}
