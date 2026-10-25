import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {REPO, sourceInputs, toolInputs} from './adapter.mjs';
import {consumerEnvironment} from './environment.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';
import {inspectAll, sdkBatch} from './consumer-runner.mjs';
const out=process.argv[2];
assert(out&&path.isAbsolute(out),'ABSOLUTE_FRESH_PROBE_OUTPUT_REQUIRED');
fs.mkdirSync(out,{recursive:true});assert.equal(fs.readdirSync(out).length,0,'FRESH_PROBE_OUTPUT_REQUIRED');
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const tools=toolInputs(REPO),primary=tools.primary;
const sources=Object.fromEntries(sourceInputs.map(rel=>[rel,hash(path.join(REPO,rel))]));
const workspace=createOwnedWorkspace({repo:REPO,output:out,primary,sourceHashes:sources});
const env=consumerEnvironment({ambient:process.env,tools,out,nodeExecutable:process.execPath});
const result={schemaVersion:1,status:'FAIL',probeOnly:true,productionAcceptance:false,formalGlobalExecuted:false,
  mainSha256:sources['native/main.cpp'],sourceHashes:sources,primaryTools:primary,toolsBefore:tools.files,
  workspace:{root:workspace.root,identity:workspace.identity},invocations:[],cases:[]};
const save=()=>fs.writeFileSync(path.join(out,'TOOL-PROBE.json'),JSON.stringify(result,null,2)+'\n');
function invoke(name,exe,args,childEnv,cwd){
 const started=Date.now(),r=spawnSync(exe,args,{cwd,env:childEnv,windowsHide:true,timeout:60000,maxBuffer:4*1024*1024});
 const raw=Buffer.concat([r.stdout||Buffer.alloc(0),r.stderr||Buffer.alloc(0)]);let text;try{text=new TextDecoder('utf-8',{fatal:true}).decode(raw);}catch{text=new TextDecoder('gbk').decode(raw);}
 fs.writeFileSync(path.join(out,name+'.log'),text);fs.writeFileSync(path.join(out,name+'.raw'),raw);
 const row={name,executable:exe,args,cwd,temp:childEnv.TEMP,tempLength:childEnv.TEMP.length,exitCode:r.status,signal:r.signal,error:r.error?.message??null,elapsedMs:Date.now()-started,log:path.join(out,name+'.log')};result.invocations.push(row);save();return {...row,text};
}
const clean=r=>assert(!r.error&&!r.signal&&r.exitCode===0,'TOOL_PROBE_PROCESS_FAILED:'+r.name);
function exactFile(root,length,name){
 let left=length-root.length-name.length-2;assert(left>0,'PATH_PROBE_LENGTH');let folder=root;
 while(left>80){folder=path.join(folder,'x'.repeat(70));left-=71;}
 folder=path.join(folder,'x'.repeat(left));fs.mkdirSync(folder,{recursive:true});const file=path.join(folder,name);assert.equal(file.length,length);return file;
}
try{
 // These are the real two runner entry points, with a probe-only CLI. They
 // execute pinned git, SDK setup, CL compile/link and one private-temp binary.
 const tasks=[['dsu',primary.node,[path.join(REPO,'tools/fixtures/dsu_lifecycle/run.mjs'),path.join(out,'dsu'),'--tool-probe']],
  ['sensor',primary.python,['-B',path.join(REPO,'tools/input_capture_sensor_owners_selftest.py'),'--output-root',path.join(out,'sensor'),'--tool-probe']]].map(([name,exe,args])=>async()=>{
   const owned=workspace.allocate(name),r=invoke(name,exe,args,{...env,TEMP:owned.temp,TMP:owned.temp},REPO);workspace.verify(owned);clean(r);result.cases.push({name,pass:true,owned});
 });
 await inspectAll(tasks,2);
 const job=workspace.allocate('cl-path-comparison'),nativeEnv={...env,TEMP:job.temp,TMP:job.temp};
 const batch=path.join(job.directory,'sdk.cmd');fs.writeFileSync(batch,sdkBatch(tools)+'set\r\n');
 const setup=invoke('sdk-setup',primary.commandProcessor,['/d','/c',batch],nativeEnv,job.directory);clean(setup);
 for(const line of setup.text.split(/\r?\n/)){const at=line.indexOf('=');if(at>0)nativeEnv[line.slice(0,at)]=line.slice(at+1);}
 const header=path.join(REPO,'native/input_capture_completion.h'),name=path.basename(header);
 const cpp=path.join(job.directory,'h.cpp');fs.writeFileSync(cpp,'#include "'+name+'"\nstatic_assert(ymcc::captureLifecycle::Debt::Motion==512);\n');
 // R2 current's header length is 248; skip-delete is 262 and orphan GM is 280.
 for(const length of [248,262,280]){
  const file=exactFile(job.directory,length,name);fs.copyFileSync(header,file);assert.equal(hash(file),sources['native/input_capture_completion.h']);
  const r=invoke('include-'+length,primary.compiler,['/nologo','/std:c++17','/EHsc','/c','/I'+path.dirname(file),cpp,'/Fo'+path.join(job.directory,'h'+length+'.obj')],nativeEnv,job.directory);
  if(length===248)clean(r);else assert(r.exitCode!==0&&/C1083/.test(r.text),'LONG_INCLUDE_FAILURE_NOT_REPRODUCED');
  result.cases.push({name:'include-'+length,pass:true,actualHeader:file,pathLength:length,fileExists:true,sha256:hash(file),outcome:r.exitCode===0?'compiled':'C1083'});
 }
 const probe=path.join(job.directory,'p.cpp');fs.copyFileSync(path.join(REPO,'tools/ymcc_sim/global/controller-signal-evidence/tool-probe.cpp'),probe);
 for(const length of [211,280]){
  const marker=exactFile(job.directory,length+2,'m'),temp=path.dirname(marker);assert.equal(temp.length,length);
  const r=invoke('temp-'+length,primary.compiler,['/nologo','/std:c++17','/EHsc','/O2','/MT',probe,'/Fo'+path.join(job.directory,'t'+length+'.obj'),'/Fe'+path.join(job.directory,'t'+length+'.exe')],{...nativeEnv,TEMP:temp,TMP:temp},job.directory);
  if(length===211)clean(r);else assert(r.exitCode!==0&&/D8018/.test(r.text),'LONG_TEMP_FAILURE_NOT_REPRODUCED');
  result.cases.push({name:'temp-'+length,pass:true,temp,pathLength:length,directoryExists:true,outcome:r.exitCode===0?'compiled':'D8018'});
 }
 const short=invoke('short-temp-and-include',primary.compiler,['/nologo','/std:c++17','/EHsc','/c','/I'+path.dirname(header),cpp,'/Fo'+path.join(job.directory,'short.obj')],nativeEnv,job.directory);clean(short);
 result.cases.push({name:'short-temp-and-include',pass:true,temp:job.temp,tempLength:job.temp.length});
 workspace.verify(job);result.toolsAfter=toolInputs(REPO).files;
 assert.deepEqual(result.toolsAfter,result.toolsBefore,'PROBE_TOOL_IDENTITIES_CHANGED');
 result.sourceDrift=Object.entries(sources).filter(([rel,h])=>hash(path.join(REPO,rel))!==h).map(([rel])=>rel);
 assert.equal(result.sourceDrift.length,0,'PROBE_SOURCE_DRIFT');result.status='TOOL_PROBE_PASS';
}catch(error){result.error=error.stack;process.exitCode=1;}
finally{workspace.verify();save();console.log(JSON.stringify({status:result.status,probeOnly:true,caseCount:result.cases.length,error:result.error,path:path.join(out,'TOOL-PROBE.json')},null,2));}
