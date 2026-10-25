// Safety: the only spawned executables are builds and scratch-compiled test
// consumers. Every consumer that can execute is pinned to a fixture terminal.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { runUiLifecycle } from './ui-lifecycle.mjs';
import {completeLightingSourceInputs,lightingArtifactInputs,resolveLightingRuntimeInputs,describeLightingPayload,verifyLightingToolInputs} from './input-closure.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../../..');
const product = 'FanLab/LightSetter/';
const sourceFiles = ['tools/HcLightingCapabilitySelfTest/Program.cs','tools/HcLightingCapabilitySelfTest/HcLightingCapabilitySelfTest.csproj',"capabilities/HcDeviceCapabilityCatalog.cs","capabilities/HcDeviceCapabilityCatalog.generated.cs","capabilities/HcLightingCapabilityCatalog.cs","capabilities/HcLightingCapabilityCatalog.generated.cs","capabilities/hc-device-catalog.json","capabilities/hc-lighting-policy.json","capabilities/hc-device-catalog.generated.h",'tools/hc-application-dependency-trim/Program.cs', 'tools/hc-application-dependency-trim/ComGateSelfTest.cs', 'tools/hc-application-dependency-trim/ApplicationTrim.csproj', product + 'Program.cs', product + 'DeviceCoreConsumer.cs', product + 'OneXQueuedLighting.cs', product + 'OneXLightingSession.cs',
  product + 'LightingAssemblyPins.cs', product + 'YeManLightSetter.csproj', 'FanLab/DeviceCoreSharedDependencies.cs', 'FanLab/RuntimeDependencyContract.cs',
  product + 'DeviceCore/LightingDeviceCore.cs', product + 'DeviceCore/PinnedOneXHidV1.g.cs',
  product + 'DeviceCore/WindowsHidTransport.cs', 'native/lighting_device_core_binding.h',
  'native/oem_lighting_operations.h', 'native/ProcessEfficiency.cs',
  'tools/fixtures/lightsetter_devicecore/ReplayHidTransport.cs', 'tools/LightSetterSelfTest/Program.cs', 'tools/LightSetterSelfTest/ArtifactProbe.cs', 'tools/LightSetterSelfTest/LightSetterSelfTest.csproj', 'tools/LightSetterSelfTest/FakeOneXDevice.cs', 'tools/LightSetterSelfTest/FakeAyaneoDevice.cs', 'deps/handheldcompanion-runtime/source/Devices/AYANEO/AYANEODeviceCEc.cs', 'deps/handheldcompanion-runtime/source/Devices/AYANEO/AYANEODeviceCEii.cs', 'deps/handheldcompanion-runtime/source/Devices/AYANEO/AYANEO2.cs', 'deps/handheldcompanion-runtime/source/Devices/AYANEO/AYANEOAIRPlus.cs', 'deps/handheldcompanion-runtime/source/Devices/AYANEO/AYANEOAIRPlusAMD.cs', 'deps/handheldcompanion-runtime/source/Misc/SerialQueueTasks.cs',
  'native/main.cpp', 'src/components/ControllerFeedbackCapabilitiesCard.vue', 'src/components/Dropdown.vue', 'src/components/Slider.vue', 'src/components/Toggle.vue', 'src/components/InlineIcon.vue', 'src/styles/tokens.css', 'src/bridge/api.ts', 'tools/oem_lighting_command_selftest.mjs', 'tools/controller_lighting_operations_browser_selftest.mjs', 'tools/controller_lighting_lifecycle_browser_selftest.mjs', 'tools/fixtures/lightsetter_devicecore/verify_native_binding.py', 'tools/fixtures/lightsetter_devicecore/native_binding_test.cpp', 'tools/fixtures/lightsetter_devicecore/AssemblyReferenceAudit.cs', 'tools/fixtures/lightsetter_devicecore/golden-v1-reports.json'];
