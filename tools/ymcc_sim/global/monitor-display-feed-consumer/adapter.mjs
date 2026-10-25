import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/monitor-display-feed-consumer', CASE_ID='sensor.monitor-display-feed-consumer';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','worker.ps1','spec.json','toolchain-inputs.json','runner.test.mjs','README.md'].map(name=>MODULE+'/'+name);}
export function getMonitorDisplayFeedAdapters(repo=REPO){
 const read=file=>JSON.parse(fs.readFileSync(file,'utf8')),spec=read(path.join(repo,MODULE,'spec.json')),tool=read(path.join(repo,MODULE,'toolchain-inputs.json'));
 return [{id:CASE_ID,domain:'sensor',title:'Original display feed throttle/subscription/reclaim and pure layout consumer checks',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/display-feed'],timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['display-feed/result.json'],evidenceInputs:[...moduleSources(),...spec.sources],artifactInputs:tool.files.map(row=>row.path),boundary:'Original16 named checks execute actual monitorDisplayFeed/monitorLayout with original virtual clock. Exact captured Build resolver maps only its old output namespace to a private case; no native/Host/IPC/device/browser. Private throttle and last-unsubscribe faults must fail original assertions. Loop iterations are not additional scenarios.'}];
}
