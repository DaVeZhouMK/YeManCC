import {fxSound} from './fxSound';
import {startDeckyUpdaterHost} from './deckyUpdaterHost';
import {createProfileMirrorActions,gameProfileRevision,gameProfileImportSource} from './deckyProfileActions';
import {createPerformanceMirrorActions,performanceMirrorSource,performanceMirrorView} from './deckyPerformanceActions';
import { createTrainerMirrorActions, trainerMirrorSource } from './deckyTrainerActions';
import { refreshGameTrainerRecovery } from './gameTrainerRecovery';
import { getReadyGameTrainer, launchReadyGameTrainer, subscribeGameTrainerState } from './gameTrainer';
import { DeckyLosslessObservation, deckyLosslessText, type DeckyLosslessDiagnostic } from './deckyLosslessState';
import { dispatchDeckyMirrorPeer, dispatchDeckyMirrorRequest } from './deckyMirrorExtensions';
import { createMonitorMirrorActions } from './deckyMonitorActions';
import { readMonitorOutputPreferences, onMonitorOutputPreferencesChanged, monitorOutputCoordinator } from './monitorOutput';
// YMCC Decky sidebar snapshot: existing stores/owners only, no new configuration.
import { compareAndSwapInputSettings, onInputSettingsChanged, readSettingsSection, saveSettingsSection } from './settingsRepository';
import { loadGlobalFrameRates, saveGlobalFrameRate, applyIndependentFrameRates, onFrameRatesChanged, dedicatedFrameRatePair, type FrameRatePair } from './frameRateLimits';
import { createFrameMirrorActions, type FrameMirrorRead } from './deckyFrameActions';
import { createTouchpadMirrorActions, touchpadMirrorFields, touchpadMirrorSource, type TouchpadMirrorRead } from './deckyTouchpadActions';
import { screenTouchpadsGet, screenTouchpadsSet, screenTouchpadProfile } from './screenTouchpads';
import { watch } from 'vue';
import { createGlobalMirrorActions, globalMirrorRead } from './deckyGlobalActions';
import { currentVirtualTargetPolicy } from './virtualTargetPolicyState';
import { virtualTargetOptions, virtualTargetSelectionError } from './virtualTargetPolicy';
import { DECKY_PAD_CHOICES, DECKY_GYRO_CHOICES } from './deckyInputOptions';
import { getGameInputOverrideState, effectiveInputPersona } from './gameInputOverride';
import { VirtualStartProtectionView } from './virtualStartProtectionView';
import { acceptVirtualOutputRuntime, virtualOutputPresentation, type VirtualOutputRuntimeState, type ExternalPriorityRuntimeState } from './virtualOutputStatus';
import { invoke, on, getLastGamepadState } from './ipc';
import { powerLifecycle } from './api';
import { powerSourceMode, refreshPowerSourceSnapshot } from './powerSource';
import { readLosslessScaling, losslessField, setLosslessFromMirror, onLosslessScalingChanged, type LsDocument } from './losslessScaling';
import {retainLosslessObservation,readLosslessProgram} from './losslessRuntimeState';
import { getPolicyGame, gamePolicyKey, subscribePolicyGameStatus, refreshPolicyGameStrict } from './gamePolicyTarget';
import { getGameCustomConfig, loadGameCustomConfig, saveGameCustomConfig, loadPerformanceSchedule, onGameCustomConfigChanged, onPerformanceScheduleChanged,
  getContinuousPerformanceSnapshot, updateContinuousPerformanceControls, importGameCustomProfile, removeGameCustomProfile, restorePerformanceScheduleIfConfigured, type PerformanceScheduleConfig } from './performanceSchedule';
import { fanFeatureEnabled, fanControlActive, getFanSelectedPreset, getFanPresetCurve, saveFanCurve,
  setFanControlActive, setFanNavigationDuty, recordFanHandshake, FAN_IMPORT_ENABLED, FAN_FORCE_PREVIEW } from './fanFeature';
