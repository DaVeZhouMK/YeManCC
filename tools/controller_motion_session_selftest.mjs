// Windows x64, pure CPU/header validation. No GamepadMotion.dll, device, Host,
// WinRT, power operation, runtime process, or network input is used.
// node tools/controller_motion_session_selftest.mjs [--out <fresh ControllerMotion/LegionConsumer/Session child>] [--vcvars <batch>]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const fixtures = path.join(repo, 'tools/fixtures/controller_motion_session');
let workspace = repo;
while (!fs.existsSync(path.join(workspace, 'Mainline/YeManCC-source/YeManCC/native'))) {
  const parent = path.dirname(workspace);
  if (parent === workspace) throw Error('Cannot locate shared workspace root');
  workspace = parent;
}
const options = {}, args = process.argv.slice(2);
for (let i = 0; i < args.length; ++i) {
  if (['--out', '--vcvars'].includes(args[i]) && args[i + 1]) options[args[i].slice(2)] = args[++i];
  else throw Error(`Unknown/incomplete option: ${args[i]}`);
}
const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionConsumer/Session');
const out = path.resolve(options.out || path.join(validation, new Date().toISOString().replace(/[:.]/g, '-')));
const inside = (base, target) => {
  const relative = path.relative(base, target);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
};
if (!inside(validation, out) || path.relative(validation, out) === '') throw Error('Evidence requires a fresh ControllerMotion/LegionConsumer/Session child');
fs.mkdirSync(validation, { recursive: true });
const realRoot = fs.realpathSync(validation);
if (!inside(validation, realRoot) || !inside(realRoot, validation)) throw Error('Validation root is redirected');
let parent = out;
while (!fs.existsSync(parent)) parent = path.dirname(parent);
if (!inside(realRoot, fs.realpathSync(parent))) throw Error('Evidence parent is redirected');
fs.mkdirSync(out, { recursive: true });
if (fs.existsSync(path.join(out, 'source-bindings.json'))) throw Error('Choose a fresh directory; previous evidence is preserved');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const receipt = { schemaVersion: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: 'standalone Legion controller motion Session, current main Madgwick formula scope', integratedIntoMain: false,
  actualControllerFeatureCompleted: false, hardwareIo: false, dllLoaded: false, network: false,
  callbacksMayThrow: 'unsupported; not tested', configurations: [], mutations: [] };
