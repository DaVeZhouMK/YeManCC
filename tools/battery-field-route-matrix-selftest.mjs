import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const [scratch] = process.argv.slice(2);
assert.ok(scratch, 'usage: node tools/battery-field-route-matrix-selftest.mjs <new scratch directory>');
fs.mkdirSync(scratch);
const input = path.join(scratch, 'FACTS.json');
const output = path.join(scratch, 'MATRIX.json');
const identity = { manufacturer: 'ONE-NETBOOK', boardProduct: 'ONEXPLAYER X2Mini PRO',
  systemProduct: 'ONEXPLAYER X2Mini PRO', bios: '0.22', ecMajor: 0, ecMinor: 12 };
const legion = {manufacturer: 'LENOVO', boardProduct: 'LNVNB161216', systemProduct: '83E1'};
const gpd = {manufacturer: 'GPD', boardProduct: 'G1617-02', systemProduct: 'G1617-02', processor: 'AMD Ryzen AI 9 HX 370'};
const superX = {manufacturer: 'ONE-NETBOOK', boardProduct: 'ONEXPLAYER SUPER X', systemProduct: 'ONEXPLAYER SUPER X',
  boardManufacturerRaw: 'prefix ONE-NETBOOK suffix', boardProductRaw: 'ONEXPLAYER SUPER X'};
const fixed = 'firmware-fixed';
const get = 'reported-firmware-get-readback';
const set = 'reported-firmware-set-result-readback';
const selectable = 'readback-is-current-selectable-target';
const cases = [
  ['get', { operation: 'get' }, 'reported-firmware-get-readback'],
  ['set', { operation: 'set', requestedLimit: 75 }, 'reported-firmware-set-result-readback'],
  ['restore', { operation: 'restore', requestedLimit: 75 }, 'reported-firmware-restore-result-readback'],
  ['refresh', { operation: 'refresh' }, 'reported-firmware-refresh-result-readback'],
  ['release', { operation: 'release' }, 'reported-firmware-release-result-readback'],
  ['unknown-op', { operation: 'set-v2' }, 'firmware-readback-operation-unrecognized'],
  ['missing-op', {}, 'firmware-readback-operation-unrecognized'],
  ['no-device-admission', { operation: 'get', supported: false }, 'not-confirmed-by-result'],
  ['ack-only', { operation: 'set', verification: 'acknowledged', currentLimit: null }, 'firmware-threshold-unconfirmed'],
  ['software-target', { operation: 'get', kind: 'software-bypass-threshold' }, 'not-a-firmware-threshold-result'],
  ['string-value', { operation: 'get', currentLimit: '75' }, 'firmware-threshold-unconfirmed'],
  ['out-of-range', { operation: 'get', currentLimit: 101 }, 'firmware-threshold-unconfirmed'],
  ['fixed-kind-on-mini-pro', { operation: 'get', kind: fixed, currentLimit: 80 }, get, 'readback-kind-differs-from-current-rule'],
  ['mini-pro-below-current-min', { operation: 'get', currentLimit: 45 }, get, 'readback-outside-current-selectable-targets'],
  ['mini-pro-between-steps', { operation: 'get', currentLimit: 73 }, get, 'readback-outside-current-selectable-targets'],
  ['mini-pro-floor', { operation: 'get', currentLimit: 50 }, get, selectable],
  ['mini-pro-full', { operation: 'get', currentLimit: 100 }, get, selectable],
  ['legion-fixed-get', { operation: 'get', kind: fixed, currentLimit: 80 }, get, selectable, legion],
  ['legion-fixed-full', { operation: 'get', kind: fixed, currentLimit: 100 }, get, selectable, legion],
  ['legion-not-a-slider', { operation: 'get', kind: fixed, currentLimit: 85 }, get, 'readback-outside-current-selectable-targets', legion],
  ['gpd-floor', { operation: 'get', currentLimit: 60 }, get, selectable, gpd],
  ['gpd-below-floor', { operation: 'get', currentLimit: 55 }, get, 'readback-outside-current-selectable-targets', gpd],
  ['superx-floor', { operation: 'get', currentLimit: 75 }, get, selectable, superX],
  ['superx-below-floor', { operation: 'get', currentLimit: 70 }, get, 'readback-outside-current-selectable-targets', superX],
  ['unmatched-current-rule', { operation: 'get' }, get, 'current-static-rule-unresolved', {...identity, boardProduct: 'unlisted', systemProduct: 'unlisted'}],
  ['mixed-one-x-identity', { operation: 'get' }, get, 'current-static-rule-unresolved', {...identity, systemProduct: 'ONEXPLAYER 3'}],
  ['get-saved-target-differs', { operation: 'get', currentLimit: 80, requestedLimit: 75 }, get, selectable],
  ['set-comparable', { operation: 'set', requestedLimit: 75 }, set, 'set-readback-matches-current-selectable-target'],
  ['set-different-current', { operation: 'set', requestedLimit: 80 }, set, 'set-readback-differs-from-reported-target'],
  ['set-target-missing', { operation: 'set' }, set, 'set-reported-target-unavailable'],
  ['set-target-string', { operation: 'set', requestedLimit: '75' }, set, 'set-reported-target-unavailable'],
  ['set-target-not-selectable', { operation: 'set', requestedLimit: 73 }, set, 'set-reported-target-outside-current-selectable-targets'],
];
const groups = cases.map(([name, overrides, , , observed = identity]) => ({ identity: observed, event: 'request-result', count: 1,
  fact: { ok: true, supported: true, batteryPresent: true, kind: 'firmware-percent', verification: 'readback', currentLimit: 75, ...overrides },
  sample: { archive: name } }));
fs.writeFileSync(input, JSON.stringify({ schemaVersion: 1, errors: [], groups }));
const tool = fileURLToPath(new URL('./battery-field-route-matrix.mjs', import.meta.url));
const result = spawnSync(process.execPath, [tool, input, output], { encoding: 'utf8' });
assert.equal(result.status, 0, result.stderr || result.error?.message);
const report = JSON.parse(fs.readFileSync(output, 'utf8'));
const requests = report.profiles.flatMap(profile => profile.observedRequests);
assert.equal(requests.length, groups.length);
const actual = new Map(requests.map(row => [row.sample.archive, row]));
assert.equal(actual.size, groups.length);
for (const group of groups) assert.deepEqual(actual.get(group.sample.archive).fact, group.fact);
for (const [name, , expected] of cases) assert.equal(actual.get(name).thresholdEvidence, expected, name);
const expectedRelations = new Map(cases.filter(([, , , relation]) => relation).map(([name, , , relation]) => [name, relation]));
const inherited = new Map([
  ['get', selectable], ['set', 'set-readback-matches-current-selectable-target'],
  ['restore', selectable], ['refresh', selectable], ['release', selectable],
]);
const checks = Object.fromEntries([...actual].map(([name, row]) => [name, {
  thresholdEvidence: row.thresholdEvidence, currentSelectableThresholdRelation: row.currentSelectableThresholdRelation ?? null,
}]));
console.log(JSON.stringify(checks, null, 2));
for (const [name] of cases) {
  assert.equal(checks[name].currentSelectableThresholdRelation, expectedRelations.get(name) ?? inherited.get(name) ?? null, name);
}
