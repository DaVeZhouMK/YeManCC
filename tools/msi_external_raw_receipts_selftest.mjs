import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {sourceInputs} from './ymcc_sim/global/controller-signal-evidence/adapter.mjs';
import {runConsumers} from './ymcc_sim/global/controller-signal-evidence/consumer-runner.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.env.YMCC_MSI_RAW_OUT || 'G:/YeManCC-Work/Build/Validation/MSI-External-Visibility-R5-20261013/raw-native');fs.mkdirSync(out,{recursive:true});
const acquisitionSource='tools/msi_external_raw_receipts_selftest.cpp';const sources=sourceInputs;const before=sources.map(relative=>({path:path.join(root,relative),sha256:createHash('sha256').update(fs.readFileSync(path.join(root,relative))).digest('hex')}));
const childEnv={...process.env,PATH:[path.dirname(process.execPath),path.join(process.env.SystemRoot||'C:/Windows','System32'),process.env.SystemRoot||'C:/Windows'].join(';')};
for(const [name,flags] of [['default',''],['partial','/DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC'],['explicit','/DYMCC_MSI_VISIBILITY_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC']]){
 const exe=path.join(out,name+'.exe'),batch=path.join(out,name+'.cmd');fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 '+flags+' /I"'+path.join(root,'deps/json')+'" "'+path.join(root,acquisitionSource)+'" /Fo"'+path.join(out,name+'.obj')+'" /Fe"'+exe+'" /link hid.lib\r\nexit /b %errorlevel%\r\n');const build=spawnSync('cmd.exe',['/d','/c',batch],{encoding:'utf8',cwd:out,env:childEnv});fs.writeFileSync(path.join(out,name+'-build.log'),(build.stdout||'')+(build.stderr||''));if(build.status!==0)process.stdout.write((build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,'Actual helper with memory RawInput boundaries compiles '+name);const run=spawnSync(exe,[],{encoding:'utf8',cwd:out,env:childEnv});const text=(run.stdout||'')+(run.stderr||'');fs.writeFileSync(path.join(out,name+'-result.txt'),text);console.log(name+': '+text.trim().split('\n').at(-1));assert.equal(run.status,0,'Actual helper runtime assertions '+name);
}
await runConsumers({repo:root,out,env:process.env});
assert(before.every(v=>createHash('sha256').update(fs.readFileSync(v.path)).digest('hex')===v.sha256),'Sources unchanged during test');fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({source:before,memoryRawInputBoundaries:true,productionHelperExercised:true,realRawInputCalls:0,physicalInputVerified:false},null,2)+'\n');
