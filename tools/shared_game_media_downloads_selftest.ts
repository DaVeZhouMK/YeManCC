import {strict as assert} from 'node:assert';
import {MediaDownloads,downloadDescription,downloadInterruptionReason,type MediaDownloadState} from '../src/bridge/gameMediaDownloads';
import {getSharedPowerLifecycleDiagnostics} from '../src/bridge/powerLifecycleState';
if(process.argv.includes('--public-power-case')) {await publicMediaPowerCases();} else {
const events=new EventTarget();Object.assign(globalThis,{window:events});
let checks=0;const check=(value:unknown,message:string)=>{checks++;assert.ok(value,message);};const tick=()=>new Promise(resolve=>setImmediate(resolve));
let ready=false,enabled=true,listen:((state:MediaDownloadState)=>void)|null=null,calls:any[]=[],pending:((state:MediaDownloadState)=>void)[]=[];
const service=new MediaDownloads({ready:()=>ready,enabled:()=>enabled,listen:callback=>{listen=callback;return()=>{listen=null;};},invoke:((command:string,args:any)=>{calls.push({command,args});return new Promise(resolve=>pending.push(resolve as any));}) as any});
const stop=service.start();check(calls.length===0,'no default-on startup before saved settings load');ready=true;events.dispatchEvent(new Event('ui-settings:loaded'));check(calls.length===1&&calls[0].args.enabled,'default-on applies once loaded');check(calls[0].args.cpuThreshold===15,'default threshold is fifteen percent');check(calls[0].args.pauseReason==='capacity-unavailable','capacity must be known before download');enabled=false;service.sync();service.policy('game-running');check(calls.length===1,'configuration requests coalesce behind one in-flight write');pending.shift()!({statusRevision:1,enabled:true});await tick();check(calls.length===2&&calls[1].args.enabled===false&&calls[1].args.pauseReason==='game-running','newest disabled setting and policy win');listen!({statusRevision:5,enabled:false,phase:'paused'});pending.shift()!({statusRevision:2,enabled:false,phase:'starting'});await tick();check(service.state.statusRevision===5,'older IPC response cannot overwrite newer event');check(downloadDescription(service.state).includes('已关闭'),'off label');enabled=true;service.policy('');check(calls.length===3&&calls[2].args.enabled,'policy resume reuses native queue');pending.shift()!({statusRevision:6,enabled:true,phase:'downloading',current:{name:'Fixture'},bytes:25,totalBytes:100,total:4,completed:1});await tick();check(downloadDescription(service.state).includes('25%'),'shared byte progress label');check(downloadDescription({enabled:true,phase:'completed',total:4,completed:4,unavailable:1}).includes('已跳过'),'no source is distinct from a downloaded video');check(downloadDescription({enabled:true,resourcePauseReason:'on-battery'}).includes('电池模式'),'battery stop label');check(downloadDescription({enabled:true,resourcePauseReason:'cpu-busy',cpuThreshold:30}).includes('30%'),'configured CPU pause label');check(downloadDescription({enabled:true,resourcePauseReason:'startup-protection'}).includes('30 秒'),'startup protection label');check(downloadInterruptionReason('media-network-read-failed').includes('网络'),'network interruption has a user reason');check(downloadInterruptionReason('worker-stalled').includes('无进展'),'stalled interruption has a user reason');stop();events.dispatchEvent(new Event('ui-settings:changed'));check(calls.length===3&&listen===null,'stop releases event subscriptions without polling');
check(downloadDescription({enabled:true,phase:'downloading',activeCount:10,parallelSlots:10,total:12,completed:2,bytes:1,totalBytes:4}).includes('正在下载 10 个'),'shared ten-lane status');
check(downloadDescription({enabled:true,phase:'downloading',activeCount:5,resumedBytes:8192}).includes('断点续传'),'verified partial is visible in main UI');
check(downloadInterruptionReason('media-staging-budget-full').includes('暂存'),'staging pause has a user reason');
check(calls[0].args.catalog===false,'loaded production settings never request popular discovery');check(!downloadDescription({enabled:true,catalogStatus:'fetching'}).includes('热门榜'),'legacy catalog status does not display retired discovery');check(!downloadDescription({enabled:true,phase:'deferred',catalogStatus:'deferred'}).includes('热门榜'),'legacy catalog failure cannot block the local queue');
check(downloadInterruptionReason('media-video-selection-expired').includes('重新搜索'),'expired chosen video has an actionable status reason');check(downloadDescription({enabled:false,reason:'media-video-selection-expired',phase:'deferred'}).includes('重新搜索'),'auto-off does not hide selected video failure');check(downloadDescription({enabled:false,manualUntil:Date.now()+60000,manualTotal:1,manualCompleted:0,total:30,completed:20}).includes('0 / 1'),'selected request counts only its own target');
check(downloadDescription({enabled:true,pauseReason:'game-running',reason:'media-video-selection-expired',manualState:'deferred'}).includes('重新搜索'),'game pause does not hide a failed chosen-video request');
// The existing coordinator must stop its actual deferred work, not just relabel it.
{
 let allowed=true,id=0;const timers=new Map<number,()=>void>(),submitted:any[]=[],returns:((v:any)=>void)[]=[];
 const owner=new MediaDownloads({workAllowed:()=>allowed,ready:()=>true,enabled:()=>true,listen:()=>()=>{},schedule:((fn:any)=>{timers.set(++id,fn);return id;}) as any,cancel:((token:number)=>timers.delete(token)) as any,invoke:((command:string,args:any)=>{submitted.push({command,args});return new Promise(resolve=>returns.push(resolve));}) as any});
 const dispose=owner.start();returns.shift()!({statusRevision:1,enabled:true,resourcePauseReason:'startup-protection',startupProtectionUntil:Date.now()+30000});await tick();check(timers.size===1,'startup has one actual deferred check');
 allowed=false;owner.policy('wallpaper-on-battery');check(timers.size===0,'DC policy cancels existing startup timer');returns.shift()!({statusRevision:2,enabled:true,pauseReason:'wallpaper-on-battery',phase:'paused'});await tick();
 const before=submitted.length;await owner.refresh();await owner.recheck();check(submitted.length===before,'DC cannot issue read or self-heal IPC');await assert.rejects(()=>owner.requestNow(),/wallpaper-power/);checks++;check(submitted.length===before,'manual start cannot bypass DC');
 check(downloadDescription(owner.state).includes('离电关闭'),'native pause reason visible');
 allowed=true;owner.policy('capacity-unavailable');check(submitted.length===before+1,'AC retains original enabled preference');returns.shift()!({statusRevision:3,enabled:true,phase:'paused'});await tick();dispose();check(timers.size===0,'shutdown leaves no deferred checks');
}
{
 const timers:any[]=[];const owner=new MediaDownloads({workAllowed:()=>false,ready:()=>true,enabled:()=>true,listen:()=>()=>{},schedule:((fn:any)=>{timers.push(fn);return 1;}) as any,invoke:(async()=>{throw Error('unavailable');}) as any});const dispose=owner.start();await tick();check(timers.length===0,'unavailable native host cannot start a recurring retry on DC');dispose();
}
{
 const manual:MediaDownloadState={enabled:true,phase:'pending',manualUntil:Date.now()+60000,manualState:'requested',inventoryRequestId:'manual-current',inventoryReadyId:'old',inventoryAwaitUntil:Date.now()+10000,total:49,completed:49,remaining:0,workerPid:0,activeCount:0};
 check(downloadDescription(manual).includes('手动补齐中')&&downloadDescription(manual).includes('正在获取游戏清单'),'accepted manual inventory must not show a completed previous round');
 check(!downloadDescription(manual).includes('49 / 49'),'manual inventory preparation never borrows the previous completed counters');
 for(const reason of ['cpu-busy','cpu-unknown','cpu-recovering']){
  const label=downloadDescription({...manual,resourcePauseReason:reason,pauseReason:'game-running'});
  check(label.includes('手动补齐中')&&!label.includes('已暂停'),'manual feedback ignores only stale soft CPU and game pauses: '+reason);
 }
 check(downloadDescription({...manual,enabled:false}).includes('手动补齐中'),'accepted explicit manual round remains visible with automatic downloads off');
 for(const [reason,copy] of [['on-battery','电池模式'],['power-unknown','供电'],['startup-protection','启动保护']]){
  const label=downloadDescription({...manual,resourcePauseReason:reason});check(label.includes(copy)&&!label.includes('手动补齐中'),'manual feedback retains hard resource protection: '+reason);
 }
 for(const reason of ['battery-saver','low-battery','media-storage-unavailable','media-disk-space-low'])check(!downloadDescription({...manual,pauseReason:reason}).includes('手动补齐中'),'manual feedback retains hard policy pause: '+reason);
 check(downloadDescription({...manual,phase:'deferred',reason:'queue-unreadable'}).includes('暂不可读'),'unreadable diagnostic still precedes accepted manual feedback');
 check(downloadDescription({...manual,manualUntil:0,resourcePauseReason:'cpu-busy',remaining:1}).includes('CPU 占用'),'automatic work still displays CPU pause');
 check(downloadDescription({...manual,manualUntil:0,phase:'completed'}).includes('本轮已处理'),'settled automatic work still displays completion');
}
console.log(JSON.stringify({ok:true,checks,scope:'production download coordinator; deterministic settings/IPC events, no native startup'}));

}

