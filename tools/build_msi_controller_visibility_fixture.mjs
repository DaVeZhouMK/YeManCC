import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const [logRoot,output]=process.argv.slice(2);if(!logRoot||!output)throw Error('Usage: log-root output.json');
const input=path.join(logRoot,'virtual-gamepad.log');const raw=fs.readFileSync(input);const rows=raw.toString('utf8').split(/\r?\n/).flatMap(line=>{try{return [JSON.parse(line)]}catch{return []}});
const receipt=rows.find(r=>r.event==='msi-dinput-enumeration'&&r.candidates?.some(c=>c.selectedMain));
if(!receipt)throw Error('No actual DirectInput metadata receipt');
const main=receipt.candidates.find(c=>c.selectedMain);
const shadow=rows.find(r=>r.event==='hidhide.msi-source-shadow'&&r.interfacePath?.toLowerCase()===main.interfacePath.toLowerCase()&&r.recordUsbInstance);
const snap=rows.find(r=>r.event==='hidhide.visibility-snapshot'&&r.targetInventory?.some(x=>x.interfacePath?.toLowerCase()===main.interfacePath.toLowerCase()));
if(!shadow||!snap)throw Error('No matching managed-source inventory');
const candidates=receipt.candidates.map(c=>{
 const inventory=snap.targetInventory.find(x=>x.interfacePath?.toLowerCase()===c.interfacePath.toLowerCase());
 if(!inventory)throw Error('DirectInput candidate has no inventory identity');
 if(inventory.vid!==c.vendorId||inventory.pid!==c.productId||inventory.usagePage!==c.hidUsagePage||inventory.usage!==c.hidUsage)throw Error('Cross-channel identity/caps conflict');
 return {...c,instanceId:inventory.instance,hidAttributesKnown:true,hidVid:inventory.vid,hidPid:inventory.pid,hidRole:inventory.role,
   inventorySourceUsb:shadow.recordUsbInstance,pnpLive:'unobserved; synthesized only in ownership policy unit tests'};
});
const steamFile=path.join(logRoot,'steam-controller.log');const steam=fs.readFileSync(steamFile,'utf8');const matches=[...steam.matchAll(/\[([^\]]+)\] Local Device Found\s+type: 0db0 1902\s+path: (sdl:\/\/\d+)[\s\S]*?Product:\s+([^\r\n]+)/g)].filter(m=>m[1]>='2026-10-11 17:16:19');
const fixture={sourceFile:input,sourceSha256:createHash('sha256').update(raw).digest('hex'),observedAt:receipt.time,generation:shadow.generation,
 mainPath:main.interfacePath,mainUsb:shadow.recordUsbInstance,candidates,originalPhysicalTargets:snap.physicalTargets,
 steamEvidence:matches.slice(-8).map(m=>({at:m[1],path:m[2],product:m[3]})),externalSteamInterfaceMapping:'unknown; SDL aliases are not mapped by candidate count',
 hardwareVerified:false,scope:'Actual metadata replay. Live PnP and journal ownership used by unit tests are explicit synthetic conditions, not field proof.'};
fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(fixture,null,2)+'\n');console.log('MSI_CONTROLLER_VISIBILITY_FIXTURE_OK candidates='+candidates.length+' steamRecords='+matches.length);
