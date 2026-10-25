import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const p=APP_VERSION.split('.').map(Number),v=(n:number)=>p[0]+'.'+p[1]+'.'+(p[2]+n),sha=(c:string)=>c.repeat(64);
const A={version:v(1),sha256:sha('A')},B={version:v(2),sha256:sha('B')},C={version:v(3),sha256:sha('C')};
let receive:(event:{data:any})=>void=()=>{},manifest:any=B,failProbe=false,holdHint=false;
const calls:{id:number;cmd:string;args:any}[]=[],downloads:{id:number;cmd:string;args:any}[]=[],hints:any[]=[];
let native:any={phase:'available',stage:'download',...A,operationId:'saved',downloadActive:false};
const response=(id:number,result:any)=>receive({data:{id,result}});
class FixtureEvent extends Event{detail:unknown;constructor(type:string,init:{detail?:unknown}){super(type);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;
const win=new EventTarget() as any;win.chrome={webview:{addEventListener(_name:string,fn:typeof receive){receive=fn;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 if(req.cmd==='app.updateState')response(req.id,native);
 else if(req.cmd==='app.downloadUpdate'){downloads.push(req);native={...req.args,phase:'downloading',stage:'download',activeDownloadOperation:req.args.operationId,downloadActive:true};}
 else if(req.cmd==='app.checkUpdate'){assert.equal(req.args.background,true);failProbe?receive({data:{id:req.id,error:'probe offline'}}):response(req.id,manifest);}
 else if(req.cmd==='app.updatePredownloadTarget'){hints.push(req);if(!holdHint)response(req.id,true);}
 else if(req.cmd==='app.cancelUpdateDownload')response(req.id,true);
 else throw Error('FORBIDDEN '+req.cmd);
});}}};(globalThis as any).window=win;
const m=await import('../src/bridge/updateManager');let checks=0;
function check(label:string,fn:()=>void){fn();checks++;console.log('PASS '+label);}
const flush=async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
await m.ensureUpdateManager();const task=m.prepareUpdate({background:true});await flush();
await m.probeBackgroundSuccessor();
check('successor hint is durable before cancelling an incomplete old version',()=>{assert.equal(hints[0].args.version,B.version);assert.ok(calls.indexOf(hints[0])<calls.findIndex(x=>x.cmd==='app.cancelUpdateDownload'));assert.equal(downloads.length,1);});
manifest=C;await m.probeBackgroundSuccessor();
check('several releases arriving during cancellation collapse to the newest target',()=>{assert.equal(hints[1].args.version,C.version);assert.equal(downloads.length,1);});
const old=downloads[0];receive({data:{id:old.id,error:'Background download paused'}});await flush();
check('new target starts only after the old native task acknowledges termination',()=>{assert.equal(downloads.length,2);assert.equal(downloads[1].args.version,C.version);assert.equal(downloads.some(x=>x.args.version===B.version),false);assert.equal(m.updateSnapshot.version,C.version);});
receive({data:{event:'update.progress',data:{operationId:old.args.operationId,phase:'downloaded',version:A.version,percent:100}}});
check('late old completion cannot mark the new target ready',()=>{assert.equal(m.updateSnapshot.phase,'downloading');assert.equal(m.updateSnapshot.version,C.version);});
manifest=A;const hintCount=hints.length;await m.probeBackgroundSuccessor();
check('an older manifest cannot downgrade the active background target',()=>assert.equal(hints.length,hintCount));
failProbe=true;await assert.rejects(m.probeBackgroundSuccessor(),/offline/);failProbe=false;
check('failed manifest probe keeps a healthy current transfer running',()=>{assert.equal(downloads.length,2);assert.equal(m.updateSnapshot.phase,'downloading');});
receive({data:{id:downloads[1].id,result:'inert verified C'}});await task;
check('shared prepare resolves only for the actual final version with no installation',()=>{assert.equal(m.updateSnapshot.phase,'downloaded');assert.equal(m.updateInfo.value?.version,C.version);assert.equal(calls.some(x=>x.cmd==='app.installUpdate'),false);});
// A game starts while successor persistence is in flight. No late response may
// turn the paused download into a new request.
m.updateInfo.value=A;Object.assign(m.updateSnapshot,{phase:'available',stage:'download'});manifest=B;holdHint=true;
const gameTask=m.prepareUpdate({background:true});await flush();const probe=m.probeBackgroundSuccessor();await flush();
m.setBackgroundGamePaused(true);response(hints.at(-1)!.id,true);await probe;receive({data:{id:downloads.at(-1)!.id,error:'game pause'}});await gameTask;await flush();
const afterGame=downloads.length;
check('game entry cancels auto work and ignores a late successor-save receipt',()=>{assert.equal(downloads.length,afterGame);assert.equal(m.updateSnapshot.phase,'interrupted');});
assert.equal(await m.prepareUpdate({background:true}),false);
check('paused game blocks any new background download before IPC',()=>assert.equal(downloads.length,afterGame));
// Reloading the durable hint at confirmed game exit selects the latest version.
m.setBackgroundGamePaused(false);await m.restoreBackgroundTarget(B);
check('resume or restart can restore a saved successor before touching the old cache',()=>{assert.equal(m.updateInfo.value?.version,B.version);assert.equal(m.updateSnapshot.phase,'available');});
holdHint=false;const resumed=m.prepareUpdate({background:true});await flush();assert.equal(downloads.at(-1)!.args.version,B.version);response(downloads.at(-1)!.id,'inert B');await resumed;
check('game exit prepares the saved newer version rather than reviving old A',()=>assert.equal(m.updateSnapshot.version,B.version));
console.log('UPDATER_SUCCESSOR_SELFTEST_OK checks='+checks+' (mock IPC only; no product IO)');
