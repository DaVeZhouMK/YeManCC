import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {REPO,FIXTURE,hash,read} from './contract.mjs';
// Explicit small mode of the existing battery contract check. It tests the
// current native boundary with the actual published library; it never sends
// a charge/display operation or starts the product UI / historical pipe host.
export async function runInProcess(args){
 const option=name=>{const n=args.indexOf(name);assert(n>=0&&args[n+1],name+' is required');return path.resolve(args[n+1]);};
 const out=option('--out-dir'),power=option('--power-root');fs.mkdirSync(out,{recursive:true});
 const tool=read(path.join(REPO,FIXTURE,'toolchain-inputs.json'));
 const input=['native/battery_charge_client.h','native/battery_managed_runtime.h','native/battery_host_contract.h','deps/json/json.hpp',FIXTURE+'/InProcessSmoke.cpp',FIXTURE+'/in-process.mjs'];
 const sourceHashes=Object.fromEntries(input.map(file=>[file,hash(path.join(REPO,file))]));
 const report={status:'FAIL',scope:'Actual hostfxr/DLL in native process, identity-only. No real hardware, product Main, UI or full legacy suite.',hardwareOperations:0,childHelpersCreated:0,sourceHashes,cases:[],executions:[],tools:{cl:{path:tool.cl,sha256:hash(tool.cl)},dotnet:{path:tool.dotnet,sha256:hash(tool.dotnet)}},errors:[]};
 function execute(name,exe,argv,options={}){const start=Date.now(),r=spawnSync(exe,argv,{cwd:out,windowsHide:true,encoding:'utf8',timeout:options.timeout??20000,...options}),entry={name,program:exe,args:argv,exitCode:r.status,signal:r.signal,error:r.error?String(r.error):null,elapsedMs:Date.now()-start,stdout:r.stdout,stderr:r.stderr};report.executions.push(entry);fs.writeFileSync(path.join(out,name+'.process.json'),JSON.stringify(entry,null,2));assert.equal(r.status,0,name+' failed: '+r.stdout+r.stderr+r.error);return r;}
 try{
  for(const variant of ['normal','wrong-contract']){
   const include=path.join(out,variant);fs.mkdirSync(include,{recursive:true});
   for(const file of input.filter(file=>file.endsWith('.h')||file.endsWith('.hpp'))){let source=fs.readFileSync(path.join(REPO,file),'utf8');if(variant==='wrong-contract'&&file==='native/battery_host_contract.h')source=source.replace(/kHostSourceId="[a-f0-9]{64}"/,'kHostSourceId="'+'0'.repeat(64)+'"');fs.writeFileSync(path.join(include,path.basename(file)),source);}
   const executable=path.join(include,'battery-in-process-test.exe'),env={...process.env,PATH:[path.dirname(tool.cl),path.join(process.env.SystemRoot||'C:/Windows','System32')].join(path.delimiter),INCLUDE:tool.includeDirs.join(';'),LIB:tool.libDirs.join(';')};
   execute('compile-'+variant,tool.cl,['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/MT','/O2','/I'+include,path.join(REPO,FIXTURE,'InProcessSmoke.cpp'),'/Fo'+path.join(include,'test.obj'),'/Fe'+executable,'/link','advapi32.lib'],{env,timeout:60000});
   const modes=variant==='normal'?['unloaded-stop','missing','normal']:['mismatch'];
   for(const mode of modes){const state=path.join(out,'state-'+mode);fs.mkdirSync(state,{recursive:true});const payload=mode==='missing'?path.join(out,'empty-power'):power;const r=execute('run-'+mode,executable,[mode,payload,state]);const result=JSON.parse(r.stdout);assert.equal(result.status,'PASS');report.cases.push({...result,executableSha256:hash(executable)});}
  }
  assert.deepEqual(Object.fromEntries(input.map(file=>[file,hash(path.join(REPO,file))])),sourceHashes,'Source changed during native execution');report.status='PASS';
 }catch(error){report.errors.push(String(error.stack||error));process.exitCode=1;}
 report.finishedAt=new Date().toISOString();fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({status:report.status,cases:report.cases.length,out,errors:report.errors},null,2));return report;
}
