import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';

const out=process.argv[2];if(!out)throw Error('output directory required');fs.mkdirSync(out,{recursive:true});

const require=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');const {chromium}=require('playwright');

const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:1440,height:1100},reducedMotion:'no-preference'});page.setDefaultTimeout(5000);

let sourceOverridesUsed=0,cssOverridesUsed=0;const sourceOverride=process.argv[3],cssOverride=process.argv[4];

const repo=path.resolve(import.meta.dirname,'..'),ui=path.join(repo,'CustomSteamLibrary/workspace-ui');

const samples='G:/YeManCC-Work/Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/preview';

const fixtureSource=fs.readFileSync(path.join(samples,'library-fixture.js'),'utf8');

if(!fixtureSource.includes('const listeners=new Set();'))throw Error('fixture listener anchor missing');

const fixture=fixtureSource.replace('const listeners=new Set();','const listeners=new Set();window.reverseEmit=message=>{for(const listener of listeners)listener({data:message});};')+"\n(()=>{const bridge=window.chrome.webview,post=bridge.postMessage;bridge.postMessage=function(message){if(message.command==='startup')window.__startupRefreshArguments=structuredClone(message.arguments||{});return post.call(bridge,message);};})();";

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

 const base=invoke;window.reverseIO={calls:[],finishPick:null};invoke=async(command,args={},timeoutMs=0)=>{

  reverseIO.calls.push({command,args:structuredClone(args)});

  if(command==='pickVideo'||command==='pickArtwork')return new Promise((resolve,reject)=>{reverseIO.finishPick=resolve;reverseIO.failPick=reject;});

  if(command==='mediaEdit'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];if(entry&&args.bindingRevision!==entry.bindingRevision)throw Error('media-conflict');}

  if(command==='mediaImport'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];if(!entry)throw Error('missing-target');if(args.bindingRevision!==entry.bindingRevision)throw Error('media-conflict');const slot=args.slot;if(!['static','dynamic'].includes(slot))throw Error('invalid-import-slot');entry.bindingRevision++;const asset={...entry.assets?.[slot],id:'imported-'+entry.bindingRevision,revision:String(entry.bindingRevision),kind:slot==='dynamic'?'video':'image'};entry.assets={...entry.assets,[slot]:asset};entry.manualSlots={...entry.manualSlots,[slot]:true};entry.deletedSlots={...entry.deletedSlots,[slot]:false};entry[slot+'Ready']=true;entry[slot+'PreviewUrl']=(slot==='dynamic'?'/sample.mp4':'/wallpaper.jpg')+'?imported='+entry.bindingRevision;entry.asset=entry.assets[entry.wallpaperMode];entry.previewUrl=entry[entry.wallpaperMode+'PreviewUrl'];return structuredClone(entry);}

  return base(command,args,timeoutMs);

 };

 window.reverseChoose=(start,length=30)=>{const skip=document.getElementById('shared-wallpaper-skip'),duration=document.getElementById('shared-wallpaper-length');if(![...skip.options].some(o=>o.value===String(start)))skip.add(new Option(start+' 秒',String(start)));skip.value=String(start);duration.value=String(length);skip.dispatchEvent(new Event('change'));};

 window.reverseOpenOther=()=>{const game=mergedGames().find(g=>g.mediaOnly&&g.mediaKey==='steam-1245620');if(!game)throw Error('second-fixture-game-missing');openEdit(game);};



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

  respond(item,structuredClone(state));

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
async function makeStaticMediaCard(){
 await page.evaluate(()=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;state.library.games=[];state.steamPlan.items=[];snapshot=structuredClone(state);closeEdit();openEdit(mergedGames().find(g=>g.mediaOnly&&g.mediaKey==='steam-570'));});
 await page.waitForTimeout(25);
}
async function restoreStaticLibraryCard(){
 await page.evaluate(()=>{const state=__LIBRARY_H5_FIXTURE__.snapshot;state.library=structuredClone(staticSourceProbe.originalLibrary);state.steamPlan=structuredClone(staticSourceProbe.originalPlan);reverseEmit({event:'library-scan-complete',refreshRequested:true,refreshProtocol:1});});
 await page.waitForFunction(()=>mergedGames().some(g=>g.primaryExecutable&&normalizedUiPath(g.primaryExecutable)===staticSourceProbe.exe));
 await page.evaluate(()=>{closeEdit();openEdit(mergedGames().find(g=>g.primaryExecutable&&normalizedUiPath(g.primaryExecutable)===staticSourceProbe.exe));});await page.waitForTimeout(25);
}

const selectedCasePattern=process.env.YMCC_EDITOR_CASE_FILTER?new RegExp(process.env.YMCC_EDITOR_CASE_FILTER):null;

