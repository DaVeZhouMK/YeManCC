// Actual Vue mirrors + real presenter. IPC/device/storage boundaries are in-memory only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const root=process.cwd(),req=createRequire(path.join(root,'package.json'));
const out=process.env.YMCC_UI_BROWSER_OUT||'G:/YeManCC-Work/_scratch/controller-oem-preview-mainline-20261013';
fs.mkdirSync(out,{recursive:true});
const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const tables={msi:'kMsiClawKeys',msig3e:'kMsiClawG3EKeys',rog:'kRogKeys',rogXbox:'kRogXbox2025Keys',aya:'kAyaneoCecKeys',legion:'kLegionGoKeys',legions:'kLegionSKeys',gpd:'kGpdLegacyKeys',gpdWin5:'kGpdWin5Keys',deck:'kSteamDeckKeys'};
const examples={};
for(const [name,table] of Object.entries(tables)){
 const body=native.match(new RegExp('static const OemKeyDef '+table+'\\[\\] = \\{([\\s\\S]*?)\\n\\};'))?.[1];assert(body,table);
 examples[name]={familyId:name.startsWith('legion')?'lenovo-legion-go':name.startsWith('msi')?'msi-claw':name==='rogXbox'?'asus-rog-ally-x':name==='gpdWin5'?'gpd':name,supported:true,keys:[...body.matchAll(/\{"([^"]+)", "([^"]+)", (\d+), OemKeyForm::\w+, (true|false)\}/g)].map(m=>({keyId:m[1],label:m[2],backIndex:Number(m[3])||null,triggerCapability:m[4]==='true'?'click-only':'full'}))};
}
examples.none={familyId:'unknown',supported:false,keys:[]};
// Compile the production key resolver/publication function with catalog-only stubs.
const maskStart=native.indexOf('static bool nativeKeyboardShortcutMaskBit(');
const maskEnd=native.indexOf('// Canonical strings avoid JS precision loss',maskStart);
const helperStart=native.indexOf('static json gamepadOemPressedKeys(uint64_t mask) {');
const helperEnd=native.indexOf('// GP-FAMILY-CAPABILITY-4 C4-D2：解析',helperStart);
assert(maskStart>0&&helperStart>maskStart);
const nativeTest=`#include <Windows.h>
#include <Xinput.h>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <iostream>
#include "json.hpp"
using json=nlohmann::json;
struct OemKeyDef{const char* keyId;};struct OemCatalogRef{const OemKeyDef* keys;int count;};
struct {int family=0;std::string deviceClass;}g_machineIdentity;
static const OemKeyDef msi[]={{"claw"},{"qs"},{"m1"},{"m2"}};
static const OemKeyDef rog[]={{"cc"},{"ac"},{"m1"},{"m2"},{"lib"}};
static const OemKeyDef legion[]={{"legionfrontl"},{"legionfrontr"},{"legiony2"},{"legionm2"}};
static OemCatalogRef oemCatalogForFamily(int family,const std::string&){switch(family){case 0:return{msi,4};case 1:return{rog,5};case 2:return{legion,4};default:return{nullptr,0};}}
${native.slice(maskStart,maskEnd)}
${native.slice(helperStart,helperEnd)}
int main(){
assert(gamepadOemPressedKeys(0).empty());
assert(gamepadOemPressedKeys((1ull<<25)|(1ull<<20))==json::array({"claw","m1"}));
assert(gamepadOemPressedKeys((1ull<<22)|(1ull<<62)|XINPUT_GAMEPAD_A).empty());
assert(gamepadOemPressedKeys((1ull<<26)|(1ull<<21))==json::array({"qs","m2"}));
g_machineIdentity.family=1;
assert(gamepadOemPressedKeys((1ull<<22)|(1ull<<23)|(1ull<<24)|(1ull<<25))==json::array({"cc","ac","lib"}));
g_machineIdentity.family=2;
assert(gamepadOemPressedKeys((1ull<<61)|(1ull<<62)|(1ull<<57)|(1ull<<59))==json::array({"legionfrontl","legionfrontr","legiony2","legionm2"}));
assert(gamepadOemPressedKeys(1ull<<62)==json::array({"legionfrontr"}));
g_machineIdentity.family=3;assert(gamepadOemPressedKeys(~0ull).empty());
std::cout<<"OEM_NATIVE_PUBLICATION_PASS 8 cases; deviceOperations=0\\n";}
`;
fs.writeFileSync(path.join(out,'oem-native-fixture.cpp'),nativeTest);
fs.writeFileSync(path.join(out,'oem-native-fixture.cmd'),`@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT "${out}/oem-native-fixture.cpp" /I"${root}/deps/json" /Fo"${out}/oem-native-fixture.obj" /Fe"${out}/oem-native-fixture.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/oem-native-fixture.exe"\r\nexit /b %errorlevel%\r\n`);
const nativeRun=spawnSync('cmd.exe',['/d','/c',path.join(out,'oem-native-fixture.cmd')],{encoding:'utf8',windowsHide:true});
fs.writeFileSync(path.join(out,'oem-native-fixture.log'),(nativeRun.stdout||'')+(nativeRun.stderr||''));assert.equal(nativeRun.status,0,nativeRun.stdout+nativeRun.stderr);
const gate=native.slice(native.indexOf('    static int publishedDriverSetup = -1;'),native.indexOf('static void gamepadReadState(bool',native.indexOf('    static int publishedDriverSetup = -1;')));
assert(gate.includes('oemMask == publishedOemMask')&&gate.includes('publishedOemMask = oemMask;')&&gate.includes('nativeOemKeyMask(false)'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const {chromium}=createRequire((process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')+'/_oem-preview.cjs')('playwright');
const styles=[],errors=[],cases=[];
const mocks={
 '@/bridge/ipc':`export function on(name,cb){const set=window.__fixture.events[name]??=new Set();set.add(cb);return()=>set.delete(cb)};export function isNativeRuntime(){return true};export async function invoke(){return {}}`,
 '@/bridge/yeman':`export async function summonGet(){return structuredClone(window.__fixture.gamepad)};export async function summonSet(){window.__fixture.writes++;return summonGet()};export async function oemKeysGet(){window.__fixture.catalogReads++;return structuredClone(window.__fixture.catalog)};export async function setShortcutRecording(){return {ok:true}}`,
 '@/bridge/gameproc':`export async function getGameInputRedistState(){return {mouseInterfaceAvailable:true}};export async function installGameInputRedist(){};export async function setMouseBackend(){window.__fixture.writes++;return {ok:true}}`,
 '@/bridge/settingsRepository':`export async function loadSettings(){return structuredClone(window.__fixture.settings)};export async function compareAndSwapInputSettings(){window.__fixture.writes++;return {ok:false}}`,
 '@/bridge/api':`export const fs={},settingsStore={},shell={}`,
 '@/gamepad/focus':`export function focusGamepadElement(n){n?.focus()};export function getGamepadPopupPlacement(){return {style:{},above:false}}`,
};
const bundle=await build({stdin:{resolveDir:root,loader:'ts',contents:`
import {createApp,h} from 'vue';import Card from './src/components/ControllerShortcutsCard.vue';import Editor from './src/views/ControllerShortcutEditorView.vue';import Preview from './src/components/GamepadVisualizer.vue';
import {normalizeSettings} from './src/bridge/settingsRepository.ts';import {NATIVE_DEFAULT_SHORTCUTS,newControllerShortcutRule} from './src/bridge/controllerShortcutRules.ts';
const examples=${JSON.stringify(examples)};
window.__fixture={events:{},writes:0,catalogReads:0,catalog:examples.msi,gamepad:{...Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(d=>[d.defaultKey,false])),mouseBackend:'joyxoff'}};
window.__emit=(name,data)=>{for(const cb of window.__fixture.events[name]??[])cb(data)};
let app;window.__mount=(mode='card',device='msi')=>{
 app?.unmount();const f=window.__fixture;f.catalog=examples[device];f.events={};
 f.settings=normalizeSettings({input:{buttonMapping:{rules:{schemaVersion:2,shortcutRules:[{...newControllerShortcutRule('oem-test','window.summon'),inputs:[{source:'oem',code:f.catalog.keys[0]?.keyId??'claw'}],enabled:true,trigger:'press'}]}}}});
 app=createApp({render:()=>h(mode==='editor'?Editor:mode==='card'?Card:Preview,mode==='preview'?{settings:{},oemKeys:f.catalog,showTestMode:false}:{})});app.mount('#app');
};window.__mount();`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'oem-preview',setup(b){
 b.onResolve({filter:/.*/},a=>a.path in mocks?{path:a.path,namespace:'mock'}:undefined);
 b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
 b.onLoad({filter:/\.vue$/},a=>{const {descriptor}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path});const id='data-v-'+createHash('sha1').update(a.path).digest('hex').slice(0,8);const c=compileScript(descriptor,{id,inlineTemplate:true});for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.deepEqual(css.errors,[]);styles.push(css.code)}return{contents:c.content.replace('export default','const C =')+'\nC.__scopeId='+JSON.stringify(id)+';export default C;',loader:'ts',resolveDir:path.dirname(a.path)}});
 }}]});
