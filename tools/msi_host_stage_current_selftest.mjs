import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// Current-source contract. The historical R8 before/after comparator is kept
// separately in msi_host_stage_integration_selftest.mjs and is not rebaselined.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const root = path.resolve(process.argv[2] ?? path.join(repo,
  '../../../Build/Validation/VirtualInput-StageCurrent', new Date().toISOString().replace(/[:.]/g, '-')));
assert.ok(!fs.existsSync(root), 'Use a fresh output directory to retain all evidence: ' + root);
fs.mkdirSync(path.join(root, 'Source'), { recursive: true });
const hash = value => createHash('sha256').update(value).digest('hex');
const inputs = ['InputHost/Program.cs', 'InputHost/MsiHostStageEvidence.cs',
  'tools/msi_host_stage_current_selftest.cs', 'tools/msi_host_stage_current_selftest.mjs',
  'tools/msi_host_stage_integration_selftest.mjs', 'tools/msi_host_stage_contract_v1.json'];
const captured = inputs.map(relative => {
  const bytes = fs.readFileSync(path.join(repo, relative));
  fs.writeFileSync(path.join(root, 'Source', path.basename(relative)), bytes);
  return { relative, sha256: hash(bytes) };
});
const text = fs.readFileSync(path.join(root, 'Source/Program.cs'), 'utf8').replaceAll('\r\n', '\n');
function extract(name) {
  const re = new RegExp('^\\s*(?:private|public|internal) (?:static )?(?:async )?[^\\n;]+\\b' + name + '\\([^;]*?\\)\\s*\\{', 'gm');
  const matches = [...text.matchAll(re)];
  assert.equal(matches.length, 1, 'unique method: ' + name);
  const match = matches[0], begin = match.index + match[0].lastIndexOf('{');
  let depth = 1, state = 'code';
  for (let i = begin + 1; i < text.length; ++i) {
    const c = text[i], n = text[i + 1];
    if (state === 'line') { if (c === '\n') state = 'code'; continue; }
    if (state === 'block') { if (c === '*' && n === '/') { state = 'code'; ++i; } continue; }
    if (state === 'str' || state === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (state === 'str' ? '"' : "'")) state = 'code';
      continue;
    }
    if (c === '/' && n === '/') { state = 'line'; ++i; continue; }
    if (c === '/' && n === '*') { state = 'block'; ++i; continue; }
    if (c === '"') { state = 'str'; continue; }
    if (c === "'") { state = 'char'; continue; }
    if (c === '{') ++depth;
    if (c === '}' && --depth === 0) return text.slice(match.index, i + 1).trim();
  }
  throw Error('Unbalanced method: ' + name);
}
function once(value, original, replacement, why) {
  assert.equal(value.split(original).length, 2, 'exact seam: ' + why);
  return value.replace(original, replacement);
}
const names = ['TryGetVirtualOutputProfile', 'TrySubmitFrame', 'RecordCostFrame',
  'MaybeEmitCostWindow', 'QueueMsiCostWindow', 'ResetCostWindow'];
const methods = Object.fromEntries(names.map(name => [name, extract(name)]));
// Process/thread enumeration, GC observation, serialization and file append are
// one opaque boundary in this test. Both variants use precisely the same seam.
// The real MSI bounded producer, reset and stage publisher remain compiled.
const payloadBegin = methods.MaybeEmitCostWindow.indexOf('var count = _costSubmitUsCount;');
const payloadEndText = 'line + Environment.NewLine, System.Text.Encoding.UTF8);';
const payloadEnd = methods.MaybeEmitCostWindow.indexOf(payloadEndText, payloadBegin) + payloadEndText.length;
assert.ok(payloadBegin > 0 && payloadEnd > payloadBegin, 'legacy cost payload seam');
const payload = methods.MaybeEmitCostWindow.slice(payloadBegin, payloadEnd);
assert.equal(methods.MaybeEmitCostWindow.split(payloadEndText).length, 2, 'unique cost append');
methods.MaybeEmitCostWindow = methods.MaybeEmitCostWindow.slice(0, payloadBegin) +
  'CostDiagnostics();' + methods.MaybeEmitCostWindow.slice(payloadEnd);
const hooked = Object.values(methods).join('\n\n');
const hookNames = ['BeginSubmit', 'EndSubmit', 'BeginBackend', 'EndBackend', 'BeginCost', 'EndCost'];
let plain = hooked;
for (const name of hookNames) plain = once(plain, '_msiHostStageEvidence?.' + name + '();', '', name);
const parser = extract('TryParseArguments');
const template = fs.readFileSync(path.join(root, 'Source/msi_host_stage_current_selftest.cs'), 'utf8');
for (const marker of ['// CURRENT_HOOKED_METHODS', '// CURRENT_PLAIN_METHODS', '// CURRENT_ARGUMENT_PARSER'])
  assert.equal(template.split(marker).length, 2, marker);
