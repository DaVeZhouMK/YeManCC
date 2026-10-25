import {resolveTdpToolchain,tdpToolDeclaration,TDP_SCOPE_SOURCES} from '../tdp-consumer-signals/tool-scope.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/tdp-schedule-consumer';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='performance.tdp-schedule-consumer';
export function moduleSources(){return ['adapter.mjs','run.mjs','vm-io.mjs','spec.json','toolchain-inputs.json','runner.test.mjs'].map(name=>MODULE+'/'+name);}
export function getTdpScheduleConsumerAdapters(repo=REPO){
 const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'spec.json'),'utf8'));
 const tool=resolveTdpToolchain(repo,JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8')));
 return [{id:CASE_ID,domain:'performance',title:'Original CPU/TDP schedule consumers: AC/DC banks, optional TDP failure, G3E isolation and bounded range',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/tdp-schedule'],timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['tdp-schedule/result.json','tdp-schedule/output-evidence.json'],evidenceInputs:[...new Set([...moduleSources(),...tdpToolDeclaration(repo,spec.sourceInputs,tool).sources])],artifactInputs:tdpToolDeclaration(repo,spec.sourceInputs,tool).tools.map(row=>row.path),boundary:'Unchanged full CPU/TDP schedule suite and actual consumers in CJS VM. Explicit default-install namespace maps only into owned scratch; inert IPC, external process/network/user storage denied. No hardware or total OEM catalog acceptance.'}];
}
