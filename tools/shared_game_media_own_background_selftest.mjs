import fs from'node:fs';import path from'node:path';import{createRequire}from'node:module';import{strict as assert}from'node:assert';
const dcPower=process.argv.includes('--dc-power'),editorCover=process.argv.includes('--editor-cover');
if(editorCover&&(!process.argv.includes('--app-player')||dcPower))throw Error('editor cover requires only original app-player group');
if(dcPower&&process.argv.includes('--app-player'))throw Error('select only one directed group');
for(let i=3;i<process.argv.length;i++){if(process.argv[i]==='--app-source'){if(!process.argv[++i])throw Error('--app-source requires a path');}else if(!['--app-player','--dc-power','--editor-cover'].includes(process.argv[i]))throw Error('unknown selection: '+process.argv[i]);}
const project=path.resolve(import.meta.dirname,'..'),out=process.argv[2];if(!out)throw Error('output directory required');const require=createRequire(project+'/package.json'),{build}=require('esbuild');
const bindingMock=`export function hasWallpaperBinding(entry){return Boolean(entry&&!entry.deleted&&(entry.ready===true||entry.wallpaper30Limited===true&&entry.dynamicReady===true));}`;

// Extend the original consumer with the actual App.vue playback handlers. Only
// DOM/media/clock/IPC boundaries are inert; the handler bodies and template
// error binding come from the selected production source, including negatives.
if(dcPower||process.argv.includes('--app-player')){
 const ts=require('typescript'),crypto=require('node:crypto');fs.mkdirSync(out,{recursive:true});
 const at=process.argv.indexOf('--app-source'),appPath=at>=0?path.resolve(process.argv[at+1]):project+'/src/App.vue';
 const app=fs.readFileSync(appPath,'utf8'),script=app.match(/<script setup[^>]*>([\s\S]*?)<\/script>/)?.[1];assert.ok(script,'App script missing');
 const tree=ts.createSourceFile('App.ts',script,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS),names=new Set(['reconcileBackgroundVideo','onBackgroundVideoError','onBackgroundVideoMediaError','scheduleSharedBackgroundRetry','onBackgroundVideoPlaying','clearBackgroundRetry','suspendBackgroundVideo','dynamicBackgroundAllowed','cancelFactoryVideoStart']);
 const originalFallback=tree.statements.some(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='recoverDynamicBackgroundOriginal');
 if(originalFallback)names.add('recoverDynamicBackgroundOriginal');
 const windowVisibility=!dcPower&&!editorCover;
 if(windowVisibility)for(const name of ['pauseBackgroundVideoForWindow','onBackgroundVisibilityChange','onBackgroundWindowHidden','onBackgroundWindowShown','shouldSuspendBackgroundVideo','resumeBackgroundVideo','onBackgroundVideoLoaded','onBackgroundVideoTimeUpdate','onBackgroundVideoCanPlay'])names.add(name);
 if(tree.statements.some(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='backgroundVideoShouldPlay'))names.add('backgroundVideoShouldPlay');
 const factoryGate=tree.statements.some(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='onBackgroundWindowActivation');
 if(factoryGate)for(const name of ['backgroundVideoPlaybackEligible','onBackgroundWindowActivation'])names.add(name);
 const windowFocusGate=tree.statements.some(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='onBackgroundFocusChanged');
 if(windowFocusGate)names.add('onBackgroundFocusChanged');
 if(editorCover)for(const name of ['acquireControllerEditorBackgroundHold','resumeAfterBackgroundHold','onBackgroundVideoPause','onBackgroundVideoCanPlay'])names.add(name);
 if(dcPower)for(const name of ['destroyHls','invalidateBackgroundMedia','isHlsUrl','isVideoPlaybackBlockedByPower','setupBackgroundVideo','shouldSuspendBackgroundVideo','reconcileBackgroundLifecycle','resumeBackgroundVideo','onBackgroundVideoPause','onBackgroundVideoCanPlay'])names.add(name);
 for(const name of names)assert.ok(tree.statements.some(n=>ts.isFunctionDeclaration(n)&&n.name?.text===name),'production App function missing: '+name);
 const bodies=tree.statements.filter(n=>ts.isFunctionDeclaration(n)&&names.has(n.name?.text)).map(n=>n.getText(tree)).join('\n');
 const binding=app.match(/@error="([^"]+)"/)?.[1];assert.ok(binding,'real video error binding missing');
 const visibilityApi=windowVisibility?',visibility:onBackgroundVisibilityChange,shown:onBackgroundWindowShown,hidden:onBackgroundWindowHidden,loaded:onBackgroundVideoLoaded,canPlay:onBackgroundVideoCanPlay':'';
 const activationApi=(factoryGate?',activation:onBackgroundWindowActivation':'')+(windowFocusGate?',focus:onBackgroundFocusChanged':'');
 const make=new Function('env',ts.transpileModule('with(env){'+bodies+';return {error:(event)=>('+binding+')(event),play:reconcileBackgroundVideo,playing:onBackgroundVideoPlaying,clear:clearBackgroundRetry,suspend:suspendBackgroundVideo,allowed:dynamicBackgroundAllowed'+(editorCover?',hold:acquireControllerEditorBackgroundHold,pause:onBackgroundVideoPause,canPlay:onBackgroundVideoCanPlay':'')+(dcPower?',setup:setupBackgroundVideo,lifecycle:reconcileBackgroundLifecycle,resume:resumeBackgroundVideo,pause:onBackgroundVideoPause,canPlay:onBackgroundVideoCanPlay,retry:scheduleSharedBackgroundRetry,epoch:()=>backgroundMediaEpoch,hls:()=>hlsInstance':'')+visibilityApi+activationApi+'};}',{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.None}}).outputText);
 const captured={key:'steam-1361510',assetId:'3b507d102803210fd2601475605a7275',bindingRevision:6,kind:'video'};
 const reports=[],tests=[];const dcRuntime=dcPower?await createDcPowerRuntime(out):null;let checks=0;const same=(a,b,m)=>{checks++;assert.deepEqual(a,b,m);};
 function fixture(options={}){let id=0;const timers=new Map(),reports=[],events=[],calls=[],ticks=[],hls=[];const ref=value=>({value});
  if(dcPower)dcRuntime.reset();
  const video={error:null,readyState:4,paused:true,ended:false,currentTime:8,src:'https://game-media.localhost/video/'+captured.assetId+'.mp4',pause(){this.paused=true;calls.push('pause');},getAttribute(k){return k==='src'&&this.src?this.src:null;},removeAttribute(k){if(k==='src'){this.src='';calls.push('remove:src');}},load(){this.error=null;calls.push('load');},play(){this.paused=false;calls.push('play');return Promise.resolve();}};
  const env={backgroundEditorPaused:ref(false),backgroundEditorHolds:new Set(),backgroundEditorPauseRevision:0,backgroundPresentationDisposed:false,wallpaperPowerPaused:ref(false),backgroundVideo:ref(video),backgroundKind:ref('video'),backgroundSource:ref('dynamic'),backgroundUrl:ref(video.src),backgroundVideoSuspended:ref(false),documentVisible:ref(true),nativeWindowVisible:ref(true),lowBatteryStatic:ref(false),videoDesired:ref(false),backgroundVideoGeneration:ref(1),backgroundMediaEpoch:1,backgroundRetryTimer:null,backgroundRetryUntil:0,sharedBackgroundRetryIdentity:'',sharedBackgroundRetryAttempts:0,activeDynamicMediaAsset:{...captured},backgroundFallbackUrls:ref([]),backgroundFallbackIndex:ref(0),dynamicBackgroundActive:ref(true),enabled:true,powerBlocked:false,sharedGameMedia:{playbackFailed(e){reports.push({...e});return Promise.resolve();}},getDynamicBackgroundConfig:()=>({enabled:env.enabled}),isVideoPlaybackBlockedByPower:()=>env.powerBlocked,invalidateBackgroundMedia:()=>++env.backgroundMediaEpoch,shouldSkipVideoReconcile:(a,b,playing)=>a===b&&(b?playing:!playing),isHlsUrl:()=>false,nextTick:async fn=>fn?.(),setupBackgroundVideo:async()=>{calls.push('setup');env.backgroundVideo.value.src=env.backgroundUrl.value;env.backgroundVideo.value.readyState=4;},scheduleBackgroundRetry:()=>calls.push('fixed-retry'),window:{setTimeout(fn,ms){timers.set(++id,{fn,ms});return id;},clearTimeout(n){timers.delete(n);},dispatchEvent(e){events.push(e);}},console:{warn(){}},CustomEvent:globalThis.CustomEvent};
  if(dcPower){
   Object.defineProperty(env.wallpaperPowerPaused,'value',{get:()=>dcRuntime.api.wallpaperPowerBlocked()});
   Object.assign(env,{backgroundVideoAutoPause:ref(false),videoPowerState:ref('desktop'),hlsInstance:null,shouldSkipVideoReconcile:dcRuntime.api.shouldSkipVideoReconcile});
   if(options.hls){env.backgroundUrl.value='https://fixture/TMNT.m3u8';video.src=env.backgroundUrl.value;}
   video.canPlayType=()=>'';
   class MediaHls {
    static Events={MEDIA_ATTACHED:'attached',ERROR:'error'};static isSupported(){return true;}
    constructor(){this.handlers=new Map();hls.push(this);calls.push('hls:create');}
    on(name,fn){this.handlers.set(name,fn);}emit(name,data){this.handlers.get(name)?.(name,data);}
    attachMedia(element){this.video=element;calls.push('hls:attach');this.emit('attached');}
    loadSource(url){calls.push('hls:loadSource');this.video.src=url;this.video.readyState=4;}
    destroy(){calls.push('hls:destroy');}
   }
   env.window.Hls=MediaHls;
   if(options.deferTicks)env.nextTick=fn=>new Promise(resolve=>ticks.push(resolve)).then(()=>fn?.());
  }
  video.hasAttribute=k=>video.getAttribute(k)!==null;
  Object.assign(env,{playingWallpaperPreviews:0,factoryBackgroundVideo:ref(false),factoryBackgroundPoster:ref(undefined),factoryVideoStartTimer:null,factoryWindowForeground:null,backgroundWindowActivation:null,
   document:{visibilityState:'visible',hasFocus:()=>true},queueSharedBackgroundRefresh(){},setNativeWindowVisible(){},stopDynamicBackgroundWatch(){},requestVideoPowerCheck(){},
   dynamicOriginalFallback:null,dynamicBackgroundGeneration:1,sharedMediaDirty:false,currentGame:{pid:12345,processCreated:'134372974230246308'},
   getCurrentGame:()=>env.currentGame,isCurrentGameTarget:target=>target===env.currentGame,sharedMediaRevisions:{outdated:()=>!!env.staleBinding}});
  const api=make(env);return {api,env,video,timers,reports,calls,ticks,hls,source(mode){return dcRuntime.source(mode);},async drain(){for(let n=0;n<48;n++){for(const resolve of ticks.splice(0))resolve();await Promise.resolve();}assert.equal(ticks.length,0,'bounded nextTick drain');},async fire(){const [id,t]=[...timers][0]||[];assert.ok(t,'missing owned retry');timers.delete(id);t.fn();for(let i=0;i<8;i++)await Promise.resolve();}};
 }
 async function run(name,fn){tests.push({name,fn});}
 if(windowVisibility)await run('Repeated media readiness keeps the current play request',async()=>{
  const f=fixture();let finish;
  f.env.backgroundPlayback={value:null};
  f.video.play=function(){this.paused=false;f.calls.push('play');return new Promise(resolve=>{finish=resolve;});};
  f.video.readyState=1;
  f.api.loaded();await f.drain();
  same(f.calls,['play'],'metadata starts a single request');
  f.video.readyState=4;
  for(let n=0;n<3;n++){f.api.canPlay();await f.drain();}
  same(f.calls,['play'],'canplay does not repeat an already advancing request');
  finish();await f.drain();f.api.loaded();await f.drain();
  same(f.calls,['play'],'a later metadata edge also keeps the request');
  f.video.paused=true;f.api.canPlay();await f.drain();
  same(f.calls,['play','play'],'a real pause still permits a new request');
  finish();await f.drain();
 });
 if(windowFocusGate&&windowVisibility)await run('Visible custom and dynamic wallpapers follow activation without releasing playback',async()=>{
  for(const source of ['fixed','dynamic']){
   const f=fixture(),active=value=>f.api.activation({detail:{active:value}});
   f.env.backgroundSource.value=source;
   const url=f.video.src;
   await f.api.play();same(f.video.paused,false,source+' starts with legacy focus');
   f.env.document.hasFocus=()=>false;f.api.focus();await f.drain();same(f.video.paused,true,source+' pauses on legacy blur');
   active(true);await f.drain();same(f.video.paused,false,source+' follows native activation');
   active(false);await f.drain();same(f.video.paused,true,source+' pauses on native deactivation');
   f.env.document.hasFocus=()=>true;f.api.focus();await f.drain();same(f.video.paused,true,source+' DOM focus cannot override native deactivation');
   active('true');await f.drain();same(f.video.paused,true,source+' ignores malformed activation');
   same(f.video.src,url,source+' retains the source');same(f.video.currentTime,8,source+' retains the playback position');
   active(true);await f.drain();same(f.video.paused,false,source+' resumes after native activation');
   f.api.hidden();active(true);await f.drain();same(f.video.paused,true,source+' activation cannot start a hidden video');
  }
  const f=fixture();await f.api.play();f.video.error={code:2};f.api.error({target:f.video});
  f.api.activation({detail:{active:false}});await f.drain();await f.fire();await f.drain();
  same(f.video.src,f.env.backgroundUrl.value,'an outstanding recovery may reattach the selected source');
  same(f.video.paused,true,'the recovered source stays paused while inactive');
  f.api.activation({detail:{active:true}});await f.drain();same(f.video.paused,false,'a recovered source resumes on activation');
 });
 if(factoryGate&&windowVisibility)await run('Factory video follows native foreground and rejects a late foreground grant',async()=>{
  const f=fixture(),foreground=active=>f.api.activation({detail:{active}});
  f.env.backgroundSource.value='fixed';f.env.factoryBackgroundVideo.value=true;
  f.env.factoryBackgroundPoster.value='poster';f.video.src='';
  const showState=foreground=>({visible:true,minimized:false,foreground});
  const drain=async()=>{for(let n=0;n<12;n++)await Promise.resolve();};
  let resolveState;
  f.env.windowApi={getState:async()=>showState(false)};
  await f.api.play();await f.fire();same(f.video.src,'','DOM focus alone cannot attach factory video');
  same(f.timers.size,0,'no retries when another window owns foreground');
  f.env.windowApi.getState=()=>new Promise(resolve=>{resolveState=resolve;});
  foreground(true);await drain();await f.fire();assert.equal(typeof resolveState,'function');
  foreground(false);resolveState(showState(true));await drain();
  same(f.video.src,'','late reply cannot start the video after deactivation');
  f.env.windowApi.getState=async()=>showState(true);foreground(true);await drain();await f.fire();
  same(f.video.src,f.env.backgroundUrl.value,'verified foreground attaches the video');
  same(f.video.paused,false,'video plays in foreground');
  foreground(false);await drain();same(f.video.paused,true,'native blur pauses playback');
  foreground(true);await drain();same([...f.timers.values()][0]?.ms,0,'resume reuses the existing source');
  await f.fire();same(f.video.paused,false,'native foreground resumes playback');
 });
 await run('captured TMNT stale error must not poison current shared asset',()=>{const f=fixture();f.api.error({target:{error:{code:3}}});same(f.reports,[],'detached error cannot report the current TMNT asset');same(f.env.backgroundMediaEpoch,1,'detached error cannot invalidate current decoder');});
 for(const code of [1,2])await run('TMNT media interruption code '+code+' retains valid cached video',()=>{const f=fixture();f.video.error={code};f.api.error({target:f.video});same(f.reports,[],'interruption is not shared corruption');same(f.timers.size,1,'one selected recovery timer');same([...f.timers.values()][0].ms,500,'first foreground retry is prompt and owned');same(f.video.paused,true,'interrupted decoder paused');same(f.video.src,'','interrupted decoder releases source');});
 await run('play AbortError cannot persistently exclude TMNT from either consumer',async()=>{const f=fixture();f.video.play=()=>Promise.reject(Object.assign(Error('play interrupted'),{name:'AbortError'}));await f.api.play();same(f.reports,[],'play promise rejection is not proof of bad bytes');same(f.timers.size,1,'one recovery owned by current selection');});
 await run('all plain play rejections remain local',async()=>{const f=fixture();f.video.play=()=>Promise.reject(Error('renderer lost'));await f.api.play();same(f.reports,[],'plain rejection cannot mutate shared cache');same(f.timers.size,1,'rejection waits for recovery');});
 await run('current confirmed media error 3 still reports exact CAS identity',()=>{const f=fixture();f.video.error={code:3};f.api.error({target:f.video});same(f.reports,[captured],'actual decode error retains quarantine path');f.api.error({target:f.video});same(f.reports.length,1,'duplicate event never reports twice');});
 if(windowVisibility){
  function coldReduced(){const f=fixture(),requests=[],applied=[];let reply;
   Object.assign(f.env.activeDynamicMediaAsset,{...f.env.currentGame,reducedPlayback:true});
   f.env.readDynamicBackgroundOriginal=(...args)=>{requests.push(args);return new Promise(resolve=>reply=resolve);};
   f.env.applyBackgroundState=(...args)=>applied.push(args);
   return {...f,requests,applied,reply:state=>reply?.(state)};
  }
  const original={enabled:true,kind:'video',url:'https://game-media.localhost/video/original.webm'};
  for(const code of [2,4])await run('30 FPS load error '+code+' falls back once without cache edit',async()=>{
   const f=coldReduced();f.video.readyState=0;
   for(let n=0;n<6;n++){f.video.error={code};f.api.error({target:f.video});}
   same(f.requests,[[f.env.currentGame,{key:captured.key,assetId:captured.assetId,bindingRevision:captured.bindingRevision,...f.env.currentGame}]],'one local read for the exact failed derivative');
   same(f.reports,[],'a load error cannot reject the shared file');same(f.timers.size,0,'no timer while resolving original');
   f.reply(original);await f.drain();same(f.applied,[[original,'dynamic']],'the original is applied on reply');
   same(f.env.dynamicOriginalFallback.assetId,captured.assetId,'ordinary same-selection refresh carries the bypass');
  });
  for(const boundary of ['hidden','off','sleep','other-game','new-binding'])await run('30 FPS original read stays retired after '+boundary,async()=>{
   const f=coldReduced();f.video.error={code:4};f.api.error({target:f.video});same(f.requests.length,1,'read began before withdrawal');
   if(boundary==='hidden'){f.env.documentVisible.value=false;f.api.suspend();}
   if(boundary==='off')f.env.enabled=false;
   if(boundary==='sleep'){f.env.dynamicBackgroundGeneration++;f.api.suspend();}
   if(boundary==='other-game')f.env.currentGame={...f.env.currentGame,processCreated:'134372974230246310'};
   if(boundary==='new-binding')f.env.staleBinding=true;
   f.reply(original);await f.drain();same(f.applied,[],'no stale original can replace the current surface');same(f.reports,[],'no persistent cache effect');
   if(boundary==='hidden'||boundary==='sleep')same(f.env.dynamicOriginalFallback,null,'new visibility scope may use the derivative again');
  });
  await run('missing original returns to the existing local retry',async()=>{
   const f=coldReduced();f.video.error={code:4};f.api.error({target:f.video});f.reply(null);await f.drain();
   same(f.requests.length,1,'one attempt');same(f.env.dynamicOriginalFallback,null,'unavailable fallback is not sticky');
   same(f.reports,[],'no cache edit on exhaustion');same([...f.timers.values()].map(t=>t.ms),[500],'original recovery delay stays bounded');
  });
 }
 await run('TMNT source rejected before decode keeps the shared video eligible',async()=>{const f=fixture(),source=f.video.src;
  for(const ms of [500,2000,60000]){
   f.video.readyState=0;f.video.error={code:4};f.api.error({target:f.video});
   same(f.reports,[],'source rejection cannot report shared corruption');same(f.env.activeDynamicMediaAsset,captured,'same cached binding stays selected');
   same([...f.timers.values()].map(t=>t.ms),[ms],'one selected retry uses the original backoff');same(f.video.src,'','failed decoder releases source');
   if(ms<60000){const generation=f.env.backgroundVideoGeneration.value;await f.fire();
    if(dcPower){
     same(f.env.backgroundVideoGeneration.value,generation+1,'one keyed video remount is requested');
     same(f.env.backgroundUrl.value,source,'remount retains the same cached source');
     const {parse}=require('@vue/compiler-sfc'),{baseParse,NodeTypes}=require('@vue/compiler-dom');
     const template=baseParse(parse(app).descriptor.template.content);
     const root=template.children.find(n=>n.type===NodeTypes.ELEMENT);
     const video=root?.children.find(n=>n.type===NodeTypes.ELEMENT&&n.tag==='video'&&n.props.some(p=>p.type===NodeTypes.ATTRIBUTE&&p.name==='ref'&&p.value?.content==='backgroundVideo'));
     const srcBinding=video?.props.find(p=>p.type===NodeTypes.DIRECTIVE&&p.name==='bind'&&p.arg?.content==='src')?.exp?.content;
     assert.ok(srcBinding,'actual Vue background source binding missing');
     const resolveSrc=new Function('backgroundUrl','factoryBackgroundPoster','isHlsUrl','return ('+srcBinding+')');
     same(resolveSrc(source,undefined,url=>url.endsWith('.m3u8')),source,'actual Vue remount binds the same cached non-HLS URL');
    }else same(f.video.src,source,'retry opens only the same cached source');
   }
  }
  const late=[...f.timers.values()][0].fn, generation=f.env.backgroundVideoGeneration.value;
  f.env.documentVisible.value=false;f.api.suspend();late();f.video.error={code:4};f.api.error({target:f.video});await f.drain();
  same(f.env.backgroundVideoGeneration.value,generation,'hidden retry does not remount the decoder');same(f.timers.size,0,'hidden route has no retry owner');
  same(f.video.src,'','hidden route keeps source released');same(f.reports,[],'hidden error cannot poison the shared asset');
 });
 await run('error storm coalesces to one recovery',()=>{const f=fixture();for(let n=0;n<100;n++){f.video.error={code:2};f.api.error({target:f.video});}same(f.timers.size,1,'no timer per error event');same(f.reports.length,0,'storm cannot quarantine healthy bytes');});
 await run('two fast retries then a single slow retry without poisoning cache',async()=>{const f=fixture();for(const ms of [500,2000,60000,60000]){f.video.error={code:2};f.api.error({target:f.video});same(f.timers.size,1,'one retry per selected decoder');same([...f.timers.values()][0].ms,ms,'fast budget is bounded then backs off');await f.fire();}same(f.reports.length,0,'persistent interruption never globally excludes the file');});
 await run('foreground recovery reopens only selected cached video',async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});await f.fire();same(f.env.backgroundVideoGeneration.value,2,'one decoder remount');same(f.calls.filter(x=>x==='setup').length,1,'one selected source setup');same(f.calls.filter(x=>x==='play').length,1,'current source plays after recovery');same(f.reports.length,0,'no download or global invalidation');});
 for(const boundary of ['hidden','off','power','other-game','new-binding'])await run('recovery withdrawn at '+boundary,async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});if(boundary==='hidden')f.env.documentVisible.value=false;if(boundary==='off')f.env.enabled=false;if(boundary==='power')f.env.powerBlocked=true;if(boundary==='other-game'){f.env.backgroundMediaEpoch++;f.env.activeDynamicMediaAsset={...captured,key:'steam-1091500',assetId:'control'};}if(boundary==='new-binding')f.env.activeDynamicMediaAsset={...captured,bindingRevision:7};await f.fire();same(f.env.backgroundVideoGeneration.value,1,'withdrawn retry cannot mount decoder');same(f.calls.filter(x=>x==='setup').length,0,'withdrawn retry cannot attach source');same(f.reports.length,0,'withdrawal cannot mutate cache');same(f.timers.size,0,'no background heartbeat');});
 await run('suspension cancels retry and queued stale callback stays silent',async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});const late=[...f.timers.values()][0].fn;f.api.suspend();same(f.timers.size,0,'visibility lifecycle cancels timer');late();for(let n=0;n<4;n++)await Promise.resolve();same(f.env.backgroundVideoGeneration.value,1,'cancelled callback cannot revive source');});
 await run('detached playing event cannot cancel current recovery',()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});f.api.playing({target:{}});same(f.timers.size,1,'old playing receipt cannot cancel current retry');f.api.playing({target:f.video});same(f.timers.size,0,'current actual playback clears retry');});
 await run('a late rejected old play cannot corrupt a new selection',async()=>{const f=fixture();let reject;f.video.play=()=>new Promise((_,r)=>reject=r);const pending=f.api.play();await Promise.resolve();f.env.backgroundMediaEpoch++;f.env.backgroundVideo.value={...f.video};f.env.activeDynamicMediaAsset={...captured,bindingRevision:7};reject(Error('late old error'));await pending;same(f.reports,[],'old promise has no write permission');same(f.timers.size,0,'old promise creates no retry');});
 if(dcPower)await registerDcPowerCases({run,fixture,same,runtime:dcRuntime,out});

 if(windowVisibility){
  for(const backgroundSource of ['fixed','dynamic'])for(const surface of ['document','native'])await run(`Window: ${surface} hide and restore for ${backgroundSource} video`,async()=>{
   const f=fixture(),source=f.video.src,isDynamic=backgroundSource==='dynamic';
   f.env.backgroundSource.value=backgroundSource;
   await f.api.play();same(f.video.paused,false,'visible playback is running');
   f.calls.length=0;
   if(surface==='document'){f.env.document.visibilityState='hidden';f.api.visibility();}else f.api.hidden();
   same(f.video.paused,true,'hidden window stops playback');
   same(f.env.backgroundVideoSuspended.value,isDynamic,'only hidden dynamic media suspends the element');
   same(f.video.src,isDynamic?'':source,'dynamic releases its source and fixed retains its source');
   same(f.calls,isDynamic?['pause','remove:src','load']:['pause'],'hidden media work is bounded');
   same(f.env.backgroundMediaEpoch,isDynamic?2:1,'only a released source revokes old media callbacks');
   same(f.timers.size,0,'hidden has no recovery timer');
   if(surface==='native'){
    f.api.visibility();await f.drain();
    same(f.video.paused,true,'visible Chromium document cannot reopen a native-hidden window');
    same(f.video.src,isDynamic?'':source,'no source change while the native window remains hidden');
   }
   if(surface==='document'){f.env.document.visibilityState='visible';f.api.visibility();}else f.api.shown();
   await f.drain();
   same(f.video.src,source,'the same selection returns');
   same(f.video.paused,false,'visible playback resumes');
   same(f.env.backgroundVideoSuspended.value,false,'no stale suspension after restore');
   same(f.calls.filter(c=>c==='setup').length,isDynamic?1:0,'only a released source needs setup');
   same(f.reports,[],'normal hide and restore never quarantine the asset');
  });
 }

 if(editorCover){
  await run('Editor: hidden decode error keeps shared bytes eligible until local reopen',async()=>{const f=fixture();const source=f.video.src,release=f.api.hold();f.video.error={code:3};f.api.error({target:f.video});same(f.reports,[],'covered decoder cannot quarantine a shared asset');same(f.timers.size,0,'covered decoder cannot schedule work');same(f.video.src,source,'covered editor retains its frame and source');release();await f.drain();same(f.timers.size,1,'reopening schedules one local recovery');same(f.reports,[],'covered decode error never becomes a shared error on release');await f.fire();same(f.video.src,source,'local recovery reopens exactly the same asset');same(f.env.backgroundVideoGeneration.value,2,'one recovery only');same(f.video.paused,false,'same video can play again');f.video.error={code:3};f.api.error({target:f.video});same(f.reports,[captured],'a new foreground decode error still publishes the exact CAS identity');});
  await run('Editor: queued shared recovery does not remount until cover is released',async()=>{const f=fixture();const source=f.video.src;f.video.error={code:2};f.api.error({target:f.video});const late=[...f.timers.values()][0].fn;const release=f.api.hold();same(f.timers.size,0,'covered editor has no shared recovery timer');late();await f.drain();same(f.env.backgroundVideoGeneration.value,1,'old queued timer cannot mount under the cover');same(f.calls.filter(x=>x==='setup').length,0,'no decoder setup under the cover');release();await f.drain();same(f.timers.size,1,'opening the window again re-arms one local recovery');await f.fire();same(f.video.src,source,'reopens the selected cache only');same(f.env.backgroundVideoGeneration.value,2,'no duplicate remount');same(f.reports,[],'no cache write');});
  await run('Editor: held retry does not re-arm after selection changes',async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});const release=f.api.hold(),replacement='https://game-media.localhost/video/other.mp4';f.env.backgroundMediaEpoch++;f.env.activeDynamicMediaAsset={key:'steam-570',assetId:'other',bindingRevision:7,kind:'video'};f.env.backgroundUrl.value=replacement;f.video.src=replacement;release();await f.drain();same(f.timers.size,0,'new source has no inherited retry');same(f.video.src,replacement,'release preserves the replacement');same(f.video.paused,false,'replacement follows normal playback');same(f.reports,[],'no write to the current or previous cache');});
  await run('Editor: pause keeps the current frame and source without recovery',async()=>{const f=fixture();await f.api.play();f.calls.length=0;const url=f.video.src,release=f.api.hold();same(f.video.paused,true,'covered video pauses immediately');same(f.video.currentTime,8,'last frame retained');same(f.video.src,url,'source retained');same(f.env.backgroundMediaEpoch,1,'no media invalidation');await f.api.play();f.api.canPlay();await f.drain();same(f.calls,['pause'],'metadata and canplay cannot restart covered playback');same(f.timers.size,0,'no retry/recovery timer');same(f.reports,[],'no cache mutation');release();await f.drain();same(f.video.paused,false,'last cover release resumes via original playback consumer');release();await f.drain();same(f.calls,['pause','play'],'release is idempotent');});
  await run('Editor: overlapping local holds release only the final owner',async()=>{const f=fixture();await f.api.play();f.calls.length=0;const a=f.api.hold(),b=f.api.hold();same(f.calls,['pause'],'first owner alone pauses');a();a();await f.drain();same(f.video.paused,true,'other owner still covers video');same(f.env.backgroundEditorHolds.size,1,'duplicate release cannot revoke other owner');b();await f.drain();same(f.calls,['pause','play'],'one resume after final owner');});
  await run('Editor: late rejected play after reopening cannot fault new playback',async()=>{const f=fixture();let reject;f.video.play=function(){f.calls.push('deferred-play');return new Promise((_,r)=>reject=r)};const old=f.api.play();await Promise.resolve();const release=f.api.hold();f.video.play=function(){this.paused=false;f.calls.push('play');return Promise.resolve()};release();await f.drain();same(f.env.videoDesired.value,true,'new visible playback started');reject(Object.assign(Error('paused by covered editor'),{name:'AbortError'}));await old;same(f.env.videoDesired.value,true,'old rejection cannot erase current desired playback');same(f.timers.size,0,'old interruption cannot create recovery');same(f.reports,[],'old interruption cannot quarantine media');});
  await run('Editor: late successful play cannot escape an active cover',async()=>{const f=fixture();let finish;f.video.play=function(){return new Promise(resolve=>finish=()=>{this.paused=false;resolve()})};const old=f.api.play();await Promise.resolve();f.api.hold();finish();await old;same(f.video.paused,true,'promise completion rechecks current cover');same(f.timers.size,0,'no restart timer');same(f.reports,[],'no recovery');});
  await run('Editor: releasing cover preserves every original playback gate',async()=>{for(const gate of ['document','native','battery','power','dynamic','kind','url','disposed']){const f=fixture();await f.api.play();f.calls.length=0;const release=f.api.hold();if(gate==='document')f.env.documentVisible.value=false;else if(gate==='native')f.env.nativeWindowVisible.value=false;else if(gate==='battery')f.env.lowBatteryStatic.value=true;else if(gate==='power')f.env.powerBlocked=true;else if(gate==='dynamic')f.env.enabled=false;else if(gate==='kind')f.env.backgroundKind.value='image';else if(gate==='url')f.env.backgroundUrl.value='';else f.env.backgroundPresentationDisposed=true;release();await f.drain();same(f.video.paused,true,gate+' blocks playback');same(f.calls.filter(x=>x==='play').length,0,gate+' cannot grant playback');same(f.timers.size,0,gate+' creates no recovery');}});
  await run('Editor: queued pause repair and fixed media obey local cover',async()=>{const f=fixture();f.env.backgroundSource.value='fixed';f.env.enabled=false;await f.api.play();same(f.video.paused,false,'fixed media uses original independent selection');f.api.pause();same(f.timers.size,1,'original pause reconciliation queued');const release=f.api.hold();await f.fire();same(f.video.paused,true,'old timer cannot restart covered video');same(f.timers.size,0,'no second repair loop');release();await f.drain();same(f.video.paused,false,'fixed media resumes on last release');});
 }
 const legacyCritical=new Set([
  'captured TMNT stale error must not poison current shared asset',
  'TMNT media interruption code 1 retains valid cached video',
  'TMNT media interruption code 2 retains valid cached video',
  'play AbortError cannot persistently exclude TMNT from either consumer',
  'all plain play rejections remain local',
  'current confirmed media error 3 still reports exact CAS identity',
  'TMNT source rejected before decode keeps the shared video eligible',
  'a late rejected old play cannot corrupt a new selection',
 ]);
 const selected=tests.filter(test=>editorCover?test.name.startsWith('Editor: '):!test.name.startsWith('Editor: ')&&(!dcPower||legacyCritical.has(test.name)||test.name.startsWith('DC: ')));
 if(dcPower)assert.equal(selected.filter(test=>legacyCritical.has(test.name)).length,legacyCritical.size,'all original critical assertions selected');
 assert.ok(selected.length,'empty selection');
 const start=new Date().toISOString();
 for(const {name,fn} of selected){try{await fn();reports.push({name,group:dcPower?'dc-power':'app-player',ok:true});console.log('PASS '+name);}catch(error){reports.push({name,group:dcPower?'dc-power':'app-player',ok:false,error:String(error),stack:error.stack});console.log('FAIL '+name+': '+error.message);if(dcPower||editorCover)break;}}
 const end=new Date().toISOString();
 const selection=dcPower||editorCover?{group:editorCover?'editor-cover':'dc-power',expected:selected.map(test=>test.name),executed:reports.map(test=>test.name),notRun:selected.slice(reports.length).map(test=>test.name),legacyCritical:[...legacyCritical],defaultGroupExecuted:false}:undefined;
 if((dcPower||editorCover)&&reports.every(test=>test.ok))assert.deepEqual(selection.executed,selection.expected,'exact directed case set');
 const summary={ok:reports.length===selected.length&&reports.every(x=>x.ok),checks,cases:reports.length,reports,selection,start,end,runtime:dcRuntime?.identity,source:{path:appPath,sha256:crypto.createHash('sha256').update(app).digest('hex')},capturedFixture:captured,scope:'production App.vue function bodies plus actual error binding; inert DOM/clock/IPC, no installed WebView or original failure causality claim'};fs.writeFileSync(out+(dcPower?'/dc-power-results.json':'/app-player-results.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify({ok:summary.ok,cases:summary.cases,checks}));process.exit(summary.ok?0:1);
}

fs.mkdirSync(out,{recursive:true});
const mocks={
 './ipc':`export class IpcRemoteError extends Error{};export const invoke=(...args)=>args[0]==='power.lifecycle'?Promise.resolve({known:true,phase:'ready',generation:1,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:0}):globalThis.__OWN_BACKGROUND_FIXTURE__.invoke(...args);export const on=()=>()=>{};`,
 './powerSource':`export const powerSourceMode={value:'ac'};`,
 './uiSettings':`export const getUiSetting=()=>false;`,
 './sharedGameMedia':bindingMock+`export const sharedGameMedia={lookup:(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.lookup(...args),ensure:(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.ensure(...args)};`,
 './steamMediaSource':`export const getDynamicBackgroundConfig=()=>({enabled:globalThis.__OWN_BACKGROUND_FIXTURE__.enabled});export const resolveSteamWallpaperIdentity=(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.identify(...args);export const cleanGameTitle=s=>s;export const normalizeGameTitle=s=>s;export const scoreSteamTitle=()=>1;export const stripLaunchModeSuffix=s=>s;export const setDynamicBackgroundEnabled=async()=>{};`
};
const result=await build({stdin:{contents:`export * from './dynamicBackground';export {onSharedPowerLifecycle} from './powerLifecycleState';`,resolveDir:project+'/src/bridge',sourcefile:'own-background-entry.ts'},bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'owner-transport-only',setup(build){build.onResolve({filter:/^\.\/(ipc|sharedGameMedia|steamMediaSource|powerSource|uiSettings)$/},args=>({path:args.path,namespace:'fixture'}));build.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'js'}));}}]});
const file=out+'/own-background-production-test.mjs';fs.writeFileSync(file,result.outputFiles[0].text);const api=await import('file:///'+file.replaceAll('\\','/'));let cases=0,checks=0;const reports=[];const same=(a,b,label)=>{checks++;assert.deepEqual(a,b,label);};
const game={pid:12345,path:'G:/Games/Fixture/game.exe',title:'Fixture',name:'Fixture',processCreated:'134372974230246308'};const cached={key:'steam-570',appId:570,name:'Fixture',ready:true,bindingRevision:4,url:'http://fixture/asset.mp4',localUrl:'https://game-media.localhost/video/asset.mp4',asset:{id:'asset',kind:'video'},playback:{start:8,end:38,fit:'cover'}};
function fixture(options={}){
 const calls=[],events=[];let prepared;
 const state={enabled:true,
  async lookup(args){calls.push({command:'lookup',args});options.beforeLookup?.();return options.lookup?options.lookup(args):options.cached?.ready===false&&prepared?prepared:options.cached??cached;},
  async ensure(target,...args){calls.push({command:'ensure',target,args});prepared=options.prepare?await options.prepare():cached;return prepared;},
  async identify(){calls.push({command:'identify'});if(options.identityFailure)throw Error('offline');return options.identity||{appId:570,name:'Fixture',executable:game.path,verified:true};},
  async invoke(command,args){calls.push({command,args});if(command==='game.detect')return typeof options.detect==='function'?options.detect(args):options.detect===undefined?game:options.detect;if(command==='gameMedia.legacy')return options.legacy||{ready:false};throw Error(command);}};
 globalThis.__OWN_BACKGROUND_FIXTURE__=state;globalThis.window={dispatchEvent(event){events.push(event);}};return {state,calls,events};
}
async function run(name,test){await test();cases++;reports.push(name);console.log('PASS '+name);}
// App.vue retains a shared power subscription while wallpaper validates the target.
globalThis.window=new EventTarget();
let acceptPower;
const ready=new Promise(resolve=>{acceptPower=resolve;});
const releasePower=api.onSharedPowerLifecycle(s=>{if(s.known&&s.phase==='ready')acceptPower();});
try {
await ready;
await run('WebView-only cached video does not require a loopback URL',async()=>{
 const control=fixture();
 same((await api.refreshDynamicBackground(game))?.state.url,cached.localUrl,'HTTP-bearing control passes the real process gate');
 same(control.calls.filter(c=>c.command==='game.detect').length,1,'control rechecks the game through the native boundary');
 console.log('CONTROL HTTP-bearing entry passes exact-game validation');
 const f=fixture({cached:{...cached,url:undefined}});
 const refreshed=await api.refreshDynamicBackground(game);
 same(refreshed?.state.url,cached.localUrl,'refresh accepts the actual local file without HTTP');
 same(f.calls.filter(c=>c.command==='game.detect').length,1,'local entry passes the same exact-process recheck');
 same(f.events.length,1,'loaded event for the local source');
 const snapshot=await api.getDynamicBackgroundState();
 same(snapshot.url,cached.localUrl,'snapshot accepts the actual local file without HTTP');
 const reads=f.calls.filter(c=>c.command==='lookup');
 same(reads.length,2,'refresh and snapshot each look up the cached target');
 same(reads.every(c=>c.args.localWallpaperDelivery===true),true,'both current-game lookups select local delivery explicitly');
});
await run('blank WebView URL does not mask a usable compatibility URL',async()=>{const f=fixture({cached:{...cached,localUrl:'  '}});const refreshed=await api.refreshDynamicBackground(game);same(refreshed?.state.url,cached.url,'refresh falls back to the nonempty compatibility URL');same((await api.getDynamicBackgroundState()).url,cached.url,'snapshot uses the same compatibility rule');same(f.events.length,1,'one actual render event');});
await run('deleted or URL-less entries are never displayed by the local route',async()=>{for(const entry of [{...cached,deleted:true,url:undefined},{...cached,url:undefined,localUrl:'  '},{...cached,url:undefined,localUrl:42}]){const f=fixture({cached:entry,prepare:()=>entry});same(await api.refreshDynamicBackground(game),null,'invalid or deleted refresh stays closed');same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'invalid or deleted snapshot stays closed');same(f.events.length,0,'no load event for unusable bytes');}});
await run('cache hits do not wait for network or missing-counterpart preparation',async()=>{let finish;const pending=new Promise(resolve=>finish=resolve);const f=fixture({prepare:()=>pending});const value=await api.refreshDynamicBackground(game);same(value.state.url,cached.localUrl,'native virtual host preferred for same physical file');same(value.state.mediaAssetId,'asset','shared asset identity retained');same(f.calls.some(c=>c.command==='identify'),false,'cache hit skips identification');same(f.calls.filter(c=>c.command==='game.detect').length,1,'exact process recheck');same(f.calls.filter(c=>c.command==='game.detect').every(c=>!c.args?.pid),true,'renderer validation never re-elects an old PID');same(value.state.playback,cached.playback,'same playback preferences');finish(cached);});
await run('a reused PID cannot reuse the old game media',async()=>{const f=fixture({detect:{...game,processCreated:'134372974230246309'}});same(await api.refreshDynamicBackground(game),null,'birth mismatch rejected');same(f.events.length,0,'no stale render event');});
await run('missing process birth never asks for cached media',async()=>{const f=fixture();await assert.rejects(()=>api.refreshDynamicBackground({...game,processCreated:''}),/进程身份/);checks++;same(f.calls.length,0,'no owner access without process identity');});
await run('complete current-game wall media skips the redundant preparation lookup',async()=>{
 const complete={...cached,staticReady:true,dynamicReady:true,asset:{...cached.asset,reducedPlayback:true}};
 for(const [entry,prepares] of [[complete,false],[{...complete,staticReady:false},true],
   [{...complete,dynamicReady:undefined},true],[{...complete,refreshRequested:true},true],
   [{...complete,currentGameWallpaperFallback:true},false]]){
  const f=fixture({cached:entry});const result=await api.refreshDynamicBackground(game);
  same(result?.state.url,cached.localUrl,'display the same cache asset');
  same(result?.state.mediaReducedPlayback,true,'keep the selected 30 FPS file');
  same(f.calls.map(c=>c.command),prepares?['lookup','ensure','game.detect']:['lookup','game.detect'],'skip only a confirmed pair or an already read-only fallback');
  same(f.events.length,1,'exactly one loaded event');
 }
});
await run('disabled own background does no media work',async()=>{const f=fixture();f.state.enabled=false;same(await api.refreshDynamicBackground(game),null,'own switch respected');same(f.calls.length,0,'disabled has no cache/network/identity IPC');same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'read state respects switch');});
await run('missing cache joins owner and retains exact original PID',async()=>{const f=fixture({cached:{key:'exe-fixture',ready:false}});const value=await api.refreshDynamicBackground(game);same(f.calls.filter(c=>c.command==='identify').length,1,'one owner identity lookup');same(f.calls.filter(c=>c.command==='ensure').length,1,'one paired preparation');same(value.state.pid,game.pid,'original target PID');same(value.state.processCreated,game.processCreated,'original target birth');same(f.calls.find(c=>c.command==='ensure').args,['ymcc',true,true],'own background joins same paired queue');});
await run('an exact current-game legacy hit is shown before complement work',async()=>{const f=fixture({cached:{key:'exe-fixture',ready:false},legacy:{...cached,url:undefined}});const value=await api.refreshDynamicBackground(game);same(value.state.mediaKey,cached.key,'migrated asset reused');same(value.state.url,cached.localUrl,'legacy local URL needs no loopback');const calls=f.calls.filter(c=>c.command==='gameMedia.legacy');same(calls.length,1,'single native guarded migration request');same(calls[0].args.localWallpaperDelivery,true,'legacy migration explicitly selects local delivery');});
await run('manual local cache stays usable when optional identity lookup is offline',async()=>{const f=fixture({cached:{...cached,appId:0},identityFailure:true});const value=await api.refreshDynamicBackground(game);same(value.state.mediaAssetId,'asset','local manual video still displayed');same(f.events.length,1,'cache output delivered despite metadata outage');});
await run('snapshot returns the same shared file for the exact current game',async()=>{fixture();const value=await api.getDynamicBackgroundState();same(value.mediaKey,cached.key,'canonical identity');same(value.url,cached.localUrl,'same native mapping');same(value.pid,game.pid,'original process identity retained');});
await run('snapshot without a current admitted game releases dynamic output',async()=>{const f=fixture({detect:null});same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'no game no output');same(f.calls.filter(c=>c.command==='lookup').length,0,'no cache lookup for absent game');});
await run('overlapping refreshes share one exact-process result and render event',async()=>{let finish;const pending=new Promise(resolve=>finish=resolve);const f=fixture({cached:{key:'exe-fixture',ready:false},prepare:()=>pending});const a=api.refreshDynamicBackground(game),b=api.refreshDynamicBackground({...game,path:'g:/games/fixture/GAME.EXE'});finish(cached);const [first,second]=await Promise.all([a,b]);same(first.state.url,second.state.url,'same completed source');same(f.calls.filter(c=>c.command==='ensure').length,1,'one full refresh flow');same(f.calls.filter(c=>c.command==='game.detect').length,1,'one post-read process check');same(f.events.length,1,'one loaded event prevents decode bursts');});
await run('a new process birth never shares an older in-flight refresh',async()=>{let finish;const pending=new Promise(resolve=>finish=resolve),fresh={...game,processCreated:'134372974230246309'};const f=fixture({cached:{key:'exe-fixture',ready:false},prepare:()=>pending,detect:fresh});const a=api.refreshDynamicBackground(game),b=api.refreshDynamicBackground(fresh);finish(cached);const [old,current]=await Promise.all([a,b]);same(old,null,'old original birth rejected');same(current.state.processCreated,'134372974230246309','new instance gets its own admission');same(f.calls.filter(c=>c.command==='ensure').length,2,'different births never share renderer validation');});
await run('an unresolved file uses the prepared confirmed AppID in output',async()=>{const f=fixture({cached:{key:'exe-fixture',ready:false},identity:{appId:0,name:'Fixture',executable:game.path,verified:false}});const value=await api.refreshDynamicBackground(game);same(f.calls.some(c=>c.command==='gameMedia.legacy'),false,'unverified file never authorizes old-cache migration');same(f.calls.find(c=>c.command==='ensure').target.appId,0,'shared queue owns verification');same(value.state.appId,570,'resolved shared entry supplies AppID');});
await run('snapshot rechecks process identity after the cache read',async()=>{let reads=0;fixture({detect:()=>++reads===1?game:{...game,processCreated:'134372974230246310'}});same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'process replacement during lookup never renders old cached output');});
await run('renderer ignores the raw asset returned by preparation',async()=>{
 let reads=0;
 const pending={key:cached.key,bindingRevision:4,ready:false,dynamicReady:true,wallpaper30Limited:true};
 const f=fixture({lookup:()=>++reads===1?{ready:false}:pending,prepare:()=>cached});
 same(await api.refreshDynamicBackground(game),null,'wait for approved asset');
 same(f.events.map(e=>[e.type,e.detail]),[['dynamic-background:waiting-for-playback',{key:cached.key,pid:game.pid,processCreated:game.processCreated}]],'retain the exact waiting binding, not a render event');
 same(f.calls.filter(c=>c.command==='lookup').map(c=>c.args.wallpaperPlayback),[true,true],'both reads request playback');
});
await run('newly published executable alias uses the authoritative no-AppID read after ensure',async()=>{
 const derived={...cached,asset:{...cached.asset,id:'verified-720p30',reducedPlayback:true}};
 let prepared=false;
 const f=fixture({identity:{appId:0,name:'Fixture',executable:game.path,verified:false},lookup:args=>{
  if(prepared&&args.currentGameWallpaper&&!( 'appId' in args))return derived;
  return {key:'exe-fixture',ready:false};
 },prepare:()=>{prepared=true;return cached;}});
 const result=await api.refreshDynamicBackground(game);
 same(result?.state.mediaAssetId,derived.asset.id,'alias selects the verified derivative, not raw ensure output');
 same(f.calls.filter(c=>c.command==='lookup').map(c=>[c.args.currentGameWallpaper,'appId' in c.args]),[[true,false],[true,false]],'both exact reads preserve alias fallback');
 same(f.calls.filter(c=>c.command==='ensure').length,1,'only the original prepare demand');
});
const failedVariant={key:cached.key,assetId:'verified-720p30',bindingRevision:cached.bindingRevision,pid:game.pid,processCreated:game.processCreated};
const variant={...cached,asset:{...cached.asset,id:failedVariant.assetId,reducedPlayback:true}};
await run('cold derivative failure resolves the exact original without download or event',async()=>{
 const f=fixture({lookup:()=>cached});const value=await api.readDynamicBackgroundOriginal(game,failedVariant);
 same(value?.url,cached.localUrl,'original local URL');same(value?.playback,cached.playback,'same wallpaper range');
 same(f.calls.map(c=>c.command),['lookup','game.detect'],'only cache read and native process check');
 same(f.calls[0].args,{key:cached.key,localWallpaperDelivery:true,wallpaperPlayback:true,wallpaperPlaybackFallback:{assetId:failedVariant.assetId,bindingRevision:cached.bindingRevision}},'uses the existing scoped native protocol');
 same(f.events,[],'only the current player owns the application of fallback');
});
await run('source fallback will not accept a different binding, a derivative, or a reused PID',async()=>{
 for(const entry of [variant,{...cached,key:'steam-other'},{...cached,bindingRevision:5},{...cached,deleted:true}]){
  const f=fixture({lookup:()=>entry});same(await api.readDynamicBackgroundOriginal(game,failedVariant),null,'not the exact playable original');
  same(f.calls.map(c=>c.command),['lookup'],'no promotion or preparation');
 }
 const replaced={...game,processCreated:'134372974230246310'};
 const f=fixture({detect:replaced});same(await api.readDynamicBackgroundOriginal(game,failedVariant),null,'old process no longer admitted');
 same(await api.readDynamicBackgroundOriginal(replaced,failedVariant),null,'new process cannot carry an old fallback ticket');same(f.calls.length,2,'old process only made one cache/valve attempt');
});
await run('same-process refresh keeps original but a new process defaults to 30 FPS',async()=>{
 const f=fixture({lookup:args=>args.wallpaperPlaybackFallback?cached:variant});
 same((await api.refreshDynamicBackground(game,failedVariant))?.state.mediaAssetId,cached.asset.id,'failed variant remains bypassed');
 const next={...game,processCreated:'134372974230246310'};
 const fresh=fixture({detect:next,lookup:args=>args.wallpaperPlaybackFallback?cached:variant});
 same((await api.refreshDynamicBackground(next,failedVariant))?.state.mediaAssetId,variant.asset.id,'new game process retries the optimized video');
 same(f.calls[0].args.wallpaperPlaybackFallback.assetId,failedVariant.assetId,'exact bypass on refresh');
 same(fresh.calls[0].args.wallpaperPlaybackFallback,undefined,'no bypass carried to successor');
});
const summary={ok:true,cases,checks,reports,scope:'production dynamicBackground.ts, gamedetect.ts and power lifecycle; synthetic IPC/media/identity plus AC and settings boundaries; verifies admission and shared-cache flow, not installed WebView rendering'};fs.writeFileSync(out+'/own-background-results.json',JSON.stringify(summary,null,2));console.log(JSON.stringify(summary));
} finally { releasePower(); }

