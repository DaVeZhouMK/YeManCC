// Offline C++17 lease verification. Compiles and executes both configurations,
// including a small set of semantic faults. Does not start YMCC or touch devices.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {freezeMainHelpers, verifyMainHelpers} from './fixtures/legion_motion_source/extract_main.mjs';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const workspace = path.resolve(repo, '../../..');
const root = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/LegacyMotion/legion-lease');
const options = {};
for (let i = 2; i < process.argv.length; ++i) {
  const arg = process.argv[i];
  if (arg === '--help') {
    console.log('node tools/legion_motion_source_selftest.mjs [--out FRESH_CHILD_OF_LEGION_LEASE] [--vcvars PATH] [--main-helpers]');
    process.exit(0);
  }
  if (arg === '--main-helpers') { options.mainHelpers = true; continue; }
  assert(['--out', '--vcvars'].includes(arg) && process.argv[i + 1], `Unknown/incomplete option: ${arg}`);
  options[arg.slice(2)] = process.argv[++i];
}
const out = path.resolve(options.out || path.join(root,
  `${new Date().toISOString().replace(/[:.]/g, '-')}-${crypto.randomBytes(3).toString('hex')}`));
assert.equal(path.dirname(out).toLowerCase(), root.toLowerCase(), 'Use a direct fresh child of legion-lease');
fs.mkdirSync(root, {recursive: true});
assert.equal(fs.realpathSync(root).toLowerCase(), path.resolve(root).toLowerCase(), 'Evidence root redirected');
fs.mkdirSync(out); // Never reuse or overwrite evidence, even after a failed run.
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const json = (file, value) => fs.writeFileSync(path.join(out, file), JSON.stringify(value, null, 2) + '\n');
const receipt = {
  schemaVersion: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: options.mainHelpers ? 'C++17 pure Legion motion lease plus exact main helpers with controlled globals and complete selected-Source stub' :
    'C++17 pure Legion motion lease; caller synchronization and lifecycle are external',
  actualHardware: false, actualPbt: false, actualSleep: false, deployment: false,
  sourceContract: {
    fields: ['physicalIdentity', 'captureEpoch', 'sensorIndex'],
    identity: 'nonempty owned string; exact equality', epoch: 'nonzero uint64_t; exact equality',
    sensor: 'unsigned 0 or 1; exact equality',
    publishAndCurrentRequireSelectedSource: true,
    allocation: 'publish/current may allocate when copying Sample.source; not noexcept',
  },
  configurations: [], mutations: [], inputs: [],
};
const save = () => json('summary.json', receipt);
const logResult = (log, result) => {
  fs.writeFileSync(path.join(out, log), (result.stdout || '') + (result.stderr || ''));
  if (result.error) throw result.error;
  assert.equal(result.signal, null, `${log}: unexpected signal`);
  return result;
};
const run = (exe, args, log, cwd = out, env = process.env) => logResult(log,
  spawnSync(exe, args, {cwd, env, encoding: 'utf8', windowsHide: true,
    timeout: 45000, maxBuffer: 8 * 1024 * 1024}));
const requireSuccess = (result, label) => {
  assert.equal(result.status, 0, `${label}: exit ${result.status}; inspect evidence log`);
  return result.stdout;
};
function parseRun(text) {
  const rows = [...text.matchAll(/^CASE ([-a-z0-9]+) checks=(\d+) failures=(\d+)\r?$/gm)]
    .map(m => ({name: m[1], checks: Number(m[2]), failures: Number(m[3])}));
  const total = /^SUMMARY cases=(\d+) checks=(\d+) failures=(\d+) failedCases=(\d+)\r?$/m.exec(text);
  assert(total, 'Executable did not emit semantic summary');
  const parsed = {cases: Number(total[1]), checks: Number(total[2]), failures: Number(total[3]),
    failedCases: Number(total[4]), scenarios: rows};
  assert.equal(rows.length, parsed.cases);
  assert.equal(rows.reduce((n, row) => n + row.checks, 0), parsed.checks);
  assert.equal(rows.reduce((n, row) => n + row.failures, 0), parsed.failures);
  assert.equal(rows.filter(row => row.failures > 0).length, parsed.failedCases);
  assert(rows.every(row => row.checks > 0));
  assert.equal(new Set(rows.map(row => row.name)).size, rows.length);
  return parsed;
}
function replaceOnce(text, from, to) {
  assert.equal(text.split(from).length - 1, 1, `Mutation anchor must occur exactly once: ${from}`);
  return text.replace(from, to);
}

