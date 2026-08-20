<script setup lang="ts">
import { ref, computed, provide, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { useRouter } from 'vue-router';
import NavRail from '@/components/NavRail.vue';
import DebugView from '@/components/DebugView.vue';
import { useDebugStore } from '@/stores/debug';
import { startGamepad } from '@/gamepad/engine';
import { enqueueGamepadTaskDetached } from '@/gamepad/serial';
import { on } from '@/bridge/ipc';
import { applyCpuAutostart } from '@/bridge/autostart';
import { applyTrayResident } from '@/bridge/trayResident';
import { getFloatInfo, applyCpuAutoEnable, setAutofloatPowerControlDir } from '@/bridge/autofloat';
import { readTdpAutoApply, applyAutoTdpIfNeeded } from '@/bridge/tdpAutoApply';
import { app, fs, powerLifecycle, windowApi } from '@/bridge/api';
import { initMusic } from '@/bridge/music';
import { readTdp, setTdp, setRtssLimit, readFps, rtssRunning, toggleTask, taskExists, detectPowerMode, detectPowerModeReliable, detectPowerModeStable, reconcileRememberedPowerScheme, resumeTdpDaemonAfterWake, getSleepPowerPlanOptimizationEnabled, optimizeSleepPowerPlans } from '@/bridge/yeman';
import TopMonitorBar from '@/components/TopMonitorBar.vue';
import { applyTheme } from '@/bridge/theme';
import { cyclePerformanceScheduleMode, getPerformanceScheduleOwnership, refreshPerformanceScheduleCoreMode, restorePerformanceScheduleIfConfigured } from '@/bridge/performanceSchedule';
import { getMouseModeState } from '@/bridge/gameproc';
import {
  backgroundGet,
  getBackgroundBlur,
  getBackgroundOpacity,
  type BackgroundKind,
  type BackgroundState,
} from '@/bridge/background';
import {
  cleanGameTitle,
  getDynamicBackgroundConfig,
  refreshDynamicBackground,
} from '@/bridge/dynamicBackground';
import { detectGame, subscribeGameStatus, type DetectedGame } from '@/bridge/gamedetect';
import { BOOT_CONTROL_CENTER_TASK, bootMirrorExists, setPowerControlDir, toggleBootMirror } from '@/bridge/yeman';
import { getUiSetting, loadUiSettings } from '@/bridge/uiSettings';
import { topMonitorData, type TopMonData } from '@/bridge/topmon';
import { nextLowBatteryStatic, shouldSkipVideoReconcile } from '@/bridge/backgroundPolicy';
import { runPowerResumeTransaction } from '@/bridge/powerResume';
import { readSettingsSection } from '@/bridge/settingsRepository';
import { isUiVisible, onUiVisibilityChange, setNativeWindowVisible } from '@/bridge/uiLifecycle';

applyTheme(
  document.documentElement.dataset.theme === 'cyberpunk'
    ? 'cyberpunk'
    : document.documentElement.dataset.theme === 'red-black'
      ? 'red-black'
      : 'blue-black',
);

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

const router = useRouter();
const store = useDebugStore();
const backgroundUrl = ref('');
const backgroundKind = ref<BackgroundKind>('image');
const backgroundVideo = ref<HTMLVideoElement | null>(null);
const backgroundOpacity = ref(getBackgroundOpacity());
const backgroundBlur = ref(getBackgroundBlur());
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
const backgroundVideoSuspended = ref(false);
const lowBatteryStatic = ref(false);
const videoDesired = ref(false);
const backgroundVideoGeneration = ref(0);
const dynamicBackgroundActive = ref(false);
const backgroundSource = ref<'fixed' | 'dynamic'>('fixed');
const backgroundFallbackUrls = ref<string[]>([]);
const backgroundFallbackIndex = ref(0);
let appliedBackgroundIdentity: string | null = null;
let lastDynamicGameIdentity: string | null = null;
let dynamicBackgroundGeneration = 0;
let lastFixedBackgroundState: BackgroundState | null = null;
const BACKGROUND_RETRY_WINDOW_MS = 2 * 60 * 1000;
const BACKGROUND_RETRY_INTERVAL_MS = 5000;
const DYNAMIC_BACKGROUND_NO_GAME_GRACE_MS = 4000;
let backgroundRetryTimer: number | null = null;
let backgroundRetryUntil = 0;
let dynamicBackgroundNoGameTimer: number | null = null;
let resumeVideoTimer: number | null = null;
let hlsInstance: { destroy: () => void; on: (event: string, callback: (...args: any[]) => void) => void; loadSource: (url: string) => void; attachMedia: (video: HTMLVideoElement) => void } | null = null;
function isVideoPlaybackBlockedByPower(): boolean {
  return backgroundVideoAutoPause.value &&
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
      (backgroundSource.value === 'dynamic' && !getDynamicBackgroundConfig().enabled)) return;
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
        (backgroundSource.value === 'dynamic' && !getDynamicBackgroundConfig().enabled)) {
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
      if (!isHlsUrl(backgroundUrl.value)) video.load();
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
  const sourceUrl = backgroundUrl.value;
  const HlsCtor = (window as any).Hls;
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
  if (video.canPlayType('application/vnd.apple.mpegurl')) video.src = backgroundUrl.value;
}
const backgroundStyle = computed(() =>
  backgroundUrl.value && backgroundKind.value === 'image'
    ? {
        backgroundImage: `url("${backgroundUrl.value}")`,
        opacity: String(backgroundOpacity.value),
        filter: `blur(${backgroundBlur.value}px) saturate(1.12) contrast(1.08)`,
      }
    : undefined,
);
function applyBackgroundState(state: BackgroundState | null | undefined, source: 'fixed' | 'dynamic' = 'fixed'): void {
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
  if (identity === appliedBackgroundIdentity) return;
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
    if (video && backgroundKind.value === 'video' && !isHlsUrl(backgroundUrl.value)) video.load();
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

async function reconcileBackgroundVideo(): Promise<void> {
  await nextTick();
  const video = backgroundVideo.value;
  const epoch = backgroundMediaEpoch;
  const shouldPlay = Boolean(
    video &&
    backgroundKind.value === 'video' &&
    backgroundUrl.value &&
    (backgroundSource.value === 'fixed' || getDynamicBackgroundConfig().enabled) &&
    !backgroundVideoSuspended.value &&
    documentVisible.value &&
    nativeWindowVisible.value &&
    !lowBatteryStatic.value &&
    !isVideoPlaybackBlockedByPower(),
  );
  if (!video) {
    videoDesired.value = shouldPlay;
    return;
  }
  if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value) return;
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
  if (video.readyState < 1) {
    videoDesired.value = false;
    return;
  }
  video.muted = true;
  try {
    await video.play();
    if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value ||
        !getDynamicBackgroundConfig().enabled && backgroundSource.value === 'dynamic') {
      video.pause();
    }
  } catch {
    if (epoch !== backgroundMediaEpoch || video !== backgroundVideo.value) return;
    videoDesired.value = false;
    onBackgroundVideoError();
  }
}

