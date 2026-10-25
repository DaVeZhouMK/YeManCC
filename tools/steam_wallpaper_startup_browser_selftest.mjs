import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';

const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2];
if(!out)throw Error('validation directory required (react.js and react-dom.js)');
fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(root+'/package.json')('esbuild');
await build({stdin:{contents:"export {installSteamWallpaper} from './decky-plugin/src/steamWallpaper';export {MirrorClient} from './decky-plugin/src/mirrorClient';",resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',globalName:'WallpaperStartup',outfile:out+'/wallpaper-startup-bundle.js'});
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright');
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
const results=[],errors=[];let checks=0;
const check=(value,label)=>{checks++;assert.ok(value,label)};
try{
 for(const viewport of [{width:1280,height:800},{width:1920,height:1200},{width:1920,height:1080},{width:3840,height:2160}]){
  for(const scenario of ['shared-connected','cold-connected-later','legacy-cold-no-ready','send-once-fails','hidden-then-visible','remount-on-shared-socket']){
   const page=await browser.newPage({viewport});page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://127.0.0.1:13140/**',route=>new URL(route.request().url()).pathname.startsWith('/asset/')?route.fulfill({contentType:'image/png',body:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==','base64')}):route.fulfill({contentType:'text/html',body:'<!doctype html><style>html,body,#root{height:100%;margin:0}.native-detail{position:absolute;inset:0}.native-images{position:absolute;inset:0;background:red}</style><div id="root"></div>'}));
   await page.goto('http://127.0.0.1:13140/');for(const file of ['react.js','react-dom.js','wallpaper-startup-bundle.js'])await page.addScriptTag({path:path.join(out,file)});
   await page.evaluate(scenario=>{
    const bootstrap={endpoint:'ws://127.0.0.1:18181/mirror',token:'a'.repeat(64),runId:'startup-proof',hudProtocol:1,wallpaperProtocol:1};window.__YMCC_DECKY_MIRROR__=bootstrap;
    const timers=new Set();let sockets=0;
    class Socket{
     listeners=new Map();sent=[];closed=false;allowReady=!['cold-connected-later','legacy-cold-no-ready'].includes(scenario);failNext=scenario==='send-once-fails';
     addEventListener(type,fn){this.listeners.set(type,fn)}close(){this.closed=true;}
     send(text){const m=JSON.parse(text);if(m.command==='wallpaper.context'&&this.failNext){this.failNext=false;throw Error('one interrupted send');}this.sent.push(m);if(m.command==='wallpaper.context'&&m.requestReady&&this.allowReady)queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:bootstrap.runId,enabled:true,stateRevision:1,contextSeq:m.contextSeq})}));if(m.command==='wallpaper.context')queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'wallpaper',runId:bootstrap.runId,enabled:true,...(scenario==='legacy-cold-no-ready'?{}:{stateRevision:1}),contextSeq:m.contextSeq,appId:m.appId,entry:m.visible?{assetId:'fixture',revision:'1',bindingRevision:1,kind:'image',url:'http://127.0.0.1:13140/asset/fixture.png?token='+bootstrap.token}:null})}));}
     emit(type,event={}){this.listeners.get(type)?.(event)}
    }
    let socket;const client=new WallpaperStartup.MirrorClient({bootstrap:()=>bootstrap,socket:()=>{sockets++;return socket=new Socket()},schedule:(fn,delay)=>{const t=setTimeout(()=>{timers.delete(t);fn()},delay);timers.add(t);return t},cancel:t=>{timers.delete(t);clearTimeout(t)},clientId:'wallpaper-startup-proof'});
    // The real HUD owns a passive connection even when the sidebar is closed.
    client.setPassiveDemand('hud',true);if(scenario!=='cold-connected-later')socket.emit('open');
    const patches=new Map(),logs=[];const host={__YMCC_DECKY_MIRROR__:bootstrap,addEventListener:(...args)=>window.addEventListener(...args),removeEventListener:(...args)=>window.removeEventListener(...args),console:{info:value=>logs.push(value)}};
    const dispose=WallpaperStartup.installSteamWallpaper({host,react:React,reactDom:ReactDOM,ui:{modules:new Map([['detail',{HeaderBackgroundImage:'native-detail',ImgSrc:'native-images',TopCapsule:'top-capsule'}]])},routerHook:{addPatch(p,f){patches.set(p,f);return f},removePatch(p){patches.delete(p)}},client});
    // The host announces readiness once per connection. A late Surface missed it.
    if(!['cold-connected-later','legacy-cold-no-ready'].includes(scenario))socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:bootstrap.runId,enabled:true})});
    if(scenario==='hidden-then-visible')Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});
    let reactRoot=ReactDOM.createRoot(document.getElementById('root'));
    const mount=()=>{const Original=()=>React.createElement('div',{className:'native-detail'},React.createElement('div',{className:'native-images'}),React.createElement('button',{'data-appid':'730'},'CS2'));const route=patches.get('/library/app/:appid')({children:React.createElement(Original,{match:{params:{appid:'730'}}})});reactRoot.render(route.children);};
    mount();window.startup={client,socket,logs,timers,get sockets(){return sockets},mount,dispose,unmount(){reactRoot.unmount();reactRoot=ReactDOM.createRoot(document.getElementById('root'))}};
   },scenario);
   await page.locator('[data-ymcc-wallpaper-route]').waitFor();
   if(scenario==='cold-connected-later'){await page.evaluate(()=>startup.socket.emit('open'));await page.waitForTimeout(80);check(await page.locator('[data-ymcc-wallpaper-surface]').count()===0,'versioned cold frames wait for current native ready');await page.evaluate(()=>{startup.socket.allowReady=true;startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true})});});}
   if(scenario==='send-once-fails')await page.locator('button').focus();
   if(scenario==='hidden-then-visible'){
    check(await page.locator('[data-ymcc-wallpaper-surface]').count()===0,'hidden document does not paint');
    await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'visible',configurable:true});document.dispatchEvent(new Event('visibilitychange'))});
   }
   await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000}).catch(async error=>{fs.writeFileSync(path.join(out,'startup-failed-scenario.json'),JSON.stringify({viewport,scenario,state:await page.evaluate(()=>({client:startup.client.snapshot(),sent:startup.socket.sent,logs:startup.logs,focus:document.hasFocus(),visibility:document.visibilityState,html:document.getElementById('root').innerHTML}))},null,2));throw error;});
   if(scenario==='remount-on-shared-socket'){
    await page.evaluate(()=>startup.unmount());check(await page.evaluate(()=>startup.client.snapshot().connected),'HUD keeps socket alive when wallpaper unmounts');
    await page.evaluate(()=>startup.mount());await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:3000});
   }
   const state=await page.evaluate(()=>({connected:startup.client.snapshot().connected,sockets:startup.sockets,contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context'),surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length,timers:startup.timers.size,logs:startup.logs}));
   check(state.connected,'transport connected');check(state.sockets===1,'wallpaper reuses shared socket');check(state.contexts.at(-1)?.appId==='730'&&state.contexts.at(-1)?.visible,'selected cached game reaches host');check(state.surfaces===1,'one background surface');check(state.timers===0,'no retry polling after successful context');check(!state.logs.some(line=>/token|\/asset\/|http:|ws:/.test(line)),'diagnostics contain no credential or URL');check(state.logs.some(line=>line.includes('background-found')),'surface stage logged');
   const sent=state.contexts.length;await page.locator('button').focus();await page.waitForTimeout(80);check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length)===sent,'same selected game does not resend after success');
   await page.evaluate(()=>startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,resumeGeneration:2})}));
   await page.waitForFunction(sent=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length>sent&&document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,sent,{timeout:3000});
   const resumedCount=await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length);
   check(resumedCount===sent+1,'authenticated committed wake requests current selection once through real shared transport');
   await page.evaluate(()=>{for(const generation of [2,1])startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'startup-proof',enabled:true,resumeGeneration:generation})});});
   await page.waitForTimeout(30);check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length)===resumedCount,'old and duplicate resume does not replay context');
   check(await page.evaluate(()=>startup.sockets===1&&startup.timers.size===0),'wake reuses one socket and leaves zero retry timers');
   await page.evaluate(()=>startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:'foreign-run',enabled:true,resumeGeneration:9})}));
   await page.waitForFunction(()=>!startup.client.snapshot().connected&&!document.querySelector('[data-ymcc-wallpaper-surface]'));
   check(await page.evaluate(()=>startup.socket.sent.filter(m=>m.command==='wallpaper.context').length)===resumedCount,'foreign-session wake closes connection without replaying requests');

   results.push({viewport,scenario,sockets:state.sockets,contexts:sent,surfaces:state.surfaces,timers:state.timers});
   await page.evaluate(()=>{startup.unmount();startup.dispose();startup.client.dispose()});await page.close();
  }
 }
 check(errors.length===0,'no browser exceptions');fs.writeFileSync(out+'/wallpaper-startup-results.json',JSON.stringify({ok:true,checks,results,errors,scope:'Production wallpaper consumer and MirrorClient with React18/Edge; deterministic transport/Steam route fixtures. No live Steam mutation.'},null,2));console.log('WALLPAPER_STARTUP_BROWSER_OK '+checks+' checks / '+results.length+' cases');
}finally{await browser.close()}
