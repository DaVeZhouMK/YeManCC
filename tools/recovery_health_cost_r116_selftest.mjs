// R116 source-bound CPU cost audit. Only compiles/runs an inert native fixture.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const workspace = path.resolve(repo, '../../..');
const opts = {source: path.join(repo, 'native/main.cpp'), out: path.join(workspace, 'Build/Validation/VirtualInput-Reanchor-R116-20261016/recovery-cost'), rounds: 7, iterations: 200000};
for (let i = 2; i < process.argv.length; ++i) {
  const key = process.argv[i];
  if (key === '--help') {
    console.log('node tools/recovery_health_cost_r116_selftest.mjs [--source MAIN_CPP] [--out EVIDENCE_DIR] [--rounds 3..15] [--iterations 10000..1000000]');
    process.exit(0);
  }
  assert(['--source', '--out', '--rounds', '--iterations'].includes(key), 'Unknown option: ' + key);
  assert(process.argv[i + 1], 'Missing value: ' + key);
  const value = process.argv[++i];
  opts[key.slice(2)] = ['--rounds', '--iterations'].includes(key) ? Number(value) : path.resolve(value);
}
assert(Number.isInteger(opts.rounds) && opts.rounds >= 3 && opts.rounds <= 15);
assert(Number.isInteger(opts.iterations) && opts.iterations >= 10000 && opts.iterations <= 1000000);
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const load = file => { const bytes = fs.readFileSync(file); return {file, bytes, text: bytes.toString('utf8'), sha256: hash(bytes)}; };
const source = load(opts.source);
const fixture = load(path.join(repo, 'tools/fixtures/recovery_health_cost_r116/fixture.cpp'));
const dependencies = ['native/input_host_recovery_health.h', 'native/virtual_output_status.h', 'deps/json/json.hpp'].map(p => load(path.join(repo, p)));
const ranges = [];
const mask = source.text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, m => m.replace(/[^\r\n]/g, ' '));
function balanced(start, open = '{', close = '}') {
  let depth = 0;
  for (let i = start; i < mask.length; ++i) {
    if (mask[i] === open) ++depth;
    if (mask[i] === close && --depth === 0) return i + 1;
  }
  throw Error('Unclosed source block at ' + start);
}
function record(name, start, end) {
  assert(start >= 0 && end > start, name + ' missing');
  const code = source.text.slice(start, end);
  ranges.push({name, file: source.file, startLine: source.text.slice(0, start).split('\n').length,
    endLine: source.text.slice(0, end).split('\n').length, sha256: hash(code), code});
  return code;
}
function fn(name) {
  for (const m of source.text.matchAll(new RegExp('^static[^\\r\\n(;{}]*\\b' + name + '\\s*\\(', 'gm'))) {
    let a = mask.indexOf('(', m.index), b = balanced(a, '(', ')');
    while (/\s/.test(mask[b])) ++b;
    if (mask[b] === '{') return record(name, m.index, balanced(b));
  }
  throw Error('Missing definition: ' + name);
}
function statement(name, needle, after = 0) {
  const start = source.text.indexOf(needle, after);
  return record(name, start, source.text.indexOf(';', start) + 1);
}
const snapshotStart = source.text.indexOf('struct InputCaptureGamepadSnapshot {');
const snapshot = record('InputCaptureGamepadSnapshot', snapshotStart, balanced(mask.indexOf('{', snapshotStart)) + 1);
const names = ['currentPowerGeneration', 'inputHostConfiguredPersona', 'inputHostPersona', 'inputHostBoundPersonaValue',
  'inputHostExitFenceRaised', 'sgVirtualWriterAllowedNow', 'physicalReleaseWindowActive',
  'msiClawKnownBoardAuthority', 'msiControllerRepairScope', 'msiDinputMainSourceEligible',
  'inputHostRecoveryHealthTrackingLocked', 'inputHostRecoveryHealthContextLocked', 'inputHostRecoveryHealthSource',
  'inputHostRecoveryHealthLatestSourceLocked', 'inputHostRecoveryHealthObserveLocked', 'inputHostRecoveryHealthAcceptedLocked'];