async function run(name,fn){if(selectedCasePattern&&!selectedCasePattern.test(name))return;await reset();try{await fn();cases.push({name,ok:true});}catch(error){cases.push({name,ok:false,error:error.message});}}

try{



 await run('saved wallpaper mode static survives reopen with cached video and routes import to static',async()=>{
  await beginStaticSourceProbe();await page.evaluate(()=>{const g=structuredClone(selectedActionGame);closeEdit();openEdit(g);});await settle();
  const before=await page.evaluate(()=>({saved:selectedActionGame.gameMedia.wallpaperMode,mode:sharedWallpaperMode(),pressed:document.querySelector('[data-shared-wallpaper-mode="static"]').getAttribute('aria-pressed'),video:!!document.querySelector('#artwork-wallpaper-preview video')}));
  await makeStaticMediaCard();await page.evaluate(()=>{openArtworkActionMenu('wallpaper');window.savedModeImport=artworkActionForCurrentType('replace');reverseIO.finishPick({path:'G:/H5Fixture/static-choice.jpg'});});await page.evaluate(()=>savedModeImport);
  const writes=await page.evaluate(()=>reverseIO.calls.filter(x=>x.command==='mediaImport').map(x=>x.args.slot));
  check(before.saved==='static'&&before.mode==='static'&&before.pressed==='true'&&!before.video&&writes.length===1&&writes[0]==='static','saved static mode was inferred from cached video or imported into the wrong slot');
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
 await run('static source media-only import remains the displayed choice when its game reappears',async()=>{
  await beginStaticSourceProbe();await makeStaticMediaCard();await page.evaluate(()=>{openArtworkActionMenu('wallpaper');window.staticImport=artworkActionForCurrentType('replace');reverseIO.finishPick({path:'G:/H5Fixture/new-static.jpg'});});const ok=await page.evaluate(()=>staticImport);await restoreStaticLibraryCard();const r=await page.evaluate(()=>({ok:!selectedActionGame.mediaOnly,dirty:artworkDraft.dirty.wallpaper,url:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),accepted:selectedActionGame.gameMedia.assets.static.id,manual:selectedActionGame.gameMedia.manualSlots?.static,imports:reverseIO.calls.filter(x=>x.command==='mediaImport').length}));progressMetrics.push({case:'static-source-library-return',importAccepted:ok,...r});check(ok&&r.ok&&!r.dirty&&r.manual&&r.url?.includes('imported=')&&r.imports===1,'returning library game displayed its old legacy image over an accepted shared manual import');
 });
 await run('static source media-only deletion is not hidden by the old image when its game reappears',async()=>{
  await beginStaticSourceProbe();await makeStaticMediaCard();await page.evaluate(()=>{const actual=invoke;invoke=async(command,args={},...rest)=>{const result=await actual(command,args,...rest);if(command==='mediaEdit'&&args.action==='delete'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];entry.deletedSlots={...entry.deletedSlots,[args.slot]:true};entry.manualSlots={...entry.manualSlots,[args.slot]:true};return structuredClone(entry);}return result;};openArtworkActionMenu('wallpaper');window.staticDelete=artworkActionForCurrentType('delete');});await page.evaluate(()=>staticDelete);await restoreStaticLibraryCard();const r=await page.evaluate(()=>({deleted:selectedActionGame.gameMedia.deletedSlots?.static,text:document.getElementById('artwork-wallpaper-preview').textContent,image:document.querySelector('#artwork-wallpaper-preview img')?.getAttribute('src'),writes:reverseIO.calls.filter(x=>x.command==='mediaEdit'&&x.args.action==='delete').length}));progressMetrics.push({case:'static-source-library-return-delete',...r});check(r.deleted&&!r.image&&r.text.includes('已删除')&&r.writes===1,'returning library game hid its accepted deletion behind an unchanged legacy image');
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

  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=60;reverseChoose(15);reverseChoose(5);reverseOpenOther();reverseChoose(3);});await settle();const values=await page.evaluate(()=>Object.fromEntries(Object.entries(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries).map(([key,entry])=>[key,entry.playback])));check(values['steam-570'].start===5&&values['steam-1245620'].start===3,'switching games dropped or redirected a queued setting');

 });

 await run('a file chosen for the first game cannot import into a newly opened game',async()=>{

  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseOpenOther();reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});await page.evaluate(()=>reversePick);const imports=await page.evaluate(()=>reverseIO.calls.filter(c=>c.command==='mediaImport'));check(imports.length===1&&imports[0].args.key==='steam-570','file import was redirected to the newly selected game');

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

  await page.evaluate(()=>{

   const base=invoke;window.commitCalls=[];invoke=async(command,args={})=>{if(command==='commit'){commitCalls.push(structuredClone(args));return new Promise(resolve=>window.finishCommit=()=>resolve(structuredClone(__LIBRARY_H5_FIXTURE__.snapshot)));}return base(command,args);};

   window.joinFinished=joinEditedGameToSteam();void joinEditedGameToSteam();

  });await page.waitForFunction(()=>window.finishCommit);

  check(await page.evaluate(()=>commitCalls.length===1&&commitCalls[0].selectedGameDirectories.length===1),'single card join duplicated or widened its target');

  check(await page.evaluate(()=>document.querySelector('#steam-commit-modal').classList.contains('hidden')&&document.querySelector('#busy').classList.contains('hidden')),'join opened a modal or busy overlay');

  await page.screenshot({path:path.join(out,'steam-join-inline-1280x800.png')});

  await page.evaluate(()=>routeWorkspaceAction('back'));await page.evaluate(async()=>{finishCommit();await joinFinished;});

  check(await page.evaluate(()=>document.querySelector('#edit-modal').classList.contains('hidden')&&[...steamCommitTasks.values()].some(v=>v.phase==='completed')),'join was discarded or reopened editor after Back');

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

 });

 await run('unchanged primary selection does not schedule a redundant worker',async()=>{

  const result=await page.evaluate(async()=>{editDraft.primaryDirty=true;const actual=invoke;let submitted;invoke=async(command,args={})=>{if(command==='saveEditor'){submitted=structuredClone(args);return structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);}return actual(command,args);};await persistEditDraft();return submitted;});

  check(!(result?.operations||[]).some(o=>o.command==='setPrimary'),'re-selecting the current EXE started another primary worker');

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

  await beginEditorPreviewProbe();await installPreviewFallbackProbe();const result=await page.evaluate(async()=>{const retired=editorPreviewProbe.cover;if(!retired)throw Error('owned cover image required');reverseOpenOther();await new Promise(requestAnimationFrame);const target=document.getElementById('artwork-cover-preview'),before=target.innerHTML,owner=keyFor(selectedActionGame);retired.dispatchEvent(new Event('error'));await new Promise(requestAnimationFrame);return {kept:target.innerHTML===before,ownerKept:keyFor(selectedActionGame)===owner,searches:previewFallbackProbe.searches,downloads:previewFallbackProbe.downloads};});check(result.kept&&result.ownerKept&&result.searches===0&&result.downloads===0,'retired image error crossed card ownership or started unrelated fallback');

 });

 await run('editor preview a current image still reaches the legitimate automatic fallback',async()=>{

  await beginEditorPreviewProbe();await installPreviewFallbackProbe();await page.evaluate(()=>editorPreviewProbe.cover.dispatchEvent(new Event('error')));await page.waitForFunction(()=>artworkDraft.cover?.state==='set'&&artworkDraft.dirty.cover);const result=await page.evaluate(()=>({provider:artworkDraft.cover.asset.provider,searches:previewFallbackProbe.searches,downloads:previewFallbackProbe.downloads}));check(result.provider==='playnite-igdb'&&result.searches===1&&result.downloads===1,'preview ownership gate rejected a current valid automatic fallback');

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
 });
 await run('editor preview an explicit native refresh reloads same-URL images without replacing the video',async()=>{
  await beginNativeArtworkProbe();const result=await page.evaluate(async()=>{const cover=document.querySelector('#artwork-cover-preview img'),long=document.querySelector('#artwork-long-preview img'),video=document.querySelector('#artwork-wallpaper-preview video'),url=long?.getAttribute('src');nativeArtworkProbe.requests[0].resolve(structuredClone(nativeArtworkProbe.requests[0].response));await new Promise(requestAnimationFrame);return {coverReloaded:cover!==document.querySelector('#artwork-cover-preview img'),longReloaded:long!==document.querySelector('#artwork-long-preview img'),videoKept:video===document.querySelector('#artwork-wallpaper-preview video'),sameUrl:url===document.querySelector('#artwork-long-preview img')?.getAttribute('src')};});check(result.coverReloaded&&result.longReloaded&&result.videoKept&&result.sameUrl,'explicit native cache refresh kept stale same-URL images or restarted an unrelated video');
 });
 check(cases.length>0,'no browser cases selected');

 check(errors.length===0,'no uncaught browser error');if(sourceOverride)check(sourceOverridesUsed===cases.length,'negative control did not load its exact source for every case');if(cssOverride)check(cssOverridesUsed===cases.length,'negative control did not load its exact CSS for every case');const result={ok:cases.every(c=>c.ok),cases:cases.length,checks,performanceMetrics,saveMetrics,progressMetrics,reports:cases,errors,sourceOverride:sourceOverride||null,sourceOverridesUsed,cssOverride:cssOverride||null,cssOverridesUsed,scope:'actual production editor; memory native transport with revision checks; no real library writes or OS picker'};fs.writeFileSync(path.join(out,'editor-reverse-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;

}finally{await browser.close();}
