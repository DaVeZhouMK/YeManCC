/** Bundle and execute existing fan tests unchanged, with child-only IO limits. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { execFile } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
const ownRoot = path.dirname(fileURLToPath(import.meta.url));
const defaultRepo = path.resolve(ownRoot, '../../../../..');
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const hashFile = file => digest(fs.readFileSync(file));
export const FAN_GLOBAL_TESTS = Object.freeze([
  { id: 'startup-self-heal', source: 'tools/fan_startup_cleanup_self_heal_selftest.ts', scope: 'real single-owner automatic successor after exact HC retirement/exit; no physical OEM proof required' },
  { id: 'control-self-heal', source: 'tools/fan_control_self_heal_selftest.ts', scope: 'real lifecycle bounded OEM-error/unknown-receipt recovery and cancellation with inert adapters' },
  { id: 'lifecycle', source: 'tools/fan_host_lifecycle_selftest.ts', scope: 'original lifecycle behavior plus original source contracts' },
  { id: 'wake-progress', source: 'tools/fan_wake_open_progress_selftest.ts', scope: 'real lifecycle wake/Open/OpenEvents progress with inert adapters' },
  { id: 'display-evidence', source: 'tools/fan_control_display_evidence_selftest.ts', scope: 'real bridge display publication and generation resolution;not browser rendering' },
]);
const staticReadInputs = [
  'FanLab/real-host/Program.cs', 'src/bridge/fanHost.ts',
  'src/bridge/fanHost/startupConfiguration.ts',
  'src/bridge/fanHost/deployment.ts',
  'src/bridge/fanHost/capabilityAdmission.ts',
  'src/bridge/api.ts', 'native/main.cpp',
  'deps/handheldcompanion-runtime/source/Devices/MSI/ClawA1M.cs',
  'deps/handheldcompanion-runtime/source/Devices/Lenovo/LegionGo.cs',
  'deps/handheldcompanion-runtime/source/Devices/Lenovo/LegionGoTablet2.cs',
  'deps/handheldcompanion-runtime/source/Devices/ASUS/ROGAlly.cs',
  'FanLab/evidence/HC-DEVICE-MATRIX-BATCH03-20260817.md',
  'src/App.vue', 'src/views/FanView.vue', 'src/views/PowerView.vue',
];
const boundary = Object.freeze({ realHostMain: false, realHostProcess: false, network: false, nativeLibrary: false,
  actualInstallationExercised: false, ecOemHidDriverAccess: false, operatingSystemSleep: false, browserUiExercised: false,
  originalAssertionsPreserved: true, writes: 'fresh-case-owned-evidence-and-temp-only' });
function failure(code, stage, testId = null) { return { code, stage, testId }; }
function readOriginalReport(stdout) {
  let report = null;
  for (const line of stdout.split(/\r?\n/)) {
    try { const value = JSON.parse(line); if (value && typeof value.ok === 'boolean' && Number.isSafeInteger(value.checks)) report = value; } catch { /* ordinary original test progress */ }
  }
  return report;
}
export function auditFanTestResult(row) {
  const findings = [];
  const require = (condition, code, stage) => { if (!condition) findings.push(failure(code, stage, row.id)); };
  require(row.process?.exitCode === 0 && row.process?.timedOut === false && row.process?.launchError === false, 'TEST_PROCESS_DID_NOT_EXIT_ZERO', 'feedback');
  require(row.originalReport?.ok === true && row.originalReport?.checks > 0, 'ORIGINAL_STRUCTURED_REPORT_MISSING_OR_FAILED', 'feedback');
  require(!row.originalReport?.failures?.length && !row.originalReport?.results?.some(item => item.ok === false), 'ORIGINAL_ASSERTION_FAILED', 'state');
  require(row.runtimeBoundary?.status === 'PASS' && Array.isArray(row.runtimeBoundary.denied) && row.runtimeBoundary.denied.length === 0, 'FORBIDDEN_IO_OR_BOUNDARY_RECEIPT_MISSING', 'consumer');
  require(Array.isArray(row.bundleInputs) && row.bundleInputs.some(item => item.path === row.source), 'ACTUAL_ENTRY_BUNDLE_INPUT_MISSING', 'consumer');
  require(row.bundleInputs?.some(item => item.path === 'src/bridge/fanHost.ts'), 'REAL_PRODUCT_LIFECYCLE_NOT_BUNDLED', 'consumer');
  require(row.bundleInputs?.every(item => /^[a-f0-9]{64}$/.test(item.sha256 ?? '') && item.hashCapturedFromActualLoad === true), 'ACTUAL_BUNDLE_INPUT_HASH_MISSING', 'consumer');
  require(row.sourceStable === true, 'SOURCE_CHANGED_DURING_TEST', 'consumer');
  return findings;
}
async function existingOwners(repoRoot) {
  const { getAdapters } = await import(pathToFileURL(path.join(repoRoot, 'tools/ymcc_sim/global/adapters.mjs')));
  const { getExtraAdapters } = await import(pathToFileURL(path.join(repoRoot, 'tools/ymcc_sim/global/extra-adapters.mjs')));
  const adapters = [...getAdapters(repoRoot), ...getExtraAdapters()];
  return { reviewedBaseAndExtraCount: adapters.length, registrations: FAN_GLOBAL_TESTS.map(test => ({ source: test.source,
    directExecutionOwners: adapters.filter(adapter => adapter.path === test.source || adapter.args?.includes(test.source)).map(adapter => adapter.id) })),
    note: 'Evidence-input references and package scripts alone do not count as execution;this wrapper is not its own duplicate.' };
}
async function runCase({ repoRoot, caseRoot, test, timeoutMs, compiler }) {
  fs.mkdirSync(caseRoot, { recursive: false });
  const scratch = path.join(caseRoot, 'tmp'); fs.mkdirSync(scratch);
  const row = { id: test.id, source: test.source, scope: test.scope, status: 'FAIL', diagnostics: [], bundleInputs: [],
    staticSourceInputs: [], sourceStable: false, originalReport: null, runtimeBoundary: null, boundary: { ...boundary } };
  const compiledInputs = new Map();
  const controlFiles = ['package.json', 'tsconfig.json', 'tools/ymcc_sim/global/consumer/fan-global/run.mjs', 'tools/ymcc_sim/global/consumer/fan-global/runtime-guard.mjs'];
  let controls = [];
  try {
    controls = controlFiles.map(relative => ({ path: relative, sha256: hashFile(path.join(repoRoot, relative)) }));
    row.staticSourceInputs = (test.id === 'lifecycle' ? staticReadInputs : []).map(relative => ({ path: relative, sha256: hashFile(path.join(repoRoot, relative)), execution: 'original static assertion/read input;not runtime execution' }));
    if (!fs.existsSync(path.join(repoRoot, test.source))) { row.diagnostics.push(failure('ORIGINAL_TEST_SOURCE_MISSING', 'input', test.id)); return row; }
    const bundleFile = path.join(caseRoot, 'test.mjs');
    const loader = file => /\.[cm]?tsx?$/.test(file) ? 'ts' : /\.jsx$/.test(file) ? 'jsx' : /\.json$/.test(file) ? 'json' : 'js';
    const build = await compiler.build({ absWorkingDir: repoRoot, entryPoints: [test.source], bundle: true, platform: 'node', format: 'esm', target: 'node22',
      alias: { '@': path.join(repoRoot, 'src') }, outfile: bundleFile, metafile: true, logLevel: 'silent',
      plugins: [{ name: 'capture-exact-compiler-inputs-without-rewriting', setup(api) {
        api.onLoad({ filter: /\.(?:[cm]?[jt]sx?|json)$/ }, args => {
          const absolute = fs.realpathSync(args.path); const bytes = fs.readFileSync(absolute);
          compiledInputs.set(absolute, { absolutePath: absolute, path: path.relative(repoRoot, absolute).replaceAll('\\', '/'), sha256: digest(bytes), hashCapturedFromActualLoad: true });
          return { contents: bytes, loader: loader(args.path), resolveDir: path.dirname(args.path) };
        });
      } }],
    });
    for (const input of Object.keys(build.metafile.inputs)) {
      const absolute = fs.realpathSync(path.resolve(repoRoot, input));
      const captured = compiledInputs.get(absolute);
      if (!captured) throw new Error('BUNDLE_INPUT_HASH_NOT_CAPTURED');
      row.bundleInputs.push(captured);
    }
    row.bundle = { path: bundleFile, sha256: hashFile(bundleFile), compiler: 'esbuild', compilerVersion: compiler.version, format: 'esm', originalSourceTransformedOnlyByCompiler: true };
    fs.writeFileSync(path.join(caseRoot, 'bundle-metafile.json'), JSON.stringify(build.metafile, null, 2));
    fs.writeFileSync(path.join(caseRoot, 'bundle-inputs.json'), JSON.stringify(row.bundleInputs, null, 2));
    const guardReceipt = path.join(caseRoot, 'runtime-boundary.json');
    const configuration = path.join(caseRoot, 'boundary-configuration.json');
    fs.writeFileSync(configuration, JSON.stringify({ caseRoot, receiptPath: guardReceipt,
      readFiles: [...row.bundleInputs.map(item => item.absolutePath), ...row.staticSourceInputs.map(item => path.join(repoRoot, item.path)), ...controls.map(item => path.join(repoRoot, item.path))] }));
    const environment = { ...process.env, TEMP: scratch, TMP: scratch, TMPDIR: scratch, LOCALAPPDATA: path.join(caseRoot, 'local-appdata'),
      APPDATA: path.join(caseRoot, 'appdata'), YMCC_FAN_GLOBAL_BOUNDARY: configuration };
    delete environment.NODE_OPTIONS; delete environment.NODE_PATH;
    const startedAt = new Date().toISOString();
    const processResult = await new Promise(resolve => {
      const child = execFile(process.execPath, ['--import', pathToFileURL(path.join(ownRoot, 'runtime-guard.mjs')).href, bundleFile],
        { cwd: repoRoot, env: environment, windowsHide: true, timeout: timeoutMs, maxBuffer: 8 * 1024 * 1024 }, (error, stdout, stderr) =>
          resolve({ error, stdout, stderr, pid: child.pid }));
    });
    fs.writeFileSync(path.join(caseRoot, 'stdout.log'), processResult.stdout);
    fs.writeFileSync(path.join(caseRoot, 'stderr.log'), processResult.stderr);
    row.process = { pid: processResult.pid, executable: process.execPath, startedAt, finishedAt: new Date().toISOString(),
      exitCode: processResult.error ? Number.isInteger(processResult.error.code) ? processResult.error.code : null : 0,
      timedOut: Boolean(processResult.error?.killed), launchError: Boolean(processResult.error && !Number.isInteger(processResult.error.code) && !processResult.error.killed),
      signal: processResult.error?.signal ?? null, naturalExit: !processResult.error, nodeVersion: process.version };
    row.originalReport = readOriginalReport(processResult.stdout);
    if (fs.existsSync(guardReceipt)) row.runtimeBoundary = JSON.parse(fs.readFileSync(guardReceipt, 'utf8'));
    row.sourceStable = [...row.bundleInputs, ...row.staticSourceInputs, ...controls].every(item => {
      try { return hashFile(item.absolutePath ?? path.join(repoRoot, item.path)) === item.sha256; } catch { return false; }
    });
    row.diagnostics.push(...auditFanTestResult(row));
    row.status = row.diagnostics.length ? 'FAIL' : 'PASS';
  } catch (error) {
    row.diagnostics.push(failure(error.message === 'BUNDLE_INPUT_HASH_NOT_CAPTURED' ? error.message : error.code === 'ENOENT' ? 'REQUIRED_SOURCE_OR_DEPENDENCY_MISSING' : 'BUNDLE_OR_EXECUTION_FAILED', 'consumer', test.id));
    row.errorType = error.name;
  } finally {
    row.controlSourceInputs = controls;
    fs.writeFileSync(path.join(caseRoot, 'case-result.json'), JSON.stringify(row, null, 2));
  }
  return row;
}
export async function runFanGlobalRegressions({ repoRoot = defaultRepo, evidenceRoot, output, timeoutMs = 60000, tests = FAN_GLOBAL_TESTS.map(test => test.id) } = {}) {
  const root = fs.realpathSync(repoRoot);
  if (typeof evidenceRoot !== 'string' || !path.isAbsolute(evidenceRoot)) throw new Error('EXPLICIT_ABSOLUTE_FRESH_EVIDENCE_ROOT_REQUIRED');
  const evidence = path.resolve(evidenceRoot);
  if (fs.existsSync(evidence)) throw new Error('FRESH_EVIDENCE_ROOT_REQUIRED');
  if (!Array.isArray(tests) || !tests.length || tests.some(id => !FAN_GLOBAL_TESTS.some(test => test.id === id)) || new Set(tests).size !== tests.length) throw new Error('REVIEWED_TEST_SELECTION_REQUIRED');
  fs.mkdirSync(evidence, { recursive: true });
  const startedAt = new Date().toISOString();
  const result = { schemaVersion: 1, kind: 'fan-global-original-regressions', status: 'FAIL', scope: 'original-tests-executing-real-product-with-inert-boundaries',
    startedAt, evidenceRoot: evidence, cases: [], boundary: { ...boundary }, sourceChanges: [],
    remainingGaps: ['physical-hardware-not-exercised', 'rendered-FanView-not-exercised', 'HC-or-Host-Main-not-loaded'] };
  let compiler;
  try {
    result.duplicationReview = await existingOwners(root);
    compiler = createRequire(path.join(root, 'package.json'))('esbuild');
    for (const test of FAN_GLOBAL_TESTS.filter(test => tests.includes(test.id)))
      result.cases.push(await runCase({ repoRoot: root, caseRoot: path.join(evidence, test.id), test, timeoutMs, compiler }));
    result.status = result.cases.length === tests.length && result.cases.every(row => row.status === 'PASS') ? 'PASS' : 'FAIL';
  } catch (error) { result.failure = error.code === 'MODULE_NOT_FOUND' ? 'EXISTING_COMPILER_DEPENDENCY_MISSING' : 'FAN_GLOBAL_ENTRY_FAILED'; }
  finally { compiler?.stop(); }
  const sourceMap = new Map();
  result.diagnostics = [];
  for (const row of result.cases) for (const input of [...row.bundleInputs, ...row.staticSourceInputs, ...(row.controlSourceInputs ?? [])]) {
    const absolute = input.absolutePath ?? path.join(root, input.path);
    const key = path.relative(root, absolute).replaceAll('\\', '/');
    const prior = sourceMap.get(key);
    if (prior && prior.sha256 !== input.sha256) result.diagnostics.push(failure('MIXED_SOURCE_VERSIONS_ACROSS_TESTS', 'consumer', row.id));
    sourceMap.set(key, { path: key, sha256: input.sha256, absolute });
  }
  for (const row of sourceMap.values()) {
    try { if (hashFile(row.absolute) !== row.sha256) result.diagnostics.push(failure('SOURCE_CHANGED_BEFORE_FINAL_RESULT', 'consumer')); }
    catch { result.diagnostics.push(failure('SOURCE_MISSING_BEFORE_FINAL_RESULT', 'consumer')); }
  }
  result.sourceHashes = [...sourceMap.values()].filter(row => row.path !== '..' && !row.path.startsWith('../') && !path.isAbsolute(row.path)).map(({ path, sha256 }) => ({ path, sha256 }));
  result.externalDependencyInputs = [...sourceMap.values()].filter(row => row.path === '..' || row.path.startsWith('../')).map(({ absolute, sha256 }) => ({ path: absolute, sha256 }));
  if (result.diagnostics.length) result.status = 'FAIL';
  result.summary = { requested: tests.length, executed: result.cases.filter(row => row.process).length, passed: result.cases.filter(row => row.status === 'PASS').length,
    failed: tests.length - result.cases.filter(row => row.status === 'PASS').length, originalChecks: result.cases.reduce((count, row) => count + (row.originalReport?.checks ?? 0), 0),
    hardwareCalls: 0, realHostStarts: 0 };
  result.finishedAt = new Date().toISOString();
  const destination = output ? path.resolve(output) : path.join(evidence, 'fan-global-result.json');
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  fs.writeFileSync(destination, JSON.stringify(result, null, 2));
  return result;
}
export async function main(args = process.argv.slice(2)) {
  const options = {};
  const keys = new Map([['--repo-root', 'repoRoot'], ['--evidence-root', 'evidenceRoot'], ['--output', 'output'], ['--test', 'tests']]);
  for (let i = 0; i < args.length; i++) {
    if (!keys.has(args[i]) || typeof args[i + 1] !== 'string') throw new Error('EXPLICIT_REVIEWED_ARGUMENTS_REQUIRED');
    const key = keys.get(args[i]); options[key] = key === 'tests' ? args[++i].split(',') : args[++i];
  }
  const result = await runFanGlobalRegressions(options);
  console.log(JSON.stringify({ status: result.status, summary: result.summary }));
  return result.status === 'PASS' ? 0 : 1;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().then(code => { process.exitCode = code; }, error => { console.error(error.message); process.exitCode = 1; });

