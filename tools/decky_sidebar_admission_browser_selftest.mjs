import {root,task,validation} from './fixtures/decky_task_paths.mjs';
// Actual SteamView, Toggle and Decky description; all Steam/native IPC is memory-only.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {createRequire} from 'node:module';import {createServer} from 'node:http';import {build} from 'esbuild';
const require=createRequire(path.join(root,'package.json'));
const {parse,compileScript,compileStyle}=require('vue/compiler-sfc');
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_decky-admission-test.cjs')('playwright');
const out=path.join(validation,'admission-browser');fs.mkdirSync(out,{recursive:true});
const ipc=`export async function invoke(cmd,args){const f=window.fixture;f.calls.push(cmd);if(cmd==='steam.uiState')return {known:true,running:true,mode:'bigpicture',pid:123,birth:'456',reason:''};if(cmd==='deckySidebar.get')return f.state;if(cmd==='deckySidebar.repairConnection'){f.repairArgs=args;return {ok:true,requestDispatched:true,mutated:true}};if(cmd==='deckySidebar.setEnabled'){f.state={...f.state,enabled:args.enabled,revision:f.state.revision+1};return f.state}if(cmd==='deckySidebar.environment')return {ready:true,reason:'ready'};throw Error('unexpected fixture IPC '+cmd)}export function on(name,fn){const h=e=>fn(e.detail);window.addEventListener('ipc:'+name,h);return()=>window.removeEventListener('ipc:'+name,h)}`;
const mocks={
 '@/bridge/yeman':`export const oemKeysGet=async()=>({supported:false,keys:[]}),setShortcutRecording=async()=>{throw Error('No recording in admission test')},summonGet=async()=>({enabled:true}),summonSet=async()=>{throw Error('No settings write in admission test')};export const STEAM_ADDONS=[];export const steamAddonExists=async()=>false,steamAddonSet=async()=>{},steamCustomAddonAdd=async()=>({}),steamCustomAddonRemove=async()=>{},steamCustomAddonSet=async()=>{},steamCustomAddons=async()=>[],steamExeExists=async()=>true,steamRunning=async()=>true;export async function launchSteam(){throw Error('No Steam launch allowed in this fixture')}`,
 '@/bridge/api':`export const windowApi={};export const dialog={openFile:async()=>null},shell={execute:async()=>{throw Error('No shell operation allowed in this fixture')}}`,
 '@/bridge/uiLifecycle':`export const isUiVisible=()=>true;export const getUiVisibilityState=()=>({documentVisible:true,nativeWindowVisible:true,powerReady:true});export const onUiVisibilityChange=()=>()=>{}`,
 '@/bridge/steamAppearance':`export const steamAppearance={start:async()=>({backendReady:true})}`,
 '@/bridge/customSteamLibrary':`export const readCustomSteamLibrarySummary=async()=>({waiting:0,joined:0,needs:0,excluded:0}),launchCustomSteamLibrary=async()=>{throw Error('No library operation allowed in this fixture')}`,
 '@/components/SteamAppearanceCard.vue':`export default {template:'<div></div>'}`,
};
const styles=[];
const built=await build({stdin:{contents:`import {createApp} from 'vue';import Steam from './src/views/SteamView.vue';createApp(Steam).mount('#app');`,loader:'ts',resolveDir:root},bundle:true,write:false,loader:{'.png':'dataurl'},platform:'browser',format:'iife',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'decky-inert-io',setup(b){
 b.onResolve({filter:/.*/},a=>a.path==='./ipc'&&/[/\\](steamUi|deckySidebar)\.ts$/.test(a.importer)?{path:'ipc',namespace:'fixture'}:a.path==='./yeman'&&a.importer.endsWith('steamUi.ts')?{path:'@/bridge/yeman',namespace:'fixture'}:a.path in mocks?{path:a.path,namespace:'fixture'}:undefined);
 b.onLoad({filter:/.*/,namespace:'fixture'},a=>({contents:a.path==='ipc'?ipc:mocks[a.path],loader:'ts'}));
 b.onLoad({filter:/\.vue$/},a=>{const {descriptor,errors}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path});assert.equal(errors.length,0);const id='data-v-decky-admission',code=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles)styles.push(compileStyle({source:style.content,filename:a.path,id,scoped:style.scoped}).code);return {contents:code.content.replace('export default','const c=')+`;c.__scopeId=${JSON.stringify(id)};export default c;`,loader:'ts',resolveDir:path.dirname(a.path)}});
}}]});
const script=built.outputFiles[0].text;fs.writeFileSync(path.join(task,'Build/decky-admission-ui-fixture.js'),script);
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:Segoe UI,sans-serif}.page{padding:14px}.card{padding:14px;margin-bottom:14px;background:var(--bg-panel);border-radius:8px}';
const pageHtml=`<!doctype html><meta charset="utf-8"><style>${css}\n${styles.join('\n')}</style><div id="app"></div><script>window.fixture={state:{enabled:true,steamKnown:true,steamRunning:true,pid:0,revision:1,phase:'waiting-manual-steam',reason:'steam-ui-state-pending',admissionRetry:true,admissionAttempts:1,admissionDetail:'live-busy'},calls:[]}</script><script src="/fixture.js"></script>`;
const server=createServer((req,res)=>{res.setHeader('Content-Type',req.url==='/fixture.js'?'application/javascript':'text/html; charset=utf-8');res.end(req.url==='/fixture.js'?script:pageHtml)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;
const cases=[],screenshots=[],errors=[];
try{
 browser=await chromium.launch({channel:"msedge",headless:true});const page=await browser.newPage({viewport:{width:580,height:820},deviceScaleFactor:1});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.locator('[data-settings-section="integration"]').click();const status=page.locator('.decky-sidebar-status');
 await status.waitFor();assert.match(await status.textContent(),/大屏状态尚未就绪.*自动复查/);cases.push('Actual Vue page displays the pending confirmation beneath the Decky toggle');
 await page.waitForFunction(()=>document.querySelector('.steam-state-card')?.disabled===false);assert.equal(await page.locator('.steam-state-card').isDisabled(),false);cases.push('A deferred Decky admission leaves Steam controls usable');
 const pending=path.join(out,'pending-580.png');await page.screenshot({path:pending});screenshots.push(pending);
 let revision=1;
 async function update(state){revision++;await page.evaluate(s=>{const next={...window.fixture.state,...s};window.fixture.state=next;window.dispatchEvent(new CustomEvent('ipc:deckySidebar.updated',{detail:next}))},{...state,revision});}
 await update({admissionRetry:true,admissionRecovery:true,admissionElapsedMs:20000,admissionNextRetryMs:30000});await page.waitForFunction(()=>document.querySelector('.decky-sidebar-status')?.textContent.includes('自动恢复'));cases.push('Slow admission recovery remains visible and does not request another toggle');
 await update({admissionRetry:false});await page.waitForFunction(()=>document.querySelector('.decky-sidebar-status')?.textContent.includes('仍未确认'));assert.match(await status.textContent(),/等待界面变化/);cases.push('Exhausted finite checks show the actual event/manual wait');
 await update({reason:'steam-bigpicture-required',admissionRetry:false,admissionDetail:'desktop'});await page.waitForFunction(()=>document.querySelector('.decky-sidebar-status')?.textContent.includes('桌面模式'));assert.match(await status.textContent(),/切换大屏后联动/);cases.push('Confirmed desktop mode has a distinct action description');
 await page.setViewportSize({width:480,height:820});await page.locator('[data-settings-section="integration"]').click();const fit=await status.evaluate(el=>({scroll:el.scrollWidth,client:el.clientWidth}));assert.ok(fit.scroll<=fit.client+1);const desktop=path.join(out,'desktop-480.png');await page.screenshot({path:desktop});screenshots.push(desktop);cases.push('Wrapped waiting text fits the 480px Steam page');
 await update({reason:'ymcc-restart-observation-only',admissionDetail:''});await page.waitForFunction(()=>document.querySelector('.decky-sidebar-status')?.textContent.includes('重开 YMCC'));assert.match(await status.textContent(),/模式按钮/);cases.push('Observation-only startup retains the explicit manual-link instruction');
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:deckySidebar.updated',{detail:{...window.fixture.state,revision:1,reason:'steam-ui-state-pending',admissionRetry:true}})));assert.match(await status.textContent(),/重开 YMCC/);cases.push('An older native revision cannot replace the current waiting reason');
 await update({phase:'waiting-steam',reason:''});await page.waitForFunction(()=>document.querySelector('.decky-sidebar-status')?.textContent.includes('等待 Steam 启动'));cases.push('Absent Steam is displayed independently of manual admission');
 await update({phase:'failed',reason:'launch-failed',error:5,launchStage:'token-start'});await page.waitForFunction(()=>document.querySelector('.decky-sidebar-status')?.textContent.includes('降权启动失败'));assert.match(await status.textContent(),/错误码 5.*token-start/);cases.push('A genuine loader failure exposes its Windows code and stage');
 await update({phase:'waiting-manual-steam',reason:'decky-context-already-loaded',pid:0});
 const repair=page.locator('[data-gp-id="steam-decky-repair"]');await repair.waitFor();assert.match(await status.innerText(),/修复连接/);
 assert.equal(await page.evaluate(()=>window.fixture.calls.filter(c=>c==='deckySidebar.repairConnection').length),0,'state event cannot auto-repair');
 await repair.click();await page.waitForFunction(()=>window.fixture.repairArgs);assert.deepEqual(await page.evaluate(()=>window.fixture.repairArgs),{userInitiated:true,pid:123,birth:'456',revision});cases.push('Actual repair button binds explicit consent and exact current Steam/runtime revision');
 await update({phase:'loader-started',reason:'mirror-handshake-pending',pid:1});assert.equal(await repair.count(),0);cases.push('Healthy connection removes the manual repair action');
await status.waitFor({state:'hidden'});cases.push('Completed admission removes obsolete waiting text');
 await update({phase:'waiting-manual-steam',pid:0,enabled:false,reason:''});await status.waitFor({state:'hidden'});cases.push('Disabled Decky does not retain an active-wait label');
 assert.deepEqual(errors,[]);const calls=await page.evaluate(()=>window.fixture.calls);assert.ok(calls.every(v=>['steam.uiState','deckySidebar.get','deckySidebar.repairConnection'].includes(v)));cases.push('Fixture uses read IPC plus one explicit inert repair; no real Steam or settings operations');
 const result={ok:true,cases:cases.length,results:cases,screenshots,pageErrors:errors,nativeCalls:0,realSteamActions:0,actualSettingsWrites:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(`Decky admission UI browser checks passed ${cases.length}`);
}finally{await browser?.close();await new Promise(r=>server.close(r))}
