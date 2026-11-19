import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/fan-pending-acquisition-consumer',CASE_ID='fan.pending-acquisition-consumer',REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','worker.ps1','spec.json','toolchain-inputs.json','runner.test.mjs','README.md'].map(name=>MODULE+'/'+name);}
const launcher={path:'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe',sha256:'ea5596296a0b6f1261c84c7edee40f27879aefc63f005ed541c440b73e382503'};
const windowsPath=value=>path.win32.normalize(value).toLowerCase();
export function pendingTools(repo=REPO){
 const shared=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8'));
 const oldShell=windowsPath(shared.powershellExecutable), selected=windowsPath(launcher.path);
 const matches=shared.files.filter(row=>windowsPath(row.path)===oldShell);
 if(matches.length!==1||oldShell===selected||shared.files.some(row=>windowsPath(row.path)===selected))throw Error('FAN_PENDING_SHARED_LAUNCHER_DECLARATION');
 return {...shared,powershellExecutable:launcher.path,files:shared.files.map(row=>windowsPath(row.path)===oldShell?{...launcher}:row)};
}
export function getFanPendingAcquisitionAdapters(repo=REPO){const read=f=>JSON.parse(fs.readFileSync(f,'utf8')),spec=read(path.join(repo,MODULE,'spec.json')),tool=pendingTools(repo);return [{id:CASE_ID,domain:'fan',title:'Original pending lease acquisition/control/OFF/Close public consumers with exact44-row endpoints',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/fan-pending-acquisition'],timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['fan-pending-acquisition/result.json'],evidenceInputs:[...moduleSources(),...spec.sources],artifactInputs:tool.files.map(row=>row.path),boundary:'Original44 ordered family/name rows execute real FanHostLifecycle public apply/preset/disable/close with original fake Host and virtual clock. Private power-boundary and cleanup-receipt faults require full44-row reports and exact original failures. No main/native/Host/device/CPU benchmark; compiler imports are provenance, not whole-module coverage.'}];}
