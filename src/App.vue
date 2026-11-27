<script setup lang="ts">
import { ref, computed, provide, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { ymccDesignWidth, ymccStageWidth, YMCC_SHELL_BASE_WIDTH, YMCC_SHELL_BASE_HEIGHT, YMCC_SHELL_SCALE_BOOST } from '@/bridge/monitorLayout';
import { useRouter } from 'vue-router';
import { resetInnerPageViewport } from '@/bridge/pageViewport';
import NavRail from '@/components/NavRail.vue';
import HidHideInstallPrompt from '@/components/HidHideInstallPrompt.vue';
import { useDebugStore } from '@/stores/debug';
import { startGamepad } from '@/gamepad/engine';
import { startControllerShortcutRuntime } from '@/bridge/controllerShortcutRuntime';
import { enqueueGamepadTaskDetached } from '@/gamepad/serial';
import { on, invoke } from '@/bridge/ipc';
import { createNetworkAccelerationWakeRecovery } from '@/bridge/networkAccelerationWake';
const networkAccelerationWake = createNetworkAccelerationWakeRecovery((generation, suspendSerial) =>
  invoke('updateAccel.resume', { generation, suspendSerial }, { timeoutMs: 8000 }));
import { startUndervoltRuntime, stopUndervoltRuntime, undervoltPresentationAllowed } from '@/bridge/undervoltRuntime';
import { applyCpuAutostart } from '@/bridge/autostart';
import { LEGACY_CCD_UNDERVOLT_ACTIVE } from '@/bridge/legacyCpuFeaturePolicy';
import { applyTrayResident } from '@/bridge/trayResident';
import { ensureDesktopModeStartupDefault } from '@/bridge/desktopModeStartup';
import { ensureNetworkAccelerationStartupDefault } from '@/bridge/networkAccelerationStartup';
import { ensureGameBarControllerStartup } from '@/bridge/gameBarControllerStartup';
import { restoreSelectedBootMode, observeBootModeRuntimeReceipts, migrateBootTrayPreference, cancelBootModeLaunches, cancelBootModeDeferredMouse } from '@/bridge/bootModes';
import { getFloatInfo, setAutofloatPowerControlDir, invalidateFloatPowerSource } from '@/bridge/autofloat';
import { app, fs, powerLifecycle, windowApi } from '@/bridge/api';
import { initMusic } from '@/bridge/music';
import { toggleTask, taskExists, detectPowerMode, detectPowerModeReliable, detectPowerModeStable, reconcileRememberedPowerScheme, reconcilePowerSchemeAfterWake, resumeTdpDaemonAfterWake, primeTdpSession, getSleepPowerPlanOptimizationEnabled, optimizeSleepPowerPlans, type WakeSchemeResult } from '@/bridge/yeman';
import TopMonitorBar from '@/components/TopMonitorBar.vue';
import { applyTheme } from '@/bridge/theme';
import { adjustContinuousPerformanceTdp, getPerformanceScheduleOwnership, refreshPerformanceScheduleCoreMode, restorePerformanceScheduleAfterWake, restorePerformanceScheduleIfConfigured, onContinuousPerformanceApplied, SkipApplyError, type WakePerformanceResult } from '@/bridge/performanceSchedule';
import { getMouseModeState, type MouseModeState } from '@/bridge/gameproc';
import { acceptMouseModeState } from '@/bridge/mouseModeState';
import { currentBackgroundHls, loadBackgroundHls, type BackgroundHls } from '@/bridge/backgroundHls';
import {
  backgroundGet,
  getBackgroundBlur,
  getBackgroundOpacity,
  getBackgroundPosition,
  type BackgroundKind,
  type BackgroundState,
  type MediaPlayback,
} from '@/bridge/background';
import {
  cleanGameTitle,
  getDynamicBackgroundConfig,
  isFailedDynamicVariant,
  readDynamicBackgroundOriginal,
  refreshDynamicBackground,
  type DynamicBackgroundFallback,
} from '@/bridge/dynamicBackground';
import { detectGame, getCurrentGame, isCurrentGameTarget, isGameStatusCurrent, subscribeGameStatus, subscribeGameTargetReadiness, type DetectedGame } from '@/bridge/gamedetect';
import { startGamePolicyRuntime, stopGamePolicyRuntime, requestGamePolicyTdpReconciliation } from '@/bridge/gamePolicyRuntime';
import {startLosslessRuntimeOwner} from '@/bridge/losslessRuntimeState';
let stopLosslessRuntimeOwner:(()=>void)|null=null;
import { startGameTrainerAutoPrepare } from '@/bridge/gameTrainerAuto';
let stopGameTrainerAutoPrepare: (() => void) | null = null;
import { applyIndependentFrameRates } from '@/bridge/frameRateLimits';
import { startDeckyMirrorHost } from '@/bridge/deckyMirrorHost';
import { startDeckySafetyNotifications } from '@/bridge/deckySidebar';
let stopDeckySafetyNotifications:(()=>void)|null=null;
import type { startUpdatePredownload } from '@/bridge/updatePredownloadService';
import { startMonitorHudHost } from '@/bridge/monitorHudHost';
import { startRadialTestingHost } from '@/radial/testing';
import {applyGameMediaRange} from '@/bridge/gameMediaPlayback';
import {gameMediaRevisions as sharedMediaRevisions} from '@/bridge/gameMediaRevisionFence';
import {sharedGameMedia,startSharedGameMedia} from '@/bridge/sharedGameMedia';
import {startSteamWallpaperHost} from '@/bridge/steamWallpaperHost';
import {wallpaperPowerBlocked,subscribeWallpaperPowerChanged} from '@/bridge/wallpaperPowerPolicy';
let stopWallpaperPower:(()=>void)|null=null;
const wallpaperPowerPaused=ref(wallpaperPowerBlocked());
function dynamicBackgroundAllowed(){return getDynamicBackgroundConfig().enabled&&!wallpaperPowerPaused.value;}
let stopSharedGameMedia:(()=>void)|null=null;
let steamWallpaperHost:ReturnType<typeof startSteamWallpaperHost>|null=null;
let updatePredownloadHost: ReturnType<typeof startUpdatePredownload> | null = null;
function startBackgroundUpdate(): void {
  if (backgroundPresentationDisposed) return;
  void import('@/bridge/updatePredownloadService').then(({ startUpdatePredownload }) => {
    if (!backgroundPresentationDisposed) updatePredownloadHost = startUpdatePredownload();
  }).catch(error => {
    if (!backgroundPresentationDisposed) console.warn('后台更新服务启动失败：', error);
  });
}
let monitorHudHost: ReturnType<typeof startMonitorHudHost> | null = null;
let stopBootModeReceipts: (() => void) | null = null;
let radialHost: ReturnType<typeof startRadialTestingHost> | null = null;
let deckyMirrorHost: ReturnType<typeof startDeckyMirrorHost> | null = null;
let controllerShortcutRuntime: ReturnType<typeof startControllerShortcutRuntime> | null = null;
import { startGameProfileAutoMemory, stopGameProfileAutoMemory } from '@/bridge/gameProfileAutoMemory';
import { BOOT_CONTROL_CENTER_TASK, bootMirrorExists, ensureRtssOverlayInstalled, setPowerControlDir, toggleBootMirror } from '@/bridge/yeman';
import { getUiSetting, loadUiSettings } from '@/bridge/uiSettings';
import { topMonitorData, type TopMonData } from '@/bridge/topmon';
import { controllerEditorBackgroundHoldKey, nextLowBatteryStatic, shouldSkipVideoReconcile, videoPreviewBackgroundHoldKey } from '@/bridge/backgroundPolicy';
import { createPowerResumeQueue, runPowerResumeTransaction } from '@/bridge/powerResume';
import { readSettingsSection, saveSettingsSection } from '@/bridge/settingsRepository';
import { isUiVisible, onUiVisibilityChange, setNativeWindowVisible, startUiPowerReconciliation } from '@/bridge/uiLifecycle';
import { onSharedPowerLifecycle, getSharedPowerLifecycle, requestSharedPowerLifecycleReadyConfirmation, isSharedPowerLifecycleAdmitted, beginSharedPowerLifecycleReadback, sharedPowerWriteScope, type SharedPowerWriteScope, type SharedPowerLifecycleSnapshot } from '@/bridge/powerLifecycleState';
import { powerSourceMode, rememberPowerSourceMode, onPowerSourceNotification, startForegroundPowerSourceRefresh } from '@/bridge/powerSource';
import { tdpRuntimeStore } from '@/bridge/tdpRuntimeState';
import {
  getFanFeatureSettings,
  getFanPresetCurve,
  initializeFanFeature,
  rememberFanDevice,
  setFanControlActive,
} from '@/bridge/fanFeature';
import { fanDiagnosticLog, fanLifecycleEvidence } from '@/bridge/fanDiagnostics';
import { fanHostLifecycle, resolveFanHostConfig } from '@/bridge/fanHost';
import { setGyroVirtualFeatureAssetsRoot } from '@/bridge/gyroVirtualFeature';
import { startAiFanService } from '@/bridge/aiFanService';
import { parseAiFanMockSession, projectAiFanErrorCode } from '@/bridge/aiFanMock';
let aiFanMockActive = false;
let aiCpuIsolated = false;
let stopAiFanService: (() => void) | null = null;

applyTheme(
  document.documentElement.dataset.theme === 'cyberpunk'
    ? 'cyberpunk'
    : document.documentElement.dataset.theme === 'red-black'
      ? 'red-black'
      : 'blue-black',
);

// Visibility gates decorative rendering only; native control/renewal remain active.
const uiVisible = ref(isUiVisible());
const decorativeWindowForeground = ref<boolean | null>(null);
let uiPowerReconciliation: ReturnType<typeof startUiPowerReconciliation> | null = null;
let stopSharedPowerWatch: (() => void) | null = null;
let stopSharedFanWakeWatch: (() => void) | null = null;

const BOOT_TASK = BOOT_CONTROL_CENTER_TASK;
const BOOT_CFG = 'C:\\SOFT\\YeMan\\PowerControl\\boot_config.json';
// 开机启动自愈：若 boot_config.json 记录"开"但计划任务被删除（Windows 更新/杀软），自动重建
async function healBootTask() {
  try {
    const startup = await readSettingsSection<any>('startupDesired');
    if (startup.bootControlCenter === true && !(await bootMirrorExists())) {
      await toggleBootMirror(true);
    }
  } catch { /* 配置文件不存在或无权限，忽略 */ }
}

// ── FAN-932：风扇「开机/休眠唤醒启动风扇」偏好（startupDesired.fanControl）的唯一自动启动编排入口 ──
// 开机与唤醒共用本函数，不在两个事件处理器里各写一份 start/apply 规则。要求（裁决 §2.1）：
//   1) 同一电源代次去重并串行；新代不等待已退役的旧预检，底层硬件仍由 Fan 生命周期串行；
//   2) 偏好非严格 true 立即返回（本路径只读一次设置，不启动 Host、不发 HTTP、不碰 HC）；
//   3) 已有活动控制意图优先（崩溃接续/活动会话/唤醒已恢复的曲线），绝不用默认曲线覆盖用户曲线；
//   4) 设备门必须同时满足 allowed && writeReady 才继续 apply；
//   5) apply 前再次核对代次、偏好与活动意图，期间被取代就取消本次写入；
//   6) 失败按现有失败路径记录，不改写成“已启动”，也不在同一代内无限重试。
let fanAutoStartChain: Promise<void> = Promise.resolve();
let fanAutoStartTransitionGeneration = -1;
let fanBootAutoStartDone = false;
const fanAutoStartGenerations = new Set<number>();
// One wake delivery keeps its first cancellation receipt. A delayed duplicate
// cannot turn an explicit OFF into a fresh automatic start for the same wake.
type FanWakeRequest = {
  generation: number;
  cancellationRevision: number;
  retryAfterPublicReady?: (state: SharedPowerLifecycleSnapshot | null) => void;
  publicRetryUsed?: boolean;
  nativeReadyInFlight?: boolean;
  nativeReadyConfirmed?: boolean;
  nativeReadyRedelivered?: boolean;
  committedWake?: boolean;
  startupSuspendBound?: boolean;
  startupClaimInFlight?: boolean;
  startupClaimAttempts?: number;
  startupClaimAccepted?: boolean;
  startupClaimRedeliveryUsed?: boolean;
  startupSuspendSerial?: number;
};
const fanWakeRequests = new Map<number, FanWakeRequest>();

function rememberFanWakeRequest(generation: number): FanWakeRequest {
  const current = fanHostLifecycle.coordinatorSnapshot.powerGeneration;
  for (const older of fanWakeRequests.keys()) if (older < current) fanWakeRequests.delete(older);
  const previous = fanWakeRequests.get(generation);
  if (previous) return previous;
  const request = { generation, cancellationRevision: fanHostLifecycle.controlCancellationRevision };
  fanWakeRequests.set(generation, request);
  return request;
}
function ensureFanAutoStart(reason: 'boot' | 'resume', generation = 0): Promise<void> {
  // Explicit AI session has its own requests; do not inherit real stored on-state.
  if (aiFanMockActive) return Promise.resolve();
  if (reason === 'boot') {
    if (fanBootAutoStartDone) return Promise.resolve();
    fanBootAutoStartDone = true;
  } else {
    if (generation <= 0 || fanAutoStartGenerations.has(generation)) return Promise.resolve();
    fanAutoStartGenerations.add(generation);
  }
  // Serialize this generation only. A retired read keeps its own completion
  // but cannot delay or acquire authority from a newer Native wake.
  const transitionGenerationAtRequest = reason === 'resume' ? generation : latestResumeGeneration;
  const predecessor = fanAutoStartTransitionGeneration === transitionGenerationAtRequest ? fanAutoStartChain : Promise.resolve();
  fanAutoStartTransitionGeneration = transitionGenerationAtRequest;
  // onFanResumeReady checks the Native Fan admission before requesting auto start.
  // An admitted Fan wake can advance without restarting the public UI transaction.
  const currentTransition = () => reason === 'resume'
    ? generation >= latestResumeGeneration && generation === fanHostLifecycle.coordinatorSnapshot.powerGeneration
    : transitionGenerationAtRequest === latestResumeGeneration;
  // This automatic request cannot inherit authority after a newer explicit OFF/Close.
  const cancellationRevision = fanHostLifecycle.controlCancellationRevision;
  const mayProceed = () => cancellationRevision === fanHostLifecycle.controlCancellationRevision
    && currentTransition()
    && !fanHostLifecycle.hasControlIntent && !fanHostLifecycle.hasDeferredCrashRecovery;
  const readStartupPreference = () => readSettingsSection<{ fanControl?: boolean }>('startupDesired');
  const runWakeStep = fanHostLifecycle.publishedStartupWakeAdmission(reason === 'resume' ? generation : undefined,
    mayProceed, async () => (await readStartupPreference()).fanControl === true);
  const run = predecessor.then(async () => {
    try {
      if (!mayProceed()) return;
      const startup = await readStartupPreference();
      if (startup.fanControl !== true || !mayProceed()) return;
      const gate = await runWakeStep(async () => {
        if (!mayProceed()) return;
        return fanHostLifecycle.start({ manualRecovery: true, reason: 'authorized-auto-' + reason,
          publishedWakeGeneration: reason === 'resume' ? generation : undefined });
      });
      if (!gate) return;
      if (!gate.allowed || !gate.writeReady) {
        fanDiagnosticLog('lifecycle.auto-start-blocked', {
          reason, generation, allowed: gate.allowed, writeReady: gate.writeReady, gate: gate.reason ?? '',
        });
        return;
      }
      // apply 前最后一次核对：代次未过期、仍无活动意图、偏好仍开。
      if (reason === 'resume' && !currentTransition()) {
        fanDiagnosticLog('lifecycle.auto-start-superseded', { reason, generation, latest: latestResumeGeneration,
          fanGeneration: fanHostLifecycle.coordinatorSnapshot.powerGeneration });
        return;
      }
      if (!mayProceed()) return;
      const stillOn = await readStartupPreference();
      if (stillOn.fanControl !== true || !mayProceed()) return;
      const preset = await runWakeStep(async () => {
        if (!mayProceed()) return;
        const fanSettings = getFanFeatureSettings();
        if (fanSettings.featureEnabled !== true) return;
        // Keep the saved curve, including when public permission was retried.
        await fanHostLifecycle.apply(getFanPresetCurve(fanSettings.preset), reason === 'resume' ? generation : undefined);
        return fanSettings.preset;
      });
      if (preset !== undefined) fanDiagnosticLog('lifecycle.auto-start-applied', { reason, generation, preset });
    } catch (error) {
      fanDiagnosticLog('lifecycle.auto-start-failed', {
        reason, generation, error: error instanceof Error ? error.message : String(error),
      });
    }
  });
  fanAutoStartChain = run.catch(() => {});
  return run;
}

const router = useRouter();
const store = useDebugStore();
// The legacy deep-link route keeps the editor document free of the main YMCC
// navigation/monitor chrome. The normal controller entry now renders the
// editor as a full-screen, centered bubble in the main WebView.
// Owned editor/monitor-preview children share the lightweight shell, never main hardware startup.
const isStandaloneEditor = computed(() => ['/button-mapping/shortcuts', '/monitor/hud-preview'].includes(router.currentRoute.value.path));
const backgroundUrl = ref('');
const backgroundKind = ref<BackgroundKind>('image');
const backgroundVideo = ref<HTMLVideoElement | null>(null);
const backgroundOpacity = ref(getBackgroundOpacity());
const backgroundBlur = ref(getBackgroundBlur());
const backgroundPosition = ref(getBackgroundPosition());
const backgroundObjectPosition = computed(() => `${backgroundPosition.value.x}% ${backgroundPosition.value.y}%`);
const backgroundVideoAutoPause = ref(getUiSetting('videoBatteryPause'));
const onAcPower = ref(false);
type VideoPowerState = 'unknown' | 'desktop' | 'charging' | 'discharging';
// Do not create a video decoder until the first power reading is known. This
// prevents a brief play() on battery while startup probes are still pending.
const videoPowerState = ref<VideoPowerState>('unknown');
let lastVideoPowerTs = 0;
let pendingVideoPowerState: Exclude<VideoPowerState, 'unknown'> | null = null;
let pendingVideoPowerSamples = 0;
const VIDEO_POWER_EVENT_SETTLE_MS = 5000;
let videoPowerEventSettleUntil = 0;
let videoPowerProbeGeneration = 0;
let videoPowerProbeTimer: number | null = null;
const documentVisible = ref(document.visibilityState === 'visible');
const nativeWindowVisible = ref(document.visibilityState === 'visible');
// WebView presents a first frame even when the owning window starts in the tray.
const undervoltWindowVisible = ref<boolean | undefined>();
provide(undervoltPresentationAllowed, computed(() => undervoltWindowVisible.value === true));
const backgroundVideoSuspended = ref(false);
const backgroundEditorPaused = ref(false);
const backgroundEditorHolds = new Set<symbol>();
let backgroundEditorPauseRevision = 0;
let backgroundPresentationDisposed = false;
function resumeAfterBackgroundHold(): void {
  if (backgroundPresentationDisposed) return;
  const video = backgroundVideo.value;
  if (backgroundSource.value === 'dynamic' && activeDynamicMediaAsset && video &&
      backgroundVideoShouldPlay(video) &&
      (video.error || (!isHlsUrl(backgroundUrl.value) && !video.getAttribute('src')))) {
    // A covered error or a cancelled local retry is not a new decode failure.
    onBackgroundVideoError(undefined, video, true);
    return;
  }
  void reconcileBackgroundVideo();
}
function acquireControllerEditorBackgroundHold(): () => void {
  const owner = Symbol();
  backgroundEditorHolds.add(owner);
  if (!backgroundEditorPaused.value) {
    backgroundEditorPaused.value = true;
    ++backgroundEditorPauseRevision;
    if (backgroundSource.value === 'dynamic') clearBackgroundRetry();
    // Keep the current frame/source. A covered editor needs no video decoder work.
    videoDesired.value = false;
    backgroundVideo.value?.pause();
  }
  return () => {
    if (!backgroundEditorHolds.delete(owner) || backgroundEditorHolds.size) return;
    backgroundEditorPaused.value = false;
    ++backgroundEditorPauseRevision;
    resumeAfterBackgroundHold();
  };
}
provide(controllerEditorBackgroundHoldKey, acquireControllerEditorBackgroundHold);
let playingWallpaperPreviews = 0;
provide(videoPreviewBackgroundHoldKey, () => {
  if (++playingWallpaperPreviews === 1) {
    // Keep the full-window frame visible while the settings preview plays.
    cancelFactoryVideoStart();
    if (backgroundSource.value === 'dynamic') clearBackgroundRetry();
    videoDesired.value = false;
    backgroundVideo.value?.pause();
  }
  let released = false;
  return () => {
    if (released) return;
    released = true;
    if (--playingWallpaperPreviews === 0) resumeAfterBackgroundHold();
  };
});
const lowBatteryStatic = ref(false);
const videoDesired = ref(false);
const backgroundVideoGeneration = ref(0);
const dynamicBackgroundActive = ref(false);
const backgroundSource = ref<'fixed' | 'dynamic'>('fixed');
const factoryBackgroundVideo = computed(() =>
  backgroundSource.value === 'fixed' &&
  backgroundKind.value === 'video' &&
  backgroundUrl.value.startsWith('https://default-background.localhost/SteamDeck.mp4?v='),
);
// A small copy of the first frame of the shipped SteamDeck.mp4. The optional
// native asset can be replaced; different advertised sizes use normal preload.
const factoryBackgroundPoster = computed(() => factoryBackgroundVideo.value &&
  backgroundUrl.value === 'https://default-background.localhost/SteamDeck.mp4?v=3026151'
  ? 'data:image/webp;base64,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' : undefined);
let factoryVideoStartTimer: number | null = null;
let factoryWindowForeground: boolean | null = null;
let softwareWebViewRendering = false;
function cancelFactoryVideoStart(): void {
  if (factoryVideoStartTimer === null) return;
  window.clearTimeout(factoryVideoStartTimer);
  factoryVideoStartTimer = null;
}
const backgroundFallbackUrls = ref<string[]>([]);
const backgroundPlayback=ref<MediaPlayback|null>(null);
let activeDynamicMediaKey='';
let sharedMediaDirty=false;
let activeDynamicMediaAsset:{key:string;assetId:string;bindingRevision:number;kind?:string;reducedPlayback?:true;pid?:number;processCreated?:string}|null=null;
let dynamicOriginalFallback:DynamicBackgroundFallback|null=null;
let sharedMediaRefreshQueued=false;
let sharedMediaPending:{notificationSequence:number|undefined}|null=null;
let waitingDynamicPlayback:{key:string;pid:number;processCreated:string}|null=null;
function requestSharedBackgroundRefresh(){
  if(!dynamicBackgroundAllowed())return;
  sharedMediaDirty=true;
  queueSharedBackgroundRefresh();
}
function queueSharedBackgroundRefresh(){
  const admitted=()=>isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle());
  if(!sharedMediaDirty||sharedMediaPending||sharedMediaRefreshQueued||!resumeLifecycleActive||!isUiVisible()||!dynamicBackgroundAllowed()||!admitted())return;
  sharedMediaRefreshQueued=true;
  queueMicrotask(async()=>{
    try{
      if(sharedMediaPending||!resumeLifecycleActive||!isUiVisible()||!dynamicBackgroundAllowed()||!admitted())return;
      sharedMediaDirty=false;
      const game=await detectGame();
      if(!resumeLifecycleActive||!dynamicBackgroundAllowed()||!admitted())return;
      if(sharedMediaPending||!isUiVisible()){sharedMediaDirty=true;return;}
      if(game&&!isCurrentGameTarget(game))return;
      lastDynamicGameIdentity=null;
      onDynamicGameStatus(game);
    }catch{/* The next actual game/media event can refresh the background. */}
    finally{sharedMediaRefreshQueued=false;queueSharedBackgroundRefresh();}
  });
}
async function readDynamicGameBackground(game:DetectedGame){
  waitingDynamicPlayback=null;
  const pending={notificationSequence:getSharedPowerLifecycle()?.observedNotificationSequence};
  sharedMediaPending=pending;
  try{const result=await refreshDynamicBackground(game,dynamicOriginalFallback??undefined);return isFailedDynamicVariant(result?.state,dynamicOriginalFallback)?null:result;}
  finally{
    if(sharedMediaPending===pending){sharedMediaPending=null;queueSharedBackgroundRefresh();}
  }
}
function onDynamicBackgroundWaiting(event:Event){
  const detail=(event as CustomEvent<NonNullable<typeof waitingDynamicPlayback>>).detail;
  const game=getCurrentGame();
  if(dynamicBackgroundAllowed()&&detail?.key&&game&&detail.pid===game.pid&&detail.processCreated===game.processCreated)
    waitingDynamicPlayback=detail;
}
function onSharedMediaChanged(event:Event){
  const detail=(event as CustomEvent).detail;
  if(detail?.playbackOnly){
    if(detail.key===activeDynamicMediaKey&&activeDynamicMediaAsset?.kind==='video')return;
    const game=getCurrentGame(),waiting=waitingDynamicPlayback;
    if(game&&waiting&&detail.key===waiting.key&&game.pid===waiting.pid&&game.processCreated===waiting.processCreated){requestSharedBackgroundRefresh();return;}
    // A publication can race the initial read, before the waiting key is known.
    if(!activeDynamicMediaKey&&sharedMediaPending){requestSharedBackgroundRefresh();return;}
  }else sharedMediaRevisions.observe(detail?.key,detail?.revision,detail?.canonicalKey);
  if(detail?.key===activeDynamicMediaKey&&activeDynamicMediaKey){requestSharedBackgroundRefresh();return;}
  // A game with no bound media still needs its first successful publication.
  if(activeDynamicMediaKey||detail?.ready!==true||typeof detail.executable!=='string'||!detail.executable)return;
  const game=getCurrentGame();
  if(game?.processCreated&&game.path.replace(/\//g,'\\').toLowerCase()===detail.executable.replace(/\//g,'\\').toLowerCase())requestSharedBackgroundRefresh();
}
function onBackgroundVideoTimeUpdate(){const video=backgroundVideo.value,range=backgroundPlayback.value;if(video&&range)applyGameMediaRange(video,range);}
const backgroundFallbackIndex = ref(0);
// 只有图片或未被暂停的视频真正作为背景层显示时，才隐藏默认黑底。
// 视频在电池保护/窗口隐藏时会暂时卸载，此时仍需要黑底托住半透明界面。
const hasVisibleBackgroundMedia = computed(() => Boolean(
  backgroundUrl.value && (
    backgroundKind.value === 'image' ||
    (backgroundKind.value === 'video' && !backgroundVideoSuspended.value)
  ),
));
let appliedBackgroundIdentity: string | null = null;
let lastDynamicGameIdentity: string | null = null;
let dynamicBackgroundGeneration = 0;
let lastFixedBackgroundState: BackgroundState | null = null;
const BACKGROUND_RETRY_WINDOW_MS = 2 * 60 * 1000;
const BACKGROUND_RETRY_INTERVAL_MS = 5000;
const DYNAMIC_BACKGROUND_NO_GAME_GRACE_MS = 4000;
let backgroundRetryTimer: number | null = null;
let backgroundRetryUntil = 0;
let sharedBackgroundRetryIdentity = '';
let sharedBackgroundRetryAttempts = 0;
let dynamicBackgroundNoGameTimer: number | null = null;
let resumeVideoTimer: number | null = null;
let hlsInstance: BackgroundHls | null = null;
function isVideoPlaybackBlockedByPower(): boolean {
  return wallpaperPowerPaused.value || backgroundVideoAutoPause.value &&
    videoPowerState.value !== 'desktop' &&
    videoPowerState.value !== 'charging';
}
// 每次隐藏、切源、销毁或重新挂载媒体都会推进 epoch。异步 nextTick/HLS 回调
// 必须匹配当前 epoch，避免隐藏后旧回调重新挂载视频或触发重试。
let backgroundMediaEpoch = 0;
function isHlsUrl(url: string): boolean {
  return /\.m3u8(?:\?|$)/i.test(url);
}
function destroyHls(): void {
  hlsInstance?.destroy();
  hlsInstance = null;
}
function invalidateBackgroundMedia(): number {
  cancelFactoryVideoStart();
  factoryWindowForeground = null;
  backgroundMediaEpoch += 1;
  destroyHls();
  return backgroundMediaEpoch;
}
function clearBackgroundRetry(): void {
  if (backgroundRetryTimer !== null) {
    window.clearTimeout(backgroundRetryTimer);
    backgroundRetryTimer = null;
  }
  backgroundRetryUntil = 0;
}
function formatRetryRemaining(ms: number): string {
  const totalSeconds = Math.max(0, Math.ceil(ms / 1000));
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return minutes > 0 ? `${minutes}分${seconds.toString().padStart(2, '0')}秒` : `${seconds}秒`;
}
function scheduleBackgroundRetry(message: string): void {
  if (backgroundKind.value !== 'video' || !backgroundUrl.value ||
      (backgroundSource.value === 'dynamic' && !dynamicBackgroundAllowed())) return;
  const now = Date.now();
  if (backgroundRetryUntil <= now) backgroundRetryUntil = now + BACKGROUND_RETRY_WINDOW_MS;
  const remaining = backgroundRetryUntil - now;
  if (remaining <= 0) {
    clearBackgroundRetry();
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${message}` }));
    return;
  }
  if (backgroundRetryTimer !== null) window.clearTimeout(backgroundRetryTimer);
  const delay = Math.min(BACKGROUND_RETRY_INTERVAL_MS, remaining);
  window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
    detail: `播放失败，${Math.ceil(delay / 1000)}秒后重试（剩余 ${formatRetryRemaining(remaining)}）`,
  }));
  backgroundRetryTimer = window.setTimeout(() => {
    backgroundRetryTimer = null;
    if (Date.now() >= backgroundRetryUntil || backgroundKind.value !== 'video' || !backgroundUrl.value ||
        backgroundVideoSuspended.value || isVideoPlaybackBlockedByPower() ||
        (backgroundSource.value === 'dynamic' && !dynamicBackgroundAllowed())) {
      const finalMessage = Date.now() >= backgroundRetryUntil ? message : 'Steam 在线视频播放失败';
      clearBackgroundRetry();
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${finalMessage}` }));
      return;
    }
    const urls = backgroundFallbackUrls.value;
    backgroundFallbackIndex.value = 0;
    backgroundVideoGeneration.value++;
    backgroundUrl.value = urls[0] || backgroundUrl.value;
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
      detail: `正在重试 Steam 在线视频（剩余 ${formatRetryRemaining(backgroundRetryUntil - Date.now())}）`,
    }));
    void nextTick(async () => {
      if (backgroundVideoSuspended.value) return;
      const video = backgroundVideo.value;
      if (!video || backgroundKind.value !== 'video') return;
      if (!factoryBackgroundPoster.value && !isHlsUrl(backgroundUrl.value)) video.load();
      await setupBackgroundVideo();
      await reconcileBackgroundVideo();
    });
  }, delay);
}
async function setupBackgroundVideo(): Promise<void> {
  const epoch = ++backgroundMediaEpoch;
  await nextTick();
  if (epoch !== backgroundMediaEpoch) return;
  const video = backgroundVideo.value;
  if (!video || backgroundKind.value !== 'video' || !backgroundUrl.value) {
    destroyHls();
    return;
  }
  destroyHls();
  if (!isHlsUrl(backgroundUrl.value)) return;
  if (backgroundVideoSuspended.value || shouldSuspendBackgroundVideo() || backgroundPresentationDisposed) return;
  const sourceUrl = backgroundUrl.value;
  const HlsCtor = currentBackgroundHls() ?? await loadBackgroundHls();
  if (epoch !== backgroundMediaEpoch || backgroundVideo.value !== video || backgroundKind.value !== 'video' ||
      backgroundUrl.value !== sourceUrl || backgroundVideoSuspended.value || shouldSuspendBackgroundVideo() ||
      backgroundPresentationDisposed) return;
  if (HlsCtor?.isSupported?.()) {
    const hls = new HlsCtor({
      enableWorker: true,
      lowLatencyMode: false,
      startLevel: 0,
      capLevelToPlayerSize: true,
      maxBufferLength: 20,
    });
    hlsInstance = hls;
    hls.on(HlsCtor.Events.MEDIA_ATTACHED, () => {
      if (epoch !== backgroundMediaEpoch || hlsInstance !== hls || backgroundVideoSuspended.value || backgroundUrl.value !== sourceUrl) {
        hls.destroy();
        return;
      }
      hls.loadSource(sourceUrl);
    });
    const generation = backgroundVideoGeneration.value;
    hls.on(HlsCtor.Events.ERROR, (_event: unknown, data: { fatal?: boolean }) => {
      if (epoch === backgroundMediaEpoch && hlsInstance === hls && !backgroundVideoSuspended.value && generation === backgroundVideoGeneration.value && data?.fatal) {
        onBackgroundVideoError();
      }
    });
    hls.attachMedia(video);
    return;
  }
  if (video.canPlayType('application/vnd.apple.mpegurl')) video.src = sourceUrl;
  else if (!HlsCtor) onBackgroundVideoError('HLS 播放组件加载失败', video, true);
}
const backgroundStyle = computed(() =>
  backgroundUrl.value && backgroundKind.value === 'image'
    ? {
        backgroundImage: `url("${backgroundUrl.value}")`,
        backgroundPosition: backgroundObjectPosition.value,
        opacity: String(backgroundOpacity.value),
        filter: `blur(${backgroundBlur.value}px) saturate(1.12) contrast(1.08)`,
      }
    : undefined,
);
function applyBackgroundState(state: BackgroundState | null | undefined, source: 'fixed' | 'dynamic' = 'fixed'): void {
  if(source==='dynamic'&&sharedMediaRevisions.outdated(state)){requestSharedBackgroundRefresh();return;}
  if(source==='dynamic'&&isFailedDynamicVariant(state,dynamicOriginalFallback))return;
  const previousPlayback=backgroundPlayback.value;
  backgroundPlayback.value=source==='dynamic'?state?.playback||null:null;
  activeDynamicMediaKey=source==='dynamic'?state?.mediaKey||'':'';
  activeDynamicMediaAsset=source==='dynamic'&&state?.mediaAssetId&&state.mediaBindingRevision!==undefined?{key:state.mediaKey||'',assetId:state.mediaAssetId,bindingRevision:state.mediaBindingRevision,kind:state.kind,reducedPlayback:state.mediaReducedPlayback,pid:state.pid,processCreated:state.processCreated}:null;
  const kind = state?.kind === 'video' ? 'video' : 'image';
  const urls = Array.isArray(state?.fallbackUrls)
    ? state.fallbackUrls.filter((url): url is string => typeof url === 'string' && url.length > 0)
    : [];
  const requestedUrl = state?.enabled && state.url ? state.url : '';
  const effectiveUrl = requestedUrl || urls[0] || '';
  const identity = JSON.stringify([
    source,
    Boolean(state?.enabled && effectiveUrl),
    kind,
    effectiveUrl,
    urls,
  ]);
  if (identity === appliedBackgroundIdentity) {
    const video=backgroundVideo.value;
    if(video&&video.readyState>=1&&backgroundPlayback.value?.startMode==='from-start'&&previousPlayback?.startMode!=='from-start')video.currentTime=0;
    return;
  }
  appliedBackgroundIdentity = identity;
  clearBackgroundRetry();
  // Stop the old element before changing source/state. The pause handler is
  // intent-aware, so this cannot enqueue an unwanted auto-resume.
  videoDesired.value = false;
  backgroundVideo.value?.pause();
  invalidateBackgroundMedia();
  backgroundVideoGeneration.value++;
  backgroundSource.value = source;
  backgroundKind.value = kind;
  backgroundVideoSuspended.value = backgroundKind.value === 'video' &&
    (!documentVisible.value || !nativeWindowVisible.value || lowBatteryStatic.value ||
      isVideoPlaybackBlockedByPower());
  backgroundFallbackUrls.value = urls;
  const index = requestedUrl && urls.length ? Math.max(0, urls.indexOf(requestedUrl)) : 0;
  backgroundFallbackIndex.value = index;
  backgroundUrl.value = effectiveUrl;
  void nextTick(async () => {
    const video = backgroundVideo.value;
    if (video && backgroundKind.value === 'video' && !factoryBackgroundPoster.value && !isHlsUrl(backgroundUrl.value)) video.load();
    await setupBackgroundVideo();
    await reconcileBackgroundVideo();
  });
}
function onBackgroundChanged(e: Event): void {
  const state = (e as CustomEvent<BackgroundState>).detail;
  lastFixedBackgroundState = state || null;
  if (dynamicBackgroundActive.value) return;
  applyBackgroundState(state);
}
function onBackgroundOpacityChanged(e: Event): void {
  const opacity = Number((e as CustomEvent<{ opacity?: number }>).detail?.opacity);
  if (Number.isFinite(opacity)) backgroundOpacity.value = opacity;
}
function onBackgroundBlurChanged(e: Event): void {
  const blur = Number((e as CustomEvent<{ blur?: number }>).detail?.blur);
  if (Number.isFinite(blur)) backgroundBlur.value = blur;
}

