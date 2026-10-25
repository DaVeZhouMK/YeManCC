'use strict';
// Confines the exact pinned producer/tool program. No untrusted code sandbox claim.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),vm=require('node:vm'),Module=require('node:module'),cp=require('node:child_process'),assert=require('node:assert/strict');
const {fileURLToPath}=require('node:url');
const original={read:fs.readFileSync.bind(fs),write:fs.writeFileSync.bind(fs),open:fs.openSync.bind(fs),readSync:fs.readSync.bind(fs),close:fs.closeSync.bind(fs),writeSync:fs.writeSync.bind(fs),readAsync:fs.promises.readFile.bind(fs.promises),load:Module._load,compile:Module.prototype._compile,run:vm.runInNewContext,exec:cp.execFileSync};
let trusted=0,compiling=false;
const read=(...args)=>{trusted++;try{return original.read(...args);}finally{trusted--;}};
const write=(...args)=>{trusted++;try{return original.write(...args);}finally{trusted--;}};
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const full=f=>path.resolve(f instanceof URL?fileURLToPath(f):Buffer.isBuffer(f)?f.toString():f);
const key=f=>full(f).toLowerCase();
const cfg=JSON.parse(read(process.env.YMCC_TRAY_CONFIG,'utf8'));
const trace={schemaVersion:1,finished:false,naturalExit:false,exitCode:null,reads:[],cjs:[],compilers:[],transforms:[],vm:[],assertions:[],forbidden:[],node:{path:process.execPath,version:process.version},boundary:{workerThreads:false,osLoadedModuleGraph:false,physicalTray:false}};
const allowed=new Map(cfg.reads.map(r=>[key(r.path),r]));
function deny(kind,value){trace.forbidden.push({kind,value:String(value)});throw Error('TRAY_CONTROL_'+kind);}
function checked(file){const p=full(file),real=fs.realpathSync(p),row=allowed.get(key(real));if(!row||digest(read(real))!==row.sha256)deny('READ_IDENTITY',p);if(key(p)!==key(real)&&!(key(real)===key(cfg.compilerEntry)&&key(p)===key(path.join(cfg.view,'node_modules/esbuild/lib/main.js'))))deny('READ_FORWARDING',p);trace.reads.push({path:p,realpath:real,sha256:row.sha256,role:row.role});return row;}
fs.readFileSync=function(file,...args){checked(file);return read(file,...args);};
fs.promises.readFile=async function(file,...args){checked(file);return original.readAsync(file,...args);};
for(const name of ['writeFileSync','appendFileSync','createReadStream','createWriteStream','renameSync','copyFileSync','rmSync','unlinkSync','mkdirSync','rmdirSync','symlinkSync','linkSync','truncateSync','ftruncateSync','chmodSync','chownSync','utimesSync','writeSync','writevSync'])if(typeof fs[name]==='function')fs[name]=()=>deny('FS_MUTATION_OR_RAW_IO',name);
for(const name of ['open','readFile','writeFile','appendFile','rename','copyFile','rm','unlink','mkdir','rmdir','symlink','link','truncate','chmod','chown','utimes','write','read','readv','writev'])if(typeof fs[name]==='function')fs[name]=()=>deny('ASYNC_FS',name);
for(const name of ['open','writeFile','appendFile','rename','copyFile','rm','unlink','mkdir','rmdir','symlink','link','truncate','chmod','chown','utimes'])fs.promises[name]=async()=>deny('PROMISE_FS_MUTATION',name);
if(typeof fs.openAsBlob==='function')fs.openAsBlob=async()=>deny('RAW_OPEN','openAsBlob');
fs.openSync=function(...args){if(!trusted)deny('RAW_OPEN',args[0]);return original.open(...args);};
fs.readSync=function(...args){if(!trusted)deny('RAW_READ',args[0]);return original.readSync(...args);};
fs.closeSync=function(...args){if(!trusted)deny('RAW_CLOSE',args[0]);return original.close(...args);};
fs.writeSync=function(...args){if(!trusted)deny('RAW_WRITE',args[0]);return original.writeSync(...args);};
const builtins=new Set(['fs','path','os','tty','child_process','assert','assert/strict','vm','module','url','crypto']);
Module._load=function(request,parent,isMain){
 const bare=request.replace(/^node:/,'');
 if(Module.isBuiltin(request)){if(!builtins.has(bare))deny('BUILTIN',request);return original.load.apply(this,arguments);}
 const actual=Module._resolveFilename(request,parent,isMain);
 if(request!=='esbuild'||key(actual)!==key(cfg.compilerEntry)||key(fs.realpathSync(actual))!==key(cfg.compilerEntry))deny('NON_BOUND_CJS',request);
 checked(actual);return original.load.apply(this,arguments);
};
Module.prototype._compile=function(code,filename){
 if(key(filename)!==key(cfg.compilerEntry)||digest(code)!==allowed.get(key(cfg.compilerEntry))?.sha256)deny('CJS_CODE',filename);
 trace.cjs.push({path:filename,sha256:digest(code),completed:false});const row=trace.cjs.at(-1);
 const result=original.compile.call(this,code,filename);row.completed=true;
 const transform=this.exports.transformSync;
 // esbuild exports accessors; wrap the returned export object, leaving compiler bytes intact.
 this.exports=new Proxy(this.exports,{get(target,name,receiver){if(name!=='transformSync')return Reflect.get(target,name,receiver);return function(source,options){
  if(digest(source)!==cfg.sourceSha256||JSON.stringify(options)!==JSON.stringify({loader:'ts',format:'cjs',target:'es2022'}))deny('TRANSFORM_INPUT',digest(source));
  const row={ordinal:trace.transforms.length,sourceSha256:digest(source),options,completed:false};trace.transforms.push(row);
  let result;compiling=true;try{result=Reflect.apply(transform,target,arguments);}finally{compiling=false;}
  if(result.code!==cfg.referenceCode)deny('TRANSFORM_CODE',digest(result.code));
  const file=path.join(cfg.generated,'transform-'+row.ordinal+'.cjs');write(file,result.code,{flag:'wx'});
  Object.assign(row,{completed:true,path:file,sha256:digest(result.code),referenceEqual:true});return result;
 };}});return result;
};
for(const name of ['spawn','spawnSync','exec','execSync','execFile','fork'])cp[name]=()=>deny('PROCESS',name);
cp.execFileSync=function(command,args,options){
 if(!compiling||key(command)!==key(cfg.compilerBinary)||key(fs.realpathSync(command))!==key(cfg.compilerBinary)||JSON.stringify(args)!==JSON.stringify(['--service='+cfg.compilerVersion])||options?.shell||key(options?.cwd??'')!==key(cfg.view)||options.windowsHide!==true||!(options.input instanceof Uint8Array)||options.input.length>1024*1024||options.env!==undefined||options.maxBuffer!==16*1024*1024)deny('COMPILER_EXEC_CONTRACT',command);
 const pin=allowed.get(key(command));if(!pin||digest(read(command))!==pin.sha256)deny('COMPILER_BINARY',command);
 const row={path:command,sha256:pin.sha256,args,inputSha256:digest(options.input),inputBytes:options.input.length,completed:false};trace.compilers.push(row);
 const result=original.exec.call(this,command,args,{...options,timeout:5000,shell:false});
 if(digest(read(command))!==pin.sha256)deny('COMPILER_CHANGED',command);
 Object.assign(row,{completed:true,outputSha256:digest(result),outputBytes:result.length});return result;
};
vm.runInNewContext=function(code,context,...args){
 const transform=trace.transforms[trace.vm.length];
 if(!transform?.completed||digest(code)!==transform.sha256||JSON.stringify(Object.keys(context).sort())!==JSON.stringify(['console','exports','module','require'])||context.exports!==context.module.exports)deny('VM_INPUT_OR_MEMORY_CONTRACT',digest(code));
 const row={ordinal:trace.vm.length,sha256:digest(code),completed:false,memoryRequires:[]};trace.vm.push(row);
 const memoryRequire=context.require;
 context.require=function(name){if(!['./api','./settingsRepository'].includes(name))deny('VM_REQUIRE',name);row.memoryRequires.push(name);return Reflect.apply(memoryRequire,this,arguments);};
 try{const result=original.run.call(this,code,context,...args);row.completed=true;return result;}finally{context.require=memoryRequire;}
};
const rejects=assert.rejects;
assert.rejects=async function(...args){try{return await Reflect.apply(rejects,this,args);}catch(e){trace.assertions.push({method:'rejects',name:e.name,code:e.code,message:e.message,stack:e.stack});throw e;}};
const workers=original.load.call(Module,'node:worker_threads',module,false);
workers.Worker=class{constructor(){deny('WORKER_THREAD','Worker');}};
workers.MessageChannel=class{constructor(){deny('WORKER_CHANNEL','MessageChannel');}};
for(const name of ['node:http','node:https','node:net','node:tls','node:dgram','node:http2']){
 // Load through the saved function; these modules are never made available by the consumer require contract.
 const mod=original.load.call(Module,name,module,false);for(const name of ['request','get','connect','createConnection','createServer','createSecureServer','createSocket'])if(typeof mod[name]==='function')mod[name]=()=>deny('NETWORK',name);
}
globalThis.fetch=()=>deny('NETWORK','fetch');globalThis.WebSocket=class{constructor(){deny('NETWORK','WebSocket');}};
process.dlopen=()=>deny('NATIVE_ADDON','dlopen');process.binding=()=>deny('NATIVE_BINDING','binding');process._linkedBinding=()=>deny('NATIVE_BINDING','linkedBinding');
for(const name of ['exit','kill','chdir','abort','_debugProcess','_debugEnd'])if(typeof process[name]==='function')process[name]=()=>deny('PROCESS_CONTROL',name);
Module.syncBuiltinESMExports();
process.on('exit',code=>{trace.finished=true;trace.naturalExit=true;trace.exitCode=code;write(cfg.trace,JSON.stringify(trace,null,2)+'\n',{flag:'wx'});});
