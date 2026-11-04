// Observe the unchanged original producer and its compiler; no product source rewrite.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import Module from 'node:module';import childProcess from 'node:child_process';import net from 'node:net';import http from 'node:http';import https from 'node:https';import tls from 'node:tls';
import {classifyBundledInput} from '../bundled-node-tool-authority.mjs';
const config=JSON.parse(fs.readFileSync(process.env.YMCC_MONITOR_CLOSE_CONFIG,'utf8'));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),digest=p=>hash(fs.readFileSync(p)),key=p=>path.resolve(p).toLowerCase();
const trace={schemaVersion:1,pid:process.pid,producer:config.producer,builds:[],inputs:[],toolChildren:[],violations:[],beforeExit:false,exitCode:null};
const inputs=new Map(),children=[],wrappers=new WeakMap();let compiler=null,stopping=false;
function need(ok,code,detail){if(!ok){trace.violations.push({code,detail});throw Error(code);}}
need(key(fs.realpathSync(process.execPath))===key(config.authority.nodeExecutable),'NODE_IDENTITY_CHANGED');
for(const row of config.authority.tools)need(digest(row.path)===row.sha256,'TOOL_INPUT_CHANGED',row.path);
function capture(file,result){
 if(!result?.contents)return result;const actual=fs.realpathSync(file),relative=path.relative(config.repoRoot,actual).replaceAll('\\','/'),bytes=Buffer.from(result.contents),sha256=hash(bytes);
 if(Object.hasOwn(config.syntheticPorts,relative)){
  need(sha256===config.syntheticPorts[relative],'SYNTHETIC_PORT_CHANGED',relative);inputs.set(relative,{path:relative,sha256,role:'synthetic-port',origin:config.producer});
 }else{
  const classified=classifyBundledInput(config.repoRoot,actual,bytes,config.authority);
  if(classified.role==='source'){need(config.productSources.includes(classified.row.path),'UNDECLARED_PRODUCTION_INPUT',classified.row.path);need(digest(actual)===sha256,'PRODUCT_CONTENT_REPLACED',relative);}
  inputs.set(classified.row.path,{...classified.row,role:classified.role});
 }
 return result;
}
const nativeSpawn=childProcess.spawn;
childProcess.spawn=function(program,args,...rest){
 let actual;try{actual=fs.realpathSync(program);}catch{need(false,'CHILD_PROGRAM_UNRESOLVED',String(program));}
 need(key(actual)===key(config.authority.compilerBinary)&&JSON.stringify(args)===JSON.stringify(['--service='+config.authority.compilerVersion,'--ping']),'UNDECLARED_CHILD_PROCESS',{program,args});
 const child=nativeSpawn.call(this,program,args,...rest),row={pid:child.pid,program:actual,args,closed:false};trace.toolChildren.push(row);children.push({child,row});child.once('close',(exitCode,signal)=>Object.assign(row,{closed:true,exitCode,signal}));return child;
};
for(const method of ['spawnSync','exec','execSync','execFile','execFileSync','fork'])childProcess[method]=()=>need(false,'UNDECLARED_CHILD_PROCESS',method);
for(const [object,names]of [[net,['connect','createConnection','createServer']],[http,['get','request','createServer']],[https,['get','request','createServer']],[tls,['connect','createServer']]])for(const name of names)object[name]=()=>need(false,'NETWORK_FORBIDDEN',name);
globalThis.fetch=()=>need(false,'NETWORK_FORBIDDEN','fetch');globalThis.WebSocket=class{constructor(){need(false,'NETWORK_FORBIDDEN','WebSocket');}};
const originalLoad=Module._load;
Module._load=function(request,parent,isMain){
 const value=originalLoad.call(this,request,parent,isMain);if(request!=='esbuild')return value;
 need(key(fs.realpathSync(Module._resolveFilename(request,parent,isMain)))===key(config.authority.compilerEntry),'COMPILER_ENTRY_CHANGED');if(wrappers.has(value))return wrappers.get(value);compiler=value;
 const wrapped={...value,async build(options){
  const row={ordinal:trace.builds.length+1,status:'RUNNING',plugins:(options.plugins??[]).map(p=>p.name)};trace.builds.push(row);
  const observed=(options.plugins??[]).map(plugin=>({name:plugin.name,setup(build){const proxy=Object.create(build);proxy.onLoad=(filter,callback)=>build.onLoad(filter,async args=>capture(args.path,await callback(args)));return plugin.setup(proxy);}}));
  const fallback={name:'owned-close-exact-inputs',setup(build){build.onLoad({filter:/\.(?:[cm]?js|ts|json)$/},args=>{if(args.namespace!=='file')return;const bytes=fs.readFileSync(args.path),ext=path.extname(args.path);return capture(args.path,{contents:bytes,resolveDir:path.dirname(args.path),loader:ext==='.ts'?'ts':ext==='.json'?'json':'js'});});}};
  try{const result=await value.build({...options,metafile:true,plugins:[...observed,fallback]});row.status='PASS';row.outputs=result.outputFiles.map(file=>({sha256:hash(file.contents),bytes:file.contents.length}));row.metafileInputs=Object.keys(result.metafile.inputs);return result;}catch(error){row.status='FAIL';row.error=String(error.message);throw error;}
 }};wrappers.set(value,wrapped);return wrapped;
};
process.once('beforeExit',async()=>{
 if(stopping)return;stopping=true;trace.beforeExit=true;
 try{compiler?.stop();await Promise.all(children.map(({child,row})=>row.closed?null:new Promise(resolve=>{const timer=setTimeout(()=>{trace.violations.push({code:'TOOL_RESOURCE_NOT_DISPOSED',detail:row.pid});child.kill();process.exitCode=1;resolve();},2000);child.once('close',()=>{clearTimeout(timer);resolve();});})));}catch(error){trace.violations.push({code:'TOOL_STOP_FAILED',detail:String(error.message)});process.exitCode=1;}
});
process.on('exit',exitCode=>{trace.exitCode=exitCode;trace.inputs=[...inputs.values()];trace.status=exitCode===0&&trace.beforeExit&&trace.builds.length>0&&trace.builds.every(x=>x.status==='PASS')&&trace.toolChildren.every(x=>x.closed)&&trace.violations.length===0?'PASS':'FAIL';fs.writeFileSync(config.traceFile,JSON.stringify(trace,null,2)+'\n');});
