// Synchronous generator-input observation. This is a source-only diagnostic,
// not a security boundary, compiler result, runtime proof or cache authority.
import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import workers from 'node:worker_threads';
import {syncBuiltinESMExports} from 'node:module';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';

const sha=b=>createHash('sha256').update(b).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const need=(v,c)=>{if(!v)throw Error('GENERATOR_INPUT_'+c);};
const version=s=>[s.ino,s.size,s.mtimeNs,s.ctimeNs].map(String).join(':');
let running=false;

export async function observeGeneratorInputs({writableRoot,operation,compilerRequest}={}){
 need(!running&&typeof operation==='function'&&typeof compilerRequest==='function','EXCLUSIVE_SYNC_OPERATION');
 const original=Object.fromEntries(Object.entries(fs)),originalWorker=workers.Worker,patches=[];
 let internal=0;const privately=fn=>{internal++;try{return fn();}finally{internal--;}};
 const absolute=value=>{
  if(value instanceof URL){need(value.protocol==='file:','FILE_URL_ONLY');value=fileURLToPath(value);}
  need(typeof value==='string'&&value.length>0&&!/[\x00-\x1f]/.test(value)&&!value.startsWith('\\\\'),'ORDINARY_PATH');
  const p=path.resolve(value);need(/^[A-Za-z]:[\\/]/.test(p)&&!p.slice(2).includes(':'),'ORDINARY_DRIVE_PATH');
  const tail=p.slice(3);need(tail===''||tail.split(/[\\/]/).every(x=>x&&!/[. ]$/.test(x)),'PATH_ALIAS');return p;
 };
 const noRedirect=file=>privately(()=>{for(let cursor=file;;cursor=path.dirname(cursor)){
  let s;try{s=original.lstatSync(cursor);}catch(error){if(error.code!=='ENOENT')throw error;}
  if(s)need(!s.isSymbolicLink(),'REDIRECTED_PATH');if(path.dirname(cursor)===cursor)break;
 }});
 writableRoot=absolute(writableRoot);noRedirect(writableRoot);
 need(original.statSync(writableRoot).isDirectory()&&original.readdirSync(writableRoot).length===0,'FRESH_EMPTY_WRITE_ROOT');
 const started=performance.now(),files=new Map(),probes=new Map(),fds=new Map(),writes=[],blocked=[],unsupported=[],pendingReads=new Set(),asyncFailures=[],closeFailures=[],descriptorCleanup=[];
 let descriptorSerial=0,pendingAtOperationEnd=0,drainRounds=0;
 let readCalls=0,readBytes=0,error=null,operationValue,restored=false,closingError=null;
 const writable=value=>{const p=absolute(value);need(inside(writableRoot,p)&&key(p)!==key(writableRoot),'WRITE_OUTSIDE_FRESH_OWNER');noRedirect(p);return p;};
 const stat=file=>privately(()=>original.statSync(file,{bigint:true}));
 function remember(file,bytes,state){
  const pin={path:file,bytes:bytes.length,sha256:sha(bytes)},old=files.get(key(file));
  if(old)need(old.pin.sha256===pin.sha256&&old.pin.bytes===pin.bytes,'INPUT_CHANGED_BETWEEN_READS');
  else files.set(key(file),{pin,version:version(state)});
  return pin;
 }
 function input(file){
  file=absolute(file);noRedirect(file);const before=stat(file);need(before.isFile()&&before.size<=512n*1024n*1024n,'BOUNDED_REGULAR_INPUT');
  const existing=files.get(key(file));if(existing){need(existing.version===version(before),'INPUT_VERSION_CHANGED');return existing.pin;}
  const bytes=privately(()=>original.readFileSync(file)),after=stat(file);need(version(before)===version(after),'INPUT_CHANGED_DURING_READ');return remember(file,bytes,after);
 }
 const fdPath=fd=>{const row=fds.get(fd);need(row,'UNTRACKED_FILE_DESCRIPTOR');return row.path;};
 const patch=(object,name,body)=>{need(typeof object[name]==='function','API_MISSING_'+name);const previous=object[name];patches.push({object,name,descriptor:Object.getOwnPropertyDescriptor(object,name)});object[name]=function(...args){if(internal)return previous.apply(this,args);return body.apply(this,args);};};
 const missing=code=>{unsupported.push({kind:code});throw Error('GENERATOR_INPUT_UNSUPPORTED_'+code);};
 function probe(name,args,result){
  const file=absolute(args[0]);if(inside(writableRoot,file))return;
  const shape=value=>value===undefined?{exists:false}:typeof value==='boolean'?{exists:value}:typeof value==='string'?{path:value}:
   Array.isArray(value)?value.map(row=>typeof row==='string'?row:{name:row.name,file:row.isFile(),directory:row.isDirectory(),link:row.isSymbolicLink()}):
   {file:value.isFile(),directory:value.isDirectory(),link:value.isSymbolicLink(),size:String(value.size),ino:String(value.ino),mtimeMs:String(value.mtimeMs),ctimeMs:String(value.ctimeMs)};
  const options=args[1]&&typeof args[1]==='object'?{bigint:args[1].bigint===true,withFileTypes:args[1].withFileTypes===true,throwIfNoEntry:args[1].throwIfNoEntry}:args[1];
  const id=JSON.stringify([name,file,options??null]),row={api:name,path:file,options:options??null,result:shape(result)},old=probes.get(id);
  if(old)need(JSON.stringify(old.result)===JSON.stringify(row.result),'METADATA_CHANGED_DURING_GENERATION');else probes.set(id,row);
 }
 try{
  running=true;
  patch(fs,'readFileSync',(value,options)=>{
   const file=typeof value==='number'?fdPath(value):absolute(value);noRedirect(file);
   const flag=typeof options==='object'&&options?.flag!==undefined?options.flag:'r';need(['r','rs','sr'].includes(flag),'READ_FILE_WRITE_FLAG');
   if(!inside(writableRoot,file))input(file);
   const before=stat(file),result=privately(()=>original.readFileSync(value,options)),after=stat(file);
   need(version(before)===version(after),'READ_FILE_VERSION_CHANGED');readCalls++;readBytes+=Buffer.isBuffer(result)?result.length:Buffer.byteLength(result);
   return result;
  });
  patch(fs,'openSync',(value,flags,mode)=>{
   const file=absolute(value),write=typeof flags==='string'?/[wa+]/.test(flags):(flags&(fs.constants.O_WRONLY|fs.constants.O_RDWR|fs.constants.O_CREAT|fs.constants.O_TRUNC|fs.constants.O_APPEND))!==0;
   if(write)writable(file);else{noRedirect(file);if(!inside(writableRoot,file))input(file);}
   const fd=privately(()=>original.openSync(value,flags,mode)),row={path:file,write,serial:++descriptorSerial,version:null};fds.set(fd,row);
   row.version=version(privately(()=>original.fstatSync(fd,{bigint:true})));return fd;
  });
  patch(fs,'readSync',(...args)=>{
   const row=fds.get(args[0]);need(row&&!row.write,'TRACKED_READ_DESCRIPTOR');
   need(version(privately(()=>original.fstatSync(args[0],{bigint:true})))===row.version,'READ_DESCRIPTOR_CHANGED');
   const count=privately(()=>original.readSync(...args));readCalls++;readBytes+=count;return count;
  });
  patch(fs,'closeSync',fd=>{const row=fds.get(fd);need(row,'TRACKED_CLOSE_DESCRIPTOR');let first=null,value;
   try{if(!row.write)need(version(privately(()=>original.fstatSync(fd,{bigint:true})))===row.version,'CLOSED_INPUT_CHANGED');}catch(failure){first=failure;}
   try{value=privately(()=>original.closeSync(fd));fds.delete(fd);}catch(failure){closeFailures.push({serial:row.serial,path:row.path,message:failure.message});if(!first)first=failure;}
   if(first)throw first;return value;
  });
  for(const name of ['existsSync','statSync','lstatSync','readdirSync','realpathSync','readlinkSync']){
   patch(fs,name,(...args)=>{const result=privately(()=>original[name](...args));probe(name,args,result);return result;});
  }
  // Keep the native realpath spelling observable rather than silently dropping it.
  if(original.realpathSync.native)fs.realpathSync.native=(...args)=>{const result=privately(()=>original.realpathSync.native(...args));probe('realpathSync',args,result);return result;};
  for(const name of ['writeFileSync','appendFileSync','mkdirSync','unlinkSync','rmSync','rmdirSync','truncateSync','chmodSync','chownSync','utimesSync']){
   if(typeof original[name]!=='function')continue;
   patch(fs,name,(...args)=>{const file=typeof args[0]==='number'?fdPath(args[0]):writable(args[0]);writable(file);writes.push({api:name,path:file});return privately(()=>original[name](...args));});
  }
  patch(fs,'copyFileSync',(from,to,...args)=>{from=absolute(from);to=writable(to);if(!inside(writableRoot,from))input(from);writes.push({api:'copyFileSync',path:to,source:from});return privately(()=>original.copyFileSync(from,to,...args));});
  patch(fs,'renameSync',(from,to)=>{from=writable(from);to=writable(to);writes.push({api:'renameSync',path:to,source:from});return privately(()=>original.renameSync(from,to));});
  for(const name of ['writeSync','writevSync'])if(typeof original[name]==='function')patch(fs,name,(fd,...args)=>{const row=fds.get(fd);need(row?.write,'TRACKED_WRITE_DESCRIPTOR');writable(row.path);return privately(()=>original[name](fd,...args));});
  for(const name of ['cpSync','linkSync','symlinkSync','mkdtempSync','createReadStream','createWriteStream','openAsBlob','watch','watchFile','readFile','writeFile','appendFile','open','read','close','copyFile','rename','readdir','stat','lstat','realpath','unlink','rm','mkdir'])if(typeof original[name]==='function')patch(fs,name,()=>missing('FS_'+name));
  const promiseRead=fs.promises.readFile;
  patch(fs.promises,'readFile',(value,options)=>{
   const file=absolute(value),flag=typeof options==='object'&&options?.flag!==undefined?options.flag:'r';need(['r','rs','sr'].includes(flag),'READ_FILE_WRITE_FLAG');
   noRedirect(file);if(!inside(writableRoot,file))input(file);const before=stat(file);
   const task=privately(()=>promiseRead.call(fs.promises,value,options)).then(result=>{
    need(version(before)===version(stat(file)),'ASYNC_READ_FILE_VERSION_CHANGED');readCalls++;readBytes+=Buffer.isBuffer(result)?result.length:Buffer.byteLength(result);return result;
   });
   pendingReads.add(task);task.then(()=>pendingReads.delete(task),failure=>{asyncFailures.push({path:file,message:failure.message});pendingReads.delete(task);});return task;
  });
  for(const name of Object.keys(fs.promises))if(name!=='readFile'&&typeof fs.promises[name]==='function')patch(fs.promises,name,()=>missing('FS_PROMISE_'+name));
  for(const name of ['spawnSync','spawn','execSync','exec','execFileSync','execFile','fork']){
   patch(childProcess,name,(...args)=>{
    const request={api:name,program:typeof args[0]==='string'?args[0]:null,arguments:Array.isArray(args[1])?args[1]:null};
    const accepted=name==='spawnSync'&&compilerRequest(structuredClone(request))===true;
    blocked.push({...request,expectedCompilerBoundary:accepted});
    throw Error(accepted?'GENERATOR_INPUT_STOP_BEFORE_COMPILER':'GENERATOR_INPUT_UNEXPECTED_CHILD_PROCESS');
   });
  }
  patches.push({object:workers,name:'Worker',descriptor:Object.getOwnPropertyDescriptor(workers,'Worker')});
  workers.Worker=new Proxy(originalWorker,{construct(){return missing('WORKER_THREAD');}});
  syncBuiltinESMExports();
  try{operationValue=await operation();need(pendingReads.size===0,'UNAWAITED_ASYNC_READS');}catch(failure){error={name:failure.name,message:failure.message};}
 }finally{
  try{
   pendingAtOperationEnd=pendingReads.size;
   if(pendingAtOperationEnd)closingError={message:'GENERATOR_INPUT_UNAWAITED_ASYNC_READS',count:pendingAtOperationEnd};
   const drainDeadline=performance.now()+5000;
   while(pendingReads.size){
    if(++drainRounds>64||performance.now()>=drainDeadline){closingError??={message:'GENERATOR_INPUT_ASYNC_DRAIN_NOT_QUIESCENT'};break;}
    let timer;const settled=await Promise.race([Promise.allSettled([...pendingReads]).then(()=>true),new Promise(resolve=>{timer=setTimeout(()=>resolve(false),Math.max(1,drainDeadline-performance.now()));})]);clearTimeout(timer);
    if(!settled){closingError??={message:'GENERATOR_INPUT_ASYNC_DRAIN_TIMEOUT'};break;}
   }
   if(pendingReads.size)closingError??={message:'GENERATOR_INPUT_ASYNC_READS_STILL_PENDING'};
   if(asyncFailures.length)closingError??={message:'GENERATOR_INPUT_ASYNC_READ_FAILURES'};
   for(const {object,name,descriptor}of [...patches].reverse())Object.defineProperty(object,name,descriptor);
   syncBuiltinESMExports();restored=true;
  }finally{running=false;}
 }
 // Closing runs after originals are restored and never changes the first cause.
 try{
  const leftovers=[...fds];
  if(leftovers.length)closingError??={message:'GENERATOR_INPUT_GENERATOR_LEFT_FILE_DESCRIPTORS',count:leftovers.length};
  for(const [fd,row]of leftovers){try{original.closeSync(fd);fds.delete(fd);descriptorCleanup.push({serial:row.serial,path:row.path,closed:true,observerCleanup:true});}catch(failure){descriptorCleanup.push({serial:row.serial,path:row.path,closed:false,observerCleanup:true,message:failure.message});}}
  if(closeFailures.length)closingError??={message:'GENERATOR_INPUT_CLOSE_FAILURES'};
  need(fds.size===0,'GENERATOR_LEFT_FILE_DESCRIPTORS');
  for(const {pin,version:expectedVersion}of files.values()){
   noRedirect(pin.path);const before=stat(pin.path),bytes=original.readFileSync(pin.path),after=stat(pin.path);
   need(version(before)===expectedVersion&&version(after)===expectedVersion&&bytes.length===pin.bytes&&sha(bytes)===pin.sha256,'CLOSING_INPUT_DRIFT');
  }
  for(const row of probes.values()){
   const options=row.options===null?undefined:row.options;
   const result=privately(()=>original[row.api](row.path,options));
   // Reuse the same normalization and exact comparison, with no new filesystem calls.
   probe(row.api,[row.path,options],result);
  }
 }catch(failure){closingError??={name:failure.name,message:failure.message};}
 const stopped=blocked.length===1&&blocked[0].expectedCompilerBoundary===true&&error?.message==='GENERATOR_INPUT_STOP_BEFORE_COMPILER';
 return {schemaVersion:1,kind:'GENERATOR_INPUT_OBSERVATION_V1',status:closingError?'INCOMPLETE_INPUT_CLOSING':stopped?'STOPPED_BEFORE_COMPILER':error?'FAIL':'COMPLETED_WITHOUT_COMPILER',
  inputPins:[...files.values()].map(row=>row.pin),metadataProbes:[...probes.values()],writes,blockedProcessRequests:blocked,unsupported,error,closingError,globalsRestored:restored,
  descriptors:{remaining:fds.size,cleanup:descriptorCleanup,closeFailures},asyncReads:{pendingAtOperationEnd,pendingAtReturn:pendingReads.size,drainRounds,failures:asyncFailures,drainLimitMs:5000},
  readCalls,readBytes,wallMs:performance.now()-started,compilerProcessesStarted:0,nativeFixtureProcessesStarted:0,compilerResultGranted:false,genericCacheQualified:false,
  fullProcessDependencyClosureProven:false,scope:'Observed synchronous filesystem APIs of this invocation; module/parser/native tool closure and current environment require separate binding.'};
}
