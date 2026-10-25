import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const fixture=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-battery-contract-'));
for(const dir of ['tools','native/BatteryHost','capabilities'])fs.mkdirSync(path.join(fixture,dir),{recursive:true});
for(const name of fs.readdirSync(path.join(root,'native/BatteryHost')).filter(n=>/\.(cs|csproj|json)$/.test(n)))fs.copyFileSync(path.join(root,'native/BatteryHost',name),path.join(fixture,'native/BatteryHost',name));
for(const name of ['native/battery_charge_client.h','native/secondary_display_client.h','native/battery_host_contract.h','tools/battery-charge-contract.mjs',"capabilities/HcDeviceCapabilityCatalog.cs","capabilities/HcDeviceCapabilityCatalog.generated.cs","capabilities/HcBatteryCapabilityCatalog.cs","capabilities/HcBatteryCapabilityCatalog.generated.cs","native/ProcessEfficiency.cs"])fs.copyFileSync(path.join(root,name),path.join(fixture,name));
const project=fs.readFileSync(path.join(root,'native/BatteryHost/YeManBatteryHost.csproj'),'utf8');
for(const match of project.matchAll(/<Compile\s+Include="([^"]+)"/g)){
 const relative=path.relative(root,path.resolve(root,'native/BatteryHost',match[1].replaceAll('\\','/')));
 const destination=path.join(fixture,relative);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.copyFileSync(path.join(root,relative),destination);
}
const run=()=>spawnSync(process.execPath,[path.join(fixture,'tools/battery-charge-contract.mjs'),'--check'],{encoding:'utf8'});
assert.equal(run().status,0);
for(const name of ['native/BatteryHost/MsiApProvider.cs','native/BatteryHost/battery-routes.json','native/battery_charge_client.h','native/secondary_display_client.h','capabilities/HcBatteryCapabilityCatalog.generated.cs']){
 const file=path.join(fixture,name),original=fs.readFileSync(file);
 fs.appendFileSync(file,'\n');assert.notEqual(run().status,0,`stale implementation accepted: ${name}`);fs.writeFileSync(file,original);
}
assert.equal(run().status,0);
console.log('BATTERY_STALE_CONTRACT_REJECTED: provider, route table and native client changes require regeneration; live sources untouched.');
