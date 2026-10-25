/** Real App shell scaling and production monitor controls. Offline mocks only. */
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {createRequire} from 'node:module';import {fileURLToPath} from 'node:url';import {createServer} from 'node:http';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_monitor-page-layout.cjs'))('playwright');
const out=path.resolve(process.env.YMCC_MONITOR_PAGE_LAYOUT_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-page-size-20261011/browser'));fs.mkdirSync(out,{recursive:true});
const layoutBaseline=process.env.YMCC_MONITOR_LAYOUT_BASELINE||'';
const readModule=file=>{const prior=layoutBaseline&&path.join(layoutBaseline,path.relative(root,file));return fs.readFileSync(prior&&fs.existsSync(prior)?prior:file,'utf8')};
const source=readModule(path.join(root,'src/App.vue'));
const scale=source.slice(source.indexOf('const layoutBaseWidth ='),source.indexOf('// M8: Start button'));
const lifecycle=source.slice(source.indexOf('let stageResizeObserver:'),source.indexOf('</script>'));
assert(scale.includes('w / YMCC_SHELL_BASE_WIDTH'));assert(!scale.includes('w / layoutBaseWidth.value'));
const id='data-v-monitor-page-layout',styles=[fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')];
for(const style of parse(source).descriptor.styles){const result=compileStyle({source:style.content,id,scoped:style.scoped});assert.deepEqual(result.errors,[]);styles.push(result.code);}
const mocks={
 '@/bridge/monitorData':`export function subscribeMonitorTdpState(cb){cb({mode:null,status:'unknown',watts:null,proof:null,revision:0,confirmedAt:null});return ()=>{}}export function subscribeMonitorSnapshot(cb){cb(null);return ()=>{}}export function acquireMonitorDemand(){return ()=>{}}export async function readMonitorSnapshot(){return null}`,
 '@/bridge/api':`export const dialog={async openFile(){throw Error('Unexpected native picker in browser fixture')}};export const shell={async run(){throw Error('Unexpected native file read in browser fixture')}};export const windowApi={async createChild(){throw Error('Native window unavailable in offline browser')},async closeChild(){return true}};`,
 '@/bridge/settingsRepository':`export async function readSettingsSection(section){return structuredClone(window.__fixture.settings[section]||{})}export async function saveSettingsSection(section,patch){const f=window.__fixture;if(f.failSave)throw Error('fixture disk rejection');f.settings[section]={...f.settings[section],...structuredClone(patch)};f.calls.push(['prefs',structuredClone(patch)]);}`,
 '@/bridge/yeman':`const f=()=>window.__fixture;export async function rtssRunning(){return f().backend}export async function toggleRtss(v){if(!v)throw Error('UI must not stop RTSS');f().backend=v;f().calls.push(['start-rtss',v])}export async function readOverlayLayout(){return f().layout}export async function setOverlayLayout(v){f().calls.push(['rtss-layout',v]);if(f().failRtss)return {mode:'saved'};f().layout=v==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[v];return {mode:'live'}}export async function readRtssZoom(){return f().zoom}export async function setRtssZoom(v){f().zoom=v;f().calls.push(['rtss-zoom',v])}`,
 '@/bridge/ipc':`export const isNativeRuntime=false;export async function invoke(command,args){const f=window.__fixture;if(command==='steam.settings.get')return structuredClone(f.steam);if(command==='steam.settings.set'){f.calls.push(['steam',structuredClone(args.monitor)]);if(f.failSteam)return {...f.steam,ok:false,pending:false,reason:'write-failed'};Object.assign(f.steam,args.monitor);f.steam.pending=f.deferSteam;f.steam.via=f.deferSteam?'deferred':'live';f.steam.liveConfirmed=!f.deferSteam;f.steam.applicationConfirmed=!f.deferSteam;return structuredClone(f.steam)}throw Error('unexpected fixture IPC '+command)}export function on(){return ()=>{}}`,
};

const fixture=`<script setup lang="ts">
import {ref,computed,onMounted,onUnmounted,nextTick} from 'vue';
import Settings from './src/components/MonitorOutputSettings.vue';
import {ymccDesignWidth,ymccStageWidth,YMCC_SHELL_BASE_WIDTH,YMCC_SHELL_BASE_HEIGHT,YMCC_SHELL_SCALE_BOOST} from './src/bridge/monitorLayout';
const router={currentRoute:ref({path:'/'})},isStandaloneEditor=ref(false);
${scale}
${lifecycle}
window.__monitorPage={async navigate(path){router.currentRoute.value={path};await nextTick();},snapshot(){const nav=document.querySelector('[data-layout-nav]'),r=nav.getBoundingClientRect();return {scale:uiScale.value,stage:scalerStyle.value,navWidth:r.width,navFont:getComputedStyle(nav).fontSize,cssScale:document.documentElement.style.getPropertyValue('--ui-scale')};}};
</script><template><div class="app-root"><div class="app-backdrop"/><div class="app-stage" :style="scalerStyle"><div class="app-body"><aside data-layout-nav style="flex:0 0 105px;font-size:13px;padding:12px"><b>YMCC</b><p>性能调度</p><p>监控</p><p>设置</p></aside><main class="app-main"><div style="height:52px;font-size:13px;padding:12px">CPU / GPU · 离线布局测试</div><div class="app-content"><div class="page"><Settings v-if="router.currentRoute.value.path==='/rtss'" :preview-only="true"/><div v-else class="card">其他页面 / 不改变整体比例</div></div></div></main></div></div></div></template>`;
const descriptor=parse(fixture).descriptor;
const fixtureScript=compileScript(descriptor,{id,inlineTemplate:true}).content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`;
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:`import {createApp} from 'vue';import Shell from 'monitor-page-shell';import {normalizeMonitorOutput} from './src/bridge/monitorHudModel';import {registerYmccHudOutputAdapter} from './src/bridge/monitorOutput';
window.__fixture={calls:[],backend:true,layout:'YeManOBS-W-1.ovl',zoom:5,customVisible:false,customReady:true,settings:{rtss:{monitorOutput:normalizeMonitorOutput()}},steam:{ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,position:0,detail:4,scale:1,saturation:1,opacity:.86}};
registerYmccHudOutputAdapter({async available(){return true},async visible(){return false},async setVisible(){throw Error('Layout test must not activate output')}});createApp(Shell).mount('#app');`},bundle:true,platform:'browser',format:'iife',write:false,define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'offline-layout',setup(b){
 b.onResolve({filter:/^monitor-page-shell$/},()=>({path:'shell',namespace:'shell'}));b.onLoad({filter:/.*/,namespace:'shell'},()=>({contents:fixtureScript,loader:'ts',resolveDir:root}));
 b.onResolve({filter:/.*/},args=>{let key=args.path;if(key.startsWith('./')&&args.importer.replaceAll('\\','/').includes('/src/bridge/'))key='@/bridge/'+key.slice(2).replace(/\.ts$/,'');return mocks[key]?{path:key,namespace:'mock'}:null;});b.onLoad({filter:/.*/,namespace:'mock'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
 b.onResolve({filter:/^@\//},args=>({path:path.join(root,'src',args.path.slice(2))+(path.extname(args.path)?'':'.ts')}));
 b.onLoad({filter:/\.vue$/},args=>{const {descriptor}=parse(readModule(args.path),{filename:args.path}),sid='data-v-monitor-'+Buffer.from(path.relative(root,args.path)).toString('hex');for(const style of descriptor.styles){const r=compileStyle({source:style.content,filename:args.path,id:sid,scoped:style.scoped});assert.deepEqual(r.errors,[]);styles.push(r.code);}return {contents:compileScript(descriptor,{id:sid,inlineTemplate:true}).content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(sid)};export default component;`,loader:'ts',resolveDir:path.dirname(args.path)};});
}}]});
assert(!result.outputFiles[0].text.includes('RTSS-start.ps1'));
const html='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><style>'+styles.join('\n')+'</style><div id="app"></div><script src="/fixture.js"></script></html>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html;charset=utf-8');response.end(request.url==='/fixture.js'?result.outputFiles[0].text:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));let browser;const cases=[],errors=[],measurements=[];
async function inspect(page,label){const m=await page.evaluate(()=>{const content=document.querySelector('.app-content'),root=document.querySelector('.monitor-output-settings'),r=root.getBoundingClientRect();const overflow=[...root.querySelectorAll('button,input,.dd-trigger,.monitor-field-tab span')].filter(el=>!el.closest('.monitor-preview-stage')).map(el=>({text:el.textContent?.trim(),r:el.getBoundingClientRect()})).filter(x=>x.r.width>0&&(x.r.left<r.left-1||x.r.right>r.right+1)).map(x=>x.text);return {contentWidth:content.clientWidth,contentScrollWidth:content.scrollWidth,outputWidth:r.width,overflow,stage:__monitorPage.snapshot(),previewScrollWidth:document.querySelector('.monitor-preview-stage')?.scrollWidth};});assert(m.contentScrollWidth<=m.contentWidth+1,label+': no page-wide horizontal overflow');assert.deepEqual(m.overflow,[],label+': controls remain in content area');measurements.push({label,...m});return m;}
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(const dpr of [1,1.5,2])for(const [width,height] of [[580,780],[800,1080],[1280,720],[1920,1080]]){
  const context=await browser.newContext({viewport:{width,height},deviceScaleFactor:dpr}),page=await context.newPage();page.on('pageerror',e=>errors.push(String(e)));await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__monitorPage);const home=await page.evaluate(()=>__monitorPage.snapshot());await page.evaluate(()=>__monitorPage.navigate('/rtss'));await page.waitForFunction(()=>document.querySelector('.monitor-output-settings')&&!document.querySelector('.output-card button:disabled'));
  const monitor=await page.evaluate(()=>__monitorPage.snapshot());assert.equal(monitor.scale,home.scale);assert.equal(monitor.cssScale,home.cssScale);assert(Math.abs(monitor.navWidth-home.navWidth)<.1);assert.equal(monitor.navFont,home.navFont);
  assert.equal(await page.locator('.preview-card h3').textContent(),'监控预览');
  assert.equal(await page.locator('.preview-card button,.preview-card .monitor-example-badge,.preview-card .monitor-window-hint,.preview-card .monitor-note').count(),0,'Preview is title and render only');
  // The preview intentionally stays hidden until ResizeObserver/rAF applies fit.
  // Wait for that real readiness state, then keep the strict bounds assertions.
  await page.locator('.monitor-preview-content').waitFor({state:'visible'});
  const previewSize=await page.evaluate(()=>{const anchor=document.querySelector('.monitor-preview-content'),viewport=document.querySelector('.monitor-preview-viewport'),hud=anchor.querySelector('.ymcc-hud');const h=hud.getBoundingClientRect(),v=viewport.getBoundingClientRect();return {hud:{l:h.left,t:h.top,r:h.right,b:h.bottom},viewport:{l:v.left,t:v.top,r:v.right,b:v.bottom},visibility:getComputedStyle(anchor).visibility,fit:new DOMMatrix(getComputedStyle(anchor).transform).a,outputZoom:Number(getComputedStyle(hud).zoom)};});
  assert(previewSize.fit>0&&previewSize.fit<=.8);assert(previewSize.hud.l>=previewSize.viewport.l-1&&previewSize.hud.r<=previewSize.viewport.r+1&&previewSize.hud.t>=previewSize.viewport.t-1&&previewSize.hud.b<=previewSize.viewport.b+1,'Preview auto-fits entirely inside viewport without changing output zoom '+JSON.stringify({width,height,dpr,previewSize}));
  await inspect(page,`${width}x${height} DPR ${dpr} corner`);assert.equal(await page.locator('[role=tab]').count(),7);
  if(dpr===1)await page.screenshot({path:path.join(out,`${width}x${height}-corner.png`)});
  let fieldCount=0;
  for(let group=0;group<7;group++){await page.locator('[role=tab]').nth(group).click();await inspect(page,`${width}x${height} DPR ${dpr} field group ${group}`);fieldCount+=await page.locator('[data-field]:visible').count();}
  assert.equal(fieldCount,41,'All independent metric controls remain reachable');
  await page.locator('[role=tab]').nth(1).click();
  if(dpr===1){await page.locator('.fields-card').scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,`${width}x${height}-fields.png`)});}
  await page.locator('.monitor-select-grid .dd-trigger').nth(1).click();await page.locator('.dd-option').filter({hasText:'横向信息条'}).click();await page.waitForSelector('.monitor-workbench--bar');await page.locator('.dd-menu').waitFor({state:'hidden'});await inspect(page,`${width}x${height} DPR ${dpr} bar`);assert.equal((await page.evaluate(()=>__monitorPage.snapshot())).scale,home.scale);
  if(dpr===1)await page.screenshot({path:path.join(out,`${width}x${height}-bar.png`)});
  await page.locator('.output-card .seg-btn').filter({hasText:'RTSS'}).click();await page.waitForSelector('.rtss-output-card');await inspect(page,`${width}x${height} DPR ${dpr} RTSS`);assert.equal(await page.locator('.preview-card').count(),0);
  await page.evaluate(()=>__monitorPage.navigate('/'));assert.deepEqual(await page.evaluate(()=>__monitorPage.snapshot()),home);cases.push(`${width}x${height} DPR ${dpr}: unchanged shell/nav scale, compact corner/bar/RTSS controls reachable without page overflow`);await context.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({ok:true,cases,measurements,errors,scope:'Production shell scaling and monitor controls, explicit offline example data; no installed product or game acceptance.'},null,2));console.log(JSON.stringify({ok:true,cases:cases.length,measurements:measurements.length,out},null,2));
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}