async function publicMediaPowerCases():Promise<void>{
 const args=process.argv.slice(2),names=['sequence-held-read','denial-ready','generation-change','metadata-only','startup-sequence','held-recheck','late-failure','stop'];
 assert.equal(args.length,4);assert.equal(args[0],'--public-power-case');assert.equal(args[2],'--out');
 const selected=args[1];assert(selected==='all'||names.includes(selected),'Unknown public media power case');
 const {writeFileSync}=await import('node:fs');const {join,isAbsolute}=await import('node:path');assert(isAbsolute(args[3]));
 const results:any[]=[];const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};
 for(const name of selected==='all'?names:[selected]){
  const events=new EventTarget(),timers=new Map<number,{fn:()=>void,at:number}>(),calls:any[]=[],holds:any[]=[];let serial=0,now=0,hold=name!=='startup-sequence';
  class Detail extends Event {detail:any;constructor(type:string,detail:any){super(type);this.detail=detail;}}
  const original={window:(globalThis as any).window,document:(globalThis as any).document,setTimeout:globalThis.setTimeout,clearTimeout:globalThis.clearTimeout};
  const state:any={known:true,phase:'ready',generation:100,hardwareWritesAllowed:true,inputReady:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:1};
  Object.assign(globalThis,{window:events,document:{visibilityState:'visible',addEventListener(){},removeEventListener(){}},setTimeout:(fn:()=>void,ms:number)=>{const id=++serial;timers.set(id,{fn,at:now+ms});return id;},clearTimeout:(id:number)=>timers.delete(id),__mediaPowerRead:()=>Promise.resolve({...state})});
  const startupDeadline=Date.now()+30000;let downloads=0,rechecks=0;
  const invoke=async(command:string,args:any)=>{
   calls.push({command,args:{...args}});
   const status=command==='gameMedia.downloads',recheck=command==='gameMedia.background'&&args.recheck===true;
   if(status)downloads++;if(recheck)rechecks++;
   if((status&&downloads===1&&name!=='held-recheck'||recheck&&name==='held-recheck')&&hold){return new Promise((resolve,reject)=>holds.push({resolve,reject}));}
   return {statusRevision:20,enabled:true,phase:'paused',completed:2,remaining:1,...(name==='startup-sequence'?{resourcePauseReason:'startup-protection',startupProtectionUntil:startupDeadline}:{})};
  };
  const owner=new MediaDownloads({workAllowed:()=>true,ready:()=>true,enabled:()=>true,threshold:()=>15,listen:()=>()=>{},invoke:invoke as any,schedule:((fn:any,ms:any)=>globalThis.setTimeout(fn,ms)) as any,cancel:((id:any)=>globalThis.clearTimeout(id)) as any});
  let stop:(()=>void)|undefined,error:any;
  const publish=async(patch:any)=>{Object.assign(state,patch);events.dispatchEvent(new Detail('ipc:power.lifecycleChanged',{...state}));await flush();};
  try{
   stop=owner.start();await flush();assert.equal(downloads,1,'one initial public-admitted status read');
   if(name==='held-recheck'){await owner.refresh();void owner.recheck();await flush();assert.equal(rechecks,1);}
   const startupDue=name==='startup-sequence'?[...timers.values()][0]?.at:null;const before=downloads,writes=calls.filter(x=>x.command==='gameMedia.background'&&!x.args.recheck).length;assert.equal(writes,1,'initial already-confirmed configuration replayed at first Ready');
   if(name==='stop') {stop();stop=undefined;await publish({publicationRevision:2,observedNotificationSequence:2});assert.equal(downloads,before);}
   else if(name==='metadata-only'){await publish({publicationRevision:2});assert.equal(downloads,before,'publication-only update must not duplicate healthy/held work');}
   else if(name==='generation-change'){hold=false;await publish({generation:101,publicationRevision:2,observedNotificationSequence:2});assert.equal(downloads,before+1,'new generation must not join retired read');}
   else if(name==='denial-ready'){
    await publish({notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});assert.equal(downloads,before,'denied public state must not issue status read');
    hold=false;await publish({notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});assert.equal(downloads,before+1,'same-generation Ready must continue with a fresh read');
   }else {hold=false;await publish({publicationRevision:2,observedNotificationSequence:2});assert.equal(downloads,before+1,'same-generation notification must retire the held read before its return');}
   if(name==='startup-sequence'){assert.equal(timers.size,1,'one original startup observation timer');assert([...timers.values()][0].at<=startupDue!+1,'sequence change extended original startup deadline');}
   assert.equal(calls.filter(x=>x.command==='gameMedia.background'&&!x.args.recheck).length,writes,'power observation must not replay background policy writes');
   if(!['metadata-only','stop'].includes(name))assert.equal(owner.state.completed,2,'current read must publish before old return');
   for(const held of holds.splice(0)){if(name==='late-failure')held.reject(Error('retired read failed'));else held.resolve({statusRevision:99,enabled:true,phase:'downloading',completed:99});}await flush();
   if(!['metadata-only','stop'].includes(name))assert.equal(owner.state.completed,2,'late old result/failure must not overwrite current snapshot');
   if(name==='held-recheck'){await owner.recheck();assert.equal(rechecks,2,'new explicit recheck must not retain previous checking promise');}
   results.push({name,passed:true,downloads,rechecks,backgroundWrites:writes});
  }catch(e){error=e;results.push({name,passed:false,error:String(e),downloads,rechecks});}
  finally{
   for(const held of holds.splice(0))held.resolve({statusRevision:1});await flush();stop?.();await flush();
   assert.equal(getSharedPowerLifecycleDiagnostics().subscriberCount,0,'shared subscriber returned');assert.equal(timers.size,0,'all public and consumer timers returned');
   Object.assign(globalThis,{window:original.window,document:original.document,setTimeout:original.setTimeout,clearTimeout:original.clearTimeout});delete(globalThis as any).__mediaPowerRead;
  }
  if(error)break;
 }
 const result={ok:results.length>0&&results.every(x=>x.passed),cases:results,scope:'actual MediaDownloads+actual public power source;memory-only IPC/settings/wallpaper facts and logical timers;no native/network/installed application',realDownloads:0,realPolicyWrites:0};
 writeFileSync(join(args[3],'MEDIA-POWER-RESULT.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
}

async function publicMediaIpcCase(name:any,f:any){
 const check=(value:any,why:string)=>assert.ok(value,why);await f.start();
 const writes=()=>f.messages.filter((x:any)=>x.cmd==='gameMedia.background'&&Object.hasOwn(x.args,'enabled'));
 const manual=()=>f.messages.filter((x:any)=>x.cmd==='gameMedia.background'&&x.args.manual===true);
 if(name==='policy-lost-return'||name==='policy-remote-failure'){
  check(writes().length===1,'one original in-flight configuration');await f.publish({publicationRevision:2,observedNotificationSequence:2});
  check(writes().length===1,'a current snapshot must not replay an unknown transaction immediately');
  if(name==='policy-lost-return'){await f.advance(4999);check(writes().length===1,'original5s response clock was shortened');await f.advance(5000);}
  else {f.reply(f.heldPolicy,{error:'original Native configure rejected before commit'});await f.flush();}
  check(writes().length===2,'interrupted desired configuration remained stranded after its original IPC terminal');
  check(f.service.state.enabled===true,'current saved preference did not recover through original sync owner');
  f.reply(f.heldPolicy,{result:{statusRevision:99,enabled:false,pauseReason:'obsolete',cpuThreshold:5}});await f.flush();check(f.service.state.enabled===true,'late retired policy receipt overwrote current state');
 }else if(name==='policy-retry-deadline'){
  f.reply(f.heldPolicy,{error:'current original failure'});await f.flush();await f.advance(100);await f.publish({publicationRevision:2,observedNotificationSequence:2});await f.advance(999);check(writes().length===1,'notification shortened original retry');await f.advance(1000);check(writes().length===2,'notification reset original retry deadline');
 }else if(name==='policy-denied-terminal'){
  await f.publish({notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});await f.advance(5000);check(writes().length===1,'denied public scope automatically reasserted configuration');await f.publish({notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});check(writes().length===2&&f.service.state.enabled===true,'Ready after old terminal did not recover saved configuration');
 }else if(name==='policy-read-confirms-same-intent'){
  f.native.enabled=true;f.native.statusRevision++;await f.publish({publicationRevision:2,observedNotificationSequence:2});await f.advance(5000);
  f.bus.dispatchEvent(new Event('ui-settings:changed'));await f.flush();check(writes().length===1,'fresh exact matching Native state was not consumed as the same original policy confirmation');
 }else if(name==='policy-latest-setting'){
  f.enabled=false;f.bus.dispatchEvent(new Event('ui-settings:changed'));await f.flush();await f.publish({publicationRevision:2,observedNotificationSequence:2});
  check(writes().length===1,'latest OFF bypassed the original unknown-completion wait');await f.advance(5000);
  check(writes().length===2&&writes()[1].args.enabled===false,'latest OFF was lost behind old ON transaction');check(f.service.state.enabled===false,'latest saved OFF did not win');
 }else if(name==='manual-timeout'){
  const old=f.service.requestNow().then((v:any)=>({v}),(error:any)=>({error}));await f.flush();const request=manual()[0];await f.publish({publicationRevision:2,observedNotificationSequence:2});
  await f.advance(7999);check(manual().length===1,'manual action replayed before original8s expiry');await f.advance(8000);const ended=await old;
  check(ended.error?.code==='IPC_TIMEOUT','actual IPC timeout classification missing');check(manual().length===1,'unknown manual completion automatically replayed');
  f.reply(request,{result:{statusRevision:99,enabled:true,manualState:'requested'}});await f.flush();check(f.service.state.manualState===undefined,'late timed-out manual receipt changed current state');
  const next=f.service.requestNow();await f.flush();check(manual().length===2,'new explicit manual confirmation could not enter');const value={...f.native,statusRevision:f.native.statusRevision+1,manualState:'requested'};f.reply(manual()[1],{result:value});check(await next===value,'current explicit caller lost its actual receipt');
 }else if(name==='manual-reply-crosses-notification'){
  const old=f.service.requestNow();await f.flush();const request=manual()[0];await f.publish({publicationRevision:2,observedNotificationSequence:2});const value={...f.native,statusRevision:99,manualState:'old-requested'};f.reply(request,{result:value});check(await old===value,'caller must retain original Native manual receipt');await f.flush();check(f.service.state.manualState===undefined&&manual().length===1,'retired manual receipt crossed scope or replayed itself');
 }else if(name==='stop-bridge-loss'){
  f.stop();f.rejectAllPending('fixture WebView loss');await f.flush();await f.publish({publicationRevision:2,observedNotificationSequence:2});check(writes().length===1,'stopped transaction recreated background writes');
 }else throw Error('Unknown actual IPC case');
 return {name,passed:true,policyRequests:writes().length,manualRequests:manual().length,commands:f.messages.length};
}
