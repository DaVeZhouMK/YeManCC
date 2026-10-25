import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

// Offline only: freeze production bytes, extract declarations/pure debt helpers,
// and execute the real runtime with in-memory boundaries. No main.cpp build.
let repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const runnerRoot = repo;
const args = process.argv.slice(2);
let output = 'G:/YeManCC-Work/Build/Validation/MSI-StartupReturn-R110-20261016/startup-runtime';
let fixtureDirectory=null,includeRebindSourceFragments=false;
for (let i = 0; i < args.length; ++i) {
  if(args[i]==='--fixture-directory' && args[i+1])fixtureDirectory=path.resolve(args[++i]);
  else if(args[i]==='--include-rebind-source-fragments')includeRebindSourceFragments=true;
  else if (args[i] === '--repo' && args[i + 1]) repo = path.resolve(args[++i]);
  else if (args[i] === '--out' && args[i + 1]) output = args[++i];
  else if (args.length === 1 && !args[i].startsWith('--')) output = args[i];
  else throw new Error('usage: node tools/verify-msi-startup-owned-return-runtime.mjs [--repo source] [--out directory]');
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
  'native/msi_owned_return_runtime.h',
  'native/msi_owned_return_recovery.h',
  'native/virtual_output_status.h',
  'native/msi_crash_recovery_journal.h',
  'native/xinput_identity_refresh_policy.h',
  'native/touch_haptic_dispatch.h',
  'deps/json/json.hpp',
  'tools/msi_owned_return_runtime_selftest.cpp',
  'native/msi_crash_recovery_runtime.h',
];
const frozen = new Map(sources.map(name => [name, fs.readFileSync(path.join(repo, name))]));
for (const [name, bytes] of frozen) {
  const target = path.join(snapshot, name);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, bytes);
}
let main = frozen.get('native/main.cpp').toString('utf8');
const mainOriginal = main;
const ranges = [];
let extractionSource='native/main.cpp';
function extract(kind, name) {
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
    ranges.push({ name, source: extractionSource,
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
  ['function', 'xinputNonVirtualConnectedMask'], ['function', 'xinputPhysicalConnectedMask'],
].map(([kind, name]) => extract(kind, name)).join('\n\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_types.h'), '#pragma once\n#include "../native/touch_haptic_dispatch.h"\n' + types + '\n');
fs.writeFileSync(path.join(snapshot, 'tools/production_debt_helpers.h'), '#pragma once\n' + debt + '\n');

let sourceHelpers='',rebindFragments=null;
if(includeRebindSourceFragments){
const exactPump=extract('function','msiDinputPump');
const openAt=exactPump.indexOf('if (g_msiDinput.pad == nullptr) {');
assert(openAt>=0);
const openEnd=endBrace(exactPump,exactPump.indexOf('{',openAt));
const entryEnd=exactPump.indexOf('    // ── GP-MSIG3E');
assert(entryEnd>0);
const exactEntry=exactPump.slice(exactPump.indexOf('{')+1,entryEnd);
const routeStart=exactPump.indexOf('    const bool routeArmed =');
const routeEnd=exactPump.indexOf('    if (routeInWindow',routeStart);
const routeFacts=exactPump.slice(routeStart,routeEnd);
const exactOpen=exactPump.slice(openAt,openEnd);
const sourceType=extract('struct','HidHideMsiMainSourceRecord');
const actualTarget=extract('function','msiDinputDeviceTargetAbsent').replace('msiDinputDeviceTargetAbsent(','rebindActualDeviceTargetAbsent(');
const actualPublisher=extract('function','msiDinputPublishMainSourceIdentity');
const intentMutation=mainOriginal.includes('struct InputHostVirtualIntentMutation')?extract('struct','InputHostVirtualIntentMutation'):'';
sourceHelpers=sourceType+'\nstatic bool rebindActualDeviceTargetAbsent();\n';
const pumpFragment='static void rebindActualPumpOpenFragment(){'+exactEntry+routeFacts+exactOpen+'\n}\n';
fs.writeFileSync(path.join(snapshot,'tools/rebind_actual.inc'),intentMutation+'\n'+actualTarget+'\n'+actualPublisher+'\n'+pumpFragment);
rebindFragments={source:'native/main.cpp',entry:exactEntry,routeFacts,openBlock:exactOpen,sourceType,actualTarget,actualPublisher,intentMutation,scope:'actual pump entry and complete open/rebind branch; explicit route-disarmed precondition, acquisition/PnP/commands inert; actual target predicate renamed only'};
}
const gates = ['msiModeReadbackFacts','msiMode1TerminalReason','msiMode1WritebackVerdict'].map(name=>extract('function',name)).join('\n\n');
const restore = ['msiClawFinishXinputRestore','msiClawRestoreXInputMode'].map(name=>extract('function',name)).join('\n\n');
const crash = frozen.get('native/msi_crash_recovery_runtime.h').toString('utf8');
const globalStart=crash.indexOf('namespace msiRecovery ='),globalEnd=crash.indexOf('static std::wstring msiCrashRecoveryPath');
assert(globalStart>=0 && globalEnd>globalStart);
const crashGlobals=crash.slice(globalStart,globalEnd).replace('namespace msiRecovery = ymcc::msi::recovery;','');
main=crash;extractionSource='native/msi_crash_recovery_runtime.h';
const crashNames=['msiCrashHasDuty','msiCrashDecision','msiCrashStoreLocked','msiCrashPhysicalXinputSnapshotEligible',
  'msiCrashPrepareOwner','msiCrashRecoveryBeforeModeWrite','msiCrashRecoveryAfterOwnedRestore',
  'msiCrashRecoveryAfterModeDischarge','msiCrashRetireStartupJournal','msiCrashRetryOwnedRecordLoad','msiCrashRecoveryMaintenance'];
if(crash.includes('static bool msiCrashAdoptStartupModeDuty('))crashNames.splice(crashNames.indexOf('msiCrashRecoveryMaintenance'),0,'msiCrashAdoptStartupModeDuty');
const crashBodies=new Map(crashNames.map(name=>[name,extract('function',name)]));
main=mainOriginal;extractionSource='native/main.cpp';
const extractDecl=body=>body.slice(0,body.indexOf('{')).trim().replaceAll('=false','')+';';
const crashDecls=[...crashBodies.values()].map(extractDecl).join('\n');
const realDecls=['static void msiOwnedReturnRecordInitial(const char*,const MsiClawCommandOutcome&);',
  'static bool msiClawRestoreXInputMode(const char*);',
  'static void msiOwnedReturnResumeAfterFailedMode2(const char*);'].join('\n');
const fixtureNames=['boundaries.inc','cases.inc'];
const fixtureBytes=new Map(fixtureNames.map(name=>[name,fs.readFileSync(path.join(fixtureDirectory??path.join(runnerRoot,'tools/fixtures/msi_startup_owned_return'),name))]));
let test = frozen.get('tools/msi_owned_return_runtime_selftest.cpp').toString('utf8').replace(/\r\n/g,'\n');
test=test.replace(/\/\/ STUB-BEGIN Rebound DInput source boundaries[\s\S]*?\/\/ STUB-END/, 'static void resetMsiReboundBoundary(){}');
if(includeRebindSourceFragments){
 test=test.replace('static bool g_targetAbsent = true;',sourceHelpers+'\nstatic bool g_rebindFixtureEnabled=false;\nstatic bool g_targetAbsent = true;');
 test=test.replace('return g_targetAbsent;','return g_rebindFixtureEnabled?rebindActualDeviceTargetAbsent():g_targetAbsent;');
}

function replaceOnce(before,after){assert.equal(test.split(before).length,2,'Fixture seam drift: '+before.slice(0,80));test=test.replace(before,after);}
replaceOnce('#include "json.hpp"', '#include "json.hpp"\n#include "../native/virtual_output_status.h"');
replaceOnce('static msiRecovery::Record g_msiCrashRecord;','');
replaceOnce('static bool g_msiCrashStoreInvalid = false;','');
replaceOnce('static std::mutex g_msiCrashMx, g_gamepadReadMx, g_inputCaptureGamepadSnapshotMx;','static std::mutex g_gamepadReadMx, g_inputCaptureGamepadSnapshotMx;\n'+crashGlobals);
const mockDischarge=test.match(/static (?:bool|void) msiCrashRecoveryAfterModeDischarge\(\) \{[^}]*\}/);
assert(mockDischarge,'Missing original discharge boundary');
replaceOnce(mockDischarge[0],extractDecl(crashBodies.get('msiCrashRecoveryAfterModeDischarge')));
replaceOnce('#include "production_debt_helpers.h"', '#include "startup_boundaries.inc"\n'+realDecls+'\n'+crashDecls+'\n'+gates+'\n'+[...crashBodies].filter(([name])=>!['msiCrashRecoveryMaintenance','msiCrashRetireStartupJournal'].includes(name)).map(([,body])=>body).join('\n\n')+'\n#include "production_debt_helpers.h"');
replaceOnce('int main() {','int ownedRuntimeBaselineMain() {');
replaceOnce('    if (outcome.write.attempted) ++g_count.physicalWriteAttempts;',
  '    if (!msiCrashRecoveryBeforeModeWrite(mode,diag,ownedReturnRetry) || (contextCurrent && !contextCurrent())) { outcome={}; outcome.diag=diag; }\n    if (outcome.write.attempted) ++g_count.physicalWriteAttempts;');
