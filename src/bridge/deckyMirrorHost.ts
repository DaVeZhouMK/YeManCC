import {fxSound} from './fxSound';
import {gameSteamContext} from './gameSteamContext';
import {startDeckyUpdaterHost} from './deckyUpdaterHost';
import {createProfileMirrorActions,gameProfileRevision,gameProfileImportSource} from './deckyProfileActions';
import {createPerformanceMirrorActions,performanceMirrorSource,performanceMirrorView} from './deckyPerformanceActions';
import { createTrainerMirrorActions, trainerMirrorSource } from './deckyTrainerActions';
import { refreshGameTrainerRecovery } from './gameTrainerRecovery';
import { getReadyGameTrainer, launchReadyGameTrainer, subscribeGameTrainerState } from './gameTrainer';
import { DeckyLosslessObservation, deckyLosslessText, type DeckyLosslessDiagnostic } from './deckyLosslessState';
import { dispatchDeckyMirrorPeer, dispatchDeckyMirrorRequest, observeDeckyMirrorRequestPeer } from './deckyMirrorExtensions';
import { createMonitorMirrorActions } from './deckyMonitorActions';
import { readMonitorOutputPreferences, onMonitorOutputPreferencesChanged, monitorOutputCoordinator } from './monitorOutput';
// YMCC Decky sidebar snapshot: existing stores/owners only, no new configuration.
import { compareAndSwapInputSettings, getSettingsGeneration, onInputSettingsChanged, readSettingsSection, saveSettingsSection } from './settingsRepository';
import { loadGlobalFrameRates, saveGlobalFrameRate, applyIndependentFrameRates, onFrameRatesChanged, dedicatedFrameRatePair, type FrameRatePair } from './frameRateLimits';
import { createFrameMirrorActions, type FrameMirrorRead } from './deckyFrameActions';
import { createTouchpadMirrorActions, touchpadMirrorFields, touchpadMirrorSource, type TouchpadMirrorRead } from './deckyTouchpadActions';
import { screenTouchpadsGet, screenTouchpadsSet, screenTouchpadProfile } from './screenTouchpads';
import { watch } from 'vue';
import { createGlobalMirrorActions, globalMirrorRead } from './deckyGlobalActions';
import { currentVirtualTargetPolicy } from './virtualTargetPolicyState';
import { virtualTargetSelectionError } from './virtualTargetPolicy';
import { effectiveInputPersona } from './gameInputOverride';
import { VirtualStartProtectionView } from './virtualStartProtectionView';
import { acceptVirtualOutputRuntime, virtualOutputPresentation, type VirtualOutputRuntimeState, type ExternalPriorityRuntimeState } from './virtualOutputStatus';
import { invoke, on, getLastGamepadState } from './ipc';
import { powerLifecycle } from './api';
import { onSharedPowerLifecycle,isSharedPowerLifecycleAdmitted } from './powerLifecycleState';
import { powerSourceMode, refreshPowerSourceSnapshot } from './powerSource';
import { readLosslessScaling, losslessField, setLosslessFromMirror, onLosslessScalingChanged, type LsDocument } from './losslessScaling';
import {retainLosslessObservation,readLosslessProgram} from './losslessRuntimeState';
import { getPolicyGame, gamePolicyKey, subscribePolicyGameStatus, refreshPolicyGameStrict } from './gamePolicyTarget';
import { getGameCustomConfig, loadGameCustomConfig, saveGameCustomConfig, loadPerformanceSchedule, onGameCustomConfigChanged, onPerformanceScheduleChanged,
  getContinuousPerformanceSnapshot, updateContinuousPerformanceControls, importGameCustomProfile, removeGameCustomProfile, restorePerformanceScheduleIfConfigured, type PerformanceScheduleConfig } from './performanceSchedule';
import { fanFeatureEnabled, fanControlActive, getFanSelectedPreset, getFanPresetCurve, saveFanCurve,
  setFanControlActive, setFanNavigationDuty, recordFanHandshake, FAN_IMPORT_ENABLED, FAN_FORCE_PREVIEW } from './fanFeature';
