import {resolveTdpToolchain,tdpToolDeclaration,TDP_SCOPE_SOURCES} from './tool-scope.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/tdp-consumer-signals';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='performance.tdp-frontend-consumer';
export function moduleSources(){return ['adapter.mjs','run.mjs','vm-io.mjs','spec.json','toolchain-inputs.json','runner.test.mjs'].map(name=>MODULE+'/'+name);}
export function getTdpConsumerAdapters(repo=REPO){
 const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'spec.json'),'utf8'));
 const tool=resolveTdpToolchain(repo,JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8')));
 return [{id:CASE_ID,domain:'performance',title:'Original setTdp frontend consumers: capability, lifecycle, CLI/daemon/MSI receipts and causal feedback',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/tdp-consumer'],timeoutMs:60000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['tdp-consumer/result.json','tdp-consumer/output-evidence.json'],evidenceInputs:[...new Set([...moduleSources(),...tdpToolDeclaration(repo,spec.sourceInputs,tool).sources])],artifactInputs:tdpToolDeclaration(repo,spec.sourceInputs,tool).tools.map(row=>row.path),boundary:'Unchanged original TS assertions and actual yeman/diagnostic/runtime source in CJS VM. Scratch FS and inert IPC; external process, network and user storage denied. No OEM total-catalog admission or physical power claim.'}];
}
