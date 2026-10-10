import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
const source=fs.readFileSync('native/steam_live.js','utf8'),cases=[];
function fixture(options={}) {
  let menu=options.before??0,clock=0;const calls=[];
  const window={settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>options.changedAccount?43:42}}}};
  const menuStore={m_cSuppressRequests:options.suppressed?1:0,GetOpenSideMenu:()=>menu,
    ToggleSideMenu(value,open){calls.push([value,open]);if(options.throwSetter)throw Error('failed');if(!options.ignore)menu=open?value:0;if(options.drift)options.changedAccount=true;return options.timeout?new Promise(()=>{}):undefined;}};
  const main={MenuStore:menuStore,BHasMenus:()=>!options.desktop,IsAnyVRWindow:()=>!!options.vr};
  window.SteamUIStore={GetFocusedWindowInstance:()=>options.noFocus?null:main,WindowStore:{GamepadUIMainWindowInstance:main,BHasStandaloneKeyboard:()=>!!options.keyboard},GetShowingLockScreen:()=>!!options.locked,BHomeAndQuickAccessButtonsEnabled:()=>!options.buttonsDisabled};
  if(options.noAPI)delete menuStore.ToggleSideMenu;
  const fn=vm.runInNewContext(source,{window,SteamClient:{},performance:{now:()=>clock+=500},setTimeout:(cb,n)=>setTimeout(cb,Math.min(n,10)),clearTimeout,console});
  return {fn,calls,window,menuStore,get menu(){return menu;}};
}
async function test(name,body){await body();cases.push({name,ok:true});}
for(const requested of ['main','quick-access'])for(const before of [0,1,2])await test(`${requested} toggles directly from ${before} with no controller/Decky API`,async()=>{
  const f=fixture({before}),result=await f.fn({operation:'menu.toggle',menu:requested,account:42}),target=requested==='main'?1:2;
  assert(result.ok);assert.equal(f.menu,before===target?0:target);assert.deepEqual(f.calls,[[target,before!==target]]);assert.equal(result.runtimeAccepted,true);assert(!f.window.__ymccSteamLiveBusy);
});
await test('unfocused Big Picture instance is selected without depending on Decky',async()=>{const f=fixture({noFocus:true});assert((await f.fn({operation:'menu.toggle',menu:'main',account:42})).ok);assert.equal(f.calls.length,1);});
for(const blocked of ['suppressed','keyboard','locked','buttonsDisabled','vr'])await test(`${blocked} refuses action before mutation`,async()=>{const f=fixture({[blocked]:true}),r=await f.fn({operation:'menu.toggle',menu:'main',account:42});assert(!r.ok);assert.equal(r.reason,'steam-menu-blocked');assert(!r.mutated);assert.equal(f.calls.length,0);});
for(const broken of [{noAPI:true},{desktop:true},{before:99}])await test(`unrecognized Steam API/state ${JSON.stringify(broken)} fails closed`,async()=>{const f=fixture(broken),r=await f.fn({operation:'menu.toggle',menu:'quick-access',account:42});assert.equal(r.reason,'live-api-unavailable');assert(!r.mutated);assert.equal(f.calls.length,0);});
await test('invalid menu is rejected before any toggle',async()=>{const f=fixture(),r=await f.fn({operation:'menu.toggle',menu:'url',account:42});assert.equal(r.reason,'invalid-steam-menu');assert.equal(f.calls.length,0);});
await test('account change before dispatch never opens another session menu',async()=>{const f=fixture(),r=await f.fn({operation:'menu.toggle',menu:'main',account:43});assert.equal(r.reason,'steam-account-changed');assert.equal(f.calls.length,0);});
for(const uncertainty of ['ignore','throwSetter','timeout','drift'])await test(`${uncertainty} keeps mutation uncertain and never retries`,async()=>{const f=fixture({[uncertainty]:true}),r=await f.fn({operation:'menu.toggle',menu:'main',account:42});assert(!r.ok);assert(r.mutated);assert.equal(f.calls.length,1);assert(!f.window.__ymccSteamLiveBusy);});
await test('native wiring keeps menu.toggle classified as a mutation and registers standalone handler',async()=>{
 const main=fs.readFileSync('native/main.cpp','utf8'),header=fs.readFileSync('native/screen_standalone_special.h','utf8'),backend=fs.readFileSync('native/screen_special_actions.h','utf8');
 assert(main.includes('operation == "menu.toggle"'));assert(main.includes('standaloneSubmit=nativeScreenStandaloneSubmit'));
 assert(main.includes('case ymcc::screenpads::kStandaloneResultMessage:'));assert(header.includes('WM_APP+175'));
 const body=main.slice(main.indexOf('static bool nativeScreenStandaloneSubmit('),main.indexOf('// Shared only by shortcut summon routes'));
 for(const forbidden of ['g_frontendButtonPulse.request','nativeFrontendButtonTargetEpoch','inputHostSubmit','outputTarget'])assert(!body.includes(forbidden));
 assert(body.indexOf('result.value("mutated",false)')<body.indexOf('sendMenuChord('));assert(body.includes('standaloneCurrent(generation)'));
 assert(body.includes('powerGeneration==g_powerGeneration.load()'));assert(body.includes('quick?"quick-access":"main"}},allowed)'));
 const transport=fs.readFileSync('native/steam_live_cdp.h','utf8');assert(transport.indexOf('if (admitAction && !admitAction())')<transport.indexOf('sent = true;'));assert(body.includes('steamForeground'));
 assert(backend.includes('GetDefaultAudioEndpoint(eCapture,eCommunications'));assert(!backend.includes('wVk=VK_VOLUME_MUTE'));
});
const result={suite:'standalone screen functional-key Steam production script',ok:true,passed:cases.length,cases,virtualTargetStarted:false,systemInputInjected:false,realSteamMenuMutated:false};
if(process.argv[2]){fs.mkdirSync(path.dirname(process.argv[2]),{recursive:true});fs.writeFileSync(process.argv[2],JSON.stringify(result,null,2));}
console.log(JSON.stringify(result,null,2));
