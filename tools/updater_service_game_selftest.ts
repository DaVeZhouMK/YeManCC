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
let rejectActivity=false;
let pending:any=null,native:any={phase:'idle',downloadActive:false},gate=false;
const policy={enabled:true,bootId:'fixture-boot',lastNotifiedBoot:'',failureCount:0,nextCheckAt:0};
const win=new EventTarget() as any;win.chrome={webview:{addEventListener(_name:string,fn:typeof receive){receive=fn;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 const result=(value:any)=>receive({data:{id:req.id,result:value}}),error=(text:string)=>receive({data:{id:req.id,error:text}});
 if(req.cmd==='app.updatePredownloadActivity'){gate=!rejectActivity&&req.args.known&&req.args.idle;result(!rejectActivity);}
 else if(req.cmd==='app.updatePredownloadSet'){policy.enabled=req.args.enabled;result({...policy});}
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
async function advance(ms:number){const end=now+ms;let wakes=0;for(;;){const due=[...timers].filter(([,v])=>v.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];if(!due)break;assert(++wakes<1000,'service timer storm');now=due[1].at;timers.delete(due[0]);void due[1].fn();await flush();}now=end;await flush();return wakes;}
function observe(known:boolean,running:boolean){game.known=known;game.game=running?{pid:77,processCreated:'fixture-77'}:null;for(const listener of listeners)listener();}
let checks=0;function check(label:string,fn:()=>void){fn();checks++;console.log('PASS '+label);}
try{
 const {startUpdatePredownload}=await import('../src/bridge/updatePredownloadService');
 const {updatePredownloadState}=await import('../src/bridge/updatePredownload');
 const idleTimersOnly=process.argv.includes('--idle-timers');
 if(!idleTimersOnly){
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
 }
 else {
  const measurements:any[]=[];
  const delay=()=>Math.min(...[...timers.values()].map(t=>t.at-now));
  const unknown=startUpdatePredownload();await flush();await advance(15000);
  check('unknown game admission has no repeated service or scheduler timer',()=>{assert.equal(timers.size,0);assert.equal(gate,false);});
  unknown.stop();await flush();
  // The real shared policy source immediately notifies initialized subscribers.
  // Model that boundary without replacing the service/scheduler/provider/IPC.
  listeners.add=function(fn:()=>void){Set.prototype.add.call(this,fn);if(game.known)fn();return this;};
  game.known=true;game.game=null;policy.enabled=false;manifest={version:APP_VERSION,sha256:sha};
  const activities=calls.filter(c=>c.cmd==='app.updatePredownloadActivity').length;
  const service=startUpdatePredownload();await flush();await advance(15000);
  check('initialized source sends one startup native activity transaction',()=>assert.equal(calls.filter(c=>c.cmd==='app.updatePredownloadActivity').length-activities,1));
  const initialCalls=calls.length,wakes=await advance(120000);
  check('disabled steady service has zero timers callbacks or native reads',()=>{assert.equal(timers.size,0);assert.equal(wakes,0);assert.equal(calls.length,initialCalls);});
  measurements.push({case:'disabled',simulatedMs:120000,wakes,nativeCalls:calls.length-initialCalls,timers:timers.size});
  const {setUpdatePredownloadEnabled}=await import('../src/bridge/updatePredownload');
  await setUpdatePredownloadEnabled(true);await flush();await advance(1000);
  const checked=calls.filter(c=>c.cmd==='app.checkUpdate').length;assert.equal(checked,1);
  const enabledCalls=calls.length,enabledWakes=await advance(120000);
  check('enabled latest state keeps one actual deadline and no minute probe',()=>{assert.equal(timers.size,1);assert(delay()>5*60*60*1000);assert.equal(enabledWakes,0);assert.equal(calls.length,enabledCalls);});
  measurements.push({case:'enabled-latest',simulatedMs:120000,wakes:enabledWakes,nativeCalls:calls.length-enabledCalls,timers:timers.size,nextDelayMs:delay()});
  observe(true,true);await flush();await advance(1000);
  check('game admission clears idle clocks while preserving paused state',()=>{assert.equal(timers.size,0);assert.equal(gate,false);});
  observe(true,false);await flush();await advance(1000);assert(gate);assert.equal(calls.filter(c=>c.cmd==='app.checkUpdate').length,checked+1);
  (navigator as any).onLine=false;win.dispatchEvent(new Event('offline'));await flush();await advance(1000);
  check('offline idle has no clock and online event reactivates existing service',()=>assert.equal(timers.size,0));
  (navigator as any).onLine=true;win.dispatchEvent(new Event('online'));await flush();await advance(1000);assert.equal(timers.size,1);
  await setUpdatePredownloadEnabled(false);await flush();await advance(1000);assert.equal(timers.size,0);
  rejectActivity=true;observe(true,false);await flush();await advance(1000);
  check('rejected native admission retains only its sixty-second recovery probe',()=>{assert.equal(timers.size,1);assert.equal(delay(),59000);assert.equal(gate,false);});
  await advance(59000);assert.equal(gate,false);rejectActivity=false;await advance(60000);await advance(1000);assert.equal(gate,true);assert.equal(timers.size,0);
  const {updateSnapshot}=await import('../src/bridge/updateManager');
  updateSnapshot.installerActive=true;updateSnapshot.phase='installing';await flush();
  const late=[...timers.values()];assert(late.some(t=>t.at-now===60000));
  service.stop();await flush();const stoppedCalls=calls.length;
  for(const t of late)void t.fn();await flush();
  check('active installer observation survives OFF and stop cancels late callbacks',()=>{assert.equal(timers.size,0);assert.equal(listeners.size,0);assert.equal(gate,false);assert.equal(calls.length,stoppedCalls);});
  console.log('UPDATER_SERVICE_IDLE_MEASUREMENTS '+JSON.stringify(measurements));
 }
 console.log('UPDATER_SERVICE_GAME_SELFTEST_OK checks='+checks+' (real service/manager/provider, mocked shared game evidence and native IPC)');
}finally{globalThis.setTimeout=realSet;globalThis.clearTimeout=realClear;Date.now=realNow;}
