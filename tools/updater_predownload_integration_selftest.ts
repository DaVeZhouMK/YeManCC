import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const parts=APP_VERSION.split('.').map(Number),version=parts[0]+'.'+parts[1]+'.'+(parts[2]+1),sha='B'.repeat(64);
let receive:(event:{data:any})=>void=()=>{};
const calls:{id:number;cmd:string;args:any}[]=[],pending:{id:number;cmd:string;args:any}[]=[];
let hold=false,fail=false;
class FixtureEvent extends Event { detail:unknown;constructor(type:string,init:{detail?:unknown}){super(type);this.detail=init.detail;} }
(globalThis as any).CustomEvent=FixtureEvent;
const win=new EventTarget() as any;
win.chrome={webview:{addEventListener(_name:string,listener:typeof receive){receive=listener;},postMessage(req:any){
 if(typeof req.id!=='number')return;calls.push(req);
 const reply=(result:any)=>receive({data:{id:req.id,result}});
 queueMicrotask(()=>{
  if(req.cmd==='app.updateState')reply({phase:'downloaded',stage:'install',operationId:'saved',version,sha256:sha,downloadActive:false});
  else if(req.cmd==='app.downloadUpdate'){
   if(hold){pending.push(req);return;}
   fail?receive({data:{id:req.id,error:'fixture download failure'}}):reply('inert package');
  }else if(req.cmd==='app.installUpdate')reply(true);
  else if(req.cmd==='app.cancelUpdateDownload')reply(false);
  else throw Error('FORBIDDEN '+req.cmd);
 });
}}};(globalThis as any).window=win;
const m=await import('../src/bridge/updateManager');let checks=0;
function check(label:string,fn:()=>void){fn();checks++;console.log('PASS '+label);}
await m.ensureUpdateManager();
check('persisted downloaded state remains ready after restart',()=>{assert.equal(m.updateSnapshot.phase,'downloaded');assert.equal(m.updateInfo.value?.version,version);});
await m.prepareUpdate({background:true,cacheOnly:true});
check('background cache verification carries explicit no-network flag and cannot install',()=>{const r=calls.find(x=>x.cmd==='app.downloadUpdate')!;assert.equal(r.args.background,true);assert.equal(r.args.cacheOnly,true);assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,0);assert.equal(m.updateSnapshot.phase,'downloaded');});
m.updateSnapshot.phase='available';hold=true;
const one=m.prepareUpdate({background:true}),two=m.prepareUpdate({background:true});
check('concurrent background callers share one real download promise',()=>assert.equal(one,two));
await Promise.resolve();await Promise.resolve();
receive({data:{event:'update.progress',data:{operationId:pending[0].args.operationId,phase:'failed',stage:'download',error:'early terminal progress'}}});
await m.checkForUpdate(APP_VERSION);
check('early terminal progress cannot release the download lock to a competing manifest check',()=>assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate').length,0));
const manual=m.downloadAndInstall();await Promise.resolve();
check('explicit install waits for the existing background owner',()=>{assert.equal(pending.length,1);assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,0);});
receive({data:{id:pending[0].id,result:'inert verified package'}});await Promise.all([one,two,manual]);
check('only explicit confirmation installs after the shared download completes',()=>{assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,1);assert.equal(m.updateSnapshot.phase,'installing');});
hold=false;fail=true;m.updateSnapshot.phase='failed';m.updateSnapshot.stage='download';
await m.prepareUpdate({background:true});
check('background errors release ownership without installation',()=>{assert.equal(m.updateSnapshot.phase,'failed');assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,1);});
fail=false;await m.prepareUpdate({background:true});
check('a later bounded round can recover after background failure',()=>assert.equal(m.updateSnapshot.phase,'downloaded'));
console.log('UPDATER_PREDOWNLOAD_INTEGRATION_SELFTEST_OK checks='+checks+' (mock IPC only)');
