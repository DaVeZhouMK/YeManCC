import {strict as assert} from 'node:assert';
import {WallpaperPlayers} from '../decky-plugin/src/wallpaperPlayers';
import {WallpaperForeground} from '../decky-plugin/src/wallpaperForeground';
import {randomGameMediaRange,applyGameMediaRange,effectiveGameMediaRange,DEFAULT_GAME_MEDIA_PLAYBACK} from '../src/bridge/gameMediaPlayback';
let checks=0;const check=(ok:any,label:string)=>{checks++;assert.ok(ok,label)};
const a=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>0),b=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>.5),c=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>1);
check(a.start===8&&a.end===120&&a.initialStart===8,'whole video after intro is the playable loop');check(b.initialStart===63.5,'random start spans whole video rather than the first30sec');check(c.initialStart===119,'latest point is near actual end, not duration minus30sec');
for(const duration of [5,20,38,39,60,120,327.233333])for(const start of [0,8,15])for(const length of [30,60])for(const seed of [0,.1,.5,.9,1]){const range=randomGameMediaRange(duration,{start,end:start+length},()=>seed);check(range.initialStart>=range.start&&range.initialStart<range.end,'initial frame remains inside full playable media');check(range.end===duration,'segment setting never caps Steam playback');if(duration>start+1)check(range.start===start&&range.initialStart>=start,'intro is excluded both initially and on loop');}
check(randomGameMediaRange(0,DEFAULT_GAME_MEDIA_PLAYBACK).end===0,'unknown duration does not seek invalid media');check(randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>NaN).initialStart===8,'invalid random falls back after intro');
const fromStart={...DEFAULT_GAME_MEDIA_PLAYBACK,startMode:'from-start'} as const;
const beginning=randomGameMediaRange(120,fromStart,()=>{throw Error('from-start must not sample random');});
check(beginning.start===0&&beginning.initialStart===0&&beginning.end===120,'Steam from-start starts at zero and loops the whole clip');
const limited=effectiveGameMediaRange(120,fromStart);
check(limited.start===0&&limited.end===30,'YMCC retains the configured segment length from zero');
const explicitRandom=randomGameMediaRange(120,{...fromStart,startMode:'random'},()=>.5);
check(explicitRandom.start===b.start&&explicitRandom.end===b.end&&explicitRandom.initialStart===b.initialStart,'restoring random restores the saved intro and remains equivalent to legacy');
const chosen=randomGameMediaRange(120,DEFAULT_GAME_MEDIA_PLAYBACK,()=>.5);const video:any={readyState:2,duration:120,currentTime:120};applyGameMediaRange(video,chosen);check(video.currentTime===8,'whole-video loop returns after intro');video.currentTime=110;applyGameMediaRange(video,chosen);check(video.currentTime===110,'video can play beyond first30sec without rewinding');
// Execute the production player with inert media nodes. The Edge regression
// separately exercises real decoding and the Surface foreground event chain.
let focused=true,allowed=true,created=0,plays=0;const nodes:any[]=[];
const doc:any={visibilityState:'visible',hasFocus:()=>focused,defaultView:null,createElement:()=>{
 ++created;const listeners=new Map<string,Array<()=>void>>(),attributes=new Map<string,string>();
 const node:any={ownerDocument:doc,style:{},dataset:{},readyState:2,seeking:false,duration:120,currentTime:0,paused:true,parentNode:null,
 setAttribute:(k:string,v:string)=>attributes.set(k,v),removeAttribute:(k:string)=>attributes.delete(k),getAttribute:(k:string)=>attributes.get(k),
 addEventListener:(key:string,fn:()=>void)=>{const list=listeners.get(key)||[];list.push(fn);listeners.set(key,list);},fire:(key:string)=>{for(const fn of listeners.get(key)||[])fn();},
 pause:()=>{node.paused=true;},load:()=>{},remove:()=>{node.parentNode=null;},play:()=>{++plays;node.paused=false;return Promise.resolve();}};
 Object.defineProperty(node,'src',{get:()=>attributes.get('src'),set:(value:string)=>attributes.set('src',value)});nodes.push(node);return node;
}};
const target:any={setAttribute(){},removeAttribute(){},getAttribute(){return null;}},container:any={parentElement:target,appendChild:(node:any)=>node.parentNode=container};
const frame:any={contextSeq:1,appId:'570',entry:{assetId:'fixture',revision:'1',bindingRevision:1,kind:'video',url:'http://fixture/video.mp4'}};
const pool=new WallpaperPlayers(doc,()=>{throw Error('background interruption is not an asset failure');},{allowed:()=>allowed});
focused=false;pool.show(container,frame);check(created===0&&plays===0,'unfocused page creates no video or source');
focused=true;doc.visibilityState='hidden';pool.show(container,frame);check(created===0,'hidden page creates no source');
doc.visibilityState='visible';allowed=false;pool.show(container,frame);check(created===0,'blur latch vetoes stale positive document focus');
allowed=true;pool.show(container,frame);check(created===1&&plays===0,'foreground selects one source without starting decoder before intro/random seek');const old=nodes.at(-1);
allowed=false;old.fire('loadedmetadata');check(old.paused&&!old.src&&old.parentNode===null,'late metadata releases source in background');
const count=plays;for(const event of ['loadeddata','canplay','seeked','timeupdate'])old.fire(event);check(plays===count,'late readiness cannot restart background video');
allowed=true;pool.show(container,{...frame,contextSeq:2});check(created===2&&plays===count,'foreground recovery creates one source and waits for metadata');const current=nodes.at(-1);current.fire('loadedmetadata');check(plays===count,'metadata/random seek does not start a discarded intro decoder');current.fire('canplay');check(plays===count+1,'ready selected seek starts playback exactly once');
focused=false;current.fire('timeupdate');check(current.paused&&!current.src,'real focus loss vetoes playback boundary');
focused=true;pool.show(container,{...frame,contextSeq:3});pool.stop();check(nodes.every(n=>!n.src&&n.paused),'stop releases all sources');
const finalPlays=plays;for(const node of nodes)for(const event of ['loadeddata','canplay','seeked'])node.fire(event);await Promise.resolve();check(plays===finalPlays,'disposed events and play completion cannot revive video');

