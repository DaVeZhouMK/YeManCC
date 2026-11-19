import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/fan-adopted-start-refusal-consumer',CASE_ID='fan.adopted-start-refusal-consumer',REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','worker.ps1','contract.mjs','assert-format.mjs','assert-format.json','contract.test.mjs','spec.json','boundary.json','README.md'].map(file=>MODULE+'/'+file);}
const launcher={path:'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe',sha256:'ea5596296a0b6f1261c84c7edee40f27879aefc63f005ed541c440b73e382503'};
const windowsPath=value=>path.win32.normalize(value).toLowerCase();
export function adoptedTools(repo,spec){
 const shared=JSON.parse(fs.readFileSync(path.join(repo,spec.sharedToolProfile),'utf8'));
 const oldShell=windowsPath(shared.powershellExecutable), selected=windowsPath(launcher.path);
 const matches=shared.files.filter(row=>windowsPath(row.path)===oldShell);
 if(matches.length!==1||oldShell===selected||shared.files.some(row=>windowsPath(row.path)===selected))throw Error('FAN_ADOPTED_SHARED_LAUNCHER_DECLARATION');
 return {...shared,powershellExecutable:launcher.path,files:shared.files.map(row=>windowsPath(row.path)===oldShell?{...launcher}:row)};
}
export function getFanAdoptedStartRefusalAdapters(repo=REPO){const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'spec.json'),'utf8')),tool=adoptedTools(repo,spec);return [{id:CASE_ID,domain:'fan',title:'Actual cold/adopted startup refusal, retained owner/handshake boundaries and cancelled launch identity',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/adopted-start'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['adopted-start/result.json'],evidenceInputs:[...new Set([...moduleSources(),...spec.sources,spec.sharedToolProfile])],artifactInputs:tool.files.map(row=>row.path),boundary:'Unchanged630-line original327 ordered cases; real FanHostLifecycle public start/apply/preset with original memory Host/launcher/power/processProbe. Five family names are fixtures. One private post-launch identity fault must compile then produce only two original625 assertion failures. Reused guard/Job/frozen V8 observer; actual native process findExact/postMessage zero, memory probes separately counted. No real Host/device/HTTP/OS sleep.'}];}
