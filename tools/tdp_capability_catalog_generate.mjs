// Targeted generation of the existing TDP consumers from the unified capability library.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {buildTdpArtifacts} from './lib/tdp_capability_catalog.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
if(args.some(a=>a!=='--check'))throw Error('Usage: node tools/tdp_capability_catalog_generate.mjs [--check]');
const before=new Map();
const text=name=>{const value=fs.readFileSync(path.join(repo,name),'utf8');before.set(name,value);return value;};
const check=args.includes('--check'),read=name=>JSON.parse(text(name).replace(/^\uFEFF/,''));
const catalog=read('capabilities/hc-device-catalog.json'),policy=read('capabilities/uxtu-tdp-policy.json');
const tdp=buildTdpArtifacts({catalog,policy,helper:text('PowerControl/pawnio/YeManTdpCtl.py')});
catalog.domains.tdp=tdp.metadata;
const performance=read('capabilities/hc-performance-metadata.json');performance.tdp={...performance.tdp,catalogBinding:'hc-device-catalog.json#/domains/tdp',sharedCapabilityPolicy:'uxtu-tdp-policy.json',takeOverDispatch:false,applySourceLimits:false};
const artifacts={...tdp.artifacts,'capabilities/hc-device-catalog.json':JSON.stringify(catalog,null,2)+'\n','capabilities/hc-performance-metadata.json':JSON.stringify(performance,null,2)+'\n'};
// Reject concurrent source changes before the first output mutation.
if(!check)for(const [name,value] of before)if(fs.readFileSync(path.join(repo,name),'utf8')!==value)throw Error('TDP_GENERATION_INPUT_CHANGED:'+name);
for(const [name,text] of Object.entries(artifacts)){const target=path.join(repo,name);if(check){if(!fs.existsSync(target)||fs.readFileSync(target,'utf8')!==text)throw Error('STALE_TDP_CATALOG:'+name);}else fs.writeFileSync(target,text);}
console.log(JSON.stringify({status:check?'CURRENT':'GENERATED',libraryId:policy.libraryId,version:policy.version,models:catalog.devices.length,artifacts:Object.keys(artifacts),hardwareIo:false,coreSwitchActive:false,voltageSchedulingActive:false}));