function onBackgroundVideoLoaded(): void {
  videoDesired.value = false;
  void reconcileBackgroundVideo();
}

function onBackgroundVideoCanPlay(): void {
  videoDesired.value = false;
  void reconcileBackgroundVideo();
}

function onBackgroundVideoPause(): void {
  if (!videoDesired.value) return;
  const shouldResume = Boolean(
    backgroundKind.value === 'video' &&
    backgroundUrl.value &&
    (backgroundSource.value === 'fixed' || getDynamicBackgroundConfig().enabled) &&
    !backgroundVideoSuspended.value &&
    documentVisible.value &&
    nativeWindowVisible.value &&
    !lowBatteryStatic.value &&
    !isVideoPlaybackBlockedByPower(),
  );
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
  clearBackgroundRetry();
  videoDesired.value = false;
  backgroundVideo.value?.pause();
  invalidateBackgroundMedia();
  backgroundVideoSuspended.value = true;
}

function pauseBackgroundVideoForWindow(): void {
  if (backgroundKind.value !== 'video') return;
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
    if (!isHlsUrl(backgroundUrl.value)) video.load();
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
  applyVideoPowerMode(mode, true);
  requestVideoPowerCheck(300, mode);
}

watch(topMonitorData, (data) => {
  observeVideoPower(data);
}, { immediate: true });

watch([backgroundUrl, backgroundKind], () => {
  void nextTick(reconcileBackgroundVideo);
});

function onBackgroundVideoError(message = 'Steam 在线视频播放失败'): void {
  videoDesired.value = false;
  invalidateBackgroundMedia();
  if (backgroundSource.value === 'dynamic' && !getDynamicBackgroundConfig().enabled) return;
  if (backgroundVideoSuspended.value) return;
  if (backgroundKind.value !== 'video') return;
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
    if (!isHlsUrl(backgroundUrl.value)) video.load();
    await setupBackgroundVideo();
    await reconcileBackgroundVideo();
  });
}

