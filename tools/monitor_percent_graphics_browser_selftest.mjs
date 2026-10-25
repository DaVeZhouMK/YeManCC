// Production model + shared SFC, explicit fixtures only. No installed app/sensor/game I/O.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { monitorHudVuePlugin } from './monitor_hud_vue_build.mjs';
const root=process.cwd(), out=path.resolve(process.env.YMCC_MONITOR_PERCENT_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/percent-graphics-battery-20261012'), dir=path.join(out,'percent-browser');
fs.mkdirSync(dir,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const entry=`
import {nextTick} from 'vue';
import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';
import * as m from './src/bridge/monitorHudModel.ts';
import {ageHudData} from './src/bridge/monitorHudFreshness.ts';
let p=m.normalizeMonitorOutput(), data=m.makeHudFixture(), sizes=0;
const hud=mountMonitorHud(document.querySelector('#surface'),{preferences:p,data,onSize:()=>sizes++});
window.__pairs=m.HUD_PERCENT_GRAPHS;
window.__set=async(ids,patch={},opts={})=>{
 const fields=Object.fromEntries(m.HUD_METRICS.map(metric=>[metric.id,ids.includes(metric.id)]));
 p=m.normalizeMonitorOutput({ymcc:{fields,layout:opts.layout||'corner',scale:opts.scale||1,hideUnavailable:!!opts.hide}});
 data=m.makeHudFixture(); data.values={...data.values,...patch};
 if(opts.stale)data.stale=true;
 if(opts.native)data.source='ymcc-uniform';
 if(opts.frameTimes)data.frameTimes=opts.frameTimes;
 if(opts.freshUntil)data.freshUntil=opts.freshUntil;
 hud.update(p,data);await nextTick();
};
window.__value=async(id,value)=>{data={...data,values:{...data.values,[id]:value}};hud.update(p,data);await nextTick();};
window.__noChange=async()=>{const mo=new MutationObserver(()=>{});mo.observe(document.querySelector('#surface'),{subtree:true,attributes:true,characterData:true,childList:true});const n=sizes;for(let i=0;i<200;i++){hud.update(p,data);await nextTick()}const mutations=mo.takeRecords().length;mo.disconnect();return {sizes:sizes-n,mutations};};
window.__modelCheck=()=>{
 const cases=[],eq=(a,b,n)=>{if(JSON.stringify(a)!==JSON.stringify(b))throw Error(n+':'+JSON.stringify({a,b}));},ok=(v,n)=>{if(!v)throw Error(n);};
 const fixture=m.makeHudFixture();
 eq(m.HUD_METRICS.length,41,'41 metrics');ok(!m.HUD_METRICS.some(metric=>metric.id==='cpuPeak'),'removed numeric highest-core load');
 eq(m.HUD_PERCENT_GRAPHS.length,7,'7 percentage pairs');
 eq(m.HUD_METRICS.filter(x=>x.unit==='%'&&!m.HUD_PERCENT_GRAPHS.some(g=>g.id===x.id)&&x.id!=='cpuCores').map(x=>x.id).sort(),m.HUD_PERCENT_GRAPHS.map(g=>g.value).filter(id=>id!=='cpuPeak').sort(),'all scalar percentages covered');
 for(const g of m.HUD_PERCENT_GRAPHS){
   const fields=Object.fromEntries(m.HUD_METRICS.map(metric=>[metric.id,metric.id===g.id]));
   const changed={...fixture,values:{...fixture.values,[g.value]:12}};
   ok(!m.sameHudData(fixture,changed,fields),'graph-only follows '+g.value);
   ok(m.sameHudData(fixture,{...fixture,values:{...fixture.values,clock:'00:00'}},fields),'unselected scalar skipped '+g.id);
   for(const v of [0,25,76,100])eq(m.hudPercentValue({...fixture,values:{[g.value]:v}},g.value),v,'valid '+g.id);
   for(const v of [null,undefined,-1,101,NaN,Infinity,'76',false]){
     const d={...fixture,values:{[g.value]:v}};
     eq(m.hudPercentValue(d,g.value),null,'unknown '+g.id);
     eq(m.hudMetricAvailable(d,g.id),false,'unavailable '+g.id);
     eq(m.boundedHudData(d).values[g.value],null,'bad numeric admission '+g.id);
   }
   eq(m.hudPercentValue({...fixture,stale:true},g.value),null,'stale '+g.id);
   if(g.value!=='cpuPeak')ok(m.hudMetricControlLabel(m.HUD_METRICS.find(x=>x.id===g.value)).includes('百分比'),'separate numeric control label');
 }
 cases.push('7 independent pairs cover every scalar percentage; source-only dependency comparison; strict 0..100/unknown admission');
 eq(m.makeHudFixture().values.fanSpeed,76,'explicit sample 76 only');
 const native=m.hudFromUniformSnapshot({hwDown:true,top:null,fps:null},Date.now());
 eq(native.values.fanSpeed,null,'no fan receipt stays unknown');
 eq(m.boundedHudData({...fixture,source:'ymcc-uniform'}).values.fanSpeed,null,'no qualified target receipt cannot reuse sample');
 ok(!m.HUD_METRICS.some(x=>x.id==='coreClocks'),'core clocks removed');
 ok(!('coreClocks' in m.normalizeMonitorOutput({ymcc:{fields:{coreClocks:true}}}).ymcc.fields),'legacy clocks ignored');
 eq(m.normalizeMonitorOutput({ymcc:{fields:{batteryPercent:true}}}).ymcc.fields.batteryBar,true,'legacy battery number gains shape');
 eq(m.normalizeMonitorOutput({ymcc:{fields:{batteryPercent:true,batteryBar:false}}}).ymcc.fields.batteryBar,false,'explicit independent battery switch honored');
 cases.push('YMCC target-only fan / unsupported unknown; removed clocks; legacy battery migration respects explicit false');
 for(const id of ['cpuTemp','gpuTemp']){
   for(const v of [89.9,90,90.01,95])eq(m.hudTemperatureWarning({...fixture,values:{[id]:v}},id),v>90,'strict threshold '+id);
   for(const v of [null,undefined,NaN,Infinity])eq(m.hudTemperatureWarning({...fixture,values:{[id]:v}},id),false,'unknown temp');
   eq(m.hudTemperatureWarning({...fixture,stale:true,values:{[id]:99}},id),false,'stale temp');
 }
 eq(m.hudTemperatureWarning({...fixture,values:{cpuPower:100}},'cpuPower'),false,'not power warning');
 cases.push('CPU/GPU independent red warning strictly >90C, not 90; unknown/stale/no-temperature stays silent');
 const now=1700000000000, map={cpuUsageBar:'hardware',cpuPeakBar:'hardware',fanSpeedBar:'fan',gpuBar:'fps',vramUsageBar:'hardware',ramUsageBar:'hardware',batteryBar:'batteryStatus'};
 for(const g of m.HUD_PERCENT_GRAPHS){
   const until={hardware:now+6000,fps:now+6000,fan:now+6000,batteryStatus:now+6000};until[map[g.id]]=now+100;
   const d={...fixture,source:'ymcc-uniform',freshUntil:until};
   const fresh=ageHudData(m.boundedHudData(d),now+99),expired=ageHudData(m.boundedHudData(d),now+100);
   ok(m.hudMetricAvailable(fresh,g.id),'independent lease live '+g.id);
   eq(m.hudPercentValue(expired,g.value),null,'independent expiry '+g.id);
   eq(m.hudValue(expired,'cpuPower')!=='—',map[g.id]!=='hardware','unrelated hardware lease '+g.id);
 }
 cases.push('every graphic reuses its numeric source lease; hardware/fps/fan/battery deadlines remain independent');
 return cases;
};
window.__dispose=()=>hud.dispose(); window.__set(['fps','cpuUsage','cpuUsageBar','batteryPercent','batteryBar']);
`;
const bundle=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,plugins:[monitorHudVuePlugin(root)]});
const html=`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>百分比图形与告警验证 · 示例数据</title><style>html,body{margin:0;background:#17212d;color:#fff;font-family:Segoe UI,Microsoft YaHei UI,sans-serif}header{padding:8px 12px;font-size:12px}#surface{width:100%;box-sizing:border-box}</style><header>生产共享组件 · 测试数据，不是本机读数或游戏验收</header><div id="surface"></div><script>${bundle.outputFiles[0].text.replaceAll('</script','<\\/script')}</script></html>`;
fs.writeFileSync(path.join(dir,'index.html'),html);
const server=createServer((req,res)=>{res.setHeader('Content-Type','text/html;charset=utf-8');res.end(html);});await new Promise(r=>server.listen(0,'127.0.0.1',r));
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_percent.cjs')('playwright');
let browser;const cases=[],errors=[],checks=[];let page;
const wait=()=>new Promise(r=>setTimeout(r,80));
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 page=await browser.newPage({viewport:{width:1920,height:1080}});page.on('pageerror',e=>errors.push(String(e)));
 await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__modelCheck);
 cases.push(...await page.evaluate(()=>window.__modelCheck()));
 const set=async(ids,patch={},opts={})=>{await page.evaluate(([ids,patch,opts])=>window.__set(ids,patch,opts),[ids,patch,opts]);await wait();};
 const graph=id=>page.locator('[data-hud-metric="'+id+'"]');
 const state=async id=>graph(id).evaluate(el=>{const track=el.querySelector('[role="meter"]'),fill=track.querySelector('i');const r=track.getBoundingClientRect();const style=getComputedStyle(track);return {state:el.dataset.percentState,text:el.innerText,fill:fill?.style.width??null,now:track.getAttribute('aria-valuenow'),unknown:track.getAttribute('aria-valuetext'),w:r.width,h:r.height,filled:fill?.getBoundingClientRect().width??null,inner:r.width-parseFloat(style.borderLeftWidth)-parseFloat(style.borderRightWidth)};});
 const pairs=await page.evaluate(()=>window.__pairs);
 for(const pair of pairs){
   for(const layout of ['corner','bar','capsule']){
     await set([pair.value],{[pair.value]:76},{layout});assert.equal(await graph(pair.id).count(),0);if(pair.value!=='cpuPeak')assert.match(await graph(pair.value).innerText(),/76\s*%/);
     await set([pair.id],{[pair.value]:76},{layout});assert.equal(await graph(pair.value).count(),0);let s=await state(pair.id);assert.equal(s.fill,'76%');assert.equal(s.now,'76');assert(s.w>15&&s.h>2);assert(!/76|%/.test(s.text));
     await set([pair.id,pair.value],{[pair.value]:76},{layout});s=await state(pair.id);assert.equal(s.fill,'76%');assert(s.w>15&&s.h>2);if(pair.value!=='cpuPeak')assert.equal((await graph(pair.id).locator('..').getAttribute('data-hud-metric')),pair.value);
     await page.evaluate(([id,value])=>window.__value(id,value),[pair.value,25]);await wait();assert.equal((await state(pair.id)).fill,'25%');
     await set([]);assert.equal(await graph(pair.id).count(),0);assert.equal(await graph(pair.value).count(),0);
   }
   cases.push(pair.value==='cpuPeak'?'Removed numeric peak absent; independent legacy graphic only, 3 layouts':pair.value+' numeric-only / graph-only / both / off; 3 layouts and graph-only changes');
   for(const value of [0,25,76,100,null,-1,101]){
     await set([pair.value,pair.id],{[pair.value]:value});const s=await state(pair.id);const valid=value!==null&&value>=0&&value<=100;
     assert.equal(s.state,valid?'live':'unknown');assert.equal(s.fill,valid?value+'%':null);assert.equal(s.now,valid?String(value):null);
     if(!valid){assert.equal(s.unknown,'未知');if(pair.value!=='cpuPeak')assert.match(await graph(pair.value).innerText(),/—/);}checks.push({id:pair.id,input:value,...s});
   }
   await set([pair.id],{}, {stale:true});assert.equal((await state(pair.id)).fill,null);
   await set([pair.id],{[pair.value]:null},{hide:true});assert.equal(await graph(pair.id).count(),0);
 }
 await set(['batteryPercent'],{batteryPercent:76});const plainHeight=await graph('batteryPercent').locator('.hud-number').evaluate(el=>el.getBoundingClientRect().height);
 await set(['batteryPercent','batteryBar'],{batteryPercent:76});const batteryState=await state('batteryBar');assert(Math.abs(batteryState.h-plainHeight)<2,'battery graphic height matches plain percentage line');assert.equal(await graph('batteryBar').locator('.hud-battery-value').innerText(),'76%');cases.push('battery percent is embedded in the block and keeps plain-text height');
 cases.push('all 7 fills use admitted source values; real 0 differs from unknown, missing/stale has no fabricated fill; auto-hide respects source');
 await set(['fanSpeed','fanSpeedBar'],{}, {native:true});assert.equal((await state('fanSpeedBar')).fill,null);assert.match(await graph('fanSpeed').innerText(),/—/);
 cases.push('uniform fan without enabled target proof is unknown, never preview 76%');
 for(const value of [0,25,76,100]){await set(['batteryPercent','batteryBar'],{batteryPercent:value});const s=await state('batteryBar');assert(Math.abs(s.filled-s.inner*value/100)<1);}
 cases.push('battery icon interior filled by actual 0/25/76/100 percent; combined mode embeds text, graph-only stays independent');
 for(const times of [[],[8,9,10],[8,9,null,10,11]]){
   await set(['frametimeGraph'],{}, {frameTimes:times});const chart=graph('frametimeGraph');
    if(!times.length){assert.equal(await chart.count(),0,'no samples hides the entire curve instead of reserving a white strip');continue;}
    assert.equal((await chart.innerText()).trim(),'');assert.equal(await chart.locator('polyline').count(),times.includes(null)?2:1);
   assert.equal(await chart.locator('text').count(),0);
 }
 await set(['frametimeGraph'],{}, {stale:true});assert.equal(await graph('frametimeGraph').locator('polyline').count(),0);
 cases.push('frametime is curve-only, no title/count/placeholder; missing samples split paths; stale produces no fake curve');
 for(const id of ['cpuTemp','gpuTemp']){
   for(const value of [89.9,90,90.01,95,null]){
     await set([id],{[id]:value});const warning=graph(id).locator('.hud-temp-warning');assert.equal(await warning.count(),1,'reserved warning slot prevents geometry jitter');assert.equal(await warning.getAttribute('aria-hidden'),value!==null&&value>90?'false':'true');assert.equal(await warning.evaluate(el=>getComputedStyle(el).visibility),value!==null&&value>90?'visible':'hidden');
     if(value>90){assert.equal(await warning.innerText(),'!');assert.equal(await warning.evaluate(el=>getComputedStyle(el).color),'rgb(255, 91, 99)');}
   }
   await set([id],{[id]:99},{stale:true});assert.equal(await page.locator('.hud-temp-warning:not(.hud-temp-warning--inactive)').count(),0);
 }
 await set(['cpuTemp','gpuTemp'],{cpuTemp:95,gpuTemp:90});assert.equal(await page.locator('.hud-temp-warning:not(.hud-temp-warning--inactive)').count(),1);
 await set(['fps'],{cpuTemp:99,gpuTemp:99});assert.equal(await page.locator('.hud-temp-warning').count(),0);
 cases.push('red ! only beside enabled CPU/GPU temperature >90C; unknown/stale/90/no-temp gets none');
 for(const width of [1280,1920]){
   await page.setViewportSize({width,height:width===1280?720:1080});
   for(const layout of ['corner','bar','capsule']){
     const ids=await page.evaluate(()=>window.__pairs.flatMap(g=>[g.value,g.id]));ids.push('fps','cpuTemp','gpuTemp','cpuCores','frametimeGraph');
     await set(ids,{cpuTemp:95,gpuTemp:96},{layout,scale:2});
     const geometry=await page.locator('[data-monitor-hud]').evaluate(el=>{const b=el.getBoundingClientRect();return {w:b.width,h:b.height,parts:[...el.querySelectorAll('[role="meter"],.hud-temp-warning')].map(n=>{const r=n.getBoundingClientRect();return {x:r.x-b.x,y:r.y-b.y,right:r.right-b.x,bottom:r.bottom-b.y,w:r.width,h:r.height};})};});
     assert(geometry.w<=width+.5,JSON.stringify({width,layout,geometry}));assert(geometry.h<=(width===1280?720:1080),JSON.stringify({width,layout,geometry}));assert(geometry.parts.every(r=>r.w>4&&r.h>2&&r.x>=0&&r.y>=0&&r.right<=geometry.w+.5&&r.bottom<=geometry.h+.5));checks.push({width,layout,geometry});
   }
 }
 cases.push('14 percentage controls + 2 warnings + curve/core tiles fit at 200% in 1280x720 and 1920x1080');
 await page.setViewportSize({width:1920,height:1080});await set(pairs.flatMap(g=>[g.value,g.id]).concat(['fps','cpuTemp','gpuTemp','cpuCores','frametimeGraph']),{cpuTemp:95,gpuTemp:96},{layout:'corner'});
 await page.locator('[data-monitor-hud]').screenshot({path:path.join(dir,'percent-battery-warnings.png')});
 await set(['cpuUsageBar'],{cpuUsage:76});await page.evaluate(()=>window.__value('cpuUsage',25));await wait();assert.equal((await state('cpuUsageBar')).fill,'25%');
 await page.evaluate(()=>window.__value('clock','00:00'));await wait();assert.deepEqual(await page.evaluate(()=>window.__noChange()),{sizes:0,mutations:0});
 cases.push('graph-only updates with numeric switch off; 200 identical packets cause no DOM mutation or native resize');
 assert.deepEqual(errors,[]);await page.evaluate(()=>window.__dispose());
 fs.writeFileSync(path.join(dir,'RESULT.json'),JSON.stringify({ok:true,cases,checks,errors,scope:'Production model and shared SFC with explicit synthetic inputs only. No deployment, live fan/hardware, Steam/game testing or CPU measurements.'},null,2));
 console.log(JSON.stringify({ok:true,cases:cases.length,checks:checks.length,errors}));
}catch(e){await page?.screenshot({path:path.join(dir,'failure.png'),fullPage:true});throw e;}finally{await browser?.close();await new Promise(r=>server.close(r));}
