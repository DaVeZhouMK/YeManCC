import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
assert(args.length===4&&args[0]==='--out'&&args[2]==='--toolchain'&&path.isAbsolute(args[1])&&path.isAbsolute(args[3]),'Use --out ABS_FRESH_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY');
const out=path.resolve(args[1]),toolchain=path.resolve(args[3]);assert(!fs.existsSync(out),'Existing evidence must never be overwritten');fs.mkdirSync(out,{recursive:true});
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'),json=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),save=(p,j)=>fs.writeFileSync(p,JSON.stringify(j,null,2)+'\n');
const sourceFiles=['native/virtual_target_return_record.h','native/virtual_target_return_store.h','tools/fixtures/virtual_target_return_record/fixture.cpp','tools/virtual_target_return_record_selftest.mjs','deps/json/json.hpp'];
const inputs=sourceFiles.map(relative=>({relative,path:path.join(repo,relative),sha256:sha(path.join(repo,relative))}));save(out+'/SOURCES.json',{inputs,claimedScopeOnly:true});
const capture=json(toolchain+'/build-environment.json'),manifest=json(toolchain+'/toolchain-manifest.json'),env={...capture.environment};
for(const k of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(k))delete env[k];env.INCLUDE=manifest.roots.filter(r=>r.category==='INCLUDE').map(r=>r.frozen).join(';');env.LIB=manifest.roots.filter(r=>r.category==='LIB').map(r=>r.frozen).join(';');env.LIBPATH=env.LIB;
const cl=capture.tools['cl.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0];for(const tool of[cl,dump,capture.tools['link.exe'].paths[0]]){const binding=manifest.tools.find(t=>path.resolve(t.original).toLowerCase()===path.resolve(tool).toLowerCase());assert(binding&&sha(tool)===binding.sha256,'captured compiler/tool identity');}
const byTool=new Map(manifest.files.map(f=>[path.resolve(f.frozen).toLowerCase(),f]));
const variants=[{name:'Release',flags:['/O2']},{name:'Checked',flags:['/Od','/RTC1']},
{name:'negative-stale-revision',flags:['/O2'],file:'virtual_target_return_record.h',before:'r.revision==expected||',after:'true||',expectedFailure:true,requiredCase:'consume/exact-origin-revision'},
{name:'negative-duplicate-keys',flags:['/O2'],file:'virtual_target_return_record.h',before:'if(duplicate||depthInvalid||',after:'if(depthInvalid||',expectedFailure:true,requiredCase:'decode/duplicate-nested-origin'},
{name:'negative-ignore-flush',flags:['/O2'],file:'virtual_target_return_store.h',before:'if(!FlushFileBuffers(h.value))return fail',after:'if(false && !FlushFileBuffers(h.value))return fail',expectedFailure:true,requiredCase:'IO/failure-flush'}];
const results=[];
try{for(const variant of variants){
 const dir=out+'/'+variant.name;fs.mkdirSync(dir+'/files',{recursive:true});const generated=[];
 for(const relative of sourceFiles.filter(r=>!r.endsWith('.mjs'))){const file=path.join(dir,path.basename(relative));fs.copyFileSync(path.join(repo,relative),file);generated.push({path:file,sha256:sha(file),original:relative});}
 if(variant.file){const file=dir+'/'+variant.file,text=fs.readFileSync(file,'utf8');assert.equal(text.split(variant.before).length,2,'precise semantic fault anchor');fs.writeFileSync(file,text.replace(variant.before,variant.after));generated.find(g=>path.resolve(g.path)===path.resolve(file)).sha256=sha(file);}
 save(dir+'/GENERATED.json',{generated,mutation:variant.expectedFailure?variant:null});
 const exe=dir+'/RecordFixture.exe',command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/DUNICODE','/D_UNICODE',...variant.flags,'/sourceDependencies',dir+'/dependencies.json',dir+'/fixture.cpp','/I'+dir,'/Fo'+dir+'/fixture.obj','/Fe'+exe];
 const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});fs.writeFileSync(dir+'/build.log',(built.stdout??'')+(built.stderr??''));save(dir+'/COMMAND.json',{executable:cl,arguments:command,exit:built.status,error:built.error?.message});assert.equal(built.status,0,'actual compilation failed: '+dir+'/build.log');
 const byGenerated=new Map(generated.map(f=>[path.resolve(f.path).toLowerCase(),f]));const dependency=json(dir+'/dependencies.json');
 const actualInputs=[dependency.Data.Source,...dependency.Data.Includes].map(file=>{const key=path.resolve(file).toLowerCase(),bound=byGenerated.get(key)??byTool.get(key);assert(bound&&sha(file)===bound.sha256,'unbound/changed actual compiler input: '+file);return {path:file,sha256:bound.sha256,scope:byGenerated.has(key)?'actual frozen source':'captured toolchain'};});
 const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(dir+'/imports.log',imports.stdout+imports.stderr);assert.equal(imports.status,0);const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];assert.deepEqual(dlls,['kernel32.dll'],'fixture statically imports no device/SDK libraries');
 save(dir+'/COMPILE-CLOSURE.json',{inputs:actualInputs,unbound:[],compiler:cl,compilerSha256:sha(cl),binarySha256:sha(exe),staticImportedDlls:dlls,scope:'Static imports and exact compiler inputs; runtime CRT module loading is not traced.'});
 const ran=spawnSync(exe,[dir+'/files',dir+'/RESULT.json'],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:20000});fs.writeFileSync(dir+'/run.log',(ran.stdout??'')+(ran.stderr??''));assert(fs.existsSync(dir+'/RESULT.json'),'fixture ended without a terminal report');const result=json(dir+'/RESULT.json');
 if(variant.expectedFailure){assert.equal(ran.status,1,'semantic fault must fail assertions, not compile or crash');assert(result.failures.some(f=>f.case===variant.requiredCase),'fault must trip intended original assertions');}
 else{assert.equal(ran.status,0,'actual fixture assertions failed: '+dir+'/RESULT.json');assert(result.pass);const child=spawnSync(exe,['--child-read',dir+'/files/cross-process.json'],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(dir+'/cross-process.log',child.stdout+child.stderr);assert.equal(child.status,0,'independent-process real-file read');const childResult=JSON.parse(child.stdout);assert(childResult.pass);save(dir+'/CROSS-PROCESS.json',childResult);}
 const receipt={variant:variant.name,passed:!variant.expectedFailure?result.pass:undefined,expectedSemanticFailure:!!variant.expectedFailure,caseCount:result.caseCount,assertions:result.assertions,failures:result.failures.length,compilerInputs:actualInputs.length,binarySha256:sha(exe),result:dir+'/RESULT.json',crossProcessRead:!variant.expectedFailure};results.push(receipt);console.log(JSON.stringify(receipt));
 }
 const drift=inputs.filter(b=>sha(b.path)!==b.sha256);assert.equal(drift.length,0,'claimed source changed during tests');save(out+'/RESULT.json',{pass:true,results,inputs,toolchain:{path:toolchain,manifestSha256:sha(toolchain+'/toolchain-manifest.json')},sourceDrift:drift,realPrivateFileIo:true,physicalDevicesOpened:false,productOrHostLaunched:false,runtimeIntegrated:false});
}catch(error){save(out+'/FAILED.json',{pass:false,error:error.stack,results,inputs});throw error;}
