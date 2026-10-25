// R117 bounded assignment-wrapper verification; never loads the product or a proxy DLL.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract, sha256} from './fixtures/openxinput_assign_guard_r117/source-binding.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const validation = path.resolve(repo, '../../../Build/Validation/VirtualInput-PhysicalGuard-R117-20261016');
const defaults = {
  source: path.join(repo, 'native/main.cpp'),
  baseline: path.join(validation, 'before/native/main.cpp'),
  out: path.join(validation, 'assign-issue'),
};
const options = {...defaults};
for (let i = 2; i < process.argv.length; ++i) {
  const argument = process.argv[i];
  if (argument === '--help') {
    console.log('node tools/openxinput_assign_guard_r117_selftest.mjs [--source MAIN_CPP] [--baseline OLD_MAIN_CPP] [--out EVIDENCE_DIR]');
    process.exit(0);
  }
  const name = argument === '--current' ? 'source' : argument.slice(2);
  assert(argument.startsWith('--') && Object.hasOwn(options, name), 'Unknown option: ' + argument);
  assert(process.argv[i + 1] && !process.argv[i + 1].startsWith('--'), 'Missing path: ' + argument);
  options[name] = path.resolve(process.argv[++i]);
}
assert.notEqual(options.source.toLowerCase(), options.baseline.toLowerCase(), 'Baseline must be a separately saved source');
const load = file => { const bytes = fs.readFileSync(file); return {file, bytes, text: bytes.toString('utf8'), sha256: sha256(bytes)}; };
const sources = {baseline: load(options.baseline), candidate: load(options.source)};
const extracted = Object.fromEntries(Object.entries(sources).map(([name, source]) => [name, extract(source.text)]));
assert(!extracted.baseline.ready, 'Baseline must contain the actual original three-argument wrapper');
assert(extracted.candidate.ready, 'Candidate guard patch is not present; wait for the production patch before running');
const fixturePath = path.join(repo, 'tools/fixtures/openxinput_assign_guard_r117/fixture.cpp');
const fixture = load(fixturePath);
const dependency = load(path.join(repo, 'deps/json/json.hpp'));
const runner = load(fileURLToPath(import.meta.url));
const binding = load(path.join(path.dirname(fixturePath), 'source-binding.mjs'));
const vcvars = process.env.YMCC_VCVARS64 ?? 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars), 'Missing vcvars64; set YMCC_VCVARS64');
fs.mkdirSync(options.out, {recursive: true});
const run = path.join(options.out, 'runs', new Date().toISOString().replace(/[:.]/g, '-') + '-' + process.pid);
fs.mkdirSync(run, {recursive: true});
const save = (relative, value) => fs.writeFileSync(path.join(run, relative), JSON.stringify(value, null, 2) + '\n');
const include = path.join(run, 'include'); fs.mkdirSync(include);
fs.writeFileSync(path.join(include, 'json.hpp'), dependency.bytes);
let cpp = fixture.text;
for (const [name, source] of Object.entries(sources)) {
  const directory = path.join(run, name); fs.mkdirSync(directory);
  fs.writeFileSync(path.join(directory, 'main.snapshot.cpp'), source.bytes);
  for (const range of extracted[name].ranges) fs.writeFileSync(path.join(directory, range.name + '.actual.cpp'), range.code);
  const marker = '/*ACTUAL_' + name.toUpperCase() + '*/';
  assert.equal(cpp.split(marker).length, 2, 'Exactly one source marker required: ' + marker);
  cpp = cpp.replace(marker, () => extracted[name].code);
}
assert(!/\/\*ACTUAL_[A-Z]+\*\//.test(cpp), 'Unexpanded fixture marker');
fs.writeFileSync(path.join(run, 'fixture.cpp'), cpp);
save('source-bindings.json', {
  sources: Object.fromEntries(Object.entries(sources).map(([name, source]) => [name, {
    file: source.file, sha256: source.sha256,
    ranges: extracted[name].ranges.map(({code, ...range}) => range),
  }])),
  fixture: {file: fixture.file, sha256: fixture.sha256},
  runner: {file: runner.file, sha256: runner.sha256},
  binding: {file: binding.file, sha256: binding.sha256},
  dependencies: [{file: dependency.file, sha256: dependency.sha256}],
  productionStatementSubstitutions: [],
  extracted: 'Entire actual assignment wrappers, actual readback helpers and constants; candidate callback alias also comes verbatim from source.',
  seams: ['proxy availability', 'raw SetUserIndex/GetUserIndex/RefreshMappings function pointers',
    'path-to-JSON conversion for an ASCII fixture path', 'inert lifecycle logger', 'inert identity reprobe recorder',
    'callback context and admission revoked synchronously inside a selected fake call'],
  scope: 'Only assignment wrapper and its readback helper; no outer physical-slot guard or hardware/product acceptance.',
  notExecuted: ['product', 'Host', 'Steam', 'device', 'service', 'power action', 'full build', 'packaging'],
});
function quote(value) { assert(!/["\r\n%]/.test(value), 'Unsupported command path character'); return '"' + value + '"'; }
function invoke(label, executable, args, cwd, timeout) {
  const result = spawnSync(executable, args, {cwd, encoding: 'utf8', timeout, windowsHide: true});
  fs.writeFileSync(path.join(cwd, label + '.stdout.log'), result.stdout ?? '');
  fs.writeFileSync(path.join(cwd, label + '.stderr.log'), result.stderr ?? '');
  fs.writeFileSync(path.join(cwd, label + '.command.json'), JSON.stringify({executable, args, cwd,
    exitCode: result.status, signal: result.signal, error: result.error?.message}, null, 2) + '\n');
  assert(!result.error, label + ': ' + result.error?.message + '; logs in ' + cwd);
  return result;
}
const profiles = {};
let firstResult = null;
for (const profile of ['Release', 'Checked']) {
  const directory = path.join(run, profile); fs.mkdirSync(directory);
  const flags = profile === 'Release' ? '/O2 /DNDEBUG /MT' : '/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MTd';
  const commandPath = path.join(directory, 'build.cmd');
  fs.writeFileSync(commandPath, '@echo off\r\ncall ' + quote(vcvars) + ' >nul\r\nif errorlevel 1 exit /b 1\r\n' +
    'cl /nologo /std:c++20 /utf-8 /EHsc /permissive- /W4 /WX ' + flags + ' ' + quote(path.join(run, 'fixture.cpp')) +
    ' /I' + quote(include) + ' /Fo' + quote(path.join(directory, 'fixture.obj')) +
    ' /Fe' + quote(path.join(directory, 'fixture.exe')) + ' /Fd' + quote(path.join(directory, 'fixture.pdb')) +
    '\r\nexit /b %errorlevel%\r\n');
  console.log('R117 assignment wrapper: compile ' + profile + '; evidence ' + directory);
  const build = invoke('build', 'cmd.exe', ['/d', '/c', commandPath], directory, 60000);
  assert.equal(build.status, 0, 'Compilation failed; see ' + path.join(directory, 'build.stdout.log'));
  const test = invoke('test', path.join(directory, 'fixture.exe'), [], directory, 20000);
  assert(test.status === 0 || test.status === 1, 'Fixture execution failed; see ' + directory);
  const results = JSON.parse(test.stdout);
  assert.equal(test.status, results.passed ? 0 : 1, 'Fixture exit status disagrees with result');
  fs.writeFileSync(path.join(directory, 'results.json'), test.stdout);
  const {cases, ...summary} = results;
  profiles[profile] = {...summary, flags, directory, matrixSha256: sha256(JSON.stringify(cases)),
    exeSha256: sha256(fs.readFileSync(path.join(directory, 'fixture.exe'))),
    resultsSha256: sha256(test.stdout)};
  if (firstResult) assert.deepEqual(results, firstResult, 'Release and Checked observable results differ');
  else firstResult = results;
  console.log(profile + ': ' + results.caseCount + ' scenarios, ' + results.executions + ' wrapper calls, ' +
    results.assertions + ' assertions, ' + results.failedChecks + ' failed');
  if (!results.passed) {
    for (const row of cases.filter(row => !row.passed)) console.error(row.name + ': ' +
      row.checks.filter(check => !check.pass).map(check => check.name).join('; '));
  }
}
const stability = {};
for (const [name, source] of Object.entries(sources)) {
  const current = load(source.file), ranges = extract(current.text).ranges;
  assert.deepEqual(ranges.map(({name: rangeName, code}) => ({name: rangeName, code})),
    extracted[name].ranges.map(({name: rangeName, code}) => ({name: rangeName, code})),
    'Actual source extract changed during verification: ' + source.file);
  stability[name] = {extractedStatementsUnchanged: true, fullSourceUnchanged: current.sha256 === source.sha256,
    fullSourceSha256AtEnd: current.sha256};
}
const result = {passed: Object.values(profiles).every(profile => profile.passed), run,
  candidate: {file: sources.candidate.file, sha256: sources.candidate.sha256},
  baseline: {file: sources.baseline.file, sha256: sources.baseline.sha256},
  profiles, stability, productionWrittenByHarness: false,
  exactBoundaryDefects: firstResult.cases.filter(row => !row.passed).map(row => ({name: row.name,
    failedChecks: row.checks.filter(check => !check.pass).map(check => check.name)})),
  finalChangedPaths: ['tools/openxinput_assign_guard_r117_selftest.mjs',
    'tools/fixtures/openxinput_assign_guard_r117/fixture.cpp',
    'tools/fixtures/openxinput_assign_guard_r117/source-binding.mjs',
    'tools/fixtures/openxinput_assign_guard_r117/README.md'],
  scope: 'Source-exact inert assignment wrapper only. Observed return/accept reflect fake proxy replies; no physical success inferred.',
};
save('RESULT.json', result);
fs.writeFileSync(path.join(options.out, 'LATEST.json'), JSON.stringify(result, null, 2) + '\n');
console.log('R117 result: ' + path.join(run, 'RESULT.json'));
process.exitCode = result.passed ? 0 : 1;