// Rapid navigation reuses one delayed latest selection, never a preload pool.
let selectedClock=1000,selectionId=0;const selectionTimers=new Map<number,{run:()=>void;delay:number}>();
const selectionPool=new WallpaperPlayers(doc,()=>{throw Error('selection must not fail');},{allowed:()=>allowed,now:()=>selectedClock,schedule:((run:()=>void,delay:number)=>{selectionTimers.set(++selectionId,{run,delay});return selectionId;}) as any,cancel:((id:number)=>selectionTimers.delete(id)) as any});
const selectedFrame=(id:number)=>({...frame,contextSeq:id,entry:{...frame.entry,assetId:'choice-'+id,url:'http://fixture/'+id+'.mp4'}});
const selectedStart=created;selectionPool.show(container,selectedFrame(1));check(created===selectedStart+1&&selectionTimers.size===1&&[...selectionTimers.values()][0].delay===15000,'first selected card is immediate with one bounded progress deadline');
selectedClock+=35;selectionPool.show(container,selectedFrame(2));check(created===selectedStart+1&&selectionTimers.size===1&&nodes.at(-1).paused&&!nodes.at(-1).src,'rapid different card stops old source and delays starting a replacement');
const repeatedTimer=selectionId;selectionPool.show(container,{...selectedFrame(2),contextSeq:99});check(selectionId===repeatedTimer&&selectionTimers.size===1,'same pending asset updates its frame without rearming delay');
const obsoleteSelection=[...selectionTimers.values()][0];
for(let i=3;i<=20;i++){selectedClock+=35;selectionPool.show(container,selectedFrame(i));check(selectionTimers.size===1&&created===selectedStart+1,'intermediate navigation creates no source and only one pending timer');}
obsoleteSelection.run();check(selectionTimers.size===1&&created===selectedStart+1,'a superseded queued callback cannot start a later selection early');
const committed=[...selectionTimers.values()][0];check(committed.delay===90,'rapid latest selection settles in ninety milliseconds');selectionTimers.clear();selectedClock+=90;committed.run();check(created===selectedStart+2&&nodes.at(-1).src==='http://fixture/20.mp4','only final selection obtains a video source');
selectedClock+=200;selectionPool.show(container,selectedFrame(21));check(created===selectedStart+3&&selectionTimers.size===1&&[...selectionTimers.values()][0].delay===15000,'ordinary isolated card movement adds no settling delay and owns one progress deadline');
selectedClock+=35;selectionPool.show(container,selectedFrame(22));const cancelled=[...selectionTimers.values()][0];selectionPool.stop();check(selectionTimers.size===0&&nodes.every(n=>!n.src&&n.paused),'background/OFF stop cancels the pending selection and every source');const stoppedCreated=created;cancelled.run();check(created===stoppedCreated,'a cancelled timer callback cannot resurrect a stopped source');
selectionPool.show(container,selectedFrame(23));selectedClock+=35;selectionPool.show(container,selectedFrame(24));const hidden=[...selectionTimers.values()][0];selectionTimers.clear();allowed=false;hidden.run();check(nodes.every(n=>!n.src&&n.paused),'late selection rechecks foreground before assigning src');allowed=true;selectionPool.stop();


// Fault injection executes the shipping player; time is virtual and no installed
// Steam, asset server or decoder is touched. The browser consumer tests real H264.
const faultResults:Array<{name:string;ok:boolean;error?:string}>=[];
const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
// Identity/meta come from the TMNT incident and user-supplied real decode probe.
// These inert nodes do not rerun that probe or establish the original error cause.
const tmntIncident={key:'steam-1361510',pid:80708,executable:'TMNT.exe',assetId:'3b507d102803210fd2601475605a7275',bindingRevision:6,bytes:23996804,firstError:'UNKNOWN',metadata:{width:1280,height:720,duration:77},probePositions:[8,38.5,75],frame:{contextSeq:6,appId:'1361510',entry:{assetId:'3b507d102803210fd2601475605a7275',revision:'3b507d102803210fd2601475605a7275',bindingRevision:6,kind:'video',url:'http://127.0.0.1:13140/asset/3b507d102803210fd2601475605a7275.mp4?token='+'a'.repeat(64),playback:{start:8,end:38,fit:'cover'}}}};
type PlayMode='normal'|'pending'|'reject'|'abort'|'throw';
function faultFixture(mode:PlayMode='normal',selectedFrame:any=frame,metadata={duration:120,width:1920,height:1080},composition?:Pick<NonNullable<ConstructorParameters<typeof WallpaperPlayers>[2]>,'observeComposition'|'composited'|'presented'|'recovering'>,acceptFailure?:(value:typeof frame)=>boolean){
 let clock=1000,id=0,admit=true,focus=true,playCount=0,playMode=mode;const timers=new Map<number,{run:()=>void;due:number;delay:number}>(),media:any[]=[],failed:any[]=[],temporaryFailures:boolean[]=[];
 const doc:any={visibilityState:'visible',hasFocus:()=>focus,defaultView:null,createElement:()=>{
  const handlers=new Map<string,Array<()=>void>>(),attrs=new Map<string,string>();
  const node:any={ownerDocument:doc,style:{},dataset:{},duration:metadata.duration,videoWidth:metadata.width,videoHeight:metadata.height,currentTime:0,readyState:0,seeking:false,paused:true,parentNode:null,error:null,rejectPlay:null,
   setAttribute:(k:string,v:string)=>attrs.set(k,v),getAttribute:(k:string)=>attrs.get(k),removeAttribute:(k:string)=>attrs.delete(k),
   addEventListener:(k:string,fn:()=>void)=>{const a=handlers.get(k)||[];a.push(fn);handlers.set(k,a);},fire:(k:string)=>{for(const fn of handlers.get(k)||[])fn();},
   pause:()=>{node.paused=true;},load:()=>{},remove:()=>{node.parentNode=null;},play:()=>{playCount++;if(playMode==='pending')return new Promise((_,reject)=>node.rejectPlay=reject);if(playMode==='throw')throw Error('inert synchronous play refusal');if(playMode==='reject'||playMode==='abort'){const error=Error('inert play refusal');if(playMode==='abort')error.name='AbortError';return Promise.reject(error);}node.paused=false;return Promise.resolve();}};
  Object.defineProperty(node,'src',{get:()=>attrs.get('src'),set:(v:string)=>attrs.set('src',v)});media.push(node);return node;
 }};
 const attrs=new Map<string,string>(),target:any={setAttribute:(k:string,v:string)=>attrs.set(k,v),removeAttribute:(k:string)=>attrs.delete(k),getAttribute:(k:string)=>attrs.get(k)},container:any={parentElement:target,appendChild:(n:any)=>n.parentNode=container};
 const pool=new WallpaperPlayers(doc,(value,temporary)=>{if(acceptFailure&&!acceptFailure(value))return false;failed.push(value);temporaryFailures.push(temporary===true);},{...composition,allowed:()=>admit,now:()=>clock,schedule:((run:()=>void,delay:number)=>{timers.set(++id,{run,delay,due:clock+delay});return id;}) as any,cancel:((id:number)=>timers.delete(id)) as any});
 const advance=async(ms:number)=>{const end=clock+ms;let guard=0;for(;;){const due=[...timers].filter(([,t])=>t.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!due)break;assert.ok(++guard<100,'unbounded deadline loop');clock=due[1].due;timers.delete(due[0]);due[1].run();await flush();}clock=end;await flush();};
 const ready=()=>{const n=media.at(-1);n.readyState=2;n.fire('loadedmetadata');n.fire('canplay');return n;};
 return{pool,timers,media,failed,temporaryFailures,advance,ready,target,container,show:(seq=selectedFrame.contextSeq,key=selectedFrame.entry.assetId)=>pool.show(container,{...selectedFrame,contextSeq:seq,entry:{...selectedFrame.entry,assetId:key}}),plays:()=>playCount,created:()=>media.length,scheduled:()=>id,setPlayMode:(v:PlayMode)=>playMode=v,setAllowed:(v:boolean)=>admit=v,setFocus:(v:boolean)=>focus=v,doc,error:(code:number|null=null)=>{const n=media.at(-1);n.error=code===null?null:{code};n.fire('error');return n;}};
}
async function fault(name:string,run:()=>Promise<void>){try{await run();faultResults.push({name,ok:true});}catch(e){faultResults.push({name,ok:false,error:String(e instanceof Error?e.message:e)});}}
function compositionSignals(node:any,throws=false){
 let callback:VideoFrameRequestCallback|undefined,requests=0;const cancelled:number[]=[];
 Object.defineProperty(node,'isConnected',{get:()=>!!node.parentNode});
 node.requestVideoFrameCallback=(run:VideoFrameRequestCallback)=>{++requests;if(throws)throw Error('inert frame callback refusal');callback=run;return 0;};
 node.cancelVideoFrameCallback=(id:number)=>cancelled.push(id);
 return {requests:()=>requests,cancelled,deliver:(presentationTime=1025,presentedFrames=3)=>callback?.(1100,{presentationTime,presentedFrames} as VideoFrameCallbackMetadata)};
}
await fault('video-compositor-observation-is-one-shot-without-a-new-watch',async()=>{
 const reports:any[]=[];let allowed=0;
 const f=faultFixture('normal',frame,undefined,{observeComposition:()=>++allowed===1,composited:(shown,detail)=>reports.push({appId:shown.appId,seq:shown.contextSeq,...detail})});try{
  f.show();const s=compositionSignals(f.media[0]);const n=f.ready();await flush();
  check(allowed===1&&s.requests()===1&&reports.length===0&&!n.paused,'reveal is not composition and never waits for the callback to play');
  const timer=[...f.timers.keys()][0];for(let i=0;i<200;i++){n.fire('canplay');n.fire('timeupdate');f.show();}
  check(allowed===1&&s.requests()===1&&f.timers.size===1&&f.timers.has(timer),'ready/event/render storm adds neither registrations nor watches');
  s.deliver();s.deliver(1050,4);
  check(reports.length===1&&reports[0].sinceRevealMs===25&&reports[0].presentedFrames===3&&reports[0].appId===frame.appId&&reports[0].seq===frame.contextSeq,'only the first submission for the captured selection is logged');
  f.pool.stop();check(s.cancelled.length===0&&f.timers.size===0,'delivered observer is not pending after shutdown');
  f.show();const exhausted=compositionSignals(f.media.at(-1));check(!f.ready().paused&&exhausted.requests()===0,'observation budget exhausted still allows playback');
 }finally{f.pool.stop();}
});
await fault('video-compositor-callback-cannot-escape-owner-or-off',async()=>{
 for(const edge of ['off','focus','context','replacement','recovery','stale-submission']){
  let reports=0;const f=faultFixture('normal',frame,undefined,{observeComposition:()=>true,composited:()=>{++reports;}});try{
   f.show();const s=compositionSignals(f.media[0]);const n=f.ready();
   if(edge==='off')f.pool.stop();else if(edge==='focus')f.setFocus(false);else if(edge==='context')f.show(frame.contextSeq+1);else if(edge==='replacement')f.show(frame.contextSeq+1,'replacement');else if(edge==='recovery')f.error(2);
   s.deliver(edge==='stale-submission'?999:1025);
   check(reports===0,'late or pre-reveal frame must not log: '+edge);
   if(edge==='off'||edge==='replacement'||edge==='recovery')check(s.cancelled.length===1&&s.cancelled[0]===0&&!n.src&&n.paused,'pending id (including zero) is cancelled with decoder release: '+edge);
  }finally{f.pool.stop();}
 }
});
await fault('video-compositor-observation-refusal-never-changes-playback',async()=>{
 for(const edge of ['unsupported','missing-cancel','registration-throws','sink-throws']){
  let allowed=0;const f=faultFixture('normal',frame,undefined,{observeComposition:()=>{++allowed;return true;},composited:()=>{throw Error('inert sink refusal');}});try{
   f.show();const n=f.media[0];const s=edge==='unsupported'?null:compositionSignals(n,edge==='registration-throws');if(edge==='missing-cancel')delete n.cancelVideoFrameCallback;
   f.ready();s?.deliver();await flush();
   check(!n.paused&&!!n.src&&f.failed.length===0&&f.timers.size===1,'optional diagnostic cannot change media outcome: '+edge);
   check(allowed===Number(edge==='registration-throws'||edge==='sink-throws'),'unsupported browsers do not spend the installation budget: '+edge);
  }finally{f.pool.stop();}
 }
});
await fault('no-metadata-hang-reopens-locally-with-bounded-budget',async()=>{const f=faultFixture();try{
 f.show();const old=f.media[0];await f.advance(15000);check(!old.src&&old.paused,'load deadline must release a silent hung source');check(f.failed.length===0&&f.timers.size===1,'one local retry precedes asset failure');await f.advance(500);check(f.created()===2,'first load hang retries once locally');
 await f.advance(15000+2000+15000);check(f.created()===3&&f.failed.length===0&&f.timers.size===1,'persistent metadata hang must not declare a shared asset corrupt');const timer=[...f.timers.keys()][0];for(let i=0;i<100;i++)f.show(i+2);check(f.created()===3&&f.failed.length===0&&[...f.timers.keys()][0]===timer&&[...f.timers.values()][0].delay===60000,'unrelated rerenders cannot refill quick retries or rearm the one minute retry');
 }finally{f.pool.stop();}});
