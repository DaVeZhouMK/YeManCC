// Runs exact queue ownership, dequeue and completion logic with inert device endpoints.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {lifecycleCompletionTools,INFRASTRUCTURE} from './ymcc_sim/global/controller-lifecycle-completion/adapter.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const caseNames=['ordinary-owned-work','actual-runid-copy-allocation-failure','dequeued-observation-failure','recovery-observer-enter-is-optional','recovery-observer-leave-is-optional','post-dispatch-failure-does-not-replay-device-work','business-failure-keeps-existing-no-replay-policy','suppression-observation-failure'];
export function lifecycleCompletionSelection(options={}){
 const select=(key,allowed,defaults)=>{
  if(options[key]===undefined)return [...defaults];
  assert(typeof options[key]==='string'&&options[key].trim().length>0,'Empty selection: '+key);
  const selected=options[key].split(',').map(s=>s.trim());
  assert(selected.length>0&&selected.every(s=>allowed.includes(s))&&new Set(selected).size===selected.length,'Unknown or duplicate selection: '+key);
  return selected;
 };
 return {configurations:select('--configs',['Release','Checked'],['Release','Checked']),caseNames:select('--cases',caseNames,caseNames),directed:options['--configs']!==undefined||options['--cases']!==undefined};
}
const opts={};
for(let i=2;i<process.argv.length;i++){
  const key=process.argv[i];assert(['--out','--source','--configs','--cases'].includes(key)&&process.argv[i+1]&&!opts[key]);opts[key]=process.argv[++i];
}
const selected=lifecycleCompletionSelection(opts);
assert(opts['--out']);
const out=path.resolve(opts['--out']);
// Reject any prior output, including an empty directory; never overwrite a prior run.
fs.mkdirSync(path.dirname(out),{recursive:true});fs.mkdirSync(out);
function writeFresh(file,value){const fd=fs.openSync(file,'wx');try{fs.writeFileSync(fd,value);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}}
const writeJson=(file,value)=>writeFresh(file,JSON.stringify(value,null,2)+'\n');
const source=path.resolve(opts['--source']||path.join(repo,'native/main.cpp'));
const sha=b=>createHash('sha256').update(b).digest('hex');
const files=[...new Set(['tools/input_host_lifecycle_completion_selftest.mjs','tools/fixtures/input_host_lifecycle_completion/fixture.cpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs','native/input_host_lifecycle_claim.h','deps/json/json.hpp',...INFRASTRUCTURE,'native/virtual_controller_ui_output_runtime.h'])];
const portReferenceNames=['native/virtual_controller_queued_start_runtime.h','native/msi_power_rearm_work_runtime.h','native/virtual_controller_start_runtime.h'];
for(const name of portReferenceNames)files.push(name);
const playerReference=fs.readFileSync(path.join(repo,portReferenceNames[0]),'utf8');
const powerReference=fs.readFileSync(path.join(repo,portReferenceNames[1]),'utf8');
const startupReference=fs.readFileSync(path.join(repo,portReferenceNames[2]),'utf8');
assert(/if\(wire\.rfind\(prefix,0\)!=0\)\{[\s\S]*?if\(startReason\(wire\)\)work\.reason=wire;\s*return true;\s*\}/.test(playerReference),'Original ordinary player-work decode changed; review private seam');
assert(/if\(wire\.rfind\(prefix,0\)!=0\)return true;/.test(powerReference),'Original ordinary MSI-work decode changed; review private seam');
assert(/g_startupInputState\.load\(std::memory_order_acquire\)!=ymcc::StartupInputState::Waiting\s*\|\|/.test(startupReference),'Original idle startup exception gate changed; review private seam');
const inputs=[{path:source,sha256:sha(fs.readFileSync(source))},...files.map(file=>({path:path.join(repo,file),sha256:sha(fs.readFileSync(path.join(repo,file)))}))];
const main=fs.readFileSync(source,'utf8');
const worker=extract(main,'inputHostRunQueuedLifecycleWork'),guard=extract(main,'RecoveryStateBusyGuard','struct');
const queueWork=extract(main,'InputHostLifecycleWork','struct');
assert.equal([...main.matchAll(/^\s*#\s*include\s*"virtual_controller_ui_output_runtime\.h"\s*$/gm)].length,1,'main must include the actual host runtime header exactly once');
const hostPath=path.join(repo,'native/virtual_controller_ui_output_runtime.h');
const hostBytes=fs.readFileSync(hostPath);
const host={...extract(hostBytes.toString('utf8'),'focusIsolationReadAuthoritativeHost'),origin:{path:hostPath,sha256:sha(hostBytes)}};
const auditAt=worker.code.indexOf('    inputHostSwitchAuditDequeued(reason);');
const at=worker.code.indexOf('    try {',auditAt);assert(auditAt>0&&at>auditAt);
const brace=worker.code.indexOf('{',at),end=closeBrace(worker.code,brace);
const sliced=worker.code.slice(0,brace+1)+'\n        dispatchTestWork(reason);\n    '+worker.code.slice(end);
const headerPath=path.join(repo,'native/input_host_lifecycle_claim.h');
const hasHeader=worker.code.includes('LifecycleClaimGuard');
if(hasHeader)fs.copyFileSync(headerPath,path.join(out,'input_host_lifecycle_claim.h'),fs.constants.COPYFILE_EXCL);
let cpp=fs.readFileSync(path.join(repo,files[1]),'utf8');
for(const[k,v]of Object.entries({HEADER:hasHeader?'#include "input_host_lifecycle_claim.h"':'',QUEUE_WORK:queueWork.code,RECOVERY_GUARD:guard.code,READ_HOST:host.code,WORKER:sliced})){
  assert.equal(cpp.split('@@'+k+'@@').length,2);cpp=cpp.replace('@@'+k+'@@',()=>v);
}
assert(!/@@\w+@@/.test(cpp));
writeFresh(path.join(out,'fixture.cpp'),cpp);fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(out,'json.hpp'),fs.constants.COPYFILE_EXCL);
const selection=lifecycleCompletionTools(),native=selection.native;
const fixturePortScope={ordinaryNonMsiQueueOnly:true,virtualTargetEnabled:false,startupInputState:'Idle',typedPlayerPowerWakeRecoveryPaths:'unsupported terminal aborts on entry',sleepRepairPowerAdmission:'empty shared_ptr; no repair dispatch',originalCaseNamesAndBodiesUnchanged:true,referenceSources:portReferenceNames};
const receipt={selection:selected,fixturePortScope,status:'running',source,sourceSha256:inputs[0].sha256,inputs,bindings:[worker,guard,host,queueWork].map(({code,...rest})=>rest),configurations:[],processes:[],toolSelection:{native,files:selection.files},hardwareIo:false,hostStarted:false,wholeDeviceDispatchExecuted:false};
writeJson(path.join(out,'source-functions.json'),[worker,guard,host,queueWork]);
const system32=path.dirname(native.commandProcessor),windows=path.dirname(system32);
const childEnvironment={...process.env};
for(const key of Object.keys(childEnvironment))if(['CL','_CL_','LINK','_LINK_','PATH','INCLUDE','LIB','LIBPATH','VSLANG','SYSTEMROOT','WINDIR','VS_UNICODE_OUTPUT'].includes(key.toUpperCase()))delete childEnvironment[key];
Object.assign(childEnvironment,{PATH:[...new Set([path.dirname(native.compiler),path.dirname(native.linker),system32,windows])].join(path.delimiter),INCLUDE:native.include.join(';'),LIB:native.lib.join(';'),LIBPATH:'',VSLANG:'1033',SystemRoot:windows,WINDIR:windows});
receipt.childEnvironment={PATH:childEnvironment.PATH,INCLUDE:childEnvironment.INCLUDE,LIB:childEnvironment.LIB,LIBPATH:childEnvironment.LIBPATH,VSLANG:childEnvironment.VSLANG,SystemRoot:windows,WINDIR:windows,cleared:['CL','_CL_','LINK','_LINK_','VS_UNICODE_OUTPUT']};
// Persist the original output and process result before interpreting success or failure.
function invoke(dir,name,executable,args,timeout){
 const startedAtUtc=new Date().toISOString(),start=performance.now();
 let r;
 try{r=spawnSync(executable,args,{cwd:dir,env:childEnvironment,windowsHide:true,encoding:'utf8',timeout,maxBuffer:8*1024*1024});}
 catch(error){r={pid:null,status:null,signal:null,stdout:'',stderr:'',error};}
 const endedAtUtc=new Date().toISOString(),elapsedMs=performance.now()-start;
 const stdout=path.join(dir,name+'.stdout.log'),stderr=path.join(dir,name+'.stderr.log'),log=path.join(dir,name+'.log');
 writeFresh(stdout,r.stdout||'');writeFresh(stderr,r.stderr||'');writeFresh(log,(r.stdout||'')+(r.stderr||''));
 const pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))});
 const error=r.error?{name:r.error.name,message:r.error.message,code:r.error.code??null,errno:r.error.errno??null,syscall:r.error.syscall??null,path:r.error.path??null,spawnargs:r.error.spawnargs??null,stack:r.error.stack??null}:null;
 const record={schemaVersion:1,name,executable,args,cwd:dir,pid:r.pid??null,startedAtUtc,endedAtUtc,elapsedMs,timeoutMs:timeout,maxBuffer:8*1024*1024,exitCode:r.status??null,signal:r.signal??null,error,logs:{stdout:pin(stdout),stderr:pin(stderr),combined:pin(log)},environment:receipt.childEnvironment};
 const processFile=path.join(dir,name+'.process.json');writeJson(processFile,record);
 receipt.processes.push({...record,process:pin(processFile)});
 return r;
}
function linkerInputsFrom(text){
 // English /VERBOSE:LIB reports both searched archives and loaded members.
 // Keep all observed absolute .lib paths, including any outside the declaration.
 const found=[];
 for(const line of text.split(/\r?\n/)){
  const match=/^\s*(?:Searching|Loaded)\s+("?)([A-Za-z]:[\\/].*?\.lib)\1(?=\s*:|\(|\s*$)/i.exec(line);
  if(match)found.push(path.resolve(match[2]));
 }
 return [...new Map(found.map(p=>[p.toLowerCase(),p])).values()];
}
for(const configuration of selected.configurations){
  const dir=path.join(out,configuration);fs.mkdirSync(dir);
  const args=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/showIncludes','/permissive-',...(configuration==='Release'?['/O2','/MT','/DNDEBUG']:['/Od','/MTd','/RTC1','/D_DEBUG']),'/I'+out,path.join(out,'fixture.cpp'),'/Fo'+path.join(dir,'fixture.obj'),'/Fe'+path.join(dir,'fixture.exe'),'/sourceDependencies',path.join(dir,'source-dependencies.json'),'/link','/verbose:lib',...native.lib.map(root=>'/LIBPATH:'+root)];
  let r=invoke(dir,'compile',native.compiler,args,60000);
  const compileText=(r.stdout||'')+(r.stderr||'');
  assert(!r.error&&!r.signal,'fixture compilation process');assert.equal(r.status,0,'fixture compilation');
  const compilerInputs=[...new Set([...compileText.matchAll(/^\s*(?:Note: including file:|注意:\s*包含文件:)\s*(.+?)\r?$/gm)].map(m=>path.resolve(m[1])))];
  assert(compilerInputs.length>0,'compiler include observation missing');
  const linkerInputs=linkerInputsFrom(compileText);assert(linkerInputs.length>0,'linker library observation missing');
  const results=[];
  for(const caseName of selected.caseNames){
    r=invoke(dir,caseName,path.join(dir,'fixture.exe'),[caseName],10000);
    assert(!r.error&&!r.signal,'isolated fixture process failed: '+caseName);
    let result;
    try{result=JSON.parse(r.stdout);}catch{result={status:'fail',checks:0,cases:[{name:caseName,passed:false}],failures:[{case:caseName,assertion:'isolated production-scope process terminated before receipt',exitCode:r.status,signal:r.signal}]};}
    results.push({caseName,exitCode:r.status,...result});
  }
  const result={status:results.every(r=>r.exitCode===0&&r.status==='pass')?'pass':'fail',checks:results.reduce((n,r)=>n+r.checks,0),failures:results.flatMap(r=>r.failures),results};
  receipt.configurations.push({configuration,exitCode:result.status==='pass'?0:1,compilerInputs,linkerInputs,sourceDependencies:{path:path.join(dir,'source-dependencies.json'),sha256:sha(fs.readFileSync(path.join(dir,'source-dependencies.json')))},executableSha256:sha(fs.readFileSync(path.join(dir,'fixture.exe'))),...result});
  console.log(configuration+': '+result.status+' '+result.checks+' checks / '+result.failures.length+' failures');
}
receipt.changedInputs=inputs.filter(row=>sha(fs.readFileSync(row.path))!==row.sha256);
receipt.sourceUnchanged=receipt.changedInputs.length===0;
receipt.generatedInputs=['fixture.cpp','json.hpp',...(hasHeader?['input_host_lifecycle_claim.h']:[])].map(name=>({path:path.join(out,name),sha256:sha(fs.readFileSync(path.join(out,name)))}));
receipt.status=receipt.configurations.every(r=>r.exitCode===0)&&receipt.sourceUnchanged?'pass':'fail';
writeJson(path.join(out,'RESULT.json'),receipt);process.exitCode=receipt.status==='pass'?0:1;
