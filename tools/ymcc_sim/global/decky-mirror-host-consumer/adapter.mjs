import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/decky-mirror-host-consumer';
export const MODULE_ROOT=path.dirname(fileURLToPath(import.meta.url));
export const REPO=path.resolve(MODULE_ROOT,'../../../..');
export const CASE_ID='plugins.decky-mirror-host-consumer';
export const SPEC=JSON.parse(fs.readFileSync(path.join(MODULE_ROOT,'spec.json'),'utf8'));
export const MODULE_FILES=['adapter.mjs','run.mjs','contract.mjs','observe.cjs','register-loader.mjs','source-loader.mjs','spec.json','toolchain-inputs.json','contract.test.mjs','README.md'];
export function getDeckyMirrorHostConsumerAdapters(repo=REPO){
 const profile=JSON.parse(fs.readFileSync(path.join(MODULE_ROOT,'toolchain-inputs.json'),'utf8'));
 let blockedReason;
 try{if(profile.files.length>640||profile.files.some(row=>!path.isAbsolute(row.path)||crypto.createHash('sha256').update(fs.readFileSync(row.path)).digest('hex')!==row.sha256))throw Error();}catch{blockedReason='DECKY_CONSUMER_TOOLCHAIN_MISSING_OR_CHANGED';}
 return [{id:CASE_ID,domain:'plugins',title:'Original Decky mirror host, relay, client and connection probe consumers',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/decky-mirror'],timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['decky-mirror/result.json'],evidenceInputs:[...MODULE_FILES.map(file=>MODULE+'/'+file),...SPEC.productSources,...SPEC.fixtures,...SPEC.suites.map(suite=>suite.script),...SPEC.metadata],artifactInputs:profile.files.map(row=>row.path),...(blockedReason?{blockedReason}:{}),boundary:'Four unchanged original producers and all 70 original scenarios; two exact host-function faults must fail original named assertions after successful transformation. Memory IPC/store/socket/clock only. App.vue/native restoration checks are static evidence; full App/Host/Steam/device/sleep acceptance remains separate.'}];
}
