// Exact production host/relay/adapters. Only the external IPC/device/store IO
// boundaries are inert; business save/load/lock/normalization stay original.
import fs from 'node:fs';import path from 'node:path';import {root,load,functions,fixture,gamePolicyKey,base,performanceModelModule,performanceControlsModule,detectedGameName} from './decky_game_memory_store.mjs';
const keyModule=load(fs.readFileSync(path.join(root,'src/bridge/gamePolicyHysteresis.ts'),'utf8'));
const steamModule=load(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'));
const relayModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':steamModule});
const virtualPolicyModule=load(fs.readFileSync(path.join(root,'src/bridge/virtualTargetPolicy.ts'),'utf8'));
const virtualStatusModule=load(fs.readFileSync(path.join(root,'src/bridge/virtualOutputStatus.ts'),'utf8'),{'./virtualTargetPolicy':virtualPolicyModule});
const virtualProtectionModule=load(fs.readFileSync(path.join(root,'src/bridge/virtualStartProtectionView.ts'),'utf8'));
const inputPersonaModule=load('const sessionId="fixture-session";\n'+functions('src/bridge/gameInputOverride.ts',['effectiveInputPersona'])+'\nmodule.exports={effectiveInputPersona};');
const gameModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyGameActions.ts'),'utf8'),{'./gamePolicyHysteresis':keyModule});
const profileModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyProfileActions.ts'),'utf8'),{'./deckyGameActions':gameModule,'./gamePolicyHysteresis':keyModule});
const trainerModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyTrainerActions.ts'),'utf8'),{'./deckyGameActions':gameModule});
const speedModule=load(fs.readFileSync(path.join(root,'src/bridge/deckySpeedActions.ts'),'utf8'),{'./deckyGameActions':gameModule});
const frameModel=load(fs.readFileSync(path.join(root,'src/bridge/frameRateModel.ts'),'utf8'));
const screenModule=load(fs.readFileSync(path.join(root,'src/bridge/screenTouchpads.ts'),'utf8'),{'./ipc':{isNativeRuntime:false}});
const frameModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyFrameActions.ts'),'utf8'),{'./frameRateModel':frameModel,'./deckyGameActions':gameModule});
const touchModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyTouchpadActions.ts'),'utf8'),{'./screenTouchpads':screenModule});
const inputOptions=load(fs.readFileSync(path.join(root,'src/bridge/deckyInputOptions.ts'),'utf8'));
const presetModel=load(fs.readFileSync(path.join(root,'src/bridge/gyroPresetModel.ts'),'utf8'));
const tdpPresentation=load('const tdpExecutionCapability=null;\n'+functions('src/bridge/yeman.ts',['formatTdpTarget'])+'\nmodule.exports={formatTdpTarget};');
const globalModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyGlobalActions.ts'),'utf8'),{'./deckyInputOptions':inputOptions,'./gyroPresetModel':presetModel,'./yeman':tdpPresentation});
const fanModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyFanActions.ts'),'utf8'));
const monitorModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyMonitorActions.ts'),'utf8'));
const monitorModel=load(fs.readFileSync(path.join(root,'src/bridge/monitorHudModel.ts'),'utf8'),{
 './monitorNativeDemand':load(fs.readFileSync(path.join(root,'src/bridge/monitorNativeDemand.ts'),'utf8')),
 './monitorSnapshotSignals':load(fs.readFileSync(path.join(root,'src/bridge/monitorSnapshotSignals.ts'),'utf8'),{'./monitorFrameTime':load(fs.readFileSync(path.join(root,'src/bridge/monitorFrameTime.ts'),'utf8')),'./monitorCoreTiles':load(fs.readFileSync(path.join(root,'src/bridge/monitorCoreTiles.ts'),'utf8'))}),
 './monitorFrameTime':load(fs.readFileSync(path.join(root,'src/bridge/monitorFrameTime.ts'),'utf8')),
 './monitorHudFreshness':load(fs.readFileSync(path.join(root,'src/bridge/monitorHudFreshness.ts'),'utf8'),{'./monitorFrameTime':load(fs.readFileSync(path.join(root,'src/bridge/monitorFrameTime.ts'),'utf8'))}),
 './monitorCoreTiles':load(fs.readFileSync(path.join(root,'src/bridge/monitorCoreTiles.ts'),'utf8')),
 './monitorHudFpsVisibility':load(fs.readFileSync(path.join(root,'src/bridge/monitorHudFpsVisibility.ts'),'utf8')),
});
export async function createMirrorHostFixture({initial={version:1,entries:{},rtss:{}},warm=true,invoke:externalInvoke,fanOwner=null,losslessOwner=null,fxSoundNative=null,readHooks={},hostSource=null,trainerOwner=null,trainerRecovery=null,policyRefresh=null,policyReadiness=null,clock={now:0}}={}){
 const memory=fixture(initial,{generation:()=>state.power.generation,game:()=>state.game});if(warm)await memory.store.loadGameCustomConfig();
 const counts={inputReads:0,frameReads:0,touchpadReads:0,monitorReads:0,power:0,schedule:0,capabilities:0,saves:0,joyxoff:0,fanSaves:0,fanSettingsSnapshots:0,fanPresetReads:0,watchers:0,attaches:0,speedApply:0,speedClear:0,frameApplies:0,globalFrameSaves:0,touchpadSaves:0,globalPowerApplies:0,inputSaves:0,monitorSaves:0,monitorApplies:0};
 const state={gameCurrent:true,gamepadState:null,virtualPolicy:null,monitor:monitorModel.normalizeMonitorOutput({output:"ymcc",enabled:true}),monitorReadError:false,monitorSaveError:false,monitorApplyResult:null,monitorApplyHook:null,inputOverrideLocked:false,globalFrames:frameModel.frameRatePair({}),input:{revision:1,applyStatus:'saved-pending',gyroMotion:{virtualPadLink:true},outputTarget:{persona:'steamdeck',buttonMappingEnabled:true}},touchpads:{...screenModule.screenTouchpadDefaults('steamdeck'),ok:true,available:true,visible:false,persona:'steamdeck',steamDeckAvailable:true,ps5Available:false},touchError:false,frameApplyResult:true,speed:null,speedResult:null,gyroDefaults:{padPersona:'steamdeck',virtualPad:true,enabled:false,preset:'fps',source:'inert-original-global-gyro'},powerSource:'ac',lossless:{settingsPath:'memory-only-LS.xml',xml:'memory-LS-xml',profile:null,enabled:false,running:false,configuredEnabled:false,controlAvailable:false,reason:'当前 EXE 不在LosslessScaling 插帧生成的列表中'},game:{pid:101,name:'game.exe',title:'Fixture Game (Vulkan)',path:'C:/Games/Game.exe',processCreated:'123456789',ts:0},power:{generation:1,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:true},caps:{detected:true,heterogeneous:true,smtAvailable:true,efficiencyClasses:[0,1],logical:8,physical:4,smtLogical:8,smtPhysical:4,source:'inert-fixture'},fan:{enabled:false,preset:'balanced'},schedule:{version:2,configured:true,enabled:true,active:{ac:'balanced',dc:'eco'},profiles:{ac:{},dc:{}}}};
 const modes=['eco','balanced','medium','performance','elite','extreme'];for(const side of ['ac','dc'])for(const [i,mode] of modes.entries())state.schedule.profiles[side][mode]={...base,tdpMax:side==='ac'?25+i*3:8+i*2,fpsTarget:side==='ac'?60:30};
 state.trainer=null;state.trainerLaunchHook=null;counts.trainerReads=0;counts.trainerLaunches=0;
 state.fxSound={known:true,installed:true,running:false,enabled:false,selected:true,preset:'移动端通用音频优化',output:'fixture physical speakers',reason:''};state.fxSoundSetHook=null;counts.fxSoundReads=0;counts.fxSoundWrites=0;
 const fxTimers=new Map();let fxTimerId=0;
 const fxClock={setTimeout:(fn,delay)=>{const id=++fxTimerId;fxTimers.set(id,{fn,delay});return id;},clearTimeout:id=>fxTimers.delete(id)};
 const diagnostics=[];
 const events=new Map(),gameListeners=new Set(),readinessListeners=new Set(),scheduleListeners=new Set(),fanListeners=new Set(),displayListeners=new Set(),replies=[];let fanDisplay=null;
 // Optional bridge consumes the actual shared core API; it never elects or
 // stores another target. The fallback below is the inert boolean test seam.
 if(policyReadiness&&(typeof policyReadiness.isGameStatusCurrent!=='function'||typeof policyReadiness.subscribeGameTargetReadiness!=='function'))throw Error('Invalid shared readiness bridge');
 const subscribeReadiness=callback=>{
  readinessListeners.add(callback);let stop;
  try{if(policyReadiness)stop=policyReadiness.subscribeGameTargetReadiness(callback);else{try{callback(state.gameCurrent);}catch{/* Match the observer-isolation contract. */}}}
  catch(error){readinessListeners.delete(callback);throw error;}
  return()=>{readinessListeners.delete(callback);stop?.();};
 };
 const setGameReadiness=current=>{
  if(policyReadiness)throw Error('Drive readiness through the actual core bridge');
  if(typeof current!=='boolean')throw Error('Expected a boolean readiness sample');
  if(state.gameCurrent===current)return;
  state.gameCurrent=current;
  for(const callback of [...readinessListeners]){try{callback(current);}catch{/* Independent observer. */}}
 };
 let handle=null;
 const controllerTimers=new Map();let controllerTimerId=0;
 const controllerClock={performance:{now:()=>clock.now},setTimeout:(fn,delay)=>{const id=++controllerTimerId;controllerTimers.set(id,{fn,at:clock.now+delay});return id;},clearTimeout:id=>controllerTimers.delete(id)};
 const on=(event,callback)=>{let set=events.get(event);if(!set)events.set(event,set=new Set());set.add(callback);return()=>{set.delete(callback);if(!set.size)events.delete(event);};};
 const emit=(event,message)=>{for(const callback of [...events.get(event)??[]])callback(message);};
 const invoke=async(command,args,options)=>{if(command==='fxsound.getState'){counts.fxSoundReads++;if(fxSoundNative)return fxSoundNative.read();await readHooks.fxSound?.();return structuredClone(state.fxSound);}if(command==='fxsound.setEnabled'){counts.fxSoundWrites++;if(fxSoundNative)return fxSoundNative.set(args.enabled);if(state.fxSoundSetHook)return state.fxSoundSetHook(args.enabled);state.fxSound={...state.fxSound,running:args.enabled,enabled:args.enabled};return {ok:true,copied:false,reused:false,repaired:!args.enabled,reason:args.enabled?'音质优化已开启':'音质优化已关闭',state:structuredClone(state.fxSound)};}if(command==='deckySidebar.mirrorDiagnostic'){diagnostics.push(structuredClone(args));return true;}if(command==='deckySidebar.mirrorAttach')counts.attaches++;if(command==='deckySidebar.mirrorReply')replies.push(structuredClone(args));if(externalInvoke)return externalInvoke(command,args,options);if(command==='input.shortcutRuntime.clear'){counts.shortcutClears=(counts.shortcutClears||0)+1;if(state.shortcutClearError)throw Error('inert shortcut owner unavailable');return state.shortcutClearResult??true;}if(command==='deckySidebar.mirrorAttach')return {connected:false};if(command==='deckySidebar.mirrorReply')return true;throw new Error('Unexpected production IPC: '+command);};
 const losslessTimers=new Map();let losslessTimerId=0;
 const losslessState=load(fs.readFileSync(path.join(root,'src/bridge/deckyLosslessState.ts'),'utf8'),{}, {setTimeout:(action,delay)=>{const id=++losslessTimerId;losslessTimers.set(id,{action,delay});return id;},clearTimeout:id=>losslessTimers.delete(id)});
 const performanceActions=load(fs.readFileSync(path.join(root,'src/bridge/deckyPerformanceActions.ts'),'utf8'),{'./performanceControls':performanceControlsModule,'../../decky-plugin/src/performanceModel':performanceModelModule});
 const readPerformance=async()=>{
   await readHooks.performance?.();if(state.performanceReadError)throw Error('injected performance read failure');
   const config=memory.store.getGameCustomConfig(),exe=state.game?gamePolicyKey(state.game):'',entry=config.entries[exe],scope=entry&&entry.enabled!==false?'dedicated':'global';
   const controls=performanceControlsModule.normalizeContinuousControls(scope==='dedicated'?entry.controls??{ac:{tdp:entry.ac.tdpMax},dc:{tdp:entry.dc.tdpMax}}:state.schedule.controls);
   const bank=state.powerSource?controls[state.powerSource]:null,hyperThreadMode=entry?.hyperThreadPolicy??'default';
   return {frameRates:scope==='dedicated'?frameModel.dedicatedFrameRatePair(config.rtss[exe],state.globalFrames):structuredClone(state.globalFrames),identity:{scope,side:state.powerSource,exe,pid:state.game?.pid??0,processCreated:String(state.game?.processCreated??''),generation:state.power.generation,revision:JSON.stringify([bank,entry?.enabled,hyperThreadMode])},controls,side:state.powerSource,bank,hardwareMinimum:1,hardwareMaximum:150,hybrid:state.caps?.heterogeneous===true,smtAvailable:!!state.game&&state.caps?.smtAvailable===true,hyperThreadMode,configured:true};
 };
 const updatePerformance=async(expected,patch)=>{
   const current=await readPerformance();if(JSON.stringify(current.identity)!==JSON.stringify(expected))throw Error('stale performance source');
   await readHooks.performanceUpdate?.(expected,patch);const controls=current.controls;controls[expected.side]=performanceControlsModule.patchContinuousBank(controls[expected.side],patch,expected.side);
   if(expected.scope==='dedicated'){const cfg=await memory.store.loadGameCustomConfig();const entry=cfg.entries[expected.exe];await memory.store.saveGameCustomConfig({...cfg,entries:{...cfg.entries,[expected.exe]:{...entry,controls,...(patch.hyperThreadMode?{hyperThreadPolicy:patch.hyperThreadMode,hyperThreadPolicyEnabled:patch.hyperThreadMode!=='default'}:{})}}});}
   else{state.schedule.controls=controls;for(const cb of [...scheduleListeners])cb(structuredClone(state.schedule));}
   counts.performanceUpdates=(counts.performanceUpdates??0)+1;return readPerformance();
 };
 const modules={
 './fxSound':{fxSound:{read:async()=>({known:false,installed:false,running:false,enabled:false,selected:false,preset:'',output:'',reason:'memory-only audio boundary'}),retain:()=>()=>{},setEnabled:async()=>{throw Error('No audio mutation in input intent audit');}}},
 './virtualStartProtectionView':virtualProtectionModule,'./virtualOutputStatus':virtualStatusModule,
 './deckyPerformanceActions':performanceActions,
 './deckyLosslessState':losslessState,
 './deckyUpdaterHost':{startDeckyUpdaterHost:()=>({ready(){},stop(){}})},
 './deckyProfileActions':profileModule,'./virtualTargetPolicyState':{currentVirtualTargetPolicy:()=>state.virtualPolicy},'./virtualTargetPolicy':load(fs.readFileSync(path.join(root,'src/bridge/virtualTargetPolicy.ts'),'utf8')),
 './virtualStartProtectionView':load(fs.readFileSync(path.join(root,'src/bridge/virtualStartProtectionView.ts'),'utf8')),
 './virtualOutputStatus':load(fs.readFileSync(path.join(root,'src/bridge/virtualOutputStatus.ts'),'utf8'),{'./virtualTargetPolicy':load(fs.readFileSync(path.join(root,'src/bridge/virtualTargetPolicy.ts'),'utf8'))}),
 './deckyMirrorExtensions':load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorExtensions.ts'),'utf8')),
 './deckyInputOptions':inputOptions,'./deckyGlobalActions':globalModule,'./gameInputOverride':{getGameInputOverrideState:()=>({locked:state.inputOverrideLocked}),effectiveInputPersona:inputPersonaModule.effectiveInputPersona},
 './deckySpeedActions':speedModule,
 './deckyTrainerActions':trainerModule,
 './gameTrainerRecovery':trainerRecovery??{refreshGameTrainerRecovery:()=>{counts.trainerRecoveryChecks=(counts.trainerRecoveryChecks||0)+1;}},
 './gameTrainer':trainerOwner??{getReadyGameTrainer:(name,processPath)=>{counts.trainerReads++;const t=state.trainer;return t&&t.targetName===name&&t.targetPath.replaceAll('\\','/').toLowerCase()===processPath.replaceAll('\\','/').toLowerCase()?structuredClone(t.installed):null;},
  subscribeGameTrainerState:callback=>on('test.trainerChanged',callback),
  launchReadyGameTrainer:async(name,processPath,options)=>{if(!await options.isTargetCurrent())throw Error('Inert target changed');counts.trainerLaunches++;if(state.trainerLaunchHook)return state.trainerLaunchHook(name,processPath,options);return {action:'opened',trainer:structuredClone(state.trainer.installed)};}},
 './deckyMonitorActions':monitorModule,
 './monitorOutput':{readMonitorOutputPreferences:async()=>{counts.monitorReads++;await readHooks.monitor?.();if(state.monitorReadError)throw Error('Inert monitor read failure');return structuredClone(state.monitor);},
  saveMonitorOutputPreferences:async prefs=>{if(state.monitorSaveError)throw Error('Inert monitor save failure');counts.monitorSaves++;state.monitor=structuredClone(prefs);emit('test.monitorChanged',state.monitor);},
  onMonitorOutputPreferencesChanged:callback=>on('test.monitorChanged',callback),
  monitorOutputCoordinator:{apply:async prefs=>{counts.monitorApplies++;if(state.monitorApplyHook)return state.monitorApplyHook(prefs);return state.monitorApplyResult??{ok:true,applied:prefs.enabled?prefs.output:'off',phase:'done',reason:'Inert original display owner ACK'};}}},
 './deckyFrameActions':frameModule,'./deckyTouchpadActions':touchModule,
 './settingsRepository':{saveSettingsSection:async(section,patch)=>{if(section!=='rtss'||Object.keys(patch).length!==1||Object.keys(patch.monitorOutput??{}).length!==1||typeof patch.monitorOutput.enabled!=='boolean')throw Error('Unexpected inert settings patch');if(state.monitorSaveError)throw Error('Inert monitor save failure');counts.monitorSaves++;state.monitor={...structuredClone(state.monitor),enabled:patch.monitorOutput.enabled};},readSettingsSection:async()=>{counts.inputReads++;await readHooks.input?.();return structuredClone(state.input);},onInputSettingsChanged:callback=>on('test.inputChanged',callback),compareAndSwapInputSettings:async(revision,patch,isCurrent)=>{if(isCurrent&&!isCurrent()||state.input.revision!==revision)return {ok:false,value:structuredClone(state.input)};counts.inputSaves++;for(const key of Object.keys(patch))state.input[key]={...state.input[key],...structuredClone(patch[key])};state.input.revision++;emit('test.inputChanged',state.input);await readHooks.inputCommit?.();return {ok:true,value:structuredClone(state.input)};}},
 './screenTouchpads':{...screenModule,screenTouchpadsGet:async persona=>{counts.touchpadReads++;await readHooks.touchpads?.();return {...structuredClone(state.touchpads),persona};},screenTouchpadsSet:async(patch,persona)=>{counts.touchpadSaves++;if(state.touchError)return {...state.touchpads,ok:false};state.touchpads={...state.touchpads,...structuredClone(patch),persona};emit('screenTouchpads.updated',state.touchpads);return structuredClone(state.touchpads);}},
 './frameRateLimits':{...frameModel,loadGlobalFrameRates:async()=>{counts.frameReads++;await readHooks.frames?.();return structuredClone(state.globalFrames);},saveGlobalFrameRate:async(side,value)=>{counts.globalFrameSaves++;state.globalFrames[side]=frameModel.frameRateSetting(value,state.globalFrames[side]);emit('test.frameRates',state.globalFrames);await readHooks.frameCommit?.();return structuredClone(state.globalFrames);},applyIndependentFrameRates:async current=>{if(!current())return false;counts.frameApplies++;return state.frameApplyResult;},onFrameRatesChanged:callback=>on('test.frameRates',callback)},
 './speedhack':{SPEED_PRESETS:[0.5,0.8,1,1.2,1.5,2,4,8],isMinecraftTarget:game=>/minecraft|java/i.test(game.name),getGameSpeedState:()=>state.speed?structuredClone(state.speed):null,onGameSpeedStateChanged:()=>()=>{},applyGameSpeed:async(pid,factor,target)=>{counts.speedApply++;if(state.speedResult)return state.speedResult;state.speed={pid,processCreated:target.processCreated,factor,enabled:true};return {ok:true,msgs:[]};},clearGameSpeed:async()=>{counts.speedClear++;if(state.speedResult)return state.speedResult;state.speed=null;return {ok:true,msgs:[]};}},
 './deckyGyroDefaults':{readGameMirrorGyroDefaults:async()=>structuredClone(state.gyroDefaults)},
 './powerSource':{powerSourceMode:{get value(){return state.powerSource;}},refreshPowerSourceSnapshot:async()=>{await readHooks.powerSource?.();}},
 'vue':{watch:source=>{source();counts.watchers++;return()=>counts.watchers--;}},'./ipc':{on,invoke,getLastGamepadState:()=>state.gamepadState},'./api':{powerLifecycle:{get:async()=>{counts.power++;await readHooks.power?.();return structuredClone(state.power);}}},
 './gamePolicyTarget':{getPolicyGame:()=>state.game,refreshPolicyGameStrict:async()=>{counts.gameReconciles=(counts.gameReconciles||0)+1;if(policyRefresh)return policyRefresh();return state.game;},gamePolicyKey,subscribePolicyGameStatus:callback=>{gameListeners.add(callback);callback(state.game);return()=>gameListeners.delete(callback);}},
 './gamedetect':{detectedGameName,isGameStatusCurrent:()=>policyReadiness?policyReadiness.isGameStatusCurrent():state.gameCurrent,subscribeGameTargetReadiness:subscribeReadiness},'./quickActionLock':memory.lock,'./gameproc':{closeJoyxoffIfRunning:async()=>{counts.joyxoff++;return false;}},
 './performanceSchedule':{importGameCustomProfile:async(...args)=>{counts.profileImports=(counts.profileImports??0)+1;return memory.store.importGameCustomProfile(...args);},removeGameCustomProfile:async(...args)=>{counts.profileRemovals=(counts.profileRemovals??0)+1;return memory.store.removeGameCustomProfile(...args);},restorePerformanceScheduleIfConfigured:async()=>{counts.profileRestores=(counts.profileRestores??0)+1;if(state.profileRestoreError)throw Error('injected restore error');return memory.store.getGameCustomConfig().entries[gamePolicyKey(state.game)]?.enabled===true?'dedicated':'global';},getContinuousPerformanceSnapshot:readPerformance,updateContinuousPerformanceControls:updatePerformance,getGameCustomConfig:memory.store.getGameCustomConfig,loadGameCustomConfig:memory.store.loadGameCustomConfig,saveGameCustomConfig:async config=>{counts.saves++;await memory.store.saveGameCustomConfig(config);},resolveGameCustomProfiles:memory.store.resolveGameCustomProfiles,applyPerformanceSchedule:async(side,mode)=>{counts.globalPowerApplies++;state.schedule.active[side]=mode;for(const callback of [...scheduleListeners])callback(structuredClone(state.schedule));return true;},loadPerformanceSchedule:async()=>{counts.schedule++;return structuredClone(state.schedule);},onGameCustomConfigChanged:memory.store.onGameCustomConfigChanged,onPerformanceScheduleChanged:callback=>{scheduleListeners.add(callback);return()=>scheduleListeners.delete(callback);}},
 './gameCorePolicy':{detectGameCorePolicy:async()=>{counts.capabilities++;return state.caps?structuredClone(state.caps):null;}},'./deckyGameActions':gameModule,'./deckyFanActions':fanModule,'./deckyMirrorRelay':relayModule,
 './deckyFanDisplay':{observeFanMirrorDisplay:value=>{fanDisplay=structuredClone(value);for(const cb of displayListeners)cb(value);},getFanMirrorDisplay:()=>fanDisplay,onFanMirrorDisplay:cb=>{displayListeners.add(cb);return()=>displayListeners.delete(cb);},isFanMirrorUiBusy:()=>false,onFanMirrorUiGate:callback=>{fanListeners.add(callback);return()=>fanListeners.delete(callback);}},
 './fanFeature':{FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},fanControlActive:{get value(){return state.fan.enabled;}},getFanFeatureSettings:()=>{counts.fanSettingsSnapshots++;return {preset:state.fan.preset};},getFanSelectedPreset:()=>{counts.fanPresetReads++;return state.fan.preset;},getFanPresetCurve:()=>[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:100}],saveFanCurve:async()=>{counts.fanSaves++;throw new Error('Game host fixture must not save Fan');},setFanControlActive:()=>{throw new Error('Game host fixture must not write Fan');},setFanNavigationDuty:()=>{},recordFanHandshake:async()=>{}},
 './fanHost':{fanHostLifecycle:{controlReady:true}},
 };
 if(fanOwner){modules['./fanHost']={fanHostLifecycle:fanOwner.lifecycle};modules['./fanFeature']={...fanOwner.featureBindings,getFanSelectedPreset:fanOwner.featureBindings.getFanSelectedPreset||(()=>fanOwner.featureBindings.getFanFeatureSettings().preset),saveFanCurve:async(...args)=>{counts.fanSaves++;return fanOwner.featureBindings.saveFanCurve(...args);}};}
 modules['./losslessRuntimeState']={retainLosslessObservation:()=>()=>{},readLosslessProgram:async()=>{
  await readHooks.losslessProgram?.();
  return losslessOwner?losslessOwner.readLosslessProgram():structuredClone(state.losslessProgram??{known:true,running:state.lossless.running});
 }};
 modules['./losslessScaling']=load(fs.readFileSync(path.join(root,'src/bridge/losslessScaling.ts'),'utf8'),{
  './deckyLosslessState':losslessState,'./api':modules['./api'],'./quickapp':losslessOwner?.owner??{getLosslessGameState:async()=>{await readHooks.lossless?.();return structuredClone(state.lossless);},setLosslessScalingEnabled:()=>{throw Error('No real LS control in fixture');},onLosslessScalingChanged:callback=>on('test.losslessChanged',callback)},'./gamePolicyTarget':modules['./gamePolicyTarget'],'./deckyGameActions':gameModule,'./quickActionLock':memory.lock});
 modules['./fxSound']=load(fs.readFileSync(path.join(root,'src/bridge/fxSound.ts'),'utf8'),{'./ipc':{invoke}},fxClock);
 const source=hostSource??fs.readFileSync(process.env.YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE||path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8');
 const start=()=>{if(handle)throw new Error('Already running host fixture');handle=load(source,modules,controllerClock).startDeckyMirrorHost();return handle;};
 const stop=()=>{handle?.stop();handle=null;};
 const result={memory,state,counts,setGameReadiness,readinessSubscriberCount:()=>readinessListeners.size,fxTimerCount:()=>fxTimers.size,advanceFx:()=>{for(const [id,t] of [...fxTimers]){fxTimers.delete(id);t.fn();}},replies,diagnostics,emit,start,stop,controllerTimerCount:()=>controllerTimers.size,advanceController:ms=>{clock.now+=ms;for(const [id,t] of [...controllerTimers])if(t.at<=clock.now){controllerTimers.delete(id);t.fn();}},ownerStats:()=>{const f=fanOwner?.stats();return {fan:f?{active:f.active,preset:f.preset,saveCount:f.saveCount,calls:f.calls,leasePresent:!!f.lease,physicalHardwareWrites:0}:undefined,lossless:losslessOwner?.stats()};},closeOwners:async()=>{await fanOwner?.close();},ready:()=>{if(!handle)throw new Error('No host');handle.ready();},
  gameChanged:()=>{for(const callback of [...gameListeners])callback(state.game);},scheduleChanged:()=>{for(const callback of [...scheduleListeners])callback(structuredClone(state.schedule));},powerChanged:(pending=false)=>emit(pending?'power.pending':'power.resumed',structuredClone(state.power)),
  monitorChanged:()=>emit('test.monitorChanged',structuredClone(state.monitor)),
  trainerChanged:()=>emit('test.trainerChanged'),
  observerStats:()=>({ipc:[...events.values()].reduce((n,set)=>n+set.size,0),game:gameListeners.size,schedule:scheduleListeners.size,fan:fanListeners.size,watchers:counts.watchers})};
 start();return result;
}
