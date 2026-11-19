import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

import {prepareOwnedReturnSources} from './fixtures/msi_owned_return_store/prepare_base.mjs';

if(process.argv.slice(2).includes('--real-store')) {
 const {runRealStoreFromArgs}=await import('./fixtures/msi_owned_return_store/run.mjs');
 await runRealStoreFromArgs({repo:path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args:process.argv.slice(2)});
} else {
// Offline only: freeze production bytes, extract declarations/pure debt helpers,
// and execute the real runtime with in-memory boundaries. No main.cpp build.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
let output = 'G:/YeManCC-Work/Build/Validation/MSI-Field143454-R108-20261016/return-runtime';
let selectedProfiles = ['Release', 'Checked'];
let profileSelected = false;
for (let i = 0; i < args.length; ++i) {
  if (args[i] === '--out' && args[i + 1]) output = args[++i];
  else if (args[i] === '--profile' && !profileSelected && ['Release', 'Checked'].includes(args[i + 1])) {
    selectedProfiles = [args[++i]]; profileSelected = true;
  }
  else if (args.length === 1 && !args[i].startsWith('--')) output = args[i];
  else throw new Error('usage: node tools/verify-msi-owned-return-runtime.mjs [--out directory] [--profile Release|Checked]');
}
const prepared=prepareOwnedReturnSources({repo,output,selectedProfiles});
output=prepared.output;
const {run,snapshot,sha,save,sources,frozen,receipt}=prepared;
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
for (const profile of selectedProfiles) {
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
    break; // A failed prerequisite must not start the next profile.
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
  if (!result.profiles[profile].passed) break;
}
result.sourceChangedDuringRun = sources.filter(name => sha(fs.readFileSync(path.join(repo, name))) !== sha(frozen.get(name)));
result.selectedProfiles = selectedProfiles;
result.passed = Object.keys(result.profiles).length === selectedProfiles.length && selectedProfiles.every(name => result.profiles[name]?.passed) && result.sourceChangedDuringRun.length === 0;
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


}