await fault('post-ready-silent-freeze-releases-decoder-and-recovers',async()=>{const f=faultFixture();try{f.show();const old=f.ready();await flush();old.currentTime=70;old.fire('timeupdate');await f.advance(15000);check(!old.src,'no-progress deadline must release a silently frozen decoder');await f.advance(500);const next=f.ready();check(Math.abs(next.currentTime-70)<.1,'local reopen preserves confirmed playback position');await flush();check(!next.paused&&!!next.src&&f.failed.length===0,'transient freeze recovers before invalidating cache');}finally{f.pool.stop();}
 const slow=faultFixture();try{
  slow.show();const pending=[...slow.timers.keys()][0];await slow.advance(14999);
  const n=slow.ready();check(slow.timers.size===1&&slow.timers.has(pending)&&slow.plays()===1,'first ready reuses the outstanding load watch');
  await slow.advance(1);check(!!n.src&&slow.timers.size===1&&slow.created()===1,'cold deadline reopens a video that just became ready');
  await slow.advance(14998);check(!!n.src&&slow.failed.length===0,'first ready gets the original playback progress window');
  n.fire('loadeddata');n.fire('canplay');n.fire('seeked');await slow.advance(1);
  check(!n.src&&slow.timers.size===1&&[...slow.timers.values()][0].delay===500&&slow.failed.length===0,'duplicate readiness cannot extend the actual no-progress deadline');
  await slow.advance(500);check(slow.created()===2,'confirmed post-ready freeze still reopens exactly once');
 }finally{slow.pool.stop();}
 const readyMarker='data-ymcc-wallpaper-ready';
 const outcomes=[];
 for(const boundary of ['detached-error','detached-stop','reparented-error'] as const){
  const f=faultFixture(),other=faultFixture();try{
   f.show();const n=f.ready();await flush();
   check(f.target.getAttribute(readyMarker)==='true','only a revealed video suppresses the native background');
   if(boundary==='reparented-error'){
    f.container.parentElement=other.target;f.show();
    check(other.target.getAttribute(readyMarker)==='true'&&f.created()===1&&f.plays()===1,'same selected decoder follows the reparented surface');
   }
   const originalBeforeRelease=f.target.getAttribute(readyMarker)==='true';
   f.container.parentElement=null;
   if(boundary==='detached-stop')f.pool.stop();else f.error(2);
   for(const event of ['loadeddata','seeked','canplay','error'])n.fire(event);
   await flush();
   check(!n.src&&n.paused&&f.failed.length===0&&f.timers.size===(boundary==='detached-stop'?0:1),'release keeps the original source and retry policy');
   outcomes.push({boundary,originalBeforeRelease,originalAfterRelease:f.target.getAttribute(readyMarker)==='true',replacementAfterRelease:other.target.getAttribute(readyMarker)==='true'});
  }finally{f.pool.stop();other.pool.stop();}
 }
 for(const row of outcomes){
  check(row.boundary!=='reparented-error'||!row.originalBeforeRelease,'moving the same video restores the former card background: '+JSON.stringify(outcomes));
  check(!row.originalAfterRelease&&!row.replacementAfterRelease,'release restores the exact previous background even after detachment: '+JSON.stringify(outcomes));
 }
});
// Win5 2026-11-17: RE2 shortcut 3219175393, cached 720p/51,378,432 bytes.
// The same context had ready records at 9,738 / 18,115 / 26,843ms, but
// no interruption reason. Inject browser stall signals, not a guessed cause.
await fault('win5-post-ready-waiting-confirms-stall-without-fifteen-second-wait',async()=>{const f=faultFixture();try{
 f.show();const old=f.ready();await flush();old.currentTime=70;old.fire('timeupdate');await f.advance(1000);old.fire('waiting');
 check(f.timers.size===1&&[...f.timers.values()][0].delay===2500,'waiting replaces the ordinary watch with one short confirmation');
 await f.advance(2499);check(!!old.src,'short buffering gets time to recover without tearing down video');await f.advance(1);check(!old.src&&old.paused&&f.failed.length===0,'confirmed no-progress stall releases only the local decoder');
 await f.advance(500);const next=f.ready();check(f.created()===2&&Math.abs(next.currentTime-70)<.1&&!next.paused,'local recovery retains position after a three-second event-to-reopen budget');
 }finally{f.pool.stop();}});