// Bundle real source policy, process validation, revision fence and TDP store.
// Only settings storage, IPC/identity/media and visibility are boundary ports.
async function createDcPowerRuntime(directory,{withoutPowerKey=false}={}) {
 const crypto=require('node:crypto'),{pathToFileURL}=require('node:url');
 const state={enabled:true,pauseOnBattery:true,transport:null,nativeListeners:new Map(),power:{known:true,phase:'ready',generation:1,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:1}};
 globalThis.__OWN_BACKGROUND_DC__=state;
 globalThis.window=new EventTarget();
 const port='globalThis.__OWN_BACKGROUND_DC__';
 const mocks={
  './ipc':`export class IpcRemoteError extends Error{};export const invoke=async(command,...args)=>command==='power.lifecycle'?{...${port}.power}:${port}.transport.invoke(command,...args);export const on=(name,listener)=>{const listeners=${port}.nativeListeners;listeners.set(name,listener);return()=>listeners.delete(name);};`,
  './sharedGameMedia':bindingMock+`export const sharedGameMedia={lookup:(...args)=>${port}.transport.lookup(...args),ensure:(...args)=>${port}.transport.ensure(...args)};`,
  './steamMediaSource':`export const getDynamicBackgroundConfig=()=>({enabled:${port}.enabled});export const resolveSteamWallpaperIdentity=(...args)=>${port}.transport.identify(...args);export const cleanGameTitle=s=>s;export const normalizeGameTitle=s=>s;export const scoreSteamTitle=()=>1;export const stripLaunchModeSuffix=s=>s;export const setDynamicBackgroundEnabled=async()=>{};`,
  './uiSettings':`export const getUiSetting=key=>{if(key!=='steamWallpaperPauseOnBattery')throw Error('unexpected setting read: '+key);return ${port}.pauseOnBattery;};`,
  './yeman':`export const detectPowerModeReliable=()=>{throw Error('unexpected performance/hardware power probe');};`,
  './uiLifecycle':`export const isUiVisible=()=>true;export const isUiPowerReady=state=>!!state&&state.known!==false&&state.phase==='ready'&&state.notificationPending!==true;export const onUiVisibilityChange=()=>()=>{};`,
 };
 const sourcePath=project+'/src/bridge/dynamicBackground.ts';
 const input=fs.readFileSync(sourcePath,'utf8');
 const keyAnchor='JSON.stringify([acdc,sleep.generation,sleep.suspendSerial,game.pid';
 if(withoutPowerKey)assert.equal(input.split(keyAnchor).length,2,'power-key negative requires exactly one production key');
 const result=await build({absWorkingDir:project,stdin:{contents:`export {refreshDynamicBackground,getDynamicBackgroundState} from './src/bridge/dynamicBackground';export * from './src/bridge/wallpaperPowerPolicy';export {powerSourceMode,startForegroundPowerSourceRefresh} from './src/bridge/powerSource';export {onSharedPowerLifecycle,getSharedPowerLifecycle,isSharedPowerLifecycleAdmitted} from './src/bridge/powerLifecycleState';export {tdpRuntimeStore} from './src/bridge/tdpRuntimeState';export {shouldSkipVideoReconcile} from './src/bridge/backgroundPolicy';`,resolveDir:project,sourcefile:'own-background-dc-entry.ts'},bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'dc-boundary-ports',setup(builder){
  builder.onResolve({filter:/^\.\/(?:bridge\/)?(ipc|sharedGameMedia|steamMediaSource|uiSettings|yeman|uiLifecycle)$/},args=>({path:'./'+args.path.split('/').at(-1),namespace:'dc-port'}));
  builder.onLoad({filter:/.*/,namespace:'dc-port'},args=>({contents:mocks[args.path],loader:'js'}));
  if(withoutPowerKey)builder.onLoad({filter:/[\\/]dynamicBackground\.ts$/},()=>({contents:input.replace(keyAnchor,keyAnchor.replace('[acdc,','[')),loader:'ts',resolveDir:path.dirname(sourcePath)}));
 }}]});
 const file=path.resolve(directory,withoutPowerKey?'dc-power-key-negative.mjs':'dc-power-production.mjs');
 fs.writeFileSync(file,result.outputFiles[0].text);
 const api=await import(pathToFileURL(file).href);
 const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const sources=Object.keys(result.metafile.inputs).filter(file=>!file.startsWith('dc-port:')&&file!=='own-background-dc-entry.ts').map(file=>path.resolve(project,file));
 const identity={bundle:{path:file,sha256:sha(file)},sources:sources.map(file=>({path:file,sha256:sha(file)})),boundaryMocks:mocks,mutation:withoutPowerKey?'remove captured AC/DC revision from production in-flight key':null};
 let revision=0;
 function source(mode,event='power.sourceChanged'){
  // Only native receipts can change a known physical source.
  const stop=state.nativeListeners.has(event)?null:api.startForegroundPowerSourceRefresh(()=>{});
  try{
   state.nativeListeners.get(event)({known:true,ac:mode==='ac',generation:1,revision:++revision});
   assert.equal(api.powerSourceMode.value,mode,'production source accepted the physical power receipt');
   return api.captureWallpaperPower();
  }finally{stop?.();}
 }
 function reset(){state.enabled=true;state.pauseOnBattery=true;source('ac');}
 reset();return {api,state,identity,reset,source};
}

