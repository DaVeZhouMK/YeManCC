// Real foreground controller, IPC listener semantics, uiLifecycle and topmon projection.
// UI mode bindings/callback are extracted verbatim from the production Vue files.
// All native/power/monitor operations are isolated mocks; no installed settings writes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {createServer} from 'node:http';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'package.json'));
const {build}=require('esbuild');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_foreground-acdc-selftest.cjs'))('playwright');
const out=process.env.YMCC_FOREGROUND_POWER_BROWSER_OUT||path.resolve(root,'../../Build/Validation/foreground-acdc-20261008/browser');
fs.mkdirSync(out,{recursive:true});
const cases=[],errors=[];
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const app=read('src/App.vue'),source=read('src/bridge/powerSource.ts');
assert(!/setInterval|setTimeout|registerScheduledTask/.test(source));
assert(app.includes('stopForegroundPowerSource?.()'));
const callback=app.match(/startForegroundPowerSourceRefresh\(\(mode\) => \{([\s\S]*?)\n  \}\)/)?.[1];assert(callback);
assert(!/fanHostLifecycle|globalRefreshKey|scheduleSleepPowerPlanOptimization/.test(callback));
const bindings={
 schedule:read('src/views/PerformanceScheduleView.vue').match(/const powerSide = computed<PowerSide>\([\s\S]*?;/)?.[0],
 dedicated:read('src/components/GameCustomProfilePanel.vue').match(/const powerSide = computed<PowerSide>\([\s\S]*?;/)?.[0],
 tdp:read('src/views/TdpView.vue').match(/const powerMode = computed\([\s\S]*?;/)?.[0],
};
for(const binding of Object.values(bindings))assert(binding?.includes('powerSourceMode.value'));
const native=read('native/main.cpp');const nativeHandler=native.match(/ipc_on\("power.sourceSnapshot", \[\]\(const json&\) -> json \{([\s\S]*?)\n    \}\);/)?.[1];assert(nativeHandler);
assert.equal((nativeHandler.match(/GetSystemPowerStatus/g)||[]).length,1);
assert(!/SetTimer|ipc_emit|powercfg|poolSubmit|g_lastAcState|Write|Reg/.test(nativeHandler));
assert(app.includes('if (changed) schedulePowerChangePerformanceRestore(mode)'));
assert(app.includes('dedicatedOnly: schemeOwner !== \'auto\''));
cases.push('Source contract: one read-only Win32 power call; no polling, hardware writes or blanket refresh; all three production mode bindings share one source; real App callback restores existing owner only when side changed');
const mocks={
 './yeman':`export async function detectPowerModeReliable(){window.__fixture.fallbacks++;if(window.__fixture.failFallback)throw Error('old-shell probe failed');return window.__fixture.fallbackMode;}`,
 './monitorData':`export const acquireMonitorDemand=()=>()=>{};export async function readMonitorSnapshot(){window.__fixture.monitorReads++;return {top:window.__fixture.monitor};}`,
};
// Use the actual IPC bridge with a fake WebView transport, not synthetic source callbacks.
const boot=`
 const f=window.__fixture={nativeMode:'ac',reads:0,writes:0,requests:[],fallbacks:0,fallbackMode:null,failFallback:false,monitorReads:0,callbacks:[],restores:[],hold:false,unknown:false,fail:false,held:[],monitor:null};
 const messageListeners=[];
 window.chrome={webview:{addEventListener(type,listener){messageListeners.push(listener)},postMessage(request){
   if(request.cmd!=='power.sourceSnapshot'){f.writes++;throw Error('unexpected command '+request.cmd)}
   f.reads++;f.requests.push(request);const response=f.fail?{id:request.id,error:'command unavailable'}:{id:request.id,result:{known:!f.unknown,acLine:f.unknown?255:f.nativeMode==='ac'?1:0}};
   const reply=()=>messageListeners.forEach(l=>l({data:response}));if(f.hold)f.held.push(reply);else queueMicrotask(reply);
 }}};
 window.__release=()=>{f.held.shift()?.()};
 window.__native=(event,data)=>messageListeners.forEach(l=>l({data:{event,data}}));
`;
const bundle=await build({stdin:{resolveDir:root,loader:'ts',contents:`
 import {ref,computed,watchEffect} from 'vue';
 import {powerSourceMode,startForegroundPowerSourceRefresh} from './src/bridge/powerSource';
 import {topMonitorData,setTopMonitorData,readTopMonitor} from './src/bridge/topmon';
 type PowerSide='ac'|'dc';
 const onAcPower=ref(true);
 function applyVideoPowerMode(mode,authoritative){onAcPower.value=mode==='ac';window.__fixture.callbacks.push({mode,authoritative})}
 function schedulePowerChangePerformanceRestore(mode){window.__fixture.restores.push(mode)}
 const detectedPowerSide=ref<PowerSide>('ac'),detectedPowerMode=ref<PowerSide>('ac');
 const schedule=(()=>{${bindings.schedule};return powerSide})();
 const dedicated=(()=>{${bindings.dedicated};return powerSide})();
 const tdp=(()=>{${bindings.tdp};return powerMode})();
 window.__stop=startForegroundPowerSourceRefresh((mode)=>{${callback}});
 window.__start=()=>{window.__stop=startForegroundPowerSourceRefresh((mode)=>{${callback}})};
 window.__readMonitor=async()=>setTopMonitorData(await readTopMonitor());
 watchEffect(()=>{document.querySelector('#schedule').textContent=schedule.value;document.querySelector('#dedicated').textContent=dedicated.value;document.querySelector('#tdp').textContent=tdp.value;document.querySelector('#topmon').textContent=topMonitorData.value?.ac===undefined?'none':String(topMonitorData.value.ac)});
 window.__source=()=>powerSourceMode.value;
 window.__ready=true;
`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'isolated-power-dependencies',setup(b){b.onResolve({filter:/.*/},args=>args.path in mocks&&args.importer.replaceAll('\\','/').endsWith(args.path==='./yeman'?'/powerSource.ts':'/topmon.ts')?{path:args.path,namespace:'fixture'}:null);b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));}}]});
const js=boot+bundle.outputFiles[0].text;
const html='<!doctype html><meta charset="utf-8"><title>Foreground AC/DC regression</title><div id="schedule"></div><div id="dedicated"></div><div id="tdp"></div><div id="topmon"></div><script src="/fixture.js"></script>';
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?js:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));let browser;
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>window.__ready&&window.__fixture.callbacks.length===1);
 const reads=()=>page.evaluate(()=>window.__fixture.reads);
 const sends=async events=>page.evaluate(events=>events.forEach(event=>window.dispatchEvent(new CustomEvent('ipc:'+event))),events);
 const side=async expected=>{await page.waitForFunction(expected=>['schedule','dedicated','tdp'].every(id=>document.getElementById(id).textContent===expected),expected);assert.equal(await page.evaluate(()=>window.__source()),expected)};
 assert.equal(await reads(),1);await side('ac');assert.deepEqual(await page.evaluate(()=>window.__fixture.restores),[]);cases.push('Initial visible shell performs one shared read; unchanged AC side causes no hardware restore');
 await page.waitForTimeout(450);assert.equal(await reads(),1);cases.push('Visible idle has zero recurring power queries');
 await sends(Array(30).fill('window.restored'));await page.waitForTimeout(70);assert.equal(await reads(),1);cases.push('Native normal-size/restored messages from window resizing do not add foreground power queries');
 await sends(['window.hidden','power.suspending']);await page.evaluate(()=>window.__fixture.nativeMode='dc');await page.waitForTimeout(350);assert.equal(await reads(),1);await sends(['power.resuming','window.maximized','window.shown']);assert.equal(await reads(),1);await side('ac');cases.push('AC sleep then DC wake: hidden/suspended/resuming UI does not query or publish a premature source');
 await sends(['power.resumed','window.maximized','window.restored','window.shown','window.summoned']);await side('dc');assert.equal(await reads(),2);assert.deepEqual(await page.evaluate(()=>window.__fixture.restores),['dc']);cases.push('Ready foreground maximization repairs missed AC->DC in all displays with exactly one query; global/dedicated restore uses original callback once');
 await sends(['window.maximized','window.restored','window.shown']);await page.waitForFunction(()=>window.__fixture.callbacks.length===3);assert.equal(await reads(),3);assert.deepEqual(await page.evaluate(()=>window.__fixture.restores),['dc']);cases.push('Visible->visible maximize is not lost to lifecycle dedupe; repeated same-side show events coalesce without reapplying settings');
 await page.evaluate(()=>{const f=window.__fixture;f.monitor={ts:Date.now(),ac:1,hasBattery:true,tdpW:17,tempC:49};return window.__readMonitor()});await page.waitForFunction(()=>document.getElementById('topmon').textContent==='0');await side('dc');assert.deepEqual(await page.evaluate(()=>({tdp:window.__fixture.monitor.tdpW,temp:window.__fixture.monitor.tempC})),{tdp:17,temp:49});cases.push('Stale HWiNFO AC snapshot cannot undo fresh DC; monitor polling is not required for schedule/dedicated/TDP, and sensor fields are untouched');
 await page.evaluate(()=>{window.__fixture.nativeMode='ac';window.dispatchEvent(new Event('focus'))});await side('ac');assert.equal(await reads(),4);assert.deepEqual(await page.evaluate(()=>window.__fixture.restores),['dc','ac']);cases.push('Returning app focus performs one reliable refresh and synchronizes DC->AC for all consumers');
 const beforeEvent=await reads();await page.evaluate(()=>window.__native('power.sourceChanged',{ac:false}));await side('dc');await page.waitForFunction(()=>document.getElementById('topmon').textContent==='0');assert.equal(await reads(),beforeEvent);await page.evaluate(()=>window.__native('power.acChanged',{ac:true}));await side('ac');assert.equal(await reads(),beforeEvent);cases.push('Existing immediate/debounced plug notifications still update shared UI without any extra power query');
 await page.evaluate(()=>window.__fixture.hold=true);await sends(['window.maximized']);await page.waitForFunction(()=>window.__fixture.held.length===1);const heldCount=await reads();await sends(['window.maximized','window.restored','window.shown']);assert.equal(await reads(),heldCount);await page.evaluate(()=>{window.__native('power.sourceChanged',{ac:false});window.__fixture.hold=false;window.__release()});await side('dc');await page.waitForTimeout(70);await side('dc');cases.push('In-flight requests are single-flight; a newer native source event defeats a delayed stale AC answer');
 await page.evaluate(()=>{window.__fixture.hold=true;window.__fixture.nativeMode='ac'});await sends(['window.maximized']);await page.waitForFunction(()=>window.__fixture.held.length===1);const beforeBoundary=await reads();await sends(['window.hidden','power.suspending','power.resuming','power.resumed','window.maximized']);await page.evaluate(()=>{window.__fixture.nativeMode='dc';window.__fixture.hold=false;window.__release()});await page.waitForFunction(expected=>window.__fixture.reads===expected,beforeBoundary+1);await side('dc');assert.equal(await reads(),beforeBoundary+1);cases.push('Hide/sleep/show while a read is pending discards the pre-sleep reply and performs exactly one fresh boundary read');
 await page.evaluate(()=>window.__fixture.unknown=true);const beforeUnknown=await reads();await sends(['window.maximized']);await page.waitForFunction(expected=>window.__fixture.reads===expected,beforeUnknown+1);await page.waitForTimeout(250);await side('dc');assert.equal(await reads(),beforeUnknown+1);assert.equal(await page.evaluate(()=>window.__fixture.fallbacks),0);cases.push('Windows unknown=255 keeps last known DC and starts no fallback poll or retry');
 await page.evaluate(()=>{window.__fixture.unknown=false;window.__fixture.fail=true;window.__fixture.failFallback=true});const beforeFailure=await reads();await sends(['window.maximized']);await page.waitForFunction(()=>window.__fixture.fallbacks===1);await page.waitForTimeout(250);await side('dc');assert.equal(await reads(),beforeFailure+1);cases.push('Command/legacy probe failure preserves DC, remains bounded and never turns unknown into AC');
 await page.evaluate(()=>{window.__fixture.failFallback=false;window.__fixture.fallbackMode='ac'});await sends(['window.maximized']);await side('ac');assert.equal(await page.evaluate(()=>window.__fixture.fallbacks),2);cases.push('Old-shell compatibility can use one existing reliable fallback on a foreground request');
 await page.evaluate(()=>{window.__fixture.fail=false;window.__fixture.nativeMode='dc'});await sends(['window.minimized']);const beforeHidden=await reads();await page.waitForTimeout(300);assert.equal(await reads(),beforeHidden);await sends(['window.restored']);await side('dc');assert.equal(await reads(),beforeHidden+1);cases.push('Minimized windows stay idle; tray/restore repairs the mode immediately with one read');
 await page.evaluate(()=>{window.__fixture.hold=true;window.__fixture.fail=true});await sends(['window.maximized']);await page.waitForFunction(()=>window.__fixture.held.length===1);const beforeHiddenFallback=await page.evaluate(()=>window.__fixture.fallbacks);await sends(['window.hidden']);await page.evaluate(()=>{window.__fixture.hold=false;window.__release()});await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>window.__fixture.fallbacks),beforeHiddenFallback);await side('dc');await page.evaluate(()=>window.__fixture.fail=false);await sends(['window.maximized']);await side('dc');cases.push('A late native failure after the window hides does not start a background legacy/system probe');
 const stoppedCount=await reads();await page.evaluate(()=>window.__stop());await sends(['window.maximized','window.shown']);await page.evaluate(()=>{window.dispatchEvent(new Event('focus'));window.__native('power.sourceChanged',{ac:true})});await page.waitForTimeout(120);assert.equal(await reads(),stoppedCount);await side('dc');cases.push('Stopping the App binding removes all visibility/native/focus listeners and blocks pending publication');
 await sends(['window.hidden']);await page.evaluate(()=>window.__start());await page.waitForTimeout(150);assert.equal(await reads(),stoppedCount);await page.evaluate(()=>window.__fixture.nativeMode='ac');await sends(['window.maximized']);await side('ac');assert.equal(await reads(),stoppedCount+1);cases.push('Starting/rebinding while hidden causes no query; next foreground reads once without duplicate listeners');
 assert.equal(await page.evaluate(()=>window.__fixture.writes),0);assert.deepEqual(errors,[]);
 const report={suite:'Foreground one-shot AC/DC refresh browser',passed:cases.length,cases,pageErrors:errors,scope:'Real uiLifecycle/powerSource/ipc/topmon plus verbatim production UI mode bindings/App callback; native transport mocked; hardware and installed configuration writes 0'};fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve))}
