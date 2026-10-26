import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const started=Date.now();
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2),trackingOnly=args.includes('--tracking-only');
const positional=args.filter(arg=>arg!=='--tracking-only');
assert(positional.length===4&&positional[0]==='--out'&&positional[2]==='--toolchain'&&path.isAbsolute(positional[1])&&path.isAbsolute(positional[3]),'Use --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN [--tracking-only]');
const out=path.resolve(positional[1]),toolchain=path.resolve(positional[3]),relative=path.relative(repo,out);
assert(relative.startsWith('..'+path.sep)||path.isAbsolute(relative),'Private evidence must stay outside product repository');
assert(!fs.existsSync(out),'Never overwrite prior evidence/failures');fs.mkdirSync(out,{recursive:true});
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const sha=file=>digest(fs.readFileSync(file)),read=file=>fs.readFileSync(file,'utf8');
const json=file=>JSON.parse(read(file).replace(/^\uFEFF/,''));
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const relativeInputs=['native/hidhide_cli_job_guard.h','native/native_refresh_writer_runtime.h','native/native_refresh_writer_record.h','native/native_refresh_writer_store.h','native/virtual_target_return_record.h','native/virtual_target_return_store.h','native/virtual_target_return_process.h','tools/fixtures/hidhide_cli_job_guard/fixture.cpp','tools/hidhide_cli_job_guard_selftest.mjs','tools/fixtures/hidhide_cli_job_guard/README.md','deps/json/json.hpp'];
const snapshots=new Map(relativeInputs.map(relative=>[relative,fs.readFileSync(path.join(repo,relative))]));
const inputs=relativeInputs.map(relative=>({relative,path:path.join(repo,relative),sha256:digest(snapshots.get(relative))}));
save(path.join(out,'SOURCES.json'),{inputs});
const capture=json(path.join(toolchain,'build-environment.json')),manifest=json(path.join(toolchain,'toolchain-manifest.json'));
const env={...capture.environment};for(const key of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(key))delete env[key];
env.INCLUDE=manifest.roots.filter(r=>r.category==='INCLUDE').map(r=>r.frozen).join(';');
env.LIB=manifest.roots.filter(r=>r.category==='LIB').map(r=>r.frozen).join(';');env.LIBPATH=env.LIB;
env.TEMP=path.join(out,'temp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
const cl=capture.tools['cl.exe'].paths[0],link=capture.tools['link.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0];
const identities=[cl,link,dump,process.execPath].map(file=>{
 const match=manifest.tools.find(t=>path.resolve(t.original).toLowerCase()===path.resolve(file).toLowerCase());
 if(file!==process.execPath)assert(match&&sha(file)===match.sha256,'Compiler/tool identity mismatch: '+file);
 return {path:file,sha256:sha(file)};
});
const byTool=new Map(manifest.files.map(f=>[path.resolve(f.frozen).toLowerCase(),f]));
const variants=[{name:'Release',flags:['/O2']},...trackingOnly?[]:[{name:'Checked',flags:['/Od','/RTC1']}],
 {name:'negative-no-atomic-association',originalVariant:'negative-no-job-list',flags:['/O2'],before:'if(!UpdateProcThreadAttribute(candidate->startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,',after:'if(false&&!UpdateProcThreadAttribute(candidate->startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,',requiredCase:'crash/ExitProcess-kills-child-before-late-write'},
 {name:'negative-no-kill-on-close',flags:['/O2'],before:'limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;',after:'limits.BasicLimitInformation.LimitFlags=0;',requiredCase:'crash/ExitProcess-kills-child-before-late-write'}];
save(path.join(out,'PLAN.json'),{canonicalEntry:'tools/hidhide_cli_job_guard_selftest.mjs',trackingOnly,variants:variants.map(v=>({name:v.name,originalVariant:v.originalVariant,flags:v.flags})),execution:'actual current product JobGuard and this original private fixture; no cached execution',scope:'tracked JOB_LIST/HANDLE_LIST plus inherited private share-zero owner lease, legacy death control and original semantic mutants',queryJobLifeline:'NOT_RUN',jobAbsence:'UNKNOWN; never an idle proof',buildTimeoutMs:60000,runTimeoutMs:60000});
const results=[];
try{
 for(const variant of variants){
  assert(inputs.every(f=>sha(f.path)===f.sha256),'Source drift before variant; do not mix concurrent product revisions');
  const variantStart=Date.now(),dir=path.join(out,variant.name);fs.mkdirSync(path.join(dir,'files'),{recursive:true});
  const generated=[];
  for(const input of inputs.filter(f=>/\.(h|hpp|cpp)$/.test(f.relative))){const file=path.join(dir,path.basename(input.relative));fs.writeFileSync(file,snapshots.get(input.relative));generated.push({path:file,sha256:sha(file),original:input.relative});}
  if(variant.before){const header=path.join(dir,'hidhide_cli_job_guard.h'),text=read(header);assert.equal(text.split(variant.before).length,2,'Unique semantic mutation site is required');fs.writeFileSync(header,text.replace(variant.before,variant.after));generated.find(f=>f.path===header).sha256=sha(header);}
  save(path.join(dir,'GENERATED.json'),{sources:generated,mutation:variant.before?variant:null});
  const exe=path.join(dir,'JobGuardFixture.exe');
  const command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/DUNICODE','/D_UNICODE',...variant.flags,'/sourceDependencies',path.join(dir,'dependencies.json'),path.join(dir,'fixture.cpp'),'/I'+dir,'/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe];
  const buildStart=Date.now();const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});
  const buildMs=Date.now()-buildStart;fs.writeFileSync(path.join(dir,'build.log'),(built.stdout??'')+(built.stderr??''));
  save(path.join(dir,'COMMAND.json'),{executable:cl,arguments:command,exit:built.status,error:built.error?.message,buildMs});assert.equal(built.status,0,'Compilation failed: '+path.join(dir,'build.log'));
  const closureStart=Date.now(),byGenerated=new Map(generated.map(f=>[path.resolve(f.path).toLowerCase(),f])),dep=json(path.join(dir,'dependencies.json'));
  const compiled=[dep.Data.Source,...dep.Data.Includes].map(file=>{const key=path.resolve(file).toLowerCase(),bound=byGenerated.get(key)??byTool.get(key);assert(bound&&sha(file)===bound.sha256,'Unbound actual compiler input: '+file);return {path:file,sha256:bound.sha256,scope:byGenerated.has(key)?'actual guard and private fixture':'captured toolchain'};});
  const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});
  fs.writeFileSync(path.join(dir,'imports.log'),(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0);
  const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];assert.deepEqual(dlls,['kernel32.dll'],'No device/SDK/product imports');
  save(path.join(dir,'COMPILE-CLOSURE.json'),{inputs:compiled,unbound:[],toolIdentities:identities,binarySha256:sha(exe),staticImportedDlls:dlls,scope:'Static imports only; real private process/job/file/event/pipe APIs are expected, no device/product APIs.'});
  const closureMs=Date.now()-closureStart,terminal=path.join(dir,'RESULT.json');
  const runArgs=[path.join(dir,'files'),terminal,...variant.before&&trackingOnly?['--default-negative-only']:trackingOnly?['--tracking-only']:[]];
  const runStart=Date.now(),ran=spawnSync(exe,runArgs,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});const runMs=Date.now()-runStart;
  fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));save(path.join(dir,'RUN-COMMAND.json'),{executable:exe,arguments:runArgs,exit:ran.status,error:ran.error?.message,runMs});
  assert(fs.existsSync(terminal),'Terminal report required; unsupported OS is never a pass');const report=json(terminal);
  assert(!report.failures.some(f=>f.assertion.startsWith('fixture-exception:')),'Setup/OS/fixture exceptions never count as semantic detection');
  let accepted;
  if(variant.before){accepted=ran.status===1&&report.failures.some(f=>f.case===variant.requiredCase);assert(accepted,'Required original crash assertions must catch compiled mutant');}
  else{
   const crash=report.processObservations.find(o=>o.case==='crash/ExitProcess-kills-child-before-late-write');
   accepted=ran.status===0&&report.pass&&crash?.childExitConfirmedBeforeLateWriteRequest&&!crash?.lateWriteExecuted&&!crash?.observerHoldsJobHandle&&!crash?.cleanupJobUsed;
  }
  const receipt={variant:variant.name,originalVariant:variant.originalVariant,pass:variant.before?undefined:report.pass,accepted,expectedSemanticFailure:!!variant.before,executedThisRun:true,caseCount:report.caseCount,assertions:report.assertions,failures:report.failures.length,notExecutedCases:report.notExecutedCases,compilerInputs:compiled.length,binarySha256:sha(exe),result:terminal,timingMs:{build:buildMs,closure:closureMs,execute:runMs,total:Date.now()-variantStart}};
  results.push(receipt);console.log(JSON.stringify(receipt));
 }
 const sourceDrift=inputs.filter(f=>sha(f.path)!==f.sha256),pass=sourceDrift.length===0&&results.every(r=>r.accepted);
 save(path.join(out,'RESULT.json'),{pass,trackingOnly,results,inputs,sourceDrift,toolchain:{path:toolchain,manifestSha256:sha(path.join(toolchain,'toolchain-manifest.json')),identities},totalMs:Date.now()-started,realPrivateJobProcesses:true,observerHoldsJobHandleBeforeParentDeath:false,cleanupJobUsed:false,deviceApiCalls:0,realCliOrHostLaunched:false,mainIntegrated:false,queryJobLifelineExperiment:'NOT_RUN',jobAbsenceIsIdleProof:false,scope:'private controlled consumer of actual product JobGuard; file lease exclusion is independent writer quiet proof only, not full job idle or device recovery'});
 process.exitCode=pass?0:1;
}catch(error){save(path.join(out,'FAILED.json'),{pass:false,error:error.stack,results,inputs,totalMs:Date.now()-started});throw error;}