import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.env.YMCC_MSI_WITNESS_OUT||'G:/YeManCC-Work/Build/Validation/VC-PHYSICAL-SIGNAL-EVIDENCE-20261022/witness');
fs.mkdirSync(out,{recursive:true});
const files=['native/msi_external_raw_receipts.h','tools/msi_external_gamepad_witness_selftest.cpp'];
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const source=files.map(relative=>({relative,path:path.join(root,relative),sha256:hash(path.join(root,relative))}));
const cases=[];
const original=fs.readFileSync(path.join(root,files[0]),'utf8');
const env={...process.env,PATH:[path.dirname(process.execPath),path.join(process.env.SystemRoot||'C:/Windows','System32'),process.env.SystemRoot||'C:/Windows'].join(';')};
for(const [name,flags,mutation,expectedFailure] of [
 ['Release','/MT /O2 /DNDEBUG',null,null],
 ['Checked','/MTd /Od /RTC1 /D_DEBUG',null,null],
 ['negative-no-budget','/MT /O2',s=>s.replace('if(haveSelection&&now-lastSelection<500)','if(false&&haveSelection&&now-lastSelection<500)'),'global budget throttles sub-500ms work'],
 ['negative-vendor-decode','/MT /O2',s=>s.replace('if(witnessEligible) {','if(true) {'),'vendor receipt remains counts-only and never decoded'],
]){
 const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});
 let cpp=path.join(root,files[1]);
 if(mutation){for(const f of files){const target=path.join(dir,f);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,f.endsWith('.h')?mutation(original):fs.readFileSync(path.join(root,f)));}cpp=path.join(dir,files[1]);assert.notEqual(fs.readFileSync(path.join(dir,files[0]),'utf8'),original);}
 const exe=path.join(dir,'selftest.exe'),batch=path.join(dir,'build.cmd');
 fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc '+flags+' /I"'+path.join(root,'deps/json')+'" "'+cpp+'" /Fo"'+path.join(dir,'selftest.obj')+'" /Fe"'+exe+'"\r\nexit /b %errorlevel%\r\n');
 const build=spawnSync('cmd.exe',['/d','/c',batch],{encoding:'utf8',cwd:dir,env});fs.writeFileSync(path.join(dir,'build.log'),(build.stdout||'')+(build.stderr||''));
 if(build.status!==0)console.error(build.stdout,build.stderr);assert.equal(build.status,0,name+' compile');
 const run=spawnSync(exe,[],{encoding:'utf8',cwd:dir,env});const text=(run.stdout||'')+(run.stderr||'');fs.writeFileSync(path.join(dir,'result.txt'),text);
 if(mutation){assert.equal(run.status,1,name+' must fail');assert.ok(text.includes('FAIL '+expectedFailure),name+' catches intended defect');}
 else {if(run.status!==0)console.error(text);assert.equal(run.status,0,name+' actual production sampler');}
 const stats=text.match(/cases=(\d+) failures=(\d+)/);cases.push({name,expectedNegative:!!mutation,exit:run.status,cases:stats?Number(stats[1]):null,failures:stats?Number(stats[2]):null,expectedFailure});console.log(name+': '+text.trim().split('\n').at(-1));
}
for(const item of source)assert.equal(hash(item.path),item.sha256,'source drift '+item.relative);
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify({pass:true,source,cases,realRawInputCalls:0,realHidCalls:0,physicalSuppressionProven:false},null,2)+'\n');