let mixedStallWatch:{pairs:number;timerReplacementsBeforeCheck:number;decoderRestartsBeforeCheck:number;localRetryMs:number}|undefined;
await fault('win5-stall-storm-keeps-original-confirmation-deadline',async()=>{const f=faultFixture();try{
 f.show();const n=f.ready();await flush();n.fire('stalled');const timer=[...f.timers.keys()][0];await f.advance(2000);for(let i=0;i<200;i++){n.fire('waiting');n.fire('stalled');}
 check(f.timers.size===1&&[...f.timers.keys()][0]===timer,'event storms neither multiply nor postpone the confirmation');await f.advance(500);check(!n.src&&f.failed.length===0,'original confirmation still expires under repeated stall signals');
 }finally{f.pool.stop();}
 const mixed=faultFixture();try{
  mixed.show();const n=mixed.ready();await flush();n.currentTime=40;n.fire('timeupdate');n.fire('stalled');
  const original=[...mixed.timers.keys()][0],before=mixed.scheduled(),pairs=200;
  for(let i=1;i<=pairs;i++){await mixed.advance(10);n.currentTime=40+i*.04;n.fire('timeupdate');n.fire(i%2?'waiting':'stalled');}
  const timerReplacementsBeforeCheck=mixed.scheduled()-before,decoderRestartsBeforeCheck=mixed.created()-1;
  check(timerReplacementsBeforeCheck===0&&mixed.timers.size===1&&mixed.timers.has(original),'progress/stall pairs replaced existing earlier timer '+timerReplacementsBeforeCheck+' times');
  check(decoderRestartsBeforeCheck===0&&!!n.src,'mixed event storm never replaces the active decoder');
  await mixed.advance(500);check(!!n.src&&mixed.timers.size===1,'an earlier wake rechecks the latest interruption without a premature reopen');
  await mixed.advance(1999);check(!!n.src,'latest real interruption still has one millisecond to recover');
  await mixed.advance(1);const localRetryMs=[...mixed.timers.values()][0]?.delay;
  check(!n.src&&mixed.failed.length===0&&mixed.timers.size===1&&localRetryMs===500,'actual no-progress window still releases and schedules the original retry');
  await mixed.advance(500);check(mixed.created()===2,'confirmed interruption reopens exactly once');
  mixedStallWatch={pairs,timerReplacementsBeforeCheck,decoderRestartsBeforeCheck,localRetryMs};
 }finally{mixed.pool.stop();}
 check(mixed.timers.size===0,'retiring the player leaves no timer');
});
await fault('win5-short-recovery-retains-the-existing-transient-retry-budget',async()=>{const f=faultFixture();try{
 f.show();for(let attempt=0;attempt<3;attempt++){const n=f.ready();await flush();n.fire('waiting');await f.advance(2500);check(!n.src&&f.failed.length===0&&f.timers.size===1,'warm stall releases one decoder without invalidating cached bytes');if(attempt<2)await f.advance([500,2000][attempt]);}
 const timer=[...f.timers.keys()][0];check([...f.timers.values()][0].delay===60000,'repeated short recoveries retain the existing slow cooldown');for(let i=0;i<50;i++)f.show(100+i);check([...f.timers.keys()][0]===timer,'same-asset renders cannot bypass or postpone exhausted retry budget');await f.advance(60000);check(f.created()===4&&f.failed.length===0,'one selected-foreground reopen occurs after the slow cooldown');
 }finally{f.pool.stop();}});
await fault('win5-buffering-with-real-progress-does-not-reopen',async()=>{for(const delivered of [true,false]){const f=faultFixture();try{
 f.show();const n=f.ready();await flush();n.currentTime=40;n.fire('timeupdate');n.fire('stalled');const timer=[...f.timers.keys()][0];await f.advance(1000);n.currentTime=41;if(delivered)n.fire('timeupdate');
 check([...f.timers.keys()][0]===timer,'progress records state without allocating another timer');await f.advance(1500);check(f.created()===1&&!!n.src&&!n.paused&&f.failed.length===0,'actual progress cancels suspicion even without a timeupdate event');check(f.timers.size===1,'healthy playback returns to one slow watch');
 }finally{f.pool.stop();}}});
await fault('win5-playing-signal-alone-cannot-conceal-a-real-freeze',async()=>{const f=faultFixture();try{
 f.show();const n=f.ready();await flush();n.fire('waiting');await f.advance(2000);n.fire('playing');await f.advance(500);check(!n.src&&f.failed.length===0,'only media position progress clears the short stall confirmation');
 }finally{f.pool.stop();}});
await fault('win5-late-stall-signal-never-postpones-an-earlier-watch',async()=>{const f=faultFixture();try{
 f.show();const n=f.ready();await flush();await f.advance(14000);n.fire('waiting');await f.advance(999);check(!!n.src,'earlier progress deadline still grants its remaining interval');await f.advance(1);check(!n.src&&f.timers.size===1&&[...f.timers.values()][0].delay===500,'late stall signal cannot extend the pre-existing recovery deadline');
 }finally{f.pool.stop();}});
await fault('win5-initial-metadata-and-random-seek-keep-cold-load-budget',async()=>{const f=faultFixture();try{
 f.show();const n=f.media[0],timer=[...f.timers.keys()][0];for(const event of ['waiting','stalled','pause'])n.fire(event);n.readyState=2;n.fire('loadedmetadata');n.seeking=true;for(const event of ['waiting','stalled','pause'])n.fire(event);
 check(f.timers.size===1&&[...f.timers.keys()][0]===timer&&[...f.timers.values()][0].delay===15000,'loading metadata and initial random seek never use the warm-playback deadline');
 }finally{f.pool.stop();}});
await fault('win5-short-stall-cancelled-by-background-and-new-selection',async()=>{for(const cause of ['hidden','off','blur','selection'] as const){const f=faultFixture();try{
 f.show();const n=f.ready();await flush();n.fire('waiting');const late=[...f.timers.values()][0].run;
 if(cause==='hidden'){f.doc.visibilityState='hidden';f.show();}else if(cause==='off'){f.setAllowed(false);f.pool.stop();}else if(cause==='blur'){f.setFocus(false);f.show();}else {f.show(9,'replacement');await f.advance(100);}
 const created=f.created(),timer=[...f.timers.keys()][0];late();await f.advance(3000);check(f.created()===created&&f.failed.length===0,'old short deadline cannot revive retired playback: '+cause);
 if(cause==='selection')check(f.timers.size===1&&[...f.timers.keys()][0]===timer,'retired stall cannot steal replacement progress watch');else check(f.timers.size===0&&f.media.every(n=>!n.src&&n.paused),'background/OFF owns no decoder or confirmation timer');
 }finally{f.pool.stop();}}});
