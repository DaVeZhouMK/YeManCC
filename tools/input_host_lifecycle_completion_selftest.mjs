// Runs exact queue ownership, dequeue and completion logic with inert device endpoints.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const opts={};
for(let i=2;i<process.argv.length;i++){
  const key=process.argv[i];assert(['--out','--source'].includes(key)&&process.argv[i+1]&&!opts[key]);opts[key]=process.argv[++i];
}
assert(opts['--out']);
const out=path.resolve(opts['--out']);fs.mkdirSync(out,{recursive:true});
const source=path.resolve(opts['--source']||path.join(repo,'native/main.cpp'));
const sha=b=>createHash('sha256').update(b).digest('hex');
const files=['tools/input_host_lifecycle_completion_selftest.mjs','tools/fixtures/input_host_lifecycle_completion/fixture.cpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','native/input_host_lifecycle_claim.h','deps/json/json.hpp'];
const inputs=[{path:source,sha256:sha(fs.readFileSync(source))},...files.map(file=>({path:path.join(repo,file),sha256:sha(fs.readFileSync(path.join(repo,file)))}))];
const main=fs.readFileSync(source,'utf8');
const worker=extract(main,'inputHostRunQueuedLifecycleWork'),guard=extract(main,'RecoveryStateBusyGuard','struct'),host=extract(main,'focusIsolationReadAuthoritativeHost');
const auditAt=worker.code.indexOf('    inputHostSwitchAuditDequeued(reason);');
const at=worker.code.indexOf('    try {',auditAt);assert(auditAt>0&&at>auditAt);
const brace=worker.code.indexOf('{',at),end=closeBrace(worker.code,brace);
const sliced=worker.code.slice(0,brace+1)+'\n        dispatchTestWork(reason);\n    '+worker.code.slice(end);
const headerPath=path.join(repo,'native/input_host_lifecycle_claim.h');
const hasHeader=worker.code.includes('LifecycleClaimGuard');
if(hasHeader)fs.copyFileSync(headerPath,path.join(out,'input_host_lifecycle_claim.h'));
let cpp=fs.readFileSync(path.join(repo,files[1]),'utf8');
for(const[k,v]of Object.entries({HEADER:hasHeader?'#include "input_host_lifecycle_claim.h"':'',RECOVERY_GUARD:guard.code,READ_HOST:host.code,WORKER:sliced})){
  assert.equal(cpp.split('@@'+k+'@@').length,2);cpp=cpp.replace('@@'+k+'@@',()=>v);
}
assert(!/@@\w+@@/.test(cpp));
fs.writeFileSync(path.join(out,'fixture.cpp'),cpp);fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(out,'json.hpp'));
const receipt={status:'running',source,sourceSha256:inputs[0].sha256,inputs,bindings:[worker,guard,host].map(({code,...rest})=>rest),configurations:[],hardwareIo:false,hostStarted:false,wholeDeviceDispatchExecuted:false};
fs.writeFileSync(path.join(out,'source-functions.json'),JSON.stringify([worker,guard,host],null,2));
const vc=process.env.YMCC_VCVARS64||'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const caseNames=['ordinary-owned-work','actual-runid-copy-allocation-failure','dequeued-observation-failure','recovery-observer-enter-is-optional','recovery-observer-leave-is-optional','post-dispatch-failure-does-not-replay-device-work','business-failure-keeps-existing-no-replay-policy','suppression-observation-failure'];
for(const configuration of ['Release','Checked']){
  const dir=path.join(out,configuration);fs.mkdirSync(dir,{recursive:true});
  const command=calibrationToolchainPreamble(vc)+'chcp 65001 >nul\r\nset VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /showIncludes /permissive- '+(configuration==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG')+' /I"'+out+'" "'+path.join(out,'fixture.cpp')+'" /Fo"'+path.join(dir,'fixture.obj')+'" /Fe"'+path.join(dir,'fixture.exe')+'"\r\nexit /b %errorlevel%\r\n';
  fs.writeFileSync(path.join(dir,'build.cmd'),command);
  let r=spawnSync('cmd.exe',['/d','/c',path.join(dir,'build.cmd')],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000});
  const compileText=(r.stdout||'')+(r.stderr||'');fs.writeFileSync(path.join(dir,'compile.log'),compileText);assert.equal(r.status,0,'fixture compilation');
  const compilerInputs=[...new Set([...compileText.matchAll(/^\s*(?:Note: including file:|注意:\s*包含文件:)\s*(.+?)\r?$/gm)].map(m=>path.resolve(m[1])))];
  assert(compilerInputs.length>0,'compiler include observation missing');
  const results=[];
  for(const caseName of caseNames){
    r=spawnSync(path.join(dir,'fixture.exe'),[caseName],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:10000});
    fs.writeFileSync(path.join(dir,caseName+'.log'),(r.stdout||'')+(r.stderr||''));
    let result;
    try{result=JSON.parse(r.stdout);}catch{result={status:'fail',checks:0,cases:[{name:caseName,passed:false}],failures:[{case:caseName,assertion:'isolated production-scope process terminated before receipt',exitCode:r.status,signal:r.signal}]};}
    results.push({caseName,exitCode:r.status,...result});
  }
  const result={status:results.every(r=>r.exitCode===0&&r.status==='pass')?'pass':'fail',checks:results.reduce((n,r)=>n+r.checks,0),failures:results.flatMap(r=>r.failures),results};
  receipt.configurations.push({configuration,exitCode:result.status==='pass'?0:1,compilerInputs,executableSha256:sha(fs.readFileSync(path.join(dir,'fixture.exe'))),...result});
  console.log(configuration+': '+result.status+' '+result.checks+' checks / '+result.failures.length+' failures');
}
receipt.changedInputs=inputs.filter(row=>sha(fs.readFileSync(row.path))!==row.sha256);
receipt.sourceUnchanged=receipt.changedInputs.length===0;
receipt.generatedInputs=['fixture.cpp','json.hpp',...(hasHeader?['input_host_lifecycle_claim.h']:[])].map(name=>({path:path.join(out,name),sha256:sha(fs.readFileSync(path.join(out,name)))}));
receipt.status=receipt.configurations.every(r=>r.exitCode===0)&&receipt.sourceUnchanged?'pass':'fail';
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(receipt,null,2));process.exitCode=receipt.status==='pass'?0:1;
