// Offline native logging audit. Only this tool, its C++ companion and evidence are written.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const defaultEvidence = path.resolve(root, '../../../Docs/Tasks/CPU/CoreLogging-20261017');
const args = process.argv.slice(2);
let output = defaultEvidence;
for (let i = 0; i < args.length; ++i) {
  if (args[i] === '--output-dir' && args[i + 1]) output = path.resolve(args[++i]);
  else throw new Error(`Unknown/incomplete argument: ${args[i]}`);
}
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
const run = path.join(output, `run-${stamp}-${process.pid}`);
fs.mkdirSync(run, { recursive: true });
function save(relative, data) {
  const dest = path.join(run, relative);
  fs.mkdirSync(path.dirname(dest), { recursive: true }); fs.writeFileSync(dest, data); return dest;
}
function extract(text, signature) {
  const start = text.indexOf(signature);
  if (start < 0 || text.indexOf(signature, start + 1) >= 0) throw new Error(`Nonunique signature ${signature}`);
  // Mask comments and literals without changing offsets; preserve code braces.
  const masked = text.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, m => ' '.repeat(m.length));
  const opening = masked.indexOf('{', start); let depth = 0;
  for (let i = opening; i < masked.length; ++i) {
    if (masked[i] === '{') ++depth;
    if (masked[i] === '}' && --depth === 0) return text.slice(start, i + 1);
  }
  throw new Error(`Unclosed function ${signature}`);
}
const mainBytes = fs.readFileSync(path.join(root, 'native/main.cpp'));
const main = mainBytes.toString('utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
save('bound-main.cpp', mainBytes);
const signatures = [
  'static bool domainLogEnabled(const char* domain) {',
  'static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {',
  'static std::wstring domainLogPath(const char* domain) {',
  'static int domainLogStreamIndex(const char* domain) {',
  'static void flushDomainLogs(bool closeStreams) {',
  'static void appendDomainLog(const char* domain, const char* event, const json& detail) {'
];
const functions = signatures.map(s => extract(main, s));
const policyBytes = fs.readFileSync(path.join(root, 'native/log_frequency_policy.h'));
const policy = policyBytes.toString('utf8').replace(/\r\n/g, '\n');
const oldClassifier = extract(policy, 'constexpr bool detailNeedsImmediateFlush(std::string_view event, bool explicitFailure) noexcept {');
// One traversal candidate: same case-sensitive, substring and explicit-failure semantics.
const newClassifier = `constexpr bool detailNeedsImmediateFlush(std::string_view event, bool explicitFailure) noexcept {
    if (explicitFailure) return true;
    for (size_t i = 0; i < event.size(); ++i) {
        const auto tail = event.substr(i);
        switch (event[i]) {
        case 'f': if (tail.starts_with("fault") || tail.starts_with("fail")) return true; break;
        case 'e': if (tail.starts_with("error")) return true; break;
        case 'r': if (tail.starts_with("reject")) return true; break;
        case 't': if (tail.starts_with("timeout")) return true; break;
        }
    }
    return false;
}`;
const bufferBytes = fs.readFileSync(path.join(root, 'native/buffered_domain_log.h'));
const boundaryBytes = fs.readFileSync(path.join(root, 'native/log_jsonl_boundary.h'));
const entry = path.join(output, 'entry/native');
const entryPresent = ['buffered_domain_log.h', 'log_frequency_policy.h', 'main.cpp'].every(f => fs.existsSync(path.join(entry, f)));
if (!entryPresent) throw new Error('Entry snapshots are required; refusing to invent a baseline.');
const namespaceSets = {
  baseline: { policy: fs.readFileSync(path.join(entry, 'log_frequency_policy.h')), buffer: fs.readFileSync(path.join(entry, 'buffered_domain_log.h')) },
  candidate: { policy: Buffer.from(policy.replace(oldClassifier, newClassifier)), buffer: bufferBytes },
  current: { policy: policyBytes, buffer: bufferBytes }
};
for (const [variant, files] of Object.entries(namespaceSets)) {
  for (const [name, content] of [['log_frequency_policy.h', files.policy], ['buffered_domain_log.h', files.buffer], ['log_jsonl_boundary.h', boundaryBytes]]) {
    save(`${variant}/${name}`, content.toString('utf8').replaceAll('namespace ymcc::logging', `namespace ${variant}::logging`));
  }
}
const candidateSource = save('candidate-native-log_frequency_policy.h', namespaceSets.candidate.policy);
const candidateDiff = spawnSync('git', ['diff', '--no-index', '--ignore-space-at-eol', '--', path.join(entry, 'log_frequency_policy.h'), candidateSource], { encoding: 'utf8' });
if (candidateDiff.status !== 1) throw new Error('Expected exactly one evidence-only candidate diff');
save('candidate-policy.patch', candidateDiff.stdout);
let bound = '';
for (const variant of Object.keys(namespaceSets)) {
  bound += `namespace bound_${variant} {\nstatic std::mutex g_domainLogMx;\nstatic ${variant}::logging::BufferedDomainLog g_domainLogStreams[3];\nstatic std::atomic<bool> g_domainLogEnabled[2] = {false, false};\nstatic std::wstring dataRoot;\nstatic unsigned pathCalls=0, timestampCalls=0;\nstatic std::wstring yemancc_data_dir(){++pathCalls;return dataRoot;}\nstatic std::string nativeLifecycleTimestamp(){++timestampCalls;return "2026-10-17T00:00:00.000";}\n`;
  bound += functions.map(f => f.replaceAll('ymcc::logging::', `${variant}::logging::`)).join('\n') + '\n}\n';
}
save('bound_domain_calls.h', bound);
const events = [...new Set([...main.matchAll(/appendDomainLog\(\s*"(?:gyro|virtual|focus)"\s*,\s*"([^"\\]+)"/g)].map(m => m[1]))].sort();
if (events.length < 20) throw new Error(`Unexpectedly small direct event corpus: ${events.length}`);
save('bound_events.h', `static const std::vector<std::string> directEvents = ${JSON.stringify(events).replace(/^\[/, '{').replace(/\]$/, '}')};\n`);
save('bound-events.json', JSON.stringify(events, null, 2) + '\n');
const checks = {};
const check = (name, pass) => { checks[name] = !!pass; };
const append = functions.at(-1);
const entryMainBytes = fs.readFileSync(path.join(entry, 'main.cpp'));
const entryMain = entryMainBytes.toString('utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
const reviewedSignatures = [...signatures,
  'static void mirrorLifecycleToDomains(const char* event, const json& detail) {',
  'static void domainLogGateRecord(const char* source, bool enabled) {',
  'static void flushNativeLifecycleLogs(bool closeDetail) {',
  'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',
  'static bool nativeLifecycleEventIsBase(const char* event) {',
  'static void appendNativeLifecycleLog(const char* event, const json& observed) {',
  'static void flushTdpObservationLogs(bool closeDetail) {',
  'static void appendTdpObservation(const char* event, json detail, bool raw = false) noexcept {',
  'ipc_on("logs.domainSetEnabled", [](const json& a) -> json {',
  'ipc_on("logs.domainClear", [](const json& a) -> json {',
  'ipc_on("logs.domainExport", [](const json& a) -> json {',
  'ipc_on("logs.exportAll", [](const json& exportArgs) -> json {'
];
const reviewedFunctions = reviewedSignatures.map(signature => {
  // Keep older baseline snapshots readable across this diagnostic-only signature change.
  const entrySignature = entryMain.includes(signature) ? signature : signature.replace('const json& observed', 'json detail');
  const before = extract(entryMain, entrySignature), now = extract(main, signature);
  return { signature, entrySha256: sha(before), currentSha256: sha(now), unchanged: before === now,
    currentLine: main.slice(0, main.indexOf(signature)).split('\n').length };
});
save('reviewed-function-manifest.json', JSON.stringify(reviewedFunctions, null, 2) + '\n');
// The whole shared export handler also contains unrelated manifest metadata.
// Bind its flush/collection prefix; record whole-handler drift separately.
const exportSignature = 'ipc_on("logs.exportAll", [](const json& exportArgs) -> json {';
check('reviewed-core-logging-functions-match-entry', reviewedFunctions.filter(f => f.signature !== exportSignature).every(f => f.unchanged));
const exportPrefix = text => {
  const handler = extract(text, exportSignature);
  const boundary = handler.indexOf('std::vector<std::wstring> files;');
  if (boundary < 0) throw new Error('Missing export collection boundary');
  return handler.slice(0, boundary);
};
check('export-flush-collection-prefix-matches-entry', exportPrefix(entryMain) === exportPrefix(main));
check('entry-buffer-unchanged', bufferBytes.equals(namespaceSets.baseline.buffer));
check('entry-policy-unchanged', policyBytes.equals(namespaceSets.baseline.policy));
check('off-before-path-and-json', append.indexOf('!domainLogEnabled(domain)') < append.indexOf('domainLogPath(') && append.indexOf('!domainLogEnabled(domain)') < append.indexOf('json line'));
check('locked-gate-recheck', append.indexOf('std::lock_guard') < append.indexOf('if (!domainLogEnabled(domain)) return;'));
check('append-uses-policy-and-retains-failure-flags', append.includes('detailNeedsImmediateFlush') && append.includes('line["ok"].is_boolean()') && append.includes('line["success"].is_boolean()'));
const nativeFlush = extract(main, 'static void flushNativeLifecycleLogs(bool closeDetail) {');
check('native-flush-includes-domain-and-tdp', nativeFlush.includes('flushDomainLogs(closeDetail);') && nativeFlush.includes('flushTdpObservationLogs(closeDetail);'));
const clear = extract(main, 'ipc_on("logs.domainClear", [](const json& a) -> json {');
check('clear-closes-before-remove', clear.indexOf('.close();') >= 0 && clear.indexOf('.close();') < clear.indexOf('fspath::remove(path,'));
const singleExport = extract(main, 'ipc_on("logs.domainExport", [](const json& a) -> json {');
check('single-export-flushes-before-copy', singleExport.indexOf('.flush();') >= 0 && singleExport.indexOf('.flush();') < singleExport.indexOf('CopyFileW('));
const exportAll = extract(main, 'ipc_on("logs.exportAll", [](const json& exportArgs) -> json {');
check('all-export-flushes-before-collection', exportAll.indexOf('flushNativeLifecycleLogs();') >= 0 && exportAll.indexOf('flushNativeLifecycleLogs();') < exportAll.indexOf('std::vector<std::wstring> files;'));
const gate = extract(main, 'static void domainLogGateRecord(const char* source, bool enabled) {');
check('gate-off-flushes-and-closes', gate.includes('flushNativeLifecycleLogs(!enabled);'));
const archiveBudget = main.match(/static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS\s*=\s*(\d+)/);
check('archive-retry-budget-two', archiveBudget?.[1] === '2');
const vcvars = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
if (!fs.existsSync(vcvars)) throw new Error('Existing MSVC toolchain unavailable; nothing installed.');
const cpp = save('core_logging_cpu_selftest.cpp', fs.readFileSync(path.join(root, 'tools/core_logging_cpu_selftest.cpp')));
const exe = path.join(run, 'core_logging_cpu_selftest.exe');
const batch = save('build.cmd', `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 "${cpp}" /I"${run}" /I"${path.join(root, 'deps/json')}" /Fo"${path.join(run, 'core_logging_cpu_selftest.obj')}" /Fe"${exe}"\r\n`);
function execute(file, argv, log, options = {}) {
  const result = spawnSync(file, argv, { cwd: run, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, ...options });
  save(log, (result.stdout || '') + (result.stderr || '') + (result.error ? String(result.error) : ''));
  if (result.status !== 0) throw new Error(`${file} exit ${result.status}; see ${path.join(run, log)}`);
  return result.stdout;
}
execute('cmd.exe', ['/d', '/c', batch], 'compile.log');
const result = JSON.parse(execute(exe, [path.join(run, 'fixtures')], 'native-result.json', { timeout: 120000 }));
function fixtureManifest(dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).sort((a,b) => a.name.localeCompare(b.name)).flatMap(item => {
    const file = path.join(dir, item.name);
    return item.isDirectory() ? fixtureManifest(file).map(f => ({ ...f, path: item.name + '/' + f.path })) : [{ path: item.name, sha256: sha(fs.readFileSync(file)) }];
  });
}
for (const kind of ['buffer', 'domain']) {
  const baseline = fixtureManifest(path.join(run, 'fixtures', kind + '-baseline'));
  for (const variant of ['candidate', 'current']) {
    check(kind + '-' + variant + '-exact-file-set-and-bytes', JSON.stringify(baseline) === JSON.stringify(fixtureManifest(path.join(run, 'fixtures', kind + '-' + variant))));
  }
}
const afterMain = fs.readFileSync(path.join(root, 'native/main.cpp'));
const afterText = afterMain.toString('utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
check('bound-functions-stable-during-run', signatures.every((s, i) => extract(afterText, s) === functions[i]));
check('headers-stable-during-run', fs.readFileSync(path.join(root, 'native/buffered_domain_log.h')).equals(bufferBytes) && fs.readFileSync(path.join(root, 'native/log_frequency_policy.h')).equals(policyBytes));
const report = { timestamp: new Date().toISOString(), runDirectory: run, sourceChecks: checks,
  failures: result.failures + Object.values(checks).filter(v => !v).length, native: result,
  entrySourceSha256: sha(entryMainBytes), entryMainUnchanged: entryMainBytes.equals(mainBytes), reviewedLoggingFunctionCount: reviewedFunctions.length, sourceSha256: sha(mainBytes), currentSourceSha256: sha(afterMain), wholeMainStableDuringRun: mainBytes.equals(afterMain),
  headerSha256: { buffer: sha(bufferBytes), policy: sha(policyBytes), boundary: sha(boundaryBytes) },
  directEventCount: events.length, namedErrorBenchmarkCorpus: events.some(e => /fault|fail|error|reject|timeout/.test(e)) ? 'direct main.cpp event literals' : 'five synthetic error tokens; direct literal corpus has no matching errors', candidateScope: 'evidence-only single-pass urgent-event classifier; production unchanged',
  realProductExecuted: false, steamExecuted: false, hardwareExecuted: false, deployed: false,
  timingScope: 'MSVC /O2 offline classifier and extracted domain writer; fixed timestamp/PID/tick fixtures; thread CPU cycles and wall time, not product CPU percentage' };
fs.writeFileSync(path.join(output, 'selftest-report.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ failures: report.failures, checks: result.checks.length, sourceChecks: Object.keys(checks).length, runDirectory: run,
  benchmarkMedians: Object.fromEntries(Object.entries(result.benchmarks).map(([k,v]) => [k, Object.fromEntries(['baseline','candidate','current'].map(name => [name, { cycles: v[name].medianCyclesPerCall, ns: v[name].medianNsPerCall }]))])) }, null, 2));
process.exitCode = report.failures ? 3 : 0;