function fixture(hooks, unhooked = plain) {
  return template.replace('// CURRENT_HOOKED_METHODS', hooks)
    .replace('// CURRENT_PLAIN_METHODS', unhooked).replace('// CURRENT_ARGUMENT_PARSER', parser);
}
const project = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><TreatWarningsAsErrors>true</TreatWarningsAsErrors><UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>\n';
const childOptions = { cwd: root, encoding: 'utf8', windowsHide: true, timeout: 60000,
  maxBuffer: 4 * 1024 * 1024, env: { ...process.env, DOTNET_CLI_HOME: path.join(root, 'cli-home'),
    DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1', DOTNET_GENERATE_ASPNET_CERTIFICATE: 'false' } };
const processes = [];
function child(args, log) {
  const result = spawnSync('dotnet', args, childOptions);
  fs.writeFileSync(log, (result.stdout ?? '') + (result.stderr ?? ''));
  processes.push({ args, status: result.status, signal: result.signal, error: result.error?.message ?? null });
  assert.ok(!result.error && result.signal === null, 'child exit: ' + log);
  return result;
}
function buildAndRun(label, body, configuration, shouldPass, unhooked = plain, expectedError = null) {
  const folder = path.join(root, label);
  fs.mkdirSync(folder);
  fs.writeFileSync(path.join(folder, 'Fixture.cs'), fixture(body, unhooked));
  fs.copyFileSync(path.join(root, 'Source/MsiHostStageEvidence.cs'), path.join(folder, 'MsiHostStageEvidence.cs'));
  fs.writeFileSync(path.join(folder, 'Fixture.csproj'), project);
  fs.writeFileSync(path.join(folder, 'NuGet.Config'), '<configuration><packageSources><clear /></packageSources></configuration>\n');
  const build = child(['build', path.join(folder, 'Fixture.csproj'), '-c', configuration, '--nologo'], path.join(folder, 'build.log'));
  assert.equal(build.status, 0, 'fixture compilation: ' + label);
  const run = child([path.join(folder, 'bin', configuration, 'net10.0/Fixture.dll')], path.join(folder, 'run.log'));
  const result = JSON.parse(run.stdout.trim());
  fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify(result, null, 2) + '\n');
  assert.equal(result.pass, shouldPass, label + ': expected contract ' + (shouldPass ? 'pass' : 'rejection'));
  assert.equal(run.status, shouldPass ? 0 : 1, label + ': process result');
  if (expectedError !== null) assert.ok(result.error?.includes(expectedError), label + ': intended contract failure');
  return { label, configuration, ...result };
}
const profiles = ['Release', 'Debug'].map(mode => buildAndRun(mode, hooked, mode, true));
const mutations = [
  ['missing-backend-finally', once(hooked, '_msiHostStageEvidence?.EndBackend();', '', 'mutation backend end'), plain, 'stage order/finally contract'],
  ['missing-cost-finally', once(hooked, '_msiHostStageEvidence?.EndCost();', '', 'mutation cost end'), plain, 'stage order/finally contract'],
  // Apply the admission defect to both comparison variants. Rejection must come
  // from the independent expected contract, rather than only their equality.
  ['bypassed-profile-identity', once(hooked, 'return profileName.Length != 0 && profileIdentity == profileName;',
    'return profileName.Length != 0;', 'mutation hooked identity admission'),
    once(plain, 'return profileName.Length != 0 && profileIdentity == profileName;',
    'return profileName.Length != 0;', 'mutation plain identity admission'), 'admission/submit contract'],
].map(([label, body, unhooked, expectedError]) => buildAndRun(label, body, 'Release', false, unhooked, expectedError));
const live = captured.map(row => ({ ...row,
  currentSha256: hash(fs.readFileSync(path.join(repo, row.relative))) }));
assert.ok(live.every(row => row.sha256 === row.currentSha256), 'inputs stable during isolated run');
const result = { pass: true, contract: 'current-source-three-output-stage-v1', profiles, mutations,
  inputs: captured, actualMethods: names.map(name => ({ name, sha256: hash(extract(name)) })),
  opaqueLegacyCostPayloadSha256: hash(payload), parserSha256: hash(parser),
  comparison: 'Current extracted methods versus the same methods with only six exact diagnostic calls removed; independent admission, dispatch, counter and stage-order expectations also checked.',
  historicalR8ComparatorUnmodified: true, sourceStableDuringRun: true, processes,
  limit: 'Extracted methods with synthetic profiles, reports, backend, diagnostic worker and in-memory stage sink. Legacy synchronous diagnostic payload is opaque. No real pipe/SDK/target, mapped channel, Steam, device, release or hardware validation.' };
fs.writeFileSync(path.join(root, 'result.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify({ pass: true, root, profiles: profiles.map(x => ({ mode: x.configuration, cases: x.cases, assertions: x.assertions })), mutationsRejected: mutations.length }));
