import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {repo,require,hash,fingerprints,runtimeBundle,listenBootFixture} from './harness.mjs';
import {buildBrowser} from './buildBrowser.mjs';
import {actualGpuBridge} from './gpuScheduleFirstRun.mjs';

// Only the terminal is simulated. The GPU declarations below this port are
// extracted from yeman.ts by the same AST helper used by the bridge checks.
const memoryPortSource=String.raw`
const gpuInitial=JSON.parse(new URLSearchParams(location.search).get('gpu')||'{}');
const gpuMemory={value:gpuInitial.missing?undefined:gpuInitial.value??null,ack:true,
 readError:!!gpuInitial.readError,writeError:false,wrongReadback:false,
 osEnabled:gpuInitial.osEnabled??false,osQueryError:!!gpuInitial.osQueryError,osQueries:[],
 holdReads:!!gpuInitial.holdReads,holdNextRead:false,holdWrite:false,
 reads:[],writes:[],pendingReads:[],pendingWrites:[]};
(window as any).__gpuScheduleFixture=gpuMemory;
async function invoke<T>(op:string,args:unknown):Promise<T>{
 if(op!=='gpuSchedule.getState'||JSON.stringify(args)!=='{}')throw Error('Unexpected GPU memory invoke command or arguments');
 const call={op,args,enabled:gpuMemory.osEnabled,failed:gpuMemory.osQueryError};gpuMemory.osQueries.push(call);
 if(call.failed)throw Error('fixture GPU state query denied');return {ok:true,enabled:call.enabled} as T;
}
function gpuAddress(root:string,key:string,name:string){
 if(root!=='HKLM'||key!=='SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers'||name!=='HwSchMode')throw Error('Unexpected GPU memory registry address');
}
const registry={
 async read(root:string,key:string,name:string){
  gpuAddress(root,key,name);const value=gpuMemory.value,fail=gpuMemory.readError;
  const call={id:gpuMemory.reads.length,root,path:key,name,value,missing:value===undefined,failed:fail,completed:false};gpuMemory.reads.push(call);
  if(gpuMemory.holdReads||gpuMemory.holdNextRead){gpuMemory.holdNextRead=false;await new Promise<void>(release=>gpuMemory.pendingReads.push({id:call.id,release}));}
  call.completed=true;if(fail)throw Error('fixture GPU read denied');return value;
 },
 async write(root:string,key:string,name:string,value:number){
  gpuAddress(root,key,name);const ack=gpuMemory.ack,fail=gpuMemory.writeError,wrong=gpuMemory.wrongReadback;
  const call={id:gpuMemory.writes.length,root,path:key,name,value,ack,failed:fail,completed:false};gpuMemory.writes.push(call);
  if(gpuMemory.holdWrite)await new Promise<void>(release=>gpuMemory.pendingWrites.push({id:call.id,release}));
  call.completed=true;if(fail)throw Error('fixture GPU write denied');if(ack&&!wrong)gpuMemory.value=value;return ack;
 }
};
`;
const label='硬件加速GPU计划';
const recommendation='通常建议开启，AMD Ryzen AI Max 385 / Max+ 388、395 建议关闭';
const selector='.gpu-schedule-setting [role="switch"]';
const tracked=['src/bridge/yeman.ts','src/views/PowerView.vue','src/components/Toggle.vue','src/components/SwitchControl.vue','src/gamepad/engine.ts','src/gamepad/focus.ts','tools/boot_modes_fixture/browserEntry.ts','tools/boot_modes_fixture/browserNative.ts','tools/boot_modes_fixture/buildBrowser.mjs','tools/boot_modes_fixture/gpuScheduleFirstRun.mjs','tools/boot_modes_fixture/gpuScheduleBrowser.mjs'];
const settle=page=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
const command=async(page,action)=>{await page.evaluate(action=>window.__bootGamepad.command(action),action);await settle(page);};
const snapshot=async page=>{const state=await page.evaluate(()=>{
 const g=window.__gpuScheduleFixture,el=document.querySelector('.gpu-schedule-setting [role="switch"]');
 return {value:g.value,missing:g.value===undefined,reads:g.reads,writes:g.writes,osQueries:g.osQueries,pendingReads:g.pendingReads.length,pendingWrites:g.pendingWrites.length,
  checked:el?.getAttribute('aria-checked'),mixed:el?.classList.contains('mixed'),disabled:el?.disabled,
  description:document.querySelector('.gpu-schedule-setting .toggle-desc')?.textContent,error:document.querySelector('.err-bar')?.textContent||''};
});assert.equal(state.mixed,false,'GPU switch must always be binary');assert(['false','true'].includes(state.checked),'GPU aria-checked must be boolean');return state;};
async function focusGpu(page){
 await page.locator(selector).evaluate(el=>{if(!window.__bootGamepad.focusables().includes(el))throw Error('GPU switch is unreachable');window.__bootGamepad.focus(el);});
 await settle(page);
}
async function confirmGpu(page){await focusGpu(page);await command(page,'confirm');}
async function waitIdle(page){await page.waitForFunction(s=>!document.querySelector(s)?.disabled,selector);await settle(page);}
async function reenter(page){
 await page.evaluate(()=>window.__bootShowPower(false));await page.locator(selector).waitFor({state:'detached'});
 await page.evaluate(()=>window.__bootShowPower(true));await page.locator(selector).waitFor();await settle(page);
}
function exactWrites(writes,values){assert.deepEqual(writes.map(({root,path:address,name,value})=>({root,path:address,name,value})),values.map(value=>({root:'HKLM',path:String.raw`SYSTEM\CurrentControlSet\Control\GraphicsDrivers`,name:'HwSchMode',value})));}

