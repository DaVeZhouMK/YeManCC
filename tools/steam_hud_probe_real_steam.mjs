// Actual running Steam / current Loader, passive ephemeral probe only.
// Does not restart Steam/YMCC/Loader, reload the original plugin, or write business settings.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';
import {targets,connect} from './fixtures/decky_cdp.mjs';
const dir=path.resolve(process.argv[2]||'');
if(!dir.startsWith('G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\'))throw Error('Owned probe directory required');
const noCapture=process.argv.includes('--no-capture');
const report={capturesIntentionallySkipped:noCapture,actualSteam:true,standaloneHtml:false,gameTested:false,fullscreenTested:false,uniformDataConnected:false,fixtureDataOnly:true,evidenceLevel:'actual-steam-dom-and-hook-not-game-pixels',visualVerified:false,hardwareCommands:0,settingsWrites:0,steps:[]};
const save=()=>fs.writeFileSync(path.join(dir,'REAL-STEAM-INTEGRATION.json'),JSON.stringify(report,null,2));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));let shared,page,installed=false;
const diag=()=>shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__?.diagnostics()');
async function wait(predicate){for(let i=0;i<45;i++){const d=await diag();if(predicate(d))return d;await sleep(80);}throw Error('Probe condition timeout: '+JSON.stringify(await diag()));}
const dom=()=>page.evaluate(`(()=>{const h=document.querySelector('[data-ymcc-steam-hud-probe]');return {count:document.querySelectorAll('[data-ymcc-steam-hud-probe]').length,text:h?.innerText,rect:h?.getBoundingClientRect().toJSON(),pointer:h?getComputedStyle(h).pointerEvents:null,backgroundAlpha:h?.getAttribute('data-background-alpha'),textAlpha:h?.getAttribute('data-text-alpha'),background:h?getComputedStyle(h).backgroundColor:null,color:h?getComputedStyle(h).color:null,opacity:h?getComputedStyle(h).opacity:null,focus:document.activeElement?.tagName,focusRole:document.activeElement?.getAttribute('role'),title:document.title};})()`);
const config=(scope,ms=18000)=>({kind:'integration-test',runId:'hud-probe-'+scope+'-'+Date.now(),expiresAt:Date.now()+ms,scope});
async function start(cfg){assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.start('+JSON.stringify(cfg)+')'),true);}
async function stop(cfg){assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.stop('+JSON.stringify(cfg.runId)+')'),true);return wait(d=>!d.registered&&d.instances.length===0&&d.counts.listeners===0);}
async function screenshot(name){if(noCapture)return false;try{const output=execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(import.meta.dirname,'steam_hud_probe_capture.ps1'),'-OutputPath',path.join(dir,name)],{timeout:10000,windowsHide:true,encoding:'utf8',stdio:['ignore','pipe','pipe']});report.screenshots??=[];report.screenshots.push(JSON.parse(output.trim()));return true;}catch(e){report.screenshotFailures??=[];report.screenshotFailures.push({name,error:String(e.message)});return false;}}
try{
  const ts=await targets(),s=ts.find(t=>t.title==='SharedJSContext'),p=ts.find(t=>t.title==='Steam 大屏幕模式'||t.title==='Steam Big Picture Mode'||t.title==='SP');
  assert.ok(s&&p,'Actual Steam shared and gamepad page must already be running');
  shared=await connect(s.webSocketDebuggerUrl);page=await connect(p.webSocketDebuggerUrl);
  report.target={shared:s.title,page:p.title};
  report.before=await shared.evaluate(`(()=>({runningApp:DFL.Router.MainRunningApp??null,probeAlreadyPresent:!!window.__YMCC_STEAM_HUD_PROBE__,plugins:window.DeckyPluginLoader.plugins.map(p=>p.name)}))()`);
  assert.equal(report.before.probeAlreadyPresent,false,'Do not replace another probe');assert.equal(report.before.runningApp,null,'This phase must not disturb a running game');
  report.domBefore=await dom();
  await shared.evaluate(`window.__YMCC_HUD_PROBE_EVENTMAP_RECEIPT__=window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');true`);
  installed=true;await shared.evaluate(fs.readFileSync(path.join(dir,'steam-hud-probe-runtime.js'),'utf8'));
  report.installed=await diag();assert.equal(report.installed.available,true,'Unique composition/context and public router API required');save();
  const overlay=config('overlay-only');await start(overlay);
  report.overlayOnly=await wait(d=>d.instances.length>0);assert.ok(report.overlayOnly.instances.every(i=>i.mode===1));assert.equal(report.overlayOnly.traces.some(t=>t.event==='lease-mounted'),false,'No overlay-only lease in main UI');
  assert.equal((await dom()).count,0);await stop(overlay);report.steps.push('overlay-only scope refuses MainGamepadUI and paints nothing');save();
  const main=config('main-window-test',60000);await start(main);
  report.main=await wait(d=>d.traces.some(t=>t.event==='lease-mounted')&&d.instances.length>0);
  report.domMounted=await dom();assert.equal(report.domMounted.count,1);assert.equal(report.domMounted.pointer,'none');assert.ok(report.domMounted.text.includes('实时数据未连接'));assert.ok(report.domMounted.text.includes('非游戏验收'));
  assert.equal(report.domMounted.focus,report.domBefore.focus);assert.equal(report.domMounted.focusRole,report.domBefore.focusRole);report.focusEvidence='DOM active element unchanged only; physical keyboard/gamepad not tested';
  await screenshot('STEAM-MAIN-HUD-UNCONNECTED.png');
  report.steps.push('actual Steam DOM mounts passive modern HUD; unconnected fields are —; pixel visibility is not yet verified');save();
  const mainMount=report.main.traces.filter(t=>t.event==='lease-mounted').at(-1),initialNotification=mainMount.detail.counts['1'];
  for(const seq of [1,2]){
    const packet={runId:main.runId,seq,collectedAt:Date.now(),source:'fixture',fps:seq===1?60:61,cpuPowerW:18,cpuTempC:67,gpuUsage:86};
    assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.update('+JSON.stringify(packet)+')'),true);await sleep(120);
  }
  assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.update('+JSON.stringify({runId:main.runId,seq:3,collectedAt:Date.now(),source:'ymcc-uniform',fps:99,cpuPowerW:99,cpuTempC:99,gpuUsage:99})+')'),false,'Unwired uniform provenance must not be fabricated');
  report.fixture=await dom();assert.ok(report.fixture.text.includes('示例数据'));assert.ok(report.fixture.text.includes('61'));
  const updated=await diag();assert.equal(updated.traces.filter(t=>t.event==='lease-mounted').length,1,'Updates must not reacquire composition');
  report.alphaCases=[];
  for(const alpha of [0,0.35,0.65,0.90]){
    assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.setAppearance('+JSON.stringify(main.runId)+','+JSON.stringify({backgroundAlpha:alpha,textAlpha:1})+')'),true);await sleep(80);
    const d=await dom(),di=await diag();report.alphaCases.push({alpha,dom:d});
    assert.equal(d.backgroundAlpha,String(alpha));assert.equal(d.textAlpha,'1');assert.equal(d.opacity,'1');assert.equal(d.color,'rgb(241, 245, 250)');
    assert.equal(d.background.replace(/\s/g,''),'rgba(16,22,31,'+alpha+')');
    assert.equal(di.view.data.seq,2);assert.equal(di.traces.filter(t=>t.event==='lease-mounted').length,1);
    assert.equal(di.instances[0].counts['1'],initialNotification);
  }
  report.steps.push('actual MainGamepadUI DOM verifies independent panel alpha, opaque glyphs and unchanged data/composition lease; NOT game pixels');
  await screenshot('STEAM-MAIN-HUD-FIXTURE.png');
  report.steps.push('explicit fixture packets update display, never control revision or hardware; composition acquired only once');save();
  await sleep(6150);report.stale=await dom();assert.ok(report.stale.text.includes('数据已过期'));assert.ok(report.stale.text.includes('—'));assert.equal((await diag()).view.data.seq,2,'Expiry must not invent producer sequence');
  report.steps.push('expired data is blanked with visible stale label; producer sequence stays unchanged');save();
  report.stopped=await stop(main);assert.equal(report.stopped.timers.deadline,false);assert.equal(report.stopped.timers.freshness,false);assert.equal((await dom()).count,0);
  const unmount=report.stopped.traces.filter(t=>t.event==='lease-unmounted').at(-1);assert.equal(unmount.detail.counts['1'],initialNotification-1,'Owned Notification request restored');
  report.steps.push('explicit stop removes actual HUD, subscribers and timers; Notification request count restored');save();
  const expires=config('main-window-test',1100);await start(expires);await wait(d=>d.registered&&d.instances.length>0);await sleep(1250);
  report.expired=await wait(d=>!d.registered&&d.instances.length===0);assert.equal(report.expired.timers.deadline,false);assert.equal(report.expired.counts.listeners,0);assert.equal((await dom()).count,0);
  report.steps.push('bounded automatic expiry also releases real component and composition');report.pass=true;save();
} catch(error){report.failure=String(error.stack||error);save();process.exitCode=1;}
finally{
  if(installed&&shared){try{report.cleanup=await shared.evaluate(`(()=>{const p=window.__YMCC_STEAM_HUD_PROBE__;p?.dispose();const componentAbsent=![...window.__ROUTER_HOOK_INSTANCE.globalComponentsState._components.keys()].some(k=>k.startsWith('YMCCSteamHudProbe-'));const eventMapUnchanged=window.__YMCC_HUD_PROBE_EVENTMAP_RECEIPT__===window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');delete window.__YMCC_HUD_PROBE_EVENTMAP_RECEIPT__;return {probeAbsent:!window.__YMCC_STEAM_HUD_PROBE__,plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),eventMapUnchanged,componentAbsent};})()`);report.originalPluginListUnchanged=JSON.stringify(report.before.plugins)===JSON.stringify(report.cleanup.plugins);if(!report.originalPluginListUnchanged||!report.cleanup.eventMapUnchanged){report.pass=false;report.cleanupFailure='Original plugin event map changed';process.exitCode=1;}save();}catch(e){report.cleanupFailure=String(e);save();process.exitCode=1;}}
  shared?.close();page?.close();
}
console.log(JSON.stringify({pass:report.pass??false,steps:report.steps,failure:report.failure,cleanup:report.cleanup,report:path.join(dir,'REAL-STEAM-INTEGRATION.json')},null,2));