function onBackgroundVideoPlaying(): void {
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
const showDebug = ref(false);
function toggleDebug() {
  showDebug.value = !showDebug.value;
}

// ── 全局刷新触发器：支持页刷新按钮触发所有已挂载页面 refresh() ──
const globalRefreshKey = ref(0);
provide('globalRefreshKey', globalRefreshKey);

// ── 模拟鼠标内页锁定 ──
// 不再进行 JoyXoff 5 秒后台轮询。程序启动只同步一次；之后仅由真实开/关操作事件更新。
async function syncMouseModeAtStartup(): Promise<void> {
  try {
    const state = await getMouseModeState();
    window.dispatchEvent(new CustomEvent('gp:mouse-mode', {
      detail: { on: state.on, backend: state.backend },
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
  resumeBackgroundVideo();
}
function onBackgroundWindowHidden(): void {
  nativeWindowVisible.value = false;
  setNativeWindowVisible(false);
  stopDynamicBackgroundWatch();
  pauseBackgroundVideoForWindow();
}
function onBackgroundWindowShown(): void {
  nativeWindowVisible.value = true;
  setNativeWindowVisible(true);
  resumeBackgroundVideo();
  requestVideoPowerCheck();
}
function powerEventGeneration(e: Event): number {
  return Number((e as CustomEvent<{ generation?: number }>).detail?.generation) || 0;
}
function notePowerTransitionGeneration(e: Event): number {
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
  notePowerTransitionGeneration(e);
  if (resumeVideoTimer !== null) {
    window.clearTimeout(resumeVideoTimer);
    resumeVideoTimer = null;
  }
  // Do not leave a decoder/HLS pipeline alive across S3. A stale media
  // pipeline can leave WebView2 visible while its page input is unavailable.
  suspendBackgroundVideo();
}
function onPowerResuming(e: Event): void {
  notePowerTransitionGeneration(e);
  if (resumeVideoTimer !== null) {
    window.clearTimeout(resumeVideoTimer);
    resumeVideoTimer = null;
  }
  suspendBackgroundVideo();
}
function onPowerResumed(e: Event): void {
  const generation = powerEventGeneration(e);
  if (generation > committedResumeGeneration) scheduleResumeTransaction(generation);
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
  backgroundOpacity.value = getBackgroundOpacity();
  backgroundBlur.value = getBackgroundBlur();
  backgroundVideoAutoPause.value = getUiSetting('videoBatteryPause');
  updateLowBatteryStatic(topMonitorData.value);
}
// ── UI 缩放层：设计基准 580×780，整体在窗口中居中显示 ──
// 用户偏好 2026-08-04：在原本「按窗口高度贴齐」基础上再放大 30%，居中后左右两侧留空更协调。
// 视觉允许高度方向溢出（被裁剪的部分由 .app-content 滚动兜底），但宽度方向不许超窗口，避免点击区跑到屏幕外。
const BASE_W = 580;
const BASE_H = 780;
const SCALE_BOOST = 1.3; // "再放大 30%"
const uiScale = ref(1);
const viewportHeight = ref(window.innerHeight);
function setUiScale(scale: number) {
  uiScale.value = scale;
  // Dropdown 等 Teleport 到 body 的浮层不在 .app-stage 的 zoom 树中。
  // 把同一缩放值发布到根节点，供这些浮层同步字号与控件尺寸。
  document.documentElement.style.setProperty('--ui-scale', String(scale));
}
function updateScale() {
  viewportHeight.value = window.innerHeight;
  if (window.innerHeight <= 0 || window.innerWidth <= 0) {
    setUiScale(1);
    return;
  }
  // 用户视觉目标：在原本 innerHeight/BASE_H 的基础上再乘 1.3
  const boosted = (window.innerHeight / BASE_H) * SCALE_BOOST;
  // 宽度兜底：BASE_W * scale 不能超过 innerWidth（避免焦点跑到屏幕外）
  const widthCapped = window.innerWidth / BASE_W;
  setUiScale(Math.min(boosted, widthCapped));
}
const scalerStyle = computed(() => ({
  width: BASE_W + 'px',
  height: BASE_H + 'px',
  // zoom 同时参与布局和命中测试；transform 只放大绘制层，会让 WebView2
  // 看到的坐标与鼠标点击坐标分离，表现为内容区"看得到但点不到"。
  zoom: uiScale.value,
  // Keep the physical safe area proportional to the viewport, then convert
  // it back to the zoomed layout coordinate space used by app-content.
  '--gamepad-safe-bottom': `${Math.max(32, Math.min(64, viewportHeight.value * 0.06)) / Math.max(0.5, uiScale.value)}px`,
}));

// M8: Start button toggles the debug panel via the same 'ipc:gamepad-start' event.
function onGamepadStart() {
  toggleDebug();
}
function onGamepadRefresh() {
  globalRefreshKey.value++;
}
// ── 手柄后台（Start+上/下）：自动模式切换已编辑档位；手动模式实时调节并记忆 TDP 最大值 ──
function onGamepadTdpDelta(e: Event) {
  const d = Number((e as CustomEvent<{ delta?: number }>).detail?.delta) || 0;
  if (d !== 0) {
    enqueueGamepadTaskDetached(() => applyGamepadTdp(d));
  }
}
async function applyGamepadTdp(delta: number) {
  try {
    if (await getPerformanceScheduleOwnership() === 'auto') {
      const result = await cyclePerformanceScheduleMode(delta > 0 ? 1 : -1);
      if (result.applied) {
        globalRefreshKey.value++;
        window.dispatchEvent(new CustomEvent('gamepad:performance-mode-changed', {
          detail: { side: result.side, mode: result.mode },
        }));
      }
      return;
    }
    const mode = await detectPowerMode().catch(() => null);
    if (!mode) return;
    const cur = await readTdp(mode).catch(() => null);
    if (cur == null) return;
    const next = Math.max(2, Math.min(200, cur + delta));
    await setTdp(mode, next, { apply: true, save: true });
    globalRefreshKey.value++;
  } catch (e) {
    console.error('[gamepad tdp-delta]', e);
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
  const detail = (e as CustomEvent<{
    ok?: boolean;
    on?: boolean;
    backend?: 'gamebar' | 'joyxoff';
    error?: string;
  }>).detail || {};
  if (detail.ok === false) {
    window.dispatchEvent(new CustomEvent('mouse-mode:error', { detail }));
    return;
  }
  window.dispatchEvent(new CustomEvent('gp:mouse-mode', {
    detail: { on: Boolean(detail.on), backend: detail.backend },
  }));
}

async function applyProgramControls() {
  const [tdp, fps] = await Promise.all([readTdp('ac'), readFps('ac')]);
  const floating = getFloatInfo().enabled;
  const scheduleOwned = await getPerformanceScheduleOwnership().catch(() => false);
  // 启动自动应用 TDP：仅当开关开启且帧数目标浮动优化未接管时，才把保存的 TDP 最大值下发硬件。
  // 浮动开启时由 autofloat 管理 TDP，这里跳过（开关此时不生效）。
  if (tdp != null && !floating && scheduleOwned !== 'auto') {
    const auto = await readTdpAutoApply();
    if (auto.boot) void setTdp('ac', tdp, { apply: true, save: false }).catch(() => {});
  }
  // 启动恢复锁帧：读程序配置的 FPS 帧率上限并应用到 RTSS。
  // 开机初期 RTSS 可能未启动 / Profiles\Global 尚未生成（setRtssLimit 会静默跳过），
  // 因此带轮询重试：直到 RTSS 在跑且配置已应用成功一次。
  if (fps != null && !getFloatInfo().enabled && scheduleOwned !== 'auto') {
    void applyRtssLimitConfig(fps > 0 ? fps : 0).catch(() => {});
  }
}

// RTSS 启动恢复锁帧：写配置 + 最多 12 次（约 60 秒）轮询，RTSS 就绪后应用一次即停。
// 浮动优化运行时（enabled）由 autofloat 自己接管 RTSS，本函数不应介入（调用方已跳过）。
async function applyRtssLimitConfig(fps: number): Promise<void> {
  for (let i = 0; i < 12; i++) {
    try {
      await setRtssLimit(fps);
      if (await rtssRunning()) return; // RTSS 已在跑并收到新配置
    } catch {
      /* 继续重试 */
    }
    await new Promise((r) => setTimeout(r, 5000));
  }
}
let stopGamepad: (() => void) | null = null;
// 手柄后台（未开浮动时）调节 RTSS 帧率上限：setRtssLimit 较重（LoadProfile+UpdateProfiles），
// 连发时每步重载会卡出转圈；2 秒尾随防抖，与 CPU 滑块 scheduleActivate 一致。
// 壳订阅系统 AC/DC 插拔事件（推送式，已在 native 侧做 5s 尾防抖 + 频繁切换熔断），
// 到达即刷新一次所有已挂载页面数据。
let stopAcWatch: (() => void) | null = null;
let stopPowerSourceWatch: (() => void) | null = null;
let stopResumeWatch: (() => void) | null = null;
let stopSchemeWatch: (() => void) | null = null;
let stopDynamicBackground: (() => void) | null = null;
let stopUiVisibility: (() => void) | null = null;
let sleepPlanOptimizationTimer: number | null = null;
let sleepPlanOptimizationQueue: Promise<void> = Promise.resolve();
let sleepPlanOptimizationQueued = false;
let sleepPlanOptimizationRunning = false;
let sleepPlanOptimizationPending = false;

// 睡眠电源计划优化属于低频后台任务。串行执行，避免 AC/DC 或方案事件叠加时
// 同时写入多个电源计划；事件处理本身不等待这个队列。
function scheduleSleepPowerPlanOptimization(): void {
  if (sleepPlanOptimizationRunning) {
    sleepPlanOptimizationPending = true;
    return;
  }
  if (sleepPlanOptimizationQueued) return;
  sleepPlanOptimizationQueued = true;
  const run = sleepPlanOptimizationQueue.then(async () => {
    sleepPlanOptimizationQueued = false;
    if (!(await powerLifecycleReadyForWrites())) return;
    if (!(await getSleepPowerPlanOptimizationEnabled())) return;
    sleepPlanOptimizationRunning = true;
    try {
      const result = await optimizeSleepPowerPlans();
      if (!result.ok) console.error('[sleep power plan optimization]', result.failed[0]);
    } catch (error) {
      console.error('[sleep power plan optimization]', error);
    } finally {
      sleepPlanOptimizationRunning = false;
      if (sleepPlanOptimizationPending) {
        sleepPlanOptimizationPending = false;
        window.setTimeout(() => scheduleSleepPowerPlanOptimization(), 0);
      }
    }
  });
  sleepPlanOptimizationQueue = run.catch(() => {});
}
let latestResumeGeneration = 0;
let committedResumeGeneration = 0;
let resumeLifecycleActive = true;
// Recovery notifications are normally delivered as events. WebView2 can be
// recreated during the same window, however, and a freshly mounted page may
// miss the one-shot `power.resume-ready` event. Serialize compensating
// recovery attempts and let newer power generations supersede stale work.
let resumeTransaction: Promise<void> = Promise.resolve();
let resumeRetryTimer: ReturnType<typeof setTimeout> | null = null;
const resumeRetryCounts = new Map<number, number>();
const MAX_RESUME_TRANSACTION_RETRIES = 3;

function scheduleResumeRetry(generation: number, reason: string): void {
  if (!resumeLifecycleActive || generation !== latestResumeGeneration ||
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

async function powerLifecycleReadyForWrites(): Promise<boolean> {
  const state = await powerLifecycle.get().catch(() => null);
  return state?.phase === 'ready' && state.hardwareWritesAllowed === true;
}

function scheduleResumeTransaction(generation: number): void {
  if (!resumeLifecycleActive || !Number.isFinite(generation) || generation <= 0) return;
  latestResumeGeneration = Math.max(latestResumeGeneration, generation);
  if (generation !== latestResumeGeneration) return;
  if (generation <= committedResumeGeneration) return;

  resumeTransaction = resumeTransaction.then(async () => {
    const targetGeneration = latestResumeGeneration;
    if (targetGeneration <= committedResumeGeneration) return;
    const isCurrent = () => resumeLifecycleActive && latestResumeGeneration === targetGeneration &&
      committedResumeGeneration < targetGeneration;

    const mode = await detectPowerModeStable().catch(() => 'ac' as const);
    if (!isCurrent()) return;
    onAcPower.value = mode === 'ac';
    // Do not let a stalled media decoder delay the native wake commit.
    void reconcileBackgroundVideo();

    const daemonRequired = getFloatInfo().enabled ||
      (await getPerformanceScheduleOwnership().catch(() => 'none' as const)) === 'auto';
    const result = await runPowerResumeTransaction(targetGeneration, daemonRequired, {
      resumeDaemon: resumeTdpDaemonAfterWake,
      completeResume: (resumeGeneration, meta) => powerLifecycle.completeResume(resumeGeneration, meta),
      isGenerationCurrent: isCurrent,
      daemonAttempts: 2,
      commitAttempts: 3,
      daemonRetryDelayMs: 350,
      commitRetryDelayMs: 250,
    });
    if (!isCurrent()) return;
    if (!result.committed) {
      scheduleResumeRetry(targetGeneration, result.reason ?? 'resume_commit_failed');
      return;
    }

    committedResumeGeneration = targetGeneration;
    resumeRetryCounts.delete(targetGeneration);
    const schemeOwner = await reconcileRememberedPowerScheme().catch(() => 'yeman' as const);
    if (schemeOwner === 'auto') {
      await restorePerformanceScheduleIfConfigured().catch(() => {});
    } else {
      await applyCpuAutoEnable().catch(() => {});
      await applyAutoTdpIfNeeded('wake', mode).catch(() => {});
    }
    // CCD and undervolt settings use separate native controls from the Windows
    // power plan. Re-apply the remembered safe values after S3/S0 hardware
    // handles and Raw Input are ready.
    await applyCpuAutostart().catch(() => {});
  }).catch((error) => {
    // Unexpected frontend errors must use the same bounded retry path instead
    // of being silently swallowed while native remains in Resuming.
    scheduleResumeRetry(
      latestResumeGeneration,
      error instanceof Error ? error.message : String(error),
    );
  });
}

function onDynamicBackgroundSettingChanged(): void {
  const generation = ++dynamicBackgroundGeneration;
  if (dynamicBackgroundNoGameTimer !== null) {
    window.clearTimeout(dynamicBackgroundNoGameTimer);
    dynamicBackgroundNoGameTimer = null;
  }
  if (!getDynamicBackgroundConfig().enabled) {
    stopDynamicBackgroundWatch();
    lastDynamicGameIdentity = null;
    dynamicBackgroundActive.value = false;
    // Remove the current Steam media immediately. If a fixed MP4 exists,
    // background.get below will install it as a separate, allowed source.
    applyBackgroundState(null, 'fixed');
    void backgroundGet().then((state) => {
      if (generation === dynamicBackgroundGeneration && !getDynamicBackgroundConfig().enabled) {
        const fixedState = state || lastFixedBackgroundState;
        lastFixedBackgroundState = fixedState;
        applyBackgroundState(fixedState, 'fixed');
      }
    }).catch(() => {
      if (generation === dynamicBackgroundGeneration && !getDynamicBackgroundConfig().enabled) {
        applyBackgroundState(lastFixedBackgroundState, 'fixed');
      }
    });
    return;
  }
  if (!isUiVisible()) return;
  startDynamicBackgroundWatch();
  void detectGame(true).then(async (game) => {
    if (generation !== dynamicBackgroundGeneration || !getDynamicBackgroundConfig().enabled) return;
    if (!game) {
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: '未识别到当前游戏' }));
      return;
    }
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `正在访问 Steam：${cleanGameTitle(game.title || game.name)}` }));
    const result = await refreshDynamicBackground(game);
    if (generation !== dynamicBackgroundGeneration || !getDynamicBackgroundConfig().enabled || !result) return;
    lastDynamicGameIdentity = `${game.pid}:${game.processCreated || ''}:${game.path || game.name}`;
    dynamicBackgroundActive.value = true;
    applyBackgroundState(result.state as BackgroundState, 'dynamic');
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
      detail: result.source === 'video'
        ? `正在缓冲在线视频：${result.gameName}`
        : `已使用 Steam 背景图：${result.gameName}`,
    }));
  }).catch((error) => {
    if (generation !== dynamicBackgroundGeneration || !getDynamicBackgroundConfig().enabled) return;
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
  if (!getDynamicBackgroundConfig().enabled) return;

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
    if (generation !== dynamicBackgroundGeneration || !getDynamicBackgroundConfig().enabled) return;
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
      if (!getDynamicBackgroundConfig().enabled) return;
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
    if (!getDynamicBackgroundConfig().enabled || !game) {
      if (dynamicBackgroundNoGameTimer !== null) {
        window.clearTimeout(dynamicBackgroundNoGameTimer);
        dynamicBackgroundNoGameTimer = null;
      }
      // A short no-game interval is normal while a title starts or switches
      // renderers. It is not permission to retain the old title indefinitely.
      if (getDynamicBackgroundConfig().enabled && dynamicBackgroundActive.value) {
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
    const gameIdentity = `${game.pid}:${game.processCreated}:${game.path || game.name}`;
    if (gameIdentity === lastDynamicGameIdentity) return;
    // The old media belongs to a different valve target. Do not display it
    // while Steam data for the new target is still being resolved.
    lastDynamicGameIdentity = null;
    dynamicBackgroundActive.value = false;
    applyBackgroundState(lastFixedBackgroundState, 'fixed');
    try {
      const result = await refreshDynamicBackground(game);
      if (result && generation === dynamicBackgroundGeneration && getDynamicBackgroundConfig().enabled) {
        lastDynamicGameIdentity = gameIdentity;
        dynamicBackgroundActive.value = true;
        applyBackgroundState(result.state as BackgroundState, 'dynamic');
      }
    } catch (error) {
      if (generation !== dynamicBackgroundGeneration || !getDynamicBackgroundConfig().enabled) return;
      const message = (error as Error).message || 'Steam 连接失败';
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${message}` }));
      console.warn('[dynamic background]', error);
    }
  })();
}
function startDynamicBackgroundWatch(): void {
  if (stopDynamicBackground || !getDynamicBackgroundConfig().enabled || !isUiVisible()) return;
  stopDynamicBackground = subscribeGameStatus(onDynamicGameStatus);
}
function stopDynamicBackgroundWatch(): void {
  stopDynamicBackground?.();
  stopDynamicBackground = null;
}
function onDynamicBackgroundLoaded(e: Event): void {
  const state = (e as CustomEvent<BackgroundState>).detail;
  if (!state?.url || !getDynamicBackgroundConfig().enabled) return;
  const generation = ++dynamicBackgroundGeneration;
  void detectGame(true).then((game) => {
    if (generation !== dynamicBackgroundGeneration || !game ||
        !game.processCreated || Number(state.pid) !== game.pid ||
        String(state.processCreated || '') !== String(game.processCreated)) return;
    lastDynamicGameIdentity = `${game.pid}:${game.processCreated}:${game.path || game.name}`;
    dynamicBackgroundActive.value = true;
    applyBackgroundState(state, 'dynamic');
  }).catch(() => {});
}
onMounted(async () => {
  resumeLifecycleActive = true;
  // Register the one-shot native wake events before the first awaited startup
  // operation. A controller recreated during S3 recovery can otherwise miss
  // resume-ready while settings/background initialization is still running.
  window.addEventListener('ipc:power.suspending', onPowerSuspending as EventListener);
  window.addEventListener('ipc:power.resuming', onPowerResuming as EventListener);
  window.addEventListener('ipc:power.resumed', onPowerResumed as EventListener);
  stopResumeWatch = on<{ generation?: number }>('power.resume-ready', ({ generation }) => {
    scheduleResumeTransaction(Number(generation));
  });
  const lifecycleAtMountPromise = powerLifecycle.get().catch(() => null);
  // Use the native-resolved PowerControl directory before startup recovery or
  // TDP calls. Native owns settings.write validation, so deriving this path
  // independently from exeDir can split the frontend/native settings contract
  // on machines where the executable and sidecar directory are laid out
  // differently (notably Intel test systems).
  try {
    const dir = await app.powerControlDir();
    if (dir) {
      setPowerControlDir(dir);
      setAutofloatPowerControlDir(dir);
    }
  } catch { /* fixed formal path remains the compatibility fallback */ }
  await loadUiSettings();
  backgroundOpacity.value = getBackgroundOpacity();
  backgroundBlur.value = getBackgroundBlur();
  backgroundVideoAutoPause.value = getUiSetting('videoBatteryPause');
  window.addEventListener('background:changed', onBackgroundChanged as EventListener);
  window.addEventListener('background:opacity-changed', onBackgroundOpacityChanged as EventListener);
  window.addEventListener('background:blur-changed', onBackgroundBlurChanged as EventListener);
  // Register native lifecycle listeners before the first background read. The
  // initial WebView2 navigation can reveal the native window while this setup
  // is still running; missing that first shown event would leave a video
  // background suspended forever.
  window.addEventListener('ipc:window.minimized', onBackgroundWindowHidden as EventListener);
  window.addEventListener('ipc:window.hidden', onBackgroundWindowHidden as EventListener);
  window.addEventListener('ipc:window.restored', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.shown', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.maximized', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.summoned', onBackgroundWindowShown as EventListener);
  document.addEventListener('visibilitychange', onBackgroundVisibilityChange);
  const initialWindowState = await windowApi.getState().catch(() => null);
  if (initialWindowState) {
    nativeWindowVisible.value = Boolean(initialWindowState.visible && !initialWindowState.minimized);
    setNativeWindowVisible(nativeWindowVisible.value);
  }
  const initialBackgroundState = await backgroundGet().catch(() => null);
  lastFixedBackgroundState = initialBackgroundState;
  applyBackgroundState(initialBackgroundState, 'fixed');
  startDynamicBackgroundWatch();
stopUiVisibility = onUiVisibilityChange(({ visible }) => {
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
  const initialPowerMode = await detectPowerModeReliable();
  if (initialPowerMode) applyVideoPowerMode(initialPowerMode, true);
  else onAcPower.value = (await detectPowerMode().catch(() => 'dc')) === 'ac';
  await reconcileBackgroundVideo();
  window.addEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.addEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  window.addEventListener('ipc:gamepad.tdp-delta', onGamepadTdpDelta as EventListener);
  window.addEventListener('ipc:gamepad.brightness', onGamepadBrightness as EventListener);
  window.addEventListener('ipc:gamepad.mouse-toggle', onGamepadMouseToggle as EventListener);
  stopGamepad = startGamepad({ router, onAction: (l) => store.pushGamepad(l) });
  // 程序启动只同步一次；之后仅在模拟鼠标真实开/关时更新内页锁定。
  void syncMouseModeAtStartup();
  window.addEventListener('background:video-battery-pause-changed', onBackgroundVideoPauseChanged as EventListener);
  window.addEventListener('ui-settings:loaded', onUiSettingsLoaded as EventListener);
  window.addEventListener('ui-settings:changed', onUiSettingsLoaded as EventListener);
  stopPowerSourceWatch = on<{ ac: boolean }>('power.sourceChanged', ({ ac }) => {
    onVideoPowerSourceChanged(ac);
  });
  // AC/DC 插拔：刷新数据；自动模式由性能调度重新应用当前电源侧组合。
  stopAcWatch = on('power.acChanged', async ({ ac }) => {
    onAcPower.value = Boolean(ac);
    onVideoPowerSourceChanged(Boolean(ac));
    globalRefreshKey.value++;
    scheduleSleepPowerPlanOptimization();
    // Resume commit will perform one authoritative re-apply after AC/DC is
    // stable. Ignore intermediate broadcasts while native writes are gated.
    if (!await powerLifecycleReadyForWrites()) return;
    // 性能调度已被用户启用时，优先应用新电源侧的组合；未启用才沿用独立 CPU 自动启用链路。
    const schemeOwner = await reconcileRememberedPowerScheme().catch(() => 'yeman' as const);
    if (schemeOwner === 'auto') {
      await restorePerformanceScheduleIfConfigured().catch(() => {});
    } else {
      await applyCpuAutoEnable().catch(() => {});
    }
  });
  // Compensate for a one-shot resume event that was emitted while WebView2
  // was being recreated.  The native lifecycle remains `resuming` until the
  // same commit path succeeds, so querying it is safe and idempotent.
  const lifecycleAtMount = await lifecycleAtMountPromise;
  if (lifecycleAtMount?.phase === 'resuming') {
    scheduleResumeTransaction(Number(lifecycleAtMount.generation));
  } else if (lifecycleAtMount && lifecycleAtMount.phase !== 'ready') {
    latestResumeGeneration = Math.max(latestResumeGeneration, Number(lifecycleAtMount.generation) || 0);
  }
  // 性能调度统一负责自动模式 CPU/TDP 应用；不再加载或回刷 AC/DC CPU 锁定配置。
  // 性能调度由用户首次选择模式后才接管启动恢复；未配置时保持原 CPU 自动启用链路不变。
  stopSchemeWatch = on('power.schemeChanged', async () => {
    scheduleSleepPowerPlanOptimization();
    // Intermediate broadcasts are expected while Windows restores a scheme.
    // The serialized resume transaction reconciles once after the gate opens.
    if (!await powerLifecycleReadyForWrites()) return;
    await reconcileRememberedPowerScheme()
      .then(async (schemeOwner) => {
        // Refresh the active page after reconciliation without changing ownership.
        globalRefreshKey.value++;
        if (schemeOwner === 'auto') await restorePerformanceScheduleIfConfigured().catch(() => {});
      })
      .catch(() => {});
  });
  // A freshly recreated page can mount while native is still Resuming. Do not
  // let normal boot restoration race the gated wake transaction; the latter
  // performs one authoritative CPU/TDP re-apply after commit.
  if (await powerLifecycleReadyForWrites()) {
    const schemeOwnerAtBoot = await reconcileRememberedPowerScheme().catch(() => 'yeman' as const);
    if (schemeOwnerAtBoot === 'auto') {
      await restorePerformanceScheduleIfConfigured().catch(() => {});
      // Some boot tasks/OEM services rewrite the three hybrid-core values
      // after the first restore.  Reapply only the saved core mode after the
      // system settles; manual mode never enters this path.
      window.setTimeout(() => {
        void refreshPerformanceScheduleCoreMode().catch(() => {});
      }, 3000);
    } else {
      await applyCpuAutoEnable().catch(() => {});
      await applyProgramControls().catch(() => {});
    }
  }

  // ── 任务栏常驻偏好：启动期按持久化套用（默认不常驻，由设置/总闸驱动）──
  applyTrayResident().catch(() => {});
  // ── 音乐播放：启动期恢复已配置的音乐目录（不自动播放）──
  void initMusic();
  // ── 开机启动自愈：若用户曾开启但计划任务被误删，自动重建 ──
  healBootTask().catch(() => {});

  // ── CPU「30秒自行启用」：打开程序 30 秒后，按记录状态自动套用 CCD / 降压 ──
  // 延迟 30 秒是开机保护：避免降压过猛（如 risk 档）在开机瞬间直接死机。
  sleepPlanOptimizationTimer = window.setTimeout(() => {
    sleepPlanOptimizationTimer = null;
    scheduleSleepPowerPlanOptimization();
  }, 30000);
  window.setTimeout(() => {
    applyCpuAutostart().catch(() => {});
  }, 30000);

});
onUnmounted(() => {
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
  if (resumeRetryTimer) {
    clearTimeout(resumeRetryTimer);
    resumeRetryTimer = null;
  }
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
  stopUiVisibility?.();
  stopUiVisibility = null;
  window.removeEventListener('background:changed', onBackgroundChanged as EventListener);
  window.removeEventListener('background:opacity-changed', onBackgroundOpacityChanged as EventListener);
  window.removeEventListener('background:blur-changed', onBackgroundBlurChanged as EventListener);
  window.removeEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.removeEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  window.removeEventListener('ipc:gamepad.tdp-delta', onGamepadTdpDelta as EventListener);
  window.removeEventListener('ipc:gamepad.brightness', onGamepadBrightness as EventListener);
  window.removeEventListener('ipc:gamepad.mouse-toggle', onGamepadMouseToggle as EventListener);
  window.removeEventListener('ipc:window.minimized', onBackgroundWindowHidden as EventListener);
  window.removeEventListener('ipc:window.hidden', onBackgroundWindowHidden as EventListener);
  window.removeEventListener('ipc:window.restored', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.shown', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.maximized', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.summoned', onBackgroundWindowShown as EventListener);
  window.removeEventListener('background:video-battery-pause-changed', onBackgroundVideoPauseChanged as EventListener);
  window.removeEventListener('ipc:power.suspending', onPowerSuspending as EventListener);
  window.removeEventListener('ipc:power.resuming', onPowerResuming as EventListener);
  window.removeEventListener('ipc:power.resumed', onPowerResumed as EventListener);
  window.removeEventListener('ui-settings:loaded', onUiSettingsLoaded as EventListener);
  window.removeEventListener('ui-settings:changed', onUiSettingsLoaded as EventListener);
  window.removeEventListener('dynamic-background:settings-changed', onDynamicBackgroundSettingChanged);
  window.removeEventListener('dynamic-background:loaded', onDynamicBackgroundLoaded as EventListener);
  window.removeEventListener('ipc:game.rules.changed', onGameRulesChanged as EventListener);
  document.removeEventListener('visibilitychange', onBackgroundVisibilityChange);
  stopGamepad?.();
  stopAcWatch?.();
  stopPowerSourceWatch?.();
  stopResumeWatch?.();
  stopSchemeWatch?.();
});

// ── UI 缩放：监听窗口尺寸 / 原生 resize 事件，保持设计比例填满窗口 ──
onMounted(() => {
  updateScale();
  // 首帧后再校准一次（WebView2 宿主尺寸刚就绪时 innerHeight 可能未稳定）
  requestAnimationFrame(updateScale);
  window.addEventListener('resize', updateScale);
  window.addEventListener('ipc:window.resized', updateScale as EventListener);
});
onUnmounted(() => {
  window.removeEventListener('resize', updateScale);
  window.removeEventListener('ipc:window.resized', updateScale as EventListener);
});
</script>

<template>
  <div class="app-root">
    <div class="app-stage" :style="scalerStyle">
      <video
        v-if="backgroundUrl && backgroundKind === 'video' && !backgroundVideoSuspended"
        :key="backgroundVideoGeneration"
        ref="backgroundVideo"
        class="user-background-video"
        :src="isHlsUrl(backgroundUrl) ? undefined : backgroundUrl"
        :style="{ opacity: String(backgroundOpacity) }"
        muted
        loop
        playsinline
        preload="metadata"
        disablepictureinpicture
        tabindex="-1"
        aria-hidden="true"
        @loadedmetadata="onBackgroundVideoLoaded"
        @canplay="onBackgroundVideoCanPlay"
        @playing="onBackgroundVideoPlaying"
        @pause="onBackgroundVideoPause"
        @error="() => onBackgroundVideoError()"
      />
      <div
        v-else-if="backgroundUrl"
        class="user-background-image"
        :style="backgroundStyle"
        aria-hidden="true"
      />
      <div class="app-body">
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
    </div>

    <DebugView v-if="showDebug" @close="showDebug = false" />
  </div>
</template>

<style scoped>
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
.app-body {
  flex: 1 1 auto;
  display: flex;
  min-height: 0;
  position: relative;
  z-index: 1;
}
.app-main {
  flex: 1 1 auto;
  min-width: 0;
  min-height: 0;
  overflow: hidden; /* 页面滚动移到 app-content，监控条不受滚动条影响 */
  padding: 0;
  /* 界面底板不再使用整体毛玻璃；自定义图片自身模糊由背景图片滑块控制。 */
  background: color-mix(in srgb, var(--bg-solid) 62%, transparent);
  position: relative;
  z-index: 1;
}
/* 页面唯一滚动层：监控条在其外部，因此固定在内页顶端且不被滚动条扣宽度。 */
.app-content {
  height: calc(100% - 52px); /* 8px 顶部留白 + 34px 监控条 + 10px 下间距 */
  min-height: 0;
  overflow-y: auto;
  /* Leave room for the gamepad focus ring and the final control itself. */
  padding: 12px 12px calc(var(--gamepad-safe-bottom, 24px) + var(--gamepad-clip-bottom, 0px));
  scroll-padding: 24px 0 var(--gamepad-safe-bottom, 24px);
}
</style>
