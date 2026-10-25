import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {MODULE,CASE_ID,validateToolManifest,readJson} from './helper.mjs';
export {MODULE,CASE_ID};
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const runtimeMembers=['adapter.mjs','Run-Radial.ps1','run.mjs','prepare.mjs','helper.mjs','plan.json','extraction-contract.json','toolchain-inputs.json','MemoryPorts.hpp','ConsumerCases.hpp'];
export const mainReturnMembers=['main-return/prepare.mjs','main-return/run.mjs','main-return/contract.json','main-return/plan.json','main-return/MainPlatform.hpp','main-return/MainEndpoints.hpp','main-return/MainCases.hpp'];
export function toolchainSpec(repo=REPO){const manifest=readJson(path.join(repo,MODULE,'toolchain-inputs.json'));validateToolManifest(manifest);return manifest;}
export function inputFiles(repo=REPO){const contract=readJson(path.join(repo,MODULE,'extraction-contract.json')),mainContract=readJson(path.join(repo,MODULE,'main-return/contract.json'));return [...new Set([...runtimeMembers.map(name=>MODULE+'/'+name),...mainReturnMembers.map(name=>MODULE+'/'+name),...contract.productSources,...mainContract.productSources.map(row=>row.path),...contract.sharedSources.map(row=>row.path)])];}
export function getRadialNativeConsumerAdapters(repo=REPO){
 const tool=toolchainSpec(repo);
 return [{id:CASE_ID,domain:'controller',title:'Mandatory native radial post-create consumer plus actual main owner, neutral return and queued-action admission through memory IO',kind:'powershell',path:MODULE+'/Run-Radial.ps1',args:['-EvidenceDir','{outDir}/radial-native','-NodePath',tool.executables.node.path],timeoutMs:480000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['radial-native/result.json','radial-native/post-create-result.json','radial-native/main-return/result.json','radial-native/wrapper-terminal.json'],completionContract:{schemaVersion:1,rules:[{line:'RADIAL_NATIVE_CONSUMER_V2_COMPLETED postCreate=12+2 mainReturn=12+2'}]},evidenceInputs:inputFiles(repo),artifactInputs:tool.files.map(row=>row.path),boundary:'V2 mandatory post-create13-definition stage retains original12+2;mandatory main-return stage extracts30 complete definitions and54 declarations from current approved main/radial/region/JSON snapshots and executes main12+2.Each stage faults require its own this-run original12;compile0,natural23,exact assertions,output completeness,cleanup0.Existing1191 tools/4 complete directories remain.Creation,real OS/device IO,full parent navigation/OEM,actual concurrency beyond a whole radial lifecycle,TS host/action/TDP remain open.No historical runtime dependency or physical acceptance.'}];
}