// Forward declaration is required by the inert writer before it includes the actual gate.
replaceOnce('static MsiClawCommandOutcome msiClawSwitchModeOnHandle(',
  'static bool msiCrashRecoveryBeforeModeWrite(BYTE,const MsiClawHidDiag&,bool);\nstatic MsiClawCommandOutcome msiClawSwitchModeOnHandle(');
const append=restore+'\n'+crashBodies.get('msiCrashRetireStartupJournal')+'\n'+crashBodies.get('msiCrashRecoveryMaintenance')+'\n'+fixtureBytes.get('cases.inc').toString('utf8');
fs.writeFileSync(path.join(snapshot,'tools/startup_integration.cpp'),test+'\n'+append);
fs.writeFileSync(path.join(snapshot,'tools/startup_boundaries.inc'),fixtureBytes.get('boundaries.inc'));
fs.writeFileSync(path.join(snapshot,'tools/startup_actual.inc'),append);
const receipt = {mode:'actual-startup-owner-transfer-mode-gates-restore-owned-observer-offline-integration',createdAt:new Date().toISOString(),
  output:run,noInputHost:true,noDeviceIo:true,noSdkDeviceIo:true,wholeMainCompiled:false,
  extractedDefinitions:ranges,actualCrashNames:crashNames,
  sourceHashes:Object.fromEntries([...frozen].map(([name,bytes])=>[name,sha(bytes)])),
  fixtureHashes:Object.fromEntries([...fixtureBytes].map(([name,bytes])=>[name,sha(bytes)])),
  seams:['clock/process liveness and board/power/Host context','seeded already-loaded in-memory journal and atomic persistence',
    'PnP physical/interface resolution and cached hardware read facts','owned hidden-list restore transport',
    'command open/close/write outcomes; actual durable BeforeModeWrite gate executes','terminal scheduling and diagnostic sinks'],
  real:['actual CrashMaintenance, PrepareOwner, BeforeModeWrite, AfterOwnedRestore, AfterModeDischarge, RetireStartupJournal',
    'actual initial Restore and Finish; actual owned-return header, pure policy, Slot and debt discharge helpers'],
  excludes:['actual journal load/migration parser, OS PnP resolver, HID transport and concurrency','Steam consumption, hardware field acceptance']};
