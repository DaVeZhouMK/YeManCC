import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

// Offline only. Freeze the actual header, link-block real adapter calls, audit
// imports before execution, and retain only allowlisted scalar log evidence.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const {values, positionals} = parseArgs({allowPositionals: true, options: {
  out: {type: 'string'}, vcvars: {type: 'string'}, help: {type: 'boolean', short: 'h'},
}});
if (values.help) {
  console.log('Usage: node tools/task_view_foreground_grant_selftest.mjs [out | --out out] [--vcvars vcvars64.bat]');
  process.exit(0);
}
if (positionals.length > 1 || (values.out && positionals.length)) throw Error('Supply one output directory');
const evidence = path.resolve(root, '../../../Build/Validation/Controller-Status-G3E-20261021');
const out = path.resolve(values.out || positionals[0] || path.join(evidence, 'task-view-grant'));
const vcvars = values.vcvars || 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const header = 'native/task_view_foreground_grant.h';
const fixture = 'tools/task_view_foreground_grant_selftest.cpp';
const inputs = [header, fixture, 'tools/task_view_foreground_grant_selftest.mjs', 'deps/json/json.hpp',
  'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const apis = ['GetTickCount64','GetCurrentThreadId','GetCurrentProcessId','GetWindowThreadProcessId',
  'GetForegroundWindow','GetLastError','SetLastError','AllowSetForegroundWindow','SendMessageTimeoutW'];
const bannedImports = ['user32.dll','win32u.dll','ole32.dll','oleaut32.dll','combase.dll','shell32.dll',
  'sendinput','keybd_event','mouse_event','setforegroundwindow','setfocus','attachthreadinput',
  'cocreateinstance','coinitialize','cogetclassobject','createprocessw','createprocessa','winexec','shellexecute'];
const sha = data => createHash('sha256').update(data).digest('hex');
const save = (name, data) => fs.writeFileSync(path.join(out,name), JSON.stringify(data,null,2) + '\n');
const strip = text => text.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
  value => value.replace(/[^\n]/g,' '));
