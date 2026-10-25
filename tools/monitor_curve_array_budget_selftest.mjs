// Before/current actual Output: byte equality and packet-only allocations.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_CURVE_ARRAY_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/frame-curve-array-budget-20261016').replaceAll('\\','/');
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',results={};
for(const side of process.argv.includes('--before-only')?['before']:['before','after']){
 const dir=out+'/native/'+side;fs.mkdirSync(dir,{recursive:true});const source=side==='before'?out+'/before/native/monitor_frame_output.h':root+'/native/monitor_frame_output.h';
 const batch=dir+'/compile.bat';fs.writeFileSync(batch,`@echo off\r\ncall "${vc}" >nul\r\nif errorlevel 1 exit /b %ERRORLEVEL%\r\ncl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /DUNICODE /D_UNICODE /DYMCC_FRAME_OUTPUT_HEADER=\\"${source}\\" /I"${root}/native" "${root}/tools/monitor_curve_array_budget_selftest.cpp" /Fo"${dir}/curve.obj" /Fe"${dir}/curve.exe"\r\nexit /b %ERRORLEVEL%\r\n`);
 const c=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/s','/c','""'+batch+'""'],{windowsVerbatimArguments:true,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});fs.writeFileSync(dir+'/COMPILE.log',(c.stdout||'')+(c.stderr||''));assert.equal(c.status,0,c.stdout+c.stderr);
 const r=spawnSync(dir+'/curve.exe',[dir+'/RESULT.json'],{encoding:'utf8',windowsHide:true,timeout:15000});fs.writeFileSync(dir+'/RUN.log',(r.stdout||'')+(r.stderr||''));assert.equal(r.status,0,r.stdout+r.stderr);
 results[side]=JSON.parse(fs.readFileSync(dir+'/RESULT.json'));assert(results[side].ok);results[side].sourceSha256=crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');
 console.log(JSON.stringify({side,checks:results[side].checks,packetComparisons:results[side].packetComparisons,work:results[side].work}));
}
if(results.after){
 assert.deepEqual(results.after.trace,results.before.trace,'canonical frame JSON bytes changed');
 for(let i=0;i<results.after.work.length;++i){const b=results.before.work[i],a=results.after.work[i];assert.equal(a.packets,b.packets);assert(a.allocations<b.allocations);assert(a.allocatedBytes<b.allocatedBytes);assert.equal(a.outputObjectBytes,b.outputObjectBytes)}
 const result={ok:true,checks:results.after.checks,packetComparisons:results.after.packetComparisons,canonicalPacketsIdentical:true,work:{before:results.before.work,after:results.after.work},sourceSha256:{before:results.before.sourceSha256,after:results.after.sourceSha256},scope:'Actual production Output with synthetic observations; allocation counts only inside packet(), no hardware, OS mapping, process, timer, CPU percentage or wall-time benchmark.'};
 fs.writeFileSync(out+'/CURVE-ARRAY-RESULT.json',JSON.stringify(result,null,2)+'\n');
}
