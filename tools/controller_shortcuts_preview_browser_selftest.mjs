// Actual mainline shortcut card + shared artwork; every native/storage port is in memory.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { diagramCatalogFixtures } from './controller_diagram_catalog_fixtures.mjs';
const root=process.cwd(),out=process.env.YMCC_CARD_PREVIEW_OUT||'G:/YeManCC-Work/Build/Tasks/ControllerShortcutBlinkFix-20261017/card-preview',req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_task.cjs')('playwright');
fs.mkdirSync(out,{recursive:true});
const styles=[],files=new Set(),mocks={
 '@/bridge/api':'export const fs={},settingsStore={},shell={}',
 '@/bridge/yeman':`export async function summonGet(){return structuredClone(window.fixture.gamepad)};export async function summonSet(p){Object.assign(window.fixture.gamepad,p);window.fixture.nativeWrites++;return summonGet()};export async function oemKeysGet(){return structuredClone(window.fixture.catalog)}`,
 '@/bridge/gameproc':`export async function getGameInputRedistState(){return {mouseInterfaceAvailable:true}};export async function installGameInputRedist(){throw Error('No native port')};export async function setMouseBackend(){throw Error('No native port')}`,
 '@/bridge/settingsRepository':`export async function loadSettings(){return structuredClone(window.fixture.settings)};export async function compareAndSwapInputSettings(){window.fixture.writes++;throw Error('Read-only preview')}`,
 '@/bridge/uiLifecycle':`export const isUiVisible=()=>true;export function onUiVisibilityChange(){return ()=>{}}`,
 '@/bridge/ipc':`export const isNativeRuntime=false;export const getLastGamepadState=()=>null;export function on(name,fn){return window.fixture.on(name,fn)};export async function invoke(){throw Error('No native port')}`,
};
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:`
import {createApp,h,ref,nextTick} from 'vue';import Card from './src/components/ControllerShortcutsCard.vue';
import {NATIVE_DEFAULT_SHORTCUTS,nativeDefaultRule} from './src/bridge/controllerShortcutRules.ts';
const persona=ref('steamdeck'),handlers=new Map();window.fixture={writes:0,nativeWrites:0,catalog:${JSON.stringify(diagramCatalogFixtures.two)},gamepad:{mouseBackend:'joyxoff',...Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(d=>[d.defaultKey,true]))},settings:{input:{revision:10,buttonMapping:{rules:{schemaVersion:2,shortcutRules:NATIVE_DEFAULT_SHORTCUTS.map(d=>nativeDefaultRule(d.defaultKey))}}}}};
window.fixture.setPersona=async value=>{persona.value=value;await nextTick()};window.fixture.on=(name,fn)=>{if(!handlers.has(name))handlers.set(name,new Set());handlers.get(name).add(fn);return ()=>handlers.get(name)?.delete(fn)};
window.fixture.state=value=>{for(const fn of handlers.get('gamepad.state')||[])fn({connected:true,name:'Memory fixture',buttons:Array(18).fill(false),axes:[0,0,0,0],oemPressedKeys:[],...value})};
createApp({render:()=>h(Card,{persona:persona.value,steamDeckActive:false,onOpenEditor(){window.fixture.editorRequested=true}})}).mount('#app');`},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'actual-shortcut-card',setup(b){
 b.onResolve({filter:/^@\//},args=>{if(mocks[args.path])return{path:args.path,namespace:'memory'};const target=path.join(root,'src',args.path.slice(2));return{path:[target,target+'.ts',target+'.vue'].find(p=>fs.existsSync(p))||target}});
 b.onResolve({filter:/^\.\/ipc$/},args=>args.resolveDir===path.join(root,'src/bridge')?{path:'@/bridge/ipc',namespace:'memory'}:undefined);
 b.onLoad({filter:/.*/,namespace:'memory'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
 b.onLoad({filter:/\.vue$/},args=>{files.add(path.relative(root,args.path).replaceAll('\\','/'));const {descriptor}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8),compiled=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.equal(css.errors.length,0);styles.push(css.code)}return{contents:compiled.content.replace('export default','const _component =')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)}});
 }}]});