function onBackgroundPositionChanged(e: Event): void {
  const position = (e as CustomEvent<{ x: number; y: number }>).detail;
  if (position && Number.isFinite(position.x) && Number.isFinite(position.y)) backgroundPosition.value = position;
}

function backgroundVideoPlaybackEligible(video = backgroundVideo.value): boolean {
  return Boolean(
    video &&
    backgroundKind.value === 'video' &&
    backgroundUrl.value &&
    // Keep the factory poster in software recovery mode without opening the video.
    !(softwareWebViewRendering && factoryBackgroundPoster.value) &&
    (backgroundSource.value === 'fixed' || dynamicBackgroundAllowed()) &&
    !backgroundVideoSuspended.value &&
    !backgroundEditorPaused.value &&
    playingWallpaperPreviews === 0 &&
    !backgroundPresentationDisposed &&
    documentVisible.value &&
    nativeWindowVisible.value &&
    (!factoryBackgroundVideo.value || factoryBackgroundPoster.value || document.hasFocus()) &&
    !lowBatteryStatic.value &&
    !isVideoPlaybackBlockedByPower(),
  );
}

function backgroundVideoShouldPlay(video = backgroundVideo.value): boolean {
  return backgroundVideoPlaybackEligible(video) &&
    (factoryBackgroundPoster.value ? factoryWindowForeground === true :
      (decorativeWindowForeground.value ?? document.hasFocus()));
}

async function reconcileBackgroundVideo(): Promise<void> {
  await nextTick();
  if (backgroundPresentationDisposed) return;
  const video = backgroundVideo.value;
  const epoch = backgroundMediaEpoch;
  const editorRevision = backgroundEditorPauseRevision;
  const eligible = backgroundVideoPlaybackEligible(video);
  const factoryPoster = factoryBackgroundPoster.value;
  const shouldPlay = backgroundVideoShouldPlay(video);
  if (!eligible) cancelFactoryVideoStart();
  if (!video) {
    videoDesired.value = shouldPlay;
    return;
  }
  if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value) return;
  if (eligible && factoryPoster && factoryWindowForeground === null && factoryVideoStartTimer === null) {
    // DOM focus may be assigned even while another app owns the foreground.
    const timer = window.setTimeout(async () => {
      const state = await windowApi.getState().catch(() => null);
      if (factoryVideoStartTimer !== timer) return;
      factoryVideoStartTimer = null;
      if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value ||
          !factoryBackgroundPoster.value || !backgroundVideoPlaybackEligible(video)) return;
      // A frontend temporarily paired with an older host keeps its legacy focus check.
      if (state) factoryWindowForeground = state.visible && !state.minimized && (state.foreground ?? document.hasFocus());
      if (factoryWindowForeground) void reconcileBackgroundVideo();
    }, video.hasAttribute('src') ? 0 : 300);
    factoryVideoStartTimer = timer;
  }
  if (shouldPlay && factoryPoster && !video.hasAttribute('src')) video.src = backgroundUrl.value;
  // Autoplay can leave the element playing while the remembered intent is
  // still false; do not skip the pause operation in that state.
  if (shouldSkipVideoReconcile(
    videoDesired.value,
    shouldPlay,
    !video.paused && !video.ended,
  )) return;
  videoDesired.value = shouldPlay;
  if (!shouldPlay) {
    video.pause();
    return;
  }
  // The element is intentionally not marked autoplay. Wait for metadata so
  // the explicit play() below cannot turn a newly mounted or HLS-backed video
  // into a false playback failure while its source is still being attached.
  if (video.readyState < 1 && !factoryBackgroundPoster.value) {
    videoDesired.value = false;
    return;
  }
  video.muted = true;
  try {
    await video.play();
    if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value || !backgroundVideoShouldPlay(video)) {
      video.pause();
    }
  } catch {
    if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value ||
        editorRevision !== backgroundEditorPauseRevision || !backgroundVideoShouldPlay(video)) return;
    videoDesired.value = false;
    onBackgroundVideoError(undefined, video);
  }
}

