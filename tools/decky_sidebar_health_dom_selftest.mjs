// Real Chromium DOM running the exact embedded observer. Official v3.3.0 error-page structure;
// no installed Steam, Loader, hardware, or product settings are used.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {verifyHealthHeader} from './decky_sidebar_health_embed.mjs';
import {createHash} from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(process.env.YMCC_UI_TEST_REQUIRE_ANCHOR||path.join(process.env.USERPROFILE||'','/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_continuous_ui.cjs')),{chromium}=req('playwright');
const source=fs.readFileSync(path.join(root,'native/decky_sidebar_health.js'),'utf8');verifyHealthHeader();
// Optional explicit historical input. Default suite needs no Evidence path or
// external legacy files; its two opaque old-interface ports remain executable.
const legacyManifestFile=process.env.YMCC_DECKY_LEGACY_WATCH_SOURCES;
const legacySources=[];
if(legacyManifestFile){
 const manifest=JSON.parse(fs.readFileSync(legacyManifestFile,'utf8'));assert.equal(manifest.schemaVersion,1);assert(Array.isArray(manifest.sources));
 for(const row of manifest.sources){const file=path.resolve(root,row.relative);assert(!path.relative(root,file).startsWith('..'));const bytes=fs.readFileSync(file);assert.equal(createHash('sha256').update(bytes).digest('hex'),row.sha256);legacySources.push({name:row.name,source:bytes.toString('utf8')});}
}

