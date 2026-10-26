// Real feedback card/controls; IPC, settings, and backend calls are in-memory only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {createServer} from 'node:http';import crypto from 'node:crypto';
const repo=process.cwd(),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const req=createRequire(path.join(repo,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');const{chromium}=createRequire(process.env.YMCC_UI_TEST_REQUIRE_ANCHOR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_lighting.cjs')('playwright');
const styles=[],errors=[],cases=[];
const mocks={
 '@/bridge/api':`export const gamepadFeedback={capabilities:async()=>structuredClone(window.f.caps),setLed:async a=>{window.f.applies.push(structuredClone(a));return {ok:true}},vibrate:async()=>{throw Error('Unexpected vibration')}};`,
 '@/bridge/ipc':`export async function invoke(){return {}};`,
 '@/bridge/settingsRepository':`export async function loadSettings(){return structuredClone(window.f.settings)};export async function saveSettingsSection(section,value){window.f.saved.push({section,value:JSON.parse(JSON.stringify(value))});window.f.settings[section]=JSON.parse(JSON.stringify(value))};`,
 '@/gamepad/focus':`export function focusGamepadElement(n){n?.focus()};export function getGamepadPopupPlacement(){return {style:{},above:false}};`,
};
const bundle=await build({stdin:{resolveDir:repo,loader:'ts',contents:`import{createApp,h}from'vue';import Card from './src/components/ControllerFeedbackCapabilitiesCard.vue';let app;
window.mount=(scenario)=>{app?.unmount();const supported=scenario==='onex';window.f={applies:[],saved:[],caps:{ledAvailable:true,brightnessAvailable:scenario!=='loki',ledEffects:supported?['solid','preset']:['solid','rainbow'],ledPresets:supported?Array.from({length:11},(_,i)=>({id:'LEDPreset_OneXPlayerX1_Preset'+String(i+1).padStart(2,'0'),label:'预设 '+(i+1)})):[],presetDeclared:scenario==='mini'||supported,presetImplemented:supported,vibrationAvailable:false,physicalPrimarySlot:-1},settings:{gamepad:{feedback:{vibrationStrength:70,lighting:{enabled:true,off:false,mode:supported?'preset':'solid',presetId:'LEDPreset_OneXPlayerX1_Preset09',color:'#112233',color2:'#445566',speed:65,brightness:42}}}}};app=createApp({render:()=>h(Card)});app.mount('#app')};window.mount('onex');`},bundle:true,write:false,format:'iife',platform:'browser',loader:{'.png':'dataurl'},alias:{'@':path.join(repo,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'lighting-fixture',setup(b){
 b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'mock'}:undefined);b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:repo}));
 b.onLoad({filter:/\.vue$/},a=>{const{descriptor}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path}),id='data-v-'+crypto.createHash('sha1').update(a.path).digest('hex').slice(0,8);const c=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.deepEqual(css.errors,[]);styles.push(css.code)}return{contents:c.content.replace('export default','const C =')+'\nC.__scopeId='+JSON.stringify(id)+';export default C;',loader:'ts',resolveDir:path.dirname(a.path)}});
}}]});
const html='<!doctype html><meta charset="utf-8"><style>'+fs.readFileSync(path.join(repo,'src/styles/tokens.css'),'utf8')+styles.join('\n')+'body{background:var(--bg-solid);color:var(--text);font-family:system-ui;margin:20px}#app{max-width:1000px;margin:auto}.card{border:1px solid #293745;border-radius:12px;padding:20px;background:var(--bg-panel)}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((r,s)=>{s.setHeader('Content-Type',r.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');s.end(r.url==='/fixture.js'?bundle.outputFiles[0].text:html)});// Port 0 can select a browser-forbidden low port on this desktop. Keep the
// listener in the high dynamic range and retry occupied/denied candidates.
async function listenForBrowser(){
 for(let attempt=0;attempt<32;attempt++){
  const candidate=crypto.randomInt(49152,65536);
  try{
   await new Promise((resolve,reject)=>{
    const failed=error=>{server.removeListener('listening',ready);reject(error)};
    const ready=()=>{server.removeListener('error',failed);resolve()};
    server.once('error',failed);server.once('listening',ready);server.listen(candidate,'127.0.0.1');
   });
   return;
  }catch(error){if(error.code!=='EADDRINUSE'&&error.code!=='EACCES')throw error;}
 }
 throw new Error('Could not reserve a browser-compatible loopback port');
}
await listenForBrowser();let browser;
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:1280,height:900}});page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);
 await page.locator('.preset-field').waitFor();await page.waitForFunction(()=>window.f.applies.length===1);assert.equal(await page.locator('.preset-field button').first().textContent().then(s=>s.trim()),'预设 9');let call=await page.evaluate(()=>window.f.applies[0]);assert.equal(call.mode,'preset');assert.equal(call.presetId,'LEDPreset_OneXPlayerX1_Preset09');assert.equal(call.brightness,42);assert.equal(await page.locator('.color-field').count(),0);cases.push('persisted stable preset ID reaches real card/API args, not enum128 or array position');
 await page.locator('.preset-field button').first().click();assert.equal(await page.getByRole('option').count(),11);await page.getByRole('option',{name:'预设 11',exact:true}).click();await page.waitForFunction(()=>window.f.applies.length===2);call=await page.evaluate(()=>window.f.applies[1]);assert.equal(call.presetId,'LEDPreset_OneXPlayerX1_Preset11');const saved=await page.evaluate(()=>window.f.saved.at(-1).value.feedback.lighting.presetId);assert.equal(saved,call.presetId);cases.push('real preset selection persists and applies the same stable ID');await page.screenshot({path:path.join(out,'onex-preset-card.png')});
 await page.locator('.effect-field button').first().click();await page.getByRole('option',{name:'常亮',exact:true}).click();await page.waitForFunction(()=>window.f.applies.length===3);assert.equal(await page.locator('.preset-field').count(),0);assert.equal(await page.locator('.color-field').count(),1);call=await page.evaluate(()=>window.f.applies.at(-1));assert.equal(call.mode,'solid');assert.equal(call.presetId,undefined);cases.push('normal color mode resumes its existing route without preset ID');
 await page.evaluate(()=>window.mount('loki'));await page.waitForFunction(()=>window.f.applies.length===1);assert.equal(await page.getByRole('slider',{name:'亮度',exact:true}).count(),0);assert.equal(await page.locator('.color-field').count(),1);assert.equal(await page.locator('.preset-field').count(),0);cases.push('Loki keeps normal color and on/off controls without a no-op brightness slider');await page.screenshot({path:path.join(out,'loki-color-without-fake-brightness.png')});
 await page.evaluate(()=>window.mount('mini'));await page.waitForFunction(()=>window.f.applies.length===1);assert.equal(await page.getByRole('slider',{name:'亮度',exact:true}).count(),1);assert.equal(await page.locator('.preset-field').count(),0);assert.equal(await page.locator('.unavailable-hint').filter({hasText:'预设暂不可用'}).count(),1);await page.locator('.effect-field button').first().click();assert.equal(await page.getByRole('option',{name:'预设',exact:true}).count(),0);assert.equal(await page.getByRole('option',{name:'彩虹',exact:true}).count(),1);cases.push('Mini retains HID light/brightness with explicit unsupported preset rather than serial fallback');
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify({ok:true,cases,errors,browserPort:server.address().port,hardwareIo:false,HCAssemblyLoaded:false,scope:'real Vue card and controls with IPC/settings memory fakes'},null,2));console.log('LIGHTING_OPERATIONS_BROWSER_OK groups='+cases.length+' deviceOperations=0');
}finally{await browser?.close();await new Promise(r=>server.close(r));}