function onBackgroundVideoLoaded(): void {
  onBackgroundVideoTimeUpdate();
  void reconcileBackgroundVideo();
}

function onBackgroundVideoCanPlay(): void {
  // Readiness can fire again after a seek while the same play is in flight.
  void reconcileBackgroundVideo();
}

function onBackgroundFocusChanged(): void {
  if (!factoryBackgroundVideo.value && decorativeWindowForeground.value !== null) return;
  // A DOM focus edge is a hint to recheck, never a foreground grant.
  if (factoryBackgroundPoster.value && factoryWindowForeground !== true) {
    // Preserve any in-flight confirmation. Native foreground loss revokes it.
    factoryWindowForeground = null;
  } else if (factoryBackgroundPoster.value && !document.hasFocus()) {
    cancelFactoryVideoStart();
    factoryWindowForeground = null;
  }
  void reconcileBackgroundVideo();
}

function onBackgroundWindowActivation(event: Event): void {
  const active = (event as CustomEvent<{ active: boolean }>).detail?.active;
  if (typeof active !== 'boolean') return;
  decorativeWindowForeground.value = active;
  if (factoryBackgroundPoster.value) {
    cancelFactoryVideoStart();
    factoryWindowForeground = active ? null : false;
  }
  if (backgroundKind.value !== 'video') return;
  void reconcileBackgroundVideo();
}

function onBackgroundVideoPause(): void {
  if (!videoDesired.value) return;
  const shouldResume = backgroundVideoShouldPlay();
  if (shouldResume) {
    videoDesired.value = false;
    window.setTimeout(() => void reconcileBackgroundVideo(), 100);
  }
}

function classifyVideoPower(data: TopMonData): Exclude<VideoPowerState, 'unknown'> | null {
  if (!data.hasBattery) return 'desktop';
  if (data.chargeW < -0.5) return 'discharging';
  if (data.chargeW > 0.5) return 'charging';
  return null;
}

function updateLowBatteryStatic(data: TopMonData | null): void {
  const shouldStatic = nextLowBatteryStatic(
    lowBatteryStatic.value,
    data,
    backgroundVideoAutoPause.value,
  );
  if (shouldStatic === lowBatteryStatic.value) return;
  lowBatteryStatic.value = shouldStatic;
  reconcileBackgroundLifecycle();
}

function shouldSuspendBackgroundVideo(): boolean {
  return backgroundKind.value === 'video' &&
    (!documentVisible.value || !nativeWindowVisible.value || lowBatteryStatic.value ||
      isVideoPlaybackBlockedByPower());
}

function reconcileBackgroundLifecycle(): void {
  if (backgroundKind.value !== 'video') return;
  if (shouldSuspendBackgroundVideo()) {
    if (!backgroundVideoSuspended.value) suspendBackgroundVideo();
    return;
  }
  if (backgroundVideoSuspended.value) {
    resumeBackgroundVideo();
    return;
  }
  void reconcileBackgroundVideo();
}

function suspendBackgroundVideo(): void {
  if (backgroundKind.value !== 'video') return;
  if (dynamicOriginalFallback) { dynamicOriginalFallback = null; sharedMediaDirty = true; }
  clearBackgroundRetry();
  videoDesired.value = false;
  // Mark suspended and revoke HLS callbacks before aborting the media load.
  backgroundVideoSuspended.value = true;
  invalidateBackgroundMedia();
  const video = backgroundVideo.value;
  if (video) { video.pause(); video.removeAttribute('src'); video.load(); }
}

function pauseBackgroundVideoForWindow(): void {
  if (backgroundKind.value !== 'video') return;
  if (backgroundSource.value === 'dynamic') {
    if (!backgroundVideoSuspended.value) suspendBackgroundVideo();
    return;
  }
  cancelFactoryVideoStart();
  factoryWindowForeground = null;
  clearBackgroundRetry();
  videoDesired.value = false;
  backgroundVideo.value?.pause();
}

function resumeBackgroundVideo(): void {
  if (shouldSuspendBackgroundVideo()) return;
  if (!backgroundVideoSuspended.value) {
    void reconcileBackgroundVideo();
    return;
  }
  backgroundVideoSuspended.value = false;
  void nextTick(async () => {
    if (backgroundVideoSuspended.value || !documentVisible.value || !nativeWindowVisible.value) return;
    const video = backgroundVideo.value;
    if (!video || backgroundKind.value !== 'video' || !backgroundUrl.value) return;
    if (!factoryBackgroundPoster.value && !isHlsUrl(backgroundUrl.value)) video.load();
    await setupBackgroundVideo();
    // Media recovery is independent from the power transaction. A decoder
    // that does not resolve play() after S3 must never hold input/hardware
    // recovery open.
    void reconcileBackgroundVideo();
  });
}

function observeVideoPower(data: TopMonData | null): void {
  if (!data) return;
  const next = classifyVideoPower(data);
  if (!next || data.ts === lastVideoPowerTs) return;
  lastVideoPowerTs = data.ts;
  if (Date.now() < videoPowerEventSettleUntil && next !== videoPowerState.value) return;
  updateLowBatteryStatic(data);
  if (next === videoPowerState.value) {
    pendingVideoPowerState = null;
    pendingVideoPowerSamples = 0;
    return;
  }
  if (pendingVideoPowerState !== next) {
    pendingVideoPowerState = next;
    pendingVideoPowerSamples = 0;
  }
  pendingVideoPowerSamples += 1;
  if (pendingVideoPowerSamples < 2) return;
  videoPowerState.value = next;
  pendingVideoPowerState = null;
  pendingVideoPowerSamples = 0;
  reconcileBackgroundLifecycle();
}

function applyVideoPowerMode(mode: 'ac' | 'dc', authoritative = false): void {
  onAcPower.value = mode === 'ac';
  videoPowerState.value = mode === 'ac' ? 'charging' : 'discharging';
  pendingVideoPowerState = null;
  pendingVideoPowerSamples = 0;
  if (mode === 'ac') lowBatteryStatic.value = false;
  if (authoritative) videoPowerEventSettleUntil = Date.now() + VIDEO_POWER_EVENT_SETTLE_MS;
  reconcileBackgroundLifecycle();
}

function requestVideoPowerCheck(delayMs = 0, expectedMode?: 'ac' | 'dc'): void {
  const generation = ++videoPowerProbeGeneration;
  if (videoPowerProbeTimer !== null) {
    window.clearTimeout(videoPowerProbeTimer);
    videoPowerProbeTimer = null;
  }
  const check = async () => {
    const mode = await detectPowerModeReliable();
    if (generation !== videoPowerProbeGeneration || !mode || expectedMode && mode !== expectedMode) return;
    applyVideoPowerMode(mode, true);
  };
  if (delayMs > 0) {
    videoPowerProbeTimer = window.setTimeout(() => {
      videoPowerProbeTimer = null;
      void check();
    }, delayMs);
  } else {
    void check();
  }
}

function onVideoPowerSourceChanged(ac: boolean): void {
  const mode = ac ? 'ac' : 'dc';
  invalidateFloatPowerSource(mode);
  applyVideoPowerMode(mode, true);
  requestVideoPowerCheck(300, mode);
}

watch(topMonitorData, (data) => {
  observeVideoPower(data);
}, { immediate: true });

watch([backgroundUrl, backgroundKind], () => {
  void nextTick(reconcileBackgroundVideo);
});

// A local interruption is not evidence that the shared file is corrupt. Keep
// one selected, foreground-only retry; visibility/source/OFF cleanup reuses the
// existing retry owner, so inactive windows own neither a decoder nor a timer.
function scheduleSharedBackgroundRetry(message: string): void {
  // Recovery may reattach a selected source while the window is visible. The
  // separate playback gate keeps it paused until the window is active again.
  if (backgroundRetryTimer !== null || !activeDynamicMediaAsset || !backgroundVideoPlaybackEligible()) return;
  const epoch = backgroundMediaEpoch;
  const editorRevision = backgroundEditorPauseRevision;
  const identity = JSON.stringify(activeDynamicMediaAsset);
  const source = backgroundUrl.value;
  const selected = JSON.stringify([identity, source]);
  if (selected !== sharedBackgroundRetryIdentity) {
    sharedBackgroundRetryIdentity = selected;
    sharedBackgroundRetryAttempts = 0;
  }
  const delay = [500, 2000][sharedBackgroundRetryAttempts] ?? 60000;
  sharedBackgroundRetryAttempts = Math.min(2, sharedBackgroundRetryAttempts + 1);
  const canRetry = () => epoch === backgroundMediaEpoch && editorRevision === backgroundEditorPauseRevision &&
    source === backgroundUrl.value && identity === JSON.stringify(activeDynamicMediaAsset) &&
    backgroundSource.value === 'dynamic' && backgroundVideoPlaybackEligible();
  const timer = window.setTimeout(() => {
    if (backgroundRetryTimer !== timer) return;
    backgroundRetryTimer = null;
    if (!canRetry()) return;
    backgroundVideoGeneration.value++;
    void nextTick(async () => {
      if (!canRetry()) return;
      await setupBackgroundVideo();
      void reconcileBackgroundVideo();
    });
  }, delay);
  backgroundRetryTimer = timer;
  window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
    detail: '播放暂时中断，' + (delay / 1000) + '秒后重试：' + message,
  }));
}

function onBackgroundVideoMediaError(event: Event): void {
  const video = backgroundVideo.value;
  if (!video || event.target !== video) return;
  onBackgroundVideoError(undefined, video);
}

function recoverDynamicBackgroundOriginal(video: HTMLVideoElement, message: string): boolean {
  const failed = activeDynamicMediaAsset;
  const game = getCurrentGame();
  if (!failed?.key || !game?.processCreated || failed.pid !== game.pid || failed.processCreated !== game.processCreated ||
      !isCurrentGameTarget(game) || !backgroundVideoShouldPlay(video)) return false;
  const previous = dynamicOriginalFallback;
  if (previous?.pid === game.pid && previous.processCreated === game.processCreated && previous.key === failed.key &&
      previous.assetId === failed.assetId && previous.bindingRevision === failed.bindingRevision) return true;
  const fallback: DynamicBackgroundFallback = {key: failed.key, assetId: failed.assetId, bindingRevision: failed.bindingRevision,
    pid: game.pid, processCreated: game.processCreated};
  dynamicOriginalFallback = fallback;
  clearBackgroundRetry();
  const epoch = invalidateBackgroundMedia();
  const generation = dynamicBackgroundGeneration;
  const editorRevision = backgroundEditorPauseRevision;
  video.pause();
  void readDynamicBackgroundOriginal(game, fallback).catch(() => null).then(state => {
    if (dynamicOriginalFallback !== fallback) return;
    if (epoch !== backgroundMediaEpoch || generation !== dynamicBackgroundGeneration || editorRevision !== backgroundEditorPauseRevision ||
        video !== backgroundVideo.value || failed !== activeDynamicMediaAsset || !isCurrentGameTarget(game) || !backgroundVideoShouldPlay(video)) {
      // A concurrent ordinary refresh already carries the same selection override.
      if (failed === activeDynamicMediaAsset) dynamicOriginalFallback = null;
      return;
    }
    if (state && !sharedMediaRevisions.outdated(state)) { applyBackgroundState(state, 'dynamic'); return; }
    dynamicOriginalFallback = null;
    onBackgroundVideoError(message, video, true);
  });
  return true;
}

function onBackgroundVideoError(message = 'Steam 在线视频播放失败', failedVideo?: HTMLVideoElement, localRecoveryOnly = false): void {
  if (failedVideo && failedVideo !== backgroundVideo.value) return;
  if (backgroundSource.value === 'dynamic' &&
      (backgroundEditorPaused.value || playingWallpaperPreviews > 0 || backgroundPresentationDisposed || !dynamicBackgroundAllowed())) return;
  if (backgroundVideoSuspended.value || !documentVisible.value || !nativeWindowVisible.value ||
      lowBatteryStatic.value || isVideoPlaybackBlockedByPower() || backgroundKind.value !== 'video') return;
  videoDesired.value = false;
  if (backgroundSource.value === 'dynamic') {
    if (!activeDynamicMediaAsset) return;
    const video = failedVideo || backgroundVideo.value;
    const code = video?.error?.code;
    if (!localRecoveryOnly && (code === 2 || code === 4) && video && activeDynamicMediaAsset.reducedPlayback === true &&
        recoverDynamicBackgroundOriginal(video, message)) return;
    // A read/source error cannot establish that the shared bytes are corrupt.
    if (!localRecoveryOnly && code === 3) {
      invalidateBackgroundMedia();
      clearBackgroundRetry();
      const failed = activeDynamicMediaAsset;
      activeDynamicMediaAsset = null;
      void sharedGameMedia.playbackFailed(failed).catch(() => {});
    } else if (backgroundRetryTimer === null) {
      invalidateBackgroundMedia();
      scheduleSharedBackgroundRetry(message);
      video?.pause();
      video?.removeAttribute('src');
      video?.load();
    }
    return;
  }
  invalidateBackgroundMedia();
  const urls = backgroundFallbackUrls.value;
  const nextIndex = backgroundFallbackIndex.value + 1;
  if (nextIndex >= urls.length) {
    backgroundFallbackIndex.value = 0;
    scheduleBackgroundRetry(message);
    return;
  }
  window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
    detail: `视频线路失败，正在切换 ${nextIndex + 1}/${urls.length}`,
  }));
  backgroundFallbackIndex.value = nextIndex;
  backgroundVideoGeneration.value++;
  backgroundUrl.value = urls[nextIndex];
    void nextTick(async () => {
      if (backgroundVideoSuspended.value) return;
      const video = backgroundVideo.value;
    if (!video || backgroundKind.value !== 'video') return;
    if (!factoryBackgroundPoster.value && !isHlsUrl(backgroundUrl.value)) video.load();
    await setupBackgroundVideo();
    await reconcileBackgroundVideo();
  });
}

function onBackgroundVideoPlaying(event?: Event): void {
  if (event && event.target !== backgroundVideo.value) return;
  clearBackgroundRetry();
  if (dynamicBackgroundActive.value && isHlsUrl(backgroundUrl.value)) {
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
      detail: '正在播放 Steam 在线视频',
    }));
  }
}
// 启动耗时采样：模块加载起点
const APP_START = performance.now();
(window as any).__appStart = APP_START;

// ── 全局刷新触发器：支持页刷新按钮触发所有已挂载页面 refresh() ──
const globalRefreshKey = ref(0);
provide('globalRefreshKey', globalRefreshKey);

