import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';

const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2],onlyScenario=process.argv[3];
const selectedArgs=process.argv.slice(4),waitIndex=selectedArgs.indexOf('--native-binding-wait');
const teardownFlags=selectedArgs.filter(arg=>arg.startsWith('--teardown-variant='));
if(teardownFlags.length>1)throw Error('WALLPAPER_TEARDOWN_VARIANT_DUPLICATE');
if(selectedArgs.includes('--teardown-variant'))throw Error('WALLPAPER_TEARDOWN_VARIANT_EMPTY');
const teardownVariant=teardownFlags.length?teardownFlags[0].slice('--teardown-variant='.length):null;
if(teardownVariant!==null&&!['normal','router-failure'].includes(teardownVariant))throw Error('WALLPAPER_TEARDOWN_VARIANT_UNKNOWN_OR_EMPTY');
const bindingWaitVariant=waitIndex<0?null:selectedArgs[waitIndex+1]||'normal';
const hudTeardownFlags=selectedArgs.filter(arg=>arg.startsWith('--hud-module-teardown='));
if(hudTeardownFlags.length>1)throw Error('HUD_MODULE_TEARDOWN_DUPLICATE');
if(selectedArgs.includes('--hud-module-teardown'))throw Error('HUD_MODULE_TEARDOWN_EMPTY');
const hudTeardownVariant=hudTeardownFlags.length?hudTeardownFlags[0].slice('--hud-module-teardown='.length):null;
if(hudTeardownVariant!==null&&!['router-failure','normal'].includes(hudTeardownVariant))throw Error('HUD_MODULE_TEARDOWN_UNKNOWN_OR_EMPTY');
const hudOnlyAt=selectedArgs.indexOf('--hud-only-bootstrap'),hudOnlyVariant=hudOnlyAt<0?null:selectedArgs[hudOnlyAt+1]||'normal';
const hudStartupFlags=selectedArgs.filter(arg=>arg.startsWith('--hud-startup-variant='));
if(hudStartupFlags.length>1)throw Error('HUD_STARTUP_VARIANT_DUPLICATE');
if(selectedArgs.includes('--hud-startup-variant'))throw Error('HUD_STARTUP_VARIANT_EMPTY');
const hudStartupVariant=hudStartupFlags.length?hudStartupFlags[0].slice('--hud-startup-variant='.length):null;
const realSiblingStartup=selectedArgs.includes('--real-sibling-startup');
if(selectedArgs.filter(arg=>arg==='--real-sibling-startup').length>1)throw Error('REAL_SIBLING_STARTUP_DUPLICATE');
const siblingStartupVariants=['wallpaper-first-patch','wallpaper-second-patch','wallpaper-second-patch-committed','wallpaper-global','wallpaper-global-committed','updater-global-committed','normal'];
if(realSiblingStartup&&!siblingStartupVariants.includes(hudStartupVariant)||!realSiblingStartup&&siblingStartupVariants.slice(0,-1).includes(hudStartupVariant))throw Error('REAL_SIBLING_STARTUP_SCOPE');
if(hudStartupVariant!==null&&![...siblingStartupVariants,'before-registration','after-registration','diagnostic-publication','wallpaper-error','updater-error'].includes(hudStartupVariant))throw Error('HUD_STARTUP_VARIANT_UNKNOWN_OR_EMPTY');
if(hudOnlyVariant&&!['normal','off','invalid','revoked','dispose','already-defined','already-revoked'].includes(hudOnlyVariant))throw Error('Known explicit HUD-only bootstrap selector required');
const bootstrapConsumers=selectedArgs.includes('--bootstrap-consumers')||!!hudOnlyVariant,entryOverrideIndex=selectedArgs.indexOf('--entry-source'),entrySourceOverride=entryOverrideIndex<0?null:path.resolve(selectedArgs[entryOverrideIndex+1]||'');
if((bootstrapConsumers&&(bindingWaitVariant!=='normal'||onlyScenario!=='cold-connected-later'))||(entrySourceOverride&&!bootstrapConsumers))throw Error('Bootstrap consumers/entry fault selection requires original cold-connected-later normal binding-wait');
if(hudTeardownVariant&&(hudOnlyVariant!=='already-defined'||onlyScenario!=='cold-connected-later'||bindingWaitVariant!=='normal'||teardownVariant||!selectedArgs.includes('--single-viewport')))throw Error('HUD_MODULE_TEARDOWN_SCOPE');
if(hudStartupVariant&&(hudOnlyVariant!=='already-defined'||onlyScenario!=='cold-connected-later'||bindingWaitVariant!=='normal'||teardownVariant||hudTeardownVariant||!selectedArgs.includes('--single-viewport')))throw Error('HUD_STARTUP_VARIANT_SCOPE');
const overrideIndex=selectedArgs.indexOf('--wallpaper-source'),wallpaperSourceOverride=overrideIndex<0?null:path.resolve(selectedArgs[overrideIndex+1]||'');
if(teardownVariant&&(onlyScenario!=='late-home-no-route-remount'||bindingWaitVariant||bootstrapConsumers||!selectedArgs.includes('--single-viewport')))throw Error('WALLPAPER_TEARDOWN_VARIANT_SCOPE');
if(wallpaperSourceOverride&&!bindingWaitVariant&&!teardownVariant)throw Error('Wallpaper source fault selection requires an explicit original scenario variant');
if(bindingWaitVariant&&(!['normal','hidden','dispose','off','invalid','revoked'].includes(bindingWaitVariant)||onlyScenario!=='cold-connected-later'))throw Error('Native binding wait requires the original cold-connected-later scenario and one known variant');
const viewports=bindingWaitVariant||selectedArgs.includes('--single-viewport')?[{width:1280,height:800}]:[{width:1280,height:800},{width:1920,height:1200},{width:1920,height:1080},{width:3840,height:2160}];
if(!out)throw Error('validation directory required (react.js and react-dom.js)');
const scenarios=['shared-connected','cold-connected-later','legacy-cold-no-ready','send-once-fails','hidden-then-visible','remount-on-shared-socket','dispose-while-mounted','late-home-no-route-remount','late-home-native-selection','late-home-delayed-native-root','late-nonhome-idle','normal-home-delayed-holder','detail-ignores-home-holder','pending-ready-dedupe'];
if(onlyScenario&&!scenarios.includes(onlyScenario))throw Error('Unknown wallpaper scenario: '+onlyScenario);
fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(root+'/package.json')('esbuild');
const testedSources=[];
let providerExpression='',listenerExpression='',entryText='',reconcileExpression='',dismountExpression='',consumerStartupExpression='';
const entrySource=entrySourceOverride||path.join(root,'decky-plugin/src/index.tsx');
if(bindingWaitVariant){
 const ts=createRequire(root+'/package.json')('typescript');entryText=fs.readFileSync(entrySource,'utf8');
 const tree=ts.createSourceFile('index.tsx',entryText,ts.ScriptTarget.Latest,true,ts.ScriptKind.TSX);
 const factory=tree.statements.find(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='createYmccPlugin'&&node.modifiers?.some(m=>m.kind===ts.SyntaxKind.ExportKeyword)&&node.modifiers?.some(m=>m.kind===ts.SyntaxKind.DefaultKeyword));
 const entryStatements=factory?.body?.statements??tree.statements,declarations=entryStatements.filter(ts.isVariableStatement).flatMap(node=>[...node.declarationList.declarations]);
 const declaration=declarations.find(node=>node.name.getText(tree)==='client');assert(declaration&&ts.isNewExpression(declaration.initializer)&&declaration.initializer.expression.getText(tree)==='MirrorClient');
 const provider=declaration.initializer.arguments[0].properties.find(node=>ts.isPropertyAssignment(node)&&node.name.getText(tree)==='bootstrap');assert(provider&&ts.isArrowFunction(provider.initializer));providerExpression=provider.initializer.getText(tree);
 const listener=declarations.find(node=>node.name.getText(tree)==='bootstrapListener');assert(listener&&ts.isArrowFunction(listener.initializer));listenerExpression=listener.initializer.getText(tree);
 if(hudStartupVariant){
  const names=['disposeMonitorHud','disposeWallpaper','disposeLoaderUpdate'];
  const bindings=node=>ts.isIdentifier(node)?[node.text]:ts.isObjectBindingPattern(node)?node.elements.map(part=>part.name.getText(tree)):[];
  const setup=entryStatements.filter(node=>ts.isVariableStatement(node)&&node.declarationList.declarations.some(part=>bindings(part.name).some(name=>names.includes(name))));
  assert([1,3].includes(setup.length)&&names.every(name=>setup.some(node=>node.declarationList.declarations.some(part=>bindings(part.name).includes(name)))),'exact original consumer initializer declarations');
  const demand=entryStatements.filter(node=>ts.isExpressionStatement(node)&&node.expression.getText(tree)==="client.setPassiveDemand('connection',true)");assert.equal(demand.length,1,'original entry connection demand');
  consumerStartupExpression=[demand[0],...setup].map(node=>node.getText(tree)).join(String.fromCharCode(10));
 }
 if(bootstrapConsumers){const reconcile=declarations.find(node=>node.name.getText(tree)==='stopBootstrapReconcile');reconcileExpression=reconcile?.initializer?.getText(tree)||'(()=>{})';const dismount=entryStatements.find(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='dismount');assert(dismount,'actual entry unload function');dismountExpression=dismount.getText(tree);}
 const snapshot=path.join(out,'tested-source/decky-plugin/src/index.tsx');fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,entryText);testedSources.push({path:path.join(root,'decky-plugin/src/index.tsx'),actualSource:entrySource,snapshot,sha256:createHash('sha256').update(entryText).digest('hex')});
}
const pinSources={name:'wallpaper-exact-tested-sources',setup(builder){builder.onLoad({filter:bootstrapConsumers?/\.(?:tsx?|m?js|json|vue|png|svg)$/:/\.tsx?$/},args=>{
 const actualSource=wallpaperSourceOverride&&args.path===path.join(root,'decky-plugin/src/steamWallpaper.tsx')?wallpaperSourceOverride:args.path;const bytes=fs.readFileSync(actualSource),relative=path.relative(root,args.path),saved=relative.startsWith('..')||path.isAbsolute(relative)?path.join(out,'tested-source/external',createHash('sha256').update(args.path).digest('hex'),path.basename(args.path)):path.join(out,'tested-source',relative);
 fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes);
 testedSources.push({path:args.path,actualSource,snapshot:saved,sha256:createHash('sha256').update(bytes).digest('hex')});
 if(bootstrapConsumers&&/\.(vue|png|svg)$/.test(args.path))return;
 return{contents:bytes.toString('utf8'),loader:args.path.endsWith('.tsx')?'tsx':args.path.endsWith('.ts')?'ts':args.path.endsWith('.json')?'json':'js',resolveDir:path.dirname(args.path)};
});}};
const startupExport=hudStartupVariant?"export {installLoaderUpdateRow} from './decky-plugin/src/deckyLoaderUpdate';export function actualConsumerStartup({client,React,DFL,routerHook,window,SP_REACTDOM,installSteamMonitorHud,installSteamWallpaper,installLoaderUpdateRow}){"+consumerStartupExpression+";return {disposeMonitorHud,disposeWallpaper,disposeLoaderUpdate};}":'';
const consumerExports=bootstrapConsumers?startupExport+"export {installSteamMonitorHud} from './decky-plugin/src/steamMonitorHud';export {DeckyUpdaterClient} from './decky-plugin/src/deckyUpdaterClient';export const actualInstallReconcile=(client)=>("+reconcileExpression+");export function actualDismountFactory({client,stopBootstrapReconcile,bootstrapListener,disposeMonitorHud,disposeWallpaper,disposeLoaderUpdate,placement}){let dismounted=false;const frontendReceipt={},disposeHudProbe=null,activeContentIntents=new Set();"+dismountExpression+";return dismount;}":'';
const consumerPlugins=bootstrapConsumers?[(await import('./monitor_hud_vue_build.mjs')).monitorHudVuePlugin(root)]:[];
const bundleBuild=await build({metafile:bootstrapConsumers||!!teardownVariant,stdin:{contents:consumerExports+"export {installSteamWallpaper} from './decky-plugin/src/steamWallpaper';export {MirrorClient} from './decky-plugin/src/mirrorClient';"+(bindingWaitVariant?"export const actualBootstrap="+providerExpression+";export const actualBootstrapListener=(client)=>("+listenerExpression+");":""),resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',globalName:'WallpaperStartup',plugins:[pinSources,...consumerPlugins],outfile:out+'/wallpaper-startup-bundle.js'});
if(bootstrapConsumers||teardownVariant){const actualInputPaths=Object.keys(bundleBuild.metafile.inputs).filter(p=>p!=='<stdin>').map(p=>path.resolve(root,p));for(const p of actualInputPaths)assert(testedSources.some(row=>path.resolve(row.path)===p),'actual compiled input must have an exact snapshot: '+p);const drift=testedSources.filter(row=>createHash('sha256').update(fs.readFileSync(row.actualSource||row.path)).digest('hex')!==row.sha256);assert.deepEqual(drift,[],'no source drift during actual bundle compilation');fs.writeFileSync(path.join(out,'BUNDLE-INPUTS.json'),JSON.stringify({actualInputPaths,testedSources,metafile:bundleBuild.metafile},null,2)+String.fromCharCode(10));}
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright');
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
const results=[],errors=[],lifecycleResults=[];let checks=0;
const check=(value,label)=>{checks++;assert.ok(value,label)};
try{
 for(const viewport of viewports){
  for(const scenario of scenarios){
   if(onlyScenario&&scenario!==onlyScenario)continue;
   const page=await browser.newPage({viewport});page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://127.0.0.1:13140/**',route=>new URL(route.request().url()).pathname.startsWith('/asset/')?route.fulfill({contentType:'image/png',body:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==','base64')}):route.fulfill({contentType:'text/html',body:'<!doctype html><style>html,body,#root{height:100%;margin:0}.native-detail,.native-home,.native-home-root{position:absolute;inset:0}.native-images{position:absolute;inset:0;background:red}</style><div id="root"></div>'}));
   await page.goto('http://127.0.0.1:13140/');for(const file of ['react.js','react-dom.js','wallpaper-startup-bundle.js']){if(realSiblingStartup&&file==='wallpaper-startup-bundle.js')await page.evaluate(()=>{window.SP_REACT=window.React;window.SP_REACTDOM=window.ReactDOM;});await page.addScriptTag({path:path.join(out,file)});}
   await page.evaluate(({scenario,bindingWaitVariant,bootstrapConsumers,hudOnlyVariant,teardownVariant,hudTeardownVariant,hudStartupVariant,realSiblingStartup})=>{
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
    if(bootstrapConsumers)Object.defineProperty(navigator,'onLine',{value:false,configurable:true});
    const bootstrap={...(bootstrapConsumers?{updaterProtocol:1}:{}),endpoint:'ws://127.0.0.1:18181/mirror',token:'a'.repeat(64),runId:'startup-proof',hudProtocol:1,wallpaperProtocol:1};if(bindingWaitVariant&&!['already-defined','already-revoked'].includes(hudOnlyVariant))delete window.__YMCC_DECKY_MIRROR__;else window.__YMCC_DECKY_MIRROR__=hudOnlyVariant==='already-revoked'?{...bootstrap,revoked:true}:bootstrap;
    const timers=new Set(),scheduled=[],scheduleTrace=[],socketHistory=[];let sockets=0,bootstrapEvents=0;const countBootstrap=()=>bootstrapEvents++;if(bootstrapConsumers)window.addEventListener('ymcc-decky-bootstrap',countBootstrap);
    let failedOnce=false;
    class Socket{
     constructor(){if(bootstrapConsumers)this.binding={...window.__YMCC_DECKY_MIRROR__};if(bindingWaitVariant||(scenario==='send-once-fails'&&sockets>1))queueMicrotask(()=>this.emit('open'));}
     // The fake authenticated native host defaults to current foreground proof, as steamWallpaperHost.reply does.
     nativeForeground=true;holdReplies=scenario==='pending-ready-dedupe';
     listeners=new Map();sent=[];closed=false;wallpaperEnabled=true;stateRevision=1;allowReady=!!bindingWaitVariant||!['cold-connected-later','legacy-cold-no-ready'].includes(scenario);failNext=scenario==='send-once-fails'&&!failedOnce;
     addEventListener(type,fn){this.listeners.set(type,fn)}close(){this.closed=true;}
     send(text){const m=JSON.parse(text);if(hudStartupVariant&&m.command==='snapshot'&&m.subscribe!==false)queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'snapshot',runId:this.binding.runId,probeId:m.probeId,snapshot:{runId:this.binding.runId,generation:1,revision:1,ready:false,provenance:{kind:'ymcc-native',pid:4242},game:null,fan:{supported:false,enabled:false,preset:'auto',choices:[]}}})}));if(bootstrapConsumers&&m.command==='updater.status'&&m.subscribe)queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'ymcc-updater',runId:this.binding.runId,view:{protocol:1,revision:1,hostVersion:'0.0.37',phase:'available',targetVersion:'0.0.38',targetHash:'b'.repeat(64),percent:null,canCheck:true,canDownload:true,canInstall:false,gameRunning:false,message:''}})}));if(m.command==='wallpaper.context'&&this.failNext){this.failNext=false;failedOnce=true;throw Error('one interrupted send');}this.sent.push(m);if(m.command==='wallpaper.context'&&this.holdReplies)return;if(m.command==='wallpaper.context'&&m.requestReady&&this.allowReady)queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:(bootstrapConsumers?this.binding:bootstrap).runId,enabled:this.wallpaperEnabled,stateRevision:this.stateRevision,contextSeq:m.contextSeq})}));if(m.command==='wallpaper.context')queueMicrotask(()=>this.emit('message',{data:JSON.stringify({type:'wallpaper',runId:(bootstrapConsumers?this.binding:bootstrap).runId,enabled:this.wallpaperEnabled,...(scenario==='legacy-cold-no-ready'?{}:{stateRevision:this.stateRevision}),contextSeq:m.contextSeq,appId:m.appId,entry:m.visible&&this.wallpaperEnabled?{assetId:'fixture',revision:'1',bindingRevision:1,kind:'image',url:'http://127.0.0.1:13140/asset/fixture.png?token='+(bootstrapConsumers?this.binding:bootstrap).token}:null})}));}
     emit(type,event={}){if(type==='message'&&typeof event.data==='string'){const packet=JSON.parse(event.data);if((packet.type==='wallpaper'||packet.type==='wallpaper-ready')&&!Object.prototype.hasOwnProperty.call(packet,'foreground'))event={...event,data:JSON.stringify({...packet,foreground:this.nativeForeground})};}this.listeners.get(type)?.(event)}
    }
    let socket;const client=new WallpaperStartup.MirrorClient({bootstrap:bindingWaitVariant?WallpaperStartup.actualBootstrap:(()=>bootstrap),socket:()=>{sockets++;socket=new Socket();socketHistory.push(socket);return socket},schedule:(fn,delay)=>{if(bindingWaitVariant)scheduled.push(fn);const record={delay,at:performance.now(),fired:null,canceled:null};if(bindingWaitVariant)scheduleTrace.push(record);const t=setTimeout(()=>{timers.delete(t);record.fired=performance.now();fn()},delay);record.timer=t;timers.add(t);return t},cancel:t=>{timers.delete(t);const record=scheduleTrace.find(row=>row.timer===t);if(record)record.canceled=performance.now();clearTimeout(t)},clientId:'wallpaper-startup-proof'});
    // The real HUD owns a passive connection even when the sidebar is closed.
    if(!bindingWaitVariant){client.setPassiveDemand('hud',true);if(scenario!=='cold-connected-later')socket.emit('open');}
    const lateHome=scenario.startsWith('late-home-'),lateNonhome=scenario==='late-nonhome-idle',normalHome=scenario==='normal-home-delayed-holder',detailScope=scenario==='detail-ignores-home-holder',homePage=lateHome||normalHome||realSiblingStartup,globalCarrier=lateHome||lateNonhome,nativeSelection=scenario==='late-home-native-selection'||normalHome||detailScope;
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
    if((globalCarrier||realSiblingStartup)&&scenario!=='late-home-delayed-native-root')ReactDOM.flushSync(()=>reactRoot.render(React.createElement(Original)));
    const nativeWindow={m_BrowserWindow:window,m_locationPathname:homePage?'/library/home':lateNonhome?'/library/collections':'/library/app/730',m_history:{listen(listener){historyListeners.add(listener);return()=>historyListeners.delete(listener)}}};
    const patches=new Map(),logs=[];const host={get __YMCC_DECKY_MIRROR__(){return bindingWaitVariant?window.__YMCC_DECKY_MIRROR__:bootstrap},addEventListener:(...args)=>window.addEventListener(...args),removeEventListener:(...args)=>window.removeEventListener(...args),console:{info:value=>logs.push(value)},...(bootstrapConsumers?{setTimeout:window.setTimeout.bind(window),clearTimeout:window.clearTimeout.bind(window)}:{})};
    // Explicit module teardown control uses the same production discovery and
    // real React root, with inert Full-mode exports and no native game/popup.
    const hudCarrierControl=!!hudTeardownVariant||!!hudStartupVariant;
    const hudModeContext=hudCarrierControl?React.createContext(1):null;
    const hudStore={CompositionStateStore:{m_Instance:{IsGamepadUIOverlayWindow:()=>false}}};
    const hudFlags=Object.fromEntries(['NotFocusable','TransparentParentWindow','ForceBrowserVisible','NoTaskbarIcon','NoWindowShadow','NoRoundedCorners','Hidden','ApplyBrowserScaleToDimensions','IgnoreSteamDisplayScale','DisableDPIScale','NoScalePosition','NoScaleSize'].map((key,i)=>[key,1<<i]));
    const hudKinds={Notification:1,Tooltip:2,StandaloneContextMenu:4};
    function hudFlagFactory(kind){return kind===hudKinds.Notification?hudFlags.NotFocusable|hudFlags.TransparentParentWindow|hudFlags.ForceBrowserVisible:0;}
    function hudNativeHook(){const params={bNoInitialShow:true,bNoFocusOnShow:true},api={SetHideOnClose(){},Close(){}};api.SetHideOnClose;api.Close();throw Error('No native popup is permitted in the inert HUD teardown control');}
    function hudComposition(){hudStore.AddMinimumCompositionStateRequest;hudStore.ChangeMinimumCompositionStateRequest;hudStore.RemoveMinimumCompositionStateRequest;}
    function hudGetter(){return hudStore.CompositionStateStore;}
    function hudUseMode(){return React.useContext(hudModeContext);}
    const hudUi=hudCarrierControl?{modules:new Map([[1,{hudComposition}],[2,{hudGetter,hudUseMode,modes:{Full:1,Overlay:4,Headless:0}}],[3,{hudNativeHook}],[4,{hudFlagFactory,hudFlags,hudKinds}]]),EUIComposition:{Notification:1}}:{modules:new Map(),EUIComposition:{Notification:1}};
    let startupFault=hudStartupVariant;
    const hudRouter=hudCarrierControl?{addGlobalComponent(name,Component){if(startupFault==='before-registration')throw Error('inert HUD failure before registration');if(name==='YMCCSteamWallpaperRouteFallback'&&startupFault==='wallpaper-global')throw Error('inert wallpaper global registration failure');globalComponents.set(name,Component);renderedGlobals=[];renderGlobals();if(startupFault==='after-registration')throw Error('inert HUD failure after registration');if(name==='YMCCSteamWallpaperRouteFallback'&&startupFault==='wallpaper-global-committed')throw Error('inert wallpaper global committed registration failure');if(name==='YMCC.DeckyLoaderUpdate'&&startupFault==='updater-global-committed')throw Error('inert updater global committed registration failure');},removeGlobalComponent(name){if(window.startup?.hudRemovalFault)throw Error('inert HUD global removal failure');globalComponents.delete(name);renderedGlobals=[];renderGlobals();}}:{addGlobalComponent(){},removeGlobalComponent(){}};
    if(hudCarrierControl){host.crypto=window.crypto;host.g_PopupManager={GetPopups:()=>[]};}
    if(realSiblingStartup){
     host.document=document;host.location=window.location;
     host.DeckyPluginLoader={deckyState:{publicState:()=>({isLoaderUpdating:false,versionInfo:{current:'v3.3.1'}})}};
     hudUi.findSP=()=>window;hudUi.Router={WindowStore:{GamepadUIMainWindowInstance:nativeWindow}};
     hudUi.modules.set('wallpaper-home',{RecentGamesBackgroundContainer:'native-home',RecentGamesBackgroundImages:'native-home-images',RecentGamesContainer:'native-home-root'});
     hudUi.modules.set('wallpaper-detail',{HeaderBackgroundImage:'native-detail',ImgSrc:'native-images',TopCapsule:'top-capsule'});
     hudUi.gamepadDialogClasses={Field:'native-field'};hudUi.Field='div';
     hudRouter.addPatch=(path,patch)=>{if(startupFault==='wallpaper-first-patch'||path==='/library/app/:appid'&&startupFault==='wallpaper-second-patch')throw Error('inert wallpaper route registration failure');patches.set(path,patch);if(path==='/library/app/:appid'&&startupFault==='wallpaper-second-patch-committed')throw Error('inert wallpaper route committed registration failure');return patch;};
     hudRouter.removePatch=(path,patch)=>{if(patches.get(path)===patch)patches.delete(path);};
    }
    const installHud=()=>WallpaperStartup.installSteamMonitorHud({host,react:React,reactDom:ReactDOM,ui:hudUi,routerHook:hudRouter,client});
    if(hudStartupVariant){
     const hudHost=new Proxy(host,{defineProperty(target,key,descriptor){if(key==='__YMCC_STEAM_MONITOR_HUD__'&&startupFault==='diagnostic-publication')throw Error('inert HUD diagnostic publication failure');return Reflect.defineProperty(target,key,descriptor);}});
     const calls=[],releases=[];let setup=null,initialError=null,replacement=null,replacementClient=null;
     const invoke=owner=>WallpaperStartup.actualConsumerStartup({client:owner,React,DFL:hudUi,routerHook:hudRouter,window:hudHost,SP_REACTDOM:ReactDOM,
      installSteamMonitorHud:deps=>{calls.push('hud');return WallpaperStartup.installSteamMonitorHud(deps);},
      installSteamWallpaper:deps=>{calls.push('wallpaper');if(realSiblingStartup)return WallpaperStartup.installSteamWallpaper(deps);if(startupFault==='wallpaper-error')throw Error('inert wallpaper startup failure');return()=>releases.push('wallpaper');},
      installLoaderUpdateRow:deps=>{calls.push('updater');if(realSiblingStartup)return WallpaperStartup.installLoaderUpdateRow(deps);if(startupFault==='updater-error')throw Error('inert updater startup failure');return()=>releases.push('updater');}});
     const baseline=resources();try{setup=invoke(client);}catch(error){initialError=String(error?.message??error);}
     window.startup={client,timers,resources,globalComponents,calls,releases,baseline,initialError,get sockets(){return sockets},observe(){return{error:initialError,disposed:client.disposed,connected:client.snapshot().connected,demands:[...client.passiveConsumers],extra:client.extraListeners.size,connection:client.connectionListeners.size,activeSockets:socketHistory.filter(s=>!s.closed).length,timers:timers.size,globals:globalComponents.size,patches:patches.size,images:document.querySelectorAll('[data-ymcc-wallpaper-surface] img').length,diagnosticPresent:!!hudHost.__YMCC_STEAM_MONITOR_HUD__,roots:hudHost.__YMCC_STEAM_MONITOR_HUD__?.inspect().roots??null,resources:resources(),calls:[...calls],releases:[...releases]};},late(){ReactDOM.flushSync(()=>globalRoot.render(null));renderedGlobals=[];ReactDOM.flushSync(renderGlobals);for(let i=0;i<32;i++)window.dispatchEvent(new Event('ymcc-decky-bootstrap'));},retry(){startupFault=null;replacementClient=new WallpaperStartup.MirrorClient({bootstrap:WallpaperStartup.actualBootstrap,socket:()=>{sockets++;socket=new Socket();socketHistory.push(socket);return socket},schedule:(fn,delay)=>{const id=setTimeout(()=>{timers.delete(id);fn()},delay);timers.add(id);return id},cancel:id=>{timers.delete(id);clearTimeout(id)},clientId:'startup-replacement-proof'});replacement=invoke(replacementClient);},replacementState(){return{connected:replacementClient?.snapshot().connected,hud:hudHost.__YMCC_STEAM_MONITOR_HUD__?.inspect(),oldDisposed:client.disposed,oldDemands:[...client.passiveConsumers],patches:patches.size,globals:globalComponents.size,images:document.querySelectorAll('[data-ymcc-wallpaper-surface] img').length,activeSockets:socketHistory.filter(s=>!s.closed).length,timers:timers.size};},close(){for(const item of [replacement,setup]){item?.disposeLoaderUpdate?.();item?.disposeWallpaper?.();item?.disposeMonitorHud?.();}replacementClient?.dispose();client.dispose();if(bootstrapConsumers)window.removeEventListener('ymcc-decky-bootstrap',countBootstrap);globalRoot.unmount();globalElement.remove();reactRoot.unmount();}};
     return;
    }
    const disposeHud=bootstrapConsumers?installHud():null;
    const updater=bootstrapConsumers?new WallpaperStartup.DeckyUpdaterClient(client,'bootstrap-consumer-proof'):null;if(!hudOnlyVariant)updater?.open();
    const dispose=hudOnlyVariant?(()=>{}):WallpaperStartup.installSteamWallpaper({host,react:React,reactDom:ReactDOM,ui:{findSP:()=>window,Router:{WindowStore:{GamepadUIMainWindowInstance:nativeWindow}},modules:new Map([['detail',{HeaderBackgroundImage:'native-detail',ImgSrc:'native-images',TopCapsule:'top-capsule'}],...(globalCarrier||normalHome||detailScope?[['home',{RecentGamesBackgroundContainer:'native-home',RecentGamesBackgroundImages:'native-home-images',RecentGamesContainer:'native-home-root'}]]:[])])},routerHook:{addPatch(p,f){patches.set(p,f);return f},removePatch(p){patches.delete(p)},...(globalCarrier?{addGlobalComponent(name,Component){globalComponents.set(name,Component);renderGlobals()},removeGlobalComponent(name){if(window.startup?.teardownFault)throw Error('inert wallpaper global removal failure');globalComponents.delete(name);renderGlobals()}}:{})},client});
    let bootstrapHandler=null;if(bindingWaitVariant){bootstrapHandler=WallpaperStartup.actualBootstrapListener(client);window.addEventListener('ymcc-decky-bootstrap',bootstrapHandler);}
    const stopReconcile=bootstrapConsumers?WallpaperStartup.actualInstallReconcile(client):()=>{};let placementsDisposed=0;const dismount=bootstrapConsumers?WallpaperStartup.actualDismountFactory({client,stopBootstrapReconcile:stopReconcile,bootstrapListener:bootstrapHandler,disposeMonitorHud:disposeHud,disposeWallpaper:dispose,disposeLoaderUpdate:()=>updater.close(),placement:{dispose(){placementsDisposed++;}}}):null;
    // The host announces readiness once per connection. A late Surface missed it.
    if(!['cold-connected-later','legacy-cold-no-ready','pending-ready-dedupe'].includes(scenario))socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:bootstrap.runId,enabled:true})});
    if(scenario==='hidden-then-visible')Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});
    const mount=(appId='730')=>{const props=homePage||appId===null?{}:{match:{params:{appid:appId}}};const route=patches.get(homePage?'/library/home':'/library/app/:appid')({children:React.createElement(Original,props)});reactRoot.render(route.children);};
    if(!globalCarrier&&!hudOnlyVariant)mount(detailScope?null:'730');window.startup={hudControl:{host,installHud,disposeHud,get diagnostic(){return host.__YMCC_STEAM_MONITOR_HUD__}},remountGlobals(){ReactDOM.flushSync(()=>globalRoot.render(null));renderedGlobals=[];ReactDOM.flushSync(renderGlobals);},client,scheduled,scheduleTrace,updater,socketHistory,dismount,stopReconcile,hudStatus(){return host.__YMCC_STEAM_MONITOR_HUD__?.inspect()??null},get bootstrapEvents(){return bootstrapEvents},get placementsDisposed(){return placementsDisposed},provideBinding(kind='normal'){window.__YMCC_DECKY_MIRROR__=kind==='off'?{...bootstrap,wallpaperProtocol:undefined,...(hudOnlyVariant?{hudProtocol:undefined}:{})}:kind==='invalid'?{...bootstrap,endpoint:'ws://example.com:18181/mirror'}:kind==='revoked'?{...bootstrap,revoked:true}:bootstrap;},get socket(){return socket},logs,timers,patches,resources,globalComponents,navigate(path){nativeWindow.m_locationPathname=path;for(const listener of historyListeners)listener({pathname:path},'PUSH')},mountNative(){reactRoot.render(React.createElement(Original))},changeNativeSelection(appId,replace){nativeAppId=appId;if(replace)nativeGeneration++;rerenderNative()},showNativeHolder(){nativeHolderReady=true;rerenderNative()},disposeGlobals(){if(bootstrapConsumers)window.removeEventListener('ymcc-decky-bootstrap',countBootstrap);if(bootstrapHandler)window.removeEventListener('ymcc-decky-bootstrap',bootstrapHandler);globalRoot.unmount();globalElement.remove()},get sockets(){return sockets},mount,dispose,unmount(){reactRoot.unmount();reactRoot=ReactDOM.createRoot(document.getElementById('root'))}};
   },{scenario,bindingWaitVariant,bootstrapConsumers,hudOnlyVariant,teardownVariant,hudTeardownVariant,hudStartupVariant,realSiblingStartup});
   if(hudStartupVariant){
    const checkpoints=[],checksBefore=checks;let failure=null;
    try{
     await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
     if(realSiblingStartup&&hudStartupVariant==='normal')await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0,null,{timeout:2500});
     const probe=await page.evaluate(()=>startup.observe());checkpoints.push({stage:'after-initializer',...probe});
     fs.writeFileSync(path.join(out,'HUD-STARTUP-PROBE.json'),JSON.stringify({scenario,viewport,hudStartupVariant,realSiblingStartup,probe,baseline:await page.evaluate(()=>startup.baseline),browserVersion:browser.version(),fixtureSha256:createHash('sha256').update(fs.readFileSync(new URL(import.meta.url))).digest('hex'),actualEntryConsumerStartup:consumerStartupExpression,scope:'Exact entry initializer statements + real HUD/MirrorClient/React, inert Full-mode capability exports; wallpaper/updater factories are inert collaborators. No live Steam/native popup or physical sleep/CPU acceptance'},null,2)+String.fromCharCode(10),{flag:'wx'});
     if(hudStartupVariant==='normal'){
      check(probe.error===null&&!probe.disposed&&probe.connected&&probe.diagnosticPresent&&probe.roots===1&&probe.activeSockets===1,'successful original consumer startup retains its live HUD and single shared connection');
      check(JSON.stringify(probe.calls)===JSON.stringify(['hud','wallpaper','updater'])&&probe.releases.length===0,'successful consumer startup initializes each sibling once without rollback');
      if(realSiblingStartup)check(probe.patches===2&&probe.globals===3&&probe.images===1&&probe.resources.mutationObservers>0,'real three-consumer startup owns its two routes, three globals and one visible wallpaper image');
     }else{
      const messages={'before-registration':'inert HUD failure before registration','after-registration':'inert HUD failure after registration','diagnostic-publication':'inert HUD diagnostic publication failure','wallpaper-error':'inert wallpaper startup failure','updater-error':'inert updater startup failure','wallpaper-first-patch':'inert wallpaper route registration failure','wallpaper-second-patch':'inert wallpaper route registration failure','wallpaper-second-patch-committed':'inert wallpaper route committed registration failure','wallpaper-global':'inert wallpaper global registration failure','wallpaper-global-committed':'inert wallpaper global committed registration failure','updater-global-committed':'inert updater global committed registration failure'};
      check(probe.error===messages[hudStartupVariant],'entry preserves the original failing initializer error');
      check(probe.disposed&&!probe.connected&&probe.activeSockets===0&&probe.timers===0&&probe.demands.length===0,'failed entry initialization must release its original shared connection and demand');
      if(realSiblingStartup)check(probe.patches===0&&probe.resources.listeners===await page.evaluate(()=>startup.baseline.listeners)&&probe.resources.mutationObservers===0&&probe.resources.intersectionObservers===0,'partial sibling startup must return route patches and foreground observers');
      check(!probe.diagnosticPresent&&probe.globals===0&&probe.extra===0&&probe.connection===0,'failed HUD or sibling startup must release partial HUD registration and subscriptions');
      check(probe.resources.listeners===await page.evaluate(()=>startup.baseline.listeners),'failed startup returns all module event listeners');
      check(JSON.stringify(probe.releases)===JSON.stringify(hudStartupVariant==='updater-error'?['wallpaper']:[]),'entry rolls back only successfully initialized sibling owners');
      await page.evaluate(()=>startup.late());await page.waitForTimeout(50);const late=await page.evaluate(()=>startup.observe());checkpoints.push({stage:'late-old-root-and-bootstrap',...late});
      check(late.extra===0&&late.connection===0&&late.demands.length===0&&late.activeSockets===0&&late.timers===0&&!late.diagnosticPresent,'failed initializer old roots and 32 late bootstrap events cannot reacquire work');
      await page.evaluate(()=>startup.retry());await page.waitForFunction(real=>startup.replacementState().connected&&startup.replacementState().hud?.roots===1&&(!real||document.querySelector('[data-ymcc-wallpaper-surface] img')?.naturalWidth>0),realSiblingStartup,{timeout:2500});
      const recovered=await page.evaluate(()=>startup.replacementState());checkpoints.push({stage:'new-module-initialization',...recovered});
      check(recovered.oldDisposed&&recovered.oldDemands.length===0&&recovered.activeSockets===1&&recovered.timers===0&&recovered.hud.registered,'a fresh module initializes after the fault clears without a duplicate old connection');
      if(realSiblingStartup)check(recovered.patches===2&&recovered.globals===3&&recovered.images===1,'a fresh real three-consumer module paints its wallpaper after a partial startup failure');
     }
    }catch(error){failure=String(error);}
    finally{await page.evaluate(()=>startup.close());}
    const final=await page.evaluate(()=>startup.observe());checkpoints.push({stage:'all-test-owners-released',...final});
    if(final.activeSockets!==0||final.timers!==0||final.extra!==0||final.connection!==0||final.diagnosticPresent||final.globals!==0||realSiblingStartup&&(final.patches!==0||final.resources.listeners!==0||final.resources.mutationObservers!==0||final.resources.intersectionObservers!==0)||errors.length)failure=failure||'startup resources or browser errors remained';
    const report={status:failure?'FAIL':'PASS',scenario,viewport,hudStartupVariant,realSiblingStartup,checks:checks-checksBefore,failure,checkpoints,errors,entrySource,entrySha256:createHash('sha256').update(entryText).digest('hex'),actualEntryConsumerStartup:consumerStartupExpression,scope:'Production entry initializer + real HUD/MirrorClient/React; inert sibling factories and Steam capability exports; no native popup or live Steam acceptance'};
    fs.writeFileSync(path.join(out,'HUD-STARTUP-RESULTS.json'),JSON.stringify(report,null,2)+String.fromCharCode(10),{flag:'wx'});
    check(!failure,'actual original HUD/entry startup rollback: '+failure);lifecycleResults.push(report);results.push({viewport,scenario,hudStartupVariant,checks:report.checks});await page.close();continue;
   }
   if(hudOnlyVariant){
    const checkpoints=[],checksBefore=checks;
    const observe=()=>page.evaluate(()=>({sockets:startup.sockets,activeSockets:startup.socketHistory.filter(s=>!s.closed).length,timers:startup.timers.size,connected:startup.client.snapshot().connected,hud:startup.hudStatus(),events:startup.bootstrapEvents,requests:startup.socketHistory.flatMap(s=>s.sent).filter(m=>m.command==='updater.request').length,images:document.querySelectorAll('[data-ymcc-wallpaper-surface] img').length}));
    const sample=async label=>{await page.waitForTimeout(40);const state=await observe();checkpoints.push({label,...state});return state;};let failure=null;
    try{
     const initial=await sample('pure-hud-no-sidebar-wallpaper-or-updater');await page.evaluate(()=>startup.initialWaitCallbacks=[...startup.scheduled]);
     if(hudOnlyVariant==='already-defined'){
      check(initial.connected&&initial.sockets===1&&initial.hud?.protocol===true&&initial.timers===0&&initial.events===1,'initial valid native binding adds no observation retry');
     }else if(hudOnlyVariant==='already-revoked'){
      check(!initial.connected&&initial.sockets===0&&initial.timers===0&&initial.events===0,'initial explicit native revocation adds no wait or socket');
     }else{
      check(!initial.connected&&initial.sockets===0&&initial.timers===1&&initial.events===0,'pure HUD missing native binding owns one shared observation retry without socket authority');
      if(hudOnlyVariant==='dispose'){
       // Test owner retains the client object without any feature transport
       // demand; release the entry's actual observer independently of dispose.
       await page.evaluate(()=>{startup.realClientDispose=startup.client.dispose.bind(startup.client);startup.client.dispose=()=>{};startup.dismount();startup.dismount();});const retired=await sample('entry-unloaded-before-binding');check(retired.hud===null&&retired.timers===0&&retired.sockets===0,'actual entry unload cancels its sole missing-binding observation owner');
      }
      await page.evaluate(kind=>startup.provideBinding(kind),hudOnlyVariant);await page.waitForTimeout(500);const available=await sample('late-native-binding-with-original-notification-missed');
      if(hudOnlyVariant==='normal')check(available.connected&&available.sockets===1&&available.activeSockets===1&&available.hud?.protocol===true&&available.events===2&&available.timers===0,'HUD alone restores from native availability without another consumer or external bootstrap event');
      else check(available.sockets===0&&!available.connected&&available.timers===0&&available.images===0,'defined OFF invalid revoked binding or disposed entry grants no automatic transport');
      await page.evaluate(()=>{for(const callback of startup.initialWaitCallbacks)callback();});const late=await sample('retired-observation-callbacks-delivered');check(late.sockets===available.sockets&&late.events===available.events&&late.timers===0&&late.requests===0,'retired shared-observation callbacks cannot duplicate bootstrap, transport or actions');
      if(hudOnlyVariant!=='dispose'){
       await page.evaluate(()=>{startup.provideBinding('normal');window.dispatchEvent(new Event('ymcc-decky-bootstrap'));});await page.waitForFunction(()=>startup.client.snapshot().connected,null,{timeout:2500});const control=await sample('fresh-valid-original-event-positive-control');check(control.sockets===1&&control.hud?.protocol===true&&control.timers===0,'fresh valid original native event can activate the current HUD after a defined refusal');
      }
     }
     const beforeNoise=await sample('stable-before-ready-noise');await page.evaluate(()=>{for(let n=0;n<32;n++){startup.socket?.emit('open');startup.socket?.emit('message',{data:JSON.stringify({type:'hud-ready',protocol:1,runId:window.__YMCC_DECKY_MIRROR__?.runId})});}});const afterNoise=await sample('duplicate-open-and-hud-ready');check(afterNoise.events===beforeNoise.events&&afterNoise.sockets===beforeNoise.sockets&&afterNoise.timers===0&&afterNoise.requests===0,'HUD ready/open noise adds no observation polling, bootstrap or mutation');
     if(hudTeardownVariant){
      const before=await page.evaluate(()=>{startup.stopReconcile();startup.client.setPassiveDemand('hud-teardown-control',true);startup.oldHudDiagnostic=startup.hudControl.diagnostic;return{hud:startup.oldHudDiagnostic.inspect(),extra:startup.client.extraListeners.size,connection:startup.client.connectionListeners.size,shared:startup.client.snapshot().connected};});
      check(before.hud.capable&&before.hud.registered&&before.hud.roots===1&&before.hud.children===0&&before.shared,'teardown exercises the capable production HUD root without a native popup');
      await page.evaluate(fault=>{startup.hudRemovalFault=fault;try{startup.hudControl.disposeHud()}catch(error){startup.hudTeardownError=String(error?.message??error)}startup.hudControl.disposeHud();},hudTeardownVariant==='router-failure');
      await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
      const observeModule=()=>page.evaluate(()=>({error:startup.hudTeardownError,diagnosticPresent:!!startup.hudControl.diagnostic,retiredHud:startup.oldHudDiagnostic.inspect(),hudDemand:startup.client.passiveConsumers.has('hud'),sharedDemand:startup.client.passiveConsumers.has('hud-teardown-control'),connected:startup.client.snapshot().connected,extra:startup.client.extraListeners.size,connection:startup.client.connectionListeners.size,timers:startup.timers.size,globals:startup.globalComponents.size,resources:startup.resources()}));
      const probe=await observeModule();
      fs.writeFileSync(path.join(out,'HUD-MODULE-TEARDOWN-PROBE.json'),JSON.stringify({scenario,viewport,hudTeardownVariant,before,probe,browserVersion:browser.version(),fixtureSha256:createHash('sha256').update(fs.readFileSync(new URL(import.meta.url))).digest('hex'),scope:'Actual production HUD module/MirrorClient and real React root with inert capability exports; no native popup, live Steam, physical sleep or CPU benefit acceptance'},null,2)+String.fromCharCode(10),{flag:'wx'});
      check(hudTeardownVariant==='router-failure'?probe.error==='inert HUD global removal failure':probe.error===undefined,'HUD module returns the original router error only for the injected failure');
      check(!probe.diagnosticPresent&&!probe.hudDemand&&probe.retiredHud.roots===0&&probe.retiredHud.children===0,'HUD route removal failure must not strand local demand or the ownership marker');
      check(probe.extra===0&&probe.connection===0&&probe.timers===0&&probe.connected&&probe.sharedDemand,'HUD unsubscribe releases only its own observers and retains the shared transport');
      check(probe.globals===(hudTeardownVariant==='router-failure'?1:0),'failed external Steam registry removal remains observable');
      await page.evaluate(()=>{startup.remountGlobals();for(let i=0;i<32;i++){window.dispatchEvent(new Event('ymcc-decky-bootstrap'));startup.socket.emit('message',{data:JSON.stringify({type:'hud-ready',protocol:1,runId:window.__YMCC_DECKY_MIRROR__.runId})});}});
      await page.waitForTimeout(50);const late=await observeModule();
      check(!late.diagnosticPresent&&!late.hudDemand&&late.retiredHud.roots===0&&late.extra===0&&late.connection===0&&late.timers===0&&late.connected,'retained old React root and late callbacks cannot reacquire a disposed HUD owner');
      await page.evaluate(()=>{startup.hudRemovalFault=false;startup.replacementHudStop=startup.hudControl.installHud();});
      await page.waitForFunction(()=>startup.hudControl.diagnostic?.inspect().roots===1,null,{timeout:2500});
      const replacement=await page.evaluate(()=>({different:startup.hudControl.diagnostic!==startup.oldHudDiagnostic,hud:startup.hudControl.diagnostic.inspect(),demand:startup.client.passiveConsumers.has('hud'),connected:startup.client.snapshot().connected}));
      check(replacement.different&&replacement.hud.registered&&replacement.hud.capable&&replacement.demand&&replacement.connected,'the next module can acquire its local marker and current shared connection');
      await page.evaluate(()=>{startup.replacementHudStop();startup.replacementHudStop();});
      await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
      const released=await observeModule();check(!released.diagnosticPresent&&!released.hudDemand&&released.globals===0&&released.extra===0&&released.connection===0&&released.timers===0&&released.connected,'replacement normal disposal releases its owners and preserves the independent transport owner');
      lifecycleResults.push({scenario,viewport,hudTeardownVariant,before,probe,late,replacement,released});
     }
    }catch(error){failure=String(error);}
    finally{await page.evaluate(()=>{startup.replacementHudStop?.();startup.dismount();if(startup.realClientDispose)startup.realClientDispose();startup.disposeGlobals();});}
    const final=await sample('all-original-and-test-owners-released');if(final.activeSockets!==0||final.timers!==0||final.images!==0||final.hud!==null||errors.length)failure=failure||'HUD-only resources remained or browser exception';
    const report={status:failure?'FAIL':'PASS',scenario,viewport,hudOnlyVariant,hudTeardownVariant,checks:checks-checksBefore,failure,checkpoints,errors,entrySource,entrySha256:createHash('sha256').update(entryText).digest('hex'),actualEntryInitializer:reconcileExpression,actualEntryDismount:dismountExpression,scope:'Actual HUD installer protocol + original entry initializer/dismount/MirrorClient with no sidebar, wallpaper or updater transport demand. Deterministic local binding, socket and callback scheduling. No native popup rendering or installed/physical recovery acceptance.'};fs.writeFileSync(path.join(out,'hud-only-bootstrap-results.json'),JSON.stringify(report,null,2)+String.fromCharCode(10));check(!failure,'actual original HUD-only bootstrap recovery: '+failure);lifecycleResults.push(report);results.push({viewport,scenario,hudOnlyVariant,checks:report.checks});await page.close();continue;
   }
   if(bootstrapConsumers){
    const checkpoints=[],checksBefore=checks;
    const observe=()=>page.evaluate(()=>({sockets:startup.sockets,activeSockets:startup.socketHistory.filter(s=>!s.closed).length,timers:startup.timers.size,connected:startup.client.snapshot().connected,images:document.querySelectorAll('[data-ymcc-wallpaper-surface] img').length,hud:startup.hudStatus(),updater:startup.updater.snapshot(),events:startup.bootstrapEvents,requests:startup.socketHistory.flatMap(s=>s.sent).filter(m=>m.command==='updater.request').length,statusReads:startup.socketHistory.flatMap(s=>s.sent).filter(m=>m.command==='updater.status'&&m.subscribe).length,contexts:startup.socketHistory.flatMap(s=>s.sent).filter(m=>m.command==='wallpaper.context').length,placementsDisposed:startup.placementsDisposed,offline:!navigator.onLine}));
    const sample=async label=>{await page.waitForTimeout(35);const state=await observe();checkpoints.push({label,...state});return state;};let failure=null;
    try{
     const initial=await sample('missing-native-binding-offline');check(!initial.connected&&initial.sockets===0&&initial.timers===1&&initial.offline,'absent local binding owns one original retry while browser is offline');
     await page.evaluate(()=>startup.provideBinding());await page.waitForFunction(()=>startup.client.snapshot().connected&&document.querySelector('[data-ymcc-wallpaper-surface] img'),null,{timeout:2500});
     const recovered=await sample('late-native-binding-without-event');check(recovered.hud?.protocol===true,'actual HUD installer synchronizes protocol after missed native event');check(recovered.sockets===1&&recovered.activeSockets===1&&recovered.timers===0&&recovered.events===2,'one native availability edge and one connection edge reconcile original subscribers exactly once each');check(recovered.updater.connected&&recovered.updater.supported&&recovered.updater.view?.targetVersion==='0.0.38'&&recovered.statusReads===1,'actual updater reads current supported state once through shared recovered transport');
     await page.evaluate(()=>{startup.client.setVisible(true);const runId=window.__YMCC_DECKY_MIRROR__.runId;for(let revision=1;revision<=32;revision++)startup.socket.emit('message',{data:JSON.stringify({type:'snapshot',runId,snapshot:{runId,generation:1,revision,ready:true,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]}}})});startup.snapshotStormRevision=startup.client.snapshot().snapshot?.revision;startup.client.setVisible(false);});const snapshots=await sample('actual-business-snapshot-storm');check(await page.evaluate(()=>startup.snapshotStormRevision===32)&&snapshots.events===recovered.events&&snapshots.timers===0,'32 accepted business snapshots do not dispatch extra bootstrap events');
     await page.evaluate(()=>{for(let n=0;n<32;n++){startup.socket.emit('open');startup.socket.emit('message',{data:JSON.stringify({type:'wallpaper-ready',runId:window.__YMCC_DECKY_MIRROR__.runId,enabled:true})});}});const duplicates=await sample('duplicate-transport-and-ready-events');check(duplicates.events===recovered.events&&duplicates.sockets===1&&duplicates.statusReads===1&&duplicates.timers===0,'duplicate open/ready events do not create bootstrap or updater read storms');
     await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});document.dispatchEvent(new Event('visibilitychange'));document.dispatchEvent(new Event('freeze'));});const hidden=await sample('hidden-frozen-route-shared-consumers');check(hidden.images===0&&hidden.connected&&hidden.activeSockets===1&&hidden.events===recovered.events,'hidden route releases media while current HUD/updater transport stays owned');
     await page.evaluate(()=>{window.dispatchEvent(new Event('offline'));document.dispatchEvent(new Event('resume'));window.dispatchEvent(new PageTransitionEvent('pageshow',{persisted:true}));Object.defineProperty(document,'visibilityState',{value:'visible',configurable:true});document.dispatchEvent(new Event('visibilitychange'));});await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img'),null,{timeout:2500});const resumed=await sample('visible-resume-pageshow-offline');check(resumed.events===recovered.events&&resumed.sockets===1&&resumed.statusReads===1&&resumed.timers===0,'renderer wake events restore selected media without reconnect or updater status storm');
     await page.evaluate(()=>{startup.uncertain=startup.updater.request('download',startup.updater.snapshot().view).then(()=>({resolved:true}),error=>({rejected:true,message:error.message}));startup.oldSocket=startup.socket;startup.oldCallbacks=[...startup.scheduled];startup.oldSocket.close();startup.oldSocket.emit('close');});const lost=await sample('transport-lost-unconfirmed-request');check(!lost.connected&&lost.updater.view===null&&!lost.updater.pending&&lost.requests===1,'lost transport retires unconfirmed updater action and stale view');
     await page.waitForFunction(()=>startup.client.snapshot().connected&&startup.sockets===2,null,{timeout:3000});const reconnected=await sample('same-binding-reconnect');check(reconnected.events===3&&reconnected.activeSockets===1&&reconnected.timers===0&&reconnected.requests===1&&reconnected.statusReads===2,'same binding reconnects once and reads status without action replay');check(await page.evaluate(async()=>!!(await startup.uncertain).rejected),'uncertain update rejects instead of being replayed');
     await page.evaluate(()=>{window.__YMCC_DECKY_MIRROR__={...window.__YMCC_DECKY_MIRROR__,runId:'next-verified-native-session',token:'c'.repeat(64)};window.dispatchEvent(new Event('ymcc-decky-bootstrap'));});await page.waitForFunction(()=>startup.client.snapshot().connected&&startup.sockets===3,null,{timeout:2500});const changed=await sample('new-native-session-and-token');check(changed.events===5&&changed.requests===1&&changed.activeSockets===1&&changed.statusReads===3,'new native session retires prior socket, reconciles once and does not replay old mutation');
     await page.evaluate(()=>{startup.oldSocket.emit('open');startup.oldSocket.emit('message',{data:JSON.stringify({type:'ymcc-updater',runId:'startup-proof',view:{}})});for(const callback of startup.oldCallbacks)callback();window.dispatchEvent(new Event('offline'));});const late=await sample('retired-socket-and-timer-callbacks-offline');check(late.events===changed.events&&late.requests===1&&late.sockets===3&&late.timers===0&&late.updater.view?.targetVersion==='0.0.38','retired callbacks cannot replace current state or reconnect ownership');
     await page.evaluate(()=>{startup.savedBinding=window.__YMCC_DECKY_MIRROR__;window.__YMCC_DECKY_MIRROR__={...startup.savedBinding,revoked:true};window.dispatchEvent(new Event('ymcc-decky-bootstrap'));});const revoked=await sample('explicit-native-revocation');check(!revoked.connected&&revoked.activeSockets===0&&revoked.images===0&&revoked.updater.view===null&&revoked.timers===0,'explicit native OFF cancels transport/media/status without restart');
     await page.evaluate(()=>{for(const callback of startup.scheduled)callback();startup.socket.emit('open');});const revokedLate=await sample('late-callbacks-after-native-off');check(revokedLate.sockets===3&&!revokedLate.connected&&revokedLate.timers===0,'retired callbacks cannot bypass explicit OFF');
     await page.evaluate(()=>{window.__YMCC_DECKY_MIRROR__={...startup.savedBinding,endpoint:'ws://example.com:18181/mirror'};window.dispatchEvent(new Event('ymcc-decky-bootstrap'));});const invalid=await sample('invalid-nonloopback-native-binding');check(invalid.sockets===3&&!invalid.connected&&invalid.activeSockets===0&&invalid.images===0,'reconciliation cannot authorize a nonloopback binding');
     await page.evaluate(()=>{window.__YMCC_DECKY_MIRROR__=startup.savedBinding;window.dispatchEvent(new Event('ymcc-decky-bootstrap'));});await page.waitForFunction(()=>startup.client.snapshot().connected&&startup.sockets===4,null,{timeout:2500});const enabled=await sample('fresh-explicit-native-grant');check(enabled.events===invalid.events+2&&enabled.timers===0&&enabled.requests===1&&enabled.statusReads===4,'fresh native grant recovers current state without old request replay');
     // Keep a test-owned transport alive to expose entry unsubscribe defects
     // that the final client.dispose would otherwise mask. The actual entry
     // body runs unchanged; the real client disposal runs in finally below.
     await page.evaluate(()=>{startup.client.setPassiveDemand('test-owned-remaining-client',true);startup.realClientDispose=startup.client.dispose.bind(startup.client);startup.client.dispose=()=>{};startup.dismount();startup.dismount();});const disposed=await sample('actual-entry-dismount-with-test-owned-client');check(disposed.hud===null&&disposed.images===0&&disposed.placementsDisposed===1&&!disposed.updater.connected,'actual idempotent entry dismount releases consumer owners');
     await page.evaluate(()=>{startup.socket.close();startup.socket.emit('close');});await page.waitForFunction(()=>startup.sockets===5&&startup.client.snapshot().connected,null,{timeout:3000});const afterUnload=await sample('test-owned-reconnect-after-entry-unload');check(afterUnload.events===disposed.events&&afterUnload.requests===1&&afterUnload.statusReads===disposed.statusReads&&afterUnload.images===0&&afterUnload.hud===null,'entry unsubscribe prevents bootstrap/consumer work after unload with another transport owner');
    }catch(error){failure=String(error);}
    finally{await page.evaluate(()=>{startup.stopReconcile();startup.dismount();if(startup.realClientDispose)startup.realClientDispose();else startup.client.dispose();startup.unmount();startup.disposeGlobals();});}
    const final=await sample('all-test-owned-resources-released');if(final.activeSockets!==0||final.timers!==0||final.images!==0||errors.length)failure=failure||'test-owned resources or browser exception remained';
    const report={status:failure?'FAIL':'PASS',scenario,viewport,checks:checks-checksBefore,failure,checkpoints,errors,entrySource,entrySha256:createHash('sha256').update(entryText).digest('hex'),actualEntryInitializer:reconcileExpression,actualEntryDismount:dismountExpression,scope:'Actual HUD installer/protocol, updater client, wallpaper/client and entry initializer/dismount. Deterministic socket/native binding interleavings and browser offline state. No native popup rendering, Steam login/updates, physical S3/S0 or real network reconfiguration acceptance.'};fs.writeFileSync(path.join(out,'bootstrap-consumers-results.json'),JSON.stringify(report,null,2)+String.fromCharCode(10));check(!failure,'actual original bootstrap consumers recovery: '+failure);lifecycleResults.push(report);results.push({viewport,scenario,bootstrapConsumers,checks:report.checks});await page.close();continue;
   }
   if(bindingWaitVariant){
    const observe=()=>page.evaluate(()=>({sockets:startup.sockets,timers:startup.timers.size,connected:startup.client.snapshot().connected,routeRoots:document.querySelectorAll('[data-ymcc-wallpaper-route]').length,images:document.querySelectorAll('[data-ymcc-wallpaper-surface] img').length,hasNativeBinding:!!window.__YMCC_DECKY_MIRROR__,contexts:startup.socket?.sent.filter(m=>m.command==='wallpaper.context').length??0,focus:document.hasFocus(),visibility:document.visibilityState,clock:performance.now(),schedule:startup.scheduleTrace.map(({delay,at,fired,canceled})=>({delay,at,fired,canceled})),logs:[...startup.logs],packets:startup.socket?.sent.map(({command,contextSeq,visible,requestReady,appId})=>({command,contextSeq,visible,requestReady,appId}))??[]}));
    await page.waitForTimeout(100);const initial=await observe();
    await page.evaluate(()=>{startup.oldWaitingCallbacks=[...startup.scheduled];});
    if(bindingWaitVariant==='hidden'){await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'hidden',configurable:true});document.dispatchEvent(new Event('visibilitychange'));});await page.waitForTimeout(40);}
    if(bindingWaitVariant==='dispose'){await page.evaluate(()=>startup.dispose());await page.waitForTimeout(40);}
    const withdrawn=await observe();await page.evaluate(kind=>startup.provideBinding(kind),bindingWaitVariant);
    await page.waitForTimeout(700);const afterBinding=await observe();
    await page.evaluate(()=>{for(const callback of startup.oldWaitingCallbacks)callback();});await page.waitForTimeout(30);const afterLate=await observe();
    let positiveControlError=null;
    if(bindingWaitVariant==='normal')await page.evaluate(()=>window.dispatchEvent(new Event('ymcc-decky-bootstrap')));
    else if(bindingWaitVariant==='hidden')await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{value:'visible',configurable:true});document.dispatchEvent(new Event('visibilitychange'));});
    else if(bindingWaitVariant==='off'||bindingWaitVariant==='invalid'||bindingWaitVariant==='revoked')await page.evaluate(()=>{startup.provideBinding('normal');window.dispatchEvent(new Event('ymcc-decky-bootstrap'));});
    else await page.evaluate(()=>window.dispatchEvent(new Event('ymcc-decky-bootstrap')));
    if(bindingWaitVariant!=='dispose')try{await page.waitForFunction(()=>startup.client.snapshot().connected&&document.querySelector('[data-ymcc-wallpaper-surface] img'),null,{timeout:2500});}catch(error){positiveControlError=String(error);}
    else await page.waitForTimeout(80);
    const control=await observe();
    const foregroundChecks=[];
    if(bindingWaitVariant==='normal'&&control.connected&&control.images===1){
     for(const proof of ['missing','false','true']){
      await page.evaluate(proof=>{const socket=startup.socket;socket.nativeForeground=proof==='missing'?undefined:proof==='true';const m=socket.sent.filter(message=>message.command==='wallpaper.context').at(-1);socket.emit('message',{data:JSON.stringify({type:'wallpaper',runId:'startup-proof',enabled:true,stateRevision:socket.stateRevision,contextSeq:m.contextSeq,appId:m.appId,entry:{assetId:'fixture',revision:'1',bindingRevision:1,kind:'image',url:'http://127.0.0.1:13140/asset/fixture.png?token='+window.__YMCC_DECKY_MIRROR__.token}})});},proof);
      let error=null;try{await page.waitForFunction(allowed=>!!document.querySelector('[data-ymcc-wallpaper-surface] img')===allowed,proof==='true',{timeout:2500});}catch(caught){error=String(caught);}
      foregroundChecks.push({proof,error,observation:await observe()});
     }
    }
    let passed=initial.routeRoots===1&&initial.sockets===0&&initial.timers===1&&errors.length===0;
    if(bindingWaitVariant==='normal')passed=passed&&afterBinding.connected&&afterBinding.images===1&&afterBinding.timers===0&&afterBinding.sockets===1&&afterLate.sockets===1&&control.sockets===1;
    else passed=passed&&afterBinding.sockets===0&&afterBinding.images===0&&afterBinding.timers===0&&afterLate.sockets===0&&afterLate.timers===0;
    if(bindingWaitVariant==='dispose')passed=passed&&control.sockets===0&&control.images===0&&control.timers===0;
    else passed=passed&&!positiveControlError&&control.connected&&control.images===1&&control.timers===0&&control.sockets===1;
    await page.evaluate(()=>{startup.unmount();startup.dispose();startup.disposeGlobals();startup.client.dispose()});await page.waitForTimeout(30);const disposed=await observe();passed=passed&&disposed.timers===0&&disposed.images===0;
    if(bindingWaitVariant==='normal')passed=passed&&foregroundChecks.length===3&&foregroundChecks.every(row=>!row.error&&row.observation.images===(row.proof==='true'?1:0));
    const bindingReport={foregroundChecks,status:passed?'PASS':'FAIL',variant:bindingWaitVariant,scenario,viewport,initial,withdrawn,afterBinding,afterLate,control,disposed,positiveControlError,errors,provider:{expression:providerExpression,listener:listenerExpression,entrySha256:createHash('sha256').update(entryText).digest('hex')},scope:'Actual React/Edge production consumer + client and actual entry provider/listener; deterministic Steam route/socket. No installed Steam or physicalsleep acceptance.'};
    fs.writeFileSync(path.join(out,'binding-wait-results.json'),JSON.stringify(bindingReport,null,2)+'\n');
    check(passed,'native binding wait restores or releases the actual mounted wallpaper: '+bindingWaitVariant);
    lifecycleResults.push(bindingReport);results.push({viewport,scenario,bindingWaitVariant,sockets:control.sockets,surfaces:control.images,timers:control.timers});await page.close();continue;
   }
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
    if(teardownVariant){
     await page.evaluate(fault=>{startup.teardownFault=fault;try{startup.dispose()}catch(error){startup.teardownError=String(error?.message??error);}startup.dispose();},teardownVariant==='router-failure');
     await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
     const probe=await page.evaluate(()=>({error:startup.teardownError,resources:startup.resources(),patches:startup.patches.size,globalComponents:startup.globalComponents.size,surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length,timers:startup.timers.size,wallpaperDemand:startup.client.passiveConsumers.has('wallpaper'),extraListeners:startup.client.extraListeners.size,connectionListeners:startup.client.connectionListeners.size,connected:startup.client.snapshot().connected}));
     fs.writeFileSync(path.join(out,'TEARDOWN-PROBE.json'),JSON.stringify({scenario,viewport,teardownVariant,before,probe,browserVersion:browser.version(),fixtureSha256:createHash('sha256').update(fs.readFileSync(new URL(import.meta.url))).digest('hex'),runtimeScripts:['react.js','react-dom.js'].map(name=>({name,sha256:createHash('sha256').update(fs.readFileSync(path.join(out,name))).digest('hex')})),scope:'Original late Home cleanup consumer with real React/Edge and simulated current native foreground receipt; image only, no live Steam or video decode acceptance'},null,2)+'\n',{flag:'wx'});
     check(teardownVariant==='router-failure'?probe.error==='inert wallpaper global removal failure':probe.error===undefined,'original router error is preserved only for the injected failure');
     check(probe.patches===0&&probe.surfaces===0&&!probe.wallpaperDemand&&probe.extraListeners===0&&probe.connectionListeners===0,'router failure must not strand local wallpaper owners');
     check(probe.resources.listeners===0&&probe.resources.historyListeners===0&&probe.resources.mutationObservers===0&&probe.resources.intersectionObservers===0,'router failure must not strand local wallpaper observations');
    }else await page.evaluate(()=>{startup.dispose();startup.dispose()});
    await page.waitForFunction(()=>!document.querySelector('[data-ymcc-wallpaper-route]'));
    const disposed=await page.evaluate(()=>({resources:startup.resources(),patches:startup.patches.size,globalComponents:startup.globalComponents.size,surfaces:document.querySelectorAll('[data-ymcc-wallpaper-surface]').length,timers:startup.timers.size,contexts:startup.socket.sent.filter(m=>m.command==='wallpaper.context').length}));
    check(disposed.patches===0&&disposed.surfaces===0&&disposed.globalComponents===(teardownVariant==='router-failure'?1:0),teardownVariant==='router-failure'?'faulted dispose removes local patches/portals while the failed Steam registry removal stays observable':'mounted dispose removes route patches, public globals and portals');
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
 check(results.length===(onlyScenario?1:scenarios.length)*viewports.length,'all requested viewport/scenario pairs executed');check(errors.length===0,'no browser exceptions');fs.writeFileSync(out+'/wallpaper-startup-results.json',JSON.stringify({ok:true,checks,results,lifecycleResults,errors,testedSources,scenarioSelection:onlyScenario||'all',bindingWaitVariant,viewports,bootstrapConsumers,hudOnlyVariant,teardownVariant,hudTeardownVariant,hudStartupVariant,realSiblingStartup,sourceOverrides:{wallpaper:wallpaperSourceOverride,entry:entrySourceOverride},fixtureSha256:createHash('sha256').update(fs.readFileSync(new URL(import.meta.url))).digest('hex'),runtimeScripts:['react.js','react-dom.js'].map(name=>({name,sha256:createHash('sha256').update(fs.readFileSync(path.join(out,name))).digest('hex')})),browserVersion:browser.version(),bundleSha256:createHash('sha256').update(fs.readFileSync(out+'/wallpaper-startup-bundle.js')).digest('hex'),scope:'Production wallpaper consumer and MirrorClient with React18/Edge; deterministic transport/Steam route fixtures. No live Steam mutation.'},null,2));console.log('WALLPAPER_STARTUP_BROWSER_OK '+checks+' checks / '+results.length+' cases');
}finally{await browser.close()}

