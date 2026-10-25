if(require('node:worker_threads').isMainThread){
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),Module=require('node:module'),vm=require('node:vm'),cp=require('node:child_process'),{fileURLToPath}=require('node:url');
const nativeRead=fs.readFileSync.bind(fs),nativeWrite=fs.writeFileSync.bind(fs),nativeLoad=Module._load,nativeVm=vm.runInNewContext,nativeExec=cp.execFileSync,nativeOpen=fs.openSync.bind(fs),nativeWriteSync=fs.writeSync.bind(fs),nativeAsyncRead=fs.promises.readFile.bind(fs.promises),nativeCallbackRead=fs.readFile.bind(fs);
const configFile=process.env.YMCC_STEAM_DRAFT_OBSERVER_CONFIG;
if(!configFile||!path.isAbsolute(configFile))throw Error('STEAM_DRAFT_OBSERVER_CONFIG_REQUIRED');
const cfg=JSON.parse(nativeRead(configFile,'utf8')),hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex'),key=file=>path.resolve(file).toLowerCase();
const pins=new Map(cfg.reads.map(row=>[key(fs.realpathSync(row.path)),row]));
const producerText=nativeRead(cfg.producer,'utf8');
const trace={schemaVersion:1,pid:process.pid,nodeExecutable:process.execPath,cwd:process.cwd(),producer:cfg.producer,producerSha256:hash(nativeRead(cfg.producer)),product:cfg.product,productSha256:hash(nativeRead(cfg.product)),sourceReads:[],toolLoads:[],writes:[],compilerProcesses:[],transforms:[],vmRuns:[],forbidden:[],failure:null,ports:'original producer memory fixture unchanged',physicalIo:false};
const writeFds=new Set();
function ownedWrite(file,...args){if(key(path.dirname(file))!==key(cfg.stepRoot))deny('OBSERVER_WRITE_SCOPE',file);const fd=nativeOpen(file,'w');writeFds.add(fd);try{return nativeWrite(fd,...args);}finally{writeFds.delete(fd);fs.closeSync(fd);}}
function deny(kind,value){trace.forbidden.push({kind,value:String(value)});throw Error('STEAM_DRAFT_OBSERVER_FORBIDDEN_'+kind);}
function checkRead(value,kind='sourceReads'){
 const file=value instanceof URL?fileURLToPath(value):typeof value==='string'?path.resolve(value):null;
 if(!file)return deny('NON_PATH_READ',value);
 let actual;try{actual=fs.realpathSync(file);}catch{return deny('MISSING_READ',file);}
 const pin=pins.get(key(actual));if(!pin)return deny('UNDECLARED_READ',file);
 const fd=nativeOpen(actual,'r');let sha256;try{sha256=hash(nativeRead(fd));}finally{fs.closeSync(fd);}if(sha256!==pin.sha256)return deny('READ_CHANGED',actual);
 trace[kind].push({path:file,realPath:actual,sha256,role:pin.role});return actual;
}
fs.readFileSync=function(file,...args){checkRead(file);return nativeRead(file,...args);};
fs.promises.readFile=async function(file,...args){checkRead(file);return nativeAsyncRead(file,...args);};
fs.readFile=function(file,...args){checkRead(file);return nativeCallbackRead(file,...args);};
fs.writeFileSync=function(file,...args){if(typeof file!=='string'||key(file)!==key(cfg.raw))deny('WRITE',file);if(key(fs.realpathSync(path.dirname(file)))!==key(cfg.stepRoot))deny('WRITE_FORWARDING',file);trace.writes.push(path.resolve(file));const fd=nativeOpen(file,'w');writeFds.add(fd);try{return nativeWrite(fd,...args);}finally{writeFds.delete(fd);fs.closeSync(fd);}};
fs.openSync=function(file,flags,...args){if(flags!=='r'&&flags!==fs.constants.O_RDONLY)deny('OPEN_WRITE',file);checkRead(file);return nativeOpen(file,flags,...args);};
for(const member of ['writeFile','appendFile','appendFileSync','mkdir','mkdirSync','mkdtemp','mkdtempSync','unlink','unlinkSync','rm','rmSync','rmdir','rmdirSync','rename','renameSync','copyFile','copyFileSync','truncate','truncateSync','link','linkSync','symlink','symlinkSync','createWriteStream','write'])if(typeof fs[member]==='function')fs[member]=()=>deny('FS_MUTATION',member);
for(const member of ['writeFile','appendFile','mkdir','mkdtemp','unlink','rm','rmdir','rename','copyFile','truncate','link','symlink','open'])if(typeof fs.promises[member]==='function')fs.promises[member]=async()=>deny('ASYNC_FS',member);
fs.writeSync=function(fd,...args){if(!writeFds.has(fd))deny('WRITE_FD',fd);return nativeWriteSync(fd,...args);};
let transformedCode=null;
Module._load=function(name,parent,isMain){const resolved=Module._resolveFilename(name,parent,isMain);if(typeof resolved==='string'&&path.isAbsolute(resolved))checkRead(resolved,'toolLoads');return nativeLoad.apply(this,arguments);};
const actualCompiler=require(cfg.compilerEntry);
module.exports.transformSync=function(input,options){
 if(trace.transforms.length!==0||typeof input!=='string'||JSON.stringify(options)!==JSON.stringify({loader:'ts',format:'cjs'}))deny('TRANSFORM_CONTRACT','unexpected transform');
 const row={inputSha256:hash(input),inputBytes:Buffer.byteLength(input),options,compilerEntry:cfg.compilerEntry,success:false};trace.transforms.push(row);
 if(row.inputSha256!==trace.productSha256)deny('TRANSFORM_SOURCE','not actual draft bytes');
 ownedWrite(path.join(cfg.stepRoot,'transform-input.ts'),input);
 const result=actualCompiler.transformSync(input,options);
 row.success=true;row.outputSha256=hash(result.code);row.outputBytes=Buffer.byteLength(result.code);row.mapSha256=hash(result.map);row.warnings=result.warnings;
 transformedCode=result.code;ownedWrite(path.join(cfg.stepRoot,'transform-output.cjs'),result.code);ownedWrite(path.join(cfg.stepRoot,'transform-output.map'),result.map);return result;
};
vm.runInNewContext=function(code,context,...args){
 if(trace.vmRuns.length!==0||code!==transformedCode)deny('VM_CONTRACT','code differs from observed transform output');
 const row={codeSha256:hash(code),contextKeys:Object.keys(context),requireBoundary:'original producer require fixture; no native service imported',requireFunctionSha256:hash(Function.prototype.toString.call(context.require)),success:false};trace.vmRuns.push(row);
 const value=Reflect.apply(nativeVm,this,[code,context,...args]);row.success=true;row.exportedCreateAppearanceDraft=typeof context.module.exports.createAppearanceDraft==='function';return value;
};
cp.execFileSync=function(executable,args,options){
 if(key(executable)!==key(cfg.compilerBinary)||JSON.stringify(args)!==JSON.stringify(['--service='+cfg.compilerVersion])||trace.compilerProcesses.length!==0)deny('PROCESS',executable);
 const pin=pins.get(key(fs.realpathSync(executable)));if(!pin||hash(nativeRead(executable))!==pin.sha256)deny('COMPILER_IDENTITY',executable);
 const row={executable,args,cwd:options.cwd,pid:null,pidObservation:'Node original execFileSync API does not expose child PID',startedAt:new Date().toISOString(),inputSha256:hash(options.input),inputBytes:options.input.length,exitCode:null};trace.compilerProcesses.push(row);
 ownedWrite(path.join(cfg.stepRoot,'compiler-request.bin'),options.input);
 try{const result=Reflect.apply(nativeExec,cp,[executable,args,options]);row.exitCode=0;row.outputSha256=hash(result);row.outputBytes=result.length;ownedWrite(path.join(cfg.stepRoot,'compiler-response.bin'),result);return result;}
 catch(error){row.exitCode=error.status??null;row.error=String(error.message);throw error;}
 finally{row.finishedAt=new Date().toISOString();}
};
for(const member of ['spawn','spawnSync','exec','execSync','execFile','fork'])cp[member]=()=>deny('PROCESS',member);
process.kill=()=>deny('PROCESS_CONTROL','kill');
for(const name of ['node:http','node:https','node:net','node:tls','node:dgram','node:dns']){const api=require(name);for(const member of ['request','get','connect','createConnection','createServer','createSocket','lookup','resolve'])if(typeof api[member]==='function')api[member]=()=>deny('NETWORK',name+':'+member);}
globalThis.fetch=()=>deny('NETWORK','fetch');
// Preload the exact declared CJS entry so Node's ESM translator observes its exports proxy.
require(cfg.compilerEntry);trace.compilerEntryPreloadedForObservation=true;
Module.syncBuiltinESMExports();
process.on('uncaughtExceptionMonitor',error=>{
 const stack=String(error.stack??error),frames=[...stack.matchAll(/file:\/\/\/(.+?):(\d+):(\d+)/g)];
 let originalNamedCheck=null,producerFrame=null;
 for(const frame of frames){let actual;try{actual=fileURLToPath('file:///'+frame[1]);}catch{continue;}if(key(actual)!==key(cfg.producer))continue;
  const line=Number(frame[2]),column=Number(frame[3]),lines=producerText.split('\n');const prefix=lines.slice(0,line-1).join('\n')+(line>1?'\n':'')+lines[line-1].slice(0,column);
  const names=[...prefix.matchAll(/await check\('([^']+)'/g)];originalNamedCheck=names.at(-1)?.[1]??null;producerFrame={path:cfg.producer,line,column};break;
 }
 trace.failure={name:error.name,code:error.code??null,message:error.message,stack,originalNamedCheck,producerFrame};
});
process.on('exit',exitCode=>{trace.exitCode=exitCode;trace.finishedAt=new Date().toISOString();ownedWrite(cfg.trace,JSON.stringify(trace,null,2)+'\n');});





}

