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
const scale=source.slice(source.indexOf('const BASE_W ='),source.indexOf('// M8: Start button'));
const lifecycle=source.slice(source.indexOf('let stageResizeObserver:'),source.indexOf('</script>'));
assert(scale.includes('function viewportSize()'));assert(lifecycle.includes('onNativeResize'));
const scope='data-v-window-layout-test',styles=[fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')];
const {descriptor:app}=parse(source,{filename:'App.vue'});
for(const style of app.styles){const result=compileStyle({source:style.content,filename:'App.vue',id:scope,scoped:style.scoped});assert.deepEqual(result.errors,[]);styles.push(result.code);}
const fixture=`<script setup lang="ts">
import {ref,computed,onMounted,onUnmounted,nextTick} from 'vue';
import Dropdown from './src/components/Dropdown.vue';
const isStandaloneEditor=ref(false);
${scale}
${lifecycle}
window.__fixture={setStandalone(value){isStandaloneEditor.value=value;updateScale();},flush:nextTick,snapshot(){return {native:nativeViewport.value,viewport:viewportSize(),scale:uiScale.value,stage:scalerStyle.value,cssScale:Number(getComputedStyle(document.documentElement).getPropertyValue('--ui-scale'))};}};
</script><template><div class="app-root"><div class="user-background-image" style="background-image:linear-gradient(#234,#456)"/><div class="app-backdrop"/><div class="app-stage" :style="scalerStyle"><div style="padding:24px;width:280px"><Dropdown model-value="a" :options="[{value:'a',label:'布局测试'},{value:'b',label:'第二项'}]"/></div></div></div></template>`;
const {descriptor}=parse(fixture,{filename:'window-layout-fixture.vue'});
const fixtureScript=compileScript(descriptor,{id:scope,inlineTemplate:true}).content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(scope)};export default component;`;
const bundle=await build({stdin:{contents:"import {createApp} from 'vue';import Component from 'window-layout-fixture';window.__app=createApp(Component);window.__app.mount('#app');",loader:'js',resolveDir:root},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},alias:{'@':path.join(root,'src')},plugins:[{name:'layout-fixture',setup(b){
 b.onResolve({filter:/^window-layout-fixture$/},()=>({path:'fixture',namespace:'fixture'}));
 b.onLoad({filter:/.*/,namespace:'fixture'},()=>({contents:fixtureScript,loader:'ts',resolveDir:root}));
 b.onResolve({filter:/^@\/gamepad\/focus$/},()=>({path:'focus',namespace:'mock'}));
 b.onLoad({filter:/.*/,namespace:'mock'},()=>({contents:"export const focusGamepadElement=el=>el?.focus();export function getGamepadPopupPlacement(rect,width,height,gap){return {above:false,style:{position:'fixed',left:rect.left+'px',top:(rect.bottom+gap)+'px',width:width+'px',maxHeight:height+'px'}}}",loader:'js'}));
 b.onLoad({filter:/\.vue$/},args=>{const {descriptor}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});const id='data-v-layout-dropdown';for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);styles.push(compiled.code);}let script=compileScript(descriptor,{id,inlineTemplate:true}).content.replace('export default','const component=');return {contents:script+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`,loader:'ts',resolveDir:path.dirname(args.path)};});
}}]});
const html='<!doctype html><meta charset="utf-8"><style>'+styles.join('\n')+'</style><div id="app"></div><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html');response.end(request.url==='/fixture.js'?bundle.outputFiles[0].text:html);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const checks=[],errors=[],observations=[];let browser;
const snapshot=page=>page.evaluate(()=>window.__fixture.snapshot());
const nativeResize=(page,w,h)=>page.evaluate(({w,h})=>window.dispatchEvent(new CustomEvent('ipc:window.resized',{detail:{w,h}})),{w,h});
async function menuFont(page){await page.locator('.dd-trigger').click();await page.locator('.dd-menu').waitFor();const font=await page.locator('.dd-option').first().evaluate(el=>Number.parseFloat(getComputedStyle(el).fontSize));await page.keyboard.press('Escape');return font;}
async function coverage(page){const rects=await page.evaluate(()=>{const rect=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};return {viewport:{width:innerWidth,height:innerHeight},image:rect('.user-background-image'),backdrop:rect('.app-backdrop'),stage:rect('.app-stage')};});for(const layer of ['image','backdrop']){assert(Math.abs(rects[layer].width-rects.viewport.width)<1,`${layer} width`);assert(Math.abs(rects[layer].height-rects.viewport.height)<1,`${layer} height`);}observations.push(rects);}
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(const dpr of [1,1.5,2]){
  const context=await browser.newContext({viewport:{width:640,height:720},deviceScaleFactor:dpr});const page=await context.newPage();page.on('pageerror',error=>errors.push(String(error)));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.waitForFunction(()=>window.__fixture);await page.evaluate(async()=>{__fixture.setStandalone(true);await __fixture.flush();});
  const before=await snapshot(page),fontBefore=await menuFont(page);await nativeResize(page,1071,1440);await page.waitForTimeout(1600);const after=await snapshot(page),fontAfter=await menuFont(page);
  assert.equal(after.native,null);assert.equal(after.cssScale,before.cssScale);assert.equal(after.stage.zoom,1);assert.equal(fontAfter,fontBefore);checks.push(`DPR ${dpr}: standalone native broadcast leaves viewport and Dropdown unchanged`);
  await page.setViewportSize({width:480,height:540});await page.waitForFunction(()=>Math.abs(__fixture.snapshot().cssScale-480/580)<1e-8);await page.evaluate(()=>__fixture.flush());const small=await snapshot(page);assert.equal(small.viewport.w,480);assert.equal(small.viewport.h,540);assert(Math.abs(small.cssScale-480/580)<1e-8);const smallFont=await menuFont(page);assert(Math.abs(smallFont-14*480/580)<0.01);checks.push(`DPR ${dpr}: standalone resize keeps Dropdown at its own CSS scale`);
  await coverage(page);await page.screenshot({path:path.join(out,`standalone-dpr-${dpr}.png`)});
  await page.evaluate(async()=>{__fixture.setStandalone(false);await __fixture.flush();});await nativeResize(page,Math.round(800*dpr),Math.round(1080*dpr));await page.evaluate(()=>__fixture.flush());const main=await snapshot(page);assert(Math.abs(main.viewport.w-800)<1);assert(Math.abs(main.viewport.h-1080)<1);assert(Number.parseFloat(main.stage.height)*main.stage.zoom>=1080);checks.push(`DPR ${dpr}: main renderer converts native physical pixels and fills height`);
  await page.setViewportSize({width:800,height:1080});await nativeResize(page,Math.round(800*dpr),Math.round(1080*dpr));await page.evaluate(()=>__fixture.flush());await coverage(page);
  await page.setViewportSize({width:480,height:540});await nativeResize(page,Math.round(480*dpr),Math.round(540*dpr));await page.evaluate(()=>__fixture.flush());const shrunk=await snapshot(page);assert.equal(shrunk.viewport.w,480);assert.equal(shrunk.viewport.h,540);await coverage(page);checks.push(`DPR ${dpr}: smaller native reflow shrinks scale and preserves full-window backgrounds`);
  await page.evaluate(()=>window.__app.unmount());await nativeResize(page,1071,1440);await page.waitForTimeout(50);await context.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({ok:true,checks,errors,observations,scope:'Production App scaling/lifecycle and Dropdown compiled in a minimal Vue fixture; no live native/hardware acceptance.'},null,2));console.log(JSON.stringify({ok:true,checks:checks.length,result:path.join(out,'result.json')},null,2));
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
