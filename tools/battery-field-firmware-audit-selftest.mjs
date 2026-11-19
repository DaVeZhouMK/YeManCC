import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const [scratch] = process.argv.slice(2);
assert.ok(scratch, 'usage: node tools/battery-field-firmware-audit-selftest.mjs <new scratch directory>');
fs.mkdirSync(scratch);
const tool = 'tools/battery-field-firmware-audit.mjs';
const policy = 'native/BatteryHost/Policy.cs';
const providers = 'native/BatteryHost/Providers.cs';
const routes = 'native/BatteryHost/battery-routes.json';
const catalog = 'capabilities/hc-device-catalog.json';
const copy = file => {
  const to = path.join(scratch, file);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.copyFileSync(new URL('../' + file, import.meta.url), to);
  return to;
};
const runner = copy(tool);
for (const file of [policy, providers, routes, catalog, 'tools/lib/battery_route_identity_audit.mjs']) copy(file);
const mini = {manufacturer: 'ONE-NETBOOK', boardProduct: 'ONEXPLAYER X2Mini PRO', systemProduct: 'ONEXPLAYER X2Mini PRO',
  processor: 'AMD Ryzen AI Max+ 395', bios: '0.22', ecMajor: 0, ecMinor: 12};
const apex = {...mini, boardProduct: 'ONEXPLAYER APEX', systemProduct: 'ONEXPLAYER APEX', bios: '0.12'};
const x1 = {...mini, boardProduct: 'ONEXPLAYER X1 A', systemProduct: 'ONEXPLAYER X1 A', ecMajor: 1, ecMinor: 3};
const gpd = {...mini, manufacturer: 'GPD', boardProduct: 'G1617-02', systemProduct: 'G1617-02', processor: 'HX 370', bios: '2.10', ecMajor: 2, ecMinor: 7};
const op3 = {...mini, boardProduct: 'Intel OP3 board', systemProduct: 'ONEXPLAYER 3'};
const yes = 'policy-predicate-matches', no = 'policy-predicate-rejects', unknown = 'missing-or-invalid-fields';
const cases = [
  ['mini-exact', mini, yes],
  ['mini-trim-case', {...mini, systemProduct: '\u0085onexplayer x2mini pro\u00a0', bios: ' 0.22 '}, yes],
  ['mini-bios-reject', {...mini, bios: '0.23'}, no],
  ['mini-ec-reject', {...mini, ecMinor: 11}, no],
  ['mini-system-reject', {...mini, systemProduct: 'other'}, no],
  ['mini-missing-system', {...mini, systemProduct: ''}, unknown],
  ['mini-numeric-bios', {...mini, bios: 0.22}, unknown],
  ['mini-non-ascii-system', {...mini, systemProduct: 'ONEXPLAYER X2Min\u0131 PRO'}, 'text-engine-unverified'],
  ['mini-op3-mixed', {...mini, systemProduct: 'ONEXPLAYER 3'}, 'static-selection-unresolved'],
  ['no-sibling-firmware-a', {...mini, ecMinor: 11}, no],
  ['no-sibling-firmware-b', {...mini, bios: '0.23'}, no],
  ['apex-same-ec-as-mini', apex, no],
  ['x1-two-components', x1, yes],
  ['x1-not-a-version-sort', {...x1, ecMajor: 2, ecMinor: 2}, no],
  ['x1-unknown-sentinel', {...x1, ecMinor: 255}, no],
  ['x1-missing-ec', {...x1, ecMinor: null}, unknown],
  ['gpd-exact', gpd, yes],
  ['gpd-production-regex-not-oem-proof', {...gpd, bios: '2.10.1'}, yes],
  ['gpd-wrong-bios', {...gpd, bios: '12.10'}, no],
  ['op3-provider-only', op3, 'provider-probe-required'],
  ['deck-pair-only', {...mini, manufacturer: 'VALVE', boardProduct: 'Galileo', systemProduct: 'Galileo'}, 'provider-board-firmware-pair-required'],
  ['win5-unregistered', {...gpd, boardProduct: 'G1618-05', systemProduct: 'G1618-05'}, 'static-selection-unresolved'],
  ['legacy-unlinked', mini, 'no-linked-identity'],
  ['future-parent', mini, 'no-linked-identity'],
  ['apex-conflicting-rejection', apex, 'conflicting-historical-identity'],
];
const groups = cases.map(([name, identity]) => {
  const reject = name === 'apex-conflicting-rejection';
  const identityEvidence = {line: name === 'future-parent' ? 3 : 1, eventLineSha256: 'b'.repeat(64)};
  return {identity, event: reject ? 'firmware-admission-rejected' : 'request-result', count: 1,
    fact: reject ? {allowed: false, providerOpened: false, observed: {ecMinor: 13}} : {operation: 'get', supported: false},
    sample: {archive: name, member: 'battery-charge-host.log', pid: 7, line: 2, eventLineSha256: 'a'.repeat(64),
      ...(name === 'legacy-unlinked' ? {} : {identityEvidence})}};
});
const input = path.join(scratch, 'FACTS.json');
fs.writeFileSync(input, JSON.stringify({schemaVersion: 1, errors: [], groups}));
const run = name => {
  const out = path.join(scratch, name + '.json');
  return {out, ...spawnSync(process.execPath, [runner, input, out], {encoding: 'utf8', windowsHide: true})};
};
const baseline = run('BASELINE');
assert.equal(baseline.status, 0, baseline.stderr);
const {observations, ruleCoverage} = JSON.parse(fs.readFileSync(baseline.out, 'utf8'));
assert.equal(observations.length, cases.length);
assert.ok(ruleCoverage.some(row => row.model === 'OneXPlayerX1Intel' && !row.selectedInFacts));
for (const [index, [name, , expected]] of cases.entries()) {
  const row = observations[index];
  assert.equal(row.sample.archive, name);
  assert.equal(row.currentFirmwareProjection.status, expected, name);
  assert.deepEqual(row.historicalFact, groups[index].fact, name);
}
assert.equal(observations.find(row => row.sample.archive === 'mini-op3-mixed').selection.status, 'ambiguous');
assert.deepEqual(observations.find(row => row.sample.archive === 'mini-missing-system').currentFirmwareProjection.fields, ['systemProduct']);
const mutations = [
  ['policy-change', policy, 'id.EcMajor<255', 'id.EcMajor<=255', 'battery firmware source requires review'],
  ['norm-change', policy, 'value.Trim().ToUpperInvariant()', 'value.ToUpperInvariant()', 'battery firmware source requires review'],
  ['route-constant-change', providers, 'X2MiniProRouteId="oxp-x2-mini-pro-direct-ec"', 'X2MiniProRouteId="changed"', 'battery firmware source requires review'],
  ['data-drift', catalog, '"bios": "0.22"', '"bios": "0.23"', 'battery audit route row'],
];
for (const [name, file, before, after, expected] of mutations) {
  const target = path.join(scratch, file);
  const original = fs.readFileSync(target, 'utf8');
  assert.ok(original.includes(before), name);
  fs.writeFileSync(target, original.replace(before, after));
  const result = run(name);
  assert.equal(result.status, 1, result.stderr);
  assert.ok(result.stderr.includes(expected), result.stderr);
  assert.ok(!fs.existsSync(result.out), name);
  fs.writeFileSync(target, original);
}
console.log(`Battery firmware field audit: ${cases.length} projection and ${mutations.length} source/row rejection cases passed.`);
