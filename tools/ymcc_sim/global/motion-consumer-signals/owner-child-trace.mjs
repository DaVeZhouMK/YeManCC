import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import {syncBuiltinESMExports} from 'node:module';
import {createHash} from 'node:crypto';
// Preload only for the unchanged owner checker. Record its actual synchronous
// compile/native children without changing arguments, budgets or return values.
const index=process.argv.indexOf('--out'),out=process.argv[index+1];
if(index<2||!out||!path.isAbsolute(out))throw Error('OWNER_TRACE_OUTPUT_REQUIRED');
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const digest=file=>hash(fs.readFileSync(file));
const original=childProcess.spawnSync;let sequence=0;
childProcess.spawnSync=function(executable,args,options){
 const i=sequence++,startedAt=Date.now();
 const compiler=i%2===0,mode=i<2?'Release':'Checked';
 const folder=path.join(out,mode),batch=path.join(folder,'compile.cmd'),native=path.join(folder,'probe.exe');
 if(i>=4||options?.timeout!==60000||options?.maxBuffer!==8*1024*1024||path.resolve(options.cwd)!==path.resolve(folder)||
  (compiler?(executable!=='cmd.exe'||(args.length!==3||args[0]!=='/d'||args[1]!=='/c'||path.resolve(args[2])!==path.resolve(batch))):
   (path.resolve(executable)!==path.resolve(native)||args.length!==1)))throw Error('OWNER_TRACE_UNEXPECTED_CHILD');
 const input=compiler?batch:native,beforeSha256=digest(input),r=original.call(this,executable,args,options);
 const streams={stdout:hash(r.stdout??''),stderr:hash(r.stderr??'')};
 const receipt={schemaVersion:1,kind:compiler?'owner-compile-child':'owner-native-child',sequence:i,mode,parentPid:process.pid,pid:r.pid??null,executable,args,cwd:options.cwd,timeoutMs:options.timeout,maxBuffer:options.maxBuffer,startedAt,finishedAt:Date.now(),exitCode:r.status,signal:r.signal??null,error:r.error?{code:r.error.code??'SPAWN_ERROR',message:r.error.message}:null,timedOut:r.error?.code==='ETIMEDOUT',outputTruncated:r.error?.code==='ENOBUFS',input:{path:input,beforeSha256,afterSha256:digest(input)},streams};
 fs.mkdirSync(path.join(out,'child-processes'),{recursive:true});
 fs.writeFileSync(path.join(out,'child-processes',i+'.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});
 return r;
};
syncBuiltinESMExports();
