import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {isDeepStrictEqual} from 'node:util';
import {extractFunction} from './ymcc_sim/global/controller-startup-return/extend-fixture.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_REPO = path.resolve(HERE, '..');
const MODULE = 'tools/ymcc_sim/global/controller-startup-return';
const FUNCTIONS = ['msiCrashProcessCreation', 'msiCrashCurrentOwner', 'msiCrashOldOwnerDead'];
const INPUTS = ['native/msi_crash_recovery_runtime.h', 'native/msi_crash_recovery_journal.h', 'deps/json/json.hpp',
  'tools/msi_crash_owner_liveness_selftest.cpp', 'tools/msi_crash_owner_liveness_selftest.mjs',
  MODULE + '/extend-fixture.mjs', 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const CASES = [
  ['owner/current-pseudo-handle', 6], ['owner/live-child', 5], ['owner/live-child-creation-mismatch', 3],
  ['owner/current-creation-mismatch', 2], ['owner/exited-child-retained-handle', 6], ['owner/invalid-handle-real-api', 2],
  ['leaf/missing-pid', 2], ['leaf/access-denied', 2], ['leaf/process-times-failure', 2],
  ['leaf/current-creation-failure', 2], ['leaf/wait-failure-matching-creation', 2], ['leaf/wait-failure-creation-mismatch', 2]
];
const FAULTS = {
  'alive-as-dead': {before: 'wait==WAIT_OBJECT_0', after: '(wait==WAIT_OBJECT_0 || wait==WAIT_TIMEOUT)', failures: [
    {case: CASES[0][0], assertion: 'live current owner is not dead'},
    {case: CASES[1][0], assertion: 'live child owner is not dead'}]},
  'drop-creation-comparison': {before: 'creation!=record.ownerCreationTime || ', after: '', failures: [
    {case: CASES[2][0], assertion: 'creation mismatch retires old identity while child lives'},
    {case: CASES[3][0], assertion: 'current PID with old creation is a different owner'},
    {case: CASES[11][0], assertion: 'proven creation mismatch retires old identity despite wait failure'}]}
};
const need = (ok, message) => { if (!ok) throw Error('MSI_OWNER_' + message); };
const hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read = p => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));
const write = (p, data) => fs.writeFileSync(p, JSON.stringify(data, null, 2) + '\n', {flag: 'wx'});
const inside = (root, p) => { const r = path.relative(root, path.resolve(p)); return r !== '' && r !== '..' && !r.startsWith('..' + path.sep) && !path.isAbsolute(r); };
function privateOutput(value) {
  need(typeof value === 'string' && path.isAbsolute(value), 'ABSOLUTE_OUTPUT_REQUIRED');
  // Find the shared workspace even when this script runs from its frozen Build copy.
  let workspace = HERE;
  while (!fs.existsSync(path.join(workspace, 'Mainline/YeManCC-source/YeManCC/AGENTS.md'))) {
    const parent = path.dirname(workspace); need(parent !== workspace, 'WORKSPACE_NOT_FOUND'); workspace = parent;
  }
  const root = path.resolve(value), allowed = path.join(workspace, 'Build/Validation/VirtualController-Lifecycle-20261019');
  // The established public runner places this sidecar in its already frozen
  // startup-return case. Accept only that exact fresh child and case marker.
  const caseRoot = path.dirname(root), marker = path.join(caseRoot, 'input-snapshot.json');
  let publicCase = false;
  if (path.basename(root) === 'o' && path.basename(caseRoot) === 'startup-return' &&
      path.basename(path.dirname(caseRoot)) === 'controller.startup-return' &&
      path.basename(path.dirname(path.dirname(caseRoot))) === 'cases' &&
      inside(path.join(workspace, 'Build'), root) && fs.existsSync(marker)) {
    const captured = read(marker);
    publicCase = captured.schemaVersion === 1 && Array.isArray(captured.sources) &&
      captured.sources.some(r => r.path === 'tools/msi_crash_owner_liveness_selftest.mjs') &&
      captured.sources.some(r => r.path === 'native/msi_crash_recovery_runtime.h');
  }
  need(inside(allowed, root) || publicCase, 'OUTPUT_OUTSIDE_PRIVATE_VALIDATION');
  for (let p = root;; p = path.dirname(p)) {
    if (fs.existsSync(p)) need(!fs.lstatSync(p).isSymbolicLink(), 'OUTPUT_REPARSE');
    if (p === path.dirname(p)) break;
  }
  need(!fs.existsSync(root), 'FRESH_OUTPUT_REQUIRED');
  return root;
}
export function verifyOwnerProfile(raw, {variant = 'original', buildExitCode, executeExitCode} = {}) {
  need(buildExitCode === 0, 'COMPILE_FAILURE_IS_NOT_SEMANTIC_REJECTION');
  need(variant === 'original' || Object.hasOwn(FAULTS, variant), 'UNKNOWN_VARIANT');
  need(raw?.schemaVersion === 1 && raw.tests === CASES.length && raw.assertions === 36 && Array.isArray(raw.cases), 'MISSING_OR_INCOMPLETE_REPORT');
  need(isDeepStrictEqual(raw.cases.map(c => [c.name, c.assertions]), CASES), 'ASSERTIONS_CHANGED');
  const failures = variant === 'original' ? [] : FAULTS[variant].failures;
  need(isDeepStrictEqual(raw.failures, failures), 'WRONG_SEMANTIC_FAILURES');
  need(raw.passed === (failures.length === 0) && executeExitCode === (failures.length ? 1 : 0), 'EXIT_OR_PASS_CONTRADICTION');
  const failed = new Set(failures.map(f => f.case));
  for (const c of raw.cases) {
    need(c.passed === !failed.has(c.name), 'CASE_PASS_CONTRADICTION');
    need(c.boundary === (c.name.startsWith('leaf/') ? 'explicit-error-leaf' : 'private-real-Win32'), 'BOUNDARY_UNLABELLED');
    need(c.name.startsWith('leaf/') ? c.explicitLeafCalls > 0 : c.explicitLeafCalls === 0 && c.realGetProcessTimesCalls > 0, 'REAL_AND_LEAF_EVIDENCE_MIXED');
  }
  need(raw.productRun === false && raw.hardwareAccess === false && raw.foreignProcessQueries === 0 && raw.childNaturalExitCode === 73,
    'PRIVATE_BOUNDARY_OR_CHILD_EXIT_MISSING');
  need(Number.isInteger(raw.currentPid) && Number.isInteger(raw.childPid) && raw.currentPid > 0 && raw.childPid > 0 && raw.currentPid !== raw.childPid &&
    /^\d+$/.test(raw.currentCreation) && /^\d+$/.test(raw.childCreation), 'OWN_PROCESS_IDENTITY_MISSING');
  return {status: failures.length ? 'EXPECTED_SEMANTIC_FAILURE' : 'PASS', tests: raw.tests, assertions: raw.assertions,
    failedCases: [...failed], failures: raw.failures, buildExitCode, executeExitCode, childNaturalExitCode: raw.childNaturalExitCode};
}
export function verifyOwnerLivenessResult(result) {
  need(result?.status === 'PASS' && result.passed === true && result.actualFunctionsVerbatim === true, 'SIDECAR_NOT_COMPLETED');
  need(isDeepStrictEqual(result.functions.map(f => f.name), FUNCTIONS), 'EXTRACTED_FUNCTION_MEMBERSHIP');
  need(isDeepStrictEqual(result.sourceBindings.map(r => r.path), INPUTS), 'SOURCE_MEMBERSHIP_CHANGED');
  const runtime = result.sourceBindings.find(r => r.path === 'native/msi_crash_recovery_runtime.h');
  const extracted = FUNCTIONS.map(name => extractFunction(fs.readFileSync(runtime.snapshot, 'utf8'), name));
  need(isDeepStrictEqual(extracted.map(({code, ...range}) => range), result.functions), 'FUNCTION_RANGE_OR_HASH_CHANGED');
  const exact = extracted.map(f => f.code).join('\n\n') + '\n';
  need(isDeepStrictEqual(Object.keys(result.generated), ['original', ...Object.keys(FAULTS)]), 'GENERATED_VARIANTS_CHANGED');
  for (const [variant, row] of Object.entries(result.generated)) {
    const expected = variant === 'original' ? exact : exact.replace(FAULTS[variant].before, () => FAULTS[variant].after);
    need(fs.readFileSync(row.path, 'utf8') === expected && hash(row.path) === row.sha256, 'EXACT_EXTRACT_OR_MUTATION_CHANGED');
  }
  need(isDeepStrictEqual(Object.keys(result.profiles), ['Release', 'Checked']), 'PROFILES_MISSING');
  for (const profile of Object.values(result.profiles)) {
    need(isDeepStrictEqual(Object.keys(profile), ['original', ...Object.keys(FAULTS)]), 'COMPILED_NEGATIVES_MISSING');
    for (const [variant, row] of Object.entries(profile)) {
      need(hash(row.report.path) === row.report.sha256 && hash(row.executable.path) === row.executable.sha256, 'REPORT_OR_EXECUTABLE_CHANGED');
      need(isDeepStrictEqual(verifyOwnerProfile(read(row.report.path), {variant, buildExitCode: row.buildExitCode, executeExitCode: row.executeExitCode}), row.verification), 'PROFILE_RECEIPT_CHANGED');
    }
  }
  for (const row of result.sourceBindings) need(hash(row.snapshot) === row.sha256, 'SOURCE_SNAPSHOT_CHANGED');
  need(result.compilerInputs.dependencyFiles === 6 && result.compilerInputs.libraries > 0 && result.inputHashesStable === true, 'COMPILER_CLOSURE_MISSING');
  need(result.boundary.privateOwnedProcessesOnly === true && result.boundary.installedOrDriverAcceptance === false && result.originalAssertionsUnchanged === true, 'OVERSTATED_SCOPE');
  return {status: 'PASS', profiles: result.profiles, functions: result.functions, testsPerExecution: CASES.length,
    assertionsPerExecution: 36, compiledSemanticNegatives: Object.keys(FAULTS), inputHashesStable: true};
}
export function runOwnerLiveness({out, repo = DEFAULT_REPO, toolchainFile = path.join(repo, MODULE, 'toolchain-inputs.json')}) {
  need(process.platform === 'win32', 'WINDOWS_REQUIRED');
  const root = privateOutput(out); fs.mkdirSync(root, {recursive: true});
  const steps = [];
  try {
    const snapshot = path.join(root, 'in');
    const sourceBindings = INPUTS.map(relative => {
      const source = path.join(repo, relative), target = path.join(snapshot, relative);
      fs.mkdirSync(path.dirname(target), {recursive: true}); fs.copyFileSync(source, target, fs.constants.COPYFILE_EXCL);
      return {path: relative, source, snapshot: target, sha256: hash(source)};
    });
    need(hash(path.join(repo, 'tools/msi_crash_owner_liveness_selftest.mjs')) === hash(fileURLToPath(import.meta.url)), 'RUNNER_DIFFERS_FROM_FROZEN_SOURCE');
    for (const relative of [MODULE + '/extend-fixture.mjs', 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'])
      need(hash(path.join(repo, relative)) === hash(path.join(DEFAULT_REPO, relative)), 'IMPORTED_HELPER_DIFFERS_FROM_FROZEN_SOURCE');
    const toolchain = read(toolchainFile), toolchainHash = hash(toolchainFile);
    need(toolchain.files.length > 0 && toolchain.files.length <= 640, 'TOOLCHAIN_MEMBERSHIP');
    const node = toolchain.files.find(row => row.roles.includes('node'));
    need(node && path.resolve(node.path).toLowerCase() === path.resolve(process.execPath).toLowerCase(), 'NODE_NOT_PINNED');
    for (const row of toolchain.files) need(hash(row.path) === row.sha256, 'TOOLCHAIN_CHANGED:' + row.path);
    write(path.join(root, 'input-snapshot.json'), {sourceBindings, toolchainFile, toolchainHash, toolchain});
    const source = fs.readFileSync(path.join(snapshot, 'native/msi_crash_recovery_runtime.h'), 'utf8');
    const functions = FUNCTIONS.map(name => extractFunction(source, name));
    const exact = functions.map(f => f.code).join('\n\n') + '\n';
    const generated = {};
    for (const variant of ['original', ...Object.keys(FAULTS)]) {
      const dir = path.join(root, variant); fs.mkdirSync(dir);
      let text = exact;
      if (variant !== 'original') {
        const fault = FAULTS[variant]; need(exact.split(fault.before).length === 2, 'MUTATION_SITE_NOT_UNIQUE');
        text = exact.replace(fault.before, () => fault.after);
      }
      const include = path.join(dir, 'actual-owner-functions.inc'); fs.writeFileSync(include, text, {flag: 'wx'});
      generated[variant] = {path: include, sha256: hash(include), mutation: FAULTS[variant] ?? null};
    }
    write(path.join(root, 'source-binding.json'), {sourceBindings, functions: functions.map(({code, ...range}) => range), generated,
      assertionsSharedByAllVariants: true, businessPredicatesRewritten: false});
    const env = {...process.env};
    for (const key of Object.keys(env)) if (/^(?:CL|_CL_|LINK|_LINK_|NODE_OPTIONS|NODE_PATH|YMCC_VCVARS64)$/i.test(key)) delete env[key];
    const temp = path.join(root, 't'); fs.mkdirSync(temp); Object.assign(env, {TEMP: temp, TMP: temp, VSCMD_SKIP_SENDTELEMETRY: '1'});
    const quote = value => { need(!/["%\r\n]/.test(value), 'UNSAFE_COMPILER_PATH'); return '"' + value + '"'; };
    const preamble = calibrationToolchainPreamble(toolchain.vcvars64);
    const profiles = {}, dependencyFiles = [], compilerRows = [];
    const artifacts = new Map(toolchain.files.map(row => [path.resolve(row.path).toLowerCase(), row.sha256]));
    function bindCompilerInput(file, kind) {
      const resolved = path.resolve(file), sha256 = hash(resolved);
      need(inside(root, resolved) || artifacts.get(resolved.toLowerCase()) === sha256, 'UNPINNED_COMPILER_INPUT:' + resolved);
      compilerRows.push({path: resolved, sha256, kind});
    }
    function execute(label, executable, args, cwd, timeout) {
      const p = spawnSync(executable, args, {cwd, env, windowsHide: true, encoding: 'utf8', timeout, maxBuffer: 8 * 1024 * 1024});
      const stdout = path.join(cwd, label + '.stdout.log'), stderr = path.join(cwd, label + '.stderr.log');
      fs.writeFileSync(stdout, p.stdout ?? ''); fs.writeFileSync(stderr, p.stderr ?? '');
      const command = {executable, args, cwd, exitCode: p.status, signal: p.signal, error: p.error?.message ?? null, stdout, stderr};
      write(path.join(cwd, label + '.command.json'), command); steps.push(command);
      need(!p.error && !p.signal && Number.isInteger(p.status), 'PROCESS_INCOMPLETE:' + label);
      return p;
    }
    let libraries = 0;
    for (const profile of ['Release', 'Checked']) {
      profiles[profile] = {};
      for (const variant of Object.keys(generated)) {
        const dir = path.join(root, variant, profile); fs.mkdirSync(dir);
        const executable = path.join(dir, 'selftest.exe'), dependencies = path.join(dir, 'selftest.cpp.json');
        const flags = ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/permissive-', '/sdl', '/Zc:__cplusplus',
          ...(profile === 'Release' ? ['/O2', '/MT', '/DNDEBUG'] : ['/Od', '/MTd', '/RTC1', '/Zi', '/D_DEBUG']),
          '/I' + quote(path.dirname(generated[variant].path)), '/external:I' + quote(path.join(snapshot, 'deps/json')), '/external:W0',
          '/showIncludes', '/sourceDependencies', quote(dependencies), quote(path.join(snapshot, 'tools/msi_crash_owner_liveness_selftest.cpp')),
          '/Fo' + quote(path.join(dir, 'selftest.obj')), '/Fe' + quote(executable), '/Fd' + quote(path.join(dir, 'selftest.pdb')),
          '/link', '/verbose:lib', '/SUBSYSTEM:CONSOLE', '/MACHINE:x64', '/INCREMENTAL:NO'];
        const batch = path.join(dir, 'build.cmd');
        fs.writeFileSync(batch, preamble + 'where cl\r\nwhere link\r\ncl ' + flags.join(' ') + '\r\nexit /b %errorlevel%\r\n', {flag: 'wx'});
        const build = execute('build', path.join(process.env.SystemRoot || 'C:/Windows', 'System32/cmd.exe'), ['/d', '/c', batch], dir, 60000);
        need(build.status === 0, 'BUILD_FAILED:' + profile + '/' + variant);
        const dep = read(dependencies); need(Array.isArray(dep.Data?.Includes), 'SOURCE_DEPENDENCIES_MISSING');
        dependencyFiles.push(dependencies); bindCompilerInput(dep.Data.Source, 'Compile');
        for (const file of dep.Data.Includes) bindCompilerInput(file, 'Include');
        need(dep.Data.Includes.some(p => path.resolve(p).toLowerCase() === path.resolve(generated[variant].path).toLowerCase()), 'EXTRACTED_INCLUDE_NOT_COMPILED');
        for (const match of build.stdout.matchAll(/([A-Za-z]:\\[^\r\n]*?\.lib)/g)) {
          if (fs.existsSync(match[1])) { bindCompilerInput(match[1], 'Library'); ++libraries; }
        }
        const executed = execute('execute', executable, [], dir, 40000);
        let raw; try { raw = JSON.parse(executed.stdout); } catch { throw Error('MSI_OWNER_EXECUTION_REPORT_MISSING:' + profile + '/' + variant); }
        const report = path.join(dir, 'results.json'); write(report, raw);
        const verification = verifyOwnerProfile(raw, {variant, buildExitCode: build.status, executeExitCode: executed.status});
        profiles[profile][variant] = {buildExitCode: build.status, executeExitCode: executed.status, verification,
          report: {path: report, sha256: hash(report)}, executable: {path: executable, sha256: hash(executable)}};
      }
    }
    need(libraries > 0, 'LIBRARY_CLOSURE_MISSING');
    for (const row of sourceBindings) need(hash(row.source) === row.sha256 && hash(row.snapshot) === row.sha256, 'SOURCE_CHANGED:' + row.path);
    for (const row of toolchain.files) need(hash(row.path) === row.sha256, 'TOOL_CHANGED:' + row.path);
    need(hash(toolchainFile) === toolchainHash, 'TOOLCHAIN_DESCRIPTOR_CHANGED');
    for (const row of Object.values(generated)) need(hash(row.path) === row.sha256, 'GENERATED_SOURCE_CHANGED');
    const compilerInputs = {dependencyFiles: dependencyFiles.length, libraries, rows: compilerRows};
    write(path.join(root, 'evaluated-compiler-inputs.json'), compilerInputs);
    write(path.join(root, 'process-ledger.json'), {steps});
    const result = {schemaVersion: 1, status: 'PASS', passed: true, actualFunctionsVerbatim: true,
      functions: functions.map(({code, ...range}) => range), sourceBindings, profiles, generated, compilerInputs,
      originalAssertionsUnchanged: true, inputHashesStable: true, toolchainArtifacts: toolchain.files.length,
      boundary: {privateOwnedProcessesOnly: true, hiddenChildNaturalExit: true, errorConditions: 'explicit-labelled-leaves; null-handle error also real Win32',
        installedOrDriverAcceptance: false, actualHostSessionParentExitIntegration: false, productOrSteamRun: false,
        devicesServicesHidHide: false, deploymentOrExport: false, terminationOfAnyProcess: false}};
    verifyOwnerLivenessResult(result); write(path.join(root, 'result.json'), result); return result;
  } catch (error) {
    write(path.join(root, 'failure.json'), {status: 'FAIL', reason: error.message, steps, compileFailureAcceptedAsNegative: false}); throw error;
  }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2), options = {};
    need(args.length >= 2 && args.length % 2 === 0, 'ARGUMENTS');
    const keys = {'--out': 'out', '--repo': 'repo', '--toolchain': 'toolchainFile'};
    for (let i = 0; i < args.length; i += 2) { need(Object.hasOwn(keys, args[i]) && !Object.hasOwn(options, keys[args[i]]), 'ARGUMENTS'); options[keys[args[i]]] = args[i + 1]; }
    const result = runOwnerLiveness(options);
    console.log(JSON.stringify({status: result.status, out: options.out, profiles: result.profiles, functions: result.functions, compilerDependencies: result.compilerInputs.dependencyFiles}));
  } catch (error) { console.error(error.stack ?? error.message); process.exitCode = 2; }
}
