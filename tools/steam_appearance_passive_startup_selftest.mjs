import fs from 'node:fs';import path from 'node:path';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.env.YMCC_APPEARANCE_TEST_OUT||'G:/YeManCC-Work/Build/Validation/SteamDetailsFix-20261018/passive-startup');fs.mkdirSync(out,{recursive:true});process.env.YMCC_APPEARANCE_TEST_OUT=out;
const source=fs.readFileSync(path.join(root,'tools/steam_appearance_browser_selftest.mjs'),'utf8'),end=source.indexOf('let browser,activePage;');if(end<0)throw Error('Browser fixture boundary missing');let setup=source.slice(0,end).replace('snapshot:{packAvailable:true,packPrepared:true,','snapshot:{targetId:"a".repeat(16),packOwned:true,packUpdateAvailable:location.search.includes("repair"),packAvailable:true,packPrepared:!location.search.includes("repair"),').replace('steamRunning:true,configuredMatchesBackend:true,note:', 'steamRunning:!location.search.includes("stopped"),configuredMatchesBackend:true,note:').replace('export const launchSteam=async()=>{};', 'export const launchSteam=async()=>{window.__fixture.steamLaunches=(window.__fixture.steamLaunches||0)+1};').replace('export const steamStop=async()=>{};', 'export const steamStop=async()=>{window.__fixture.steamStops=(window.__fixture.steamStops||0)+1};');
const tail=String.raw`
let browser;const cases=[],errors=[];
try{
 browser=await chromium.launch({channel:'msedge',headless:true});
 for(const scenario of [{name:'Stopped Steam and saved appearance enabled',query:'?stopped'},{name:'Existing Steam with themes already enabled',query:''},{name:'Existing Steam and a resource upgrade pending',query:'?repair'},{name:'Frontend restart restores an unsubmitted selection',query:'?journal',journal:true}]){
  const page=await browser.newPage({viewport:{width:1280,height:720}});page.on('pageerror',e=>errors.push(e.message));
  await page.clock.install();
  if(scenario.journal)await page.addInitScript(()=>localStorage.setItem('YMCC.steamAppearanceDraft.v1:'+'a'.repeat(16),JSON.stringify({version:1,targetId:'a'.repeat(16),states:{'YMCC Game Details':false,Round:false},savedAt:Date.now()})));
  await page.goto('http://127.0.0.1:'+server.address().port+'/'+scenario.query);await page.waitForFunction(()=>window.__draft.getState().snapshot);
  await page.clock.fastForward(65000);
  const result=await page.evaluate(()=>({hidden:window.__fixture.hiddenCalls,launches:window.__fixture.steamLaunches||0,stops:window.__fixture.steamStops||0,reloads:window.__fixture.reloads||0,presets:window.__fixture.presets,actions:window.__fixture.calls.map(c=>c.action),phase:window.__draft.getState().phase,changes:window.__draft.getState().changes}));
  assert.equal(result.hidden,0);assert.equal(result.launches,0);assert.equal(result.stops,0);assert.equal(result.reloads,0);assert.equal(result.presets.length,0);assert(result.actions.every(a=>a==='Status'),JSON.stringify(result));
  if(scenario.query==='?repair'){assert.match(await page.locator('body').innerText(),/新版主题资源待应用/);assert.doesNotMatch(await page.locator('body').innerText(),/首次修改时备份并接管/);}
  if(scenario.journal){assert.equal(result.phase,'restored');assert.equal(result.changes['YMCC Game Details'],false);}
  await page.reload();await page.waitForFunction(()=>window.__draft.getState().snapshot);await page.clock.fastForward(65000);
  const after=await page.evaluate(()=>({hidden:window.__fixture.hiddenCalls,launches:window.__fixture.steamLaunches||0,stops:window.__fixture.steamStops||0,reloads:window.__fixture.reloads||0,actions:window.__fixture.calls.map(c=>c.action)}));assert.equal(after.hidden+after.launches+after.stops+after.reloads,0);assert(after.actions.every(a=>a==='Status'));
  cases.push({name:scenario.name,beforeReload:result,afterReload:after});await page.close();
 }
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({ok:true,passed:cases.length,cases,errors,scope:'Current real Steam Vue page and appearance service/queue; simulated IPC and timers; each initial mount and browser reload has 65 seconds of fake elapsed time; no real Steam operations'},null,2));console.log('PASS: '+cases.length+' frontend restart / no Steam launch or mode-replay scenarios');
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
`;
const generated=path.join(out,'passive-startup-runner.mjs');fs.writeFileSync(generated,setup+tail);await import(pathToFileURL(generated).href);
