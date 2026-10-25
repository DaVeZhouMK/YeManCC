// Actual browser DOM + production API/IPC/repository boundary. Only native
// services are memory fixtures. No installed files, processes or devices used.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
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
const compiled=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,format:'iife',platform:'browser',define:{'process.env.NODE_ENV':'"production"'}});
const bootstrap=`window.chrome??={};let receiver;window.chrome.webview={addEventListener(t,f){if(t!=='message')throw Error('Unexpected event');receiver=f},postMessage(m){window.__nativeFixtureRequest(m).then(result=>receiver({data:{id:m.id,result}}),error=>receiver({data:{id:m.id,error:error.message}}))}};`;
const html='<!doctype html><meta charset="utf-8"><title>YMCC startup recovery test</title><div id="app"><button id="underlying">underlying</button><main id="page">loading</main></div><script>'+bootstrap+'</script><script src="/fixture.js"></script>';
const server=createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?compiled.outputFiles[0].text:html)});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const url='http://127.0.0.1:'+server.address().port;
const key=p=>String(p).replaceAll('/','\\').toLowerCase();
const owned=['ui','background','music','performanceSchedule','gameCustom','tdp','cpu','sleep','quickApps','power','tray','autoclose'];
const cases=[],pages=[],pageErrors=[];let browser;
const check=async(name,run)=>{await run();cases.push({name,ok:true});console.log('PASS '+name)};
async function fixture(seed={schemaVersion:99,ui:{theme:'kept'},input:{revision:48,marker:'protected'},fan:{marker:'protected'},future:{marker:'protected'}}){
 const fx={dir:'G:\\fixture-override\\PowerControl',files:new Map(),calls:[],confirm:false,confirmHook:null,archiveFailure:false,writes:[],archives:[],resets:0};fx.file=fx.dir+'\\yeman-settings.json';if(seed!==null)fx.files.set(key(fx.file),typeof seed==='string'?seed:JSON.stringify(seed));
 const page=await browser.newPage();pages.push(page);page.on('pageerror',e=>pageErrors.push(e.message));
 await page.exposeFunction('__nativeFixtureRequest',async m=>{fx.calls.push(m);const {cmd,args}=m;if(cmd==='settings.location')return {directory:fx.dir,file:fx.file,backup:fx.file+'.bak'};
 if(cmd==='dialog.confirm')return fx.confirmHook?await fx.confirmHook():fx.confirm;
 if(cmd==='fs.exists')return fx.files.has(key(args.path));
 if(cmd==='fs.readTextFile'){if(!fx.files.has(key(args.path)))throw Error('missing fixture');return fx.files.get(key(args.path))}
 if(cmd==='fs.rename'){const a=key(args.from),b=key(args.to);assert(fx.files.has(a));fx.files.set(b,fx.files.get(a));fx.files.delete(a);return true}
 if(!['settings.read','settings.write','settings.reset'].includes(cmd))throw Error('FORBIDDEN native request '+cmd);
 assert.equal(key(args.path),key(fx.file),'wrong root native I/O');
 if(cmd==='settings.read'){const raw=fx.files.get(key(fx.file));return {stamp:raw??'missing',unchanged:raw!==undefined&&raw===args.stamp,content:raw??''}}
 if(cmd==='settings.write'){fx.files.set(key(fx.file),args.content);fx.writes.push(args);return {ok:true,content:args.content}}
 fx.resets++;assert.equal(args.confirm,'reset-application-settings');assert.equal(args.defaults.schemaVersion,1);if(fx.archiveFailure)throw Error('Cannot back up settings; reset cancelled');
 // Storage mock only; native reset implementation is independently tested by
 // global_cache_config_native_selftest extracting the real C++ helper/IPC.
 let current={};for(const p of [fx.file,fx.file+'.bak']){try{const d=JSON.parse(fx.files.get(key(p)));if(d&&typeof d==='object'&&!Array.isArray(d)){current=d;break}}catch{}}
 const next=structuredClone(current);for(const name of owned)next[name]=structuredClone(args.defaults[name]);next.schemaVersion=1;next.tdp.autoApply={boot:false,wake:false};next.tdp.float={target:120,profile:'none',tdpStrategy:'none'};next.cpu.autoEnable={mode:'off'};next.cpu.autostart={ccd:{enabled:false},uv:{enabled:false}};next.performanceSchedule.enabled=false;
 const backups=[];for(const p of [fx.file,fx.file+'.bak'])if(fx.files.has(key(p))){const saved=p+'.before-user-reset-fixture-'+fx.resets;fx.files.set(key(saved),fx.files.get(key(p)));backups.push(saved);fx.archives.push(saved)}
 fx.files.set(key(fx.file),JSON.stringify(next));return {ok:true,backups,protectedDataRecovered:Object.keys(current).length>0};
 });
 await page.goto(url);await page.evaluate(()=>window.__boot);return {page,fx};
}
const recover=p=>p.getByRole('button',{name:'备份并重置应用配置',exact:true});
try{
 browser=await chromium.launch({headless:true,channel:'msedge'});
 await check('healthy override startup reaches schedule without wrong-root I/O or reset',async()=>{const f=await fixture(null);const normalized=await f.page.evaluate(()=>window.__repo.normalizeSettings({tdp:{tdpMax:37},input:{revision:48}}));f.fx.files.set(key(f.fx.file),JSON.stringify(normalized));f.fx.calls=[];f.fx.writes=[];await f.page.reload();assert.equal((await f.page.evaluate(()=>window.__boot)).ready,true);assert.equal(await f.page.locator('#page').textContent(),'schedule ready');assert.equal(f.fx.writes.length,0);assert.equal(f.fx.resets,0);assert.equal(f.fx.calls[0].cmd,'settings.location')});
 const f=await fixture(),original=f.fx.files.get(key(f.fx.file));
 await check('future config yields an actionable recovery surface outside inert app',async()=>{assert.equal((await f.page.evaluate(()=>window.__boot)).ready,false);assert.match(await f.page.locator('#yemancc-route-fallback').textContent(),/配置版本 99/);assert.equal(await f.page.locator('#app').evaluate(e=>e.inert),true);assert.equal(await f.page.locator('#yemancc-route-fallback').evaluate(e=>e.parentElement===document.body),true);assert.equal(await f.page.locator('#yemancc-route-fallback button').first().evaluate(e=>e===document.activeElement),true);await f.page.keyboard.press('Tab');assert.equal(await recover(f.page).evaluate(e=>e===document.activeElement),true);assert.equal(f.fx.writes.length,0);assert.equal(f.fx.files.get(key(f.fx.file)),original)});
 await check('cancelled user confirmation writes no files and buttons remain usable',async()=>{await recover(f.page).click();await f.page.getByRole('status').filter({hasText:'已取消'}).waitFor();assert.equal(f.fx.resets,0);assert.equal(f.fx.files.get(key(f.fx.file)),original);assert.equal(await recover(f.page).isEnabled(),true)});
 await check('native semantic confirm activates focused recovery control without hidden page input',async()=>{await recover(f.page).focus();await f.page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'confirm'}})));await f.page.waitForFunction(()=>document.querySelector('#yemancc-route-fallback').dataset.recoveryBusy==='false');assert.equal(f.fx.calls.filter(c=>c.cmd==='dialog.confirm').length,2);assert.equal(f.fx.resets,0)});
 await check('pending confirmation prevents duplicate actions or fallback replacement',async()=>{let release;f.fx.confirmHook=()=>new Promise(r=>{release=r});await recover(f.page).click();await f.page.waitForFunction(()=>document.querySelector('#yemancc-route-fallback').dataset.recoveryBusy==='true');assert.equal(await recover(f.page).isEnabled(),false);await f.page.evaluate(()=>{window.__show('late route','other',true);document.querySelector('[data-gp="startup-reset-settings"]').click()});assert.match(await f.page.locator('#yemancc-route-fallback').textContent(),/配置版本 99/);release(false);await f.page.waitForFunction(()=>document.querySelector('#yemancc-route-fallback').dataset.recoveryBusy==='false');f.fx.confirmHook=null;assert.equal(f.fx.resets,0)});
 await check('backup failure is visible, originals remain, recovery can be retried',async()=>{f.fx.confirm=true;f.fx.archiveFailure=true;await recover(f.page).click();await f.page.getByRole('status').filter({hasText:'恢复失败'}).waitFor();assert.equal(f.fx.files.get(key(f.fx.file)),original);assert.equal(f.fx.archives.length,0);assert.equal(await recover(f.page).isEnabled(),true);assert.equal(await f.page.getByRole('button',{name:'重新加载',exact:true}).isEnabled(),true)});
 await check('confirmed successful retry reloads to ready schedule after exact-byte archives',async()=>{f.fx.archiveFailure=false;const reload=f.page.waitForEvent('load');await recover(f.page).click();await reload;assert.equal((await f.page.evaluate(()=>window.__boot)).ready,true);assert.equal(await f.page.locator('#yemancc-route-fallback').count(),0);assert.equal(f.fx.files.get(key(f.fx.archives[0])),original);const saved=JSON.parse(f.fx.files.get(key(f.fx.file)));assert.equal(saved.input.revision,48);assert.equal(saved.fan.marker,'protected');assert.equal(saved.future.marker,'protected');assert.deepEqual(saved.tdp.autoApply,{boot:false,wake:false});assert.equal(saved.performanceSchedule.enabled,false);assert.equal(f.fx.resets,2)});
 await check('corrupt main without backup stays byte-identical and presents recovery',async()=>{const g=await fixture('{broken-main');assert.equal((await g.page.evaluate(()=>window.__boot)).ready,false);assert.match(await g.page.locator('#yemancc-route-fallback').textContent(),/共享配置损坏/);assert.equal(g.fx.files.get(key(g.fx.file)),'{broken-main');assert.equal(g.fx.writes.length,0);assert.equal(await recover(g.page).isEnabled(),true)});
 await check('reload button really navigates even when app is inert and does not reset',async()=>{const g=await fixture('{broken-main');const reload=g.page.waitForEvent('load');await g.page.getByRole('button',{name:'重新加载',exact:true}).click();await reload;assert.equal((await g.page.evaluate(()=>window.__boot)).ready,false);assert.equal(g.fx.resets,0);assert.equal(g.fx.files.get(key(g.fx.file)),'{broken-main')});
 await check('legacy schema remains compatible and preserves unrelated/protected branches',async()=>{const g=await fixture({schemaVersion:0,tdp:{tdpMax:37},input:{revision:48},future:{kept:true}});assert.equal((await g.page.evaluate(()=>window.__boot)).ready,true);const saved=JSON.parse(g.fx.files.get(key(g.fx.file)));assert.equal(saved.schemaVersion,1);assert.equal(saved.tdp.tdpMax,37);assert.equal(saved.input.revision,48);assert.equal(saved.future.kept,true);assert.equal(g.fx.resets,0)});
 await check('error text is safe and transient fallback restores previous inert state',async()=>{const g=await fixture(null);await g.page.evaluate(()=>window.__show('<img src=x onerror="window.__pwn=true">','schedule',false,'<script>evil</script>'));assert.equal(await g.page.locator('#yemancc-route-fallback img, #yemancc-route-fallback script').count(),0);await g.page.evaluate(()=>window.__clear());assert.equal(await g.page.locator('#app').evaluate(e=>e.inert),false);assert.equal(await g.page.locator('#yemancc-route-fallback').count(),0)});
 assert.deepEqual(pageErrors,[]);
 const report={passed:cases.length,failed:0,cases,pageErrors,actualApiAndIpc:true,actualRepositoryAndFallback:true,nativeServicesMemoryOnly:true,realSettingsWritten:false,actualProductLaunched:false,hardwareOperations:false};console.log(JSON.stringify(report,null,2));
 if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2)+'\n');
}finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}
