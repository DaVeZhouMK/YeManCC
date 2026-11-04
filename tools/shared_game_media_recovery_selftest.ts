import {strict as assert} from 'node:assert';
import {GameMediaPreparationPolicy} from '../src/bridge/gameMediaPreparationPolicy';
import * as preparationPolicy from '../src/bridge/gameMediaPreparationPolicy';
import {readFileSync} from 'node:fs';
import {downloadDescription,MediaDownloads} from '../src/bridge/gameMediaDownloads';
let checks=0;const reports:any[]=[];const check=(value:any,label:string)=>{checks++;assert.ok(value,label)};const tick=()=>new Promise<void>(resolve=>setImmediate(resolve));
function fixture(){
 const bus=new EventTarget();Object.assign(globalThis,{window:bus});let enabled=true,queued=2,running=true,storage='',catalogRequests=0,checksNative=0,readCalls=0,hold=false,release:any,listener:any;let seq=0;const timers=new Map<number,{fn:()=>void;ms:number}>();const calls:any[]=[];
 let native:any={enabled:true,pauseReason:'capacity-unavailable',cpuThreshold:50,statusRevision:1,remaining:2,completed:0,phase:'paused',resourcePauseReason:''};
 const schedule=((fn:()=>void,ms:number)=>{timers.set(++seq,{fn,ms});return seq}) as any,cancel=((id:number)=>timers.delete(id)) as any;
 const service=new MediaDownloads({enabled:()=>enabled,ready:()=>true,threshold:()=>50,listen:fn=>{listener=fn;return()=>{}},schedule,cancel,invoke:(async(command:string,args:any)=>{calls.push({command,args});assert.equal(command,'gameMedia.background');if(args.enabled!==undefined){native={...native,...args,statusRevision:native.statusRevision+1};if(args.catalog)catalogRequests++;}if(args.recheck){checksNative++;if(hold)return new Promise(resolve=>release=()=>resolve({...native}));}return {...native};}) as any});
 const stop=service.start();const policy=new GameMediaPreparationPolicy({read:async()=>{readCalls++;return {blockedReason:storage}},apply:state=>service.policy(state.reason),queued:()=>queued,enabled:()=>enabled,recheck:async()=>{policy.game(running);await service.recheck()},schedule,cancel});policy.game(true);policy.request();
 const fire=async(ms:number)=>{const timer=[...timers].find(([,item])=>item.ms===ms);assert.ok(timer,'missing timer '+ms);timers.delete(timer[0]);timer[1].fn();await tick();};
 return {service,policy,timers,calls,fire,stop:()=>{policy.stop();stop()},native:()=>native,change:(value:any)=>{native={...native,...value,statusRevision:native.statusRevision+1};listener({...native})},setGame:(value:boolean)=>running=value,setEnabled:(value:boolean)=>{enabled=value;service.sync();policy.demand()},setQueued:(value:number)=>{queued=value;policy.demand()},setStorage:(value:string)=>storage=value,catalogs:()=>catalogRequests,rechecks:()=>checksNative,reads:()=>readCalls,hold:()=>hold=true,release:()=>release?.(),bus};
}
let currentCase="";const generationSamples:any[]=[];
async function run(name:string,fn:()=>Promise<void>){currentCase=name;try{await fn();reports.push({name,ok:true});}catch(error){reports.push({name,ok:false,error:String(error instanceof Error?error.message:error)});}}
await run('lost-game-exit-self-heals-in-twenty-seconds',async()=>{const f=fixture();await tick();await f.fire(250);check(f.native().pauseReason==='game-running','running game stays blocked');for(let n=0;n<100;n++)f.policy.demand();check(f.timers.size===1&&[...f.timers.values()][0].ms===20000,'one timer, exactly20sec');f.setGame(false);await f.fire(20000);await f.fire(250);check(f.native().pauseReason==='','fresh exit clears paused native policy');check(f.rechecks()===1,'one authenticated recheck wakes native owner');check(f.service.state.remaining===2,'queue survives exit recovery');check(f.timers.size===1,'retained tasks keep one next health check');f.stop();check(f.timers.size===0,'stop cancels health check');});
await run('cpu-return-and-missed-progress-use-no-catalog-scan',async()=>{const f=fixture();await tick();await f.fire(250);f.setGame(false);f.policy.game(false);await tick();const catalogs=f.catalogs();f.change({resourcePauseReason:'cpu-busy',phase:'paused'});await f.fire(20000);await f.fire(250);check(f.service.state.resourcePauseReason==='cpu-busy','recheck does not pretend CPU is safe');check(f.catalogs()===catalogs,'same settings recheck does not repeat catalog discovery');const last=f.calls.findLast(c=>c.args.recheck);check(last&&Object.keys(last.args).join(',')==='recheck','recheck does not replay targets/settings/manual run');f.change({resourcePauseReason:'',phase:'downloading'});await f.fire(20000);await f.fire(250);check(f.service.state.phase==='downloading','fresh native recovery is consumed');check(f.catalogs()===catalogs,'recovered download reuses original inventory');f.stop();});
await run('completion-removes-health-check-and-off-does-not-reenable',async()=>{const f=fixture();await tick();await f.fire(250);f.change({completed:2,remaining:0,phase:'complete'});f.setQueued(0);check(f.timers.size===0,'empty queue is dormant');f.setQueued(2);check(f.timers.size===1,'new retained work re-arms once');f.setEnabled(false);await tick();check(f.timers.size===0,'disabled automatic mode has no periodic recovery');await f.service.recheck();check(!f.calls.at(-1).args.recheck,'disabled state never issues health wake');check(f.native().enabled===false,'recovery cannot enable saved switch');f.stop();});
await run('slow-check-reconciles-setting-only-when-return-differs',async()=>{const f=fixture();await tick();await f.fire(250);const before=f.calls.length;const one=f.service.recheck(),two=f.service.recheck();check(one===two,'concurrent callers share one health request');await one;check(f.calls.length===before+1,'healthy check has one native call');f.change({enabled:false,cpuThreshold:15});await f.service.recheck();await tick();check(f.native().enabled===true&&f.native().cpuThreshold===50,'lost native settings repaired from current persisted intent');f.stop();});
await run('stop-and-new-lifecycle-fence-late-check',async()=>{const f=fixture();await tick();await f.fire(250);f.hold();const pending=f.service.recheck();const phase=f.service.state.phase;f.stop();f.release();await pending;check(f.service.state.phase===phase,'stopped owner ignores old response');check(f.timers.size===0,'stopped owner cannot restart timer');});
await run('suspend-cancels-waits-and-resume-revalidates',async()=>{const f=fixture();await tick();await f.fire(250);f.policy.suspend();check(f.timers.size===0,'suspend cancels recovery timer');check(f.native().pauseReason==='media-power-transition','suspend blocks admission immediately');f.policy.resume();await tick();await f.fire(250);check(f.timers.size===1,'resume checks fresh capacity and retains single timer');f.stop();});
await run('missed-publication-event-refreshes-from-completed-count',async()=>{const f=fixture();await tick();await f.fire(250);let assets=0;const stop=f.service.watchCompleted(()=>assets++);f.change({completed:1,remaining:1,phase:'downloading'});check(assets===1,'completion recovers asset consumer without a file event');f.change({bytes:100,totalBytes:200});check(assets===1,'byte progress never repeats asset scans');f.change({completed:2,remaining:0,phase:'complete'});check(assets===2,'final completion also reaches output');stop();f.change({completed:3});check(assets===2,'disposed completion observer ignores later returns');f.stop();});
await run('manual45of48-shows-backoff-reason-not-fake-preparation',async()=>{const text=downloadDescription({enabled:true,manualUntil:Date.now()+600000,completed:45,total:48,remaining:3,workerPid:0,activeCount:0,phase:'deferred',pendingReason:'media-size-invalid',nextDueAt:Date.now()+3600000});check(text.includes('等待重试')&&text.includes('单文件限制')&&!text.includes('正在准备'),'retained terminal backoff is visible');check(downloadDescription({enabled:true,completed:48,total:48,remaining:0,unavailable:2,phase:'completed'}).includes('2 个已跳过'),'unsupported items remain visible as skipped');});

