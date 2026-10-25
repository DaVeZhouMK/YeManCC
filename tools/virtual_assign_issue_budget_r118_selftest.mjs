// Source-exact R118 integrated issue-budget sidecar; no product or hardware execution.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract, sha256, normalizeLineEndings} from './fixtures/virtual_assign_issue_budget_r118/source-binding.mjs';
try {
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const root = path.resolve(repo, '../../../Build/Validation/VirtualInput-VirtualIssue-R118-20261016');
const options = {mode: 'candidate', source: null, baseline: path.join(root, 'before/native/main.cpp'), out: path.join(root, 'integrated-issue')};
for (let i = 2; i < process.argv.length; ++i) {
  const arg = process.argv[i];
  if (arg === '--help') { console.log('node tools/virtual_assign_issue_budget_r118_selftest.mjs [--mode baseline|candidate] [--source MAIN_CPP] [--baseline OLD_MAIN_CPP] [--out DIR]\nBaseline red returns 1; compile/infrastructure errors return 2.'); process.exit(0); }
  assert(arg.startsWith('--') && Object.hasOwn(options, arg.slice(2)), 'Unknown ' + arg);
  const value = process.argv[++i]; assert(value && !value.startsWith('--'), 'Missing value');
  options[arg.slice(2)] = arg === '--mode' ? value : path.resolve(value);
}
assert(['baseline', 'candidate'].includes(options.mode));
const sourceFile = options.source ?? (options.mode === 'baseline' ? options.baseline : path.join(repo, 'native/main.cpp'));
const load = file => { const bytes = fs.readFileSync(file); return {file, bytes, text: bytes.toString('utf8'), sha256: sha256(bytes)}; };
const source = load(sourceFile), slices = extract(source.text);
const baselineSource = options.mode === 'baseline' ? source : load(options.baseline);
const baselineExtract = options.mode === 'baseline' ? slices : extract(baselineSource.text);
assert.equal(normalizeLineEndings(slices.parts.WRAPPER), normalizeLineEndings(baselineExtract.parts.WRAPPER), 'R118 unexpectedly changed the actual wrapper/readback/path helper/alias');
assert.equal(normalizeLineEndings(slices.parts.BUDGET), normalizeLineEndings(baselineExtract.parts.BUDGET), 'R118 unexpectedly changed the actual shared budget implementation');
const fixtureRoot = path.join(repo, 'tools/fixtures/virtual_assign_issue_budget_r118');
const fixture = load(path.join(fixtureRoot, 'fixture.cpp')), dependency = load(path.join(repo, 'deps/json/json.hpp'));
const binding = load(path.join(fixtureRoot, 'source-binding.mjs'));
const runner = load(fileURLToPath(import.meta.url));
const modeRoot = path.join(options.out, options.mode); fs.mkdirSync(modeRoot, {recursive: true});
const run = path.join(modeRoot, 'runs', new Date().toISOString().replace(/[:.]/g, '-') + '-' + process.pid);
fs.mkdirSync(run, {recursive: true});
const save = (name, value) => fs.writeFileSync(path.join(run, name), JSON.stringify(value, null, 2) + '\n');
fs.writeFileSync(path.join(run, 'main.snapshot.cpp'), source.bytes);
const inc = path.join(run, 'include'); fs.mkdirSync(inc); fs.writeFileSync(path.join(inc, 'json.hpp'), dependency.bytes);
const extractedDir = path.join(run, 'actual'); fs.mkdirSync(extractedDir);
for (const range of slices.ranges) fs.writeFileSync(path.join(extractedDir, range.name.replace(/[^\w.-]/g, '_') + '.cpp'), range.code);
let cpp = fixture.text;
for (const [name, code] of Object.entries(slices.parts)) {
  const marker = '/*ACTUAL_' + name + '*/'; assert.equal(cpp.split(marker).length, 2, name);
  cpp = cpp.replace(marker, () => code);
}
assert(!/\/\*ACTUAL_\w+\*\//.test(cpp));
fs.writeFileSync(path.join(run, 'fixture.cpp'), cpp);
save('source-bindings.json', {mode: options.mode, source: {file: source.file, sha256: source.sha256},
  ranges: slices.ranges.map(({code, ...range}) => range),
  fixture: {file: fixture.file, sha256: fixture.sha256}, binding: {file: binding.file, sha256: binding.sha256},
  runner: {file: runner.file, sha256: runner.sha256}, dependencies: [{file: dependency.file, sha256: dependency.sha256}],
  productionStatementSubstitutions: [],
  baseline: {file: baselineSource.file, sha256: baselineSource.sha256},
  wrapperAndHelpersUnchangedFromBaseline: true, budgetImplementationUnchangedFromBaseline: true,
  actualScope: 'Complete actual VirtualSlotGuard, actual entire case4 body, entire actual shared budget, actual gates/plan clearing/classifier/masks/refresh helper, complete actual assignment wrapper and readback/path helpers.',
  seams: ['raw proxy availability/GetDevicePath/Get/Set/Refresh', 'cached source/intent/power/topology/plan globals',
    'inert physical cycle leaf', 'inert lifecycle log and identity reprobe recorders', 'inert requeue recorder', 'virtual clock',
    'test driver invokes case4 body directly with its original now local; no full scheduler'],
  notExecuted: ['product', 'Host', 'device', 'Steam', 'driver', 'service', 'power action', 'full build', 'packaging'],
  claimLimit: 'Supplied proxy responses and cached-state transition evidence only; no broad concurrency or physical success claim.'});
const vcvars = process.env.YMCC_VCVARS64 ?? 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars));
const quote = value => { assert(!/["%\r\n]/.test(value)); return '"' + value + '"'; };
function execute(label, executable, args, cwd, timeout) {
  const result = spawnSync(executable, args, {cwd, encoding: 'utf8', windowsHide: true, timeout});
  fs.writeFileSync(path.join(cwd, label + '.stdout.log'), result.stdout ?? '');
  fs.writeFileSync(path.join(cwd, label + '.stderr.log'), result.stderr ?? '');
  fs.writeFileSync(path.join(cwd, label + '.command.json'), JSON.stringify({executable, args, cwd, status: result.status, signal: result.signal, error: result.error?.message}, null, 2) + '\n');
  assert(!result.error, label + ' failed: ' + result.error?.message + '; ' + cwd);
  return result;
}
const profiles = {}; let first = null;
for (const profile of ['Release', 'Checked']) {
  const dir = path.join(run, profile); fs.mkdirSync(dir);
  const flags = profile === 'Release' ? '/O2 /DNDEBUG /MT' : '/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MTd';
  const buildCmd = path.join(dir, 'build.cmd');
  fs.writeFileSync(buildCmd, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\n' +
    'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- ' + flags + ' ' + quote(path.join(run, 'fixture.cpp')) +
    ' /I' + quote(inc) + ' /Fo' + quote(path.join(dir, 'fixture.obj')) + ' /Fe' + quote(path.join(dir, 'fixture.exe')) +
    ' /Fd' + quote(path.join(dir, 'fixture.pdb')) + '\r\nexit /b %errorlevel%\r\n');
  console.log('R118 ' + options.mode + ': compile ' + profile + '; ' + dir);
  const build = execute('build', 'cmd.exe', ['/d', '/c', buildCmd], dir, 60000);
  assert.equal(build.status, 0, 'Compile failure: ' + path.join(dir, 'build.stdout.log'));
  const test = execute('test', path.join(dir, 'fixture.exe'), [], dir, 20000);
  assert(test.status === 0 || test.status === 1, 'Fixture infrastructure error: ' + dir);
  const result = JSON.parse(test.stdout); assert.equal(test.status, result.passed ? 0 : 1);
  fs.writeFileSync(path.join(dir, 'results.json'), test.stdout);
  const {cases, ...summary} = result;
  profiles[profile] = {...summary, directory: dir, flags, matrixSha256: sha256(JSON.stringify(cases)),
    exeSha256: sha256(fs.readFileSync(path.join(dir, 'fixture.exe')))};
  if (first) assert.deepEqual(result, first, 'Release/Checked result mismatch'); else first = result;
  console.log(profile + ': ' + result.caseCount + ' scenarios, ' + result.callerExecutions + ' caller executions, ' +
    result.assertions + ' assertions, ' + result.failedChecks + ' failures (' + result.failedCases + ' cases)');
}
const again = load(source.file), after = extract(again.text);
assert.deepEqual(after.ranges.map(({name, code}) => ({name, code})), slices.ranges.map(({name, code}) => ({name, code})), 'Actual tested scope changed during build');
assert.equal(load(fixture.file).sha256, fixture.sha256, 'Fixture changed during verification');
const failed = first.cases.filter(row => !row.passed).map(row => ({name: row.name, checks: row.checks.filter(check => !check.pass)}));
const result = {passed: Object.values(profiles).every(profile => profile.passed), mode: options.mode, run,
  source: {file: source.file, sha256: source.sha256}, profiles,
  sourceStability: {actualStatementsStable: true, fullSourceStable: source.sha256 === again.sha256, sha256AtEnd: again.sha256},
  productionWrittenByHarness: false, failedCases: failed,
  wrapperAndHelpersUnchangedFromBaseline: true, budgetImplementationUnchangedFromBaseline: true,
  finalChangedPaths: ['tools/virtual_assign_issue_budget_r118_selftest.mjs', 'tools/fixtures/virtual_assign_issue_budget_r118/fixture.cpp',
    'tools/fixtures/virtual_assign_issue_budget_r118/source-binding.mjs', 'tools/fixtures/virtual_assign_issue_budget_r118/README.md']};
save('RESULT.json', result);
fs.writeFileSync(path.join(modeRoot, 'LATEST.json'), JSON.stringify(result, null, 2) + '\n');
fs.writeFileSync(path.join(options.out, 'LATEST.json'), JSON.stringify(result, null, 2) + '\n');
console.log('R118 result: ' + path.join(run, 'RESULT.json'));
if (failed.length) for (const row of failed.slice(0, 6)) console.error(row.name + ': ' + row.checks.map(check => check.name).join('; '));
process.exitCode = result.passed ? 0 : 1;

} catch (error) { console.error(error.stack ?? String(error)); process.exitCode = 2; }
