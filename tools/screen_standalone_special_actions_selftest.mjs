import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
const source=fs.readFileSync('native/steam_live.js','utf8'),cases=[];
function fixture(options={}) {
 let menu=options.before??0,overlayMenu=options.overlayBefore??0,now=100000;
 class ClockDate extends Date {static now(){return now;}}
 const calls=[],keyCalls=[];
 const window={settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>options.changedAccount?43:42}}}};
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
  MenuStore:{GetOpenSideMenu:()=>overlayMenu},OnHomeButtonPressed(){return receiver('overlay',1)},OnQuickAccessButtonPressed(){return receiver('overlay',2)}};
 const desktop={BHasMenus:()=>false};
 window.SteamUIStore={ActiveWindowInstance:options.activeOverlay?overlay:options.focusedDesktop?desktop:options.noActive?null:main,
  WindowStore:{GamepadUIMainWindowInstance:main},GetShowingLockScreen:()=>!!options.locked};
 // Foreground/window/PID helpers are forbidden in the global key path.
 window.SteamUIStore.GetFocusedWindowInstance=()=>{throw Error('OS-focus-like selection must not run')};
 window.SteamUIStore.GetWindowInstanceForPID=()=>{throw Error('PID selection must not run')};
 if(options.noAPI){delete main.OnHomeButtonPressed;delete main.OnQuickAccessButtonPressed;}
 const fn=vm.runInNewContext(source,{window,SteamClient:{},Date:ClockDate,performance:{now:()=>now},setTimeout:(cb,n)=>setTimeout(cb,Math.min(n,10)),clearTimeout,console});
 return{fn,window,calls,keyCalls,advance:ms=>{now+=ms;},get now(){return now;},setMenu:value=>{menu=value;},get menu(){return menu},get overlayMenu(){return overlayMenu}};
}
async function test(name,body){await body();cases.push({name,ok:true});}
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
 for(const bad of ['throwReceiver','timeout','drift'])await test(requested+' '+bad+' never retries a possibly delivered key',async()=>{
  const f=fixture({[bad]:true}),r=await f.fn({operation:'menu.button',menu:requested,account:42});assert(!r.ok&&r.mutated);assert.equal(f.keyCalls.length,1);assert(!f.window.__ymccSteamLiveBusy);
 });
}
for(const flag of ['locked','vr'])await test(flag+' Steam safety boundary before mutation',async()=>{const f=fixture({[flag]:true}),r=await f.fn({operation:'menu.button',menu:'main',account:42});assert.equal(r.reason,'steam-menu-blocked');assert(!r.mutated);assert.equal(f.keyCalls.length,0)});
for(const option of [{noAPI:true}])await test('unsupported native receiver '+JSON.stringify(option)+' fails closed without keyboard/focus fallback',async()=>{const f=fixture(option),r=await f.fn({operation:'menu.button',menu:'main',account:42});assert.equal(r.reason,'live-api-unavailable');assert(!r.mutated);assert.equal(f.keyCalls.length,0)});
for(const option of [{desktop:true},{before:99}])await test('receiver delivery does not depend on menu capability/state '+JSON.stringify(option),async()=>{
 const f=fixture(option),r=await f.fn({operation:'menu.button',menu:'main',account:42});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1);if(option.before===99){assert.equal(r.runtimeAccepted,false);assert.equal(r.response,'key-delivered-menu-state-unknown');}
});
await test('account mismatch never invokes another session key receiver',async()=>{const f=fixture(),r=await f.fn({operation:'menu.button',menu:'main',account:43});assert.equal(r.reason,'steam-account-changed');assert.equal(f.keyCalls.length,0)});
await test('invalid dedicated menu never invokes a receiver',async()=>{const f=fixture(),r=await f.fn({operation:'menu.button',menu:'url',account:42});assert.equal(r.reason,'invalid-steam-menu');assert.equal(f.keyCalls.length,0)});
await test('legacy menu.toggle maps to the same native key receiver',async()=>{const f=fixture(),r=await f.fn({operation:'menu.toggle',menu:'main',account:42});assert(r.ok&&r.keyDelivered);assert.equal(f.keyCalls.length,1)});
await test('production native path has no Windows focus, PID gate, virtual output or keyboard fallback',async()=>{
 const main=fs.readFileSync('native/main.cpp','utf8'),start=main.indexOf('static bool nativeScreenStandaloneSubmit('),end=main.indexOf('// Shared only by shortcut summon routes',start),body=main.slice(start,end);
 for(const forbidden of ['GetForegroundWindow','GetWindowThreadProcessId','foregroundPid','SetForegroundWindow','SetFocus(','AttachThreadInput','sendMenuChord','SendInput','inputHostSubmit','outputTarget','g_frontendButtonPulse.request'])assert(!body.includes(forbidden),forbidden);
 assert(main.includes('operation == "menu.button"'));assert(body.includes('"menu.button"'));assert(body.includes('keyDelivered'));assert(body.includes('menuStateVerified'));assert(body.includes('focusChangedByYmcc'));assert(body.includes('standaloneCurrent(generation)'));
 const branch=source.slice(source.indexOf("if (['menu.button'"),source.indexOf("if (request.operation === 'settings.get'"));
 for(const forbidden of ['foregroundPid','GetFocusedWindowInstance','GetWindowInstanceForPID','.ToggleSideMenu(','.OpenSideMenu(','SetUIMode','EnsureMainWindowCreated','SetOverlayState(','SetFocus'])assert(!branch.includes(forbidden),forbidden);
 assert(branch.includes('OnHomeButtonPressed'));assert(branch.includes('OnQuickAccessButtonPressed'));
});

// The same production bridge, deterministic click clock; no real Steam/CDP/input.
const ticket=f=>({id:'fixture-click',startedAt:f.now});
const qam=(f,phase,t)=>f.fn({operation:'menu.qam.'+phase,account:42,qamFollowup:t});
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
for(const flag of ['suppressed','locked','vr','changedAccount'])await test('follow-up: '+flag+' cannot emit a supplement',async()=>{
 const options={ignore:true},f=fixture(options),t=ticket(f);await original(f,t);options[flag]=true;f.advance(750);const r=await qam(f,'retry',t);assert(!r.mutated);assert.equal(f.keyCalls.length,1);
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
const result={suite:'global Steam functional-key receivers',ok:true,passed:cases.length,cases,virtualTargetStarted:false,systemInputInjected:false,realSteamMenuMutated:false,focusManipulated:false};
if(process.argv[2]){fs.mkdirSync(path.dirname(process.argv[2]),{recursive:true});fs.writeFileSync(process.argv[2],JSON.stringify(result,null,2))}console.log(JSON.stringify(result,null,2));