const save = () => fs.writeFileSync(path.join(out, 'source-bindings.json'), JSON.stringify(receipt, null, 2) + '\n');
save();
function logged(log, result) {
  fs.mkdirSync(path.dirname(path.join(out, log)), { recursive: true });
  fs.writeFileSync(path.join(out, log), (result.stdout || '') + (result.stderr || ''));
  if (result.error) throw result.error;
  return result;
}
function run(exe, argv, log, cwd = out, env = process.env) {
  return logged(log, spawnSync(exe, argv, { cwd, env, windowsHide: true, encoding: 'utf8', timeout: 30000, maxBuffer: 4 * 1024 * 1024 }));
}
function success(result, label) {
  if (result.status !== 0) throw Error(`${label}: exit ${result.status}`);
  return result.stdout;
}
async function compile(argv, log, cwd, env) {
  const result = await new Promise((resolve, reject) => {
    const child = spawn('cl.exe', argv, { cwd, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '';
    const timer = setTimeout(() => { child.kill(); reject(Error(`${log}: compiler timeout`)); }, 180000);
    child.stdout.on('data', value => { stdout += value; });
    child.stderr.on('data', value => { stderr += value; });
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('close', status => { clearTimeout(timer); resolve({ status, stdout, stderr }); });
  });
  success(logged(log, result), log);
}
function replaceOnce(source, from, to) {
  if (source.split(from).length !== 2) throw Error(`Mutation anchor is not unique: ${from}`);
  return source.replace(from, to);
}

try {
  if (process.platform !== 'win32' || process.arch !== 'x64') throw Error('This selftest requires the actual Windows x64 MSVC toolchain');
  const header = path.join(repo, 'native/controller_motion_session.h');
  const ownerHeader = path.join(repo, 'native/controller_motion_processor.h');
  const pinsPath = path.join(fixtures, 'source-pins.json'), pins = JSON.parse(fs.readFileSync(pinsPath, 'utf8'));
  const protocol = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/ProtocolRecovery');
  const treePath = path.join(protocol, 'historical-tree.json'), tree = JSON.parse(fs.readFileSync(treePath, 'utf8'));
  const acquisitionPath = path.join(protocol, 'acquisition-historical.json'), acquisition = JSON.parse(fs.readFileSync(acquisitionPath, 'utf8'));
  const hcPath = path.join(protocol, 'historical-source', pins.gamepad.path);
  const madgwickPath = path.join(workspace, pins.madgwick.archive);
  if (tree.sha !== pins.historicalCommit || acquisition.reference !== pins.historicalCommit) throw Error('Historical primary commit pin mismatch');
  const primaryFiles = [[hcPath, pins.gamepad], [madgwickPath, pins.madgwick]].map(([file, pin]) => {
    const bytes = fs.readFileSync(file), entry = tree.tree.find(entry => entry.path === pin.path);
    const blob = createHash('sha1').update(Buffer.from('blob ' + bytes.length + '\0')).update(bytes).digest('hex');
    if (!entry || entry.sha !== pin.gitBlob || blob !== pin.gitBlob || sha(bytes) !== pin.sha256) throw Error('Primary file hash/blob mismatch: ' + pin.path);
    return { path: file, primaryPath: pin.path, sha256: sha(bytes), gitBlob: blob };
  });
  const hcText = fs.readFileSync(hcPath, 'utf8');
  for (const mapped of ['-InputUtils.deg2rad(calGX)', 'InputUtils.deg2rad(calGY)', '-InputUtils.deg2rad(calGZ)', '-gravX', '-gravZ'])
    if (!hcText.includes(mapped)) throw Error('HC wrapper mapping anchor changed: ' + mapped);
  const madgwickText = fs.readFileSync(madgwickPath, 'utf8');
  if (/if\s*\(\s*(?:DeltaSeconds|SamplePeriod)\s*<=?/.test(madgwickText)) throw Error('HC no-dt-guard reference assumption changed');
  const mainPath = path.join(repo, 'native/main.cpp');
  function extractFormula() {
    const text = fs.readFileSync(mainPath, 'utf8');
    const start = text.indexOf('static float g_ymMadgQ[4]'), update = text.indexOf('static void ymccMadgwickUpdate(', start);
    if (start < 0 || update < 0) throw Error('Current main Madgwick anchors unavailable');
    const open = text.indexOf('{', update); let depth = 1, end = open + 1;
    while (end < text.length && depth) { if (text[end] === '{') ++depth; if (text[end] === '}') --depth; ++end; }
    if (depth) throw Error('Current main formula not balanced');
    return { fragment: text.slice(start, end).replace(/\r\n/g, '\n'), startLine: text.slice(0, start).split('\n').length,
      fileSha256AtRead: sha(fs.readFileSync(mainPath)) };
  }
  const extracted = extractFormula();
  if (!extracted.fragment.includes('if (dtSeconds <= 0.0f) return;')) throw Error('Current main dt guard changed; scope needs review');
  const oracle = '#pragma once\n#include <cmath>\nnamespace reference { struct MainFormula {\n' + extracted.fragment
    .replace('static float g_ymMadgQ[4]', 'float state[4]')
    .replace('static void ymccMadgwickResetOrientation()', 'void reset()')
    .replace('static void ymccMadgwickUpdate(', 'void update(').replaceAll('g_ymMadgQ', 'state') + '\n}; }\n';
  receipt.formula = { path: mainPath, startLine: extracted.startLine, fragmentSha256: sha(extracted.fragment),
    fileSha256AtRead: extracted.fileSha256AtRead, oracleScope: pins.oracleScope, primaryFiles,
    archivedMadgwickNoDtGuard: true, originalMainZeroGradientMayBeNonfinite: true, controllerOnlyExactZeroGradientRepair: true, nonfiniteOutputExplicitlyFlagged: true };
  const inputs = [runner, header, ownerHeader, path.join(fixtures, 'harness.cpp'), path.join(fixtures, 'README.md'), pinsPath,
    hcPath, madgwickPath, treePath, acquisitionPath];
  receipt.inputs = inputs.map(file => ({ path: file, sha256: sha(fs.readFileSync(file)) }));
  const source = fs.readFileSync(header, 'utf8');
  const sourceDir = path.join(out, 'sources'); fs.mkdirSync(sourceDir);
  for (const file of inputs) fs.copyFileSync(file, path.join(sourceDir, path.basename(file)));
  fs.writeFileSync(path.join(sourceDir, 'main_madgwick_oracle.generated.h'), oracle);
  fs.writeFileSync(path.join(sourceDir, 'main_madgwick.fragment.cpp.txt'), extracted.fragment);
  fs.writeFileSync(path.join(sourceDir, 'standalone.cpp'), '#include "controller_motion_session.h"\nint main() { ymcc::controller_motion::Session session; session.revoke(); }\n');
  receipt.headerSha256 = sha(source); save();

  let vcvars = options.vcvars;
  if (!vcvars) {
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
    const install = success(run(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], 'toolchain-discovery.log'), 'VS discovery').trim();
    vcvars = path.join(install, 'VC/Auxiliary/Build/vcvars64.bat');
  }
  if (!fs.existsSync(vcvars) || /[\r\n"%]/.test(vcvars)) throw Error('Local MSVC environment batch is unavailable or has unsupported path characters');
  const environmentCmd = path.join(out, 'compiler-environment.cmd');
  if (/[&|<>^%\r\n"]/.test(environmentCmd)) throw Error('Unsupported batch output path characters');
  fs.writeFileSync(environmentCmd, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  // Do not persist the full inherited environment, which may contain credentials.
  const environmentResult = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', environmentCmd], { cwd: out, windowsHide: true, encoding: 'utf8', timeout: 30000 });
  if (environmentResult.error) throw environmentResult.error;
  const environmentText = success(environmentResult, 'MSVC setup');
  fs.writeFileSync(path.join(out, 'environment-setup-status.log'), 'MSVC environment initialized; inherited values intentionally omitted.\n');
  const env = { ...process.env };
  for (const line of environmentText.split(/\r?\n/)) {
    const at = line.indexOf('='); if (at <= 0) continue;
    const key = line.slice(0, at), existing = Object.keys(env).find(value => value.toLowerCase() === key.toLowerCase());
    env[existing || key] = line.slice(at + 1);
  }
  const compilerTemp = path.join(out, 'compiler-temp'); fs.mkdirSync(compilerTemp);
  for (const key of ['TMP', 'TEMP']) {
    const existing = Object.keys(env).find(value => value.toLowerCase() === key.toLowerCase()); env[existing || key] = compilerTemp;
  }
  receipt.compiler = { vcvars, vcToolsVersion: env.VCToolsVersion, windowsSdkVersion: env.WindowsSDKVersion, target: 'x64', language: 'C++17' };
  const common = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-', '/fp:precise'];
  const configurations = [
    ['Release', ['/O2', '/DNDEBUG', '/MD']],
    ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MDd']]
  ];
  function compileArguments(dir, sourceFile, executable, flags) {
    return [...common, ...flags, sourceFile, `/Fo${path.join(dir, 'harness.obj')}`, `/Fd${path.join(dir, 'compiler.pdb')}`, `/Fe${executable}`, '/link', '/INCREMENTAL:NO'];
  }
  for (const [name, flags] of configurations) {
    const dir = path.join(out, name); fs.mkdirSync(dir);
    const executable = path.join(dir, 'controller_motion_session.exe');
    const argv = compileArguments(dir, path.join(sourceDir, 'harness.cpp'), executable, flags);
    await compile(argv, `${name}/compile.log`, dir, env);
    const output = success(run(executable, [], `${name}/run.log`, dir, env), `${name} harness`);
    const summary = output.match(/^SUMMARY cases=(\d+) checks=(\d+)$/m);
    const caseNames = [...output.matchAll(/^PASS ([\w-]+) checks=\d+$/gm)].map(match => match[1]);
    if (!summary || caseNames.length !== Number(summary[1])) throw Error(`${name}: incomplete semantic output`);
    const standaloneDir = path.join(dir, 'standalone'); fs.mkdirSync(standaloneDir);
    const standalone = path.join(standaloneDir, 'standalone.exe');
    await compile(compileArguments(standaloneDir, path.join(sourceDir, 'standalone.cpp'), standalone, flags), `${name}/standalone/compile.log`, standaloneDir, env);
    success(run(standalone, [], `${name}/standalone/run.log`, standaloneDir, env), `${name} standalone`);
    receipt.configurations.push({ name, status: 'pass', cases: Number(summary[1]), checks: Number(summary[2]), caseNames,
      compileArguments: argv, executableSha256: sha(fs.readFileSync(executable)), standaloneHeader: 'compiled-and-ran' }); save();
    console.log(`${name}: PASS (${summary[1]} cases, ${summary[2]} checks; standalone header compiled and ran)`);
  }

  const controls = [
    { name: 'restore-zero-gradient-division', case: 'exact-aligned-stationary', expected: 'aligned stationary gravity must keep finite orientation',
      change: s => replaceOnce(s, 'if (gradientSquaredNorm == 0.0f)', 'if (gradientSquaredNorm < 0.0f)') },
    { name: 'skip-gyro-integration-on-zero-gradient', case: 'exact-aligned-gyro-integration', expected: 'aligned nonzero gyro must integrate instead of freeze',
      change: s => replaceOnce(s, '        s1 = s2 = s3 = s4 = 0.0f;', '        return;') },
    { name: 'replace-exact-zero-with-broad-gradient-threshold', case: 'madgwick-trajectory', expected: 'Madgwick output differs from main formula and HC gyro/gravity remapping',
      change: s => replaceOnce(s, 'if (gradientSquaredNorm == 0.0f)', 'if (gradientSquaredNorm < 100.0f)') },
    { name: 'first-delta-from-zero', case: 'first-and-repeated-tick', expected: 'delta must use selection/previous processed steady double time',
      change: s => replaceOnce(s, '            previousSeconds_ = nowSeconds;', '            static_cast<void>(nowSeconds); previousSeconds_ = 0;') },
    { name: 'never-advance-previous-process-time', case: 'first-and-repeated-tick', expected: 'delta must use selection/previous processed steady double time',
      change: s => replaceOnce(s, '        previousSeconds_ = nowSeconds;\n        reads_.getCal', '        reads_.getCal') },
    { name: 'clamp-native-delta', case: 'delta-unmodified', expected: 'native dt must be caller difference without clamp/cache/finite gate',
      change: s => replaceOnce(s, 'const float dt = static_cast<float>(result.deltaSeconds);', 'const float dt = result.deltaSeconds > 0.00001 ? static_cast<float>(result.deltaSeconds) : 0.0f;') },
    { name: 'float-clock-before-subtraction', case: 'delta-unmodified', expected: 'delta must use selection/previous processed steady double time',
      change: s => replaceOnce(s, 'result.deltaSeconds = nowSeconds - previousSeconds_;', 'result.deltaSeconds = static_cast<float>(nowSeconds) - static_cast<float>(previousSeconds_);') },
    { name: 'shared-orientation-between-sessions', case: 'independent-sessions', expected: 'sessions shared Madgwick quaternion',
      change: s => replaceOnce(s, '    MotionQuaternion orientation_;', '    inline static MotionQuaternion orientation_;') },
    { name: 'wrong-hc-gravity-axis-sign', case: 'madgwick-trajectory', expected: 'Madgwick output differs from main formula and HC gyro/gravity remapping',
      change: s => replaceOnce(s, '-result.gravity.x, result.gravity.y, -result.gravity.z, dt', 'result.gravity.x, result.gravity.y, -result.gravity.z, dt') },
    { name: 'ignore-read-callback-replacement', case: 'all-callback-replacements', expected: 'every native/read callback change must replace resource',
      change: s => replaceOnce(s, '        if (sameCurrentKey && ops_ == ops && !(reads_ == reads)) {', '        if (false) {') },
    { name: 'wrong-player-factor', case: 'reads-and-requested-spaces', expected: 'player getter/factor must be requested-only 1.41',
      change: s => replaceOnce(s, '&player.x, &player.y, 1.41f', '&player.x, &player.y, 0.125f') },
    { name: 'eager-unrequested-world-read', case: 'reads-and-requested-spaces', expected: 'inactive spaces must not call getters',
      change: s => replaceOnce(s, '        if (request.world) {', '        if (reads_.getWorld) {') },
    { name: 'forget-history-after-read-replacement', case: 'read-replacement-history', expected: 'private Owner reconstruction revived historical token',
      change: s => replaceOnce(s, '        if (!sameCurrentKey && attemptedKeys_.find(key) != attemptedKeys_.end()) {', '        if (false) {') },
    { name: 'do-not-reset-orientation-on-replacement', case: 'key-replacement', expected: 'key replacement must reset orientation even for same native pointer',
      change: s => replaceOnce(s, '        if (!unchanged) {\n            orientation_ = {};', '        if (!unchanged) {') },
    { name: 'pretend-nonfinite-orientation-is-valid', case: 'nonfinite-readback-status', expected: 'nonfinite output must be explicitly marked nonfinite',
      change: s => replaceOnce(s, 'result.orientationFinite = std::isfinite(orientation_.w) && std::isfinite(orientation_.x) &&\n                                   std::isfinite(orientation_.y) && std::isfinite(orientation_.z);', 'result.orientationFinite = true;') }
  ];
  // Each edit compiles in BOTH configurations. A crash, build failure, or wrong
  // failure message is an invalid control, never counted as a detected mutant.
  for (const control of controls) {
    const mutantRoot = path.join(out, 'mutations', control.name); fs.mkdirSync(mutantRoot, { recursive: true });
    const mutated = control.change(source);
    fs.writeFileSync(path.join(mutantRoot, 'controller_motion_session.h'), mutated);
    for (const name of ['harness.cpp', 'controller_motion_processor.h', 'main_madgwick_oracle.generated.h'])
      fs.copyFileSync(path.join(sourceDir, name), path.join(mutantRoot, name));
    const mutation = { name: control.name, status: 'running', expectedCase: control.case, expectedFailure: control.expected,
      mutatedHeaderSha256: sha(mutated), configurations: [] };
    receipt.mutations.push(mutation); save();
    for (const [name, flags] of configurations) {
      const dir = path.join(mutantRoot, name); fs.mkdirSync(dir);
      const executable = path.join(dir, 'mutant.exe'), base = `mutations/${control.name}/${name}`;
      const argv = compileArguments(dir, path.join(mutantRoot, 'harness.cpp'), executable, flags);
      await compile(argv, `${base}/compile.log`, dir, env);
      const result = run(executable, ['--case', control.case], `${base}/run.log`, dir, env);
      const expectedLine = `FAIL ${control.case}: ${control.expected}`;
      if (result.status !== 1 || !result.stderr.includes(expectedLine)) throw Error(`${control.name}/${name}: missing specified semantic rejection`);
      mutation.configurations.push({ name, compiled: true, status: 'detected', exitCode: result.status, failure: expectedLine,
        compileArguments: argv, executableSha256: sha(fs.readFileSync(executable)) }); save();
    }
    mutation.status = 'detected'; save(); console.log(`Mutant: ${control.name} detected in Release + Checked`);
  }
  for (const input of receipt.inputs) if (sha(fs.readFileSync(input.path)) !== input.sha256) throw Error(`Owned input changed during validation: ${input.path}`);
  if (sha(extractFormula().fragment) !== receipt.formula.fragmentSha256) throw Error('Current main Madgwick formula changed during validation');
  receipt.status = 'pass'; receipt.finishedAt = new Date().toISOString(); save();
  console.log(`PASS: ${controls.length} semantic mutants, both configurations. Evidence: ${out}`);
} catch (error) {
  receipt.status = 'fail'; receipt.error = error.message; receipt.finishedAt = new Date().toISOString(); save();
  console.error(error.stack || error.message); process.exitCode = 1;
}
