/** Actual GyroMotionView + child SFCs in Chromium. Only IO is mocked.
 * No YMCC process, device, calibration or settings action is performed.
 * Run: node tools/gyro_xyz_format_browser_selftest.mjs [outDir | --out outDir]
 * Output precedence: CLI directory, YMCC_GYRO_XYZ_OUT, timestamped default.
 * Optional: YMCC_GYRO_XYZ_OUT, YMCC_UI_BROWSER_MODULE_DIR, YMCC_UI_BROWSER_EXE.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(path.join(root, 'package.json'));
const { build } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_gyro-xyz-selftest.cjs'))('playwright');
const evidenceRoot = 'G:/YeManCC-Work/Build/Validation/Controller-Status-G3E-20261021/gyro';
const args = process.argv.slice(2);
let cliOut;
for (let index = 0; index < args.length; index++) {
  const arg = args[index];
  let value;
  if (arg === '--out') value = args[++index];
  else if (arg.startsWith('--out=')) value = arg.slice('--out='.length);
  else {
    assert(!arg.startsWith('-'), 'unknown option: ' + arg);
    value = arg;
  }
  assert(value && !value.startsWith('--'), 'output directory is required');
  assert(cliOut === undefined, 'specify the output directory only once');
  cliOut = value;
}
const out = path.resolve(cliOut || process.env.YMCC_GYRO_XYZ_OUT || path.join(evidenceRoot, 'runs', new Date().toISOString().replace(/[:.]/g, '-')));
assert(!fs.existsSync(path.join(out, 'results.json')), 'use a fresh evidence directory');
fs.mkdirSync(out, { recursive: true });
const sources = new Map(), errors = [], cases = [], screenshots = [], baselines = [];
const tracked = ['src/views/GyroMotionView.vue', 'src/components/GyroStatusPreview.vue', 'src/bridge/gyroAxisFormat.ts', 'tools/gyro_presentation_selftest.ts', 'tools/gyro_xyz_format_browser_selftest.mjs'];
function source(filename) {
  const relative = path.relative(root, filename).replaceAll('\\', '/');
  if (!sources.has(relative)) {
    const data = fs.readFileSync(filename);
    sources.set(relative, { data, sha256: createHash('sha256').update(data).digest('hex') });
    const copy = path.join(out, 'sources', relative);
    fs.mkdirSync(path.dirname(copy), { recursive: true });
    fs.writeFileSync(copy, data);
  }
  return sources.get(relative).data.toString('utf8');
}
for (const file of tracked) source(path.join(root, file));
const mocks = {
  '@/bridge/gameInputEditor': `import {loadSettings} from '@/bridge/settingsRepository';export async function readGameInputEditor(){return {input:(await loadSettings()).input,target:{exe:'',pid:0,processCreated:'',generation:1},ownerEpoch:1,gameName:''}};export async function saveGameInputEditor(){window.__fixture.writes.push('saveGameInputEditor');throw Error('unexpected settings write')};export function onGameInputEditorChanged(){return ()=>{}}`,
  '@/bridge/ipc': `export const isNativeRuntime=true;export function getLastGamepadState(){return null};export function on(name,cb){const list=window.__fixture.events[name]??=new Set();list.add(cb);return ()=>list.delete(cb)};export async function invoke(method,args){window.__fixture.requests.push({method,args});if(method==='input.shortcutRuntime.get')return {active:false};if(method==='gyro.calibrate.status')return {status:'not-calibrated',trusted:false};throw Error('unexpected fixture IPC: '+method)}`,
  '@/bridge/settingsRepository': `export {mergeSettings} from ${JSON.stringify(path.join(root, 'src/bridge/settingsRepository.ts'))};export function onInputSettingsChanged(){return ()=>{}};export async function loadSettings(){return structuredClone(window.__fixture.settings)};export async function compareAndSwapInputSettings(){window.__fixture.writes.push('compareAndSwapInputSettings');throw Error('unexpected settings write')}`,
  '@/bridge/performanceSchedule': `export async function loadGameCustomConfig(){return {entries:{},rtss:{}}};export async function removeGameCustomProfile(){throw Error('profile removal outside display fixture')};export async function restorePerformanceScheduleIfConfigured(){throw Error('performance restore outside display fixture')}`,
  '@/bridge/gameInputOverride': `export function getGameInputOverrideState(){return {locked:false,identity:''}};export function subscribeGameInputOverrideState(cb){cb(getGameInputOverrideState());return ()=>{}}`,
  '@/bridge/api': `export const fs={},settingsStore={},shell={open:async()=>{throw Error('unexpected shell action')}},windowApi={controllerEditorWidth:async()=>{throw Error('unexpected window action')}}`,
  '@/bridge/gameproc': `export async function closeJoyxoffIfRunning(){};export async function getGameInputRedistState(){return {mouseInterfaceAvailable:true}}`,
};
const fixture = `
import {createApp,nextTick} from 'vue';
import Gyro from './src/views/GyroMotionView.vue';
import {normalizeSettings} from './src/bridge/settingsRepository.ts';
window.__fixture={settings:normalizeSettings({input:{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false,preset:'fps',outputMode:'virtual-stick',motionMode:'on'}}}),events:{},requests:[],writes:[]};
const app=createApp(Gyro);app.mount('#app');
let sequence=0;
window.__gyroXYZ={
 async feed(values,direct=false){
  if(direct)window.__gyroAxisRef.value={x:values[0],y:values[1],z:values[2]};
  else window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:++sequence,timestampUtc:'2026-10-21T00:00:00Z',sensorPresent:true,gyro:{x:values[0],y:values[1],z:values[2]},accel:{x:0,y:0,z:1},orientation:{w:1,x:0,y:0,z:0}}}));
  await new Promise(requestAnimationFrame);await nextTick();
 },ready:true
};`;
const builds = {};
async function bundle(label) {
  const styles = [];
  const built = await build({stdin:{contents:fixture,resolveDir:root,loader:'ts'},bundle:true,write:false,loader:{'.png':'dataurl','.svg':'dataurl'},format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'real-gyro-xyz-sfcs',setup(b){
    b.onResolve({filter:/.*/},args=>{
      if(args.path in mocks)return {path:args.path,namespace:'fixture'};
      if(args.path.startsWith('.')&&path.isAbsolute(args.importer)){
        const target=path.resolve(path.dirname(args.importer),args.path).replace(/\.ts$/,'');
        for(const name of ['ipc','api','gameInputEditor','gameInputOverride','performanceSchedule'])if(target===path.join(root,'src/bridge',name))return {path:'@/bridge/'+name,namespace:'fixture'};
      }
    });
    b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
    b.onLoad({filter:/\.(?:ts|json)$/},args=>{
      if(args.path.startsWith(path.join(root,'src')+path.sep))return {contents:source(args.path),loader:path.extname(args.path).slice(1),resolveDir:path.dirname(args.path)};
    });
    b.onLoad({filter:/\.vue$/},args=>{
      const relative=path.relative(root,args.path).replaceAll('\\','/');
      const saved=path.join(evidenceRoot,'before',relative);
      const text=label==='before'&&['src/views/GyroMotionView.vue','src/components/GyroStatusPreview.vue'].includes(relative)?fs.readFileSync(saved,'utf8'):source(args.path);
      const id='data-v-'+createHash('sha256').update(relative).digest('hex').slice(0,8);
      const {descriptor,errors}=parse(text,{filename:args.path});assert.deepEqual(errors,[]);
      let script=compileScript(descriptor,{id,inlineTemplate:true}).content;
      if(relative==='src/views/GyroMotionView.vue'){
        const anchor='const gyroAxes = ref({ x: 0, y: 0, z: 0 });';
        assert(script.includes(anchor),'actual axes ref instrumentation anchor');
        // Expose only the real ref to exercise invalid display values directly.
        // Normal telemetry still goes through the real normalizer and RAF gate.
        script=script.replace(anchor,anchor+'\nwindow.__gyroAxisRef = gyroAxes;');
      }
      for(const style of descriptor.styles){const result=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(result.errors,[]);styles.push(result.code)}
      return {contents:script.replace('export default','const component =')+'\ncomponent.__scopeId='+JSON.stringify(id)+';export default component;',loader:'ts',resolveDir:path.dirname(args.path)};
    });
  }}]});
  const css=['src/styles/tokens.css','src/styles/integratedConsole.css'].map(file=>source(path.join(root,file))).join('\n')+'\n'+styles.join('\n');
  const js=built.outputFiles[0].text;
  fs.writeFileSync(path.join(out,label+'.js'),js);
  fs.writeFileSync(path.join(out,label+'.css'),css);
  builds[label]={js,css};
}
let browser,server,passed=false,failure=null;
try {
  // An existing exact working-tree snapshot gives a geometry regression check.
  if(fs.existsSync(path.join(evidenceRoot,'before/src/views/GyroMotionView.vue')))await bundle('before');
  await bundle('after');
  server=createServer((request,response)=>{
    const label=request.url.startsWith('/before')?'before':'after';
    response.setHeader('Content-Type',request.url.endsWith('.js')?'text/javascript; charset=utf-8':'text/html; charset=utf-8');
    if(request.url.endsWith('.js')){response.end(builds[label].js);return}
    response.end(`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Actual gyro XYZ validation</title><style>${builds[label].css}\n*{box-sizing:border-box}body{margin:0;background:var(--bg-solid);color:var(--text);font-family:'Segoe UI',sans-serif}.app-root{display:flex;height:100vh}.fixture-nav{width:var(--nav-w);flex:none;border-right:1px solid var(--border);padding:18px 12px}.app-main{flex:1;min-width:0}.fixture-monitor{height:52px;border-bottom:1px solid var(--border);padding:16px}.app-content{height:calc(100vh - 52px);overflow-y:auto;scrollbar-gutter:stable}</style><div class="app-root"><aside class="fixture-nav">YMCC<br>陀螺仪</aside><div class="app-main"><div class="fixture-monitor">Gyro XYZ · ${label}</div><main class="app-content"><div id="app"></div></main></div></div><script src="/${label}.js"></script></html>`);
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin='http://127.0.0.1:'+server.address().port;
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',args:['--disable-background-networking','--disable-component-update']});
  const open=async(label,height)=>{
    const page=await browser.newPage({viewport:{width:1280,height},deviceScaleFactor:1});
    page.on('pageerror',error=>errors.push(String(error)));
    await page.route('**/*',route=>route.request().url().startsWith(origin+'/')?route.continue():route.abort());
    await page.goto(origin+'/'+label);
    await page.waitForFunction(()=>window.__gyroXYZ?.ready&&window.__fixture.requests.some(r=>r.method==='input.shortcutRuntime.get')&&document.querySelectorAll('.gyro-rate-values b').length===3);
    return page;
  };
  const observe=page=>page.evaluate(()=>{
    const rect=node=>{const r=node.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom}};
    const preview=document.querySelector('.gyro-status-preview'),numbers=[...document.querySelectorAll('.gyro-rate-values b')];
    const geometry=['.gyro-integrated-page','.gyro-overview-card','.gyro-status-preview','.gyro-space-scene','.gyro-space-caption','.gyro-power-row','.gyro-preset-field'];
    return {geometry:Object.fromEntries(geometry.map(s=>[s,rect(document.querySelector(s))])),numbers:numbers.map(node=>{
      const range=document.createRange();range.selectNodeContents(node);const dot=document.createRange();const index=node.textContent.indexOf('.');if(index>=0){dot.setStart(node.firstChild,index);dot.setEnd(node.firstChild,index+1)}
      return {text:node.textContent,box:rect(node),glyphs:rect(range),axis:rect(node.parentElement),decimal:index>=0?rect(dot):null,font:getComputedStyle(node).fontFamily,variant:getComputedStyle(node).fontVariantNumeric,whiteSpace:getComputedStyle(node).whiteSpace};
    }),preview:rect(preview),writes:window.__fixture.writes,requests:window.__fixture.requests.map(r=>r.method)};
  });
  const near=(a,b,message)=>assert(Math.abs(a-b)<0.1,`${message}: ${a} != ${b}`);
  const sameRect=(a,b,name)=>{for(const key of ['x','y','width','height'])near(a[key],b[key],name+'.'+key)};
  const normal=[
    {name:'minus-999.9',value:-999.9,text:'-999.9'}, {name:'minus-0.1',value:-0.1,text:'  -0.1'},
    {name:'zero',value:0,text:'   0.0'}, {name:'9.9',value:9.9,text:'   9.9'},
    {name:'99.9',value:99.9,text:'  99.9'}, {name:'999.9',value:999.9,text:' 999.9'},
    {name:'minus-1234.5',value:-1234.5,text:'-1234.5'}, {name:'12345.6',value:12345.6,text:'12345.6'},
    {name:'minus-999999.9',value:-999999.9,text:'-999999.9'}, {name:'round-up',value:999999.99,text:'1000000.0'},
    {name:'negative-round-up',value:-999999.99,text:'-1.0e+6'},
    {name:'1e21',value:1e21,text:'1.0e+21'}, {name:'max-finite',value:Number.MAX_VALUE,text:'1.8e+308'},
    {name:'minus-max-finite',value:-Number.MAX_VALUE,text:'-1.8e+308'},
  ];
  for(const height of [720,800]){
    const page=await open('after',height);
    await page.evaluate(()=>window.__gyroXYZ.feed([0,0,0]));
    const initial=await observe(page);
    if(builds.before){
      const before=await open('before',height);await before.evaluate(()=>window.__gyroXYZ.feed([0,0,0]));const old=await observe(before);
      for(const selector of Object.keys(initial.geometry))sameRect(initial.geometry[selector],old.geometry[selector],'unchanged '+selector);
      baselines.push({viewport:{width:1280,height},before:old.geometry,after:initial.geometry});await before.close();
    }
    const validate=(row,label,expected,decimal=true)=>{
      assert.equal(row.numbers.length,3);
      for(let axis=0;axis<3;axis++){
        const n=row.numbers[axis],original=initial.numbers[axis];
        if(expected)assert.equal(n.text,expected[axis],label+' text');
        sameRect(n.box,original.box,label+' number '+axis);sameRect(n.axis,original.axis,label+' label '+axis);
        assert(n.glyphs.x>=n.box.x-0.1&&n.glyphs.right<=n.box.right+0.1,label+' glyphs fit number slot');
        assert(n.glyphs.y>=n.box.y-0.1&&n.glyphs.bottom<=n.box.bottom+0.1,label+' full glyph height visible');
        assert(n.box.x>=row.preview.x&&n.box.right<=row.preview.right,label+' number inside preview');
        assert(n.box.bottom<=height,label+' number visible in viewport');
        assert.equal(n.variant,'tabular-nums');assert.equal(n.whiteSpace,'pre');
        if(decimal&&n.decimal)near(n.decimal.x,original.decimal.x,label+' decimal column '+axis);
      }
      for(const selector of Object.keys(initial.geometry))sameRect(row.geometry[selector],initial.geometry[selector],label+' stable '+selector);
      assert.deepEqual(row.writes,[],'no settings writes');
    };
    for(const item of normal){
      await page.evaluate(value=>window.__gyroXYZ.feed([value,value,value]),item.value);
      const row=await observe(page);validate(row,item.name,[item.text,item.text,item.text],!item.text.includes('e'));
      cases.push({viewport:{width:1280,height},name:item.name,input:String(item.value),observation:row});
      if(['minus-999.9','zero','minus-1234.5','minus-max-finite'].includes(item.name)){
        const name=`1280x${height}-${item.name}.png`;await page.screenshot({path:path.join(out,name)});screenshots.push(name);
      }
    }
    // Mixed values also prove that one axis cannot push its neighbors.
    await page.evaluate(()=>window.__gyroXYZ.feed([-999.9,0,99.9]));let row=await observe(page);validate(row,'mixed',['-999.9','   0.0','  99.9']);cases.push({viewport:{width:1280,height},name:'mixed',observation:row});
    // The actual telemetry normalizer maps nonfinite input to zero.
    await page.evaluate(()=>window.__gyroXYZ.feed([NaN,Infinity,-Infinity]));row=await observe(page);validate(row,'nonfinite telemetry',['   0.0','   0.0','   0.0']);cases.push({viewport:{width:1280,height},name:'nonfinite-telemetry',observation:row});
    // Exercise the formatter's fallback in the real Vue slot as well.
    await page.evaluate(()=>window.__gyroXYZ.feed([NaN,Infinity,-Infinity],true));row=await observe(page);validate(row,'nonfinite display',['     \u2014','     \u2014','     \u2014'],false);cases.push({viewport:{width:1280,height},name:'nonfinite-display',observation:row});
    await page.evaluate(()=>window.__gyroXYZ.feed([-0,-0.01,Number.MIN_VALUE]));row=await observe(page);validate(row,'rounded zero',['   0.0','   0.0','   0.0']);cases.push({viewport:{width:1280,height},name:'rounded-zero',observation:row});
    await page.close();
  }
  assert.deepEqual(errors,[],'browser errors');
  for(const file of tracked)assert.equal(createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex'),sources.get(file).sha256,'source changed during test: '+file);
  passed=true;
} catch(error){failure=error.stack||String(error);process.exitCode=1;console.error(failure)}
finally {
  if(browser)await browser.close();
  if(server)await new Promise(resolve=>server.close(resolve));
  fs.writeFileSync(path.join(out,'source-bindings.json'),JSON.stringify([...sources].map(([file,s])=>({path:file,sha256:s.sha256})),null,2));
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({passed,failure,scope:'actual Vue SFCs; isolated Chromium; memory IO only; no device actions',viewports:['1280x720','1280x800'],cases,baselines,screenshots,errors},null,2));
  console.log(`gyro XYZ browser selftest ${passed?'PASS':'FAIL'}: ${cases.length} cases; ${out}`);
}
