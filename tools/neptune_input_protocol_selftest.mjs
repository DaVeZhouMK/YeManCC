import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const runner = fileURLToPath(import.meta.url);
const repo = path.resolve(path.dirname(runner), '..');
const workspace = path.resolve(repo, '../../..');
const root = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/NeptuneProtocol');
const options = {};
for (let i = 2; i < process.argv.length; ++i) {
  const arg = process.argv[i];
  assert(['--out', '--recovery', '--vcvars'].includes(arg) && process.argv[i + 1], `Invalid option: ${arg}`);
  options[arg.slice(2)] = process.argv[++i];
}
const recovery = path.resolve(options.recovery || path.join(root, '../ProtocolRecovery'));
const out = path.resolve(options.out || path.join(root,
  `20261017-${new Date().toISOString().replace(/[:.]/g, '-')}-${crypto.randomBytes(3).toString('hex')}`));
assert.equal(path.dirname(out).toLowerCase(), root.toLowerCase(), 'Evidence must be a direct fresh child of NeptuneProtocol');
fs.mkdirSync(root, {recursive: true});
assert.equal(fs.realpathSync(root).toLowerCase(), path.resolve(root).toLowerCase(), 'Redirected evidence root');
fs.mkdirSync(out); // Exclusive creation, never overwrite a previous proof.
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const gitBlob = b => crypto.createHash('sha1').update(Buffer.from(`blob ${b.length}\0`)).update(b).digest('hex');
const load = f => JSON.parse(fs.readFileSync(f, 'utf8').replace(/^\uFEFF/, ''));
const rel = f => path.relative(out, f).replaceAll('\\', '/');
const json = (name, data) => fs.writeFileSync(path.join(out, name), JSON.stringify(data, null, 2) + '\n');
const receipt = {
  schema: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: 'Pure already-identified logical NCInput decoder, protocol software proof; unconsumed',
  commit: '06c0b9544db2b1f39abf9cd3796225ccfb096103',
  sourceInputs: [], binaries: [], configurations: [], mutations: [], oracleBuilds: [],
  physicalInput: false, nativeHidapiFraming: false, windowsRawInputFraming: false,
  actualBind: false, readerEnabled: false, capabilityFlagsChanged: false,
  completeInputConsumerAccepted: false, deployment: false,
};
const save = () => json('summary.json', receipt);
const freeze = (file, destination, meta = {}) => {
  const bytes = fs.readFileSync(file);
  const target = path.join(out, destination);
  fs.mkdirSync(path.dirname(target), {recursive: true});
  fs.writeFileSync(target, bytes);
  receipt.sourceInputs.push({origin: path.resolve(file), frozen: rel(target),
    bytes: bytes.length, sha256: sha(bytes), gitBlob: gitBlob(bytes), ...meta});
  return target;
};
const walk = dir => fs.readdirSync(dir, {withFileTypes: true}).sort((a, b) => a.name.localeCompare(b.name))
  .flatMap(e => e.isDirectory() ? walk(path.join(dir, e.name)) : e.isFile() ? [path.join(dir, e.name)] : []);
const run = (exe, args, log, cwd = out, env = process.env) => {
  const result = spawnSync(exe, args, {cwd, env, windowsHide: true, encoding: 'utf8', timeout: 60000,
    maxBuffer: 24 * 1024 * 1024});
  fs.mkdirSync(path.dirname(path.join(out, log)), {recursive: true});
  fs.writeFileSync(path.join(out, log), (result.stdout || '') + (result.stderr || ''));
  if (result.error) throw result.error;
  assert.equal(result.signal, null, `Process signal: ${log}`);
  return result;
};
const success = (result, label) => {
  assert.equal(result.status, 0, `${label}: exit ${result.status}; evidence: ${out}`);
  return result.stdout;
};
const replaceOnce = (text, before, after) => {
  assert.equal(text.split(before).length - 1, 1, `Mutation anchor is not unique: ${before}`);
  return text.replace(before, after);
};
const reportRun = result => {
  const m = /^SUMMARY packets=(\d+) checks=(\d+) failures=(\d+)\r?$/m.exec(result.stdout);
  assert(m, 'No C++ semantic summary');
  return {packets: +m[1], checks: +m[2], failures: +m[3], exit: result.status};
};

