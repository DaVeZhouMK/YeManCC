// Explicit, finite 2077 native-child pixel test. NEVER auto-launches game or changes display settings.
// Not run by unit tests/readiness checks. Use --run only after human resumes live desktop tests.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';
import {targets,connect} from './fixtures/decky_cdp.mjs';
const args=process.argv.slice(2),arg=name=>args.includes(name)?args[args.indexOf(name)+1]:undefined;
assert.ok(args.includes('--run'),'Explicit --run required; preparation must not touch live Steam');
const dir=path.resolve(arg('--out')||''),ready=path.resolve(arg('--runtime')||''),pin=arg('--pin')||'creation';
const owned='G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\';assert.ok(dir.startsWith(owned)&&path.basename(dir).startsWith('2077-native-'));
assert.ok(ready.startsWith(owned)&&path.basename(ready)==='steam-hud-probe-runtime.js');assert.ok(['creation','none'].includes(pin));
const readiness=JSON.parse(fs.readFileSync(path.join(path.dirname(ready),'NATIVE-READY.json'),'utf8'));
assert.equal(readiness.readyForLiveTest,true,'Offline readiness checks required');
assert.equal(crypto.createHash('sha256').update(fs.readFileSync(ready)).digest('hex'),readiness.runtimeSha256,'Prepared runtime changed; rerun preparation checks');
const output=path.join(dir,'2077-NATIVE-CHILD-INTEGRATION.json');assert.ok(!fs.existsSync(output),'New output directory required; preserve previous reports');
fs.mkdirSync(dir,{recursive:true});
const report={game:'Cyberpunk2077.exe',source:'fixture',carrier:'native-popup',pin,layout:'corner',textAlpha:1,alphaPresets:[0,0.35,0.65,0.9],
 actualSteam:true,uniformDataConnected:false,settingsWrites:0,hardwareCommands:0,exclusiveFullscreenVerified:false,inputVerified:false,
 visualVerified:false,passiveHudVerified:false,frames:[],runtime:ready};
