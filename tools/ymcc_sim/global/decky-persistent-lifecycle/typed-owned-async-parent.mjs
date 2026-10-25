/** Read-only parent consumer of completed, independently pinned AsyncRequest runs.
 * verified means evidence integrity, never PASS. No compiler/native process is run.
 * sourceMode=frozen seals historical FAILs; it can never authorize acceptance.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {reconstructTypedAsyncOriginalContract} from './typed-async-parent.mjs';
import {inspectOwnedAsyncNative} from './typed-owned-async-native.mjs';

const MODULE = 'tools/ymcc_sim/global/decky-persistent-lifecycle';
const LEGACY = 'tools/decky_binding_cancel_selftest.cpp';
const DRIVER = MODULE + '/typed-owned-async-request-regression';
const V1_DRIVER = MODULE + '/typed-legacy-async-request-regression';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const digest = file => hash(fs.readFileSync(file));
const key = file => path.resolve(file).toLowerCase();
const inside = (root, file) => key(file).startsWith(key(root) + path.sep);
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const integer = (value, label, minimum = 0) => assert(Number.isSafeInteger(value) && value >= minimum, label);
const bool = (value, label) => assert.equal(typeof value, 'boolean', label);
const samePath = (actual, expected, label) => assert.equal(key(actual), key(expected), label);
const fail = label => 'TYPED_ASYNC_PARENT_' + label;
export const TYPED_ASYNC_ORDERED_IDS = Object.freeze([
  'inert child did not start', 'owned child handle unavailable', 'child identity mismatch',
  'production request did not reach silent fixture', 'elapsed < 1500', 'returned',
  'failure == mirror-bootstrap-canceled', 'childExited', 'closed()',
  '!runtime.snapshot().enabled', 'runtime.snapshot().pid == 0', 'stayedOff',
]);
const KINDS = ['http-silent', 'ws-silent'];
const STOPS = ['shutdown', 'fatal', 'client-harvest'];
const ROWS = STOPS.flatMap(stop => KINDS.map(kind => stop + '.' + kind));
const BUDGETS = Object.freeze({compileMs:60000, nativeBindingCancelMs:20000,
  outerTypedGateMs:135000, shutdownMs:1500, callbackOwnerReleaseMs:1500});

/** Structural checks grant no source-current or behavioral acceptance. */
export function validateOwnedAsyncParentEnvelope(value) {
  assert.equal(value.schemaVersion, 1); assert.equal(value.id, 'sidecar.decky-typed-owned-async-request');
  bool(value.passed, fail('VERDICT_BOOLEAN')); assert.equal(value.status, value.passed ? 'PASS' : 'FAIL');
  assert.equal(value.fullPersistentAcceptance,false);assert.equal(value.acceptanceScope,'typed-worker-owned-subtree-v2');assert.equal(value.sameProcessLibraryNaturalZeroClaim,'NOT_ASSERTED_BY_TYPED_ISOLATION_SCOPE');
  assert.deepEqual(value.sourceChanged, []); assert.deepEqual(value.toolsChanged, []);
  assert.equal(value.maxArtifactInputs, 3072);
  assert.deepEqual(Object.keys(value.budgets).sort(), [...Object.keys(BUDGETS), 'reference'].sort());
  for (const [name, ms] of Object.entries(BUDGETS)) assert.equal(value.budgets[name], ms, fail('BUDGET:') + name);
  integer(value.elapsedMs, fail('OUTER_ELAPSED')); assert(value.elapsedMs <= 135000, fail('OUTER_DEADLINE'));
  assert.deepEqual(value.executions.map(row => row.id), ['legacy-compile','legacy-native','typed-compile','typed-native']);
  let total = 0;
  value.executions.forEach((row, i) => {
    const cap = i % 2 === 0 ? 60000 : 20000;
    assert.equal(row.originalBudgetMs, cap, fail('PROCESS_BUDGET'));
    assert(row.timeoutMs > 0 && row.timeoutMs <= cap, fail('PROCESS_TIMEOUT'));
    integer(row.elapsedMs, fail('PROCESS_ELAPSED')); assert(row.elapsedMs <= row.timeoutMs, fail('PROCESS_DEADLINE'));
    assert.equal(row.signal, null); assert.equal(row.error, null);
    assert.equal(row.exitCode, i === 3 ? (value.passed ? 0 : 1) : (i === 1 ? value.legacyRegression.naturalExit : 0));
    total += row.elapsedMs;
  });
  assert(total <= value.elapsedMs, fail('SHARED_OUTER_BUDGET'));
  assert.equal(value.legacyRegression.source, LEGACY);
  assert.equal(value.legacyRegression.historicalResultsReclassified, false);
  assert(['PASS','FAIL'].includes(value.legacyRegression.status), fail('LEGACY_NOT_EXECUTED'));
  assert.equal(value.nodeToolProof.sourceApprovalGranted, false);
  assert(/^[a-f0-9]{64}$/.test(value.nodeToolProof.graphSha256), fail('NODE_GRAPH_PIN'));
  return value;
}

