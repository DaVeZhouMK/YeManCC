// Layout-only test: production shared renderer with clearly marked synthetic data.
// Never attaches to the installed app, Steam, game, sensors or desktop input.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {createRequire} from 'node:module';import {createServer} from 'node:http';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_CORE_TILES_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/core-tiles-20261011/browser'));fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const entry=String.raw`import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';
import {makeHudFixture,normalizeMonitorOutput,HUD_METRICS} from './src/bridge/monitorHudModel.ts';
let preferences=normalizeMonitorOutput(),data=makeHudFixture();let notifications=0;
const renderer=mountMonitorHud(document.querySelector('#surface'),{preferences,data,onSize:()=>notifications++});
window.__coreTilesTestOnly=true;
window.__set=(layout,count,scale=1.4,only=false,missing=false,autoHide=false)=>{
 preferences=normalizeMonitorOutput({ymcc:{layout,scale,hideUnavailable:autoHide}});
 if(only)for(const m of HUD_METRICS)preferences.ymcc.fields[m.id]=m.id==='cpuCores';
 data={...makeHudFixture(),coreUsage:Array.from({length:count},(_,i)=>i===0?0:i===1?null:i===2?100:(i*13)%101),coreLabels:Array.from({length:count},(_,i)=>'CPU'+i)};
 if(missing)data.coreUsage=[];
 renderer.update(preferences,data);
 document.querySelector('#description').textContent='布局测试 · 模拟数据 · '+layout+' / '+count+' 项 / '+Math.round(scale*100)+'% · 不是实时核心占用';
};
window.__sample=()=>{data={...data,coreUsage:data.coreUsage.map((v,i)=>v===null?null:(v+i+7)%101)};renderer.update(preferences,data)};
window.__stale=()=>{data={...data,stale:true};renderer.update(preferences,data)};
window.__toggleOff=()=>{preferences.ymcc.fields.cpuCores=false;renderer.update(preferences,data)};
window.__dispose=()=>renderer.dispose();window.__notifications=()=>notifications;
for(const button of document.querySelectorAll('[data-count]'))button.addEventListener('click',()=>window.__set('corner',Number(button.dataset.count),1.4));
window.__set('corner',32,1.4);`;
const b=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,plugins:[monitorHudVuePlugin(root)]});
const html='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>YMCC 核心图块 · 布局测试 / 模拟数据</title><style>html,body{margin:0;background:#283442;color:#dbe4ee;font-family:Segoe UI,Microsoft YaHei UI,sans-serif}header{padding:12px 16px;font-size:12px;display:flex;gap:12px;align-items:center}header button{background:#152232;color:#d7e6f4;border:1px solid #496078;border-radius:5px;padding:4px 9px;cursor:pointer}#surface{padding:16px;box-sizing:border-box;width:100%}</style><header><span id="description"></span><button data-count="16">16</button><button data-count="32">32</button><button data-count="64">64</button><button data-count="128">128</button></header><div id="surface"></div><script>'+b.outputFiles[0].text.replaceAll('</script','<\/script')+'</script></html>';
const htmlPath=path.join(out,'index.html');fs.writeFileSync(htmlPath,html);
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html;charset=utf-8');res.end(html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_core-tiles.cjs')('playwright');
let browser;const cases=[],images=[],metrics=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:1280,height:720}});page.on('pageerror',e=>errors.push(String(e)));
 await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__set);
 for(const [width,height] of [[1280,720],[1920,1080]])for(const scale of [1,1.4])for(const layout of ['corner','bar','capsule'])for(const count of [1,8,16,24,32,64,128]){
  await page.setViewportSize({width,height});await page.evaluate(v=>window.__set(...v),[layout,count,scale]);await page.waitForTimeout(55);
  const s=await page.locator('[data-monitor-hud]').evaluate(el=>{
   const r=el.getBoundingClientRect(),bars=el.querySelector('.hud-core-bars'),br=bars.getBoundingClientRect(),tiles=[...el.querySelectorAll('.hud-core-tile')];
   return {width:r.width,height:r.height,right:r.right,bottom:r.bottom,font:getComputedStyle(el).fontSize,zoom:Number(getComputedStyle(el).zoom),tileCount:tiles.length,cols:getComputedStyle(bars).gridTemplateColumns.split(' ').length,tileBoxes:tiles.map(n=>{const x=n.getBoundingClientRect();return {x:x.x,y:x.y,right:x.right,bottom:x.bottom,width:x.width,height:x.height}}),grid:{x:br.x,y:br.y,right:br.right,bottom:br.bottom},anim:tiles.map(n=>getComputedStyle(n).animationName),transition:tiles.map(n=>getComputedStyle(n).transitionDuration)};
  });
  assert.equal(s.tileCount,Math.min(count,16));assert.equal(s.cols,Math.min(count,16));
  assert.equal(await page.locator('.hud-core-bars').innerText(),'');assert.equal(await page.locator('[data-hud-metric=cpuCores] .hud-chart-label,.hud-core-name,.hud-core-number').count(),0);
  assert(new Set(s.tileBoxes.map(t=>Math.round(t.y))).size===1);assert(s.grid.bottom-s.grid.y<=13*scale*1.2+.5);assert.equal(s.font,'13px');assert.equal(s.zoom,scale);
  assert(s.right<=width+.5,'HUD exceeds '+width+'px width');assert(s.bottom<=height+.5,JSON.stringify({layout,count,scale,width,height,s}));
  for(const t of s.tileBoxes){assert(t.width>=4&&t.height>=10);assert(t.x>=s.grid.x-.5&&t.right<=s.grid.right+.5);assert(t.y>=s.grid.y-.5&&t.bottom<=s.grid.bottom+.5)}
  assert(s.anim.every(a=>a==='none'));assert(s.transition.every(a=>a==='0s'));
  metrics.push({width,height,scale,layout,count,hudWidth:s.width,hudHeight:s.height,columns:s.cols});
 }
 cases.push('84 layouts fit both supported resolutions/scales: first 16 items only, one short row, no visible names/numbers/header');
 await page.setViewportSize({width:1280,height:720});await page.evaluate(()=>window.__set('corner',32,1.4));await page.waitForTimeout(80);
 const states=await page.locator('.hud-core-tile').evaluateAll(ns=>ns.slice(0,3).map(n=>({unknown:n.classList.contains('hud-core-tile--unknown'),fill:n.querySelector('i')?.style.height??null,text:n.textContent,index:n.dataset.coreIndex})));
 assert.equal(states[0].fill,'0%');assert.equal(states[0].unknown,false);assert.equal(states[1].fill,null);assert.equal(states[1].unknown,true);assert.equal(states[2].fill,'100%');cases.push('0% and unknown are visually distinct; fill/identity reflect incoming item, never aggregate CPU');
 assert(states.every(s=>s.text===''));assert.equal(states[2].index,'2');
 const first=await page.locator('.hud-core-tile i').first().getAttribute('style');await page.evaluate(()=>window.__sample());await page.waitForTimeout(50);assert.notEqual(await page.locator('.hud-core-tile i').first().getAttribute('style'),first);cases.push('Existing data updates repaint tiles without rebuilding cardinality or adding animation loops');
 for(const count of [16,32,64,128]){await page.evaluate(n=>window.__set('corner',n,1.4),count);await page.waitForTimeout(80);const p=path.join(out,'corner-'+count+'-items-140.png');await page.screenshot({path:p});images.push(p)}
 await page.evaluate(()=>window.__set('bar',32,1));await page.waitForTimeout(80);const bar=path.join(out,'bar-32-items-100.png');await page.screenshot({path:bar});images.push(bar);
 await page.evaluate(()=>window.__stale());await page.waitForTimeout(50);assert.equal(await page.locator('.hud-core-tile').count(),16);assert.equal(await page.locator('.hud-core-tile i').count(),0);assert.equal(await page.locator('[data-hud-metric=cpuCores]').innerText(),'');cases.push('Expired readings keep only neutral empty slots; no frozen loads or visible placeholder text');
 await page.evaluate(()=>window.__set('corner',32,1,true,true));await page.waitForTimeout(50);assert.equal(await page.locator('.hud-core-tile').count(),16);assert.equal(await page.locator('.hud-core-tile i').count(),0);assert.equal(await page.locator('[data-hud-metric=cpuCores]').innerText(),'');cases.push('Missing unified arrays keep 16 neutral slots when auto-hide is explicitly off, with no text and no synthetic usage');
 await page.evaluate(()=>window.__set('corner',32,1,true,true,true));await page.waitForTimeout(50);assert.equal(await page.locator('.hud-core-tile').count(),0);assert.equal(await page.locator('[data-hud-metric=cpuCores]').count(),0);cases.push('Auto-hide removes a missing core family without altering its saved field switch');
 await page.evaluate(()=>window.__set('corner',32,1));await page.evaluate(()=>window.__toggleOff());await page.waitForTimeout(50);assert.equal(await page.locator('[data-hud-metric=cpuCores]').count(),0);cases.push('Independent core toggle removes grid completely');
 await page.evaluate(()=>window.__dispose());assert.equal(await page.locator('[data-monitor-hud]').count(),0);assert.equal(await page.locator('[data-ymcc-hud-style]').count(),0);assert.deepEqual(errors,[]);cases.push('Renderer/observer/style disposal passes with no browser errors');
 const report={passed:cases.length,cases,metrics,images,htmlPath,scope:'Layout-only with explicit synthetic data through real shared Vue component. No live Windows/HWiNFO data, no installed HUD/game verification; no CPU benchmark.'};fs.writeFileSync(path.join(out,'CORE-TILES-BROWSER-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,cases,images,htmlPath,layoutCount:metrics.length},null,2));
}catch(error){if(browser)for(const p of browser.contexts().flatMap(c=>c.pages()))await p.screenshot({path:path.join(out,'failure.png'),fullPage:true});throw error;}
finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}
