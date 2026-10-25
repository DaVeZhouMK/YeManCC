import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const p=APP_VERSION.split('.').map(Number),version=p[0]+'.'+p[1]+'.'+(p[2]+1),sha='C'.repeat(64);
let receive:(event:{data:any})=>void=()=>{};
const calls:{id:number;cmd:string;args:any}[]=[];
let native:any={phase:'downloading',stage:'download',version,sha256:sha,operationId:'surviving-native',activeDownloadOperation:'surviving-native',downloadActive:true,background:true,downloadedBytes:4096};
let lostReply=false;
class FixtureEvent extends Event{detail:unknown;constructor(type:string,init:{detail?:unknown}){super(type);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;
const win=new EventTarget() as any;win.chrome={webview:{addEventListener(_name:string,cb:typeof receive){receive=cb;},postMessage(req:any){
 if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
  if(req.cmd==='app.updateState')receive({data:{id:req.id,result:{...native}}});
  else if(req.cmd==='app.cancelUpdateDownload')receive({data:{id:req.id,result:true}});
  else if(req.cmd==='app.downloadUpdate'){if(!lostReply)throw Error('competing download forbidden');native={...native,operationId:req.args.operationId,activeDownloadOperation:'',downloadActive:false,phase:'downloaded',stage:'install'};}
  else throw Error('FORBIDDEN '+req.cmd);
 });
}}};(globalThis as any).window=win;
const m=await import('../src/bridge/updateManager');let checks=0;
function check(label:string,fn:()=>void){fn();checks++;console.log('PASS '+label);}
await m.ensureUpdateManager();
check('WebView restart adopts live native ownership without turning it into a retry',()=>{assert.equal(m.updateSnapshot.phase,'downloading');assert.equal(m.updateInfo.value?.version,version);assert.equal(m.updateSnapshot.downloadedBytes,4096);});
await m.checkForUpdate(APP_VERSION);assert.equal(await m.prepareUpdate({background:true}),false);
check('adopted live transfer blocks competing checks and downloads',()=>assert.equal(calls.filter(x=>x.cmd==='app.downloadUpdate'||x.cmd==='app.checkUpdate').length,0));
await m.cancelBackgroundUpdate();check('cancel targets the adopted operation identity',()=>assert.equal(calls.find(x=>x.cmd==='app.cancelUpdateDownload')!.args.operationId,'surviving-native'));
native={...native,downloadActive:false,activeDownloadOperation:'',phase:'downloaded',stage:'install'};await m.refreshAdoptedUpdate();
check('lost native completion event is recovered from real terminal ownership',()=>assert.equal(m.updateSnapshot.phase,'downloaded'));
// Drop the actual command response and progress event, then drive the production
// poll timer against the native terminal state. Timers here never touch product IO.
const originalSet=globalThis.setTimeout,originalClear=globalThis.clearTimeout;
const timers=new Map<number,{fn:()=>unknown;ms:number}>();let id=0;
(globalThis as any).setTimeout=(fn:()=>unknown,ms:number)=>{timers.set(++id,{fn,ms});return id;};
(globalThis as any).clearTimeout=(id:number)=>timers.delete(id);
try{
 lostReply=true;const download=m.prepareUpdate({background:true});await Promise.resolve();await Promise.resolve();
 const timer=[...timers.values()].find(t=>t.ms===15000);assert.ok(timer);await timer.fn();await download;
 check('missing download IPC response is repaired by native terminal polling',()=>{assert.equal(m.updateSnapshot.phase,'downloaded');assert.equal(calls.filter(x=>x.cmd==='app.downloadUpdate').length,1);});
 check('completion recovery never installs and clears the polling timer',()=>{assert.equal(calls.filter(x=>x.cmd==='app.installUpdate').length,0);assert.equal(timers.size,0);});
}finally{globalThis.setTimeout=originalSet;globalThis.clearTimeout=originalClear;}
console.log('UPDATER_NATIVE_ADOPTION_SELFTEST_OK checks='+checks+' (mock IPC and fake poll timer only)');
