import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const req=createRequire(root+'/package.json');const runtime='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(runtime+'/_uv-mainline.cjs')('playwright');
let source=fs.readFileSync(root+'/tools/lib/continuousPerformancePreview.mjs','utf8').replace("const root=path.resolve(import.meta.dirname,'../..')",'const root='+JSON.stringify(root));
source=source.replace("const styles=[];",`mocks.ipc="export * from './tools/fixtures/undervolt_trial/browserPorts';";
mocks.cpuctl="export {probeUndervolt,setUndervolt} from './tools/fixtures/undervolt_trial/browserPorts';";
mocks.previewRouter="export const useRouter=()=>({push:()=>window.__uvShowSettings(true)});";
const styles=[];`);
source=source.replace('const built=await build(',`entry+="\\nimport UvCard from './src/components/UndervoltCard.vue';import {startUndervoltRuntime,undervoltPresentationAllowed,undervoltState} from './src/bridge/undervoltRuntime';";
const start=entry.indexOf("const dedicated=params.get('dedicated')==='1';const app="),end=entry.indexOf("app.mount('#app');",start);
assert(start>=0&&end>start);entry=entry.slice(0,start)+"const settingsShown=ref(!params.has('performance'));const presentation=ref(!params.has('hidden'));window.__uvShowSettings=value=>settingsShown.value=value;window.__uvSetPresented=value=>presentation.value=value;window.__uvStartup=startUndervoltRuntime();window.__uvState=undervoltState;const app=createApp({render:()=>h('div',[h('div',{style:{display:'flex',gap:'6px',marginBottom:'8px'}},[h('button',{id:'test-performance',onClick:()=>settingsShown.value=false},'性能调度'),h('button',{id:'test-settings',onClick:()=>settingsShown.value=true},'设置')]),settingsShown.value?h(UvCard):h(Page)])});app.provide(undervoltPresentationAllowed,presentation);"+entry.slice(end);
const built=await build(`);
source=source.replace('builder.onResolve({filter:/.*/},args=>{',"builder.onResolve({filter:/.*/},args=>{const port=args.path.split('/').pop().replace(/\\.ts$/,'');if(args.importer.endsWith('undervoltRuntime.ts')&&(port==='ipc'||port==='cpuctl'))return {path:port,namespace:'fixture'};if(args.path==='vue-router')return {path:'previewRouter',namespace:'fixture'};");
fs.writeFileSync(out+'/build-browser.mjs',source,{flag:'wx'});const {createPerformancePreviewServer}=await import(pathToFileURL(out+'/build-browser.mjs'));const server=await createPerformancePreviewServer(0,{realNavigation:true});let browser;const errors=[],cases=[];
try{
 browser=await chromium.launch({channel:'msedge',headless:true});
 for(const height of [720,800]){
  const page=await browser.newPage({viewport:{width:1280,height}});page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);await page.locator('.uv-setting-toggle').waitFor();await page.waitForFunction(()=>window.__uvPorts.stats.probe===1);await page.clock.install();
  await page.locator('.uv-setting-toggle').focus();await page.evaluate(()=>window.__press('confirm'));await page.waitForFunction(()=>window.__uvPorts.state.pending);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.hardware.at(-1)),0);
  await page.locator('.cpu-setting input[type=range]').fill('-6');await page.locator('.cpu-setting input[type=range]').dispatchEvent('change');await page.waitForFunction(()=>window.__uvPorts.stats.hardware.at(-1)===-6);
  await page.clock.runFor(10000);assert((await page.locator('.uv-trial').textContent()).includes('20 秒'));
  await page.locator('#test-performance').click();await page.locator('.uv-quick-button').waitFor();assert.equal(await page.locator('.cpu-setting').count(),0);assert((await page.locator('.uv-quick-button').textContent()).includes('开启'));
  await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{configurable:true,value:'hidden'});document.dispatchEvent(new Event('visibilitychange'));window.dispatchEvent(new Event('blur'));});
  await page.clock.runFor(20000);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.saves),1);
  await page.locator('#test-settings').click();await page.locator('.cpu-setting').waitFor();assert((await page.locator('.uv-trial').textContent()).includes('已自动保存'));assert((await page.locator('.cpu-setting').textContent()).includes('-6 CO 步进'));assert.equal(await page.evaluate(()=>window.__uvPorts.stats.probe),1);
  await page.locator('#test-performance').click();await page.getByRole('button',{name:'降压',exact:true}).focus();await page.evaluate(()=>window.__press('confirm'));await page.waitForFunction(()=>window.__uvPorts.state.pending);await page.locator('#test-settings').click();assert((await page.locator('.uv-setting-toggle').textContent()).includes('关闭'));await page.clock.runFor(30000);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.saves),2);
  await page.evaluate(()=>window.__uvPorts.stats.failNext=true);await page.locator('.uv-setting-toggle').click();await page.waitForFunction(()=>document.querySelector('.cpu-setting-error')?.textContent.includes('fixture failure'));assert.equal(await page.evaluate(()=>window.__uvPorts.state.pending),false);await page.clock.runFor(31000);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.saves),2);
  await page.screenshot({path:out+'/'+height+'-settings.png'});cases.push({height,routeUnmountPreserved:true,backgroundSave:true,oneProbe:true,bothSwitchesShared:true,failedWriteNotSaved:true});await page.close();
 }
 const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port+'/?rog=1');await page.getByRole('button',{name:'降压',exact:true}).waitFor();await page.getByRole('button',{name:'降压',exact:true}).click();assert.equal(await page.evaluate(()=>window.__uvPorts.stats.probe),0);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.hardware.length),0);cases.push({rogRejectedBeforeProbe:true});await page.close();
 for(const saved of ['on','off']){const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port+'/?saved='+saved);await page.waitForFunction(()=>window.__uvPorts.stats.hardware.length===1);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.hardware[0]),saved==='on'?-4:0);assert.equal(await page.evaluate(()=>window.__uvPorts.stats.saves),0);cases.push({startupSaved:saved,identityKeyOrderIndependent:true,noDuplicatePersistence:true});await page.close();}
 for(const path of ['/?hidden=1&performance=1','/?hidden=1']){
  const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port+path);await page.getByRole('button',{name:'降压',exact:true}).waitFor();
  await page.evaluate(async()=>{await window.__uvStartup;await window.__settle();});
  assert.deepEqual(await page.evaluate(()=>[window.__uvPorts.stats.reads,window.__uvPorts.stats.probe,window.__uvPorts.stats.hardware]),[1,0,[]]);
  await page.evaluate(()=>window.__uvSetPresented(true));await page.waitForFunction(()=>window.__uvState.ready&&window.__uvPorts.stats.reads===2);
  await page.evaluate(async()=>{window.__uvSetPresented(false);await window.__settle();window.__uvSetPresented(true);await window.__settle();});
  assert.deepEqual(await page.evaluate(()=>[window.__uvPorts.stats.reads,window.__uvPorts.stats.probe,window.__uvPorts.stats.hardware]),[2,1,[]]);
  cases.push({startupHidden:path.includes('performance')?'performance':'settings',noIdleProbe:true,firstShowProbeOnce:true,noRepeatReadOnShow:true});await page.close();
 }
 for(const path of ['/?hidden=1&saved=on','/?hidden=1&saved=off','/?hidden=1&pending=1']){
  const page=await browser.newPage();await page.goto('http://127.0.0.1:'+server.address().port+path);await page.evaluate(()=>window.__uvStartup);
  const result=await page.evaluate(()=>({probe:window.__uvPorts.stats.probe,writes:window.__uvPorts.stats.hardware,saves:window.__uvPorts.stats.saves,pending:window.__uvState.pending,remaining:window.__uvState.remaining,value:window.__uvState.value}));
  const pending=path.includes('pending');assert.equal(result.probe,1);assert.deepEqual(result.writes,pending?[]:[path.endsWith('on')?-4:0]);assert.equal(result.saves,0);
  if(pending)assert.deepEqual([result.pending,result.remaining,result.value],[true,18,-5]);
  cases.push({startupHidden:path,restoredBeforePresentation:true,trialRejoinWithoutWrite:pending});await page.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(out+'/browser-results.json',JSON.stringify({status:'PASS',scope:'Actual mainline runtime + both Vue components; native IO replaced by memory ports. Native hidden-window timer is tested separately.',cases,pageErrors:errors,sourcePins:server.previewEvidence.sourcePins},null,2),{flag:'wx'});console.log(JSON.stringify({status:'PASS',cases},null,2));
}catch(error){fs.writeFileSync(out+'/browser-failure.json',JSON.stringify({status:'FAIL',error:error.stack,cases,pageErrors:errors},null,2),{flag:'wx'});throw error;}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
