import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
const selectorFlags=process.argv.slice(2).filter(value=>value.startsWith('--case='));
assert(selectorFlags.length<=1,'DUPLICATE_CASE_SELECTOR');
const compositionSelector=selectorFlags.length?selectorFlags[0].slice('--case='.length):null;
const allowedCompositionSelectors=['menu-composition/popup-native-decky-lifetime','menu-composition/popup-native-decky-wrapper','menu-composition/popup-native-depth-path','menu-composition/popup-native-depth','menu-composition/popup-native-pair-guard','menu-composition/popup-native-shared-return','menu-composition/popup-native-shared-child','menu-composition/popup-native-focus','menu-composition/popup-native-focus-control','menu-composition/popup-previous-control','menu-composition/popup-mount-control','menu-composition/popup-mount-successor','menu-composition/popup-late-control','menu-composition/popup-final','menu-composition/popup-activation-remainder','menu-composition/popup-activation','menu-composition/popup-activation-control','menu-composition/focus-coordinator-scope-r2','menu-composition/focus-scope-incomplete-control','menu-composition/focus-scope-row-control','menu-composition/focus-coordinator-scope','menu-composition/focus-coordinator-control','menu-composition/focus-handoff-final','menu-composition/focus-handoff','menu-composition/focus-handoff-control','menu-composition/assemble','menu-composition/affected','menu-composition/stale-global','menu-composition/late-browser','menu-composition/native-owner','menu-composition/guide','menu-composition/owner-successor','menu-composition/owner-window-control','menu-composition/review-final','menu-composition/review-control','menu-composition/promise-final','menu-composition/promise-rejected','menu-composition/throw-sync','menu-composition/focus-diagnostic','menu-composition/focus-shadow-control','menu-composition/context-diagnostic','menu-composition/context-leaf-control','menu-composition/context-health-control','menu-composition/native-route-trace','menu-composition/native-route-control','menu-composition/native-route-remainder','menu-composition/activation-observation','menu-composition/activation-control','menu-composition/activation-cleanup-result','menu-composition/short-registration-reassert','menu-composition/short-registration-control','menu-composition/short-registration-home'];
allowedCompositionSelectors.push('menu-composition/missing-receiver');
allowedCompositionSelectors.push('menu-composition/proven-followup');
allowedCompositionSelectors.push('menu-composition/base-registration');
if(compositionSelector!==null)assert(allowedCompositionSelectors.includes(compositionSelector),'UNKNOWN_OR_EMPTY_CASE_SELECTOR');
const outputPath=process.argv[2]?.startsWith('--case=')?undefined:process.argv[2];
const source=fs.readFileSync('native/steam_live.js','utf8'),cases=[];
function fixture(options={}) {
 let menu=options.before??0,overlayMenu=options.overlayBefore??0,now=100000;
 class ClockDate extends Date {static now(){return now;}}
 const calls=[],keyCalls=[];
 const window={settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>options.changedAccount?43:42}}}};
 if(options.noAccountApi)delete window.settingsStore;
 if(options.accountReadThrows)window.settingsStore.m_CMInterface.steamid.GetAccountID=()=>{throw Error('menu must not read account')};
 function receiver(kind,value){
  keyCalls.push([kind,value===1?'OnHomeButtonPressed':'OnQuickAccessButtonPressed']);
  if(options.throwReceiver)throw Error('receiver-failed');
  if(options.asyncReceiver)return options.asyncReceiver();
  if(options.timeout)return new Promise(()=>{});
  // Model Steam's own receiver policy, not YMCC admission/focus policy.
  if(options.buttonsDisabled||(value===2&&options.keyboard)||options.suppressed||options.ignore||options.navigateInstead)return;
  const current=kind==='main'?menu:overlayMenu,after=current===value?0:value;
  calls.push([kind,value,current!==value]);if(kind==='main')menu=after;else overlayMenu=after;
  if(options.drift)options.changedAccount=true;
 }
 const main={WindowType:0,BHasMenus:()=>!options.desktop,IsAnyVRWindow:()=>!!options.vr,IsOverlayWindow:()=>false,
  MenuStore:{GetOpenSideMenu:()=>menu,get m_cSuppressRequests(){return options.suppressed?1:0}},OnHomeButtonPressed(){return receiver('main',1)},OnQuickAccessButtonPressed(){return receiver('main',2)}};
 const overlay={WindowType:2,BHasMenus:()=>!options.overlayDesktop,IsAnyVRWindow:()=>!!options.overlayVR,IsOverlayWindow:()=>true,
  IsGamepadUIOverlayWindow:()=>!options.desktopOnlyOverlay,
  MenuStore:{GetOpenSideMenu:()=>overlayMenu},OnHomeButtonPressed(){return receiver('overlay',1)},OnQuickAccessButtonPressed(){return receiver('overlay',2)}};
 const desktop={BHasMenus:()=>false};
 window.SteamUIStore={ActiveWindowInstance:options.activeOverlay?overlay:options.focusedDesktop?desktop:options.noActive?null:main,
  WindowStore:{GamepadUIMainWindowInstance:main},GetShowingLockScreen:()=>!!options.locked};
 // Foreground/window/PID helpers are forbidden in the global key path.
 window.SteamUIStore.GetFocusedWindowInstance=()=>{throw Error('OS-focus-like selection must not run')};
 window.SteamUIStore.GetWindowInstanceForPID=()=>{throw Error('PID selection must not run')};
 const overlayLookups=[];
 window.FocusedAppWindowStore={
  GetFocusedOverlayAppID:()=>{if(options.overlayFocusThrows)throw Error('focus-cache-unavailable');return options.gameOverlayFocus?(options.focusApp??123):0},
  GetFocusedOverlayPID:()=>options.gameOverlayFocus?(options.focusPid??901):0,
 };
 window.SteamUIStore.WindowStore.GetOverlayInstance=(app,pid)=>{
  overlayLookups.push([app,pid]);return app===123&&(pid===901||pid===0)&&!options.noGameOverlay?overlay:null;
 };
 // New routing must never create overlay/UI windows or fall back to OS focus.
 window.SteamUIStore.WindowStore.EnsureMainWindowCreated=()=>{throw Error('must not create main')};
 window.SteamUIStore.WindowStore.GetOverlayInstanceWithFallback=()=>{throw Error('must use exact existing overlay')};
 if(options.noOverlayHandler){delete overlay.OnHomeButtonPressed;delete overlay.OnQuickAccessButtonPressed;}
 if(options.noAPI){delete main.OnHomeButtonPressed;delete main.OnQuickAccessButtonPressed;}
 const client={};
 const fn=vm.runInNewContext(source,{window,SteamClient:client,Date:ClockDate,performance:{now:()=>now},setTimeout:(cb,n)=>setTimeout(cb,Math.min(n,10)),clearTimeout,console});
 return{fn,window,client,calls,keyCalls,overlayLookups,overlay,main,advance:ms=>{now+=ms;},get now(){return now;},setMenu:value=>{menu=value;},get menu(){return menu},setOverlayMenu:value=>{overlayMenu=value},get overlayMenu(){return overlayMenu}};
}
async function test(name,body){
 try{await body();cases.push({name,ok:true});}
 catch(error){
  cases.push({name,ok:false,error:String(error.stack??error)});
  if(outputPath){fs.mkdirSync(path.dirname(outputPath),{recursive:true});fs.writeFileSync(outputPath,JSON.stringify({suite:'global Steam functional-key receivers',ok:false,passed:cases.filter(c=>c.ok).length,cases,systemInputInjected:false,realSteamMenuMutated:false,focusManipulated:false},null,2),{flag:'wx'});}
  throw error;
 }
}
function identityFixture({healthy=false,registration}={}) {
 let logical=3301072349,full='14177997780718452736',registered=healthy?full:'2054970',app=healthy?logical:2054970;
 if(registration)({logical,full,registered,app}=registration);
 const f=fixture({gameOverlayFocus:true});
 const info={m_unPID:901,m_nBrowserID:900,m_unAppID:app,m_gameID:registered,m_eBrowserType:0,m_eUIMode:4};
 f.overlay.params={appid:app,gameid:registered,browserInfo:info};f.overlay.Destroy=()=>{f.destroyed=true};
 f.overlay.CompositionStateStore={GetCompositionState:()=>f.overlayMenu?2:0,
  ComputeOverallMinimumRequestedComposition:()=>f.overlayMenu?2:0,GetOverridingComposition:()=>null};
 f.window.SteamUIStore.WindowStore.GetInstanceForPID=id=>id===901?f.overlay:null;
 // Mirror Valve's existing browser-AppID lookup for the optional host witness.
 // These are read-only fixture leaves, not an alternate product reader.
 f.window.SteamUIStore.WindowStore.GetOverlayInstance=(id,pid)=>
  id===app&&(pid===0||pid===901)?f.overlay:undefined;
 f.window.SteamUIStore.WindowStore.GetOverlayInstances=id=>id===app?[f.overlay]:[];
 f.window.SteamUIStore.m_runningAppIDs=[logical];
 f.window.appStore={m_mapApps:new Map([[logical,{appid:logical,gameid:full,app_type:0x40000000,local_per_client_data:{display_status:4}}]])};
 f.rows=[{unPID:901,nBrowserID:900,appID:app,gameID:registered,eBrowserType:0,eUIMode:4}];f.nativeReads=0;f.states=[];
 f.window.overlayStore={m_mapBrowserInfo:new Map([['host',info]])};
 f.client.Overlay={GetOverlayBrowserInfo:async()=>{++f.nativeReads;f.onRead?.();return f.rows},SetOverlayState:(...args)=>f.states.push(args)};
 const original=f.overlay.OnQuickAccessButtonPressed;f.originalQuickReceiver=original;
 f.overlay.OnQuickAccessButtonPressed=function(){const result=original.call(this);f.client.Overlay.SetOverlayState(full,this.CompositionStateStore.GetCompositionState());return result};
 f.proof={version:1,appid:logical,gameid64:full,account:42,gamePid:111,steamPid:222,hostPid:901,browserId:900,gameCreated:'1',steamCreated:'2',hostCreated:'3',registeredAppid:app,registeredGameid64:registered};
 f.send=()=>f.fn({operation:'menu.button',menu:'quick-access',menuGame:f.proof});
 return f;
}
if(compositionSelector!==null){
 const {runCompositionSelection}=await import('./fixtures/steam_menu_composition/cases.mjs');
 const result=await runCompositionSelection({fixture,identityFixture,source,filter:compositionSelector.slice('menu-composition/'.length)});
 if(outputPath){fs.mkdirSync(path.dirname(outputPath),{recursive:true});fs.writeFileSync(outputPath,JSON.stringify(result,null,2),{flag:'wx'});}
 console.log(JSON.stringify(result,null,2));
}else{
for(const requested of ['main','quick-access'])for(const before of [0,1,2])await test(requested+' native key receiver from '+before,async()=>{
 const f=fixture({before}),r=await f.fn({operation:'menu.button',menu:requested,account:42}),target=requested==='main'?1:2;
 assert(r.ok&&r.keyDelivered&&r.runtimeAccepted);assert.equal(f.menu,before===target?0:target);assert.equal(f.keyCalls.length,1);assert.equal(r.menuTarget.keyboardFallback,false);assert(!f.window.__ymccSteamLiveBusy);
});
for(const requested of ['main','quick-access']) {
 await test(requested+' null Steam active context still delivers to its native main receiver',async()=>{
  const f=fixture({noActive:true}),r=await f.fn({operation:'menu.button',menu:requested,account:42});assert(r.ok&&r.keyDelivered);assert.equal(r.menuTarget.source,'steam-big-picture');
 });
 await test(requested+' auxiliary desktop Steam context never becomes an input permission gate',async()=>{
  const f=fixture({focusedDesktop:true}),r=await f.fn({operation:'menu.button',menu:requested,account:42});assert(r.ok&&r.keyDelivered);assert.equal(r.menuTarget.source,'steam-big-picture');
 });
 await test(requested+' uses Steam own active overlay receiver, not a chosen OS window',async()=>{
  const f=fixture({activeOverlay:true}),r=await f.fn({operation:'menu.button',menu:requested,account:42});assert(r.ok&&r.keyDelivered);assert.equal(f.menu,0);assert.equal(f.overlayMenu,requested==='main'?1:2);assert.equal(r.menuTarget.source,'steam-active-context');
 });
 for(const ignoredPid of [0,1212,-1,'arbitrary'])await test(requested+' ignores caller foreground selection '+ignoredPid,async()=>{
  const f=fixture(),r=await f.fn({operation:'menu.button',menu:requested,account:42,foregroundPid:ignoredPid});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);
 });
 for(const flag of ['ignore','suppressed','buttonsDisabled','navigateInstead'])await test(requested+' '+flag+' distinguishes key delivered from menu response',async()=>{
  const f=fixture({[flag]:true}),r=await f.fn({operation:'menu.button',menu:requested,account:42});assert(r.ok&&r.keyDelivered);assert.equal(r.runtimeAccepted,false);assert.equal(r.response,'key-delivered-no-menu-state-change');assert.equal(f.keyCalls.length,1);assert.equal(f.calls.length,0);
 });
 await test(requested+' read-only inspection never invokes a key',async()=>{
  const f=fixture(),r=await f.fn({operation:'menu.inspect',menu:requested,account:42});assert(r.ok&&!r.mutated);assert.equal(f.keyCalls.length,0);
 });
 for(const bad of ['throwReceiver','timeout'])await test(requested+' '+bad+' never retries a possibly delivered key',async()=>{
  const f=fixture({[bad]:true}),r=await f.fn({operation:'menu.button',menu:requested,account:42});assert(!r.ok&&r.mutated);assert.equal(f.keyCalls.length,1);assert(!f.window.__ymccSteamLiveBusy);
 });
}
for(const flag of ['locked','vr'])await test(flag+' Steam safety boundary before mutation',async()=>{const f=fixture({[flag]:true}),r=await f.fn({operation:'menu.button',menu:'main',account:42});assert.equal(r.reason,'steam-menu-blocked');assert(!r.mutated);assert.equal(f.keyCalls.length,0)});
for(const option of [{noAPI:true}])await test('unsupported native receiver '+JSON.stringify(option)+' fails closed without keyboard/focus fallback',async()=>{const f=fixture(option),r=await f.fn({operation:'menu.button',menu:'main',account:42});assert.equal(r.reason,'live-api-unavailable');assert(!r.mutated);assert.equal(f.keyCalls.length,0)});
for(const option of [{desktop:true},{before:99}])await test('receiver delivery does not depend on menu capability/state '+JSON.stringify(option),async()=>{
 const f=fixture(option),r=await f.fn({operation:'menu.button',menu:'main',account:42});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);if(option.before===99){assert.equal(r.runtimeAccepted,false);assert.equal(r.response,'key-delivered-menu-state-unknown');}
});
for(const menu of ['main','quick-access'])for(const account of [43,0,undefined])await test(menu+' functional key ignores registry account '+String(account),async()=>{
 const f=fixture(),r=await f.fn({operation:'menu.button',menu,account});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);assert.equal(f.menu,menu==='main'?1:2);
});
for(const menu of ['main','quick-access'])for(const option of [{noAccountApi:true},{accountReadThrows:true},{drift:true}])await test(menu+' functional key does not require account reader '+JSON.stringify(option),async()=>{
 const f=fixture(option),r=await f.fn({operation:'menu.button',menu,account:43});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);assert(!f.window.__ymccSteamLiveBusy);
});
for(const op of ['menu.inspect','menu.toggle'])for(const menu of ['main','quick-access'])await test(op+' follows current UI without account gate '+menu,async()=>{
 const f=fixture({noAccountApi:true}),r=await f.fn({operation:op,menu,account:0});assert(r.ok);assert.equal(f.keyCalls.length,op==='menu.inspect'?0:1);
});
for(const operation of ['settings.get','settings.set','overlay.get','desktopMouse.set','sleep.inspect','menu.button.extra','menu.qam.unknown'])await test(operation+' retains original account mismatch rejection',async()=>{
 const f=fixture(),r=await f.fn({operation,account:43});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-account-changed');assert.equal(f.keyCalls.length,0);
});
await test('retired gamepad lease rejects before account lookup or dispatch',async()=>{
 const f=fixture(),r=await f.fn({operation:'input.gamepadLease',account:43});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'light-isolation-retired');assert.equal(r.requestDispatched,false);assert.equal(f.keyCalls.length,0);
});
await test('invalid dedicated menu never invokes a receiver',async()=>{const f=fixture(),r=await f.fn({operation:'menu.button',menu:'url',account:42});assert.equal(r.reason,'invalid-steam-menu');assert.equal(f.keyCalls.length,0)});
await test('legacy menu.toggle maps to the same native key receiver',async()=>{const f=fixture(),r=await f.fn({operation:'menu.toggle',menu:'main',account:42});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1)});
await test('production native path has no Windows focus, PID gate, virtual output or keyboard fallback',async()=>{
 const main=fs.readFileSync('native/main.cpp','utf8'),start=main.indexOf('static bool nativeScreenStandaloneSubmit('),end=main.indexOf('// Shared only by shortcut summon routes',start),body=main.slice(start,end);
 for(const forbidden of ['GetForegroundWindow','GetWindowThreadProcessId','foregroundPid','SetForegroundWindow','SetFocus(','AttachThreadInput','sendMenuChord','SendInput','inputHostSubmit','outputTarget','g_frontendButtonPulse.request'])assert(!body.includes(forbidden),forbidden);
 const sharedStart=main.indexOf('static json nativeSteamMenuButton('),shared=main.slice(sharedStart,start);
 assert(sharedStart>=0);assert(main.includes('"operation","menu.button"'));assert(shared.includes('"menu.button"'));assert(body.includes('nativeSteamMenuButton(quick,allowed,followup,coldDeadline,requestId,quick?&origin:nullptr)'));assert(body.includes('keyDelivered'));assert(body.includes('menuStateVerified'));assert(body.includes('focusChangedByYmcc'));assert(body.includes('standaloneCurrent(generation)'));
 const shortcut=fs.readFileSync('native/shortcut_steam_buttons.h','utf8');
 assert(shortcut.includes('nativeSteamMenuButton(quick,allowed,json(),coldDeadline,actionEpoch,quick?&origin:nullptr)'));
 for(const forbidden of ['SendCtrl1','g_frontendButtonPulse.request','focusBeginReturnToPreviousWindow','nativeFrontendButtonTargetEpoch'])assert(!shortcut.includes(forbidden),forbidden);
 const menuStart=source.indexOf("    if (['menu.button', 'menu.toggle', 'menu.inspect'].includes(request.operation)) {");assert(menuStart>=0);
 const branch=source.slice(menuStart,source.indexOf("if (request.operation === 'settings.get'",menuStart));
 for(const forbidden of ['foregroundPid','GetFocusedWindowInstance','GetWindowInstanceForPID','.ToggleSideMenu(','.OpenSideMenu(','SetUIMode','EnsureMainWindowCreated','SetOverlayState(','SetFocus'])assert(!branch.includes(forbidden),forbidden);
 assert(branch.includes('OnHomeButtonPressed'));assert(branch.includes('OnQuickAccessButtonPressed'));
});

