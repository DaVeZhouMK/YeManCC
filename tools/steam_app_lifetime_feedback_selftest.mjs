// Production Steam live-script regression for the read-only GameSessions lifetime
// observer. Registration/delivery use a test double; only production local JS
// logic is tested, not the actual Valve binding, native health or CEF identity.
// No Steam, CDP, launch, focus, Running setter or device operation runs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const source=fs.readFileSync(fileURLToPath(new URL('../native/steam_live.js',import.meta.url)),'utf8');
const sourceHash=crypto.createHash('sha256').update(source).digest('hex');
const results=[];
const tuple={appid:123,gameid:'123',display_name:'Original',app_type:1,local_per_client_data:{display_status:11},selected_per_client_data:{display_status:11},display_status:11};
function fixture({registerThrows=false,synchronousPayload=null,returnUndefined=false}={}){
  const state={account:42,accountReadThrows:false,now:1792054552995,callback:null,registrations:0,actions:0,writes:0,unregisters:0};
  const app={RegisterForAppLifetimeNotifications(callback){
    assert.equal(this,app);assert.equal(arguments.length,1);assert.equal(typeof callback,'function');state.registrations++;
    if(registerThrows)throw Error('fixture-registration-failed');
    state.callback=callback;if(synchronousPayload)callback(synchronousPayload);
    if(!returnUndefined)return {unregister(){state.unregisters++;}};
  }};
  const steamid={GetAccountID:()=>{if(state.accountReadThrows)throw Error('fixture-account-unavailable');return state.account;}};
  const window={settingsStore:{m_CMInterface:{steamid}},SteamUIStore:{m_runningAppIDs:[],WindowStore:{GetAppWindowIDs:()=>[],BHasStandaloneKeyboard:()=>false},GetShowingLockScreen:()=>false,GetFocusedWindowInstance:()=>null},appStore:{m_mapApps:new Map([[123,tuple]])},FocusedAppWindowStore:{GetFocusedAppID:()=>0,GetFocusedOverlayAppID:()=>0,GetFocusedWindowID:()=>0},ControllerStore:{GetControllers:()=>[]}};
  class FixtureDate extends Date {static now(){return state.now;}}
  const context=vm.createContext({window,SteamClient:{GameSessions:app,Input:{}},Date:FixtureDate,performance:{now:()=>0},setTimeout,clearTimeout,console});
  const fn=new vm.Script(source,{filename:'steam_live.js'}).runInContext(context);
  return {fn,state,window,app,context,call:req=>fn(req),emit:event=>{assert.equal(typeof state.callback,'function');state.callback(event)}};
}
const req=()=>({operation:'sleep.inspect',account:42,appids:[123],runningLinkRead:true,nativeAppLifetimeFeedbackRead:true});
async function test(name,body){try{await body();results.push({name,passed:true});}catch(e){results.push({name,passed:false,error:String(e?.stack||e)});}}
await test('observer installs once and preserves Steam event fields',async()=>{
  const f=fixture(); const first=await f.call(req()); assert.equal(first.ok,true); assert.equal(first.mutated,false);
  assert.equal(first.nativeAppLifetimeFeedback.available,true); assert.equal(first.nativeAppLifetimeFeedback.observerInstalled,true); assert.equal(first.nativeAppLifetimeFeedback.registrationAttempted,true); assert.equal(f.state.registrations,1);
  const initial=first.nativeAppLifetimeFeedback;
  assert.equal(typeof initial.observerInstanceId,'string');assert(initial.observerInstanceId.length>0);
  assert.equal(initial.observerCreatedAtUnixMs,f.state.now);assert.equal(initial.diagnosticCountersAvailable,true);
  assert.equal(initial.observerIdentityScope,'local-js-watch-only-not-cef-context-proof');
  assert.equal(initial.registrationEvidenceScope,'local-registration-returned-without-throwing-only');assert.equal(initial.nativeSubscriptionHealth,'unknown');
  assert.equal(initial.rawCallbacksEntered,0);assert.equal(initial.accountUnavailableCallbacks,0);assert.equal(initial.accountMismatchCallbacks,0);assert.equal(initial.schemaInvalidEvents,0);
  f.state.now+=60000;
  f.emit({unAppID:123,nInstanceID:77,bRunning:true}); const second=await f.call(req()); const v=second.nativeAppLifetimeFeedback;
  assert.equal(v.observerInstanceId,initial.observerInstanceId);assert.equal(v.observerCreatedAtUnixMs,initial.observerCreatedAtUnixMs);
  assert.equal(v.rawCallbacksEntered,1);assert.equal(v.accountUnavailableCallbacks,0);assert.equal(v.accountMismatchCallbacks,0);assert.equal(v.schemaInvalidEvents,0);
  assert.equal(v.nativeSubscriptionHealth,'unknown');assert.equal(v.authority,'diagnostic-only-no-admission');assert.equal(Object.hasOwn(v,'account'),false);
  assert.equal(v.events[0].receivedAtUnixMs,f.state.now);
  assert.deepEqual(Object.keys(v.events[0]).sort(),['bRunning','nInstanceID','receivedAtPerformanceMs','receivedAtUnixMs','sequence','unAppID']);
  assert.equal(v.available,true); assert.equal(v.callbackReceived,true); assert.equal(v.latestEventSequence,1); assert.equal(v.events[0].sequence,1); assert.equal(v.events[0].unAppID,123); assert.equal(v.events[0].nInstanceID,'77'); assert.equal(v.events[0].bRunning,true); assert.equal(typeof v.events[0].receivedAtUnixMs,'number');
  const third=await f.call(req()); assert.equal(f.state.registrations,1); assert.equal(f.state.unregisters,0); assert.equal(third.nativeAppLifetimeFeedback.observerInstanceId,initial.observerInstanceId); assert.equal(third.nativeAppLifetimeFeedback.observerCreatedAtUnixMs,initial.observerCreatedAtUnixMs); assert.equal(third.nativeAppLifetimeFeedback.events.length,1); assert.equal(third.overviews[0].localDisplayStatus,11); assert.equal(third.runningAppIDs.length,0);
});
await test('running false and Steam instance id are observed without making Running true',async()=>{
  const f=fixture(); await f.call(req()); f.emit({unAppID:123,nInstanceID:9007199254740991,bRunning:false}); const r=await f.call(req());
  assert.equal(r.nativeAppLifetimeFeedback.events[0].nInstanceID,'9007199254740991'); assert.equal(r.nativeAppLifetimeFeedback.events[0].bRunning,false); assert.equal(r.overviews[0].localRunning,false); assert.equal(r.runningAppIDs.length,0);
});
await test('other app events remain visible and are not borrowed',async()=>{
  const f=fixture(); await f.call(req()); f.emit({unAppID:999,nInstanceID:8,bRunning:true}); const r=await f.call(req()); assert.equal(r.nativeAppLifetimeFeedback.events[0].unAppID,999); assert.equal(r.nativeAppLifetimeFeedback.events[0].bRunning,true); assert.equal(r.nativeAppLifetimeFeedback.events[0].nInstanceID,'8');
});
await test('malformed callback is retained only as schema-invalid evidence',async()=>{
  const f=fixture(); await f.call(req()); f.emit({unAppID:123,nInstanceID:7}); const r=await f.call(req()); assert.equal(r.nativeAppLifetimeFeedback.schemaInvalidEvents,1); assert.equal(r.nativeAppLifetimeFeedback.events.length,0); assert.equal(r.nativeAppLifetimeFeedback.available,true); assert.equal(r.nativeAppLifetimeFeedback.rawCallbacksEntered,1); assert.equal(r.nativeAppLifetimeFeedback.accountUnavailableCallbacks,0); assert.equal(r.nativeAppLifetimeFeedback.accountMismatchCallbacks,0); assert.equal(r.nativeAppLifetimeFeedback.latestEventSequence,0); assert.equal(r.nativeAppLifetimeFeedback.callbackReceived,false);
});
await test('account drift is ignored by the bound observer and never rebinds',async()=>{
  const f=fixture(); const first=(await f.call(req())).nativeAppLifetimeFeedback;f.state.account=43;
  f.emit({unAppID:123,nInstanceID:7,bRunning:true});
  assert.equal((await f.call({...req(),account:42})).reason,'steam-account-changed');
  assert.equal((await f.call({...req(),account:43})).nativeAppLifetimeFeedback.reason,'app-lifetime-watch-account-changed');
  f.state.account=42;const v=(await f.call(req())).nativeAppLifetimeFeedback;
  assert.equal(v.observerInstanceId,first.observerInstanceId);assert.equal(v.rawCallbacksEntered,1);assert.equal(v.accountMismatchCallbacks,1);
  assert.equal(v.accountUnavailableCallbacks,0);assert.equal(v.schemaInvalidEvents,0);assert.equal(v.latestEventSequence,0);assert.equal(v.callbackReceived,false);
  assert.equal(f.state.registrations,1);assert.equal(f.state.unregisters,0);assert.equal(f.window.__ymccSteamAppLifetimeWatch.account,42);
});
await test('flag is restricted to the running-link read lane',async()=>{
  const f=fixture(); for(const operation of ['sleep.reconcile','sleep.input-resume','menu.inspect']){const r=await f.call({...req(),operation}); assert.equal(r.reason,'invalid-steam-app-lifetime-read'); assert.equal(f.state.registrations,0);}
});
await test('new VM watches are distinguishable local identities, not proof of CEF context identity',async()=>{
  const a=fixture(),b=fixture();const av=(await a.call(req())).nativeAppLifetimeFeedback,bv=(await b.call(req())).nativeAppLifetimeFeedback;
  assert.notEqual(av.observerInstanceId,bv.observerInstanceId);assert.equal(av.observerCreatedAtUnixMs,bv.observerCreatedAtUnixMs);
  for(const v of [av,bv]){assert.equal(v.observerIdentityScope,'local-js-watch-only-not-cef-context-proof');assert.equal(v.nativeSubscriptionHealth,'unknown');}
  a.emit({unAppID:123,nInstanceID:7,bRunning:true});
  assert.equal((await a.call(req())).nativeAppLifetimeFeedback.latestEventSequence,1);
  assert.equal((await b.call(req())).nativeAppLifetimeFeedback.rawCallbacksEntered,0);
  assert.equal((await b.call(req())).nativeAppLifetimeFeedback.callbackReceived,false);
  assert.equal(a.state.registrations,1);assert.equal(b.state.registrations,1);
});
await test('raw entry distinguishes unavailable account drops from no callback and never exposes account data',async()=>{
  for(const kind of ['throw','invalid','missing']){
    const f=fixture();const initial=(await f.call(req())).nativeAppLifetimeFeedback;
    const steamid=f.window.settingsStore.m_CMInterface.steamid;
    if(kind==='throw')f.state.accountReadThrows=true;
    if(kind==='invalid')f.state.account=0;
    if(kind==='missing')delete f.window.settingsStore.m_CMInterface.steamid;
    // Malformed content must be rejected by the account gate before schema handling.
    f.emit({});f.state.accountReadThrows=false;f.state.account=42;f.window.settingsStore.m_CMInterface.steamid=steamid;
    const r=await f.call(req()),v=r.nativeAppLifetimeFeedback;
    assert.equal(initial.rawCallbacksEntered,0);assert.equal(v.rawCallbacksEntered,1);assert.equal(v.accountUnavailableCallbacks,1);
    assert.equal(v.accountMismatchCallbacks,0);assert.equal(v.schemaInvalidEvents,0);assert.equal(v.callbackReceived,false);assert.equal(v.latestEventSequence,0);
    assert.equal(v.available,true);assert.equal(v.authority,'diagnostic-only-no-admission');assert.equal(v.nativeSubscriptionHealth,'unknown');
    assert.equal(v.observerInstanceId,initial.observerInstanceId);assert.equal(v.observerCreatedAtUnixMs,initial.observerCreatedAtUnixMs);
    assert.equal(Object.hasOwn(v,'account'),false);assert.equal(v.events.length,0);assert.equal(r.mutated,false);assert.equal(f.state.registrations,1);
  }
});
await test('legacy missing diagnostic fields remain unknown without replacing or backfilling the watch',async()=>{
  for(const kind of ['known-schema','missing-schema','partial-new-counters']){
    const f=fixture();const event={sequence:1,receivedAtUnixMs:100,receivedAtPerformanceMs:0,unAppID:123,nInstanceID:'7',bRunning:false};
    // Synthetic pre-existing legacy watch. Its original callback has no new counters.
    const legacy={version:1,id:'legacy-watch',account:42,events:[event],droppedEvents:0,nextEventSequence:1,active:true,registrationAttempted:true,unregisterAvailable:false,subscription:null};
    if(kind!=='missing-schema')legacy.schemaInvalidEvents=0;
    if(kind==='partial-new-counters'){legacy.diagnosticCountersVersion=1;legacy.rawCallbacksEntered=0;}
    Object.freeze(legacy);const keys=Object.keys(legacy).join(',');f.window.__ymccSteamAppLifetimeWatch=legacy;
    for(let i=0;i<2;i++){
      f.state.now+=60000;const v=(await f.call(req())).nativeAppLifetimeFeedback;
      assert.equal(v.observerInstanceId,'legacy-watch');assert.equal(v.observerCreatedAtUnixMs,null);assert.equal(v.diagnosticCountersAvailable,false);
      assert.equal(v.rawCallbacksEntered,null);assert.equal(v.accountUnavailableCallbacks,null);assert.equal(v.accountMismatchCallbacks,null);
      assert.equal(v.schemaInvalidEvents,kind==='missing-schema'?null:0);assert.equal(v.nativeSubscriptionHealth,'unknown');
      assert.equal(v.latestEventSequence,1);assert.equal(v.callbackReceived,true);assert.equal(v.events[0].bRunning,false);
      assert.equal(f.window.__ymccSteamAppLifetimeWatch,legacy);assert.equal(Object.keys(legacy).join(','),keys);
    }
    assert.equal(f.state.registrations,0);assert.equal(f.state.unregisters,0);
  }
});
await test('registration failure stays quarantined and never retries or claims native health',async()=>{
  const f=fixture({registerThrows:true});const r=await f.call(req()),first=r.nativeAppLifetimeFeedback;
  assert.equal(first.reason,'app-lifetime-registration-failed');assert.equal(first.observerInstalled,false);assert.equal(first.available,false);assert.equal(first.nativeSubscriptionHealth,'unknown');
  const watch=f.window.__ymccSteamAppLifetimeWatch;f.state.now+=3600000;const second=(await f.call(req())).nativeAppLifetimeFeedback;
  assert.equal(second.reason,'app-lifetime-subscription-quarantined');assert.equal(second.nativeSubscriptionHealth,'unknown');
  assert.equal(second.diagnosticCountersAvailable,false);assert.equal(second.rawCallbacksEntered,null);
  assert.equal(f.window.__ymccSteamAppLifetimeWatch,watch);assert.equal(f.state.registrations,1);assert.equal(f.state.unregisters,0);
});
await test('busy and account preflight rejection preserve the existing owner and do not register',async()=>{
  const f=fixture();f.window.__ymccSteamLiveBusy='other-owner';const busy=await f.call(req());
  assert.equal(busy.reason,'live-busy');assert.equal(busy.mutated,false);assert.equal(f.window.__ymccSteamLiveBusy,'other-owner');assert.equal(f.state.registrations,0);
  delete f.window.__ymccSteamLiveBusy;f.state.account=43;const changed=await f.call(req());
  assert.equal(changed.reason,'steam-account-changed');assert.equal(changed.mutated,false);assert.equal(f.state.registrations,0);assert.equal(f.window.__ymccSteamAppLifetimeWatch,undefined);
});
await test('accepted event sequence and 64-entry ring keep their original semantics independently of new counters',async()=>{
  const f=fixture();await f.call(req());
  for(let i=0;i<70;i++)f.emit({unAppID:123,nInstanceID:7,bRunning:i%2===0});
  f.emit({unAppID:123,nInstanceID:7,bRunning:1});const r=await f.call(req()),v=r.nativeAppLifetimeFeedback;
  assert.equal(v.rawCallbacksEntered,71);assert.equal(v.schemaInvalidEvents,1);assert.equal(v.latestEventSequence,70);assert.equal(v.callbackReceived,true);
  assert.equal(v.events.length,64);assert.equal(v.droppedEvents,6);assert.equal(v.events[0].sequence,7);assert.equal(v.events[63].sequence,70);assert.equal(v.events[63].bRunning,false);
  assert.equal(v.nativeSubscriptionHealth,'unknown');assert.equal(r.mutated,false);assert.equal(r.overviews[0].localRunning,false);assert.equal(r.runningAppIDs.length,0);
  assert.equal(v.gameInputVerified,false);assert.equal(v.continueVerified,false);assert.equal(f.state.registrations,1);assert.equal(f.state.unregisters,0);
});
await test('synchronous callbacks and absent disposer preserve registration and accepted-event semantics',async()=>{
  for(const returnUndefined of [true,false]){
    const f=fixture({returnUndefined,synchronousPayload:{unAppID:123,nInstanceID:7,bRunning:true}});
    const v=(await f.call(req())).nativeAppLifetimeFeedback;
    assert.equal(v.observerInstalled,true);assert.equal(v.rawCallbacksEntered,1);assert.equal(v.latestEventSequence,1);assert.equal(v.callbackReceived,true);
    assert.equal(v.subscriptionUnregisterAvailable,!returnUndefined);assert.equal(v.nativeSubscriptionHealth,'unknown');
    await f.call(req());assert.equal(f.state.registrations,1);assert.equal(f.state.unregisters,0);
  }
});
await test('long empty observation does not retry, change identity or infer native subscription health',async()=>{
  const f=fixture();const first=(await f.call(req())).nativeAppLifetimeFeedback;
  f.state.now+=86400000;const next=(await f.call(req())).nativeAppLifetimeFeedback;
  assert.equal(next.observerInstanceId,first.observerInstanceId);assert.equal(next.observerCreatedAtUnixMs,first.observerCreatedAtUnixMs);
  assert.equal(next.rawCallbacksEntered,0);assert.equal(next.latestEventSequence,0);assert.equal(next.callbackReceived,false);
  assert.equal(next.nativeSubscriptionHealth,'unknown');assert.equal(f.state.registrations,1);assert.equal(f.state.unregisters,0);
});
const failed=results.filter(x=>!x.passed);
console.log(JSON.stringify({suite:'Steam GameSessions lifetime observer',sourceSHA256:sourceHash,executionScope:'production-local-JS-logic-only',registrationBinding:'test-double',actualValveBindingExecuted:false,cefContextIdentityVerified:false,passed:results.length-failed.length,cases:results.length,hardwareOperations:0,steamWrites:0,gameLaunches:0,focusWrites:0,deviceWrites:0,tests:results},null,2));
if(failed.length)process.exitCode=1;


