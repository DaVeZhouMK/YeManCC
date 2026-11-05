// Regenerate the Windows runtime allowlist from the reviewed, immutable research catalogue.
// Linux-only transport and unverified SuiPlay inheritance remain research candidates.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import {validateSuperXChargeOverlay} from './lib/hc_battery_catalog.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const catalog=JSON.parse(fs.readFileSync(path.join(root,'docs/battery-charge-capabilities.json'),'utf8'));
const excluded=new Set(['oxp-linux-platform','suiplay-cec-inheritance']);
const routes=catalog.routes.filter(r=>!excluded.has(r.id)).flatMap(r=>r.exactIdentities.map(identity=>({route:r.id,family:r.family,kind:r.kind,identity,firmwareGate:r.firmwareGate,min:r.id==='msi-claw-wmi'?20:r.id==='gpd-win-mini-2025'?60:40})));
// The original catalogue remains an immutable research snapshot. This separately
// reviewed CG3EM protocol supersedes its inherited Get_Data(215) assumption.
const msiAp=JSON.parse(fs.readFileSync(path.join(root,'native/BatteryHost/msi-ap-route-review.json'),'utf8'));
assert.equal(msiAp.schema,1);assert.deepEqual(msiAp.boardProducts,['MS-1T91']);
assert.equal(msiAp.selector,0);assert.equal(msiAp.valueByte,5);assert.equal(msiAp.packetBytes,32);assert.equal(msiAp.min,20);
let replaced=0;
for(const route of routes)if(route.route===msiAp.baseRoute&&route.identity.field==='boardProduct'&&route.identity.values.some(v=>msiAp.boardProducts.includes(v))){
 assert.ok(route.identity.values.every(v=>msiAp.boardProducts.includes(v)),'Mixed MSI identity group must be split before overriding');
 route.route=msiAp.route;route.min=msiAp.min;replaced++;
}
assert.equal(replaced,1,'CG3EM override must match exactly one identity group');
// Fixed-source OXP3 WMI EC route. The OneXPlayerX2 parent supplies the WMI EC transport
// (root\WMI SuRwECRegInterface, ReadECReg/WriteECReg); OneXPlayer3 overrides only the
// battery limit (0x4A3) and bypass mode (0x4A4) writes. The old research catalogue stays
// byte-identical; this reviewed overlay is the only OXP3 runtime admission input.
const oxp3=JSON.parse(fs.readFileSync(path.join(root,'native/BatteryHost/oxp3-battery-route.json'),'utf8'));
assert.equal(oxp3.schema,1);assert.equal(oxp3.route,'oxp3-wmi-ec');assert.equal(oxp3.kind,'firmware-percent');assert.equal(oxp3.min,40);
assert.deepEqual(oxp3.identity.manufacturers,['ONE-NETBOOK','ONE-NETBOOK TECHNOLOGY CO., LTD.']);assert.equal(oxp3.identity.field,'systemProduct');
assert.deepEqual(oxp3.identity.values,['ONEXPLAYER 3','ONEXPLAYER-3']);assert.equal(oxp3.identity.deviceClass,'OneXPlayer3');
assert.equal(oxp3.protocol.limitRegister,'0x04A3');assert.equal(oxp3.protocol.bypassRegister,'0x04A4');assert.equal(oxp3.limits.min,0);assert.equal(oxp3.limits.max,100);
assert.ok(!routes.some(r=>r.route===oxp3.route),'OXP3 overlay route must not duplicate research routes');
routes.push({route:oxp3.route,family:oxp3.family,kind:oxp3.kind,identity:oxp3.identity,firmwareGate:oxp3.firmwareGate,min:oxp3.min});
assert.equal(routes.filter(r=>r.route===oxp3.route).length,1,'OXP3 overlay must add exactly one identity group');
// Super X is one separately reviewed, source-bound dual-transport charge route.
// Preserve all old rows verbatim: no normalized alias or CPU predicate is added.
for(const item of routes){
 assert.ok(item.identity.manufacturerMatch===undefined||item.identity.manufacturerMatch==='normalized-exact','Unreviewed battery manufacturer predicate');
 assert.ok(item.identity.valueMatch===undefined||item.identity.valueMatch==='normalized-exact','Unreviewed battery value predicate');
 assert.notEqual(item.identity.deviceClass,'OneXPlayerSuperX','Super X must be a reviewed extension, never a research/frozen alias');
}
const superx=validateSuperXChargeOverlay({repo:root});
assert.ok(!routes.some(r=>r.route===superx.route),'Super X overlay must not duplicate another route');
routes.push({route:superx.route,family:superx.family,kind:superx.kind,identity:superx.identity,firmwareGate:superx.firmwareGate,min:superx.min});
assert.equal(routes.filter(r=>r.route===superx.route).length,1,'Super X overlay must add exactly one source-bound identity group');
const target=path.join(root,'native/BatteryHost/battery-routes.json');
if(process.argv.includes('--check'))assert.deepEqual(JSON.parse(fs.readFileSync(target,'utf8')),routes,'Runtime allowlist differs from reviewed catalogue');
else fs.writeFileSync(target,JSON.stringify(routes,null,2)+'\n');
console.log(`${routes.length} reviewed identity groups, ${new Set(routes.map(r=>r.route)).size} Windows routes; Linux/SuiPlay candidates remain disabled.`);
