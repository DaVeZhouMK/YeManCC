import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const tool = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(tool), '..');
const workspace = path.resolve(repo, '../../..');
const validation = path.join(workspace, 'Build/Validation/MSI-Field130641-R106-20261016');
const args = process.argv.slice(2);
const options = new Map();
for (let i = 0; i < args.length; i += 2) {
  assert(['--out', '--source', '--before', '--vcvars'].includes(args[i]), `Unknown option: ${args[i]}`);
  assert(args[i + 1] && !args[i + 1].startsWith('--'), `Missing value: ${args[i]}`);
  assert(!options.has(args[i]), `Duplicate option: ${args[i]}`);
  options.set(args[i], args[i + 1]);
}
const out = path.resolve(options.get('--out') ?? path.join(validation, 'quarantine-order-regression'));
assert(!fs.existsSync(out), `Use a fresh --out directory: ${out}`);
const before = path.resolve(options.get('--before') ?? path.join(workspace, 'Build/V105-1240/Mainline/YeManCC-source/YeManCC/native/main.cpp'));
const current = path.resolve(options.get('--source') ?? path.join(repo, 'native/main.cpp'));
const localBefore = path.join(validation, 'before/main.cpp');
const fixtureDir = path.join(repo, 'tools/fixtures/msi_rearm_quarantine_order');
const vcvars = path.resolve(options.get('--vcvars') ?? 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const inputs = new Map();
function read(file) {
  file = path.resolve(file);
  if (!inputs.has(file)) inputs.set(file, fs.readFileSync(file));
  return inputs.get(file);
}
const variants = [{ name: 'Before', file: before, expectPreserved: false }];
if (fs.existsSync(localBefore) && path.resolve(localBefore) !== before)
  variants.push({ name: 'R106Before', file: localBefore, expectPreserved: false });
variants.push({ name: 'Current', file: current, expectPreserved: true });
const template = read(path.join(fixtureDir, 'namespace.cpp.in')).toString('utf8');
const cases = read(path.join(fixtureDir, 'cases.cpp.in')).toString('utf8');
read(tool);
const sourceTicket = path.join(path.dirname(current), 'msi_explicit_rearm_ticket.h');
const ticketFile = fs.existsSync(sourceTicket) ? sourceTicket : path.join(repo, 'native/msi_explicit_rearm_ticket.h');
const ticket = read(ticketFile);
const jsonFile = path.join(repo, 'deps/json/json.hpp');
read(jsonFile);
const historicalTicket = path.join(path.dirname(before), 'msi_explicit_rearm_ticket.h');
if (fs.existsSync(historicalTicket))
  assert.equal(sha(read(historicalTicket)), sha(ticket), 'Before/current real ticket headers differ; compare their semantics explicitly');
assert(fs.existsSync(vcvars), `Missing MSVC environment: ${vcvars}`);

// Mask strings/comments without changing offsets. Each brace-balanced fragment
// retains absolute source offsets, line numbers, exact text and a SHA256.
const mask = source => source.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
  value => value.replace(/[^\n]/g, ' '));