const save=()=>fs.writeFileSync(output,JSON.stringify(report,null,2)),sleep=ms=>new Promise(r=>setTimeout(r,ms));
let shared,overlay,installed=false,runId,seq=0;
const receipt='__YMCC_NATIVE_CHILD_EVENTMAP_RECEIPT__';
function action(name,filename){const a=['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(import.meta.dirname,'steam_hud_probe_game_window.ps1'),'-Action',name];if(filename)a.push('-OutputPath',path.join(dir,filename));if(report.windowBefore?.GamePid)a.push('-ExpectedGamePid',String(report.windowBefore.GamePid));const r=JSON.parse(execFileSync('powershell.exe',a,{windowsHide:true,encoding:'utf8',timeout:15000,stdio:['ignore','pipe','pipe']}).trim());(report.windowActions??=[]).push(r);save();return r;}
const diag=()=>shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__?.diagnostics()');
async function wait(predicate,label){for(let j=0;j<60;j++){const d=await diag();if(predicate(d))return d;await sleep(100);}throw Error('Native HUD '+label+' timeout: '+JSON.stringify(await diag()));}
function contextExpr(){return `[...DFL.Router.WindowStore.m_mapOverlayPopupByPID.values()].find(i=>i.BrowserWindow?.document?.title===${JSON.stringify(report.binding.overlayTitle)})`;}
async function nativeState(){return shared.evaluate(`(()=>{const i=${contextExpr()},m=i?.MenuStore,s=i?.CompositionStateStore;return {title:i?.BrowserWindow?.document?.title,rootHidden:i?.BrowserWindow?.document?.hidden,menuVisible:m?.IsSideMenuVisible?.(),menuInteractable:m?.IsSideMenuInteractable?.(),menu:m?.GetOpenSideMenu?.(),composition:s?.GetCompositionState?.(),route:i?.LocationPathName};})()`);}
async function frame(filename,alpha){
 if(runId){assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.update('+JSON.stringify({runId,seq:++seq,collectedAt:Date.now(),source:'fixture',fps:60,cpuPowerW:18,cpuTempC:67,gpuUsage:86})+')'),true,'Matching game/fixture update failed');await sleep(180);}
 const state=await nativeState(),diagnostics=await diag();
 if(runId){assert.equal(state.menuVisible,false,'Never accept a screenshot with native interactive menu open as passive HUD');assert.equal(diagnostics.nativeChildren.length,1,'Exactly one owned native child required');assert.equal(diagnostics.nativeChildren[0].dom?.count,1,'Child HUD DOM absent');assert.ok(diagnostics.nativeChildren[0].dom.text.includes('示例数据'));}
 const window=action('Capture',filename);report.frames.push({filename,alpha,state,diagnostics,window,path:path.join(dir,filename),pixelReview:'pending-human-review'});save();
}
function savedMode(){const p=path.join(process.env.LOCALAPPDATA||'','CD Projekt Red','Cyberpunk 2077','UserSettings.json');if(!fs.existsSync(p))return null;const j=JSON.parse(fs.readFileSync(p,'utf8'));return {readOnly:true,path:p,notRuntimeDxgiProof:true,options:(j.data??[]).flatMap(g=>(g.options??[]).filter(o=>['WindowMode','Resolution'].includes(o.name)).map(o=>({name:o.name,value:o.value})))};}
try{
 const ts=await targets(),s=ts.find(t=>t.title==='SharedJSContext');assert.ok(s,'Already-running Steam required');shared=await connect(s.webSocketDebuggerUrl);
 report.before=await shared.evaluate(`(async()=>({probePresent:!!window.__YMCC_STEAM_HUD_PROBE__,receiptPresent:Object.hasOwn(window,${JSON.stringify(receipt)}),plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),game:DFL.Router.MainRunningApp?{appid:DFL.Router.MainRunningApp.appid,gameid:DFL.Router.MainRunningApp.gameid,name:DFL.Router.MainRunningApp.display_name}:null,infos:await SteamClient.Overlay.GetOverlayBrowserInfo()}))()`);
 assert.equal(report.before.probePresent,false);assert.equal(report.before.receiptPresent,false);assert.match(report.before.game?.name??'',/Cyberpunk(?:2077)?/i);
 const infos=report.before.infos.filter(i=>[String(report.before.game.appid),String(report.before.game.gameid)].includes(String(i.gameID)));
 const matched=ts.filter(t=>infos.some(i=>t.title===`SP Overlay: ${i.unPID}/${i.nBrowserID}/${i.eBrowserType}`));assert.equal(matched.length,1,'One current game-matched overlay browser required');
 report.overlayTarget={id:matched[0].id,title:matched[0].title};overlay=await connect(matched[0].webSocketDebuggerUrl);
 report.binding=await shared.evaluate(`(()=>{const list=[...DFL.Router.WindowStore.m_mapOverlayPopupByPID.values()].filter(i=>i.BrowserWindow?.document?.title===${JSON.stringify(matched[0].title)});if(list.length!==1)throw Error('Ambiguous/current overlay context missing');const i=list[0],b=i.params.browserInfo,w=i.BrowserWindow;if(!i.IsGamepadUIOverlayWindow())throw Error('Not a gamepad game overlay');return {appId:i.MainRunningAppID,gameId:String(i.MainRunningApp.gameid),browserGameId:String(b.m_gameID),overlayTitle:w.document.title,overlayPid:b.m_unPID,overlayBrowserId:b.m_nBrowserID,overlayBrowserType:b.m_eBrowserType,overlayNativeAppId:b.m_unAppID,ownerBrowserId:w.SteamClient.Browser.GetBrowserID()};})()`);
 report.browserIdsIntentionallyNotAssumedEqual=true;
 const info=infos.find(i=>matched[0].title===`SP Overlay: ${i.unPID}/${i.nBrowserID}/${i.eBrowserType}`);
 assert.equal(report.binding.appId,report.before.game.appid);assert.equal(report.binding.gameId,String(report.before.game.gameid));
 assert.equal(report.binding.overlayPid,info.unPID);assert.equal(report.binding.overlayBrowserId,info.nBrowserID);assert.equal(report.binding.overlayNativeAppId,info.appID);assert.ok([String(report.before.game.appid),String(report.before.game.gameid)].includes(report.binding.browserGameId),'Context browser game must map to the running game, not an unrelated target');report.overlayInfoGameId=String(info.gameID);
 report.savedMode=savedMode();report.nativeBefore=await nativeState();report.windowBefore=action('Inspect');
 assert.equal(await overlay.evaluate(`document.querySelectorAll('[data-ymcc-steam-hud-probe]').length`),0);
 // Unlike earlier heat-key-only testing, the original MenuStore is read and closed explicitly once.
 if(report.nativeBefore.menuVisible){report.menuClose=await shared.evaluate(`(()=>{const i=${contextExpr()};if(i.LocationPathName!=='/apprunning')throw Error('Only running-game side menu may be closed');i.MenuStore.CloseSideMenus();return {originalCloseSideMenus:true};})()`);await sleep(800);}
 assert.equal((await nativeState()).menuVisible,false);
 await frame('2077-NATIVE-BASELINE.png');
 await shared.evaluate(`window[${JSON.stringify(receipt)}]=window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');true`);
 installed=true;await shared.evaluate(fs.readFileSync(ready,'utf8'));
 const before=await diag();report.capabilities=before.capabilities;assert.equal(before.capabilities.nativePopup.available,true,'Original native popup hook/flags/portal missing or ambiguous');
 runId='hud-probe-native-2077-'+Date.now();report.runId=runId;
 const config={kind:'integration-test',runId,expiresAt:Date.now()+170000,scope:'overlay-only',carrier:'native-popup',popupPin:pin,gameBinding:report.binding};
 assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.start('+JSON.stringify(config)+')'),true);
 report.mounted=await wait(d=>d.nativeChildren.length===1&&!d.nativeChildren[0].released,'child-mount');
 assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.setAppearance('+JSON.stringify(runId)+', '+JSON.stringify({backgroundAlpha:0.86,textAlpha:1,layout:'corner'})+')'),true);
 for(const alpha of report.alphaPresets){assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.setAppearance('+JSON.stringify(runId)+', '+JSON.stringify({backgroundAlpha:alpha,textAlpha:1,layout:'corner'})+')'),true);await frame('2077-NATIVE-ALPHA-'+Math.round(alpha*100).toString().padStart(2,'0')+'.png',alpha);}
 // A short passive stability frame, with a NEW explicitly fixture packet (not fake source timestamps).
 await sleep(1800);await frame('2077-NATIVE-STABILITY.png',0.9);
 report.beforeStop=await diag();assert.equal(report.beforeStop.traces.filter(t=>t.event==='native-child-shown-request').length,1,'Data/alpha must not recreate child');
 assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.stop('+JSON.stringify(runId)+')'),true);runId=null;
 report.stopped=await wait(d=>!d.registered&&d.nativeChildren.length===0&&d.instances.length===0&&d.counts.listeners===0,'stop');
 assert.equal(report.stopped.timers.deadline,false);assert.equal(report.stopped.timers.freshness,false);
 await frame('2077-NATIVE-AFTER-STOP.png');report.domHookLifecyclePass=true;report.visualVerified=false;save();
}catch(e){report.failure=String(e.stack||e);process.exitCode=1;save();}
finally{
 if(installed&&shared)try{
  report.cleanup=await shared.evaluate(`(()=>{window.__YMCC_STEAM_HUD_PROBE__?.dispose();const eventMapUnchanged=window[${JSON.stringify(receipt)}]===window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');delete window[${JSON.stringify(receipt)}];return {probeAbsent:!window.__YMCC_STEAM_HUD_PROBE__,componentAbsent:![...window.__ROUTER_HOOK_INSTANCE.globalComponentsState._components.keys()].some(k=>k.startsWith('YMCCSteamHudProbe-')),plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),eventMapUnchanged};})()`);
  assert.equal(report.cleanup.probeAbsent,true);assert.equal(report.cleanup.componentAbsent,true);assert.equal(report.cleanup.eventMapUnchanged,true);assert.deepEqual(report.cleanup.plugins,report.before.plugins);
  for(let i=0;i<30;i++){report.childCleanup=await shared.evaluate(`(()=>({remainingOwnedPopups:[...window.g_PopupManager.GetPopups()].filter(p=>p.GetName().startsWith(${JSON.stringify('YMCCSteamHudChild-'+(report.runId??'no-run')+'-')})).map(p=>({name:p.GetName(),closed:p.window?.closed??true}))}))()`);if(report.childCleanup.remainingOwnedPopups.length===0)break;await sleep(100);}
  assert.equal(report.childCleanup.remainingOwnedPopups.length,0,'Owned native child still registered after stop');
 }catch(e){report.cleanupFailure=String(e);process.exitCode=1;}
 save();shared?.close();overlay?.close();
}
console.log(JSON.stringify({domHookLifecyclePass:report.domHookLifecyclePass??false,visualVerified:false,passiveHudVerified:false,failure:report.failure,cleanup:report.cleanup,childCleanup:report.childCleanup,report:output},null,2));
