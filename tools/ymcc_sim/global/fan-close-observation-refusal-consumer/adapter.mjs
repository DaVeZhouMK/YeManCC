import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import assert from 'node:assert/strict';
export const MODULE='tools/ymcc_sim/global/fan-close-observation-refusal-consumer',CASE_ID='fan.close-observation-refusal-consumer',REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const toolKey=file=>path.resolve(file).toLowerCase();
export function resolveCloseToolProfile(base,change){
 const {previous,replacement}=change??{};
 assert([previous,replacement].every(row=>typeof row?.path==='string'&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256)),'FAN_CLOSE_POWERSHELL_PIN_REQUIRED');
 const before=toolKey(previous.path),after=toolKey(replacement.path);
 assert(before!==after&&typeof base.powershellExecutable==='string'&&toolKey(base.powershellExecutable)===before,'FAN_CLOSE_SHARED_POWERSHELL_PREIMAGE');
 const hits=base.files.filter(row=>toolKey(row.path)===before);
 assert(hits.length===1&&hits[0].sha256===previous.sha256&&!base.files.some(row=>toolKey(row.path)===after),'FAN_CLOSE_POWERSHELL_FILE_PREIMAGE');
 return {...base,powershellExecutable:replacement.path,files:base.files.map(row=>toolKey(row.path)===before?{...row,...replacement}:row)};
}
export function readCloseToolProfile(repo,spec){return resolveCloseToolProfile(read(path.join(repo,spec.sharedToolProfile)),spec.closePowerShell);}
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','worker.ps1','contract.mjs','assert-format.mjs','assert-format.json','contract.test.mjs','spec.json','boundary.json','README.md'].map(file=>MODULE+'/'+file);}
export function getFanCloseObservationRefusalAdapters(repo=REPO){const spec=read(path.join(repo,MODULE,'spec.json')),tool=readCloseToolProfile(repo,spec);return [{id:CASE_ID,domain:'fan',title:'Actual public Close/start/apply refusal observation, retained exact owner and corrected manual reentry',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/close-observation'],timeoutMs:120000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['close-observation/result.json'],evidenceInputs:[...new Set([...moduleSources(),...spec.sources,spec.sharedToolProfile])],artifactInputs:tool.files.map(row=>row.path),boundary:'Original44 ordered(model,name) cases, real FanHostLifecycle public Close/start/apply, original virtual Clock/memory Host/inert exact-exit launcher. One private entry-refusal guard fault must compile and hit original state-read counter assertion21. Native/physical restore fields are memory fixtures only; no real HTTP/Host/process/device/settings/OS sleep.'}];}
