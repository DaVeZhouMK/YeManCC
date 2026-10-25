#!/usr/bin/env node
// Offline: compile the verbatim production helpers, never the Host/driver SDK.
// Usage: node tools/inputhost_sha256_perf_selftest.mjs --before <Program.cs> --out <new-directory>
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const options = {};
for (let i = 2; i < process.argv.length; i += 2) {
  const name = process.argv[i];
  if (!['--before', '--current', '--out', '--iterations', '--rounds', '--note', '--jit'].includes(name) || !process.argv[i + 1])
    throw new Error(`Unknown/missing argument: ${name}`);
  options[name.slice(2)] = process.argv[i + 1];
}
if (!options.before || !options.out) throw new Error('--before and --out are required');
const jitMode = options.jit ?? 'tiered';
if (!['tiered', 'fixed'].includes(jitMode)) throw new Error('--jit must be tiered or fixed');
const beforePath = path.resolve(options.before);
const currentPath = path.resolve(options.current ?? path.join(repo, 'InputHost', 'Program.cs'));
const out = path.resolve(options.out);
if (fs.existsSync(out) && fs.readdirSync(out).length) throw new Error(`Output must be empty: ${out}`);
fs.mkdirSync(out, { recursive: true });
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const startedUtc = new Date().toISOString();

function extract(text) {
  const declaration = 'private static bool IsSha256(string value)';
  const at = text.indexOf(declaration);
  if (at < 0 || text.indexOf(declaration, at + 1) >= 0) throw new Error('Expected exactly one IsSha256');
  let start = text.lastIndexOf('\n', at) + 1;
  let cursor = at + declaration.length;
  while (/\s/.test(text[cursor])) cursor++;
  let end;
  if (text.startsWith('=>', cursor)) {
    end = text.indexOf(';', cursor) + 1;
  } else if (text[cursor] === '{') {
    // Lex comments/literals so braces inside them cannot truncate the function.
    let depth = 0, state = 'code';
    for (let i = cursor; i < text.length; i++) {
      const c = text[i], pair = text.slice(i, i + 2);
      if (state === 'line') { if (c === '\n') state = 'code'; }
      else if (state === 'block') { if (pair === '*/') { state = 'code'; i++; } }
      else if (state === '"' || state === "'") {
        if (c === '\\') i++;
        else if (c === state) state = 'code';
      } else if (pair === '//') { state = 'line'; i++; }
      else if (pair === '/*') { state = 'block'; i++; }
      else if (c === '"' || c === "'") state = c;
      else if (c === '{') depth++;
      else if (c === '}' && --depth === 0) { end = i + 1; break; }
    }
  }
  if (!end) throw new Error('Unsupported/unclosed IsSha256 declaration');
  return { method: text.slice(start, end), start, end, outside: text.slice(0, start) + '<IsSha256>' + text.slice(end) };
}
function run(args, logName, env = process.env) {
  const result = spawnSync('dotnet', args, { encoding: 'utf8', env, maxBuffer: 16 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, logName), (result.stdout ?? '') + (result.stderr ?? ''));
  if (result.error || result.status !== 0) throw new Error(`dotnet failed: ${result.error ?? result.status}; see ${logName}`);
  return result.stdout.trim();
}
function versions(text, prefix) {
  return text.split(/\r?\n/).map(line => line.match(prefix)).filter(Boolean)
    .sort((a, b) => a[1].localeCompare(b[1], 'en', { numeric: true }));
}
const beforeBytes = fs.readFileSync(beforePath), currentBytes = fs.readFileSync(currentPath);
const before = extract(beforeBytes.toString('utf8')), current = extract(currentBytes.toString('utf8'));
// Preserve full byte snapshots as well as the exact extracted compiler inputs.
const beforeSnapshot = path.join(out, 'source-before', 'Program.cs');
const currentSnapshot = path.join(out, 'source-current', 'Program.cs');
for (const [snapshot, bytes] of [[beforeSnapshot, beforeBytes], [currentSnapshot, currentBytes]]) {
  fs.mkdirSync(path.dirname(snapshot));
  fs.writeFileSync(snapshot, bytes);
  if (!fs.readFileSync(snapshot).equals(bytes)) throw new Error('Snapshot identity mismatch');
}
function outsideBytes(bytes, extracted) {
  const text = bytes.toString('utf8');
  return Buffer.concat([bytes.subarray(0, Buffer.byteLength(text.slice(0, extracted.start))),
    bytes.subarray(Buffer.byteLength(text.slice(0, extracted.end)))]);
}
const beforeOutside = outsideBytes(beforeBytes, before), currentOutside = outsideBytes(currentBytes, current);

const hostProject = fs.readFileSync(path.join(repo, 'InputHost', 'YeManInputHost.csproj'), 'utf8');
const major = hostProject.match(/<TargetFramework>net(\d+)\.\d+/)?.[1];
if (!major) throw new Error('Cannot determine Host target runtime');
const sdks = versions(run(['--list-sdks'], 'sdks.log'), /^(\d+\.\d+\.\d+) \[(.+)\]$/);
const sdk = sdks.at(-1);
const runtimes = versions(run(['--list-runtimes'], 'runtimes.log'), /^Microsoft\.NETCore\.App (\d+\.\d+\.\d+) \[(.+)\]$/)
  .filter(match => match[1].startsWith(`${major}.`));
const runtime = runtimes.at(-1);
if (!sdk || !runtime) throw new Error('Local SDK/runtime matching the Host is required');
const dotnetRoot = path.resolve(sdk[2], '..');
const packRoot = path.join(dotnetRoot, 'packs', 'Microsoft.NETCore.App.Ref');
const packVersion = fs.readdirSync(packRoot).filter(name => name.startsWith(`${major}.`))
  .sort((a, b) => a.localeCompare(b, 'en', { numeric: true })).at(-1);
