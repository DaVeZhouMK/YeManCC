import {strict as assert} from 'node:assert';
import {downloadDescription,MediaDownloads} from '../src/bridge/gameMediaDownloads';
import {SharedGameMedia} from '../src/bridge/sharedGameMedia';
const powerPublications=new WeakMap<EventTarget,number>();
function nativePower(bus:EventTarget,phase:'ready'|'suspending',generation:number,flags:Record<string,unknown>={}){const revision=(powerPublications.get(bus)||0)+1;powerPublications.set(bus,revision);bus.dispatchEvent(new CustomEvent('ipc:power.lifecycleChanged',{detail:{phase,generation,known:true,hardwareWritesAllowed:phase==='ready',notificationPending:false,publicationRevision:revision,observedNotificationSequence:revision,...flags}}));}
async function admitFixture(f:ReturnType<typeof fixture>,generation=1){const calls=f.calls.length;nativePower(f.bus,'ready',generation);assert.equal(f.calls.length,calls+1,'current Native Ready needs one fresh download status read');assert.equal(f.calls.at(-1).command,'gameMedia.downloads');f.pending.shift()!.resolve(f.service.state);await tick();}
const reports:any[]=[];let checks=0;const check=(x:unknown,why:string)=>{checks++;assert.ok(x,why);};const tick=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(){const bus=new EventTarget();Object.assign(globalThis,{window:bus});let enabled=true;const calls:any[]=[],pending:Array<{resolve:(x:any)=>void;reject:(x:any)=>void}>=[],timers=new Map<number,()=>void>();let timerId=0,listener:((state:any)=>void)|null=null;const service=new MediaDownloads({ready:()=>true,enabled:()=>enabled,threshold:()=>15,listen:callback=>{listener=callback;return()=>{listener=null;};},invoke:((command:string,args:any)=>{calls.push({command,args});return new Promise((resolve,reject)=>pending.push({resolve,reject}));}) as any,schedule:((fn:()=>void)=>{timers.set(++timerId,fn);return timerId;}) as any,cancel:((id:number)=>timers.delete(id)) as any} as any);return{service,bus,calls,pending,timers,setEnabled:(x:boolean)=>enabled=x,emit:(s:any)=>listener?.(s)};}
const scenarios:Array<{name:string;fn:()=>Promise<void>}>=[];async function run(name:string,fn:()=>Promise<void>){scenarios.push({name,fn});}
await run('unchanged-settings-do-not-resubmit',async()=>{const f=fixture(),stop=f.service.start();f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();for(let i=0;i<100;i++)f.bus.dispatchEvent(new Event('ui-settings:changed'));await tick();check(f.calls.length===1,'unrelated appearance settings trigger redundant native writes/wakes');stop();});
await run('duplicate-progress-revision-does-not-render',async()=>{const f=fixture(),stop=f.service.start();f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();let updates=0;const off=f.service.subscribe(()=>updates++);const value={statusRevision:2,enabled:true,phase:'downloading',bytes:50,totalBytes:100};f.emit(value);for(let i=0;i<100;i++)f.emit({...value});check(updates===1,'same authoritative progress revision repeatedly updates the UI');off();stop();});
await run('lost-configuration-return-retries-itself',async()=>{const f=fixture(),stop=f.service.start();f.pending.shift()!.reject(Error('temporary IPC loss'));await tick();check(f.timers.size===1,'temporary IPC failure never schedules recovery without user interaction');const timer=[...f.timers][0];f.timers.delete(timer[0]);timer[1]();check(f.calls.length===2,'scheduled recovery does not resubmit configuration');f.pending.shift()!.resolve({statusRevision:2,enabled:true});await tick();check(f.timers.size===0,'successful return leaves an idle retry timer');stop();});
await run('old-return-cannot-override-restarted-owner',async()=>{const f=fixture(),stop=f.service.start();const old=f.pending.shift()!;stop();const stopNew=f.service.start();old.resolve({statusRevision:99,enabled:false});await tick();check(f.service.state.enabled!==false,'previous lifecycle response is accepted by the restarted service');stopNew();});
await run('on-off-on-returns-to-latest-intent',async()=>{const f=fixture(),stop=f.service.start();f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();f.setEnabled(false);f.service.sync();const disabling=f.pending.shift()!;f.setEnabled(true);f.service.sync();disabling.resolve({statusRevision:2,enabled:false});await tick();check(f.calls.length===3&&f.calls.at(-1).args.enabled===true,'late off return loses the newest on intent');f.pending.shift()!.resolve({statusRevision:3,enabled:true});await tick();check(f.service.state.enabled===true,'latest on return was not accepted');stop();});
await run('ambiguous-off-failure-reasserts-latest-on',async()=>{const f=fixture(),stop=f.service.start();f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();f.setEnabled(false);f.service.sync();const disabling=f.pending.shift()!;f.setEnabled(true);f.service.sync();disabling.reject(Error('return lost after native write'));await tick();check(f.calls.length===3&&f.calls.at(-1).args.enabled,'ambiguous failure incorrectly assumes the old on state is still native');stop();});
await run('library-refresh-cannot-bypass-native-protection',async()=>{const commands:string[]=[];const owner=new SharedGameMedia({backgroundEnabled:()=>true,invoke:(async(command:string)=>{commands.push(command);if(command==='gameMedia.resolve')return {key:'steam-570',bindingRevision:2,status:'missing',refreshRequested:true};if(command==='gameMedia.background')return {};throw Error('renderer preparation bypassed resource protection');}) as any});try{await owner.ensure({appId:570,name:'fixture',verified:true},'library',false,true);}catch{}check(commands.includes('gameMedia.background')&&!commands.includes('gameMedia.begin'),'library refresh still uses the foreground preparation lane');owner.stop();});

