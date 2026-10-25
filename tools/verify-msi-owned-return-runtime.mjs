import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

// Offline only: freeze production bytes, extract declarations/pure debt helpers,
// and execute the real runtime with in-memory boundaries. No main.cpp build.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
let output = 'G:/YeManCC-Work/Build/Validation/MSI-Field143454-R108-20261016/return-runtime';
for (let i = 0; i < args.length; ++i) {
  if (args[i] === '--out' && args[i + 1]) output = args[++i];
  else if (args.length === 1 && !args[i].startsWith('--')) output = args[i];
  else throw new Error('usage: node tools/verify-msi-owned-return-runtime.mjs [--out directory]');
}
output = path.resolve(output);
assert(output.toLowerCase() !== repo.toLowerCase() && !output.toLowerCase().startsWith(repo.toLowerCase() + path.sep), 'Evidence must stay outside the source repository');
fs.mkdirSync(output, { recursive: true });
const run = path.join(output, new Date().toISOString().replace(/[:.]/g, '-'));
fs.mkdirSync(run);
const snapshot = path.join(run, 'snapshot');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const save = (name, value) => fs.writeFileSync(path.join(run, name), JSON.stringify(value, null, 2) + '\n');

// A balanced C++ scanner avoids regex truncation at nested initializers/comments.
function endBrace(text, start) {
  let depth = 0, mode = 'code';
  for (let i = start; i < text.length; ++i) {
    const c = text[i], n = text[i + 1];
    if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
    if (mode === 'block') { if (c === '*' && n === '/') { mode = 'code'; ++i; } continue; }
    if (mode === 'string' || mode === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
      continue;
    }
    if (c === '/' && n === '/') { mode = 'line'; ++i; continue; }
    if (c === '/' && n === '*') { mode = 'block'; ++i; continue; }
    if (c === '"' || c === "'") { mode = c === '"' ? 'string' : 'char'; continue; }
    if (c === '{') ++depth;
    if (c === '}' && --depth === 0) return i + 1;
  }
  throw new Error('Unclosed C++ declaration');
}
const sources = [
  'native/main.cpp',
  'native/virtual_controller_stop_runtime.h',
  'native/msi_owned_return_runtime.h',
  'native/msi_owned_return_recovery.h',
  'native/virtual_output_status.h',
  'native/msi_crash_recovery_journal.h',
  'native/xinput_identity_refresh_policy.h',
  'native/touch_haptic_dispatch.h',
  'deps/json/json.hpp',
  'tools/msi_owned_return_runtime_selftest.cpp',
  'tools/verify-msi-owned-return-runtime.mjs',
];
const frozen = new Map(sources.map(name => [name, fs.readFileSync(path.join(repo, name))]));
for (const [name, bytes] of frozen) {
  const target = path.join(snapshot, name);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, bytes);
}
const ranges = [];
function extract(kind, name, owner = 'native/main.cpp') {
  const main = frozen.get(owner).toString('utf8');
  const prefix = kind === 'function' ? '^static[^\\n;{]*\\b' + name + '\\s*\\('
    : '^' + (kind === 'enum' ? 'enum class' : 'struct') + '\\s+' + name + '\\s*\\{';
  const re = new RegExp(prefix, 'mg');
  let match;
  while ((match = re.exec(main))) {
    const brace = main.indexOf('{', match.index), semicolon = main.indexOf(';', match.index);
    if (brace < 0 || (semicolon >= 0 && semicolon < brace)) continue;
    let end = endBrace(main, brace);
    if (kind !== 'function') { assert.equal(main[end], ';', name); ++end; }
    const code = main.slice(match.index, end);
    ranges.push({ name, source: owner,
      startLine: main.slice(0, match.index).split('\n').length,
      endLine: main.slice(0, end).split('\n').length,
      startUtf8Byte: Buffer.byteLength(main.slice(0, match.index)),
      endUtf8ByteExclusive: Buffer.byteLength(main.slice(0, end)), sha256: sha(code) });
    return code;
  }
  throw new Error('Missing production definition: ' + name);
}
const types = [
  ['struct', 'InputCaptureGamepadSnapshot'], ['struct', 'MsiClawHidDiag'],
  ['enum', 'MsiClawIoState'], ['struct', 'MsiClawWriteResult'],
  ['struct', 'MsiClawCommandOutcome'], ['function', 'msiClawIoStateName'],
  ['struct', 'MsiModeReadbackFacts'], ['struct', 'MsiMode1WritebackVerdict'],
].map(([kind, name]) => extract(kind, name)).join('\n\n');
const debt = [
  ['enum', 'MsiReturnDebtBasis'], ['function', 'msiReturnDebtBasisName'],
  ['function', 'msiReturnDebtDischarge'], ['function', 'msiDinputTryDischargeReturnDebt'],
  ['function', 'msiTerminalReturnMissingTransport'],
  ['function', 'xinputNonVirtualConnectedMask'], ['function', 'xinputPhysicalConnectedMask'],
].map(([kind, name]) => extract(kind, name)).join('\n\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_types.h'), '#pragma once\n#include "../native/touch_haptic_dispatch.h"\n' + types + '\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_debt_helpers.h'), '#pragma once\n' + debt + '\n');
const restore = [
  'msiModeReadbackFacts', 'msiMode1TerminalReason', 'msiMode1WritebackVerdict',
  'msiClawFinishXinputRestore', 'msiClawRestoreXInputMode',
].map(name => extract('function', name)).concat([extract('function', 'inputHostDisabledCleanupProvenLocked',
  'native/virtual_controller_stop_runtime.h')]).join('\n\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_restore_helpers.h'), '#pragma once\n' + restore + '\n');