if (!packVersion) throw new Error('Offline reference pack is missing');
const refs = path.join(packRoot, packVersion, 'ref', `net${major}.0`);
const nativePath = path.join(repo, 'native', 'main.cpp');
const nativeBytes = fs.readFileSync(nativePath), native = nativeBytes.toString('utf8');
const configHash = native.match(/kInputHostProtocolConfigHash\s*=\s*"([0-9a-fA-F]{64})"/)?.[1];
if (!configHash) throw new Error('Cannot locate the real native config hash');
const callSites = text => text.split(/\r?\n/).flatMap((line, i, all) =>
  line.includes('!IsSha256(') ? [{ line: i + 1, text: all.slice(Math.max(0, i - 3), i + 5).join('\n') }] : []);
const currentCallSites = callSites(currentBytes.toString('utf8'));
if (currentCallSites.length !== 2 || !currentCallSites[0].text.includes('configHash') ||
    !currentCallSites[1].text.includes('normalizedInputHash')) throw new Error('Re-audit changed production call sites');
const fixturePath = path.join(repo, 'tools', 'inputhost_sha256_perf_selftest.cs');
const fixtureBytes = fs.readFileSync(fixturePath);
const manifest = {
  beforePath, currentPath, beforeSnapshot, currentSnapshot, startedUtc, jitMode, runNote: options.note ?? '',
  beforeFileSha256: sha(beforeBytes), currentFileSha256: sha(currentBytes),
  beforeMethodSha256: sha(before.method), currentMethodSha256: sha(current.method),
  beforeOutsideMethodSha256: sha(beforeOutside), currentOutsideMethodSha256: sha(currentOutside),
  outsideMethodByteIdentical: beforeOutside.equals(currentOutside),
  nativePath, nativeFileSha256: sha(nativeBytes), nativeConfigHash: configHash,
  beforeCallSites: callSites(beforeBytes.toString('utf8')), currentCallSites,
  fixtureSha256: sha(fixtureBytes), launcherSha256: sha(fs.readFileSync(fileURLToPath(import.meta.url))),
  compilerSdk: sdk[1], referencePack: packVersion, requestedRuntime: runtime[1],
  executionScope: 'Only verbatim IsSha256 functions and a BCL-only fixture; no Host, HID SDK, parser, pipe, devices or lifecycle execute.'
};
fs.writeFileSync(path.join(out, 'source-manifest.json'), JSON.stringify(manifest, null, 2));
for (const [name, extracted] of [['Before', before], ['Current', current]]) {
  fs.writeFileSync(path.join(out, `${name}Sha256.cs`),
    `using System;\nusing System.Linq;\ninternal static class ${name}Sha256\n{\n${extracted.method}\n    public static Func<string, bool> Bind() => IsSha256;\n}\n`);
}
fs.writeFileSync(path.join(out, 'Fixture.cs'), fixtureBytes);
fs.writeFileSync(path.join(out, 'NativeHash.cs'), `internal static class NativeHash { public const string Config = "${configHash}"; }\n`);
const assembly = path.join(out, 'InputHostSha256Selftest.dll');
const quote = value => `"${value}"`;
const response = ['/nologo', '/target:exe', '/optimize+', '/debug:portable', '/deterministic+', '/nullable:disable',
  '/langversion:latest', `/out:${quote(assembly)}`,
  ...fs.readdirSync(refs).filter(name => name.endsWith('.dll')).map(name => `/reference:${quote(path.join(refs, name))}`),
  ...['BeforeSha256.cs', 'CurrentSha256.cs', 'Fixture.cs', 'NativeHash.cs'].map(name => quote(path.join(out, name)))];
fs.writeFileSync(path.join(out, 'compile.rsp'), response.join('\n'));
run([path.join(sdk[2], sdk[1], 'Roslyn', 'bincore', 'csc.dll'), `@${path.join(out, 'compile.rsp')}`], 'compile.log');
fs.writeFileSync(path.join(out, 'InputHostSha256Selftest.runtimeconfig.json'), JSON.stringify({ runtimeOptions: {
  tfm: `net${major}.0`, framework: { name: 'Microsoft.NETCore.App', version: runtime[1] }, rollForward: 'Disable'
} }, null, 2));
// Default: production-like tiered JIT/PGO. --jit fixed is a separate diagnostic control;
// its LINQ allocations can disappear under PGO and must not be presented as deployed savings.
const flag = jitMode === 'tiered' ? '1' : '0';
const env = { ...process.env, DOTNET_TieredCompilation: flag, DOTNET_TieredPGO: flag, DOTNET_ReadyToRun: flag };
manifest.benchmarkEnvironment = { DOTNET_TieredCompilation: flag, DOTNET_TieredPGO: flag, DOTNET_ReadyToRun: flag };
const output = run([assembly, path.join(out, 'results.json'), options.iterations ?? '500000', options.rounds ?? '9'], 'run.log', env);
manifest.completedUtc = new Date().toISOString();
manifest.sourceStableDuringRun = sha(fs.readFileSync(beforePath)) === manifest.beforeFileSha256 &&
  sha(fs.readFileSync(currentPath)) === manifest.currentFileSha256;
fs.writeFileSync(path.join(out, 'source-manifest.json'), JSON.stringify(manifest, null, 2));
if (!manifest.sourceStableDuringRun) throw new Error('Source changed during this run; extracted snapshots remain in output');
console.log(output);
console.log(`Evidence: ${out}`);
