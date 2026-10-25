// Targeted offline executable consumer test. No simulator/global registry mutation.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const options = {};
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i], value = process.argv[i + 1];
  if (!['--evidence-dir', '--before-main'].includes(key) || !value) throw new Error(`Unknown/incomplete argument ${key}`);
  options[key] = path.resolve(value);
}
if (!options['--evidence-dir']) throw new Error('--evidence-dir is required; no implicit output directory.');
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
const output = path.join(options['--evidence-dir'], `run-${stamp}-${process.pid}`);
fs.mkdirSync(output, { recursive: true });
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const save = (file, bytes) => { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, bytes); };
function extract(text, signature) {
  const start = text.indexOf(signature);
  if (start < 0 || text.indexOf(signature, start + 1) >= 0) throw new Error(`Nonunique/missing source signature: ${signature}`);
  // Mask literals/comments, preserving offsets and only structural braces.
  const masked = text.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, value => ' '.repeat(value.length));
  const opening = masked.indexOf('{', start); let depth = 0;
  for (let i = opening; i < masked.length; ++i) {
    if (masked[i] === '{') ++depth;
    if (masked[i] === '}' && --depth === 0) return text.slice(start, i + 1);
  }
  throw new Error(`Unclosed source function: ${signature}`);
}
const mainFile = path.join(repo, 'native/main.cpp');
const runtimeFile = path.join(repo, 'native/steam_game_link_runtime.h');
const cppFile = path.join(repo, 'tools/ymcc_sim/global/consumer/steam-game-link-log-gate-selftest.cpp');
const runtime = fs.readFileSync(runtimeFile, 'utf8');
const sourceBytes = fs.readFileSync(mainFile), current = sourceBytes.toString('utf8');
const vcvars = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat';
if (!fs.existsSync(vcvars)) throw new Error('Existing MSVC toolchain unavailable; no toolchain installation attempted.');
const inputFiles = [mainFile, runtimeFile, cppFile, fileURLToPath(import.meta.url), ...['log_frequency_policy.h', 'log_snapshot_state.h', 'log_jsonl_boundary.h', 'sleep_steam_tracking.h', 'steam_overlay_provider_cache_diagnostic.h'].map(name => path.join(repo, 'native', name)), path.join(repo, 'deps/json/json.hpp')];
const inputs = inputFiles.map(file => ({ file, sha256: sha(fs.readFileSync(file)), bytes: fs.statSync(file).size }));
save(path.join(output, 'bound-main.cpp'), sourceBytes);
save(path.join(output, 'bound-runtime.h'), fs.readFileSync(runtimeFile));
const whitelistEntry = '        "steam-game-link-state",';
if (current.split(whitelistEntry).length !== 2) throw new Error('Expected exactly one production whitelist entry for mutation.');
const variants = [{ name: 'current', text: current, ordinaryOnly: false, expectPass: true },
  { name: 'without-whitelist', text: current.replace(whitelistEntry, '        // Negative control: Steam whitelist entry removed.'), ordinaryOnly: true, expectPass: false }];
if (options['--before-main']) {
  const beforeBytes = fs.readFileSync(options['--before-main']);
  save(path.join(output, 'bound-before-main.cpp'), beforeBytes);
  variants.push({ name: 'original-before-fix', text: beforeBytes.toString('utf8'), ordinaryOnly: true, expectPass: false, originalSha256: sha(beforeBytes) });
}
const signatures = ['static std::string nativeLifecycleTimestamp() {',
  'static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {',
  'static bool nativeLifecycleEventIsBase(const char* event) {',
  'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',
  'static void appendNativeLifecycleLog(const char* event, const json& observed) {'];
