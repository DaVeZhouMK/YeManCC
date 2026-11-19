// Check the reviewed OP3 overlay independently of regenerating its runtime row.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const repo = fileURLToPath(new URL('../', import.meta.url));
const overlayPath = 'native/BatteryHost/oxp3-battery-route.json';
const routesPath = 'native/BatteryHost/battery-routes.json';
const generator = 'tools/battery-charge-generate-routes.mjs';
const sourcePath = 'capabilities/upstream/f2e58468/OneXPlayer3.cs';
const configPath = 'capabilities/upstream/f2e58468/OneXPlayer3.json';
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const read = (root, name) => fs.readFileSync(path.join(root, name));
const json = (root, name) => JSON.parse(read(root, name));
const sharedFiles = [overlayPath, routesPath, generator, sourcePath, configPath,
  'tools/lib/hc_battery_catalog.mjs', 'docs/battery-charge-capabilities.json',
  'native/BatteryHost/msi-ap-route-review.json', 'native/BatteryHost/superx-charge-route.json',
  'capabilities/upstream/xmax-6d728795/SuperXCharge.source-facts.json',
  'capabilities/upstream/xmax-6d728795/SuperXFan.source-facts.json',
  'capabilities/upstream/onexconsole-0.10.4-fix3/X2MiniProCharge.source-facts.json'];
const [mode, scratch] = process.argv.slice(2);
if (mode === undefined || !((mode === '--check' && !scratch) || (mode === '--exercise' && scratch)) || process.argv.length > 4)
  throw Error('usage: node tools/battery-op3-overlay-audit.mjs --check | --exercise <new scratch directory>');

function verify(root) {
  const overlay = json(root, overlayPath);
  const { schema, source, protocol, limits, bypassModes, ...row } = overlay;
  assert.equal(schema, 1, 'OP3 schema drift');
  assert.deepEqual(row, {
    route: 'oxp3-wmi-ec', family: 'OneXPlayer 3', kind: 'firmware-percent', min: 40,
    identity: { manufacturers: ['ONE-NETBOOK', 'ONE-NETBOOK TECHNOLOGY CO., LTD.'], field: 'systemProduct',
      values: ['ONEXPLAYER 3', 'ONEXPLAYER-3'], deviceClass: 'OneXPlayer3' },
    firmwareGate: { kind: 'provider-probe-required', majorMin: 0, minorMin: 0 },
  }, 'OP3 reviewed runtime projection changed');
  assert.deepEqual(json(root, routesPath).filter(r => r.route === row.route || r.identity.deviceClass === row.identity.deviceClass),
    [row], 'OP3 runtime row drift');

  assert.equal(source.commit, 'f2e58468d9917189d4be7ccfc242385ce2c3035b', 'OP3 source revision drift');
  assert.equal(source.pinnedSource, sourcePath, 'OP3 source path drift');
  assert.equal(source.deviceJson, configPath, 'OP3 device JSON path drift');
  for (const [file, declared, expected] of [
    [sourcePath, source.sha256, 'e3a6275e34db52fa60c7b5a421655af24d8580611fe9ebb875986817891809dc'],
    [configPath, source.deviceJsonSha256, '5c0f9ae26f1b01233d0ffee78dc7831df2bf2447dacbbea7145f8a321617371a'],
  ]) {
    assert.equal(declared, expected, `OP3 declared source hash drift: ${file}`);
    assert.equal(sha(read(root, file)), expected, `OP3 pinned source bytes drift: ${file}`);
  }
  for (const [ref, name, expected] of [
    [source.parentX2, 'OneXPlayerX2.cs', '41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b'],
    [source.wmiEc, 'OneXPlayerWmiEc.cs', 'f84150a6e4714568e4cb14c019bd8e3cda394a32ef0cebe620a86f82c16a3021'],
  ]) {
    assert.equal(path.win32.basename(ref.path), name, 'OP3 parent source path drift');
    assert.equal(ref.sha256, expected, 'OP3 parent source declaration drift');
  }
  assert.deepEqual(protocol, {
    namespace: 'root\\WMI', class: 'SuRwECRegInterface', readMethod: 'ReadECReg', writeMethod: 'WriteECReg',
    limitRegister: '0x04A3', bypassRegister: '0x04A4', addressPacking: 'bank | offset << 8 (0x04A3 -> 0xA304)',
    writePacking: 'packedAddress | value << 16', response: 'hex status,value; status must be 0',
  }, 'OP3 source protocol declaration drift');
  assert.deepEqual(limits, { min: 0, max: 100, step: 1, origin: 'upstream source range; product choices remain 50..100 by 5' },
    'OP3 source limits drift');
  assert.deepEqual(bypassModes, { 0: '0x00', 1: '0x01', 2: '0x0B' }, 'OP3 source bypass declaration drift');
}

verify(repo);
const result = { schemaVersion: 1, baseline: 'passed', evidence: 'Offline overlay checks; no runtime change or hardware',
  inputs: Object.fromEntries([...sharedFiles, 'tools/battery-op3-overlay-audit.mjs'].map(name => [name, sha(read(repo, name))])), cases: [] };

if (mode === '--exercise') {
  const base = path.resolve(scratch);
  // Do not reuse, delete, or modify a caller's existing directory.
  fs.mkdirSync(base);
  const mutations = [
    ['probe-kind', o => { o.firmwareGate = { kind: 'exact-board-firmware', pairs: [] }; }, /runtime projection/],
    ['new-cpu-filter', o => { o.identity.processorPattern = 'ExampleCPU'; }, /runtime projection/],
    ['declared-source-hash', o => { o.source.sha256 = '0'.repeat(64); }, /declared source hash/],
    ['parent-source-hash', o => { o.source.wmiEc.sha256 = '0'.repeat(64); }, /parent source declaration/],
    ['source-bytes', null, /pinned source bytes/],
    ['protocol-response', o => { o.protocol.response = 'unspecified'; }, /protocol declaration/],
    ['source-step', o => { o.limits.step = 5; }, /source limits/],
  ];
  const runGenerator = (root, ...args) => {
    const run = spawnSync(process.execPath, [path.join(root, generator), ...args], { cwd: root, encoding: 'utf8', timeout: 10000, windowsHide: true });
    if (run.error) throw run.error;
    assert.equal(run.status, 0, run.stderr);
  };
  for (const [name, mutate, refusal] of mutations) {
    const root = path.join(base, name);
    for (const file of sharedFiles) {
      const target = path.join(root, file);
      fs.mkdirSync(path.dirname(target), { recursive: true });
      fs.copyFileSync(path.join(repo, file), target);
    }
    const overlay = json(root, overlayPath);
    if (mutate) {
      mutate(overlay);
      fs.writeFileSync(path.join(root, overlayPath), JSON.stringify(overlay, null, 2) + '\n');
    } else fs.appendFileSync(path.join(root, sourcePath), '\n// Synthetic audit mutation.\n');
    runGenerator(root);
    runGenerator(root, '--check');
    assert.throws(() => verify(root), refusal, name);
    result.cases.push({ name, originalGenerator: 'accepted', originalCheck: 'accepted', offlineAudit: 'rejected',
      runtimeOutputChanged: sha(read(root, routesPath)) !== result.inputs[routesPath] });
  }
}
for (const [name, pin] of Object.entries(result.inputs)) assert.equal(sha(read(repo, name)), pin, 'input drift: ' + name);
console.log(JSON.stringify(result, null, 2));
