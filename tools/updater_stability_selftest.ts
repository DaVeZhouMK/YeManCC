import assert from 'node:assert/strict';
import { APP_VERSION } from '../src/version';
const mode=process.argv[2]||'missing-start';
let now=1e9,seq=0,receive:(e:any)=>void=()=>{};
const timers=new Map<number,{at:number;fn:()=>unknown}>(),calls:any[]=[],records:string[]=[];
const parts=APP_VERSION.split('.').map(Number),info={version:`${parts[0]}.${parts[1]}.${parts[2]+1}`,sha256:'A'.repeat(64)};
let state:any={phase:'idle',downloadActive:false},pending:any,manifestRequest:any;
let holdProbe=false;
let initializationRequest:any;
let clearBusy=false,clearError=false,retireLive=false,retireNoProof=false,retired=false,holdClear=false,clearRequest:any;
const originals={setTimeout,clearTimeout,now:Date.now};
(globalThis as any).setTimeout=(fn:any,ms:number)=>{timers.set(++seq,{fn,at:now+ms});return seq;};
(globalThis as any).clearTimeout=(id:number)=>timers.delete(id);Date.now=()=>now;
(globalThis as any).CustomEvent=class extends Event {detail:any;constructor(name:string,init:any){super(name);this.detail=init.detail;}};
const win:any=new EventTarget();
win.chrome={webview:{addEventListener(_name:string,cb:any){receive=cb;},postMessage(req:any){
 if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{
 const reply=(value:any)=>receive({data:{id:req.id,result:value}});
 switch(req.cmd){
 case 'app.updateState':if(mode==='late-initialize'&&!initializationRequest){initializationRequest=req;break;}reply({...state});break;
 case 'app.checkUpdate':if(mode==='late-check'||holdProbe){manifestRequest=req;break;}reply(info);break;
 case 'app.downloadUpdate':pending=req;if(mode!=='missing-start')state={...req.args,phase:'downloading',stage:'download',downloadActive:true,activeDownloadOperation:req.args.operationId,downloadedBytes:1,attempt:1};break;
 case 'app.retireUpdateDownload':retired=true;if(retireNoProof){reply({retired:false});break;}reply({retired:true,active:retireLive,activeOperation:retireLive?req.args.operationId:'',background:retireLive});break;
 case 'app.cancelUpdateDownload':reply(true);break;
 case 'app.clearUpdateCache':if(holdClear){clearRequest=req;break;}reply({ok:!clearBusy&&!clearError,busy:clearBusy,bytesFreed:clearBusy?0:4096,message:clearError?'部分缓存被占用，请重试':clearBusy?'更新仍在停止':'更新缓存已清理'});break;
 case 'app.installUpdate':reply(true);break;
 default:throw Error('UNEXPECTED_NATIVE '+req.cmd);
 }
 });}}};(globalThis as any).window=win;
