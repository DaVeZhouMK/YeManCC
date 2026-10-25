import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const opts={},args=process.argv.slice(2),allowed=new Set(['--out','--toolchain','--main','--before-main','--prior-function','--prior-globals']);
for(let i=0;i<args.length;i+=2){assert(allowed.has(args[i])&&args[i+1]&&!opts[args[i]],'Unknown, duplicate or incomplete option');assert(path.isAbsolute(args[i+1]),'All paths must be explicit absolute paths');opts[args[i]]=path.resolve(args[i+1]);}
assert(opts['--out']&&opts['--toolchain'],'Use --out ABS_FRESH_PRIVATE_DIR --toolchain ABS_CAPTURED_DIR [--main ABS_SOURCE] [--before-main ABS_OLD_SOURCE] [--prior-function ABS_OLD_FUNCTION --prior-globals ABS_OLD_GLOBALS]');
assert(!!opts['--prior-function']===!!opts['--prior-globals'],'Prior function/globals must be supplied together');assert(!opts['--prior-function']||opts['--before-main'],'Prior candidate needs its own original main context');
const out=opts['--out'],toolchain=opts['--toolchain'],main=opts['--main']??path.join(repo,'native/main.cpp');
const inside=(p,r)=>{const rel=path.relative(r,p);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
assert(!fs.existsSync(out)&&out!==path.parse(out).root&&!inside(out,repo)&&!inside(out,toolchain),'Output must be fresh, private and outside product/toolchain');
assert(fs.existsSync(path.join(toolchain,'build-environment.json'))&&fs.existsSync(path.join(toolchain,'toolchain-manifest.json')),'Captured toolchain must exist');fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=p=>sha(fs.readFileSync(p));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
const sourceInputs=[],mutableInputs=[];
function freeze(file,label,current=true){const bytes=fs.readFileSync(file),snapshot=path.join(out,'inputs',label);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes);const item={path:file,snapshot,sha256:sha(bytes),bytes:bytes.length};sourceInputs.push(item);if(current)mutableInputs.push(item);return bytes;}
function select(text){
 const method=extract(text,'hidHideRun'),cli=extract(text,'hidHideCliPath');
 const matches=[...text.matchAll(/^static std::mutex g_hidHideMx;\r*$/gm)];assert.equal(matches.length,1,'One actual command mutex');const mutex=matches[0][0].replace(/\r$/,'');
 const deadlines=[...text.matchAll(/^static thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline\s*=\s*0;\r*$/gm)];assert.equal(deadlines.length,1,'One actual inherited deadline');
 const deadline=deadlines[0][0].replace(/\r$/,''),at=text.indexOf(method.code),begin=matches[0].index+matches[0][0].length;
 assert(at>begin&&at-begin<32768,'CLI adjacency must be a bounded local preamble');
 const adjacent=text.slice(begin,at),fenced=/hidHideCliWriterRuntime|hidhideCliWriter::|hidhideCliWriterRuntime/.test(method.code+adjacent);
 const execution = fenced ? [...text.matchAll(/^static thread_local std::uint64_t g_inputHostSwitchRecoveryStartRequestId[^;\n]*;\r*$/gm), ...text.matchAll(/^static ymcc::VirtualStartProtection g_virtualStartProtection;\r*$/gm)].map(m=>m[0].replace(/\r+$/, '')) : []; if(fenced)assert.equal(execution.length,2,'Actual execution request/power/protection declarations');
 const reap=fenced?extract(text,'hidHideCliReapPendingLocked'):null;if(reap)assert(adjacent.includes(reap.code),'Actual reap helper in frozen CLI adjacency');
 const globals=deadline+'\n'+adjacent;
 const declarations=[...adjacent.matchAll(/^static[^;\r\n]*\bg_[A-Za-z0-9_]+[^;]*;/gm)].map(m=>({code:m[0],sha256:sha(m[0])}));
 if(fenced){assert.equal([...text.matchAll(/^#include "hidhide_cli_writer_runtime\.h"\r*$/gm)].length,1,'Runtime expanded only from actual main include adjacency');assert(/JobGuard/.test(adjacent)&&/Session/.test(adjacent),'Actual pending JobGuard and Session declarations');}
 return {method,cli,mutex:{code:mutex,sha256:sha(mutex)},deadline:{code:deadline,sha256:sha(deadline)},adjacent:{code:adjacent,sha256:sha(adjacent)},globals,declarations,fenced,execution,reap};
}
function consumerFrom(file,label){const bytes=freeze(file,label,false);return{...select(bytes.toString('utf8')),main:file,wholeSourceSha256:sha(bytes)};}
const current=consumerFrom(main,'main.source.cpp'),before=opts['--before-main']?consumerFrom(opts['--before-main'],'before-main.source.cpp'):null;
const prior=opts['--prior-function']?{method:freeze(opts['--prior-function'],'prior-function.inc').toString('utf8'),globals:freeze(opts['--prior-globals'],'prior-globals.inc').toString('utf8')}:null;
const fixture=freeze(path.join(repo,'tools/fixtures/hidhide_cli_lifetime/fixture.cpp'),'fixture.source.cpp').toString('utf8');
const ordinaryAt=fixture.indexOf('void ordinary('),ordinaryEnd=fixture.indexOf('\n}\n}',ordinaryAt);assert(ordinaryAt>=0&&ordinaryEnd>ordinaryAt);const originalOrdinarySha256='d1deb3ba1510aaa7dc2999d363ef9a44fcbeda1ceaa03c556c91f429c6518a78';assert.equal(sha(fixture.slice(ordinaryAt,ordinaryEnd+2)),originalOrdinarySha256,'Original 28-case assertion body is frozen');
const jsonBytes=freeze(path.join(repo,'deps/json/json.hpp'),'json.hpp');
for(const rel of ['tools/hidhide_cli_lifetime_selftest.mjs','tools/fixtures/hidhide_cli_lifetime/README.md','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'])freeze(path.join(repo,rel),rel);
const headers=new Map(),runtimeAdaptations=[];
function header(file){const rel=path.relative(path.join(repo,'native'),file);assert(!rel.startsWith('..')&&!path.isAbsolute(rel),'Native header closure stays in native');if(headers.has(rel))return;
 const raw=freeze(file,'native/'+rel).toString('utf8');headers.set(rel,{raw,adapted:raw,path:file});
 for(const m of raw.matchAll(/^#include "([^"\r\n]+)"/gm)){if(m[1]==='json.hpp')continue;const next=path.resolve(path.dirname(file),m[1]);assert(fs.existsSync(next),'Explicit actual local header '+next);header(next);}
}
if(current.fenced){
 header(path.join(repo,'native/hidhide_cli_writer_runtime.h'));header(path.join(repo,'native/hidhide_cli_job_guard.h'));header(path.join(repo,'native/virtual_start_protection.h'));
 const row=headers.get('hidhide_cli_writer_runtime.h');
 const replacements={
  hidHideCliWriterRecordPath:'static std::wstring hidHideCliWriterRecordPath() { return privateWriter::recordPath(); }',
  hidHideCliWriterMachineBinding:'static std::string hidHideCliWriterMachineBinding() { return privateWriter::machineBinding(); }',
  hidHideCliWriterCapture:'static std::optional<ymcc::targetReturn::ProcessIdentity> hidHideCliWriterCapture(HANDLE process) { return privateWriter::capture(process); }',
  hidHideCliWriterProbe:'static ymcc::targetReturn::process::Probe hidHideCliWriterProbe(const ymcc::targetReturn::ProcessIdentity& identity) { return privateWriter::probe(identity); }',
  hidHideCliWriterProbeJob:'static ymcc::hidHideCliWriterRuntime::JobState hidHideCliWriterProbeJob(const std::string& name) { return static_cast<ymcc::hidHideCliWriterRuntime::JobState>(privateWriter::probeJob(name)); }'
 };
 for(const [symbol,code]of Object.entries(replacements)){const selected=extract(row.raw,symbol);assert.equal(row.adapted.split(selected.code).length-1,1,'One actual runtime leaf '+symbol);row.adapted=row.adapted.replace(selected.code,code);runtimeAdaptations.push({symbol,actual:selected,replacement:code,replacementSha256:sha(code)});}
 let reconstructed=row.adapted;for(const r of [...runtimeAdaptations].reverse())reconstructed=reconstructed.replace(r.replacement,r.actual.code);assert.equal(reconstructed,row.raw,'Only the five approved runtime leaves change');
}
save(out+'/SOURCE-MANIFEST.json',{inputs:sourceInputs,selected:current,runtimeAdaptations,localNativeClosure:[...headers.keys()],originalOrdinarySha256,wholeCurrentMainApproval:false});
const capture=read(path.join(toolchain,'build-environment.json')),tm=read(path.join(toolchain,'toolchain-manifest.json')),env={...capture.environment};
for(const k of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(k))delete env[k];
env.INCLUDE=tm.roots.filter(x=>x.category==='INCLUDE').map(x=>x.frozen).join(';');env.LIB=tm.roots.filter(x=>x.category==='LIB').map(x=>x.frozen).join(';');env.LIBPATH=env.LIB;
const cl=capture.tools['cl.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0];for(const tool of[cl,dump,capture.tools['link.exe'].paths[0]]){const bound=tm.tools.find(x=>path.resolve(x.original).toLowerCase()===path.resolve(tool).toLowerCase());assert(bound&&hash(tool)===bound.sha256,'Captured compiler/link/import tool identity');}
const byTool=new Map(tm.files.map(x=>[path.resolve(x.frozen).toLowerCase(),x]));
const legacyCases=['expired-deadline','deadline-during-setup','busy','unknown-child-retained','termination-accepted-still-live','wait-failed-retained','tail-output','output-read-failure','output-peek-failure','output-zero-read','timeout-confirmed','exit-query-failure','output-budget','output-limit','success','error-exit','missing-cli','pipe-failure','create-failure','inherit-failure','default-arguments','empty-output','fallback-unknown-no-extra-wait','exception-live-retained','tail-after-empty-peek','shared-expired-default','shared-shorter-than-explicit','explicit-shorter-than-shared'];
const fenceCases=['writer-open-fails-zero-create','writer-reserve-fails-zero-create','writer-mayrun-fails-zero-resume','writer-commit-expiry-zero-resume','writer-unknown-session-retained','writer-normal-settle','writer-settle-fails-retains-debt','writer-stale-child-alive-blocks','writer-stale-child-unknown-blocks','writer-stale-child-dead-admits','writer-postcommit-power-stale','writer-proven-exit-retains-only-debt'];
const variants=[...(before?[{name:'Before-Release',source:before,old:true,flags:['/O2']},{name:'Before-Checked',source:before,old:true,flags:['/Od','/RTC1']}]:[]),...(prior?[{name:'Prior-candidate-race',source:{method:{code:prior.method},globals:before.deadline.code+'\n'+prior.globals,cli:before.cli,mutex:before.mutex,fenced:false},required:'tail-after-empty-peek',prior:true,flags:['/O2']}]:[]),
 {name:'Current-Release',source:current,flags:['/O2']},{name:'Current-Checked',source:current,flags:['/Od','/RTC1']},
 {name:'negative-expired-admission',source:current,flags:['/O2'],before:'if (expired()) { if (exitCodeOut) *exitCodeOut = WAIT_TIMEOUT; return false; }',after:'if (false) { if (exitCodeOut) *exitCodeOut = WAIT_TIMEOUT; return false; }',required:'expired-deadline'},
 {name:'negative-live-handle-release',source:current,flags:['/O2'],global:true,before:'if (pi.hProcess && !exitProven)',after:'if (false && pi.hProcess && !exitProven)',required:'unknown-child-retained'},
 {name:'negative-stale-empty-peek',source:current,flags:['/O2'],before:'scope.exitProven && exitObservedBeforePeek && available == 0',after:'scope.exitProven && available == 0',required:'tail-after-empty-peek'},
 {name:'negative-no-command-lock',source:current,flags:['/O2'],lockFault:true,required:'busy'},
 {name:'negative-ignore-inherited-deadline',source:current,flags:['/O2'],before:'if (executionDeadline && (!absoluteDeadlineTick || executionDeadline < absoluteDeadlineTick))',after:'if (false && executionDeadline && (!absoluteDeadlineTick || executionDeadline < absoluteDeadlineTick))',required:'shared-expired-default'}];

if(current.fenced)variants.push(
 {name:'negative-writer-reserve-bypass',source:current,flags:['/O2'],before:'if(!scope.writer->reserve(scope.writer->record()->revision,reservation))',after:'if(false)',required:'writer-reserve-fails-zero-create',requiredLabel:'failed durable reservation dispatches zero child',caseNames:['writer-reserve-fails-zero-create']},
 {name:'negative-writer-mayrun-bypass',source:current,flags:['/O2'],before:'if(!child||!scope.writer->markMayRun(scope.writer->record()->revision,scope.attemptId,*child))',after:'if(!child)',required:'writer-mayrun-fails-zero-resume',requiredLabel:'failed MayRun commit never resumes suspended fake child',caseNames:['writer-mayrun-fails-zero-resume']},
 {name:'negative-writer-postcommit-deadline',source:current,flags:['/O2'],postcommit:'deadline',required:'writer-commit-expiry-zero-resume',requiredLabel:'committed but stale command never resumes child',caseNames:['writer-commit-expiry-zero-resume']},
 {name:'negative-writer-postcommit-currentness',source:current,flags:['/O2'],postcommit:'currentness',required:'writer-postcommit-power-stale',requiredLabel:'committed but stale command never resumes child',caseNames:['writer-postcommit-power-stale']},
 {name:'negative-writer-lease-release',source:current,flags:['/O2'],global:true,before:'g_hidHidePendingWriter=std::move(writer);',after:'writer.reset();',required:'writer-unknown-session-retained',requiredLabel:'unknown keeps real owned job and actual Session/attempt',caseNames:['writer-unknown-session-retained']},
 {name:'negative-writer-settle-bypass',source:current,flags:['/O2'],global:true,before:'ymcc::hidHideCliWriterRuntime::settleOwned(*writer,attemptId)',after:'true',required:'writer-normal-settle',requiredLabel:'normal completion actually settles durable attempt',caseNames:['writer-normal-settle']}
);
const originalRequiredLabels={'negative-expired-admission':'expired deadline never creates process','negative-live-handle-release':'exact old process handle remains owned','negative-stale-empty-peek':'post-exit peek captures tail written after prior empty peek','negative-no-command-lock':'busy does not dispatch a process','negative-ignore-inherited-deadline':'expired shared context never creates CLI'};
for(const v of variants)if(originalRequiredLabels[v.name])v.requiredLabel=originalRequiredLabels[v.name];
const results=[];
try{
 for(const v of variants){
  const dir=path.join(out,v.name);fs.mkdirSync(dir);let body=v.source.method.code,globals=v.source.globals;
  if(v.before){let text=v.global?globals:body; if(v.source.fenced){
    if(v.name==='negative-expired-admission'){const re=/if\s*\(\s*expired\(\)\s*\)/g;assert([...text.matchAll(re)].length>=3);text=text.replace(re,'if(false)');v.applied=true;}
    if(v.name==='negative-live-handle-release'){const re=/if\s*\(\s*!exitProven\s*(?:\|\|\s*writerActive\(\)\s*)?\)\s*\{/g;assert.equal([...text.matchAll(re)].length,1);text=text.replace(re,'if(false){');v.applied=true;}
    if(v.name==='negative-stale-empty-peek'){const re=/scope\.exitProven\s*&&\s*exitObservedBeforePeek\s*&&\s*available\s*==\s*0/g;assert.equal([...text.matchAll(re)].length,1);text=text.replace(re,'scope.exitProven&&available==0');v.applied=true;}
    if(v.name==='negative-ignore-inherited-deadline'){const re=/if\s*\(executionDeadline\s*&&\s*\(!absoluteDeadlineTick\s*\|\|\s*executionDeadline\s*<\s*absoluteDeadlineTick\)\)/g;assert.equal([...text.matchAll(re)].length,1);text=text.replace(re,'if(false)');v.applied=true;}
   } if(!v.applied){const count=text.split(v.before).length-1;assert(count>0,'Exact semantic fault anchor '+v.name);if(v.name==='negative-expired-admission')assert(count>=3,'Preserve original three deadline guards');else assert.equal(count,1,'Unique semantic fault anchor '+v.name);text=text.split(v.before).join(v.after);}if(v.global)globals=text;else body=text;}
  if(v.postcommit){const end=body.indexOf('if(ResumeThread(scope.pi.hThread)!=1)');assert(end>=0,'Actual Resume anchor');let begin=body.lastIndexOf('if(expired())',end);assert(begin>=0&&end-begin<320,'Final original deadline adjacent to Resume');let slice=body.slice(begin,end);const anchor=v.postcommit==='deadline'?'if(expired())':'if(!currentRequest())';assert.equal(slice.split(anchor).length-1,1);slice=slice.replace(anchor,'if(false)');body=body.slice(0,begin)+slice+body.slice(end);}
  if(v.lockFault){assert.equal(body.split('std::try_to_lock').length-1,1);const re=/if\s*\(!commandOwner\.owns_lock\(\)\)/g;assert.equal([...body.matchAll(re)].length,1);body=body.replace('std::try_to_lock','std::defer_lock').replace(re,'if(false)');}
  const content={'fixture.cpp':fixture,'json.hpp':jsonBytes,'cli-path.inc':v.source.cli.code,'mutex.inc':v.source.mutex.code,'globals.inc':globals,'consumer.inc':body,'execution-globals.inc':(v.source.execution??[]).join('\n')};
  if(v.source.fenced)for(const [rel,row]of headers)content['native/'+rel]=row.adapted;
  const generated=Object.entries(content).map(([rel,bytes])=>{const file=path.join(dir,rel);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return{path:file,sha256:hash(file)};});
  save(dir+'/GENERATED.json',{generated,semanticFault:v.required?v.name:null,runtimeAdaptations:v.source.fenced?runtimeAdaptations:[],fullMainExpanded:false});
  const exe=path.join(dir,'CliFixture.exe'),command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4',...v.flags,...(v.source.fenced?['/DCLI_WRITER_FENCE_ENABLED=1']:[]),'/sourceDependencies',dir+'/dependencies.json',dir+'/fixture.cpp','/I'+dir,'/Fo'+dir+'/fixture.obj','/Fe'+exe,'/link','/VERBOSE:LIB'];
  const built=spawnSync(cl,command,{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:16*1024*1024}),buildLog=(built.stdout??'')+(built.stderr??'');fs.writeFileSync(dir+'/build.log',buildLog);save(dir+'/COMMAND.json',{executable:cl,arguments:command,exit:built.status,error:built.error?.message});assert.equal(built.status,0,'Compile failed '+dir+'/build.log');
  const byGenerated=new Map(generated.map(x=>[path.resolve(x.path).toLowerCase(),x])),dependency=read(dir+'/dependencies.json');
  const actualInputs=[dependency.Data.Source,...dependency.Data.Includes].map(file=>{const key=path.resolve(file).toLowerCase(),bound=byGenerated.get(key)??byTool.get(key);assert(bound&&hash(file)===bound.sha256,'Unbound/changed compiler input '+file);return{path:file,sha256:bound.sha256,scope:byGenerated.has(key)?'actual frozen source':'frozen toolchain'};});
  if(v.source.fenced)for(const rel of headers.keys())assert(actualInputs.some(x=>x.path.toLowerCase()===path.join(dir,'native',rel).toLowerCase()),'Required actual header input '+rel);
  const libPaths=[...new Set([...buildLog.matchAll(/([A-Za-z]:[\\/][^\r\n]*?\.lib)(?=[:\s(]|$)/gi)].map(m=>m[1].trim()))];assert(libPaths.length>=3,'Actual linker library observations');
  const libraries=libPaths.map(file=>{const bound=byTool.get(path.resolve(file).toLowerCase());assert(bound&&hash(file)===bound.sha256,'Unbound actual link input '+file);return{path:file,sha256:bound.sha256};});
  const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:10000});fs.writeFileSync(dir+'/imports.log',(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0);const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];assert.deepEqual(dlls,['kernel32.dll']);
  save(dir+'/COMPILE-CLOSURE.json',{inputs:actualInputs,libraries,unbound:[],binarySha256:hash(exe),staticImportedDlls:dlls,runtimeModulesTraced:false});
  const rows=[],cases=v.caseNames??[...legacyCases,...(v.source.fenced?fenceCases:[])];
  for(const name of cases){const privateDir=path.join(dir,'private-files',name);fs.mkdirSync(privateDir,{recursive:true});const ran=spawnSync(exe,[name,privateDir],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:15000,maxBuffer:16*1024*1024});fs.writeFileSync(dir+'/'+name+'.stdout.log',ran.stdout??'');fs.writeFileSync(dir+'/'+name+'.stderr.log',ran.stderr??'');assert(ran.status===0||ran.status===1,'Crash/timeout is not semantic fault proof '+name);const row=JSON.parse(ran.stdout);assert.equal(ran.status,row.pass?0:1);rows.push(row);save(dir+'/'+name+'.json',row);}
  const failed=rows.filter(r=>!r.pass),r={variant:v.name,old:!!v.old,prior:!!v.prior,semanticFault:!v.prior&&!!v.required,pass:failed.length===0,caseCount:rows.length,checks:rows.reduce((n,r)=>n+r.checks,0),legacyCases:rows.filter(r=>legacyCases.includes(r.case)).length,legacyChecks:rows.filter(r=>legacyCases.includes(r.case)).reduce((n,r)=>n+r.checks,0),fenceCases:rows.filter(r=>fenceCases.includes(r.case)).length,failedCases:failed.map(r=>r.case),assertionFailures:rows.reduce((n,r)=>n+r.failures.length,0),cases:rows,compilerInputs:actualInputs.length,libraries:libraries.length,binarySha256:hash(exe),realCliRuns:0,deviceApiCalls:0};
  save(dir+'/RESULT.json',r);results.push({...r,cases:undefined,result:dir+'/RESULT.json'});console.log(JSON.stringify({variant:r.variant,cases:r.caseCount,checks:r.checks,legacyChecks:r.legacyChecks,failedCases:r.failedCases,assertionFailures:r.assertionFailures,compilerInputs:r.compilerInputs}));
  r.expectedOutcomeObserved=v.old?failed.length>0:v.required?failed.some(row=>row.case===v.required&&(!v.requiredLabel||row.failures.includes(v.requiredLabel))):failed.length===0; r.requiredCase=v.required??null;r.requiredLabel=v.requiredLabel??null;save(dir+'/RESULT.json',r);Object.assign(results[results.length-1],{expectedOutcomeObserved:r.expectedOutcomeObserved,requiredCase:r.requiredCase,requiredLabel:r.requiredLabel});
 }
 const selectedNow=select(fs.readFileSync(main,'utf8')),currentState={methodUnchanged:selectedNow.method.sha256===current.method.sha256,cliPathUnchanged:selectedNow.cli.sha256===current.cli.sha256,mutexUnchanged:selectedNow.mutex.sha256===current.mutex.sha256,deadlineUnchanged:selectedNow.deadline.sha256===current.deadline.sha256,adjacentUnchanged:selectedNow.adjacent.sha256===current.adjacent.sha256,executionUnchanged:JSON.stringify(selectedNow.execution)===JSON.stringify(current.execution),wholeMainSha256:hash(main),wholeFileChanged:hash(main)!==current.wholeSourceSha256};
 save(out+'/SOURCE-CURRENT-OBSERVATION.json',currentState);assert(Object.entries(currentState).filter(([k])=>k.endsWith('Unchanged')).every(([,v])=>v),'Selected consumer changed during run');const toolDrift=mutableInputs.filter(i=>hash(i.path)!==i.sha256);assert.equal(toolDrift.length,0,'Actual source/generator/fixture changed during run');
 const pass=results.every(r=>r.expectedOutcomeObserved);save(out+'/RESULT.json',{status:pass?'PASS_SELECTED_CLI_LIFETIME_SCOPE':'FAIL_SELECTED_CLI_LIFETIME_SCOPE',pass,results,selectedSource:main,selectedProductionMain:main===path.resolve(repo,'native/main.cpp'),consumerSha256:current.method.sha256,sourceInputs,selectedSourceCurrent:currentState,runtimeAdaptations,toolDrift,wholeCurrentMainApproval:false,toolchain:{path:toolchain,manifestSha256:hash(path.join(toolchain,'toolchain-manifest.json'))},eachCaseFreshNativeProcess:true,noStateResetInsideRecoveryCase:true,realMutexBusyTest:true,realPrivateWriterSession:current.fenced,realEmptyPrivateJobGuard:current.fenced,externalLeaves:'modeled CLI filesystem/process/pipe/clock/ResumeThread; exactly five runtime environment leaves',actualJobContainmentValidatedHere:false,noActualCliOrHostOrDeviceRuns:true,staticImportScopeOnly:true});if(!pass)throw new Error('One or more actual consumer / exact-mutant expected outcomes failed; all completed results retained');
}catch(e){save(out+'/FAILED.json',{pass:false,error:e.stack,results,selectedConsumerSha256:current.method.sha256,sourceInputs,productOrDeviceActions:0});throw e;}
