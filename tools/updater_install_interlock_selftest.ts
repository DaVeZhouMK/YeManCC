import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const parts=APP_VERSION.split('.').map(Number),version=`${parts[0]}.${parts[1]}.${parts[2]+1}`,sha='C'.repeat(64);
let receive:(event:{data:any})=>void=()=>{};
let moduleBusy=true,holdInstall=false;
const calls:any[]=[],pending:any[]=[];
class FixtureEvent extends Event {detail:unknown;constructor(name:string,init:any){super(name);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;
const win=new EventTarget() as any;
win.chrome={webview:{addEventListener(_name:string,listener:typeof receive){receive=listener;},postMessage(request:any){
 if(typeof request.id!=='number')return;calls.push(request);
 queueMicrotask(()=>{
  const result=(value:any)=>receive({data:{id:request.id,result:value}});
  if(request.cmd==='app.updateState')result({phase:'available',operationId:'saved',version,sha256:sha,downloadActive:false});
  else if(request.cmd==='app.downloadUpdate')result('verified-cache-only-fixture');
  else if(request.cmd==='app.installUpdate'){
   if(moduleBusy)receive({data:{id:request.id,error:'模块正在安装或升级，请等待完成后重试更新'}});
   else if(holdInstall)pending.push(request);
   else result(true);
  }else throw Error('FORBIDDEN '+request.cmd);
 });
}}};(globalThis as any).window=win;
const manager=await import('../src/bridge/updateManager');let checks=0;
function check(name:string,test:()=>void){test();checks++;console.log('PASS '+name);}
await manager.ensureUpdateManager();
await manager.prepareUpdate({background:true});
check('automatic package preparation never asks the running YMCC to exit or install',()=>{
 assert.equal(manager.updateSnapshot.phase,'downloaded');assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,0);
});
await manager.downloadAndInstall();
check('module installer busy returns actionable failure while YMCC stays usable',()=>{
 assert.equal(manager.updateSnapshot.phase,'failed');assert.match(manager.updateSnapshot.error,/模块正在/);
 assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,1);
});
await Promise.resolve();await Promise.resolve();
check('busy module does not create an automatic install retry loop',()=>assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,1));
moduleBusy=false;holdInstall=true;
const one=manager.downloadAndInstall(),two=manager.downloadAndInstall();
await Promise.resolve();await Promise.resolve();await Promise.resolve();
check('manual retry after module completion has one install owner',()=>{assert.equal(pending.length,1);assert.equal(manager.updateSnapshot.phase,'installing');});
const clear=await manager.clearUpdateCache();
check('cache cleanup cannot delete an install that is waiting for shutdown handoff',()=>{assert.equal(clear,false);assert.match(manager.updateCacheState.error,/安装/);assert.equal(calls.filter(x=>x.cmd==='app.clearUpdateCache').length,0);});
receive({data:{id:pending[0].id,result:true}});await Promise.all([one,two]);
check('duplicate confirmation issues exactly one successor handoff after recovery',()=>assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,2));
console.log(JSON.stringify({status:'PASS',checks,consumer:'production updateManager + IPC',physicalProcesses:'covered separately by actual emitted helper fixtures',actualInstallation:false}));
