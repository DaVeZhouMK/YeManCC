// R114/R115: real Windows private subprocesses against the unchanged production capture header.
// Writes only its explicitly selected evidence directory; never starts YMCC or InputHost.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
function option(name, fallback) {
  const index = args.indexOf(name);
  if (index < 0) return fallback;
  assert(index + 1 < args.length, `${name} needs a value`);
  return args[index + 1];
}
assert.equal(process.platform, 'win32', 'This test requires real Windows process/handle APIs.');
const output = path.resolve(option('--output', 'G:/YeManCC-Work/Build/Validation/VirtualInput-HelperLease-R115-20261016/native-helper-diagnostic'));
const rounds = Number(option('--rounds', '96'));
assert(Number.isInteger(rounds) && rounds >= 8 && rounds <= 256, '--rounds must be 8..256');
const fixture = path.join(repo, 'tools/fixtures/one_shot_helper_diagnostic/fixture.cpp');
const header = path.join(repo, 'native/one_shot_helper_diagnostic.h');
const runtime = path.join(repo, 'native/msi_native_foreground_isolation_runtime.h');
const jsonHeader = path.join(repo, 'deps/json/json.hpp');
// The explicit current-refresh adapter consumes completed R114 current-status
// receipts only. Default legacy producer gates and native fixture stay intact.
const producerMode = option('--producer-mode', 'legacy');
assert(['legacy', 'current-refresh'].includes(producerMode), '--producer-mode must be legacy or current-refresh');
const currentValidationOption = option('--current-refresh-validation', '');
const currentValidation = currentValidationOption ? path.resolve(currentValidationOption) : null;
const currentValidationSha256 = option('--current-refresh-validation-sha256', '').toLowerCase();
if (producerMode === 'current-refresh') {
  assert(currentValidation && /^[a-f0-9]{64}$/.test(currentValidationSha256), 'explicit current-refresh validation and SHA256 required');
  assert(!fs.existsSync(output), 'current-refresh requires a fresh evidence directory');
} else assert(!currentValidation, 'current-refresh validation requires explicit current-refresh mode');
const currentSourceBindings = currentValidation ? path.join(path.dirname(currentValidation), 'source-bindings.json') : null;
const producerCoverage = {mode: producerMode, legacyAuthorityReplay: producerMode === 'legacy' ? 'requested' : 'NOT_RUN', currentStatusRecords: 0};
const refreshProducerOption = option('--refresh-producer-results', '');
const refreshProducerResults = refreshProducerOption ? path.resolve(refreshProducerOption) : null;
const producerResults = path.resolve(option('--producer-results', 'G:/YeManCC-Work/Build/Validation/VirtualInput-OwnedExtensions-R114-20261016/helper-diagnostics/offline/candidate/results.json'));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const write = (file, bytes) => { fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, bytes); };
const trackingHeaders = ['native_refresh_writer_runtime.h', 'native_refresh_writer_store.h', 'native_refresh_writer_record.h', 'virtual_target_return_store.h', 'virtual_target_return_record.h', 'virtual_target_return_process.h', 'hidhide_cli_job_guard.h'].map(name => path.join(repo, 'native', name));
const inputs = [header, runtime, jsonHeader, fixture, ...trackingHeaders, fileURLToPath(import.meta.url), path.join(repo, 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'), producerResults, ...(refreshProducerResults ? [refreshProducerResults] : []), ...(currentValidation ? [currentValidation, currentSourceBindings] : [])];
const before = new Map(inputs.map(file => [file, fs.readFileSync(file)]));
const binding = inputs.map(file => ({path: file, sha256: hash(before.get(file)), bytes: before.get(file).length}));
const runtimeText = before.get(runtime).toString('utf8');
const headerText = before.get(header).toString('utf8');
function mask(code) {
  return code.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,
    value => value.replace(/[^\r\n]/g, ' '));
}
function block(name, kind = 'function') {
  const masked = mask(runtimeText);
  const expression = kind === 'struct' ? new RegExp(`^struct ${name}\\s*\\{`, 'm') :
    new RegExp(`^static [^\\n;]*\\b${name}\\([^;{}]*\\)[^;{}]*\\{`, 'm');
  const found = expression.exec(masked);
  assert(found, `bound actual ${name}`);
  let position = masked.indexOf('{', found.index), depth = 0;
  do {
    const char = masked[position++];
    if (char === '{') ++depth;
    if (char === '}') --depth;
  } while (depth > 0 && position < masked.length);
  assert.equal(depth, 0, name + ' complete source span');
  if (kind === 'struct') { assert.equal(masked[position], ';'); ++position; }
  const text = runtimeText.slice(found.index, position);
  return {name, line: runtimeText.slice(0, found.index).split('\n').length, sha256: hash(text), bytes: Buffer.byteLength(text), text};
}
const lease = block('MsiNativeForegroundLease', 'struct');
const settle = block('msiNativeForegroundSettleHelperDiagnosticLocked');
const poll = block('msiNativeForegroundPollCycleLocked');
const receipt = block('msiNativeForegroundReceipt');
const cycle = block('msiNativeForegroundCycleLocked');
const optionalStart = cycle.text.indexOf('    std::unique_ptr<ymcc::input::OneShotHelperDiagnostic> capture;');
const optionalLaunch = cycle.text.indexOf('    const DWORD launchFlags=', optionalStart);
const optionalTrackedBranch = cycle.text.indexOf('    if(s.profile==ymcc::nativeForeground::Profile::Gpd){', optionalStart);
const optionalEnd = optionalTrackedBranch >= 0 ? Math.min(optionalLaunch, optionalTrackedBranch) : optionalLaunch;
assert(optionalStart >= 0 && optionalEnd > optionalStart, 'exact optional capture allocation block');
const optional = cycle.text.slice(optionalStart, optionalEnd);
const assertions = [];
const clearPosition = cycle.text.indexOf('s.helperDiagnostic=nullptr;');
function sourceCheck(label, condition) { assertions.push({test: label, passed: !!condition}); }
sourceCheck('new attempt clears diagnostic JSON before helper fields/early failures', clearPosition >= 0 && clearPosition < cycle.text.indexOf('s.helperExePath.clear()') && clearPosition < cycle.text.indexOf('s.locations.empty()'));
sourceCheck('runtime includes actual capture header', runtimeText.includes('#include "one_shot_helper_diagnostic.h"'));
sourceCheck('ordinary receipt includes helperDiagnostic', receipt.text.includes('{"helperDiagnostic",s.helperDiagnostic}'));
sourceCheck('failure Settle emits existing ordinary lifecycle diagnostic', /if\(exitCode!=0\)appendNativeLifecycleLog\("input-host\.native-helper-diagnostic"/.test(settle.text));
sourceCheck('failure lifecycle does not change device result', settle.text.includes('{"deviceResultChanged",false}'));
sourceCheck('capture allocation is guarded by gameSource; root-only has no spool', /try\s*\{\s*if\s*\(gameSource\)\s*\{\s*capture=/.test(optional));
sourceCheck('fallback retains basic startup and disables inheritance when unavailable', /capture\?TRUE:FALSE/.test(cycle.text) && /capture\?capture->startup\(\):&si/.test(cycle.text));
sourceCheck('launch enables extended startup only for capture', cycle.text.includes('capture?EXTENDED_STARTUPINFO_PRESENT:0'));
sourceCheck('timeout stores same process handle and returns pending', /if\(wait!=WAIT_OBJECT_0\)\s*\{\s*s\.cycleProcess=pi\.hProcess;g_msiNativeForegroundCyclePending=true;return 1;/.test(cycle.text));
sourceCheck('poll proves termination before GetExitCode and Settle', poll.text.indexOf('WaitForSingleObject(s.cycleProcess,0)') < poll.text.indexOf('GetExitCodeProcess') && poll.text.indexOf('GetExitCodeProcess') < poll.text.indexOf('msiNativeForegroundSettleHelperDiagnosticLocked(code)'));
sourceCheck('terminal bound is 2048 bytes and oversize precedes read', /constexpr DWORD cap = 2048/.test(headerText) && headerText.indexOf('size.QuadPart > cap') < headerText.indexOf('ReadFile('));
sourceCheck('explicit inherited handle list contains exactly two handles', /std::array<HANDLE, 2> inherited_/.test(headerText) && /PROC_THREAD_ATTRIBUTE_HANDLE_LIST/.test(headerText));
sourceCheck('temporary stdout uses delete-on-close', headerText.includes('FILE_FLAG_DELETE_ON_CLOSE'));
const extracted = [lease, settle, poll, receipt, cycle];
const consumer = `// Exact bound runtime source; device dependencies are inert fixture declarations.\n${lease.text}\n` +
  'static MsiNativeForegroundLease g_msiNativeForegroundLease;\nstatic std::atomic<bool> g_msiNativeForegroundCyclePending{false};\nstatic std::atomic<uint64_t> g_msiNativeForegroundEdge{1};\n' +
  `${settle.text}\n${receipt.text}\n${poll.text}\n#define CreateProcessW fixtureCycleCreateProcessW\n${cycle.text}\n#undef CreateProcessW\n` +
  `static std::unique_ptr<ymcc::input::OneShotHelperDiagnostic> fixtureActualOptionalCapture(bool gameSource) {\n${optional}    return capture;\n}\n`;
write(path.join(output, 'runtime-consumer.actual.h'), consumer);
const producerDocument = JSON.parse(before.get(producerResults).toString('utf8'));
assert.equal(producerDocument.realHardwareAccess, false, 'producer is the completed no-device C# fixture');
const selectedProducer = [];
if (producerMode === 'legacy') {
for (const stage of ['first', 'after-prepare']) {
  const row = producerDocument.results.find(row => row.Facts?.Stage === stage && row.Facts?.LocateResult === 4294967295 && row.DiagnosticCount === 1);
  if (row) selectedProducer.push(row);
}
assert(selectedProducer.length >= 1, 'actual C# facts with LocateResult uint32 max');
}
const producerBindings = selectedProducer.map(row => {
  const wire = 'YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ' + JSON.stringify(row.Facts) + '\r\n';
  assert(Buffer.byteLength(wire) <= 2048, row.Id + ' byte cap');
  assert.equal(row.Facts.Schema, 1); assert.equal(row.Facts.Kind, 'ymcc.gameAuthority.failure'); assert.equal(row.Facts.Result, 8);
  return {id: row.Id, facts: row.Facts, bytes: Buffer.byteLength(wire), originalProducerUtf8Bytes: row.Utf8Bytes,
    sha256: hash(wire), wire, replay: 'C# deserialized Facts in original property order + fixed prefix/CRLF; C++ fixture child replay, no product C# process'};
});
const refreshProducerBindings = [];
if (producerMode === 'current-refresh') {
  assert(refreshProducerResults, 'current-refresh requires the actual candidate producer results');
  assert.equal(hash(before.get(currentValidation)), currentValidationSha256, 'current-refresh validation SHA256');
  const validation = JSON.parse(before.get(currentValidation).toString('utf8'));
  assert.equal(validation.mode, 'current-status');
  assert.equal(validation.success, true); assert.equal(validation.executedThisRun, true);
  assert.equal(validation.failures, 0); assert.equal(validation.realHardwareAccess, false);
  assert.equal(validation.productHostOrSdkRun, false);
  const samePath = (a, b) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
  const actual = path.dirname(currentValidation);
  assert(samePath(producerResults, path.join(actual, 'offline/baseline/results.json')), 'exact completed baseline receipt');
  assert(samePath(refreshProducerResults, path.join(actual, 'offline/candidate/results.json')), 'exact completed candidate receipt');
  function bindExpected(file, expected) {
    file = path.resolve(file);
    const bytes = before.has(file) ? before.get(file) : fs.readFileSync(file);
    assert.equal(hash(bytes), expected, 'bound completed helper input ' + file);
    if (!before.has(file)) { inputs.push(file); before.set(file, bytes); binding.push({path: file, sha256: hash(bytes), bytes: bytes.length}); }
  }
  for (const row of validation.fixtureBindings) bindExpected(path.join(repo, row.file), row.sha256);
  const sourceBindings = JSON.parse(before.get(currentSourceBindings).toString('utf8'));
  assert.equal(sourceBindings.completeSourceCompiled, true);
  const candidate = sourceBindings.bindings.find(row => row.variant === 'candidate');
  const baseline = sourceBindings.bindings.find(row => row.variant === 'baseline');
  assert(candidate && baseline, 'actual baseline/candidate source bindings required');
  assert.equal(candidate.sourceSha256, validation.candidateSha256);
  assert.equal(baseline.sourceSha256, validation.baselineSha256);
  assert(samePath(candidate.sourcePath, path.join(repo, 'InputHost/MsiNativePortCycle.cs')), 'current product helper source binding');
  for (const row of sourceBindings.bindings) {
    bindExpected(row.sourcePath, row.sourceSha256); bindExpected(row.offlineSourcePath, row.offlineSourceSha256);
  }
  const document = JSON.parse(before.get(refreshProducerResults).toString('utf8'));
  for (const completed of [producerDocument, document]) {
    assert.equal(completed.realHardwareAccess, false); assert.equal(completed.realSdkLinked, false);
    assert(Array.isArray(completed.failures) && completed.failures.length === 0, 'completed helper contains no failed assertions');
    assert.equal(completed.results.length, validation.scenarios, 'actual completed helper scenario count');
  }
  const rows = document.results.filter(row => row.Facts?.Kind === 'ymcc.gameRefresh.failure' && row.DiagnosticCount === 1);
  assert(rows.length > 0, 'actual current Result6 Facts required');
  const statusRows = rows.filter(row => row.Facts.DisableFailureStatus);
  const states = new Set(statusRows.map(row => row.Facts.DisableFailureStatus.State));
  for (const state of ['observed', 'unknown-locate-error', 'unknown-node-mismatch', 'unknown-status-error',
    'unknown-post-locate-error', 'unknown-post-node-mismatch', 'unknown-diagnostic-throw'])
    assert(states.has(state), 'actual current status/UNKNOWN producer coverage ' + state);
  for (const row of rows) {
    const facts = row.Facts;
    assert.equal(row.Result, 6); assert.equal(facts.Schema, 1); assert.equal(facts.Result, 6);
    assert(['admission', 'prepare', 'prepare-locate', 'revalidate', 'current-locate', 'current-authority', 'disable', 'enable'].includes(facts.Stage));
    const wire = 'YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ' + JSON.stringify(facts) + '\r\n';
    assert(Buffer.byteLength(wire) <= 2048, row.Id + ' current refresh byte cap');
    refreshProducerBindings.push({id: row.Id, facts, bytes: Buffer.byteLength(wire), sha256: hash(wire), wire,
      exitCode: 6, refresh: true, originalProducerUtf8Bytes: row.Utf8Bytes,
      replay: 'actual completed current-status C# Facts, property-order reserialization, original native C++ private child; not product C# execution'});
  }
  producerCoverage.currentStatusRecords = statusRows.length;
  producerCoverage.currentRefreshRecords = rows.length;
  producerCoverage.legacyHidRefreshMatrix = 'NOT_RUN_CURRENT_REFRESH_ONLY';
} else if (refreshProducerResults) {
  const document = JSON.parse(before.get(refreshProducerResults).toString('utf8'));
  assert.equal(document.passed, true, 'refresh producer is a completed passing test');
  assert(Array.isArray(document.failures) && document.failures.length === 0, 'refresh producer contains no failures');
  assert.equal(document.realHardwareAccess, false, 'refresh producer did not access hardware');
  assert.equal(document.productMainExecuted, false, 'refresh producer did not execute product Main');
  const rows = document.results.filter(row => row.diagnostic?.Kind === 'ymcc.gameRefresh.failure');
  assert(rows.length > 0, 'actual Result6 producer records required when requested');
  for (const row of rows) {
    const facts = row.diagnostic;
    assert.equal(row.result, 6); assert.equal(row.expected, 6);
    assert.equal(facts.Schema, 1); assert.equal(facts.Result, 6);
    assert(['disable', 'enable'].includes(facts.Stage));
    const recordWire = 'YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ' + JSON.stringify(facts) + '\r\n';
    assert(Buffer.byteLength(recordWire) <= 2048, row.scenario.Name + ' refresh byte cap');
    refreshProducerBindings.push({id: row.scenario.Name, facts, bytes: Buffer.byteLength(recordWire),
      sha256: hash(recordWire), wire: recordWire, exitCode: 6, refresh: true,
      replay: 'actual completed C# Result6 diagnostic, original field order reserialized, C++ child exits with result6; not product C# execution'});
  }
  for (const xusb of [false, true]) for (const fault of ['disable-result', 'disable-throw', 'enable-result', 'enable-throw'])
    assert(rows.some(row => row.scenario.Xusb === xusb && row.scenario.Fault === fault), 'refresh producer HID/XUSB fault coverage ' + fault);
  if (rows.some(row => Object.hasOwn(row.diagnostic, 'CompensationChecked'))) {
    assert(rows.length > 32, 'compensation replay extends the original refresh corpus without a fixed record count');
    const productScoped = document.productionCompensationScope === 'rog-pid1b4c-exact-owned-node';
    if (productScoped) assert(rows.every(row => !row.diagnostic.CompensationChecked || row.scenario.Family === 'rog'), 'no new physical cleanup writes for unproven OEM routes');
    for (const xusb of [false, true]) {
      const selected = rows.filter(row => row.scenario.Xusb === xusb);
      if (productScoped && !xusb) {
        assert(selected.every(row => !row.diagnostic.CompensationChecked && !row.diagnostic.CompensationAttempted), 'quarantined ROG HID is absent; other OEM HID writes remain unchanged');
        continue;
      }
      assert(selected.some(row => row.diagnostic.CompensationAttempted && row.diagnostic.CompensationEnableResult === 0), 'successful compensation keeps failure6');
      assert(selected.some(row => row.diagnostic.CompensationAttempted && row.diagnostic.CompensationEnableResult > 0), 'nonzero compensation retained');
      assert(selected.some(row => row.diagnostic.CompensationAttempted && typeof row.diagnostic.CompensationExceptionType === 'string'), 'compensation exception retained');
      for (const fault of ['enable-identity-retired', 'enable-node-replaced', 'enable-location-changed', 'enable-parent-gone-after-disable'])
        assert(selected.some(row => row.scenario.Fault === fault), 'compensation producer semantic coverage ' + fault);
    }
  }
}
const combinedProducerBindings = [...producerBindings.map(row => ({...row, exitCode: 8, refresh: false})), ...refreshProducerBindings];
const producerCpp = combinedProducerBindings.map((row, index) => 'static const unsigned char producerWire' + index + '[] = {' + [...Buffer.from(row.wire)].join(',') + '};').join('\n') +
  '\nstruct ProducerReplay { std::string id, bytes; DWORD exitCode; bool refresh; };\nstatic const std::vector<ProducerReplay> producerReplays = {\n' +
  combinedProducerBindings.map((row, index) => '{' + JSON.stringify(row.id) + ', std::string(reinterpret_cast<const char*>(producerWire' + index + '), sizeof(producerWire' + index + ')), ' + row.exitCode + ', ' + row.refresh + '}').join(',\n') + '\n};\n';
write(path.join(output, 'producer-replay.actual.h'), producerCpp);
write(path.join(output, 'producer-replay-binding.json'), JSON.stringify({producerResults, producerResultsSha256: hash(before.get(producerResults)), records: producerBindings, refreshProducerResults, refreshProducerResultsSha256: refreshProducerResults ? hash(before.get(refreshProducerResults)) : null, refreshRecords: refreshProducerBindings}, null, 2) + '\n');

for (const file of [header, runtime, fixture, ...trackingHeaders, fileURLToPath(import.meta.url)]) write(path.join(output, 'tested-source', path.relative(repo, file)), before.get(file));
write(path.join(output, 'source-checks.json'), JSON.stringify(assertions, null, 2) + '\n');
const sourceBinding = {schema: 1, producerMode, producerCoverage, directProductionHeaderInclude: true, inputs: binding,
  extractedRuntimeSpans: extracted.map(({text, ...entry}) => entry), cycleFunction: (({text, ...entry}) => entry)(cycle),
  optionalCaptureExactSpanSha256: hash(optional), generatedConsumerSha256: hash(consumer), producerReplayHeaderSha256: hash(producerCpp), producerReplayIds: combinedProducerBindings.map(row => row.id),
  substitutions: [], inertDependencies: ['GPD tracked-writer branch is not executed; its path/machine/evidence endpoints throw and their total calls must stay zero', 'nativeForeground Profile/name', 'gameNodeRefreshMutationAllowed is an explicitly inert true transport leaf; production mutation policy NOT exercised', 'device data type declarations', 'W2U for fixture strings', 'ordinary lifecycle memory sink', 'Cycle image selection limited to fixture/nonexistent own path', 'Cycle hash function inert', 'Cycle CreateProcessW forwards real Win32 except explicit failure injection'],
  mutationPolicy: {entry: 'ymcc::nativeForeground::gameNodeRefreshMutationAllowed', fixtureValue: true, inert: true, productionPolicyExecuted: false, actualPolicyOracle: 'separate full native TU owned by main task'},
  boundary: 'Capture and all Win32 process, standard handle, spool and wait APIs are real; mutation policy is inert true and untested here; no device/product/Steam/driver operation is linked.'};
write(path.join(output, 'source-binding.json'), JSON.stringify(sourceBinding, null, 2) + '\n');
const audit = spawnSync(process.execPath, [path.join(repo, 'tools/steam-integration-audit.mjs'), '--json'],
  {cwd: repo, encoding: 'utf8', windowsHide: true, timeout: 30000});
write(path.join(output, 'steam-integration-audit.stdout.json'), audit.stdout || '');
write(path.join(output, 'steam-integration-audit.stderr.txt'), audit.stderr || '');
let vcvars = option('--vcvars', '');
if (!vcvars) {
  const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
  const vs = spawnSync(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
    {encoding: 'utf8', windowsHide: true, timeout: 10000});
  assert.equal(vs.status, 0, 'MSVC discovery');
  vcvars = path.join(vs.stdout.trim(), 'VC/Auxiliary/Build/vcvars64.bat');
}
assert(fs.existsSync(vcvars), 'vcvars64.bat must exist');
function unchanged() { return inputs.map(file => ({path: file, unchanged: hash(fs.readFileSync(file)) === hash(before.get(file))})); }
const variants = [];
for (const variant of ['Release', 'Checked']) {
  const directory = path.join(output, variant);
  fs.mkdirSync(directory, {recursive: true});
  const executable = path.join(directory, 'one_shot_helper_diagnostic.fixture.exe');
  const switches = variant === 'Release' ? '/O2 /DNDEBUG /MT' : `/Od /RTC1 /Zi /MTd /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /Fd"${path.join(directory, 'fixture.compile.pdb')}"`;
  const batch = calibrationToolchainPreamble(vcvars) + 'chcp 65001 >nul\r\n' +
    `cl /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /Zc:__cplusplus ${switches} ` +
    `/I"${path.join(repo, 'native')}" /I"${path.join(repo, 'deps/json')}" /I"${output}" "${fixture}" ` +
    `/Fo"${path.join(directory, 'fixture.obj')}" /Fe"${executable}" /link /INCREMENTAL:NO ${variant === 'Checked' ? '/DEBUG' : ''}\r\nexit /b %errorlevel%\r\n`;
  const batchPath = path.join(directory, 'compile.cmd');
  write(batchPath, batch);
  console.log(`${variant}: compiling unchanged production capture + exact runtime consumers with MSVC x64`);
  const build = spawnSync('cmd.exe', ['/d', '/c', batchPath], {cwd: directory, encoding: 'utf8', windowsHide: true, timeout: 90000});
  write(path.join(directory, 'build.log'), (build.stdout || '') + (build.stderr || ''));
  const entry = {variant, buildExitCode: build.status, buildError: build.error?.message || null};
  if (build.status !== 0) {
    console.log((build.stdout || '') + (build.stderr || ''));
    variants.push(entry);
    continue;
  }
  entry.executableSha256 = hash(fs.readFileSync(executable));
  console.log(`${variant}: running private child matrix and ${rounds} resource rounds`);
  const run = spawnSync(executable, ['--parent', output, variant, String(rounds)],
    {cwd: directory, encoding: 'utf8', windowsHide: true, timeout: 150000, maxBuffer: 16 * 1024 * 1024});
  write(path.join(directory, 'results.json'), run.stdout || '');
  write(path.join(directory, 'stderr.txt'), run.stderr || '');
  entry.runExitCode = run.status;
  entry.runError = run.error?.message || null;
  try {
    const report = JSON.parse(run.stdout);
    Object.assign(entry, {success: report.success, assertions: report.assertions, failures: report.failures,
      fatal: report.fatal, privateChildProcesses: report.privateChildProcesses,
      privateDirectoryRemoved: report.privateDirectoryRemoved, readsWhileAlive: report.readsWhileAlive,
      scenarioCount: report.cases.length, failedCases: report.cases.filter(test => !test.passed),
      failedChecks: report.checks.filter(test => !test.passed)});
  } catch (error) { entry.parseError = error.message; }
  entry.sourcesStableAfterRun = unchanged().every(item => item.unchanged);
  variants.push(entry);
  console.log(`${variant}: assertions=${entry.assertions ?? '?'} failures=${entry.failures ?? '?'} children=${entry.privateChildProcesses ?? '?'} fatal=${JSON.stringify(entry.fatal ?? null)}`);
}
const after = unchanged();
const sourceStable = after.every(item => item.unchanged);
const success = sourceStable && assertions.every(item => item.passed) && variants.length === 2 &&
  variants.every(item => item.buildExitCode === 0 && item.runExitCode === 0 && item.success);
const result = {schema: 1, success, sourceStable, producerMode, producerCoverage, sourceChecks: assertions, sourceBinding: 'source-binding.json',
  auditExitCode: audit.status, variants, sourceAfter: after,
  boundary: {privateFixtureOnly: true, productBuild: false, productOrHostStarted: false, deviceAccess: false,
    steamAccess: false, driverAccess: false, packagesOrDeployment: false, recursiveDelete: false,
    mutationPolicyInertTrue: true, productionMutationPolicyTested: false},
  changedSourcePaths: ['tools/one_shot_helper_diagnostic_selftest.mjs', 'tools/fixtures/one_shot_helper_diagnostic/fixture.cpp']};
write(path.join(output, 'validation.json'), JSON.stringify(result, null, 2) + '\n');
const findings = variants.flatMap(item => (item.failedCases || []).map(test => `- ${item.variant}: ${test.name}: expected ${test.expected}; actual ${JSON.stringify(test.actual)}`));
const lines = ['# R114/R115 native helper diagnostic private Windows validation', '',
  `Result: ${success ? 'PASS' : 'FAIL / findings retained'}. Production source stable throughout: ${sourceStable}.`, '',
  ...variants.map(item => `- ${item.variant}: build=${item.buildExitCode}, run=${item.runExitCode}, assertions=${item.assertions}, failures=${item.failures}, private children=${item.privateChildProcesses}, own temporary directory removed=${item.privateDirectoryRemoved}.`), '',
  'Boundary: fixture executable starts only itself. No device, product/Host, Steam or driver operation; no deployment or sleep. Runtime dependencies and lifecycle sink are inert. gameNodeRefreshMutationAllowed is an explicit inert true leaf; production policy admission is not tested here and belongs to the separate full native TU oracle. Actual production capture, Win32 process/spool APIs and exact extracted Settle/poll/ordinary receipt execute.', '',
  'Checked uses /Od /RTC1 /Zi /MTd /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2; Release uses /O2 /DNDEBUG /MT. Both MSVC x64 C++20.', '',
  'Missing-image control: records per-process handle counts across four CreateProcess failures with no capture/spool. This isolates any first-call platform initialization from repeated capture cleanup; 16 prepared failed launches and the resource matrix must remain flat afterward.', '',
  'Only newly named private temporary directories are created. Spools are removed solely by production DELETE_ON_CLOSE; only the resulting empty own directory is removed. No recursive deletion.', '',
  'Read validation.json, source-binding.json, per-variant results.json/build.log and tested-source/ for full evidence.', '',
  '## Findings', '', ...findings, '', '## Changed paths', '', ...result.changedSourcePaths.map(item => '- ' + item), ''];
write(path.join(output, 'REPORT.md'), lines.join('\n'));
console.log(JSON.stringify({success, sourceStable, output, variants: variants.map(item => ({variant: item.variant,
  build: item.buildExitCode, run: item.runExitCode, assertions: item.assertions, failures: item.failures,
  failedCases: (item.failedCases || []).map(test => test.name), fatal: item.fatal}))}, null, 2));
process.exitCode = success ? 0 : 1;
