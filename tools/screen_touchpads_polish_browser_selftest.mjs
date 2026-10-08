// Real Vue controls + production gamepad engine in Chromium. All storage and
// Steam IPC is memory-only; this test never changes Steam or installed YMCC.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_screen-touchpads-polish.cjs'))('playwright');
const out=path.resolve(root,'../../Build/Validation/ScreenTouchpads-Polish');fs.mkdirSync(out,{recursive:true});
const css=[fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')],cases=[],errors=[];
const mocks={
 '@/router':`export const ROUTES=[];`,
 '@/bridge/performanceSchedule':`export async function loadPerformanceSchedule(){return {enabled:false}}`,
 '@/bridge/yeman':`export async function summonGet(){return {enabled:true}}`,
 '@/robust/frontendDiagnostics':`export function reportFrontendError(){}`,
 '@/bridge/ipc':`export const isNativeRuntime=true;
 export function on(type,fn){const cb=e=>fn(e.detail);window.addEventListener('ipc:'+type,cb);return ()=>window.removeEventListener('ipc:'+type,cb)}
 function padState(persona){const f=window.__fixture;return {...structuredClone(f.padProfiles[persona]),persona,ok:true,available:true,visible:f.padProfiles[persona].enabled,
 steamDeckAvailable:persona==='steamdeck'&&f.currentPersona==='steamdeck'&&f.targetEnabled,
 ps5Available:persona==='dualsense-edge'&&['dualsense-edge','dualsense'].includes(f.currentPersona)&&f.targetEnabled,
 ps4Available:persona==='dualsense-edge'&&f.currentPersona==='dualshock4'&&f.targetEnabled};}
 export async function invoke(type,args){const f=window.__fixture;
 if(type==='steamDeckMouse.get')return structuredClone(f.steam);
 if(type==='steamDeckMouse.set'){f.savedSteam.push(args.percent);f.steam={...f.steam,percent:args.percent,appliedLive:true};return structuredClone(f.steam)}
 if(type==='screenTouchpads.get')return padState(args.persona);
 if(type==='screenTouchpads.set'){f.savedPads.push(structuredClone(args));Object.assign(f.padProfiles[args.persona],args.config);if(args.config.layout!==undefined)f.padProfiles[args.persona].enabled=args.config.layout!=='off';return padState(args.persona)}
 throw new Error('Unexpected production IPC: '+type);}`,
};
const fixture=`<script setup lang="ts">
import {ref} from 'vue';import Steam from './src/components/SteamDeckMouseSensitivity.vue';import Pads from './src/components/ScreenTouchpadsSettings.vue';import Slider from './src/components/Slider.vue';
const disabled=ref(false),regular=ref(50),persona=ref('steamdeck'),targetEnabled=ref(true);window.__disableSteam=v=>disabled.value=v;
window.__setPersona=(p,on=true)=>{window.__fixture.currentPersona=p;window.__fixture.targetEnabled=on;persona.value=p;targetEnabled.value=on;};
</script><template><section><Steam :disabled="disabled"/><Pads :key="persona" :persona="persona" :steam-deck-enabled="persona==='steamdeck' && targetEnabled" :ps5-enabled="['dualsense-edge','dualsense','dualshock4'].includes(persona) && targetEnabled"/><Slider v-model="regular" label="普通滑条" :min="0" :max="100" :step="5"/></section></template>`;
function compile(source,filename,resolveDir){const {descriptor,errors}=parse(source,{filename});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(filename).digest('hex').slice(0,8);let script=compileScript(descriptor,{id,inlineTemplate:true}).content;for(const style of descriptor.styles){const c=compileStyle({source:style.content,id,scoped:style.scoped});assert.equal(c.errors.length,0);css.push(c.code)}script=script.replace('export default','const _component=')+`\n_component.__scopeId=${JSON.stringify(id)};export default _component;`;return {contents:script,loader:'ts',resolveDir};}
const entry=compile(fixture,'screen-touchpads-polish.vue',root);
const bundle=await build({stdin:{...entry,contents:entry.contents.replace('export default _component;',`
import {createApp} from 'vue';import {startGamepad} from './src/gamepad/engine.ts';import {screenTouchpadDefaults} from './src/bridge/screenTouchpads.ts';
window.__fixture={steam:{ok:true,available:true,percent:100,pending:false,steamRunning:true,liveAvailable:true},pads:{...screenTouchpadDefaults('steamdeck'),ok:true,available:true,visible:true},savedSteam:[],savedPads:[],currentPersona:'steamdeck',targetEnabled:true};
window.__fixture.padProfiles=Object.fromEntries(['disabled','steamdeck','dualsense-edge','elite'].map(p=>[p,screenTouchpadDefaults(p)]));
createApp(_component).mount('#app');startGamepad({router:{currentRoute:{value:{path:'/button-mapping'}},push:async()=>{}}});`)},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'actual-screen-touchpads-controls',setup(b){b.onResolve({filter:/.*/},args=>{const key=args.path==='./ipc' && /src[/\\]bridge[/\\]/.test(args.importer)?'@/bridge/ipc':args.path;return mocks[key]?{path:key,namespace:'mock'}:undefined});b.onLoad({filter:/.*/,namespace:'mock'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));b.onLoad({filter:/\.vue$/},args=>compile(fs.readFileSync(args.path,'utf8'),args.path,path.dirname(args.path)));}}]});
const html=`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><style>${css.join('\n')}body{margin:0;padding:20px;background:var(--bg-solid);color:var(--text);font-family:system-ui}section{max-width:520px}button.focused,input.focused{outline:2px solid var(--accent)}</style><div id="app"></div><script src="/fixture.js"></script></html>`;
const server=createServer((r,res)=>{res.setHeader('Content-Type',r.url==='/fixture.js'?'text/javascript':'text/html;charset=utf-8');res.end(r.url==='/fixture.js'?bundle.outputFiles[0].text:html)});await new Promise(r=>server.listen(0,'127.0.0.1',r));
let browser;
try{browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:680,height:820},hasTouch:true});page.on('pageerror',e=>errors.push(e.message));await page.goto(`http://127.0.0.1:${server.address().port}`);
 const steam=page.locator('input[aria-label="SteamDeck鼠标灵敏度"]');await steam.waitFor();await page.waitForFunction(()=>!document.querySelector('input[aria-label="SteamDeck鼠标灵敏度"]').disabled);
 assert.equal(await page.locator('.screen-touchpad-layout button').innerText(),'双触摸板');
 assert.equal(await page.locator('.screen-touchpad-mode').count(),2);
 assert.ok((await page.locator('.screen-touchpad-mode').nth(0).innerText()).includes('SteamDeck 左触摸板 (Steam内设置)'));
 assert.ok((await page.locator('.screen-touchpad-mode').nth(1).innerText()).includes('SteamDeck 右触摸板 (Steam内设置)'));
 cases.push({name:'new-SteamDeck-profile-defaults-to-dual-native-mappings',ok:true});
 await page.screenshot({path:path.join(out,'preset-steamdeck-settings.png')});
 await switchPersona('dualsense-edge');assert.equal(await page.locator('.screen-touchpad-layout button').innerText(),'单触摸板');
 assert.equal(await page.locator('.screen-touchpad-mode').count(),0);assert.equal(await page.locator('.screen-touchpad-single-mode').count(),1);
 assert.ok((await page.locator('.screen-touchpad-single-mode').innerText()).includes('PS5触摸板 (Steam内设置)'));
 cases.push({name:'new-PS5-profile-defaults-to-single-native-mapping',ok:true});
 await page.screenshot({path:path.join(out,'preset-ps5-settings.png')});
 for(const persona of ['disabled','elite']){await switchPersona(persona);assert.equal(await page.locator('.screen-touchpad-layout button').innerText(),'关闭');assert.equal(await page.locator('.screen-touchpad-mode,.screen-touchpad-single-mode').count(),0);cases.push({name:persona+'-new-profile-remains-off',ok:true});}
 assert.equal((await page.evaluate(()=>window.__fixture.savedPads)).length,0);cases.push({name:'loading-presets-does-not-overwrite-saved-settings',ok:true});
 for(const [persona,mask] of [['steamdeck',3],['dualsense-edge',3],['elite',1],['dualshock4',3]]){
  await switchPersona(persona);assert.equal(await controlValue('专用按键组合'),'都开启');
  const profile=['dualsense','dualshock4'].includes(persona)?'dualsense-edge':persona;
  assert.equal(await page.evaluate(p=>window.__fixture.padProfiles[p].specialMask,profile),mask);
  cases.push({name:persona+'-new-special-buttons-default-all-on',ok:true});
 }
 await switchPersona('disabled');assert.equal(await controlValue('专用按键组合'),'关闭');
 assert.equal(await page.locator('button[aria-label="专用按键组合"]').isDisabled(),true);
 assert.equal(await page.evaluate(()=>window.__fixture.savedPads.length),0);
 cases.push({name:'default-special-buttons-render-without-persisting-or-enabling-disabled-profile',ok:true});
 // All remaining old-profile regressions deliberately start with saved opt-outs.
 await page.evaluate(()=>{for(const p of Object.values(window.__fixture.padProfiles)){p.specialMask=0;p.specialEnabled=false;}});
 for(const persona of ['steamdeck','dualsense-edge','elite']){
  await switchPersona(persona);assert.equal(await controlValue('专用按键组合'),'关闭');
  cases.push({name:persona+'-saved-special-off-is-not-overwritten-on-load',ok:true});
 }
 await switchPersona('steamdeck');
 async function action(name){await page.evaluate(name=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:name}})),name);await page.waitForTimeout(30)}
 async function checkValue(label,expected,el=steam){assert.equal(Number(await el.inputValue()),expected,label);cases.push({name:label,ok:true})}
 await checkValue('initial-Steam-value-preserved',100);assert.equal(await steam.getAttribute('step'),'any');assert.equal(await steam.getAttribute('data-gp-step'),'5');
 await steam.focus();await action('confirm');await action('nav-right');await checkValue('gamepad-right-advances-5-percent',105);await action('nav-left');await checkValue('gamepad-left-decreases-5-percent',100);
 await action('slider-increase');await checkValue('gamepad-trigger-increase',105);await action('slider-decrease');await checkValue('gamepad-trigger-decrease',100);await action('confirm');await page.waitForTimeout(220);assert.deepEqual(await page.evaluate(()=>window.__fixture.savedSteam),[100]);cases.push({name:'gamepad-A-commits-through-production-Steam-bridge',ok:true});
 async function externalValue(percent){await page.evaluate(percent=>{const f=window.__fixture;f.steam.percent=percent;window.dispatchEvent(new CustomEvent('ipc:steamDeckMouse.updated',{detail:structuredClone(f.steam)}))},percent);await page.waitForTimeout(30)}
 await externalValue(103);await checkValue('off-grid-Steam-readback-not-rewritten',103);await action('confirm');await action('nav-right');await checkValue('off-grid-right-to-next-5-percent-grid',105);await action('nav-left');await checkValue('grid-left-to-100',100);await action('confirm');await page.waitForTimeout(220);
 await externalValue(103);await action('confirm');await action('nav-left');await checkValue('off-grid-left-to-previous-grid',100);await action('confirm');await page.waitForTimeout(220);
 await externalValue(1);await action('confirm');await action('nav-left');await checkValue('minimum-remains-1',1);await action('nav-right');await checkValue('minimum-right-to-5',5);await action('confirm');await page.waitForTimeout(220);
 await externalValue(300);await action('confirm');await action('nav-right');await checkValue('maximum-remains-300',300);await action('nav-left');await checkValue('maximum-left-to-295',295);await action('confirm');await page.waitForTimeout(220);
 const regular=page.locator('input[aria-label="普通滑条"]');await regular.focus();await action('confirm');await action('nav-right');await checkValue('ordinary-slider-gamepad-regression',55,regular);await action('confirm');
 const transparency=page.locator('input[aria-label="触摸板显示透明度"]');await checkValue('transparency-default-80',80,transparency);await transparency.focus();await action('confirm');await action('nav-right');await checkValue('transparency-gamepad-edit',85,transparency);await action('confirm');await page.waitForTimeout(220);assert.ok((await page.evaluate(()=>window.__fixture.savedPads)).some(p=>p.persona==='steamdeck' && p.config.transparency===85));
 const scale=page.locator('input[aria-label="触摸板缩放"]');
 await checkValue('scale-default-100',100,scale);
 assert.equal(await scale.getAttribute('min'),'50');assert.equal(await scale.getAttribute('max'),'200');assert.equal(await scale.getAttribute('step'),'5');assert.equal(await scale.getAttribute('data-gp-accelerate'),'false');cases.push({name:'scale-bounds-step-and-no-acceleration',ok:true});
 await scale.focus();await action('confirm');await action('nav-right');await checkValue('scale-gamepad-right-105',105,scale);await action('nav-left');await checkValue('scale-gamepad-left-100',100,scale);
 await action('slider-increase');await checkValue('scale-gamepad-trigger-105',105,scale);await action('slider-decrease');await checkValue('scale-gamepad-trigger-100',100,scale);await action('confirm');await page.waitForTimeout(220);
 assert.ok((await page.evaluate(()=>window.__fixture.savedPads)).some(p=>p.persona==='steamdeck' && p.config.scale===100));cases.push({name:'scale-gamepad-save-through-production-bridge',ok:true});
 await scale.press('ArrowRight');await checkValue('scale-keyboard-right-exactly-5',105,scale);await scale.press('ArrowLeft');await checkValue('scale-keyboard-left-exactly-5',100,scale);await page.waitForTimeout(220);
 await setRange('触摸板缩放',50);await scale.focus();await action('confirm');await action('nav-left');await checkValue('scale-minimum-remains-50',50,scale);await action('nav-right');await checkValue('scale-minimum-right-55',55,scale);await action('confirm');await page.waitForTimeout(220);
 await setRange('触摸板缩放',200);await scale.focus();await action('confirm');await action('nav-right');await checkValue('scale-maximum-remains-200',200,scale);await action('nav-left');await checkValue('scale-maximum-left-195',195,scale);await action('confirm');await page.waitForTimeout(220);
 await setRange('触摸板缩放',100);
 async function switchPersona(persona,on=true){await page.evaluate(([p,on])=>window.__setPersona(p,on),[persona,on]);await page.waitForTimeout(70);await page.evaluate(()=>{const f=window.__fixture,p=['dualsense','dualshock4'].includes(f.currentPersona)?'dualsense-edge':f.currentPersona;window.dispatchEvent(new CustomEvent('ipc:screenTouchpads.updated',{detail:{...f.padProfiles[p],persona:p,ok:true,available:true,visible:f.padProfiles[p].enabled,steamDeckAvailable:p==='steamdeck'&&f.targetEnabled,ps5Available:['dualsense-edge','dualsense'].includes(f.currentPersona)&&f.targetEnabled,ps4Available:f.currentPersona==='dualshock4'&&f.targetEnabled}}))});assert.equal(await page.locator('.screen-touchpads-bubble').count(),1)}
 async function setLayout(name){await page.locator('.screen-touchpad-layout button').click();await page.locator('.dd-menu:visible').last().getByRole('option',{name,exact:true}).click();await page.waitForTimeout(80)}
 async function enablePads(){await setLayout('双触摸板')}
 async function setRange(label,value){await page.locator('input[aria-label="'+label+'"]').evaluate((el,value)=>{el.value=String(value);el.dispatchEvent(new Event('input',{bubbles:true}))},value);await page.waitForTimeout(220)}
 for(const [persona,opacity,size] of [['disabled',35,50],['dualsense-edge',70,150],['elite',60,200]]){await switchPersona(persona);await enablePads();await checkValue(persona+'-default-80',80,transparency);await checkValue(persona+'-default-scale-100',100,scale);await setRange('触摸板显示透明度',opacity);await checkValue(persona+'-independent-opacity',opacity,transparency);await setRange('触摸板缩放',size);await checkValue(persona+'-independent-scale',size,scale);}
 for(const [persona,opacity,size] of [['disabled',35,50],['dualsense-edge',70,150],['elite',60,200],['steamdeck',85,100]]){await switchPersona(persona);await checkValue(persona+'-restored-opacity',opacity,transparency);await checkValue(persona+'-restored-scale',size,scale);}
 await transparency.evaluate(el=>{el.value='95';el.dispatchEvent(new Event('input',{bubbles:true}))});await switchPersona('dualsense-edge');await setRange('触摸板显示透明度',65);const profiles=await page.evaluate(()=>window.__fixture.padProfiles);assert.equal(profiles.steamdeck.transparency,95);assert.equal(profiles['dualsense-edge'].transparency,65);assert.equal(profiles.disabled.transparency,35);cases.push({name:'fast-persona-switch-drains-save-to-original-slot',ok:true});
 await switchPersona('steamdeck');await scale.evaluate(el=>{el.value='105';el.dispatchEvent(new Event('input',{bubbles:true}))});await switchPersona('elite');await setRange('触摸板缩放',175);
 const scaleProfiles=await page.evaluate(()=>window.__fixture.padProfiles);assert.equal(scaleProfiles.steamdeck.scale,105);assert.equal(scaleProfiles.elite.scale,175);assert.equal(scaleProfiles['dualsense-edge'].scale,150);assert.equal(scaleProfiles.disabled.scale,50);cases.push({name:'fast-scale-switch-drains-save-to-original-profile',ok:true});
 await switchPersona('disabled');await page.locator('.screen-touchpad-mode').first().locator('button').click();assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'SteamDeck 左触摸板 (Steam内设置)',exact:true}).count(),0);await page.keyboard.press('Escape');cases.push({name:'non-SteamDeck-mode-does-not-offer-native-Deck-pad',ok:true});
 await switchPersona('steamdeck',false);
 assert.ok((await page.locator('.screen-touchpad-mode').first().innerText()).includes('SteamDeck 左触摸板 (Steam内设置)'));
 assert.ok((await page.locator('.screen-touchpad-mode').nth(1).innerText()).includes('SteamDeck 右触摸板 (Steam内设置)'));
 await page.locator('.screen-touchpad-mode').first().locator('button').click();const inactiveDeck=page.locator('.dd-menu:visible').last().getByRole('option',{name:'SteamDeck 左触摸板 (Steam内设置)',exact:true});
 assert.equal(await inactiveDeck.count(),1);assert.ok((await inactiveDeck.getAttribute('class')).includes('dd-opt-disabled'));
 const deckSaveCount=await page.evaluate(()=>window.__fixture.savedPads.length);await inactiveDeck.click();assert.equal(await page.evaluate(()=>window.__fixture.savedPads.length),deckSaveCount);
 await page.keyboard.press('Escape');cases.push({name:'inactive-SteamDeck-shows-preset-label-but-cannot-select-native-pad',ok:true});
 await switchPersona('steamdeck',true);await page.locator('.screen-touchpad-mode').first().locator('button').click();assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'SteamDeck 左触摸板 (Steam内设置)',exact:true}).count(),1);await page.keyboard.press('Escape');cases.push({name:'active-SteamDeck-offers-native-pad',ok:true});
 assert.equal(await page.locator('.screen-touchpads-bubble [role="switch"]').count(),0);cases.push({name:'layout-dropdown-replaces-switch',ok:true});
 for(const persona of ['disabled','steamdeck','elite','dualsense-edge']) {
  await switchPersona(persona,true);await setLayout('单触摸板');assert.equal(await page.locator('.screen-touchpad-mode').count(),0);assert.equal(await page.locator('.screen-touchpad-single-mode').count(),1);
  await page.locator('.screen-touchpad-single-mode button').click();assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'PS5触摸板 (Steam内设置)',exact:true}).count(),persona==='dualsense-edge'?1:0);
  for(const name of ['键盘 W / A / S / D','键盘 ↑ / ← / ↓ / →','模拟鼠标'])assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name,exact:true}).count(),1);
  await page.keyboard.press('Escape');cases.push({name:persona+'-single-layout-only-one-mapping-and-correct-native-eligibility',ok:true});
  await setLayout('关闭');assert.equal(await page.locator('.screen-touchpad-mode,.screen-touchpad-single-mode').count(),0);assert.equal(await scale.count(),0);cases.push({name:persona+'-off-hides-mapping-and-controls',ok:true});
 }
 await switchPersona('dualsense-edge',true);await setLayout('单触摸板');await page.locator('.screen-touchpad-single-mode button').click();await page.locator('.dd-menu:visible').last().getByRole('option',{name:'PS5触摸板 (Steam内设置)',exact:true}).click();await page.waitForTimeout(80);
 assert.equal((await page.evaluate(()=>window.__fixture.padProfiles))['dualsense-edge'].singleMode,'dualsense');cases.push({name:'PS5-single-native-mapping-saved-to-PS5-profile',ok:true});
 await page.screenshot({path:path.join(out,'single-ps5-settings.png')});
 await switchPersona('dualsense-edge',false);assert.ok((await page.locator('.screen-touchpad-single-mode').innerText()).includes('PS5触摸板 (Steam内设置)'));
 await page.locator('.screen-touchpad-single-mode button').click();const inactivePs=page.locator('.dd-menu:visible').last().getByRole('option',{name:'PS5触摸板 (Steam内设置)',exact:true});
 assert.equal(await inactivePs.count(),1);assert.ok((await inactivePs.getAttribute('class')).includes('dd-opt-disabled'));
 const psSaveCount=await page.evaluate(()=>window.__fixture.savedPads.length);await inactivePs.click();assert.equal(await page.evaluate(()=>window.__fixture.savedPads.length),psSaveCount);
 await page.keyboard.press('Escape');cases.push({name:'PS5-native-single-label-kept-but-disabled-when-virtual-target-off',ok:true});
 await switchPersona('elite',true);await setLayout('单触摸板');await page.locator('.screen-touchpad-single-mode button').click();await page.locator('.dd-menu:visible').last().getByRole('option',{name:'键盘 ↑ / ← / ↓ / →',exact:true}).click();await page.waitForTimeout(80);
 await switchPersona('dualsense-edge',true);assert.ok((await page.locator('.screen-touchpad-single-mode').innerText()).includes('PS5触摸板 (Steam内设置)'));await switchPersona('elite',true);assert.ok((await page.locator('.screen-touchpad-single-mode').innerText()).includes('↑'));cases.push({name:'single-layout-and-mode-restore-without-profile-cross-talk',ok:true});
 await page.locator('.screen-touchpad-layout button').focus();await action('confirm');assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'单触摸板',exact:true}).count(),1);await page.keyboard.press('Escape');cases.push({name:'gamepad-opens-layout-dropdown',ok:true});
 // Actual compiled controls exercise both native halves, including legacy PS4.
 for(const persona of ['dualsense-edge','dualsense','dualshock4']){
  const label=persona==='dualshock4'?'PS4':'PS5';await switchPersona(persona,true);await setLayout('单触摸板');await setLayout('双触摸板');
  assert.equal(await page.locator('.screen-touchpad-mode').count(),2);
  for(const [i,side] of [[0,'左'],[1,'右']]){
   const name=label+' '+side+'触摸板 (Steam内设置)';const row=page.locator('.screen-touchpad-mode').nth(i);
   assert.ok((await row.innerText()).includes(name));await row.locator('button').click();const option=page.locator('.dd-menu:visible').last().getByRole('option',{name,exact:true});
   assert.equal(await option.count(),1);assert.ok(!(await option.getAttribute('class')).includes('dd-opt-disabled'));await option.click();await page.waitForTimeout(80);
  }
  const cfg=await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge']);assert.equal(cfg.layout,'dual');assert.equal(cfg.leftMode,'dualsense');assert.equal(cfg.rightMode,'dualsense');
  assert.equal(await page.locator('input[aria-label="触摸板鼠标灵敏度"]').count(),0);
  cases.push({name:persona+'-dual-native-left-and-right-dropdowns-enabled-and-saved',ok:true});
  await page.waitForTimeout(200);await page.screenshot({path:path.join(out,persona+'-dual-native-settings.png'),animations:'disabled'});
  if(persona==='dualshock4'){
   assert.equal(await page.locator('button[aria-label="背部按键组合"]').isDisabled(),true);
   await page.locator('button[aria-label="专用按键组合"]').click();assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'只开启静音',exact:true}).count(),0);
   await page.keyboard.press('Escape');cases.push({name:'PS4-offers-native-PS-only-without-Mute-or-Edge-rear-buttons',ok:true});
  }
  await switchPersona(persona,false);
  for(const [i,side] of [[0,'左'],[1,'右']]){
   const row=page.locator('.screen-touchpad-mode').nth(i),name=label+' '+side+'触摸板 (Steam内设置)';assert.ok((await row.innerText()).includes(name));
   await row.locator('button').click();const option=page.locator('.dd-menu:visible').last().getByRole('option',{name,exact:true});assert.ok((await option.getAttribute('class')).includes('dd-opt-disabled'));
   const saves=await page.evaluate(()=>window.__fixture.savedPads.length);await option.click();assert.equal(await page.evaluate(()=>window.__fixture.savedPads.length),saves);await page.keyboard.press('Escape');
  }
  cases.push({name:persona+'-dual-native-disabled-when-target-off-without-mutating-saved-mapping',ok:true});
  await switchPersona('elite',true);await switchPersona(persona,true);assert.equal(await page.locator('.screen-touchpad-layout button').innerText(),'双触摸板');
  cases.push({name:persona+'-dual-layout-and-mappings-restore-after-persona-switch',ok:true});
 }
 await page.locator('.screen-touchpad-mode').first().locator('button').click();await page.locator('.dd-menu:visible').last().getByRole('option',{name:'键盘 W / A / S / D',exact:true}).click();await page.waitForTimeout(80);
 assert.equal(await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge'].leftMode),'wasd');assert.equal(await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge'].rightMode),'dualsense');
 cases.push({name:'PS-dual-left-keyboard-and-right-native-mappings-can-coexist',ok:true});
 await setLayout('关闭');await setLayout('单触摸板');await setLayout('双触摸板');
 assert.equal(await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge'].leftMode),'wasd');assert.equal(await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge'].rightMode),'dualsense');
 cases.push({name:'PS-dual-custom-mappings-survive-off-single-dual-layout-switches',ok:true});
 await switchPersona('dualsense-edge',true);
 const controlDropdownLabels=['YMCC呼出位置','专用按键组合','背部按键组合'];
 for(const label of controlDropdownLabels)assert.equal(await page.locator(`button[aria-label="${label}"]`).count(),1);
 cases.push({name:'screen-controls-use-three-dropdowns',ok:true});
 async function chooseControl(label,option){const trigger=page.locator(`button[aria-label="${label}"]`);await trigger.click();await page.locator('.dd-menu:visible').last().getByRole('option',{name:option,exact:true}).click();await page.waitForTimeout(90)}
 async function controlValue(label){return page.locator(`button[aria-label="${label}"]`).innerText()}
 await switchPersona('steamdeck',true);await setLayout('关闭');
 for(const [option,mask] of [['关闭',0],['只开启 Steam',1],['只开启三点',2],['都开启',3]]){await chooseControl('专用按键组合',option);const cfg=await page.evaluate(()=>window.__fixture.padProfiles.steamdeck);assert.equal(cfg.specialMask,mask);assert.equal(cfg.specialEnabled,mask!==0);cases.push({name:'steam-special-dropdown-'+mask,ok:true})}
 for(const [option,mask] of [['L5+R5',10],['L4+R4',5],['全开启',15],['全关闭',0]]){await chooseControl('背部按键组合',option);const cfg=await page.evaluate(()=>window.__fixture.padProfiles.steamdeck);assert.equal(cfg.rearMask,mask);assert.equal(cfg.rearEnabled,mask!==0);assert.equal(await controlValue('背部按键组合'),option);cases.push({name:'steam-rear-dropdown-'+mask,ok:true})}
 async function verifyRearMenu(labels,file,name){await page.locator('button[aria-label="背部按键组合"]').click();const menu=page.locator('.dd-menu:visible').last();assert.equal(await menu.getByRole('option').count(),4);assert.deepEqual((await menu.getByRole('option').allTextContents()).map(label=>label.trim()),labels);await page.waitForFunction(()=>{const menu=document.querySelector('.dd-menu');return menu&&Number(getComputedStyle(menu).opacity)===1});await page.screenshot({path:path.join(out,file),animations:'disabled'});await page.keyboard.press('Escape');cases.push({name,ok:true})}
 await verifyRearMenu(['L5+R5','L4+R4','全开启','全关闭'],'sd-rear-combinations-dropdown.png','SD-rear-menu-has-only-four-real-name-choices');
 await page.evaluate(()=>{window.__fixture.padProfiles.steamdeck.rearMask=2;window.__fixture.padProfiles.steamdeck.rearEnabled=true});await switchPersona('elite',true);await switchPersona('steamdeck',true);assert.equal(await controlValue('背部按键组合'),'选择组合');assert.equal(await page.evaluate(()=>window.__fixture.padProfiles.steamdeck.rearMask),2);await chooseControl('背部按键组合','L5+R5');assert.equal(await page.evaluate(()=>window.__fixture.padProfiles.steamdeck.rearMask),10);cases.push({name:'legacy-single-key-mask-preserved-until-a-four-choice-pair-is-selected',ok:true});
 for(const [option,expected] of [['关闭','关闭'],['左侧呼出','左侧呼出'],['右侧呼出','右侧呼出']]){await chooseControl('YMCC呼出位置',option);const cfg=await page.evaluate(()=>window.__fixture.padProfiles.steamdeck);assert.equal(cfg.summonPosition,option==='左侧呼出'?'left':option==='右侧呼出'?'right':'off');assert.equal(cfg.summonEnabled,option!=='关闭');assert.equal(await controlValue('YMCC呼出位置'),expected);cases.push({name:'summon-position-dropdown-'+option,ok:true})}
 await switchPersona('dualsense-edge',true);await setLayout('关闭');
 for(const [option,mask] of [['关闭',0],['只开启 PS',1],['只开启静音',2],['都开启',3]]){await chooseControl('专用按键组合',option);const cfg=await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge']);assert.equal(cfg.specialMask,mask);assert.equal(cfg.specialEnabled,mask!==0);cases.push({name:'ps-special-dropdown-'+mask,ok:true})}
 for(const [option,mask] of [['LFN+RFN',5],['LB+RB',10],['全开启',15],['全关闭',0]]){await chooseControl('背部按键组合',option);const cfg=await page.evaluate(()=>window.__fixture.padProfiles['dualsense-edge']);assert.equal(cfg.rearMask,mask);assert.equal(cfg.rearEnabled,mask!==0);assert.equal(await controlValue('背部按键组合'),option);cases.push({name:'ps-rear-dropdown-'+mask,ok:true})}
 await verifyRearMenu(['LFN+RFN','LB+RB','全开启','全关闭'],'ps-rear-combinations-dropdown.png','PS5-rear-menu-has-only-four-real-name-choices');
 const nativeMain=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
 const summonHandler=nativeMain.match(/case ymcc::screenpads::kSummonMessage:([\s\S]*?)case ymcc::screenpads::kRefreshMessage:/)?.[1];
 assert.ok(summonHandler,'Screen-button owner route must exist');assert.match(summonHandler,/nativeYmccShortcutEmit\("window\.summon",\s*0\);/);
 assert.match(fs.readFileSync(path.join(root,'native/screen_button_overlay.h'),'utf8'),/PostMessageW\(parent,kSummonMessage,0,0\)/);cases.push({name:'Y-uses-default-summon-route-without-forced-Windows-maximize',ok:true});
 await switchPersona('disabled',true);assert.equal(await page.locator('button[aria-label="专用按键组合"]').isDisabled(),true);assert.equal(await page.locator('button[aria-label="背部按键组合"]').isDisabled(),true);assert.equal(await page.locator('button[aria-label="YMCC呼出位置"]').isDisabled(),false);cases.push({name:'dropdown-capability-gates-match-persona',ok:true});
 await switchPersona('steamdeck',true);await page.locator('button[aria-label="专用按键组合"]').focus();await action('confirm');assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'只开启 Steam',exact:true}).count(),1);await page.keyboard.press('Escape');cases.push({name:'gamepad-opens-special-dropdown',ok:true});
 await page.locator('button[aria-label="背部按键组合"]').focus();await action('confirm');assert.equal(await page.locator('.dd-menu:visible').last().getByRole('option',{name:'L5+R5',exact:true}).count(),1);await page.keyboard.press('Escape');cases.push({name:'gamepad-opens-rear-dropdown',ok:true});
 await setLayout('关闭');await chooseControl('YMCC呼出位置','关闭');await chooseControl('专用按键组合','关闭');await chooseControl('背部按键组合','全关闭');assert.equal(await scale.count(),0);assert.equal(await page.locator('input[aria-label="触摸板显示透明度"]').count(),0);cases.push({name:'all-dropdown-overlays-off-hide-shared-sliders',ok:true});
 await chooseControl('YMCC呼出位置','右侧呼出');assert.equal(await scale.count(),1);assert.equal(await page.locator('input[aria-label="触摸板显示透明度"]').count(),1);cases.push({name:'summon-only-mode-retains-shared-sliders',ok:true});
await page.screenshot({path:path.join(out,'touchpad-off-independent-buttons.png')});
 const body=await page.locator('body').innerText();assert.ok(!body.includes('屏幕双触摸板'));for(const removed of ['模拟触摸自测','键盘模式按区域中心判断方向','左右区域已显示','底部点击条'])assert.ok(!body.includes(removed),removed);assert.equal(await page.locator('.screen-touchpad-test').count(),0);cases.push({name:'removed-explanations-and-public-selftest',ok:true});
 await page.screenshot({path:path.join(out,'controller-settings.png')});await page.evaluate(()=>window.__disableSteam(true));assert.equal(await steam.isDisabled(),true);cases.push({name:'disabled-slider-stays-disabled',ok:true});assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'browser-verification.json'),JSON.stringify({ok:true,cases,errors,realGamepadEngine:true,realVueComponents:true,steamModified:false},null,2));console.log(`SCREEN_TOUCHPADS_POLISH_BROWSER_OK: ${cases.length} cases; production engine and Vue controls; no real Steam writes`);
}finally{await browser?.close();await new Promise(r=>server.close(r))}
