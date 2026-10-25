// Shared renderer availability transitions: synthetic data only, no monitor acquisition.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {createRequire} from 'node:module';import {createServer} from 'node:http';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_AUTO_HIDE_OUT||path.join(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/battery-generic-gamepad-20261011/auto-hide-browser'));fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_monitor-auto-hide.cjs'))('playwright');
const entry=String.raw`import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';import {makeHudFixture,normalizeMonitorOutput,HUD_METRICS} from './src/bridge/monitorHudModel.ts';
let sizes=0,prefs=normalizeMonitorOutput(),data=makeHudFixture();
const renderer=mountMonitorHud(document.querySelector('#surface'),{preferences:prefs,data,onSize:()=>++sizes});
window.__set=(layout,mode,hide=true)=>{
 prefs=normalizeMonitorOutput({ymcc:{layout,scale:2,hideUnavailable:hide,fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,true]))}});
 data={...makeHudFixture(),stale:false,values:{},coreUsage:[],coreClocks:[],frameTimes:[]};
 if(mode==='zero'){data.values={cpuPower:0,batteryPower:0,gpuUsage:0};data.coreUsage=[0];}
 if(mode==='battery'){data.values={batteryPercent:75,batteryPower:-24};data.batteryPowerEstimated=true;}
 if(mode==='full')data=makeHudFixture();
 renderer.update(prefs,data);
};window.__repeat=()=>{for(let i=0;i<5;i++)renderer.update(prefs,{...data})};window.__sizes=()=>sizes;window.__metricCount=HUD_METRICS.filter(m=>m.id!=='clock').length;window.__selected=()=>Object.values(prefs.ymcc.fields).filter(Boolean).length;`;
const b=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,format:'iife',platform:'browser',plugins:[monitorHudVuePlugin(root)]});
const html='<!doctype html><meta charset="utf-8"><title>TEST ONLY — availability/auto-hide</title><style>body{margin:0;background:#293648}#surface{width:100%;box-sizing:border-box;padding:8px}</style><div id="surface"></div><script>'+b.outputFiles[0].text.replaceAll('</script','<\/script')+'</script>';
const server=createServer((q,r)=>{r.setHeader('Content-Type','text/html;charset=utf-8');r.end(html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;const cases=[],errors=[];
const wait=()=>new Promise(r=>setTimeout(r,100));
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(const width of [1280,1920]){
  const page=await browser.newPage({viewport:{width,height:1080}});page.on('pageerror',e=>errors.push(String(e)));await page.goto('http://127.0.0.1:'+server.address().port+'/');
  for(const layout of ['corner','bar','capsule']){
   for(const mode of ['empty','zero','battery','full','empty']){
    await page.evaluate(([l,m])=>__set(l,m),[layout,mode]);await wait();assert.equal(await page.locator('[data-monitor-hud]').count(),1);assert.equal(await page.evaluate(()=>__selected()),await page.evaluate(()=>__metricCount));
    const shape=await page.locator('[data-monitor-hud]').evaluate(el=>({text:el.textContent,visibility:getComputedStyle(el).visibility,width:el.getBoundingClientRect().width,groups:el.querySelectorAll('.hud-group').length,tiles:el.querySelectorAll('.hud-core-track').length}));
    if(mode==='empty'){assert.equal(shape.visibility,'hidden');assert(shape.width<=8);assert.equal(shape.groups,0);}
    if(mode==='zero'){assert.equal(shape.visibility,'visible');assert(shape.text.includes('0.0'));assert.equal(shape.tiles,1);assert(!shape.text.includes('—'));}
    if(mode==='battery'){assert(shape.text.includes('≈-24.0'));assert(shape.text.includes('75'));assert.equal(shape.groups,1);}
    if(mode==='full')assert.equal(shape.tiles,16);
    const previous=await page.evaluate(()=>__sizes());await page.evaluate(()=>__repeat());await wait();assert.equal(await page.evaluate(()=>__sizes()),previous,width+' '+layout+' '+mode+': same availability data must not trigger geometry churn');cases.push({width,layout,mode,widthPx:shape.width});
   }
   await page.evaluate(l=>__set(l,'empty',false),layout);await wait();assert.equal(await page.locator('[data-monitor-hud]').evaluate(el=>getComputedStyle(el).visibility),'visible');assert((await page.locator('[data-monitor-hud]').textContent()).includes('—'));
  }
  await page.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({ok:true,cases,errors,scope:'shared renderer, synthetic transitions; no real battery/game/CPU benchmark'},null,2));console.log(JSON.stringify({ok:true,cases:cases.length,errors,scope:'all 3 layouts, 200%, stable single root, valid zero/estimated power, selection preserved'},null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r));}
