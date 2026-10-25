import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const opts={},argv=process.argv.slice(2),allowed=new Set(['--out','--toolchain','--main']);
for(let i=0;i<argv.length;i+=2){assert(allowed.has(argv[i])&&argv[i+1]&&!opts[argv[i]],'Unknown/duplicate/incomplete argument');assert(path.isAbsolute(argv[i+1]),'Explicit absolute paths required');opts[argv[i]]=path.resolve(argv[i+1]);}
assert(opts['--out']&&opts['--toolchain'],'Use --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_PRODUCTION100 [--main ABS_MAIN]');
const out=opts['--out'],toolchain=opts['--toolchain'],main=opts['--main']??path.join(repo,'native/main.cpp');
const inside=(p,root)=>{const rel=path.relative(root,p);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
assert(!fs.existsSync(out)&&out!==path.parse(out).root&&!inside(out,repo)&&!inside(out,toolchain),'Output must be fresh, private and outside product/toolchain');
fs.mkdirSync(out,{recursive:true});
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex'),hash=p=>sha(fs.readFileSync(p)),read=p=>fs.readFileSync(p,'utf8'),json=p=>JSON.parse(read(p).replace(/^\uFEFF/,'')),save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
const inputs=[];
function freeze(p,label,check=true){const bytes=fs.readFileSync(p),snapshot=path.join(out,'inputs',label);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes);inputs.push({path:p,snapshot,sha256:sha(bytes),bytes:bytes.length,check});return bytes;}
function declaration(text,name){const re=new RegExp('^static[^\\r\\n;]*\\b'+name+'\\b[^\\r\\n;]*;','gm'),found=[...text.matchAll(re)];assert.equal(found.length,1,'One actual declaration '+name);const code=found[0][0];return {symbol:name,kind:'actual declaration',code,sha256:sha(code),startUtf8Byte:Buffer.byteLength(text.slice(0,found[0].index))};}
function select(text){
 const convertersBegin=text.indexOf('static std::string W2U(const wchar_t*'),u2w=extract(text,'U2W');assert(convertersBegin>=0&&u2w.endUtf8ByteExclusive>convertersBegin);
 const converterEnd=text.indexOf(u2w.code)+u2w.code.length,converterCode=text.slice(convertersBegin,converterEnd);
 const context=[{symbol:'W2U and U2W',kind:'actual converter range',code:converterCode,sha256:sha(converterCode)},declaration(text,'g_powerGeneration'),extract(text,'currentPowerGeneration'),declaration(text,'g_virtualStartProtection'),declaration(text,'g_inputHostSwitchRecoveryStartDeadline'),declaration(text,'g_inputHostSwitchRecoveryStartRequestId')];
 const consumer=[...['g_hidHideMx','g_hidHidePendingChildProcess','g_hidHidePendingReadPipe','g_hidHidePendingJob','g_hidHidePendingWriter','g_hidHidePendingAttempt'].map(n=>declaration(text,n)),extract(text,'inputHostNewId'),extract(text,'HidHideCliCallHandles','struct'),extract(text,'hidHideCliReapPendingLocked'),extract(text,'hidHideRun')];
 return {context,consumer,cli:extract(text,'hidHideCliPath')};
}
const mainBytes=freeze(main,'main.source.cpp',false),selected=select(mainBytes.toString('utf8'));
const files=['native/process_efficiency.h','native/virtual_start_protection.h','native/hidhide_cli_job_guard.h','native/hidhide_cli_writer_record.h','native/hidhide_cli_writer_store.h','native/hidhide_cli_writer_runtime.h','native/virtual_target_return_record.h','native/virtual_target_return_store.h','native/virtual_target_return_process.h','deps/json/json.hpp','tools/fixtures/hidhide_cli_fenced_process/fixture.cpp'];
const copied=files.map(relative=>({relative,name:path.basename(relative),bytes:freeze(path.join(repo,relative),relative)}));
for(const relative of ['tools/hidhide_cli_fenced_process_selftest.mjs','tools/fixtures/hidhide_cli_fenced_process/README.md','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'])freeze(path.join(repo,relative),relative);
let runtime=copied.find(x=>x.name==='hidhide_cli_writer_runtime.h').bytes.toString('utf8');
const pathLeaf=extract(runtime,'hidHideCliWriterRecordPath'),bindingLeaf=extract(runtime,'hidHideCliWriterMachineBinding');
const substitutions=[{leaf:pathLeaf.symbol,original:pathLeaf,replacement:'static std::wstring hidHideCliWriterRecordPath() { return file(L"writer.json"); }'},{leaf:bindingLeaf.symbol,original:bindingLeaf,replacement:'static std::string hidHideCliWriterMachineBinding() { return binding(); }'}];
for(const leaf of substitutions){assert.equal(runtime.split(leaf.original.code).length,2);runtime=runtime.replace(leaf.original.code,leaf.replacement);}
const context=selected.context.map(x=>x.code).join('\n\n')+'\n',consumer=selected.consumer.map(x=>x.code).join('\n\n')+'\n';
save(path.join(out,'SOURCE-MANIFEST.json'),{inputs,wholeMainSha256:sha(mainBytes),selected,substitutions,controlledLeaves:[{leaf:'hidHideCliPath',original:selected.cli,replacement:'own private fixture executable --fake-cli role'},{leaf:'appendNativeLifecycleLog',replacement:'private in-memory notes included in each native receipt; no product log path'}],actualUnmodifiedAlgorithms:['hidHideRun','HidHideCliCallHandles','hidHideCliReapPendingLocked','inputHostNewId','currentPowerGeneration','real VirtualStartProtection','JobGuard','writer Record/Session/store','writer runtime except explicit private path and binding','process identity capture/probe and job probe','processEfficiency::createProcess','Win32 clock/pipes/process/job/file operations'],stdinScope:'Actual hidHideRun has no stdin pipe; long command arguments and complete stdout/stderr are tested. stdin is not claimed.'});
const capture=json(path.join(toolchain,'build-environment.json')),tm=json(path.join(toolchain,'toolchain-manifest.json')),env={...capture.environment};
for(const key of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(key))delete env[key];
env.INCLUDE=tm.roots.filter(x=>x.category==='INCLUDE').map(x=>x.frozen).join(';');env.LIB=tm.roots.filter(x=>x.category==='LIB').map(x=>x.frozen).join(';');env.LIBPATH=env.LIB;env.TEMP=path.join(out,'temp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
const cl=capture.tools['cl.exe'].paths[0],link=capture.tools['link.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0],tools=[cl,link,dump].map(p=>{const bound=tm.tools.find(t=>path.resolve(t.original).toLowerCase()===path.resolve(p).toLowerCase());assert(bound&&hash(p)===bound.sha256,'Captured tool identity '+p);return {path:p,sha256:bound.sha256};}),byTool=new Map(tm.files.map(x=>[path.resolve(x.frozen).toLowerCase(),x]));
const cases=['normal-long-io','crash-cold-restart','live-owner-exclusion','root-exit-job-active-reap'];
const variants=[{name:'Release',flags:['/O2'],cases},{name:'Checked',flags:['/Od','/RTC1'],cases},{name:'negative-no-may-run',flags:['/O2'],cases:['normal-long-io'],negative:true}],results=[];
try{
 for(const v of variants){
  const dir=path.join(out,v.name);fs.mkdirSync(dir);let body=consumer;const mutation={before:'if(!child||!scope.writer->markMayRun(scope.writer->record()->revision,scope.attemptId,*child)) {',after:'if(!child) {'};
  if(v.negative){assert.equal(body.split(mutation.before).length,2,'Exact MayRun mutation anchor');body=body.replace(mutation.before,mutation.after);}
  const generated=[];
  for(const entry of [...copied.filter(x=>x.name!=='hidhide_cli_writer_runtime.h'),{name:'hidhide_cli_writer_runtime.h',bytes:runtime},{name:'actual_context.inc',bytes:context},{name:'actual_consumer.inc',bytes:body}]){const p=path.join(dir,entry.name);fs.writeFileSync(p,entry.bytes);generated.push({path:p,sha256:hash(p),original:entry.relative??'source extraction or declared path/binding substitution'});}
  save(path.join(dir,'GENERATED-MANIFEST.json'),{files:generated,mutation:v.negative?mutation:null});
  const exe=path.join(dir,'private-fenced-cli122.exe'),command=['/nologo','/std:c++20','/EHsc','/W4','/utf-8','/MT',...v.flags,'/sourceDependencies','dependencies.json','fixture.cpp','/Fe:private-fenced-cli122.exe','/Fo:fixture.obj','/link','ole32.lib','/VERBOSE:LIB'];
  const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});fs.writeFileSync(path.join(dir,'build.log'),(built.stdout??'')+(built.stderr??''));save(path.join(dir,'COMMAND.json'),{executable:cl,arguments:command,exit:built.status,error:built.error?.message});assert.equal(built.status,0,'Actual compilation failed: '+path.join(dir,'build.log'));
  const generatedMap=new Map(generated.map(x=>[path.resolve(x.path).toLowerCase(),x])),dependency=json(path.join(dir,'dependencies.json'));
  const actualInputs=[dependency.Data.Source,...dependency.Data.Includes].map(p=>{const key=path.resolve(p).toLowerCase(),bound=generatedMap.get(key)??byTool.get(key);assert(bound&&hash(p)===bound.sha256,'Unbound or changed actual compiler input '+p);return {path:p,sha256:bound.sha256,scope:generatedMap.has(key)?'bound source':'captured toolchain'};});
  const libraryPaths=[...new Set([...((built.stdout??'')+'\n'+(built.stderr??'')).matchAll(/([A-Za-z]:[\\/][^\r\n]*?\.lib)(?=[:\s(]|$)/gi)].map(m=>m[1].trim()))];assert(libraryPaths.length,'Actual linker library inputs required');
  const libraries=libraryPaths.map(p=>{const bound=byTool.get(path.resolve(p).toLowerCase());assert(bound&&hash(p)===bound.sha256,'Unbound/changed linker input '+p);return {path:p,sha256:bound.sha256};});
  const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(path.join(dir,'imports.log'),(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0);const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))].sort();assert.deepEqual(dlls,['kernel32.dll','ole32.dll']);
  save(path.join(dir,'COMPILE-CLOSURE.json'),{inputs:actualInputs,libraries,tools,binarySha256:hash(exe),staticImportedDlls:dlls,unbound:[],dynamicModuleTrace:false,scope:'Actual private process/job/file/event operations and original CoCreateGuid ID generation, no device or product imports.'});
  const rows=[];
  for(const name of v.cases){
   const root=path.join(dir,name);fs.mkdirSync(root);const ran=spawnSync(exe,['--observe',name,root],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(root,'stdout.log'),ran.stdout??'');fs.writeFileSync(path.join(root,'stderr.log'),ran.stderr??'');
   assert(ran.status===0||ran.status===1,'Crash/timeout is not semantic evidence: '+root+' '+ran.error?.message);const row=json(path.join(root,'observer-result.json'));assert.equal(row.pass,ran.status===0);assert(!row.infrastructureException,'Actual execution infrastructure failed: '+row.infrastructureException);assert(row.noOwnedProcessesRemain,'Private process leak');assert(!row.observerHoldsJobHandle&&!row.cleanupJobUsed,'Observer must never retain any job');
   if(v.negative)assert(!row.pass&&row.failures.includes('first instruction sees durable MayRun with exact parent child birth and named job'),'Original first-action assertion must catch omitted durable MayRun');else assert(row.pass,'Actual consumer assertions: '+JSON.stringify(row.failures));
   rows.push({name,pass:row.pass,checks:row.checks,failures:row.failures,result:path.join(root,'observer-result.json'),resultSha256:hash(path.join(root,'observer-result.json')),noOwnedProcessesRemain:row.noOwnedProcessesRemain});
  }
  const receipt={variant:v.name,negative:!!v.negative,expectedOutcomeObserved:true,cases:rows,checks:rows.reduce((s,r)=>s+r.checks,0),compilerInputs:actualInputs.length,libraries:libraries.length,binary:exe,binarySha256:hash(exe)};save(path.join(dir,'RESULT.json'),receipt);results.push(receipt);console.log(JSON.stringify(receipt));
 }
 const now=select(read(main)),current={wholeMainSha256:hash(main),wholeMainChanged:hash(main)!==sha(mainBytes),selectedCurrent:[...now.context,...now.consumer,now.cli].every(n=>[...selected.context,...selected.consumer,selected.cli].find(p=>p.symbol===n.symbol)?.sha256===n.sha256),inputDrift:inputs.filter(i=>i.check&&hash(i.path)!==i.sha256)};save(path.join(out,'SOURCE-CURRENT.json'),current);assert(current.selectedCurrent&&!current.inputDrift.length,'Selected source or native/tool input changed during test');
 save(path.join(out,'RESULT.json'),{task:122,status:'PASS_PRIVATE_ACTUAL_FENCED_PROCESS',pass:true,results,sourceInputs:inputs,wholeMainSha256:sha(mainBytes),current,toolchain:{path:toolchain,manifestSha256:hash(path.join(toolchain,'toolchain-manifest.json')),tools},actualConsumer:true,actualWriterSessionStoreRuntime:true,actualJobGuard:true,observerHoldsJobHandle:false,cleanupJobUsed:false,noOwnedProcessesRemain:true,stdinTested:false,actualCliOrHostRuns:0,deviceApiCalls:0,installedAppDataAccess:false,productionChanges:0,limitations:['stdin is not supplied by the actual hidHideRun; tests use long argv and stdout/stderr','private fake CLI/world, not actual driver/game or installed-product acceptance','static import and compiler/link input closure, not dynamic module trace']});
}catch(error){save(path.join(out,'FAILED.json'),{task:122,pass:false,error:error.stack,results,inputs,productionChanges:0});throw error;}