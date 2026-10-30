import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';

const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2],onlyScenario=process.argv[3];
if(!out)throw Error('validation directory required (react.js and react-dom.js)');
const scenarios=['shared-connected','cold-connected-later','legacy-cold-no-ready','send-once-fails','hidden-then-visible','remount-on-shared-socket','dispose-while-mounted','late-home-no-route-remount','late-home-native-selection','late-home-delayed-native-root','late-nonhome-idle','normal-home-delayed-holder','detail-ignores-home-holder','pending-ready-dedupe'];
if(onlyScenario&&!scenarios.includes(onlyScenario))throw Error('Unknown wallpaper scenario: '+onlyScenario);
fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(root+'/package.json')('esbuild');
const testedSources=[];
const pinSources={name:'wallpaper-exact-tested-sources',setup(builder){builder.onLoad({filter:/\.tsx?$/},args=>{
 const bytes=fs.readFileSync(args.path),relative=path.relative(root,args.path),saved=path.join(out,'tested-source',relative);
 fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes);
 testedSources.push({path:args.path,snapshot:saved,sha256:createHash('sha256').update(bytes).digest('hex')});
 return{contents:bytes.toString('utf8'),loader:args.path.endsWith('.tsx')?'tsx':'ts',resolveDir:path.dirname(args.path)};
});}};
await build({stdin:{contents:"export {installSteamWallpaper} from './decky-plugin/src/steamWallpaper';export {MirrorClient} from './decky-plugin/src/mirrorClient';",resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',globalName:'WallpaperStartup',plugins:[pinSources],outfile:out+'/wallpaper-startup-bundle.js'});
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright');
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
const results=[],errors=[],lifecycleResults=[];let checks=0;
const check=(value,label)=>{checks++;assert.ok(value,label)};
try{
 for(const viewport of [{width:1280,height:800},{width:1920,height:1200},{width:1920,height:1080},{width:3840,height:2160}]){
  for(const scenario of scenarios){
   if(onlyScenario&&scenario!==onlyScenario)continue;
   const page=await browser.newPage({viewport});page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://127.0.0.1:13140/**',route=>new URL(route.request().url()).pathname.startsWith('/asset/')?route.fulfill({contentType:'image/png',body:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==','base64')}):route.fulfill({contentType:'text/html',body:'<!doctype html><style>html,body,#root{height:100%;margin:0}.native-detail,.native-home,.native-home-root{position:absolute;inset:0}.native-images{position:absolute;inset:0;background:red}</style><div id="root"></div>'}));
   await page.goto('http://127.0.0.1:13140/');for(const file of ['react.js','react-dom.js','wallpaper-startup-bundle.js'])await page.addScriptTag({path:path.join(out,file)});
   await page.evaluate(scenario=>{
    // Instrument only this isolated document. Forward every browser operation;
    // count actual live owners rather than inferring cleanup from DOM absence.
    const trackedTypes=new Set(['visibilitychange','freeze','resume','blur','focus','pagehide','pageshow','focusin','ymcc-decky-bootstrap']);
    const historyListeners=new Set();
    const eventRecords=[],activeMutations=new Set(),activeIntersections=new Set();let mutationCallbacks=0,intersectionCallbacks=0;
    const add=EventTarget.prototype.addEventListener,remove=EventTarget.prototype.removeEventListener;
    const eligible=(target,type)=>trackedTypes.has(type)&&(target===window||target===document||target?.hasAttribute?.('data-ymcc-wallpaper-route'));
    const capture=options=>typeof options==='boolean'?options:options?.capture===true;
    EventTarget.prototype.addEventListener=function(type,listener,options){if(listener&&eligible(this,type)&&!eventRecords.some(x=>x.target===this&&x.type===type&&x.listener===listener&&x.capture===capture(options)))eventRecords.push({target:this,type,listener,capture:capture(options)});return add.call(this,type,listener,options)};
    EventTarget.prototype.removeEventListener=function(type,listener,options){const at=eventRecords.findIndex(x=>x.target===this&&x.type===type&&x.listener===listener&&x.capture===capture(options));if(at>=0)eventRecords.splice(at,1);return remove.call(this,type,listener,options)};
    const NativeMutation=window.MutationObserver,NativeIntersection=window.IntersectionObserver;
    window.MutationObserver=class extends NativeMutation{constructor(callback){super((...args)=>{mutationCallbacks++;callback(...args)})}observe(...args){activeMutations.add(this);return super.observe(...args)}disconnect(){activeMutations.delete(this);return super.disconnect()}};
    window.IntersectionObserver=class extends NativeIntersection{constructor(callback,options){super((...args)=>{intersectionCallbacks++;callback(...args)},options)}observe(...args){activeIntersections.add(this);return super.observe(...args)}disconnect(){activeIntersections.delete(this);return super.disconnect()}};
    const resources=()=>({listeners:eventRecords.length,historyListeners:historyListeners.size,mutationObservers:activeMutations.size,intersectionObservers:activeIntersections.size,mutationCallbacks,intersectionCallbacks});
    const bootstrap={endpoint:'ws://127.0.0.1:18181/mirror',token:'a'.repeat(64),runId:'startup-proof',hudProtocol:1,wallpaperProtocol:1};window.__YMCC_DECKY_MIRROR__=bootstrap;
    const timers=new Set();let sockets=0;
    let failedOnce=false;
    class Socket{
     constructor(){if(scenario==='send-once-fails'&&sockets>1)queueMicrotask(()=>this.emit('open'));}
     holdReplies=scenario==='pending-ready-dedupe';
     listeners=new Map();sent=[];closed=false;wallpaperEnabled=true;stateRevision=1;allowReady=!['cold-connected-later','legacy-cold-no-ready'].includes(scenario);failNext=scenario==='send-once-fails'&&!failedOnce;
     addEventListener(type,fn){this.listeners.set(type,fn)}close(){this.closed=true;}
     send(text){const m=JSON.parse(text);if(m.command==='wallpaper.context'&&this.failNext){this.failNext=false;failedOnce=true;throw Error('one interrupted send');}this.sent.push(m);if(m.command==='wallpaper.context'&&this.holdReplies)return;if(m.command==='wallpaper.context'&&m.requestReady&&this.allowReady)queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:bootstrap.runId,enabled:this.wallpaperEnabled,stateRevision:this.stateRevision,contextSeq:m.contextSeq})}));if(m.command==='wallpaper.context')queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'wallpaper',runId:bootstrap.runId,enabled:this.wallpaperEnabled,...(scenario==='legacy-cold-no-ready'?{}:{stateRevision:this.stateRevision}),contextSeq:m.contextSeq,appId:m.appId,entry:m.visible&&this.wallpaperEnabled?{assetId:'fixture',revision:'1',bindingRevision:1,kind:'image',url:'http://127.0.0.1:13140/asset/fixture.png?token='+bootstrap.token}:null})}));}
     emit(type,event={}){this.listeners.get(type)?.(event)}
    }
    let socket;const client=new WallpaperStartup.MirrorClient({bootstrap:()=>bootstrap,socket:()=>{sockets++;return socket=new Socket()},schedule:(fn,delay)=>{const t=setTimeout(()=>{timers.delete(t);fn()},delay);timers.add(t);return t},cancel:t=>{timers.delete(t);clearTimeout(t)},clientId:'wallpaper-startup-proof'});
    // The real HUD owns a passive connection even when the sidebar is closed.
    client.setPassiveDemand('hud',true);if(scenario!=='cold-connected-later')socket.emit('open');
    const lateHome=scenario.startsWith('late-home-'),lateNonhome=scenario==='late-nonhome-idle',normalHome=scenario==='normal-home-delayed-holder',detailScope=scenario==='detail-ignores-home-holder',homePage=lateHome||normalHome,globalCarrier=lateHome||lateNonhome,nativeSelection=scenario==='late-home-native-selection'||normalHome||detailScope;
    const globalComponents=new Map();const globalElement=document.createElement('div');globalElement.id='decky-globals';document.body.appendChild(globalElement);
    const globalRoot=ReactDOM.createRoot(globalElement);let renderedGlobals=[];const renderGlobals=()=>{if(renderedGlobals.length!==globalComponents.size)renderedGlobals=[...globalComponents].map(([key,Component])=>React.createElement(Component,{key}));globalRoot.render(React.createElement(React.Fragment,null,...renderedGlobals))};
    let reactRoot=ReactDOM.createRoot(document.getElementById('root'));
    let nativeAppId='730',nativeGeneration=0,nativeHolderReady=!normalHome,rerenderNative=()=>{};
    const NativeImages=({appid,previousAppid,preloadAppid})=>React.createElement('div',{className:'native-home-images native-images'});
    const Original=()=>{
     const [,update]=React.useState(0);rerenderNative=()=>update(n=>n+1);
     const holder=nativeHolderReady?React.createElement(NativeImages,{key:nativeGeneration,appid:nativeAppId,previousAppid:'945360',preloadAppid:'999'}):null;
     return React.createElement('div',{className:homePage?'native-home-root':lateNonhome?'native-other':'native-detail'},homePage?React.createElement('div',{className:'native-home'},nativeSelection?holder:React.createElement('div',{className:'native-home-images native-images'})):React.createElement('div',{className:'native-images'}),detailScope?React.createElement('div',{className:'native-home'},holder):null,React.createElement('button',{'data-appid':nativeSelection?undefined:'730',className:nativeSelection?undefined:'gpfocus'},'CS2'));
    };
    if(normalHome)ReactDOM.flushSync(()=>globalRoot.render(React.createElement('div',{'data-outside-home':true,className:'native-home'},React.createElement(NativeImages,{appid:'945360',previousAppid:'570',preloadAppid:'999'}))));
    // Steam has already rendered this cached page. addPatch below registers a
    // patch but deliberately never asks this existing page root to render again.
    if(globalCarrier&&scenario!=='late-home-delayed-native-root')ReactDOM.flushSync(()=>reactRoot.render(React.createElement(Original)));
    const nativeWindow={m_BrowserWindow:window,m_locationPathname:homePage?'/library/home':lateNonhome?'/library/collections':'/library/app/730',m_history:{listen(listener){historyListeners.add(listener);return()=>historyListeners.delete(listener)}}};
    const patches=new Map(),logs=[];const host={__YMCC_DECKY_MIRROR__:bootstrap,addEventListener:(...args)=>window.addEventListener(...args),removeEventListener:(...args)=>window.removeEventListener(...args),console:{info:value=>logs.push(value)}};
    const dispose=WallpaperStartup.installSteamWallpaper({host,react:React,reactDom:ReactDOM,ui:{findSP:()=>window,Router:{WindowStore:{GamepadUIMainWindowInstance:nativeWindow}},modules:new Map([['detail',{HeaderBackgroundImage:'native-detail',ImgSrc:'native-images',TopCapsule:'top-capsule'}],...(globalCarrier||normalHome||detailScope?[['home',{RecentGamesBackgroundContainer:'native-home',RecentGamesBackgroundImages:'native-home-images',RecentGamesContainer:'native-home-root'}]]:[])])},routerHook:{addPatch(p,f){patches.set(p,f);return f},removePatch(p){patches.delete(p)},...(globalCarrier?{addGlobalComponent(name,Component){globalComponents.set(name,Component);renderGlobals()},removeGlobalComponent(name){globalComponents.delete(name);renderGlobals()}}:{})},client});
    // The host announces readiness once per connection. A late Surface missed it.
    if(!['cold-connected-later','legacy-cold-no-ready','pending-ready-dedupe'].includes(scenario))socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:bootstrap.runId,enabled:true})});
    if(scenario==='hidden-then-visible')Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});
    const mount=(appId='730')=>{const props=homePage||appId===null?{}:{match:{params:{appid:appId}}};const route=patches.get(homePage?'/library/home':'/library/app/:appid')({children:React.createElement(Original,props)});reactRoot.render(route.children);};
    if(!globalCarrier)mount(detailScope?null:'730');window.startup={client,get socket(){return socket},logs,timers,patches,resources,globalComponents,navigate(path){nativeWindow.m_locationPathname=path;for(const listener of historyListeners)listener({pathname:path},'PUSH')},mountNative(){reactRoot.render(React.createElement(Original))},changeNativeSelection(appId,replace){nativeAppId=appId;if(replace)nativeGeneration++;rerenderNative()},showNativeHolder(){nativeHolderReady=true;rerenderNative()},disposeGlobals(){globalRoot.unmount();globalElement.remove()},get sockets(){return sockets},mount,dispose,unmount(){reactRoot.unmount();reactRoot=ReactDOM.createRoot(document.getElementById('root'))}};
   },scenario);
   if(scenario==='late-home-delayed-native-root'){
    await page.waitForTimeout(30);
    check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length===0),'empty cached Home has no fabricated selection');
    await page.evaluate(()=>startup.mountNative());
   }
   if(scenario==='late-nonhome-idle'){
    await page.waitForTimeout(60);
    await page.evaluate(()=>{const own=document.createElement('div');own.id='unrelated-live-content';document.body.appendChild(own)});
    await page.waitForTimeout(30);
    const idle=await page.evaluate(async()=>{
     const original=document.querySelectorAll;let scans=0;
     document.querySelectorAll=function(selector){if(selector==='.native-home-root'||selector==='[data-ymcc-wallpaper-route]')scans++;return original.call(this,selector)};
     const before=startup.resources();
     try{for(let i=0;i<24;i++){const item=document.createElement('span');document.getElementById('unrelated-live-content').appendChild(item);await new Promise(r=>setTimeout(r,0));item.remove();await new Promise(r=>setTimeout(r,0))}}finally{document.querySelectorAll=original}
     return{scans,before,after:startup.resources(),contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length,globals:startup.globalComponents.size};
    });
    fs.writeFileSync(path.join(out,'nonhome-idle-observation.json'),JSON.stringify({viewport,scenario,...idle},null,2));
    check(idle.contexts===0,'non-home page cannot invent a wallpaper selection or intent');
    check(idle.globals===1,'public late carrier remains registered on a non-home page');
    check(idle.scans===0,'unrelated non-home subtree mutations cause zero wallpaper whole-document scans');
    await page.evaluate(()=>{startup.dispose();startup.unmount();startup.disposeGlobals();startup.client.dispose()});
    results.push({viewport,scenario,idle});await page.close();continue;
   }
   if(!scenario.startsWith('late-home-'))await page.locator('[data-ymcc-wallpaper-route]').waitFor();
   if(scenario==='pending-ready-dedupe'){
     await page.waitForFunction(()=>startup.socket.sent.some(m=>m.command==='wallpaper.context'&&m.requestReady),null,{timeout:3000});
     const storm=await page.evaluate(()=>{
      const contexts=()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context');
      const before=contexts().length,button=document.querySelector('button');
      for(let i=0;i<24;i++)button.dispatchEvent(new FocusEvent('focusin',{bubbles:true}));
      return{before,after:contexts().length,lastSeq:contexts().at(-1).contextSeq};
     });
     fs.writeFileSync(path.join(out,`pending-ready-${viewport.width}x${viewport.height}.json`),JSON.stringify({viewport,scenario,storm},null,2));
     check(storm.after===storm.before,'identical focus burst sends no extra context while native ready is pending');
     // Event-driven spacing: this does not add a timer to the product.
     await page.waitForTimeout(1050);
     const retry=await page.evaluate(()=>{const before=startup.socket.sent.filter(m=>m.command==='wallpaper.context').length;document.querySelector('button').dispatchEvent(new FocusEvent('focusin',{bubbles:true}));return{before,after:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length}});
     check(retry.after===retry.before+1,'a later event can retry an unacknowledged ready once');
     await page.evaluate(()=>startup.mount('570'));
     await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='570',null,{timeout:3000});
     check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1).requestReady===true),'different game stays immediate while native ready is pending');
     await page.evaluate(()=>{const last=startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1);startup.socket.holdReplies=false;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,stateRevision:1,contextSeq:last.contextSeq})})});
     await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='570'&&document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
     fs.writeFileSync(path.join(out,`pending-ready-${viewport.width}x${viewport.height}.json`),JSON.stringify({viewport,scenario,storm,retry,differentGameConfirmed:'570'},null,2));
     // Return to the common fixture selection without weakening its assertions.
     await page.evaluate(()=>startup.mount('730'));
     await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='730'&&document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
   }
   if(scenario==='cold-connected-later'){await page.evaluate(()=>startup.socket.emit('open'));await page.waitForTimeout(80);check(await page.locator('[data-ymcc-wallpaper-surface]').count()===0,'versioned cold frames wait for current native ready');await page.evaluate(()=>{startup.socket.allowReady=true;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true})});});}
   if(scenario==='send-once-fails')await page.locator('button').focus();
   if(scenario==='hidden-then-visible'){
    check(await page.locator('[data-ymcc-wallpaper-surface]').count()===0,'hidden document does not paint');
    await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'visible',configurable:true});document.dispatchEvent(new Event('visibilitychange'))});
   }
   if(scenario==='normal-home-delayed-holder'||scenario==='detail-ignores-home-holder'){
    await page.waitForTimeout(60);
    check(await page.evaluate(()=>!document.activeElement?.closest('[data-ymcc-wallpaper-route]')&&!document.querySelector('.gpfocus')),'scope test starts without card focus');
    check(await page.evaluate(()=>!startup.socket.sent.some(m=>m.command==='wallpaper.context')&&!document.querySelector('[data-ymcc-wallpaper-surface]')),'missing Home holder and detail route cannot borrow another Home selection');
    if(scenario==='normal-home-delayed-holder'){
     check(await page.locator('[data-outside-home] .native-home-images').count()===1,'unrelated Home holder exists outside this route scope');
     await page.evaluate(()=>{startup.socket.wallpaperEnabled=false;startup.socket.stateRevision=2;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:false,stateRevision:2})});Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});document.dispatchEvent(new Event('visibilitychange'));startup.showNativeHolder()});
     await page.waitForTimeout(60);check(await page.locator('[data-ymcc-wallpaper-surface]').count()===0,'delayed holder cannot start hidden or OFF Home media');
     await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'visible',configurable:true});document.dispatchEvent(new Event('visibilitychange'))});
     await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='730',null,{timeout:3000});
     check(await page.locator('[data-ymcc-wallpaper-surface]').count()===0,'normal Home discovers its holder without overriding native OFF');
     await page.evaluate(()=>{startup.socket.wallpaperEnabled=true;startup.socket.stateRevision=3;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,stateRevision:3})})});
    }else{
     check(await page.locator('[data-ymcc-wallpaper-route] .native-home-images').count()===1,'detail negative includes a Home holder inside its own scope');
     await page.evaluate(()=>startup.changeNativeSelection('570',true));await page.waitForTimeout(40);
     check(await page.evaluate(()=>!startup.socket.sent.some(m=>m.command==='wallpaper.context')),'detail ignores Home holder replacement before confirmed detail selection');
     await page.evaluate(()=>startup.mount('730'));
    }
   }
   await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000}).catch(async error=>{fs.writeFileSync(path.join(out,'startup-failed-scenario.json'),JSON.stringify({viewport,scenario,state:await page.evaluate(()=>({client:startup.client.snapshot(),sent:startup.socket.sent,logs:startup.logs,focus:document.hasFocus(),visibility:document.visibilityState,html:document.getElementById('root').innerHTML}))},null,2));throw error;});
   if(scenario==='remount-on-shared-socket'){
    await page.evaluate(()=>startup.unmount());check(await page.evaluate(()=>startup.client.snapshot().connected),'HUD keeps socket alive when wallpaper unmounts');
    await page.evaluate(()=>startup.mount());await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    const baseline=await page.evaluate(()=>startup.resources());const samples=[];
    check(baseline.listeners>0&&baseline.mutationObservers>0&&baseline.intersectionObservers>0,'live route instrumentation observes real owners before cleanup');
    for(let turn=0;turn<12;turn++){
     await page.evaluate(()=>startup.unmount());
     const unmounted=await page.evaluate(()=>startup.resources());
     check(unmounted.mutationObservers===0&&unmounted.intersectionObservers===0,'route unmount disconnects both real observer types');
     await page.evaluate(()=>startup.mount());await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
     const current=await page.evaluate(()=>startup.resources());samples.push({turn,unmounted,current});
     check(current.listeners===baseline.listeners&&current.mutationObservers===baseline.mutationObservers&&current.intersectionObservers===baseline.intersectionObservers,'repeated route mount retains one listener/observer owner set');
    }
    lifecycleResults.push({viewport,scenario,baseline,samples});
   }
   if(scenario.startsWith('late-home-')){
    if(scenario==='late-home-native-selection'){
     await page.evaluate(()=>startup.changeNativeSelection('570',true));
     await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='570',null,{timeout:3000}).catch(()=>{});
     const replaced=await page.evaluate(()=>({contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').map(m=>({appId:m.appId,visible:m.visible})),surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length,resources:startup.resources()}));
     fs.writeFileSync(path.join(out,'native-holder-replacement.json'),JSON.stringify({viewport,scenario,replaced},null,2));
     check(replaced.contexts.at(-1)?.appId==='570'&&replaced.contexts.at(-1)?.visible,'replaced native background holder publishes its current game without focus or route remount');
     await page.evaluate(()=>startup.changeNativeSelection('730',true));
     await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='730'&&document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    }
    const before=await page.evaluate(()=>({globals:startup.globalComponents.size,contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length}));
    check(before.globals===1,'late cached home uses one registered public Decky global component');
    check(await page.locator('[data-ymcc-wallpaper-surface]').count()===1,'late home mounts without navigating or rerendering the cached native route');
    check(await page.evaluate(()=>startup.resources().historyListeners===1),'late carrier owns one public history subscription');
    await page.evaluate(()=>startup.navigate('/library/collections'));
    await page.waitForFunction(()=>!document.querySelector('[data-ymcc-wallpaper-surface] img'));
    check(await page.evaluate(()=>startup.client.snapshot().connected),'leaving Home revokes the cached fallback without closing the shared connection');
    await page.evaluate(()=>startup.navigate('/library/home'));
    await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    check(await page.locator('[data-ymcc-wallpaper-surface]').count()===1,'public history return to cached Home reattaches without a focus or route render');
    // Native OFF is authoritative even while the global carrier stays installed.
    await page.evaluate(()=>{startup.socket.wallpaperEnabled=false;startup.socket.stateRevision=2;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:false,stateRevision:2})})});
    await page.waitForFunction(()=>!document.querySelector('[data-ymcc-wallpaper-surface] img'));
    await page.evaluate(()=>{const request=startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1);startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper',runId:'startup-proof',enabled:true,stateRevision:1,contextSeq:request.contextSeq,appId:request.appId,entry:{assetId:'fixture',revision:'1',bindingRevision:1,kind:'image',url:'http://127.0.0.1:13140/asset/fixture.png?token='+'a'.repeat(64)}})})});
    await page.waitForTimeout(40);
    check(await page.locator('[data-ymcc-wallpaper-surface] img').count()===0,'stale ON frame cannot revive the late global carrier after native OFF');
    check(await page.evaluate(()=>startup.globalComponents.size===1&&startup.client.snapshot().connected),'OFF retains one public carrier and the shared HUD connection');
    await page.evaluate(()=>{startup.socket.wallpaperEnabled=true;startup.socket.stateRevision=3;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,stateRevision:3})})});
    await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});document.dispatchEvent(new Event('visibilitychange'))});
    await page.waitForFunction(()=>!document.querySelector('[data-ymcc-wallpaper-surface] img'));
    check(await page.evaluate(()=>startup.client.snapshot().connected),'hidden late home releases media and retains shared HUD transport');
    await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'visible',configurable:true});document.dispatchEvent(new Event('visibilitychange'))});
    await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    // A later legitimate route render must hand over rather than duplicate the
    // background. Keep the same connection and native ON intent throughout.
    if(scenario==='late-home-native-selection')check(await page.evaluate(()=>!document.querySelector('.gpfocus')&&!document.activeElement?.closest('[data-ymcc-wallpaper-fallback-route]')),'native fallback hands off without card focus or route appid');
    await page.evaluate(()=>startup.mount());
    await page.waitForFunction(()=>!document.querySelector('[data-ymcc-wallpaper-fallback-route]')&&document.querySelector('[data-ymcc-wallpaper-route] [data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    await page.waitForTimeout(60);
    check(await page.locator('[data-ymcc-wallpaper-surface]').count()===1,'late home bridge yields to a normal patched route without a second surface');
    lifecycleResults.push({viewport,scenario,before,after:await page.evaluate(()=>({contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').map(m=>({appId:m.appId,visible:m.visible})),resources:startup.resources(),fallbacks:document.querySelectorAll('[data-ymcc-wallpaper-fallback-route]').length,surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length}))});
   }
   if(scenario==='late-home-native-selection'||scenario==='normal-home-delayed-holder'){
    await page.evaluate(()=>startup.changeNativeSelection('570',true));
    await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='570'&&document.querySelector('[data-ymcc-wallpaper-route] [data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
    check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').every(m=>m.appId==='730'||m.appId==='570')&&document.querySelectorAll('[data-ymcc-wallpaper-surface]').length===1),'normal Home observes current holder with one owner, excluding previous/preload and outside scope');
    await page.evaluate(()=>startup.changeNativeSelection('730',true));
    await page.waitForFunction(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').at(-1)?.appId==='730'&&document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
   }
   const state=await page.evaluate(()=>({connected:startup.client.snapshot().connected,sockets:startup.sockets,contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context'),surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length,timers:startup.timers.size,logs:startup.logs}));
   const expectedSockets=scenario==='send-once-fails'?2:1;
    check(state.connected,'transport connected');check(state.sockets===expectedSockets,'wallpaper reuses the shared socket, reconnecting once after the injected send failure');check(state.contexts.at(-1)?.appId==='730'&&state.contexts.at(-1)?.visible,'selected cached game reaches host');check(state.surfaces===1,'one background surface');check(state.timers===0,'no retry polling after successful context');check(!state.logs.some(line=>/token|\/asset\/|http:|ws:/.test(line)),'diagnostics contain no credential or URL');check(state.logs.some(line=>line.includes('background-found')),'surface stage logged');
   const sent=state.contexts.length;await page.locator('button').focus();await page.waitForTimeout(80);check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length)===sent,'same selected game does not resend after success');
   await page.evaluate(()=>startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,resumeGeneration:2})}));
   await page.waitForFunction(sent=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length>sent&&document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,sent,{timeout:3000});
   const resumedCount=await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length);
   check(resumedCount===sent+1,'authenticated committed wake requests current selection once through real shared transport');
   await page.evaluate(()=>{for(const generation of [2,1])startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,resumeGeneration:generation})});});
   await page.waitForTimeout(30);check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length)===resumedCount,'old and duplicate resume does not replay context');
   check(await page.evaluate(expected=>startup.sockets===expected&&startup.timers.size===0,expectedSockets),'wake reuses the recovered socket and leaves zero retry timers');
   if(scenario==='dispose-while-mounted'||scenario.startsWith('late-home-')){
    const before=await page.evaluate(()=>({resources:startup.resources(),contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length}));
    check(before.resources.listeners>0&&before.resources.mutationObservers>0&&before.resources.intersectionObservers>0,'dispose starts with a live instrumented wallpaper route');
    await page.evaluate(()=>{startup.dispose();startup.dispose()});
    await page.waitForFunction(()=>!document.querySelector('[data-ymcc-wallpaper-route]'));
    const disposed=await page.evaluate(()=>({resources:startup.resources(),patches:startup.patches.size,globalComponents:startup.globalComponents.size,surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length,timers:startup.timers.size,contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length}));
    check(disposed.patches===0&&disposed.surfaces===0&&disposed.globalComponents===0,'mounted dispose removes route patches, public globals and portals');
    check(disposed.resources.listeners===0&&disposed.resources.historyListeners===0&&disposed.resources.mutationObservers===0&&disposed.resources.intersectionObservers===0,'mounted dispose removes all tracked DOM/history listeners and observers');
    check(disposed.timers===0&&await page.evaluate(()=>startup.client.snapshot().connected),'wallpaper disposal leaves shared HUD socket and no retry timer');
    await page.evaluate(()=>{for(const type of ['focus','blur','pagehide','pageshow','ymcc-decky-bootstrap'])window.dispatchEvent(new Event(type));for(const type of ['visibilitychange','freeze','resume','focusin'])document.dispatchEvent(new Event(type));const node=document.createElement('span');document.getElementById('root').append(node);node.remove();startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,resumeGeneration:3})})});
    await page.waitForTimeout(40);
    const late=await page.evaluate(()=>({resources:startup.resources(),contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length,surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length}));
    check(late.contexts===disposed.contexts&&late.surfaces===0,'late foreground/bootstrap/transport events cannot revive disposed wallpaper');
    check(late.resources.mutationCallbacks===disposed.resources.mutationCallbacks&&late.resources.intersectionCallbacks===disposed.resources.intersectionCallbacks,'disconnected observers perform zero work after disposal');
    lifecycleResults.push({viewport,scenario,before,disposed,late});
    results.push({viewport,scenario,sockets:state.sockets,contexts:sent,surfaces:state.surfaces,timers:state.timers});
    await page.evaluate(()=>{startup.unmount();startup.disposeGlobals();startup.client.dispose()});await page.close();continue;
   }
   await page.evaluate(()=>startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'foreign-run',enabled:true,resumeGeneration:9})}));
   await page.waitForFunction(()=>!startup.client.snapshot().connected&&!document.querySelector('[data-ymcc-wallpaper-surface]'));
   check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length)===resumedCount,'foreign-session wake closes connection without replaying requests');

   results.push({viewport,scenario,sockets:state.sockets,contexts:sent,surfaces:state.surfaces,timers:state.timers});
   await page.evaluate(()=>{startup.unmount();startup.dispose();startup.disposeGlobals();startup.client.dispose()});await page.close();
  }
 }
 check(results.length===(onlyScenario?1:scenarios.length)*4,'all requested viewport/scenario pairs executed');check(errors.length===0,'no browser exceptions');fs.writeFileSync(out+'/wallpaper-startup-results.json',JSON.stringify({ok:true,checks,results,lifecycleResults,errors,testedSources,scenarioSelection:onlyScenario||'all',fixtureSha256:createHash('sha256').update(fs.readFileSync(new URL(import.meta.url))).digest('hex'),runtimeScripts:['react.js','react-dom.js'].map(name=>({name,sha256:createHash('sha256').update(fs.readFileSync(path.join(out,name))).digest('hex')})),browserVersion:browser.version(),bundleSha256:createHash('sha256').update(fs.readFileSync(out+'/wallpaper-startup-bundle.js')).digest('hex'),scope:'Production wallpaper consumer and MirrorClient with React18/Edge; deterministic transport/Steam route fixtures. No live Steam mutation.'},null,2));console.log('WALLPAPER_STARTUP_BROWSER_OK '+checks+' checks / '+results.length+' cases');
}finally{await browser.close()}