// A lost startup-complete push must heal even when there are no remaining jobs.
// Keep this on the existing registered production coordinator adapter.
function startupFixture(deadline=true){
 const bus=new EventTarget();Object.assign(globalThis,{window:bus});
 let enabled=true,seq=0,listener:any,holding=false,failRead=false;const held:Array<()=>void>=[];
 const timers=new Map<number,{fn:()=>void;ms:number}>(),calls:any[]=[];
 let native:any={statusRevision:1,enabled:true,pauseReason:'game-running',resourcePauseReason:'startup-protection',startupProtectionUntil:deadline?Date.now()+30000:undefined,remaining:0,total:0};
 const service=new MediaDownloads({enabled:()=>enabled,ready:()=>true,threshold:()=>15,listen:fn=>{listener=fn;return()=>listener=null;},schedule:((fn:()=>void,ms:number)=>{timers.set(++seq,{fn,ms});return seq;}) as any,cancel:((id:number)=>timers.delete(id)) as any,invoke:(async(command:string,args:any,options:any)=>{
  check(options?.timeoutMs===5000,'wallpaper status and configuration calls have a bounded five-second wait');
  calls.push({command,args});if(command==='gameMedia.downloads'){if(failRead)throw Error('fixture bridge unavailable');if(holding){const snapshot={...native};return new Promise(resolve=>held.push(()=>resolve(snapshot)));}}
  return {...native};
 }) as any});
 const stop=service.start();
 const fire=async()=>{const t=[...timers][0];check(!!t,'startup protection must schedule its own recovery without remaining work');timers.delete(t[0]);t[1].fn();await tick();};
 return {service,stop,timers,calls,bus,fire,change:(v:any,push=false)=>{native={...native,...v,statusRevision:native.statusRevision+1};if(push)listener?.({...native});},off:()=>{enabled=false;bus.dispatchEvent(new Event('ui-settings:changed'));},hold:()=>holding=true,release:()=>held.shift()?.(),fail:()=>failRead=true};
}
await run('startup-deadline-heals-missed-completion-with-empty-queue',async()=>{
 const f=startupFixture();try{await tick();check(f.timers.size===1,'one deadline wake despite zero remaining jobs');const ms=[...f.timers.values()][0].ms;check(ms>=29000&&ms<=30000,'native deadline schedules only the remainder of the mandatory protection');
 f.change({resourcePauseReason:'',startupProtectionUntil:0,phase:'paused'});await f.fire();
 check(f.calls.at(-1).command==='gameMedia.downloads','deadline uses a read-only status snapshot');check(f.service.state.resourcePauseReason==='','lost completion event recovered from native snapshot');check(f.service.state.pauseReason==='game-running','no local claim that downloads are allowed');check(f.timers.size===0,'healthy empty queue has no resident polling');
 }finally{f.stop();}
});
await run('startup-stale-or-unavailable-return-retries-slowly-once',async()=>{
 const f=startupFixture(false);try{await tick();check([...f.timers.values()][0]?.ms===30000,'legacy snapshot without deadline has one conservative thirty-second fallback');await f.fire();check(f.service.state.resourcePauseReason==='startup-protection','same revision never locally clears protection');check(f.timers.size===1&&[...f.timers.values()][0].ms===20000,'unchanged startup status retains a single slow recovery');f.fail();await f.fire();check(f.timers.size===1&&[...f.timers.values()][0].ms===20000,'failed IPC also retains bounded slow recovery');}finally{f.stop();}
});
await run('startup-off-and-disposal-cancel-late-wakes',async()=>{
 const f=startupFixture();try{await tick();f.off();await tick();check(f.timers.size===0,'saved OFF cancels startup recovery');const before=f.calls.length;f.stop();f.bus.dispatchEvent(new Event('pageshow'));await tick();check(f.calls.length===before,'disposed page listener cannot query or enable downloads');}finally{f.stop();}
 const g=startupFixture();try{await tick();g.hold();const pending=g.service.refresh();g.stop();g.change({resourcePauseReason:'',startupProtectionUntil:0});g.release();await pending;check(g.service.state.resourcePauseReason==='startup-protection','old lifecycle snapshot is fenced');check(g.timers.size===0,'late snapshot cannot rearm a stopped lifecycle');}finally{g.stop();}
});
await run('startup-wake-coalesces-and-fences-stale-returns',async()=>{
 const f=startupFixture();try{await tick();f.hold();const before=f.calls.length;f.bus.dispatchEvent(new Event('pageshow'));f.bus.dispatchEvent(new Event('focus'));await tick();check(f.calls.length===before+1,'concurrent visibility returns share one snapshot');f.change({resourcePauseReason:'',startupProtectionUntil:0},true);f.change({resourcePauseReason:'startup-protection'});f.release();await tick();check(f.service.state.resourcePauseReason==='','older in-flight snapshot cannot resurrect startup protection after a newer push');check(f.timers.size===0,'completed protection stays dormant after a stale read');}finally{f.stop();}
});
await run('startup-sleep-stops-timer-and-resume-refreshes-truth',async()=>{
 const f=startupFixture();try{await tick();f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:2}}));check(f.timers.size===0,'suspend removes startup timer');const before=f.calls.length;f.bus.dispatchEvent(new Event('focus'));await tick();check(f.calls.length===before,'suspended appearance never queries status');f.change({resourcePauseReason:'on-battery',startupProtectionUntil:0});f.bus.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:2}}));await tick();check(f.service.state.resourcePauseReason==='on-battery','wake shows fresh battery reason without inventing permission');check(f.timers.size===0,'awake battery state has no startup polling');}finally{f.stop();}
});
await run('startup-snapshot-before-sleep-cannot-block-or-overwrite-wake',async()=>{
 const f=startupFixture();try{await tick();f.hold();const old=f.service.refresh();f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:2}}));f.change({resourcePauseReason:'on-battery',startupProtectionUntil:0});const before=f.calls.length;f.bus.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:2}}));await tick();check(f.calls.length===before+1,'wake gets a fresh read independently of an old sleeping request');f.release();await old;check(f.service.state.resourcePauseReason==='startup-protection','old read does not publish across sleep generation');f.release();await tick();check(f.service.state.resourcePauseReason==='on-battery','new generation publishes true power admission');check(f.timers.size===0,'old read cannot recreate the startup timer');}finally{f.stop();}
});
await run('startup-S0-Ready-refreshes-true-policy-without-duplicate-reads',async()=>{
 const f=startupFixture();try{await tick();f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:2}}));
  check(f.timers.size===0,'S0 suspension cancels appearance status timers');f.change({resourcePauseReason:'on-battery',startupProtectionUntil:0});
  const before=f.calls.length;f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:2,source:'sleep-intent-canceled'}}));nativePower(f.bus,'ready',2);await tick();
  check(f.calls.length===before+1&&f.service.state.resourcePauseReason==='on-battery','S0 Ready must refresh the real status even without normal power.resumed');
  f.bus.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:2}}));f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:2}}));await tick();
  check(f.calls.length===before+1&&f.timers.size===0,'duplicate Ready has no extra read or idle timer');
  f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:3}}));f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:2}}));f.bus.dispatchEvent(new Event('focus'));await tick();
  check(f.calls.length===before+1,'old generation cannot release status suspension');
  f.stop();f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:3}}));await tick();check(f.calls.length===before+1,'disposed S0 event cannot restart status reads');
 }finally{f.stop();}
});
await run('older-recheck-cannot-resubmit-settings-after-newer-native-push',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();const checking=f.service.recheck(),old=f.pending.shift()!;
 f.emit({statusRevision:3,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});const calls=f.calls.length;old.resolve({statusRevision:2,enabled:false,pauseReason:'game-running',cpuThreshold:5});await checking;await tick();
 check(f.service.state.statusRevision===3,'older recheck replaced a newer native push');check(f.calls.length===calls,'rejected old recheck still resubmitted settings and woke native work');}finally{stop();}
});
await run('same-revision-recheck-still-repairs-a-real-native-setting-mismatch',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();f.emit({statusRevision:2,enabled:false,pauseReason:'capacity-unavailable',cpuThreshold:15});const checking=f.service.recheck();f.pending.shift()!.resolve({statusRevision:2,enabled:false,pauseReason:'capacity-unavailable',cpuThreshold:15});await checking;await tick();
 check(f.calls.length===3&&f.calls.at(-1).args.enabled===true,'equal-revision authoritative mismatch did not repair settings');f.pending.shift()!.resolve({statusRevision:3,enabled:true});await tick();check(f.service.state.enabled===true,'valid repair response was lost');}finally{stop();}
});
for(const command of ['requestNow','cancelManual'] as const)for(const boundary of ['restart','resume'])await run(`manual-${command}-receipt-cannot-cross-${boundary}`,async()=>{
 const f=fixture();let stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();const pending=f.service[command](),old=f.pending.shift()!;let fresh:any;
 if(boundary==='restart'){stop();stop=f.service.start();fresh=f.pending.shift()!;}else{f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:7}}));f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:7}}));fresh=f.pending.shift()!;}
 // Native revision order remains 1 -> old command 2 -> new read/config 3.
 // The old return lands while the new generation is still waiting for truth.
 await tick();let updates=0;const off=f.service.subscribe(()=>updates++);const receipt={statusRevision:2,enabled:true,manualState:command==='requestNow'?'requested':'cancelled',manualUntil:command==='requestNow'?Date.now()+60000:0};old.resolve(receipt);const returned=await pending;await tick();off();
 check(returned===receipt,'caller lost the actual native command return');check(f.service.state.statusRevision===1&&updates===0,'old explicit command return mutated a new owner/read generation');fresh.resolve({statusRevision:3,enabled:true});await tick();check(f.service.state.statusRevision===3,'current native state failed to publish after the old command return');}finally{stop();}
});
await run('suspended-recheck-is-inert-and-new-generation-can-check-independently',async()=>{
 const f=fixture(),stop=f.service.start();let old:any;try{f.pending.shift()!.resolve({statusRevision:1,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();const previous=f.service.recheck();old=f.pending.shift()!;f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:8}}));const calls=f.calls.length;
 void f.service.recheck();check(f.calls.length===calls,'suspended coordinator started a repair IPC');f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:8}}));const freshRead=f.pending.shift()!;const next=f.service.recheck();check(next!==previous&&f.calls.length===calls+2,'old pre-suspend repair blocks a current-generation repair');const freshCheck=f.pending.shift()!;
 freshCheck.resolve({statusRevision:3,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15,resourcePauseReason:'on-battery'});await next;old.resolve({statusRevision:2,enabled:false});await previous;freshRead.resolve({statusRevision:4,enabled:true,resourcePauseReason:'on-battery'});await tick();
 check(f.service.state.statusRevision===4&&f.service.state.resourcePauseReason==='on-battery','old repair return published or repaired policy across a read generation');check(f.calls.length===calls+2,'old repair return created an extra settings IPC');}finally{stop();old?.resolve({});for(const p of f.pending)p.resolve({});}
});
for(const command of ['requestNow','cancelManual'] as const)await run(`current-${command}-receipt-remains-visible-and-returned`,async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();let updates=0;const off=f.service.subscribe(()=>updates++);const waiting=f.service[command]();const value={statusRevision:2,enabled:true,manualState:command==='requestNow'?'requested':'cancelled'};f.pending.shift()!.resolve(value);const actual=await waiting;off();
 check(actual===value&&f.service.state===value&&updates===1,'current command did not return and publish its real native receipt exactly once');}finally{stop();}
});
const readableProgress=(revision=20)=>({statusRevision:revision,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15,phase:'downloading',bytes:50,totalBytes:100,total:4,completed:1,remaining:3,workerPid:17});
const unreadableProgress=(reason='queue-unreadable')=>({enabled:false,phase:'deferred',reason,workerPid:0});
for(const reason of ['queue-unreadable','media-download-checkpoint-upgrade-required'])await run(`unversioned-${reason}-is-visible-and-keeps-native-revision-floor`,async()=>{
 const f=fixture(),stop=f.service.start();try{const good=readableProgress();f.pending.shift()!.resolve(good);await tick();let updates=0,completions=0;const off=f.service.subscribe(()=>updates++),offCompleted=f.service.watchCompleted(()=>completions++);const error=unreadableProgress(reason);f.emit(error);const displayed=f.service.state;
 check(displayed.reason===reason&&displayed.phase==='deferred','unversioned native read error was swallowed after authoritative progress');const label=downloadDescription(displayed);check(label.includes(reason==='queue-unreadable'?'不可读':'待更新')&&!label.includes('已关闭')&&!label.includes('已处理'),'read failure was presented as saved OFF or successful progress');
 check(displayed.remaining===3&&displayed.completed===1&&displayed.activeCount===0&&displayed.workerPid===0,'unavailable projection lost retained recovery counts or claimed active work');
 for(let n=0;n<40;n++)f.emit({...error});check(updates===1,'duplicate unavailable status repeatedly renders');f.emit(readableProgress(19));check(f.service.state===displayed&&updates===1,'unavailable display erased the authoritative revision floor');
 f.emit({...good});check(f.service.state.statusRevision===20&&f.service.state.phase==='downloading'&&updates===2,'same-revision healthy native truth could not clear the error');f.emit({...good});check(updates===2,'restored duplicate progress rendered again');check(f.calls.length===1&&f.timers.size===0&&completions===0,'unavailable presentation creates retry IPC, a timer or false completion refresh');off();offCompleted();
 }finally{stop();}
});
for(const method of ['refresh','recheck','requestNow','cancelManual'] as const)await run(`older-${method}-unversioned-error-cannot-cover-a-newer-push`,async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve(readableProgress());await tick();const waiting=f.service[method](),old=f.pending.shift()!;const fresh=readableProgress(21);f.emit(fresh);const error=unreadableProgress();old.resolve(error);const returned=await waiting;
 check(f.service.state===fresh,'old unversioned return covered a newer healthy native push');check(f.calls.length===2,'rejected old read error submitted configuration or replayed a command');if(method==='requestNow'||method==='cancelManual')check(returned===error,'manual caller lost the actual unavailable command receipt');
 }finally{stop();}
});
await run('same-revision-read-clears-a-current-transport-error',async()=>{
 const f=fixture(),stop=f.service.start();try{const good=readableProgress();f.pending.shift()!.resolve(good);await tick();const failed=f.service.recheck();f.pending.shift()!.reject(Error('owned current transport failure'));await failed;check(f.service.state.reason==='host-unavailable','current transport failure lost its diagnostic');
 const read=f.service.refresh();f.pending.shift()!.resolve({...good});await read;check(f.service.state.statusRevision===20&&f.service.state.phase==='downloading'&&!f.service.state.reason,'same-version healthy read could not clear a local transport error');check(f.calls.length===3&&f.timers.size===0,'error recovery replayed configuration or created a timer');
 f.emit({...good,pauseReason:"",statusRevision:21,checkpointReadable:true,recoveryRequired:true,reason:'queue-recovery-required'});let label=downloadDescription(f.service.state);check(label.includes('50%')&&label.includes('历史记录待确认')&&!label.includes('不可读'),'recovered current progress is hidden by historical recovery debt');
 f.emit({...good,statusRevision:22,checkpointReadable:false,recoveryRequired:true,reason:'queue-recovery-required',phase:'deferred',workerPid:0});label=downloadDescription(f.service.state);check(label.includes('不可读')&&!label.includes('已关闭')&&!label.includes('已处理'),'unreadable current checkpoint is mislabeled as OFF or successful work');

 }finally{stop();}
});
for(const kind of ['recheck','configuration'] as const)await run(`older-${kind}-transport-failure-cannot-cover-a-newer-push`,async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve(readableProgress());await tick();let waiting:Promise<unknown>|undefined;if(kind==='recheck')waiting=f.service.recheck();else{f.setEnabled(false);f.service.sync();}const old=f.pending.shift()!;const fresh={...readableProgress(21),enabled:kind==='recheck'};f.emit(fresh);old.reject(Error('owned obsolete transport failure'));if(waiting)await waiting;await tick();
 check(f.service.state===fresh,'obsolete transport failure replaced a newer healthy push with host-unavailable');check(f.calls.length===2,'transport failure replayed work immediately');
 }finally{stop();}
});
await run('observer-sync-failure-keeps-push-siblings-and-completion',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true,completed:0});await tick();let updates=0,completed=0;
 const bad=f.service.subscribe(()=>{throw Error('owned display observer');}),good=f.service.subscribe(()=>updates++),offCompleted=f.service.watchCompleted(()=>completed++);
 const value={statusRevision:2,enabled:true,completed:1};let error:unknown;try{f.emit(value);}catch(e){error=e;}
 check(!error,'display observer escaped the accepted native push');check(f.service.state===value&&updates===1&&completed===1,'one observer blocked siblings or completion notification');
 f.emit({...value});check(updates===1&&completed===1,'duplicate native revision redelivered after observer failure');check(f.calls.length===1&&f.timers.size===0,'display failure replayed work or created a recovery timer');bad();good();offCompleted();
 }finally{stop();}
});
await run('observer-async-rejection-is-contained',async()=>{
 const f=fixture(),stop=f.service.start(),rejections:unknown[]=[];const onRejected=(error:unknown)=>rejections.push(error);process.on('unhandledRejection',onRejected);
 try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();let updates=0;const bad=f.service.subscribe(async()=>{throw Error('owned async observer');}),good=f.service.subscribe(()=>updates++);
 f.emit({statusRevision:2,enabled:true});await tick();await tick();check(rejections.length===0,'async display observer produced an unhandled rejection');check(updates===1&&f.service.state.statusRevision===2,'async observer prevented native display delivery');check(f.calls.length===1&&f.timers.size===0,'async display failure scheduled business work');bad();good();
 }finally{stop();process.removeListener('unhandledRejection',onRejected);}
});
await run('observer-failure-keeps-recheck-setting-repair',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();await admitFixture(f);const beforeCalls=f.calls.length;let updates=0;f.service.subscribe(()=>{throw Error('owned recheck observer');});f.service.subscribe(()=>updates++);
 const waiting=f.service.recheck();f.pending.shift()!.resolve({statusRevision:2,enabled:false,pauseReason:'capacity-unavailable',cpuThreshold:15});await waiting;await tick();
 check(f.calls.length===beforeCalls+2&&f.calls.at(-1).args.enabled===true,'observer failure prevented real native setting mismatch repair');check(updates===1&&f.service.state.statusRevision===2,'recheck native truth failed to reach the sibling');
 f.pending.shift()!.resolve({statusRevision:3,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();check(f.service.state.statusRevision===3&&updates===2,'successful repair receipt lost its current display');check(f.calls.length===beforeCalls+2&&f.timers.size===0,'successful native write was mistaken for transport failure');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});
await run('observer-failure-keeps-configuration-retry-and-success',async()=>{
 const f=fixture(),rejections:unknown[]=[];const onRejected=(error:unknown)=>rejections.push(error);process.on('unhandledRejection',onRejected);let updates=0;f.service.subscribe(()=>{throw Error('owned configuration observer');});f.service.subscribe(()=>updates++);const stop=f.service.start();
 try{f.pending.shift()!.reject(Error('owned actual IPC failure'));await tick();check(f.timers.size===1,'display observer blocked the original failed-IPC retry');check(f.service.state.reason==='host-unavailable'&&updates===1,'real transport failure did not reach sibling display');
 const timer=[...f.timers][0];f.timers.delete(timer[0]);timer[1]();check(f.calls.length===2,'original recovery timer did not submit the pending configuration');f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();await tick();
 check(f.service.state.statusRevision===1&&updates===2&&f.timers.size===0,'successful retry did not settle cleanly after observer failure');f.service.sync();await tick();check(f.calls.length===2,'successful configuration was retried because an observer failed');check(rejections.length===0,'configuration observer rejection escaped the coordinator');
 }finally{stop();process.removeListener('unhandledRejection',onRejected);for(const p of f.pending)p.resolve({});}
});
await run('observer-failure-keeps-wake-truth-and-sleep-fence',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();await admitFixture(f,20);let updates=0;f.service.subscribe(()=>{throw Error('owned wake observer');});f.service.subscribe(()=>updates++);
 const oldRead=f.service.refresh(),old=f.pending.shift()!;f.bus.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:21}}));f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:21}}));nativePower(f.bus,'ready',21);const fresh=f.pending.shift()!;
 fresh.resolve({statusRevision:3,enabled:true,resourcePauseReason:'on-battery'});await tick();check(updates===1&&f.service.state.statusRevision===3&&f.service.state.resourcePauseReason==='on-battery','observer failure blocked current wake truth');
 old.resolve({statusRevision:2,enabled:false});await oldRead;check(updates===1&&f.service.state.statusRevision===3,'old pre-sleep receipt escaped generation fencing');
 const calls=f.calls.length;f.bus.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:21}}));await tick();check(f.calls.length===calls&&f.timers.size===0,'duplicate Ready caused a read or idle timer after observer failure');
 stop();f.bus.dispatchEvent(new CustomEvent('ipc:gameMedia.resume-ready',{detail:{generation:22}}));await tick();check(f.calls.length===calls,'disposed observer lifecycle restarted status work');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});
