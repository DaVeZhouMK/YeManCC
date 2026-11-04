import fs from 'node:fs';import crypto from 'node:crypto';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {fileURLToPath} from 'node:url';
export async function manualRepairTests(root,out,{upgradeActionsFile=process.env.YMCC_DECKY_UPGRADE_ACTIONS,sourceOverride=null,embeddedOverride=null}={}){
 fs.mkdirSync(out,{recursive:true});const sourcePath=sourceOverride??path.join(root,'native/steam_live.js'),embeddedPath=embeddedOverride??path.join(root,'native/steam_live_script.h'),hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
 fs.writeFileSync(path.join(out,'manual-repair-inputs.json'),JSON.stringify({source:{path:sourcePath,sha256:hash(sourcePath)},embedded:{path:embeddedPath,sha256:hash(embeddedPath)},consumer:{path:path.join(root,'tools/decky_manual_repair_selftest.mjs'),sha256:hash(path.join(root,'tools/decky_manual_repair_selftest.mjs'))},overrides:{source:!!sourceOverride,embedded:!!embeddedOverride},scope:'Actual full source and embedded repair script in isolated VM; no native Steam action'},null,2)+'\n');
 const source=fs.readFileSync(sourcePath,'utf8');const header=fs.readFileSync(embeddedPath,'utf8').split('inline constexpr const char kUiModeScript[] =')[0];
 const embedded=[...header.matchAll(/R"YMSTEAM\(([\s\S]*?)\)YMSTEAM"/g)].map(m=>m[1]).join('');const cases=[],traces=[];
 const invoke=async(script,options={})=>{
  const old='a'.repeat(64),fresh='b'.repeat(64),request={operation:'decky.repairConnection',userInitiated:true,expectedFrontendBuild:old,installedFrontendBuild:fresh,loaderVersion:'v3.3.0',...options.request};
  const loader={plugins:[{name:'ymcc-sidebar'}],deckyState:{publicState:()=>({versionInfo:{current:options.version??'v3.3.0'}})}};
  const window={DeckyPluginLoader:loader,__YMCC_DECKY_FRONTEND__:{active:!options.inactive,buildId:options.build??old},deckyHasLoaded:true};window.top=options.frame?{}:window;
  if(options.emptyFrontend){window.__YMCC_DECKY_FRONTEND__=undefined;loader.plugins=[];request.expectedFrontendBuild=null;window.__YMCC_DECKY_MIRROR__={frontendBuildId:fresh,runId:'d'.repeat(32),token:'e'.repeat(64),endpoint:'ws://127.0.0.1:12345/mirror'};if(options.noBridge)delete window.__YMCC_DECKY_MIRROR__;if(options.wrongBridge)window.__YMCC_DECKY_MIRROR__.frontendBuildId=old;}
  let actualAccount=9;window.__YMCC_DECKY_ACCOUNT_WATCH__={read:account=>account===actualAccount};
  if(options.cleanup){request.expectedAccount=options.request?.expectedAccount??9;delete request.userInitiated;request.operation='decky.cleanupConnection';request.cleanupAuthorized=options.request?.cleanupAuthorized??true;request.expectedModeEpoch=options.epoch??options.request?.expectedModeEpoch??'current-document:1';window.__ymccSteamUiModeWatch={id:'current-document',entries:1,mode:'bigpicture'};}
  if(options.automatic){request.operation='decky.recoverConnection';delete request.userInitiated;request.automatic=options.noAutomatic?false:true;}if(options.noPlugin)loader.plugins=[];if(options.pending)window.__ymccDeckyManualRepair=true;
  let refreshes=0,modeReads=0,restartAttempts=0,timerErrors=0;const timers=[];const SteamClient={UI:{GetUIMode:async()=>{modeReads++;if(options.accountModeRace)actualAccount=10;if(options.race)window.__YMCC_DECKY_FRONTEND__={active:true,buildId:fresh};return options.desktop?7:4}},Browser:{RestartJSContext(){restartAttempts++;refreshes++;if(options.restartThrows)throw Error("fixture-restart-result-uncertain");}}};
  const context=vm.createContext({window,location:{origin:options.foreign?'https://example.invalid':'https://steamloopback.host'},SteamClient,Date,Math,Promise,Number,Array,Object,setTimeout(fn,ms){if(ms===0){timers.push(fn);return 1}return setTimeout(fn,ms)},clearTimeout});
  const result=await vm.runInContext(script+'('+JSON.stringify(request)+')',context);const before=refreshes;if(options.timerRace)window.__YMCC_DECKY_FRONTEND__={active:true,buildId:fresh};if(options.accountRace)actualAccount=10;if(options.epochRace)window.__ymccSteamUiModeWatch.entries++;if(options.desktopRace)window.__ymccSteamUiModeWatch.mode='desktop';if(options.markerRace)window.__ymccDeckyManualRepair={foreignOwner:true};
  if(options.timerGuardThrows)loader.deckyState.publicState=()=>{throw Error('fixture-guard-read-before-restart');};
  const runTimer=fn=>{try{fn();}catch(error){if(!options.restartThrows)throw error;timerErrors++;}};
  const originalTimers=timers.slice();for(const timer of originalTimers)runTimer(timer);
  if(options.duplicateCallback)for(const timer of originalTimers)runTimer(timer);
  let retry;
  if(options.freshManualRetry){
   if(options.timerGuardThrows)loader.deckyState.publicState=()=>({versionInfo:{current:options.version??'v3.3.0'}});
   const freshRequest={operation:'decky.repairConnection',userInitiated:true,expectedFrontendBuild:window.__YMCC_DECKY_FRONTEND__?.buildId,installedFrontendBuild:fresh,loaderVersion:options.version??'v3.3.0'};
   const firstTimerCount=timers.length,firstRefreshes=refreshes;
   const next=await vm.runInContext(script+'('+JSON.stringify(freshRequest)+')',context);
   const freshMarker=window.__ymccDeckyManualRepair;
   for(const timer of originalTimers)runTimer(timer); // Late old callback must not clear the new operation's marker.
   const markerPreserved=window.__ymccDeckyManualRepair===freshMarker;
   for(const timer of timers.slice(firstTimerCount))runTimer(timer);
   retry={result:JSON.parse(JSON.stringify(next)),firstRefreshes,markerPreserved};
  }
  const observed={result:JSON.parse(JSON.stringify(result)),refreshes,before,modeReads,timerCount:timers.length,restartAttempts,timerErrors,retry};
  traces.push({scriptSha256:crypto.createHash('sha256').update(script).digest('hex'),scenario:Object.keys(options).filter(k=>options[k]===true),...observed});
  fs.writeFileSync(path.join(out,'manual-repair-traces.json'),JSON.stringify({realSteamActions:0,settingsWrites:0,traces},null,2)+'\n');
  return{...observed,window};
 };
 for(const [kind,script] of Object.entries({source,embedded})){
  for(const [label,options] of Object.entries({'empty owned frontend':{emptyFrontend:true},'automatic empty owned frontend':{automatic:true,emptyFrontend:true},'empty without own bridge':{emptyFrontend:true,noBridge:true},'empty with wrong installed build':{emptyFrontend:true,wrongBridge:true},'empty wrong official version':{emptyFrontend:true,version:'v3.2.10'},'empty module races mode read':{emptyFrontend:true,race:true},'explicit repair':{},'approved automatic repair':{automatic:true},'automatic without origin':{automatic:true,noAutomatic:true},'no consent':{request:{userInitiated:false}},'extra arguments':{request:{force:true}},'bad old build':{request:{expectedFrontendBuild:'x'}},'different actual module':{build:'c'.repeat(64)},'inactive module':{inactive:true},'foreign page':{foreign:true},'subframe':{frame:true},'no plugin registration':{noPlugin:true},'wrong official version':{version:'v3.2.10'},'desktop':{desktop:true},'module replaced during async mode read':{race:true},'already scheduled':{pending:true}})){
   const r=await invoke(script,options);const allowed=label==='explicit repair'||label==='empty owned frontend';assert.equal(r.result.ok,allowed,kind+': '+label);assert.equal(r.refreshes,allowed?1:0,kind+': '+label+' actual refresh count');assert.equal(r.before,0,'result only acknowledges a scheduled request');cases.push(kind+': '+label);
  }
  for(const options of [{cleanup:true},{cleanup:true,epoch:'old-document:1'},{cleanup:true,epochRace:true},{cleanup:true,desktopRace:true},{cleanup:true,desktop:true},{cleanup:true,timerRace:true},{cleanup:true,request:{cleanupAuthorized:false}},{cleanup:true,accountRace:true},{cleanup:true,accountModeRace:true}]){
   const r=await invoke(script,options);const allowed=!options.epoch&&!options.epochRace&&!options.desktopRace&&!options.desktop&&!options.timerRace&&!options.request&&!options.accountRace&&!options.accountModeRace;
   assert.equal(r.refreshes,allowed?1:0,kind+': bounded cleanup target');assert.equal(r.before,0);cases.push(kind+': final cleanup rechecks consent document module and mode');
  }
  for(const options of [{automatic:true},{automatic:true,emptyFrontend:true},{automatic:true,desktop:true}]){const r=await invoke(script,options);assert.equal(r.result.reason,'steam-ui-refresh-requires-user');assert.equal(r.result.mutated,false);assert.equal(r.refreshes,0);assert.equal(r.modeReads,0);assert.equal(r.timerCount,0);assert.equal(r.window.__ymccDeckyManualRepair,undefined);cases.push(kind+': automatic UI rebuild is denied before markers or timers');}
  const unsafe=script.replace("if (automatic) fail('steam-ui-refresh-requires-user');",'');assert.notEqual(unsafe,script);const autoNegative=await invoke(unsafe,{automatic:true});assert.equal(autoNegative.refreshes,1,'removed automatic UI guard reproduces the field window rebuild');cases.push(kind+': removed automatic guard reproduces unauthorized Steam UI rebuild');
  const raced=await invoke(script,{timerRace:true});assert.equal(raced.refreshes,0,'queued callback rechecks actual module');cases.push(kind+': stale scheduled target cannot refresh replacement module');
  for(const scenario of [{accountRace:true},{epochRace:true},{desktopRace:true},{timerRace:true},{timerGuardThrows:true}]){
   const result=await invoke(script,{cleanup:true,...scenario,freshManualRetry:true});
   assert.equal(result.retry.firstRefreshes,0,kind+': cancelled cleanup actually mutated before retry');
   assert.equal(result.retry.result.ok,true,kind+': positively cancelled callback must not poison a fresh explicit repair');
   assert.equal(result.retry.markerPreserved,true,kind+': old callback cleared a newer repair reservation');
   assert.equal(result.refreshes,1,kind+': fresh explicit repair after cancellation executes exactly once');
   cases.push(kind+': cancelled cleanup releases only its own marker and allows fresh explicit repair '+Object.keys(scenario)[0]);
  }
  const uncertain=await invoke(script,{restartThrows:true,freshManualRetry:true});
  assert.equal(uncertain.restartAttempts,1,kind+': uncertain RestartJSContext was replayed');
  assert.equal(uncertain.retry.result.reason,'decky-repair-already-requested');assert.equal(uncertain.timerErrors,1);
  cases.push(kind+': exception after entering actual restart keeps the repair marker spent');
  const duplicate=await invoke(script,{duplicateCallback:true});assert.equal(duplicate.restartAttempts,1,kind+': duplicate callback invoked restart twice');cases.push(kind+': duplicate callback cannot repeat the actual restart');
  const foreignReservation=await invoke(script,{markerRace:true});assert.equal(foreignReservation.refreshes,0,kind+': old callback executed after reservation replacement');assert.equal(foreignReservation.window.__ymccDeckyManualRepair.foreignOwner,true);
  cases.push(kind+': replaced reservation is not consumed or cleared by the old callback');
  // A negative must reproduce the unauthorized refresh, not just remove source text.
  const negative=script.replace('request.userInitiated !== true','false');assert.notEqual(negative,script);
  const r=await invoke(negative,{request:{userInitiated:false}});assert.equal(r.refreshes,1,'negative must lose the exact consent boundary');cases.push(kind+': removed consent guard reproduces unauthorized refresh');
 }
 const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');const handler=main.slice(main.indexOf('ipc_on("deckySidebar.repairConnection"'),main.indexOf('ipc_on("deckySidebar.setEnabled"'));
 const deps=main.slice(main.indexOf('static ymcc::deckyrepair::Dependencies deckySidebarRepairDependencies'),main.indexOf('static json deckySidebarConnectionState'));
 assert(handler.includes('g_deckyConnectionRepair.request(args,deckySidebarRepairDependencies())')&&deps.includes('allowManualRepair(session)')&&deps.includes('deckySidebarEnvironment(false)')&&deps.includes('deckySidebarPortPreflight()'));
 assert(deps.includes('PowerLifecycle::Ready'),'final admission retains native power boundary');
 assert(deps.includes('ymcc::steamui::read(g_steamSettingsObserver,true)'),'manual request has one native current context reader');
 // When supplied, consume real serialized actions produced by compiled native
 // target selection, not separately invented JS requests. Same canonical test.
 const upgradeFile=upgradeActionsFile;
 if(upgradeFile){const native=JSON.parse(fs.readFileSync(upgradeFile,'utf8'));assert.equal(native.pass,true);assert(native.upgradeActions?.length>=3);
  const trace=[];
  assert(native.cleanupActions?.length>=1,'compiled cleanup producer has no actual serialized final action');
  for(const row of native.cleanupActions){for(const [kind,script]of Object.entries({source,embedded})){
   const action=row.action;const result=await invoke(script,{cleanup:true,request:action,version:action.loaderVersion,build:action.expectedFrontendBuild});
   assert.equal(result.result.ok,true);assert.equal(result.refreshes,1);assert.equal(result.before,0);
   for(const race of [{epochRace:true},{desktopRace:true},{timerRace:true},{accountRace:true},{accountModeRace:true}]){const denied=await invoke(script,{cleanup:true,request:action,version:action.loaderVersion,build:action.expectedFrontendBuild,...race});assert.equal(denied.refreshes,0);}
   cases.push(kind+': compiled native final-cleanup action consumed by actual JS with epoch desktop and module races');
  }}
  for(const row of native.upgradeActions){for(const [kind,script]of Object.entries({source,embedded})){
   const action=row.action;
   const normal=await invoke(script,{request:action,version:row.loadedLoader,build:action.expectedFrontendBuild});
   assert.equal(normal.result.ok,true);assert.equal(normal.result.requestDispatched,true);assert.equal(normal.refreshes,1);assert.equal(normal.before,0);
   const stale=await invoke(script,{request:action,version:row.installedLoader,build:action.expectedFrontendBuild});
   assert.equal(stale.result.ok,false);assert.equal(stale.refreshes,0,'new page cannot consume old document refresh');
   const late=await invoke(script,{request:action,version:row.loadedLoader,build:action.expectedFrontendBuild,timerRace:true});assert.equal(late.refreshes,0,'new module cannot consume old scheduled refresh');
   const automatic=await invoke(script,{request:action,version:row.loadedLoader,build:action.expectedFrontendBuild,automatic:true});assert.equal(automatic.result.reason,'steam-ui-refresh-requires-user');assert.equal(automatic.refreshes,0);assert.equal(automatic.modeReads,0);
   trace.push({kind,installed:row.installedLoader,loaded:row.loadedLoader,normalRefreshes:normal.refreshes,staleRefreshes:stale.refreshes,lateRefreshes:late.refreshes,automaticRefreshes:automatic.refreshes});
   cases.push(kind+': serialized native upgrade action '+row.loadedLoader+' -> '+row.installedLoader+' preserves exact document fencing');
  }}
  fs.writeFileSync(path.join(out,'native-to-js-upgrade-scenarios.json'),JSON.stringify({pass:true,nativeActionFile:upgradeFile,scope:'Actual compiled native target -> production source and embedded JS; memory Steam API, no real Steam actions',trace},null,2));
 }
 const repairHeader=fs.readFileSync(path.join(root,'native/decky_sidebar_manual_repair.h'),'utf8');const autoGuard='if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user")';assert(repairHeader.includes(autoGuard)&&repairHeader.indexOf(autoGuard)<repairHeader.indexOf('const auto session=deps.session();'),'ordinary automatic demand is rejected before context IO');assert(repairHeader.includes('!deps.reserveCleanup||!deps.freshMode'),'final cleanup requires durable reservation and a fresh context read');
 assert(!handler.includes('setEnabled(true)')&&!handler.includes('startOwned')&&!handler.includes('steam.uiMode'));
 const automatic=main.slice(main.indexOf('static void deckySidebarObserveAutomaticRepair'),main.indexOf('static void deckySidebarInitialize'));
 assert(automatic.includes('g_deckyAutomaticRepair.observe(g_deckyConnectionRepair,deckySidebarRepairDependencies()')&&automatic.includes('PowerLifecycle::Ready'));
 const wake=main.slice(main.indexOf('case WM_POWER_RESUME_COMMIT:'),main.indexOf('case WM_POWER_INPUT_RECOVER:'));
 assert(wake.includes('requestBindingRefresh(currentPowerGeneration())')&&wake.includes('requestConnectionCheck()')&&wake.includes('wake-connection-check'));

 const bridge=fs.readFileSync(path.join(root,'src/bridge/deckySidebar.ts'),'utf8'),page=fs.readFileSync(path.join(root,'src/views/SteamView.vue'),'utf8');
 assert(bridge.includes("'deckySidebar.repairConnection', {userInitiated:true,pid,birth,revision}"));assert(page.includes('@click="repairDeckyConnection"'));
 assert.equal((page.match(/await deckySidebar\.repairConnection\(/g)||[]).length,1,'only the explicit click handler requests repair');
 assert(!fs.readFileSync(path.join(root,'native/decky_sidebar_bootstrap.h'),'utf8').includes('RestartJSContext()'),'automatic bootstrap does not inherit explicit repair');
 cases.push('Actual native handler uses shared orchestrator with resource/port/consent checks and no retry','Actual Vue consumer requests repair only from explicit click with current identity');
 fs.writeFileSync(path.join(out,'manual-repair.json'),JSON.stringify({passed:true,cases:cases.length,results:cases,scope:'Production full/embedded script in VM only; native orchestrator and actual Vue execution are separate. Prior serialized native actions are references, not current native execution.',realSteamActions:0,settingsWrites:0,nativeExecutedThisRun:false,serializedNativeActionsReference:upgradeFile?{path:upgradeFile,sha256:hash(upgradeFile),producerExecutedThisRun:false}:null},null,2));return cases.length;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});console.log('PASS manual repair cases='+await manualRepairTests(root,out));}
