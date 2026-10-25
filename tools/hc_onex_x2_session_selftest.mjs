// Pure C++ Session tests. No application/native wrapper, Windows API, HID, Host,
// Steam, device/service mutation or real power transitions are executed.
// node tools/hc_onex_x2_session_selftest.mjs [--out <OneXApplication/Session-R3 child>] [--vcvars <batch>]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';
import { sha } from './fixtures/hc_onex_x2_protocol/oracle.mjs';
import { expectations, generatedHeader } from './fixtures/hc_onex_x2_session/expectations.mjs';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const fixtures = path.join(repo, 'tools/fixtures/hc_onex_x2_session');
const protocolFixtures = path.join(repo, 'tools/fixtures/hc_onex_x2_protocol');
let workspace = repo;
while (!fs.existsSync(path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))) {
  const next = path.dirname(workspace);
  if (next === workspace) throw Error('Fixed local HC reference unavailable');
  workspace = next;
}
const options = {}, args = process.argv.slice(2);
for (let i = 0; i < args.length; ++i) {
  if (['--out', '--vcvars'].includes(args[i]) && args[i + 1]) options[args[i].slice(2)] = args[++i];
  else throw Error(`Unknown/incomplete argument: ${args[i]}`);
}
const validation = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/OneXApplication/Session-R3');
const out = path.resolve(options.out || path.join(validation, new Date().toISOString().replace(/[:.]/g, '-')));
const inside = (base, target) => {
  const rel = path.relative(base, target);
  return rel === '' || (!path.isAbsolute(rel) && rel !== '..' && !rel.startsWith(`..${path.sep}`));
};
if (!inside(validation, out)) throw Error('Output must stay under OneXApplication/Session-R3');
fs.mkdirSync(validation, { recursive: true });
const realRoot = fs.realpathSync(validation);
if (!inside(path.resolve(validation), realRoot)) throw Error('Validation root redirected');
let parent = out;
while (!fs.existsSync(parent)) parent = path.dirname(parent);
if (!inside(realRoot, fs.realpathSync(parent))) throw Error('Output parent redirected');
fs.mkdirSync(out, { recursive: true });
if (fs.existsSync(path.join(out, 'source-bindings.json'))) throw Error('Use a fresh output directory to preserve evidence');
const receipt = {
  schemaVersion: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: 'actual pure Session+protocol frozen header copies; independent fixed HC wire oracle and task safety contract',
  hardwareIo: false, windowsApi: false, nativeWrapper: false, host: false, steam: false,
  realSleep: false, network: false, configurations: [], mutations: [],
  writeSet: ['tools/hc_onex_x2_session_selftest.mjs', 'tools/fixtures/hc_onex_x2_session/*'],
};
const save = () => fs.writeFileSync(path.join(out, 'source-bindings.json'), JSON.stringify(receipt, null, 2) + '\n');
save();
const logResult = (relative, result) => {
  fs.writeFileSync(path.join(out, relative), (result.stdout || '') + (result.stderr || ''));
  if (result.error) throw result.error;
  return result;
};
function command(exe, argv, log, cwd = out, env = process.env) {
  const result = logResult(log, spawnSync(exe, argv, {
    cwd, env, encoding: 'utf8', windowsHide: true, timeout: 30000, maxBuffer: 16 * 1024 * 1024,
  }));
  if (result.status !== 0) throw Error(`${log}: exit ${result.status}`);
  return result.stdout;
}
async function compile(argv, log, cwd, env) {
  const result = await new Promise((resolve, reject) => {
    const child = spawn('cl.exe', argv, { cwd, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '';
    const timer = setTimeout(() => { child.kill(); reject(Error(`${log}: compile timeout`)); }, 180000);
    child.stdout.on('data', chunk => stdout += chunk);
    child.stderr.on('data', chunk => stderr += chunk);
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('close', status => { clearTimeout(timer); resolve({ status, stdout, stderr }); });
  });
  logResult(log, result);
  if (result.status !== 0) throw Error(`${log}: compiler exit ${result.status}`);
}
function runHarness(exe, cwd, env, log) {
  const result = logResult(log, spawnSync(exe, [], { cwd, env, windowsHide: true, encoding: 'utf8', timeout: 30000 }));
  let report;
  try { report = JSON.parse(result.stdout.trim()); } catch { throw Error(`${log}: missing structured harness output, exit=${result.status}`); }
  if (!Array.isArray(report.results) || !['pass', 'fail'].includes(report.status) ||
      (report.status === 'pass' ? result.status !== 0 : result.status !== 1))
    throw Error(`${log}: exit/report mismatch`);
  return report;
}
try {
  const headers = ['onex_x2_session.h', 'onex_x2_protocol.h'];
  const originals = headers.map(name => path.join(repo, 'native', name));
  const sources = originals.map(file => fs.readFileSync(file, 'utf8'));
  const auxiliary = [path.join(protocolFixtures, 'oracle.mjs'), path.join(protocolFixtures, 'source-pins.json')];
  receipt.inputs = [runner, ...originals, ...['expectations.mjs', 'harness.cpp', 'README.md', 'framing-pins.json'].map(name => path.join(fixtures, name)), ...auxiliary]
    .map(file => ({ path: file, sha256: sha(fs.readFileSync(file)) }));
  receipt.dependencies = sources.map((source, i) => {
    const quoted = [...source.matchAll(/^\s*#\s*include\s*"([^"]+)"/gm)].map(m => m[1]);
    const system = [...source.matchAll(/^\s*#\s*include\s*<([^>]+)>/gm)].map(m => m[1]);
    const allowed = ['array', 'cstddef', 'cstdint', 'optional', 'string_view'];
    if (system.some(name => !allowed.includes(name)) || JSON.stringify(quoted) !== JSON.stringify(i === 0 ? ['onex_x2_protocol.h'] : []))
      throw Error(`${headers[i]}: pure-header dependency boundary changed`);
    return { header: headers[i], quoted, system };
  });
  const expected = expectations(workspace, protocolFixtures);
  receipt.upstream = {
    commit: expected.protocol.commit, files: expected.protocol.provenance,
    manifestSha256: expected.protocol.manifestSha256, treeSha256: expected.protocol.treeSha256,
    sourcePinsSha256: expected.protocol.sourcePinsSha256,
  };
  receipt.framing = expected.framing;
  receipt.inputs.push(...expected.framing.provenance);
  receipt.expectedSha256 = sha(JSON.stringify(expected));
  receipt.policy = expected.policy;
  receipt.applicationContract = expected.applicationContract;
  fs.writeFileSync(path.join(out, 'expectations.json'), JSON.stringify(expected, null, 2) + '\n');
  fs.writeFileSync(path.join(out, 'expected.generated.h'), generatedHeader(expected));
  for (let i = 0; i < headers.length; ++i) fs.writeFileSync(path.join(out, headers[i]), sources[i]);
  fs.copyFileSync(path.join(fixtures, 'harness.cpp'), path.join(out, 'harness.cpp'));
  save();
  let vcvars = options.vcvars;
  if (!vcvars) {
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
    const install = command(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], 'toolchain-discovery.log').trim();
    vcvars = path.join(install, 'VC/Auxiliary/Build/vcvars64.bat');
  }
  if (!fs.existsSync(vcvars) || /[\r\n"%]/.test(vcvars)) throw Error('Local MSVC environment batch unavailable/unsupported');
  const envCmd = path.join(out, 'compiler-environment.cmd');
  if (/[&|<>^%\r\n"]/.test(envCmd)) throw Error('Unsupported batch path characters');
  fs.writeFileSync(envCmd, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const envResult = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', envCmd], { encoding: 'utf8', windowsHide: true, timeout: 30000 });
  if (envResult.error || envResult.status !== 0) throw Error('MSVC environment setup failed');
  const env = { ...process.env };
  for (const line of envResult.stdout.split(/\r?\n/)) {
    const at = line.indexOf('=');
    if (at <= 0) continue;
    const key = line.slice(0, at), existing = Object.keys(env).find(k => k.toLowerCase() === key.toLowerCase());
    env[existing || key] = line.slice(at + 1);
  }
  const compilerTemp = path.join(out, 'compiler-temp');
  fs.mkdirSync(compilerTemp, { recursive: true });
  for (const key of ['TEMP', 'TMP']) env[Object.keys(env).find(k => k.toLowerCase() === key.toLowerCase()) || key] = compilerTemp;
  receipt.compiler = { vcvars, vcToolsVersion: env.VCToolsVersion, sdkVersion: env.WindowsSDKVersion, language: 'C++17', warningsAsErrors: true };
  const common = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-'];
  for (const [name, flags] of [['Release', ['/O2', '/DNDEBUG', '/MD']], ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MDd']]]) {
    const dir = path.join(out, name);
    fs.mkdirSync(dir, { recursive: true });
    const exe = path.join(dir, 'onex_session.exe');
    const argv = [...common, ...flags, path.join(out, 'harness.cpp'), `/Fo${path.join(dir, 'harness.obj')}`, `/Fd${path.join(dir, 'harness.pdb')}`, `/Fe${exe}`, '/link', '/INCREMENTAL:NO'];
    await compile(argv, `${name}/compile.log`, dir, env);
    const report = runHarness(exe, dir, env, `${name}/run.log`);
    receipt.configurations.push({ name, compileArguments: argv, executableSha256: sha(fs.readFileSync(exe)), ...report });
    save();
    console.log(`${name}: ${report.status.toUpperCase()} (${report.checks} checks, ${report.failedGroups} failed groups)`);
  }
  const controls = [
    { name: 'submission-counts-as-completion', expectedGroup: 'initialization',
      from: 'write_ = Operation{generation_, ++sequence_}; return write_;',
      to: 'write_ = Operation{generation_, ++sequence_}; ++initIndex_; return write_;' },
    { name: 'short-write-accepted', expectedGroup: 'write-failure-backoff',
      from: 'bytes != writeBuffer_.size', to: 'bytes > writeBuffer_.size' },
    { name: 'retirement-frees-pending-storage', expectedGroup: 'retirement-matrix',
      from: 'phase_ = Phase::Retaining; retiredAt_ = now;',
      to: 'read_.reset(); write_.reset(); phase_ = Phase::Retaining; retiredAt_ = now;' },
    { name: 'overflow-silently-drops-event', expectedGroup: 'queue-capacity',
      from: 'if (eventsCount_ == eventCapacity) { retire(StopReason::QueueFull, now); return; }',
      to: 'if (eventsCount_ == eventCapacity) { (void)now; return; }' },
    { name: 'status-skips-final-page-settlement', expectedGroup: 'status-final-page-boundary', exclusive: true,
      from: '            settleInitialization(now); // A status read may precede advance on the exact final-page due tick.',
      to: '            // Fault injection: omit only Status-branch settlement before handling 0x20.' },
    { name: 'redundant-up-observation-lost', expectedGroup: 'rear-observation',
      from: 'else if (!frame->pressed) push({EventKind::RearObserved, frame->button, rearBits_, generation_, now, context_, frame->buttonId}, now);',
      to: '// Fault injection: omit redundant actual UP evidence.' },
    { name: 'redundant-down-observed', expectedGroup: 'rear-observation',
      from: 'else if (!frame->pressed) push({EventKind::RearObserved,',
      to: 'else push({EventKind::RearObserved,' },
    { name: 'discard-prepared-advances-page', expectedGroup: 'discard-prepared-write',
      from: 'write_.reset(); return true;\n    }\n    Cancellation takeCancellation()',
      to: 'write_.reset(); ++initIndex_; return true;\n    }\n    Cancellation takeCancellation()' },
    { name: 'discard-prepared-clears-read', expectedGroup: 'discard-prepared-write',
      from: 'write_.reset(); return true;\n    }\n    Cancellation takeCancellation()',
      to: 'write_.reset(); read_.reset(); return true;\n    }\n    Cancellation takeCancellation()' },
    { name: 'discard-retained-writer', expectedGroup: 'discard-prepared-write',
      from: 'if (!write_ || !(*write_ == operation) || phase_ != Phase::Live || operation.generation != generation_) return false;',
      to: 'if (!write_ || !(*write_ == operation)) return false;' },
  ];
  const mutationResults = await Promise.allSettled(controls.map(async control => {
    if (sources[0].split(control.from).length !== 2) throw Error(`${control.name}: missing unique mutation anchor`);
    const dir = path.join(out, 'mutations', control.name);
    fs.mkdirSync(dir, { recursive: true });
    const mutated = sources[0].replace(control.from, control.to);
    fs.writeFileSync(path.join(dir, 'onex_x2_session.h'), mutated);
    for (const name of ['onex_x2_protocol.h', 'harness.cpp', 'expected.generated.h']) fs.copyFileSync(path.join(out, name), path.join(dir, name));
    const exe = path.join(dir, 'mutant.exe');
    const argv = [...common, '/O2', '/MD', path.join(dir, 'harness.cpp'), `/Fo${path.join(dir, 'harness.obj')}`, `/Fe${exe}`, '/link', '/INCREMENTAL:NO'];
    await compile(argv, `mutations/${control.name}/compile.log`, dir, env);
    const report = runHarness(exe, dir, env, `mutations/${control.name}/run.log`);
    if (report.status !== 'fail' || !report.results.some(row => row.name === control.expectedGroup && row.status === 'fail') ||
        (control.exclusive && report.results.some(row => row.name !== control.expectedGroup && row.status === 'fail')))
      throw Error(`${control.name}: missing intended semantic rejection at ${control.expectedGroup}`);
    return { name: control.name, status: 'detected', expectedGroup: control.expectedGroup, compileArguments: argv,
      mutatedHeaderSha256: sha(mutated), executableSha256: sha(fs.readFileSync(exe)), report };
  }));
  receipt.mutations = mutationResults.map((result, i) => result.status === 'fulfilled' ? result.value :
    { name: controls[i].name, status: 'invalid-control', error: result.reason.message });
  receipt.sourceStability = receipt.inputs.map(input => ({ ...input, finalSha256: sha(fs.readFileSync(input.path)) }));
  receipt.sourceStable = receipt.sourceStability.every(input => input.sha256 === input.finalSha256);
  receipt.status = !receipt.sourceStable ? 'stale-source' : mutationResults.some(r => r.status === 'rejected') ?
    'invalid-mutation' : receipt.configurations.some(r => r.status !== 'pass') ? 'candidate-failed' : 'pass';
  receipt.finishedAt = new Date().toISOString();
  save();
  const failures = receipt.configurations.flatMap(config => config.results.filter(row => row.status === 'fail').map(row => ({ configuration: config.name, ...row })));
  fs.writeFileSync(path.join(out, 'candidate-failures.json'), JSON.stringify({ status: receipt.status, failures }, null, 2) + '\n');
  console.log(`${controls.length} controls: ${receipt.mutations.map(m => `${m.name}=${m.status}`).join(', ')}`);
  console.log(`Evidence: ${path.join(out, 'source-bindings.json')}`);
  if (receipt.status !== 'pass') process.exitCode = 1;
} catch (error) {
  receipt.status = 'error'; receipt.error = error.message; receipt.finishedAt = new Date().toISOString(); save();
  console.error(error.stack || error.message); process.exitCode = 1;
}