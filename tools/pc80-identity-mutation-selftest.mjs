import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out=path.resolve(repo,'../../../Build/Validation/Pc80-Software-20261014/mutations');
fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
const variants=[
 ['mini-v01',s=>s.replace('else if (ver.find("V03") != std::string::npos)','else if (ver.find("V03") != std::string::npos || p.find("MINI PRO") != std::string::npos)')],
 ['suiplay-matrix',s=>s.replace('id.accelMatrixId = "hc-06c0b954:suiplay-0x1:accel:neg-x-neg-z-y";', 'id.accelMatrixId.clear();')],
 ['deck-board',s=>s.replace('p == "JUPITER" || p == "GALILEO" ||', '')],
 ['msi-unknown',s=>s.replace(/else \{\s*id.family = YmccFamily::Unknown;\s*return;\s*\}\s*id.inputHidKey = "msi-claw/, 'else { id.deviceClass = "HandheldCompanion.Devices.ClawCG3EM"; }\n        id.inputHidKey = "msi-claw')],
];
const results=[];
for(const [name,mutate] of variants){
 const content=mutate(source);assert.notEqual(content,source,'mutation missing: '+name);
 const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const main=path.join(dir,'main.cpp');fs.writeFileSync(main,content);
 const r=spawnSync(process.execPath,['tools/pc80-identity-selftest.mjs','--main',main,'--out',dir],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:2*1024*1024});
 fs.writeFileSync(path.join(dir,'test.log'),(r.stdout||'')+(r.stderr||''));
 const report=JSON.parse(fs.readFileSync(path.join(dir,'comparison.json')));
 assert.equal(r.status,1,name+': test should reject mutation');assert.equal(report.ok,false);assert(report.failures.length,name+': no semantic failure');
 results.push({name,detected:true,failures:report.failures});console.log(name+': DETECTED');
}
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(results,null,2));
console.log('PC80_MUTATION_PASS count='+results.length+' hardwareWrites=false');
