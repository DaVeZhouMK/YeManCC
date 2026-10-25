// Production Vue controls + complete production gamepad engine, synthetic native actions.
// All native/storage/output dependencies are mocks: no installed app, controller or game I/O.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
import {createRequire} from 'node:module';import {createServer} from 'node:http';import {execFile} from 'node:child_process';import {promisify} from 'node:util';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_monitor-controller.cjs'))('playwright');
const out=path.resolve(process.env.YMCC_MONITOR_CONTROLLER_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cpu-robustness-pass-20261012/presets-browser'));
fs.mkdirSync(out,{recursive:true});
const shared=fs.readFileSync('tools/monitor_page_layout_browser_selftest.mjs','utf8'),start=shared.indexOf('const mocks={'),end=shared.indexOf('\n};',start)+3;
assert(start>=0&&end>start);const mocks=vm.runInNewContext('(function(){'+shared.slice(start,end)+'return mocks;})()');
mocks['@/bridge/yeman']+=';export const summonGet=async()=>({});';
// Exercise the native picker branch through the real gamepad engine and helper.
mocks['@/bridge/ipc']=mocks['@/bridge/ipc'].replace('isNativeRuntime=false','isNativeRuntime=true');
mocks['@/bridge/api'] = `export const windowApi={};export const dialog={openFile(filters){const f=window.__fixture;f.calls.push(['picture-dialog',filters]);return new Promise(resolve=>{f.resolvePicture=path=>{delete f.resolvePicture;resolve(path)}})}};export const shell={async run(program,args,timeoutMs){window.__fixture.calls.push(['picture-read']);const r=await fetch('/read-picture',{method:'POST',body:JSON.stringify({program,args,timeoutMs})});if(!r.ok)throw Error('Fixture image read rejected');return r.json()}};`;
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
import Dropdown from './src/components/Dropdown.vue';import Slider from './src/components/Slider.vue';import Toggle from './src/components/Toggle.vue';import SegButton from './src/components/SegButton.vue';import Icon from './src/components/InlineIcon.vue';import Hud from './src/components/MonitorHudTemplate.vue';import Preview from './src/components/MonitorHudPreview.vue';
import {focusGamepadElement} from './src/gamepad/focus.ts';import {startGamepad} from './src/gamepad/engine.ts';import {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';
import {registerYmccHudOutputAdapter} from './src/bridge/monitorOutput.ts';
import {spatialNavigationTarget} from './src/gamepad/spatial.ts';
window.__fixture={calls:[],backend:true,layout:'YeManOBS-W-1.ovl',zoom:5,customVisible:true,customReady:true,settings:{rtss:{monitorOutput:normalizeMonitorOutput({ymcc:{layout:'corner',underlayPreset:'none'}})}},steam:{ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,position:0,detail:4,scale:1,saturation:1,opacity:.86}};
registerYmccHudOutputAdapter({async available(){return true},async visible(){return window.__fixture.customVisible},async setVisible(value){window.__fixture.customVisible=value;return true;}});
for(const component of [Settings,Dropdown,Slider,Toggle,SegButton,Icon,Hud,Preview]){const style=document.createElement('style');style.textContent=component.__ymccHudCss||'';document.head.append(style)}
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
const fixtureImage=path.join(out,"手柄 图片 ' $safe.png");
const exec=promisify(execFile);
const server=createServer(async(request,response)=>{
 if(request.url==='/read-picture'){
  try{let body='';for await(const chunk of request)body+=chunk;const {program,args,timeoutMs}=JSON.parse(body);assert.equal(program,'powershell.exe');assert.deepEqual(args.slice(0,4),['-NoLogo','-NoProfile','-NonInteractive','-EncodedCommand']);assert.equal(timeoutMs,15000);const script=Buffer.from(args[4],'base64').toString('utf16le');assert(script.includes(Buffer.from(fixtureImage).toString('base64')),'only task fixture image may be read');assert(script.includes('[IO.File]::OpenRead'));const r=await exec(program,args,{timeout:15000,maxBuffer:8*1024*1024,windowsHide:true}).then(r=>({exitCode:0,...r}),e=>({exitCode:e.code||1,stdout:e.stdout||'',stderr:e.stderr||''}));response.setHeader('Content-Type','application/json');response.end(JSON.stringify(r));}
  catch(e){response.statusCode=500;response.end(String(e));}return;
 }
 response.setHeader('Content-Type',request.url==='/test.js'?'text/javascript':'text/html;charset=utf-8');response.end(request.url==='/test.js'?bundle.outputFiles[0].text:html);
});
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
  const crosshair=page.locator('.monitor-crosshair-choice .dd-trigger');assert.equal(await page.locator('[data-monitor-crosshair]').count(),0);
  for(const kind of ['dot','cross','gap','ring','ring-dot']){
    await crosshair.scrollIntoViewIfNeeded();await page.mouse.move(0,0);await crosshair.evaluate(el=>window.__focusElement(el));await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await send(page,'confirm');assert.equal(await page.getByRole('option').count(),6);await send(page,'nav-down');await send(page,'confirm');await page.waitForFunction(k=>window.__fixture.settings.rtss.monitorOutput.ymcc.crosshair===k,kind);assert.equal(await page.locator('[data-monitor-crosshair]').getAttribute('data-monitor-crosshair'),kind);
  }
  await crosshair.click();await page.getByRole('option',{name:'关闭',exact:true}).click();await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.crosshair==='none');assert.equal(await page.locator('[data-monitor-crosshair]').count(),0);await page.waitForFunction(()=>!document.querySelector('[role=option]'));cases.push({label:width+' crosshair dropdown: default off, all five styles through unified controller, live persistence and preview'});
  const fpsSwitch=page.getByRole('switch',{name:'FPS 突出显示',exact:true});assert.equal(await fpsSwitch.getAttribute('aria-checked'),'true');const beforeFocus=await page.evaluate(()=>structuredClone(window.__fixture.settings.rtss.monitorOutput.ymcc.fields));const bigFps=await page.locator('.hud-fps strong').evaluate(e=>parseFloat(getComputedStyle(e).fontSize));await fpsSwitch.click();await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.fpsEmphasis===false);assert.equal(await fpsSwitch.getAttribute('aria-checked'),'false');assert((await page.locator('.hud-fps strong').evaluate(e=>parseFloat(getComputedStyle(e).fontSize)))<bigFps);assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.fields),beforeFocus);
  await fpsSwitch.locator('button').evaluate(el=>window.__focusElement(el));await send(page,'confirm');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.fpsEmphasis===true);assert.equal(await fpsSwitch.getAttribute('aria-checked'),'true');cases.push({label:width+' FPS emphasis defaults on, mouse disables and unified gamepad re-enables, data fields unchanged'});
  assert.equal(await page.getByRole('switch',{name:'单色图片垫底',exact:true}).count(),0);
  const pictureDropdown=page.locator('.monitor-underlay-actions .dd-trigger');await pictureDropdown.evaluate(el=>window.__focusElement(el));await send(page,'confirm');assert.equal(await page.getByRole('option').count(),4);await send(page,'nav-down');await send(page,'nav-down');await send(page,'confirm');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='ac8-pink');assert.equal(await page.locator('.hud-image-underlay').count(),1);
  await pictureDropdown.evaluate(el=>window.__focusElement(el));await send(page,'confirm');await send(page,'nav-up');await send(page,'nav-up');await send(page,'confirm');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='none');assert.equal(await page.locator('.hud-image-underlay').count(),0);assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayPreset),'none');cases.push({label:width+' unified controller directly selects AC8 and none without an enable switch'});
  // Controller custom selection opens the Windows-picker IPC without browser
  // user activation. Keep it pending to verify repeat A cannot open two dialogs.
  const png=await page.evaluate(()=>{const c=document.createElement('canvas');c.width=48;c.height=64;const x=c.getContext('2d');x.fillStyle='#bf6688';x.fillRect(8,8,24,48);return c.toDataURL('image/png').split(',')[1]});fs.writeFileSync(fixtureImage,Buffer.from(png,'base64'));
  const dialogCount=()=>page.evaluate(()=>window.__fixture.calls.filter(c=>c[0]==='picture-dialog').length),readCount=()=>page.evaluate(()=>window.__fixture.calls.filter(c=>c[0]==='picture-read').length);
  await pictureDropdown.evaluate(el=>window.__focusElement(el));await send(page,'confirm');for(let step=0;step<3;step++)await send(page,'nav-down');await send(page,'confirm');await page.waitForFunction(()=>!!window.__fixture.resolvePicture);assert.equal(await dialogCount(),1);await send(page,'confirm');assert.equal(await dialogCount(),1);await page.evaluate(()=>window.__fixture.resolvePicture(null));await page.waitForFunction(()=>document.querySelector('.monitor-underlay-actions .action-btn')?.textContent==='选择图片');assert.equal(await readCount(),0);assert.equal(await page.locator('.hud-image-underlay').count(),0);
  await pictureDropdown.evaluate(el=>window.__focusElement(el));await send(page,'nav-right');assert.equal(await page.evaluate(()=>document.activeElement.textContent.trim()),'选择图片','right from image dropdown reaches picker');await send(page,'confirm');await page.waitForFunction(()=>!!window.__fixture.resolvePicture);assert.equal(await dialogCount(),2);await page.evaluate(p=>window.__fixture.resolvePicture(p),fixtureImage);await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='custom');const imported=await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayImage);assert(imported.startsWith('data:image/webp;base64,'));assert(imported.length<=45000);assert.equal(await readCount(),1);assert.equal(await page.locator('.hud-image-underlay').count(),1);
  const replaceButton=page.getByRole('button',{name:'选择图片',exact:true});await replaceButton.evaluate(el=>window.__focusElement(el));await send(page,'confirm');await page.waitForFunction(()=>!!window.__fixture.resolvePicture);assert.equal(await dialogCount(),3);await page.evaluate(()=>window.__fixture.resolvePicture(null));await page.waitForFunction(()=>document.querySelector('.monitor-underlay-actions .action-btn')?.textContent==='选择图片');assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayImage),imported);assert.equal(await readCount(),1);
  if(width===1920){
   // Above the native pipe cap: two bounded reads, then the existing alpha importer.
   const padded=Buffer.alloc(7*1024*1024);Buffer.from(png,'base64').copy(padded);fs.writeFileSync(fixtureImage,padded);const before=await readCount();await replaceButton.evaluate(el=>window.__focusElement(el));await send(page,'confirm');await page.waitForFunction(()=>!!window.__fixture.resolvePicture);await page.evaluate(p=>window.__fixture.resolvePicture(p),fixtureImage);await page.waitForFunction(()=>document.querySelector('.monitor-underlay-actions .action-btn')?.textContent==='选择图片');assert.equal(await readCount(),before+2);assert.equal(await page.locator('.monitor-underlay-error').count(),0);assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayImage),imported);cases.push({label:'7 MiB PNG reads in two bounded chunks below the native 8 MiB pipe cap; image normalization is unchanged'});
  }
  // An invalid replacement is reported while preserving the last working image.
  const readsBeforeInvalid=await readCount();fs.writeFileSync(fixtureImage,'invalid-image');await replaceButton.evaluate(el=>window.__focusElement(el));await send(page,'confirm');await page.waitForFunction(()=>!!window.__fixture.resolvePicture);await page.evaluate(p=>window.__fixture.resolvePicture(p),fixtureImage);await page.waitForFunction(()=>document.querySelector('.monitor-underlay-error')?.textContent.includes('PNG'));assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayImage),imported);assert.equal(await readCount(),readsBeforeInvalid+1);
  await pictureDropdown.evaluate(el=>window.__focusElement(el));await send(page,'confirm');for(let step=0;step<3;step++)await send(page,'nav-up');await send(page,'confirm');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='none');cases.push({label:width+' unified controller opens custom picker and replacement button; real bounded Windows file read with CJK/quote/dollar path; cancellation, duplicate A and invalid replacement preserve image'});
  const imageTransparency=page.locator('input[aria-label="图片透明度"]');await imageTransparency.evaluate(el=>window.__focusElement(el));const oldOpacity=await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayOpacity);await send(page,'confirm');await send(page,'nav-right');await send(page,'confirm');await page.waitForFunction(old=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayOpacity<old,oldOpacity);await send(page,'back');cases.push({label:width+' independent image transparency adjusted through unified controller'});
  for(const [name,count]of [['简约',21],['精简',3],['标准',24],['详细',40]]) {
    await page.getByRole('button',{name,exact:true}).click();
    await page.waitForFunction(n=>Object.values(window.__fixture.settings.rtss.monitorOutput.ymcc.fields).filter(Boolean).length===n,count);
    const fields=await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.fields);
    assert.equal(fields.cpuVoltage,name==='标准'||name==='详细');assert.equal(fields.gpuVoltage,name==='详细');
    assert(await page.getByRole('button',{name,exact:true}).evaluate(el=>el.classList.contains('active')));
    cases.push({label:width+' preset '+name,count});
  }
  await page.getByRole('button',{name:'标准',exact:true}).click();await page.waitForFunction(()=>Object.values(window.__fixture.settings.rtss.monitorOutput.ymcc.fields).filter(Boolean).length===24);
  await page.getByRole('tab',{name:/CPU/}).click();
  const voltage=page.getByRole('switch',{name:'CPU · 电压',exact:true});await voltage.click();
  await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.fields.cpuVoltage===false);
  assert(!(await page.getByRole('button',{name:'详细',exact:true}).evaluate(el=>el.classList.contains('active'))));
  cases.push({label:width+' individual override keeps custom choice'});
  const reset=page.getByRole('button',{name:'重置为预设',exact:true});assert.equal(await reset.count(),1);await reset.click();await page.waitForFunction(()=>Object.values(window.__fixture.settings.rtss.monitorOutput.ymcc.fields).filter(Boolean).length===24);assert(await page.getByRole('button',{name:'标准',exact:true}).evaluate(el=>el.classList.contains('active')));cases.push({label:width+' custom fields reset to screenshot standard'});
  await page.getByRole('button',{name:'标准',exact:true}).click();await page.waitForFunction(()=>Object.values(window.__fixture.settings.rtss.monitorOutput.ymcc.fields).filter(Boolean).length===24);

  await traverse(page,width+' corner');
  const toggle=page.locator('.monitor-output-head button');await toggle.focus();const toggleState=page.locator('.monitor-output-head [role=switch]');const prior=await toggleState.getAttribute('aria-checked');await send(page,'confirm');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));assert.notEqual(await toggleState.getAttribute('aria-checked'),prior);await send(page,'confirm');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));
  const scale=page.locator('input[aria-label="文字大小缩放"]');await scale.focus();const old=Number(await scale.inputValue());await send(page,'confirm');await send(page,'nav-right');await send(page,'confirm');assert(Number(await scale.inputValue())>old);await send(page,'back');
  const summary=page.locator('summary');await summary.focus();await send(page,'confirm');assert(await page.locator('details').evaluate(el=>el.open));await traverse(page,width+' expanded appearance');await summary.focus();await send(page,'confirm');assert(!await page.locator('details').evaluate(el=>el.open));
  assert.equal(await page.locator('[role=tab][tabindex="0"]').count(),7);
  const template=page.locator('.monitor-select-grid .dd-trigger').last();await template.scrollIntoViewIfNeeded();await template.evaluate(el=>window.__focusElement(el));
  // Flush the fixture's programmatic scroll before native A, as normal separate controller presses do.
  await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await send(page,'confirm');assert.equal(await template.getAttribute('aria-expanded'),'true');await send(page,'nav-down');await send(page,'confirm');await page.waitForSelector('.monitor-workbench--bar');await traverse(page,width+' bar');
  // Earlier pointer clicks leave a pointer over changing popup geometry. Park it
  // outside the monitor before exercising an exclusively controller-driven path.
  await page.mouse.move(0,0);
  const beforeStyles=await page.evaluate(()=>structuredClone(window.__fixture.settings.rtss.monitorOutput.ymcc.fields));
  for(const layout of ['capsule','card','dashboard','glass','split','cinema','neon','circuit','prism','holo','tactical']){
    const trigger=page.locator('.monitor-select-grid .dd-trigger').last();await trigger.scrollIntoViewIfNeeded();await trigger.evaluate(el=>window.__focusElement(el));
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await send(page,'confirm');assert.equal(await page.getByRole('option').count(),13);await page.waitForTimeout(50);await send(page,'nav-down');await page.waitForTimeout(50);await send(page,'confirm');
    await page.waitForFunction(l=>window.__fixture.settings.rtss.monitorOutput.ymcc.layout===l,layout,{timeout:3000}).catch(async e=>{console.error({width,target:layout,state:await page.evaluate(()=>({saved:window.__fixture.settings.rtss.monitorOutput.ymcc.layout,active:document.activeElement?.outerHTML,highlight:document.querySelector('[data-hl=true]')?.outerHTML}))});throw e});assert.deepEqual(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.fields),beforeStyles);
  }cases.push({label:width+' unified controller selects all thirteen templates in order, including scrolled dropdown options, without changing standard fields'});
  const menu=page.locator('.output-card .seg-btn');await menu.filter({hasText:'RTSS'}).focus();await send(page,'confirm');await page.waitForSelector('.rtss-output-card');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));await traverse(page,width+' RTSS');assert.equal(await page.locator('.rtss-output-card .seg-btn').count(),3);assert.equal(await page.locator('.preview-card').count(),0);
  assert.deepEqual(await menu.allTextContents(),['自定义监控','RTSS监控']);assert.equal(await page.getByLabel('监控承载',{exact:true}).count(),0);await menu.filter({hasText:'自定义监控'}).focus();await send(page,'confirm');await page.waitForSelector('.monitor-select-grid');await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));await traverse(page,width+' custom HUD');assert.equal(await page.locator('.preview-card').count(),1);assert.equal(await page.locator('.monitor-crosshair-choice').count(),1);assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.output),'ymcc');assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymccCarrier),'desktop');assert.equal(await page.getByRole('button',{name:'Steam全屏内置监控',exact:true}).count(),0);assert.equal(await page.getByRole('button',{name:'Steam 原生',exact:true}).count(),0);assert.equal(await page.getByLabel('性能详情等级').count(),0);
  await page.screenshot({path:path.join(out,width+'-steam-controller.png'),fullPage:true});await page.close();
 }
 assert.deepEqual(errors,[]);const result={ok:true,cases,errors,scope:'production renderer/gamepad native-event entry in an isolated browser with mocked dialogs/outputs and real task-only Windows image reads; not physical controller/battery/game acceptance'};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r));}
