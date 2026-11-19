import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {isDeepStrictEqual} from 'node:util';
import {createHash} from 'node:crypto';
import {buildBatteryArtifacts} from './lib/hc_battery_catalog.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
if(args.length>1||(args.length===1&&!['--check','--refresh'].includes(args[0])))throw Error('Usage: node tools/battery-charge-check-binder.mjs [--check | --refresh]');
const refresh=args[0]==='--refresh';
const catalogPath=path.join(repo,'capabilities/hc-device-catalog.json');
const frozenIdentityFile='capabilities/HcDeviceCapabilityCatalog.generated.cs';
const binderFile='capabilities/HcBatteryCapabilityCatalog.generated.cs';
const binderPath=path.join(repo,binderFile);
const read=file=>fs.readFileSync(file,'utf8');
const print=value=>JSON.stringify(value,null,2)+'\n';
const catalogText=read(catalogPath),binderText=read(binderPath);
const catalog=JSON.parse(catalogText);
function requireFrozenBatteryFacts(catalog){
 // The scoped path consumes cached device declarations. Bind the battery
 // projection to the reviewed C# table without pinning any other domain.
 const mismatch=()=>{throw Error('BATTERY_FROZEN_DEVICE_FACTS_MISMATCH: verify the frozen HC device inventory before regenerating the battery domain');};
 if(catalog?.sourceBaseline!=='HC-BINARY-BATCH12-20260831'||
    catalog.sourceFilesIndexSha256!=='9105b05655c280f99ffffdb214c165955ced5ef3463719d681e7abecf7e904d1'||
    !Array.isArray(catalog.devices))mismatch();
 const facts=catalog.devices.map(d=>{
  const flags=d?.declared?.Capabilities?.possible;
  if(!Array.isArray(flags)||flags.some(x=>typeof x!=='string'))mismatch();
  return [d.deviceId,d.name,d.factoryInstantiable,d.sourcePath,d.sourceSha256,flags.filter(x=>x.startsWith('Battery')).sort()];
 }).sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
 if(createHash('sha256').update(JSON.stringify(facts)).digest('hex')!==
    '288465062ec42790a218dc5e9b18a26c309cfefdb521fcd324dcfe2d78dfc908')mismatch();
}
requireFrozenBatteryFacts(catalog);
// Bind<T> resolves every frozen rule through the shared generated identity and alias tables.
function requireFrozenBindIdentitySource(){
 const mismatch=error=>new Error(`BATTERY_FROZEN_BIND_IDENTITY_SOURCE_MISMATCH: verify ${frozenIdentityFile} against the reviewed frozen HC catalog`,{cause:error});
 let source;
 try{source=read(path.join(repo,frozenIdentityFile)).replaceAll('\r\n','\n');}catch(error){throw mismatch(error);}
 if(createHash('sha256').update(source).digest('hex')!==
    '718d9b0340bec0ff81d4ebff8e1d96e3724d2a362b8169ca72788bf92002b674')throw mismatch();
}
requireFrozenBindIdentitySource();
const {metadata,artifacts}=buildBatteryArtifacts({repo,catalog});
const binder=artifacts[binderFile];
const binderStale=binderText.replaceAll('\r\n','\n')!==binder;
const catalogStale=!isDeepStrictEqual(catalog.domains?.battery,metadata);
const remedy='run node tools/battery-charge-check-binder.mjs --refresh on the unfrozen capture before deriving component contracts';
if(!refresh){
 if(binderStale)throw Error(`BATTERY_GENERATED_BINDER_STALE: ${binderFile}; ${remedy}`);
 if(catalogStale)throw Error(`BATTERY_GENERATED_CATALOG_STALE: capabilities/hc-device-catalog.json:domains.battery; ${remedy}`);
}else if(binderStale||catalogStale){
 // Refuse to reserialize another domain's edits or normalize its formatting.
 if(!catalog.domains||catalogText!==print(catalog))throw Error('BATTERY_REFRESH_REQUIRES_CANONICAL_DEVICE_CATALOG');
 if(read(catalogPath)!==catalogText||read(binderPath)!==binderText)throw Error('CONCURRENT_BATTERY_GENERATED_FILE_CHANGE');
 requireFrozenBindIdentitySource();
 if(catalogStale)fs.writeFileSync(catalogPath,print({...catalog,domains:{...catalog.domains,battery:metadata}}));
 if(binderStale)fs.writeFileSync(binderPath,binder);
}
const status=refresh&&(binderStale||catalogStale)?'BATTERY_GENERATED_BINDER_REFRESHED':'BATTERY_GENERATED_BINDER_OK';
console.log(`${status}: ${metadata.registeredRules} rules; resource=${metadata.ruleDocumentSha256}`);