export async function runGpuScheduleBrowser(output){
 const out=path.resolve(output);fs.mkdirSync(out,{recursive:true});const started=Date.now(),before=fingerprints(tracked);
 const report={schema:1,suite:'boot-modes/gpu-schedule/browser',pass:false,startedAt:new Date().toISOString(),
  scope:'Actual PowerView, Toggle, SwitchControl, production gamepad engine and AST-extracted yeman GPU bridge; isolated memory registry and readonly Windows-state invoke terminals.',
  formalReleaseGate:'NOT_RUN',physicalControllerAcceptance:false,physicalGpuRebootAcceptance:false,realRegistryWrites:0,
  cases:[],screenshots:[],pageErrors:[],consoleErrors:[],networkRejected:[],inputs:[],sourceHashes:{...before},hashes:{},timings:{},errors:[]};
 let browser,server,page,scene='setup';
 const shot=async(name)=>{const file=path.join(out,name+'.png');await page.screenshot({path:file});report.screenshots.push(file);return file;};
 try{
  const bridge=actualGpuBridge(),injection=memoryPortSource+'\n'+bridge;
  fs.writeFileSync(path.join(out,'actual-gpu-bridge.ts'),bridge);fs.writeFileSync(path.join(out,'gpu-memory-injection.ts'),injection);
  const buildStart=Date.now(),runtime=await runtimeBundle(out),seed=await runtime.boot.captureBootModeSnapshot();
  fs.writeFileSync(path.join(out,'boot-seed.json'),JSON.stringify(seed,null,2));
  const built=await buildBrowser(out,seed,{gpuScheduleBridgeSource:injection});
  report.inputs=[...new Set([...tracked,...runtime.inputs,...built.inputs])].sort();
  report.sourceHashes={...fingerprints(runtime.inputs),...built.sourceHashes,...before};
  assert.deepEqual(fingerprints(tracked),before,'GPU source changed during browser compilation');
  report.timings.buildMs=Date.now()-buildStart;
  const tools={node:process.execPath,esbuild:require.resolve('esbuild'),typescript:require.resolve('typescript'),vueCompiler:require.resolve('vue/compiler-sfc')};
  const buildEvidence={option:'gpuScheduleBridgeSource',actualProductionBridgeExtracted:true,startedAt:report.startedAt,
   finishedAt:new Date().toISOString(),elapsedMs:report.timings.buildMs,inputs:report.inputs,sourceHashes:report.sourceHashes,
   tools:Object.fromEntries(Object.entries(tools).map(([name,file])=>[name,{path:file,sha256:hash(file)}])),
   outputs:Object.fromEntries(['boot-runtime.mjs','boot-browser.js','boot-browser.css','index.html','boot-seed.json','actual-gpu-bridge.ts','gpu-memory-injection.ts'].map(file=>[file,hash(path.join(out,file))]))};
  fs.writeFileSync(path.join(out,'build-result.json'),JSON.stringify(buildEvidence,null,2));
  const publicRoot=path.join(repo,'public');
  server=http.createServer((req,res)=>{
   let requested;try{requested=decodeURIComponent(new URL(req.url,'http://127.0.0.1').pathname);}catch{res.writeHead(400);res.end();return;}
   const file=requested==='/'?'index.html':requested.slice(1);let source;
   if(['index.html','boot-browser.js','boot-browser.css'].includes(file))source=path.join(out,file);
   else{const candidate=path.resolve(publicRoot,file);if(candidate.startsWith(publicRoot+path.sep)&&fs.existsSync(candidate)&&fs.statSync(candidate).isFile()){source=candidate;report.sourceHashes[path.relative(repo,candidate).replaceAll('\\','/')]=hash(candidate);}}
   if(!source){res.writeHead(404);res.end();return;}
   const mime={'.js':'application/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.jpg':'image/jpeg','.mp4':'video/mp4'};
   res.setHeader('Content-Type',mime[path.extname(source)]||'text/html');res.end(fs.readFileSync(source));
  });
  await listenBootFixture(server);const origin='http://127.0.0.1:'+server.address().port;
  const playwrightAnchor='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_boot_preview.cjs';
  const playwrightRequire=createRequire(playwrightAnchor),{chromium}=playwrightRequire('playwright');
  report.tools={...buildEvidence.tools,playwright:{path:playwrightRequire.resolve('playwright'),sha256:hash(playwrightRequire.resolve('playwright'))}};
  browser=await chromium.launch({channel:'msedge',headless:true});report.browser={channel:'msedge',version:browser.version(),headless:true};
  const executionStart=Date.now();
  for(const height of [720,800]){
   page=await browser.newPage({viewport:{width:1280,height},reducedMotion:'no-preference'});page.setDefaultTimeout(6000);
   page.on('pageerror',error=>report.pageErrors.push({height,scene,error:String(error.stack||error)}));
   page.on('console',message=>{if(message.type()==='error')report.consoleErrors.push({height,scene,error:message.text()});});
   await page.route('**/*',async route=>{const url=route.request().url();if(url.startsWith(origin+'/')||url.startsWith('data:'))await route.continue();else{report.networkRejected.push({height,url});await route.abort();}});
   const load=async(config={value:null})=>{await page.goto(origin+'/?'+new URLSearchParams({gpu:JSON.stringify(config)}));await page.waitForFunction(()=>window.__gpuScheduleFixture&&window.__bootGamepad);await page.locator(selector).waitFor();if(!config.holdReads&&!config.readError&&!config.osQueryError)await waitIdle(page);};
   const check=async(id,name,run)=>{
    scene=id;const time=Date.now();try{const evidence=await run();report.cases.push({id,height,name,pass:true,ok:true,elapsedMs:Date.now()-time,evidence,state:await snapshot(page)});}
    catch(error){let screenshot;try{screenshot=await shot(height+'-'+id+'-FAIL');}catch{}report.cases.push({id,height,name,pass:false,ok:false,elapsedMs:Date.now()-time,error:String(error.stack||error),screenshot});}
   };
   await check('initial-loading','First GPU read is disabled and binary; clicks cannot create a preference',async()=>{
    await load({value:null,holdReads:true});await page.waitForFunction(()=>window.__gpuScheduleFixture.pendingReads.length>0);
    let state=await snapshot(page);assert.equal(state.disabled,true);assert.equal(state.checked,'false');assert.equal(state.description,'正在读取系统状态；'+recommendation);assert.equal(state.osQueries.length,0);
    assert.equal(await page.locator(selector).evaluate(el=>window.__bootGamepad.focusables().includes(el)),false);
    await page.locator('.gpu-schedule-setting').evaluate(el=>{el.click();el.click();el.querySelector('button').click();});assert.equal((await snapshot(page)).writes.length,0);
    await page.locator('.gpu-schedule-setting').scrollIntoViewIfNeeded();await shot(height+'-initial-loading');
    await page.evaluate(()=>{const g=window.__gpuScheduleFixture;g.holdReads=false;for(const held of g.pendingReads.splice(0))held.release();});await waitIdle(page);
    state=await snapshot(page);assert.equal(state.disabled,false);assert.equal(state.checked,'false');assert.equal(state.description,'当前设置：关闭；'+recommendation);assert.equal(state.osQueries.length,1);assert.equal(state.writes.length,0);
   });
   for(const [id,config,checked,prefix]of [
    ['missing',{missing:true},'false','当前设置：关闭；'],['null',{value:null},'false','当前设置：关闭；'],
    ['zero',{value:0},'false','当前设置：关闭；'],['other',{value:3},'false','当前设置：关闭；'],
    ['off',{value:1},'false','当前设置：关闭；'],['on',{value:2},'true','当前设置：开启；']]){
    await check('state-'+id,'Read '+id+' displays the exact state and recommendation without writing',async()=>{
     await load(config);const state=await snapshot(page);assert.equal(state.checked,checked);assert.equal(state.disabled,false);assert.equal(state.description,prefix+recommendation);assert.equal(state.writes.length,0);assert.equal(state.reads.length,1);assert.equal(state.osQueries.length,id==='off'||id==='on'?0:1);
     await focusGpu(page);assert(await page.locator('.gpu-schedule-setting .toggle-desc').isVisible());
     if(['null','off','on'].includes(id))await shot(height+'-state-'+id);
     if(id==='null'){
      await load({value:null,osEnabled:true});const on=await snapshot(page);assert.equal(on.value,null);assert.equal(on.checked,'true');assert.equal(on.disabled,false);assert.equal(on.description,'当前设置：开启；'+recommendation);assert.equal(on.reads.length,1);assert.equal(on.osQueries.length,1);assert.equal(on.osQueries[0].enabled,true);assert.equal(on.writes.length,0);await shot(height+'-state-null-os-on');return {states:[state,on]};
     }
    });
   }
   await check('gamepad-navigation','Production gamepad follows adjacent service rows, excludes sidebar, and scrolls GPU focus into view',async()=>{
    await load();const labels=await page.locator('[data-boot-startup-group="services"] [role="switch"]').evaluateAll(els=>els.map(el=>el.getAttribute('aria-label')));
    assert.equal(labels.at(-1),label);assert.equal(labels.at(-2),'网络加速');
    await page.getByRole('switch',{name:labels[0],exact:true}).evaluate(el=>window.__bootGamepad.focus(el));await settle(page);
    const visited=[];for(let i=0;i<labels.length;i++){
     if(i)await command(page,'nav-down');const active=await page.evaluate(()=>({label:document.activeElement?.getAttribute('aria-label'),sidebar:!!document.activeElement?.closest('.navrail')}));
     assert.equal(active.label,labels[i]);assert.equal(active.sidebar,false);visited.push(active.label);
    }
    for(const action of ['nav-up','nav-down']){await command(page,action);assert.equal(await page.evaluate(()=>document.activeElement?.getAttribute('aria-label')),action==='nav-up'?'网络加速':label);}
    const geometry=await page.locator(selector).evaluate(el=>{
     const rect=el.getBoundingClientRect(),row=el.closest('.toggle-row').getBoundingClientRect();let visible={left:0,top:0,right:innerWidth,bottom:innerHeight};
     for(let parent=el.parentElement;parent;parent=parent.parentElement){const css=getComputedStyle(parent),r=parent.getBoundingClientRect();if(/auto|scroll|hidden|clip/.test(css.overflowY)){visible.top=Math.max(visible.top,r.top);visible.bottom=Math.min(visible.bottom,r.bottom);}if(/auto|scroll|hidden|clip/.test(css.overflowX)){visible.left=Math.max(visible.left,r.left);visible.right=Math.min(visible.right,r.right);}}
     return {rect:rect.toJSON(),row:row.toJSON(),visible,scrollTop:document.querySelector('.app-content').scrollTop,scale:window.__bootShellGeometry().scale};
    });
    assert(geometry.scrollTop>0,'Navigation must scroll the actual content');for(const r of [geometry.rect,geometry.row]){assert(r.top>=geometry.visible.top-1&&r.bottom<=geometry.visible.bottom+1,'GPU focus and description must fit the visible scrollport');assert(r.left>=geometry.visible.left-1&&r.right<=geometry.visible.right+1);}
    assert.equal((await snapshot(page)).writes.length,0);await shot(height+'-gamepad-gpu-focus');return {visited,geometry};
   });
   await check('explicit-values','Gamepad A from Windows OFF writes exact 2 ON and exact 1 OFF with readback',async()=>{
    await load();const initial=await snapshot(page);assert.equal(initial.checked,'false');assert.equal(initial.value,null);assert.equal(initial.osQueries.length,1);assert.equal(initial.osQueries[0].enabled,false);assert.equal(initial.writes.length,0);await confirmGpu(page);await waitIdle(page);let state=await snapshot(page);assert.equal(state.value,2);assert.equal(state.checked,'true');assert.equal(state.mixed,false);assert.equal(state.description,'当前设置：开启；'+recommendation);assert.equal(state.osQueries.length,1);assert.equal(state.error,'硬件加速GPU计划已更新，重启电脑后生效');
    await confirmGpu(page);await waitIdle(page);state=await snapshot(page);assert.equal(state.value,1);assert.equal(state.checked,'false');assert.equal(state.description,'当前设置：关闭；'+recommendation);assert.equal(state.osQueries.length,1);exactWrites(state.writes,[2,1]);assert.equal(state.reads.length,3);assert.deepEqual(state.reads.slice(1).map(c=>c.value),[2,1]);await shot(height+'-explicit-off');
   });
   for(const fault of ['ack','wrongReadback','writeError','readError']){
    await check('failure-'+fault,'Rejected '+fault+' restores the displayed state and recovers on the next explicit operation',async()=>{
     await load({value:2});await page.evaluate(fault=>{window.__gpuScheduleFixture[fault]=fault!=='ack';},fault);
     await confirmGpu(page);await waitIdle(page);let state=await snapshot(page);assert.equal(state.checked,'true');assert.equal(state.mixed,false);assert.equal(state.disabled,false);
     const message=fault==='ack'||fault==='wrongReadback'?'注册表写入或回读验证失败':fault==='writeError'?'fixture GPU write denied':'fixture GPU read denied';
     assert.equal(state.error,'硬件加速GPU计划设置失败：'+message);exactWrites(state.writes,[1]);assert.equal(state.reads.length,fault==='ack'||fault==='writeError'?1:2);await shot(height+'-failure-'+fault);
     await page.evaluate(()=>Object.assign(window.__gpuScheduleFixture,{ack:true,wrongReadback:false,writeError:false,readError:false}));await reenter(page);await waitIdle(page);
     const actualOff=fault==='readError';assert.equal((await snapshot(page)).checked,actualOff?'false':'true');await confirmGpu(page);await waitIdle(page);state=await snapshot(page);assert.equal(state.checked,actualOff?'true':'false');assert.equal(state.value,actualOff?2:1);assert.equal(state.error,'硬件加速GPU计划已更新，重启电脑后生效');exactWrites(state.writes,[1,actualOff?2:1]);
    });
   }
   await check('read-failure-recovery','Registry or Windows query failure stays binary and disabled, then re-entry recovers without automatic writes',async()=>{
    const scenarios=[];for(const [source,config]of [['registry',{value:1,readError:true}],['windows-gpu-state',{value:null,osQueryError:true}]]){
     await load(config);await page.waitForFunction(()=>document.querySelector('.gpu-schedule-setting .toggle-desc')?.textContent.startsWith('状态读取失败'));
     let state=await snapshot(page);assert.equal(state.disabled,true);assert.equal(state.checked,'false');assert.equal(state.description,'状态读取失败，请重新进入页面重试；'+recommendation);assert.equal(state.reads.length,1);assert.equal(state.osQueries.length,source==='registry'?0:1);assert.equal(await page.locator(selector).evaluate(el=>window.__bootGamepad.focusables().includes(el)),false);const failed=state;
     await page.locator('.gpu-schedule-setting').evaluate(el=>{el.click();el.querySelector('button').click();});assert.equal((await snapshot(page)).writes.length,0);await page.locator('.gpu-schedule-setting').scrollIntoViewIfNeeded();await shot(height+'-'+source+'-read-failure-disabled');
     await page.evaluate(()=>Object.assign(window.__gpuScheduleFixture,{readError:false,osQueryError:false}));await reenter(page);await waitIdle(page);state=await snapshot(page);assert.equal(state.checked,'false');assert.equal(state.description,'当前设置：关闭；'+recommendation);assert.equal(state.osQueries.length,source==='registry'?0:2);assert.equal(state.writes.length,0);const recovered=state;
     await confirmGpu(page);await waitIdle(page);state=await snapshot(page);assert.equal(state.value,2);assert.equal(state.checked,'true');exactWrites(state.writes,[2]);assert.equal(state.osQueries.length,recovered.osQueries.length);scenarios.push({source,failed,recovered,afterExplicitWrite:state});
    }return {scenarios};
   });
   await check('busy-duplicates','Pending write rejects repeated row clicks, switch clicks and controller confirmations',async()=>{
    await load({value:2});await page.evaluate(()=>window.__gpuScheduleFixture.holdWrite=true);await confirmGpu(page);await page.waitForFunction(()=>window.__gpuScheduleFixture.pendingWrites.length===1);assert(await page.locator(selector).isDisabled());
    await page.locator('.gpu-schedule-setting').evaluate(el=>{for(let i=0;i<3;i++){el.click();el.querySelector('button').click();window.__bootGamepad.command('confirm');}});await settle(page);let state=await snapshot(page);exactWrites(state.writes,[1]);assert.equal(state.pendingWrites,1);await shot(height+'-pending-write-busy');
    await page.evaluate(()=>{const g=window.__gpuScheduleFixture;g.holdWrite=false;for(const held of g.pendingWrites.splice(0))held.release();});await waitIdle(page);state=await snapshot(page);exactWrites(state.writes,[1]);assert.equal(state.value,1);assert.equal(state.checked,'false');assert.equal(state.reads.length,2);
   });
   await check('late-old-read','Late old OFF activation read cannot overwrite a newer verified ON write',async()=>{
    await load({value:1});await page.evaluate(()=>window.__gpuScheduleFixture.holdNextRead=true);await reenter(page);await page.waitForFunction(()=>window.__gpuScheduleFixture.pendingReads.length===1);
    let state=await snapshot(page);assert.equal(state.reads.at(-1).value,1);assert.equal(state.checked,'false');assert.equal(state.disabled,false);
    await confirmGpu(page);await waitIdle(page);state=await snapshot(page);assert.equal(state.value,2);assert.equal(state.checked,'true');assert.equal(state.pendingReads,1);exactWrites(state.writes,[2]);
    await page.evaluate(()=>{for(const held of window.__gpuScheduleFixture.pendingReads.splice(0))held.release();});await settle(page);await settle(page);state=await snapshot(page);assert.equal(state.checked,'true');assert.equal(state.mixed,false);assert.equal(state.description,'当前设置：开启；'+recommendation);assert(state.reads.every(call=>call.completed));await shot(height+'-late-old-read-retains-on');
   });
   await page.close();page=null;
  }
  report.timings.executionMs=Date.now()-executionStart;
  assert.equal(report.pageErrors.length,0,'Browser page errors');assert.equal(report.consoleErrors.length,0,'Browser console errors');assert.equal(report.networkRejected.length,0,'Unexpected external network requests');
 }catch(error){report.errors.push(String(error.stack||error));}
 finally{
  if(browser)await browser.close();if(server)await new Promise(resolve=>server.close(resolve));
  const identityStart=Date.now();try{assert.deepEqual(fingerprints(Object.keys(report.sourceHashes)),report.sourceHashes,'Browser source identity changed during verification');report.sourceIdentityStable=true;}catch(error){report.sourceIdentityStable=false;report.errors.push(String(error.stack||error));}
  report.timings.identityMs=Date.now()-identityStart;report.timings.totalMs=Date.now()-started;
  report.passed=report.cases.filter(c=>c.pass).length;report.failed=report.cases.filter(c=>!c.pass).length;
  report.pass=report.cases.length>0&&report.failed===0&&report.errors.length===0;
  for(const file of ['boot-runtime.mjs','boot-browser.js','boot-browser.css','index.html','actual-gpu-bridge.ts','gpu-memory-injection.ts','build-result.json'])if(fs.existsSync(path.join(out,file)))report.hashes[file]=hash(path.join(out,file));
  report.screenshotHashes=Object.fromEntries(report.screenshots.map(file=>[file,hash(file)]));report.finishedAt=new Date().toISOString();
  fs.writeFileSync(path.join(out,'gpu-browser-results.json'),JSON.stringify(report,null,2));
 }
 if(!report.pass)throw Error('Focused GPU browser verification failed ('+report.passed+'/'+report.cases.length+'); see '+path.join(out,'gpu-browser-results.json'));
 return report;
}