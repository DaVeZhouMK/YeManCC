// Real PowerView + Dropdown + Toggle DOM/CSS/navigation. Only native/storage mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
const root=process.cwd(), req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'), {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_startup-ui-selftest.cjs'))('playwright');
const out=process.env.YMCC_UI_BROWSER_OUT || 'G:/YeManCC-Work/Mainline/Build/Validation/input-startup-policy-20261005';fs.mkdirSync(out,{recursive:true});
const power=fs.readFileSync(path.join(root,'src/views/PowerView.vue'),'utf8');
const yemanNames=[...new Set([...power.matchAll(/import\s*\{([^{}]+)\}\s*from\s*['"]@\/bridge\/yeman['"]/g)].flatMap(m=>m[1].split(',').map(n=>n.trim()).filter(Boolean)))];
const mocks={
  '@/bridge/yeman':yemanNames.map(name=>name==='BOOT_CONTROL_CENTER_TASK'?`export const ${name}='boot';`:name==='BOOT_MIRROR_CHANGED_EVENT'?`export const ${name}='boot-changed';`:`export async function ${name}(...args){window.__fixture?.actions.push([${JSON.stringify(name)},...args]);return false;}`).join('\n'),
  '@/bridge/settingsRepository':`import {mergeSettings} from ${JSON.stringify(path.join(root,'src/bridge/settingsRepository.ts'))};export async function readSettingsSection(section){return structuredClone(window.__fixture.settings[section]||{})};export async function saveSettingsSection(section,value){const f=window.__fixture;f.writes++;if(f.holdSave)await new Promise(r=>f.releaseSave=r);if(f.failSave)throw Error('simulated write failure');f.settings[section]=mergeSettings(f.settings[section],value)}`,
  '@/bridge/api':`export const fs={readTextFile:async()=>'{"configured":true}'},settingsStore={},shell={open:async()=>{}}`,
  '@/bridge/trayResident':`export async function readTrayResident(){return false};export async function setTrayResident(){}`,
};
const styles=[];
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:`import {createApp,ref} from 'vue';import Page from './src/views/PowerView.vue';import {normalizeSettings} from './src/bridge/settingsRepository.ts';
window.__fixture={writes:0,actions:[],failSave:false,holdSave:false,settings:normalizeSettings({startupDesired:{unknownBoot:'keep'},input:{outputTarget:{persona:'elite',buttonMappingEnabled:true},gyroMotion:{enabled:true,preset:'custom',presets:{fps:{gyroMultiplier:1.9}}}}})};
const app=createApp(Page);app.provide('globalRefreshKey',ref(0));app.mount('#app');`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'startup-ui-fixture',setup(b){
  b.onResolve({filter:/.*/},args=>{if(args.path in mocks)return {path:args.path,namespace:'fixture'};if(args.path==='./api'&&args.importer.endsWith('settingsRepository.ts'))return {path:'@/bridge/api',namespace:'fixture'};});
  b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.vue$/},args=>{const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.equal(errors.length,0);const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8),compiled=compileScript(descriptor,{id,inlineTemplate:true});
    for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}
    return {contents:compiled.content.replace('export default','const _component =')+`\n_component.__scopeId=${JSON.stringify(id)};export default _component;`,loader:'ts',resolveDir:path.dirname(args.path)};
  });
}}]});
const js=result.outputFiles[0].text,css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html=`<!doctype html><meta charset="utf-8"><title>Input startup UI isolated validation</title><style>${css}\nbody{background:var(--bg-solid);margin:0;padding:16px;}#app{max-width:760px;margin:0 auto;height:calc(100vh - 32px);overflow-y:auto;scroll-padding:24px;}[data-input-startup]{scroll-margin:20px}</style><main id="app" class="app-content"></main><script src="/fixture.js"></script>`;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?js:html);});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;const cases=[],screenshots=[],errors=[];
try {
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
  for(const [width,height] of [[1280,720],[1280,800],[1920,1080],[580,800],[420,800]]) {
    await page.setViewportSize({width,height});await page.goto('http://127.0.0.1:'+server.address().port);const pad=page.getByRole('button',{name:'开机启动虚拟手柄',exact:true}),gyro=page.getByRole('button',{name:'开机启动陀螺仪',exact:true});await pad.waitFor();await page.waitForFunction(()=>!document.querySelector('[data-input-startup="pad"] button').disabled);
    assert.equal(await gyro.isDisabled(),true);assert((await gyro.textContent()).includes('关闭陀螺仪'));
    const layout=await page.evaluate(()=>{const rows=[...document.querySelectorAll('[data-input-startup]')];return {overflow:document.documentElement.scrollWidth>innerWidth,rows:rows.map(r=>{const t=r.querySelector('span').getBoundingClientRect(),b=r.querySelector('button').getBoundingClientRect();return {left:b.left,right:b.right,top:b.top,bottom:b.bottom,textRight:t.right,textTop:t.top,textBottom:t.bottom};}),writes:window.__fixture.writes};});
    assert(!layout.overflow);assert.equal(layout.writes,0);for(const r of layout.rows){assert(r.left>r.textRight);assert(r.right<=width-12);assert(Math.min(r.bottom,r.textBottom)>Math.max(r.top,r.textTop));}assert(layout.rows[0].top<layout.rows[1].top);
    cases.push(`parallel label/dropdown, disabled gyro, no overflow ${width}x${height}`);
    if(width===1280&&height===720){await pad.scrollIntoViewIfNeeded();const p=path.join(out,'startup-input-default-1280x720.png');await page.screenshot({path:p,fullPage:true});screenshots.push(p);}
  }
  await page.setViewportSize({width:1280,height:800});await page.goto('http://127.0.0.1:'+server.address().port);
  const pad=page.getByRole('button',{name:'开机启动虚拟手柄',exact:true}),gyro=page.getByRole('button',{name:'开机启动陀螺仪',exact:true});await pad.waitFor();await page.waitForFunction(()=>!document.querySelector('[data-input-startup="pad"] button').disabled);
  async function openTrigger(button){
    // Let the previous popup leave and settle programmatic focus scrolling.
    await page.waitForSelector('.dd-menu',{state:'detached'});await button.scrollIntoViewIfNeeded();
    await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
    await button.click();await page.waitForSelector('.dd-menu:not(.dd-pop-leave-active)');
  }
  const inputBefore=await page.evaluate(()=>JSON.stringify(window.__fixture.settings.input));
  await pad.focus();await pad.press('Enter');await page.waitForSelector('[role="listbox"]');assert.equal(await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').count(),4);
  await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').filter({hasText:'SteamDeck'}).click();await page.waitForFunction(()=>window.__fixture.settings.startupDesired.virtualGamepadPersona==='steamdeck');assert.equal(await gyro.isDisabled(),false);cases.push('keyboard confirm opens 4 modes and enabled pad unlocks gyro');
  await openTrigger(gyro);await page.waitForSelector('.dd-menu:not(.dd-pop-leave-active)');assert.equal(await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').count(),5);await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').filter({hasText:'赛车'}).click();await page.waitForFunction(()=>window.__fixture.settings.startupDesired.gyroPreset==='racing');
  assert.equal(await page.evaluate(()=>JSON.stringify(window.__fixture.settings.input)),inputBefore);assert.equal(await page.evaluate(()=>window.__fixture.settings.startupDesired.unknownBoot),'keep');cases.push('5 gyro presets save only boot intent, retaining live input and unknown fields');
  await page.evaluate(()=>{window.__fixture.holdSave=true;window.__fixture.failSave=true;});await openTrigger(pad);await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').filter({hasText:'Xbox'}).click();await page.waitForFunction(()=>typeof window.__fixture.releaseSave==='function');assert(await pad.isDisabled());assert(await gyro.isDisabled());
  await page.evaluate(()=>{window.__fixture.holdSave=false;window.__fixture.releaseSave();});await page.waitForFunction(()=>document.body.textContent.includes('输入开机启动设置失败'));
  assert((await pad.textContent()).includes('SteamDeck'));assert((await gyro.textContent()).includes('赛车'));assert.equal(await page.evaluate(()=>window.__fixture.settings.startupDesired.virtualGamepadPersona),'steamdeck');cases.push('in-flight disables both inputs; write failure retains previous UI and disk intent');
  await page.evaluate(()=>window.__fixture.failSave=false);await openTrigger(pad);await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').filter({hasText:'关闭虚拟手柄'}).click();await page.waitForFunction(()=>window.__fixture.settings.startupDesired.virtualGamepadPersona==='disabled');assert(await gyro.isDisabled());assert((await gyro.textContent()).includes('关闭陀螺仪'));assert.equal(await page.evaluate(()=>window.__fixture.settings.startupDesired.gyroPreset),'off');
  await openTrigger(pad);await page.locator('.dd-menu:not(.dd-pop-leave-active) .dd-option').filter({hasText:'PS5'}).click();await page.waitForFunction(()=>window.__fixture.settings.startupDesired.virtualGamepadPersona==='dualsense-edge');assert((await gyro.textContent()).includes('关闭陀螺仪'));cases.push('disable forces saved off; re-enable does not resurrect previous gyro mode');
  await page.waitForSelector('.dd-menu',{state:'detached'});await pad.scrollIntoViewIfNeeded();await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));await pad.focus();await pad.evaluate(n=>n.dispatchEvent(new CustomEvent('gp:dropdown-open',{bubbles:true})));await page.waitForSelector('.dd-menu');assert.equal(await page.locator('.dd-menu:not(.dd-pop-leave-active) [data-hl="true"]').evaluate(n=>n===document.activeElement),true);
  await page.locator('.dd-menu:not(.dd-pop-leave-active) [data-hl="true"]').press('Escape');assert.equal(await pad.evaluate(n=>n===document.activeElement),true);cases.push('gamepad semantic open owns popup focus; back returns to trigger');
  assert.deepEqual(errors,[]);const report={suite:'Input startup browser UI',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'real Vue + controls + focus, mocked settings/native; no hardware'};fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
} finally {if(browser)await browser.close();await new Promise(r=>server.close(r));}