// Replay the shipping event bindings. A direct policy.resume() would miss the
// S0 bug: this path returns Ready without emitting the normal power.resumed.
function wireShippingPower(policy:GameMediaPreparationPolicy){
 const source=readFileSync('src/bridge/sharedGameMedia.ts','utf8');
 const bindings=['suspendPolicy','resumePolicy'].map(name=>{
  const statement=source.split('\n').find(line=>line.trimStart().startsWith('const '+name+'='));
  assert.ok(statement,'missing shipping '+name+' binding');return statement;
 }).join('\n');
 const listeners=new Map<string,Set<(event:any)=>void>>(),captured:Array<()=>void>=[];
 const on=(name:string,fn:(event:any)=>void)=>{const values=listeners.get(name)||new Set();values.add(fn);listeners.set(name,values);return()=>values.delete(fn);};
 const stop=new Function('policy','on','subscribeGameMediaResume',bindings+'\nreturn ()=>{suspendPolicy();resumePolicy();};')(policy,on,(preparationPolicy as any).subscribeGameMediaResume);
 return {stop,emit:(name:string,event:any)=>{for(const fn of listeners.get(name)||[]){captured.push(()=>fn(event));fn(event);}},late:()=>captured.forEach(fn=>fn()),count:()=>[...listeners.values()].reduce((n,set)=>n+set.size,0)};
}
await run('S0-Ready-reaches-shipping-media-binding-without-power-resumed',async()=>{
 const f=fixture(),events=wireShippingPower(f.policy);
 try{await tick();await f.fire(250);events.emit('power.suspending',{generation:2});
  check(f.native().pauseReason==='media-power-transition'&&f.timers.size===0,'suspend cancels timers and retains the native pause');
  const receipt=process.env.YMCC_MEDIA_READY_NATIVE_RECEIPT?JSON.parse(readFileSync(process.env.YMCC_MEDIA_READY_NATIVE_RECEIPT,'utf8')):null;
  const nativeReady=receipt?.nativeReady||{event:'gameMedia.resume-ready',data:{generation:2,source:'sleep-intent-canceled'}};
  assert.equal(nativeReady.event,'gameMedia.resume-ready');assert.equal(nativeReady.data.generation,2);
  events.emit(nativeReady.event,nativeReady.data);
  check([...f.timers.values()].some(t=>t.ms===250),'S0 Ready must revalidate capacity without waiting for normal power.resumed');
  await f.fire(250);check(f.native().pauseReason==='game-running','S0 Ready clears only the sleep latch, keeping the actual game restriction');
  const reads=f.reads();events.emit('power.resumed',{generation:2});events.emit('gameMedia.resume-ready',{generation:2});await tick();
  check(f.reads()===reads&&![...f.timers.values()].some(t=>t.ms===250),'duplicate Ready signals do not reread or reset healthy media');
  events.emit('power.suspending',{generation:3});events.emit('gameMedia.resume-ready',{generation:2});events.emit('power.resumed',{generation:2});
  check(f.native().pauseReason==='media-power-transition'&&f.timers.size===0,'old generation cannot release a newer sleep gate');
  for(const generation of [0,-1,1.5,NaN,undefined])events.emit('gameMedia.resume-ready',{generation});
  check(f.timers.size===0,'invalid Ready cannot resume media');
  events.emit('gameMedia.resume-ready',{generation:3});await f.fire(250);check(f.native().pauseReason==='game-running','current S0 generation can recover again');
 }finally{events.stop();f.stop();}
 check(events.count()===0,'dispose releases every power subscription');events.late();check(f.timers.size===0,'late callbacks cannot reactivate stopped recovery');
});
await run('S0-Ready-keeps-explicit-off-and-storage-veto',async()=>{
 const f=fixture(),events=wireShippingPower(f.policy);
 try{await tick();await f.fire(250);f.setEnabled(false);f.setGame(false);f.policy.game(false);f.setStorage('media-disk-space-low');
  events.emit('power.suspending',{generation:4});events.emit('gameMedia.resume-ready',{generation:4});await f.fire(250);await tick();
  check(f.native().pauseReason==='media-disk-space-low','S0 recovery reads fresh disk policy before opening storage');
  check(f.native().enabled===false&&!f.calls.some(c=>c.args.recheck),'Ready does not turn OFF into ON or launch background work');
  check(f.timers.size===0,'disabled downloads stay dormant after Ready');
 }finally{events.stop();f.stop();}
});


