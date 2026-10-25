import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const self = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(self), '..');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileHash = name => hash(fs.readFileSync(name));
const need = (ok, message) => { if (!ok) throw Error(message); };

// Public entry point also serves the existing global simulator adapter.
export function runUsbipSetupEvidenceSelftest(output) {
  need(typeof output === 'string' && path.isAbsolute(output), '--out must be absolute');
  const out = path.resolve(output);
  need(!fs.existsSync(out), 'Fresh output directory required: ' + out);
  const rel = path.relative(repo, out);
  need(rel.startsWith('..' + path.sep) || path.isAbsolute(rel), 'Private output must be outside the product source');
  fs.mkdirSync(out, {recursive: true});
  const write = (relative, value) => {
    const name = path.resolve(out, relative);
    need(name.startsWith(out + path.sep), 'Output escaped private directory');
    fs.writeFileSync(name, value, {flag: 'wx'});
  };
  const json = (name, value) => write(name, JSON.stringify(value, null, 2) + '\n');
  const specPath = path.join(repo, 'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json');
  const toolchain = JSON.parse(fs.readFileSync(specPath, 'utf8').replace(/^\uFEFF/, ''));
  const dotnet = path.resolve(toolchain.dotnetRoot, 'dotnet.exe');
  need(path.isAbsolute(toolchain.dotnetRoot) && fs.existsSync(dotnet), 'Pinned absolute dotnet executable required');
  const pin = toolchain.pinned.find(row => path.resolve(row.path).toLowerCase() === dotnet.toLowerCase());
  need(pin && fileHash(dotnet) === pin.sha256, 'Pinned dotnet executable hash mismatch');
  const files = [specPath, path.join(repo, 'InputHost/UsbipSetupEvidence.cs'), path.join(repo, 'tools/fixtures/usbip_setup_evidence/Fixture.cs'), path.join(repo, 'tools/fixtures/usbip_setup_evidence/README.md'), self];
  const inputs = files.map(name => ({path: name, sha256: fileHash(name), bytes: fs.statSync(name).size}));
  const substitutions = [], commands = [];
  let sourcesStillCurrent = false;
  try {
    let source = fs.readFileSync(files[1], 'utf8').replace(/^\uFEFF/, '').replaceAll('\r\n', '\n');
    const original = source;
    write('production.original.cs.txt', fs.readFileSync(files[1]));
    function replaceOnce(before, after, leaf) {
      need(source.split(before).length === 2, 'Production leaf anchor drift: ' + leaf);
      source = source.replace(before, after);
      substitutions.push({leaf, original: before, replacement: after, originalSha256: hash(before), replacementSha256: hash(after)});
    }
    replaceOnce('Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC")', 'FixtureLeaves.LogRoot', 'private log directory');
    replaceOnce('Environment.GetFolderPath(Environment.SpecialFolder.Windows)', 'FixtureLeaves.WindowsRoot', 'trusted private Windows directory');
    function replaceTerminal(signature, body) {
      const start = source.indexOf(signature);
      need(start >= 0 && source.indexOf(signature, start + 1) < 0, 'Nonunique terminal: ' + signature);
      const open = source.indexOf('\n    {', start), end = source.indexOf('\n    }', open + 1);
      need(open > start && end > open, 'Terminal body boundary drift: ' + signature);
      const before = source.slice(start, end + '\n    }'.length);
      replaceOnce(before, signature + '\n    {\n        ' + body + '\n    }', signature);
    }
    replaceTerminal('    private static object?[]? ReadRegistryValues(string path, params string[] names)', 'return FixtureLeaves.ReadRegistryValues(path, names);');
    replaceTerminal('    private static (uint Locate, uint Result, uint Status, uint Problem) ReadCmStatus(string id)', 'return FixtureLeaves.ReadCmStatus(id);');
    let restored = source;
    for (const change of [...substitutions].reverse()) restored = restored.replace(change.replacement, change.original);
    need(restored === original && substitutions.length === 4, 'Unexpected production source edits in fixture');
    write('UsbipSetupEvidence.cs', source);
    write('Fixture.cs', fs.readFileSync(files[2]));
    write('NuGet.Config', '<configuration><packageSources><clear /></packageSources></configuration>\n');
    write('Fixture.csproj', `<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets>
    <FileVersion>99.3.4.5</FileVersion><AssemblyVersion>99.3.4.5</AssemblyVersion><Version>99.3.4.5</Version>
    <RestoreIgnoreFailedSources>true</RestoreIgnoreFailedSources><UseSharedCompilation>false</UseSharedCompilation>
  </PropertyGroup>
  <ItemGroup><Compile Include="UsbipSetupEvidence.cs"/><Compile Include="Fixture.cs"/></ItemGroup>
</Project>\n`);
    function run(name, args) {
      const r = spawnSync(dotnet, args, {cwd: out, encoding: 'utf8', windowsHide: true, timeout: 60000,
        env: {...process.env, DOTNET_NOLOGO: '1', DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE: 'true', DOTNET_CLI_HOME: path.join(out, 'dotnet-home'), NUGET_PACKAGES: path.join(out, 'nuget-packages'), TEMP: path.join(out, 'temp'), TMP: path.join(out, 'temp')}});
      write(name + '.log', (r.stdout ?? '') + (r.stderr ?? ''));
      commands.push({name, command: dotnet, args, exit: r.status, error: r.error?.message ?? null});
      need(r.status === 0 && !r.error, name + ' failed; see ' + path.join(out, name + '.log'));
      return (r.stdout ?? '').trim();
    }
    fs.mkdirSync(path.join(out, 'temp'));
    write('global.json', JSON.stringify({sdk: {version: toolchain.sdkVersion, rollForward: 'disable', allowPrerelease: false}}));
    const sdkVersion = run('dotnet-version', ['--version']);
    need(sdkVersion === toolchain.sdkVersion, 'Pinned SDK version mismatch');
    json('SOURCE-BINDINGS.json', {inputs, sourceBindings: inputs, substitutions, productionClassCompiled: true, productionFlowUnchanged: true,
      generated: ['UsbipSetupEvidence.cs', 'Fixture.cs', 'Fixture.csproj', 'NuGet.Config', 'global.json'].map(name => ({path: path.join(out, name), sha256: fileHash(path.join(out, name))})),
      lockCarrier: 'global XboxSdkDiagnostics.LogLock; only the same-name static lock carrier is provided', sdkVersion});
    run('build', ['build', 'Fixture.csproj', '-c', 'Release', '--nologo', '--configfile', path.join(out, 'NuGet.Config'), '--disable-build-servers']);
    run('run', [path.join(out, 'bin/Release/net10.0-windows/Fixture.dll'), out]);
    const fixture = JSON.parse(fs.readFileSync(path.join(out, 'FIXTURE-RESULT.json'), 'utf8'));
    sourcesStillCurrent = inputs.every(row => fileHash(row.path) === row.sha256);
    need(sourcesStillCurrent, 'Source changed during validation');
    need(fixture.pass && fixture.passed && fixture.status === 'PASS', 'Actual production fixture failed');
    const result = {...fixture, sourcesStillCurrent, productionClassCompiled: true, productionFlowUnchanged: true, sdkVersion, commands, inputs, sourceBindings: inputs,
      artifacts: ['bin/Release/net10.0-windows/Fixture.dll', 'UsbipSetupEvidence.cs', 'Fixture.cs'].map(name => ({path: path.join(out, name), sha256: fileHash(path.join(out, name))})),
      limitations: ['Software fixture only; no installed-package, actual USBIP/SDK, hardware or loaded driver memory validation.', 'Registry/CM terminal bodies and private directories are explicitly substituted; no production branch or limit is replaced.', 'SDK internal EnsureInstalled(null) migration callback remains unobserved; migrationBranchObserved=false.', 'Registered binding, disk bytes and caller availability observations are separate facts; no runtime policy changes.']};
    json('RESULT.json', result);
    console.log(JSON.stringify({pass: result.pass, passed: result.passed, status: result.status, cases: result.cases, checks: result.checks, sourcesStillCurrent, realSdkLoaded: false, hardwareIo: false, result: path.join(out, 'RESULT.json')}));
    return result;
  } catch (error) {
    sourcesStillCurrent = inputs.every(row => fs.existsSync(row.path) && fileHash(row.path) === row.sha256);
    let fixture = {};
    if (fs.existsSync(path.join(out, 'FIXTURE-RESULT.json'))) fixture = JSON.parse(fs.readFileSync(path.join(out, 'FIXTURE-RESULT.json'), 'utf8'));
    if (!fs.existsSync(path.join(out, 'RESULT.json'))) json('RESULT.json', {...fixture, pass: false, passed: false, status: 'FAIL', sourcesStillCurrent, error: String(error.stack ?? error), commands, inputs, sourceBindings: inputs, realSdkLoaded: false, realSdkCalled: false, hardwareIo: false, realDeviceApiCalled: false});
    throw error;
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === self) {
  const args = process.argv.slice(2);
  try {
    need(args.length === 2 && args[0] === '--out', 'Use --out ABS_FRESH_PRIVATE_DIRECTORY');
    runUsbipSetupEvidenceSelftest(args[1]);
  } catch (error) { console.error(error.stack ?? error); process.exitCode = 1; }
}