// ── 模拟鼠标内页锁定 ──
// 不再进行 JoyXoff 5 秒后台轮询。程序启动只同步一次；之后仅由真实开/关操作事件更新。
async function syncMouseModeAtStartup(): Promise<void> {
  try {
    const state = await getMouseModeState();
    if (!acceptMouseModeState(state)) return;
    window.dispatchEvent(new CustomEvent('gp:mouse-mode', {
      detail: { ...state },
    }));
  } catch {
    // 启动同步失败保持默认未锁定；不生成后台重试或错误轮询。
  }
}
function onBackgroundVisibilityChange(): void {
  documentVisible.value = document.visibilityState === 'visible';
  // Page visibility and native HWND visibility are independent after a
  // WebView2 controller reload. Never turn a tray-hidden window "visible"
  // merely because Chromium restored the document.
  if (!documentVisible.value) {
    pauseBackgroundVideoForWindow();
    return;
  }
  queueSharedBackgroundRefresh();
  resumeBackgroundVideo();
}
function onBackgroundWindowHidden(): void {
  nativeWindowVisible.value = false;
  undervoltWindowVisible.value = false;
  setNativeWindowVisible(false);
  stopDynamicBackgroundWatch();
  pauseBackgroundVideoForWindow();
}
function onBackgroundWindowShown(): void {
  nativeWindowVisible.value = true;
  undervoltWindowVisible.value = true;
  if (factoryWindowForeground === false) factoryWindowForeground = null;
  setNativeWindowVisible(true);
  queueSharedBackgroundRefresh();
  resumeBackgroundVideo();
  requestVideoPowerCheck();
}
function powerEventGeneration(e: Event): number {
  return Number((e as CustomEvent<{ generation?: number }>).detail?.generation) || 0;
}
function notePowerTransitionGeneration(e: Event): number {
  resumeQueue.invalidate();
  const generation = powerEventGeneration(e);
  if (generation > 0) {
    latestResumeGeneration = Math.max(latestResumeGeneration, generation);
    if (resumeRetryTimer) {
      clearTimeout(resumeRetryTimer);
      resumeRetryTimer = null;
    }
    for (const oldGeneration of resumeRetryCounts.keys()) {
      if (oldGeneration < generation) resumeRetryCounts.delete(oldGeneration);
    }
  }
  return generation;
}
function onPowerSuspending(e: Event): void {
  tdpRuntimeStore.invalidatePower();
  const generation = notePowerTransitionGeneration(e);
  if (Number.isSafeInteger(generation) && generation > 0 && generation >= fanHostLifecycle.coordinatorSnapshot.powerGeneration) {
    // Retain the cancellation scope even if the Fan-specific suspend notice
    // never arrives. A delayed wake must not recapture it after explicit OFF.
    rememberFanWakeRequest(generation).startupSuspendBound = true;
    fanHostLifecycle.setPowerGeneration(generation);
    fanHostLifecycle.observePowerBoundary('suspending', generation);
    setFanControlActive(false);
  }
  // F4 has exactly one production lifecycle authority: native sends the
  // generation-tagged /api/suspend command. The renderer only freezes local
  // UI writes and keeps its generation/coordinator view coherent; it must not
  // race native with a second HC CurrentDevice.Close request.
  // FanView can be evicted from KeepAlive, so the root owns the navigation
  // indicator at the power boundary as well.
  if (resumeVideoTimer !== null) {
    window.clearTimeout(resumeVideoTimer);
    resumeVideoTimer = null;
  }
  // Do not leave a decoder/HLS pipeline alive across S3. A stale media
  // pipeline can leave WebView2 visible while its page input is unavailable.
  suspendBackgroundVideo();
}
function onFanPhysicalSuspending(e: Event): void {
  const detail = (e as CustomEvent<{ generation?: number; scope?: string; resumeCatchup?: boolean }>).detail;
  const generation = Number(detail?.generation) || 0;
  const coordinator = fanHostLifecycle.coordinatorSnapshot;
  if (!Number.isSafeInteger(generation) || generation <= 0 || generation < coordinator.powerGeneration) return;
  // This is still native-owned Close; only its explicit wake catchup keeps
  // the current request. A genuine sleep notification always supersedes it.
  const fanOnly = detail?.scope === 'fan-only';
  const resumeCatchup = fanOnly && detail?.resumeCatchup === true;
  // Native gives a subsequent physical sleep a new Fan generation. Once this
  // generation is Ready, even a delayed wake Close catchup belongs to the old lease.
  if (fanOnly && coordinator.phase === 'ready'
      && generation === coordinator.wakeReadyGeneration) return;
  fanHostLifecycle.setPowerGeneration(generation);
  fanHostLifecycle.observePowerBoundary(resumeCatchup ? 'manual-resume-start' : 'suspending', generation);
  if (fanOnly && !resumeCatchup) rememberFanWakeRequest(generation).startupSuspendBound = true;
  setFanControlActive(false);
}
function onSharedFanWake(state: SharedPowerLifecycleSnapshot): void {
  if (state.known !== true || state.notificationPending === true) return;
  if (state.fanWakeGeneration) onFanResumeReady(state.fanWakeGeneration, true);
  else if (resumeLifecycleActive && !aiFanMockActive && isSharedPowerLifecycleAdmitted(state))
    void fanHostLifecycle.retryCrashRecoveryAfterOrdinaryReady().catch((error) => {
      fanDiagnosticLog('lifecycle.crash-recovery-public-readback-failed', { error: String(error) });
    });
  const generation = state.fanStartupWakeGeneration;
  if (!resumeLifecycleActive || aiFanMockActive || !generation || state.phase === 'suspending' ||
      generation < fanHostLifecycle.coordinatorSnapshot.powerGeneration ||
      state.suspendSerial === undefined || state.observedNotificationSequence === undefined) return;
  // The Fan generation may differ from the shared gen. Only an earlier
  // suspend receipt can authorize an automatic request after a delayed hint.
  const fanBoundary = fanWakeRequests.get(generation);
  const observedBoundary = fanBoundary?.startupSuspendBound ? fanBoundary : fanWakeRequests.get(state.generation);
  const localBoundary = observedBoundary?.startupSuspendBound ? observedBoundary : undefined;
  const startupBoundary = typeof state.fanStartupWakeBoundary === 'string' && state.fanStartupWakeBoundary || undefined;
  if (!localBoundary && !startupBoundary) return;
  // A coalesced notice can be redeemed only against the original Native boundary.
  // For this path the Native OFF receipt, not the captured local revision, orders past actions.
  const cancellationRevision = fanHostLifecycle.controlCancellationRevision;
  if (localBoundary && localBoundary.cancellationRevision !== cancellationRevision) return;
  const request = fanBoundary ?? { generation, cancellationRevision, startupSuspendBound: !!localBoundary };
  if (request.cancellationRevision !== cancellationRevision) return;
  fanWakeRequests.set(generation, request);
  if (request.startupClaimInFlight || request.startupClaimAccepted || (request.startupClaimAttempts ?? 0) >= 2 ||
      fanHostLifecycle.hasControlIntent || fanHostLifecycle.hasDeferredCrashRecovery || (request.startupSuspendSerial !== undefined &&
        request.startupSuspendSerial !== state.suspendSerial)) return;
  request.startupSuspendSerial = state.suspendSerial;
  const matchesOffer = (power: SharedPowerLifecycleSnapshot | null): power is SharedPowerLifecycleSnapshot & { observedNotificationSequence: number } =>
    power?.known === true && power.notificationPending !== true && power.phase !== 'suspending' &&
    power.generation === state.generation && power.suspendSerial === state.suspendSerial &&
    power.fanStartupWakeGeneration === generation && power.observedNotificationSequence !== undefined &&
    (!startupBoundary || power.fanStartupWakeBoundary === startupBoundary);
  const stillCurrent = () => resumeLifecycleActive && !fanHostLifecycle.hasControlIntent && !fanHostLifecycle.hasDeferredCrashRecovery &&
    request.cancellationRevision === fanHostLifecycle.controlCancellationRevision &&
    generation >= fanHostLifecycle.coordinatorSnapshot.powerGeneration && fanWakeRequests.get(generation) === request;
  request.startupClaimInFlight = true;
  let preferenceReadFailed = false;
  let offReadDenied = false;
  void (async () => {
    const optIn = async () => (await readSettingsSection<{ fanControl?: boolean }>('startupDesired')).fanControl === true;
    if (!await optIn() || !stillCurrent()) return;
    if (!matchesOffer(getSharedPowerLifecycle())) return;
    const off = startupBoundary ? await fanHostLifecycle.confirmStartupWakeCancellation() : true;
    if (!stillCurrent()) return;
    if (!off) { offReadDenied = true; return; }
    const current = getSharedPowerLifecycle();
    if (!matchesOffer(current)) return;
    const claim = async (power: SharedPowerLifecycleSnapshot) => {
      request.startupClaimAttempts = (request.startupClaimAttempts ?? 0) + 1;
      const reply = await invoke<{ ok: boolean }>('power.fanClaimStartupWake', {
        generation, sharedGeneration: power.generation, suspendSerial: power.suspendSerial,
        notificationSequence: power.observedNotificationSequence,
        ...(startupBoundary ? { boundary: startupBoundary } : {}),
        ...(off === true ? {} : off),
      }, { timeoutMs: 3000 }).catch(() => null);
      if (reply?.ok) request.startupClaimAccepted = true;
    };
    await claim(current);
    if (request.startupClaimAccepted || !stillCurrent() || (request.startupClaimAttempts ?? 0) >= 2) return;
    if (!await optIn() || !stillCurrent()) return;
    // A lost reply is not permission to repeat work. The Native candidate is
    // absent once claimed. Read after settings so the retry uses the current receipt.
    const beforeRead = getSharedPowerLifecycle();
    if (beforeRead?.scopeReadbackPending) return;
    const readback = new AbortController();
    const stopReadback = onSharedPowerLifecycle(power => {
      // Ready can be committed in the same notification scope.
      const admissionOpened = matchesOffer(power) && isSharedPowerLifecycleAdmitted(power) &&
        !isSharedPowerLifecycleAdmitted(beforeRead);
      if (!stillCurrent() || power.scopeReadbackPending || (beforeRead && (
          power.generation !== beforeRead.generation || power.suspendSerial !== beforeRead.suspendSerial ||
          power.observedNotificationSequence !== beforeRead.observedNotificationSequence)) || admissionOpened) readback.abort();
    }, { observeSuspendSerial: true, observeNotificationSequence: true });
    let latest: SharedPowerLifecycleSnapshot | null;
    try {
      latest = await invoke<SharedPowerLifecycleSnapshot>('power.lifecycle', {}, {
        timeoutMs: 3000, signal: readback.signal,
      }).catch(() => null);
    } finally { stopReadback(); }
    if (!matchesOffer(latest) || !stillCurrent()) return;
    // A newer publication can arrive before the older read reply. It may
    // replace the offer or fence it; neither should spend the last claim on the old receipt.
    const published = getSharedPowerLifecycle();
    if (published?.scopeReadbackPending) return;
    const receipt = published && (
      (published.observedNotificationSequence ?? -1) > latest.observedNotificationSequence ||
      (latest.publicationRevision !== undefined && (published.publicationRevision ?? -1) > latest.publicationRevision)
    ) ? published : latest;
    if (matchesOffer(receipt)) await claim(receipt);
  })().catch(error => {
    preferenceReadFailed = true;
    fanDiagnosticLog('lifecycle.startup-wake-claim-failed', { generation, error: String(error) });
  }).finally(() => {
    request.startupClaimInFlight = false;
    // A newer offer may have arrived while the preference read was still
    // in flight. Drain it once even if the failed read never reached IPC.
    const attempts = request.startupClaimAttempts ?? 0;
    const current = getSharedPowerLifecycle();
    // Native can commit Ready without another ingress notification.
    const admissionOpened = !isSharedPowerLifecycleAdmitted(state) && isSharedPowerLifecycleAdmitted(current);
    if (!request.startupClaimAccepted && !request.startupClaimRedeliveryUsed &&
        (attempts === 1 || (attempts === 0 && (preferenceReadFailed || offReadDenied))) && stillCurrent() &&
        matchesOffer(current) && (current.observedNotificationSequence !== state.observedNotificationSequence || admissionOpened)) {
      request.startupClaimRedeliveryUsed = true;
      onSharedFanWake(current);
    }
  });
}
function onPowerResuming(e: Event): void {
  tdpRuntimeStore.invalidatePower();
  const generation = notePowerTransitionGeneration(e);
  // Start the same Fan wake request at the public wake boundary. An OFF
  // before its first delayed F5 notification must still cancel that request.
  if (Number.isSafeInteger(generation) && generation > 0 && generation >= fanHostLifecycle.coordinatorSnapshot.powerGeneration)
    rememberFanWakeRequest(generation);
  // A shared Resuming fact must not revoke this generation's independently
  // issued Fan F5 readiness. A new generation still retires the old device work.
  if (generation > fanHostLifecycle.coordinatorSnapshot.wakeReadyGeneration)
    fanHostLifecycle.observePowerBoundary('resuming', generation);
  if (resumeVideoTimer !== null) {
    window.clearTimeout(resumeVideoTimer);
    resumeVideoTimer = null;
  }
  suspendBackgroundVideo();
}
function onPowerResumeReady(generation: number | undefined): void {
  const resumeGeneration = Number(generation) || 0;
  if (resumeGeneration <= 0) return;
  latestResumeGeneration = Math.max(latestResumeGeneration, resumeGeneration);
  onFanResumeReady(resumeGeneration);
  scheduleResumeTransaction(resumeGeneration);
}

// Public Ready can replace a lost notification, but not the Fan readiness read.
function requestFanCommittedResume(generation: number): void {
  onFanResumeReady(generation, true);
}
// FAN-938 R4: a Fan-only Ready notification must not restart the shared input/UI transaction.
function onFanResumeReady(resumeGeneration: number, fromPublicReady = false): void {
  if (!resumeLifecycleActive || !Number.isSafeInteger(resumeGeneration) || resumeGeneration <= 0) return;
  if (resumeGeneration < fanHostLifecycle.coordinatorSnapshot.powerGeneration) return;
  const cancellationRevision = fanHostLifecycle.controlCancellationRevision;
  // The notification may be ahead of the current Fan fact. It cannot replace
  // another generation's cancellation receipt or suppress its confirmed wake.
  const request = rememberFanWakeRequest(resumeGeneration);
  if (cancellationRevision !== request.cancellationRevision) return;
  // Public Ready can join a read begun by the Fan notification. Keep its
  // bounded continuation on the same request even if the first read fails.
  if (fromPublicReady) request.committedWake = true;
  if (request.nativeReadyInFlight) {
    request.nativeReadyRedelivered = true;
    return;
  }
  if (request.nativeReadyConfirmed && fanHostLifecycle.coordinatorSnapshot.wakeReadyGeneration === resumeGeneration) {
    if (fromPublicReady) void fanHostLifecycle.retryCrashRecoveryAfterPublicWake(resumeGeneration).catch((error) => {
      fanDiagnosticLog('lifecycle.crash-recovery-public-wake-failed', {
        generation: resumeGeneration,
        error: error instanceof Error ? error.message : String(error),
      });
    });
    return;
  }
  request.nativeReadyConfirmed = false;
  request.nativeReadyRedelivered = false;
  // F5 remains a device-session hint. The lifecycle reads the shared Native
  // context before admitting it; late/unknown/pending events grant no wake.
  // HC SystemReady opens the device fire-and-forget: start the fan rebuild in
  // the background and never gate the native Ready commit on it. The fan
  // coordinator keeps the fan write gate closed until this rebuild completes;
  // the guard/mount compensation recover a lost rebuild without blocking wake.
  // FAN-932 §2.2：唤醒必须先**完成**现有 resume（含已有活动曲线的恢复），再按偏好自动启动。
  // 本事件处理器保持非阻塞（按代异步事务），不改成同步等待；resume 失败/被取代时
  // 本代**不做默认曲线 fallback**，也不覆盖用户曲线。
  const startedAt = getSharedPowerLifecycle();
  const stillCurrent = () => resumeLifecycleActive && fanWakeRequests.get(resumeGeneration) === request &&
    cancellationRevision === fanHostLifecycle.controlCancellationRevision &&
    resumeGeneration >= fanHostLifecycle.coordinatorSnapshot.powerGeneration;
  const attempt = (mayWaitForPublic = true) => {
    request.nativeReadyInFlight = true;
    const committedWake = request.committedWake === true;
    void fanHostLifecycle.resumeAfterNativeReady(resumeGeneration, committedWake).then((resumeOk) => {
      request.nativeReadyInFlight = false;
      if (!stillCurrent()) return;
      if (resumeOk) {
        request.nativeReadyConfirmed = true;
        request.retryAfterPublicReady = undefined;
        request.publicRetryUsed = true;
        if (fanHostLifecycle.hasControlIntent) return;
        return ensureFanAutoStart('resume', resumeGeneration);
      }
      if (!mayWaitForPublic || request.publicRetryUsed) return;
      // A scoped receipt can join an in-flight hint while the shared phase
      // remains Suspended. Hand it off once to the bounded Fan readback.
      if (!committedWake && request.committedWake) {
        request.retryAfterPublicReady = undefined;
        request.publicRetryUsed = true;
        attempt(false);
        return;
      }
      if (request.retryAfterPublicReady) {
        request.retryAfterPublicReady(getSharedPowerLifecycle());
        return;
      }
      // A private Fan generation may be newer than the public Ready generation.
      // Keep the rejected hint and make one fresh Fan read after public admission advances.
      request.retryAfterPublicReady = (power) => {
        if (!stillCurrent()) { request.retryAfterPublicReady = undefined; return; }
        if (!power || !isSharedPowerLifecycleAdmitted(power)) return;
        if (power.generation > resumeGeneration) { request.retryAfterPublicReady = undefined; return; }
        if (request.nativeReadyInFlight) return;
        if (!request.nativeReadyRedelivered && startedAt && isSharedPowerLifecycleAdmitted(startedAt) && power.generation === startedAt.generation &&
            power.observedNotificationSequence === startedAt.observedNotificationSequence &&
            power.suspendSerial === startedAt.suspendSerial &&
            power.publicationRevision === startedAt.publicationRevision) return;
        request.retryAfterPublicReady = undefined;
        request.publicRetryUsed = true;
        attempt(false);
      };
      // Public Ready can arrive while the first Fan read is still returning.
      request.retryAfterPublicReady(getSharedPowerLifecycle());
    }, (error) => {
      request.nativeReadyInFlight = false;
      throw error;
    }).catch((error) => {
      fanDiagnosticLog('lifecycle.resume-fire-and-forget-failed', {
        generation: resumeGeneration,
        error: error instanceof Error ? error.message : String(error),
      });
    });
  };
  attempt();
}
function onPowerResumed(e: Event): void {
  // A few native builds omitted the generation on the final commit event even
  // though the preceding resume edge had already recorded it.  Reuse the
  // durable latest generation instead of silently dropping Fan compensation.
  const generation = powerEventGeneration(e) || latestResumeGeneration;
  latestResumeGeneration = Math.max(latestResumeGeneration, generation);
  fanHostLifecycle.setPowerGeneration(generation);
  fanHostLifecycle.observePowerBoundary('resumed', generation);
  // F5 is normally admitted by `power.resume-ready`, but that notification is
  // one-shot and may be lost while WebView2 is recreated.  The committed
  // native edge is the Fan-only compensation path; FanHost shares any already
  // running F5 promise so this never opens HC twice.
  requestFanCommittedResume(generation);
  // The shared native Ready fact also compensates a lost resume-ready event.
  // This queue coalesces the existing task; Fan recovery remains independent.
  scheduleResumeTransaction(generation);
  scheduleSleepPowerPlanOptimization();
  if (resumeVideoTimer !== null) window.clearTimeout(resumeVideoTimer);
  resumeVideoTimer = window.setTimeout(() => {
    resumeVideoTimer = null;
    if (documentVisible.value && nativeWindowVisible.value) resumeBackgroundVideo();
  }, 250);
  requestVideoPowerCheck();
}
function onBackgroundVideoPauseChanged(e: Event): void {
  backgroundVideoAutoPause.value = Boolean(
    (e as CustomEvent<{ enabled?: boolean }>).detail?.enabled,
  );
  if (!backgroundVideoAutoPause.value) lowBatteryStatic.value = false;
  updateLowBatteryStatic(topMonitorData.value);
  reconcileBackgroundLifecycle();
}
function onUiSettingsLoaded(): void {
  backgroundPosition.value = getBackgroundPosition();
  backgroundOpacity.value = getBackgroundOpacity();
  backgroundBlur.value = getBackgroundBlur();
  backgroundVideoAutoPause.value = getUiSetting('videoBatteryPause');
  updateLowBatteryStatic(topMonitorData.value);
}
// ── UI 缩放层：设计基准 580×780，整体在窗口中居中显示 ──
// 用户偏好 2026-08-04：在原本「按窗口高度贴齐」基础上再放大 30%，居中后左右两侧留空更协调。
// 保留原缩放风格；stage 和内页滚动视口始终止于当前可视边界，避免控件落在滚动区外。
const layoutBaseWidth = computed(() => ymccDesignWidth(router.currentRoute.value.path));
const BASE_H = YMCC_SHELL_BASE_HEIGHT;
const SCALE_BOOST = YMCC_SHELL_SCALE_BOOST; // "再放大 30%"
const viewportWidth = ref(window.innerWidth);
const viewportHeight = ref(window.innerHeight);
const uiScale = ref(scaleForViewport(viewportWidth.value, viewportHeight.value));
setUiScale(uiScale.value);
// KeepAlive preserves page data, while the shared scroll viewport must start
// at the new page title. Do this before the controller's first-focus pass.
watch(() => router.currentRoute.value.path, async () => {
  await nextTick();
  resetInnerPageViewport();
}, { flush: 'post' });
// Native sizes bridge the first few frames of WebView2 restoration. They are
// physical pixels at the DPR at receipt, not a permanent maximum over DOM size.
const nativeViewport = ref<{ w: number; h: number; dpr: number } | null>(null);
let nativeViewportFrames = 0;
const NATIVE_VIEWPORT_GRACE_FRAMES = 3;
function setUiScale(scale: number) {
  uiScale.value = scale;
  // Teleported popups share the shell's zoom without inheriting its CSS tree.
  const value = String(scale);
  if (document.documentElement.style.getPropertyValue('--ui-scale') !== value)
    document.documentElement.style.setProperty('--ui-scale', value);
}
function validViewport(w: number, h: number): boolean {
  return Number.isFinite(w) && Number.isFinite(h) && w > 0 && h > 0;
}
function viewportDpr(): number {
  const dpr = window.devicePixelRatio;
  return Number.isFinite(dpr) && dpr > 0 ? dpr : 1;
}
function viewportSize(): { w: number; h: number } {
  const root = document.documentElement;
  const vv = window.visualViewport;
  // Keep width/height from one coordinate space. Per-axis maxima can invent
  // a viewport that never existed during rotation or a monitor/DPI change.
  const dom = root && validViewport(root.clientWidth, root.clientHeight)
    ? { w: root.clientWidth, h: root.clientHeight }
    : validViewport(window.innerWidth, window.innerHeight)
      ? { w: window.innerWidth, h: window.innerHeight }
      : vv && validViewport(vv.width, vv.height) ? { w: vv.width, h: vv.height } : null;
  const sample = isStandaloneEditor.value ? null : nativeViewport.value;
  // A cached physical sample must not be reinterpreted at a new display DPR.
  if (sample && sample.dpr === viewportDpr() && (nativeViewportFrames > 0 || !dom)) {
    return { w: sample.w / sample.dpr, h: sample.h / sample.dpr };
  }
  // Root client size defines the viewport for the fixed shell. Other DOM
  // observations may lag; a stale larger observation cannot pin its size.
  return dom ?? { w: viewportWidth.value, h: viewportHeight.value };
}
function resetViewportScrollTail() {
  document.querySelector<HTMLElement>('.app-content')?.style.removeProperty('--gamepad-clip-bottom');
}
function updateScale() {
  const { w, h } = viewportSize();
  const changed = w !== viewportWidth.value || h !== viewportHeight.value;
  if (w !== viewportWidth.value || h !== viewportHeight.value) resetViewportScrollTail();
  viewportWidth.value = w;
  viewportHeight.value = h;
  setUiScale(scaleForViewport(w, h));
  return changed;
}
// One scale for the initial shell and later viewport corrections.
function scaleForViewport(w: number, h: number): number {
  if (h <= 0 || w <= 0) return 1;
  return Math.min((h / BASE_H) * SCALE_BOOST, w / YMCC_SHELL_BASE_WIDTH);
}
// 呼出/尺寸变化后视口读数可能分多帧才稳定（WebView2 隐藏→恢复、DWM 合成延迟）。
// 按「帧数」而非墙钟时间收尾：窗口隐藏时 rAF 会被暂停，墙钟计时会提前结束。
let settleFrames = 0;
let settleRaf = 0;
const STABLE_VIEWPORT_FRAMES = 4;
let stableViewportFrames = 0;
function settleScale(frames = 90) {
  stableViewportFrames = 0;
  if (settleRaf) {
    settleFrames = Math.max(settleFrames, frames);
    return;
  }
  settleFrames = frames;
  const step = () => {
    if (nativeViewportFrames > 0) nativeViewportFrames -= 1;
    const changed = updateScale();
    stableViewportFrames = changed || nativeViewportFrames > 0 ? 0 : stableViewportFrames + 1;
    settleFrames -= 1;
    // ResizeObserver/native/visibility events re-arm a later real change. Do
    // not poll a stable viewport for 90 frames or rewrite identical CSS zoom.
    if (stableViewportFrames >= STABLE_VIEWPORT_FRAMES) settleFrames = 0;
    if (settleFrames > 0) settleRaf = requestAnimationFrame(step);
    else settleRaf = 0;
  };
  settleRaf = requestAnimationFrame(step);
}
// Route changes only adapt stage width; they must not alter the shared UI scale.
function onNativeResize(e: Event) {
  const detail = (e as CustomEvent<{ w?: number; h?: number }>).detail;
  if (!isStandaloneEditor.value && detail && validViewport(Number(detail.w), Number(detail.h))) {
    nativeViewport.value = { w: Number(detail.w), h: Number(detail.h), dpr: viewportDpr() };
    nativeViewportFrames = NATIVE_VIEWPORT_GRACE_FRAMES;
  }
  updateScale();
  settleScale();
}
// 呼出后 WebView2 才恢复渲染，此时原生尺寸消息可能晚于 DOM 视口更新到达。跨多帧重算
// 兜底，确保视口稳定后再定稿缩放与 stage 高度。
function refreshScaleAfterSummon() {
  resetViewportScrollTail();
  nativeViewportFrames = 0;
  updateScale();
  settleScale();
}
function refreshScaleWhenVisible() {
  if (document.visibilityState === 'visible') refreshScaleAfterSummon();
}
const scalerStyle = computed(() => {
  if (isStandaloneEditor.value) {
    return {
      width: '100%',
      height: '100%',
      zoom: 1,
      '--gamepad-safe-bottom': '0px',
    };
  }
  const scale = Math.max(uiScale.value, 0.01);
  // The zoom is visual policy; the scroll viewport must still fit the actual
  // client area, including wide/rotated windows. A minimum design height can
  // otherwise clip controls outside the only scroll layer. Keep subpixel
  // precision so rounding up does not create a clipped tail at high DPI.
  const stageHeight = viewportHeight.value / scale;
  return {
    width: ymccStageWidth(layoutBaseWidth.value, viewportWidth.value, scale) + 'px',
    height: stageHeight + 'px',
    // zoom 同时参与布局和命中测试；transform 只放大绘制层，会让 WebView2
    // 看到的坐标与鼠标点击坐标分离，表现为内容区"看得到但点不到"。
    zoom: scale,
    // Keep the physical safe area proportional to the viewport, then convert
    // it back to the zoomed layout coordinate space used by app-content.
    '--gamepad-safe-bottom': `${Math.max(32, Math.min(64, viewportHeight.value * 0.06)) / Math.max(0.5, uiScale.value)}px`,
  };
});