// A stale main navigation context must not send an in-game touch key to BPM.
for(const menu of ['main','quick-access'])for(const context of [{},{noActive:true},{focusedDesktop:true},{focusPid:0},{focusPid:0,noActive:true},{focusPid:0,focusedDesktop:true}])await test('overlay route: '+menu+' selects Valve focused game with '+JSON.stringify(context),async()=>{
 const f=fixture({gameOverlayFocus:true,noAccountApi:true,...context}),r=await f.fn({operation:'menu.button',menu});
 assert(r.ok&&r.keyDelivered&&r.runtimeAccepted);assert.equal(f.menu,0);assert.equal(f.overlayMenu,menu==='main'?1:2);
 assert.equal(r.menuTarget.source,'steam-focused-game-overlay');assert.equal(r.menuTarget.isGameOverlay,true);
 assert.deepEqual(f.overlayLookups,[[123,context.focusPid??901]]);assert.deepEqual(f.keyCalls,[['overlay',menu==='main'?'OnHomeButtonPressed':'OnQuickAccessButtonPressed']]);
});
await test('overlay route: inspection uses same game receiver without a toggle',async()=>{
 const f=fixture({gameOverlayFocus:true}),r=await f.fn({operation:'menu.inspect',menu:'quick-access'});
 assert(r.ok&&!r.mutated);assert.equal(r.menuTarget.source,'steam-focused-game-overlay');assert.equal(f.keyCalls.length,0);
});
for(const bad of [{noGameOverlay:true},{desktopOnlyOverlay:true},{noOverlayHandler:true},{overlayFocusThrows:true},{focusApp:0},{focusPid:-1},{focusApp:123.5},{focusPid:0xffffffff+1}])await test('overlay route: unavailable optional game route preserves original receiver '+JSON.stringify(bad),async()=>{
 const f=fixture({gameOverlayFocus:true,...bad}),r=await f.fn({operation:'menu.button',menu:'quick-access'});
 assert(r.ok&&r.keyDelivered);assert.equal(f.menu,2);assert.equal(f.overlayMenu,0);assert.equal(f.keyCalls.length,1);
});
await test('overlay route: active game overlay retains its original receiver',async()=>{
 const f=fixture({gameOverlayFocus:true,activeOverlay:true}),r=await f.fn({operation:'menu.button',menu:'quick-access'});
 assert(r.ok&&r.keyDelivered);assert.equal(r.menuTarget.source,'steam-active-context');assert.equal(f.overlayMenu,2);assert.equal(f.menu,0);assert.equal(f.overlayLookups.length,0);
});
for(const guard of ['locked','overlayVR'])await test('overlay route: '+guard+' keeps original Steam safety boundary',async()=>{
 const f=fixture({gameOverlayFocus:true,[guard]:true}),r=await f.fn({operation:'menu.button',menu:'quick-access'});
 assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-menu-blocked');assert.equal(f.keyCalls.length,0);
});
for(const focusPid of [0,901])await test('overlay route: normal game QAM success and close use one game toggle each PID='+focusPid,async()=>{
 const f=fixture({gameOverlayFocus:true,focusPid}),t={id:'game-open',startedAt:f.now};let r=await f.fn({operation:'menu.button',menu:'quick-access',qamFollowup:t});
 assert(r.ok&&r.qamRecovery.terminal);assert.equal(f.overlayMenu,2);f.advance(750);
 r=await f.fn({operation:'menu.qam.retry',qamFollowup:t});assert(!r.mutated);assert.equal(f.keyCalls.length,1);
 f.advance(1);const close={id:'game-close',startedAt:f.now};r=await f.fn({operation:'menu.button',menu:'quick-access',qamFollowup:close});
 assert(r.ok&&r.qamRecovery.terminal);assert.equal(f.overlayMenu,0);f.advance(750);
 r=await f.fn({operation:'menu.qam.retry',qamFollowup:close});assert(!r.mutated);assert.equal(f.keyCalls.length,2);assert.equal(f.menu,0);
});
for(const focusPid of [0,901])await test('overlay route: game followup keeps the game receiver through stale main context PID='+focusPid,async()=>{
 const options={gameOverlayFocus:true,ignore:true,focusPid},f=fixture(options),t={id:'game-followup',startedAt:f.now};await f.fn({operation:'menu.button',menu:'quick-access',qamFollowup:t});
 f.advance(750);options.ignore=false;const r=await f.fn({operation:'menu.qam.retry',qamFollowup:t});
 assert(r.ok&&r.mutated&&r.qamRecovery.terminal);assert.equal(f.overlayMenu,2);assert.equal(f.menu,0);assert.equal(f.keyCalls.length,2);assert(f.keyCalls.every(c=>c[0]==='overlay'));
});
for(const focusPid of [0,901])await test('overlay route: game focus changes cancel retry instead of toggling main menu PID='+focusPid,async()=>{
 const options={gameOverlayFocus:true,ignore:true,focusPid},f=fixture(options),t={id:'game-away',startedAt:f.now};await f.fn({operation:'menu.button',menu:'quick-access',qamFollowup:t});
 options.gameOverlayFocus=false;f.advance(750);const r=await f.fn({operation:'menu.qam.retry',qamFollowup:t});
 assert(!r.mutated&&r.qamRecovery.terminal);assert.equal(r.qamRecovery.status,'context-changed');assert.equal(f.keyCalls.length,1);assert.equal(f.menu,0);
});
// The same production bridge, deterministic click clock; no real Steam/CDP/input.
const ticket=f=>({id:'fixture-click',startedAt:f.now});
const qam=(f,phase,t)=>f.fn({operation:'menu.qam.'+phase,account:42,qamFollowup:t});
await test('route diagnostics: signed cached app keeps its branch without logging its identity',async()=>{
 for(const [focusApp,kind] of [[-835934955,'signed'],[0,'zero'],[undefined,'missing'],['123','other-type'],[NaN,'invalid-number']]){
 const f=fixture({gameOverlayFocus:true});
 // Override just the existing primitive read; the receiver path remains global.
 f.window.FocusedAppWindowStore.GetFocusedOverlayAppID=()=>focusApp;
 const r=await f.fn({operation:'menu.button',menu:'quick-access'});
 assert(r.ok&&r.keyDelivered);assert.equal(f.menu,2);assert.equal(f.overlayMenu,0);
 assert.equal(r.menuTarget.routing.version,3);assert.equal('focusedOverlayAppID'in r.menuTarget.routing,false);
 assert.equal(r.menuTarget.routing.lookup,'focused-app-invalid');assert.equal(f.overlayLookups.length,0);
 assert.equal(r.menuTarget.routing.focusValueKind,kind);
 assert.deepEqual(Object.keys(r.menuTarget.routing).sort(),['focusValueKind','lookup','version']);
 assert.equal(f.keyCalls.length,1);
 }
});
await test('route diagnostics: PID zero keeps exact selection without diagnostic identity reads',async()=>{
 const f=fixture({gameOverlayFocus:true,focusPid:0});f.overlay.params={appid:123,browserInfo:{m_unPID:901,m_nBrowserID:7}};
 const r=await f.fn({operation:'menu.button',menu:'quick-access'});assert(r.ok&&r.keyDelivered);
 assert.equal(r.menuTarget.routing.lookup,'game-overlay-selected');assert.equal('focusedOverlayPID'in r.menuTarget.routing,false);
 assert.deepEqual(Object.keys(r.menuTarget.routing).sort(),['lookup','version']);
 assert.deepEqual(f.overlayLookups,[[123,0]]);
});
await test('route diagnostics: throwing optional inventory is never accessed',async()=>{
 const f=fixture();let reads=0;Object.defineProperty(f.window.SteamUIStore.WindowStore,'m_mapOverlayPopupByPID',{get(){reads++;throw Error('inventory unavailable')}});
 const r=await f.fn({operation:'menu.button',menu:'quick-access'});assert(r.ok&&r.keyDelivered);assert.equal(f.menu,2);
 assert.equal(reads,0);assert.equal(r.menuTarget.routing.lookup,'focused-app-invalid');assert.equal(f.keyCalls.length,1);
});
await test('route diagnostics: cached window metadata is never accessed',async()=>{
 const f=fixture({gameOverlayFocus:true,focusPid:0});let reads=0;Object.defineProperty(f.overlay,'params',{get(){reads++;throw Error('optional fact unavailable')}});
 const r=await f.fn({operation:'menu.button',menu:'quick-access'});assert(r.ok&&r.keyDelivered);assert.equal(f.overlayMenu,2);assert.equal(f.menu,0);
 assert.equal(reads,0);assert.equal(r.menuTarget.routing.lookup,'game-overlay-selected');
});
await test('route diagnostics: existing overlay inventory is not enumerated or returned',async()=>{
 const f=fixture();let reads=0;const ws=f.window.SteamUIStore.WindowStore;
 ws.m_mapOverlayPopupByPID={size:1000,*values(){for(let i=0;i<1000;i++){reads++;yield {WindowType:1,params:{appid:i+1,browserInfo:{m_unPID:i+1}}}}}};
 const r=await f.fn({operation:'menu.button',menu:'quick-access'});assert(r.ok&&r.keyDelivered);assert.equal(f.menu,2);assert.equal(f.overlayMenu,0);
 assert.equal('existingOverlays'in r.menuTarget.routing,false);assert.deepEqual(Object.keys(r.menuTarget.routing).sort(),['focusValueKind','lookup','version']);
 assert.equal(reads,0);assert.equal(f.overlayLookups.length,0);assert(!JSON.stringify(r.menuTarget.routing).includes('steamid'));
});
const original=(f,t)=>f.fn({operation:'menu.button',menu:'quick-access',account:42,qamFollowup:t});
await test('follow-up: immediate normal success stays exactly one call',async()=>{
 const f=fixture(),t=ticket(f),r=await original(f,t);assert(r.ok&&r.keyDelivered&&r.runtimeAccepted);assert(r.qamRecovery.terminal);
 f.advance(750);const r2=await qam(f,'retry',t);assert(r2.qamRecovery.terminal);assert(!r2.mutated);assert.equal(f.keyCalls.length,1);
});
await test('follow-up: a close request never reopens the menu',async()=>{
 const f=fixture({before:2}),t=ticket(f),r=await original(f,t);assert.equal(r.qamRecovery.status,'close-request');
 f.advance(750);assert.equal((await qam(f,'retry',t)).qamRecovery.status,'close-request');assert.equal(f.menu,0);assert.equal(f.keyCalls.length,1);
});
for(const succeedsAt of [750,1500,2250])await test('follow-up: recovery at '+succeedsAt+'ms stops immediately',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await original(f,t);
 for(let at=750;at<=2250;at+=750){f.advance(750);if(at===succeedsAt)options.ignore=false;const r=await qam(f,'retry',t);assert(r.ok);if(at>=succeedsAt)assert(r.qamRecovery.terminal);}
 assert.equal(f.menu,2);assert.equal(f.keyCalls.length,1+succeedsAt/750);
});
await test('follow-up: maximum three uniform supplements, none at 3000ms',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await original(f,t);
 for(let i=1;i<=3;i++){f.advance(750);const r=await qam(f,'retry',t);assert(r.mutated&&r.qamRecovery.supplementSent);assert.equal(r.qamRecovery.attempts,i);}
 f.advance(750);const r=await qam(f,'retry',t);assert.equal(r.qamRecovery.status,'expired');assert(!r.mutated);assert.equal(f.keyCalls.length,4);
});
await test('follow-up: queued/duplicate slots cannot bunch inputs',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await original(f,t);f.advance(1490);await qam(f,'retry',t);
 f.advance(10);const skipped=await qam(f,'retry',t);assert(!skipped.mutated);assert.equal(skipped.qamRecovery.status,'waiting-spacing');
 f.advance(750);await qam(f,'retry',t);await qam(f,'retry',t);assert.equal(f.keyCalls.length,3);
});
await test('follow-up: SD observation never emits another virtual or API key',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await qam(f,'prepare');
 f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();await qam(f,'observe',t);assert.equal(f.keyCalls.length,1);
 f.advance(750);options.ignore=false;const r=await qam(f,'retry',t);assert(r.mutated);assert.equal(f.keyCalls.length,2);assert.equal(f.menu,2);
});
await test('follow-up: a skipped initial observation can bind the recorded SD receiver',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await qam(f,'prepare');f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();
 f.advance(750);options.ignore=false;const r=await qam(f,'retry',t);assert(r.mutated);assert.equal(f.menu,2);
});
await test('follow-up: missing/late SD recorder never guesses original intent',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();await qam(f,'observe',t);
 for(let i=0;i<4;i++){f.advance(750);assert(!(await qam(f,'retry',t)).mutated);}assert.equal(f.keyCalls.length,1);
});
await test('follow-up: a newer user QAM event cancels the old ticket',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await original(f,t);f.advance(100);f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();
 f.advance(650);const r=await qam(f,'retry',t);assert.equal(r.qamRecovery.status,'newer-user-input');assert(!r.mutated);assert.equal(f.keyCalls.length,2);
});
for(const flag of ['suppressed','locked','vr'])await test('follow-up: '+flag+' cannot emit a supplement',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await original(f,t);options[flag]=true;f.advance(750);const r=await qam(f,'retry',t);assert(!r.mutated);assert.equal(f.keyCalls.length,1);
});
await test('follow-up: account changes do not block an admitted menu ticket',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await original(f,t);options.changedAccount=true;f.advance(750);
 const r=await f.fn({operation:'menu.qam.retry',account:0,qamFollowup:t});assert(r.ok&&r.mutated&&r.qamRecovery.supplementSent);assert.equal(f.keyCalls.length,2);
});
await test('follow-up: prepare observe retry dispose work without account API',async()=>{
 const options={noAccountApi:true,ignore:true},f=fixture(options),t=ticket(f);assert((await qam(f,'prepare')).ok);
 f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();assert((await qam(f,'observe',t)).ok);f.advance(750);options.ignore=false;
 const r=await qam(f,'retry',t);assert(r.ok&&r.mutated);assert.equal(f.menu,2);assert((await qam(f,'dispose')).ok);assert(!f.window.__ymccQamFollowup);
});
await test('follow-up: context replacement revokes the ticket',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await original(f,t);f.window.SteamUIStore.ActiveWindowInstance={MenuStore:{GetOpenSideMenu:()=>0},OnQuickAccessButtonPressed(){throw Error('must not replay into new context')}};
 f.advance(750);const r=await qam(f,'retry',t);assert(!r.mutated);assert.equal(r.qamRecovery.status,'context-changed');
});
for(const which of ['menu','home','route'])await test('follow-up: other '+which+' navigation cancels recovery',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await original(f,t);const w=f.window.SteamUIStore.ActiveWindowInstance;
 if(which==='menu')f.setMenu(1);else if(which==='home')w.m_flLastHomePressMS=900;else w.m_history={location:{pathname:'/new'}};
 f.advance(750);const r=await qam(f,'retry',t);assert.equal(r.qamRecovery.status,'other-navigation');assert(!r.mutated);
});
await test('follow-up: lost menu state is terminal and never inhibits the original key',async()=>{
 const f=fixture({before:99,ignore:true}),t=ticket(f),r=await original(f,t);assert(r.keyDelivered);assert.equal(r.qamRecovery.status,'menu-state-unknown');
 f.advance(750);assert(!(await qam(f,'retry',t)).mutated);assert.equal(f.keyCalls.length,1);
});
await test('follow-up: pending SD receiver is observed without waiting or retrying it',async()=>{
 let resolve;const options={asyncReceiver:()=>new Promise(r=>{resolve=r;})},f=fixture(options),t=ticket(f);await qam(f,'prepare');f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();await qam(f,'observe',t);
 f.advance(750);const r=await qam(f,'retry',t);assert(!r.mutated);assert.equal(f.keyCalls.length,1);f.setMenu(2);resolve();await new Promise(setImmediate);f.setMenu(0);
 f.advance(750);assert.equal((await qam(f,'retry',t)).qamRecovery.status,'menu-was-opened');assert.equal(f.keyCalls.length,1);
});
await test('follow-up: rejected asynchronous receiver is terminal',async()=>{
 let reject;const options={asyncReceiver:()=>new Promise((_,r)=>{reject=r;})},f=fixture(options),t=ticket(f);await qam(f,'prepare');f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();await qam(f,'observe',t);reject(Error('failed'));await new Promise(setImmediate);
 f.advance(750);assert.equal((await qam(f,'retry',t)).qamRecovery.status,'receiver-failed');assert.equal(f.keyCalls.length,1);
});
await test('follow-up: a throwing supplement reports possible mutation and stops',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await original(f,t);options.throwReceiver=true;f.advance(750);const r=await qam(f,'retry',t);assert(!r.ok&&r.mutated);assert.equal(f.keyCalls.length,2);
});
await test('follow-up: disposal restores ownership without input or delayed retry',async()=>{
 const f=fixture({ignore:true}),w=f.window.SteamUIStore.ActiveWindowInstance,old=w.OnQuickAccessButtonPressed,t=ticket(f);await original(f,t);assert.notEqual(w.OnQuickAccessButtonPressed,old);
 const r=await qam(f,'dispose');assert(r.ok&&!r.mutated);assert.equal(w.OnQuickAccessButtonPressed,old);assert(!f.window.__ymccQamFollowup);await new Promise(r=>setTimeout(r,20));assert.equal(f.keyCalls.length,1);
});
await test('follow-up: disposal preserves a newer foreign receiver',async()=>{
 const f=fixture(),w=f.window.SteamUIStore.ActiveWindowInstance;await qam(f,'prepare');const foreign=()=>{};w.OnQuickAccessButtonPressed=foreign;await qam(f,'dispose');assert.equal(w.OnQuickAccessButtonPressed,foreign);
});
await test('follow-up: immutable receiver leaves the normal key functional',async()=>{
 const f=fixture(),w=f.window.SteamUIStore.ActiveWindowInstance;Object.defineProperty(w,'OnQuickAccessButtonPressed',{value:w.OnQuickAccessButtonPressed,writable:false,configurable:false});
 const r=await original(f,ticket(f));assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);assert.equal(r.qamRecovery.status,'receiver-not-writable');
});
await test('follow-up: Steam is absent, original fails without input or a fallback',async()=>{
 const f=fixture({noAPI:true}),t=ticket(f),r=await original(f,t);assert(!r.ok&&!r.mutated);assert.equal(f.keyCalls.length,0);
 f.advance(750);const retry=await qam(f,'retry',t);assert(!retry.mutated&&retry.qamRecovery.terminal);assert.equal(retry.qamRecovery.status,'receiver-unavailable');assert.equal(f.keyCalls.length,0);
});
await test('follow-up: Steam closes during the three-second window',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await original(f,t);delete f.window.SteamUIStore;
 f.advance(750);const r=await qam(f,'retry',t);assert(!r.mutated&&r.qamRecovery.terminal);assert.equal(f.keyCalls.length,1);
});
await test('follow-up: failed recorder metadata never blocks the original receiver',async()=>{
 const f=fixture(),w=f.window.SteamUIStore.ActiveWindowInstance;Object.defineProperty(w,'m_history',{get(){throw Error('metadata unavailable')}});
 const r=await original(f,ticket(f));assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);assert.equal(f.menu,2);
});
await test('follow-up: failed recorder setup never blocks the original API key',async()=>{
 const f=fixture(),w=f.window.SteamUIStore.ActiveWindowInstance;f.window.SteamUIStore.ActiveWindowInstance=new Proxy(w,{getOwnPropertyDescriptor(){throw Error('not inspectable')}});
 const r=await original(f,ticket(f));assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);assert.equal(r.qamRecovery.status,'recorder-unavailable');
});
await test('follow-up bridge never creates a timer, changes menu flags or sends OS input',async()=>{
 const body=source.slice(source.indexOf('    const qamFollowup ='),source.indexOf("    if (['menu.button'"));
 for(const forbidden of ['setTimeout(','setInterval(','ToggleSideMenu(','OpenSideMenu(','SendInput','SetForegroundWindow','m_eOpenSideMenu='])assert(!body.includes(forbidden),forbidden);
});