const bindings = [];
function extract(variant) {
  const source = read(variant.file).toString('utf8');
  const masked = mask(source);
  function range(start, end, label, kind = 'fragment') {
    assert(start >= 0 && end > start && end <= source.length, `${variant.name}: ${label}`);
    const code = source.slice(start, end);
    bindings.push({ variant: variant.name, file: variant.file, label, kind,
      start, end, line: source.slice(0, start).split('\n').length,
      endLine: source.slice(0, end).split('\n').length, sha256: sha(code), code });
    return { code, start, end };
  }
  function block(pattern, label, within = { start: 0, end: source.length }) {
    const hits = [...masked.slice(within.start, within.end).matchAll(new RegExp(pattern.source, 'g'))];
    assert.equal(hits.length, 1, `${variant.name}: unique ${label}`);
    const start = within.start + hits[0].index;
    const open = masked.indexOf('{', start);
    let depth = 1, end = open + 1;
    for (; depth && end < within.end; ++end) {
      if (masked[end] === '{') ++depth;
      if (masked[end] === '}') --depth;
    }
    assert.equal(depth, 0, `${variant.name}: balanced ${label}`);
    return range(start, end, label, pattern.source.startsWith('static') ? 'function' : 'fragment');
  }
  function statement(marker, label, within = { start: 0, end: source.length }) {
    const start = masked.indexOf(marker, within.start);
    assert(start >= within.start && start < within.end, `${variant.name}: ${label}`);
    const end = masked.indexOf(';', start) + 1;
    assert(end > start && end <= within.end, `${variant.name}: ${label} terminator`);
    return range(start, end, label);
  }
  const parts = {};
  const functions = {
    CLEAR: /static void inputHostClearLocked\(\)\s*\{/,
    RECONCILE: /static void inputHostReconcileQuarantineOnLifecycleThread\(\)\s*\{/,
    RESTORED: /static bool msiExplicitRearmRestoredLocked\(\)\s*\{/,
    SETTING_CHANGED: /static void msiExplicitRearmSettingChanged\(bool changed,bool wanted,const std::string& persona\)\s*\{/,
    START_SEQUENCE: /static unsigned long long msiExplicitRearmStartSettingSequence\(\)\s*\{/,
    STARTED: /static void msiExplicitRearmStarted\(unsigned long long settingSequence\)\s*\{/,
    RELEASE: /static void msiExplicitRearmRelease\(const char\* reason,bool quarantined,bool localAck,bool shutdownAck,bool exited,unsigned long long power\)\s*\{/,
    PERSONA: /static std::string inputHostConfiguredPersona\(\)\s*\{/,
    PARAM_PERSONA: /static const char\* inputHostPersona\(\)\s*\{/,
  };
  for (const [label, pattern] of Object.entries(functions)) parts[label] = block(pattern, label).code;
  parts.SETTING_COMPARE = statement('const std::string personaBefore = inputHostConfiguredPersona();', 'settings-persona-snapshot').code + '\n' +
    statement('const bool virtualTargetChanged =', 'settings-transition-predicate').code;
  parts.SETTING_ENABLED = statement('g_realStickTest.virtualTargetEnabled = targetEnabled;', 'settings-enabled-commit').code;
  parts.SETTING_PERSONA = block(/\{\s*std::lock_guard<std::mutex> personaLock\(g_inputHostPersonaMx\);\s*g_realStickTest\.virtualTargetPersona = configuredPersona;/, 'settings-persona-commit').code;
  parts.SETTING_RECEIPT = statement('msiExplicitRearmSettingChanged(virtualTargetChanged,targetEnabled,configuredPersona);', 'settings-ticket-callsite').code;
  parts.SETTING_REARM = block(/if \(virtualTargetChanged && targetEnabled\)\s*\{/, 'settings-explicit-rearm-boundary').code;
  const start = block(/static bool inputHostStart\(\)\s*\{/, 'inputHostStart-scope');
  const gate = block(/if \(g_inputHostStartBlocked\)\s*\{/, 'start-explicit-block-gate', start);
  const gateBegin = masked.lastIndexOf('if (g_inputHostProcess)', gate.start);
  assert(gateBegin >= start.start, 'Post-old-host gate required');
  parts.START_GATES = range(gateBegin, gate.end, 'start-existing-process-quarantine-and-block-gates').code;
  const stop = block(/static void inputHostStopLocked\(const char\* reason\)\s*\{/, 'inputHostStopLocked-scope');
  const release = statement('msiExplicitRearmRelease(reason,g_inputHostReleaseQuarantined,localReleaseAck,shutdownAck,hostExitConfirmed,msiStopPower);', 'stop-ticket-callsite', stop);
  const clearStart = masked.indexOf('inputHostClearLocked();', release.end);
  assert(clearStart >= release.end && clearStart < stop.end, 'Stop Clear must follow ticket release');
  parts.STOP_RECEIPT = release.code;
  parts.STOP_CLEAR = range(clearStart, clearStart + 'inputHostClearLocked();'.length, 'stop-final-clear-callsite').code;
  parts.CASES = cases;
  const lifecycle = parts.RECONCILE;
  const clearAt = lifecycle.indexOf('inputHostClearLocked();');
  const rearmAt = lifecycle.indexOf('msiExplicitRearmRestoredLocked();');
  assert(clearAt >= 0 && rearmAt >= 0, `${variant.name}: actual ordered calls`);
  variant.clearBeforeRearm = clearAt < rearmAt;
  variant.sourceSha256 = sha(read(variant.file));
  variant.functionHashes = Object.fromEntries(bindings.filter(b => b.variant === variant.name && b.kind === 'function')
    .map(b => [b.label, b.sha256]));
  return `namespace ${variant.name} {\n${template.replace(/@@([A-Z_]+)@@/g, (_, label) => {
    assert(label in parts, `Unknown fixture slot ${label}`);
    return parts[label];
  })}\n}\n`;
}
const generated = variants.map(extract).join('\n');
assert(!generated.includes('@@'), 'Unfilled fixture slot');
// This is a controlled order-only comparison of the functions under test.
for (const v of variants.slice(0, -1)) {
  for (const name of ['CLEAR', 'RESTORED', 'SETTING_CHANGED', 'START_SEQUENCE', 'STARTED', 'RELEASE', 'PERSONA', 'PARAM_PERSONA'])
    assert.equal(v.functionHashes[name], variants.at(-1).functionHashes[name], `${v.name}: unexpected production change in ${name}`);
  const previous = bindings.filter(b => b.variant === v.name && b.kind === 'fragment');
  for (const binding of previous) {
    const now = bindings.find(b => b.variant === 'Current' && b.label === binding.label);
    assert.equal(binding.sha256, now?.sha256, `${v.name}: unexpected gate/callsite change in ${binding.label}`);
  }
  assert.equal(v.clearBeforeRearm, false, `${v.name}: baseline no longer reproduces old order`);
}
assert.equal(variants.at(-1).clearBeforeRearm, true, 'Current must Clear the retired session before rearm');
fs.mkdirSync(out, { recursive: true });
fs.mkdirSync(path.join(out, 'source/native'), { recursive: true });
fs.writeFileSync(path.join(out, 'source/native/msi_explicit_rearm_ticket.h'), ticket);
for (const v of variants) fs.writeFileSync(path.join(out, `source/${v.name}-main.cpp`), read(v.file));
const headerInclude = ticketFile.replaceAll('\\', '/');
const cpp = '#include <atomic>\n#include <string>\n#include <vector>\n#include <mutex>\n#include <cstring>\n#include <iostream>\n' +
  `#include "${headerInclude}"\n#include "json.hpp"\nusing json=nlohmann::json;\n` +
  'namespace ymcc::screenpads { inline void observeNativeTransport(unsigned long long,bool){} }\n' + generated +
  'int main(){json result;unsigned failures=0;\n' + variants.map(v =>
    `result["${v.name}"]=${v.name}::runSuite(${v.expectPreserved});failures+=result["${v.name}"]["failures"].get<unsigned>();`).join('\n') +
  '\nresult["failures"]=failures;result["realHostSdkProductOrDeviceIO"]=false;std::cout<<result.dump()<<"\\n";return failures?1:0;}\n';
fs.writeFileSync(path.join(out, 'fixture.cpp'), cpp);
fs.writeFileSync(path.join(out, 'source-bindings.json'), JSON.stringify({ variants, ticketHeader: { file: ticketFile, sha256: sha(ticket) }, bindings }, null, 2) + '\n');

const profiles = [];
for (const mode of ['Release', 'Checked']) {
  const batch = path.join(out, `${mode}.cmd`);
  const flags = mode === 'Release' ? '/MT /O2 /DNDEBUG' : '/MTd /Od /Zi /RTC1 /D_DEBUG';
  // All paths are explicit and quoted. cmd metacharacters in overrides are rejected.
  for (const p of [out, vcvars, repo]) assert(!/["%\r\n&|<>^!]/.test(p), `Unsupported batch path: ${p}`);
  fs.writeFileSync(batch, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n` +
    `cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX ${flags} /I"${path.dirname(jsonFile)}" "${path.join(out, 'fixture.cpp')}" ` +
    `/Fo"${path.join(out, mode + '.obj')}" /Fd"${path.join(out, mode + '.pdb')}" /Fe"${path.join(out, mode + '.exe')}"\r\nexit /b %errorlevel%\r\n`);
  const built = spawnSync('cmd.exe', ['/d', '/c', batch], { cwd: out, encoding: 'utf8', windowsHide: true, timeout: 60000 });
  fs.writeFileSync(path.join(out, `${mode}-build.log`), (built.stdout ?? '') + (built.stderr ?? '') + (built.error ? '\n' + built.error.message : ''));
  if (built.status !== 0) { profiles.push({ mode, buildExit: built.status, error: 'compile failed' }); continue; }
  const run = spawnSync(path.join(out, `${mode}.exe`), [], { cwd: out, encoding: 'utf8', windowsHide: true, timeout: 15000 });
  fs.writeFileSync(path.join(out, `${mode}-run.log`), (run.stdout ?? '') + (run.stderr ?? '') + (run.error ? '\n' + run.error.message : ''));
  let result;
  try { result = JSON.parse(run.stdout.trim()); } catch { result = { failures: 1, error: 'Invalid fixture output' }; }
  profiles.push({ mode, buildExit: built.status, exit: run.status, result });
  console.log(JSON.stringify({ mode, exit: run.status, failures: result.failures,
    suites: variants.map(v => ({ name: v.name, cases: result[v.name]?.cases,
      scenarios: result[v.name]?.scenarios, failures: result[v.name]?.rows?.filter(r => !r.pass).map(r => r.name) })) }));
}
const sourceStable = [...inputs].every(([file, data]) => sha(fs.readFileSync(file)) === sha(data));
const pass = sourceStable && profiles.length === 2 && profiles.every(p => p.buildExit === 0 && p.exit === 0 && p.result.failures === 0);
const scope = {
  production: ['Full actual Clear, quarantine lifecycle, explicit rearm helper, settings/Start/release ticket wrappers and persona reader',
    'Actual settings comparison/commits/ticket-callsite/explicit-rearm boundary', 'Actual Stop ticket/clear callsites',
    'Actual Start post-old-Host process/quarantine/startBlocked gates', 'Real native/msi_explicit_rearm_ticket.h included directly'],
  modeled: ['Host accepted tuple and ACK/exit facts', 'Pinned endpoint absence and complete enumeration',
    'HidHide restore result/journal side effect', 'Board identity/power/fault/exit-fence facts', 'Logging, pipe close and notification leaves'],
  excluded: ['Full Stop/Start and settings parser', 'Real Host, SDK, product, transport, PnP, HidHide or device I/O',
    'Actual target creation, input publication, sleep, field acceptance'],
  contract: 'Every scenario uses a real retained Clear before explicit ON/reconcile. No fixture assignment clears transportFault; only actual Clear resets it. Positive rearm means intent preservation, never target insertion.',
};
const result = { at: new Date().toISOString(), pass, sourceStable, variants, profiles, scope,
  replay: { command: ['node', tool, ...args], vcvars, ticketFile, output: out },
  inputs: [...inputs].map(([file, data]) => ({ file, sha256: sha(data) })), productionChangedByTool: false };
fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(result, null, 2) + '\n');
const report = [
  '# MSI quarantine order offline regression', '', `Pass: ${pass}; input snapshots stable: ${sourceStable}.`, '',
  ...variants.map(v => `- ${v.name}: ${v.file}; SHA256 ${v.sourceSha256}; Clear before rearm: ${v.clearBeforeRearm}.`), '',
  ...profiles.map(p => `- ${p.mode}: compile ${p.buildExit}, run ${p.exit ?? 'not run'}, failures ${p.result?.failures ?? 'n/a'}.`), '',
  'Expected old positive result: false; current: true. Negative gates must refuse in every source variant.', '',
  ...Object.entries(scope).flatMap(([key, value]) => [`${key}:`, ...(Array.isArray(value) ? value.map(x => `- ${x}`) : [value]), '']),
  'Function hashes, exact extracted text and original line locations: source-bindings.json.',
  'All case assertions, lifecycle logs and state snapshots: result.json; compiler/run output: *-build.log / *-run.log.',
];
fs.writeFileSync(path.join(out, 'REPORT.md'), report.join('\n') + '\n');
console.log(JSON.stringify({ pass, sourceStable, out, profiles: profiles.map(p => ({ mode: p.mode, buildExit: p.buildExit, exit: p.exit })) }, null, 2));
if (!pass) process.exitCode = 1;
