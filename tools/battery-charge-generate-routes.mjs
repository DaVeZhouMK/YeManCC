// Regenerate the Windows runtime allowlist from the reviewed, immutable research catalogue.
// Linux-only transport and unverified SuiPlay inheritance remain research candidates.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
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
const target=path.join(root,'native/BatteryHost/battery-routes.json');
if(process.argv.includes('--check'))assert.deepEqual(JSON.parse(fs.readFileSync(target,'utf8')),routes,'Runtime allowlist differs from reviewed catalogue');
else fs.writeFileSync(target,JSON.stringify(routes,null,2)+'\n');
console.log(`${routes.length} exact identity groups, ${new Set(routes.map(r=>r.route)).size} Windows routes; Linux/SuiPlay candidates remain disabled.`);