function onGamepadRefresh() {
  globalRefreshKey.value++;
}
// ── 手柄后台（Start+上/下）：自动/手动都调节当前侧的连续 TDP 保存值 ──
function onGamepadTdpDelta(e: Event) {
  const detail = (e as CustomEvent<{ delta?: number; feedbackId?: string; powerScope?: unknown }>).detail;
  const d = Number(detail?.delta) || 0;
  const scope = detail?.powerScope, id = detail?.feedbackId;
  if (!d || typeof id !== 'string' || !id || !Array.isArray(scope) || scope.length !== 3 ||
      !scope.every(value => Number.isSafeInteger(value) && value >= 0) || scope[0] <= 0) return;
  const origin: SharedPowerWriteScope = [scope[0], scope[1], scope[2]];
  enqueueGamepadTaskDetached(() => applyGamepadTdp(d, id, origin));
}
async function applyGamepadTdp(delta: number, feedbackId: string, origin: SharedPowerWriteScope) {
  const controller = new AbortController(), { signal } = controller;
  let confirmed = false;
  const matches = (state: SharedPowerLifecycleSnapshot | null) => !state?.scopeReadbackPending &&
    sharedPowerWriteScope(state)?.every((value, index) => value === origin[index]) === true;
  const admit = () => !signal.aborted && matches(getSharedPowerLifecycle());
  const off = onSharedPowerLifecycle(state => {
    if (confirmed ? !admit() : state.generation > origin[0] || (state.suspendSerial ?? -1) > origin[1] ||
        (state.observedNotificationSequence ?? -1) > origin[2]) controller.abort();
  }, { observeSuspendSerial: true, observeNotificationSequence: true });
  const read = async <T>(work: () => Promise<T>): Promise<T> => {
    if (signal.aborted) throw new SkipApplyError();
    let retire!: () => void;
    const retired = new Promise<never>((_, reject) => { retire = () => reject(new SkipApplyError()); });
    signal.addEventListener('abort', retire, { once: true });
    try { return await Promise.race([work(), retired]); }
    finally { signal.removeEventListener('abort', retire); }
  };
  const confirm = async () => {
    const accept = beginSharedPowerLifecycleReadback();
    const state = await read(() => invoke<SharedPowerLifecycleSnapshot>('power.lifecycle', {}, { signal, timeoutMs: 3000 }));
    accept(state);
    if (!matches(state) || !admit()) throw new SkipApplyError();
    confirmed = true;
  };
  const complete = async (outcome: Record<string, unknown>) => admit() &&
    (await invoke<{ok:boolean}>('input.shortcutAction.complete', {id: feedbackId, outcome}, {timeoutMs: 3000}).catch(() => null))?.ok === true;
  try {
    if (!(await read(() => invoke<{ok:boolean}>('input.shortcutAction.claim', {id:feedbackId}, {signal, timeoutMs: 3000}))).ok) return;
    await confirm();
    const result = await adjustContinuousPerformanceTdp(delta, { admit, read, confirm });
    if (!admit()) return;
    if (result.applied) {
      if (!await complete({state:'applied',title:'TDP',message:Number.isFinite(result.tdp)?'TDP 已调整为 '+result.tdp+' W。':'TDP 已调整。',...(Number.isFinite(result.tdp)?{value:{text:result.tdp+' W',number:result.tdp,unit:'W'}}:{})}) || !admit()) return;
      globalRefreshKey.value++;
      window.dispatchEvent(new CustomEvent('gamepad:performance-mode-changed', {
        detail: { side: result.side },
      }));
    } else {
      await complete({state:'failed',title:'TDP',message:result.blocked==='boundary'?'已到 TDP 调节边界，本次未更改。':'TDP 本次未调整，请稍后重试。',silent:result.blocked==='cooldown'});
    }
  } catch (e) {
    if (signal.aborted || e instanceof SkipApplyError) return;
    await complete({state:'failed',title:'TDP',message:'TDP 调整未确认完成，请检查当前性能状态。'});
    console.error('[gamepad tdp-delta]', e);
  } finally {
    off();
  }
}
// 手柄后台（Start+左/右）：native 直接按当前野蛮系统电源 AC/DC 侧调节亮度。
function onGamepadBrightness(e: Event) {
  const detail = (e as CustomEvent<{ ok?: boolean; value?: number; mode?: 'ac' | 'dc'; reason?: string }>).detail || {};
  if (detail.ok === false && detail.reason === 'not-yeman') {
    console.info('[gamepad brightness] ignored: current power scheme is not YeMan');
  }
  globalRefreshKey.value++;
}
function onGamepadMouseToggle(e: Event) {
  const detail = (e as CustomEvent<Partial<MouseModeState>>).detail || {};
  if (!acceptMouseModeState(detail)) return;
  if (detail.ok === false) {
    window.dispatchEvent(new CustomEvent('mouse-mode:error', { detail }));
  }
  if (typeof detail.on !== 'boolean' || detail.stateKnown === false) return;
  window.dispatchEvent(new CustomEvent('gp:mouse-mode', { detail: { ...detail } }));
}
function onFanGuardState(e: Event): void {
  const detail = (e as CustomEvent<{ active?: boolean }>).detail ?? {};
  setFanControlActive(detail.active === true);
}

async function applyProgramControls(isCurrent: () => boolean) {
  // Hardware performance ownership is restored only by the continuous
  // performance schedule. Keep this boot hook for the independent FPS owner;
  // it must not reapply a legacy TDP/CPU setting when no schedule exists.
  void applyIndependentFrameRates(isCurrent).catch(() => {});
}


let stopGamepad: (() => void) | null = null;
let stopGyroTelemetry: (() => void) | null = null;
// 手柄后台（未开浮动时）调节 RTSS 帧率上限：setRtssLimit 较重（LoadProfile+UpdateProfiles），
// 连发时每步重载会卡出转圈；2 秒尾随防抖，与 CPU 滑块 scheduleActivate 一致。
// 壳订阅系统 AC/DC 插拔事件（推送式，已在 native 侧做 5s 尾防抖 + 频繁切换熔断），
// 到达即刷新一次所有已挂载页面数据。
let stopAcWatch: (() => void) | null = null;
let stopPowerSourceWatch: (() => void) | null = null;
let stopForegroundPowerSource: (() => void) | null = null;
let stopFanResumeWatch: (() => void) | null = null;
let stopSchemeWatch: (() => void) | null = null;
let stopStartupPerformanceWatch: (() => void) | null = null;
let stopDynamicBackground: (() => void) | null = null;
let stopUiVisibility: (() => void) | null = null;
let sleepPlanOptimizationTimer: number | null = null;
const sleepPlanOptimizationQueue = createPowerResumeQueue();
let sleepPlanOptimizationRequest: object | null = null;
let sleepPlanOptimizationConfirmation: {
  request: object; captured: SharedPowerLifecycleSnapshot; denied: boolean; stop: () => void;
} | null = null;

function sameSleepPowerPlanScope(a: SharedPowerLifecycleSnapshot, b: SharedPowerLifecycleSnapshot): boolean {
  return a.generation === b.generation && a.suspendSerial === b.suspendSerial &&
    a.observedNotificationSequence === b.observedNotificationSequence;
}
function confirmSleepPowerPlanOptimization(request: object, captured: SharedPowerLifecycleSnapshot, ownsQueue: () => boolean): void {
  if (!resumeLifecycleActive || !ownsQueue() || !sleepPlanOptimizationRequest || sleepPlanOptimizationConfirmation) return;
  // Retire this attempt before asking the existing public source for a new read.
  // A cached Ready/event never stands in for this request's confirmation.
  sleepPlanOptimizationQueue.invalidate();
  const confirmation = { request, captured, denied: false, stop: () => {} };
  sleepPlanOptimizationConfirmation = confirmation;
  confirmation.stop = requestSharedPowerLifecycleReadyConfirmation(state => {
    if (!resumeLifecycleActive || sleepPlanOptimizationConfirmation !== confirmation) return;
    sleepPlanOptimizationConfirmation = null;
    if (!sleepPlanOptimizationRequest) return;
    // An unchanged fresh Ready means an ordinary failed attempt, not a retry
    // grant. Only a changed public scope or a newer queued request continues.
    if (sleepPlanOptimizationRequest === request && !confirmation.denied && sameSleepPowerPlanScope(state, captured)) {
      sleepPlanOptimizationRequest = null;
      return;
    }
    continueSleepPowerPlanOptimization();
  });
}

// This background consumer uses the public wake queue and admission. A denied
// owner retires permanently; its demand waits for a later admitted observation.
function scheduleSleepPowerPlanOptimization(): void {
  if (!resumeLifecycleActive) return;
  sleepPlanOptimizationRequest = {};
  continueSleepPowerPlanOptimization();
}
function continueSleepPowerPlanOptimization(): void {
  const state = getSharedPowerLifecycle();
  if (!resumeLifecycleActive || !sleepPlanOptimizationRequest || !isSharedPowerLifecycleAdmitted(state)) return;
  const confirmation = sleepPlanOptimizationConfirmation;
  if (confirmation) {
    if (!confirmation.denied && sameSleepPowerPlanScope(state!, confirmation.captured)) return;
    // A newer admitted public scope can supersede a held old read immediately.
    sleepPlanOptimizationConfirmation = null;
    confirmation.stop();
  }
  const generation = state!.generation;
  void sleepPlanOptimizationQueue.run(generation, async ownsQueue => {
    const request = sleepPlanOptimizationRequest;
    const captured = getSharedPowerLifecycle();
    const isCurrent = () => {
      const current = getSharedPowerLifecycle();
      return ownsQueue() && resumeLifecycleActive && !!captured && !!current &&
        isSharedPowerLifecycleAdmitted(current) && current.generation === generation && sameSleepPowerPlanScope(current, captured);
    };
    if (!request || !isCurrent()) return;
    try {
      const enabled = await getSleepPowerPlanOptimizationEnabled();
      if (!isCurrent()) return;
      if (enabled) {
        const sequence = captured?.observedNotificationSequence;
        const suspendSerial = captured?.suspendSerial;
        if (sequence === undefined) throw new Error('睡眠电源计划优化缺少公共通知序号');
        if (suspendSerial === undefined) throw new Error('睡眠电源计划优化缺少公共挂起序号');
        const result = await optimizeSleepPowerPlans({ expectedPowerGeneration: generation, expectedPowerNotificationSequence: sequence,
          expectedPowerSuspendSerial: suspendSerial, isCurrent });
        if (!isCurrent()) return;
        if (!result.ok && (!result.stopped || result.failed.length)) console.error('[sleep power plan optimization]', result.failed[0]);
      }
      if (sleepPlanOptimizationRequest === request) sleepPlanOptimizationRequest = null;
    } catch (error) {
      if (!ownsQueue() || !resumeLifecycleActive) return;
      console.error('[sleep power plan optimization]', error);
      if (captured) confirmSleepPowerPlanOptimization(request, captured, ownsQueue);
    } finally {
      // Metadata-only ingress updates do not fan out business callbacks. A
      // retired read still owns demand and uses the same fresh-read channel.
      if (captured && ownsQueue() && resumeLifecycleActive && sleepPlanOptimizationRequest && !isCurrent())
        confirmSleepPowerPlanOptimization(request, captured, ownsQueue);
    }
  }, () => resumeLifecycleActive && sleepPlanOptimizationRequest !== null &&
    isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())).catch(error => {
    console.error('[sleep power plan optimization queue]', error);
  });
}
let latestResumeGeneration = 0;
let committedResumeGeneration = 0;
// This consumer retains only the exact stages of its already committed wake.
// Shared Native admission and the original queue still own every continuation.
let resumeFollowup: {
  generation: number; mode: 'ac' | 'dc'; completed: boolean;
  transaction?: ReturnType<typeof runPowerResumeTransaction>;
  scheme?: Promise<WakeSchemeResult>;
  performance?: Promise<WakePerformanceResult>;
  waitingForGame?: boolean;
  legacy?: Promise<void>;
} | null = null;
let stopResumeGameReadinessWatch: (() => void) | null = null;
let resumeLifecycleActive = true;
let resumeWorkAllowed = false;
// Recovery notifications are normally delivered as events. WebView2 can be
// recreated during the same window, however, and a freshly mounted page may
// miss the one-shot `power.resume-ready` event. Coalesce compensating
// recovery attempts and let newer power generations supersede pending work.
const resumeQueue = createPowerResumeQueue();
let resumeRetryTimer: ReturnType<typeof setTimeout> | null = null;
const resumeRetryCounts = new Map<number, number>();
const MAX_RESUME_TRANSACTION_RETRIES = 3;

function scheduleResumeRetry(generation: number, reason: string): void {
  if (!resumeLifecycleActive || !resumeWorkAllowed || generation !== latestResumeGeneration ||
      generation <= committedResumeGeneration || resumeRetryTimer) return;
  const retryCount = (resumeRetryCounts.get(generation) ?? 0) + 1;
  resumeRetryCounts.set(generation, retryCount);
  if (retryCount <= MAX_RESUME_TRANSACTION_RETRIES) {
    resumeRetryTimer = setTimeout(() => {
      resumeRetryTimer = null;
      scheduleResumeTransaction(generation);
    }, Math.min(2000, 500 * retryCount));
    return;
  }
  window.dispatchEvent(new CustomEvent('power.resume-degraded', {
    detail: { generation, reason },
  }));
}

function matchesOrdinaryPowerScope(scope: SharedPowerWriteScope, state = getSharedPowerLifecycle()): boolean {
  return !state?.scopeReadbackPending && sharedPowerWriteScope(state)?.every((value, index) => value === scope[index]) === true;
}

async function powerLifecycleReadyForWrites(scope = sharedPowerWriteScope(getSharedPowerLifecycle()), signal?: AbortSignal): Promise<boolean> {
  if (!scope || !resumeLifecycleActive || signal?.aborted || !matchesOrdinaryPowerScope(scope)) return false;
  const accept = beginSharedPowerLifecycleReadback();
  const state = await powerLifecycle.get(undefined, signal).catch(() => null);
  if (!resumeLifecycleActive || signal?.aborted || !matchesOrdinaryPowerScope(scope)) return false;
  accept(state);
  return matchesOrdinaryPowerScope(scope, state) && matchesOrdinaryPowerScope(scope);
}

async function runOrdinaryPowerRestore(work: (scope: SharedPowerWriteScope, current: () => boolean, signal: AbortSignal) => Promise<void>): Promise<void> {
  const scope = sharedPowerWriteScope(getSharedPowerLifecycle());
  if (!scope) return;
  const controller = new AbortController(), { signal } = controller;
  const current = () => !signal.aborted && resumeLifecycleActive && matchesOrdinaryPowerScope(scope);
  const off = onSharedPowerLifecycle(() => { if (!current()) controller.abort(); },
    { observeSuspendSerial: true, observeNotificationSequence: true });
  try {
    if (await powerLifecycleReadyForWrites(scope, signal) && current()) await work(scope, current, signal);
  } finally { off(); }
}

// AC/DC broadcasts can arrive while Windows/native are still settling the
// adapter state. A single read in that window can select the previous side and
// make a dedicated game profile appear stuck on battery/AC. Coalesce rapid
// events and only commit after the reported side is observed steadily enough;
// a newer event invalidates every older attempt.
const POWER_CHANGE_RETRY_DELAYS_MS = [0, 250, 750, 1500] as const;
let powerChangeRestore: { side: 'ac' | 'dc'; nextAttempt: number } | null = null;
let powerChangeRestoreScope: SharedPowerLifecycleSnapshot | null = null;
let powerChangeRestoreTimer: ReturnType<typeof setTimeout> | null = null;

function clearPowerChangeRestoreTimer(): void {
  if (powerChangeRestoreTimer !== null) {
    window.clearTimeout(powerChangeRestoreTimer);
    powerChangeRestoreTimer = null;
  }
}

function samePowerChangeRestoreScope(state: SharedPowerLifecycleSnapshot | null, expected: SharedPowerLifecycleSnapshot): boolean {
  return state !== null && isSharedPowerLifecycleAdmitted(state) && state.generation === expected.generation &&
    state.suspendSerial === expected.suspendSerial && state.observedNotificationSequence === expected.observedNotificationSequence;
}

