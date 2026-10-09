// Real Window.structuredClone regression; storage/native IPC are memory-only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
const root=process.cwd(),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_global-cache-config-browser.cjs'))('playwright');
const baseline=process.env.YMCC_SETTINGS_BASELINE_SOURCE;
const api=String.raw`
let document='',failWrite=false,failRead=false,stats=0,reads=0,commitHook=null;
export const seed=v=>{document=v};export const persisted=()=>JSON.parse(document);
export const setFailure=(read,write)=>{failRead=read;failWrite=write};export const counters=()=>({stats,reads});export const beforeCommit=f=>{commitHook=f};
function obj(v){return v!==null&&typeof v==='object'&&!Array.isArray(v)}
function delta(current,base,next){if(JSON.stringify(base)===JSON.stringify(next))return current;if(!obj(next)||(!obj(base)&&base!==null))return next;const result=obj(current)?current:{};if(obj(base))for(const k of Object.keys(base))if(!(k in next))delete result[k];for(const k of Object.keys(next))if(!obj(base)||!(k in base)||JSON.stringify(base[k])!==JSON.stringify(next[k]))result[k]=delta(result[k],obj(base)&&k in base?base[k]:null,next[k]);return result}
export const fs={exists:async p=>p.endsWith('yeman-settings.json'),readTextFile:async p=>{reads++;if(failRead)throw Error('fixture unreadable');if(p.endsWith('yeman-settings.json'))return document;throw Error('missing fixture')},rename:async()=>true};
export const settingsStore={read:async(p,known)=>{await Promise.resolve();stats++;if(failRead)throw Error('fixture unreadable');if(document&&known===document)return {stamp:document,unchanged:true};reads++;return {stamp:document,unchanged:false,content:document}},write:async(p,v,tx)=>{if(failWrite)return false;if(commitHook){const hook=commitHook;commitHook=null;hook()}const next=tx?delta(JSON.parse(document||'{}'),tx.baseline,JSON.parse(v)):JSON.parse(v);document=JSON.stringify(next);return tx?{ok:true,content:document}:true}};
`;