await run('observer-disposal-removes-not-yet-called-subscriber',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();let removed=0,survivor=0,offLater=()=>{};f.service.subscribe(()=>{offLater();throw Error('owned detach observer');});offLater=f.service.subscribe(()=>removed++);f.service.subscribe(()=>survivor++);
 let error:unknown;try{f.emit({statusRevision:2,enabled:true});}catch(e){error=e;}check(!error,'detaching observer escaped dispatch');check(removed===0&&survivor===1,'display dispatch lost live-set disposal semantics');check(f.calls.length===1&&f.timers.size===0,'disposal scheduled business work');
 }finally{stop();}
});
for(const command of ['requestNow','cancelManual'] as const)await run(`observer-failure-keeps-current-${command}-receipt`,async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();await admitFixture(f);const beforeCalls=f.calls.length;let updates=0;f.service.subscribe(()=>{throw Error('owned manual observer');});f.service.subscribe(()=>updates++);
 const waiting=f.service[command](),value={statusRevision:2,enabled:true,manualState:command==='requestNow'?'requested':'cancelled'};f.pending.shift()!.resolve(value);let returned:unknown,error:unknown;try{returned=await waiting;}catch(e){error=e;}
 check(!error&&returned===value,'successful native manual receipt was converted to observer rejection');check(f.service.state===value&&updates===1,'manual native truth did not reach the remaining display subscriber');check(f.calls.length===beforeCalls+1&&f.timers.size===0,'manual receipt observation replayed the command');
 }finally{stop();}
});