function continuePowerChangePerformanceRestore(): void {
  const request = powerChangeRestore, scope = powerChangeRestoreScope;
  if (!request || !scope || powerChangeRestoreTimer !== null) return;
  const delay = POWER_CHANGE_RETRY_DELAYS_MS[request.nextAttempt];
  if (delay === undefined) { powerChangeRestore = null; return; }
  const current = () => powerChangeRestore === request && powerChangeRestoreScope === scope &&
    samePowerChangeRestoreScope(getSharedPowerLifecycle(), scope);
  const run = async (): Promise<void> => {
    if (!current()) return;
    ++request.nextAttempt;
    const actualSide = await detectPowerModeReliable().catch(() => null);
    if (!current()) return;
    if (actualSide !== request.side) { continuePowerChangePerformanceRestore(); return; }
    const acceptReadback = beginSharedPowerLifecycleReadback();
    const ready = await powerLifecycle.get().catch(() => null);
    if (!current()) return;
    acceptReadback(ready);
    if (!current()) return;
    if (!samePowerChangeRestoreScope(ready, scope)) { continuePowerChangePerformanceRestore(); return; }

    try {
      const schemeOwner = await reconcileRememberedPowerScheme(current);
      if (!current()) return;
      // In manual Windows-scheme mode, inspect/apply only a game-exclusive
      // entry. Do not let the ordinary global scheduler take ownership back.
      await restorePerformanceScheduleIfConfigured(request.side, {
        dedicatedOnly: schemeOwner !== 'auto',
        automaticOnly: true, restoreRemembered: true, ownsPowerRequest: current,
      });
      if (current()) powerChangeRestore = null;
    } catch (error) {
      if (current()) {
        if (error instanceof SkipApplyError || request.nextAttempt < POWER_CHANGE_RETRY_DELAYS_MS.length)
          continuePowerChangePerformanceRestore();
        else powerChangeRestore = null;
      }
    }
  };

  const timer = window.setTimeout(() => {
    if (powerChangeRestoreTimer !== timer) return;
    powerChangeRestoreTimer = null;
    void run();
  }, delay);
  powerChangeRestoreTimer = timer;
}

function observePowerChangePerformanceRestore(state: SharedPowerLifecycleSnapshot): void {
  if (powerChangeRestoreScope && samePowerChangeRestoreScope(state, powerChangeRestoreScope)) return;
  clearPowerChangeRestoreTimer();
  powerChangeRestoreScope = isSharedPowerLifecycleAdmitted(state) ? state : null;
  continuePowerChangePerformanceRestore();
}

function schedulePowerChangePerformanceRestore(side: 'ac' | 'dc'): void {
  clearPowerChangeRestoreTimer();
  powerChangeRestore = { side, nextAttempt: 0 };
  continuePowerChangePerformanceRestore();
}

function scheduleResumeTransaction(generation: number): void {
  if (!resumeLifecycleActive || !resumeWorkAllowed || !Number.isSafeInteger(generation) || generation <= 0) return;
  latestResumeGeneration = Math.max(latestResumeGeneration, generation);
  if (generation !== latestResumeGeneration) return;
  if (generation < committedResumeGeneration ||
      generation === committedResumeGeneration && (!resumeFollowup || resumeFollowup.completed)) return;

  void resumeQueue.run(generation, async (ownsQueue) => {
    const targetGeneration = generation;
    if (targetGeneration < committedResumeGeneration ||
        targetGeneration === committedResumeGeneration && (!resumeFollowup || resumeFollowup.completed)) return;
    let needsConfirmedFollowup = targetGeneration === committedResumeGeneration;
    const ownsCurrent = () => ownsQueue() && resumeLifecycleActive && resumeWorkAllowed && latestResumeGeneration === targetGeneration
      && (!needsConfirmedFollowup || isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle()));
    let followup = resumeFollowup;
    const isCurrent = () => ownsCurrent() && (committedResumeGeneration < targetGeneration ||
      followup?.generation === targetGeneration && resumeFollowup === followup);

    if (targetGeneration > committedResumeGeneration) {
      const mode = await detectPowerModeStable().catch(() => 'ac' as const);
      if (!isCurrent()) return;
      onAcPower.value = mode === 'ac';
      // Do not let a stalled media decoder delay the native wake commit.
      void reconcileBackgroundVideo();

      const daemonRequired = getFloatInfo().enabled ||
        (await getPerformanceScheduleOwnership().catch(() => 'none' as const)) === 'auto';
      if (!isCurrent()) return;
      const transaction: ReturnType<typeof runPowerResumeTransaction> = runPowerResumeTransaction(targetGeneration, daemonRequired, {
        // Fan F5 remains independent; the native Ready commit never waits for it.
        resumeDaemon: resumeTdpDaemonAfterWake,
        completeResume: (resumeGeneration, meta) => powerLifecycle.completeResume(resumeGeneration, meta).then((result) => {
          if (result.ok && isCurrent()) {
            // The daemon phase can cross a revocation. Save the Native receipt
            // now so the next queue owner can await that phase without recommitting.
            committedResumeGeneration = targetGeneration;
            followup = { generation: targetGeneration, mode, completed: false, transaction };
            resumeFollowup = followup;
            resumeRetryCounts.delete(targetGeneration);
            uiPowerReconciliation?.reconcile();
          }
          return result;
        }),
        isGenerationCurrent: isCurrent,
        daemonAttempts: 2,
        commitAttempts: 3,
        daemonRetryDelayMs: 350,
        commitRetryDelayMs: 250,
      });
      const result = await transaction;
      if (!isCurrent()) return;
      if (!result.committed) {
        scheduleResumeRetry(targetGeneration, result.reason ?? 'resume_commit_failed');
        return;
      }

      needsConfirmedFollowup = true;
    }
    if (!followup || followup.generation !== targetGeneration || !ownsCurrent()) return;
    if (followup.transaction) {
      await followup.transaction;
      if (!ownsCurrent()) return;
      followup.transaction = undefined;
      // The resident owner can keep healing even if a later scheme read fails.
      requestGamePolicyTdpReconciliation(targetGeneration);
    }
    // Keep each exact in-flight operation and its result across revocation.
    // A successor consumes it only while the original shared queue is current.
    followup.scheme ??= reconcilePowerSchemeAfterWake(ownsCurrent);
    const schemeOwner = await followup.scheme;
    if (!ownsCurrent()) return;
    if (schemeOwner === 'retry-on-ready') {
      followup.scheme = undefined;
      // A merged Ready can retire only this read while leaving the App owner current.
      scheduleResumeTransaction(targetGeneration);
      return;
    }
    if (followup.performance) {
      const previous = await followup.performance;
      if (!ownsCurrent()) return;
      if (previous === 'retry-on-ready') followup.performance = undefined;
      if (previous === 'retry-on-game-ready') {
        followup.performance = undefined;
        followup.waitingForGame = true;
      }
    }
    if (followup.waitingForGame && !isGameStatusCurrent()) return;
    followup.waitingForGame = false;
    followup.performance ??= restorePerformanceScheduleAfterWake(followup.mode, schemeOwner !== 'auto', ownsCurrent);
    const performance = await followup.performance;
    if (!ownsCurrent()) return;
    if (performance === 'retry-on-ready') {
      followup.performance = undefined;
      scheduleResumeTransaction(targetGeneration);
      return;
    }
    if (performance === 'retry-on-game-ready') {
      // The old attempt sent no writes. Recognition may already have advanced
      // while its result was resolving, so check the same authority now.
      followup.performance = undefined;
      followup.waitingForGame = true;
      onResumeGameTargetReadiness(isGameStatusCurrent());
      return;
    }
    // Retained legacy controls stay inactive even when old settings opted in.
    if (LEGACY_CCD_UNDERVOLT_ACTIVE) {
      followup.legacy ??= applyCpuAutostart().catch(() => {});
      await followup.legacy;
      if (!ownsCurrent()) return;
    }
    followup.completed = true;
  }, () => resumeLifecycleActive && resumeWorkAllowed && generation === latestResumeGeneration
    && resumeFollowup?.generation === generation && !resumeFollowup.completed
    && isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())).catch((error) => {
    scheduleResumeRetry(generation, error instanceof Error ? error.message : String(error));
  });
}

function onResumeGameTargetReadiness(current: boolean): void {
  if (!current || !resumeFollowup?.waitingForGame) return;
  if (!isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())) return;
  scheduleResumeTransaction(resumeFollowup.generation);
}

