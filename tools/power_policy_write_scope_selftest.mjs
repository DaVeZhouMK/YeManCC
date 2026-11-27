import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

const out=process.argv[2];
assert(out&&path.isAbsolute(out)&&!fs.existsSync(out),'Supply a fresh absolute evidence directory');
const inputs=['native/main.cpp','native/sleep_power_state.h','deps/json/json.hpp','tools/fixtures/power_policy_write_scope.cpp','tools/power_policy_write_scope_selftest.mjs'];
const bytes=new Map(inputs.map(file=>[file,fs.readFileSync(file)]));
const sha=value=>createHash('sha256').update(value).digest('hex');
const main=bytes.get('native/main.cpp').toString('utf8');
function part(start,end){
 const first=main.indexOf(start),last=main.indexOf(end,first+start.length);
 assert(first>=0&&last>first&&main.indexOf(start,first+1)===-1,start);
 return main.slice(first,last);
}
const sections={
 lease:part('static bool beginHardwareWriteLease(', 'static bool isPowerRegistryPath('),
 capture:part('[[maybe_unused]] static bool captureSharedPowerLifecycle(', '[[maybe_unused]] static ymcc::sleeppower::RevocationMirrors::Registration observeSharedPowerRevocation('),
 registry:part('    ipc_on("registry.writePowerBatch",', '    const auto deleteRegistryValue ='),
 active:part('    ipc_on("power.setActiveScheme",','    ipc_on("power.activeScheme",'),
};
fs.mkdirSync(out,{recursive:true});
const save=(name,text)=>fs.writeFileSync(path.join(out,name),text,{flag:'wx'});
for(const [name,text] of Object.entries(sections))save(name+'.inc',text);
for(const file of inputs.slice(1,4))save(path.basename(file),bytes.get(file));
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.statSync(vcvars).isFile()&&!/["%\r\n]/.test(vcvars));
save('compile.cmd',`@echo off\r\ncall "${vcvars}" > toolchain.log 2>&1\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /EHsc /utf-8 power_policy_write_scope.cpp /Fe:fixture.exe\r\n`);
const run=(exe,args)=>spawnSync(exe,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:60000});
const compile=run('cmd.exe',['/d','/c','compile.cmd']);
save('compile.log',(compile.stdout??'')+(compile.stderr??'')+(compile.error?.message??''));
const executed=compile.status===0?run(path.join(out,'fixture.exe'),[]):null;
save('run.log',(executed?.stdout??'')+(executed?.stderr??'')+(executed?.error?.message??''));
const cases=(executed?.stdout??'').trim().split(/\r?\n/).filter(Boolean).map(line=>{
 const [id,result,writes,leases]=line.split(' ');
 return {id,pass:result==='PASS',writes:Number(writes),leases:Number(leases)};
});
const sourceStable=inputs.every(file=>sha(fs.readFileSync(file))===sha(bytes.get(file)));
const report={pass:sourceStable&&executed?.status===0&&cases.length>0&&cases.every(c=>c.pass),sourceStable,
 compileExit:compile.status,runExit:executed?.status??null,deviceWrites:0,cases,vcvars,
 sources:Object.fromEntries([...bytes].map(([file,data])=>[file,sha(data)])),sections:Object.fromEntries(Object.entries(sections).map(([key,data])=>[key,sha(data)]))};
save('RESULT.json',JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report));
if(!report.pass)process.exitCode=1;
