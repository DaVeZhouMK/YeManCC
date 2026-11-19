import assert from 'node:assert/strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { auditOneXConsoleBattery as audit } from './lib/battery_onex_oem_audit.mjs';

const source = process.argv[2];
if (!source) throw new Error('usage: node tools/battery-oem-coverage-selftest.mjs <pinned background.js>');
const catalog = JSON.parse(fs.readFileSync(new URL('../capabilities/hc-device-catalog.json', import.meta.url), 'utf8'));
const rows = catalog.domains.battery.rows;
const current = audit(source, rows);
const beforeX2 = audit(source, rows.filter(row => row.deviceClass !== 'OneXPlayerX2MiniPro'));
const find = (result, product) => result.offerings.find(o => o.product === product);
const missing = 'vendor-enabled-literal-without-runtime-match';

assert.equal(current.branches.length, 29);
assert.equal(current.offerings.length, 39);
assert.equal(find(beforeX2, 'ONEXPLAYER X2Mini PRO').status, missing);
const x2Route = rows.find(r => r.deviceClass === 'OneXPlayerX2MiniPro');
if (x2Route) assert.equal(find(current, 'ONEXPLAYER X2Mini PRO').matchingLiterals[0].class, x2Route.deviceClass);
assert.deepEqual(current.branches.find(b => b.exactProducts.includes('ONEXPLAYER X2')).exactProducts,
  ['ONEXPLAYER X2', 'ONEXPLAYER X2 EVA', 'ONEXPLAYER X2 EVA-02']);
assert.equal(find(current, 'AOKZOE A1X').manufacturer, 'AOKZOE');
assert.equal(find(current, 'AOKZOE A2 Ultra'), undefined);
assert.ok(find(current, 'ONEXPLAYER F1').firmwareConditions.some(c => c.includes('EcMinorVersion>=30')));
assert.ok(find(current, 'ONEXPLAYER X1 A').firmwareConditions.some(c => c.includes('BIOSVersion>=1.06')));
assert.ok(find(current, 'ONEXPLAYER X1 mini').firmwareConditions.some(c => c.includes('BIOSVersion>=1.04')));
assert.throws(() => audit(fileURLToPath(import.meta.url), rows), /SOURCE_MISMATCH/);
console.log('OEM coverage audit: 29 branches / 39 labels; missing-X2 replay, guard/alias separation and wrong-source rejection passed.');
