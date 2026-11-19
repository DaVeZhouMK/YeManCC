import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import ts from 'typescript';
import { auditOneXConsoleBattery } from './lib/battery_onex_oem_audit.mjs';
import { assertBatteryAuditRouteRows } from './lib/battery_route_identity_audit.mjs';

const [sourceFile, output] = process.argv.slice(2);
assert.equal(process.argv.length, 4,
  'usage: node tools/battery-onex-oem-manufacturer-audit.mjs <pinned background.js> <new report.json>');
const readRepo = file => fs.readFileSync(new URL('../' + file, import.meta.url));
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const routeBytes = readRepo('native/BatteryHost/battery-routes.json');
const catalogBytes = readRepo('capabilities/hc-device-catalog.json');
const battery = JSON.parse(catalogBytes).domains.battery;
assertBatteryAuditRouteRows(battery, routeBytes);
const oem = auditOneXConsoleBattery(sourceFile, battery.rows);
const bytes = fs.readFileSync(sourceFile);
assert.equal(sha(bytes), oem.sourceSha256, 'OEM precedence source differs from branch inventory');
const sf = ts.createSourceFile('background.js', bytes.toString('utf8'), ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
assert.equal(sf.parseDiagnostics.length, 0);

const short = 'ONE-NETBOOK';
const full = 'ONE-NETBOOK TECHNOLOGY CO., LTD.';
const contains = 'oe.Manufacturer&&-1!=oe.Manufacturer.indexOf("ONE-NETBOOK")';
const priorAliases = [
  'AOKZOE',
  'Rona',
  full,
  'IP3 Technology CO.,Ltd.',
];
const offsets = new Set(oem.branches.filter(b => b.manufacturer === short).map(b => b.offset));
const guardLocations = new Map();
function inspect(node) {
  if (ts.isBinaryExpression(node) && offsets.has(node.getStart(sf)) &&
      node.left.getText(sf) === 'q.supportFunc.enableBatteryProtection') {
    const guards = [];
    for (let child = node, p = child.parent; p; child = p, p = p.parent) {
      if (ts.isFunctionExpression(p)) break;
      if (!ts.isIfStatement(p)) continue;
      const expression = p.expression.getText(sf);
      if (!expression.includes('oe.Manufacturer')) continue;
      guards.push({ node: p, expression, arm: child === p.thenStatement ? 'then' : child === p.elseStatement ? 'else' : 'expression' });
    }
    assert.deepEqual(guards.map(({ expression, arm }) => [expression, arm]), [
      [contains, 'then'], ...priorAliases.map(alias => [`${JSON.stringify(alias)}==oe.Manufacturer`, 'else']),
    ], 'OEM manufacturer precedence requires review');
    const legacy = guards.find(g => g.expression === `${JSON.stringify(full)}==oe.Manufacturer`).node;
    assert.ok(ts.isBlock(legacy.thenStatement), 'OEM full-name legacy block requires review');
    const [early] = legacy.thenStatement.statements;
    assert.ok(early && ts.isIfStatement(early) && ts.isReturnStatement(early.thenStatement));
    const check = early.expression;
    assert.ok(ts.isBinaryExpression(check) && check.operatorToken.kind === ts.SyntaxKind.CommaToken &&
      check.right.getText(sf) === '"ONE XPLAYER"!=oe.Product' && early.thenStatement.expression?.getText(sf) === '!1',
    'OEM full-name legacy product rejection requires review');
    guardLocations.set(node.getStart(sf), { containsOffset: guards[0].node.getStart(sf),
      fullNameOffset: legacy.getStart(sf), legacyReturnOffset: early.thenStatement.getStart(sf) });
  }
  ts.forEachChild(node, inspect);
}
const first = Math.min(...offsets), last = Math.max(...offsets);
function bounded(node) {
  if (node.end < first || node.pos > last) return;
  if (ts.isFunctionExpression(node) && node.getStart(sf) < first && node.end > last) inspect(node);
  else ts.forEachChild(node, bounded);
}
bounded(sf);
assert.deepEqual(new Set(guardLocations.keys()), offsets, 'missing reviewed OneX battery branch');

const locations = [...guardLocations.values()];
assert.ok(locations.length > 0 && locations.every(l => JSON.stringify(l) === JSON.stringify(locations[0])),
  'OneX battery branches do not share the same manufacturer guards');
const manufacturerGate = (make, product) => make === full && product !== 'ONE XPLAYER'
  ? 'returns-false-in-earlier-exact-manufacturer-branch'
  : make === full || priorAliases.includes(make) || !make.includes(short)
    ? 'does-not-enter-reviewed-onex-battery-branch'
    : 'not-excluded-by-manufacturer-only';
const equal = (a, b) => a.trim().toUpperCase() === b.trim().toUpperCase();
const offerings = oem.offerings.filter(o => o.manufacturer === short).map(o => {
  assert.notEqual(o.product, 'ONE XPLAYER');
  return { product: o.product, featureOffset: o.offset, firmwareConditions: o.firmwareConditions,
    oemInputProjection: [short, full].map(value => ({ value, gate: manufacturerGate(value, o.product) })),
    literalLibraryMatches: o.matchingLiterals.map(({ ruleId }) => {
      const row = battery.rows.find(r => r.ruleId === ruleId);
      const { manufacturers, manufacturerMatch } = row.identity;
      return { ruleId, model: row.deviceClass, ymccIdentityField: row.identity.field,
        candidates: [...new Set([...manufacturers, short, full])].map(value => ({ value,
          matchesLibraryManufacturerPredicate: manufacturers.some(literal => manufacturerMatch === 'ordinal-contains'
            ? value.includes(literal) : equal(value, literal)),
          oemGateForSameLiteral: manufacturerGate(value, o.product),
        })) };
    }) };
});
const report = { schemaVersion: 1,
  scope: 'Static literal input to the fixed OneXConsole JS branch only. Does not equate raw SMBIOS with getSimpleDeviceInfo(), assert firmware or native transaction success, or modify runtime admission.',
  oemSourceSha256: oem.sourceSha256, routeSha256: sha(routeBytes), catalogFileSha256: sha(catalogBytes),
  inputOffsetUnits: 'UTF-16 code units', guardLocations: locations[0],
  oneXProductCount: offerings.length, oneXBranchCount: locations.length,
  libraryMatchedProductCount: offerings.filter(o => o.literalLibraryMatches.length).length,
  unmatchedProductCount: offerings.filter(o => !o.literalLibraryMatches.length).length,
  offerings };
fs.writeFileSync(output, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
console.log(`${report.oneXBranchCount} OneX branches, ${report.oneXProductCount} exact product labels; ${report.libraryMatchedProductCount} have a literal library match.`);