const entry=String.raw`
import {ref,reactive,readonly,shallowReactive,isProxy} from 'vue';
import * as repo from './src/bridge/settingsRepository';
import {seed,persisted,setFailure,counters,beforeCommit} from './src/bridge/api';
import * as ui from './src/bridge/uiSettings';
window.__settingsRegression=(async()=>{
 const cases=[];let activeCase='initial load';
 const require=(condition,message)=>{if(!condition)throw Error(message)};
 const detached=(value,seen=new Set())=>{if(value===null||typeof value!=='object'||seen.has(value))return;require(!isProxy(value),'Proxy retained');seen.add(value);Object.values(value).forEach(v=>detached(v,seen))};
 const pass=name=>{cases.push(name)};
 try{
  const initial=repo.normalizeSettings({tdp:{tdpMax:37,fpsLimit:75}});seed(JSON.stringify(initial));await repo.loadSettings();
  const identity=ref({manufacturer:'browser fixture',model:'device',nested:{keep:true}});
  activeCase='save reactive object, then switch schedule';
  await repo.saveSettingsSection('fan',{deviceIdentity:identity.value});
  await repo.saveSettingsSection('performanceSchedule',{active:{ac:'medium'}});
  let loaded=await repo.loadSettings();detached(loaded);window.structuredClone(loaded);require(loaded.performanceSchedule.active.ac==='medium','mode not saved');pass(activeCase);
  identity.value.model='late';loaded=await repo.loadSettings();require(loaded.fan.deviceIdentity.model==='device','caller mutated cache');pass('object ownership isolation');
  activeCase='save nested reactive array without poisoning cache';
  const apps=ref([{name:'before',args:['a']}]);const pending=repo.saveSettingsSection('quickApps',{apps:apps.value});apps.value[0].name='after';await pending;
  loaded=await repo.loadSettings();window.structuredClone(loaded);require(loaded.quickApps.apps[0].name==='before','queued patch not snapshotted');pass(activeCase);
  const mixed=shallowReactive({future:readonly(reactive({nodes:[reactive({value:1})]}))});const normalized=repo.normalizeSettings({extensions:mixed});detached(normalized);window.structuredClone(normalized);pass('mixed readonly/shallow/nested proxies');
  activeCase='cross-Window plain record with nested proxy';const iframe=document.createElement('iframe');document.body.append(iframe);const foreign=new iframe.contentWindow.Object();foreign.nested=[reactive({keep:true})];const crossRealm=repo.normalizeSettings({extensions:foreign});detached(crossRealm);window.structuredClone(crossRealm);require(crossRealm.extensions.nested[0].keep,'cross-realm proxy retained');iframe.remove();pass('cross-Window plain record with nested proxy');
  const a=repo.normalizeSettings({});a.gamepad.feedback.lighting.color='poison';require(repo.normalizeSettings({}).gamepad.feedback.lighting.color!=='poison','shared defaults aliased');pass('defaults remain independent');
  activeCase='replace reactive section';await repo.replaceSettingsSection('quickApps',reactive({apps:[],future:{keep:true}}));loaded=await repo.loadSettings();detached(loaded);window.structuredClone(loaded);pass(activeCase);
  activeCase='reactive input CAS';const current=reactive(repo.normalizeSettings({}).input);const decision=repo.evaluateInputCas(current,current.revision,reactive({gameOverride:{profileId:'fixture'}}));require(decision.ok,'CAS failed');detached(decision.value);window.structuredClone(decision.value);pass(activeCase);
  require(JSON.stringify(persisted().tdp)===JSON.stringify(initial.tdp),'TDP unexpectedly changed');pass('TDP unchanged throughout');
  repo.clearSettingsCache();loaded=await repo.loadSettings();detached(loaded);window.structuredClone(loaded);pass('reload remains cloneable');

  activeCase='cold coalesced readers own independent browser snapshots';repo.clearSettingsCache();const [one,two]=await Promise.all([repo.loadSettings(),repo.loadSettings()]);one.tdp.tdpMax=188;require(two.tdp.tdpMax===37,'shared cold snapshot');pass(activeCase);
  activeCase='native changes between read and commit are preserved and cached';beforeCommit(()=>{const disk=persisted();disk.background.external='new';disk.tdp.float.target=77;disk.fan.opaque='fan-sentinel';disk.input.opaque='input-sentinel';disk.future={keep:true};seed(JSON.stringify(disk))});await repo.saveSettingsSection('ui',{theme:'cyberpunk'});loaded=await repo.loadSettings();require(loaded.background.external==='new'&&loaded.tdp.float.target===77,'native change erased');require(loaded.fan.opaque==='fan-sentinel'&&loaded.input.opaque==='input-sentinel'&&loaded.future.keep,'opaque section erased');pass(activeCase);
  activeCase='metadata-only cache validation has no recurring JSON read';const count=counters().reads;for(let i=0;i<40;i++)await repo.loadSettings();require(counters().reads===count,'repeated JSON reads');pass(activeCase);
  activeCase='failed save does not update cache and queue can retry';setFailure(false,true);let rejected=false;try{await repo.saveSettingsSection('tdp',{tdpMax:200})}catch{rejected=true}require(rejected,'failed save reported success');require((await repo.loadSettings()).tdp.tdpMax===37,'failed save updated cache');setFailure(false,false);await repo.saveSettingsSection('tdp',{tdpMax:41});require((await repo.loadSettings()).tdp.tdpMax===41,'queue stuck');pass(activeCase);
  activeCase='Vue UI patch is captured before the module queue waits';await ui.loadUiSettings();const patch=reactive({backgroundOpacity:0.31});const queued=ui.setUiSettings(patch);patch.backgroundOpacity=0.72;await queued;require(persisted().ui.backgroundOpacity===0.31,'UI patch captured too late');pass(activeCase);
  activeCase='UI partial patch retains externally updated sibling';const disk=persisted();disk.ui.backgroundBlur=18;disk.ui.future={keep:true};seed(JSON.stringify(disk));await ui.setUiSettings({backgroundOpacity:0.33});require(persisted().ui.backgroundBlur===18&&persisted().ui.future.keep,'UI copied stale siblings');pass(activeCase);
  activeCase='UI read failure remains retryable without default overwrite';setFailure(true,false);repo.clearSettingsCache();let failed=false;try{await ui.loadUiSettings()}catch{failed=true}require(failed,'I/O failure converted to defaults');setFailure(false,false);await ui.loadUiSettings();require(ui.getUiSettings().theme==='cyberpunk','retry lost durable theme');pass(activeCase);
  activeCase='module queue generation cancels old-directory submission';const directoryPending=ui.setUiSettings({theme:'red-black'});repo.setSettingsDirectory('Q:/browser-config-next');const later=repo.normalizeSettings({ui:{theme:'blue-black'}});seed(JSON.stringify(later));let cancelled=false;try{await directoryPending}catch{cancelled=true}require(cancelled&&persisted().ui.theme==='blue-black','old queue redirected to new directory');pass(activeCase);
  return {ok:true,cases,userAgent:navigator.userAgent};
 }catch(error){return {ok:false,cases,activeCase,name:error.name,message:error.message,userAgent:navigator.userAgent}}
})();
`;
const result=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,format:'iife',platform:'browser',define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'memory-only-settings',setup(b){
 b.onLoad({filter:/api\.ts$/},()=>({contents:api,loader:'ts'}));
 if(baseline)b.onLoad({filter:/settingsRepository\.ts$/},()=>({contents:fs.readFileSync(baseline,'utf8'),loader:'ts',resolveDir:path.join(root,'src/bridge')}));
}}]});
const js=result.outputFiles[0].text;
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?js:'<!doctype html><title>Settings regression</title><body><script src="/fixture.js"></script></body>')});
await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;
try{
 browser=await chromium.launch({headless:true,channel:'msedge'});const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:'+server.address().port);const report=await page.evaluate(()=>window.__settingsRegression);
 if(baseline){assert.equal(report.ok,false);assert.equal(report.name,'DataCloneError');assert.match(report.message,/structuredClone/);console.log('BASELINE EXPECTED FAILURE:',report.name,report.message)}else{assert.equal(report.ok,true,JSON.stringify(report));assert.deepEqual(errors,[]);console.log('Browser global cache/config: '+report.cases.length+' cases PASS')}
 console.log(JSON.stringify(report,null,2));
 if(process.env.YMCC_SETTINGS_BROWSER_REPORT){const target=process.env.YMCC_SETTINGS_BROWSER_REPORT;fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,JSON.stringify({...report,expectedBaselineFailure:!!baseline,pageErrors:errors},null,2)+'\n')}
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}
