// Compile the actual frozen/current frame output against deterministic receipts.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_HWINFO_ACQUISITION_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-producer-budget-20261016').replaceAll('\\','/'),vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';const results={};
for(const side of ['before','after']){
 const dir=out+'/frames/'+side;fs.mkdirSync(dir,{recursive:true});const source=side==='before'?out+'/before/native/monitor_frame_output.h':root+'/native/monitor_frame_output.h';
 const definition='/DYMCC_FRAME_OUTPUT_HEADER=\\"'+source+'\\"';
 const batch=dir+'/compile.bat';fs.writeFileSync(batch,`@echo off\r\ncall "${vc}" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /DUNICODE /D_UNICODE ${side==='before'?'/DYMCC_BEFORE_PRIORITY ':''}${definition} /I"${root}/native" "${root}/tools/monitor_acquisition_frames_selftest.cpp" /Fo"${dir}/frames.obj" /Fe"${dir}/frames.exe"\r\nexit /b %ERRORLEVEL%\r\n`);
 const c=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});fs.writeFileSync(dir+'/COMPILE.log',(c.stdout||'')+(c.stderr||''));assert.equal(c.status,0,c.stdout+c.stderr);
 const r=spawnSync(dir+'/frames.exe',[dir+'/RESULT.json'],{encoding:'utf8',windowsHide:true,timeout:10000});assert.equal(r.status,0,r.stdout+r.stderr);results[side]=JSON.parse(fs.readFileSync(dir+'/RESULT.json','utf8'));results[side].sourceSha256=crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');console.log(side+': '+results[side].checks+' checks');
}
fs.writeFileSync(out+'/FRAMES-RESULT.json',JSON.stringify({ok:true,before:results.before,after:results.after,realHardwareReads:false,realRtssMappingReads:false},null,2)+'\n');
