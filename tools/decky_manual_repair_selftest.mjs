import fs from 'node:fs';import crypto from 'node:crypto';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {fileURLToPath} from 'node:url';
import {uniqueAnchor,cppBlock,extractDeckyReady} from './ymcc_sim/global/decky-update-recovery/native-source-admission.mjs';
// Follow the current shared native owner; these are source wiring checks, not native execution.
function admitManualSources(root,io){
 const read=relative=>io.readFileSync(path.join(root,relative),'utf8'),main=read('native/main.cpp'),runtime=read('native/decky_sidebar_runtime.h');
 const block=(text,anchor,label)=>{const at=uniqueAnchor(text,anchor,label);return cppBlock(text,text.indexOf('{',at+anchor.length),label).body;};
 const need=(text,parts,label)=>{for(const part of parts)assert(text.includes(part),label+': '+part);};
 const ordered=(text,parts,label)=>{let at=0;for(const part of parts){const next=text.indexOf(part,at);assert(next>=0,label+': '+part);at=next+part.length;}};
 const handler=block(main,'ipc_on("deckySidebar.repairConnection"','manual handler');
 // Include the complete declarator so the default argument's {} is not the body.
 const deps=block(main,'static ymcc::deckyrepair::Dependencies deckySidebarRepairDependencies(const std::function<bool()>& beforeFirstEffect={})','manual dependencies');
 need(handler,['g_deckyConnectionRepair.request(args,deckySidebarRepairDependencies())'],'shared manual owner');
 need(deps,['allowManualRepair(session)','deckySidebarEnvironment(false,{},true)','deckySidebarPortPreflight()','PowerLifecycle::Ready','ymcc::steamui::read(g_steamSettingsObserver,true)'],'manual current context');
 assert(!handler.includes('setEnabled(true)')&&!handler.includes('startOwned')&&!handler.includes('steam.uiMode'));
 const ready=block(main,'static bool deckySidebarResumeAfterIntent(','shared committed Ready');
 const delivery=extractDeckyReady(main);
 if(delivery.publicReady){
  need(ready,['g_exitRequested.load()','g_aiFanMockSession.isolated','g_deckySidebarRuntime.exitFenced()','!g_deckySidebarRuntime.snapshot().enabled'],'shared Ready refusal');
  ordered(ready,['snapshot().enabled','if(!sharedPowerLifecycleCurrent(power))return false;','delivered.compare_exchange_weak'],'public power proof before generation admission');
 }else{
 const firstReturn=ready.indexOf('return false;');assert(firstReturn>=0,'shared Ready refusal missing');
 const leading=ready.slice(0,firstReturn).replace(/\s+/g,'');assert(leading.startsWith('if(')&&leading.endsWith(')'),'shared Ready leading refusal');
 const guards=leading.slice(3,-1).split('||');for(const guard of ['!generation','generation!=currentPowerGeneration()','g_exitRequested.load()','g_aiFanMockSession.isolated','g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready','!g_deckySidebarRuntime.snapshot().enabled'])assert(guards.includes(guard),'shared Ready refusal: '+guard);
 }
 need(ready,['generation<=prior'],'Ready generation duplicate refusal');
 const delivered=uniqueAnchor(ready,'delivered.compare_exchange_weak','Ready once per generation');
 for(const call of ['requestPreparationRefresh(generation)','requestBindingRefresh(generation)','requestLaunchAdmissionRefresh()','requestConnectionCheck()','g_steamModeObserver.requestRefresh(session)','"wake-connection-check"'])assert(uniqueAnchor(ready,call,'Ready recovery')>delivered,'Ready demand preceded generation admission');
 const begin=uniqueAnchor(main,'case WM_POWER_RESUME_COMMIT:','committed wake'),end=uniqueAnchor(main,'case WM_POWER_INPUT_RECOVER:','input wake');assert(end>begin);
 const wake=main.slice(begin,end),readyGate=uniqueAnchor(wake,'if (g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready)','committed Ready gate');
 const readyFanout=cppBlock(wake,wake.indexOf('{',readyGate),'committed Ready branch').body;
 const dispatch=block(readyFanout,'const auto dispatch=[&](unsigned stage,const char* name,auto&& consume)','Ready dispatch');
 ordered(dispatch,['generation!=currentPowerGeneration()','PowerLifecycle::Ready','g_sleepPowerDispatch.pendingSuspend()','fanout.next=stage+1','consume();'],'Ready current before Decky fanout');
 if(delivery.publicReady){
  assert(!wake.includes('deckySidebarResumeAfterIntent('),'legacy window commit must not claim the first Decky wave');
  ordered(delivery.delivery,['sharedPowerLifecycleCurrent(power)','power.suspendSerial','deckySidebarResumeAfterIntent(power,"public-ready")'],'public wake before Decky fanout');
 }else need(readyFanout,['dispatch(3,"decky",[&]{deckySidebarResumeAfterIntent(generation,"committed-ready");})'],'wake shared consumer');
 for(const signature of ['bool requestLaunchAdmissionRefresh(','bool requestConnectionCheck(','bool requestPreparationRefresh(','bool requestBindingRefresh('])need(block(runtime,signature,signature),['pendingSafetyStop_.active(intent_)','stopping_','!state_.enabled'],'Runtime current owner');
 for(const [signature,counter] of [['bool requestPreparationRefresh(','lastPreparationGeneration_'],['bool requestBindingRefresh(','lastBindingGeneration_']])need(block(runtime,signature,signature),['!generation','generation<='+counter], 'Runtime generation fence');
 assert(ready.indexOf('requestBindingRefresh(generation)')<ready.indexOf('requestConnectionCheck()'),'binding invalidation must precede new connection observation');
 const automatic=block(main,'static void deckySidebarObserveAutomaticRepair(','automatic observation');
 need(automatic,['PowerLifecycle::Ready','deckySidebarServiceManagedLifecycle()'],'automatic Ready service');
 assert(automatic.indexOf('PowerLifecycle::Ready')<automatic.indexOf('deckySidebarServiceManagedLifecycle()'));
 const initialize=block(main,'static void deckySidebarInitialize(','Decky initialization');
 const reserve=block(initialize,'config.reserveConnectionStep=[]','Steam command reservation');
 ordered(reserve,['g_steamSettingsObserver.sessionSnapshot()','!g_steamSettingsObserver.sourceActive()','ymcc::steamcommand::reserve({session.pid,session.created,session.root}','Plan::Reopen:ymcc::steamcommand::Plan::Single'],'Steam session-bound action reservation');
 const step=block(initialize,'config.connectionStepReserved=[]','Runtime page-action callback');
 ordered(step,['if(!operation)return json','ymcc::steamcommand::Use operationUse(operation->ticket())','deckySidebarRepairDependencies(beforeEffect?','return beforeEffect();','session=deps.session()','deps.reserveOperation=[operation]','operation->ticket().target({target.pid,target.created,target.root})','operation->ticket().held()','mode=deps.mode(session)','state=deps.runtime()','g_deckyConnectionRepair.request(args,deps,reopen?ymcc::deckyrepair::Origin::ReopenBigPicture:ymcc::deckyrepair::Origin::FinalCleanup)','return result;'],'same reserved UI action owner');
 const selection=block(runtime,'const auto action=connectionFallback.observe(healthNow,health,[&](auto requested)','Runtime action selection');
 ordered(selection,['if(!launchAllowed(intent))return false','commandOperation=config_.reserveConnectionStep(requested);return bool(commandOperation);'],'current Runtime reservation');
 const action=block(runtime,'if((action==Action::rebuild||action==Action::reopen)&&(config_.connectionStep||config_.connectionStepReserved))','Runtime UI action branch');
 ordered(action,['const bool defer=config_.connectionStepReserved&&!process&&retainedFrontend&&context&&','const auto retire=[&]{teardownEntered=true;closeOwned(cause);};','if(!defer)retire();','if(ownedBindingCleanupError_.load()==ERROR_SUCCESS&&launchAllowed(intent))'],'owned service retirement and admission');
 const admitted=block(action,'if(ownedBindingCleanupError_.load()==ERROR_SUCCESS&&launchAllowed(intent))','admitted Runtime action');
 ordered(admitted,['const std::function<bool()> beforeEffect=defer?','if(!launchAllowed(intent)||teardownEntered)return false;','retire();return ownedBindingCleanupError_.load()==ERROR_SUCCESS&&launchAllowed(intent);','config_.connectionStepReserved(action,commandOperation,beforeEffect):config_.connectionStep(action)'],'Runtime action before effect');
 const receipt=admitted.replace(/\s+/g,'');
 ordered(receipt,[
  'constboolnoEffect=receipt.contains("ok")&&receipt["ok"]==false&&receipt.contains("mutated")&&receipt["mutated"]==false&&(!receipt.contains("uncertain")||receipt["uncertain"]==false);',
  'constboolawaitObservedHealth=action==Action::reopen&&noEffect&&receipt.value("reason",nlohmann::json{})==deckyrepair::bigpicture::alreadyHealthy;',
  'constboolidentityDeferred=noEffect&&receipt.value("awaitCurrentIdentity",nlohmann::json{})==true&&receipt.value("cleanupNotStarted",nlohmann::json{})==true&&receipt.value("reason",nlohmann::json{})=="decky-cleanup-account-unconfirmed";',
  'keptTransport=defer&&!teardownEntered&&noEffect;',
  'if(receipt.value("ok",false)&&receipt.value("mutated",false)&&!receipt.value("uncertain",false))connectionFallback.started(action,GetTickCount64());',
  'elseif(keptTransport&&identityDeferred)connectionFallback.parkUnstartedPage(action,GetTickCount64());',
  'elseif(noEffect&&!awaitObservedHealth)connectionFallback.failed(action,GetTickCount64(),identityDeferred);',
 ],'confirmed Runtime action receipt');
 const repairHeader=read('native/decky_sidebar_manual_repair.h');
 ordered(repairHeader,['if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user")','const auto session=deps.session();','if(cleanup&&deps.cleanupWorker)','if(!current()||!deps.consent(session))','deps.cleanupWorker(session,args,current)','!deps.reserveCleanup||!deps.freshMode','if(!deps.reserveCleanup(session))return refuse','deps.dispatch(session,action,admit)'],'native cleanup authorization');
 need(deps,['[beforeFirstEffect](const auto& session,const auto& args,const auto& current)','cleanupworker::dispatchShared(g_steamSettingsObserver,verified','cleanupworker::schema,beforeFirstEffect'],'parent cleanup child wiring');
 const child=block(main,'static bool deckySidebarRunContextCleanupWorker(const json& request,HANDLE cancel,const std::function<bool(const json&)>& emit,int& exit)','cleanup child entry');
 ordered(child,['deps.reserveCleanup=[&]','deckySidebarReserveContextCleanup(session','deps.freshMode=[&]','{"operation","ui.mode.get"}','deps.dispatch=[&]','repair.request(request.args,deps'],'child durable cleanup and fresh mode');
 const worker=read('native/decky_context_cleanup_worker.h');
 need(worker,['return dispatch(steam,fromUi,identity,args,state,current,recoveryMs,kind,std::move(delegation),read,post,beforeFirstEffect);'],'forward retirement callback to child grant');
 ordered(worker,['const auto answer=commandServer.serve(reply,requestCurrent,deadline);','if(beforeFirstEffect&&command.at("phase")=="enter"&&command.at("stage")==0&&answer.at("detail").at("ok")==true)','!beforeFirstEffect()||!requestCurrent()','owner.writeFrame(listenerproxy::wire(answer)'],'retire owned transport before child effect grant');
 const bridge=read('src/bridge/deckySidebar.ts'),page=read('src/views/SteamView.vue');assert(bridge.includes("'deckySidebar.repairConnection', {userInitiated:true,pid,birth,revision}"));assert(page.includes('@click="repairDeckyConnection"'));assert.equal((page.match(/await deckySidebar\.repairConnection\(/g)||[]).length,1,'only the explicit click handler requests repair');
 assert(!read('native/decky_sidebar_bootstrap.h').includes('RestartJSContext()'),'automatic bootstrap does not inherit explicit repair');
}

export async function manualRepairTests(root,out,{upgradeActionsFile=process.env.YMCC_DECKY_UPGRADE_ACTIONS,sourceOverride=null,embeddedOverride=null,emptyBridgeActionsFile=process.env.YMCC_DECKY_EMPTY_BRIDGE_ACTIONS,caseFilter=process.env.YMCC_DECKY_MANUAL_REPAIR_CASE,io=fs,runInContext=vm.runInContext}={}){
 const sourcePath=sourceOverride??path.join(root,'native/steam_live.js'),embeddedPath=embeddedOverride??path.join(root,'native/steam_live_script.h'),hash=p=>crypto.createHash('sha256').update(io.readFileSync(p)).digest('hex');
 const diagnosticsOnly=caseFilter==='repair callback diagnostics';
 const registryOnly=caseFilter==='repair registry admission';
 if(caseFilter!==undefined&&caseFilter!==null&&caseFilter!=='compiled native prior-empty bridge upgrade action'&&!diagnosticsOnly&&!registryOnly)throw Error('Unknown manual repair case');
 if(diagnosticsOnly||registryOnly)assert(!upgradeActionsFile&&!emptyBridgeActionsFile,'isolated selection cannot consume serialized native action groups');
 if(caseFilter&&!diagnosticsOnly&&!registryOnly)assert(emptyBridgeActionsFile,'selected native action case requires the actual serialized producer');
 if(!diagnosticsOnly&&!registryOnly)admitManualSources(root,io);
 const upgradeFile=upgradeActionsFile,actionBytes=upgradeFile?io.readFileSync(upgradeFile):null,native=actionBytes?JSON.parse(actionBytes.toString('utf8')):null;
 if(upgradeFile){assert.equal(native.pass,true);assert(native.upgradeActions?.length>=3);assert(native.cleanupActions?.length>=1,'compiled cleanup producer has no actual serialized final action');}
 const nativeActionSha=actionBytes?crypto.createHash('sha256').update(actionBytes).digest('hex'):null;
 io.mkdirSync(out,{recursive:true});
 io.writeFileSync(path.join(out,'manual-repair-inputs.json'),JSON.stringify({source:{path:sourcePath,sha256:hash(sourcePath)},embedded:{path:embeddedPath,sha256:hash(embeddedPath)},consumer:{path:path.join(root,'tools/decky_manual_repair_selftest.mjs'),sha256:hash(path.join(root,'tools/decky_manual_repair_selftest.mjs'))},overrides:{source:!!sourceOverride,embedded:!!embeddedOverride},scope:'Actual full source and embedded repair script in isolated VM; no native Steam action'},null,2)+'\n');
 const source=io.readFileSync(sourcePath,'utf8');const header=io.readFileSync(embeddedPath,'utf8').split('inline constexpr const char kUiModeScript[] =')[0];
 const embedded=[...header.matchAll(/R"YMSTEAM\(([\s\S]*?)\)YMSTEAM"/g)].map(m=>m[1]).join('');const cases=[],traces=[];
 const invoke=async(script,options={})=>{
  const old='a'.repeat(64),fresh='b'.repeat(64),request={operation:'decky.repairConnection',userInitiated:true,expectedFrontendBuild:old,installedFrontendBuild:fresh,loaderVersion:'v3.3.0',...options.request};
  const loader={plugins:[{name:'ymcc-sidebar'}],deckyState:{publicState:()=>({versionInfo:{current:options.version??'v3.3.0'}})}};
  const window={DeckyPluginLoader:loader,__YMCC_DECKY_FRONTEND__:{active:!options.inactive,buildId:options.build??old},deckyHasLoaded:true};window.top=options.frame?{}:window;
  if(options.emptyFrontend){window.__YMCC_DECKY_FRONTEND__=undefined;loader.plugins=[];request.expectedFrontendBuild=null;window.__YMCC_DECKY_MIRROR__={frontendBuildId:fresh,runId:'d'.repeat(32),token:'e'.repeat(64),endpoint:'ws://127.0.0.1:12345/mirror'};if(options.noBridge)delete window.__YMCC_DECKY_MIRROR__;if(options.wrongBridge)window.__YMCC_DECKY_MIRROR__.frontendBuildId=old;if(options.bridgeBuild)window.__YMCC_DECKY_MIRROR__.frontendBuildId=options.bridgeBuild;}
  let actualAccount=options.account??9;const differentAccount=actualAccount===9?10:9;window.__YMCC_DECKY_ACCOUNT_WATCH__={read:account=>account===actualAccount};
  if(options.cleanup){request.expectedAccount=options.request?.expectedAccount??9;delete request.userInitiated;request.operation='decky.cleanupConnection';request.cleanupAuthorized=options.request?.cleanupAuthorized??true;request.expectedModeEpoch=options.epoch??options.request?.expectedModeEpoch??'current-document:1';window.__ymccSteamUiModeWatch={id:'current-document',entries:1,mode:'bigpicture',...options.modeWitness};}
  if(options.automatic){request.operation='decky.recoverConnection';delete request.userInitiated;request.automatic=options.noAutomatic?false:true;}if(options.noPlugin)loader.plugins=[];if(options.pending)window.__ymccDeckyManualRepair=true;
   if(options.registry)loader.plugins=options.registry(loader.plugins);
   let refreshes=0,modeReads=0,restartAttempts=0,timerErrors=0;const timers=[];const SteamClient={UI:{GetUIMode:async()=>{modeReads++;if(options.accountModeRace)actualAccount=differentAccount;if(options.race)window.__YMCC_DECKY_FRONTEND__={active:true,buildId:fresh};if(options.registryModeRace)loader.plugins=options.registryModeRace(loader.plugins);return options.desktop?7:4}},Browser:{RestartJSContext(){restartAttempts++;refreshes++;if(options.restartThrows)throw Error("fixture-restart-result-uncertain");}}};
  const diagnosticLogs=[];const diagnosticConsole={info(tag,payload){if(options.loggingThrows)throw Error('fixture-diagnostic-sink-failed');if(tag==='[YMCC:DeckyRepair]')diagnosticLogs.push({tag,...JSON.parse(payload)});}};
  const context=vm.createContext({console:diagnosticConsole,window,location:{origin:options.foreign?'https://example.invalid':'https://steamloopback.host'},SteamClient,Date,Math,Promise,Number,Array,Object,setTimeout(fn,ms){if(ms===0){timers.push(fn);return 1}return setTimeout(fn,ms)},clearTimeout});
  const result=await runInContext(script+'('+JSON.stringify(request)+')',context);const before=refreshes;if(options.bridgeRace&&window.__YMCC_DECKY_MIRROR__)window.__YMCC_DECKY_MIRROR__.frontendBuildId=(window.__YMCC_DECKY_MIRROR__.frontendBuildId==='c'.repeat(64)?'d':'c').repeat(64);if(options.timerRace)window.__YMCC_DECKY_FRONTEND__={active:true,buildId:fresh};if(options.accountRace)actualAccount=differentAccount;if(options.epochRace)window.__ymccSteamUiModeWatch.entries++;if(options.desktopRace)window.__ymccSteamUiModeWatch.mode='desktop';if(options.markerRace)window.__ymccDeckyManualRepair={foreignOwner:true};
  if(options.timerGuardThrows)loader.deckyState.publicState=()=>{throw Error('fixture-guard-read-before-restart');};
   if(options.registryTimerRace)loader.plugins=options.registryTimerRace(loader.plugins);
  const runTimer=fn=>{try{fn();}catch(error){if(!options.restartThrows)throw error;timerErrors++;}};
  const originalTimers=timers.slice();for(const timer of originalTimers)runTimer(timer);
  if(options.duplicateCallback)for(const timer of originalTimers)runTimer(timer);
  let retry;
  if(options.freshManualRetry){
   if(options.timerGuardThrows)loader.deckyState.publicState=()=>({versionInfo:{current:options.version??'v3.3.0'}});
   const freshRequest={operation:'decky.repairConnection',userInitiated:true,expectedFrontendBuild:window.__YMCC_DECKY_FRONTEND__?.buildId,installedFrontendBuild:fresh,loaderVersion:options.version??'v3.3.0'};
   const firstTimerCount=timers.length,firstRefreshes=refreshes;
   const next=await runInContext(script+'('+JSON.stringify(freshRequest)+')',context);
   const freshMarker=window.__ymccDeckyManualRepair;
   for(const timer of originalTimers)runTimer(timer); // Late old callback must not clear the new operation's marker.
   const markerPreserved=window.__ymccDeckyManualRepair===freshMarker;
   for(const timer of timers.slice(firstTimerCount))runTimer(timer);
   retry={result:JSON.parse(JSON.stringify(next)),firstRefreshes,markerPreserved};
  }
  const observed={result:JSON.parse(JSON.stringify(result)),refreshes,before,modeReads,timerCount:timers.length,restartAttempts,timerErrors,retry,diagnosticLogs};
  traces.push({scriptSha256:crypto.createHash('sha256').update(script).digest('hex'),scenario:Object.keys(options).filter(k=>options[k]===true),...observed});
  io.writeFileSync(path.join(out,'manual-repair-traces.json'),JSON.stringify({realSteamActions:0,settingsWrites:0,traces},null,2)+'\n');
  return{...observed,window};
 };
  if(!caseFilter||registryOnly){
   const front=rows=>[null,...rows],back=rows=>[...rows,null],hole=([own])=>[own,,{name:'unrelated'}];
   const expected=[],actual=[];
   for(const [kind,script]of Object.entries({source,embedded}))for(const [label,options,want]of [
    ['manual healthy',{},'restart'],
    ['cleanup healthy',{cleanup:true},'restart'],
    ['cleanup owned empty',{cleanup:true,emptyFrontend:true},'restart'],
    ['manual leading null',{registry:front},'refuse'],
    ['cleanup trailing null',{cleanup:true,registry:back},'refuse'],
    ['manual sparse registry',{registry:hole},'refuse'],
    ['cleanup sparse is not empty',{cleanup:true,emptyFrontend:true,registry:()=>Array(1)},'refuse'],
    ['cleanup registry changes during mode read',{cleanup:true,registryModeRace:back},'mode-refuse'],
    ['manual registry changes after scheduling',{registryTimerRace:hole},'cancel'],
    ['cleanup registry changes after scheduling',{cleanup:true,registryTimerRace:back},'cancel'],
    ['automatic malformed registry still denied',{automatic:true,registry:back},'auto-refuse'],
   ]){
    const name=kind+': repair registry '+label,r=await invoke(script,options);
    const scheduled=want==='restart'||want==='cancel';
    actual.push({name,ok:r.result.ok,mutated:r.result.mutated,reason:r.result.reason??null,reads:r.modeReads,timers:r.timerCount,entered:r.restartAttempts,before:r.before,marker:!!r.window.__ymccDeckyManualRepair,busy:!!r.window.__ymccSteamLiveBusy,phases:r.diagnosticLogs.map(row=>row.phase)});
    expected.push({name,ok:scheduled,mutated:scheduled,reason:scheduled?null:want==='auto-refuse'?'steam-ui-refresh-requires-user':'decky-repair-context-changed',reads:scheduled||want==='mode-refuse'?1:0,timers:scheduled?1:0,entered:want==='restart'?1:0,before:0,marker:want==='restart',busy:false,phases:scheduled?['scheduled',want==='restart'?'restart-call-entered':'cancelled-before-call']:[]});
    cases.push(name);
   }
   io.writeFileSync(path.join(out,'manual-repair-registry.json'),JSON.stringify({sourceAndEmbedded:actual,realSteamActions:0},null,2)+'\n',{flag:'wx'});
   assert.deepEqual(actual,expected,'malformed registries must refuse or cancel before RestartJSContext');
  }
  if(diagnosticsOnly){
  for(const [kind,script]of Object.entries({source,embedded})){
   for(const [label,options,phases,count]of [
    ['manual',{},['scheduled','restart-call-entered'],1],
    ['cleanup',{cleanup:true},['scheduled','restart-call-entered'],1],
    ['cancelled cleanup',{cleanup:true,epochRace:true},['scheduled','cancelled-before-call'],0],
    ['preflight refused',{request:{userInitiated:false}},[],0],
    ['restart throws',{restartThrows:true},['scheduled','restart-call-entered'],1],
    ['logger throws',{loggingThrows:true},[],1],
    ['duplicate callback',{duplicateCallback:true},['scheduled','restart-call-entered'],1],
    ['retired reservation',{markerRace:true},['scheduled'],0]]){
    const result=await invoke(script,options);assert.equal(result.restartAttempts,count,kind+': '+label+' original restart count');
    assert.deepEqual(result.diagnosticLogs.map(row=>row.phase),phases,kind+': '+label+' actual restart callback facts');
    for(const row of result.diagnosticLogs){assert.deepEqual(Object.keys(row).sort(),['origin','phase','schema','tag']);assert.equal(row.schema,1);assert.equal(row.origin,options.cleanup?'final-cleanup':'manual');}
    assert.equal(result.before,0);if(options.restartThrows){assert.equal(result.timerErrors,1);assert(result.window.__ymccDeckyManualRepair,'uncertain call must retain the spent marker');}
    cases.push(kind+': repair callback diagnostic '+label);
   }
  }
 }
 if(!caseFilter)for(const [kind,script] of Object.entries({source,embedded})){
  for(const [label,options] of Object.entries({'empty owned frontend':{emptyFrontend:true},'automatic empty owned frontend':{automatic:true,emptyFrontend:true},'empty without own bridge':{emptyFrontend:true,noBridge:true},'empty with wrong installed build':{emptyFrontend:true,wrongBridge:true},'pinned prior empty bridge':{emptyFrontend:true,wrongBridge:true,request:{expectedEmptyBridgeBuild:'a'.repeat(64)}},'wrong prior empty bridge pin':{emptyFrontend:true,wrongBridge:true,request:{expectedEmptyBridgeBuild:'c'.repeat(64)}},'malformed prior empty bridge pin':{emptyFrontend:true,request:{expectedEmptyBridgeBuild:'bad'}},'empty wrong official version':{emptyFrontend:true,version:'v3.2.10'},'empty module races mode read':{emptyFrontend:true,race:true},'explicit repair':{},'approved automatic repair':{automatic:true},'automatic without origin':{automatic:true,noAutomatic:true},'no consent':{request:{userInitiated:false}},'extra arguments':{request:{force:true}},'bad old build':{request:{expectedFrontendBuild:'x'}},'different actual module':{build:'c'.repeat(64)},'inactive module':{inactive:true},'foreign page':{foreign:true},'subframe':{frame:true},'no plugin registration':{noPlugin:true},'wrong official version':{version:'v3.2.10'},'desktop':{desktop:true},'module replaced during async mode read':{race:true},'already scheduled':{pending:true}})){
   const r=await invoke(script,options);const allowed=label==='explicit repair'||label==='empty owned frontend'||label==='pinned prior empty bridge';assert.equal(r.result.ok,allowed,kind+': '+label);assert.equal(r.refreshes,allowed?1:0,kind+': '+label+' actual refresh count');assert.equal(r.before,0,'result only acknowledges a scheduled request');cases.push(kind+': '+label);
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
 if(emptyBridgeActionsFile){
  const actual=JSON.parse(io.readFileSync(emptyBridgeActionsFile,'utf8'));assert.equal(actual.pass,true);assert.equal(actual.actions.length,1);
  for(const [kind,script]of Object.entries({source,embedded}))for(const action of actual.actions){
   const epoch=/^(.+):([1-9]\d*)$/.exec(action.expectedModeEpoch);assert(epoch);
   const options={emptyFrontend:true,bridgeBuild:action.expectedEmptyBridgeBuild,cleanup:true,request:action,version:action.loaderVersion,account:action.expectedAccount,modeWitness:{id:epoch[1],entries:Number(epoch[2]),mode:'bigpicture'}};
   const accepted=await invoke(script,options);assert(accepted.result.ok&&accepted.refreshes===1);cases.push(kind+': compiled native prior-empty bridge upgrade action executes once');
   for(const race of [{bridgeRace:true},{epochRace:true},{accountRace:true},{desktopRace:true},{timerRace:true}]){const denied=await invoke(script,{...options,...race});assert.equal(denied.refreshes,0);cases.push(kind+': compiled empty bridge retains final '+Object.keys(race)[0]);}
  }
 }
 // When supplied, consume real serialized actions produced by compiled native
 // target selection, not separately invented JS requests. Same canonical test.
 if(upgradeFile&&!caseFilter){
  const trace=[];
  for(const row of native.cleanupActions){for(const [kind,script]of Object.entries({source,embedded})){
   const action=row.action;assert(Number.isSafeInteger(action.expectedAccount)&&action.expectedAccount>0,'native cleanup account witness');const epoch=/^(.+):([1-9]\d*)$/.exec(action.expectedModeEpoch);assert(epoch,'native cleanup mode witness');const context={account:action.expectedAccount,modeWitness:{id:epoch[1],entries:Number(epoch[2]),mode:'bigpicture'}};
   const result=await invoke(script,{cleanup:true,request:action,version:action.loaderVersion,build:action.expectedFrontendBuild,...context});
   assert.equal(result.result.ok,true);assert.equal(result.refreshes,1);assert.equal(result.before,0);
   for(const race of [{epochRace:true},{desktopRace:true},{timerRace:true},{accountRace:true},{accountModeRace:true}]){const denied=await invoke(script,{cleanup:true,request:action,version:action.loaderVersion,build:action.expectedFrontendBuild,...context,...race});assert.equal(denied.refreshes,0);}
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
  io.writeFileSync(path.join(out,'native-to-js-upgrade-scenarios.json'),JSON.stringify({pass:true,nativeActionFile:upgradeFile,scope:'Actual compiled native target -> production source and embedded JS; memory Steam API, no real Steam actions',trace},null,2));
 }
 if(!caseFilter)cases.push('Actual native handler uses shared orchestrator with resource/port/consent checks and no retry','Actual Vue consumer requests repair only from explicit click with current identity');
 io.writeFileSync(path.join(out,'manual-repair.json'),JSON.stringify({passed:true,caseFilter:caseFilter??null,emptyBridgeSerializedReference:emptyBridgeActionsFile?{path:emptyBridgeActionsFile,sha256:hash(emptyBridgeActionsFile),producerExecutedThisRun:false}:null,cases:cases.length,results:cases,scope:'Production full/embedded script in VM only; native orchestrator and actual Vue execution are separate. Prior serialized native actions are references, not current native execution.',realSteamActions:0,settingsWrites:0,nativeExecutedThisRun:false,serializedNativeActionsReference:upgradeFile?{path:upgradeFile,sha256:nativeActionSha,producerExecutedThisRun:false}:null},null,2));return cases.length;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});console.log('PASS manual repair cases='+await manualRepairTests(root,out));}
