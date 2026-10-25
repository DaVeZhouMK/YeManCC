// Extract only the current PnP census functions. No Host project, SDK, native
// DLL declarations or product entry point enters the generated BCL executable.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const workspace = path.resolve(repo, '..', '..', '..');
const evidenceRoot = path.join(workspace, 'Build', 'Validation', 'MSI-Controller-R12-20261013', 'pnp-census-gap-20261014', 'actual-functions');
if (process.argv.length > 3) throw new Error('usage: node verify-msi-pnp-enumeration-evidence.mjs [output-directory]');
const out = path.resolve(process.argv[2] ?? path.join(evidenceRoot, 'with-msi-capture'));
const relativeOut = path.relative(evidenceRoot, out);
if (!relativeOut || relativeOut.startsWith('..') || path.isAbsolute(relativeOut)) throw new Error('Use a child evidence directory; the initial actual-functions baseline must never be overwritten.');
fs.mkdirSync(out, { recursive: true });
const save = (name, value) => fs.writeFileSync(path.join(out, name), JSON.stringify(value, null, 2) + '\n', 'utf8');
const sha = value => crypto.createHash('sha256').update(value).digest('hex').toUpperCase();
const binding = file => ({ path: file, sha256: sha(fs.readFileSync(file)) });
const sourcePath = path.join(repo, 'InputHost', 'Program.cs');
const testPath = path.join(repo, 'tools', 'msi_pnp_enumeration_actual_selftest.cs');
const helperPath = path.join(repo, 'InputHost', 'MsiPnpCensusDiagnostic.cs');
const initialReportPath = path.join(evidenceRoot, 'report.json');
const initialSummaryPath = path.join(evidenceRoot, 'validation-summary.json');
const initialReport = JSON.parse(fs.readFileSync(initialReportPath, 'utf8'));
if (initialReport.caseCount !== 15 || initialReport.checkCount !== 107 || !initialReport.passed || initialReport.sourceSha256 !== '081BE23AF6E4FB2CBC02D8D345D6F7CC4AB1B36E09BCC56B7F7963D920955084') throw new Error('Unexpected initial baseline; refusing comparison.');
const inputs = [sourcePath, helperPath, testPath, fileURLToPath(import.meta.url), initialReportPath, initialSummaryPath];
const before = inputs.map(binding);
save('source-bindings-before.json', before);
const text = fs.readFileSync(sourcePath, 'utf8');
// Preserve offsets when masking comments and normal/verbatim C# literals.
// Extracted members use these forms; raw-string syntax fails closed below.
const mask = text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|@"(?:""|[^"])*"|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,
  token => token.replace(/[^\r\n]/g, ' '));
const lineAt = offset => text.slice(0, offset).split('\n').length;
const escapeRegExp = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
function balanced(open, left, right) {
  let depth = 0;
  for (let i = open; i < mask.length; ++i) {
    if (mask[i] === left) ++depth;
    else if (mask[i] === right && --depth === 0) return i + 1;
  }
  throw new Error('Unbalanced extracted member.');
}
function extract(name, kind) {
  const method = `^[ \\t]*private\\s+static\\s+[^;{}=\\n]+\\b${escapeRegExp(name)}\\s*\\(`;
  const type = `^[ \\t]*private\\s+(?:readonly\\s+record\\s+)?struct\\s+${escapeRegExp(name)}\\b`;
  const constant = `^[ \\t]*private\\s+(?:const\\s+|static\\s+readonly\\s+)[^;\\n]+\\b${escapeRegExp(name)}\\s*=`;
  const found = [...mask.matchAll(new RegExp(kind === 'method' ? method : kind === 'type' ? type : constant, 'gm'))];
  if (found.length !== 1) throw new Error(`Expected exactly one ${kind} ${name}; found ${found.length}.`);
  let start = found[0].index;
  let cursor = start + found[0][0].length;
  if (kind === 'method') cursor = balanced(cursor - 1, '(', ')');
  else if (kind === 'type' && mask.slice(cursor).trimStart().startsWith('(')) {
    cursor = balanced(mask.indexOf('(', cursor), '(', ')');
  }
  while (/\s/.test(mask[cursor] ?? '') && cursor < mask.length) ++cursor;
  let end;
  if (kind === 'constant' || mask[cursor] === ';' || mask.slice(cursor, cursor + 2) === '=>') {
    const semicolon = mask.indexOf(';', cursor);
    if (semicolon < 0) throw new Error(`Missing terminator for ${name}.`);
    end = semicolon + 1;
  } else if (mask[cursor] === '{') end = balanced(cursor, '{', '}');
  else throw new Error(`Unsupported member syntax for ${name}.`);
  if (name === 'SP_DEVINFO_DATA') {
    const previousLineStart = text.lastIndexOf('\n', start - 2) + 1;
    if (text.slice(previousLineStart, start).trim() !== '[StructLayout(LayoutKind.Sequential)]') throw new Error('SP_DEVINFO_DATA layout attribute changed.');
    start = previousLineStart;
  }
  const original = text.slice(start, end);
  if (original.includes('"""')) throw new Error(`Raw-string syntax requires review in ${name}.`);
  return { name, kind, startLine: lineAt(start), endLine: lineAt(end - 1), original, start, end };
}
const methodNames = ['ObserveVirtualEndpoint', 'EnumeratePresentPnpNodes', 'GetPnpInstanceId', 'GetPnpHardwareIds',
  'IsHidMaestroPrimaryRoot', 'IsHidMaestroXboxCompanionRoot', 'IsSteamDeckCompositeHardwareId', 'IsSteamDeckUsbRoot',
  'GetPnpAncestors', 'IsPnpNodeStarted', 'DescribeVirtualEndpoint', 'CaptureExistingMsiPnpCensus'];
