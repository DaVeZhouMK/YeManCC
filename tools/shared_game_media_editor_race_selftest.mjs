import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';


const automaticFallbackScope=process.env.YMCC_EDITOR_AUTOMATIC_FALLBACK_SCOPE||'all';
if(!['all','lost-receipt'].includes(automaticFallbackScope))throw Error('unknown automatic fallback check scope');
if(automaticFallbackScope==='lost-receipt'&&(process.env.YMCC_EDITOR_CASE_FILTER!=="^editor preview a current image still reaches the legitimate automatic fallback$"||process.argv[3]||process.argv[4]))throw Error('lost receipt requires its exact original case and production sources');

const out=process.argv[2];if(!out)throw Error('output directory required');fs.mkdirSync(out,{recursive:true});

const require=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');const {chromium}=require('playwright');

const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:1440,height:1100},reducedMotion:'no-preference'});page.setDefaultTimeout(5000);
page.on('console',event=>{if(event.type()==='debug'&&event.text().startsWith('BUSY_BOUNDARY '))console.log(event.text());});

let sourceOverridesUsed=0,cssOverridesUsed=0;const sourceOverride=process.argv[3],cssOverride=process.argv[4];

const repo=path.resolve(process.env.YMCC_EDITOR_REPOSITORY||path.resolve(import.meta.dirname,'..')),ui=path.join(repo,'CustomSteamLibrary/workspace-ui');

const samples='G:/YeManCC-Work/Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/preview';

const fixtureSource=fs.readFileSync(path.join(samples,'library-fixture.js'),'utf8');

if(!fixtureSource.includes('const listeners=new Set();'))throw Error('fixture listener anchor missing');

const fixture=fixtureSource.replace('const listeners=new Set();','const listeners=new Set();window.reverseEmit=message=>{for(const listener of listeners)listener({data:message});};')+"\n(()=>{const bridge=window.chrome.webview,post=bridge.postMessage;bridge.postMessage=function(message){if(message.command==='startup')window.__startupRefreshArguments=structuredClone(message.arguments||{});return post.call(bridge,message);};})();";
const shutdownClient={pid:4242,birth:'133900719925474099',powerGeneration:'1',powerSequence:'0',powerSerial:'0'};
await page.addInitScript(client=>{window.__libraryShutdownClient=client;},shutdownClient);

await page.route('http://127.0.0.1:13139/**',route=>{

 const name=path.basename(new URL(route.request().url()).pathname)||'index.html';

 if(['index.html','library','app.js','styles.css'].includes(name)){

  const file=name==='library'?'index.html':name;let body=fs.readFileSync(file==='app.js'&&sourceOverride?sourceOverride:file==='styles.css'&&cssOverride?cssOverride:path.join(ui,file));

  if(file==='app.js'&&sourceOverride)sourceOverridesUsed++;

  if(file==='styles.css'&&cssOverride)cssOverridesUsed++;

  if(file==='index.html')body=Buffer.from(body.toString().replace('</head>','<script>'+fixture+'</script></head>'));

  return route.fulfill({contentType:file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'text/html; charset=utf-8',body});

 }

 if(['cover.jpg','header.jpg','wallpaper.jpg','sample.mp4'].includes(name))return route.fulfill({contentType:name.endsWith('.mp4')?'video/mp4':'image/jpeg',body:fs.readFileSync(path.join(samples,name))});

 return route.fulfill({status:404,body:''});

});

const cases=[],errors=[],performanceMetrics=[],saveMetrics=[],progressMetrics=[];let checks=0;page.on('pageerror',e=>errors.push(e.message));

const normalizedForTest=value=>String(value||'').replaceAll('/','\\').toLowerCase();

const check=(ok,label)=>{checks++;if(!ok)throw Error(label);};

async function reset(){await page.goto('http://127.0.0.1:13139/library/');await page.waitForFunction(()=>selectedActionGame?.gameMedia?.key==='steam-570');await page.evaluate(()=>{

 window.fixtureSteamCommitResult=(state,keys)=>{
  const result=structuredClone(state);
  const plan={...result.steamPlan,mode:'steam-library-add-commit',schemaVersion:1,committed:true,accountId:'fixture'};
  for(const item of plan.items)if(keys.includes(normalizedUiPath(item.gameDirectory)))item.status='added-to-steam';
  result.steamPlan={...plan,mode:'steam-library-add-commit-all-accounts',allSteamAccounts:true,accountResults:[plan]};
  result.steamPlanSource='verified-steam-add-completion';return result;
 };
 const base=invoke;window.reverseIO={calls:[],finishPick:null};invoke=async(command,args={},timeoutMs=0)=>{

  reverseIO.calls.push({command,args:structuredClone(args)});

  if(command==='pickVideo'||command==='pickArtwork')return new Promise((resolve,reject)=>{reverseIO.finishPick=resolve;reverseIO.failPick=reject;});

  if(command==='mediaEdit'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];if(entry&&args.bindingRevision!==entry.bindingRevision)throw Error('media-conflict');}

  if(command==='mediaImport'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];if(!entry)throw Error('missing-target');if(args.bindingRevision!==entry.bindingRevision)throw Error('media-conflict');const slot=args.slot;if(!['static','dynamic'].includes(slot))throw Error('invalid-import-slot');entry.bindingRevision++;const asset={...entry.assets?.[slot],id:'imported-'+entry.bindingRevision,revision:String(entry.bindingRevision),kind:slot==='dynamic'?'video':'image'};entry.assets={...entry.assets,[slot]:asset};entry.manualSlots={...entry.manualSlots,[slot]:true};entry.deletedSlots={...entry.deletedSlots,[slot]:false};entry[slot+'Ready']=true;entry[slot+'PreviewUrl']=(slot==='dynamic'?'/sample.mp4':'/wallpaper.jpg')+'?imported='+entry.bindingRevision;entry.asset=entry.assets[entry.wallpaperMode];entry.previewUrl=entry[entry.wallpaperMode+'PreviewUrl'];return structuredClone(entry);}

  return base(command,args,timeoutMs);

 };

 window.reverseChoose=(start,length=30)=>{const skip=document.getElementById('shared-wallpaper-skip'),duration=document.getElementById('shared-wallpaper-length');if(![...skip.options].some(o=>o.value===String(start)))skip.add(new Option(start+' 秒',String(start)));skip.value=String(start);duration.value=String(length);skip.dispatchEvent(new Event('change'));};

 window.reverseOpenOther=()=>{
  const appId=1245620,key='steam-'+appId,find=()=>mergedGames().find(g=>!g.mediaOnly&&g.gameMedia?.key===key);
  let game=find();
  if(!game){
   // A second card requires an inventory row, not just a cached wallpaper.
   const state=__LIBRARY_H5_FIXTURE__.snapshot,gameDirectory='G:/H5Fixture/SecondInstalled';
   const row={gameDirectory,primaryExecutable:gameDirectory+'/Game.exe',directoryName:'Second installed fixture',status:'ready',contentType:'game'};
   const plan={...row,appId,steamStoreAppId:appId,status:'ready-to-add',identityStatus:'verified',steamVerificationStatus:'steam-verified'};
   state.library.games.push(row);state.steamPlan.items.push(plan);
   snapshot={...snapshot,library:{...snapshot.library,games:[...snapshot.library.games,row]},steamPlan:{...snapshot.steamPlan,items:[...snapshot.steamPlan.items,plan]}};
   game=find();
  }
  if(!game)throw Error('second-fixture-game-missing');
  openEdit(game);
 };



 window.reverseHoldSearch=()=>{

  const actual=invoke;window.searchIO={identities:[],artworks:[],warmCallbacks:[],warmCancelled:[],warmSources:[]};

  invoke=async(command,args={},timeoutMs=0)=>{

   if(command==='searchIdentity'||command==='searchArtwork')return new Promise((resolve,reject)=>searchIO[command==='searchIdentity'?'identities':'artworks'].push({args:structuredClone(args),resolve,reject}));

   return actual(command,args,timeoutMs);

  };

  window.reverseSearch=(query='Owned search',steamId='')=>{document.getElementById('identity-name').value=query;document.getElementById('identity-steam-id').value=steamId;return searchGame();};

  window.reverseSearchAck=(index,jobId,generation=index+1)=>searchIO.identities[index].resolve({pending:true,jobId,generation,candidates:[]});

  window.reverseIdentityEvent=(event,jobId,generation,query='Owned search',candidates=[])=>reverseEmit({event,jobId,generation,query,executable:searchIO.identities.at(-1).args.executable,candidates});

  window.reverseWatchWarmups=()=>{

   window.requestIdleCallback=callback=>{searchIO.warmCallbacks.push(callback);return searchIO.warmCallbacks.length;};

   window.cancelIdleCallback=id=>searchIO.warmCancelled.push(id);

   const RealImage=Image;window.Image=function(){const image=new RealImage();Object.defineProperty(image,'src',{configurable:true,set:value=>searchIO.warmSources.push(value),get:()=>''});return image;};

  };

 };

 window.reverseLargeLibrary=count=>{

  closeEdit();const games=[],plans=[],entries={},items={};

  for(let i=0;i<count;i++){

   const directory='G:/Fixture/Progress'+String(i).padStart(4,'0'),executable=directory+'/game.exe',appId=200000+i,name='Cached '+String(i).padStart(4,'0');

   games.push({gameDirectory:directory,primaryExecutable:executable,directoryName:name,status:'ready'});

   plans.push({gameDirectory:directory,primaryExecutable:executable,appId,steamStoreAppId:appId,status:'ready-to-add',identityStatus:'verified',steamVerificationStatus:'steam-verified'});

   entries['steam-'+appId]={key:'steam-'+appId,appId,executable,name,staticReady:true,dynamicReady:false,wallpaperMode:'static',staticPreviewUrl:'/wallpaper.jpg',previewUrl:'/wallpaper.jpg',bindingRevision:1,assets:{static:{id:'cached-'+appId,revision:'1',kind:'image',extension:'jpg',mime:'image/jpeg'}}};

   items[executable.toUpperCase().replaceAll('/','\\')+'\\']={status:'queued'};

  }

  const state=__LIBRARY_H5_FIXTURE__.snapshot;Object.assign(state,{dataRoot:'G:/Fixture',config:{},manualOverrides:{items:{}},resolvedIdentities:{},library:{games},steamPlan:{items:plans},gameMedia:{entries},scrape:{mode:'automatic',items},mediaDownloads:{enabled:false,total:count,remaining:0,completed:count,phase:'completed'}});

  snapshot=structuredClone(state);selectedGameDirectories=null;activeTab='not-in-steam';steamCommitTasks.clear();renderLibrary();

 };



 });}

let testClockInstalled=false;

async function installTestClock(){if(!testClockInstalled){await page.clock.install({time:Date.now()});testClockInstalled=true;}await page.clock.resume();}

// Hold production bridge receipts while modeling the native EXE/type replacement rule.

async function holdNativeArtwork(){await page.evaluate(()=>{

 const actual=chrome.webview.postMessage.bind(chrome.webview);

 const state=window.artworkNativeIO={jobs:[],writes:[],cancels:0};

 const scope=args=>(normalizedUiPath(args.executable)||'query:'+String(args.query||''))+'|'+String(args.type||'cover');

 const reply=(item,result,error)=>{if(item.replied)throw Error('fixture duplicate reply');item.replied=true;reverseEmit({kind:'response',id:item.message.id,ok:!error,...(error?{error}:{result})});};

 state.reply=(index,result)=>{const job=state.jobs[index];reply(job,result||(job.message.command==='downloadArtworkCandidate'?{path:'/cover.jpg',width:600,height:900}:{query:job.message.arguments.query,candidates:[]}),job.cancelled?'素材任务已取消或被新请求替代':null);};

 state.replyWrite=index=>reply(state.writes[index],{saved:true});

 chrome.webview.postMessage=message=>{

  if(['searchArtwork','downloadArtworkCandidate','mediaSearchVideos'].includes(message.command)){

   const key=scope(message.arguments||{});for(const job of state.jobs)if(!job.replied&&job.scope===key)job.cancelled=true;

   state.jobs.push({message:structuredClone(message),scope:key,cancelled:false,replied:false});return;

  }

  if(message.command==='cancelArtwork'){

   state.cancels++;for(const job of state.jobs)if(!job.replied)job.cancelled=true;

   queueMicrotask(()=>reverseEmit({kind:'response',id:message.id,ok:true,result:{cancelled:true}}));return;

  }

  if(message.command==='saveManualName'){state.writes.push({message:structuredClone(message),replied:false});return;}

  actual(message);

 };

 artworkPrefetchCache.clear();

});}

// Model ordinary write/state receipt FIFO while detached snapshot events use the real listener.

async function holdNativeSnapshotLane(){

 await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));__LIBRARY_H5_FIXTURE__.downloads('complete');});

 await page.waitForFunction(()=>!refreshAfterScrapeTimer&&!silentRefreshInFlight);

 await page.evaluate(()=>{

 const actual=chrome.webview.postMessage.bind(chrome.webview),render=renderLibrary,entry=renderEntry;

 const lane=window.snapshotIO={reads:[],writes:[],order:[],libraryPaints:0,entryPaints:0};

 renderLibrary=(...args)=>{lane.libraryPaints++;return render(...args);};renderEntry=(...args)=>{lane.entryPaints++;return entry(...args);};

 const assertOrder=item=>{if([...lane.reads,...lane.writes].some(row=>row.message.id<item.message.id&&!row.replied))throw Error('fixture violated native foreground FIFO');};

 const respond=(item,result,error)=>{assertOrder(item);if(item.replied)throw Error('fixture duplicate receipt');item.replied=true;lane.order.push(item.message.command);reverseEmit({kind:'response',id:item.message.id,ok:!error,...(error?{error}:{result})});};

 lane.replyWrite=index=>{

  const item=lane.writes[index],state=__LIBRARY_H5_FIXTURE__.snapshot,args=item.message.arguments;assertOrder(item);

  if(item.message.command==='saveEditor')for(const op of args.operations){

   if(op.command==='saveManualName'){const key=normalizedUiPath(op.arguments.executable);state.manualOverrides.items[key]={...(state.manualOverrides.items[key]||{}),name:op.arguments.name};}

   else if(op.command==='setBucket'){state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}

   else throw Error('unmodeled fixture write '+op.command);

  }

  else for(const plan of state.steamPlan.items)if((args.selectedGameDirectories||[]).includes(normalizedUiPath(plan.gameDirectory)))plan.status='added-to-steam';

  respond(item,item.message.command==='commit'?fixtureSteamCommitResult(state,args.selectedGameDirectories):structuredClone(state));

 };

 lane.replyState=index=>{const current=silentRefreshInFlight;respond(lane.reads[index],structuredClone(__LIBRARY_H5_FIXTURE__.snapshot));return current||Promise.resolve();};

 lane.failState=index=>{const current=silentRefreshInFlight;respond(lane.reads[index],null,'fixture local read failed');return current||Promise.resolve();};

 lane.resetPaint=()=>{lane.libraryPaints=0;lane.entryPaints=0;};

 chrome.webview.postMessage=message=>{

  if(message.command==='state'){lane.reads.push({message:structuredClone(message),replied:false});return;}

  if(message.command==='saveEditor'||message.command==='commit'){lane.writes.push({message:structuredClone(message),replied:false});return;}

  actual(message);

 };

 document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));

});}

async function settle(){await page.waitForFunction(()=>!sharedWallpaperBusy&&(!sharedWallpaperPendingPlayback||sharedWallpaperPendingPlayback.size===0));}

async function beginDownloadPaintProbe(overrides={}){

 await page.evaluate(overrides=>{

  document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));

  const revision=Number(lastMediaDownloadDisplay?.statusRevision||snapshot.mediaDownloads?.statusRevision||0)+100;

  const completed=Number(lastMediaDownloadDisplay?.completed||0);

  const initial={enabled:true,total:completed+80,completed,remaining:80,phase:'downloading',activeCount:1,workerPid:12345,bytes:100,totalBytes:10000,statusRevision:revision,current:{name:'Owned download'},manualUntil:0,...overrides};

  renderMediaDownloads(initial);

  const wrap=document.getElementById('media-download-progress');let mutations=0;

  const observer=new MutationObserver(records=>{mutations+=records.length;});observer.observe(wrap,{childList:true,subtree:true,characterData:true,attributes:true});

  window.downloadPaintProbe={initial:structuredClone(initial),nextRevision:revision+1,observer,

   emit(patch={}){const state={...initial,statusRevision:this.nextRevision++,...patch};reverseEmit({kind:'event',event:'media-download-progress',mediaDownloads:state});return state;},

   take(){mutations+=observer.takeRecords().length;const bar=document.getElementById('media-download-bar');return {mutations,label:document.getElementById('media-download-label').textContent,button:document.getElementById('media-download-now').textContent,hasValue:bar.hasAttribute('value'),value:bar.value,maximum:bar.max,revision:snapshot.mediaDownloads?.statusRevision,completed:snapshot.mediaDownloads?.completed,current:snapshot.mediaDownloads?.current?.name,recoveryTimer:mediaRecoveryTimer,reads:window.snapshotIO?.reads.length||0};}

  };

 },overrides);

}

async function beginEditorPreviewProbe(){

 await page.waitForFunction(()=>document.querySelector('#artwork-wallpaper-preview video')?.readyState>=1);await page.waitForTimeout(60);

 await page.evaluate(()=>{

  let mutations=0;const observer=new MutationObserver(rows=>mutations+=rows.length);

  for(const type of ['cover','long','wallpaper'])observer.observe(document.getElementById('artwork-'+type+'-preview'),{subtree:true,childList:true,attributes:true,characterData:true});

  window.editorPreviewProbe={observer,video:document.querySelector('#artwork-wallpaper-preview video'),cover:document.querySelector('#artwork-cover-preview img'),long:document.querySelector('#artwork-long-preview img'),reset(){mutations=0;observer.takeRecords();this.wallpaperImage=document.querySelector('#artwork-wallpaper-preview img');},take(){mutations+=observer.takeRecords().length;return {mutations,sameWallpaperImage:this.wallpaperImage===document.querySelector('#artwork-wallpaper-preview img'),sameVideo:this.video===document.querySelector('#artwork-wallpaper-preview video'),sameCover:this.cover===document.querySelector('#artwork-cover-preview img'),mode:selectedActionGame?.gameMedia?.wallpaperMode,revision:selectedActionGame?.gameMedia?.bindingRevision};}};

 });

}

async function beginNativeArtworkProbe(){

 await page.evaluate(()=>{

  const original=selectedActionGame,key=keyFor(original),exe=normalizedUiPath(original.primaryExecutable);closeEdit();

  for(const state of [snapshot,__LIBRARY_H5_FIXTURE__.snapshot]){

   for(const game of state.library.games)if(normalizedUiPath(game.primaryExecutable)===exe){game.steamNative=true;game.contentType='steam-native';}

   for(const item of state.steamPlan.items)if(normalizedUiPath(item.primaryExecutable)===exe){item.steamNative=true;item.status='steam-native';item.nativeSteamAppId=570;}

  }

  const actual=invoke;window.nativeArtworkProbe={key,exe,requests:[],metadata:0};

  invoke=(command,...rest)=>{

   if(command==='refreshNativeArtwork')return new Promise(resolve=>nativeArtworkProbe.requests.push({args:structuredClone(rest[0]),response:structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),resolve}));

   if(command==='mediaCreate'){nativeArtworkProbe.metadata++;return Promise.resolve(actual(command,...rest)).finally(()=>nativeArtworkProbe.metadata--);}

   return actual(command,...rest);

  };

  nativeArtworkProbe.open=()=>openEdit(mergedGames().find(game=>keyFor(game)===key));

  nativeArtworkProbe.reply=(index,marker)=>{const row=nativeArtworkProbe.requests[index],value=structuredClone(row.response),plan=value.steamPlan.items.find(item=>normalizedUiPath(item.primaryExecutable)===exe);plan.artworkPreview={tall:{url:'http://127.0.0.1:13139/cover.jpg?'+marker,provider:'steam-store'},long:{url:'http://127.0.0.1:13139/header.jpg?'+marker,provider:'steam-store'},hero:{url:'http://127.0.0.1:13139/wallpaper.jpg?'+marker,provider:'steam-store'}};row.resolve(value);};

  nativeArtworkProbe.open();

 });await page.waitForFunction(()=>nativeArtworkProbe.requests.length===1&&nativeArtworkProbe.metadata===0);await page.waitForTimeout(40);

}

async function installPreviewFallbackProbe(fail=false){

 await page.evaluate(fail=>{

  artworkPrefetchCache.clear();artworkUnavailableUrls.clear();const actual=invoke;window.previewFallbackProbe={searches:0,downloads:0,reject:null};

  invoke=(command,...rest)=>{

   if(command==='searchArtwork'){previewFallbackProbe.searches++;if(fail)return new Promise((resolve,reject)=>previewFallbackProbe.reject=reject);return Promise.resolve({candidates:[{provider:'playnite-igdb',url:'http://127.0.0.1:13139/cover.jpg?owned-fallback'}]});}

   if(command==='downloadArtworkCandidate'){previewFallbackProbe.downloads++;return Promise.resolve({path:'G:/Fixture/owned-fallback.jpg',width:600,height:900});}

   return actual(command,...rest);

  };

 },fail);

}


async function beginStaticSourceProbe(){
 await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));__LIBRARY_H5_FIXTURE__.downloads('complete');const state=__LIBRARY_H5_FIXTURE__.snapshot,entry=state.gameMedia.entries['steam-570'],exe=normalizedUiPath(entry.executable);entry.wallpaperMode='static';entry.asset=entry.assets.static;entry.previewUrl=entry.staticPreviewUrl;state.manualOverrides.items[exe]={...(state.manualOverrides.items[exe]||{}),wallpaper:{file:'G:/H5Fixture/legacy-static.jpg',url:'/wallpaper.jpg?legacy=old'},artworkProtection:{wallpaper:'manual'}};snapshot=structuredClone(state);window.staticSourceProbe={originalLibrary:structuredClone(state.library),originalPlan:structuredClone(state.steamPlan),exe};closeEdit();openEdit(mergedGames().find(g=>g.primaryExecutable&&normalizedUiPath(g.primaryExecutable)===exe));});
 await page.click('[data-shared-wallpaper-mode="static"]');await settle();
}
async function hideStaticLibraryCard(){
 const row=await page.evaluate(()=>{
  const state=__LIBRARY_H5_FIXTURE__.snapshot;state.library.games=[];state.steamPlan.items=[];
  snapshot=structuredClone(state);closeEdit();renderLibrary();
  return {models:mergedGames().length,cards:libraryCards().length,cached:!!snapshot.gameMedia.entries['steam-570']};
 });
 check(row.models===0&&row.cards===0&&row.cached,'unmatched media created a card or was dropped');
}
async function restoreStaticLibraryCard(){
 await page.evaluate(()=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;state.library=structuredClone(staticSourceProbe.originalLibrary);state.steamPlan=structuredClone(staticSourceProbe.originalPlan);reverseEmit({event:'library-scan-complete',refreshRequested:true,refreshProtocol:1});});
 await page.waitForFunction(()=>mergedGames().some(g=>g.primaryExecutable&&normalizedUiPath(g.primaryExecutable)===staticSourceProbe.exe));
 await page.evaluate(()=>{closeEdit();openEdit(mergedGames().find(g=>g.primaryExecutable&&normalizedUiPath(g.primaryExecutable)===staticSourceProbe.exe));});await page.waitForTimeout(25);
}

const selectedCasePattern=process.env.YMCC_EDITOR_CASE_FILTER?new RegExp(process.env.YMCC_EDITOR_CASE_FILTER):null;

async function run(name,fn){if(selectedCasePattern&&!selectedCasePattern.test(name))return;await reset();try{await fn();cases.push({name,ok:true});}catch(error){cases.push({name,ok:false,error:error.message});}}

try{

 async function beginDraftVideoProbe(id='620'){
  await page.evaluate(id=>{
   const actual=invoke,state=__LIBRARY_H5_FIXTURE__.snapshot;
   window.draftVideoProbe={calls:[],saved:JSON.stringify(state.manualOverrides),oldMedia:JSON.stringify(state.gameMedia.entries['steam-570'])};
   invoke=async(command,args={},timeout=0)=>{
    draftVideoProbe.calls.push({command,args:structuredClone(args)});
    if(command==='mediaCreate'&&args.appId&&args.appId!==570){
     const key='steam-'+args.appId;
     state.gameMedia.entries[key]||={key,appId:args.appId,name:args.name,executable:'',identityVerified:true,bindingRevision:1,wallpaperMode:'static',assets:{},manualSlots:{},deletedSlots:{},playback:{start:8,end:38,fit:'cover'}};
     return structuredClone(state.gameMedia.entries[key]);
    }
    const response=await actual(command,args,timeout);
    if(command==='mediaSelectVideo'&&args.appId!==570){
     const entry=state.gameMedia.entries[response.key];
     entry.refreshRequested=true;entry.refreshSlot='dynamic';entry.status='missing';
     entry.selectedVideo.selectionToken=args.searchToken;
     return structuredClone(entry);
    }
    return response;
   };
   const input=document.getElementById('identity-steam-id');input.value=id;input.dispatchEvent(new Event('input'));
  },id);
 }
 await run('draft wallpaper searches and selects the unsaved Steam AppID without saving identity',async()=>{
  await beginDraftVideoProbe();await page.evaluate(()=>searchSharedWallpaperVideos(captureSharedWallpaperWrite()));
  const query=await page.evaluate(()=>({calls:draftVideoProbe.calls,oldKept:JSON.stringify(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'])===draftVideoProbe.oldMedia,candidates:document.querySelectorAll('[data-video-candidate]').length,id:editDraft.game.override.steamId}));
  check(query.candidates===2&&query.calls.find(c=>c.command==='mediaSearchVideos')?.args.appId===620,'video search ignored the unsaved Steam AppID');
  check(query.id===620&&query.oldKept&&query.calls.filter(c=>c.command==='mediaCreate').every(c=>!c.args.executable),'draft media search rebound the saved EXE or media');
  await page.evaluate(()=>document.querySelector('[data-video-candidate]').click());await page.waitForFunction(()=>!sharedWallpaperBusy);
  const selected=await page.evaluate(()=>({calls:draftVideoProbe.calls,entry:selectedActionGame.gameMedia,oldKept:JSON.stringify(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'])===draftVideoProbe.oldMedia,identityKept:JSON.stringify(__LIBRARY_H5_FIXTURE__.snapshot.manualOverrides)===draftVideoProbe.saved,draft:editDraft.game.override.steamId,phase:sharedWallpaperDownloadRequests.get(keyFor(selectedActionGame))?.phase}));
  check(selected.entry.appId===620&&selected.entry.selectedVideo?.id==='0'&&selected.phase==='queued','unsaved-ID video choice did not reach the existing download queue');
  check(selected.oldKept&&selected.identityKept&&selected.draft===620&&!selected.calls.some(c=>['saveEditor','identifyOne','saveManualName','clearManualSteamId','commit'].includes(c.command)),'video choice prematurely saved game identity');
  const feedback=await page.evaluate(()=>{
   const entry=selectedActionGame.gameMedia,key=entry.key,panel=$('#shared-wallpaper-download-status'),bar=panel.querySelector('progress'),retry=panel.querySelector('button');
   const row=()=>({mode:sharedWallpaperMode(),visible:!panel.classList.contains('hidden'),status:panel.dataset.status,value:bar.hasAttribute('value')?bar.value:null,retry:!retry.classList.contains('hidden'),text:panel.querySelector('[data-wallpaper-download-label]').textContent});
   const original=snapshot.mediaDownloads,queued=row();
   snapshot.mediaDownloads={active:{[key]:{key,phase:'downloading',requestBindingRevision:entry.bindingRevision,bytes:25,totalBytes:100}}};renderSharedWallpaperDownloadStatus();const active=row();
   snapshot.mediaDownloads={manualItems:{[key]:{state:'unavailable',requestBindingRevision:entry.bindingRevision,autoStopped:true,lastError:'media-network-read-failed'}}};renderSharedWallpaperDownloadStatus();const failed=row();
   return {queued,active,failed,original};
  });
  progressMetrics.push({case:'draft-video-static-download-feedback',feedback});
  check(feedback.queued.mode==='static'&&feedback.queued.visible&&feedback.queued.status==='pending'&&feedback.queued.value===null,'static draft preview hid its accepted video download feedback: '+JSON.stringify(feedback.queued));
  check(feedback.active.visible&&feedback.active.status==='downloading'&&feedback.active.value===0.25&&feedback.failed.visible&&feedback.failed.status==='error'&&feedback.failed.retry,'static draft preview omitted active progress or terminal video failure');
  const retry=await page.evaluate(async()=>{const p=draftVideoProbe,from=p.calls.length;$('#shared-wallpaper-download-status button').click();for(let i=0;i<15;i++)await Promise.resolve();return {video:p.calls.slice(from).filter(c=>c.command==='mediaSearchVideos').map(c=>c.args.appId),art:p.calls.slice(from).filter(c=>c.command==='searchArtwork').length,candidates:document.querySelectorAll('[data-video-candidate]').length};});
  check(retry.video.join(',')==='620'&&retry.art===0&&retry.candidates===2,'static draft video retry opened image search or the wrong AppID');
  await page.evaluate(state=>{cancelArtworkTask();snapshot.mediaDownloads=state;renderSharedWallpaperDownloadStatus();},feedback.original);
  const switches=await page.evaluate(()=>{
   const input=document.getElementById('identity-steam-id');
   for(const appId of [730,440]){
    const key='steam-'+appId;
    snapshot.gameMedia.entries[key]={...selectedActionGame.gameMedia,key,appId,selectedVideo:null};
   }
   return ['730','440','','480'].map(id=>{
    input.value=id;input.dispatchEvent(new Event('input'));
    return {id,preview:selectedActionGame.gameMedia?.appId,target:captureSharedWallpaperWrite()?.target.appId,feedbackHidden:$('#shared-wallpaper-download-status').classList.contains('hidden')};
   });
  });
  progressMetrics.push({case:'draft-video-selected',query,selected,switches});
  check(switches[0].preview===730&&switches[0].target===730,'a later cached draft ID kept the previous draft video preview');
  check(switches[1].preview===440&&switches[1].target===440,'second cached draft ID kept a stale preview and wallpaper write target: '+JSON.stringify(switches));
  check(switches.slice(0,2).every(row=>row.feedbackHidden),'video download feedback followed the editor to another static AppID');
  check(switches.slice(2).every(row=>row.preview===570&&row.target===570),'clearing or leaving the draft cache did not restore the saved wallpaper');
  // The media-change snapshot may arrive before the selection IPC receipt.
  // It advances the target revision while the editor still previews its old ID.
  await reset();await beginDraftVideoProbe();
  await page.evaluate(async()=>{const actual=invoke;invoke=async(command,args={},timeout=0)=>{const result=await actual(command,args,timeout);if(command==='mediaSelectVideo'){const next=structuredClone(snapshot);next.gameMedia.entries[result.key]=structuredClone(result);applyLibrarySnapshot(next);}return result;};await searchSharedWallpaperVideos(captureSharedWallpaperWrite());document.querySelector('[data-video-candidate]').click();});
  await page.waitForFunction(()=>!sharedWallpaperBusy);
  check(await page.evaluate(()=>selectedActionGame.gameMedia?.appId===620&&selectedActionGame.gameMedia.selectedVideo?.id==='0'&&document.getElementById('artwork-search-modal').classList.contains('hidden')&&draftVideoProbe.calls.filter(c=>c.command==='mediaSelectVideo').length===1),'an accepted draft selection snapshot stranded the previous-ID preview');
 });
 await run('draft wallpaper rejects empty and invalid IDs without falling back to saved media',async()=>{
  const rows=[];for(const id of ['', '0','abc','2147483648']){
   if(rows.length)await reset();await beginDraftVideoProbe(id);
   const row=await page.evaluate(async()=>{await searchSharedWallpaperVideos(captureSharedWallpaperWrite());return {calls:draftVideoProbe.calls,gallery:!document.getElementById('artwork-search-modal').classList.contains('hidden'),focus:document.activeElement.id};});rows.push({id,...row});
   check(!row.calls.some(c=>['mediaSearchVideos','mediaCreate','mediaSelectVideo','saveEditor','identifyOne'].includes(c.command))&&!row.gallery&&row.focus==='identity-steam-id','invalid draft ID searched the persisted ID or saved the game');
  }progressMetrics.push({case:'draft-video-invalid',rows});
 });
 await run('draft wallpaper late results and old candidates cannot select after the draft ID changes',async()=>{
  await beginDraftVideoProbe();await page.evaluate(()=>{const actual=invoke;invoke=async(command,args={},timeout=0)=>{if(command==='mediaSearchVideos'&&!draftVideoProbe.held){draftVideoProbe.held=true;return new Promise(resolve=>draftVideoProbe.reply=async()=>resolve(await actual(command,args,timeout)));}return actual(command,args,timeout);};draftVideoProbe.task=searchSharedWallpaperVideos(captureSharedWallpaperWrite());});
  await page.waitForFunction(()=>!!draftVideoProbe.reply);
  await page.evaluate(async()=>{const input=document.getElementById('identity-steam-id');input.value='730';input.dispatchEvent(new Event('input'));await draftVideoProbe.reply();await draftVideoProbe.task;});
  check(await page.evaluate(()=>document.querySelectorAll('[data-video-candidate]').length===0&&!!document.querySelector('[data-video-search-retry]')),'late old-ID results remained selectable');
  await page.evaluate(()=>document.querySelector('[data-video-search-retry]').click());await page.waitForFunction(()=>document.querySelectorAll('[data-video-candidate]').length===2);
  const stale=await page.evaluate(async()=>{const old=document.querySelector('[data-video-candidate]'),input=document.getElementById('identity-steam-id');input.value='620';input.dispatchEvent(new Event('input'));old.click();return {selects:draftVideoProbe.calls.filter(c=>c.command==='mediaSelectVideo').length,searches:draftVideoProbe.calls.filter(c=>c.command==='mediaSearchVideos').map(c=>c.args.appId),retry:!!document.querySelector('[data-video-search-retry]')};});
  check(stale.selects===0&&stale.searches.join(',')==='620,730'&&stale.retry,'stale candidate submitted a selection or explicit retry reused the old draft');progressMetrics.push({case:'draft-video-stale',stale});
 });
 await run('draft wallpaper keeps native Steam manifest identity immutable',async()=>{
  await page.evaluate(()=>{const game=structuredClone(selectedActionGame);closeEdit();game.steamNative=true;game.steam.nativeSteamAppId=570;openEdit(game);});await page.waitForTimeout(25);
  await page.evaluate(()=>{window.nativeDraftCalls=[];const actual=invoke;invoke=async(command,args={},timeout=0)=>{nativeDraftCalls.push({command,args:structuredClone(args)});return actual(command,args,timeout);};document.getElementById('identity-steam-id').value='620';return searchSharedWallpaperVideos(captureSharedWallpaperWrite());});
  check(await page.evaluate(()=>nativeDraftCalls.find(c=>c.command==='mediaSearchVideos')?.args.appId===570&&!nativeDraftCalls.some(c=>c.command==='mediaCreate')),'native Steam query accepted a mutable input instead of the manifest AppID');
 });
 await run('wallpaper controller reaches mode playback and search controls at handheld sizes',async()=>{
  const rows=[];for(const height of [720,800]){
   await page.setViewportSize({width:1280,height});if(rows.length)await reset();
   const row=await page.evaluate(async()=>{
    const actions=['navigate-up','navigate-down','navigate-left','navigate-right'];
    const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    const step=async action=>{lastDirectionalActionAt=0;controllerBackLastAt=0;routeWorkspaceAction(action);await tick();};
    focusEditModal();await tick();const start=document.activeElement,paths=new Map([[start,[]]]),queue=[start];
    while(queue.length&&paths.size<60){const from=queue.shift();for(const action of actions){focusWorkspaceElement(from);await step(action);const to=document.activeElement;if(!paths.has(to)){paths.set(to,[...paths.get(from),action]);queue.push(to);}}}
    const dynamic=document.querySelector('[data-shared-wallpaper-mode="dynamic"]'),staticButton=document.querySelector('[data-shared-wallpaper-mode="static"]'),skip=workspaceDropdownTriggerForSelect(document.getElementById('shared-wallpaper-skip')),length=workspaceDropdownTriggerForSelect(document.getElementById('shared-wallpaper-length'));
    const controls=[dynamic,staticButton,skip,length],reachable=controls.every(control=>paths.has(control));
    if(!reachable)return {reachable:false,visited:[...paths.keys()].map(e=>e.id||e.dataset.sharedWallpaperMode||e.textContent.slice(0,25)),missing:controls.filter(c=>!paths.has(c)).map(c=>c.id||c.dataset.sharedWallpaperMode)};
    const visited=[];for(const control of controls){focusWorkspaceElement(start);for(const action of paths.get(control))await step(action);const r=control.getBoundingClientRect();visited.push({label:control.id||control.dataset.sharedWallpaperMode,visible:r.top>=0&&r.bottom<=innerHeight,focus:document.activeElement===control});}
    focusWorkspaceElement(staticButton);await step('accept');await new Promise(resolve=>setTimeout(resolve,30));const staticMode=sharedWallpaperMode()==='static';
    focusWorkspaceElement(dynamic);await step('accept');await new Promise(resolve=>setTimeout(resolve,30));const dynamicMode=sharedWallpaperMode()==='dynamic';
    focusWorkspaceElement(skip);await step('accept');const dropdown=!!openWorkspaceDropdownState;await step('back');const returned=document.activeElement===skip&&!openWorkspaceDropdownState;
    focusWorkspaceElement(dynamic);await step('navigate-down');const down=document.activeElement.id;await step('navigate-up');const upInModes=controls.includes(document.activeElement);
    focusWorkspaceElement(document.getElementById('artwork-wallpaper-preview'));await step('accept');const actionModal=activeWorkspaceModal()?.id;focusWorkspaceElement(document.getElementById('artwork-action-search'));await step('accept');await new Promise(resolve=>setTimeout(resolve,30));const search=activeWorkspaceModal()?.id;await step('back');const back=activeWorkspaceModal()?.id;
    return {reachable,visited,staticMode,dynamicMode,dropdown,returned,down,upInModes,actionModal,search,back};
   });rows.push({height,...row});
   check(row.reachable,'controller could not reach wallpaper mode/playback controls '+JSON.stringify(row));
   check(row.visited.every(v=>v.visible&&v.focus)&&row.staticMode&&row.dynamicMode&&row.dropdown&&row.returned&&row.down==='artwork-wallpaper-preview'&&row.upInModes,'wallpaper focus, mode activation or dropdown return failed '+JSON.stringify(row));
   check(row.actionModal==='artwork-action-modal'&&row.search==='artwork-search-modal'&&row.back==='edit-modal','controller search/Back escaped the editor '+JSON.stringify(row));
  }progressMetrics.push({case:'wallpaper-controller-handheld',rows});
  const dockRows=[];
  for(const [width,height]of [[1280,720],[1280,800],[1920,1080]]){
   await page.setViewportSize({width,height});
   dockRows.push(...await page.evaluate(async({width,height})=>{
    closeEdit();const saved=structuredClone(snapshot),state=librarySidebarState(),savedState={...state},savedTab=activeTab,savedSelected=selectedSteamDirectories,rows=[];
    const games=Array.from({length:3},(_,i)=>({gameDirectory:'G:/VisibleDock/Game'+i,directoryName:'Dock Game '+i,primaryExecutable:'G:/VisibleDock/Game'+i+'/Game.exe',primaryProductName:'Dock Game '+i,status:'ready'}));
    const plans=games.map(game=>({...game,status:'already-in-steam',formalName:game.directoryName}));
    const key=i=>normalizedUiPath(games[i].gameDirectory),tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    const install=()=>{snapshot={...saved,library:{games:structuredClone(games)},manualOverrides:{items:{}},gameMedia:{entries:{}},steamPlan:{items:structuredClone(plans),summary:{}}};activeTab='in-steam';state.query='';$('#library-search').value='';selectedSteamDirectories=null;currentCardDirectory='';state.lastCard='';renderLibrary();focusLibraryCard(libraryCards()[0]);};
    const observe=(mode,expected,extra={})=>{syncLibrarySidebarDock();const actual=librarySidebarCurrentGame(),edit=$('#library-dock-edit'),select=$('#library-dock-select');rows.push({mode,width,height,expected,actual:actual?keyFor(actual):null,label:$('#library-dock-name').textContent,editDisabled:edit.disabled,selectDisabled:select.disabled,visible:libraryCards().map(c=>c.dataset.directory),...extra});};
    const search=value=>{focusWorkspaceElement($('#library-search'));$('#library-search').value=value;$('#library-search').dispatchEvent(new Event('input',{bubbles:true}));};
    for(const mode of ['filter to other','empty search','different category','removed card','focus beats remembered hover','same category remembered','visible alias','metadata refresh']){
     install();await tick();
     if(mode==='filter to other'||mode==='empty search'){search(mode==='filter to other'?'Dock Game 1':'No visible matching title');await tick();observe(mode,mode==='filter to other'?key(1):null);$('#library-dock-edit').click();await tick();rows.at(-1).edited=selectedActionGame?keyFor(selectedActionGame):null;rows.at(-1).editorOpen=!$('#edit-modal').classList.contains('hidden');if(selectedActionGame)closeEdit();}
     else if(mode==='different category'){snapshot.steamPlan.items[1].status='ready-to-add';focusWorkspaceElement($('.page-tab[data-tab="not-in-steam"]'));$('.page-tab[data-tab="not-in-steam"]').click();await tick();observe(mode,key(1));$('#library-dock-edit').click();await tick();rows.at(-1).edited=selectedActionGame?keyFor(selectedActionGame):null;if(selectedActionGame)closeEdit();}
     else if(mode==='removed card'){focusWorkspaceElement($('#library-search'));snapshot.library.games.shift();snapshot.steamPlan.items.shift();renderLibrary();await tick();observe(mode,key(1));}
     else if(mode==='focus beats remembered hover'){libraryCards()[1].dispatchEvent(new PointerEvent('pointerenter',{pointerId:1,isPrimary:true}));state.lastCard=key(2);observe(mode,key(0));}
     else if(mode==='same category remembered'){focusLibraryCard(libraryCards()[1]);focusWorkspaceElement($('#library-search'));observe(mode,key(1));}
     else if(mode==='visible alias'){focusWorkspaceElement($('#library-search'));state.lastCard=key(1).toUpperCase().replaceAll(String.fromCharCode(92),'/');currentCardDirectory='';observe(mode,key(1));}
     else {focusWorkspaceElement($('#library-search'));snapshot.library.games[0].primaryExecutable='G:/VisibleDock/Game0/Updated.exe';snapshot.steamPlan.items[0].primaryExecutable=snapshot.library.games[0].primaryExecutable;renderLibrary();observe(mode,key(0),{executable:librarySidebarCurrentGame()?.primaryExecutable});}
    }
    snapshot=saved;Object.assign(state,savedState);activeTab=savedTab;selectedSteamDirectories=savedSelected;$('#library-search').value=state.query;renderLibrary();return rows;
   },{width,height}));
  }
  progressMetrics.push({case:'sidebar-dock-visible-card',rows:dockRows});
  check(dockRows.every(row=>row.actual===row.expected&&(row.expected?row.visible.includes(row.expected)&&!row.editDisabled&&!row.selectDisabled:row.editDisabled&&row.selectDisabled)&&(!('edited'in row)||row.edited===row.expected)&&(row.mode!=='empty search'||!row.editorOpen)&&(row.mode!=='metadata refresh'||row.executable==='G:/VisibleDock/Game0/Updated.exe')),'Sidebar dock edited a remembered hidden card, lost current focus, or enabled an empty page');

  const coverRows=[];
  for(const [width,height]of [[1280,720],[1280,800],[1920,1080]]){
   await page.setViewportSize({width,height});
   coverRows.push(...await page.evaluate(async({width,height})=>{
    closeEdit();const saved=structuredClone(snapshot),state=librarySidebarState(),savedState={...state},savedTab=activeTab,savedSelected=selectedGameDirectories,hasFocus=document.hasFocus,rows=[];
    const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    const seed={gameDirectory:'G:/CoverFallback/ActionGame',primaryExecutable:'G:/CoverFallback/ActionGame/Game.exe',directoryName:'Action Game',status:'ready'},key=keyFor(seed),url='/cover.jpg?owned-card='+width+'x'+height;
    let serial=0;
    const install=(selected=true,source=url)=>{source+=(source.includes('?')?'&':'?')+'ownedAttempt='+(++serial);snapshot={...saved,library:{games:[structuredClone(seed)]},manualOverrides:{items:{[normalizedUiPath(seed.primaryExecutable)]:{cover:{url:source}}}},gameMedia:{entries:{}},steamPlan:{items:[{...seed,status:'waiting-steam-verification'}],summary:{}}};activeTab='not-in-steam';selectedGameDirectories=selected?null:new Set();state.query='';$('#library-search').value='';renderLibrary();const card=libraryCards()[0];focusLibraryCard(card);return card;};
    const inspect=(mode,card,prior={})=>{const hint=card.querySelector('.sidebar-card-edit'),check=card.querySelector('.sidebar-card-check'),cover=card.querySelector('.cover-placeholder'),box=hint?.getBoundingClientRect(),parent=cover.getBoundingClientRect();return{mode,width,height,check:!!check,hint:!!hint,hintVisible:!!hint&&getComputedStyle(hint).display!=='none',hintInside:!!box&&box.left>=parent.left&&box.right<=parent.right+1&&box.top>=parent.top&&box.bottom<=parent.bottom+1,fallback:cover.textContent.includes('AG'),image:!!cover.querySelector('img'),selected:card.getAttribute('aria-pressed'),focus:document.activeElement===card,prior};};
    for(const mode of ['selected error','unselected error','background error','retired card error','same snapshot reuse','healthy image','actual route404']){
     document.hasFocus=()=>true;const selected=mode!=='unselected error',card=install(selected,mode==='actual route404'?'/owned-cover-missing-'+width+'-'+height+'.jpg':url),image=card.querySelector('img'),hint=card.querySelector('.sidebar-card-edit'),check=card.querySelector('.sidebar-card-check');
     if(!image||!hint||!check)throw Error('owned image/control setup missing');
     if(mode==='background error'){document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
     if(mode==='retired card error'){const fresh={...structuredClone(snapshot),manualOverrides:{items:{[normalizedUiPath(seed.primaryExecutable)]:{cover:{url:'/cover.jpg?replacement='+width}}}}};applyLibrarySnapshot(fresh);const next=libraryCards()[0],markup=next.querySelector('.cover-placeholder').innerHTML;image.dispatchEvent(new Event('error'));rows.push({...inspect(mode,next),retiredKept:next.querySelector('.cover-placeholder').innerHTML===markup});continue;}
     if(mode==='actual route404'){for(let i=0;i<100&&image.isConnected;i++)await new Promise(resolve=>setTimeout(resolve,10));}
     else if(mode!=='healthy image')image.dispatchEvent(new Event('error'));
     if(mode==='background error'){document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));}
     if(mode==='same snapshot reuse'){applyLibrarySnapshot(structuredClone(snapshot));}
     await tick();const current=libraryCards()[0],row=inspect(mode,current,{hintKept:current.querySelector('.sidebar-card-edit')===hint,checkKept:current.querySelector('.sidebar-card-check')===check,sameCard:current===card});
     if(mode!=='healthy image'){
      focusLibraryCard(current);routeWorkspaceAction('edit');await tick();row.edited=selectedActionGame?keyFor(selectedActionGame):null;if(selectedActionGame)closeEdit();
     }
     rows.push(row);
    }
    document.hasFocus=hasFocus;snapshot=saved;Object.assign(state,savedState);activeTab=savedTab;selectedGameDirectories=savedSelected;$('#library-search').value=state.query;renderLibrary();return rows;
   },{width,height}));
  }
  progressMetrics.push({case:'card-cover-failure-controls',rows:coverRows});
  check(coverRows.every(row=>row.check&&row.hint&&row.hintVisible&&row.hintInside&&(row.mode==='healthy image'?row.image&&!row.fallback:row.mode==='retired card error'?row.image&&row.retiredKept:row.fallback&&!row.image&&row.focus&&row.prior.sameCard&&row.prior.hintKept&&row.prior.checkKept&&row.edited===normalizedForTest('G:/CoverFallback/ActionGame'))&&row.selected===(row.mode==='unselected error'?'false':'true')),'Failed card cover removed its controller edit hint, selection indicator or current card ownership');

  const searchRows=[];
  for(const [width,height]of [[1280,720],[1280,800],[1920,1080]]){
   await page.setViewportSize({width,height});
   searchRows.push(...await page.evaluate(async({width,height})=>{
    closeEdit();const actual=invoke,input=$('#library-search'),state=librarySidebarState(),oldQuery=state.query,rows=[];let keyboards=0,closes=0;
    invoke=(command,args={},timeout=0)=>command==='keyboard'?(keyboards++,Promise.resolve({requested:true})):command==='closeWorkspace'?(closes++,Promise.resolve({closing:true})):actual(command,args,timeout);
    const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))),key=k=>input.dispatchEvent(new KeyboardEvent('keydown',{key:k,code:k,bubbles:true,cancelable:true}));
    const reset=()=>{input.blur();input.value='';state.query='';finishControllerTextInput(input);renderLibrary();focusWorkspaceElement(input);controllerBackLastAt=0;if(controllerBackResetTimer)clearTimeout(controllerBackResetTimer);controllerBackResetTimer=0;keyboards=0;closes=0;};
    for(const mode of ['controller accept and Back','physical Enter and Escape','pointer and Escape','controller direction','blur relocks','submit visible search']){
     reset();const lockedInitially=input.readOnly;
     if(mode==='physical Enter and Escape')key('Enter');else if(mode==='pointer and Escape'||mode==='submit visible search')input.dispatchEvent(new PointerEvent('pointerdown',{button:0,bubbles:true}));else routeWorkspaceAction('accept');
     await tick();const entered={editing:input.dataset.controllerEditing==='true',locked:input.readOnly,focused:document.activeElement===input,keyboards,closes};
     input.value=mode==='submit visible search'?'Dota':'Retained search input';input.dispatchEvent(new Event('input',{bubbles:true}));
     // Navigation controls need results. Text retention is checked on Back.
     if(mode==='controller direction'||mode==='blur relocks'){input.value='';input.dispatchEvent(new Event('input',{bubbles:true}));}
     if(mode==='controller accept and Back'){routeWorkspaceAction('back');}
     else if(mode==='physical Enter and Escape'||mode==='pointer and Escape')key('Escape');
     else if(mode==='controller direction'){lastDirectionalActionAt=0;routeWorkspaceAction('navigate-down');}
     else if(mode==='blur relocks')focusWorkspaceElement(document.querySelector('[data-library-density="compact"]'));
     else key('Enter');
     await tick();const settled={editing:input.dataset.controllerEditing==='true',locked:input.readOnly,keyboards,closes,backArmed:controllerBackLastAt>0,backTimer:!!controllerBackResetTimer,focused:document.activeElement===input,card:!!activeLibraryCard(),text:input.value,query:state.query};
     if(mode==='controller accept and Back'){routeWorkspaceAction('back');await tick();settled.nextBackCloses=closes;}
     rows.push({mode,width,height,lockedInitially,entered,settled});
    }
    input.blur();finishControllerTextInput(input);input.value=oldQuery;state.query=oldQuery;controllerBackLastAt=0;if(controllerBackResetTimer)clearTimeout(controllerBackResetTimer);controllerBackResetTimer=0;invoke=actual;renderLibrary();return rows;
   },{width,height}));
  }
  progressMetrics.push({case:'library-search-input-state',rows:searchRows});
  check(searchRows.every(row=>row.lockedInitially&&row.entered.editing&&!row.entered.locked&&row.entered.focused&&row.entered.keyboards===(['physical Enter and Escape','pointer and Escape','submit visible search'].includes(row.mode)?0:1)&&!row.settled.editing&&row.settled.locked&&row.settled.closes===0&&(row.mode==='controller accept and Back'?row.settled.nextBackCloses===0&&!row.settled.backArmed&&!row.settled.backTimer&&row.settled.focused&&row.settled.text==='Retained search input'&&row.settled.query==='retained search input':row.mode.includes('Escape')?row.settled.focused&&!row.settled.backArmed&&row.settled.text==='Retained search input':row.mode==='controller direction'||row.mode==='submit visible search'?row.settled.card:true)),'Library search did not preserve explicit text entry or first-Back exit without closing the workspace');

  const refreshFocusRows=[];
  for(const [width,height]of [[1280,720],[1280,800],[1920,1080]]){
   await page.setViewportSize({width,height});
   refreshFocusRows.push(...await page.evaluate(async({width,height})=>{
    closeEdit();const actual=invoke,saved=structuredClone(snapshot),state=librarySidebarState(),savedState={...state},rows=[],savedMemory=libraryFocusMemory,focus=document.hasFocus;let releases=[],reads=0;
    invoke=(command,args={},timeout=0)=>command==='openLibrary'?new Promise(resolve=>releases.push(resolve)):['state','plan'].includes(command)?Promise.resolve(workspaceBridgeResult(command,structuredClone(snapshot))):command==='steamCommitPreflight'?Promise.resolve({steamRunning:false}):actual(command,args,timeout);
    if(silentRefreshInFlight)await silentRefreshInFlight;
    const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    const describe=element=>element?.matches('[data-library-density]')?'density:'+element.dataset.libraryDensity:element?.matches('.root-row button')?(element.closest('#manual-game-list')?'manual:':'root:')+normalizedUiPath(element.closest('.root-row').querySelector('code').textContent):element?.id||element?.tagName||'';
    const roots=['G:/FocusSources/Alpha','G:/FocusSources/Beta'],manual=['G:/FocusManual/Alpha','G:/FocusManual/Beta'];
    const install=()=>{document.hasFocus=()=>true;snapshot={...structuredClone(saved),config:{...saved.config,roots:[...roots],manualGameDirectories:[...manual]}};state.query='';$('#library-search').value='';librarySidebarPage('library',false);renderLibrary();focusLibraryCard(libraryCards()[0]);releases=[];};
    for(const mode of ['density standard','density compact','source remove second','manual remove second','search editing','removed source action','removed manual action','hidden remembered density','empty inventory management']){
     install();const task=openLibraryView();let target,expected;
     if(mode.startsWith('density')){target=document.querySelector('button[data-library-density="'+mode.split(' ')[1]+'"]');expected='density:'+mode.split(' ')[1];}
     else if(mode==='search editing'){target=$('#library-search');expected='library-search';}
     else if(mode==='hidden remembered density'){target=document.querySelector('button[data-library-density="compact"]');focusWorkspaceElement(target);librarySidebarPage('sources',false);expected='pick-root-button';}
     else {const isManual=mode.includes('manual');librarySidebarPage(isManual?'manual':'sources');target=document.querySelectorAll((isManual?'#manual-game-list':'#root-list')+' .root-row button')[1];expected=mode.startsWith('removed')||mode==='empty inventory management'?(isManual?'pick-game-button':'pick-root-button'):(isManual?'manual:':'root:')+normalizedUiPath((isManual?manual:roots)[1]);}
     if(mode!=='hidden remembered density')focusWorkspaceElement(target);
     if(mode==='search editing'){beginControllerTextInput(target);target.value='Dota';target.dispatchEvent(new Event('input',{bubbles:true}));}
     const before=describe(document.activeElement),reply=structuredClone(snapshot);
     if(mode==='removed source action')reply.config.roots=reply.config.roots.slice(0,1);
     if(mode==='removed manual action')reply.config.manualGameDirectories=reply.config.manualGameDirectories.slice(0,1);
     if(mode==='empty inventory management'){reply.config.roots=[];reply.library.games=[];reply.steamPlan.items=[];}
     if(releases.length!==1)throw Error('owned delayed open not admitted');releases[0](workspaceBridgeResult('openLibrary',reply));await task;await tick();
     rows.push({mode,width,height,before,expected,after:describe(document.activeElement),visible:isVisuallyPresent(document.activeElement),inside:$('#library-view').contains(document.activeElement),page:state.page,editing:$('#library-search').dataset.controllerEditing==='true',text:$('#library-search').value,openReads:releases.length});
    }
    // An already scheduled repair cannot focus controls after the page blurs.
    install();focusWorkspaceElement(document.querySelector('button[data-library-density="compact"]'));const marker=document.createElement('button');marker.textContent='external focus';document.body.appendChild(marker);marker.focus();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));scheduleLibraryFocusRepair();await tick();rows.push({mode:'background scheduled repair',width,height,kept:document.activeElement===marker});marker.remove();document.hasFocus=focus;
    snapshot=saved;Object.assign(state,savedState);$('#library-search').value=state.query;finishControllerTextInput($('#library-search'));librarySidebarPage(savedState.page,false);libraryFocusMemory=savedMemory;renderLibrary();invoke=actual;return rows;
   },{width,height}));
  }
  progressMetrics.push({case:'sidebar-refresh-focus',rows:refreshFocusRows});
  check(refreshFocusRows.every(row=>row.mode==='background scheduled repair'?row.kept:row.after===row.expected&&row.visible&&row.inside&&row.openReads===1&&(row.mode!=='search editing'||row.editing&&row.text==='Dota')),'Delayed library refresh lost sidebar/management focus or selected a hidden/retired target');



 });



 await run('saved wallpaper mode static survives reopen with cached video and stages a static image',async()=>{
  await beginStaticSourceProbe();await page.evaluate(()=>{const g=structuredClone(selectedActionGame);closeEdit();openEdit(g);});await settle();
  const before=await page.evaluate(()=>({saved:selectedActionGame.gameMedia.wallpaperMode,mode:sharedWallpaperMode(),pressed:document.querySelector('[data-shared-wallpaper-mode="static"]').getAttribute('aria-pressed'),video:!!document.querySelector('#artwork-wallpaper-preview video')}));
  await page.evaluate(()=>{openArtworkActionMenu('wallpaper');window.savedModeImport=artworkActionForCurrentType('replace');reverseIO.finishPick({path:'/wallpaper.jpg?pending=saved-static'});});await page.evaluate(()=>savedModeImport);
  const after=await page.evaluate(()=>({dirty:artworkDraft.dirty.wallpaper,image:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),path:document.querySelector('#artwork-wallpaper-preview .artwork-path')?.textContent,imports:reverseIO.calls.filter(x=>x.command==='mediaImport').length,pickers:reverseIO.calls.filter(x=>x.command==='pickArtwork'||x.command==='pickVideo').map(x=>x.command)}));
  progressMetrics.push({case:'saved-static-mode-picker',before,after});
  check(before.saved==='static'&&before.mode==='static'&&before.pressed==='true'&&!before.video&&after.dirty&&(after.image?.includes('pending=saved-static')||after.path==='/wallpaper.jpg?pending=saved-static')&&!after.imports&&after.pickers.join(',')==='pickArtwork','saved static mode was inferred from cached video or sent to the wrong picker');
 });
 await run('saved wallpaper mode dynamic remains selected when its video is missing',async()=>{
  await page.evaluate(()=>{const e=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'];e.wallpaperMode='dynamic';e.dynamicPreviewUrl='';e.dynamicReady=false;delete e.assets.dynamic;e.asset=null;e.previewUrl='';snapshot=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);const g=mergedGames().find(g=>g.gameMedia?.key==='steam-570');closeEdit();openEdit(g);});await settle();
  const r=await page.evaluate(()=>({mode:sharedWallpaperMode(),pressed:document.querySelector('[data-shared-wallpaper-mode="dynamic"]').getAttribute('aria-pressed'),image:!!document.querySelector('#artwork-wallpaper-preview img'),text:document.getElementById('artwork-wallpaper-preview').textContent}));
  check(r.mode==='dynamic'&&r.pressed==='true'&&!r.image&&r.text.includes('动态壁纸'),'unavailable video silently changed the saved wallpaper mode');
 });
 await run('saved wallpaper mode absent retains the legacy video fallback',async()=>{
  await page.evaluate(()=>{delete selectedActionGame.gameMedia.wallpaperMode;sharedWallpaperPreviewChoice=null;renderArtworkDraft();});
  const r=await page.evaluate(()=>({mode:sharedWallpaperMode(),video:!!document.querySelector('#artwork-wallpaper-preview video')}));check(r.mode==='dynamic'&&r.video,'legacy records without saved mode lost their video fallback');
 });
 await run('static source regular staged deletion stays visible before saving',async()=>{
  await beginStaticSourceProbe();await page.evaluate(()=>{openArtworkActionMenu('wallpaper');artworkActionForCurrentType('delete');});const r=await page.evaluate(()=>({dirty:artworkDraft.dirty.wallpaper,deleted:artworkDraft.wallpaper.state==='deleted',text:document.getElementById('artwork-wallpaper-preview').textContent,image:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),calls:reverseIO.calls.filter(x=>x.command==='mediaImport'||x.command==='mediaEdit'&&x.args.action==='delete').length}));progressMetrics.push({case:'static-source-pending-delete',...r});check(r.dirty&&r.deleted&&r.text.includes('已删除')&&!r.image&&r.calls===0,'regular static staged delete was hidden by the existing shared image or written before Save');
 });
 await run('static source regular staged replacement keeps its pending preview ahead of saved shared media',async()=>{
  await beginStaticSourceProbe();await page.evaluate(()=>{openArtworkActionMenu('wallpaper');window.staticPick=artworkActionForCurrentType('replace');reverseIO.finishPick({path:'/wallpaper.jpg?pending=new'});});await page.evaluate(()=>staticPick);const r=await page.evaluate(()=>({dirty:artworkDraft.dirty.wallpaper,url:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),text:document.getElementById('artwork-wallpaper-preview').textContent,imports:reverseIO.calls.filter(x=>x.command==='mediaImport').length}));check(r.dirty&&(r.url?.includes('pending=new')||r.text.includes('pending=new'))&&r.imports===0,'regular static staged replacement lost its pending preview or imported immediately');
 });
 await run('static source shared import remains the displayed choice when its scanned game reappears',async()=>{
  await beginStaticSourceProbe();await hideStaticLibraryCard();
  // Model another shared-media consumer writing while the card is absent.
  const accepted=await page.evaluate(()=>invoke('mediaImport',{...snapshot.gameMedia.entries['steam-570'],slot:'static',source:'G:/H5Fixture/new-static.jpg'}));
  await restoreStaticLibraryCard();
  const r=await page.evaluate(()=>({ok:!selectedActionGame.mediaOnly,dirty:artworkDraft.dirty.wallpaper,url:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),asset:selectedActionGame.gameMedia.assets.static.id,manual:selectedActionGame.gameMedia.manualSlots?.static,imports:reverseIO.calls.filter(x=>x.command==='mediaImport').length}));progressMetrics.push({case:'static-source-library-return',accepted,...r});
  check(accepted.manualSlots?.static&&r.asset===accepted.assets.static.id&&r.ok&&!r.dirty&&r.manual&&r.url?.includes('imported=')&&r.imports===1,'returning library game displayed its old legacy image over an accepted shared manual import');
 });
 await run('static source shared deletion is not hidden by the old image when its scanned game reappears',async()=>{
  await beginStaticSourceProbe();await hideStaticLibraryCard();
  await page.evaluate(async()=>{
   const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'];
   await invoke('mediaEdit',{...entry,action:'delete',slot:'static'});
   entry.deletedSlots={...entry.deletedSlots,static:true};entry.manualSlots={...entry.manualSlots,static:true};
  });
  await restoreStaticLibraryCard();
  const r=await page.evaluate(()=>({deleted:selectedActionGame.gameMedia.deletedSlots?.static,text:document.getElementById('artwork-wallpaper-preview').textContent,image:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),writes:reverseIO.calls.filter(x=>x.command==='mediaEdit'&&x.args.action==='delete').length}));progressMetrics.push({case:'static-source-library-return-delete',...r});check(r.deleted&&!r.image&&r.text.includes('已删除')&&r.writes===1,'returning library game hid its accepted deletion behind an unchanged legacy image');
 });

 await run('wallpaper import consumes its accepted preview receipt without a whole library read',async()=>{
  const result=await page.evaluate(async()=>{const old=snapshot.gameMedia.entries['steam-570'].assets.dynamic.id;document.getElementById('identity-name').value='Keep import draft';document.getElementById('identity-name').dispatchEvent(new Event('input'));editDraft.bucket='non-game';const promise=actOnSharedWallpaper('replace');reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});const ok=await promise;return {ok,old,stored:snapshot.gameMedia.entries['steam-570'],current:selectedActionGame.gameMedia,video:document.querySelector('#artwork-wallpaper-preview video')?.getAttribute('src'),draftName:editDraft.game.override?.name,bucket:editDraft.bucket,reads:reverseIO.calls.filter(x=>x.command==='mediaState').length};});progressMetrics.push({case:'import-receipt-current',...result});
  check(result.ok&&result.stored.assets.dynamic.id!==result.old&&result.current.assets.dynamic.id===result.stored.assets.dynamic.id&&result.video?.includes('imported=2'),'accepted import was not presented from its new asset receipt');
  check(result.reads===0&&result.draftName==='Keep import draft'&&result.bucket==='non-game','import rebuilt the full library or overwrote unsaved draft fields');
 });
 await run('wallpaper import does not depend on a follow-up read to drain both captured card queues after Back',async()=>{
  await page.evaluate(()=>{const actual=invoke;invoke=(command,...args)=>command==='mediaState'?Promise.reject(Error('fixture-followup-state-unavailable')):actual(command,...args);window.importReceiptPending=actOnSharedWallpaper('replace');reverseChoose(5);reverseOpenOther();reverseChoose(10);routeWorkspaceAction('back');reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});const ok=await page.evaluate(()=>importReceiptPending);await settle();const result=await page.evaluate(()=>({a:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'],b:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-1245620'],writes:reverseIO.calls.filter(x=>x.command==='mediaEdit'),reads:reverseIO.calls.filter(x=>x.command==='mediaState').length,closed:document.getElementById('edit-modal').classList.contains('hidden')}));progressMetrics.push({case:'import-receipt-back-queue',ok,...result});
  check(ok&&result.a.playback.start===5&&result.b.playback.start===10&&result.a.bindingRevision===3&&result.b.bindingRevision===2,'accepted import lost its revision and stranded or redirected queued playback');
  check(result.closed&&result.reads===0&&result.writes.length===2,'import reopened the editor, retried writes, or depended on a whole-library refresh');
 });
 await run('wallpaper import background completion updates data and defers library and preview rendering',async()=>{
  await page.evaluate(()=>{window.importReceiptPending=actOnSharedWallpaper('replace');const actual=renderLibrary;window.importPaints=0;renderLibrary=(...args)=>{importPaints++;return actual(...args);};window.importOldVideo=document.querySelector('#artwork-wallpaper-preview video');document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});await page.evaluate(()=>importReceiptPending);const hidden=await page.evaluate(()=>({paints:importPaints,stored:snapshot.gameMedia.entries['steam-570'].assets.dynamic.id,sameVideo:importOldVideo===document.querySelector('#artwork-wallpaper-preview video'),reads:reverseIO.calls.filter(x=>x.command==='mediaState').length}));await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.waitForTimeout(40);const resumed=await page.evaluate(()=>({url:document.querySelector('#artwork-wallpaper-preview video')?.getAttribute('src'),id:selectedActionGame.gameMedia.assets.dynamic.id}));progressMetrics.push({case:'import-receipt-background',hidden,resumed});
  check(hidden.paints===0&&hidden.sameVideo&&hidden.reads===0&&hidden.stored==='imported-2','inactive import rebuilt the library or replaced the active preview node');
  check(resumed.id===hidden.stored&&resumed.url?.includes('imported=2'),'focus did not present the accepted imported asset');
 });
 await run('wallpaper import genuine publication failure preserves assets and drains pending playback once',async()=>{
  await page.evaluate(()=>{window.importOriginalAsset=snapshot.gameMedia.entries['steam-570'].assets.dynamic.id;const actual=invoke;invoke=(command,args={},...rest)=>{if(command==='mediaImport'){reverseIO.calls.push({command,args:structuredClone(args)});return Promise.reject(Error('fixture-import-publication-denied'));}return actual(command,args,...rest);};window.importReceiptPending=actOnSharedWallpaper('replace');reverseChoose(5);reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});const ok=await page.evaluate(()=>importReceiptPending);await settle();const result=await page.evaluate(()=>({entry:snapshot.gameMedia.entries['steam-570'],original:importOriginalAsset,imports:reverseIO.calls.filter(x=>x.command==='mediaImport').length,writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length,notices:[toastNotice.textContent,...noticeQueue.map(x=>x.message)]}));progressMetrics.push({case:'import-receipt-failure',ok,...result});
  check(!ok&&result.entry.assets.dynamic.id===result.original&&result.entry.playback.start===5&&result.entry.bindingRevision===2,'failed publication changed assets or stranded queued playback');
  check(result.imports===1&&result.writes===1&&result.notices.some(x=>x.includes('fixture-import-publication-denied')),'publication failure was retried, hidden, or duplicated');
 });

 await run('closing the editor preserves the latest accepted playback setting',async()=>{

  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=60;reverseChoose(15);reverseChoose(5);closeEdit();});await page.waitForFunction(()=>!sharedWallpaperBusy);const state=await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback);check(state.start===5&&state.end===35,'latest setting was lost after closing editor');

 });

 await run('two games retain their own queued playback values',async()=>{
  await page.evaluate(()=>{
   // Shared media alone no longer invents an installed card. This original
   // two-game queue control needs a second authoritative inventory record.
   const state=__LIBRARY_H5_FIXTURE__.snapshot,entry=state.gameMedia.entries['steam-1245620'],second={gameDirectory:'G:/H5Fixture/SecondInstalled',primaryExecutable:'G:/H5Fixture/SecondInstalled/Game.exe',directoryName:'Second installed fixture',status:'ready'};
   entry.executable=second.primaryExecutable;state.library.games.push(second);state.steamPlan.items.push({...second,status:'ready-to-add',appId:1245620,steamStoreAppId:1245620,steamVerificationStatus:'steam-verified'});snapshot=normalizeWorkspaceSnapshot(structuredClone(state),snapshot);renderLibrary();
   window.reverseOpenOther=()=>{const game=mergedGames().find(g=>g.gameMedia?.key==='steam-1245620');if(!game)throw Error('second-fixture-game-missing');openEdit(game);};
  });


  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=60;reverseChoose(15);reverseChoose(5);reverseOpenOther();reverseChoose(3);});await settle();const values=await page.evaluate(()=>Object.fromEntries(Object.entries(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries).map(([key,entry])=>[key,entry.playback])));check(values['steam-570'].start===5&&values['steam-1245620'].start===3,'switching games dropped or redirected a queued setting');

 });

 await run('a file chosen for the first game cannot import into a newly opened game',async()=>{
  await page.evaluate(()=>{
   // This original two-card control needs authoritative installed inventory.
   // A wallpaper cache entry alone cannot create a card in the current product.
   const state=__LIBRARY_H5_FIXTURE__.snapshot,entry=state.gameMedia.entries['steam-1245620'],second={gameDirectory:'G:/H5Fixture/SecondInstalled',primaryExecutable:'G:/H5Fixture/SecondInstalled/Game.exe',directoryName:'Second installed fixture',status:'ready'};
   entry.executable=second.primaryExecutable;state.library.games.push(second);state.steamPlan.items.push({...second,status:'ready-to-add',appId:1245620,steamStoreAppId:1245620,steamVerificationStatus:'steam-verified'});snapshot=normalizeWorkspaceSnapshot(structuredClone(state),snapshot);
   window.reverseOpenOther=()=>{const game=mergedGames().find(g=>g.gameMedia?.key==='steam-1245620');if(!game)throw Error('second-fixture-game-missing');openEdit(game);};
  });


  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseOpenOther();reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});await page.evaluate(()=>reversePick);const imports=await page.evaluate(()=>reverseIO.calls.filter(c=>c.command==='mediaImport'));check(imports.length===1&&imports[0].args.key==='steam-570','file import was redirected to the newly selected game');


  const pickerRows=await page.evaluate(async()=>{
   const actual=invoke,oldFocus=document.hasFocus,rows=[];let picks=[],writes=[];
   const flush=async()=>{for(let i=0;i<10;i++)await Promise.resolve();};
   invoke=(command,args={},timeout=0)=>{
    if(command==='pickArtwork')return new Promise((resolve,reject)=>picks.push({type:args.type,resolve,reject}));
    if(command==='searchArtwork')return Promise.resolve({candidates:[]});
    if(command==='state'||command==='plan')return Promise.resolve(structuredClone(snapshot));
    if(['saveEditor','commit','mediaImport','setArtwork','deleteArtwork','setBucket'].includes(command))writes.push({command,args:structuredClone(args)});
    return actual(command,args,timeout);
   };
   const modes=['current','cancel','current failure','other game','same EXE other directory','closed and reopened','root replaced','root alias','page retired','primary replaced','newer picker','newer cancelled picker','explicit delete','new search','draft reset','independent slots','background','save rebase','retired failure'];
   const setup=async()=>{closeEdit();silentRefreshClosed=false;document.hasFocus=()=>true;snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/OwnedArtworkPicker'};const game=mergedGames().find(g=>!g.mediaOnly&&g.primaryExecutable&&String(formalSteamAppId(g))==='570');if(!game)throw Error('picker-primary-fixture-missing');openEdit(game);await flush();clearNotice();noticeQueue.length=0;toastNotice.textContent='';picks=[];writes=[];return structuredClone(selectedActionGame);};
   try{for(const type of ['cover','long','wallpaper'])for(const mode of modes){
    const game=await setup();if(type==='wallpaper'){selectedActionGame.gameMedia.wallpaperMode='static';renderArtworkDraft();}const beforeSlot=JSON.stringify(artworkDraft[type]),beforeDirty=artworkDraft.dirty[type],expected='G:/H5Fixture/owned-'+type+'.jpg',task=promptArtworkPath(type);await flush();
    let second=null,rebaseChanged=null,backgroundMarkup=null;
    if(mode==='other game')reverseOpenOther();
    if(mode==='same EXE other directory'){const other=structuredClone(game);other.gameDirectory+='-other';openEdit(other);}
    if(mode==='closed and reopened'){closeEdit();openEdit(game);}
    if(mode==='root replaced')snapshot={...snapshot,dataRoot:'G:/DifferentArtworkPicker'};
    if(mode==='root alias')snapshot={...snapshot,dataRoot:'g:\\ownedartworkpicker\\'};
    if(mode==='page retired'){window.dispatchEvent(new Event('pagehide'));window.dispatchEvent(new Event('pageshow'));}
    if(mode==='primary replaced')selectedActionGame.primaryExecutable+='-replacement.exe';
    if(mode==='newer picker'||mode==='newer cancelled picker'){second=promptArtworkPath(type);await flush();picks[1].resolve(mode==='newer picker'?{path:expected+'-latest'}:{cancelled:true});await second;}
    if(mode==='explicit delete')clearArtwork(type);
    if(mode==='new search')await searchArtwork(type);
    if(mode==='draft reset')cancelArtworkEditor();
    if(mode==='independent slots'){const other=type==='cover'?'long':'cover';second=promptArtworkPath(other);await flush();picks[1].resolve({path:expected+'-other'});await second;}
    if(mode==='background'){document.hasFocus=()=>false;backgroundMarkup=document.getElementById('artwork-'+type+'-preview').innerHTML;}
    if(mode==='save rebase'){const submitted=editDraft;rebaseSavedEditor(submitted,structuredClone(submitted),structuredClone(submitted.game),{name:draftName(),steamId:draftSteamId()});rebaseChanged=editDraft!==submitted;}
    if(mode==='retired failure')reverseOpenOther();
    const retained=JSON.stringify(artworkDraft[type]),retainedDirty=artworkDraft.dirty[type];
    if(mode==='current failure'||mode==='retired failure')picks[0].reject(Error('owned-picker-failure-'+type));else picks[0].resolve(mode==='cancel'?{cancelled:true}:{path:expected});await task;await flush();
    const after=JSON.stringify(artworkDraft[type]),path=artworkDraft[type]?.asset?.path,notices=[toastNotice.textContent,...noticeQueue.map(n=>n.message)],backgroundKept=mode!=='background'||backgroundMarkup===document.getElementById('artwork-'+type+'-preview').innerHTML;
    const row={type,mode,path,dirty:artworkDraft.dirty[type],sameAsBefore:after===beforeSlot&&artworkDraft.dirty[type]===beforeDirty,sameAsRetained:after===retained&&artworkDraft.dirty[type]===retainedDirty,reported:notices.some(n=>n.includes('owned-picker-failure-'+type)),writes:writes.length,backgroundKept,rebaseChanged};
    if(mode==='background'){document.hasFocus=()=>true;resumeEditorArtworkPresentation();row.resumed=document.getElementById('artwork-'+type+'-preview').innerHTML!==backgroundMarkup;}
    rows.push(row);$('#artwork-search-modal').classList.add('hidden');
   }}finally{invoke=actual;document.hasFocus=oldFocus;closeEdit();}
   return rows;
  });progressMetrics.push({case:'artwork-picker-context',rows:pickerRows});
  check(pickerRows.length===57,'picker scene matrix incomplete');
  check(pickerRows.every(r=>r.writes===0&&(r.mode==='current'||r.mode==='root alias'||r.mode==='independent slots'||r.mode==='background'||r.mode==='save rebase'?r.dirty&&r.path==='G:/H5Fixture/owned-'+r.type+'.jpg'&&r.backgroundKept&&(r.mode!=='background'||r.resumed)&&(r.mode!=='save rebase'||r.rebaseChanged):r.mode==='newer picker'?r.dirty&&r.path==='G:/H5Fixture/owned-'+r.type+'.jpg-latest':r.mode==='current failure'?r.sameAsRetained&&r.reported:r.sameAsRetained&&!r.reported)),'unsaved artwork picker crossed editor/root/slot ownership, lost a current choice or misreported a retired failure');

 });

 await run('cancelling a file picker drains accepted playback changes',async()=>{

  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseChoose(10);reverseIO.finishPick({cancelled:true});});await page.evaluate(()=>reversePick);await page.waitForFunction(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===10);check(true,'queued playback resumes after cancelled picker');

 });

 await run('successful import drains pending edits for both original and newly selected game',async()=>{

  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseChoose(5);reverseOpenOther();reverseChoose(10);reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});await page.evaluate(()=>reversePick);await settle();const state=await page.evaluate(()=>({a:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'],b:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-1245620'],imports:reverseIO.calls.filter(c=>c.command==='mediaImport')}));check(state.imports[0].args.key==='steam-570'&&state.a.playback.start===5&&state.b.playback.start===10,'import or queued values crossed game ownership');check(state.a.bindingRevision===3,'queued playback used the revision returned by import');

 });

 await run('failed picker releases the queue and reports its failure',async()=>{

  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.failPick);await page.evaluate(()=>{reverseChoose(3);reverseIO.failPick(Error('fixture-picker-failed'));});await page.evaluate(()=>reversePick);await settle();check(await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===3),'picker failure stranded accepted edits');check(await page.evaluate(()=>[toastNotice.textContent,...noticeQueue.map(item=>item.message)].some(text=>text.includes('fixture-picker-failed'))),'picker failure was not reported');

 });

 await run('external revision conflicts are preserved without blind retries',async()=>{

  await page.evaluate(()=>{const media=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'];media.bindingRevision++;media.playback={start:20,end:50,fit:'cover'};reverseChoose(5);});await settle();check(await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===20),'external edit was overwritten');check(await page.evaluate(()=>reverseIO.calls.filter(c=>c.command==='mediaEdit').length===1&&[toastNotice.textContent,...noticeQueue.map(item=>item.message)].some(text=>text.includes('media-conflict'))),'conflict retried or silently disappeared');

 });

 await run('a late preparation reply cannot roll back a newly saved preference',async()=>{

  await page.evaluate(()=>{const base=invoke;invoke=async(command,args={})=>{if(command!=='mediaCreate')return base(command,args);const old=await base(command,args);return new Promise(resolve=>window.reverseReleasePreparation=()=>resolve(old));};window.reversePreparation=prepareSharedWallpaperPreview(selectedActionGame);});await page.waitForFunction(()=>window.reverseReleasePreparation);await page.evaluate(()=>reverseChoose(5));await settle();await page.evaluate(async()=>{reverseReleasePreparation();await reversePreparation;});check(await page.evaluate(()=>selectedActionGame.gameMedia.playback.start===5&&__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===5),'late metadata rolled the displayed preference back after saving');

 });



 async function beginReopenedSaveProbe(){
  await page.evaluate(()=>{
   const original=invoke;const state=__LIBRARY_H5_FIXTURE__.snapshot;
   window.reopenSaveProbe={key:keyFor(selectedActionGame),original:structuredClone(selectedActionGame),calls:[],pending:[]};
   const commit=args=>{for(const op of args.operations){
    if(op.command==='saveManualName'){state.manualOverrides||={items:{}};const key=normalizedUiPath(op.arguments.executable);state.manualOverrides.items[key]={...(state.manualOverrides.items[key]||{}),name:op.arguments.name};}
    else if(op.command==='setBucket'){state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}
    else throw Error('unexpected reopened save fixture operation '+op.command);
   }return structuredClone(state);};
   invoke=async(command,args={},timeoutMs=0)=>{if(command!=='saveEditor')return original(command,args,timeoutMs);reopenSaveProbe.calls.push(structuredClone(args));return new Promise((resolve,reject)=>reopenSaveProbe.pending.push({accept:()=>resolve(commit(args)),fail:()=>reject(Error('owned reopened save unavailable'))}));};
   reopenSaveProbe.name=value=>{const input=document.getElementById('identity-name');input.value=value;input.dispatchEvent(new Event('input'));};
   reopenSaveProbe.open=()=>openEdit(mergedGames().find(g=>keyFor(g)===reopenSaveProbe.key)||reopenSaveProbe.original);
  });
 }
 async function beginRootCardWriteProbe(){
  await page.evaluate(()=>{const game=structuredClone(selectedActionGame);closeEdit();snapshot={...snapshot,dataRoot:'G:/OwnedCardRoot'};__LIBRARY_H5_FIXTURE__.snapshot.dataRoot=snapshot.dataRoot;openEdit(game);});
  await beginReopenedSaveProbe();
  await page.evaluate(()=>{
   const actual=invoke,state=__LIBRARY_H5_FIXTURE__.snapshot;window.rootCardWriteProbe={root:normalizedUiPath(snapshot.dataRoot),draft:editDraft,key:keyFor(selectedActionGame),commits:[],commitPending:[]};const p=rootCardWriteProbe;
   invoke=(command,args={},timeout=0)=>command==='commit'?(p.commits.push(structuredClone(args)),new Promise((resolve,reject)=>p.commitPending.push({accept:()=>resolve(structuredClone(state)),fail:()=>reject(Error('owned old-root Steam rejected'))}))):actual(command,args,timeout);
   p.startSave=()=>{editDraft.bucket='non-game';p.task=persistEditDraft();};p.startJoin=()=>{p.task=joinEditedGameToSteam();};
   p.switchRoot=()=>{snapshot={...structuredClone(snapshot),dataRoot:'G:/ReplacementCardRoot',rootMarker:'replacement-card-root'};closeEdit();reopenSaveProbe.open();reopenSaveProbe.name('Replacement root unsaved name');p.currentDraft=editDraft;p.notice=libraryNotice.textContent;};
   p.result=()=>({root:snapshot.dataRoot,marker:snapshot.rootMarker,sameDraft:editDraft===p.currentDraft,name:document.getElementById('identity-name').value,saveCalls:reopenSaveProbe.calls.length,commitCalls:p.commits.length,saveTask:editorSaveTasks.get(p.key),steamTask:steamCommitTasks.get(p.key),operation:activeCardOperation(p.key)?.phase,notice:libraryNotice.textContent,active:editSaveInFlight});
  });
 }
 await run('accepted editor save cannot replace a different data roots snapshot or draft',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;p.startSave();p.switchRoot();reopenSaveProbe.pending[0].accept();const ok=await p.task;return {ok,...p.result(),nativeBucket:__LIBRARY_H5_FIXTURE__.snapshot.config.manualBuckets[p.key]?.bucket};});saveMetrics.push({case:'save-root-receipt',r});
  check(r.ok&&r.root==='G:/ReplacementCardRoot'&&r.marker==='replacement-card-root'&&r.sameDraft&&r.name==='Replacement root unsaved name'&&r.saveCalls===1&&r.nativeBucket==='non-game'&&!r.active,'old-root save replaced the current snapshot/draft or lost its accepted commit');
  const libraryActions=await page.evaluate(async()=>{
   const actual=invoke,account=refreshSteamAccountDiscovery,library=renderLibrary,rows=[];let pending=[],accountReads=0,throwDraw=false;
   const flush=async()=>{for(let i=0;i<14;i++)await Promise.resolve();};
   refreshSteamAccountDiscovery=async()=>{accountReads++;return {};};
   renderLibrary=(...args)=>{if(throwDraw){throwDraw=false;throw Error('owned library drawing failed');}return library(...args);};
   invoke=(command,args={})=>['prepare','openLibrary','scan','pickExecutable','pickDirectory','addGame','addRoot','removeRoot','removeGame'].includes(command)?new Promise((resolve,reject)=>pending.push({command,args:structuredClone(args),resolve,reject})):command==='steamCommitPreflight'?Promise.resolve({steamRunning:false}):actual(command,args);
   const setup=()=>{closeEdit();steamAddNotices.length=0;$('#steam-add-notice-modal').classList.add('hidden');clearNotice();setBusy(false);snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/OwnedReadRoot',readMarker:'original',config:{...__LIBRARY_H5_FIXTURE__.snapshot.config,roots:['G:/FixtureRoot'],manualGameDirectories:['G:/FixtureManual']}};manualRefreshProgress={phase:'idle',current:0,total:0};pending=[];accountReads=0;throwDraw=false;renderLibrary();};
   const start=command=>command==='prepare'?startNonBlockingRefresh():command==='openLibrary'?openLibraryView():document.getElementById('scan-library-button').click();
   const replace=()=>{snapshot={...snapshot,dataRoot:'G:/ReplacementReadRoot',readMarker:'replacement'};renderLibrary();};
   const reply=(index,root='G:/OwnedReadRoot',marker='accepted')=>{const p=pending[index],value={...structuredClone(snapshot),dataRoot:root,readMarker:marker,scrapeRequest:{queued:true,total:1}};p.resolve(workspaceBridgeResult(p.command,value));};
   const record=(command,mode)=>rows.push({command,mode,root:snapshot.dataRoot,marker:snapshot.readMarker,commands:pending.map(p=>p.command),accountReads,phase:manualRefreshProgress.phase,refreshDisabled:document.getElementById('refresh-artwork-button').disabled,scanDisabled:document.getElementById('scan-library-button').disabled,notices:[toastNotice.textContent,...noticeQueue.map(n=>n.message)],busy:!busy.classList.contains('hidden')});
   for(const command of ['prepare','openLibrary','scan'])for(const mode of ['current','old root','retired page','foreign reply','old failure','same alias','drawing failure']){
    console.debug('BUSY_BOUNDARY library '+command+' '+mode);setup();start(command);
    if(mode==='old root'||mode==='old failure')replace();
    if(mode==='retired page'){window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));snapshot={...snapshot,readMarker:'new-page'};renderLibrary();}
    if(mode==='same alias')snapshot={...snapshot,dataRoot:snapshot.dataRoot.toUpperCase().replaceAll('/',String.fromCharCode(92))};
    if(mode==='drawing failure')throwDraw=true;
    if(mode==='old failure')pending[0].reject(Error('owned retired library read failed'));else reply(0,mode==='foreign reply'?'G:/ForeignReadRoot':'G:/OwnedReadRoot');await flush();record(command,mode);
   }
   for(const [command,mode]of [['prepare','first bind'],['openLibrary','first bind'],['openLibrary','startup same bind'],['openLibrary','startup different bind']]){
    setup();delete snapshot.dataRoot;start(command);if(mode==='startup same bind')snapshot={...snapshot,dataRoot:'G:/OwnedReadRoot'};if(mode==='startup different bind')replace();reply(0);await flush();record(command,mode);
   }
   for(const command of ['prepare','openLibrary','scan'])for(const mode of ['duplicate current','new root success','new root failure']){
    console.debug('BUSY_BOUNDARY library owner '+command+' '+mode);setup();start(command);
    if(mode==='duplicate current'){start(command);for(let i=0;i<pending.length;i++)reply(i);await flush();record(command,mode);continue;}
    replace();start(command);const admitted=pending.length===2;
    if(mode==='new root failure')pending[0].reject(Error('owned retired library read failed'));else reply(0);
    await flush();const mid={root:snapshot.dataRoot,marker:snapshot.readMarker,refreshDisabled:document.getElementById('refresh-artwork-button').disabled,scanDisabled:document.getElementById('scan-library-button').disabled,phase:manualRefreshProgress.phase,notices:[toastNotice.textContent,...noticeQueue.map(n=>n.message)]};
    if(pending[1])reply(1,'G:/ReplacementReadRoot','newer');await flush();record(command,mode);rows.at(-1).admitted=admitted;rows.at(-1).mid=mid;
   }
   for(const command of ['pickExecutable','pickDirectory'])for(const mode of ['current','old root','cancelled','retired page']){
    setup();const add=command==='pickExecutable'?'addGame':'addRoot';pickAndAddLibraryPath(command,add,'Owned path','','Owned added');if(mode==='old root')replace();if(mode==='retired page'){window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));}
    pending[0].resolve(mode==='cancelled'?{cancelled:true}:{path:'G:/Chosen/game.exe'});await flush();if(pending[1])reply(1,snapshot.dataRoot,'path-added');await flush();record(command,mode);
   }
   setup();pickAndAddLibraryPath('pickExecutable','addGame','Old pick','','');replace();pickAndAddLibraryPath('pickExecutable','addGame','New pick','','');pending[0].resolve({path:'G:/Old/game.exe'});await flush();const beforeDuplicate=pending.length;pickAndAddLibraryPath('pickExecutable','addGame','Duplicate pick','','');await flush();const afterDuplicate=pending.length;
   for(const p of pending.filter(p=>p.command==='pickExecutable').slice(1))p.resolve({path:'G:/New/game.exe'});await flush();for(let i=0;i<pending.length;i++)if(pending[i].command==='addGame')reply(i,snapshot.dataRoot,'path-added');await flush();record('pickExecutable','new root picker');rows.at(-1).beforeDuplicate=beforeDuplicate;rows.at(-1).afterDuplicate=afterDuplicate;rows.at(-1).paths=pending.filter(p=>p.command==='addGame').map(p=>p.args.path);
   for(const command of ['removeRoot','removeGame'])for(const mode of ['current','old root']){
    setup();renderRoots();const selector=command==='removeRoot'?'#root-list button':'#manual-game-list button';document.querySelector(selector).click();if(mode==='old root')replace();closeConfirm(true);await flush();for(let i=0;i<pending.length;i++)reply(i,snapshot.dataRoot,'removed');await flush();record(command,mode);
   }
   invoke=actual;refreshSteamAccountDiscovery=account;renderLibrary=library;return rows;
  });saveMetrics.push({case:'library-action-context',rows:libraryActions});
  check(libraryActions.every(row=>row.mode==='old root'||row.mode==='old failure'||row.mode==='startup different bind'?row.root==='G:/ReplacementReadRoot'&&row.marker==='replacement'&&row.accountReads===0&&!row.notices.some(n=>n.includes('owned retired library'))&&(!row.command.startsWith('pick')&&!row.command.startsWith('remove')||row.commands.length===(row.command.startsWith('pick')?1:0)):row.mode==='retired page'?row.command.startsWith('pick')?row.commands.length===1:row.marker==='new-page'&&row.accountReads===0:row.mode==='foreign reply'?row.root==='G:/OwnedReadRoot'&&row.marker==='original':row.mode==='duplicate current'?row.commands.length===1&&row.marker==='accepted':row.mode==='new root success'||row.mode==='new root failure'?row.admitted&&row.mid.root==='G:/ReplacementReadRoot'&&row.mid.marker==='replacement'&&!row.mid.notices.some(n=>n.includes('owned retired library'))&&(row.command!=='scan'||row.mid.scanDisabled)&&(row.command!=='prepare'||row.mid.refreshDisabled&&row.mid.phase==='starting')&&row.root==='G:/ReplacementReadRoot'&&row.marker==='newer':row.mode==='cancelled'?row.commands.length===1:row.mode==='new root picker'?row.beforeDuplicate===2&&row.afterDuplicate===2&&row.paths.length===1&&row.paths[0]==='G:/New/game.exe':row.command.startsWith('pick')?row.commands.length===2&&row.marker==='path-added':row.command.startsWith('remove')?row.commands.length===1&&row.marker==='removed':row.marker==='accepted'&&(row.command!=='prepare'||row.phase==='queued')),'retired library read picker or confirmation affected another context or blocked its new owner');


  const confirmRows=await page.evaluate(async()=>{
   closeEdit();const actual=invoke,saved=structuredClone(snapshot),state=librarySidebarState(),savedState={...state},focus=document.hasFocus,rows=[];let writes=[];
   invoke=(command,args={},timeout=0)=>['state','plan'].includes(command)?Promise.resolve(workspaceBridgeResult(command,structuredClone(snapshot))):command==='steamCommitPreflight'?Promise.resolve({steamRunning:false}):['removeRoot','removeGame','commit','deleteFromSteam','closeSteam','closeWorkspace'].includes(command)?(writes.push({command,args}),Promise.resolve(structuredClone(snapshot))):actual(command,args,timeout);
   if(silentRefreshInFlight)await silentRefreshInFlight;
   const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))),roots=['G:/ConfirmFocus/SourceA','G:/ConfirmFocus/SourceB'],manual=['G:/ConfirmFocus/ManualA','G:/ConfirmFocus/ManualB'];
   const describe=element=>element?.matches('.root-row button')?(element.closest('#manual-game-list')?'manual:':'root:')+normalizedUiPath(element.closest('.root-row').querySelector('code').textContent):element?.id||element?.tagName||'';
   for(const page of ['sources','manual'])for(const mode of ['cancel after refresh','confirm after refresh','Back after refresh','path removed','path alias','other management page','root replaced','page retired','connected trigger','duplicate close','background close']){
    document.hasFocus=()=>true;if(confirmResolver)closeConfirm(false);snapshot={...structuredClone(saved),dataRoot:'G:/ConfirmFocus/Library',config:{...saved.config,roots:[...roots],manualGameDirectories:[...manual]}};state.query='';$('#library-search').value='';renderLibrary();librarySidebarPage(page);const list=page==='sources'?'#root-list':'#manual-game-list',paths=page==='sources'?roots:manual,target=document.querySelectorAll(list+' .root-row button')[1];focusWorkspaceElement(target);writes=[];let settlements=0,result;
    const pending=askConfirm('Owned confirmation','Current path action','确认').then(value=>{settlements++;result=value;});let expected=(page==='sources'?'root:':'manual:')+normalizedUiPath(paths[1]);
    if(mode==='path removed'){snapshot.config[page==='sources'?'roots':'manualGameDirectories']=paths.slice(0,1);expected=page==='sources'?'pick-root-button':'pick-game-button';}
    if(mode==='path alias')snapshot.config[page==='sources'?'roots':'manualGameDirectories']=paths.map(p=>p.toUpperCase().replaceAll('/',String.fromCharCode(92)));
    if(mode==='other management page'){librarySidebarPage(page==='sources'?'manual':'sources',false);expected=page==='sources'?'pick-game-button':'pick-root-button';}
    if(mode==='root replaced')snapshot={...snapshot,dataRoot:'G:/ConfirmFocus/ReplacementLibrary'};
    if(mode==='page retired'){window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));}
    if(!['connected trigger','background close'].includes(mode))renderLibrary();
    await Promise.resolve();const settledBeforeClose=settlements,focusBeforeClose=describe(document.activeElement);
    let marker;if(mode==='background close'){marker=document.createElement('button');marker.textContent='external confirm focus';document.body.appendChild(marker);marker.focus();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
    if(mode==='Back after refresh'){controllerBackLastAt=0;routeWorkspaceAction('back');}else document.getElementById(mode==='confirm after refresh'?'confirm-confirm':'confirm-cancel').click();await pending;await tick();
    const after=describe(document.activeElement),inside=$('#library-view').contains(document.activeElement);let duplicateKept=true;if(mode==='duplicate close'){const next=$('#library-management-back');focusWorkspaceElement(next);closeConfirm(true);duplicateKept=document.activeElement===next;}
    rows.push({page,mode,expected,after,inside,visible:isVisuallyPresent(document.activeElement),result,settlements,settledBeforeClose,focusBeforeClose,writes:writes.length,duplicateKept,backgroundKept:marker?document.activeElement===marker:null});if(marker)marker.remove();
   }
   document.hasFocus=()=>true;snapshot=structuredClone(saved);Object.assign(state,savedState);librarySidebarPage('library',false);$('#library-search').value=state.query;renderLibrary();
   for(const accept of [false,true]){const game=mergedGames()[0];openEdit(game);const button=$('#edit-back-button');focusWorkspaceElement(button);const pending=askConfirm('Owned editor confirm','No library path',accept?'确认':'取消');await tick();const modalFocus=!!document.activeElement?.closest('#confirm-modal');document.getElementById(accept?'confirm-confirm':'confirm-cancel').click();const result=await pending;await tick();rows.push({page:'editor',mode:accept?'confirm':'cancel',result,expectedResult:accept,modalFocus,kept:document.activeElement===button,visible:isVisuallyPresent(button)});closeEdit();}
   for(const mode of ['ordinary entry','immediate dropdown','background entry']){
    document.hasFocus=()=>true;openEdit(mergedGames()[0]);let expected,marker;
    if(mode==='immediate dropdown'){const dropdown=workspaceDropdownForSelect($('#edit-bucket-select'));openWorkspaceDropdown(dropdown);expected=document.activeElement;}
    if(mode==='background entry'){marker=document.createElement('button');marker.textContent='external editor focus';document.body.appendChild(marker);marker.focus();expected=marker;document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
    await tick();rows.push({page:'editor-entry',mode,kept:mode==='ordinary entry'?!!document.activeElement?.closest('#edit-modal'):document.activeElement===expected,dropdown:mode==='immediate dropdown'?!!openWorkspaceDropdownState:null});
    if(marker)marker.remove();document.hasFocus=()=>true;closeWorkspaceDropdown(false);closeEdit();
   }
   document.hasFocus=focus;invoke=actual;snapshot=saved;Object.assign(state,savedState);$('#library-search').value=state.query;librarySidebarPage(savedState.page,false);renderLibrary();return rows;
  });saveMetrics.push({case:'confirmation-return-focus',rows:confirmRows});
  check(confirmRows.every(row=>row.page==='editor-entry'?row.kept&&(row.mode!=='immediate dropdown'||row.dropdown):row.page==='editor'?row.modalFocus&&row.kept&&row.visible&&row.result===row.expectedResult:row.settlements===1&&row.writes===0&&row.result===(row.mode==='confirm after refresh')&&row.duplicateKept&&(row.mode==='background close'?row.backgroundKept:row.mode==='page retired'?row.settledBeforeClose===1&&row.after===row.focusBeforeClose:row.mode==='root replaced'?!row.inside:row.after===row.expected&&row.visible)),'Confirmation return lost current path focus or borrowed retired/background ownership');

 });
 await run('accepted inline Steam join cannot replace a different data roots snapshot',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;p.startJoin();p.switchRoot();p.commitPending[0].accept();const ok=await p.task;return {ok,...p.result()};});saveMetrics.push({case:'commit-root-receipt',r});
  check(r.ok&&r.root==='G:/ReplacementCardRoot'&&r.marker==='replacement-card-root'&&r.sameDraft&&r.name==='Replacement root unsaved name'&&r.commitCalls===1&&r.steamTask.phase==='completed'&&!r.operation,'old-root Steam receipt replaced the current library or leaked its completion badge');
 });
 await run('a stale editor cannot submit a new save into another data root',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;editDraft.bucket='non-game';snapshot={...snapshot,dataRoot:'G:/ReplacementCardRoot'};p.currentDraft=editDraft;p.startSave();reopenSaveProbe.pending[0]?.accept();const ok=await p.task;return {ok,...p.result()};});saveMetrics.push({case:'save-root-stale-submit',r});
  check(!r.ok&&r.saveCalls===0&&r.sameDraft&&!r.active&&r.root==='G:/ReplacementCardRoot','stale editor submitted a save against another data root');
 });
 await run('a stale editor cannot join a game into another data root',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;snapshot={...snapshot,dataRoot:'G:/ReplacementCardRoot'};p.currentDraft=editDraft;p.startJoin();p.commitPending[0]?.accept();const ok=await p.task;return {ok,...p.result()};});saveMetrics.push({case:'commit-root-stale-submit',r});
  check(!r.ok&&r.commitCalls===0&&r.sameDraft&&r.root==='G:/ReplacementCardRoot','stale editor submitted a Steam join against another data root');
 });
 await run('failed old-root save remains with its task without showing an error on the new root',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;p.startSave();p.switchRoot();reopenSaveProbe.pending[0].fail();const ok=await p.task;const r={ok,...p.result(),noticeKept:libraryNotice.textContent===p.notice};snapshot={...snapshot,dataRoot:'G:/OwnedCardRoot'};r.oldRootOperation=activeCardOperation(p.key)?.phase;return r;});saveMetrics.push({case:'save-root-failure',r});
  check(!r.ok&&r.root==='G:/ReplacementCardRoot'&&r.saveTask.phase==='failed'&&r.noticeKept&&!r.operation&&r.oldRootOperation==='failed'&&!r.active&&r.saveCalls===1,'old-root save failure appeared on the new root or lost its own failure state');
 });
 await run('a new root editor cannot inherit an old-root pending draft or save marker',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;p.startSave();p.switchRoot();const pending={draftRoot:editDraft.mediaDataRoot,expected:normalizedUiPath(snapshot.dataRoot),oldDraft:editDraft===p.draft,operation:activeCardOperation(p.key)?.phase,disabled:document.getElementById('edit-save-content').disabled};reopenSaveProbe.pending[0].accept();await p.task;return pending;});saveMetrics.push({case:'save-root-pending-draft',r});
  check(r.draftRoot===r.expected&&!r.oldDraft&&!r.operation&&!r.disabled,'new-root editor inherited the old-root pending draft or save badge');
 });
 await run('Steam progress preserves its admitted data root while another root is visible',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;p.startJoin();p.switchRoot();reverseEmit({event:'steam-commit-progress',directories:[p.key],phase:'running'});const pending={owner:steamCommitTasks.get(p.key)?.dataRoot,expected:p.root,operation:activeCardOperation(p.key)?.phase};p.commitPending[0].accept();await p.task;snapshot={...snapshot,dataRoot:'G:/OwnedCardRoot'};return {...pending,oldRootOperation:activeCardOperation(p.key)?.phase,calls:p.commits.length};});saveMetrics.push({case:'commit-root-progress-owner',r});
  check(r.owner===r.expected&&!r.operation&&r.oldRootOperation==='completed'&&r.calls===1,'Steam progress lost its task root or appeared on another data root');
 });
 await run('same-root aliases keep an accepted editor save on its existing owner',async()=>{
  await beginRootCardWriteProbe();const r=await page.evaluate(async()=>{const p=rootCardWriteProbe;p.startSave();snapshot={...snapshot,dataRoot:snapshot.dataRoot.replaceAll('/','\\').toUpperCase()};reopenSaveProbe.pending[0].accept();const ok=await p.task;return {ok,root:normalizedUiPath(snapshot.dataRoot),expected:p.root,bucket:editDraft.originalBucket,calls:reopenSaveProbe.calls.length};});
  check(r.ok&&r.root===r.expected&&r.bucket==='non-game'&&r.calls===1,'same-root alias prevented accepted editor rebase or replayed the save');
 });
 await run('editor reopening a pending save restores submission and advances its committed baseline',async()=>{
  await beginReopenedSaveProbe();
  const opened=await page.evaluate(()=>{reopenSaveProbe.name('Reopened submitted name');editDraft.bucket='non-game';window.reopenedSave=persistEditDraft();routeWorkspaceAction('back');reopenSaveProbe.open();return {name:document.getElementById('identity-name').value,bucket:editDraft.bucket,disabled:document.getElementById('edit-save-content').disabled,calls:reopenSaveProbe.calls.length};});
  check(opened.name==='Reopened submitted name'&&opened.bucket==='non-game'&&opened.disabled&&opened.calls===1,'reopening pending save discarded its submitted values');
  const finished=await page.evaluate(async()=>{reopenSaveProbe.pending[0].accept();const ok=await reopenedSave;const again=await persistEditDraft();return {ok,again,name:document.getElementById('identity-name').value,baseline:editDraft.original.override?.name,bucket:editDraft.originalBucket,task:editorSaveTasks.has(reopenSaveProbe.key),calls:reopenSaveProbe.calls.length};});saveMetrics.push({case:'save-reopen-pending',opened,finished});
  check(finished.ok&&finished.again&&finished.name==='Reopened submitted name'&&finished.baseline===finished.name&&finished.bucket==='non-game'&&!finished.task&&finished.calls===1,'reopened completed save retained stale baseline or replayed accepted operations');
 });
 await run('failed editor submission survives Back and another card until explicit retry',async()=>{
  await beginReopenedSaveProbe();
  await page.evaluate(()=>{editDraft.bucket='non-game';window.reopenedSave=persistEditDraft();routeWorkspaceAction('back');reverseOpenOther();});
  const failed=await page.evaluate(async()=>{const other=keyFor(selectedActionGame);reopenSaveProbe.pending[0].fail();const ok=await reopenedSave;return {ok,otherKept:keyFor(selectedActionGame)===other,phase:editorSaveTasks.get(reopenSaveProbe.key)?.phase,calls:reopenSaveProbe.calls.length};});
  check(!failed.ok&&failed.otherKept&&failed.phase==='failed'&&failed.calls===1,'background failed save replaced another card or retried itself');
  const opened=await page.evaluate(()=>{reopenSaveProbe.open();return {bucket:editDraft.bucket,status:document.getElementById('edit-status').textContent,disabled:document.getElementById('edit-save-content').disabled,calls:reopenSaveProbe.calls.length};});
  check(opened.bucket==='non-game'&&opened.status.includes('保存失败')&&!opened.disabled&&opened.calls===1,'reopening failed save lost the requested changes or retried without intent');
  const done=await page.evaluate(async()=>{window.retryReopenedSave=persistEditDraft();reopenSaveProbe.pending[1].accept();const ok=await retryReopenedSave;return {ok,calls:reopenSaveProbe.calls,bucket:editDraft.originalBucket,task:editorSaveTasks.has(reopenSaveProbe.key),active:editSaveInFlight};});saveMetrics.push({case:'save-reopen-failure',failed,opened,done});
  check(done.ok&&done.calls.length===2&&JSON.stringify(done.calls[0].operations)===JSON.stringify(done.calls[1].operations)&&done.bucket==='non-game'&&!done.task&&!done.active,'explicit reopened retry lost submitted operations or its release');
 });
 await run('reopened pending save preserves later edits without replaying committed fields',async()=>{
  await beginReopenedSaveProbe();
  const done=await page.evaluate(async()=>{reopenSaveProbe.name('Reopened baseline');editDraft.bucket='non-game';window.reopenedSave=persistEditDraft();closeEdit();reopenSaveProbe.open();reopenSaveProbe.name('Later reopened change');reopenSaveProbe.pending[0].accept();await reopenedSave;return {name:document.getElementById('identity-name').value,baseline:editDraft.original.override?.name,bucket:editDraft.originalBucket,dirty:editDraft.identityDirty};});
  check(done.name==='Later reopened change'&&done.baseline==='Reopened baseline'&&done.bucket==='non-game'&&done.dirty,'reopened save lost later input or failed to advance committed fields');
  const retry=await page.evaluate(async()=>{window.laterReopenedSave=persistEditDraft();const args=structuredClone(reopenSaveProbe.calls[1]);reopenSaveProbe.pending[1].accept();const ok=await laterReopenedSave;return {ok,args,name:editDraft.original.override?.name,calls:reopenSaveProbe.calls.length};});saveMetrics.push({case:'save-reopen-later-edits',done,retry});
  check(retry.ok&&retry.name==='Later reopened change'&&retry.calls===2&&retry.args.operations.length===1&&retry.args.operations[0].command==='saveManualName'&&retry.args.operations[0].arguments.name==='Later reopened change','reopened second save replayed a committed bucket or lost later input');
 });
 await run('retained editor submission cannot replace a different primary executable at the same card',async()=>{
  await beginReopenedSaveProbe();
  const guarded=await page.evaluate(async()=>{editDraft.bucket='non-game';window.reopenedSave=persistEditDraft();closeEdit();reopenSaveProbe.pending[0].fail();await reopenedSave;const replacement={...reopenSaveProbe.original,primaryExecutable:reopenSaveProbe.original.primaryExecutable+'.replacement'};openEdit(replacement);return {current:selectedActionGame.primaryExecutable,expected:replacement.primaryExecutable,bucket:editDraft.bucket,calls:reopenSaveProbe.calls.length};});saveMetrics.push({case:'save-reopen-executable-guard',guarded});
  check(guarded.current===guarded.expected&&guarded.bucket!=='non-game'&&guarded.calls===1,'retained failed submission crossed to a different executable or replayed automatically');
 });


 await run('editor cards sharing an executable keep distinct drafts and save ownership',async()=>{
  await beginReopenedSaveProbe();
  const opened=await page.evaluate(()=>{editDraft.bucket='non-game';window.reopenedSave=persistEditDraft();closeEdit();reopenSaveProbe.open();const other={...reopenSaveProbe.original,gameDirectory:reopenSaveProbe.original.gameDirectory+'-distinct-card'};openEdit(other);return {key:keyFor(selectedActionGame),expected:keyFor(other),bucket:editDraft.bucket,calls:reopenSaveProbe.calls.length};});
  check(opened.key===opened.expected&&opened.bucket!=='non-game'&&opened.calls===1,'two game cards sharing one executable reused the same submitted draft');
  const completed=await page.evaluate(async()=>{const key=keyFor(selectedActionGame);reopenSaveProbe.pending[0].accept();const ok=await reopenedSave;return {ok,keyKept:keyFor(selectedActionGame)===key,task:editorSaveTasks.has(reopenSaveProbe.key),calls:reopenSaveProbe.calls.length};});saveMetrics.push({case:'save-reopen-card-key-guard',opened,completed});
  check(completed.ok&&completed.keyKept&&!completed.task&&completed.calls===1,'first card completion replaced the same-executable second card or retained its save owner');
 });


 await run('reopened submission keeps a known newer wallpaper before delayed preview replies',async()=>{
  await beginReopenedSaveProbe();
  const opened=await page.evaluate(async()=>{const owner=captureSharedWallpaperWrite();editDraft.bucket='non-game';window.reopenedSave=persistEditDraft();closeEdit();const changed=await updateSharedWallpaper('mode',{mode:'static'},owner);const newer=structuredClone(snapshot.gameMedia.entries[owner.entry.key]);const actual=invoke;reopenSaveProbe.heldCreates=[];invoke=async(command,args={},timeoutMs=0)=>command==='mediaCreate'?new Promise(resolve=>reopenSaveProbe.heldCreates.push({resolve,args})):actual(command,args,timeoutMs);reopenSaveProbe.open();return {changed,newer,current:structuredClone(selectedActionGame.gameMedia),bucket:editDraft.bucket,reads:reopenSaveProbe.heldCreates.length};});
  check(opened.changed&&opened.bucket==='non-game'&&opened.reads===1&&opened.current.bindingRevision===opened.newer.bindingRevision&&opened.current.wallpaperMode==='static','reopened submission replaced a known newer wallpaper entry before its preview refresh');
  const completed=await page.evaluate(async()=>{reopenSaveProbe.pending[0].accept();const ok=await reopenedSave;reopenSaveProbe.heldCreates[0].resolve(structuredClone(reopenSaveProbe.original.gameMedia));await new Promise(requestAnimationFrame);return {ok,current:structuredClone(selectedActionGame.gameMedia),stored:structuredClone(snapshot.gameMedia.entries[selectedActionGame.gameMedia.key]),bucket:editDraft.originalBucket,calls:reopenSaveProbe.calls.length};});saveMetrics.push({case:'save-reopen-newer-media',opened,completed});
  check(completed.ok&&completed.current.wallpaperMode==='static'&&completed.current.bindingRevision===opened.newer.bindingRevision&&completed.stored.bindingRevision===opened.newer.bindingRevision&&completed.bucket==='non-game'&&completed.calls===1,'late preview or save receipt rolled back the reopened newer wallpaper or replayed the save');
 });


 async function beginSaveCardReceiptProbe(changedPrimary=false){
  await page.evaluate(changedPrimary=>{
   const state=__LIBRARY_H5_FIXTURE__.snapshot,base=invoke,first=structuredClone(state.library.games[0]);
   const second={...first,gameDirectory:first.gameDirectory+'-second-card',directoryName:'Receipt second card',primaryExecutable:changedPrimary?first.primaryExecutable+'.old':first.primaryExecutable};
   state.library.games.push(second);snapshot=structuredClone(state);const key=keyFor(second);openEdit(mergedGames().find(g=>keyFor(g)===key));
   window.saveCardReceiptProbe={key,firstKey:keyFor(first),sharedExecutable:first.primaryExecutable,calls:[],state};
   invoke=async(command,args={},timeoutMs=0)=>{
    if(command!=='saveEditor')return base(command,args,timeoutMs);
    saveCardReceiptProbe.calls.push(structuredClone(args));
    return new Promise(resolve=>saveCardReceiptProbe.accept=(alter='')=>{
     for(const op of args.operations){
      if(op.command==='setBucket'){state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}
      else if(op.command==='setPrimary'){const game=state.library.games.find(g=>keyFor(g)===normalizedUiPath(op.arguments.gameDirectory));if(!game)throw Error('owned primary target missing');game.primaryExecutable=op.arguments.executable;}
      else throw Error('unexpected receipt fixture operation '+op.command);
     }
     if(alter==='missing')state.library.games=state.library.games.filter(g=>keyFor(g)!==key);
     if(alter==='different-executable')state.library.games.find(g=>keyFor(g)===key).primaryExecutable=first.primaryExecutable+'.replacement';
     resolve(structuredClone(state));
    });
   };
  },changedPrimary);
 }
 await run('editor save receipt selects its card when two directories share one executable',async()=>{
  await beginSaveCardReceiptProbe();
  const row=await page.evaluate(async()=>{editDraft.bucket='non-game';const pending=persistEditDraft();saveCardReceiptProbe.accept();const ok=await pending;return {ok,expected:saveCardReceiptProbe.key,current:keyFor(selectedActionGame),baseline:keyFor(editDraft.original),bucket:editDraft.originalBucket,firstBucket:manualBucket(mergedGames().find(g=>keyFor(g)===saveCardReceiptProbe.firstKey)),calls:saveCardReceiptProbe.calls.length,task:editorSaveTasks.has(saveCardReceiptProbe.key)};});saveMetrics.push({case:'save-receipt-shared-exe',row});
  check(row.ok&&row.current===row.expected&&row.baseline===row.expected&&row.bucket==='non-game'&&row.firstBucket!=='non-game'&&row.calls===1&&!row.task,'save receipt borrowed the first same-executable card as its committed baseline');
 });
 await run('changed primary receipt stays on its card when the selected executable is shared',async()=>{
  await beginSaveCardReceiptProbe(true);
  const row=await page.evaluate(async()=>{selectedActionGame.primaryExecutable=saveCardReceiptProbe.sharedExecutable;editDraft.primaryDirty=true;editDraft.bucket='non-game';const pending=persistEditDraft();saveCardReceiptProbe.accept();const ok=await pending;const calls=saveCardReceiptProbe.calls;return {ok,current:keyFor(selectedActionGame),expected:saveCardReceiptProbe.key,baseline:keyFor(editDraft.original),primary:editDraft.original.primaryExecutable,expectedPrimary:saveCardReceiptProbe.sharedExecutable,bucket:editDraft.originalBucket,calls};});saveMetrics.push({case:'save-receipt-changed-primary',row});
  check(row.ok&&row.current===row.expected&&row.baseline===row.expected&&row.primary===row.expectedPrimary&&row.bucket==='non-game'&&row.calls.length===1&&row.calls[0].operations[0].command==='setPrimary','primary save receipt rebased a different card with the newly selected executable');
 });
 await run('missing editor card receipt cannot borrow another card with the same executable',async()=>{
  await beginSaveCardReceiptProbe();
  const row=await page.evaluate(async()=>{editDraft.bucket='non-game';const pending=persistEditDraft();saveCardReceiptProbe.accept('missing');const ok=await pending;return {ok,current:keyFor(selectedActionGame),expected:saveCardReceiptProbe.key,baseline:keyFor(editDraft.original),receiptHasOwner:mergedGames().some(g=>keyFor(g)===saveCardReceiptProbe.key),calls:saveCardReceiptProbe.calls.length,task:editorSaveTasks.has(saveCardReceiptProbe.key)};});saveMetrics.push({case:'save-receipt-missing-owner',row});
  check(row.ok&&!row.receiptHasOwner&&row.current===row.expected&&row.baseline===row.expected&&row.calls===1&&!row.task,'missing card receipt replaced the editor with a same-executable neighbour');
 });
 await run('changed executable in an editor receipt cannot borrow another matching card',async()=>{
  await beginSaveCardReceiptProbe();
  const row=await page.evaluate(async()=>{editDraft.bucket='non-game';const original=selectedActionGame.primaryExecutable;const pending=persistEditDraft();saveCardReceiptProbe.accept('different-executable');const ok=await pending;return {ok,current:keyFor(selectedActionGame),expected:saveCardReceiptProbe.key,primary:selectedActionGame.primaryExecutable,original,baseline:keyFor(editDraft.original),receiptPrimary:mergedGames().find(g=>keyFor(g)===saveCardReceiptProbe.key)?.primaryExecutable,calls:saveCardReceiptProbe.calls.length};});saveMetrics.push({case:'save-receipt-executable-mismatch',row});
  check(row.ok&&row.current===row.expected&&row.primary===row.original&&row.baseline===row.expected&&row.receiptPrimary!==row.original&&row.calls===1,'mismatched receipt adopted another card or a different executable');
 });


 async function beginSavePresentationProbe(){
  await page.evaluate(()=>{
   const base=invoke,post=window.chrome.webview.postMessage;
   window.savePresentationProbe={key:keyFor(selectedActionGame),calls:[],diagnostics:[],throwEditor:false,throwLibrary:false,editorThrows:0,libraryThrows:0};
   const editor=renderEditDraft,library=renderLibrary;
   renderEditDraft=(...args)=>{if(savePresentationProbe.throwEditor){savePresentationProbe.throwEditor=false;savePresentationProbe.editorThrows++;throw Error('owned editor presentation unavailable');}return editor(...args);};
   renderLibrary=(...args)=>{if(savePresentationProbe.throwLibrary){savePresentationProbe.throwLibrary=false;savePresentationProbe.libraryThrows++;throw Error('owned card presentation unavailable');}return library(...args);};
   window.chrome.webview.postMessage=function(message){if(message.command==='uiError')savePresentationProbe.diagnostics.push(structuredClone(message.arguments));return post.call(this,message);};
   invoke=async(command,args={},timeoutMs=0)=>{if(command!=='saveEditor')return base(command,args,timeoutMs);savePresentationProbe.calls.push(structuredClone(args));const state=__LIBRARY_H5_FIXTURE__.snapshot;const commit=()=>{for(const op of args.operations){if(op.command!=='setBucket')throw Error('unexpected presentation probe operation');state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}return structuredClone(state);};if(savePresentationProbe.calls.length>1)return commit();return new Promise(resolve=>savePresentationProbe.accept=()=>resolve(commit()));};
   editDraft.bucket='non-game';
  });
 }
 await run('editor presentation failure before submit does not strand or discard the accepted save',async()=>{
  await beginSavePresentationProbe();
  const initial=await page.evaluate(async()=>{savePresentationProbe.throwEditor=true;window.presentationSave=persistEditDraft().then(ok=>({ok}),error=>({rejected:String(error)}));await Promise.resolve();return {calls:savePresentationProbe.calls.length,phase:editorSaveTasks.get(savePresentationProbe.key)?.phase,throws:savePresentationProbe.editorThrows};});
  check(initial.calls===1&&initial.phase==='running'&&initial.throws===1,'editor presentation exception prevented save submission and stranded its running task');
  const result=await page.evaluate(async()=>{savePresentationProbe.accept();const outcome=await presentationSave;return {outcome,calls:savePresentationProbe.calls.length,task:editorSaveTasks.has(savePresentationProbe.key),active:editSaveInFlight,bucket:editDraft.originalBucket,diagnostics:savePresentationProbe.diagnostics};});saveMetrics.push({case:'save-start-presentation-failure',initial,result});
  check(result.outcome.ok&&!result.task&&!result.active&&result.calls===1&&result.bucket==='non-game'&&result.diagnostics.some(x=>x.message==='owned editor presentation unavailable'),'save start presentation failure lost result, release or original diagnostic');
 });
 await run('card drawing failure after save cannot turn a committed operation into failed work',async()=>{
  await beginSavePresentationProbe();
  const result=await page.evaluate(async()=>{const promise=persistEditDraft();savePresentationProbe.throwLibrary=true;savePresentationProbe.accept();const ok=await promise;return {ok,calls:savePresentationProbe.calls.length,throws:savePresentationProbe.libraryThrows,bucket:editDraft.originalBucket,task:editorSaveTasks.get(savePresentationProbe.key)?.phase,active:editSaveInFlight,diagnostics:savePresentationProbe.diagnostics,pending:silentRenderPending};});saveMetrics.push({case:'save-card-presentation-failure',result});
  check(result.ok&&result.calls===1&&result.throws===1&&result.bucket==='non-game'&&!result.task&&!result.active&&result.pending&&result.diagnostics.some(x=>x.message==='owned card presentation unavailable'),'successful native save was reported failed or left stale baseline after card rendering failed');
  const retry=await page.evaluate(async()=>({ok:await persistEditDraft(),calls:savePresentationProbe.calls.length,task:editorSaveTasks.has(savePresentationProbe.key)}));
  check(retry.ok&&retry.calls===1&&!retry.task,'recovering card presentation replayed already committed save operations');
 });
 await run('editor drawing failure after save preserves committed baseline and deferred display',async()=>{
  await beginSavePresentationProbe();
  const result=await page.evaluate(async()=>{const promise=persistEditDraft();savePresentationProbe.throwEditor=true;savePresentationProbe.accept();const ok=await promise;return {ok,calls:savePresentationProbe.calls.length,throws:savePresentationProbe.editorThrows,bucket:editDraft.originalBucket,task:editorSaveTasks.get(savePresentationProbe.key)?.phase,active:editSaveInFlight,diagnostics:savePresentationProbe.diagnostics,pending:cardOperationEditorPending};});saveMetrics.push({case:'save-editor-presentation-failure',result});
  check(result.ok&&result.calls===1&&result.throws===1&&result.bucket==='non-game'&&!result.task&&!result.active&&result.pending&&result.diagnostics.some(x=>x.message==='owned editor presentation unavailable'),'editor drawing exception converted saved work into failed task or lost recovery demand');
  const recovered=await page.evaluate(async()=>{refreshCardOperationState();const ok=await persistEditDraft();return {ok,pending:cardOperationEditorPending,title:document.getElementById('edit-title').textContent,calls:savePresentationProbe.calls.length,task:editorSaveTasks.has(savePresentationProbe.key)};});saveMetrics.at(-1).recovered=recovered;
  check(recovered.ok&&!recovered.pending&&recovered.calls===1&&!recovered.task,'deferred editor recovery retried writes or kept its failed display demand');
 });


 async function beginCommitPresentationProbe(){
  await page.evaluate(()=>{steamAddNotices.length=0;closeSteamAddNotice();clearNotice();});
  await beginSavePresentationProbe();
  await page.evaluate(()=>{const base=invoke;savePresentationProbe.commits=[];invoke=async(command,args={},timeoutMs=0)=>{if(command!=='commit')return base(command,args,timeoutMs);savePresentationProbe.commits.push(structuredClone(args));return new Promise(resolve=>savePresentationProbe.acceptCommit=()=>resolve(workspaceBridgeResult('commit',fixtureSteamCommitResult(__LIBRARY_H5_FIXTURE__.snapshot,args.selectedGameDirectories))));};});
 }
 await run('inline Steam join is submitted despite its initial editor display failure',async()=>{
  await beginCommitPresentationProbe();
  const started=await page.evaluate(async()=>{savePresentationProbe.throwEditor=true;window.presentationJoin=joinEditedGameToSteam().then(ok=>({ok}),error=>({rejected:String(error)}));await Promise.resolve();return {calls:savePresentationProbe.commits.length,phase:steamCommitTasks.get(savePresentationProbe.key)?.phase,throws:savePresentationProbe.editorThrows};});
  check(started.calls===1&&started.phase==='queued'&&started.throws===1,'initial editor drawing prevented inline Steam join and stranded its queue marker');
  const result=await page.evaluate(async()=>{savePresentationProbe.acceptCommit();const outcome=await presentationJoin;return {outcome,calls:savePresentationProbe.commits.length,phase:steamCommitTasks.get(savePresentationProbe.key)?.phase,diagnostics:savePresentationProbe.diagnostics};});saveMetrics.push({case:'commit-start-display-failure',started,result});
  check(result.outcome.ok&&result.calls===1&&result.phase==='completed'&&result.diagnostics.some(x=>x.message==='owned editor presentation unavailable'),'inline Steam result lost its successful outcome or original display diagnostic');
 });
 await run('inline Steam commit success is not changed to failed by card drawing',async()=>{
  await beginCommitPresentationProbe();
  const result=await page.evaluate(async()=>{const joined=joinEditedGameToSteam();savePresentationProbe.throwLibrary=true;savePresentationProbe.acceptCommit();const ok=await joined;return {ok,calls:savePresentationProbe.commits.length,phase:steamCommitTasks.get(savePresentationProbe.key)?.phase,throws:savePresentationProbe.libraryThrows,pending:silentRenderPending,diagnostics:savePresentationProbe.diagnostics};});saveMetrics.push({case:'commit-complete-display-failure',result});
  check(result.ok&&result.calls===1&&result.phase==='completed'&&result.throws===1&&result.pending&&result.diagnostics.some(x=>x.message==='owned card presentation unavailable'),'successful Steam commit was marked failed or silently lost its card rendering error');
  await beginCommitPresentationProbe();
  const partial=await page.evaluate(async()=>{const warning='另有 1 个 Steam 账户暂时无法读取，本次未处理这些账户，原始数据已保留。';__LIBRARY_H5_FIXTURE__.snapshot.steamPlan.accountReadWarning=warning;const joined=joinEditedGameToSteam();savePresentationProbe.acceptCommit();const ok=await joined;const task=steamCommitTasks.get(savePresentationProbe.key);return {ok,calls:savePresentationProbe.commits.length,phase:task?.phase,label:task?.label,partial:task?.partialAccounts,message:document.getElementById('steam-add-notice-message').textContent,title:document.getElementById('steam-add-notice-title').textContent};});saveMetrics.push({case:'partial-account-warning',partial});
  const batch=await page.evaluate(async()=>{const rows=[];for(const command of ['commit','refreshSteamArtwork','deleteFromSteam']){
   steamAddNotices.length=0;closeSteamAddNotice();clearNotice();const result=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);delete result.steamPlan.accountReadWarning;
   const warning='另有 1 个 Steam 账户暂时无法读取';result.steamAccountWriteWarning=warning;
   let calls=0;invoke=async()=>{calls++;return result;};const ok=await runBusy(command,'owned partial operation','',{},'处理完成');
   const message=command==='commit'?document.getElementById('steam-add-notice-message').textContent:toastNotice.textContent;
   const error=command==='commit'?document.getElementById('steam-add-notice-title').textContent.includes('未完成'):toastNotice.classList.contains('error');
   rows.push({command,ok,calls,message,error,passed:ok&&calls===1&&message.includes(warning)&&error});
  }return rows;});saveMetrics.push({case:'partial-account-batch-warning',batch});
  check(partial.ok&&partial.calls===1&&partial.phase==='completed'&&partial.partial&&partial.label==='部分完成'&&partial.message.includes('另有 1 个')&&partial.title.includes('未完成'),'partial Steam account success was presented as complete success');
  check(batch.length===3&&batch.every(row=>row.passed),'partial batch operation omitted account warning');
  const uncertain=await page.evaluate(async()=>{
   steamAddNotices.length=0;closeSteamAddNotice();clearNotice();
   const warning='当前账户结果未确认。请先刷新游戏库核对，勿直接重复加入。';
   const key=savePresentationProbe.key;
   const result=fixtureSteamCommitResult(__LIBRARY_H5_FIXTURE__.snapshot,[key]);
   result.steamAddCompletion={...result.steamPlan,displayAccountCompletionConfirmed:false,partialSteamAccounts:true};
   result.steamPlan=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot.steamPlan);delete result.steamPlanSource;
   result.steamAccountWriteWarning=warning;
   let calls=0;invoke=async()=>{calls++;return workspaceBridgeResult('commit',result);};
   steamCommitTasks.set(key,{phase:'running',awaitingReceipt:true});
   reverseEmit({event:'steam-commit-progress',directories:[key],phase:'partial',error:warning});
   const keptWaiting=steamCommitTasks.get(key)?.phase==='running';
   steamCommitTasks.delete(key);
   reverseEmit({event:'steam-commit-progress',directories:[key],phase:'partial',error:warning});
   const batchBadge=steamCommitTasks.get(key)?.label;
   await commitSelectedGames({selectedGameDirectories:[key]});
   const batchNotice=$('#steam-add-notice-message').textContent;
   closeSteamAddNotice();
   const singleOk=await joinEditedGameToSteam(),singleNotice=$('#steam-add-notice-message').textContent;
   return {calls,keptWaiting,batchBadge,batchNotice,singleNotice,singleOk,warning,label:steamCommitTasks.get(key)?.label};
  });saveMetrics.push({case:'partial-account-current-unconfirmed',uncertain});
  check(uncertain.calls===2&&uncertain.keptWaiting&&uncertain.batchBadge==='部分完成'&&uncertain.singleOk&&uncertain.label==='部分完成'&&
    [uncertain.batchNotice,uncertain.singleNotice].every(text=>text.includes(uncertain.warning)&&text.includes('部分完成')&&!text.includes('已加入 Steam；')),
    'unconfirmed current Steam account was shown as a complete success or consumed a terminal event before the reply');
  const busyRows=await page.evaluate(async()=>{
   const actual=invoke,realLibrary=renderLibrary,realEntry=renderEntry,rows=[];let requests=[],throwDrawing=false;
   const flush=async()=>{for(let i=0;i<10;i++)await Promise.resolve();};
   invoke=(command,args={})=>{if(command==='steamCommitPreflight'){requests.push({command,args:structuredClone(args)});return Promise.resolve({steamRunning:false});}return ['commit','deleteFromSteam','refreshSteamArtwork','state','closeSteam','cancelTask'].includes(command)?new Promise((resolve,reject)=>requests.push({command,args:structuredClone(args),resolve,reject})):actual(command,args);};
   renderLibrary=(...args)=>{if(throwDrawing){throwDrawing=false;throw Error('owned batch drawing failed');}return realLibrary(...args);};
   const setup=()=>{steamAddNotices.length=0;$('#steam-add-notice-modal').classList.add('hidden');clearNotice();closeEdit();snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/OwnedBusyRoot',busyMarker:'original'};requests=[];cancelInFlight=false;};
   const replacement=()=>{snapshot={...snapshot,dataRoot:'G:/ReplacementBusyRoot',busyMarker:'replacement'};};
   const reply=(index,marker='accepted',foreign=false)=>{const request=requests[index],result={...structuredClone(snapshot),dataRoot:foreign?'G:/ForeignBusyRoot':'G:/OwnedBusyRoot',busyMarker:marker};request.resolve(workspaceBridgeResult(request.command,result));};
   const observe=(mode,command,ok)=>rows.push({mode,command,ok,root:snapshot.dataRoot,marker:snapshot.busyMarker,commands:requests.filter(r=>command==='closeSteam'||r.command!=='steamCommitPreflight').map(r=>r.command),busy:!busy.classList.contains('hidden'),title:$('#busy-title').textContent,cancelled:cancelInFlight,notice:toastNotice.textContent,noticeModal:!$('#steam-add-notice-modal').classList.contains('hidden')});
   for(const command of ['commit','deleteFromSteam','refreshSteamArtwork'])for(const mode of ['current success','old root success','retired page success','foreign receipt','old root failure','late failure reread','current failure','same root alias','drawing failure']){
    console.debug('BUSY_BOUNDARY '+command+' '+mode);    setup();const task=runBusy(command,'Owned batch','',{},'Owned completed');
    if(mode==='old root success'||mode==='old root failure')replacement();
    if(mode==='retired page success'){window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));snapshot={...snapshot,busyMarker:'new-page'};}
    if(mode==='same root alias')snapshot={...snapshot,dataRoot:snapshot.dataRoot.toUpperCase().replaceAll('/',String.fromCharCode(92))};
    if(mode==='drawing failure')throwDrawing=true;
    if(mode.includes('failure')&&mode!=='drawing failure'||mode==='late failure reread'){
     requests[0].reject(Error('owned native batch failed'));await flush();
     if(mode==='late failure reread')replacement();
     const recovery=requests.findIndex(r=>r.command==='state');if(recovery>=0)reply(recovery,'recovered');
    }else reply(0,'accepted',mode==='foreign receipt');
    await flush();if(mode==='drawing failure'){const recovery=requests.findIndex(r=>r.command==='state');if(recovery>=0)reply(recovery,'recovered');}
    const ok=await task;observe(mode,command,ok);
   }
   for(const mode of ['older completion','older rejection','unrelated scrape terminal','cancel then native cancellation','stale cancel rejection']){
    console.debug('BUSY_BOUNDARY overlap '+mode);setup();const older=runBusy('commit','Older batch','');let cancelTask;
    if(mode==='cancel then native cancellation'||mode==='stale cancel rejection'){cancelTask=cancelActiveTask();await flush();}
    if(mode==='cancel then native cancellation'){
     requests.find(r=>r.command==='cancelTask').resolve({});requests[0].reject(Error('TASK_CANCELLED'));await cancelTask;const ok=await older;observe(mode,'commit',ok);continue;
    }
    const newer=runBusy('refreshSteamArtwork','Newer batch','');
    if(mode==='unrelated scrape terminal'){reverseEmit({event:'scrape-paused',scrape:{mode:'automatic',items:{}}});await flush();}
    else if(mode==='older rejection')requests[0].reject(Error('owned obsolete command failed'));
    else if(mode==='stale cancel rejection'){requests.find(r=>r.command==='cancelTask').reject(Error('owned obsolete cancel failed'));await cancelTask;reply(0,'older');}
    else reply(0,'older');
    await flush();const recovery=requests.findIndex(r=>r.command==='state');if(recovery>=0)reply(recovery,'old-recovery');
    if(mode!=='unrelated scrape terminal')await older;
    const before={busy:!busy.classList.contains('hidden'),title:$('#busy-title').textContent,marker:snapshot.busyMarker,commands:requests.map(r=>r.command),cancelled:cancelInFlight};
    if(mode==='unrelated scrape terminal'){reply(0,'older');await older;}
    const index=requests.findIndex(r=>r.command==='refreshSteamArtwork');reply(index,'newer');const ok=await newer;observe(mode,'overlap',ok);rows.at(-1).before=before;
   }
   console.debug('BUSY_BOUNDARY close-followup');setup();applySteamRuntimeState({steamRunning:true,steamClient:window.__libraryShutdownClient});const closing=closeSteamOnly(captureSteamClient(steamRuntimeState));replacement();requests[0].resolve({steamRunning:false});await flush();if(requests[1]?.resolve)requests[1].resolve({steamRunning:false});await closing;observe('old close has no foreign followup','closeSteam',true);

   const zeroReplies=[];
   for(const result of [{recorded:true},{closing:true},[],7]){
    setup();const before=JSON.stringify(snapshot);let error='';try{reverseEmit({kind:'response',id:0,ok:true,result});}catch(e){error=String(e.message||e);}await flush();zeroReplies.push({result,error,unchanged:JSON.stringify(snapshot)===before});
   }
   setup();const pushed={...structuredClone(snapshot),busyMarker:'legacy-zero-snapshot'};reverseEmit({kind:'response',id:0,ok:true,result:pushed});await flush();zeroReplies.push({positive:true,marker:snapshot.busyMarker,mediaCount:Object.keys(snapshot.gameMedia?.entries||{}).length});window.busyZeroReplies=zeroReplies;
   invoke=actual;renderLibrary=realLibrary;renderEntry=realEntry;return rows;
  });saveMetrics.push({case:'busy-write-context',rows:busyRows});
  check(busyRows.every(row=>row.mode==='old root success'?row.ok&&row.root==='G:/ReplacementBusyRoot'&&row.marker==='replacement'&&!row.noticeModal:row.mode==='retired page success'?row.ok&&row.marker==='new-page':row.mode==='foreign receipt'?row.ok&&row.root==='G:/OwnedBusyRoot'&&row.marker==='original':row.mode==='old root failure'? !row.ok&&row.root==='G:/ReplacementBusyRoot'&&row.marker==='replacement'&&row.commands.length===1&&!row.noticeModal:row.mode==='late failure reread'?!row.ok&&row.root==='G:/ReplacementBusyRoot'&&row.marker==='replacement'&&!row.noticeModal:row.mode==='current failure'?!row.ok&&row.marker==='recovered'&&row.commands.join()===[row.command,'state'].join():row.mode==='cancel then native cancellation'?!row.ok&&!row.busy&&!row.cancelled:row.command==='overlap'?row.ok&&row.before.busy&&row.before.title==='Newer batch'&&!row.before.cancelled&&row.before.marker==='original'&&row.marker==='newer'&&!row.busy:row.mode==='old close has no foreign followup'?row.commands.join()==='closeSteam'&&row.root==='G:/ReplacementBusyRoot':row.ok&&row.marker==='accepted'),'bulk result or busy cleanup affected a different root page or operation');
  const zeroReplies=await page.evaluate(()=>busyZeroReplies);saveMetrics.push({case:'busy-zero-reply-shape',rows:zeroReplies});
  check(zeroReplies.every(row=>row.positive?row.marker==='legacy-zero-snapshot'&&row.mediaCount>0:row.unchanged&&!row.error),'metadata-only zero-ID acknowledgement replaced the library snapshot');


 });


 await run('rejected inline Steam request keeps its business failure through a drawing error',async()=>{
  await beginCommitPresentationProbe();
  const result=await page.evaluate(async()=>{const actual=invoke;invoke=async(command,args={},timeoutMs=0)=>{if(command!=='commit')return actual(command,args,timeoutMs);savePresentationProbe.commits.push(structuredClone(args));savePresentationProbe.throwEditor=true;throw Error('owned Steam admission rejected');};const ok=await joinEditedGameToSteam();const task=steamCommitTasks.get(savePresentationProbe.key);return {ok,calls:savePresentationProbe.commits.length,task:structuredClone(task),throws:savePresentationProbe.editorThrows,pending:cardOperationEditorPending,diagnostics:savePresentationProbe.diagnostics};});saveMetrics.push({case:'commit-business-rejection-display-failure',result});
  check(!result.ok&&result.calls===1&&result.task?.phase==='failed'&&result.task.error.includes('owned Steam admission rejected')&&result.throws===1&&result.pending&&result.diagnostics.some(x=>x.message==='owned editor presentation unavailable'),'a drawing error concealed a real Steam rejection, replaced its failure reason or retried its write');
  const recovered=await page.evaluate(()=>{refreshCardOperationState();const status=$('#edit-status');return {calls:savePresentationProbe.commits.length,status:status.textContent,detail:status.title,notice:$('#steam-add-notice-message').textContent,task:steamCommitTasks.get(savePresentationProbe.key)?.phase,pending:cardOperationEditorPending};});saveMetrics.at(-1).recovered=recovered;
  check(recovered.calls===1&&recovered.task==='failed'&&recovered.status==='加入失败'&&[recovered.detail,recovered.notice].every(text=>text.includes('owned Steam admission rejected'))&&!recovered.pending,'normal display recovery lost the rejected Steam reason or resubmitted its request');
 });


 async function beginRevertedFailureProbe(){
  await page.evaluate(()=>{
   const actual=invoke;window.revertedFailureProbe={calls:[],pending:[],original:structuredClone(selectedActionGame),key:keyFor(selectedActionGame)};
   invoke=async(command,args={},timeoutMs=0)=>{
    if(command!=='saveEditor')return actual(command,args,timeoutMs);
    revertedFailureProbe.calls.push(structuredClone(args));
    return new Promise((resolve,reject)=>revertedFailureProbe.pending.push({fail:()=>reject(Error('owned reverted save unavailable')),accept:()=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;for(const op of args.operations){if(op.command!=='setBucket')throw Error('unexpected reverted save fixture operation');state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}resolve(structuredClone(state));}}));
   };
  });
 }
 await run('reverting a failed submission clears only its stale failure without a new save',async()=>{
  await beginRevertedFailureProbe();
  const failed=await page.evaluate(async()=>{editDraft.bucket='non-game';const saving=persistEditDraft();revertedFailureProbe.pending[0].fail();return {ok:await saving,task:editorSaveTasks.get(revertedFailureProbe.key)?.phase,bucket:editDraft.bucket,baseline:editDraft.originalBucket};});
  check(!failed.ok&&failed.task==='failed'&&failed.bucket==='non-game'&&failed.baseline!=='non-game','failed submission precondition was not real');
  const settled=await page.evaluate(async()=>{closeEdit();openEdit(mergedGames().find(g=>keyFor(g)===revertedFailureProbe.key));editDraft.bucket=editDraft.originalBucket;const ok=await persistEditDraft();return {ok,task:editorSaveTasks.has(revertedFailureProbe.key),active:editSaveInFlight,calls:revertedFailureProbe.calls.length,status:document.getElementById('edit-status').textContent,operation:activeCardOperation(revertedFailureProbe.key),dirty:editDraft.bucket!==editDraft.originalBucket};});saveMetrics.push({case:'save-reverted-failure',failed,settled});
  check(settled.ok&&!settled.task&&!settled.active&&settled.calls===1&&!settled.status.includes('保存失败')&&!settled.operation&&!settled.dirty,'explicit unchanged save retained its withdrawn failed submission');
  const reopened=await page.evaluate(()=>{closeEdit();openEdit(mergedGames().find(g=>keyFor(g)===revertedFailureProbe.key));return {bucket:editDraft.bucket,baseline:editDraft.originalBucket,task:editorSaveTasks.has(revertedFailureProbe.key),calls:revertedFailureProbe.calls.length};});
  check(reopened.bucket===reopened.baseline&&!reopened.task&&reopened.calls===1,'reopening resurrected withdrawn values or replayed their old save');
 });
 await run('reverted failure settlement keeps another running card and current Steam progress',async()=>{
  await beginRevertedFailureProbe();
  const first=await page.evaluate(async()=>{editDraft.bucket='non-game';const one=persistEditDraft();revertedFailureProbe.pending[0].fail();await one;closeEdit();const state=__LIBRARY_H5_FIXTURE__.snapshot;const row={...state.library.games[0],gameDirectory:state.library.games[0].gameDirectory+'-other-running',primaryExecutable:state.library.games[0].primaryExecutable+'.other'};state.library.games.push(row);snapshot=structuredClone(state);openEdit(mergedGames().find(g=>keyFor(g)===keyFor(row)));revertedFailureProbe.otherKey=keyFor(row);editDraft.bucket='non-game';window.otherRevertedSave=persistEditDraft();closeEdit();openEdit(mergedGames().find(g=>keyFor(g)===revertedFailureProbe.key));editDraft.bucket=editDraft.originalBucket;reverseEmit({event:'steam-commit-progress',directories:[selectedActionGame.gameDirectory],phase:'running'});document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const ok=await persistEditDraft();return {ok,firstTask:editorSaveTasks.has(revertedFailureProbe.key),otherTask:editorSaveTasks.get(revertedFailureProbe.otherKey)?.phase,active:editSaveInFlight,calls:revertedFailureProbe.calls.length,steam:steamCommitTasks.get(revertedFailureProbe.key)?.phase,pending:cardOperationEditorPending};});saveMetrics.push({case:'save-reverted-other-running',first});
  check(first.ok&&!first.firstTask&&first.otherTask==='running'&&first.active&&first.calls===2&&first.steam==='running'&&first.pending,'unchanged settlement erased another running save, Steam progress or background display demand');
  const completed=await page.evaluate(async()=>{const current=keyFor(selectedActionGame);revertedFailureProbe.pending[1].accept();const ok=await otherRevertedSave;document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);refreshCardOperationState();return {ok,keyKept:keyFor(selectedActionGame)===current,active:editSaveInFlight,tasks:editorSaveTasks.size,calls:revertedFailureProbe.calls.length,operation:activeCardOperation(revertedFailureProbe.key)?.phase,status:document.getElementById('edit-status').textContent};});saveMetrics.at(-1).completed=completed;
  check(completed.ok&&completed.keyKept&&!completed.active&&completed.tasks===0&&completed.calls===2&&completed.operation==='running'&&!completed.status.includes('保存失败'),'other card completion revived discarded failure, changed current card or lost Steam work');
 });
 await run('unchanged save from a replacement executable cannot dismiss a different failed draft',async()=>{
  await beginRevertedFailureProbe();
  const row=await page.evaluate(async()=>{editDraft.bucket='non-game';const pending=persistEditDraft();revertedFailureProbe.pending[0].fail();await pending;const failed=editorSaveTasks.get(revertedFailureProbe.key);closeEdit();const replacement={...revertedFailureProbe.original,primaryExecutable:revertedFailureProbe.original.primaryExecutable+'.replacement'};openEdit(replacement);const originalDraft=editDraft;const ok=await persistEditDraft();return {ok,sameTask:editorSaveTasks.get(revertedFailureProbe.key)===failed,failedPhase:failed.phase,draftIsDifferent:failed.draft!==originalDraft,current:selectedActionGame.primaryExecutable,expected:replacement.primaryExecutable,calls:revertedFailureProbe.calls.length,active:editSaveInFlight};});saveMetrics.push({case:'save-reverted-executable-guard',row});
  check(row.ok&&row.sameTask&&row.failedPhase==='failed'&&row.draftIsDifferent&&row.current===row.expected&&row.calls===1&&!row.active,'a no-op from a different executable cleared another draft failure or replayed it');
 });


 async function beginMediaCardConflictProbe(){
  await page.evaluate(()=>{
   closeEdit();const seed=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570']);
   window.mediaCardConflictProbe={seed,baseCalls:reverseIO.calls.length,rows:[]};
   mediaCardConflictProbe.game=(directory,id,executable,verified=true,native=false)=>({gameDirectory:directory,directoryName:directory.split('/').at(-1),primaryExecutable:executable,status:'ready',...(native?{steamNative:true,nativeSteamAppId:id}:{}),fixtureAppId:id,fixtureVerified:verified});
   mediaCardConflictProbe.entry=(appId,executable)=>({...structuredClone(seed),key:appId?'steam-'+appId:'exe-owned-local',appId,executable,name:'Owned media '+appId,bindingRevision:4});
   mediaCardConflictProbe.install=(games,entries)=>{
    const state=__LIBRARY_H5_FIXTURE__.snapshot;state.library={games:structuredClone(games)};state.config={roots:[],manualBuckets:{}};state.manualOverrides={items:{}};state.resolvedIdentities={};
    state.steamPlan={items:games.map(g=>({gameDirectory:g.gameDirectory,primaryExecutable:g.primaryExecutable,appId:g.fixtureAppId,steamStoreAppId:g.fixtureAppId,status:g.fixtureVerified?'ready-to-add':'identity-unconfirmed',identityStatus:g.fixtureVerified?'verified':'unconfirmed',steamVerificationStatus:g.fixtureVerified?'steam-verified':'unconfirmed',...(g.steamNative?{nativeSteamAppId:g.fixtureAppId,steamNative:true}:{})}))};
    state.gameMedia={entries:Object.fromEntries(entries.map(e=>[e.key,structuredClone(e)]))};snapshot=structuredClone(state);activeTab='not-in-steam';selectedGameDirectories=null;
   };
   mediaCardConflictProbe.row=game=>({key:keyFor(game),appId:confirmedMediaAppId(game),media:game.gameMedia?.key||'',mediaOnly:!!game.mediaOnly,executable:game.primaryExecutable||''});
   mediaCardConflictProbe.writes=()=>reverseIO.calls.slice(mediaCardConflictProbe.baseCalls).filter(c=>['saveEditor','commit','identifyOne','mediaCreate','mediaEdit','mediaImport','mediaSelectVideo','mediaDownloadNow','mediaDownloadCancel'].includes(c.command));
  });
 }
 async function beginManualVideoReceiptProbe(waitForSelection=true){
  await page.evaluate(async()=>{
   const actual=invoke,owner=captureSharedWallpaperWrite();window.manualVideoReceiptProbe={owner,base:structuredClone(owner.entry),selects:[],states:0};
   invoke=async(command,args={},timeout=0)=>{
    const p=manualVideoReceiptProbe;
    if(command==='mediaSelectVideo'){p.selects.push(structuredClone(args));return new Promise((resolve,reject)=>{p.reject=reject;p.finish=async(overrides={})=>{const receipt=await actual(command,args,timeout);p.nativeReceipt=structuredClone(receipt);p.beforeReply?.(receipt);resolve({...receipt,...overrides});};});}
    if(command==='mediaState')p.states++;
    return actual(command,args,timeout);
   };
   await searchSharedWallpaperVideos(owner);const card=document.querySelector('[data-video-candidate]');if(!card)throw Error('owned video candidate missing');
  });
  await page.evaluate(()=>document.querySelector('[data-video-candidate]').click());
  if(waitForSelection)await page.waitForFunction(()=>manualVideoReceiptProbe.selects.length===1&&sharedWallpaperBusy);
 }
 async function manualVideoReceiptResult(){
  await page.waitForFunction(()=>!sharedWallpaperBusy);
  return page.evaluate(()=>{const p=manualVideoReceiptProbe,entry=snapshot.gameMedia.entries[p.base.key];return {entry,phase:sharedWallpaperDownloadRequests.get(p.owner.key)?.phase,request:sharedWallpaperDownloadRequests.get(p.owner.key),selects:p.selects.length,states:p.states,busy:sharedWallpaperBusy,selectedKey:keyFor(selectedActionGame),selectedMedia:selectedActionGame?.gameMedia,galleryHidden:document.getElementById('artwork-search-modal').classList.contains('hidden'),editorHidden:document.getElementById('edit-modal').classList.contains('hidden'),native:p.nativeReceipt,base:p.base,ownerKey:p.owner.key};});
 }
 async function beginVideoGalleryFreshnessProbe(held=true){
  await beginLiveEditorMediaProbe();
  await page.evaluate(held=>{
   const actual=invoke;window.videoGalleryFreshnessProbe={searches:[],selects:[]};
   invoke=async(command,args={},timeout=0)=>{const p=videoGalleryFreshnessProbe;
    if(command==='mediaSearchVideos'){p.searches.push(structuredClone(args));if(held&&p.searches.length===1)return new Promise((resolve,reject)=>{p.reply=async()=>resolve(await actual(command,args,timeout));p.reject=reject;});}
    if(command==='mediaSelectVideo')p.selects.push(structuredClone(args));return actual(command,args,timeout);
   };
   videoGalleryFreshnessProbe.task=searchSharedWallpaperVideos(captureSharedWallpaperWrite());
  },held);
  if(held)await page.waitForFunction(()=>!!videoGalleryFreshnessProbe.reply);else await page.waitForFunction(()=>document.querySelectorAll('[data-video-candidate]').length===2);
 }
 async function videoGalleryFreshnessResult(){
  return page.evaluate(()=>{const p=videoGalleryFreshnessProbe;return {searches:p.searches,selects:p.selects,summary:document.getElementById('artwork-search-summary').textContent,retry:document.querySelectorAll('[data-video-search-retry]').length,candidates:document.querySelectorAll('[data-video-candidate]').length,entry:structuredClone(selectedActionGame.gameMedia),hidden:document.getElementById('artwork-search-modal').classList.contains('hidden')};});
 }
 await run('video gallery late search offers an explicit fresh revision retry',async()=>{
  await beginVideoGalleryFreshnessProbe();const fresh=await applyLiveEditorMedia();await page.evaluate(async()=>{await videoGalleryFreshnessProbe.reply();await videoGalleryFreshnessProbe.task;});const stale=await videoGalleryFreshnessResult();progressMetrics.push({case:'video-gallery-late-search',fresh,stale});
  check(stale.retry===1&&stale.candidates===0&&stale.summary.includes('壁纸设置已更新')&&stale.searches.length===1&&stale.selects.length===0&&stale.entry.bindingRevision===fresh.bindingRevision,'stale search left a silent gallery instead of an explicit retry');
  await page.evaluate(async()=>{await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});check(await page.evaluate(()=>document.activeElement===document.querySelector('[data-video-search-retry]')),'stale search retry was not keyboard reachable');
  await page.keyboard.press('Enter');await page.waitForFunction(()=>videoGalleryFreshnessProbe.searches.length===2&&document.querySelectorAll('[data-video-candidate]').length===2);const retried=await videoGalleryFreshnessResult();progressMetrics.push({case:'video-gallery-explicit-retry',retried});
  check(retried.searches[1].bindingRevision===fresh.bindingRevision&&retried.selects.length===0&&retried.retry===0,'explicit retry reused the obsolete revision or submitted a video');
 });
 await run('video gallery an obsolete candidate explains its rejection without a write',async()=>{
  await beginVideoGalleryFreshnessProbe(false);const fresh=await applyLiveEditorMedia();await page.evaluate(()=>document.querySelector('[data-video-candidate]').click());await settle();const r=await videoGalleryFreshnessResult();progressMetrics.push({case:'video-gallery-obsolete-candidate',fresh,r});
  check(r.retry===1&&r.candidates===0&&r.summary.includes('壁纸设置已更新')&&r.selects.length===0&&r.searches.length===1&&r.entry.assets.dynamic.id===fresh.assets.dynamic.id,'obsolete candidate silently ignored the click or dispatched a stale write');
 });
 await run('video gallery a late search failure leaves a fresh search route',async()=>{
  await beginVideoGalleryFreshnessProbe();const fresh=await applyLiveEditorMedia();await page.evaluate(async()=>{videoGalleryFreshnessProbe.reject(Error('owned delayed video search failure'));await videoGalleryFreshnessProbe.task;});const r=await videoGalleryFreshnessResult();progressMetrics.push({case:'video-gallery-late-search-error',fresh,r});
  check(r.retry===1&&r.candidates===0&&r.summary.includes('壁纸设置已更新')&&!r.summary.includes('正在读取')&&r.searches.length===1&&r.selects.length===0&&r.entry.bindingRevision===fresh.bindingRevision,'stale failed search stranded the loading message');
 });
 await run('video gallery matching accepted snapshot closes its own gallery once',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{const p=manualVideoReceiptProbe;p.beforeReply=receipt=>{const next=structuredClone(snapshot);next.gameMedia.entries[receipt.key]=structuredClone(receipt);applyLibrarySnapshot(next);p.snapshotBeforeReply=selectedActionGame.gameMedia.bindingRevision;};await p.finish();});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-gallery-matching-accepted-snapshot',r});
  check(r.galleryHidden&&!r.editorHidden&&r.selects===1&&r.states===1&&r.phase==='queued'&&r.entry.bindingRevision===r.native.bindingRevision&&r.selectedMedia.bindingRevision===r.native.bindingRevision,'matching accepted full snapshot stranded the gallery or replayed selection');
 });
 await run('video gallery stale background replies defer feedback and focus until foreground',async()=>{
  const rows=[];for(const replyFirst of [false,true]){
   if(rows.length)await reset();await beginVideoGalleryFreshnessProbe();await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const p=videoGalleryFreshnessProbe;p.mutations=0;p.focuses=0;p.observer=new MutationObserver(records=>p.mutations+=records.length);p.observer.observe(document.getElementById('artwork-search-modal'),{attributes:true,childList:true,characterData:true,subtree:true});p.onFocus=()=>p.focuses++;document.getElementById('artwork-search-modal').addEventListener('focusin',p.onFocus);});
   if(replyFirst)await page.evaluate(async()=>{await videoGalleryFreshnessProbe.reply();await videoGalleryFreshnessProbe.task;});const fresh=await applyLiveEditorMedia(true);if(!replyFirst)await page.evaluate(async()=>{await videoGalleryFreshnessProbe.reply();await videoGalleryFreshnessProbe.task;});
   const hidden=await page.evaluate(()=>{const p=videoGalleryFreshnessProbe;p.mutations+=p.observer.takeRecords().length;p.observer.disconnect();document.getElementById('artwork-search-modal').removeEventListener('focusin',p.onFocus);return {mutations:p.mutations,focuses:p.focuses,searches:p.searches.length,selects:p.selects.length};});
   await page.evaluate(async()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});const visible=await videoGalleryFreshnessResult();rows.push({replyFirst,fresh,hidden,visible});
  }progressMetrics.push({case:'video-gallery-stale-background',rows});
  check(rows.every(x=>x.hidden.mutations===0&&x.hidden.focuses===0&&x.hidden.searches===1&&x.hidden.selects===0&&x.visible.retry===1&&x.visible.candidates===0&&x.visible.summary.includes('壁纸设置已更新')&&x.visible.searches.length===1),'stale background search painted, refocused, retried automatically or failed to resume');
 });
 await run('video gallery selection rejected after a newer snapshot offers fresh search',async()=>{
  await beginManualVideoReceiptProbe();await beginLiveEditorMediaProbe();const fresh=await applyLiveEditorMedia();await page.evaluate(()=>manualVideoReceiptProbe.reject(Error('owned superseded video selection rejection')));const r=await manualVideoReceiptResult();const gallery=await page.evaluate(()=>({retry:document.querySelectorAll('[data-video-search-retry]').length,candidates:document.querySelectorAll('[data-video-candidate]').length,summary:document.getElementById('artwork-search-summary').textContent}));progressMetrics.push({case:'video-gallery-stale-selection-rejection',fresh,r,gallery});
  check(gallery.retry===1&&gallery.candidates===0&&gallery.summary.includes('壁纸设置已更新')&&r.selects===1&&r.states===0&&r.entry.bindingRevision===fresh.bindingRevision&&r.selectedMedia.assets.dynamic.id===fresh.assets.dynamic.id&&r.phase!=='queued','superseded selection rejection left unresponsive candidates or replaced current media');
 });

 await run('video search reply cannot populate a replacement data root',async()=>{
  await page.evaluate(()=>{const actual=invoke;window.videoRootSearch={selects:0};invoke=async(command,args={},timeout=0)=>{if(command==='mediaSearchVideos')return new Promise(resolve=>videoRootSearch.reply=async()=>resolve(await actual(command,args,timeout)));if(command==='mediaSelectVideo')videoRootSearch.selects++;return actual(command,args,timeout);};window.videoRootSearch.task=searchSharedWallpaperVideos(captureSharedWallpaperWrite());});
  await page.waitForFunction(()=>!!videoRootSearch.reply);await page.evaluate(async()=>{snapshot={...snapshot,dataRoot:'G:/ReplacementVideoRoot'};await videoRootSearch.reply();await videoRootSearch.task;});
  const r=await page.evaluate(()=>({candidates:document.querySelectorAll('[data-video-candidate]').length,selects:videoRootSearch.selects}));progressMetrics.push({case:'video-root-search',r});
  check(r.candidates===0&&r.selects===0,'old-root video search populated the replacement library gallery');
 });
 await run('video candidate from a retired data root cannot submit',async()=>{
  await page.evaluate(async()=>{const actual=invoke;window.videoRootCandidate={selects:0};invoke=async(command,...args)=>{if(command==='mediaSelectVideo')videoRootCandidate.selects++;return actual(command,...args);};await searchSharedWallpaperVideos(captureSharedWallpaperWrite());snapshot={...snapshot,dataRoot:'G:/ReplacementVideoRoot'};document.querySelector('[data-video-candidate]').click();});await settle();
  const r=await page.evaluate(()=>({selects:videoRootCandidate.selects,requests:sharedWallpaperDownloadRequests.size,busy:sharedWallpaperBusy}));progressMetrics.push({case:'video-root-candidate',r});
  check(r.selects===0&&r.requests===0&&!r.busy,'candidate from an old data root submitted a new video write');
 });
 await run('accepted video selection cannot overwrite a replacement data root',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{const p=manualVideoReceiptProbe;const entry={...p.base,name:'Replacement root media'};snapshot={...snapshot,dataRoot:'G:/ReplacementVideoRoot',gameMedia:{...snapshot.gameMedia,entries:{...snapshot.gameMedia.entries,[p.base.key]:entry}}};p.expectedRootEntry=JSON.stringify(entry);await p.finish();});const r=await manualVideoReceiptResult();const kept=await page.evaluate(()=>JSON.stringify(snapshot.gameMedia.entries[manualVideoReceiptProbe.base.key])===manualVideoReceiptProbe.expectedRootEntry);progressMetrics.push({case:'video-root-selection',r,kept});
  check(kept&&r.states===0&&r.phase!=='queued'&&r.selects===1&&!r.busy,'accepted old-root video receipt overwrote new-root media or issued a foreign state read');
 });
 await run('post-selection state cannot replace another data roots media or progress',async()=>{
  await beginVideoStateFenceProbe();await page.evaluate(()=>{const p=manualVideoReceiptProbe,f=videoStateFenceProbe;const entry={...f.accepted,name:'Replacement root media'},progress={statusRevision:1,phase:'pending',rootMarker:'current-root'};snapshot={...snapshot,dataRoot:'G:/ReplacementVideoRoot',gameMedia:{...snapshot.gameMedia,entries:{...snapshot.gameMedia.entries,[p.base.key]:entry}},mediaDownloads:progress};f.expectedRootEntry=JSON.stringify(entry);f.expectedRootProgress=JSON.stringify(progress);f.reply({...f.accepted,name:'Retired root state',bindingRevision:f.accepted.bindingRevision+2},{statusRevision:99999,phase:'downloading',rootMarker:'retired-root'});});const r=await manualVideoReceiptResult();const kept=await page.evaluate(()=>({entry:JSON.stringify(snapshot.gameMedia.entries[manualVideoReceiptProbe.base.key])===videoStateFenceProbe.expectedRootEntry,progress:JSON.stringify(snapshot.mediaDownloads)===videoStateFenceProbe.expectedRootProgress}));progressMetrics.push({case:'video-root-state',r,kept});
  check(kept.entry&&kept.progress&&r.selects===1&&!r.busy,'old-root post-selection read replaced new-root media or download progress');
 });
 await run('video download submission feedback stays with its data root',async()=>{
  await beginManualVideoReceiptProbe();const r=await page.evaluate(()=>{const p=manualVideoReceiptProbe,entry={...p.base,refreshRequested:false,selectedVideo:null,taskSlots:[]};snapshot={...snapshot,dataRoot:'G:/ReplacementVideoRoot',gameMedia:{...snapshot.gameMedia,entries:{...snapshot.gameMedia.entries,[p.base.key]:entry}},mediaDownloads:{}};renderSharedWallpaperDownloadStatus();const panel=document.getElementById('shared-wallpaper-download-status');return {hidden:panel.classList.contains('hidden'),text:panel.textContent,request:sharedWallpaperDownloadRequests.get(p.owner.key)};});await page.evaluate(async()=>manualVideoReceiptProbe.finish());await manualVideoReceiptResult();progressMetrics.push({case:'video-root-feedback',r});
  check(r.hidden,'pending download feedback from an old data root appeared in the replacement library');
 });
 async function beginLiveEditorMediaProbe(){
  await page.evaluate(()=>{window.liveEditorMediaProbe={base:structuredClone(selectedActionGame.gameMedia),draft:editDraft,game:selectedActionGame,root:snapshot.dataRoot,key:keyFor(selectedActionGame)};const p=liveEditorMediaProbe;p.fresh=(delta=3)=>{const asset={...p.base.assets.dynamic,id:'live-editor-video-'+delta,revision:'live-'+delta};return {...p.base,bindingRevision:p.base.bindingRevision+delta,assets:{...p.base.assets,dynamic:asset},asset,dynamicPreviewUrl:'/sample.mp4?live='+delta,previewUrl:'/sample.mp4?live='+delta};};});
 }
 async function applyLiveEditorMedia(deferRender=false){
  return page.evaluate(deferRender=>{const p=liveEditorMediaProbe,fresh=p.fresh();__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[p.base.key]=structuredClone(fresh);const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=fresh;applyLibrarySnapshot(next,{deferRender});return fresh;},deferRender);
 }
 async function liveEditorMediaResult(){
  return page.evaluate(()=>{const p=liveEditorMediaProbe;return {entry:snapshot.gameMedia.entries[p.base.key],selected:selectedActionGame?.gameMedia,sameDraft:editDraft===p.draft,sameGame:selectedActionGame===p.game,key:keyFor(selectedActionGame),video:document.querySelector('#artwork-wallpaper-preview video')?.getAttribute('src'),pending:editorArtworkRenderPending,busy:sharedWallpaperBusy,closed:document.getElementById('edit-modal').classList.contains('hidden'),writes:reverseIO.calls.filter(x=>x.command==='mediaEdit'),base:p.base};});
 }
 await run('current full media update reaches the already open editor preview',async()=>{
  await beginLiveEditorMediaProbe();const fresh=await applyLiveEditorMedia();const r=await liveEditorMediaResult();progressMetrics.push({case:'live-editor-media-preview',fresh,r});
  check(r.selected.bindingRevision===fresh.bindingRevision&&r.video?.includes('live=3'),'independent media snapshot did not reach the open editor preview');
  check(r.sameDraft&&r.sameGame&&!r.closed&&r.writes.length===0,'live media update rebuilt the draft, reopened the editor or wrote configuration');
 });
 await run('next playback save uses the independently refreshed editor media revision',async()=>{
  await beginLiveEditorMediaProbe();const fresh=await applyLiveEditorMedia();const ok=await page.evaluate(()=>updateSharedWallpaper('playback',{start:5,end:35,fit:'cover'}));const r=await liveEditorMediaResult();progressMetrics.push({case:'live-editor-media-next-write',fresh,ok,r});
  check(ok&&r.selected.playback.start===5&&r.selected.bindingRevision===fresh.bindingRevision+1&&r.writes.length===1&&r.writes[0].args.bindingRevision===fresh.bindingRevision,'next editor write used the stale media revision after a full refresh');
 });
 await run('live media synchronization preserves unsaved identity bucket and artwork drafts',async()=>{
  await beginLiveEditorMediaProbe();await page.evaluate(()=>{const p=liveEditorMediaProbe,input=document.getElementById('identity-name');input.value='Owned unsaved editor name';input.dispatchEvent(new Event('input'));editDraft.bucket='non-game';artworkDraft.cover={state:'set',asset:{url:'/cover.jpg?owned-unsaved',provider:'manual'}};artworkDraft.dirty.cover=true;p.cover=artworkDraft.cover;p.fields=JSON.stringify({override:editDraft.game.override,bucket:editDraft.bucket,identityBaseline:editDraft.identityBaseline,nameEdits:editDraft.nameEdits,artworkChanged:editDraft.artworkChanged});input.focus();p.focus=document.activeElement;});const fresh=await applyLiveEditorMedia();const r=await liveEditorMediaResult();const kept=await page.evaluate(()=>{const p=liveEditorMediaProbe;return {fields:JSON.stringify({override:editDraft.game.override,bucket:editDraft.bucket,identityBaseline:editDraft.identityBaseline,nameEdits:editDraft.nameEdits,artworkChanged:editDraft.artworkChanged})===p.fields,cover:artworkDraft.cover===p.cover&&artworkDraft.dirty.cover,input:document.getElementById('identity-name').value,focus:document.activeElement===p.focus};});progressMetrics.push({case:'live-editor-unsaved-fields',fresh,r,kept});
  check(r.selected.bindingRevision===fresh.bindingRevision&&r.sameDraft&&r.sameGame&&kept.fields&&kept.cover&&kept.input==='Owned unsaved editor name'&&kept.focus&&r.writes.length===0,'media-only live update overwrote an unsaved field, artwork draft or focus');
 });
 await run('silent full media reply refreshes the open editor without reopening it',async()=>{
  await beginLiveEditorMediaProbe();await page.evaluate(()=>{const post=window.chrome.webview.postMessage;liveEditorMediaProbe.held=[];window.chrome.webview.postMessage=function(message){if(message.command==='state'){liveEditorMediaProbe.held.push(structuredClone(message));return;}return post.call(this,message);};scheduleSilentRefresh();});await page.waitForFunction(()=>liveEditorMediaProbe.held.length===1);await page.evaluate(()=>{const p=liveEditorMediaProbe,next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=p.fresh();reverseEmit({kind:'response',id:p.held[0].id,ok:true,result:next});});await page.waitForFunction(()=>!silentRefreshInFlight);const r=await liveEditorMediaResult();progressMetrics.push({case:'live-editor-silent-full-reply',r});
  check(r.selected.bindingRevision===r.base.bindingRevision+3&&r.video?.includes('live=3')&&r.sameDraft&&r.sameGame&&!r.closed&&r.writes.length===0,'silent media snapshot left the current editor behind or reopened its draft');
 });
 await run('background live media synchronization remains data only until focus',async()=>{
  await beginLiveEditorMediaProbe();await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const p=liveEditorMediaProbe,target=document.getElementById('artwork-wallpaper-preview');p.oldVideo=target.querySelector('video');p.mutations=0;p.observer=new MutationObserver(rows=>p.mutations+=rows.length);p.observer.observe(target,{attributes:true,childList:true,subtree:true,characterData:true});});const fresh=await applyLiveEditorMedia(true);const hidden=await page.evaluate(()=>{const p=liveEditorMediaProbe;p.mutations+=p.observer.takeRecords().length;p.observer.disconnect();return {mutations:p.mutations,sameVideo:p.oldVideo===document.querySelector('#artwork-wallpaper-preview video'),revision:selectedActionGame.gameMedia.bindingRevision,pending:editorArtworkRenderPending};});await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});const visible=await liveEditorMediaResult();progressMetrics.push({case:'live-editor-background',fresh,hidden,visible});
  check(hidden.revision===fresh.bindingRevision&&hidden.mutations===0&&hidden.sameVideo&&hidden.pending&&visible.video?.includes('live=3')&&!visible.pending&&visible.writes.length===0,'background media adoption painted or failed to resume current preview');
 });
 await run('live editor media updates honor root identity primary and binding ownership',async()=>{
  const rows=[];for(const test of ['root','AppID','primary','binding','hidden']){if(rows.length)await reset();await beginLiveEditorMediaProbe();await page.evaluate(test=>{const p=liveEditorMediaProbe,next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=p.fresh();if(test==='root')next.dataRoot='G:/DifferentLiveEditorRoot';if(test==='AppID')next.gameMedia.entries[p.base.key].appId=620;if(test==='primary')next.library.games[0].primaryExecutable+='-changed';if(test==='binding')selectedActionGame.gameMedia={...p.base,key:'exe-unsaved-new-binding',appId:0};if(test==='hidden')document.getElementById('edit-modal').classList.add('hidden');p.selectedBefore=selectedActionGame.gameMedia;applyLibrarySnapshot(next);if(test==='root'){const later=structuredClone(snapshot);later.gameMedia.entries[p.base.key]=p.fresh(4);applyLibrarySnapshot(later);}},test);const r=await liveEditorMediaResult();const kept=await page.evaluate(()=>selectedActionGame.gameMedia===liveEditorMediaProbe.selectedBefore);rows.push({test,r,kept});}progressMetrics.push({case:'live-editor-ownership',rows});
  check(rows.every(x=>x.kept&&x.r.writes.length===0),'live media update crossed editor root, identity, primary, binding or visibility ownership');
 });
 await run('equal or older media snapshots cannot redraw the current editor',async()=>{
  await beginLiveEditorMediaProbe();await applyLiveEditorMedia();const r=await page.evaluate(()=>{const p=liveEditorMediaProbe,render=renderArtworkDraft,selected=selectedActionGame.gameMedia;let calls=0;renderArtworkDraft=(...args)=>{calls++;return render(...args);};for(const delta of [3,1]){const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=p.fresh(delta);applyLibrarySnapshot(next);}return {calls,same:selectedActionGame.gameMedia===selected,revision:selectedActionGame.gameMedia.bindingRevision,expected:p.base.bindingRevision+3};});progressMetrics.push({case:'live-editor-equal-old',r});
  check(r.calls===0&&r.same&&r.revision===r.expected,'equal or older inventory caused redundant editor painting or rollback');
 });
 await run('live media refresh rebases one captured playback queued behind a failed mode',async()=>{
  await beginLiveEditorMediaProbe();await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(()=>reverseChoose(5));const fresh=await applyLiveEditorMedia();await page.evaluate(()=>wallpaperModeReceiptProbe.requests[0].reject(Error('owned superseded mode rejected')));await settle();const r=await liveEditorMediaResult();progressMetrics.push({case:'live-editor-pending-playback',fresh,r});
  check(r.selected.playback.start===5&&r.selected.bindingRevision===fresh.bindingRevision+1&&r.writes.filter(x=>x.args.action==='playback').length===1&&!r.busy,'live media update left captured pending playback on its obsolete revision');
  await reset();await holdNativeSnapshotLane();await beginLiveEditorMediaProbe();await beginWallpaperModeReceiptProbe();
  await page.evaluate(()=>{const p=liveEditorMediaProbe,input=$('#identity-name');input.value='Unsaved while joining';input.dispatchEvent(new Event('input'));p.join=joinEditedGameToSteam();});
  await startWallpaperModeButton();
  const hidden=await page.evaluate(async()=>{
   const p=liveEditorMediaProbe;reverseChoose(5);
   document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));
   snapshotIO.resetPaint();
   const preview=$('#artwork-wallpaper-preview'),oldVideo=preview.querySelector('video');
   let mutations=0;const observer=new MutationObserver(records=>{mutations+=records.length;});observer.observe(preview,{attributes:true,childList:true,characterData:true,subtree:true});
   const fresh=p.fresh();__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[fresh.key]=structuredClone(fresh);
   snapshotIO.replyWrite(0);const joined=await p.join;
   const queued=sharedWallpaperPendingPlayback.get(p.key)?.owner.entry?.bindingRevision;
   wallpaperModeReceiptProbe.requests[0].reject(Error('owned superseded mode rejected'));
   await new Promise(requestAnimationFrame);
   mutations+=observer.takeRecords().length;observer.disconnect();
   const playback=reverseIO.calls.filter(x=>x.command==='mediaEdit'&&x.args.action==='playback');
   return {joined,fresh:fresh.bindingRevision,queued,selected:selectedActionGame.gameMedia.bindingRevision,sameDraft:editDraft===p.draft,name:$('#identity-name').value,plan:category(mergedGames().find(game=>keyFor(game)===p.key)),playbackCount:playback.length,used:playback[0]?.args.bindingRevision,start:selectedActionGame.gameMedia.playback.start,paint:snapshotIO.libraryPaints+snapshotIO.entryPaints,previewUntouched:oldVideo===preview.querySelector('video')&&mutations===0,reads:snapshotIO.reads.length,noticeHidden:$('#steam-add-notice-modal').classList.contains('hidden')};
  });
  await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});
  const resumed=await liveEditorMediaResult();
  progressMetrics.push({case:'commit-media-pending-playback-background',hidden,revision:resumed.selected.bindingRevision,video:resumed.video});
  check(hidden.joined&&hidden.plan==='in-steam'&&hidden.sameDraft&&hidden.name==='Unsaved while joining','joining lost Steam membership or the unsaved draft');
  check(hidden.paint===0&&hidden.previewUntouched&&hidden.reads===0&&hidden.noticeHidden,'background join painted the editor, surfaced a foreground dialog or added a state read');
  check(hidden.queued===hidden.fresh&&hidden.used===hidden.fresh&&hidden.selected===hidden.fresh+1&&hidden.playbackCount===1&&hidden.start===5,'background Steam join left queued playback on an obsolete media revision');
  check(resumed.video?.includes('live=3')&&resumed.selected.bindingRevision===hidden.selected,'resumed editor discarded the adopted media and saved playback');
 });
 await run('second installed media editor receives only its own independently updated record',async()=>{
  await page.evaluate(()=>reverseOpenOther());await page.evaluate(async()=>{await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});await beginLiveEditorMediaProbe();const fresh=await applyLiveEditorMedia();const r=await liveEditorMediaResult();progressMetrics.push({case:'live-editor-media-only',fresh,r});
  check(r.key.endsWith('\\secondinstalled')&&r.selected.key==='steam-1245620'&&r.selected.key===fresh.key&&r.selected.bindingRevision===fresh.bindingRevision&&r.video?.includes('live=3')&&r.sameDraft&&r.writes.length===0,'second installed editor missed its own snapshot or adopted a neighbor');
 });
 async function beginFullMediaSnapshotProbe(){
  await page.evaluate(()=>{window.fullMediaSnapshotProbe={old:structuredClone(snapshot),base:structuredClone(snapshot.gameMedia.entries['steam-570']),ownerKey:keyFor(selectedActionGame)};});
  const ok=await page.evaluate(()=>updateSharedWallpaper('mode',{mode:'static'}));check(ok,'full media snapshot precondition mode save failed');
  await page.evaluate(()=>{fullMediaSnapshotProbe.accepted=structuredClone(snapshot.gameMedia.entries['steam-570']);});
 }
 await run('full snapshot bridge preserves an accepted newer wallpaper binding',async()=>{
  await beginFullMediaSnapshotProbe();const r=await page.evaluate(()=>{const p=fullMediaSnapshotProbe,old=structuredClone(p.old);old.library.summary={ownedIncomingMarker:'fresh-nonmedia'};const result=workspaceBridgeResult('state',old);return {entry:result.gameMedia.entries[p.base.key],marker:result.library.summary.ownedIncomingMarker,accepted:p.accepted,stored:snapshot.gameMedia.entries[p.base.key],writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length};});progressMetrics.push({case:'full-media-bridge-fence',r});
  check(r.entry.bindingRevision===r.accepted.bindingRevision&&r.entry.wallpaperMode==='static','ordinary snapshot bridge rolled back accepted media');
  check(r.marker==='fresh-nonmedia'&&r.writes===1&&r.stored.bindingRevision===r.accepted.bindingRevision,'media preservation blocked other incoming fields or replayed a save');
 });
 await run('full snapshot application preserves current media for normalized and raw inputs',async()=>{
  await beginFullMediaSnapshotProbe();const rows=await page.evaluate(()=>{const p=fullMediaSnapshotProbe,rows=[];for(const normalized of [false,true]){snapshot.gameMedia.entries[p.base.key]=structuredClone(p.accepted);const incoming=structuredClone(p.old);incoming.library.summary={ownedIncomingMarker:normalized?'already-normalized':'raw'};applyLibrarySnapshot(incoming,{normalized,deferRender:true});rows.push({normalized,entry:snapshot.gameMedia.entries[p.base.key],marker:snapshot.library.summary.ownedIncomingMarker,accepted:p.accepted});}return rows;});progressMetrics.push({case:'full-media-application-fence',rows});
  check(rows.every(r=>r.entry.bindingRevision===r.accepted.bindingRevision&&r.entry.wallpaperMode==='static'),'ordinary snapshot application rolled back accepted media');
  check(rows.every(r=>r.marker===(r.normalized?'already-normalized':'raw')),'media-only reconciliation swallowed valid full snapshot updates');
 });
 await run('silent full snapshot reply cannot undo a wallpaper edit accepted during the read',async()=>{
  await page.evaluate(()=>{const post=window.chrome.webview.postMessage;window.silentMediaSnapshotProbe={held:[],writes:0};window.chrome.webview.postMessage=function(message){if(message.command==='state'){silentMediaSnapshotProbe.held.push({message:structuredClone(message),reply:structuredClone(__LIBRARY_H5_FIXTURE__.snapshot)});return;}return post.call(this,message);};scheduleSilentRefresh();});
  await page.waitForFunction(()=>silentMediaSnapshotProbe.held.length===1);
  const r=await page.evaluate(async()=>{const ok=await updateSharedWallpaper('mode',{mode:'static'}),accepted=structuredClone(snapshot.gameMedia.entries['steam-570']),held=silentMediaSnapshotProbe.held[0];held.reply.library.summary={ownedIncomingMarker:'silent-read-completed'};reverseEmit({kind:'response',id:held.message.id,ok:true,result:held.reply});return {ok,accepted};});
  await page.waitForFunction(()=>!silentRefreshInFlight);const final=await page.evaluate(()=>({entry:snapshot.gameMedia.entries['steam-570'],marker:snapshot.library.summary.ownedIncomingMarker,reads:silentMediaSnapshotProbe.held.length,writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length}));progressMetrics.push({case:'full-media-silent-real-bridge',r,final});
  check(r.ok&&final.entry.bindingRevision===r.accepted.bindingRevision&&final.entry.wallpaperMode==='static'&&final.marker==='silent-read-completed'&&final.reads===1&&final.writes===1,'late ordinary state response undid a wallpaper save or replayed work');
 });
 await run('full media snapshot still applies newer rows deletions and authoritative removals',async()=>{
  await beginFullMediaSnapshotProbe();const r=await page.evaluate(()=>{const p=fullMediaSnapshotProbe,rows=[];for(const state of ['newer','deleted','empty','removed']){const previous=structuredClone(snapshot);previous.gameMedia.entries[p.base.key]=structuredClone(p.accepted);const incoming=structuredClone(previous);if(state==='newer')incoming.gameMedia.entries[p.base.key]={...p.accepted,bindingRevision:p.accepted.bindingRevision+1,wallpaperMode:'dynamic'};if(state==='deleted')incoming.gameMedia.entries[p.base.key]={...p.accepted,bindingRevision:p.accepted.bindingRevision+1,deleted:true};if(state==='empty')incoming.gameMedia.entries={};if(state==='removed')delete incoming.gameMedia.entries[p.base.key];const next=normalizeWorkspaceSnapshot(incoming,previous);rows.push({state,entry:next.gameMedia.entries[p.base.key],count:Object.keys(next.gameMedia.entries).length,expectedCount:Object.keys(incoming.gameMedia.entries).length});}return {rows,accepted:p.accepted};});progressMetrics.push({case:'full-media-authoritative-incoming',r});
  check(r.rows.every(x=>x.state==='newer'?x.entry.bindingRevision===r.accepted.bindingRevision+1&&x.entry.wallpaperMode==='dynamic':x.state==='deleted'?x.entry?.deleted:!x.entry)&&r.rows.every(x=>x.count===x.expectedCount),'media preservation rejected current changes or resurrected authoritative removals');
 });
 await run('media version preservation is scoped to the supplied previous root and identity',async()=>{
  const rows=await page.evaluate(()=>{const original=structuredClone(snapshot),base=structuredClone(snapshot.gameMedia.entries['steam-570']),rows=[];for(const test of ['supplied-previous','root-change','different-AppID']){const previous=structuredClone(original),incoming=structuredClone(original);previous.gameMedia.entries[base.key]={...base,bindingRevision:base.bindingRevision+10,wallpaperMode:'static'};if(test==='root-change')incoming.dataRoot='G:/OtherFullMediaRoot';if(test==='different-AppID')incoming.gameMedia.entries[base.key]={...base,appId:620};const result=normalizeWorkspaceSnapshot(incoming,previous);rows.push({test,entry:result.gameMedia.entries[base.key],base});}return rows;});progressMetrics.push({case:'full-media-root-and-identity',rows});
  check(rows.every(r=>r.test==='supplied-previous'?r.entry.bindingRevision===r.base.bindingRevision+10&&r.entry.wallpaperMode==='static':r.entry.bindingRevision===r.base.bindingRevision&&(r.test!=='different-AppID'||r.entry.appId===620)),'media preservation used unrelated global state or crossed root/AppID boundaries');
 });
 await run('background full snapshot merge remains data only until focus',async()=>{
  await beginFullMediaSnapshotProbe();const hidden=await page.evaluate(()=>{const p=fullMediaSnapshotProbe,libraryNode=document.getElementById('library'),observer=new MutationObserver(()=>{});observer.observe(libraryNode,{attributes:true,childList:true,subtree:true,characterData:true});document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));observer.takeRecords();applyLibrarySnapshot(structuredClone(p.old),{deferRender:true});const mutations=observer.takeRecords().length;observer.disconnect();return {mutations,entry:snapshot.gameMedia.entries[p.base.key],accepted:p.accepted};});await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});progressMetrics.push({case:'full-media-background',hidden});
  check(hidden.mutations===0&&hidden.entry.bindingRevision===hidden.accepted.bindingRevision&&hidden.entry.wallpaperMode==='static','background full media reconciliation painted cards or lost the accepted state');
 });
 await run('full snapshot media reconciliation never mutates either input document',async()=>{
  const r=await page.evaluate(()=>{const p={incoming:structuredClone(snapshot),previous:structuredClone(snapshot)},key='steam-570';p.previous.gameMedia.entries[key]={...p.previous.gameMedia.entries[key],bindingRevision:8};p.incoming.gameMedia.entries[key]={...p.incoming.gameMedia.entries[key],bindingRevision:2};const a=JSON.stringify(p.incoming),b=JSON.stringify(p.previous),result=normalizeWorkspaceSnapshot(p.incoming,p.previous);return {incoming:JSON.stringify(p.incoming)===a,previous:JSON.stringify(p.previous)===b,result:result.gameMedia.entries[key].bindingRevision};});progressMetrics.push({case:'full-media-input-immutability',r});
  check(r.incoming&&r.previous&&r.result===8,'normalization mutated an input document or failed its immutable media merge');
 });
 async function beginFailedVideoFeedbackProbe(){
  await beginManualVideoReceiptProbe();await page.evaluate(()=>{manualVideoReceiptProbe.reject(Error('owned previous video selection failed'));});await page.waitForFunction(()=>!sharedWallpaperBusy);
  await page.evaluate(()=>{cancelArtworkTask();window.videoFeedbackProbe={base:structuredClone(selectedActionGame.gameMedia),ownerKey:keyFor(selectedActionGame)};});
 }
 async function videoFeedbackPanel(restoreReceipt=false){
  return page.evaluate(restore=>{if(restore)snapshot.mediaDownloads=completedVideoReceiptState;renderSharedWallpaperDownloadStatus();const panel=document.getElementById('shared-wallpaper-download-status'),bar=panel.querySelector('progress');return {visible:!panel.classList.contains('hidden'),status:panel.dataset.status,text:panel.textContent,value:bar.hasAttribute('value')?bar.value:null,request:sharedWallpaperDownloadRequests.get(keyFor(selectedActionGame)),entry:selectedActionGame.gameMedia,stored:snapshot.gameMedia.entries[selectedActionGame.gameMedia.key],busy:sharedWallpaperBusy,selects:manualVideoReceiptProbe?.selects?.length||0,imports:reverseIO.calls.filter(x=>x.command==='mediaImport').length,writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length};},restoreReceipt);
 }
 async function importFeedbackVideo(){
  return page.evaluate(async()=>{const waiting=actOnSharedWallpaper('replace');reverseIO.finishPick({path:'G:/H5Fixture/feedback-replacement.mp4'});return waiting;});
 }
 await run('successful local video import retires the old failed selection feedback',async()=>{
  await beginFailedVideoFeedbackProbe();const before=await videoFeedbackPanel(),focusRows=[];
  check(before.visible&&before.status==='error'&&before.text.includes('owned previous video selection failed'),'actual rejected video selection did not establish the feedback precondition');
  try{for(const [width,height] of [[1280,720],[1920,1080]]){
   await page.setViewportSize({width,height});
   const first=await page.evaluate(async()=>{await new Promise(requestAnimationFrame);lastDirectionalAction='';const preview=$('#artwork-wallpaper-preview'),retry=$('#shared-wallpaper-download-status button');preview.focus();const from=document.activeElement.id,retryVisible=isVisuallyPresent(retry);routeWorkspaceAction('navigate-down');const active=document.activeElement,rect=active.getBoundingClientRect();return {from,retryVisible,focus:active.id||editModalRowFor(active)||active.tagName,visible:isVisuallyPresent(active),withinViewport:rect.top>=0&&rect.bottom<=innerHeight};});
   const row={width,height,first};focusRows.push(row);progressMetrics.push({case:'video-feedback-controller',row});
   check(first.from==='artwork-wallpaper-preview'&&first.retryVisible,'wallpaper preview or retry was unavailable before controller navigation');
   check(first.focus==='wallpaper-download-retry'&&first.visible&&first.withinViewport,'Down from wallpaper preview skips the visible retry button');
   row.up=await page.evaluate(()=>{routeWorkspaceAction('navigate-up');return document.activeElement.id;});check(row.up==='artwork-wallpaper-preview','Up from retry should return to the wallpaper preview');
   await page.evaluate(()=>{routeWorkspaceAction('navigate-down');routeWorkspaceAction('accept');});
   await page.waitForFunction(()=>document.activeElement?.matches('[data-video-candidate]'));
   row.back=await page.evaluate(()=>{routeWorkspaceAction('back');return {focus:document.activeElement?.dataset.editRow,modal:activeWorkspaceModal()?.id,searches:reverseIO.calls.filter(x=>x.command==='mediaSearchVideos').length};});
   check(row.back.focus==='wallpaper-download-retry'&&row.back.modal==='edit-modal'&&row.back.searches===focusRows.length+1,'B did not return from the reopened gallery to retry, or search ran more than once');
  }}finally{await page.setViewportSize({width:1440,height:1100});}
  const ok=await importFeedbackVideo(),after=await videoFeedbackPanel();
  const returned=await page.evaluate(()=>document.activeElement?.id);
  progressMetrics.push({case:'feedback-import-after-rejection',before,after,ok,returned});
  check(ok&&!after.visible&&after.entry.assets.dynamic.id!==before.entry.assets.dynamic.id&&!after.busy,'successful local import retained the previous failed selection feedback');
  check(after.imports===1&&after.selects===1,'retiring obsolete feedback replayed a video selection or import');
  check(returned==='artwork-wallpaper-preview','retiring a focused retry button did not return focus to the preview');
  await page.evaluate(()=>{
   const retry=$('#shared-wallpaper-download-status button');
   setSharedWallpaperDownloadRequest(captureSharedWallpaperWrite(),{phase:'error',error:'new request failed'});
   renderSharedWallpaperDownloadStatus();retry.focus();routeWorkspaceAction('accept');
  });
  await page.waitForFunction(()=>document.activeElement?.matches('[data-video-candidate]'));
  await page.evaluate(async()=>{routeWorkspaceAction('accept');await manualVideoReceiptProbe.finish();});
  const accepted=await manualVideoReceiptResult(),focusAfterAccept=await page.evaluate(()=>document.activeElement?.id);
  progressMetrics.push({case:'video-feedback-controller-accepted',focusAfterAccept,galleryHidden:accepted.galleryHidden,selects:accepted.selects,phase:accepted.phase});
  check(accepted.galleryHidden&&accepted.phase==='queued'&&accepted.selects===2&&focusAfterAccept==='artwork-wallpaper-preview','accepting a video through retry did not queue once and restore preview focus');
 });
 await run('changed video asset cannot claim completion while replacement remains pending',async()=>{
  await beginFailedVideoFeedbackProbe();await page.evaluate(()=>{const p=videoFeedbackProbe,entry={...p.base,bindingRevision:p.base.bindingRevision+2,refreshRequested:true,refreshSlot:'dynamic',taskSlots:['dynamic'],assets:{...p.base.assets,dynamic:{...p.base.assets.dynamic,id:'intermediate-published-video'}}};selectedActionGame.gameMedia=entry;snapshot.gameMedia.entries[entry.key]=entry;setSharedWallpaperDownloadRequest(captureSharedWallpaperWrite(),{phase:'queued',revision:p.base.bindingRevision,assetId:p.base.assets.dynamic.id});snapshot.mediaDownloads={active:{[entry.key]:{key:entry.key,phase:'downloading',bytes:50,totalBytes:100}},manualItems:{[entry.key]:{state:'done',requestBindingRevision:p.base.bindingRevision}},manualUntil:Date.now()+60000};});const r=await videoFeedbackPanel();progressMetrics.push({case:'feedback-pending-after-asset-change',r});
  check(r.visible&&r.status==='downloading'&&r.value===0.5&&!r.text.includes('已下载并应用'),'changed asset prematurely completed an ongoing replacement');
  const rows=await page.evaluate(()=>{
   const p=videoFeedbackProbe,entry={...p.base,bindingRevision:p.base.bindingRevision+2,refreshRequested:true,refreshSlot:'dynamic',taskSlots:['dynamic']};
   selectedActionGame.gameMedia=entry;snapshot.gameMedia.entries[entry.key]=entry;
   setSharedWallpaperDownloadRequest(captureSharedWallpaperWrite(),{phase:'queued',revision:entry.bindingRevision,assetId:p.base.assets.dynamic.id});
   const output=[];
   for(const [label,revision,phase,useCurrent,expected] of [
    ['older active transfer',p.base.bindingRevision,'downloading',false,'pending'],
    ['older current fallback',p.base.bindingRevision,'downloading',true,'pending'],
    ['older retry',p.base.bindingRevision,'retrying',false,'pending'],
    ['older publication',p.base.bindingRevision,'publishing',false,'pending'],
    ['older automatic revision zero',0,'downloading',false,'pending'],
    ['current transfer',entry.bindingRevision,'downloading',false,'downloading'],
    ['current retry',entry.bindingRevision,'retrying',false,'retrying'],
    ['legacy missing revision',null,'downloading',false,'downloading']
   ]){
    const active={key:entry.key,phase,bytes:50,totalBytes:100};if(revision!==null)active.requestBindingRevision=revision;
    snapshot.mediaDownloads={active:useCurrent?{}:{[entry.key]:active},current:useCurrent?active:null,manualItems:{[entry.key]:{state:'pending',requestBindingRevision:entry.bindingRevision}}};
    renderSharedWallpaperDownloadStatus();const panel=document.getElementById('shared-wallpaper-download-status'),bar=panel.querySelector('progress');
    output.push({label,expected,status:panel.dataset.status,text:panel.textContent,value:bar.hasAttribute('value')?bar.value:null,visible:!panel.classList.contains('hidden')});
   }
   return output;
  });progressMetrics.push({case:'feedback-active-request-revision',rows});
  check(rows.every(row=>row.visible&&row.status===row.expected&&(row.expected!=='pending'||row.value===null&&row.text.includes('已加入下载队列')&&!row.text.includes('50%'))&&(row.expected!=='downloading'||row.value===0.5)),'older video transfer progress was shown for a newer queued selection');

 });
 await run('old terminal queue error cannot follow a successful local video replacement',async()=>{
  await beginFailedVideoFeedbackProbe();await page.evaluate(()=>{
   const p=videoFeedbackProbe;snapshot.mediaDownloads={active:{},manualItems:{[p.base.key]:{state:'unavailable',autoStopped:true,lastError:'media-network-request-failed',requestBindingRevision:p.base.bindingRevision}},manualUntil:0};
   const actual=invoke;invoke=async(command,args,...rest)=>{const result=await actual(command,args,...rest);if(command==='mediaImport'&&args.slot==='dynamic'){
    for(const entry of [result,__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key]]){
     delete entry.selectedVideo;entry.refreshRequested=false;entry.taskSlots=[];
    }
   }return result;};
  });const ok=await importFeedbackVideo();const r=await videoFeedbackPanel();progressMetrics.push({case:'feedback-old-terminal-after-import',r,ok});
  check(ok&&!r.visible&&r.imports===1&&r.selects===1,'obsolete terminal queue error was assigned to the replacement asset');
  const reopened=await page.evaluate(()=>{
   const game=selectedActionGame,key=game.gameMedia.key,state=snapshot.mediaDownloads;
   closeEdit();sharedWallpaperDownloadRequests.clear();openEdit(game);snapshot.mediaDownloads=state;
   const entry=selectedActionGame.gameMedia,old=state.manualItems[key];
   const panel=$('#shared-wallpaper-download-status');
   const sample=(label,next)=>{snapshot.mediaDownloads=next;renderSharedWallpaperDownloadStatus();return {label,visible:isVisuallyPresent(panel),retry:isVisuallyPresent(panel.querySelector('button')),status:panel.dataset.status,text:panel.textContent};};
   const running={key,phase:'downloading',bytes:50,totalBytes:100};
   const rows=[
    sample('retained failure',state),
    sample('unversioned retained failure',{...state,manualItems:{[key]:{...old,requestBindingRevision:undefined}}}),
    sample('older active transfer',{...state,active:{[key]:{...running,requestBindingRevision:old.requestBindingRevision}}}),
    sample('newer active transfer',{...state,active:{[key]:{...running,requestBindingRevision:entry.bindingRevision+1}}}),
    sample('newer terminal failure',{...state,manualItems:{[key]:{...old,requestBindingRevision:entry.bindingRevision+1}}}),
   ];
   snapshot.mediaDownloads=state;
   return {entry,rows,requestCount:sharedWallpaperDownloadRequests.size,oldRevision:old.requestBindingRevision};
  });progressMetrics.push({case:'feedback-reopened-after-import',reopened});
  check(reopened.requestCount===0&&reopened.entry.assets.dynamic.id===r.entry.assets.dynamic.id&&reopened.entry.manualSlots.dynamic&&!reopened.entry.selectedVideo&&reopened.entry.bindingRevision>reopened.oldRevision,'reopened card did not contain the accepted manual video and the independent older receipt');
  check(reopened.rows.slice(0,3).every(row=>!row.visible&&!row.retry),'reopened local video showed superseded queue feedback');
  check(reopened.rows[3].visible&&reopened.rows[3].status==='downloading'&&!reopened.rows[3].retry&&reopened.rows[4].visible&&reopened.rows[4].status==='error'&&reopened.rows[4].retry,'a newer request ahead of the game media snapshot lost its feedback');
 });
 await run('a replaced failed selection yields to current video byte progress',async()=>{
  await beginFailedVideoFeedbackProbe();await page.evaluate(()=>{const p=videoFeedbackProbe,entry={...p.base,bindingRevision:p.base.bindingRevision+3,refreshRequested:true,refreshSlot:'dynamic',taskSlots:['dynamic'],assets:{...p.base.assets,dynamic:{...p.base.assets.dynamic,id:'new-current-video'}}};selectedActionGame.gameMedia=entry;snapshot.gameMedia.entries[entry.key]=entry;snapshot.mediaDownloads={active:{[entry.key]:{key:entry.key,phase:'downloading',bytes:75,totalBytes:100}},manualItems:{[entry.key]:{state:'pending',requestBindingRevision:entry.bindingRevision}},manualUntil:Date.now()+60000};});const r=await videoFeedbackPanel();progressMetrics.push({case:'feedback-new-progress-after-old-error',r});
  check(r.visible&&r.status==='downloading'&&r.value===0.75&&!r.text.includes('owned previous video selection failed'),'superseded local error hid the current video transfer progress');
 });
 await run('unchanged video keeps its real failed selection through playback edits',async()=>{
  await beginFailedVideoFeedbackProbe();await page.evaluate(()=>{reverseChoose(5);});await settle();const r=await videoFeedbackPanel();progressMetrics.push({case:'feedback-real-error-same-asset',r});
  check(r.visible&&r.status==='error'&&r.text.includes('owned previous video selection failed')&&r.entry.playback.start===5&&r.selects===1&&r.writes===1,'an unrelated playback revision concealed the unresolved video selection failure');
 });
 await run('completed replacement still shows its own completion feedback',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(()=>{const p=manualVideoReceiptProbe;p.beforeReply=receipt=>{const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[receipt.key];entry.selectedVideo.selectionToken=p.selects[0].searchToken;receipt.selectedVideo=structuredClone(entry.selectedVideo);};return p.finish();});await page.waitForFunction(()=>!sharedWallpaperBusy);
  const rows=await page.evaluate(()=>{
   const base=selectedActionGame.gameMedia,key=base.key,ownerKey=keyFor(selectedActionGame),request=sharedWallpaperDownloadRequests.get(ownerKey);
   const entry={...base,bindingRevision:base.bindingRevision+1,refreshRequested:false,taskSlots:[],dynamicReady:true,manualSlots:{...base.manualSlots,dynamic:false},assets:{...base.assets,dynamic:{...base.assets.dynamic,id:'completed-current-video'}}};
   const item={state:'done',requestBindingRevision:entry.bindingRevision};
   const state={active:{},manualItems:{[key]:item}};
   const controls=[
    ['own',entry,state,request],
    ['reopened',entry,state],
    ['playback advanced revision',{...entry,bindingRevision:entry.bindingRevision+2},state],
    ['legacy local request',entry,{active:{}},{phase:'queued',revision:base.bindingRevision,assetId:base.assets.dynamic.id}],
    ['no video',{...entry,assets:{static:entry.assets.static},dynamicReady:false,selectedVideo:undefined},state],
    ['invalid asset',{...entry,invalidSlots:{dynamic:entry.assets.dynamic.id}},state,request],
    ['missing on disk',{...entry,dynamicReady:false},state],
    ['queue ahead of card',entry,{active:{},manualItems:{[key]:{...item,requestBindingRevision:entry.bindingRevision+1}}}],
    ['unrelated job',{...entry,selectedVideo:undefined},state],
    ['superseded local choice',{...entry,selectedVideo:{...entry.selectedVideo,selectionToken:'another-choice'}},{active:{}},request],
   ];
   // Store publication advances the manual receipt, then a local import
   // updates the Store without clearing that separate queue receipt.
   selectedActionGame.gameMedia=entry;snapshot.gameMedia.entries[key]=entry;
   __LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[key]=structuredClone(entry);
   window.completedVideoReceiptState=state;snapshot.mediaDownloads=state;sharedWallpaperDownloadRequests.delete(ownerKey);
   const actual=invoke;invoke=async(command,args,...rest)=>{const result=await actual(command,args,...rest);if(command==='mediaImport'&&args.slot==='dynamic'){delete result.selectedVideo;delete __LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key].selectedVideo;}return result;};
   return {request,token:entry.selectedVideo.selectionToken,controls:controls.map(([label,media,status,local])=>({label,feedback:sharedWallpaperDownloadFeedback(media,status,local)}))};
  });
  const ok=await importFeedbackVideo(),imported=await videoFeedbackPanel(true);progressMetrics.push({case:'feedback-completed-own-replacement',rows,ok,imported});
  check(ok&&!imported.visible&&imported.entry.manualSlots.dynamic&&!imported.entry.selectedVideo&&imported.imports===1&&imported.selects===1,'retained download receipt claimed a local import was downloaded');
  check(rows.request?.selectionToken===rows.token&&rows.request?.mediaKey===imported.entry.key,'queued native acknowledgement lost the accepted video selection');
  for(const {label,feedback} of rows.controls)check(['own','reopened','playback advanced revision','legacy local request'].includes(label)?feedback?.status==='done'&&feedback?.value===1:feedback===null,'incorrect completion feedback: '+label);
 });
 await run('background video import defers removal of superseded error until normal focus',async()=>{
  await beginFailedVideoFeedbackProbe();await page.evaluate(()=>{const p=videoFeedbackProbe,panel=document.getElementById('shared-wallpaper-download-status');p.beforeText=panel.textContent;p.mutations=0;p.observer=new MutationObserver(records=>p.mutations+=records.length);p.observer.observe(panel,{attributes:true,childList:true,subtree:true,characterData:true});document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));p.mutations=0;p.observer.takeRecords();});const ok=await importFeedbackVideo();const hidden=await page.evaluate(()=>{const p=videoFeedbackProbe;p.mutations+=p.observer.takeRecords().length;p.observer.disconnect();return {mutations:p.mutations,pending:editorArtworkRenderPending,kept:document.getElementById('shared-wallpaper-download-status').textContent===p.beforeText};});await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});const r=await videoFeedbackPanel();progressMetrics.push({case:'feedback-background-import',hidden,r,ok});
  check(ok&&hidden.mutations===0&&hidden.pending&&hidden.kept&&!r.visible&&r.imports===1&&r.selects===1,'background replacement painted its feedback or failed to retire the error on focus');
 });
 await run('feedback preserves current terminal retry cancellation and power meanings',async()=>{
  const rows=await page.evaluate(()=>{const entry={key:'steam-owned-feedback',bindingRevision:10,wallpaperMode:'dynamic',refreshRequested:true,refreshSlot:'dynamic',assets:{dynamic:{id:'owned-old'}}},request={phase:'queued',revision:10,assetId:'owned-old',mediaKey:entry.key},item={requestBindingRevision:10},rows=[];for(const [label,state]of [
   ['retry',{active:{},manualItems:{[entry.key]:{...item,state:'pending',lastError:'media-network-request-failed',retryAt:Date.now()+20000}},manualUntil:Date.now()+60000}],
   ['terminal',{active:{},manualItems:{[entry.key]:{...item,state:'unavailable',autoStopped:true,lastError:'media-task-time-budget',stopReason:'automatic-time-budget-exhausted'}},pauseReason:'game-running'}],
   ['cancelled',{active:{},manualItems:{[entry.key]:{...item,state:'pending'}},manualState:'cancelled',pauseReason:'game-running'}],
   ['terminal after cancel',{active:{},manualItems:{[entry.key]:{...item,state:'unavailable',autoStopped:true,lastError:'media-task-time-budget',stopReason:'automatic-time-budget-exhausted'}},manualState:'cancelled',pauseReason:'game-running'}],
   ['expired after cancel',{active:{},manualItems:{[entry.key]:{...item,state:'pending',lastError:'media-video-selection-expired'}},manualState:'cancelled'}],
   ['power',{active:{[entry.key]:{key:entry.key,phase:'downloading',bytes:10,totalBytes:100}},manualItems:{},manualUntil:Date.now()+60000,manualTargets:[entry.key],resourcePauseReason:'on-battery'}]
  ])rows.push({label,feedback:sharedWallpaperDownloadFeedback(entry,state,request)});
  const failed={...entry,error:'media-network-request-failed'},idle={current:{},active:{},manualItems:{}};
  const drained={...idle,remaining:0,nextDueAt:-1,activeCount:0,workerPid:0};
  const pendingItem=retryAt=>({...idle,remaining:1,nextDueAt:retryAt,manualItems:{[entry.key]:{...item,state:'pending',lastError:'media-network-request-failed',retryAt}}});
  for(const [label,media,state,local] of [
   ['unbound idle',{},idle],
   ['unbound submitting',{},idle,{phase:'submitting'}],
   ['unbound request failure',{},idle,{phase:'error',error:'owned request failed'}],
   ['foreign manual',{...failed},{...idle,manualUntil:Date.now()+60000,manualTargets:['steam-other']}],
   ['own manual',failed,{...idle,manualUntil:Date.now()+60000,manualTargets:[entry.key]}],
   ['bulk manual',failed,{...idle,manualUntil:Date.now()+60000}],
   ['invalid manual lifetime',failed,{...idle,manualUntil:Date.now()+1800000,manualTargets:[entry.key]}],
   ['own timed retry while another manual runs',{...failed,retryAt:Date.now()+20000},{...idle,manualUntil:Date.now()+60000,manualTargets:['steam-other']}],
   ['expired cache retry without queue',{...failed,error:'media-task-time-budget',retryAt:Date.now()-20000},idle],
   ['drained queue with newer cache timestamp',{...failed,retryAt:Date.now()+20000},{...drained,resourcePauseReason:'cpu-busy'}],
   ['current retry canceled deadline',{...failed,retryAt:Date.now()+20000},pendingItem(0)],
   ['current overdue queue item',failed,pendingItem(Date.now()-1000)],
   ['current scheduled queue item',failed,pendingItem(Date.now()+20000)],
   ['unreadable summary cannot prove terminal state',failed,{...drained,resourcePauseReason:'on-battery',checkpointReadable:false}],
   ['no error keeps power pause',entry,{...drained,resourcePauseReason:'on-battery'}],
   ['legacy current fallback',entry,{...idle,current:{key:entry.key,phase:'downloading',bytes:15,totalBytes:100}}],
  ])rows.push({label,feedback:sharedWallpaperDownloadFeedback(media,state,local)});
  const game=selectedActionGame,savedMedia=game.gameMedia,savedStatus=snapshot.mediaDownloads;
  try{
   game.gameMedia={wallpaperMode:'dynamic'};snapshot.mediaDownloads=idle;
   paintSharedWallpaperDownloadStatus();
   rows.push({label:'unbound panel',visible:!document.getElementById('shared-wallpaper-download-status').classList.contains('hidden')});
  }finally{game.gameMedia=savedMedia;snapshot.mediaDownloads=savedStatus;paintSharedWallpaperDownloadStatus();}
  return rows;});progressMetrics.push({case:'feedback-current-controls',rows});
  check(rows.find(x=>x.label==='retry').feedback.status==='retrying'&&rows.find(x=>x.label==='terminal').feedback.status==='error'&&rows.find(x=>x.label==='cancelled').feedback.text.includes('已停止')&&rows.find(x=>x.label==='power').feedback.status==='waiting','feedback freshness changed current retry, terminal, cancellation or power policy meanings');
  const terminal=rows.find(x=>x.label==='terminal after cancel').feedback,expired=rows.find(x=>x.label==='expired after cancel').feedback;
  check(terminal.status==='error'&&terminal.canSearch&&terminal.text.includes('时间预算已用完'),'batch cancel hid a terminal per-game download timeout');
  check(expired.status==='error'&&expired.canSearch&&expired.text.includes('请重新选择'),'batch cancel hid an expired per-game candidate');
  const feedback=label=>rows.find(r=>r.label===label).feedback;
  check(feedback('unbound idle')===null&&!rows.find(r=>r.label==='unbound panel').visible,'idle queue showed a phantom transfer on an unbound card');
  check(feedback('foreign manual')?.status==='error'&&feedback('foreign manual').canSearch,'another game supplied a false retry authorization');
  check(feedback('invalid manual lifetime')?.status==='error','out-of-range manual deadline supplied a false retry authorization');
  check(feedback('own manual')?.status==='retrying'&&feedback('bulk manual')?.status==='retrying'&&feedback('own timed retry while another manual runs')?.status==='retrying','a real scoped, bulk or scheduled retry lost its status');
  check(feedback('unbound submitting')?.status==='pending'&&feedback('unbound request failure')?.status==='error','a detached request lost its local status before binding');
  check(feedback('legacy current fallback')?.status==='downloading'&&feedback('legacy current fallback')?.value===0.15,'keyed legacy current transfer lost its progress');
  check(feedback('expired cache retry without queue')?.status==='error'&&feedback('expired cache retry without queue').canSearch&&feedback('expired cache retry without queue').text.includes('下载超时')&&feedback('expired cache retry without queue').text.includes('当前壁纸保留'),'expired cache retry is still shown as live work');
  check(feedback('drained queue with newer cache timestamp')?.status==='error'&&feedback('drained queue with newer cache timestamp').canSearch,'drained queue borrowed future retry or another pause from shared state');
  check(['current retry canceled deadline','current overdue queue item'].every(label=>feedback(label)?.status==='pending'&&feedback(label).text.includes('等待处理')),'eligible unscheduled item was shown as backing off or terminal');
  check(feedback('current scheduled queue item')?.status==='retrying','current scheduled retry lost its feedback');
  check(['unreadable summary cannot prove terminal state','no error keeps power pause'].every(label=>feedback(label)?.status==='waiting'&&feedback(label).text.includes('接通电源')),'empty counters hid a real power pause or were trusted despite a read failure');
 });
 async function beginWallpaperPreparationProbe(first=false){
  await page.evaluate(first=>{
   const actual=invoke,preview=renderSharedWallpaperPreview;window.wallpaperPreparationProbe={base:structuredClone(selectedActionGame.gameMedia),requests:[],paints:0};
   const p=wallpaperPreparationProbe;p.key=keyFor(selectedActionGame);if(first){delete snapshot.gameMedia.entries[p.base.key];selectedActionGame.gameMedia=null;}
   renderSharedWallpaperPreview=(...args)=>{p.paints++;return preview(...args);};
   invoke=async(command,args={},timeout=0)=>{if(command!=='mediaCreate')return actual(command,args,timeout);p.requests.push(structuredClone(args));const receipt=await actual(command,args,timeout);return new Promise((resolve,reject)=>{p.receipt=structuredClone(receipt);p.reply=(overrides={},replace=false)=>resolve(replace?overrides:{...receipt,...overrides});p.reject=reject;});};
   window.wallpaperPreparationResult=prepareSharedWallpaperPreview(selectedActionGame);
  },first);
  await page.waitForFunction(()=>wallpaperPreparationProbe.reply);
  await page.evaluate(()=>{wallpaperPreparationProbe.before=wallpaperPreparationProbe.paints;});
 }
 async function wallpaperPreparationResult(){
  await page.evaluate(()=>wallpaperPreparationResult);
  return page.evaluate(()=>{const p=wallpaperPreparationProbe;return {entry:snapshot.gameMedia?.entries?.[p.base.key],selected:selectedActionGame?.gameMedia,key:keyFor(selectedActionGame),requests:p.requests.length,paints:p.paints-p.before,pending:editorArtworkRenderPending,busy:sharedWallpaperBusy,base:p.base,notices:[toastNotice.textContent,...noticeQueue.map(x=>x.message)],calls:reverseIO.calls.filter(x=>['mediaCreate','mediaEdit','mediaImport'].includes(x.command))};});
 }
 await run('wallpaper preparation uses a newer complete snapshot before an old reply',async()=>{
  await beginWallpaperPreparationProbe();await page.evaluate(()=>{const p=wallpaperPreparationProbe,fresh={...p.base,bindingRevision:p.base.bindingRevision+4,wallpaperMode:'static',playback:{start:20,end:50,fit:'contain'}};const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=fresh;applyLibrarySnapshot(next,{deferRender:true});p.before=p.paints;p.reply();});const r=await wallpaperPreparationResult();progressMetrics.push({case:'preparation-newer-complete-snapshot',r});
  check(r.selected.bindingRevision===r.base.bindingRevision+4&&r.selected.wallpaperMode==='static'&&r.entry.bindingRevision===r.selected.bindingRevision,'preparation ignored the newer full media snapshot');
  check(r.requests===1&&r.calls.filter(x=>x.command==='mediaEdit').length===0&&!r.busy,'preparation replayed a business write or retained busy');
 });
 await run('wallpaper preparation rejects foreign keys AppIDs and invalid versions',async()=>{
  const rows=[];for(const field of ['key','appId','bindingRevision']){if(rows.length)await reset();await beginWallpaperPreparationProbe();await page.evaluate(field=>wallpaperPreparationProbe.reply({[field]:field==='key'?'steam-foreign':field==='appId'?620:'invalid-version'}),field);rows.push({field,...await wallpaperPreparationResult()});}progressMetrics.push({case:'preparation-invalid-receipt',rows});
  check(rows.every(r=>r.selected.key===r.base.key&&r.selected.appId===r.base.appId&&r.selected.bindingRevision===r.base.bindingRevision&&r.paints===0),'preparation attached an invalid identity or version receipt');
  check(rows.every(r=>r.requests===1&&r.notices.some(x=>x.includes('回执'))),'invalid preparation receipt was silent or retried');
 });
 await run('wallpaper preparation publishes a first registration into the local snapshot',async()=>{
  await beginWallpaperPreparationProbe(true);await page.evaluate(()=>wallpaperPreparationProbe.reply());const r=await wallpaperPreparationResult();progressMetrics.push({case:'preparation-first-registration',r});
  check(r.entry?.key===r.base.key&&r.selected.key===r.entry.key&&r.entry.appId===r.base.appId,'first prepared media was missing from the current snapshot');
  const saved=await page.evaluate(async()=>{const ok=await updateSharedWallpaper('mode',{mode:'static'});return {ok,entry:snapshot.gameMedia.entries['steam-570'],selected:selectedActionGame.gameMedia,writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length,creates:wallpaperPreparationProbe.requests.length};});progressMetrics.at(-1).saved=saved;
  check(saved.ok&&saved.entry.wallpaperMode==='static'&&saved.selected.wallpaperMode==='static'&&saved.writes===1&&saved.creates===1,'first prepared record was treated as a removed entry during its next explicit save');
 });
 await run('wallpaper preparation cannot repaint deleted or removed media',async()=>{
  const rows=[];for(const removed of [false,true]){if(rows.length)await reset();await beginWallpaperPreparationProbe();await page.evaluate(removed=>{const p=wallpaperPreparationProbe;if(removed)delete snapshot.gameMedia.entries[p.base.key];else snapshot.gameMedia.entries[p.base.key]={...p.base,deleted:true,bindingRevision:p.base.bindingRevision+3};p.selectedBefore=selectedActionGame.gameMedia;p.reply();},removed);const r=await wallpaperPreparationResult();const kept=await page.evaluate(()=>selectedActionGame.gameMedia===wallpaperPreparationProbe.selectedBefore);rows.push({removed,...r,kept});}
  progressMetrics.push({case:'preparation-removed-media',rows});check(rows.every(r=>(r.removed?!r.entry:r.entry?.deleted)&&r.kept&&r.paints===0&&r.requests===1),'preparation repainted or rebound deleted or removed media');
 });
 await run('wallpaper preparation never inherits preview URLs for changed assets',async()=>{
  await beginWallpaperPreparationProbe();await page.evaluate(()=>{const p=wallpaperPreparationProbe,asset={...p.base.assets.dynamic,id:'changed-preparation-asset',revision:'changed'};const entry={...p.receipt,bindingRevision:p.base.bindingRevision+1,assets:{...p.base.assets,dynamic:asset},asset};delete entry.dynamicPreviewUrl;delete entry.previewUrl;p.reply(entry,true);});const r=await wallpaperPreparationResult();progressMetrics.push({case:'preparation-changed-asset-preview',r});
  check(r.selected.assets.dynamic.id==='changed-preparation-asset'&&!r.selected.dynamicPreviewUrl&&!r.selected.previewUrl&&r.entry.bindingRevision===r.base.bindingRevision+1,'preparation reused an old preview for a changed immutable asset');
 });
 await run('wallpaper preparation keeps exact unchanged asset previews from older hosts',async()=>{
  await beginWallpaperPreparationProbe();await page.evaluate(()=>{const p=wallpaperPreparationProbe,entry={...p.receipt,bindingRevision:p.base.bindingRevision+1};delete entry.dynamicPreviewUrl;delete entry.staticPreviewUrl;delete entry.previewUrl;p.reply(entry,true);});const r=await wallpaperPreparationResult();progressMetrics.push({case:'preparation-unchanged-preview-compat',r});
  check(r.selected.dynamicPreviewUrl===r.base.dynamicPreviewUrl&&r.selected.staticPreviewUrl===r.base.staticPreviewUrl&&r.selected.previewUrl===r.base.previewUrl&&r.entry.bindingRevision===r.base.bindingRevision+1,'exact unchanged asset lost its compatible mapped previews');
 });
 await run('wallpaper preparation preserves a newer active editor record',async()=>{
  await beginWallpaperPreparationProbe();await page.evaluate(()=>{const p=wallpaperPreparationProbe;selectedActionGame.gameMedia={...p.base,bindingRevision:p.base.bindingRevision+6,wallpaperMode:'static'};p.reply();});const r=await wallpaperPreparationResult();progressMetrics.push({case:'preparation-newer-editor-record',r});
  check(r.selected.bindingRevision===r.base.bindingRevision+6&&r.selected.wallpaperMode==='static'&&r.entry.bindingRevision===r.selected.bindingRevision&&r.requests===1,'preparation lost the newest active editor record or left the snapshot behind it');
 });
 await run('retired page wallpaper preparation success and failure cannot affect the resumed page',async()=>{
  const rows=[];for(const fail of [false,true]){if(rows.length)await reset();await beginWallpaperPreparationProbe();await page.evaluate(fail=>{const p=wallpaperPreparationProbe;p.selectedBefore=selectedActionGame.gameMedia;window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));p.before=p.paints;if(fail)p.reject(Error('owned retired preparation denied'));else p.reply({...p.receipt,bindingRevision:p.base.bindingRevision+1,wallpaperMode:'static'});},fail);const r=await wallpaperPreparationResult();const same=await page.evaluate(()=>selectedActionGame.gameMedia===wallpaperPreparationProbe.selectedBefore);rows.push({fail,...r,same});}progressMetrics.push({case:'preparation-retired-page',rows});
  check(rows.every(r=>r.same&&r.paints===0&&!r.notices.some(x=>x.includes('owned retired preparation denied'))&&r.requests===1),'retired page preparation changed the resumed editor or displayed its stale failure');
 });
 await run('background wallpaper preparation stores data and defers preview painting',async()=>{
  await beginWallpaperPreparationProbe();await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const p=wallpaperPreparationProbe;p.before=p.paints;p.reply({...p.receipt,bindingRevision:p.base.bindingRevision+1,wallpaperMode:'static'});});const hidden=await wallpaperPreparationResult();await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});const visible=await page.evaluate(()=>({pending:editorArtworkRenderPending,pressed:document.querySelector('[data-shared-wallpaper-mode="static"]').getAttribute('aria-pressed')}));progressMetrics.push({case:'preparation-background',hidden,visible});
  check(hidden.entry.bindingRevision===hidden.base.bindingRevision+1&&hidden.selected.wallpaperMode==='static'&&hidden.pending&&hidden.paints===0&&visible.pressed==='true'&&!visible.pending,'background preparation failed data settlement or painted before foreground');
 });
 await run('wallpaper preparation cannot cross a changed data root or media binding',async()=>{
  const rows=[];for(const root of [false,true]){if(rows.length)await reset();await beginWallpaperPreparationProbe();await page.evaluate(root=>{const p=wallpaperPreparationProbe;if(root)snapshot.dataRoot='G:/ChangedPreparationDataRoot';else selectedActionGame.gameMedia={...p.base,key:'exe-current-media-binding',appId:0,bindingRevision:8};p.selectedBefore=selectedActionGame.gameMedia;p.entryBefore=JSON.stringify(snapshot.gameMedia.entries[p.base.key]);p.reply({...p.receipt,bindingRevision:p.base.bindingRevision+1});},root);const r=await wallpaperPreparationResult();const same=await page.evaluate(()=>({editor:selectedActionGame.gameMedia===wallpaperPreparationProbe.selectedBefore,entry:JSON.stringify(snapshot.gameMedia.entries[wallpaperPreparationProbe.base.key])===wallpaperPreparationProbe.entryBefore}));rows.push({root,...r,same});}progressMetrics.push({case:'preparation-root-and-binding',rows});
  check(rows.every(r=>r.same.editor&&r.same.entry&&r.paints===0&&r.requests===1),'preparation crossed its captured data root or current editor binding');
 });
 async function beginWallpaperWriteFenceProbe(kind='edit'){
  await page.evaluate(kind=>{
   const actual=invoke;window.wallpaperWriteFenceProbe={kind,base:structuredClone(snapshot.gameMedia.entries['steam-570']),ownerKey:keyFor(selectedActionGame),requests:[],native:null};
   invoke=async(command,args={},timeout=0)=>{const p=wallpaperWriteFenceProbe,match=kind==='import'?command==='mediaImport':command==='mediaEdit'&&args.action==='mode';if(!match)return actual(command,args,timeout);p.requests.push({command,args:structuredClone(args)});const receipt=await actual(command,args,timeout);p.native=structuredClone(receipt);return new Promise((resolve,reject)=>{p.finish=(overrides={})=>{p.beforeReply?.();resolve({...receipt,...overrides});};p.reject=reject;});};
   if(kind==='import'){window.wallpaperWriteFenceResult=actOnSharedWallpaper('replace');reverseIO.finishPick({path:'G:/H5Fixture/receipt-fence.mp4'});}
   else window.wallpaperWriteFenceResult=updateSharedWallpaper('mode',{mode:'static'});
  },kind);
  await page.waitForFunction(()=>wallpaperWriteFenceProbe.finish&&sharedWallpaperBusy);
 }
 async function wallpaperWriteFenceResult(){
  const ok=await page.evaluate(()=>wallpaperWriteFenceResult);await settle();
  return page.evaluate(ok=>{const p=wallpaperWriteFenceProbe;return {ok,entry:snapshot.gameMedia.entries[p.base.key],selected:selectedActionGame?.gameMedia,key:keyFor(selectedActionGame),busy:sharedWallpaperBusy,requests:p.requests,base:p.base,native:p.native,pending:sharedWallpaperPendingPlayback.size,calls:reverseIO.calls.filter(x=>['mediaCreate','mediaEdit','mediaImport'].includes(x.command)),notices:[toastNotice.textContent,...noticeQueue.map(x=>x.message)]};},ok);
 }
 await run('accepted wallpaper edit receipt cannot roll back a newer full snapshot',async()=>{
  await beginWallpaperWriteFenceProbe();await page.evaluate(()=>{const p=wallpaperWriteFenceProbe;const fresh={...p.native,bindingRevision:p.native.bindingRevision+4,wallpaperMode:'dynamic',playback:{start:20,end:50,fit:'contain'}};const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=fresh;applyLibrarySnapshot(next,{deferRender:true});p.finish();});const r=await wallpaperWriteFenceResult();progressMetrics.push({case:'edit-receipt-newer-snapshot',r});
  check(r.entry.bindingRevision===r.native.bindingRevision+4&&r.entry.wallpaperMode==='dynamic'&&r.entry.playback.start===20,'late accepted wallpaper edit rolled back a newer snapshot');
  check(r.ok&&r.selected.bindingRevision===r.entry.bindingRevision&&r.requests.length===1&&!r.busy,'superseded accepted edit lost success, current editor baseline or single dispatch');
 });
 await run('accepted wallpaper import receipt cannot roll back a newer full snapshot',async()=>{
  await beginWallpaperWriteFenceProbe('import');await page.evaluate(()=>{const p=wallpaperWriteFenceProbe,asset={...p.native.assets.dynamic,id:'newer-import-owner',revision:'newer'};const fresh={...p.native,bindingRevision:p.native.bindingRevision+3,assets:{...p.native.assets,dynamic:asset},dynamicPreviewUrl:'/sample.mp4?newer-binding'};const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=fresh;applyLibrarySnapshot(next,{deferRender:true});p.finish();});const r=await wallpaperWriteFenceResult();progressMetrics.push({case:'import-receipt-newer-snapshot',r});
  check(r.entry.bindingRevision===r.native.bindingRevision+3&&r.entry.assets.dynamic.id==='newer-import-owner'&&r.entry.dynamicPreviewUrl.includes('newer-binding'),'late accepted wallpaper import rolled back a newer snapshot');
  check(r.ok&&r.selected.assets.dynamic.id===r.entry.assets.dynamic.id&&r.requests.length===1&&!r.busy,'superseded accepted import lost success or did not preserve current editor binding');
 });
 await run('accepted wallpaper write receipts cannot resurrect deleted or removed media',async()=>{
  const rows=[];for(const kind of ['edit','import'])for(const removed of [false,true]){if(rows.length)await reset();await beginWallpaperWriteFenceProbe(kind);await page.evaluate(removed=>{const p=wallpaperWriteFenceProbe;if(removed)delete snapshot.gameMedia.entries[p.base.key];else snapshot.gameMedia.entries[p.base.key]={...p.native,deleted:true,bindingRevision:p.native.bindingRevision+2};p.finish();},removed);rows.push({kind,removed,...await wallpaperWriteFenceResult()});}
  progressMetrics.push({case:'write-receipt-deleted',rows});check(rows.every(r=>(r.removed?!r.entry:r.entry?.deleted)&&r.ok&&r.requests.length===1&&!r.busy),'late wallpaper write resurrected removed/deleted media or replayed a superseded native write');
 });
 await run('wallpaper edit and import reject foreign or invalid media receipts',async()=>{
  const rows=[];for(const kind of ['edit','import'])for(const field of ['key','appId','bindingRevision']){if(rows.length)await reset();await beginWallpaperWriteFenceProbe(kind);await page.evaluate(field=>wallpaperWriteFenceProbe.finish({[field]:field==='key'?'steam-foreign':field==='appId'?620:'invalid-version'}),field);rows.push({kind,field,...await wallpaperWriteFenceResult()});}
  progressMetrics.push({case:'write-receipt-invalid',rows});check(rows.every(r=>!r.ok&&!r.busy&&r.entry.key===r.base.key&&r.entry.appId===r.base.appId&&r.entry.bindingRevision===r.base.bindingRevision&&r.requests.length===1),'invalid wallpaper receipt was trusted or changed the captured media identity');
  check(rows.every(r=>r.notices.some(x=>x.includes('回执'))),'invalid media receipt failed without an actionable error');
 });
 await run('accepted wallpaper receipt cannot replace a newly rebound editor media key',async()=>{
  await beginWallpaperWriteFenceProbe();await page.evaluate(()=>{const p=wallpaperWriteFenceProbe;selectedActionGame.gameMedia={...p.base,key:'exe-explicit-rebind',appId:0,bindingRevision:8};p.rebound=JSON.stringify(selectedActionGame.gameMedia);p.finish();});const r=await wallpaperWriteFenceResult();const kept=await page.evaluate(()=>JSON.stringify(selectedActionGame.gameMedia)===wallpaperWriteFenceProbe.rebound);progressMetrics.push({case:'write-receipt-editor-rebind',r,kept});
  check(r.ok&&r.entry.bindingRevision===r.native.bindingRevision&&kept&&r.selected.key==='exe-explicit-rebind'&&r.requests.length===1,'late wallpaper receipt replaced a different explicit editor media binding');
 });
 await run('queued playback uses the newer known binding after a late edit receipt',async()=>{
  await beginWallpaperWriteFenceProbe();await page.evaluate(()=>{const p=wallpaperWriteFenceProbe,fresh={...p.native,bindingRevision:p.native.bindingRevision+3,wallpaperMode:'dynamic'};__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[p.base.key]=structuredClone(fresh);const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=structuredClone(fresh);applyLibrarySnapshot(next,{deferRender:true});reverseChoose(5);p.finish();});const r=await wallpaperWriteFenceResult();const native=await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570']);progressMetrics.push({case:'write-receipt-queued-newer',r,native});
  check(r.ok&&r.entry.playback.start===5&&r.entry.bindingRevision===r.native.bindingRevision+4&&native.playback.start===5&&!r.pending&&!r.busy,'late receipt left queued playback on a stale revision');
  check(r.requests.length===1&&r.calls.filter(x=>x.command==='mediaEdit'&&x.args.action==='playback').length===1,'queued playback was duplicated while rebasing the receipt');
 });
 await run('first wallpaper edit can register a new entry before settling its receipt',async()=>{
  const r=await page.evaluate(async()=>{const key='steam-570';delete snapshot.gameMedia.entries[key];selectedActionGame.gameMedia=null;const ok=await updateSharedWallpaper('mode',{mode:'static'});return {ok,entry:snapshot.gameMedia.entries[key],selected:selectedActionGame.gameMedia,busy:sharedWallpaperBusy,creates:reverseIO.calls.filter(x=>x.command==='mediaCreate').length,writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length};});progressMetrics.push({case:'write-first-registration',r});
  check(r.ok&&r.entry.key==='steam-570'&&r.entry.wallpaperMode==='static'&&r.selected.key===r.entry.key&&r.creates===1&&r.writes===1&&!r.busy,'receipt guard prevented a legitimate first media registration');
 });
 await run('accepted wallpaper write cannot cross a changed data root',async()=>{
  await beginWallpaperWriteFenceProbe();await page.evaluate(()=>{const p=wallpaperWriteFenceProbe;const next=structuredClone(snapshot);next.dataRoot='G:/DifferentOwnedDataRoot';next.gameMedia.entries[p.base.key]={...p.base,name:'Other data root owner'};applyLibrarySnapshot(next,{deferRender:true});p.expected=JSON.stringify(snapshot.gameMedia.entries[p.base.key]);p.finish();});const r=await wallpaperWriteFenceResult();const same=await page.evaluate(()=>JSON.stringify(snapshot.gameMedia.entries[wallpaperWriteFenceProbe.base.key])===wallpaperWriteFenceProbe.expected);progressMetrics.push({case:'write-receipt-data-root',r,same});
  check(r.ok&&same&&r.requests.length===1&&!r.busy,'receipt from a previous data root modified the new root snapshot');
 });
 async function beginWallpaperModeReceiptProbe(action='mode'){
  await page.evaluate(action=>{
   const base=invoke;window.wallpaperModeReceiptProbe={action,requests:[],base:structuredClone(snapshot.gameMedia.entries['steam-570']),ownerKey:keyFor(selectedActionGame)};
   invoke=async(command,args={},timeout=0)=>{const p=wallpaperModeReceiptProbe;if(command==='mediaEdit'&&args.action===p.action){const request={args:structuredClone(args)};p.requests.push(request);return new Promise((resolve,reject)=>{request.reject=reject;request.finish=async()=>resolve(await base(command,args,timeout));});}return base(command,args,timeout);};
  },action);
 }
 async function startWallpaperModeButton(){
  await page.evaluate(()=>document.querySelector('[data-shared-wallpaper-mode="static"]').click());
  await page.waitForFunction(()=>wallpaperModeReceiptProbe.requests.length===1&&sharedWallpaperBusy);
 }
 async function wallpaperModeReceiptResult(){
  await page.evaluate(async()=>{await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});
  return page.evaluate(()=>{const p=wallpaperModeReceiptProbe;return {busy:sharedWallpaperBusy,choice:sharedWallpaperPreviewChoice,mode:sharedWallpaperMode(),entry:snapshot.gameMedia.entries[p.base.key],selected:selectedActionGame?.gameMedia,key:keyFor(selectedActionGame),requests:p.requests.map(x=>x.args),base:p.base,pending:sharedWallpaperPendingPlayback.size,editorPending:editorArtworkRenderPending,staticPressed:document.querySelector('[data-shared-wallpaper-mode="static"]').getAttribute('aria-pressed'),dynamicPressed:document.querySelector('[data-shared-wallpaper-mode="dynamic"]').getAttribute('aria-pressed'),video:!!document.querySelector('#artwork-wallpaper-preview video'),notices:[toastNotice.textContent,...noticeQueue.map(x=>x.message)],calls:reverseIO.calls.filter(x=>x.command==='mediaEdit')};});
 }
 await run('failed wallpaper mode button restores the saved mode and preview',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();const pending=await wallpaperModeReceiptResult();await page.evaluate(()=>wallpaperModeReceiptProbe.requests[0].reject(Error('owned mode save denied')));await settle();const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-rejection-preview',pending,r});
  check(pending.mode==='static'&&pending.staticPressed==='true'&&pending.busy,'pending explicit mode lost its immediate preview');
  check(r.mode==='dynamic'&&r.dynamicPressed==='true'&&r.video&&r.choice===null&&r.entry.wallpaperMode==='dynamic'&&r.selected.wallpaperMode==='dynamic','failed mode left an unsaved temporary preview selected');
  check(r.requests.length===1&&!r.busy&&r.notices.some(x=>x.includes('owned mode save denied'))&&r.entry.bindingRevision===r.base.bindingRevision,'mode rejection hid its reason, changed storage or replayed the save');
 });
 await run('successful wallpaper mode button retires only its temporary choice',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(async()=>wallpaperModeReceiptProbe.requests[0].finish());await settle();const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-success-preview-settlement',r});
  check(r.choice===null&&r.mode==='static'&&r.staticPressed==='true'&&r.entry.wallpaperMode==='static'&&r.entry.bindingRevision===r.base.bindingRevision+1,'successful mode retained a temporary choice above the accepted record');
  check(r.requests.length===1&&!r.busy,'mode settlement replayed its native write or retained busy');
 });
 await run('shared wallpaper delete returns its actual native rejection',async()=>{
  await beginWallpaperModeReceiptProbe('delete');await page.evaluate(()=>{window.modeDeleteResult=actOnSharedWallpaper('delete');});await page.waitForFunction(()=>wallpaperModeReceiptProbe.requests.length===1);await page.evaluate(()=>wallpaperModeReceiptProbe.requests[0].reject(Error('owned media delete denied')));const ok=await page.evaluate(()=>modeDeleteResult);const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'wallpaper-delete-rejection-result',ok,r});
  check(!ok&&!r.busy&&r.requests.length===1&&r.entry.bindingRevision===r.base.bindingRevision&&r.notices.some(x=>x.includes('owned media delete denied')),'wallpaper delete reported success after its native rejection');
 });
 await run('failed wallpaper mode cannot redirect a later delete to the wrong slot',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(()=>wallpaperModeReceiptProbe.requests[0].reject(Error('owned mode save denied')));await settle();const result=await page.evaluate(async()=>{const p=wallpaperModeReceiptProbe;p.action='delete';window.modeDeleteResult=actOnSharedWallpaper('delete');return {slot:p.requests.at(-1).args.slot,requests:p.requests.length};});await page.evaluate(()=>wallpaperModeReceiptProbe.requests[1].reject(Error('owned captured delete denied')));const ok=await page.evaluate(()=>modeDeleteResult);const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-rejection-next-delete',result,ok,r});
  check(result.slot==='dynamic'&&result.requests===2&&!ok&&r.entry.assets.dynamic.id===r.base.assets.dynamic.id,'failed mode redirected a later media delete to the unsaved slot');
 });
 await run('wallpaper mode rejection after switching cards leaves the other editor alone',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(async()=>{reverseOpenOther();await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const p=wallpaperModeReceiptProbe;p.otherKey=keyFor(selectedActionGame);p.other=JSON.stringify(selectedActionGame.gameMedia);p.video=document.querySelector('#artwork-wallpaper-preview video');p.focus=document.activeElement;p.requests[0].reject(Error('owned retired mode denied'));});await settle();const r=await wallpaperModeReceiptResult();const other=await page.evaluate(()=>({key:wallpaperModeReceiptProbe.otherKey,unchanged:JSON.stringify(selectedActionGame.gameMedia)===wallpaperModeReceiptProbe.other,video:document.querySelector('#artwork-wallpaper-preview video')===wallpaperModeReceiptProbe.video,focus:document.activeElement===wallpaperModeReceiptProbe.focus}));progressMetrics.push({case:'mode-rejection-other-card',r,other});
  check(r.key===other.key&&other.unchanged&&other.video&&other.focus&&r.choice===null&&r.requests.length===1,'retired mode rejection changed or refocused another card');
 });
 await run('wallpaper mode rejection after reopening cannot redraw the new editor session',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(async()=>{closeEdit();openEdit(mergedGames().find(g=>g.gameMedia?.key==='steam-570'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const p=wallpaperModeReceiptProbe;p.video=document.querySelector('#artwork-wallpaper-preview video');const original=renderArtworkDraft;p.renders=0;renderArtworkDraft=(...args)=>{p.renders++;return original(...args);};p.requests[0].reject(Error('owned old-session mode denied'));});await settle();const r=await wallpaperModeReceiptResult();const after=await page.evaluate(()=>({renders:wallpaperModeReceiptProbe.renders,video:document.querySelector('#artwork-wallpaper-preview video')===wallpaperModeReceiptProbe.video}));progressMetrics.push({case:'mode-rejection-reopened',r,after});
  check(after.renders===0&&after.video&&r.choice===null&&r.mode==='dynamic'&&r.requests.length===1,'late failure took over the reopened editor presentation');
 });
 await run('background mode rejection defers preview rollback until normal focus',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const p=wallpaperModeReceiptProbe;p.node=document.querySelector('#artwork-wallpaper-preview').firstChild;p.requests[0].reject(Error('owned background mode denied'));});await settle();const hidden=await wallpaperModeReceiptResult();const same=await page.evaluate(()=>document.querySelector('#artwork-wallpaper-preview').firstChild===wallpaperModeReceiptProbe.node);await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});const visible=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-rejection-background',hidden,same,visible});
  check(hidden.mode==='dynamic'&&hidden.choice===null&&hidden.editorPending&&same&&visible.dynamicPressed==='true'&&visible.video&&!visible.editorPending&&visible.requests.length===1,'background rejection repainted or failed to restore the saved mode on normal focus');
 });
 await run('busy mode buttons do not replace the accepted mode intent',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(()=>document.querySelector('[data-shared-wallpaper-mode="dynamic"]').click());const pending=await wallpaperModeReceiptResult();await page.evaluate(async()=>wallpaperModeReceiptProbe.requests[0].finish());await settle();const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-busy-intent',pending,r});
  check(pending.requests.length===1&&pending.mode==='static'&&pending.busy&&r.requests.length===1&&r.choice===null&&r.entry.wallpaperMode==='static','busy mode button replaced the pending intent or issued another save');
 });
 await run('shared wallpaper delete reports busy without releasing another operation',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();const blocked=await page.evaluate(async()=>({ok:await actOnSharedWallpaper('delete'),busy:sharedWallpaperBusy,requests:wallpaperModeReceiptProbe.requests.length}));await page.evaluate(async()=>wallpaperModeReceiptProbe.requests[0].finish());await settle();const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-busy-delete',blocked,r});
  check(!blocked.ok&&blocked.busy&&blocked.requests===1&&r.requests.length===1&&r.entry.wallpaperMode==='static','blocked delete reported success, released another operation or dispatched a write');
 });
 await run('mode rejection still drains its captured pending playback once',async()=>{
  await beginWallpaperModeReceiptProbe();await startWallpaperModeButton();await page.evaluate(()=>{reverseChoose(5);wallpaperModeReceiptProbe.requests[0].reject(Error('owned queued mode denied'));});await settle();const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'mode-rejection-playback-drain',r});
  check(r.mode==='dynamic'&&r.choice===null&&r.entry.playback.start===5&&r.entry.bindingRevision===r.base.bindingRevision+1&&r.requests.length===1&&r.calls.filter(x=>x.args.action==='playback').length===1&&!r.pending,'mode rejection stranded or duplicated the captured playback queue');
 });
 await run('accepted shared wallpaper deletion returns success for the captured slot once',async()=>{
  await beginWallpaperModeReceiptProbe('delete');await page.evaluate(()=>{window.modeDeleteResult=actOnSharedWallpaper('delete');});await page.waitForFunction(()=>wallpaperModeReceiptProbe.requests.length===1);await page.evaluate(async()=>wallpaperModeReceiptProbe.requests[0].finish());const ok=await page.evaluate(()=>modeDeleteResult);const r=await wallpaperModeReceiptResult();progressMetrics.push({case:'wallpaper-delete-accepted-control',ok,r});
  check(ok&&!r.busy&&r.requests.length===1&&r.requests[0].slot==='dynamic'&&r.entry.bindingRevision===r.base.bindingRevision+1&&!r.entry.assets.dynamic,'accepted deletion lost its true result, slot ownership or single native execution');
  check(r.entry.assets.static.id===r.base.assets.static.id&&r.calls.filter(x=>x.args.action==='delete').length===1&&r.choice===null,'deleting the dynamic slot changed the static asset or left a temporary mode');
 });
 async function beginWallpaperPresentationProbe(){
  await page.evaluate(()=>{
   const p=window.wallpaperPresentationProbe={editor:0,library:0,preview:0,progress:0,editorThrows:0,libraryThrows:0,previewThrows:0,progressThrows:0,progressWhenIdle:false,armProgressAtSearch:false,diagnostics:[],unhandled:[]};
   const artwork=renderArtworkDraft,library=renderLibrary,preview=renderSharedWallpaperPreview,post=window.chrome.webview.postMessage,base=invoke;
   renderArtworkDraft=(...args)=>{if(p.editor){p.editor--;p.editorThrows++;throw Error('owned wallpaper editor drawing failed');}return artwork(...args);};
   renderLibrary=(...args)=>{if(p.library){p.library--;p.libraryThrows++;throw Error('owned wallpaper library drawing failed');}return library(...args);};
   renderSharedWallpaperPreview=(...args)=>{if(p.preview){p.preview--;p.previewThrows++;throw Error('owned wallpaper preview drawing failed');}return preview(...args);};
   const classes=document.getElementById('shared-wallpaper-download-status').classList,toggle=classes.toggle.bind(classes);
   classes.toggle=(...args)=>{if(p.progress&&(!p.progressWhenIdle||!sharedWallpaperBusy)){p.progress--;p.progressThrows++;throw Error('owned wallpaper progress drawing failed');}return toggle(...args);};
   window.chrome.webview.postMessage=function(message){if(message.command==='uiError'){p.diagnostics.push(structuredClone(message.arguments));return;}return post.call(this,message);};
   const injectedFault=error=>String(error?.message||error).startsWith('owned wallpaper ')&&String(error?.message||error).includes(' drawing failed');window.addEventListener('unhandledrejection',event=>{if(injectedFault(event.reason)){p.unhandled.push(String(event.reason));event.preventDefault();}});window.addEventListener('error',event=>{if(injectedFault(event.error||event.message)){p.unhandled.push(String(event.error||event.message));event.preventDefault();}});
   invoke=async(command,args={},timeout=0)=>{const result=await base(command,args,timeout);if(command==='mediaSearchVideos'&&p.armProgressAtSearch){p.armProgressAtSearch=false;p.progress=1;}return result;};
  });
 }
 async function wallpaperPresentationResult(){
  await page.evaluate(async()=>{await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});
  return page.evaluate(()=>{const p=wallpaperPresentationProbe;return {busy:sharedWallpaperBusy,pendingPlayback:sharedWallpaperPendingPlayback.size,editorPending:editorArtworkRenderPending,libraryPending:silentRenderPending,downloadPending:mediaDownloadPaintPending,editorThrows:p.editorThrows,libraryThrows:p.libraryThrows,previewThrows:p.previewThrows,progressThrows:p.progressThrows,diagnostics:p.diagnostics,unhandled:p.unhandled,calls:reverseIO.calls.filter(x=>['mediaEdit','mediaImport','mediaSelectVideo','mediaCreate'].includes(x.command)),entry:snapshot.gameMedia.entries['steam-570'],selected:selectedActionGame?.gameMedia,galleryHidden:document.getElementById('artwork-search-modal').classList.contains('hidden'),summary:document.getElementById('artwork-search-summary').textContent,notices:[toastNotice.textContent,...noticeQueue.map(x=>x.message)]};});
 }
 await run('video progress drawing failure cannot strand busy before dispatch',async()=>{
  await beginWallpaperPresentationProbe();await page.evaluate(()=>wallpaperPresentationProbe.armProgressAtSearch=true);await beginManualVideoReceiptProbe(false);
  const start=await page.evaluate(()=>({selects:manualVideoReceiptProbe.selects.length,busy:sharedWallpaperBusy,throws:wallpaperPresentationProbe.progressThrows}));
  check(start.selects===1&&start.busy&&start.throws===1,'progress drawing stranded busy before the video selection dispatch');
  await page.evaluate(async()=>manualVideoReceiptProbe.finish());await settle();const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-progress-before-dispatch',start,r});
  check(!r.busy&&r.unhandled.length===0&&r.entry.selectedVideo?.id==='0'&&r.diagnostics.some(x=>x.message==='owned wallpaper progress drawing failed'),'progress presentation failure lost the accepted video, release or diagnostic');
 });
 await run('wallpaper mode button drawing failure cannot prevent its native save',async()=>{
  await beginWallpaperPresentationProbe();await page.evaluate(()=>{wallpaperPresentationProbe.editor=1;document.querySelector('[data-shared-wallpaper-mode="static"]').click();});await settle();const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-mode-before-submit',r});
  check(r.calls.filter(x=>x.command==='mediaEdit'&&x.args.action==='mode').length===1&&r.entry.wallpaperMode==='static'&&!r.busy,'mode button drawing prevented its explicit native save');
  check(r.editorThrows===1&&r.diagnostics.some(x=>x.message==='owned wallpaper editor drawing failed')&&!r.unhandled.length,'mode button display fault lost its diagnostic or escaped the handler');
 });
 await run('wallpaper mode save stays successful when editor drawing fails',async()=>{
  await beginWallpaperPresentationProbe();const ok=await page.evaluate(async()=>{wallpaperPresentationProbe.editor=1;return updateSharedWallpaper('mode',{mode:'static'});});const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-mode-editor-result',ok,r});
  check(ok&&r.entry.wallpaperMode==='static'&&!r.busy&&r.calls.filter(x=>x.command==='mediaEdit').length===1,'editor drawing relabeled an accepted wallpaper mode save');
  check(r.editorThrows===1&&r.diagnostics.some(x=>x.message==='owned wallpaper editor drawing failed'),'accepted mode lost its editor display diagnostic');
 });
 await run('wallpaper mode save stays successful when library drawing fails',async()=>{
  await beginWallpaperPresentationProbe();const ok=await page.evaluate(async()=>{wallpaperPresentationProbe.library=1;return updateSharedWallpaper('mode',{mode:'static'});});const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-mode-library-result',ok,r});
  check(ok&&r.entry.wallpaperMode==='static'&&!r.busy&&r.calls.filter(x=>x.command==='mediaEdit').length===1,'library drawing relabeled an accepted wallpaper mode save');
  check(r.libraryThrows===1&&r.diagnostics.some(x=>x.message==='owned wallpaper library drawing failed'),'accepted mode lost its library display diagnostic');
 });
 for(const target of ['editor','library'])await run('wallpaper import stays successful when '+target+' drawing fails',async()=>{
  await beginWallpaperPresentationProbe();const ok=await page.evaluate(async target=>{wallpaperPresentationProbe[target]=1;const waiting=actOnSharedWallpaper('replace');reverseIO.finishPick({path:'G:/H5Fixture/presentation-import.mp4'});return waiting;},target);const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-import-display-'+target,ok,r});
  check(ok&&!r.busy&&r.entry.assets.dynamic.id==='imported-2'&&r.calls.filter(x=>x.command==='mediaImport').length===1,'drawing relabeled a successful wallpaper import or repeated its native publication');
  check(r.diagnostics.some(x=>x.message==='owned wallpaper '+target+' drawing failed')&&!r.unhandled.length,'accepted import lost its display diagnostic or leaked a rejection');
 });
 for(const target of ['editor','library'])await run('video selection survives its accepted '+target+' drawing failure',async()=>{
  await beginWallpaperPresentationProbe();await beginManualVideoReceiptProbe();await page.evaluate(async target=>{wallpaperPresentationProbe[target]=1;await manualVideoReceiptProbe.finish();},target);await settle();const r=await wallpaperPresentationResult();const reads=await page.evaluate(()=>manualVideoReceiptProbe.states);progressMetrics.push({case:'video-accepted-display-'+target,r,reads});
  check(r.galleryHidden&&reads===1&&r.entry.selectedVideo?.id==='0'&&!r.busy&&r.calls.filter(x=>x.command==='mediaSelectVideo').length===1,'drawing interrupted an accepted video selection or skipped its normal state refresh');
  check(r.diagnostics.some(x=>x.message==='owned wallpaper '+target+' drawing failed')&&!r.unhandled.length,'accepted video lost its display diagnostic or leaked an unhandled rejection');
 });
 await run('video rejection keeps its reason when error progress drawing fails',async()=>{
  await beginWallpaperPresentationProbe();await beginManualVideoReceiptProbe();await page.evaluate(()=>{wallpaperPresentationProbe.progress=1;manualVideoReceiptProbe.reject(Error('owned native video selection denied'));});await page.waitForFunction(()=>!sharedWallpaperBusy);const r=await wallpaperPresentationResult();const request=await page.evaluate(()=>sharedWallpaperDownloadRequests.get(manualVideoReceiptProbe.owner.key));progressMetrics.push({case:'video-rejection-progress-display',r,request});
  check(request?.phase==='error'&&request.error.includes('owned native video selection denied')&&r.summary.includes('owned native video selection denied')&&!r.unhandled.length,'progress drawing concealed the true video rejection or leaked its failure');
  check(r.progressThrows===1&&r.diagnostics.some(x=>x.message==='owned wallpaper progress drawing failed')&&r.calls.filter(x=>x.command==='mediaSelectVideo').length===0,'rejected selection display fault lost diagnostics or replayed the native request');
 });
 await run('video final progress drawing failure cannot strand queued playback',async()=>{
  await beginWallpaperPresentationProbe();await beginManualVideoReceiptProbe();await page.evaluate(async()=>{reverseChoose(3);wallpaperPresentationProbe.progress=1;wallpaperPresentationProbe.progressWhenIdle=true;await manualVideoReceiptProbe.finish();});await page.waitForFunction(()=>!sharedWallpaperBusy);const initial=await wallpaperPresentationResult();
  check(initial.pendingPlayback===0&&!initial.unhandled.length,'final progress drawing stranded the existing playback queue');
  await settle();const r=await wallpaperPresentationResult();progressMetrics.push({case:'video-final-progress-queue',initial,r});check(r.entry.playback.start===3&&r.calls.filter(x=>x.command==='mediaSelectVideo').length===1&&r.calls.filter(x=>x.command==='mediaEdit'&&x.args.action==='playback').length===1&&r.diagnostics.some(x=>x.message==='owned wallpaper progress drawing failed'),'final display fault lost queued playback, duplicated writes or hid its diagnostic');
 });
 await run('wallpaper preview drawing failure preserves accepted preparation and diagnostics',async()=>{
  await beginWallpaperPresentationProbe();await page.evaluate(async()=>{wallpaperPresentationProbe.preview=1;await prepareSharedWallpaperPreview(selectedActionGame);});const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-prepare-display',r});
  check(r.selected.key==='steam-570'&&r.previewThrows===1&&r.diagnostics.some(x=>x.message==='owned wallpaper preview drawing failed')&&!r.notices.some(x=>x.includes('owned wallpaper preview drawing failed')),'accepted media preparation was mislabeled as a business error by preview drawing');
 });
 await run('wallpaper genuine edit rejection remains false without write replay',async()=>{
  await beginWallpaperPresentationProbe();const ok=await page.evaluate(async()=>{const base=invoke;invoke=async(command,args={},timeout=0)=>{if(command==='mediaEdit'){reverseIO.calls.push({command,args:structuredClone(args)});throw Error('owned media edit denied');}return base(command,args,timeout);};return updateSharedWallpaper('mode',{mode:'static'});});const r=await wallpaperPresentationResult();progressMetrics.push({case:'wallpaper-edit-actual-rejection',ok,r});
  check(!ok&&!r.busy&&r.entry.wallpaperMode==='dynamic'&&r.calls.filter(x=>x.command==='mediaEdit').length===1&&r.notices.some(x=>x.includes('owned media edit denied')),'presentation isolation hid a real edit failure or replayed it');
 });
 await run('wallpaper drawing demand recovers on original focus without native replay',async()=>{
  await beginWallpaperPresentationProbe();const pre=await page.evaluate(async()=>{wallpaperPresentationProbe.editor=1;wallpaperPresentationProbe.library=1;const ok=await updateSharedWallpaper('mode',{mode:'static'});return {ok,editor:editorArtworkRenderPending,library:silentRenderPending};});
  check(pre.ok&&pre.editor&&pre.library,'wallpaper drawing failures lost the existing editor or library recovery demand');
  await page.evaluate(()=>window.dispatchEvent(new Event('focus')));const r=await wallpaperPresentationResult();const visible=await page.evaluate(()=>({editor:editorArtworkRenderPending,library:silentRenderPending,pressed:document.querySelector('[data-shared-wallpaper-mode="static"]').getAttribute('aria-pressed')}));
  progressMetrics.push({case:'wallpaper-normal-focus-recovery',pre,r,visible});check(!visible.editor&&!visible.library&&visible.pressed==='true'&&r.entry.wallpaperMode==='static'&&r.calls.filter(x=>x.command==='mediaEdit').length===1&&!r.unhandled.length,'normal focus did not recover the accepted wallpaper presentation or replayed its native save');
 });
 async function beginVideoStateFenceProbe(){
  await beginManualVideoReceiptProbe();
  await page.evaluate(async()=>{
   const actual=invoke,p=manualVideoReceiptProbe;window.videoStateFenceProbe={reads:[],libraryRenders:0};
   invoke=async(command,args={},timeout=0)=>{if(command==='mediaState')return new Promise((resolve,reject)=>videoStateFenceProbe.reads.push({args:structuredClone(args),resolve,reject}));return actual(command,args,timeout);};
   await p.finish();
  });
  await page.waitForFunction(()=>videoStateFenceProbe.reads.length===1);
  await page.evaluate(()=>{
   const p=manualVideoReceiptProbe,f=videoStateFenceProbe;f.accepted=structuredClone(snapshot.gameMedia.entries[p.base.key]);f.progressBefore=snapshot.mediaDownloads;
   f.reply=(entry=f.accepted,downloads)=>{const state={gameMedia:{entries:entry?{[p.base.key]:structuredClone(entry)}:{}}};if(downloads)state.mediaDownloads=downloads;f.reads[0].resolve(state);};
   const render=renderLibrary;renderLibrary=(...args)=>{f.libraryRenders++;return render(...args);};
  });
 }
 await run('post-selection state cannot roll back a newer snapshot binding',async()=>{
  await beginVideoStateFenceProbe();
  await page.evaluate(()=>{const p=manualVideoReceiptProbe,f=videoStateFenceProbe;f.newer={...f.accepted,bindingRevision:f.accepted.bindingRevision+4,wallpaperMode:'static',selectedVideo:{id:'newer-independent-binding'}};const next=structuredClone(snapshot);next.gameMedia.entries[p.base.key]=structuredClone(f.newer);applyLibrarySnapshot(next,{deferRender:true});f.reply(f.accepted);});
  const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-state-newer-snapshot',r});
  check(r.entry.bindingRevision===r.native.bindingRevision+4&&r.entry.selectedVideo.id==='newer-independent-binding'&&r.entry.wallpaperMode==='static','post-selection state rolled back a newer snapshot binding');
  check(r.phase==='queued'&&r.selects===1,'discarding stale video state changed the accepted request or replayed its write');
 });
 await run('post-selection state rejects a foreign AppID under the same key',async()=>{
  await beginVideoStateFenceProbe();await page.evaluate(()=>{const f=videoStateFenceProbe;f.reply({...f.accepted,appId:620,bindingRevision:f.accepted.bindingRevision+2});});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-state-wrong-app',r});
  check(r.entry.appId===r.base.appId&&r.entry.bindingRevision===r.native.bindingRevision&&r.selectedMedia.appId===r.base.appId,'post-selection state accepted a foreign AppID under the owned key');
  check(r.phase==='queued'&&r.selects===1,'invalid read result changed a successful selection to failure or replayed it');
 });
 await run('post-selection state cannot resurrect deleted or removed media',async()=>{
  const rows=[];for(const removed of [false,true]){if(removed)await reset();await beginVideoStateFenceProbe();await page.evaluate(removed=>{const p=manualVideoReceiptProbe,f=videoStateFenceProbe;if(removed)delete snapshot.gameMedia.entries[p.base.key];else snapshot.gameMedia.entries[p.base.key]={...f.accepted,deleted:true,bindingRevision:f.accepted.bindingRevision+3};f.reply(f.accepted);},removed);rows.push({removed,...await manualVideoReceiptResult()});}
  progressMetrics.push({case:'video-state-deletion',rows});check(rows.every(r=>r.removed?!r.entry:r.entry?.deleted&&r.entry.bindingRevision===r.native.bindingRevision+3),'post-selection state resurrected deleted or removed media');
 });
 await run('post-selection state missing entry retains current media and accepts valid progress',async()=>{
  await beginVideoStateFenceProbe();await page.evaluate(()=>{const p=manualVideoReceiptProbe,f=videoStateFenceProbe;f.newer={...f.accepted,bindingRevision:f.accepted.bindingRevision+2,wallpaperMode:'static'};snapshot.gameMedia.entries[p.base.key]=f.newer;f.downloads={...snapshot.mediaDownloads,statusRevision:Number(snapshot.mediaDownloads?.statusRevision||0)+100,phase:'downloading',current:{key:p.base.key,name:'Owned valid progress'},bytes:200};f.reply(null,f.downloads);});const r=await manualVideoReceiptResult();const progress=await page.evaluate(()=>({current:snapshot.mediaDownloads.current?.name,revision:snapshot.mediaDownloads.statusRevision}));
  progressMetrics.push({case:'video-state-missing-entry',r,progress});check(r.entry.bindingRevision===r.native.bindingRevision+2&&r.entry.wallpaperMode==='static'&&progress.current==='Owned valid progress','entry-only state fallback restored the first receipt or discarded independent valid download progress');
 });
 await run('post-selection state accepts a matching newer binding once',async()=>{
  await beginVideoStateFenceProbe();await page.evaluate(()=>{const f=videoStateFenceProbe;f.reply({...f.accepted,bindingRevision:f.accepted.bindingRevision+2,selectedVideo:{id:'matching-fresh-video'},dynamicPreviewUrl:'/sample.mp4?matching-state'});});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-state-matching',r});
  check(r.entry.bindingRevision===r.native.bindingRevision+2&&r.selectedMedia.bindingRevision===r.entry.bindingRevision&&r.entry.selectedVideo.id==='matching-fresh-video'&&r.selects===1&&r.phase==='queued','matching current post-selection state was dropped or replayed its write');
 });
 await run('post-selection state cannot replace progress received after its request',async()=>{
  await beginVideoStateFenceProbe();await page.evaluate(()=>{const p=manualVideoReceiptProbe,f=videoStateFenceProbe;const fresh={...snapshot.mediaDownloads,statusRevision:Number(snapshot.mediaDownloads?.statusRevision||0)+100,phase:'downloading',current:{key:p.base.key,name:'Newer live progress'},bytes:500};renderMediaDownloads(fresh,{currentSnapshot:true});f.reply(f.accepted,{...fresh,statusRevision:fresh.statusRevision-1,current:{key:p.base.key,name:'Stale state progress'},bytes:1});});const r=await manualVideoReceiptResult();const p=await page.evaluate(()=>({current:snapshot.mediaDownloads.current?.name,bytes:snapshot.mediaDownloads.bytes}));progressMetrics.push({case:'video-state-progress-fence',r,p});
  check(p.current==='Newer live progress'&&p.bytes===500&&r.selects===1&&r.entry.bindingRevision===r.native.bindingRevision,'late entry state replaced progress observed after its request');
 });
 await run('post-selection state after editor close releases busy without repaint or replay',async()=>{
  await beginVideoStateFenceProbe();await page.evaluate(()=>{closeEdit();videoStateFenceProbe.rendersAtClose=videoStateFenceProbe.libraryRenders;const f=videoStateFenceProbe;f.reply({...f.accepted,bindingRevision:f.accepted.bindingRevision+2});});const r=await manualVideoReceiptResult();const paints=await page.evaluate(()=>({after:videoStateFenceProbe.libraryRenders,before:videoStateFenceProbe.rendersAtClose}));progressMetrics.push({case:'video-state-closed',r,paints});
  check(!r.busy&&r.editorHidden&&r.galleryHidden&&r.phase==='queued'&&r.selects===1&&r.entry.bindingRevision===r.native.bindingRevision&&paints.after===paints.before,'retired editor read repainted/reopened, retained busy or replayed a selection');
 });
 await run('manual video selection accepts its receipt after gallery Back',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{cancelArtworkTask();await manualVideoReceiptProbe.finish();});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-selection-gallery-back',r});
  check(r.entry.bindingRevision===r.native.bindingRevision&&r.entry.selectedVideo?.id===r.native.selectedVideo.id&&r.selectedMedia.bindingRevision===r.native.bindingRevision,'accepted video receipt was lost after gallery Back');
  check(r.galleryHidden&&!r.editorHidden&&r.phase==='queued'&&r.selects===1,'returning from video candidates reopened the gallery or lost/replayed the queued request');
 });
 await run('manual video selection accepts its receipt after editor close',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{closeEdit();await manualVideoReceiptProbe.finish();});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-selection-editor-close',r});
  check(r.entry.bindingRevision===r.native.bindingRevision&&r.entry.selectedVideo?.id===r.native.selectedVideo.id,'accepted video receipt was lost after editor close');
  check(r.editorHidden&&r.galleryHidden&&r.phase==='queued'&&r.selects===1,'background video completion reopened the editor or lost/replayed its queued feedback');
 });
 await run('manual video selection settles the original card without closing another gallery',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{reverseOpenOther();await searchSharedWallpaperVideos(captureSharedWallpaperWrite());const p=manualVideoReceiptProbe;p.otherKey=keyFor(selectedActionGame);p.otherBefore=JSON.stringify(selectedActionGame.gameMedia);p.otherGallery=document.getElementById('artwork-search-list').textContent;p.focusBeforeInitialFrame=document.activeElement;await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);p.initialFocusMoved=document.activeElement!==p.focusBeforeInitialFrame;p.focus=document.activeElement;await p.finish();});const r=await manualVideoReceiptResult();const other=await page.evaluate(()=>({key:manualVideoReceiptProbe.otherKey,unchanged:JSON.stringify(selectedActionGame.gameMedia)===manualVideoReceiptProbe.otherBefore,gallery:document.getElementById('artwork-search-list').textContent===manualVideoReceiptProbe.otherGallery,initialFocusMoved:manualVideoReceiptProbe.initialFocusMoved,focus:document.activeElement===manualVideoReceiptProbe.focus}));progressMetrics.push({case:'video-selection-other-card',r,other});
  check(r.entry.bindingRevision===r.native.bindingRevision&&r.phase==='queued'&&r.selects===1,'retired video receipt was not settled on its original card');
  check(r.selectedKey===other.key&&other.unchanged&&other.gallery&&other.focus&&!r.galleryHidden,'old video completion changed another card or closed/refocused its gallery');
 });
 await run('manual video selection cannot roll back a newer binding after Back',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{cancelArtworkTask();const p=manualVideoReceiptProbe;p.beforeReply=()=>{const fresh={...p.base,bindingRevision:p.base.bindingRevision+5,wallpaperMode:'static',selectedVideo:{id:'newer-explicit-video'}};snapshot.gameMedia.entries[p.base.key]=fresh;selectedActionGame.gameMedia=fresh;p.expected=JSON.stringify(fresh);};await p.finish();});const r=await manualVideoReceiptResult();const same=await page.evaluate(()=>JSON.stringify(snapshot.gameMedia.entries[manualVideoReceiptProbe.base.key])===manualVideoReceiptProbe.expected);
  check(same&&r.selectedMedia.selectedVideo.id==='newer-explicit-video'&&r.phase!=='queued'&&r.selects===1,'late video selection overwrote a newer explicit binding');
 });
 await run('manual video selection cannot resurrect deleted or removed media after Back',async()=>{
  const rows=[];for(const removed of [false,true]){if(removed)await reset();await beginManualVideoReceiptProbe();await page.evaluate(async removed=>{cancelArtworkTask();const p=manualVideoReceiptProbe;p.beforeReply=()=>{if(removed)delete snapshot.gameMedia.entries[p.base.key];else snapshot.gameMedia.entries[p.base.key]={...p.base,deleted:true,bindingRevision:p.base.bindingRevision+3};};closeEdit();await p.finish();},removed);const r=await manualVideoReceiptResult();rows.push({removed,...r});}
  progressMetrics.push({case:'video-selection-deleted',rows});check(rows.every(r=>(r.removed?!r.entry:r.entry?.deleted)&&r.phase!=='queued'&&r.selects===1),'late video selection resurrected removed/deleted media or published stale feedback');
 });
 await run('manual video selection rejects a wrong AppID receipt after Back',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>{cancelArtworkTask();await manualVideoReceiptProbe.finish({appId:620});});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-selection-wrong-identity',r});
  check(r.entry.appId===r.base.appId&&r.entry.bindingRevision===r.base.bindingRevision&&r.phase==='error'&&r.selects===1,'wrong AppID video receipt was accepted or silently lost instead of retaining an error');
 });
 await run('manual video selection retains a real rejection after Back without replay',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(()=>{cancelArtworkTask();manualVideoReceiptProbe.reject(Error('owned video selection rejected'));});const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-selection-rejected-back',r});
  check(r.entry.bindingRevision===r.base.bindingRevision&&r.phase==='error'&&r.request.error.includes('owned video selection rejected')&&r.selects===1,'returning hid a real video rejection or replayed its business write');
 });
 await run('manual video selection keeps the normal foreground queue path',async()=>{
  await beginManualVideoReceiptProbe();await page.evaluate(async()=>manualVideoReceiptProbe.finish());const r=await manualVideoReceiptResult();progressMetrics.push({case:'video-selection-foreground',r});
  check(r.entry.bindingRevision===r.native.bindingRevision&&r.selectedMedia.bindingRevision===r.native.bindingRevision&&r.phase==='queued'&&r.galleryHidden&&!r.editorHidden&&r.selects===1&&r.states===1,'foreground video selection lost its accepted binding, normal state refresh or single native dispatch');
 });
 await run('Steam plan directory ownership survives a shared launcher in either order',async()=>{
  await beginMediaCardConflictProbe();
  const rows=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/Launcher/game.exe',rows=[];for(const reverse of [false,true]){const a=p.game('G:/Owned/A-card',570,exe),b=p.game('G:/Owned/B-card',620,exe);p.install([a,b],[p.entry(570,exe),p.entry(620,exe)]);if(reverse)snapshot.steamPlan.items.reverse();const before=JSON.stringify(snapshot),games=mergedGames();renderLibrary();rows.push({reverse,a:keyFor(a),b:keyFor(b),games:games.map(p.row),sourceUnchanged:JSON.stringify(snapshot)===before,writes:p.writes(),cards:libraryCards().length});}return rows;});saveMetrics.push({case:'steam-plan-shared-launcher-directory',rows});
  check(rows.every(r=>r.games.find(g=>g.key===r.a)?.appId==='570'&&r.games.find(g=>g.key===r.b)?.appId==='620'),'shared launcher assigned another directory Steam identity');
  check(rows.every(r=>r.games.find(g=>g.key===r.a)?.media==='steam-570'&&r.games.find(g=>g.key===r.b)?.media==='steam-620'&&r.cards===2),'shared launcher plan mismatch displaced the owned wallpaper');
  check(rows.every(r=>r.sourceUnchanged&&r.writes.length===0),'directory plan reconciliation rewrote source or dispatched a business operation');
 });
 await run('Steam plan cannot borrow a foreign directory through a shared EXE',async()=>{
  await beginMediaCardConflictProbe();
  const result=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/Launcher/game.exe',g=p.game('G:/Owned/No-plan',570,exe);p.install([g],[]);snapshot.steamPlan.items[0].gameDirectory='G:/Owned/Foreign';const before=JSON.stringify(snapshot),game=mergedGames()[0];activeTab=category(game);renderLibrary();return {row:p.row(game),ready:isSteamReady(game),formal:formalSteamAppId(game),target:sharedWallpaperTarget(game),sourceUnchanged:JSON.stringify(snapshot)===before,writes:p.writes(),cards:libraryCards().length};});saveMetrics.push({case:'steam-plan-foreign-directory-rejected',result});
  check(!result.row.appId&&!result.ready&&!result.formal&&!result.target.appId&&!result.target.verified,'foreign directory plan lent Steam identity through the same EXE');
  check(result.cards===1&&result.sourceUnchanged&&result.writes.length===0,'rejecting a foreign plan hid the scanned card or performed a write');
 });
 await run('Steam plan directory aliases and missing-EXE legacy rows keep their own identities',async()=>{
  await beginMediaCardConflictProbe();
  const rows=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/Launcher/game.exe',slash=String.fromCharCode(92),rows=[];for(const missingExe of [false,true]){const g=p.game('G:/Owned/A-alias',570,exe),foreign=p.game('G:/Owned/Z-foreign',620,exe);p.install([g,foreign],[]);const own=snapshot.steamPlan.items[0];own.gameDirectory=own.gameDirectory.toUpperCase().replaceAll('/',slash)+slash;if(missingExe)delete own.primaryExecutable;else own.primaryExecutable=exe.toUpperCase().replaceAll('/',slash)+slash;const before=JSON.stringify(snapshot);rows.push({missingExe,key:keyFor(g),games:mergedGames().map(p.row),sourceUnchanged:JSON.stringify(snapshot)===before,writes:p.writes()});}return rows;});saveMetrics.push({case:'steam-plan-alias-and-no-exe',rows});
  check(rows.every(r=>r.games.find(g=>g.key===r.key)?.appId==='570'),'normalized own directory or legacy missing-EXE plan lost its identity');
  check(rows.every(r=>r.sourceUnchanged&&r.writes.length===0),'legacy directory display fallback altered persisted identity');
 });
 await run('Steam plan primary changes reject stale rows while EXE-only legacy fallback remains',async()=>{
  await beginMediaCardConflictProbe();
  const result=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/New/game.exe',g=p.game('G:/Owned/A-changed',570,exe);p.install([g],[]);snapshot.steamPlan.items[0].primaryExecutable='G:/Owned/Old/game.exe';const stale=mergedGames().map(p.row);const rows=[];for(const reverse of [false,true]){p.install([g],[]);const legacy={...snapshot.steamPlan.items[0],appId:620,steamStoreAppId:620};delete legacy.gameDirectory;const foreign={...snapshot.steamPlan.items[0],gameDirectory:'G:/Owned/Foreign'};snapshot.steamPlan.items=reverse?[foreign,legacy]:[legacy,foreign];rows.push({reverse,games:mergedGames().map(p.row)});}return {stale,rows,writes:p.writes()};});saveMetrics.push({case:'steam-plan-primary-and-legacy-exe',result});
  check(result.stale.length===1&&!result.stale[0].appId,'a primary EXE change retained the stale directory identity');
  check(result.rows.every(r=>r.games.length===1&&r.games[0].appId==='620')&&result.writes.length===0,'a foreign directory masked a valid EXE-only legacy plan');
 });
 await run('Steam plan matching directory takes precedence over an EXE-only legacy row',async()=>{
  await beginMediaCardConflictProbe();
  const rows=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/Launcher/game.exe',rows=[];for(const reverse of [false,true]){const g=p.game('G:/Owned/A-current',570,exe);p.install([g],[]);const legacy={...snapshot.steamPlan.items[0],appId:620,steamStoreAppId:620};delete legacy.gameDirectory;snapshot.steamPlan.items.push(legacy);if(reverse)snapshot.steamPlan.items.reverse();rows.push({reverse,games:mergedGames().map(p.row),writes:p.writes()});}return rows;});saveMetrics.push({case:'steam-plan-own-before-legacy',rows});
  check(rows.every(r=>r.games.length===1&&r.games[0].appId==='570'&&r.writes.length===0),'EXE-only legacy row replaced the current matching directory plan');
 });
 await run('media merge rejects a conflicting executable before the matching confirmed AppID',async()=>{
  await beginMediaCardConflictProbe();
  const row=await page.evaluate(()=>{const p=mediaCardConflictProbe,executable='G:/Owned/Shared/game.exe',a=p.game('G:/Owned/A-first',620,executable),b=p.game('G:/Owned/B-owner',570,'G:/Owned/Other/game.exe');p.install([a,b],[p.entry(570,executable)]);const before=JSON.stringify(snapshot);const rows=mergedGames();renderLibrary();return {rows:rows.map(p.row),sourceUnchanged:JSON.stringify(snapshot)===before,writes:p.writes(),a:keyFor(a),b:keyFor(b),cards:libraryCards().length};});saveMetrics.push({case:'media-conflicting-exe-vs-app',row});
  check(row.rows.find(g=>g.key===row.a)?.media===''&&row.rows.find(g=>g.key===row.b)?.media==='steam-570'&&!row.rows.some(g=>g.mediaOnly),'old executable binding attached media to a contradictory confirmed Steam card');
  check(row.cards===2&&row.sourceUnchanged&&row.writes.length===0,'read-only media conflict reconciliation altered records, wrote intent or duplicated a valid owner');
 });
 await run('stale EXE media stays cached without replacing current native or verified card media',async()=>{
  await beginMediaCardConflictProbe();
  const rows=await page.evaluate(()=>{const p=mediaCardConflictProbe,rows=[];for(const native of [false,true]){const executable='G:/Owned/Reused/game.exe',a=p.game('G:/Owned/Reused',620,executable,true,native);p.install([a],[p.entry(620,executable),p.entry(570,executable)]);const before=JSON.stringify(snapshot),games=mergedGames();rows.push({native,key:keyFor(a),games:games.map(p.row),sourceUnchanged:JSON.stringify(snapshot)===before,writes:p.writes()});}return rows;});saveMetrics.push({case:'media-reused-exe-current-and-old',rows});
  check(rows.every(r=>r.games.length===1&&r.games[0].key===r.key&&!r.games[0].mediaOnly&&r.games[0].media==='steam-620'),'stale AppID record overwrote current card media or created an unscanned card');
  check(rows.every(r=>r.sourceUnchanged&&r.writes.length===0),'media fallback deleted old bytes or performed a write to repair UI association');
 });
 await run('media merge keeps unconfirmed EXE fallback without granting the record Steam identity',async()=>{
  await beginMediaCardConflictProbe();
  const row=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/Unconfirmed/game.exe',g=p.game('G:/Owned/Unconfirmed',620,exe,false);p.install([g],[p.entry(570,exe)]);const rows=mergedGames();return {rows:rows.map(p.row),key:keyFor(g),writes:p.writes()};});saveMetrics.push({case:'media-unconfirmed-exe-fallback',row});
  check(row.rows.length===1&&row.rows[0].key===row.key&&row.rows[0].media==='steam-570'&&!row.rows[0].appId&&!row.rows[0].mediaOnly&&row.writes.length===0,'unconfirmed numeric candidate blocked personal media or was promoted by its record');
 });
 await run('media merge retains same-identity sorted matches and normalized local assets',async()=>{
  await beginMediaCardConflictProbe();
  const row=await page.evaluate(()=>{const p=mediaCardConflictProbe,exe='G:/Owned/Same/game.exe',a=p.game('G:/Owned/A-same',570,'G:/Owned/A/game.exe'),b=p.game('G:/Owned/B-same',570,exe);p.install([a,b],[p.entry(570,exe.toUpperCase().replaceAll('/','\\')+'\\')]);const same=mergedGames().map(p.row);const local=p.game('G:/Owned/Local',620,'G:/Owned/Local/game.exe');p.install([local],[p.entry(0,local.primaryExecutable)]);return {same,first:keyFor(a),second:keyFor(b),local:mergedGames().map(p.row),writes:p.writes()};});saveMetrics.push({case:'media-compatible-sorted-and-local',row});
  check(row.same.find(g=>g.key===row.first)?.media==='steam-570'&&row.same.find(g=>g.key===row.second)?.media===''&&row.same.length===2,'compatible AppID and EXE matches lost original sorted first-match behavior');
  check(row.local.length===1&&row.local[0].media==='exe-owned-local'&&!row.local[0].mediaOnly&&row.writes.length===0,'identity-free local imported media was rejected or caused a write');
 });


 await run('conflicting old media becomes editable when a matching installed card is scanned',async()=>{
  await beginMediaCardConflictProbe();
  const hidden=await page.evaluate(()=>{
   const p=mediaCardConflictProbe,exe='G:/Owned/Reused/game.exe',g=p.game('G:/Owned/Reused',620,exe),entries=[p.entry(620,exe),p.entry(570,exe)];
   p.install([g],entries);renderLibrary();const before={cards:libraryCards().length,models:mergedGames().map(p.row),cached:!!snapshot.gameMedia.entries['steam-570']};
   const restored=p.game('G:/Owned/Restored',570,'G:/Owned/Restored/game.exe');
   p.install([g,restored],entries);p.standaloneOwnerKey=keyFor(g);p.restoredOwnerKey=keyFor(restored);p.currentMediaBefore=JSON.stringify(snapshot.gameMedia.entries['steam-620']);
   const card=mergedGames().find(x=>keyFor(x)===p.restoredOwnerKey);if(!card||card.mediaOnly||card.gameMedia?.key!=='steam-570')throw Error('matching installed owner did not reuse its old media');openEdit(card);
   return before;
  });
  check(hidden.cached&&hidden.cards===1&&hidden.models.length===1&&hidden.models[0].media==='steam-620','old cache produced a card before a matching game was scanned');
  await page.waitForFunction(()=>selectedActionGame?.gameMedia?.key==='steam-570'&&!sharedWallpaperBusy);
  const result=await page.evaluate(async()=>{const p=mediaCardConflictProbe,before=selectedActionGame.gameMedia.bindingRevision,ok=await updateSharedWallpaper('mode',{mode:'static'}),edits=reverseIO.calls.slice(p.baseCalls).filter(x=>x.command==='mediaEdit');return {ok,before,current:selectedActionGame.gameMedia,editor:keyFor(selectedActionGame),edits,currentCard:mergedGames().find(g=>keyFor(g)===p.standaloneOwnerKey)?.gameMedia?.key,currentMediaUnchanged:JSON.stringify(snapshot.gameMedia.entries['steam-620'])===p.currentMediaBefore};});saveMetrics.push({case:'media-conflict-standalone-edit',result});
  check(result.ok&&result.editor.endsWith('\\restored')&&result.current.key==='steam-570'&&result.current.wallpaperMode==='static'&&result.current.bindingRevision>result.before,'old asset could not be edited through its newly scanned owner');
  check(result.edits.length===1&&result.edits[0].args.key==='steam-570'&&result.edits[0].args.appId===570&&result.currentCard==='steam-620'&&result.currentMediaUnchanged,'editing restored old media changed the other game identity or its wallpaper record');
 });

 await run('editor save background receipt retains committed data and defers editor rebuild until focus',async()=>{
  await page.evaluate(()=>{
   const actual=invoke;window.backgroundSaveProbe={submissions:[],candidateBuilds:0,nameBuilds:0};
   const candidates=renderIdentityCandidates,names=renderIdentityNameCandidates;renderIdentityCandidates=(...args)=>{backgroundSaveProbe.candidateBuilds++;return candidates(...args);};renderIdentityNameCandidates=(...args)=>{backgroundSaveProbe.nameBuilds++;return names(...args);};
   invoke=async(command,args={},timeoutMs=0)=>{
    if(command!=='saveEditor')return actual(command,args,timeoutMs);
    backgroundSaveProbe.submissions.push(structuredClone(args));
    return new Promise(resolve=>backgroundSaveProbe.release=()=>{
     const state=__LIBRARY_H5_FIXTURE__.snapshot;state.manualOverrides||={items:{}};
     for(const op of args.operations){
      if(op.command==='saveManualName'){const key=normalizedUiPath(op.arguments.executable);state.manualOverrides.items[key]={...(state.manualOverrides.items[key]||{}),name:op.arguments.name};}
      else if(op.command==='setBucket'){state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}
      else throw Error('unexpected background save fixture operation '+op.command);
     }
     resolve(structuredClone(state));
    });
   };
   const name=document.getElementById('identity-name');name.value='Committed background name';name.dispatchEvent(new Event('input'));editDraft.bucket='non-game';
   window.backgroundSaveFinished=persistEditDraft();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));
  });
  const hidden=await page.evaluate(async()=>{
   const probe=backgroundSaveProbe;probe.candidateBuilds=0;probe.nameBuilds=0;let mutations=0;
   const observer=new MutationObserver(rows=>mutations+=rows.length);observer.observe(document.getElementById('edit-modal'),{childList:true,subtree:true,attributes:true,characterData:true});
   probe.release();const ok=await backgroundSaveFinished;await new Promise(requestAnimationFrame);mutations+=observer.takeRecords().length;observer.disconnect();
   return {ok,mutations,candidateBuilds:probe.candidateBuilds,nameBuilds:probe.nameBuilds,submissions:probe.submissions.length,originalName:editDraft.original.override?.name,name:editDraft.game.override?.name,bucket:editDraft.bucket,originalBucket:editDraft.originalBucket,nameEdits:editDraft.nameEdits,identityDirty:editDraft.identityDirty,saveTasks:editorSaveTasks.size,active:editSaveInFlight};
  });saveMetrics.push({case:'editor-save-background',hidden});
  check(hidden.mutations===0&&hidden.candidateBuilds===0&&hidden.nameBuilds===0,'background save rebuilt editor candidate, status or dropdown DOM');
  check(hidden.ok&&hidden.originalName==='Committed background name'&&hidden.name==='Committed background name'&&hidden.bucket==='non-game'&&hidden.originalBucket==='non-game'&&!hidden.identityDirty&&hidden.saveTasks===0&&!hidden.active&&hidden.submissions===1,'background save lost the committed model or retained its write owner');
  await page.evaluate(async()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});
  const resumed=await page.evaluate(()=>({title:document.getElementById('edit-title').textContent,candidateBuilds:backgroundSaveProbe.candidateBuilds,nameBuilds:backgroundSaveProbe.nameBuilds,input:document.getElementById('identity-name').value,pending:cardOperationEditorPending}));saveMetrics.at(-1).resumed=resumed;
  check(resumed.candidateBuilds===1&&resumed.nameBuilds===1&&resumed.title==='Committed background name'&&resumed.input==='Committed background name'&&!resumed.pending,'focus did not rebuild exactly one editor from the committed model');
  const unchanged=await page.evaluate(async()=>({ok:await persistEditDraft(),submissions:backgroundSaveProbe.submissions.length}));
  check(unchanged.ok&&unchanged.submissions===1,'foreground presentation made a committed save dirty and replayed its writes');
 });


 await run('editor save background failure survives notice dismissal and explicit retry clears only its task',async()=>{
  await page.evaluate(()=>{
   const actual=invoke;window.failedSaveProbe={calls:[],key:keyFor(selectedActionGame),directory:selectedActionGame.gameDirectory};
   const commit=args=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;for(const op of args.operations){if(op.command!=='setBucket')throw Error('unexpected failed save fixture operation');state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}return structuredClone(state);};
   invoke=async(command,args={},timeoutMs=0)=>{if(command!=='saveEditor')return actual(command,args,timeoutMs);failedSaveProbe.calls.push(structuredClone(args));if(failedSaveProbe.calls.length===1)return new Promise((resolve,reject)=>failedSaveProbe.fail=()=>reject(Error('owned save unavailable')));return commit(args);};
   editDraft.bucket='non-game';window.failedBackgroundSave=persistEditDraft();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));
  });
  const failed=await page.evaluate(async()=>{failedSaveProbe.fail();const ok=await failedBackgroundSave;clearNotice();return {ok,task:structuredClone(editorSaveTasks.get(failedSaveProbe.key)),active:editSaveInFlight,bucket:editDraft.bucket,originalBucket:editDraft.originalBucket,calls:failedSaveProbe.calls.length};});saveMetrics.push({case:'editor-save-background-failure',failed});
  check(!failed.ok&&failed.task?.phase==='failed'&&failed.task.label.includes('owned save unavailable')&&!failed.active&&failed.bucket==='non-game'&&failed.originalBucket!=='non-game'&&failed.calls===1,'background save failure disappeared with its transient notice or retained a running owner');
  await page.evaluate(async()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);});
  check(await page.evaluate(()=>document.getElementById('edit-status').textContent.includes('保存失败')&&!document.getElementById('edit-save-content').disabled&&failedSaveProbe.calls.length===1),'failed original card is not visibly retryable or retried without user intent');
  await page.evaluate(()=>reverseEmit({event:'steam-commit-progress',directories:[failedSaveProbe.directory],phase:'running'}));
  check(await page.evaluate(()=>activeCardOperation(failedSaveProbe.key)?.phase==='running'),'a retained save failure masked current Steam join progress');
  await page.evaluate(()=>reverseEmit({event:'steam-commit-progress',directories:[failedSaveProbe.directory],phase:'completed'}));
  check(await page.evaluate(()=>activeCardOperation(failedSaveProbe.key)?.phase==='failed'),'completed unrelated Steam work erased the failed save result');
  const retried=await page.evaluate(async()=>({ok:await persistEditDraft(),task:editorSaveTasks.get(failedSaveProbe.key),calls:failedSaveProbe.calls,active:editSaveInFlight,bucket:editDraft.originalBucket}));saveMetrics.at(-1).retried=retried;
  check(retried.ok&&!retried.task&&!retried.active&&retried.bucket==='non-game'&&retried.calls.length===2&&JSON.stringify(retried.calls[0].operations)===JSON.stringify(retried.calls[1].operations),'explicit retry did not preserve its operations, release only its save task or commit the baseline');
 });

 await run('captured editor save continues after Back without reopening or replacing another card',async()=>{

  await page.setViewportSize({width:1280,height:720});

  await page.evaluate(()=>{

   const base=invoke;window.savedOwner=selectedActionGame.primaryExecutable;editDraft.bucket='non-game';

   invoke=async(command,args={})=>{if(command==='saveEditor'){window.acceptedSave=structuredClone(args);return new Promise(resolve=>window.finishSave=()=>resolve(structuredClone(__LIBRARY_H5_FIXTURE__.snapshot)));}return base(command,args);};

   window.saveFinished=persistEditDraft();routeWorkspaceAction('back');reverseOpenOther();

  });await page.waitForFunction(()=>window.finishSave);

  const owner=await page.evaluate(()=>keyFor(selectedActionGame));await page.evaluate(async()=>{finishSave();await saveFinished;});

  check(await page.evaluate(()=>acceptedSave.operations.some(o=>o.command==='setBucket'&&o.arguments.bucket==='non-game')),'accepted save lost its captured bucket');

  check(await page.evaluate(owner=>keyFor(selectedActionGame)===owner,owner),'save completion reopened or overwrote another editor');

  check(await page.evaluate(()=>editorSaveTasks.size===0&&!editSaveInFlight),'save ownership did not release');

 });

 await run('editor Steam join stays inline, suppresses duplicate requests and survives Back',async()=>{

  await page.setViewportSize({width:1280,height:800});

  const rows=[];
  for(const phase of ['completed','failed']){
   const inFlight=await page.evaluate(async phase=>{
    document.hasFocus=()=>true;
    const bridge=window.chrome.webview,post=bridge.postMessage;
    const key=keyFor(selectedActionGame),button=$('#edit-add-to-steam'),modal=$('#steam-add-notice-modal');
    const error='请先关闭 Steam，再点击卡片中的加入 Steam 重试';
    const probe=window.joinReceiptProbe={bridge,post,key,phase,error,requests:[],toast:toastNotice.textContent};
    bridge.postMessage=function(message){if(message.command==='commit'){probe.requests.push(structuredClone(message));return;}return post.call(bridge,message);};
    probe.join=joinEditedGameToSteam();void joinEditedGameToSteam();
    const initial={count:probe.requests.length,target:probe.requests[0]?.arguments.selectedGameDirectories,inline:$('#steam-commit-modal').classList.contains('hidden')&&$('#busy').classList.contains('hidden')&&modal.classList.contains('hidden')};
    reverseEmit({event:'steam-commit-progress',directories:[key],phase:'running'});
    reverseEmit({event:'steam-commit-progress',directories:[key],phase,error});
    await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);
    const disabled=button.disabled,stillInline=modal.classList.contains('hidden');
    button.click();
    probe.direct=joinEditedGameToSteam();
    return {phase,key,initial,disabled,stillInline,count:probe.requests.length};
   },phase);
   if(phase==='completed')await page.screenshot({path:path.join(out,'steam-join-inline-1280x800.png')});
   const after=await page.evaluate(async()=>{
    const p=joinReceiptProbe,completed=p.phase==='completed',modal=$('#steam-add-notice-modal');
    if(completed)routeWorkspaceAction('back');
    for(const {id} of p.requests)reverseEmit(completed?{kind:'response',id,ok:true,result:fixtureSteamCommitResult(__LIBRARY_H5_FIXTURE__.snapshot,[p.key])}:{kind:'response',id,ok:false,error:p.error});
    const ok=await p.join,direct=await p.direct;
    await new Promise(requestAnimationFrame);
    const view={ok,direct,phase:steamCommitTasks.get(p.key)?.phase,editorClosed:$('#edit-modal').classList.contains('hidden'),notice:!modal.classList.contains('hidden'),message:$('#steam-add-notice-message').textContent,legacyToastUnchanged:toastNotice.textContent===p.toast,canRetry:!$('#edit-add-to-steam').disabled};
    p.bridge.postMessage=p.post;
    while(!modal.classList.contains('hidden'))closeSteamAddNotice();
    if(completed)openEdit(mergedGames().find(game=>keyFor(game)===p.key));
    return view;
   });
   rows.push({...inFlight,after});
  }
  progressMetrics.push({case:'editor-steam-join-waits-for-receipt',rows});
  check(rows.every(row=>row.initial.count===1&&row.initial.target.join()===row.key),'single card join duplicated or widened its target');
  check(rows.every(row=>row.initial.inline&&row.stillInline),'join opened a modal or busy overlay before its receipt');
  check(rows.every(row=>row.disabled&&row.count===1&&row.after.direct===false),'terminal progress unlocked the same card before its Steam join receipt');
  check(rows[0].after.ok&&rows[0].after.editorClosed&&rows[0].after.phase==='completed'&&rows[0].after.notice,'join was discarded or reopened editor after Back');
  check(!rows[1].after.ok&&!rows[1].after.editorClosed&&rows[1].after.phase==='failed'&&rows[1].after.canRetry&&rows[1].after.notice&&rows[1].after.message.includes('请先关闭 Steam')&&rows[1].after.legacyToastUnchanged,'Steam running did not use the shared dialog and release the card for retry');

 });

 await run('a Steam join receipt waits for the open confirmation before showing its notice',async()=>{
  const rows=[];
  for(const action of ['back','accept']){
   if(rows.length)await reset();
   await holdNativeSnapshotLane();
   const r=await page.evaluate(async action=>{
    const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
    const view=()=>({top:activeWorkspaceModal()?.id,confirm:!$('#confirm-modal').classList.contains('hidden'),notice:!$('#steam-add-notice-modal').classList.contains('hidden'),busy:!$('#busy').classList.contains('hidden'),queued:steamAddNotices.length,focus:document.activeElement.id});
    const key=keyFor(selectedActionGame),join=joinEditedGameToSteam();closeEdit();
    let answered=null,answerCount=0,secondAnswer=null;
    const confirm=askConfirm('移除扫描目录','不删除游戏文件。','移除').then(value=>{answered=value;answerCount++;if(value)setBusy(true,'正在处理确认操作','');});
    const second=askConfirm('第二项确认','另一项操作。','继续').then(value=>{secondAnswer=value;});
    const initial=view();
    snapshotIO.replyWrite(0);const joined=await join;await flush();
    const before=view();
    controllerBackLastAt=0;
    if(action==='accept'&&before.top==='confirm-modal')$('#confirm-confirm').focus();
    routeWorkspaceAction(action);await flush();
    const after={...view(),answered,answerCount,secondAnswer};
    if(after.busy)setBusy(false);
    await flush();
    const result={...view(),message:$('#steam-add-notice-message').textContent};
    if(result.notice){controllerBackLastAt=0;routeWorkspaceAction('back');await flush();}
    const next={...view(),title:$('#confirm-title').textContent};
    cancelPendingConfirmations();await Promise.all([confirm,second]);
    return {action,initial,before,after,result,next,joined,phase:steamCommitTasks.get(key)?.phase,writes:snapshotIO.writes.length,reads:snapshotIO.reads.length};
   },action);
   rows.push(r);
  }
  progressMetrics.push({case:'steam-join-notice-waits-for-confirmation',rows});
  check(rows.every(r=>r.initial.top==='confirm-modal'&&r.joined&&r.phase==='completed'&&r.writes===1&&r.reads===0),'confirmation fixture lost the original join or its reply');
  check(rows.every(r=>r.before.top==='confirm-modal'&&r.before.confirm&&!r.before.notice&&r.before.queued===1&&r.before.focus===r.initial.focus),'Steam join notice covered a live confirmation or stole its focus');
  check(rows.every(r=>r.after.answerCount===1&&r.after.answered===(r.action==='accept')&&r.after.secondAnswer===null&&(r.action==='accept'?r.after.busy&&!r.after.notice&&r.after.queued===1:r.after.notice&&!r.after.busy)),'controller input did not finish the original confirmation or await its accepted work');
  check(rows.every(r=>r.result.top==='steam-add-notice-modal'&&!r.result.confirm&&r.result.message.includes('已加入 Steam')&&r.result.queued===0&&r.next.top==='confirm-modal'&&r.next.title==='第二项确认'),'Steam result was lost, duplicated or covered the next confirmation');
 });

 await run('Steam result and dedicated confirmation dialogs keep one focus owner',async()=>{
  const rows=[];
  for(const mode of ['batch first','delete first','result first','batch accepted']){
   if(rows.length)await reset();
   await holdNativeSnapshotLane();
   rows.push(await page.evaluate(async mode=>{
    const flush=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
    const ports=[],actual=invoke;
    invoke=(command,args={},timeout=0)=>command==='steamCommitPreflight'?new Promise(resolve=>ports.push(resolve)):actual(command,args,timeout);
    const back=()=>{controllerBackLastAt=0;routeWorkspaceAction('back');};
    const view=()=>({top:activeWorkspaceModal()?.id,focus:document.activeElement.id,notice:!$('#steam-add-notice-modal').classList.contains('hidden'),add:!$('#steam-commit-modal').classList.contains('hidden'),remove:!$('#steam-delete-modal').classList.contains('hidden'),busy:!$('#busy').classList.contains('hidden'),waiting:steamAddNotices.length});
    const key=keyFor(selectedActionGame),join=joinEditedGameToSteam();closeEdit();
    let request;
    if(mode==='delete first'){
     snapshot.steamPlan.items[0].status='already-in-steam';activeTab='in-steam';selectedSteamDirectories=new Set([key]);renderLibrary();requestSteamDelete();
    }else{
     request=requestSteamCommit([key]);
     if(mode!=='result first'){ports[0]({steamRunning:false});await request;}
    }
    const initial=view();snapshotIO.replyWrite(0);const joined=await join;await flush();
    const afterReceipt=view();
    if(mode==='result first'){selectedGameDirectories=new Set();ports[0]({steamRunning:false});await request;}
    const overlapping=view(),captured=pendingSteamCommitArguments?.target.selectedGameDirectories||null;
    let pendingAdmission,working;
    if(mode==='batch accepted'&&overlapping.top==='steam-commit-modal'){
     $('#steam-commit-confirm').focus();routeWorkspaceAction('accept');await flush();pendingAdmission=view();
     ports[1]({steamRunning:false});await flush();working=view();snapshotIO.replyWrite(1);await flush();
    }else back();
    await flush();const afterFirst=view();
    if(afterFirst.notice||afterFirst.add||afterFirst.remove){back();await flush();}
    const afterSecond=view();
    return {mode,key,joined,initial,afterReceipt,overlapping,captured,pendingAdmission,working,afterFirst,afterSecond,ports:ports.length,writes:snapshotIO.writes.map(x=>x.message.arguments.selectedGameDirectories)};
   },mode));
  }
  progressMetrics.push({case:'steam-dedicated-dialogs-one-owner',rows});
  check(rows.every(r=>r.joined&&r.writes[0].join()===r.key),'single-card receipt did not complete on its original target');
  check(rows.every(r=>Number(r.overlapping.add)+Number(r.overlapping.remove)+Number(r.overlapping.notice)===1&&r.overlapping.top===(r.mode==='delete first'?'steam-delete-modal':r.mode==='result first'?'steam-add-notice-modal':'steam-commit-modal')),'Steam result and dedicated confirmation displayed together');
  check(rows.slice(0,3).every(r=>r.writes.length===1&&(r.mode==='result first'?r.captured.join()===r.key&&r.afterFirst.add&&!r.afterFirst.notice:!r.afterFirst.add&&!r.afterFirst.remove&&r.afterFirst.notice)&&!r.afterSecond.notice&&!r.afterSecond.add&&!r.afterSecond.remove),'Back lost a result, changed a captured target or started an unconfirmed write');
  const accepted=rows.at(-1);
  check(accepted.ports===2&&accepted.pendingAdmission.waiting===1&&!accepted.pendingAdmission.notice&&accepted.working.busy&&!accepted.working.notice&&accepted.writes.length===2&&accepted.writes[1].join()===accepted.key&&accepted.afterFirst.notice&&accepted.afterSecond.notice,'accepted batch preflight let a result steal the busy surface or lost either completion');
 });

 await run('Steam add replies report only verified results for the requested cards',async()=>{
  const rows=await page.evaluate(async()=>{
   document.hasFocus=()=>true;
   const a=selectedActionGame;reverseOpenOther();const b=selectedActionGame,keys=[a,b].map(keyFor),original=structuredClone(snapshot);
   const bridge=window.chrome.webview,post=bridge.postMessage,rows=[];
   const account=(accountId,committed,statuses)=>({schemaVersion:1,mode:'steam-library-add-commit',accountId,committed,
    ...(committed?{}:{reason:'nothing-ready-to-add'}),...(statuses?{items:statuses.map((status,i)=>({gameDirectory:[a,b][i].gameDirectory,primaryExecutable:[a,b][i].primaryExecutable,status}))}:{})});
   for(const [label,committed,statuses] of [
    ['not ready',false,['requires-review','already-in-steam']],['already present',false,['already-in-steam']],
    ['added',true,['added-to-steam']],['mixed batch',true,['added-to-steam','requires-review']],
    ['another account missing',true,['added-to-steam']],['current account unknown',true,['added-to-steam']],
    ['cache only',true,['added-to-steam']],['malformed receipt',true,null],
   ]){
    steamAddNotices.length=0;closeSteamAddNotice();steamCommitTasks.clear();snapshot=structuredClone(original);openEdit(a);
    const batch=label==='mixed batch',unknown=label==='current account unknown';
    const receipt=account(unknown?'other':'current',committed,statuses);
    const completion={...receipt,mode:'steam-library-add-commit-all-accounts',allSteamAccounts:true,
     accountResults:label==='another account missing'?[receipt,account('other',false,['requires-review'])]:[receipt]};
    const next={...structuredClone(original),steamPlan:completion,steamPlanSource:'verified-steam-add-completion'};
    if(unknown){next.steamPlan=original.steamPlan;next.steamPlanSource='cached';next.steamAddCompletion={...completion,displayAccountCompletionConfirmed:false,partialSteamAccounts:true};next.steamAccountWriteWarning='当前账户结果未确认。';}
    if(label==='cache only')next.steamPlanSource='cached';
    if(!statuses)snapshot.steamPlan=account('current',true,['added-to-steam']);
    let request,count=0;const target=keys.slice(0,batch?2:1);
    bridge.postMessage=function(message){if(message.command==='commit'){request=message;++count;}else post.call(bridge,message);};
    try{
     const work=batch?commitSelectedGames(selectedTargetArguments(target)):joinEditedGameToSteam();
     for(const phase of ['running','completed'])reverseEmit({event:'steam-commit-progress',directories:target,phase});
     const early=target.map(key=>steamCommitTasks.get(key)?.label);
     if(!batch)closeEdit();
     reverseEmit({kind:'response',id:request.id,ok:true,result:next});await work;
     rows.push({label,early,badges:target.map(key=>steamCommitTasks.get(key)?.label),message:$('#steam-add-notice-message').textContent,
      count,target:request.arguments.selectedGameDirectories,expectedTarget:target,closed:$('#edit-modal').classList.contains('hidden'),
      keptPlan:!unknown||snapshot.steamPlan.items[0]?.status===original.steamPlan.items[0]?.status});
    }finally{bridge.postMessage=post;}
   }return rows;
  });progressMetrics.push({case:'verified-steam-add-presentation',rows});
  const expected=[['本次未加入'],['原已在 Steam'],['已加入 Steam'],['已加入 Steam','本次未加入'],['部分完成'],['部分完成'],['结果未确认'],['结果未确认']];
  check(rows.every((r,i)=>r.badges.join('|')===expected[i].join('|')&&r.count===1&&r.target.join()===r.expectedTarget.join()&&r.early.every(label=>label!=='已加入 Steam')&&r.keptPlan),'Steam progress or receipt claimed an unproven write, lost its target or projected another account');
  check(rows.every(r=>r.label==='mixed batch'||r.closed)&&rows[0].message.includes('本次未加入')&&rows[1].message.includes('原已在 Steam')&&rows[2].message.includes('已加入 Steam')&&rows[3].message.includes('新加入 1')&&rows[3].message.includes('未加入 1')&&rows[4].message.includes('部分完成')&&rows[5].message.includes('当前账户结果未确认')&&rows.slice(6).every(r=>r.message.includes('结果未确认')&&!r.message.includes('已加入 Steam')),'Steam add dialog did not distinguish confirmed, unchanged, partial and unknown results through Back');
 });

 await run('different editor cards submit distinct Steam requests while the first is pending',async()=>{

  await page.evaluate(()=>{

   const base=invoke;window.multiJoins=[];invoke=async(command,args={})=>{if(command==='commit')return new Promise(resolve=>multiJoins.push({args:structuredClone(args),resolve}));return base(command,args);};

   window.firstJoin=joinEditedGameToSteam();const second=structuredClone(selectedActionGame);second.gameDirectory+='-second';second.primaryExecutable+='-second';openEdit(second);window.secondJoin=joinEditedGameToSteam();

  });

  check(await page.evaluate(()=>multiJoins.length===2&&multiJoins.every(j=>j.args.selectedGameDirectories.length===1)&&multiJoins[0].args.selectedGameDirectories[0]!==multiJoins[1].args.selectedGameDirectories[0]),'pending join blocked another card or mixed the selected targets');

  const owner=await page.evaluate(()=>keyFor(selectedActionGame));

  await page.evaluate(async()=>{multiJoins[0].resolve(structuredClone(__LIBRARY_H5_FIXTURE__.snapshot));await firstJoin;});

  check(await page.evaluate(key=>keyFor(selectedActionGame)===key&&steamCommitTasks.get(key).phase==='queued',owner),'first completion changed the second card or its queue status');

  await page.evaluate(async()=>{multiJoins[1].resolve(structuredClone(__LIBRARY_H5_FIXTURE__.snapshot));await secondJoin;});
  const deletion=await page.evaluate(async()=>{
   const actual=invoke,rows=[];let calls=[];
   invoke=async(command,args={},timeout=0)=>{
    if(command==='deleteFromSteam'){calls.push(structuredClone(args));return structuredClone(snapshot);}
    return actual(command,args,timeout);
   };
   const settle=()=>new Promise(resolve=>requestAnimationFrame(resolve));
   const setup=()=>{
    closeEdit();steamAddNotices.length=0;$('#steam-add-notice-modal').classList.add('hidden');$('#steam-delete-modal').classList.add('hidden');
    reverseLargeLibrary(2);activeTab='in-steam';for(const item of snapshot.steamPlan.items)item.status='already-in-steam';
    const [a,b]=snapshot.library.games.map(keyFor);selectedSteamDirectories=new Set([a]);calls=[];renderLibrary();$('#delete-button').focus();return {a,b};
   };
   for(const mode of ['unchanged','selection changed','default selection expanded','confirmed card removed','different root','same root alias','cancel and reopen','Back and reopen','duplicate confirm','empty selection']){
    const {a,b}=setup();let expected=[a];
    if(mode==='default selection expanded'){selectedSteamDirectories=null;snapshot.steamPlan.items[1].status='ready-to-add';}
    if(mode==='empty selection')selectedSteamDirectories=new Set();
    $('#delete-button').click();const opened=!$('#steam-delete-modal').classList.contains('hidden'),summary=$('#delete-summary').textContent;
    const focused=Boolean(document.activeElement?.closest('#steam-delete-modal'));
    if(mode==='selection changed')selectedSteamDirectories=new Set([b]);
    if(mode==='default selection expanded'){const next=structuredClone(snapshot);next.steamPlan.items[1].status='already-in-steam';applyLibrarySnapshot(next,{deferRender:true});}
    if(mode==='confirmed card removed'){const next=structuredClone(snapshot);next.library.games=next.library.games.slice(1);next.steamPlan.items=next.steamPlan.items.slice(1);selectedSteamDirectories=null;applyLibrarySnapshot(next,{deferRender:true});}
    if(mode==='different root'){const next=structuredClone(snapshot);next.dataRoot='G:/ReplacedDeleteRoot';applyLibrarySnapshot(next,{deferRender:true});expected=[];}
    if(mode==='same root alias')snapshot={...snapshot,dataRoot:snapshot.dataRoot.replaceAll('/','\\').toUpperCase()};
    if(mode==='cancel and reopen'||mode==='Back and reopen'){
     if(mode==='cancel and reopen')$('#steam-delete-cancel').click();else routeWorkspaceAction('back');
     const closed=$('#steam-delete-modal').classList.contains('hidden');selectedSteamDirectories=new Set([b]);$('#delete-button').click();expected=[b];
     if(!closed)throw Error('Delete confirmation did not close before reopening');
    }
    if(mode==='empty selection')expected=[];
    $('#steam-delete-confirm').click();if(mode==='duplicate confirm')$('#steam-delete-confirm').click();await settle();
    rows.push({mode,a,b,expected,calls:structuredClone(calls),opened,focused,summary,closed:$('#steam-delete-modal').classList.contains('hidden')});
   }
   invoke=actual;return rows;
  });saveMetrics.push({case:'steam-delete-confirmed-selection',rows:deletion});
  check(deletion.every(row=>row.expected.length?row.calls.length===1&&row.calls[0].selectionMode==='all-steam-accounts'&&JSON.stringify(row.calls[0].selectedGameDirectories)===JSON.stringify(row.expected):row.calls.length===0),'Steam deletion used changed or broadened selection after confirmation opened');
  check(deletion.every(row=>row.closed&&(row.mode==='empty selection'?!row.opened:row.opened&&row.focused&&row.summary.includes('1 个'))),'Steam deletion confirmation lost focus count or close state');
  const preflights=await page.evaluate(async()=>{
   const actual=invoke,actualBusy=runBusy,actualClose=closeSteamOnly,rows=[];let pending=[],writes=[],closes=0,closeClients=[];
   const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
   invoke=(command,args={},timeout=0)=>command==='steamCommitPreflight'?new Promise((resolve,reject)=>pending.push({resolve,reject})):actual(command,args,timeout);
   runBusy=async(command,title,detail,args={})=>{writes.push({command,args:structuredClone(args)});return true;};
   closeSteamOnly=async expected=>{closes++;closeClients.push(structuredClone(expected));};
   const setup=(icons=false)=>{
    closeEdit();steamAddNotices.length=0;$('#steam-add-notice-modal').classList.add('hidden');$('#confirm-modal').classList.add('hidden');closeSteamCommit();
    reverseLargeLibrary(2);if(icons){activeTab='in-steam';for(const item of snapshot.steamPlan.items)item.status='already-in-steam';selectedSteamDirectories=new Set([keyFor(snapshot.library.games[0])]);renderLibrary();}
    pending=[];writes=[];closes=0;closeClients=[];steamRuntimeState={running:false,steamPath:'owned-current-state',steamClient:null};clearNotice();return keyFor(snapshot.library.games[0]);
   };
   const switchRoot=()=>{snapshot={...snapshot,dataRoot:'G:/ReplacementPreflightRoot'};};
   const retire=()=>{window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));};
   const record=(mode,expected,notice)=>rows.push({mode,expected,preflightCount:pending.length,writes:structuredClone(writes),closes,closeClients:structuredClone(closeClients),commitVisible:!$('#steam-commit-modal').classList.contains('hidden'),confirmVisible:!$('#confirm-modal').classList.contains('hidden'),addNoticeVisible:!$('#steam-add-notice-modal').classList.contains('hidden'),runtime:steamRuntimeState.running,runtimePath:steamRuntimeState.steamPath,noticeKept:notice===undefined||notice===toastNotice.textContent,requestActive:steamCommitRequestInFlight});
   for(const mode of ['bulk normal','bulk initial root changed','bulk initial page retired','bulk late error','bulk close consent root changed','bulk close consent current','bulk confirm root changed','bulk second root changed','bulk second close consent root changed','bulk confirmed selection stable','bulk duplicate initial','bulk empty selection','icons root changed','icons close consent root changed','icons current confirmed','icons duplicate initial']){
    const icons=mode.startsWith('icons'),a=setup(icons);let task;
    if(icons){updateSteamArtworkButton.click();if(mode==='icons duplicate initial')updateSteamArtworkButton.click();}
    else{task=requestSteamCommit(mode==='bulk empty selection'?[]:[a]);if(mode==='bulk duplicate initial')void requestSteamCommit([a]);}
    if(mode==='bulk initial root changed'||mode==='icons root changed')switchRoot();
    if(mode==='bulk initial page retired')retire();
    if(mode==='bulk late error'){switchRoot();const notice=toastNotice.textContent;pending[0].reject(Error('owned retired preflight failed'));await task;record(mode,'retired',notice);continue;}
    const firstRunning=mode==='bulk close consent root changed'||mode==='bulk close consent current'||mode==='icons close consent root changed'||mode==='icons current confirmed'||mode==='bulk empty selection';
    for(const request of pending)request.resolve({steamRunning:firstRunning,steamClient:firstRunning?structuredClone(window.__libraryShutdownClient):null});await flush();
    if(mode==='bulk close consent root changed'||mode==='icons close consent root changed'){switchRoot();if(!$('#confirm-modal').classList.contains('hidden'))closeConfirm(true);await flush();}
    else if(!$('#confirm-modal').classList.contains('hidden')){closeConfirm(mode==='bulk close consent current'||mode==='icons current confirmed');await flush();}
    if(task)await task;
    if(mode==='bulk confirm root changed'||mode==='bulk second root changed'||mode==='bulk second close consent root changed'||mode==='bulk confirmed selection stable'){
     if(mode==='bulk confirm root changed')switchRoot();
     selectedGameDirectories=new Set([keyFor(snapshot.library.games[1])]);$('#steam-commit-confirm').click();await flush();
     if(mode==='bulk second root changed')switchRoot();
     if(pending[1])pending[1].resolve({steamRunning:mode==='bulk second close consent root changed',steamClient:mode==='bulk second close consent root changed'?structuredClone(window.__libraryShutdownClient):null});await flush();
     if(mode==='bulk second close consent root changed'){switchRoot();if(!$('#confirm-modal').classList.contains('hidden'))closeConfirm(true);await flush();}
    }
    const expected=mode==='bulk normal'||mode==='bulk duplicate initial'?'dialog':mode==='bulk close consent current'?'close':mode==='bulk confirmed selection stable'?'commit':mode==='icons current confirmed'||mode==='icons duplicate initial'?'artwork':'retired';record(mode,expected);
    rows.at(-1).target=a;
   }

   const continuations=[];
   {
    const a=setup(),older=requestSteamCommit([a]);switchRoot();const newer=requestSteamCommit([a]);const admitted=pending.length===2;
    pending[0].resolve({steamRunning:false});await older;await flush();const activeAfterOld=Boolean(steamCommitRequestInFlight);
    if(admitted){void requestSteamCommit([a]);pending[1].resolve({steamRunning:false});}await newer;await flush();
    continuations.push({mode:'bulk new owner while retired read remains pending',admitted,activeAfterOld,preflights:pending.length,dialog:!$('#steam-commit-modal').classList.contains('hidden'),writes:writes.length,released:!steamCommitRequestInFlight});closeSteamCommit();
   }
   {
    setup(true);updateSteamArtworkButton.click();switchRoot();updateSteamArtworkButton.click();const admitted=pending.length===2;
    pending[0].resolve({steamRunning:false});await flush();const activeAfterOld=typeof steamArtworkRequestInFlight!=='undefined'&&Boolean(steamArtworkRequestInFlight);
    if(admitted){updateSteamArtworkButton.click();pending[1].resolve({steamRunning:false});}await flush();
    continuations.push({mode:'artwork new owner while retired read remains pending',admitted,activeAfterOld,preflights:pending.length,writes:writes.length,released:typeof steamArtworkRequestInFlight==='undefined'||!steamArtworkRequestInFlight});
   }
   window.steamPreflightContinuations=continuations;
   invoke=actual;runBusy=actualBusy;closeSteamOnly=actualClose;return rows;
  });saveMetrics.push({case:'steam-preflight-context',rows:preflights});
  check(preflights.every(row=>row.expected==='dialog'?row.preflightCount===1&&row.commitVisible&&row.closes===0&&row.writes.length===0:row.expected==='close'?row.closes===1&&row.writes.length===0:row.expected==='commit'?row.writes.length===1&&row.writes[0].command==='commit'&&row.writes[0].args.selectedGameDirectories.join()===row.target:row.expected==='artwork'?row.preflightCount===1&&row.writes.length===1&&row.writes[0].command==='refreshSteamArtwork'&&(row.mode!=='icons current confirmed'||row.writes[0].args.forceCloseSteam===true):row.closes===0&&row.writes.length===0&&!row.commitVisible&&!row.confirmVisible&&(row.mode==='bulk empty selection'||row.mode==='bulk confirm root changed'||!row.addNoticeVisible)&&row.noticeKept),'retired Steam preflight or confirmation affected the replacement context');
  check(preflights.every(row=>!row.requestActive)&&preflights.find(row=>row.mode==='bulk empty selection').preflightCount===0,'Steam preflight retained an admission flag or queried an empty selection');
  check(preflights.every(row=>row.closeClients.every(client=>JSON.stringify(client)===JSON.stringify(shutdownClient))&&row.writes.every(write=>!write.args.forceCloseSteam||JSON.stringify(write.args.steamClient)===JSON.stringify(shutdownClient))),'Steam consent did not carry its exact client and Native power expectation');
  const continuations=await page.evaluate(()=>steamPreflightContinuations);saveMetrics.push({case:'steam-preflight-retired-owner',rows:continuations});
  check(continuations.every(row=>row.admitted&&row.activeAfterOld&&row.preflights===2&&row.released&&(row.mode.startsWith('bulk')?row.dialog&&row.writes===0:row.writes===1)),'retired Steam preflight blocked or released a newer request owner');

  const filteredSelectionRows=await page.evaluate(async()=>{
   const actual=invoke,saved=structuredClone(snapshot),savedTab=activeTab,state=librarySidebarState(),savedState={...state},savedAdd=selectedGameDirectories,savedSteam=selectedSteamDirectories,rows=[];let writes=[];
   const tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))),flush=async()=>{for(let i=0;i<16;i++)await Promise.resolve();};
   invoke=(command,args={},timeout=0)=>command==='steamCommitPreflight'?Promise.resolve({steamRunning:false}):['state','plan'].includes(command)?Promise.resolve(workspaceBridgeResult(command,structuredClone(snapshot))):['commit','deleteFromSteam'].includes(command)?(writes.push({command,args:structuredClone(args)}),Promise.resolve(workspaceBridgeResult(command,structuredClone(snapshot)))):actual(command,args,timeout);
   if(silentRefreshInFlight)await silentRefreshInFlight;
   const games=['Alpha','Beta'].map(name=>({gameDirectory:'G:/FilteredSelection/'+name,primaryExecutable:'G:/FilteredSelection/'+name+'/Game.exe',directoryName:'Selection '+name,status:'ready'})),[a,b]=games.map(keyFor),missing=normalizedUiPath('G:/FilteredSelection/Removed'),selection=()=>pageSelectionKeys(activeTab);
   const query=text=>{focusWorkspaceElement($('#library-search'));$('#library-search').value=text;$('#library-search').dispatchEvent(new Event('input',{bubbles:true}));};
   const choose=key=>{const card=libraryCards().find(card=>card.dataset.directory===key);if(!card)throw Error('visible selection fixture card missing');focusLibraryCard(card);routeWorkspaceAction('accept');};
   const install=(page,selected)=>{closeEdit();steamAddNotices.length=0;closeSteamAddNotice();closeSteamCommit();$('#steam-delete-modal').classList.add('hidden');snapshot={...saved,library:{games:structuredClone(games)},manualOverrides:{items:{}},gameMedia:{entries:{}},steamPlan:{items:games.map(game=>({...game,status:page==='in-steam'?'already-in-steam':'waiting-steam-verification'})),summary:{}}};activeTab=page;state.query='';$('#library-search').value='';if(page==='in-steam')selectedSteamDirectories=new Set(selected);else selectedGameDirectories=new Set(selected);writes=[];applySteamRuntimeState({steamRunning:false});renderLibrary();};
   for(const page of ['not-in-steam','in-steam'])for(const mode of ['hidden selected deselect visible','removed selected deselect visible','hidden selected select visible','normal toggle on','normal toggle off','all invert roundtrip']){
    const start=mode==='hidden selected deselect visible'?[a,b]:mode==='removed selected deselect visible'?[missing,b]:mode==='hidden selected select visible'||mode==='normal toggle off'?[a]:[];install(page,start);
    let expected,filteredCount=null,pressed=null;
    if(mode.includes('visible')){query('Beta');choose(b);await tick();filteredCount=selectedPageCount();pressed=libraryCards()[0].getAttribute('aria-pressed');query('');expected=mode==='hidden selected deselect visible'?[a]:mode==='removed selected deselect visible'?[]:[a,b];}
    else if(mode==='all invert roundtrip'){setPageSelection('all');setPageSelection('invert');if(selectedPageCount()!==0)throw Error('invert control did not clear all');setPageSelection('invert');expected=[a,b];}
    else {choose(a);expected=mode==='normal toggle on'?[a]:[];}
    await tick();const count=selectedPageCount(),selected=[...selection()].filter(key=>[a,b].includes(key)).sort(),labels=libraryCards().map(card=>({key:card.dataset.directory,pressed:card.getAttribute('aria-pressed')}));
    if(page==='in-steam'){$('#delete-button').click();if(!$('#steam-delete-modal').classList.contains('hidden'))$('#steam-delete-confirm').click();}
    else {await requestSteamCommit();if(!$('#steam-commit-modal').classList.contains('hidden'))$('#steam-commit-confirm').click();}
    await flush();await tick();rows.push({page,mode,a,b,expected,filteredCount,pressed,count,selected,labels,writes:structuredClone(writes)});
   }
   closeSteamCommit();$('#steam-delete-modal').classList.add('hidden');steamAddNotices.length=0;closeSteamAddNotice();invoke=actual;snapshot=saved;activeTab=savedTab;Object.assign(state,savedState);selectedGameDirectories=savedAdd;selectedSteamDirectories=savedSteam;$('#library-search').value=state.query;renderLibrary();return rows;
  });saveMetrics.push({case:'filtered-selection-membership',rows:filteredSelectionRows});
  check(filteredSelectionRows.every(row=>row.count===row.expected.length&&JSON.stringify(row.selected)===JSON.stringify([...row.expected].sort())&&row.labels.every(card=>card.pressed===String(row.expected.includes(card.key)))&&(row.mode.includes('deselect visible')?row.filteredCount===0&&row.pressed==='false':true)&&(row.expected.length?row.writes.length===1&&row.writes[0].command===(row.page==='in-steam'?'deleteFromSteam':'commit')&&JSON.stringify([...row.writes[0].args.selectedGameDirectories].sort())===JSON.stringify([...row.expected].sort()):row.writes.length===0)),'Filtered selection count equality reselected a cancelled game or expanded the Steam operation');





  const queueRows=await page.evaluate(async()=>{
   const actual=invoke,saved=structuredClone(snapshot),state=librarySidebarState(),savedState={...state},focus=document.hasFocus,oldDiscovery=steamAccountDiscovery,oldDiscoveryOwner=steamAccountDiscoveryOwner,oldInstallStatus=steamInstallPromptStatus,oldInstallOwner=steamInstallPromptOwner,rows=[];let preflights=[],writes=[],holdPreflights=false;
   const flush=async()=>{for(let i=0;i<24;i++)await Promise.resolve();},tick=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
   invoke=(command,args={},timeout=0)=>command==='steamCommitPreflight'?(holdPreflights?new Promise(resolve=>preflights.push(resolve)):Promise.resolve({steamRunning:false})):['state','plan'].includes(command)?Promise.resolve(workspaceBridgeResult(command,structuredClone(snapshot))):['closeSteam','refreshSteamArtwork','commit','openSteamWebsite'].includes(command)?(writes.push({command,args:structuredClone(args)}),Promise.resolve(command==='closeSteam'?{steamRunning:false}:command==='openSteamWebsite'?{opened:true}:workspaceBridgeResult(command,structuredClone(snapshot)))):actual(command,args,timeout);
   const states=()=>({title:$('#confirm-title').textContent,label:$('#confirm-confirm').textContent,shown:!$('#confirm-modal').classList.contains('hidden')});
   const reset=()=>{if(typeof cancelPendingConfirmations==='function')cancelPendingConfirmations();else if(confirmResolver)closeConfirm(false);document.hasFocus=()=>true;steamAddNotices.length=0;$('#steam-add-notice-modal').classList.add('hidden');busyOwner=null;setBusy(false);closeSteamCommit();$('#steam-delete-modal').classList.add('hidden');steamCommitRequestInFlight=false;steamArtworkRequestInFlight=false;snapshot={...structuredClone(saved),dataRoot:'G:/ConfirmQueue/Library'};librarySidebarPage('library',false);renderLibrary();preflights=[];writes=[];holdPreflights=false;};
   const track=(list,title,current)=>{const row={title,settled:0,value:null};list.push(row);askConfirm(title,'Owned '+title,title, current?{current}:{}).then(value=>{row.settled++;row.value=value;});return row;};
   for(const mode of ['two independent','three order','page retired','root replaced','background wait','busy wait','accepted work before next','stale predicate','duplicate close before next']){
    reset();await flush();const replies=[];
    if(mode==='background wait'){document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
    if(mode==='busy wait')setBusy(true,'Owned pending work','');
    let allowed=true,acceptedWork=0;
    if(mode==='accepted work before next'){const first=track(replies,'First');const prior=confirmResolver;/* Keep actual promise observer independent from its native continuation. */askConfirm('Second','Owned Second','Second').then(value=>replies.push({title:'Second',settled:1,value}));
     // Answering the first must let its caller start work before showing next.
     const actualResolve=confirmResolver;if(actualResolve===prior)confirmResolver=value=>{actualResolve(value);if(value){acceptedWork++;setBusy(true,'Owned accepted work','');}};
    }else{track(replies,'First');track(replies,'Second',mode==='stale predicate'?()=>allowed:null);if(mode==='three order')track(replies,'Third');}
    const initial=states();
    if(mode==='page retired'){window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));await flush();rows.push({mode,initial,after:states(),replies:structuredClone(replies),writes:writes.length});continue;}
    if(mode==='root replaced'){snapshot={...snapshot,dataRoot:'G:/ConfirmQueue/NewRoot'};track(replies,'Current');await flush();const current=states();closeConfirm(false);await flush();rows.push({mode,initial,current,replies:structuredClone(replies),writes:writes.length});continue;}
    if(mode==='background wait'){document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await flush();}
    if(mode==='busy wait'){setBusy(false);await flush();}
    const first=states();if(mode==='stale predicate')allowed=false;
    closeConfirm(mode==='three order'||mode==='accepted work before next');if(mode==='duplicate close before next')closeConfirm(true);await flush();const second=states(),middle=structuredClone(replies);
    if(mode==='accepted work before next'){const held=states();setBusy(false);await flush();const resumed=states();closeConfirm(false);await flush();rows.push({mode,initial,first,held,resumed,acceptedWork,replies:structuredClone(replies),writes:writes.length});continue;}
    closeConfirm(true);await flush();const third=states();if(mode==='three order'){closeConfirm(true);await flush();}
    rows.push({mode,initial,first,second,third,middle,replies:structuredClone(replies),writes:writes.length});
   }
   reset();await flush();
   // Real delayed callers: bulk-close consent followed by icon-refresh consent.
   holdPreflights=true;const games=['A','B'].map(name=>({gameDirectory:'G:/ConfirmQueue/'+name,primaryExecutable:'G:/ConfirmQueue/'+name+'/Game.exe',directoryName:'Queue '+name,status:'ready'})),[a,b]=games.map(keyFor);
   snapshot={...snapshot,library:{games},manualOverrides:{items:{}},gameMedia:{entries:{}},steamPlan:{items:games.map(game=>({...game,status:'waiting-steam-verification'})),summary:{}}};activeTab='not-in-steam';selectedGameDirectories=new Set([a]);renderLibrary();void requestSteamCommit([a]);
   activeTab='in-steam';snapshot.steamPlan.items[1].status='already-in-steam';selectedSteamDirectories=new Set([b]);renderLibrary();$('#update-steam-artwork-button').click();await flush();if(preflights.length!==2)throw Error('overlapping native preflight setup missing');preflights[0]({steamRunning:true,steamClient:structuredClone(window.__libraryShutdownClient)});await flush();preflights[1]({steamRunning:true,steamClient:structuredClone(window.__libraryShutdownClient)});await flush();const overlapFirst=states();closeConfirm(false);await flush();const overlapSecond=states();closeConfirm(true);await flush();await tick();rows.push({mode:'actual overlapping Steam callers',first:overlapFirst,second:overlapSecond,writes:structuredClone(writes),released:!steamCommitRequestInFlight&&!steamArtworkRequestInFlight,target:b});
   reset();await flush();const manual=[];track(manual,'Manual');steamAccountDiscovery={accounts:[],steamStatus:'not-installed'};steamAccountDiscoveryOwner=captureSteamActionContext();steamInstallPromptStatus='';offerSteamWebsite('not-installed');await new Promise(resolve=>setTimeout(resolve,20));if(!confirmationQueue.some(item=>item.title==='未检测到 Steam'))throw Error('install prompt did not reach the confirmation queue');const beforeInstall=states();steamAccountDiscovery={accounts:[{accountId:'1000',steamRoot:'G:/FixtureSteam'}],steamStatus:'ready'};closeConfirm(false);await flush();const afterInstall=states();closeConfirm(true);await flush();rows.push({mode:'obsolete install prompt',before:beforeInstall,after:afterInstall,manual:structuredClone(manual),writes:structuredClone(writes)});
   if(typeof cancelPendingConfirmations==='function')cancelPendingConfirmations();else if(confirmResolver)closeConfirm(false);document.hasFocus=focus;steamAccountDiscovery=oldDiscovery;steamAccountDiscoveryOwner=oldDiscoveryOwner;steamInstallPromptStatus=oldInstallStatus;steamInstallPromptOwner=oldInstallOwner;invoke=actual;snapshot=saved;Object.assign(state,savedState);$('#library-search').value=state.query;librarySidebarPage(savedState.page,false);renderLibrary();return rows;
  });saveMetrics.push({case:'confirmation-queue-ownership',rows:queueRows});
  check(queueRows.filter(row=>row.mode==='actual overlapping Steam callers').every(row=>row.writes.every(write=>JSON.stringify(write.args.steamClient)===JSON.stringify(shutdownClient))),'Overlapping Steam confirmation replaced the captured client or power tuple');
  check(queueRows.every(row=>row.mode==='actual overlapping Steam callers'?row.first.label==='关闭 Steam'&&row.second.label==='关闭 Steam 并更新'&&row.released&&row.writes.length===1&&row.writes[0].command==='refreshSteamArtwork'&&row.writes[0].args.forceCloseSteam===true&&row.writes[0].args.selectedGameDirectories.join()===row.target:row.mode==='obsolete install prompt'?row.before.title==='Manual'&&!row.after.shown&&row.manual[0].settled===1&&row.manual[0].value===false&&row.writes.length===0:row.mode==='page retired'?!row.after.shown&&row.replies.every(r=>r.settled===1&&r.value===false):row.mode==='root replaced'?row.current.title==='Current'&&row.replies.every(r=>r.settled===1&&r.value===false):row.mode==='accepted work before next'?row.initial.title==='First'&&row.first.title==='First'&&!row.held.shown&&row.resumed.title==='Second'&&row.acceptedWork===1&&row.replies.length===2&&row.replies.every(r=>r.settled===1)&&row.replies[0].value===true&&row.replies[1].value===false:(!['background wait','busy wait'].includes(row.mode)||!row.initial.shown)&&row.first.title==='First'&&row.replies.every(r=>r.settled===1)&&(row.mode==='three order'?row.second.title==='Second'&&row.third.title==='Third'&&row.replies.map(r=>r.value).join()==='true,true,true':row.mode==='stale predicate'?!row.second.shown&&row.replies[0].value===false&&row.replies[1].value===false:row.second.title==='Second'&&row.replies[0].value===false&&row.replies[1].value===true)),'Overlapping confirmation replaced an owner, leaked a request or replayed an unconfirmed Steam operation');

 });

 await run('mode preference commits one entry without requesting a whole library snapshot',async()=>{

  await page.evaluate(()=>updateSharedWallpaper('mode',{mode:'static'}));

  check(await page.evaluate(()=>reverseIO.calls.filter(c=>c.command==='mediaEdit').length===1&&!reverseIO.calls.some(c=>c.command==='mediaState')),'preference save rebuilt the full library');

 });

 await run('background preview and wallpaper recovery stop while cached media remains usable',async()=>{

  await page.waitForFunction(()=>document.querySelector('#artwork-wallpaper-preview video'));

  await page.evaluate(()=>{window.savedFocus=document.hasFocus.bind(document);document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));renderMediaDownloads({...snapshot.mediaDownloads,enabled:true,remaining:3});scheduleSilentRefresh();});

  check(await page.evaluate(()=>document.querySelector('#artwork-wallpaper-preview video').paused&&mediaRecoveryTimer===0&&refreshAfterScrapeTimer===0&&silentRefreshPending),'background kept preview decoding or scheduled repeated reads');

  await page.evaluate(()=>{document.hasFocus=savedFocus;});

 });

 await run('a failed render cannot retain a stale library merge view',async()=>{

  const result=await page.evaluate(()=>{

   const actual=renderMediaDownloads;renderMediaDownloads=()=>{throw Error('fixture-render-failure');};let failed=false;

   try{renderLibrary();}catch(error){failed=error.message==='fixture-render-failure';}finally{renderMediaDownloads=actual;}

   const added={gameDirectory:'G:/Fixture/Late',primaryExecutable:'G:/Fixture/Late/game.exe',directoryName:'Late'};

   snapshot.library.games.push(added);

   return {failed,visible:mergedGames().some(game=>game.primaryExecutable===added.primaryExecutable)};

  });check(result.failed&&result.visible,'exception left the library displaying a stale cached inventory');

 });

 await run('large cached library builds its derived view once per render',async()=>{

  const rows=await page.evaluate(()=>{

   closeEdit();const actualPath=normalizedUiPath,actualPlans=planMap;let pathWork=0,planBuilds=0;

   normalizedUiPath=value=>{++pathWork;return actualPath(value);};planMap=()=>{++planBuilds;return actualPlans();};const rows=[];

   try{for(const count of [80,160]){

    const games=[],items=[],entries={};for(let i=0;i<count;i++){

     const directory='G:/Fixture/Large'+String(i).padStart(4,'0'),executable=directory+'/game.exe',appId=1000+i;

     games.push({gameDirectory:directory,primaryExecutable:executable,directoryName:'Game '+String(i).padStart(4,'0'),status:'ready'});

     items.push({gameDirectory:directory,primaryExecutable:executable,appId,status:'ready-to-add'});entries['steam-'+appId]={key:'steam-'+appId,appId,executable,staticReady:true,dynamicReady:true};

    }

    snapshot={...snapshot,config:{},library:{games},steamPlan:{items},manualOverrides:{items:{}},resolvedIdentities:{},gameMedia:{entries}};

    selectedGameDirectories=null;activeTab='not-in-steam';pathWork=planBuilds=0;const started=performance.now();renderLibrary();

    rows.push({games:count,pathNormalizations:pathWork,planBuilds,elapsedMs:performance.now()-started,cards:library.querySelectorAll('.game-card').length,selected:library.querySelectorAll('.selected-for-steam').length});

   }}finally{normalizedUiPath=actualPath;planMap=actualPlans;}return rows;

  });performanceMetrics.push(...rows);

  check(rows.every(row=>row.planBuilds===1),'card selection rebuilt the complete derived inventory');

  check(rows.every(row=>row.cards===row.games&&row.selected===row.games),'linear rendering changed default selection or lost a cached game');

  check(rows[1].pathNormalizations<=rows[0].pathNormalizations*2.6,'doubling cached games caused quadratic path normalization work');

 });

 await run('legacy path data keeps large render work linear and fresh after edits',async()=>{

  const result=await page.evaluate(()=>{

   closeEdit();const actualPath=normalizedUiPath;let work=0;normalizedUiPath=value=>{++work;return actualPath(value);};const rows=[];

   const make=count=>{

    const games=[],items=[],overrides={},identities={},buckets={};

    for(let i=0;i<count;i++){

     const dir='G:/Fixture/Legacy'+String(i).padStart(4,'0'),exe=dir+'/game.exe';

     games.push({gameDirectory:dir,primaryExecutable:exe,directoryName:'Legacy '+String(i).padStart(4,'0'),status:'ready'});items.push({gameDirectory:dir,primaryExecutable:exe,status:'ready-to-add',appId:1000+i});

     overrides[exe.toUpperCase().replaceAll('/','\\')+'\\']={name:'Renamed '+i};identities['historical-'+i]={executable:exe,formalName:'Resolved '+i};buckets[dir.toUpperCase()+'/']={bucket:'not-in-steam'};

    }

    snapshot={...snapshot,config:{manualBuckets:buckets},library:{games},steamPlan:{items},manualOverrides:{items:overrides},resolvedIdentities:identities,gameMedia:{entries:{}}};activeTab='not-in-steam';selectedGameDirectories=null;

   };

   try{

    for(const count of [80,160]){make(count);work=0;const started=performance.now();renderLibrary();rows.push({legacyGames:count,pathNormalizations:work,elapsedMs:performance.now()-started,cards:libraryCards().length,namesCorrect:libraryCards().every(card=>card.querySelector('.game-card-name')?.textContent.startsWith('Renamed '))});}

    const game=snapshot.library.games[0],oldKey=Object.keys(snapshot.manualOverrides.items)[0];

    snapshot.manualOverrides.items[oldKey]={name:'Changed without replacing snapshot'};renderLibrary();const renamed=libraryCards().some(card=>card.querySelector('.game-card-name')?.textContent==='Changed without replacing snapshot');

    snapshot.config.manualBuckets[Object.keys(snapshot.config.manualBuckets)[0]]={bucket:'non-game'};renderLibrary();const reclassified=libraryCards().length===159;

    return {rows,renamed,reclassified,ownerCleared:typeof libraryPathLookups==='undefined'||libraryPathLookups===null};

   }finally{normalizedUiPath=actualPath;}

  });performanceMetrics.push(...result.rows);

  check(result.rows.every(row=>row.cards===row.legacyGames&&row.namesCorrect),'historical aliases lost card names or bucket ownership');

  check(result.rows[1].pathNormalizations<=result.rows[0].pathNormalizations*2.6,'legacy map fallback returned to quadratic render work');

  check(result.renamed&&result.reclassified&&result.ownerCleared,'synchronous lookup indexes survived a render or ignored a later local edit');

 });

 await run('a changed primary EXE is captured before dependent edits and survives Back',async()=>{
  // Inventory-only cards no longer expose an unmatched media-cache record.
  // The original Back/other-editor assertion needs a real second game.
  await page.evaluate(()=>{
   const state=__LIBRARY_H5_FIXTURE__.snapshot,entry=state.gameMedia.entries['steam-1245620'];
   const exe=entry?.executable||'G:/H5Fixture/OtherGame/other.exe',directory=exe.replace(/[\\/][^\\/]+$/,'');
   if(!state.library.games.some(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(exe))){
    state.library.games.push({gameDirectory:directory,primaryExecutable:exe,directoryName:'Other owned fixture',status:'ready',contentType:'game'});
    state.steamPlan.items.push({gameDirectory:directory,primaryExecutable:exe,appId:1245620,steamStoreAppId:1245620,formalName:'Other owned fixture',status:'ready-to-add'});
   }
   snapshot=structuredClone(state);window.reverseOpenOther=()=>{const game=mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(exe));if(!game)throw Error('second-owned-game-missing');openEdit(game);};
  });


  const result=await page.evaluate(async()=>{

   const before=selectedActionGame.primaryExecutable,next=before.replace(/[^\\/]+$/,'Alternate.exe');window.primaryAccepted=null;

   const actual=invoke;invoke=async(command,args={})=>{if(command==='saveEditor'){window.primaryAccepted=structuredClone(args);return new Promise(resolve=>window.primaryFinish=()=>resolve(structuredClone(__LIBRARY_H5_FIXTURE__.snapshot)));}return actual(command,args);};

   selectedActionGame.primaryExecutable=next;editDraft.primaryDirty=true;document.getElementById('identity-name').value='Explicit new primary name';

   const promise=persistEditDraft();routeWorkspaceAction('back');reverseOpenOther();const owner=keyFor(selectedActionGame);primaryFinish();await promise;

   return {before,next,accepted:primaryAccepted,ownerKept:keyFor(selectedActionGame)===owner};

  });

  check(result.accepted.operations[0]?.command==='setPrimary','changed primary EXE was never included as the first saved operation');

  check(normalizedForTest(result.accepted.operations[0].arguments.executable)===normalizedForTest(result.next),'primary save targeted the wrong EXE');

  check(result.accepted.operations.filter(o=>['saveManualName','identifyOne','saveArtworkOverride'].includes(o.command)).every(o=>normalizedForTest(o.arguments.executable)===normalizedForTest(result.next))&&result.ownerKept,'dependent write was redirected or a late primary result replaced the other editor');
  const transferRows=[];
  for(const mode of ['manual-content','cleared-identity','automatic-defaults','edited-artwork','unavailable-artwork','manual-name-only','legacy-igdb']){
   await reset();
   const row=await page.evaluate(async(mode)=>{
    const state=__LIBRARY_H5_FIXTURE__.snapshot,old=selectedActionGame.primaryExecutable,directory=selectedActionGame.gameDirectory,oldKey=normalizedUiPath(old),next=directory+'/SelectedAlternate.exe';
    const choices=mode==='manual-content'?{name:'Pinned manual title',steamId:620,cover:{path:'G:/RemovedOriginal/cover.jpg',portableFile:'G:/RetainedMedia/cover.jpg',url:'/cover.jpg'},artwork:{wallpaper:{file:'G:/RemovedOriginal/wallpaper.jpg',portableFile:'G:/RetainedMedia/wallpaper.jpg',url:'/wallpaper.jpg'}},artworkProtection:{long:'deleted'}}
      :mode==='cleared-identity'?{idCleared:true}:mode==='manual-name-only'?{name:'Pinned name without ID'}:mode==='legacy-igdb'?{idCleared:true,igdbId:72}
      :mode==='edited-artwork'?{cover:{path:'G:/Old/cover.jpg',portableFile:'G:/OldCache/cover.jpg',url:'/cover.jpg'},artworkProtection:{cover:'manual'}}
      :mode==='unavailable-artwork'?{cover:{path:'G:/Missing/cover.jpg',available:false}}:{};
    state.manualOverrides||={items:{}};state.manualOverrides.items||={};state.manualOverrides.items[oldKey]=structuredClone(choices);
    const owned=state.library.games.find(g=>normalizedUiPath(g.primaryExecutable)===oldKey);if(!owned)throw Error('Primary transfer fixture has no owned game');
    owned.steamNative=false;owned.nativeSteamAppId=0;owned.contentType='game';for(const item of state.steamPlan.items||[])if(normalizedUiPath(item.primaryExecutable)===oldKey){item.steamNative=false;item.nativeSteamAppId=0;if(item.status==='steam-native')item.status='ready-to-add';}
    snapshot=structuredClone(state);closeEdit();openEdit(mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===oldKey));
    const accepted=[];const actual=invoke;invoke=async(command,args={})=>{
     if(command!=='saveEditor')return actual(command,args);
     accepted.push(structuredClone(args));
     for(const operation of args.operations){const a=operation.arguments,exe=normalizedUiPath(a.executable||'');
      if(operation.command==='setPrimary'){state.library.games.find(g=>normalizedUiPath(g.gameDirectory)===normalizedUiPath(a.gameDirectory)).primaryExecutable=a.executable;continue;}
      if(!exe)continue;const item=state.manualOverrides.items[exe]||={};
      if(operation.command==='saveManualName'){if(a.clearName)delete item.name;else item.name=a.name;}
      if(operation.command==='clearManualSteamId'){delete item.steamId;delete item.igdbId;item.idCleared=true;}
      if(operation.command==='identifyOne'){delete item.idCleared;if(a.steamId){item.steamId=Number(a.steamId);delete item.igdbId;}else if(a.igdbId){item.igdbId=Number(a.igdbId);delete item.steamId;}item.identityStatus='pending-network';item.retryable=true;}
      if(operation.command==='saveArtworkOverride'){item.artworkProtection||={};for(const type of ['cover','long','wallpaper'])if(Object.hasOwn(a,type+'Path')||a['clear'+type[0].toUpperCase()+type.slice(1)]){if(a['clear'+type[0].toUpperCase()+type.slice(1)]){delete item[type];item.artworkProtection[type]='deleted';}else{item[type]={path:a[type+'Path'],portableFile:a[type+'Path'],source:'manual',url:type==='wallpaper'?'/wallpaper.jpg':'/cover.jpg'};item.artworkProtection[type]='manual';}}}
     }
     return structuredClone(state);
    };
    const selector=document.getElementById('edit-primary-select');selector.add(new Option('Selected alternate',next));selector.value=next;selector.dispatchEvent(new Event('change',{bubbles:true}));
    if(mode==='edited-artwork'){artworkDraft.cover={state:'set',asset:{path:'G:/NewChoice/cover.jpg',portableFile:'G:/OldCache/cover.jpg',url:'/cover.jpg'}};artworkDraft.dirty.cover=true;}
    const saved=await persistEditDraft(),operations=accepted[0]?.operations||[],find=command=>operations.find(o=>o.command===command)?.arguments,stored=structuredClone(state.manualOverrides.items[normalizedUiPath(next)]||{});
    closeEdit();openEdit(mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(next)));
    const primaryFirst=operations[0]?.command==='setPrimary'&&operations.filter(o=>o.arguments?.executable).every(o=>normalizedUiPath(o.arguments.executable)===normalizedUiPath(next));
    let preserved=false;
    if(mode==='manual-content')preserved=stored.name===choices.name&&stored.steamId===620&&stored.identityStatus==='pending-network'&&find('identifyOne')?.steamId==='620'&&stored.cover?.path==='G:/RetainedMedia/cover.jpg'&&stored.wallpaper?.path==='G:/RetainedMedia/wallpaper.jpg'&&stored.artworkProtection?.long==='deleted';
    if(mode==='cleared-identity')preserved=stored.idCleared===true&&!stored.steamId&&Boolean(find('clearManualSteamId'))&&!find('identifyOne');
    if(mode==='automatic-defaults'||mode==='unavailable-artwork')preserved=operations.length===1&&!Object.keys(stored).length;
    if(mode==='edited-artwork')preserved=stored.cover?.path==='G:/NewChoice/cover.jpg'&&find('saveArtworkOverride')?.coverPath==='G:/NewChoice/cover.jpg';
    if(mode==='manual-name-only')preserved=stored.name===choices.name&&!find('identifyOne')&&!find('clearManualSteamId');
    if(mode==='legacy-igdb')preserved=stored.igdbId===72&&!stored.steamId&&find('identifyOne')?.igdbId==='72'&&stored.identityStatus==='pending-network';
    const reopened=structuredClone(selectedActionGame.override||{});if(reopened.artwork&&!Object.keys(reopened.artwork).length)delete reopened.artwork;const reopenMatches=JSON.stringify(reopened)===JSON.stringify(stored);
    return {mode,saved,primaryFirst,preserved,reopenMatches,operations,stored,passed:saved&&primaryFirst&&preserved&&reopenMatches};
   },mode);
   transferRows.push(row);
  }
  saveMetrics.push({case:'primary-manual-selection-transfer',rows:transferRows});
  check(transferRows.every(row=>row.passed),'Primary EXE change lost explicit manual selections or copied automatic defaults');


 });

 await run('unchanged primary selection does not schedule a redundant worker',async()=>{

  const result=await page.evaluate(async()=>{editDraft.primaryDirty=true;const actual=invoke;let submitted;invoke=async(command,args={})=>{if(command==='saveEditor'){submitted=structuredClone(args);return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);}return actual(command,args);};await persistEditDraft();return submitted;});

  check(!(result?.operations||[]).some(o=>o.command==='setPrimary'),'re-selecting the current EXE started another primary worker');

 });

 await run('formal enrichment metadata prefills the editor without becoming a manual name',async()=>{
  const r=await page.evaluate(async()=>{
   closeEdit();const state=__LIBRARY_H5_FIXTURE__.snapshot,game=mergedGames().find(g=>!g.mediaOnly),exe=normalizedUiPath(game.primaryExecutable);
   state.manualOverrides||={items:{}};state.manualOverrides.items[exe]={steamId:570,futureAnnotation:{keep:true}};
   for(const item of state.steamPlan.items)if(normalizedUiPath(item.primaryExecutable)===exe){item.formalName='Official enriched title';item.suggestedName='Older suggested name';}
   snapshot=structuredClone(state);openEdit(mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===exe));
   const actual=invoke,calls=[];invoke=async(command,args={},timeoutMs=0)=>{calls.push(command);return actual(command,args,timeoutMs);};
   const field=document.getElementById('identity-name').value,title=gameDisplayName(selectedActionGame),manual=editDraft.game.override?.name,ok=await persistEditDraft();
   closeEdit();state.manualOverrides.items[exe].name='Explicit chosen name';snapshot=structuredClone(state);openEdit(mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===exe));
   return {field,title,manual,ok,calls,chosen:document.getElementById('identity-name').value};
  });saveMetrics.push({case:'formal-enrichment-editor-prefill',r});
  check(r.field==='Official enriched title'&&r.title===r.field,'editor ignored current formal metadata after enrichment');
  check(r.ok&&!r.manual&&!r.calls.some(command=>['saveEditor','saveManualName','identifyOne'].includes(command)),'automatic metadata prefill was silently persisted as a manual name');
  check(r.chosen==='Explicit chosen name','formal metadata replaced an explicit user name');
 });
 await run('untouched editor save has no config write or snapshot IPC',async()=>{

  const result=await page.evaluate(async()=>{const original=JSON.stringify(editDraft.game.override);const actual=invoke;const calls=[];invoke=async(command,args={})=>{calls.push({command,args:structuredClone(args)});return actual(command,args);};const ok=await persistEditDraft();return {ok,original,after:JSON.stringify(editDraft.game.override),calls};});

  saveMetrics.push({case:'untouched',nativeCalls:result.calls.map(call=>({command:call.command,operations:call.args?.operations||[]}))});

  check(result.ok&&result.calls.length===0,'unchanged editor still submits a native save or requests a full snapshot');

  check(result.original===result.after,'unchanged editor turned its suggested name or verified AppID into a manual override');

 });

 await run('explicitly accepting the displayed name remains a manual edit',async()=>{

  const result=await page.evaluate(async()=>{const value=document.getElementById('identity-name').value;document.getElementById('identity-name').dispatchEvent(new Event('input',{bubbles:true}));const actual=invoke;let submitted;invoke=async(command,args={})=>{if(command==='saveEditor'){submitted=structuredClone(args);return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);}return actual(command,args);};await persistEditDraft();return {value,submitted};});

  check(result.submitted?.operations.some(o=>o.command==='saveManualName'&&o.arguments.name===result.value),'explicit same-name choice was treated as untouched auto-filled text');

 });

 await run('candidate acceptance with the same displayed name retains explicit user intent',async()=>{

  const result=await page.evaluate(async()=>{const value=document.getElementById('identity-name').value;await identifyCandidate({name:value});const actual=invoke;let submitted;invoke=async(command,args={})=>{if(command==='saveEditor'){submitted=structuredClone(args);return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);}return actual(command,args);};await persistEditDraft();return {value,submitted};});

  check(result.submitted?.operations.some(o=>o.command==='saveManualName'&&o.arguments.name===result.value),'selected same-name candidate was lost');

 });

 await run('already-saved manual name is an unchanged save without another round trip',async()=>{

  const result=await page.evaluate(async()=>{closeEdit();const game=mergedGames().find(g=>!g.mediaOnly);game.override={...(game.override||{}),name:'Saved manual name'};openEdit(game);const actual=invoke;const calls=[];invoke=async(command,args={})=>{calls.push(command);return actual(command,args);};await persistEditDraft();return {calls,name:document.getElementById('identity-name').value};});

  saveMetrics.push({case:'already-manual-name',nativeCalls:result.calls});

  check(result.calls.length===0&&result.name==='Saved manual name','existing manual name caused a redundant snapshot/write');

 });

 async function beginClearNameProbe(){
  await page.evaluate(()=>{
   closeEdit();const state=__LIBRARY_H5_FIXTURE__.snapshot,game=mergedGames().find(g=>!g.mediaOnly),exe=normalizedUiPath(game.primaryExecutable);
   state.manualOverrides||={items:{}};state.manualOverrides.items[exe]={...(state.manualOverrides.items[exe]||{}),name:'Owned saved manual name',steamId:570,futureAnnotation:{keep:true}};
   snapshot=structuredClone(state);openEdit(mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===exe));
   const original=invoke;window.clearNameProbe={exe,calls:[],pending:[],hold:false,fail:false};
   invoke=async(command,args={},timeoutMs=0)=>{
    if(command!=='saveEditor')return original(command,args,timeoutMs);
    clearNameProbe.calls.push(structuredClone(args));
    const commit=()=>{const item=state.manualOverrides.items[exe];for(const operation of args.operations){if(operation.command==='saveManualName'){if(operation.arguments.clearName===true)delete item.name;else item.name=operation.arguments.name;}}return structuredClone(state);};
    return new Promise((resolve,reject)=>{const finish=()=>clearNameProbe.fail?reject(Error('owned clear write failed')):resolve(commit());if(clearNameProbe.hold)clearNameProbe.pending.push(finish);else finish();});
   };
   clearNameProbe.input=value=>{const field=document.getElementById('identity-name');field.value=value;field.dispatchEvent(new Event('input',{bubbles:true}));};
   clearNameProbe.input('');
  });
 }
 await run('clearing an existing manual name submits one explicit clear and restores automatic naming',async()=>{
  await beginClearNameProbe();const r=await page.evaluate(async()=>{const p=clearNameProbe,ok=await persistEditDraft(),operations=p.calls[0]?.operations,item=__LIBRARY_H5_FIXTURE__.snapshot.manualOverrides.items[p.exe],first=structuredClone(item),field=document.getElementById('identity-name').value;const again=await persistEditDraft();return {ok,again,operations,item:first,field,calls:p.calls.length,dirty:editDraft.identityDirty};});saveMetrics.push({case:'clear-existing-manual-name',r});
  check(r.ok&&r.operations?.length===1&&r.operations[0].command==='saveManualName'&&r.operations[0].arguments.clearName===true&&r.operations[0].arguments.name==='','emptying saved manual name did not submit its explicit clear');
  check(!('name' in r.item)&&r.item.steamId===570&&r.item.futureAnnotation.keep===true&&r.field&&r.field!=='Owned saved manual name','manual clear did not restore automatic name or changed other overrides');
  check(r.again&&r.calls===1&&!r.dirty,'unchanged save after clear replayed the committed operation');
 });
 await run('accepted manual name clear survives Back and reopening without replaying the save',async()=>{
  await beginClearNameProbe();const r=await page.evaluate(async()=>{const p=clearNameProbe;p.hold=true;const saved=persistEditDraft();const game=structuredClone(editDraft.original);routeWorkspaceAction('back');p.pending.shift()();const ok=await saved;openEdit(mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===p.exe)||game);return {ok,name:document.getElementById('identity-name').value,original:editDraft.original.override?.name,stored:__LIBRARY_H5_FIXTURE__.snapshot.manualOverrides.items[p.exe].name,calls:p.calls.length,task:editorSaveTasks.get(keyFor(selectedActionGame))?.phase};});saveMetrics.push({case:'clear-name-back-reopen',r});
  check(r.ok&&r.calls===1&&!r.stored&&!r.original&&r.name!=='Owned saved manual name'&&!r.task,'accepted name clear was discarded by Back or reopened with the old name');
 });
 await run('a later name edit survives an older clear receipt and saves only the new value',async()=>{
  await beginClearNameProbe();const r=await page.evaluate(async()=>{const p=clearNameProbe;p.hold=true;const first=persistEditDraft();p.input('Later unsaved name');p.pending.shift()();const ok=await first,kept=document.getElementById('identity-name').value==='Later unsaved name',baseline=editDraft.original.override?.name;p.hold=false;const again=await persistEditDraft();return {ok,again,kept,baseline,calls:p.calls,stored:__LIBRARY_H5_FIXTURE__.snapshot.manualOverrides.items[p.exe].name};});saveMetrics.push({case:'clear-name-later-edit',r});
  check(r.ok&&r.kept&&!r.baseline,'old clear receipt replaced a later unsaved name or retained the cleared baseline');
  check(r.again&&r.calls.length===2&&r.calls[1].operations.length===1&&r.calls[1].operations[0].arguments.name==='Later unsaved name'&&!r.calls[1].operations[0].arguments.clearName&&r.stored==='Later unsaved name','later name save replayed clear or lost its new value');
 });
 await run('failed manual name clear retains the intent until an explicit retry',async()=>{
  await beginClearNameProbe();const r=await page.evaluate(async()=>{const p=clearNameProbe;p.fail=true;const ok=await persistEditDraft(),failed=editorSaveTasks.get(keyFor(selectedActionGame))?.phase,original=__LIBRARY_H5_FIXTURE__.snapshot.manualOverrides.items[p.exe].name;p.fail=false;const again=await persistEditDraft();return {ok,again,failed,original,calls:p.calls.length,stored:__LIBRARY_H5_FIXTURE__.snapshot.manualOverrides.items[p.exe].name,task:editorSaveTasks.get(keyFor(selectedActionGame))?.phase};});saveMetrics.push({case:'clear-name-explicit-retry',r});
  check(!r.ok&&r.failed==='failed'&&r.original==='Owned saved manual name','failed clear was acknowledged as success or erased the original name');
  check(r.again&&r.calls===2&&!r.stored&&!r.task,'explicit clear retry did not settle the original intent');
 });
 await run('edits during a save use the committed baseline and do not replay saved fields',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;window.rebaseCalls=[];

   const commit=args=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;state.manualOverrides||={items:{}};for(const op of args.operations){if(op.command==='saveManualName'){state.manualOverrides.items[normalizedUiPath(op.arguments.executable)]={...(state.manualOverrides.items[normalizedUiPath(op.arguments.executable)]||{}),name:op.arguments.name};}if(op.command==='setBucket'){state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(op.arguments.gameDirectory)]={bucket:op.arguments.bucket};}}return structuredClone(state);};

   invoke=async(command,args={})=>{if(command!=='saveEditor')return actual(command,args);rebaseCalls.push(structuredClone(args));if(rebaseCalls.length===1)return new Promise(resolve=>window.finishRebase=()=>resolve(commit(args)));return commit(args);};

   const name=document.getElementById('identity-name');name.value='First saved name';name.dispatchEvent(new Event('input'));editDraft.bucket='non-game';const first=persistEditDraft();name.value='Newer unsaved name';name.dispatchEvent(new Event('input'));finishRebase();await first;

   const kept=name.value==='Newer unsaved name'&&editDraft.game.override.name==='Newer unsaved name';const baseline=editDraft.original.override?.name;await persistEditDraft();return {kept,baseline,calls:rebaseCalls};

  });

  saveMetrics.push({case:'edit-during-save',committedOriginalName:result.baseline,operations:result.calls.map(call=>call.operations.map(op=>({command:op.command,...op.arguments})))});

  check(result.kept,'successful old save replaced a newer local name edit');

  check(result.baseline==='First saved name','successful save did not advance the draft original');

  check(result.calls.length===2&&result.calls[1].operations.length===1&&result.calls[1].operations[0].command==='saveManualName'&&result.calls[1].operations[0].arguments.name==='Newer unsaved name','second save replayed the already committed bucket or lost the newer change');

 });

 await run('a failed save retains user changes and can be explicitly retried',async()=>{

  const result=await page.evaluate(async()=>{const actual=invoke;const calls=[];let failures=1;invoke=async(command,args={})=>{if(command!=='saveEditor')return actual(command,args);calls.push(structuredClone(args));if(failures-- >0)throw Error('fixture-save-denied');return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);};const name=document.getElementById('identity-name');name.value='Retry this name';name.dispatchEvent(new Event('input'));const first=await persistEditDraft(),kept=editDraft.game.override.name===name.value;const second=await persistEditDraft();return {first,second,kept,calls,busy:editSaveInFlight};});

  check(!result.first&&result.second&&result.kept&&!result.busy&&result.calls.length===2,'failure discarded the draft, blocked its retry or retained the busy owner');

  check(JSON.stringify(result.calls[0].operations)===JSON.stringify(result.calls[1].operations),'explicit retry changed its captured operations after failure');

 });

 await run('late editor save cannot roll back a newer wallpaper preference',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;const before=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);let finish;

   invoke=async(command,args={})=>{if(command==='saveEditor')return new Promise(resolve=>finish=()=>resolve(before));return actual(command,args);};

   editDraft.bucket='non-game';const pendingSave=persistEditDraft();const originalRevision=selectedActionGame.gameMedia.bindingRevision;

   await updateSharedWallpaper('mode',{mode:'static'});const newer=structuredClone(selectedActionGame.gameMedia);finish();await pendingSave;

   return {originalRevision,newer,current:selectedActionGame.gameMedia,stored:snapshot.gameMedia.entries[newer.key]};

  });

  check(result.newer.bindingRevision>result.originalRevision&&result.current.bindingRevision===result.newer.bindingRevision&&result.current.wallpaperMode==='static','old editor save replaced a newer displayed wallpaper preference');

  check(result.stored.bindingRevision===result.newer.bindingRevision,'old editor snapshot replaced the newer local media entry');

 });

 await run('saved artwork flags clear while later name edits remain pending',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,calls=[];let finish;const commit=args=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;state.manualOverrides||={items:{}};for(const op of args.operations){if(op.arguments.executable){const key=normalizedUiPath(op.arguments.executable);state.manualOverrides.items[key]||={};const override=state.manualOverrides.items[key];if(op.command==='saveArtworkOverride'){delete override.cover;override.artworkProtection={cover:'deleted'};}if(op.command==='saveManualName')override.name=op.arguments.name;}}return structuredClone(state);};

   invoke=async(command,args={})=>{if(command!=='saveEditor')return actual(command,args);calls.push(structuredClone(args));if(calls.length===1)return new Promise(resolve=>finish=()=>resolve(commit(args)));return commit(args);};

   artworkDraft.cover={state:'deleted',asset:null};artworkDraft.dirty.cover=true;const first=persistEditDraft();const name=document.getElementById('identity-name');name.value='After artwork';name.dispatchEvent(new Event('input'));finish();await first;const cleared=!editDraft.artworkDirty&&!editDraft.artworkChanged.cover;await persistEditDraft();return {cleared,calls};

  });

  check(result.cleared,'committed artwork retained its dirty flag after a later name edit');

  check(result.calls[1].operations.length===1&&result.calls[1].operations[0].command==='saveManualName','next save replayed already committed artwork');

 });

 await run('a primary change made during a save uses the latest committed baseline',async()=>{

  const result=await page.evaluate(async()=>{const actual=invoke,calls=[];let finish;invoke=async(command,args={})=>{if(command!=='saveEditor')return actual(command,args);calls.push(structuredClone(args));if(calls.length===1)return new Promise(resolve=>finish=()=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;state.config.manualBuckets||={};state.config.manualBuckets[normalizedUiPath(selectedActionGame.gameDirectory)]={bucket:'non-game'};resolve(structuredClone(state));});return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);};

   editDraft.bucket='non-game';const old=selectedActionGame.primaryExecutable,next=old.replace(/[^\\/]+$/,'LaterPrimary.exe');const first=persistEditDraft();selectedActionGame.primaryExecutable=next;editDraft.primaryDirty=true;finish();await first;const kept=selectedActionGame.primaryExecutable===next;await persistEditDraft();return {kept,old,next,calls};});

  check(result.kept&&result.calls[1].operations.length===1&&result.calls[1].operations[0].command==='setPrimary','later primary choice was lost or saved bucket was replayed');

  check(normalizedForTest(result.calls[1].operations[0].arguments.previousExecutable)===normalizedForTest(result.old),'later primary used a stale pre-save baseline');

 });

 await run('a later Steam ID edit remains pending after a name save',async()=>{

  const result=await page.evaluate(async()=>{const actual=invoke,calls=[];let finish;invoke=async(command,args={})=>{if(command!=='saveEditor')return actual(command,args);calls.push(structuredClone(args));if(calls.length===1)return new Promise(resolve=>finish=()=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;const name=args.operations.find(o=>o.command==='saveManualName');state.manualOverrides||={items:{}};state.manualOverrides.items[normalizedUiPath(name.arguments.executable)]={name:name.arguments.name};resolve(structuredClone(state));});return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);};

   document.getElementById('identity-name').value='Saved before ID';document.getElementById('identity-name').dispatchEvent(new Event('input'));const first=persistEditDraft();const id=document.getElementById('identity-steam-id');id.value='620';id.dispatchEvent(new Event('input'));finish();await first;const kept=id.value==='620'&&editDraft.game.override.steamId===620;await persistEditDraft();return {kept,calls};});

  check(result.kept&&result.calls[1].operations.length===1&&result.calls[1].operations[0].command==='identifyOne'&&result.calls[1].operations[0].arguments.steamId==='620','later Steam ID was lost or the saved name was replayed');
  const boundaries=await page.evaluate(async()=>{
    const base=structuredClone(mergedGames().find(game=>game.primaryExecutable&&!game.mediaOnly));if(!base)throw Error('Missing real game for AppID boundary');const actualInvoke=invoke,calls=[];
    invoke=async(command,args={})=>{if(command==='saveEditor'||command==='searchIdentity'){calls.push({command,args:structuredClone(args)});if(command==='searchIdentity')throw Error('Unexpected invalid ID search');return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);}return actualInvoke(command,args);};
    const rows=[];
    for(const input of ['1e3','0x26c','620.5','-620','0','00','2147483648','9007199254740993','18446744073709551615']){
      if(editDraft)closeEdit();openEdit(structuredClone(base));const name=document.getElementById('identity-name'),id=document.getElementById('identity-steam-id');name.value='Must not save invalid ID '+input;name.dispatchEvent(new Event('input'));id.readOnly=false;id.value=input;id.dispatchEvent(new Event('input'));const before=calls.length,saved=await persistEditDraft();await searchGame();rows.push({input,saved,dispatched:calls.length-before,text:id.value,preserved:!!editDraft&&editDraft.game.override.name===name.value});
    }
    const controls=[];for(const input of ['1','620','2147483647']){if(editDraft)closeEdit();openEdit(structuredClone(base));const id=document.getElementById('identity-steam-id');id.value=input;id.dispatchEvent(new Event('input'));const before=calls.length,saved=await persistEditDraft();const identify=calls.slice(before).flatMap(call=>call.args.operations||[]).find(operation=>operation.command==='identifyOne');controls.push({input,saved,submitted:identify?.arguments.steamId});}
    return {rows,controls};
  });saveMetrics.push({case:'manual-id-input-boundaries',...boundaries});
  check(boundaries.rows.every(row=>row.saved===false&&row.dispatched===0&&row.text===row.input&&row.preserved),'invalid AppID text was coerced or dispatched by save/search');
  check(boundaries.controls.every(row=>row.saved===true&&row.submitted===row.input),'valid Steam AppID boundary was rejected or changed');


 });

 await run('save reply for one card preserves newer media saved on another card',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,older=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);let finish;invoke=async(command,args={})=>{if(command==='saveEditor')return new Promise(resolve=>finish=()=>resolve(older));return actual(command,args);};

   editDraft.bucket='non-game';const saving=persistEditDraft();reverseOpenOther();await updateSharedWallpaper('mode',{mode:'static'});const newer=structuredClone(selectedActionGame.gameMedia),owner=keyFor(selectedActionGame);finish();await saving;

   return {newer,ownerKept:keyFor(selectedActionGame)===owner,entry:snapshot.gameMedia.entries[newer.key],displayed:selectedActionGame.gameMedia};

  });

  check(result.ownerKept&&result.entry.bindingRevision===result.newer.bindingRevision&&result.entry.wallpaperMode==='static'&&result.displayed.bindingRevision===result.newer.bindingRevision,'old card save rolled back a newer entry on the other card');

 });

 await run('save media reconciliation keeps root, deletion and fresh revision boundaries',async()=>{

  const result=await page.evaluate(()=>{

   const old=structuredClone(snapshot),key=selectedActionGame.gameMedia.key;old.gameMedia.entries[key].bindingRevision=9;snapshot=old;

   const incoming=structuredClone(old);incoming.gameMedia.entries[key].bindingRevision=8;const original=JSON.stringify(incoming),kept=preserveNewerEditorMedia(incoming);

   const elsewhere={...incoming,dataRoot:String(incoming.dataRoot||'')+'-other'},rootKept=preserveNewerEditorMedia(elsewhere)===elsewhere;

   const removed=structuredClone(incoming);delete removed.gameMedia.entries[key];const absent=preserveNewerEditorMedia(removed);

   const fresh=structuredClone(old);fresh.gameMedia.entries[key].bindingRevision=10;const unchanged=preserveNewerEditorMedia(fresh)===fresh;

   return {newer:kept.gameMedia.entries[key].bindingRevision===9,inputUnchanged:JSON.stringify(incoming)===original,rootKept,absent:!absent.gameMedia.entries[key],unchanged};

  });

  check(result.newer&&result.inputUnchanged,'media reconciliation modified the incoming response or lost the newer revision');

  check(result.rootKept&&result.absent&&result.unchanged,'media reconciliation mixed data roots, revived a removed entry or copied an already fresh response');

 });



 await run('large historical scrape progress indexes each source once per synchronous update',async()=>{

  const rows=await page.evaluate(()=>{

   const actual=normalizedUiPath,rows=[];let work=0;normalizedUiPath=value=>{++work;return actual(value);};

   try{for(const count of [160,320]){reverseLargeLibrary(count);work=0;const t=performance.now();syncScrapeCardStates();rows.push({games:count,pathNormalizations:work,wallMs:performance.now()-t,cards:libraryCards().length,queued:libraryCards().every(card=>card.dataset.scrapeStatus==='queued')});}}

   finally{normalizedUiPath=actual;}return rows;

  });progressMetrics.push({case:'historical-progress',rows});

  check(rows.every(row=>row.cards===row.games&&row.queued),'historical progress lost card or status ownership');

  check(rows[1].pathNormalizations<=rows[0].pathNormalizations*2.6&&rows[1].pathNormalizations<320*8,'historical progress lookup is quadratic');

 });

 await run('unchanged card progress produces no DOM writes and terminal retry restores its spinner',async()=>{

  const result=await page.evaluate(()=>{

   reverseLargeLibrary(160);syncScrapeCardStates();const observer=new MutationObserver(()=>{});observer.observe(library,{attributes:true,childList:true,characterData:true,subtree:true});

   for(let i=0;i<4;i++)syncScrapeCardStates();const mutations=observer.takeRecords().length;observer.disconnect();

   const card=libraryCards()[0],key=card.dataset.directory;steamCommitTasks.set(key,{phase:'failed',label:'加入失败：offline'});syncScrapeCardStates();const failed=card.querySelector('.game-card-scrape-label').textContent==='加入失败：offline'&&card.querySelector('.spinner').style.display==='none';

   steamCommitTasks.set(key,{phase:'running',label:'正在加入 Steam'});syncScrapeCardStates();const running=card.classList.contains('scrape-loading')&&card.querySelector('.spinner').style.display===''&&card.getAttribute('aria-busy')==='true';

   steamCommitTasks.delete(key);syncScrapeCardStates();return {mutations,failed,running,recovered:card.dataset.scrapeStatus==='queued'};

  });progressMetrics.push({case:'unchanged-progress',...result});

  check(result.mutations===0,'unchanged progress rewrote card attributes/text/styles');

  check(result.failed&&result.running&&result.recovered,'failed/retry/removed operation state left a stale label or hidden spinner');

 });

 await run('a foreground event burst paints the latest cached statuses in one frame without replacing focus',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(320);document.hasFocus=()=>true;libraryCards()[4].focus();const focused=document.activeElement;

   const actual=syncScrapeCardStates;let paints=0;syncScrapeCardStates=()=>{paints++;return actual();};

   try{

    for(let i=0;i<40;i++){const items={...snapshot.scrape.items},first=Object.keys(items)[0];items[first]={status:i===39?'failed':'loading'};reverseEmit({event:'scrape-progress',scrape:{mode:'automatic',items}});}

    const immediate=paints;await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

    return {immediate,paints,cards:libraryCards().length,focusKept:document.activeElement===focused,status:libraryCards()[0].dataset.scrapeStatus};

   }finally{syncScrapeCardStates=actual;}

  });progressMetrics.push({case:'foreground-burst',...result});

  check(result.immediate===0&&result.paints===1,'foreground progress burst did not coalesce into one paint');

  check(result.cards===320&&result.focusKept&&result.status==='failed','burst changed the card tree/focus or lost the latest progress');

 });

 await run('large offline background progress is data only and focus paints the latest queue state once',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(320);document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));

   let deliveredMutations=0;const observer=new MutationObserver(records=>{deliveredMutations+=records.length;});observer.observe(library,{attributes:true,childList:true,characterData:true,subtree:true});

   const actual=syncScrapeCardStates;let paints=0;syncScrapeCardStates=()=>{paints++;return actual();};

   try{

    const commands=reverseIO.calls.length,directory=snapshot.library.games[0].gameDirectory.toUpperCase()+'/';

    for(let i=0;i<40;i++){reverseEmit({event:'scrape-progress',scrape:{mode:'automatic',reason:'offline',items:snapshot.scrape.items}});reverseEmit({event:'steam-commit-progress',directories:[directory],phase:i===39?'failed':'running',error:'offline'});}

    await new Promise(requestAnimationFrame);const background={paints,mutations:deliveredMutations+observer.takeRecords().length,nativeCalls:reverseIO.calls.length-commands,dataKept:snapshot.scrape.reason==='offline'&&steamCommitTasks.get(keyFor(snapshot.library.games[0]))?.phase==='failed'};observer.disconnect();

    document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

    return {background,paints,label:libraryCards()[0].querySelector('.game-card-scrape-label').textContent,cards:libraryCards().length};

   }finally{observer.disconnect();syncScrapeCardStates=actual;}

  });progressMetrics.push({case:'background-offline-burst',...result});

  check(result.background.paints===0&&result.background.mutations===0&&result.background.nativeCalls===0,'background progress rendered cards or started native work');

  check(result.background.dataKept&&result.paints===1&&result.label==='加入失败：offline'&&result.cards===320,'focus did not publish exactly the latest canonical queue state');

 });

 await run('page teardown cancels a queued progress frame and pageshow resumes only the latest data',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(160);document.hasFocus=()=>true;const actual=syncScrapeCardStates;let paints=0;syncScrapeCardStates=()=>{paints++;return actual();};

   try{

    const first=Object.keys(snapshot.scrape.items)[0];reverseEmit({event:'scrape-progress',scrape:{mode:'automatic',items:{...snapshot.scrape.items,[first]:{status:'loading'}}}});window.dispatchEvent(new PageTransitionEvent('pagehide'));

    reverseEmit({event:'scrape-progress',scrape:{mode:'automatic',items:{...snapshot.scrape.items,[first]:{status:'failed'}}}});await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const hiddenPaints=paints;

    window.dispatchEvent(new PageTransitionEvent('pageshow'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

    return {hiddenPaints,paints,status:libraryCards()[0].dataset.scrapeStatus};

   }finally{syncScrapeCardStates=actual;}

  });progressMetrics.push({case:'progress-lifecycle',...result});

  check(result.hiddenPaints===0&&result.paints===1&&result.status==='failed','retired page frame painted or pageshow lost the latest progress');

 });



 await run('large offline edit and Steam queues survive Back and one failure without crossing card ownership',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(320);document.hasFocus=()=>true;const actual=invoke,requests=[],promises=[];

   invoke=async(command,args={})=>['saveEditor','commit'].includes(command)?new Promise((resolve,reject)=>requests.push({command,args:structuredClone(args),resolve,reject})):actual(command,args);

   try{

    const games=mergedGames(),a=games[0],b=games[1];openEdit(a);editDraft.bucket='non-game';promises.push(persistEditDraft(),joinEditedGameToSteam());

    openEdit(b);document.getElementById('identity-name').value='Second saved name';syncIdentityDraft('name');promises.push(persistEditDraft(),joinEditedGameToSteam());closeEdit();

    document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));let mutations=0;const watched=new MutationObserver(records=>{mutations+=records.length;});watched.observe(library,{attributes:true,childList:true,subtree:true});

    const state=__LIBRARY_H5_FIXTURE__.snapshot;

    for(let i=0;i<40;i++)reverseEmit({event:'scrape-progress',scrape:{...snapshot.scrape,reason:'offline'}});

    state.config.manualBuckets={[keyFor(a)]:{bucket:'non-game'}};requests[0].resolve(structuredClone(state));await promises[0];

    reverseEmit({event:'steam-commit-progress',directories:[a.gameDirectory.toUpperCase()+'/'],phase:'failed',error:'offline'});requests[1].reject(new Error('offline'));await promises[1];

    state.manualOverrides.items[normalizedUiPath(b.primaryExecutable)]={name:'Second saved name'};requests[2].resolve(structuredClone(state));await promises[2];

    state.steamPlan.items[1].status='added-to-steam';reverseEmit({event:'steam-commit-progress',directories:[b.gameDirectory.toUpperCase()+'/'],phase:'completed'});requests[3].resolve(structuredClone(state));const settled=await Promise.all(promises);

    await new Promise(requestAnimationFrame);mutations+=watched.takeRecords().length;watched.disconnect();

    const background={mutations,editorClosed:document.getElementById('edit-modal').classList.contains('hidden'),aPhase:steamCommitTasks.get(keyFor(a))?.phase,bPhase:steamCommitTasks.get(keyFor(b))?.phase,remainingSaves:editorSaveTasks.size};

    document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

    return {requests:requests.map(({command,args})=>({command,args})),settled,background,aKey:keyFor(a),bKey:keyFor(b),savedBucket:manualBucket(mergedGames().find(game=>keyFor(game)===keyFor(a))),savedName:mergedGames().find(game=>keyFor(game)===keyFor(b)).override.name,bJoined:category(mergedGames().find(game=>keyFor(game)===keyFor(b)))==='in-steam',allClosed:document.getElementById('edit-modal').classList.contains('hidden')&&document.getElementById('busy').classList.contains('hidden')};

   }finally{invoke=actual;}

  });progressMetrics.push({case:'large-offline-edit-join-queue',...result});

  check(result.requests.map(row=>row.command).join(',')==='saveEditor,commit,saveEditor,commit'&&result.requests[1].args.selectedGameDirectories.join()===result.aKey&&result.requests[3].args.selectedGameDirectories.join()===result.bKey,'rapid submissions changed FIFO order or single-card Steam ownership');

  check(JSON.stringify(result.settled)==='[true,false,true,true]'&&result.background.remainingSaves===0&&result.background.aPhase==='failed'&&result.background.bPhase==='completed','failed first join blocked or replaced later accepted work');

  check(result.background.mutations===0&&result.background.editorClosed&&result.allClosed&&result.savedBucket==='non-game'&&result.savedName==='Second saved name'&&result.bJoined,'background completion redrew cards, reopened an editor, or lost committed values');

 });



 await run('a delayed return frame cannot pull focus back after the user selects another card',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(160);document.hasFocus=()=>true;const games=mergedGames();openEdit(games[0]);closeEdit();libraryCards()[5].focus();const moved=document.activeElement,key=keyFor(games[5]);

   await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

   return {focusKept:document.activeElement===moved,current:currentCardDirectory,key};

  });

  check(result.focusKept&&result.current===result.key,'old return frame stole the newer card focus');

 });

 await run('a delayed editor return is retired by page teardown and cannot paint after restoration',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(160);document.hasFocus=()=>true;openEdit(mergedGames()[0]);closeEdit();window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));

   libraryCards()[6].focus();const moved=document.activeElement;

   await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);return document.activeElement===moved;

  });

  check(result,'retired editor return frame changed restored-page focus');

 });

 await run('progress aliases retain direct and first-match precedence without stale indexes',async()=>{

  const result=await page.evaluate(()=>{

   reverseLargeLibrary(160);const game=snapshot.library.games[0],key=normalizedUiPath(game.primaryExecutable),alias=game.primaryExecutable.toUpperCase()+'/';

   snapshot.scrape.items={[alias]:{status:'queued'},[key]:{status:'failed'}};syncScrapeCardStates();const direct=libraryCards()[0].dataset.scrapeStatus;

   delete snapshot.scrape.items[key];snapshot.scrape.items[alias]={status:'loading'};syncScrapeCardStates();const changed=libraryCards()[0].dataset.scrapeStatus;

   snapshot.scrape.items={[alias]:null,[key.toUpperCase()]:{status:'failed'}};syncScrapeCardStates();return {direct,changed,firstNull:libraryCards()[0].dataset.scrapeStatus};

  });

  check(result.direct==='failed'&&result.changed==='loading'&&result.firstNull==='','progress index changed direct/first-alias precedence or retained a prior source view');

 });



 await run('cached glow and pending card spinners stop in background and resume without changing library state',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(80);snapshot.scrape={items:{}};syncScrapeCardStates();window.__activityFocused=true;document.hasFocus=()=>__activityFocused;window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();

   for(const game of mergedGames().slice(0,3))steamCommitTasks.set(keyFor(game),{phase:'running',label:'正在加入 Steam'});refreshCardOperationState();

   await new Promise(requestAnimationFrame);const animations=document.getAnimations().filter(animation=>typeof animation.animationName==='string'),cached=JSON.stringify(snapshot.gameMedia),key=currentCardDirectory;

   const runningBefore=animations.filter(animation=>animation.playState==='running').length;

   __activityFocused=false;window.dispatchEvent(new Event('blur'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

   const start=animations.map(animation=>Number(animation.currentTime));await new Promise(resolve=>setTimeout(resolve,80));

   const paused=animations.every(animation=>animation.playState==='paused'),advanced=animations.filter((animation,i)=>Number(animation.currentTime)-start[i]>1).length,backgroundClass=document.documentElement.classList.contains('workspace-background');

   __activityFocused=true;window.dispatchEvent(new Event('focus'));const before=animations.map(animation=>Number(animation.currentTime));await new Promise(resolve=>setTimeout(resolve,80));

   return {runningBefore,paused,advanced,backgroundClass,restored:animations.filter((animation,i)=>animation.playState==='running'&&Number(animation.currentTime)-before[i]>1).length,foregroundClass:document.documentElement.classList.contains('workspace-background'),cacheKept:JSON.stringify(snapshot.gameMedia)===cached,focusKept:currentCardDirectory===key,queueKept:[...steamCommitTasks.values()].every(task=>task.phase==='running')};

  });progressMetrics.push({case:'cached-background-animation',...result});

  check(result.runningBefore===4&&result.backgroundClass&&result.paused&&result.advanced===0,'real cached glow/spinner CSS animations kept advancing in background');

  check(result.restored===4&&!result.foregroundClass&&result.cacheKept&&result.focusKept&&result.queueKept,'focus did not restore animation without changing cache, queue or current card');

 });

 await run('hidden and retired page states dominate late focus when controlling animations',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(80);snapshot.scrape={items:{}};syncScrapeCardStates();window.__activityFocused=true;window.__activityHidden=false;document.hasFocus=()=>__activityFocused;Object.defineProperty(document,'hidden',{configurable:true,get:()=>__activityHidden});window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();steamCommitTasks.set(keyFor(mergedGames()[0]),{phase:'running',label:'正在加入 Steam'});refreshCardOperationState();await new Promise(requestAnimationFrame);

   const loops=()=>document.getAnimations().filter(animation=>typeof animation.animationName==='string');const paused=()=>loops().length===2&&loops().every(animation=>animation.playState==='paused');

   __activityHidden=true;document.dispatchEvent(new Event('visibilitychange'));const hidden=paused();window.dispatchEvent(new Event('focus'));const hiddenFocus=paused();

   __activityFocused=false;__activityHidden=false;document.dispatchEvent(new Event('visibilitychange'));const visibleUnfocused=paused();

   __activityFocused=true;window.dispatchEvent(new Event('focus'));const foreground=loops().every(animation=>animation.playState==='running');

   window.dispatchEvent(new PageTransitionEvent('pagehide'));const retired=paused();window.dispatchEvent(new Event('focus'));const retiredFocus=paused();

   window.dispatchEvent(new PageTransitionEvent('pageshow'));return {hidden,hiddenFocus,visibleUnfocused,foreground,retired,retiredFocus,restored:loops().every(animation=>animation.playState==='running')};

  });progressMetrics.push({case:'animation-lifecycle-policy',...result});

  check(result.hidden&&result.hiddenFocus&&result.visibleUnfocused&&result.retired&&result.retiredFocus,'late focus or visibility transition revived inactive-page animation');

  check(result.foreground&&result.restored,'valid focus/pageshow did not resume animations');

 });



 await run('foreground animation restore keeps the user reduced-motion artwork preference',async()=>{

  await page.emulateMedia({reducedMotion:'reduce'});

  try{

   const result=await page.evaluate(async()=>{

    reverseLargeLibrary(80);snapshot.scrape={items:{}};syncScrapeCardStates();window.__activityFocused=true;document.hasFocus=()=>__activityFocused;window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();

    const glow=document.querySelector('.game-card.current-card .card-art-glow'),read=()=>({name:getComputedStyle(glow).animationName,opacity:getComputedStyle(glow).opacity,transform:getComputedStyle(glow).transform}),before=read();

    __activityFocused=false;window.dispatchEvent(new Event('blur'));__activityFocused=true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);const after=read();

    return {before,after,glowLoops:document.getAnimations().filter(animation=>animation.animationName==='selected-artwork-breathe').length};

   });

   check(result.before.name==='none'&&result.after.name==='none'&&result.before.opacity===result.after.opacity&&result.before.transform===result.after.transform&&result.glowLoops===0,'focus overrode the reduced-motion preference');

  }finally{await page.emulateMedia({reducedMotion:'no-preference'});}

 });

 await run('selected cached artwork keeps its breathing motion with a stable blur filter',async()=>{

  const result=await page.evaluate(async()=>{

   reverseLargeLibrary(80);snapshot.scrape={items:{}};syncScrapeCardStates();document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();await new Promise(requestAnimationFrame);

   const animation=document.getAnimations().find(item=>item.animationName==='selected-artwork-breathe'),target=animation.effect.target,read=()=>({filter:getComputedStyle(target).filter,opacity:getComputedStyle(target).opacity,transform:getComputedStyle(target).transform});

   const before=read();await new Promise(resolve=>setTimeout(resolve,100));const after=read();return {filterFrames:animation.effect.getKeyframes().filter(frame=>Object.hasOwn(frame,'filter')).length,before,after,running:animation.playState==='running'};

  });progressMetrics.push({case:'foreground-cached-glow',...result});

  check(result.filterFrames===0&&result.before.filter===result.after.filter&&result.before.opacity!==result.after.opacity&&result.before.transform!==result.after.transform&&result.running,'selected artwork either lost its breathing motion or still animates an expensive filter');

 });



 await run('identity wait timer stops in background, resumes once and stays stopped after cancellation',async()=>{

  await page.evaluate(()=>{reverseHoldSearch();document.hasFocus=()=>true;window.searchWait=reverseSearch();});

  await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));window.dotsMutations=0;window.dotsObserver=new MutationObserver(records=>dotsMutations+=records.length);dotsObserver.observe(document.getElementById('identity-search-loading-dots'),{childList:true,characterData:true,subtree:true});});

  await page.waitForTimeout(1400);const background=await page.evaluate(()=>({timer:identitySearchEllipsisTimer,mutations:dotsMutations+dotsObserver.takeRecords().length,requests:searchIO.identities.length}));

  await page.evaluate(()=>{dotsObserver.disconnect();document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));dotsMutations=0;dotsObserver.observe(document.getElementById('identity-search-loading-dots'),{childList:true,characterData:true,subtree:true});});await page.waitForTimeout(1400);

  const result=await page.evaluate(async()=>{const resumed={timer:identitySearchEllipsisTimer,mutations:dotsMutations+dotsObserver.takeRecords().length};dotsObserver.disconnect();await cancelIdentitySearch();searchIO.identities[0].resolve({pending:false,candidates:[]});await searchWait;window.dispatchEvent(new Event('focus'));return {resumed,cancelled:identitySearchEllipsisTimer===0,requests:searchIO.identities.length};});

  progressMetrics.push({case:'identity-background-timer',background,...result});

  check(background.timer===0&&background.mutations===0&&background.requests===1,'identity ellipsis kept a timer or DOM loop in background');

  check(result.resumed.timer!==0&&result.resumed.mutations>=2&&result.resumed.mutations<=4&&result.cancelled&&result.requests===1,'identity timer duplicated, restarted a search or survived cancellation');

 });

 await run('an old search rejection cannot stop or replace a newer card search',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();window.oldSearch=reverseSearch('First query');const second=structuredClone(selectedActionGame);second.gameDirectory+='-second';second.primaryExecutable+='-second';openEdit(second);window.newSearch=reverseSearch('Second query');

   searchIO.identities[0].reject(new Error('old provider offline'));await oldSearch;

   const pending={inFlight:identitySearchInFlight,loading:!document.getElementById('identity-search-loading').classList.contains('hidden'),query:document.getElementById('identity-search-query').textContent,body:document.getElementById('identity-search-list').textContent};

   searchIO.identities[1].resolve({pending:false,candidates:[{name:'Fresh second candidate',steamAppId:999}]});await newSearch;return {pending,fresh:document.getElementById('identity-search-list').textContent.includes('Fresh second candidate')};

  });progressMetrics.push({case:'old-search-rejection',...result});

  check(result.pending.inFlight&&result.pending.loading&&result.pending.query==='Second query'&&!result.pending.body.includes('暂未完成'),'old rejected request mutated the newer search');

  check(result.fresh,'newer search could not finish after obsolete rejection');

 });

 await run('cancelled identity work ignores queued start and completion events',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();const waiting=reverseSearch();reverseSearchAck(0,'retired-job',1);await waiting;await cancelIdentitySearch();

   const list=document.getElementById('identity-search-list'),before=list.innerHTML;reverseIdentityEvent('identity-search-started','retired-job',1);const restarted=identitySearchInFlight;

   reverseIdentityEvent('identity-search-complete','retired-job',1,'Owned search',[{name:'Obsolete candidate',steamAppId:100}]);

   return {restarted,inFlight:identitySearchInFlight,jobId:identitySearchJobId,same:list.innerHTML===before,closed:document.getElementById('identity-search-modal').classList.contains('hidden')};

  });

  check(!result.restarted&&!result.inFlight&&!result.jobId&&result.same&&result.closed,'queued event revived or rendered a cancelled search');

 });

 await run('same query restart binds events to its native receipt instead of an old job',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();let waiting=reverseSearch();reverseSearchAck(0,'older-job',1);await waiting;await cancelIdentitySearch();waiting=reverseSearch();reverseSearchAck(1,'current-job',2);await waiting;

   reverseIdentityEvent('identity-search-started','older-job',1);reverseIdentityEvent('identity-search-complete','older-job',1,'Owned search',[{name:'Wrong old candidate',steamAppId:100}]);const pending=identitySearchInFlight;

   reverseIdentityEvent('identity-search-started','current-job',2);reverseIdentityEvent('identity-search-complete','current-job',2,'Owned search',[{name:'Correct current candidate',steamAppId:200}]);

   return {pending,text:document.getElementById('identity-search-list').textContent,inFlight:identitySearchInFlight};

  });

  check(result.pending&&result.text.includes('Correct current candidate')&&!result.text.includes('Wrong old candidate')&&!result.inFlight,'stale job captured a same-query retry');

 });

 await run('pre-receipt identity completion selects only the matching latest generation',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();const waiting=reverseSearch();reverseIdentityEvent('identity-search-started','obsolete-preack',1);reverseIdentityEvent('identity-search-complete','obsolete-preack',1,'Owned search',[{name:'Wrong preack candidate',steamAppId:100}]);

   reverseIdentityEvent('identity-search-started','fast-current',2);reverseIdentityEvent('identity-search-complete','fast-current',2,'Owned search',[{name:'Correct preack candidate',steamAppId:200}]);reverseIdentityEvent('identity-search-started','obsolete-preack',1);reverseIdentityEvent('identity-search-complete','obsolete-preack',1,'Owned search',[{name:'Wrong late preack candidate',steamAppId:100}]);reverseSearchAck(0,'fast-current',2);await waiting;

   return {text:document.getElementById('identity-search-list').textContent,inFlight:identitySearchInFlight,timer:identitySearchEllipsisTimer};

  });

  check(result.text.includes('Correct preack candidate')&&!result.text.includes('Wrong preack candidate')&&!result.inFlight&&result.timer===0,'pre-ack event ordering lost the current native completion');

 });

 await run('background identity completion defers candidate DOM and focus until one foreground resume',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();let waiting=reverseSearch();reverseSearchAck(0,'background-job',1);await waiting;document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const list=document.getElementById('identity-search-list');let mutations=0;const observer=new MutationObserver(records=>mutations+=records.length);observer.observe(list,{childList:true,subtree:true});const focused=document.activeElement;

   reverseIdentityEvent('identity-search-complete','background-job',1,'Owned search',[{name:'Deferred fresh candidate',steamAppId:200}]);await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);

   const background={mutations:mutations+observer.takeRecords().length,focusKept:document.activeElement===focused,timer:identitySearchEllipsisTimer,inFlight:identitySearchInFlight};observer.disconnect();

   document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);return {background,text:list.textContent,requests:searchIO.identities.length};

  });progressMetrics.push({case:'background-search-result',...result});

  check(result.background.mutations===0&&result.background.focusKept&&result.background.timer===0&&!result.background.inFlight,'background result rebuilt candidates, moved focus or kept visual polling');

  check(result.text.includes('Deferred fresh candidate')&&result.requests===1,'foreground did not reuse the completed candidate result');

 });

 await run('page teardown retires pending identity UI without discarding the editor draft',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();const waiting=reverseSearch();reverseSearchAck(0,'page-old-job',1);await waiting;const draft=editDraft;window.dispatchEvent(new PageTransitionEvent('pagehide'));const retired={inFlight:identitySearchInFlight,timer:identitySearchEllipsisTimer};

   reverseIdentityEvent('identity-search-started','page-old-job',1);reverseIdentityEvent('identity-search-complete','page-old-job',1,'Owned search',[{name:'Retired page result',steamAppId:100}]);window.dispatchEvent(new PageTransitionEvent('pageshow'));await Promise.resolve();

   return {retired,draftKept:editDraft===draft,closed:document.getElementById('identity-search-modal').classList.contains('hidden'),staleVisible:document.getElementById('identity-search-list').textContent.includes('Retired page result'),cancels:reverseIO.calls.filter(call=>call.command==='cancelIdentitySearch').map(call=>call.args)};

  });

  check(!result.retired.inFlight&&result.retired.timer===0&&result.draftKept&&result.closed&&!result.staleVisible&&result.cancels.length===1,'page teardown kept an identity owner/timer or accepted its late result');

 });

 await run('cached immediate identity results and explicit manual candidates retain their original flow',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();let waiting=reverseSearch('Cached query');searchIO.identities[0].resolve({pending:false,cached:true,candidates:[{name:'Cached candidate',steamAppId:300}]});await waiting;const cached=document.getElementById('identity-search-list').textContent.includes('Cached candidate')&&!identitySearchInFlight;await cancelIdentitySearch();

   waiting=reverseSearch('Manual candidate', '570');reverseSearchAck(1,'manual-job',2);await waiting;const manual=document.getElementById('identity-search-list').textContent.includes('Manual candidate')&&identitySearchInFlight&&identitySearchEllipsisTimer===0;

   reverseIdentityEvent('identity-search-complete','manual-job',2,'Manual candidate',[{name:'Network candidate',steamAppId:200}]);return {cached,manual,text:document.getElementById('identity-search-list').textContent,writes:reverseIO.calls.filter(call=>['saveEditor','identifyOne','commit'].includes(call.command)).length};

  });

  check(result.cached&&result.manual&&result.text.includes('Manual candidate')&&result.text.includes('Network candidate')&&result.writes===0,'search cache/manual candidates changed or search wrote a selection automatically');

 });

 await run('an idle artwork warmup cannot load previews after its editor is closed',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();reverseWatchWarmups();artworkPrefetchCache.clear();const game=selectedActionGame,type=artworkSearchTypeForSlot('cover'),waiting=ensureArtworkSearch(game,type);searchIO.artworks[0].resolve({candidates:[{previewUrl:'http://127.0.0.1:13139/cover.jpg?retired-warm'}]});await waiting;const scheduled=searchIO.warmCallbacks.length;

   closeEdit();for(const callback of searchIO.warmCallbacks)callback({});return {scheduled,attempts:searchIO.warmSources.length,closed:document.getElementById('edit-modal').classList.contains('hidden')};

  });progressMetrics.push({case:'closed-editor-warmup',...result});

  check(result.scheduled===1&&result.attempts===0&&result.closed,'retired idle callback loaded artwork previews after closing editor');

 });

 await run('background artwork result stays cached and warms once when the matching editor regains focus',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();reverseWatchWarmups();artworkPrefetchCache.clear();const game=selectedActionGame,type=artworkSearchTypeForSlot('cover'),key=artworkSearchCacheKey(game,type),waiting=ensureArtworkSearch(game,type);document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));searchIO.artworks[0].resolve({candidates:[{previewUrl:'http://127.0.0.1:13139/cover.jpg?foreground-warm'}]});await waiting;

   const background={scheduled:searchIO.warmCallbacks.length,attempts:searchIO.warmSources.length,cached:artworkPrefetchCache.get(key)?.result?.candidates.length===1};document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));for(const callback of [...searchIO.warmCallbacks])callback({});const once=searchIO.warmSources.length;window.dispatchEvent(new Event('focus'));

   return {background,once,after:searchIO.warmSources.length,requests:searchIO.artworks.length};

  });progressMetrics.push({case:'background-artwork-cache',...result});

  check(result.background.scheduled===0&&result.background.attempts===0&&result.background.cached&&result.once===1&&result.after===1&&result.requests===1,'background result preloaded images or foreground duplicated the cached provider request');

 });



 await run('queued initial identity focus waits for foreground ownership',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();let calls=0;const button=document.getElementById('identity-search-cancel'),actual=button.focus.bind(button);button.focus=(...args)=>{calls++;return actual(...args);};const waiting=reverseSearch();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const background=calls;

   document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const foreground=calls;searchIO.identities[0].resolve({pending:false,candidates:[]});await waiting;return {background,foreground};

  });

  check(result.background===0&&result.foreground===1,'initial search focus ran in background or was lost on resume');

 });

 await run('an artwork warmup for an obsolete name cannot load into the current editor query',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();reverseWatchWarmups();artworkPrefetchCache.clear();const game=selectedActionGame,type=artworkSearchTypeForSlot('cover'),waiting=ensureArtworkSearch(game,type);searchIO.artworks[0].resolve({candidates:[{previewUrl:'http://127.0.0.1:13139/cover.jpg?old-query'}]});await waiting;

   game.override={...(game.override||{}),name:'Newer editor query'};for(const callback of [...searchIO.warmCallbacks])callback({});const obsolete=searchIO.warmSources.length;

   const current=ensureArtworkSearch(game,type);searchIO.artworks[1].resolve({candidates:[{previewUrl:'http://127.0.0.1:13139/cover.jpg?new-query'}]});await current;searchIO.warmCallbacks.at(-1)({});return {obsolete,sources:searchIO.warmSources,queries:searchIO.artworks.map(request=>request.args.query)};

  });

  check(result.obsolete===0&&result.sources.length===1&&result.sources[0].endsWith('new-query')&&result.queries[0]!==result.queries[1],'obsolete query idle work loaded previews or blocked the current warmup');

 });



 await run('accepting a manual identity candidate retires its pending network job without saving automatically',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();const waiting=reverseSearch('Chosen manual name','570');reverseSearchAck(0,'manual-pending',1);await waiting;await identifyCandidate({name:'Chosen manual name',steamAppId:570});

   reverseIdentityEvent('identity-search-started','manual-pending',1,'Chosen manual name');reverseIdentityEvent('identity-search-complete','manual-pending',1,'Chosen manual name',[{name:'Late provider name',steamAppId:200}]);

   return {inFlight:identitySearchInFlight,timer:identitySearchEllipsisTimer,closed:document.getElementById('identity-search-modal').classList.contains('hidden'),name:editDraft.game.override.name,id:editDraft.game.override.steamId,cancels:reverseIO.calls.filter(call=>call.command==='cancelIdentitySearch').length,writes:reverseIO.calls.filter(call=>['saveEditor','identifyOne','commit'].includes(call.command)).length};

  });

  check(!result.inFlight&&result.timer===0&&result.closed&&result.name==='Chosen manual name'&&result.id===570&&result.cancels===1&&result.writes===0,'manual acceptance left a live search or let a late provider replace the chosen draft');

 });

 await run('background static artwork search keeps its result without building or refetching its gallery',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();artworkPrefetchCache.clear();const waiting=searchArtwork('cover');document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const list=document.getElementById('artwork-search-list');let mutations=0;const observer=new MutationObserver(records=>mutations+=records.length);observer.observe(list,{childList:true,subtree:true});const focused=document.activeElement;

   searchIO.artworks[0].resolve({query:'Owned artwork',candidates:[{url:'http://127.0.0.1:13139/cover.jpg',title:'Deferred cover',provider:'steam-store',width:600,height:900}]});await waiting;await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const background={mutations:mutations+observer.takeRecords().length,focusKept:document.activeElement===focused};observer.disconnect();

   document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);return {background,text:list.textContent,requests:searchIO.artworks.length};

  });progressMetrics.push({case:'background-static-gallery',...result});

  check(result.background.mutations===0&&result.background.focusKept&&result.text.includes('Deferred cover')&&result.requests===1,'background artwork search rebuilt/focused the gallery or discarded its cached result');

  const presentationRows=await page.evaluate(async()=>{
   const actual=invoke,oldFocus=document.hasFocus,base=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),rows=[];let downloads=[];
   const flush=async()=>{for(let i=0;i<64;i++)await Promise.resolve();};
   invoke=(command,args={},timeout=0)=>command==='downloadArtworkCandidate'?new Promise((resolve,reject)=>downloads.push({args:structuredClone(args),resolve,reject})):command==='searchArtwork'?Promise.resolve({candidates:[]}):command==='state'||command==='plan'?Promise.resolve(structuredClone(snapshot)):actual(command,args,timeout);
   const notify=()=>[toastNotice.textContent,...noticeQueue.map(n=>n.message)].join('|');
   try{for(const type of ['cover','long','wallpaper'])for(const mode of ['thumbnail failure','thumbnail dimensions','download success','download failure']){
    closeEdit();cancelArtworkTask();document.hasFocus=()=>true;snapshot={...structuredClone(base),dataRoot:'G:/BackgroundGallery'};openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();artworkPrefetchCache.clear();clearNotice();toastNotice.textContent='';downloads=[];
    artworkSearchSlot=type;artworkSearchType=artworkSearchTypeForSlot(type);$('#artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?background-'+type,title:'Background owned image',provider:'steam-store',width:12,height:34},{url:'http://127.0.0.1:13139/cover.jpg?fallback-'+type,title:'Explicit background fallback',provider:'playnite-igdb',width:600,height:900}]});
    const card=$('#artwork-search-list .artwork-candidate'),image=card.querySelector('img');card.focus();if(mode.startsWith('download'))card.click();await flush();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const focused=document.activeElement;let mutations=0;const observer=new MutationObserver(records=>mutations+=records.length);for(const target of [$('#artwork-search-modal'),toastNotice])observer.observe(target,{subtree:true,childList:true,attributes:true,characterData:true});
    if(mode==='thumbnail failure')image.dispatchEvent(new Event('error'));else if(mode==='thumbnail dimensions'){Object.defineProperty(image,'naturalWidth',{value:641,configurable:true});Object.defineProperty(image,'naturalHeight',{value:961,configurable:true});image.dispatchEvent(new Event('load'));}else if(mode==='download success')downloads[0].resolve({path:'G:/H5Fixture/background-'+type+'.jpg',width:600,height:900});else downloads[0].reject(Error('owned background download failed'));await flush();
    const background={mutations:mutations+observer.takeRecords().length,focusKept:document.activeElement===focused,notice:notify(),dirty:artworkDraft.dirty[type],path:artworkDraft[type]?.asset?.path,busy:artworkTaskInFlight,calls:downloads.length};observer.disconnect();
    document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await flush();const visible={closed:$('#artwork-search-modal').classList.contains('hidden'),disabled:card.disabled,dimensions:card.querySelector('.artwork-candidate-meta').children[1].textContent,notice:notify(),loading:card.classList.contains('loading'),dirty:artworkDraft.dirty[type],fallbackFocused:document.activeElement===$('#artwork-search-list .artwork-candidate:nth-child(2)')};
    rows.push({type,mode,background,visible});
   }}finally{for(const d of downloads)d.resolve({path:'G:/H5Fixture/cleanup.jpg'});await flush();invoke=actual;document.hasFocus=oldFocus;closeEdit();}return rows;
  });progressMetrics.push({case:'background-gallery-presentation',rows:presentationRows});
  check(presentationRows.length===12&&presentationRows.every(r=>r.background.mutations===0&&r.background.focusKept&&!r.background.notice&&(r.mode==='download success'?r.background.dirty&&r.background.path==='G:/H5Fixture/background-'+r.type+'.jpg'&&!r.background.busy&&r.background.calls===1&&r.visible.closed&&!r.visible.loading&&r.visible.notice.includes('已下载'):r.mode==='download failure'?!r.background.dirty&&!r.background.busy&&r.background.calls===1&&r.visible.disabled&&r.visible.fallbackFocused&&!r.visible.loading&&r.visible.notice.includes('失败'):r.background.calls===0&&(r.mode==='thumbnail failure'?r.visible.disabled&&r.visible.fallbackFocused&&r.visible.notice.includes('失败'):r.visible.dimensions==='641×961'&&!r.visible.notice))),'background gallery mutated DOM/focus/notice, lost accepted download data, or failed to present on foreground');


 });

 await run('a static artwork reply from a retired page cannot repopulate its gallery',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();artworkPrefetchCache.clear();const draft=editDraft,waiting=searchArtwork('cover');window.dispatchEvent(new PageTransitionEvent('pagehide'));searchIO.artworks[0].resolve({candidates:[{url:'http://127.0.0.1:13139/cover.jpg',title:'Retired static candidate'}]});await waiting;window.dispatchEvent(new PageTransitionEvent('pageshow'));await new Promise(requestAnimationFrame);

   return {closed:document.getElementById('artwork-search-modal').classList.contains('hidden'),stale:document.getElementById('artwork-search-list').textContent.includes('Retired static candidate'),draftKept:editDraft===draft};

  });

  check(result.closed&&!result.stale&&result.draftKept,'retired static reply rebuilt an old page or lost the editor draft');

 });

 await run('background video search defers candidate DOM and never selects a trailer automatically',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;let finish,requests=0;invoke=async(command,args={},timeout=0)=>{if(command==='mediaSearchVideos'){requests++;return new Promise(resolve=>finish=resolve);}return actual(command,args,timeout);};

   try{

    const waiting=searchSharedWallpaperVideos(captureSharedWallpaperWrite());document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const list=document.getElementById('artwork-search-list');let mutations=0;const observer=new MutationObserver(records=>mutations+=records.length);observer.observe(list,{childList:true,subtree:true});const focused=document.activeElement;

    finish({query:'Owned video',entryKey:selectedActionGame.gameMedia.key,bindingRevision:1,searchToken:'owned-video-search',candidates:[{id:'video-1',title:'Deferred trailer',previewUrl:'/wallpaper.jpg'}]});await waiting;await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);const background={mutations:mutations+observer.takeRecords().length,focusKept:document.activeElement===focused};observer.disconnect();

    document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);return {background,text:list.textContent,requests,selections:reverseIO.calls.filter(call=>call.command==='mediaSelectVideo').length};

   }finally{invoke=actual;}

  });progressMetrics.push({case:'background-video-gallery',...result});

  check(result.background.mutations===0&&result.background.focusKept&&result.text.includes('Deferred trailer')&&result.requests===1&&result.selections===0,'background video result rebuilt/focused the gallery or automatically selected a trailer');

 });

 await run('a retired video search reply cannot reopen its old gallery',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;let finish;invoke=async(command,args={},timeout=0)=>command==='mediaSearchVideos'?new Promise(resolve=>finish=resolve):actual(command,args,timeout);

   try{const draft=editDraft,waiting=searchSharedWallpaperVideos(captureSharedWallpaperWrite());window.dispatchEvent(new PageTransitionEvent('pagehide'));finish({query:'Retired video',candidates:[{id:'old-video',title:'Retired trailer'}]});await waiting;window.dispatchEvent(new PageTransitionEvent('pageshow'));await new Promise(requestAnimationFrame);return {closed:document.getElementById('artwork-search-modal').classList.contains('hidden'),stale:document.getElementById('artwork-search-list').textContent.includes('Retired trailer'),draftKept:editDraft===draft};}finally{invoke=actual;}

  });

  check(result.closed&&!result.stale&&result.draftKept,'retired video reply reopened or repopulated its old gallery');

 });



 await run('a detached gallery cannot receive a late video response or recursive error rendering',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;let finish;invoke=async(command,args={},timeout=0)=>command==='mediaSearchVideos'?new Promise(resolve=>finish=resolve):actual(command,args,timeout);

   try{const waiting=searchSharedWallpaperVideos(captureSharedWallpaperWrite());document.getElementById('artwork-search-modal').remove();finish({query:'Detached gallery',candidates:[{id:'detached',title:'Detached trailer'}]});let error='';try{await waiting;}catch(failure){error=String(failure);}return {error,busy:artworkTaskInFlight};}finally{invoke=actual;}

  });

  check(!result.error&&!result.busy,'detached gallery callback threw or recursively tried to render an error');

 });



 await run('rapid offline editor reopen does not restart all automatic artwork providers',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,game=structuredClone(selectedActionGame),calls=[];invoke=async(command,args={},timeout=0)=>{if(command==='searchArtwork'){calls.push(structuredClone(args));throw Error('offline fixture');}return actual(command,args,timeout);};artworkPrefetchCache.clear();

   try{for(let i=0;i<4;i++){closeEdit();openEdit(game);await new Promise(resolve=>setTimeout(resolve,0));}return {requests:calls.length,types:calls.map(call=>call.type),cacheSize:artworkPrefetchCache.size};}finally{invoke=actual;}

  });progressMetrics.push({case:'automatic-offline-reopen',...result});

  check(result.requests===3,'four rapid offline opens restarted the automatic provider requests');

 });

 await run('explicit artwork retry bypasses automatic backoff and shares the same pending request',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,game=selectedActionGame,type=artworkSearchTypeForSlot('cover');let requests=0,fail=true,finish;invoke=async(command,args={},timeout=0)=>{if(command!=='searchArtwork')return actual(command,args,timeout);requests++;if(fail)throw Error('offline fixture');return new Promise(resolve=>finish=resolve);};artworkPrefetchCache.clear();

   try{await ensureArtworkSearch(game,type).catch(()=>{});fail=false;const first=ensureArtworkSearch(game,type),second=ensureArtworkSearch(game,type),shared=first===second;finish({query:'Manual retry',candidates:[]});const fresh=await first;await second;const again=await ensureArtworkSearch(game,type);return {requests,shared,fresh:again===fresh};}finally{invoke=actual;}

  });

  check(result.requests===2&&result.shared&&result.fresh,'automatic backoff blocked a user retry or duplicated the in-flight search');

 });

 await run('automatic artwork backoff expires on demand and increases after another failure',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,realNow=Date.now,game=selectedActionGame;let now=realNow(),requests=0;Date.now=()=>now;invoke=async(command,args={},timeout=0)=>{if(command==='searchArtwork'){requests++;throw Error('offline fixture');}return actual(command,args,timeout);};artworkPrefetchCache.clear();

   const tick=()=>new Promise(resolve=>setTimeout(resolve,0));

   try{startArtworkPrefetch(game);await tick();const initial=requests;now+=30001;startArtworkPrefetch(game);await tick();const retried=requests;startArtworkPrefetch(game);await tick();const immediate=requests;now+=30001;startArtworkPrefetch(game);await tick();return {initial,retried,immediate,afterThirtyMore:requests};}finally{Date.now=realNow;invoke=actual;}

  });progressMetrics.push({case:'automatic-backoff-expiry',...result});

  check(result.initial===3&&result.retried===6&&result.immediate===6&&result.afterThirtyMore===6,'automatic failure retries ignored the increasing cooldown');

 });

 await run('user cancellation does not become a long automatic offline penalty',async()=>{

  const rows=await page.evaluate(async()=>{

   const actual=invoke,game=selectedActionGame,rows=[];

   try{for(const error of ['ERROR_CANCELLED: editor switched','素材任务已取消或被新请求替代']){let requests=0;artworkPrefetchCache.clear();invoke=async(command,args={},timeout=0)=>{if(command==='searchArtwork'){requests++;throw Error(error);}return actual(command,args,timeout);};for(let i=0;i<2;i++){startArtworkPrefetch(game);await new Promise(resolve=>setTimeout(resolve,0));}rows.push({error,requests});}return rows;}finally{invoke=actual;}

  });

  check(rows.every(row=>row.requests===6),'a deliberate cancellation incorrectly imposed offline backoff');

 });

 await run('settled artwork cache is bounded, preserves the current editor and prunes expired queries',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,realNow=Date.now,active=selectedActionGame,type=artworkSearchTypeForSlot('cover');let now=realNow(),requests=0;Date.now=()=>now;invoke=async(command,args={},timeout=0)=>command==='searchArtwork'?(requests++,{query:args.query,candidates:[]}):actual(command,args,timeout);artworkPrefetchCache.clear();

   const game=i=>({...structuredClone(active),gameDirectory:'G:/Fixture/Cache'+i,primaryExecutable:'G:/Fixture/Cache'+i+'/game.exe',override:{name:'Cache query '+i}});

   try{await ensureArtworkSearch(active,type);for(let i=0;i<130;i++)await ensureArtworkSearch(game(i),type);const size=artworkPrefetchCache.size,before=requests;await ensureArtworkSearch(active,type);const currentReused=requests===before;now+=5*60*1000+1;await ensureArtworkSearch(game(200),type);return {size,currentReused,expiredSize:artworkPrefetchCache.size,requests};}finally{Date.now=realNow;invoke=actual;}

  });progressMetrics.push({case:'bounded-settled-cache',...result});

  check(result.size<=96&&result.currentReused&&result.expiredSize===1,'completed cache grew without a bound, evicted active data or retained expired queries');

 });

 await run('recent artwork cache use survives capacity pressure without losing its response data',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,base=structuredClone(selectedActionGame),type=artworkSearchTypeForSlot('cover');closeEdit();let requests=0;invoke=async(command,args={},timeout=0)=>command==='searchArtwork'?(requests++,{query:args.query,candidates:[]}):actual(command,args,timeout);artworkPrefetchCache.clear();const game=i=>({...base,gameDirectory:'G:/Fixture/Lru'+i,primaryExecutable:'G:/Fixture/Lru'+i+'/game.exe',override:{name:'Lru '+i}});

   try{for(let i=0;i<96;i++)await ensureArtworkSearch(game(i),type);const first=await ensureArtworkSearch(game(0),type);await ensureArtworkSearch(game(96),type);return {size:artworkPrefetchCache.size,recent:artworkPrefetchCache.get(artworkSearchCacheKey(game(0),type))?.result===first,oldestRetained:artworkPrefetchCache.has(artworkSearchCacheKey(game(1),type)),requests};}finally{invoke=actual;}

  });

  check(result.size===96&&result.recent&&!result.oldestRetained&&result.requests===97,'cache pressure ignored recent use or re-requested a cached response');

 });

 await run('cache pruning never duplicates a pending artwork request and bounds it after settlement',async()=>{

  const result=await page.evaluate(async()=>{

   reverseHoldSearch();artworkPrefetchCache.clear();const base=structuredClone(selectedActionGame),type=artworkSearchTypeForSlot('cover'),games=Array.from({length:105},(_,i)=>({...base,gameDirectory:'G:/Fixture/Pending'+i,primaryExecutable:'G:/Fixture/Pending'+i+'/game.exe',override:{name:'Pending '+i}}));const pendingSearches=games.map(game=>ensureArtworkSearch(game,type));const duplicate=ensureArtworkSearch(games[0],type),shared=duplicate===pendingSearches[0],requests=searchIO.artworks.length;

   for(const request of searchIO.artworks)request.resolve({query:request.args.query,candidates:[]});await Promise.all([...pendingSearches,duplicate]);return {shared,requests,settledSize:artworkPrefetchCache.size,pending:[...artworkPrefetchCache.values()].filter(entry=>entry.pending).length};

  });progressMetrics.push({case:'pending-cache-preservation',...result});

  check(result.shared&&result.requests===105&&result.pending===0&&result.settledSize<=96,'capacity policy evicted an in-flight request or kept unlimited completed results');

 });

 await run('old gallery clicks cannot download an image into a newly opened editor',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,attempts=[];invoke=async(command,args={},timeout=0)=>{if(command==='downloadArtworkCandidate'){attempts.push(args);return {path:'/cover.jpg',width:600,height:900};}return actual(command,args,timeout);};

   try{artworkSearchSlot='cover';artworkSearchType=artworkSearchTypeForSlot('cover');document.getElementById('artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg',title:'Old game cover',provider:'steam-store'}]});const old=document.querySelector('#artwork-search-list .artwork-candidate'),next=structuredClone(selectedActionGame);next.gameDirectory+='-new';next.primaryExecutable+='-new';openEdit(next);const closedBefore=document.getElementById('artwork-search-modal').classList.contains('hidden');old.click();await new Promise(resolve=>setTimeout(resolve,0));return {attempts,closedBefore,dirty:artworkDraft.dirty.cover};}finally{invoke=actual;}

  });progressMetrics.push({case:'old-gallery-new-game',...result});

  check(result.closedBefore&&result.attempts.length===0&&!result.dirty,'old gallery click downloaded/applied the previous game cover to the new editor');


  const ownershipRows=await page.evaluate(async()=>{
   const actual=invoke,oldFocus=document.hasFocus,rows=[];let downloads=[];
   const flush=async()=>{for(let i=0;i<32;i++)await Promise.resolve();};
   invoke=(command,args={},timeout=0)=>command==='downloadArtworkCandidate'?new Promise((resolve,reject)=>downloads.push({args:structuredClone(args),resolve,reject})):command==='searchArtwork'?Promise.resolve({candidates:[]}):command==='state'||command==='plan'?Promise.resolve(structuredClone(snapshot)):actual(command,args,timeout);
   const setup=async type=>{closeEdit();cancelArtworkTask();document.hasFocus=()=>true;snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/OwnedStaticGallery'};const game=mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570');openEdit(game);await flush();artworkPrefetchCache.clear();clearNotice();noticeQueue.length=0;toastNotice.textContent='';downloads=[];artworkSearchSlot=type;artworkSearchType=artworkSearchTypeForSlot(type);$('#artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?owned-gallery-'+type,title:'Captured gallery',provider:'steam-store'}]});return {game:structuredClone(selectedActionGame),card:$('#artwork-search-list .artwork-candidate')};};
   const change=(mode,game)=>{
    if(mode.includes('other directory')){const other=structuredClone(game);other.gameDirectory+='-other';snapshot.library.games.push({...other});snapshot.steamPlan.items.push({...other.steam,gameDirectory:other.gameDirectory,primaryExecutable:other.primaryExecutable});openEdit(mergedGames().find(g=>keyFor(g)===keyFor(other)));}
    if(mode.includes('root replaced'))snapshot={...snapshot,dataRoot:'G:/ReplacementStaticGallery'};
    if(mode.includes('root alias'))snapshot={...snapshot,dataRoot:'g:\\ownedstaticgallery\\'};
    if(mode.includes('EXE alias'))selectedActionGame.primaryExecutable=selectedActionGame.primaryExecutable.toUpperCase().replaceAll(String.fromCharCode(92),'/');
    if(mode.includes('draft name')){$('#identity-name').value='Changed unsaved artwork query';$('#identity-name').dispatchEvent(new Event('input',{bubbles:true}));}
    if(mode.includes('draft ID')){$('#identity-steam-id').value='620';$('#identity-steam-id').dispatchEvent(new Event('input',{bubbles:true}));}
   };
   const modes=['click current','click root alias','click EXE alias','click other directory','click root replaced','click draft name','click draft ID','receipt current','receipt root alias','receipt other directory','receipt root replaced','receipt draft name','receipt draft ID','error current','error root replaced'];
   try{for(const type of ['cover','long','wallpaper'])for(const mode of modes){
    const {game,card}=await setup(type),isClick=mode.startsWith('click'),expectedPath='G:/H5Fixture/gallery-'+type+'.jpg';
    if(isClick)change(mode,game);card.click();await flush();if(!isClick)change(mode,game);
    const retained=JSON.stringify(artworkDraft[type]),dirty=artworkDraft.dirty[type],hiddenBefore=$('#artwork-search-modal').classList.contains('hidden');
    for(const request of downloads){if(mode.startsWith('error'))request.reject(Error('owned-gallery-download-failure'));else request.resolve({path:expectedPath,width:600,height:900});}await flush();
    const r={type,mode,requests:downloads.length,requestType:downloads[0]?.args.type,path:artworkDraft[type]?.asset?.path,dirty:artworkDraft.dirty[type],kept:retained===JSON.stringify(artworkDraft[type])&&dirty===artworkDraft.dirty[type],hiddenBefore,reported:[toastNotice.textContent,...noticeQueue.map(n=>n.message)].some(n=>n.includes('owned-gallery-download-failure'))};rows.push(r);
   }}finally{invoke=actual;document.hasFocus=oldFocus;closeEdit();}return rows;
  });progressMetrics.push({case:'static-gallery-owner',rows:ownershipRows});
  check(ownershipRows.length===45,'static gallery matrix incomplete');
  check(ownershipRows.every(r=>['click current','click root alias','click EXE alias','receipt current','receipt root alias'].includes(r.mode)?r.requests===1&&r.requestType===r.type&&r.path==='G:/H5Fixture/gallery-'+r.type+'.jpg'&&r.dirty&&!r.reported:r.mode==='error current'?r.requests===1&&r.kept&&r.reported:r.requests===(r.mode.startsWith('click')?0:1)&&r.kept&&!r.reported&&(!r.mode.includes('other directory')||r.hiddenBefore)),'static gallery click/download crossed root/editor/card/draft identity ownership, rejected a current alias, or displayed a retired error');

 });

 await run('a detached old gallery click or Enter cannot start another artwork download',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;let attempts=0;invoke=async(command,args={},timeout=0)=>command==='downloadArtworkCandidate'?(attempts++,{path:'/cover.jpg'}):actual(command,args,timeout);

   try{artworkSearchSlot='cover';artworkSearchType=artworkSearchTypeForSlot('cover');document.getElementById('artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?old',title:'Old query',provider:'steam-store'}]});const old=document.querySelector('#artwork-search-list .artwork-candidate');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?new',title:'New query',provider:'steam-store'}]});old.click();await new Promise(resolve=>setTimeout(resolve,0));old.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));await new Promise(resolve=>setTimeout(resolve,0));return {attempts,dirty:artworkDraft.dirty.cover};}finally{invoke=actual;}

  });

  check(result.attempts===0&&!result.dirty,'detached candidate activation started a native request');

 });

 await run('gallery item binding preserves a legitimate fallback choice after a failed download',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke,attempts=[];invoke=async(command,args={},timeout=0)=>{if(command!=='downloadArtworkCandidate')return actual(command,args,timeout);attempts.push(args);if(attempts.length===1)throw Error('fixture first download failed');return {path:'/cover.jpg',width:600,height:900};};

   try{artworkSearchSlot='cover';artworkSearchType=artworkSearchTypeForSlot('cover');document.getElementById('artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?steam',title:'Steam choice',provider:'steam-store'},{url:'http://127.0.0.1:13139/cover.jpg?fallback',title:'Fallback choice',provider:'playnite-igdb'}]});const cards=[...document.querySelectorAll('#artwork-search-list .artwork-candidate')];cards[0].click();await new Promise(resolve=>setTimeout(resolve,0));cards[1].click();await new Promise(resolve=>setTimeout(resolve,0));return {attempts:attempts.length,provider:artworkDraft.cover.asset?.provider,dirty:artworkDraft.dirty.cover};}finally{invoke=actual;}

  });

  check(result.attempts===2&&result.provider==='playnite-igdb'&&result.dirty,'binding an item to its gallery prevented a legitimate current fallback selection');

  const receiptRows=await page.evaluate(async()=>{
   const actualPost=chrome.webview.postMessage,actualSet=window.setTimeout,oldFocus=document.hasFocus,base=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),rows=[];
   const flush=async()=>{for(let i=0;i<48;i++)await Promise.resolve();};let held=[],timers=[];
   chrome.webview.postMessage=message=>{if(message.command==='downloadArtworkCandidate'){held.push(structuredClone(message));return;}return actualPost.call(chrome.webview,message);};
   window.setTimeout=(callback,delay,...args)=>{const handle=actualSet(callback,delay,...args);if(delay===ARTWORK_SEARCH_TIMEOUT_MS)timers.push({callback,handle,delay});return handle;};
   const reply=(m,suffix)=>reverseEmit({kind:'response',id:m.id,ok:true,result:{path:'G:/H5Fixture/manual-receipt-'+suffix+'.jpg',width:600,height:900}});
   try{for(const type of ['cover','long','wallpaper'])for(const mode of ['timeout and explicit retry','normal reply','retired owner','background timeout']){
    closeEdit();cancelArtworkTask();document.hasFocus=()=>true;snapshot={...structuredClone(base),dataRoot:'G:/ManualArtworkReceipt'};openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();artworkPrefetchCache.clear();clearNotice();noticeQueue.length=0;toastNotice.textContent='';held=[];timers=[];
    artworkSearchSlot=type;artworkSearchType=artworkSearchTypeForSlot(type);$('#artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?manual-deadline-'+type,title:'Selected Steam image',provider:'steam-store'},{url:'http://127.0.0.1:13139/cover.jpg?manual-retry-'+type,title:'Explicit alternate',provider:'playnite-igdb'}]});
    const cards=[...$('#artwork-search-list').querySelectorAll('.artwork-candidate')];cards[0].click();await flush();const before={calls:held.length,timers:timers.length,delay:timers[0]?.delay,pending:held.filter(m=>pending.has(m.id)).length,busy:artworkTaskInFlight};
    if(mode==='normal reply'){reply(held[0],type);await flush();rows.push({type,mode,before,dirty:artworkDraft.dirty[type],path:artworkDraft[type]?.asset?.path,busy:artworkTaskInFlight,url:downloadingCandidateUrl,pending:held.filter(m=>pending.has(m.id)).length});}
    else{
     if(mode==='retired owner'){const next=structuredClone(selectedActionGame);next.gameDirectory+='-other';openEdit(next);await flush();}
     if(mode==='background timeout'){document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
     for(const t of timers){clearTimeout(t.handle);t.callback();}await flush();
     const expired={busy:artworkTaskInFlight,url:downloadingCandidateUrl,dirty:artworkDraft.dirty[type],pending:held.filter(m=>pending.has(m.id)).length,calls:held.length,reported:[toastNotice.textContent,...noticeQueue.map(n=>n.message)].some(s=>s.includes('失败')||s.includes('暂未响应'))};reply(held[0],'late-'+type);await flush();const lateKept=!artworkDraft.dirty[type];let retry;
     if(mode==='timeout and explicit retry'){cards[1].click();await flush();const calls=held.length;if(calls===2)reply(held[1],'retry-'+type);await flush();retry={calls,dirty:artworkDraft.dirty[type],path:artworkDraft[type]?.asset?.path,busy:artworkTaskInFlight,pending:held.filter(m=>pending.has(m.id)).length};}
     let foregroundReported;if(mode==='background timeout'){document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await flush();foregroundReported=[toastNotice.textContent,...noticeQueue.map(n=>n.message)].some(s=>s.includes('失败')||s.includes('暂未响应'));}
     rows.push({type,mode,before,expired,lateKept,retry,foregroundReported});
    }
    cancelArtworkTask();for(const m of held)if(pending.has(m.id))reply(m,'cleanup');for(const t of timers)clearTimeout(t.handle);await flush();document.hasFocus=()=>true;
   }}finally{cancelArtworkTask();for(const m of held)if(pending.has(m.id))reply(m,'cleanup');for(const t of timers)clearTimeout(t.handle);await flush();chrome.webview.postMessage=actualPost;window.setTimeout=actualSet;document.hasFocus=oldFocus;closeEdit();}return rows;
  });progressMetrics.push({case:'manual-artwork-receipt',rows:receiptRows});
  check(receiptRows.length===12&&receiptRows.every(r=>r.before.calls===1&&r.before.timers===1&&r.before.delay===185000&&r.before.pending===1&&r.before.busy&&(r.mode==='normal reply'?r.dirty&&r.path==='G:/H5Fixture/manual-receipt-'+r.type+'.jpg'&&!r.busy&&!r.url&&r.pending===0:!r.expired.busy&&!r.expired.url&&!r.expired.dirty&&r.expired.pending===0&&r.expired.calls===1&&r.lateKept&&(r.mode==='retired owner'?!r.expired.reported:r.mode==='background timeout'?!r.expired.reported&&r.foregroundReported:r.expired.reported)&&(r.mode!=='timeout and explicit retry'||r.retry.calls===2&&r.retry.dirty&&r.retry.path==='G:/H5Fixture/manual-receipt-retry-'+r.type+'.jpg'&&!r.retry.busy&&r.retry.pending===0))),'manual artwork receipt lacked its original deadline, held busy/pending, replayed automatically, accepted a retired reply or blocked explicit retry');

  const pathReceiptRows=await page.evaluate(async()=>{
   const post=chrome.webview.postMessage,oldFocus=document.hasFocus,base=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),rows=[];
   let held=[];const flush=async()=>{for(let i=0;i<64;i++)await Promise.resolve();};
   chrome.webview.postMessage=message=>{if(message.command==='downloadArtworkCandidate'){held.push(structuredClone(message));return;}return post.call(chrome.webview,message);};
   const notices=()=>[toastNotice.textContent,...noticeQueue.map(n=>n.message)].join('|');
   const reply=(message,value)=>reverseEmit({kind:'response',id:message.id,ok:true,result:value});
   const examples=[['missing',{}],['empty',{path:''}],['whitespace',{path:' \t\r\n '}],['number',{path:7}],['boolean',{path:true}],['object',{path:{file:'G:/H5Fixture/not-a-string.jpg'}}],['array',{path:['G:/H5Fixture/not-a-string.jpg']}],['valid',{path:'G:/H5Fixture/Valid Name 日本語 image.jpg'}]];
   try{for(const type of ['cover','long','wallpaper'])for(const [mode,value]of examples){
    closeEdit();cancelArtworkTask();document.hasFocus=()=>true;snapshot={...structuredClone(base),dataRoot:'G:/ManualFileReceipt'};openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();artworkPrefetchCache.clear();clearNotice();toastNotice.textContent='';held=[];
    const originalAsset=JSON.stringify(artworkDraft[type]),originalDirty=artworkDraft.dirty[type];artworkSearchSlot=type;artworkSearchType=artworkSearchTypeForSlot(type);$('#artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg?path-receipt-'+type,title:'Chosen response',provider:'steam-store'},{url:'http://127.0.0.1:13139/cover.jpg?path-retry-'+type,title:'Explicit valid alternative',provider:'playnite-igdb'}]});
    const cards=[...$('#artwork-search-list').querySelectorAll('.artwork-candidate')];cards[0].click();await flush();if(held.length!==1)throw Error('manual file receipt did not reach native transport');reply(held[0],value);await flush();
    const after={calls:held.length,pending:held.filter(m=>pending.has(m.id)).length,dirty:artworkDraft.dirty[type],assetKept:JSON.stringify(artworkDraft[type])===originalAsset&&artworkDraft.dirty[type]===originalDirty,path:artworkDraft[type]?.asset?.path,closed:$('#artwork-search-modal').classList.contains('hidden'),disabled:cards[0].disabled,busy:artworkTaskInFlight,url:downloadingCandidateUrl,notice:notices()};
    let retry;
    if(mode!=='valid'){cards[1].click();await flush();if(held.length===2)reply(held[1],{path:'G:/H5Fixture/valid-retry-'+type+'.jpg',width:600,height:900});await flush();retry={calls:held.length,dirty:artworkDraft.dirty[type],path:artworkDraft[type]?.asset?.path,busy:artworkTaskInFlight,url:downloadingCandidateUrl};}
    rows.push({type,mode,value,after,retry});
    for(const m of held)if(pending.has(m.id))reply(m,{path:'G:/H5Fixture/cleanup.jpg'});await flush();
   }}finally{for(const m of held)if(pending.has(m.id))reply(m,{path:'G:/H5Fixture/cleanup.jpg'});await flush();chrome.webview.postMessage=post;document.hasFocus=oldFocus;closeEdit();}return rows;
  });progressMetrics.push({case:'manual-artwork-file-receipt',rows:pathReceiptRows});
  check(pathReceiptRows.length===24&&pathReceiptRows.every(r=>r.after.calls===1&&r.after.pending===0&&!r.after.busy&&!r.after.url&&(r.mode==='valid'?r.after.dirty&&r.after.path===r.value.path&&r.after.closed&&r.after.notice.includes('已下载'):r.after.assetKept&&!r.after.closed&&r.after.disabled&&!r.after.notice.includes('已下载')&&r.after.notice.includes('失败')&&r.retry.calls===2&&r.retry.dirty&&r.retry.path==='G:/H5Fixture/valid-retry-'+r.type+'.jpg'&&!r.retry.busy&&!r.retry.url)),'manual gallery accepted a missing, blank or nonstring file receipt, replaced the existing draft, or blocked an explicit valid alternative');



 });

 await run('old gallery item binding cannot be reused for a newly selected artwork slot',async()=>{

  const result=await page.evaluate(async()=>{

   const actual=invoke;let attempts=0;invoke=async(command,args={},timeout=0)=>command==='downloadArtworkCandidate'?(attempts++,{path:'/cover.jpg'}):actual(command,args,timeout);

   try{artworkSearchSlot='cover';artworkSearchType=artworkSearchTypeForSlot('cover');document.getElementById('artwork-search-modal').classList.remove('hidden');renderArtworkCandidates({candidates:[{url:'http://127.0.0.1:13139/cover.jpg',title:'Cover only',provider:'steam-store'}]});const old=document.querySelector('#artwork-search-list .artwork-candidate');artworkSearchSlot='long';artworkSearchType=artworkSearchTypeForSlot('long');old.click();await new Promise(resolve=>setTimeout(resolve,0));return {attempts,cover:artworkDraft.dirty.cover,long:artworkDraft.dirty.long};}finally{invoke=actual;}

  });

  check(result.attempts===0&&!result.cover&&!result.long,'an old cover item was downloaded as a different slot');

 });

 await run('same-slot query and AppID round trips never reuse replaced native searches',async()=>{

  await holdNativeArtwork();const rows=await page.evaluate(async()=>{

   const rows=[],outcome=p=>p.then(result=>({ok:true,result}),error=>({ok:false,error:String(error)}));

   for(const change of ['query','appid']){

    const start=artworkNativeIO.jobs.length,game={gameDirectory:'G:/Fixture/NativeQuery',primaryExecutable:'G:/Fixture/NativeQuery/game.exe',override:{name:'Query one',steamId:'570'}},other=structuredClone(game);

    if(change==='query')other.override.name='Query two';else other.override.steamId='730';artworkPrefetchCache.clear();

    const first=ensureArtworkSearch(game,'cover'),firstResult=outcome(first),secondResult=outcome(ensureArtworkSearch(other,'cover')),again=ensureArtworkSearch(game,'cover'),lastResult=outcome(again);

    const before={requests:artworkNativeIO.jobs.length-start,reused:first===again,pending:[...pending.values()].filter(row=>row.command==='searchArtwork').length};

    for(let i=start;i<artworkNativeIO.jobs.length;i++)artworkNativeIO.reply(i,{query:'Current '+change,candidates:[]});

    const [a,b,c]=await Promise.all([firstResult,secondResult,lastResult]);rows.push({change,...before,oldCancelled:!a.ok&&!b.ok,latestOk:c.ok,latestQuery:c.result?.query});

   }return rows;

  });progressMetrics.push({case:'artwork-native-round-trip',rows});

  check(rows.every(row=>row.requests===3&&!row.reused&&row.pending===1&&row.oldCancelled&&row.latestOk),'query/AppID round trip reused a native-cancelled request or retained old IPC');

 });

 await run('late replaced receipts cannot replace the newest same-key artwork result',async()=>{

  await holdNativeArtwork();const result=await page.evaluate(async()=>{

   const game={gameDirectory:'G:/Fixture/LateQuery',primaryExecutable:'G:/Fixture/LateQuery/game.exe',override:{name:'Latest one'}},other={...game,override:{name:'Middle two'}},outcome=p=>p.then(value=>({ok:true,value}),error=>({ok:false,error:String(error)}));

   const a=outcome(ensureArtworkSearch(game,'cover')),b=outcome(ensureArtworkSearch(other,'cover')),c=outcome(ensureArtworkSearch(game,'cover')),latest=artworkNativeIO.jobs.length-1;

   artworkNativeIO.reply(latest,{query:'Newest result',candidates:[]});for(let i=0;i<latest;i++)artworkNativeIO.reply(i,{query:'Old result',candidates:[]});

   const values=await Promise.all([a,b,c]),entry=artworkPrefetchCache.get(artworkSearchCacheKey(game,'cover'));return {requests:artworkNativeIO.jobs.length,latestOk:values[2].ok,query:entry?.result?.query,error:entry?.error,pending:[...pending.values()].filter(item=>item.command==='searchArtwork').length};

  });progressMetrics.push({case:'artwork-replaced-late-receipts',...result});

  check(result.requests===3&&result.latestOk&&result.query==='Newest result'&&!result.error&&result.pending===0,'late cancelled receipt poisoned the newest query result');

 });

 await run('returning from a gallery or editor can immediately start a fresh artwork read',async()=>{

  await holdNativeArtwork();const rows=await page.evaluate(async()=>{

   const rows=[];for(const close of ['gallery','editor']){

    const start=artworkNativeIO.jobs.length,game=selectedActionGame;artworkPrefetchCache.clear();const old=searchArtwork('cover');

    if(close==='gallery')cancelArtworkTask();else{closeEdit();openEdit(game);}

    const expectedReads=close==='editor'&&!game.mediaOnly?3:1,current=searchArtwork('cover'),before={requests:artworkNativeIO.jobs.length-start,pending:[...pending.values()].filter(item=>item.command==='searchArtwork').length,expectedRequests:expectedReads+1,expectedPending:expectedReads};

    for(let i=start;i<artworkNativeIO.jobs.length;i++)artworkNativeIO.reply(i,{query:'Reopened',candidates:[{url:'http://127.0.0.1:13139/cover.jpg',title:'Reopened '+close,provider:'steam-store'}]});

    await Promise.all([old,current]);rows.push({close,...before,rendered:document.getElementById('artwork-search-list').textContent.includes('Reopened '+close)});cancelArtworkTask();

   }return rows;

  });progressMetrics.push({case:'artwork-cancel-reopen',rows});

  check(rows.every(row=>row.requests===row.expectedRequests&&row.pending===row.expectedPending&&row.rendered),'immediate reopen shared the cancelled gallery read or failed to render its new result');

 });

 await run('a native artwork download retires only its replaced read and keeps other slots',async()=>{

  await holdNativeArtwork();const result=await page.evaluate(async()=>{

   const game=selectedActionGame,outcome=p=>p.then(value=>({ok:true,value}),error=>({ok:false,error:String(error)}));

   const cover=outcome(ensureArtworkSearch(game,'cover')),long=outcome(ensureArtworkSearch(game,'long')),download=outcome(invoke('downloadArtworkCandidate',{executable:game.primaryExecutable,type:'cover',url:'http://127.0.0.1:13139/cover.jpg'}));

   const before={readIpc:[...pending.values()].filter(item=>item.command==='searchArtwork').length,downloadIpc:[...pending.values()].filter(item=>item.command==='downloadArtworkCandidate').length,coverPending:Boolean(artworkPrefetchCache.get(artworkSearchCacheKey(game,'cover'))?.pending),longPending:Boolean(artworkPrefetchCache.get(artworkSearchCacheKey(game,'long'))?.pending)};

   artworkNativeIO.reply(2,{path:'/cover.jpg'});artworkNativeIO.reply(0);artworkNativeIO.reply(1,{query:'Unaffected long',candidates:[]});const values=await Promise.all([cover,long,download]);return {...before,oldCancelled:!values[0].ok,longOk:values[1].ok,downloadOk:values[2].ok,requests:artworkNativeIO.jobs.length};

  });progressMetrics.push({case:'artwork-download-replaces-read',...result});

  check(result.readIpc===1&&result.downloadIpc===1&&!result.coverPending&&result.longPending&&result.oldCancelled&&result.longOk&&result.downloadOk&&result.requests===3,'download retained its cancelled read or retired an unrelated slot/download receipt');

 });

 await run('global artwork cancellation retires reads while accepted saves remain pending',async()=>{

  await holdNativeArtwork();const result=await page.evaluate(async()=>{

   const game=selectedActionGame,other={...game,primaryExecutable:game.primaryExecutable+'-other'},outcome=p=>p.then(value=>({ok:true,value}),error=>({ok:false,error:String(error)}));

   const reads=[outcome(ensureArtworkSearch(game,'cover')),outcome(ensureArtworkSearch(game,'long')),outcome(ensureArtworkSearch(other,'cover'))],save=outcome(invoke('saveManualName',{executable:game.primaryExecutable,name:'Accepted save'}));

   cancelArtworkTask();const fresh=outcome(ensureArtworkSearch(game,'cover')),before={readIpc:[...pending.values()].filter(item=>item.command==='searchArtwork').length,saveIpc:[...pending.values()].filter(item=>item.command==='saveManualName').length,requests:artworkNativeIO.jobs.length};

   artworkNativeIO.replyWrite(0);for(let i=artworkNativeIO.jobs.length-1;i>=0;i--)artworkNativeIO.reply(i);const old=await Promise.all(reads),saved=await save,current=await fresh;return {...before,oldCancelled:old.every(item=>!item.ok),saved:saved.ok,current:current.ok,cancels:artworkNativeIO.cancels};

  });progressMetrics.push({case:'artwork-cancel-save-owner',...result});

  check(result.readIpc===1&&result.saveIpc===1&&result.requests===4&&result.oldCancelled&&result.saved&&result.current&&result.cancels===1,'artwork cancellation retained old reads, replayed work, or disturbed an accepted save');

 });

 await run('pending artwork sharing preserves other EXEs slots and normalized same-query aliases',async()=>{

  await holdNativeArtwork();const result=await page.evaluate(async()=>{

   const game={gameDirectory:'G:/Fixture/Scope',primaryExecutable:'G:/Fixture/Scope/game.exe',override:{name:'Scope control'}},other={...game,primaryExecutable:'G:/Fixture/OtherScope/game.exe'},alias={...game,primaryExecutable:game.primaryExecutable.toUpperCase().replaceAll('/','\\')};

   const a=ensureArtworkSearch(game,'cover'),b=ensureArtworkSearch(other,'cover'),c=ensureArtworkSearch(game,'long'),shared=ensureArtworkSearch(alias,'cover')===a,before={requests:artworkNativeIO.jobs.length,cancelled:artworkNativeIO.jobs.filter(job=>job.cancelled).length,pending:[...pending.values()].filter(item=>item.command==='searchArtwork').length};

   artworkNativeIO.jobs.forEach((_,i)=>artworkNativeIO.reply(i));await Promise.all([a,b,c]);return {...before,shared};

  });

  check(result.requests===3&&result.cancelled===0&&result.pending===3&&result.shared,'native scope ownership broke independent jobs or same-query deduplication');

 });

 await run('artwork searches without EXEs use distinct native query scopes',async()=>{

  await holdNativeArtwork();const result=await page.evaluate(async()=>{

   const game={primaryExecutable:'',override:{name:'Query scope one'}},other={primaryExecutable:'',override:{name:'Query scope two'}};

   const a=ensureArtworkSearch(game,'cover'),b=ensureArtworkSearch(other,'cover'),shared=ensureArtworkSearch(game,'cover')===a,before={requests:artworkNativeIO.jobs.length,cancelled:artworkNativeIO.jobs.filter(job=>job.cancelled).length};artworkNativeIO.jobs.forEach((_,i)=>artworkNativeIO.reply(i));await Promise.all([a,b]);return {...before,shared};

  });

  check(result.requests===2&&result.cancelled===0&&result.shared,'query-only artwork jobs were incorrectly treated as one native scope');

 });

 await run('completed artwork data remains reusable across native download and cancellation',async()=>{

  await holdNativeArtwork();const result=await page.evaluate(async()=>{

   const game=selectedActionGame,a=ensureArtworkSearch(game,'cover');artworkNativeIO.reply(0,{query:'Reusable settled',candidates:[]});const original=await a;

   const download=invoke('downloadArtworkCandidate',{executable:game.primaryExecutable,type:'cover',url:'http://127.0.0.1:13139/cover.jpg'});artworkNativeIO.reply(1,{path:'/cover.jpg'});await download;cancelArtworkTask();await Promise.resolve();const cached=await ensureArtworkSearch(game,'cover');return {same:cached===original,requests:artworkNativeIO.jobs.length,query:cached.query};

  });

  check(result.same&&result.requests===2&&result.query==='Reusable settled','native task retirement discarded valid completed artwork data');

 });

 await run('a late detached plan snapshot cannot roll back a completed name and bucket save',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   const immediate=await page.evaluate(async()=>{

    const stale=structuredClone(snapshot),game=selectedActionGame,key=keyFor(game),exe=normalizedUiPath(game.primaryExecutable),name=document.getElementById('identity-name');name.value='Saved before late plan';name.dispatchEvent(new Event('input'));editDraft.bucket='non-game';const save=persistEditDraft();snapshotIO.replyWrite(0);await save;snapshotIO.resetPaint();

    reverseEmit({event:'library-plan-refreshed',source:'scrape-queue',snapshot:stale});return {kept:snapshot.manualOverrides.items[exe]?.name==='Saved before late plan'&&snapshot.config.manualBuckets?.[key]?.bucket==='non-game',paint:snapshotIO.libraryPaints};

   });await page.clock.runFor(251);

   const final=await page.evaluate(async()=>{const reads=snapshotIO.reads.length;if(reads)await snapshotIO.replyState(0);await Promise.resolve();return {reads,name:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(selectedActionGame.primaryExecutable))?.override?.name,bucket:manualBucket(selectedActionGame),paints:snapshotIO.libraryPaints};});progressMetrics.push({case:'advisory-after-save',immediate,final});

   check(immediate.kept&&immediate.paint===0&&final.reads===1&&final.name==='Saved before late plan'&&final.bucket==='non-game'&&final.paints===1,'late background plan rolled back a saved name/bucket or painted its stale payload');

  }finally{await page.clock.resume();}

 });

 await run('a late network plan cannot undo the card result of a join completed after Back',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   const immediate=await page.evaluate(async()=>{

    const stale=structuredClone(snapshot),game=selectedActionGame,key=keyFor(game),join=joinEditedGameToSteam();closeEdit();snapshotIO.replyWrite(0);await join;snapshotIO.resetPaint();

    const joined=()=>category(mergedGames().find(g=>keyFor(g)===key))==='in-steam';const before=joined();reverseEmit({event:'network-plan-refreshed',snapshot:stale});return {before,kept:joined(),phase:steamCommitTasks.get(key)?.phase,closed:document.getElementById('edit-modal').classList.contains('hidden')};

   });await page.clock.runFor(251);

   const final=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,joined:mergedGames().some(g=>category(g)==='in-steam'),closed:document.getElementById('edit-modal').classList.contains('hidden')};});progressMetrics.push({case:'advisory-after-join',immediate,final});

   check(immediate.before&&immediate.kept&&immediate.phase==='completed'&&immediate.closed&&final.reads===1&&final.joined&&final.closed,'late network snapshot undid a successful single-card join or reopened the editor');

  }finally{await page.clock.resume();}

 });

 await run('all detached scan and plan events preserve committed edits and load current discovered games',async()=>{

  await installTestClock();await holdNativeSnapshotLane();const rows=[];

  try{

   for(const event of ['library-first-run-scan-complete','library-first-run-complete','library-scan-complete','library-plan-refreshed','network-plan-refreshed']){

    const before=await page.evaluate(async event=>{

     const stale=structuredClone(snapshot),index=snapshotIO.writes.length,name=document.getElementById('identity-name'),label='Saved for '+event;name.value=label;name.dispatchEvent(new Event('input'));const save=persistEditDraft();snapshotIO.replyWrite(index);await save;

     const directory='G:/Fixture/AdvisoryScan/'+index;__LIBRARY_H5_FIXTURE__.snapshot.library.games.push({gameDirectory:directory,primaryExecutable:directory+'/game.exe',directoryName:'New scan '+index,status:'ready'});const reads=snapshotIO.reads.length;

     reverseEmit({event,snapshot:stale});return {reads,label,directory,kept:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(selectedActionGame.primaryExecutable))?.override?.name===label};

    },event);await page.clock.runFor(251);

    const after=await page.evaluate(async before=>{if(snapshotIO.reads.length>before.reads)await snapshotIO.replyState(before.reads);await Promise.resolve();return {reads:snapshotIO.reads.length-before.reads,discovered:mergedGames().some(g=>keyFor(g)===normalizedUiPath(before.directory)),name:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(selectedActionGame.primaryExecutable))?.override?.name};},before);rows.push({event,...before,...after});

   }progressMetrics.push({case:'advisory-source-matrix',rows});

   check(rows.every(row=>row.kept&&row.reads===1&&row.discovered&&row.name===row.label),'a detached source applied old data or failed to publish the current completed scan');

  }finally{await page.clock.resume();}

 });

 await run('a burst of detached snapshots uses one current read and one library paint',async()=>{

  await installTestClock();await page.evaluate(()=>reverseLargeLibrary(240));await page.clock.runFor(301);await page.evaluate(()=>Promise.resolve());await holdNativeSnapshotLane();

  try{

   const immediate=await page.evaluate(()=>{const stale=structuredClone(snapshot);snapshotIO.resetPaint();for(let i=0;i<64;i++)reverseEmit({event:'library-plan-refreshed',source:'artwork-stage',snapshot:stale});return {paints:snapshotIO.libraryPaints,cards:libraryCards().length};});await page.clock.runFor(251);

   const final=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints,cards:libraryCards().length};});progressMetrics.push({case:'advisory-foreground-burst',immediate,final});

   check(immediate.paints===0&&immediate.cards===240&&final.reads===1&&final.paints===1&&final.cards===240,'detached snapshot burst rebuilt intermediate trees or multiplied state reads');

  }finally{await page.clock.resume();}

 });

 await run('background detached snapshots defer both library work and native refresh until focus',async()=>{

  await installTestClock();await page.evaluate(()=>reverseLargeLibrary(240));await page.clock.runFor(301);await page.evaluate(()=>Promise.resolve());await holdNativeSnapshotLane();

  try{

   await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));snapshotIO.resetPaint();const stale=structuredClone(snapshot);for(let i=0;i<64;i++)reverseEmit({event:i%2?'network-plan-refreshed':'library-plan-refreshed',snapshot:stale});});await page.clock.runFor(1001);

   const background=await page.evaluate(()=>({reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints,pending:silentRefreshPending}));await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);

   const final=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints,cards:libraryCards().length};});progressMetrics.push({case:'advisory-background-burst',background,final});

   check(background.reads===0&&background.paints===0&&background.pending&&final.reads===1&&final.paints===1&&final.cards===240,'background detached snapshots redrew cards, queried state, or lost the deferred refresh');

  }finally{await page.clock.resume();}

 });

 await run('detached snapshots during a current read coalesce into one bounded follow-up',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   await page.evaluate(()=>{window.advisoryOld=structuredClone(snapshot);reverseEmit({event:'library-plan-refreshed',snapshot:advisoryOld});snapshotIO.resetPaint();});await page.clock.runFor(251);

   const middle=await page.evaluate(async()=>{for(let i=0;i<32;i++)reverseEmit({event:'network-plan-refreshed',snapshot:advisoryOld});if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints};});await page.clock.runFor(251);

   const final=await page.evaluate(async()=>{if(snapshotIO.reads.length>1)await snapshotIO.replyState(1);await Promise.resolve();return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints,pending:silentRefreshPending};});progressMetrics.push({case:'advisory-read-follow-up',middle,final});

   check(middle.reads===1&&middle.paints===0&&final.reads===2&&final.paints===1&&!final.pending,'events during the read lost a change, painted every payload, or scheduled more than one follow-up');

  }finally{await page.clock.resume();}

 });

 await run('a failed advisory refresh keeps saved data and waits for a later explicit opportunity',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   await page.evaluate(async()=>{const stale=structuredClone(snapshot),name=document.getElementById('identity-name');name.value='Kept through read failure';name.dispatchEvent(new Event('input'));const save=persistEditDraft();snapshotIO.replyWrite(0);await save;reverseEmit({event:'library-plan-refreshed',snapshot:stale});});await page.clock.runFor(251);

   const failed=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.failState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,name:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(selectedActionGame.primaryExecutable))?.override?.name};});await page.clock.runFor(20001);

   const quiet=await page.evaluate(()=>snapshotIO.reads.length);await page.evaluate(()=>window.dispatchEvent(new Event('focus')));await page.clock.runFor(251);

   const recovered=await page.evaluate(async()=>{if(snapshotIO.reads.length>1)await snapshotIO.replyState(1);await Promise.resolve();return {reads:snapshotIO.reads.length,name:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(selectedActionGame.primaryExecutable))?.override?.name};});progressMetrics.push({case:'advisory-failed-read',failed,quiet,recovered});

   check(failed.reads===1&&failed.name==='Kept through read failure'&&quiet===1&&recovered.reads===2&&recovered.name===failed.name,'failed advisory read replaced saved data, retried itself, or failed to recover later');

  }finally{await page.clock.resume();}

 });

 await run('ordinary bootstrap snapshots remain directly authoritative without an extra read',async()=>{

  await holdNativeSnapshotLane();const result=await page.evaluate(()=>{const next=structuredClone(snapshot),exe=normalizedUiPath(selectedActionGame.primaryExecutable);next.manualOverrides.items[exe]={name:'Bootstrap current name'};snapshotIO.resetPaint();reverseEmit({kind:'response',id:0,ok:true,result:next});return {name:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===exe)?.override?.name,reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints};});

  check(result.name==='Bootstrap current name'&&result.reads===0&&result.paints===1,'advisory policy delayed a normal FIFO bootstrap snapshot or queried it again');

 });

 await run('queued save and state receipts retain their native FIFO publication order',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   await page.evaluate(()=>{const name=document.getElementById('identity-name');name.value='FIFO current name';name.dispatchEvent(new Event('input'));window.snapshotSave=persistEditDraft();scheduleSilentRefresh();});await page.clock.runFor(251);

   const result=await page.evaluate(async()=>{snapshotIO.replyWrite(0);await snapshotSave;await snapshotIO.replyState(0);await Promise.resolve();return {order:snapshotIO.order,reads:snapshotIO.reads.length,writes:snapshotIO.writes.length,name:mergedGames().find(g=>normalizedUiPath(g.primaryExecutable)===normalizedUiPath(selectedActionGame.primaryExecutable))?.override?.name};});progressMetrics.push({case:'advisory-fifo-control',...result});

   check(result.order.join(',')==='saveEditor,state'&&result.reads===1&&result.writes===1&&result.name==='FIFO current name','state handling reordered/replayed an accepted save or discarded its result');

  }finally{await page.clock.resume();}

 });

 await run('lost artwork search response releases its IPC and pending cache entry without replay',async()=>{

  await installTestClock();

  try{

   await page.evaluate(()=>{

    window.artworkReceiptMessages=[];const actual=chrome.webview.postMessage.bind(chrome.webview);chrome.webview.postMessage=message=>{if(message.command==='searchArtwork'){artworkReceiptMessages.push(message);return;}actual(message);};artworkPrefetchCache.clear();window.artworkReceiptKey=artworkSearchCacheKey(selectedActionGame,artworkSearchTypeForSlot('cover'));window.artworkLostReceipt=ensureArtworkSearch(selectedActionGame,artworkSearchTypeForSlot('cover')).then(()=>null,error=>String(error));document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));

   });await page.clock.runFor(185001);await page.evaluate(()=>Promise.resolve());

   const result=await page.evaluate(async()=>{

    const entry=artworkPrefetchCache.get(artworkReceiptKey),observed={pending:[...pending.values()].filter(item=>item.command==='searchArtwork').length,cachePending:Boolean(entry?.pending),requests:artworkReceiptMessages.length};reverseEmit({kind:'response',id:artworkReceiptMessages[0].id,ok:true,result:{candidates:[]}});await artworkLostReceipt;return observed;

   });progressMetrics.push({case:'artwork-read-deadline',...result});

   check(result.pending===0&&!result.cachePending&&result.requests===1,'a lost read response left a permanently fresh pending cache entry or retried');

  }finally{await page.clock.resume();}

 });

 // This final case uses the existing browser virtual clock to exercise two bounded read deadlines.

 await run('lost identity receipts and terminal events time out once without retry or draft loss',async()=>{

  await installTestClock();

  await page.evaluate(()=>{

   document.hasFocus=()=>true;window.deadlineMessages=[];window.deadlineDropAck=true;const actual=chrome.webview.postMessage.bind(chrome.webview);chrome.webview.postMessage=message=>{

    if(message.command!=='searchIdentity')return actual(message);deadlineMessages.push(message);if(deadlineDropAck)return;queueMicrotask(()=>reverseEmit({kind:'response',id:message.id,ok:true,result:{pending:true,jobId:'deadline-job',generation:2,candidates:[]}}));

   };document.getElementById('identity-name').value='Deadline query';document.getElementById('identity-steam-id').value='';window.deadlineDraft=editDraft;window.lostReceipt=searchGame();

  });await page.clock.runFor(8001);await page.evaluate(()=>Promise.resolve());

  const receipt=await page.evaluate(()=>({inFlight:identitySearchInFlight,timer:identitySearchEllipsisTimer,pending:[...pending.values()].filter(item=>item.command==='searchIdentity').length,cancels:reverseIO.calls.filter(call=>call.command==='cancelIdentitySearch').length}));

  await page.evaluate(async()=>{reverseEmit({kind:'response',id:deadlineMessages[0].id,ok:true,result:{pending:false,candidates:[]}});await lostReceipt;deadlineDropAck=false;window.lostTerminal=searchGame();});await page.evaluate(()=>lostTerminal);await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));});await page.clock.runFor(95001);await page.evaluate(()=>Promise.resolve());

  const terminal=await page.evaluate(()=>({inFlight:identitySearchInFlight,timer:identitySearchEllipsisTimer,requests:deadlineMessages.length,cancels:reverseIO.calls.filter(call=>call.command==='cancelIdentitySearch').length,draftKept:editDraft===deadlineDraft}));

  await page.evaluate(()=>reverseEmit({event:'identity-search-complete',jobId:'deadline-job',generation:2,query:'Deadline query',executable:selectedActionGame.primaryExecutable,candidates:[]}));

  progressMetrics.push({case:'identity-deadlines',receipt,terminal});

  check(!receipt.inFlight&&receipt.timer===0&&receipt.pending===0&&receipt.cancels===1,'lost search receipt never released or retried itself');

  check(!terminal.inFlight&&terminal.timer===0&&terminal.requests===2&&terminal.cancels===2&&terminal.draftKept,'lost search terminal event kept polling, retried or lost the editor');

 });

 await run('compact refresh startup declares its protocol while keeping ordinary legacy transport',async()=>{

  const result=await page.evaluate(()=>({protocol:window.__startupRefreshArguments?.advisoryRefreshProtocol,library:snapshot.library.games.length,plan:snapshot.steamPlan.items.length}));check(result.protocol===1&&result.library>0&&result.plan>0,'startup did not declare compact refresh protocol or lost legacy startup snapshot');

 });

 await run('compact refresh events coalesce all five advisory paths without adopting old data',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   const immediate=await page.evaluate(()=>{snapshotIO.resetPaint();const name=document.getElementById('identity-name');name.value='Draft kept by compact notice';name.dispatchEvent(new Event('input'));const draft=editDraft;for(const event of ['library-first-run-scan-complete','library-first-run-complete','library-scan-complete','library-plan-refreshed','network-plan-refreshed'])reverseEmit({kind:'event',event,refreshRequested:true,refreshProtocol:1,refreshDocument:1});return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints,draft:editDraft===draft};});await page.clock.runFor(251);

   const queued=await page.evaluate(()=>snapshotIO.reads.length);const final=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints,draft:document.getElementById('identity-name').value};});progressMetrics.push({case:'compact-all-paths',immediate,queued,final});check(immediate.reads===0&&immediate.paints===0&&immediate.draft&&queued===1&&final.reads===1&&final.paints===1&&final.draft==='Draft kept by compact notice','compact notice did not coalesce current reads or replaced the draft');

  }finally{await page.clock.resume();}

 });

 await run('compact refresh invalid versions stay quiet while old snapshot events still refresh',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   await page.evaluate(()=>{for(const marker of [{refreshRequested:true,refreshProtocol:2},{refreshRequested:true,refreshProtocol:'1'},{refreshRequested:'true',refreshProtocol:1},{refreshRequested:false,refreshProtocol:1}])reverseEmit({kind:'event',event:'library-plan-refreshed',...marker});});await page.clock.runFor(1001);const quiet=await page.evaluate(()=>snapshotIO.reads.length);await page.evaluate(()=>reverseEmit({event:'library-plan-refreshed',snapshot:structuredClone(snapshot)}));await page.clock.runFor(251);const legacy=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);return snapshotIO.reads.length;});check(quiet===0&&legacy===1,'invalid compact marker refreshed or legacy host compatibility was lost');

  }finally{await page.clock.resume();}

 });

 await run('compact refresh stays dirty in background and reads once on return',async()=>{

  await installTestClock();await holdNativeSnapshotLane();

  try{

   await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));snapshotIO.resetPaint();for(let i=0;i<32;i++)reverseEmit({kind:'event',event:'library-plan-refreshed',refreshRequested:true,refreshProtocol:1,refreshDocument:1});});await page.clock.runFor(1001);const hidden=await page.evaluate(()=>({reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints}));await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);const visible=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return {reads:snapshotIO.reads.length,paints:snapshotIO.libraryPaints};});check(hidden.reads===0&&hidden.paints===0&&visible.reads===1&&visible.paints===1,'compact background notifications did work or failed to refresh on return');

  }finally{await page.clock.resume();}

 });

 await run('compact refresh native captured envelopes drive the current browser consumer',async()=>{

  const nativeFile=process.argv[5];check(Boolean(nativeFile),'native producer envelopes are required for closed protocol check');const messages=JSON.parse(fs.readFileSync(nativeFile,'utf8'));check(messages.length===2&&messages.every(m=>m.refreshRequested===true&&m.refreshProtocol===1&&!('snapshot'in m)),'closed protocol check did not load actual compact producer output');await installTestClock();await holdNativeSnapshotLane();

  try{

   await page.evaluate(messages=>{snapshotIO.resetPaint();for(const message of messages)reverseEmit(message);},messages);await page.clock.runFor(251);const final=await page.evaluate(async()=>{const reads=snapshotIO.reads.length;if(reads)await snapshotIO.replyState(0);await Promise.resolve();return {reads,paints:snapshotIO.libraryPaints,games:snapshot.library.games.length};});check(final.reads===1&&final.paints===1&&final.games>0,'actual native compact envelope failed current UI consumption');

  }finally{await page.clock.resume();}

 });

 await run('download progress from a new data root starts its own revision sequence',async()=>{
  const rows=[];for(const normalized of [false,true]){if(rows.length)await reset();await beginDownloadPaintProbe();const r=await page.evaluate(normalized=>{const next={...structuredClone(snapshot),dataRoot:'G:/NewProgressRoot',mediaDownloads:{enabled:false,statusRevision:1,total:3,completed:0,remaining:3,phase:'pending',current:{name:'New root progress'},bytes:300,totalBytes:3000}};applyLibrarySnapshot(next,{normalized});return {revision:snapshot.mediaDownloads.statusRevision,current:snapshot.mediaDownloads.current?.name,bar:document.getElementById('media-download-bar').value,root:snapshot.dataRoot};},normalized);rows.push(r);}progressMetrics.push({case:'progress-root-version',rows});
  check(rows.every(r=>r.revision===1&&r.current==='New root progress'&&r.bar===300&&r.root==='G:/NewProgressRoot'),'old-root revision suppressed the new data roots progress');
 });
 await run('snapshot normalization never borrows missing records from another data root',async()=>{
  const r=await page.evaluate(()=>{const old=structuredClone(snapshot);old.config.roots=['G:/RetiredLibrary'];old.manualOverrides.items.retired={name:'Retired override'};old.resolvedIdentities.retired={appId:620};old.steamAccounts={accounts:[{accountId:'retired-account'}]};old.mediaDownloads={statusRevision:999,current:{name:'Retired progress'}};const before=JSON.stringify(old);const next=normalizeWorkspaceSnapshot({dataRoot:'G:/NewProgressRoot',config:{roots:42},library:{games:42},steamPlan:{items:42}},old);return {next,unchanged:JSON.stringify(old)===before};});progressMetrics.push({case:'progress-root-snapshot-fallback',r});
  check(r.unchanged&&r.next.library.games.length===0&&r.next.steamPlan.items.length===0&&Object.keys(r.next.manualOverrides.items).length===0&&Object.keys(r.next.resolvedIdentities).length===0&&r.next.steamAccounts.accounts.length===0&&r.next.config.roots.length===0&&r.next.mediaDownloads===undefined,'new-root snapshot borrowed records or progress from the old root');
 });
 await run('a new data root recovery error cannot borrow another roots progress bar',async()=>{
  await beginDownloadPaintProbe();const r=await page.evaluate(()=>{applyLibrarySnapshot({...structuredClone(snapshot),dataRoot:'G:/NewProgressRoot',mediaDownloads:{recoveryRequired:true,reason:'queue-unreadable',statusRevision:1}});const bar=document.getElementById('media-download-bar');return {reason:snapshot.mediaDownloads.reason,value:bar.value,maximum:bar.max,previous:lastMediaDownloadDisplay};});progressMetrics.push({case:'progress-root-unreadable',r});
  check(r.reason==='queue-unreadable'&&r.value===0&&r.maximum===1&&!r.previous,'new-root recovery error displayed retained progress from another root');
 });
 await run('a recovery read from a retired data root cannot replace current progress',async()=>{
  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();try{
   await page.evaluate(()=>{const actual=invoke;window.progressRootRead={reads:0};invoke=(command,...args)=>command==='mediaDownloadStatus'?(progressRootRead.reads++,new Promise(resolve=>progressRootRead.resolve=resolve)):actual(command,...args);progressRootRead.task=recheckMediaDownloads();snapshot={...snapshot,dataRoot:'G:/NewProgressRoot',mediaDownloads:{enabled:false,statusRevision:1,total:2,completed:0,current:{name:'New root progress'}}};lastMediaDownloadDisplay=null;renderMediaDownloads(snapshot.mediaDownloads);progressRootRead.expected=JSON.stringify(snapshot.mediaDownloads);progressRootRead.resolve({enabled:false,statusRevision:99999,total:2,completed:1,current:{name:'Retired root recovery'}});});await page.evaluate(()=>progressRootRead.task);const r=await page.evaluate(()=>({kept:JSON.stringify(snapshot.mediaDownloads)===progressRootRead.expected,reads:progressRootRead.reads,active:!!mediaRecoveryRequest}));progressMetrics.push({case:'progress-root-recovery-read',r});
   check(r.kept&&r.reads===1&&!r.active,'old-root recovery read replaced current progress');
  }finally{await page.clock.resume();}
 });
 await run('silent state read from a retired data root cannot switch the current library back',async()=>{
  await installTestClock();await holdNativeSnapshotLane();try{
   await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);await page.waitForFunction(()=>snapshotIO.reads.length===1);await page.evaluate(async()=>{snapshot={...structuredClone(snapshot),dataRoot:'G:/NewProgressRoot',mediaDownloads:{enabled:false,statusRevision:1,current:{name:'New root progress'}}};lastMediaDownloadDisplay=null;renderMediaDownloads(snapshot.mediaDownloads);window.progressRootSilentExpected=JSON.stringify({root:snapshot.dataRoot,progress:snapshot.mediaDownloads});await snapshotIO.replyState(0);});const r=await page.evaluate(()=>({kept:JSON.stringify({root:snapshot.dataRoot,progress:snapshot.mediaDownloads})===progressRootSilentExpected,reads:snapshotIO.reads.length,active:!!silentRefreshInFlight}));progressMetrics.push({case:'progress-root-silent-read',r});
   check(r.kept&&r.reads===1&&!r.active,'old-root silent read restored the retired library or progress');
  }finally{await page.clock.resume();}
 });
 await run('manual download receipt from a retired data root cannot replace current progress',async()=>{
  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();try{
   await page.evaluate(()=>{const actual=invoke;window.progressRootManual={writes:0,reads:0};invoke=(command,...args)=>command==='mediaDownloadNow'?(progressRootManual.writes++,new Promise(resolve=>progressRootManual.resolve=resolve)):command==='mediaDownloadStatus'?(progressRootManual.reads++,Promise.resolve(snapshot.mediaDownloads)):actual(command,...args);document.getElementById('media-download-now').click();snapshot={...snapshot,dataRoot:'G:/NewProgressRoot',mediaDownloads:{enabled:false,statusRevision:1,total:2,completed:0,current:{name:'New root progress'}}};lastMediaDownloadDisplay=null;renderMediaDownloads(snapshot.mediaDownloads);progressRootManual.expected=JSON.stringify(snapshot.mediaDownloads);progressRootManual.resolve({enabled:false,statusRevision:99999,total:2,completed:1,current:{name:'Retired manual receipt'},manualUntil:Date.now()+60000});});await page.waitForFunction(()=>!mediaManualBusy&&!mediaRecoveryRequest);const r=await page.evaluate(()=>({kept:JSON.stringify(snapshot.mediaDownloads)===progressRootManual.expected,writes:progressRootManual.writes,reads:progressRootManual.reads,disabled:document.getElementById('media-download-now').disabled}));progressMetrics.push({case:'progress-root-manual-receipt',r});
   check(r.kept&&r.writes===1&&r.reads===1&&!r.disabled,'old-root manual receipt replaced current progress or replayed the write');
  }finally{await page.clock.resume();}
 });
 await run('background data root switch retains only new progress and paints it on focus',async()=>{
  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();try{
   await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));downloadPaintProbe.take();applyLibrarySnapshot({...structuredClone(snapshot),dataRoot:'G:/NewProgressRoot',mediaDownloads:{enabled:false,statusRevision:1,total:2,completed:0,phase:'downloading',current:{name:'New root background progress'},bytes:400,totalBytes:2000}},{deferRender:true});});await page.clock.runFor(33);const hidden=await page.evaluate(()=>downloadPaintProbe.take());await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(33);const visible=await page.evaluate(()=>downloadPaintProbe.take());progressMetrics.push({case:'progress-root-background',hidden,visible});
   check(hidden.mutations===0&&hidden.reads===0&&hidden.recoveryTimer===0&&hidden.current==='New root background progress'&&visible.current==='New root background progress'&&!visible.label.includes('Owned download')&&visible.value===400&&visible.maximum===2000,'background root switch retained foreign progress or painted before focus');
  }finally{await page.clock.resume();}
 });
 await run('same data root aliases preserve the current newer download status',async()=>{
  await page.evaluate(()=>{snapshot={...snapshot,dataRoot:'G:/OwnedProgressRoot'};});await beginDownloadPaintProbe();const r=await page.evaluate(()=>{const revision=snapshot.mediaDownloads.statusRevision,next={...structuredClone(snapshot),dataRoot:snapshot.dataRoot.replaceAll('/','\\').toUpperCase(),mediaDownloads:{enabled:false,statusRevision:0,current:{name:'Old same-root progress'}}};applyLibrarySnapshot(next);return {kept:snapshot.mediaDownloads.statusRevision===revision,current:snapshot.mediaDownloads.current?.name};});
  check(r.kept&&r.current==='Owned download','same-root alias change cleared the newer progress revision');
 });
 await run('snapshot fallback remains within the current data root',async()=>{
  const rows=[];for(const absent of [false,true]){const r=await page.evaluate(absent=>{const old=structuredClone(snapshot),input={dataRoot:old.dataRoot,library:{games:42},steamPlan:{items:42}};old.manualOverrides.items.retained={name:'Owned override'};if(absent)delete input.dataRoot;const next=normalizeWorkspaceSnapshot(input,old);return {root:next.dataRoot,expected:old.dataRoot,games:next.library.games.length,expectedGames:old.library.games.length,plans:next.steamPlan.items.length,expectedPlans:old.steamPlan.items.length,override:next.manualOverrides.items.retained?.name};},absent);rows.push(r);}
  check(rows.every(r=>r.root===r.expected&&r.games===r.expectedGames&&r.plans===r.expectedPlans&&r.override==='Owned override'),'same-root partial or legacy snapshot lost its owned fallback context');
 });
 await run('download progress visible bursts keep current data and paint only the final projection',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();

  try{

   const immediate=await page.evaluate(()=>{for(let i=0;i<32;i++)downloadPaintProbe.emit({bytes:100+i*100,current:{name:'Visible download '+i}});return downloadPaintProbe.take();});await page.clock.runFor(33);const final=await page.evaluate(()=>downloadPaintProbe.take());progressMetrics.push({case:'download-visible-burst',immediate,final});

   check(immediate.mutations===0&&immediate.current==='Visible download 31'&&final.label.includes('Visible download 31')&&final.value===3200&&final.mutations>0&&final.mutations<=6&&final.reads===0,'native progress rebuilt every intermediate foreground status or delayed accepted current data');

  }finally{await page.clock.resume();}

 });

 await run('download progress identical visible projections do not rewrite live status DOM',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();

  try{

   const result=await page.evaluate(()=>{for(let i=0;i<32;i++)downloadPaintProbe.emit({diagnosticSequence:i});return {expected:downloadPaintProbe.nextRevision-1,immediate:downloadPaintProbe.take()};});await page.clock.runFor(33);const final=await page.evaluate(()=>downloadPaintProbe.take());progressMetrics.push({case:'download-identical-projection',...result,final});

   check(final.revision===result.expected&&final.mutations===0&&final.reads===0,'identical download projection repeatedly rewrote the live status region');
   await page.evaluate(()=>downloadPaintProbe.emit({checkpointReadable:true,recoveryRequired:true,reason:'queue-recovery-required',bytes:3300}));await page.clock.runFor(33);const recovered=await page.evaluate(()=>downloadPaintProbe.take());progressMetrics.push({case:'recovered-history-active',...recovered});
   check(recovered.label.includes('Owned download')&&recovered.label.includes('33%')&&recovered.label.includes('历史记录待确认')&&!recovered.label.includes('暂不可读')&&recovered.value===3300,'recovered readable task is obscured by historical recovery warning');
   await page.evaluate(()=>downloadPaintProbe.emit({checkpointReadable:false,recoveryRequired:true,reason:'queue-recovery-required',phase:'deferred',workerPid:0,activeCount:0}));await page.clock.runFor(33);const denied=await page.evaluate(()=>downloadPaintProbe.take());
   check(denied.label.includes('暂不可读')&&!denied.label.includes('Owned download')&&denied.value===3300,'current read failure must retain prior progress without claiming active work');

   const rows=[];
   for(const [name,patch] of [
    ['queued',{remaining:2,completed:34}],
    ['catalog',{catalogPending:true}],
    ['inventory',{inventoryRequestId:'current',inventoryReadyId:'old'}],
    ['paused with work',{remaining:2,completed:34,resourcePauseReason:'cpu-busy'}],
    ['retry with work',{remaining:2,completed:34,retry:true}],
    ['drained stale pause',{resourcePauseReason:'cpu-busy'}],
    ['drained idle',{phase:'idle'}],
    ['drained disabled',{enabled:false}],
    ['empty',{total:0,completed:0,unavailable:0}]
   ]){
    await page.evaluate(({name,patch})=>downloadPaintProbe.emit({enabled:true,total:36,completed:36,remaining:0,failed:0,unavailable:3,phase:'pending',nextDueAt:patch.retry?Date.now()+10000:-1,workerPid:0,activeCount:0,bytes:0,totalBytes:0,current:{},inventoryAwaitUntil:name==='inventory'?Date.now()+10000:0,...patch}),{name,patch});
    await page.clock.runFor(33);rows.push({name,...await page.evaluate(()=>downloadPaintProbe.take())});
   }
   progressMetrics.push({case:'download-pending-and-drained',rows});
   const [queued,catalog,inventory,paused,retry,drained,idle,disabled,empty]=rows;
   check(queued.label.includes('等待下载')&&queued.label.includes('34 / 36')&&!queued.label.includes('已处理')&&queued.value===34&&queued.maximum===36,'pending queue incorrectly projects completion before the Worker claims it');
   check([catalog,inventory].every(row=>row.label.includes('正在准备')&&!row.label.includes('已处理')),'outstanding catalog or inventory work was described as a drained queue');
   check(paused.label.includes('CPU')&&retry.label.includes('等待重试'),'real resource or retry state was hidden behind the waiting label');
   check([drained,idle].every(row=>row.label.includes('本轮已处理')&&row.label.includes('3 个已跳过'))&&disabled.label.includes('已关闭')&&!empty.label.includes('已处理'),'settled, disabled or empty queue labels regressed');


  }finally{await page.clock.resume();}

 });

 await run('download progress background bursts stay data-only and resume one current refresh',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();

  try{

   await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));for(let i=0;i<32;i++)downloadPaintProbe.emit({completed:downloadPaintProbe.initial.completed+Math.floor(i/8)+1,bytes:100+i*100,current:{name:'Hidden download '+i}});});await page.clock.runFor(1001);const hidden=await page.evaluate(()=>({...downloadPaintProbe.take(),dirty:silentRefreshPending,previewPaused:document.querySelector('#artwork-wallpaper-preview video')?.paused}));

   await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);const final=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return downloadPaintProbe.take();});progressMetrics.push({case:'download-background-burst',hidden,final});

   check(hidden.mutations===0&&hidden.reads===0&&hidden.recoveryTimer===0&&hidden.dirty&&hidden.previewPaused&&hidden.current==='Hidden download 31'&&final.reads===1&&final.current==='Hidden download 31'&&final.label.includes('Hidden download 31'),'background download progress painted, polled, lost completion dirty state or failed current resume');

  }finally{await page.clock.resume();}

 });

 await run('download progress stale events and snapshots cannot roll back manual queue status',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();

  try{

   const expected=await page.evaluate(()=>{const fresh=downloadPaintProbe.emit({manualUntil:Date.now()+60000,manualTotal:80,manualCompleted:7,current:{name:'Fresh manual queue'},bytes:4500});const old={...fresh,statusRevision:fresh.statusRevision-1,manualUntil:0,current:{name:'Stale automatic queue'},bytes:1};reverseEmit({kind:'event',event:'media-download-progress',mediaDownloads:old});applyLibrarySnapshot({...structuredClone(snapshot),mediaDownloads:old});return fresh.statusRevision;});await page.clock.runFor(33);const final=await page.evaluate(()=>downloadPaintProbe.take());

   check(final.revision===expected&&final.current==='Fresh manual queue'&&final.label.includes('Fresh manual queue')&&final.button.includes('停止')&&final.value===4500,'older progress or state rolled back current manual ownership/status');

  }finally{await page.clock.resume();}

 });

 await run('download progress pagehide retains completion without DOM timers or reads until pageshow',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();

  try{

   await page.evaluate(()=>{window.dispatchEvent(new Event('pagehide'));for(let i=0;i<32;i++)downloadPaintProbe.emit({completed:downloadPaintProbe.initial.completed+i+1,current:{name:'After pagehide '+i}});});await page.clock.runFor(20001);const closed=await page.evaluate(()=>downloadPaintProbe.take());await page.evaluate(()=>window.dispatchEvent(new Event('pageshow')));await page.clock.runFor(251);const resumed=await page.evaluate(async()=>{if(snapshotIO.reads.length)await snapshotIO.replyState(0);await Promise.resolve();return downloadPaintProbe.take();});progressMetrics.push({case:'download-pagehide',closed,resumed});

   check(closed.mutations===0&&closed.reads===0&&closed.recoveryTimer===0&&closed.current==='After pagehide 31'&&resumed.reads===1&&resumed.current==='After pagehide 31'&&resumed.label.includes('After pagehide 31'),'closed document painted or restarted recovery, or lost retained completion on pageshow');

  }finally{await page.clock.resume();}

 });

 await run('download progress indeterminate control suspends on blur and restores current state on focus',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe({phase:'identifying',totalBytes:0,bytes:0});

  try{

   const before=await page.evaluate(()=>downloadPaintProbe.take());await page.evaluate(()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));});await page.clock.runFor(33);const hidden=await page.evaluate(()=>({...downloadPaintProbe.take(),phase:snapshot.mediaDownloads.phase}));await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(33);const resumed=await page.evaluate(()=>({...downloadPaintProbe.take(),phase:snapshot.mediaDownloads.phase}));progressMetrics.push({case:'download-indeterminate',before,hidden,resumed});

   check(!before.hasValue&&hidden.hasValue&&hidden.phase==='identifying'&&hidden.recoveryTimer===0&&!resumed.hasValue&&resumed.phase==='identifying','inactive native indeterminate animation stayed active or resume changed download state');

  }finally{await page.clock.resume();}

 });

 await run('download progress a late recovery reply after blur preserves data without background painting',async()=>{

  await installTestClock();await holdNativeSnapshotLane();await beginDownloadPaintProbe();

  try{

   await page.evaluate(()=>{const actual=invoke;window.downloadRecoveryProbe={requests:0,resolve:null};invoke=(command,...rest)=>command==='mediaDownloadStatus'?(downloadRecoveryProbe.requests++,new Promise(resolve=>{downloadRecoveryProbe.resolve=resolve;})):actual(command,...rest);window.downloadRecoveryWait=recheckMediaDownloads();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));downloadRecoveryProbe.resolve({...downloadPaintProbe.initial,statusRevision:downloadPaintProbe.nextRevision++,current:{name:'Late recovery result'},bytes:5600});return downloadRecoveryWait;});await page.clock.runFor(20001);const hidden=await page.evaluate(()=>({...downloadPaintProbe.take(),recoveryRequests:downloadRecoveryProbe.requests}));await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(33);const resumed=await page.evaluate(()=>downloadPaintProbe.take());progressMetrics.push({case:'download-late-recovery',hidden,resumed});

   check(hidden.mutations===0&&hidden.recoveryRequests===1&&hidden.recoveryTimer===0&&hidden.current==='Late recovery result'&&resumed.label.includes('Late recovery result')&&resumed.value===5600,'late recovery repainted/polled in background or lost the accepted current result');

  }finally{await page.clock.resume();}

 });

 await run('editor preview accepted playback keeps the existing video and cover nodes',async()=>{

  await beginEditorPreviewProbe();await page.evaluate(()=>reverseChoose(5));await settle();const result=await page.evaluate(()=>({...editorPreviewProbe.take(),saved:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start}));progressMetrics.push({case:'editor-preview-playback-node',...result});check(result.sameVideo&&result.sameCover&&result.saved===5,'playback receipt unnecessarily replaced the bound video/cover or lost the write');

 });

 await run('editor preview identical foreground projections do not rebuild loaded resources',async()=>{

  await beginEditorPreviewProbe();const result=await page.evaluate(()=>{for(let i=0;i<24;i++)renderArtworkDraft();return editorPreviewProbe.take();});progressMetrics.push({case:'editor-preview-identical',...result});check(result.sameVideo&&result.sameCover&&result.mutations===0,'unchanged editor artwork rebuilt loaded nodes or rewrote control attributes');

 });

 await run('editor preview late mode receipt stays data only while backgrounded and resumes current mode',async()=>{
  await beginEditorPreviewProbe();await page.evaluate(()=>{const actual=invoke;invoke=async(command,...rest)=>{const value=await actual(command,...rest);return command==='mediaEdit'?new Promise(resolve=>window.releasePreviewMode=()=>resolve(value)):value;};document.querySelector('[data-shared-wallpaper-mode="static"]').click();});await page.waitForFunction(()=>window.releasePreviewMode);
  await page.evaluate(()=>{editorPreviewProbe.reset();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));releasePreviewMode();});await settle();const hidden=await page.evaluate(()=>editorPreviewProbe.take());await page.evaluate(async()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);});const resumed=await page.evaluate(()=>({mode:selectedActionGame.gameMedia.wallpaperMode,image:!!document.querySelector('#artwork-wallpaper-preview img'),writes:reverseIO.calls.filter(x=>x.command==='mediaEdit').length}));progressMetrics.push({case:'editor-preview-late-mode',hidden,resumed});check(hidden.mutations===0&&hidden.sameWallpaperImage&&hidden.mode==='static'&&resumed.image&&resumed.mode==='static'&&resumed.writes===1,'late wallpaper receipt rebuilt inactive previews or failed to present accepted data on focus');
 });
 await run('editor preview retired video metadata cannot rewrite the current card duration hint',async()=>{

  await beginEditorPreviewProbe();const result=await page.evaluate(async()=>{const retired=editorPreviewProbe.video;reverseOpenOther();await new Promise(requestAnimationFrame);const hint=document.getElementById('shared-wallpaper-duration-hint');hint.textContent='Current editor owns this hint';const owner=keyFor(selectedActionGame);Object.defineProperty(retired,'duration',{configurable:true,value:99});retired.dispatchEvent(new Event('loadedmetadata'));await Promise.resolve();return {kept:hint.textContent==='Current editor owns this hint',ownerKept:keyFor(selectedActionGame)===owner};});check(result.kept&&result.ownerKept,'retired video metadata changed another card presentation');

 });

 await run('editor preview retired image errors cannot start fallback or erase the current card',async()=>{

  await beginEditorPreviewProbe();await page.evaluate(()=>reverseOpenOther());
  await page.waitForFunction(()=>artworkPrefetchPendingCount===0);
  await installPreviewFallbackProbe();
  const result=await page.evaluate(async()=>{const retired=editorPreviewProbe.cover;if(!retired||retired.isConnected)throw Error('detached owned cover image required');const target=document.getElementById('artwork-cover-preview'),before=target.innerHTML,owner=keyFor(selectedActionGame);retired.dispatchEvent(new Event('error'));await new Promise(requestAnimationFrame);return {kept:target.innerHTML===before,ownerKept:keyFor(selectedActionGame)===owner,searches:previewFallbackProbe.searches,downloads:previewFallbackProbe.downloads};});
  progressMetrics.push({case:'retired-image-error-owner',result});
  check(result.kept&&result.ownerKept&&result.searches===0&&result.downloads===0,'retired image error crossed card ownership or started unrelated fallback');

 });

 await run('editor preview a current image still reaches the legitimate automatic fallback',async()=>{
  if(automaticFallbackScope==='all'){


  await beginEditorPreviewProbe();await installPreviewFallbackProbe();await page.evaluate(()=>editorPreviewProbe.cover.dispatchEvent(new Event('error')));await page.waitForFunction(()=>artworkDraft.cover?.state==='set'&&artworkDraft.dirty.cover);const result=await page.evaluate(()=>({provider:artworkDraft.cover.asset.provider,searches:previewFallbackProbe.searches,downloads:previewFallbackProbe.downloads}));check(result.provider==='playnite-igdb'&&result.searches===1&&result.downloads===1,'preview ownership gate rejected a current valid automatic fallback');


  const collisionRows=await page.evaluate(async()=>{
   const actual=invoke,oldFocus=document.hasFocus,base=structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),rows=[];let mode='',calls=[],heldVideo=[],heldAutomatic=[];
   const flush=async()=>{for(let i=0;i<16;i++)await Promise.resolve();};
   const videoResult=()=>{const entry=selectedActionGame.gameMedia;return {appId:570,entryKey:entry.key,bindingRevision:entry.bindingRevision,query:'Owned video query',searchToken:'owned-gallery',candidates:[{id:'0',title:'Owned video A',previewUrl:'/wallpaper.jpg'},{id:'1',title:'Owned video B',previewUrl:'/header.jpg'}]};};
   invoke=(command,args={},timeout=0)=>{
    calls.push({command,args:structuredClone(args)});
    if(command==='searchArtwork')return Promise.resolve({query:args.query,candidates:[{provider:args.type==='cover'?'playnite-igdb':'steam-store',url:'http://127.0.0.1:13139/'+(args.type==='cover'?'cover.jpg?owned-auto':'header.jpg?owned-manual'),previewUrl:'/cover.jpg'}]});
    if(command==='downloadArtworkCandidate'){
     const value={path:'G:/H5Fixture/'+(String(args.url).includes('owned-auto')?'automatic-cover':'manual-long')+'.jpg',width:600,height:900};
     if(mode==='automatic before image gallery'&&String(args.url).includes('owned-auto'))return new Promise(resolve=>heldAutomatic.push(()=>resolve(value)));
     return Promise.resolve(value);
    }
    if(command==='mediaSearchVideos'){const result=videoResult();return mode==='image fails during video search'?new Promise(resolve=>heldVideo.push(()=>resolve(result))):Promise.resolve(result);}
    return actual(command,args,timeout);
   };
   const stats=()=>({coverReads:calls.filter(r=>r.command==='searchArtwork'&&r.args.type==='cover').length,automaticDownloads:calls.filter(r=>r.command==='downloadArtworkCandidate'&&String(r.args.url).includes('owned-auto')).length,manualDownloads:calls.filter(r=>r.command==='downloadArtworkCandidate'&&String(r.args.url).includes('owned-manual')).length,selections:calls.filter(r=>r.command==='mediaSelectVideo').length,candidates:document.querySelectorAll('[data-video-candidate]').length,selected:artworkDraft.long?.asset?.path,coverProvider:artworkDraft.cover?.asset?.provider,coverDirty:artworkDraft.dirty.cover,globalDownload:downloadingCandidateUrl,taskBusy:artworkTaskInFlight});
   const setup=async()=>{closeEdit();cancelArtworkTask();document.hasFocus=()=>true;snapshot=structuredClone(base);openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();artworkPrefetchCache.clear();clearNotice();calls=[];heldVideo=[];heldAutomatic=[];};
   try{for(const next of ['image fails during video search','image fails before video selection','automatic before image gallery','deferred image resumes after image Back','foreground return keeps active video gallery']){
    mode=next;await setup();const cover=document.querySelector('#artwork-cover-preview img');if(!cover)throw Error('gallery-cover-fixture-missing');
    if(mode==='automatic before image gallery'){
     cover.dispatchEvent(new Event('error'));await flush();const before=stats();await searchArtwork('long');await flush();const button=document.querySelector('#artwork-search-list .artwork-candidate');button.click();await flush();const foreground=stats();for(const release of heldAutomatic)release();await flush();rows.push({mode,before,foreground,after:stats()});continue;
    }
    const task=mode==='deferred image resumes after image Back'?searchArtwork('long'):actOnSharedWallpaper('search');await flush();
    if(mode==='foreground return keeps active video gallery'){document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
    const token=artworkTaskToken;cover.dispatchEvent(new Event('error'));await flush();
    if(mode==='foreground return keeps active video gallery'){document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await flush();}
    const during={...stats(),tokenKept:token===artworkTaskToken};
    for(const release of heldVideo)release();await task;await flush();const ready=stats();
    if(mode==='image fails before video selection'){document.querySelector('[data-video-candidate]')?.click();await flush();}
    const selected=stats();cancelArtworkTask();await flush();const after=stats();rows.push({mode,during,ready,selected,after});
   }}finally{for(const release of heldVideo)release();for(const release of heldAutomatic)release();await flush();invoke=actual;document.hasFocus=oldFocus;closeEdit();}
   return rows;
  });progressMetrics.push({case:'fallback-gallery-priority',rows:collisionRows});
  check(collisionRows.length===5,'automatic/gallery matrix incomplete');
  check(collisionRows.every(r=>r.mode==='automatic before image gallery'?r.before.automaticDownloads===1&&r.foreground.manualDownloads===1&&r.after.selected==='G:/H5Fixture/manual-long.jpg'&&!r.after.globalDownload&&!r.after.taskBusy:r.during.coverReads===0&&r.during.automaticDownloads===0&&r.during.tokenKept&&(r.mode==='deferred image resumes after image Back'||r.ready.candidates===2)&&(r.mode!=='image fails before video selection'||r.selected.selections===1)&&r.after.coverReads===1&&r.after.automaticDownloads===1&&r.after.coverProvider==='playnite-igdb'&&r.after.coverDirty&&!r.after.globalDownload&&!r.after.taskBusy),'automatic editor fallback invalidated or blocked the foreground gallery, or did not resume once after Back');


  const serialRows=await page.evaluate(async()=>{
   const actual=invoke,oldFocus=document.hasFocus,rows=[];let armed=false,requests=[],events=[],activeReads=0,activeDownloads=0,maxReads=0,maxDownloads=0,cancels=0;
   const flush=async()=>{for(let i=0;i<48;i++)await Promise.resolve();};
   invoke=(command,args={},timeout=0)=>{
    if(armed&&['searchArtwork','downloadArtworkCandidate'].includes(command))return new Promise((resolve,reject)=>{const read=command==='searchArtwork';if(read)maxReads=Math.max(maxReads,++activeReads);else maxDownloads=Math.max(maxDownloads,++activeDownloads);events.push((read?'Q:':'D:')+args.type);requests.push({command,args:structuredClone(args),resolve,reject,done:false,read});});
    if(command==='searchArtwork')return Promise.resolve({candidates:[]});
    if(command==='cancelArtwork')cancels++;
    if(command==='state'||command==='plan')return Promise.resolve(structuredClone(snapshot));return actual(command,args,timeout);
   };
   const setup=async()=>{armed=false;closeEdit();cancelArtworkTask();silentRefreshClosed=false;document.hasFocus=()=>true;snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/SerialAutomaticArtwork'};openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();selectedActionGame.gameMedia.wallpaperMode='static';renderArtworkDraft();artworkPrefetchCache.clear();clearNotice();requests=[];events=[];activeReads=activeDownloads=maxReads=maxDownloads=cancels=0;armed=true;};
   const reply=(r,fail=false)=>{r.done=true;if(r.read)activeReads--;else activeDownloads--;if(fail)r.reject(Error('owned serial first download failed'));else r.resolve(r.read?{candidates:[{provider:'playnite-igdb',url:'http://127.0.0.1:13139/cover.jpg?serial-'+r.args.type}]}:{path:'G:/H5Fixture/serial-'+r.args.type+'.jpg',width:600,height:900});};
   const drain=async fail=>{for(let i=0;i<30;i++){await flush();const r=requests.find(r=>!r.done);if(!r)return;reply(r,fail&&!r.read&&r.args.type==='cover');}throw Error('serial-drain-unbounded');};
   const states=()=>Object.fromEntries(['cover','long','wallpaper'].map(type=>[type,{state:artworkDraft[type]?.state,dirty:artworkDraft.dirty[type],error:!!artworkDraft[type]?.previewError,path:artworkDraft[type]?.asset?.path}]));
   try{for(const mode of ['three previews','first download fails','background handback','same EXE owner change','new root owner change','stale image root']){
    await setup();const images=['cover','long','wallpaper'].map(t=>document.querySelector('#artwork-'+t+'-preview img'));if(images.some(v=>!v))throw Error('serial-three-image-fixture-missing');
    if(mode==='stale image root'){snapshot={...snapshot,dataRoot:'G:/DifferentSerialRoot'};images[0].dispatchEvent(new Event('error'));await flush();const started=requests.length;await drain();rows.push({mode,started});continue;}
    images.forEach(image=>image.dispatchEvent(new Event('error')));await flush();const initial={events:[...events],activeReads,activeDownloads,busy:artworkTaskInFlight};
    if(mode==='same EXE owner change'||mode==='new root owner change'){
     const game=structuredClone(selectedActionGame);if(mode==='same EXE owner change')game.gameDirectory+='-other';else snapshot={...snapshot,dataRoot:'G:/NewSerialRoot'};openEdit(game);const releasedBeforeReply=!artworkTaskInFlight&&!downloadingCandidateUrl;await drain();rows.push({mode,initial,releasedBeforeReply,cancels,states:states(),events:[...events]});continue;
    }
    let background;
    if(mode==='background handback'){document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));const before=['cover','long','wallpaper'].map(t=>document.getElementById('artwork-'+t+'-preview').innerHTML);for(let i=0;i<2;i++){const r=requests.find(r=>!r.done);if(!r)break;reply(r);await flush();}background={events:[...events],kept:before.every((s,i)=>s===document.getElementById('artwork-'+['cover','long','wallpaper'][i]+'-preview').innerHTML)};document.hasFocus=()=>true;resumeEditorArtworkPresentation();}
    await drain(mode==='first download fails');rows.push({mode,initial,background,events:[...events],maxReads,maxDownloads,states:states(),busy:artworkTaskInFlight,url:downloadingCandidateUrl});
   }}finally{armed=false;for(const r of requests)if(!r.done)reply(r);await flush();invoke=actual;document.hasFocus=oldFocus;closeEdit();}return rows;
  });progressMetrics.push({case:'automatic-artwork-serial',rows:serialRows});
  check(serialRows.every(r=>r.mode==='stale image root'?r.started===0:r.mode.includes('owner change')?r.releasedBeforeReply&&r.cancels===1&&Object.values(r.states).every(v=>!v.dirty):r.initial.events.join('|')==='Q:cover'&&r.maxReads===1&&r.maxDownloads===1&&!r.busy&&!r.url&&r.events.join('|')==='Q:cover|D:cover|Q:long|D:long|Q:wallpaper|D:wallpaper'&&Object.entries(r.states).every(([type,v])=>r.mode==='first download fails'&&type==='cover'?!v.dirty&&v.error:v.dirty&&v.path==='G:/H5Fixture/serial-'+type+'.jpg')&&(r.mode!=='background handback'||r.background.kept&&r.background.events.join('|')==='Q:cover|D:cover')),'automatic previews overlapped/retired one another, failed to drain serially, or borrowed an old owner');

  await reset();
  }

  const lost=await page.evaluate(async()=>{
   const actual=invoke,post=chrome.webview.postMessage,set=window.setTimeout,focus=document.hasFocus,held=[],timers=[];const flush=async()=>{for(let i=0;i<48;i++)await Promise.resolve();};let calls=0;
   closeEdit();cancelArtworkTask();document.hasFocus=()=>true;snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/LostAutomaticReceipt'};
   invoke=(command,args={},timeout=0)=>command==='searchArtwork'?Promise.resolve({candidates:[{provider:'playnite-igdb',url:'http://127.0.0.1:13139/cover.jpg?lost-auto'}]}):command==='state'||command==='plan'?Promise.resolve(structuredClone(snapshot)):actual(command,args,timeout);
   chrome.webview.postMessage=message=>{if(message.command==='downloadArtworkCandidate'){calls++;held.push(structuredClone(message));return;}return post.call(chrome.webview,message);};
   window.setTimeout=(callback,delay,...args)=>{const handle=set(callback,delay,...args);if(delay===ARTWORK_SEARCH_TIMEOUT_MS)timers.push({callback,handle,delay});return handle;};
   try{openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();artworkPrefetchCache.clear();document.querySelector('#artwork-cover-preview img').dispatchEvent(new Event('error'));await flush();const before={calls,timers:timers.length,delay:timers[0]?.delay,pending:held.filter(m=>pending.has(m.id)).length};for(const t of timers){clearTimeout(t.handle);t.callback();}await flush();const expired={busy:artworkTaskInFlight,url:downloadingCandidateUrl,error:!!artworkDraft.cover?.previewError,dirty:artworkDraft.dirty.cover,pending:held.filter(m=>pending.has(m.id)).length};for(const m of held)reverseEmit({kind:'response',id:m.id,ok:true,result:{path:'G:/H5Fixture/late-auto.jpg'}});await flush();return {before,expired,calls,lateKept:!artworkDraft.dirty.cover&&!!artworkDraft.cover.previewError};}
   finally{for(const t of timers)clearTimeout(t.handle);invoke=actual;chrome.webview.postMessage=post;window.setTimeout=set;document.hasFocus=focus;closeEdit();}
  });progressMetrics.push({case:'automatic-artwork-lost-receipt',...lost});
  check(lost.before.calls===1&&lost.before.timers===1&&lost.before.delay===185000&&lost.before.pending===1&&!lost.expired.busy&&!lost.expired.url&&lost.expired.error&&!lost.expired.dirty&&lost.expired.pending===0&&lost.calls===1&&lost.lateKept,'lost automatic download receipt held the serial guard, replayed work or consumed a late response');
 });

 await run('editor preview native artwork refresh preserves unsaved fields and newer wallpaper writes',async()=>{

  await beginNativeArtworkProbe();await page.evaluate(async()=>{await updateSharedWallpaper('mode',{mode:'static'});window.latestNativeMode=selectedActionGame.gameMedia.bindingRevision;document.getElementById('identity-name').value='Keep native draft input';document.getElementById('identity-name').dispatchEvent(new Event('input'));editDraft.bucket='non-game';clearArtwork('cover');nativeArtworkProbe.reply(0,'fresh-native');await new Promise(requestAnimationFrame);});const result=await page.evaluate(()=>({name:document.getElementById('identity-name').value,bucket:editDraft.bucket,draftName:editDraft.game.override?.name,deleted:artworkDraft.cover.state==='deleted'&&artworkDraft.dirty.cover,mode:selectedActionGame.gameMedia.wallpaperMode,mediaRevision:snapshot.gameMedia.entries['steam-570'].bindingRevision,expectedRevision:latestNativeMode,long:document.querySelector('#artwork-long-preview img')?.getAttribute('src'),reads:reverseIO.calls.filter(x=>x.command==='state').length}));progressMetrics.push({case:'editor-preview-native-draft',...result});check(result.name==='Keep native draft input'&&result.draftName==='Keep native draft input'&&result.bucket==='non-game'&&result.deleted&&result.mode==='static'&&result.mediaRevision===result.expectedRevision&&result.long?.includes('fresh-native')&&result.reads===0,'native artwork refresh replaced unsaved draft/newer media or failed to update inherited artwork');

 });

 await run('editor preview native artwork replies in background defer only presentation',async()=>{

  await beginNativeArtworkProbe();await beginEditorPreviewProbe();const hidden=await page.evaluate(async()=>{document.getElementById('identity-name').value='Background native input';document.getElementById('identity-name').dispatchEvent(new Event('input'));document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));nativeArtworkProbe.reply(0,'background-native');await new Promise(requestAnimationFrame);return {...editorPreviewProbe.take(),name:document.getElementById('identity-name').value};});await page.evaluate(async()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);});const resumed=await page.evaluate(()=>({name:document.getElementById('identity-name').value,long:document.querySelector('#artwork-long-preview img')?.getAttribute('src')}));progressMetrics.push({case:'editor-preview-native-background',hidden,resumed});check(hidden.mutations===0&&hidden.name==='Background native input'&&resumed.name==='Background native input'&&resumed.long?.includes('background-native'),'background native artwork callback painted/reset fields or failed foreground resume');

 });

 await run('editor preview a closed and reopened native editor rejects the retired refresh',async()=>{

  await beginNativeArtworkProbe();await page.evaluate(()=>{closeEdit();nativeArtworkProbe.open();document.getElementById('identity-name').value='New native session';document.getElementById('identity-name').dispatchEvent(new Event('input'));});await page.waitForFunction(()=>nativeArtworkProbe.requests.length===2&&nativeArtworkProbe.metadata===0);const first=await page.evaluate(async()=>{nativeArtworkProbe.reply(0,'retired-native');await new Promise(requestAnimationFrame);return {name:document.getElementById('identity-name').value,long:document.querySelector('#artwork-long-preview img')?.getAttribute('src')};});const second=await page.evaluate(async()=>{nativeArtworkProbe.reply(1,'current-native');await new Promise(requestAnimationFrame);return {name:document.getElementById('identity-name').value,long:document.querySelector('#artwork-long-preview img')?.getAttribute('src')};});check(first.name==='New native session'&&!first.long?.includes('retired-native')&&second.name==='New native session'&&second.long?.includes('current-native'),'same-EXE reopened editor accepted a retired refresh or lost its current draft');

 });

 await run('editor preview changing mode without a static asset removes the previous video',async()=>{
  await beginEditorPreviewProbe();await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].staticPreviewUrl='';document.querySelector('[data-shared-wallpaper-mode="static"]').click();});await settle();const off=await page.evaluate(()=>({video:!!document.querySelector('#artwork-wallpaper-preview video'),mode:selectedActionGame.gameMedia.wallpaperMode}));await page.evaluate(()=>document.querySelector('[data-shared-wallpaper-mode="dynamic"]').click());await settle();const on=await page.evaluate(()=>({video:!!document.querySelector('#artwork-wallpaper-preview video'),mode:selectedActionGame.gameMedia.wallpaperMode}));check(!off.video&&off.mode==='static'&&on.video&&on.mode==='dynamic','artwork projection retained the wrong media kind across a legitimate mode change');
 });
 await run('editor preview retires an unavailable video even when its mapped URL is retained',async()=>{
  await beginEditorPreviewProbe();
  const r=await page.evaluate(()=>{
   const base=structuredClone(selectedActionGame.gameMedia),video=base.assets.dynamic,key=base.key;
   const preview=document.getElementById('artwork-wallpaper-preview'),old=preview.querySelector('video');
   const invalid={...base,bindingRevision:base.bindingRevision+1,dynamicReady:false,invalidSlots:{dynamic:video.id},assets:{...base.assets,dynamic:{...video,selectedHeight:1080}},asset:base.assets.static,previewUrl:base.staticPreviewUrl};
   const apply=entry=>{snapshot.gameMedia.entries[key]=entry;selectedActionGame.gameMedia=entry;presentSharedWallpaperEditor(true);};
   const state=()=>({video:preview.querySelector('video')?.getAttribute('src')||'',image:preview.querySelector('img')?.getAttribute('src')||'',text:preview.textContent,title:preview.title,mode:sharedWallpaperMode(),oldConnected:old.isConnected,oldSource:old.getAttribute('src')});
   document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));editorPreviewProbe.reset();apply(invalid);
   const hidden={...state(),...editorPreviewProbe.take(),deferred:editorArtworkRenderPending};
   document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));const settled=state();
   apply({...invalid,dynamicReady:undefined});const markerOnly=state();
   apply({...invalid,invalidSlots:{}});const missingDisk=state();
   apply({...invalid,invalidSlots:{dynamic:video.id,static:base.assets.static.id}});const noFallback=state();
   apply({...invalid,deletedSlots:{dynamic:true},dynamicReady:true});const deleted=state();
   apply({...invalid,dynamicReady:true,bindingRevision:invalid.bindingRevision+1,assets:{...invalid.assets,dynamic:{...video,id:'recovered-preview',selectedHeight:720}},dynamicPreviewUrl:'/sample.mp4?owned-recovered=1'});const recovered=state();
   apply({...base,assets:undefined,dynamicReady:undefined,dynamicPreviewUrl:undefined,asset:video,previewUrl:base.dynamicPreviewUrl});const legacy=state();
   return {hidden,settled,markerOnly,missingDisk,noFallback,deleted,recovered,legacy,staticUrl:base.staticPreviewUrl,legacyUrl:base.dynamicPreviewUrl,mediaWrites:reverseIO.calls.filter(c=>['mediaEdit','mediaSelectVideo','mediaImport','mediaDownloadNow'].includes(c.command)).length};
  });progressMetrics.push({case:'editor-preview-unavailable-video',r});
  check(r.hidden.mutations===0&&r.hidden.deferred&&r.hidden.oldConnected&&r.hidden.video,'background invalidation touched the retained preview before focus');
  check(!r.settled.video&&!r.settled.oldConnected&&r.settled.oldSource===null&&r.settled.image===r.staticUrl&&!r.settled.title&&r.settled.mode==='dynamic','editor kept the decoder attached to a video already marked unavailable');
  check([r.markerOnly,r.missingDisk].every(row=>!row.video&&row.image===r.staticUrl),'exact invalid marker or missing-file receipt did not choose the static fallback');
  check(!r.noFallback.video&&!r.noFallback.image&&r.noFallback.text.includes('无法预览'),'editor reused an invalid static fallback or omitted the unavailable state');
  check(!r.deleted.video&&!r.deleted.image&&r.deleted.text.includes('暂无动态壁纸'),'deleted dynamic slot resurrected a stale preview');
  check(r.recovered.video==='/sample.mp4?owned-recovered=1'&&r.recovered.title.endsWith('720p')&&!r.recovered.image,'previous invalidation blocked the newer video asset');
  check(r.legacy.video===r.legacyUrl&&r.mediaWrites===0,'legacy playable video was lost or preview fallback wrote user settings');
 });
 await run('editor preview a background image failure waits for focus before automatic fallback',async()=>{

  await beginEditorPreviewProbe();await installPreviewFallbackProbe();const hidden=await page.evaluate(async()=>{document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));editorPreviewProbe.cover.dispatchEvent(new Event('error'));await new Promise(requestAnimationFrame);return {...editorPreviewProbe.take(),searches:previewFallbackProbe.searches};});await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.waitForFunction(()=>artworkDraft.cover?.state==='set'&&artworkDraft.dirty.cover);const resumed=await page.evaluate(()=>({searches:previewFallbackProbe.searches,downloads:previewFallbackProbe.downloads,provider:artworkDraft.cover.asset.provider}));progressMetrics.push({case:'editor-preview-deferred-error',hidden,resumed});check(hidden.mutations===0&&hidden.searches===0&&resumed.searches===1&&resumed.downloads===1&&resumed.provider==='playnite-igdb','background image error painted/started fallback or dropped the valid foreground retry');

 });

 await run('editor preview late fallback failure retains its error without inactive DOM rebuild',async()=>{

  await beginEditorPreviewProbe();await installPreviewFallbackProbe(true);await page.evaluate(()=>editorPreviewProbe.cover.dispatchEvent(new Event('error')));await page.waitForFunction(()=>previewFallbackProbe.reject);const hidden=await page.evaluate(async()=>{editorPreviewProbe.observer.takeRecords();document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));let mutations=0;const observer=new MutationObserver(rows=>mutations+=rows.length);observer.observe(document.getElementById('artwork-cover-preview'),{childList:true,subtree:true,attributes:true});previewFallbackProbe.reject(Error('owned fallback unavailable'));await new Promise(requestAnimationFrame);mutations+=observer.takeRecords().length;observer.disconnect();return {mutations,searches:previewFallbackProbe.searches};});await page.evaluate(async()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));await new Promise(requestAnimationFrame);});const resumed=await page.evaluate(()=>({label:document.getElementById('artwork-cover-preview').textContent,searches:previewFallbackProbe.searches}));check(hidden.mutations===0&&hidden.searches===1&&resumed.searches===1&&resumed.label.includes('加载失败'),'late fallback failure painted while inactive, vanished on resume or retried blindly');

 });

 await run('editor preview native artwork rejects another directory and AppID using the same EXE',async()=>{
  await beginNativeArtworkProbe();const result=await page.evaluate(async()=>{const before=document.querySelector('#artwork-long-preview img')?.getAttribute('src'),plan=nativeArtworkProbe.requests[0].response.steamPlan.items.find(item=>normalizedUiPath(item.primaryExecutable)===nativeArtworkProbe.exe);plan.gameDirectory='G:/Fixture/ForeignNative';plan.nativeSteamAppId=620;plan.appId=620;nativeArtworkProbe.reply(0,'foreign-native');await new Promise(requestAnimationFrame);return {before,long:document.querySelector('#artwork-long-preview img')?.getAttribute('src'),directory:keyFor(snapshot.steamPlan.items.find(item=>normalizedUiPath(item.primaryExecutable)===nativeArtworkProbe.exe)),expected:nativeArtworkProbe.key};});check(result.long===result.before&&result.directory===result.expected,'foreign-directory/AppID native artwork crossed an EXE-only match');
 });
 await run('editor preview native artwork retains a matching legacy EXE and AppID without directory',async()=>{
  await beginNativeArtworkProbe();const result=await page.evaluate(async()=>{const plan=nativeArtworkProbe.requests[0].response.steamPlan.items.find(item=>normalizedUiPath(item.primaryExecutable)===nativeArtworkProbe.exe);delete plan.gameDirectory;delete plan.nativeSteamAppId;nativeArtworkProbe.reply(0,'legacy-native');await new Promise(requestAnimationFrame);return document.querySelector('#artwork-long-preview img')?.getAttribute('src');});check(result?.includes('legacy-native'),'valid matching legacy native artwork was rejected');
 });
 await run('editor preview a retired fallback exits cleanly after draft rebase or an explicit slot replacement',async()=>{
  await beginEditorPreviewProbe();const rows=[];
  for(const action of ['rebase','replacement']){
   await installPreviewFallbackProbe(true);await page.evaluate(()=>document.querySelector('#artwork-cover-preview img').dispatchEvent(new Event('error')));await page.waitForFunction(()=>previewFallbackProbe.reject);
   const row=await page.evaluate(async action=>{const original=editDraft;if(action==='rebase'){editDraft.bucket='non-game';await persistEditDraft();}else{artworkDraft.cover={state:'set',asset:{url:'/cover.jpg',provider:'manual'}};artworkDraft.dirty.cover=true;renderArtworkDraft();}previewFallbackProbe.reject(Error('retired fallback result'));await new Promise(requestAnimationFrame);return {action,draftChanged:editDraft!==original,image:!!document.querySelector('#artwork-cover-preview img'),text:document.getElementById('artwork-cover-preview').textContent,searches:previewFallbackProbe.searches,manualKept:artworkDraft.cover.state==='set'&&artworkDraft.dirty.cover&&artworkDraft.cover.asset.provider==='manual'};},action);rows.push(row);check(row.image&&!row.text.includes('正在切换')&&row.searches===1&&(action==='rebase'?row.draftChanged:row.manualKept),'retired fallback stranded a loading placeholder or replaced a newer editor choice');
  }progressMetrics.push({case:'editor-preview-retired-fallback-exit',rows});

  const ownerRows=await page.evaluate(async()=>{
   const actual=invoke,oldFocus=document.hasFocus,rows=[];let armed=false,stage='',slotType='',queries=[],downloads=[];
   const flush=async()=>{for(let i=0;i<40;i++)await Promise.resolve();};
   const candidates=()=>({candidates:[{provider:'playnite-igdb',url:'http://127.0.0.1:13139/cover.jpg?owned-automatic'}]});
   invoke=(command,args={},timeout=0)=>{
    if(command==='searchArtwork')return armed&&args.type===slotType&&stage==='query'?new Promise((resolve,reject)=>queries.push({args:structuredClone(args),resolve,reject})):Promise.resolve(armed?candidates():{candidates:[]});
    if(command==='downloadArtworkCandidate'){const value={path:'G:/H5Fixture/current-auto-'+slotType+'.jpg',width:600,height:900};if(stage==='download')return new Promise((resolve,reject)=>downloads.push({args:structuredClone(args),resolve,reject}));downloads.push({args:structuredClone(args)});return Promise.resolve(value);}
    if(command==='state'||command==='plan')return Promise.resolve(structuredClone(snapshot));return actual(command,args,timeout);
   };
   const setup=async type=>{armed=false;closeEdit();cancelArtworkTask();silentRefreshClosed=false;document.hasFocus=()=>true;snapshot={...structuredClone(__LIBRARY_H5_FIXTURE__.snapshot),dataRoot:'G:/OwnedAutomaticArtwork'};openEdit(mergedGames().find(g=>g.primaryExecutable&&String(formalSteamAppId(g))==='570'));await flush();if(type==='wallpaper'){selectedActionGame.gameMedia.wallpaperMode='static';renderArtworkDraft();}artworkPrefetchCache.clear();clearNotice();noticeQueue.length=0;toastNotice.textContent='';queries=[];downloads=[];armed=true;};
   const change=mode=>{
    if(mode.includes('root replaced'))snapshot={...snapshot,dataRoot:'G:/ReplacementAutomaticArtwork'};
    if(mode.includes('root alias'))snapshot={...snapshot,dataRoot:'g:\\ownedautomaticartwork\\'};
    if(mode.includes('draft name')){$('#identity-name').value='Current unsaved query';$('#identity-name').dispatchEvent(new Event('input',{bubbles:true}));}
    if(mode.includes('draft ID')){$('#identity-steam-id').value='620';$('#identity-steam-id').dispatchEvent(new Event('input',{bubbles:true}));}
    if(mode==='page retired')window.dispatchEvent(new Event('pagehide'));
    if(mode==='background'){document.hasFocus=()=>false;window.dispatchEvent(new Event('blur'));}
   };
   try{for(const type of ['cover','long','wallpaper'])for(const phase of ['query','download'])for(const mode of ['current','root alias','root replaced','draft name','draft ID','page retired','background','error current','error root replaced']){
    slotType=type;stage=phase;await setup(type);const target=document.getElementById('artwork-'+type+'-preview'),image=target.querySelector('img');if(!image)throw Error('automatic-image-fixture-missing:'+type);image.dispatchEvent(new Event('error'));await flush();if((phase==='query'?queries:downloads).length!==1)throw Error('automatic-owner-hold-missing:'+phase);
    const oldAsset=JSON.stringify(artworkDraft[type]?.asset);change(mode);const beforeMarkup=target.innerHTML;
    if(phase==='query'){if(mode.startsWith('error'))queries[0].reject(Error('owned automatic search failed'));else queries[0].resolve(candidates());}
    else if(mode.startsWith('error'))downloads[0].reject(Error('owned automatic download failed'));else downloads[0].resolve({path:'G:/H5Fixture/current-auto-'+type+'.jpg',width:600,height:900});await flush();
    const r={type,phase,mode,downloads:downloads.length,dirty:artworkDraft.dirty[type],state:artworkDraft[type]?.state,path:artworkDraft[type]?.asset?.path,error:!!artworkDraft[type]?.previewError,oldAssetKept:oldAsset===JSON.stringify(artworkDraft[type]?.asset),markupKept:target.innerHTML===beforeMarkup,notice:[toastNotice.textContent,...noticeQueue.map(n=>n.message)].some(n=>n.includes('备用图片')),busy:artworkTaskInFlight,url:downloadingCandidateUrl};
    if(mode==='background'){document.hasFocus=()=>true;resumeEditorArtworkPresentation();r.resumed=target.innerHTML!==beforeMarkup;}rows.push(r);
   }
   for(const type of ['cover','long','wallpaper'])for(const value of [{},{path:''},{path:'   '},{path:{file:'not-a-path'}}]){
    slotType=type;stage='download';await setup(type);const target=document.getElementById('artwork-'+type+'-preview'),image=target.querySelector('img'),oldAsset=JSON.stringify(artworkDraft[type]?.asset);image.dispatchEvent(new Event('error'));await flush();downloads[0].resolve(value);await flush();rows.push({type,phase:'download',mode:'invalid receipt',value,downloads:downloads.length,dirty:artworkDraft.dirty[type],state:artworkDraft[type]?.state,error:!!artworkDraft[type]?.previewError,oldAssetKept:oldAsset===JSON.stringify(artworkDraft[type]?.asset),notice:[toastNotice.textContent,...noticeQueue.map(n=>n.message)].some(n=>n.includes('备用图片')),busy:artworkTaskInFlight,url:downloadingCandidateUrl});
   }}finally{armed=false;invoke=actual;document.hasFocus=oldFocus;closeEdit();silentRefreshClosed=false;}return rows;
  });progressMetrics.push({case:'automatic-artwork-owner',rows:ownerRows});
  check(ownerRows.length===66,'automatic artwork owner matrix incomplete');
  check(ownerRows.every(r=>!r.busy&&!r.url&&(['current','root alias','background'].includes(r.mode)?r.downloads===1&&r.dirty&&r.path==='G:/H5Fixture/current-auto-'+r.type+'.jpg'&&!r.error&&(r.mode!=='background'||r.markupKept&&r.resumed):['error current','invalid receipt'].includes(r.mode)?!r.dirty&&r.oldAssetKept&&r.error&&r.notice:!r.dirty&&r.oldAssetKept&&!r.error&&!r.notice&&r.downloads===(r.phase==='query'?0:1)&&(!r.mode.includes('root replaced')&&r.mode!=='page retired'||r.markupKept))),'automatic artwork result/error crossed query/root/page ownership, accepted a missing path, or lost current background completion');

 });
 await run('editor preview an explicit native refresh reloads same-URL images without replacing the video',async()=>{
  await beginNativeArtworkProbe();const result=await page.evaluate(async()=>{const cover=document.querySelector('#artwork-cover-preview img'),long=document.querySelector('#artwork-long-preview img'),video=document.querySelector('#artwork-wallpaper-preview video'),url=long?.getAttribute('src');nativeArtworkProbe.requests[0].resolve(structuredClone(nativeArtworkProbe.requests[0].response));await new Promise(requestAnimationFrame);return {coverReloaded:cover!==document.querySelector('#artwork-cover-preview img'),longReloaded:long!==document.querySelector('#artwork-long-preview img'),videoKept:video===document.querySelector('#artwork-wallpaper-preview video'),sameUrl:url===document.querySelector('#artwork-long-preview img')?.getAttribute('src')};});check(result.coverReloaded&&result.longReloaded&&result.videoKept&&result.sameUrl,'explicit native cache refresh kept stale same-URL images or restarted an unrelated video');
 });
 check(cases.length>0,'no browser cases selected');

 check(errors.length===0,'no uncaught browser error');if(sourceOverride)check(sourceOverridesUsed===cases.length,'negative control did not load its exact source for every case');if(cssOverride)check(cssOverridesUsed===cases.length,'negative control did not load its exact CSS for every case');const result={automaticFallbackScope,ok:cases.every(c=>c.ok),cases:cases.length,checks,performanceMetrics,saveMetrics,progressMetrics,reports:cases,errors,sourceOverride:sourceOverride||null,sourceOverridesUsed,cssOverride:cssOverride||null,cssOverridesUsed,scope:'actual production editor; memory native transport with revision checks; no real library writes or OS picker'};fs.writeFileSync(path.join(out,'editor-reverse-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;

}finally{await browser.close();}
