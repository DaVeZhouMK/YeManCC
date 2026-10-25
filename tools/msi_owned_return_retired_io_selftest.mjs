import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { extract, replaceOnce, sha } from './fixtures/msi_owned_return_retired_io/source-binding.mjs';
import { writeReport } from './fixtures/msi_owned_return_retired_io/report.mjs';

// Validation sidecar only. Never launches the production app or any device API.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const fixtures = path.join(repo, 'tools/fixtures/msi_owned_return_retired_io');
const fixedOutputRoot = 'G:/YeManCC-Work/Build/Validation/MSI-PostExport-R109-20261016/retired-slot-fixed';
const defaultR108Root = 'G:/YeManCC-Work/Build/Validation/MSI-Field143454-R108-20261016/return-runtime/2026-10-16T07-09-44-392Z/snapshot';
const options = { variant: 'both', out: fixedOutputRoot, r108Root: defaultR108Root, currentRoot: repo };
const args = process.argv.slice(2);
for (let i = 0; i < args.length; ++i) {
  const keys = { '--variant': 'variant', '--out': 'out', '--r108-root': 'r108Root', '--current-root': 'currentRoot' };
  if (args[i] === '--help') {
    console.log('node tools/msi_owned_return_retired_io_selftest.mjs [--variant both|r108|current] [--r108-root path] [--current-root path] [--out path-under-retired-slot-fixed]');
    process.exit(0);
  }
  assert(keys[args[i]] && args[i + 1], 'Unknown argument / missing value: ' + args[i]);
  options[keys[args[i]]] = args[++i];
}
assert(['both', 'r108', 'current'].includes(options.variant));
const output = path.resolve(options.out);
const relativeOutput = path.relative(path.resolve(fixedOutputRoot), output);
assert(!relativeOutput.startsWith('..') && !path.isAbsolute(relativeOutput), 'All output must remain under retired-slot-fixed');
assert(!output.toLowerCase().startsWith(repo.toLowerCase() + path.sep), 'No generated files in source repository');
const run = path.join(output, new Date().toISOString().replace(/[:.]/g, '-'));
fs.mkdirSync(run, { recursive: true });
const logs = path.join(run, 'logs'); fs.mkdirSync(logs);
const save = (name, value) => fs.writeFileSync(path.join(run, name), JSON.stringify(value, null, 2) + '\n');
function command(label, exe, argv, cwd = run) {
  const result = spawnSync(exe, argv, { cwd, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 16 * 1024 * 1024 });
  fs.writeFileSync(path.join(logs, label + '.stdout.txt'), result.stdout ?? '');
  fs.writeFileSync(path.join(logs, label + '.stderr.txt'), result.stderr ?? '');
  fs.writeFileSync(path.join(logs, label + '.command.json'), JSON.stringify({ exe, args: argv, cwd, status: result.status, signal: result.signal, error: result.error ? String(result.error) : null }, null, 2) + '\n');
  return result;
}
const audit = command('steam-integration-audit', 'node', ['tools/steam-integration-audit.mjs'], repo);
assert.equal(audit.status, 0, 'Read-only Steam integration audit failed');
command('git-status-before', 'git', ['status', '--short'], repo);
const baselineName = 'tools/msi_owned_return_runtime_selftest.cpp';
// Use only the frozen original boundary fixture; parent current general suite
// can change independently. All runtime/IO definitions come from each source.
const baselineBytes = fs.readFileSync(path.join(defaultR108Root, baselineName));
const fileNames = ['native/main.cpp', 'native/msi_owned_return_runtime.h', 'native/msi_owned_return_recovery.h',
  'native/msi_crash_recovery_journal.h', 'native/xinput_identity_refresh_policy.h', 'deps/json/json.hpp'];
