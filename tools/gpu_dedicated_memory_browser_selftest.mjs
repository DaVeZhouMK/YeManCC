// Offline tests of the production component and bridge parser. No native API or registry access.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:http';
import crypto from 'node:crypto';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(path.join(root,'package.json'));
const { build } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime,'_gpu-memory-tests.cjs'))('playwright');
const out = path.resolve(process.env.YMCC_GPU_MEMORY_TEST_OUT || path.join(root,'../../../Build/Validation/GpuDedicatedMemory-20261013/browser'));
fs.mkdirSync(out,{recursive:true});
const windowsResultPath=process.env.YMCC_GPU_MEMORY_WINDOWS_RESULT;
const windowsResult=windowsResultPath?JSON.parse(fs.readFileSync(windowsResultPath,'utf8')):null;
if(windowsResult)assert(windowsResult.ok&&windowsResult.productionReader&&windowsResult.productionApply);

// Exercise the actual Settings device-tab insertion, not just a standalone widget.
const sourceHashes = {};
function readSource(file) {
 const bytes=fs.readFileSync(file);sourceHashes[path.relative(root,file).replaceAll('\\','/')]=crypto.createHash('sha256').update(bytes).digest('hex');
 return bytes.toString('utf8');
}
const settingsFile=process.env.YMCC_GPU_MEMORY_SETTINGS_SOURCE_OVERRIDE || path.join(root,'src/views/SettingsView.vue');
const settingsSource=readSource(settingsFile),settingsAst=parse(settingsSource).descriptor.template.ast;
let settingsMount,deviceSlot;
function findSettingsEntry(node,inDevice=false) {
 if(node.type===1){
  const slot=node.props.find(p=>p.type===7&&p.name==='slot'&&p.arg?.content==='device');
  if(slot){deviceSlot=node;inDevice=true;}
  if(node.tag==='GpuDedicatedMemorySettings'&&inDevice){assert(!settingsMount,'duplicate GPU entry');settingsMount=node;}
 }
 for(const child of node.children||[])findSettingsEntry(child,inDevice);
}
findSettingsEntry(settingsAst);
assert(settingsMount,'SETTINGS_GPU_MEMORY_ENTRY_MISSING');
assert(settingsMount.props.some(p=>p.type===7&&p.name==='if'&&p.exp?.content==='active'),'SETTINGS_GPU_MEMORY_MUST_REQUIRE_ACTIVE_TAB');
assert(deviceSlot.props.some(p=>p.type===7&&p.name==='slot'&&p.exp?.content.includes('active')));
const settingsParentPath=path.join(root,'__gpu-settings-parent.vue');
const settingsParentSource=`<script setup>import SettingsSections from './src/components/SettingsSections.vue';import GpuDedicatedMemorySettings from './src/components/GpuDedicatedMemorySettings.vue';const sections=[{id:'appearance',title:'外观',description:'',icon:'layers'},{id:'device',title:'设备',description:'',icon:'tools'}];</script><template><SettingsSections :sections="sections" label="设置"><template #appearance><p>外观设置</p></template><template #device="{ active }"><div class="device-flow">${settingsMount.loc.source}</div></template></SettingsSections></template>`;
const styles = [fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')];
const fixture = `
import {createApp,h,ref,KeepAlive} from 'vue';
import Component from './src/components/GpuDedicatedMemorySettings.vue';
import SettingsParent from 'gpu-settings-parent';
import * as model from './src/bridge/gpuDedicatedMemory';
window.__model=model;
window.__fixture={calls:[],state:null,offset:0,defer:false,fail:false,pending:[],pendingSaves:[],timers:new Map()};
const time=Date.now.bind(Date);Date.now=()=>time()+window.__fixture.offset;
let timerId=0;window.setInterval=(fn)=>{const id=++timerId;window.__fixture.timers.set(id,fn);return id};
window.clearInterval=id=>window.__fixture.timers.delete(id);
window.__tick=()=>{for(const fn of [...window.__fixture.timers.values()])fn()};
const app=createApp({setup(){const shown=ref(true),settings=ref(false),settingsGeneration=ref(0);window.__show=v=>shown.value=v;window.__settings=()=>{settings.value=true;++settingsGeneration.value};return()=>settings.value?h(SettingsParent,{key:settingsGeneration.value}):h(KeepAlive,null,{default:()=>shown.value?h(Component):null})}});app.mount('#app');window.__unmount=()=>app.unmount();
`;
const ipcMock = `export async function invoke(cmd,args){const f=window.__fixture;f.calls.push({cmd,args});
if(cmd==='gpu.dedicatedMemory.get'){if(f.fail)throw Error('fixture read failure');if(f.defer)return new Promise(resolve=>f.pending.push(resolve));return structuredClone(f.state)}
if(cmd==='gpu.dedicatedMemory.set'){if(f.failSave)throw Error('fixture write failure');if(f.deferSave)return new Promise(resolve=>f.pendingSaves.push(resolve));const reply=f.nativeSave?structuredClone(f.nativeSave):{...f.state,selected:args.option,fence:f.state.fence+'-saved',verification:'pending_restart',pendingOption:args.option};f.state=f.nativeReadAfterSave?structuredClone(f.nativeReadAfterSave):reply;return structuredClone(reply)}
throw Error('unexpected command '+cmd)}`;
const output = await build({stdin:{contents:fixture,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},plugins:[{
 name:'gpu-memory-offline',setup(b){
  b.onResolve({filter:/.*/},args=>{
    if(args.path==='gpu-settings-parent')return {path:settingsParentPath};
    const normalized=args.importer.replaceAll('\\','/');
    if(args.path==='@/bridge/ipc'||(args.path==='./ipc'&&normalized.includes('/src/bridge/')))return {path:'ipc',namespace:'mock'};
    if(args.path==='@/gamepad/focus')return {path:'focus',namespace:'mock'};
    return null;
  });
  b.onLoad({filter:/.*/,namespace:'mock'},args=>({contents:args.path==='ipc'?ipcMock:`export const focusGamepadElement=el=>el?.focus();export const getGamepadPopupPlacement=()=>({style:{left:'24px',top:'180px',maxHeight:'360px'}});`,loader:'ts'}));
  b.onResolve({filter:/^@\//},args=>({path:path.join(root,'src',args.path.slice(2))+(path.extname(args.path)?'':'.ts')}));
  b.onLoad({filter:/\.vue$/},args=>{
    const {descriptor}=parse(args.path===settingsParentPath?settingsParentSource:readSource(args.path));
    const id='data-v-'+path.basename(args.path).replace(/\W/g,'');
    for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);styles.push(compiled.code)}
    const code=compileScript(descriptor,{id,inlineTemplate:true}).content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`;
    return {contents:code,loader:'ts',resolveDir:path.dirname(args.path)};
  });
 }
}]});
const html=`<!doctype html><meta charset="utf-8"><style>${styles.join('\n')}body{background:#0b1016;color:var(--text);font-family:system-ui;padding:24px}.card{padding:20px;background:var(--bg-panel);border-radius:12px;max-width:700px}.muted{color:var(--text-dim)}.card-title{margin:0;font-size:18px}</style><div id="app"></div><script src="/fixture.js"></script>`;
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html;charset=utf-8');response.end(request.url==='/fixture.js'?output.outputFiles[0].text:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const state={schema:1,ok:true,available:true,settingRoute:'known-model',reason:'',provider:'rog-vram',adapterName:'AMD Radeon (offline)',driverVersion:'fixture',pnpId:'PCI\\fixture',luid:'fixture-a',capacityRead:true,dedicatedBytes:2048*1048576,selected:'mib:2048',fence:'fixture-fence',verification:'none',options:[
 {id:'auto',raw:0,capacityMiB:null,removeOverride:false},{id:'mib:2048',raw:4,capacityMiB:2048,removeOverride:false},{id:'mib:4096',raw:8,capacityMiB:4096,removeOverride:false},{id:'mib:8192',raw:16,capacityMiB:8192,removeOverride:false}]};
let browser;const cases=[];const errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:900,height:650}});
 page.on('pageerror',e=>errors.push(e.message));await page.goto(`http://127.0.0.1:${server.address().port}`);
 const panel=page.locator('section[aria-label="专用显存设置"]');
 const refresh=async value=>{await page.evaluate(v=>{window.__fixture.state=v;window.dispatchEvent(new Event('focus'));},value);await page.waitForTimeout(50)};
 const select=async name=>{await page.getByRole('listbox').waitFor({state:'detached'});await page.locator('.dd-trigger').click();await page.getByRole('option',{name,exact:true}).click();await page.getByRole('listbox').waitFor({state:'detached'})};
 const save=()=>page.getByRole('button',{name:'保存',exact:true}).click();
 const resetCalls=()=>page.evaluate(()=>window.__fixture.calls=[]);
 assert.equal(await panel.count(),0);cases.push('unavailable read hides the complete control');
 await refresh(state);await panel.waitFor();assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');cases.push('current capacity displays the real dedicated readback');
 assert.deepEqual(await panel.locator('button:not(.dd-trigger)').allTextContents(),['读取','保存']);
 assert.equal(await panel.locator('p').count(),0);assert.equal(await panel.locator('.adapter-name,.memory-note,.memory-confirm').count(),0);
 assert.equal((await panel.textContent()).includes(state.adapterName),false);cases.push('idle menu contains only title current capacity dropdown read and save controls');
 await resetCalls();await select('8 GiB');
 assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),0);
 cases.push('selection neither writes nor replaces the current capacity');
 await select('2 GiB');assert.equal(await page.getByRole('button',{name:'保存',exact:true}).isEnabled(),false);cases.push('returning to current configuration needs no write');
 await select('8 GiB');await save();await page.getByRole('status').filter({hasText:'请重启'}).waitFor();
 assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');
 assert.deepEqual(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).map(c=>c.args)),[{fence:'fixture-fence',option:'mib:8192',confirmed:true}]);
 assert.equal(await page.getByRole('button',{name:'保存',exact:true}).isEnabled(),false);cases.push('one explicit save uses the live fence and prompts restart while preserving actual capacity');
 await panel.screenshot({path:path.join(out,'known-model-pending.png')});
 await refresh({...state,selected:'mib:8192',dedicatedBytes:8192*1048576,verification:'verified',pendingOption:'mib:8192'});
 assert.equal(await page.getByTestId('dedicated-current').textContent(),'8 GiB');assert.equal(await page.getByRole('status').textContent(),'已生效');cases.push('verified actual capacity alone is displayed as effective');
 await refresh({...state,selected:'mib:8192',verification:'not_effective',pendingOption:'mib:8192'});
 assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');assert.equal(await page.getByRole('status').textContent(),'未生效');
 assert.equal(await page.getByRole('button',{name:'保存',exact:true}).isEnabled(),false);cases.push('failed adoption displays real capacity and no redundant same-target save');
 await select('自动');await save();await page.getByRole('status').filter({hasText:'请重启'}).waitFor();cases.push('Auto can be saved explicitly after a target fails verification');
 await refresh({...state,selected:'auto',dedicatedBytes:512*1048576,verification:'default_read'});
 assert.equal(await page.getByTestId('dedicated-current').textContent(),'512 MiB');assert.equal(await page.getByRole('status').count(),0);cases.push('default mode displays the actual capacity without explanatory text');
 await refresh({...state,selected:'mib:4096',verification:'unconfirmed'});
 assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');assert.equal(await page.getByRole('status').textContent(),'未确认');assert.equal(await page.getByRole('button',{name:'保存',exact:true}).isEnabled(),false);
 cases.push('existing firmware mismatch keeps the true current value without an Intel reapply action');
 for(const bad of [null,{...state,available:false},{...state,ok:false},{...state,settingRoute:undefined},{...state,settingRoute:'read-only'},{...state,provider:'intel-gmm'},{...state,provider:'amd-unsupported'},{...state,capacityRead:false},{...state,dedicatedBytes:0},{...state,dedicatedBytes:null},{...state,pnpId:''},{...state,options:[]}]){
  await refresh(bad);assert.equal(await panel.count(),0);
 }
 cases.push('failed reads missing actual capacity unconfirmed routes and malformed identity all hide controls');
 await refresh(state);await select('8 GiB');await refresh({...state,fence:'fresh-fence'});
 assert.equal(await page.locator('.dd-trigger').textContent(),'8 GiB');cases.push('fresh bound rereads preserve an unsaved offered choice');
 await resetCalls();await save();await page.getByRole('status').filter({hasText:'请重启'}).waitFor();
 assert.equal(await page.evaluate(()=>window.__fixture.calls.find(c=>c.cmd.endsWith('.set')).args.fence),'fresh-fence');cases.push('retained selection saves using the fresh fence');
 await refresh(state);await select('8 GiB');await refresh({...state,pnpId:'PCI\\other',fence:'other-fence'});
 assert.equal(await page.locator('.dd-trigger').textContent(),'2 GiB');cases.push('adapter identity change resets the unsaved choice');
 await refresh(state);await select('8 GiB');await resetCalls();
 await page.evaluate(()=>{window.__fixture.offset=31000;window.__fixture.defer=true});await save();
 assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),0);cases.push('save refuses an expired snapshot even before the UI timer runs');
 await page.evaluate(()=>window.__tick());await page.waitForTimeout(50);assert.equal(await panel.count(),0);cases.push('expired capacity hides controls while the fresh read is pending');
 await page.evaluate(()=>window.__show(false));await page.waitForTimeout(30);
 await page.evaluate(v=>{for(const resolve of window.__fixture.pending.splice(0))resolve(v)},state);await page.waitForTimeout(50);assert.equal(await panel.count(),0);cases.push('a late inactive-page response cannot restore old controls');
 await page.evaluate(v=>{window.__fixture.offset=0;window.__fixture.defer=false;window.__fixture.state=v;window.__show(true)},state);await panel.waitFor();cases.push('reactivation obtains a new readable capacity');
 await page.evaluate(()=>window.__fixture.fail=true);await refresh(state);assert.equal(await panel.count(),0);cases.push('transport read failure hides previously readable controls');
 await page.evaluate(()=>window.__fixture.fail=false);await refresh(state);await select('8 GiB');
 await page.evaluate(v=>{window.__fixture.nativeSave={...v,ok:false,reason:'[firmware.return] very long technical error'};window.__fixture.nativeReadAfterSave=v},state);
 await save();await page.getByRole('alert').waitFor();assert.equal(await page.getByRole('alert').textContent(),'保存未确认，请读取');assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');
 assert.equal((await panel.textContent()).includes('[firmware.return]'),false);assert.equal(await page.getByRole('status').count(),0);cases.push('failed save keeps real capacity gives a short error and does not request restart');
 await page.evaluate(()=>{delete window.__fixture.nativeSave;delete window.__fixture.nativeReadAfterSave;window.__fixture.failSave=true});await refresh(state);await select('8 GiB');await save();
 await page.getByRole('alert').filter({hasText:'保存未确认'}).waitFor();assert.equal(await page.getByRole('status').count(),0);cases.push('unknown write reply never becomes a restart or success claim');
 await page.evaluate(()=>{window.__fixture.failSave=false;window.__fixture.deferSave=true});await refresh(state);await select('8 GiB');await resetCalls();await save();
 await page.getByRole('button',{name:'保存中',exact:true}).waitFor();assert.equal(await page.getByRole('button',{name:'读取',exact:true}).isEnabled(),false);
 await page.getByRole('button',{name:'保存中',exact:true}).evaluate(el=>el.click());assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),1);
 await page.evaluate(v=>{window.__fixture.deferSave=false;window.__fixture.state=v;for(const resolve of window.__fixture.pendingSaves.splice(0))resolve(v)},{...state,selected:'mib:8192',verification:'pending_restart'});
 await page.getByRole('status').filter({hasText:'请重启'}).waitFor();cases.push('a pending save disables duplicate writes and explicit reads');
 if(windowsResult){
  for(const [name,response] of Object.entries(windowsResult.responses)){
   await refresh(response);
   const allowed=response.ok&&response.available&&response.settingRoute==='known-model'&&['rog-apu','rog-vram'].includes(response.provider);
   assert.equal(await panel.count(),allowed?1:0,name);
   if(allowed)assert.equal(await page.getByTestId('dedicated-current').textContent(),await page.evaluate(v=>window.__model.formatDedicatedCapacity(v),response.dedicatedBytes),name);
  }
  cases.push('all Windows responses honor the public model policy and exact actual capacity in the real Vue component');
  await refresh(windowsResult.responses['public-g3e-read-only']);assert.equal(await panel.count(),0);
  await refresh({...windowsResult.responses['public-g3e-read-only'],available:true,settingRoute:'known-model'});assert.equal(await panel.count(),0);
  cases.push('Intel diagnostics and a forged writable marker cannot expose an unconfirmed setter');
  await refresh(windowsResult.responses['rog-initial']);await resetCalls();
  await page.evaluate(v=>window.__fixture.nativeSave=v,windowsResult.responses['rog-pending']);await select('4 GiB');await save();
  await page.getByRole('status').filter({hasText:'请重启'}).waitFor();assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.calls.find(c=>c.cmd.endsWith('.set')).args),{fence:windowsResult.responses['rog-initial'].fence,option:'mib:4096',confirmed:true});
  cases.push('production ROG read fence and saved configuration drive the single-button save UI');
  await page.evaluate(()=>delete window.__fixture.nativeSave);
  await refresh(windowsResult.responses['rog-not-effective']);assert.equal(await page.getByRole('status').textContent(),'未生效');assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');
  await refresh(windowsResult.responses['rog-verified']);assert.equal(await page.getByRole('status').textContent(),'已生效');assert.equal(await page.getByTestId('dedicated-current').textContent(),'4 GiB');
  cases.push('production reboot verification changes only with exact capacity adoption');
  for(const name of ['rog-config-ignored','rog-open-denied','rog-firmware-refused']){
   await refresh(windowsResult.responses['rog-initial']);await resetCalls();
   await page.evaluate(v=>{window.__fixture.nativeSave=v.save;window.__fixture.nativeReadAfterSave=v.read},{save:windowsResult.responses[name],read:windowsResult.responses['rog-initial']});
   await select('4 GiB');await save();await page.getByRole('alert').waitFor();
   assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');assert.equal(await page.getByRole('status').count(),0);assert.equal(await page.getByRole('alert').textContent(),'保存未确认，请读取');
   assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),1);
   cases.push(`${name} real failure has a short message and no invented restart or capacity`);
   await page.evaluate(()=>{delete window.__fixture.nativeSave;delete window.__fixture.nativeReadAfterSave});
  }
 }
 await refresh(state);await page.getByRole('button',{name:'读取',exact:true}).click();assert.equal(await page.getByRole('alert').count(),0);cases.push('explicit reading clears the previous short save error');
 await page.setViewportSize({width:520,height:320});await panel.screenshot({path:path.join(out,'known-model-menu.png')});
 assert(await panel.evaluate(el=>el.scrollWidth<=el.clientWidth));cases.push('the minimal menu fits a handheld viewport without horizontal overflow');
 for(const height of [720,800]) {
  await page.setViewportSize({width:1280,height});
  await page.evaluate(v=>{const f=window.__fixture;f.state=v;f.defer=false;f.deferSave=false;f.fail=false;f.failSave=false;f.offset=0;delete f.nativeSave;delete f.nativeReadAfterSave;window.__settings();},state);
  await page.locator('[data-settings-section="device"]').waitFor();
  assert.equal(await panel.count(),0,'inactive Settings tab must not mount the GPU consumer');
  assert.equal(await page.evaluate(()=>window.__fixture.timers.size),0);
  await resetCalls();await page.locator('[data-settings-section="device"]').click();await panel.waitFor();
  const initial=await page.evaluate(()=>window.__fixture.calls);
  assert.equal(initial.filter(c=>c.cmd==='gpu.dedicatedMemory.get').length,1,'device entry must request a real read once');
  assert.equal(initial.filter(c=>c.cmd==='gpu.dedicatedMemory.set').length,0,'opening the entry must not change VRAM');
  assert.equal(await page.getByTestId('dedicated-current').textContent(),'2 GiB');
  await panel.screenshot({path:path.join(out,'settings-device-'+height+'.png')});
  await page.locator('[data-settings-section="appearance"]').click();await panel.waitFor({state:'detached'});
  assert.equal(await page.evaluate(()=>window.__fixture.timers.size),0,'leaving device tab stops its refresher');
  const reads=await page.evaluate(()=>window.__fixture.calls.length);await page.evaluate(()=>{window.__tick();window.dispatchEvent(new Event('focus'));});
  assert.equal(await page.evaluate(()=>window.__fixture.calls.length),reads,'inactive tab cannot keep probing');
  await page.locator('[data-settings-section="device"]').click();await panel.waitFor();
  assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),0,'reentry must not replay a write');
  await page.locator('[data-settings-section="appearance"]').click();await panel.waitFor({state:'detached'});
  cases.push('actual Settings device-tab insertion reads once without writes, is reachable and releases resources at 1280x'+height);
 }
 await page.evaluate(()=>window.__unmount());assert.equal(await page.evaluate(()=>window.__fixture.timers.size),0);
 assert.deepEqual(errors,[]);
 const result={ok:true,cases,sourceHashes,settingsEntryValidated:true,hardwareAccess:false,registryAccess:false,nativeIpc:false,windowsResponses:windowsResult?Object.keys(windowsResult.responses).length:0};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));
 console.log(`GPU_DEDICATED_MEMORY_BROWSER_SELFTEST_OK cases=${cases.length} hardware=none registry=none`);
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve))}