await run('pending-native-ready-fences-status-and-heals-same-generation',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();await admitFixture(f,51);
 const before=f.calls.length,old=f.service.refresh(),receipt=f.pending.shift()!;nativePower(f.bus,'ready',51,{notificationPending:true});receipt.resolve({statusRevision:2,enabled:false});await old;
 check(f.service.state.statusRevision===1,'pending Native Ready admitted a pre-ingress status return');const calls=f.calls.length;await f.service.refresh();await f.service.recheck();f.bus.dispatchEvent(new Event('focus'));await tick();check(f.calls.length===calls&&f.timers.size===0,'pending Native Ready issued a status or repair request');
 nativePower(f.bus,'ready',51,{notificationPending:false,inputReady:false});check(f.calls.length===before+2&&f.calls.at(-1).command==='gameMedia.downloads','same-generation admitted edge failed to start exactly one fresh read');f.pending.shift()!.resolve({statusRevision:3,enabled:true,resourcePauseReason:'on-battery'});await tick();check(f.service.state.statusRevision===3&&f.service.state.resourcePauseReason==='on-battery','current read failed to publish true wake policy');
 const healthy=f.calls.length;nativePower(f.bus,'ready',51,{inputReady:false});f.bus.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:51}}));await tick();check(f.calls.length===healthy&&f.timers.size===0,'duplicate or diagnostic Ready repeated the media read');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});