function commandArg(value) {
  assert(!/["%\r\n]/.test(value), 'Unsupported command path/argument');
  return `"${value}"`;
}
function fieldRecord() {
  const result = {schemaVersion: 1, source: 'local native-lifecycle log pair', rawLinesRetained: false,
    privacy: 'allowlisted numeric/boolean fields and fixed event/status values only; paths/text/handles omitted',
    available: false, sources: [], records: [], counts: {}, failureCallingThreads: {}, failureOwnerThreads: {}};
  const scalarKeys = ['nativeRequestSequence','threadId','pid','helperPid','helperStarted','helperResumed',
    'helperReady','executionReleased','foregroundGrantAttempted','foregroundGrantAccepted',
    'foregroundGrantError','errorHint','cleanupVerified','cleanupRetained','apiRequestAccepted','taskViewVisibleProven'];
  for (const name of ['native-lifecycle.log.1','native-lifecycle.log']) {
    const file = path.join(evidence,'logs',name);
    if (!fs.existsSync(file)) continue;
    const bytes = fs.readFileSync(file);
    result.available = true; result.sources.push({name, sha256: sha(bytes)});
    for (const line of bytes.toString('utf8').split(/\r?\n/)) {
      let data; try { data = JSON.parse(line); } catch { continue; }
      if (data.event !== 'focus-task-view-api-result' || !['foreground-grant-failed','requested'].includes(data.status)) continue;
      const row = {event: data.event, status: data.status};
      for (const key of scalarKeys) if (typeof data[key] === 'boolean' || Number.isSafeInteger(data[key])) row[key] = data[key];
      for (const phase of ['foregroundBeforeDispatch','foregroundAfterDispatch']) {
        const source = data[phase]; if (!source || typeof source !== 'object') continue;
        const snapshot = {};
        if (Number.isSafeInteger(source.callingThreadId)) snapshot.callingThreadId = source.callingThreadId;
        for (const name of ['callerWindow','foregroundBefore','foregroundAfter']) {
          const win = {};
          for (const key of ['threadId','processId']) if (Number.isSafeInteger(source[name]?.[key])) win[key] = source[name][key];
          if (Object.keys(win).length) snapshot[name] = win;
        }
        row[phase] = snapshot;
      }
      result.records.push(row); result.counts[row.status] = (result.counts[row.status] || 0) + 1;
      if (row.status === 'foreground-grant-failed') {
        const thread = row.threadId, owner = row.foregroundBeforeDispatch?.callerWindow?.threadId;
        if (Number.isSafeInteger(thread)) result.failureCallingThreads[thread] = (result.failureCallingThreads[thread] || 0) + 1;
        if (Number.isSafeInteger(owner)) result.failureOwnerThreads[owner] = (result.failureOwnerThreads[owner] || 0) + 1;
      }
    }
  }
  const failures = result.records.filter(row => row.status === 'foreground-grant-failed');
  result.failureReadyWithoutReleaseError5 = failures.length > 0 && failures.every(row => row.helperReady === true &&
    row.executionReleased === false && row.foregroundGrantAccepted === false && row.foregroundGrantError === 5 && row.errorHint === 5);
  result.matchesReportedCounts = result.counts['foreground-grant-failed'] === 33 && result.counts.requested === 2;
  result.runtimeUiBridgeProven = false;
  return result;
}
function sourceAudit(sources) {
  const code = strip(sources[header].toString('utf8'));
  const before = sources[fixture].toString('utf8').split('#include "../native/task_view_foreground_grant.h"')[0];
  for (const api of apis) assert(new RegExp(`^#define ${api} forbidden_os::\\w+$`,'m').test(before), `Missing unresolved tripwire: ${api}`);
  const forbidden = /\b(?:SendInput|keybd_event|mouse_event|SetForegroundWindow|SetFocus|AttachThreadInput|CoCreateInstance|CoInitializeEx|CreateProcessW|CreateThread|Sleep|while|for|INFINITE)\b/g;
  assert.deepEqual(code.match(forbidden) || [], [], 'Forbidden side effect/retry/polling in broker header');
  const cpp = strip(sources[fixture].toString('utf8'));
  assert(!/\bWindowsOps\s*[({]/.test(cpp), 'Fixture must not instantiate real adapter');
  assert.deepEqual(cpp.match(forbidden) || [], cpp.match(/\bfor\b/g) || [], 'Fixture has forbidden OS side effect');
  return {pass: true, unresolvedApiTripwires: apis, actualHeaderCompiled: true,
    brokerFocusInputProcessComOrRetryCalls: false, fixtureUsesOnlyFakeOps: true};
}
function buildAndRun(name, flags, preamble) {
  const exe = path.join(out,`${name}.exe`), object = path.join(out,`${name}.obj`);
  const batch = path.join(out,`${name}.cmd`);
  const args = ['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/Bv',...flags,
    path.join(out,'source',fixture),'/I'+path.join(out,'source/deps/json'),
    '/Fo'+object,'/Fd'+path.join(out,`${name}-compiler.pdb`),'/Fe'+exe,
    '/link','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib','/NODEFAULTLIB:ole32.lib',
    '/NODEFAULTLIB:oleaut32.lib','/NODEFAULTLIB:shell32.lib'];
  const batchText = preamble + args.map(commandArg).join(' ') + '\r\nif errorlevel 1 exit /b 1\r\n' +
    `dumpbin /symbols ${commandArg(object)} > ${commandArg(path.join(out,`${name}-symbols.txt`))}\r\nif errorlevel 1 exit /b 1\r\n` +
    `dumpbin /imports ${commandArg(exe)} > ${commandArg(path.join(out,`${name}-imports.txt`))}\r\nexit /b %errorlevel%\r\n`;
  fs.writeFileSync(batch,batchText);
  const env = {...process.env, TMP: path.join(out,'temp'), TEMP: path.join(out,'temp')};
  const compile = spawnSync(process.env.ComSpec || 'cmd.exe',['/d','/c',batch],
    {cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 120000});
  fs.writeFileSync(path.join(out,`${name}-compile.log`),(compile.stdout || '') + (compile.stderr || '') + (compile.error ? '\n'+compile.error.message : ''));
  const profile = {profile: name, pass: false, compileExitCode: compile.status, executableRun: false};
  if (compile.status !== 0) { profile.error = 'Compilation/import dump failed; see compile log'; return profile; }
  const imports = fs.readFileSync(path.join(out,`${name}-imports.txt`),'utf8').toLowerCase();
  const symbols = fs.readFileSync(path.join(out,`${name}-symbols.txt`),'utf8').toLowerCase();
  const hits = bannedImports.filter(api => imports.includes(api));
  const adapterRefs = apis.filter(api => symbols.includes('__imp_'+api.toLowerCase()));
  const audit = {pass: hits.length === 0 && adapterRefs.length === 0, forbiddenImportMatches: hits,
    actualAdapterObjectImports: adapterRefs, dlls: [...imports.matchAll(/^\s+([\w.-]+\.dll)\s*$/gm)].map(m => m[1]),
    staticCrtKernel32ImportsAllowed: true};
  save(`${name}-import-audit.json`,audit); profile.importAudit = audit;
  if (!audit.pass) { profile.error = 'Forbidden import; executable was not run'; return profile; }
  const run = spawnSync(exe,[],{cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 30000});
  fs.writeFileSync(path.join(out,`${name}-stdout.json`),run.stdout || '');
  fs.writeFileSync(path.join(out,`${name}-stderr.txt`),(run.stderr || '') + (run.error ? '\n'+run.error.message : ''));
  profile.executableRun = true; profile.exitCode = run.status;
  try {
    profile.test = JSON.parse(run.stdout);
    assert(profile.test.count > 0 && profile.test.count === profile.test.cases.length, 'Invalid case count');
    assert(new Set(profile.test.cases.map(row => row.case)).size === profile.test.count, 'Duplicate case names');
    profile.pass = run.status === 0 && profile.test.pass === true && profile.test.cases.every(row => row.pass === true);
  } catch (error) { profile.error = 'Invalid fixture JSON: '+error.message; }
  return profile;
}
fs.mkdirSync(path.join(out,'temp'),{recursive: true});
const result = {schemaVersion: 1, pass: false, count: 0, cases: [], profiles: [],
  allOpsInMemory: true, productExecuted: false, actualHelperExecuted: false,
  windowsOpsInvoked: false, actualOSInput: false, comInvoked: false,
  scope: 'Frozen actual Broker header with logical FakeOps UI routing; no live UI/COM/product proof',
  startedAt: new Date().toISOString()};
try {
  const sources = Object.fromEntries(inputs.map(name => [name,fs.readFileSync(path.join(root,name))]));
  result.sourceHashes = Object.fromEntries(inputs.map(name => [name,sha(sources[name])]));
  for (const [name,bytes] of Object.entries(sources)) {
    const destination = path.join(out,'source',name);
    fs.mkdirSync(path.dirname(destination),{recursive: true}); fs.writeFileSync(destination,bytes);
  }
  result.sourceAudit = sourceAudit(sources); save('source-audit.json',result.sourceAudit);
  const observed = fieldRecord(); save('field-record.json',observed);
  result.fieldRecord = {available: observed.available, counts: observed.counts,
    failureCallingThreads: observed.failureCallingThreads, failureOwnerThreads: observed.failureOwnerThreads,
    failureReadyWithoutReleaseError5: observed.failureReadyWithoutReleaseError5};
  const preamble = calibrationToolchainPreamble(vcvars);
  for (const [name,flags] of [['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/D_DEBUG','/MTd']]]) {
    const profile = buildAndRun(name,flags,preamble); result.profiles.push(profile);
    const rows = profile.test?.cases || [];
    result.cases.push(...rows.map(row => ({profile: name,...row})));
    console.log(`${name}: ${profile.pass ? 'PASS' : 'FAIL'} ${rows.filter(row => row.pass).length}/${rows.length}`);
    for (const row of rows.filter(row => !row.pass)) console.log(`FAIL ${row.case}: ${row.error}`);
  }
  result.count = result.cases.length;
  result.checks = result.profiles.reduce((n,p) => n + (p.test?.checks || 0),0);
  result.passed = result.cases.filter(row => row.pass).length; result.failed = result.count - result.passed;
  result.changedInputs = inputs.filter(name => sha(fs.readFileSync(path.join(root,name))) !== result.sourceHashes[name]);
  result.testedInputsStable = result.changedInputs.length === 0;
  result.pass = result.testedInputsStable && result.profiles.length === 2 && result.profiles.every(p => p.pass);
  if (!result.pass) result.error = result.changedInputs.length ? 'Tested source changed during run' :
    result.profiles.filter(p => !p.pass).map(p => `${p.profile}: ${p.error || 'fixture assertions failed'}`).join('; ');
  save('baseline-record.json',result.profiles[0]?.test?.records || []);
} catch (error) { result.error = error.message; }
result.finishedAt = new Date().toISOString();
save('result.json',result);
save('foreground-grant-result.json',result);
console.log(JSON.stringify({pass: result.pass, count: result.count, checks: result.checks || 0,
  fieldCounts: result.fieldRecord?.counts, error: result.error}));
process.exitCode = result.pass ? 0 : 1;
