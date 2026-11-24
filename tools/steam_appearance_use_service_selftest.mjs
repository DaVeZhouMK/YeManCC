import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {buildSync,transformSync} from 'esbuild';
let clock=0;
const preferenceModule={exports:{}};vm.runInNewContext(buildSync({entryPoints:[path.resolve(import.meta.dirname,'../src/bridge/bootModeGlobalPreferences.ts')],bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text,{module:preferenceModule,exports:preferenceModule.exports,structuredClone,TextEncoder});
const module={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(process.env.YMCC_STEAM_APPEARANCE_SERVICE_SOURCE||path.resolve(import.meta.dirname,'../src/bridge/steamAppearance.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,require:p=>p==='./bootModeGlobalPreferences'?preferenceModule.exports:p==='./ipc'?{invoke:async()=>{throw Error('no production IPC')}}:{fs:{},shell:{},settingsStore:{}},TextEncoder,btoa,performance:{now:()=>clock},Number,Error,JSON});
const {createSteamAppearance,STEAM_APPEARANCE_THEMES,validAppearanceSnapshot}=module.exports;
const payloadOf=args=>{const i=args.indexOf('-Payload');return i<0?undefined:JSON.parse(Buffer.from(args[i+1],'base64').toString('utf8'));};
function fixture(){
 clock=0;
 const snap={controlProtocol:2,targetId:'a'.repeat(16),revision:'b'.repeat(64),linked:true,backendReady:true,backendProcessCount:2,steamRunning:true,configuredMatchesBackend:true,note:'',themes:STEAM_APPEARANCE_THEMES.map(t=>({name:t.name,installed:true,configured:true,backendEnabled:true,parameterCount:0,error:''}))};
 const f={snap,policy:{supported:true,version:3,manualObservationProtocol:1,sharedSteamReadProtocol:1,processQuery:'Local\\YMCC.CssInventory.'+'a'.repeat(32),pid:42,birth:'100'},calls:[],statusReads:[],statusMs:0,begin:0,ensure:0,end:0,holdEnd:false,slowReady:0,readyReads:0,failEnsure:false,failIdle:false,failApply:false,failEnd:false,group:false,token:0,wait:null};
 Object.assign(f,{directory:'C:/fixture/PowerControl',locations:0,existPaths:[],policyReads:0,hasScript:true});
 f.api=createSteamAppearance({loadBootPreferences:async()=>{await f.onBootPreferences?.();return structuredClone(f.bootDocument);},rememberBootPreferences:async states=>{f.preferenceWrites=(f.preferenceWrites||0)+1;f.preferences={...(f.preferences||{}),...states};},
 location:async()=>{f.locations++;f.onLocation?.();if(f.holdLocation)await f.holdLocation;if(f.locationError)throw Error('location transport failure');return {directory:f.directory};},
 exists:async file=>{f.existPaths.push(file);f.onExists?.();return f.hasScript;},hidden:async()=>{throw Error('detached launches forbidden')},delay:async ms=>{clock+=ms},idlePolicy:async()=>{f.policyReads++;return f.policy;},
 beginUse:async()=>{f.begin++;f.group=true;return {ok:true,token:String(++f.token),pid:42,birth:'100',backendPath:'C:\\fixture\\CssLoader-Standalone-Headless.exe'}},ensureUse:async()=>{f.ensure++;if(f.failEnsure)throw Error('fixture Ensure failed');Object.assign(snap,{backendReady:!f.slowReady,backendProcessCount:2,backendIdle:false,configuredMatchesBackend:true});f.onEnsure?.();return {ok:true,completionEvent:f.completionEvent}},
 observeEnsure:token=>{assert.equal(token,String(f.token));f.observations=(f.observations||0)+1;return {wake:f.observe?.()||new Promise(()=>{}),close:()=>{f.closed=(f.closed||0)+1}}},
 endUse:async token=>{f.end++;assert.equal(token,String(f.token));f.onEnd?.();if(f.holdRelease)await f.holdRelease;if(f.holdEnd)return {ok:true,minimumHoldPending:true};f.group=false;if(f.failEnd){Object.assign(snap,{backendReady:true,backendProcessCount:1,backendIdle:false});return {ok:false};}return {ok:true}},
 run:async(program,args,timeoutMs)=>{const action=args[args.indexOf('-Action')+1];f.lastArgs=[...args];f.calls.push(action);if(f.bindingFailure)return {exitCode:1,stdout:'',stderr:'NamedParameterNotFound'};if(action==='Status'&&f.failStatus)throw Error('status transport failed');if(action==='Status'&&f.statusFailure)return {exitCode:1,stdout:JSON.stringify({ok:false,message:'status rejected'}),stderr:''};if(action==='Status'){f.statusReads.push({at:clock,timeoutMs,payload:payloadOf(args)});const response=f.statusResponses?.shift();if(response)return {exitCode:response.ok?0:1,stdout:JSON.stringify(response),stderr:''};if(f.ensure){clock+=Math.min(f.statusMs,timeoutMs);if(f.statusMs>timeoutMs)throw Error('fixture timeout');}}if(f.wait)await f.wait;let result;
  if(action==='Status'&&f.ensure&&f.readyAt!==undefined)snap.backendReady=clock>=f.readyAt;
  if(action==='Status'&&f.ensure&&f.slowReady&&++f.readyReads>=f.slowReady)snap.backendReady=true;
  if(action==='Reconcile')result={ok:true,snapshot:structuredClone(snap)};
  if(action==='ScopedApply'){
   const payload=payloadOf(args);f.scopedApply=payload;
   if(f.oldApplyScript)return {exitCode:1,stdout:'',stderr:'Cannot validate argument on parameter Action'};
   if(f.scopedApplyResult)result=f.scopedApplyResult();
   else if(payload.targetId!==snap.targetId)result={ok:false,message:'主题目录已变化'};
   else if(payload.expectedRevision!==snap.revision)result={ok:false,applyPreflightChanged:true};
   else{for(const [name,value]of Object.entries(payload.states))snap.themes.find(row=>row.name===name).configured=value;snap.revision='c'.repeat(64);result={ok:true,saved:true,renderConfirmed:true,renderContextsComplete:true,snapshot:structuredClone(snap)};}
  }
  else if(action==='Apply'){if(f.failApply)throw Error('fixture Apply failure');const payload=JSON.parse(Buffer.from(args[args.indexOf('-Payload')+1],'base64').toString('utf8'));f.applied=payload;f.beforeApply?.();if(payload.expectedRevision!==snap.revision)throw Error('Theme settings changed; reread before applying');for(const [name,value]of Object.entries(payload.states))snap.themes.find(row=>row.name===name).configured=value;snap.revision='c'.repeat(64);result={ok:true,saved:true,snapshot:structuredClone(snap)}}
  else if(action==='Prepare'&&f.firstPrepare){Object.assign(snap,{packOwned:true,packPrepared:true,revision:'d'.repeat(64)});for(const row of snap.themes)row.configured=true;if(f.moveDuringPrepare)snap.targetId='e'.repeat(16);result={ok:true,snapshot:structuredClone(snap)}}
   else if(action==='ScopedRefresh'){
    f.refreshScopes??=[];f.refreshScopes.push(payloadOf(args));
    if(f.oldRefreshScript)return {exitCode:1,stdout:'',stderr:'Cannot validate argument on parameter Action'};
    result=f.scopedResponses?.shift()??{ok:true,saved:true,renderConfirmed:true,snapshot:structuredClone(snap)};
   }
   else if(action==='Reload'||action==='ScopedReload'){
    f.reloadPayload=payloadOf(args);
    if(f.oldReloadScript)return {exitCode:1,stdout:'',stderr:'Cannot validate argument on parameter Action'};
    snap.backendReloadRequired=false;
    result=f.reloadResponse?.()??{ok:true,reloaded:true,previousRevision:f.reloadPayload.expectedRevision,snapshot:structuredClone(snap)};
   }
   else if(action==='ScopedPreset'){
   if(f.oldPresetScript)return {exitCode:1,stdout:'',stderr:'Cannot validate argument on parameter Action'};
   result=f.presetResponse??{ok:true,saved:true,snapshot:structuredClone(snap)};
  }
  else if(action==='Idle'){if(f.failIdle)result={ok:false,message:'DOM not confirmed'};else{snap.backendIdle=true;Object.assign(snap,{backendReady:false,backendProcessCount:0});result={ok:true,snapshot:structuredClone(snap)}}}
  else result={ok:true,snapshot:structuredClone(snap)};
  if(action==='Refresh'&&f.refreshResponse)result=f.refreshResponse;
  if(action==='Reconcile'&&f.renderReceipt!==undefined)result.renderConfirmed=f.renderReceipt;
  return{exitCode:result.ok?0:1,stdout:JSON.stringify(result),stderr:''};}});return f;
}
const cases=[], comparisons=[];async function check(name,fn){await fn();cases.push(name)};
await check('one native directory lookup covers a sequence of Status and manual Refresh requests',async()=>{
 const f=fixture();await f.api.read();await f.api.refresh();await f.api.read();
 const counts={location:f.locations,exists:f.existPaths.length,policy:f.policyReads,actions:f.calls};
 console.log(JSON.stringify({appearancePreflight:counts}));
 assert.deepEqual(f.calls,['Status','Status','Refresh','Status']);
 assert.equal(f.locations,1,'PowerControl directory is fixed for the native process');
 assert.equal(f.existPaths.length,f.calls.length);assert(f.policyReads>=f.calls.length);
});
await check('simultaneous first readers join directory discovery but independently admit their Status',async()=>{
 const f=fixture();let release;f.holdLocation=new Promise(r=>release=r);const reads=[f.api.read(),f.api.read()];
 assert.equal(f.locations,1);assert.equal(f.existPaths.length,0);release();await Promise.all(reads);
 assert.deepEqual(f.calls,['Status','Status']);assert.equal(f.existPaths.length,2);assert.equal(f.policyReads,2);
});
await check('retired Status leaves shared location discovery available to the visible caller',async()=>{
 const f=fixture();let release,current=true;
 f.holdLocation=new Promise(r=>release=r);
 const old=assert.rejects(f.api.read(()=>current),/Appearance observation retired/);
 const visible=f.api.read(()=>true);
 current=false;release();await Promise.all([old,visible]);
 assert.deepEqual([f.locations,f.existPaths.length,f.policyReads],[1,1,1]);
 assert.deepEqual(f.calls,['Status']);
});
await check('retirement during file probing stops Status and Reconcile before native policy reads',async()=>{
 for(const action of ['Status','Reconcile']){
  const f=fixture();f.snap.packOwned=true;f.snap.configuredMatchesBackend=false;
  let current=true;f.onExists=()=>{current=false;};
  if(action==='Status')await assert.rejects(f.api.read(()=>current),/Appearance observation retired/);
  else assert.equal(await f.api.reconcileExisting(f.snap,()=>current),undefined);
  assert.deepEqual([f.existPaths.length,f.policyReads,f.calls.length],[1,0,0],action);
  f.onExists=undefined;
  if(action==='Status')await f.api.read();
  else await f.api.reconcileExisting(f.snap,()=>true);
  assert.deepEqual(f.calls,[action]);
  assert.deepEqual([f.locations,f.existPaths.length,f.policyReads],[1,2,1]);
  assert.equal(f.begin+f.ensure+f.end,0);
 }
});
await check('failed and malformed directory lookup can recover without caching a default path',async()=>{
 const f=fixture();f.locationError=true;await assert.rejects(f.api.read(),/location transport failure/);f.locationError=false;
 f.directory='relative/PowerControl';await assert.rejects(f.api.read(),/无法确认 YMCC 配置目录/);
 assert.deepEqual([f.existPaths.length,f.policyReads,f.calls.length],[0,0,0]);
 f.directory='D:/fixture/PowerControl/';await f.api.read();await f.api.read();
 assert.equal(f.locations,3);assert.deepEqual(f.existPaths,Array(2).fill('D:\\fixture\\PowerControl\\steam-appearance.ps1'));
});
await check('each launch checks component disappearance, repair and fresh Steam session after discovery',async()=>{
 const f=fixture();await f.api.read();f.hasScript=false;
 await assert.rejects(f.api.read(),/缺少外观控制组件.*steam-appearance\.ps1.*这不代表 CSSLoader/);
 assert.deepEqual(f.calls,['Status']);assert.equal(f.policyReads,1);
 f.hasScript=true;f.policy.birth='101';await f.api.read();
 assert.equal(f.lastArgs[f.lastArgs.indexOf('-SessionBirth')+1],'101');
 assert.deepEqual([f.locations,f.existPaths.length,f.policyReads],[1,3,2]);
});
await check('binding diagnostics report the path actually dispatched without a second existence probe',async()=>{
 const f=fixture();f.bindingFailure=true;
 await assert.rejects(f.api.read(),/不配套.*C:\\fixture\\PowerControl\\steam-appearance\.ps1/);
 assert.deepEqual([f.locations,f.existPaths.length,f.policyReads],[1,1,1]);assert.deepEqual(f.calls,['Status']);
});
await check('read never acquires a lease, starts CSS or writes anything',async()=>{const f=fixture();await f.api.read();assert.deepEqual(f.calls,['Status']);assert.equal(f.begin+f.ensure+f.end,0)});
await check('Status requests a summary and accepts both lean and legacy full snapshots',async()=>{
 const f=fixture(),plan={protocol:4,files:[{path:'shared.css',property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true}],variables:[]};
 const lean=await f.api.read();assert.deepEqual(payloadOf(f.lastArgs),{profile:'ui-summary-v1',lockWait:'single-v1'});assert.equal(lean.themes[0].renderPlan,undefined);
 f.snap.themes[0].renderPlan=plan;const full=await f.api.read();assert.deepEqual(payloadOf(f.lastArgs),{profile:'ui-summary-v1',lockWait:'single-v1'});assert.deepEqual(full.themes[0].renderPlan,plan);
 f.snap.themes[0].renderPlan.protocol=3;await assert.rejects(f.api.read(),/无效状态/);assert.deepEqual(f.calls,['Status','Status','Status']);assert.equal(f.begin+f.ensure+f.end,0);
});
const busy={ok:false,message:'Theme operation busy; retry after rereading'};
await check('a negotiated Status lock timeout is terminal and has no delay or second shell',async()=>{
 const f=fixture();f.statusResponses=[{...busy,statusLockTimedOut:true}];
 await assert.rejects(f.api.read(),/外观配置正被其他操作占用/);
 assert.deepEqual(f.calls,['Status']);assert.equal(f.statusReads[0].timeoutMs,22000);assert.equal(clock,0);assert.equal(f.begin+f.ensure+f.end,0);
});
await check('only a legacy busy response gets the existing short bounded retry',async()=>{
 const f=fixture();f.statusResponses=[busy,busy];assert.equal((await f.api.read()).revision,f.snap.revision);
 assert.deepEqual(f.statusReads,[{at:0,timeoutMs:22000,payload:{profile:'ui-summary-v1',lockWait:'single-v1'}},
  {at:250,timeoutMs:22000,payload:{profile:'ui-summary-v1'}},{at:500,timeoutMs:22000,payload:{profile:'ui-summary-v1'}}]);
 const failed=fixture();failed.statusResponses=[busy,busy,busy];await assert.rejects(failed.api.read(),/Theme operation busy/);assert.equal(failed.calls.length,3);
});
await check('malformed and contradictory lock receipts are rejected without retrying',async()=>{
 for(const response of [{...busy,statusLockTimedOut:'true'},{ok:true,statusLockTimedOut:true},{...busy,statusLockTimedOut:true,snapshot:null}]){
  const f=fixture();f.statusResponses=[response];await assert.rejects(f.api.read(),/无效状态/);assert.deepEqual(f.calls,['Status']);
 }
});
await check('explicit Refresh and Reconcile request compact receipts with the existing target and revision',async()=>{
 const f=fixture();f.refreshResponse={ok:true,saved:true,renderConfirmed:true,snapshot:f.snap};
 const result=await f.api.refresh();assert.deepEqual(f.calls,['Status','Refresh']);assert.deepEqual(payloadOf(f.lastArgs),{profile:'ui-summary-v1'});
 assert.equal(result.renderConfirmed,true);assert.equal(result.snapshot.themes.length,7);assert.equal(result.snapshot.themes[0].renderPlan,undefined);
 assert.deepEqual([f.policyReads,f.begin,f.ensure,f.end],[3,1,0,1]);
 f.snap.packOwned=true;f.snap.configuredMatchesBackend=false;await f.api.reconcileExisting(f.snap);assert.equal(f.calls.at(-1),'Reconcile');assert.deepEqual(payloadOf(f.lastArgs),{profile:'ui-summary-v1',expectedRevision:f.snap.revision,targetId:f.snap.targetId});
});
await check('preset reset and restore request compact receipts but accept legacy returned snapshots',async()=>{
 for(const [mode,action] of [['preset','Reset'],['previous','Restore']]){
  const f=fixture();f.snap.steamRunning=false;f.snap.backendReady=false;f.snap.backendProcessCount=0;
  const result=await f.api.reset(mode);assert.deepEqual(f.calls,['Status',action]);assert.deepEqual(payloadOf(f.lastArgs),{profile:'ui-summary-v1'});assert.equal(result.snapshot.themes.length,7);
 }
});
await check('a known preset scope removes the extra Status helper for both reset modes',async()=>{
 for(const mode of ['preset','previous']){
  const f=fixture();Object.assign(f.snap,{presetScopeProtocol:1,steamRunning:false,backendReady:false,backendProcessCount:0});
  const result=await f.api.reset(mode,{},f.snap);
  assert.deepEqual(f.calls,['ScopedPreset']);assert.deepEqual(payloadOf(f.lastArgs),{profile:'ui-summary-v1',mode,targetId:f.snap.targetId.toUpperCase()});
  assert.equal(result.saved,true);assert.equal(f.preferenceWrites,1);assert.equal(f.begin+f.ensure+f.end,0);assert.equal(f.policyReads,1);
 }
});
await check('an unknown scope capability or unusable target uses legacy actions after Status',async()=>{
 for(const observed of [{presetScopeProtocol:2},{presetScopeProtocol:1,targetId:undefined},{presetScopeProtocol:1,targetId:'a'.repeat(16)+'\n'},{controlProtocol:1,presetScopeProtocol:1}]){
  const f=fixture();Object.assign(f.snap,{steamRunning:false,backendReady:false,backendProcessCount:0});
  await f.api.reset('preset',{}, {...f.snap,...observed});assert.deepEqual(f.calls,['Status','Reset']);
 }
});
await check('a moved legacy target and a rejected scoped mutation never replay a write',async()=>{
 const legacy=fixture();await assert.rejects(legacy.api.reset('preset',{}, {...legacy.snap,targetId:'b'.repeat(16)}),/目录已变化/);assert.deepEqual(legacy.calls,['Status']);
 for(const oldPresetScript of [true,false]){
  const f=fixture();f.snap.presetScopeProtocol=1;f.oldPresetScript=oldPresetScript;f.presetResponse={ok:false,message:'主题目录已变化'};
  await assert.rejects(f.api.reset('preset',{},f.snap));assert.deepEqual(f.calls,['ScopedPreset']);assert.equal(f.begin+f.ensure+f.end+(f.preferenceWrites||0),0);
 }
});
await check('absence or mismatch of a scoped commit snapshot preserves the saved fact without CSS or preference writes',async()=>{
 for(const moved of [false,true]){
  const f=fixture();f.snap.presetScopeProtocol=1;
  f.presetResponse=moved?{ok:true,saved:true,snapshot:{...structuredClone(f.snap),targetId:'c'.repeat(16)}}:{ok:false,saved:true,message:'外观配置已保存，但当前目录未确认'};
  const result=await f.api.reset('previous',{},f.snap);
  assert.equal(result.ok,false);assert.equal(result.saved,true);assert.equal(result.snapshot,undefined);assert.match(result.message,/已保存.*未确认/);
  assert.deepEqual(f.calls,['ScopedPreset']);assert.equal(f.begin+f.ensure+f.end+(f.preferenceWrites||0),0);
 }
});
await check('restoring unowned original files while Steam runs never requests the managed-pack Reload',async()=>{
 const f=fixture();Object.assign(f.snap,{presetScopeProtocol:1,packOwned:false,packPrepared:false,backendReloadRequired:true});
 const result=await f.api.reset('previous',{},f.snap);
 assert.equal(result.ok,false);assert.equal(result.saved,true);assert.equal(result.snapshot.packOwned,false);assert.match(result.message,/已保存.*画面尚未确认/);
 assert.deepEqual(f.calls,['ScopedPreset']);assert.equal(f.begin+f.ensure+f.end,0);
});
await check('new Refresh callers accept full receipts from older helpers',async()=>{
 const f=fixture();const plan={protocol:4,files:[{path:'round.css',property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true}],variables:[]};
 const snapshot=structuredClone(f.snap);snapshot.themes[0].renderPlan=plan;
 f.refreshResponse={ok:true,saved:true,renderConfirmed:true,snapshot};
 const result=await f.api.refresh();assert.deepEqual(result.snapshot.themes[0].renderPlan,plan);assert.deepEqual(f.calls,['Status','Refresh']);
});
await check('explicit Refresh revalidates the ticket on its dispatch policy read',async()=>{
 for(const delta of [{pid:73},{birth:'101'},{manualObservationProtocol:0}]){
  const f=fixture();f.onExists=()=>{if(f.existPaths.length===2)Object.assign(f.policy,delta);};
  await assert.rejects(f.api.refresh(),/Steam 会话已变化/);
  assert.deepEqual(f.calls,['Status']);assert.deepEqual([f.policyReads,f.begin,f.ensure,f.end],[3,1,0,1]);
 }
});
await check('explicit Refresh still waits for an unready backend before submitting',async()=>{
 const f=fixture();f.snap.backendReady=false;f.snap.backendProcessCount=0;
 await f.api.refresh();assert.deepEqual(f.calls,['Status','Status','Refresh']);
 assert.deepEqual([f.begin,f.ensure,f.end],[1,1,1]);
 assert.equal(f.statusReads.at(-1).at,2000);
});
await check('Refresh after preset repair reuses the settled readiness and still verifies the ticket at dispatch',async()=>{
 for(const cold of [false,true]){
  for(const stale of [false,true]){
   const f=fixture();f.firstPrepare=true;
   Object.assign(f.snap,{packAvailable:true,packOwned:true,packPrepared:false,packRepairRequired:true,backendReady:!cold,backendProcessCount:cold?0:2});
   if(stale)f.onExists=()=>{if(f.existPaths.length===(cold?4:3))f.policy.birth='101';};
   let dispatched=0;
   const operation=f.api.refresh(()=>{dispatched++;});
   if(stale)await assert.rejects(operation,/Steam 会话已变化/);
   else assert.equal((await operation).ok,true);
   assert.deepEqual(f.calls,['Status','Prepare',...(cold?['Status']:[]),...(stale?[]:['Refresh'])]);
   assert.deepEqual([f.begin,f.ensure,f.end,dispatched],[1,Number(cold),1,Number(!stale)]);
   assert.equal(f.policyReads,cold?6:5,'prepared Refresh repeats a native readiness check');
  }
 }
});
function scopedRefreshFixture(){
 const f=fixture();Object.assign(f.snap,{refreshScopeProtocol:1,packPrepared:true,packOwned:true,steamPid:42,steamBirth:'100'});
 return f;
}
await check('Fallback Refresh refuses a changed observed scope before acquiring or preparing a backend',async()=>{
 const probes=[];
 for(const edge of ['directory','session','offline']){
  const f=scopedRefreshFixture();delete f.snap.refreshScopeProtocol;
  const shown=structuredClone(f.snap);
  f.firstPrepare=true;Object.assign(f.snap,{packAvailable:true,packOwned:false,packPrepared:false});
  if(edge==='directory')f.snap.targetId='d'.repeat(16);
  if(edge==='session')f.snap.steamBirth='101';
  if(edge==='offline')Object.assign(f.snap,{steamRunning:false,steamPid:undefined,steamBirth:undefined,backendReady:false,backendProcessCount:0});
  let result;
  try{result=await f.api.refresh(undefined,shown);}catch(error){result={ok:false,message:error.message};}
  probes.push({edge,accepted:result.ok,saved:result.saved,message:result.message,
   shownNewScope:result.snapshot?.targetId===f.snap.targetId&&result.snapshot?.steamRunning===f.snap.steamRunning&&result.snapshot?.steamBirth===f.snap.steamBirth,
   actions:f.calls,leases:[f.begin,f.ensure,f.end]});
 }
 console.log(JSON.stringify({fallbackRefreshScope:probes}));
 for(const probe of probes){
  assert.equal(probe.accepted,false,probe.edge);
  assert.equal(probe.saved,undefined,probe.edge);
  assert.equal(probe.shownNewScope,true,probe.edge);
  assert.match(probe.message,/Steam 会话或主题目录已变化/,probe.edge);
  assert.deepEqual(probe.actions,['Status'],probe.edge);
  assert.deepEqual(probe.leases,[0,0,0],probe.edge);
 }
});
await check('Fallback Refresh compares the confirmed session against the new lease policy',async()=>{
 const f=scopedRefreshFixture();delete f.snap.refreshScopeProtocol;
 const shown=structuredClone(f.snap);
 f.policy.birth='101';
 await assert.rejects(f.api.refresh(undefined,shown),/Steam 会话已变化/);
 assert.deepEqual(f.calls,['Status']);assert.deepEqual([f.begin,f.ensure,f.end],[0,0,0]);
});
await check('Fallback Refresh stops settling a preparation receipt from another directory',async()=>{
 const f=scopedRefreshFixture();delete f.snap.refreshScopeProtocol;
 const shown=structuredClone(f.snap);
 f.firstPrepare=true;f.moveDuringPrepare=true;
 Object.assign(f.snap,{packAvailable:true,packOwned:false,packPrepared:false,backendReady:false,backendProcessCount:0});
 await assert.rejects(f.api.refresh(undefined,shown),/Steam 会话或主题目录已变化/);
 assert.deepEqual(f.calls,['Status','Prepare']);assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
});
await check('Scoped Refresh skips the Status shell with an already observed healthy target',async()=>{
 const f=scopedRefreshFixture(),old=fixture();await old.api.refresh(undefined,old.snap);
 let dispatched=0;
 const result=await f.api.refresh(()=>{dispatched++;},f.snap);
 assert.deepEqual(f.calls,['ScopedRefresh']);assert.deepEqual(old.calls,['Status','Refresh']);
 assert.deepEqual(f.refreshScopes,[{profile:'ui-summary-v1',targetId:f.snap.targetId}]);
 assert.equal(result.renderConfirmed,true);assert.deepEqual([f.existPaths.length,f.policyReads,f.begin,f.ensure,f.end,dispatched],[1,2,1,0,1,1]);
});
await check('Scoped Refresh with a superseded Steam instance stops before any shell dispatch',async()=>{
 for(const point of ['before-lease','before-dispatch']){
  const f=scopedRefreshFixture();
  if(point==='before-lease')f.policy.birth='101';else f.onExists=()=>{f.policy.birth='101';};
  await assert.rejects(f.api.refresh(undefined,f.snap),/Steam 会话已变化/);
  assert.deepEqual(f.calls,[]);assert.deepEqual([f.begin,f.ensure,f.end],point==='before-lease'?[0,0,0]:[1,0,1]);
 }
});
await check('Scoped Refresh falls back only after an explicit pre-write decline and stays bound to the observed target',async()=>{
 const decline={ok:false,refreshPreparationRequired:true,message:'needs preflight'};
 const f=scopedRefreshFixture(),prior=structuredClone(f.snap);f.firstPrepare=true;
 Object.assign(f.snap,{packAvailable:true,packPrepared:false,packRepairRequired:true});f.scopedResponses=[decline];
 const result=await f.api.refresh(undefined,prior);
 assert.equal(result.ok,true);assert.deepEqual(f.calls,['ScopedRefresh','Status','Prepare','ScopedRefresh']);
 assert.deepEqual(f.refreshScopes.map(scope=>scope.targetId),[prior.targetId,prior.targetId]);assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
 for(const field of ['targetId','steamBirth']){
  const g=scopedRefreshFixture(),previous=structuredClone(g.snap);g.scopedResponses=[decline];g.snap[field]=field==='targetId'?'d'.repeat(16):'101';
  await assert.rejects(g.api.refresh(undefined,previous),/Steam 会话或主题目录已变化/);
  assert.deepEqual(g.calls,['ScopedRefresh','Status']);assert.equal(g.end,1);
 }
 const again=scopedRefreshFixture();again.scopedResponses=[decline,decline];
 await assert.rejects(again.api.refresh(undefined,again.snap),/needs preflight/);
 assert.deepEqual(again.calls,['ScopedRefresh','Status','ScopedRefresh']);
});
await check('Scoped Refresh never replays ambiguous, malformed or unsupported results',async()=>{
 for(const kind of ['partial','contradictory','different-target','old-script']){
  const f=scopedRefreshFixture();
  f.scopedResponses=[kind==='partial'?{ok:false,saved:true,renderConfirmed:false,snapshot:f.snap}:
   kind==='contradictory'?{ok:false,saved:true,refreshPreparationRequired:true}:
   {ok:true,snapshot:{...f.snap,targetId:'e'.repeat(16)}}];
  f.oldRefreshScript=kind==='old-script';
  if(kind==='partial'){const result=await f.api.refresh(undefined,f.snap);assert.equal(result.saved,true);assert.equal(result.renderConfirmed,false);}
  else await assert.rejects(f.api.refresh(undefined,f.snap),/无效状态|回执目录|无法解析/);
  assert.deepEqual(f.calls,['ScopedRefresh']);assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
 }
});
function reloadRetryFixture(){
 const f=scopedRefreshFixture(),prior=structuredClone(f.snap);
 f.snap.revision='c'.repeat(64);f.snap.backendReloadRequired=true;
 const {targetId,revision,steamPid,steamBirth}=f.snap;
 f.scopedResponses=[{ok:false,refreshPreparationRequired:true,refreshReload:{targetId,revision,steamPid,steamBirth}}];
 return {f,prior,first:f.scopedResponses[0]};
}
await check('Scoped reload retry avoids a second Status without replaying the old revision',async()=>{
 const {f,prior}=reloadRetryFixture(),old=reloadRetryFixture();delete old.first.refreshReload;
 assert.equal((await old.f.api.refresh(undefined,old.prior)).ok,true);
 assert.deepEqual(old.f.calls,['ScopedRefresh','Status','Reload','ScopedRefresh']);
 assert.equal((await f.api.refresh(undefined,prior)).renderConfirmed,true);
 assert.deepEqual(f.calls,['ScopedRefresh','ScopedReload','ScopedRefresh']);
 assert.deepEqual(f.reloadPayload,{profile:'ui-summary-v1',targetId:f.snap.targetId,expectedRevision:f.snap.revision});
 assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
});
await check('Scoped reload retry fails closed for malformed, stale or ambiguous evidence',async()=>{
 for(const edge of ['invalid-hint','different-target','different-birth','changed-policy','lost-policy','old-script','failed','different-revision','moved-during-reload','declined-again','already-reloaded']){
  const {f,prior,first}=reloadRetryFixture();
  if(edge==='invalid-hint')first.refreshReload.steamPid='42';
  if(edge==='different-target')first.refreshReload.targetId='d'.repeat(16);
  if(edge==='different-birth')first.refreshReload.steamBirth='101';
  if(edge==='changed-policy'||edge==='lost-policy')f.onExists=()=>{if(f.calls.length===1){if(edge==='changed-policy')f.policy.birth='101';else f.policy.manualObservationProtocol=0;}};
  if(edge==='old-script')f.oldReloadScript=true;
  if(edge==='failed')f.reloadResponse=()=>({ok:false,message:'readback failed'});
  if(edge==='different-revision'||edge==='moved-during-reload'||edge==='already-reloaded')f.reloadResponse=()=>({ok:true,reloaded:edge!=='already-reloaded',
   previousRevision:edge==='different-revision'?prior.revision:f.snap.revision,
   snapshot:{...f.snap,steamBirth:edge==='moved-during-reload'?'101':f.snap.steamBirth}});
  if(edge==='declined-again')f.scopedResponses.push(first);
  const result=f.api.refresh(undefined,prior);
  if(edge==='already-reloaded')assert.equal((await result).renderConfirmed,true);
  else await assert.rejects(result,/无效状态|Steam 会话|无法解析|readback failed|重读主题未确认|尚未就绪/,edge);
  assert.deepEqual(f.calls,['ScopedRefresh',...(['old-script','failed','different-revision','moved-during-reload','declined-again','already-reloaded'].includes(edge)?['ScopedReload']:[]),
   ...(['declined-again','already-reloaded'].includes(edge)?['ScopedRefresh']:[])],edge);
  assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
 }
});
const draft=(f,states,mode='selection',onCurrent=()=>{})=>f.api.submitDraft('a'.repeat(16),states,mode,onCurrent);
const observedApply=f=>structuredClone(Object.assign(f.snap,{applyScopeProtocol:1,packOwned:true,packPrepared:true,steamPid:42,steamBirth:'100'}));
const submitScoped=(f,observed,states={Round:false})=>f.api.submitDraft('a'.repeat(16),states,'selection',()=>{},observed);
await check('ready ordinary draft takes one helper and one lease; legacy takes Status plus Apply',async()=>{
 const f=fixture(), shown=observedApply(f), old=fixture(), oldShown=observedApply(old);delete oldShown.applyScopeProtocol;
 const after=await submitScoped(f,shown,{Round:false,'Art Hero':true}), before=await submitScoped(old,oldShown,{Round:false,'Art Hero':true});
 assert.equal(after.renderConfirmed,true);assert.equal(before.ok,true);assert.deepEqual(f.calls,['ScopedApply']);assert.deepEqual(old.calls,['Status','Apply']);
 assert.deepEqual(f.scopedApply,{profile:'ui-summary-v1',targetId:shown.targetId,expectedRevision:shown.revision,states:{Round:false}});
 assert.deepEqual(f.preferences,old.preferences);assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);assert.deepEqual([f.policyReads,old.policyReads],[3,4]);
});
await check('stale scoped revision allows only a fresh read and never rewrites a selection that already landed',async()=>{
 const f=fixture(), shown=observedApply(f);f.snap.revision='e'.repeat(64);f.snap.themes.find(row=>row.name==='Round').configured=false;
 assert((await submitScoped(f,shown)).ok);assert.deepEqual(f.calls,['ScopedApply','Status']);assert.deepEqual(f.preferences,{Round:false});assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
});
await check('scoped preflight can retry one fresh same-target draft without taking another lease; foreign session cannot retry',async()=>{
 const f=fixture(), shown=observedApply(f);f.snap.revision='e'.repeat(64);
 assert((await submitScoped(f,shown)).ok);assert.deepEqual(f.calls,['ScopedApply','Status','Apply']);assert.equal(f.applied.expectedRevision,'e'.repeat(64));assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
 const g=fixture(), observed=observedApply(g);g.snap.steamBirth='101';g.snap.revision='e'.repeat(64);
 await assert.rejects(submitScoped(g,observed),/会话已变化/);assert.deepEqual(g.calls,['ScopedApply','Status']);assert.equal(g.end,1);
});
await check('scoped apply stays single-shot after old binding, target change, conflicting receipt or saved but unconfirmed result',async()=>{
 for(const cause of ['old','moved','contradiction','saved']){
  const f=fixture(), shown=observedApply(f);
  if(cause==='old')f.oldApplyScript=true;
  if(cause==='moved')f.snap.targetId='d'.repeat(16);
  if(cause==='contradiction')f.scopedApplyResult=()=>({ok:false,saved:true,applyPreflightChanged:true});
  if(cause==='saved')f.scopedApplyResult=()=>{f.snap.themes.find(row=>row.name==='Round').configured=false;return {ok:false,saved:true,renderConfirmed:false,snapshot:structuredClone(f.snap)}};
  if(cause==='saved'){const r=await submitScoped(f,shown);assert(!r.ok&&r.saved&&r.preferenceSaved);}
  else{await assert.rejects(submitScoped(f,shown));assert.equal(f.preferenceWrites,undefined);}
  assert.deepEqual(f.calls,['ScopedApply']);assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
 }
});
await check('same-value or preferences-only drafts never take a scoped lease',async()=>{
 for(const mode of ['selection','preferences']){
  const f=fixture(), shown=observedApply(f);
  assert((await f.api.submitDraft(shown.targetId,{Round:true},mode,()=>{},shown)).ok);assert.deepEqual(f.calls,['Status']);assert.equal(f.begin,0);
 }
});
await check('a draft reads once under the write guard and sends only fresh differences',async()=>{
 const f=fixture();f.snap.themes[0].configured=false;
 let unblock;f.wait=new Promise(resolve=>unblock=resolve);
 const pending=draft(f,{Round:false,'Art Hero':false});await assert.rejects(f.api.refresh(),/进行中/);unblock();
 const result=await pending;assert.equal(result.preferenceSaved,true);assert.deepEqual(f.calls,['Status','Apply']);assert.deepEqual(f.applied,{profile:'ui-summary-v1',expectedRevision:'b'.repeat(64),states:{Round:false}});assert.deepEqual(f.preferences,{Round:false});assert.equal(f.begin,1);assert.equal(f.end,1);
});
await check('matching selections and preference-only retries read once without touching CSS or theme files',async()=>{
 const f=fixture();assert.equal((await draft(f,{Round:true})).preferenceSaved,true);assert.deepEqual(f.calls,['Status']);assert.equal(f.preferenceWrites,1);
 f.calls=[];assert.equal((await draft(f,{Round:true},'preferences')).preferenceSaved,true);assert.deepEqual(f.calls,['Status']);assert.equal(f.preferenceWrites,2);
 f.calls=[];assert.equal((await draft(f,{Round:false},'preferences')).preferenceSaved,false);assert.deepEqual(f.calls,['Status']);assert.equal(f.preferenceWrites,2);assert.equal(f.begin+f.ensure+f.end,0);
});
await check('changed directory rejects old choices and preference repair without writing',async()=>{
 const f=fixture();f.snap.targetId='d'.repeat(16);let observed;
 await assert.rejects(draft(f,{Round:false},'selection',value=>{observed=value.targetId}),/未跨目录提交/);
 await assert.rejects(draft(f,{Round:true},'preferences'),/未补写旧全局偏好/);
 assert.equal(observed,f.snap.targetId);assert.deepEqual(f.calls,['Status','Status']);assert.equal(f.begin+f.ensure+f.end,0);assert.equal(f.preferenceWrites,undefined);
});
await check('script rejects a changed revision after the one draft read; strict direct apply rejects stale snapshots',async()=>{
 const f=fixture(),old=structuredClone(f.snap);f.beforeApply=()=>{f.snap.revision='d'.repeat(64)};
 await assert.rejects(draft(f,{Round:false}),/Theme settings changed/);assert.deepEqual(f.calls,['Status','Apply']);assert.equal(f.end,1);assert.equal(f.preferenceWrites,undefined);
 f.calls=[];await assert.rejects(f.api.apply(old,{Round:false}),/外观配置已变化/);assert.deepEqual(f.calls,['Status']);assert.equal(f.snap.themes.find(r=>r.name==='Round').configured,true);
});
await check('offline first takeover preserves untouched switches and rejects a mismatched Prepare target',async()=>{
 for(const moveDuringPrepare of [false,true]){
  const f=fixture();Object.assign(f.snap,{steamRunning:false,backendReady:false,backendProcessCount:0,packAvailable:true,packPrepared:false,packOwned:false});f.snap.themes[0].configured=false;Object.assign(f,{firstPrepare:true,moveDuringPrepare});
  if(moveDuringPrepare){await assert.rejects(draft(f,{Round:false}),/主题目录已改变/);assert.deepEqual(f.calls,['Status','Prepare']);assert.equal(f.preferenceWrites,undefined);}
  else{const result=await draft(f,{Round:false});assert.equal(result.saved,true);assert.deepEqual(f.calls,['Status','Prepare','Apply']);assert.equal(f.applied.expectedRevision,'d'.repeat(64));assert.equal(Object.keys(f.applied.states).length,7);assert.equal(f.applied.states['Art Hero'],false);assert.equal(f.applied.states.Round,false);assert.equal(f.applied.states['Mini Carousel'],true);assert.deepEqual(f.preferences,{Round:false});}
  assert.equal(f.begin+f.ensure+f.end,0);
 }
});
await check('offline apply saves without any CSS or Steam launch',async()=>{const f=fixture();f.snap.steamRunning=false;f.snap.backendReady=false;f.snap.backendProcessCount=0;assert.equal((await f.api.apply(f.snap,{'Round':false})).ok,true);assert.equal(f.begin+f.ensure+f.end,0)});
await check('offline start does not bring CSS or Steam up',async()=>{const f=fixture();f.snap.steamRunning=false;await f.api.start();assert.equal(f.begin+f.ensure,0)});
await check('ready existing backend is adopted, not duplicated',async()=>{const f=fixture();await f.api.apply(f.snap,{'Round':false});assert(!f.group);assert(f.begin>0);assert.equal(f.ensure,0);assert.equal(f.end,1)});
await check('idle releases the request while retaining the resident backend',async()=>{const f=fixture();await f.api.start();const result=await f.api.idle(f.snap);assert(result.ok);assert.equal(f.end,1);assert.equal(f.group,false)});
await check('missing backend Ensure only uses native job IPC',async()=>{const f=fixture();f.snap.backendReady=false;f.snap.backendProcessCount=0;await f.api.start();assert.equal(f.ensure,1);assert(!f.group);assert.equal(f.end,1);await f.api.idle(f.snap);assert.equal(f.end,1)});
await check('Ensure failure requests native release with no repeated launch',async()=>{const f=fixture();f.snap.backendReady=false;f.snap.backendProcessCount=0;f.failEnsure=true;await assert.rejects(f.api.start(),/Ensure failed/);assert.equal(f.ensure,1);assert.equal(f.end,1);assert(!f.group)});
await check('Apply exception releases its request without restarting CSS',async()=>{const f=fixture();f.failApply=true;await assert.rejects(f.api.apply(f.snap,{'Round':false}),/Apply failure/);assert.equal(f.end,1);assert(!f.group)});
await check('frontend compatibility release retains CSS without a separate Idle process',async()=>{const f=fixture();await f.api.start();f.calls=[];const begins=f.begin;const result=await f.api.idle(f.snap);assert(result.ok);assert.equal(result.snapshot.backendProcessCount,2);assert.deepEqual(f.calls,['Status']);assert.equal(f.begin,begins);assert.equal(f.end,1);assert.equal(f.ensure,0)});
await check('deferred request release reports that CSS remains resident',async()=>{const f=fixture();f.holdEnd=true;await f.api.start();f.failIdle=true;const beforeBegin=f.begin;f.calls=[];const result=await f.api.idle(f.snap);assert(result.ok);assert.equal(f.begin,beforeBegin);assert.deepEqual(f.calls,['Status']);assert.equal(result.snapshot.backendProcessCount,2);assert.match(result.message,/常驻/);assert(!result.message.includes('临时 CSS 已释放'));assert.equal(f.ensure,0)});
await check('slow startup becomes ready on the fourth spaced read without a second CSS launch or extended lock wait',async()=>{const f=fixture();f.slowReady=4;f.snap.backendReady=false;f.snap.backendProcessCount=0;const ready=await f.api.start();assert(ready.backendReady);assert.equal(f.readyReads,4);assert.deepEqual(f.statusReads.slice(1).map(r=>r.at),[2000,7000,15000,24000]);assert(f.statusReads.every(r=>!r.payload.lockWait));assert.equal(clock,24000);assert.equal(f.ensure,1);assert.equal(f.end,1)});
await check('unavailable CSS does at most four startup reads and releases once',async()=>{const f=fixture();f.snap.backendReady=false;f.slowReady=100;await assert.rejects(f.api.start());assert.equal(f.readyReads,4);assert.equal(f.statusReads.length,5);assert.equal(f.ensure,1);assert.equal(f.end,1);assert(clock<=30000)});
await check('slow status IO is capped at the remaining 30 second startup deadline',async()=>{const f=fixture();f.snap.backendReady=false;f.slowReady=100;f.statusMs=7000;await assert.rejects(f.api.start(),/fixture timeout/);assert.equal(clock,30000);assert.equal(f.statusReads.at(-1).timeoutMs,6000);assert.equal(f.ensure,1);assert.equal(f.end,1)});
await check('completion received before the native reply wakes one fresh Status immediately',async()=>{
 const f=fixture();f.snap.backendReady=false;f.completionEvent=1;
 let receive;f.observe=()=>new Promise(resolve=>{receive=resolve});f.onEnsure=()=>{assert.equal(f.observations,1);clock+=650;receive('completed');};
 assert((await f.api.start()).backendReady);assert.deepEqual(f.statusReads.map(r=>r.at),[0,650]);assert.equal(f.closed,1);assert.equal(f.ensure,1);assert.equal(f.end,1);
});
await check('same readiness timestamps reduce either fixed waiting or premature script starts',async()=>{
 for(const readyAt of [650,8800]){
  const run=async events=>{
   const f=fixture();f.readyAt=readyAt;f.snap.backendReady=false;
   if(events){f.completionEvent=1;f.observe=()=>({then:resolve=>{clock=readyAt;resolve('completed');}});}
   assert((await f.api.start()).backendReady);assert.equal(f.ensure,1);
   return {firstConfirmationMs:clock,postEnsureStatusTimesMs:f.statusReads.slice(1).map(row=>row.at)};
  };
  const legacy=await run(false), event=await run(true);
  assert.deepEqual(legacy.postEnsureStatusTimesMs,readyAt===650?[2000]:[2000,7000,15000]);
  assert.deepEqual(event.postEnsureStatusTimesMs,[readyAt]);
  assert(event.firstConfirmationMs<legacy.firstConfirmationMs);
  comparisons.push({readyAt,legacy,event});
 }
});
await check('late completion skips premature and elapsed reads while still verifying the backend',async()=>{
 const f=fixture();f.snap.backendReady=false;f.completionEvent=1;f.slowReady=2;
 f.observe=()=>({then:resolve=>{clock+=8800;resolve('completed');}});
 assert((await f.api.start()).backendReady);assert.deepEqual(f.statusReads.map(r=>r.at),[0,8800,15000]);assert.equal(f.closed,1);assert.equal(f.ensure,1);
});
await check('lost completion falls back inside the original deadline without the early processes',async()=>{
 const f=fixture();f.snap.backendReady=false;f.completionEvent=1;f.slowReady=100;
 f.observe=()=>({then:resolve=>{clock+=15000;resolve('timeout');}});
 await assert.rejects(f.api.start(),/美化后端尚未就绪/);assert.deepEqual(f.statusReads.map(r=>r.at),[0,15000,24000]);assert.equal(f.closed,1);assert.equal(f.end,1);
});
await check('cancellation or a changed Steam identity ends before any post-Ensure shell call',async()=>{
 for(const changed of [false,true]){
  const f=fixture();f.snap.backendReady=false;f.completionEvent=1;
  f.observe=()=>({then:resolve=>{if(changed)f.policy.birth='101';resolve(changed?'completed':'canceled');}});
  await assert.rejects(f.api.start(),changed?/Steam 会话已变化/:/启动请求已取消/);assert.deepEqual(f.calls,['Status']);assert.equal(f.closed,1);assert.equal(f.end,1);
 }
});
await check('failed launch closes the completion subscription before releasing the lease',async()=>{
 const f=fixture();f.snap.backendReady=false;f.failEnsure=true;f.onEnd=()=>assert.equal(f.closed,1);
 await assert.rejects(f.api.start(),/fixture Ensure failed/);assert.deepEqual(f.calls,['Status']);assert.equal(f.ensure,1);assert.equal(f.end,1);
});
await check('unconfirmed snapshot also releases a previous lease without running Idle',async()=>{const f=fixture();await f.api.start();f.calls=[];const result=await f.api.idle({...f.snap,configuredMatchesBackend:false});assert(result.ok);assert.deepEqual(f.calls,['Status']);assert.equal(f.end,1)});
await check('old native protocol cannot create unmanaged CSS',async()=>{const f=fixture();f.policy.version=1;f.snap.backendReady=false;await assert.rejects(f.api.start(),/同步更新/);assert.equal(f.begin+f.ensure+f.end,0)});
await check('native Steam identity changes cannot accept a stale ticket',async()=>{const f=fixture();f.policy.birth='101';await assert.rejects(f.api.start(),/会话已变化/);assert.equal(f.end,1);assert.equal(f.ensure,0)});
await check('sequential edits release each lease and keep reusing the existing resident backend',async()=>{const f=fixture();await f.api.apply(f.snap,{'Round':false});await f.api.apply(f.snap,{'Round':true});assert(!f.group);assert.equal(f.begin,2);assert.equal(f.ensure,0);assert.equal(f.end,2);await f.api.idle(f.snap);assert.equal(f.end,2)});
await check('overlapping writes fail fast rather than queue or start more CSS',async()=>{const f=fixture();let release;f.wait=new Promise(r=>release=r);const first=f.api.start();await Promise.resolve();await assert.rejects(f.api.start(),/进行中/);release();await first;assert.equal(f.ensure,0)});
await check('native release failure surfaces at operation completion and is never retried with a stale token',async()=>{const f=fixture();f.failEnd=true;await assert.rejects(f.api.start(),/释放未确认/);assert.equal(f.snap.backendProcessCount,1);assert.equal(f.end,1);assert(!f.group)});
await check('old PowerControl script is refused before any CSS lease or write',async()=>{const f=fixture();delete f.snap.controlProtocol;await assert.rejects(f.api.apply(f.snap,{'Round':false}),/外观脚本与当前 YMCC 不配套/);assert.deepEqual(f.calls,['Status']);assert.equal(f.begin+f.ensure+f.end,0)});
await check('reset saves defaults before a missing native CSS interface is reported',async()=>{const f=fixture();f.policy.version=1;const result=await f.api.reset('preset');assert.equal(result.ok,false);assert.equal(result.saved,true);assert(f.calls.includes('Reset'));assert.equal(f.begin+f.ensure,0);assert.match(result.message,/配置已保存/)});

const sceneOptions=f=>{
 f.bootDocument={input:{revision:1},extensions:{bootModes:{schema:1,current:'steam',revision:2}}};
 return {origin:'boot-mode',bootContext:structuredClone(f.bootDocument)};
};
await check('scene selection replaces the two standalone Status helpers with one owned Status',async()=>{
 const before=fixture(),options=sceneOptions(before);
 const previous=await before.api.apply(await before.api.read(),{Round:false},options);
 const after=fixture(),current=await after.api.applyScene({Round:false},sceneOptions(after),read=>read());
 assert.deepEqual(before.calls,['Status','Status','Apply']);
 assert.deepEqual(after.calls,['Status','Apply']);
 assert.deepEqual([after.existPaths.length,after.policyReads],[before.existPaths.length-1,before.policyReads-1]);
 assert.deepEqual(after.applied,before.applied);assert.deepEqual(current,previous);
 assert.equal(after.preferenceWrites,undefined);assert.deepEqual([after.begin,after.ensure,after.end],[1,0,1]);
});
await check('scene selection is fenced after observation and before the script write',async()=>{
 for(const point of ['before','after','apply']){
  const f=fixture(),options=sceneOptions(f);
  if(point==='before')f.bootDocument.extensions.bootModes.revision++;
  if(point==='apply')f.beforeApply=()=>{f.snap.revision='d'.repeat(64);};
  await assert.rejects(f.api.applyScene({Round:false},options,async read=>{
   const state=await read();if(point==='after')f.bootDocument.extensions.bootModes.revision++;return state;
  }),point==='apply'?/Theme settings changed/:/BOOT_APPEARANCE_REQUEST_EXPIRED/);
  assert.deepEqual(f.calls,point==='before'?[]:point==='after'?['Status']:['Status','Apply']);
  assert.equal(f.begin,f.end);assert.equal(f.ensure,0);assert.equal(f.snap.themes.find(row=>row.name==='Round').configured,true);
 }
});
await check('a timed-out scene observation stops at the next boundary, including after an owner read',async()=>{
 for(const [point,counts] of [['location',[0,0,0]],['file-owner',[0,0,0]],['policy-owner',[1,0,0]],['shell-owner',[1,1,0]],['status',[1,1,1]]]){
  const f=fixture(),options=sceneOptions(f);let unblock,observed,enter;
  const held=new Promise(r=>unblock=r),entered=new Promise(r=>enter=r);
  if(point==='location'){f.holdLocation=held;f.onLocation=enter;}
  else if(point==='status'){f.wait=held;f.statusResponses={shift:()=>{enter();}};}
  else{
   let ownerReads=0;
   const reached=()=>point==='file-owner'?f.locations:point==='policy-owner'?f.existPaths.length:f.policyReads;
   f.onBootPreferences=()=>{if(reached()&&++ownerReads===2){enter();return held;}};
  }
  let expire;
  const deadline=new Promise((_,reject)=>expire=()=>reject(Error('fixture deadline')));
  const failed=assert.rejects(f.api.applyScene({Round:false},options,read=>{observed=read();return Promise.race([observed,deadline]);}),/fixture deadline/);
  await entered;expire();await failed;unblock();await assert.rejects(observed,/Appearance observation retired/);
  assert.deepEqual(f.calls,point==='status'?['Status']:[],point);
  assert.deepEqual([f.existPaths.length,f.policyReads,f.calls.length],counts,point);
  assert.deepEqual([f.begin,f.ensure,f.end],[0,0,0]);
 }
});
await check('a repeated observed mismatch reconciles once with no native launch',async()=>{const f=fixture();f.snap.packOwned=true;f.snap.configuredMatchesBackend=false;await f.api.reconcileExisting(f.snap);await f.api.reconcileExisting(f.snap);assert.deepEqual(f.calls,['Reconcile']);assert.equal(f.begin+f.ensure+f.end,0)});
await check('same-session native automatic apply owns repair without an extra IPC or consuming the anomaly key',async()=>{
 const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});f.policy.automaticRepairActive=true;
 assert.equal(await f.api.reconcileExisting(f.snap),undefined);assert.deepEqual(f.calls,[]);assert.equal(f.policyReads,1);
 f.policy.automaticRepairActive=false;
 assert((await f.api.reconcileExisting(f.snap)).ok);await f.api.reconcileExisting(f.snap);
 assert.deepEqual(f.calls,['Reconcile']);assert.equal(f.policyReads,2);assert.equal(f.begin+f.ensure+f.end,0);
});
await check('stale session retires before reconciliation and compatibility responses do not claim native ownership',async()=>{
 const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});
 f.policy.birth='101';f.policy.automaticRepairActive=true;
 assert.equal(await f.api.reconcileExisting(f.snap),undefined);assert.deepEqual(f.calls,[]);
 f.policy.birth='100';delete f.policy.automaticRepairActive;
 assert((await f.api.reconcileExisting(f.snap)).ok);assert.deepEqual(f.calls,['Reconcile']);
 const bad=fixture();Object.assign(bad.snap,f.snap);bad.policy.automaticRepairActive='true';
 assert((await bad.api.reconcileExisting(bad.snap)).ok);assert.deepEqual(bad.calls,['Reconcile']);
});
await check('native automatic repair hint never suppresses explicit user reapplication',async()=>{
 const f=fixture();f.policy.automaticRepairActive=true;
 assert((await f.api.refresh()).ok);assert.deepEqual(f.calls,['Status','Refresh']);assert.deepEqual([f.begin,f.ensure,f.end],[1,0,1]);
});
await check('a newer accepted automatic apply makes an older observed mismatch stale without starting Reconcile',async()=>{
 const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});
 f.policy.automaticAppliedRevision=0;const old=await f.api.read();f.policy.automaticAppliedRevision=1;
 assert.equal(await f.api.reconcileExisting(old),'superseded');assert.deepEqual(f.calls,['Status']);assert.equal(f.policyReads,2);
 assert((await f.api.reconcileExisting(await f.api.read())).ok);
 assert.deepEqual(f.calls,['Status','Status','Reconcile']);assert.equal(f.policyReads,4);
});
await check('invalid or absent completion revisions cannot suppress a necessary Reconcile',async()=>{
 for(const [first,later] of [[undefined,1],[0,'1'],[0,Infinity],[0,1.5],[5,4],[5,5]]){
  const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});
  f.policy.automaticAppliedRevision=first;const read=await f.api.read();f.policy.automaticAppliedRevision=later;
  assert((await f.api.reconcileExisting(read)).ok);assert.deepEqual(f.calls,['Status','Reconcile']);
 }
});
await check('Steam exit retires an earlier mismatch before starting PowerShell',async()=>{
 const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});
 Object.assign(f.policy,{pid:0,birth:'0'});
 assert.equal(await f.api.reconcileExisting(f.snap),undefined);assert.deepEqual(f.calls,[]);assert.equal(f.policyReads,1);
 Object.assign(f.policy,{pid:42,birth:'101'});
 assert((await f.api.reconcileExisting({...f.snap,steamBirth:'101'})).ok);
 assert.deepEqual(f.calls,['Reconcile']);assert.equal(f.policyReads,2);
});
await check('only a fresh Steam lifetime may retry the same observed mismatch once',async()=>{
 const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});
 f.wait=Promise.reject(Error('reconcile transport failed'));f.wait.catch(()=>{});
 await assert.rejects(f.api.reconcileExisting(f.snap),/reconcile transport failed/);
 f.wait=null;
 assert.equal((await f.api.reconcileExisting(f.snap)).ok,false);
 f.policy.birth='101';const next={...f.snap,steamBirth:'101'};
 assert((await f.api.reconcileExisting(next)).ok);
 assert.equal((await f.api.reconcileExisting(next)).ok,false);
 assert.deepEqual(f.calls,['Reconcile','Reconcile']);assert.equal(f.policyReads,2);assert.equal(f.begin+f.ensure+f.end,0);
});
await check('retirement during reconciliation preflight skips the helper and does not consume the fresh attempt',async()=>{
 const f=fixture();f.snap.packOwned=true;f.snap.configuredMatchesBackend=false;
 let release,current=true;
 const entered=new Promise(resolve=>{f.onLocation=resolve;});
 f.holdLocation=new Promise(resolve=>release=resolve);
 const retired=f.api.reconcileExisting(f.snap,()=>current);
 // The real service caches the directory; hold the first resolution until retirement.
 await entered;assert.equal(f.locations,1);
 current=false;release();assert.equal(await retired,undefined);assert.deepEqual(f.calls,[]);
 assert.deepEqual([f.existPaths.length,f.policyReads],[0,0]);
 await f.api.reconcileExisting(f.snap,()=>true);await f.api.reconcileExisting(f.snap,()=>true);
 assert.deepEqual(f.calls,['Reconcile']);assert.equal(f.begin+f.ensure+f.end,0);
});
await check('manual intent retires an undispatched correction without consuming its anomaly key',async()=>{
 for(const locationError of [undefined,true]){
  const f=fixture();Object.assign(f.snap,{packOwned:true,configuredMatchesBackend:false,steamPid:42,steamBirth:'100'});
  let release,canDispatch=true;
  const entered=new Promise(resolve=>{f.onLocation=resolve;});
  f.holdLocation=new Promise(resolve=>release=resolve);
  const retired=f.api.reconcileExisting(f.snap,()=>true,()=>canDispatch);
  await entered;canDispatch=false;f.locationError=locationError;release();
  assert.equal(await retired,undefined);assert.deepEqual(f.calls,[]);
  assert.deepEqual([f.existPaths.length,f.policyReads],[0,0]);
  f.locationError=false;
  assert((await f.api.reconcileExisting(f.snap)).ok);
  assert.equal((await f.api.reconcileExisting(f.snap)).ok,false);
  assert.deepEqual(f.calls,['Reconcile']);assert.equal(f.begin+f.ensure+f.end,0);
 }
});
await check('offline or unowned appearance never triggers automatic mutation',async()=>{const f=fixture();f.snap.packOwned=false;await f.api.reconcileExisting(f.snap);f.snap.packOwned=true;f.snap.backendReady=false;await f.api.reconcileExisting(f.snap);assert.deepEqual(f.calls,[]);assert.equal(f.begin+f.ensure,0)});
await check('a success flag cannot override an explicit failed render receipt',async()=>{const f=fixture();f.snap.packOwned=true;f.renderReceipt=false;await assert.rejects(f.api.reconcileExisting(f.snap),/页面样式确认矛盾/);assert.equal(f.ensure+f.begin,0)});
await check('nonboolean render receipts are refused as invalid control data',async()=>{const f=fixture();f.snap.packOwned=true;f.renderReceipt='true';await assert.rejects(f.api.reconcileExisting(f.snap),/无效状态/);assert.equal(f.ensure+f.begin,0)});
await check('typed snapshots accept a bounded per-file render plan',async()=>{const f=fixture();f.snap.themes[0].renderPlan={protocol:4,variables:[],files:[{path:'shared.css',property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true}]};assert(validAppearanceSnapshot(f.snap));});
await check('typed snapshots refuse malformed or oversized render plans',async()=>{for(const plan of [{protocol:1,files:[]},{protocol:4,variables:[],files:[{path:'../bad.css',property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true}]},{protocol:4,variables:[],files:Array.from({length:129},(_,i)=>({path:'shared.css',property:'--ymcc-file-'+i.toString(16).padStart(16,'0'),enabled:true}))},{protocol:4,variables:[],files:[{path:'shared.css',property:'arbitrary',enabled:true}]}]){const f=fixture();f.snap.themes[0].renderPlan=plan;assert(!validAppearanceSnapshot(f.snap));}});
await check('typed snapshots reject unsafe, repeated and OFF-stale variable plans',async()=>{for(const variables of [[{property:'--scale-value',enabled:true,value:'0.5;color:red'}],[{property:'--scale-value',enabled:true,value:'0.5'},{property:'--scale-value',enabled:true,value:'0.9'}],[{property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true,value:'1'}],[{property:'--scale-value',enabled:false,value:'0.5'}]]){const f=fixture();f.snap.themes[0].renderPlan={protocol:4,files:[{path:'shared.css',property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true}],variables};assert(!validAppearanceSnapshot(f.snap));}});
await check('old resident policy without the manual observation capability is refused before a lease',async()=>{const f=fixture();delete f.policy.manualObservationProtocol;await assert.rejects(f.api.start(),/同步更新/);assert.equal(f.begin+f.ensure+f.end,0)});
await check('a slow release still serializes the next operation and cannot release its token',async()=>{const f=fixture();let unblock;f.holdRelease=new Promise(r=>unblock=r);const entered=new Promise(r=>f.onEnd=r);const pending=f.api.start();let deadline;try{await Promise.race([entered,new Promise((_,reject)=>deadline=setTimeout(()=>reject(Error("completion release was not observed")),1000))])}finally{clearTimeout(deadline)}assert.equal(f.end,1);await assert.rejects(f.api.start(),/进行中/);assert.equal(f.begin,1);unblock();await pending;f.holdRelease=null;await f.api.start();assert.equal(f.begin,2);assert.equal(f.end,2);assert(!f.group)});
await check('failed Apply preserves the original error even when native release also fails',async()=>{const f=fixture();f.failApply=true;f.failEnd=true;await assert.rejects(f.api.apply(f.snap,{'Round':false}),/Apply failure/);assert.equal(f.begin,1);assert.equal(f.end,1)});
await check('idle cannot report old success after final Status transport failure',async()=>{const f=fixture();f.failStatus=true;const result=await f.api.idle(f.snap);assert.equal(result.ok,false);assert.match(result.message,/状态暂未确认/);assert.equal(f.begin+f.ensure+f.end,0)});
await check('idle cannot report old success after final Status rejection',async()=>{const f=fixture();f.statusFailure=true;const result=await f.api.idle(f.snap);assert.equal(result.ok,false);assert.equal(result.message,'status rejected');assert.equal(f.begin+f.ensure+f.end,0)});
await check('typed render v4 refuses old plans and missing, unsafe or duplicate content identities',async()=>{
 for(const mutation of ['old','missing','unsafe','duplicate']){const f=fixture();const file={path:'shared.css',property:'--ymcc-file-0123456789abcdef',contentProperty:'--ymcc-content-abcdef0123456789',enabled:true};const plan={protocol:4,variables:[],files:[file]};if(mutation==='old')plan.protocol=3;if(mutation==='missing')delete file.contentProperty;if(mutation==='unsafe')file.contentProperty='--ymcc-content-<invalid>';if(mutation==='duplicate')plan.files.push({...file,path:'other.css',property:'--ymcc-file-0000000000000000'});f.snap.themes[0].renderPlan=plan;assert(!validAppearanceSnapshot(f.snap));}
});
await check('appearance requests carry the native owner channel and exact Steam identity as discrete arguments',async()=>{
 const f=fixture();f.policy.processQuery='Local\\YMCC.CssInventory.'+'a'.repeat(32);await f.api.read();
 assert.equal(f.lastArgs[f.lastArgs.indexOf('-ProcessQuery')+1],f.policy.processQuery);
 assert.equal(f.lastArgs[f.lastArgs.indexOf('-SessionPid')+1],'42');assert.equal(f.lastArgs[f.lastArgs.indexOf('-SessionBirth')+1],'100');assert.equal(f.begin+f.ensure,0);
});
await check('malformed owner channel cannot be forwarded to PowerShell',async()=>{
 const f=fixture();f.policy.processQuery='Local\\YMCC.CssInventory.invalid" -Action Ensure';await assert.rejects(f.api.read(),/公共读取组件/);assert.equal(f.lastArgs,undefined);assert.equal(f.begin+f.ensure,0);
});
await check('missing public Steam protocol refuses old native before launching any PowerShell request',async()=>{const f=fixture();delete f.policy.sharedSteamReadProtocol;await assert.rejects(f.api.read(),/公共读取组件/);assert.deepEqual(f.calls,[]);assert.equal(f.begin+f.ensure,0)});
const result={ok:true,passed:cases.length,cases,comparisons,actualSteamOperations:0,actualCssOperations:0};if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
