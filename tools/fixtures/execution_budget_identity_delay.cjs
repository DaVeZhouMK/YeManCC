// Opt-in delay control for execution-budget.test.mjs. Requiring this module is inert.
const fs=require('node:fs'),path=require('node:path');
const original={readFileSync:fs.readFileSync,openSync:fs.openSync,readSync:fs.readSync,closeSync:fs.closeSync};
const key=file=>process.platform==='win32'?path.resolve(file).toLowerCase():path.resolve(file);
let active=null,wholeFileDepth=0;
function eligible(file){return active&&typeof file==='string'&&active.files.has(key(file));}
function visit(file,api,requestBytes){
 if(!eligible(file))return;
 const target=key(file),scan=(active.counts.get(target)??0)+1;active.counts.set(target,scan);
 const delayed=scan===(active.phase==='pre'?1:2)&&!active.delayed.has(target);
 if(delayed){active.delayed.add(target);Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,active.delayMs);}
 fs.appendFileSync(active.traceFile,JSON.stringify({path:file,api,requestBytes,scan,delayed,delayMs:delayed?active.delayMs:0})+'\n');
}
function begin(configFile){
 if(active)throw Error('IDENTITY_DELAY_ALREADY_ACTIVE');
 if(typeof configFile!=='string'||!path.isAbsolute(configFile)||fs.lstatSync(configFile).isSymbolicLink()||fs.statSync(configFile).size>64*1024)throw Error('IDENTITY_DELAY_CONFIG_INVALID');
 const actual=fs.realpathSync(configFile),own=path.dirname(actual);
 if(key(actual)!==key(configFile))throw Error('IDENTITY_DELAY_CONFIG_REPARSE');
 const config=JSON.parse(original.readFileSync(actual,'utf8'));
 if(config.schemaVersion!==1||!['pre','post'].includes(config.phase)||!Number.isInteger(config.delayMs)||config.delayMs<1||config.delayMs>40||!Array.isArray(config.files)||config.files.length!==90||typeof config.fixtureDir!=='string'||!path.isAbsolute(config.fixtureDir)||typeof config.traceFile!=='string'||!path.isAbsolute(config.traceFile))throw Error('IDENTITY_DELAY_CONFIG_INVALID');
 const fixtureDir=fs.realpathSync(config.fixtureDir);
 if(key(fixtureDir)!==key(config.fixtureDir)||key(path.dirname(fixtureDir))!==key(own)||key(path.dirname(config.traceFile))!==key(own))throw Error('IDENTITY_DELAY_OUTSIDE_OWN_CASE');
 const files=new Set();
 for(const file of config.files){
  if(typeof file!=='string'||!path.isAbsolute(file)||key(path.dirname(file))!==key(fixtureDir)||!fs.lstatSync(file).isFile()||fs.statSync(file).size!==257||key(fs.realpathSync(file))!==key(file)||files.has(key(file)))throw Error('IDENTITY_DELAY_FIXTURE_INVALID');
  files.add(key(file));
 }
 fs.writeFileSync(config.traceFile,'',{flag:'wx'});
 active={files,phase:config.phase,delayMs:config.delayMs,traceFile:config.traceFile,counts:new Map(),delayed:new Set(),fds:new Map()};
 fs.readFileSync=function(file,...args){
  visit(file,'readFileSync',null);wholeFileDepth++;
  try{return original.readFileSync.call(this,file,...args);}finally{wholeFileDepth--;}
 };
 fs.openSync=function(file,...args){
  const fd=original.openSync.call(this,file,...args);
  if(eligible(file))active.fds.set(fd,{file,visited:false});return fd;
 };
 fs.readSync=function(fd,buffer,...args){
  const entry=active.fds.get(fd);
  if(entry&&!entry.visited&&!wholeFileDepth){entry.visited=true;visit(entry.file,'readSync',typeof args[1]==='number'?args[1]:buffer.byteLength);}
  return original.readSync.call(this,fd,buffer,...args);
 };
 fs.closeSync=function(fd,...args){active.fds.delete(fd);return original.closeSync.call(this,fd,...args);};
}
function end(){
 if(!active)return;
 for(const name of Object.keys(original))fs[name]=original[name];
 active=null;wholeFileDepth=0;
}
module.exports={begin,end};
