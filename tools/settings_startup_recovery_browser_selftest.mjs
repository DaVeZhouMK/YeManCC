// Actual browser DOM + production API/IPC/repository boundary. Only native
// services are memory fixtures. No installed files, processes or devices used.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const startedAt=Date.now(),sha256=data=>createHash('sha256').update(data).digest('hex');
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));
const {build}=require('esbuild');
const runtime='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_settings-startup-recovery.cjs'))('playwright');
const entry=`
import * as repo from './src/bridge/settingsRepository';
import {showRouteFallback,clearRouteFallback} from './src/robust/routeFallback';
import {StartupWorkBarrier} from './src/robust/startupReadiness';
window.__repo=repo;window.__show=showRouteFallback;window.__clear=clearRouteFallback;
window.__boot=(async()=>{const barrier=new StartupWorkBarrier();let error='';
try{await barrier.track(()=>repo.readSettingsSection('performanceSchedule'))}catch(e){error=e.message}
if(await barrier.settle()){document.querySelector('#page').textContent='schedule ready';return {ready:true}}
showRouteFallback('启动页面未能完成加载','schedule',true,error);return {ready:false,error};})();
`;
const compiled=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,metafile:true,format:'iife',platform:'browser',define:{'process.env.NODE_ENV':'"production"'}});
const inputIdentity=Object.keys(compiled.metafile.inputs).filter(p=>p!=='<stdin>').map(p=>({path:path.resolve(root,p),sha256:sha256(fs.readFileSync(path.resolve(root,p)))}));
inputIdentity.push({path:fileURLToPath(import.meta.url),sha256:sha256(fs.readFileSync(fileURLToPath(import.meta.url)))});
const toolIdentity=[process.execPath,require.resolve('esbuild')].map(p=>({path:p,sha256:sha256(fs.readFileSync(p))}));
const bootstrap=`window.chrome??={};let receiver;window.chrome.webview={addEventListener(t,f){if(t!=='message')throw Error('Unexpected event');receiver=f},postMessage(m){window.__nativeFixtureRequest(m).then(result=>receiver({data:{id:m.id,result}}),error=>receiver({data:{id:m.id,error:error.message}}))}};`;
const html='<!doctype html><meta charset="utf-8"><title>YMCC startup recovery test</title><div id="app"><button id="underlying">underlying</button><main id="page">loading</main></div><script>'+bootstrap+'</script><script src="/fixture.js"></script>';
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?compiled.outputFiles[0].text:html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const url='http://127.0.0.1:'+server.address().port;
const key=p=>String(p).replaceAll('/','\\').toLowerCase();
const owned=['ui','background','music','performanceSchedule','gameCustom','tdp','rtss','cpu','sleep','quickApps','power','tray','autoclose'];
const cases=[],pages=[],pageErrors=[];let browser;
const check=async(name,run)=>{await run();cases.push({name,ok:true});console.log('PASS '+name)};
function boundedDocument(value){const pending=[{value,depth:0}];while(pending.length){const current=pending.pop();if(current.depth>64)return false;if(current.value&&typeof current.value==='object')for(const child of Object.values(current.value))if(child&&typeof child==='object')pending.push({value:child,depth:current.depth+1});}return true;}
async function fixture(seed={schemaVersion:99,ui:{theme:'kept'},input:{revision:48,marker:'protected'},fan:{marker:'protected'},future:{marker:'protected'}},options={}){
 const fx={dir:'G:\\fixture-override\\PowerControl',files:new Map(),calls:[],confirm:false,confirmHook:null,archiveFailure:false,writes:[],archives:[],resets:0,...options};fx.file=fx.dir+'\\yeman-settings.json';if(seed!==null)fx.files.set(key(fx.file),typeof seed==='string'?seed:JSON.stringify(seed));
 if(options.backup!==undefined)fx.files.set(key(fx.file+'.bak'),typeof options.backup==='string'?options.backup:JSON.stringify(options.backup));
 const page=await browser.newPage();page.setDefaultTimeout(5000);pages.push(page);page.on('pageerror',e=>pageErrors.push(e.message));
 await page.exposeFunction('__nativeFixtureRequest',async m=>{fx.calls.push(m);const {cmd,args}=m;if(cmd==='settings.location'){if(fx.locationHook)return await fx.locationHook();return {directory:fx.dir,file:fx.file,backup:fx.file+'.bak'}};
 if(cmd==='dialog.confirm')return fx.confirmHook?await fx.confirmHook():fx.confirm;
 if(cmd==='fs.exists'){if(fx.existsHook)await fx.existsHook(args);return fx.files.has(key(args.path))}
 if(cmd==='fs.readTextFile'){if(fx.textReadHook)await fx.textReadHook(args);if(!fx.files.has(key(args.path)))throw Error('missing fixture');return fx.files.get(key(args.path))}
 if(cmd==='fs.rename'){const a=key(args.from),b=key(args.to);assert(fx.files.has(a));fx.files.set(b,fx.files.get(a));fx.files.delete(a);return true}
 if(!['settings.read','settings.write','settings.reset'].includes(cmd))throw Error('FORBIDDEN native request '+cmd);
 assert([key(fx.file),...(cmd==='settings.read'?[key(fx.file+'.bak')]:[])].includes(key(args.path)),'wrong root native I/O');
 if(cmd==='settings.read'){const main=key(args.path)===key(fx.file);if(!main&&fx.backupReadHook)return await fx.backupReadHook(args);if(main&&fx.readFault)return {stamp:'unreadable',unchanged:false,content:'',readFault:fx.readFault};if(main&&fx.readError)throw Error(fx.readError);if(main&&fx.readHook)return await fx.readHook(args);const raw=fx.files.get(key(args.path));if(typeof raw==='string'&&Buffer.byteLength(raw)>4*1024*1024)return {stamp:'too-large',unchanged:false,content:'',readFault:'content-too-large'};return {stamp:raw??'missing',unchanged:raw!==undefined&&raw===args.stamp,content:raw??''}}
 if(cmd==='settings.write'){
  if(fx.writeHook)return await fx.writeHook(args);
  if(fx.writeFailure)return false;
  const raw=fx.files.get(key(fx.file));let current;
  try{current=JSON.parse(raw?.replace(/^\uFEFF/,''));if(!boundedDocument(current))current=undefined;}catch{}
  if(current?.schemaVersion>1||current?.configurationBaseline?.revision>1)throw Error('native protects future document');
  if(args.expectedInputRevision!==undefined&&current?.input?.revision!==args.expectedInputRevision)return {ok:false,reason:'revision-conflict',content:JSON.stringify(current)};
  if(fx.readFault&&!['invalid-text-encoding','content-too-large','invalid-document'].includes(fx.readFault))throw Error('native strict read fault');
  const object=value=>value&&typeof value==='object'&&!Array.isArray(value);
  const backup=fx.files.get(key(fx.file+'.bak'));let parsed;try{parsed=JSON.parse(backup?.replace(/^\uFEFF/,''));if(!boundedDocument(parsed))parsed=undefined;}catch{}
  if(!object(current)&&(raw!==undefined||backup!==undefined)){
   if(object(parsed)&&(parsed.schemaVersion>1||parsed.configurationBaseline?.revision>1))throw Error('native protects future backup');
   if(!object(parsed)){
    if(args.initialize!==true||!object(args.baseline)||args.expectedInputRevision!==undefined)throw Error('native requires damaged initialization');
    const requested=JSON.parse(args.content);assert.equal(requested.input.applyStatus,'safe-stop');assert.equal(requested.startupDesired.virtualGamepadPersona,'disabled');
    assert.deepEqual(args.baseline,{});assert.deepEqual(requested.tdp.autoApply,{boot:false,wake:false});
   }
   if(fx.archiveFailure)throw Error('native preservation failed');
   for(const p of object(parsed)?[fx.file]:[fx.file,fx.file+'.bak'])if(fx.files.has(key(p))){
    const archived=p+'.before-auto-recovery-fixture-'+fx.writes.length;fx.files.set(key(archived),fx.files.get(key(p)));fx.archives.push(archived);
   }
  }
  if(fx.invalidReceipt)return {ok:true,content:'[]'};
  fx.files.set(key(fx.file),args.content);fx.writes.push(args);fx.readFault=null;return fx.booleanReceipt?true:{ok:true,content:args.content};
 }
 fx.resets++;assert.equal(args.confirm,'reset-application-settings');assert.equal(args.defaults.schemaVersion,1);if(fx.archiveFailure)throw Error('Cannot back up settings; reset cancelled');
 // Storage mock only; native reset implementation is independently tested by
 // global_cache_config_native_selftest extracting the real C++ helper/IPC.
 let current={};for(const p of [fx.file,fx.file+'.bak']){try{const d=JSON.parse(fx.files.get(key(p)));if(d&&typeof d==='object'&&!Array.isArray(d)){current=d;break}}catch{}}
 const next=structuredClone(current);for(const name of owned)next[name]=structuredClone(args.defaults[name]);next.schemaVersion=1;next.configurationBaseline=structuredClone(args.defaults.configurationBaseline);next.tdp.autoApply={boot:false,wake:false};next.tdp.float={target:120,profile:'none',tdpStrategy:'none'};next.cpu.autoEnable={mode:'off'};next.cpu.autostart={ccd:{enabled:false},uv:{enabled:false}};next.performanceSchedule.enabled=false;
 const backups=[];for(const p of [fx.file,fx.file+'.bak'])if(fx.files.has(key(p))){const saved=p+'.before-user-reset-fixture-'+fx.resets;fx.files.set(key(saved),fx.files.get(key(p)));backups.push(saved);fx.archives.push(saved)}
 fx.files.set(key(fx.file),JSON.stringify(next));return {ok:true,backups,protectedDataRecovered:Object.keys(current).length>0};
 });
 await page.goto(url);await page.evaluate(()=>window.__boot);return {page,fx};
}
const recover=p=>p.getByRole('button',{name:'备份并重置应用配置',exact:true});
try{
 browser=await chromium.launch({headless:true,channel:'msedge',args:['--explicitly-allowed-ports='+server.address().port]});
 await check('healthy override startup reaches schedule without wrong-root I/O or reset',async()=>{const f=await fixture(null);const normalized=await f.page.evaluate(()=>window.__repo.normalizeSettings({tdp:{tdpMax:37},input:{revision:48}}));f.fx.files.set(key(f.fx.file),JSON.stringify(normalized));f.fx.calls=[];f.fx.writes=[];await f.page.reload();assert.equal((await f.page.evaluate(()=>window.__boot)).ready,true);assert.equal(await f.page.locator('#page').textContent(),'schedule ready');assert.equal(f.fx.writes.length,0);assert.equal(f.fx.resets,0);assert.equal(f.fx.calls[0].cmd,'settings.location')});
 const f=await fixture(),original=f.fx.files.get(key(f.fx.file));
 // The old mandatory-startup-block assertion failed after runtime recovery was
 // implemented. Its original source/log are retained in this run's evidence.
 // Optional confirmed-reset UI still exercises all original ownership checks.
 await check('future config keeps app available; optional explicit recovery retains modal ownership',async()=>{assert.equal((await f.page.evaluate(()=>window.__boot)).ready,true);assert.equal(await f.page.locator('#yemancc-route-fallback').count(),0);assert.equal(await f.page.locator('#app').evaluate(e=>e.inert),false);const state=await f.page.evaluate(()=>window.__repo.getSettingsRecoveryState());assert.equal(state.reason,'unsupported');assert.equal(state.persisted,false);await f.page.evaluate(()=>window.__show('配置恢复','schedule',true,window.__repo.getSettingsRecoveryState().message));assert.match(await f.page.locator('#yemancc-route-fallback').textContent(),/配置版本 99/);assert.equal(await f.page.locator('#app').evaluate(e=>e.inert),true);assert.equal(await f.page.locator('#yemancc-route-fallback').evaluate(e=>e.parentElement===document.body),true);assert.equal(await f.page.locator('#yemancc-route-fallback button').first().evaluate(e=>e===document.activeElement),true);await f.page.keyboard.press('Tab');assert.equal(await recover(f.page).evaluate(e=>e===document.activeElement),true);assert.equal(f.fx.writes.length,0);assert.equal(f.fx.files.get(key(f.fx.file)),original)});
 await check('cancelled user confirmation writes no files and buttons remain usable',async()=>{await recover(f.page).click();await f.page.getByRole('status').filter({hasText:'已取消'}).waitFor();assert.equal(f.fx.resets,0);assert.equal(f.fx.files.get(key(f.fx.file)),original);assert.equal(await recover(f.page).isEnabled(),true)});
 await check('native semantic confirm activates focused recovery control without hidden page input',async()=>{await recover(f.page).focus();await f.page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'confirm'}})));await f.page.waitForFunction(()=>document.querySelector('#yemancc-route-fallback').dataset.recoveryBusy==='false');assert.equal(f.fx.calls.filter(c=>c.cmd==='dialog.confirm').length,2);assert.equal(f.fx.resets,0)});
 await check('pending confirmation prevents duplicate actions or fallback replacement',async()=>{let release;f.fx.confirmHook=()=>new Promise(r=>{release=r});await recover(f.page).click();await f.page.waitForFunction(()=>document.querySelector('#yemancc-route-fallback').dataset.recoveryBusy==='true');assert.equal(await recover(f.page).isEnabled(),false);await f.page.evaluate(()=>{window.__show('late route','other',true);document.querySelector('[data-gp="startup-reset-settings"]').click()});assert.match(await f.page.locator('#yemancc-route-fallback').textContent(),/配置版本 99/);release(false);await f.page.waitForFunction(()=>document.querySelector('#yemancc-route-fallback').dataset.recoveryBusy==='false');f.fx.confirmHook=null;assert.equal(f.fx.resets,0)});
 await check('backup failure is visible, originals remain, recovery can be retried',async()=>{f.fx.confirm=true;f.fx.archiveFailure=true;await recover(f.page).click();await f.page.getByRole('status').filter({hasText:'恢复失败'}).waitFor();assert.equal(f.fx.files.get(key(f.fx.file)),original);assert.equal(f.fx.archives.length,0);assert.equal(await recover(f.page).isEnabled(),true);assert.equal(await f.page.getByRole('button',{name:'重新加载',exact:true}).isEnabled(),true)});
 await check('confirmed successful retry reloads to ready schedule after exact-byte archives',async()=>{f.fx.archiveFailure=false;const reload=f.page.waitForEvent('load');await recover(f.page).click();await reload;assert.equal((await f.page.evaluate(()=>window.__boot)).ready,true);assert.equal(await f.page.locator('#yemancc-route-fallback').count(),0);assert.equal(f.fx.files.get(key(f.fx.archives[0])),original);const saved=JSON.parse(f.fx.files.get(key(f.fx.file)));assert.equal(saved.input.revision,48);assert.equal(saved.fan.marker,'protected');assert.equal(saved.future.marker,'protected');assert.deepEqual(saved.tdp.autoApply,{boot:false,wake:false});assert.equal(saved.performanceSchedule.enabled,false);assert.equal(f.fx.resets,2)});
 await check('corrupt main without backup initializes safe defaults after exact original archive',async()=>{const g=await fixture('{broken-main');assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);assert.equal(await g.page.locator('#yemancc-route-fallback').count(),0);const state=await g.page.evaluate(()=>window.__repo.getSettingsRecoveryState());assert.equal(state.status,'ready');assert.equal(state.persisted,true);assert.equal(g.fx.archives.length,1);assert.equal(g.fx.files.get(key(g.fx.archives[0])),'{broken-main');assert.equal(g.fx.writes.length,1);assert.equal(g.fx.writes[0].initialize,true);assert.equal(JSON.parse(g.fx.files.get(key(g.fx.file))).input.applyStatus,'safe-stop');await g.page.evaluate(()=>window.__show('配置恢复','schedule',true));assert.equal(await recover(g.page).isEnabled(),true)});
 await check('reload button really navigates during failed auto-preservation and does not reset',async()=>{const g=await fixture('{broken-main',{archiveFailure:true});await g.page.evaluate(()=>window.__show('配置恢复','schedule',true,window.__repo.getSettingsRecoveryState().message));const reload=g.page.waitForEvent('load');await g.page.getByRole('button',{name:'重新加载',exact:true}).click();await reload;assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);assert.equal(g.fx.resets,0);assert.equal(g.fx.files.get(key(g.fx.file)),'{broken-main')});
 await check('legacy schema remains compatible and preserves unrelated/protected branches',async()=>{const g=await fixture({schemaVersion:0,tdp:{tdpMax:37},input:{revision:48},future:{kept:true}});assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);const saved=JSON.parse(g.fx.files.get(key(g.fx.file)));assert.equal(saved.schemaVersion,1);assert.equal(saved.tdp.tdpMax,37);assert.equal(saved.input.revision,48);assert.equal(saved.future.kept,true);assert.equal(g.fx.resets,0)});
 await check('future baseline is inspectable and explicit archived reset establishes the current receipt',async()=>{
  const g=await fixture({schemaVersion:1,configurationBaseline:{id:'ymcc-settings',revision:2,legacyImported:true},input:{revision:48,marker:'protected'},fan:{marker:'protected'},future:{marker:'protected'}});
  assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);assert.match((await g.page.evaluate(()=>window.__repo.getSettingsRecoveryState())).message,/配置基准/);await g.page.evaluate(()=>window.__show('配置恢复','schedule',true,window.__repo.getSettingsRecoveryState().message));
  const original=g.fx.files.get(key(g.fx.file));g.fx.confirm=true;const loaded=g.page.waitForEvent('load');await recover(g.page).click();await loaded;
  assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);const saved=JSON.parse(g.fx.files.get(key(g.fx.file)));
  assert.equal(saved.configurationBaseline.revision,1);assert.equal(saved.input.revision,48);assert.equal(saved.fan.marker,'protected');assert.equal(saved.future.marker,'protected');
  assert(g.fx.archives.some(file=>g.fx.files.get(key(file))===original));assert.equal(g.fx.resets,1);
 });
 await check('error text is safe and transient fallback restores previous inert state',async()=>{const g=await fixture(null);await g.page.evaluate(()=>window.__show('<img src=x onerror="window.__pwn=true">','schedule',false,'<script>evil</script>'));assert.equal(await g.page.locator('#yemancc-route-fallback img, #yemancc-route-fallback script').count(),0);await g.page.evaluate(()=>window.__clear());assert.equal(await g.page.locator('#app').evaluate(e=>e.inert),false);assert.equal(await g.page.locator('#yemancc-route-fallback').count(),0)});

 const normalize=seed=>f.page.evaluate(seed=>window.__repo.normalizeSettings(seed),seed);
 const runtime=page=>page.evaluate(async()=>({document:await window.__repo.loadSettings(),state:window.__repo.getSettingsRecoveryState()}));
 const save=(page,value)=>page.evaluate(async value=>{try{await window.__repo.saveSettingsSection('ui',value);return {ok:true,state:window.__repo.getSettingsRecoveryState()}}catch(error){return {ok:false,error:error.message,state:window.__repo.getSettingsRecoveryState()}}},value);
 const assertSafe=document=>{
  assert.equal(document.startupDesired.virtualGamepadPersona,'disabled');assert.equal(document.startupDesired.gyroPreset,'off');
  assert.equal(document.input.outputTarget.persona,'disabled');assert.equal(document.input.gyroMotion.enabled,false);
  assert.equal(document.input.appliedRevision,null);assert.equal(document.input.hostAcknowledgedRevision,null);
  assert.equal(document.input.pendingConfigHash,null);assert.equal(document.input.applyStatus,'safe-stop');
  assert.equal(document.performanceSchedule.enabled,false);assert.deepEqual(document.tdp.autoApply,{boot:false,wake:false});
  assert.equal(document.cpu.autostart.ccd.enabled,false);assert.equal(document.cpu.autostart.uv.enabled,false);
  assert.equal(document.fan.featureEnabled,false);
 };
 const legal=await normalize({ui:{theme:'kept',backgroundOpacity:0.35},tdp:{tdpMax:37},input:{revision:48},future:{nested:[1,{keep:true}]}});
 await check('valid BOM JSON and unknown fields survive with exact original bytes and no writes',async()=>{
  const raw='\uFEFF'+JSON.stringify(legal);const g=await fixture(raw);const r=await runtime(g.page);
  assert.equal(r.state.status,'ready');assert.equal(r.document.tdp.tdpMax,37);assert.deepEqual(r.document.future,legal.future);
  assert.equal(g.fx.files.get(key(g.fx.file)),raw);assert.equal(g.fx.writes.length,0);await g.page.close();
 });
 // Native initializeDamaged now archives originals before rebuilding. The
 // pre-change preservation assertion FAIL is retained in this run's evidence.
 await check('main/backup malformed storm archives exact originals then persists safe defaults once',async()=>{
  const pairs=[...['', '[]', 'null', 'false', '0', '"text"', '{"ui":', '{"tdp":{"tdpMax":NaN}}'].map(raw=>[raw,'{broken-backup']),['{broken-main',''],['','']];
  for(const [raw,backup] of pairs){
   const g=await fixture(raw,{backup});assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);
   const r=await runtime(g.page);assertSafe(r.document);assert.equal(r.state.status,'ready');assert.equal(r.state.persisted,true);
   assert.equal(g.fx.archives.length,2);assert.equal(g.fx.files.get(key(g.fx.archives[0])),raw);assert.equal(g.fx.files.get(key(g.fx.archives[1])),backup);
   assert.equal(g.fx.writes.length,1);assert.equal(g.fx.writes[0].initialize,true);assert.deepEqual(g.fx.writes[0].baseline,{});
   assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.write'&&c.args.initialize).length,1);assert.equal(g.fx.resets,0);
   assert.equal(g.fx.calls.some(c=>['fs.rename','fs.remove'].includes(c.cmd)),false);
   assert.equal((await save(g.page,{theme:'saved-after-initialize'})).ok,true);assert.equal(JSON.parse(g.fx.files.get(key(g.fx.file))).ui.theme,'saved-after-initialize');await g.page.close();
  }
 });
 await check('future main/backup and baseline keep originals and reject ordinary saves and input CAS',async()=>{
  const future={...legal,schemaVersion:99};const futureBaseline={...legal,configurationBaseline:{id:'ymcc-settings',revision:2,legacyImported:true}};
  for(const [main,backup] of [[future,legal],[futureBaseline,legal],['{bad',future],['{bad',futureBaseline]]){
   const g=await fixture(main,{backup});const originals=[g.fx.files.get(key(g.fx.file)),g.fx.files.get(key(g.fx.file+'.bak'))];
   const r=await runtime(g.page);assertSafe(r.document);assert.equal(r.state.reason,'unsupported');assert.equal(r.state.persisted,false);
   assert.equal((await save(g.page,{theme:'blocked'})).ok,false);
   const cas=await g.page.evaluate(()=>window.__repo.compareAndSwapInputSettings(0,{outputTarget:{persona:'elite'}}));
   assert.equal(cas.ok,false);assert.equal(cas.reason,'write-failed');assert.equal(cas.value.appliedRevision,null);
   assert.deepEqual([g.fx.files.get(key(g.fx.file)),g.fx.files.get(key(g.fx.file+'.bak'))],originals);
   assert.equal(g.fx.writes.length,0);assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.write').length,0);await g.page.close();
  }
 });
 await check('untyped oversized read receipt fails closed to runtime defaults and cannot save over unknown bytes',async()=>{
  const raw=' '.repeat(4*1024*1024+1);const g=await fixture(raw,{readHook:async()=>({stamp:'untyped-too-large',unchanged:false,content:raw})});const r=await runtime(g.page);
  assertSafe(r.document);assert.equal(r.state.reason,'read-failed');assert.equal((await save(g.page,{theme:'blocked'})).ok,false);
  assert.equal(g.fx.files.get(key(g.fx.file)),raw);assert.equal(g.fx.writes.length,0);await g.page.close();
 });
 await check('known wrong fields repair to defaults while valid settings and unknown fields persist',async()=>{
  const bad=structuredClone(legal);bad.ui.theme=[];bad.ui.backgroundOpacity='0.2';bad.gamepad.enabled='false';bad.music.volume=null;
  bad.input.revision='48';bad.input.appliedRevision='48';bad.input.hostAcknowledgedRevision=true;bad.input.gyroMotion.gyroMultiplier='3';
  const g=await fixture(bad);const r=await runtime(g.page);const defaults=await normalize({});
  assert.equal(r.state.status,'ready');assert.equal(r.document.ui.theme,defaults.ui.theme);assert.equal(r.document.music.volume,defaults.music.volume);
  assert.equal(r.document.gamepad.enabled,defaults.gamepad.enabled);assert.equal(r.document.input.revision,0);
  assert.equal(r.document.input.appliedRevision,null);assert.equal(r.document.input.hostAcknowledgedRevision,null);
  assert.equal(r.document.input.gyroMotion.gyroMultiplier,defaults.input.gyroMotion.gyroMultiplier);
  assert.equal(r.document.tdp.tdpMax,37);assert.deepEqual(r.document.future,legal.future);assert.equal(g.fx.writes.length,1);
  assert.equal((await save(g.page,{theme:'after-repair'})).ok,true);assert.equal(JSON.parse(g.fx.files.get(key(g.fx.file))).ui.theme,'after-repair');await g.page.close();
 });
 await check('compatible backup is restored through native write; frontend never moves the corrupt main',async()=>{
  const g=await fixture('{corrupt-main',{backup:legal});assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);
  const r=await runtime(g.page);assert.equal(r.state.status,'ready');assert.equal(r.document.input.revision,48);assert.deepEqual(r.document.future,legal.future);
  assert.equal(g.fx.writes.length,1);assert.equal(g.fx.archives.length,1);assert.equal(g.fx.files.get(key(g.fx.archives[0])),'{corrupt-main');
  assert.equal(g.fx.files.get(key(g.fx.file+'.bak')),JSON.stringify(legal));assert.equal(g.fx.calls.some(c=>c.cmd==='fs.rename'),false);await g.page.close();
 });
 await check('backup preservation failure leaves both originals and runtime remains usable until retry',async()=>{
  const g=await fixture('{corrupt-main',{backup:legal,archiveFailure:true});const r=await runtime(g.page);
  assertSafe(r.document);assert.equal(r.document.tdp.tdpMax,37);assert.deepEqual(r.document.future,legal.future);
  assert.equal(r.state.reason,'write-failed');assert.equal(r.state.persisted,false);assert.equal(g.fx.writes.length,0);
  assert.equal(g.fx.files.get(key(g.fx.file)),'{corrupt-main');assert.equal(g.fx.files.get(key(g.fx.file+'.bak')),JSON.stringify(legal));
  g.fx.archiveFailure=false;assert.equal((await runtime(g.page)).state.status,'ready');assert.equal((await save(g.page,{theme:'recovered'})).ok,true);await g.page.close();
 });
 await check('typed native IO readFault never becomes damaged initialization; later healthy read can recover',async()=>{
  for(const fault of ['metadata-read-failed','content-read-failed']){
   const g=await fixture('{main-broken',{backup:'{backup-broken',readFault:fault});const r=await runtime(g.page);
   assertSafe(r.document);assert.equal(r.state.reason,'read-failed');assert.equal(r.state.persisted,false);assert.match(r.state.message,new RegExp(fault));
   assert.equal((await save(g.page,{theme:'blocked'})).ok,false);assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.write').length,0);
   assert.equal(g.fx.files.get(key(g.fx.file)),'{main-broken');assert.equal(g.fx.files.get(key(g.fx.file+'.bak')),'{backup-broken');
   g.fx.readFault=null;assert.equal((await runtime(g.page)).state.status,'ready');assert.equal(g.fx.archives.length,2);await g.page.close();
  }
 });
 await check('typed oversized or invalid-encoding documents preserve originals and self-heal through native recovery',async()=>{
  for(const fault of ['content-too-large','invalid-text-encoding','invalid-document']){
   const g=await fixture('{invalid-source',{backup:'{invalid-backup',readFault:fault});const r=await runtime(g.page);
   assert.equal(r.state.status,'ready');assert.equal(r.state.persisted,true);assertSafe(r.document);assert.equal(g.fx.archives.length,2);
   assert.equal(g.fx.files.get(key(g.fx.archives[0])),'{invalid-source');assert.equal(g.fx.files.get(key(g.fx.archives[1])),'{invalid-backup');
   assert.equal((await save(g.page,{theme:'after-typed-recovery'})).ok,true);await g.page.close();
  }
 });
 await check('deep configuration trees are preserved as invalid documents before safe reconstruction',async()=>{
  const deep='{"schemaVersion":1,"deep":'+ '{"next":'.repeat(2000)+'0'+'}'.repeat(2000)+'}';
  const g=await fixture(deep);const r=await runtime(g.page);assert.equal(r.state.status,'ready');assertSafe(r.document);assert.equal(g.fx.archives.length,1);assert.equal(g.fx.files.get(key(g.fx.archives[0])),deep);assert.equal((await save(g.page,{theme:'depth-healed'})).ok,true);await g.page.close();
 });
 await check('both over-limit sources recover safely while a compatible backup retains legitimate settings',async()=>{
  const oversized='x'.repeat(4*1024*1024+1);
  const both=await fixture(oversized,{backup:oversized});const r=await runtime(both.page);assert.equal(r.state.status,'ready');assertSafe(r.document);assert.equal(both.fx.archives.length,2);assert.equal(both.fx.files.get(key(both.fx.archives[0])),oversized);await both.page.close();
  const legal=await normalize({});legal.ui.theme='backup-preserved';legal.future={keep:true};
  const good=await fixture(oversized,{backup:legal});const recovered=await runtime(good.page);assert.equal(recovered.state.status,'ready');assert.equal(recovered.document.ui.theme,'backup-preserved');assert.deepEqual(recovered.document.future,{keep:true});assert.equal(good.fx.archives.length,1);await good.page.close();
 });
 await check('both-corrupt archive failure keeps runtime defaults; explicit retry persists before CAS revision can advance',async()=>{
  const g=await fixture('{main-broken',{backup:'{backup-broken',archiveFailure:true});const r=await runtime(g.page);
  assertSafe(r.document);assert.equal(r.state.reason,'corrupt');assert.equal(r.state.persisted,false);assert.equal(g.fx.writes.length,0);
  const rejected=await g.page.evaluate(()=>window.__repo.compareAndSwapInputSettings(0,{ownership:{externalControllerPriority:true}}));
  assert.equal(rejected.ok,false);assert.equal(rejected.reason,'write-failed');assert.equal(rejected.value.appliedRevision,null);assert.equal(g.fx.archives.length,0);
  assert.equal(g.fx.files.get(key(g.fx.file)),'{main-broken');assert.equal(g.fx.files.get(key(g.fx.file+'.bak')),'{backup-broken');
  g.fx.archiveFailure=false;assert.equal((await save(g.page,{theme:'retry-after-archive-failure'})).ok,true);assert.equal(g.fx.archives.length,2);
  const before=JSON.parse(g.fx.files.get(key(g.fx.file)));assert.equal(before.input.revision,0);assert.equal(before.input.appliedRevision,null);
  const accepted=await g.page.evaluate(()=>window.__repo.compareAndSwapInputSettings(0,{ownership:{externalControllerPriority:true}}));
  assert.equal(accepted.ok,true);assert.equal(accepted.revision,1);assert.equal(accepted.value.applyStatus,'saved-pending');assert.equal(accepted.value.appliedRevision,null);await g.page.close();
 });
 await check('cold read fault and native invalid-encoding fault keep page usable; next explicit read heals',async()=>{
  const g=await fixture(legal,{readError:'settings.read invalid UTF-8 fault'});assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);
  const r=await runtime(g.page);assertSafe(r.document);assert.equal(r.state.reason,'read-failed');assert.equal(r.state.persisted,false);
  assert.equal((await save(g.page,{theme:'blocked'})).ok,false);assert.equal(g.fx.writes.length,0);
  g.fx.readError=null;const healed=await runtime(g.page);assert.equal(healed.state.status,'ready');assert.equal(healed.document.input.revision,48);
  assert.equal((await save(g.page,{theme:'read-healed'})).ok,true);await g.page.close();
 });
 await check('a failed backup read stays a fault even if the file disappears before failure returns',async()=>{
  const g=await fixture(legal);g.fx.files.set(key(g.fx.file),'{broken-main');g.fx.files.set(key(g.fx.file+'.bak'),JSON.stringify(legal));
  await g.page.evaluate(()=>window.__repo.clearSettingsCache());g.fx.calls=[];
  g.fx.backupReadHook=async()=>{g.fx.files.delete(key(g.fx.file+'.bak'));throw Error('backup access denied while file changed')};
  const r=await runtime(g.page);assertSafe(r.document);assert.equal(r.state.reason,'read-failed');assert.equal(r.state.persisted,false);
  assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.write').length,0);assert.equal(g.fx.files.get(key(g.fx.file)),'{broken-main');await g.page.close();
 });
 await check('warm read rejection retains the latest trusted snapshot and does not fabricate confirmations',async()=>{
  const g=await fixture(legal);g.fx.readError='access denied';const r=await runtime(g.page);
  assert.equal(r.state.reason,'read-failed');assert.equal(r.state.source,'cache');assert.equal(r.state.persisted,true);
  assert.equal(r.document.input.revision,48);assert.equal(r.document.input.appliedRevision,null);assert.equal(r.document.tdp.tdpMax,37);
  assert.deepEqual(r.document.future,legal.future);assert.equal((await save(g.page,{theme:'blocked'})).ok,false);
  g.fx.readError=null;assert.equal((await save(g.page,{theme:'permission-returned'})).ok,true);await g.page.close();
 });
 await check('location rejection cannot write the default installation root and discovery can retry',async()=>{
  const g=await fixture(legal,{locationHook:()=>{throw Error('location denied')}});assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);
  const r=await runtime(g.page);assertSafe(r.document);assert.equal(r.state.reason,'read-failed');assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.read').length,0);
  assert.equal((await save(g.page,{theme:'blocked'})).ok,false);assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.write').length,0);
  g.fx.locationHook=null;assert.equal((await save(g.page,{theme:'location-returned'})).ok,true);await g.page.close();
 });
 await check('128 simultaneous loads coalesce one read and return independent snapshots',async()=>{
  const g=await fixture(legal);g.fx.calls=[];const r=await g.page.evaluate(async()=>{
   const documents=await Promise.all(Array.from({length:128},()=>window.__repo.loadSettings()));documents[0].ui.theme='caller-only';
   documents[0].future.nested[1].keep=false;return {others:documents.slice(1).every(d=>d.ui.theme==='kept'&&d.future.nested[1].keep),state:window.__repo.getSettingsRecoveryState()};
  });assert.equal(r.others,true);assert.equal(r.state.status,'ready');assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.read').length,1);assert.equal(g.fx.writes.length,0);await g.page.close();
 });
 await check('read timeout is bounded with no retry loop; later explicit read wins over late response',async()=>{
  const g=await fixture(legal);let release;g.fx.readHook=()=>new Promise(r=>{release=r});g.fx.calls=[];
  const started=Date.now();const r=await runtime(g.page);assert(Date.now()-started<5000);assert.equal(r.state.reason,'read-failed');assert.equal(r.state.source,'cache');
  assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.read').length,1);g.fx.readHook=null;
  const newer=structuredClone(legal);newer.ui.theme='newer';g.fx.files.set(key(g.fx.file),JSON.stringify(newer));
  assert.equal((await runtime(g.page)).document.ui.theme,'newer');release({stamp:'old',unchanged:false,content:'{old-broken'});
  await g.page.evaluate(()=>new Promise(r=>setTimeout(r,20)));assert.equal((await runtime(g.page)).document.ui.theme,'newer');assert.equal(g.fx.writes.length,0);await g.page.close();
 });
 await check('old settings generation cannot publish a late read or hide cancellation behind defaults',async()=>{
  const g=await fixture(legal);let release;g.fx.readHook=()=>new Promise(r=>{release=r});
  await g.page.evaluate(()=>{window.__oldRead=window.__repo.loadSettings().then(()=>({ok:true}),error=>({ok:false,error:error.message}))});
  await g.page.waitForFunction(()=>true);assert.equal(typeof release,'function');await g.page.evaluate(()=>window.__repo.clearSettingsCache());
  release({stamp:'old',unchanged:false,content:JSON.stringify(legal)});const result=await g.page.evaluate(()=>window.__oldRead);
  assert.equal(result.ok,false);assert.match(result.error,/配置缓存已失效|目录已切换/);assert.equal(g.fx.writes.length,0);
  g.fx.readHook=null;assert.equal((await runtime(g.page)).document.input.revision,48);await g.page.close();
 });
 await check('repair write refusal exposes safe runtime state and a later save can succeed',async()=>{
  const bad=structuredClone(legal);bad.music.volume='bad';const original=JSON.stringify(bad);const g=await fixture(bad,{writeFailure:true});
  const r=await runtime(g.page);assertSafe(r.document);assert.equal(r.state.reason,'write-failed');assert.equal(r.state.persisted,false);assert.deepEqual(r.document.future,legal.future);
  assert.equal(g.fx.files.get(key(g.fx.file)),original);assert.equal((await save(g.page,{theme:'not-saved'})).ok,false);assert.equal(g.fx.writes.length,0);
  g.fx.writeFailure=false;assert.equal((await save(g.page,{theme:'after-write-recovery'})).ok,true);
  const saved=JSON.parse(g.fx.files.get(key(g.fx.file)));assert.equal(saved.ui.theme,'after-write-recovery');assert.equal(saved.music.volume,0.11);assert.deepEqual(saved.future,legal.future);await g.page.close();
 });
 await check('failed ordinary save and CAS leave disk/revision unchanged, queue accepts a later save',async()=>{
  const g=await fixture(legal);g.fx.writeFailure=true;assert.equal((await save(g.page,{theme:'failed'})).ok,false);
  const cas=await g.page.evaluate(()=>window.__repo.compareAndSwapInputSettings(48,{ownership:{externalControllerPriority:true}}));
  assert.equal(cas.ok,false);assert.equal(cas.reason,'write-failed');assert.equal(cas.currentRevision,48);assert.equal(cas.value.revision,48);
  assert.equal(g.fx.files.get(key(g.fx.file)),JSON.stringify(legal));g.fx.writeFailure=false;assert.equal((await save(g.page,{theme:'retry-saved'})).ok,true);
  const committed=await g.page.evaluate(()=>window.__repo.compareAndSwapInputSettings(48,{ownership:{externalControllerPriority:true}}));
  assert.equal(committed.ok,true);assert.equal(committed.revision,49);assert.equal(committed.value.applyStatus,'saved-pending');assert.equal(committed.value.appliedRevision,null);await g.page.close();
 });
 await check('native CAS conflict reports the actually committed newer revision',async()=>{
  const g=await fixture(legal);const newer=structuredClone(legal);newer.input.revision=49;newer.input.ownership.externalControllerPriority=true;
  g.fx.writeHook=args=>{assert.equal(args.expectedInputRevision,48);g.fx.files.set(key(g.fx.file),JSON.stringify(newer));return {ok:false,reason:'revision-conflict',content:JSON.stringify(newer)}};
  const cas=await g.page.evaluate(()=>window.__repo.compareAndSwapInputSettings(48,{diagnostics:{inputLoggingEnabled:true}}));
  assert.equal(cas.ok,false);assert.equal(cas.reason,'revision-conflict');assert.equal(cas.currentRevision,49);assert.equal(cas.value.ownership.externalControllerPriority,true);assert.equal(g.fx.writes.length,0);await g.page.close();
 });
 await check('malformed successful write receipt cannot claim saved, and retry remains available',async()=>{
  const g=await fixture(legal);g.fx.invalidReceipt=true;const result=await save(g.page,{theme:'false-success'});
  assert.equal(result.ok,false);assert.equal(result.state.reason,'write-failed');assert.equal(g.fx.files.get(key(g.fx.file)),JSON.stringify(legal));
  g.fx.invalidReceipt=false;g.fx.booleanReceipt=true;assert.equal((await save(g.page,{theme:'verified-legacy-receipt'})).ok,true);await g.page.close();
 });
 await check('write timeout does not wedge write queue or let late receipt replace a newer save',async()=>{
  const g=await fixture(legal);let release;g.fx.writeHook=()=>new Promise(r=>{release=r});const started=Date.now();
  const result=await save(g.page,{theme:'timed-out'});assert.equal(result.ok,false);assert.match(result.error,/超时/);assert(Date.now()-started<5000);
  assert.equal(g.fx.files.get(key(g.fx.file)),JSON.stringify(legal));g.fx.writeHook=null;assert.equal((await save(g.page,{theme:'after-timeout'})).ok,true);
  release(false);await g.page.evaluate(()=>new Promise(r=>setTimeout(r,20)));assert.equal((await runtime(g.page)).document.ui.theme,'after-timeout');await g.page.close();
 });

 await check('successful receipt containing an unchanged old document cannot claim the requested save',async()=>{
  const g=await fixture(legal);g.fx.writeHook=()=>({ok:true,content:JSON.stringify(legal)});
  const result=await save(g.page,{theme:'unconfirmed-change'});assert.equal(result.ok,false);assert.match(result.error,/未确认/);
  assert.equal(g.fx.files.get(key(g.fx.file)),JSON.stringify(legal));g.fx.writeHook=null;assert.equal((await save(g.page,{theme:'confirmed-retry'})).ok,true);await g.page.close();
 });
 await check('total startup deadline stops legacy reads and cannot publish or write a late migration',async()=>{
  let delayedReads=0;const g=await fixture(null,{existsHook:async()=>{delayedReads++;await new Promise(r=>setTimeout(r,1300))}});
  assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);const state=await g.page.evaluate(()=>window.__repo.getSettingsRecoveryState());
  assert.equal(state.status,'runtime-fallback');assert.equal(state.reason,'read-failed');assert.match(state.message,/加载.*超时/);
  assert.equal(g.fx.writes.length,0);const afterDeadline=delayedReads;
  await new Promise(r=>setTimeout(r,1500));assert.equal(delayedReads,afterDeadline);assert.equal(g.fx.writes.length,0);
  g.fx.existsHook=null;assert.equal((await save(g.page,{theme:'after-total-deadline'})).ok,true);await g.page.close();
 });
 await check('committed content with post-commit refreshWarnings still confirms save without replay or UI expansion',async()=>{
  const g=await fixture(legal);const warnings=['sleep','fxsound','input-refresh','input-shortcuts','gesture','stale-diagnostic'];g.fx.calls=[];
  g.fx.writeHook=args=>{g.fx.files.set(key(g.fx.file),args.content);g.fx.writes.push(args);return {ok:true,content:args.content,refreshWarnings:warnings}};
  const saved=await save(g.page,{theme:'committed-with-refresh-warnings'});assert.equal(saved.ok,true);assert.equal(saved.state.persisted,true);
  assert.equal((await runtime(g.page)).document.ui.theme,'committed-with-refresh-warnings');assert.equal(g.fx.calls.filter(c=>c.cmd==='settings.write').length,1);
  assert.equal(await g.page.locator('#yemancc-route-fallback').count(),0);await g.page.close();
 });
 await check('corrupt files become saveable after external/native repair without clearing runtime cache',async()=>{
  const g=await fixture('{main-broken',{backup:'{backup-broken',archiveFailure:true});assertSafe((await runtime(g.page)).document);
  g.fx.archiveFailure=false;g.fx.files.set(key(g.fx.file),JSON.stringify(legal));assert.equal((await save(g.page,{theme:'after-native-repair'})).ok,true);
  assert.equal((await runtime(g.page)).state.status,'ready');assert.equal(g.fx.files.get(key(g.fx.file+'.bak')),'{backup-broken');await g.page.close();
 });
 assert.deepEqual(pageErrors,[]);
 for(const input of inputIdentity)assert.equal(sha256(fs.readFileSync(input.path)),input.sha256,'source drift during browser run: '+input.path);
 const report={startedAt:new Date(startedAt).toISOString(),durationMs:Date.now()-startedAt,inputIdentity,toolIdentity,bundleSha256:sha256(compiled.outputFiles[0].text),browserVersion:browser.version(),legacyStartupBlockAssertionsUpdated:true,legacyFailureRetained:true,passed:cases.length,failed:0,cases,pageErrors,actualApiAndIpc:true,actualRepositoryAndFallback:true,nativeServicesMemoryOnly:true,realSettingsWritten:false,actualProductLaunched:false,hardwareOperations:false};console.log(JSON.stringify(report,null,2));
 if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2)+'\n');
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}