await fault('never-settled-play-is-bounded-and-old-events-inert',async()=>{const f=faultFixture('pending');try{f.show();const old=f.ready();for(let i=0;i<200;i++)old.fire('canplay');check(f.plays()===1,'pending play never multiplies under ready storm');await f.advance(15000+500);check(f.created()===2&&!old.src,'never-settled play cannot pin a source forever');const count=f.created();for(const ev of ['error','loadedmetadata','canplay','timeupdate'])old.fire(ev);check(f.created()===count&&f.failed.length===0,'old events cannot corrupt replacement');}finally{f.pool.stop();}});
await fault('error-and-play-refusal-storms-share-one-retry',async()=>{for(const mode of ['normal','reject'] as const){const f=faultFixture(mode);try{f.show();const n=f.ready();if(mode==='normal')for(let i=0;i<200;i++)n.fire('error');await flush();check(!n.src&&f.timers.size===1,'explicit failure leaves one retry and zero live old source');await f.advance(500);check(f.created()===2,'repeated errors do not enqueue multiple reloads');}finally{f.pool.stop();}}});
await fault('healthy-progress-keeps-single-low-rate-watch-and-reclaims-budget',async()=>{const f=faultFixture();try{f.show();const n=f.ready();await flush();for(let i=0;i<8;i++){n.currentTime=20+i;n.fire('timeupdate');await f.advance(5000);check(f.created()===1&&f.failed.length===0,'real playback progress must not cause reload');check(f.timers.size<=1,'progress updates do not create per-frame timer fanout');}check(f.plays()===1,'healthy updates never repeat play');}finally{f.pool.stop();}check(f.timers.size===0,'stop removes the progress deadline');});
await fault('background-off-and-late-deadline-cannot-revive-media',async()=>{for(const cause of ['off','hidden','blur'] as const){const f=faultFixture();try{f.show();const n=f.ready();n.fire('error');check(f.timers.size===1,'failure has one owned retry before withdrawal');const late=[...f.timers.values()][0].run;const count=f.created();if(cause==='off'){f.setAllowed(false);f.pool.stop();}else if(cause==='hidden')f.doc.visibilityState='hidden';else f.setFocus(false);await f.advance(30000);late();await flush();check(f.created()===count&&f.failed.length===0&&f.timers.size===0,'withdrawn or cancelled retry stays silent');check(f.media.every(n=>!n.src&&n.paused),'withdrawal leaves no active decoder');}finally{f.pool.stop();}}});
await fault('new-selection-revokes-old-recovery-and-updates-error-context',async()=>{const f=faultFixture();try{f.show();const n=f.ready();n.fire('error');const late=[...f.timers.values()][0]?.run;await f.advance(200);f.show(2,'new-asset');late?.();check(f.media.at(-1).src&&f.failed.length===0,'old retry cannot replace a new selected source');f.show(99,'new-asset');f.error(3);await f.advance(500);f.error(3);await f.advance(2000);f.error(3);check(f.failed.length===1&&f.failed[0].contextSeq===99,'terminal decode failure correlates the latest authenticated context');}finally{f.pool.stop();}});

await fault('queued-progress-deadline-is-single-use-even-on-same-slot',async()=>{const f=faultFixture();try{f.show();const n=f.ready();await flush();const late=[...f.timers.values()][0].run;n.currentTime=30;n.fire('timeupdate');await f.advance(5000);n.currentTime=35;n.fire('timeupdate');await f.advance(10000);check(f.timers.size===1,'healthy deadline rearms exactly once');const timer=[...f.timers.keys()][0];late();check(f.timers.size===1&&[...f.timers.keys()][0]===timer,'already queued old callback cannot erase the new deadline');}finally{f.pool.stop();}});
await fault('real-sustained-playback-restores-only-the-local-retry-budget',async()=>{const f=faultFixture();try{
 f.show();f.ready().fire('error');await f.advance(500);f.ready().fire('error');await f.advance(2000);const n=f.ready();await flush();
 for(let i=0;i<8;i++){n.currentTime=20+i;n.fire('timeupdate');await f.advance(5000);}const count=f.created();n.fire('error');await f.advance(500);check(f.created()===count+1&&f.failed.length===0,'only sustained real progress replenishes local retry attempts');
 }finally{f.pool.stop();}});

