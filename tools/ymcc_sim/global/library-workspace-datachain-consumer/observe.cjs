'use strict';
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm'), crypto = require('node:crypto'), assert = require('node:assert/strict');
const {fileURLToPath} = require('node:url'), {syncBuiltinESMExports} = require('node:module');
const nativeRead=fs.readFileSync.bind(fs),nativeWrite=fs.writeFileSync.bind(fs),nativeOpen=fs.openSync.bind(fs),mkdir=fs.mkdirSync.bind(fs);
let trustedIo=0;
const read=(...args)=>{trustedIo++;try{return nativeRead(...args);}finally{trustedIo--;}};
const write=(...args)=>{trustedIo++;try{return nativeWrite(...args);}finally{trustedIo--;}};
const digest = data => crypto.createHash('sha256').update(data).digest('hex');
const cfg = JSON.parse(read(process.env.YMCC_WORKSPACE_CONFIG,'utf8'));
const trace = {schemaVersion:1,finished:false,reads:[],writes:[],vm:[],calls:{},assertions:[],forbidden:[],node:{path:process.execPath,version:process.version},nonBuiltinModulesUsed:[]};
const full = f => path.resolve(f instanceof URL ? fileURLToPath(f) : Buffer.isBuffer(f) ? f.toString() : f);
const allowed = new Map(cfg.reads.map(row=>[full(row.path).toLowerCase(),row]));
function deny(kind,value) {trace.forbidden.push({kind,value}); throw Error('WORKSPACE_CONTROL_'+kind);}
function checkedRead(file) {
  const target = full(file), row = allowed.get(target.toLowerCase());
  if (!row || digest(read(target)) !== row.sha256) deny('READ_IDENTITY',target);
  trace.reads.push({path:target,sha256:row.sha256});
}
fs.readFileSync = function(file,...args) {checkedRead(file);return read(file,...args);};
const asyncRead = fs.promises.readFile.bind(fs.promises);
fs.promises.readFile = async function(file,...args) {checkedRead(file);return asyncRead(file,...args);};
fs.writeFileSync = function(file,...args) {if(full(file)!==full(cfg.report))deny('WRITE_PATH',full(file));trace.writes.push(full(file));return write(file,...args);};
fs.mkdirSync = function(file,...args) {if(full(file)!==path.dirname(full(cfg.report)))deny('MKDIR_PATH',full(file));return mkdir(file,...args);};
for (const name of ['writeFile','appendFile','mkdir','rename','rm','unlink','copyFile','open']) fs.promises[name] = async()=>deny('ASYNC_FS',name);
for (const name of ['appendFileSync','createReadStream','createWriteStream','renameSync','copyFileSync','rmSync','unlinkSync']) fs[name] = ()=>deny('UNREVIEWED_FS',name);
fs.openSync=function(...args){if(!trustedIo)deny('UNREVIEWED_FS','openSync');return nativeOpen(...args);};
const originalVm = vm.runInContext;
const watched = ['normalizeWorkspaceSnapshot','workspaceBridgeResult','artworkEntry','artworkMarkup','artworkPreviewUrl','isBuiltinExcludedSteamTool'];
vm.runInContext = function(code,context,...args) {
  const expected = cfg.vm[trace.vm.length], row = {ordinal:trace.vm.length,sha256:digest(code),code,completed:false};trace.vm.push(row);
  if (!expected || expected.sha256!==row.sha256 || expected.code!==code) deny('VM_INPUT',row.sha256);
  const result = originalVm.call(this,code,context,...args);row.completed=true;
  for (const name of watched) if (typeof context[name]==='function' && !context[name].__workspaceObserved) {
    const original = context[name];
    const wrapper = function(...values) {const counter=trace.calls[name]??={calls:0,returned:0,threw:0};counter.calls++;try{const result=Reflect.apply(original,this,values);counter.returned++;return result;}catch(e){counter.threw++;throw e;}};
    wrapper.__workspaceObserved=true;context[name]=wrapper;
  }
  return result;
};
for(const name of ['equal','deepEqual','strictEqual','throws','match','doesNotMatch']) {
  const original=assert[name];assert[name]=function(...args){try{return Reflect.apply(original,this,args);}catch(e){trace.assertions.push({method:name,name:e.name,code:e.code,message:e.message,stack:e.stack});throw e;}};
}
for(const name of ['node:http','node:https','node:net','node:tls','node:dgram']) {const mod=require(name);for(const key of ['request','get','connect','createConnection','createServer','createSocket'])if(typeof mod[key]==='function')mod[key]=()=>deny('NETWORK',name+':'+key);}
globalThis.fetch=()=>deny('NETWORK','fetch');
const cp=require('node:child_process');for(const name of ['spawn','spawnSync','exec','execSync','execFile','execFileSync','fork'])cp[name]=()=>deny('PROCESS',name);
syncBuiltinESMExports();
process.on('exit',()=>{trace.finished=true;write(cfg.trace,JSON.stringify(trace,null,2)+'\n',{flag:'wx'});});
