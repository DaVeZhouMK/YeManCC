import {spawn} from 'node:child_process';
export async function runBoundedNode(script,args=[],{cwd,timeoutMs=30000,env=process.env,outputLimit=1024*1024}={}){
 return new Promise(resolve=>{
 let stdout='',stderr='',bytes=0,outputTruncated=false,timedOut=false,launchError=null;
 const started=Date.now(),child=spawn(process.execPath,[script,...args],{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
 const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutMs);
 for(const kind of ['stdout','stderr'])child[kind].on('data',chunk=>{const allowed=Math.max(0,outputLimit-bytes);bytes+=chunk.length;if(chunk.length>allowed)outputTruncated=true;if(kind==='stdout')stdout+=chunk.subarray(0,allowed).toString();else stderr+=chunk.subarray(0,allowed).toString();});
 child.once('error',error=>{launchError=error.code??'SPAWN_FAILED';});
 child.once('close',(exitCode,signal)=>{clearTimeout(timer);resolve({exitCode,signal,timedOut,launchError,outputTruncated,stdout,stderr,pid:child.pid??null,durationMs:Date.now()-started});});
 });
}
export function assessRun({exitCode,timedOut,launchError,outputTruncated,receipt,expectedSourceHashes,currentSourceHashes}){
 const reasons=[];
 if(timedOut)reasons.push('WORKER_TIMEOUT');if(launchError)reasons.push('WORKER_LAUNCH_FAILED');if(outputTruncated)reasons.push('WORKER_OUTPUT_TRUNCATED');
 if(exitCode!==0)reasons.push('WORKER_NONZERO_EXIT');if(!receipt)reasons.push('MISSING_WORKER_RECEIPT');else if(receipt.ok!==true||receipt.status!=='PASS')reasons.push('WORKER_RECEIPT_NOT_PASS');
 for(const [file,pin]of Object.entries(expectedSourceHashes??{}))if(currentSourceHashes?.[file]!==pin)reasons.push('SOURCE_DRIFT:'+file);
 return {status:reasons.length?'FAIL':'PASS',reasons};
}
