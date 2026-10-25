// Production renderer/geometry tests; browser fixtures are not game acceptance.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
import {monitorHudVuePlugin} from './monitor_hud_vue_build.mjs';
const root=process.cwd(),out=path.resolve(process.env.YMCC_CROSSHAIR_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/crosshair-20261013');fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const entry=`import {nextTick} from 'vue';import {mountMonitorHud} from './src/bridge/monitorHudRenderer.ts';import {normalizeMonitorOutput,HUD_CROSSHAIRS,makeHudFixture,hudDataPresetFields} from './src/bridge/monitorHudModel.ts';import {hudCarrierPlacement,validHudViewport} from './src/bridge/monitorHudViewport.ts';import {hudPlacement} from './src/bridge/monitorHudProtocol.ts';
let renderer,events=0,current,viewport;const host=document.getElementById('hud');
window.__test={kinds:HUD_CROSSHAIRS.map(p=>p.value),normalize:v=>normalizeMonitorOutput({ymcc:{crosshair:v}}).ymcc.crosshair,valid:validHudViewport,carrier:hudCarrierPlacement,placement:hudPlacement,
 async set(spec){viewport=spec.viewport;host.style.width=viewport.width+'px';host.style.height=viewport.height+'px';let data=makeHudFixture();if(spec.missing){for(const key of Object.keys(data.values))data.values[key]=null;data.coreUsage=[];data.coreClocks=[];data.frameTimes=[];}current=normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{crosshair:spec.kind,position:spec.position||1,scale:spec.scale||1,opacity:spec.opacity??.86,layout:spec.layout||'corner',fields:hudDataPresetFields(spec.preset||'minimal'),hideUnavailable:!!spec.missing}});if(!renderer)renderer=mountMonitorHud(host,{preferences:current,data,viewport,onSize:()=>events++});else{renderer.setViewport(viewport);renderer.update(current,data);}await nextTick();await nextTick();return current;},
 inspect(){const el=host.querySelector('[data-monitor-crosshair]'),scene=host.querySelector('[data-monitor-scene]'),board=host.querySelector('[data-monitor-hud]');const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}};return {crosshair:el?{...rect(el),kind:el.dataset.monitorCrosshair,pointer:getComputedStyle(el).pointerEvents,opacity:getComputedStyle(el).opacity}:null,scene:scene?rect(scene):null,board:board?rect(board):null,events,hit:document.elementFromPoint(innerWidth/2,innerHeight/2)?.id,animations:document.getAnimations().length,focusable:host.querySelectorAll('button,input,select,a[href],[tabindex]:not([tabindex="-1"])').length};},
 async same(){const start=events;for(let i=0;i<50;i++){renderer.update(current,makeHudFixture());await nextTick();}return events-start},dispose(){renderer?.dispose();renderer=null;}};`;
const result=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,define:{'process.env.NODE_ENV':'"production"'},plugins:[monitorHudVuePlugin(root)]});
const html='<!doctype html><meta charset="utf-8"><title>Crosshair geometry fixture</title><style>*{box-sizing:border-box}html,body{margin:0;overflow:hidden;background:#142430}#underneath{position:fixed;inset:0;width:100%;height:100%;border:0;background:transparent}#hud{position:relative;pointer-events:none}</style><button id="underneath" aria-label="Underlying game surface"></button><div id="hud"></div><script>'+result.outputFiles[0].text.replaceAll('</script','<\\/script')+'</script>';
const htmlFile=path.join(out,'crosshair-geometry.html');fs.writeFileSync(htmlFile,html);
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_crosshair.cjs')('playwright');
const cases=[],errors=[];let browser;
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 for(const dpr of [1,1.25,1.5,2]){
  const context=await browser.newContext({viewport:{width:1920,height:1080},deviceScaleFactor:dpr}),page=await context.newPage();page.on('pageerror',e=>errors.push(String(e)));await page.goto(pathToFileURL(htmlFile).href);await page.waitForFunction(()=>window.__test);
  assert.deepEqual(await page.evaluate(()=>window.__test.kinds),['none','dot','cross','gap','ring','ring-dot']);
  for(const kind of ['dot','cross','gap','ring','ring-dot'])assert.equal(await page.evaluate(v=>window.__test.normalize(v),kind),kind);
  for(const invalid of [null,'x',5,{},false])assert.equal(await page.evaluate(v=>window.__test.normalize(v),invalid),'none');
  for(const v of [{width:0,height:1080},{width:1920,height:-1},{width:20000,height:1080},{width:1920,height:1080,workArea:{left:0,top:0,width:1920,height:1090}}])assert.equal(await page.evaluate(v=>window.__test.valid(v),v),false);
  const scenarios=[];
  for(const [width,height]of [[1280,720],[1920,1080],[2560,1440],[3840,2160],[1919,1079]])for(const kind of ['dot','cross','gap','ring','ring-dot'])scenarios.push({viewport:{width,height},kind,scale:1,position:1});
  for(const scale of [.2,.8,1.4,2])for(const position of [1,2,3,4,5,6])scenarios.push({viewport:{width:1920,height:1080,workArea:{left:70,top:30,width:1850,height:990}},kind:'gap',scale,position,opacity:position%2?0:1,layout:position%2?'bar':'corner'});
  scenarios.push({viewport:{width:1280,height:720},kind:'dot',missing:true});
  for(const spec of scenarios){
   await page.setViewportSize({width:spec.viewport.width,height:spec.viewport.height});await page.evaluate(s=>window.__test.set(s),spec);await page.waitForFunction(()=>{const v=window.__test.inspect();return !!v.crosshair&&!!v.scene});
   const state=await page.evaluate(()=>window.__test.inspect()),c=state.crosshair;
   assert.equal(c.x+c.width/2,spec.viewport.width/2);assert.equal(c.y+c.height/2,spec.viewport.height/2);assert.equal(c.width,24);assert.equal(c.height,24);assert.equal(c.pointer,'none');assert.equal(c.opacity,'1');assert.equal(state.scene.width,spec.viewport.width);assert.equal(state.scene.height,spec.viewport.height);assert.equal(state.hit,'underneath');assert.equal(state.animations,0);assert.equal(state.focusable,0);
   const placement=await page.evaluate(s=>window.__test.carrier(s.viewport,300,200,s.position||1,s.kind),spec);assert.deepEqual(placement,{left:0,top:0,width:spec.viewport.width,height:spec.viewport.height});
   // Virtual desktop coordinates may be negative; full surface origin must preserve physical monitor center.
   for(const [left,top]of [[0,0],[-1920,0],[2560,-1440]]){assert.equal(left+c.x+c.width/2,left+spec.viewport.width/2);assert.equal(top+c.y+c.height/2,top+spec.viewport.height/2);}
   cases.push({dpr,...spec,center:[c.x+c.width/2,c.y+c.height/2]});
  }
  await page.setViewportSize({width:1920,height:1080});
  for(const kind of ['dot','cross','gap','ring','ring-dot','none','ring','none']){await page.evaluate(kind=>window.__test.set({kind,viewport:{width:1920,height:1080},scale:2,position:6}),kind);await page.waitForTimeout(30);const state=await page.evaluate(()=>window.__test.inspect());assert.equal(!!state.crosshair,kind!=='none');assert.equal(!!state.scene,kind!=='none');if(kind==='none'){const placements=await page.evaluate(()=>[window.__test.carrier({width:1920,height:1080},300,200,6,'none'),window.__test.placement(1920,1080,300,200,6)]);assert.deepEqual(...placements);}else if(dpr===1){await page.locator('[data-monitor-crosshair]').screenshot({path:path.join(out,'crosshair-'+kind+'.png')});}}
  await page.evaluate(()=>window.__test.set({kind:'ring-dot',viewport:{width:1920,height:1080}}));await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>window.__test.same()),0);
  if(dpr===1)await page.screenshot({path:path.join(out,'full-screen-1920.png')});
  await page.evaluate(()=>window.__test.dispose());assert.equal(await page.locator('[data-monitor-crosshair]').count(),0);await context.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'CROSSHAIR-BROWSER.json'),JSON.stringify({ok:true,cases,errors,actualGame:false,installed:false},null,2));console.log('Crosshair browser PASS: '+cases.length+' center/scale/DPR/viewport cases; whitelist, toggles, no hit targets, idle and dispose checked.');
}finally{await browser?.close();}
