// Compare the fixed HC factory with the battery catalog, without probing hardware.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { auditBatteryRouteIdentity } from './lib/battery_route_identity_audit.mjs';

const repo = fileURLToPath(new URL('../', import.meta.url));
const [factoryPath, motherboardPath] = process.argv.slice(2);
if (process.argv.length !== 4) throw Error('usage: node tools/battery-op3-dmi-source-audit.mjs <fixed IDevice.cs> <fixed MotherboardInfo.cs>');
const inputs = {};
function read(name, file, expectedSha) {
  const absolute = path.resolve(file);
  const bytes = fs.readFileSync(absolute);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  if (expectedSha) assert.equal(sha256, expectedSha, `${name}: wrong pinned input`);
  inputs[name] = { path: absolute, bytes: bytes.length, sha256 };
  return bytes.toString('utf8');
}
const local = (name, file) => read(name, path.join(repo, file));
const factory = read('hcFactory', factoryPath, 'f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47');
const motherboard = read('hcMotherboardInfo', motherboardPath, '42b6fcf99721e978c04dd05530ef2a37c477a63a61b46e5fbdffe864aef0a1bc');
const providers = local('ymccIdentityCapture', 'native/BatteryHost/Providers.cs');
const policy = local('ymccPolicy', 'native/BatteryHost/Policy.cs');
const routes = JSON.parse(local('runtimeRoutes', 'native/BatteryHost/battery-routes.json'));
const { domains: { battery } } = JSON.parse(local('catalog', 'capabilities/hc-device-catalog.json'));
local('auditModel', 'tools/lib/battery_route_identity_audit.mjs');
local('auditTool', 'tools/battery-op3-dmi-source-audit.mjs');
assert.equal(battery.ruleDocumentSha256, inputs.runtimeRoutes.sha256, 'stale battery catalog');

function line(text, anchor) {
  const matches = text.split('\n').flatMap((value, i) => value.includes(anchor) ? [i + 1] : []);
  assert.equal(matches.length, 1, `source anchor not unique: ${anchor}`);
  return matches[0];
}
const locations = {
  hcProductInput: line(factory, 'var ProductName = MotherboardInfo.Product;'),
  hcFactorySelection: line(factory, 'device = new OneXPlayer3();'),
  hcBoardQuery: line(motherboard, 'SELECT * FROM Win32_BaseBoard'),
  hcBoardMap: line(motherboard, '{ "baseboard", baseboardSearcher }'),
  hcBoardProduct: line(motherboard, 'string Product => Convert.ToString(queryCacheValue("baseboard", "Product"))'),
  hcSystemModel: line(motherboard, 'string SystemModel => Convert.ToString(queryCacheValue("computer", "Model"))'),
  ymccBoardQuery: line(providers, String.raw`using var board=Wmi.First("root\\CIMV2","Win32_BaseBoard")`),
  ymccSystemQuery: line(providers, String.raw`using var system=Wmi.First("root\\CIMV2","Win32_ComputerSystem")`),
  ymccBoardProduct: line(providers, 'Convert.ToString(board["Product"])'),
  ymccSystemModel: line(providers, 'S(system,"Model"),S(cpu,"Name")'),
  ymccDispatch: line(policy, 'capabilityRoutes.Resolve(id.BoardManufacturerRaw??id.Manufacturer,id.BoardProductRaw??id.BoardProduct,id.SystemProduct,id.Processor)'),
};
const makes = ['ONE-NETBOOK', 'ONE-NETBOOK TECHNOLOGY CO., LTD.'];
const aliases = ['ONEXPLAYER 3', 'ONEXPLAYER-3'];
assert.match(factory, /case "ONE-NETBOOK TECHNOLOGY CO\., LTD\.":\s*case "ONE-NETBOOK":\s*\{\s*switch \(ProductName\)/);
assert.match(factory, /case "ONEXPLAYER 3":\s*case "ONEXPLAYER-3":\s*device = new OneXPlayer3\(\);/);
const op3 = battery.rows.filter(row => row.deviceClass === 'OneXPlayer3');
assert.equal(op3.length, 1, 'OP3 rule count changed; re-review');
const [rule] = op3;
assert.equal(rule.provider, 'oxp3-wmi-ec');
assert.equal(rule.firmwareGate.kind, 'provider-probe-required');
assert.deepEqual(rule.identity, { manufacturers: makes, field: 'systemProduct', values: aliases, deviceClass: 'OneXPlayer3' });
assert.deepEqual(routes[rule.index].identity, rule.identity);

const cases = makes.flatMap(manufacturer => aliases.flatMap(label => [
  ['both', label, label, true, true],
  ['baseboard-only', label, 'GENERIC-SYSTEM', true, false],
  ['system-model-only', 'GENERIC-BOARD', label, false, true],
  ['neither', 'GENERIC-BOARD', 'GENERIC-SYSTEM', false, false],
].map(([name, boardProduct, systemProduct, hcSelectsOp3, ymccSelectsOp3]) => ({
  name, label, identity: { manufacturer, boardProduct, systemProduct }, hcSelectsOp3, ymccSelectsOp3,
}))));
const { samples } = auditBatteryRouteIdentity(battery.rows, cases);
const projections = cases.map((sample, i) => {
  const ymccHits = samples[i].hits;
  assert.deepEqual(ymccHits, sample.ymccSelectsOp3 ? [rule.ruleId] : [], sample.name);
  return { ...sample, ymccHits };
});
console.log(JSON.stringify({ schemaVersion: 1, upstreamCommit: 'f2e58468d9917189d4be7ccfc242385ce2c3035b', inputs, locations,
  scope: 'Static source and synthetic identity projection only; no provider, hardware, runtime change or package.',
  hcOp3IdentityField: 'Win32_BaseBoard.Product', ymccOp3IdentityField: 'Win32_ComputerSystem.Model',
  ruleId: rule.ruleId, projections }, null, 2));
