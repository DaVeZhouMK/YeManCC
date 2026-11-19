import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { auditOneXConsoleBattery } from './lib/battery_onex_oem_audit.mjs';
import { assertBatteryAuditRouteRows, auditBatteryRouteIdentity } from './lib/battery_route_identity_audit.mjs';

const [hcPath, fieldPath, oneXPath, ayaPath, oxpecPath] = process.argv.slice(2);
assert.equal(process.argv.length, 7,
  'usage: node tools/battery-oem-family-source-audit.mjs <reviewed HC audit JSON> <reviewed field matrix JSON> <pinned OneX background.js> <pinned AYANEO platform.c> <pinned Linux oxpec.c>');
const repo = fileURLToPath(new URL('../', import.meta.url));
const absolute = file => path.resolve(file).replaceAll('\\', '/');
const read = file => {
  const bytes = fs.readFileSync(file);
  return { bytes, pin: { path: absolute(file), sha256: createHash('sha256').update(bytes).digest('hex') } };
};
const json = ({ bytes }) => JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
const norm = value => (value ?? '').trim().toUpperCase();
const equal = (a, b) => norm(a) === norm(b);
// A matching route SHA does not authenticate derived HC or field facts.
const reviewedHcAudits = new Set([
  '12b658c0429433950b19ae50ab03011f196b87df3852f274b7d72bd2a6e11c42', // current library report
  'a141676c48bde3f7c883079b94723bdf7a265d13449b8090eff9c712cce2c4bb', // fixed HC221 historical replay
]);
const reviewedFieldMatrices = new Set([
  '4eb6ec8f4a2b378935c2625b9c02d88be6481d8f5440f140966b39a749c2375f', // HC293
  '008510cf3abcc40195a52a47c4959657f8f5f9fa16cd16796752a5a0d27d9d96', // HC301
  'd384a11285ec04ddf3c2341bf96abbe76b5354f6202bfcd5294f257769f95f75', // HC308
]);
const routeInput = read(path.join(repo, 'native/BatteryHost/battery-routes.json'));
const catalogInput = read(path.join(repo, 'capabilities/hc-device-catalog.json'));
const hcInput = read(hcPath);
const fieldInput = read(fieldPath);
const ayaInput = read(ayaPath);
const oxpecInput = read(oxpecPath);
const routeSha = routeInput.pin.sha256;
const hc = json(hcInput);
const field = json(fieldInput);
const catalog = json(catalogInput).domains.battery;
assert.equal(hc.runtimeRouteSha256, routeSha, 'HC audit refers to another route revision');
assert.equal(field.ruleDocumentSha256, routeSha, 'field matrix refers to another route revision');
assert.equal(field.schemaVersion, 2, 'field matrix predates unique-only selection; regenerate from the original field facts');
assertBatteryAuditRouteRows(catalog, routeInput.bytes);
assert.equal(ayaInput.pin.sha256, '86da6d57233eeb7e415645c047c0b4a7b96db9b7645812b149d67e4e68f8c9c7',
  'AYANEO source is not the reviewed commit');
assert.equal(oxpecInput.pin.sha256, '254499952d4b2383b0cee42e3349323023e3e9624aa269628de92835568e20f7',
  'oxpec source is not the reviewed commit');