import { createFanMirrorActions } from './deckyFanActions';
import { makeGameMirrorAdmission, gameMirrorIdentity } from './deckyGameActions';
import { createSpeedMirrorActions, type SpeedMirrorNativeState } from './deckySpeedActions';
import { SPEED_PRESETS, applyGameSpeed, clearGameSpeed, getGameSpeedState, isMinecraftTarget, onGameSpeedStateChanged } from './speedhack';
import { readGameMirrorGyroDefaults } from './deckyGyroDefaults';
import { detectedGameName, isGameStatusCurrent, subscribeGameTargetReadiness } from './gamedetect';
import { tryAcquireQuickAction, isQuickActionBusy } from './quickActionLock';
import { closeJoyxoffIfRunning } from './gameproc';
import { observeFanMirrorDisplay, getFanMirrorDisplay, onFanMirrorDisplay, isFanMirrorUiBusy, onFanMirrorUiGate } from './deckyFanDisplay';
import { fanHostLifecycle } from './fanHost';
import { ReadOnlyMirrorRelay, type MirrorPeer, type MirrorRequest, type RelayReadSnapshot } from './deckyMirrorRelay';
import type { SteamRunningObservation } from '../../decky-plugin/src/steamRunningContext';
import type { Choice, MirrorField, MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';

const fan: Choice[] = [{data:'soft',label:'轻柔转速'},{data:'balanced',label:'均衡转速'},{data:'aggressive',label:'暴力转速'}];
function field(value: string, choices: Choice[], supported = true): MirrorField { return { value, choices: structuredClone(choices), supported }; }

const startupPhases = ['registered','native-context','fan-settings','ui-settings','placement','background-state','power-source','background-media','wake-state','fan-owner','boot-mode','performance','tray','ready'] as const;
export type DeckyStartupPhase = typeof startupPhases[number];

export function startDeckyMirrorHost(): { startupPhase: (phase: DeckyStartupPhase) => void; ready: () => void; stop: () => void } {
  let hostReady = false;
  let provenance: NonNullable<MirrorSnapshot['provenance']> = { kind: 'unconfirmed' };
  let schedule: PerformanceScheduleConfig | null = null;
  let ended = false;
  // At most one fact per fixed startup boundary. No timer or readiness authority:
  // an unfinished startup leaves its last entered phase in the original log.
  let startupIndex=-1,startupStarted=0,startupLast=0;
  const startupPhase=(stage:DeckyStartupPhase)=>{
    try{
      if(ended||hostReady)return;
      const index=startupPhases.indexOf(stage);if(index<0||index<=startupIndex)return;
      const now=performance.now();if(!Number.isFinite(now)||now<0||(startupIndex>=0&&now<startupLast))return;
      if(startupIndex<0){startupStarted=now;startupLast=now;}
      const detail={event:'startup-phase',stage,elapsedMs:Math.min(3600000,Math.round(now-startupStarted)),
        ...(startupIndex>=0?{previousStage:startupPhases[startupIndex],previousMs:Math.min(3600000,Math.round(now-startupLast))}:{})};
      startupIndex=index;startupLast=now;
      void invoke<boolean>('deckySidebar.mirrorDiagnostic',detail,{timeoutMs:1000}).catch(()=>{});
    }catch{/* Observing startup cannot fail or delay original restoration. */}
  };
  // Read the same native receipt/presentation as the controller page. This is a
  // bounded view of its startup lease, never a new input owner or a retry loop.
  const controllerProtection=new VirtualStartProtectionView();
  let controllerRuntime:VirtualOutputRuntimeState|null=null;
  let externalPriority:ExternalPriorityRuntimeState|null=null;
  let controllerFeedback:((raw:unknown)=>void)|null=null;
  let controllerTarget={persona:'disabled',enabled:false};
  const observeController=(raw:unknown)=>{
    const state=raw as {virtualOutput?:VirtualOutputRuntimeState;externalPriority?:ExternalPriorityRuntimeState}|null;
    const view=controllerProtection.observe(state?.virtualOutput?.startProtection,performance.now());
    controllerRuntime=acceptVirtualOutputRuntime(controllerRuntime,state?.virtualOutput??null,view.requestId);
    externalPriority=state?.externalPriority??null;
    return view;
  };
  const controllerLocked=()=>observeController(getLastGamepadState()).locked;
  const gameTargetReady=()=>hostReady&&!ended&&isGameStatusCurrent();
  const updaterHost=startDeckyUpdaterHost(()=>hostReady&&!ended);
  let steam:SteamRunningObservation = {availability:'unavailable'};
  const fanActions = createFanMirrorActions({
    owner:fanHostLifecycle,enabled:() => fanControlActive.value,currentPreset:getFanSelectedPreset,
    curve:getFanPresetCurve,save:saveFanCurve,updateActive:setFanControlActive,updateDuty:setFanNavigationDuty,
    power:() => powerLifecycle.get(),featureAllowed:() => hostReady && !ended && FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW,
    uiBusy:isFanMirrorUiBusy,display:observeFanMirrorDisplay,observeCapability:allowed => recordFanHandshake(allowed),
  });
  // This completion is still inside YMCC. Preserve commit truth even when the
  // accepted request has just acquired its own native startup protection. Only
  // the original native shortcut/input owner may apply or refuse the selection.
  const finishInput=async(saved:boolean,context:import('./deckyMirrorRelay').RelayMutationContext)=>{
    let acknowledged=false;
    try{
      context.checkpoint();
      const power=await powerLifecycle.get();context.checkpoint();
      if(hostReady && !ended && power.phase==='ready' && power.hardwareWritesAllowed && power.generation===context.generation && gameMirrorIdentity(getPolicyGame())===(context.gameIdentity??'')){
        const result=await invoke<{ok?:boolean;startProtection?:NonNullable<VirtualOutputRuntimeState['startProtection']>}|boolean>('input.shortcutRuntime.clear',{observeProtection:true});
        if(!ended&&typeof result==='object'&&result?.startProtection){
          const feedback={virtualOutput:{...controllerRuntime,startProtection:result.startProtection},externalPriority};
          if(controllerFeedback)controllerFeedback(feedback);else observeController(feedback);
        }
        acknowledged=result===true || typeof result==='object'&&result?.ok===true;
      }
    }catch{/* A refused/unknown apply cannot undo a completed original save. */}
    return {saved,applied:false,pending:true,notice:(saved?'已保存输入选择':'已提交当前手柄选择')+(acknowledged?'；等待 YMCC 原输入服务确认':'；运行确认未完成，请查看手柄状态，不自动重试')};
  };
  const gameLabel = (game: NonNullable<ReturnType<typeof getPolicyGame>>) => detectedGameName(game) || game.name || gamePolicyKey(game) || '当前游戏';
  const speedActions = createSpeedMirrorActions({featureAllowed:gameTargetReady,game:getPolicyGame,power:() => powerLifecycle.get(),acquire:tryAcquireQuickAction,presets:SPEED_PRESETS,blocked:isMinecraftTarget,state:getGameSpeedState,nativeState:()=>invoke<SpeedMirrorNativeState>('deckySidebar.mirrorAttach',{}, {timeoutMs:1500}),apply:(game,factor,admission) => applyGameSpeed(game.pid,factor,game,'user-factor',admission),clear:(game,admission) => clearGameSpeed(game.pid,'user-reset',admission)});
  const trainerActions = createTrainerMirrorActions({allowed:gameTargetReady,game:getPolicyGame,label:gameLabel,
    ready:(name,path)=>getReadyGameTrainer(name,path,getPolicyGame()??undefined),power:()=>powerLifecycle.get(),busy:isQuickActionBusy,invalidate:()=>relay.invalidate(),
    launch:(game,isTargetCurrent)=>launchReadyGameTrainer(gameLabel(game),game.path,{isTargetCurrent,processTarget:game,interactiveShell:true})});
  const readGlobal=async(cachedSchedule?:PerformanceScheduleConfig,inputRead=()=>readSettingsSection('input'))=>{
    return globalMirrorRead(cachedSchedule??await loadPerformanceSchedule(),await inputRead() as any);
  };
  const globalActions=createGlobalMirrorActions({allowed:()=>hostReady&&!ended,game:getPolicyGame,locked:controllerLocked,
    contextCurrent:context=>gameMirrorIdentity(getPolicyGame())===(context.gameIdentity??''),
    side:()=>powerSourceMode.value,read:readGlobal,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,cas:compareAndSwapInputSettings,closeJoyxoff:closeJoyxoffIfRunning,finishInput,clearShortcut:()=>invoke('input.shortcutRuntime.clear',{observeProtection:true})});
  let performanceSource='',performanceRevision=0,profileSource='',profileRevision=0;
  const profileActions=createProfileMirrorActions({allowed:gameTargetReady,game:getPolicyGame,label:gameLabel,settingsGeneration:getSettingsGeneration,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,load:loadGameCustomConfig,schedule:loadPerformanceSchedule,currentSchedule:()=>schedule,snapshot:()=>getContinuousPerformanceSnapshot(),import:importGameCustomProfile,remove:removeGameCustomProfile,restore:()=>restorePerformanceScheduleIfConfigured()});
  const performanceActions=createPerformanceMirrorActions({allowed:gameTargetReady,read:()=>getContinuousPerformanceSnapshot(),update:updateContinuousPerformanceControls,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction});
  // Only this display can supply its captured profile. Ordinary action reads
  // keep fresh=true and ignore display data, including a supplied fourth arg.
  const readFrames=async(fresh=true,game=getPolicyGame(),readGlobalFrames=loadGlobalFrameRates,displayConfig?:ReturnType<typeof getGameCustomConfig>):Promise<FrameMirrorRead>=>{
    const global=await readGlobalFrames();
    const config=fresh?await loadGameCustomConfig():displayConfig??getGameCustomConfig();
    const currentSchedule=fresh&&game?await loadPerformanceSchedule():schedule;
    const key=game?gamePolicyKey(game):'',entry=config.entries[key];
    const dedicated=!!entry&&entry.enabled!==false&&config.rtss[key]?.enabled!==false;
    const pair=dedicated?dedicatedFrameRatePair(config.rtss[key],global):global;
    return {game,global,pair,config,schedule:currentSchedule,dedicated,
      source:JSON.stringify({identity:gameMirrorIdentity(game),global,config,schedule:currentSchedule})};
  };
  const readTouchpads=async(inputRead=()=>readSettingsSection('input')):Promise<TouchpadMirrorRead>=>{
    const input=await inputRead(),target=input.outputTarget||{};
    const persona=typeof target.persona==='string'?target.persona:'disabled';
    const virtualEnabled=target.buttonMappingEnabled===true;
    const state=await screenTouchpadsGet(screenTouchpadProfile(persona));
    return {persona,virtualEnabled,state,source:touchpadMirrorSource(persona,virtualEnabled,state),fields:touchpadMirrorFields(persona,virtualEnabled,state)};
  };
  const frameActions=createFrameMirrorActions({allowed:gameTargetReady,game:getPolicyGame,side:()=>powerSourceMode.value,power:()=>powerLifecycle.get(),
    acquire:tryAcquireQuickAction,read:()=>readFrames(),key:gamePolicyKey,label:gameLabel,gyro:readGameMirrorGyroDefaults,
    save:saveGameCustomConfig,saveGlobal:saveGlobalFrameRate,apply:applyIndependentFrameRates});
  const touchpadActions=createTouchpadMirrorActions({allowed:()=>hostReady&&!ended,read:readTouchpads,power:()=>powerLifecycle.get(),set:screenTouchpadsSet});
  const monitorActions = createMonitorMirrorActions({allowed:()=>hostReady&&!ended,power:()=>powerLifecycle.get(),
    read:readMonitorOutputPreferences,save:enabled=>saveSettingsSection('rtss',{monitorOutput:{enabled}}),apply:prefs=>monitorOutputCoordinator.apply(prefs)});
  const losslessDiagnostic:DeckyLosslessDiagnostic=(event,detail)=>{void invoke<boolean>('deckySidebar.mirrorDiagnostic',{event,...detail},{timeoutMs:1000}).catch(()=>{});};
  // A snapshot consumes its own LS completions before publishing. A completion
  // after that transaction still invalidates; unrelated source events always do.
  type SnapshotReadLease={audioPending:boolean;powerSource:string|null;powerObservation:string|null};
  let snapshotReadLease:SnapshotReadLease|null=null,observedPowerSource:string|null=null;
  let beginAudioObservation:(()=>void)|null=null;
  // One notification's existing read, consumed once by its following snapshot.
  // Explicit demand and observer retirement discard it; actions still read fresh.
  let audioObservation:{read:ReturnType<typeof fxSound.read>;powerSource:string|null}|null=null;
  const powerSnapshotSource=(value:{generation?:unknown;phase?:unknown;known?:unknown;hardwareWritesAllowed?:unknown;inputReady?:unknown})=>
    JSON.stringify([value.generation,value.phase,value.known!==false,value.hardwareWritesAllowed,value.inputReady]);
  const losslessObserved=()=>{if(snapshotReadLease===null)relay.invalidate();};
  const losslessObservation=new DeckyLosslessObservation<LsDocument>(losslessObserved,losslessDiagnostic);
  const losslessProgramObservation=new DeckyLosslessObservation<Awaited<ReturnType<typeof readLosslessProgram>>>(losslessObserved,losslessDiagnostic);
  let lastReadView:RelayReadSnapshot|null=null;
  // Optional metadata belongs only to this visible relay's display. The shared
  // reader remains the facts owner; these DTOs never enter action admission.
  type GameSteamDisplay={identity:string;generation:number;value:RelayReadSnapshot['gameSteam']};
  type GameSteamDisplayScope={cancellation:AbortController;view:GameSteamDisplay|null};
  let gameSteamDisplayVisible=false,gameSteamDisplayScope:GameSteamDisplayScope|null=null;
  const retireGameSteamDisplay=()=>{
    const old=gameSteamDisplayScope;
    gameSteamDisplayScope=gameSteamDisplayVisible?{cancellation:new AbortController(),view:null}:null;
    old?.cancellation.abort(); // Only this consumer's subscribers, never the singleton.
  };
  const warmGameSteamDisplay=()=>{
    const scope=gameSteamDisplayScope;
    if(!ended&&gameSteamDisplayVisible&&scope&&!scope.cancellation.signal.aborted)
      void gameSteamContext.refresh({signal:scope.cancellation.signal}).catch(()=>{});
  };
  const optionalGameSteam=(game:ReturnType<typeof getPolicyGame>,generation:number):GameSteamDisplay|undefined=>{
    if(!game||!gameSteamDisplayVisible||!isGameStatusCurrent()){retireGameSteamDisplay();return;}
    const identity=gameMirrorIdentity(game);
    if(gameSteamDisplayScope?.view&&(gameSteamDisplayScope.view.identity!==identity||gameSteamDisplayScope.view.generation!==generation))retireGameSteamDisplay();
    const scope=gameSteamDisplayScope;if(!scope)return;
    if(scope.view)return scope.view;
    const view:GameSteamDisplay={identity,generation,value:undefined};scope.view=view;
    const current=()=>!ended&&gameSteamDisplayVisible&&gameSteamDisplayScope===scope&&scope.view===view&&
      !scope.cancellation.signal.aborted&&isGameStatusCurrent()&&gameMirrorIdentity(getPolicyGame())===identity;
    const complete=(value?:Awaited<ReturnType<typeof gameSteamContext.read>>)=>{
      if(!current())return;
      const observed=value?.game;
      if(!observed||(observed.pid===game.pid&&observed.processCreated===String(game.processCreated)&&observed.path===game.path&&
          observed.identityGeneration===game.identityGeneration&&(observed.powerGeneration===undefined||observed.powerGeneration===String(generation))))view.value=value;
      // An in-flight core snapshot consumes this value at its final synchronous
      // assembly. A later completion repaints once and never submits a warm.
      if(snapshotReadLease===null && !relay.repaintGameSteam(identity,generation,view.value))relay.invalidate();
    };
    void Promise.resolve().then(()=>current()?gameSteamContext.read({signal:scope.cancellation.signal}):undefined).then(complete,()=>complete());
    return view;
  };

  // Pure existing owner state, including its independent cancel/intent gate.
  const readFanView=():MirrorSnapshot['fan']=>{
    const preset=getFanSelectedPreset(),display=getFanMirrorDisplay(),enabled=fanActions.pendingIntent||fanControlActive.value;
    const pending=display?.active===true&&enabled&&(display.pending===true||fanHostLifecycle.recoveryActive)&&!(fanHostLifecycle.controlReady&&fanHostLifecycle.state==='ready'&&!fanHostLifecycle.recoveryActive);
    return {supported:fanFeatureEnabled.value&&fanHostLifecycle.controlReady,enabled,preset,pending,notice:display?.notice,
      canToggle:FAN_IMPORT_ENABLED&&!FAN_FORCE_PREVIEW&&!isFanMirrorUiBusy(),choices:structuredClone(fan)};
  };
  const relay = new ReadOnlyMirrorRelay({
    diagnostic:(event,detail)=>{void invoke<boolean>('deckySidebar.mirrorDiagnostic',{event,...detail},{timeoutMs:1000}).catch(()=>{});},
    snapshotDemand(){
      audioObservation=null;
      losslessObservation.demand();losslessProgramObservation.demand();
      // Only an explicit sidebar request warms missing shared facts. Completion
      // invalidates the display; it never grants a second refresh attempt.
      retireGameSteamDisplay();warmGameSteamDisplay();
    },
    observeSteam(observation){
      if(JSON.stringify(observation)===JSON.stringify(steam))return false;
      steam=observation;return true;
    },
    async read(trace) {
      const measure:NonNullable<typeof trace>['measure']=trace?.measure??(async(_stage,read)=>read());
      const makeEarly = (notice: string): Omit<MirrorSnapshot,'runId'|'revision'> => ({ generation:0,ready:false,provenance,game:null,
        fan:{supported:false,enabled:false,preset:'',choices:structuredClone(fan)},notice });
      if (!hostReady || ended) return makeEarly('YMCC 正在初始化，等待当前状态就绪');
      const observedAudio=audioObservation;audioObservation=null;
      const readLease:SnapshotReadLease={audioPending:true,powerSource:null,powerObservation:null};snapshotReadLease=readLease;
      try {
      // Power is one on-demand native read. Game/fan data is already held by the resident YMCC owner.
      const power=await measure('power',()=>powerLifecycle.get());
      if(ended || snapshotReadLease!==readLease)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
      readLease.powerSource=powerSnapshotSource(power);
      // A shared fact received during native IO is consumed by this read only
      // when it confirms the same state. A changed fact retires it before any
      // business reader starts; no Ready or write permission is synthesized.
      if(readLease.powerObservation!==null&&readLease.powerObservation!==readLease.powerSource){
        relay.invalidateFresh(true,'power');throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
      }
      observedPowerSource=readLease.powerSource;
      if(power.phase!=='ready'||power.hardwareWritesAllowed!==true){
        retireGameSteamDisplay();
        // Recovery may observe only the native lifecycle fact while gated. Retain
        // display values, never their old admissions or a different game's target.
        const previous=(lastReadView?.game?.identity??'')===gameMirrorIdentity(getPolicyGame())?lastReadView:null;
        const currentFan=readFanView();
        return {...(previous??makeEarly('')),generation:power.generation,ready:false,provenance,steam:structuredClone(steam),
          gameAdmission:undefined,controlAdmission:undefined,gameSteam:undefined,
          actions:{fan:currentFan.canToggle===true,game:false,performance:false,frames:false,touchpads:false,global:false,monitor:false,audioOptimization:false},
          fan:currentFan,notice:'已连接 YMCC · 等待唤醒完成，控制暂不可用'};
      }
      // retain() and this request share the original in-flight audio read.
      // Its completion is consumed here; changes after this await must refresh.
      const readAudio=async()=>{try{
        // Start the original read before retaining its observer. retain() can
        // then share this flight instead of eagerly finishing another read
        // while this snapshot is still awaiting its native power fact.
        const pending=observedAudio&&observedAudio.powerSource===readLease.powerSource?observedAudio.read:fxSound.read();
        beginAudioObservation?.();return await pending;
      }finally{readLease.audioPending=false;}};
      // Audio and the independent controls share this read lease, not a serial
      // prerequisite. Observe rejection immediately while other owners settle.
      const audioRead=measure('audio',readAudio).then(value=>({ok:true as const,value}),error=>({ok:false as const,error}));
      await measure('powerSource',refreshPowerSourceSnapshot);
      if(ended || snapshotReadLease!==readLease)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
      // One current set of owner reads supplies this entire display. Start
      // independent capabilities while the shared schedule/profile IO settles.
      const scheduleGeneration=getSettingsGeneration(),previousSchedule=schedule;
      const game=getPolicyGame(),identity=gameMirrorIdentity(game),profileGeneration=scheduleGeneration;
      const scheduleRead=measure('schedule',loadPerformanceSchedule);
      // A power or other relay invalidation can retire this read without replacing the game.
      const profileReadCurrent=()=>!ended&&snapshotReadLease===readLease&&(!trace||trace.isCurrent())&&gameMirrorIdentity(getPolicyGame())===identity;
      const profileRead=game?measure('profile',()=>loadGameCustomConfig('read-only',{
        onTiming:trace?.profileStage,
        displayCurrent:profileReadCurrent,
      })):Promise.resolve(getGameCustomConfig());
      // One fresh section supplies FPS and monitor display in this snapshot.
      // A failed first read does not consume monitor's independent recovery read.
      const rtssRead=readSettingsSection('rtss');
      const readMonitorRtss=()=>rtssRead.catch(()=>{
        if(ended || snapshotReadLease!==readLease)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
        return readSettingsSection('rtss');
      });
      const globalFrameRates=loadGlobalFrameRates(()=>rtssRead);
      // Keep early failures observed while another owner is pending. These
      // original promises still reject for their normal consumers below.
      void profileRead.catch(()=>{});void globalFrameRates.catch(()=>{});
      const performanceTask=measure('performance',()=>getContinuousPerformanceSnapshot('effective',undefined,(stage,elapsedMs)=>trace?.performanceStage?.(stage,elapsedMs),'read-only',{generation:profileGeneration,schedule:scheduleRead,custom:profileRead,globalFrameRates}))
        .then(value=>({ok:true as const,value}),error=>({ok:false as const,error}));
      // A failed config read cannot become a default/global game admission.
      // Keep unrelated original owners usable and let the relay's existing
      // partial-read timer retry; transport and business intents are untouched.
      const independentConfigView=async(notice:string):Promise<RelayReadSnapshot>=>{
        const expectedSchedule=schedule;
        const current=()=>schedule===expectedSchedule&&!ended&&snapshotReadLease===readLease&&getSettingsGeneration()===scheduleGeneration&&
          gameMirrorIdentity(getPolicyGame())===identity;
        if(!current())throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
        const side=powerSourceMode.value,input=measure('input',()=>readSettingsSection('input'));
        const [touchpadRead,monitorRead]=await Promise.allSettled([
          measure('touchpads',()=>readTouchpads(()=>input)),
          measure('monitor',()=>readMonitorOutputPreferences(readMonitorRtss)),
        ] as const);
        const audio=await audioRead;
        // This fault path crosses independent awaits. Recheck the same native
        // fact before offering any unaffected owner's current admission.
        if(!current())throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
        const after=await powerLifecycle.get();
        if(!current()||powerSnapshotSource(after)!==readLease.powerSource||powerSourceMode.value!==side)
          throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
        const monitor=monitorRead.status==='fulfilled'?monitorRead.value:null;
        const touchpads=touchpadRead.status==='fulfilled'?touchpadRead.value:null;
        const audioOptimization=audio.ok?audio.value:undefined,fan=readFanView();
        const view:RelayReadSnapshot={generation:power.generation,ready:true,provenance,steam:structuredClone(steam),
          game:game?{label:gameLabel(game),identity,fields:{}}:null,powerSource:side,
          controlAdmission:{audioOptimization:audioOptimization?JSON.stringify(audioOptimization):undefined,
            monitor:monitor?JSON.stringify(monitor):undefined,touchpads:touchpads?.source},
          audioOptimization,monitor:monitor?{enabled:monitor.enabled}:undefined,
          touchpads:touchpads?{persona:touchpads.persona,fields:touchpads.fields}:undefined,fan,
          actions:{game:false,performance:false,frames:false,global:false,
            audioOptimization:audioOptimization?.known===true&&audioOptimization.installed,
            monitor:!!monitor,touchpads:!!touchpads&&touchpads.state.ok&&touchpads.state.available,
            fan:FAN_IMPORT_ENABLED&&!FAN_FORCE_PREVIEW&&!isFanMirrorUiBusy()},notice};
        lastReadView=view;return view;
      };
      let loaded:PerformanceScheduleConfig;
      try{loaded=await scheduleRead;}
      catch{return await independentConfigView('性能配置读取失败，相关控制暂不可用');}
      if(ended || snapshotReadLease!==readLease || schedule!==previousSchedule || getSettingsGeneration()!==scheduleGeneration || gameMirrorIdentity(getPolicyGame())!==identity)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
      schedule=loaded;
      const snapshotSchedule=schedule;
      // External profile deletion remains observable through the original read.
      const gameSteamDisplay=optionalGameSteam(game,power.generation);
      let profileConfig:Awaited<typeof profileRead>;
      try{profileConfig=await profileRead;}
      catch(error){
        if(!profileReadCurrent())throw error;
        return await independentConfigView('游戏专属配置读取失败，相关控制暂不可用');
      }
      const profileDocument=JSON.stringify(profileConfig);
      const checkReadSource=()=>{
        if(ended || snapshotReadLease!==readLease || gameMirrorIdentity(getPolicyGame())!==identity || schedule!==snapshotSchedule)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
        // Return this accepted copy for the final synchronous profile receipt.
        // Both pre-control and final fences still query the current owner.
        const currentConfig=game?getGameCustomConfig():profileConfig;
        if(game&&JSON.stringify(currentConfig)!==profileDocument)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
        return currentConfig;
      };
      // Start non-blocking LS observation before the parallel control batch. The
      // same current-key views below consume completions without a second batch.
      checkReadSource();
      const observeLossless=()=>game?.path?losslessObservation.view(identity+':'+power.generation+':'+power.phase,()=>readLosslessScaling(game.path)):null;
      const observeProgram=()=>losslessProgramObservation.view('program:'+power.generation+':'+power.phase,readLosslessProgram);
      if(!game)losslessObservation.clear();
      observeLossless();observeProgram();
      // Independent read-only owners join one snapshot. Share the input read and
      // cached schedule for the global view; mutation paths still load fresh data.
      const input=measure('input',()=>readSettingsSection('input')),inputRead=()=>input;
      // Commands keep their independent original FPS reader.
      const controls=Promise.allSettled([
        measure('frames',()=>readFrames(false,game,()=>globalFrameRates,profileConfig)),measure('touchpads',()=>readTouchpads(inputRead)),
        measure('global',()=>readGlobal(snapshotSchedule!,inputRead)),
        measure('monitor',()=>readMonitorOutputPreferences(readMonitorRtss)),performanceTask.then(result=>{if(!result.ok)throw (result as {ok:false;error:unknown}).error;return result.value;}),
      ] as const);
      // Steam IDs are volatile observations only. EXE remains the original config key,
      // including non-Steam games and the original same-basename sharing behavior.
      const entry = game ? profileConfig.entries[gamePolicyKey(game)] : undefined;
      // Input always mirrors global settings, independently of performance profiles.
      const fields: Record<string,MirrorField> = {};
      const speedState=getGameSpeedState(),ownSpeed=speedState?.pid===game?.pid && speedState?.processCreated===game?.processCreated;
      fields.speedFactor=field(ownSpeed&&speedState?.cleanupPending?'unknown':String(ownSpeed && speedState?.enabled ? speedState.factor : 1),SPEED_PRESETS.map(factor=>({data:String(factor),label:`${factor}×`})),!!game && !isMinecraftTarget(game));
      const [frameRead,touchpadRead,globalRead,monitorRead,performanceRead]=await controls;
      const audio=await audioRead;
      if(!audio.ok)throw (audio as {ok:false;error:unknown}).error;
      const audioOptimization=audio.value;
      const inputValue=await input.catch(()=>null);
      // A concurrent source event also invalidates the relay. This local fence
      // prevents old reads from resetting a newer observation key or publishing
      // a mixed game/schedule view before that retry is delivered.
      const confirmedProfileConfig=checkReadSource();
      const losslessView=observeLossless(),programView=observeProgram();
      const lossless=losslessView?.document??null,losslessError=losslessView?.notice??'';
      // Available even without a game or a matching EXE rule. No Decky-side probe.
      const losslessProgram=programView.pending||programView.notice?{known:false,running:false}:programView.document??{known:false,running:false};
      fields.losslessScaling = losslessField(lossless,losslessView?.pending===true);
      const frames=frameRead.status==='fulfilled'?frameRead.value:null;
      const touchpads=touchpadRead.status==='fulfilled'?touchpadRead.value:null;
      const global=globalRead.status==='fulfilled'?globalRead.value:null;
      const monitor=monitorRead.status==='fulfilled'?monitorRead.value:null;
      const performance=performanceRead.status==='fulfilled'?performanceRead.value:null;
      for(const key of ['padPersona','gyroEnabled','gyroPreset'])fields[key]=global?.fields[key]??field('unavailable',[],false);
      const controllerView=observeController(getLastGamepadState());

      const persona=inputValue?effectiveInputPersona(inputValue as any):'disabled';
      const enabled=!!inputValue && persona!=='disabled' && inputValue.outputTarget?.buttonMappingEnabled===true;
      controllerTarget={persona,enabled};
      const targetError=virtualTargetSelectionError(persona,currentVirtualTargetPolicy());
      const controllerPresentation=targetError?{text:'虚拟手柄-本机不可用',detail:targetError,tone:'warning' as const}:virtualOutputPresentation(controllerRuntime,enabled,persona,false,externalPriority);
      const virtualController=inputValue?{...controllerPresentation,locked:controllerView.locked}:undefined;
      if(controllerView.locked){
        for(const key of ['padPersona','gyroEnabled','gyroPreset']){
          fields[key]={...fields[key],supported:false};
          if(global?.fields[key])global.fields[key]={...global.fields[key],supported:false};
        }
      }
      const freshProfileSource=game?gameProfileRevision(confirmedProfileConfig,gamePolicyKey(game)):'';
      if(freshProfileSource!==profileSource){profileSource=freshProfileSource;profileRevision++;}
      fields.profile={value:entry&&entry.enabled!==false?'dedicated':'global',choices:[{data:'global',label:'全局配置'},{data:'dedicated',label:'游戏专属'}],supported:!!entry&&entry.enabled!==false||!!performance&&!!frames,sourceRevision:String(profileRevision),inherited:!entry};
      const freshPerformanceSource=performance?performanceMirrorSource(performance):'';
      if(freshPerformanceSource!==performanceSource){performanceSource=freshPerformanceSource;performanceRevision++;}
      const performanceView=performance?performanceMirrorView(performance,String(performanceRevision)):undefined;
      const controlError=frameRead.status==='rejected'?'独立帧率读取失败，暂不可修改':touchpadRead.status==='rejected'?'全局触摸板读取失败，暂不可修改':globalRead.status==='rejected'?'全局 TDP / 虚拟手柄读取失败':monitorRead.status==='rejected'?'监控总开关读取失败，暂不可修改':performanceRead.status==='rejected'?'性能调度读取失败，暂不可修改':'';
      const readyTrainer = game ? getReadyGameTrainer(gameLabel(game),game.path,game) : null;
      const trainer = trainerActions.view(game,readyTrainer);
      const fanView=readFanView();
      const gameSteam=gameSteamDisplayScope?.view===gameSteamDisplay?gameSteamDisplay?.value:undefined;
      const view:RelayReadSnapshot={ gameAdmission:game ? {...makeGameMirrorAdmission(entry,schedule!,lossless?.xml),speed:JSON.stringify(speedState),profile:freshProfileSource,profileSource:performance?gameProfileImportSource(performance):undefined} : undefined,steam:structuredClone(steam),gameSteam,generation:power.generation,ready:power.phase === 'ready' && power.hardwareWritesAllowed === true,
        controlAdmission:{audioOptimization:JSON.stringify(audioOptimization),performance:performance?freshPerformanceSource:undefined,trainer:trainerMirrorSource(readyTrainer),frames:frames?.source,touchpads:touchpads?.source,global:global?.source,monitor:monitor?JSON.stringify(monitor):undefined},
        audioOptimization,losslessProgram,trainer,performance:performanceView,virtualController,
        monitor:monitor?{enabled:monitor.enabled}:undefined,
        global:global?{fields:global.fields}:undefined,
        frames:frames?{pair:frames.pair,dedicated:frames.dedicated}:undefined,
        touchpads:touchpads?{persona:touchpads.persona,fields:touchpads.fields}:undefined,
        actions:{audioOptimization:audioOptimization.known&&audioOptimization.installed,performance:isGameStatusCurrent()&&!!performanceView?.enabled,monitor:!!monitor,global:!!global&&!controllerView.locked,frames:isGameStatusCurrent()&&!!frames&&powerSourceMode.value!==null,touchpads:!!touchpads&&touchpads.state.ok&&touchpads.state.available,fan:FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW && !isFanMirrorUiBusy(),game:isGameStatusCurrent()&&!!game && !!gamePolicyKey(game)},
        game:game ? {label:entry?.displayName || gameLabel(game),
          identity:gameMirrorIdentity(game),fields} : null,
        provenance, powerSource:powerSourceMode.value,
        fan:fanView,
        notice:power.phase !== 'ready' ? '已连接 YMCC · 等待唤醒完成，控制暂不可用' : controlError || losslessError || (!game ? 'YMCC 当前没有确认游戏' : (!lossless?.controlAvailable?deckyLosslessText(lossless?.reason || ''):'') || (!lossless ? 'LosslessScaling 插帧配置正在读取' : '')) };
      lastReadView=view;return view;
      } finally {
        // A stale peer's slow read cannot release a newer transaction's lease.
        if(snapshotReadLease===readLease)snapshotReadLease=null;
      }
    },
    observe(invalidate) {
      let observing=true;gameSteamDisplayVisible=true;retireGameSteamDisplay();
      // Already dispatched callbacks may arrive after unsubscribe. Retired
      // observers cannot alter a reopened display, source or recovery budget.
      // Allow every synchronous owner invalidation for the edge to settle
      // before creating one demand. Unsubscribe retires this queued callback.
      const warmGameSteam=()=>queueMicrotask(()=>{if(observing&&!ended)warmGameSteamDisplay();});
      // Sidebar demand may resume after edits made while its observers were paused.
      // Refresh this display cache once on resume; resident YMCC owners retain their own data.
      schedule = null;
      // A verified permission edge gets one fresh snapshot even when a different
      // reader is backing off. Keep that fault lease and its attempt unchanged.
      const fanAdmission=(view:MirrorSnapshot['fan'])=>JSON.stringify([view.supported,view.enabled,view.pending===true,view.canToggle===true]);
      const initialFan=readFanView();
      let observedFanSource=JSON.stringify(initialFan),observedFanAdmission=fanAdmission(initialFan);
      const refreshFan=()=>{
        if(!observing)return;
        const view=readFanView(),source=JSON.stringify(view);if(source===observedFanSource)return;
        observedFanSource=source;const admission=fanAdmission(view);
        if(admission!==observedFanAdmission){observedFanAdmission=admission;relay.invalidateFresh(false,'fan');}
        else invalidate('fan');
      };
      const stops: Array<() => void> = [retainLosslessObservation()];
      let audioObserved=false;
      const startAudio=()=>{if(observing&&!audioObserved){audioObserved=true;stops.push(fxSound.retain(()=>{if(observing&&snapshotReadLease?.audioPending!==true){
        // accept() notifies while the original native read is still in flight.
        // Borrow that promise instead of starting another read after power IO.
        // The owner only notifies accepted full-state changes. Refresh its
        // independent admission even while another read is backing off,
        // retaining that fault's original timer/attempt/deadline.
        audioObservation={read:fxSound.read(),powerSource:observedPowerSource};relay.invalidateFresh(false,'audio');
      }}));}};
      beginAudioObservation=startAudio;
      // A failed unsubscribe must not strand the remaining independent observers.
      const stopObservers=()=>{observing=false;if(beginAudioObservation===startAudio){beginAudioObservation=null;audioObservation=null;}gameSteamDisplayVisible=false;retireGameSteamDisplay();for(const stop of stops){try{stop();}catch{}}};
      try {
        const stopGameSteam=gameSteamContext.subscribeInvalidation?.(()=>{if(observing){retireGameSteamDisplay();invalidate('steam');}});
        if(stopGameSteam)stops.push(stopGameSteam);
        stops.push(onPerformanceScheduleChanged(value => { if(!observing)return;schedule = value; invalidate('schedule'); }));
        stops.push(onGameCustomConfigChanged(()=>invalidate('profile')));
        stops.push(onFrameRatesChanged(()=>invalidate('frames')));
        stops.push(onMonitorOutputPreferencesChanged(prefs=>{
          if(!observing)return;
          // Equivalent committed preferences need no repaint. A real enable
          // edge refreshes this independent owner during another read's fault;
          // the original fault timer/deadline and fresh action checks remain.
          if(JSON.stringify(prefs)===lastReadView?.controlAdmission?.monitor)return;
          if(lastReadView?.monitor&&prefs.enabled!==lastReadView.monitor.enabled)relay.invalidateFresh(false,'monitor');
          else invalidate('monitor');
        }));
        stops.push(on('screenTouchpads.updated',()=>invalidate('touchpads')));
        stops.push(on('gameSteam.context.changed',()=>{if(!observing)return;retireGameSteamDisplay();invalidate('steam');}));
        let observedSteamSource='';
        stops.push(on('gameSteam.context.sourceChanged',(raw:unknown)=>{
          if(!observing)return;
          const source=raw as {pid?:unknown;processCreated?:unknown;processEpoch?:unknown;accountEpoch?:unknown;sourceAvailable?:unknown;presence?:unknown;windowsSessionKnown?:unknown;windowsSession?:unknown}|null;
          if(!source||typeof source!=='object'||Array.isArray(source)||!Number.isSafeInteger(source.pid)||Number(source.pid)<0||
             typeof source.processCreated!=='string'||typeof source.processEpoch!=='string'||typeof source.accountEpoch!=='string')return;
          const hasPublication=['sourceAvailable','presence','windowsSessionKnown','windowsSession'].some(key=>key in source);
          if(hasPublication){
            if(typeof source.sourceAvailable!=='boolean'||![-1,0,1].includes(Number(source.presence))||typeof source.presence!=='number'||
               typeof source.windowsSessionKnown!=='boolean'||!Number.isSafeInteger(source.windowsSession)||Number(source.windowsSession)<0||
               (!source.windowsSessionKnown&&source.windowsSession!==0))return;
            if(!source.sourceAvailable&&(source.pid!==0||source.processCreated!=='0'||source.processEpoch!=='0'||source.accountEpoch!=='0'||
               source.presence!==-1||source.windowsSessionKnown!==false))return;
          }
          const signature=JSON.stringify(hasPublication?
            [source.pid,source.processCreated,source.processEpoch,source.accountEpoch,source.sourceAvailable,source.presence,source.windowsSessionKnown,source.windowsSession]:
            [source.pid,source.processCreated,source.processEpoch,source.accountEpoch]);
          if(signature===observedSteamSource)return;
          observedSteamSource=signature;
          retireGameSteamDisplay();warmGameSteam();invalidate('game');
        }));
        let controllerTimer:ReturnType<typeof setTimeout>|null=null,controllerDeadline:number|null=null;
        // Native samples include timestamps/counters. Only a user-visible status or
        // admission change may invalidate the sidebar, never the telemetry rate.
        const controllerSignature=()=>JSON.stringify([currentVirtualTargetPolicy(),virtualOutputPresentation(controllerRuntime,controllerTarget.enabled,controllerTarget.persona,false,externalPriority),controllerProtection.read(performance.now()).locked]);
        observeController(getLastGamepadState());
        let observedController=controllerSignature();
        const refreshController=(raw:unknown)=>{
          if(!observing)return;
          const view=observeController(raw);
          const next=controllerSignature();
          if(next!==observedController){observedController=next;relay.invalidateFresh(false,'controller');}
          if(view.deadline===controllerDeadline)return;
          if(controllerTimer!==null)clearTimeout(controllerTimer);
          controllerDeadline=view.deadline;controllerTimer=null;
          if(view.deadline!==null)controllerTimer=setTimeout(()=>{
            if(!observing)return;
            controllerTimer=null;controllerDeadline=null;
            const next=controllerSignature();
            if(next!==observedController){observedController=next;relay.invalidateFresh(false,'controller');}
          },Math.max(0,Math.ceil(view.deadline-performance.now())));
        };
        controllerFeedback=refreshController;
        stops.push(()=>{if(controllerFeedback===refreshController)controllerFeedback=null;if(controllerTimer!==null)clearTimeout(controllerTimer);});
        stops.push(on('gamepad.state',refreshController));
        refreshController(getLastGamepadState());
        stops.push(onLosslessScalingChanged(()=>{if(!observing)return;losslessObservation.refresh();losslessProgramObservation.refresh();invalidate('lossless');}));
        stops.push(onGameSpeedStateChanged(()=>invalidate('speed')));
        const trainerSource = () => {
          const current = getPolicyGame();
          return JSON.stringify([gameMirrorIdentity(current),trainerMirrorSource(current ? getReadyGameTrainer(gameLabel(current),current.path,current) : null)]);
        };
        let observedTrainer = trainerSource();
        stops.push(subscribeGameTrainerState(() => {
          if(!observing)return;
          const next = trainerSource();
          if(next !== observedTrainer){observedTrainer = next;invalidate('trainer');}
        }));
        // One reconciliation at sidebar entry; the resident game owner pushes later changes.
        void refreshPolicyGameStrict().catch(() => {});
        refreshGameTrainerRecovery();
        let observedGame=gameMirrorIdentity(getPolicyGame());
        stops.push(subscribePolicyGameStatus(()=>{
          if(!observing)return;
          const next=gameMirrorIdentity(getPolicyGame());if(next===observedGame)return;
          observedGame=next;
          retireGameSteamDisplay();warmGameSteam();relay.invalidateFresh(true,'game');
        }));
        // Readiness can recover without changing the target identity. Repaint
        // the existing relay once per edge; the shared detector owns retries.
        let observedReadiness=isGameStatusCurrent();
        stops.push(subscribeGameTargetReadiness(()=>{
          if(!observing)return;
          const next=isGameStatusCurrent();if(next===observedReadiness)return;
          observedReadiness=next;retireGameSteamDisplay();if(next===true)warmGameSteam();relay.invalidateFresh(true,'game');
        }));
        stops.push(watch(() => [fanFeatureEnabled.value,fanControlActive.value,getFanSelectedPreset(),powerSourceMode.value] as const,([,,,side],[,,,previousSide])=>{
          if(!observing)return;
          refreshFan();
          // AC/DC selects the frame settings independently of the fan view.
          if(side!==previousSide)invalidate('power');
        }));
        stops.push(on('input.shortcutRuntime',()=>invalidate('input')));
        stops.push(onInputSettingsChanged(()=>invalidate('input')));
        // Pending intent only invalidates cached UI; it grants no sleep/Ready permission.
        stops.push(on('power.pending',()=>{if(!observing)return;audioObservation=null;retireGameSteamDisplay();relay.invalidateFresh(true,'power');}));
        let observedPower='',gameSteamReadyGeneration:number|null=null;
        stops.push(onSharedPowerLifecycle(snapshot=>{
          if(!observing||ended)return;
          const next=powerSnapshotSource(snapshot);
          if(next===observedPower)return;
          observedPower=next;
          const ready=isSharedPowerLifecycleAdmitted(snapshot);
          if(!ready||gameSteamDisplayScope?.view&&gameSteamDisplayScope.view.generation!==snapshot.generation)retireGameSteamDisplay();
          const warm=ready&&gameSteamReadyGeneration!==snapshot.generation;
          gameSteamReadyGeneration=ready?snapshot.generation:null;
          if(warm)warmGameSteam();
          const reading=snapshotReadLease;
          if(reading&&reading.powerSource===null){reading.powerObservation=next;return;}
          // The same current native fact can arrive through the shared owner
          // after our direct read. It needs no second complete control batch.
          if(next===observedPowerSource)return;
          relay.invalidateFresh(true,'power');
        }));
        stops.push(on('fan.resume-ready',refreshFan));
        stops.push(onFanMirrorUiGate(refreshFan));
        stops.push(onFanMirrorDisplay(refreshFan));
      } catch (error) { stopObservers(); throw error; }
      return () => { snapshotReadLease=null;stopObservers(); };
    },
    execute(command,args,context) {
      // Game policy actions recheck target freshness after awaits. Global input
      // does not acquire a game target, including legacy game-addressed controls.
      const globalInput=command==='global.setField'||command==='game.setField'&&['padPersona','gyroEnabled','gyroPreset'].includes(String(args.field));
      if(!globalInput&&(command==='game.setField'||command==='performance.setControls'||command==='frame.setField')){
        const checkpoint=context.checkpoint;
        context={...context,checkpoint:()=>{checkpoint();if(!isGameStatusCurrent())throw Error('GAME_MIRROR_TARGET_PENDING');}};
        try{context.checkpoint();}catch(error){return Promise.reject(error);}
      }
      if(command === 'game.setField' && args.field === 'profile')return profileActions.execute(args,context);
      if(command === 'performance.setControls')return performanceActions.execute(args,context);
      if(command === 'audioOptimization.setEnabled')return (async()=>{
        if(Object.keys(args).length!==1||typeof args.enabled!=='boolean')throw Error('FXSOUND_INVALID_REQUEST');
        context.checkpoint();if(!hostReady||ended)throw Error('FXSOUND_NOT_READY');
        const power=await powerLifecycle.get();context.checkpoint();
        if(power.phase!=='ready'||!power.hardwareWritesAllowed||power.generation!==context.generation)throw Error('FXSOUND_POWER_NOT_READY');
        const current=await fxSound.read();context.checkpoint();
        if(JSON.stringify(current)!==context.controlAdmission?.audioOptimization)throw Error('FXSOUND_SOURCE_CHANGED');
        // Bind this user intent to its admitted generation inside the original
        // native transaction; a read/IPC wait cannot move it across a wake.
        const result=await fxSound.setEnabled(args.enabled,context.generation);
        return {saved:result.ok,applied:result.ok,pending:!result.ok,notice:result.reason};
      })();
      if(command === 'monitor.setEnabled')return monitorActions.execute(args,context);
      if(command === 'global.setField')return globalActions.execute(args,context);
      if(command === 'frame.setField')return frameActions.execute(args,context);
      if(command === 'touchpad.setField')return touchpadActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'trainer')return trainerActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'speedFactor') return speedActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'losslessScaling') {losslessObservation.refresh();return setLosslessFromMirror(args,context,losslessDiagnostic).finally(()=>losslessObservation.refresh());}
      if(command === 'game.setField' && ['padPersona','gyroEnabled','gyroPreset'].includes(String(args.field)))return globalActions.execute(args,context);
      if(command === 'game.setField')throw Error('GAME_MIRROR_INPUT_GLOBAL');
      if(command === 'fan.setPreset' && !fanControlActive.value)return Promise.reject(new Error('FAN_MIRROR_CONTROL_DISABLED'));
      if(command !== 'fan.setEnabled' && command !== 'fan.setPreset' && command !== 'fan.setChoice') return Promise.reject(new Error('MIRROR_UNKNOWN_COMMAND'));
      return fanActions.request(command,args,context);
    },
    reply: (connection,message) => invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000}),
  });
  let transportObservationEpoch=0;
  const stopRequests = on<MirrorRequest>('deckySidebar.mirrorRequest',event => {
    if(ended||!relay.observeAuthenticatedRequestPeer(event))return;
    ++transportObservationEpoch;
    observeDeckyMirrorRequestPeer(event);
    if(!dispatchDeckyMirrorRequest(event))void relay.request(event);
  });
  const stopPeers = on<MirrorPeer>('deckySidebar.mirrorPeer',event => { if(!relay.connected(event))return;++transportObservationEpoch;dispatchDeckyMirrorPeer(event); });
  // Reattach transport as soon as this renderer registers. Main-app restoration
  // still owns ready(); before that only an explicit initializing snapshot is sent.
  const attach = () => {
      // This is an observation of one native instant. A newer native peer edge
      // or attach request retires its peer state before relay/extension changes.
      // A bridge-local owner confirmation may still establish provenance while
      // unknown; it cannot replace a newer confirmed owner or revive old demand.
      const observationEpoch=++transportObservationEpoch;
      void invoke<MirrorPeer>('deckySidebar.mirrorAttach',{}, {timeoutMs:1500}).then(peer => {
        if(ended)return;
        const owner = peer as MirrorPeer & { owner?: string; pid?: number };
        if (owner.owner === 'YMCC-native' && Number.isSafeInteger(owner.pid) && owner.pid! > 0 &&
            (observationEpoch===transportObservationEpoch||provenance.kind!=='ymcc-native')) {
          if(provenance.kind!=='ymcc-native'||provenance.pid!==owner.pid){provenance = { kind: 'ymcc-native', pid: owner.pid }; relay.invalidate();}
        }
        if(observationEpoch!==transportObservationEpoch)return;
        if (peer.connected && relay.connected(peer)) {
          dispatchDeckyMirrorPeer(peer); relay.invalidate();
          void relay.refreshSnapshotAfterAttach(peer);
        }
      }).catch(() => {});
  };
  startupPhase('registered');
  attach();
  return {
    startupPhase,
    ready() {if(ended)return;if(!hostReady){startupPhase('ready');hostReady=true;updaterHost.ready();relay.invalidateFresh(true);}attach();},
    stop() { ended = true;gameSteamDisplayVisible=false;retireGameSteamDisplay(); updaterHost.stop(); trainerActions.stop(); losslessObservation.dispose(); losslessProgramObservation.dispose(); fanActions.stop(); stopRequests(); stopPeers(); relay.dispose(); },
  };
}
