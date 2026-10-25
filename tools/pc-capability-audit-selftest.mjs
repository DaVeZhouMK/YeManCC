import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(repo, '../../../Build/Validation/PcCapabilityAudit-20261014', String(Date.now()));
fs.mkdirSync(out, { recursive: true });
const results = [];
const execute = (name, exe, args) => {
  const r = spawnSync(exe, args, { cwd: repo, encoding: 'utf8', windowsHide: true, timeout: 120000 });
  fs.writeFileSync(path.join(out, name + '.log'), (r.stdout || '') + (r.stderr || '') + (r.error || ''));
  if (r.error) throw r.error;
  return r;
};
const check = (name, fn) => { fn(); results.push(name); };
const build = execute('build', 'dotnet', ['build', 'tools/PcCapabilityAudit/PcCapabilityAudit.csproj', '--nologo', '-v:q']);
assert.equal(build.status, 0);
const dll = path.join(repo, 'tools/PcCapabilityAudit/bin/Debug/net10.0/PcCapabilityAudit.dll');
const fixture = path.join(out, 'fixture'); fs.mkdirSync(fixture);
const code = `using Alias = HandheldCompanion.Devices.Nested.Leaf;
namespace HandheldCompanion.Devices {
 public enum DeviceCapabilities { None=0, FanControl=1, DynamicLighting=2, BatteryChargeLimit=4, OEMGPU=8 }
 public enum LEDLevel { None=0, SolidColor=1, Breathing=2 }
 public class IDevice { public DeviceCapabilities Capabilities; public LEDLevel DynamicLightingCapabilities;
  public static IDevice GetCurrent() { return new Alias(); }
 }
 public class Parent : IDevice { public Parent() { Capabilities |= DeviceCapabilities.FanControl; DynamicLightingCapabilities = LEDLevel.SolidColor | LEDLevel.Breathing; } }
}
namespace HandheldCompanion.Devices.Nested {
 public class Leaf : HandheldCompanion.Devices.Parent {
  public Leaf() {
    // Capabilities |= DeviceCapabilities.OEMGPU;
    var text = "Capabilities |= DeviceCapabilities.OEMGPU;";
    Capabilities = DeviceCapabilities.DynamicLighting;
    if (FirmwareOk()) Capabilities |= DeviceCapabilities.BatteryChargeLimit;
    System.Action later = () => Capabilities |= DeviceCapabilities.OEMGPU;
    DynamicLightingCapabilities -= LEDLevel.Breathing;
    this.OEMChords.Clear();
    this.OEMChords.Add(new KeyboardChord("Fixture", [], [], false));
  }
  public bool FirmwareOk() => false;
  public void Open() { Capabilities |= DeviceCapabilities.OEMGPU; }
 }
}`;
fs.writeFileSync(path.join(fixture, 'Devices.cs'), code);
const extract = execute('fixture-inventory', 'dotnet', [dll, fixture, path.join(out, 'fixture.json')]);
assert.equal(extract.status, 0);
const data = JSON.parse(fs.readFileSync(path.join(out, 'fixture.json')));
const leaf = data.devices.find(d => d.name === 'Leaf');
check('constant false virtual gate evidence retained', () => assert.equal(leaf.methods.find(m => m.name === 'FirmwareOk').constantBooleanReturn, false));
check('namespace and using-alias identity', () => { assert.equal(leaf.fullName, 'HandheldCompanion.Devices.Nested.Leaf'); assert.equal(leaf.instantiable, true); });
check('inheritance reset removes parent flag', () => assert.deepEqual(leaf.declared.Capabilities.minimum, ['DynamicLighting']));
check('firmware condition stays conditional', () => assert.deepEqual(leaf.declared.Capabilities.possible, ['BatteryChargeLimit', 'DynamicLighting']));
check('callbacks and methods do not grant constructor capability', () => assert.equal(leaf.operations.filter(o => o.values.includes('OEMGPU') && !o.constructor).length, 2));
check('comments and strings never become operations', () => assert.equal(leaf.operations.filter(o => o.values.includes('OEMGPU')).length, 2));
check('explicit this OEM chords and clear retained', () => assert.equal(leaf.oemChords.length, 2));
check('multi-bit LED assignment and removal', () => assert.deepEqual(leaf.declared.DynamicLightingCapabilities.minimum, ['SolidColor']));
fs.writeFileSync(path.join(fixture, 'Devices.cs'), code.replace('Capabilities = DeviceCapabilities.DynamicLighting;', 'Capabilities = GetUnknownCapabilities();'));
const unknown = execute('unknown-expression', 'dotnet', [dll, fixture, path.join(out, 'unknown.json')]);
check('unknown source expression rejects silent inventory', () => { assert.equal(unknown.status, 1); assert.match(unknown.stderr, /unparsed constructor capability/); });
const fanSource = fs.readFileSync(path.join(repo, 'FanLab/real-host/Program.cs'), 'utf8');
for (const [name, content, expected] of [
  ['fan-good', fanSource, 0],
  ['fan-wrong-namespace', fanSource.replace('routes["HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex"] =', 'routes["HandheldCompanion.Devices.Wrong.OneXPlayerApex"] ='), 1],
  ['fan-missing-alias', fanSource.replace(/routes\["HandheldCompanion\.Devices\.OneXPlayer\.OneXPlayerApex"\]\s*=\s*routes\["HandheldCompanion\.Devices\.OneXPlayerX1AMD"\];/, ''), 1],
]) {
  const file = path.join(out, name + '.cs'); fs.writeFileSync(file, content);
  const r = execute(name, 'pwsh', ['-NoProfile', '-File', 'tools/fan_route_coverage_audit.ps1', '-FanHostSource', file, '-OutputRoot', path.join(out, name)]);
  check(name, () => assert.equal(r.status, expected));
  if (name === 'fan-good') check('Apex counted with inherited strategy', () => {
    const matrix = JSON.parse(fs.readFileSync(path.join(out, name, 'fan-capability-matrix.json'), 'utf8').replace(/^\uFEFF/, ''));
    assert.equal(matrix.registeredRoutes, matrix.selfDeclaredRoutes);
    assert.equal(matrix.routes.find(x => x.deviceClass === 'OneXPlayerApex').strategy, matrix.routes.find(x => x.deviceClass === 'OneXPlayerX1AMD').strategy);
  });
}
const empty = path.join(out, 'empty-matrix');fs.mkdirSync(empty);
fs.writeFileSync(path.join(empty, 'IDevice.cs'), 'namespace HandheldCompanion.Devices; public class IDevice {} public class Test : IDevice {}');
const noMatrices = execute('gyro-empty', 'pwsh', ['-NoProfile', '-File', 'tools/gyro_matrix_coverage_audit.ps1', '-HcDevicesRoot', empty]);
check('empty gyro parse fails', () => { assert.notEqual(noMatrices.status, 0); assert.match(noMatrices.stderr, /matrix-parse-empty/); });
fs.writeFileSync(path.join(empty, 'IDevice.cs'), 'namespace HandheldCompanion.Devices; public class IDevice { void Load() { DeviceConfigurationHelper.LoadConfiguration("Test"); } } public class Test : IDevice {}');
const missingConfigs = execute('gyro-json-missing', 'pwsh', ['-NoProfile', '-File', 'tools/gyro_matrix_coverage_audit.ps1', '-HcDevicesRoot', empty, '-HcConfigsRoot', path.join(out, 'not-present')]);
check('missing migrated JSON source fails', () => { assert.notEqual(missingConfigs.status, 0); assert.match(missingConfigs.stderr, /matrix-config-source-missing/); });
const configs = path.join(out, 'configs');fs.mkdirSync(configs);
fs.writeFileSync(path.join(configs, 'Test.json'), JSON.stringify({ DeviceClass: 'Test', GyroMatrix: { AxisSwap: { X: 'Y', Y: 'X', Z: 'Y' } } }));
const invalid = execute('gyro-json-invalid', 'pwsh', ['-NoProfile', '-File', 'tools/gyro_matrix_coverage_audit.ps1', '-HcDevicesRoot', empty, '-HcConfigsRoot', configs]);
check('non-bijective matrix rejected', () => { assert.notEqual(invalid.status, 0); assert.match(invalid.stdout, /matrix-config-invalid/); });
const summary = { status: 'PASS', checks: results, hardwareWrites: false, out };
fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(summary, null, 2) + '\n');
console.log(`PC_CAPABILITY_AUDIT_SELFTEST=PASS checks=${results.length} output=${out}`);