const routes = catalog.rows;
const { samples: currentProfiles } = auditBatteryRouteIdentity(routes, field.profiles.map(({ identity }) => ({ identity })), { historical: true });
for (const [i, sample] of currentProfiles.entries()) {
  assert.deepEqual(field.profiles[i].selection, sample.selection, `field matrix selection differs: profile ${i}`);
  assert.deepEqual(field.profiles[i].hits, sample.hits, `field matrix rule hits differ: profile ${i}`);
  assert.deepEqual(field.profiles[i].indeterminateOrdinalRuleIds, sample.indeterminateOrdinalRuleIds,
    `field matrix raw identity evidence differs: profile ${i}`);
  assert.deepEqual(field.profiles[i].indeterminateEngineRuleIds, sample.indeterminateEngineRuleIds,
    `field matrix identity engine evidence differs: profile ${i}`);
}
assert.ok(reviewedHcAudits.has(hcInput.pin.sha256), 'battery crosswalk HC audit is not reviewed');
assert.ok(reviewedFieldMatrices.has(fieldInput.pin.sha256), 'battery crosswalk field matrix is not reviewed');
const profiles = (vendor, product, vendorMatches = equal) => field.profiles.flatMap(p => {
  if (!vendorMatches(p.identity.manufacturer, vendor)) return [];
  const matchedFields = ['boardProduct', 'systemProduct'].filter(key => equal(p.identity[key], product));
  return matchedFields.length ? [{ identity: p.identity, matchedFields, firmware: p.observedFirmware, currentSelection: p.selection,
    ...(p.indeterminateOrdinalRuleIds ? { indeterminateOrdinalRuleIds: p.indeterminateOrdinalRuleIds } : {}),
    ...(p.indeterminateEngineRuleIds ? { indeterminateEngineRuleIds: p.indeterminateEngineRuleIds } : {}) }] : [];
});
const hcLiterals = (dataset, families, product) => hc.datasets[dataset].rows.flatMap(row => {
  if (!families.includes(row.family)) return [];
  const factoryLiterals = row.identityEvidence.flatMap(e => e.switches)
    .filter(s => ['ProductName', 'SystemModel', 'SystemName'].includes(s.field))
    .flatMap(s => s.values.filter(v => equal(v, product)).map(value => ({ field: s.field, value })));
  return factoryLiterals.length ? [{ deviceClass: row.deviceClass, factoryLiterals, batteryStatus: row.effectiveStatus }] : [];
});

const oxpec = oxpecInput.bytes.toString('utf8').replaceAll('\r\n', '\n');
const oxpecDmi = oxpec.match(/static const struct dmi_system_id dmi_table\[\] = \{([\s\S]+?)\n\};/)[1];
const oxpecGate = oxpec.match(/static bool oxp_psy_ext_supported\(void\)\n\{([\s\S]+?)\n\}/)[1];
const chargeBoards = [...oxpecGate.matchAll(/case (\w+):/g)].map(match => match[1]);
assert.deepEqual(chargeBoards, ['oxp_x1', 'oxp_g1_i', 'oxp_g1_a', 'oxp_fly']);
assert.match(oxpec, /if \(oxp_psy_ext_supported\(\)\) \{\s+ret = devm_battery_hook_register/);
const oxpecRows = Array.from(oxpecDmi.matchAll(
  /DMI_(EXACT_MATCH|MATCH)\(DMI_BOARD_VENDOR, "([^"]+)"\),\s+DMI_(EXACT_MATCH|MATCH)\(DMI_BOARD_NAME, "([^"]+)"\),\s+\},\s+\.driver_data = \(void \*\)(\w+),/g),
([, vendorMatch, vendor, boardMatch, boardProduct, board]) => ({
  vendorMatch, vendor, boardMatch, boardProduct, board, linuxChargeHook: chargeBoards.includes(board),
}));
assert.equal(oxpecRows.length, 28);
assert.equal(oxpecRows.length, [...oxpecDmi.matchAll(/\.driver_data\s*=/g)].length);

const oneX = auditOneXConsoleBattery(oneXPath, routes);
const oneXBranches = oneX.branches.map(branch => ({
  offset: branch.offset, vendor: branch.manufacturer, firmwareConditions: branch.firmwareConditions,
  labels: oneX.offerings.filter(o => o.offset === branch.offset).map(o => ({
    product: o.product, routeLiteralMatches: o.matchingLiterals,
    fixedHC: hcLiterals('locked', ['OneXPlayer', 'AOKZOE', 'ASUS', 'MSI'], o.product),
    fixedUpstreamHC: hcLiterals('upstream', ['OneXPlayer', 'AOKZOE', 'ASUS', 'MSI'], o.product),
    linuxOxpecExactBoardLiterals: oxpecRows.filter(r => r.boardMatch === 'EXACT_MATCH' && r.vendor === o.manufacturer && r.boardProduct === o.product),
    historicalProfiles: profiles(o.manufacturer, o.product, (actual, candidate) =>
      candidate === 'ONE-NETBOOK' ? norm(actual).includes(candidate) : equal(actual, candidate)),
  })),
}));

