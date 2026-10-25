// Compare complete production consumers before/current with deterministic work
// counters and headless Vue replay. No timing, hardware, installed app or game.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { monitorHudVuePlugin } from './monitor_hud_vue_build.mjs';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const task=path.resolve(process.env.YMCC_MONITOR_CONSUMPTION_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cpu-consumption-20261014');
const baseline=path.resolve(process.env.YMCC_MONITOR_CONSUMPTION_BASELINE||path.join(task,'before'));
const out=path.join(task,'validation');fs.mkdirSync(out,{recursive:true});
const keys=['normalizations','presetEntryArrays','imageChecks','valueCopies','curveBuilds','dataBounds'];
const blank=()=>Object.fromEntries(keys.map(key=>[key,0]));
const workPlugin=(side)=>({name:'monitor-offline-work-'+side,setup(b){
 b.onResolve({filter:/^(?:\.{1,2}\/|@\/)/},args=>{
   let p=args.path.startsWith('@/')?path.join(side==='before'?baseline:root,'src',args.path.slice(2)):path.resolve(args.resolveDir,args.path);
   const find=v=>[v,v+'.ts',v+'.vue',v+'.mjs',path.join(v,'index.ts')].find(f=>fs.existsSync(f)&&fs.statSync(f).isFile());
   if(side==='before'&&p.startsWith(baseline)&&!find(p))p=path.join(root,path.relative(baseline,p));
   const file=find(p);return file?{path:file}:undefined;
 });
 b.onResolve({filter:/^vue$/},()=>({path:req.resolve('vue/dist/vue.runtime.esm-bundler.js')}));
 b.onLoad({filter:/monitor(?:HudModel|HudFreshness|FrameTime)\.ts$/},args=>{
   let text=fs.readFileSync(args.path,'utf8');
   const bump=k=>`globalThis.__monitorCpuOps.${side}.${k}++`;
   text=text.replace('export function normalizeMonitorOutput(raw?: unknown): MonitorOutputPreferences {',`export function normalizeMonitorOutput(raw?: unknown): MonitorOutputPreferences { ${bump('normalizations')};`);
   text=text.replace('    dataPresetFields[preset.value] = Object.fromEntries(HUD_METRICS.map(metric => [metric.id,',`    globalThis.__monitorCpuOps.${side}.presetEntryArrays += HUD_METRICS.length + 1;\n    dataPresetFields[preset.value] = Object.fromEntries(HUD_METRICS.map(metric => [metric.id,`);
   const raster='/^data:image\\/(?:webp|png|jpeg);base64,[A-Za-z0-9+/]+={0,2}$/.test(value)';
   text=text.replace(raster,`(${bump('imageChecks')}, ${raster})`);
   text=text.replaceAll('{ ...data.values }',`(${bump('valueCopies')}, { ...data.values })`);
   text=text.replace('export function frameTimeCurve(times: Array<number | null>): string[] {',`export function frameTimeCurve(times: Array<number | null>): string[] { ${bump('curveBuilds')};`);
   text=text.replace('export function boundedHudData(data: HudData, stale = data.stale): HudData {',`export function boundedHudData(data: HudData, stale = data.stale): HudData { ${bump('dataBounds')};`);
   return {contents:text,loader:'ts',resolveDir:path.dirname(args.path)};
 });
}});
const entryFor=side=>{
 const base=side==='before'?baseline:root;
 return ['monitorHudModel','monitorHudFreshness','monitorFrameTime'].map(name=>`export * from ${JSON.stringify(path.join(base,'src/bridge',name+'.ts'))};`).join('\n');
};
globalThis.__monitorCpuOps={before:blank(),after:blank()};
async function pure(side){const bundle=await build({stdin:{contents:entryFor(side),resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'esm',plugins:[workPlugin(side)]});return import('data:text/javascript;base64,'+Buffer.from(bundle.outputFiles[0].text).toString('base64'));}
const before=await pure('before'),after=await pure('after'),checks=[],operations={};
function check(label,run){run();checks.push(label);}
let seed=0x169c12;const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return seed>>>0;};
check('complete preference normalization is unchanged across defaults, all presets, legacy and malformed inputs',()=>{
 const presets=after.HUD_DATA_PRESETS.map(p=>p.value);
 const cases=[undefined,null,[],{},after.normalizeMonitorOutput(),{ymcc:{fields:{batteryPercent:true}}},{ymcc:{fields:{clock:true},dataPreset:'detailed',dataPresetVersion:0}}];
 for(let i=0;i<500;i++)cases.push({output:['ymcc','rtss','steam','bad'][random()%4],enabled:random()%3===0?null:!!(random()%2),ymcc:{dataPreset:presets[random()%presets.length],dataPresetVersion:random()%2,layout:after.HUD_LAYOUTS[random()%13].value,scale:(random()%250)/100,underlayPreset:['none','custom','game-logo','tifa','ac8-pink','bad'][random()%6],underlayImage:random()%2?'data:image/png;base64,AAAA':'https://invalid/image',fields:Object.fromEntries(after.HUD_METRICS.map(m=>[m.id,[true,false,null,1][random()%4]])),dataPresetFields:{simple:{fps:false},detailed:{cpuPower:false,clock:true}}}});
 for(const raw of cases)assert.deepEqual(after.normalizeMonitorOutput(raw),before.normalizeMonitorOutput(raw));
});
check('ageing preserves exact values, validity, estimates and array semantics across family deadlines without mutating input',()=>{
 const now=100000,fixture=after.boundedHudData(after.makeHudFixture());
 for(let i=0;i<1000;i++){
   const sample={...structuredClone(fixture),source:i%2?'ymcc-uniform':'fixture',stale:!!(random()%2),freshUntil:{}};
   // Compare the baseline protocol families only. The new independent Windows
   // CPU lease has its own native/publication/wire/expiry regression suite.
   for(const family of before.HUD_FRESH_FAMILIES){const end=[undefined,0,now-1,now,now+1,now+5999,now+6001,Infinity][random()%8];if(end!==undefined)sample.freshUntil[family]=end;}
   sample.batteryPowerEstimated=[true,false,undefined][random()%3];sample.batteryTimeEstimated=[true,false,undefined][random()%3];
   sample.values.batteryPower=[-32,0,30,null,NaN][random()%5];sample.values.batteryTime=[90,null,10081,-1][random()%4];
   if(i%3===0){sample.coreUsage=[];sample.coreLabels=[];sample.coreClocks=[];sample.frameTimes=[];}
   if(i%5===0)delete sample.coreLabels;if(i%7===0)delete sample.values.cpuClock;
   const saved=structuredClone(sample);assert.deepEqual(after.ageHudData(sample,now),before.ageHudData(sample,now));assert.deepEqual(sample,saved);
 }
});
check('unchanged fresh/cleared reads retain owned data; real expiry creates a copy and clears values',()=>{
 const sample=after.boundedHudData({...after.makeHudFixture(),freshUntil:Object.fromEntries(after.HUD_FRESH_FAMILIES.map(f=>[f,106000]))});
 assert.equal(after.ageHudData(sample,100000),sample);const expired=after.ageHudData(sample,106000);assert.notEqual(expired,sample);assert.equal(sample.values.fps,117);assert.equal(expired.values.fps,null);assert.equal(after.ageHudData(expired,106001),expired);
 globalThis.__monitorCpuOps={before:blank(),after:blank()};for(let i=0;i<200;i++){before.ageHudData(sample,100000+i);after.ageHudData(sample,100000+i);}operations.freshValuesCopies=structuredClone(globalThis.__monitorCpuOps);
 assert.equal(operations.freshValuesCopies.before.valueCopies,200);assert.equal(operations.freshValuesCopies.after.valueCopies,0);
});
check('curve coordinates/segments are identical for empty, gaps, malformed and long histories; mutated arrays invalidate cache',()=>{
 const cases=[[],[8],[8,9],[0,null,8,9,NaN,10,12,Infinity],[16.7,1000,.001],Array(200).fill(7)];
 for(let i=0;i<1000;i++)cases.push(Array.from({length:random()%240},()=>[null,NaN,Infinity,-1,0,.01,10,16.7,100,1001][random()%10]));
 const cached=after.createFrameTimeCurveCache();for(const times of cases){assert.deepEqual(after.frameTimeCurve(times),before.frameTimeCurve(times));assert.equal(after.hasFrameTimeCurve(times),before.frameTimeCurve(times).length>0);assert.deepEqual([...cached(times)],before.frameTimeCurve(times));assert.equal(cached([...times]),cached(times));}
 const data=[8,9,10];const first=cached(data);data[0]=16;assert.notEqual(cached(data),first);assert.deepEqual([...cached(data)],before.frameTimeCurve(data));
 globalThis.__monitorCpuOps={before:blank(),after:blank()};const stable=after.createFrameTimeCurveCache(),history=Array.from({length:120},(_,i)=>8+i%4);for(let i=0;i<200;i++){before.frameTimeCurve([...history]);stable([...history]);}operations.curveBuilds=structuredClone(globalThis.__monitorCpuOps);assert.equal(operations.curveBuilds.before.curveBuilds,200);assert.equal(operations.curveBuilds.after.curveBuilds,1);
});
check('bounded raster admission is unchanged and only two valid primitive strings are retained',()=>{
 const a='data:image/png;base64,'+'A'.repeat(1000),b='data:image/webp;base64,'+'B'.repeat(1000),c='data:image/jpeg;base64,'+'C'.repeat(1000);
 for(const raw of [null,undefined,{},'',42,'data:image/svg+xml;base64,AAAA','file:///logo','https://image',a,b,c,a,'data:image/png;base64,'+'A'.repeat(45000),'data:image/png;base64,A!'])assert.equal(after.normalizeHudUnderlayImage(raw),before.normalizeHudUnderlayImage(raw));
 globalThis.__monitorCpuOps={before:blank(),after:blank()};for(let i=0;i<200;i++)for(const value of [a,b]){before.normalizeHudUnderlayImage(value);after.normalizeHudUnderlayImage(value);}operations.imageChecks=structuredClone(globalThis.__monitorCpuOps);assert.equal(operations.imageChecks.before.imageChecks,400);assert(operations.imageChecks.after.imageChecks<=2);
 before.normalizeHudUnderlayImage(c);after.normalizeHudUnderlayImage(c);const n=globalThis.__monitorCpuOps.after.imageChecks;after.normalizeHudUnderlayImage(a);assert.equal(globalThis.__monitorCpuOps.after.imageChecks,n+1);
});
check('preset assembly removes temporary entry arrays while all saved banks/false selections stay identical',()=>{
 const p=before.normalizeMonitorOutput();globalThis.__monitorCpuOps={before:blank(),after:blank()};for(let i=0;i<200;i++){assert.deepEqual(after.normalizeMonitorOutput(p),before.normalizeMonitorOutput(p));}operations.presetEntryArrays=structuredClone(globalThis.__monitorCpuOps);assert(operations.presetEntryArrays.before.presetEntryArrays>30000);assert.equal(operations.presetEntryArrays.after.presetEntryArrays,0);
});
check('renderer reuse admits only exact display input; missing options, mutations and all toggles are detected',()=>{
 const p=after.normalizeMonitorOutput();assert(after.sameHudDisplayInput(p.ymcc,structuredClone(p).ymcc));assert(!after.sameHudDisplayInput(p.ymcc,null));assert(!after.sameHudDisplayInput(p.ymcc,[]));assert(!after.sameHudDisplayInput(p.ymcc,{...p.ymcc,dataPresetVersion:0}));
 for(const key of ['crosshair','position','scale','saturation','opacity','layout','radius','gap','hideUnavailable','fpsEmphasis','underlay','underlayPreset','underlayOpacity','underlayImage']){const raw=structuredClone(p.ymcc);delete raw[key];assert(!after.sameHudDisplayInput(p.ymcc,raw),key);}
 const raw=structuredClone(p.ymcc);for(const metric of after.HUD_METRICS){raw.fields[metric.id]=!raw.fields[metric.id];assert(!after.sameHudDisplayInput(p.ymcc,raw));raw.fields[metric.id]=p.ymcc.fields[metric.id];}
 raw.dataPresetFields.minimal.fps=!raw.dataPresetFields.minimal.fps;assert(after.sameHudDisplayInput(p.ymcc,raw));assert.notDeepEqual(after.normalizeMonitorOutput({...p,ymcc:raw}).ymcc.dataPresetFields,p.ymcc.dataPresetFields);
});
check('visibility, auto-hide and FPS-family selection retain exact baseline semantics',()=>{
 const base=after.makeHudFixture();for(let i=0;i<300;i++){
  const data={...base,stale:!!(random()%2),values:{...base.values,fps:[0,.9,1,89,null,NaN][random()%6]},frameTimes:i%2?[8,9]:[8,null,9]};
  const fields=Object.fromEntries(after.HUD_METRICS.map(m=>[m.id,!!(random()%2)]));
  for(const hide of [false,true])for(const ready of [false,true])assert.deepEqual(after.hudVisibleFields(fields,data,hide,ready),before.hudVisibleFields(fields,data,hide,ready));
 }
});
const htmls={};
for(const side of ['before','after']){
 const base=side==='before'?baseline:root;
 const entry=`import {mountMonitorHud} from ${JSON.stringify(path.join(base,'src/bridge/monitorHudRenderer.ts'))};import {normalizeMonitorOutput,makeHudFixture,HUD_LAYOUTS,HUD_METRICS} from ${JSON.stringify(path.join(base,'src/bridge/monitorHudModel.ts'))};import {nextTick} from 'vue';
 let prefs=normalizeMonitorOutput(),data=makeHudFixture();const container=document.getElementById('surface');const owner=mountMonitorHud(container,{preferences:prefs,data});window.bootWork={...globalThis.__monitorCpuOps.${side}};
 window.configure=async(layout,detailed,width,image)=>{container.style.width=width+'px';prefs=normalizeMonitorOutput({ymcc:{layout,underlayPreset:'custom',underlayImage:image,underlayOpacity:.4,fields:detailed?Object.fromEntries(HUD_METRICS.map(m=>[m.id,m.id!=='clock'])):prefs.ymcc.fields}});data=makeHudFixture();data.frameTimes=Array.from({length:120},(_,i)=>8+i%4);owner.update(prefs,data);await nextTick();};
 window.replay=async()=>{globalThis.__monitorCpuOps.${side}=${JSON.stringify(blank())};for(let i=0;i<80;i++){data={...data,values:{...data.values,fps:111+i%2,cpuPower:28+i%2/10},frameTimes:[...data.frameTimes]};owner.update(structuredClone(prefs),data);await nextTick();}return {...globalThis.__monitorCpuOps.${side}};};
 window.change=async()=>{prefs=normalizeMonitorOutput({...prefs,ymcc:{...prefs.ymcc,opacity:.23,scale:1.4,fpsEmphasis:false,fields:{...prefs.ymcc.fields,cpuVoltage:true}}});owner.update(prefs,data);await nextTick();};window.dispose=()=>owner.dispose();window.ready=true;`;
 const bundle=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',plugins:[workPlugin(side),monitorHudVuePlugin(root)]});
 htmls[side]=`<!doctype html><html><head><style>html,body{margin:0;background:#253041}#surface{width:1920px}</style></head><body><div id="surface"></div><script>globalThis.__monitorCpuOps={before:${JSON.stringify(blank())},after:${JSON.stringify(blank())}};</script><script>${bundle.outputFiles[0].text.replaceAll('</script','<\\/script')}</script></body></html>`;
 fs.writeFileSync(path.join(out,side+'.html'),htmls[side]);
}
const server=createServer((request,response)=>{response.setHeader('Content-Type','text/html;charset=utf-8');response.end(htmls[request.url?.includes('before')?'before':'after']);});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(path.join(runtime,'_monitor-cpu-offline.cjs'))('playwright');let browser;const errors=[],browserCases=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const pages={};for(const side of ['before','after']){pages[side]=await browser.newPage({viewport:{width:1920,height:1080}});pages[side].on('pageerror',error=>errors.push(side+': '+error));await pages[side].goto('http://127.0.0.1:'+server.address().port+'/'+side);await pages[side].waitForFunction(()=>window.ready);}
 const startup={};for(const side of ['before','after'])startup[side]=await pages[side].evaluate(()=>window.bootWork);assert.equal(startup.before.dataBounds,2);assert.equal(startup.after.dataBounds,1);operations.rendererStartup=startup;
 const imagePath=path.resolve(task,'../game-logo-20261014/PREVIEW-LOGO.txt');const image=fs.existsSync(imagePath)?fs.readFileSync(imagePath,'utf8').trim():'';
 const shape=page=>page.locator('[data-monitor-hud]').evaluate(el=>{const r=el.getBoundingClientRect(),css=getComputedStyle(el);return {text:el.innerText,width:r.width,height:r.height,color:css.color,background:css.backgroundColor,zoom:css.zoom,metrics:[...el.querySelectorAll('[data-hud-metric]')].map(m=>[m.dataset.hudMetric,m.innerText]),curve:[...el.querySelectorAll('polyline')].map(n=>n.getAttribute('points')),coreTiles:el.querySelectorAll('.hud-core-track').length,image:el.querySelector('img')?.getAttribute('src')??''};});
 for(const layout of after.HUD_LAYOUTS)for(const detailed of [false,true]){
   for(const side of ['before','after'])await pages[side].evaluate(([l,d,w,img])=>window.configure(l,d,w,img),[layout.value,detailed,1920,image]);
   await Promise.all(Object.values(pages).map(page=>page.waitForTimeout(70)));assert.deepEqual(await shape(pages.after),await shape(pages.before),layout.value+'/'+detailed);browserCases.push(layout.value+'/'+(detailed?'detailed':'simple'));
 }
 for(const side of ['before','after'])await pages[side].evaluate(img=>window.configure('circuit',true,1280,img),image);
 await Promise.all(Object.values(pages).map(page=>page.waitForTimeout(70)));assert.deepEqual(await shape(pages.after),await shape(pages.before));browserCases.push('1280 width/circuit/detailed');
 const rendererWork={};for(const side of ['before','after'])rendererWork[side]=await pages[side].evaluate(()=>window.replay());operations.rendererUpdates=rendererWork;
 assert.equal(rendererWork.before.normalizations,80);assert.equal(rendererWork.after.normalizations,0);assert(rendererWork.before.curveBuilds>=80);assert.equal(rendererWork.after.curveBuilds,0);assert.deepEqual(await shape(pages.after),await shape(pages.before));browserCases.push('80 numeric updates: unchanged output/geometry, no repeat preset normalization or curve build');
 for(const side of ['before','after'])await pages[side].evaluate(()=>window.change());await Promise.all(Object.values(pages).map(page=>page.waitForTimeout(70)));assert.deepEqual(await shape(pages.after),await shape(pages.before));browserCases.push('appearance change: 140% scale, opacity, FPS emphasis and field update');
 const pixelHashes={};for(const side of ['before','after']){const bytes=await pages[side].locator('[data-monitor-hud]').screenshot({path:path.join(out,side+'-renderer.png')});pixelHashes[side]=crypto.createHash('sha256').update(bytes).digest('hex');}assert.equal(pixelHashes.before,pixelHashes.after);operations.pixelHashes=pixelHashes;browserCases.push('detailed circuit at 140%: exact screenshot pixels match the baseline');
 for(const side of ['before','after'])await pages[side].evaluate(()=>window.dispose());assert.deepEqual(errors,[]);
 checks.push('all 13 Vue layouts, simple/detailed fields, 1280 width and appearance updates match the complete baseline renderer');
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
const result={ok:true,checks,browserCases,operations,scope:'Actual production before/current functions and Vue renderer; synthetic fixtures, deterministic operation counts only. No CPU percentage/time benchmark, hardware collector, installed application or game acceptance.'};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,checks:checks.length,browserCases:browserCases.length,operations},null,2));