receipt.runnerSha256=sha(fs.readFileSync(fileURLToPath(import.meta.url)));
receipt.fixtureDirectory=fixtureDirectory??path.join(runnerRoot,'tools/fixtures/msi_startup_owned_return');
receipt.rebindFragments=rebindFragments;
save('source-binding.json',receipt);
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
    quote(path.join(snapshot, 'tools/startup_integration.cpp')),
    '/Fo' + quote(path.join(dir, 'selftest.obj')), '/Fe' + quote(exe), '/Fd' + quote(path.join(dir, 'selftest.pdb')),
    '/link', '/SUBSYSTEM:CONSOLE', '/MACHINE:x64', '/INCREMENTAL:NO'];
  const batch = path.join(dir, 'build.cmd');
  fs.writeFileSync(batch, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\ncl ' + opts.join(' ') + '\r\nexit /b %errorlevel%\r\n');
  console.log(profile + ': compiling integrated startup runtime /W4 /WX /permissive-');
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
  ...[...receipt.real,...receipt.seams,...receipt.excludes].map(s => '- ' + s), '',
  'Source changes during verification: ' + (result.sourceChangedDuringRun.join(', ') || 'none') + '.', '',
];
fs.writeFileSync(path.join(run, 'REPORT.md'), lines.join('\n'));
console.log('Evidence: ' + run);
process.exitCode = result.passed ? 0 : 1;