const aya = ayaInput.bytes.toString('utf8');
const dmiTable = aya.match(/static const struct dmi_system_id dmi_table\[\] = \{([\s\S]+?)\n\};/)[1];
const check = aya.match(/static int ayaneo_check_charge_control\(void\)\n\{([\s\S]+?)\n\}/)[1];
assert.match(check, /default:\s+return -1;/);
assert.match(check, /ret = memcmp\(version, version_needed, version_length\);\s+return ret;/);
assert.match(aya, /if\(ayaneo_check_charge_control\(\) >= 0\) \{\s+ps_priv\.bypass_available = 1;/);
const chargeModels = [...check.matchAll(/case (\w+):/g)].map(match => match[1]);
assert.deepEqual(chargeModels, ['air_plus', 'slide']);
const ayaRows = Array.from(dmiTable.matchAll(
  /DMI_EXACT_MATCH\(DMI_BOARD_VENDOR, "([^"]+)"\),\s+DMI_EXACT_MATCH\(DMI_BOARD_NAME, "([^"]+)"\),\s+\},\s+\.driver_data = \(void \*\)(\w+),/g),
([, vendor, boardProduct, model]) => ({
  vendor, boardProduct, model, linuxChargeCheck: chargeModels.includes(model) ? 'requires-ec-version-probe' : 'default-denied',
  windowsBoardRules: routes.filter(({ identity: r }) => r.field === 'boardProduct' &&
    r.manufacturers.some(value => r.manufacturerMatch === 'ordinal-contains' ? vendor.includes(value) : equal(value, vendor)) &&
    r.values.some(value => r.valueMatch === 'ordinal-exact' ? value === boardProduct : equal(value, boardProduct))).map(r => r.ruleId),
  fixedHC: hcLiterals('locked', ['AYANEO', 'SuiPlay'], boardProduct),
  fixedUpstreamHC: hcLiterals('upstream', ['AYANEO', 'SuiPlay'], boardProduct),
  historicalProfiles: profiles(vendor, boardProduct),
}));
assert.equal(ayaRows.length, 13);
assert.equal(ayaRows.filter(row => row.linuxChargeCheck === 'requires-ec-version-probe').length, 2);
const labels = oneXBranches.flatMap(b => b.labels);
const unmatched = labels.filter(l => !l.routeLiteralMatches.length);

console.log(JSON.stringify({
  schemaVersion: 1,
  scope: 'Static literal crosswalk only; OEM Product and Linux DMI do not confer Windows admission or prove a transaction.',
  inputs: { tool: read(fileURLToPath(import.meta.url)).pin,
    oneXParser: read(path.join(repo, 'tools/lib/battery_onex_oem_audit.mjs')).pin,
    routeAuditModel: read(path.join(repo, 'tools/lib/battery_route_identity_audit.mjs')).pin,
    routes: routeInput.pin, generatedCatalog: catalogInput.pin, hcAudit: hcInput.pin, historicalFieldMatrix: fieldInput.pin,
    oneX: { path: absolute(oneXPath), sha256: oneX.sourceSha256 },
    aya: { ...ayaInput.pin, revision: 'eb849d5deabe70fde004f67349fa281af322a499' },
    oxpec: { ...oxpecInput.pin, revision: '24a19b953e2a3d0ea5a8c4951a0ab8f32720c907' } },
  oneX: { offsetUnit: oneX.offsetUnit, summary: { branches: oneXBranches.length, labels: labels.length,
    withoutRuntimeLiteral: unmatched.length, withoutRuntimeLiteralWithFixedHc: unmatched.filter(l => l.fixedHC.length).length,
    withoutRuntimeLiteralWithFixedUpstreamHc: unmatched.filter(l => l.fixedUpstreamHC.length).length,
    withoutRuntimeLiteralWithFieldProfile: unmatched.filter(l => l.historicalProfiles.length).length,
    withoutRuntimeLiteralWithLinuxOxpecChargeHook: unmatched.filter(l => l.linuxOxpecExactBoardLiterals.some(r => r.linuxChargeHook)).length },
    branches: oneXBranches, linuxOxpec: { chargeBoards, rows: oxpecRows } },
  aya: { rows: ayaRows },
}, null, 2));
