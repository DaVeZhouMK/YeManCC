import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/sleep-recovery-ui-consumer';
export const SHARED='tools/ymcc_sim/global/fan-monitor-browser-consumer';
export const CASE_ID='sleep.recovery-ui-consumer';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function moduleSources(){return ['adapter.mjs','run.mjs','worker.mjs','worker.ps1','observe.cjs','contract.mjs','cleanup.mjs','contract.test.mjs','spec.json','boundary.json','README.md','fixture-maintenance.json','fixtures/prior-script.mjs.txt','fixtures/prior-original.stderr.log','fixtures/prior-ui-observation.json','fixtures/prior-boundary-observation.json','fixtures/dom-result.json','fixtures/actual-panel.png','fixtures/actual-page.png'].map(name=>MODULE+'/'+name);}
export function getSleepRecoveryUiAdapters(repo=REPO){
 const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'spec.json'),'utf8'));
 const tool=JSON.parse(fs.readFileSync(path.join(repo,SHARED,'toolchain-inputs.json'),'utf8'));
 return [{id:CASE_ID,domain:'sleep',title:'Standalone automatic sleep recovery Vue settings/status consumer and stale reply fault detection',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/sleep-ui'],timeoutMs:120000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['sleep-ui/result.json'],evidenceInputs:[...new Set([...moduleSources(),...spec.sources])],artifactInputs:tool.files.map(row=>row.path),boundary:'Unchanged original22 standalone component UI cases (current Settings does not mount recovery UI), exact native memory terminals and real Vue/TS consumers; owned headless browser/private profile/random loopback, actual page errors and natural closure. No native recovery/Steam/OS sleep/installed or physical input acceptance.'}];
}