await run('closed-hardware-ready-keeps-startup-recovery-dormant',async()=>{
 const f=fixture(),stop=f.service.start();try{const protection={statusRevision:1,enabled:true,resourcePauseReason:'startup-protection',startupProtectionUntil:Date.now()+30000};f.pending.shift()!.resolve(protection);await tick();const calls=f.calls.length;
 nativePower(f.bus,'ready',52,{hardwareWritesAllowed:false});await tick();check(f.calls.length===calls&&f.timers.size===0,'closed Native Ready started a status read or startup timer');nativePower(f.bus,'ready',52,{hardwareWritesAllowed:false});await tick();check(f.calls.length===calls,'duplicate closed Native Ready repeated work');
 nativePower(f.bus,'ready',52,{inputReady:false});check(f.calls.length===calls+1,'opening the real native gate did not recover the same generation');f.pending.shift()!.resolve(protection);await tick();check(f.timers.size===1,'admitted startup protection lost its original bounded recovery timer');stop();check(f.timers.size===0,'stopped admission owner retained a startup timer');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});
await run('pending-native-ready-fences-configuration-receipt',async()=>{
 const f=fixture(),stop=f.service.start();try{const old=f.pending.shift()!;nativePower(f.bus,'ready',53);f.pending.shift()!.resolve({statusRevision:1,enabled:false,pauseReason:'capacity-unavailable',cpuThreshold:15});await tick();nativePower(f.bus,'ready',53,{notificationPending:true});old.resolve({statusRevision:2,enabled:true});await tick();
 check(f.service.state.statusRevision===1&&f.service.state.enabled===false,'pre-ingress configuration receipt crossed the pending Native fence');check(f.calls.length===2&&f.timers.size===0,'retired configuration receipt replayed work or scheduled a retry');nativePower(f.bus,'ready',53);check(f.calls.length===3,'fresh admitted edge remained blocked by the old write');f.pending.shift()!.resolve({statusRevision:3,enabled:true});await tick();check(f.service.state.statusRevision===3&&f.service.state.enabled===true,'fresh Native truth did not replace the retained pending state');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});
await run('stale-native-ready-cannot-reopen-current-pending-ingress',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();await admitFixture(f,54);const calls=f.calls.length;nativePower(f.bus,'ready',54,{notificationPending:true});nativePower(f.bus,'ready',54,{notificationPending:false,observedNotificationSequence:1});f.bus.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:54}}));await tick();await f.service.refresh();check(f.calls.length===calls&&f.timers.size===0,'out-of-order Native or legacy Ready reopened pending media observation');
 nativePower(f.bus,'ready',54,{notificationPending:false});check(f.calls.length===calls+1,'new current Native Ready failed to heal after stale delivery');f.pending.shift()!.resolve({statusRevision:2,enabled:true});await tick();check(f.service.state.statusRevision===2,'current read was lost after out-of-order notification');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});
