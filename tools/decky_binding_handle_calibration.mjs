// Run from any cwd: node tools/decky_binding_handle_calibration.mjs <fresh-evidence-dir>
// Only the two new calibration sources and fresh evidence are written.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const repo = path.resolve(import.meta.dirname, '..');
const outputArgument = process.argv[2];
assert(outputArgument && process.argv.length === 3, 'Supply one fresh evidence directory.');
const out = path.resolve(outputArgument);
assert(!fs.existsSync(out), 'Evidence directory must be fresh.');
assert(!path.relative(repo, out).split(path.sep).every(part => part !== '..'), 'Evidence must be outside the repository.');
fs.mkdirSync(out, { recursive: true });
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const write = (name, value) => fs.writeFileSync(path.join(out, name), JSON.stringify(value, null, 2) + '\n');
const originalResult = [path.resolve(out, '../typed-runtime-run-002/typed-runtime-result.json'), path.resolve(out, '../../typed-runtime-run-002/typed-runtime-result.json')].find(p => fs.existsSync(p));
assert(originalResult, 'Keep calibration evidence beside typed-runtime-run-002 or one directory deeper.');
const originalBytes = fs.readFileSync(originalResult), originalSha = sha(originalBytes);
const original = JSON.parse(originalBytes);
const key = row => [row.handle, row.type, row.access].join(':');
const diff = (before, after) => {
  const b = new Set(before.map(key)), a = new Set(after.map(key));
  return { delta: after.length - before.length, added: after.filter(row => !b.has(key(row))), removed: before.filter(row => !a.has(key(row))) };
};
write('ORIGINAL-COMPARISON.json', { path: originalResult, sha256: originalSha, cases: original.cases.map(c => ({
  id: c.id, before: c.beforeHandles ?? c.parentHandlesBefore, after: c.afterHandles ?? c.parentHandlesAfter,
  difference: diff(c.handleSnapshotBefore, c.handleSnapshotAfter),
})) });

const source = path.join(out, 'source');
const inputs = ['tools/decky_binding_handle_calibration.cpp', 'tools/decky_binding_handle_calibration.mjs', 'deps/json/json.hpp'];
const pins = inputs.map(relative => {
  const bytes = fs.readFileSync(path.join(repo, relative)), target = path.join(source, relative);
  fs.mkdirSync(path.dirname(target), { recursive: true }); fs.writeFileSync(target, bytes);
  return { path: relative, sha256: sha(bytes), bytes: bytes.length };
});
// Read-only pins guard the product and fixture inputs from the supplied historical run.
const historicalPins = JSON.parse(fs.readFileSync(path.resolve(originalResult, '../SOURCE-PINS.json'), 'utf8'));
const protectedInputs = historicalPins.files.filter(p => !inputs.includes(p.path)).map(p => ({ path: p.path, sha256: sha(fs.readFileSync(path.join(repo, p.path))) }));
const protectionExtra = ['AGENTS.md', 'native/main.cpp', 'tools/ymcc_sim/registry.json'];
for (const relative of protectionExtra) if (fs.existsSync(path.join(repo, relative)) && !protectedInputs.some(p => p.path === relative)) protectedInputs.push({ path: relative, sha256: sha(fs.readFileSync(path.join(repo, relative))) });
write('SOURCE-PINS.json', { repo, source, files: pins, protectedInputs });

const pf = process.env['ProgramFiles(x86)']; assert(pf, 'Windows x64 toolchain required.');
const discovery = spawnSync(path.join(pf, 'Microsoft Visual Studio/Installer/vswhere.exe'),
  ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
  { encoding: 'utf8', windowsHide: true, timeout: 10000 });
assert.equal(discovery.status, 0, 'MSVC discovery failed.');
const vs = discovery.stdout.trim(); assert(vs, 'No MSVC x64 toolchain.');
const vcVersion = fs.readFileSync(path.join(vs, 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'), 'utf8').trim();
const vc = path.join(vs, 'VC/Tools/MSVC', vcVersion), sdk = path.join(pf, 'Windows Kits/10');
const sdkVersion = fs.readdirSync(path.join(sdk, 'Include')).filter(v => fs.existsSync(path.join(sdk, 'Include', v, 'um/windows.h')))
  .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }))[0];
