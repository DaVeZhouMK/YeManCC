import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.dirname(fileURLToPath(import.meta.url));
const [dllFile,headerFile,receiptFile,dispatchLibrary,...rest]=process.argv.slice(2);
if(!dllFile||!headerFile||!receiptFile||!dispatchLibrary||rest.some(x=>x!=='--verify'))throw Error('usage: embed-trial.mjs <dll> <header> <receipt> <dispatch-static-lib> [--verify]');
const verify=rest.includes('--verify');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const image=fs.readFileSync(dllFile);
function need(value,reason){if(!value)throw Error(reason);}
function u16(at){need(at>=0&&at+2<=image.length,'PE u16 bounds');return image.readUInt16LE(at);}
function u32(at){need(at>=0&&at+4<=image.length,'PE u32 bounds');return image.readUInt32LE(at);}
need(image.length>4096&&u16(0)===0x5a4d,'DLL DOS header');
const pe=u32(0x3c),sections=u16(pe+6),optional=pe+24;
need(u32(pe)===0x4550&&u16(pe+4)===0x8664&&u16(optional)===0x20b&&(u16(pe+22)&0x2000)!==0,'AMD64 PE32+ DLL required');
need((u16(optional+70)&0x4140)===0x4140,'ASLR, NX and CFG required');
const sectionTable=optional+u16(pe+20);
function offset(rva){for(let i=0;i<sections;i++){const s=sectionTable+i*40,va=u32(s+12),size=u32(s+16),file=u32(s+20);if(rva>=va&&rva-va<size){const at=file+rva-va;need(at<image.length,'PE section bounds');return at;}}throw Error('PE RVA unavailable');}
const ex=offset(u32(optional+112)),exportNames=u32(ex+24),names=offset(u32(ex+32));
let control=false;
for(let i=0;i<exportNames;i++){const at=offset(u32(names+4*i)),end=image.indexOf(0,at);need(end>=at&&end-at<256,'bounded export name');if(image.toString('ascii',at,end)==='RunningTrialControl')control=true;}
need(control,'RunningTrialControl export missing');
const abiHeader=fs.readFileSync(path.join(root,'running-publish-trial.h'),'utf8');
need(/inline constexpr std::uint32_t abi=3;/.test(abiHeader)&&/QualificationRoute qualificationRoute=/.test(abiHeader)&&/steamPath\[520\]/.test(abiHeader),'shared packet ABI3 with explicit qualification route required');
need(/kAbiVersion\s*=\s*1;/.test(fs.readFileSync(path.join(root,'one_shot_dispatch.h'),'utf8')),'unchanged dispatcher ABI1 required');
const digest=hash(image),lines=[];
for(let i=0;i<image.length;i+=32)lines.push([...image.subarray(i,i+32)].map(x=>'0x'+x.toString(16).padStart(2,'0')).join(',')+',');
const header='#pragma once\nnamespace ymcc::steamOriginalRecovery::embedded {\ninline constexpr unsigned trialAbi=3;\ninline constexpr char sha256[]="'+digest+'";\ninline constexpr unsigned char image[]={\n'+lines.join('\n')+'\n};\n}\n';
const inputs=['running-publish-trial.cpp','running-publish-trial.h','driver_port.inc','one_shot_dispatch.h','vendor/steam_native_app_state_win32.h','vendor/steam_native_app_state_diagnostic.h','vendor/steam_native_record_win32.h','vendor/steam_native_record_diagnostic.h','vendor/json.hpp','../steam_compatibility_core.h','../steam_compatibility_graph_core.h','../steam_compatibility_publish_contracts.h','../steam_compatibility_publish_graph.h','../steam_compatibility_read_contracts.h','../steam_compatibility_read_graph.h','../steam_compatibility_win32.h','../steam_import_target_win32.h','Build-Trial.cmd','embed-trial.mjs'];
const sourcePins=Object.fromEntries(inputs.map(file=>[file,hash(fs.readFileSync(path.join(root,file)))]));
if(verify){need(fs.readFileSync(headerFile,'utf8')===header,'embedded header differs from compiled DLL');const prior=JSON.parse(fs.readFileSync(receiptFile,'utf8'));need(prior.sha256===digest&&JSON.stringify(prior.sourcePins)===JSON.stringify(sourcePins),'build receipt stale');console.log(JSON.stringify({verified:true,abi:3,bytes:image.length,sha256:digest}));}
else{if(!fs.existsSync(headerFile)||fs.readFileSync(headerFile,'utf8')!==header)fs.writeFileSync(headerFile,header);const receipt={schema:'ymcc.generic-trial-build.v1',abi:3,dispatcherAbi:1,bytes:image.length,sha256:digest,dll:path.resolve(dllFile),embeddedHeader:path.resolve(headerFile),embeddedHeaderSha256:hash(Buffer.from(header)),dispatchLibrarySha256:hash(fs.readFileSync(dispatchLibrary)),sourcePins,validation:'PE/export/hash only; DLL not loaded or executed',steamProcessOpened:false,debuggerAttached:false,liveRecoveryVerified:false,extraProductExecutables:0};fs.writeFileSync(receiptFile,JSON.stringify(receipt,null,2)+'\n');console.log(JSON.stringify({built:true,abi:3,bytes:image.length,sha256:digest}));}