await test('follow-up: delayed first observation cannot adopt a newer user event',async()=>{
 const f=fixture({ignore:true}),t=ticket(f);await qam(f,'prepare');f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();
 f.advance(100);f.window.SteamUIStore.ActiveWindowInstance.OnQuickAccessButtonPressed();f.advance(650);
 const r=await qam(f,'retry',t);assert.equal(r.qamRecovery.status,'newer-user-input');assert(!r.mutated);assert.equal(f.keyCalls.length,2);
});
await test('follow-up: a delayed old ticket cannot replace a newer observed ticket',async()=>{
 const f=fixture({ignore:true}),old=ticket(f);await original(f,old);f.advance(100);const next={id:'newer-click',startedAt:f.now};await original(f,next);f.advance(650);
 const r=await qam(f,'retry',old);assert.equal(r.qamRecovery.status,'superseded');assert(!r.mutated);assert.equal(f.keyCalls.length,2);assert.equal(f.window.__ymccQamFollowup.ticket.id,next.id);
});

await test('follow-up: ambiguous same-millisecond tickets preserve both normal keys without supplementation',async()=>{
 const f=fixture({ignore:true}),old=ticket(f);await original(f,old);const next={id:'same-clock-new-key',startedAt:f.now},r=await original(f,next);
 assert(r.ok&&r.keyDelivered);assert.equal(r.qamRecovery.status,'superseded');f.advance(750);assert(!(await qam(f,'retry',old)).mutated);assert.equal(f.keyCalls.length,2);
});
await test('follow-up: upgrading a passive v1 recorder restores it before installing v2',async()=>{
 const f=fixture(),w=f.window.SteamUIStore.ActiveWindowInstance,normal=w.OnQuickAccessButtonPressed;let disposed=0;
 const legacy=function(...args){return normal.apply(this,args)};w.OnQuickAccessButtonPressed=legacy;f.window.__ymccQamFollowup={version:1,account:42,store:f.window.SteamUIStore,dispose(){++disposed;if(w.OnQuickAccessButtonPressed===legacy)w.OnQuickAccessButtonPressed=normal;}};
 const r=await original(f,ticket(f));assert(r.ok&&r.keyDelivered);assert.equal(disposed,1);assert.equal(f.window.__ymccQamFollowup.version,2);assert.equal(f.keyCalls.length,1);
});
// The same receiver fixture now covers the optional native host witness. Real
// Valve method/GPU evidence is separate; these endpoints perform no app control.
await test('game identity: native registration alias follows original open and close without changing params',async()=>{
 const f=identityFixture(),params=f.overlay.params,api=f.client.Overlay.SetOverlayState;
 assert((await f.send()).ok);assert.equal(f.overlayMenu,2);assert.equal(f.menu,0);assert.deepEqual(f.states,[[f.proof.gameid64,2],['2054970',2]]);
 assert((await f.send()).ok);assert.equal(f.overlayMenu,0);assert.deepEqual(f.states.slice(-2),[[f.proof.gameid64,0],['2054970',0]]);
 assert.equal(f.overlay.params,params);assert.equal(f.client.Overlay.SetOverlayState,api);assert(!f.window.__ymccMenuGameAlias);
});
await test('game identity: healthy complete ID adds no alias or duplicate native state',async()=>{
 const f=identityFixture({healthy:true});assert((await f.send()).ok);assert.equal(f.states.length,1);assert(!f.window.__ymccMenuGameAlias);
});
for(const [name,change]of [
 ['different host',f=>++f.proof.hostPid],['different browser',f=>++f.proof.browserId],
 ['duplicate registration',f=>f.rows.push({...f.rows[0],unPID:902})],
 ['duplicate cached host',f=>f.window.overlayStore.m_mapBrowserInfo.set('other',{...f.overlay.params.browserInfo,m_unPID:902})],
 ['changed account',f=>f.onRead=()=>{f.window.settingsStore.m_CMInterface.steamid={GetAccountID:()=>43}}],
 ['retired overview',f=>f.onRead=()=>{f.window.appStore.m_mapApps.clear()}],
])await test('game identity: '+name+' refuses before any key or alias',async()=>{
 const f=identityFixture();change(f);const r=await f.send();assert(!r.ok);assert.equal(f.keyCalls.length,0);assert.equal(f.states.length,0);assert.equal(f.menu,0);
});
await test('game identity: OFF disposes only owned hooks and does not replay a key',async()=>{
 const f=identityFixture(),api=f.client.Overlay.SetOverlayState;assert((await f.send()).ok);const keys=f.keyCalls.length;
 const r=await f.fn({operation:'menu.qam.dispose',menuGameDisposeOnly:true});assert(r.ok);assert.equal(f.keyCalls.length,keys);assert.deepEqual(f.states.at(-1),['2054970',0]);assert.equal(f.client.Overlay.SetOverlayState,api);
});
await test('game identity: overlay Destroy releases alias and preserves original destroy',async()=>{
 const f=identityFixture();assert((await f.send()).ok);f.overlay.Destroy();assert(f.destroyed);assert(!f.window.__ymccMenuGameAlias);assert.deepEqual(f.states.at(-1),['2054970',0]);
});
await test('game identity: another window sharing the logical ID cannot close this window',async()=>{
 const f=identityFixture();assert((await f.send()).ok);const before=f.states.length;
 f.client.Overlay.SetOverlayState(f.proof.gameid64,0);
 assert.equal(f.states.length,before+1);assert.deepEqual(f.states.at(-1),[f.proof.gameid64,0]);
 assert.equal(f.window.__ymccMenuGameAlias.lastState,2);assert.equal(f.overlayMenu,2);
 assert((await f.send()).ok);assert.deepEqual(f.states.at(-1),['2054970',0]);assert(!f.window.__ymccMenuGameAlias);
});
const result={suite:'global Steam functional-key receivers',ok:true,passed:cases.length,cases,virtualTargetStarted:false,systemInputInjected:false,realSteamMenuMutated:false,focusManipulated:false};
if(outputPath){fs.mkdirSync(path.dirname(outputPath),{recursive:true});fs.writeFileSync(outputPath,JSON.stringify(result,null,2),{flag:'wx'})}console.log(JSON.stringify(result,null,2));

}
