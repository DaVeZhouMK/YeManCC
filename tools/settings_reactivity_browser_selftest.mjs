// Real Window.structuredClone regression; storage/native IPC are memory-only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
const root=process.cwd(),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_settings-reactivity-browser.cjs'))('playwright');
const baseline=process.env.YMCC_SETTINGS_BASELINE_SOURCE;
const api=String.raw`let document='';export const seed=v=>{document=v};export const persisted=()=>JSON.parse(document);export const fs={exists:async p=>p.endsWith('yeman-settings.json'),readTextFile:async p=>{if(p.endsWith('yeman-settings.json'))return document;throw Error('missing fixture')},rename:async()=>true};export const settingsStore={write:async(p,v)=>{document=v;return true}};`;
const entry=String.raw`
import {ref,reactive,readonly,shallowReactive,isProxy} from 'vue';
import * as repo from './src/bridge/settingsRepository';
import {seed,persisted} from './src/bridge/api';
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
 if(baseline){assert.equal(report.ok,false);assert.equal(report.name,'DataCloneError');assert.match(report.message,/structuredClone/);console.log('BASELINE EXPECTED FAILURE:',report.name,report.message)}else{assert.equal(report.ok,true,JSON.stringify(report));assert.deepEqual(errors,[]);console.log('Browser settings reactivity: '+report.cases.length+' cases PASS')}
 console.log(JSON.stringify(report,null,2));
 if(process.env.YMCC_SETTINGS_BROWSER_REPORT){const target=process.env.YMCC_SETTINGS_BROWSER_REPORT;fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,JSON.stringify({...report,expectedBaselineFailure:!!baseline,pageErrors:errors},null,2)+'\n')}
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}
