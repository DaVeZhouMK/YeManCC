import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));
const {build}=require('esbuild');
const {parse,compileScript,compileStyle}=require('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_gesture-summon.cjs'))('playwright');
const out=path.resolve(process.argv[2]||'../../Build/Validation/GestureSummon-20261018/browser');
fs.mkdirSync(out,{recursive:true});
const api=String.raw`
let document='',fail=false,hold=false,release=null;export function seed(value){document=JSON.stringify(value)}export const persisted=()=>JSON.parse(document);
export function controls(values){if('fail'in values)fail=values.fail;if('hold'in values)hold=values.hold;if(!hold&&release){const fn=release;release=null;fn()}}
export const fs={exists:async p=>p.endsWith('yeman-settings.json'),readTextFile:async()=>document,rename:async()=>true};
export const settingsStore={read:async(p,stamp)=>({stamp:document,unchanged:stamp===document,content:document}),write:async(p,value,tx)=>{if(hold)await new Promise(r=>release=r);if(fail)throw Error('fixture disk unavailable');document=value;return tx?{ok:true,content:document}:true}};`;
const ipc=String.raw`export async function invoke(cmd){if(cmd!=='window.gestureSummonStatus')throw Error('Unexpected IPC '+cmd);return {enabled:false,available:true}}export function on(event,fn){window.fixture.listeners++;const cb=e=>fn(e.detail);window.addEventListener('ipc:'+event,cb);return()=>{window.fixture.listeners--;window.removeEventListener('ipc:'+event,cb)}}`;
const entry=String.raw`
import {createApp,h,ref,KeepAlive} from 'vue';
import Gesture from './src/components/GestureSummonSettings.vue';
import SegButton from './src/components/SegButton.vue';
import {loadUiSettings,setUiSettings,getUiSettings} from './src/bridge/uiSettings';
import {normalizeSettings} from './src/bridge/settingsRepository';
import {seed,persisted,controls} from './src/bridge/api';
seed(normalizeSettings({ui:{windowPlacement:'right',futureUi:{keep:true}},input:{futureInput:{keep:true}},fan:{sentinel:'fan'},extensions:{future:{keep:true}}}));
window.fixture.persisted=persisted;window.fixture.controls=controls;window.fixture.setUiSettings=setUiSettings;window.fixture.reload=loadUiSettings;window.fixture.settings=getUiSettings;
createApp({setup(){const shown=ref(true),side=ref('right');window.fixture.show=v=>shown.value=v;return()=>h(KeepAlive,null,{default:()=>shown.value?h('main',null,[h('section',{class:'card window-position-settings placement-fixture'},[h('h3',{class:'card-title'},'窗口位置及呼出'),h(SegButton,{modelValue:side.value,options:[{value:'left',label:'左侧'},{value:'right',label:'右侧'}],full:true,'onUpdate:modelValue':async v=>{await setUiSettings({windowPlacement:v});side.value=v}}),h(Gesture)])]):null})}}).mount('#app');`;
const styles=[];
const bundle=await build({stdin:{contents:entry,loader:'ts',resolveDir:root},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'gesture-fixture',setup(b){
 b.onLoad({filter:/[\\/]bridge[\\/]api\.ts$/},()=>({contents:api,loader:'ts'}));
 b.onLoad({filter:/[\\/]bridge[\\/]ipc\.ts$/},()=>({contents:ipc,loader:'ts'}));
 b.onLoad({filter:/\.vue$/},a=>{const source=fs.readFileSync(a.path,'utf8'),{descriptor}=parse(source,{filename:a.path}),id='data-v-gesture-'+path.basename(a.path).replace(/\W/g,'');const c=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles)styles.push(compileStyle({source:style.content,filename:a.path,id,scoped:style.scoped}).code);return {contents:c.content.replace('export default','const c=')+';c.__scopeId="'+id+'";export default c',loader:'ts',resolveDir:path.dirname(a.path)}});
}}]});
fs.writeFileSync(path.join(out,'fixture.js'),bundle.outputFiles[0].text);
const css=fs.readFileSync('src/styles/tokens.css','utf8')+'\n'+styles.join('\n')+'\nbody{margin:0;padding:14px;background:#141b26;color:#f2f4fa;font-family:Segoe UI,sans-serif}.card-header-row{display:flex;align-items:center;justify-content:space-between;gap:12px}.card-header-row .card-title{margin:0}';
const html='<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><style>'+css+'</style></head><body><div id="app"></div><script>window.fixture={listeners:0}</script><script src="/fixture.js"></script></body></html>';
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript':'text/html;charset=utf-8');r.end(q.url==='/fixture.js'?bundle.outputFiles[0].text:html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const cases=[],errors=[];let browser;
async function check(name,fn){await fn();cases.push(name)}
try{
 browser=await chromium.launch({headless:true,channel:'msedge'});const page=await browser.newPage({viewport:{width:580,height:700}});page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);
 const card=page.locator('.gesture-summon-settings'),toggle=card.getByRole('switch',{name:'手势呼出',exact:true});await card.waitFor();
 const initial=await page.evaluate(()=>window.fixture.persisted());
 await check('default off and merged inside window position and summon card',async()=>{await page.waitForFunction(()=>window.fixture.settings().gestureSummonEnabled===false);assert.equal(await toggle.getAttribute('aria-checked'),'false');assert.match(await card.textContent(),/右侧边缘向左/);assert.equal(await card.evaluate(el=>el.parentElement.classList.contains('window-position-settings')),true);assert.equal(await page.locator('.card').count(),1);assert.equal(await page.getByRole('heading',{name:'窗口位置及呼出',exact:true}).count(),1);const source=fs.readFileSync('src/views/SettingsView.vue','utf8');assert.match(source,/<section class="card window-position-settings">[\s\S]*?窗口位置及呼出[\s\S]*?<GestureSummonSettings\s*\/>\s*<\/section>/)});
 await check('enable persists through real settings repository',async()=>{await toggle.click();await page.waitForFunction(()=>window.fixture.persisted().ui.gestureSummonEnabled===true);assert.equal(await toggle.getAttribute('aria-checked'),'true')});
 await check('left placement moves summon edge',async()=>{await page.getByRole('button',{name:'左侧',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.gesture-summon-settings').textContent.includes('左侧边缘向右'));assert.equal(await page.evaluate(()=>window.fixture.persisted().ui.windowPlacement),'left')});
 await check('slow writes block repeated toggles until committed',async()=>{await page.evaluate(()=>window.fixture.controls({hold:true}));await toggle.click();await toggle.locator('button').waitFor();assert.equal(await toggle.locator('button').isDisabled(),true);assert.equal(await page.evaluate(()=>window.fixture.persisted().ui.gestureSummonEnabled),true);await page.evaluate(()=>window.fixture.controls({hold:false}));await page.waitForFunction(()=>window.fixture.persisted().ui.gestureSummonEnabled===false);await page.waitForFunction(()=>!document.querySelector('.gesture-summon-settings button').disabled)});
 await check('failed persistence retains previous value with visible error',async()=>{await page.evaluate(()=>window.fixture.controls({fail:true}));await toggle.click();await card.getByRole('alert').waitFor();assert.equal(await toggle.getAttribute('aria-checked'),'false');assert.equal(await page.evaluate(()=>window.fixture.persisted().ui.gestureSummonEnabled),false);await page.evaluate(()=>window.fixture.controls({fail:false}))});
 await check('retry succeeds and clear error',async()=>{await toggle.click();await page.waitForFunction(()=>window.fixture.persisted().ui.gestureSummonEnabled===true);assert.equal(await card.getByRole('alert').count(),0)});
 await check('native failure status is visible and clears on disable',async()=>{await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.gestureSummonStatus',{detail:{enabled:true,available:false}})));await card.getByRole('status').waitFor();await toggle.click();await page.waitForFunction(()=>window.fixture.persisted().ui.gestureSummonEnabled===false);assert.equal(await card.getByRole('status').count(),0)});
 await check('opposite edge reserved for monitoring and bottom remains native',async()=>{assert.match(await card.textContent(),/居中 80%/);assert.match(await card.textContent(),/另一侧留给监控/);assert.equal(await card.getByRole('switch').count(),1);assert.equal(await card.getByRole('switch',{name:'底部上滑打开开始菜单',exact:true}).count(),0)});
 await check('other input fan and future data preserved',async()=>{const saved=await page.evaluate(()=>window.fixture.persisted());assert.deepEqual(saved.input,initial.input);assert.deepEqual(saved.fan,initial.fan);assert.deepEqual(saved.extensions,initial.extensions);assert.deepEqual(saved.ui.futureUi,{keep:true})});
 await check('keep-alive removes and restores exactly one native subscription',async()=>{assert.equal(await page.evaluate(()=>window.fixture.listeners),1);await page.evaluate(()=>window.fixture.show(false));await page.waitForFunction(()=>window.fixture.listeners===0);await page.evaluate(()=>window.fixture.show(true));await page.waitForFunction(()=>window.fixture.listeners===1)});
 await check('normalization rejects truthy nonboolean imported flag',async()=>{await page.evaluate(()=>window.fixture.setUiSettings({gestureSummonEnabled:'true'}));assert.equal(await page.evaluate(()=>window.fixture.settings().gestureSummonEnabled),false)});
 await page.evaluate(()=>window.fixture.setUiSettings({gestureSummonEnabled:true}));
 await page.waitForFunction(()=>document.querySelector('.gesture-summon-settings [role=switch]').getAttribute('aria-checked')==='true');
 for(const width of [580,360])await check('layout fits '+width+' pixels',async()=>{await page.setViewportSize({width,height:600});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);await page.locator('main').screenshot({path:path.join(out,'gesture-'+width+'.png'),animations:'disabled'})});
 assert.deepEqual(errors,[]);const report={ok:true,passed:cases.length,cases,pageErrors:errors,realSettingsWrites:0,touchInjected:false,scope:'Real Vue component, uiSettings and settingsRepository; memory storage and native status fixtures'};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log('GESTURE_BROWSER_OK passed='+cases.length);
}finally{await browser?.close();await new Promise(r=>server.close(r))}
