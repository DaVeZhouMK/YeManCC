// Builds and runs only a BCL fixture linked to the three diagnostic helpers.
// No Host build, SDK reference/loading, product execution or live device calls.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const script = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(script), '..');
const workspace = path.resolve(repo, '..', '..', '..');
const root = path.join(workspace, 'Build', 'Validation', 'MSI-Controller-R12-20261013',
  'sdk-declared-identity-implementation-20261014', 'pure-identity-test');
if (process.argv.length > 3) throw new Error('usage: node verify-msi-sdk-declared-identity.mjs [output-directory]');
const out = path.resolve(process.argv[2] ?? root);
const relative = path.relative(root, out);
if (relative === '..' || relative.startsWith('..' + path.sep) || path.isAbsolute(relative))
  throw new Error('Output must remain within the authorized pure-identity-test evidence directory.');
fs.mkdirSync(out, { recursive: true });
const save = (name, value) => fs.writeFileSync(path.join(out, name), JSON.stringify(value, null, 2) + '\n', 'utf8');
const sha = value => crypto.createHash('sha256').update(value).digest('hex').toUpperCase();
const binding = file => ({ path: file, sha256: sha(fs.readFileSync(file)) });
const project = path.join(repo, 'tools', 'MsiSdkDeclaredIdentityDiagnosticSelfTest', 'MsiSdkDeclaredIdentityDiagnosticSelfTest.csproj');
const fixture = path.join(path.dirname(project), 'Program.cs');
const inputs = [path.join(repo, 'InputHost', 'MsiSdkDeclaredIdentityDiagnostic.cs'),
  path.join(repo, 'InputHost', 'MsiNonSdReleaseDiagnostic.cs'), path.join(repo, 'InputHost', 'MsiPnpCensusDiagnostic.cs'),
  fixture, project, script];
if (fs.existsSync(path.join(out, 'validation-summary.json')) || fs.existsSync(path.join(out, 'report.json')))
  throw new Error('Evidence already exists; choose a new child directory to preserve earlier results.');
const before = inputs.map(binding);
save('source-bindings-before.json', before);
const dotnet = path.join(process.env.ProgramFiles ?? 'C:/Program Files', 'dotnet', 'dotnet.exe');
const env = { ...process.env, DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_NOLOGO: '1',
  DOTNET_CLI_HOME: path.join(out, 'dotnet-cli'), NUGET_PACKAGES: path.join(out, 'nuget-packages') };
function execute(label, args) {
  save(label + '-command.json', { executable: dotnet, arguments: args, cwd: repo, timeoutMs: 60000 });
  const result = spawnSync(dotnet, args, { cwd: repo, env, encoding: 'utf8', timeout: 60000,
    windowsHide: true, maxBuffer: 4 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, label + '.log'), (result.stdout ?? '') + (result.stderr ?? '') +
    (result.error ? '\n' + result.error.stack + '\n' : ''), 'utf8');
  return { exitCode: result.status, signal: result.signal, error: result.error?.message ?? null,
    timedOut: result.error?.code === 'ETIMEDOUT' };
}
const version = execute('sdk-version', ['--version']);
const sdk = fs.readFileSync(path.join(out, 'sdk-version.log'), 'utf8').trim();
if (version.exitCode !== 0 || !/^10\./.test(sdk)) throw new Error('A .NET 10 SDK is required; see sdk-version.log.');
const build = execute('build', ['build', project, '-c', 'Release', '--artifacts-path', path.join(out, 'artifacts'),
  '-p:RestoreIgnoreFailedSources=true', '-p:RestoreSources=', '-p:NuGetAudit=false', '-v:minimal']);
const assembly = path.join(out, 'artifacts', 'bin', 'MsiSdkDeclaredIdentityDiagnosticSelfTest', 'release',
  'MsiSdkDeclaredIdentityDiagnosticSelfTest.dll');
const reportPath = path.join(out, 'report.json');
const run = build.exitCode === 0 ? execute('run', [assembly, reportPath]) : null;
const after = inputs.map(binding);
save('source-bindings-after.json', after);
const sourceStable = before.every((b, i) => b.sha256 === after[i].sha256);
const report = fs.existsSync(reportPath) ? JSON.parse(fs.readFileSync(reportPath, 'utf8')) : null;
const reportSourcesMatched = report?.sourceBindings?.length === before.length &&
  report.sourceBindings.every(b => before.some(other => other.path.toLowerCase() === b.path.toLowerCase() && other.sha256 === b.sha256));
const passed = build.exitCode === 0 && run?.exitCode === 0 && report?.passed === true &&
  sourceStable && reportSourcesMatched;
const summary = { utc: new Date().toISOString(), passed, sdk, sourceStable, reportSourcesMatched,
  scope: 'Actual linked pure diagnostic helpers; BCL fake controller fields/reflection and fake data only. No actual Host or SDK execution.',
  caseCount: report?.caseCount ?? 0, checkCount: report?.checkCount ?? 0, failureCount: report?.failureCount ?? null,
  build, run, sources: before, fixtureAssembly: fs.existsSync(assembly) ? binding(assembly) : null,
  report: reportPath, sdkReferences: 0, sdkAssembliesLoaded: 0, realSdkObjectReads: 0, liveDeviceApis: 0 };
save('validation-summary.json', summary);
console.log(JSON.stringify(summary, null, 2));
process.exitCode = passed ? 0 : 1;
