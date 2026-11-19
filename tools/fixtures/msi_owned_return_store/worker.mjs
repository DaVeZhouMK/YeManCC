import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {verifyOriginalRealStoreCase} from './verify_case.mjs';
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),pin=file=>{const bytes=fs.readFileSync(file);return {path:path.resolve(file),bytes:bytes.length,sha256:sha(bytes)};};
const save=(file,value)=>{const fd=fs.openSync(file,'wx');try{fs.writeFileSync(fd,typeof value==='string'||Buffer.isBuffer(value)?value:JSON.stringify(value,null,2)+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}};
export async function executeRealStorePlan(planFile){
 const plan=read(planFile),output=path.dirname(planFile),started=Date.now();
 assert(path.resolve(plan.run)===output&&path.resolve(plan.binding.path)===path.join(output,'REAL-STORE-SOURCES.json'),'ORIGINAL_OWNED_COMPONENT_PATH_REQUIRED');
 assert.equal(pin(plan.binding.path).sha256,plan.binding.sha256,'PREPARED_SOURCE_BINDING_CHANGED');
 const binding=read(plan.binding.path);assert.equal(path.resolve(binding.run),output);assert(['Release','Checked'].includes(plan.profile));
assert(plan.kind==='ORIGINAL_OWNED_RETURN_REAL_STORE'&&Date.now()+150000<plan.deadlineEpochMs,'ASSESSED_ORIGINAL_ENTRY_PLAN_REQUIRED');
const result={status:'RUNNING_ORIGINAL_REAL_STORE_COMPONENT',atUtc:new Date().toISOString(),sourceBinding:pin(plan.binding.path),sourceReady:false,logicalOnAccepted:false,physicalReaderAccepted:false,steps:[],cases:[],compilerInputs:[],errors:[]};
const key=file=>path.resolve(file).toLowerCase(),inside=(root,file)=>{const rel=path.relative(root,file);return rel!==''&&!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel);};
const artifacts=new Map(plan.toolArtifacts.map(row=>[key(row.path),row]));
const declaredSources=new Map(plan.sourceInputs.map(row=>[key(row.path),row]));
function check(row){const actual=pin(row.path);assert.equal(actual.sha256,row.sha256,'INPUT_CHANGED:'+row.path);if(row.bytes!==undefined)assert.equal(actual.bytes,row.bytes);return actual;}
function bind(file,kind){const actual=pin(file),declared=declaredSources.get(key(file))??artifacts.get(key(file));assert(declared&&declared.sha256===actual.sha256,'UNPINNED_COMPILER_INPUT:'+file);result.compilerInputs.push({...actual,kind});}
function command(name,program,args,cwd,timeoutMs){
 const begin=performance.now(),binary=spawnSync(program,args,{cwd,windowsHide:true,timeout:timeoutMs,maxBuffer:16*1024*1024});
 const stdout=path.join(cwd,name+'.stdout.raw'),stderr=path.join(cwd,name+'.stderr.raw');save(stdout,binary.stdout??Buffer.alloc(0));save(stderr,binary.stderr??Buffer.alloc(0));
 const run={...binary,stdout:binary.stdout?.toString('utf8')??'',stderr:binary.stderr?.toString('utf8')??''};
 const row={name,program:pin(program),args,cwd,timeoutMs,elapsedMs:performance.now()-begin,exitCode:run.status,signal:run.signal,error:run.error?.message??null,pid:run.pid,stdout:pin(stdout),stderr:pin(stderr)};
 result.steps.push(row);save(path.join(cwd,name+'.command.json'),row);return {run,row};
}
function quote(value){assert(!/["\r\n%]/.test(value));return '"'+value+'"';}
try{
 for(const row of plan.sourceInputs)check(row);for(const row of plan.toolArtifacts)check(row);
 let exe;
 if(plan.reuseCompile){
  check(plan.reuseCompile);const previous=read(plan.reuseCompile.path);assert.equal(previous.compiler.build.exitCode,0);check(previous.compiler.executable);
  for(const row of previous.compilerInputs){check(row);bind(row.path,row.kind);}for(const row of previous.compiler.dependencies)check(row);
  assert(previous.compilerInputs.some(row=>key(row.path)===key(binding.consumer.path)&&row.sha256===binding.consumer.sha256),'SAME_EXACT_CONSUMER_BINARY_REQUIRED');
  for(const file of [binding.writerInclude.path,binding.businessInclude.path])assert(previous.compilerInputs.some(row=>key(row.path)===key(file)&&row.sha256===pin(file).sha256),'SAME_VERBATIM_STORE_INPUTS_REQUIRED');
  exe=previous.compiler.executable.path;result.compiler={...previous.compiler,reference:plan.reuseCompile,executedThisRun:false,referenceOnlyForThisComponent:true};
 }else{
 const toolchain=read(plan.toolchain.path),build=path.join(binding.run,'build'),deps=path.join(build,'deps');assert(!fs.existsSync(build));fs.mkdirSync(build);fs.mkdirSync(deps);
 exe=path.join(build,'selftest.exe');const cpps=[binding.consumer.path,...binding.bridgeRows.filter(row=>row.candidate.path.endsWith('.cpp')).map(row=>row.candidate.path)];assert.equal(cpps.length,2);
 const options=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-','/sdl','/Zc:__cplusplus',...(plan.profile==='Release'?['/O2','/MT','/DNDEBUG']:['/Od','/MTd','/RTC1','/Zi','/D_DEBUG']),
 '/external:I'+quote(path.join(binding.snapshot,'deps/json')),'/external:W0','/showIncludes','/sourceDependencies',quote(deps.replaceAll('\\','/')+'/'),...cpps.map(quote),
 '/Fo'+quote(build.replaceAll('\\','/')+'/'),'/Fe'+quote(exe),'/Fd'+quote(path.join(build,'selftest.pdb')),'/link','/verbose:lib','/SUBSYSTEM:CONSOLE','/MACHINE:x64','/INCREMENTAL:NO'];
 const windows=path.resolve(toolchain.windowsRoot),msvc=path.resolve(toolchain.msvcRoot),sdk=path.resolve(toolchain.sdkRoot),sdkVersion=toolchain.sdkVersion;
 assert(/^[0-9]+(?:\.[0-9]+){3}$/.test(sdkVersion),'FIXED_SDK_VERSION_REQUIRED');
 const include=[...['ucrt','shared','um','winrt','cppwinrt'].map(name=>path.join(sdk,'Include',sdkVersion,name)),path.join(msvc,'include')];
 const libraryDirectories=[path.join(sdk,'Lib',sdkVersion,'ucrt/x64'),path.join(sdk,'Lib',sdkVersion,'um/x64'),path.join(msvc,'lib/x64')];
 const compiler=path.join(msvc,'bin/Hostx64/x64/cl.exe');check(artifacts.get(key(compiler)));
 const preamble=['@echo off','set "PATH='+[path.join(msvc,'bin/Hostx64/x64'),path.join(windows,'System32'),windows].join(';')+'"','set "INCLUDE='+include.join(';')+'"','set "LIB='+libraryDirectories.join(';')+'"'].join('\r\n')+'\r\n';
 const batch=path.join(build,'build.cmd');save(batch,preamble+quote(compiler)+' '+options.join(' ')+'\r\nexit /b %errorlevel%\r\n');
 const {run:compiled,row:buildRow}=command('build',plan.cmd,['/d','/c',batch],build,120000);assert.equal(compiled.error,undefined,'COMPILER_SPAWN_MUST_COMPLETE');assert.equal(compiled.signal,null);assert.equal(compiled.status,0,'BUILD_FAILURE_IS_NOT_A_SEMANTIC_NEGATIVE');
 const dependencyFiles=fs.readdirSync(deps).filter(name=>name.endsWith('.json')).map(name=>path.join(deps,name));assert.equal(dependencyFiles.length,2,'TWO_ACTUAL_TRANSLATION_UNIT_GRAPHS_REQUIRED');
 const observed=new Set();
 for(const file of dependencyFiles){const graph=read(file);assert(Array.isArray(graph.Data?.Includes));bind(graph.Data.Source,'Compile');for(const input of graph.Data.Includes){bind(input,'Include');observed.add(key(input));}}
 for(const file of [binding.writerInclude.path,binding.businessInclude.path,path.join(binding.snapshot,'native/msi_owned_return_runtime.h')])assert(observed.has(key(file)),'ACTUAL_ORIGINAL_FUNCTION_INCLUDE_REQUIRED:'+file);
 let libraries=0;for(const match of compiled.stdout.matchAll(/([A-Za-z]:\\[^\r\n]*?\.lib)/g))if(fs.existsSync(match[1])){bind(match[1],'Library');libraries++;}assert(libraries>0,'OBSERVED_LIBRARY_CLOSURE_REQUIRED');
 result.compiler={build:buildRow,executable:pin(exe),dependencies:dependencyFiles.map(pin),inputOccurrences:result.compilerInputs.length,uniqueInputs:new Set(result.compilerInputs.map(row=>key(row.path))).size,libraries};
 }
 const runtime=path.join(binding.run,'runtime');fs.mkdirSync(runtime);
 for(const parameter of plan.parameters){
  const directory=path.join(runtime,parameter);fs.mkdirSync(directory);
  const {run:native,row}=command('native',exe,[directory,parameter],directory,10000);assert.equal(native.error,undefined);assert.equal(native.signal,null);
  let raw;try{raw=JSON.parse(native.stdout);}catch{throw Error('RETURNED_NATIVE_REPORT_MISSING:'+parameter+':exit='+native.status);}
  const rawFile=path.join(directory,'NATIVE-RESULT.json');save(rawFile,raw);const accepted={parameter,process:row,raw:pin(rawFile),accepted:false};result.cases.push(accepted);
  const journal=path.join(directory,'msi-controller-return.json');verifyOriginalRealStoreCase({raw,parameter,processRow:row,journalPath:journal,originalCase:binding.originalSelectedCase.name});
  assert.equal(fs.readdirSync(directory).filter(name=>name.startsWith('msi-controller-return.json.ymcc.')).length,0,'ORIGINAL_WRITER_TEMPORARIES_MUST_BE_CLEANED');
  accepted.accepted=true;accepted.assertions=raw.assertions;accepted.journal=pin(journal);accepted.io=raw.finalIo;
 }
 for(const row of plan.sourceInputs)check(row);for(const row of plan.toolArtifacts)check(row);
 result.status=plan.parameters.length===2?'TWO_ORIGINAL_LOADED_OFF_OWNER_REAL_FILE_PARAMETERS_VERIFIED':'ONE_SELECTED_ORIGINAL_LOADED_OFF_OWNER_REAL_FILE_PARAMETER_VERIFIED';result.componentAccepted=true;result.completeTwoParameterRun=plan.parameters.length===2;
}catch(error){result.status='FAIL_ORIGINAL_REAL_STORE_COMPONENT';result.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
result.finishedAtUtc=new Date().toISOString();result.wallMs=Date.now()-started;save(path.join(output,'COMPONENT-RESULT.json'),result);
console.log(JSON.stringify({status:result.status,cases:result.cases.length,accepted:result.cases.filter(row=>row.accepted).length,wallMs:result.wallMs,errors:result.errors.map(row=>row.message)}));return result;
}
