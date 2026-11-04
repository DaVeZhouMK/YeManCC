// Real Chromium DOM running the exact embedded observer. Official v3.3.0 error-page structure;
// no installed Steam, Loader, hardware, or product settings are used.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {verifyHealthHeader} from './decky_sidebar_health_embed.mjs';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(process.env.YMCC_UI_TEST_REQUIRE_ANCHOR||path.join(process.env.USERPROFILE||'','/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_continuous_ui.cjs')),{chromium}=req('playwright');
const source=fs.readFileSync(path.join(root,'native/decky_sidebar_health.js'),'utf8');verifyHealthHeader();
const out=path.resolve(process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-health'),cases=[],measurements=[];fs.mkdirSync(out,{recursive:true});
const requested=[],argv=process.argv.slice(2);
for(let i=0;i<argv.length;i+=2){assert.equal(argv[i],'--case','Expected exact original health case selector');assert.ok(argv[i+1]&&!argv[i+1].startsWith('--'));assert(!requested.includes(argv[i+1]),'Duplicate health case');requested.push(argv[i+1]);}
const watchSource=fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap_watch.h'),'utf8');
const expressionLine=/const std::string teardownExpression=([^\r\n]+);/.exec(watchSource)?.[1];assert.ok(expressionLine,'Original teardown expression boundary missing');
// Decode only actual C++ string literals + the two JSON-quoted native slots.
// This is the production expression, not a reimplementation of its conditions.
function teardown(runId,fatal){let rest=expressionLine.trim(),term=true,result='';while(rest){rest=rest.trimStart();if(!term){assert.equal(rest[0],'+','Unknown C++ expression operation');rest=rest.slice(1);term=true;continue;}const literal=/^"(?:\\.|[^"\\])*"/.exec(rest);if(literal){result+=JSON.parse(literal[0]);rest=rest.slice(literal[0].length);}else{const slot=/^Json\((runId|fatal)\)\.dump\(\)/.exec(rest);assert.ok(slot,'Unknown C++ expression operand');result+=JSON.stringify(slot[1]==='runId'?runId:fatal);rest=rest.slice(slot[0].length);}term=false;}assert.equal(term,false);return result;}
const actualCapture=process.env.YMCC_HEALTH_NATIVE_CAPTURE?JSON.parse(fs.readFileSync(process.env.YMCC_HEALTH_NATIVE_CAPTURE,'utf8')):null;
if(actualCapture){assert.ok(Array.isArray(actualCapture)&&actualCapture.every(x=>typeof x==='string'));for(const fatal of ['decky-frontend-incompatible','decky-render-failed'])assert.ok(actualCapture.includes(teardown('b'.repeat(32),fatal)),'Decoded source must equal actual C++ expression sent over the real fixture WebSocket');}

const browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage();await page.route('https://steamloopback.host/**',r=>r.fulfill({contentType:'text/html',body:'<!doctype html><html><body></body></html>'}));await page.goto('https://steamloopback.host/health-fixture');
 const run='b'.repeat(32),loaderVersion=JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/vendor/decky-loader.lock.json'),'utf8')).version;
 const setup=async(options={})=>page.evaluate(({source,run,options,loaderVersion})=>{
   window.__YMCC_DECKY_HEALTH__?.dispose();document.body.replaceChildren();window.signals=[];Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:run},configurable:true,writable:true});window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
   const el=document.createElement('div');el.className='deckyErrorBoundary';if(options.hidden)el.hidden=true;
   el.innerHTML='<div>An error occurred while rendering this content.</div><pre><code></code></pre><div class="likely-occured-msg"></div><div class="panel-section"><div class="panel-header">Trace</div><pre><code></code></pre></div>';
   el.querySelector('pre code').textContent=`Error Reference: Shared SteamUI_123\nDecky Version: ${options.version||loaderVersion}${options.unsupported?'[U]':''}`;
   el.querySelector('.likely-occured-msg').textContent=options.attribution||'This error likely occurred in the Decky frontend.';
   el.querySelector('.panel-section code').textContent=options.trace||'TypeError: Cannot read properties of undefined\n at http://127.0.0.1:1337/frontend/index.js:1:100\n\nComponent Stack:\n at Example';
   document.body.append(el);(0,eval)('('+source+')')({runId:options.badRun?'invalid':run,loaderVersion});
   return {read:window.__YMCC_DECKY_HEALTH__?.read()||'',signals:window.signals};
 },{source,run,options,loaderVersion});
 const registered=[];const check=async(name,fn)=>{registered.push({name,fn});};
 await check('actual official layout without unused trace class detects visible core fatal',async()=>{const v=await setup();assert.equal(v.read,'decky-render-failed');assert.deepEqual(v.signals,[{runId:run,code:'decky-render-failed'}]);const exactLimit='TypeError: Cannot read properties of undefined\nat http://127.0.0.1:1337/frontend/index.js'.padEnd(8192,' ');assert.equal((await setup({trace:exactLimit})).signals.length,1,'complete8192-char core stack remains actionable');});
 await check('Windows unsupported suffix has no whitespace before next sibling',async()=>assert.equal((await setup({unsupported:true})).signals.length,1));
 for(const [name,options] of [['hidden boundary',{hidden:true}],['stale Loader version',{version:'v3.2.10'}],['Steam-only attribution',{attribution:'This error likely occurred in Steam.'}],['single plugin attribution',{attribution:'This error likely occurred in ExamplePlugin.'}],['plugin HTTP frame',{trace:'TypeError: Cannot read properties of undefined\nat http://127.0.0.1:1337/plugins/example/dist/index.js'}],['plugin sourcemap',{trace:'Minified React error #31\nat decky://decky/plugin/example/index.js'}],['legacy plugin sourcemap',{trace:'ReferenceError: missing\nat decky://decky/legacy_plugin/example/index.js'}],['network failure',{trace:'Error: Failed to fetch; HTTP 429'}],['network TypeError',{trace:'TypeError: Failed to fetch\nat http://127.0.0.1:1337/frontend/index.js'}],['transient resource TypeError',{trace:'TypeError: Load failed\nat http://127.0.0.1:1337/frontend/index.js'}],['unknown run',{badRun:true}]])await check(name+' cannot disable Decky',async()=>{
  assert.equal((await setup(options)).signals.length,0);
  if(name==='plugin HTTP frame'){
   const prefix='TypeError: Cannot read properties of undefined\nat http://127.0.0.1:1337/frontend/index.js';
   const latePlugin=prefix.padEnd(8192,' ')+'\nat http://127.0.0.1:1337/plugins/example/dist/index.js';
   assert.equal((await setup({trace:latePlugin})).signals.length,0,'truncated plugin trace must remain unconfirmed');
   assert.equal((await setup({trace:prefix.padEnd(8193,' ')})).signals.length,0,'oversized core-only trace remains unconfirmed');
  }
 });
 await check('text-node version update triggers observer, without polling',async()=>{await setup({version:'v3.2.10'});await page.evaluate(v=>{document.querySelector('pre code').firstChild.data='Error Reference: Shared SteamUI_123\nDecky Version: '+v+'[U]';},loaderVersion);await page.waitForFunction(()=>window.signals.length===1);});
 await check('revealed actual boundary is scanned on its hidden attribute change',async()=>{await setup({hidden:true});await page.evaluate(()=>document.querySelector('.deckyErrorBoundary').hidden=false);await page.waitForFunction(()=>window.signals.length===1);});
 await check('retained invisible ancestor cannot close Decky until revealed',async()=>{await setup({hidden:true});await page.evaluate(()=>{const parent=document.createElement('section');parent.style.visibility='hidden';const boundary=document.querySelector('.deckyErrorBoundary');boundary.before(parent);parent.append(boundary);boundary.hidden=false;});assert.equal(await page.evaluate(()=>window.signals.length),0);await page.evaluate(()=>document.querySelector('section').style.visibility='visible');await page.waitForFunction(()=>window.signals.length===1);});
 await check('same run only reports once across repeated mutations and reattachment',async()=>{await setup();await page.evaluate(({source,run,loaderVersion})=>{document.querySelector('.panel-section code').textContent+='\nTypeError: more';(0,eval)('('+source+')')({runId:run,loaderVersion});},{source,run,loaderVersion});assert.equal(await page.evaluate(()=>window.signals.length),1);});
 await check('normal stop disposes observer and removes only owned global',async()=>{await setup({hidden:true});await page.evaluate(()=>{window.dispatchEvent(new Event('ymcc-decky-health-stop'));document.querySelector('.deckyErrorBoundary').hidden=false;});assert.equal(await page.evaluate(()=>window.__YMCC_DECKY_HEALTH__),undefined);assert.equal(await page.evaluate(()=>window.signals.length),0);});
 await check('stale mirror session cannot emit a close request',async()=>{await setup({hidden:true});await page.evaluate(()=>{window.__YMCC_DECKY_MIRROR__={runId:'c'.repeat(32)};document.querySelector('.deckyErrorBoundary').hidden=false;});assert.equal(await page.evaluate(()=>window.signals.length),0);});
 await check('early execution before the html element exists remains event-driven',async()=>{await setup({hidden:true});await page.evaluate(({source,run,loaderVersion})=>{window.__YMCC_DECKY_HEALTH__.dispose();const html=document.documentElement;html.remove();(0,eval)('('+source+')')({runId:run,loaderVersion});html.querySelector('.deckyErrorBoundary').hidden=false;document.append(html);},{source,run,loaderVersion});await page.waitForFunction(()=>window.signals.length===1);});
 await check('offline browser: current UI proof and bootstrap revocation work with external networking blocked',async()=>{
  const liveSource=fs.readFileSync(path.join(root,'native/steam_live.js'),'utf8');
  let blocked=0;await page.route('**/*',route=>{blocked++;return route.abort('internetdisconnected');});
  const result=await page.evaluate(async({liveSource,source,run,loaderVersion})=>{
    window.__YMCC_DECKY_HEALTH__?.dispose();document.body.replaceChildren();delete window.__YMCC_DECKY_ACCOUNT_WATCH__;window.__ymccSteamLiveBusy=null;
    let currentCallback,loginCallback,account=42;
    window.settingsStore={m_CMInterface:{steamid:{GetAccountID:()=>account}}};
    window.SteamClient={UI:{GetUIMode:async()=>4,RegisterForUIModeChanged:()=>({unregister(){}})},User:{
      RegisterForCurrentUserChanges:callback=>{currentCallback=callback;queueMicrotask(()=>callback({strSteamID:'76561197960265770',bIsOfflineMode:true}));},
      RegisterForLoginStateChange:callback=>{loginCallback=callback;return {unregister(){}};}}};
    window.signals=[];window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
    const mode=await(0,eval)(liveSource)({operation:'ui.mode.get'});await Promise.resolve();
    const watch=window.__YMCC_DECKY_ACCOUNT_WATCH__;
    const before=watch.read(42);window.__YMCC_DECKY_MIRROR__={runId:run,frontendBuildId:'a'.repeat(64),token:'c'.repeat(64),endpoint:'ws://127.0.0.1:1/mirror'};
    (0,eval)('('+source+')')({...window.__YMCC_DECKY_MIRROR__,loaderVersion,account:42,accountSource:'verified-steam-ui'});
    const healthy=window.__YMCC_DECKY_HEALTH__.accountCurrent();
    currentCallback({strSteamID:'76561197960265770',bIsOfflineMode:false});const onlineStable=window.__YMCC_DECKY_HEALTH__.accountCurrent()&&window.signals.length===0;
    loginCallback('');const revoked=!window.__YMCC_DECKY_HEALTH__.accountCurrent()&&window.__YMCC_DECKY_MIRROR__.revoked===true&&!window.__YMCC_DECKY_MIRROR__.token;
    currentCallback({strSteamID:'76561197960265770',bIsOfflineMode:true});const oldCannotRevive=!window.__YMCC_DECKY_HEALTH__.accountCurrent();
    window.__YMCC_DECKY_HEALTH__.dispose();
    return {mode:mode.mode,before,healthy,onlineStable,revoked,oldCannotRevive,signals:window.signals};
  },{liveSource,source,run,loaderVersion});
  assert.equal(result.mode,'bigpicture');for(const key of ['before','healthy','onlineStable','revoked','oldCannotRevive'])assert.equal(result[key],true,key);
  assert.deepEqual(result.signals,[{runId:run,code:'decky-account-changed'}]);assert.equal(blocked,0,'identity and revoke chain never required a network fetch');
 });
 await check('healthy theme ancestor style storm never scans descendant subtrees',async()=>{
  const result=await page.evaluate(async({source,run,loaderVersion})=>{
    window.__YMCC_DECKY_HEALTH__?.dispose();document.body.replaceChildren();
    const library=document.createElement('section');library.innerHTML='<div><span>game</span></div>'.repeat(1200);document.body.append(library);
    window.signals=[];window.__YMCC_DECKY_MIRROR__={runId:run};
    window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
    let descendantQueries=0;const query=Element.prototype.querySelector;
    Element.prototype.querySelector=function(selector){if(selector==='.deckyErrorBoundary')descendantQueries++;return query.call(this,selector);};
    try{
      (0,eval)('('+source+')')({runId:run,loaderVersion});
      for(let i=0;i<160;i++)library.style.setProperty('--theme-frame',String(i));
      await Promise.resolve();await Promise.resolve();
      return {descendantQueries,signals:window.signals.length};
    }finally{Element.prototype.querySelector=query;window.__YMCC_DECKY_HEALTH__?.dispose();}
  },{source,run,loaderVersion});
  measurements.push({case:'healthy-theme-style-storm',gameSubtrees:1200,attributeMutations:160,...result});
  assert.equal(result.descendantQueries,0,'healthy ancestor style changes must not scan1200 game subtrees');assert.equal(result.signals,0);
 });
 await check('boundary class removal and reappearance below a hidden ancestor stay detectable',async()=>{
  await setup({hidden:true});
  const result=await page.evaluate(async()=>{
    const boundary=document.querySelector('.deckyErrorBoundary'),parent=document.createElement('section');parent.hidden=true;boundary.before(parent);parent.append(boundary);boundary.hidden=false;
    await Promise.resolve();boundary.classList.remove('deckyErrorBoundary');await Promise.resolve();
    boundary.classList.add('deckyErrorBoundary');await Promise.resolve();parent.hidden=false;
    await Promise.resolve();await Promise.resolve();return window.signals;
  });
  assert.deepEqual(result,[{runId:run,code:'decky-render-failed'}]);
 });
 await check('removing a stale boundary promotes the visible fifth boundary into the original four-node scan',async()=>{
  await setup({hidden:true});
  const result=await page.evaluate(async()=>{
    const first=document.querySelector('.deckyErrorBoundary');
    for(let i=0;i<4;i++){const node=first.cloneNode(true);node.hidden=i!==3;document.body.append(node);}
    await Promise.resolve();await Promise.resolve();const before=window.signals.length;
    first.remove();await Promise.resolve();await Promise.resolve();return {before,signals:window.signals};
  });
  assert.equal(result.before,0);assert.deepEqual(result.signals,[{runId:run,code:'decky-render-failed'}]);
 });

 await check('native teardown rechecks recovered health before any lifecycle action',async()=>{
  for(const kind of ['loader-recovered','loader-still-missing','boundary-recovered','boundary-still-fatal','boundary-read-throws','revoked-run']){
   const fatal=kind.startsWith('loader-')?'decky-frontend-incompatible':'decky-render-failed';
   await setup({hidden:fatal==='decky-frontend-incompatible'});
   const result=await page.evaluate(({kind,script})=>{
    let stopped=0,dismount=0,deinit=0;const listener=()=>stopped++;window.addEventListener('ymcc-decky-health-stop',listener);
    try{
     if(kind==='loader-still-missing')delete window.DeckyPluginLoader;else window.DeckyPluginLoader={dismountAll(){dismount++;},deinit(){deinit++;}};
     if(kind==='boundary-recovered')document.querySelector('.deckyErrorBoundary').hidden=true;
     if(kind==='boundary-read-throws')window.__YMCC_DECKY_HEALTH__.read=()=>{throw Error('readback unavailable');};
     if(kind==='revoked-run')window.__YMCC_DECKY_MIRROR__.revoked=true;
     return {value:(0,eval)(script),stopped,dismount,deinit};
    }finally{window.removeEventListener('ymcc-decky-health-stop',listener);}
   },{kind,script:teardown(run,fatal)});
   const accepted=kind==='loader-still-missing'||kind==='boundary-still-fatal';
   assert.equal(result.value,accepted,kind+' must use current fault evidence');assert.equal(result.stopped,accepted?1:0,kind+' stop event');
   assert.equal(result.dismount,kind==='boundary-still-fatal'?1:0,kind+' dismount');assert.equal(result.deinit,kind==='boundary-still-fatal'?1:0,kind+' deinit');
  }
 });
 await check('healthy native readback rearms a later fatal in the same document',async()=>{
  await setup();const result=await page.evaluate(async script=>{
   window.DeckyPluginLoader={};const boundary=document.querySelector('.deckyErrorBoundary');boundary.hidden=true;
   const declined=(0,eval)(script);await Promise.resolve();await Promise.resolve();
   const afterHealthy=window.signals.length;boundary.hidden=false;await Promise.resolve();await Promise.resolve();
   const afterNewFatal=window.signals.length;
   for(let i=0;i<20;i++)boundary.style.borderWidth=(i%2)+'px';await Promise.resolve();await Promise.resolve();
   return {declined,afterHealthy,afterNewFatal,afterNoise:window.signals.length};
  },teardown(run,'decky-render-failed'));
  assert.deepEqual(result,{declined:false,afterHealthy:1,afterNewFatal:2,afterNoise:2});
 });
 for(const name of requested)assert.ok(registered.some(c=>c.name===name),'Unknown exact health case: '+name);
 const selected=requested.length?registered.filter(c=>requested.includes(c.name)):registered;
 for(const {name,fn} of selected){try{await fn();cases.push(name);}catch(error){fs.writeFileSync(path.join(out,'HEALTH-DOM-PROGRESS.json'),JSON.stringify({passed:false,completed:cases,failed:name,error:String(error)},null,2));throw error;}}
 fs.writeFileSync(path.join(out,'HEALTH-DOM.json'),JSON.stringify({passed:true,cases:cases.length,results:cases,measurements,selectedCases:cases,skippedCases:registered.filter(c=>!selected.includes(c)).map(c=>c.name),fullSuite:!requested.length,actualCompiledTeardownCaptureVerified:!!actualCapture,realBrowser:true,realSteam:false},null,2));console.log('Decky health real DOM tests passed: '+cases.length);
}finally{await browser.close();}
