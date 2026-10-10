// Next-round preparation and actual in-game alpha matrix. No game launch/settings writes.
// Pixel evidence is always CopyFromScreen; neither DOM nor hook success is a visual pass.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {targets,connect} from './fixtures/decky_cdp.mjs';

const [action,rawDir]=process.argv.slice(2);
if(!['--preflight','--run'].includes(action))throw Error('Use --preflight or --run and an owned task directory');
const dir=path.resolve(rawDir||'');
const owned='G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\';
if(!dir.toLowerCase().startsWith(owned.toLowerCase()))throw Error('Owned HUD-probe task directory required');
const runtime=path.join(dir,'steam-hud-probe-runtime.js');
assert.ok(fs.existsSync(runtime),'Run steam_hud_probe_selftest.mjs in this directory first');
const reportFile=path.join(dir,action==='--preflight'?'ALPHA-PREFLIGHT.json':'2077-ALPHA-INTEGRATION.json');
assert.ok(!fs.existsSync(reportFile),'Preserve previous attempts: choose a fresh task directory');
const report={action,fixture:{source:'fixture',origin:'existing H5 preview',fps:60,cpuPowerW:18,cpuTempC:67,gpuUsage:86},
  backgroundAlphaPresets:[0,0.35,0.65,0.90],textAlpha:1,layout:'corner',actualSteam:true,
  uniformDataConnected:false,hardwareCommands:0,settingsWrites:0,
  visualVerified:false,passiveHudVerified:false,inputVerified:false,exclusiveFullscreenVerified:false,steps:[],captures:[]};
