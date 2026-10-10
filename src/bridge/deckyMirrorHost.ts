// YMCC Decky sidebar snapshot: existing stores/owners only, no new configuration.
import { compareAndSwapInputSettings, onInputSettingsChanged, readSettingsSection } from './settingsRepository';
import { loadGlobalFrameRates, saveGlobalFrameRate, applyIndependentFrameRates, onFrameRatesChanged, dedicatedFrameRatePair, type FrameRatePair } from './frameRateLimits';
import { createFrameMirrorActions, type FrameMirrorRead } from './deckyFrameActions';
import { createTouchpadMirrorActions, touchpadMirrorFields, touchpadMirrorSource, type TouchpadMirrorRead } from './deckyTouchpadActions';
import { screenTouchpadsGet, screenTouchpadsSet, screenTouchpadProfile } from './screenTouchpads';
import { watch } from 'vue';
import { createGlobalMirrorActions, globalMirrorRead } from './deckyGlobalActions';
import { DECKY_PAD_CHOICES } from './deckyInputOptions';
import { getGameInputOverrideState } from './gameInputOverride';
import { invoke, on } from './ipc';
import { powerLifecycle } from './api';
import { powerSourceMode, refreshPowerSourceSnapshot } from './powerSource';
import { readLosslessScaling, losslessField, setLosslessFromMirror, onLosslessScalingChanged, type LsDocument } from './losslessScaling';
import { getPolicyGame, gamePolicyKey, subscribePolicyGameStatus } from './gamePolicyTarget';
import { getGameCustomConfig, loadGameCustomConfig, saveGameCustomConfig, loadPerformanceSchedule, resolveGameCustomProfiles, onGameCustomConfigChanged, onPerformanceScheduleChanged,
  applyPerformanceSchedule, type PerformanceScheduleConfig, type ScheduleMode, type ScheduleProfile } from './performanceSchedule';
import { detectGameCorePolicy, type GameCorePolicyCapabilities } from './gameCorePolicy';
import { fanFeatureEnabled, fanControlActive, getFanFeatureSettings, getFanPresetCurve, saveFanCurve,
  setFanControlActive, setFanNavigationDuty, recordFanHandshake, FAN_IMPORT_ENABLED, FAN_FORCE_PREVIEW } from './fanFeature';
