import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const parts=APP_VERSION.split('.').map(Number),v=(n:number)=>parts[0]+'.'+parts[1]+'.'+(parts[2]+n),sha='E'.repeat(64);
let receive:(event:{data:any})=>void=()=>{},now=1000000000,timerId=0,manifest={version:v(1),sha256:sha};
const timers=new Map<number,{at:number;fn:()=>unknown}>(),calls:{id:number;cmd:string;args:any}[]=[];
const listeners=new Set<()=>void>();const game={known:false,game:null as any,listeners};
(globalThis as any).__updaterGameFixture=game;
const realSet=globalThis.setTimeout,realClear=globalThis.clearTimeout,realNow=Date.now;
(globalThis as any).setTimeout=(fn:()=>unknown,ms:number)=>{timers.set(++timerId,{at:now+ms,fn});return timerId;};
(globalThis as any).clearTimeout=(id:number)=>timers.delete(id);Date.now=()=>now;
Object.defineProperty(globalThis,'navigator',{value:{onLine:true},configurable:true});
class FixtureEvent extends Event{detail:unknown;constructor(type:string,init:{detail?:unknown}){super(type);this.detail=init.detail;}}
(globalThis as any).CustomEvent=FixtureEvent;
let pending:any=null,native:any={phase:'idle',downloadActive:false},gate=false;
const policy={enabled:true,bootId:'fixture-boot',lastNotifiedBoot:'',failureCount:0,nextCheckAt:0};
const win=new EventTarget() as any;win.chrome={webview:{addEventListener(_name:string,fn:typeof receive){receive=fn;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 const result=(value:any)=>receive({data:{id:req.id,result:value}}),error=(text:string)=>receive({data:{id:req.id,error:text}});
 if(req.cmd==='app.updatePredownloadActivity'){gate=req.args.known&&req.args.idle;result(true);}
 else if(req.cmd==='app.updatePredownloadRead')result({...policy});
 else if(req.cmd==='app.updatePredownloadSchedule'){Object.assign(policy,req.args);result(true);}
 else if(req.cmd==='app.updateState')result({...native});
 else if(req.cmd==='app.checkUpdate'){assert.ok(gate&&game.known&&!game.game,'game must admit manifest IO');result(manifest);}
 else if(req.cmd==='app.downloadUpdate'){
  assert.ok(gate&&game.known&&!game.game,'game must admit package IO');
  if(req.args.cacheOnly){error('Verified cached update package is unavailable');return;}
  pending=req;native={...req.args,phase:'downloading',stage:'download',downloadActive:true,activeDownloadOperation:req.args.operationId};
 }else if(req.cmd==='app.cancelUpdateDownload'){
  result(true);if(pending){const old=pending;pending=null;native={...native,phase:'failed',downloadActive:false,activeDownloadOperation:''};receive({data:{id:old.id,error:'Background download paused'}});}
 }else throw Error('FORBIDDEN '+req.cmd);
});}}};(globalThis as any).window=win;
const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};
async function advance(ms:number){now+=ms;for(let loops=0;loops<100;loops++){const due=[...timers].filter(([,v])=>v.at<=now);if(!due.length)break;for(const[id,value]of due){timers.delete(id);void value.fn();}await flush();}}
function observe(known:boolean,running:boolean){game.known=known;game.game=running?{pid:77,processCreated:'fixture-77'}:null;for(const listener of listeners)listener();}
let checks=0;function check(label:string,fn:()=>void){fn();checks++;console.log('PASS '+label);}
try{
 const {startUpdatePredownload}=await import('../src/bridge/updatePredownloadService');
 const {updatePredownloadState}=await import('../src/bridge/updatePredownload');
 const service=startUpdatePredownload();await flush();await advance(15000);
 check('real lifecycle starts paused until game detection is initialized',()=>{assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate').length,0);assert.equal(updatePredownloadState.phase,'paused');});
 observe(true,true);await flush();await advance(1000);
 check('detected game blocks startup and keeps provider state paused',()=>{assert.equal(calls.filter(x=>x.cmd==='app.downloadUpdate').length,0);assert.equal(gate,false);});
 observe(true,false);await flush();await advance(1000);
 check('confirmed idle admission permits one background transfer',()=>{assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate').length,1);assert.equal(pending.args.version,v(1));});
 observe(true,true);await flush();await advance(1000);
 check('game entry cancels the current transfer and shows pause rather than failure',()=>{assert.equal(pending,null);assert.equal(updatePredownloadState.phase,'paused');assert.equal(policy.failureCount,0);});
 const before=calls.filter(x=>x.cmd==='app.checkUpdate'||x.cmd==='app.downloadUpdate').length;await advance(60000);
 check('periodic successor probe performs no network work during gameplay',()=>assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate'||x.cmd==='app.downloadUpdate').length,before));
 manifest={version:v(3),sha256:sha};observe(false,false);await flush();await advance(1000);
 check('unknown detection after game exit cannot be treated as idle',()=>assert.equal(pending,null));
 observe(true,false);await flush();await advance(1000);
 check('game exit refreshes the manifest and skips to the actual newest version',()=>assert.equal(pending.args.version,v(3)));
 service.stop();await flush();
 check('lifecycle stop releases watcher subscription timers and native admission',()=>{assert.equal(listeners.size,0);assert.equal(timers.size,0);assert.equal(gate,false);});
 console.log('UPDATER_SERVICE_GAME_SELFTEST_OK checks='+checks+' (real service/manager/provider, mocked shared game evidence and native IPC)');
}finally{globalThis.setTimeout=realSet;globalThis.clearTimeout=realClear;Date.now=realNow;}
