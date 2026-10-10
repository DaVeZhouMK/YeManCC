// User-opened Cyberpunk 2077, authorized app switching and actual game-client capture.
// Only the original Steam overlay hotkey is sent; no game settings, gameplay or hardware commands.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';
import {targets,connect} from './fixtures/decky_cdp.mjs';
const dir=path.resolve(process.argv[2]||'');if(!dir.startsWith('G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\2077-'))throw Error('Owned 2077 probe directory required');
const report={game:'Cyberpunk2077.exe',actualSteam:true,actualGameClientCapture:true,mode:'user-reported-windowed',exclusiveFullscreenTested:false,borderlessTested:false,uniformDataConnected:false,source:'fixture or unconnected only',steps:[]};
const save=()=>fs.writeFileSync(path.join(dir,'2077-INTEGRATION.json'),JSON.stringify(report,null,2));const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let shared,overlay,installed=false,runId;
function windowAction(action,name){const args=['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(import.meta.dirname,'steam_hud_probe_game_window.ps1'),'-Action',action];if(name)args.push('-OutputPath',path.join(dir,name));const out=execFileSync('powershell.exe',args,{timeout:12000,windowsHide:true,encoding:'utf8',stdio:['ignore','pipe','pipe']});const value=JSON.parse(out.trim());report.windowActions??=[];report.windowActions.push(value);save();return value;}
const diagnostics=()=>shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__?.diagnostics()');
const dom=()=>overlay.evaluate(`(()=>{const h=document.querySelector('[data-ymcc-steam-hud-probe]');return {title:document.title,hidden:document.hidden,focused:document.hasFocus(),dimensions:{w:innerWidth,h:innerHeight},count:document.querySelectorAll('[data-ymcc-steam-hud-probe]').length,text:h?.innerText,rect:h?.getBoundingClientRect().toJSON(),pointer:h?getComputedStyle(h).pointerEvents:null,background:h?getComputedStyle(h).backgroundColor:null,bodyPreview:document.body.innerText.slice(0,1000)};})()`);
async function wait(predicate){for(let i=0;i<70;i++){const d=await diagnostics();if(predicate(d))return d;await sleep(100);}throw Error('Actual game overlay probe timeout: '+JSON.stringify(await diagnostics()));}
async function closedMenu(){windowAction('ToggleOverlay');await sleep(450);return diagnostics();}
try{
 const ts=await targets(),s=ts.find(t=>t.title==='SharedJSContext'),o=ts.find(t=>t.title.startsWith('SP Overlay:'));assert.ok(s&&o,'Actual running Steam overlay required');shared=await connect(s.webSocketDebuggerUrl);overlay=await connect(o.webSocketDebuggerUrl);
 report.before=await shared.evaluate(`(()=>({game:DFL.Router.MainRunningApp?{appid:DFL.Router.MainRunningApp.appid,gameid:DFL.Router.MainRunningApp.gameid,name:DFL.Router.MainRunningApp.display_name}:null,probePresent:!!window.__YMCC_STEAM_HUD_PROBE__,plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),overlayInfo:SteamClient.Overlay.GetOverlayBrowserInfo()}))()`);
 assert.match(report.before.game?.name??'',/Cyberpunk2077/i);assert.equal(report.before.probePresent,false,'Never replace an active probe');report.domBefore=await dom();save();
 await shared.evaluate(`window.__YMCC_HUD_PROBE_EVENTMAP_RECEIPT__=window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');true`);
 await shared.evaluate(fs.readFileSync(path.join(dir,'steam-hud-probe-runtime.js'),'utf8'));installed=true;
 runId='hud-probe-2077-'+Date.now();const config={kind:'integration-test',runId,expiresAt:Date.now()+150000,scope:'overlay-only'};
 assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.start('+JSON.stringify(config)+')'),true);
 report.mounted=await wait(d=>d.instances.some(i=>i.mode===d.capabilities.modes.Overlay)&&d.traces.some(t=>t.event==='lease-mounted'));
 report.openDom=await dom();assert.equal(report.openDom.count,1,'Probe must be inside actual game overlay DOM');assert.equal(report.openDom.pointer,'none');assert.ok(report.openDom.text.includes('实时数据未连接'));
 report.steps.push('probe mounts in actual Overlay mode rather than only MainGamepadUI');save();
 if(report.domBefore.hidden){windowAction('ToggleOverlay');await sleep(500);}
 windowAction('Capture','2077-PROBE-OPEN-UNCONNECTED.png');
 report.menuClosed=await closedMenu();report.closedDom=await dom();windowAction('Capture','2077-PROBE-CLOSED-UNCONNECTED.png');
 report.steps.push('game-client pixels captured with Steam menu open and then closed; visibility pending visual inspection');save();
 const packet={runId,seq:1,collectedAt:Date.now(),source:'fixture',fps:61,cpuPowerW:18.5,cpuTempC:67,gpuUsage:86};
 assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.update('+JSON.stringify(packet)+')'),true);await sleep(350);
 report.updated=await diagnostics();report.fixtureDom=await dom();windowAction('Capture','2077-PROBE-CLOSED-FIXTURE.png');assert.ok(report.fixtureDom.text.includes('示例数据'));
 report.steps.push('fixture update is labelled and sampled from actual game-client screen, not native Steam monitor values');save();
 await sleep(6500);report.stale=await diagnostics();report.staleDom=await dom();windowAction('Capture','2077-PROBE-CLOSED-STALE.png');assert.ok(report.staleDom.text.includes('数据已过期'));assert.equal(report.stale.view.data.seq,1);
 report.steps.push('real overlay DOM expires fixture data without fabricating producer sequence');save();
 assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.stop('+JSON.stringify(runId)+')'),true);report.stopped=await wait(d=>!d.registered&&d.instances.length===0&&d.counts.listeners===0);
 assert.equal(report.stopped.timers.deadline,false);assert.equal(report.stopped.timers.freshness,false);
 windowAction('Capture','2077-AFTER-PROBE-STOP.png');report.afterStopDom=await dom();assert.equal(report.afterStopDom.count,0);report.steps.push('stop removes actual overlay component and owned requests without closing game');
 report.domHookPass=true;report.visualVerification='pending-screenshot-review';save();
}catch(e){report.failure=String(e.stack||e);save();process.exitCode=1;}
finally{
 if(installed&&shared){try{report.cleanup=await shared.evaluate(`(()=>{const p=window.__YMCC_STEAM_HUD_PROBE__;p?.dispose();const eventMapUnchanged=window.__YMCC_HUD_PROBE_EVENTMAP_RECEIPT__===window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');delete window.__YMCC_HUD_PROBE_EVENTMAP_RECEIPT__;return {probeAbsent:!window.__YMCC_STEAM_HUD_PROBE__,componentAbsent:![...window.__ROUTER_HOOK_INSTANCE.globalComponentsState._components.keys()].some(k=>k.startsWith('YMCCSteamHudProbe-')),plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),eventMapUnchanged};})()`);save();}catch(e){report.cleanupFailure=String(e);save();process.exitCode=1;}}
 shared?.close();overlay?.close();
}
console.log(JSON.stringify({domHookPass:report.domHookPass??false,failure:report.failure,cleanup:report.cleanup,steps:report.steps,report:path.join(dir,'2077-INTEGRATION.json')},null,2));
