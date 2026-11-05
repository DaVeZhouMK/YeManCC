import fs from'node:fs';import path from'node:path';import{createRequire}from'node:module';import{strict as assert}from'node:assert';
const dcPower=process.argv.includes('--dc-power');
if(dcPower&&process.argv.includes('--app-player'))throw Error('select only one directed group');
for(let i=3;i<process.argv.length;i++){if(process.argv[i]==='--app-source'){if(!process.argv[++i])throw Error('--app-source requires a path');}else if(!['--app-player','--dc-power'].includes(process.argv[i]))throw Error('unknown selection: '+process.argv[i]);}
const project=path.resolve(import.meta.dirname,'..'),out=process.argv[2];if(!out)throw Error('output directory required');const require=createRequire(project+'/package.json'),{build}=require('esbuild');

// Extend the original consumer with the actual App.vue playback handlers. Only
// DOM/media/clock/IPC boundaries are inert; the handler bodies and template
// error binding come from the selected production source, including negatives.
if(dcPower||process.argv.includes('--app-player')){
 const ts=require('typescript'),crypto=require('node:crypto');fs.mkdirSync(out,{recursive:true});
 const at=process.argv.indexOf('--app-source'),appPath=at>=0?path.resolve(process.argv[at+1]):project+'/src/App.vue';
 const app=fs.readFileSync(appPath,'utf8'),script=app.match(/<script setup[^>]*>([\s\S]*?)<\/script>/)?.[1];assert.ok(script,'App script missing');
 const tree=ts.createSourceFile('App.ts',script,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS),names=new Set(['reconcileBackgroundVideo','onBackgroundVideoError','onBackgroundVideoMediaError','scheduleSharedBackgroundRetry','onBackgroundVideoPlaying','clearBackgroundRetry','suspendBackgroundVideo','dynamicBackgroundAllowed']);
 if(dcPower)for(const name of ['destroyHls','invalidateBackgroundMedia','isHlsUrl','isVideoPlaybackBlockedByPower','setupBackgroundVideo','shouldSuspendBackgroundVideo','reconcileBackgroundLifecycle','resumeBackgroundVideo','onBackgroundVideoPause','onBackgroundVideoCanPlay'])names.add(name);
 for(const name of names)assert.ok(tree.statements.some(n=>ts.isFunctionDeclaration(n)&&n.name?.text===name),'production App function missing: '+name);
 const bodies=tree.statements.filter(n=>ts.isFunctionDeclaration(n)&&names.has(n.name?.text)).map(n=>n.getText(tree)).join('\n');
 const binding=app.match(/@error="([^"]+)"/)?.[1];assert.ok(binding,'real video error binding missing');
 const make=new Function('env',ts.transpileModule('with(env){'+bodies+';return {error:(event)=>('+binding+')(event),play:reconcileBackgroundVideo,playing:onBackgroundVideoPlaying,clear:clearBackgroundRetry,suspend:suspendBackgroundVideo,allowed:dynamicBackgroundAllowed'+(dcPower?',setup:setupBackgroundVideo,lifecycle:reconcileBackgroundLifecycle,resume:resumeBackgroundVideo,pause:onBackgroundVideoPause,canPlay:onBackgroundVideoCanPlay,retry:scheduleSharedBackgroundRetry,epoch:()=>backgroundMediaEpoch,hls:()=>hlsInstance':'')+'};}',{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.None}}).outputText);
 const captured={key:'steam-1361510',assetId:'3b507d102803210fd2601475605a7275',bindingRevision:6,kind:'video'};
 const reports=[],tests=[];const dcRuntime=dcPower?await createDcPowerRuntime(out):null;let checks=0;const same=(a,b,m)=>{checks++;assert.deepEqual(a,b,m);};
 function fixture(options={}){let id=0;const timers=new Map(),reports=[],events=[],calls=[],ticks=[],hls=[];const ref=value=>({value});
  if(dcPower)dcRuntime.reset();
  const video={error:null,readyState:4,paused:true,ended:false,currentTime:8,src:'https://game-media.localhost/video/'+captured.assetId+'.mp4',pause(){this.paused=true;calls.push('pause');},removeAttribute(k){if(k==='src'){this.src='';calls.push('remove:src');}},load(){this.error=null;calls.push('load');},play(){this.paused=false;calls.push('play');return Promise.resolve();}};
  const env={wallpaperPowerPaused:ref(false),backgroundVideo:ref(video),backgroundKind:ref('video'),backgroundSource:ref('dynamic'),backgroundUrl:ref(video.src),backgroundVideoSuspended:ref(false),documentVisible:ref(true),nativeWindowVisible:ref(true),lowBatteryStatic:ref(false),videoDesired:ref(false),backgroundVideoGeneration:ref(1),backgroundMediaEpoch:1,backgroundRetryTimer:null,backgroundRetryUntil:0,sharedBackgroundRetryIdentity:'',sharedBackgroundRetryAttempts:0,activeDynamicMediaAsset:{...captured},backgroundFallbackUrls:ref([]),backgroundFallbackIndex:ref(0),dynamicBackgroundActive:ref(true),enabled:true,powerBlocked:false,sharedGameMedia:{playbackFailed(e){reports.push({...e});return Promise.resolve();}},getDynamicBackgroundConfig:()=>({enabled:env.enabled}),isVideoPlaybackBlockedByPower:()=>env.powerBlocked,invalidateBackgroundMedia:()=>++env.backgroundMediaEpoch,shouldSkipVideoReconcile:(a,b,playing)=>a===b&&(b?playing:!playing),isHlsUrl:()=>false,nextTick:async fn=>fn?.(),setupBackgroundVideo:async()=>{calls.push('setup');env.backgroundVideo.value.src=env.backgroundUrl.value;env.backgroundVideo.value.readyState=4;},scheduleBackgroundRetry:()=>calls.push('fixed-retry'),window:{setTimeout(fn,ms){timers.set(++id,{fn,ms});return id;},clearTimeout(n){timers.delete(n);},dispatchEvent(e){events.push(e);}},console:{warn(){}},CustomEvent:globalThis.CustomEvent};
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
  const api=make(env);return {api,env,video,timers,reports,calls,ticks,hls,source(mode){dcRuntime.api.rememberPowerSourceMode(mode);dcRuntime.api.captureWallpaperPower();},async drain(){for(let n=0;n<48;n++){for(const resolve of ticks.splice(0))resolve();await Promise.resolve();}assert.equal(ticks.length,0,'bounded nextTick drain');},async fire(){const [id,t]=[...timers][0]||[];assert.ok(t,'missing owned retry');timers.delete(id);t.fn();for(let i=0;i<8;i++)await Promise.resolve();}};
 }
 async function run(name,fn){tests.push({name,fn});}
 await run('captured TMNT stale error must not poison current shared asset',()=>{const f=fixture();f.api.error({target:{error:{code:3}}});same(f.reports,[],'detached error cannot report the current TMNT asset');same(f.env.backgroundMediaEpoch,1,'detached error cannot invalidate current decoder');});
 for(const code of [1,2])await run('TMNT media interruption code '+code+' retains valid cached video',()=>{const f=fixture();f.video.error={code};f.api.error({target:f.video});same(f.reports,[],'interruption is not shared corruption');same(f.timers.size,1,'one selected recovery timer');same([...f.timers.values()][0].ms,500,'first foreground retry is prompt and owned');same(f.video.paused,true,'interrupted decoder paused');same(f.video.src,'','interrupted decoder releases source');});
 await run('play AbortError cannot persistently exclude TMNT from either consumer',async()=>{const f=fixture();f.video.play=()=>Promise.reject(Object.assign(Error('play interrupted'),{name:'AbortError'}));await f.api.play();same(f.reports,[],'play promise rejection is not proof of bad bytes');same(f.timers.size,1,'one recovery owned by current selection');});
 await run('all plain play rejections remain local',async()=>{const f=fixture();f.video.play=()=>Promise.reject(Error('renderer lost'));await f.api.play();same(f.reports,[],'plain rejection cannot mutate shared cache');same(f.timers.size,1,'rejection waits for recovery');});
 for(const code of [3,4])await run('current confirmed media error '+code+' still reports exact CAS identity',()=>{const f=fixture();f.video.error={code};f.api.error({target:f.video});same(f.reports,[captured],'actual decode/unsupported error retains quarantine path');f.api.error({target:f.video});same(f.reports.length,1,'duplicate event never reports twice');});
 await run('error storm coalesces to one recovery',()=>{const f=fixture();for(let n=0;n<100;n++){f.video.error={code:2};f.api.error({target:f.video});}same(f.timers.size,1,'no timer per error event');same(f.reports.length,0,'storm cannot quarantine healthy bytes');});
 await run('two fast retries then a single slow retry without poisoning cache',async()=>{const f=fixture();for(const ms of [500,2000,60000,60000]){f.video.error={code:2};f.api.error({target:f.video});same(f.timers.size,1,'one retry per selected decoder');same([...f.timers.values()][0].ms,ms,'fast budget is bounded then backs off');await f.fire();}same(f.reports.length,0,'persistent interruption never globally excludes the file');});
 await run('foreground recovery reopens only selected cached video',async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});await f.fire();same(f.env.backgroundVideoGeneration.value,2,'one decoder remount');same(f.calls.filter(x=>x==='setup').length,1,'one selected source setup');same(f.calls.filter(x=>x==='play').length,1,'current source plays after recovery');same(f.reports.length,0,'no download or global invalidation');});
 for(const boundary of ['hidden','off','power','other-game','new-binding'])await run('recovery withdrawn at '+boundary,async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});if(boundary==='hidden')f.env.documentVisible.value=false;if(boundary==='off')f.env.enabled=false;if(boundary==='power')f.env.powerBlocked=true;if(boundary==='other-game'){f.env.backgroundMediaEpoch++;f.env.activeDynamicMediaAsset={...captured,key:'steam-1091500',assetId:'control'};}if(boundary==='new-binding')f.env.activeDynamicMediaAsset={...captured,bindingRevision:7};await f.fire();same(f.env.backgroundVideoGeneration.value,1,'withdrawn retry cannot mount decoder');same(f.calls.filter(x=>x==='setup').length,0,'withdrawn retry cannot attach source');same(f.reports.length,0,'withdrawal cannot mutate cache');same(f.timers.size,0,'no background heartbeat');});
 await run('suspension cancels retry and queued stale callback stays silent',async()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});const late=[...f.timers.values()][0].fn;f.api.suspend();same(f.timers.size,0,'visibility lifecycle cancels timer');late();for(let n=0;n<4;n++)await Promise.resolve();same(f.env.backgroundVideoGeneration.value,1,'cancelled callback cannot revive source');});
 await run('detached playing event cannot cancel current recovery',()=>{const f=fixture();f.video.error={code:2};f.api.error({target:f.video});f.api.playing({target:{}});same(f.timers.size,1,'old playing receipt cannot cancel current retry');f.api.playing({target:f.video});same(f.timers.size,0,'current actual playback clears retry');});
 await run('a late rejected old play cannot corrupt a new selection',async()=>{const f=fixture();let reject;f.video.play=()=>new Promise((_,r)=>reject=r);const pending=f.api.play();await Promise.resolve();f.env.backgroundMediaEpoch++;f.env.backgroundVideo.value={...f.video};f.env.activeDynamicMediaAsset={...captured,bindingRevision:7};reject(Error('late old error'));await pending;same(f.reports,[],'old promise has no write permission');same(f.timers.size,0,'old promise creates no retry');});
 if(dcPower)await registerDcPowerCases({run,fixture,same,runtime:dcRuntime,out});
 const legacyCritical=new Set([
  'captured TMNT stale error must not poison current shared asset',
  'TMNT media interruption code 1 retains valid cached video',
  'TMNT media interruption code 2 retains valid cached video',
  'play AbortError cannot persistently exclude TMNT from either consumer',
  'all plain play rejections remain local',
  'current confirmed media error 3 still reports exact CAS identity',
  'current confirmed media error 4 still reports exact CAS identity',
  'a late rejected old play cannot corrupt a new selection',
 ]);
 const selected=tests.filter(test=>!dcPower||legacyCritical.has(test.name)||test.name.startsWith('DC: '));
 if(dcPower)assert.equal(selected.filter(test=>legacyCritical.has(test.name)).length,legacyCritical.size,'all original critical assertions selected');
 assert.ok(selected.length,'empty selection');
 const start=new Date().toISOString();
 for(const {name,fn} of selected){try{await fn();reports.push({name,group:dcPower?'dc-power':'app-player',ok:true});console.log('PASS '+name);}catch(error){reports.push({name,group:dcPower?'dc-power':'app-player',ok:false,error:String(error),stack:error.stack});console.log('FAIL '+name+': '+error.message);if(dcPower)break;}}
 const end=new Date().toISOString();
 const selection=dcPower?{group:'dc-power',expected:selected.map(test=>test.name),executed:reports.map(test=>test.name),notRun:selected.slice(reports.length).map(test=>test.name),legacyCritical:[...legacyCritical],defaultGroupExecuted:false}:undefined;
 if(dcPower&&reports.every(test=>test.ok))assert.deepEqual(selection.executed,selection.expected,'exact directed case set');
 const summary={ok:reports.length===selected.length&&reports.every(x=>x.ok),checks,cases:reports.length,reports,selection,start,end,runtime:dcRuntime?.identity,source:{path:appPath,sha256:crypto.createHash('sha256').update(app).digest('hex')},capturedFixture:captured,scope:'production App.vue function bodies plus actual error binding; inert DOM/clock/IPC, no installed WebView or original failure causality claim'};fs.writeFileSync(out+(dcPower?'/dc-power-results.json':'/app-player-results.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify({ok:summary.ok,cases:summary.cases,checks}));process.exit(summary.ok?0:1);
}

fs.mkdirSync(out,{recursive:true});
const mocks={
 './ipc':`export const invoke=(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.invoke(...args);`,
 './sharedGameMedia':`export const sharedGameMedia={lookup:(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.lookup(...args),ensure:(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.ensure(...args)};`,
 './steamMediaSource':`export const getDynamicBackgroundConfig=()=>({enabled:globalThis.__OWN_BACKGROUND_FIXTURE__.enabled});export const resolveSteamWallpaperIdentity=(...args)=>globalThis.__OWN_BACKGROUND_FIXTURE__.identify(...args);export const cleanGameTitle=s=>s;export const normalizeGameTitle=s=>s;export const scoreSteamTitle=()=>1;export const stripLaunchModeSuffix=s=>s;export const setDynamicBackgroundEnabled=async()=>{};`
};
const result=await build({entryPoints:[project+'/src/bridge/dynamicBackground.ts'],bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'owner-transport-only',setup(build){build.onResolve({filter:/^\.\/(ipc|sharedGameMedia|steamMediaSource)$/},args=>({path:args.path,namespace:'fixture'}));build.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'js'}));}}]});
const file=out+'/own-background-production-test.mjs';fs.writeFileSync(file,result.outputFiles[0].text);const api=await import('file:///'+file.replaceAll('\\','/'));let cases=0,checks=0;const reports=[];const same=(a,b,label)=>{checks++;assert.deepEqual(a,b,label);};
const game={pid:12345,path:'G:/Games/Fixture/game.exe',title:'Fixture',name:'Fixture',processCreated:'134372974230246308'};const cached={key:'steam-570',appId:570,name:'Fixture',ready:true,bindingRevision:4,url:'http://fixture/asset.mp4',localUrl:'https://game-media.localhost/video/asset.mp4',asset:{id:'asset',kind:'video'},playback:{start:8,end:38,fit:'cover'}};
function fixture(options={}){const calls=[],events=[];const state={enabled:true,async lookup(args){calls.push({command:'lookup',args});options.beforeLookup?.();return options.cached??cached;},async ensure(target,...args){calls.push({command:'ensure',target,args});return options.prepare?options.prepare():cached;},async identify(){calls.push({command:'identify'});if(options.identityFailure)throw Error('offline');return options.identity||{appId:570,name:'Fixture',executable:game.path,verified:true};},async invoke(command,args){calls.push({command,args});if(command==='game.detect')return typeof options.detect==='function'?options.detect(args):options.detect===undefined?game:options.detect;if(command==='gameMedia.legacy')return options.legacy||{ready:false};throw Error(command);}};globalThis.__OWN_BACKGROUND_FIXTURE__=state;globalThis.window={dispatchEvent(event){events.push(event);}};return {state,calls,events};}
async function run(name,test){await test();cases++;reports.push(name);console.log('PASS '+name);}
await run('cache hits do not wait for network or missing-counterpart preparation',async()=>{let finish;const pending=new Promise(resolve=>finish=resolve);const f=fixture({prepare:()=>pending});const value=await api.refreshDynamicBackground(game);same(value.state.url,cached.localUrl,'native virtual host preferred for same physical file');same(value.state.mediaAssetId,'asset','shared asset identity retained');same(f.calls.some(c=>c.command==='identify'),false,'cache hit skips identification');same(f.calls.filter(c=>c.command==='game.detect').length,1,'exact process recheck');same(f.calls.filter(c=>c.command==='game.detect').every(c=>!c.args?.pid),true,'renderer validation never re-elects an old PID');same(value.state.playback,cached.playback,'same playback preferences');finish(cached);});
await run('a reused PID cannot reuse the old game media',async()=>{const f=fixture({detect:{...game,processCreated:'134372974230246309'}});same(await api.refreshDynamicBackground(game),null,'birth mismatch rejected');same(f.events.length,0,'no stale render event');});
await run('missing process birth never asks for cached media',async()=>{const f=fixture();await assert.rejects(()=>api.refreshDynamicBackground({...game,processCreated:''}),/进程身份/);checks++;same(f.calls.length,0,'no owner access without process identity');});
await run('disabled own background does no media work',async()=>{const f=fixture();f.state.enabled=false;same(await api.refreshDynamicBackground(game),null,'own switch respected');same(f.calls.length,0,'disabled has no cache/network/identity IPC');same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'read state respects switch');});
await run('missing cache joins owner and retains exact original PID',async()=>{const f=fixture({cached:{key:'exe-fixture',ready:false}});const value=await api.refreshDynamicBackground(game);same(f.calls.filter(c=>c.command==='identify').length,1,'one owner identity lookup');same(f.calls.filter(c=>c.command==='ensure').length,1,'one paired preparation');same(value.state.pid,game.pid,'original target PID');same(value.state.processCreated,game.processCreated,'original target birth');same(f.calls.find(c=>c.command==='ensure').args,['ymcc',true,true],'own background joins same paired queue');});
await run('an exact current-game legacy hit is shown before complement work',async()=>{const f=fixture({cached:{key:'exe-fixture',ready:false},legacy:cached});const value=await api.refreshDynamicBackground(game);same(value.state.mediaKey,cached.key,'migrated asset reused');same(f.calls.filter(c=>c.command==='gameMedia.legacy').length,1,'single native guarded migration request');});
await run('manual local cache stays usable when optional identity lookup is offline',async()=>{const f=fixture({cached:{...cached,appId:0},identityFailure:true});const value=await api.refreshDynamicBackground(game);same(value.state.mediaAssetId,'asset','local manual video still displayed');same(f.events.length,1,'cache output delivered despite metadata outage');});
await run('snapshot returns the same shared file for the exact current game',async()=>{fixture();const value=await api.getDynamicBackgroundState();same(value.mediaKey,cached.key,'canonical identity');same(value.url,cached.localUrl,'same native mapping');same(value.pid,game.pid,'original process identity retained');});
await run('snapshot without a current admitted game releases dynamic output',async()=>{const f=fixture({detect:null});same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'no game no output');same(f.calls.filter(c=>c.command==='lookup').length,0,'no cache lookup for absent game');});
await run('overlapping refreshes share one exact-process result and render event',async()=>{let finish;const pending=new Promise(resolve=>finish=resolve);const f=fixture({cached:{key:'exe-fixture',ready:false},prepare:()=>pending});const a=api.refreshDynamicBackground(game),b=api.refreshDynamicBackground({...game,path:'g:/games/fixture/GAME.EXE'});finish(cached);const [first,second]=await Promise.all([a,b]);same(first.state.url,second.state.url,'same completed source');same(f.calls.filter(c=>c.command==='ensure').length,1,'one full refresh flow');same(f.calls.filter(c=>c.command==='game.detect').length,1,'one post-read process check');same(f.events.length,1,'one loaded event prevents decode bursts');});
await run('a new process birth never shares an older in-flight refresh',async()=>{let finish;const pending=new Promise(resolve=>finish=resolve),fresh={...game,processCreated:'134372974230246309'};const f=fixture({cached:{key:'exe-fixture',ready:false},prepare:()=>pending,detect:fresh});const a=api.refreshDynamicBackground(game),b=api.refreshDynamicBackground(fresh);finish(cached);const [old,current]=await Promise.all([a,b]);same(old,null,'old original birth rejected');same(current.state.processCreated,'134372974230246309','new instance gets its own admission');same(f.calls.filter(c=>c.command==='ensure').length,2,'different births never share renderer validation');});
await run('an unresolved file uses the prepared confirmed AppID in output',async()=>{const f=fixture({cached:{key:'exe-fixture',ready:false},identity:{appId:0,name:'Fixture',executable:game.path,verified:false}});const value=await api.refreshDynamicBackground(game);same(f.calls.some(c=>c.command==='gameMedia.legacy'),false,'unverified file never authorizes old-cache migration');same(f.calls.find(c=>c.command==='ensure').target.appId,0,'shared queue owns verification');same(value.state.appId,570,'resolved shared entry supplies AppID');});
await run('snapshot rechecks process identity after the cache read',async()=>{let reads=0;fixture({detect:()=>++reads===1?game:{...game,processCreated:'134372974230246310'}});same(await api.getDynamicBackgroundState(),{enabled:false,url:''},'process replacement during lookup never renders old cached output');});
const summary={ok:true,cases,checks,reports,scope:'current production dynamicBackground.ts and shared gamedetect.ts; only IPC/media/identity transports replaced; verifies admission and shared-cache flow, not installed WebView rendering'};fs.writeFileSync(out+'/own-background-results.json',JSON.stringify(summary,null,2));console.log(JSON.stringify(summary));

