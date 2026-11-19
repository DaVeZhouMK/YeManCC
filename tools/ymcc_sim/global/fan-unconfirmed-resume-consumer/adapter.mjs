import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/fan-unconfirmed-resume-consumer',CASE_ID='fan.unconfirmed-resume-consumer',REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','worker.ps1','spec.json','toolchain-inputs.json','runner.test.mjs','leaf-control.ts','README.md'].map(name=>MODULE+'/'+name);}
const launcher={path:'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe',sha256:'ea5596296a0b6f1261c84c7edee40f27879aefc63f005ed541c440b73e382503'};
const windowsPath=value=>path.win32.normalize(value).toLowerCase();
export function resumeTools(repo=REPO){
 const shared=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8'));
 const oldShell=windowsPath(shared.powershellExecutable), selected=windowsPath(launcher.path);
 const matches=shared.files.filter(row=>windowsPath(row.path)===oldShell);
 if(matches.length!==1||oldShell===selected||shared.files.some(row=>windowsPath(row.path)===selected))throw Error('FAN_UNCONFIRMED_SHARED_LAUNCHER_DECLARATION');
 return {...shared,powershellExecutable:launcher.path,files:shared.files.map(row=>windowsPath(row.path)===oldShell?{...launcher}:row)};
}
export function getFanUnconfirmedResumeAdapters(repo=REPO){const read=f=>JSON.parse(fs.readFileSync(f,'utf8')),spec=read(path.join(repo,MODULE,'spec.json')),tool=resumeTools(repo);return [{id:CASE_ID,domain:'fan',title:'Original unconfirmed-handshake public sleep/resume consumers with exact30-row endpoints',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/fan-unconfirmed-resume'],timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['fan-unconfirmed-resume/result.json'],evidenceInputs:[...moduleSources(),...spec.sources],artifactInputs:tool.files.map(row=>row.path),boundary:'Original30 ordered public start/sleep/resume/manual consumers use original NoIoHost and virtual power generations. Actual Node coverage keeps native proc probe0 and memory launcher calls observable; separate real-class leaf control proves captured fake lookup twice. Private handshake fault must compile then hit20 original assertions and5 original production refusals. No native/Host/device/OS sleep or CPU benchmark.'}];}