assert(sdkVersion, 'Windows SDK required.');
const bin = path.join(vc, 'bin/Hostx64/x64'), compiler = path.join(bin, 'cl.exe');
const env = Object.fromEntries(Object.entries(process.env).map(([k, v]) => [k.toUpperCase(), v]));
env.PATH = bin + ';' + path.join(process.env.SystemRoot, 'System32');
env.INCLUDE = [path.join(vc, 'include'), ...['ucrt', 'shared', 'um'].map(d => path.join(sdk, 'Include', sdkVersion, d))].join(';');
env.LIB = [path.join(vc, 'lib/x64'), ...['ucrt', 'um'].map(d => path.join(sdk, 'Lib', sdkVersion, d, 'x64'))].join(';');
for (const name of ['CL', '_CL_', 'LINK', '_LINK_']) env[name] = '';
env.TEMP = path.join(out, 'tmp'); env.TMP = env.TEMP; fs.mkdirSync(env.TEMP);
const exe = path.join(out, 'handle-calibration.exe'), object = path.join(out, 'handle-calibration.obj');
const args = ['/nologo', '/Bv', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', '/WX', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE',
  '/I' + path.join(source, 'deps/json'), path.join(source, inputs[0]), '/sourceDependencies', path.join(out, 'dependencies.json'),
  '/Fo' + object, '/Fe' + exe, '/link', 'bcrypt.lib'];