// All transient cases share this original consumer, inert media and virtual clock.
const transientCauses=['no-metadata','silent-freeze','seek-hang','pending-play','reject-play','abort-play','throw-play','error-without-mediaerror','media-aborted-1','media-network-2','media-source-rejected-4'] as const;
for(const cause of transientCauses)await fault('tmnt-transient-'+cause+'-never-invalidates-shared-asset',async()=>{
 const mode:PlayMode=cause==='pending-play'?'pending':cause==='reject-play'?'reject':cause==='abort-play'?'abort':cause==='throw-play'?'throw':'normal';
 const f=faultFixture(mode,tmntIncident.frame,tmntIncident.metadata);try{
  f.show();for(let attempt=0;attempt<3;attempt++){
   const n=f.media.at(-1);if(cause!=='no-metadata'&&cause!=='media-source-rejected-4')f.ready();
   if(cause==='seek-hang')n.seeking=true;
   if(['no-metadata','silent-freeze','seek-hang','pending-play'].includes(cause))await f.advance(15000);
   else if(cause.startsWith('media-')||cause==='error-without-mediaerror')f.error(cause==='media-aborted-1'?1:cause==='media-network-2'?2:cause==='media-source-rejected-4'?4:null);
   await flush();check(f.failed.length===0,tmntIncident.key+' '+cause+' must never report shared corruption after attempt '+attempt);
   check(f.timers.size===1&&f.media.every(n=>!n.src&&n.paused),'each transient interruption releases its decoder and keeps exactly one retry');
   if(attempt<2){check([...f.timers.values()][0].delay===[500,2000][attempt],'two original quick retry delays remain unchanged');await f.advance([500,2000][attempt]);}
  }
  const timer=[...f.timers.keys()][0];check([...f.timers.values()][0].delay===60000,'exhausted transient budget owns one sixty second retry');
  for(let i=0;i<100;i++)f.show(100+i);check([...f.timers.keys()][0]===timer&&f.created()===3,'same game/context updates do not bypass or postpone cooldown');
  f.setPlayMode('normal');await f.advance(59999);check(f.created()===3&&f.timers.size===1,'no reopen before the sixty second deadline');
  await f.advance(1);check(f.created()===4&&f.timers.size===1,'exactly one reopen occurs at sixty seconds');const n=f.ready();await flush();
  check(n.videoWidth===1280&&n.videoHeight===720&&n.duration===77&&!n.paused&&!!n.src,'TMNT metadata fixture can resume playing after a transient interruption');
  for(const position of tmntIncident.probePositions){n.currentTime=position;n.fire('timeupdate');check(n.currentTime===position,'full-video loop retains the supplied TMNT probe position');}
  check(f.failed.length===0,'successful later playback leaves shared invalidation callback untouched');
 }finally{f.pool.stop();}check(f.timers.size===0&&f.media.every(n=>!n.src&&n.paused),'transient fixture stop owns no timer or decoder');
});
await fault('tmnt-source-rejected-backoff-cannot-resurrect-in-background',async()=>{
 const f=faultFixture('normal',tmntIncident.frame,tmntIncident.metadata);try{
  f.show();f.error(4);await f.advance(500);f.error(4);await f.advance(2000);f.error(4);
  check(f.failed.length===0&&f.timers.size===1&&[...f.timers.values()][0].delay===60000,'source rejection owns one local backoff, not shared invalidation');
  const late=[...f.timers.values()][0].run;f.setAllowed(false);f.pool.stop();late();await f.advance(60000);
  check(f.timers.size===0&&f.created()===3&&f.media.every(n=>!n.src&&n.paused),'hidden route cancels backoff and even a queued callback cannot reopen the video');
 }finally{f.pool.stop();}
});
await fault('generated-source-rejection-requests-original-after-bounded-reopens',async()=>{
 const generated={...frame,entry:{...frame.entry,assetId:'generated',reducedPlayback:true}};
 const f=faultFixture('normal',generated);try{
  f.show();f.error(4);await f.advance(500);f.error(4);await f.advance(2000);f.error(4);
  check(f.failed.length===1&&f.failed[0].entry.assetId==='generated'&&f.timers.size===0&&!f.temporaryFailures[0],'legacy host uses the existing failure report');
  check(f.media.every(n=>!n.src&&n.paused),'failed generated source retains no decoder');
  f.show(2,'original');await f.advance(200);const next=f.ready();
  check(!!next.src&&!next.paused&&f.media.filter(n=>!!n.src).length===1,'original can resume on the same selection with one decoder');
 }finally{f.pool.stop();}
});
await fault('cold-generated-read-failure-requests-original-without-reopening',async()=>{
 const generated={...frame,entry:{...frame.entry,assetId:'generated',reducedPlayback:true,recoverOriginal:true}};
 const original={...frame,entry:{...frame.entry,assetId:'original',url:'http://fixture/original.webm'}};
 for(const code of [2,4]){
  const f=faultFixture('normal',generated);try{
   f.show();const old=f.error(code);for(let i=0;i<40;i++)old.fire('error');
   check(f.failed.length===1&&f.temporaryFailures[0]&&f.failed[0].entry.assetId==='generated','cold read failure reports one temporary fallback immediately');
   check(f.created()===1&&!old.src&&old.paused&&f.timers.size===0,'cold read failure releases the decoder and schedules no reopen');
   f.show();await f.advance(2200);check(f.created()===1&&f.failed.length===1,'repeated temporary-failure context cannot restart the derivative');
   f.pool.show(f.container,{...original,contextSeq:generated.contextSeq});const next=f.ready();await flush();
   check(next.src===original.entry.url&&!next.paused&&f.media.filter(n=>!!n.src).length===1,'same context plays the original with one decoder');
  }finally{f.pool.stop();}
 }
 for(const edge of ['hidden','blur','off'] as const){
  const f=faultFixture('normal',generated);try{
   f.show();if(edge==='hidden')f.doc.visibilityState='hidden';else if(edge==='blur')f.setFocus(false);else f.setAllowed(false);
   const n=f.error(4);check(!n.src&&n.paused&&f.failed.length===0&&f.timers.size===0,'cold read failure cannot request the original after '+edge);
  }finally{f.pool.stop();}
 }
});
await fault('win5-baseball-slow-generated-start-requests-one-temporary-original',async()=>{
 const generated={...frame,appId:'4128400',entry:{...frame.entry,assetId:'generated',reducedPlayback:true,recoverOriginal:true}};
 const original={...frame,appId:generated.appId,entry:{...frame.entry,assetId:'original',url:'http://fixture/original.mp4'}};
 for(const phase of ['metadata','seek','frame'] as const){
  const recovery:any[]=[],f=faultFixture('normal',generated,undefined,{recovering:(_frame,detail)=>recovery.push(detail)});
  try{
   f.show();const old=f.media[0];
   if(phase!=='metadata'){await f.advance(140);old.seeking=true;old.readyState=1;old.fire('loadedmetadata');}
   if(phase==='frame'){await f.advance(140);old.seeking=false;old.fire('seeked');}
   await f.advance(899-(phase==='metadata'?0:phase==='seek'?140:280));
   check(!!old.src&&f.failed.length===0&&f.timers.size===1,'cold watchdog waits until the deadline: '+phase);
   await f.advance(1);
   check(!old.src&&old.paused&&f.created()===1&&f.timers.size===0&&f.temporaryFailures.length===1&&f.temporaryFailures[0],'slow derivative retires before the one temporary handoff: '+phase);
   check(recovery[0]?.reason==='derivative-startup-timeout'&&recovery[0]?.prepareMs===900&&recovery[0]?.metadataMs===(phase==='metadata'?null:140)&&recovery[0]?.seekedMs===(phase==='frame'?280:null),'one bounded receipt identifies the last completed stage: '+phase);
   check(recovery[0]?.seekTargetS===(phase==='metadata'?null:Math.round(old.currentTime*1000)/1000),'a sampled target is logged only after metadata: '+phase);
   old.readyState=2;old.fire('canplay');old.fire('seeked');f.show();
   check(f.plays()===0&&f.created()===1&&f.failed.length===1,'late old readiness and the same frame cannot reopen a retired derivative');
   f.pool.show(f.container,original);const next=f.ready();await flush();
   check(next.src===original.entry.url&&!next.paused&&f.media.filter(n=>!!n.src).length===1,'original opens in the same selection with one source');
  }finally{f.pool.stop();}
 }
});
await fault('ready-derivative-wins-at-deadline-and-keeps-normal-progress-watch',async()=>{
 const generated={...frame,entry:{...frame.entry,reducedPlayback:true,recoverOriginal:true}};
 for(const queued of [false,true]){
  const timings:any[]=[],f=faultFixture('normal',generated,undefined,{presented:(_frame,detail)=>timings.push(detail)});
  try{
   f.show();const n=f.media[0];await f.advance(175);n.readyState=1;n.fire('loadedmetadata');n.seeking=true;
   await f.advance(724);n.seeking=false;n.readyState=2;if(!queued)n.fire('seeked');await f.advance(1);
   check(f.failed.length===0&&f.created()===1&&f.plays()===1&&!n.paused&&f.timers.size===1,'already prepared variant is not replaced');
   check(timings[0]?.prepareMs===(queued?900:899)&&timings[0]?.metadataMs===175&&timings[0]?.seekedMs===(queued?null:899),'ready receipt is measured from the same decoder');
   check(timings[0]?.seekTargetS===Math.round(n.currentTime*1000)/1000,'first ready receipt identifies the exact sampled seek');
   check([...f.timers.values()][0].delay===15000,'the short startup timer does not survive reveal');
  }finally{f.pool.stop();}
 }
});
await fault('cold-deadline-cannot-fallback-offscreen-or-on-legacy-host',async()=>{
 const generated={...frame,entry:{...frame.entry,reducedPlayback:true,recoverOriginal:true}};
 for(const edge of ['hidden','blur','off','switch'] as const){
  const f=faultFixture('normal',generated);try{
   f.show();const old=f.media[0];await f.advance(875);
   if(edge==='hidden')f.doc.visibilityState='hidden';else if(edge==='blur')f.setFocus(false);else if(edge==='off')f.setAllowed(false);else f.show(2,'new');
   await f.advance(25);check(!old.src&&f.failed.length===0,'retired selection never asks for the original: '+edge);
  }finally{f.pool.stop();}
 }
 for(const entry of [frame.entry,{...frame.entry,reducedPlayback:true}]){
  const f=faultFixture('normal',{...frame,entry});try{f.show();await f.advance(900);check(f.failed.length===0&&[...f.timers.values()][0].delay===15000,'original and legacy hosts keep their existing startup watch');}finally{f.pool.stop();}
 }
});
await fault('warm-generated-seek-hang-requests-one-temporary-original',async()=>{
 const f=faultFixture('normal',{...frame,entry:{...frame.entry,reducedPlayback:true,recoverOriginal:true}});
 try{
  f.show();let n=f.ready();n.seeking=true;n.fire('seeking');
  await f.advance(1000);n.seeking=false;n.currentTime+=1;n.fire('timeupdate');
  await f.advance(4000);check(!!n.src&&!f.failed.length,'completed seek does not reopen');
  n.seeking=true;n.fire('seeking');await f.advance(4999);
  check(!!n.src&&f.timers.size===1,'warm seek retains one bounded watchdog');
  await f.advance(1);check(!n.src&&f.timers.size===1,'stalled seek releases its decoder');
  await f.advance(500);n=f.error(4);
  check(f.temporaryFailures.length===1&&f.temporaryFailures[0]&&!n.src&&f.timers.size===0,'reopen refusal requests transient original exactly once');
 }finally{f.pool.stop();}
});
await fault('tmnt-current-mediaerror-3-reports-once-with-latest-tuple',async()=>{
 const f=faultFixture('normal',tmntIncident.frame,tmntIncident.metadata);try{
  f.show();for(let attempt=0;attempt<3;attempt++){f.ready();if(attempt===2)f.show(99);const n=f.error(3);for(let i=0;i<100;i++)n.fire('error');await flush();if(attempt<2){check(f.failed.length===0&&f.timers.size===1,'hard media errors also get both local reopen attempts');await f.advance([500,2000][attempt]);}}
  check(f.failed.length===1&&f.timers.size===0,'current decode error reports exactly once after both local retries');
  const value=f.failed[0];check(value.appId==='1361510'&&value.contextSeq===99&&value.entry.assetId===tmntIncident.assetId&&value.entry.bindingRevision===6,'hard failure preserves the latest context and exact asset/binding CAS tuple');
  for(let i=0;i<100;i++){f.show(100+i);for(const n of f.media)n.fire('error');}check(f.failed.length===1&&f.created()===3,'same terminal hard-error tuple never reports twice');
  await f.advance(200);f.show(300,'another-asset');await f.advance(200);f.show(301);check(f.created()===5&&!!f.media.at(-1).src,'leaving and later reselecting the same game can open it again');
 }finally{f.pool.stop();}
});
await fault('terminal-decode-during-revalidation-reports-on-fresh-same-binding-only',async()=>{
 for(const outcome of ['same-binding','changed-binding','changed-app','off'] as const){
  let acceptedSeq=1;const attempts:number[]=[];
  const f=faultFixture('normal',frame,undefined,undefined,value=>{attempts.push(value.contextSeq);return value.contextSeq===acceptedSeq;});
  try{
   f.show();f.error(3);await f.advance(500);f.error(3);await f.advance(2000);f.ready();await flush();
   acceptedSeq=2;const old=f.error(3);
   for(let i=0;i<30;i++){f.show(1);old.fire('error');}
   check(attempts.join(',')==='1'&&f.failed.length===0&&f.timers.size===0&&f.media.every(n=>!n.src&&n.paused),'stale failure leaves native fallback without reopening or resending');
   if(outcome==='off'){f.setAllowed(false);f.pool.stop();}
   if(outcome==='changed-binding'||outcome==='changed-app'){
    const replacement={...frame,contextSeq:2,appId:outcome==='changed-app'?'1091500':frame.appId,entry:{...frame.entry,bindingRevision:outcome==='changed-binding'?2:1}};
    f.pool.show(f.container,replacement);
   }else f.show(2);
   if(outcome==='same-binding'){
    for(let i=0;i<30;i++)f.show(2+i);
    check(attempts.join(',')==='1,2'&&f.failed[0]?.contextSeq===2&&f.failed[0]?.entry.bindingRevision===1,'only the renewed exact binding receives the deferred terminal report');
    check(f.created()===3&&f.timers.size===0,'the deferred report opens no decoder or timer');
   }else check(attempts.join(',')==='1'&&f.failed.length===0,'superseded or disabled selection cannot report a deferred failure: '+outcome);
  }finally{f.pool.stop();}
 }
});
await fault('transient-cooldown-repeats-at-one-minute-without-refilling-fast-budget',async()=>{
 const f=faultFixture('normal',tmntIncident.frame,tmntIncident.metadata);try{
  f.show();for(let i=0;i<3;i++){f.error(2);if(i<2)await f.advance([500,2000][i]);}
  for(let i=0;i<3;i++){check(f.timers.size===1&&[...f.timers.values()][0].delay===60000,'only one low frequency timer survives each unsuccessful attempt');await f.advance(60000);f.ready();f.error(2);}
  check(f.created()===6&&f.failed.length===0,'repeated foreground retries do not regrant the quick budget or invalidate media');
 }finally{f.pool.stop();}
});
await fault('cooldown-requires-thirty-seconds-of-real-progress-to-reset-fast-budget',async()=>{
 const f=faultFixture();try{
  f.show();for(let i=0;i<3;i++){f.error(2);if(i<2)await f.advance([500,2000][i]);}await f.advance(60000);let n=f.ready();await flush();n.currentTime=10;n.fire('timeupdate');
  for(let i=1;i<=5;i++){await f.advance(5000);n.currentTime=10+i;n.fire('timeupdate');}await f.advance(4999);n.currentTime=16;n.fire('timeupdate');f.error(2);
  check([...f.timers.values()][0].delay===60000&&f.failed.length===0,'29.999 seconds of observed progress cannot refill quick retries');
  await f.advance(60000);n=f.ready();await flush();n.currentTime=10;n.fire('timeupdate');
  const created=f.created();
  for(let i=1;i<=6;i++){
   await f.advance(3500);n.fire('waiting');await f.advance(1500);n.currentTime=10+i;n.fire('timeupdate');
   check(f.created()===created&&!!n.src&&f.timers.size===1,'short recovered stalls keep the current decoder and one watch');
  }
  f.error(2);check(f.timers.size===1&&f.failed.length===0,'transient error retains one retry');
  const delayAfterStalls=[...f.timers.values()][0].delay;
  assert.equal(delayAfterStalls,60000,'short interruptions cannot refill quick retries from pre-stall time; actual delay '+delayAfterStalls+'ms');
  await f.advance(60000);n=f.ready();await flush();n.currentTime=10;n.fire('timeupdate');for(let i=1;i<=6;i++){await f.advance(5000);n.currentTime=10+i;n.fire('timeupdate');}
  f.error(2);check(f.timers.size===1&&[...f.timers.values()][0].delay===500&&f.failed.length===0,'thirty seconds of real progress alone restores the first quick retry');
 }finally{f.pool.stop();}
});
await fault('cooldown-hidden-off-blur-stop-and-new-selection-revoke-queued-callbacks',async()=>{
 for(const cause of ['hidden','off','blur','stop','selection'] as const){const f=faultFixture();try{
  f.show();for(let i=0;i<3;i++){f.error(2);if(i<2)await f.advance([500,2000][i]);}const late=[...f.timers.values()][0].run;
  if(cause==='hidden'){f.doc.visibilityState='hidden';f.show();}else if(cause==='off'){f.setAllowed(false);f.pool.stop();}else if(cause==='blur'){f.setFocus(false);f.show();}else if(cause==='stop')f.pool.stop();else f.show(50,'replacement');
  const created=f.created(),timer=[...f.timers.keys()][0];late();await flush();check(f.created()===created&&f.failed.length===0,'cancelled cooldown cannot reopen the preceding selection: '+cause);
  if(cause==='selection')check(f.timers.size===1&&[...f.timers.keys()][0]===timer,'old cooldown cannot erase or replace new selection deadline');else check(f.timers.size===0&&f.media.every(n=>!n.src&&n.paused),'withdrawn foreground ownership immediately has zero media/timers');
  f.pool.stop();await f.advance(120000);late();check(f.created()===created&&f.timers.size===0&&f.failed.length===0,'stopped owner stays dormant beyond old cooldown deadline');
 }finally{f.pool.stop();}}
});
await fault('late-play-rejection-and-old-hard-mediaerror-cannot-poison-current-tuple',async()=>{
 const f=faultFixture('pending');try{
  f.show();const old=f.ready();await flush();await f.advance(15500);const n=f.ready();await flush();const timer=[...f.timers.keys()][0];old.error={code:3};old.rejectPlay(Error('late old play rejection'));for(const event of ['error','loadedmetadata','canplay','seeked','timeupdate'])old.fire(event);await flush();
  check(f.media.at(-1)===n&&!!n.src&&f.failed.length===0&&f.timers.size===1&&[...f.timers.keys()][0]===timer,'retired node events and rejected promise cannot fail the new decoder or steal its timer');
 }finally{f.pool.stop();}
});
await fault('normal-end-loops-and-seeking-without-mediaerror-never-declares-corruption',async()=>{
 const f=faultFixture('normal',tmntIncident.frame,tmntIncident.metadata);try{f.show();const n=f.ready();await flush();n.currentTime=77;n.fire('ended');n.fire('timeupdate');check(n.currentTime===8&&f.failed.length===0,'normal full-video end returns to intro boundary without a failure');n.seeking=true;await f.advance(15000);check(f.failed.length===0&&f.timers.size===1&&[...f.timers.values()][0].delay===500,'seek timeout is only a local retry');}finally{f.pool.stop();}
});

