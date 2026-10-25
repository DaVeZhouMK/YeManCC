import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';

export async function startBatteryControllerBridge({dll,evidenceDir,dotnet}){
 if(!path.isAbsolute(dll)||!path.isAbsolute(evidenceDir)||!dotnet)throw Error('EXPLICIT_CONTROLLER_BRIDGE_INPUTS_REQUIRED');
 fs.mkdirSync(evidenceDir,{recursive:true});
 const receipt=path.join(evidenceDir,'native-boundary.json');
 const env=Object.fromEntries(['SystemRoot','WINDIR','PATH','TEMP','TMP','LOCALAPPDATA','APPDATA','DOTNET_ROOT','ComSpec'].filter(k=>process.env[k]!==undefined).map(k=>[k,process.env[k]]));
 Object.assign(env,{YMCC_BATTERY_SIM_RECEIPT:receipt,YMCC_BATTERY_SIM_GUARD_NEGATIVE:'0',YMCC_BATTERY_SIM_BRIDGE:'1'});
 const child=spawn(dotnet,[dll],{cwd:evidenceDir,env,windowsHide:true,stdio:['pipe','pipe','pipe']});
 const pending=new Map(),trace=[];let sequence=0,buffer='',stderr='',exit=null,forced=false;
 const closed=new Promise(resolve=>child.once('close',(code,signal)=>{exit={code,signal};for(const item of pending.values()){clearTimeout(item.timer);item.reject(Error('CONTROLLER_BRIDGE_EXITED'));}pending.clear();resolve(exit);}));
 const fail=()=>{for(const item of pending.values()){clearTimeout(item.timer);item.reject(Error('CONTROLLER_BRIDGE_BAD_REPLY'));}pending.clear();};
 child.once('error',fail);
 child.stderr.on('data',chunk=>{stderr=(stderr+chunk.toString()).slice(-32768);});
 child.stdout.on('data',chunk=>{
  buffer+=chunk.toString('utf8');if(buffer.length>1024*1024){fail();child.kill();forced=true;return;}
  let end;while((end=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,end);buffer=buffer.slice(end+1);if(!line.trim())continue;
   let reply;try{reply=JSON.parse(line);}catch{fail();continue;}
   const item=pending.get(reply.id);if(!item){fail();continue;}pending.delete(reply.id);clearTimeout(item.timer);
   trace.push({id:reply.id,command:item.command,args:item.args,ok:reply.ok,reply:reply.reply,error:reply.error??null});
   if(reply.ok===true)item.resolve(reply.reply);else item.reject(Error(reply.error??'CONTROLLER_BRIDGE_REJECTED'));
  }
 });
 const request=(command,args={})=>new Promise((resolve,reject)=>{
  if(exit){reject(Error('CONTROLLER_BRIDGE_NOT_RUNNING'));return;}
  if(!/^(battery\.charge\.(get|set|refresh|release)|fixture\.(stats|reset|set|suspend|resume|tick|restore|shutdown))$/.test(command)){reject(Error('UNREVIEWED_BATTERY_COMMAND'));return;}
  const id=++sequence,body=JSON.stringify({id,command,args});if(body.length>16384){reject(Error('CONTROLLER_BRIDGE_REQUEST_TOO_LARGE'));return;}
  const timer=setTimeout(()=>{pending.delete(id);reject(Error('CONTROLLER_BRIDGE_REPLY_TIMEOUT'));},10000);
  pending.set(id,{resolve,reject,timer,command,args});child.stdin.write(body+'\n',error=>{if(error){clearTimeout(timer);pending.delete(id);reject(Error('CONTROLLER_BRIDGE_WRITE_FAILED'));}});
 });
 const close=async()=>{
  if(!exit){try{await request('fixture.shutdown');}catch{}child.stdin.end();}
  const timer=setTimeout(()=>{if(!exit){forced=true;child.kill();}},5000);
  await closed;clearTimeout(timer);fs.writeFileSync(path.join(evidenceDir,'stderr.log'),stderr);
  const boundary=fs.existsSync(receipt)?JSON.parse(fs.readFileSync(receipt,'utf8').replace(/^\uFEFF/,'')):{status:'FAIL',nativeAttempts:[],failure:'CONTROLLER_BOUNDARY_RECEIPT_MISSING'};
  const result={status:exit.code===0&&!forced&&boundary.status==='PASS'&&boundary.nativeAttempts.length===0?'PASS':'FAIL',process:{pid:child.pid,exitCode:exit.code,signal:exit.signal,forcedCleanup:forced},boundary,trace,
   dll:{path:dll,sha256:createHash('sha256').update(fs.readFileSync(dll)).digest('hex')}};
  fs.writeFileSync(path.join(evidenceDir,'result.json'),JSON.stringify(result,null,2));return result;
 };
 try{const ready=await request('fixture.stats');if(ready.scope!=='actual-controller-with-injected-ports')throw Error('REAL_CONTROLLER_HANDSHAKE_MISSING');}
 catch(error){if(!exit){forced=true;child.kill();}await closed;throw error;}
 return {request,close,trace,pid:child.pid};
}