const out=path.resolve(process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-health'),cases=[],measurements=[];fs.mkdirSync(out,{recursive:true});
// Original raw files are immutable even when an outer caller reuses an output lane.
for(const name of ['HEALTH-DOM.json','HEALTH-DOM-PROGRESS.json'])assert(!fs.existsSync(path.join(out,name)),'HEALTH_DOM_RAW_ALREADY_EXISTS:'+name);
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
   window.__YMCC_DECKY_HEALTH__?.dispose();document.body.replaceChildren();window.signals=[];
   window.boundaryRecords=[];window.healthOriginalConsoleInfo??=console.info;
   console.info=(...args)=>{if(typeof args[0]==='string'&&args[0].startsWith('[YMCC:DeckyHealth] '))window.boundaryRecords.push(JSON.parse(args[0].slice(19)));return window.healthOriginalConsoleInfo(...args);};Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:run},configurable:true,writable:true});window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
   const el=document.createElement('div');el.className='deckyErrorBoundary';if(options.hidden)el.hidden=true;
   el.innerHTML='<div>An error occurred while rendering this content.</div><pre><code></code></pre><div class="likely-occured-msg"></div><div class="panel-section"><div class="panel-header">Trace</div><pre><code></code></pre></div>';
   el.querySelector('pre code').textContent=`Error Reference: Shared SteamUI_123\nDecky Version: ${options.version||loaderVersion}${options.unsupported?'[U]':''}`;
   el.querySelector('.likely-occured-msg').textContent=options.attribution||'This error likely occurred in the Decky frontend.';
   el.querySelector('.panel-section code').textContent=options.trace||'TypeError: Cannot read properties of undefined\n at http://127.0.0.1:1337/frontend/index.js:1:100\n\nComponent Stack:\n at Example';
   document.body.append(el);(0,eval)('('+source+')')({runId:options.badRun?'invalid':run,loaderVersion});
   return {read:window.__YMCC_DECKY_HEALTH__?.read()||'',signals:window.signals,diagnostics:window.boundaryRecords};
 },{source,run,options,loaderVersion});
 const registered=[];const check=async(name,fn)=>{registered.push({name,fn});};
 await check('actual official layout without unused trace class detects visible core fatal',async()=>{const v=await setup();assert.equal(v.read,'decky-render-failed');assert.deepEqual(v.signals,[{runId:run,code:'decky-render-failed'}]);const exactLimit='TypeError: Cannot read properties of undefined\nat http://127.0.0.1:1337/frontend/index.js'.padEnd(8192,' ');assert.equal((await setup({trace:exactLimit})).signals.length,1,'complete8192-char core stack remains actionable');});
 await check('Windows unsupported suffix has no whitespace before next sibling',async()=>assert.equal((await setup({unsupported:true})).signals.length,1));
 for(const [name,options] of [['hidden boundary',{hidden:true}],['stale Loader version',{version:'v3.2.10'}],['Steam-only attribution',{attribution:'This error likely occurred in Steam.'}],['single plugin attribution',{attribution:'This error likely occurred in ExamplePlugin.'}],['plugin HTTP frame',{trace:'TypeError: Cannot read properties of undefined\nat http://127.0.0.1:1337/plugins/example/dist/index.js'}],['plugin sourcemap',{trace:'Minified React error #31\nat decky://decky/plugin/example/index.js'}],['legacy plugin sourcemap',{trace:'ReferenceError: missing\nat decky://decky/legacy_plugin/example/index.js'}],['network failure',{trace:'Error: Failed to fetch; HTTP 429'}],['network TypeError',{trace:'TypeError: Failed to fetch\nat http://127.0.0.1:1337/frontend/index.js'}],['transient resource TypeError',{trace:'TypeError: Load failed\nat http://127.0.0.1:1337/frontend/index.js'}],['unknown run',{badRun:true}]])await check(name+' cannot disable Decky',async()=>{
  const observed=await setup(options);assert.equal(observed.signals.length,0);
  if(name==='network TypeError'){
   assert.deepEqual(observed.diagnostics,[{schema:1,stage:'visible-boundary',ordinal:1,attribution:'decky',fault:'network',loader:'current'}]);
   const quiet=await page.evaluate(async()=>{
    const health=window.__YMCC_DECKY_HEALTH__,boundary=document.querySelector('.deckyErrorBoundary');
    for(let i=0;i<100;i++){health.read();boundary.style.borderWidth=(i%2)+'px';}await Promise.resolve();await Promise.resolve();
    return {signals:window.signals.length,diagnostics:window.boundaryRecords};
   });
   assert.equal(quiet.signals,0);assert.deepEqual(quiet.diagnostics,observed.diagnostics,'same visible boundary noise must not spam exports');
   const openings=await page.evaluate(async()=>{
    for(let i=0;i<1000;i++)window.dispatchEvent(new Event('ymcc-decky-connection-check'));
    await Promise.resolve();const one=window.signals.splice(0);
    window.dispatchEvent(new Event('ymcc-decky-connection-check'));await Promise.resolve();const two=window.signals.splice(0);
    const mirror=window.__YMCC_DECKY_MIRROR__;mirror.revoked=true;
    window.dispatchEvent(new Event('ymcc-decky-connection-check'));await Promise.resolve();const revoked=window.signals.splice(0);delete mirror.revoked;
    window.dispatchEvent(new Event('ymcc-decky-connection-result'));await Promise.resolve();const result=window.signals.splice(0);
    window.dispatchEvent(new Event('ymcc-decky-connection-check'));window.dispatchEvent(new Event('ymcc-decky-connection-result'));
    await Promise.resolve();const coalesced=window.signals.splice(0);
    window.dispatchEvent(new Event('ymcc-decky-connection-result'));
    for(let i=0;i<1000;i++)window.dispatchEvent(new Event(i%2?'ymcc-decky-connection-result':'ymcc-decky-connection-check'));
    await Promise.resolve();const reverse=window.signals.splice(0);
    window.dispatchEvent(new Event('ymcc-decky-connection-check'));window.dispatchEvent(new Event('ymcc-decky-connection-result'));
    mirror.revoked=true;await Promise.resolve();const revokedResult=window.signals.splice(0);delete mirror.revoked;
    window.dispatchEvent(new Event('ymcc-decky-connection-check'));await Promise.resolve();const nextOpening=window.signals.splice(0);
    return {one,two,revoked,result,coalesced,reverse,revokedResult,nextOpening};
   });
   assert.deepEqual(openings.one,[{runId:run,code:'decky-connection-check'}]);assert.deepEqual(openings.two,openings.one);assert.deepEqual(openings.revoked,[]);
   assert.deepEqual(openings.result,[{runId:run,code:'decky-connection-result'}],'probe completion must remain distinguishable from an opening');
   assert.deepEqual(openings.coalesced,openings.result,'completion survives an already queued opening without sending two hints');
   assert.deepEqual(openings.reverse,openings.result,'opening storms cannot downgrade a pending completion');
   assert.deepEqual(openings.revokedResult,[]);assert.deepEqual(openings.nextOpening,openings.one);
   await page.evaluate(()=>{document.querySelector('.likely-occured-msg').textContent='This error likely occurred in ExamplePlugin.';});
   await page.waitForFunction(()=>window.boundaryRecords.length===2);
   const plugin=await page.evaluate(()=>window.boundaryRecords.at(-1));assert.equal(plugin.attribution,'plugin');assert.equal(plugin.fault,'network');
   assert.equal(await page.evaluate(()=>window.signals.length),0);
   // Native browser EventTarget/CloseEvent lifecycle. The controlled socket
   // object never opens a network connection or exposes its URL/reason.
   const socketFacts=await page.evaluate(({source,run,loaderVersion})=>{
    const saved={backend:window.DeckyBackend,fetch:window.fetch,ws:window.WebSocket,timeout:window.setTimeout,interval:window.setInterval};
    let urlReads=0,reasonReads=0,network=0,timers=0,constructors=0,pendingDescriptorReads=0,pendingGetterReads=0;
    const rows=()=>window.boundaryRecords.filter(x=>x.stage==='official-socket-close');
    class OwnSocket extends EventTarget{
     constructor(state){super();this.readyState=state;this.adds=0;this.removes=0;this.active=new Map();}
     addEventListener(type,fn,...rest){this.adds++;this.active.set(type,fn);super.addEventListener(type,fn,...rest);}
     removeEventListener(type,fn,...rest){this.removes++;if(this.active.get(type)===fn)this.active.delete(type);super.removeEventListener(type,fn,...rest);}
     get url(){urlReads++;throw Error('URL must not be observed');}
     open(){this.readyState=1;this.dispatchEvent(new Event('open'));}
     close(code=1006,clean=false){this.readyState=3;const event=new Event('close');Object.defineProperties(event,{code:{value:code},wasClean:{value:clean},reason:{get(){reasonReads++;return 'credential-sentinel';}}});this.dispatchEvent(event);}
    }
    try{
     window.fetch=()=>{network++;throw Error('no health fetch');};window.WebSocket=class{constructor(){constructors++;throw Error('no health socket');}};
     window.setTimeout=()=>{timers++;throw Error('no health timer');};window.setInterval=window.setTimeout;
     const api=window.__YMCC_DECKY_HEALTH__,first=new OwnSocket(0),one=new Proxy({ws:first,runningCalls:new Map([[1,{secret:'pending-sentinel'}],[2,{get toJSON(){throw Error('do not inspect pending values');}}]])},{getOwnPropertyDescriptor(target,key){if(key==='runningCalls')pendingDescriptorReads++;return Reflect.getOwnPropertyDescriptor(target,key);}});window.DeckyBackend=one;
     for(let n=0;n<100;n++)api.read();const healthy={adds:first.adds,active:first.active.size,records:rows().length,pendingDescriptorReads};
     first.open();first.close();const firstFacts=rows().slice();
     for(let n=0;n<1000;n++)first.close();const repeated=rows().length;const afterDuplicatePendingReads=pendingDescriptorReads;
     const second=new OwnSocket(0);window.DeckyBackend={ws:second};first.close(1001,true);const lateRouter=rows().length;api.read();
     const replacement={firstActive:first.active.size,firstRemoves:first.removes,secondAdds:second.adds};
     second.close();const failedOpening=rows().at(-1);
     second.close('1006','false');const malformed=rows().at(-1);
     Object.defineProperty(window.DeckyBackend,'runningCalls',{get(){pendingGetterReads++;throw Error('pending getter must not run');},configurable:true});
     const third=new OwnSocket(1);window.DeckyBackend.ws=third;api.read();second.close(1002,false);const lateSocket=rows().length;
     for(let n=0;n<30;n++)third.close(4100+n,false);const cap=rows().length;
     const mirror=window.__YMCC_DECKY_MIRROR__;mirror.revoked=true;api.read();third.close(1000,true);const revoked={active:third.active.size,records:rows().length};delete mirror.revoked;
     Object.defineProperty(window.DeckyBackend,'runningCalls',{value:new Map(),configurable:true});
     const fourth=new OwnSocket(1);window.DeckyBackend.ws=fourth;api.read();
     const next='d'.repeat(32);Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:next},configurable:true,writable:true});
     (0,eval)('('+source+')')({runId:next,loaderVersion});
     const newOwner=window.__YMCC_DECKY_HEALTH__;fourth.close(1001,true);const newRunRecord=rows().at(-1);
     window.dispatchEvent(new Event('ymcc-decky-health-stop'));const disposed={active:fourth.active.size,healthAbsent:window.__YMCC_DECKY_HEALTH__===undefined};
     const count=rows().length;fourth.close(1002,false);first.close(1003,false);
     const closedQuiet=rows().length===count;
     return {healthy,firstFacts,repeated,lateRouter,replacement,failedOpening,malformed,lateSocket,cap,revoked,newRunRecord,disposed,closedQuiet,
      urlReads,reasonReads,network,timers,constructors,pendingDescriptorReads,pendingGetterReads,afterDuplicatePendingReads,signals:window.signals,credentialExported:JSON.stringify(rows()).includes('credential-sentinel')||JSON.stringify(rows()).includes('pending-sentinel')};
    }finally{window.__YMCC_DECKY_HEALTH__?.dispose();window.DeckyBackend=saved.backend;window.fetch=saved.fetch;window.WebSocket=saved.ws;window.setTimeout=saved.timeout;window.setInterval=saved.interval;Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:run},configurable:true,writable:true});}
   },{source,run,loaderVersion});
   measurements.push({case:'official-socket-close-primitives',observations:socketFacts,realBrowserEventTarget:true,realNetworkSocket:false});
   assert.equal(socketFacts.firstFacts.length,1,'official socket close primitives are missing');
   assert.deepEqual(socketFacts.healthy,{adds:2,active:2,records:0,pendingDescriptorReads:0});
   assert.deepEqual(socketFacts.firstFacts[0],{schema:1,stage:'official-socket-close',ordinal:1,codeKnown:true,code:1006,cleanKnown:true,wasClean:false,opened:true,pendingCallsKnown:true,pendingCalls:2});
   assert.equal(socketFacts.afterDuplicatePendingReads,1);assert.equal(socketFacts.pendingGetterReads,0);
   assert.equal(socketFacts.repeated,1);assert.equal(socketFacts.lateRouter,1);
   assert.deepEqual(socketFacts.replacement,{firstActive:0,firstRemoves:2,secondAdds:2});
   assert.equal(socketFacts.failedOpening.pendingCallsKnown,false);assert(!Object.hasOwn(socketFacts.failedOpening,'pendingCalls'));
   assert.equal(socketFacts.newRunRecord.pendingCallsKnown,true);assert.equal(socketFacts.newRunRecord.pendingCalls,0);
   assert.equal(socketFacts.failedOpening.opened,false);assert.equal(socketFacts.failedOpening.code,1006);
   assert.equal(socketFacts.malformed.codeKnown,false);assert.equal(socketFacts.malformed.cleanKnown,false);assert.equal(socketFacts.malformed.code,0);
   assert.equal(socketFacts.lateSocket,3);assert.equal(socketFacts.cap,16);assert.deepEqual(socketFacts.revoked,{active:0,records:16});
   assert.equal(socketFacts.newRunRecord.ordinal,1);assert.equal(socketFacts.newRunRecord.code,1001);
   assert.deepEqual(socketFacts.disposed,{active:0,healthAbsent:true});assert.equal(socketFacts.closedQuiet,true);
   for(const key of ['urlReads','reasonReads','network','timers','constructors'])assert.equal(socketFacts[key],0,key);
   assert.equal(socketFacts.credentialExported,false);
   assert.equal(socketFacts.signals.length,17,'bounded close binding count');
   assert.equal(socketFacts.signals[0].code,'decky-socket-close');
   assert.equal(socketFacts.signals[0].runId,run);
   assert.deepEqual(Object.keys(socketFacts.signals[0]).sort(),['runId','code','ordinal','codeKnown','closeCode','cleanKnown','wasClean','opened','pendingCallsKnown','pendingCalls'].sort());
   assert.equal(socketFacts.signals[0].closeCode,1006);assert.equal(socketFacts.signals[0].opened,true);
   assert(socketFacts.signals.every(v=>v.code==='decky-socket-close'&&v.ordinal>=1&&v.ordinal<=16&&JSON.stringify(v).length<=256));
   assert(socketFacts.signals.every(v=>!('url'in v)&&!('reason'in v)&&!('token'in v)));
   const loadFacts=await page.evaluate(({source,run,loaderVersion})=>{
    const saved={fetch:window.fetch,ws:window.WebSocket,timeout:window.setTimeout,interval:window.setInterval,backend:window.DeckyBackend,frontend:window.__YMCC_DECKY_FRONTEND__,boot:window.__YMCC_DECKY_FRONTEND_BOOT__,update:window.__YMCC_DECKY_FRONTEND_UPDATE__};
    let network=0,timers=0,sockets=0;const records=()=>window.boundaryRecords.filter(x=>x.stage==='frontend-load-state');
    window.__YMCC_DECKY_HEALTH__?.dispose();document.body.replaceChildren();window.boundaryRecords=[];window.signals=[];
    const build='a'.repeat(64),value={runId:run,loaderVersion,frontendBuildId:build},stamp=run+':'+build;
    const install=next=>{Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:next,configurable:true,writable:true});(0,eval)(source)(next);return window.__YMCC_DECKY_HEALTH__;};
    try{
     window.fetch=()=>{network++;throw Error('no frontend diagnostic fetch');};window.WebSocket=class{constructor(){sockets++;throw Error('no diagnostic socket');}};
     window.setTimeout=()=>{timers++;throw Error('no frontend diagnostic timer');};window.setInterval=window.setTimeout;
     window.DeckyBackend=undefined;delete window.__YMCC_DECKY_FRONTEND_UPDATE__;window.__YMCC_DECKY_FRONTEND_BOOT__={stamp,phase:'waiting-loader',probes:0,dispatched:false};
     window.__YMCC_DECKY_FRONTEND__={active:true,buildId:build};let api=install(value);for(let n=0;n<100;n++)api.read();const healthyLogs=records().length;
     window.__YMCC_DECKY_FRONTEND__={active:false,buildId:build};api.read();const waiting=records().at(-1);for(let n=0;n<1000;n++)api.read();const repeated=records().length;
     window.__YMCC_DECKY_FRONTEND_BOOT__={stamp,phase:'reading-enabled-plugin',probes:1,dispatched:false};api.read();const reading=records().at(-1);
     const goodBoot=window.__YMCC_DECKY_FRONTEND_BOOT__;let malformedAdded=0;const beforeBad=records().length;
     for(const bad of [null,{},'raw-secret',{stamp:run+':'+('b'.repeat(64)),phase:'failed',probes:1,dispatched:false},{stamp,phase:'credential-raw-text',probes:1,dispatched:false},{stamp,phase:'failed',probes:-1,dispatched:false},{stamp,phase:'failed',probes:4,dispatched:false},{stamp,phase:'failed',probes:1.5,dispatched:false},{stamp,phase:'failed',probes:1,dispatched:'secret'}]){window.__YMCC_DECKY_FRONTEND_BOOT__=bad;api.read();}
     malformedAdded=records().length-beforeBad;window.__YMCC_DECKY_FRONTEND_BOOT__=goodBoot;
     window.__YMCC_DECKY_FRONTEND_UPDATE__={stamp,phase:'pending',attempts:1,pending:true};api.read();const update=records().at(-1);
     delete window.__YMCC_DECKY_FRONTEND_BOOT__;
     const beforeSlot=records().length;
     window.__YMCC_DECKY_FRONTEND_UPDATE__={stamp,phase:'waiting-official-slot',attempts:1,pending:false};
     for(let n=0;n<128;n++)api.read();
     const slotRecords=records().slice(beforeSlot);
     const beforeBadSlot=records().length;
     for(const next of [{stamp:'other:'+build},{phase:'waiting-secret'},{attempts:4},{pending:'secret'}]){
      window.__YMCC_DECKY_FRONTEND_UPDATE__={stamp,phase:'waiting-official-slot',attempts:1,pending:false,...next};api.read();
     }
     const badSlotAdded=records().length-beforeBadSlot;
     window.__YMCC_DECKY_FRONTEND_BOOT__=goodBoot;
     window.__YMCC_DECKY_FRONTEND_UPDATE__={stamp,phase:'pending',attempts:1,pending:true};
     const beforeRevoked=records().length;window.__YMCC_DECKY_MIRROR__.revoked=true;window.__YMCC_DECKY_FRONTEND_BOOT__={stamp,phase:'failed',probes:2,dispatched:false};api.read();const revokedAdded=records().length-beforeRevoked;delete window.__YMCC_DECKY_MIRROR__.revoked;
     const originalInfo=console.info;console.info=()=>{throw Error('controlled diagnostic sink rejection');};let readOnLogFailure;try{readOnLogFailure=api.read();}finally{console.info=originalInfo;}
     const firstApi=api;api.dispose();const afterDisposeBefore=records().length;firstApi.read();const afterDisposeAdded=records().length-afterDisposeBefore;
     const next={...value,runId:'c'.repeat(32)},nextStamp=next.runId+':'+build;window.__YMCC_DECKY_FRONTEND_BOOT__={stamp:nextStamp,phase:'waiting-loader',probes:0,dispatched:false};delete window.__YMCC_DECKY_FRONTEND_UPDATE__;api=install(next);const newRunFirst=records().at(-1),newRunStart=records().length-1;
     for(const phase of ['waiting-loader','reading-enabled-plugin','waiting-receipt','official-load-in-progress','absent-or-disabled','invalid-plugin-contract','current','failed','canceled'])for(let probes=0;probes<4;probes++){window.__YMCC_DECKY_FRONTEND_BOOT__={stamp:nextStamp,phase,probes,dispatched:probes===3};api.read();}
     const newRunRecords=records().slice(newRunStart);const cap=newRunRecords.length;
     const exported=JSON.stringify(records()),privacy=!exported.includes(run)&&!exported.includes(next.runId)&&!exported.includes(build)&&!exported.includes('raw-secret')&&!exported.includes('credential');
     api.dispose();const malformedBuildStart=records().length;let malformedBuildReads=0;
     for(const badBuild of [null,17,{},'bad-build']){
      const bad={...value,runId:'d'.repeat(32),frontendBuildId:badBuild};const badApi=install(bad);if(badApi.read()==='')malformedBuildReads++;badApi.dispose();
     }
     const malformedBuildAdded=records().length-malformedBuildStart;
     return {malformedBuildReads,malformedBuildAdded,healthyLogs,waiting,repeated,reading,update,slotRecords,badSlotAdded,malformedAdded,revokedAdded,readOnLogFailure,afterDisposeAdded,newRunFirst,cap,privacy,network,timers,sockets,signals:window.signals};
    }finally{
     window.__YMCC_DECKY_HEALTH__?.dispose();window.fetch=saved.fetch;window.WebSocket=saved.ws;window.setTimeout=saved.timeout;window.setInterval=saved.interval;window.DeckyBackend=saved.backend;window.__YMCC_DECKY_FRONTEND__=saved.frontend;window.__YMCC_DECKY_FRONTEND_BOOT__=saved.boot;window.__YMCC_DECKY_FRONTEND_UPDATE__=saved.update;
    }
   },{source,run,loaderVersion});
   measurements.push({case:'own-frontend-load-state',observations:loadFacts,realBrowser:true,realNetwork:false});
   assert.equal(loadFacts.waiting?.stage,'frontend-load-state','own frontend startup wait phase is missing');
   assert.equal(loadFacts.healthyLogs,0);assert.deepEqual(loadFacts.waiting,{schema:1,stage:'frontend-load-state',ordinal:1,bootPhase:'waiting-loader',bootProbes:0,bootDispatched:false});
   assert.equal(loadFacts.repeated,1);assert.equal(loadFacts.reading.bootPhase,'reading-enabled-plugin');assert.equal(loadFacts.reading.bootProbes,1);
   assert.deepEqual({phase:loadFacts.update.updatePhase,attempts:loadFacts.update.updateAttempts,pending:loadFacts.update.updatePending},{phase:'pending',attempts:1,pending:true});
   assert.deepEqual(loadFacts.slotRecords,[{schema:1,stage:'frontend-load-state',ordinal:4,updatePhase:'waiting-official-slot',updateAttempts:1,updatePending:false}]);
   for(const key of ['badSlotAdded','malformedAdded','revokedAdded','afterDisposeAdded','network','timers','sockets'])assert.equal(loadFacts[key],0,key);
   assert.equal(loadFacts.malformedBuildReads,4);assert.equal(loadFacts.malformedBuildAdded,0);assert.equal(loadFacts.readOnLogFailure,'');assert.equal(loadFacts.newRunFirst.ordinal,1);assert.equal(loadFacts.cap,16);assert(loadFacts.privacy);assert.deepEqual(loadFacts.signals,[]);


  }
  if(name==='hidden boundary'||name==='unknown run')assert.equal(observed.diagnostics.length,0);
  if(name==='single plugin attribution')assert.equal(observed.diagnostics[0].attribution,'plugin');
  if(name==='plugin HTTP frame'){
   const prefix='TypeError: Cannot read properties of undefined\nat http://127.0.0.1:1337/frontend/index.js';
   const latePlugin=prefix.padEnd(8192,' ')+'\nat http://127.0.0.1:1337/plugins/example/dist/index.js';
   assert.equal((await setup({trace:latePlugin})).signals.length,0,'truncated plugin trace must remain unconfirmed');
   assert.equal((await setup({trace:prefix.padEnd(8193,' ')})).signals.length,0,'oversized core-only trace remains unconfirmed');
  }
 });
 await check('text-node version update triggers observer, without polling',async()=>{await setup({version:'v3.2.10'});await page.evaluate(v=>{document.querySelector('pre code').firstChild.data='Error Reference: Shared SteamUI_123\nDecky Version: '+v+'[U]';},loaderVersion);await page.waitForFunction(()=>window.signals.length===1);});
 await check('revealed actual boundary is scanned on its hidden attribute change',async()=>{await setup({hidden:true});await page.evaluate(()=>document.querySelector('.deckyErrorBoundary').hidden=false);await page.waitForFunction(()=>window.signals.length===1);});
 await check('retained invisible ancestor cannot close Decky until revealed',async()=>{
   await setup({hidden:true});
   await page.evaluate(()=>{const parent=document.createElement('section');parent.style.visibility='hidden';const boundary=document.querySelector('.deckyErrorBoundary');boundary.before(parent);parent.append(boundary);boundary.hidden=false;});
   assert.equal(await page.evaluate(()=>window.signals.length),0);
   await page.evaluate(()=>document.querySelector('section').style.visibility='visible');
   await page.waitForFunction(()=>window.signals.length===1);
   await setup({hidden:true});
   const result=await page.evaluate(async()=>{
     const boundary=document.querySelector('.deckyErrorBoundary'),parent=document.createElement('section'),style=document.createElement('style');
     style.textContent='#hidden-until-sibling { visibility:hidden } #hidden-until-sibling:has(> .show-boundary) { visibility:visible }';
     parent.id='hidden-until-sibling';document.body.append(style);boundary.before(parent);parent.append(boundary);boundary.hidden=false;
     await Promise.resolve();
     const all=Document.prototype.querySelectorAll, bounds=Element.prototype.getClientRects;
     let scans=0,visibilityChecks=0;
     Document.prototype.querySelectorAll=function(selector){if(selector==='.deckyErrorBoundary')scans++;return all.call(this,selector);};
     Element.prototype.getClientRects=function(){if(this===boundary)visibilityChecks++;return bounds.call(this);};
     try{
       const batches=60;
       for(let i=0;i<batches;i++){
         const label=document.createElement('span');label.textContent='unrelated';parent.append(label);await Promise.resolve();
       }
       const noise={scans,visibilityChecks,signals:window.signals.length};
       const show=document.createElement('span');show.className='show-boundary';parent.append(show);await Promise.resolve();
       return {batches,noise,showScans:scans-noise.scans,showChecks:visibilityChecks-noise.visibilityChecks,signals:window.signals};
     }finally{Document.prototype.querySelectorAll=all;Element.prototype.getClientRects=bounds;}
   });
   measurements.push({case:'hidden-boundary-sibling-mutations',...result});
   assert.equal(result.noise.signals,0);assert.equal(result.noise.visibilityChecks,result.batches);
   assert.equal(result.showChecks,1);assert.deepEqual(result.signals,[{runId:run,code:'decky-render-failed'}]);
   assert.equal(result.noise.scans,0,'simple leaf insertion under an ancestor need not rediscover the boundary list');
   assert.equal(result.showScans,0);
 });
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
    const Observer=window.MutationObserver,priorRouter=window.DeckyBackend;
    let callbacks=0;window.MutationObserver=class extends Observer{constructor(fn){super((batch,self)=>{callbacks++;fn(batch,self);});}};
    const socket=new EventTarget(),listeners=new Set();socket.readyState=1;
    socket.addEventListener=function(type,fn){listeners.add(fn);EventTarget.prototype.addEventListener.call(this,type,fn);};
    socket.removeEventListener=function(type,fn){listeners.delete(fn);EventTarget.prototype.removeEventListener.call(this,type,fn);};
    window.DeckyBackend={ws:socket};
    try{
      (0,eval)('('+source+')')({...window.__YMCC_DECKY_MIRROR__,loaderVersion,account:42,accountSource:'verified-steam-ui'});
      const health=window.__YMCC_DECKY_HEALTH__,healthy=health.accountCurrent(),socketListenersBefore=listeners.size;
      currentCallback({strSteamID:'76561197960265770',bIsOfflineMode:false});const onlineStable=health.accountCurrent()&&window.signals.length===0;
      loginCallback('');const revoked=!health.accountCurrent()&&window.__YMCC_DECKY_MIRROR__.revoked===true&&!window.__YMCC_DECKY_MIRROR__.token;
      const socketListenersAfterRevoke=listeners.size,callbacksAtRevoke=callbacks;
      const fatal=document.createElement('div');fatal.className='deckyErrorBoundary';
      fatal.innerHTML='<pre><code></code></pre><div class="likely-occured-msg">This error likely occurred in the Decky frontend.</div><div class="panel-section"><div class="panel-header">Trace</div><pre><code>TypeError: Cannot read properties of undefined</code></pre></div>';
      fatal.querySelector('pre code').textContent='Error Reference: Shared SteamUI_123\nDecky Version: '+loaderVersion;
      document.body.append(fatal);await Promise.resolve();
      const revokedDomCallbacks=callbacks-callbacksAtRevoke,revokedRead=health.read(),signals=window.signals.slice();
      currentCallback({strSteamID:'76561197960265770',bIsOfflineMode:true});const oldCannotRevive=!health.accountCurrent();
      return {mode:mode.mode,before,healthy,onlineStable,revoked,oldCannotRevive,signals,socketListenersBefore,socketListenersAfterRevoke,revokedDomCallbacks,revokedRead};
    }finally{window.__YMCC_DECKY_HEALTH__?.dispose();window.MutationObserver=Observer;window.DeckyBackend=priorRouter;document.body.replaceChildren();}
  },{liveSource,source,run,loaderVersion});
  assert.equal(result.mode,'bigpicture');for(const key of ['before','healthy','onlineStable','revoked','oldCannotRevive'])assert.equal(result[key],true,key);
  measurements.push({case:'offline-account-retired-health-observation',...result});
  assert.equal(result.socketListenersBefore,2);assert.equal(result.socketListenersAfterRevoke,0,'revoked account must immediately release the two official socket listeners');
  assert.equal(result.revokedDomCallbacks,0);assert.equal(result.revokedRead,'');
  assert.deepEqual(result.signals,[{runId:run,code:'decky-account-changed'}]);assert.equal(blocked,0,'identity and revoke chain never required a network fetch');

  // The existing offline case also covers a page whose native callback hook
  // is absent, throws once, or has not delivered its initial account yet.
  // Repeated existing mode reads must not accumulate callback registrations.
  const initialization=await page.evaluate(async({liveSource,source,run,loaderVersion})=>{
    const rows=[];
    for(const kind of ['missing-current-hook','missing-login-hook','current-hook-throws','login-hook-throws','callback-pending']){
      window.__YMCC_DECKY_HEALTH__?.dispose();delete window.__YMCC_DECKY_ACCOUNT_WATCH__;
      let account=42,currentCallback=null,loginCallback=null,currentAttempts=0,loginAttempts=0;
      window.settingsStore={m_CMInterface:{steamid:{GetAccountID:()=>account}}};
      const user={
        RegisterForCurrentUserChanges(fn){currentAttempts++;if(kind==='current-hook-throws')throw Error('fixture callback registration unavailable');currentCallback=fn;if(kind!=='callback-pending')queueMicrotask(()=>fn({strSteamID:'76561197960265770',bIsOfflineMode:true}));},
        RegisterForLoginStateChange(fn){loginAttempts++;if(kind==='login-hook-throws')throw Error('fixture callback registration unavailable');loginCallback=fn;return {unregister(){}};}
      };
      if(kind==='missing-current-hook')delete user.RegisterForCurrentUserChanges;
      if(kind==='missing-login-hook')delete user.RegisterForLoginStateChange;
      window.SteamClient={UI:{GetUIMode:async()=>4,RegisterForUIModeChanged:()=>({unregister(){}})},User:user};
      window.signals=[];window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
      const initial=await(0,eval)(liveSource)({operation:'ui.mode.get'});await Promise.resolve();
      const watch=window.__YMCC_DECKY_ACCOUNT_WATCH__;
      Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:run,frontendBuildId:'a'.repeat(64),token:'c'.repeat(64),endpoint:'ws://127.0.0.1:1/mirror'},configurable:true,writable:true});
      (0,eval)('('+source+')')({...window.__YMCC_DECKY_MIRROR__,loaderVersion,account:42,accountSource:'verified-steam-ui'});
      const healthy=window.__YMCC_DECKY_HEALTH__.accountCurrent()===true,read=watch?.read(42)===true;
      for(let n=0;n<20;n++)await(0,eval)(liveSource)({operation:'ui.mode.get'});
      const sameWatch=watch===window.__YMCC_DECKY_ACCOUNT_WATCH__;
      // A different getter value must still reject credentials for account42,
      // even if no native account callback exists to announce the transition.
      account=43;
      const rejectsChangedUi=watch?.read(42)===false&&window.__YMCC_DECKY_HEALTH__.accountCurrent()===false;
      // This is an EXISTING mode read, not a synthetic native account event.
      const changedMode=await(0,eval)(liveSource)({operation:'ui.mode.get'});
      const readbackRevokesOldBinding=window.__YMCC_DECKY_MIRROR__.revoked===true&&window.__YMCC_DECKY_MIRROR__.token===undefined&&window.signals.length===1;
      const oldHealth=window.__YMCC_DECKY_HEALTH__;
      const install=(id)=>{
        Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:id,frontendBuildId:'a'.repeat(64),token:'e'.repeat(64),endpoint:'ws://127.0.0.1:1/mirror'},configurable:true,writable:true});
        (0,eval)('('+source+')')({...window.__YMCC_DECKY_MIRROR__,loaderVersion,account:43,accountSource:'verified-steam-ui'});
      };
      install('d'.repeat(32));
      const recoveredNewBinding=changedMode.deckyAccountWatchAvailable===true&&window.__YMCC_DECKY_HEALTH__.accountCurrent()===true&&window.__YMCC_DECKY_MIRROR__.revoked!==true;
      // Late native evidence for the OLD account must not poison the live UI.
      let lateCallbackIgnored=null;
      if(currentCallback){const signals=window.signals.length;currentCallback({strSteamID:'76561197960265770',bIsOfflineMode:true});lateCallbackIgnored=window.__YMCC_DECKY_HEALTH__.accountCurrent()===true&&window.signals.length===signals;}
      const stableSignals=window.signals.length;
      for(let n=0;n<20;n++)await(0,eval)(liveSource)({operation:'ui.mode.get'});
      const stableReadbackQuiet=window.signals.length===stableSignals;
      // Explicit logout remains stronger than an unchanged stale settings ID.
      let explicitLogoutPreserved=null;
      if(loginCallback){loginCallback('');explicitLogoutPreserved=watch?.read(43)===false;}
      // If login callbacks are lost, an actually observed empty->live UI can
      // recover a NEW run; no revoked credential or old observer can revive.
      account=0;await(0,eval)(liveSource)({operation:'ui.mode.get'});
      account=43;const restored=await(0,eval)(liveSource)({operation:'ui.mode.get'});
      install('f'.repeat(32));
      const returnedUiRecovers=restored.deckyAccountWatchAvailable===true&&window.__YMCC_DECKY_HEALTH__.accountCurrent()===true;
      account=42;const oldCredentialsStayRevoked=oldHealth.accountCurrent()===false;account=43;
      rows.push({kind,mode:initial.mode,proofAvailable:initial.deckyAccountWatchAvailable===true,healthy,read,sameWatch,currentAttempts,loginAttempts,rejectsChangedUi,explicitLogoutPreserved,
        readbackRevokesOldBinding,recoveredNewBinding,lateCallbackIgnored,stableReadbackQuiet,returnedUiRecovers,oldCredentialsStayRevoked});
      window.__YMCC_DECKY_HEALTH__?.dispose();
    }
    return rows;
  },{liveSource,source,run,loaderVersion});
  measurements.push({case:'offline-account-hook-initialization',observations:initialization});
  for(const row of initialization){
    for(const key of ['proofAvailable','healthy','read','sameWatch','rejectsChangedUi','readbackRevokesOldBinding','recoveredNewBinding','stableReadbackQuiet','returnedUiRecovers','oldCredentialsStayRevoked'])assert.equal(row[key],true,row.kind+':'+key);
    assert.equal(row.mode,'bigpicture');
    assert.equal(row.currentAttempts,row.kind==='missing-current-hook'?0:1,row.kind+':current subscriptions');
    assert.equal(row.loginAttempts,row.kind==='missing-login-hook'?0:1,row.kind+':login subscriptions');
    if(row.explicitLogoutPreserved!==null)assert.equal(row.explicitLogoutPreserved,true,row.kind+':explicit logout');
    if(row.lateCallbackIgnored!==null)assert.equal(row.lateCallbackIgnored,true,row.kind+':late callback');
  }
  assert.equal(blocked,0,'initialization recovery never requires an external network fetch');

  // Logout is an immediate invalidation. If Steam returns to the same
  // account between reads and loses the login callback, a fresh existing mode
  // read must permit a NEW binding without needing to observe account=0.
  const sameAccount=await page.evaluate(async({liveSource,source,loaderVersion})=>{
    window.__YMCC_DECKY_HEALTH__?.dispose();delete window.__YMCC_DECKY_ACCOUNT_WATCH__;
    let loginCallback=null,currentAttempts=0,loginAttempts=0,account=42;
    window.settingsStore={m_CMInterface:{steamid:{GetAccountID:()=>account}}};
    window.SteamClient={UI:{GetUIMode:async()=>4,RegisterForUIModeChanged:()=>({unregister(){}})},User:{
      RegisterForCurrentUserChanges(){currentAttempts++;return {unregister(){}};},
      RegisterForLoginStateChange(fn){loginAttempts++;loginCallback=fn;return {unregister(){}};}
    }};
    window.signals=[];window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
    await(0,eval)(liveSource)({operation:'ui.mode.get'});
    const watch=window.__YMCC_DECKY_ACCOUNT_WATCH__;
    const install=id=>{Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:id,frontendBuildId:'a'.repeat(64),token:'c'.repeat(64),endpoint:'ws://127.0.0.1:1/mirror'},configurable:true,writable:true});
      (0,eval)('('+source+')')({...window.__YMCC_DECKY_MIRROR__,loaderVersion,account:42,accountSource:'verified-steam-ui'});};
    install('1'.repeat(32));const oldHealth=window.__YMCC_DECKY_HEALTH__;
    loginCallback('');
    const immediateLogoutRevoked=watch.read(42)===false&&oldHealth.accountCurrent()===false&&window.__YMCC_DECKY_MIRROR__.revoked===true;
    const stableUiObject=window.settingsStore.m_CMInterface.steamid;
    // No login/current-user callback and no empty-account observation here.
    const readback=await(0,eval)(liveSource)({operation:'ui.mode.get'});
    const proofReturned=readback.observedAccount===42&&readback.deckyAccountWatchAvailable===true&&watch.read(42)===true;
    install('2'.repeat(32));const newBindingHealthy=window.__YMCC_DECKY_HEALTH__.accountCurrent()===true;
    const oldCredentialCannotRevive=oldHealth.accountCurrent()===false;
    const signals=window.signals.length;
    for(let n=0;n<20;n++)await(0,eval)(liveSource)({operation:'ui.mode.get'});
    const healthyReadQuiet=window.signals.length===signals&&window.__YMCC_DECKY_ACCOUNT_WATCH__===watch;
    // Empty current UI still blocks recovery and does not restore old tokens.
    loginCallback('');account=0;const empty=await(0,eval)(liveSource)({operation:'ui.mode.get'});
    const emptyUiCannotRecover=empty.deckyAccountWatchAvailable===false&&watch.read(42)===false&&oldHealth.accountCurrent()===false;
    return {immediateLogoutRevoked,proofReturned,newBindingHealthy,oldCredentialCannotRevive,healthyReadQuiet,emptyUiCannotRecover,
      sameUiObject:window.settingsStore.m_CMInterface.steamid===stableUiObject,currentAttempts,loginAttempts,signalsBeforeEmpty:signals};
  },{liveSource,source,loaderVersion});
  measurements.push({case:'offline-same-account-missed-login-readback',observation:sameAccount});
  for(const key of ['immediateLogoutRevoked','proofReturned','newBindingHealthy','oldCredentialCannotRevive','healthyReadQuiet','emptyUiCannotRecover','sameUiObject'])assert.equal(sameAccount[key],true,'same-account:'+key);
  assert.equal(sameAccount.currentAttempts,1);assert.equal(sameAccount.loginAttempts,1);assert.equal(sameAccount.signalsBeforeEmpty,1);

  const upgrades=await page.evaluate(async({liveSource,source,run,loaderVersion,legacySources})=>{
    const rows=[];
    for(const seed of [{name:'opaque-legacy',source:null},{name:'throwing-subscription',source:null},...legacySources]){
      window.__YMCC_DECKY_HEALTH__?.dispose();delete window.__YMCC_DECKY_ACCOUNT_WATCH__;
      let account=42,currentAttempts=0,loginAttempts=0,currentCallback=null,loginCallback=null;
      window.settingsStore={m_CMInterface:{steamid:{GetAccountID:()=>account}}};
      window.SteamClient={UI:{GetUIMode:async()=>4,RegisterForUIModeChanged:()=>({unregister(){}})},User:{
        RegisterForCurrentUserChanges(fn){currentAttempts++;if(seed.name==='actual-strict-registration-failed')throw Error('fixture old registration failure');currentCallback=fn;queueMicrotask(()=>fn({strSteamID:seed.name.includes('stale-account')?'76561197960265771':'76561197960265770',bIsOfflineMode:true}));},
        RegisterForLoginStateChange(fn){loginAttempts++;loginCallback=fn;return {unregister(){}};}
      }};
      let legacyReadCalls=0,legacySubscribeCalls=0;
      if(seed.source){await(0,eval)(seed.source)({operation:'ui.mode.get'});await Promise.resolve();}
      else Object.defineProperty(window,'__YMCC_DECKY_ACCOUNT_WATCH__',{value:{version:1,read:()=>false,subscribe:()=>{if(seed.name==='throwing-subscription')throw Error('fixture legacy subscription unavailable');return ()=>{};}},configurable:true});
      const previous=window.__YMCC_DECKY_ACCOUNT_WATCH__,modern=previous?.readbackPolicy===2&&typeof previous?.refresh==='function';
      if(!modern&&previous){
        const oldRead=previous.read,oldSubscribe=previous.subscribe;
        previous.read=function(...args){legacyReadCalls++;return oldRead.apply(this,args);};
        previous.subscribe=function(...args){legacySubscribeCalls++;return oldSubscribe.apply(this,args);};
      }
      const before={current:currentAttempts,login:loginAttempts};
      const mode=await(0,eval)(liveSource)({operation:'ui.mode.get'});await Promise.resolve();
      const upgraded=window.__YMCC_DECKY_ACCOUNT_WATCH__;
      window.signals=[];window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
      const install=(id,expected)=>{
        Object.defineProperty(window,'__YMCC_DECKY_MIRROR__',{value:{runId:id,frontendBuildId:'a'.repeat(64),token:'c'.repeat(64),endpoint:'ws://127.0.0.1:1/mirror'},configurable:true,writable:true});
        (0,eval)('('+source+')')({...window.__YMCC_DECKY_MIRROR__,loaderVersion,account:expected,accountSource:'verified-steam-ui'});
      };
      try{install('a'.repeat(32),42);}catch(error){
        if(seed.name!=='throwing-subscription'||!String(error).includes('fixture legacy subscription unavailable'))throw error;
        rows.push({kind:seed.name,actualLegacySource:!!seed.source,modern,proofAvailable:mode.deckyAccountWatchAvailable===true,observerHealthy:false,stable:false,
          oldCredentialsRevoked:false,newBindingHealthy:false,lateOldCallbackIgnored:false,callbacksNotReregistered:currentAttempts===before.current&&loginAttempts===before.login,
          singleForwarder:false,legacyReadCalls,legacySubscribeCalls,compatibleObjectReused:!modern||upgraded===previous,expectedLegacySubscriptionError:String(error)});
        continue;
      }
      const observerHealthy=window.__YMCC_DECKY_HEALTH__.accountCurrent()===true;
      for(let n=0;n<20;n++)await(0,eval)(liveSource)({operation:'ui.mode.get'});
      // Read-only old/new consumers alternate on the SAME document and owner.
      if(seed.source)for(let n=0;n<5;n++){
        await(0,eval)(seed.source)({operation:'ui.mode.get'});
        await(0,eval)(liveSource)({operation:'ui.mode.get'});
      }
      const stable=window.__YMCC_DECKY_ACCOUNT_WATCH__===upgraded&&window.__YMCC_DECKY_HEALTH__.accountCurrent()===true&&window.signals.length===0;
      account=43;const changed=await(0,eval)(liveSource)({operation:'ui.mode.get'});
      const oldCredentialsRevoked=window.__YMCC_DECKY_MIRROR__.revoked===true&&window.__YMCC_DECKY_MIRROR__.token===undefined&&window.signals.length===1;
      install('d'.repeat(32),43);
      const newBindingHealthy=changed.deckyAccountWatchAvailable===true&&window.__YMCC_DECKY_HEALTH__.accountCurrent()===true;
      if(currentCallback)currentCallback({strSteamID:'76561197960265770',bIsOfflineMode:true});
      const lateOldCallbackIgnored=window.__YMCC_DECKY_HEALTH__.accountCurrent()===true&&window.signals.length===1;
      for(let n=0;n<8;n++)install(String(n).repeat(32),43);
      const callbacksNotReregistered=currentAttempts===before.current&&loginAttempts===before.login;
      const singleForwarder=modern||legacySubscribeCalls===1;
      rows.push({kind:seed.name,actualLegacySource:!!seed.source,modern,proofAvailable:mode.deckyAccountWatchAvailable===true,observerHealthy,stable,
        oldCredentialsRevoked,newBindingHealthy,lateOldCallbackIgnored,callbacksNotReregistered,singleForwarder,legacyReadCalls,legacySubscribeCalls,
        compatibleObjectReused:!modern||upgraded===previous,nativeBefore:before,nativeAfter:{current:currentAttempts,login:loginAttempts}});
      window.__YMCC_DECKY_HEALTH__?.dispose();
    }
    return rows;
  },{liveSource,source,run,loaderVersion,legacySources});
  measurements.push({case:'offline-account-watch-upgrade',observations:upgrades});
  for(const row of upgrades){
    for(const key of ['proofAvailable','observerHealthy','stable','oldCredentialsRevoked','newBindingHealthy','lateOldCallbackIgnored','callbacksNotReregistered','singleForwarder','compatibleObjectReused'])assert.equal(row[key],true,row.kind+':upgrade:'+key);
    if(!row.modern)assert.equal(row.legacyReadCalls,0,row.kind+':old account predicate is not authority');
  }
  assert.equal(blocked,0,'legacy watch migration does not require internet');


 });
 await check('healthy theme ancestor style storm never scans descendant subtrees',async()=>{
  const result=await page.evaluate(async({source,run,loaderVersion})=>{
    window.__YMCC_DECKY_HEALTH__?.dispose();document.body.replaceChildren();
    const library=document.createElement('section');library.innerHTML='<div><span>game</span></div>'.repeat(1200);document.body.append(library);
    window.signals=[];window.__YMCC_DECKY_MIRROR__={runId:run};
    window.__YMCC_DECKY_HEALTH_V1=raw=>window.signals.push(JSON.parse(raw));
     let descendantQueries=0,callbacks=0,records=0,boundaryScans=0;
     const query=Element.prototype.querySelector,all=Document.prototype.querySelectorAll,Observer=window.MutationObserver;
    Element.prototype.querySelector=function(selector){if(selector==='.deckyErrorBoundary')descendantQueries++;return query.call(this,selector);};
     Document.prototype.querySelectorAll=function(selector){if(selector==='.deckyErrorBoundary')boundaryScans++;return all.call(this,selector);};
    window.MutationObserver=class extends Observer{
      constructor(callback){super((batch,self)=>{callbacks++;records+=batch.length;callback(batch,self);});}
    };
    try{
      (0,eval)('('+source+')')({runId:run,loaderVersion});
      for(let i=0;i<160;i++)library.style.setProperty('--theme-frame',String(i));
      await Promise.resolve();await Promise.resolve();
       const style={descendantQueries,signals:window.signals.length,callbacks,records};
       descendantQueries=callbacks=records=boundaryScans=0;
       const leaves=document.createDocumentFragment();
       for(let i=0;i<1200;i++){
         const leaf=i%2?document.createElement('button'):document.createElementNS('http://www.w3.org/2000/svg','title');
         leaf.textContent='game';leaves.append(leaf);
       }
       const nested=document.createElement('section');nested.append(document.createElement('span'));
       library.append(leaves,nested);await Promise.resolve();
       const insertion={descendantQueries,signals:window.signals.length,callbacks,records,boundaryScans};
       const direct=document.createElement('div');direct.className='deckyErrorBoundary';library.append(direct);
       await Promise.resolve();const directBoundaryScans=boundaryScans;
       direct.remove();await Promise.resolve();boundaryScans=0;
       const wrapper=document.createElement('section'),fatal=document.createElement('div');fatal.className='deckyErrorBoundary';
       fatal.innerHTML='<pre><code></code></pre><div class="likely-occured-msg">This error likely occurred in the Decky frontend.</div><div class="panel-section"><div class="panel-header">Trace</div><pre><code>TypeError: Cannot read properties of undefined</code></pre></div>';
       fatal.querySelector('pre code').textContent='Error Reference: Shared SteamUI_123\nDecky Version: '+loaderVersion;
       wrapper.append(fatal);library.append(wrapper);await Promise.resolve();
       return {style,insertion,directBoundaryScans,nestedBoundaryScans:boundaryScans,nestedSignals:window.signals};
     }finally{Element.prototype.querySelector=query;Document.prototype.querySelectorAll=all;window.MutationObserver=Observer;window.__YMCC_DECKY_HEALTH__?.dispose();}
  },{source,run,loaderVersion});
   measurements.push({case:'healthy-theme-style-storm',gameSubtrees:1200,attributeMutations:160,...result.style});
   assert.equal(result.style.descendantQueries,0,'healthy ancestor style changes must not scan1200 game subtrees');assert.equal(result.style.signals,0);
   assert.equal(result.style.records,0,'healthy styles must not be delivered to the Decky observer');assert.equal(result.style.callbacks,0);
   measurements.push({case:'healthy-leaf-insertion-storm',leafInsertions:1200,...result.insertion,directBoundaryScans:result.directBoundaryScans,nestedBoundaryScans:result.nestedBoundaryScans,nestedSignalCount:result.nestedSignals.length});
   assert.equal(result.insertion.descendantQueries,1,'healthy leaves must not run subtree queries; the single non-leaf still must');
   assert.equal(result.insertion.signals,0);assert.equal(result.insertion.boundaryScans,0);
   assert.equal(result.directBoundaryScans,1,'an inserted boundary root is still discovered, even without element children');
   assert.equal(result.nestedBoundaryScans,1);assert.deepEqual(result.nestedSignals,[{runId:run,code:'decky-render-failed'}]);
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
 for(const {name,fn} of selected){try{await fn();cases.push(name);}catch(error){fs.writeFileSync(path.join(out,'HEALTH-DOM-PROGRESS.json'),JSON.stringify({passed:false,completed:cases,failed:name,error:String(error),measurements},null,2),{flag:'wx'});throw error;}}
 fs.writeFileSync(path.join(out,'HEALTH-DOM.json'),JSON.stringify({passed:true,cases:cases.length,results:cases,measurements,selectedCases:cases,skippedCases:registered.filter(c=>!selected.includes(c)).map(c=>c.name),fullSuite:!requested.length,actualCompiledTeardownCaptureVerified:!!actualCapture,realBrowser:true,realSteam:false},null,2),{flag:'wx'});console.log('Decky health real DOM tests passed: '+cases.length);
}finally{await browser.close();}