const fixtureNames = ['r108-review.cpp', 'current-review.cpp', 'inert_win32.inc', 'source-binding.mjs', 'report.mjs', 'rebound_inert.inc'];
const receipt = { createdAt: new Date().toISOString(), run, options, passed: false, variants: {},
  fixtureHashes: Object.fromEntries(fixtureNames.map(n => [n, sha(fs.readFileSync(path.join(fixtures, n)))])),
  runnerSha256: sha(fs.readFileSync(fileURLToPath(import.meta.url))),
  boundarySource: { root: defaultR108Root, name: baselineName, sha256: sha(baselineBytes) },
  safety: { allDeviceIoInert: true, productionWritten: false, parentTestsWritten: false, oldEvidenceWritten: false,
    currentMode2ResumeCalls: false, realCurrentCommandReadyRequired: true } };
const selected = options.variant === 'both' ? ['r108', 'current'] : [options.variant];
for (const variant of selected) {
  const root = path.resolve(variant === 'r108' ? options.r108Root : options.currentRoot);
  const base = path.join(run, variant), snapshot = path.join(base, 'snapshot');
  fs.mkdirSync(path.join(snapshot, 'tools'), { recursive: true });
  const bytes = new Map(fileNames.map(n => [n, fs.readFileSync(path.join(root, n))]));
  if (variant === 'r108') assert.equal(sha(bytes.get('native/msi_owned_return_runtime.h')), 'abb44867f56184b32efaedb98bb0309c1c557b437a69f1ed6f6606f5e1bb3d8f', 'Frozen R108 header pin changed');
  for (const [name, data] of bytes) {
    const dest = path.join(snapshot, name); fs.mkdirSync(path.dirname(dest), { recursive: true }); fs.writeFileSync(dest, data);
  }
  fs.writeFileSync(path.join(snapshot, baselineName), baselineBytes);
  const main = bytes.get('native/main.cpp').toString('utf8'), ranges = [];
  const take = (kind, name) => extract(main, kind, name, 'native/main.cpp', ranges);
  const types = [['struct', 'InputCaptureGamepadSnapshot'], ['struct', 'MsiClawHidDiag'], ['enum', 'MsiClawIoState'],
    ['struct', 'MsiClawWriteResult'], ['struct', 'MsiClawCommandOutcome'], ['function', 'msiClawIoStateName']].map(([k, n]) => take(k, n)).join('\n\n');
  const debt = [['enum', 'MsiReturnDebtBasis'], ['function', 'msiReturnDebtBasisName'], ['function', 'msiReturnDebtDischarge'],
    ['function', 'msiDinputTryDischargeReturnDebt'], ['function', 'xinputNonVirtualConnectedMask'], ['function', 'xinputPhysicalConnectedMask']].map(([k, n]) => take(k, n)).join('\n\n');
  const slot = take('struct', 'MsiClawIoSlot');
  const free = take('function', 'msiClawIoSlotFreeLocked'), reap = take('function', 'msiClawIoSlotReapLocked');
  const ready = variant === 'current' ? take('function', 'msiOwnedReturnCommandReady') : '';
  const writer = take('function', 'msiClawCommandWriteBounded'), modeWriter = take('function', 'msiClawSwitchModeOnHandle');
  const runtime = bytes.get('native/msi_owned_return_runtime.h').toString('utf8');
  for (const name of ['msiOwnedReturnRetryable', 'msiOwnedReturnRecordInitial', 'msiOwnedReturnMaintenance', 'msiOwnedReturnContext', 'msiOwnedReturnRecordCurrent'])
    extract(runtime, 'function', name, 'native/msi_owned_return_runtime.h', ranges);
  if (variant === 'current') {
    assert(runtime.includes('if(!msiOwnedReturnCommandReady())'), 'Current runtime is missing real readiness gate');
    const maintenance = extract(runtime, 'function', 'msiOwnedReturnMaintenance', 'native/msi_owned_return_runtime.h', []);
    assert(maintenance.indexOf('msiOwnedReturnCommandReady()') < maintenance.indexOf('msiClawOpenModeCommand('));
    assert(maintenance.indexOf('msiOwnedReturnCommandReady()') < maintenance.indexOf('.claimRetry('));
  }
  const policy = bytes.get('native/msi_owned_return_recovery.h').toString('utf8');
  for (const name of ['begin', 'claimRetry']) extract(policy, 'method', name, 'native/msi_owned_return_recovery.h', ranges);
  fs.writeFileSync(path.join(snapshot, 'tools/production_types.h'), '#pragma once\n' + types + '\n');
  fs.writeFileSync(path.join(snapshot, 'tools/production_debt_helpers.h'), '#pragma once\n' + debt + '\n');
  fs.writeFileSync(path.join(snapshot, 'tools/offline_command_io.h'), fs.readFileSync(path.join(fixtures, 'inert_win32.inc'), 'utf8') +
    '\nstatic constexpr DWORD kMsiClawWriteWaitMs=2000;\nstatic constexpr DWORD kMsiClawWriteReapMs=500;\n' + slot + '\nstatic MsiClawIoSlot g_msiClawIoSlot;\n' +
    [free, reap, ready, writer].join('\n\n') + '\n#define msiClawSwitchModeOnHandle actualMsiClawSwitchModeOnHandle\n' + modeWriter + '\n#undef msiClawSwitchModeOnHandle\n');
  let test = baselineBytes.toString('utf8'); const edits = [];
  // The archived boundary remains immutable. Adapt only its inert sink ABI to
  // the selected production discharge contract; the real debt helper is intact.
  if (variant === 'current' && debt.includes('if(!msiCrashRecoveryAfterModeDischarge())')) {
    const sink=test.match(/static void msiCrashRecoveryAfterModeDischarge\(\) \{[^}]*\}/);
    assert(sink,'Missing archived in-memory discharge sink');
    const adapted=sink[0].replace('static void ','static bool ').replace(/\}$/, '    return true;\n}');
    test=replaceOnce(test,sink[0],adapted,edits);
  }
  if (variant === 'current' && runtime.includes('g_msiTerminalReturnPending')) {
    const cancel='static void msiTerminalReturnCancel(const char*) { ++g_count.terminalCancels; }';
    test=replaceOnce(test,cancel,'static std::atomic<bool> g_msiTerminalReturnPending{false};\n'+cancel,edits);
  }

  if (variant === 'current' && runtime.includes('msiOwnedReturnQuiesceReboundDinput')) {
    const runtimeInclude='#include "../native/msi_owned_return_runtime.h"';
    const seam=fs.readFileSync(path.join(fixtures,'rebound_inert.inc'),'utf8');
    test=replaceOnce(test,runtimeInclude,seam+'\n'+runtimeInclude,edits);
  }

  if (variant === 'current' && runtime.includes('g_inputHostVirtualIntentRevision')) {
    const revisionBoundary='static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{0},g_msiMainSourceRevision{0};\nstatic std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};\n';
    const runtimeInclude='#include "../native/msi_owned_return_runtime.h"';
    test=replaceOnce(test,runtimeInclude,revisionBoundary+runtimeInclude,edits);
  }

  const replacements = [
    ['using HANDLE = std::uintptr_t;', 'using HANDLE = void*;'],
    ['constexpr HANDLE INVALID_HANDLE_VALUE = (std::numeric_limits<HANDLE>::max)();', 'static HANDLE const INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(~std::uintptr_t(0));'],
    ['static HANDLE GetCurrentProcess() { return 7; }', 'static HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(std::uintptr_t(7)); }'],
    ['static HANDLE g_nextHandle = 1000;', 'static std::uintptr_t g_nextHandle = 1000;'],
    ['const auto handle = ++g_nextHandle;', 'const auto handle = reinterpret_cast<HANDLE>(++g_nextHandle);'],
    ['g_writtenHandles.front() != 0x55', 'g_writtenHandles.front() != reinterpret_cast<HANDLE>(std::uintptr_t(0x55))'],
  ];
  for (const [a, b] of replacements) test = replaceOnce(test, a, b, edits);
  assert.equal(test.split('g_inputHostProcess = 55;').length, 3);
  test = test.replaceAll('g_inputHostProcess = 55;', 'g_inputHostProcess = reinterpret_cast<HANDLE>(std::uintptr_t(55));');
  edits.push({ before: 'g_inputHostProcess = 55;', after: 'g_inputHostProcess = reinterpret_cast<HANDLE>(std::uintptr_t(55));', count: 2 });
  if (variant === 'current' && modeWriter.includes('contextCurrent')) {
    const oldSignature='static MsiClawCommandOutcome msiClawSwitchModeOnHandle(BYTE mode, HANDLE handle, const MsiClawHidDiag& diag, bool ownedReturnRetry = false) {';
    const newSignature=oldSignature.replace('bool ownedReturnRetry = false)', 'bool ownedReturnRetry = false, const std::function<bool()>& contextCurrent = {})');
    test=replaceOnce(test,oldSignature,newSignature,edits);
  }
  const marker = variant === 'current' && modeWriter.includes('contextCurrent')
    ? 'static MsiClawCommandOutcome msiClawSwitchModeOnHandle(BYTE mode, HANDLE handle, const MsiClawHidDiag& diag, bool ownedReturnRetry = false, const std::function<bool()>& contextCurrent = {}) {'
    : 'static MsiClawCommandOutcome msiClawSwitchModeOnHandle(BYTE mode, HANDLE handle, const MsiClawHidDiag& diag, bool ownedReturnRetry = false) {';
  test = replaceOnce(test, marker, '#include "offline_command_io.h"\n' + marker, edits);
  test = replaceOnce(test, '    auto outcome = g_retryOutcome;', `    if (g_executeActualWriter) {\n        auto actual = actualMsiClawSwitchModeOnHandle(mode, handle, diag, ownedReturnRetry${variant === 'current' && modeWriter.includes('contextCurrent') ? ',contextCurrent' : ''});\n        if (actual.write.attempted) ++g_count.physicalWriteAttempts;\n        g_lastActualOutcome = actual;\n        return actual;\n    }\n    auto outcome = g_retryOutcome;`, edits);
  fs.writeFileSync(path.join(snapshot, 'tools/runtime_boundary_copy.cpp'), test);
  const originalReview = fs.readFileSync(path.join(fixtures, 'r108-review.cpp'), 'utf8');
  const mainAt = originalReview.indexOf('int main(){'); assert(mainAt > 0);
  fs.writeFileSync(path.join(snapshot, 'tools/review_helpers.h'), originalReview.slice(0, mainAt));
  const code = fs.readFileSync(path.join(fixtures, variant === 'r108' ? 'r108-review.cpp' : 'current-review.cpp'));
  const source = path.join(base, 'fixture.cpp'); fs.writeFileSync(source, code);
  const binding = { variant, root, sourceHashes: Object.fromEntries([...bytes].map(([n, b]) => [n, sha(b)])),
    extractedDefinitions: ranges, boundaryCopyEdits: edits, boundarySource: receipt.boundarySource,
    compiledRuntimeUnmodified: true, readyHighLevelStubbed: false, actualWriterMacroRenameOnly: true,
    fixtureSha256: sha(code), inertSeamSha256: receipt.fixtureHashes['inert_win32.inc'] };
  fs.writeFileSync(path.join(base, 'source-binding.json'), JSON.stringify(binding, null, 2) + '\n');
  const profiles = {};
  for (const profile of ['Release', 'Checked']) {
    const dir = path.join(base, profile); fs.mkdirSync(dir);
    const exe = path.join(dir, 'retired-slot-selftest.exe');
    const quote = s => { assert(!/["\r\n%]/.test(s)); return '"' + s + '"'; };
    const vcvars = process.env.YMCC_VCVARS64 ?? 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
    assert(fs.existsSync(vcvars), 'MSVC x64 not found');
    const flags = ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/permissive-', '/sdl', '/Zc:__cplusplus',
      ...(profile === 'Release' ? ['/O2', '/MT', '/DNDEBUG'] : ['/Od', '/MTd', '/RTC1', '/Zi', '/D_DEBUG']),
      '/external:I' + quote(path.join(snapshot, 'deps/json')), '/external:W0', '/I' + quote(path.join(snapshot, 'tools')), quote(source),
      '/Fo' + quote(path.join(dir, 'fixture.obj')), '/Fe' + quote(exe), '/Fd' + quote(path.join(dir, 'fixture.pdb')),
      '/link', '/SUBSYSTEM:CONSOLE', '/MACHINE:x64', '/INCREMENTAL:NO'];
    const batch = path.join(dir, 'build.cmd');
    fs.writeFileSync(batch, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\ncl ' + flags.join(' ') + '\r\nexit /b %errorlevel%\r\n');
    console.log(variant + '/' + profile + ': compiling actual production IO/runtime');
    const build = command(variant + '-' + profile + '-build', 'cmd.exe', ['/d', '/c', batch], dir);
    if (build.status !== 0) { profiles[profile] = { buildPassed: false, passed: false }; console.error(build.stdout, build.stderr); continue; }
    const execute = command(variant + '-' + profile + '-execute', exe, [], dir);
    let report; try { report = JSON.parse(execute.stdout); } catch { report = { passed: false, parseError: true, stdout: execute.stdout }; }
    fs.writeFileSync(path.join(dir, 'results.json'), JSON.stringify(report, null, 2) + '\n');
    profiles[profile] = { buildPassed: true, passed: execute.status === 0 && report.passed === true, verdict: report.verdict,
      cases: report.cases?.length, assertions: report.assertions, baselineCases: report.baseline?.tests ?? 0,
      baselineAssertions: report.baseline?.assertions ?? 0, failures: report.failures, exeSha256: sha(fs.readFileSync(exe)) };
    console.log(variant + '/' + profile + ': ' + JSON.stringify(profiles[profile]));
  }
  const changed = fileNames.filter(n => sha(fs.readFileSync(path.join(root, n))) !== sha(bytes.get(n)));
  const relevantChanges = changed.filter(n => n !== 'native/main.cpp' || ranges.filter(r => r.file === n).some(r => {
    try { return extract(fs.readFileSync(path.join(root, n), 'utf8'), r.kind, r.symbol, n, []).length === 0 ||
      sha(extract(fs.readFileSync(path.join(root, n), 'utf8'), r.kind, r.symbol, n, [])) !== r.sha256; } catch { return true; }
  }));
  receipt.variants[variant] = { root, profiles, sourceHashes: binding.sourceHashes, sourceChangedDuringRun: changed,
    relevantSourceChangedDuringRun: relevantChanges, bindingPath: path.join(base, 'source-binding.json') };
}
receipt.passed = Object.values(receipt.variants).every(v => Object.values(v.profiles).length === 2 && Object.values(v.profiles).every(p => p.passed) && v.relevantSourceChangedDuringRun.length === 0);
receipt.verdict = receipt.passed ? 'SELECTED_VARIANTS_VALIDATED' : 'VALIDATION_INCOMPLETE';
save('RESULT.json', receipt);
command('git-status-after', 'git', ['status', '--short'], repo);
writeReport(run, receipt);
fs.writeFileSync(path.join(output, 'LATEST.json'), JSON.stringify(receipt, null, 2) + '\n');
fs.copyFileSync(path.join(run, 'REPORT.md'), path.join(output, 'REPORT.md'));
console.log('REPORT: ' + path.join(run, 'REPORT.md'));
process.exitCode = receipt.passed ? 0 : 1;