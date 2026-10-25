import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]||path.join(root,'../../../Build/Validation/Lossless-Toggle-Xbox-20261022/focus'));
fs.mkdirSync(out,{recursive:true});
const inputs=['native/lossless_return_focus.h','native/resume_focus.h','native/monitor_identity.h','tools/lossless_return_focus_selftest.cpp','tools/lossless_return_focus_selftest.py','deps/json/json.hpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/lossless_return_focus_selftest.mjs'];
const hash=name=>createHash('sha256').update(fs.readFileSync(path.join(root,name))).digest('hex');
const sourceHashes=Object.fromEntries(inputs.map(name=>[name,hash(name)]));
const run=spawnSync('python',['tools/lossless_return_focus_selftest.py','--output',path.join(out,'native')],{cwd:root,encoding:'utf8',windowsHide:true,timeout:60000});
fs.writeFileSync(path.join(out,'stdout.log'),run.stdout||'');fs.writeFileSync(path.join(out,'stderr.log'),(run.stderr||'')+(run.error?String(run.error):''));
let result={pass:false,sourceHashes,actualOSInput:false,actualLsControl:false,scope:'Actual native LS return consumer and source-extracted production entrypoints against FakeOS; no live Windows foreground proof'};
try{
 assert.equal(run.status,0,'Native focus test failed; see stdout/stderr');
 const pointer=JSON.parse(fs.readFileSync(path.join(out,'native/latest.json'),'utf8'));
 const receipt=JSON.parse(fs.readFileSync(path.join(pointer.run,'result.json'),'utf8'));
 assert.equal(receipt.pass,true);assert.equal(receipt.realApplicationsOrHardwareRun,false);assert(receipt.profiles.length===2&&receipt.profiles.every(x=>x.pass));
 const changedInputs=inputs.filter(name=>hash(name)!==sourceHashes[name]);assert.deepEqual(changedInputs,[]);
 result={...result,pass:true,classification:receipt.classification,profiles:receipt.profiles,
   functionHashes:receipt.functionHashes,entryHashes:receipt.entryHashes,changedInputs};
}catch(error){result.error=String(error);}
fs.writeFileSync(path.join(out,'lossless-return-focus-result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({pass:result.pass,profiles:result.profiles?.length,error:result.error}));process.exitCode=result.pass?0:1;