const functions = Object.fromEntries(names.map(name => [name, fn(name)]));
// These policy assertions fail on related drift instead of silently measuring another policy.
assert(functions.inputHostRecoveryHealthSource.includes('msiControllerRepairScope() && msiDinputMainSourceEligible()'));
assert(functions.inputHostRecoveryHealthSource.includes('xinput-any-connected'));
assert(functions.inputHostRecoveryHealthAcceptedLocked.includes('inputHostRecoveryHealthContextLocked(tracking)'));
assert(functions.inputHostRecoveryHealthAcceptedLocked.includes('tracking ? inputHostRecoveryHealthLatestSourceLocked(eligible) : source'));
assert(functions.msiControllerRepairScope.includes('g_msiDinputRouteDinputArmed.load(std::memory_order_acquire)'));
assert(functions.msiDinputMainSourceEligible.includes('g_msiDinputRouteDinputArmed.load(std::memory_order_acquire)'));
assert(functions.msiDinputMainSourceEligible.includes('return msiClawKnownBoardAuthority() &&'));
const submit = source.text.indexOf('static json inputHostSubmitPad(');
const observe = statement('production submit observe call', 'inputHostRecoveryHealthObserveLocked(physicalSourceAdmitted && !semanticNeutral)', submit);
const requestStart = source.text.indexOf('            const auto recoveryHealthRequestContext =', submit);
const request = record('production pre-request Context/Source capture', requestStart, source.text.indexOf('            accepted = frameTextReady', requestStart));
const accepted = statement('production accepted-receipt health call', 'inputHostRecoveryHealthAcceptedLocked(recoveryHealthRequestContext,', requestStart);
const producerStart = source.text.indexOf('        static unsigned long long diagnosticReadSequence=0;', source.text.indexOf('static void gamepadReadStateUnlocked('));
const producer = record('production canonical snapshot and continuity commit', producerStart, source.text.indexOf('        statusSourceWindow =', producerStart));
// Bind maintenance and identity init for review; neither is executed (no IO).
fn('inputHostRecoveryMaintenanceTick');
fn('ymccResolveMachineIdentityOnce');
const bodyNeedle = 'msiControllerRepairScope() && msiDinputMainSourceEligible()';
const bodyReplacement = 'g_msiDinputRouteDinputArmed.load(std::memory_order_acquire) && msiDinputMainSourceEligible()';
assert(functions.inputHostRecoveryHealthSource.split(bodyNeedle).length === 2);
const candidateSource = functions.inputHostRecoveryHealthSource.replace(bodyNeedle, bodyReplacement);
const instrument = {
  inputHostConfiguredPersona: ['std::string', '', '', 'configuredCopies'],
  inputHostBoundPersonaValue: ['std::string', '', '', 'boundCopies'],
  msiClawKnownBoardAuthority: ['bool', '', '', 'board'],
  msiControllerRepairScope: ['bool', '', '', 'repair'],
  msiDinputMainSourceEligible: ['bool', '', '', 'eligible'],
  inputHostRecoveryHealthTrackingLocked: ['bool', '', '', 'tracking'],
  inputHostRecoveryHealthContextLocked: ['ymcc::input::recoveryHealth::Context', 'bool verifyPersona = true', 'verifyPersona', 'context'],
  inputHostRecoveryHealthSource: ['ymcc::input::recoveryHealth::Source', 'const InputCaptureGamepadSnapshot* source, bool eligible', 'source, eligible', 'source'],
  inputHostRecoveryHealthLatestSourceLocked: ['ymcc::input::recoveryHealth::Source', 'bool eligible = true', 'eligible', 'snapshotLocks'],
};
function functionText(candidate) {
  return names.map(name => {
    if (candidate && name === 'msiControllerRepairScope') return ''; // No remaining candidate call site.
    const code = candidate && name === 'inputHostRecoveryHealthSource' ? candidateSource : functions[name];
    const spec = instrument[name];
    if (!spec) return code;
    const [type, args, call, counter] = spec;
    return '#if R116_COUNT\n#define ' + name + ' ' + name + '_r116_body\n' + code + '\n#undef ' + name +
      '\nstatic ' + type + ' ' + name + '(' + args + ') { ++counts.' + counter + '; return ' + name + '_r116_body(' + call + '); }\n#else\n' + code + '\n#endif';
  }).join('\n\n');
}
fs.mkdirSync(opts.out, {recursive: true});
const run = path.join(opts.out, 'runs', new Date().toISOString().replace(/[:.]/g, '-') + '-' + process.pid);
fs.mkdirSync(run, {recursive: true});
const save = (name, value) => fs.writeFileSync(path.join(run, name), JSON.stringify(value, null, 2) + '\n');
const inc = path.join(run, 'extracted'); fs.mkdirSync(inc);
for (const d of dependencies) fs.writeFileSync(path.join(inc, path.basename(d.file)), d.bytes);
for (const r of ranges) fs.writeFileSync(path.join(inc, r.name.replace(/[^\w-]/g, '_') + '.txt'), r.code);
fs.writeFileSync(path.join(run, 'candidate-source.cpp.txt'), candidateSource);
fs.writeFileSync(path.join(run, 'candidate.patch'), '--- actual/inputHostRecoveryHealthSource\n+++ fixture-only/inputHostRecoveryHealthSource\n-' + bodyNeedle + '\n+' + bodyReplacement + '\n');
save('source-bindings.json', {source: {file: source.file, sha256: source.sha256},
  ranges: ranges.map(({code, ...r}) => r), fixture: {file: fixture.file, sha256: fixture.sha256},
  dependencies: dependencies.map(({file, sha256}) => ({file, sha256})),
  candidate: {scope: 'one literal substitution inside extracted Source only; production untouched', before: bodyNeedle, after: bodyReplacement},
  instrumentation: 'Count build only: preprocessor rename + same-signature counting wrappers; armed atomic probe delegates to std::atomic and can revoke at either load. Timing build has original bodies without wrappers and a real std::atomic armed flag.',
  seams: ['inert process/Host/power/settings globals', 'virtual tick replaces GetTickCount64', 'non-Ready platform standby leaf returns a supplied bool',
    'frame driver invokes actual observe/request/accepted statements under inert existing mutexes; no whole Submit/pipe transport',
    'producer uses actual snapshot assignment/continuity commit, supplied route and read-window locals', 'allocation counter surrounds one measured frame/loop only'],
  notExecuted: ['identity initializer/registry', 'maintenance lifecycle work', 'product', 'Host', 'Steam', 'device', 'service', 'sleep', 'full build']});
