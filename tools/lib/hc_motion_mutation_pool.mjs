import {spawn} from 'node:child_process';

/** Run independent owned tasks with bounded concurrency and retain input order.
 * After an error, stop taking queued work and await every already-started task. */
export async function runBoundedTasks(items,{concurrency=1,task,onEvent=()=>{}}={}){
 if(!Array.isArray(items)||!Number.isSafeInteger(concurrency)||concurrency<1||concurrency>4||typeof task!=='function'||typeof onEvent!=='function')throw Error('MOTION_MUTATION_POOL_ARGUMENTS');
 const results=new Array(items.length);let cursor=0,active=0,firstError=null;
 async function worker(){
  while(!firstError&&cursor<items.length){const index=cursor++;active++;
   try{onEvent({phase:'start',index,active,atUtc:new Date().toISOString()});results[index]=await task(items[index],index);active--;onEvent({phase:'finish',index,active,atUtc:new Date().toISOString()});}
   catch(error){if(!firstError)firstError=error;active=Math.max(0,active-1);try{onEvent({phase:'error',index,active,error:String(error.message),atUtc:new Date().toISOString()});}catch(eventError){if(!firstError)firstError=eventError;}}
  }
 }
 await Promise.allSettled(Array.from({length:Math.min(concurrency,items.length)},worker));
 if(firstError){firstError.partialResults=results;throw firstError;}return results;
}

/** Close, timeout, signal, launch and output status remain separate from exit1. */
export function runLoggedChild(command,args,{cwd,env=process.env,timeoutMs=180000,outputLimit=32*1024*1024,onSpawn=()=>{}}={}){
 if(typeof command!=='string'||!Array.isArray(args)||!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isSafeInteger(outputLimit)||outputLimit<1)throw Error('MOTION_MUTATION_CHILD_ARGUMENTS');
 return new Promise(resolve=>{
  const start=Date.now();let stdout='',stderr='',stdoutBytes=0,stderrBytes=0,timedOut=false,outputTruncated=false,launchError=null,executionError=null;
  const child=spawn(command,args,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
  const stop=(code,message)=>{if(!executionError){executionError=new Error(message);executionError.code=code;}child.kill();};
  const append=(name,text)=>{const size=Buffer.byteLength(text),prior=name==='stdout'?stdoutBytes:stderrBytes;if(prior+size>outputLimit){outputTruncated=true;stop('ENOBUFS','Motion mutation child output exceeded its original32MiB stream limit');return;}if(name==='stdout'){stdoutBytes+=size;stdout+=text;}else{stderrBytes+=size;stderr+=text;}};
  child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');child.stdout.on('data',text=>append('stdout',text));child.stderr.on('data',text=>append('stderr',text));
  child.once('error',error=>{launchError=error.message;executionError=error;});
  const timer=setTimeout(()=>{timedOut=true;stop('ETIMEDOUT','Motion mutation child reached its original180000ms limit');},timeoutMs);
  try{onSpawn({pid:child.pid??null,executable:command,args,cwd,startedAtUtc:new Date(start).toISOString()});}catch(error){executionError=error;child.kill();}
  child.once('close',(status,signal)=>{clearTimeout(timer);resolve({pid:child.pid??null,status,signal,stdout,stderr,error:executionError,timedOut,outputTruncated,launchError,durationMs:Date.now()-start,naturalExitObserved:Number.isInteger(status)&&signal===null&&!timedOut&&!outputTruncated&&!launchError&&!executionError});});
 });
}