/** Caller supplies sourcePins/toolPins captured before child execution and a
 * trusted result pin. Never derive new approval hashes from the child receipt.
 * sourcePins: Map<repo-relative path, sha256>; toolPins: Map<canonical path, pin>.
 * workRoot/evidenceRoot are the exact original child's roots, not umbrella roots.
 * Frozen mode additionally requires currentRepo and a separate frozen repo view;
 * its envelope always has allowAcceptance=false, including for a historical PASS.
 */
export function validateOwnedAsyncRequestParentReceipt(value, {
  repo, evidenceRoot, workRoot, sourcePins, toolPins, receiptPin,
  sourceMode = 'current', currentRepo = repo,
}) {
  validateOwnedAsyncParentEnvelope(value);
  assert(sourcePins instanceof Map && sourcePins.size > 0, fail('SOURCE_PREBINDING_REQUIRED'));
  assert(toolPins instanceof Map && toolPins.size > 0, fail('TOOL_PREBINDING_REQUIRED'));
  assert(['current','frozen'].includes(sourceMode), fail('SOURCE_MODE'));
  if (sourceMode === 'frozen') assert(key(currentRepo) !== key(repo), fail('SEPARATE_FROZEN_VIEW_REQUIRED'));
  else samePath(currentRepo, repo, fail('CURRENT_REPO_FORWARDING'));
  const verify = (row, root) => {
    assert(row && typeof row.path === 'string' && path.isAbsolute(row.path) && /^[a-f0-9]{64}$/.test(row.sha256), fail('FILE_PIN'));
    if (root) assert(inside(root, row.path), fail('OWNED_FILE_ESCAPE:') + row.path);
    samePath(fs.realpathSync(row.path), row.path, fail('REPARSE_FILE'));
    assert.equal(digest(row.path), row.sha256, fail('FILE_CHANGED:') + row.path);
    if (row.bytes !== undefined) assert.equal(fs.statSync(row.path).size, row.bytes, fail('FILE_SIZE'));
    return row;
  };
  const originalResult = verify(receiptPin, evidenceRoot);
  samePath(originalResult.path, path.join(evidenceRoot, 'result.json'), fail('RESULT_LOCATION'));
  assert.deepEqual(read(originalResult.path), value, fail('ORIGINAL_RESULT_MISMATCH'));
  for (const root of [repo,evidenceRoot,workRoot]) samePath(fs.realpathSync(root), root, fail('ROOT_REPARSE'));
  const sourceSeen = new Set(), compilePins = new Map(), staleSources = [];
  assert(Array.isArray(value.sourceInputs) && value.sourceInputs.length > 0 && value.sourceInputs.length <= 256);
  for (const row of value.sourceInputs) {
    assert(typeof row.path === 'string' && !path.isAbsolute(row.path) && !row.path.includes('\\') &&
      row.path.split('/').every(p => p && p !== '.' && p !== '..'), fail('SOURCE_PATH'));
    assert(!sourceSeen.has(row.path), fail('DUPLICATE_SOURCE')); sourceSeen.add(row.path);
    assert.equal(sourcePins.get(row.path), row.sha256, fail('SOURCE_NOT_PREBOUND:') + row.path);
    verify({...row,path:path.join(repo,row.path)}, repo);
    const live = path.join(currentRepo, row.path);
    if (!fs.existsSync(live) || digest(live) !== row.sha256) staleSources.push(row.path);
    const frozen = path.join(workRoot, 's', row.path);
    if (fs.existsSync(frozen)) { verify({...row,path:frozen}, workRoot); compilePins.set(key(frozen), row); }
  }
  if (sourceMode === 'current') assert.deepEqual(staleSources, [], fail('CURRENT_SOURCE_STALE'));
  for (const name of [DRIVER+'.mjs',DRIVER+'.cpp',DRIVER+'.test.mjs',V1_DRIVER+'.cpp',V1_DRIVER+'.mjs',MODULE+'/typed-owned-async-native.mjs',MODULE+'/typed-owned-async-parent.mjs',LEGACY,
    'tools/decky_binding_runtime_selftest.cpp','tools/decky_sidebar_bootstrap_selftest.cpp',
    'native/decky_sidebar_runtime.h','native/decky_binding_client.h','native/decky_binding_process_owner.h',
    'native/decky_sidebar_async_http.h',MODULE+'/owned-tools.json',
    'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json']) {
    assert(sourceSeen.has(name), fail('REQUIRED_SOURCE:') + name);
  }
  assert.deepEqual(read(path.join(evidenceRoot,'SOURCE-PINS.json')), value.sourceInputs, fail('SOURCE_PRECOMPILE_RECEIPT'));
  assert.deepEqual(read(path.join(evidenceRoot,'TOOL-PINS.json')), value.toolArtifacts, fail('TOOL_PRECOMPILE_RECEIPT'));
  const toolSeen = new Set();
  assert(value.toolArtifacts.length > 0 && value.toolArtifacts.length <= 3072);
  for (const row of value.toolArtifacts) {
    const id = key(row.path); assert(!toolSeen.has(id), fail('DUPLICATE_TOOL')); toolSeen.add(id);
    const bound = toolPins.get(id); assert(bound, fail('TOOL_NOT_PREBOUND:') + row.path);
    assert.equal(bound.sha256, row.sha256, fail('TOOL_PREBINDING_CHANGED')); verify(row); compilePins.set(id, row);
  }
  const declarationPath = path.join(repo,MODULE,'typed-owned-async-source-inputs.json');
  // Declaration is optional for older sidecar runs; every frozen source still
  // needs an external pin, and both compiler graphs require complete membership.
  if (sourcePins.has(MODULE+'/typed-async-source-inputs.json') && fs.existsSync(declarationPath)) {
    assert.equal(digest(declarationPath), sourcePins.get(MODULE+'/typed-async-source-inputs.json'));
    assert.deepEqual([...sourceSeen].sort(), [...read(declarationPath).files].sort(), fail('SOURCE_DECLARATION_MEMBERSHIP'));
  }
  const originalContract = reconstructTypedAsyncOriginalContract(fs.readFileSync(path.join(repo,LEGACY),'utf8'));
  assert.deepEqual(value.originalContract, originalContract, fail('ORIGINAL_CONTRACT'));
  assert.deepEqual(read(path.join(evidenceRoot,'ORIGINAL-CONTRACT.json')), originalContract, fail('ORIGINAL_CONTRACT_FILE'));
  const runtime = fs.readFileSync(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'),'utf8');
  const peer = fs.readFileSync(path.join(repo,'tools/decky_sidebar_bootstrap_selftest.cpp'),'utf8');
  const legacy = fs.readFileSync(path.join(repo,LEGACY),'utf8');
  const ioBegin = legacy.indexOf('                AsyncRequest request(');
  const extractions = [
    {name:'helpers',origin:'tools/decky_binding_runtime_selftest.cpp',source:runtime,begin:0,end:runtime.indexOf('\nint main(int argc,char** argv){')},
    {name:'local-peer',origin:'tools/decky_sidebar_bootstrap_selftest.cpp',source:peer,begin:0,end:peer.indexOf('\nint main()')},
    {name:'original-io',origin:LEGACY,source:legacy,begin:ioBegin,end:legacy.indexOf('\n            }catch',ioBegin)},
  ];
  assert.equal(value.generated.length, 4, fail('EXTRACTION_MEMBERSHIP'));
  extractions.forEach((expected, i) => {
    const row = value.generated[i]; assert(expected.begin >= 0 && expected.end > expected.begin, fail('EXTRACTION_BOUNDARY'));
    assert.equal(row.origin, expected.origin); assert.equal(row.originSha256, sourcePins.get(expected.origin));
    assert.equal(row.begin, expected.begin); assert.equal(row.end, expected.end);
    assert.equal(row.appendNewline, true); assert.equal(row.bytePreserved, true);
    samePath(row.path, path.join(workRoot,'s/tools/typed-legacy-async-request-'+expected.name+'.inc'), fail('EXTRACTION_PATH'));
    assert.equal(hash(Buffer.from(expected.source.slice(expected.begin, expected.end)+'\n')), row.sha256, fail('EXTRACTED_BYTES'));
    verify(row, workRoot); compilePins.set(key(row.path), row);
  });
  const renamed=value.generated[3],v1Text=fs.readFileSync(path.join(repo,V1_DRIVER+'.cpp'),'utf8'),entry='int main(int argc,char** argv){',replacement='int legacySameProcessMain(int argc,char** argv){';
  assert.equal(v1Text.split(entry).length,2,fail('V1_RENAME_ENTRY_UNIQUE'));assert.equal(renamed.origin,V1_DRIVER+'.cpp');assert.equal(renamed.originSha256,sourcePins.get(V1_DRIVER+'.cpp'));assert.equal(renamed.derivation,'rename-main-only');assert.equal(renamed.entry,entry);assert.equal(renamed.replacement,replacement);samePath(renamed.path,path.join(workRoot,'s/tools/typed-owned-async-legacy-body.inc'),fail('V1_DERIVED_PATH'));assert.equal(renamed.sha256,hash(Buffer.from(v1Text.replace(entry,replacement))),fail('V1_BODY_BYTE_PRESERVATION'));verify(renamed,workRoot);compilePins.set(key(renamed.path),renamed);
  assert.deepEqual(read(path.join(evidenceRoot,'GENERATED.json')), value.generated, fail('EXTRACTION_FILE'));
  const native = read(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json')).native;
  const driverSource=fs.readFileSync(path.join(repo,DRIVER+'.mjs'),'utf8');
  const guiFlagsDeclared=driverSource.includes("'/SUBSYSTEM:WINDOWS','/ENTRY:mainCRTStartup'");
  const guiGuardDeclared=driverSource.includes('TYPED_NATIVE_GUI_REQUIRED');
  assert.equal(guiFlagsDeclared,guiGuardDeclared,fail('GUI_CONTRACT_PARTIAL'));
  // Historical frozen console receipts remain integrity-only. Current production
  // same-image evidence must reproduce the actual Windows GUI subsystem.
  if(sourceMode==='current')assert.equal(guiFlagsDeclared,true,fail('CURRENT_GUI_CONTRACT_REQUIRED'));
  const typedLinkFlags=guiFlagsDeclared?['/SUBSYSTEM:WINDOWS','/ENTRY:mainCRTStartup']:[];
  const libraries = ['advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib','shell32.lib'];
  const flags = ['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',
    '/I'+path.join(workRoot,'s/native'),'/I'+path.join(workRoot,'s/deps/json'),'/I'+path.join(workRoot,'s/tools')];
  const old = path.join(workRoot,'l'), exe = path.join(workRoot,'steam.exe');
  const expectedArgs = [
    [...flags,path.join(workRoot,'s',LEGACY),'/sourceDependencies',path.join(old,'d.json'),'/Fo'+path.join(old,'p.obj'),'/Fe'+path.join(old,'legacy.exe'),'/link',...libraries],
    [path.join(old,'binding-fixtures')],
    [...flags,'/Bv','/W4','/WX','/UYMCC_DECKY_RUNTIME_LEGACY_FIXTURE',path.join(workRoot,'s',DRIVER+'.cpp'),'/sourceDependencies',path.join(workRoot,'d.json'),'/Fo'+path.join(workRoot,'p.obj'),'/Fe'+exe,'/link',...typedLinkFlags,...libraries],
    [path.join(workRoot,'binding-fixtures')],
  ];
  value.executions.forEach((row, i) => {
    assert.deepEqual(row.args, expectedArgs[i], fail('COMMAND_CHANGED:')+row.id);
    samePath(row.cwd, i < 2 ? old : workRoot, fail('PROCESS_CWD'));
    samePath(row.program.path, i % 2 === 0 ? native.compiler : i === 1 ? path.join(old,'legacy.exe') : exe, fail('PROCESS_PROGRAM'));
    verify(row.program, i % 2 === 0 ? undefined : workRoot);
    if (i % 2 === 0) assert.equal(toolPins.get(key(row.program.path))?.sha256, row.program.sha256, fail('COMPILER_NOT_PREBOUND'));
    for (const channel of ['stdout','stderr']) {
      samePath(row[channel].path,path.join(evidenceRoot,row.id+'.'+channel+'.log'),fail('PROCESS_LOG_PATH'));
      verify(row[channel], evidenceRoot);
    }
    assert.deepEqual(read(path.join(evidenceRoot,row.id+'.process.json')),row,fail('PROCESS_ORIGINAL'));
  });
  const graphCounts = [];
  for (const [i, graphPin] of [value.legacyRegression.compilerGraph, value.compilerGraph].entries()) {
    samePath(graphPin.path,path.join(i === 0 ? old : workRoot,'d.json'),fail('GRAPH_PATH')); verify(graphPin,workRoot);
    const graph = read(graphPin.path); assert.equal(graph.Version,'1.2');
    const files = [graph.Data.Source,...graph.Data.Includes];
    samePath(graph.Data.Source,path.join(workRoot,'s',i === 0 ? LEGACY : DRIVER+'.cpp'),fail('GRAPH_SOURCE'));
    assert.equal(files.length,graphPin.inputs); assert.equal(graphPin.allDeclared,true); assert.equal(graphPin.osLoadedModuleGraph,false);
    const seen = new Set();
    for (const file of files) {
      // MSVC records repeatable SDK headers once per include occurrence.
      // Preserve the raw graph count and check every occurrence against its pin.
      seen.add(key(file));
      const pin = compilePins.get(key(file)); assert(pin,fail('UNDECLARED_COMPILER_INPUT:')+file);
      verify({...pin,path:file});
    }
    for (const file of i === 0 ? [path.join(workRoot,'s',LEGACY)] : value.generated.map(r=>r.path))
      assert(seen.has(key(file)),fail('MISSING_GRAPH_INPUT'));
    graphCounts.push(files.length);
  }
  assert.equal(value.nativeArtifacts.length,3); assert(value.toolArtifacts.length+value.nativeArtifacts.length+value.generated.length <= 3072);
  const artifactPaths = [exe,path.join(workRoot,'bin/cef/fixture/steamwebhelper.exe'),path.join(workRoot,'p.obj')];
  value.nativeArtifacts.forEach((row,i)=>{samePath(row.path,artifactPaths[i],fail('NATIVE_ARTIFACT_MEMBERSHIP'));verify(row,workRoot);});
  assert.equal(value.nativeArtifacts[0].sha256,value.nativeArtifacts[1].sha256,fail('CEF_SELF_IMAGE'));
  assert.deepEqual(value.nativeArtifacts[0],value.executions[3].program,fail('EXECUTED_NATIVE_IDENTITY'));
  const nativeImage=fs.readFileSync(exe),nativePe=nativeImage.readUInt32LE(0x3c);
  assert(nativePe>=0x40&&nativePe+94<=nativeImage.length&&nativeImage.readUInt32LE(nativePe)===0x4550,fail('NATIVE_PE_HEADER'));
  assert.equal(nativeImage.readUInt16LE(nativePe+4),0x8664,fail('NATIVE_X64'));
  assert.equal(nativeImage.readUInt16LE(nativePe+24),0x20b,fail('NATIVE_PE32PLUS'));
  assert.equal(nativeImage.readUInt16LE(nativePe+24+68),guiFlagsDeclared?2:3,fail('NATIVE_GUI_SUBSYSTEM'));
  if(guiFlagsDeclared){samePath(value.nativeImage.path,exe,fail('NATIVE_IMAGE_PATH'));verify(value.nativeImage,workRoot);assert.equal(value.nativeImage.subsystem,2);assert.equal(value.nativeImage.sourceBuilt,true);}

  samePath(value.raw.path,path.join(evidenceRoot,'native-result.json'),fail('RAW_PATH')); verify(value.raw,evidenceRoot);
  const raw = read(value.raw.path);
  const workRaw = path.join(workRoot,'native-result-v2.json'); assert.equal(digest(workRaw),value.raw.sha256,fail('NATIVE_RAW_COPY'));
  const stdoutValue = row => JSON.parse(fs.readFileSync(row.stdout.path,'utf8').trim().split(/\r?\n/).at(-1));
  assert.deepEqual(stdoutValue(value.executions[3]),raw,fail('NATIVE_STDOUT'));
  const observations=new Map();assert(Array.isArray(value.controls)&&value.controls.length===10,fail('CONTROL_RAW_COUNT'));
  for(const row of value.controls){assert(/^[a-f0-9]{32}$/.test(row.nonce??''));assert(!observations.has(row.nonce),fail('CONTROL_RAW_NONCE_REUSE'));samePath(row.raw.path,path.join(evidenceRoot,'controls','control-'+row.nonce+'.json'),fail('CONTROL_RAW_PATH'));verify(row.raw,evidenceRoot);const original=path.join(workRoot,'control-'+row.nonce+'.json');assert.equal(digest(original),row.raw.sha256,fail('CONTROL_NATIVE_RAW_COPY'));const value=read(row.raw.path);assert.equal(value.id,row.nonce);assert.deepEqual(value.identity,row.childIdentity);observations.set(row.nonce,value);}
  const reconstructed=inspectOwnedAsyncNative(raw,{executablePath:exe,observations});reconstructed.missingPredicates=[];

  assert.deepEqual(value.summary,reconstructed.summary,fail('CHILD_SUMMARY'));
  samePath(value.mapping.path,path.join(evidenceRoot,'ORDERED-MAPPING.json'),fail('MAPPING_PATH')); verify(value.mapping,evidenceRoot);
  const mapping = read(value.mapping.path);
  assert.deepEqual(mapping,raw.originalTypedRows.map(row=>({id:row.id,kind:row.kind,stop:row.stop,status:row.passed?'PASS':'FAIL',
    legacyEquivalentCallOrder:row.stop==='shutdown',
    difference:row.stop==='client-harvest'?'Current Client.cancel is followed by a measured callback harvest before Runtime.shutdown. This is additional evidence and cannot replace the original immediate Runtime.shutdown order.':row.stop==='fatal'?'Same immediate owned stop, with explicit fatal reason added.':'Exact original immediate shutdown ordering and original deadlines retained.',
    originalAssertions:row.originalAssertions,raw:value.raw,actualAsync:row.afterStopAsync,currentResources:row.atExit})),fail('ORDERED_MAPPING'));
  const originalLegacy = read(path.join(evidenceRoot,'LEGACY-RESULT.json'));
  assert.deepEqual(value.legacyRegression,originalLegacy,fail('INDEPENDENT_LEGACY_RESULT'));
  const oldRun = originalLegacy;
  assert.deepEqual(oldRun.compile,value.executions[0],fail('LEGACY_COMPILE_RECEIPT'));
  samePath(oldRun.raw.path,path.join(old,'binding-cancel-results.json'),fail('LEGACY_RAW_PATH')); verify(oldRun.raw,workRoot);
  const legacyRaw = read(oldRun.raw.path); assert.deepEqual(oldRun.rawValue,legacyRaw,fail('LEGACY_RAW'));
  assert.deepEqual(stdoutValue(value.executions[1]),legacyRaw,fail('LEGACY_STDOUT'));
  assert.deepEqual(legacyRaw.cases.map(r=>r.kind),KINDS,fail('LEGACY_CASES'));
  const legacyOkay = legacyRaw.cases.map(row => {
    assert.equal(row.deadlineMs,1500); integer(row.shutdownMs,fail('LEGACY_SHUTDOWN_MS'));
    const okay = row.shutdownMs < 1500 && row.bindingReturned === true && row.bindingReason === 'mirror-bootstrap-canceled' &&
      row.ownedChildExitObserved === true && row.requestCallbackOwnersReleased === true && row.offSurvivedLateRequests === true;
    assert.equal(row.ok,okay,fail('LEGACY_CONDITIONS')); return okay;
  }).every(Boolean);
  assert.equal(legacyRaw.ok,legacyOkay); assert.equal(oldRun.status,legacyOkay?'PASS':'FAIL');
  assert.equal(oldRun.naturalExit,legacyOkay?0:1);
  assert.equal(legacyRaw.realSteamActions,0); assert.equal(legacyRaw.officialLoaderExecuted,false);
  assert.equal(value.passed,reconstructed.passed && legacyOkay,fail('RESULT_VERDICT'));
  const firstFailure = reconstructed.summary.typedFailed ? 'UNMET_ORIGINAL_TYPED_CALLBACK_INVARIANTS' :
    !raw.ownedCalibrationPassed ? 'TYPED_OWNED_ASYNC_OWNED_LIFECYCLE_NOT_PASS' : !raw.strictParentHandlesEqual ? 'TYPED_OWNED_ASYNC_OWNED_LIFECYCLE_NOT_PASS' :
      !raw.strictParentThreadsEqual ? 'THREAD_EQUALITY' : !legacyOkay ? 'INDEPENDENT_ORIGINAL_REGRESSION_FAILED' : null;
  if (value.passed) assert(!value.error,fail('PASS_WITH_ERROR'));
  else assert(value.error?.message?.startsWith(firstFailure),fail('UNRELATED_DRIVER_FAILURE'));
  const missingPredicates = [...reconstructed.missingPredicates,
    ...(!legacyOkay ? [{rowId:'legacy',predicate:'independent original regression PASS'}] : [])];
  // Recheck approval-bound inputs after consuming all raw files. Historical
  // source status is observational; the executed frozen source never changes.
  staleSources.length = 0;
  for (const row of value.sourceInputs) {
    verify({...row,path:path.join(repo,row.path)},repo);
    const live=path.join(currentRepo,row.path);
    if(!fs.existsSync(live)||digest(live)!==row.sha256) staleSources.push(row.path);
  }
  for(const row of value.toolArtifacts) verify(row);
  for(const row of [receiptPin,value.raw,value.mapping,...value.generated,...value.nativeArtifacts]) verify(row);
  if(sourceMode==='current') assert.deepEqual(staleSources,[],fail('CURRENT_SOURCE_STALE'));
  const allowAcceptance = value.passed && sourceMode === 'current' && staleSources.length === 0 && missingPredicates.length === 0;
  return {verified:true,acceptance:allowAcceptance,allowAcceptance,
    acceptanceScope:'typed-worker-owned-subtree-v2',sameProcessLibraryNaturalZeroClaim:'NOT_ASSERTED_BY_TYPED_ISOLATION_SCOPE',status:value.passed?'CURRENT_TYPED_OWNED_ASYNC_REQUEST_SATISFIED':'CURRENT_TYPED_OWNED_ASYNC_REQUEST_FAIL',
    currentSourceStatus:staleSources.length?'STALE':'CURRENT',sourceMode,staleSources,
    fullPersistentAcceptance:false,summary:reconstructed.summary,missingPredicates,ordered:reconstructed.ordered,
    result:receiptPin,raw:value.raw,mapping:value.mapping,legacy:{status:oldRun.status,raw:oldRun.raw,independentlyExecuted:true},
    checked:{sources:sourceSeen.size,tools:toolSeen.size,compilerGraphs:graphCounts,generated:4,originalPredicates:72,ownedDirectControls:10},
    boundary:'Completed original files only; frozen historical evidence cannot authorize current-source acceptance. No installed Steam/Loader, real browser/physical sleep, complete parent lifecycle or power claim.'};
}