import { createFanMirrorActions } from './deckyFanActions';
import { createGameMirrorActions, makeGameMirrorEntry, makeGameMirrorAdmission, gameMirrorIdentity, readGameMirrorGyro, needsGameMirrorInputMemory } from './deckyGameActions';
import { createSpeedMirrorActions } from './deckySpeedActions';
import { SPEED_PRESETS, applyGameSpeed, clearGameSpeed, getGameSpeedState, isMinecraftTarget, onGameSpeedStateChanged } from './speedhack';
import { readGameMirrorGyroDefaults } from './deckyGyroDefaults';
import { detectedGameName, isGameStatusCurrent, subscribeGameTargetReadiness } from './gamedetect';
import { tryAcquireQuickAction, isQuickActionBusy } from './quickActionLock';
import { closeJoyxoffIfRunning } from './gameproc';
import { observeFanMirrorDisplay, getFanMirrorDisplay, onFanMirrorDisplay, isFanMirrorUiBusy, onFanMirrorUiGate } from './deckyFanDisplay';
import { fanHostLifecycle } from './fanHost';
import { ReadOnlyMirrorRelay, type MirrorPeer, type MirrorRequest } from './deckyMirrorRelay';
import type { SteamRunningObservation } from '../../decky-plugin/src/steamRunningContext';
import type { Choice, MirrorField, MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';

const pad: Choice[] = DECKY_PAD_CHOICES;
const gyro: Choice[] = [{data:'follow',label:'遵循全局',disabled:true},{data:'off',label:'陀螺仪关闭'},...DECKY_GYRO_CHOICES];
const fan: Choice[] = [{data:'soft',label:'轻柔转速'},{data:'balanced',label:'均衡转速'},{data:'aggressive',label:'暴力转速'}];
function field(value: string, choices: Choice[], supported = true): MirrorField { return { value, choices: structuredClone(choices), supported }; }

export function startDeckyMirrorHost(): { ready: () => void; stop: () => void } {
  let hostReady = false;
  let provenance: NonNullable<MirrorSnapshot['provenance']> = { kind: 'unconfirmed' };
  let schedule: PerformanceScheduleConfig | null = null;
  let ended = false;
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
      if(gameTargetReady() && power.phase==='ready' && power.hardwareWritesAllowed && power.generation===context.generation && gameMirrorIdentity(getPolicyGame())===(context.gameIdentity??'')){
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
  const gameActions = createGameMirrorActions({
    featureAllowed: () => gameTargetReady() && !controllerLocked(), game: getPolicyGame, label: gameLabel,
    power: () => powerLifecycle.get(),
    acquire: tryAcquireQuickAction, load: loadGameCustomConfig, current: getGameCustomConfig,
    // A fresh original load can detect unseen presets. Update only our display
    // cache so the existing post-action refresh shows them without another IO.
    schedule: async () => {const value=await loadPerformanceSchedule();schedule=value;return value;},
    currentSchedule: () => schedule, rememberWholeInput:true, finishInput, gyroDefaults:readGameMirrorGyroDefaults, save: saveGameCustomConfig, closeJoyxoff: closeJoyxoffIfRunning,
  });
  const speedActions = createSpeedMirrorActions({featureAllowed:gameTargetReady,game:getPolicyGame,power:() => powerLifecycle.get(),acquire:tryAcquireQuickAction,presets:SPEED_PRESETS,blocked:isMinecraftTarget,state:getGameSpeedState,apply:(game,factor) => applyGameSpeed(game.pid,factor,game,'user-factor'),clear:game => clearGameSpeed(game.pid,'user-reset')});
  const trainerActions = createTrainerMirrorActions({allowed:gameTargetReady,game:getPolicyGame,label:gameLabel,
    ready:(name,path)=>getReadyGameTrainer(name,path,getPolicyGame()??undefined),power:()=>powerLifecycle.get(),busy:isQuickActionBusy,invalidate:()=>relay.invalidate(),
    launch:(game,isTargetCurrent)=>launchReadyGameTrainer(gameLabel(game),game.path,{isTargetCurrent,processTarget:game,interactiveShell:true})});
  const readGlobal=async(cachedSchedule?:PerformanceScheduleConfig,inputRead=()=>readSettingsSection('input'))=>{
    // Mutations reload the original profile file before CAS. Snapshot reads use
    // the one shared profile read below, rather than another IO owner.
    if(!cachedSchedule && getPolicyGame())await loadGameCustomConfig();
    return globalMirrorRead(cachedSchedule??await loadPerformanceSchedule(),await inputRead() as any);
  };
  const globalActions=createGlobalMirrorActions({allowed:()=>hostReady&&!ended,game:getPolicyGame,locked:()=>getGameInputOverrideState().locked||controllerLocked(),contextCurrent:context=>{
    if(!isGameStatusCurrent())return false;
    const game=getPolicyGame();
    if(gameMirrorIdentity(game)!==(context.gameIdentity??''))return false;
    if(!game)return true;
    const config=getGameCustomConfig(),entry=config.entries[gamePolicyKey(game)];
    return (!entry||entry.enabled===false) && !!context.gameAdmission?.profile && gameProfileRevision(config,gamePolicyKey(game))===context.gameAdmission.profile;
  },side:()=>powerSourceMode.value,read:readGlobal,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,cas:compareAndSwapInputSettings,closeJoyxoff:closeJoyxoffIfRunning,finishInput,clearShortcut:()=>invoke('input.shortcutRuntime.clear',{observeProtection:true})});
  let performanceSource='',performanceRevision=0,profileSource='',profileRevision=0;
  const profileActions=createProfileMirrorActions({allowed:gameTargetReady,game:getPolicyGame,label:gameLabel,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,load:loadGameCustomConfig,schedule:loadPerformanceSchedule,currentSchedule:()=>schedule,snapshot:()=>getContinuousPerformanceSnapshot(),gyro:readGameMirrorGyroDefaults,targetPolicy:currentVirtualTargetPolicy,import:importGameCustomProfile,remove:removeGameCustomProfile,restore:()=>restorePerformanceScheduleIfConfigured()});
  const performanceActions=createPerformanceMirrorActions({allowed:gameTargetReady,read:()=>getContinuousPerformanceSnapshot(),update:updateContinuousPerformanceControls,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction});
  const readFrames=async(fresh=true,game=getPolicyGame()):Promise<FrameMirrorRead>=>{
    const global=await loadGlobalFrameRates();
    const config=fresh?await loadGameCustomConfig():getGameCustomConfig();
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
  const losslessObservation=new DeckyLosslessObservation<LsDocument>(()=>relay.invalidate(),losslessDiagnostic);
  const losslessProgramObservation=new DeckyLosslessObservation<Awaited<ReturnType<typeof readLosslessProgram>>>(()=>relay.invalidate(),losslessDiagnostic);
  const relay = new ReadOnlyMirrorRelay({
    diagnostic:(event,detail)=>{void invoke<boolean>('deckySidebar.mirrorDiagnostic',{event,...detail},{timeoutMs:1000}).catch(()=>{});},
    snapshotDemand(){losslessObservation.demand();losslessProgramObservation.demand();relay.invalidate();},
    observeSteam(observation){
      if(JSON.stringify(observation)===JSON.stringify(steam))return false;
      steam=observation;return true;
    },
    async read(trace) {
      const measure:NonNullable<typeof trace>['measure']=trace?.measure??(async(_stage,read)=>read());
      const makeEarly = (notice: string): Omit<MirrorSnapshot,'runId'|'revision'> => ({ generation:0,ready:false,game:null,
        fan:{supported:false,enabled:false,preset:'',choices:structuredClone(fan)},notice });
      if (!hostReady || ended) return makeEarly('YMCC 正在初始化，等待当前状态就绪');
      // Power is one on-demand native read. Game/fan data is already held by the resident YMCC owner.
      const audioOptimization=await fxSound.read();
      const [power] = await Promise.all([measure('power',()=>powerLifecycle.get()),measure('powerSource',refreshPowerSourceSnapshot)]);
      if (!schedule) schedule = await measure('schedule',loadPerformanceSchedule);
      const game = getPolicyGame(),identity=gameMirrorIdentity(game),snapshotSchedule=schedule;
      // Capture the entry only after loading the original file. An external
      // delete has no renderer save event; mixed old-value/new-receipt snapshots
      // would leave the dedicated button stuck until another refresh.
      const profileConfig=game?await loadGameCustomConfig():getGameCustomConfig();
      const profileDocument=JSON.stringify(profileConfig);
      // Independent read-only owners join one snapshot. Share the input read and
      // cached schedule for the global view; mutation paths still load fresh data.
      const input=measure('input',()=>readSettingsSection('input')),inputRead=()=>input;
      const controls=Promise.allSettled([
        measure('frames',()=>readFrames(false,game)),measure('touchpads',()=>readTouchpads(inputRead)),
        measure('global',()=>readGlobal(snapshotSchedule!,inputRead)),
        measure('monitor',readMonitorOutputPreferences),measure('performance',()=>getContinuousPerformanceSnapshot()),
      ] as const);
      // Steam IDs are volatile observations only. EXE remains the original config key,
      // including non-Steam games and the original same-basename sharing behavior.
      const entry = game ? profileConfig.entries[gamePolicyKey(game)] : undefined;
      // The widgets mirror original EDIT values, not a hardware readback.
      // Dormant profiles display global input until the explicit top-level import.
      const activeProfile=!!entry && entry.enabled!==false;
      const draft = game && activeProfile ? makeGameMirrorEntry(entry, schedule!, gameLabel(game)) : null;
      let gyroReadError='';
      let gyroDefaults: Awaited<ReturnType<typeof readGameMirrorGyroDefaults>> | null = null;
      if(game){try{gyroDefaults=await measure('gyro',readGameMirrorGyroDefaults);}catch{gyroReadError='陀螺仪全局方案读取失败，暂不可修改';}}
      const gyroState=gyroDefaults?readGameMirrorGyro(draft??undefined,gyroDefaults):null;
      const gyroSupported=!!gyroState && draft?.padPersona!=='disabled' && (draft?.padPersona!=='follow'||gyroDefaults?.virtualPad!==false);
      const fields: Record<string,MirrorField> = {
        padPersona:field(draft?.padPersona==='follow'||!draft?.padPersona?gyroDefaults?.padPersona??'disabled':draft.padPersona,virtualTargetOptions(pad,currentVirtualTargetPolicy()),!!gyroDefaults||draft?.padPersona!=='follow'),
        gyroEnabled:field(gyroState?.enabled?'on':'off',[{data:'off',label:'关闭'},{data:'on',label:'开启'}],gyroSupported),
        gyroPreset:field(gyroState?.preset??'fps',gyro.filter(option=>['fps','racing','custom','steam'].includes(option.data)),gyroSupported),
        gyroOverride:field(draft?.gyroOverride ?? 'follow',draft?.gyroOverride === 'on' ? [...gyro,{data:'on',label:'陀螺仪开启（旧版独立配置）'}] : gyro,draft?.padPersona !== 'disabled'),
      };
      // Legacy active profiles may still need input memory on an explicit edit.
      const needsExplicitProfileMemory=activeProfile && needsGameMirrorInputMemory(entry);
      fields.padPersona.inherited=needsExplicitProfileMemory;
      const speedState=getGameSpeedState(),ownSpeed=speedState?.pid===game?.pid && speedState?.processCreated===game?.processCreated;
      fields.speedFactor=field(String(ownSpeed && speedState?.enabled ? speedState.factor : 1),SPEED_PRESETS.map(factor=>({data:String(factor),label:`${factor}×`})),!!game && !isMinecraftTarget(game));
      fields.gyroPreset.inherited=needsExplicitProfileMemory;
      const losslessView=game?.path?losslessObservation.view(gameMirrorIdentity(game)+':'+power.generation+':'+power.phase,()=>readLosslessScaling(game.path)):null;
      if(!game)losslessObservation.clear();
      const lossless=losslessView?.document??null,losslessError=losslessView?.notice??'';
      // Available even without a game or a matching EXE rule. No Decky-side probe.
      const programView=losslessProgramObservation.view('program:'+power.generation+':'+power.phase,readLosslessProgram);
      const losslessProgram=programView.pending||programView.notice?{known:false,running:false}:programView.document??{known:false,running:false};
      fields.losslessScaling = losslessField(lossless,losslessView?.pending===true);
      const [frameRead,touchpadRead,globalRead,monitorRead,performanceRead]=await controls;
      const inputValue=await input.catch(()=>null);
      // A concurrent source event also invalidates the relay. This local fence
      // prevents a mixed game/schedule view even before that retry is delivered.
      if(ended || gameMirrorIdentity(getPolicyGame())!==identity || schedule!==snapshotSchedule || game&&JSON.stringify(getGameCustomConfig())!==profileDocument)throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');
      const frames=frameRead.status==='fulfilled'?frameRead.value:null;
      const touchpads=touchpadRead.status==='fulfilled'?touchpadRead.value:null;
      const global=globalRead.status==='fulfilled'?globalRead.value:null;
      const monitor=monitorRead.status==='fulfilled'?monitorRead.value:null;
      const performance=performanceRead.status==='fulfilled'?performanceRead.value:null;
      if(!activeProfile){
        for(const key of ['padPersona','gyroEnabled','gyroPreset'])fields[key]=global?.fields[key]??field('unavailable',[],false);
      }
      const controllerView=observeController(getLastGamepadState());

      const persona=inputValue?effectiveInputPersona(inputValue as any):'disabled';
      const enabled=!!inputValue && persona!=='disabled' && (getGameInputOverrideState().locked
        ? inputValue.gameOverride?.outputTarget?.buttonMappingEnabled??inputValue.outputTarget?.buttonMappingEnabled
        : inputValue.outputTarget?.buttonMappingEnabled)===true;
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
      const freshProfileSource=game?gameProfileRevision(getGameCustomConfig(),gamePolicyKey(game)):'';
      if(freshProfileSource!==profileSource){profileSource=freshProfileSource;profileRevision++;}
      fields.profile={value:entry&&entry.enabled!==false?'dedicated':'global',choices:[{data:'global',label:'全局配置'},{data:'dedicated',label:'游戏专属'}],supported:!!entry&&entry.enabled!==false||!!performance&&!!frames&&!!gyroDefaults,sourceRevision:String(profileRevision),inherited:!entry};
      const freshPerformanceSource=performance?performanceMirrorSource(performance):'';
      if(freshPerformanceSource!==performanceSource){performanceSource=freshPerformanceSource;performanceRevision++;}
      const performanceView=performance?performanceMirrorView(performance,String(performanceRevision)):undefined;
      const controlError=frameRead.status==='rejected'?'独立帧率读取失败，暂不可修改':touchpadRead.status==='rejected'?'全局触摸板读取失败，暂不可修改':globalRead.status==='rejected'?'全局 TDP / 虚拟手柄读取失败':monitorRead.status==='rejected'?'监控总开关读取失败，暂不可修改':performanceRead.status==='rejected'?'性能调度读取失败，暂不可修改':'';
      const readyTrainer = game ? getReadyGameTrainer(gameLabel(game),game.path,game) : null;
      const trainer = trainerActions.view(game,readyTrainer);
      const fanPreset = getFanSelectedPreset(),fanDisplay=getFanMirrorDisplay(),fanEnabled=fanActions.pendingIntent||fanControlActive.value;
      const fanPending=fanDisplay?.active===true&&fanEnabled&&(fanDisplay.pending===true||fanHostLifecycle.recoveryActive)&&!(fanHostLifecycle.controlReady&&fanHostLifecycle.state==='ready'&&!fanHostLifecycle.recoveryActive);
      return { gameAdmission:game ? {...makeGameMirrorAdmission(entry,schedule!,lossless?.xml,gyroDefaults?.source),speed:JSON.stringify(speedState),profile:freshProfileSource,profileSource:performance?gameProfileImportSource(performance):undefined} : undefined,steam:structuredClone(steam),generation:power.generation,ready:power.phase === 'ready' && power.hardwareWritesAllowed === true,
        controlAdmission:{audioOptimization:JSON.stringify(audioOptimization),performance:performance?freshPerformanceSource:undefined,trainer:trainerMirrorSource(readyTrainer),frames:frames?.source,touchpads:touchpads?.source,global:global?.source,monitor:monitor?JSON.stringify(monitor):undefined},
        audioOptimization,losslessProgram,trainer,performance:performanceView,virtualController,
        monitor:monitor?{enabled:monitor.enabled}:undefined,
        global:global?{fields:global.fields}:undefined,
        frames:frames?{pair:frames.pair,dedicated:frames.dedicated}:undefined,
        touchpads:touchpads?{persona:touchpads.persona,fields:touchpads.fields}:undefined,
        actions:{audioOptimization:audioOptimization.known&&audioOptimization.installed,performance:isGameStatusCurrent()&&!!performanceView?.enabled,monitor:!!monitor,global:isGameStatusCurrent()&&!!global&&!game&&!getGameInputOverrideState().locked,frames:isGameStatusCurrent()&&!!frames&&powerSourceMode.value!==null,touchpads:!!touchpads&&touchpads.state.ok&&touchpads.state.available,fan:FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW && !isFanMirrorUiBusy(),game:isGameStatusCurrent()&&!!game && !!gamePolicyKey(game)},
        game:game ? {label:entry?.displayName || gameLabel(game),
          identity:gameMirrorIdentity(game),fields} : null,
        provenance, powerSource:powerSourceMode.value,
        fan:{supported:fanFeatureEnabled.value && fanHostLifecycle.controlReady,enabled:fanEnabled,preset:fanPreset,
          pending:fanPending,notice:fanDisplay?.notice,canToggle:FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW && !isFanMirrorUiBusy(),choices:structuredClone(fan)},
        notice:power.phase !== 'ready' ? '已连接 YMCC · 等待唤醒完成，控制暂不可用' : controlError || gyroReadError || losslessError || (!game ? 'YMCC 当前没有确认游戏' : (!lossless?.controlAvailable?deckyLosslessText(lossless?.reason || ''):'') || (!lossless ? 'LosslessScaling 插帧配置正在读取' : '')) };
    },
    observe(invalidate) {
      // Sidebar demand may resume after edits made while its observers were paused.
      // Refresh this display cache once on resume; resident YMCC owners retain their own data.
      schedule = null;
      const stops: Array<() => void> = [retainLosslessObservation(),fxSound.retain(invalidate)];
      try {
        stops.push(onPerformanceScheduleChanged(value => { schedule = value; invalidate(); }));
        stops.push(onGameCustomConfigChanged(invalidate));
        stops.push(onFrameRatesChanged(invalidate));
        stops.push(onMonitorOutputPreferencesChanged(invalidate));
        stops.push(on('screenTouchpads.updated',invalidate));
        let controllerTimer:ReturnType<typeof setTimeout>|null=null,controllerDeadline:number|null=null;
        // Native samples include timestamps/counters. Only a user-visible status or
        // admission change may invalidate the sidebar, never the telemetry rate.
        const controllerSignature=()=>JSON.stringify([currentVirtualTargetPolicy(),virtualOutputPresentation(controllerRuntime,controllerTarget.enabled,controllerTarget.persona,false,externalPriority),controllerProtection.read(performance.now()).locked]);
        observeController(getLastGamepadState());
        let observedController=controllerSignature();
        const refreshController=(raw:unknown)=>{
          const view=observeController(raw);
          const next=controllerSignature();
          if(next!==observedController){observedController=next;invalidate();}
          if(view.deadline===controllerDeadline)return;
          if(controllerTimer!==null)clearTimeout(controllerTimer);
          controllerDeadline=view.deadline;controllerTimer=null;
          if(view.deadline!==null)controllerTimer=setTimeout(()=>{
            controllerTimer=null;controllerDeadline=null;
            const next=controllerSignature();
            if(next!==observedController){observedController=next;invalidate();}
          },Math.max(0,Math.ceil(view.deadline-performance.now())));
        };
        controllerFeedback=refreshController;
        stops.push(()=>{if(controllerFeedback===refreshController)controllerFeedback=null;if(controllerTimer!==null)clearTimeout(controllerTimer);});
        stops.push(on('gamepad.state',refreshController));
        refreshController(getLastGamepadState());
        stops.push(onLosslessScalingChanged(()=>{losslessObservation.refresh();losslessProgramObservation.refresh();invalidate();}));
        stops.push(onGameSpeedStateChanged(invalidate));
        const trainerSource = () => {
          const current = getPolicyGame();
          return JSON.stringify([gameMirrorIdentity(current),trainerMirrorSource(current ? getReadyGameTrainer(gameLabel(current),current.path,current) : null)]);
        };
        let observedTrainer = trainerSource();
        stops.push(subscribeGameTrainerState(() => {
          const next = trainerSource();
          if(next !== observedTrainer){observedTrainer = next;invalidate();}
        }));
        // One reconciliation at sidebar entry; the resident game owner pushes later changes.
        void refreshPolicyGameStrict().catch(() => {});
        refreshGameTrainerRecovery();
        stops.push(subscribePolicyGameStatus(invalidate));
        // Readiness can recover without changing the target identity. Repaint
        // the existing relay once per edge; the shared detector owns retries.
        stops.push(subscribeGameTargetReadiness(invalidate));
        stops.push(watch(() => [fanFeatureEnabled.value,fanControlActive.value,getFanSelectedPreset(),powerSourceMode.value],invalidate));
        stops.push(on('input.shortcutRuntime',invalidate));
        stops.push(onInputSettingsChanged(invalidate));
        stops.push(on('power.pending',invalidate));
        stops.push(on('power.resumed',invalidate));
        stops.push(on('fan.resume-ready',invalidate));
        stops.push(onFanMirrorUiGate(invalidate));
        stops.push(onFanMirrorDisplay(invalidate));
      } catch (error) { for (const stop of stops) stop(); throw error; }
      return () => { for (const stop of stops) stop(); };
    },
    execute(command,args,context) {
      // A native signal only marks the shared target stale. Each existing action
      // must recheck freshness after awaits, including LosslessScaling's adapter.
      if(command==='game.setField'||command==='global.setField'||command==='performance.setControls'||command==='frame.setField'){
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
        const result=await fxSound.setEnabled(args.enabled);
        return {saved:result.ok,applied:result.ok,pending:!result.ok,notice:result.reason};
      })();
      if(command === 'monitor.setEnabled')return monitorActions.execute(args,context);
      if(command === 'global.setField')return globalActions.execute(args,context);
      if(command === 'frame.setField')return frameActions.execute(args,context);
      if(command === 'touchpad.setField')return touchpadActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'trainer')return trainerActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'speedFactor') return speedActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'losslessScaling') {losslessObservation.refresh();return setLosslessFromMirror(args,context,losslessDiagnostic).finally(()=>losslessObservation.refresh());}
      if(command === 'game.setField' && ['padPersona','gyroEnabled','gyroPreset'].includes(String(args.field))){
        const game=getPolicyGame(),entry=game?getGameCustomConfig().entries[gamePolicyKey(game)]:undefined;
        if(!entry||entry.enabled===false)return globalActions.execute(args,context);
      }
      if(command === 'game.setField') return gameActions.execute(command,args,context);
      if(command === 'fan.setPreset' && !fanControlActive.value)return Promise.reject(new Error('FAN_MIRROR_CONTROL_DISABLED'));
      if(command !== 'fan.setEnabled' && command !== 'fan.setPreset' && command !== 'fan.setChoice') return Promise.reject(new Error('MIRROR_UNKNOWN_COMMAND'));
      return fanActions.request(command,args,context);
    },
    reply: (connection,message) => invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000}),
  });
  const stopRequests = on<MirrorRequest>('deckySidebar.mirrorRequest',event => { if(!dispatchDeckyMirrorRequest(event))void relay.request(event); });
  const stopPeers = on<MirrorPeer>('deckySidebar.mirrorPeer',event => { relay.connected(event); dispatchDeckyMirrorPeer(event); });
  // Reattach transport as soon as this renderer registers. Main-app restoration
  // still owns ready(); before that only an explicit initializing snapshot is sent.
  const attach = () => {
      void invoke<MirrorPeer>('deckySidebar.mirrorAttach',{}, {timeoutMs:1500}).then(peer => {
        const owner = peer as MirrorPeer & { owner?: string; pid?: number };
        if (!ended && owner.owner === 'YMCC-native' && Number.isSafeInteger(owner.pid) && owner.pid! > 0) {
          provenance = { kind: 'ymcc-native', pid: owner.pid }; relay.invalidate();
        }
        if (!ended && peer.connected) {
          relay.connected(peer); dispatchDeckyMirrorPeer(peer); relay.invalidate();
          void relay.refreshSnapshotAfterAttach(peer);
        }
      }).catch(() => {});
  };
  attach();
  return {
    ready() {hostReady = true; updaterHost.ready(); relay.invalidate();attach();},
    stop() { ended = true; updaterHost.stop(); trainerActions.stop(); losslessObservation.dispose(); losslessProgramObservation.dispose(); fanActions.stop(); stopRequests(); stopPeers(); relay.dispose(); },
  };
}