const ownFiles = ['index.mjs', 'Harness.cs', 'LightingSignals.csproj', 'ReceiptFixture.cs', 'ReceiptFixture.csproj', 'LegacyRegression.csproj', 'native-probe.py', 'dependency-map.py', 'ui-feedback.mjs', 'ui-lifecycle.mjs', 'adapter.mjs'];
export const schema = 'ymcc.lighting-signals.v1';
export const contract = Object.freeze({ schema, apiVersion: 1, assemblyName: 'YeManLightDeviceCore',
  methods: ['bool Supports(string,string,string)', 'string Execute(string,string)'],
  supported: { deviceClass: 'HandheldCompanion.Devices.OneXPlayerX1Mini', color: ['solid', 'rainbow'], brightness: true },
  receiptFields: ['deviceClass', 'action', 'note', 'ready', 'opened', 'released', 'writeAttempts', 'physicalEffectVerified'],
  hcVersionPolicy: 'candidate input; never inferred from directory name or accepted as binary authorization',
  physicalEffectVerified: false });
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const legacyLightingSourceInputs = [...sourceFiles, ...ownFiles.map(f => path.relative(repo, path.join(here, f)).replaceAll('\\', '/'))];
export const getLightingSourceInputs = (repoRoot=repo) => completeLightingSourceInputs(repoRoot,legacyLightingSourceInputs);
export const lightingSourceInputs = getLightingSourceInputs();
export const getLightingArtifactInputs = (repoRoot=repo) => lightingArtifactInputs(repoRoot);
const snapshot = () => Object.fromEntries(lightingSourceInputs.map(f => [f, hash(path.join(repo, f))]));
function command(exe, args, log, env = {}) {
  const result = spawnSync(exe, args, { cwd: repo, encoding: 'utf8', timeout: 180000, maxBuffer: 4 * 1024 * 1024,
    windowsHide: true, env: { ...process.env, ...env } });
  fs.writeFileSync(log, `${result.stdout ?? ''}\n${result.stderr ?? ''}\n${result.error?.message ?? ''}`);
  return result;
}
function dll(root, name) {
  const files = [];
  function visit(dir) { for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.isDirectory() && entry.name !== 'ref' && entry.name !== 'refint') visit(path.join(dir, entry.name));
    else if (entry.name === name) files.push(path.join(dir, entry.name));
  } }
  visit(path.join(root, 'bin'));
  if (files.length !== 1) throw new Error(`AMBIGUOUS_BUILD_OUTPUT:${name}:${files.length}`);
  return files[0];
}
export async function runLightingSignals(options = {}) {
  if (!options.evidenceDir && !options.outDir) throw new Error('evidenceDir is required');
  const out = path.resolve(options.evidenceDir ?? options.outDir);
  fs.mkdirSync(out, { recursive: true });
  if (fs.existsSync(path.join(out, 'result.json'))) throw new Error('Evidence directory already contains a result; use a fresh directory');
  const hashesBefore = snapshot();
  const inputToolsBefore = verifyLightingToolInputs(repo);
  const dotnet = options.dotnet ?? inputToolsBefore.executables.dotnet;
  if(path.resolve(dotnet).toLowerCase()!==path.resolve(inputToolsBefore.executables.dotnet).toLowerCase())throw Error('LIGHTING_DOTNET_TOOL_NOT_DECLARED');
  if(path.resolve(process.execPath).toLowerCase()!==path.resolve(inputToolsBefore.executables.node).toLowerCase())throw Error('LIGHTING_NODE_TOOL_NOT_DECLARED');
  const builds = [];
  function build(project, area, props = []) {
    const artifacts = path.join(out, 'build', area);
    const args = ['build', project, '-c', 'Release', '--artifacts-path', artifacts, '--nologo', '-v:q',
      '-m:1', '-p:UseSharedCompilation=false', '-nodeReuse:false', ...props];
    const result = command(dotnet, args, path.join(out, `${area}-build.log`));
    builds.push({ area, command: [dotnet, ...args], exitCode: result.status, hardwareIo: false });
    if (result.error || result.status !== 0) throw new Error(`BUILD_FAILED:${area}:see ${area}-build.log`);
    return artifacts;
  }
  const fixtureBuild = build(path.join(here, 'ReceiptFixture.csproj'), 'receipt-fixture');
  const fixture = dll(fixtureBuild, 'YeManLightDeviceCore.dll');
  function pin(file, filename) {
    const pin = path.join(out, filename);
    fs.writeFileSync(pin, `// Scratch fixture authorization only. Never ship this file.\ninternal static class LightingAssemblyPins { internal const string StandaloneLightingSha256 = "${hash(file)}"; }\n`);
    return pin;
  }
  const receiptPin = pin(fixture, 'ReceiptFixturePins.cs');
  const harnessBuild = build(path.join(here, 'LightingSignals.csproj'), 'product-regressions', [`-p:FixturePinSource=${receiptPin}`]);
  const harness = dll(harnessBuild, 'LightingSignalsHarness.dll');
  const test = command(dotnet, [harness, fixture], path.join(out, 'product-regressions.log'));
  if (test.error) throw test.error;
  const regression = JSON.parse(test.stdout.trim());
  fs.writeFileSync(path.join(out, 'product-regressions.json'), JSON.stringify(regression, null, 2));
  const preload = command(dotnet, [harness, fixture, '--preload'], path.join(out, 'preloaded-image.log'));
  const preloadCase = JSON.parse(preload.stdout.trim());
  regression.cases.push(preloadCase);
  let beforeComparison = { status: 'NOT_EXECUTED', reason: 'beforeDirectory not supplied; current-source regression cases remain mandatory' };
  if (options.beforeDirectory) {
    const before = path.resolve(options.beforeDirectory);
    const beforeBuild = build(path.join(here, 'LightingSignals.csproj'), 'before-regressions', [
      `-p:FixturePinSource=${receiptPin}`, `-p:LightingProgramSource=${path.join(before, 'Program.cs')}`,
      `-p:LightingConsumerSource=${path.join(before, 'DeviceCoreConsumer.cs')}`]);
    const old = command(dotnet, [dll(beforeBuild, 'LightingSignalsHarness.dll'), fixture], path.join(out, 'before-regressions.log'));
    if (old.error) throw old.error;
    const previous = JSON.parse(old.stdout.trim());
    fs.writeFileSync(path.join(out, 'before-regressions.json'), JSON.stringify(previous, null, 2));
    const fixed = previous.cases.filter(c => c.status === 'FAIL' && regression.cases.some(n => n.id === c.id && n.status === 'PASS')).map(c => c.id);
    beforeComparison = { status: fixed.length >= 3 ? 'PASS' : 'FAIL', fixed, previousFailures: previous.failures,
      previousSourceHashes: Object.fromEntries(['Program.cs', 'DeviceCoreConsumer.cs'].map(f => [f, hash(path.join(before, f))])) };
  }
  const replayBuild = build(path.join(repo, 'tools/fixtures/lightsetter_devicecore/ReplayCore.csproj'), 'hid-replay-core');
  const replayCore = dll(replayBuild, 'YeManLightDeviceCore.dll');
  const replayPin = pin(replayCore, 'HidReplayPins.cs');
  const consumerBuild = build(path.join(here, 'LightingSignals.csproj'), 'hid-replay-consumer', [
    `-p:FixturePinSource=${replayPin}`, '-p:StartupObject=Program']);
  const consumer = dll(consumerBuild, 'LightingSignalsHarness.dll');
  const golden = readJson(path.join(repo, 'tools/fixtures/lightsetter_devicecore/golden-v1-reports.json'));
  const reports = golden.reports ?? golden;
  const scenarios = [
    { id: 'hid-solid-color', action: 'color', extra: ['--r', '10', '--g', '20', '--b', '30'], reports: [reports['solid-10-20-30']] },
    { id: 'hid-brightness', action: 'brightness', extra: ['--brightness', '50'], reports: [reports['brightness-50']] },
    { id: 'hid-color-and-brightness', action: 'color', extra: ['--r', '10', '--g', '20', '--b', '30', '--brightness', '50'], reports: [reports['solid-10-20-30'], reports['brightness-50']] },
    { id: 'hid-false-first-write', action: 'color', mode: 'false:1', extra: ['--r', '10', '--g', '20', '--b', '30'], note: 'setLedColor-false', reports: [reports['solid-10-20-30']] },
    { id: 'hid-throw-first-write', action: 'color', mode: 'throw:1', extra: [], error: true, writes: 1 },
    { id: 'hid-disconnected', action: 'color', mode: 'disconnected', extra: [], error: true, writes: 0 },
    { id: 'hid-release-failure', action: 'brightness', mode: 'release-throw', extra: ['--brightness', '50'], error: true, writes: 1 },
    { id: 'hid-unsupported-machine', action: 'color', model: 'HandheldCompanion.Devices.Unknown', extra: [], error: true, opens: 0, writes: 0 },
  ];
  const replayCases = [];
  for (const scenario of scenarios) {
    const terminalLog = path.join(out, `${scenario.id}-terminal.jsonl`);
    const args = [consumer, '--device-core-assembly', replayCore, '--expected-device-class', scenario.model ?? contract.supported.deviceClass,
      '--action', scenario.action, ...scenario.extra];
    const result = command(dotnet, args, path.join(out, `${scenario.id}.log`), { YMCC_LIGHT_REPLAY_MODE: scenario.mode ?? 'normal', YMCC_LIGHT_REPLAY_LOG: terminalLog });
    const terminal = fs.existsSync(terminalLog) ? fs.readFileSync(terminalLog, 'utf8').trim().split(/\r?\n/).filter(Boolean).map(JSON.parse) : [];
    const json = JSON.parse(result.stdout.trim());
    const writes = terminal.filter(e => e.eventName === 'write').map(e => e.hex);
    const assemblies = [...new Set(terminal.flatMap(e => e.loaded ?? []))];
    const issues = [];
    const expectedSuccess = !scenario.error && !scenario.note;
    if (result.status !== (expectedSuccess ? 0 : 1) || json.ok !== expectedSuccess) issues.push('exit/envelope mismatch');
    if (!scenario.error && json.result?.note !== (scenario.note ?? 'ok')) issues.push('operation note mismatch');
    if (scenario.reports && JSON.stringify(writes) !== JSON.stringify(scenario.reports)) issues.push('golden report mismatch');
    if (scenario.writes !== undefined && writes.length !== scenario.writes) issues.push('write count mismatch');
    if (scenario.opens !== undefined && terminal.filter(e => e.eventName === 'open').length !== scenario.opens) issues.push('unsupported capability opened terminal');
    if (terminal.some(e => e.eventName === 'open') && !terminal.some(e => e.eventName === 'release')) issues.push('terminal not released');
    if (assemblies.some(n => ['HandheldCompanion', 'Shared', 'HandheldCompanion.Shared'].includes(n))) issues.push('unexpected HC assembly loaded');
    replayCases.push({ id: scenario.id, status: issues.length ? 'FAIL' : 'PASS', input: { action: scenario.action, args: scenario.extra, terminalMode: scenario.mode ?? 'normal', model: scenario.model ?? contract.supported.deviceClass },
      consumer: 'Program.Main -> DeviceCoreConsumer.TryExecute -> LightingDeviceCore.Execute -> pinned protocol -> ReplayHidTransport',
      state: { writes, terminal, hardwareIo: false }, feedback: { exitCode: result.status, json }, issues });
  }
  const legacyBuild = build(path.join(here, 'LegacyRegression.csproj'), 'existing-legacy-regressions');
  const legacy = command(dotnet, [dll(legacyBuild, 'LightingSignalsLegacyRegression.dll')], path.join(out, 'existing-legacy-regressions.log'));
  const legacyResult = JSON.parse(legacy.stdout.trim());
  const pythonBundled = path.join(os.homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const python = options.python ?? (fs.existsSync(pythonBundled) ? pythonBundled : 'python');
  if(path.resolve(python).toLowerCase()!==path.resolve(inputToolsBefore.executables.python).toLowerCase())throw Error('LIGHTING_PYTHON_TOOL_NOT_DECLARED');
  const nativeOut = path.join(out, 'native');
  const nativeRun = command(python, [path.join(here, 'native-probe.py'), '--evidence-dir', nativeOut, '--node', process.execPath], path.join(out, 'native-probe.log'));
  if (nativeRun.status !== 0) throw new Error('NATIVE_PROBE_FAILED:see native-probe.log');
  const nativeResult = readJson(path.join(nativeOut, 'result.json'));
  const uiOut = path.join(out, 'ui');
  const uiRun = command(process.execPath, [path.join(here, 'ui-feedback.mjs'), uiOut], path.join(out, 'ui-probe.log'));
  if (uiRun.status !== 0) throw new Error('UI_PROBE_FAILED:see ui-probe.log');
  const uiResult = readJson(path.join(uiOut, 'browser-results.json'));
  const uiFeedback = readJson(path.join(uiOut, 'feedback-result.json'));
  const uiLifecycle = runUiLifecycle(path.join(out, 'ui-lifecycle'));
  if (uiLifecycle.status !== 'PASS') throw new Error('UI_LIFECYCLE_PROOF_FAILED');
  const dependencyMapPath = path.join(out, 'dependency-map.json');
  const dependencyMapRun = command(python, [path.join(here, 'dependency-map.py'), '--output', dependencyMapPath], path.join(out, 'dependency-map.log'));
  if (dependencyMapRun.status !== 0) throw new Error('DEPENDENCY_MAP_FAILED');
  const dependencyMap = readJson(dependencyMapPath);
  const shippingBuild = build(path.join(repo, 'FanLab/LightSetter/YeManLightSetter.csproj'), 'shipping-setter');
  const shippingSetter = dll(shippingBuild, 'YeManLightSetter.dll');
  const artifactBuild = build(path.join(repo, 'tools/LightSetterSelfTest/LightSetterSelfTest.csproj'), 'shipping-artifact-probe', ['-p:StartupObject=LightSetterArtifactProbe']);
  const artifactProbe = dll(artifactBuild, 'LightSetterSelfTest.dll');
  function checkArtifact(file, expected, label) {
    const receipt = path.join(out, `${label}-behavior.json`);
    const ran = command(dotnet, [artifactProbe, file, expected, receipt], path.join(out, `${label}-behavior.log`));
    const result = fs.existsSync(receipt) ? readJson(receipt) : null;
    if (ran.status !== 0 || !result?.passed || result.checksPassed < 282 || result.actualSha256 !== expected || result.mainEntryInvoked !== false ||
        result.nativeAttempts.length || result.dependencyAttempts.length) throw new Error(`SHIPPING_ARTIFACT_BEHAVIOR_FAILED:${label}`);
    return result;
  }
  const shippingArtifactBehavior = checkArtifact(shippingSetter, hash(shippingSetter), 'shipping-helper');
  const catalogBuild = build(path.join(repo, 'tools/HcLightingCapabilitySelfTest/HcLightingCapabilitySelfTest.csproj'), 'actual-artifact-catalog');
  const catalogProbe = dll(catalogBuild, 'HcLightingCapabilitySelfTest.dll');
  function checkArtifactCatalog(file, expected, label) {
    const receipt = path.join(out, `${label}-catalog.json`);
    const ran = command(dotnet, [catalogProbe, path.join(repo, 'capabilities/hc-device-catalog.json'), receipt, file, expected], path.join(out, `${label}-catalog.log`));
    const actual = fs.existsSync(receipt) ? readJson(receipt) : null;
    if (ran.status !== 0 || actual?.status !== 'PASS' || actual.checks !== 549 || actual.models !== 47 || actual.assemblySha256 !== expected || actual.artifactMode !== true ||
        actual.productionEntryPointInvoked !== false || actual.HCAssemblyLoaded !== false || actual.hardwareIo !== false || actual.nativeAttempts.length || actual.dependencyAttempts.length ||
        actual.cases.filter(c => c.ok && c.actualTargetCalls > 0).length !== 548 || actual.targetCalls.length !== 552) throw new Error(`ACTUAL_CATALOG_BEHAVIOR_FAILED:${label}`);
    return actual;
  }
  const shippingCatalogBehavior = checkArtifactCatalog(shippingSetter, hash(shippingSetter), 'shipping-helper');
  const productionCoreBuild = build(path.join(repo, product + 'DeviceCore/YeManLightDeviceCore.csproj'), 'shipping-core', ['-p:Platform=AnyCPU']);
  const productionCore = dll(productionCoreBuild, 'YeManLightDeviceCore.dll');
  const productionPin = fs.readFileSync(path.join(repo, product + 'LightingAssemblyPins.cs'), 'utf8').match(/StandaloneLightingSha256\s*=\s*"([a-f0-9]{64})"/)[1];
  const corePinMatches = hash(productionCore) === productionPin;
  const auditBuild = build(path.join(repo, 'tools/fixtures/lightsetter_devicecore/AssemblyReferenceAudit.csproj'), 'dependency-metadata');
  const auditor = dll(auditBuild, 'AssemblyReferenceAudit.dll');
  const runtimeSelection = resolveLightingRuntimeInputs(repo,options);
  const {profilePath,runtimeRoot,sharedDll} = runtimeSelection;
  const dependencyMetadata = {};
  for (const [label, file] of [['independentCore', productionCore], ['shippingHelper', shippingSetter], ['hcCore', path.join(runtimeRoot, 'HandheldCompanion.dll')], ['hcShared', sharedDll]]) {
    if (!fs.existsSync(file)) { dependencyMetadata[label] = { status: 'UNKNOWN', path: file, reason: 'file unavailable; no runtime inference' }; continue; }
    const jsonFile = path.join(out, `metadata-${label}.json`);
    const audited = command(dotnet, [auditor, file, jsonFile], path.join(out, `metadata-${label}.log`));
    // The auditor's nonzero code means forbidden HC/WPF references for an
    // independent component, which are expected for the legacy HC assembly.
    if (!fs.existsSync(jsonFile)) throw new Error(`METADATA_PROBE_FAILED:${label}`);
    const metadata = readJson(jsonFile);
    dependencyMetadata[label] = { ...metadata, scannerExitCode: audited.status, evidenceKind: 'static-pe-metadata', executed: false };
  }
  const modelAuditBuild = build(path.join(repo, 'tools/hc-application-dependency-trim/ApplicationTrim.csproj'), 'actual-msi-lifecycle-metadata');
  const modelAuditor = dll(modelAuditBuild, 'ApplicationTrim.dll');
  const modelDump = path.join(out, 'actual-msi-lifecycle.json');
  const modelPattern = 'ClawA1M::(Open|SetLedColor|SetLedBrightness|get_IsOpen)|Claw(A2VM|CG3EM).*::Open|IDevice::Open\\(' + '|OneXAOKZOE::(Open|Close|\\.ctor|SendV1Brightness|SendV1SolidColor|SendV1Rainbow)|OneXPlayerX1.*::(Open|Close|IsReady|SetLed|SetLED|WriteToSerialPort|\\.ctor)' + '|OneXPlayerOneXFly.*::(Open|Close|IsReady|SetLed|\\.ctor)|OneXAOKZOE::SendV2|OneXPlayerApex::(Open|SetLed|SetLED|\\.ctor)|OneXPlayerG1.*::(Open|Close|IsReady|SetLed|\\.ctor)|AOKZOEA(1(Pro|X)?|2)::(Open|Close|IsReady|SetLed|FindClosestColor|CalculateDistance|\\.ctor)|AYANEODeviceCE(c|ii)::(\\.ctor|Open|SetLedColor|SetLedBrightness|SetLedStatus|CEcRgb_GetBrightness)|IDevice::Initialize|DynamicLightingManager::UpdateLED';
  const modelRun = command(dotnet, [modelAuditor, 'dump', path.join(runtimeRoot, 'HandheldCompanion.dll'), modelPattern, modelDump], path.join(out, 'actual-msi-lifecycle.log'));
  if (modelRun.status !== 0 || !fs.existsSync(modelDump)) throw new Error('ACTUAL_MSI_LIFECYCLE_METADATA_MISSING');
  const modelRows = readJson(modelDump);
  const body = suffix => modelRows.find(row => row.FullName.includes(suffix))?.il ?? '';
  const parentOpen = body('HandheldCompanion.Devices.ClawA1M::Open()');
  const getter = body('HandheldCompanion.Devices.ClawA1M::get_IsOpen()');
  const dictionaryOne = text => /IDevice::hidDevices[^\n]*\nldc\.i4\.1(?:\s|$)/.test(text);
  const modelChecks = [
    { name:'actual MSI parent owns firmware and open state', passed:parentOpen.includes('ClawA1M::Firmware') && parentOpen.includes('ClawA1M::ClawOpen') && parentOpen.includes('HidDeviceAttributes::get_Version()') },
    { name:'actual MSI IsOpen requires both state flags', passed:getter.includes('IDevice::DeviceOpen') && getter.includes('ClawA1M::ClawOpen') },
    { name:'actual parent and both LED methods select bound HID key one', passed:dictionaryOne(parentOpen) && dictionaryOne(body('ClawA1M::SetLedColor(')) && dictionaryOne(body('ClawA1M::SetLedBrightness(')) },
    { name:'general MSI parent Open contains unrelated policy writes', passed:['set_long_limit','set_short_limit','SetShiftMode','SyncToROM','SwitchMode'].every(name=>parentOpen.includes('::'+name+'(')) },
    { name:'both specific child Open overrides also write power limits', passed:['ClawA2VM::Open()','ClawCG3EM::Open()'].every(name=>['set_long_limit','set_short_limit'].every(method=>body(name).includes('::'+method+'('))) },
    { name:'base Open contains managed open flag and gated OpenLib only', passed:body('HandheldCompanion.Devices.IDevice::Open()').includes('IDevice::DeviceOpen') && !['set_long_limit','set_short_limit','SetShiftMode','SyncToROM','SwitchMode'].some(name=>body('HandheldCompanion.Devices.IDevice::Open()').includes('::'+name+'(')) },
  ];
  const msiLightingContract = { status:modelChecks.every(row=>row.passed)?'PASS':'FAIL', passed:modelChecks.every(row=>row.passed), checks:modelChecks,
    hcSha256:hash(path.join(runtimeRoot,'HandheldCompanion.dll')), evidenceKind:'actual-PE-method-body-and-state-contract', targetCodeExecuted:false, hardwareIo:false };
  fs.writeFileSync(path.join(out,'actual-msi-contract.json'),JSON.stringify(msiLightingContract,null,2));
  if (!msiLightingContract.passed) throw new Error('ACTUAL_MSI_PARENT_OR_LED_CONTRACT_CHANGED');
  const x1 = body('HandheldCompanion.Devices.OneXPlayerX1::Open()');
  const oxp = body('HandheldCompanion.Devices.OneXAOKZOE::Open()');
  const ledRows = modelRows.filter(row => /OneXPlayerX1(Mini)?::(SetLed|SetLED|WriteToSerialPort)/.test(row.FullName));
  const queueBody = body('OneXPlayerX1::WriteToSerialPort(');
  const onexChecks = [
    { name:'actual OneX parent opts into OpenLib', passed:body('OneXAOKZOE::.ctor(').includes('IDevice::UseOpenLib') && body('HandheldCompanion.Devices.IDevice::Open()').includes('IDevice::OpenLibSys(') },
    { name:'actual OneX general Open changes all three OEM registers', passed:oxp.includes('IDevice::Open()') && oxp.includes('ldc.i4 241') && oxp.includes('ldc.i4 242') && x1.includes('OneXAOKZOE::Open()') && x1.includes('ldc.i4 235') && [oxp,x1].every(text=>text.includes('IDevice::EcWriteByte(')) },
    { name:'actual X1 serial construction consumes the audited settings', passed:['_serialPort','EnableSerialPort','SerialPortDeviceName','SerialPortBaudRate','SerialPortParity','SerialPortDataBits','SerialPortStopBits'].every(name=>x1.includes('OneXPlayerX1::'+name)) && x1.includes('DeviceUtils::GetSerialDevices()') && x1.includes('SerialPort::.ctor(') && x1.includes('SerialPort::Open()') },
    { name:'actual OneX LED methods have no general-open or EC dependency', passed:ledRows.length===7 && ledRows.every(row=>!/(IDevice::(get_IsOpen|DeviceOpen|UseOpenLib|EcWriteByte|EcReadByte|Open)|OneXAOKZOE::Open)/.test(row.il)) },
    { name:'actual OneX serial writes require an open port and use SerialQueue', passed:queueBody.includes('OneXPlayerX1::_serialPort') && queueBody.includes('SerialPort::get_IsOpen()') && queueBody.includes('SerialQueue::Enqueue(') },
    { name:'actual Mini disables serial and binds its connected HID', passed:body('OneXPlayerX1Mini::.ctor(').includes('OneXPlayerX1::EnableSerialPort') && body('OneXPlayerX1Mini::IsReady()').includes('OneXPlayerX1Mini::hidDevice') && body('OneXPlayerX1Mini::IsReady()').includes('HidDevice::get_IsConnected()') && !modelRows.some(row=>row.FullName.includes('OneXPlayerX1Mini::Open()')) },
    { name:'actual Mini HID helpers depend on connected HID and write acknowledgement', passed:['SendV1Brightness(','SendV1SolidColor(','SendV1Rainbow('].every(name=>{const text=body('OneXAOKZOE::'+name);return text.includes('HidDevice::get_IsConnected()') && text.includes('HidDevice::Write(') && !/OpenLib|EcWriteByte|get_IsOpen/.test(text);}) },
    { name:'actual general OneX Close also changes OEM registers', passed:body('OneXPlayerX1::Close()').includes('ldc.i4 235') && body('OneXPlayerX1::Close()').includes('SerialPort::Close()') && body('OneXAOKZOE::Close()').includes('ldc.i4 241') && body('OneXAOKZOE::Close()').includes('ldc.i4 242') },
  ];
  const onexLightingContract = {status:onexChecks.every(row=>row.passed)?'PASS':'FAIL',passed:onexChecks.every(row=>row.passed),checks:onexChecks,
    hcSha256:hash(path.join(runtimeRoot,'HandheldCompanion.dll')),evidenceKind:'actual-PE-transport-and-LED-dependency-contract',targetCodeExecuted:false,hardwareIo:false};
  fs.writeFileSync(path.join(out,'actual-onex-contract.json'),JSON.stringify(onexLightingContract,null,2));
  if(!onexLightingContract.passed) throw new Error('ACTUAL_ONEX_TRANSPORT_OR_LED_CONTRACT_CHANGED');
  const brightnessBody=body('OneXPlayerX1::SetLedBrightness('),statusBody=body('OneXPlayerX1::SetLedStatus('),constructor=body('OneXPlayerX1::.ctor(');
  const cacheChecks=[
    {name:'actual OneX constructor does not read or initialize brightness/status from device',passed:!constructor.includes('OneXPlayerX1::LEDBrightness')&&!constructor.includes('OneXPlayerX1::LEDEnabled')},
    {name:'actual brightness method skips equal cached input before serial write',passed:/ldfld System.Int32 .*OneXPlayerX1::LEDBrightness[^\n]*\nldarg\.1\s*\nbeq/.test(brightnessBody)&&brightnessBody.includes('OneXPlayerX1::WriteToSerialPort(')&&brightnessBody.includes('IDevice::SetLedStatus(')},
    {name:'actual status method skips equal default boolean before both serial writes',passed:/ldfld System.Boolean .*OneXPlayerX1::LEDEnabled[^\n]*\nldarg\.1\s*\nbeq/.test(statusBody)&&(statusBody.match(/OneXPlayerX1::WriteToSerialPort\(/g)||[]).length===2},
  ];
  const onexRequestCacheContract={status:cacheChecks.every(x=>x.passed)?'PASS':'FAIL',passed:cacheChecks.every(x=>x.passed),checks:cacheChecks,hcSha256:hash(path.join(runtimeRoot,'HandheldCompanion.dll')),targetCodeExecuted:false,hardwareIo:false};
  fs.writeFileSync(path.join(out,'actual-onex-cache-contract.json'),JSON.stringify(onexRequestCacheContract,null,2));
  if(!onexRequestCacheContract.passed)throw new Error('ACTUAL_ONEX_REQUEST_CACHE_CONTRACT_CHANGED');
  const flyColor=body('OneXPlayerOneXFly::SetLedColor('),flyBrightness=body('OneXPlayerOneXFly::SetLedBrightness('),flyReady=body('OneXPlayerOneXFly::IsReady()');
  const flyChecks=[
    {name:'actual Fly and F1Pro inherit the audited OneXAOKZOE lifecycle',passed:body('OneXPlayerOneXFly::.ctor(').includes('OneXAOKZOE::.ctor(')&&body('OneXPlayerOneXFlyF1Pro::.ctor(').includes('OneXPlayerOneXFly::.ctor(')},
    {name:'actual inherited OneX Open initializes base and writes both OEM registers',passed:oxp.includes('IDevice::Open()')&&oxp.includes('ldc.i4 241')&&oxp.includes('ldc.i4 242')&&(oxp.match(/IDevice::EcWriteByte\(/g)||[]).length===2},
    {name:'actual Fly readiness binds connected HID before LED calls',passed:flyReady.includes('OneXPlayerOneXFly::hidDevice')&&flyReady.includes('HidDevice::get_IsConnected()')&&flyReady.includes('stfld HidLibrary.HidDevice')},
    {name:'actual Fly color and brightness consume only bound HID v2 helpers',passed:flyColor.includes('OneXPlayerOneXFly::hidDevice')&&flyColor.includes('OneXAOKZOE::SendV2SolidColor(')&&flyColor.includes('OneXAOKZOE::SendV2Rainbow(')&&flyBrightness.includes('OneXPlayerOneXFly::hidDevice')&&flyBrightness.includes('OneXAOKZOE::SendV2Brightness(')&&![flyColor,flyBrightness].some(text=>/UseOpenLib|DeviceOpen|get_IsOpen|EcWriteByte|EcReadByte/.test(text))},
    {name:'actual Fly family has no own general Open or Close override',passed:!modelRows.some(row=>/OneXPlayerOneXFly(F1Pro)?::(Open|Close)\(/.test(row.FullName))},
    {name:'actual V2 helpers preserve connected-HID write acknowledgement',passed:['SendV2Brightness(','SendV2SolidColor(','SendV2Rainbow('].every(name=>{const text=body('OneXAOKZOE::'+name);return text.includes('HidDevice::get_IsConnected()')&&text.includes('HidDevice::Write(')&&!/UseOpenLib|DeviceOpen|get_IsOpen/.test(text);})},
    ...['OneXPlayerG1','AOKZOEA1'].flatMap(family=>{
      const ready=body(family+'::IsReady()'),color=body(family+'::SetLedColor('),brightness=body(family+'::SetLedBrightness(');
      const version=family==='OneXPlayerG1'?'V1':'V2';
      return [
        {name:family+' actual constructor inherits audited OneX parent',passed:body(family+'::.ctor(').includes('OneXAOKZOE::.ctor(')},
        {name:family+' actual readiness binds connected HID',passed:ready.includes(family+'::hidDevice')&&ready.includes('HidDevice::get_IsConnected()')&&ready.includes('stfld HidLibrary.HidDevice')},
        {name:family+' actual LED methods use bound HID and original '+version+' helpers',passed:color.includes(family+'::hidDevice')&&color.includes('OneXAOKZOE::Send'+version+'SolidColor(')&&color.includes('OneXAOKZOE::Send'+version+'Rainbow(')&&brightness.includes(family+'::hidDevice')&&brightness.includes('OneXAOKZOE::Send'+version+'Brightness(')},
        {name:family+' LED methods do not require general Open or EC',passed:[color,brightness].every(text=>text.length>0&&!/UseOpenLib|DeviceOpen|get_IsOpen|EcWriteByte|EcReadByte|::Open\(/.test(text))},
      ];
    }),
    {name:'all seven G1/AOKZOE identities retain inherited lighting and no general lifecycle override',passed:
      ['OneXPlayerG1','OneXPlayerG1AMD','OneXPlayerG1Intel','AOKZOEA1','AOKZOEA1Pro','AOKZOEA1X','AOKZOEA2'].every(name=>
        body(name+'::.ctor(').length>0&&!modelRows.some(row=>new RegExp('\\.'+name+'::(Open|Close)\\(').test(row.FullName)))&&
      [['OneXPlayerG1AMD','OneXPlayerG1'],['OneXPlayerG1Intel','OneXPlayerG1'],['AOKZOEA1Pro','AOKZOEA1'],['AOKZOEA1X','AOKZOEA1Pro'],['AOKZOEA2','AOKZOEA1Pro']].every(([name,parent])=>
        body(name+'::.ctor(').includes(parent+'::.ctor(')&&!modelRows.some(row=>new RegExp('\\.'+name+'::SetLed').test(row.FullName)))},
    {name:'actual V1 helpers retain connected-HID write acknowledgement without driver state',passed:['SendV1Brightness(','SendV1SolidColor(','SendV1Rainbow('].every(name=>{
      const text=body('OneXAOKZOE::'+name);return text.includes('HidDevice::get_IsConnected()')&&text.includes('HidDevice::Write(')&&!/UseOpenLib|DeviceOpen|get_IsOpen|EcWriteByte|EcReadByte/.test(text);})},
    {name:'actual A1 nearest-color helpers have no lifecycle or transport dependency',passed:['FindClosestColor(','CalculateDistance('].every(name=>{
      const text=body('AOKZOEA1::'+name);return text.length>0&&!/UseOpenLib|DeviceOpen|get_IsOpen|EcWriteByte|EcReadByte|HidDevice::|::Open\(/.test(text);})},
    {name:'actual nested Apex inherits X1AMD with no own lighting or Open override',passed:body('HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex::.ctor(').includes('OneXPlayerX1AMD::.ctor(')&&!modelRows.some(row=>/OneXPlayerApex::(Open|SetLed|SetLED)\(/.test(row.FullName))},
    {name:'actual X1 preset list preserves named high-byte preset used by Apex',passed:constructor.includes('LEDPreset_OneXPlayerX1_Preset09')&&constructor.includes('ldc.i4 483')},
  ];
  const boundHidLightingContract={status:flyChecks.every(x=>x.passed)?'PASS':'FAIL',passed:flyChecks.every(x=>x.passed),checks:flyChecks,hcSha256:hash(path.join(runtimeRoot,'HandheldCompanion.dll')),targetCodeExecuted:false,hardwareIo:false};
  fs.writeFileSync(path.join(out,'actual-bound-hid-contract.json'),JSON.stringify(boundHidLightingContract,null,2));
  if(!boundHidLightingContract.passed)throw new Error('ACTUAL_BOUND_HID_LIGHTING_CONTRACT_CHANGED');



  const ayaColor=body('AYANEODeviceCEc::SetLedColor('),ayaBrightness=body('AYANEODeviceCEc::SetLedBrightness('),ayaConstructor=body('AYANEODeviceCEc::.ctor(');
  const ayaneoChecks=[
    {name:'actual frozen CEc constructor leaves first-request brightness and colors uninitialized',passed:!['ledBrightness','ledColorSticksLeft','ledColorStickRight'].some(name=>ayaConstructor.includes('AYANEODeviceCEc::'+name))},
    {name:'actual CEc color requires nullable brightness before its RGB consumer',passed:ayaColor.includes('AYANEODeviceCEc::ledBrightness')&&ayaColor.includes('System.Nullable`1<System.Int32>::get_HasValue()')&&ayaColor.indexOf('AYANEODeviceCEc::ledBrightness')<ayaColor.indexOf('AYANEODeviceCEc::CEcRgb_SetColorAll(')},
    {name:'actual CEc brightness requires both saved colors before reapplying RGB',passed:['ledColorSticksLeft','ledColorStickRight','CEcRgb_SetColorAll('].every(name=>ayaBrightness.includes('AYANEODeviceCEc::'+name))&&ayaBrightness.includes('System.Nullable`1<System.Windows.Media.Color>::get_HasValue()')},
    {name:'actual CEii inherits CEc color and brightness methods',passed:body('AYANEODeviceCEii::.ctor(').includes('AYANEODeviceCEc::.ctor(')&&!modelRows.some(row=>/AYANEODeviceCEii::SetLed(Color|Brightness)\(/.test(row.FullName))},
    {name:'actual device Initialize supplies no lighting cache context',passed:!['ledBrightness','SetLedBrightness','SetLedColor'].some(name=>body('IDevice::Initialize(').includes(name))},
    {name:'actual general CEc Open holds RGB control without initializing color or brightness',passed:body('AYANEODeviceCEc::Open()').includes('CEcControl_RgbHoldControl(')&&!['SetLedBrightness','SetLedColor','ledBrightness'].some(name=>body('AYANEODeviceCEc::Open()').includes(name))},
    {name:'actual original lighting manager applies brightness before color',passed:body('DynamicLightingManager::UpdateLED(').indexOf('IDevice::SetLedBrightness(')>=0&&body('DynamicLightingManager::UpdateLED(').indexOf('IDevice::SetLedBrightness(')<body('DynamicLightingManager::UpdateLED(').indexOf('IDevice::SetLedColor(')},
  ];
  const ayaneoLightingContract={status:ayaneoChecks.every(x=>x.passed)?'PASS':'FAIL',passed:ayaneoChecks.every(x=>x.passed),checks:ayaneoChecks,hcSha256:hash(path.join(runtimeRoot,'HandheldCompanion.dll')),targetCodeExecuted:false,hardwareIo:false};
  fs.writeFileSync(path.join(out,'actual-ayaneo-context-contract.json'),JSON.stringify(ayaneoLightingContract,null,2));
  if(!ayaneoLightingContract.passed)throw new Error('ACTUAL_AYANEO_LIGHTING_CONTEXT_CONTRACT_CHANGED');

  const independentHasNoHc = dependencyMetadata.independentCore.scannerExitCode === 0;
  // The exact helper pure methods were executed above with memory devices; Main/factory/native are denied.
  const inputToolsAfter = verifyLightingToolInputs(repo);
  if(JSON.stringify(inputToolsAfter.packageSnapshot)!==JSON.stringify(inputToolsBefore.packageSnapshot))throw Error('LIGHTING_TOOL_MEMBERSHIP_CHANGED');
  const hashesAfter = snapshot();
  const changedDuringRun = Object.keys(hashesBefore).filter(f => hashesBefore[f] !== hashesAfter[f]);
  const differences = [];
  const pinSource = fs.readFileSync(path.join(repo, product + 'Program.cs'), 'utf8');
  const profileName = options.hcProfile ?? 'ymcc-device-core-v1';
  const hcPin = profileName === 'legacy' ? pinSource.match(/ExpectedHcSha256\s*=\s*"([a-f0-9]{64})"/)?.[1]
    : profileName === 'ymcc-device-core-v1' ? pinSource.match(/ExpectedYmccHcCoreSha256\s*=\s*"([a-f0-9]{64})"/)?.[1] : null;
  if (options.runtimeRoot) {
    if (!hcPin) differences.push({ code: 'HC_PROFILE_NOT_AUTHORIZED', profileName });
    if (!dependencyMetadata.hcCore.sha256) differences.push({ code: 'HC_ASSEMBLY_MISSING', runtimeRoot });
    else if (dependencyMetadata.hcCore.sha256 !== hcPin) differences.push({ code: 'HC_HASH_NOT_AUTHORIZED', expected: hcPin, actual: dependencyMetadata.hcCore.sha256 });
    if (options.hcVersion && dependencyMetadata.hcCore.assemblyVersion !== options.hcVersion)
      differences.push({ code: 'HC_VERSION_MISMATCH', expected: options.hcVersion, actual: dependencyMetadata.hcCore.assemblyVersion });
  }
  const sharedCandidate = [];
  if (options.sharedRuntimeRoot) {
    const resolver = fs.readFileSync(path.join(repo, 'FanLab/DeviceCoreSharedDependencies.cs'), 'utf8');
    for (const item of resolver.matchAll(/\("([^"\n]+)", "([A-F0-9]{64})"\)/g)) {
      const file = path.join(path.resolve(options.sharedRuntimeRoot), item[1] + '.dll');
      const actual = fs.existsSync(file) ? hash(file) : null;
      sharedCandidate.push({ name: item[1], path: file, expected: item[2].toLowerCase(), actual });
      if (actual !== item[2].toLowerCase()) differences.push({ code: actual ? 'SHARED_HASH_NOT_AUTHORIZED' : 'SHARED_ASSEMBLY_MISSING', assembly: item[1], actual });
    }
  }
  let payloadCandidate = null;
  if (options.hostPayloadRoot) {
    payloadCandidate = describeLightingPayload(options);
    const file = payloadCandidate.file;
    const expectedHelperSha256 = payloadCandidate.expectedReviewedSha256;
    // The helper build has no path-normalization contract; an isolated build's
    // hash changes with its PDB/output location. Require an explicit reviewed
    // candidate pin rather than manufacture a stale-payload failure from that.
    if (!expectedHelperSha256 || !/^[a-f0-9]{64}$/.test(expectedHelperSha256)) differences.push({ code: 'HELPER_PAYLOAD_PIN_MISSING_OR_INVALID' });
    if (!file || payloadCandidate.actual !== expectedHelperSha256) differences.push({ code: file ? 'HELPER_PAYLOAD_HASH_MISMATCH' : 'HELPER_PAYLOAD_MISSING', ...payloadCandidate });
    if (file && /^[a-f0-9]{64}$/.test(expectedHelperSha256 ?? '') && payloadCandidate.actual === expectedHelperSha256) {
      payloadCandidate.behavior = checkArtifact(file, expectedHelperSha256, 'candidate-helper');
      payloadCandidate.catalogBehavior = checkArtifactCatalog(file, expectedHelperSha256, 'candidate-helper');
    }
  }
  const uiCases = readJson(path.join(uiOut, 'ui-feedback-cases.json'));
  const cases = [...regression.cases, ...replayCases, ...uiCases, ...uiLifecycle.cases].map(c => ({ ...c, evidenceKind: 'source-derived-fixture' }));
  const passed = cases.every(c => c.status === 'PASS') && legacy.status === 0 && nativeResult.status === 'PASS' && uiResult.ok && uiLifecycle.status === 'PASS' && corePinMatches && independentHasNoHc && preload.status === 0 && differences.length === 0 && beforeComparison.status !== 'FAIL' && changedDuringRun.length === 0;
  const result = { schema, status: passed ? 'PASS' : 'FAIL', scope: 'offline product behavior and protocol regression; not physical acceptance',
    sourceChanges: [product + 'Program.cs', product + 'OneXLightingSession.cs', product + 'DeviceCoreConsumer.cs', 'src/components/ControllerFeedbackCapabilitiesCard.vue (parent integrated status rendering)'], sourceHashes: hashesAfter,
    executedProductModules: [product + 'Program.cs', product + 'DeviceCoreConsumer.cs', product + 'OneXQueuedLighting.cs', product + 'OneXLightingSession.cs', product + 'DeviceCore/LightingDeviceCore.cs', product + 'DeviceCore/PinnedOneXHidV1.g.cs', 'deps/handheldcompanion-runtime/source/Misc/SerialQueueTasks.cs', 'FanLab/RuntimeDependencyContract.cs', 'native/lighting_device_core_binding.h', 'native/oem_lighting_operations.h', 'native/main.cpp:extracted lighting command/admission/receipt functions', 'src/components/ControllerFeedbackCapabilitiesCard.vue', 'src/bridge/api.ts'],
    inputConsumerStateFeedback: cases, compatibility: { contract, candidate: { hcVersion: options.hcVersion ?? null,
      runtimeRoot: options.runtimeRoot ?? null, sharedRuntimeRoot: options.sharedRuntimeRoot ?? null, hostPayloadRoot: options.hostPayloadRoot ?? null },
      candidateRuntimeAcceptance: differences.length ? 'BLOCKED' : 'NOT_EXECUTED', differences, sharedCandidate, payloadCandidate, candidateReason: 'No HC runtime is loaded by this safe suite. Reuse runtime-connectivity compatibility gate for a supplied candidate.',
      compiledFixtures: { receiptSha256: hash(fixture), hidReplaySha256: hash(replayCore) }, productionHelper: { origin: 'current-source-build', path: shippingSetter, sha256: hash(shippingSetter), executed: true, mainEntryInvoked: false, artifactBehavior: shippingArtifactBehavior, catalogBehavior: shippingCatalogBehavior },
      abiCases: regression.cases.filter(c => c.id.startsWith('abi-')), changedDuringRun, corePinMatches, independentHasNoHc },
    msiLightingContract, onexLightingContract, onexRequestCacheContract, boundHidLightingContract, ayaneoLightingContract, dependencyMetadata, dependencyMap, uiFeedback, uiLifecycle, existingNativeRegression: nativeResult, existingUiRegression: uiResult, beforeComparison, existingLegacyRegression: legacyResult,
    builds, inputDeclaration: {runtimeSelection, inputToolsBefore, inputToolsAfter, productionCore: {origin:'current-source-build',path:productionCore,sha256:hash(productionCore)}}, hardwareIo: false, physicalEffectVerified: false,
    remainingGaps: [
      { id: 'physical-led', status: 'NOT_EXECUTED', detail: 'Actual descriptor selection, device write acknowledgement and visible lighting require authorized hardware evidence.' },
      { id: 'live-ui-native-process', status: 'NOT_EXECUTED', detail: 'Real Vue feedback, extracted native command/admission/receipt functions and helper/protocol execute separately with memory terminals. Native OS process timeout/kill, integrated live IPC and installed UI remain outside safe execution.' },
      { id: 'legacy-hc-runtime', status: 'NOT_EXECUTED', detail: 'No legacy HC factory/driver loading. HC payload/shared-runtime compatibility and selected factory require runtime-connectivity gate.' },
      { id: 'historic-sender', status: 'UNKNOWN', detail: 'Pinned source proves sender lineage; no archived pre-trim successful device receipt supplied.' },
      { id: 'restore', status: 'NO_CONTRACT', detail: 'One-shot lighting setter has no restoration lease/readback. No automatic restore is claimed.' },
    ] };
  fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(result, null, 2));
  return result;
}
function parse(args) {
  const options = {};
  const names = new Set(['expected-helper-sha256', 'python', 'evidence-dir', 'out-dir', 'before-directory', 'hc-version', 'runtime-root', 'shared-runtime-root', 'host-payload-root', 'hc-profile', 'dotnet']);
  for (let i = 0; i < args.length; i++) {
    const key = args[i].slice(2);
    if (!args[i].startsWith('--') || !names.has(key) || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`INVALID_ARGUMENT:${args[i]}`);
    options[key.replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = args[++i];
  }
  return options;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { const options = parse(process.argv.slice(2)); const result = await runLightingSignals(options);
    console.log(JSON.stringify({ schema, status: result.status, cases: result.inputConsumerStateFeedback.length,
      failed: result.inputConsumerStateFeedback.filter(c => c.status !== 'PASS').map(c => c.id), beforeComparison: result.beforeComparison,
      evidence: path.resolve(options.evidenceDir ?? options.outDir), hardwareIo: false })); process.exitCode = result.status === 'PASS' ? 0 : 1;
  } catch (error) { console.error(error.stack); process.exitCode = 1; }
}

