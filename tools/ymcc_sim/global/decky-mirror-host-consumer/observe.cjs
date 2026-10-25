'use strict';
if(require('node:worker_threads').isMainThread){
// Observation and inert IO fence only. Original scripts and assertions are not rewritten.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),Module=require('node:module'),cp=require('node:child_process'),{fileURLToPath}=require('node:url');
const native={read:fs.readFileSync.bind(fs),write:fs.writeFileSync.bind(fs),append:fs.appendFileSync.bind(fs),mkdir:fs.mkdirSync.bind(fs),real:fs.realpathSync.bind(fs),exists:fs.existsSync.bind(fs),open:fs.openSync.bind(fs)};
const config=JSON.parse(native.read(process.env.YMCC_DECKY_CONSUMER_CONFIG,'utf8'));
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const files=new Map([...config.repoInputs.map(row=>[key(row.actualPath),row.sha256]),...config.tools.map(row=>[key(row.path),row.sha256]),...config.ownInputs.map(row=>[key(row.path),row.sha256])]);
files.set(key(native.real(process.env.YMCC_DECKY_CONSUMER_CONFIG)),digest(native.read(process.env.YMCC_DECKY_CONSUMER_CONFIG)));
const trace={schemaVersion:1,pid:process.pid,repoRoot:config.repoRoot,evidenceDir:config.evidenceDir,reads:[],moduleLoads:[],transforms:[],vmExecutions:[],compilerErrors:[],toolChildren:[],writes:[],violations:[],beforeExit:0,exitCode:null};
const sourceTexts=new Map();
function deny(code,details){trace.violations.push({code,details});throw Object.assign(Error(code),{code});}
function filename(file){if(file instanceof URL)return fileURLToPath(file);if(typeof file==='number')return deny('UNOWNED_FILE_DESCRIPTOR',file);return path.resolve(String(file));}
function inspectRead(file,value){const requestedPath=filename(file),actualPath=native.real(requestedPath),bytes=Buffer.isBuffer(value)?value:Buffer.from(value),sha256=digest(bytes);if(!files.has(key(actualPath)))deny('UNDECLARED_FILE_READ',{requestedPath,actualPath});if(files.get(key(actualPath))!==sha256)deny('FILE_READ_CHANGED',{requestedPath,actualPath});trace.reads.push({requestedPath,actualPath,sha256,bytes:bytes.length});if(/\.(?:ts|cpp|h|json)$/.test(actualPath)&&inside(config.repoRoot,actualPath)||config.override===actualPath)sourceTexts.set(actualPath,bytes.toString('utf8'));return value;}
function writable(file){const actual=filename(file);if(!inside(config.evidenceDir,actual))deny('WRITE_OUTSIDE_OWN_EVIDENCE',actual);let parent=path.dirname(actual);while(!native.exists(parent))parent=path.dirname(parent);if(!inside(native.real(config.evidenceDir),native.real(parent)))deny('WRITE_REPARSE_ESCAPE',actual);trace.writes.push(actual);return actual;}
fs.readFileSync=function(file,...args){return inspectRead(file,native.read(file,...args));};
const read=fs.readFile.bind(fs);fs.readFile=function(file,...args){const callback=args.pop();return read(file,...args,(error,value)=>{if(error)return callback(error);try{callback(null,inspectRead(file,value));}catch(error){callback(error);}});};
const promiseRead=fs.promises.readFile.bind(fs.promises);fs.promises.readFile=async function(file,...args){return inspectRead(file,await promiseRead(file,...args));};
for(const name of ['writeFileSync','appendFileSync','mkdirSync','rmSync','unlinkSync','rmdirSync','truncateSync','chmodSync','chownSync','utimesSync']){const fn=fs[name];if(fn)fs[name]=function(file,...args){writable(file);return fn.call(this,file,...args);};}
for(const name of ['writeFile','appendFile','mkdir','rm','unlink','rmdir','truncate','chmod','chown','utimes']){const fn=fs[name];if(fn)fs[name]=function(file,...args){writable(file);return fn.call(this,file,...args);};const promise=fs.promises[name];if(promise)fs.promises[name]=async function(file,...args){writable(file);return promise.call(this,file,...args);};}
for(const name of ['copyFileSync','renameSync','linkSync','symlinkSync','copyFile','rename','link','symlink'])fs[name]=()=>deny('UNREVIEWED_FILESYSTEM_MUTATION',name);
fs.openSync=function(file,flags,...args){if(flags==='r'||flags===0){const actual=native.real(filename(file));if(!files.has(key(actual)))deny('UNDECLARED_OPEN_READ',actual);}else writable(file);return native.open(file,flags,...args);};
for(const name of ['open','createWriteStream','createReadStream'])fs[name]=()=>deny('UNREVIEWED_FILE_HANDLE',name);
fs.promises.open=()=>deny('UNREVIEWED_FILE_HANDLE','promises.open');
const resolve=Module._resolveFilename;Module._resolveFilename=function(name,parent,...args){const target=resolve.call(this,name,parent,...args);if(typeof target==='string'&&path.isAbsolute(target)){const actual=native.real(target);if(!files.has(key(actual)))deny('UNDECLARED_REQUIRE_MODULE',{name,target:actual});}return target;};
const load=Module._load,wrapped=new WeakMap();Module._load=function(name,parent,main){const actual=load.apply(this,arguments);if(name==='esbuild'){
 if(wrapped.has(actual))return wrapped.get(actual);
 const value={...actual,transformSync(text,options){const input=String(text),id=trace.transforms.length+1,sourcePaths=[...sourceTexts].filter(([,source])=>source===input||input.includes(source)&&source.length>100||source.includes(input)&&input.length>100).map(([file])=>file);const row={id,inputSha256:digest(Buffer.from(input)),sourcePaths,succeeded:false};trace.transforms.push(row);try{const result=actual.transformSync(text,options);row.succeeded=true;row.outputSha256=digest(Buffer.from(result.code));native.append(path.join(config.evidenceDir,'transforms.ndjson'),JSON.stringify({...row,options,input,code:result.code,map:result.map,warnings:result.warnings})+'\n');return result;}catch(error){trace.compilerErrors.push({id,message:String(error.message)});throw error;}}};wrapped.set(actual,value);return value;
 }return actual;};
const vm=require('node:vm'),runInNewContext=vm.runInNewContext;
vm.runInNewContext=function(code,...args){const codeSha256=digest(Buffer.from(String(code))),transforms=trace.transforms.filter(row=>row.outputSha256===codeSha256),row={codeSha256,transformIds:transforms.map(row=>row.id),sourcePaths:[...new Set(transforms.flatMap(row=>row.sourcePaths))],succeeded:false};trace.vmExecutions.push(row);try{const value=runInNewContext.call(this,code,...args);row.succeeded=true;return value;}catch(error){row.error=String(error.message);throw error;}};
function toolAllowed(command,args){const actual=native.real(filename(command));if(key(actual)!==key(config.esbuildBinary)||digest(native.read(actual))!==files.get(key(actual)))deny('UNDECLARED_CHILD_EXECUTABLE',actual);if(!Array.isArray(args)||args.length!==1||args[0]!==config.esbuildServiceArg)deny('UNDECLARED_CHILD_ARGUMENTS',{command:actual,args});return actual;}
// Keep the original execFileSync semantics; record the exact tool and natural API completion.
const execFileSync=cp.execFileSync;cp.execFileSync=function(command,args,options){const actualPath=toolAllowed(command,args),row={actualPath,args,sha256:files.get(key(actualPath)),pid:null,pidObserved:false,api:'node.execFileSync',startedAt:new Date().toISOString(),exitCode:null,naturalExit:false};trace.toolChildren.push(row);try{const result=execFileSync.apply(this,arguments);row.exitCode=0;row.naturalExit=true;return result;}catch(error){row.exitCode=error.status??null;row.signal=error.signal??null;row.naturalExit=row.exitCode!==null&&!row.signal;throw error;}finally{row.finishedAt=new Date().toISOString();}};
for(const name of ['spawn','spawnSync','exec','execSync','execFile','fork'])cp[name]=()=>deny('UNDECLARED_CHILD_PROCESS_API',name);
const net=require('node:net'),http=require('node:http'),https=require('node:https'),tls=require('node:tls'),dns=require('node:dns'),dgram=require('node:dgram');
for(const [owner,names] of [[net,['connect','createConnection','createServer']],[http,['request','get','createServer']],[https,['request','get','createServer']],[tls,['connect','createServer']],[dns,['lookup','resolve']],[dgram,['createSocket']]])for(const name of names)owner[name]=()=>deny('NETWORK_FORBIDDEN',name);
net.Socket.prototype.connect=()=>deny('NETWORK_FORBIDDEN','Socket.connect');
globalThis.fetch=()=>deny('NETWORK_FORBIDDEN','fetch');globalThis.WebSocket=function(){deny('NETWORK_FORBIDDEN','WebSocket');};
process.dlopen=()=>deny('NATIVE_ADDON_FORBIDDEN','dlopen');
Module.syncBuiltinESMExports();
process.on('beforeExit',()=>trace.beforeExit++);
process.prependOnceListener('exit',code=>{trace.exitCode=code;native.write(path.join(config.evidenceDir,'observation.json'),JSON.stringify(trace,null,2)+'\n');});

}