const html='<!doctype html><meta charset="utf-8"><style>'+fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+styles.join('\n')+'body{background:var(--bg-solid);margin:0;font-family:system-ui;color:var(--text)}#app{max-width:640px;padding:20px;margin:auto}.card{border:1px solid #293745;border-radius:12px;padding:16px;background:var(--bg-panel)}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{if(request.url.startsWith('/gamepad-base.png')){response.setHeader('Content-Type','image/png');response.end(fs.readFileSync(path.join(root,'public/gamepad-base.png')));return}response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?bundle.outputFiles[0].text:html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:1280,height:800}});page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);
 await page.locator('[data-oem-key="claw"]').waitFor();
 assert.deepEqual(await page.locator('.oem-special-exterior .oem-name').allTextContents(),['CLAW','QS']);
 assert.deepEqual(await page.locator('.oem-back-code').allTextContents(),['M1','M2']);
 await page.locator('.shortcut-toggle').first().focus();await page.waitForFunction(()=>document.querySelector('[data-oem-key="claw"]').classList.contains('mapped'));cases.push('controller card uses real catalog and selected rule');
 const send=async keys=>page.evaluate(keys=>window.__emit('gamepad.state',{connected:true,name:'fixture',buttons:Array(16).fill(false),axes:[0,0,0,0],oemPressedKeys:keys}),keys);
 await send(['qs','m1']);assert.equal(await page.locator('.oem-capsule.live,.oem-back-key.live').count(),2);
 await page.screenshot({path:path.join(out,'mainline-controller.png')});
 await send([]);assert.equal(await page.locator('.oem-capsule.live,.oem-back-key.live').count(),0);cases.push('physical press/release updates capsules and rear independently of standard buttons');
 await send(['qs']);await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.hidden')));await send([]);assert.equal(await page.locator('.oem-capsule.live').count(),1);
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.shown')));assert.equal(await page.locator('.oem-capsule.live').count(),0);cases.push('hidden UI freezes rendering and replays current release on return');
 assert.equal(await page.evaluate(()=>window.__fixture.catalogReads),1);cases.push('main card queries catalog once across state/visibility changes');
 await page.evaluate(()=>window.__mount('editor','msi'));await page.locator('.oem-special-exterior [data-oem-key="claw"]').waitFor();assert.equal(await page.locator('.oem-capsule.mapped').getAttribute('data-oem-key'),'claw');
 assert.deepEqual(await page.locator('.oem-actions .oem-action').allTextContents(),['CLAW','QS']);
 await page.locator('.oem-actions .oem-action').filter({hasText:'QS'}).click();assert.equal(await page.locator('.oem-capsule.mapped').getAttribute('data-oem-key'),'qs');
 await page.locator('.oem-back-slot').first().click();assert.equal(await page.locator('.oem-back-key.mapped').getAttribute('data-oem-key'),'m2');assert.equal(await page.locator('.oem-capsule.mapped').count(),0);cases.push('actual editor key selection synchronizes mirror without saving');
 await send(['m2']);await page.evaluate(()=>document.querySelector('.editor-dialog').scrollTop=0);await page.screenshot({path:path.join(out,'mainline-editor.png')});

 // These expectations use the physical layout, independently of catalog iteration order.
 // The SVG is a front view with rear paddles shown through it.
 for(const device of ['msi','msig3e']) {
  await page.evaluate(d=>window.__mount('preview',d),device);
  await page.locator('.oem-back-key[data-oem-key="m1"]').waitFor();
  assert.deepEqual(await page.locator('.oem-special-exterior .oem-name').allTextContents(),device==='msig3e'?['QS','CLAW']:['CLAW','QS']);
  const rearX=await page.locator('.oem-back-key').evaluateAll(nodes=>Object.fromEntries(nodes.map(n=>[
   n.dataset.oemKey,Number(n.querySelector('.oem-back-paddle').parentElement.getAttribute('transform').match(/translate\(([^ ]+)/)[1])])));
  assert(rearX.m2<210 && rearX.m1>210,'MSI rear preview uses left M2 and right M1');
  for(const key of ['claw','qs','m1','m2']) {
   await send([key]);
   assert.equal(await page.locator('.oem-capsule.live,.oem-back-key.live').count(),1);
   assert.equal(await page.locator('.oem-capsule.live,.oem-back-key.live').getAttribute('data-oem-key'),key);
   await send([]);assert.equal(await page.locator('.oem-capsule.live,.oem-back-key.live').count(),0);
  }
  await page.evaluate(d=>window.__mount('editor',d),device);
  await page.locator('.oem-actions .oem-action').first().waitFor();
  assert.deepEqual(await page.locator('.oem-actions .oem-action').allTextContents(),device==='msig3e'?['QS','CLAW']:['CLAW','QS']);
  for(const label of ['QS','CLAW']) {
   await page.locator('.oem-actions .oem-action').filter({hasText:label}).click();
   assert.equal(await page.locator('.oem-capsule.mapped').getAttribute('data-oem-key'),label==='QS'?'qs':'claw');
  }
  for(const [slot,key,label] of [[0,'m2','M2'],[1,'m1','M1']]) {
   assert.equal(await page.locator('.oem-back-slot').nth(slot).getAttribute('title'),label);
   await page.locator('.oem-back-slot').nth(slot).click();
   assert.equal(await page.locator('.oem-back-key.mapped').getAttribute('data-oem-key'),key);
  }
  await send(['qs','m2']);await page.evaluate(()=>document.querySelector('.editor-dialog').scrollTop=0);
  await page.screenshot({path:path.join(out,device+'-physical-layout-editor.png')});
  cases.push(device+' physical front/rear order; real editor slots and live keys retain canonical identity');
 }


 // Front-view positions are verified independently of array ordering. The
 // Classic and Xbox ROG share the independently confirmed front-view order.
 // The already-correct MSI catalogs remain separate.
 for(const device of ['rogXbox','rog']) {
  const expected=[['m2','M2'],['m1','M1']];
  await page.evaluate(d=>window.__mount('card',d),device);
  await page.locator('.oem-back-key[data-oem-key="m1"]').waitFor();
  assert.deepEqual(await page.locator('.oem-special-exterior .oem-name').allTextContents(),['CC','AC','LIB']);
  const rearX=await page.locator('.oem-back-key').evaluateAll(nodes=>Object.fromEntries(nodes.map(n=>[
   n.dataset.oemKey,Number(n.querySelector('.oem-back-paddle').parentElement.getAttribute('transform').match(/translate\(([^ ]+)/)[1])])));
  assert(rearX[expected[0][0]]<210 && rearX[expected[1][0]]>210,device+' uses its own rear physical positions');
  for(const key of ['m1','m2']) {
   await send([key]);assert.equal(await page.locator('.oem-back-key.live').count(),1);
   assert.equal(await page.locator('.oem-back-key.live').getAttribute('data-oem-key'),key);
   await send([]);assert.equal(await page.locator('.oem-back-key.live').count(),0);
  }
  await page.evaluate(d=>window.__mount('editor',d),device);
  await page.locator('.oem-back-slot').first().waitFor();
  assert.deepEqual(await page.locator('.oem-actions .oem-action').allTextContents(),['CC','AC','LIB']);
  for(const [slot,[key,label]] of expected.entries()) {
   assert.equal(await page.locator('.oem-back-slot').nth(slot).getAttribute('title'),label);
   await page.locator('.oem-back-slot').nth(slot).click();
   assert.equal(await page.locator('.oem-back-key.mapped').getAttribute('data-oem-key'),key);
   assert.equal(await page.locator('.oem-back-key.mapped .oem-back-code').textContent(),label);
  }
  await send(['m2']);await page.evaluate(()=>document.querySelector('.editor-dialog').scrollTop=0);
  await page.screenshot({path:path.join(out,device+'-physical-layout-editor.png')});
  cases.push(device+' real card/editor positions, selection and press/release use the same canonical rear key');
 }


 // Win5 keeps semantic L4 on the front-view left and R4 on the right.
 // Source parser/keyboard parity is tested separately with real native functions.
 await page.evaluate(()=>window.__mount('card','gpdWin5'));
 await page.locator('.oem-back-key[data-oem-key="l4"]').waitFor();
 const win5X=await page.locator('.oem-back-key').evaluateAll(nodes=>Object.fromEntries(nodes.map(n=>[
  n.dataset.oemKey,Number(n.querySelector('.oem-back-paddle').parentElement.getAttribute('transform').match(/translate\(([^ ]+)/)[1])])));
 assert(win5X.l4<210 && win5X.r4>210,'Win5 L4/R4 geometry agrees with canonical side');
 assert.deepEqual(await page.locator('.oem-special-exterior .oem-name').allTextContents(),['Gamepad','Keyboard','Home']);
 for(const key of ['l4','r4']){await send([key]);assert.equal(await page.locator('.oem-back-key.live').getAttribute('data-oem-key'),key);await send([]);assert.equal(await page.locator('.oem-back-key.live').count(),0)}
 await page.evaluate(()=>window.__mount('editor','gpdWin5'));await page.locator('.oem-back-slot').first().waitFor();
 for(const [slot,key,label] of [[0,'l4','L4'],[1,'r4','R4']]){
  assert.equal(await page.locator('.oem-back-slot').nth(slot).getAttribute('title'),label);
  await page.locator('.oem-back-slot').nth(slot).click();assert.equal(await page.locator('.oem-back-key.mapped').getAttribute('data-oem-key'),key);
 }
 await send(['l4']);await page.evaluate(()=>document.querySelector('.editor-dialog').scrollTop=0);
 await page.screenshot({path:path.join(out,'win5-physical-layout-editor.png')});
 cases.push('Win5 card/editor L4 left R4 right; canonical key selection and press/release independent of the virtual target');

 for(const device of ['rog','aya','gpd','deck','legion','legions','none']){
  await page.evaluate(d=>window.__mount('preview',d),device);await page.locator('.viz').waitFor();
  const special=examples[device].keys.filter(k=>k.backIndex===null&&!(device==='legion'&&['legionl','legionm2'].includes(k.keyId)));
  assert.deepEqual(await page.locator('.oem-special-exterior .oem-name').allTextContents(),special.map(k=>k.label));
  assert.equal(await page.locator('.oem-special-exterior').count(),special.length?1:0);
  assert.equal(await page.locator('.oem-back-key').count(),examples[device].keys.filter(k=>k.backIndex!==null).length);
  if(device==='legion'){assert.equal(await page.locator('.oem-additional-back .oem-capsule').count(),2);await send(['legionfrontr','legionm2']);assert.equal(await page.locator('.oem-capsule.live').count(),2)}
  cases.push(device+' catalog separation and empty-area behavior');
 }
 await page.evaluate(()=>window.__mount('preview','aya'));
 for(const width of [1280,800,420,320])for(const theme of ['', 'red-black','cyberpunk']){
  await page.setViewportSize({width,height:800});await page.evaluate(t=>document.documentElement.dataset.theme=t,theme);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth||[...document.querySelectorAll('.oem-capsules')].some(n=>n.scrollWidth>n.clientWidth)),false);
 }
 cases.push('long real names wrap without overflow at four widths and three themes');
 assert.equal(await page.evaluate(()=>window.__fixture.writes),0);assert.deepEqual(errors,[]);
 fs.writeFileSync(path.join(out,'oem-preview-results.json'),JSON.stringify({pass:true,nativeCases:8,cases,errors,deviceOperations:0,productionStatusSha256:createHash('sha256').update(native.slice(helperStart,helperEnd)).digest('hex')},null,2));
 console.log('OEM_PREVIEW_PASS native=8 browser='+cases.length+' deviceOperations=0');
}finally{await browser?.close();await new Promise(r=>server.close(r));}
