import crypto from 'node:crypto';import os from 'node:os';import fs from 'node:fs';import path from 'node:path';import {createRequire}from'node:module';import assert from'node:assert/strict';
const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2];if(!out)throw Error('evidence lane required');fs.mkdirSync(out,{recursive:true});
if(process.argv.includes('--library-process')||process.argv.includes('--library-idle')||process.argv.includes('--library-advisory')||process.argv.includes('--library-download-progress')||process.argv.includes('--library-editor-artwork')){await runLibraryProcess();process.exit(0);}
const {build}=createRequire(root+'/package.json')('esbuild'),{chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright');
for(const variant of ['before','after'])for(const [file,globalName]of[['steamWallpaper.tsx','YMCCWallpaper'],['wallpaperPlayers.ts','YMCCPlayers']])await build({entryPoints:[root+'/decky-plugin/src/'+file],bundle:true,platform:'browser',format:'iife',globalName,outfile:out+'/'+variant+'-'+file.split('.')[0]+'.js',plugins:variant==='before'?[{name:'exact-before-source',setup(api){api.onLoad({filter:/decky-plugin[\\/]src[\\/](steamWallpaper\.tsx|wallpaperPlayers\.ts)$/},args=>({contents:fs.readFileSync(path.join(out,'before',path.relative(root,args.path)),'utf8'),loader:args.path.endsWith('.tsx')?'tsx':'ts'}));}}]:[]});
const browser=await chromium.launch({channel:'msedge',headless:true}),rows=[],playerRows=[],libraryRows=[];let checks=0;const check=(value,label)=>{checks++;assert.ok(value,label)};
try{
 for(const variant of ['before','after','after','before','before','after']){
  const page=await browser.newPage({viewport:{width:1280,height:720}}),errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.route('http://127.0.0.1:13140/**',route=>new URL(route.request().url()).pathname.startsWith('/asset/')?route.fulfill({contentType:'image/png',body:Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==','base64')}):route.fulfill({contentType:'text/html',body:'<!doctype html><style>body{margin:0;background:#171d26}.native-home{position:absolute;inset:0;opacity:0}.native-images{position:absolute;inset:0}button{position:relative;z-index:4}</style><div id="root"></div>'}));
  await page.goto('http://127.0.0.1:13140/');for(const file of ['react.js','react-dom.js',variant+'-steamWallpaper.js'])await page.addScriptTag({path:out+'/'+file});
  await page.evaluate(()=>{
   const patches=new Map(),sent=[],demands=[];let extra=()=>{},connection=()=>{};const client={sendWallpaper(command,args){sent.push({command,...args})},setPassiveDemand(key,enabled){demands.push({key,enabled})},subscribeExtra(fn){extra=fn;return()=>extra=()=>{}},subscribe(fn){connection=fn;return()=>connection=()=>{}}};
   window.__YMCC_DECKY_MIRROR__={wallpaperProtocol:1,wallpaperPrefetchProtocol:1};let renders=0,updateNative=()=>{};
   const Original=()=>{renders++;const[n,set]=React.useState(0);updateNative=()=>set(x=>x+1);return React.createElement('main',{'data-native-count':n},React.createElement('div',{className:'native-home'},React.createElement('div',{className:'native-images'})),...Array.from({length:120},(_,i)=>React.createElement('button',{'data-appid':String(570+i),key:i},'Game '+i)));};
   const dispose=YMCCWallpaper.installSteamWallpaper({host:window,react:React,reactDom:ReactDOM,ui:{modules:new Map([['home',{RecentGamesBackgroundContainer:'native-home',RecentGamesBackgroundImages:'native-images'}]])},routerHook:{addPatch(p,fn){patches.set(p,fn);return fn},removePatch(p){patches.delete(p)}},client});
   const root=ReactDOM.createRoot(document.getElementById('root'));root.render(patches.get('/library/home')({children:React.createElement(Original)}).children);
   const emit=(n=0)=>{const context=[...sent].reverse().find(x=>x.command==='wallpaper.context');extra({type:'wallpaper',enabled:true,opacity:n%2?.65:.64,contextSeq:context.contextSeq,appId:context.appId,entry:{assetId:'fixture',revision:'1',bindingRevision:1,kind:'image',url:'http://127.0.0.1:13140/asset/fixture.png?token='+'a'.repeat(64)}})};
   window.probe={sent,demands,patches,get renders(){return renders},emit,dispose,extra:msg=>extra(msg),native:()=>updateNative(),root};extra({type:'wallpaper-ready',enabled:true});
  });
  await page.locator('[data-appid="570"]').focus();await page.waitForFunction(()=>probe.sent.some(x=>x.command==='wallpaper.context'));await page.evaluate(()=>ReactDOM.flushSync(()=>probe.emit(0)));await page.waitForFunction(()=>document.querySelector('[data-ymcc-wallpaper-surface] img'));
  // Warm both paths with the same real React rendering work before measuring.
  await page.evaluate(async()=>{for(let i=1;i<=12;i++){ReactDOM.flushSync(()=>probe.emit(i));await new Promise(requestAnimationFrame);}});
  const cdp=await page.context().newCDPSession(page);await cdp.send('Performance.enable');const metrics=async()=>Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(m=>[m.name,m.value]));const a=await metrics();
  const result=await page.evaluate(async()=>{const before=probe.renders,elapsed=[],observerEntries=[];const observer=new PerformanceObserver(list=>observerEntries.push(...list.getEntries().map(x=>x.duration)));observer.observe({type:'longtask'});const start=performance.now();for(let i=1;i<=120;i++){const at=performance.now();ReactDOM.flushSync(()=>probe.emit(i));elapsed.push(performance.now()-at);await new Promise(requestAnimationFrame);}observer.disconnect();return{originalRenders:probe.renders-before,wallMs:performance.now()-start,handlerMs:elapsed.reduce((a,b)=>a+b,0),maxHandlerMs:Math.max(...elapsed),longTasks:observerEntries};});
  const b=await metrics();result.scriptMs=(b.ScriptDuration-a.ScriptDuration)*1000;result.rendererTaskMs=(b.TaskDuration-a.TaskDuration)*1000;result.layoutMs=(b.LayoutDuration-a.LayoutDuration)*1000;result.variant=variant;
  check(result.originalRenders===(variant==='before'?120:0),'wallpaper messages do not rerender the native route after isolation');
  const previous=await page.evaluate(()=>probe.renders);await page.evaluate(()=>ReactDOM.flushSync(()=>probe.native()));check(await page.evaluate(()=>probe.renders)===previous+1,'native page retains its own state updates');
  check(await page.locator('[data-ymcc-wallpaper-surface] img').count()===1,'wallpaper output survives the render isolation');
  await page.evaluate(()=>ReactDOM.flushSync(()=>probe.dispose()));check(await page.locator('[data-ymcc-wallpaper-route]').count()===0,'teardown still restores the original Steam route');check(await page.locator('main').count()===1&&await page.evaluate(()=>probe.patches.size)===0,'dispose preserves native content and removes both patches');check(errors.length===0,'no browser errors during route stress');rows.push({...result,errors});await page.close();
 }
 for(const variant of ['before','after']){
  const page=await browser.newPage();await page.goto('about:blank');await page.addScriptTag({path:out+'/'+variant+'-wallpaperPlayers.js'});
  const result=await page.evaluate(async()=>{const real=document.createElement.bind(document),nodes=[];let calls=0,rejectPlay=null;document.createElement=(name,...args)=>{const node=real(name,...args);if(name==='video'){let position=0;Object.defineProperties(node,{src:{get:()=>'',set:()=>{}},duration:{get:()=>90},currentTime:{get:()=>position,set:v=>position=v},readyState:{get:()=>2},seeking:{get:()=>false},paused:{get:()=>true}});node.play=()=>{calls++;return new Promise((_,reject)=>{rejectPlay=reject})};node.pause=()=>{};node.load=()=>{};nodes.push(node);}return node;};const outer=document.createElement('div'),container=document.createElement('div');outer.append(container);document.body.append(outer);const frame={contextSeq:1,appId:'570',entry:{assetId:'fixture',revision:'1',bindingRevision:1,kind:'video',url:'http://127.0.0.1:13140/asset/fixture.mp4?token='+'a'.repeat(64)}};const pool=new YMCCPlayers.WallpaperPlayers(document,()=>{});pool.show(container,frame);const video=nodes[0];video.dispatchEvent(new Event('loadedmetadata'));video.dispatchEvent(new Event('loadeddata'));await Promise.resolve();const observer=new MutationObserver(()=>{});observer.observe(outer,{attributes:true,subtree:true});for(let i=0;i<600;i++)pool.show(container,frame);const mutations=observer.takeRecords().length;const playCalls=calls;observer.disconnect();rejectPlay(new Error('interrupted before ready'));for(let n=0;n<6;n++)await Promise.resolve();const retriedCalls=calls;pool.stop();rejectPlay(new Error('stopped before return'));for(let n=0;n<6;n++)await Promise.resolve();return{playCalls,retriedCalls,callsAfterStop:calls,mutations,liveNodes:container.children.length};});
  if(variant==='after'){check(result.playCalls===1,'an unresolved play promise is shared across repeated show calls');check(result.mutations===0,'unchanged player frames perform no style or ready-attribute writes');check(result.retriedCalls===2,'ready arriving during interrupted play retains one bounded retry');check(result.callsAfterStop===2,'late rejected play cannot resurrect a stopped player');}check(result.liveNodes===0,'player still releases on stop');playerRows.push({variant,...result});await page.close();
 }
 for(const variant of ['before','after']){
  const page=await browser.newPage();await page.addInitScript(()=>{const listeners=new Set();Object.defineProperty(window,'chrome',{value:{webview:{addEventListener(t,fn){listeners.add(fn)},postMessage(msg){if(msg.command==='uiError')return;queueMicrotask(()=>{const result=msg.command==='steamAccounts'?{accounts:[],steamStatus:'not-running'}:{config:{},manualOverrides:{items:{}},library:{games:[]},steamPlan:{items:[],summary:{}},mediaDownloads:{enabled:false,total:0,remaining:0},gameMedia:{entries:{}}};for(const fn of listeners)fn({data:{kind:'response',id:msg.id,ok:true,result}});});}}}});});
  await page.route('http://app.localhost/**',route=>{const name=path.basename(new URL(route.request().url()).pathname)||'index.html';if(!['index.html','styles.css','app.js'].includes(name))return route.fulfill({status:404,body:''});const folder=variant==='before'&&name==='app.js'?out+'/before/CustomSteamLibrary/workspace-ui':root+'/CustomSteamLibrary/workspace-ui';return route.fulfill({contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html',body:fs.readFileSync(folder+'/'+name)});});await page.goto('http://app.localhost/index.html');await page.waitForFunction(()=>typeof applyWallpaperPreviewRange==='function');
  const result=await page.evaluate(()=>{selectedActionGame={gameDirectory:'C:/Fixture',primaryExecutable:'C:/Fixture/game.exe',gameMedia:{playback:{start:8,end:38}}};const video={duration:90,currentTime:8,readyState:1};applyWallpaperPreviewRange(video);const hint=document.getElementById('shared-wallpaper-duration-hint'),trigger=workspaceDropdownForSelect(document.getElementById('shared-wallpaper-length')).trigger,observer=new MutationObserver(()=>{});observer.observe(hint,{childList:true,subtree:true});observer.observe(trigger,{attributes:true,attributeFilter:['title']});const start=performance.now();for(let n=0;n<600;n++)applyWallpaperPreviewRange(video);const ms=performance.now()-start,mutations=observer.takeRecords().length;observer.disconnect();video.currentTime=40;applyWallpaperPreviewRange(video);return{ms,mutations,loopedTo:video.currentTime,text:hint.textContent}});
  if(variant==='after')check(result.mutations===0,'unchanged editor timeupdates perform no hint/title DOM writes');check(result.loopedTo===8&&result.text.includes('30'),'editor range looping and duration hint remain correct');libraryRows.push({variant,...result});await page.close();
 }
 const median=values=>values.sort((a,b)=>a-b)[Math.floor(values.length/2)],summary=Object.fromEntries(['before','after'].map(v=>[v,{originalRendersPer120Messages:rows.find(x=>x.variant===v).originalRenders,medianScriptMs:median(rows.filter(x=>x.variant===v).map(x=>x.scriptMs)),medianRendererTaskMs:median(rows.filter(x=>x.variant===v).map(x=>x.rendererTaskMs)),medianHandlerMs:median(rows.filter(x=>x.variant===v).map(x=>x.handlerMs))}]));
 const result={ok:true,checks,rows,summary,playerRows,libraryRows,scope:'exact before snapshots and production code in real Edge/React with a 120-card native-page stand-in, 120 separate frame deliveries and actual browser CPU task metrics; unresolved media play is an explicit fixture; editor uses actual HTML/CSS/JS; not installed Steam CEF CPU acceptance'};fs.writeFileSync(out+'/performance-results.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,checks,summary,playerRows,libraryRows}));
}finally{await browser.close();}

// Explicit library-only mode: the default Decky/player assertions above remain intact.
// This measures owned Edge processes, with the same production HTML/CSS on both sides.
async function runLibraryProcess() {
  const args = process.argv.slice(3), idleMode = args.includes('--library-idle'), advisoryMode = args.includes('--library-advisory'), downloadMode = args.includes('--library-download-progress'), editorMode = args.includes('--library-editor-artwork'), allowed = new Set(['--library-process', '--library-idle', '--library-advisory', '--library-download-progress', '--library-editor-artwork', '--before-ui', '--before-sha256', '--before-css', '--before-css-sha256']);
  let beforePath = '', expectedHash = '', beforeCss = '', expectedCssHash = '';
  for (let i = 0; i < args.length; i++) {
    assert.ok(allowed.has(args[i]), `unknown library-process argument: ${args[i]}`);
    if (args[i] === '--before-ui') beforePath = args[++i];
    else if (args[i] === '--before-sha256') expectedHash = args[++i];
    else if (args[i] === '--before-css') beforeCss = args[++i];
    else if (args[i] === '--before-css-sha256') expectedCssHash = args[++i];
  }
  assert.ok(Number(idleMode)+Number(advisoryMode)+Number(downloadMode)+Number(editorMode)<=1,'select one library workload');
  assert.ok(beforePath && /^[a-f0-9]{64}$/i.test(expectedHash), 'explicit before source and SHA256 required');
  const evidenceRoot = path.resolve(root, '../../../Testing/YMCC/Evidence'), relativeOut = path.relative(evidenceRoot, path.resolve(out));
  assert.ok(relativeOut && !relativeOut.startsWith('..') && !path.isAbsolute(relativeOut), 'library-process output must be inside the shared evidence layer');
  assert.equal(fs.readdirSync(out).length, 0, 'use a fresh evidence directory; prior FAIL/results must remain');
  const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
  const ui = path.join(root, 'CustomSteamLibrary/workspace-ui');
  const sourcePaths = {before: path.resolve(beforePath), after: path.join(ui, 'app.js'), html: path.join(ui, 'index.html'), css: path.join(ui, 'styles.css'), consumer: import.meta.filename};
  if(idleMode){assert.ok(beforeCss && /^[a-f0-9]{64}$/i.test(expectedCssHash), 'idle comparison requires exact before CSS and SHA256');sourcePaths.beforeCss=path.resolve(beforeCss);}
  const bytes = Object.fromEntries(Object.entries(sourcePaths).map(([key, file]) => [key, fs.readFileSync(file)]));
  assert.equal(sha(bytes.before), expectedHash.toLowerCase(), 'before archive identity');
  if(idleMode)assert.equal(sha(bytes.beforeCss),expectedCssHash.toLowerCase(),'before CSS identity');
  const inputDir = path.join(out, 'inputs'); fs.mkdirSync(inputDir);
  for (const [key, content] of Object.entries(bytes)) fs.writeFileSync(path.join(inputDir, key + (['before', 'after', 'consumer'].includes(key) ? '.js' : key === 'html' ? '.html' : '.css')), content);
  const require = createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');
  const {chromium} = require('playwright'), edge = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  const manifest = {
    createdAt: new Date().toISOString(), sources: Object.fromEntries(Object.entries(sourcePaths).map(([key, file]) => [key, {file, sha256: sha(bytes[key])}])),
    node: {path: process.execPath, version: process.version, sha256: sha(fs.readFileSync(process.execPath))},
    edge: {path: edge, sha256: sha(fs.readFileSync(edge))},
    playwright: {version: require('playwright/package.json').version, sha256: sha(fs.readFileSync(require.resolve('playwright/package.json')))},
    logicalProcessors: os.availableParallelism(), budgetMs: Number(process.env.YMCC_LIBRARY_PROCESS_BUDGET_MS||180000),
    workload: {games: 80, cachedMedia: 80, renders: 12, untouchedSaves: 6, order: ['before', 'after', 'after', 'before', 'before', 'after']},
    scope: 'Owned headless Edge process CPU and renderer metrics; production library HTML/CSS/JS, memory native transport, static cached images. No video decoder, installed WebView/native worker, Steam write, energy or hardware acceptance.'
  };
  assert.ok(Number.isInteger(manifest.budgetMs)&&manifest.budgetMs>0&&manifest.budgetMs<=180000,'bounded original performance budget required');
  if(idleMode){manifest.workload={games:80,cachedMedia:80,foregroundMs:1200,backgroundMs:2500,busyBackgroundMs:1800,order:manifest.workload.order};manifest.scope='Owned headless Edge process CPU and production UI; actual CSS timelines with a test focus-policy seam while the browser page remains visible. Native IPC is memory transport; this is not installed WebView/Windows occlusion/power acceptance.';manifest.focusPolicySeam=true;manifest.reducedMotion='no-preference';}
  if(advisoryMode){manifest.workload={games:80,cachedMedia:80,eventsPerPhase:32,settleMs:700,order:manifest.workload.order};manifest.scope='Owned headless Edge CPU for production snapshot event handling and real library DOM.80 cached static cards,32 native-bridge event notifications per phase, then700ms settlement; explicit focus-policy seam and memory native state. No installed WebView/native worker/Steam write/energy acceptance.';manifest.focusPolicySeam=true;}
  if(downloadMode){manifest.workload={games:80,cachedMedia:80,eventsPerPhase:256,settleMs:700,indeterminateIdleMs:1800,order:manifest.workload.order};manifest.scope='Owned headless Edge CPU for actual production download-progress bridge handling:80 cached static wallpaper cards,256 progress events then700ms settlement per focus phase, plus1800ms inactive indeterminate-control idle. Explicit focus-policy seam on a visible page, memory native transport; no timed DOM observer. No installed WebView/native worker, actual download/video/Steam/energy acceptance.';manifest.focusPolicySeam=true;}
  if(editorMode){manifest.workload={games:80,cachedMedia:80,writesPerPhase:24,settleMs:400,order:manifest.workload.order};manifest.scope='Owned headless Edge CPU for24 sequential accepted playback write receipts per focus phase in the production editor,80 cached static wallpaper cards, then400ms settlement. Memory native revision/CAS transport, explicit visible-page focus seam, no timed diagnostic observer. No installed WebView/native worker, actual video decoder, Steam write or energy acceptance.';manifest.focusPolicySeam=true;}
  const fixture = {dataRoot: 'G:/ProcessFixture', config: {roots: [], manualBuckets: {}}, manualOverrides: {items: {}}, resolvedIdentities: {}, library: {games: [], summary: {}}, steamPlan: {items: [], summary: {}}, gameMedia: {entries: {}}, mediaDownloads: {enabled: false, total: 80, completed: 80, remaining: 0, phase: 'completed'}, steamAccounts: {accounts: [{accountId:'1000',steamRoot:'G:/ProcessFixture/Steam',accountName:'Owned CPU fixture'}], steamStatus: 'ready'}};
  for (let i = 0; i < 80; i++) {
    const directory = `G:/ProcessFixture/Game${String(i).padStart(3, '0')}`, executable = directory + '/game.exe', appId = 100000 + i, name = `Cached game ${String(i).padStart(3, '0')}`;
    const historical = executable.replaceAll('/', '\\').toUpperCase() + '\\';
    fixture.library.games.push({gameDirectory: directory, primaryExecutable: executable, directoryName: name, primaryProductName: name, status: 'ready'});
    fixture.steamPlan.items.push({gameDirectory: directory, primaryExecutable: executable, status: 'ready-to-add', appId, steamStoreAppId: appId, steamVerificationStatus: 'steam-verified', identityStatus: 'verified', suggestedName: name, artworkPreview: {tall: {url: '/asset.png', available: true}, long: {url: '/asset.png', available: true}, hero: {url: '/asset.png', available: true}}});
    if (i % 2 === 0) fixture.manualOverrides.items[historical] = {name};
    fixture.resolvedIdentities[historical] = {executable: historical, formalName: name};
    fixture.config.manualBuckets[directory.toUpperCase() + '/'] = {bucket: 'not-in-steam'};
    const asset = {id: `static-${appId}`, revision: '1', kind: 'image', extension: 'png', mime: 'image/png', bytes: 70};
    fixture.gameMedia.entries[`steam-${appId}`] = {key: `steam-${appId}`, executable, name, appId, bindingRevision: 1, status: 'ready', wallpaperMode: 'static', staticReady: true, dynamicReady: false, assets: {static: asset}, asset, staticPreviewUrl: '/asset.png', previewUrl: '/asset.png', playback: {start: 8, end: 38, fit: 'cover'}};
  }
  if(editorMode)fixture.__editorArtworkWorkload=true;
  const fixtureBytes = Buffer.from(JSON.stringify(fixture));
  fs.writeFileSync(path.join(inputDir, 'fixture.json'), fixtureBytes); manifest.fixtureSha256 = sha(fixtureBytes);
  fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(manifest, null, 2));
  const rows = [], checkNames = [], started = performance.now(); let activeBrowser, timer;
  const check = (value, label) => {assert.ok(value, label); checkNames.push(label);};
  const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==', 'base64');
  const median = values => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
  try {
    timer = setTimeout(() => {void activeBrowser?.close();}, manifest.budgetMs);
    for (const variant of manifest.workload.order) {
      assert.ok(performance.now() - started < manifest.budgetMs, 'common library-process budget exhausted');
      activeBrowser = await chromium.launch({executablePath: edge, headless: true});
      const browser = activeBrowser, page = await browser.newPage({viewport: {width: 1280, height: 800},reducedMotion:'no-preference'}), errors = [], network = [];
      page.setDefaultTimeout(8000); page.on('pageerror', error => errors.push(error.message));
      manifest.edge.version = browser.version();
      await page.addInitScript(seed => {
        const listeners = new Set(); window.__cpuFixture = structuredClone(seed); window.__cpuCommands = [];
        window.__cpuEmit=message=>{for(const listener of listeners)listener({data:message});};
        const canonical = value => String(value || '').replaceAll('/', '\\').replace(/[\\]+$/, '').toLowerCase();
        Object.defineProperty(window, 'chrome', {configurable: true, value: {webview: {
          addEventListener(type, listener) {listeners.add(listener);},
          postMessage(message) {
            __cpuCommands.push(structuredClone(message)); if (message.command === 'uiError') return;
            queueMicrotask(() => {
              try {
                const state = __cpuFixture, args = message.arguments || {}; let result;
                if (message.command === 'steamAccounts') result = state.steamAccounts;
                else if (message.command === 'steamCommitPreflight') result = {steamRunning:false};
                else if (message.command === 'mediaCreate') result = Object.values(state.gameMedia.entries).find(entry => canonical(entry.executable) === canonical(args.executable) || String(entry.appId) === String(args.appId));
                else if(message.command==='mediaEdit'&&state.__editorArtworkWorkload){
                  const entry=state.gameMedia.entries[args.key];if(!entry||args.bindingRevision!==entry.bindingRevision||args.action!=='playback')throw Error('invalid editor benchmark write or stale revision');
                  const next={...entry,bindingRevision:entry.bindingRevision+1,playback:{start:args.start,end:args.end,fit:args.fit}};state.gameMedia.entries[entry.key]=next;result=next;
                }
                else if (message.command === 'saveEditor') {
                  for (const operation of args.operations || []) {
                    const values = operation.arguments;
                    if (operation.command === 'saveManualName') state.manualOverrides.items[canonical(values.executable)] = {...state.manualOverrides.items[canonical(values.executable)], name: values.name};
                    else if (operation.command === 'setBucket') state.config.manualBuckets[canonical(values.gameDirectory)] = {bucket: values.bucket};
                    else throw Error('unexpected benchmark write: ' + operation.command);
                  }
                  result = state;
                } else if (['startup', 'openLibrary', 'state', 'mediaState'].includes(message.command)) result = state;
                else if (message.command === 'mediaDownloadStatus') result = state.mediaDownloads;
                else throw Error('unexpected benchmark command: ' + message.command);
                if (!result) throw Error('missing fixture result for ' + message.command);
                for (const listener of listeners) listener({data: {kind: 'response', id: message.id, ok: true, result: structuredClone(result)}});
              } catch (error) {
                for (const listener of listeners) listener({data: {kind: 'response', id: message.id, ok: false, error: error.message}});
              }
            });
          }
        }}});
      }, fixture);
      await page.route('**/*', route => {
        const url = new URL(route.request().url()); network.push(url.href);
        if (url.origin !== 'http://app.localhost') return route.abort();
        const name = path.basename(url.pathname) || 'index.html';
        if (name === 'asset.png') return route.fulfill({contentType: 'image/png', body: png});
        const key = { 'index.html': 'html', 'styles.css': idleMode&&variant==='before'?'beforeCss':'css', 'app.js': variant }[name];
        return key ? route.fulfill({contentType: name.endsWith('.js') ? 'text/javascript' : name.endsWith('.css') ? 'text/css' : 'text/html', body: bytes[key]}) : route.fulfill({status: 404, body: ''});
      });
      try {
        await page.goto('http://app.localhost/index.html');
        await page.waitForFunction(() => typeof renderLibrary === 'function' && snapshot?.library?.games?.length === 80);
        await page.evaluate(async () => {activeTab = 'not-in-steam'; selectedGameDirectories = new Set(mergedGames().filter((_, i) => i % 3 === 0).map(keyFor)); renderLibrary(); await document.fonts.ready; await new Promise(requestAnimationFrame); await new Promise(requestAnimationFrame); await new Promise(resolve => setTimeout(resolve, 700));});
        const browserCDP = await browser.newBrowserCDPSession(), rendererCDP = await page.context().newCDPSession(page);
        await rendererCDP.send('Performance.enable');
        const processes = async () => (await browserCDP.send('SystemInfo.getProcessInfo')).processInfo;
        const metrics = async () => Object.fromEntries((await rendererCDP.send('Performance.getMetrics')).metrics.map(metric => [metric.name, metric.value]));
        const measure = async (name, workload) => {
          const beforeProcesses = await processes(), a = await metrics(), t = performance.now();
          const work = await workload();
          const b = await metrics(), afterProcesses = await processes(), elapsedSec = (performance.now() - t) / 1000;
          const prior = new Map(beforeProcesses.map(row => [row.id, row])), current = new Map(afterProcesses.map(row => [row.id, row]));
          const cpuByType = {}, spawned = [], exited = [];
          for (const row of afterProcesses) {
            const old = prior.get(row.id);
            if (old && old.type === row.type) cpuByType[row.type] = (cpuByType[row.type] || 0) + Math.max(0, row.cpuTime - old.cpuTime);
            else spawned.push(row);
          }
          for (const row of beforeProcesses) if (!current.has(row.id)) exited.push(row);
          const cpuSeconds = Object.values(cpuByType).reduce((a, b) => a + b, 0);
          const result = {name, elapsedSec, cpuMs: cpuSeconds * 1000, cpuByType, oneCoreCpuPct: cpuSeconds / elapsedSec * 100, machineCpuPct: cpuSeconds / elapsedSec / manifest.logicalProcessors * 100, scriptMs: (b.ScriptDuration - a.ScriptDuration) * 1000, rendererTaskMs: (b.TaskDuration - a.TaskDuration) * 1000, layoutMs: (b.LayoutDuration - a.LayoutDuration) * 1000, heapBeforeBytes: a.JSHeapUsedSize, heapAfterBytes: b.JSHeapUsedSize, nodesBefore: a.Nodes, nodesAfter: b.Nodes, processSetStable: !spawned.length && !exited.length, spawned, exited, beforeProcesses, afterProcesses, work};
          fs.writeFileSync(path.join(out, `raw-${rows.length + 1}-${variant}-${name}.json`), JSON.stringify(result, null, 2));
          check(result.processSetStable, `${variant} ${name}: no unsampled spawned/exited process CPU`);
          return result;
        };

        if(editorMode){
          await page.evaluate(()=>{window.__cpuNativeFocus=document.hasFocus.bind(document);window.__cpuFocus=true;document.hasFocus=()=>__cpuFocus;window.dispatchEvent(new Event('focus'));openEdit(mergedGames()[0]);});
          await page.waitForFunction(()=>selectedActionGame?.gameMedia?.key&&pending.size===0&&!sharedWallpaperBusy);await page.waitForTimeout(200);
          const acceptedWrites=phase=>page.evaluate(async({phase,count,settleMs})=>{
            const owner=captureSharedWallpaperWrite(),key=owner.entry.key,revision=Number(owner.entry.bindingRevision),startCommands=__cpuCommands.length;
            const cover=document.querySelector('#artwork-cover-preview img'),wallpaper=document.querySelector('#artwork-wallpaper-preview img');let accepted=0;
            const start=performance.now();for(let i=0;i<count;i++){const position=(i+(phase==='background'?2:0))%8;if(await updateSharedWallpaper('playback',{start:position,end:position+30,fit:'cover'},owner))accepted++;}
            const handlerMs=performance.now()-start;await new Promise(resolve=>setTimeout(resolve,settleMs));const commands=__cpuCommands.slice(startCommands);
            return {accepted,expected:count,expectedStart:phase==='background'?1:7,handlerMs,commands:commands.map(x=>x.command),key,backendRevision:__cpuFixture.gameMedia.entries[key].bindingRevision,uiRevision:snapshot.gameMedia.entries[key].bindingRevision,editorRevision:selectedActionGame.gameMedia.bindingRevision,expectedRevision:revision+count,lastStart:selectedActionGame.gameMedia.playback.start,coverRetained:cover===document.querySelector('#artwork-cover-preview img'),wallpaperRetained:wallpaper===document.querySelector('#artwork-wallpaper-preview img'),cards:libraryCards().length,selected:selectedPageCount(),policyFocused:document.hasFocus(),nativeFocused:__cpuNativeFocus(),visibility:document.visibilityState,modalCount:document.querySelectorAll('.modal:not(.hidden)').length,queuedPlayback:sharedWallpaperPendingPlayback.size,busy:sharedWallpaperBusy};
          },{phase,count:manifest.workload.writesPerPhase,settleMs:manifest.workload.settleMs});
          const foregroundWrites=await measure('foreground-editor-artwork-receipts',()=>acceptedWrites('foreground'));
          await page.evaluate(()=>{__cpuFocus=false;window.dispatchEvent(new Event('blur'));});await page.waitForTimeout(120);
          const backgroundWrites=await measure('background-editor-artwork-receipts',()=>acceptedWrites('background'));
          await page.evaluate(()=>{__cpuFocus=true;window.dispatchEvent(new Event('focus'));});await page.waitForTimeout(120);
          const restored=await page.evaluate(()=>({start:Number(document.getElementById('shared-wallpaper-skip').value),expected:selectedActionGame.gameMedia.playback.start,revision:selectedActionGame.gameMedia.bindingRevision,key:selectedActionGame.gameMedia.key,policyFocused:document.hasFocus(),cover:!!document.querySelector('#artwork-cover-preview img')}));
          for(const phase of [foregroundWrites,backgroundWrites]){
            const row=phase.work;check(row.accepted===row.expected&&row.commands.length===row.expected&&row.commands.every(x=>x==='mediaEdit'),`${variant}: all fixed accepted editor writes execute without extra state/create requests`);
            check(row.backendRevision===row.expectedRevision&&row.uiRevision===row.expectedRevision&&row.editorRevision===row.expectedRevision&&row.lastStart===row.expectedStart,`${variant}: latest accepted editor model reaches backend/UI`);
            check(row.cards===80&&row.selected===27&&row.modalCount===1&&row.visibility==='visible'&&!row.busy&&row.queuedPlayback===0,`${variant}: fixed editor/card/focus fixture and no stranded writes`);
            if(variant==='after')check(row.coverRetained&&row.wallpaperRetained,'after: stable artwork resources survive playback receipts');
            else check(!row.coverRetained&&!row.wallpaperRetained,'before: repeated artwork resource reconstruction reproduced');
          }
          check(foregroundWrites.work.policyFocused&&!backgroundWrites.work.policyFocused&&restored.policyFocused&&restored.start===restored.expected&&restored.start===1&&restored.cover,`${variant}: accepted current editor range resumes on focus`);
          const sample={variant,foregroundWrites,backgroundWrites,restored,pageErrors:errors,nativeCommands:await page.evaluate(()=>__cpuCommands),network};rows.push(sample);fs.writeFileSync(path.join(out,`sample-${rows.length}-${variant}.json`),JSON.stringify(sample,null,2));
          check(errors.length===0&&!sample.nativeCommands.some(x=>x.command==='uiError')&&network.every(url=>url.startsWith('http://app.localhost/')),`${variant}: editor workload has only owned transport/assets`);
          console.log(JSON.stringify({sample:rows.length,variant,foregroundCpuMs:foregroundWrites.cpuMs,backgroundCpuMs:backgroundWrites.cpuMs,acceptedPerPhase:foregroundWrites.work.accepted}));continue;
        }
        if(downloadMode){
          check(await page.evaluate(()=>document.querySelectorAll('.modal:not(.hidden)').length===0&&!document.querySelector('#library-view').classList.contains('hidden')),`${variant}: unobstructed cached progress consumer`);
          await page.evaluate(()=>{
            window.__cpuNativeFocus=document.hasFocus.bind(document);window.__cpuFocus=true;document.hasFocus=()=>__cpuFocus;window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();
            const completed=Number(snapshot.mediaDownloads?.completed||0);window.__cpuDownloadRevision=Number(lastMediaDownloadDisplay?.statusRevision||0)+100;
            const state={enabled:true,total:completed+80,completed,remaining:80,phase:'downloading',activeCount:1,workerPid:12345,bytes:100,totalBytes:16777216,statusRevision:__cpuDownloadRevision,current:{name:'Initial download'},manualUntil:0};__cpuFixture.mediaDownloads=state;renderMediaDownloads(state);
          });await page.waitForTimeout(300);
          const eventWork=phase=>page.evaluate(async({phase,count,settleMs})=>{
            const startCommands=__cpuCommands.length,start=performance.now();
            for(let i=0;i<count;i++){const state={...snapshot.mediaDownloads,statusRevision:++__cpuDownloadRevision,bytes:5120+i*512,current:{name:'Download '+phase+' '+i}};__cpuFixture.mediaDownloads=state;__cpuEmit({kind:'event',event:'media-download-progress',mediaDownloads:state});}
            const handlerMs=performance.now()-start;await new Promise(resolve=>setTimeout(resolve,settleMs));const commands=__cpuCommands.slice(startCommands),bar=document.getElementById('media-download-bar');
            return {events:count,handlerMs,readRequests:commands.filter(x=>x.command==='state').length,commands:commands.map(x=>x.command),cards:libraryCards().length,selected:selectedPageCount(),current:snapshot.mediaDownloads.current.name,completed:snapshot.mediaDownloads.completed,revision:snapshot.mediaDownloads.statusRevision,label:document.getElementById('media-download-label').textContent,value:bar.value,hasValue:bar.hasAttribute('value'),policyFocused:document.hasFocus(),nativeFocused:__cpuNativeFocus(),visibility:document.visibilityState,modalCount:document.querySelectorAll('.modal:not(.hidden)').length,recoveryTimer:mediaRecoveryTimer};
          },{phase,count:manifest.workload.eventsPerPhase,settleMs:manifest.workload.settleMs});
          const foregroundEvents=await measure('foreground-download-progress',()=>eventWork('foreground'));
          await page.evaluate(()=>{__cpuFocus=false;window.dispatchEvent(new Event('blur'));});await page.waitForTimeout(120);
          const backgroundEvents=await measure('background-download-progress',()=>eventWork('background'));
          await page.evaluate(()=>{const state={...snapshot.mediaDownloads,statusRevision:++__cpuDownloadRevision,totalBytes:0,bytes:0,phase:'identifying',current:{name:'Indeterminate background'}};__cpuFixture.mediaDownloads=state;renderMediaDownloads(state);});await page.waitForTimeout(100);
          const indeterminateBackground=await measure('download-indeterminate-background-idle',()=>page.evaluate(async ms=>{
            const start=__cpuCommands.length;await new Promise(resolve=>setTimeout(resolve,ms));const bar=document.getElementById('media-download-bar');return {waitedMs:ms,phase:snapshot.mediaDownloads.phase,hasValue:bar.hasAttribute('value'),policyFocused:document.hasFocus(),visibility:document.visibilityState,recoveryTimer:mediaRecoveryTimer,commands:__cpuCommands.slice(start).map(x=>x.command)};
          },manifest.workload.indeterminateIdleMs));
          await page.evaluate(()=>{__cpuFocus=true;window.dispatchEvent(new Event('focus'));});await page.waitForTimeout(120);
          const restored=await page.evaluate(()=>({hasValue:document.getElementById('media-download-bar').hasAttribute('value'),phase:snapshot.mediaDownloads.phase,label:document.getElementById('media-download-label').textContent,current:snapshot.mediaDownloads.current.name}));
          for(const phase of [foregroundEvents,backgroundEvents]){
            check(phase.work.events===manifest.workload.eventsPerPhase&&phase.work.cards===80&&phase.work.selected===27,`${variant}: fixed progress event count and cached cards`);
            check(phase.work.visibility==='visible'&&phase.work.modalCount===0,`${variant}: visible unobstructed focus-policy seam`);
            check(phase.work.readRequests===0&&phase.work.commands.length===0,`${variant}: progress-only data does not request current library state`);
          }
          check(foregroundEvents.work.policyFocused&&!backgroundEvents.work.policyFocused&&foregroundEvents.work.label.includes('Download foreground 255')&&foregroundEvents.work.current==='Download foreground 255'&&backgroundEvents.work.current==='Download background 255',`${variant}: current progress data preserved across focus`);
          check(indeterminateBackground.work.commands.length===0&&indeterminateBackground.work.recoveryTimer===0&&!indeterminateBackground.work.policyFocused&&indeterminateBackground.work.visibility==='visible',`${variant}: no inactive recovery polling`);
          if(variant==='after')check(!backgroundEvents.work.label.includes('Download background')&&indeterminateBackground.work.hasValue,'after: inactive progress remains data-only with suspended indeterminate control');
          else check(backgroundEvents.work.label.includes('Download background 255')&&!indeterminateBackground.work.hasValue,'before: repeated inactive status/indeterminate work reproduced');
          check(!restored.hasValue&&restored.phase==='identifying'&&restored.label.includes('Indeterminate background')&&restored.current==='Indeterminate background',`${variant}: current indeterminate progress resumes without changing native data`);
          check(errors.length===0,`${variant}: no page errors`);
          const sample={variant,foregroundEvents,backgroundEvents,indeterminateBackground,restored,pageErrors:errors,nativeCommands:await page.evaluate(()=>__cpuCommands),network};rows.push(sample);fs.writeFileSync(path.join(out,`sample-${rows.length}-${variant}.json`),JSON.stringify(sample,null,2));
          check(!sample.nativeCommands.some(x=>x.command==='uiError')&&network.every(url=>url.startsWith('http://app.localhost/')),`${variant}: owned progress transport only`);
          console.log(JSON.stringify({sample:rows.length,variant,foregroundCpuMs:foregroundEvents.cpuMs,backgroundCpuMs:backgroundEvents.cpuMs,indeterminateCpuMs:indeterminateBackground.cpuMs}));continue;
        }
        if(advisoryMode){
          check(await page.evaluate(()=>document.querySelectorAll('.modal:not(.hidden)').length===0&&!document.querySelector('#library-view').classList.contains('hidden')),`${variant}: unobstructed cached event consumer`);
          await page.evaluate(()=>{
            window.__cpuNativeFocus=document.hasFocus.bind(document);window.__cpuFocus=true;document.hasFocus=()=>__cpuFocus;window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();
            const render=renderLibrary;window.__cpuPaints=0;renderLibrary=(...args)=>{__cpuPaints++;return render(...args);};
          });await page.waitForTimeout(300);
          const eventWork=(phase)=>page.evaluate(async({phase,count,settleMs})=>{
            const stale=structuredClone(snapshot),startCommands=__cpuCommands.length,startPaints=__cpuPaints,game=__cpuFixture.library.games[0];
            const key=Object.keys(__cpuFixture.manualOverrides.items).find(value=>normalizedUiPath(value)===normalizedUiPath(game.primaryExecutable))||normalizedUiPath(game.primaryExecutable);
            __cpuFixture.manualOverrides.items[key]={...__cpuFixture.manualOverrides.items[key],name:'Fresh '+phase};
            const start=performance.now();for(let i=0;i<count;i++)__cpuEmit({event:i%2?'network-plan-refreshed':'library-plan-refreshed',snapshot:structuredClone(stale)});const handlerMs=performance.now()-start;
            await new Promise(resolve=>setTimeout(resolve,settleMs));const commands=__cpuCommands.slice(startCommands);
            return {events:count,handlerMs,readRequests:commands.filter(message=>message.command==='state').length,paints:__cpuPaints-startPaints,commands:commands.map(message=>message.command),cards:libraryCards().length,selected:selectedPageCount(),name:mergedGames().find(row=>normalizedUiPath(row.primaryExecutable)===normalizedUiPath(game.primaryExecutable))?.override?.name,policyFocused:document.hasFocus(),nativeFocused:__cpuNativeFocus(),visibility:document.visibilityState,modalCount:document.querySelectorAll('.modal:not(.hidden)').length,pending:silentRefreshPending,inFlight:Boolean(silentRefreshInFlight)};
          },{phase,count:manifest.workload.eventsPerPhase,settleMs:manifest.workload.settleMs});
          const foregroundEvents=await measure('foreground-advisory-events',()=>eventWork('foreground'));
          await page.evaluate(()=>{__cpuFocus=false;window.dispatchEvent(new Event('blur'));});await page.waitForTimeout(120);
          const backgroundEvents=await measure('background-advisory-events',()=>eventWork('background'));
          const restoreStart=await page.evaluate(()=>({commands:__cpuCommands.length,paints:__cpuPaints}));await page.evaluate(()=>{__cpuFocus=true;window.dispatchEvent(new Event('focus'));});await page.waitForTimeout(700);
          const restored=await page.evaluate(start=>({reads:__cpuCommands.slice(start.commands).filter(message=>message.command==='state').length,paints:__cpuPaints-start.paints,name:mergedGames().find(game=>normalizedUiPath(game.primaryExecutable)===normalizedUiPath(__cpuFixture.library.games[0].primaryExecutable))?.override?.name,pending:silentRefreshPending,inFlight:Boolean(silentRefreshInFlight)}),restoreStart);
          for(const phase of [foregroundEvents,backgroundEvents]){
            check(phase.work.events===manifest.workload.eventsPerPhase&&phase.work.cards===80&&phase.work.selected===27,`${variant}: event count cached cards and selection preserved`);
            check(phase.work.visibility==='visible'&&phase.work.modalCount===0,`${variant}: event sample uses explicit focus seam on visible unobstructed page`);
          }
          check(foregroundEvents.work.policyFocused&&!backgroundEvents.work.policyFocused,`${variant}: event workload crosses intended focus policy`);
          check(backgroundEvents.work.readRequests===0,`${variant}: no background state IPC`);
          if(variant==='after'){
            check(foregroundEvents.work.paints===1&&foregroundEvents.work.readRequests===1&&foregroundEvents.work.name==='Fresh foreground','after: foreground burst paints current state once');
            check(backgroundEvents.work.paints===0&&backgroundEvents.work.pending&&!backgroundEvents.work.inFlight,'after: background burst leaves one dirty state without paint/read');
            check(restored.reads===1&&restored.paints===1&&restored.name==='Fresh background'&&!restored.pending&&!restored.inFlight,'after: focus publishes the current deferred update once');
          }else{
            check(foregroundEvents.work.paints===manifest.workload.eventsPerPhase&&foregroundEvents.work.readRequests===0,'before: every foreground payload repaints without current read');
            check(backgroundEvents.work.paints===manifest.workload.eventsPerPhase,'before: inactive payloads still repaint');
          }
          const sample={variant,foregroundEvents,backgroundEvents,restored,pageErrors:errors,nativeCommands:await page.evaluate(()=>__cpuCommands),network};rows.push(sample);fs.writeFileSync(path.join(out,`sample-${rows.length}-${variant}.json`),JSON.stringify(sample,null,2));
          check(errors.length===0&&!sample.nativeCommands.some(message=>message.command==='uiError'),`${variant}: no event page/transport errors`);
          check(network.every(url=>url.startsWith('http://app.localhost/')),`${variant}: event workload remains on owned fixture requests`);
          console.log(JSON.stringify({sample:rows.length,variant,foregroundCpuMs:foregroundEvents.cpuMs,backgroundCpuMs:backgroundEvents.cpuMs,foregroundPaints:foregroundEvents.work.paints,backgroundPaints:backgroundEvents.work.paints}));
          continue;
        }
        if(idleMode){
          check(await page.evaluate(()=>document.querySelectorAll('.modal:not(.hidden)').length===0&&!document.querySelector('#library-view').classList.contains('hidden')),`${variant}: unobstructed normal cached library, no fixture prompt`);
          await page.evaluate(()=>{window.__cpuNativeFocus=document.hasFocus.bind(document);window.__cpuFocus=true;document.hasFocus=()=>window.__cpuFocus;window.dispatchEvent(new Event('focus'));libraryCards()[0].focus();});
          const settle=()=>page.waitForTimeout(120);
          const idleWork=ms=>page.evaluate(async duration=>{
            const observe=()=>({
              commands:__cpuCommands.length,commandNames:__cpuCommands.map(message=>message.command),
              policyFocused:document.hasFocus(),nativeFocused:__cpuNativeFocus(),visibility:document.visibilityState,
              currentCard:document.querySelector('.game-card.current-card')?.dataset.directory||'',modalCount:document.querySelectorAll('.modal:not(.hidden)').length,
              timers:{steam:steamRuntimeWatchTimer,media:mediaRecoveryTimer,cards:cardOperationFrame},
              animations:document.getAnimations().map(animation=>{const target=animation.effect?.target;return {key:(target?.closest?.('.game-card')?.dataset.directory||'')+':'+String(target?.className||'')+':'+animation.animationName,name:animation.animationName,state:animation.playState,time:Number(animation.currentTime)};})
            });
            const before=observe();await new Promise(resolve=>setTimeout(resolve,duration));const after=observe();
            const prior=new Map(before.animations.map(row=>[row.key,row]));
            const advancing=after.animations.filter(row=>prior.has(row.key)&&row.time-prior.get(row.key).time>1).map(row=>({name:row.name,key:row.key,deltaMs:row.time-prior.get(row.key).time}));
            return {duration,before,after,advancing,nativeCalls:after.commands-before.commands,cards:libraryCards().length,selected:selectedPageCount()};
          },ms);
          await settle();const foreground=await measure('cached-focused-idle',()=>idleWork(manifest.workload.foregroundMs));
          await page.evaluate(()=>{__cpuFocus=false;window.dispatchEvent(new Event('blur'));});await settle();
          const background=await measure('cached-unfocused-idle',()=>idleWork(manifest.workload.backgroundMs));
          await page.evaluate(()=>{__cpuFocus=true;window.dispatchEvent(new Event('focus'));for(const game of mergedGames().slice(0,4))steamCommitTasks.set(keyFor(game),{phase:'running',label:'正在加入 Steam'});refreshCardOperationState();});await settle();
          const activeQueue=await page.evaluate(()=>document.getAnimations().filter(animation=>animation.animationName==='spin'&&animation.playState==='running').length);
          await page.evaluate(()=>{__cpuFocus=false;window.dispatchEvent(new Event('blur'));});await settle();
          const busyBackground=await measure('queued-cached-unfocused-idle',()=>idleWork(manifest.workload.busyBackgroundMs));
          await page.evaluate(()=>{__cpuFocus=true;window.dispatchEvent(new Event('focus'));});await settle();
          const restored=await page.evaluate(async()=>{const animations=document.getAnimations(),before=animations.map(animation=>Number(animation.currentTime));await new Promise(resolve=>setTimeout(resolve,80));return {running:animations.filter(animation=>animation.playState==='running').length,advancing:animations.filter((animation,i)=>Number(animation.currentTime)-before[i]>1).length,policyFocused:document.hasFocus(),filterAnimated:animations.find(animation=>animation.animationName==='selected-artwork-breathe')?.effect.getKeyframes().some(frame=>Object.hasOwn(frame,'filter'))};});
          const sample={variant,foreground,background,busyBackground,activeQueue,restored,pageErrors:errors,nativeCommands:await page.evaluate(()=>__cpuCommands),network};
          rows.push(sample);fs.writeFileSync(path.join(out,`sample-${rows.length}-${variant}.json`),JSON.stringify(sample,null,2));
          check(foreground.work.cards===80&&foreground.work.selected===27&&foreground.work.advancing.length>0,`${variant}: real cached foreground animation and selection`);
          check(activeQueue===4&&restored.policyFocused&&restored.advancing>=5,`${variant}: queued spinners and glow resume on focus`);
          if(variant==='after')check(restored.filterAnimated===false,'after: foreground glow retains motion without animated filter');
          for(const [label,phase] of [['background',background],['busyBackground',busyBackground]]){
            check(!phase.work.before.policyFocused&&phase.work.before.visibility==='visible'&&phase.work.after.visibility==='visible'&&phase.work.before.modalCount===0&&phase.work.after.modalCount===0,`${variant} ${label}: explicit focus-policy seam, visible browser`);
            check(phase.work.nativeCalls===0&&Object.values(phase.work.after.timers).every(value=>value===0),`${variant} ${label}: no background polling or status native requests`);
            if(variant==='after')check(phase.work.advancing.length===0&&phase.work.after.animations.every(animation=>animation.state==='paused'),`after ${label}: CSS animation timelines must stop while unfocused`);
            else check(phase.work.advancing.length>0,`before ${label}: prior inactive-animation cost reproduced`);
          }
          check(errors.length===0&&!sample.nativeCommands.some(message=>message.command==='uiError'),`${variant}: no idle page/transport errors`);
          check(network.every(url=>url.startsWith('http://app.localhost/')),`${variant}: idle requests remain owned fixture requests`);
          if(rows.filter(row=>row.variant===variant).length===1){await page.evaluate(()=>{for(const animation of document.getAnimations()){animation.pause();animation.currentTime=1600;}});await page.screenshot({path:path.join(out,variant+'-library-fixed-animation-phase.png')});}
          console.log(JSON.stringify({sample:rows.length,variant,backgroundCpuMs:background.cpuMs,busyBackgroundCpuMs:busyBackground.cpuMs,backgroundAdvancing:background.work.advancing.length,busyAdvancing:busyBackground.work.advancing.length}));
          continue;
        }
        const render = await measure('twelve-cached-library-renders', () => page.evaluate(async () => {
          const t = performance.now();
          for (let i = 0; i < 12; i++) {renderLibrary(); await new Promise(requestAnimationFrame);}
          const games = mergedGames();
          return {wallMs: performance.now() - t, cards: document.querySelectorAll('.game-card').length, games: games.length, selected: selectedPageCount(), ownershipCorrect: games.every(game => !game.mediaOnly && normalizedUiPath(game.gameMedia?.executable) === normalizedUiPath(game.primaryExecutable)), commands: __cpuCommands.map(message => message.command)};
        }));
        check(render.work.cards === 80 && render.work.games === 80 && render.work.selected === 27 && render.work.ownershipCorrect, `${variant}: cached cards, selection and EXE media ownership`);
        const saves = await measure('six-untouched-editor-saves', () => page.evaluate(async () => {
          const calls = [], t = performance.now();
          for (let i = 0; i < 6; i++) {
            editDraft = null; const game = mergedGames()[i]; openEdit(game);
            await new Promise(resolve => setTimeout(resolve, 0));
            const begin = __cpuCommands.length, saved = await persistEditDraft();
            calls.push({key: keyFor(game), saved, commands: __cpuCommands.slice(begin).map(message => ({command: message.command, arguments: message.arguments}))});
            closeEdit(); await new Promise(requestAnimationFrame);
          }
          return {wallMs: performance.now() - t, calls, saveRequests: calls.reduce((sum, row) => sum + row.commands.filter(message => message.command === 'saveEditor').length, 0), allSaved: calls.every(row => row.saved), remainingPending: pending.size};
        }));
        check(saves.work.allSaved && saves.work.remainingPending === 0, `${variant}: all accepted saves settle`);
        if (variant === 'after') check(saves.work.saveRequests === 0, 'after: unchanged editor saves use zero saveEditor requests');
        else check(saves.work.saveRequests === 6, 'before: original unchanged-save request cost reproduced');
        const sample = {variant, render, saves, pageErrors: errors, nativeCommands: await page.evaluate(() => __cpuCommands), network};
        rows.push(sample); fs.writeFileSync(path.join(out, `sample-${rows.length}-${variant}.json`), JSON.stringify(sample, null, 2));
        check(errors.length === 0 && !sample.nativeCommands.some(message => message.command === 'uiError'), `${variant}: no page/native transport errors`);
        check(network.every(url => url.startsWith('http://app.localhost/')), `${variant}: all requests remain owned fixture requests`);
        console.log(JSON.stringify({sample: rows.length, variant, renderCpuMs: render.cpuMs, renderTaskMs: render.rendererTaskMs, savesCpuMs: saves.cpuMs, saveRequests: saves.work.saveRequests}));
      } catch (error) {
        fs.writeFileSync(path.join(out, `failed-sample-${rows.length + 1}-${variant}.json`), JSON.stringify({error: String(error.stack || error), pageErrors: errors, network, browserState: await page.evaluate(() => ({commands: window.__cpuCommands, snapshotGames: snapshot?.library?.games?.length})).catch(() => null)}, null, 2));
        throw error;
      } finally {await browser.close(); activeBrowser = null;}
    }
    for (const [key, file] of Object.entries(sourcePaths)) check(sha(fs.readFileSync(file)) === sha(bytes[key]), `${key}: input did not drift during measurement`);
    const summary = Object.fromEntries(['before', 'after'].map(variant => [variant, Object.fromEntries((editorMode?['foregroundWrites','backgroundWrites']:downloadMode?['foregroundEvents','backgroundEvents','indeterminateBackground']:advisoryMode?['foregroundEvents','backgroundEvents']:idleMode?['foreground','background','busyBackground']:['render','saves']).map(workload => {
      const samples = rows.filter(row => row.variant === variant).map(row => row[workload]);
      return [workload, {samples: samples.length, medianCpuMs: median(samples.map(row => row.cpuMs)), medianRendererCpuMs: median(samples.map(row => (row.cpuByType.renderer || 0) * 1000)), medianScriptMs: median(samples.map(row => row.scriptMs)), medianRendererTaskMs: median(samples.map(row => row.rendererTaskMs)), medianWallMs: median(samples.map(row => row.elapsedSec * 1000)), medianHeapAfterBytes: median(samples.map(row => row.heapAfterBytes)), medianNodesAfter: median(samples.map(row => row.nodesAfter)), saveRequests: workload === 'saves' ? samples.map(row => row.work.saveRequests) : undefined}];
    }))]));
    const result = {ok: true, checks: checkNames.length, checkNames, elapsedMs: performance.now() - started, summary, scope: manifest.scope, rows};
    fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify(manifest, null, 2));
    fs.writeFileSync(path.join(out, 'library-process-results.json'), JSON.stringify(result, null, 2));
    console.log(JSON.stringify({ok: true, checks: result.checks, elapsedMs: result.elapsedMs, summary}));
  } catch (error) {
    fs.writeFileSync(path.join(out, 'library-process-results.json'), JSON.stringify({ok: false, error: String(error.stack || error), checks: checkNames, rows, elapsedMs: performance.now() - started, scope: manifest.scope}, null, 2));
    throw error;
  } finally {clearTimeout(timer); await activeBrowser?.close();}
}
