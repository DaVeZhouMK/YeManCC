// Focused adapter for the existing topProfile and dedicated draft preview entries.
// No new fixture ID, engine, native transport or installed application access.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'../../..');
const require=createRequire(path.join(root,'package.json'));
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_game-profile-summary.cjs'))('playwright');
const out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Testing/YMCC/Evidence/game-profile-summary-'+new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(out,{recursive:true});
const factoryFile=path.join(root,'tools/lib/continuousPerformancePreview.mjs');
const original=fs.readFileSync(factoryFile,'utf8');
// Adapt memory ports at the existing factory's build boundary without writing
// the shared helper owned by another task. The Vue compiler and engine stay intact.
const rootAnchor="const root=path.resolve(import.meta.dirname,'../..'),require=";
const buildAnchor='const built=await build(';
assert(original.includes(rootAnchor)&&original.includes(buildAnchor));
const adapted=original.replace(rootAnchor,'const root='+JSON.stringify(root)+',require=')
 .replace(buildAnchor,'if(options.summaryMemoryAdapter)entry=options.summaryMemoryAdapter(mocks,entry);\n'+buildAnchor);
const {createPerformancePreviewServer}=await import('data:text/javascript;base64,'+Buffer.from(adapted).toString('base64'));
const scheduleSource=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');
const skipClass=scheduleSource.match(/export class SkipApplyError extends Error \{[\s\S]*?\r?\n\}/)?.[0];
assert(skipClass,'Use the actual SkipApplyError class declaration');
function memoryAdapter(mocks,entry){
 if(mocks.profileQuickShell){const before='h(Profile,{game:this.game,open:this.open})';assert(mocks.profileQuickShell.includes(before));mocks.profileQuickShell=mocks.profileQuickShell.replace(before,'h(Profile,{game:this.game,open:this.open,onStatus:value=>{window.__fixture.profileStatus=value}})');}
 if(mocks.gamedetect)mocks.gamedetect+='export const getCurrentGame=()=>window.__fixture.game;';
 let port=mocks.performanceSchedule;
 const load=/export const loadGameCustomConfig=async\(\)=>\(\{version:1,entries:[^;]+;/;
 if(load.test(port)){
  port=port.replace(load,`export const loadGameCustomConfig=async()=>{
   f().uiConfig??={version:1,entries:{},rtss:{}};
   return copy(f().uiConfig);
  };`);
  assert(port.includes('export const patchGameCustomProfile=async()=>{};'));
  port=port.replace('export const patchGameCustomProfile=async()=>{};',`${skipClass}
   export const patchGameCustomProfile=async(target,patch,seed)=>{
    const x=f();x.profileSaveCalls??=[];x.profileSaveCalls.push({target:copy(target),patch:copy(patch)});
    if(x.uiScenario==='save-outdated')throw new SkipApplyError();
    if(x.uiScenario==='save-fail')throw Error('fixture disk full');
    x.uiConfig??={version:1,entries:{},rtss:{}};
    x.uiConfig.entries[target.exe]={...copy(seed||x.uiConfig.entries[target.exe]),...patch};
    x.profileSaveSuccesses=(x.profileSaveSuccesses||0)+1;
   };`);
  assert(port.includes('export const restorePerformanceScheduleIfConfigured=async()=>f().effectiveScope;'));
  port=port.replace('export const restorePerformanceScheduleIfConfigured=async()=>f().effectiveScope;',`export const restorePerformanceScheduleIfConfigured=async()=>{
   const x=f();x.restoreCalls=(x.restoreCalls||0)+1;
   if(x.uiScenario==='apply-fail')throw Error('fixture hardware owner unavailable');
   if(x.uiScenario==='apply-outdated')throw new SkipApplyError();
   if(x.uiScenario==='apply-none')return 'none';
   x.effectiveScope='dedicated';return 'dedicated';
  };`);
 }
 // Identity values come from the observed game in this memory port. The target
 // producer itself is compiled from src/bridge/gameCustomProfileTarget.ts.
 const identityAnchor="processCreated:x.game?.processCreated||'',generation:1";
 assert(port.includes(identityAnchor));
 port=port.replace(identityAnchor,"processCreated:x.game?.processCreated||'',path:x.game?.path,identityGeneration:x.game?.identityGeneration,generation:1");
 mocks.performanceSchedule=port;
 const mountAnchor="app.mount('#app');";
 assert(entry.includes(mountAnchor));
 entry=entry.replace(mountAnchor,`window.__fixture.game.identityGeneration=9;
  window.__fixture.uiScenario=params.get('scenario')||'fresh';
  if(window.__fixture.uiScenario==='existing')window.__fixture.uiConfig={version:1,entries:{'a.exe':{enabled:true,displayName:'Fixture',controls:structuredClone(window.__fixture.dedicatedControls),ac:{},dc:{},padPersona:'follow',gyroOverride:'follow'}},rtss:{}};
  ${mountAnchor}`);
 return entry;
}
const report={pass:false,scope:'Existing topProfile / dedicated draft entry, actual Vue components and production engine, memory storage and hardware ports only',cases:[],pageErrors:[],sourcePins:{},screenshots:[]};
let browser,top,editor;
function record(name,height,detail={}){report.cases.push({name,height,pass:true,...detail});}
try{
 top=await createPerformancePreviewServer(0,{realNavigation:true,topProfile:true,summaryMemoryAdapter:memoryAdapter});
 const helper=path.join(root,'src/bridge/gameCustomProfileTarget.ts');
 assert(top.previewEvidence.sourcePins[helper],'Actual target helper must be compiled');
 Object.assign(report.sourcePins,top.previewEvidence.sourcePins);
 browser=await chromium.launch({channel:'msedge',headless:true});
 const page=await browser.newPage({viewport:{width:1280,height:720}});
 page.setDefaultTimeout(6000);page.on('pageerror',e=>report.pageErrors.push(e.stack));
 const body=page.locator('[data-gp-custom-body]');
 const summary=page.locator('[data-gp-custom-body] .parameter-summary');
 async function open(height,scenario){
  await page.setViewportSize({width:1280,height});
  await page.goto('http://127.0.0.1:'+top.address().port+'/?scenario='+scenario);
  await page.locator('.game-recognition-status').click();
  await page.locator('[data-gp-custom-entry]').waitFor();
 }
 async function expectSummary(){
  await summary.waitFor();
  await page.waitForFunction(()=>Number(getComputedStyle(document.querySelector('[data-gp-custom-body]')).opacity)>.99);
  assert.equal(await summary.locator(':scope > div').count(),3);
  assert.equal(await page.locator('[data-gp-custom-body] .continuous-performance :is(button,input,select,[tabindex],.integrated-heading,.supply-status,.save-note)').count(),0);
  assert.equal(await page.locator('[data-gp-custom-body] .continuous-performance > *').count(),1);
  const x=await summary.evaluate(el=>({r:el.getBoundingClientRect().toJSON(),cols:[...el.children].map(c=>c.getBoundingClientRect().toJSON())}));
  assert(x.r.width>0&&x.cols.every(c=>c.width>0&&Math.abs(c.top-x.cols[0].top)<2));
  assert(x.r.left>=0&&x.r.right<=1280&&x.r.bottom<=await page.evaluate(()=>innerHeight));
 }
 async function expectFirstFocus(){
  await page.waitForFunction(()=>{
   const first=document.querySelector('[data-gp-custom-body] button:not(:disabled)');
   return first&&document.activeElement===first;
  });
 }
 for(const height of [720,800]){
  await open(height,'fresh');
  assert.equal(await body.count(),0);
  await page.locator('[data-gp-custom-remove]').click();
  await expectSummary();await expectFirstFocus();
  const saved=await page.evaluate(()=>({calls:__fixture.profileSaveCalls,saves:__fixture.profileSaveSuccesses,game:__fixture.game,writes:__fixture.writes}));
  assert.equal(saved.saves,1);assert.equal(saved.calls.length,1);assert.deepEqual(saved.calls[0].target,{exe:'a.exe',pid:saved.game.pid,processCreated:saved.game.processCreated,path:saved.game.path,identityGeneration:saved.game.identityGeneration,generation:1});
  assert.deepEqual(saved.writes,[]);
  record('Successful save expands the shared three-column summary and focuses the first reachable input control',height);
  const autoImage=path.join(out,'dedicated-summary-auto-1280x'+height+'.png');await page.screenshot({path:autoImage});report.screenshots.push(autoImage);
  // Production directional engine follows the visible input row, then footer.
  const first=await page.evaluate(()=>document.activeElement.getAttribute('aria-label'));
  await page.evaluate(()=>__move(-1,0));assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),first);
  await page.evaluate(()=>__move(1,0));assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'当前游戏专属陀螺仪开关');
  await page.evaluate(()=>__move(1,0));assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'当前游戏专属陀螺仪开关');
  await page.evaluate(()=>__move(0,1));
  assert.equal(await page.evaluate(()=>document.activeElement.closest('[data-gp-game-row]')?.dataset.gpGameRow),'custom-actions');
  const focused=await page.evaluate(()=>{const r=document.activeElement.getBoundingClientRect(),p=document.querySelector('.game-recognition-popover').getBoundingClientRect();return r.top>=p.top&&r.bottom<=p.bottom;});assert(focused);
  await page.evaluate(()=>__move(0,-1));
  assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'当前游戏专属陀螺仪开关');
  record('Directions stay on the visible row, stop at edges, reach the footer and return with focus visible',height);
  // A opens the production dropdown; B first cancels the dropdown, then collapses.
  await page.evaluate(()=>__press('confirm'));
  await page.waitForSelector('.dd-menu[role="listbox"]');
  await page.evaluate(()=>__press('back'));
  await page.waitForSelector('.dd-menu[role="listbox"]',{state:'detached'});
  assert.equal(await body.count(),1);
  await page.evaluate(()=>__press('back'));
  await body.waitFor({state:'detached'});
  await page.waitForFunction(()=>document.activeElement?.hasAttribute('data-gp-custom-entry'));
  await page.evaluate(()=>{__refresh();__setPower('dc')});
  await page.evaluate(()=>__settle());assert.equal(await body.count(),0);
  record('A/dropdown/B ownership is preserved; B collapses and refresh/power changes preserve manual collapse',height);
  await page.locator('[data-gp-custom-entry]').click();await expectSummary();
  await page.evaluate(()=>{
   Object.assign(__fixture.dedicatedControls.ac,{mode:'manual',tdp:31});Object.assign(__fixture.dedicatedControls.ac.manual,{frequencyMHz:2200,aggressiveness:55,turbo:false,unlimited:false});
   Object.assign(__fixture.dedicatedControls.dc,{mode:'manual',tdp:19});Object.assign(__fixture.dedicatedControls.dc.manual,{frequencyMHz:1800,aggressiveness:40,turbo:true,unlimited:false});
   __setPower('ac');__refresh();
  });
  await page.waitForFunction(()=>document.querySelector('.parameter-summary')?.textContent.includes('2.2 GHz'));
  assert.match(await summary.innerText(),/31[\s\S]*关睿频[\s\S]*2\.2 GHz[\s\S]*55%/);
  await page.evaluate(()=>__setPower('dc'));
  await page.waitForFunction(()=>document.querySelector('.parameter-summary')?.textContent.includes('1.8 GHz'));
  assert.match(await summary.innerText(),/19[\s\S]*开睿频[\s\S]*1\.8 GHz[\s\S]*40%/);
  assert.deepEqual(await page.evaluate(()=>__fixture.writes),[]);
  const image=path.join(out,'dedicated-summary-1280x'+height+'.png');await page.screenshot({path:image});report.screenshots.push(image);
  record('Shared manual summary follows AC/DC, frequency, turbo and aggressiveness without performance writes',height);
  for(const scenario of ['save-fail','save-outdated','apply-fail','apply-outdated','apply-none']){
   await open(height,scenario);await page.locator('[data-gp-custom-remove]').click();
   const saved=scenario.startsWith('apply');
   const text=saved?'专属配置已保存，应用尚未完成':'启用专属失败';
   await page.waitForFunction(text=>window.__fixture.profileStatus?.error?.includes(text),text);
   if(saved){await expectSummary();await expectFirstFocus();assert.equal(await page.evaluate(()=>__fixture.profileSaveSuccesses),1)}
   else{assert.equal(await body.count(),0);assert.equal(await page.evaluate(()=>__fixture.restoreCalls||0),0)}
   if(scenario.endsWith('outdated'))assert.match(await page.evaluate(()=>window.__fixture.profileStatus.error),/当前操作已失效，请重新打开专属配置后重试/);
   assert.equal(await page.evaluate(()=>__fixture.profileSaveCalls.length),1);
   record(scenario+': saved/apply status, friendly outdated message, expansion and no retry',height);
  }
 }
 // The same existing draft harness checks only the requested identity-change UI boundary.
 editor=await createPerformancePreviewServer(0,{realNavigation:true,draftHarness:true,summaryMemoryAdapter:memoryAdapter});
 Object.assign(report.sourcePins,editor.previewEvidence.sourcePins);
 for(const height of [720,800])for(const field of ['path','identityGeneration'])for(const event of ['policy','config']){
  await page.setViewportSize({width:1280,height});
  await page.goto('http://127.0.0.1:'+editor.address().port+'/?scope=dedicated');
  const slider=page.locator('.limits-bubble input[type="range"]').first();await slider.waitFor();await slider.focus();
  assert.equal(await page.evaluate(()=>__readEdit()),false);
  await page.evaluate(()=>__press('confirm'));assert.equal(await page.evaluate(()=>__readEdit()),true);
  await page.evaluate(({field,event})=>{const game={...__fixture.game};if(field==='path')game.path='C:/Moved/a.exe';else game.identityGeneration++;if(event==='policy')__setGame(game);else{__fixture.game=game;__setEffectiveScope('dedicated')}},{field,event});
  await page.waitForFunction(()=>!__readEdit());
  const before=await page.evaluate(()=>__fixture.writes.length);
  await page.evaluate(()=>__press('nav-right'));
  assert.equal(await page.evaluate(()=>__fixture.writes.length),before);
  record('Same PID/birth '+field+' change via '+event+' invalidates active slider editing before another write',height);
 }
 assert.deepEqual(report.pageErrors,[]);
 for(const file of [factoryFile,path.join(root,'src/gamepad/engine.ts'),path.join(root,'src/bridge/performanceSchedule.ts')])report.sourcePins[file]=createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 for(const [file,hash] of Object.entries(report.sourcePins))assert.equal(createHash('sha256').update(fs.readFileSync(file)).digest('hex'),hash,'Source changed during UI replay: '+file);
 report.inputsUnchanged=true;report.pass=true;
} catch(error){report.failure=error.stack;throw error;}
finally{
 await browser?.close();for(const server of [top,editor])if(server)await new Promise(resolve=>server.close(resolve));
 report.adapterSha256=createHash('sha256').update(fs.readFileSync(import.meta.filename)).digest('hex');
 report.factorySha256=createHash('sha256').update(original).digest('hex');
 fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2)+'\n');
 console.log(JSON.stringify({pass:report.pass,cases:report.cases.length,evidence:out,failure:report.failure},null,2));
}
