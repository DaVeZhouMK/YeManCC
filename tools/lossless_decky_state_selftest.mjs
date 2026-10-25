import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]||path.join(root,'../../../Build/Validation/Lossless-Toggle-Xbox-20261022/decky'));
fs.mkdirSync(out,{recursive:true});
const inputs=['src/bridge/losslessProgramState.ts','src/bridge/losslessRuntimeState.ts','src/bridge/deckyLosslessState.ts','src/bridge/losslessScaling.ts','src/bridge/deckyMirrorHost.ts','decky-plugin/src/mirrorClient.ts','decky-plugin/src/index.tsx','tools/decky_sidebar_lossless_resilience_selftest.mjs','tools/lossless_decky_state_selftest.mjs'];
const hash=name=>createHash('sha256').update(fs.readFileSync(path.join(root,name))).digest('hex');
const sourceHashes=Object.fromEntries(inputs.map(name=>[name,hash(name)]));
const run=spawnSync(process.execPath,['tools/decky_sidebar_lossless_resilience_selftest.mjs'],{cwd:root,encoding:'utf8',windowsHide:true,timeout:30000,
 env:{...process.env,YMCC_DECKY_ARTIFACT_ROOT:out}});
fs.writeFileSync(path.join(out,'stdout.log'),run.stdout||'');fs.writeFileSync(path.join(out,'stderr.log'),(run.stderr||'')+(run.error?String(run.error):''));
let result={pass:false,sourceHashes,actualOSInput:false,actualLsControl:false,scope:'Actual mirror and observation consumers with bounded logical time/IPC; no installed Decky/Steam or LS'};
try{
 assert.equal(run.status,0,'Decky state test failed; see stdout/stderr');
 const evidence=JSON.parse(fs.readFileSync(path.join(out,'validation/lossless-resilience-selftest.json'),'utf8'));
 assert.equal(evidence.hardwareWrites,0);assert.equal(evidence.memoryOwnersOnly,true);assert(evidence.cases>0&&evidence.cases===evidence.results.length);
 const changedInputs=inputs.filter(name=>hash(name)!==sourceHashes[name]);assert.deepEqual(changedInputs,[]);
 result={...result,pass:true,cases:evidence.cases,results:evidence.results,changedInputs};
}catch(error){result.error=String(error);}
fs.writeFileSync(path.join(out,'lossless-decky-result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({pass:result.pass,cases:result.cases,error:result.error}));process.exitCode=result.pass?0:1;
