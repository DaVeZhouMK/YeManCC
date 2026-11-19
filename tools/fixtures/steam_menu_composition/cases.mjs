import assert from 'node:assert/strict';
import {createCompositionEnvironment} from './navigation.mjs';
export function registerCompositionCases(environment){
 const {cacheFocusMethods,actualActivation,check,checks,cases,plain,compositionMethods,equalMethod,getState,promiseVariant}=environment;
 const allCases=[],contextReads=[];let activeCase=null;
async function execute(name,fn){activeCase=name;await fn();cases.push(name);activeCase=null;}
function model(options={}){
 const f=actualActivation({healthy:options.healthy===true}),w=f.overlay,windows=f.windows,ui=f.ui,info=w.params.browserInfo;
 if(options.observableParams){
  let currentAppid=w.params.appid;
  Object.defineProperty(w.params,'appid',{enumerable:true,configurable:true,get(){return currentAppid},set(value){currentAppid=value}});
 }
 const A=f.proof.appid,B=730,sourceA=options.signed?(A|0):A,sourceB=B;
 const overview=f.window.appStore.m_mapApps.get(A);overview.appid=options.signed?(A|0):A;
 if(options.override!==undefined)overview.shortcut_override_appid=options.override;
 const other={appid:B,gameid:String(B),app_type:0,local_per_client_data:{display_status:4}};
 f.window.appStore.m_mapApps.set(B,other);ui.m_runningAppIDs=options.single?[sourceA]:[sourceA,sourceB];
 f.window.appStore.GetAppOverviewByAppID=id=>f.window.appStore.m_mapApps.get(id)||f.window.appStore.m_mapApps.get(id>>>0);
 windows.m_mapAppWindows=new Map([[sourceA,{focusedWindowID:0xabc}],[sourceB,{focusedWindowID:0xdef}]]);
 Object.assign(windows,new Function('return ({'+compositionMethods.GetAppFocusedWindowID+'});')());
 const native=[],globalStates=f.states;let phase='outside-key',desired=0;
 const browser=f.nav.RootWindow;browser.browserInfo=info;
 const api={SetComposition(mode,queue,windowID){native.push({phase,self:this,mode,queue:plain(queue),windowID});if(options.throwSupplement&&phase==='after-ensure')throw Error('native-uncertain');if(options.promiseSupplement&&phase==='after-ensure')return options.rejectPromiseSupplement?Promise.reject(new Error('native-composition-rejected')):Promise.resolve('unconfirmed');return undefined;}};
 browser.SteamClient={Window:api};
 w.BrowserWindow=options.late?null:browser;w.m_BrowserWindow=w.BrowserWindow;
 w.MainRunningAppID=options.globalA?sourceA:sourceB;w.MainRunningApp=options.globalA?overview:other;
 const methods=new Function('a','c','g','s','I','D','SteamClient','A','return ({'+[compositionMethods.EnsureCorrectComposition,compositionMethods.EnsureCorrectGameOverlayState,equalMethod,getState].join(',')+'});')(
  {tw:f.window.appStore},{U5N:769},{oy:ui},{I5:{Hidden:0,Notification:1,Overlay:2,Opaque:3,OverlayKeyboard:4,VirtualMenu:5}},769,()=>{},f.client,{j3:()=>({IN_GAMESCOPE:false})});
 const cs={...methods,m_Instance:w,GetName:()=> 'original-composition-vm',ComputeOverallMinimumRequestedComposition:()=>desired,GetOverridingComposition:()=>null,
  m_eLatestCompositionState:0,m_eLastPushedToWebHelperCompositionState:{eCompositionMode:0,appidCompositionQueue:[769],windowID:0},m_bPushedOverlayUISinceHidden:false};
 w.CompositionStateStore=cs;
 if(options.cache){const q=options.globalA?[overview.shortcut_override_appid||overview.appid,769]:[B,769];cs.m_eLastPushedToWebHelperCompositionState={eCompositionMode:2,appidCompositionQueue:q,windowID:options.globalA?0xabc:0xdef};cs.m_eLatestCompositionState=2;}
 if(options.close){f.setOverlayMenu(2);desired=2;}
 const receiver=f.originalQuickReceiver;
 w.OnQuickAccessButtonPressed=function(){
  if(!options.noop)receiver.call(this);else f.keyCalls.push(['overlay','OnQuickAccessButtonPressed']);
  desired=options.keyboard?4:(f.overlayMenu?2:0);phase='original-ensure';
  cs.EnsureCorrectComposition();f.ensureCache=plain(cs.m_eLastPushedToWebHelperCompositionState);
  f.ensureCacheObject=cs.m_eLastPushedToWebHelperCompositionState;f.ensureQueueObject=f.ensureCacheObject.appidCompositionQueue;
  phase='after-ensure';
  if(options.late){w.BrowserWindow=browser;w.m_BrowserWindow=browser;}
  options.afterEnsure?.(f);
 };
 if(options.homeComposition){
 const homeReceiver=w.OnHomeButtonPressed;
 w.OnHomeButtonPressed=function(){
  const value=homeReceiver.call(this);desired=f.overlayMenu?2:0;phase='original-ensure';
  cs.EnsureCorrectComposition();f.ensureCache=plain(cs.m_eLastPushedToWebHelperCompositionState);
  f.ensureCacheObject=cs.m_eLastPushedToWebHelperCompositionState;f.ensureQueueObject=f.ensureCacheObject.appidCompositionQueue;
  phase='after-ensure';options.afterEnsure?.(f);return value;
 };
 }
 f.composition={A,B,sourceA,overview,other,cs,browser,api,native,globalStates,windows,ui,info,setDesired:v=>{desired=v}};
 f.send=(operation='menu.button',menu='quick-access')=>f.fn({operation,menu,menuGame:f.proof});
 return f;
}
const extra=f=>f.composition.native.filter(x=>x.phase==='after-ensure');
const originalCalls=f=>f.composition.native.filter(x=>x.phase==='original-ensure');
function base(f,r,keys=1){
 check('one-original-key',f.keyCalls.length===keys);
 check('official-cache-object-preserved',f.composition.cs.m_eLastPushedToWebHelperCompositionState===f.ensureCacheObject);
 check('official-cache-queue-object-preserved',f.ensureQueueObject===f.composition.cs.m_eLastPushedToWebHelperCompositionState.appidCompositionQueue);
 check('official-cache-values-preserved',JSON.stringify(f.ensureCache)===JSON.stringify(f.composition.cs.m_eLastPushedToWebHelperCompositionState));
 check('correct-native-this-mode',f.composition.native.every(x=>x.self===f.composition.api&&[0,2,4].includes(x.mode)));
 check('no-force-focus',f.forced.length===0);
}
function supplement(f,r){
 base(f,r);const calls=extra(f),q=[f.composition.overview.shortcut_override_appid||f.composition.overview.appid,769];
 check('selected-game-native-tuple-exactly-once',calls.length===1&&calls[0].mode===2&&JSON.stringify(calls[0].queue)===JSON.stringify(q)&&calls[0].windowID===0xabc);
 check('original-key-runtime-opened',r.ok&&r.keyDelivered&&r.runtimeAccepted&&f.overlayMenu===2);
 check('registered-short-never-forced',calls[0].queue[0]!==f.proof.registeredAppid);
 check('native-submit-receipt',r.menuTarget.composition?.nativeRequestSubmitted===true);
}
const add=(name,fn,tag='affected')=>allCases.push({name,fn,tag});
add('global B real Ensure sends B then selected A once',async()=>{
 const f=model(),r=await f.send();base(f,r);
 check('real-ensure-original-native-B',originalCalls(f).length===1&&originalCalls(f)[0].queue[0]===730&&originalCalls(f)[0].windowID===0xdef);
 check('before-mirrored-overlay-A-is-insufficient',f.states.some(x=>x[0]===f.proof.registeredGameid64&&x[1]===2));
 supplement(f,r);
},'stale-global');
add('late BrowserWindow real Ensure skips native but commits cache',async()=>{
 const f=model({late:true,globalA:true}),r=await f.send();base(f,r);
 check('late-browser-official-zero-native-cache-two',originalCalls(f).length===0&&f.ensureCache.eCompositionMode===2&&f.ensureCache.appidCompositionQueue[0]===f.composition.A);
 supplement(f,r);
},'late-browser');
add('cached B tuple suppresses original native but selected A is submitted once',async()=>{const f=model({cache:true}),r=await f.send();check('cache-B-original-no-call',originalCalls(f).length===0);supplement(f,r)});
add('healthy A real Ensure native one and no duplicate',async()=>{const f=model({globalA:true}),r=await f.send();base(f,r);check('healthy-A-one-original-zero-extra',r.ok&&originalCalls(f).length===1&&extra(f).length===0)});
add('healthy cached A performs zero native calls',async()=>{const f=model({globalA:true,cache:true}),r=await f.send();base(f,r);check('healthy-cache-A-zero-native',r.ok&&f.composition.native.length===0)});
add('signed running ID uses exact signed focused-window map key',async()=>{const f=model({signed:true}),r=await f.send();supplement(f,r);check('signed-queue-and-lookup-preserved',extra(f)[0].queue[0]<0&&f.composition.sourceA<0)});
add('shortcut override takes precedence over logical and registered IDs',async()=>{const f=model({signed:true,override:480}),r=await f.send();supplement(f,r);check('override-used',extra(f)[0].queue[0]===480)});
add('legacy menu.toggle uses same original receiver and supplement',async()=>{const f=model(),r=await f.send('menu.toggle');supplement(f,r)});
add('foreign browserInfo after original Ensure prevents supplement',async()=>{const f=model({afterEnsure:f=>{f.composition.browser.browserInfo={...f.composition.info}}}),r=await f.send();base(f,r);check('foreign-info-zero-extra',extra(f).length===0)});
add('non-null browser replacement after original Ensure is refused',async()=>{const f=model({afterEnsure:f=>{f.overlay.BrowserWindow={browserInfo:f.composition.info,SteamClient:{Window:f.composition.api}}}}),r=await f.send();base(f,r);check('new-browser-zero-extra',extra(f).length===0)});
add('account owner revoked after original Ensure preserves one key and no supplement',async()=>{const f=model({afterEnsure:f=>{f.window.settingsStore.m_CMInterface.steamid={GetAccountID:()=>43}}}),r=await f.send();base(f,r);check('account-revoked-no-extra',!r.ok&&extra(f).length===0)});
add('alias native API owner replaced after original Ensure is preserved',async()=>{let foreign;const f=model({afterEnsure:f=>{foreign=function(){};f.client.Overlay.SetOverlayState=foreign}}),r=await f.send();base(f,r);check('foreign-api-owner-preserved',extra(f).length===0&&f.client.Overlay.SetOverlayState===foreign)});
add('unqualified focused-window method is rejected',async()=>{const f=model({afterEnsure:f=>{f.windows.GetAppFocusedWindowID=function unknown(){return 0xabc}}}),r=await f.send();base(f,r);check('unknown-profile-zero-extra',extra(f).length===0&&r.menuTarget.composition?.status==='composition-window-lookup-unqualified')});
add('keyboard requested composition is not overlaid',async()=>{const f=model({keyboard:true}),r=await f.send();base(f,r);check('keyboard-zero-extra',extra(f).length===0&&f.ensureCache.eCompositionMode===4)});
add('inspect does not call original key or supplement',async()=>{const f=model(),cache=plain(f.composition.cs.m_eLastPushedToWebHelperCompositionState),r=await f.send('menu.inspect');check('inspect-read-only-native-and-key',r.ok&&f.keyCalls.length===0&&f.composition.native.length===0&&JSON.stringify(cache)===JSON.stringify(f.composition.cs.m_eLastPushedToWebHelperCompositionState))});
add('close keeps original hidden composition without supplement',async()=>{const f=model({close:true}),r=await f.send();base(f,r);check('close-zero-extra',r.ok&&f.overlayMenu===0&&extra(f).length===0)});
add('no-op key does not submit composition',async()=>{const f=model({noop:true}),r=await f.send();base(f,r);check('noop-zero-extra',r.ok&&r.runtimeAccepted===false&&extra(f).length===0)});
add('throwing supplementary native API is uncertain once without cache commit',async()=>{const f=model({throwSupplement:true}),r=await f.send();supplement(f,r);check('throw-once-uncertain',r.menuTarget.composition.status==='per-browser-composition-uncertain'&&extra(f).length===1)});
add('promise native API remains uncertain and is not replayed',async()=>{
 if(promiseVariant==='rejected'){
  const unhandled=[],onUnhandled=reason=>unhandled.push(String(reason));process.on('unhandledRejection',onUnhandled);
  try{
   const f=model({promiseSupplement:true,rejectPromiseSupplement:true}),r=await f.send();supplement(f,r);
   check('promise-uncertain',r.menuTarget.composition.status==='per-browser-composition-uncertain'&&extra(f).length===1);
   await new Promise(resolve=>setImmediate(()=>setImmediate(resolve)));
   check('native-rejection-isolated',unhandled.length===0);
   check('rejected-native-key-not-replayed',f.keyCalls.length===1&&extra(f).length===1);
  }finally{process.removeListener('unhandledRejection',onUnhandled);}
  return;
 }
const f=model({promiseSupplement:true}),r=await f.send();supplement(f,r);check('promise-uncertain',r.menuTarget.composition.status==='per-browser-composition-uncertain'&&extra(f).length===1)});
add('ambiguous signed and unsigned running representations refuse supplement',async()=>{const f=model({afterEnsure:f=>{f.ui.m_runningAppIDs.push(f.composition.A|0)}}),r=await f.send();base(f,r);check('ambiguous-running-zero-extra',extra(f).length===0)});
add('unknown focused window ID refuses supplement',async()=>{const f=model({afterEnsure:f=>{f.windows.m_mapAppWindows.set(f.composition.sourceA,{focusedWindowID:-1})}}),r=await f.send();base(f,r);check('invalid-focused-window-zero-extra',extra(f).length===0)});
add('foreign composition store owner refuses supplement',async()=>{const f=model({afterEnsure:f=>{f.composition.cs.m_Instance={foreign:true}}}),r=await f.send();base(f,r);check('foreign-cs-zero-extra',extra(f).length===0)});
add('new native Window API during original focused-window read is preserved',async()=>{
 let replacement;const f=model({afterEnsure:f=>{const map=f.windows.m_mapAppWindows;map.get=function(id){if(id===f.composition.sourceA){replacement={SetComposition(){throw Error('foreign-api-must-not-run')}};f.composition.browser.SteamClient.Window=replacement;}return Map.prototype.get.call(this,id)};}}),r=await f.send();
 base(f,r);check('new-native-window-api-no-submit',extra(f).length===0&&f.composition.browser.SteamClient.Window===replacement&&r.menuTarget.composition?.nativeRequestSubmitted===false);
},'native-owner');
add('new native SetComposition method during original focused-window read is preserved',async()=>{
 let replacement;const f=model({afterEnsure:f=>{const map=f.windows.m_mapAppWindows;map.get=function(id){if(id===f.composition.sourceA){replacement=function(){throw Error('foreign-set-must-not-run')};f.composition.api.SetComposition=replacement;}return Map.prototype.get.call(this,id)};}}),r=await f.send();
 base(f,r);check('new-native-set-no-submit',extra(f).length===0&&f.composition.api.SetComposition===replacement&&r.menuTarget.composition?.nativeRequestSubmitted===false);
},'native-owner');


// Guide adds only a new control leaf for the original Home receiver. The
// existing QAM model/cases above remain byte-for-byte unchanged.
function guideModel(options={}){
 const f=model(options),w=f.overlay,{cs,api,native}=f.composition;
 const originalHome=w.OnHomeButtonPressed;let phase='outside-key';
 api.SetComposition=function(mode,queue,windowID){native.push({phase,self:this,mode,queue:plain(queue),windowID});};
 w.OnHomeButtonPressed=function(){
  originalHome.call(this);
  f.composition.setDesired(f.overlayMenu?2:0);phase='original-ensure';
  cs.EnsureCorrectComposition();f.ensureCache=plain(cs.m_eLastPushedToWebHelperCompositionState);
  f.ensureCacheObject=cs.m_eLastPushedToWebHelperCompositionState;f.ensureQueueObject=f.ensureCacheObject.appidCompositionQueue;
  phase='after-ensure';
 };
 f.send=()=>f.fn({operation:'menu.button',menu:'main',menuGame:f.proof});
 return f;
}
add('Guide original Home key corrects global B native queue to selected A once',async()=>{
 const f=guideModel(),r=await f.send();base(f,r);
 check('Guide-original-Home-leaf-exactly-once',f.keyCalls.length===1&&f.keyCalls[0][0]==='overlay'&&f.keyCalls[0][1]==='OnHomeButtonPressed');
 check('Guide-real-Ensure-B-first',originalCalls(f).length===1&&originalCalls(f)[0].queue[0]===730&&originalCalls(f)[0].windowID===0xdef);
 const calls=extra(f);check('Guide-selected-A-tuple-once',calls.length===1&&calls[0].mode===2&&JSON.stringify(calls[0].queue)===JSON.stringify([f.composition.overview.appid,769])&&calls[0].windowID===0xabc);
 check('Guide-main-open-not-QAM',r.ok&&r.keyDelivered&&r.runtimeAccepted&&r.menu===1&&f.overlayMenu===1&&r.before===0);
 check('Guide-native-submit-receipt',r.menuTarget.composition?.nativeRequestSubmitted===true);
 check('Guide-registered-short-never-forced',calls[0].queue[0]!==f.proof.registeredAppid);
},'guide');
add('Guide healthy A real Ensure sends once with no supplement',async()=>{
 const f=guideModel({globalA:true}),r=await f.send();base(f,r);
 check('Guide-healthy-original-Home-leaf',f.keyCalls.length===1&&f.keyCalls[0][1]==='OnHomeButtonPressed'&&f.overlayMenu===1&&r.menu===1);
 check('Guide-healthy-real-Ensure-one-A',originalCalls(f).length===1&&originalCalls(f)[0].mode===2&&originalCalls(f)[0].queue[0]===f.composition.overview.appid&&originalCalls(f)[0].windowID===0xabc);
 check('Guide-healthy-zero-supplement',extra(f).length===0&&f.composition.native.length===1&&r.menuTarget.composition?.nativeRequestSubmitted===false);
 check('Guide-healthy-runtime-opened',r.ok&&r.keyDelivered&&r.runtimeAccepted);
},'guide');

add('post-key Window API replacement is not adopted',async()=>{
 let replacement;const f=model({afterEnsure:f=>{
  replacement={SetComposition(mode,queue,windowID){f.composition.native.push({phase:'after-ensure',self:this,mode,queue:plain(queue),windowID});}};
  f.composition.browser.SteamClient.Window=replacement;
 }}),r=await f.send();
 check('post-key-native-owner-replacement-not-submitted',extra(f).length===0&&r.menuTarget.composition?.nativeRequestSubmitted===false);
 base(f,r);check('post-key-Window-owner-preserved',f.composition.browser.SteamClient.Window===replacement&&r.ok&&r.keyDelivered);
},'owner-successor');
add('post-key SetComposition replacement is not adopted',async()=>{
 let replacement;const f=model({afterEnsure:f=>{
  replacement=function(mode,queue,windowID){f.composition.native.push({phase:'after-ensure',self:this,mode,queue:plain(queue),windowID});};
  f.composition.api.SetComposition=replacement;
 }}),r=await f.send();
 check('post-key-native-owner-replacement-not-submitted',extra(f).length===0&&r.menuTarget.composition?.nativeRequestSubmitted===false);
 base(f,r);check('post-key-method-owner-preserved',f.composition.api.SetComposition===replacement&&r.ok&&r.keyDelivered);
},'owner-successor');
add('post-key SteamClient replacement preserving Window API is not adopted',async()=>{
 let replacement;const f=model({afterEnsure:f=>{replacement={Window:f.composition.api};f.composition.browser.SteamClient=replacement;}}),r=await f.send();
 check('post-key-native-owner-replacement-not-submitted',extra(f).length===0&&r.menuTarget.composition?.nativeRequestSubmitted===false);
 base(f,r);check('post-key-client-owner-preserved',f.composition.browser.SteamClient===replacement&&replacement.Window===f.composition.api&&r.ok&&r.keyDelivered);
},'owner-successor');

function changeAtFinalFocus(f,change){
 const map=f.windows.m_mapAppWindows;let reads=0;
 map.get=function(id){const value=Map.prototype.get.call(this,id);if(id===f.composition.sourceA&&++reads===2){change(f);f.reviewMutatedCache=plain(f.composition.cs.m_eLastPushedToWebHelperCompositionState);}return value;};
}
for(const [label,change]of [
 ['queue element',f=>{f.composition.cs.m_eLastPushedToWebHelperCompositionState.appidCompositionQueue[0]=999;}],
 ['queue length',f=>{f.composition.cs.m_eLastPushedToWebHelperCompositionState.appidCompositionQueue.push(480);}],
 ['cache windowID',f=>{f.composition.cs.m_eLastPushedToWebHelperCompositionState.windowID=0x123;}],
 ['overview AppID',f=>{f.composition.overview.appid=999;}],
 ['cache mode',f=>{f.composition.cs.m_eLastPushedToWebHelperCompositionState.eCompositionMode=4;}],
])add('final focused-window read changes '+label+' in place',async()=>{
 const f=model({afterEnsure:f=>changeAtFinalFocus(f,change)}),r=await f.send();
 check('final-composition-observation-change-rejects-native-submit',extra(f).length===0&&r.menuTarget?.composition?.nativeRequestSubmitted!==true);
 check('final-change-original-key-once',f.keyCalls.length===1);
 check('final-change-cache-owner-preserved',f.composition.cs.m_eLastPushedToWebHelperCompositionState===f.ensureCacheObject);
 check('final-change-cache-values-not-rewritten',JSON.stringify(f.composition.cs.m_eLastPushedToWebHelperCompositionState)===JSON.stringify(f.reviewMutatedCache));
},'review-final');
add('bounded cachedComposition traces original B cache and action scope',async()=>{
 const f=model(),r=await f.send();supplement(f,r);
 const result=r.menuTarget.composition,cached=result.cachedComposition;
 check('action-record-declares-native-action-scope',result.scope==='same-key-per-browser-composition'&&result.diagnosticOnly===false&&result.nativeRequestSubmitted===true);
 check('action-record-does-not-grant-input',result.inputCapabilityGranted===false&&result.physicalInputProven===false);
 check('cached-diagnostic-reports-original-B-tuple',cached.diagnosticOnly===true&&cached.mode===2&&cached.windowID===0xdef&&JSON.stringify(cached.queue)===JSON.stringify([730,769])&&cached.maxQueueLength===16&&cached.snapshot==='before-supplement');
 cached.queue[0]=123;check('cached-diagnostic-is-independent-copy',f.composition.cs.m_eLastPushedToWebHelperCompositionState.appidCompositionQueue[0]===730);
},'review-final');
add('unsafe cached queue is not exported or used for native authority',async()=>{
 const f=model({afterEnsure:f=>{f.composition.cs.m_eLastPushedToWebHelperCompositionState.appidCompositionQueue[0]={unsafeReviewMarker:'must-not-export'};}}),r=await f.send();
 check('unsafe-cached-values-no-submit',extra(f).length===0&&r.menuTarget.composition.nativeRequestSubmitted===false);
 check('unsafe-cached-values-bounded-diagnostic',r.menuTarget.composition.cachedComposition.queue===null&&!JSON.stringify(r.menuTarget.composition).includes('must-not-export'));
 check('unsafe-cached-values-original-key-once',f.keyCalls.length===1);
},'review-final');
add('current menu own-data focus cache records both sides without native work',async()=>{

 const f=model({globalA:true,cache:true,afterEnsure:f=>{f.ui.m_mainInstanceUIMode=2;Object.assign(f.window.FocusedAppWindowStore,{m_unFocusedAppID:769,m_nFocusedWindowID:34})}});

 Object.assign(f.window.FocusedAppWindowStore,{m_unFocusedAppID:f.proof.appid|0,m_nFocusedWindowID:17});

 const r=await f.send();base(f,r);const c=r.menuTarget.inputContext;
 check('focus-cache-required-fields',typeof c.before.steamFocusCache==='object'&&typeof c.after.steamFocusCache==='object');
 check('focus-cache-before-main-mode',c.before.steamFocusCache.mainInstanceUIMode===4);

 check('focus-cache-after-main-mode',c.after.steamFocusCache.mainInstanceUIMode===2);

 check('focus-cache-exact-signed-appid',c.before.steamFocusCache.focusedAppid===(f.proof.appid|0)&&c.before.steamFocusCache.focusedAppidUnsigned===f.proof.appid);

 check('focus-cache-fresh-after-key',c.after.steamFocusCache.focusedAppid===769&&c.before.steamFocusCache.focusedWindowID===17&&c.after.steamFocusCache.focusedWindowID===34);

 check('focus-cache-no-input-authority',c.after.steamFocusCache.diagnosticOnly===true&&c.after.steamFocusCache.cacheFreshnessKnown===false&&c.after.steamFocusCache.activationCallbackDelivery==='UNKNOWN'&&c.after.steamFocusCache.inputCapabilityGranted===false);

 check('focus-cache-healthy-native-zero-key-once',f.composition.native.length===0&&f.keyCalls.length===1&&f.forced.length===0);

},'focus-diagnostic');

add('current menu cache accessors are not invoked or used as authority',async()=>{

 const f=model({globalA:true,cache:true});let getters=0;

 Object.defineProperty(f.ui,'m_mainInstanceUIMode',{get(){++getters;throw Error('diagnostic getter must not run')},configurable:true});

 for(const key of ['m_unFocusedAppID','m_nFocusedWindowID'])Object.defineProperty(f.window.FocusedAppWindowStore,key,{get(){++getters;throw Error('diagnostic getter must not run')},configurable:true});

 const r=await f.send();base(f,r);const c=r.menuTarget.inputContext.after.steamFocusCache;

 check('focus-cache-no-accessor-side-effect',getters===0);

 check('focus-cache-accessors-stay-unknown',c.mainInstanceUIMode===null&&c.focusedAppid===null&&c.focusedAppidUnsigned===null&&c.focusedWindowID===null);

 check('focus-cache-unknown-does-not-gate-key',r.ok&&r.keyDelivered&&f.keyCalls.length===1&&f.composition.native.length===0&&f.forced.length===0);

},'focus-diagnostic');

add('invalid current-menu cache values remain bounded unknown',async()=>{

 const f=model({globalA:true,cache:true});f.ui.m_mainInstanceUIMode={unsafeFocusMarker:'do-not-export'};

 Object.assign(f.window.FocusedAppWindowStore,{m_unFocusedAppID:'413080',m_nFocusedWindowID:-1});

 const r=await f.send();base(f,r);const c=r.menuTarget.inputContext.after.steamFocusCache;

 check('focus-cache-invalid-values-unknown',c.mainInstanceUIMode===null&&c.focusedAppid===null&&c.focusedAppidUnsigned===null&&c.focusedWindowID===null);

 check('focus-cache-no-opaque-value-export',!JSON.stringify(r).includes('unsafeFocusMarker'));

 check('focus-cache-invalid-keeps-original-key',r.ok&&r.keyDelivered&&f.keyCalls.length===1&&f.composition.native.length===0&&f.forced.length===0);

},'focus-diagnostic');

add('qualified original Steam getters observe reactive cache fields without routing input',async()=>{
 let main=4,app=413080,windowID=0;const calls={main:0,app:0,window:0};
 const f=model({globalA:true,cache:true,afterEnsure:()=>{main=2;app=769;windowID=34}}),focus=f.window.FocusedAppWindowStore;
 const mode=Object.getOwnPropertyDescriptors(new Function('return ({'+cacheFocusMethods.mainMode+'});')());
 const proto=Object.create(Object.getPrototypeOf(f.ui));Object.defineProperties(proto,mode);Object.setPrototypeOf(f.ui,proto);
 Object.assign(focus,new Function('return ({'+cacheFocusMethods.appid+','+cacheFocusMethods.windowID+'});')());
 const symbol=Symbol('fixture-mobx');
 const fieldGetter=key=>new Function('W','g','return ('+cacheFocusMethods.observableFieldGetter+');')(symbol,key);
 f.ui[symbol]={getObservablePropValue_(){++calls.main;return main}};
 focus[symbol]={getObservablePropValue_(key){if(key==='m_unFocusedAppID'){++calls.app;return app;}++calls.window;return windowID;}};
 Object.defineProperty(f.ui,'m_mainInstanceUIMode',{get:fieldGetter('m_mainInstanceUIMode'),configurable:true});
 Object.defineProperty(focus,'m_unFocusedAppID',{get:fieldGetter('m_unFocusedAppID'),configurable:true});
 Object.defineProperty(focus,'m_nFocusedWindowID',{get:fieldGetter('m_nFocusedWindowID'),configurable:true});
 const r=await f.send();base(f,r);const c=r.menuTarget.inputContext;
 check('qualified-cache-real-before-values',c.before.steamFocusCache.mainInstanceUIMode===4&&c.before.steamFocusCache.focusedAppid===413080&&c.before.steamFocusCache.focusedWindowID===0);
 check('qualified-cache-real-after-values',c.after.steamFocusCache.mainInstanceUIMode===2&&c.after.steamFocusCache.focusedAppid===769&&c.after.steamFocusCache.focusedWindowID===34);
 check('qualified-cache-only-two-reads-per-field',calls.main===2&&calls.app===2&&calls.window===2);
 check('qualified-cache-no-extra-native-work',r.ok&&f.keyCalls.length===1&&f.composition.native.length===0&&f.forced.length===0);
},'focus-diagnostic');
add('unqualified shadow cache getters stay unknown without invocation',async()=>{
 const f=model({globalA:true,cache:true}),focus=f.window.FocusedAppWindowStore;let calls=0;
 Object.defineProperty(f.ui,'m_mainInstanceUIMode',{get(){++calls;throw Error('unknown')},configurable:true});
 Object.defineProperty(f.ui,'MainInstanceUIMode',{get(){++calls;return 4},configurable:true});
 Object.defineProperty(focus,'m_unFocusedAppID',{get(){++calls;throw Error('unknown')},configurable:true});
 Object.defineProperty(focus,'m_nFocusedWindowID',{get(){++calls;throw Error('unknown')},configurable:true});
 focus.GetFocusedAppID=()=>{++calls;return 413080};focus.GetFocusedWindowID=()=>{++calls;return 0};
 const r=await f.send();base(f,r);const c=r.menuTarget.inputContext.after.steamFocusCache;
 check('unknown-public-readers-not-called',calls===0);
 check('unknown-public-readers-no-proof',c.mainInstanceUIMode===null&&c.focusedAppid===null&&c.focusedWindowID===null&&c.activationCallbackDelivery==='UNKNOWN');
 check('unknown-public-readers-original-key-kept',r.ok&&r.keyDelivered&&f.keyCalls.length===1&&f.composition.native.length===0&&f.forced.length===0);
},'focus-diagnostic');
add('official public readers never invoke unqualified underlying field getters',async()=>{
 const f=model({globalA:true,cache:true}),focus=f.window.FocusedAppWindowStore;let calls=0;
 const proto=Object.create(Object.getPrototypeOf(f.ui));Object.defineProperties(proto,Object.getOwnPropertyDescriptors(new Function('return ({'+cacheFocusMethods.mainMode+'});')()));Object.setPrototypeOf(f.ui,proto);
 Object.assign(focus,new Function('return ({'+cacheFocusMethods.appid+','+cacheFocusMethods.windowID+'});')());
 for(const [object,key] of [[f.ui,'m_mainInstanceUIMode'],[focus,'m_unFocusedAppID'],[focus,'m_nFocusedWindowID']])Object.defineProperty(object,key,{get(){++calls;return 4},configurable:true});
 const r=await f.send();base(f,r);const c=r.menuTarget.inputContext.after.steamFocusCache;
 check('unknown-raw-getters-not-called',calls===0);
 check('unknown-raw-getters-stay-unknown',c.mainInstanceUIMode===null&&c.focusedAppid===null&&c.focusedWindowID===null);
 check('unknown-raw-getters-original-key-once',r.ok&&r.keyDelivered&&f.keyCalls.length===1&&f.composition.native.length===0&&f.forced.length===0);
},'focus-diagnostic');
add('stopped game overview reports first false leaf without later reads',async()=>{
 const f=model({globalA:true,cache:true});let statusReads=0,runningReads=0;
 Object.defineProperty(f.composition.overview.local_per_client_data,'display_status',{get(){++statusReads;return 11;}});
 Object.defineProperty(f.ui,'m_runningAppIDs',{get(){++runningReads;throw Error('later-running-read-must-not-run');},configurable:true});
 const r=await f.send();
 check('preflight-first-false-display-status',r.stage==='menu-game-display-status');
 check('preflight-original-context-reason',r.ok===false&&r.reason==='steam-menu-game-context-changed');
 check('preflight-original-short-circuit',statusReads===1&&runningReads===0);
 check('preflight-no-key-alias-native-or-focus',r.mutated===false&&r.keyDelivered!==true&&f.keyCalls.length===0&&f.nativeReads===0&&!f.window.__ymccMenuGameAlias&&f.composition.native.length===0&&f.forced.length===0);
 check('preflight-busy-owner-returned',!f.window.__ymccSteamLiveBusy);
},'context-diagnostic');
add('qualified game preflight false leaves preserve short circuit and no mutation',async()=>{
 const setups=[
  ['account-object',f=>{f.window.settingsStore=null;}],
  ['account-binding',f=>{const cm=f.window.settingsStore.m_CMInterface,original=cm.steamid;let n=0;Object.defineProperty(cm,'steamid',{get(){return ++n===1?original:{GetAccountID:()=>42};}});}],
  ['account-id',f=>{f.window.settingsStore.m_CMInterface.steamid.GetAccountID=()=>43;}],
  ['app-store',f=>{const store=f.window.appStore,map=store.m_mapApps;let n=0;Object.defineProperty(store,'m_mapApps',{get(){return ++n===1?map:new Map;}});}],
  ['overview-binding',f=>{const map=f.window.appStore.m_mapApps,get=map.get;let n=0;map.get=function(id){if(++n===3)return {};return get.call(this,id);};}],
  ['overview-alias',f=>{f.window.appStore.m_mapApps.set(f.proof.appid|0,{...f.composition.overview});}],
  ['full-id',f=>{f.composition.overview.gameid='1';}],
  ['appid-type',f=>{f.composition.overview.appid=String(f.proof.appid);}],
  ['appid',f=>{f.composition.overview.appid=730;}],
  ['app-type',f=>{f.composition.overview.app_type=NaN;}],
  ['app-kind',f=>{f.composition.overview.app_type=0;}],
  ['running-list',f=>{f.ui.m_runningAppIDs={};}],
  ['running-app',f=>{f.ui.m_runningAppIDs=[];}],
  ['original-alias',f=>{const map=f.window.appStore.m_mapApps,get=map.get,foreign={...f.composition.overview};let signed=0;map.get=function(id){if(id===(f.proof.appid|0))return ++signed===1?foreign:f.composition.overview;return get.call(this,id);};}],
 ];
 for(const [leaf,setup]of setups){
  const f=model({globalA:true,cache:true});setup(f);const r=await f.send();
  check('preflight-false-leaf-'+leaf,r.stage==='menu-game-'+leaf);
  check('preflight-rejection-kept-'+leaf,r.ok===false&&r.reason==='steam-menu-game-context-changed'&&r.mutated===false);
  check('preflight-zero-actions-'+leaf,f.keyCalls.length===0&&f.nativeReads===0&&f.composition.native.length===0&&f.forced.length===0&&!f.window.__ymccMenuGameAlias);
  check('preflight-no-busy-leak-'+leaf,!f.window.__ymccSteamLiveBusy);
 }
},'context-diagnostic');
add('healthy menu context getters and original input route stay unchanged',async()=>{
 const f=model({globalA:true,cache:true}),o=f.composition.overview,store=f.window.appStore,map=store.m_mapApps,trace=[];
 const field=(obj,key,label)=>{const value=obj[key];Object.defineProperty(obj,key,{get(){trace.push(label);return value;},configurable:true});};
 for(const key of ['gameid','appid','app_type'])field(o,key,'overview.'+key);
 field(o.local_per_client_data,'display_status','overview.display_status');field(f.ui,'m_runningAppIDs','ui.running');field(store,'m_mapApps','store.map');
 const get=map.get;map.get=function(id){trace.push('map.get:'+id);return get.call(this,id);};
 const account=f.window.settingsStore.m_CMInterface.steamid,getAccount=account.GetAccountID;
 account.GetAccountID=function(){trace.push('account.id');return getAccount.call(this);};
 const r=await f.send();const observed=trace.slice();base(f,r);
 check('healthy-context-menu-success',r.ok&&r.keyDelivered&&r.runtimeAccepted);
 check('healthy-context-original-native-call-count',f.nativeReads===1&&f.keyCalls.length===1&&f.composition.native.length===0&&f.forced.length===0);
 check('healthy-context-no-stage-export',!('stage'in r));
 check('healthy-context-getter-observations-real',observed.length>20&&observed.includes('account.id')&&observed.includes('overview.display_status')&&observed.includes('ui.running'));
 contextReads.push({case:'healthy',trace:observed,response:plain(r)});
},'context-diagnostic');
add('throwing preflight getter keeps original exception and unknown stage',async()=>{
 const f=model({globalA:true,cache:true});let calls=0;
 Object.defineProperty(f.composition.overview,'gameid',{get(){++calls;throw Error('private-value-must-not-be-read-again');},configurable:true});
 const r=await f.send();
 check('throwing-context-getter-once',calls===1);
 check('throwing-context-original-error-stage',r.ok===false&&r.reason==='live-api-error'&&r.stage==='preflight'&&r.steamFailure?.exceptionClass==='Error');
 check('throwing-context-zero-actions',r.mutated===false&&f.keyCalls.length===0&&f.nativeReads===0&&!f.window.__ymccMenuGameAlias&&f.forced.length===0);
},'context-diagnostic');

// Same original receiver/lookup fixture; only the new bounded native-route
// diagnostics are selected. No hardware callback is fabricated as evidence.
function routeModel(){
 const f=actualActivation();f.ui.m_runningAppIDs=[f.proof.appid];
 f.routeSend=(operation='menu.button')=>f.fn({operation,menu:'quick-access',menuGame:f.proof});
 f.routeSnapshot=()=>f.window.__ymccMenuGameAlias?.menuInputTrace.snapshot().nativeRoute;
 return f;
}
add('original activation lookup is observed for the exact held Host',async()=>{
 const f=routeModel(),r=await f.routeSend();
 check('route-original-open-succeeded',r.ok&&r.keyDelivered&&f.overlayMenu===2);
 check('route-trace-initialized-before-original-state',r.menuTarget.inputTrace?.nativeRoute?.attempted===true);
 const writes=f.states.length;
 f.ui.OnOverlayActivated(f.proof.hostPid,f.proof.appid,true,false);
 const trace=f.routeSnapshot(),calls=trace.calls;
 check('route-logical-original-empty',calls.some(x=>x.kind==='lookup'&&x.path==='original'&&x.appidRole==='logical'&&x.pidRole==='held-host'&&x.resultKind==='none'));
 check('route-registered-original-held',calls.some(x=>x.kind==='lookup'&&x.path==='registered-fallback'&&x.appidRole==='registered'&&x.pidRole==='held-host'&&x.resultKind==='held-window'));
 check('route-state-original-and-alias',calls.some(x=>x.kind==='state'&&x.path==='original'&&x.gameidRole==='logical'&&x.state===2)&&calls.some(x=>x.kind==='state'&&x.path==='alias-submit'&&x.gameidRole==='registered'&&x.state===2));
 check('route-no-new-write-or-force',f.states.length===writes&&f.forced.length===0);
 check('route-record-order',calls.every((x,i)=>x.ordinal===i+1));
 check('route-not-callback-or-input-proof',trace.callerCallback==='UNKNOWN'&&trace.nativeInputAcceptance==='UNKNOWN'&&trace.finalWrapperSelectionProven===false&&trace.physicalInputProven===false);
},'native-route-trace');
add('PID zero game PID and nonempty lookup results keep original selection',async()=>{
 const f=routeModel(),foreign={foreign:true};const original=f.windows.GetOverlayInstance;
 f.windows.GetOverlayInstance=function(id,pid,...args){if(id===730)return foreign;return original.call(this,id,pid,...args)};
 const r=await f.routeSend();check('route-selection-open',r.ok);
 check('route-PID-zero-stays-empty',f.windows.GetOverlayInstance(f.proof.appid,0)==null);
 check('route-game-PID-stays-empty',f.windows.GetOverlayInstance(f.proof.appid,f.proof.gamePid)==null);
 check('route-nonempty-stays-original',f.windows.GetOverlayInstance(730,f.proof.hostPid)===foreign);
 const calls=f.routeSnapshot().calls;
 check('route-PID-zero-observed',calls.some(x=>x.kind==='lookup'&&x.pidRole==='zero'&&x.path==='original'));
 check('route-game-PID-observed',calls.some(x=>x.kind==='lookup'&&x.pidRole==='game'&&x.path==='original'));
 check('route-nonempty-observed-without-alias',calls.some(x=>x.kind==='lookup'&&x.appid===730&&x.resultKind==='other')&&!calls.some(x=>x.path==='registered-fallback'&&['zero','game'].includes(x.pidRole)));
},'native-route-trace');
add('singular original throw and foreign this preserve exact objects',async()=>{
 const f=routeModel(),error={marker:'original-lookup-throw'},foreignThis={},returnValue={marker:'foreign-this-return'};
 const original=f.windows.GetOverlayInstance;
 f.windows.GetOverlayInstance=function(id,pid,...args){if(this===foreignThis)return returnValue;if(id===730)throw error;return original.call(this,id,pid,...args)};
 await f.routeSend();let caught;
 try{f.windows.GetOverlayInstance(730,f.proof.hostPid)}catch(value){caught=value;}
 check('route-throw-same-object',caught===error);
 check('route-foreign-this-return-same-object',f.windows.GetOverlayInstance.call(foreignThis,f.proof.appid,f.proof.hostPid)===returnValue);
 const calls=f.routeSnapshot().calls;
 check('route-throw-recorded',calls.some(x=>x.kind==='lookup'&&x.outcome==='threw'&&x.appid===730));
 check('route-foreign-this-recorded',calls.some(x=>x.kind==='lookup'&&x.receiverMatches===false&&x.resultKind==='other'));
},'native-route-trace');
add('state promise return and throw remain exact without reading then',async()=>{
 const f=routeModel(),error={marker:'original-state-throw'};let thenReads=0;
 const token=Object.defineProperty({},'then',{get(){++thenReads;throw Error('diagnostic-must-not-read-then')}});
 f.client.Overlay.SetOverlayState=function(id,state){f.states.push([id,state]);if(id==='730')throw error;return token;};
 const r=await f.routeSend();check('route-state-open',r.ok);
 const before=f.states.length;
 check('route-state-return-same-token',f.client.Overlay.SetOverlayState(f.proof.gameid64,2)===token);
 let caught;try{f.client.Overlay.SetOverlayState('730',2)}catch(value){caught=value;}
 check('route-state-throw-same-object',caught===error);
 check('route-state-no-then-read',thenReads===0);
 check('route-state-two-original-calls-only',f.states.length===before+2);
 check('route-state-throw-recorded',f.routeSnapshot().calls.some(x=>x.kind==='state'&&x.gameidRole==='other'&&x.outcome==='threw'));
},'native-route-trace');
add('native route window caps expires and cannot rearm',async()=>{
 const f=routeModel();await f.routeSend();
 for(let n=0;n<24;++n)f.windows.GetOverlayInstance(f.proof.registeredAppid,f.proof.hostPid);
 const owner=f.window.__ymccMenuGameAlias,trace=f.routeSnapshot();
 check('route-cap-exact',trace.eventCount===16&&trace.maxEvents===16&&trace.reason==='event-limit'&&trace.status==='stopped');
 owner.menuInputTrace.armNative();
 check('route-no-cap-refund',f.routeSnapshot().eventCount===16&&f.routeSnapshot().status==='stopped');
 check('route-snapshot-immutable',Object.isFrozen(trace)&&Object.isFrozen(trace.calls)&&trace.calls.every(Object.isFrozen));
 const g=routeModel();await g.routeSend();const count=g.routeSnapshot().eventCount;
 g.advance(10001);const returned=g.windows.GetOverlayInstance(g.proof.registeredAppid,g.proof.hostPid);
 const expired=g.routeSnapshot();
 check('route-expiry-preserves-return',returned===g.overlay);
 check('route-expiry-count-stable',expired.eventCount===count&&expired.reason==='window-expired'&&expired.observedWindowMs===10000);
 g.window.__ymccMenuGameAlias.menuInputTrace.armNative();check('route-expiry-no-rearm',g.routeSnapshot().status==='stopped');
},'native-route-trace');
add('close destroy and newer owner end native observation safely',async()=>{
 const f=routeModel();await f.routeSend();const owner=f.window.__ymccMenuGameAlias,lookup=f.windows.GetOverlayInstance;
 const before=owner.menuInputTrace.snapshot().nativeRoute.eventCount;
 const closed=await f.routeSend();
 check('route-close-original-menu',closed.ok&&f.overlayMenu===0&&!f.window.__ymccMenuGameAlias);
 check('route-close-restores-original-lookup',Object.getOwnPropertyDescriptor(f.windows,'GetOverlayInstance').value===f.originalDescriptors.single.value);
 lookup.call(f.windows,f.proof.registeredAppid,f.proof.hostPid);
 check('route-close-no-late-observation',owner.menuInputTrace.snapshot().nativeRoute.eventCount===before);
 const g=routeModel();await g.routeSend();const old=g.window.__ymccMenuGameAlias;
 g.overlay.Destroy();check('route-destroy-original-and-disposed',g.destroyed===true&&old.disposed===true&&old.menuInputTrace.snapshot().nativeRoute.status==='stopped');
 const h=routeModel();await h.routeSend();const prior=h.window.__ymccMenuGameAlias,next={newOwner:true};
 const count=prior.menuInputTrace.snapshot().nativeRoute.eventCount;h.window.__ymccMenuGameAlias=next;
 check('route-new-owner-return-preserved',h.windows.GetOverlayInstance(h.proof.registeredAppid,h.proof.hostPid)===h.overlay);
 check('route-new-owner-preserved',h.window.__ymccMenuGameAlias===next&&prior.menuInputTrace.snapshot().nativeRoute.eventCount===count&&prior.menuInputTrace.snapshot().nativeRoute.reason==='owner-or-menu-ended');
},'native-route-trace');
add('inspect and exact registrations add no native observation',async()=>{
 const f=routeModel(),r=await f.routeSend('menu.inspect');
 check('route-inspect-not-armed',r.ok&&r.menuTarget.inputTrace.nativeRoute.attempted===false&&r.menuTarget.inputTrace.nativeRoute.eventCount===0);
 check('route-inspect-original-hidden-state-kept',f.keyCalls.length===0&&f.states.length===1&&f.states[0][0]===f.proof.registeredGameid64&&f.states[0][1]===0);
 const g=actualActivation({healthy:true}),result=await g.send();
 check('route-exact-original-success',result.ok&&result.keyDelivered&&result.menuTarget.gameState.status==='exact-identity');
 check('route-exact-no-owner-or-trace',!g.window.__ymccMenuGameAlias&&!('inputTrace'in result.menuTarget)&&g.states.length===1);
},'native-route-trace');
add('plural result accessor is never invoked by native diagnostics',async()=>{
 const f=routeModel();let enabled=false,getterReads=0;
 const array=[];Object.defineProperty(array,'0',{get(){++getterReads;throw Error('diagnostic-result-getter')},configurable:true});
 const original=f.windows.GetOverlayInstances;
 f.windows.GetOverlayInstances=function(id,...args){return enabled&&id===f.proof.appid?array:original.call(this,id,...args)};
 const r=await f.routeSend();check('route-array-open',r.ok);enabled=true;
 check('route-array-return-object-preserved',f.windows.GetOverlayInstances(f.proof.appid)===array);
 check('route-array-getter-not-read',getterReads===0);
 const row=f.routeSnapshot().calls.find(x=>x.kind==='list'&&x.appidRole==='logical'&&x.arrayLength===1);
 check('route-array-unknown-held-kept',row?.resultKind==='array'&&row.heldOccurrences===null);
},'native-route-trace');
// Actual product with a controlled native subscription transport. Invoking
// this transport is never evidence that Windows/Steam delivered physical input.
function activationModel(options={}){
 const f=actualActivation({healthy:options.healthy===true});
 f.ui.m_runningAppIDs=[f.proof.appid];
 f.overlay.BRouteMatch=()=>false;f.overlay.BIsOverlayPath=()=>true;
 const actualMain=f.ui.OnOverlayActivated,mainCalls=[],cleanupCalls=[],registrations=[];
 const settings={...options};const subscribers=new Set();
 const originalMain=function(...args){mainCalls.push({self:this,args});return Reflect.apply(settings.main??actualMain,this,args)};
 subscribers.add(originalMain);
 const nativeApi=f.client.Overlay;
 const register=function(callback){
  check('activation-native-register-this',this===nativeApi);
  registrations.push(callback);
  if(settings.registerThrow)throw settings.registerThrow;
  subscribers.add(callback);
  const token={unregister(){
    cleanupCalls.push({self:this,token});
    if(this!==token)throw Error('OWNED_NATIVE_UNREGISTER_RECEIVER_CHANGED');
    if(settings.cleanupThrow)throw settings.cleanupThrow;
    if(settings.cleanupPromise)return Promise.reject(Error('controlled-async-cleanup'));
    if('cleanupValue'in settings)return settings.cleanupValue;
    subscribers.delete(callback);
  }};
  settings.onRegister?.(callback);
  if(settings.noUnregister)return {};
  return token;
 };
 nativeApi.RegisterForOverlayActivated=register;
 const emit=(...args)=>[...subscribers].map(callback=>{
  try{return {ok:true,value:Reflect.apply(callback,f.ui,args)}}catch(error){return {ok:false,error}}
 });
 const snap=()=>f.window.__ymccMenuGameAlias?.menuInputTrace.snapshot().nativeRoute.activation;
 return Object.assign(f,{nativePort:{settings,subscribers,originalMain,actualMain,mainCalls,registrations,cleanupCalls,register,nativeApi,emit},activationSnapshot:snap,
   activationSend:(operation='menu.button')=>f.fn({operation,menu:'quick-access',menuGame:f.proof})});
}
add('passive activation records exact native primitives without replacing the original action',async()=>{
 const f=activationModel(),port=f.nativePort,original=f.ui.OnOverlayActivated,r=await f.activationSend();
 check('activation-registered-before-key-result',r.ok&&r.keyDelivered&&r.menuTarget.inputTrace.nativeRoute.activation?.attempted===true);
 check('activation-one-added-subscription',port.registrations.length===1&&port.subscribers.size===2&&port.subscribers.has(port.originalMain));
 const states=f.states.length,keys=f.keyCalls.length,args=[f.proof.hostPid,f.proof.registeredAppid,true,false];
 const result=port.emit(...args),a=f.activationSnapshot();
 check('activation-original-action-identity-kept',f.ui.OnOverlayActivated===original&&port.actualMain===original&&port.mainCalls.length===1);
 check('activation-original-action-this-and-four-args',port.mainCalls[0].self===f.ui&&port.mainCalls[0].args.length===4&&port.mainCalls[0].args.every((x,i)=>x===args[i]));
 check('activation-native-four-primitives',a?.events.length===1&&a.events[0].pid===args[0]&&a.events[0].appid===args[1]&&a.events[0].active===true&&a.events[0].fourth===false);
 check('activation-original-result-preserved',result[0].ok&&result[0].value===undefined);
 check('activation-no-new-state-key-or-force',f.states.length===states&&f.keyCalls.length===keys&&f.forced.length===0);
 check('activation-does-not-sign-input-or-original-handler',a.physicalInputProven===false&&a.inputCapabilityGranted===false&&a.originalActionCallbackDelivery==='UNKNOWN'&&a.fourthArgumentProducerMeaning==='UNKNOWN'&&a.callbackStateFreshnessKnown===false);
 check('activation-snapshot-primitive-immutable',Object.isFrozen(a)&&Object.isFrozen(a.events)&&a.events.every(Object.isFrozen));
},'activation-observation');
add('foreign callbacks and malformed flags never gain target or input authority',async()=>{
 const value={returnMarker:1},f=activationModel({main(){return value}}),p=f.nativePort;
 await f.activationSend();
 for(const args of [[0,f.proof.registeredAppid,true,false],[f.proof.gamePid,f.proof.registeredAppid,true,false],[f.proof.hostPid+1,f.proof.registeredAppid,true,false],[f.proof.hostPid,730,true,false]])p.emit(...args);
 let a=f.activationSnapshot();check('activation-foreign-targets-only-counted',a?.ignoredCallbacks===4&&a.events.length===0);
 const signed=f.proof.appid|0,args=[f.proof.hostPid,signed,false,true],result=p.emit(...args);a=f.activationSnapshot();
 check('activation-signed-logical-id-exactly-normalized',a.events[0].appidRaw===signed&&a.events[0].appid===f.proof.appid&&a.events[0].appidRole==='logical');
 check('activation-raw-false-and-fourth-retained',a.events[0].active===false&&a.events[0].fourth===true&&result[0].value===value);
 p.emit(f.proof.hostPid,f.proof.registeredAppid,'true',{mustNotRead:true});a=f.activationSnapshot();
 check('activation-malformed-booleans-not-coerced',a.events[1].active===null&&a.events[1].fourth===null&&a.events[1].activeType==='string'&&a.events[1].fourthType==='other'&&a.invalidMatchingPayloads===1);
 const error={originalThrow:1};p.settings.main=function(){throw error};const thrown=p.emit(f.proof.hostPid,f.proof.registeredAppid,true,false);
 check('activation-original-throw-object-kept',thrown[0].ok===false&&thrown[0].error===error&&thrown[1].ok===true);
 check('activation-fanout-does-not-change-menu',f.overlayMenu===2&&f.forced.length===0);
},'activation-observation');
add('native activation callback and semantic time caps stop only the owned observer',async()=>{
 const f=activationModel(),p=f.nativePort;await f.activationSend();const owner=f.window.__ymccMenuGameAlias;
 for(let n=0;n<20;++n)p.emit(f.proof.hostPid,f.proof.registeredAppid,true,false);
 let a=f.activationSnapshot();
 check('activation-cap-sixteen',a?.callbacksObserved===16&&a.events.length===16&&a.reason==='callback-limit');
 check('activation-cap-owned-returned',a.ownSubscriptionUnregistered&&p.subscribers.size===1&&p.cleanupCalls.length===1);
 owner.menuInputTrace.armNative();check('activation-cap-no-rearm-or-refund',p.registrations.length===1&&f.activationSnapshot().events.length===16);
 const g=activationModel(),q=g.nativePort;await g.activationSend();g.advance(10001);q.emit(g.proof.hostPid,g.proof.registeredAppid,true,false);a=g.activationSnapshot();
 check('activation-semantic-expiry-retired-on-existing-event',a.reason==='window-expired'&&a.eventCount===0&&a.ownSubscriptionUnregistered&&q.subscribers.size===1);
 check('activation-time-not-unconditional-unsubscribe-claim',a.noTimer===true&&a.expiry==='semantic-window-checked-on-callback-or-existing-snapshot');
},'activation-observation');
add('next request close destroy OFF and superseding owner independently return activation token',async()=>{
 for(const mode of ['close','destroy','off','new-owner','account-change']){
  const f=activationModel(),p=f.nativePort;await f.activationSend();const old=f.window.__ymccMenuGameAlias,keys=f.keyCalls.length;
  if(mode==='close')await f.activationSend();
  if(mode==='destroy')f.overlay.Destroy();
  if(mode==='off')await f.fn({operation:'menu.qam.dispose',menuGameDisposeOnly:true});
  const next={newOwner:true};
  if(mode==='new-owner'){f.window.__ymccMenuGameAlias=next;p.emit(f.proof.hostPid,f.proof.registeredAppid,true,false);}
  if(mode==='account-change'){f.window.settingsStore.m_CMInterface.steamid={GetAccountID:()=>43};p.emit(f.proof.hostPid,f.proof.registeredAppid,true,false);}
  const a=old.menuInputTrace.snapshot().nativeRoute.activation;
  check('activation-cleanup-'+mode,a?.ownSubscriptionUnregistered===true&&a.cleanupPending===false&&p.subscribers.size===1&&p.subscribers.has(p.originalMain));
  check('activation-original-unregister-receiver-'+mode,p.cleanupCalls.length===1&&p.cleanupCalls[0].self===p.cleanupCalls[0].token);
  if(mode==='new-owner')check('activation-new-owner-not-deleted',f.window.__ymccMenuGameAlias===next);
  if(mode!=='close')check('activation-cleanup-no-key-replay-'+mode,f.keyCalls.length===keys);
  const observed=a.callbacksObserved;p.emit(f.proof.hostPid,f.proof.registeredAppid,true,false);
  check('activation-no-late-old-observation-'+mode,old.menuInputTrace.snapshot().nativeRoute.activation.callbacksObserved===observed);
 }
},'activation-observation');
add('native registration failures quarantine resources without rejecting a functional key',async()=>{
 for(const mode of ['register-throw','missing-unregister']){
  const f=activationModel(mode==='register-throw'?{registerThrow:Error('native-register-uncertain')}:{noUnregister:true}),p=f.nativePort;
  const r=await f.activationSend(),a=f.activationSnapshot();
  check('activation-functional-key-survives-'+mode,r.ok&&r.keyDelivered&&f.overlayMenu===2&&f.keyCalls.length===1);
  check('activation-unknown-resource-retained-'+mode,a?.cleanupPending===true&&!a.ownSubscriptionUnregistered&&!!f.window.__ymccMenuNativeActivationObservation);
  await f.activationSend();const opened=await f.activationSend(),later=f.activationSnapshot();
  check('activation-no-reregister-after-uncertain-'+mode,opened.ok&&p.registrations.length===1&&later.reason==='prior-subscription-or-foreign-slot'&&later.attempted===false);
 }
},'activation-observation');
add('failed activation unregister retains its exact token and independent cleanup can retry it',async()=>{
 const error={nativeCleanup:1},f=activationModel({cleanupThrow:error}),p=f.nativePort;
 await f.activationSend();const old=f.window.__ymccMenuGameAlias,token=f.window.__ymccMenuNativeActivationObservation.resource;
 const closed=await f.activationSend(),a=old.menuInputTrace.snapshot().nativeRoute.activation;
 check('activation-original-close-survives-cleanup-throw',closed.ok&&f.overlayMenu===0&&f.keyCalls.length===2);
 check('activation-failed-unregister-not-reported-returned',a?.cleanupPending&&!a.ownSubscriptionUnregistered&&a.cleanupReason==='unregister-threw');
 check('activation-failed-token-reference-kept',f.window.__ymccMenuNativeActivationObservation.resource===token&&token.handle!==null&&p.subscribers.size===2);
 p.settings.cleanupThrow=null;old.menuInputTrace.stop('alias-dispose');
 const after=old.menuInputTrace.snapshot().nativeRoute.activation;
 check('activation-same-token-eventually-returned',after.ownSubscriptionUnregistered&&!after.cleanupPending&&!f.window.__ymccMenuNativeActivationObservation&&p.subscribers.size===1);
 check('activation-no-new-registration-for-cleanup',p.registrations.length===1&&p.cleanupCalls.every(x=>x.self===x.token));
},'activation-observation');
add('synchronous activation delivery defers disposal until the original token returns',async()=>{
 const f=activationModel(),p=f.nativePort;
 p.settings.onRegister=callback=>{for(let n=0;n<16;++n)callback(f.proof.hostPid,f.proof.registeredAppid,true,false)};
 const r=await f.activationSend(),a=f.activationSnapshot();
 check('activation-synchronous-delivery-key-kept',r.ok&&r.keyDelivered&&f.keyCalls.length===1&&f.states.length===2);
 check('activation-synchronous-cap-retained',a?.eventCount===16&&a.reason==='callback-limit');
 check('activation-synchronous-return-disposed-once',a.ownSubscriptionUnregistered&&p.cleanupCalls.length===1&&p.subscribers.size===1&&!f.window.__ymccMenuNativeActivationObservation);
 const g=activationModel({cleanupPromise:true}),q=g.nativePort;await g.activationSend();const owner=g.window.__ymccMenuGameAlias;await g.activationSend();
 await Promise.resolve();const unknown=owner.menuInputTrace.snapshot().nativeRoute.activation;
 check('activation-async-unregister-is-not-success',unknown.cleanupPending&&!unknown.ownSubscriptionUnregistered&&unknown.cleanupReason==='unregister-contract-unknown');
 const attempts=q.cleanupCalls.length;owner.menuInputTrace.stop('alias-dispose');
 check('activation-async-unregister-not-replayed',q.cleanupCalls.length===attempts&&!!g.window.__ymccMenuNativeActivationObservation);
 for(const value of [false,true,0,null]){
  const h=activationModel({cleanupValue:value}),t=h.nativePort;await h.activationSend();const old=h.window.__ymccMenuGameAlias;await h.activationSend();
  const unknown=old.menuInputTrace.snapshot().nativeRoute.activation;
  check('activation-nonvoid-not-certified-'+String(value),unknown.cleanupPending&&!unknown.ownSubscriptionUnregistered&&unknown.cleanupReason==='unregister-contract-unknown'&&!!h.window.__ymccMenuNativeActivationObservation);
  const count=t.cleanupCalls.length;old.menuInputTrace.stop('alias-dispose');check('activation-nonvoid-not-replayed-'+String(value),t.cleanupCalls.length===count);
 }
},'activation-observation');
add('exact games inspection and unavailable diagnostic API preserve original behavior',async()=>{
 const f=activationModel({healthy:true}),r=await f.activationSend();
 check('activation-exact-game-still-original',r.ok&&r.keyDelivered&&r.menuTarget.gameState.status==='exact-identity'&&f.states.length===1);
 check('activation-exact-zero-registration',f.nativePort.registrations.length===0&&!f.window.__ymccMenuGameAlias&&!f.window.__ymccMenuNativeActivationObservation&&!('inputTrace'in r.menuTarget));
 const g=activationModel(),inspected=await g.activationSend('menu.inspect');
 check('activation-inspect-zero-subscription',inspected.ok&&g.nativePort.registrations.length===0&&!g.window.__ymccMenuNativeActivationObservation&&g.keyCalls.length===0);
 const h=activationModel();delete h.client.Overlay.RegisterForOverlayActivated;const normal=await h.activationSend();
 check('activation-missing-api-does-not-block-menu',normal.ok&&normal.keyDelivered&&h.activationSnapshot().reason==='registration-api-unavailable'&&h.nativePort.registrations.length===0);
},'activation-observation');


add('short registration reasserts same held tuple after original Ensure',async()=>{
 for(const observableParams of [false,true]){
 const f=model({globalA:true,single:true,observableParams}),r=await f.send();base(f,r);
 check('short-params-field-shape',Object.prototype.hasOwnProperty.call(Object.getOwnPropertyDescriptor(f.overlay.params,'appid'),'get')===observableParams);
 check('short-original-ensure-preserved',originalCalls(f).length===1);
 supplement(f,r);
 check('short-reassert-explicit-receipt',r.menuTarget.composition.sameTupleInputReassert===true&&r.menuTarget.composition.status==='per-browser-composition-reassert-submitted');
 check('short-cache-still-selected',r.menuTarget.composition.sourceTupleMatchesSelected===true&&!r.menuTarget.composition.browserMountedDuringKey);
 }
},'short-registration-reassert');
add('short cached signed tuple reasserts once without changing its queue',async()=>{
 const f=model({observableParams:true,globalA:true,single:true,cache:true,signed:true}),r=await f.send();base(f,r);supplement(f,r);
 check('short-cache-original-remains-zero',originalCalls(f).length===0);
 check('short-signed-source-preserved',extra(f)[0].queue[0]===f.composition.sourceA&&extra(f)[0].queue[0]<0);
},'short-registration-reassert');
add('short repeated open close uses one original key and zero close supplement',async()=>{
 const f=model({observableParams:true,globalA:true,single:true,cache:true});
 let r=await f.send();base(f,r);check('short-first-open-one',extra(f).length===1&&f.overlayMenu===2);
 r=await f.send();base(f,r,2);check('short-close-not-reasserted',extra(f).length===1&&f.overlayMenu===0&&!r.menuTarget.composition);
 r=await f.send();base(f,r,3);check('short-next-explicit-open-once',extra(f).length===2&&f.overlayMenu===2);
 check('short-no-extra-state-or-key-replay',f.keyCalls.length===3&&f.forced.length===0);
},'short-registration-reassert');
add('short final ownership change refuses the pending same tuple reassertion',async()=>{
 for(const [label,change]of [
  ['second-running-game',f=>f.composition.ui.m_runningAppIDs.push(730)],
  ['registered-appid',f=>++f.overlay.params.appid],
  ['native-window-api',f=>{f.composition.browser.SteamClient.Window={SetComposition(){throw Error('foreign write')}}}],
  ['same-cache-queue',f=>{f.composition.cs.m_eLastPushedToWebHelperCompositionState.appidCompositionQueue[0]=730}],
 ]){
  const f=model({observableParams:true,globalA:true,single:true,cache:true,afterEnsure:f=>changeAtFinalFocus(f,change)}),r=await f.send();
  check('short-final-zero-extra-'+label,extra(f).length===0&&f.keyCalls.length===1&&f.forced.length===0);
  check('short-final-never-input-proof-'+label,r.menuTarget?.composition?.physicalInputProven!==true);
 }
},'short-registration-reassert');
add('short uncertain native setter stays once and keeps the original cache',async()=>{
 for(const options of [{throwSupplement:true},{promiseSupplement:true,rejectPromiseSupplement:true}]){
  const f=model({observableParams:true,...options,globalA:true,single:true,cache:true}),r=await f.send();base(f,r);supplement(f,r);
  await Promise.resolve();
  check('short-uncertain-once',extra(f).length===1&&r.menuTarget.composition.status==='per-browser-composition-uncertain');
  check('short-uncertain-not-input-ack',r.menuTarget.composition.inputCapabilityGranted===false&&r.menuTarget.composition.physicalInputProven===false);
 }
},'short-registration-reassert');
add('exact base alias multiple games inspection and no-op preserve original work',async()=>{
 const exact=model({observableParams:true,globalA:true,single:true,healthy:true}),er=await exact.send();base(exact,er);
 check('short-exact-no-supplement',er.ok&&extra(exact).length===0&&!exact.window.__ymccMenuGameAlias);
 const baseAlias=model({observableParams:true,globalA:true,single:true,cache:true}),w=baseAlias.overlay;
 baseAlias.proof.registeredAppid=2054970;baseAlias.proof.registeredGameid64='2054970';
 w.params.appid=2054970;w.params.gameid='2054970';w.params.browserInfo.m_unAppID=2054970;w.params.browserInfo.m_gameID='2054970';
 baseAlias.rows[0].appID=2054970;baseAlias.rows[0].gameID='2054970';
 const br=await baseAlias.send();base(baseAlias,br);check('short-legacy-base-alias-zero',br.ok&&extra(baseAlias).length===0&&br.menuTarget.composition.status==='composition-source-tuple-unchanged');
 const multi=model({observableParams:true,globalA:true,cache:true}),mr=await multi.send();base(multi,mr);check('short-multiple-games-zero',mr.ok&&extra(multi).length===0);
 const inspect=model({observableParams:true,globalA:true,single:true,cache:true}),ir=await inspect.send('menu.inspect');
 check('short-inspect-zero-key-native',ir.ok&&inspect.keyCalls.length===0&&inspect.composition.native.length===0);
 const noop=model({observableParams:true,globalA:true,single:true,cache:true,noop:true}),nr=await noop.send();base(noop,nr);
 check('short-noop-zero-supplement',nr.ok&&nr.runtimeAccepted===false&&extra(noop).length===0);
},'short-registration-reassert');

add('short QAM to MAIN preserves one same tuple reassert and original close',async()=>{
 const f=model({globalA:true,single:true,cache:true,homeComposition:true});
 let result=await f.send();base(f,result);check('short-home-qam-first',f.overlayMenu===2&&extra(f).length===1);
 result=await f.send('menu.button','main');base(f,result,2);
 check('short-home-switch-one-reassert',result.ok&&result.runtimeAccepted&&f.overlayMenu===1&&extra(f).length===2&&result.menuTarget.composition.sameTupleInputReassert===true);
 check('short-home-same-mode-queue-window',extra(f).every(x=>x.mode===2&&x.windowID===0xabc&&JSON.stringify(x.queue)===JSON.stringify([f.composition.overview.appid,769])));
 check('short-home-no-hidden-pulse',f.states.every(x=>x[1]===2));
 result=await f.send('menu.button','main');base(f,result,3);
 check('short-home-close-original-no-supplement',result.ok&&result.runtimeAccepted&&f.overlayMenu===0&&extra(f).length===2&&!result.menuTarget.composition);
 check('short-home-no-force-or-extra-key',f.forced.length===0&&f.keyCalls.length===3);
},'short-registration-home');

// Exact shipped focus coordinator class and ordinary bind decorator. Only the
// native window leaves and callback collection transport are controlled here.
function registeredFocus(options={}){
 const f=model({observableParams:true,globalA:true,single:true,cache:true,...options.model}),c=f.nav,m=f.manager;
 const original=environment.focusCoordinator,library=environment.focusLibrary;
 check('official-focus-library-decorator-pinned',library.decoratorSha256==='1d7b18ec8ec1e77156c06e5ad2e0e984f568215d22f1ec7521931cc16fd1ac75');
 const callbacks=()=>{const entries=[];return {Register(fn){entries.push(fn);return {Unregister:()=>{const i=entries.indexOf(fn);if(i>=0)entries.splice(i,1)}};},Dispatch(...args){for(const fn of [...entries])fn(...args)},get size(){return entries.length}};};
 class Cleanup {constructor(){this.fns=[]}Push(fn){this.fns.push(fn)}GetUnregisterFunc(){return()=>{for(const fn of this.fns.splice(0).reverse())fn()}}}
 const C=new Function('s','n','S','i','g','return (()=>{'+original.j+original.A+original.b+'return '+original.constructor+';})()')(
  {e0:Cleanup},{wT:assert},()=>{},{x9:(array,value)=>array.splice(array.indexOf(value),1)},()=>{});
 const decorate=new Function('return ('+library.decorator+');')();
 for(const key of ['OnTreeActivated','OnTreeDeactivated'])Object.defineProperty(C.prototype,key,decorate(C.prototype,key,Object.getOwnPropertyDescriptor(C.prototype,key)));
 const co=new C,root=c.RootWindow,native=[];
 const makeWindow=label=>({label,document:{activeElement:null},SteamClient:{Window:{
  MarkLastFocused(){native.push({kind:'mark',owner:label,self:this});options.onMark?.(f);if(options.throwMark)throw Error('mark-uncertain')},
  SetKeyFocus(value){native.push({kind:'key',owner:label,self:this,value});if(options.throwKey)throw Error('key-uncertain')}
 }}});
 Object.assign(root.SteamClient.Window,makeWindow('root').SteamClient.Window);
 const owner=root,treeWindow=options.popup?makeWindow('popup'):root,tree={m_Controller:m,m_context:c,m_window:treeWindow,m_valueIsMounted:{Value:false},
  OnActivateCallbacks:callbacks(),OnDeactivateCallbacks:callbacks(),OnChildTreesChangedCallbacks:callbacks(),ChildTrees:[]};
 Object.defineProperties(tree,Object.getOwnPropertyDescriptors(new Function('return ({'+[original.treeActive,original.treeContextActive,original.treeWindowContext,original.treeWindow].join(',')+'});')()));
 m.IsActiveNavTree=t=>c.m_LastActiveNavTree===t;m.IsActiveFocusNavTree=t=>c.m_LastActiveFocusNavTree===t;
 m.BIsInActiveContext=t=>t.WindowContext===m.m_ActiveContext;
 const unregister=co.RegisterNavTree('selected-tree',tree,{ownerWindow:owner});
 const row=co.FindTree(tree);f.window.g_WindowFocusCoordinator=co;
 const key=f.overlay.OnQuickAccessButtonPressed;
 f.overlay.OnQuickAccessButtonPressed=function(...args){
  const reply=key.apply(this,args);
  if(f.overlayMenu){c.m_LastActiveFocusNavTree=tree;c.m_LastActiveNavTree=tree;tree.m_valueIsMounted.Value=true;c.m_activeWindow=treeWindow;}
  options.afterKey?.(f);return reply;
 };
 Object.assign(f,{focus:{co,C,row,tree,owner,root,native,unregister,callbacks},focusOptions:options});
 return f;
}
add('registered original focus callback selects the held root or popup once',async()=>{
 for(const popup of [false,true]){
  const f=registeredFocus({popup}),r=await f.send();base(f,r);supplement(f,r);
  check('registered-focus-original-native-window',f.focus.native.length===2&&f.focus.native[0].kind==='mark'&&f.focus.native[1].kind==='key'&&f.focus.native[1].value===true);
  check('registered-focus-owner-this',f.focus.native.every(x=>x.owner==='root'&&x.self===f.focus.owner.SteamClient.Window));
  check('registered-focus-explicit-receipt',r.menuTarget.windowFocus?.callbackSubmitted===true&&r.menuTarget.windowFocus.status==='original-focus-callback-submitted');
  check('registered-focus-not-input-ack',r.menuTarget.windowFocus.physicalInputProven===false&&r.menuTarget.windowFocus.inputCapabilityGranted===false);
  check('registered-focus-original-registered-row',f.focus.co.FindTree(f.focus.tree)===f.focus.row&&f.focus.co.m_rgTrees.length===1);
 }
},'focus-handoff');
add('registered callback close retains the original deactivation lifecycle',async()=>{
 const f=registeredFocus({popup:true}),r=await f.send();check('registered-close-open-once',r.ok&&f.focus.native.length===2);
 const closed=await f.send();check('registered-close-no-extra-focus-write',closed.ok&&f.overlayMenu===0&&f.focus.native.length===2&&!closed.menuTarget.windowFocus);
 const next={...f.focus.tree,ChildTrees:[],BIsActive:()=>false,OnActivateCallbacks:f.focus.callbacks(),OnDeactivateCallbacks:f.focus.callbacks(),OnChildTreesChangedCallbacks:f.focus.callbacks()};const cleanup=f.focus.co.RegisterNavTree('same-window-next',next,{ownerWindow:f.focus.owner});
 f.focus.tree.OnDeactivateCallbacks.Dispatch(f.focus.tree,next);check('registered-internal-transfer-no-false',f.focus.native.length===2);
 f.focus.tree.OnDeactivateCallbacks.Dispatch(f.focus.tree,null);
 check('registered-original-deactivate-returns-focus',f.focus.native.length===3&&f.focus.native[2].kind==='key'&&f.focus.native[2].value===false&&f.focus.native[2].owner==='root');
 cleanup();f.focus.unregister();check('registered-original-unregister-returns-callbacks',f.focus.co.m_rgTrees.length===0&&f.focus.tree.OnActivateCallbacks.size===0&&f.focus.tree.OnDeactivateCallbacks.size===0);
},'focus-handoff');
add('unconfirmed registered context owner view and callback shapes perform no focus writes',async()=>{
 const variants=[
  ['foreign-owner',f=>{f.focus.row.browserContext={ownerWindow:{}}}],
  ['foreign-view',f=>{f.focus.row.browser={SetFocus(){throw Error('foreign browser view')}}}],
  ['accessor-view',f=>Object.defineProperty(f.focus.row,'browser',{get(){throw Error('must not read foreign getter')}})],
  ['foreign-context',f=>{f.focus.tree.m_context={}}],
  ['foreign-active-window',f=>{f.nav.m_activeWindow={}}],
  ['inactive-tree',f=>{f.focus.tree.m_valueIsMounted.Value=false}],
  ['duplicate-tree',f=>{f.focus.co.m_rgTrees.push({...f.focus.row})}],
  ['unknown-constructor',f=>{Object.defineProperty(f.focus.C.prototype,'constructor',{value:function Other(){}})}],
  ['unknown-bound-callback',f=>{const foreign=Object.create(f.focus.C.prototype);foreign.m_rgTrees=f.focus.co.m_rgTrees;Object.defineProperty(foreign,'OnTreeActivated',{value:function replaced(){throw Error('foreign callback')}});f.window.g_WindowFocusCoordinator=foreign}],
  ['unknown-coordinator-getter',f=>Object.defineProperty(f.focus.C.prototype,'OnTreeActivated',{get(){throw Error('foreign getter')}})]
 ];
 for(const [label,change]of variants){const f=registeredFocus({popup:true,afterKey:change}),r=await f.send();check('registered-reject-'+label,r.ok&&f.keyCalls.length===1&&f.focus.native.length===0&&r.menuTarget.windowFocus?.callbackSubmitted===false)}
},'focus-handoff');
add('final registered owner revocation prevents the pending focus action',async()=>{
 for(const [label,change]of [
  ['game-account',f=>{f.window.settingsStore.m_CMInterface.steamid={GetAccountID:()=>43}}],
  ['new-game',f=>f.ui.m_runningAppIDs.push(730)],
  ['row-owner',f=>{f.focus.row.browserContext.ownerWindow={}}],
  ['native-owner',f=>{f.focus.owner.SteamClient.Window={MarkLastFocused(){throw Error('foreign')},SetKeyFocus(){throw Error('foreign')}}}]
 ]){
  let reads=0;const f=registeredFocus({popup:true,afterKey:f=>{const read=f.focus.tree.BIsContextActive;f.focus.tree.BIsContextActive=function(){const value=read.call(this);if(++reads===2)change(f);return value}}}),r=await f.send();
  check('registered-final-revoke-'+label,f.focus.native.length===0&&f.keyCalls.length===1&&r.menuTarget.windowFocus?.callbackSubmitted===false);
 }
},'focus-handoff');
add('uncertain original focus callback is never retried or converted to key replay',async()=>{
 for(const flags of [{throwMark:true},{throwKey:true}]){
  const f=registeredFocus({popup:true,...flags}),r=await f.send();
  check('registered-focus-uncertain-once',r.ok&&r.menuTarget.windowFocus?.callbackSubmitted===true&&r.menuTarget.windowFocus.status==='original-focus-callback-uncertain');
  check('registered-focus-uncertain-native-count',f.focus.native.length===(flags.throwMark?1:2)&&f.keyCalls.length===1&&f.forced.length===0);
  check('registered-focus-uncertain-original-menu-truth',r.runtimeAccepted===true&&f.overlayMenu===2&&r.menuTarget.windowFocus.inputCapabilityGranted===false);
 }
},'focus-handoff');
add('exact multiple inspect no-op base alias and uncertain composition never enter focus action',async()=>{
 for(const modelOptions of [{healthy:true},{single:false},{noop:true},{throwSupplement:true},{promiseSupplement:true,rejectPromiseSupplement:true}]){
  const f=registeredFocus({model:modelOptions}),r=await f.send();await Promise.resolve();
  check('registered-focus-excluded-model',r.ok&&f.focus.native.length===0&&!r.menuTarget.windowFocus);
 }
 const inspect=registeredFocus(),ir=await inspect.send('menu.inspect');check('registered-focus-inspect-zero',ir.ok&&inspect.keyCalls.length===0&&inspect.focus.native.length===0&&!ir.menuTarget.windowFocus);
 const f=registeredFocus(),w=f.overlay;f.proof.registeredAppid=2054970;f.proof.registeredGameid64='2054970';
 w.params.appid=2054970;w.params.gameid='2054970';w.params.browserInfo.m_unAppID=2054970;w.params.browserInfo.m_gameID='2054970';f.rows[0].appID=2054970;f.rows[0].gameID='2054970';
 const r=await f.send();check('registered-focus-base-alias-zero',r.ok&&f.focus.native.length===0&&!r.menuTarget.windowFocus);
},'focus-handoff');

// Original coordinator is window-local. Model the field's four unrelated
// evaluation-global rows while the selected tree lives in a held root/popup.
function scopedCoordinator(f,placement='root'){
 const {C,co,tree,root,callbacks}=f.focus,other=new C;
 for(let i=0;i<4;++i){
  const t={ChildTrees:[],BIsActive:()=>false,OnActivateCallbacks:callbacks(),OnDeactivateCallbacks:callbacks(),OnChildTreesChangedCallbacks:callbacks()};
  other.RegisterNavTree('unrelated-'+i,t,{ownerWindow:f.window});
 }
 f.window.g_WindowFocusCoordinator=other;
 const target=placement==='root'?root:tree.Window;
 Object.defineProperty(target,'g_WindowFocusCoordinator',{value:co,writable:true,configurable:true});
 return {other,target,selected:co};
}
add('held root or active popup coordinator owns the original registered tree',async()=>{
 for(const placement of ['root','popup']){
  const f=registeredFocus({popup:true}),scoped=scopedCoordinator(f,placement),r=await f.send();
  check('scoped-old-evaluation-has-four-unrelated-rows',scoped.other.m_rgTrees.length===4&&scoped.other.FindTree(f.focus.tree)===undefined);
  check('scoped-held-coordinator-native-once',f.focus.native.length===2&&f.focus.native[0].kind==='mark'&&f.focus.native[1].kind==='key'&&f.focus.native[1].value===true&&f.focus.native.every(x=>x.owner==='root'));
  const receipt=r.menuTarget.windowFocus,selected=receipt.coordinatorSelection;
  check('scoped-original-callback-receipt',r.ok&&receipt.callbackSubmitted===true&&receipt.status==='original-focus-callback-submitted');
  check('scoped-exact-origin-selected',selected.uniqueRegisteredCoordinators===1&&selected.selectedOrigins.includes(placement==='root'?'receiver-root':'active-tree-window'));
  check('scoped-three-or-less-windows',selected.maximumWindows===3&&selected.candidates.length<=3&&selected.candidates.some(x=>x.origins.includes('evaluation-window')&&x.registeredTreeCount===4&&x.findReturnedRow===false));
  check('scoped-original-row-and-own-bound-preserved',f.focus.co.FindTree(f.focus.tree)===f.focus.row&&Object.getOwnPropertyDescriptor(f.focus.co,'OnTreeActivated').configurable===false);
 }
},'focus-coordinator-scope');
add('coordinator identity deduplicates and distinct matching registrations refuse action',async()=>{
 const same=registeredFocus({popup:true});same.focus.root.g_WindowFocusCoordinator=same.focus.co;same.focus.tree.Window.g_WindowFocusCoordinator=same.focus.co;
 const sr=await same.send();check('scoped-same-object-once',sr.ok&&same.focus.native.length===2&&sr.menuTarget.windowFocus.coordinatorSelection.uniqueRegisteredCoordinators===1);
 const f=registeredFocus({popup:true}),second=new f.focus.C;
 second.RegisterNavTree('same-tree-other-coordinator',f.focus.tree,{ownerWindow:f.focus.root});f.focus.root.g_WindowFocusCoordinator=second;
 const r=await f.send();check('scoped-distinct-coordinators-refused',r.ok&&f.focus.native.length===0&&r.menuTarget.windowFocus.callbackSubmitted===false&&r.menuTarget.windowFocus.reason==='registered-tree-coordinator-ambiguous');
 check('scoped-no-extra-key-force',f.keyCalls.length===1&&f.forced.length===0);
},'focus-coordinator-scope');
add('late held coordinator registration and owner replacement revoke pending action',async()=>{
 for(const [label,change]of [
  ['slot',f=>{f.focus.root.g_WindowFocusCoordinator=new f.focus.C}],
  ['second-registration',f=>{f.window.g_WindowFocusCoordinator.m_rgTrees.push({name:'late-foreign-registration',tree:f.focus.tree,browserContext:{ownerWindow:f.focus.root}})}],
  ['selected-row',f=>{f.focus.co.m_rgTrees.splice(0)}],
  ['selected-constructor',f=>{Object.defineProperty(f.focus.C.prototype,'constructor',{value:function Changed(){}})}]
 ]){
  let reads=0;const f=registeredFocus({popup:true,afterKey:f=>{const original=f.focus.tree.BIsContextActive;f.focus.tree.BIsContextActive=function(){const value=original.call(this);if(++reads===2)change(f);return value}}});scopedCoordinator(f,'root');
  const r=await f.send();check('scoped-final-revocation-'+label,f.keyCalls.length===1&&f.focus.native.length===0&&r.menuTarget.windowFocus.callbackSubmitted===false&&r.menuTarget.windowFocus.reason==='registered-focus-owner-changed-before-callback');
 }
},'focus-coordinator-scope');
add('missing and accessor registration rows report the actual refusal without focus writes',async()=>{
 const absent=registeredFocus({popup:true});scopedCoordinator(absent,'root');absent.focus.co.m_rgTrees.splice(0);
 let r=await absent.send();check('scoped-no-registration-zero-action',r.ok&&absent.focus.native.length===0&&r.menuTarget.windowFocus.reason==='registered-tree-unconfirmed'&&r.menuTarget.windowFocus.coordinatorSelection.uniqueRegisteredCoordinators===0);
 const f=registeredFocus({popup:true});scopedCoordinator(f,'root');let readCount=0;
 Object.defineProperty(f.focus.row,'tree',{get(){++readCount;return f.focus.tree},configurable:true});
 r=await f.send();const observation=r.menuTarget.windowFocus.coordinatorSelection.candidates.find(x=>x.origins.includes('receiver-root'));
 check('scoped-accessor-slot-refusal-distinguished',r.ok&&f.focus.native.length===0&&observation.findReturnedRow===null&&observation.unverifiedRowCount===1&&observation.status==='registration-tree-slot-unverified'&&r.menuTarget.windowFocus.reason==='coordinator-scope-incomplete');
 check('scoped-row-only-read-by-original-FindTree',readCount===0&&f.keyCalls.length===1);
},'focus-coordinator-scope');

add('present unverified coordinator cannot be omitted from held-window uniqueness',async()=>{
 for(const kind of ['accessor-slot','null-slot','unknown-profile']){
  const f=registeredFocus({popup:true});const scoped=scopedCoordinator(f,'root');let getterReads=0;
  if(kind==='accessor-slot')Object.defineProperty(f.focus.tree.Window,'g_WindowFocusCoordinator',{get(){++getterReads;return scoped.selected},configurable:true});
  if(kind==='null-slot')f.focus.tree.Window.g_WindowFocusCoordinator=null;
  if(kind==='unknown-profile')f.focus.tree.Window.g_WindowFocusCoordinator={m_rgTrees:[]};
  const r=await f.send();
  check('scope-incomplete-refuses-'+kind,r.ok&&f.focus.native.length===0&&r.menuTarget.windowFocus.callbackSubmitted===false&&r.menuTarget.windowFocus.reason==='coordinator-scope-incomplete');
  check('scope-unknown-getter-not-read-'+kind,getterReads===0&&f.keyCalls.length===1&&f.forced.length===0);
 }
},'focus-scope-r2');
add('final selected own-data row cannot be replaced by accessor and equal match count',async()=>{
 let treeReads=0,contextReads=0;
 const f=registeredFocus({popup:true,afterKey:f=>{const read=f.focus.tree.BIsContextActive;f.focus.tree.BIsContextActive=function(){const active=read.call(this);if(++contextReads===2){Object.defineProperty(f.focus.row,'tree',{get(){++treeReads;return f.focus.tree},configurable:true});f.focus.co.m_rgTrees.push({name:'replacement-data-row',tree:f.focus.tree,browserContext:f.focus.row.browserContext});}return active}}});
 scopedCoordinator(f,'root');const r=await f.send();
 check('final-row-exact-data-required',r.ok&&f.focus.native.length===0&&r.menuTarget.windowFocus.callbackSubmitted===false&&r.menuTarget.windowFocus.reason==='registered-focus-owner-changed-before-callback');
 check('final-row-accessor-never-invoked',treeReads===0&&f.keyCalls.length===1&&f.forced.length===0);
},'focus-scope-r2');

// Actual source tree OnActivate and module91780 hook; React, DOM and native
// BrowserView are controlled transports. No hardware input success is inferred.
function popupActivation(options={}){
 const f=model({observableParams:true,globalA:true,single:true,cache:true,...options.model}),c=f.nav,m=f.manager;
 const Dispatcher=new Function('l','return ('+"class o{m_vecCallbacks=[];Register(s){return this.m_vecCallbacks.push(s),{Unregister:()=>{l.x9(this.m_vecCallbacks,s)}}}Dispatch(...s){for(const c of Array.from(this.m_vecCallbacks))c(...s)}ClearAllCallbacks(){this.m_vecCallbacks=[]}CountRegistered(){return this.m_vecCallbacks.length}static PromiseFromAny(s){return new Promise(c=>{let z=[];const f=()=>{z.forEach(g=>g.Unregister()),c()};for(const g of s)z.push(g.Register(f))})}}"+');')({x9(list,value){const i=list.indexOf(value);if(i>=0)list.splice(i,1)}});
 const callbacks=()=>new Dispatcher;
 const methods=new Function('performance','return ({'+Object.values(environment.popupTree).join(',')+'});')(performance);
 const proto={...methods,get id(){return this.m_ID},get Root(){return this.m_Root},get Window(){return this.m_window},get WindowContext(){return this.m_context}};
 const root=c.RootWindow,popup={document:{}};popup.document.defaultView=popup;
 const NodeClass=class {get isConnected(){return this.nativeConnected}};popup.Node=NodeClass;
 const element=Object.assign(new NodeClass,{ownerDocument:popup.document,nativeConnected:true});
 const tree=Object.assign(Object.create(proto),{m_ID:'QuickAccess-NA',m_Controller:m,m_context:c,m_window:popup,
  m_valueIsMounted:{Value:true},m_bIsEnabled:true,m_Root:{m_Tree:null,m_element:element},
  m_onActivateCallbacks:callbacks(),m_onActiveFocusStateChangedCallbacks:callbacks(),m_lastFocusNode:null});
 tree.m_Root.m_Tree=tree;element.__nav_tree=tree;
 m.IsActiveNavTree=t=>t===c.m_LastActiveNavTree;m.IsActiveFocusNavTree=t=>t===c.m_LastActiveFocusNavTree;
 m.BIsInActiveContext=t=>t.m_context===m.m_ActiveContext;
 c.m_rgGamepadNavigationTrees=[tree];
 const calls=[],view={SetTouchGesturesToCancel(){},SetFocus(value){calls.push({value,self:this});if(options.throwFocus)throw Error('uncertain native view focus')}};
 let popupOwner={GetBrowserView:()=>view,GetViewWindow:()=>popup};
 let container,hookTuples=[];
 if(options.nativeFocusData){
  // Controlled generic DOM/native transports stringify as native via Proxy.
  // These are explicit fixture leaves, not real DOM or Steam input evidence.
  const native=fn=>new Proxy(fn,{});
  Object.defineProperties(NodeClass.prototype,{
   ownerDocument:{get:native(function(){return this.nativeDocument}),configurable:true},
   isConnected:{get:native(function(){return this.nativeConnected}),configurable:true},
   contains:{value:native(function(node){return this.nativeChild===node}),configurable:true}
  });
  delete element.ownerDocument;element.nativeDocument=popup.document;
  container=Object.assign(new NodeClass,{nativeConnected:true,nativeDocument:popup.document,nativeChild:element});
  const originalFocus=view.SetFocus;
  view.SetFocus=native(function(value){const r=Reflect.apply(originalFocus,this,[value]);options.nativeFocusMutation?.(f);if(options.promiseFocus)return Promise.resolve('native-unconfirmed');return r});
  const Owner=new Function('return ('+environment.popupNativeFocus.owner+');')();
  popupOwner=new Owner;Object.assign(popupOwner,{m_viewWindow:popup,m_rootElement:container,m_browserView:view});
 }

 const hook=new Function('e','a','r','o','i','n','s','c','l','u','return ('+environment.popupBrowserViewHook+');')(
  {ch:()=>options.nativeFocusData?Object.assign(f.overlay.MenuStore,{ToggleSideMenu(menu,open){check('original-popup-toggle-same-menu',menu===2&&open===true&&f.overlayMenu===2)}}):({ToggleSideMenu(menu,open){check('original-popup-toggle-same-menu',menu===2&&open===true&&f.overlayMenu===2)}})},
  {$2:()=>({BrowserWindow:root})},{useEffect(fn){fn()},useCallback:(fn,deps)=>{hookTuples.push([fn,deps]);return fn}},
  {XX:{RegisterChildWindow(){return()=>{}}}},{},{nN:()=>c},{},{wy:1},{},
  {Fx:()=>()=>{f.activationTail=(f.activationTail??0)+1}});
 const original=hook(2,popupOwner);
 tree.m_onActivateCallbacks.Register((...args)=>{f.previousTree=args[1]});
 let subscription;
 const commitCaptured=active=>{
  subscription?.Unregister();
  const activateCallback=new Function('zt','Tr','return ('+environment.popupOriginalActivatedCallback+');')(active,original.fnOnFocusNavActivated);
  subscription=tree.m_onActivateCallbacks.Register(activateCallback);f.capturedPopupCallback=activateCallback;
 };
 commitCaptured(options.capturedActive!==false);
 const key=f.overlay.OnQuickAccessButtonPressed;
 f.overlay.OnQuickAccessButtonPressed=function(...args){const reply=key.apply(this,args);
  if(f.overlayMenu){c.m_LastActiveFocusNavTree=tree;c.m_LastActiveNavTree=tree;c.m_activeWindow=popup;}
  if(f.overlayMenu===2&&options.commitOnTask)setTimeout(()=>{
   ++f.layoutCommitCount;commitCaptured(true);options.afterCommit?.(f);
  },0);
  options.afterKey?.(f);return reply;
 };
 f.layoutCommitCount=0;
 Object.assign(f,{popupActivation:{tree,proto,popup,element,calls,view,original,root}});
 if(options.nativeFocusData){
  const profiles=environment.popupNativeFocus;
  const makeType=text=>new Function('return ('+text+');')();
  const fiber=(tag,type,stateNode=null,memoizedProps={})=>({tag,type,stateNode,memoizedProps,memoizedState:null,child:null,sibling:null,return:null,alternate:null});
  const leaf=fiber(5,'div',element),b2=fiber(0,makeType(profiles.b2)),ye=fiber(0,makeType(profiles.ye));
  const portal=fiber(4,null,{containerInfo:container}),parent=fiber(15,makeType(profiles.parent)),rootFiber=fiber(3,null);
  const active=options.currentActive!==false,focus=original.fnOnFocusNavActivated;
  const currentCallback=active===(options.capturedActive!==false)?f.capturedPopupCallback:new Function('zt','Tr','return ('+profiles.subscriber+');')(active,focus);
  ye.memoizedProps={active,onFocusNavActivated:focus};ye.memoizedState={memoizedState:[currentCallback,[active,focus]],next:null};
  b2.memoizedProps={navID:'QuickAccess-NA',onActivated:currentCallback,navTreeRef:{current:tree}};
  const focusTuple=hookTuples.find(row=>row[0]===focus);assert(focusTuple);
  parent.memoizedState={memoizedState:focusTuple,next:null};rootFiber.stateNode={current:rootFiber};
  const path=[leaf,b2,ye,portal,parent,rootFiber];for(let i=0;i<path.length-1;++i){path[i].return=path[i+1];path[i+1].child=path[i];}
  Object.defineProperty(element,'__reactFiber$controlled',{value:leaf,writable:true,configurable:true});
  f.popupNativeData={leaf,b2,ye,portal,parent,rootFiber,container,owner:popupOwner,currentCallback};
 }

 // Controlled second CDP evaluation; native admission is tested by its owner.
 const originalSend=f.send;
 f.send=async(...args)=>{
  const r=await originalSend(...args),a=r.menuTarget?.windowFocus?.menuTreeActivation;
  if(a?.status==='pending-browser-task'&&options.nativeFollowup!==false){
   f.firstStageCalls=calls.length;f.pendingTicket=a.ticket;
   await new Promise(resolve=>setTimeout(resolve,0));
   const next=await f.fn({operation:'menu.qam.observe',menu:'quick-access',menuGame:f.proof,menuGamePopupActivation:a.ticket});
   f.followupResult=next;if(next.menuPopupActivation)r.menuTarget.windowFocus.menuTreeActivation=next.menuPopupActivation;
  }return r;
 };return f;
}
add('unregistered held QAM popup invokes its original tree activation once',async()=>{
 const f=popupActivation(),r=await f.send();base(f,r);supplement(f,r);
 const a=r.menuTarget.windowFocus?.menuTreeActivation;
 check('popup-original-tree-activation-submitted',a?.callbackSubmitted===true&&a.status==='original-popup-tree-activation-submitted');
 check('popup-original-browser-view-focus-once',f.popupActivation.calls.length===1&&f.popupActivation.calls[0].value===true&&f.popupActivation.calls[0].self===f.popupActivation.view);
 check('popup-original-activation-tail',f.activationTail===1&&f.nav.ActiveWindow===f.popupActivation.popup);
 check('popup-success-no-current-failure',!Object.prototype.hasOwnProperty.call(a,'currentFailure'));
 check('popup-context-suffix-exact-return-restored',a.contextActivationObservation?.calls===1&&a.contextActivationObservation.returned===1&&a.contextActivationObservation.threw===0&&a.contextActivationObservation.restored===true);
 check('popup-context-method-original-restored',Function.prototype.toString.call(f.nav.OnActivate)===environment.originalContextActivate);
 check('popup-context-phase-is-original-activation-dispatch',a.contextActivationObservation.contextCallPhases['activation-dispatch']===1&&a.contextActivationObservation.contextCallPhases['active-state-dispatch']===0&&a.contextActivationObservation.contextCallPhases['outside-dispatch']===0);
 check('popup-original-dispatch-scopes-restored',Object.values(a.contextActivationObservation.dispatchScopes).every(x=>x.calls===1&&x.returned===1&&x.threw===0&&x.restored===true)&&!Object.hasOwn(f.popupActivation.tree.m_onActivateCallbacks,'Dispatch')&&!Object.hasOwn(f.popupActivation.tree.m_onActiveFocusStateChangedCallbacks,'Dispatch'));

 // Compare the strict class methods against each installed and saved observer.
 // Product VM global sentinels expose null/undefined coercion without live I/O.
 const receiverValues=[null,undefined,17,'receiver',true,Symbol('receiver'),1n];
 const receiverChecks=[];const verifyReceiverChecks=()=>{for(const [name,passed]of receiverChecks.splice(0))check(name,passed)};
 const receiverProbe=(observer,original,label)=>{
  const productGlobal=observer.constructor('return globalThis')();
  const prior=Object.getOwnPropertyDescriptors(productGlobal),seen=[],sentinel=function(...args){seen.push({self:this,args});return true};
  Object.assign(productGlobal,{m_vecCallbacks:[sentinel],BIsActive:()=>true,m_activeWindow:'receiver-window',m_activeBrowserView:undefined});
  const capture=(fn,receiver,args)=>{try{return{kind:'return',value:Reflect.apply(fn,receiver,args)}}catch(error){return{kind:'throw',name:error.name}}};
  try{
   for(const receiver of receiverValues){
    const args=label.includes('context')?['receiver-window']:[];
    const expected=capture(original,receiver,args),before=seen.length,actual=capture(observer,receiver,args);
    receiverChecks.push(['strict-receiver-outcome-'+label+'-'+String(receiver),expected.kind===actual.kind&&(expected.kind==='throw'?expected.name===actual.name:expected.value===actual.value)]);
    receiverChecks.push(['strict-receiver-never-global-'+label+'-'+String(receiver),seen.length===before]);
   }
  }finally{
   for(const key of ['m_vecCallbacks','BIsActive','m_activeWindow','m_activeBrowserView']){if(prior[key])Object.defineProperty(productGlobal,key,prior[key]);else delete productGlobal[key];}
  }
 };
 let savedDispatch,savedContext,originalDispatch,originalContext;
 const strict=popupActivation({afterKey:f=>{
  const tree=f.popupActivation.tree;originalDispatch=Object.getPrototypeOf(tree.m_onActivateCallbacks).Dispatch;originalContext=f.nav.OnActivate;
  const focus=f.popupActivation.view.SetFocus;
  f.popupActivation.view.SetFocus=function(...args){
   savedDispatch=tree.m_onActivateCallbacks.Dispatch;savedContext=f.nav.OnActivate;
   receiverProbe(savedDispatch,originalDispatch,'dispatch-active');receiverProbe(savedContext,originalContext,'context-active');
   return Reflect.apply(focus,this,args);
  };
 }}),strictResult=await strict.send();
 verifyReceiverChecks();
 check('strict-receiver-probes-preserve-original-menu',strictResult.ok&&strict.keyCalls.length===1&&strict.popupActivation.calls.length===1);
 const strictBefore=JSON.stringify(strictResult.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation);
 receiverProbe(savedDispatch,originalDispatch,'dispatch-returned');receiverProbe(savedContext,originalContext,'context-returned');verifyReceiverChecks();
 check('strict-returned-observers-no-late-receipt',JSON.stringify(strictResult.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation)===strictBefore);

 check('popup-profiled-held-original-qam-subscriber',a.contextActivationObservation.subscriptionStatus==='profiled-held-list'&&a.contextActivationObservation.callbackCount===2&&a.contextActivationObservation.originalQamCallbacks===1);
 check('popup-no-registered-callback-or-input-claim',r.menuTarget.windowFocus.callbackSubmitted===false&&a.physicalInputProven===false&&!a.inputCapabilityGranted&&f.forced.length===0);
 const close=await f.send();check('popup-close-no-extra-lifecycle',close.ok&&f.overlayMenu===0&&f.popupActivation.calls.length===1&&!close.menuTarget.windowFocus);
 // Controlled subscription commit uses the original React callback and hook.
 // The host task is a transport for useLayoutEffect, not a full React runtime.
 const late=popupActivation({capturedActive:false,commitOnTask:true}),lr=await late.send();
 check('popup-deferred-layout-commit-before-original-activation',lr.ok&&late.layoutCommitCount===1&&late.popupActivation.calls.length===1);
 check('popup-deferred-original-previous-tree-retained',late.previousTree===late.popupActivation.tree&&lr.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted===true);
 check('popup-one-task-no-input-ack',lr.menuTarget.windowFocus.menuTreeActivation?.activationPhase==='native-followup'&&lr.menuTarget.windowFocus.menuTreeActivation.physicalInputProven===false&&late.keyCalls.length===1&&late.firstStageCalls===0);
 const lc=await late.send();check('popup-deferred-close-preserved',lc.ok&&late.popupActivation.calls.length===1&&late.overlayMenu===0);
 for(const label of ['close','second-game','popup-window','disabled','expired-task','clock-backwards']){
  const revoked=popupActivation({capturedActive:false,commitOnTask:true,afterCommit:f=>{
   if(label==='close')f.setOverlayMenu(0);
   else if(label==='second-game')f.ui.m_runningAppIDs.push(730);
   else if(label==='popup-window')f.popupActivation.tree.m_window={};
   else if(label==='disabled')f.popupActivation.tree.m_bIsEnabled=false;
   else if(label==='expired-task')f.advance(4001);
   else f.advance(-1);
  }}),rr=await revoked.send();
  check('popup-task-final-owner-'+label,rr.ok&&revoked.layoutCommitCount===1&&revoked.popupActivation.calls.length===0&&rr.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted===false);
 }
 const staged=popupActivation({nativeFollowup:false}),sr=await staged.send(),ticket=sr.menuTarget.windowFocus.menuTreeActivation?.ticket;
 check('popup-stage-does-not-activate',sr.ok&&typeof ticket==='string'&&staged.popupActivation.calls.length===0);
 const observe=id=>staged.fn({operation:'menu.qam.observe',menu:'quick-access',menuGame:staged.proof,menuGamePopupActivation:id});
 const wrong=await observe('wrong-ticket');check('popup-wrong-ticket-no-consumption',wrong.ok&&wrong.menuPopupActivation?.callbackSubmitted===false&&staged.popupActivation.calls.length===0);
 const first=await observe(ticket),twice=await observe(ticket);
 check('popup-native-ticket-once',first.ok&&first.menuPopupActivation?.callbackSubmitted===true&&twice.ok&&twice.menuPopupActivation?.callbackSubmitted===false&&staged.popupActivation.calls.length===1&&staged.keyCalls.length===1);
 const replaced=popupActivation({nativeFollowup:false}),old=await replaced.send(),oldTicket=old.menuTarget.windowFocus.menuTreeActivation?.ticket;
 await replaced.send();const cancelled=await replaced.fn({operation:'menu.qam.observe',menu:'quick-access',menuGame:replaced.proof,menuGamePopupActivation:oldTicket});
 check('popup-close-retires-native-ticket',cancelled.ok&&cancelled.menuPopupActivation?.callbackSubmitted===false&&replaced.popupActivation.calls.length===0&&replaced.overlayMenu===0);
},'popup-activation');
add('popup activation rejects wrong tree mount unknown method and duplicate context row',async()=>{
 for(const [label,change]of [
  ['wrong-id',f=>f.popupActivation.tree.m_ID='other-game-menu'],
  ['wrong-context',f=>f.popupActivation.tree.m_context={}],
  ['different-root',f=>f.popupActivation.tree.m_Root.m_Tree={}],
  ['foreign-document',f=>f.popupActivation.element.ownerDocument={}],
  ['detached-element',f=>f.popupActivation.element.nativeConnected=false],
  ['duplicate-row',f=>f.nav.m_rgGamepadNavigationTrees.push(f.popupActivation.tree)],
  ['unknown-activate',f=>f.popupActivation.tree.OnActivate=()=>{throw Error('foreign')}],
  ['method-accessor',f=>Object.defineProperty(f.popupActivation.tree,'OnActivate',{get(){throw Error('must not invoke')}})]
 ]){const f=popupActivation({afterKey:change}),r=await f.send();check('popup-reject-'+label,r.ok&&f.keyCalls.length===1&&f.popupActivation.calls.length===0&&r.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted!==true)}


 for(const phase of ['active-state-dispatch','outside-dispatch']){
  const f=popupActivation({capturedActive:false,afterKey:f=>{
   const call=()=>f.nav.OnActivate(f.popupActivation.popup);
   if(phase==='active-state-dispatch')f.popupActivation.tree.m_onActiveFocusStateChangedCallbacks.Register(call);
   else f.popupActivation.tree.m_lastFocusNode={Parent:null,SetFocusWithin:call};
  }}),r=await f.send(),o=r.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation;
  check('context-other-phase-no-browser-focus-'+phase,f.popupActivation.calls.length===0&&o.calls===1&&o.returned===1&&o.contextCallPhases[phase]===1&&o.contextCallPhases['activation-dispatch']===0);
  check('context-other-phase-restores-'+phase,o.restored&&Object.values(o.dispatchScopes).every(x=>x.restored));
 }
 for(const label of ['saved-reference','descriptor-change','unknown-method','foreign-this']){
  let captured,slot,changed;const f=popupActivation({afterKey:f=>{
   slot=f.popupActivation.tree.m_onActivateCallbacks;
   if(label==='unknown-method'){const original=slot.Dispatch;slot.Dispatch=function(...args){return Reflect.apply(original,this,args)};}
   const focus=f.popupActivation.view.SetFocus;
   f.popupActivation.view.SetFocus=function(...args){
    captured=slot.Dispatch;
    if(label==='descriptor-change'){Object.defineProperty(slot,'Dispatch',{enumerable:!Object.getOwnPropertyDescriptor(slot,'Dispatch').enumerable});changed=Object.getOwnPropertyDescriptor(slot,'Dispatch');}
    if(label==='foreign-this')Reflect.apply(captured,{m_vecCallbacks:[()=>f.nav.OnActivate(f.popupActivation.popup)]},[true,f.popupActivation.tree]);
    return Reflect.apply(focus,this,args);
   };
  }}),r=await f.send(),o=r.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation,d=o.dispatchScopes['activation-dispatch'];
  check('dispatch-observation-keeps-single-action-'+label,r.ok&&f.keyCalls.length===1&&f.popupActivation.calls.length===1);
  if(label==='unknown-method')check('dispatch-profile-unknown-keeps-context-unclassified',d.status==='method-profile-unknown'&&!d.restored&&o.contextCallPhases.unclassified===1);
  else if(label==='descriptor-change')check('dispatch-descriptor-cas-preserves-new-owner',d.status==='slot-descriptor-changed-during-callback'&&!d.restored&&slot.Dispatch===captured&&Object.getOwnPropertyDescriptor(slot,'Dispatch').enumerable===changed.enumerable);
  else check('dispatch-exact-original-slot-restored-'+label,d.restored&&!Object.hasOwn(slot,'Dispatch'));
  if(label==='foreign-this')check('dispatch-foreign-this-unclassified',d.foreignCalls===1&&o.contextCallPhases.unclassified===1&&o.contextCallPhases['activation-dispatch']===1);
  if(label!=='unknown-method'){
   const before=JSON.stringify(o);Reflect.apply(captured,{m_vecCallbacks:[()=>{}]},[null,null]);
   check('dispatch-saved-reference-no-late-observation-'+label,JSON.stringify(o)===before);
  }
 }

 for(const label of ['inherited','fixed-writable','fixed-readonly','accessor','unknown-method','replacement']){
  let original,own,prototype,reads=0,replacement;
  const f=popupActivation({afterKey:f=>{
   const c=f.nav;own=Object.getOwnPropertyDescriptor(c,'OnActivate');original=own.value;prototype=Object.getPrototypeOf(c);
   if(label==='inherited'){const next=Object.create(prototype);Object.defineProperty(next,'OnActivate',own);Object.setPrototypeOf(c,next);delete c.OnActivate;}
   if(label==='fixed-writable')Object.defineProperty(c,'OnActivate',{...own,configurable:false});
   if(label==='fixed-readonly')Object.defineProperty(c,'OnActivate',{...own,configurable:false,writable:false});
   if(label==='accessor')Object.defineProperty(c,'OnActivate',{get(){++reads;return original},configurable:true});
   if(label==='unknown-method')c.OnActivate=function(...args){return Reflect.apply(original,this,args)};
   if(label==='replacement'){
    const focus=f.popupActivation.view.SetFocus;replacement=function(...args){return Reflect.apply(original,this,args)};
    f.popupActivation.view.SetFocus=function(...args){const value=Reflect.apply(focus,this,args);c.OnActivate=replacement;return value};
   }
  }}),r=await f.send(),a=r.menuTarget.windowFocus.menuTreeActivation,o=a.contextActivationObservation;
  check('suffix-preserves-action-'+label,r.ok&&a.callbackSubmitted===true&&f.popupActivation.calls.length===1&&f.keyCalls.length===1);
  if(label==='inherited')check('suffix-inherited-slot-deleted',o?.calls===1&&o.returned===1&&o.restored===true&&!Object.hasOwn(f.nav,'OnActivate')&&f.nav.OnActivate===original);
  else if(label==='fixed-writable')check('suffix-fixed-writable-restored',o?.restored===true&&Object.getOwnPropertyDescriptor(f.nav,'OnActivate').value===original&&Object.getOwnPropertyDescriptor(f.nav,'OnActivate').configurable===false);
  else if(label==='fixed-readonly')check('suffix-readonly-unknown-keeps-action',o?.status==='slot-not-reversible'&&o.calls===0&&f.nav.OnActivate===original);
  else if(label==='accessor')check('suffix-accessor-no-extra-read',o?.status==='method-profile-unknown'&&reads===1&&typeof Object.getOwnPropertyDescriptor(f.nav,'OnActivate').get==='function');
  else if(label==='unknown-method')check('suffix-unknown-method-keeps-action',o?.status==='method-profile-unknown'&&o.calls===0);
  else check('suffix-does-not-clobber-replacement',o?.status==='slot-replaced-during-callback'&&o.restored===false&&f.nav.OnActivate===replacement);
 }
 for(const label of ['same-value-flags','locked-slot','saved-reference','prototype-change']){
  let captured,descriptorAfter,original,own;
  const f=popupActivation({afterKey:f=>{
   const c=f.nav;own=Object.getOwnPropertyDescriptor(c,'OnActivate');original=own.value;
   if(label==='prototype-change'){const p=Object.create(Object.getPrototypeOf(c));Object.defineProperty(p,'OnActivate',own);Object.setPrototypeOf(c,p);delete c.OnActivate;}
   const focus=f.popupActivation.view.SetFocus;
   f.popupActivation.view.SetFocus=function(...args){
    captured=c.OnActivate;
    if(label==='same-value-flags')Object.defineProperty(c,'OnActivate',{enumerable:!Object.getOwnPropertyDescriptor(c,'OnActivate').enumerable});
    if(label==='locked-slot')Object.defineProperty(c,'OnActivate',{configurable:false,writable:false});
    if(label==='prototype-change')Object.setPrototypeOf(c,{OnActivate:original});
    descriptorAfter=Object.getOwnPropertyDescriptor(c,'OnActivate');
    return Reflect.apply(focus,this,args);
   };
  }}),r=await f.send(),a=r.menuTarget.windowFocus.menuTreeActivation,o=a.contextActivationObservation;
  check('suffix-review-preserves-action-'+label,r.ok&&a.callbackSubmitted===true&&f.popupActivation.calls.length===1&&f.keyCalls.length===1);
  if(label==='same-value-flags'||label==='locked-slot')check('suffix-review-full-descriptor-cas-'+label,o?.status==='slot-descriptor-changed-during-callback'&&!o.restored&&JSON.stringify(Object.getOwnPropertyDescriptor(f.nav,'OnActivate'))===JSON.stringify(descriptorAfter)&&f.nav.OnActivate===captured);
  if(label==='prototype-change')check('suffix-review-prototype-change-unknown',o?.restored===true&&o.status==='prototype-source-changed-after-observation'&&!Object.hasOwn(f.nav,'OnActivate'));
  const before=JSON.stringify(o),returned=Reflect.apply(captured,f.nav,[f.popupActivation.popup]);
  check('suffix-review-saved-reference-no-late-observation-'+label,JSON.stringify(o)===before&&returned===undefined);
 }

 for(const label of ['unknown-dispatch','duplicate-qam','missing-qam','accessor-index','oversize']){
  let reads=0;
  const f=popupActivation({afterKey:f=>{
   const callbacks=f.popupActivation.tree.m_onActivateCallbacks,list=callbacks.m_vecCallbacks;
   if(label==='unknown-dispatch'){const original=callbacks.Dispatch;callbacks.Dispatch=function(...args){return Reflect.apply(original,this,args)};}
   if(label==='duplicate-qam')list.push(list[1]);
   if(label==='missing-qam')list.splice(1,1);
   if(label==='accessor-index'){const fn=list[1];Object.defineProperty(list,'1',{get(){++reads;return fn},configurable:true});}
   if(label==='oversize')while(list.length<65)list.push(()=>{});
  }}),r=await f.send(),o=r.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation;
  check('suffix-subscription-action-preserved-'+label,r.ok&&f.keyCalls.length===1&&f.popupActivation.calls.length===(label==='duplicate-qam'?2:label==='missing-qam'?0:1));
  if(label==='duplicate-qam'||label==='missing-qam')check('suffix-subscription-exact-count-'+label,o?.subscriptionStatus==='profiled-held-list'&&o.originalQamCallbacks===(label==='duplicate-qam'?2:0));
  else check('suffix-subscription-unknown-'+label,o?.subscriptionStatus==='unknown'&&o.callbackCount===null&&o.originalQamCallbacks===null);
  if(label==='accessor-index')check('suffix-subscription-index-getter-only-original-dispatch',reads===1);
 }

},'popup-activation');
add('final popup current source revocation refuses activation',async()=>{
 for(const label of ['window','document','game-list']){
  const f=popupActivation();let reads=0;Object.defineProperty(f.popupActivation.element,'isConnected',{get(){
   if(++reads===1){if(label==='window')f.popupActivation.tree.m_window={};else if(label==='document')f.popupActivation.popup.document={};else f.ui.m_runningAppIDs.push(730)}return true;
  }});const r=await f.send();check('popup-final-revoke-'+label,r.ok&&f.popupActivation.calls.length===0&&r.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted!==true);
 }
},'popup-activation');
add('uncertain popup activation is not retried or converted into key replay',async()=>{
 const f=popupActivation({throwFocus:true}),r=await f.send();
 check('popup-uncertain-once',r.ok&&f.keyCalls.length===1&&f.popupActivation.calls.length===1&&r.menuTarget.windowFocus.menuTreeActivation.callbackSubmitted===true&&r.menuTarget.windowFocus.menuTreeActivation.status==='original-popup-tree-activation-uncertain');
 const observation=r.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation;check('popup-throw-before-suffix-restores',observation?.calls===0&&observation.restored===true);
 check('popup-throw-restores-both-dispatch-slots',Object.values(observation.dispatchScopes).every(x=>x.restored)&&observation.dispatchScopes['activation-dispatch'].threw===1&&observation.dispatchScopes['active-state-dispatch'].calls===0);
},'popup-activation');
add('exact multiple inspect main and registered trees preserve prior routes',async()=>{
 for(const opts of [{healthy:true,single:true},{single:false}]){
  const f=popupActivation({model:opts}),r=await f.send();check('popup-excluded-alias-form',r.ok&&f.popupActivation.calls.length===0&&!r.menuTarget.windowFocus);
 }
 const f=popupActivation(),r=await f.send('menu.inspect');check('popup-inspect-zero',r.ok&&f.keyCalls.length===0&&f.popupActivation.calls.length===0);
 const main=popupActivation(),mr=await main.send('menu.button','main');check('popup-main-zero',mr.ok&&main.popupActivation.calls.length===0&&!mr.menuTarget.windowFocus?.menuTreeActivation);
 const reg=registeredFocus({popup:true}),rr=await reg.send();check('popup-registered-path-preserved',rr.ok&&reg.focus.native.length===2&&rr.menuTarget.windowFocus.callbackSubmitted===true&&!rr.menuTarget.windowFocus.menuTreeActivation);
},'popup-activation');

add('late original composition observation cannot redirect a held popup activation',async()=>{
 const f=popupActivation();let armed=false;
 Object.defineProperty(f.popupActivation.element,'isConnected',{get(){armed=true;return true}});
 const cs=f.overlay.CompositionStateStore,original=cs.GetCompositionState;
 cs.GetCompositionState=function(){const state=original.call(this);if(armed){armed=false;f.popupActivation.tree.m_window={};}return state};
 const r=await f.send();check('popup-late-owner-revoked',r.ok&&f.keyCalls.length===1&&f.popupActivation.calls.length===0&&r.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted===false);
 check('popup-late-owner-reason-preserved',r.menuTarget.windowFocus.menuTreeActivation?.reason==='popup-tree-owner-changed-before-activation');
 check('popup-late-owner-first-failure-known',r.menuTarget.windowFocus.menuTreeActivation?.currentFailure==='tree-popup');
},'popup-late');
add('last original read cannot activate disabled unmounted or detached popup',async()=>{
 for(const label of ['disabled','unmounted','detached']){
  const f=popupActivation();let armed=false;Object.defineProperty(f.popupActivation.element,'isConnected',{get(){armed=true;return true}});
  const cs=f.overlay.CompositionStateStore,original=cs.GetCompositionState;
  cs.GetCompositionState=function(){const state=original.call(this);if(armed){armed=false;if(label==='disabled')f.popupActivation.tree.m_bIsEnabled=false;else if(label==='unmounted')f.popupActivation.tree.m_valueIsMounted.Value=false;else f.popupActivation.element.nativeConnected=false}return state};
  const r=await f.send();check('popup-final-mount-revoke-'+label,r.ok&&f.popupActivation.calls.length===0&&r.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted===false);
 }
},'popup-mount-final');
add('inactive captured QAM callback preserves original same-tree ancestry filter',async()=>{
 const f=popupActivation({capturedActive:false}),r=await f.send();
 check('popup-original-previous-tree-retained',r.ok&&r.menuTarget.windowFocus.menuTreeActivation?.callbackSubmitted===true&&f.previousTree===f.popupActivation.tree);
 check('popup-inactive-captured-filter-preserved',f.popupActivation.calls.length===0&&f.keyCalls.length===1);
 const observation=r.menuTarget.windowFocus.menuTreeActivation.contextActivationObservation;check('popup-inactive-context-suffix-absent-restored',observation?.calls===0&&observation.returned===0&&observation.restored===true);
},'popup-previous');

add('committed held popup records its actual native focus without adding actions',async()=>{
 const f=popupActivation({nativeFocusData:true}),r=await f.send(),a=r.menuTarget.windowFocus.menuTreeActivation,n=a.nativeFocusObservation;
 check('native-popup-holder-bound',n?.status==='committed-popup-holder-confirmed'&&n.registeredIsCurrent===true);
 check('native-popup-one-actual-focus',n.nativeFocus.calls===1&&n.nativeFocus.trueCalls===1&&n.nativeFocus.falseCalls===0&&n.nativeFocus.returned===1&&n.nativeFocus.threw===0);
 check('native-popup-restored-and-no-ack',n.nativeFocus.restored&&n.nativeFocus.status==='original-native-focus-scope-observed'&&n.nativeFocus.nativeInputAcceptance==='UNKNOWN'&&n.nativeFocus.physicalInputProven===false);
 check('native-popup-original-action-once',r.ok&&f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.previousTree===f.popupActivation.tree&&f.activationTail===1);
 const before=f.popupActivation.calls.length;await f.send();check('native-popup-close-preserved',f.overlayMenu===0&&f.popupActivation.calls.length===before);
},'popup-native-focus-positive');
add('native focus observation refuses foreign holders and preserves original completion',async()=>{
 for(const [label,change]of [
  ['root-current',f=>f.popupNativeData.rootFiber.stateNode.current={}],
  ['current-b2',f=>f.popupNativeData.b2.memoizedProps.onActivated=()=>{}],
  ['tree-ref',f=>f.popupNativeData.b2.memoizedProps.navTreeRef.current={}],
  ['portal',f=>f.popupNativeData.portal.stateNode.containerInfo={}],
  ['holder-window',f=>f.popupNativeData.owner.m_viewWindow={}],
  ['holder-root',f=>f.popupNativeData.owner.m_rootElement={}],
  ['focus-accessor',f=>Object.defineProperty(f.popupActivation.view,'SetFocus',{get(){return f.originalNativeFocus},configurable:true})],
  ['hook-ambiguous',f=>{const h=f.popupNativeData.ye.memoizedState;h.next={memoizedState:h.memoizedState,next:null}}],
  ['parent-source',f=>f.popupNativeData.parent.type=()=>{}],
  ['foreign-document',f=>f.popupNativeData.container.nativeDocument={}],
  ['detached-container',f=>f.popupNativeData.container.nativeConnected=false],
  ['fiber-accessor',f=>Object.defineProperty(f.popupActivation.element,'__reactFiber$controlled',{get(){++f.fiberGetterReads;return f.popupNativeData.leaf},configurable:true})],
  ['ambiguous-fiber',f=>f.popupActivation.element.__reactFiber$other=f.popupNativeData.leaf],
  ['frozen-native-slot',f=>Object.defineProperty(f.popupActivation.view,'SetFocus',{value:f.popupActivation.view.SetFocus,writable:false,configurable:false})]
 ]){
  const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.fiberGetterReads=0;f.originalNativeFocus=f.popupActivation.view.SetFocus;change(f)}}),r=await f.send(),n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
  check('native-popup-no-foreign-observation-'+label,n?.nativeFocus?.calls!==1&&f.popupActivation.calls.length===1&&f.keyCalls.length===1);
  check('native-popup-no-fiber-getter-'+label,f.fiberGetterReads===0);
 }
 const inactive=popupActivation({nativeFocusData:true,capturedActive:false,currentActive:false}),ir=await inactive.send();
 check('native-popup-inactive-unchanged',inactive.popupActivation.calls.length===0&&inactive.keyCalls.length===1&&ir.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation.status==='committed-component-not-active');
 const stale=popupActivation({nativeFocusData:true,capturedActive:false}),sr=await stale.send(),sn=sr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
 check('native-popup-no-stale-repair',sn.status==='committed-popup-holder-confirmed'&&sn.registeredIsCurrent===false&&sn.nativeFocus.calls===0&&stale.popupActivation.calls.length===0&&stale.keyCalls.length===1);
},'popup-native-focus-negative');
add('native focus scoped observer preserves throws replacements and saved strict receivers',async()=>{
 const thrown=popupActivation({nativeFocusData:true,throwFocus:true}),tr=await thrown.send(),tn=tr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation.nativeFocus;
 check('native-popup-throw-no-replay',tn.calls===1&&tn.threw===1&&tn.returned===0&&tn.restored&&thrown.popupActivation.calls.length===1&&thrown.keyCalls.length===1&&tr.menuTarget.windowFocus.menuTreeActivation.status==='original-popup-tree-activation-uncertain');
 const promise=popupActivation({nativeFocusData:true,promiseFocus:true}),pr=await promise.send(),pn=pr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation.nativeFocus;
 check('native-popup-promise-not-ack-or-replayed',pn.calls===1&&pn.returned===1&&pn.restored&&pn.nativeInputAcceptance==='UNKNOWN'&&promise.popupActivation.calls.length===1);
 for(const label of ['replacement','descriptor','holder']){
  let saved,replacement;const f=popupActivation({nativeFocusData:true,nativeFocusMutation:f=>{
   saved=f.popupActivation.view.SetFocus;
   if(label==='replacement'){replacement=()=>{};f.popupActivation.view.SetFocus=replacement;}
   else if(label==='descriptor'){const d=Object.getOwnPropertyDescriptor(f.popupActivation.view,'SetFocus');Object.defineProperty(f.popupActivation.view,'SetFocus',{...d,enumerable:!d.enumerable});}
   else f.popupNativeData.owner.m_viewWindow={};
  }}),r=await f.send(),n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation.nativeFocus;
  check('native-popup-original-preserved-'+label,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&n.calls===1);
  check('native-popup-restoration-owner-'+label,label==='holder'?n.restored&&n.status==='holder-changed-after-observation':!n.restored&&n.status===(label==='replacement'?'slot-replaced-during-callback':'slot-descriptor-changed-during-callback'));
  if(label==='replacement')check('native-popup-new-method-preserved',f.popupActivation.view.SetFocus===replacement);
  const before=JSON.stringify(n);try{Reflect.apply(saved,null,[true])}catch{}check('native-popup-saved-observer-no-late-note-'+label,JSON.stringify(n)===before);
 }
},'popup-native-focus-lifetime');

 const sharedNativePopup=f=>{
  const n=f.popupNativeData,original=n.parent,root=n.rootFiber;
  const oldRoot={...root,child:null,alternate:root},oldParent={...original,return:oldRoot,alternate:original};
  root.alternate=oldRoot;original.alternate=oldParent;oldRoot.child=oldParent;
  // Both parents share the committed Portal, whose return names oldParent.
  n.portal.return=oldParent;return{oldRoot,oldParent,root,original};
 };
 add('current popup observation follows reciprocal shared children to its current root',async()=>{
  const normal=popupActivation({nativeFocusData:true}),nr=await normal.send();
  check('shared-normal-original-still-bound',nr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation.nativeFocus.calls===1);
  const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.sharedGraph=sharedNativePopup(f)}}),r=await f.send();
  const n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
  check('shared-current-holder-bound',n.status==='committed-popup-holder-confirmed'&&n.registeredIsCurrent===true);
  check('shared-current-unique-path',n.currentPathCount===1&&n.sharedParentTransitions>0&&n.componentMatchCount===1);
  check('shared-original-native-once',n.nativeFocus.calls===1&&n.nativeFocus.trueCalls===1&&n.nativeFocus.returned===1&&n.nativeFocus.restored===true);
  check('shared-no-input-ack',n.nativeFocus.nativeInputAcceptance==='UNKNOWN'&&n.inputCapabilityGranted===false&&n.physicalInputProven===false);
  check('shared-original-actions-once',f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
 },'popup-native-focus-shared-child');
 add('shared child observation rejects unowned ambiguous and accessor parent paths',async()=>{
  for(const [label,change]of[
   ['nonreciprocal',g=>{g.oldParent.alternate=null}],
   ['different-child',g=>{g.original.child=null}],
   ['wrong-current-root',g=>{g.root.stateNode={current:null};g.oldRoot.stateNode=g.root.stateNode}],
   ['two-current-roots',g=>{g.oldRoot.stateNode={current:g.oldRoot}}],
   ['accessor-child',(g,f)=>{Object.defineProperty(g.original,'child',{get(){++f.sharedGetterReads;return f.popupNativeData.portal},configurable:true})}],
   ['sibling-cycle',(g,f)=>{f.popupNativeData.portal.sibling=f.popupNativeData.portal}],
  ]){
   const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.sharedGetterReads=0;change(sharedNativePopup(f),f)}}),r=await f.send();
   const n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
   check('shared-refused-'+label,n.nativeFocus.calls===0&&n.nativeFocus.status==='unavailable');
   check('shared-original-preserved-'+label,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
   check('shared-no-getter-'+label,f.sharedGetterReads===0);
  }
 },'popup-native-focus-shared-child');

 const commonReturnPopup=f=>{
  const n=f.popupNativeData,root=n.rootFiber,parent=n.b2;
  const oldRoot={...root,child:null,alternate:root},oldParent={...parent,return:oldRoot,alternate:parent};
  const alternate={...n.leaf,return:oldParent,alternate:n.leaf};
  root.alternate=oldRoot;parent.alternate=oldParent;oldRoot.child=oldParent;
  oldParent.child=n.leaf;parent.child=alternate;n.leaf.return=oldParent;n.leaf.alternate=alternate;
  return{oldRoot,oldParent,root,parent,alternate};
 };
 add('current popup resolves paired shared return without mixing roots or old ancestors',async()=>{
  const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.commonGraph=commonReturnPopup(f)}}),r=await f.send();
  const n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
  check('common-current-holder-bound',n.status==='committed-popup-holder-confirmed'&&n.registeredIsCurrent===true);
  check('common-unique-child-path',n.currentPathCount===1&&n.sharedReturnTransitions===1&&n.componentMatchCount===1);
  check('common-native-original-once',n.nativeFocus.calls===1&&n.nativeFocus.trueCalls===1&&n.nativeFocus.restored===true&&f.popupActivation.calls.length===1&&f.keyCalls.length===1);
  check('common-observation-not-ack',n.nativeFocus.nativeInputAcceptance==='UNKNOWN'&&n.physicalInputProven===false);
  const sh=popupActivation({nativeFocusData:true,afterKey:f=>{sharedNativePopup(f)}}),shr=await sh.send();
  const sn=shr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
  check('common-shared-head-still-bound',sn.status==='committed-popup-holder-confirmed'&&sn.sharedParentTransitions>0&&sn.nativeFocus.calls===1);
  for(const [label,change]of[
   ['foreign-current-root',g=>{g.root.stateNode={current:g.root}}],
   ['nonreciprocal-child',g=>{g.alternate.alternate=null}],
   ['missing-other-child',g=>{g.oldParent.child=null}],
   ['nonreciprocal-parent',g=>{g.oldParent.alternate=null}],
   ['accessor-peer-child',(g,f)=>{Object.defineProperty(g.parent,'child',{get(){++f.commonGetterReads;return g.alternate},configurable:true})}],
  ]){
   const x=popupActivation({nativeFocusData:true,afterKey:f=>{f.commonGetterReads=0;change(commonReturnPopup(f),f)}}),xr=await x.send();
   const q=xr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
   check('common-refused-'+label,q.nativeFocus.calls===0&&q.nativeFocus.status==='unavailable');
   check('common-original-preserved-'+label,x.popupActivation.calls.length===1&&x.keyCalls.length===1&&x.activationTail===1);
   check('common-no-getter-'+label,x.commonGetterReads===0);
  }
 },'popup-native-focus-shared-return');

 add('paired current popup rejects noncomplementary lists and detached root ownership',async()=>{
  for(const [label,change]of[
   ['old-root-different-state',g=>{g.oldRoot.stateNode={current:null}}],
   ['current-outside-root-pair',g=>{g.root.stateNode.current={tag:3,stateNode:g.root.stateNode}}],
   ['parent-list-both-children',(g,f)=>{g.oldParent.child=f.popupNativeData.leaf;f.popupNativeData.leaf.sibling=g.alternate}],
   ['both-parent-lists-same-member',(g,f)=>{const head={sibling:g.alternate};g.oldParent.child=head}],
  ]){
   const f=popupActivation({nativeFocusData:true,afterKey:f=>{change(commonReturnPopup(f),f)}}),r=await f.send();
   const n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
   check('paired-current-refused-'+label,n.nativeFocus.calls===0&&n.nativeFocus.status==='unavailable');
   check('paired-current-action-preserved-'+label,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
  }
 },'popup-native-focus-pair-guard');
 
  const deepNativePopup=(f,depth)=>{
    const n=f.popupNativeData,nodes=[];let child=n.parent;
    for(let i=0;i<depth-6;++i){
      const parent={tag:0,type:null,stateNode:null,memoizedProps:{},memoizedState:null,
        child,sibling:null,return:null,alternate:null};
      child.return=parent;nodes.push(parent);child=parent;
    }
    child.return=n.rootFiber;n.rootFiber.child=child;
    return{nodes,depth};
  };
  add('committed popup observation bounds deep current paths and explains rejected roots',async()=>{
    for(const depth of [97,160,256]){
      const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.deepGraph=deepNativePopup(f,depth)}}),r=await f.send();
      const n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
      check('deep-holder-bound-'+depth,n.status==='committed-popup-holder-confirmed'&&n.registeredIsCurrent===true);
      check('deep-exact-path-'+depth,n.rootWalkSteps===depth&&n.rootWalkStop==='host-root-confirmed'&&n.rootWalkLimit===256&&n.currentPathCount===1&&n.currentPathDepth===depth);
      check('deep-original-focus-once-'+depth,n.nativeFocus.calls===1&&n.nativeFocus.trueCalls===1&&n.nativeFocus.returned===1&&n.nativeFocus.restored===true);
      check('deep-original-actions-once-'+depth,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
      check('deep-no-input-ack-'+depth,n.nativeFocus.nativeInputAcceptance==='UNKNOWN'&&n.physicalInputProven===false&&n.inputCapabilityGranted===false);
    }
    for(const [label,depth,change,stop]of[
      ['depth-budget',257,()=>{},'depth-budget'],
      ['cycle',160,(f,g)=>{g.nodes[95].return=g.nodes[0]},'cycle'],
      ['wrong-current',160,f=>{f.popupNativeData.rootFiber.stateNode.current={}},'root-current-unconfirmed'],
      ['nonreciprocal-root',160,f=>{const root=f.popupNativeData.rootFiber;root.alternate={tag:3,stateNode:root.stateNode,alternate:null}},'root-pair-unconfirmed'],
      ['holder-window',160,f=>{f.popupNativeData.owner.m_viewWindow={}},'host-root-confirmed'],
      ['return-accessor',160,(f,g)=>{Object.defineProperty(g.nodes[80],'return',{get(){++f.deepGetterReads;return f.popupNativeData.rootFiber},configurable:true})},null],
    ]){
      const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.deepGetterReads=0;const g=deepNativePopup(f,depth);change(f,g)}}),r=await f.send();
      const n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
      check('deep-refused-'+label,n.nativeFocus.calls===0&&n.nativeFocus.status==='unavailable');
      check('deep-original-preserved-'+label,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
      check('deep-no-getter-'+label,f.deepGetterReads===0);
      if(stop)check('deep-reason-'+label,n.rootWalkStop===stop);
      if(label==='depth-budget')check('deep-limit-is-finite',n.rootWalkSteps===256&&n.reads===258);
    }
  },'popup-native-focus-depth');

  add('deep popup uniqueness remains unknown when an alternate path search is incomplete',async()=>{
    const f=popupActivation({nativeFocusData:true,afterKey:f=>{
      const n=f.popupNativeData;deepNativePopup(f,97);
      const alternate={...n.leaf,return:null,alternate:n.leaf};n.leaf.alternate=alternate;
      let child=alternate;
      for(let i=0;i<255;++i){
        const parent={tag:0,type:null,stateNode:null,memoizedProps:{},memoizedState:null,
          child,sibling:null,return:null,alternate:null};child.return=parent;child=parent;
      }
      child.return=n.rootFiber;n.rootFiber.child.sibling=child;
    }}),r=await f.send(),n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
    check('deep-incomplete-never-unique',n.status==='committed-component-unconfirmed'&&n.pathSearchIncomplete===true&&n.currentPathCount===1);
    check('deep-incomplete-no-native-observer',n.nativeFocus.calls===0&&n.nativeFocus.status==='unavailable');
    check('deep-incomplete-original-preserved',f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
    const good=popupActivation({nativeFocusData:true,afterKey:f=>{deepNativePopup(f,160)}}),gr=await good.send();
    const gn=gr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
    check('deep-complete-still-bound',gn.status==='committed-popup-holder-confirmed'&&gn.pathSearchIncomplete===false&&gn.currentPathDepth===160);
    check('deep-complete-original-native-once',gn.nativeFocus.calls===1&&gn.nativeFocus.returned===1&&gn.nativeFocus.restored&&good.popupActivation.calls.length===1&&good.keyCalls.length===1);
  },'popup-native-focus-depth-path');

  add('official Decky own-data wrapper provenance binds only the held current popup observer',async()=>{
    // Exact218-byte FunctionExpression from official v3.3.1 chunk-sIGTtP5a.js SHA9603df1b, byte[2666,2884).
    const deckyAfterWrapper="function (...args) {\n        let ret = patch.original.call(this, ...args);\n        ret = handler.call(this, args, ret);\n        if (options.singleShot) {\n            patch.unpatch();\n        }\n        return ret;\n    }";
    const wrap=(node,f)=>{
      const original=node.type,object={type:original},patch={object,property:'type',original,patchedFunction:null,hasUnpatched:false};
      const handler=(args,ret)=>ret,options={};
      const fn=new Function('patch','handler','options','return ('+deckyAfterWrapper+');')(patch,handler,options);
      object.type=fn;patch.patchedFunction=fn;fn.__deckyPatch=patch;
      Object.defineProperty(fn,'__deckyOrig',{get(){++f.wrapperGetterReads;throw Error('must not invoke original getter')}});
      fn.toString=()=>{++f.wrapperGetterReads;throw Error('must not invoke custom toString')};
      node.type=fn;return patch;
    };
    for(const depth of [1,2,4]){
      const f=popupActivation({nativeFocusData:true,afterKey:f=>{
        f.wrapperGetterReads=0;for(let i=0;i<depth;++i)wrap(f.popupNativeData.ye,f);
        wrap(f.popupNativeData.parent,f);
      }}),r=await f.send(),n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
      check('decky-wrapper-holder-'+depth,n.status==='committed-popup-holder-confirmed'&&n.registeredIsCurrent===true&&n.componentWrapperDepth===depth);
      check('decky-wrapper-original-native-'+depth,n.nativeFocus.calls===1&&n.nativeFocus.trueCalls===1&&n.nativeFocus.returned===1&&n.nativeFocus.restored===true);
      check('decky-wrapper-no-getters-'+depth,f.wrapperGetterReads===0);
      check('decky-wrapper-no-new-actions-'+depth,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1&&n.inputCapabilityGranted===false&&n.physicalInputProven===false);
    }
    for(const [label,change]of[
      ['wrong-original',(f,p)=>{p.original=function foreign(){}}],
      ['wrong-backref',(f,p)=>{p.patchedFunction=function foreign(){}}],
      ['already-unpatched',(f,p)=>{p.hasUnpatched=true}],
      ['wrong-property',(f,p)=>{p.property='render'}],
      ['replaced-slot',(f,p)=>{p.object.type=function foreign(){}}],
      ['cycle',(f,p)=>{p.original=p.patchedFunction}],
      ['depth-five',f=>{for(let i=0;i<4;++i)wrap(f.popupNativeData.ye,f)}],
      ['patch-accessor',(f,p)=>{Object.defineProperty(f.popupNativeData.ye.type,'__deckyPatch',{get(){++f.wrapperGetterReads;return p}})}],
      ['original-accessor',(f,p)=>{const original=p.original;Object.defineProperty(p,'original',{get(){++f.wrapperGetterReads;return original}})}],
      ['foreign-wrapper-source',f=>{const original=f.popupNativeData.ye.type;const foreign=function foreign(){return original()};foreign.__deckyPatch=original.__deckyPatch;f.popupNativeData.ye.type=foreign}],
    ]){
      const f=popupActivation({nativeFocusData:true,afterKey:f=>{f.wrapperGetterReads=0;change(f,wrap(f.popupNativeData.ye,f))}}),r=await f.send(),n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
      check('decky-wrapper-refused-'+label,n.nativeFocus.calls===0&&n.nativeFocus.status==='unavailable');
      check('decky-wrapper-no-getter-'+label,f.wrapperGetterReads===0);
      check('decky-wrapper-preserved-'+label,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1);
    }
  },'popup-native-focus-decky-wrapper');

  add('official Decky observer preserves direct components and rejects changed patch provenance after dispatch',async()=>{
    const direct=popupActivation({nativeFocusData:true}),dr=await direct.send(),dn=dr.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
    check('decky-direct-still-bound',dn.status==='committed-popup-holder-confirmed'&&dn.componentWrapperDepth===0&&dn.nativeFocus.calls===1&&dn.nativeFocus.restored);
    check('decky-direct-actions-unchanged',direct.popupActivation.calls.length===1&&direct.keyCalls.length===1&&direct.activationTail===1);
    for(const key of ['original','hasUnpatched','slot']){
      let patch;const f=popupActivation({nativeFocusData:true,afterKey:f=>{
        const original=f.popupNativeData.ye.type,object={type:original};patch={object,property:'type',original,patchedFunction:null,hasUnpatched:false};
        const fn=new Function('patch','handler','options','return ('+"function (...args) {\n        let ret = patch.original.call(this, ...args);\n        ret = handler.call(this, args, ret);\n        if (options.singleShot) {\n            patch.unpatch();\n        }\n        return ret;\n    }"+');')(patch,(args,ret)=>ret,{});
        object.type=fn;patch.patchedFunction=fn;fn.__deckyPatch=patch;f.popupNativeData.ye.type=fn;
      },nativeFocusMutation:()=>{
        if(key==='original')patch.original=function changed(){};
        else if(key==='slot')patch.object.type=function changed(){};
        else patch.hasUnpatched=true;
      }}),r=await f.send(),n=r.menuTarget.windowFocus.menuTreeActivation.nativeFocusObservation;
      check('decky-late-change-recorded-'+key,n.nativeFocus.calls===1&&n.nativeFocus.returned===1&&n.nativeFocus.restored&&n.nativeFocus.status==='holder-changed-after-observation');
      check('decky-late-change-actions-unchanged-'+key,f.popupActivation.calls.length===1&&f.keyCalls.length===1&&f.activationTail===1&&n.inputCapabilityGranted===false&&n.physicalInputProven===false);
    }
  },'popup-native-focus-decky-lifetime');
return {allCases,execute,contextReads,get activeCase(){return activeCase}};
}
const filters=['popup-native-decky-lifetime','popup-native-decky-wrapper','popup-native-depth-path','popup-native-depth','popup-native-pair-guard','popup-native-shared-return','popup-native-shared-child','popup-native-focus','popup-native-focus-control','popup-previous-control','popup-mount-control','popup-mount-successor','popup-late-control','popup-final','popup-activation-remainder','popup-activation','popup-activation-control','focus-coordinator-scope-r2','focus-scope-incomplete-control','focus-scope-row-control','focus-coordinator-scope','focus-coordinator-control','focus-handoff-final','focus-handoff','focus-handoff-control','short-registration-home','short-registration-reassert','short-registration-control','assemble','affected','stale-global','late-browser','native-owner','guide','owner-successor','owner-window-control','review-final','review-control','promise-final','promise-rejected','throw-sync','focus-diagnostic','focus-shadow-control','context-diagnostic','context-leaf-control','context-health-control','native-route-trace','native-route-control','native-route-remainder','activation-observation','activation-control','activation-cleanup-result'];
export async function runCompositionSelection({fixture,identityFixture,source,filter}){
 assert(filters.includes(filter),'UNKNOWN_OR_EMPTY_COMPOSITION_SELECTOR');
 const environment=createCompositionEnvironment({fixture,identityFixture,source});
 const registered=registerCompositionCases({...environment,promiseVariant:['promise-final','promise-rejected'].includes(filter)?'rejected':'fulfilled'});
 const selected=registered.allCases.filter(x=>filter==='popup-native-decky-lifetime'?x.tag==='popup-native-focus-decky-lifetime':filter==='popup-native-decky-wrapper'?x.tag==='popup-native-focus-decky-wrapper':filter==='popup-native-depth-path'?x.tag==='popup-native-focus-depth-path':filter==='popup-native-depth'?x.tag==='popup-native-focus-depth':filter==='popup-native-pair-guard'?x.tag==='popup-native-focus-pair-guard':filter==='popup-native-shared-return'?x.tag==='popup-native-focus-shared-return':filter==='popup-native-shared-child'?x.tag==='popup-native-focus-shared-child':filter==='popup-native-focus'?x.tag.startsWith('popup-native-focus-'):filter==='popup-native-focus-control'?x.tag==='popup-native-focus-lifetime':filter==='popup-previous-control'?x.tag==='popup-previous':filter==='popup-mount-control'?x.tag==='popup-mount-final':filter==='popup-mount-successor'?x.tag==='popup-mount-final'||x.name==='unregistered held QAM popup invokes its original tree activation once'||x.name==='uncertain popup activation is not retried or converted into key replay':filter==='popup-late-control'?x.tag==='popup-late':filter==='popup-final'?x.tag==='popup-late'||x.name==='final popup current source revocation refuses activation'||x.name==='uncertain popup activation is not retried or converted into key replay':filter==='popup-activation-remainder'?x.tag==='popup-activation'&&x.name!=='unregistered held QAM popup invokes its original tree activation once':filter==='popup-activation-control'?x.name==='unregistered held QAM popup invokes its original tree activation once':filter==='focus-coordinator-scope-r2'?['focus-coordinator-scope','focus-scope-r2'].includes(x.tag):filter==='focus-scope-incomplete-control'?x.name==='present unverified coordinator cannot be omitted from held-window uniqueness':filter==='focus-scope-row-control'?x.name==='final selected own-data row cannot be replaced by accessor and equal match count':filter==='focus-coordinator-scope'?['focus-coordinator-scope','focus-handoff'].includes(x.tag):filter==='focus-coordinator-control'?x.name==='held root or active popup coordinator owns the original registered tree':filter==='focus-handoff-final'?['registered original focus callback selects the held root or popup once','final registered owner revocation prevents the pending focus action','uncertain original focus callback is never retried or converted to key replay','exact multiple inspect no-op base alias and uncertain composition never enter focus action'].includes(x.name):filter==='focus-handoff-control'?x.name==='registered original focus callback selects the held root or popup once':filter==='short-registration-home'?(x.tag==='short-registration-home'||x.tag==='guide'):filter==='short-registration-control'?x.name==='short registration reasserts same held tuple after original Ensure':filter==='activation-cleanup-result'?x.name==='synchronous activation delivery defers disposal until the original token returns':filter==='activation-control'?x.name==='passive activation records exact native primitives without replacing the original action':filter==='native-route-remainder'?['inspect and exact registrations add no native observation','plural result accessor is never invoked by native diagnostics'].includes(x.name):filter==='native-route-control'?x.name==='original activation lookup is observed for the exact held Host':filter==='context-leaf-control'?x.name==='stopped game overview reports first false leaf without later reads':filter==='context-health-control'?x.name==='healthy menu context getters and original input route stay unchanged':filter==='focus-shadow-control'?x.name==='official public readers never invoke unqualified underlying field getters':filter==='promise-final'?(x.name==='promise native API remains uncertain and is not replayed'||x.name==='throwing supplementary native API is uncertain once without cache commit'):filter==='promise-rejected'?x.name==='promise native API remains uncertain and is not replayed':filter==='throw-sync'?x.name==='throwing supplementary native API is uncertain once without cache commit':filter==='review-final'?x.tag==='review-final':filter==='review-control'?x.name==='final focused-window read changes queue element in place':filter==='owner-successor'?(x.tag==='owner-successor'||x.tag==='native-owner'||x.tag==='late-browser'||x.name==='healthy A real Ensure native one and no duplicate'):filter==='owner-window-control'?x.name==='post-key Window API replacement is not adopted':(filter==='affected'&&!x.tag.startsWith('popup-native-focus-')&&x.tag!=='popup-previous'&&x.tag!=='popup-mount-final'&&x.tag!=='popup-late'&&x.tag!=='popup-activation'&&x.tag!=='focus-scope-r2'&&x.tag!=='focus-coordinator-scope'&&x.tag!=='focus-handoff'&&x.tag!=='native-owner'&&x.tag!=='guide'&&x.tag!=='owner-successor'&&x.tag!=='review-final'&&x.tag!=='focus-diagnostic'&&x.tag!=='context-diagnostic'&&x.tag!=='native-route-trace'&&x.tag!=='activation-observation')||x.tag===filter);
 assert.equal(new Set(registered.allCases.map(x=>x.name)).size,registered.allCases.length);
 if(filter==='assemble'){
  const f=environment.actualActivation();assert.equal(f.keyCalls.length,0);assert.equal(f.nativeReads,0);
  assert.equal(f.window.SteamUIStore.WindowStore,f.windows);assert.equal(typeof f.send,'function');
  return {selection:'screen-shortcut-same-action',parameter:'per-browser-composition',caseFilter:filter,status:'PASS_ENTRY_ASSEMBLY_ONLY',ok:true,registeredCases:registered.allCases.map(x=>({name:x.name,tag:x.tag})),cases:[],passed:0,businessCasesExecuted:0,actualPassedThisRun:0,officialDataSha256:environment.officialDataSha256,assemblyIdentity:environment.assemblyIdentity,defaultSuiteExecuted:false,physicalInputProven:false};
 }
 assert(selected.length>0,'EMPTY_COMPOSITION_SELECTION');const expected=selected.map(x=>x.name);
 try{for(const item of selected)await registered.execute(item.name,item.fn);assert.deepEqual(environment.cases,expected);
  return {selection:'screen-shortcut-same-action',parameter:'per-browser-composition',caseFilter:filter,status:'PASS_SCOPED_COMPOSITION',ok:true,selectedCases:expected,cases:environment.cases,checks:environment.checks,passed:environment.cases.length,businessCasesExecuted:environment.cases.length,defaultSuiteExecuted:false,physicalInputProven:false,...(filter.startsWith('context-')?{contextReadObservations:registered.contextReads}:{})};
 }catch(error){process.exitCode=2;return {selection:'screen-shortcut-same-action',parameter:'per-browser-composition',caseFilter:filter,status:'FAIL_RETAINED',ok:false,selectedCases:expected,cases:environment.cases,checks:environment.checks,failedCase:error.message,activeCase:registered.activeCase,error:String(error.stack??error),defaultSuiteExecuted:false,physicalInputProven:false};}
}
