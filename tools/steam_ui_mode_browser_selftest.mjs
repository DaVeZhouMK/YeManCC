import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';import {createServer} from 'node:http';import {build} from 'esbuild';
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));const {parse,compileScript,compileStyle}=require('vue/compiler-sfc');const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_mode-test.cjs')('playwright');
const out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Deliverables/Steam-Mode-Switch-20261011/validation/browser');fs.mkdirSync(out,{recursive:true});
const wakeOnly=process.argv[3]==='--wake-only';
const ipc=`export async function invoke(cmd,args,{signal}={}){const f=window.fixture;if(cmd==='power.lifecycle'){f.powerReads=(f.powerReads||0)+1;const state=f.power;if(f.holdPower)await new Promise(resolve=>f.releasePower=resolve);return state}if(cmd==='steam.uiState'){
 f.reads++;if(f.fail)throw Error('fixture timeout');
 const receipt={known:true,running:f.running,mode:f.running?f.mode:'stopped',pid:f.running?(f.pid??123):0,birth:f.running?(f.birth??'456'):'0',reason:''};
 if(f.holdStatus)await new Promise((resolve,reject)=>{
  const abort=()=>{f.retiredStatus=(f.retiredStatus||0)+1;reject(Error('IPC request retired'));};
  f.releaseStatus=()=>{signal?.removeEventListener('abort',abort);resolve();};
  if(signal?.aborted)abort();else signal?.addEventListener('abort',abort,{once:true});
 });
 return receipt;
}if(cmd==='steam.ui.showBigPicture'){f.shows=(f.shows||0)+1;f.showArgs=args;return {ok:true}}if(cmd==='steam.uiMode.set'){f.requests.push(args);if(f.holdSwitch)await new Promise(resolve=>window.releaseSwitch=resolve);if(f.switchFail)throw Error('fixture switch failed');if(!f.unconfirmed){f.mode=args.mode;window.dispatchEvent(new Event('ipc:steam.uiStateChanged'));}return {ok:true}}throw Error('unexpected IPC '+cmd)}export function on(event,handler){const notify=e=>handler(e.detail,e);window.addEventListener('ipc:'+event,notify);return()=>window.removeEventListener('ipc:'+event,notify)}`;
const mocks={
 '@/bridge/yeman':`export const STEAM_ADDONS=[];export const steamAddonExists=async()=>false,steamAddonSet=async()=>{},steamCustomAddonAdd=async()=>({}),steamCustomAddonRemove=async()=>{},steamCustomAddonSet=async()=>{},steamCustomAddons=async()=>[],steamExeExists=async()=>true,steamRunning=async()=>window.fixture.running;export async function launchSteam(){window.fixture.launches++;window.fixture.running=true;window.fixture.mode=window.fixture.launchMode};export async function launchSteamAddon(){window.fixture.launches++};export async function steamStop(){window.fixture.stops++}`,
 '@/bridge/api':`export const dialog={openFile:async()=>null},shell={execute:async()=>{throw Error('unexpected shell')}}`,
 '@/bridge/uiLifecycle':`export const isUiVisible=()=>window.fixture.visible!==false&&window.fixture.inputReady!==false;export const getUiVisibilityState=()=>({documentVisible:window.fixture.visible!==false,nativeWindowVisible:true,powerReady:window.fixture.inputReady!==false});export const onUiVisibilityChange=()=>()=>{}`,
 '@/bridge/deckySidebar':`export const deckySidebar={get:async()=>({enabled:true,revision:1,phase:'ready'}),subscribe:()=>()=>{},setEnabled:async()=>({enabled:true,revision:2,phase:'ready'})};export const deckySidebarCanRepair=()=>false,deckyConnectionSummary=()=>'',deckySidebarDescription=()=>'',deckySidebarSetupFailure=()=>''`,
 '@/bridge/steamAppearance':`export const steamAppearance={start:async()=>({backendReady:true})}`,
 '@/bridge/customSteamLibrary':`export const readCustomSteamLibrarySummary=async()=>({waiting:0,joined:0,needs:0,excluded:0}),launchCustomSteamLibrary=async()=>({ok:true})`,
 '@/bridge/sharedGameMedia':`export const sharedGameMedia={preparationDescription:()=>'',subscribe:()=>()=>{}};export const steamWallpaperEnabled=()=>false,setSteamWallpaperEnabled=async()=>{}`,
 '@/bridge/gameMediaDownloads':`export const gameMediaDownloads={state:{},subscribe:()=>()=>{},cancelManual:async()=>{},requestNow:async()=>{}};export const backgroundDownloadEnabled=()=>false,setBackgroundDownloadEnabled=async()=>{},downloadDescription=()=>'',setDownloadCpuThreshold=async()=>{}`,
 '@/bridge/uiSettings':`export const getUiSetting=()=>30;export const setUiSettings=async()=>{}`,
 '@/components/SteamWallpaperPlaybackToggle.vue':`export default {render:()=>null}`,
 '@/components/SteamAppearanceCard.vue':`export default {template:'<div></div>'}`,
 '@/components/SteamQuickAccessMapping.vue':`export default {render:()=>null}`,
};
const styles=[];const entry=`import {createApp} from 'vue';import Steam from './src/views/SteamView.vue';createApp(Steam).mount('#app');`;
const result=await build({stdin:{contents:entry,loader:'ts',resolveDir:root},bundle:true,write:false,platform:'browser',format:'iife',loader:{'.png':'dataurl','.svg':'dataurl'},alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'mode-fixture',setup(b){
 b.onResolve({filter:/.*/},a=>a.path==='./ipc'&&/[/\\](steamUi|powerLifecycleState)\.ts$/.test(a.importer)?{path:'ipc',namespace:'fixture'}:a.path==='./yeman'&&a.importer.endsWith('steamUi.ts')?{path:'@/bridge/yeman',namespace:'fixture'}:a.path in mocks?{path:a.path,namespace:'fixture'}:undefined);
 b.onLoad({filter:/.*/,namespace:'fixture'},a=>({contents:a.path==='ipc'?ipc:mocks[a.path],loader:'ts'}));
 b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-mode',code=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles){styles.push(compileStyle({source:style.content,filename:a.path,id,scoped:style.scoped}).code)}return {contents:code.content.replace('export default','const c=')+`;c.__scopeId=${JSON.stringify(id)};export default c;`,loader:'ts',resolveDir:path.dirname(a.path)}})
 }}]});