import { createFanMirrorActions } from './deckyFanActions';
import { createGameMirrorActions, makeGameMirrorEntry, makeGameMirrorAdmission, gameMirrorIdentity, readGameMirrorGyro, needsGameMirrorInputMemory } from './deckyGameActions';
import { createSpeedMirrorActions } from './deckySpeedActions';
import { SPEED_PRESETS, applyGameSpeed, clearGameSpeed, getGameSpeedState, isMinecraftTarget, onGameSpeedStateChanged } from './speedhack';
import { readGameMirrorGyroDefaults } from './deckyGyroDefaults';
import { detectedGameName } from './gamedetect';
import { tryAcquireQuickAction } from './quickActionLock';
import { closeJoyxoffIfRunning } from './gameproc';
import { observeFanMirrorDisplay, getFanMirrorDisplay, onFanMirrorDisplay, isFanMirrorUiBusy, onFanMirrorUiGate } from './deckyFanDisplay';
import { fanHostLifecycle } from './fanHost';
import { ReadOnlyMirrorRelay, type MirrorPeer, type MirrorRequest } from './deckyMirrorRelay';
import type { SteamRunningObservation } from '../../decky-plugin/src/steamRunningContext';
import type { Choice, MirrorField, MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';

const labels: Record<ScheduleMode,string> = { eco:'节能',balanced:'平衡',medium:'中等',performance:'高性能',elite:'精睿',extreme:'极致性能' };
const core: Choice[] = [{data:'big-small',label:'大核为主'},{data:'only-big',label:'仅大核'},{data:'all',label:'全部核心'},
  {data:'default',label:'Windows 默认'},{data:'small-super-small',label:'小核＋超小核'},{data:'only-small',label:'仅小核'}];
const hyper: Choice[] = [{data:'default',label:'Windows 默认'},{data:'on',label:'开启'},{data:'off',label:'关闭'}];
const pad: Choice[] = DECKY_PAD_CHOICES;
const gyro: Choice[] = [{data:'follow',label:'遵循全局',disabled:true},{data:'off',label:'陀螺仪关闭'},{data:'fps',label:'FPS射击'},{data:'racing',label:'赛车'},{data:'custom',label:'自定义'},{data:'steam',label:'Steam'}];
const fan: Choice[] = [{data:'soft',label:'轻柔转速'},{data:'balanced',label:'均衡转速'},{data:'aggressive',label:'暴力转速'}];
function field(value: string, choices: Choice[], supported = true): MirrorField { return { value, choices: structuredClone(choices), supported }; }

export function startDeckyMirrorHost(): { ready: () => void; stop: () => void } {
  let hostReady = false;
  let provenance: NonNullable<MirrorSnapshot['provenance']> = { kind: 'unconfirmed' };
  let schedule: PerformanceScheduleConfig | null = null;
  let topology: GameCorePolicyCapabilities | null = null;
  let topologyGeneration = -1;
  let ended = false;
  let steam:SteamRunningObservation = {availability:'unavailable'};
  const fanActions = createFanMirrorActions({
    owner:fanHostLifecycle,enabled:() => fanControlActive.value,currentPreset:() => getFanFeatureSettings().preset,
    curve:getFanPresetCurve,save:saveFanCurve,updateActive:setFanControlActive,updateDuty:setFanNavigationDuty,
    power:() => powerLifecycle.get(),featureAllowed:() => hostReady && !ended && FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW,
    uiBusy:isFanMirrorUiBusy,display:observeFanMirrorDisplay,observeCapability:allowed => recordFanHandshake(allowed),
  });
  const gameLabel = (game: NonNullable<ReturnType<typeof getPolicyGame>>) => detectedGameName(game) || game.name || gamePolicyKey(game) || '当前游戏';
  const gameActions = createGameMirrorActions({
    featureAllowed: () => hostReady && !ended, game: getPolicyGame, label: gameLabel,
    power: () => powerLifecycle.get(), capabilities: () => topology,
    acquire: tryAcquireQuickAction, load: loadGameCustomConfig, current: getGameCustomConfig,
    // A fresh original load can detect unseen presets. Update only our display
    // cache so the existing post-action refresh shows them without another IO.
    schedule: async () => {const value=await loadPerformanceSchedule();schedule=value;return value;},
    currentSchedule: () => schedule, rememberWholeInput:true, gyroDefaults:readGameMirrorGyroDefaults, save: saveGameCustomConfig, closeJoyxoff: closeJoyxoffIfRunning,
  });
  const speedActions = createSpeedMirrorActions({featureAllowed:() => hostReady && !ended,game:getPolicyGame,power:() => powerLifecycle.get(),acquire:tryAcquireQuickAction,presets:SPEED_PRESETS,blocked:isMinecraftTarget,state:getGameSpeedState,apply:(game,factor) => applyGameSpeed(game.pid,factor,game,'user-factor'),clear:game => clearGameSpeed(game.pid,'user-reset')});
  const readGlobal=async()=>globalMirrorRead(await loadPerformanceSchedule(),await readSettingsSection('input') as any);
  const globalActions=createGlobalMirrorActions({allowed:()=>hostReady&&!ended,game:getPolicyGame,locked:()=>getGameInputOverrideState().locked,side:()=>powerSourceMode.value,read:readGlobal,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,apply:applyPerformanceSchedule,cas:compareAndSwapInputSettings,closeJoyxoff:closeJoyxoffIfRunning,clearShortcut:()=>invoke('input.shortcutRuntime.clear',{})});
  const readFrames=async(fresh=true):Promise<FrameMirrorRead>=>{
    const game=getPolicyGame(),global=await loadGlobalFrameRates();
    const config=fresh?await loadGameCustomConfig():getGameCustomConfig();
    const currentSchedule=fresh&&game?await loadPerformanceSchedule():schedule;
    const key=game?gamePolicyKey(game):'',entry=config.entries[key];
    const dedicated=!!entry&&entry.enabled!==false&&config.rtss[key]?.enabled!==false;
    const pair=dedicated?dedicatedFrameRatePair(config.rtss[key],global):global;
    return {game,global,pair,config,schedule:currentSchedule,dedicated,
      source:JSON.stringify({identity:gameMirrorIdentity(game),global,config,schedule:currentSchedule})};
  };
  const readTouchpads=async():Promise<TouchpadMirrorRead>=>{
    const input=await readSettingsSection('input'),target=input.outputTarget||{};
    const persona=typeof target.persona==='string'?target.persona:'disabled';
    const virtualEnabled=target.buttonMappingEnabled===true;
    const state=await screenTouchpadsGet(screenTouchpadProfile(persona));
    return {persona,virtualEnabled,state,source:touchpadMirrorSource(persona,virtualEnabled,state),fields:touchpadMirrorFields(persona,virtualEnabled,state)};
  };
  const frameActions=createFrameMirrorActions({allowed:()=>hostReady&&!ended,game:getPolicyGame,side:()=>powerSourceMode.value,power:()=>powerLifecycle.get(),
    acquire:tryAcquireQuickAction,read:()=>readFrames(),key:gamePolicyKey,label:gameLabel,gyro:readGameMirrorGyroDefaults,
    save:saveGameCustomConfig,saveGlobal:saveGlobalFrameRate,apply:applyIndependentFrameRates});
  const touchpadActions=createTouchpadMirrorActions({allowed:()=>hostReady&&!ended,read:readTouchpads,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,set:screenTouchpadsSet});
  const relay = new ReadOnlyMirrorRelay({
    observeSteam(observation){
      if(JSON.stringify(observation)===JSON.stringify(steam))return false;
      steam=observation;return true;
    },
    async read() {
      const makeEarly = (notice: string): Omit<MirrorSnapshot,'runId'|'revision'> => ({ generation:0,ready:false,game:null,
        fan:{supported:false,enabled:false,preset:'',choices:structuredClone(fan)},notice });
      if (!hostReady || ended) return makeEarly('YMCC 正在初始化；不会为侧栏提前启动业务');
      // Power is one on-demand native read. Game/fan data is already held by the resident YMCC owner.
      const power = await powerLifecycle.get();
      await refreshPowerSourceSnapshot();
      if (!schedule) schedule = await loadPerformanceSchedule();
      const game = getPolicyGame();
      if (game && topologyGeneration !== power.generation) {
        // One probe per observed power generation; failed/unknown probe clears
        // previous support rather than granting permission from stale topology.
        topology = await detectGameCorePolicy(); topologyGeneration = power.generation;
      }
      // Steam IDs are volatile observations only. EXE remains the original config key,
      // including non-Steam games and the original same-basename sharing behavior.
      const entry = game ? getGameCustomConfig().entries[gamePolicyKey(game)] : undefined;
      // The widgets mirror original EDIT values, not a hardware readback.
      // Dormant records remain dormant until an explicit edit; new defaults are
      // shown BEFORE the edit that enables the original dedicated record.
      const selected = { ac:entry?.acMode ?? schedule!.active.ac, dc:entry?.dcMode ?? schedule!.active.dc };
      const draft = game ? makeGameMirrorEntry(entry,schedule!,selected,gameLabel(game)) : null;
      const resolved = entry ? resolveGameCustomProfiles(entry,schedule!) : null;
      const detail = (profile?: ScheduleProfile) => profile ? `${profile.tdpMax}W` : '';
      const modes = (side: 'ac' | 'dc') => {
        const current = schedule!.active[side];
        // follow is only a display sentinel, never a persisted AC/DC mode.
        const choices = [...(!entry ? [{data:'follow',label:`全局方案预设（${labels[current]} · ${detail(schedule!.profiles[side][current])}）`,disabled:true}] : []),...Object.keys(labels).map(data => {
          const profile = schedule!.profiles[side][data as ScheduleMode];
          return {data,label:`${labels[data as ScheduleMode]} · ${detail(profile)}`};
        })];
        const value = entry?.[side === 'ac' ? 'acMode' : 'dcMode'];
        // Legacy snapshots cannot be mislabeled as dynamic global inheritance.
        if (entry && !value) choices.push({data:'legacy',disabled:true,label:`既有专属快照（旧档）${resolved?.[side] ? ' · '+detail(resolved[side]) : ''}`});
        return field(value ?? (entry ? 'legacy' : 'follow'),choices);
      };
      let gyroReadError='';
      let gyroDefaults: Awaited<ReturnType<typeof readGameMirrorGyroDefaults>> | null = null;
      if(game){try{gyroDefaults=await readGameMirrorGyroDefaults();}catch{gyroReadError='陀螺仪全局方案读取失败，暂不可修改';}}
      const gyroState=gyroDefaults?readGameMirrorGyro(draft??undefined,gyroDefaults):null;
      const gyroSupported=!!gyroState && draft?.padPersona!=='disabled' && (draft?.padPersona!=='follow'||gyroDefaults?.virtualPad!==false);
      const fields: Record<string,MirrorField> = {
        acMode:modes('ac'),dcMode:modes('dc'),
        corePolicyMode:field(draft?.corePolicyMode ?? 'default',core,topology?.heterogeneous === true),
        hyperThreadPolicy:field(draft?.hyperThreadPolicy ?? 'default',hyper,topology?.smtAvailable === true),
        padPersona:field(draft?.padPersona==='follow'||!draft?.padPersona?gyroDefaults?.padPersona??'disabled':draft.padPersona,pad,!!gyroDefaults||draft?.padPersona!=='follow'),
        gyroEnabled:field(gyroState?.enabled?'on':'off',[{data:'off',label:'关闭'},{data:'on',label:'开启'}],gyroSupported),
        gyroPreset:field(gyroState?.preset??'fps',gyro.filter(option=>['fps','racing','custom','steam'].includes(option.data)),gyroSupported),
        gyroOverride:field(draft?.gyroOverride ?? 'follow',draft?.gyroOverride === 'on' ? [...gyro,{data:'on',label:'陀螺仪开启（旧档）'}] : gyro,draft?.padPersona !== 'disabled'),
      };
      // Selecting the displayed default or a dormant/partly inherited value is
      // an explicit whole-profile activation, not an already-remembered no-op.
      const needsExplicitProfileMemory=!entry || entry.enabled!==true || needsGameMirrorInputMemory(entry);
      fields.corePolicyMode.inherited=needsExplicitProfileMemory;
      fields.padPersona.inherited=needsExplicitProfileMemory;
      const speedState=getGameSpeedState(),ownSpeed=speedState?.pid===game?.pid && speedState?.processCreated===game?.processCreated;
      fields.speedFactor=field(String(ownSpeed && speedState?.enabled ? speedState.factor : 1),SPEED_PRESETS.map(factor=>({data:String(factor),label:`${factor}×`})),!!game && !isMinecraftTarget(game));
      fields.gyroPreset.inherited=needsExplicitProfileMemory;
      let lossless: LsDocument | null = null;
      let losslessError = '';
      if (game?.path) { try { lossless = await readLosslessScaling(game.path); } catch (error) { losslessError = String((error as Error).message); } }
      fields.losslessScaling = losslessField(lossless);
      let frames:FrameMirrorRead|null=null,touchpads:TouchpadMirrorRead|null=null,controlError='';
      try{frames=await readFrames(false);}catch{controlError='独立帧率读取失败，暂不可修改';}
      try{touchpads=await readTouchpads();}catch{controlError=controlError||'全局触摸板读取失败，暂不可修改';}
      let global:Awaited<ReturnType<typeof readGlobal>>|null=null;if(!game){try{global=await readGlobal();}catch{controlError=controlError||'全局 TDP / 虚拟手柄读取失败';}}
      const settings = getFanFeatureSettings(),fanDisplay=getFanMirrorDisplay(),fanEnabled=fanActions.pendingIntent||fanControlActive.value;
      const fanPending=fanDisplay?.active===true&&fanEnabled&&(fanDisplay.pending===true||fanHostLifecycle.recoveryActive)&&!(fanHostLifecycle.controlReady&&fanHostLifecycle.state==='ready'&&!fanHostLifecycle.recoveryActive);
      return { gameAdmission:game ? {...makeGameMirrorAdmission(entry,schedule!,lossless?.xml,gyroDefaults?.source),speed:JSON.stringify(speedState)} : undefined,steam:structuredClone(steam),generation:power.generation,ready:power.phase === 'ready' && power.hardwareWritesAllowed === true,
        controlAdmission:{frames:frames?.source,touchpads:touchpads?.source,global:global?.source},
        global:global?{fields:global.fields}:undefined,
        frames:frames?{pair:frames.pair,dedicated:frames.dedicated}:undefined,
        touchpads:touchpads?{persona:touchpads.persona,fields:touchpads.fields}:undefined,
        actions:{global:!!global&&!game&&!getGameInputOverrideState().locked,frames:!!frames&&powerSourceMode.value!==null,touchpads:!!touchpads&&touchpads.state.ok&&touchpads.state.available,fan:FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW && !isFanMirrorUiBusy(),game:!!game && !!gamePolicyKey(game)},
        game:game ? {label:entry?.displayName || gameLabel(game),
          identity:gameMirrorIdentity(game),fields} : null,
        provenance, powerSource:powerSourceMode.value,
        fan:{supported:fanFeatureEnabled.value && fanHostLifecycle.controlReady,enabled:fanEnabled,preset:settings.preset,
          pending:fanPending,notice:fanDisplay?.notice,canToggle:FAN_IMPORT_ENABLED && !FAN_FORCE_PREVIEW && !isFanMirrorUiBusy(),choices:structuredClone(fan)},
        notice:power.phase !== 'ready' ? 'YMCC 电源恢复中；不可下发' : controlError || gyroReadError || losslessError || (!game ? 'YMCC 当前没有确认游戏' : lossless?.reason || (!lossless?.profile ? 'LosslessScaling 插帧列表待读取' : '')) };
    },
    observe(invalidate) {
      const stops: Array<() => void> = [];
      try {
        stops.push(onPerformanceScheduleChanged(value => { schedule = value; invalidate(); }));
        stops.push(onGameCustomConfigChanged(invalidate));
        stops.push(onFrameRatesChanged(invalidate));
        stops.push(on('screenTouchpads.updated',invalidate));
        stops.push(onLosslessScalingChanged(invalidate));
        stops.push(onGameSpeedStateChanged(invalidate));
        stops.push(subscribePolicyGameStatus(invalidate));
        stops.push(watch(() => [fanFeatureEnabled.value,fanControlActive.value,getFanFeatureSettings().preset,powerSourceMode.value],invalidate));
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
      if(command === 'global.setField')return globalActions.execute(args,context);
      if(command === 'frame.setField')return frameActions.execute(args,context);
      if(command === 'touchpad.setField')return touchpadActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'speedFactor') return speedActions.execute(args,context);
      if(command === 'game.setField' && args.field === 'losslessScaling') return setLosslessFromMirror(args,context);
      if(command === 'game.setField') return gameActions.execute(command,args,context);
      if(command === 'fan.setPreset' && !fanControlActive.value)return Promise.reject(new Error('FAN_MIRROR_CONTROL_DISABLED'));
      if(command !== 'fan.setEnabled' && command !== 'fan.setPreset') return Promise.reject(new Error('MIRROR_UNKNOWN_COMMAND'));
      return fanActions.request(command,args,context);
    },
    reply: (connection,message) => invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000}),
  });
  const stopRequests = on<MirrorRequest>('deckySidebar.mirrorRequest',event => { void relay.request(event); });
  const stopPeers = on<MirrorPeer>('deckySidebar.mirrorPeer',event => relay.connected(event));
  return {
    ready() {
      hostReady = true; relay.invalidate();
      // One metadata reattach, not a poll or backend launch. An old native shell fails closed.
      void invoke<MirrorPeer>('deckySidebar.mirrorAttach',{}, {timeoutMs:1500}).then(peer => {
        const owner = peer as MirrorPeer & { owner?: string; pid?: number };
        if (!ended && owner.owner === 'YMCC-native' && Number.isSafeInteger(owner.pid) && owner.pid! > 0) {
          provenance = { kind: 'ymcc-native', pid: owner.pid }; relay.invalidate();
        }
        if (!ended && peer.connected) {
          relay.connected(peer); relay.invalidate();
          void relay.request({ ...peer, request: { command:'snapshot', runId:peer.runId } });
        }
      }).catch(() => {});
    },
    stop() { ended = true; fanActions.stop(); stopRequests(); stopPeers(); relay.dispose(); },
  };
}
