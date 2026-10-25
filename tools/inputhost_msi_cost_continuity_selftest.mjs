import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const requested = process.argv[2];
const out = requested ? path.resolve(requested) : path.resolve(repo,
  '../../../Build/Validation/MSI-CpuContinuity-Selftest', new Date().toISOString().replace(/[:.]/g, '-'));
if (fs.existsSync(out)) throw Error('Choose a new output directory to retain prior evidence: ' + out);
fs.mkdirSync(out, { recursive: true });
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
const sources = [
  ['InputHost/MsiCostWindowDiagnostics.cs', 'MsiCostWindowDiagnostics.cs'],
  ['InputHost/MsiCostWindowDispatcher.cs', 'MsiCostWindowDispatcher.cs'],
  ['InputHost/HostCpuCostWindow.cs', 'HostCpuCostWindow.cs'],
  ['tools/inputhost_msi_cost_continuity_selftest.cs', 'Fixture.cs'],
];
const manifest = sources.map(([relative, name]) => {
  const source = path.join(repo, relative), bytes = fs.readFileSync(source);
  fs.writeFileSync(path.join(out, name), bytes);
  return { source, name, sha256: sha(bytes) };
});
fs.writeFileSync(path.join(out, 'NuGet.Config'), '<configuration><packageSources><clear /></packageSources></configuration>\n');
fs.writeFileSync(path.join(out, 'Fixture.csproj'), `<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><NuGetAudit>false</NuGetAudit><UseSharedCompilation>false</UseSharedCompilation></PropertyGroup>
</Project>\n`);
const environment = { ...process.env, DOTNET_GENERATE_ASPNET_CERTIFICATE: 'false',
  DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1' };
function run(args, log) {
  const r = spawnSync('dotnet', args, { cwd: out, env: environment, encoding: 'utf8',
    timeout: 120000, maxBuffer: 4 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, log), (r.stdout ?? '') + (r.stderr ?? ''));
  if (r.error || r.status !== 0) throw Error(log + ': ' + (r.error ?? r.status));
}
const results = [];
for (const profile of ['Release', 'Debug']) {
  run(['build', 'Fixture.csproj', '-c', profile, '--nologo'], profile + '-build.log');
  const resultPath = path.join(out, profile + '-result.json');
  run([path.join(out, 'bin', profile, 'net10.0-windows/Fixture.dll'), resultPath], profile + '-run.log');
  const result = JSON.parse(fs.readFileSync(resultPath, 'utf8'));
  if (!result.pass || result.failures) throw Error('Failed profile: ' + profile);
  results.push({ profile, ...result });
  console.log(`${profile}: ${result.groups} groups, ${result.checks} checks, ${result.failures} failures`);
}
for (const item of manifest) if (sha(fs.readFileSync(item.source)) !== item.sha256) throw Error('Source changed: ' + item.source);
fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify({ pass: true, results,
  sources: manifest, sourceStableDuringRun: true, runtimeSamplerCalls: 0, deviceCalls: 0,
  scope: 'Actual diagnostic and dispatcher with injected clock/counters, flag, SDK observation and log sink; no product Host or device execution.' }, null, 2) + '\n');
console.log(out);