async function registerDcPowerCases({run,fixture,same,runtime,out}) {
 const {api,state}=runtime;
 const microtasks=async()=>{for(let i=0;i<32;i++)await Promise.resolve();};
 const deferred=()=>{let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};};
 const count=(f,command)=>f.calls.filter(call=>call.command===command).length;
 const game={pid:12345,path:'G:/Games/Fixture/game.exe',title:'Fixture',name:'Fixture',processCreated:'134372974230246308'};
 const cached={key:'steam-570',appId:570,name:'Fixture',ready:true,bindingRevision:4,identityVerified:true,url:'http://fixture/asset.mp4',localUrl:'https://game-media.localhost/video/asset.mp4',asset:{id:'asset',kind:'video'}};
 const missing={key:'exe-fixture',ready:false};
 const identity={appId:570,name:'Fixture',executable:game.path,verified:true};
 function mediaFixture(options={}) {
  runtime.reset();const calls=[],events=[];
  state.transport={
   lookup(args){calls.push({command:'lookup',args});return options.lookup?.(count({calls},'lookup'))??Promise.resolve(cached);},
   identify(){calls.push({command:'identify'});return options.identify?.()??Promise.resolve(identity);},
   ensure(target,...args){calls.push({command:'ensure',target,args});return options.ensure?.(count({calls},'ensure'))??Promise.resolve(cached);},
   invoke(command,args){calls.push({command,args});if(command==='game.detect')return options.detect?.()??Promise.resolve(game);if(command==='gameMedia.legacy')return options.legacy?.()??Promise.resolve(missing);throw Error('unexpected IPC '+command);},
  };
  window.dispatchEvent=event=>{events.push({type:event.type,detail:event.detail});return true;};
  return {calls,events};
 }
 const source=runtime.source;
 async function reached(f,command){await microtasks();same(count(f,command),1,'old flow reached the selected '+command+' boundary');}
 async function withMountedPower(current,work){
  // App keeps the shared power owner alive across strict process validation.
  const stop=current.api.onSharedPowerLifecycle(()=>{});
  try{
   await microtasks();
   assert.ok(current.api.isSharedPowerLifecycleAdmitted(current.api.getSharedPowerLifecycle()),'fixture has a real admitted ready receipt before lookup');
   return await work();
  }finally{stop();}
 }

 await run('DC: physical source gate ignores requested performance profile',async()=>{
  const f=fixture();const acEpoch=api.captureWallpaperPower();
  const dcProfile=api.tdpRuntimeStore.begin('dc',12);
  same(dcProfile.requestedMode,'dc','real TDP store records the selected DC profile');
  same(api.powerSourceMode.value,'ac','profile selection leaves physical source AC');
  same(f.api.allowed(),true,'DC performance profile on AC does not pause wallpaper');
  same(api.captureWallpaperPower(),acEpoch,'profile selection cannot advance wallpaper power epoch');
  source('dc');const acProfile=api.tdpRuntimeStore.begin('ac',25);
  same(acProfile.requestedMode,'ac','real TDP store records the selected AC profile');
  same(api.powerSourceMode.value,'dc','AC performance profile cannot change physical DC');
  same(f.api.allowed(),false,'physical DC closes the real App effective helper');
  same(api.wallpaperPowerReason(),'wallpaper-on-battery','DC is identified by source');
  f.api.lifecycle();await f.api.play();same(f.video.src,'','DC releases current source');same(f.calls.includes('play'),false,'no play on physical DC');
  state.pauseOnBattery=false;same(api.wallpaperPowerBlocked(),false,'opt-out preserves original DC playback policy');
  state.pauseOnBattery=true;const unknown=new api.WallpaperPowerPolicy();unknown.update(true,null);
  same(unknown.blocked,true,'opt-in unknown source fails closed');same(unknown.reason,'wallpaper-power-unknown','unknown is not an invented DC receipt');
 });

 await run('DC: native source event drives policy without another hardware reader',()=>{
  runtime.reset();let foregroundReceipts=0;
  const stop=api.startForegroundPowerSourceRefresh(()=>foregroundReceipts++);
  const changes=[];const unsubscribe=api.subscribeWallpaperPowerChanged(blocked=>changes.push(blocked));
  try{
   source('dc');source('dc');
   same(api.wallpaperPowerBlocked(),true,'actual native source binding feeds the production policy');
   const dcEpoch=api.captureWallpaperPower();source('dc','power.sourceHeartbeat');
   same(api.captureWallpaperPower(),dcEpoch,'repeated native DC does not create another epoch');
   source('ac');
   same(changes,[true,false],'production subscriber coalesces repeated source events');
   same(foregroundReceipts,0,'no foreground repair or performance read was requested');
  }finally{unsubscribe();stop();}
  same(state.nativeListeners.size,0,'owned native listeners released');
 });

 await run('DC: real suspension destroys HLS and clears retry before decoder release',async()=>{
  const f=fixture({hls:true});await f.api.setup();f.api.retry('fixture interruption');f.env.backgroundRetryUntil=12345;
  same(f.timers.size,1,'real retry owner seeded');same(f.api.hls(),f.hls[0],'real setup owns the boundary HLS instance');
  const epoch=f.api.epoch();f.calls.length=0;f.source('dc');f.api.lifecycle();
  same(f.calls,['hls:destroy','pause','remove:src','load'],'revocation destroys HLS before pause/remove/load');
  same(f.api.epoch(),epoch+1,'real invalidation advances media epoch');same(f.api.hls(),null,'HLS owner released');
  same(f.env.backgroundRetryTimer,null,'retry owner cleared');same(f.env.backgroundRetryUntil,0,'retry deadline cleared');same(f.timers.size,0,'owned timer removed');
  same(f.env.videoDesired.value,false,'pause handler sees withdrawn intent');same(f.env.backgroundVideoSuspended.value,true,'suspended before abort callbacks');
  same(f.video.paused,true,'decoder paused');same(f.video.src,'','media source removed');same(f.reports,[],'DC is not corruption');
 });

 await run('DC: repeated suspension and old timer HLS error playing cannot revive media',async()=>{
  const f=fixture({hls:true});await f.api.setup();f.api.retry('old retry');
  const lateRetry=[...f.timers.values()][0].fn,oldHls=f.hls[0];f.source('dc');f.api.lifecycle();
  for(let n=0;n<20;n++){f.source('dc');f.api.suspend();}
  lateRetry();oldHls.emit('attached');oldHls.emit('error',{fatal:true});
  for(const code of [1,2,3,4]){f.video.error={code};f.api.error({target:f.video});f.api.error({target:{error:{code}}});}
  f.api.playing({target:f.video});f.api.pause();f.api.canPlay();await f.api.play();await f.drain();
  same(f.hls.length,1,'old callbacks cannot create HLS');same(f.calls.filter(call=>call==='hls:loadSource').length,1,'stale attachment cannot reload HLS source');
  same(f.calls.includes('play'),false,'no old event plays on DC');same(f.video.src,'','source remains detached');same(f.video.paused,true,'source remains paused');
  same(f.timers.size,0,'no timer resurrection');same(f.reports,[],'old error never quarantines shared TMNT asset');
 });

 await run('DC: an already queued retry nextTick cannot attach after unplug',async()=>{
  const f=fixture({hls:true,deferTicks:true});f.api.retry('queued retry');
  const timer=[...f.timers.entries()][0];f.timers.delete(timer[0]);timer[1].fn();same(f.ticks.length,1,'actual retry reached nextTick');
  f.source('dc');f.api.lifecycle();await f.drain();
  same(f.hls.length,0,'queued retry cannot set up HLS on DC');same(f.calls.includes('play'),false,'queued retry cannot play');same(f.timers.size,0,'no replacement timer');same(f.video.src,'','queued callback leaves src removed');
 });

 await run('DC: AC resume queued before a second unplug cannot attach or play',async()=>{
  const f=fixture({hls:true,deferTicks:true});f.source('dc');f.api.lifecycle();
  f.source('ac');f.api.lifecycle();same(f.ticks.length,1,'real resume queued');f.source('dc');f.api.lifecycle();await f.drain();
  same(f.hls.length,0,'withdrawn resume cannot attach HLS');same(f.calls.includes('play'),false,'withdrawn resume cannot play');same(f.video.src,'','withdrawn source remains detached');
 });

 for(const outcome of ['resolve','reject'])await run('DC: late play '+outcome+' after unplug is fenced',async()=>{
  const f=fixture(),play=deferred();let rejectPlay;
  f.video.play=()=>{f.calls.push('play:pending');return new Promise((resolve,reject)=>{rejectPlay=reject;play.promise.then(()=>{f.video.paused=false;resolve();});});};
  const pending=f.api.play();await microtasks();same(f.calls.includes('play:pending'),true,'actual play promise is in flight');
  f.source('dc');f.api.lifecycle();if(outcome==='resolve')play.resolve();else rejectPlay(Error('old play rejection'));
  await pending;await f.drain();same(f.video.paused,true,'late promise leaves decoder paused');same(f.video.src,'','late promise cannot reattach src');same(f.timers.size,0,'late promise cannot queue recovery');same(f.reports,[],'late promise cannot quarantine cache');
 });

 await run('DC: restored AC resumes real HLS playback only with the original switch ON',async()=>{
  const f=fixture({hls:true,deferTicks:true});f.source('dc');f.api.lifecycle();f.source('ac');f.api.lifecycle();await f.drain();
  same(f.api.allowed(),true,'real effective helper reopens on AC');same(f.env.backgroundVideoSuspended.value,false,'real resume clears suspension');
  same(f.hls.length,1,'one new decoder on AC');same(f.calls.filter(call=>call==='play').length,1,'real reconcile plays current source');same(f.video.paused,false,'current decoder is playing');same(f.video.src,f.env.backgroundUrl.value,'current HLS source restored');
  f.source('dc');f.api.lifecycle();state.enabled=false;f.env.enabled=false;const before=f.calls.filter(call=>call==='play').length;
  f.source('ac');f.api.lifecycle();await f.drain();same(f.api.allowed(),false,'saved original OFF remains closed after AC');same(f.calls.filter(call=>call==='play').length,before,'AC never overrides original OFF');same(f.reports,[],'source transitions never quarantine cache');
 });

 // Observe actual calls rather than outer Promise identity: async functions
 // wrap adopted promises. The old finally runs while the new lookup is pending.
 async function epochRace(current=runtime) {
  const {api:currentApi,state:currentState,source:change}=current;
  const oldRead=deferred(),newRead=deferred();let f;
  if(currentApi===api)f=mediaFixture({lookup:n=>n===1?oldRead.promise:newRead.promise});
  else{
   current.reset();
   const calls=[],events=[];f={calls,events};
   currentState.transport={lookup(args){calls.push({command:'lookup',args});return count(f,'lookup')===1?oldRead.promise:newRead.promise;},identify(){calls.push({command:'identify'});return Promise.resolve(identity);},ensure(){calls.push({command:'ensure'});return Promise.resolve(cached);},invoke(command){calls.push({command});if(command==='game.detect')return Promise.resolve(game);if(command==='gameMedia.legacy')return Promise.resolve(missing);throw Error(command);}};
   window.dispatchEvent=event=>{events.push({type:event.type,detail:event.detail});return true;};
  }
  return withMountedPower(current,async()=>{
  const firstEpoch=currentApi.captureWallpaperPower(),old=currentApi.refreshDynamicBackground(game);
  change('dc');const blocked=await currentApi.refreshDynamicBackground(game),blockedCalls=count(f,'lookup');
  const newEpoch=change('ac'),fresh=currentApi.refreshDynamicBackground({...game,path:'g:/games/fixture/GAME.EXE'}),newCalls=count(f,'lookup');
  // Without epoch admission this image would request legacy and ensure work.
  oldRead.resolve({...cached,dynamicReady:false,asset:{id:'old-image',kind:'image'}});const oldValue=await old;await microtasks();
  const afterOld={calls:f.calls.map(call=>call.command),loaded:f.events.filter(event=>event.type==='dynamic-background:loaded').length};
  const joined=currentApi.refreshDynamicBackground(game),joinedCalls=count(f,'lookup');
  newRead.resolve(cached);const [freshValue,joinedValue]=await Promise.all([fresh,joined]);await microtasks();
  return {f,firstEpoch,newEpoch,blocked,blockedCalls,newCalls,oldValue,afterOld,joinedCalls,freshValue,joinedValue};
  });
 }
 function checkEpochRace(result) {
  const {f}=result;
  same(result.blocked,null,'physical DC admits no refresh');same(result.blockedCalls,1,'blocked request starts no lookup');
  same(result.newEpoch>result.firstEpoch,true,'AC restoration has a new power epoch');same(result.newCalls,2,'new AC lookup must not reuse old unresolved Promise');
  same(result.oldValue,null,'old lookup cannot render after AC returns');same(result.afterOld,{calls:['lookup','lookup'],loaded:0},'old reply cannot start legacy ensure identity or loaded');
  same(result.joinedCalls,2,'old finally cannot delete current epoch in-flight owner');same(result.freshValue?.state.url,cached.localUrl,'new epoch renders fresh source');same(result.joinedValue?.state.url,cached.localUrl,'current epoch duplicate joins fresh result');
  same(count(f,'ensure'),1,'one complement for current epoch');same(count(f,'gameMedia.legacy'),0,'stale image never launches legacy migration');same(f.events.filter(event=>event.type==='dynamic-background:loaded').length,1,'one current loaded event');
 }
 await run('DC: same-game AC DC AC isolates in-flight key and old finally owner',async()=>checkEpochRace(await epochRace()));

 for(const phase of ['identify','gameMedia.legacy','ensure'])await run('DC: late '+phase+' reply cannot start new legacy ensure or loaded',()=>withMountedPower(runtime,async()=>{
  const pending=deferred();let lookup=0;
  const f=mediaFixture({lookup:()=>Promise.resolve(++lookup===1?missing:cached),identify:()=>phase==='identify'?pending.promise:Promise.resolve({...identity,verified:phase!=='ensure'}),legacy:()=>pending.promise,ensure:n=>phase==='ensure'&&n===1?pending.promise:Promise.resolve(cached)});
  const old=api.refreshDynamicBackground(game);await reached(f,phase);source('dc');const before=f.calls.map(call=>call.command);
  pending.resolve(phase==='identify'?identity:cached);same(await old,null,'reply from revoked power epoch is dropped');await microtasks();
  same(f.calls.map(call=>call.command),before,'late reply cannot initiate another transport stage');same(f.events,[],'late reply cannot dispatch loaded');
  source('ac');const fresh=await api.refreshDynamicBackground(game);same(fresh?.state.url,cached.localUrl,'fresh AC request still works');same(f.events.filter(event=>event.type==='dynamic-background:loaded').length,1,'only current AC result is delivered');
 }));

 await run('DC: process-validation reply arriving after unplug cannot dispatch loaded',()=>withMountedPower(runtime,async()=>{
  const pending=deferred();const f=mediaFixture({detect:()=>pending.promise});
  const old=api.refreshDynamicBackground(game);await reached(f,'game.detect');source('dc');
  const before=f.calls.map(call=>call.command);pending.resolve(game);same(await old,null,'late exact-game validation cannot override power gate');
  same(f.calls.map(call=>call.command),before,'no new complement work after validation');same(f.events,[],'no loaded event while DC');
 }));

 await run('DC: deleting production power epoch key is rejected by the same race assertion',async()=>{
  const negative=await createDcPowerRuntime(out,{withoutPowerKey:true});
  const result=await epochRace(negative);same(result.newCalls,1,'control reproduces old-epoch Promise reuse');
  assert.throws(()=>checkEpochRace(result),/new AC lookup must not reuse old unresolved Promise/);
  runtime.identity.negative=negative.identity;globalThis.__OWN_BACKGROUND_DC__=state;
 });

 await run('DC: same-generation Ready retires a held foreground power source read',async()=>{
  runtime.reset();const priorPower=state.power,priorTransport=state.transport;
  state.power={...priorPower,suspendSerial:7};
  const old=deferred(),fresh=deferred(),reads=[],published=[];
  state.transport={invoke(command,args,options){
   assert.equal(command,'power.sourceSnapshot');reads.push({args,timeout:options.timeoutMs});
   return reads.length===1?old.promise:fresh.promise;
  }};
  const stop=api.startForegroundPowerSourceRefresh(mode=>published.push(mode));
  const send=patch=>{state.power={...state.power,...patch};state.nativeListeners.get('power.lifecycleChanged')({...state.power});};
  try{
   await microtasks();assert.equal(reads.length,1,'first public Ready requests the initial foreground snapshot');
   send({publicationRevision:2,suspendSerial:8});await microtasks();
   assert.equal(reads.length,1,'recovery waits for the existing bounded request to settle');
   old.resolve({known:true,acLine:1,generation:1,revision:100,batteryKnown:true,hasBattery:true});await microtasks();
   assert.deepEqual(published,[],'pre-suspend AC reply must not publish after coalesced Ready');
   assert.deepEqual(reads,[{args:{refresh:true},timeout:2000},{args:{refresh:true},timeout:2000}]);
   fresh.resolve({known:true,acLine:0,generation:1,revision:101,batteryKnown:true,hasBattery:true});await microtasks();
   assert.equal(api.powerSourceMode.value,'dc');assert.deepEqual(published,['dc']);
   send({publicationRevision:3,inputReady:true});send({publicationRevision:4,suspendSerial:7});await microtasks();
   assert.equal(reads.length,2,'duplicate and stale Ready do not request another sample');
  }finally{
   stop();old.resolve({});fresh.resolve({});await microtasks();state.power=priorPower;state.transport=priorTransport;
   assert.equal(state.nativeListeners.size,0,'power and AC/DC subscriptions returned');
  }
 });
}