// Bundle real source policy, process validation, revision fence and TDP store.
// Only settings storage, IPC/identity/media and visibility are boundary ports.
async function createDcPowerRuntime(directory,{withoutPowerKey=false}={}) {
 const crypto=require('node:crypto'),{pathToFileURL}=require('node:url');
 const state={enabled:true,pauseOnBattery:true,transport:null,nativeListeners:new Map()};
 globalThis.__OWN_BACKGROUND_DC__=state;
 globalThis.window=new EventTarget();
 const port='globalThis.__OWN_BACKGROUND_DC__';
 const mocks={
  './ipc':`export const invoke=(...args)=>${port}.transport.invoke(...args);export const on=(name,listener)=>{const listeners=${port}.nativeListeners;listeners.set(name,listener);return()=>listeners.delete(name);};`,
  './sharedGameMedia':`export const sharedGameMedia={lookup:(...args)=>${port}.transport.lookup(...args),ensure:(...args)=>${port}.transport.ensure(...args)};`,
  './steamMediaSource':`export const getDynamicBackgroundConfig=()=>({enabled:${port}.enabled});export const resolveSteamWallpaperIdentity=(...args)=>${port}.transport.identify(...args);export const cleanGameTitle=s=>s;export const normalizeGameTitle=s=>s;export const scoreSteamTitle=()=>1;export const stripLaunchModeSuffix=s=>s;export const setDynamicBackgroundEnabled=async()=>{};`,
  './uiSettings':`export const getUiSetting=key=>{if(key!=='steamWallpaperPauseOnBattery')throw Error('unexpected setting read: '+key);return ${port}.pauseOnBattery;};`,
  './yeman':`export const detectPowerModeReliable=()=>{throw Error('unexpected performance/hardware power probe');};`,
  './uiLifecycle':`export const isUiVisible=()=>true;export const onUiVisibilityChange=()=>()=>{};`,
 };
 const sourcePath=project+'/src/bridge/dynamicBackground.ts';
 const input=fs.readFileSync(sourcePath,'utf8');
 if(withoutPowerKey)assert.ok(input.includes('[captureWallpaperPower(),game.pid'),'power-key negative requires current production key');
 const result=await build({absWorkingDir:project,stdin:{contents:`export {refreshDynamicBackground,getDynamicBackgroundState} from './src/bridge/dynamicBackground';export * from './src/bridge/wallpaperPowerPolicy';export {rememberPowerSourceMode,powerSourceMode,startForegroundPowerSourceRefresh} from './src/bridge/powerSource';export {tdpRuntimeStore} from './src/bridge/tdpRuntimeState';export {shouldSkipVideoReconcile} from './src/bridge/backgroundPolicy';`,resolveDir:project,sourcefile:'own-background-dc-entry.ts'},bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'dc-boundary-ports',setup(builder){
  builder.onResolve({filter:/^\.\/(?:bridge\/)?(ipc|sharedGameMedia|steamMediaSource|uiSettings|yeman|uiLifecycle)$/},args=>({path:'./'+args.path.split('/').at(-1),namespace:'dc-port'}));
  builder.onLoad({filter:/.*/,namespace:'dc-port'},args=>({contents:mocks[args.path],loader:'js'}));
  if(withoutPowerKey)builder.onLoad({filter:/[\\/]dynamicBackground\.ts$/},()=>({contents:input.replace('[captureWallpaperPower(),game.pid','[game.pid'),loader:'ts',resolveDir:path.dirname(sourcePath)}));
 }}]});
 const file=path.resolve(directory,withoutPowerKey?'dc-power-key-negative.mjs':'dc-power-production.mjs');
 fs.writeFileSync(file,result.outputFiles[0].text);
 const api=await import(pathToFileURL(file).href);
 const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const sources=Object.keys(result.metafile.inputs).filter(file=>!file.startsWith('dc-port:')&&file!=='own-background-dc-entry.ts').map(file=>path.resolve(project,file));
 const identity={bundle:{path:file,sha256:sha(file)},sources:sources.map(file=>({path:file,sha256:sha(file)})),boundaryMocks:mocks,mutation:withoutPowerKey?'remove captureWallpaperPower() from production in-flight key':null};
 function reset(){state.enabled=true;state.pauseOnBattery=true;api.rememberPowerSourceMode('ac');api.captureWallpaperPower();}
 reset();return {api,state,identity,reset};
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
 function source(mode){api.rememberPowerSourceMode(mode);return api.captureWallpaperPower();}
 async function reached(f,command){await microtasks();same(count(f,command),1,'old flow reached the selected '+command+' boundary');}

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
   state.nativeListeners.get('power.sourceChanged')({ac:false});state.nativeListeners.get('power.sourceChanged')({ac:false});
   same(api.wallpaperPowerBlocked(),true,'actual native source binding feeds the production policy');
   const dcEpoch=api.captureWallpaperPower();state.nativeListeners.get('power.sourceHeartbeat')({ac:false});
   same(api.captureWallpaperPower(),dcEpoch,'repeated native DC does not create another epoch');
   state.nativeListeners.get('power.sourceChanged')({ac:true});
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
 async function epochRace(currentApi=api,currentState=state) {
  const oldRead=deferred(),newRead=deferred();let f;
  if(currentApi===api)f=mediaFixture({lookup:n=>n===1?oldRead.promise:newRead.promise});
  else{
   currentState.enabled=true;currentState.pauseOnBattery=true;currentApi.rememberPowerSourceMode('ac');currentApi.captureWallpaperPower();
   const calls=[],events=[];f={calls,events};
   currentState.transport={lookup(args){calls.push({command:'lookup',args});return count(f,'lookup')===1?oldRead.promise:newRead.promise;},identify(){calls.push({command:'identify'});return Promise.resolve(identity);},ensure(){calls.push({command:'ensure'});return Promise.resolve(cached);},invoke(command){calls.push({command});if(command==='game.detect')return Promise.resolve(game);if(command==='gameMedia.legacy')return Promise.resolve(missing);throw Error(command);}};
   window.dispatchEvent=event=>{events.push({type:event.type,detail:event.detail});return true;};
  }
  const change=mode=>{currentApi.rememberPowerSourceMode(mode);return currentApi.captureWallpaperPower();};
  const firstEpoch=currentApi.captureWallpaperPower(),old=currentApi.refreshDynamicBackground(game);
  change('dc');const blocked=await currentApi.refreshDynamicBackground(game),blockedCalls=count(f,'lookup');
  const newEpoch=change('ac'),fresh=currentApi.refreshDynamicBackground({...game,path:'g:/games/fixture/GAME.EXE'}),newCalls=count(f,'lookup');
  // Without epoch admission this image would request legacy and ensure work.
  oldRead.resolve({...cached,dynamicReady:false,asset:{id:'old-image',kind:'image'}});const oldValue=await old;await microtasks();
  const afterOld={calls:f.calls.map(call=>call.command),loaded:f.events.filter(event=>event.type==='dynamic-background:loaded').length};
  const joined=currentApi.refreshDynamicBackground(game),joinedCalls=count(f,'lookup');
  newRead.resolve(cached);const [freshValue,joinedValue]=await Promise.all([fresh,joined]);await microtasks();
  return {f,firstEpoch,newEpoch,blocked,blockedCalls,newCalls,oldValue,afterOld,joinedCalls,freshValue,joinedValue};
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

 for(const phase of ['identify','gameMedia.legacy','ensure'])await run('DC: late '+phase+' reply cannot start new legacy ensure or loaded',async()=>{
  const pending=deferred();let lookup=0;
  const f=mediaFixture({lookup:()=>Promise.resolve(++lookup===1?missing:cached),identify:()=>phase==='identify'?pending.promise:Promise.resolve({...identity,verified:phase!=='ensure'}),legacy:()=>pending.promise,ensure:n=>phase==='ensure'&&n===1?pending.promise:Promise.resolve(cached)});
  const old=api.refreshDynamicBackground(game);await reached(f,phase);source('dc');const before=f.calls.map(call=>call.command);
  pending.resolve(phase==='identify'?identity:cached);same(await old,null,'reply from revoked power epoch is dropped');await microtasks();
  same(f.calls.map(call=>call.command),before,'late reply cannot initiate another transport stage');same(f.events,[],'late reply cannot dispatch loaded');
  source('ac');const fresh=await api.refreshDynamicBackground(game);same(fresh?.state.url,cached.localUrl,'fresh AC request still works');same(f.events.filter(event=>event.type==='dynamic-background:loaded').length,1,'only current AC result is delivered');
 });

 await run('DC: process-validation reply arriving after unplug cannot dispatch loaded',async()=>{
  const pending=deferred();const f=mediaFixture({detect:()=>pending.promise});
  const old=api.refreshDynamicBackground(game);await reached(f,'game.detect');source('dc');
  const before=f.calls.map(call=>call.command);pending.resolve(game);same(await old,null,'late exact-game validation cannot override power gate');
  same(f.calls.map(call=>call.command),before,'no new complement work after validation');same(f.events,[],'no loaded event while DC');
 });

 await run('DC: deleting production power epoch key is rejected by the same race assertion',async()=>{
  const negative=await createDcPowerRuntime(out,{withoutPowerKey:true});
  const result=await epochRace(negative.api,negative.state);same(result.newCalls,1,'control reproduces old-epoch Promise reuse');
  assert.throws(()=>checkEpochRace(result),/new AC lookup must not reuse old unresolved Promise/);
  runtime.identity.negative=negative.identity;globalThis.__OWN_BACKGROUND_DC__=state;
 });
}
