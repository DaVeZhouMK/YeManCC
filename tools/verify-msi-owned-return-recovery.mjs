// Offline policy verification only. Builds this selftest and its standalone
// header; never builds/executes the product Host, an OEM SDK, or a device helper.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const script = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(script), '..');
const workspace = path.resolve(repo, '..', '..', '..');
const evidenceRoot = path.join(workspace, 'Build', 'Validation',
  'MSI-PostExport-R109-20261016', 'return-policy');
const historicalEvidenceRoot = path.join(workspace, 'Build', 'Validation',
  'MSI-Field143454-R108-20261016', 'return-policy');
if (process.argv.length > 3)
  throw new Error('Usage: node tools/verify-msi-owned-return-recovery.mjs [output-directory]');
if (process.platform !== 'win32')
  throw new Error('This verifier uses the local MSVC x64 toolchain for Release/Checked.');
let out = path.resolve(process.argv[2] ?? evidenceRoot);
const withinEvidence = [evidenceRoot, historicalEvidenceRoot].some(root => {
  const relative = path.relative(root, out);
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
});
if (!withinEvidence)
  throw new Error('Output must remain within the authorized return-policy evidence root.');
// Preserve previous and concurrent evidence; no cleanup or recursive deletion.
if (fs.existsSync(out) && fs.readdirSync(out).length !== 0) {
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  out = path.join(out, `run-${stamp}-${process.pid}`);
}
fs.mkdirSync(out, { recursive: true });
const save = (name, value) => fs.writeFileSync(path.join(out, name),
  `${JSON.stringify(value, null, 2)}\n`, 'utf8');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const selected = [
  'native/msi_owned_return_recovery.h',
  'tools/msi_owned_return_recovery_selftest.cpp',
  'tools/verify-msi-owned-return-recovery.mjs',
];
const snapshot = path.join(out, 'source');
const sources = selected.map(relativePath => {
  const source = path.join(repo, relativePath);
  const bytes = fs.readFileSync(source);
  const captured = path.join(snapshot, relativePath);
  fs.mkdirSync(path.dirname(captured), { recursive: true });
  fs.writeFileSync(captured, bytes);
  return { path: source, snapshot: captured, sha256: sha(bytes) };
});
save('source-bindings-before.json', sources);

function run(command, args, cwd, env, timeout = 60000) {
  const result = spawnSync(command, args, {
    cwd, env, encoding: 'utf8', windowsHide: true, timeout,
    stdio: ['ignore', 'pipe', 'pipe'], maxBuffer: 8 * 1024 * 1024,
  });
  return {
    command, args, cwd, exitCode: result.status, signal: result.signal,
    error: result.error?.message ?? null,
    stdout: result.stdout ?? '', stderr: result.stderr ?? '',
  };
}