const typeNames = ['PnpNode', 'VirtualEndpointObservation', 'SP_DEVINFO_DATA'];
const constantNames = ['ConfigurationManagerSuccess', 'DeviceNodeStarted', 'InvalidDeviceInfoSet'];
const members = [...methodNames.map(n => extract(n, 'method')), ...typeNames.map(n => extract(n, 'type')), ...constantNames.map(n => extract(n, 'constant'))];
const apis = ['SetupDiGetClassDevsW', 'SetupDiEnumDeviceInfo', 'SetupDiGetDeviceInstanceIdW',
  'SetupDiGetDeviceRegistryPropertyW', 'SetupDiDestroyDeviceInfoList', 'CM_Get_Parent', 'CM_Get_Device_IDW', 'CM_Get_DevNode_Status'];
const apiPattern = new RegExp(`\\b(${apis.join('|')})\\s*(?=\\()`, 'g');
const transformations = [];
const extracted = members.map(member => {
  const replacements = [];
  let generated = member.original.replace(apiPattern, (full, name) => {
    replacements.push({ from: name, to: `Fake.${name}` }); return `Fake.${name}`;
  });
  generated = generated.replace(/\bMarshal\.GetLastWin32Error\s*\(\s*\)/g, full => {
    replacements.push({ from: full, to: 'Fake.GetLastWin32Error()' }); return 'Fake.GetLastWin32Error()';
  });
  transformations.push(...replacements.map(r => ({ member: member.name, ...r })));
  return { ...member, generated, replacements };
});
const sourceFileHash = before[0].sha256;
const memberBindings = extracted.map(({ name, kind, startLine, endLine, original, generated, replacements }) => ({
  name, kind, sourcePath, sourceFileSha256: sourceFileHash, startLine, endLine,
  sourceMemberSha256: sha(Buffer.from(original, 'utf8')), extractedMemberSha256: sha(Buffer.from(generated, 'utf8')),
  replacements, fixture: false,
  notes: name === 'SP_DEVINFO_DATA' ? 'Actual source structure including its sequential-layout attribute; not a copied fixture.' : ''
}));
const generatedSource = `// Generated from current InputHost/Program.cs; native calls explicitly rebound to Fake.\n#nullable enable\nusing YeManInputHost.Diagnostics;\nusing System.Runtime.InteropServices;\nusing System.Text;\ninternal static partial class ActualPnp\n{\n${extracted.map(m => m.generated).join('\n\n')}\n}\n`;
if (/\b(?:DllImport|LibraryImport|extern)\b/.test(generatedSource) || /Marshal\.GetLastWin32Error/.test(generatedSource)) throw new Error('Native import/last-error reference survived extraction.');
const leftovers = generatedSource.match(new RegExp(`(?<!Fake\\.)\\b(?:${apis.join('|')})\\s*\\(`, 'g'));
if (leftovers) throw new Error('A PnP call was not rebound to Fake.');
fs.writeFileSync(path.join(out, 'actual-pnp-functions.generated.cs'), generatedSource, 'utf8');
save('method-source-bindings.json', memberBindings);
save('extraction.json', { source: before[0], members: memberBindings, nativeRebindings: transformations,
  actualMethodCount: methodNames.length, actualTypeCount: typeNames.length, actualConstantCount: constantNames.length,
  structFixtureUsed: false, retainedBcl: ['Marshal.SizeOf', 'Encoding.Unicode', 'StringBuilder', 'Collections', 'LINQ', 'JSON'],
  scope: 'Actual method bodies/types/constants with call-name substitutions only. Host entry point, SDK and native DLL declarations are excluded.' });
const csString = value => JSON.stringify(value);
fs.writeFileSync(path.join(out, 'actual-pnp-source-binding.generated.cs'),
  `internal static partial class ActualPnp { private const string SourceSha256 = "${sourceFileHash}"; private const string HelperSha256 = "${before[1].sha256}"; private const string ExtractionSha256 = "${sha(Buffer.from(generatedSource, 'utf8'))}"; private const string InitialReportPath = ${csString(initialReportPath)}; private const string InitialReportSha256 = "${before[4].sha256}"; }\n`, 'utf8');
