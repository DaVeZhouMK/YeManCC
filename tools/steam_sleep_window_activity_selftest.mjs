import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
const arg=name=>{const i=process.argv.indexOf(name);return i<0?null:process.argv[i+1]};
const source=fs.readFileSync('native/steam_live.js','utf8');
const baselinePath=arg('--baseline'),baseline=baselinePath?fs.readFileSync(baselinePath,'utf8'):null;
const results=[];
async function test(name,body){await body();results.push({name,passed:true})}
const appid=2399932135,gameid='10307630032478011392';
function fixture(code=source,options={}){
  const stats={raises:0,listeners:0,lazyStores:0,diagnosticReads:0};
  const browser={closed:options.closed??false,document:{visibilityState:options.visibility??'hidden',hasFocus:()=>options.focused??false}};
  const nav={RootWindow:browser,BIsActive:()=>true};
  const active={BrowserWindow:browser,LocationPathName:'/apprunning',params:{appid:appid|0,gameid,browserInfo:{m_unAppID:appid,m_unPID:11960}},
    MenuStore:{GetOpenSideMenu:()=>options.menu??0,m_cSuppressRequests:options.suppressed?1:0},
    IsAnyVRWindow:()=>options.vr??false,IsGamepadUIOverlayWindow:()=>options.overlay??true,
    IsDesktopOverlayWindow:()=>false,IsControllerConfiguratorWindow:()=>options.configurator??false};
  Object.defineProperty(active,'DesktopOverlay',{get(){stats.lazyStores++;throw Error('lazy Overlay store must not be constructed')}});
  const ui={m_runningAppIDs:[],WindowStore:{GetAppWindowIDs:()=>[],BHasStandaloneKeyboard:()=>options.keyboard??false},
    NavigationManager:{GetActiveContext:()=>nav},GetFocusedWindowInstance:()=>active,GetShowingLockScreen:()=>options.lock??false};
  // Verified plain data fields refer to the same existing navigation/window caches.
  ui.m_WindowStore=ui.WindowStore;ui.m_GamepadNavigationManager=ui.NavigationManager;
  ui.m_WindowStore.m_mapOverlayPopupByPID=new Map();ui.m_WindowStore.m_mapDesiredWindowInstances=new Map([[1,active]]);
  const window={SteamUIStore:ui,appStore:{m_mapApps:new Map([[appid,{appid,app_type:0x40000000,gameid,local_per_client_data:{display_status:11}}]])},
    settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>42}},GetClientSetting:()=>[true]},
    ControllerStore:{GetControllers:()=>[{nControllerIndex:15,eControllerType:2}]},
    FocusedAppWindowStore:{GetFocusedAppID:()=>413080,GetFocusedWindowID:()=>null,GetFocusedOverlayAppID:()=>0,GetFocusedOverlayPID:()=>0},
    controllerConfiguratorStore:{EditingConfigurationAppId:0,EditedConfiguration:null,PreviewedConfiguration:null,IsUpdatingEditingConfiguration:false,m_nEditNumber:0,m_nLastSavedEditNumber:0}};
  const SteamClient={Apps:{RaiseWindowForGame(){stats.raises++;return Promise.resolve()}},Overlay:{RegisterForOverlayActivated(){stats.listeners++;throw Error('no new Overlay listener')}},Input:{}};
  const sandbox={window,SteamClient,performance,setTimeout,clearTimeout,console};
  const fn=vm.runInNewContext(code,sandbox);
  return {fn,window,ui,active,nav,browser,stats,sandbox};
}
const inspect=async f=>f.fn({operation:'sleep.inspect',account:42,appids:[appid]});
const attempt=async f=>f.fn({operation:'sleep.input-resume',account:42,appids:[appid],originalWindowReturn:true,originalTarget:{basis:'captured-original-tracker',appid,pid:11960,created:'134364217869743774'}});
const guards=i=>Object.fromEntries(['available','idle','lockScreen','keyboard','overlayOrVrWindow','menuOpen','menuSuppressed','windowInstanceObserved','windowKindProbesComplete','focusEvidence'].map(k=>[k,i[k]]));
await test('hidden retained Overlay has explicit diagnostics and remains blocked without a RETURN',async()=>{
  const f=fixture(),r=await inspect(f),i=r.inputContext.interactive,a=i.windowActivity;
  assert(r.ok&&!r.mutated&&!i.idle&&i.overlayOrVrWindow);assert.equal(a.documentVisibility,'hidden');assert.equal(a.documentFocused,false);assert.equal(a.navigationActive,true);assert.equal(a.rootMatchesBrowser,true);
  assert.equal(a.windowAppid,appid);assert.equal(a.browserPid,11960);assert.equal(a.gameidString,gameid);assert.equal(a.routeKind,'app-running');assert.equal(a.authority,'diagnostic-only-no-admission');
  const action=await attempt(f);assert.equal(action.reason,'steam-interactive-ui-active');assert(!action.mutated);assert.equal(f.stats.raises+f.stats.listeners+f.stats.lazyStores,0);
});
await test('visible focused Overlay, different route and dirty editor still cannot authorize restoration',async()=>{
  const f=fixture(source,{visibility:'visible',focused:true});f.active.LocationPathName='/controllerconfigurator/private-id';
  const r=await inspect(f),a=r.inputContext.interactive.windowActivity;assert.equal(a.documentFocused,true);assert.equal(a.documentVisibility,'visible');assert.equal(a.routeKind,'other');assert(!JSON.stringify(a).includes('private-id'));
  assert.equal((await attempt(f)).reason,'steam-interactive-ui-active');f.window.controllerConfiguratorStore.m_nEditNumber=1;assert.equal((await attempt(f)).reason,'steam-input-editor-busy-or-unknown');assert.equal(f.stats.raises,0);
});
await test('unavailable or throwing DOM/cache probes stay unknown, do not erase independent guard results',async()=>{
  const f=fixture();for(const key of ['LocationPathName','params','m_DesktopOverlay'])Object.defineProperty(f.active,key,{get(){throw Error('unavailable')},configurable:true});
  Object.defineProperty(f.browser,'document',{get(){throw Error('unavailable')},configurable:true});
  Object.defineProperty(f.ui,'NavigationManager',{get(){throw Error('unavailable')},configurable:true});
  const r=await inspect(f),i=r.inputContext.interactive,a=i.windowActivity;assert(r.ok&&i.available&&!i.idle);assert.equal(a.documentVisibility,'unknown');assert.equal(a.documentFocused,null);assert.equal(a.rootMatchesBrowser,true);assert.equal(a.desktopOverlayActive,null);assert.equal(a.gameidString,null);assert.equal(f.stats.raises,0);
});
await test('wrong types are unknown; an existing desktop cache is observed without its lazy getter',async()=>{
  const f=fixture();f.active.m_DesktopOverlay={overlay_active:false};f.browser.document.visibilityState='prerender';f.browser.document.hasFocus=()=>1;f.nav.BIsActive=()=>1;
  let a=(await inspect(f)).inputContext.interactive.windowActivity;assert.equal(a.desktopOverlayActive,false);assert.equal(a.documentVisibility,'unknown');assert.equal(a.documentFocused,null);assert.equal(a.navigationActive,null);assert.equal(f.stats.lazyStores,0);
  f.active.m_DesktopOverlay.overlay_active=true;a=(await inspect(f)).inputContext.interactive.windowActivity;assert.equal(a.desktopOverlayActive,true);assert.equal(f.stats.lazyStores,0);
});
await test('navigation root mismatch or missing browser cannot establish an existing-window match',async()=>{
  const f=fixture();f.nav.RootWindow={};let i=(await inspect(f)).inputContext.interactive;assert(!i.available&&!i.idle);assert.equal(i.reason,'steam-passive-window-cache-no-match');
  f.active.BrowserWindow=null;i=(await inspect(f)).inputContext.interactive;assert(!i.available&&!i.idle);assert.equal(i.reason,'steam-passive-window-cache-no-match');
  assert.equal((await attempt(f)).reason,'steam-interactive-ui-active');assert.equal(f.stats.raises,0);
});
await test('throwing BrowserWindow identity refuses passive matching and keeps the actual overlay veto',async()=>{
  const f=fixture();Object.defineProperty(f.active,'BrowserWindow',{get(){throw Error('unavailable')},configurable:true});
  const i=(await inspect(f)).inputContext.interactive;assert(!i.available&&!i.idle);assert.equal(i.reason,'steam-passive-window-cache-read-failed');
  assert.equal((await attempt(f)).reason,'steam-interactive-ui-active');assert.equal(f.stats.raises,0);
});
await test('diagnostic values cannot turn a real keyboard/menu/lock/VR/configurator veto into idle',async()=>{
  for(const options of [{keyboard:true},{menu:1},{lock:true},{vr:true},{configurator:true},{suppressed:true}]){
    const f=fixture(source,{overlay:false,...options}),r=await inspect(f);assert(r.inputContext.interactive.available&&!r.inputContext.interactive.idle);assert.equal((await attempt(f)).reason,'steam-interactive-ui-active');assert.equal(f.stats.raises,0);
  }
});
if(baseline)await test('all 64 original interaction guard combinations match the previous production code',async()=>{
  for(let mask=0;mask<64;mask++){
    const o={overlay:!!(mask&1),keyboard:!!(mask&2),lock:!!(mask&4),menu:mask&8?1:0,suppressed:!!(mask&16),vr:!!(mask&32)};
    const before=await inspect(fixture(baseline,o)),after=await inspect(fixture(source,o));
    assert.deepEqual(guards(after.inputContext.interactive),guards(before.inputContext.interactive),'guard changed at '+mask);
  }
});
const valvePath=arg('--valve-source');let valveReceipt=null;
if(valvePath){
  const data=fs.readFileSync(valvePath),sha=crypto.createHash('sha256').update(data).digest('hex');
  assert.equal(sha,'5a28c25041d07e69cf591a414519e0cf970b6884c1a0106659dcd9f1fb9586f9');const text=data.toString('utf8');
  const fragments={};
  // Definitions have a '{' immediately following their parameter list. A name
  // may occur in calls too, so select the unique method signature explicitly.
  function definition(name,signature){const needle=name+signature+'{',at=text.indexOf(needle);assert(at>=0&&text.indexOf(needle,at+1)<0);const replaced=text.slice(at);let depth=0,quote=null,end=-1;
    for(let i=needle.length-1;i<replaced.length;i++){const c=replaced[i];if(quote){if(c==='\\'){i++;continue}if(c===quote)quote=null;continue}if(c==='"'||c==="'"||c==='\x60'){quote=c;continue}if(c==='{')depth++;if(c==='}'&&!--depth){end=i+1;break}}assert(end>0);
    const raw=replaced.slice(0,end);fragments[name]={offset:at,length:raw.length,sha256:crypto.createHash('sha256').update(raw).digest('hex')};return raw;
  }
  const names={FindAnActiveContext:'()',GetActiveContext:'()',GetFocusedWindowInstance:'()',GetWindowInstanceFromWindow:'(Zt)',IsGamepadUIOverlayWindow:'()',IsDesktopOverlayWindow:'()',IsAnyVRWindow:'()',OnOverlayActivated:'(Zt,Nt,Er,ni)'};
  const methods=Object.fromEntries(Object.entries(names).map(([n,s])=>[n,definition(n,s)]));
  await test('real Valve fallback retains a hidden unfocused Overlay navigation context; production logs it without granting authority',async()=>{
    const f=fixture(),last={RootWindow:f.browser,FindNavTreeInFocusedWindow:()=>undefined,LogName:()=> 'retained-overlay',BIsActive:()=>true,OnActivate(){this.activated=true}};
    const manager=vm.runInNewContext('({'+methods.FindAnActiveContext+','+methods.GetActiveContext+'})',{pt(){},console:{warn(){}}});
    Object.assign(manager,{m_ActiveContext:null,m_LastActiveContext:last,m_rgAllContexts:[last],BCanActivateContext:()=>true});assert.equal(manager.FindAnActiveContext(),last);assert(last.activated&&!f.browser.document.hasFocus());
    const w={W7:{OverlayGamepadUI:2,OverlayDesktopUI:3,MainVRGamepadUI:4,OverlayVRGamepadUI:5}};
    Object.assign(f.active,vm.runInNewContext('({'+methods.IsGamepadUIOverlayWindow+','+methods.IsDesktopOverlayWindow+','+methods.IsAnyVRWindow+'})',{w}));f.active.params.eWindowType=2;
    const ws=vm.runInNewContext('({'+methods.GetWindowInstanceFromWindow+'})');ws.SteamUIWindows=[f.active];
    const realUI=vm.runInNewContext('({'+methods.GetFocusedWindowInstance+'})');realUI.m_GamepadNavigationManager=manager;realUI.m_WindowStore=ws;
    f.ui.GetFocusedWindowInstance=realUI.GetFocusedWindowInstance.bind(realUI);f.ui.NavigationManager=manager;
    const r=await inspect(f),a=r.inputContext.interactive.windowActivity;assert(r.ok&&!r.inputContext.interactive.idle);assert.equal(a.documentVisibility,'hidden');assert.equal(a.documentFocused,false);assert.equal(a.rootMatchesBrowser,true);assert.equal((await attempt(f)).reason,'steam-interactive-ui-active');assert.equal(f.stats.raises,0);
  });
  await test('real Valve deactivation leaves Gamepad Overlay type/navigation context; desktop deactivation has a separate cache',async()=>{
    const f=fixture(),windowInstance={...f.active,BIsOverlayPath:()=>true,BRouteMatch:()=>false};let navWrites=0;windowInstance.Navigate=()=>navWrites++;
    const callback=vm.runInNewContext('({'+methods.OnOverlayActivated+'})',{si:{WindowStore:{GetOverlayInstance:()=>windowInstance}},l:{$l2:4},SteamClient:{Input:{ForceConfiguratorFocus(){throw Error('not needed')}}},o:{BV:{GamepadUI:{GameAPIOSK:()=>'/gameapiosk',AppRunning:()=>'/apprunning'}}},Di:{Debug(){}}});
    callback.m_mainInstanceUIMode=4;callback.OnOverlayActivated(11960,appid,false,false);assert(windowInstance.IsGamepadUIOverlayWindow());assert.equal(navWrites,0);assert.equal(f.nav.RootWindow,f.browser);
    let state=null;windowInstance.IsGamepadUIOverlayWindow=()=>false;windowInstance.IsDesktopOverlayWindow=()=>true;windowInstance.DesktopOverlay={SetIsOverlayActive(v){state=v}};callback.OnOverlayActivated(11960,appid,false,false);assert.equal(state,false);
  });
  valveReceipt={sourcePath:valvePath,sha256:sha,fragments,fieldCauseProven:false,gamepadOverlayVisibilityProven:false};
}
const result={schema:1,implementation:'steam-wake-round35-window-activity-evidence',cases:results,guardMatrix:baseline?64:0,valveReceipt,fieldAcceptance:false};
if(arg('--output'))fs.writeFileSync(arg('--output'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
