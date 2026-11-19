import assert from 'node:assert/strict';
import fs from 'node:fs';
import {auditBatteryRouteIdentity as audit} from './lib/battery_route_identity_audit.mjs';

const {rows} = JSON.parse(fs.readFileSync(new URL('../capabilities/hc-device-catalog.json', import.meta.url), 'utf8')).domains.battery;
const rules = names => names.map(name => {
  const rule = rows.find(r => r.deviceClass === name);
  assert.ok(rule, 'missing catalog rule: ' + name);
  return rule.ruleId;
});
const samples = [
  ['logged X2 Mini Pro', 'ONE-NETBOOK', 'ONEXPLAYER X2Mini PRO', 'ONEXPLAYER X2Mini PRO', '', ['OneXPlayerX2MiniPro']],
  ['logged APEX', 'ONE-NETBOOK', 'ONEXPLAYER APEX', 'ONEXPLAYER APEX', '', ['OneXPlayerApex']],
  ['logged X1 Mini', 'ONE-NETBOOK', 'ONEXPLAYER X1 mini', 'ONEXPLAYER X1 mini', '', ['OneXPlayerX1Mini']],
  ['logged A1X', 'AOKZOE', 'AOKZOE A1X', 'AOKZOE A1X', '', []],
  ['OP3 system-only positive', 'ONE-NETBOOK', 'unrelated-board', ' onexplayer-3 ', '', ['OneXPlayer3']],
  ['OP3 board-only negative', 'ONE-NETBOOK', 'ONEXPLAYER 3', '', '', []],
  ['mixed Mini Pro / OP3', 'ONE-NETBOOK', 'ONEXPLAYER X2Mini PRO', 'ONEXPLAYER 3', '', ['OneXPlayerX2MiniPro', 'OneXPlayer3']],
  ['mixed Super X / OP3', 'ONE-NETBOOK TECHNOLOGY CO., LTD.', 'ONEXPLAYER SUPER X', 'ONEXPLAYER 3', '', ['OneXPlayer3', 'OneXPlayerSuperX']],
  ['Super X raw positive', 'OEM ONE-NETBOOK alias', 'ONEXPLAYER SUPER X', '', '', ['OneXPlayerSuperX']],
  ['Super X case negative', 'one-netbook', 'ONEXPLAYER SUPER X', '', '', []],
  ['Super X padding negative', 'ONE-NETBOOK', 'ONEXPLAYER SUPER X ', '', '', []],
  ['Win Mini HX370 positive', 'GPD', 'G1617-02', '', 'AMD Ryzen AI 9 HX370', ['GPDWinMini_HX370']],
  ['Win Mini different CPU', 'GPD', 'G1617-02', '', 'AMD Ryzen 7 8840U', []],
  ['Win 5 is not Win Mini', 'GPD', 'G1618-05', '', 'AMD Ryzen AI MAX+ 395', []],
  ['Legion Go field identity', 'LENOVO', 'LNVNB161216', '83E1', 'AMD Ryzen Z1 Extreme', ['LegionGoTablet']],
  ['Legion Go 2 8ASP2', 'LENOVO', '', '83N0', '', ['LegionGoTablet2']],
  ['Legion Go 2 8AHP2', 'LENOVO', '', '83N1', '', ['LegionGoTablet2']],
  ['Legion Go S 8ARP1', 'LENOVO', '', '83L3', '', ['LegionGoSZ2']],
  ['Legion Go S 8APU1', 'LENOVO', '', '83N6', '', ['LegionGoSZ1']],
  ['HC selector 83Q2', 'LENOVO', '', '83Q2', '', ['LegionGoSZ1']],
  ['HC selector 83Q3', 'LENOVO', '', '83Q3', '', ['LegionGoSZ1']],
  ['Legion selector ignores board SKU when system SKU is present', 'LENOVO', '83N6', '83E1', '', ['LegionGoTablet']],
  ['Legion shared board alone is not a system identity', 'LENOVO', 'LNVNB161216', '', '', []],
  ['Legion MT in the wrong DMI field', 'LENOVO', '83E1', '', '', []],
  ['Legion unknown system SKU', 'LENOVO', 'LNVNB161216', '83E10', '', []],
  ['Legion system SKU from another vendor', 'NOT LENOVO', 'LNVNB161216', '83E1', '', []],
].map(([name, manufacturer, boardProduct, systemProduct, processor, expected]) => ({name, identity: {manufacturer, boardProduct, systemProduct, processor}, expected: rules(expected)}));