function findVcvars() {
  const installer = path.join(process.env['ProgramFiles(x86)'] ?? 'C:/Program Files (x86)',
    'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
  const candidates = [];
  if (process.env.VSINSTALLDIR) candidates.push(process.env.VSINSTALLDIR);
  if (fs.existsSync(installer)) {
    const found = run(installer, ['-latest', '-products', '*', '-requires',
      'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
    out, process.env);
    save('toolchain-discovery.json', found);
    if (found.exitCode === 0) candidates.push(...found.stdout.trim().split(/\r?\n/).filter(Boolean));
  }
  candidates.push('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools');
  for (const candidate of candidates) {
    const file = path.join(candidate, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
    if (fs.existsSync(file)) return file;
  }
  throw new Error('MSVC x64 vcvars64.bat was not found.');
}

const summary = {
  schemaVersion: 1,
  policy: 'ymcc::msi::ownedReturn::Recovery',
  output: out,
  sources,
  configurations: [],
  sourceStable: false,
  passed: false,
  executionBoundary: {
    kind: 'offline standalone C++ policy',
    realDeviceIo: 0, hostExecuted: false, sdkExecuted: false,
    productionRuntimeLinked: false, threadsCreatedByPolicy: 0,
    devicePermissionGranted: false,
    parentOwns: ['exact owned root/location identity', 'fresh XInput/DInput samples',
      'I/O outcome classification', 'tickets and lifecycle guards', 'all real device I/O',
      'serialized policy access and integer atomic idle/UI mirrors'],
  },
};
try {
  const vcvars = findVcvars();
  // These paths are passed to cmd only to initialize the compiler environment.
  // Reject shell metacharacters rather than interpolating untrusted shell text.
  if (/[\r\n"%&|<>^!]/.test(vcvars) || /[\r\n"%&|<>^!]/.test(out))
    throw new Error('Toolchain/evidence paths contain unsupported cmd metacharacters.');
  const envBatch = path.join(out, 'toolchain-env.cmd');
  fs.writeFileSync(envBatch,
    `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`, 'utf8');
  const init = run(process.env.ComSpec ?? 'cmd.exe', ['/d', '/c', envBatch], out, process.env);
  if (init.error || init.exitCode !== 0)
    throw new Error(`MSVC environment initialization failed: ${init.error ?? init.stderr}`);
  // Do not save the complete environment, which may contain unrelated secrets.
  const compilerEnv = { ...process.env };
  for (const line of init.stdout.split(/\r?\n/)) {
    const equals = line.indexOf('=');
    if (equals > 0) {
      const key = line.slice(0, equals);
      const previous = Object.keys(compilerEnv).find(name => name.toLowerCase() === key.toLowerCase());
      if (previous) delete compilerEnv[previous];
      compilerEnv[key] = line.slice(equals + 1);
    }
  }
  const envValue = name => Object.entries(compilerEnv)
    .find(([key]) => key.toLowerCase() === name.toLowerCase())?.[1];
  const tools = envValue('VCToolsInstallDir');
  if (!tools) throw new Error('VCToolsInstallDir missing from initialized MSVC environment.');
  compilerEnv.VSLANG = '1033';
  const compiler = path.join(tools, 'bin', 'Hostx64', 'x64', 'cl.exe');
  if (!fs.existsSync(compiler)) throw new Error('MSVC x64 compiler was not found.');
  // Eliminate inherited compiler/linker response-file injection and options.
  for (const key of Object.keys(compilerEnv)) {
    if (['cl', '_cl_', 'link', '_link_'].includes(key.toLowerCase())) delete compilerEnv[key];
  }
  save('toolchain.json', { vcvars, compiler, version: envValue('VCToolsVersion'),
    strictFlags: ['/std:c++20', '/W4', '/WX', '/permissive-', '/sdl'],
    inheritedCompilerOptionsRemoved: true });

  for (const configuration of [
    { name: 'Release', flags: ['/O2', '/MT', '/DNDEBUG'] },
    { name: 'Checked', flags: ['/Od', '/MTd', '/Zi', '/RTC1', '/D_DEBUG'] },
  ]) {
    const dir = path.join(out, configuration.name);
    fs.mkdirSync(dir, { recursive: true });
    const exe = path.join(dir, 'msi_owned_return_recovery_selftest.exe');
    const args = ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX',
      '/permissive-', '/Zc:__cplusplus', '/sdl', '/FC', ...configuration.flags,
      path.join(snapshot, 'tools', 'msi_owned_return_recovery_selftest.cpp'),
      `/Fo${path.join(dir, 'selftest.obj')}`, `/Fe${exe}`,
      `/Fd${path.join(dir, 'compiler.pdb')}`,
      '/link', '/INCREMENTAL:NO', '/WX', `/PDB:${path.join(dir, 'selftest.pdb')}`];
    const build = run(compiler, args, dir, compilerEnv);
    fs.writeFileSync(path.join(dir, 'build.log'), build.stdout + build.stderr, 'utf8');
    save(path.join(configuration.name, 'build-command.json'), build);
    const result = { configuration: configuration.name, compileExitCode: build.exitCode,
      runExitCode: null, passed: false, tests: null, error: build.error };
    if (!build.error && build.exitCode === 0) {
      const test = run(exe, [], dir, compilerEnv, 30000);
      fs.writeFileSync(path.join(dir, 'results.txt'), test.stdout + test.stderr, 'utf8');
      save(path.join(configuration.name, 'run-command.json'), test);
      result.runExitCode = test.exitCode;
      result.error = test.error;
      const lastLine = test.stdout.trim().split(/\r?\n/).at(-1);
      try { result.tests = JSON.parse(lastLine); }
      catch { result.error = result.error ?? 'Selftest did not emit its JSON result.'; }
      result.passed = !result.error && test.exitCode === 0 &&
        result.tests?.configuration === configuration.name &&
        result.tests?.tests === 37 && result.tests?.checks > 0 &&
        result.tests?.failures === 0 && result.tests?.realDeviceIo === 0 &&
        result.tests?.hostExecuted === false && result.tests?.sdkExecuted === false;
      if (result.passed) result.executableSha256 = sha(fs.readFileSync(exe));
    }
    summary.configurations.push(result);
    console.log(`${configuration.name}: ${result.passed ? 'PASS' : 'FAIL'}; ` +
      `compile=${result.compileExitCode}, run=${result.runExitCode}, ` +
      `tests=${result.tests?.tests ?? 0}, checks=${result.tests?.checks ?? 0}, ` +
      `failures=${result.tests?.failures ?? 'unknown'}`);
    if (!result.passed) console.error(`See ${path.join(dir, 'build.log')} and results.txt.`);
  }
  summary.sourceStable = sources.every(source => sha(fs.readFileSync(source.path)) === source.sha256);
  summary.passed = summary.sourceStable && summary.configurations.length === 2 &&
    summary.configurations.every(result => result.passed);
} catch (error) {
  summary.error = error instanceof Error ? error.message : String(error);
  console.error(summary.error);
} finally {
  save('validation-summary.json', summary);
}
console.log(`Evidence: ${out}`);
if (!summary.passed) process.exitCode = 1;
