/** Actual Vue SFC / Chromium display A-B. All IPC is memory-only; no YMCC or hardware is started. */
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
const repo = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(resolve(repo, 'package.json'));
const { build } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const packageRoot = process.env.CPU_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(resolve(packageRoot, '_cpu-bench.cjs'))('playwright');
const output = resolve(repo, 'Build/Validation/CPU-Frontend-20261003');
const before = resolve(output, 'before/src/components/GamepadVisualizer.vue');
const after = resolve(repo, 'src/components/GamepadVisualizer.vue');
if (!existsSync(before)) throw new Error('Missing exact pre-edit SFC backup. Do not reconstruct an invented baseline.');
mkdirSync(output, { recursive: true });
const fixture = (component) => `
import { createApp, defineComponent, h, ref, KeepAlive, nextTick } from 'vue';
import Component from ${JSON.stringify(component)};
const listeners = window.__nativeListeners;
let active = ref(true), timer = null, phase = 0;
const state = () => ({ connected:true, name:'ROG memory-only fixture', buttons:Array(16).fill(false), axes:[0,0,0,0] });
const send = (type, data) => { for (const fn of listeners) fn(new MessageEvent('message', { data:{event:type,data} })); };
const Empty = defineComponent({ render:() => h('div', 'inactive route') });
createApp({ render:() => h(KeepAlive, null, {default:() => active.value ? h(Component,{key:'gamepad',settings:{},highlightButtons:[]}) : h(Empty,{key:'other'})}) }).mount('#app');
window.__benchApi = {
  activate: async (enabled) => { active.value=enabled; await nextTick(); },
  visibility: (visible) => send(visible ? 'window.shown':'window.hidden',{}),
  snapshot: (data) => send('gamepad.state',data),
  start: (kind) => { if(timer)clearInterval(timer);phase=0;window.__benchCounts={messages:0,vueUpdates:0};
    const push=()=>{const p=state();p.sequence=++phase;
      if(kind==='changing'||kind==='bhold') p.axes=[Math.sin(phase/13),0,0,0];
      if(kind==='bhold')p.buttons[1]=true;
      send('gamepad.state',p);window.__benchCounts.messages++;};
    push();timer=setInterval(push,8);
  },
  stop:async()=>{if(timer)clearInterval(timer);timer=null;await nextTick();return {...window.__benchCounts};},
  mode:(value)=>window.dispatchEvent(new CustomEvent('ipc:gamepad.testmode',{detail:value})),
};
`;
const builds = {};
for (const [label,path] of [['before',before],['after',after]]) {
  const css = [];
  const result = await build({ stdin:{contents:fixture(path),resolveDir:repo,loader:'js'},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},alias:{'@':resolve(repo,'src')},nodePaths:[resolve(repo,'node_modules')],plugins:[{
    name:'actual-vue-sfc',
    setup(build) {
      if(label==='before') build.onResolve({filter:/[/\\]bridge[/\\]ipc(?:\.ts)?$/},args=>({path:resolve(output,'before/src/bridge/ipc.ts')}));
      build.onLoad({filter:/\.vue$/},args=>{
        const source=readFileSync(args.path,'utf8');const id=createHash('sha1').update(args.path).digest('hex').slice(0,8);
        const {descriptor,errors}=parse(source,{filename:args.path});if(errors.length)throw errors[0];
        let script=compileScript(descriptor,{id,inlineTemplate:true}).content;
        if(args.path===path){
          script="import { onUpdated as __cpuUpdated } from 'vue';\n"+script;
          script=script.replace(/setup\(__props[^)]*\)\s*\{/,'$&\n __cpuUpdated(() => { if(window.__benchCounts) window.__benchCounts.vueUpdates++; });');
        }
        for(const style of descriptor.styles){const result=compileStyle({source:style.content,filename:args.path,id:'data-v-'+id,scoped:style.scoped});if(result.errors.length)throw result.errors[0];css.push(result.code);}
        script=script.replace('export default','const __sfc__ =')+'\n__sfc__.__scopeId='+JSON.stringify('data-v-'+id)+';export default __sfc__;';
        return {contents:script,loader:'ts',resolveDir:dirname(args.path)};
      });
    },
  }]});
  builds[label]={js:result.outputFiles[0].text,css:css.join('\n')};
  writeFileSync(resolve(output,`browser-${label}.js`),builds[label].js);
}
const imageRequests={before:0,after:0};
const server=createServer((request,response)=>{
  const url=new URL(request.url,'http://127.0.0.1');const label=url.pathname.includes('before')?'before':'after';
  if(url.pathname.startsWith('/gamepad-base.png')){
    const ref=new URL(request.headers.referer||'http://127.0.0.1/after.html');const key=ref.pathname.includes('before')?'before':'after';imageRequests[key]++;
    if(ref.searchParams.get('imageFail')){response.writeHead(503,{'Cache-Control':'no-store'});response.end('memory-only missing image control');}
    else{response.writeHead(200,{'Content-Type':'image/png'});response.end(readFileSync(resolve(repo,'public/gamepad-base.png')));}return;
  }
  if(url.pathname.endsWith('.js')){response.writeHead(200,{'Content-Type':'text/javascript'});response.end(builds[label].js);return;}
  response.writeHead(200,{'Content-Type':'text/html'});
  response.end(`<!doctype html><html><head><style>body{background:#1f2228;color:#fff} ${builds[label].css}</style></head><body><div id="app"></div><script>window.__nativeListeners=[];window.__modeEvents=[];window.chrome={webview:{postMessage:()=>{},addEventListener:(type,fn)=>window.__nativeListeners.push(fn)}};window.addEventListener('ipc:gamepad.testmode',e=>window.__modeEvents.push(e.detail));</script><script src="/${label}.js"></script></body></html>`);
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
let browser;
const windows=[];const correctness=[];const windowSeconds=Number(process.env.CPU_BENCH_SECONDS||4);const repeats=Number(process.env.CPU_BENCH_REPEATS||3);
try {
  browser=await chromium.launch({executablePath:process.env.CPU_BENCH_BROWSER||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true,args:['--disable-background-networking','--disable-component-update']});
  for(let repeat=1;repeat<=repeats;repeat++)for(const kind of ['stable','changing','cached'])for(const label of (repeat%2?['before','after']:['after','before'])){
    const page=await browser.newPage({viewport:{width:1000,height:720}});await page.goto(`http://127.0.0.1:${port}/${label}.html`);await page.waitForFunction(()=>!!window.__benchApi);
    await page.evaluate(()=>window.__benchApi.snapshot({connected:true,name:'ROG memory-only fixture',buttons:Array(16).fill(false),axes:[0,0,0,0]}));
    if(kind==='cached')await page.evaluate(()=>window.__benchApi.activate(false));
    const session=await page.context().newCDPSession(page);await session.send('Performance.enable');
    const metrics=async()=>Object.fromEntries((await session.send('Performance.getMetrics')).metrics.map(m=>[m.name,m.value]));
    await page.waitForTimeout(300);const beforeMetrics=await metrics();
    const wallStart=performance.now();await page.evaluate(kind=>window.__benchApi.start(kind==='cached'?'changing':kind),kind);await page.waitForTimeout(windowSeconds*1000);
    const counts=await page.evaluate(()=>window.__benchApi.stop());const elapsed=(performance.now()-wallStart)/1000;const afterMetrics=await metrics();
    const row={repeat,label,kind,elapsedSeconds:elapsed,...counts,taskSeconds:afterMetrics.TaskDuration-beforeMetrics.TaskDuration,scriptSeconds:afterMetrics.ScriptDuration-beforeMetrics.ScriptDuration,layoutSeconds:afterMetrics.LayoutDuration-beforeMetrics.LayoutDuration,recalcStyleSeconds:afterMetrics.RecalcStyleDuration-beforeMetrics.RecalcStyleDuration};
    row.rendererTaskCores=row.taskSeconds/elapsed;
    if(label==='after'&&kind!=='changing')assert.equal(counts.vueUpdates,0,`${kind} view performed reactive presentation work`);
    assert.ok(counts.messages>=windowSeconds*80,'telemetry fixture was throttled; cannot claim a valid comparison');
    windows.push(row);console.log(JSON.stringify(row));await page.close();
  }
  for(const label of ['before','after']){
    const page=await browser.newPage();await page.goto(`http://127.0.0.1:${port}/${label}.html`);await page.waitForFunction(()=>!!window.__benchApi);
    await page.evaluate(()=>{window.__benchApi.mode(true);window.__benchApi.start('bhold')});await page.waitForTimeout(3300);
    const exited=await page.evaluate(()=>window.__modeEvents.includes(false));await page.evaluate(()=>window.__benchApi.stop());
    if(label==='after')assert.ok(exited,'held B did not exit test mode while axes kept changing');
    correctness.push({label,case:'B-hold-with-moving-axis',exitedWithin3300ms:exited});await page.close();
    const previous=imageRequests[label];const failed=await browser.newPage();await failed.goto(`http://127.0.0.1:${port}/${label}.html?imageFail=1`);await failed.waitForTimeout(1800);
    const attempted=imageRequests[label]-previous;
    if(label==='after')assert.ok(attempted<=4,'image failure retry was not bounded');
    correctness.push({label,case:'failed-image',requestsWithin1800ms:attempted});await failed.close();
  }
  // Hidden-window gating (not only KeepAlive deactivation) must replay freshest state on show.
  const page=await browser.newPage();await page.goto(`http://127.0.0.1:${port}/after.html`);await page.waitForFunction(()=>!!window.__benchApi);
  await page.evaluate(()=>{window.__benchApi.visibility(false);window.__benchApi.start('changing')});await page.waitForTimeout(400);
  const counts=await page.evaluate(()=>window.__benchApi.stop());assert.equal(counts.vueUpdates,0);
  await page.evaluate(()=>window.__benchApi.visibility(true));await page.waitForTimeout(30);
  const shown=await page.evaluate(()=>window.__benchCounts.vueUpdates);assert.equal(shown,1);
  correctness.push({label:'after',case:'hidden-window',hiddenMessages:counts.messages,hiddenVueUpdates:counts.vueUpdates,shownLatestUpdates:shown});await page.close();
  const means={};for(const kind of ['stable','changing','cached']){
    means[kind]={};for(const label of ['before','after']){const rows=windows.filter(x=>x.kind===kind&&x.label===label);means[kind][label]=rows.reduce((a,b)=>a+b.rendererTaskCores,0)/rows.length;}
    means[kind].taskReductionPercent=(1-means[kind].after/means[kind].before)*100;
  }
  const report={status:'PASS',capturedAtUtc:new Date().toISOString(),scope:'actual production Vue component + memory-only native IPC fixture in Chromium; not full YMCC/WebView2 process CPU',browserVersion:browser.version(),vueVersion:req('vue/package.json').version,beforeSha256:createHash('sha256').update(readFileSync(before)).digest('hex'),afterSha256:createHash('sha256').update(readFileSync(after)).digest('hex'),windowSeconds,repeats,means,correctness,windows,hardwareWrites:0};
  writeFileSync(resolve(output,'browser-ab-evidence.json'),JSON.stringify(report,null,2));console.log('PASS '+JSON.stringify(means));
} finally { if(browser)await browser.close();await new Promise(r=>server.close(r)); }
