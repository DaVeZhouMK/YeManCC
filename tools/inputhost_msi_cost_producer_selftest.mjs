import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(repo,
  '../../../Build/Validation/MSI-ProducerContract', new Date().toISOString().replace(/[:.]/g, '-'));
if (fs.existsSync(out)) throw Error('Use a new output directory to preserve prior evidence: ' + out);
fs.mkdirSync(out, { recursive: true });
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const programPath = path.join(repo, 'InputHost/Program.cs');
const program = fs.readFileSync(programPath), text = program.toString('utf8');
const methods = [];
for (const [start, end] of [
  ['        private void QueueMsiCostWindow(long now, long windowMs)', '        // R5:'],
  ['        private void ResetCostWindow()', '        private bool TrySubmitNeutral('],
  ['        private void RecordCostFrame(long submitUs, bool rejected)', '        // R3 补充：GC'],
  ['        private void MaybeEmitCostWindow()', '        // Only bounded in-memory copies happen here.'],
]) {
  const a = text.indexOf(start), b = text.indexOf(end, a);
  if (a < 0 || b <= a || text.indexOf(start, a + start.length) >= 0) throw Error('Method anchor changed: ' + start);
  methods.push(text.slice(a, b));
}
const fixturePath = path.join(repo, 'tools/inputhost_msi_cost_producer_selftest.cs');
const fixture = fs.readFileSync(fixturePath, 'utf8');
const marker = '// EXACT_PRODUCTION_METHODS';
if (fixture.split(marker).length !== 2) throw Error('Fixture insertion marker');
fs.writeFileSync(path.join(out, 'Fixture.cs'), fixture.replace(marker, methods.join('\n').replaceAll('Environment.TickCount64', 'FixtureClock.Tick')));
const sources = [{ source: programPath, sha256: sha(program) },
  { source: fixturePath, sha256: sha(fs.readFileSync(fixturePath)) }];
for (const name of ['MsiCostWindowDiagnostics.cs', 'MsiCostWindowDispatcher.cs', 'HostCpuCostWindow.cs', 'SteamDeckPadSubmissionEvidence.cs']) {
  const source = path.join(repo, 'InputHost', name), bytes = fs.readFileSync(source);
  fs.writeFileSync(path.join(out, name), bytes); sources.push({ source, sha256: sha(bytes) });
}
fs.writeFileSync(path.join(out, 'NuGet.Config'), '<configuration><packageSources><clear /></packageSources></configuration>\n');
fs.writeFileSync(path.join(out, 'Fixture.csproj'), '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>\n');
const env = { ...process.env, DOTNET_GENERATE_ASPNET_CERTIFICATE: 'false', DOTNET_CLI_TELEMETRY_OPTOUT: '1' };
function run(args, log) {
  const r = spawnSync('dotnet', args, { cwd: out, env, encoding: 'utf8', timeout: 120000, maxBuffer: 4 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, log), (r.stdout ?? '') + (r.stderr ?? ''));
  if (r.error || r.status !== 0) throw Error(log + ': ' + (r.error ?? r.status));
}
const profiles = [];
for (const profile of ['Release', 'Debug']) {
  run(['build', 'Fixture.csproj', '-c', profile, '--nologo'], profile + '-build.log');
  const resultPath = path.join(out, profile + '-result.json');
  run([path.join(out, 'bin', profile, 'net10.0-windows/Fixture.dll'), resultPath], profile + '-run.log');
  const result = JSON.parse(fs.readFileSync(resultPath, 'utf8'));
  if (!result.pass) throw Error('Failed profile ' + profile);
  profiles.push({ profile, ...result });
  console.log(`${profile}: ${result.groups} groups, ${result.checks} checks`);
}
for (const r of sources) if (sha(fs.readFileSync(r.source)) !== r.sha256) throw Error('Source changed: ' + r.source);
fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify({ pass: true, sources, profiles,
  queueAndResetBytesTransformed: false, cadenceClockSeam: 'Environment.TickCount64 -> FixtureClock.Tick', actualMethodSha256: sha(methods.join('\n')), sourceStableDuringRun: true,
  limit: 'Diagnostic producer protocol with injected runtime, flag, SDK and sink; no real virtual submit, ACK, jitter or hardware acceptance.' }, null, 2) + '\n');
console.log(out);