const save=()=>fs.writeFileSync(reportFile,JSON.stringify(report,null,2));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let shared,overlay,installed=false,openedByHarness=false,runId,seq=0;
const receipt='__YMCC_HUD_ALPHA_EVENTMAP_RECEIPT__';
function windowAction(name,filename){
  const args=['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(import.meta.dirname,'steam_hud_probe_game_window.ps1'),'-Action',name];
  if(filename)args.push('-OutputPath',path.join(dir,filename));
  const output=execFileSync('powershell.exe',args,{timeout:15000,windowsHide:true,encoding:'utf8',stdio:['ignore','pipe','pipe']});
  const value=JSON.parse(output.trim());
  (report.windowActions??=[]).push(value);save();return value;
}
const diagnostics=()=>shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__?.diagnostics()');
async function waitFor(predicate){
  for(let i=0;i<70;i++){const d=await diagnostics();if(predicate(d))return d;await sleep(100);}
  throw Error('HUD context mount/cleanup timeout');
}
async function dom(){
  return overlay.evaluate(`(()=>{
    const h=document.querySelector('[data-ymcc-steam-hud-probe]');
    const parents=[];for(let p=h?.parentElement;p&&parents.length<8;p=p.parentElement){const c=getComputedStyle(p);parents.push({tag:p.tagName,opacity:c.opacity,background:c.backgroundColor});}
    const c=h?getComputedStyle(h):null;
    return {title:document.title,hidden:document.hidden,focused:document.hasFocus(),
      width:innerWidth,height:innerHeight,count:document.querySelectorAll('[data-ymcc-steam-hud-probe]').length,
      text:h?.innerText,rect:h?.getBoundingClientRect().toJSON(),
      background:c?.backgroundColor,color:c?.color,containerOpacity:c?.opacity,pointerEvents:c?.pointerEvents,
      backgroundAlpha:h?.getAttribute('data-background-alpha'),textAlpha:h?.getAttribute('data-text-alpha'),parents,
      focusTag:document.activeElement?.tagName,focusRole:document.activeElement?.getAttribute('role')};
  })()`);
}
async function injectFixture(){
  const packet={...report.fixture,runId,seq:++seq,collectedAt:Date.now()};
  delete packet.origin;
  assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.update('+JSON.stringify(packet)+')'),true);
  await sleep(120);
}
async function capture(stage,alpha){
  await injectFixture();
  const filename='2077-ALPHA-'+Math.round(alpha*100).toString().padStart(2,'0')+'-'+stage+'.png';
  const before=await dom();
  assert.equal(before.count,1);assert.equal(before.backgroundAlpha,String(alpha));
  assert.equal(before.textAlpha,'1');assert.equal(before.containerOpacity,'1');
  assert.equal(before.pointerEvents,'none');assert.ok(before.text.includes('示例数据'));
  const window=windowAction('Capture',filename),after=await dom(),diag=await diagnostics();
  report.captures.push({stage,expectedHotkeyState:stage,actualNativeMenuStateNotIndependentlyVerified:true,
    alpha,domBefore:before,domAfter:after,diagnostics:diag,window,path:path.join(dir,filename),pixelReview:'pending'});
  save();
}
function savedDisplaySetting(){
  // The saved option can be stale until the game saves. It is not proof of DXGI exclusivity.
  const settings=path.join(process.env.LOCALAPPDATA||'','CD Projekt Red','Cyberpunk 2077','UserSettings.json');
  if(!fs.existsSync(settings))return null;
  const json=JSON.parse(fs.readFileSync(settings,'utf8'));
  return {path:settings,readOnly:true,notRuntimeDxgiProof:true,options:(json.data??[]).flatMap(g=>(g.options??[])
    .filter(o=>['WindowMode','Resolution'].includes(o.name)).map(o=>({group:g.group_name,name:o.name,value:o.value})))};
}

try{
  const ts=await targets(),target=ts.find(t=>t.title==='SharedJSContext');
  assert.ok(target,'Already-running Steam SharedJSContext required');shared=await connect(target.webSocketDebuggerUrl);
  report.before=await shared.evaluate(`(async()=>({probePresent:!!window.__YMCC_STEAM_HUD_PROBE__,
    receiptPresent:Object.hasOwn(window,${JSON.stringify(receipt)}),plugins:window.DeckyPluginLoader?.plugins.map(p=>p.name),
    game:DFL.Router.MainRunningApp?{appid:DFL.Router.MainRunningApp.appid,gameid:DFL.Router.MainRunningApp.gameid,name:DFL.Router.MainRunningApp.display_name}:null,
    overlayInfo:await SteamClient.Overlay.GetOverlayBrowserInfo()}))()`);
  assert.equal(report.before.probePresent,false,'Do not replace another probe');assert.equal(report.before.receiptPresent,false);
  assert.ok(report.before.plugins.includes('ymcc-sidebar'),'Existing YMCC Decky required');
  report.savedDisplaySetting=savedDisplaySetting();save();
  if(action==='--run'){
    assert.match(report.before.game?.name??'',/Cyberpunk(?:2077)?/i,'The running Steam game must be Cyberpunk');
    const game=report.before.game;
    const infos=report.before.overlayInfo.filter(i=>[String(game.appid),String(game.gameid)].includes(String(i.gameID)));
    const candidates=ts.filter(t=>infos.some(i=>t.title===`SP Overlay: ${i.unPID}/${i.nBrowserID}/${i.eBrowserType}`));
    assert.equal(candidates.length,1,'Require one uniquely game-matched Overlay context, never pick an unrelated game');
    report.overlayTarget={id:candidates[0].id,title:candidates[0].title};
    overlay=await connect(candidates[0].webSocketDebuggerUrl);
    report.domBefore=await dom();
    assert.equal(report.domBefore.count,0);
    assert.equal(report.domBefore.hidden,true,'Start with Steam overlay closed; close it manually if needed');
    report.gameWindow=windowAction('Inspect');
    windowAction('Capture','2077-ALPHA-BASELINE.png');
    report.captureMethod='CopyFromScreen-game-client';
    report.displayMode='user-requested-fullscreen; saved option and geometry recorded separately, not DXGI proof';
  }
  await shared.evaluate(`window[${JSON.stringify(receipt)}]=window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');true`);
  // Mark the attempted installation before evaluating so partial installs are also disposed.
  installed=true;
  await shared.evaluate(fs.readFileSync(runtime,'utf8'));
  report.capabilities=await diagnostics();assert.equal(report.capabilities.available,true,'Required original Steam hook/context absent or ambiguous');
  if(action==='--preflight'){
    report.prepared=true;report.gameRequiredForNextRound=true;
    report.steps.push('Actual Steam/Loader resolves the original hook and router; nothing mounted, no keys sent');
  }else{
    runId='hud-probe-alpha-'+Date.now();
    const config={kind:'integration-test',runId,expiresAt:Date.now()+170000,scope:'overlay-only'};
    assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.start('+JSON.stringify(config)+')'),true);
    report.mounted=await waitFor(d=>d.instances.some(i=>i.mode===d.capabilities.modes.Overlay)&&d.traces.some(t=>t.event==='lease-mounted'));
    for(const alpha of report.backgroundAlphaPresets){
      assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.setAppearance('+JSON.stringify(runId)+','+JSON.stringify({backgroundAlpha:alpha,textAlpha:1,layout:report.layout})+')'),true);
      windowAction('ToggleOverlay');openedByHarness=true;await sleep(500);
      await capture('HOTKEY-OPEN',alpha);
      windowAction('ToggleOverlay');openedByHarness=false;await sleep(650);
      await capture('HOTKEY-CLOSED',alpha);
    }
    report.finalDiagnostics=await diagnostics();
    assert.equal(report.finalDiagnostics.traces.filter(t=>t.event==='lease-mounted').length,1,'Alpha/data changes must not reacquire composition');
    report.steps.push('Eight real game-client frames recorded; visual/passive/input/fullscreen claims remain pending manual review');
    assert.equal(await shared.evaluate('window.__YMCC_STEAM_HUD_PROBE__.stop('+JSON.stringify(runId)+')'),true);
    report.stopped=await waitFor(d=>!d.registered&&d.instances.length===0&&d.counts.listeners===0);
    assert.equal(report.stopped.timers.deadline,false);assert.equal(report.stopped.timers.freshness,false);
    windowAction('Capture','2077-ALPHA-AFTER-STOP.png');
    report.domAfterStop=await dom();assert.equal(report.domAfterStop.count,0);
    report.domHookPass=true;
  }
  save();
}catch(error){report.failure=String(error.stack||error);save();process.exitCode=1;}
finally{
  if(openedByHarness){try{report.menuRestore=windowAction('ToggleOverlay');openedByHarness=false;}catch(e){report.menuRestoreFailure=String(e);process.exitCode=1;}}
  if(installed&&shared){
    try{
      report.cleanup=await shared.evaluate(`(()=>{
        window.__YMCC_STEAM_HUD_PROBE__?.dispose();
        const eventMapUnchanged=window[${JSON.stringify(receipt)}]===window.DeckyPluginLoader.pluginEventListeners.get('ymcc-sidebar');
        delete window[${JSON.stringify(receipt)}];
        return {probeAbsent:!window.__YMCC_STEAM_HUD_PROBE__,eventMapUnchanged,
          componentAbsent:![...window.__ROUTER_HOOK_INSTANCE.globalComponentsState._components.keys()].some(k=>k.startsWith('YMCCSteamHudProbe-')),
          plugins:window.DeckyPluginLoader.plugins.map(p=>p.name)};
      })()`);
      assert.equal(report.cleanup.probeAbsent,true);assert.equal(report.cleanup.componentAbsent,true);
      assert.equal(report.cleanup.eventMapUnchanged,true);assert.deepEqual(report.cleanup.plugins,report.before.plugins);
    }catch(error){report.cleanupFailure=String(error);process.exitCode=1;}
  }
  report.prepared=report.prepared===true&&!report.failure&&!report.cleanupFailure;
  save();shared?.close();overlay?.close();
}
console.log(JSON.stringify({prepared:report.prepared,domHookPass:report.domHookPass??false,
  visualVerified:report.visualVerified,passiveHudVerified:report.passiveHudVerified,
  failure:report.failure,cleanup:report.cleanup,report:reportFile},null,2));