const xml = s => s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const project = path.join(out, 'MsiPnpEnumerationActualSelfTest.csproj');
fs.writeFileSync(project, `<Project Sdk="Microsoft.NET.Sdk">\n  <PropertyGroup>\n    <OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework>\n    <ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable>\n    <TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n    <UseAppHost>false</UseAppHost><Deterministic>true</Deterministic><NuGetAudit>false</NuGetAudit>\n  </PropertyGroup>\n  <ItemGroup>\n    <Compile Include="${xml(testPath)}" />\n    <Compile Include="${xml(helperPath)}" Link="MsiPnpCensusDiagnostic.cs" />\n    <Compile Include="actual-pnp-functions.generated.cs" />\n    <Compile Include="actual-pnp-source-binding.generated.cs" />\n  </ItemGroup>\n</Project>\n`, 'utf8');
const dotnet = path.join(process.env.ProgramFiles ?? 'C:\\Program Files', 'dotnet', 'dotnet.exe');
function execute(args, label) {
  save(`${label}-command.json`, { executable: dotnet, arguments: args, cwd: out });
  const result = spawnSync(dotnet, args, { cwd: out, encoding: 'utf8', windowsHide: true, maxBuffer: 16 * 1024 * 1024, timeout: 120000 });
  fs.writeFileSync(path.join(out, `${label}.log`), (result.stdout ?? '') + (result.stderr ?? '') + (result.error ? `\n${result.error}\n` : ''), 'utf8');
  return result;
}
const sdk = execute(['--version'], 'sdk');
if (sdk.status !== 0 || !(sdk.stdout ?? '').trim().startsWith('10.')) throw new Error('An installed .NET 10 SDK is required; no installation attempted.');
const emptyFeed = path.join(out, 'empty-nuget-feed');
fs.mkdirSync(emptyFeed, { recursive: true });
const build = execute(['build', project, '--configuration', 'Release', '--artifacts-path', path.join(out, 'artifacts'),
  '--source', emptyFeed, '-p:NuGetAudit=false', '-p:RestoreIgnoreFailedSources=true', '--nologo'], 'build');
let run = null, report = null;
const binary = path.join(out, 'artifacts', 'bin', 'MsiPnpEnumerationActualSelfTest', 'release', 'MsiPnpEnumerationActualSelfTest.dll');
const reportPath = path.join(out, 'report.json');
if (build.status === 0) {
  run = execute([binary, reportPath], 'run');
  if (fs.existsSync(reportPath)) report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
}
const after = inputs.map(binding);
save('source-bindings-after.json', after);
const sourceStable = before.every((b, i) => b.sha256 === after[i].sha256);
const passed = sourceStable && build.status === 0 && run?.status === 0 && report?.passed === true;
const summary = {
  utc: new Date().toISOString(), passed, sourceStable, sdk: sdk.stdout.trim(), source: before,
  generatedSource: binding(path.join(out, 'actual-pnp-functions.generated.cs')),
  linkedPureHelper: before[1], initialBaseline: { report: before[4], summary: before[5], sourceSha256: initialReport.sourceSha256, caseCount: 15, checkCount: 107, preserved: sourceStable },
  sourceBindings: path.join(out, 'method-source-bindings.json'), actualMethodCount: methodNames.length,
  actualTypeCount: typeNames.length, actualConstantCount: constantNames.length, structFixtureUsed: false,
  buildExitCode: build.status, runExitCode: run?.status ?? null,
  binary: build.status === 0 ? binding(binary) : null, report: reportPath,
  tests: report ? { caseCount: report.caseCount, checkCount: report.checkCount, failureCount: report.failureCount,
    actualObserveCalls: report.actualObserveCalls, fakeApiCallTotals: report.fakeApiCallTotals, onOffFixtureCount: report.onOffFixtureCount, helperBoundaryCount: report.helperBoundaryCount } : null,
  recordedGaps: report?.currentGaps ?? [],
  expectedCurrentBehavior: 'Known classification gaps are expected observations in this baseline; they are not suppressed and do not cause a test failure.',
  scope: 'Generated pure net10 executable with actual extracted census/capture members, linked pure diagnostic helper and managed fake API replacements; original 15 fixtures compared off/on and against preserved initial results. No InputHost project, SDK, product or native DLL execution.',
  liveDeviceApiCalls: 0, registryApiCalls: 0, serviceApiCalls: 0, hostExecuted: false, sdkExecuted: false,
  limits: 'These finite fixtures reproduce actual code decisions. They do not certify a live census, classification completeness or exact-owned absence.'
};
save('validation-summary.json', summary);
if (report) save('current-behavior-gaps.json', { source: before[0], report: reportPath, gaps: report.currentGaps,
  conclusion: 'Original probe results and SetupDi/CM traces are compared off/on. Query-gap counters record current loss paths without granting complete classification or exact-owned absence.' });
console.log(`MSI_PNP_ACTUAL_VALIDATION passed=${passed} build=${build.status} run=${run?.status ?? 'not-run'} cases=${report?.caseCount ?? 'none'} checks=${report?.checkCount ?? 'none'} out=${out}`);
if (!passed) console.error(`See ${path.join(out, build.status !== 0 ? 'build.log' : 'run.log')}`);
process.exitCode = passed ? 0 : 1;