const build = spawnSync(compiler, args, { cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 4 * 1024 * 1024 });
fs.writeFileSync(path.join(out, 'compile.log'), (build.stdout ?? '') + (build.stderr ?? ''));
write('BUILD.json', { compiler, compilerSha256: sha(fs.readFileSync(compiler)), vcVersion, sdkVersion, args, exit: build.status, error: build.error?.message ?? null });
assert.equal(build.status, 0, 'Standalone calibration compile failed; see compile.log.');
const imports = spawnSync(path.join(bin, 'dumpbin.exe'), ['/imports', exe], { cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 10000 });
fs.writeFileSync(path.join(out, 'imports.log'), (imports.stdout ?? '') + (imports.stderr ?? ''));
assert.equal(imports.status, 0, 'Import audit failed.');
const dynamicExe = path.join(out, 'handle-calibration-dynamic.exe');
const dynamicArgs = args.filter(a => a !== 'bcrypt.lib').map(a => a === '/Fo' + object ? '/Fo' + path.join(out, 'handle-calibration-dynamic.obj') : a === '/Fe' + exe ? '/Fe' + dynamicExe : a === path.join(out, 'dependencies.json') ? path.join(out, 'dependencies-dynamic.json') : a);
dynamicArgs.splice(1, 0, '/DCALIBRATION_DYNAMIC_BCRYPT');
const dynamicBuild = spawnSync(compiler, dynamicArgs, { cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 4 * 1024 * 1024 });
fs.writeFileSync(path.join(out, 'compile-dynamic.log'), (dynamicBuild.stdout ?? '') + (dynamicBuild.stderr ?? ''));
write('BUILD-DYNAMIC.json', { compiler, args: dynamicArgs, exit: dynamicBuild.status, error: dynamicBuild.error?.message ?? null });
assert.equal(dynamicBuild.status, 0, 'Dynamic module control compile failed.');
const dynamicImports = spawnSync(path.join(bin, 'dumpbin.exe'), ['/imports', dynamicExe], { cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 10000 });
fs.writeFileSync(path.join(out, 'imports-dynamic.log'), (dynamicImports.stdout ?? '') + (dynamicImports.stderr ?? ''));
assert.equal(dynamicImports.status, 0); assert(!/\bbcrypt\.dll\b/i.test(dynamicImports.stdout), 'Dynamic control must not import bcrypt.dll.');
const scenarios = ['observer-only', 'rng-only', 'threads-only', 'pipe-only', 'own-child-only', 'rng-then-threads', 'threads-then-rng', 'rng-once-only', 'rng-once-no-names', 'rng-only-no-names', 'rng-once-then-threads', 'dynamic-load-only', 'dynamic-rng-once', 'dynamic-rng-repeat'];
const runs = [];
for (const scenario of scenarios) for (let repeat = 1; repeat <= 3; ++repeat) {
  const id = `${scenario}-${repeat}`, resultFile = `${id}.json`;
  const executable = scenario.startsWith('dynamic-') ? dynamicExe : exe;
  const run = spawnSync(executable, [scenario], { cwd: out, env, encoding: 'utf8', windowsHide: true, timeout: 30000, maxBuffer: 16 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, `${id}.stderr.log`), run.stderr ?? '');
  fs.writeFileSync(path.join(out, resultFile), run.stdout ?? '');
  const row = { id, scenario, repeat, executable: path.basename(executable), file: resultFile, exit: run.status, signal: run.signal, error: run.error?.message ?? null };
  if (run.status === 0) {
    const result = JSON.parse(run.stdout);
    assert.equal(result.scenario, scenario); assert(result.ownedResourcesZero);
    row.net = result.net; row.rounds = result.rounds;
    row.changes = result.stages.filter(s => s.difference.delta || s.difference.added.length || s.difference.removed.length).map(s => ({ id: s.id, ...s.difference }));
    row.observerStable = result.stages.filter(s => s.id.startsWith('observer.') || s.id.startsWith('final.quiescent-')).every(s => s.difference.delta === 0 && s.difference.added.length === 0 && s.difference.removed.length === 0);
    assert(row.observerStable, `${id}: observer/quiescent control changed handles.`);
  }
  runs.push(row); write('RUNS.json', runs);
  assert.equal(run.status, 0, `${id}: calibration failed; stderr and partial evidence preserved.`);
  console.log(`${id}: net=${row.net.delta}; retained=${row.net.added.map(h => h.type + '/' + h.access + (h.objectName ? '/' + h.objectName : '')).join(',') || 'none'}`);
}
const changedInputs = [...pins, ...protectedInputs].filter(p => sha(fs.readFileSync(path.join(repo, p.path))) !== p.sha256).map(p => p.path);
assert.equal(sha(fs.readFileSync(originalResult)), originalSha, 'Historical failure evidence changed.');
const observedPackets = runs.flatMap(r => r.changes.flatMap(s => s.added.filter(h => h.type === 'WaitCompletionPacket').map(h => ({ run: r.id, stage: s.id, handle: h }))));
const retainedPackets = runs.flatMap(r => r.net.added.filter(h => h.type === 'WaitCompletionPacket').map(h => ({ run: r.id, handle: h })));
const result = {
  schema: 'ymcc.handle-calibration.runner.v1', at: new Date().toISOString(), repo, evidence: out,
  status: changedInputs.length === 0 ? 'calibration-complete' : 'input-drift', os: { type: os.type(), release: os.release(), version: os.version(), arch: os.arch() },
  compilerExit: build.status, exeSha256: sha(fs.readFileSync(exe)), dynamicExeSha256: sha(fs.readFileSync(dynamicExe)), runs: runs.length, scenarios, coldRepeats: 3, changedInputs,
  snapshots: 'NtQueryInformationProcess(GetCurrentProcess(),51); NtQueryObject(type) on self; new File names only while own IO and workers are quiescent',
  rng: runs.filter(r => r.scenario === 'rng-only').map(r => ({ run: r.id, net: r.net, rounds: r.rounds })),
  waitCompletionPacket: {
    status: observedPackets.length ? 'standalone-observed-original-owner-missing-evidence' : 'missing-evidence', observedPackets, retainedPackets,
    limitation: 'Matching a type/access or retention pattern does not identify the original Runtime handle creation site or its owner. No original File object name or creation stack was supplied.'
  },
  persistenceLimit: 'A retained handle is observed through joined threads, closed explicit resources, repeated calls, and final quiescent samples. These bounded samples alone do not prove retention until process exit or general OS-library ownership.',
  originalFailurePreserved: true, originalResultSha256: originalSha, runtimeOrClientExecuted: false, steamOrDeviceActions: 0, sleepActions: 0,
  gateChanged: false, productAcceptance: false, hostReleasePassed: false,
};
write('RESULT.json', result);
const lines = ['# Independent handle calibration', '', `Evidence: ${out}`, `Completed ${runs.length} probes (${scenarios.length} scenarios, 3 fresh processes each); MSVC ${vcVersion}, SDK ${sdkVersion}, ${os.version()}.`, '',
  '| Scenario | Cold repeat | Initial/final delta | Retained additions |', '| --- | --- | ---: | --- |',
  ...runs.map(r => `| ${r.scenario} | ${r.repeat} | ${r.net.delta} | ${r.net.added.map(h => `${h.type} access=${h.access}${h.objectName ? ' name=' + h.objectName : ''}`).join('; ') || 'none'} |`), '',
  'All per-stage before/after handle tables, type counts, numeric handles, access masks, safe File names, API counts and repeat controls are in each probe JSON. RUNS.json lists changes; ORIGINAL-COMPARISON.json preserves the input differences.', '',
  `WaitCompletionPacket: ${result.waitCompletionPacket.status}. First appearance sites: ${observedPackets.map(p => p.run + ':' + p.stage).join(', ') || 'none'}.`, '',
  result.waitCompletionPacket.limitation, result.persistenceLimit, '',
  'No Runtime/Client/Steam code is linked or executed. Only self handles are queried; inherited baseline File names are skipped. Original resource assertions and the product/release gate are unchanged. Calibration completion is not product acceptance.', '',
  `Protected/source input drift: ${changedInputs.join(', ') || 'none'}. Historical result SHA256: ${originalSha}.`, ''];
fs.writeFileSync(path.join(out, 'REPORT.md'), lines.join('\n'));
assert.equal(changedInputs.length, 0, 'Protected input drift detected.');
console.log(JSON.stringify({ evidence: out, status: result.status, runs: result.runs, waitCompletionPacket: result.waitCompletionPacket.status, gateChanged: false }));