const css=fs.readFileSync('src/styles/tokens.css','utf8')+'\n'+fs.readFileSync('src/styles/integratedConsole.css','utf8')+'\n'+styles.join('\n')+'\nbody{margin:0;padding:12px;background:#0b0e13;color:#f2f4fa;font-family:Segoe UI,sans-serif}button{font-family:inherit}.page{display:grid;gap:12px}.card{padding:14px;border-radius:14px;background:#141b26}.card-title{margin:0 0 10px}';
const html=`<!doctype html><html><head><meta charset='utf-8'><style>${css}</style></head><body><div id='app'></div><script>window.fixture={running:true,mode:'desktop',launchMode:'desktop',reads:0,requests:[],launches:0,stops:0,fail:false,switchFail:false,power:{generation:2,phase:'ready',known:true,hardwareWritesAllowed:true,resumeReady:false,publicationRevision:1}};</script><script src='/fixture.js'></script></body></html>`;
const server=createServer((req,res)=>{res.setHeader('Content-Type',req.url==='/fixture.js'?'text/javascript':'text/html;charset=utf-8');res.end(req.url==='/fixture.js'?result.outputFiles[0].text:html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));
if(wakeOnly){
 let browser,page;const cases=[],errors=[];
 try{
  browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  page=await browser.newPage();page.on('pageerror',error=>errors.push(error.message));
  await page.clock.install();
  await page.goto('http://127.0.0.1:'+server.address().port);
  const status=()=>page.locator('.sc-text').textContent({timeout:4000});
  const read=()=>page.evaluate(()=>window.fixture.reads);
  const dispatch=(name,detail)=>page.evaluate(([name,detail])=>window.dispatchEvent(new CustomEvent('ipc:'+name,{detail})),[name,detail]);
  const publish=detail=>page.evaluate(detail=>{window.fixture.power=detail;window.dispatchEvent(new CustomEvent('ipc:power.lifecycleChanged',{detail}));},detail);
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中',null,{timeout:5000});
  const first=await read();
  await page.evaluate(()=>{window.fixture.mode='bigpicture';window.fixture.inputReady=false;});
  await dispatch('power.resume-ready',{generation:200});
  await dispatch('power.resumed',{generation:1});
  await page.clock.runFor(5200);
  assert.equal(await read(),first,'unconfirmed and old aliases must not renew the Steam query budget');
  assert.equal(await status(),'桌面Steam运行中');cases.push('unconfirmed and old aliases leave current Steam observation untouched');
  const frame={generation:3,phase:'resuming',known:true,hardwareWritesAllowed:false,resumeReady:true,publicationRevision:2,observedNotificationSequence:7};
  await publish({...frame,notificationPending:true});await page.clock.runFor(5200);
  assert.equal(await read(),first,'pending F5 must not start Steam observation');
  await publish({...frame,notificationPending:false});
  await page.clock.runFor(5200);
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='大屏Steam运行中',null,{timeout:5000});
  assert.equal(await read(),first+1);cases.push('only confirmed current F5 refreshes Steam before input is ready');
  await publish({...frame,notificationPending:false,publicationRevision:3});
  await dispatch('power.resume-ready',{generation:3});
  await page.clock.runFor(5200);assert.equal(await read(),first+1);cases.push('duplicate F5 and its raw alias do not add reads');
  await page.evaluate(()=>{window.fixture.mode='desktop';window.fixture.holdPower=true;});
  const ready={generation:4,phase:'ready',known:true,hardwareWritesAllowed:true,resumeReady:false,notificationPending:false,publicationRevision:4,observedNotificationSequence:9,suspendSerial:7};
  await page.evaluate(ready=>{window.fixture.power=ready;},ready);
  await dispatch('power.resumed',{generation:4});
  // F5 keeps the public source in Resuming; honor its existing capped retry.
  await page.clock.runFor(31000);
  await page.waitForFunction(()=>!!window.fixture.releasePower,null,{timeout:5000});
  await page.clock.runFor(5200);assert.equal(await read(),first+1,'missing public publication must await Native readback');
  await page.evaluate(()=>{window.fixture.holdPower=false;window.fixture.releasePower();});
  await page.clock.runFor(5200);
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中',null,{timeout:5000});
  assert.equal(await read(),first+2);cases.push('public readback compensates for missing Ready publication');
  await page.evaluate(()=>{window.fixture.visible=false;window.fixture.mode='bigpicture';});
  await publish({...ready,generation:5,publicationRevision:5,observedNotificationSequence:10});
  await page.clock.runFor(5200);assert.equal(await read(),first+2);
  await page.evaluate(()=>{window.fixture.visible=true;document.dispatchEvent(new Event('visibilitychange'));});
  await page.clock.runFor(5200);
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='大屏Steam运行中',null,{timeout:5000});
  assert.equal(await read(),first+3);cases.push('hidden Ready stays quiet and document return refreshes even while input is unavailable');
  await page.evaluate(()=>{window.fixture.running=false;window.fixture.holdStatus=true;});
  await dispatch('window.shown');await page.clock.runFor(5200);
  await page.waitForFunction(()=>!!window.fixture.releaseStatus,null,{timeout:5000});
  assert.equal(await read(),first+4);
  const merged={...ready,generation:5,publicationRevision:6,observedNotificationSequence:10,suspendSerial:8};
  await page.evaluate(()=>{const f=window.fixture;f.running=true;f.mode='desktop';f.holdStatus=false;});
  await publish(merged);
  assert.equal(await status(),'大屏Steam运行中','retiring the old read must not report Steam stopped');
  await page.clock.runFor(5200);
  assert.equal(await read(),first+5,'current observation must run before the old transport replies');
  assert.equal(await page.evaluate(()=>window.fixture.retiredStatus),1,'one old renderer wait retired');
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中',null,{timeout:5000});
  await page.evaluate(()=>window.fixture.releaseStatus());
  assert.equal(await status(),'桌面Steam运行中','the late pre-suspend stopped reply must not replace the current status');
  await publish({...merged,publicationRevision:7,inputReady:true});
  await publish({...merged,publicationRevision:8,suspendSerial:7});
  await page.clock.runFor(5200);
  assert.equal(await read(),first+5,'duplicate and stale Ready do not rearm Steam observation');
  cases.push('coalesced Ready suppresses the held old status and observes once without actions');
  await page.evaluate(()=>{const f=window.fixture;f.running=false;f.holdStatus=true;f.releaseStatus=null;});
  await dispatch('window.shown');await page.clock.runFor(5200);
  await page.waitForFunction(()=>!!window.fixture.releaseStatus,null,{timeout:5000});
  assert.equal(await read(),first+6);
  await dispatch('power.resumed',{generation:5,readbackScope:{suspendSerial:8,notificationSequence:11}});
  await page.evaluate(()=>{const f=window.fixture;f.holdStatus=false;f.releaseStatus();});
  await page.clock.runFor(100);
  assert.equal(await status(),'桌面Steam运行中','a newer scope awaiting readback must fence the old stopped reply');
  assert.equal(await page.evaluate(()=>window.fixture.retiredStatus),2,'the old renderer wait was released at the hint');
  await dispatch('window.shown');await dispatch('steam.uiStateChanged');await page.clock.runFor(5200);
  assert.equal(await read(),first+6,'ordinary status and visibility notices cannot bypass pending public readback');
  await page.evaluate(()=>{const f=window.fixture;f.running=true;f.mode='bigpicture';});
  await publish({...merged,publicationRevision:9,observedNotificationSequence:11});
  await page.clock.runFor(5200);
  assert.equal(await read(),first+7,'matching notification sequence resumes observation even within the same suspend serial');
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='大屏Steam运行中',null,{timeout:5000});
  cases.push('new public scope fences the old Steam status and reenables one read after confirmation');
  await page.evaluate(()=>{const f=window.fixture;f.running=false;f.holdStatus=true;f.releaseStatus=null;});
  await dispatch('window.shown');await page.clock.runFor(5200);
  await page.waitForFunction(()=>!!window.fixture.releaseStatus,null,{timeout:5000});
  assert.equal(await read(),first+8);
  const confirmed={...merged,publicationRevision:9,observedNotificationSequence:11};
  await publish({...confirmed,publicationRevision:10});
  await page.clock.runFor(5200);
  assert.equal(await read(),first+8,'A publication revision alone must leave the held read in place');
  assert.equal(await page.evaluate(()=>window.fixture.retiredStatus),2);
  const newer={...confirmed,publicationRevision:10,observedNotificationSequence:12};
  await page.evaluate(()=>{const f=window.fixture;f.running=true;f.mode='desktop';f.holdStatus=false;});
  await publish(newer);
  await page.clock.runFor(5200);
  assert.equal(await read(),first+9,'A directly confirmed notification must replace the held read before the old reply');
  assert.equal(await page.evaluate(()=>window.fixture.retiredStatus),3);
  await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中',null,{timeout:5000});
  await page.evaluate(()=>window.fixture.releaseStatus());
  await publish({...confirmed,publicationRevision:11});
  await publish({...newer,publicationRevision:11});
  await publish({...newer,publicationRevision:11,observedNotificationSequence:13});
  await page.clock.runFor(5200);
  assert.equal(await read(),first+9,'Late, duplicate and idle ingress updates must not issue another status read');
  assert.equal(await status(),'桌面Steam运行中','The late stopped reply must never replace the current status');
  cases.push('direct same-suspend Ready advances notification scope, replaces one held read and keeps idle updates quiet');
  const actions=await page.evaluate(()=>({launches:window.fixture.launches,switches:window.fixture.requests.length,stops:window.fixture.stops}));
  assert.deepEqual(actions,{launches:0,switches:0,stops:0});assert.deepEqual(errors,[]);
  const record={ok:true,passed:cases.length,cases,actions,errors};fs.writeFileSync(path.join(out,'wake-result.json'),JSON.stringify(record,null,2));console.log(JSON.stringify(record,null,2));
 }catch(error){fs.writeFileSync(path.join(out,'wake-failure.json'),JSON.stringify({ok:false,error:String(error),cases,errors,reads:await page?.evaluate(()=>window.fixture?.reads).catch(()=>null)},null,2));throw error;}
 finally{await browser?.close();await new Promise(r=>server.close(r));}
 process.exit(0);
}
let browser;const cases=[],shots=[],errors=[];try{browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:580,height:780}});page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中');assert.equal(await page.locator('.steam-power-button').textContent().then(s=>s.trim()),'点击启动为Steam大屏');cases.push('actual desktop-mode labels');
const borders=await page.locator('.states-row').evaluate(el=>[...el.querySelectorAll('button')].map(b=>({width:getComputedStyle(b).borderTopWidth,style:getComputedStyle(b).borderTopStyle})));assert(borders.every(b=>parseFloat(b.width)>=1&&b.style==='solid'));cases.push('both main buttons retain the current console layout visible borders');
await page.locator('.steam-session-bar').screenshot({path:path.join(out,'steam-buttons-desktop.png')});
let shot=path.join(out,'desktop-580.png');await page.screenshot({path:shot});shots.push(shot);await page.locator('.steam-power-button').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='大屏Steam运行中'&&!document.querySelector('.steam-power-button').disabled);assert.equal(await page.locator('.steam-power-button').textContent().then(s=>s.trim()),'点击启动为Steam桌面');cases.push('actual desktop to bigpicture through typed service and readback');
const preShow=await page.evaluate(()=>({requests:window.fixture.requests.length,launches:window.fixture.launches}));await page.locator('.steam-state-card').click();await page.waitForFunction(()=>window.fixture.shows===1&&!document.querySelector('.steam-state-card').disabled);assert.deepEqual(await page.evaluate(()=>window.fixture.showArgs),{pid:123,birth:'456',userInitiated:true});assert.deepEqual(await page.evaluate(()=>({requests:window.fixture.requests.length,launches:window.fixture.launches})),preShow);assert.equal(await page.locator('.sc-action').textContent(),'点击显示大屏');cases.push('Big Picture running status shows the existing client on explicit click without toggling modes or launching');
await page.locator('.steam-session-bar').screenshot({path:path.join(out,'steam-buttons-bigpicture.png')});
shot=path.join(out,'bigpicture-580.png');await page.screenshot({path:shot});shots.push(shot);await page.locator('.steam-power-button').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中'&&!document.querySelector('.steam-power-button').disabled);cases.push('actual bigpicture to desktop with no quit');
await page.setViewportSize({width:480,height:780});await page.screenshot({path:path.join(out,'desktop-480.png')});const fit=await page.locator('.states-row').evaluate(el=>({client:el.clientWidth,scroll:el.scrollWidth}));assert(fit.scroll<=fit.client+1);cases.push('two parallel buttons fit 480px without horizontal overflow');
for(const theme of ['red-black','cyberpunk']){await page.evaluate(t=>document.documentElement.setAttribute('data-theme',t),theme);const info=await page.locator('.states-row').evaluate(el=>({fit:el.scrollWidth<=el.clientWidth+1,borders:[...el.querySelectorAll('button')].map(b=>getComputedStyle(b).borderTopColor)}));assert.equal(info.fit,true);assert(info.borders.every(c=>c!=='rgba(0, 0, 0, 0)'));await page.locator('.steam-session-bar').screenshot({path:path.join(out,'steam-buttons-'+theme+'.png')});cases.push(theme+' uses YMCC theme border colors without overflow')}
await page.evaluate(()=>document.documentElement.removeAttribute('data-theme'));
await page.evaluate(()=>{window.fixture.mode='unknown';window.dispatchEvent(new Event('ipc:steam.uiStateChanged'))});await page.waitForFunction(()=>document.querySelector('.steam-power-button').textContent.includes('重新识别'));const before=await page.evaluate(()=>window.fixture.requests.length);await page.locator('.steam-power-button').click();assert.equal(await page.evaluate(()=>window.fixture.requests.length),before);cases.push('unknown mode remains retryable and cannot silently switch or quit');
await page.evaluate(()=>{window.fixture.running=false;window.fixture.launchMode='desktop';window.dispatchEvent(new Event('ipc:steam.uiStateChanged'))});await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='未启动');await page.locator('.steam-power-button').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中'&&!document.querySelector('.steam-power-button').disabled);cases.push('stopped state launches once and reports desktop without an automatic delayed mode correction');
let counts=await page.evaluate(()=>({requests:window.fixture.requests,launches:window.fixture.launches,stops:window.fixture.stops}));assert.equal(counts.launches,1);assert.equal(counts.stops,0);assert.deepEqual(counts.requests.map(r=>r.mode),['bigpicture','desktop']);
await page.evaluate(()=>{window.fixture.running=false;window.fixture.launchMode='bigpicture';window.dispatchEvent(new Event('ipc:steam.uiStateChanged'))});await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='未启动');const beforeAlreadyBig=await page.evaluate(()=>window.fixture.requests.length);await page.locator('.steam-power-button').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='大屏Steam运行中'&&!document.querySelector('.steam-power-button').disabled);assert.equal(await page.evaluate(()=>window.fixture.requests.length),beforeAlreadyBig);cases.push('launch that already opens Big Picture does not send a duplicate mode request');
await page.evaluate(()=>{window.fixture.running=false;window.fixture.launchMode='unknown';window.dispatchEvent(new Event('ipc:steam.uiStateChanged'))});await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='未启动');const beforeUnknownLaunch=await page.evaluate(()=>({requests:window.fixture.requests.length,launches:window.fixture.launches}));await page.locator('.steam-power-button').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent.includes('模式未确认')&&!document.querySelector('.steam-power-button').disabled);const afterUnknownLaunch=await page.evaluate(()=>({requests:window.fixture.requests.length,launches:window.fixture.launches}));assert.deepEqual(afterUnknownLaunch,{requests:beforeUnknownLaunch.requests,launches:beforeUnknownLaunch.launches+1});cases.push('unknown mode after launch never guesses or relaunches Steam');
await page.evaluate(()=>{window.fixture.running=false;window.fixture.launchMode='desktop';window.fixture.switchFail=true;window.dispatchEvent(new Event('ipc:steam.uiStateChanged'))});await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='未启动');const beforeSwitchFail=await page.evaluate(()=>({requests:window.fixture.requests.length,launches:window.fixture.launches}));await page.locator('.steam-power-button').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中'&&!document.querySelector('.steam-power-button').disabled);const afterSwitchFail=await page.evaluate(()=>({requests:window.fixture.requests.length,launches:window.fixture.launches}));assert.deepEqual(afterSwitchFail,{requests:beforeSwitchFail.requests,launches:beforeSwitchFail.launches+1});cases.push('launch confirmation does not attempt a mode switch even when that interface would fail');
await page.evaluate(()=>{Object.assign(window.fixture,{mode:'desktop',switchFail:false,pid:123,birth:'456'});});await page.locator('.steam-state-card').click();await page.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中');
const beforeSessionDrift=await page.evaluate(()=>{window.fixture.pid=124;return window.fixture.requests.length});await page.locator('.steam-power-button').click();await page.waitForFunction(()=>!document.querySelector('.steam-power-button').disabled);assert.equal(await page.evaluate(()=>window.fixture.requests.length),beforeSessionDrift);assert.match(await page.locator('.err-bar').textContent(),/会话已改变/);cases.push('session change between displayed state and fresh read refuses to switch another Steam');
await page.evaluate(()=>{window.fixture.holdSwitch=true;window.fixture.unconfirmed=true});await page.locator('.steam-power-button').click();await page.waitForFunction(()=>!!window.releaseSwitch);assert.equal(await page.locator('.steam-power-button').isDisabled(),true);assert.match(await page.locator('.steam-power-button').textContent(),/正在切换到Steam大屏/);await page.evaluate(()=>{document.querySelector('.steam-power-button').click();document.querySelector('.steam-power-button').click()});assert.equal(await page.evaluate(()=>window.fixture.requests.length),beforeSessionDrift+1);cases.push('slow mode IPC keeps the correct progress label and rejects repeated clicks');
await page.evaluate(()=>{window.fixture.birth='789';window.fixture.mode='bigpicture';window.fixture.holdSwitch=false;window.releaseSwitch();window.releaseSwitch=null});await page.waitForFunction(()=>!document.querySelector('.steam-power-button').disabled);assert.match(await page.locator('.err-bar').textContent(),/会话已改变/);cases.push('target mode in a replacement Steam session cannot confirm an earlier request');
counts=await page.evaluate(()=>({requests:window.fixture.requests,launches:window.fixture.launches,stops:window.fixture.stops}));assert.equal(errors.length,0);cases.push('no page errors, no stop calls and no extra launches');
const record={ok:true,passed:cases.length,cases,shots,counts,errors,scope:'actual Vue component and typed mode service; isolated mocked native IPC; no real Steam mode switches'};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(record,null,2));console.log(JSON.stringify(record,null,2));}finally{await browser?.close();await new Promise(r=>server.close(r))}
