// Pure offline fixture: compile only the exact helper and self-test snapshots.
// Never build or execute Program.cs, InputHost, HIDMaestro, or device probes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const self = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(self), '..');
const workspace = path.resolve(repo, '../../..');
const validation = path.join(workspace, 'Build/Validation/VirtualInput-Win5-ROG-R101-20261016/helper-validation');
const out = path.resolve(process.argv[2] ?? validation);
const relative = path.relative(validation, out);
if (relative.startsWith('..') || path.isAbsolute(relative))
  throw new Error('Output must stay within the R101 helper-validation directory.');
fs.mkdirSync(out, { recursive: true });
const run = path.join(out, `run-${new Date().toISOString().replace(/[:.]/g, '')}-${process.pid}`);
fs.mkdirSync(run);
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const save = (name, data) => fs.writeFileSync(path.join(run, name), JSON.stringify(data, null, 2) + '\n');
const inputPaths = [path.join(repo, 'InputHost/HostReleaseTimingEvidence.cs'),
  path.join(repo, 'tools/host_release_timing_evidence_selftest.cs'), self];
const inputs = inputPaths.map(source => {
  const before = fs.statSync(source);
  const data = fs.readFileSync(source);
  const after = fs.statSync(source);
  if (before.size !== after.size || before.mtimeMs !== after.mtimeMs) throw new Error(`Input changed while reading: ${source}`);
  return { source, bytes: data.length, sha256: sha(data), data };
});
save('source-bindings-before.json', inputs.map(({ data, ...record }) => record));
for (const [index, name] of [[0, 'HostReleaseTimingEvidence.cs'], [1, 'SelfTest.cs']]) {
  fs.writeFileSync(path.join(run, name), inputs[index].data);
  if (sha(fs.readFileSync(path.join(run, name))) !== inputs[index].sha256) throw new Error('Snapshot hash mismatch.');
}
fs.writeFileSync(path.join(run, 'NuGet.Config'), '<configuration><packageSources><clear /></packageSources></configuration>\n');
const project = path.join(run, 'HostReleaseTimingEvidenceSelfTest.csproj');
fs.writeFileSync(project, `<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <UseAppHost>false</UseAppHost>
    <Deterministic>true</Deterministic>
    <EnableNETAnalyzers>false</EnableNETAnalyzers>
    <NuGetAudit>false</NuGetAudit>
    <RestoreSources></RestoreSources>
    <AssemblyName>HostReleaseTimingEvidenceSelfTest</AssemblyName>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="HostReleaseTimingEvidence.cs" />
    <Compile Include="SelfTest.cs" />
  </ItemGroup>
</Project>\n`);
const commands = [];
function execute(name, args) {
  const startedUtc = new Date().toISOString();
  const result = spawnSync(process.env.DOTNET_EXE ?? 'dotnet', args, {
    cwd: run, encoding: 'utf8', timeout: 60_000, windowsHide: true, maxBuffer: 8 * 1024 * 1024,
    env: { ...process.env, DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_NOLOGO: '1',
      DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1', DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE: 'true' },
  });
  fs.writeFileSync(path.join(run, `${name}.stdout.log`), result.stdout ?? '');
  fs.writeFileSync(path.join(run, `${name}.stderr.log`), result.stderr ?? '');
  commands.push({ name, executable: process.env.DOTNET_EXE ?? 'dotnet', args, startedUtc,
    finishedUtc: new Date().toISOString(), status: result.status, error: result.error?.message ?? null });
  save('commands.json', commands);
  if (result.error || result.status !== 0) throw new Error(`${name} failed; see ${path.join(run, `${name}.stdout.log`)}`);
  return result.stdout;
}
let result, failure;
try {
  const sdk = execute('sdk-version', ['--version']).trim();
  if (!sdk.startsWith('10.')) throw new Error(`Expected net10 SDK; got ${sdk}`);
  execute('restore', ['restore', project, '--configfile', path.join(run, 'NuGet.Config'), '--nologo', '--verbosity', 'minimal']);
  execute('build', ['build', project, '--no-restore', '--configuration', 'Release', '--nologo', '--verbosity', 'minimal']);
  const fixture = path.join(run, 'bin/Release/net10.0/HostReleaseTimingEvidenceSelfTest.dll');
  const output = execute('selftest', [fixture]);
  result = JSON.parse(output.replace(/^\uFEFF/, ''));
  save('selftest-results.json', result);
  if (result.failed !== 0 || result.pureInjected !== true || result.hostOrHardwareStarted !== false)
    throw new Error('Unexpected fixture result.');
} catch (error) {
  failure = String(error?.message ?? error);
  // Retain the case-level report even if a fixture exits nonzero.
  const log = path.join(run, 'selftest.stdout.log');
  if (fs.existsSync(log)) {
    try { result = JSON.parse(fs.readFileSync(log, 'utf8').replace(/^\uFEFF/, '')); save('selftest-results.json', result); }
    catch { /* Compiler/process errors are preserved in command logs. */ }
  }
}
const after = inputs.map(({ source, sha256 }) => {
  const data = fs.readFileSync(source);
  return { source, sha256: sha(data), unchanged: sha(data) === sha256 };
});
save('source-bindings-after.json', after);
if (!after.every(row => row.unchanged)) failure ??= 'An input changed during validation; rerun against the new snapshot.';
const summary = {
  version: 1, status: failure ? 'fail' : 'pass', failure: failure ?? null, runDirectory: run,
  targetFramework: 'net10.0', warningsAsErrors: true, productionHostBuiltOrRun: false,
  sdkOrDeviceInvoked: false, onlyHelperAndFixtureCompiled: true, sourceStable: after.every(row => row.unchanged),
  passed: result?.passed ?? null, failed: result?.failed ?? null,
  bindings: inputs.map(({ data, ...record }) => record),
};
save('validation-summary.json', summary);
fs.writeFileSync(path.join(out, 'latest-validation.json'), JSON.stringify(summary, null, 2) + '\n');
console.log(JSON.stringify(summary, null, 2));
process.exitCode = failure ? 1 : 0;