const test = frozen.get('tools/msi_owned_return_runtime_selftest.cpp').toString('utf8');
const realFunctions = ['msiOwnedReturnRecordInitial', 'msiOwnedReturnMaintenance',
  'msiOwnedReturnReadSignal', 'msiOwnedReturnRetryable', 'msiOwnedReturnRecordCurrent', 'msiOwnedReturnContext',
  'msiOwnedReturnObserveOffIntent', 'msiClawRestoreXInputMode', 'inputHostDisabledCleanupProvenLocked'];
for (const name of realFunctions) {
  assert(test.includes(name + '('), 'Missing direct execution: ' + name);
  assert(!new RegExp('(?:static\\s+)?(?:bool|void|MsiOwnedReturnSignal)\\s+' + name + '\\s*\\(').test(test), 'High-level stub forbidden: ' + name);
}
for (const header of ['msi_owned_return_runtime.h', 'msi_owned_return_recovery.h', 'msi_crash_recovery_journal.h'])
  assert(test.includes('#include "../native/' + header + '"'), 'Missing real include: ' + header);
const stubs = [...test.matchAll(/\/\/ STUB-BEGIN ([^\r\n]+)\r?\n([\s\S]*?)\/\/ STUB-END/g)].map(m => ({
  name: m[1], source: 'tools/msi_owned_return_runtime_selftest.cpp',
  startLine: test.slice(0, m.index).split('\n').length,
  endLine: test.slice(0, m.index + m[0].length).split('\n').length,
  sha256: sha(m[0]),
}));
assert(stubs.length >= 4, 'Boundary ranges must be explicit');
const receipt = {
  mode: 'actual-production-runtime-offline-integration', createdAt: new Date().toISOString(),
  output: run, noInputHost: true, noDeviceIo: true, noSdkDeviceIo: true,
  wholeMainCompiled: false, productionHeadersModifiedByVerifier: false,
  strictProfiles: { Release: ['/O2', '/MT', '/DNDEBUG'], Checked: ['/Od', '/MTd', '/RTC1', '/Zi', '/D_DEBUG'] },
  realFunctions, extractedDefinitions: ranges, stubs,
  sourceHashes: Object.fromEntries([...frozen].map(([name, bytes]) => [name, sha(bytes)])),
  scope: [
    'Real production runtime/recovery/journal/Slot; actual cached structs extracted byte-for-byte from main.cpp.',
    'Real debt-discharge, physical-mask, terminal gate, restore and finish consumers extracted byte-for-byte; durable-after-discharge is an in-memory sink.',
    'Legacy deferred-transport schedule, terminal command transaction and optional diagnostic trace are explicit fake boundaries; the restore gate/terminal suppression/finish and OFF intent remain production code.',
    'Fake WinAPI clock/process/handle types, board/power/Host context, USB parent/location mapping, command open/close/write, logs and durable store.',
    'No InputHost, SDK, SetupAPI, HID, XInput DLL, Steam, sleep, deployment or actual device command is invoked.',
    'Read-window freshness is production cache provenance, not proof of device-internal freshness or Steam consumption.',
  ],
};
save('source-binding.json', receipt);
function command(label, exe, argv, cwd = run, timeout = 60000) {
  const result = spawnSync(exe, argv, { cwd, encoding: 'utf8', timeout, windowsHide: true, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(run, label + '.stdout.txt'), result.stdout ?? '');
  fs.writeFileSync(path.join(run, label + '.stderr.txt'), result.stderr ?? '');
  save(label + '.command.json', { exe, args: argv, cwd, status: result.status, signal: result.signal, error: result.error ? String(result.error) : null });
  return result;
}
function quote(value) {
  assert(!/["\r\n%]/.test(value), 'Unsupported command path');
  return '"' + value + '"';
}
const vcvars = process.env.YMCC_VCVARS64 ?? 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars), 'MSVC x64 not found; set YMCC_VCVARS64');
const result = { run, passed: false, runtimeSha256: receipt.sourceHashes['native/msi_owned_return_runtime.h'], profiles: {}, sourceChangedDuringRun: [] };
for (const profile of ['Release', 'Checked']) {
  const dir = path.join(run, profile);
  fs.mkdirSync(dir);
  const exe = path.join(dir, 'selftest.exe');
  const opts = ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/permissive-', '/sdl', '/Zc:__cplusplus',
    profile === 'Release' ? '/O2' : '/Od', profile === 'Release' ? '/MT' : '/MTd',
    ...(profile === 'Release' ? ['/DNDEBUG'] : ['/RTC1', '/Zi', '/D_DEBUG']),
    '/external:I' + quote(path.join(snapshot, 'deps/json')), '/external:W0',
    quote(path.join(snapshot, 'tools/msi_owned_return_runtime_selftest.cpp')),
    '/Fo' + quote(path.join(dir, 'selftest.obj')), '/Fe' + quote(exe), '/Fd' + quote(path.join(dir, 'selftest.pdb')),
    '/link', '/SUBSYSTEM:CONSOLE', '/MACHINE:x64', '/INCREMENTAL:NO'];
  const batch = path.join(dir, 'build.cmd');
  fs.writeFileSync(batch, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\ncl ' + opts.join(' ') + '\r\nexit /b %errorlevel%\r\n');
  console.log(profile + ': compiling real runtime /W4 /WX /permissive-');
  const build = command(profile + '-build', 'cmd.exe', ['/d', '/c', batch], dir);
  if (build.error || build.status !== 0) {
    result.profiles[profile] = { buildPassed: false, status: build.status };
    console.error(build.stdout, build.stderr);
    continue;
  }
  const execute = command(profile + '-execute', exe, [], dir);
  let report;
  try { report = JSON.parse(execute.stdout); }
  catch { report = { passed: false, parseError: true, stdout: execute.stdout }; }
  save(profile + '-results.json', report);
  result.profiles[profile] = { buildPassed: true, passed: execute.status === 0 && report.passed === true,
    tests: report.tests, assertions: report.assertions, failures: report.failures, exeSha256: sha(fs.readFileSync(exe)) };
  console.log(profile + ': ' + JSON.stringify(result.profiles[profile]));
  if (report.failures?.length) console.error(JSON.stringify(report.failures, null, 2));
}
result.sourceChangedDuringRun = sources.filter(name => sha(fs.readFileSync(path.join(repo, name))) !== sha(frozen.get(name)));
result.passed = Object.keys(result.profiles).length === 2 && Object.values(result.profiles).every(p => p.passed) && result.sourceChangedDuringRun.length === 0;
save('RESULT.json', result);
fs.writeFileSync(path.join(output, 'LATEST.json'), JSON.stringify(result, null, 2) + '\n');
const lines = [
  '# MSI owned-return runtime offline integration', '',
  'Result: ' + (result.passed ? 'PASS' : 'FAIL') + '.', '',
  'Production runtime SHA-256: ' + result.runtimeSha256 + '.', '',
  ...Object.entries(result.profiles).map(([name, p]) => '- ' + name + ': ' + (p.passed ? 'PASS' : 'FAIL') + '; ' + (p.tests ?? 0) + ' cases; ' + (p.assertions ?? 0) + ' assertions.'), '',
  'Production source binding and exact stub/extraction ranges: source-binding.json.',
  'Per-case assertions, captured command/close/discharge counters and lifecycle receipts: Release-results.json / Checked-results.json.',
  'Compiler commands/stdout/stderr and immutable source snapshot are retained in this run.', '',
  ...receipt.scope.map(s => '- ' + s), '',
  'Source changes during verification: ' + (result.sourceChangedDuringRun.join(', ') || 'none') + '.', '',
];
fs.writeFileSync(path.join(run, 'REPORT.md'), lines.join('\n'));
console.log('Evidence: ' + run);
process.exitCode = result.passed ? 0 : 1;