function onDynamicBackgroundSettingChanged(): void {
  const generation = ++dynamicBackgroundGeneration;
  if (dynamicBackgroundNoGameTimer !== null) {
    window.clearTimeout(dynamicBackgroundNoGameTimer);
    dynamicBackgroundNoGameTimer = null;
  }
  if (!dynamicBackgroundAllowed()) {
    dynamicOriginalFallback = null;
    stopDynamicBackgroundWatch();
    lastDynamicGameIdentity = null;
    dynamicBackgroundActive.value = false;
    // Remove the current Steam media immediately. If a fixed MP4 exists,
    // background.get below will install it as a separate, allowed source.
    applyBackgroundState(null, 'fixed');
    void backgroundGet().then((state) => {
      if (generation === dynamicBackgroundGeneration && !dynamicBackgroundAllowed()) {
        const fixedState = state || lastFixedBackgroundState;
        lastFixedBackgroundState = fixedState;
        applyBackgroundState(fixedState, 'fixed');
      }
    }).catch(() => {
      if (generation === dynamicBackgroundGeneration && !dynamicBackgroundAllowed()) {
        applyBackgroundState(lastFixedBackgroundState, 'fixed');
      }
    });
    return;
  }
  if (!isUiVisible()) return;
  startDynamicBackgroundWatch();
  void detectGame().then(async (game) => {
    if (generation !== dynamicBackgroundGeneration || !dynamicBackgroundAllowed()) return;
    if (!game) {
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: '未识别到当前游戏' }));
      return;
    }
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `正在访问 Steam：${cleanGameTitle(game.title || game.name)}` }));
    const result = await readDynamicGameBackground(game);
    if (generation !== dynamicBackgroundGeneration || !dynamicBackgroundAllowed() || !result) return;
    lastDynamicGameIdentity = `${game.pid}:${game.processCreated || ''}:${game.path || game.name}`;
    dynamicBackgroundActive.value = true;
    applyBackgroundState(result.state as BackgroundState, 'dynamic');
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
      detail: result.source === 'video'
        ? `正在缓冲在线视频：${result.gameName}`
        : `已使用 Steam 背景图：${result.gameName}`,
    }));
  }).catch((error) => {
    if (generation !== dynamicBackgroundGeneration || !dynamicBackgroundAllowed()) return;
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${(error as Error).message || 'Steam 连接失败'}` }));
  });
}
function onGameRulesChanged(): void {
  const generation = ++dynamicBackgroundGeneration;
  if (dynamicBackgroundNoGameTimer !== null) {
    window.clearTimeout(dynamicBackgroundNoGameTimer);
    dynamicBackgroundNoGameTimer = null;
  }
  lastDynamicGameIdentity = null;
  if (!dynamicBackgroundAllowed()) return;

  // A rules update is authoritative: a game that was just blacklisted must
  // not keep its already-installed Steam media through the normal brief-null
  // tolerance used while processes are starting or switching renderers.
  dynamicBackgroundActive.value = false;
  applyBackgroundState(lastFixedBackgroundState, 'fixed');
  if (lastFixedBackgroundState) return;

  // The initial fixed-background read can fail during WebView recovery. Fill
  // that snapshot opportunistically without allowing it to overwrite a newer
  // game/rules result.
  void backgroundGet().then((state) => {
    if (generation !== dynamicBackgroundGeneration || !dynamicBackgroundAllowed()) return;
    lastFixedBackgroundState = state;
    applyBackgroundState(state, 'fixed');
  }).catch(() => {});
}

function scheduleDynamicBackgroundNoGameClear(): void {
  if (dynamicBackgroundNoGameTimer !== null) return;
  dynamicBackgroundNoGameTimer = window.setTimeout(() => {
    dynamicBackgroundNoGameTimer = null;
    void detectGame(true).then((current) => {
      if (current) {
        onDynamicGameStatus(current);
        return;
      }
      if (!dynamicBackgroundAllowed()) return;
      dynamicBackgroundGeneration++;
      lastDynamicGameIdentity = null;
      dynamicBackgroundActive.value = false;
      applyBackgroundState(lastFixedBackgroundState, 'fixed');
    }).catch(() => {
      // Keep the current media during a transient valve/IPC failure. The next
      // scheduled game-status poll will make the authoritative decision.
    });
  }, DYNAMIC_BACKGROUND_NO_GAME_GRACE_MS);
}

function onDynamicGameStatus(game: DetectedGame | null): void {
  const generation = ++dynamicBackgroundGeneration;
  void (async () => {
    if (!dynamicBackgroundAllowed() || !game) {
      if (dynamicBackgroundNoGameTimer !== null) {
        window.clearTimeout(dynamicBackgroundNoGameTimer);
        dynamicBackgroundNoGameTimer = null;
      }
      // A short no-game interval is normal while a title starts or switches
      // renderers. It is not permission to retain the old title indefinitely.
      if (dynamicBackgroundAllowed() && dynamicBackgroundActive.value) {
        scheduleDynamicBackgroundNoGameClear();
        return;
      }
      lastDynamicGameIdentity = null;
      dynamicBackgroundActive.value = false;
      const state = await backgroundGet().catch(() => null);
      if (generation === dynamicBackgroundGeneration) {
        applyBackgroundState(state, 'fixed');
      }
      return;
    }
    if (dynamicBackgroundNoGameTimer !== null) {
      window.clearTimeout(dynamicBackgroundNoGameTimer);
      dynamicBackgroundNoGameTimer = null;
    }
    if (!game.processCreated) return;
    if (dynamicOriginalFallback && (dynamicOriginalFallback.pid !== game.pid ||
        dynamicOriginalFallback.processCreated !== game.processCreated)) dynamicOriginalFallback = null;
    const gameIdentity = `${game.pid}:${game.processCreated}:${game.path || game.name}`;
    if (gameIdentity === lastDynamicGameIdentity) return;
    // The old media belongs to a different valve target. Do not display it
    // while Steam data for the new target is still being resolved.
    lastDynamicGameIdentity = null;
    dynamicBackgroundActive.value = false;
    applyBackgroundState(lastFixedBackgroundState, 'fixed');
    try {
      const result = await readDynamicGameBackground(game);
      if (result && generation === dynamicBackgroundGeneration && dynamicBackgroundAllowed()) {
        lastDynamicGameIdentity = gameIdentity;
        dynamicBackgroundActive.value = true;
        applyBackgroundState(result.state as BackgroundState, 'dynamic');
      }
    } catch (error) {
      if (generation !== dynamicBackgroundGeneration || !dynamicBackgroundAllowed()) return;
      const message = (error as Error).message || 'Steam 连接失败';
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${message}` }));
      console.warn('[dynamic background]', error);
    }
  })();
}
function startDynamicBackgroundWatch(hasInitialFixedBackground = false): void {
  if (stopDynamicBackground || !dynamicBackgroundAllowed() || !isUiVisible()) return;
  stopDynamicBackground = subscribeGameStatus(onDynamicGameStatus, { skipEmptyReplay: hasInitialFixedBackground });
}
function stopDynamicBackgroundWatch(): void {
  stopDynamicBackground?.();
  stopDynamicBackground = null;
}
function onDynamicBackgroundLoaded(e: Event): void {
  const state = (e as CustomEvent<BackgroundState>).detail;
  if (!state?.url || !dynamicBackgroundAllowed() || isFailedDynamicVariant(state,dynamicOriginalFallback)) return;
  const accept = (game: DetectedGame | null) => {
    if (!game?.processCreated || Number(state.pid) !== game.pid ||
        String(state.processCreated || '') !== game.processCreated || !isCurrentGameTarget(game)) return;
    ++dynamicBackgroundGeneration;
    lastDynamicGameIdentity = `${game.pid}:${game.processCreated}:${game.path || game.name}`;
    dynamicBackgroundActive.value = true;
    applyBackgroundState(state, 'dynamic');
  };
  // The media producer already validated the native target. Reuse its
  // observation while current, including a confirmed switch or game exit.
  if (isGameStatusCurrent()) {
    accept(getCurrentGame());
    return;
  }
  const generation = ++dynamicBackgroundGeneration;
  void detectGame().then((game) => {
    if (generation === dynamicBackgroundGeneration && dynamicBackgroundAllowed()) accept(game);
  }).catch(() => {});
}
// Only the main renderer owns placement listeners; unrelated UI settings do not move it.
const windowPlacement = ref<'left' | 'right'>(getUiSetting('windowPlacement'));
async function applyWindowPlacement(): Promise<void> {
  const placed = await windowApi.place(windowPlacement.value);
  if (!placed) throw new Error('Native window placement failed');
}
function reportWindowPlacementError(error: unknown): void {
  console.warn('[window placement]', error);
}
function onWindowPlacementSettingsChanged(): void {
  const next = getUiSetting('windowPlacement');
  if (next === windowPlacement.value) return;
  windowPlacement.value = next;
  void applyWindowPlacement().catch(reportWindowPlacementError);
}
function onWindowPlacementSummoned(): void {
  void applyWindowPlacement().catch(reportWindowPlacementError);
}
onMounted(async () => {
  // A shortcut editor popup is another renderer owned by the same native
  // process, not a second YMCC application instance. Do not start the main
  // renderer's gamepad engine, FanHost lifecycle, tray/autostart jobs,
  // background watchers, or power transaction listeners in this document.
  // The editor itself talks to native IPC directly and still receives the
  // broadcast recording events it needs.
  if (isStandaloneEditor.value) {
    window.dispatchEvent(new CustomEvent('app-startup-ready'));
    return;
  }
  controllerShortcutRuntime = startControllerShortcutRuntime();
  stopDeckySafetyNotifications = startDeckySafetyNotifications();
  deckyMirrorHost = startDeckyMirrorHost();
  deckyMirrorHost.startupPhase('native-context');
  monitorHudHost = startMonitorHudHost();
  stopBootModeReceipts = observeBootModeRuntimeReceipts();
  radialHost = startRadialTestingHost(router);
  stopWallpaperPower=subscribeWallpaperPowerChanged(blocked=>{
    wallpaperPowerPaused.value=blocked;
    // Revoke all pre-unplug background/target callbacks before removing the decoder.
    ++dynamicBackgroundGeneration;lastDynamicGameIdentity=null;
    if(blocked){stopDynamicBackgroundWatch();clearBackgroundRetry();if(dynamicBackgroundNoGameTimer!==null){clearTimeout(dynamicBackgroundNoGameTimer);dynamicBackgroundNoGameTimer=null;}suspendBackgroundVideo();}
    else {reconcileBackgroundLifecycle();if(getDynamicBackgroundConfig().enabled)onDynamicBackgroundSettingChanged();}
  });
  wallpaperPowerPaused.value=wallpaperPowerBlocked();
  stopSharedGameMedia = startSharedGameMedia();
  window.addEventListener('game-media:changed',onSharedMediaChanged);
  window.addEventListener('dynamic-background:waiting-for-playback',onDynamicBackgroundWaiting);
  steamWallpaperHost = startSteamWallpaperHost();
  try {
    aiFanMockActive = parseAiFanMockSession(await app.aiFanMockSession()) !== null;
  } catch (error) {
    if (!/^unknown: app\.aiFanMockSession$/.test(String(error instanceof Error ? error.message : error))) {
      // Unknown capability state must not accidentally arm stored real fan intent.
      aiFanMockActive = true;
      fanDiagnosticLog('ai.mock-session-unreadable', { policy: 'fan-auto-start-suppressed' });
    }
  }
  // One-time default schedules next logon only; never waits on a page or runs the desktop script.
  if (!aiFanMockActive && !aiCpuIsolated) void ensureDesktopModeStartupDefault().catch(error => {
    console.warn('全局桌面模式默认开机任务未完成：', error instanceof Error ? error.message : String(error));
  });
  if (!aiFanMockActive && !aiCpuIsolated) void ensureNetworkAccelerationStartupDefault().catch(error => {
    console.warn('网络加速默认开机任务未完成：', error instanceof Error ? error.message : String(error));
  });
  if (!aiFanMockActive && !aiCpuIsolated) void ensureGameBarControllerStartup().catch(error => {
    console.warn('Game Bar 手柄入口开机设置未完成：', error instanceof Error ? error.message : String(error));
  });
  resumeLifecycleActive = true;
  // Register the one-shot native wake events before the first awaited startup
  // operation. A controller recreated during S3 recovery can otherwise miss
  // resume-ready while settings/background initialization is still running.
  window.addEventListener('ipc:fan.suspending', onFanPhysicalSuspending as EventListener);
  let sharedPowerKey = '', sharedPowerSeen = false, sharedPowerAdmitted = false, sharedF5Confirmed = false;
  let sharedSuspendScope: string | null = null;
  let backgroundRefreshOnReady = false;
  stopSharedPowerWatch = onSharedPowerLifecycle(state => {
    if (!resumeLifecycleActive) return;
    observePowerChangePerformanceRestore(state);
    const confirmedF5 = state.known === true && state.notificationPending !== true && state.resumeReady === true;
    const f5Restored = confirmedF5 && !sharedF5Confirmed;
    sharedF5Confirmed = confirmedF5;
    const wasWorkAllowed = resumeWorkAllowed;
    resumeWorkAllowed = state.known !== false && state.notificationPending !== true &&
      state.scopeReadbackPending !== true &&
      (state.phase === 'ready' || (state.phase === 'resuming' && state.resumeReady !== false));
    const admitted = isSharedPowerLifecycleAdmitted(state);
    const suspendScope = JSON.stringify([state.generation, state.suspendSerial]);
    const suspendScopeChanged = sharedSuspendScope !== null && sharedSuspendScope !== suspendScope;
    sharedSuspendScope = suspendScope;
    if (admitted) for (const request of fanWakeRequests.values()) request.retryAfterPublicReady?.(state);
    if (!admitted || suspendScopeChanged) sleepPlanOptimizationQueue.invalidate();
    if (!admitted && sleepPlanOptimizationConfirmation) sleepPlanOptimizationConfirmation.denied = true;
    if (admitted && sleepPlanOptimizationRequest) continueSleepPowerPlanOptimization();
    const admissionRestored = admitted && !sharedPowerAdmitted;
    const admissionLost = !admitted && sharedPowerAdmitted;
    sharedPowerAdmitted = admitted;
    const backgroundScopeChanged = sharedMediaPending !== null &&
      sharedMediaPending.notificationSequence !== state.observedNotificationSequence;
    if (admissionLost || suspendScopeChanged || backgroundScopeChanged) {
      backgroundRefreshOnReady ||= suspendScopeChanged || sharedMediaPending !== null;
      ++dynamicBackgroundGeneration;
      sharedMediaPending = null;
      lastDynamicGameIdentity = null;
      if (dynamicBackgroundNoGameTimer !== null) {
        window.clearTimeout(dynamicBackgroundNoGameTimer);
        dynamicBackgroundNoGameTimer = null;
      }
    }
    if (admitted && backgroundRefreshOnReady) {
      backgroundRefreshOnReady = false;
      requestSharedBackgroundRefresh();
    } else if (admissionRestored) queueSharedBackgroundRefresh();
    const key = `${state.generation}:${state.phase}:${state.known}:${state.notificationPending}`;
    // The network check follows hardware admission, which can change without a phase change.
    if (!admitted) networkAccelerationWake.cancel();
    else if ((sharedPowerSeen || confirmedF5) && (admissionRestored || f5Restored || key !== sharedPowerKey || suspendScopeChanged))
      networkAccelerationWake.schedule(state.generation, state.suspendSerial);
    // A scoped hint can retire the old owner before Native confirms the new suspend serial.
    if (suspendScopeChanged || (wasWorkAllowed && !resumeWorkAllowed)) {
      resumeQueue.invalidate();
      if (resumeRetryTimer) { clearTimeout(resumeRetryTimer); resumeRetryTimer = null; }
    }
    if (key === sharedPowerKey) {
      if (admissionLost) resumeQueue.invalidate();
      if (f5Restored && state.phase === 'resuming') onPowerResumeReady(state.generation);
      else if (f5Restored && state.phase === 'ready' && state.generation > latestResumeGeneration) {
        onPowerResumed(new CustomEvent('shared:power-lifecycle', { detail: state }));
      }
      // Admission alone can resume unfinished work even if the phase and generation did not change.
      else if (state.generation === latestResumeGeneration && (admissionRestored ||
          (suspendScopeChanged && (admitted || (confirmedF5 && state.phase === 'resuming'))))) {
        scheduleResumeTransaction(state.generation);
      }
      return;
    }
    const first = !sharedPowerSeen;
    sharedPowerSeen = true; sharedPowerKey = key;
    const event = new CustomEvent('shared:power-lifecycle', { detail: state });
    if (state.known === false || state.notificationPending === true) {
      resumeQueue.invalidate();
      if (resumeRetryTimer) { clearTimeout(resumeRetryTimer); resumeRetryTimer = null; }
      tdpRuntimeStore.invalidatePower();
      if (resumeVideoTimer !== null) window.clearTimeout(resumeVideoTimer);
      resumeVideoTimer = null; suspendBackgroundVideo();
      // Unknown shared context is not evidence that the Fan device lost F5.
    } else if (state.phase === 'resuming') {
      onPowerResuming(event);
      if (confirmedF5) onPowerResumeReady(state.generation);
    } else if (state.phase !== 'ready') onPowerSuspending(event);
    else if (!first || confirmedF5) onPowerResumed(event);
    else if (backgroundVideoSuspended.value) resumeBackgroundVideo();
  }, { observeSuspendSerial: true, observeNotificationSequence: true, onFanWakeRepublished: state => state.fanWakeGeneration && fanWakeRequests.get(state.fanWakeGeneration)?.retryAfterPublicReady?.(state) });
  stopSharedFanWakeWatch = onSharedPowerLifecycle(onSharedFanWake, { observeFanWakeGeneration: true, observeSuspendSerial: true, observeNotificationSequence: true });
  stopResumeGameReadinessWatch = subscribeGameTargetReadiness(onResumeGameTargetReadiness);
  window.addEventListener('fan:guard-state', onFanGuardState as EventListener);
  stopFanResumeWatch = on<{ generation?: number }>('fan.resume-ready', ({ generation }) => {
    onFanResumeReady(Number(generation) || 0);
  });
  uiPowerReconciliation = startUiPowerReconciliation();
  const fanLifecycleAtMountPromise = powerLifecycle.getForFan().catch(() => null);
  try {
    const isolation = await app.aiCpuIsolatedSession() as any;
    if (!isolation || isolation.schemaVersion !== 1 || typeof isolation.enabled !== 'boolean') throw new Error('AI_ISOLATION_CAPABILITY_INVALID');
    aiCpuIsolated = isolation.enabled === true;
    if (aiCpuIsolated && (isolation.physicalFeatureEvidence !== false || typeof isolation.sessionId !== 'string')) throw new Error('AI_ISOLATION_CAPABILITY_INVALID');
  } catch (error) {
    if (String(error instanceof Error ? error.message : error) !== 'unknown: app.aiCpuIsolatedSession') throw error;
  }
  // Use the native-resolved PowerControl directory before startup recovery or
  // TDP calls. Native owns settings.write validation, so deriving this path
  // independently from exeDir can split the frontend/native settings contract
  // on machines where the executable and sidecar directory are laid out
  // differently (notably Intel test systems).
  let nativePowerControlDir: string | null = null;
  try {
    const dir = await app.powerControlDir();
    nativePowerControlDir = dir || null;
    if (dir) {
      setPowerControlDir(dir);
      setAutofloatPowerControlDir(dir);
      setGyroVirtualFeatureAssetsRoot(dir);
    }
  } catch { /* fixed formal path remains the compatibility fallback */ }
  // Prepare TDP state at shell startup. Start the helper on the first operation that needs it.
  if (!aiCpuIsolated) void primeTdpSession(undefined, { startHelper: false }).catch((error) => {
    store.pushLog({ cmd: 'tdp.startup-session', args: {}, ok: false, error: String(error) });
  });
  // Restore performance ownership before optional Fan/media initialization can
  // delay it. Keep the same resident writer and boot receipt across later hooks.
  let startupPerformanceIntent = '';
  let startupPerformanceResumeGeneration = latestResumeGeneration;
  stopStartupPerformanceWatch = onContinuousPerformanceApplied(signature => {
    startupPerformanceIntent = signature;
    startupPerformanceResumeGeneration = latestResumeGeneration;
  }, () => { startupPerformanceIntent = ''; });
  startGamePolicyRuntime();
  // Provision the RTSS bridge at every main YMCC startup, not just when
  // monitoring is enabled. This optional repair must not delay UI readiness,
  // start/restart RTSS, or change the user's currently selected template.
  if (!aiCpuIsolated) void ensureRtssOverlayInstalled().catch((error) => {
    store.pushLog({ cmd: 'rtss.startup-install', args: {}, ok: false, error: String(error) });
  });
  // GP-926R3: sidebar/routes are resident. Do not probe optional input assets at shell startup.
  // Fan settings must load only after the native-resolved PowerControl path is
  // installed; otherwise a child NavRail mount could write defaults to the
  // compatibility path before native startup finishes resolving directories.
  deckyMirrorHost?.startupPhase('fan-settings');
  await initializeFanFeature();

  // FanHost handshake is optional hardware discovery and must not delay the
  // controller receiver. The page can already be visible/clickable while the
  // handshake is in progress, so register Native UI actions first.
  window.addEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  window.addEventListener('ipc:gamepad.tdp-delta', onGamepadTdpDelta as EventListener);
  window.addEventListener('ipc:gamepad.brightness', onGamepadBrightness as EventListener);
  window.addEventListener('ipc:gamepad.mouse-toggle', onGamepadMouseToggle as EventListener);
  window.addEventListener('ipc:mouseMode.changed', onGamepadMouseToggle as EventListener);
  stopGamepad = startGamepad({ router, onAction: (l) => store.pushGamepad(l) });
  stopGyroTelemetry = on('gyro.telemetry', (data) => {
    window.dispatchEvent(new CustomEvent('input:motion-telemetry', { detail: data }));
  });

  // Finish settings and background initialization before the optional
  // FanHost handshake. The native splash waits for this signal, so the first
  // visible frame is already in its normal background state.
  deckyMirrorHost?.startupPhase('ui-settings');
  await loadUiSettings();
  // Wallpaper only needs its persisted switch/appearance and the existing
  // shared media owner. Do not wait for unrelated background, controls or
  // tray initialization; those hosts keep their original later readiness gate.
  if (!aiFanMockActive && !aiCpuIsolated) steamWallpaperHost?.ready();
  backgroundOpacity.value = getBackgroundOpacity();
  backgroundBlur.value = getBackgroundBlur();
  backgroundVideoAutoPause.value = getUiSetting('videoBatteryPause');
  windowPlacement.value = getUiSetting('windowPlacement');
  // Await saved placement before signalling first-paint readiness; never place using defaults first.
  deckyMirrorHost?.startupPhase('placement');
  await applyWindowPlacement().catch(reportWindowPlacementError);
  window.addEventListener('ui-settings:changed', onWindowPlacementSettingsChanged);
  window.addEventListener('ui-settings:loaded', onWindowPlacementSettingsChanged);
  window.addEventListener('background:changed', onBackgroundChanged as EventListener);
  window.addEventListener('background:opacity-changed', onBackgroundOpacityChanged as EventListener);
  window.addEventListener('background:blur-changed', onBackgroundBlurChanged as EventListener);
  window.addEventListener('background:position-changed', onBackgroundPositionChanged as EventListener);
  window.addEventListener('ipc:window.minimized', onBackgroundWindowHidden as EventListener);
  window.addEventListener('ipc:window.hidden', onBackgroundWindowHidden as EventListener);
  window.addEventListener('ipc:window.restored', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.shown', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.maximized', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.summoned', onBackgroundWindowShown as EventListener);
  // 唤起（托盘呼出）时按设置项对齐主窗口位置。
  window.addEventListener('ipc:window.summoned', onWindowPlacementSummoned);
  document.addEventListener('visibilitychange', onBackgroundVisibilityChange);
  window.addEventListener('focus', onBackgroundFocusChanged);
  window.addEventListener('blur', onBackgroundFocusChanged);
  window.addEventListener('ipc:window.activationChanged', onBackgroundWindowActivation);
  deckyMirrorHost?.startupPhase('background-state');
  const initialWindowState = await windowApi.getState().catch(() => null);
  if (initialWindowState) {
    if (decorativeWindowForeground.value === null && typeof initialWindowState.foreground === 'boolean') {
      decorativeWindowForeground.value = initialWindowState.foreground;
    }
    softwareWebViewRendering = initialWindowState.softwareRendering === true;
    nativeWindowVisible.value = Boolean(initialWindowState.visible && !initialWindowState.minimized);
    undervoltWindowVisible.value ??= nativeWindowVisible.value;
    setNativeWindowVisible(nativeWindowVisible.value);
  }
  const initialBackgroundState = await backgroundGet().catch(() => null);
  lastFixedBackgroundState = initialBackgroundState;
  applyBackgroundState(initialBackgroundState, 'fixed');
  startDynamicBackgroundWatch(initialBackgroundState != null);
  startGameProfileAutoMemory();
  stopLosslessRuntimeOwner=startLosslessRuntimeOwner();
  if (!aiFanMockActive && !aiCpuIsolated) stopGameTrainerAutoPrepare = startGameTrainerAutoPrepare();
  stopUiVisibility = onUiVisibilityChange(({ visible }) => {
    uiVisible.value = visible;
    if (visible) {
      startDynamicBackgroundWatch();
      resumeBackgroundVideo();
    } else {
      stopDynamicBackgroundWatch();
      pauseBackgroundVideoForWindow();
    }
  });
  window.addEventListener('dynamic-background:settings-changed', onDynamicBackgroundSettingChanged);
  window.addEventListener('dynamic-background:loaded', onDynamicBackgroundLoaded as EventListener);
  window.addEventListener('ipc:game.rules.changed', onGameRulesChanged as EventListener);
  deckyMirrorHost?.startupPhase('power-source');
  const initialPowerMode = await detectPowerModeReliable();
  if (initialPowerMode) {
    rememberPowerSourceMode(initialPowerMode);
    applyVideoPowerMode(initialPowerMode, true);
  }
  else onAcPower.value = (await detectPowerMode().catch(() => 'dc')) === 'ac';
  deckyMirrorHost?.startupPhase('background-media');
  await reconcileBackgroundVideo();
  window.dispatchEvent(new CustomEvent('app-startup-ready'));

  // FAN-926R U2 / FAN-932 §2.3: shell admission itself stays inert. The renderer
  // registers only the side-effect-free Fan configuration and the identity sink.
  // This block does NOT spawn the Fan Host, call /api/handshake, open HC, acquire
  // a lease or start any Fan timer. The preference-driven boot auto-start is a
  // separate step that runs after the renderer reattach decision below; page entry
  // remains a non-start path.
  try {
    const configuredFan = getFanFeatureSettings();
    if (nativePowerControlDir) fanHostLifecycle.setConfig(resolveFanHostConfig(nativePowerControlDir));
    fanHostLifecycle.setSavedIdentity(aiFanMockActive ? null : configuredFan.deviceIdentity ?? null);
    fanHostLifecycle.setDeviceIdentitySink(aiFanMockActive ? undefined : (identity) => {
      void rememberFanDevice(identity).catch(() => {});
    });
    if (aiFanMockActive) stopAiFanService = await startAiFanService({
      lifecycle: fanHostLifecycle, preset: getFanPresetCurve,
      onError: (error) => {
        // R11 T-04 / R12：只保留**显式枚举**的自有固定码（不是匹配 `AI_FAN_*` 形状），
        // 其余一律折叠，避免未受信任载荷或原始异常进入诊断。
        // 走免门有限责任收据（fanLifecycleEvidence），使默认关闭详细日志时"下一次失败"
        // 依然落盘可判读，而不是整条消失。
        fanLifecycleEvidence('ai.mock-request-failed', {
          reason: projectAiFanErrorCode(error, 'AI_FAN_UNCLASSIFIED_FAILURE'),
        });
      },
    });
    fanDiagnosticLog('lifecycle.shell-admission-deferred', { phase: fanHostLifecycle.phase });
  } catch (error) {
    // A configuration/identity wiring failure must never gate YMCC startup, the
    // controller path or the fan entry. The fan page reports its own
    // connect/permission state when the user asks for it.
    fanDiagnosticLog('lifecycle.shell-admission-failure', {
      error: error instanceof Error ? error.message : String(error),
    });
  }
  // 程序启动只同步一次；之后仅在模拟鼠标真实开/关时更新内页锁定。
  void syncMouseModeAtStartup();
  window.addEventListener('background:video-battery-pause-changed', onBackgroundVideoPauseChanged as EventListener);
  window.addEventListener('ui-settings:loaded', onUiSettingsLoaded as EventListener);
  window.addEventListener('ui-settings:changed', onUiSettingsLoaded as EventListener);
  stopPowerSourceWatch = onPowerSourceNotification('immediate', (mode) => {
    onVideoPowerSourceChanged(mode === 'ac');
    // sourceChanged is the immediate native notification; acChanged is a
    // later 5-second tail-debounced confirmation. Start the dedicated
    // profile restore here so a stable plug/unplug applies promptly, while
    // the debounced event below still provides the final settling pass.
    schedulePowerChangePerformanceRestore(mode);
  });
  // AC/DC 插拔：刷新数据；自动模式由性能调度重新应用当前电源侧组合。
  stopAcWatch = onPowerSourceNotification('settled', async (mode) => {
    onAcPower.value = mode === 'ac';
    onVideoPowerSourceChanged(mode === 'ac');
    globalRefreshKey.value++;
    scheduleSleepPowerPlanOptimization();
    // Register the intent in delivery order. Its own public Ready gate handles writes.
    schedulePowerChangePerformanceRestore(mode);
    const scope = sharedPowerWriteScope(getSharedPowerLifecycle());
    if (!scope) return;
    const current = () => resumeLifecycleActive && powerSourceMode.value === mode && matchesOrdinaryPowerScope(scope);
    if (!await powerLifecycleReadyForWrites(scope) || !current()) return;
    // FAN-929（2026-09-27 用户裁决）：风扇控制的 AC/DC 电源档重放。原来由 Host 侧 1 Hz
    // watchdog 轮询发现电源线变化（稳态恒定 CPU 成本），现改为事件驱动：由这里触发一次
    // 曲线重放，Host 在写入点按当前电源档重新捕获模板并做 route 就绪校验。
    void fanHostLifecycle.reapplyActiveCurve('ac-changed', current);
  });
  stopForegroundPowerSource = startForegroundPowerSourceRefresh((mode) => {
    const changed = onAcPower.value !== (mode === 'ac');
    applyVideoPowerMode(mode, true);
    // A missed source notification must also restore the correct global/game
    // profile. Reuse existing ownership/generation/write guards only on change;
    // merely showing the same AC/DC side never replays fan/CPU/TDP settings.
    if (changed) { invalidateFloatPowerSource(mode); schedulePowerChangePerformanceRestore(mode); }
  });
  deckyMirrorHost?.startupPhase('wake-state');  // FAN-927 §5（2026-09-27 用户批准）：渲染器重建 / 主程序非正常退出后重开的**唯一**事件入口。
  // 依据完全来自 native 活动记录：本 run 已确认的意图只接上已有会话（不重开 HC、不重发曲线）；
  // 崩溃前仍有效的记录才做一次有界恢复；无依据时零动作（不启动 Host、不发 HTTP）。
  deckyMirrorHost?.startupPhase('fan-owner');
  const fanLifecycleAtMount = await fanLifecycleAtMountPromise;
  if (fanLifecycleAtMount?.resumeReady === true
      && (fanLifecycleAtMount.phase === 'ready' || fanLifecycleAtMount.phase === 'resuming')) {
    onFanResumeReady(Number(fanLifecycleAtMount.generation) || 0);
  }
  const fanReattach = fanHostLifecycle.reattachAfterRendererRestart().catch((error) => {
    fanDiagnosticLog('lifecycle.renderer-restart-hook-failed', {
      error: error instanceof Error ? error.message : String(error),
    });
    return false;
  });
  // FAN-932 §2.1.4：偏好驱动的开机自动启动必须**等接续判定完成**后再执行（不能 `void` 发出
  // reattach 后立刻另起自动启动，否则可能抢在接续前下发第二条曲线）。串进同一流程后再启动；
  // 若接续已恢复活动曲线，ensureFanAutoStart 会因 hasControlIntent 直接 no-op。
  // Boot scenes apply once per Windows boot; app/renderer restart and wake never replay next-boot scenes.
  // The original Fan resume ownership decision completes before any scene curve can be requested.
  await fanReattach;
  deckyMirrorHost?.startupPhase('boot-mode');
  if (!aiFanMockActive && !aiCpuIsolated && await powerLifecycleReadyForWrites()) {
    await restoreSelectedBootMode().catch((error) => {
      console.warn('开机模式部分配置未完成，请在开机模式页检查：', error instanceof Error ? error.message : String(error));
    });
  }
  void ensureFanAutoStart('boot');
  // 性能调度统一负责自动模式 CPU/TDP 应用；不再加载或回刷 AC/DC CPU 锁定配置。
  // 性能调度由用户首次选择模式后才接管启动恢复；未配置时保持原 CPU 自动启用链路不变。
  stopSchemeWatch = on('power.schemeChanged', async () => {
    if (aiCpuIsolated) return;
    scheduleSleepPowerPlanOptimization();
    // Intermediate broadcasts are expected while Windows restores a scheme.
    // The serialized resume transaction reconciles once after the gate opens.
    await runOrdinaryPowerRestore(async (scope, current, signal) => {
      const schemeOwner = await reconcileRememberedPowerScheme(current, scope);
      if (!current()) return;
      // Refresh the active page after reconciliation without changing ownership.
      globalRefreshKey.value++;
      await restorePerformanceScheduleIfConfigured(undefined, {
        dedicatedOnly: schemeOwner !== 'auto',
        automaticOnly: true, restoreRemembered: true, ownsPowerRequest: current, powerReadSignal: signal,
      });
    }).catch(() => {});
  });
  // A freshly recreated page can mount while native is still Resuming. Do not
  // let normal boot restoration race the gated wake transaction; the latter
  // restores the current saved bank after commit, including remembered manual limits.
  deckyMirrorHost?.startupPhase('performance');
  try {
    if (!aiCpuIsolated) await runOrdinaryPowerRestore(async (bootCoreScope, current, signal) => {
      const schemeOwnerAtBoot = await reconcileRememberedPowerScheme(current, bootCoreScope).catch(() => 'yeman' as const);
      if (!current()) return;
      const performanceOwnerAtBoot = await restorePerformanceScheduleIfConfigured(undefined, {
        dedicatedOnly: schemeOwnerAtBoot !== 'auto',
        automaticOnly: true, restoreRemembered: true, ownsPowerRequest: current, powerReadSignal: signal,
        completedStartupIntent: () => startupPerformanceResumeGeneration === latestResumeGeneration ? startupPerformanceIntent : '',
      }).catch(() => null);
      if (!current()) return;
      if (performanceOwnerAtBoot === 'auto' && schemeOwnerAtBoot === 'auto') {
        // Some boot tasks/OEM services rewrite the three hybrid-core values
        // after the first restore.  Reapply only the saved core mode after the
        // system settles; manual mode never enters this path.
        window.setTimeout(() => {
          if (resumeLifecycleActive) void refreshPerformanceScheduleCoreMode(bootCoreScope).catch(() => {});
        }, 3000);
      } else if (performanceOwnerAtBoot === 'none') {
        await applyProgramControls(current).catch(() => {});
      }
    });
  } finally {
    stopStartupPerformanceWatch?.();
    stopStartupPerformanceWatch = null;
  }

  // ── 任务栏常驻偏好：启动期按持久化套用（默认不常驻，由设置/总闸驱动）──
  deckyMirrorHost?.startupPhase('tray');
  if (!aiFanMockActive && !aiCpuIsolated) await migrateBootTrayPreference().catch(() => {});
  applyTrayResident().catch(() => {});
  // ── 音乐播放：启动期恢复已配置的音乐目录（不自动播放）──
  void initMusic();
  // ── 开机启动自愈：若用户曾开启但计划任务被误删，自动重建 ──
  if (!aiCpuIsolated) healBootTask().catch(() => {});

  // Preserve sleep power-plan optimization independently of retired CPU controls.
  if (!aiCpuIsolated) sleepPlanOptimizationTimer = window.setTimeout(() => {
    sleepPlanOptimizationTimer = null;
    scheduleSleepPowerPlanOptimization();
  }, 30000);
  if (!aiCpuIsolated) void startUndervoltRuntime();
  // No legacy CCD/undervolt startup timer is armed while the feature is inactive.
  if (!aiCpuIsolated && LEGACY_CCD_UNDERVOLT_ACTIVE) window.setTimeout(() => {
    applyCpuAutostart().catch(() => {});
  }, 30000);

  if (!aiFanMockActive && !aiCpuIsolated) { deckyMirrorHost?.ready(); monitorHudHost?.ready(); startBackgroundUpdate(); }
});
onUnmounted(() => {
  backgroundPresentationDisposed = true;
  stopUndervoltRuntime();
  cancelBootModeLaunches();
  cancelBootModeDeferredMouse();
  stopBootModeReceipts?.(); stopBootModeReceipts = null;
  uiPowerReconciliation?.stop();
  uiPowerReconciliation = null;
  stopSharedPowerWatch?.(); stopSharedPowerWatch = null;
  stopSharedFanWakeWatch?.(); stopSharedFanWakeWatch = null;
  updatePredownloadHost?.stop(); updatePredownloadHost = null;
  controllerShortcutRuntime?.stop(); controllerShortcutRuntime = null;
  window.removeEventListener('game-media:changed',onSharedMediaChanged);
  window.removeEventListener('dynamic-background:waiting-for-playback',onDynamicBackgroundWaiting);
  stopWallpaperPower?.();stopWallpaperPower=null;steamWallpaperHost?.stop(); steamWallpaperHost=null;stopSharedGameMedia?.();stopSharedGameMedia=null;
  radialHost?.stop(); radialHost = null;
  monitorHudHost?.stop(); monitorHudHost = null;
  stopDeckySafetyNotifications?.();stopDeckySafetyNotifications=null;
  deckyMirrorHost?.stop(); deckyMirrorHost = null;
  stopAiFanService?.();
  stopAiFanService = null;
  window.removeEventListener('ui-settings:changed', onWindowPlacementSettingsChanged);
  window.removeEventListener('ui-settings:loaded', onWindowPlacementSettingsChanged);
  window.removeEventListener('ipc:window.summoned', onWindowPlacementSummoned);
  videoPowerProbeGeneration++;
  if (videoPowerProbeTimer !== null) {
    window.clearTimeout(videoPowerProbeTimer);
    videoPowerProbeTimer = null;
  }
  if (dynamicBackgroundNoGameTimer !== null) {
    window.clearTimeout(dynamicBackgroundNoGameTimer);
    dynamicBackgroundNoGameTimer = null;
  }
  resumeLifecycleActive = false;
  stopResumeGameReadinessWatch?.(); stopResumeGameReadinessWatch = null;
  fanWakeRequests.clear();
  resumeQueue.invalidate();
  sleepPlanOptimizationQueue.invalidate();
  sleepPlanOptimizationRequest = null;
  sleepPlanOptimizationConfirmation?.stop();
  sleepPlanOptimizationConfirmation = null;
  networkAccelerationWake.dispose();
  if (resumeRetryTimer) {
    clearTimeout(resumeRetryTimer);
    resumeRetryTimer = null;
  }
  powerChangeRestore = null;
  powerChangeRestoreScope = null;
  clearPowerChangeRestoreTimer();
  if (sleepPlanOptimizationTimer !== null) {
    window.clearTimeout(sleepPlanOptimizationTimer);
    sleepPlanOptimizationTimer = null;
  }
  clearBackgroundRetry();
  if (resumeVideoTimer !== null) {
    window.clearTimeout(resumeVideoTimer);
    resumeVideoTimer = null;
  }
  invalidateBackgroundMedia();
  stopDynamicBackgroundWatch();
  stopGameProfileAutoMemory();
  stopLosslessRuntimeOwner?.();stopLosslessRuntimeOwner=null;
  stopGamePolicyRuntime();
  stopGameTrainerAutoPrepare?.();
  stopGameTrainerAutoPrepare = null;
  stopUiVisibility?.();
  stopUiVisibility = null;
  window.removeEventListener('background:changed', onBackgroundChanged as EventListener);
  window.removeEventListener('background:opacity-changed', onBackgroundOpacityChanged as EventListener);
  window.removeEventListener('background:blur-changed', onBackgroundBlurChanged as EventListener);
  window.removeEventListener('background:position-changed', onBackgroundPositionChanged as EventListener);
  window.removeEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  window.removeEventListener('ipc:gamepad.tdp-delta', onGamepadTdpDelta as EventListener);
  window.removeEventListener('ipc:gamepad.brightness', onGamepadBrightness as EventListener);
  window.removeEventListener('ipc:gamepad.mouse-toggle', onGamepadMouseToggle as EventListener);
  window.removeEventListener('ipc:mouseMode.changed', onGamepadMouseToggle as EventListener);
  window.removeEventListener('ipc:window.minimized', onBackgroundWindowHidden as EventListener);
  window.removeEventListener('ipc:window.hidden', onBackgroundWindowHidden as EventListener);
  window.removeEventListener('ipc:window.restored', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.shown', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.maximized', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.summoned', onBackgroundWindowShown as EventListener);
  window.removeEventListener('background:video-battery-pause-changed', onBackgroundVideoPauseChanged as EventListener);
  window.removeEventListener('ipc:fan.suspending', onFanPhysicalSuspending as EventListener);
  window.removeEventListener('fan:guard-state', onFanGuardState as EventListener);
  window.removeEventListener('ui-settings:loaded', onUiSettingsLoaded as EventListener);
  window.removeEventListener('ui-settings:changed', onUiSettingsLoaded as EventListener);
  window.removeEventListener('dynamic-background:settings-changed', onDynamicBackgroundSettingChanged);
  window.removeEventListener('dynamic-background:loaded', onDynamicBackgroundLoaded as EventListener);
  window.removeEventListener('ipc:game.rules.changed', onGameRulesChanged as EventListener);
  document.removeEventListener('visibilitychange', onBackgroundVisibilityChange);
  window.removeEventListener('focus', onBackgroundFocusChanged);
  window.removeEventListener('blur', onBackgroundFocusChanged);
  window.removeEventListener('ipc:window.activationChanged', onBackgroundWindowActivation);
  stopGamepad?.();
  stopGyroTelemetry?.();
  stopAcWatch?.();
  stopPowerSourceWatch?.();
  stopForegroundPowerSource?.();
  stopForegroundPowerSource = null;
  stopFanResumeWatch?.();
  stopSchemeWatch?.();
  stopStartupPerformanceWatch?.();
  stopStartupPerformanceWatch = null;
  // Fan Host shutdown has one canonical owner: the native app-exit boundary
  // (or NavRail.quit, which awaits the same lifecycle before app.exit).  A
  // second close from root unmount races that HC gate and can turn one
  // firmware timeout into duplicate /api/close requests.  HC itself has only
  // one Window_Closed -> CurrentDevice.Close() path, so root unmount must not
  // invent a second device-session close.
});

// ── UI 缩放：监听窗口尺寸 / 原生 resize 事件，保持设计比例填满窗口 ──
let stageResizeObserver: ResizeObserver | null = null;
onMounted(() => {
  updateScale();
  settleScale();
  window.addEventListener('resize', updateScale);
  window.addEventListener('ipc:window.resized', onNativeResize as EventListener);
  // 呼出后重新定稿：原生尺寸消息可能早于 WebView2 视口恢复到达。
  window.addEventListener('ipc:window.summoned', refreshScaleAfterSummon as EventListener);
  window.addEventListener('orientationchange', refreshScaleAfterSummon);
  document.addEventListener('visibilitychange', refreshScaleWhenVisible);
  window.visualViewport?.addEventListener('resize', updateScale);
  // 原生 reflow（隐藏任务栏后窗口吸底变高）可能不派发 window resize，
  // 用 ResizeObserver 监测文档根节点尺寸变化兜底，避免内容层停留在旧尺寸、
  // 底部露出透明带。
  if (typeof ResizeObserver !== 'undefined') {
    stageResizeObserver = new ResizeObserver(() => {
      updateScale();
      settleScale();
    });
    stageResizeObserver.observe(document.documentElement);
  }
});
onUnmounted(() => {
  stageResizeObserver?.disconnect();
  stageResizeObserver = null;
  if (settleRaf) cancelAnimationFrame(settleRaf);
  settleRaf = 0;
  settleFrames = 0;
  nativeViewportFrames = 0;
  window.removeEventListener('resize', updateScale);
  window.removeEventListener('ipc:window.resized', onNativeResize as EventListener);
  window.removeEventListener('ipc:window.summoned', refreshScaleAfterSummon as EventListener);
  window.removeEventListener('orientationchange', refreshScaleAfterSummon);
  document.removeEventListener('visibilitychange', refreshScaleWhenVisible);
  window.visualViewport?.removeEventListener('resize', updateScale);
});
</script>

<template>
  <div class="app-root">
    <video
      v-if="backgroundUrl && backgroundKind === 'video' && !backgroundVideoSuspended"
      v-show="!backgroundEditorPaused"
      :key="backgroundVideoGeneration"
      ref="backgroundVideo"
      class="user-background-video"
      :src="factoryBackgroundPoster || isHlsUrl(backgroundUrl) ? undefined : backgroundUrl"
      :style="{ opacity: String(backgroundOpacity), filter: `blur(${backgroundBlur}px) saturate(1.12) contrast(1.08)`, objectFit: backgroundPlayback?.fit || 'cover', objectPosition: backgroundObjectPosition }"
      muted
      loop
      playsinline
      :poster="factoryBackgroundPoster"
      :preload="factoryBackgroundPoster ? 'none' : factoryBackgroundVideo ? 'auto' : 'metadata'"
      disablepictureinpicture
      tabindex="-1"
      aria-hidden="true"
      @loadedmetadata="onBackgroundVideoLoaded"
      @timeupdate="onBackgroundVideoTimeUpdate"
      @canplay="onBackgroundVideoCanPlay"
      @playing="onBackgroundVideoPlaying"
      @pause="onBackgroundVideoPause"
      @error="onBackgroundVideoMediaError"
    />
    <div
      v-else-if="backgroundUrl"
      class="user-background-image"
      :style="backgroundStyle"
      aria-hidden="true"
    />
    <div class="app-backdrop" aria-hidden="true" />
    <div class="app-stage" :data-ui-visible="uiVisible" :data-ui-foreground="decorativeWindowForeground" :style="scalerStyle">
      <div v-if="!hasVisibleBackgroundMedia" class="empty-background" aria-hidden="true" />
      <div v-if="!isStandaloneEditor" class="app-body" :class="{ 'app-body--nav-right': windowPlacement === 'left' }">
        <NavRail />
        <main class="app-main">
          <!-- 顶部监控条：独立于页面滚动层，左右顶满 app-main，不受滚动条宽度影响 -->
          <TopMonitorBar />
          <div class="app-content">
            <router-view v-slot="{ Component }">
              <KeepAlive :max="2">
                <component :is="Component" />
              </KeepAlive>
            </router-view>
          </div>
        </main>
      </div>
      <div v-else class="standalone-body">
        <router-view v-slot="{ Component }">
          <component :is="Component" />
        </router-view>
      </div>
    </div>

    <HidHideInstallPrompt v-if="!isStandaloneEditor" />
  </div>
</template>

<style scoped>
/* Pause the decorative animations owned by the main UI while it is hidden.
 * Keep this list in sync with CSS animations rendered inside .app-stage so
 * a visibility change only restyles animation targets and nearby elements.
 * A visible sidebar can be displayed without taking focus; only the music
 * disc uses the foreground gate. */
.app-stage[data-ui-foreground="false"] :deep(:where(.cd-shape)),
.app-stage[data-ui-visible="false"] :deep(:where(
  .nav-icon .spinning, .cd-shape, .wallpaper-preview-ambient span,
  .device-blink-layer.blinking, .keyboard-blink-layer.blinking,
  .pad-shoulder.mapped path, .pad-trigger.mapped path, .pad-btn.mapped circle,
  .dpad.mapped path, .dpad.mapped rect, .stick-dot.mapped circle,
  .oem-capsule.mapped:not(.live), .handheld-key-bubbles span:not(.live), .creation-dot
)),
.app-stage[data-ui-visible="false"] :deep(:where(.rule-row.creating)::before) {
  animation-play-state: paused !important;
}
/* 顶层居中容器：viewport 内 grid place-items: center，整体应用壳视觉居中 */
.app-root {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  background: transparent;
  isolation: isolate;
  overflow: hidden;
}
.app-stage {
  /* width / height / zoom 由 inline style 注入；保持原视觉根职责 */
  display: flex;
  flex-direction: column;
  /* 根节点透明；可辨识度由导航、卡片和控件底板分块提供。 */
  background: transparent;
  position: relative;
  isolation: isolate;
  /* 必须在全窗底板（.app-backdrop, z-index 1）之上，内容才不会被底板盖住。 */
  z-index: 2;
}
/* 全窗暗色底板：有壁纸（视频/图片）时铺满整个窗口，尺寸始终等于窗口并随窗口自适应。
   此前该底板由 .app-main 自绘（右缘止于导航栏，只有 487px 宽），在壁纸上会呈现为
   「一块比窗口小的黑色矩形」。改由根节点整窗层承担后，底板恒等于窗口大小。
   挂在 .app-root（position:fixed; inset:0）直下，纯 CSS 自适应，无需跟随 stage 缩放。 */
.app-backdrop {
  position: absolute;
  inset: 0;
  /* 必须与 .user-background-video 一样显式写满 100%：.app-root 是
     display:grid + place-items:center，栅格容器会给绝对定位子项施加
     justify-self/align-self:center，只写 inset:0 时在 Chromium 里可能收缩，
     导致底板只覆盖左上角、右下露出透明。视频层因带 width/height:100% 不受影响。 */
  width: 100%;
  height: 100%;
  z-index: 1;
  background: color-mix(in srgb, var(--bg-solid) 62%, transparent);
  pointer-events: none;
}
.empty-background {
  position: absolute;
  inset: 0;
  z-index: 0;
  background: rgba(0, 0, 0, 0.3);
  pointer-events: none;
}
.user-background-video {
  position: absolute;
  inset: 0;
  z-index: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  opacity: var(--background-video-opacity, 0.64);
  pointer-events: none;
  background: transparent;
}
/* 自定义图片背景层：与视频层同为铺满 stage 的绝对定位层。此前缺少定位/尺寸规则，
   该 div 高度为 0，图片背景完全不可见（且 empty-background 会被 hasVisibleBackgroundMedia
   关掉，导致没有底板托底）。 */
.user-background-image {
  position: absolute;
  inset: 0;
  z-index: 0;
  width: 100%;
  height: 100%;
  background-size: cover;
  background-position: center;
  background-repeat: no-repeat;
  pointer-events: none;
}
.app-body {
  flex: 1 1 auto;
  display: flex;
  min-height: 0;
  position: relative;
  z-index: 1;
}
/* 窗口贴左侧时，侧边导航栏翻到右侧（贴近屏幕中心侧）；分隔线随之移到导航栏左缘。 */
.app-body--nav-right {
  flex-direction: row-reverse;
}
.app-body--nav-right :deep(.navrail) {
  border-right: none;
  border-left: 1px solid #1c2533;
}
.standalone-body {
  width: 100%;
  height: 100%;
  min-width: 0;
  min-height: 0;
  position: relative;
  z-index: 1;
  overflow: hidden;
}
.app-main {
  flex: 1 1 auto;
  min-width: 0;
  min-height: 0;
  overflow: hidden; /* 页面滚动移到 app-content，监控条不受滚动条影响 */
  padding: 0;
  /* 界面底板不再使用整体毛玻璃；自定义图片自身模糊由背景图片滑块控制。
     暗色底板已整体上移到全窗层 .app-backdrop，内容区不再自绘，避免同一块
     色板叠两层、也避免它只覆盖内容列（右缘止于导航栏）而看起来「比窗口小」。 */
  background: transparent;
  position: relative;
  z-index: 1;
}
/* 页面唯一滚动层：监控条在其外部，因此固定在内页顶端且不被滚动条扣宽度。 */
.app-content {
  height: calc(100% - 52px); /* 8px 顶部留白 + 34px 监控条 + 10px 下间距 */
  min-height: 0;
  overflow-y: auto;
  /* Keep the layout viewport width identical before/after a zoom-induced
     height change. Gamepad spatial coordinates must not shift when the
     vertical scrollbar is introduced or removed. */
  scrollbar-gutter: stable;
  /* Leave room for the gamepad focus ring and the final control itself. */
  padding: 12px 12px calc(var(--gamepad-safe-bottom, 24px) + var(--gamepad-clip-bottom, 0px));
  scroll-padding: 24px 0 var(--gamepad-safe-bottom, 24px);
}
</style>
