// Real controller, gyro and preview views. Storage/IPC are in-memory only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_controller-display-test.cjs'))('playwright');
const out=process.env.YMCC_UI_BROWSER_OUT||path.resolve(root,'../../Build/Validation/controller-display-20261006/browser');
fs.mkdirSync(out,{recursive:true});
const styles=[],cases=[],errors=[],screenshots=[];
const mocks={
  '@/bridge/ipc':`export const isNativeRuntime=true;export function on(name,cb){const f=window.__fixture;const list=f.events[name]??=new Set();list.add(cb);return ()=>list.delete(cb)};export async function invoke(method,args){const f=window.__fixture;f.requests.push(method);if(method==='input.shortcutRuntime.get')return f.shortcut??{active:false};if(method==='input.shortcutRuntime.clear'){f.shortcut={active:false};window.__emit('input.shortcutRuntime',f.shortcut)}return {ok:true}}`,
  '@/bridge/settingsRepository':`import {evaluateInputCasWrite} from ${JSON.stringify(path.join(root,'src/bridge/settingsRepository.ts'))};const clone=v=>JSON.parse(JSON.stringify(v));export async function loadSettings(){return clone(window.__fixture.settings)};export async function compareAndSwapInputSettings(revision,patch){const f=window.__fixture;const detached=clone(patch);f.writes.push(detached);const r=evaluateInputCasWrite(f.settings.input,revision,detached,true);if(r.ok)f.settings.input=clone(r.value);return r}`,
  '@/bridge/gameInputOverride':`export function getGameInputOverrideState(){return {locked:false,identity:''}};export function subscribeGameInputOverrideState(cb){cb(getGameInputOverrideState());return ()=>{}}`,
  '@/bridge/api':`export const fs={},settingsStore={},shell={open:async()=>{}}`,
  '@/bridge/gameproc':`export async function closeJoyxoffIfRunning(){};export async function getGameInputRedistState(){return {mouseInterfaceAvailable:true}}`,
  '@/gamepad/focus':`export function focusGamepadElement(n){n?.focus()};export function getGamepadPopupPlacement(){return {style:{},above:false}}`,
  '@/components/SteamDeckMouseSensitivity.vue':`export default {render(){return null}}`,
  '@/components/ControllerShortcutsCard.vue':`export default {render(){return null}}`,
  '@/components/ControllerFeedbackCapabilitiesCard.vue':`export default {render(){return null}}`,
  '@/views/ControllerShortcutEditorView.vue':`export default {render(){return null}}`,
};
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:`
import {createApp} from 'vue';import Controller from './src/views/ButtonMappingView.vue';import Gyro from './src/views/GyroMotionView.vue';import Preview from './src/components/GamepadVisualizer.vue';import {normalizeSettings} from './src/bridge/settingsRepository.ts';
const initial=normalizeSettings({input:{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true,preset:'custom',outputStick:'right',outputMode:'virtual-stick',motionMode:'on',virtualPadLink:true}}});
window.__fixture={settings:initial,events:{},writes:[],requests:[],shortcut:null};window.__emit=(name,data)=>{for(const cb of window.__fixture.events[name]??[])cb(data)};
let app;window.__mount=mode=>{app?.unmount();app=createApp(mode==='controller'?Controller:mode==='gyro'?Gyro:Preview,mode==='preview'?{settings:{enabled:false},showTestMode:false}:{});app.mount('#app')};window.__mount('controller');
`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'controller-display-fixture',setup(b){
  b.onResolve({filter:/.*/},args=>{
    if(args.path in mocks)return {path:args.path,namespace:'fixture'};
    if(args.path==='./api'&&args.importer.endsWith('settingsRepository.ts'))return {path:'@/bridge/api',namespace:'fixture'};
  });
  b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},args=>{
    const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.deepEqual(errors,[]);
    const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8);
    const compiled=compileScript(descriptor,{id,inlineTemplate:true});
    for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(css.errors,[]);styles.push(css.code)}
    return {contents:compiled.content.replace('export default','const _component =')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)};
  });
}}]});
const js=result.outputFiles[0].text;
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html='<!doctype html><meta charset="utf-8"><title>Controller display validation</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{padding:24px;max-width:800px;margin:auto}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{
  if(request.url.startsWith('/gamepad-base.png')){response.setHeader('Content-Type','image/png');response.end(fs.readFileSync(path.join(root,'public/gamepad-base.png')));return}
  response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage({viewport:{width:1120,height:900}});page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://127.0.0.1:'+server.address().port);
  await page.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent==='SteamDeck');
  const baseline=await page.evaluate(()=>structuredClone(window.__fixture.settings));
  const status=page.locator('.persona-capabilities span').first();
  const snapshot=async(persona,steamRunning,state='active')=>{
    await page.evaluate(({persona,steamRunning,state})=>window.__emit('gamepad.state',{virtualOutput:{state,reason:'current-session-frame-accepted',requestedPersona:persona,boundPersona:persona,hostAlive:true,prepared:true,neutralized:true,frameAccepted:true,sourceLive:true,suppressionState:'verified',steamRunning}}),{persona,steamRunning,state});
  };
  const temporary=async(persona,enabled=true)=>page.evaluate(({persona,enabled})=>{const state={active:true,persona,virtualEnabled:enabled,gyroEnabled:enabled};window.__fixture.shortcut=state;window.__emit('input.shortcutRuntime',state)}, {persona,enabled});
  for(const persona of ['steamdeck','dualsense-edge']){
    await temporary(persona);await snapshot(persona,false);
    assert.equal(await status.textContent(),'虚拟手柄-Steam未开启实际无效');
    assert((await status.getAttribute('class')).includes('cap-warning'));
    assert.deepEqual(await page.locator('.persona-capabilities span').allTextContents(),['虚拟手柄-Steam未开启实际无效','陀螺仪-支持','背部按键-支持识别2个按键']);
    cases.push(persona+' shows missing Steam warning, preserves gyro/back-button capabilities and follows runtime persona');
    const shot=path.join(out,persona+'-steam-stopped.png');await page.screenshot({path:shot});screenshots.push(shot);
    await snapshot(persona,true);assert.equal(await status.textContent(),'虚拟手柄-成功开启');cases.push(persona+' returns to backend success after Steam starts');
    await snapshot(persona,null);assert.equal(await status.textContent(),'虚拟手柄-成功开启');cases.push(persona+' unknown Steam state does not invent missing Steam');
    await snapshot(persona,true,'suspected-failed');assert.equal(await status.textContent(),'虚拟手柄-疑似失败');cases.push(persona+' retains independent backend failure');
    await temporary(persona,false);await snapshot(persona,false);assert.equal(await status.textContent(),'虚拟手柄-未开启');cases.push(persona+' disabled runtime is off, not missing Steam');
  }
  await temporary('elite');await snapshot('elite',false);assert.equal(await status.textContent(),'虚拟手柄-成功开启');
  assert.deepEqual(await page.locator('.persona-capabilities span').allTextContents(),['虚拟手柄-成功开启','陀螺仪-不支持','背部按键-不支持']);cases.push('Xbox is unaffected by missing Steam');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings),baseline);assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);cases.push('runtime feedback never writes saved configuration');
  await page.evaluate(()=>{window.__fixture.shortcut=null;window.__mount('gyro')});
  const stick=page.locator('.seg:has(button[data-gp-row="5"])');
  await page.waitForFunction(()=>document.querySelector('button[data-gp-row="5"].active')?.textContent?.trim()==='右摇杆');
  assert.deepEqual(await stick.locator('button .seg-main').allTextContents(),['左摇杆','右摇杆']);
  const positions=await stick.locator('button').evaluateAll(nodes=>nodes.map(node=>node.getBoundingClientRect().x));assert(positions[0]<positions[1]);
  assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);assert.deepEqual(await page.evaluate(()=>window.__fixture.settings),baseline);cases.push('gyro UI places left before right while saved right selection and hydration remain unchanged');
  const gyroShot=path.join(out,'gyro-left-right-layout.png');await page.screenshot({path:gyroShot,fullPage:true});screenshots.push(gyroShot);
  await stick.getByRole('button',{name:'左摇杆',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.settings.input.gyroMotion.outputStick==='left');cases.push('left UI still saves left output value');
  await stick.getByRole('button',{name:'右摇杆',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.settings.input.gyroMotion.outputStick==='right');cases.push('right UI still saves right output value');
  const writesBeforePreview=await page.evaluate(()=>window.__fixture.writes.length);
  await page.evaluate(()=>window.__mount('preview'));
  const coords=()=>page.locator('.stick-dot > circle:not(.stick-cap-ring)').evaluateAll(nodes=>nodes.map(n=>({x:+n.getAttribute('cx'),y:+n.getAttribute('cy')})));
  const sendAxes=async axes=>page.evaluate(axes=>window.__emit('gamepad.state',{connected:true,name:'Validation Controller',buttons:[],axes}),axes);
  await sendAxes([0,0,0,0]);const neutral=await coords();
  for(const axes of [[0,1,0,1],[0,-1,0,-1],[1,0,-1,0],[0,1,0,-1],[0.25,0.125,-0.25,-0.125]]){
    await sendAxes(axes);const drawn=await coords();
    for(let i=0;i<2;i++){assert.equal(drawn[i].x,neutral[i].x+axes[i*2]*10);assert.equal(drawn[i].y,neutral[i].y-axes[i*2+1]*10)}
    cases.push('both actual SVG stick caps have correct display-only X/Y for '+axes.join(','));
  }
  await sendAxes([0,1,0,-1]);const previewShot=path.join(out,'stick-left-up-right-down.png');await page.screenshot({path:previewShot});screenshots.push(previewShot);
  await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.hidden')));await sendAxes([0,-1,0,1]);
  const frozen=await coords();assert.equal(frozen[0].y,neutral[0].y-10);
  await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.shown')));const resumed=await coords();assert.equal(resumed[0].y,neutral[0].y+10);assert.equal(resumed[1].y,neutral[1].y-10);cases.push('hidden preview resumes latest sample with corrected Y');
  await sendAxes([0,0,0,0]);assert.deepEqual(await coords(),neutral);cases.push('neutral returns both sticks to unchanged centers');
  assert.equal(await page.evaluate(()=>window.__fixture.writes.length),writesBeforePreview);cases.push('preview never changes settings');
  assert.deepEqual(errors,[],'no browser runtime errors');
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({pass:true,cases,screenshots,errors,hardwareOperations:0},null,2));
  console.log(`CONTROLLER_DISPLAY_BROWSER_PASS cases=${cases.length} hardwareOperations=0`);console.log(screenshots.join('\n'));
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve))}
