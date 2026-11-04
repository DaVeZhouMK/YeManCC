/** Production App scale/lifecycle + Dropdown in Chromium; no hardware or live product IO. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {createServer} from 'node:http';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_window-layout-test.cjs'))('playwright');
const out=path.resolve(process.env.YMCC_WINDOW_LAYOUT_OUT||path.join(root,'Build/Validation/WindowLayoutBrowser'));fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(path.join(root,'src/App.vue'),'utf8');
const scale=source.slice(source.indexOf('const layoutBaseWidth ='),source.indexOf('function onGamepadRefresh()'));
const lifecycle=source.slice(source.indexOf('let stageResizeObserver:'),source.indexOf('</script>'));
assert(scale.includes('function viewportSize()'));assert(!scale.includes('</script>'),'App scale extraction boundary missing');assert(lifecycle.includes('onNativeResize'));
const scope='data-v-window-layout-test',styles=[fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')];
const {descriptor:app}=parse(source,{filename:'App.vue'});
for(const style of app.styles){const result=compileStyle({source:style.content,filename:'App.vue',id:scope,scoped:style.scoped});assert.deepEqual(result.errors,[]);styles.push(result.code);}
const fixture=`<script setup lang="ts">
import {ref,computed,watch,onMounted,onUnmounted,nextTick} from 'vue';
import Dropdown from './src/components/Dropdown.vue';
import {resetInnerPageViewport} from './src/bridge/pageViewport';
import {ymccDesignWidth,ymccStageWidth,YMCC_SHELL_BASE_WIDTH,YMCC_SHELL_BASE_HEIGHT,YMCC_SHELL_SCALE_BOOST} from './src/bridge/monitorLayout';
const router={currentRoute:ref({path:'/'})};
const isStandaloneEditor=ref(false);
${scale}
${lifecycle}
window.__fixture={async setRoute(path){router.currentRoute.value={path};await nextTick();},setStandalone(value){isStandaloneEditor.value=value;updateScale();},flush:nextTick,snapshot(){return {native:nativeViewport.value,nativeFrames:nativeViewportFrames,settleFrames,viewport:viewportSize(),scale:uiScale.value,stage:scalerStyle.value,cssScale:Number(getComputedStyle(document.documentElement).getPropertyValue('--ui-scale'))};}};
</script><template><div class="app-root"><div class="user-background-image" style="background-image:linear-gradient(#234,#456)"/><div class="app-backdrop"/><div class="app-stage" :style="scalerStyle"><div class="app-body"><main class="app-main"><div style="height:52px">缩放恢复</div><div class="app-content"><div style="padding:24px;width:280px;max-width:100%"><Dropdown model-value="a" :options="[{value:'a',label:'布局测试'},{value:'b',label:'第二项'}]"/></div><div style="height:1400px"></div><button id="layout-bottom">末尾控件</button></div></main></div></div></div></template>`;
const {descriptor}=parse(fixture,{filename:'window-layout-fixture.vue'});
const fixtureScript=compileScript(descriptor,{id:scope,inlineTemplate:true}).content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(scope)};export default component;`;
const bundle=await build({stdin:{contents:"import {createApp} from 'vue';import Component from 'window-layout-fixture';window.__app=createApp(Component);window.__app.mount('#app');",loader:'js',resolveDir:root},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},alias:{'@':path.join(root,'src')},plugins:[{name:'layout-fixture',setup(b){
 b.onResolve({filter:/^window-layout-fixture$/},()=>({path:'fixture',namespace:'fixture'}));
 b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:fixtureScript,loader:'ts',resolveDir:root}));
 b.onLoad({filter:/\.vue$/},args=>{const {descriptor}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});const id='data-v-layout-dropdown';for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);styles.push(compiled.code);}let script=compileScript(descriptor,{id,inlineTemplate:true}).content.replace('export default','const component=');return {contents:script+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(args.path)};});
}}]});
const html='<!doctype html><meta charset="utf-8"><style>'+styles.join('\n')+'</style><div id="app"></div><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html');response.end(request.url==='/fixture.js'?bundle.outputFiles[0].text:html);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const checks=[],errors=[],observations=[];let browser,activePage;
const snapshot=page=>page.evaluate(()=>window.__fixture.snapshot());
const nativeResize=(page,w,h)=>page.evaluate(({w,h})=>window.dispatchEvent(new CustomEvent('ipc:window.resized',{detail:{w,h}})),{w,h});
async function menuFont(page){await page.locator('.dd-trigger').click();await page.locator('.dd-menu').waitFor();const font=await page.locator('.dd-option').first().evaluate(el=>Number.parseFloat(getComputedStyle(el).fontSize));await page.keyboard.press('Escape');return font;}
async function coverage(page){const rects=await page.evaluate(()=>{const rect=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};return {viewport:{width:innerWidth,height:innerHeight},image:rect('.user-background-image'),backdrop:rect('.app-backdrop'),stage:rect('.app-stage')};});for(const layer of ['image','backdrop']){assert(Math.abs(rects[layer].width-rects.viewport.width)<1,`${layer} width`);assert(Math.abs(rects[layer].height-rects.viewport.height)<1,`${layer} height`);}observations.push(rects);}
async function settled(page) {
 await page.waitForFunction(()=>__fixture.snapshot().settleFrames===0);
 await page.evaluate(()=>__fixture.flush());
}
async function fits(page,label) {
 const r=await page.evaluate(()=>{const rect=s=>{const b=document.querySelector(s).getBoundingClientRect();return {x:b.x,y:b.y,right:b.right,bottom:b.bottom,width:b.width,height:b.height}};return{viewport:{w:innerWidth,h:innerHeight},stage:rect('.app-stage'),content:rect('.app-content'),snapshot:__fixture.snapshot(),css:[...document.querySelectorAll('.app-stage,.app-body,.app-main,.app-content')].map(e=>({tag:e.outerHTML.slice(0,220),height:getComputedStyle(e).height,display:getComputedStyle(e).display,flex:getComputedStyle(e).flex,overflow:getComputedStyle(e).overflow}))}});
 observations.push({label,...r});
 assert(r.stage.x>=-1&&r.stage.y>=-1&&r.stage.right<=r.viewport.w+1&&r.stage.bottom<=r.viewport.h+1,label+' stage inside viewport');
 assert(r.content.bottom<=r.viewport.h+1,label+' scroll area inside viewport');
 await page.locator('#layout-bottom').evaluate(el=>el.scrollIntoView({block:'end',behavior:'instant'}));
 const b=await page.locator('#layout-bottom').boundingBox();assert(b.y>=0&&b.y+b.height<=r.viewport.h+1,label+' bottom control reachable');
 await page.locator('.app-content').evaluate(el=>{el.scrollTop=0});
}
async function checkMenuBounds(page,label) {
 await page.locator('.dd-trigger').click();await page.locator('.dd-menu').waitFor();
 const r=await page.locator('.dd-menu').boundingBox(),v=page.viewportSize();
 assert(r.x>=-1&&r.y>=-1&&r.x+r.width<=v.width+1&&r.y+r.height<=v.height+1,label+' popup bounds');
 await page.keyboard.press('Escape');
}
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(const dpr of [1,1.5,2]){
  const context=await browser.newContext({viewport:{width:640,height:720},deviceScaleFactor:dpr});const page=await context.newPage();activePage=page;
  page.on('pageerror',error=>errors.push(String(error)));page.on('console',m=>{if(m.type()==='error')errors.push(m.text())});
  await page.addInitScript(()=>{window.__scaleWrites=0;window.__scaleRaf=0;const set=CSSStyleDeclaration.prototype.setProperty;CSSStyleDeclaration.prototype.setProperty=function(name,...args){if(name==='--ui-scale')window.__scaleWrites++;return set.call(this,name,...args);};const raf=requestAnimationFrame.bind(window);window.requestAnimationFrame=fn=>raf(t=>{window.__scaleRaf++;fn(t);});});
  await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__fixture);
  await settled(page);
  const stable=await page.evaluate(()=>({writes:__scaleWrites,frames:__scaleRaf}));await page.evaluate(()=>dispatchEvent(new CustomEvent('ipc:window.summoned')));await settled(page);
  const refreshed=await page.evaluate(()=>({writes:__scaleWrites,frames:__scaleRaf}));assert.equal(refreshed.writes,stable.writes);assert(refreshed.frames-stable.frames<=10,'stable summon still runs 90 frames');
  await page.waitForTimeout(120);assert.deepEqual(await page.evaluate(()=>({writes:__scaleWrites,frames:__scaleRaf})),refreshed);
  checks.push('DPR '+dpr+': stable summon settles early with no repeated zoom writes or idle scale frames');
  await page.evaluate(()=>__fixture.setStandalone(true));
  const before=await snapshot(page),fontBefore=await menuFont(page);await nativeResize(page,1071,1440);await settled(page);
  const after=await snapshot(page),fontAfter=await menuFont(page);assert.equal(after.native,null);assert.equal(after.cssScale,before.cssScale);assert.equal(after.stage.zoom,1);assert.equal(fontAfter,fontBefore);
  checks.push('DPR '+dpr+': standalone ignores main native sizes');
  await page.setViewportSize({width:480,height:540});await page.waitForFunction(()=>Math.abs(__fixture.snapshot().cssScale-480/580)<1e-8);
  const small=await snapshot(page);assert.equal(small.viewport.w,480);assert.equal(small.viewport.h,540);assert(Math.abs(await menuFont(page)-14*480/580)<.01);
  await coverage(page);checks.push('DPR '+dpr+': standalone follows its own DOM and popup scale');
  await page.evaluate(()=>__fixture.setStandalone(false));
  const immediate=await page.evaluate(dpr=>{dispatchEvent(new CustomEvent('ipc:window.resized',{detail:{w:800*dpr,h:1080*dpr}}));return __fixture.snapshot()},dpr);
  assert.equal(immediate.viewport.w,800);assert.equal(immediate.viewport.h,1080);assert(immediate.nativeFrames>0);
  await settled(page);const expired=await snapshot(page);assert.equal(expired.viewport.w,480);assert.equal(expired.viewport.h,540);assert.equal(expired.nativeFrames,0);
  await fits(page,'DPR '+dpr+' stale native expires');checks.push('DPR '+dpr+': native bridges restoration then expires against current DOM');
  for(const [width,height] of [[580,780],[800,1080],[900,780],[1280,720],[1920,1080]]){
   await page.setViewportSize({width,height});await nativeResize(page,Math.round(width*dpr),Math.round(height*dpr));await settled(page);
   await page.evaluate(()=>__fixture.setRoute('/'));const home=await snapshot(page),homeFont=await menuFont(page);
   await page.evaluate(()=>__fixture.setRoute('/rtss'));const monitor=await snapshot(page),monitorFont=await menuFont(page);
   assert.equal(monitor.stage.zoom,home.stage.zoom);assert.equal(monitor.cssScale,home.cssScale);assert.equal(monitorFont,homeFont);assert(Number.parseFloat(monitor.stage.width)*monitor.stage.zoom<=width+1);
   await page.evaluate(()=>__fixture.setRoute('/'));await fits(page,'DPR '+dpr+' '+width+'x'+height);await checkMenuBounds(page,'DPR '+dpr);
   checks.push('DPR '+dpr+', '+width+'x'+height+': route scale stable, scroll end and popup reachable');
  }
  await page.evaluate(()=>__app.unmount());await nativeResize(page,1071,1440);await page.waitForTimeout(50);await context.close();
 }
 // Actual browser DPR changes in one renderer, with old native messages left in memory.
 const context=await browser.newContext({viewport:{width:595,height:800},deviceScaleFactor:1.5});const page=await context.newPage();activePage=page;
 page.on('pageerror',error=>errors.push(String(error)));page.on('console',m=>{if(m.type()==='error')errors.push(m.text())});
 await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__fixture);await settled(page);
 const cdp=await context.newCDPSession(page);
 for(const dpi of [1,1.25,1.5,1.75,2,2.5,3,4,5,1.5]){
  const portrait={w:1200,h:Math.round(1200*780/580)},landscape={w:892,h:1200};
  for(const [name,physical]of[['landscape',landscape],['portrait',portrait],['landscape-return',landscape]]){
   const width=Math.round(physical.w/dpi),height=Math.round(physical.h/dpi);
   await cdp.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:dpi,mobile:false});
   await page.evaluate(()=>dispatchEvent(new Event('orientationchange')));await settled(page);
   const r=await snapshot(page);assert.equal(r.viewport.w,width);assert.equal(r.viewport.h,height);assert.equal(r.nativeFrames,0);
   await fits(page,dpi+' '+name);await checkMenuBounds(page,dpi+' '+name);
   checks.push('DPR '+dpi+' '+name+': old native size cannot pin rotation/DPI restoration');
   // Inject an old/wrong-axis sample. It must age out without another native resize.
   await nativeResize(page,physical.h,physical.w);await settled(page);await fits(page,dpi+' '+name+' delayed wrong native');
  }
 }
 await page.setViewportSize({width:595,height:800});await cdp.send('Emulation.setDeviceMetricsOverride',{width:595,height:800,deviceScaleFactor:1.5,mobile:false});await settled(page);
 await page.evaluate(()=>{
  window.__viewportDescriptors=[Object.getOwnPropertyDescriptor(window,'innerWidth'),Object.getOwnPropertyDescriptor(window,'innerHeight'),Object.getOwnPropertyDescriptor(visualViewport,'width'),Object.getOwnPropertyDescriptor(visualViewport,'height')];
  Object.defineProperty(window,'innerWidth',{configurable:true,value:4000});Object.defineProperty(window,'innerHeight',{configurable:true,value:3000});
  Object.defineProperty(visualViewport,'width',{configurable:true,value:5000});Object.defineProperty(visualViewport,'height',{configurable:true,value:4000});
  dispatchEvent(new Event('resize'));
 });
 const consistent=await snapshot(page);assert.equal(consistent.viewport.w,595);assert.equal(consistent.viewport.h,800);
 await page.evaluate(()=>{[[window,'innerWidth'],[window,'innerHeight'],[visualViewport,'width'],[visualViewport,'height']].forEach(([obj,key],i)=>{const d=window.__viewportDescriptors[i];if(d)Object.defineProperty(obj,key,d);else delete obj[key]});delete window.__viewportDescriptors;dispatchEvent(new Event('resize'))});
 checks.push('Root client viewport is one consistent pair; stale inner/visual dimensions cannot invent a large viewport');
 await page.evaluate(()=>{for(const detail of[{w:Infinity,h:800},{w:800,h:NaN},{w:0,h:0},{w:-1,h:720}])dispatchEvent(new CustomEvent('ipc:window.resized',{detail}));});
 await settled(page);await fits(page,'invalid sizes rejected');checks.push('Zero, negative and nonfinite native sizes do not poison recovery');
 // A delayed stale message followed by summon/visibility recovery must not survive.
 await nativeResize(page,3840,2160);await page.evaluate(()=>dispatchEvent(new CustomEvent('ipc:window.summoned')));await settled(page);await fits(page,'summon repairs old dimensions');
 await nativeResize(page,3840,2160);await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await settled(page);await fits(page,'visibility repairs old dimensions');
 checks.push('Summon and return-to-visible discard old native preference and recover');
 await page.evaluate(()=>{document.querySelector('.app-content').style.setProperty('--gamepad-clip-bottom','9999px');dispatchEvent(new CustomEvent('ipc:window.summoned'))});
 await settled(page);await fits(page,'old focus scroll tail cleared on summon');checks.push('Stale focus padding cannot persist after summon');
 await page.locator('.dd-trigger').click();await page.locator('.dd-menu').waitFor();
 await cdp.send('Emulation.setDeviceMetricsOverride',{width:400,height:600,deviceScaleFactor:2,mobile:false});
 await page.locator('.dd-menu').waitFor({state:'hidden'});await settled(page);await fits(page,'resize closes old popup');
 await checkMenuBounds(page,'reopened popup after resize');
 await cdp.send('Emulation.setDeviceMetricsOverride',{width:595,height:800,deviceScaleFactor:1.5,mobile:false});await settled(page);
 checks.push('Popup closes during resize and reopens at current size');
 await page.screenshot({path:path.join(out,'recovered-1200p-150pct.png')});
 await context.close();assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({ok:true,checks,errors,observations,scope:'Production App scaling/lifecycle/CSS and real Dropdown/focus compiled in a minimal Vue fixture. CDP DPR/orientation/size injection; no Windows display or installed app change; no physical controller acceptance.'},null,2));
 console.log(JSON.stringify({ok:true,checks:checks.length,observations:observations.length,result:path.join(out,'result.json')},null,2));
}catch(error){if(activePage)await activePage.screenshot({path:path.join(out,'failure.png')}).catch(()=>{});fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify({error:String(error.stack||error),checks,errors,observations},null,2));throw error;
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