const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};
async function advance(ms:number){now+=ms;for(let i=0;i<50;i++){const due=[...timers].filter(([,t])=>t.at<=now);if(!due.length)break;for(const[id,t]of due){timers.delete(id);void t.fn();}await flush();}}
function check(label:string,fn:()=>void){fn();records.push(label);}
try{
 const m=await import('../src/bridge/updateManager');
 if(mode==='late-initialize'){
  const initial=m.ensureUpdateManager();await flush();assert.ok(initializationRequest);
  assert.equal(await m.clearUpdateCache(),true);
  receive({data:{id:initializationRequest.id,result:{...info,operationId:'obsolete-startup',phase:'downloaded',stage:'install',downloadActive:false}}});await initial;
  check('late startup state cannot resurrect a cache cleared during initialization',()=>{assert.equal(m.updateInfo.value,null);assert.equal(m.updateSnapshot.phase,'idle');});
 }else {
 await m.ensureUpdateManager();
 if(mode==='late-check'){
  const checking=m.checkForUpdate(APP_VERSION);await flush();assert.ok(manifestRequest);
  assert.equal(await m.clearUpdateCache(),true);
  receive({data:{id:manifestRequest.id,result:info}});await checking;
  check('manifest reply arriving after clear cannot resurrect target',()=>{assert.equal(m.updateInfo.value,null);assert.equal(m.updateSnapshot.phase,'idle');});
 }else if(mode==='manual-protection'){
  await m.checkForUpdate(APP_VERSION);const downloading=m.prepareUpdate();await flush();
  check('manual download remains available and cleanup cannot interrupt it',()=>assert.equal(pending.args.background,undefined));
  assert.equal(await m.clearUpdateCache(),false);
  check('manual owner prevents native cleanup and retirement',()=>assert.equal(calls.some(c=>c.cmd==='app.clearUpdateCache'||c.cmd==='app.retireUpdateDownload'),false));
  receive({data:{id:pending.id,result:'fixture-package'}});assert.equal(await downloading,true);
  m.updateSnapshot.phase='installing';assert.equal(await m.clearUpdateCache(),false);
  check('install cannot be interrupted by cache maintenance',()=>assert.equal(calls.some(c=>c.cmd==='app.clearUpdateCache'),false));
 }else if(mode==='cleanup-error'||mode==='cleanup-busy'||mode==='cleanup-idle'){
  clearError=mode==='cleanup-error';clearBusy=mode==='cleanup-busy';
  const ok=await m.clearUpdateCache();
  check('cleanup receipt and byte count stay truthful',()=>{assert.equal(ok,!clearError&&!clearBusy);assert.equal(m.updateCacheState.bytesFreed,clearBusy?0:4096);assert.equal(m.updateCacheState.busy,false);});
  clearError=false;clearBusy=false;assert.equal(await m.clearUpdateCache(),true);
  check('partial/busy cleanup is retryable and never locks the page',()=>assert.equal(m.updateCacheState.busy,false));
 }else if(mode==='scheduler-quiet'){
  const {createUpdatePredownloadScheduler}=await import('../src/bridge/updatePredownloadScheduler');let checks=0,prepares=0;
  const policy={enabled:true,bootId:'boot',lastNotifiedBoot:'',failureCount:0,nextCheckAt:now};
  const scheduler=createUpdatePredownloadScheduler({now:()=>now,timer:()=>0,clearTimer:()=>{},online:()=>true,canRun:()=>true,readPolicy:async()=>({...policy}),setEnabled:async()=>({...policy}),saveSchedule:async()=>true,initialize:async()=>{},refresh:async()=>{},current:()=>({info:null,progress:{phase:'idle'}}),check:async()=>{checks++;},prepare:async()=>{prepares++;return false;},cancel:async()=>{},notify:async()=>true,notified:async()=>true});
  scheduler.invalidateCache();await scheduler.run();scheduler.wake(true);await scheduler.run();
  check('clear quiet period survives online/recovery wake and blocks refill',()=>{assert.equal(checks,0);assert.equal(prepares,0);});
  await advance(600001);await scheduler.run();
  check('quiet window expires and automatic checks resume',()=>assert.equal(checks,1));scheduler.stop();
 }else{
  await m.checkForUpdate(APP_VERSION,true);
  const task=m.prepareUpdate({background:true});await flush();assert.ok(pending);
  if(mode==='cleanup-probe'){
   holdProbe=true;const probing=m.probeBackgroundSuccessor();await flush();assert.ok(manifestRequest);
   receive({data:{id:pending.id,result:'package'}});await flush();
   let finished=false;void task.then(()=>finished=true);await flush();assert.equal(finished,false);
   assert.equal(await m.clearUpdateCache(),true);assert.equal(await task,false);
   check('clear releases successor-wait without waiting for a stuck manifest reply',()=>{assert.equal(m.updateSnapshot.phase,'idle');assert.equal(m.updateCacheState.busy,false);});
   receive({data:{id:manifestRequest.id,result:{...info,version:info.version.split('.').map((x,i)=>i===2?String(Number(x)+1):x).join('.')}}});await probing;
   check('late successor probe cannot write a target or restore cleared readiness',()=>{assert.equal(m.updateInfo.value,null);assert.equal(calls.some(c=>c.cmd==='app.updatePredownloadTarget'),false);});
  }else if(mode==='cleanup-running'||mode==='cleanup-no-proof'){
   retireNoProof=mode==='cleanup-no-proof';holdClear=mode==='cleanup-running';
   const clear=m.clearUpdateCache();await flush();
   if(retireNoProof){for(let i=0;i<50;i++)await advance(1000);assert.equal(await clear,false);check('unconfirmed owner blocks deletion and releases only cleanup UI busy',()=>{assert.equal(calls.some(c=>c.cmd==='app.clearUpdateCache'),false);assert.equal(m.updateCacheState.busy,false);});}
   else{
    assert.ok(clearRequest);assert.equal(await m.prepareUpdate({background:true}),false);await m.checkForUpdate(APP_VERSION,true);
    check('clear fences background POST before deleting and blocks parallel prepare',()=>{assert.ok(retired);assert.equal(calls.filter(c=>c.cmd==='app.downloadUpdate').length,1);assert.equal(calls.filter(c=>c.cmd==='app.checkUpdate').length,1);});
    receive({data:{id:clearRequest.id,result:{ok:true,busy:false,bytesFreed:8192,message:'已清理'}}});assert.equal(await clear,true);assert.equal(await task,false);
    receive({data:{id:pending.id,result:'obsolete-package'}});win.dispatchEvent(new CustomEvent('ipc:update.progress',{detail:{operationId:pending.args.operationId,phase:'downloaded'}}));await flush();
    check('late completion/event cannot restore erased ready cache',()=>{assert.equal(m.updateInfo.value,null);assert.equal(m.updateSnapshot.phase,'idle');assert.equal(m.updateCacheState.busy,false);});
   }
  }else if(mode==='slow-progress'){
   for(let i=0;i<25;i++){state.downloadedBytes++;await advance(15000);}
   check('slow but advancing background transfer is not cancelled by age',()=>assert.equal(retired,false));
   receive({data:{id:pending.id,result:'package'}});assert.equal(await task,true);
  }else if(mode==='active-stall'||mode==='transport-reset'){
   retireLive=true;
   if(mode==='transport-reset'){const ipc=await import('../src/bridge/ipc');ipc.rejectAllPending('WebView2 is recovering');await flush();}
   for(let i=0;i<16;i++)await advance(15000);let finished=false;void task.then(()=>finished=true);await flush();
   check('stalled live owner is cancelled but retained until actual exit',()=>{assert.equal(retired,true);assert.equal(finished,false);assert.equal(m.prepareUpdate({background:true}),task);});
   retireLive=false;state={phase:'idle',downloadActive:false};await advance(15000);assert.equal(await task,false);
   check('native stop proof releases stalled owner without installation',()=>{assert.equal(m.updateSnapshot.phase,'failed');assert.equal(calls.some(c=>c.cmd==='app.installUpdate'),false);});
  }else{
   for(let i=0;i<4;i++)await advance(15000);assert.equal(await task,false);
   check('never-started POST recovers after native queued-request fence',()=>{assert.equal(retired,true);assert.equal(m.updateSnapshot.phase,'failed');});
   const previousPhase=m.updateSnapshot.phase;receive({data:{id:pending.id,result:'late-package'}});win.dispatchEvent(new CustomEvent('ipc:update.progress',{detail:{operationId:pending.args.operationId,phase:'downloaded'}}));await flush();
   check('late retired IPC/events do not change recovered state',()=>assert.equal(m.updateSnapshot.phase,previousPhase));
  }
 }
 }
 console.log(JSON.stringify({status:'PASS',mode,checks:records.length,records,calls:calls.map(c=>c.cmd),scope:'Actual manager/scheduler/yeman/IPC; isolated memory transport and deterministic clock. No installed application or network.'}));
}finally{globalThis.setTimeout=originals.setTimeout;globalThis.clearTimeout=originals.clearTimeout;Date.now=originals.now;}