// Deferred ports cover capacity/read and health handoff on the existing policy.
function generationFixture(){
 const name=currentCase;
 let queued=2,enabled=true,seq=0;const timers=new Map<number,{fn:()=>void;ms:number}>();
 const reads:Array<{resolve:(x:any)=>void;reject:(e:Error)=>void}>=[],checksPending:Array<{resolve:()=>void;reject:(e:Error)=>void}>=[],states:any[]=[];
 const policy=new GameMediaPreparationPolicy({read:()=>new Promise((resolve,reject)=>reads.push({resolve,reject})),recheck:()=>new Promise<void>((resolve,reject)=>checksPending.push({resolve,reject})),apply:s=>states.push({...s}),queued:()=>queued,enabled:()=>enabled,schedule:((fn:()=>void,ms:number)=>{timers.set(++seq,{fn,ms});return seq;}) as any,cancel:((id:number)=>timers.delete(id)) as any});
 const power=wireShippingPower(policy);
 const fire=async(ms:number)=>{const timer=[...timers].find(([,t])=>t.ms===ms);assert.ok(timer,'missing generation timer '+ms);timers.delete(timer[0]);timer[1].fn();await tick();};
 return{policy,power,timers,reads,checksPending,states,fire,last:()=>states.at(-1),count:(ms:number)=>[...timers.values()].filter(t=>t.ms===ms).length,setQueued:(x:number)=>{queued=x;policy.demand();},setEnabled:(x:boolean)=>{enabled=x;policy.demand();},stop:()=>{generationSamples.push({name,capacityReads:reads.length,healthCalls:checksPending.length,scheduledTimers:[...timers.values()].map(t=>t.ms),publishedStates:states.length,lastReason:states.at(-1)?.reason});power.stop();policy.stop();for(const p of reads)p.resolve({});for(const p of checksPending)p.resolve();}};
}
await run('cancelled-capacity-callback-cannot-read-during-suspension',async()=>{
 const f=generationFixture();try{f.policy.request();const callback=[...f.timers.values()][0].fn;f.power.emit('power.suspending',{generation:31});const applied=f.states.length;callback();await tick();check(f.reads.length===0,'already-dispatched cancelled capacity callback started an IPC while suspended');check(f.timers.size===0&&f.states.length===applied,'cancelled callback changed the pause or left work scheduled');}finally{f.stop();}
});
await run('current-capacity-bursts-coalesce-and-retain-one-followup',async()=>{
 const f=generationFixture();try{for(let n=0;n<100;++n)f.policy.request();check(f.count(250)===1,'current request burst created multiple capacity timers');await f.fire(250);for(let n=0;n<100;++n)f.policy.request();check(f.reads.length===1&&f.count(250)===0,'active current read was duplicated');f.reads[0].resolve({});await tick();check(f.count(250)===1&&f.count(20000)===1,'current read did not keep exactly one event followup/slow recovery');await f.fire(250);f.reads[1].resolve({blockedReason:'media-disk-space-low'});await tick();check(f.last().reason==='media-disk-space-low'&&f.count(250)===0&&f.count(20000)===1,'coalesced followup lost fresh storage veto or repeated itself');}finally{f.stop();}
});
for(const outcome of ['success','failure'])await run(`resumed-capacity-read-is-independent-of-old-${outcome}`,async()=>{
 const f=generationFixture();try{f.policy.request();await f.fire(250);f.power.emit('power.suspending',{generation:32});f.power.emit('gameMedia.resume-ready',{generation:32});check(f.count(250)===1,'old pre-suspend capacity read blocked fresh resume admission');await f.fire(250);check(f.reads.length===2,'fresh resume did not start its own capacity read');f.reads[1].resolve({blockedReason:'media-disk-space-low'});await tick();const applied=f.states.length;
 if(outcome==='success')f.reads[0].resolve({});else f.reads[0].reject(Error('old transport failed'));await tick();check(f.last().reason==='media-disk-space-low'&&f.states.length===applied,'old read settlement republished or relaxed current capacity state');check(f.count(250)===0&&f.count(20000)===1,'old read settlement restarted a superseded followup');}finally{f.stop();}
});
await run('old-read-settlement-does-not-release-current-read-or-event-burst',async()=>{
 const f=generationFixture();try{f.policy.request();await f.fire(250);f.policy.request();f.power.emit('power.suspending',{generation:33});f.power.emit('gameMedia.resume-ready',{generation:33});check(f.count(250)===1,'old pending read/event blocked the next generation');await f.fire(250);for(let n=0;n<100;++n)f.policy.request();const applied=f.states.length;f.reads[0].resolve({});await tick();check(f.reads.length===2&&f.count(250)===0&&f.states.length===applied,'old finally released a newer active read or replayed its events');f.reads[1].resolve({});await tick();check(f.count(250)===1,'current-generation event burst was dropped');await f.fire(250);f.reads[2].resolve({});await tick();check(f.reads.length===3&&f.count(250)===0&&f.count(20000)===1,'event burst created more than one followup');}finally{f.stop();}
});
for(const outcome of ['success','failure'])await run(`resumed-health-check-is-independent-of-old-${outcome}`,async()=>{
 const f=generationFixture();try{f.policy.request();await f.fire(250);f.reads[0].resolve({});await tick();await f.fire(20000);check(f.checksPending.length===1,'first health check started');f.power.emit('power.suspending',{generation:34});f.power.emit('gameMedia.resume-ready',{generation:34});await f.fire(250);f.reads[1].resolve({});await tick();check(f.count(20000)===1,'old health check blocked current-generation recovery timer');await f.fire(20000);check(f.checksPending.length===2,'current-generation health check did not start independently');
 if(outcome==='success')f.checksPending[0].resolve();else f.checksPending[0].reject(Error('old health failed'));await tick();f.policy.demand();check(f.count(250)===0&&f.count(20000)===0,'old health finally released or rescheduled the newer in-flight check');f.checksPending[1].resolve();await tick();check(f.count(250)===1,'current health completion did not request capacity truth');await f.fire(250);f.reads[2].resolve({blockedReason:'media-storage-unavailable'});await tick();check(f.last().reason==='media-storage-unavailable'&&f.count(20000)===1,'current recovery lost the storage hard gate');}finally{f.stop();}
});
await run('fresh-empty-off-state-stays-dormant-after-late-old-read',async()=>{
 const f=generationFixture();try{f.policy.request();await f.fire(250);f.power.emit('power.suspending',{generation:35});f.setEnabled(false);f.setQueued(0);f.power.emit('gameMedia.resume-ready',{generation:35});check(f.count(250)===1,'disabled output still needs one fresh policy read after Ready');await f.fire(250);f.reads[1].resolve({});await tick();const applied=f.states.length;f.reads[0].reject(Error('late old check'));await tick();check(f.timers.size===0&&f.checksPending.length===0&&f.states.length===applied,'old read revived timers/health while OFF and empty');}finally{f.stop();}
});
await run('cancelled-capacity-callback-cannot-steal-fresh-resume-timer',async()=>{
 const f=generationFixture();try{f.policy.request();const old=[...f.timers.values()][0].fn;f.power.emit('power.suspending',{generation:36});f.power.emit('gameMedia.resume-ready',{generation:36});old();await tick();check(f.reads.length===0&&f.count(250)===1,'cancelled old callback stole the fresh resume timer or performed an extra read');await f.fire(250);f.reads[0].resolve({blockedReason:'media-cache-budget-full'});await tick();check(f.last().reason==='media-cache-budget-full'&&f.reads.length===1,'fresh callback lost current capacity protection');}finally{f.stop();}
});
await run('cancelled-health-callback-cannot-steal-an-OFF-ON-replacement',async()=>{
 const f=generationFixture();try{f.policy.request();await f.fire(250);f.reads[0].resolve({});await tick();const old=[...f.timers.values()].find(t=>t.ms===20000)!.fn;f.setEnabled(false);f.setEnabled(true);check(f.count(20000)===1,'ON should retain one health timer');old();await tick();check(f.checksPending.length===0&&f.count(20000)===1,'cancelled health callback started work or stole its replacement');await f.fire(20000);check(f.checksPending.length===1,'current health timer did not run');f.checksPending[0].resolve();await tick();check(f.count(250)===1,'current health timer lost its capacity followup');}finally{f.stop();}
});
await run('stopped-policy-rejects-late-capacity-and-callbacks',async()=>{
 const f=generationFixture();try{f.policy.request();const callback=[...f.timers.values()][0].fn;await f.fire(250);const applied=f.states.length;f.policy.stop();callback();f.reads[0].reject(Error('settled after stop'));await tick();f.policy.demand();f.policy.request();check(f.states.length===applied&&f.timers.size===0&&f.reads.length===1&&f.checksPending.length===0,'stopped policy produced state/reads/timers from a late callback');}finally{f.stop();}
});
const ok=reports.every(row=>row.ok);if(!ok)process.exitCode=1;
console.log(JSON.stringify({ok,checks,cases:reports,generationSamples,nativeReadyReceipt:process.env.YMCC_MEDIA_READY_NATIVE_RECEIPT||null,scope:'production policy and download coordinator with shipping power bindings; virtual20sec timers and inert authenticated IPC, no installed writes'}));
