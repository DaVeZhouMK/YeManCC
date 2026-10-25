// Windows x64, pure CPU/header validation. No GamepadMotion.dll, device, Host,
// WinRT, power operation, runtime process, or network input is used.
// node tools/legion_input_protocol_selftest.mjs [--out <fresh ControllerMotion/LegionFamilies/Protocol child>] [--vcvars <batch>]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { derivePrimary } from './fixtures/legion_input_protocol/oracle.mjs';
import { executeCSharpOracle } from './fixtures/legion_input_protocol/execute-oracle.mjs';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const fixtures = path.join(repo, 'tools/fixtures/legion_input_protocol');
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
const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/Protocol');
const out = path.resolve(options.out || path.join(validation, new Date().toISOString().replace(/[:.]/g, '-')));
const inside = (base, target) => {
  const relative = path.relative(base, target);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
};
if (!inside(validation, out) || path.relative(validation, out) === '') throw Error('Evidence requires a fresh ControllerMotion/LegionFamilies/Protocol child');
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
  scope: 'pure logical Legion Tablet/GoS decoder; no physical framing admission', integratedIntoMain: false,
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
  const header = path.join(repo, 'native/legion_input_protocol.h');
  const primary = derivePrimary(workspace, fixtures);
  receipt.primary = { commit: primary.pins.historicalCommit, files: primary.files.map(({text, ...record}) => record),
    minActualBytesDerivedFromSource: primary.minOffsets, tabletMisalignmentShiftsImuInPrimary: false,
    priorYmccImuMinusTwoCorrected: true, decoderTransportScope: 'one caller-normalized logical report; physical framing is not proven by this module' };
  const inputs = [runner, header, path.join(fixtures, 'harness.cpp'), path.join(fixtures, 'README.md'), path.join(fixtures, 'oracle.mjs'),path.join(fixtures,'execute-oracle.mjs'),
    primary.pinsPath, primary.treePath, primary.acquisitionPath, ...primary.files.map(file => file.path)];
  receipt.inputs = inputs.map(file => ({ path: file, sha256: sha(fs.readFileSync(file)) }));
  const source = fs.readFileSync(header, 'utf8');
  const sourceDir = path.join(out, 'sources'); fs.mkdirSync(sourceDir);
  for (const file of inputs) fs.copyFileSync(file, path.join(sourceDir, path.basename(file)));
  fs.writeFileSync(path.join(sourceDir, 'standalone.cpp'), '#include "legion_input_protocol.h"\nint main() { auto result = ymcc::legion_input::decodeLogical(ymcc::legion_input::Protocol::GoS, nullptr, 0, 0, 0); return result ? 1 : 0; }\n');
  receipt.headerSha256 = sha(source); save();

  receipt.csharpOracle = executeCSharpOracle(primary,out,sourceDir,run,success);save();
  console.log('Actual fixed C# source switch/body compiled and executed: '+receipt.csharpOracle.vectors+' bit-oracle vectors');
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
    const executable = path.join(dir, 'legion_input_protocol.exe');
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
    { name:'capacity-instead-of-actual-count',case:'all-short-actual-counts',expected:'short logical actual count was admitted',
      change:s=>replaceOnce(s,'    MotionAxes axes;','    actualBytes = capacity;\n    MotionAxes axes;') },
    { name:'gos-exact-33-whitelist',case:'lengths-not-whitelist',expected:'GoS full core length was incorrectly whitelisted',
      change:s=>replaceOnce(s,'sensorIndex != 0 || actualBytes < 26','sensorIndex != 0 || actualBytes != 33') },
    { name:'tablet-obsolete-all60-gate',case:'lengths-not-whitelist',expected:'Tablet full core length was incorrectly whitelisted',
      change:s=>replaceOnce(s,'if (sensorIndex > 1) return std::nullopt;','if (sensorIndex > 1 || actualBytes < 60) return std::nullopt;') },
    { name:'tablet-byte1-guessed-imu-shift',case:'selected-side-and-byte1',expected:'Tablet byte1 incorrectly shifted left IMU',
      change:s=>replaceOnce(replaceOnce(s,'const std::size_t a = sensorIndex == 0 ? 35u : 48u;','const std::size_t a = (sensorIndex == 0 ? 35u : 48u) - (logical[1] == 0 ? 2u : 0u);'),
        'const std::size_t g = sensorIndex == 0 ? 41u : 54u;','const std::size_t g = (sensorIndex == 0 ? 41u : 54u) - (logical[1] == 0 ? 2u : 0u);') },
    { name:'tablet-endian-swapped',case:'executed-csharp-oracle',expected:'actual C# body bit oracle mismatch:',
      change:s=>replaceOnce(s,'(static_cast<std::uint32_t>(logical[offset]) << 8) | logical[offset + 1]',
        'static_cast<std::uint32_t>(logical[offset]) | (static_cast<std::uint32_t>(logical[offset + 1]) << 8)') },
    { name:'gos-endian-swapped',case:'manual-pins',expected:'GoS little endian hand pin mismatch',
      change:s=>replaceOnce(s,'static_cast<std::uint32_t>(logical[offset]) |\n                    (static_cast<std::uint32_t>(logical[offset + 1]) << 8)',
        '(static_cast<std::uint32_t>(logical[offset]) << 8) | logical[offset + 1]') },
    { name:'right-axis-positive-sign-lost',case:'manual-pins',expected:'right side hand pin mismatch',
      change:s=>replaceOnce(s,'detail::bigEndian16(logical, g)) * detail::gyroScale','detail::bigEndian16(logical, g)) * -detail::gyroScale') },
    { name:'gos-foreign-sensor-accepted',case:'protocol-and-sensor-indices',expected:'GoS sensorIndex other than zero must be rejected',
      change:s=>replaceOnce(s,'sensorIndex != 0 || actualBytes < 26','sensorIndex > 1 || actualBytes < 26') },
    { name:'signed16-decoded-as-unsigned',case:'signed-extremes',expected:'signed int16 extremes or scale mismatch',
      change:s=>replaceOnce(s,'return value >= 0x8000u ? static_cast<int>(value) - 0x10000 : static_cast<int>(value);','return static_cast<int>(value);') },
    { name:'logical-zero-prefix-stripped-again',case:'logical-byte0-is-data',expected:'decoder guessed/removed logical byte0 prefix',
      change:s=>replaceOnce(s,'    MotionAxes axes;','    if (actualBytes && logical[0] == 0) { ++logical; --actualBytes; --capacity; }\n    MotionAxes axes;') },
    { name:'zero-stationary-incorrectly-rejected',case:'zero-stationary',expected:'all-zero stationary logical sample rejected',
      change:s=>replaceOnce(s,'    MotionAxes axes;','    bool any = false; for (std::size_t i = 0; i < actualBytes; ++i) any = any || logical[i] != 0;\n    if (!any) return std::nullopt;\n    MotionAxes axes;') }
  ];
  // Each edit compiles in BOTH configurations. A crash, build failure, or wrong
  // failure message is an invalid control, never counted as a detected mutant.
  for (const control of controls) {
    const mutantRoot = path.join(out, 'mutations', control.name); fs.mkdirSync(mutantRoot, { recursive: true });
    const mutated = control.change(source);
    fs.writeFileSync(path.join(mutantRoot, 'legion_input_protocol.h'), mutated);
    for (const name of ['harness.cpp','oracle.generated.h']) fs.copyFileSync(path.join(sourceDir,name),path.join(mutantRoot,name));
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
  receipt.status = 'pass'; receipt.finishedAt = new Date().toISOString(); save();
  console.log(`PASS: ${controls.length} semantic mutants, both configurations. Evidence: ${out}`);
} catch (error) {
  receipt.status = 'fail'; receipt.error = error.message; receipt.finishedAt = new Date().toISOString(); save();
  console.error(error.stack || error.message); process.exitCode = 1;
}
