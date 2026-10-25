import fs from 'node:fs';import path from 'node:path';import {spawn}from'node:child_process';
/** One explicitly supplied owned fixture process. No production executable discovery. */
export class GpuNativeClient{
 constructor(worker,root){
  if(!path.isAbsolute(worker)||!path.isAbsolute(root)||!fs.statSync(worker).isFile()||fs.existsSync(root))throw Error('EXPLICIT_WORKER_AND_FRESH_FIXTURE_REQUIRED');
  fs.mkdirSync(root,{recursive:true});this.pending=new Map();this.sequence=0;this.trace=[];this.stderr='';this.buffer='';this.lastStats=null;this.forcedCleanup=false;
  const env={};for(const key of ['SystemRoot','WINDIR','PATH','PATHEXT','TEMP','TMP'])if(process.env[key])env[key]=process.env[key];
  this.child=spawn(worker,[root],{cwd:root,env,windowsHide:true,shell:false,stdio:['pipe','pipe','pipe']});this.pid=this.child.pid;
  this.exit=new Promise(resolve=>{this.child.once('error',error=>{this.failure=error.message;this.rejectPending(error);resolve({code:null,error:error.message});});this.child.once('exit',(code,signal)=>{this.rejectPending(Error('GPU_FIXTURE_EXITED'));resolve({code,signal});});});
  this.child.stdout.setEncoding('utf8');this.child.stdout.on('data',chunk=>{
   this.buffer+=chunk;if(this.buffer.length>2*1024*1024){this.rejectPending(Error('GPU_FIXTURE_OUTPUT_BOUND'));this.child.kill();return;}
   let newline;while((newline=this.buffer.indexOf('\n'))>=0){const text=this.buffer.slice(0,newline);this.buffer=this.buffer.slice(newline+1);if(!text.trim())continue;
    try{const value=JSON.parse(text);const pending=this.pending.get(value.sequence);if(!pending)throw Error('UNMATCHED_FIXTURE_REPLY');this.pending.delete(value.sequence);clearTimeout(pending.timeout);this.lastStats=value.stats;this.trace.push({request:pending.request,response:value.response,stats:value.stats});pending.resolve(value);}
    catch(error){this.rejectPending(error);this.child.kill();return;}
   }
  });this.child.stderr.setEncoding('utf8');this.child.stderr.on('data',text=>{this.stderr=(this.stderr+text).slice(-65536);});
 }
 rejectPending(error){for(const item of this.pending.values()){clearTimeout(item.timeout);item.reject(error);}this.pending.clear();}
 send(request){
  if(this.trace.length+this.pending.size>=200)throw Error('GPU_FIXTURE_REQUEST_BOUND');
  if(!request||typeof request!=='object'||!('fixture'in request)&&!['gpu.dedicatedMemory.get','gpu.dedicatedMemory.set'].includes(request.cmd))throw Error('UNREVIEWED_FIXTURE_COMMAND');
  const sequence=++this.sequence;return new Promise((resolve,reject)=>{const timeout=setTimeout(()=>{this.pending.delete(sequence);reject(Error('GPU_FIXTURE_REPLY_TIMEOUT'));},10000);this.pending.set(sequence,{request:structuredClone(request),timeout,resolve,reject});this.child.stdin.write(JSON.stringify({sequence,request})+'\n',error=>{if(error){this.pending.delete(sequence);clearTimeout(timeout);reject(error);}});});
 }
 async configure(fixture){const result=await this.send({fixture});if(!result.response.ok)throw Error(result.response.error??'GPU_FIXTURE_CONTROL_FAILED');return result.stats;}
 async close(){
  this.child.stdin.end();let timer;const result=await Promise.race([this.exit,new Promise(resolve=>{timer=setTimeout(()=>resolve(null),5000);})]);clearTimeout(timer);
  if(result)return {...result,pid:this.pid,forcedCleanup:false,stderr:this.stderr};
  this.forcedCleanup=true;this.child.kill();const exited=await this.exit;return {...exited,pid:this.pid,forcedCleanup:true,stderr:this.stderr};
 }
}