await run('diagnostic-and-input-metadata-keep-current-media-read',async()=>{
 const f=fixture(),stop=f.service.start();try{f.pending.shift()!.resolve({statusRevision:1,enabled:true});await tick();await admitFixture(f,55);const waiting=f.service.refresh(),receipt=f.pending.shift()!,calls=f.calls.length;let updates=0;f.service.subscribe(()=>updates++);
 nativePower(f.bus,'ready',55,{inputReady:false});nativePower(f.bus,'ready',55,{inputReady:false});receipt.resolve({statusRevision:2,enabled:true});await waiting;check(f.calls.length===calls&&updates===1&&f.service.state.statusRevision===2,'metadata-only or input-only change cancelled or duplicated healthy media observation');check(f.timers.size===0,'healthy media admission created a resident polling timer');
 }finally{stop();for(const p of f.pending)p.resolve({});}
});

const selected:string[]=[];for(let i=2;i<process.argv.length;i++){assert.equal(process.argv[i],'--case','unknown media-recovery option');const name=process.argv[++i];assert.ok(name&&name.trim(),'empty media-recovery selection');assert.ok(!selected.includes(name),'duplicate media-recovery selection');selected.push(name);}
const names=scenarios.map(x=>x.name);assert.equal(new Set(names).size,names.length,'duplicate media-recovery scenario');for(const name of selected)assert.ok(names.includes(name),'unknown media-recovery case: '+name);
for(const {name,fn} of scenarios){if(selected.length&&!selected.includes(name))continue;try{await fn();reports.push({name,ok:true});}catch(error){reports.push({name,ok:false,error:String(error instanceof Error?error.message:error)});}}
assert.equal(reports.length,selected.length||scenarios.length,'media-recovery selection incomplete');
const ok=reports.every(x=>x.ok);console.log(JSON.stringify({ok,checks,cases:reports,selectedCases:selected,scope:'production settings/progress/refresh coordinators with deterministic IPC and lifecycle returns'}));if(!ok)process.exitCode=1;