const results = [];
for (const variant of variants) {
  const dir = path.join(output, variant.name); fs.mkdirSync(dir, { recursive: true });
  const extra = 'static json nativeSteamGameLinkLogDetail(const json& detail) {';
  const selected = [...signatures.slice(0, 3), ...(variant.text.includes(extra) ? [extra] : []), ...signatures.slice(3)];
  const mainFunctions = selected.map(signature => extract(variant.text, signature));
  const publishFunctions = ['struct Control {', 'static json base() {', 'static void publish(const std::shared_ptr<Control>& c,json value,bool logChange) {'].map(signature => extract(runtime, signature));
  // The production struct closing semicolon is outside the brace extraction.
  publishFunctions[0] += ';';
  const bridge = 'using Json = json;\n' + extract(fs.readFileSync(path.join(repo, 'native/sleep_steam_tracking.h'), 'utf8'), 'inline bool flag(const Json& j,const char* key) {');
  save(path.join(dir, 'extracted-main.h'), (variant.text.includes(extra) ? '#define YMCC_STEAM_LOG_PROJECTION 1\n' : '') + mainFunctions.join('\n\n'));
  save(path.join(dir, 'extracted-publish.h'), `${bridge}\n${publishFunctions.join('\n\n')}\n`);
  save(path.join(dir, 'source-functions.json'), JSON.stringify(selected.map((signature, index) => ({ signature, sha256: sha(mainFunctions[index]) })), null, 2));
  const exe = path.join(dir, 'steam-log-selftest.exe');
  const batch = path.join(dir, 'build.cmd');
  save(batch, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 "${cppFile}" /I"${dir}" /I"${path.join(repo, 'deps/json')}" /I"${path.join(repo, 'native')}" /I"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um" /I"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared" /I"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt" /Fo"${path.join(dir, 'selftest.obj')}" /Fe"${exe}" /link /LIBPATH:"C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64" /LIBPATH:"C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"\r\n`);
  const build = spawnSync('cmd.exe', ['/d', '/c', batch], { cwd: dir, timeout: 60000, maxBuffer: 8 * 1024 * 1024 });
  save(path.join(dir, 'build.stdout.log'), build.stdout ?? Buffer.alloc(0)); save(path.join(dir, 'build.stderr.log'), build.stderr ?? Buffer.alloc(0));
  if (build.error || build.status !== 0) throw new Error(`Compilation failed for ${variant.name}; see ${dir}; ${build.error ?? build.status}`);
  const execution = spawnSync(exe, [path.join(dir, 'logs'), ...(variant.ordinaryOnly ? ['--ordinary-only'] : [])], { cwd: dir, timeout: 60000, maxBuffer: 8 * 1024 * 1024 });
  save(path.join(dir, 'stdout.json'), execution.stdout ?? Buffer.alloc(0)); save(path.join(dir, 'stderr.log'), execution.stderr ?? Buffer.alloc(0));
  if (execution.error) throw execution.error;
  const observed = JSON.parse(execution.stdout.toString('utf8'));
  const failedChecks = observed.checks.filter(check => !check.pass).map(check => check.name);
  const negativeWitness = failedChecks.includes('ordinary-gate-off-selects-event-lane') && failedChecks.includes('ordinary-gate-off-publish-reaches-native-file');
  const pass = variant.expectPass ? execution.status === 0 && observed.ok && !failedChecks.length : execution.status === 1 && !observed.ok && negativeWitness;
  results.push({ variant: variant.name, pass, expectedPass: variant.expectPass, exitCode: execution.status, checks: observed.checks.length, failedChecks,
    originalSha256: variant.originalSha256, executedFunctions: selected, exeSha256: sha(fs.readFileSync(exe)) });
}
const stable = inputs.every(item => sha(fs.readFileSync(item.file)) === item.sha256);
const report = { schema: 'steam-ordinary-log-production-consumer.v1', at: new Date().toISOString(), ok: stable && results.every(result => result.pass),
  output, inputs, sourcesStableDuringRun: stable, results,
  boundary: 'Only extracted production logging/publish functions executed. Compiler and isolated offline fixture executable only; no YMCC/Steam launch, settings, devices, sleep, debug/private API or deployment. Contact-only publish(false) remains snapshot/history-only.' };
save(path.join(output, 'result.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
process.exitCode = report.ok ? 0 : 1;