save();
try {
  const fixtureDir = path.join(repo, 'tools/fixtures/legion_motion_source');
  const inputs = [path.join(repo, 'native/legion_motion_source.h'), runner,
    ...fs.readdirSync(fixtureDir).sort().map(name => path.join(fixtureDir, name))];
  if (options.mainHelpers) inputs.push(path.join(repo, 'native/virtual_output_status.h'), path.join(repo, 'native/legion_motion_message_clock.h'));
  const snapshotDir = path.join(out, 'source');
  fs.mkdirSync(snapshotDir);
  for (const file of inputs) {
    assert(fs.statSync(file).isFile(), `Unexpected fixture directory: ${file}`);
    const bytes = fs.readFileSync(file);
    const relative = path.relative(repo, file).replaceAll('\\', '/');
    const frozen = path.join(snapshotDir, relative);
    fs.mkdirSync(path.dirname(frozen), {recursive: true});
    fs.writeFileSync(frozen, bytes);
    receipt.inputs.push({path: relative, sha256: sha(bytes), bytes: bytes.length,
      snapshot: path.relative(out, frozen).replaceAll('\\', '/')});
  }
  const frozenHeader = path.join(snapshotDir, 'native/legion_motion_source.h');
  const frozenHarness = path.join(snapshotDir, 'tools/fixtures/legion_motion_source/harness.cpp');
  const header = fs.readFileSync(frozenHeader, 'utf8');
  const harness = fs.readFileSync(frozenHarness, 'utf8');
  const scenarioNames = [...harness.matchAll(/\{"([-a-z0-9]+)", [A-Za-z0-9_]+\}/g)].map(m => m[1]);
  assert(scenarioNames.length >= 18, 'Scenario inventory incomplete');
  receipt.scenarios = scenarioNames;
  json('source-bindings.json', {inputs: receipt.inputs});
  let mainBinding;
  if (options.mainHelpers) {
    mainBinding = freezeMainHelpers(repo, out);
    receipt.mainHelpers = {status: 'running', sourceSha256: mainBinding.sourceSha256,
      generatedSha256: mainBinding.generatedSha256, sourceAnchorChecks: mainBinding.sourceAnchorChecks,
      controlledStub: 'legionMotionSourceForSelectedLocked returns complete Source; inputCaptureLegionControllerMotionRoute returns controlled eligibility',
      realSelectedSourceProviderExecuted: false, configurations: [], mutations: []};
  }

  let vcvars = options.vcvars;
  if (!vcvars) {
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)',
      'Microsoft Visual Studio/Installer/vswhere.exe');
    const install = requireSuccess(run(vswhere, ['-latest', '-products', '*', '-requires',
      'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
    'toolchain-discovery.log'), 'MSVC discovery').trim();
    assert(install && !/[\r\n]/.test(install), 'Expected one MSVC installation');
    vcvars = path.join(install, 'VC/Auxiliary/Build/vcvars64.bat');
  }
  assert(fs.existsSync(vcvars) && !/[\r\n"%&|<>^!]/.test(vcvars), 'Unsupported MSVC batch path');
  const setup = path.join(out, 'compiler-environment.cmd');
  assert(!/[\r\n"%&|<>^!]/.test(setup), 'Unsupported evidence batch path');
  fs.writeFileSync(setup, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  // Do not log environment values: only compiler identity below is persisted.
  const setupResult = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', setup],
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  if (setupResult.error) throw setupResult.error;
  assert.equal(setupResult.status, 0, 'MSVC environment setup failed');
  const env = {...process.env};
  for (const line of setupResult.stdout.split(/\r?\n/)) {
    const at = line.indexOf('=');
    if (at <= 0) continue;
    const key = line.slice(0, at), old = Object.keys(env).find(k => k.toLowerCase() === key.toLowerCase());
    env[old || key] = line.slice(at + 1);
  }
  // No inherited forced includes, compiler options or linker options.
  for (const name of ['CL', '_CL_', 'LINK', '_LINK_']) {
    const old = Object.keys(env).find(k => k.toLowerCase() === name.toLowerCase());
    if (old) delete env[old];
  }
  const temp = path.join(out, 'compiler-temp');
  fs.mkdirSync(temp);
  for (const name of ['TEMP', 'TMP']) {
    const old = Object.keys(env).find(k => k.toLowerCase() === name.toLowerCase());
    env[old || name] = temp;
  }
  receipt.compiler = {vcvars, vcToolsVersion: env.VCToolsVersion, windowsSdkVersion: env.WindowsSDKVersion};
  const configurations = [
    ['Release', ['/O2', '/DNDEBUG', '/MT']],
    ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd']],
  ];
  const common = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-', '/fp:precise'];
  const build = (dir, flags, includeDir, cpp = frozenHarness, names = scenarioNames, extraIncludes = []) => {
    fs.mkdirSync(dir, {recursive: true});
    const exe = path.join(dir, 'legion_motion_source.exe');
    const args = [...common, ...flags, `/I${includeDir}`, ...extraIncludes.map(p => `/I${p}`), cpp,
      `/Fo${path.join(dir, 'harness.obj')}`, `/Fd${path.join(dir, 'compiler.pdb')}`,
      `/Fe${exe}`, '/link', '/INCREMENTAL:NO'];
    const rel = path.relative(out, dir);
    requireSuccess(run('cl.exe', args, path.join(rel, 'compile.log'), dir, env), `${rel} compilation`);
    const result = run(exe, [], path.join(rel, 'run.log'), dir, env);
    const report = parseRun(result.stdout);
    assert.deepEqual(report.scenarios.map(row => row.name), names);
    return {compileCommand: ['cl.exe', ...args], executableSha256: sha(fs.readFileSync(exe)),
      exitCode: result.status, ...report};
  };
  for (const [name, flags] of configurations) {
    const result = build(path.join(out, name), flags, path.dirname(frozenHeader));
    receipt.configurations.push({name, flags, ...result});
    save();
    assert.equal(result.exitCode, 0, `${name} baseline execution failed`);
    assert.equal(result.failures, 0, `${name} baseline assertions failed`);
    console.log(`${name}: PASS (${result.cases} cases, ${result.checks} checks)`);
  }
  if (options.mainHelpers) {
    const cpp = path.join(snapshotDir, 'tools/fixtures/legion_motion_source/main_harness.cpp');
    const text = fs.readFileSync(cpp, 'utf8');
    const names = [...text.matchAll(/\{"([-a-z0-9]+)", [A-Za-z0-9_]+\}/g)].map(m => m[1]);
    assert(names.length >= 13);
    for (const [name, flags] of configurations) {
      const result = build(path.join(out, 'main-helpers', name), flags, path.dirname(frozenHeader), cpp, names, [out]);
      receipt.mainHelpers.configurations.push({name, flags, ...result});
      save();
      assert.equal(result.exitCode, 0, name + ' main helper execution failed');
      assert.equal(result.failures, 0, name + ' main helper assertions failed');
      console.log(name + ': real main helpers PASS (' + result.cases + ' cases, ' + result.checks + ' checks)');
    }
    const original = fs.readFileSync(path.join(out, 'main_helpers.generated.h'), 'utf8').replaceAll('\r\n', '\n'); // Mutant copies only.
    const helperFaults = [
      {name: 'bypass-publish-queue-cutover',
        from: 'ymcc::legion_motion_clock::belongsToEpoch(sample.capturedAt,\n            g_legionMotionEpochStartedAt.load(std::memory_order_acquire))', to: 'true',
        witnesses: ['main-queue-time-cutover']},
      {name: 'bypass-snapshot-queue-cutover',
        from: '!sample || !ymcc::legion_motion_clock::belongsToEpoch(sample->capturedAt,\n        g_legionMotionEpochStartedAt.load(std::memory_order_acquire))', to: '!sample',
        witnesses: ['main-queue-time-cutover']},
      {name: 'bypass-capture-epoch',
        from: 'captureEpoch == source.captureEpoch', to: 'true',
        witnesses: ['main-capture-epoch-edges']},
      {name: 'revoke-retains-epoch',
        from: 'g_legionMotionCaptureEpoch.store(next, std::memory_order_release);', to: '(void)next;',
        witnesses: ['main-stop-start-same-context', 'main-revoke-no-resurrection']},
    ];
    for (const fault of helperFaults) {
      const altered = replaceOnce(original, fault.from, fault.to);
      const dir = path.join(out, 'main-helper-mutations', fault.name);
      fs.mkdirSync(dir, {recursive: true});
      fs.writeFileSync(path.join(dir, 'main_helpers.generated.h'), altered);
      const row = {name: fault.name, from: fault.from, to: fault.to,
        generatedSha256: sha(altered), witnesses: fault.witnesses, configurations: []};
      receipt.mainHelpers.mutations.push(row);
      for (const [name, flags] of configurations) {
        const result = build(path.join(dir, name), flags, path.dirname(frozenHeader), cpp, names, [dir]);
        row.configurations.push({name, ...result});
        save();
        assert.equal(result.exitCode, 1, fault.name + '/' + name + ': expected assertion failure');
        for (const witness of fault.witnesses) assert(result.scenarios.some(s => s.name === witness && s.failures > 0), witness);
      }
      console.log('Main helper fault detected in Release + Checked: ' + fault.name);
    }
    Object.assign(receipt.mainHelpers, verifyMainHelpers(repo, mainBinding));
    assert(receipt.mainHelpers.selectedHelpersStable && receipt.mainHelpers.sourceAnchorsStable, 'Selected main helpers changed during verification');
    receipt.mainHelpers.status = 'pass';
  }
  const faults = [
    {name: 'bypass-source-identity', from: 'captured.physicalIdentity == currentSource.physicalIdentity', to: 'true',
      witnesses: ['source-physical-identity-change']},
    {name: 'bypass-source-epoch', from: 'captured.captureEpoch == currentSource.captureEpoch', to: 'true',
      witnesses: ['source-capture-epoch-change', 'source-epoch-width-and-ownership']},
    {name: 'bypass-source-sensor', from: 'captured.sensorIndex == currentSource.sensorIndex', to: 'true',
      witnesses: ['source-sensor-change']},
    {name: 'accept-empty-source-identity', from: '!currentSource.physicalIdentity.empty() &&', to: '',
      witnesses: ['invalid-sources']},
    {name: 'accept-zero-source-epoch', from: 'currentSource.captureEpoch != 0 &&', to: '',
      witnesses: ['invalid-sources']},
    {name: 'accept-out-of-range-source-sensor', from: 'currentSource.sensorIndex <= 1 &&', to: '',
      witnesses: ['invalid-sources']},
    {name: 'bypass-power', from: 'current.power != 0 && captured.power == current.power', to: 'true',
      witnesses: ['publish-power-fence', 'current-power-fence']},
    {name: 'bypass-raw-generation', from: 'current.rawGeneration != 0 && captured.rawGeneration == current.rawGeneration',
      to: 'true', witnesses: ['publish-raw-fence', 'current-raw-fence']},
    {name: 'bypass-publish-ready', from: 'if (!ready || !sameContext(sample.context, current) ||',
      to: 'if (false || !sameContext(sample.context, current) ||', witnesses: ['publish-ready-fence']},
    {name: 'bypass-current-ready', from: 'if (!ready || !sample_ || !sameContext(sample_->context, current) ||',
      to: 'if (false || !sample_ || !sameContext(sample_->context, current) ||', witnesses: ['current-ready-fence']},
    {name: 'accept-expired', from: 'if (now - sample_->capturedAt > maxAgeMs) return std::nullopt;',
      to: '(void)maxAgeMs;', witnesses: ['age-boundaries']},
    {name: 'accept-future', from: 'if (sample_->capturedAt > now) return std::nullopt;',
      to: '// Fault: future timestamp admitted.', witnesses: ['future-tick']},
    {name: 'ignore-revoke', from: 'void revoke() noexcept { sample_.reset(); }',
      to: 'void revoke() noexcept {}', witnesses: ['revoke-no-resurrection']},
  ];
  for (const fault of faults) {
    let altered = replaceOnce(header, fault.from, fault.to);
    // The Ready parameter remains intentionally present in the broken API.
    if (fault.name === 'bypass-publish-ready') {
      altered = replaceOnce(altered, 'bool ready) {', 'bool ready) {\n        (void)ready;');
    }
    if (fault.name === 'bypass-current-ready') {
      // Scope the warning-only injection to current(), not inspect() or future methods.
      const signature = 'std::optional<Sample> current(Context current, const Source& currentSource, bool ready,\n' +
        '                                  std::uint64_t now,\n' +
        '                                  std::uint64_t maxAgeMs) const {';
      altered = replaceOnce(altered, signature, signature + '\n        (void)ready;');
    }
    const dir = path.join(out, 'mutations', fault.name);
    fs.mkdirSync(dir, {recursive: true});
    fs.writeFileSync(path.join(dir, 'legion_motion_source.h'), altered);
    const row = {name: fault.name, from: fault.from, to: fault.to, headerSha256: sha(altered),
      witnesses: fault.witnesses, configurations: []};
    receipt.mutations.push(row);
    for (const [name, flags] of configurations) {
      const result = build(path.join(dir, name), flags, dir);
      row.configurations.push({name, ...result});
      save();
      assert.equal(result.exitCode, 1, `${fault.name}/${name}: expected assertion failure, not crash/compile failure`);
      assert(result.failures > 0, `${fault.name}/${name}: fault escaped`);
      for (const witness of fault.witnesses) {
        assert(result.scenarios.some(s => s.name === witness && s.failures > 0),
          `${fault.name}/${name}: missing semantic witness ${witness}`);
      }
    }
    console.log(`Fault detected in Release + Checked: ${fault.name}`);
  }
  if (options.mainHelpers) {
    Object.assign(receipt.mainHelpers, verifyMainHelpers(repo, mainBinding));
    assert(receipt.mainHelpers.selectedHelpersStable && receipt.mainHelpers.sourceAnchorsStable, 'Selected main helpers changed before finalization');
  }
  receipt.sourceStable = receipt.inputs.every(input => sha(fs.readFileSync(path.join(repo, input.path))) === input.sha256);
  assert(receipt.sourceStable, 'Owned sources changed during verification; use fresh evidence');
  receipt.status = 'pass';
  receipt.finishedAt = new Date().toISOString();
  save();
  const report = `# Legion motion lease verification\n\nStatus: PASS\n\n` +
    receipt.configurations.map(c => `- ${c.name}: ${c.cases} cases, ${c.checks} checks, ${c.failures} failures; executable actually compiled and run.`).join('\n') +
    `\n- Semantic faults: ${receipt.mutations.length}, each compiled and detected by assertions in both configurations.\n\n` +
    `Header SHA256: \`${receipt.inputs[0].sha256}\`. All owned source hashes stayed fixed during this run. ` +
    `See source-bindings.json for complete input hashes, source/ for exact bytes, summary.json for commands, executable hashes and semantic witnesses.\n\n` +
    `The header has no timing policy, I/O, threads, system calls or synchronization. The caller provides current power/raw generation, selected physical identity/capture epoch/sensor, Ready, monotonic millisecond ticks, maxAgeMs and its mutex. ` +
    `The 1500ms test window mirrors the existing caller constant; zero and other windows are also tested. ` +
    `current is const and gates access; the lifecycle owner must revoke on retirement. ` +
    `Source identity must be nonempty, captureEpoch nonzero and sensorIndex at most 1; publish/current require exact source equality. A same-context delayed callback after Stop/Start is rejected once the caller supplies its new capture epoch.\n\n` +
    `Not verified by this run: real HID protocol/decoding, PBT delivery, actual sleep/wake, real concurrency/locking, complete main integration, complete controller admission, Host/Steam consumption or whole-product compatibility. ` +
    `No product/device/Steam/service was started and no deployment was performed.\n`;
  const mainReport = receipt.mainHelpers ?
    '\nActual main helper composition: PASS. ' + receipt.mainHelpers.configurations.map(c =>
      c.name + ': ' + c.cases + ' cases, ' + c.checks + ' checks, ' + c.failures + ' failures').join('; ') +
    '. Two helper capture epoch faults compiled and detected in both configurations. Exact function bodies and ' + receipt.mainHelpers.sourceAnchorChecks + ' read-only call-site anchors are frozen in this evidence. Main source SHA256: ' + receipt.mainHelpers.sourceSha256 + '. Selected helpers and anchors stable: true. ' +
    'Globals, phases, complete selected Source resolver, model route eligibility and clock inputs are controlled stand-ins; deterministic interleavings do not prove real concurrent locking, WM_INPUT execution, complete source decoding or controller admission.\n' : '';
  fs.writeFileSync(path.join(out, 'REPORT.md'), report + mainReport);
  const files = [];
  const walk = dir => {
    for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
      const file = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(file);
      else files.push({path: path.relative(out, file).replaceAll('\\', '/'),
        bytes: fs.statSync(file).size, sha256: sha(fs.readFileSync(file))});
    }
  };
  walk(out);
  json('FILES.json', {note: 'Self inventory excluded; hashes cover completed evidence files.', files});
  console.log(`Evidence: ${out}`);
} catch (error) {
  receipt.status = 'fail';
  receipt.finishedAt = new Date().toISOString();
  receipt.error = error.stack || String(error);
  save();
  console.error(receipt.error);
  process.exitCode = 1;
}
