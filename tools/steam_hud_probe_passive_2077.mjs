// Actual game-client pixel test. Explicitly closes the real Steam side menu once;
// no CSS hides native menus, no permanent patches, no alternate HTML window.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';
import {targets,connect} from './fixtures/decky_cdp.mjs';
const pinTest=process.argv.includes('--pin'),activateTest=process.argv.includes('--activate');
const dir=path.resolve(process.argv[2]||'');assert.ok(dir.startsWith('G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\'));
const outfile=path.join(dir,'PASSIVE-INTEGRATION.json');assert.ok(!fs.existsSync(outfile),'Keep attempt history');
const report={actualSteam:true,actualGameClientPixels:true,fixtureOnly:true,uniformDataConnected:false,
  settingsWrites:0,hardwareCommands:0,nativePinTest:pinTest,nativeActivateTest:activateTest,visualVerified:false,passiveHudVerified:false,inputVerified:false,exclusiveFullscreenVerified:false,frames:[]};
const save=()=>fs.writeFileSync(outfile,JSON.stringify(report,null,2)),sleep=ms=>new Promise(r=>setTimeout(r,ms));
let shared,overlay,installed=false,nativePinAttempted=false,openedByHarness=false,runId,seq=0;
const receipt='__YMCC_HUD_PASSIVE_EVENTMAP_RECEIPT__';
function win(action,name){const args=['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(import.meta.dirname,'steam_hud_probe_game_window.ps1'),'-Action',action];if(name)args.push('-OutputPath',path.join(dir,name));const r=JSON.parse(execFileSync('powershell.exe',args,{encoding:'utf8',windowsHide:true,timeout:15000,stdio:['ignore','pipe','pipe']}).trim());(report.windowActions??=[]).push(r);save();return r;}
const diag=()=>shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__?.diagnostics()');
function instanceExpr(){return `[...DFL.Router.WindowStore.m_mapOverlayPopupByPID.values()].find(i=>i.BrowserWindow?.document?.title===${JSON.stringify(report.overlayTarget.title)})`;}
async function native(){return shared.evaluate(`(()=>{const i=${instanceExpr()},s=i?.CompositionStateStore,m=i?.MenuStore;return {title:i?.BrowserWindow?.document?.title,hidden:i?.BrowserWindow?.document?.hidden,openMenu:m?.GetOpenSideMenu?.(),menuVisible:m?.IsSideMenuVisible?.(),menuInteractable:m?.IsSideMenuInteractable?.(),menuExtended:m?.m_cSideMenuExtendedVisibilityRequests,composition:s?.GetCompositionState?.(),lastPushed:s?.m_eLastPushedToWebHelperCompositionState,route:i?.LocationPathName};})()`);}
async function dom(){return overlay.evaluate(`(()=>{const h=document.querySelector('[data-ymcc-steam-hud-probe]'),c=h?getComputedStyle(h):null;return {hidden:document.hidden,focus:document.hasFocus(),count:document.querySelectorAll('[data-ymcc-steam-hud-probe]').length,text:h?.innerText,rect:h?.getBoundingClientRect().toJSON(),bg:c?.backgroundColor,color:c?.color,pointer:c?.pointerEvents,opacity:c?.opacity,width:innerWidth,height:innerHeight};})()`);}
async function frame(name,alpha){
 if(runId){assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.update('+JSON.stringify({runId,seq:++seq,collectedAt:Date.now(),source:'fixture',fps:60,cpuPowerW:18,cpuTempC:67,gpuUsage:86})+')'),true);await sleep(120);}
 const before=await native(),d=await dom(),window=win('Capture',name),after=await native();report.frames.push({name,alpha,nativeBefore:before,nativeAfter:after,dom:d,window,path:path.join(dir,name),pixelReview:'pending'});save();
}
try{
 const ts=await targets(),s=ts.find(t=>t.title==='SharedJSContext');assert.ok(s);shared=await connect(s.webSocketDebuggerUrl);
 report.before=await shared.evaluate(`(async()=>({probePresent:!!window.__YMCC_STEAM_HUD_PROBE__,plugins:window.DeckyPluginLoader.plugins.map(p=>p.name),game:DFL.Router.MainRunningApp?{appid:DFL.Router.MainRunningApp.appid,gameid:DFL.Router.MainRunningApp.gameid,name:DFL.Router.MainRunningApp.display_name}:null,info:await SteamClient.Overlay.GetOverlayBrowserInfo()}))()`);
 assert.equal(report.before.probePresent,false);assert.match(report.before.game?.name??'',/Cyberpunk/i);
 const info=report.before.info.filter(i=>[String(report.before.game.appid),String(report.before.game.gameid)].includes(String(i.gameID)));
 const os=ts.filter(t=>info.some(i=>t.title===`SP Overlay: ${i.unPID}/${i.nBrowserID}/${i.eBrowserType}`));assert.equal(os.length,1);report.overlayTarget={id:os[0].id,title:os[0].title};overlay=await connect(os[0].webSocketDebuggerUrl);
 report.nativeBefore=await native();report.beforeWindow=win('Inspect');await frame('PASSIVE-BASELINE.png');
 await shared.evaluate(`window[${JSON.stringify(receipt)}]=window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');true`);
 installed=true;await shared.evaluate(fs.readFileSync(path.join(dir,'steam-hud-probe-runtime.js'),'utf8'));
 runId='hud-probe-passive-'+Date.now();assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.start('+JSON.stringify({kind:'integration-test',runId,expiresAt:Date.now()+170000,scope:'overlay-only'})+')'),true);
 for(let j=0;j<60;j++){const d=await diag();if(d.instances.some(i=>i.mode===4))break;await sleep(100);}
 assert.equal((await dom()).count,1);await frame('PASSIVE-BEFORE-MENU-CLOSE.png',0.92);
 if(activateTest){
  assert.equal((await dom()).hidden,true,'Activate test starts with native overlay hidden');
  win('ToggleOverlay');openedByHarness=true;await sleep(600);await frame('NATIVE-ACTIVATED-MENU-OPEN.png',0.92);
 }

 report.closeResult=await shared.evaluate(`(()=>{const i=${instanceExpr()};if(!i?.MenuStore||i.LocationPathName!=='/apprunning')throw Error('Actual running-game menu context required');const before=i.MenuStore.GetOpenSideMenu();i.MenuStore.CloseSideMenus();return {calledOriginalCloseSideMenus:true,before,after:i.MenuStore.GetOpenSideMenu()};})()`);
 await sleep(850);report.nativeAfterClose=await native();assert.equal(report.nativeAfterClose.menuVisible,false,'Do not call menus hidden unless actual native store says so');
 if(pinTest){
  nativePinAttempted=true;
  report.nativePinInstall=await overlay.evaluate(`(()=>{
   if(window.__YMCC_NATIVE_PIN_TEST__)throw Error('Existing native pin test');
   if(typeof SteamClient.Overlay.SetWindowPinned!=='function'||typeof SteamClient.Window.ShowWindow!=='function')throw Error('Native pinned window API required');
   const beforeHidden=document.hidden;
   if(!beforeHidden)throw Error('Native pin test requires initially hidden root, no active user overlay');
   let disposed=false;const restore=()=>{if(disposed)return;disposed=true;clearTimeout(timer);window.removeEventListener('pagehide',restore);try{SteamClient.Overlay.SetWindowPinned(false);}finally{if(beforeHidden)SteamClient.Window.HideWindow();delete window.__YMCC_NATIVE_PIN_TEST__;}};
   const timer=setTimeout(restore,180000);window.addEventListener('pagehide',restore);window.__YMCC_NATIVE_PIN_TEST__={restore,beforeHidden};
   SteamClient.Overlay.SetWindowPinned(true);return {beforeHidden,pinCalled:true,settingsStoreSaveNotCalled:true};
  })()`);
  await sleep(500);await frame('NATIVE-PIN-ONLY.png',0.92);
  report.nativeShow=await overlay.evaluate(`(()=>{if(!window.__YMCC_NATIVE_PIN_TEST__)throw Error('Owned pin lease required');SteamClient.Window.ShowWindow();return {showCalled:true};})()`);
  await sleep(600);await frame('NATIVE-PIN-SHOW.png',0.92);
 }
 for(const alpha of [0,0.35,0.65,0.9]){
  assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.setAppearance('+JSON.stringify(runId)+','+JSON.stringify({backgroundAlpha:alpha,textAlpha:1,layout:'corner'})+')'),true);await sleep(250);
  await frame('PASSIVE-ALPHA-'+Math.round(alpha*100).toString().padStart(2,'0')+'.png',alpha);
 }
 report.diagnostics=await diag();
 if(activateTest){win('ToggleOverlay');openedByHarness=false;await sleep(700);await frame('NATIVE-HOTKEY-CLOSED.png',0.9);report.afterHotkey=await native();}
 report.domHookPass=true;
}catch(e){report.failure=String(e.stack||e);process.exitCode=1;save();}
finally{
 if(openedByHarness)try{win('ToggleOverlay');openedByHarness=false;report.nativeSessionRestored=true;}catch(e){report.nativeSessionRestoreFailure=String(e);process.exitCode=1;}
 if(nativePinAttempted&&overlay)try{report.nativePinCleanup=await overlay.evaluate(`(()=>{window.__YMCC_NATIVE_PIN_TEST__?.restore();return {pinLeaseAbsent:!window.__YMCC_NATIVE_PIN_TEST__,hidden:document.hidden};})()`);}catch(e){report.nativePinCleanupFailure=String(e);process.exitCode=1;}
 if(installed&&shared)try{
  report.cleanup=await shared.evaluate(`(()=>{window.__YMCC_STEAM_HUD_PROBE__?.dispose();const eventMapUnchanged=window[${JSON.stringify(receipt)}]===window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');delete window[${JSON.stringify(receipt)}];return {probeAbsent:!window.__YMCC_STEAM_HUD_PROBE__,eventMapUnchanged,componentAbsent:![...window.__ROUTER_HOOK_INSTANCE.globalComponentsState._components.keys()].some(k=>k.startsWith('YMCCSteamHudProbe-')),plugins:window.DeckyPluginLoader.plugins.map(p=>p.name)};})()`);
  assert.equal(report.cleanup.probeAbsent,true);assert.equal(report.cleanup.eventMapUnchanged,true);assert.equal(report.cleanup.componentAbsent,true);assert.deepEqual(report.cleanup.plugins,report.before.plugins);await sleep(250);report.nativeAfterCleanup=await native();runId=null;await frame('PASSIVE-AFTER-STOP.png');
 }catch(e){report.cleanupFailure=String(e);process.exitCode=1;}
 save();shared?.close();overlay?.close();
}
console.log(JSON.stringify({domHookPass:report.domHookPass??false,visualVerified:false,passiveHudVerified:false,failure:report.failure,cleanup:report.cleanup,report:outfile},null,2));
