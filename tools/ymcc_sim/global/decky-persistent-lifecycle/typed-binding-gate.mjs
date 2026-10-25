// Public lifecycle wiring for the production typed binding path. Historical
// callback receipts remain negative evidence; this file never rewrites them.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const slash = file => file.replaceAll('\\', '/');
const relative = (root, file) => slash(path.relative(root, file));
const hashFile = file => sha(fs.readFileSync(file));
export const typedIds = Object.freeze({
 healthy: 'native.typed-runtime-drains-live-watch-without-extra-reader-thread',
 inactive: 'native.typed-rejected-final-admission-releases-watch-before-off',
 stalled: 'native.typed-stalled-binding-stop-before-fixture-release',
 descendants: 'native.typed-stalled-binding-descendants-stop-before-release',
 ready: 'native.typed-ready-rebind-keeps-original-owned-loader',
 retained: 'native.typed-retained-ready-rebind-never-starts-loader',
});
export const historicalStallId = 'native.stalled-binding-stop-requires-fixture-release';
export const productionWiringId = 'native.production-typed-binding-wiring';
export const historicalValidationId = 'native.historical-callback-negative-preserved';
export function resourceZero(s) {
 return s?.fullyDestroyed === true && s.handles === 0 && s.pendingIo === 0 && s.activeCalls === 0 && s.jobActiveProcesses === 0 && s.childExited === true &&
  s.ownerHandles === 0 && s.ownerPendingIo === 0 && s.readerHandles === 0 && s.readerPendingIo === 0 && s.clientHandles === 0;
}
function blockAfter(text, anchor) {
 const start = text.indexOf(anchor); assert(start >= 0, 'Production binding source anchor missing: ' + anchor);
 const open = text.indexOf('{', start); let depth = 0, quoted = '', comment = '';
 for (let i = open; i < text.length; ++i) {
  const c = text[i], next = text[i + 1];
  if (comment === '//') { if (c === '\n') comment = ''; continue; }
  if (comment === '/*') { if (c === '*' && next === '/') { comment = ''; ++i; } continue; }
  if (quoted) { if (c === '\\') ++i; else if (c === quoted) quoted = ''; continue; }
  if (c === '/' && (next === '/' || next === '*')) { comment = '/' + next; ++i; continue; }
  if (c === '"' || c === "'") { quoted = c; continue; }
  if (c === '{') ++depth;
  else if (c === '}' && --depth === 0) return text.slice(open + 1, i);
 }
 throw Error('Unclosed production source block: ' + anchor);
}
export function verifyProductionWiring({root, out, tools, execute}) {
 const source = path.join(out, 'production-binding-wiring.cpp'), preprocessed = path.join(out, 'production-binding-wiring.ii');
 fs.writeFileSync(source, '#ifndef WIN32_LEAN_AND_MEAN\n#define WIN32_LEAN_AND_MEAN\n#endif\n#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE\n#error legacy fixture macro is forbidden in production evidence\n#endif\n#include "decky_sidebar_runtime.h"\n');
 const run = execute('build.production-binding-wiring', tools.native.compiler,
  ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE', '/UYMCC_DECKY_RUNTIME_LEGACY_FIXTURE',
   '/I' + path.join(root, 'native'), '/I' + path.join(root, 'deps/json'), '/P', '/Fi' + preprocessed, source], {timeout: 60000, allowFailure: true});
 assert.equal(run.passed, true, 'Production Runtime preprocessing failed');
 const expanded = fs.readFileSync(preprocessed, 'utf8');
 const config = blockAfter(expanded, 'struct Config {'), runtime = blockAfter(expanded, 'class Runtime {');
 assert.match(config, /std::shared_ptr\s*<\s*ymcc::deckybinding::Client\s*>\s*ownedBinding\s*;/);
 assert.match(config, /bindingPlan\s*;/);
 for (const name of ['refreshBinding', 'prepareBinding', 'retainedBinding', 'beginPending', 'cancelPending', 'takeContextProcess']) {
  assert.doesNotMatch(config, new RegExp('\\b' + name + '\\b'), 'Legacy Config member executable in production: ' + name);
  assert.doesNotMatch(runtime, new RegExp('config_\\s*\\.\\s*' + name + '\\b'), 'Legacy Runtime callback executable: ' + name);
 }
 for (const method of ['enable', 'begin', 'next', 'cancel', 'stop', 'takeContextProcess'])
  assert.match(runtime, new RegExp('config_\\.ownedBinding\\s*->\\s*' + method + '\\s*\\('));
 assert.match(runtime, /client\s*->\s*duplicateWaitHandles\s*\(/);
 const mainFile = path.join(root, 'native/main.cpp'), main = fs.readFileSync(mainFile, 'utf8');
 assert.doesNotMatch(main, /^\s*#\s*define\s+YMCC_DECKY_RUNTIME_LEGACY_FIXTURE\b/m);
 const initialize = blockAfter(main, 'static void deckySidebarInitialize()');
 assert.match(initialize, /config\.ownedBinding\s*=\s*std::make_shared\s*<\s*ymcc::deckybinding::Client\s*>\s*\(\s*\)/);
 assert.match(initialize, /config\.bindingPlan\s*=\s*deckySidebarOwnedBindingPlan\s*;/);
 assert.match(initialize, /config\.bindingDiagnostic\s*=\s*deckySidebarOwnedBindingDiagnostic\s*;/);
 assert.match(initialize, /g_deckySidebarRuntime\.initialize\s*\(/);
 assert.doesNotMatch(main, /config\s*\.\s*(refreshBinding|prepareBinding|retainedBinding|takeContextProcess|beginPending|cancelPending)\s*=/);
 const receipt = {schema: 'ymcc.decky.production-typed-binding-wiring.v1', passed: true,
  productionMacro: 'YMCC_DECKY_RUNTIME_LEGACY_FIXTURE undefined', executableLegacyCallbackFallback: false,
  mainOwnedBindingConnected: true, preprocessed: relative(out, preprocessed), preprocessedSha256: hashFile(preprocessed),
  mainSha256: hashFile(mainFile), runtimeSha256: hashFile(path.join(root, 'native/decky_sidebar_runtime.h')),
  scope: 'Actual compiler preprocessing of current Runtime plus current main Config wiring; not a full linked host/package acceptance'};
 fs.writeFileSync(path.join(out, 'production-binding-wiring.json'), JSON.stringify(receipt, null, 2) + '\n');
 return {id: productionWiringId, name: productionWiringId, passed: true, measurements: receipt};
}
export function verifyTypedRuntime({root, out, execution}) {
 assert.equal(execution.passed, true, 'Fresh typed Runtime build/run failed');
 const dir = path.join(out, 'typed-runtime'), file = path.join(dir, 'typed-runtime-result.json');
 const result = read(file), build = read(path.join(dir, 'BUILD.json')), run = read(path.join(dir, 'RESULT.json')), pins = read(path.join(dir, 'SOURCE-PINS.json'));
 assert.equal(build.exit, 0); assert.equal(run.status, 'pass'); assert.equal(run.compilerExit, 0); assert.equal(run.runExit, 0);
 assert.deepEqual(run.changedOriginalInputs, []); assert.equal(run.exeSha256, hashFile(path.join(dir, 'FixtureSteam/steam.exe')));
 assert(!build.args.some(arg => /^\/DYMCC_DECKY_RUNTIME_LEGACY_FIXTURE(?:=|$)/i.test(arg)));
 assert(pins.files.length > 0);
 for (const pin of pins.files) {
  assert(!path.isAbsolute(pin.path) && !pin.path.split(/[\\/]/).includes('..'), 'Invalid frozen source path');
  assert.equal(hashFile(path.join(root, pin.path)), pin.sha256, 'Typed source drift: ' + pin.path);
  assert.equal(hashFile(path.join(dir, 'source', pin.path)), pin.sha256, 'Frozen typed source changed: ' + pin.path);
 }
 for (const required of ['native/decky_sidebar_runtime.h', 'native/decky_binding_client.h', 'native/decky_binding_process_owner.h', 'native/decky_binding_worker.h', 'tools/decky_binding_runtime_selftest.cpp'])
  assert(pins.files.some(pin => pin.path === required), 'Typed production dependency not frozen: ' + required);
 assert.equal(result.passed, true); assert.equal(result.tests, Object.keys(typedIds).length); assert.equal(result.passedCount, result.tests);
 assert.equal(result.deadlineMs, 3200); assert.equal(result.productCallbackMode, 'typed-only; legacy macro absent');
 assert.equal(result.originalHistoricalFailurePreserved, true); assert.equal(result.realSteamOrDeviceActions, 0);
 assert.deepEqual(result.cases.map(row => row.id).sort(), Object.values(typedIds).sort());
 const cold = result.sharedCryptoInitialization;
 assert(cold && cold.samples.length >= 10 && cold.samples.at(-1).elapsedMs >= 500, 'Shared CNG baseline stability not recorded');
 assert(cold.samples.every(sample => sample.handles === cold.afterHandles), 'Unstable shared CNG baseline');
 const rawProof = {path: relative(out, file), sha256: hashFile(file)};
 const rows = result.cases.map(raw => {
  assert.equal(raw.passed, true, 'Typed business case failed: ' + raw.id);
  assert(resourceZero(raw.atExit ?? raw.atDeadline ?? raw.afterOff), 'Typed owned resources not zero: ' + raw.id);
  const before = raw.beforeHandles ?? raw.parentHandlesBefore, after = raw.afterHandles ?? raw.parentHandlesAfter;
  assert(Number.isSafeInteger(before) && before === after, 'Raw parent handle count mismatch: ' + raw.id);
  if (raw.id === typedIds.healthy) {
   assert.equal(raw.watchDrained, true); assert.equal(raw.diagnosticFrames, 192); assert.equal(raw.cefUnaffected, true);
   assert.equal(raw.activeThreads, raw.beforeThreads + 1, 'Extra parent binding reader thread');
  } else if (raw.id === typedIds.inactive) {
   assert(resourceZero(raw.beforeOff), 'Inactive watch survived rejected final admission');
   assert.equal(raw.loaderPid, 0); assert(raw.admissionReads >= 2); assert.equal(raw.cefUnaffected, true);
  } else if ([typedIds.stalled, typedIds.descendants].includes(raw.id)) {
   for (const name of ['completedBeforeFixtureRelease', 'fixtureReleaseUnsentAtMeasurement', 'ownedLoaderExited', 'bindingChildExited', 'descendantExited', 'cefUnaffected', 'lateRefreshRejected', 'lateRecoveryRejected']) assert.equal(raw[name], true, raw.id + ': ' + name);
   assert.equal(raw.deadlineMs, 3200); assert(raw.stopMs >= 0 && raw.stopMs < 3200);
   assert(raw.beforeStop.handles > 0 && raw.beforeStop.pendingIo > 0 && raw.beforeStop.childExited === false, 'Stall not exercised with live binding/IO');
  } else {
   for (const name of ['acceptedReady', 'duplicateReadyRejected', 'zeroReadyRejected', 'originalCefAlive']) assert.equal(raw[name], true, raw.id + ': ' + name);
   assert.equal(raw.plans, 2); assert(raw.initialBindingPid > 0 && raw.nextBindingPid > 0 && raw.initialBindingPid !== raw.nextBindingPid);
   assert.equal(raw.afterLoaderPid, raw.initialLoaderPid);
   assert(raw.id === typedIds.retained ? raw.initialLoaderPid === 0 : raw.initialLoaderPid > 0);
  }
  // Raw tables remain in the byte-pinned receipt; acceptance uses the measured
  // scalar and resource fields, without copying thousands of handle-table rows.
  const measurements = Object.fromEntries(Object.entries(raw).filter(([key, value]) => !['id', 'passed'].includes(key) && (value === null || typeof value !== 'object' || ['atExit', 'atDeadline', 'beforeStop', 'beforeOff', 'afterOff', 'active'].includes(key))));
  return {id: raw.id, name: raw.id, passed: true, evidence: rawProof, measurements};
 });
 return {rows, receipt: {schema: result.schema, passed: true, raw: rawProof, build: {path: relative(out, path.join(dir, 'BUILD.json')), sha256: hashFile(path.join(dir, 'BUILD.json'))},
  execution: {path: relative(out, path.join(dir, 'RESULT.json')), sha256: hashFile(path.join(dir, 'RESULT.json'))},
  sourcePins: {path: relative(out, path.join(dir, 'SOURCE-PINS.json')), sha256: hashFile(path.join(dir, 'SOURCE-PINS.json')), verifiedCurrentFiles: pins.files.length},
  sharedCryptoInitialization: {scope: cold.scope, beforeHandles: cold.beforeHandles, afterHandles: cold.afterHandles, samples: cold.samples},
  currentEvidenceFor: 'destroyOnExit/destroyWhenStopStalls/explicitOffStaysOff/recoverAfterSleep; typed production path, legacy macro absent'}};
}
export function preserveHistoricalCallbackNegative({root, out, execution, nativeResult}) {
 const file = path.join(out, 'native/native-result.json'), bytes = fs.readFileSync(file);
 const source = path.join(root, 'tools/ymcc_sim/global/decky-persistent-lifecycle/native-fixture.cpp');
 const record = {classification: 'historical-in-process-callback-expected-negative', execution, path: relative(out, file), sha256: sha(bytes), bytes: bytes.length,
  source: relative(root, source), sourceSha256: hashFile(source), originalNativeResult: nativeResult,
  expectedExitCode: 1, expectedFailedCaseId: historicalStallId, currentProductionEvidence: 'typed-runtime/typed-runtime-result.json'};
 let error;
 try {
  assert.equal(execution.exitCode, 1); assert.equal(execution.passed, false); assert(!execution.error);
  assert.equal(nativeResult.nativeExecuted, true); assert.equal(nativeResult.passed, false);
  assert.match(fs.readFileSync(source, 'utf8'), /^\s*#define\s+YMCC_DECKY_RUNTIME_LEGACY_FIXTURE\s+1\b/m);
  assert.deepEqual(nativeResult.cases.filter(row => row.passed !== true).map(row => row.id), [historicalStallId]);
  const row = nativeResult.cases.find(row => row.id === historicalStallId), m = row.measurements;
  assert.equal(row.passed, false); assert.equal(m.deadlineMs, 3200); assert.equal(m.completedBeforeFixtureRelease, false);
  assert.equal(m.fixtureReleaseRequired, true); assert.equal(m.bounded, false); assert(m.elapsedMs >= 3200 && m.heldResourcesAtDeadline > 0);
  assert.equal(hashFile(file), record.sha256, 'Original historical bytes changed');
 } catch (cause) { error = cause.stack; }
 const validation = {schema: 'ymcc.decky.historical-callback-negative-validation.v1', passed: !error,
  raw: {path: record.path, sha256: record.sha256, originalExitCode: execution.exitCode, originalPassed: nativeResult.passed, expectedFailedCaseId: historicalStallId},
  originalStdout: {path: execution.stdout, sha256: execution.stdoutSha256}, originalStderr: {path: execution.stderr, sha256: execution.stderrSha256},
  ...(error ? {error} : {}), meaning: 'Expected negative preserved byte-for-byte; this is not current production callback acceptance'};
 fs.writeFileSync(path.join(out, 'native/historical-callback-negative-validation.json'), JSON.stringify(validation, null, 2) + '\n');
 return {record, validation, row: {id: historicalValidationId, name: historicalValidationId, passed: !error, measurements: validation}};
}
