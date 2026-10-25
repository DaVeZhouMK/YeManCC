// Complete before/current production SFCs and renderer in equal browser documents.
// Counts work, not CPU% or timing. No hardware, Steam, installed app or game I/O.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
import {createRequire} from 'node:module';import {createServer} from 'node:http';
const root=process.cwd(),task=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-cpu-optimization-20261011'),out=path.join(task,'browser');fs.mkdirSync(out,{recursive:true});
const req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(path.join(runtime,'_cpu-equivalence.cjs'))('playwright');
const beforeRenderer=path.join(task,'before/src/bridge/monitorHudRenderer.ts');
const entry=`import {mountMonitorHud as mountBefore} from ${JSON.stringify(beforeRenderer.replaceAll('\\','/'))};import {mountMonitorHud as mountAfter} from './src/bridge/monitorHudRenderer.ts';
import {makeHudFixture,normalizeMonitorOutput,HUD_METRICS} from './src/bridge/monitorHudModel.ts';import {nextTick} from 'vue';
window.__cpuOps={before:{updates:0,groups:0,style:0,visibility:0,other:0},after:{updates:0,groups:0,style:0,visibility:0,other:0}};
let preferences=normalizeMonitorOutput(),data=makeHudFixture();const dimensions={before:[],after:[]};
for(const frame of document.querySelectorAll('iframe')){const doc=frame.contentDocument;doc.documentElement.style.background='#253041';doc.body.style.margin='0';doc.body.innerHTML='<div id="surface" style="width:700px"></div>';}
const one=mountBefore(document.querySelector('#before').contentDocument.querySelector('#surface'),{preferences,data,onSize:(w,h)=>dimensions.before.push([w,h])});
const two=mountAfter(document.querySelector('#after').contentDocument.querySelector('#surface'),{preferences,data,onSize:(w,h)=>dimensions.after.push([w,h])});
window.__set=async(layout,scale,hide,mode)=>{
 preferences=normalizeMonitorOutput({ymcc:{layout,scale,hideUnavailable:hide,fields:Object.fromEntries(HUD_METRICS.map(m=>[m.id,true]))}});data=makeHudFixture();
 if(mode==='empty')data={...data,values:{},coreUsage:[],coreClocks:[],frameTimes:[]};
 if(mode==='zero')data={...data,values:{fps:0,cpuPower:0,gpuUsage:0,batteryPower:0},coreUsage:[0,null],coreClocks:[],frameTimes:[]};
 if(mode==='estimate')data={...data,values:{batteryPercent:75,batteryPower:-24},batteryPowerEstimated:true,coreUsage:[],coreClocks:[],frameTimes:[]};
 one.update(preferences,data);two.update(preferences,data);await nextTick();
};
window.__reset=()=>{for(const name of ['before','after'])window.__cpuOps[name]={updates:0,groups:0,style:0,visibility:0,other:0}};
window.__sample=async(change=false)=>{const next=structuredClone(data);if(change){next.values.fps=Number(data.values.fps||0)+1;data=next;}one.update(structuredClone(preferences),next);two.update(structuredClone(preferences),next);await nextTick();};
window.__mutate=()=>{data.values.fps=9999;data.coreUsage[0]=99;preferences.ymcc.opacity=.01;};
window.__applyMutated=async()=>{one.update(preferences,data);two.update(preferences,data);await nextTick();};
window.__position=async()=>{preferences.ymcc.position=preferences.ymcc.position===1?2:1;const old=[dimensions.before.length,dimensions.after.length];one.update(preferences,data);two.update(preferences,data);await nextTick();return [dimensions.before.length-old[0],dimensions.after.length-old[1]];};
window.__measure=()=>{const old=[dimensions.before.length,dimensions.after.length];one.measure();two.measure();return [dimensions.before.length-old[0],dimensions.after.length-old[1]];};
window.__dispose=()=>{one.dispose();two.dispose()};window.__dimensions=()=>dimensions;`;
const bundled=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,platform:'browser',format:'iife',write:false,nodePaths:[path.join(root,'node_modules')],define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'exact-source-sfc-work-counts',setup(b){
 b.onResolve({filter:/^\.\/monitorCoreTiles$/},args=>({path:path.join(root,'src/bridge/monitorCoreTiles.ts')}));
 b.onResolve({filter:/^@\//},args=>({path:path.resolve(root,'src',args.path.slice(2)+(/\.[a-z]+$/i.test(args.path)?'':'.ts'))}));
 b.onLoad({filter:/\.vue$/},args=>{
  const variant=args.path.replaceAll('\\','/').includes('/before/')?'before':'after';const source=fs.readFileSync(args.path,'utf8');let {descriptor}=parse(source,{filename:args.path});const id='cpu-equivalence-'+variant;
  assert(descriptor.scriptSetup&&descriptor.scriptSetup.content.includes('computed,'));
  const originalSetup=descriptor.scriptSetup.content;const instrumentedSetup=descriptor.scriptSetup.content.replace('computed,','computed as __computed, onUpdated,')+`\nonUpdated(()=>{window.__cpuOps[${JSON.stringify(variant)}].updates++;});\nfunction computed<T>(getter:(previous?:T)=>T){return __computed((previous?:T)=>{const text=getter.toString();const key=text.includes('groupNames')?'groups':text.includes('fontSize')?'style':text.includes('hudVisibleFields')?'visibility':'other';window.__cpuOps[${JSON.stringify(variant)}][key]++;return getter(previous);});}\n`;
  descriptor=parse(source.replace(originalSetup,instrumentedSetup),{filename:args.path}).descriptor;
  const script=compileScript(descriptor,{id,inlineTemplate:true});const css=descriptor.styles.map(style=>{const compiled=compileStyle({source:style.content,filename:args.path,id:'data-v-'+id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);return compiled.code}).join('\n');
  return {contents:script.content.replace('export default ','const __component = ')+'\n__component.__scopeId='+JSON.stringify('data-v-'+id)+';__component.__ymccHudCss='+JSON.stringify(css)+';export default __component;',loader:'ts',resolveDir:path.dirname(args.path)};
 });
}}]});
const html='<!doctype html><meta charset="utf-8"><title>TEST ONLY — work-count and pixel equivalence</title><style>html,body{margin:0;background:#253041}main{position:relative;height:2200px}iframe{position:absolute;left:0;top:0;border:0;width:700px;height:2200px;opacity:0}iframe:first-child{opacity:1}</style><main><iframe id="before"></iframe><iframe id="after"></iframe></main><script src="/test.js"></script>';
fs.writeFileSync(path.join(out,'index.html'),html);fs.writeFileSync(path.join(out,'test.js'),bundled.outputFiles[0].text);
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/test.js'?'text/javascript':'text/html;charset=utf-8');r.end(q.url==='/test.js'?bundled.outputFiles[0].text:html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;const visuals=[],work=[],errors=[];
const wait=()=>new Promise(r=>setTimeout(r,90));
async function inspect(frame){return frame.locator('[data-monitor-hud]').evaluate(el=>{const r=el.getBoundingClientRect(),style=getComputedStyle(el);return {text:el.textContent,width:r.width,height:r.height,visibility:style.visibility,background:style.backgroundColor,font:style.fontSize,zoom:style.zoom,groups:[...el.querySelectorAll('[data-hud-group]')].map(e=>e.getAttribute('data-hud-group')),fields:[...el.querySelectorAll('[data-hud-metric]')].map(e=>[e.getAttribute('data-hud-metric'),e.textContent]),tiles:[...el.querySelectorAll('[data-core-state]')].map(e=>[e.getAttribute('data-core-state'),e.firstElementChild?.getAttribute('style')]),svg:[...el.querySelectorAll('polyline')].map(e=>e.getAttribute('points')),aria:[...el.querySelectorAll('[aria-label]')].map(e=>e.getAttribute('aria-label'))};});}
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:1420,height:2300}});page.on('pageerror',e=>errors.push(String(e)));await page.goto('http://127.0.0.1:'+server.address().port+'/');await page.waitForFunction(()=>window.__sample);
 const [before,after]=page.frames().filter(f=>f!==page.mainFrame());assert(before&&after);
 for(const layout of ['corner','capsule','bar'])for(const scale of [1,1.4,2])for(const hide of [false,true])for(const mode of ['full','empty','zero','estimate']){
  await page.evaluate(args=>__set(...args),[layout,scale,hide,mode]);await wait();const a=await inspect(before),b=await inspect(after);assert.deepEqual(b,a,'render behavior changed: '+JSON.stringify({layout,scale,hide,mode}));
  if(a.visibility!=='hidden'){await page.evaluate(()=>{document.querySelector('#before').style.opacity='1';document.querySelector('#after').style.opacity='0';});await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));const left=await before.locator('[data-monitor-hud]').screenshot();
  await page.evaluate(()=>{document.querySelector('#before').style.opacity='0';document.querySelector('#after').style.opacity='1';});await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));const right=await after.locator('[data-monitor-hud]').screenshot();if(!left.equals(right)){fs.writeFileSync(path.join(out,'failure-before.png'),left);fs.writeFileSync(path.join(out,'failure-after.png'),right);fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify({layout,scale,hide,mode,a,b,ops:await page.evaluate(()=>__cpuOps)},null,2));}
  assert.equal(crypto.createHash('sha256').update(right).digest('hex'),crypto.createHash('sha256').update(left).digest('hex'),'pixel difference '+JSON.stringify({layout,scale,hide,mode}));}
  visuals.push({layout,scale,hide,mode,width:a.width,height:a.height});
 }
 for(const hide of [false,true]){
  await page.evaluate(hide=>__set('corner',1,hide,'full'),hide);await wait();await page.evaluate(()=>__reset());
  for(let i=0;i<50;i++)await page.evaluate(()=>__sample(false));await wait();let counts=await page.evaluate(()=>structuredClone(__cpuOps));assert(counts.before.updates>=50);assert.equal(counts.after.updates,0);assert.equal(counts.after.style,0);assert.equal(counts.after.groups,0);work.push({mode:'50 identical frames',hide,counts});
  await page.evaluate(()=>__reset());for(let i=0;i<50;i++)await page.evaluate(()=>__sample(true));await wait();counts=await page.evaluate(()=>structuredClone(__cpuOps));assert(counts.before.updates>=50&&counts.after.updates>=50);assert(counts.before.style>=50&&counts.before.groups>=50);assert.equal(counts.after.style,0);assert.equal(counts.after.groups,0);assert.deepEqual(await inspect(after),await inspect(before));work.push({mode:'50 changing FPS frames',hide,counts});
 }
 await page.evaluate(()=>__set('corner',1,false,'full'));await wait();const owned=await inspect(after);await page.evaluate(()=>__mutate());await wait();assert.deepEqual(await inspect(after),owned,'caller mutation leaked into owned renderer state');await page.evaluate(()=>__applyMutated());await wait();assert.deepEqual(await inspect(after),await inspect(before));assert((await inspect(after)).text.includes('9999'));
 const position=await page.evaluate(()=>__position());assert(position.every(n=>n>0),'same-size position changes must still reach native/Steam geometry callbacks');assert.deepEqual(await page.evaluate(()=>__measure()),[1,1],'explicit geometry measurement must remain forceable');
 await page.evaluate(()=>__dispose());assert.equal(await before.locator('[data-monitor-hud]').count(),0);assert.equal(await after.locator('[data-monitor-hud]').count(),0);assert.equal(await before.locator('[data-ymcc-hud-style]').count(),0);assert.equal(await after.locator('[data-ymcc-hud-style]').count(),0);assert.deepEqual(errors,[]);
 const result={ok:true,visualCases:visuals.length,pixelAndSemanticEquivalence:true,work,copyIsolation:true,sameSizePositionCallbacks:true,forceMeasure:true,dispose:true,errors,timingOrCpuUsageMeasured:false,scope:'complete production before/current SFC+renderer; equal browser documents and injected packets, no real hardware/Steam/game acceptance'};fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify({...result,visuals},null,2));console.log(JSON.stringify(result,null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r));}