const status = spawnSync('git', ['status', '--porcelain=v1', '--untracked-files=all'], {cwd: repo, encoding: 'utf8'});
assert(status.status === 0); fs.writeFileSync(path.join(run, 'git-status-before.txt'), status.stdout);
const vcvars = process.env.YMCC_VCVARS64 ?? 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars), 'Missing vcvars64; set YMCC_VCVARS64');
function quote(s) { assert(!/["\r\n]/.test(s)); return '"' + s + '"'; }
function command(label, exe, args, cwd, timeout = 60000, expected = 0) {
  const result = spawnSync(exe, args, {cwd, encoding: 'utf8', timeout});
  fs.writeFileSync(path.join(cwd, label + '.stdout.log'), result.stdout ?? '');
  fs.writeFileSync(path.join(cwd, label + '.stderr.log'), result.stderr ?? '');
  fs.writeFileSync(path.join(cwd, label + '.command.json'), JSON.stringify({exe, args, cwd, exitCode: result.status, expectedExitCode: expected, error: result.error?.message}, null, 2) + '\n');
  assert(!result.error && result.status === expected, label + ' failed; see ' + cwd);
  return result.stdout;
}
const profiles = {};
for (const candidate of [false, true]) for (const counted of [true, false]) {
  const label = (candidate ? 'candidate' : 'actual') + (counted ? '-counted' : '-timing');
  const dir = path.join(run, label); fs.mkdirSync(dir);
  let cpp = fixture.text;
  const parts = {SNAPSHOT: snapshot, FUNCTIONS: functionText(candidate), PRODUCER: producer, OBSERVE: observe, REQUEST: request, ACCEPTED: accepted};
  for (const [name, code] of Object.entries(parts)) {
    const marker = '@@' + name + '@@'; assert(cpp.split(marker).length === 2, name);
    cpp = cpp.replace(marker, () => code);
  }
  assert(!/@@\w+@@/.test(cpp)); fs.writeFileSync(path.join(dir, 'fixture.cpp'), cpp);
  const buildCmd = path.join(dir, 'compile.cmd');
  fs.writeFileSync(buildCmd, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /O2 /DR116_COUNT=' + Number(counted) + ' /DR116_CANDIDATE=' + Number(candidate) + ' ' + quote(path.join(dir, 'fixture.cpp')) + ' /I' + quote(inc) + ' /Fo' + quote(path.join(dir, 'fixture.obj')) + ' /Fe' + quote(path.join(dir, 'fixture.exe')) + '\r\nexit /b %errorlevel%\r\n');
  console.log('R116 compile and execute ' + label);
  command('build', 'cmd.exe', ['/d', '/c', buildCmd], dir);
  const result = JSON.parse(command('test', path.join(dir, 'fixture.exe'), [], dir));
  assert(result.passed); profiles[label] = {dir, ...result}; save(label + '.results.json', result);
}
for (const mode of ['counted', 'timing']) {
  assert.equal(profiles['actual-' + mode].matrixDigest, profiles['candidate-' + mode].matrixDigest, 'source classification/metadata diverged');
  assert.deepEqual(profiles['actual-' + mode].cases, profiles['candidate-' + mode].cases);
}
// Negative controls mutate extracted real code only. Each must fail an actual
// runtime policy assertion; compile failures are never accepted as witnesses.
const negativeControls = [];
const mutations = [
  {name: 'remove-final-armed-read', cpp: true,
    before: 'if (!g_msiDinputRouteDinputArmed.load(std::memory_order_acquire)) return false;',
    after: 'if (false) return false;', expected: 'armed revocation retained'},
  {name: 'weaken-source-freshness', header: 'virtual_output_status.h',
    before: 'kVirtualOutputSourceWindowMaxAgeMs = 1500',
    after: 'kVirtualOutputSourceWindowMaxAgeMs = 150000', expected: '1500ms source freshness bound'},
  {name: 'ignore-request-generation', header: 'input_host_recovery_health.h',
    before: 'if (!(request == current) || !valid(request, read, now)',
    after: 'if (false || !valid(request, read, now)', expected: 'generation change during ACK'},
  {name: 'ignore-reader-continuity-break', header: 'input_host_recovery_health.h',
    before: 'if (continuous && previous.continuity && previous.continuity != exhausted)',
    after: 'if ((continuous || true) && previous.continuity && previous.continuity != exhausted)', expected: 'reader stall must break continuity'},
];
for (const mutation of mutations) {
  const dir = path.join(run, 'negative-' + mutation.name); fs.mkdirSync(dir);
  let cpp = fs.readFileSync(path.join(profiles['actual-counted'].dir, 'fixture.cpp'), 'utf8');
  if (mutation.cpp) { assert(cpp.includes(mutation.before)); cpp = cpp.replaceAll(mutation.before, mutation.after); }
  for (const dep of dependencies) {
    let text = dep.text;
    if (path.basename(dep.file) === mutation.header) {
      assert(text.split(mutation.before).length === 2, mutation.name);
      text = text.replace(mutation.before, mutation.after);
    }
    fs.writeFileSync(path.join(dir, path.basename(dep.file)), text);
  }
  fs.writeFileSync(path.join(dir, 'fixture.cpp'), cpp);
  const cmd = path.join(dir, 'compile.cmd');
  fs.writeFileSync(cmd, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /O2 /DR116_COUNT=1 /DR116_CANDIDATE=0 ' + quote(path.join(dir, 'fixture.cpp')) + ' /I' + quote(dir) + ' /Fo' + quote(path.join(dir, 'fixture.obj')) + ' /Fe' + quote(path.join(dir, 'fixture.exe')) + '\r\nexit /b %errorlevel%\r\n');
  console.log('R116 verify negative control ' + mutation.name);
  command('build', 'cmd.exe', ['/d', '/c', cmd], dir);
  command('test', path.join(dir, 'fixture.exe'), [], dir, 60000, 1);
  const stderr = fs.readFileSync(path.join(dir, 'test.stderr.log'), 'utf8').trim();
  assert(stderr.includes(mutation.expected), mutation.name + ': unexpected failure ' + stderr);
  negativeControls.push({...mutation, actualFailure: stderr, caught: true});
}
save('negative-controls.json', negativeControls);
const rawCycles = [];
for (let round = 0; round < opts.rounds; ++round)
for (const route of ['xinput', 'msi-dinput']) for (const tracking of [false, true])
for (const variant of round % 2 ? ['candidate', 'actual'] : ['actual', 'candidate']) {
  const dir = profiles[variant + '-timing'].dir;
  const row = JSON.parse(command('bench-' + round + '-' + route + '-' + Number(tracking), path.join(dir, 'fixture.exe'), ['--bench', route, String(Number(tracking)), String(opts.iterations)], dir));
  rawCycles.push({round, variant, ...row});
}
save('thread-cycle-samples.json', rawCycles);
const median = values => { const xs = [...values].sort((a, b) => a - b); return xs[Math.floor(xs.length / 2)]; };
const comparisons = [];
for (const route of ['xinput', 'msi-dinput']) for (const tracking of [false, true]) {
  const group = rawCycles.filter(row => row.route === route && row.tracking === tracking);
  const stats = Object.fromEntries(['actual', 'candidate'].map(variant => {
    const xs = group.filter(row => row.variant === variant).map(row => row.cyclesPerFrame);
    return [variant, {medianCycles: median(xs), minCycles: Math.min(...xs), maxCycles: Math.max(...xs)}];
  }));
  const pairs = Array.from({length: opts.rounds}, (_, round) => {
    const xs = group.filter(row => row.round === round);
    return xs.find(row => row.variant === 'actual').cyclesPerFrame - xs.find(row => row.variant === 'candidate').cyclesPerFrame;
  });
  comparisons.push({route, tracking, ...stats, deltaMedianCycles: stats.actual.medianCycles - stats.candidate.medianCycles,
    pairedMedianSavedCycles: median(pairs), positivePairs: pairs.filter(x => x > 0).length, pairs: pairs.length,
    medianPercent: 100 * (stats.actual.medianCycles - stats.candidate.medianCycles) / stats.actual.medianCycles});
}
const after = load(source.file);
const rangeDrift = ranges.filter(r => {
  const code = r.code; return !after.text.includes(code);
}).map(r => r.name);
assert(rangeDrift.length === 0, 'Related source drift: ' + rangeDrift.join(', '));
for (const d of dependencies) assert.equal(load(d.file).sha256, d.sha256, 'Dependency drift: ' + d.file);
const afterStatus = spawnSync('git', ['status', '--porcelain=v1', '--untracked-files=all'], {cwd: repo, encoding: 'utf8'});
assert(afterStatus.status === 0); fs.writeFileSync(path.join(run, 'git-status-after.txt'), afterStatus.stdout);
const summary = {passed: true, run, sourceSha256: source.sha256,
  sourceStability: {wholeFileChanged: after.sha256 !== source.sha256, relatedRangesStable: true, dependencyHashesStable: true},
  validation: Object.fromEntries(Object.entries(profiles).map(([name, p]) => [name, {checks: p.checks, matrixRows: p.matrixRows, matrixDigest: p.matrixDigest}])),
  actualCounts: profiles['actual-counted'].countsPerFrameIncludingCanonicalCommit,
  candidateCounts: profiles['candidate-counted'].countsPerFrameIncludingCanonicalCommit,
  negativeControls, cycles: comparisons, settings: {rounds: opts.rounds, iterations: opts.iterations, optimization: 'MSVC /O2; no LTCG; independent counter-free bodies'},
  productionChanged: false, existingTestsChanged: false, productOrHostOrDeviceExecuted: false,
  scope: 'Actual extracted adapter + canonical commit cost, inert environment; current-thread cycles exclude descheduling but are not whole-product CPU/latency or hardware acceptance.'};
save('summary.json', summary);
fs.writeFileSync(path.join(opts.out, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
console.log(JSON.stringify(summary, null, 2));