const result = audit(rows, samples);
assert.equal(result.pairsExamined, rows.length * (rows.length - 1) / 2);
for (const [i, {hits, selection}] of result.samples.entries()) {
  assert.deepEqual(hits, samples[i].expected, samples[i].name);
  assert.deepEqual(selection, {status: hits.length > 1 ? 'ambiguous' : hits.length === 1 ? 'unique' : 'no-match',
    ruleId: hits.length === 1 ? hits[0] : null}, samples[i].name);
}
const ambiguous = {status: 'ambiguous', ruleId: null};
for (const pair of result.pairs) {
  assert.deepEqual(pair.selection, ambiguous, pair.rules.join(' + '));
  for (const projection of [rows, [...rows].reverse()]) {
    assert.deepEqual(audit(projection, [{identity: pair.witness}]).samples[0].selection, ambiguous);
  }
}
const op3 = rows.find(r => r.deviceClass === 'OneXPlayer3');
assert.ok(result.pairs.some(p => p.kind === 'cross-field' && p.rules.includes(op3.ruleId) && p.rules.includes(rules(['OneXPlayerX2MiniPro'])[0])));
const duplicate = {...op3, ruleId: 'synthetic.normalized-alias', identity: {...op3.identity, values: [' onexplayer-3 ']}};
assert.deepEqual(audit([duplicate, op3]).pairs.find(p => p.kind === 'same-field')?.selection, ambiguous);
const historical = (rows, identity) => audit(rows, [{identity}], { historical: true }).samples[0];
const mixed = {manufacturer: 'ONE-NETBOOK', boardProduct: 'ONEXPLAYER SUPER X', systemProduct: 'ONEXPLAYER 3', processor: ''};
for (const projection of [rows, [...rows].reverse()]) {
  const incomplete = historical(projection, mixed);
  assert.deepEqual(incomplete.hits, [op3.ruleId]);
  assert.deepEqual(incomplete.selection, {status: 'indeterminate', ruleId: null});
  assert.deepEqual(historical(projection, {...mixed, boardProductRaw: 'ONEXPLAYER SUPER X', boardManufacturerRaw: 'ONE-NETBOOK'}).selection, ambiguous);
  assert.deepEqual(historical(projection, {...mixed, boardProductRaw: 'ONEXPLAYER SUPER X '}).selection, {status: 'unique', ruleId: op3.ruleId});
}
const oxIdentity = {manufacturer: 'ONE-NETBOOK', boardProduct: 'ONEXPLAYER X2Mini PRO', systemProduct: '', processor: ''};
const msiIdentity = {manufacturer: 'M\u0131cro-Star International Co., Ltd.', boardProduct: 'MS-1T41', systemProduct: '', processor: ''};
const gpdIdentity = {manufacturer: 'GPD', boardProduct: 'G1617-02', systemProduct: '', processor: ''};
const superIdentity = {...mixed, systemProduct: '', boardProductRaw: 'ONEXPLAYER SUPER X', boardManufacturerRaw: '\u00e9 ONE-NETBOOK'};
const engineCases = [
  [{...oxIdentity, manufacturer: 'ONE-NETBOOK\ufeff'}, [], rules(['OneXPlayerX2MiniPro'])],
  [msiIdentity, [], rules(['ClawA1M'])],
  [{...oxIdentity, manufacturer: '\u0085ONE-NETBOOK\u00a0'}, rules(['OneXPlayerX2MiniPro']), []],
  [{...gpdIdentity, processor: '\u00e9HX370'}, [], rules(['GPDWinMini_HX370'])],
  [{...gpdIdentity, processor: 'HX\u0085370'}, rules(['GPDWinMini_HX370']), []],
  [{...gpdIdentity, boardProduct: 'unrelated', processor: '\u00e9HX370'}, [], []],
  [{...oxIdentity, boardProductRaw: 'ONEXPLAYER X2M\u0131n\u0131 PRO'}, [], rules(['OneXPlayerX2MiniPro'])],
  [{...superIdentity, manufacturer: superIdentity.boardManufacturerRaw, boardProduct: superIdentity.boardProductRaw}, rules(['OneXPlayerSuperX']), []],
  [{...oxIdentity, systemProduct: 'ONEXPLAYER\u00a03', processor: '\u00e9'}, rules(['OneXPlayerX2MiniPro']), rules(['OneXPlayer3'])],
];
for (const [identity, hits, uncertain] of engineCases) {
  const result = historical(rows, identity);
  assert.deepEqual(result.hits, hits, JSON.stringify(identity));
  assert.deepEqual(result.indeterminateEngineRuleIds ?? [], uncertain, JSON.stringify(identity));
  assert.deepEqual(result.selection, {
    status: uncertain.length ? 'indeterminate' : hits.length ? 'unique' : 'no-match',
    ruleId: uncertain.length ? null : hits[0] ?? null,
  }, JSON.stringify(identity));
}
assert.throws(() => audit([{...op3, identity: {...op3.identity, processorPattern: 'unreviewed'}}]), /unreviewed/);
console.log(`Battery identity audit: ${result.pairsExamined} pairs, ${result.pairs.length} possible overlaps, ${samples.length} identity samples and ${engineCases.length} text-engine controls passed.`);