save();
try {
  const fixture = path.join(repo, 'tools/fixtures/neptune_input_protocol');
  const realHeader = path.join(repo, 'native/neptune_input_protocol.h');
  for (const f of [realHeader, runner, ...walk(fixture)])
    freeze(f, path.join('implementation', path.relative(repo, f)), {kind: 'implementation'});
  const headerBytes = fs.readFileSync(realHeader);
  const header = headerBytes.toString('utf8');
  const frozenFixture = path.join(out, 'implementation/tools/fixtures/neptune_input_protocol');
  const frozenHarness = path.join(frozenFixture, 'harness.cpp');

  const acquisitionPath = path.join(recovery, 'acquisition-historical.json');
  const acquisition = load(acquisitionPath);
  assert.equal(acquisition.reference, receipt.commit);
  assert.equal(acquisition.files.length, 40, 'Expected all 40 recovered historical files');
  freeze(acquisitionPath, 'recovery/acquisition-historical.json', {kind: 'primary-acquisition'});
  for (const item of acquisition.files) {
    assert.equal(item.status, 'verified');
    assert(item.path && !item.path.includes('..') && !path.isAbsolute(item.path));
    const original = path.join(recovery, 'historical-source', item.path);
    const bytes = fs.readFileSync(original);
    assert.equal(bytes.length, item.bytes, `Primary size: ${item.path}`);
    assert.equal(sha(bytes), item.sha256.toLowerCase(), `Primary SHA: ${item.path}`);
    assert.equal(gitBlob(bytes), item.gitBlob.toLowerCase(), `Primary blob: ${item.path}`);
    assert.equal(item.url,
      `https://raw.githubusercontent.com/Valkirie/HandheldCompanion/${receipt.commit}/${item.path}`);
    freeze(original, path.join('primary-source', item.path), {kind: 'primary-source',
      commit: receipt.commit, officialPath: item.path, url: item.url, verifiedGitBlob: item.gitBlob});
  }
  receipt.primaryFilesVerified = acquisition.files.length;
  for (const file of walk(path.join(recovery, 'decompiled')))
    freeze(file, path.join('shipped-ilspy', path.relative(path.join(recovery, 'decompiled'), file)),
      {kind: 'actual-shipped-static-decompilation'});
  for (const name of ['shipped-binaries.json', 'historical-commit.json', 'historical-tree.json',
    'decompile-steam.log', 'decompile-hidapi.log', 'decompile-controller.log'])
    freeze(path.join(recovery, name), path.join('recovery', name), {kind: 'recovery-receipt'});
  for (const item of load(path.join(recovery, 'shipped-binaries.json'))) {
    const actual = path.join(recovery, 'shipped-binaries', item.name);
    const bytes = fs.readFileSync(actual);
    assert.equal(bytes.length, item.bytes);
    assert.equal(sha(bytes), item.sha256.toLowerCase());
    receipt.binaries.push({...item, checkedCopy: actual, reverifiedSha256: sha(bytes),
      executed: false, referencedByOracle: false});
  }
  assert.equal(receipt.binaries.length, 3);
  // Static correspondence at the transport boundary. These files are NEVER compiled.
  const primaryHid = fs.readFileSync(path.join(out, 'primary-source/hidapi.net/HidDevice.cs'), 'utf8');
  const shippedHid = fs.readFileSync(path.join(out, 'shipped-ilspy/hidapi.net/hidapi/HidDevice.cs'), 'utf8');
  const primaryGate = fs.readFileSync(path.join(out, 'primary-source/steam-hidapi.net/NeptuneController.cs'), 'utf8');
  const shippedGate = fs.readFileSync(path.join(out, 'shipped-ilspy/steam-hidapi.net/steam_hidapi.net/NeptuneController.cs'), 'utf8');
  const wrapper = fs.readFileSync(path.join(out, 'primary-source/HandheldCompanion/Controllers/Steam/NeptuneController.cs'), 'utf8');
  assert(/\[65,\s*64\]/.test(wrapper), 'Fixed HC buffer capacity loop not found');
  assert(/e\.Buffer\[0\]\s*!=\s*1/.test(primaryGate));
  assert(/e\.Buffer\[1\]\s*!=\s*0/.test(primaryGate));
  assert(/case \(byte\)SCEventType\.DECK_INPUT_DATA/.test(primaryGate));
  assert(/e\.Buffer\[0\] != 1 \|\| e\.Buffer\[1\] != 0/.test(shippedGate));
  assert(/byte b = e\.Buffer\[2\]/.test(shippedGate) && /b == 9/.test(shippedGate));
  assert(/hid_read_timeout\(_deviceHandle, buffer, _inputBufferLen, timeout\)/.test(primaryHid));
  assert(/hid_read_timeout\(_deviceHandle, buffer, _inputBufferLen, timeout\)/.test(shippedHid));
  assert(/int len = Read\(_buffer\)/.test(primaryHid));
  assert(/BlockCopy\(_buffer, 0, _eventArgs.Buffer, 0, len\)/.test(primaryHid));
  assert(/int num = Read\(_buffer\)/.test(shippedHid));
  assert(/BlockCopy\(_buffer, 0, _eventArgs.Buffer, 0, num\)/.test(shippedHid));
  const gates = {};
  for (const [name, text] of [['primary', primaryGate], ['shipped', shippedGate]]) {
    const begin = text.indexOf('internal override void OnInputReceived');
    const end = text.indexOf('public override void Open');
    assert(begin >= 0 && end > begin);
    gates[name] = text.slice(begin, end).trim();
    assert(!/\.Length|GetReadLength|inputBufferLen/.test(gates[name]), 'Unexpected per-packet size gate');
  }
  json('logical-gates.json', {methods: gates,
    conclusion: 'No per-packet size gate in either exact source method; 60-byte minimum comes from NCInput layout, not wrapper capacities'});
  receipt.lengthContract = {
    packedNCInputBytes: 60, minimumLogicalActualBytes: 60,
    maximumLogicalActualBytes: 'capacity; no fixed whitelist',
    tail: 'Every byte beyond the complete 60-byte core is opaque and never read',
    upstreamPerPacketSizeWhitelist: false,
    primaryWrapperCapacities: [65, 64],
    managedReadCount: 'Read(byte[]) returns hid_read_timeout count; ReadLoop copies count into reused buffer',
    eventCount: 'No actual count is published in HidDeviceInputReceivedEventArgs',
    getReadLengthSymbolInSelectedHidDeviceSources: /GetReadLength/.test(primaryHid + shippedHid),
    actualNativeReturnLengthsObserved: false,
    nativeStrippingOrWin32PrefixProved: false,
    shortReadHazard: 'Potential retained-tail read in old consumer; no physical short read observed here',
  };
  json('source-bindings.json', {sources: receipt.sourceInputs, binaries: receipt.binaries,
    compiledScope: 'Only exact pure structure, enum, state and utility sources; no audited DLL reference'});

  const raw = load(path.join(frozenFixture, 'raw-logical-packets.json'));
  const corpus = [];
  const add = (id, b) => {assert.equal(b.length, 60); corpus.push({id, bytes: Buffer.from(b)});};
  for (const p of raw.packets) add(p.id, Buffer.from(p.hex, 'hex'));
  const blank = () => {const b = Buffer.alloc(60); b[0] = 1; b[2] = 9; return b;};
  for (let offset = 8; offset < 16; ++offset) for (let bit = 0; bit < 8; ++bit) {
    const b = blank(); b[offset] = 1 << bit; add(`wire-bit-${offset}-${bit}`, b);
  }
  // All 22 consecutive wire words, independent of canonical axis ordering.
  for (let offset = 16; offset < 60; offset += 2)
    for (const value of [-32768, -32767, -4660, -256, -1, 1, 256, 4660, 32767]) {
      const b = blank(); b.writeInt16LE(value, offset); add(`wire-word-${offset}-${value}`, b);
    }
  let rng = 0x4e435031;
  for (let i = 0; i < 128; ++i) {
    const b = Buffer.alloc(60);
    for (let j = 0; j < b.length; ++j) {
      rng ^= rng << 13; rng ^= rng >>> 17; rng ^= rng << 5; b[j] = rng & 255;
    }
    b[0] = 1; b[1] = 0; b[2] = 9; add(`deterministic-${i}`, b);
  }
  const logicalSizes = [60, 61, 62, 63, 64, 65, 66, 80, 129];
  const logical = corpus.flatMap(p => logicalSizes.map(n => ({id: `${p.id}-logical-${n}`,
    bytes: Buffer.concat([p.bytes, Buffer.alloc(n - 60, n === 64 ? 0xa5 : 0x5a)])})));
  assert.equal(new Set(logical.map(p => p.id)).size, logical.length);
  const packetFile = path.join(out, 'logical-corpus.tsv');
  fs.writeFileSync(packetFile, logical.map(p => `${p.id}\t${p.bytes.toString('hex')}`).join('\n') + '\n');
  receipt.corpus = {packets: logical.length, exact60BasePackets: corpus.length,
    testedLogicalLengths: logicalSizes,
    origin: 'hand-authored raw pins + all wire-bit and signed-word probes + seeded random; no decoder-generated bytes',
    file: rel(packetFile), sha256: sha(fs.readFileSync(packetFile))};

  const dotnetVersion = success(run('dotnet', ['--version'], 'dotnet-version.log'), 'dotnet version').trim();
  const major = Number(dotnetVersion.split('.')[0]); assert(major >= 8);
  const oracleEnv = {...process.env, DOTNET_CLI_HOME: path.join(out, 'dotnet-home'),
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1', DOTNET_CLI_TELEMETRY_OPTOUT: '1',
    DOTNET_NOLOGO: '1', NUGET_PACKAGES: path.join(out, 'nuget-packages')};
  const xml = value => value.replaceAll('&', '&amp;').replaceAll('"', '&quot;').replaceAll('<', '&lt;');
  const oracleSets = {
    primary: [
      'primary-source/steam-hidapi.net/Hid/HidEnums.cs',
      'primary-source/steam-hidapi.net/NeptuneControllerEnums.cs',
      'primary-source/steam-hidapi.net/NeptuneControllerInputState.cs',
      'primary-source/steam-hidapi.net/Util/StructUtils.cs'],
    shipped: [
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net.Hid/NCInput.cs',
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net/NeptuneControllerButton.cs',
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net/NeptuneControllerAxis.cs',
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net/NeptuneControllerInputState.cs',
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net/NeptuneControllerButtonState.cs',
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net/NeptuneControllerAxesState.cs',
      'shipped-ilspy/steam-hidapi.net/steam_hidapi.net.Util/StructUtils.cs'],
  };
  const oracleResults = {};
  for (const [name, set] of Object.entries(oracleSets)) {
    const dir = path.join(out, `oracle-${name}`); fs.mkdirSync(dir);
    const inputs = [...set.map(f => path.join(out, f)), path.join(frozenFixture, 'Oracle.cs')];
    const project = path.join(dir, 'Oracle.csproj');
    fs.writeFileSync(path.join(dir, 'NuGet.Config'),
      '<configuration><packageSources><clear /></packageSources></configuration>\n');
    fs.writeFileSync(project, `<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>` +
      `<OutputType>Exe</OutputType><TargetFramework>net${major}.0</TargetFramework>` +
      `<EnableDefaultCompileItems>false</EnableDefaultCompileItems><ImplicitUsings>disable</ImplicitUsings>` +
      `<Nullable>disable</Nullable><AllowUnsafeBlocks>false</AllowUnsafeBlocks>` +
      `<EnableNETAnalyzers>false</EnableNETAnalyzers><Deterministic>true</Deterministic>` +
      `</PropertyGroup><ItemGroup>${inputs.map(f => `<Compile Include="${xml(f)}" />`).join('')}` +
      `</ItemGroup><Target Name="RecordActualCompileInputs" BeforeTargets="CoreCompile">` +
      `<WriteLinesToFile File="actual-compile-inputs-$(Configuration).txt" Lines="@(Compile-&gt;'%(FullPath)')" Overwrite="true" />` +
      `</Target></Project>\n`);
    const configurationResults = [];
    for (const config of ['Release', 'Checked']) {
      const output = path.join(dir, config);
      const args = ['build', project, '--configuration', config, '--output', output,
        '--configfile', path.join(dir, 'NuGet.Config'), '--nologo',
        '-p:UseSharedCompilation=false', '-p:CheckForOverflowUnderflow=true',
        '-p:ImportDirectoryBuildProps=false', '-p:ImportDirectoryBuildTargets=false',
        `-p:Optimize=${config === 'Release' ? 'true' : 'false'}`];
      success(run('dotnet', args, `oracle-${name}/${config}-build.log`, dir, oracleEnv), `${name} oracle build`);
      const result = run('dotnet', [path.join(output, 'Oracle.dll'), packetFile],
        `oracle-${name}/${config}-oracle.json`, dir, oracleEnv);
      const parsed = JSON.parse(success(result, `${name} oracle run`));
      const actualInputList = path.join(dir, `actual-compile-inputs-${config}.txt`);
      const actualInputs = fs.readFileSync(actualInputList, 'utf8').replace(/^\uFEFF/, '').trim().split(/\r?\n/);
      for (const input of inputs) assert(actualInputs.includes(input), `Missing actual C# input: ${input}`);
      assert.equal(actualInputs.length, inputs.length + 2, 'Unexpected C# sources beyond two SDK assembly attributes');
      for (const input of actualInputs) {
        assert(input.toLowerCase().startsWith((out + path.sep).toLowerCase()), 'C# source escaped evidence');
        assert(!['NeptuneController.cs', 'SteamController.cs', 'HidDevice.cs'].includes(path.basename(input)));
      }
      if (oracleResults[name]) assert.deepEqual(parsed, oracleResults[name], `${name} configurations differ`);
      oracleResults[name] = parsed;
      configurationResults.push({configuration: config, buildArguments: args,
        actualCompileInputList: rel(actualInputList),
        actualCompileInputs: actualInputs.map(f => ({file: rel(f), sha256: sha(fs.readFileSync(f))})),
        oracleJson: `oracle-${name}/${config}-oracle.json`, assemblySha256: sha(fs.readFileSync(path.join(output, 'Oracle.dll')))});
    }
    receipt.oracleBuilds.push({name, sdk: dotnetVersion,
      inputs: inputs.map(f => ({frozen: rel(f), sha256: sha(fs.readFileSync(f))})),
      project: rel(project), projectSha256: sha(fs.readFileSync(project)), configurations: configurationResults});
    save();
  }
  assert.deepEqual(oracleResults.primary, oracleResults.shipped, 'Shipped and old official oracles disagree');
  const oracle = oracleResults.primary;
  assert.equal(oracle.size, 60); assert.equal(oracle.layout.length, 35);
  const fieldNames = 'ptype _a1 _a2 _a3 seq buttons0 buttons1 buttons2 buttons3 nop0 buttons5 buttons6 nop1 lpad_x lpad_y rpad_x rpad_y accel_x accel_y accel_z gpitch gyaw groll q1 q2 q3 q4 ltrig rtrig lthumb_x lthumb_y rthumb_x rthumb_y lpad_pressure rpad_pressure'.split(' ');
  assert.deepEqual(oracle.layout.map(f => f.name), fieldNames);
  const fieldOffsets = [0, 1, 2, 3, 4, 8, 9, 10, 11, 12, 13, 14, 15, ...Array.from({length: 22}, (_, i) => 16 + i * 2)];
  assert.deepEqual(oracle.layout.map(f => f.offset), fieldOffsets);
  assert.equal(oracle.layout.find(f => f.name === 'rpad_x').offset, 20);
  assert.equal(oracle.packets.length, logical.length);
  for (const p of oracle.packets) {
    assert.deepEqual(p.buttons.map(b => b.name), raw.buttons);
    assert.deepEqual(p.buttons.map(b => b.id), Array.from({length: 28}, (_, i) => i + 1));
    assert.deepEqual(p.axes.map(a => a.name), raw.axes);
    assert.deepEqual(p.axes.map(a => a.id), Array.from({length: 22}, (_, i) => i + 1));
  }
  for (const manual of raw.packets) for (const n of logicalSizes) {
    const expected = oracle.packets.find(p => p.id === `${manual.id}-logical-${n}`);
    assert.equal(expected.seq, manual.seq);
    assert.deepEqual(expected.buttons.filter(b => b.down).map(b => b.name),
      raw.buttons.filter(name => manual.down.includes(name)), `Manual buttons ${manual.id}`);
    assert.deepEqual(expected.axes.map(a => a.value), manual.axes, `Manual axes ${manual.id}`);
  }
  receipt.oracleAgreement = {size: oracle.size, fields: oracle.layout.length,
    buttons: 28, axes: 22, packets: logical.length, configurationsPerOrigin: 2,
    exactLayoutAndEnumAndValuesEqual: true, manualPinnedPackets: raw.packets.length};
  json('layout.json', {size: oracle.size, fields: oracle.layout, origins: ['primary', 'shipped']});
  const expectedFile = path.join(out, 'cpp-expected.tsv');
  fs.writeFileSync(expectedFile, oracle.packets.map((p, i) => {
    assert.equal(p.id, logical[i].id);
    const buttons = p.buttons.reduce((mask, b) => b.down ? mask + 2 ** (b.id - 1) : mask, 0);
    return [p.id, logical[i].bytes.toString('hex'), p.seq, buttons, ...p.axes.map(a => a.value)].join('\t');
  }).join('\n') + '\n');
  const enumAssertions = [...raw.buttons.map((name, i) =>
    `static_assert(static_cast<unsigned>(ymcc::neptune_input::Button::${name}) == ${i + 1});`),
    ...raw.axes.map((name, i) => `static_assert(static_cast<unsigned>(ymcc::neptune_input::Axis::${name}) == ${i + 1});`)];
  const enumHeader = path.join(out, 'canonical-enum-checks.h');
  fs.writeFileSync(enumHeader, '#include "neptune_input_protocol.h"\n' + enumAssertions.join('\n') + '\n');
  receipt.cppExpected = {file: rel(expectedFile), sha256: sha(fs.readFileSync(expectedFile))};

  let vcvars = options.vcvars;
  if (!vcvars) {
    const vswhere = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)',
      'Microsoft Visual Studio/Installer/vswhere.exe');
    const installation = success(run(vswhere,
      ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
      'toolchain-discovery.log'), 'MSVC discovery').trim();
    assert(installation && !/[\r\n]/.test(installation));
    vcvars = path.join(installation, 'VC/Auxiliary/Build/vcvars64.bat');
  }
  assert(fs.existsSync(vcvars) && !/[\r\n"%&|<>^!]/.test(vcvars));
  const setup = path.join(out, 'compiler-environment.cmd');
  assert(!/[\r\n"%&|<>^!]/.test(setup));
  fs.writeFileSync(setup, `@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const setupResult = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', setup],
    {encoding: 'utf8', windowsHide: true, timeout: 30000});
  if (setupResult.error) throw setupResult.error;
  assert.equal(setupResult.status, 0, 'MSVC setup failed');
  const env = {...process.env};
  for (const line of setupResult.stdout.split(/\r?\n/)) {
    const at = line.indexOf('='); if (at <= 0) continue;
    const key = line.slice(0, at), old = Object.keys(env).find(k => k.toLowerCase() === key.toLowerCase());
    env[old || key] = line.slice(at + 1);
  }
  for (const key of Object.keys(env)) if (['cl', '_cl_', 'link', '_link_'].includes(key.toLowerCase())) delete env[key];
  const compiler = path.join(env.VCToolsInstallDir, 'bin/Hostx64/x64/cl.exe');
  receipt.compiler = {vcvars, compiler, sha256: sha(fs.readFileSync(compiler)),
    vcToolsVersion: env.VCToolsVersion, windowsSdkVersion: env.WindowsSDKVersion};
  const configurations = [
    ['Release', ['/O2', '/DNDEBUG', '/MT']],
    ['Checked', ['/Od', '/RTC1', '/Zi', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd']],
  ];
  const compileAndRun = (dir, flags, includeDir) => {
    fs.mkdirSync(dir, {recursive: true});
    const exe = path.join(dir, 'neptune_input_protocol.exe');
    const args = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-', '/showIncludes', ...flags,
      `/I${includeDir}`, `/FI${enumHeader}`, frozenHarness,
      `/Fo${path.join(dir, 'harness.obj')}`, `/Fd${path.join(dir, 'compiler.pdb')}`,
      `/Fe${exe}`, '/link', '/INCREMENTAL:NO'];
    const compileLog = success(run(compiler, args, `${rel(dir)}/compile.log`, dir, env), `${rel(dir)} compile`);
    assert(compileLog.includes(path.join(includeDir, 'neptune_input_protocol.h')), 'Compiler did not include selected real/mutated header');
    const result = run(exe, [expectedFile], `${rel(dir)}/run.log`, dir, env);
    return {...reportRun(result), buildArguments: args, exeSha256: sha(fs.readFileSync(exe))};
  };
  for (const [name, flags] of configurations) {
    assert.equal(sha(fs.readFileSync(realHeader)), sha(headerBytes), 'Real header changed before build');
    const result = compileAndRun(path.join(out, name), flags, path.dirname(realHeader));
    assert.equal(result.exit, 0); assert.equal(result.failures, 0); assert.equal(result.packets, logical.length);
    receipt.configurations.push({name, realHeader, realHeaderSha256: sha(headerBytes), ...result});
    save();
  }
  const mutations = [
    ['wrong-axis-offset', h => replaceOnce(h, '48, 50, 52, 54, 16, 18, 20, 22', '46, 50, 52, 54, 16, 18, 20, 22')],
    ['wrong-signed-short', h => replaceOnce(h, 'return static_cast<std::int16_t>(value);', 'return static_cast<std::int16_t>(value < 0 ? 32767 : value);')],
    ['wrong-short-endian', h => replaceOnce(h, 'const std::uint32_t u = std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8);',
      'const std::uint32_t u = std::uint32_t{p[1]} | (std::uint32_t{p[0]} << 8);')],
    ['wrong-header', h => replaceOnce(h, 'logical[0] != 1 || logical[1] != 0', 'logical[0] != 1')],
    ['battery-event-accepted', h => replaceOnce(h, 'if (logical[2] != 9)', 'if (logical[2] != 9 && logical[2] != 4)')],
    ['short-actual-accepted-with-old-tail', h => replaceOnce(h,
      'if (actualBytes < kNCInputSize)', 'if (actualBytes < kNCInputSize && capacity < kNCInputSize)')],
    ['invented-length-whitelist', h => replaceOnce(h,
      'if (logical[0] != 1 || logical[1] != 0)',
      'if (actualBytes != 60 && actualBytes != 64 && actualBytes != 65) return {Error::InvalidActualBytes, {}};\n    if (logical[0] != 1 || logical[1] != 0)')],
    ['left-right-pad-press', h => replaceOnce(replaceOnce(h,
      '{Button::BtnRPadPress, 10, 0x04}', '{Button::BtnRPadPress, 10, 0x02}'),
      '{Button::BtnLPadPress, 10, 0x02}', '{Button::BtnLPadPress, 10, 0x04}')],
    ['left-right-pad-touch', h => replaceOnce(replaceOnce(h,
      '{Button::BtnRPadTouch, 10, 0x10}', '{Button::BtnRPadTouch, 10, 0x08}'),
      '{Button::BtnLPadTouch, 10, 0x08}', '{Button::BtnLPadTouch, 10, 0x10}')],
    ['rear-l4-l5-order', h => replaceOnce(replaceOnce(h,
      '{Button::BtnL4, 13, 0x02}', '{Button::BtnL4, 9, 0x80}'),
      '{Button::BtnL5, 9, 0x80}', '{Button::BtnL5, 13, 0x02}')],
    ['rear-r4-r5-order', h => replaceOnce(replaceOnce(h,
      '{Button::BtnR4, 13, 0x04}', '{Button::BtnR4, 10, 0x01}'),
      '{Button::BtnR5, 10, 0x01}', '{Button::BtnR5, 13, 0x04}')],
    ['gyro-roll-pitch-order', h => replaceOnce(h, '24, 26, 28, 34, 30, 32, 36', '24, 26, 28, 30, 34, 32, 36')],
    ['sequence-endian', h => replaceOnce(h,
      'std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |\n        (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24)',
      'std::uint32_t{p[3]} | (std::uint32_t{p[2]} << 8) |\n        (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[0]} << 24)')],
    ['reserved-bit-as-button', h => replaceOnce(h, '{Button::BtnQuickAccess, 14, 0x04}', '{Button::BtnQuickAccess, 14, 0x08}')],
    ['left-pad-helper-side', h => replaceOnce(h,
      'axis(Axis::LeftPadX), axis(Axis::LeftPadY), axis(Axis::LeftPadPressure)',
      'axis(Axis::RightPadX), axis(Axis::LeftPadY), axis(Axis::LeftPadPressure)')],
  ];
  for (const [name, mutate] of mutations) {
    const dir = path.join(out, 'mutations', name); fs.mkdirSync(dir, {recursive: true});
    const mutated = mutate(header);
    assert.notEqual(mutated, header);
    fs.writeFileSync(path.join(dir, 'neptune_input_protocol.h'), mutated);
    const results = [];
    for (const [configuration, flags] of configurations) {
      const result = compileAndRun(path.join(dir, configuration), flags, dir);
      assert.equal(result.exit, 1, `${name}: expected semantic failure, not crash`);
      assert(result.failures > 0, `${name}: mutation survived`);
      results.push({configuration, ...result});
    }
    receipt.mutations.push({name, headerSha256: sha(Buffer.from(mutated)), killed: true, results});
    save();
  }
  assert.equal(sha(fs.readFileSync(realHeader)), sha(headerBytes), 'Real header changed during proof');
  for (const item of receipt.sourceInputs.filter(i => i.kind === 'implementation'))
    assert.equal(sha(fs.readFileSync(item.origin)), item.sha256, `Implementation changed during proof: ${item.origin}`);
  receipt.status = 'passed'; receipt.completedAt = new Date().toISOString();
  receipt.mutationCoverage = {mutations: mutations.length, configurations: configurations.length,
    compiledAndSemanticallyKilled: receipt.mutations.length * configurations.length};
  save();
  const counts = receipt.configurations.map(c => `- ${c.name}: ${c.packets} logical packets, ${c.checks} checks, ${c.failures} failures.`).join('\n');
  fs.writeFileSync(path.join(out, 'REPORT.md'), `# Neptune logical NCInput software proof\n\n` +
    `Status: PASSED, UNCONSUMED. No device/hidapi/API/OEM/Lizard operation or production reader enabled.\n\n` +
    `Fixed official source: Valkirie/HandheldCompanion ${receipt.commit}, all 40 files reverified by size, SHA256 and Git blob. ` +
    `Actual shipped DLLs were rehashed as bytes, never executed or referenced. Exact source/static recovery is frozen in primary-source, shipped-ilspy and recovery; source-bindings.json records every input hash and official URL/blob.\n\n` +
    `Independent C# primary and shipped-ILSpy builds each ran Release and Checked. Exact NCInput Marshal.SizeOf=60 and all 35 Marshal.OffsetOf field layouts agree, as do all 28 canonical button enums and 22 raw signed axes for ${logical.length} packets. ` +
    `rpad_x offset is 20/0x14; historical 0x13 comment is stale. No unsafe memory alias is used to fill oracle fields. Every compiled C# input, project, argument list and output hash is retained.\n\n` +
    `${counts}\n- ${mutations.length} finite semantic mutants compiled and killed in both Release and Checked (${mutations.length * 2} runs); no compile error or crash counts as a kill.\n\n` +
    `Hand-authored raw pins, all 64 wire bits including reserved bits, each signed wire word, seeded random logical packets, already-logical lengths 60, 61, 62, 63, 64, 65, 66, 80 and 129 with opaque tails were compared against both oracles. ` +
    `Short actual counts 0..59 were tested with exact buffers and with a poisoned larger backing buffer. All 256 values in each of the first three header positions were exercised, including other-input/connect/battery. Zero motion with valid button/header input is accepted.\n\n` +
    `Capacity is not actualBytes. HC tries 65 then 64 capacities; HidDevice.Read returns hid_read_timeout count, ReadLoop copies only count into reused event storage without publishing count. This establishes a potential stale-tail hazard, not a physical occurrence. ` +
    `60 is the exact struct size and minimum. Any actualBytes >= 60 within capacity passes the length guard; no per-packet size whitelist exists upstream (see logical-gates.json). All suffix bytes are ignored. Known capacities do not prove native returned packet lengths. BufferKind defaults to Unverified/rejected, and no report-ID prefix inference is made.\n\n` +
    `Physical input, actual native hidapi.dll stripping, Windows RawInput/report framing, firmware report lengths, actual binding, reader lifecycle and complete input consumer acceptance remain OPEN. No dps/g conversion, calibration, transport, main, OEM query, existing test or capability flag changes.\n`);
  console.log(JSON.stringify({status: receipt.status, evidence: out, oracle: receipt.oracleAgreement,
    configurations: receipt.configurations.map(({name, packets, checks, failures}) => ({name, packets, checks, failures})),
    mutationCoverage: receipt.mutationCoverage}, null, 2));
} catch (error) {
  receipt.status = 'failed'; receipt.error = error.stack || String(error); save();
  console.error(receipt.error); console.error(`Evidence: ${out}`); process.exitCode = 1;
}
