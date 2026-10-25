// Production Vue controls + complete production gamepad engine, synthetic native actions.
// All native/storage/output dependencies are mocks: no installed app, controller or game I/O.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
import {createRequire} from 'node:module';import {createServer} from 'node:http';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_monitor-controller.cjs'))('playwright');
const out=path.resolve(process.env.YMCC_MONITOR_CONTROLLER_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/battery-generic-gamepad-20261011/browser'));
fs.mkdirSync(out,{recursive:true});
const shared=fs.readFileSync('tools/monitor_page_layout_browser_selftest.mjs','utf8'),start=shared.indexOf('const mocks={'),end=shared.indexOf('\n};',start)+3;
assert(start>=0&&end>start);const mocks=vm.runInNewContext('(function(){'+shared.slice(start,end)+'return mocks;})()');
mocks['@/bridge/yeman']+=';export const summonGet=async()=>({});';
Object.assign(mocks,{
 '@/router': 'export const ROUTES=[{path:"/monitor",title:"监控"}];',
 '@/bridge/performanceSchedule':'export const loadPerformanceSchedule=async()=>({enabled:false});',
 '@/bridge/uiLifecycle':'export const isUiVisible=()=>true;export const onUiVisibilityChange=()=>()=>{};export const getUiVisibilityState=()=>({visible:true,reason:"fixture"});',
 '@/robust/frontendDiagnostics':'export const reportFrontendTrace=()=>{};',
 '@/bridge/residentNavigation':'export const residentNavigationRoutes=routes=>routes;',
});
const entry=String.raw`
import {createApp,nextTick} from 'vue';
import Settings from './src/components/MonitorOutputSettings.vue';
import Dropdown from './src/components/Dropdown.vue';import Slider from './src/components/Slider.vue';import Toggle from './src/components/Toggle.vue';import SegButton from './src/components/SegButton.vue';import Icon from './src/components/InlineIcon.vue';import Hud from './src/components/MonitorHudTemplate.vue';
import {focusGamepadElement} from './src/gamepad/focus.ts';import {startGamepad} from './src/gamepad/engine.ts';import {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';
import {registerYmccHudOutputAdapter} from './src/bridge/monitorOutput.ts';
import {spatialNavigationTarget} from './src/gamepad/spatial.ts';
window.__fixture={calls:[],backend:true,layout:'YeManOBS-W-1.ovl',zoom:5,customVisible:true,customReady:true,settings:{rtss:{monitorOutput:normalizeMonitorOutput()}},steam:{ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,position:0,detail:4,scale:1,saturation:1,opacity:.86}};
registerYmccHudOutputAdapter({async available(){return true},async visible(){return window.__fixture.customVisible},async setVisible(value){window.__fixture.customVisible=value;return true;}});
for(const component of [Settings,Dropdown,Slider,Toggle,SegButton,Icon,Hud]){const style=document.createElement('style');style.textContent=component.__ymccHudCss||'';document.head.append(style)}
createApp(Settings,{previewOnly:true}).mount('#app');
startGamepad({router:{currentRoute:{value:{path:'/monitor'}},push:async()=>{}},onAction:()=>{}});
let seq=0;
window.__send=async action=>{window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action,uiSeq:++seq,provenance:{session:'monitor-offline-test'}}}));await new Promise(r=>setTimeout(r,3));await nextTick();window.__controls();return {id:document.activeElement?.dataset?.testNav||null,group:window.__group()};};
const selector='button:not(:disabled):not([tabindex="-1"]),input:not(:disabled):not([tabindex="-1"]),[tabindex]:not([tabindex="-1"])';let id=0;
window.__controls=()=>[...document.querySelectorAll('#app '+selector)].filter(el=>{const r=el.getBoundingClientRect(),s=getComputedStyle(el);return el.checkVisibility()&&r.width>0&&r.height>0&&s.visibility!=='hidden'&&s.display!=='none';}).map(el=>{if(!el.dataset.testNav)el.dataset.testNav='nav-'+(++id);return {id:el.dataset.testNav,label:el.getAttribute('aria-label')||el.textContent.trim(),panel:el.closest('[data-field-group]')?.dataset.fieldGroup||null,rect:el.getBoundingClientRect().toJSON()}});
window.__group=()=>document.querySelector('[role=tab][aria-selected="true"] span')?.textContent.trim()||null;
window.__focus=async state=>{const el=document.querySelector('[data-test-nav="'+state.id+'"]');if(state.group){const tab=[...document.querySelectorAll('[role=tab]')].find(t=>t.querySelector('span')?.textContent.trim()===state.group);tab?.click();await nextTick();}focusGamepadElement(el);await nextTick();};window.__focusElement=focusGamepadElement;
window.__spatial=spatialNavigationTarget;
`;
const bundle=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'isolated-native-output-mocks',setup(b){
 b.onResolve({filter:/.*/},args=>{let key=args.path;if(key.startsWith('./')&&args.importer.replaceAll('\\','/').includes('/src/bridge/'))key='@/bridge/'+key.slice(2).replace(/\.ts$/,'');return mocks[key]?{path:key,namespace:'mock'}:null;});
 b.onLoad({filter:/.*/,namespace:'mock'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
}},monitorHudVuePlugin(root)]});
const css=fs.readFileSync('src/styles/tokens.css','utf8')+'\nhtml,body{margin:0;background:#17212d}#app{padding:14px;box-sizing:border-box;max-width:100%}';
const html='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>'+css+'</style><div id="app"></div><script src="/test.js"></script></html>';
fs.writeFileSync(path.join(out,'index.html'),html);fs.writeFileSync(path.join(out,'test.js'),bundle.outputFiles[0].text);
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/test.js'?'text/javascript':'text/html;charset=utf-8');response.end(request.url==='/test.js'?bundle.outputFiles[0].text:html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;const cases=[],errors=[];
async function controls(page){return page.evaluate(()=>__controls())}
async function send(page,action){return page.evaluate(a=>__send(a),action)}
async function focus(page,id){await page.evaluate(state=>__focus(state),typeof id==='string'?{id}:id)}
async function traverse(page,label){
 const list=await controls(page),initial={id:list[0].id,group:await page.evaluate(()=>__group())};
 const queue=[initial],key=state=>state.id+'|'+state.group,states=new Set([key(initial)]),seen=new Set([initial.id]);let edges=0;
 for(let i=0;i<queue.length;i++){
  await focus(page,queue[i]);const isTab=await page.evaluate(()=>document.activeElement?.getAttribute('role')==='tab');
  for(const action of ['nav-up','nav-down','nav-left','nav-right',...(isTab?['confirm']:[])]){
   await focus(page,queue[i]);await controls(page);const next=await send(page,action);++edges;
   if(next.id){seen.add(next.id);if(!states.has(key(next))){states.add(key(next));queue.push(next);}}
  }
 }
 const tabs=await page.locator('[role=tab]').count();let required=[];
 for(let i=0;i<Math.max(1,tabs);i++){
  if(tabs){await page.locator('[role=tab]').nth(i).focus();await send(page,'confirm');}required.push(...await controls(page));
 }
 const missing=[...new Map(required.filter(c=>!seen.has(c.id)).map(c=>[c.id,c.label])).values()];
 if(missing.length){fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify({label,seen:[...seen],required,missing},null,2));await page.screenshot({path:path.join(out,'failure.png'),fullPage:true});}
 assert.deepEqual(missing,[],label+': all controls reachable through production native gamepad actions');
 cases.push({label,reachable:seen.size,states:states.size,edges});
}
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(const width of [1920,1280,580]){
  const page=await browser.newPage({viewport:{width,height:width===1280?720:width===580?780:1080}});page.on('pageerror',e=>errors.push(String(e)));
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__send&&!document.querySelector('.output-card button:disabled'));
  await traverse(page,width+' corner');
  const toggle=page.locator('.monitor-output-head button');await toggle.focus();const toggleState=page.locator('.monitor-output-head [role=switch]');const prior=await toggleState.getAttribute('aria-checked');await send(page,'confirm');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));assert.notEqual(await toggleState.getAttribute('aria-checked'),prior);await send(page,'confirm');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));
  const scale=page.locator('input[aria-label="文字大小缩放"]');await scale.focus();const old=Number(await scale.inputValue());await send(page,'confirm');await send(page,'nav-right');await send(page,'confirm');assert(Number(await scale.inputValue())>old);await send(page,'back');
  const summary=page.locator('summary');await summary.focus();await send(page,'confirm');assert(await page.locator('details').evaluate(el=>el.open));await traverse(page,width+' expanded appearance');await summary.focus();await send(page,'confirm');assert(!await page.locator('details').evaluate(el=>el.open));
  assert.equal(await page.locator('[role=tab][tabindex="0"]').count(),7);
  const template=page.locator('.monitor-select-grid .dd-trigger').last();await template.scrollIntoViewIfNeeded();await template.evaluate(el=>window.__focusElement(el));
  // Flush the fixture's programmatic scroll before native A, as normal separate controller presses do.
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await send(page,'confirm');assert.equal(await template.getAttribute('aria-expanded'),'true');await send(page,'nav-down');await send(page,'confirm');await page.waitForSelector('.monitor-workbench--bar');await traverse(page,width+' bar');
  const menu=page.locator('.output-card .seg-btn');await menu.filter({hasText:'RTSS'}).focus();await send(page,'confirm');await page.waitForSelector('.rtss-output-card');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));await traverse(page,width+' RTSS');assert.equal(await page.locator('.rtss-output-card .seg-btn').count(),3);assert.equal(await page.locator('.preview-card').count(),0);
  await menu.filter({hasText:'Steam 原生'}).focus();await send(page,'confirm');await page.waitForSelector('.monitor-select-grid');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));await traverse(page,width+' Steam');assert.equal(await page.locator('.preview-card').count(),0);
  await page.screenshot({path:path.join(out,width+'-steam-controller.png'),fullPage:true});await page.close();
 }
 assert.deepEqual(errors,[]);const result={ok:true,cases,errors,scope:'production renderer/gamepad native-event entry in an isolated browser with mocked outputs; not physical controller/battery/game acceptance'};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r));}