check(faultResults.every(x=>x.ok),'player fault cases failed: '+JSON.stringify(faultResults.filter(x=>!x.ok)));
// The shared document owner survives route teardown without creating timers.
class TrackedEvents extends EventTarget {
 registrations=new Map<string,Set<any>>();
 addEventListener(name:string,listener:any,options?:any){const entries=this.registrations.get(name)||new Set();entries.add(listener);this.registrations.set(name,entries);super.addEventListener(name,listener,options);}
 removeEventListener(name:string,listener:any,options?:any){this.registrations.get(name)?.delete(listener);super.removeEventListener(name,listener,options);}
 count(){return [...this.registrations.values()].reduce((n,s)=>n+s.size,0);}
}
const win:any=new TrackedEvents();win.closed=false;
const pageDoc:any=new TrackedEvents();pageDoc.defaultView=win;pageDoc.visibilityState='visible';pageDoc.hasFocus=()=>true;
const foreground=new WallpaperForeground(pageDoc);let updates=0;let detach=foreground.subscribe(()=>updates++);
check(!foreground.allowed(),'visible DOM focus without current native proof must stay denied');check(foreground.localAllowed(),'local DOM focus may request native proof');foreground.confirmNative(true);check(foreground.allowed(),'current native Big Picture proof admits the visible page');const registrations=win.count()+pageDoc.count();
let focusReads=0;pageDoc.hasFocus=()=>{focusReads++;return true;};
check(foreground.localVeto()===null&&foreground.nativeAdmitted&&focusReads===1,'visibility decision and native proof share a single document focus read');
pageDoc.hasFocus=()=>false;check(foreground.localVeto()==='document-unfocused'&&!foreground.allowed(),'focused window with no document focus retains the local veto');pageDoc.hasFocus=()=>true;
detach();win.dispatchEvent(new Event('blur'));detach=foreground.subscribe(()=>updates++);check(!foreground.allowed(),'new route subscription preserves a blur recorded with no routes');
check(foreground.localVeto()==='window-blurred'&&!foreground.nativeAdmitted,'window blur has a separate reason and revokes native admission');
win.dispatchEvent(new Event('focus'));check(!foreground.allowed(),'window focus alone cannot restore a retired native proof');foreground.confirmNative(true);check(foreground.allowed(),'fresh native proof restores document admission');
pageDoc.dispatchEvent(new Event('freeze'));detach();detach=foreground.subscribe(()=>updates++);check(!foreground.allowed(),'remount cannot erase a document freeze');
check(foreground.localVeto()==='document-suspended','a frozen document reports suspension');
win.dispatchEvent(new Event('focus'));check(!foreground.allowed(),'window focus does not thaw a frozen document');pageDoc.dispatchEvent(new Event('resume'));check(!foreground.allowed(),'resume requires a new native proof');foreground.confirmNative(true);check(foreground.allowed(),'new native proof restores the foreground route');
for(let i=0;i<100;i++){detach();detach=foreground.subscribe(()=>updates++);}check(win.count()+pageDoc.count()===registrations,'one event owner survives repeated route subscriptions');
pageDoc.visibilityState='hidden';pageDoc.dispatchEvent(new Event('visibilitychange'));check(!foreground.allowed(),'hidden document remains blocked with a focused snapshot');pageDoc.visibilityState='visible';pageDoc.dispatchEvent(new Event('visibilitychange'));check(!foreground.allowed(),'visible document cannot reuse hidden-page proof');foreground.confirmNative(true);check(foreground.allowed(),'visible foreground document recovers with current native proof');
pageDoc.visibilityState='hidden';check(foreground.localVeto()==='document-hidden','document hidden is distinct from an unconfirmed native proof');pageDoc.visibilityState='visible';
win.dispatchEvent(new Event('pagehide'));detach();win.dispatchEvent(new Event('pageshow'));check(!foreground.allowed(),'page transition cannot restore an expired proof');foreground.confirmNative(true);check(foreground.allowed(),'current native proof resumes after page transition');
win.dispatchEvent(new Event('blur'));foreground.revalidateFocus(new Event('focusin'));check(!foreground.allowed(),'synthetic controller focus cannot override background blur');
pageDoc.hasFocus=()=>false;foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted DOM selection without document focus stays paused');
pageDoc.hasFocus=()=>true;foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted DOM selection cannot grant actual native foreground');foreground.confirmNative(true);check(foreground.allowed(),'fresh native proof recovers the selected route');
win.dispatchEvent(new Event('blur'));pageDoc.visibilityState='hidden';foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted focus cannot activate a hidden document');
pageDoc.visibilityState='visible';pageDoc.dispatchEvent(new Event('freeze'));foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted focus cannot thaw a frozen document');pageDoc.dispatchEvent(new Event('resume'));foreground.revalidateFocus({isTrusted:true} as Event);check(!foreground.allowed(),'trusted resumed selection still requires native proof');foreground.confirmNative(true);check(foreground.allowed(),'current native proof recovers after document resume');
const stableUpdates=updates;for(let n=0;n<100;n++)foreground.confirmNative(true);check(updates===stableUpdates,'stable native foreground does not republish or restart player');foreground.confirmNative(false);check(!foreground.allowed(),'native blur vetoes stale affirmative document focus');foreground.confirmNative(true);
const last=updates;foreground.stop();foreground.stop();win.dispatchEvent(new Event('focus'));pageDoc.dispatchEvent(new Event('resume'));check(updates===last&&!foreground.allowed(),'stopped event owner cannot publish or authorize playback');check(win.count()+pageDoc.count()===0,'disposal unregisters all document and window callbacks');
console.log(JSON.stringify({ok:true,canonicalId:'plugins.wallpaper-background',checks,incident:tmntIncident,faultCases:faultResults,mixedStallWatch,scope:'production full-video range, foreground guard and bounded player fault recovery with virtual time/inert media events; no network or Steam'}));
