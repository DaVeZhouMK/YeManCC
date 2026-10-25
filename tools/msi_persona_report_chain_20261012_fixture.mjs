// Offline fixture generation only. No SDK loading, product execution, or hardware APIs.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const repo = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/, '$1')), '..');
const root = 'G:/YeManCC-Work/_scratch/hidmaestro-locked-source-20261012';
const allowed = path.resolve('G:/YeManCC-Work/_scratch/msi-unified-reboot-20261012-1322/persona-agent');
if (process.argv[2] !== '--out' || !process.argv[3]) throw Error('Explicit --out PERSONA_AGENT_SUBDIRECTORY required');
const out = path.resolve(process.argv[3]);
if (!out.startsWith(allowed + path.sep)) throw Error('Fixture outputs must be under persona-agent');
const receipts = JSON.parse(fs.readFileSync(root + '/source-provenance.json', 'utf8'));
const lock = JSON.parse(fs.readFileSync(root + '/locked-dependency-receipt.json', 'utf8'));
if (lock.matchedSourceCommit !== '46054b862830fcec7bc98d72ccb7c4f0c0179fb1' || lock.coreExecuted !== false) throw Error('Wrong pinned commit/boundary');
for (const receipt of receipts) {
  const data = fs.readFileSync(receipt.path);
  const sha256 = crypto.createHash('sha256').update(data).digest('hex');
  const blob = crypto.createHash('sha1').update(Buffer.from(`blob ${data.length}\0`)).update(data).digest('hex');
  if (sha256.toUpperCase() !== receipt.sha256.toUpperCase() || blob !== receipt.gitBlobSha1 || receipt.commit !== lock.matchedSourceCommit)
    throw Error('Pinned source mismatch: ' + receipt.repoPath);
}
const hostPath = path.join(repo, 'InputHost', 'Program.cs');
const touchPath = path.join(repo, 'InputHost', 'ScreenTouchpadReport.cs');
const host = fs.readFileSync(hostPath, 'utf8'), touch = fs.readFileSync(touchPath, 'utf8');
const extracted = [];
function extract(text, name, origin) {
  const pattern = new RegExp(`^[ \\t]*private static[^\\n]*\\b${name}\\(`, 'm');
  const match = pattern.exec(text); if (!match) throw Error('Missing Host method: ' + name);
  const begin = match.index;
  // Strip comments/strings for delimiter scanning while retaining positions.
  let sanitized = text.slice(begin).replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\//g, s => s.replace(/[^\n]/g, ' '))
    .replace(/"(?:\\.|[^"\\])*"/g, s => ' '.repeat(s.length));
  let parens = 0, signatureEnd = -1;
  const open = sanitized.indexOf('(');
  for (let i = open; i < sanitized.length; i++) { if (sanitized[i] === '(') parens++; if (sanitized[i] === ')' && --parens === 0) { signatureEnd = i; break; } }
  if (signatureEnd < 0) throw Error('Invalid method signature: ' + name);
  let at = signatureEnd + 1; while (/\s/.test(sanitized[at])) at++;
  let end;
  if (sanitized.slice(at, at + 2) === '=>') end = sanitized.indexOf(';', at) + 1;
  else { if (sanitized[at] !== '{') throw Error('Unexpected method body: ' + name); let depth = 0;
    for (let i = at; i < sanitized.length; i++) { if (sanitized[i] === '{') depth++; if (sanitized[i] === '}' && --depth === 0) { end = i + 1; break; } } }
  if (!end) throw Error('Unclosed method: ' + name);
  const original = text.slice(begin, begin + end);
  extracted.push({ name, origin, line: text.slice(0, begin).split('\n').length,
    sha256: crypto.createHash('sha256').update(original).digest('hex') });
  return original.replace(/\bHMProfile\b/g, 'FakeProfile').replace(/\bCommandEnvelope\b/g, 'FakeFrame').replace(/\bScreenPad\b/g, 'FakeScreenPad');
}
const code = ['BuildDualSenseDataOnlyReport', 'WriteDs4ImuLe16', 'AxisToByte', 'TriggerToByte', 'MapHatOctant', 'MapFaceButtons', 'MapDigitalButtons', 'MapDsEdgePaddles']
  .map(name => extract(host, name, hostPath)).concat(['WriteDualSenseScreenPads', 'WriteDualSenseContact', 'WriteSonyContact'].map(name => extract(touch, name, touchPath)));
const fixture = `using System;\ninternal sealed record FakeProfile(int InputReportSize = 64);\ninternal readonly record struct FakeScreenPad(bool Active, bool Click, short X, short Y, ushort Pressure);\ninternal sealed record FakeFrame {\n public string Persona { get; init; } = "dualsense-edge"; public int Buttons { get; init; } public int BackButtons { get; init; } public int ScreenButtons { get; init; }\n public float Lx { get; init; } public float Ly { get; init; } public float Rx { get; init; } public float Ry { get; init; } public float LtRaw { get; init; } public float RtRaw { get; init; }\n public bool ImuAdmitted { get; init; } public double ImuGyroX { get; init; } public double ImuGyroY { get; init; } public double ImuGyroZ { get; init; } public double ImuAccelX { get; init; } public double ImuAccelY { get; init; } public double ImuAccelZ { get; init; }\n public bool ScreenPadsPresent { get; init; } public FakeScreenPad ScreenPadLeft { get; init; } public FakeScreenPad ScreenPadRight { get; init; }\n}\ninternal static class ExtractedHostBuilder20261012 {\n private const byte Ds4WireBatteryFull=0x0A; private const int DsEdgePaddleLeft=0x40, DsEdgePaddleRight=0x80; private const int DualSenseTouchWidth=1920,DualSenseTouchHeight=1080;\n public static byte[] Build(FakeFrame frame, long index) => BuildDualSenseDataOnlyReport(new FakeProfile(),frame,index,MapDsEdgePaddles(frame.BackButtons));\n${code.join('\n')}\n}\n`;
fs.mkdirSync(out, { recursive: true });
fs.writeFileSync(path.join(out, 'ExtractedHostBuilder.cs'), fixture);
const escape = p => p.replaceAll('&', '&amp;').replaceAll('"', '&quot;');
fs.writeFileSync(path.join(out, 'MsiReportChainFake.csproj'), `<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="ExtractedHostBuilder.cs"/><Compile Include="${escape(path.join(repo,'tools','msi_persona_report_chain_20261012.cs'))}"/><Compile Include="${escape(path.join(repo,'tools','msi_persona_report_chain_20261012_selftest.cs'))}"/></ItemGroup></Project>`);
fs.writeFileSync(path.join(out, 'NuGet.Config'), '<configuration><packageSources><clear /></packageSources></configuration>');
fs.writeFileSync(path.join(out, 'fixture-source-receipt.json'), JSON.stringify({ pinnedCommit: lock.matchedSourceCommit, pinnedFilesVerified: receipts.length,
  hostPath, hostSha256: crypto.createHash('sha256').update(host).digest('hex'), extracted, changesToExtractedMethods: 'type-name replacements only;no SDK calls',
  SDKLoaded: false, productExecuted: false, profileEdge: root+'/source/profiles/sony/dualsense-edge.json' }, null, 2));
console.log('GENERATED_FAKE_HOST_REPORT_CHAIN_FIXTURE ' + out + ' pinnedFiles=' + receipts.length);