const css=fs.readFileSync('src/styles/tokens.css','utf8')+'\n'+styles.join('\n');
fs.writeFileSync(out+'/card.html','<!doctype html><html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>YMCC · 手柄快捷操作</title><style>'+css+'\nbody{margin:0;padding:16px;background:var(--bg-solid)}#app{max-width:760px;margin:0 auto}</style><main id="app"></main><script>'+result.outputFiles[0].text.replace(/<\/script/gi,'<\\/script')+'</script></html>');
const checks=[],errors=[],metrics=[],check=(v,n)=>{assert(v,n);checks.push(n)},browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
try{const page=await browser.newPage({viewport:{width:1280,height:720},deviceScaleFactor:1,reducedMotion:'no-preference'});page.on('pageerror',e=>errors.push(e.message));await page.goto(pathToFileURL(out+'/card.html').href);await page.locator('.shortcut-toggle').first().waitFor();
for(const height of [720,800]){
 await page.setViewportSize({width:1280,height});const modelMetrics=[];
 for(const [name,persona]of [['Steam Deck','steamdeck'],['PS5','dualsense-edge'],['Xbox','elite']]){
  await page.evaluate(p=>window.fixture.setPersona(p),persona);
  const layout=await page.evaluate(()=>{const box=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return{x:r.x,y:r.y,w:r.width,h:r.height,bottom:r.bottom}};return{title:box('.shortcuts-card .card-head'),preview:box('.shortcuts-card .viz'),diagram:box('.device-diagram'),frame:box('.controller-preview-frame'),grid:box('.shortcut-grid'),overflow:document.documentElement.scrollWidth>innerWidth}});
  modelMetrics.push({name,...layout});
  check(layout.title.bottom<=layout.preview.y&&layout.preview.bottom<=layout.grid.y,height+'p '+name+' preview sits below the card title and above shortcut controls');
  check(!layout.overflow&&layout.diagram.x>=0&&layout.diagram.x+layout.diagram.w<=1280,height+'p '+name+' proportional preview remains inside the card');
  await page.locator('.shortcut-editor-open').focus();
  const first=page.locator('.shortcut-toggle').first();await first.hover();
  check(await page.locator('.device-vector [data-fill-for~="lb"][data-fill-active=true]').count()>0,name+' mouse hover previews the exact first combination without clicking');
  check(await page.locator('.device-vector [data-fill-active=true]').evaluateAll(nodes=>nodes.length>0&&nodes.every(n=>n.getAnimations().length>0)),name+' hover reuses associated-key blinking');
  await page.locator('.shortcut-toggle').nth(1).focus();
  check(await page.locator('.device-vector [data-fill-for~="b"][data-fill-active=true]').count()>0&&await page.locator('.device-vector [data-fill-for~="lb"][data-fill-active=true]').count()===0,name+' controller focus previews its exact key even with the mouse still over another card');
  await page.mouse.move(1,1);
  check(await page.locator('.device-vector [data-fill-active=true]').evaluateAll(nodes=>nodes.length>0&&nodes.every(n=>n.getAnimations().length>0)),name+' controller focus also selects blinking keys');
  const after=await page.locator('.device-diagram').boundingBox();check(Math.abs(after.y-layout.diagram.y)<.1&&Math.abs(after.height-layout.diagram.h)<.1,name+' hover and focus keep preview fixed');
  await page.screenshot({path:out+'/card-'+persona+'-'+height+'.png',fullPage:true});
 }
 const deck=modelMetrics[0];for(const m of modelMetrics.slice(1)){
  check(Math.abs(m.diagram.h-deck.diagram.h)<.1,height+'p '+m.name+' exactly matches Steam Deck display height');
  check(Math.abs(m.diagram.y-deck.diagram.y)<.1,height+'p '+m.name+' aligns with Steam Deck at the top');
  check(Math.abs((m.diagram.x+m.diagram.w/2)-(deck.diagram.x+deck.diagram.w/2))<.1,height+'p '+m.name+' stays horizontally centered');
 }
 metrics.push({height,models:modelMetrics});
}
check(await page.evaluate(()=>window.fixture.writes===0&&window.fixture.nativeWrites===0),'card mount, model switch, hover and focus never save settings');
await page.locator('.shortcut-editor-open').click();check(await page.evaluate(()=>window.fixture.editorRequested===true),'top preview preserves existing editor entry');
check(errors.length===0,'actual shortcut card has no runtime exceptions');
fs.writeFileSync(out+'/ACCEPTANCE.json',JSON.stringify({checks,errors,metrics,sourceFiles:[...files].map(file=>({file,sha256:createHash('sha256').update(fs.readFileSync(root+'/'+file)).digest('hex')})),actualSettingsWritten:false,nativeInputInjected:false},null,2));console.log('PASS '+checks.length+' actual shortcut card placement/Steam Deck height checks');
}finally{await browser.close()}
