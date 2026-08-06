<script setup lang="ts">
import { ref, computed, provide, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { useRouter } from 'vue-router';
import NavRail from '@/components/NavRail.vue';
import DebugView from '@/components/DebugView.vue';
import { useDebugStore } from '@/stores/debug';
import { startGamepad } from '@/gamepad/engine';
import { on } from '@/bridge/ipc';
import { applyCpuAutostart } from '@/bridge/autostart';
import { applyTrayResident } from '@/bridge/trayResident';
import { getFloatInfo, applyCpuAutoEnable } from '@/bridge/autofloat';
import { readTdpAutoApply, applyAutoTdpIfNeeded } from '@/bridge/tdpAutoApply';
import { fs, proc } from '@/bridge/api';
import { initMusic } from '@/bridge/music';
import { readTdp, setTdp, setRtssLimit, readFps, rtssRunning, toggleTask, taskExists, detectPowerMode } from '@/bridge/yeman';
import AppIcon from '@/components/AppIcon.vue';
import TopMonitorBar from '@/components/TopMonitorBar.vue';
import { applyTheme } from '@/bridge/theme';
import { cyclePerformanceScheduleMode, getPerformanceScheduleOwnership, restorePerformanceScheduleIfConfigured } from '@/bridge/performanceSchedule';
import { registerScheduledTask } from '@/scheduler';
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
import { BOOT_CONTROL_CENTER_TASK, bootMirrorExists, toggleBootMirror } from '@/bridge/yeman';

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
    const txt = await fs.readTextFile(BOOT_CFG);
    const j = JSON.parse(txt) as { bootOn?: boolean };
    if (j.bootOn === true && !(await bootMirrorExists())) {
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
const backgroundVideoAutoPause = ref(localStorage.getItem('yeman.ui.video-battery-pause') !== 'false');
const onAcPower = ref(false);
const documentVisible = ref(document.visibilityState === 'visible');
const nativeWindowVisible = ref(document.visibilityState === 'visible');
const videoDesired = ref(false);
const backgroundVideoGeneration = ref(0);
const dynamicBackgroundActive = ref(false);
const backgroundFallbackUrls = ref<string[]>([]);
const backgroundFallbackIndex = ref(0);
const BACKGROUND_RETRY_WINDOW_MS = 2 * 60 * 1000;
const BACKGROUND_RETRY_INTERVAL_MS = 5000;
let backgroundRetryTimer: number | null = null;
let backgroundRetryUntil = 0;
let hlsInstance: { destroy: () => void; on: (event: string, callback: (...args: any[]) => void) => void; loadSource: (url: string) => void; attachMedia: (video: HTMLVideoElement) => void } | null = null;
function isHlsUrl(url: string): boolean {
  return /\.m3u8(?:\?|$)/i.test(url);
}
function destroyHls(): void {
  hlsInstance?.destroy();
  hlsInstance = null;
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
  if (backgroundKind.value !== 'video' || !backgroundUrl.value) return;
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
    if (Date.now() >= backgroundRetryUntil || backgroundKind.value !== 'video' || !backgroundUrl.value) {
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
      const video = backgroundVideo.value;
      if (!video || backgroundKind.value !== 'video') return;
      if (!isHlsUrl(backgroundUrl.value)) video.load();
      await setupBackgroundVideo();
      await reconcileBackgroundVideo();
    });
  }, delay);
}
async function setupBackgroundVideo(): Promise<void> {
  await nextTick();
  const video = backgroundVideo.value;
  if (!video || backgroundKind.value !== 'video' || !backgroundUrl.value) {
    destroyHls();
    return;
  }
  destroyHls();
  if (!isHlsUrl(backgroundUrl.value)) return;
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
    hls.on(HlsCtor.Events.MEDIA_ATTACHED, () => hls.loadSource(backgroundUrl.value));
    const generation = backgroundVideoGeneration.value;
    hls.on(HlsCtor.Events.ERROR, (_event: unknown, data: { fatal?: boolean }) => {
      if (generation === backgroundVideoGeneration.value && data?.fatal) onBackgroundVideoError();
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
function applyBackgroundState(state: BackgroundState | null | undefined): void {
  clearBackgroundRetry();
  destroyHls();
  backgroundVideoGeneration.value++;
  videoDesired.value = false;
  backgroundKind.value = state?.kind === 'video' ? 'video' : 'image';
  const urls = Array.isArray(state?.fallbackUrls)
    ? state.fallbackUrls.filter((url): url is string => typeof url === 'string' && url.length > 0)
    : [];
  backgroundFallbackUrls.value = urls;
  const requestedUrl = state?.enabled && state.url ? state.url : '';
  const index = requestedUrl && urls.length ? Math.max(0, urls.indexOf(requestedUrl)) : 0;
  backgroundFallbackIndex.value = index;
  backgroundUrl.value = requestedUrl || urls[0] || '';
  void nextTick(async () => {
    const video = backgroundVideo.value;
    if (video && backgroundKind.value === 'video' && !isHlsUrl(backgroundUrl.value)) video.load();
    await setupBackgroundVideo();
    await reconcileBackgroundVideo();
  });
}
function onBackgroundChanged(e: Event): void {
  if (dynamicBackgroundActive.value) return;
  applyBackgroundState((e as CustomEvent<BackgroundState>).detail);
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
  const shouldPlay = Boolean(
    video &&
    backgroundKind.value === 'video' &&
    backgroundUrl.value &&
    documentVisible.value &&
    nativeWindowVisible.value &&
    (!backgroundVideoAutoPause.value || onAcPower.value),
  );
  if (videoDesired.value === shouldPlay && (!shouldPlay || !video?.paused)) return;
  videoDesired.value = shouldPlay;
  if (!video) return;
  if (!shouldPlay) {
    video.pause();
    return;
  }
  video.muted = true;
  try {
    await video.play();
  } catch {
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
  const shouldResume = Boolean(
    backgroundKind.value === 'video' &&
    backgroundUrl.value &&
    documentVisible.value &&
    nativeWindowVisible.value &&
    (!backgroundVideoAutoPause.value || onAcPower.value),
  );
  if (shouldResume) {
    videoDesired.value = false;
    window.setTimeout(() => void reconcileBackgroundVideo(), 100);
  }
}

watch([backgroundUrl, backgroundKind], () => {
  void nextTick(reconcileBackgroundVideo);
});

function onBackgroundVideoError(message = 'Steam 在线视频播放失败'): void {
  videoDesired.value = false;
  destroyHls();
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

// ── JoyXoff 模拟鼠标全局状态守护 ──
// 模拟鼠标可能由快捷应用、bat、任务计划或用户手动启动；不能只依赖 QuickAppView 的页面生命周期。
// App 常驻每 5 秒扫描一次，状态变化时同步给 gamepad engine，确保内页聚焦始终被正确抑制。
const JOYXOFF_SCAN_MS = 5000;
let stopJoyxoffSchedule: (() => void) | null = null;
let joyxoffState: boolean | null = null;
let joyxoffPausedByWindow = false;
function startJoyxoffSchedule(): void {
  if (joyxoffPausedByWindow || stopJoyxoffSchedule !== null) return;
  stopJoyxoffSchedule = registerScheduledTask(
    'joyxoff-state',
    JOYXOFF_SCAN_MS,
    syncJoyxoffMouseMode,
    // 窗口事件负责启停；任务本身不依赖 document.hidden，避免托盘/最小化态判断不一致。
    { runImmediately: false },
  );
}
function stopJoyxoffScheduleForWindow(): void {
  stopJoyxoffSchedule?.();
  stopJoyxoffSchedule = null;
}
function pauseJoyxoffForWindow(): void {
  joyxoffPausedByWindow = true;
  stopJoyxoffScheduleForWindow();
}
function resumeJoyxoffForWindow(): void {
  joyxoffPausedByWindow = false;
  void syncJoyxoffMouseMode(true);
  startJoyxoffSchedule();
}
function onJoyxoffWindowVisibility(): void {
  // WebView2 在托盘隐藏时通常会同步 visibilityState；这是 native WM_SIZE/HIDE
  // 事件之外的兜底。恢复时立即同步一次，之后恢复 5 秒扫描。
  if (document.visibilityState === 'visible') resumeJoyxoffForWindow();
  else pauseJoyxoffForWindow();
}
function onBackgroundVisibilityChange(): void {
  documentVisible.value = document.visibilityState === 'visible';
  nativeWindowVisible.value = documentVisible.value;
  onJoyxoffWindowVisibility();
  void reconcileBackgroundVideo();
}
function onBackgroundWindowHidden(): void {
  nativeWindowVisible.value = false;
  pauseJoyxoffForWindow();
  void reconcileBackgroundVideo();
}
function onBackgroundWindowShown(): void {
  nativeWindowVisible.value = true;
  resumeJoyxoffForWindow();
  void reconcileBackgroundVideo();
}
function onBackgroundVideoPauseChanged(e: Event): void {
  backgroundVideoAutoPause.value = Boolean(
    (e as CustomEvent<{ enabled?: boolean }>).detail?.enabled,
  );
  void reconcileBackgroundVideo();
}
async function syncJoyxoffMouseMode(force = false): Promise<void> {
  try {
    const result = await proc.running(['JoyXoff*']);
    const running = Object.entries(result).some(([name, on]) =>
      Boolean(on) && name.toLowerCase().includes('joyxoff'),
    );
    if (force || joyxoffState !== running) {
      joyxoffState = running;
      window.dispatchEvent(new CustomEvent('gp:mouse-mode', { detail: { on: running } }));
    }
  } catch {
    // 扫描失败不擅自解除现有抑制状态，避免 JoyXoff 运行中因一次 IPC 失败恢复页面聚焦。
  }
}

// ── UI 缩放层：设计基准 580×780，整体在窗口中居中显示 ──
// 用户偏好 2026-08-04：在原本「按窗口高度贴齐」基础上再放大 30%，居中后左右两侧留空更协调。
// 视觉允许高度方向溢出（被裁剪的部分由 .app-content 滚动兜底），但宽度方向不许超窗口，避免点击区跑到屏幕外。
const BASE_W = 580;
const BASE_H = 780;
const SCALE_BOOST = 1.3; // "再放大 30%"
const uiScale = ref(1);
function updateScale() {
  if (window.innerHeight <= 0 || window.innerWidth <= 0) {
    uiScale.value = 1;
    return;
  }
  // 用户视觉目标：在原本 innerHeight/BASE_H 的基础上再乘 1.3
  const boosted = (window.innerHeight / BASE_H) * SCALE_BOOST;
  // 宽度兜底：BASE_W * scale 不能超过 innerWidth（避免焦点跑到屏幕外）
  const widthCapped = window.innerWidth / BASE_W;
  uiScale.value = Math.min(boosted, widthCapped);
}
const scalerStyle = computed(() => ({
  width: BASE_W + 'px',
  height: BASE_H + 'px',
  // zoom 同时参与布局和命中测试；transform 只放大绘制层，会让 WebView2
  // 看到的坐标与鼠标点击坐标分离，表现为内容区"看得到但点不到"。
  zoom: uiScale.value,
}));

// ── 启动顺序预加载（TDP → 支持）──
// KeepAlive 只挂载当前页，故需在启动时顺序导航各路由，触发各页 onMounted→refresh
// 预加载期间内容区隐藏（visibility:hidden），避免页面闪烁
const preloading = ref(true);
const PRELOAD_ROUTES = ['/tdp', '/cpu', '/rtss', '/power', '/steam', '/sleep', '/settings'];

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
    const run = gamepadTdpQueue.then(() => applyGamepadTdp(d));
    gamepadTdpQueue = run.catch((error) => {
      console.error('[gamepad tdp-delta]', error);
    });
  }
}
let gamepadTdpQueue: Promise<void> = Promise.resolve();
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
function onGamepadMouseToggle() {
  // native 已异步启动 模拟鼠标.vbs（内部先跑完 JoyXoff.bat 再播提示音）；
  // 立即扫描会拿到切换前的旧状态，延迟 1.5s 再同步一次，随后仍由 5 秒守护兜底。
  window.setTimeout(() => void syncJoyxoffMouseMode(true), 1500);
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
let stopDynamicBackground: (() => void) | null = null;
function onDynamicBackgroundSettingChanged(): void {
  if (!getDynamicBackgroundConfig().enabled) {
    dynamicBackgroundActive.value = false;
    void backgroundGet().then((state) => applyBackgroundState(state)).catch(() => {});
    return;
  }
  void detectGame(true).then((game) => {
    if (!game) {
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: '未识别到当前游戏' }));
      return;
    }
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `正在访问 Steam：${cleanGameTitle(game.title || game.name)}` }));
    return refreshDynamicBackground(game).then((result) => {
      if (!result) return;
      dynamicBackgroundActive.value = true;
      applyBackgroundState(result.state as BackgroundState);
      window.dispatchEvent(new CustomEvent('dynamic-background:progress', {
        detail: result.source === 'video'
          ? `正在缓冲在线视频：${result.gameName}`
          : `已使用 Steam 背景图：${result.gameName}`,
      }));
    });
  }).catch((error) => {
    window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${(error as Error).message || 'Steam 连接失败'}` }));
  });
}
function onDynamicBackgroundLoaded(e: Event): void {
  const state = (e as CustomEvent<BackgroundState>).detail;
  if (!state?.url) return;
  dynamicBackgroundActive.value = true;
  applyBackgroundState(state);
}
onMounted(async () => {
  window.addEventListener('background:changed', onBackgroundChanged as EventListener);
  window.addEventListener('background:opacity-changed', onBackgroundOpacityChanged as EventListener);
  window.addEventListener('background:blur-changed', onBackgroundBlurChanged as EventListener);
  applyBackgroundState(await backgroundGet().catch(() => null));
  stopDynamicBackground = subscribeGameStatus((game: DetectedGame | null) => {
    void (async () => {
      if (!getDynamicBackgroundConfig().enabled || !game) {
        // Process enumeration can briefly return no game while a title is starting
        // or switching renderers. Keep an already-installed dynamic background until
        // a later status update confirms the next game state.
        if (getDynamicBackgroundConfig().enabled && !game && dynamicBackgroundActive.value) return;
        dynamicBackgroundActive.value = false;
        applyBackgroundState(await backgroundGet().catch(() => null));
        return;
      }
      try {
        const result = await refreshDynamicBackground(game);
        if (result) {
          dynamicBackgroundActive.value = true;
          applyBackgroundState(result.state as BackgroundState);
        }
      } catch (error) {
        const message = (error as Error).message || 'Steam 连接失败';
        window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: `失败：${message}` }));
        console.warn('[dynamic background]', error);
      }
    })();
  });
  window.addEventListener('dynamic-background:settings-changed', onDynamicBackgroundSettingChanged);
  window.addEventListener('dynamic-background:loaded', onDynamicBackgroundLoaded as EventListener);
  onAcPower.value = (await detectPowerMode().catch(() => 'dc')) === 'ac';
  await reconcileBackgroundVideo();
  window.addEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.addEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  window.addEventListener('ipc:gamepad.tdp-delta', onGamepadTdpDelta as EventListener);
  window.addEventListener('ipc:gamepad.brightness', onGamepadBrightness as EventListener);
  window.addEventListener('ipc:gamepad.mouse-toggle', onGamepadMouseToggle as EventListener);
  stopGamepad = startGamepad({ router, onAction: (l) => store.pushGamepad(l) });
  // JoyXoff 可能由外部途径启动；启动立即同步一次，之后由统一状态调度器每 5 秒扫描。
  void syncJoyxoffMouseMode(true);
  startJoyxoffSchedule();
  window.addEventListener('ipc:window.minimized', onBackgroundWindowHidden as EventListener);
  window.addEventListener('ipc:window.restored', onBackgroundWindowShown as EventListener);
  window.addEventListener('ipc:window.maximized', onBackgroundWindowShown as EventListener);
  window.addEventListener('background:video-battery-pause-changed', onBackgroundVideoPauseChanged as EventListener);
  document.addEventListener('visibilitychange', onBackgroundVisibilityChange);
  window.addEventListener('ipc:window.minimized', pauseJoyxoffForWindow as EventListener);
  window.addEventListener('ipc:window.restored', resumeJoyxoffForWindow as EventListener);
  window.addEventListener('ipc:window.maximized', resumeJoyxoffForWindow as EventListener);
  stopPowerSourceWatch = on('power.sourceChanged', ({ ac }) => {
    onAcPower.value = Boolean(ac);
    void reconcileBackgroundVideo();
  });
  // AC/DC 插拔：刷新数据；自动模式由性能调度重新应用当前电源侧组合。
  stopAcWatch = on('power.acChanged', async ({ ac }) => {
    onAcPower.value = Boolean(ac);
    void reconcileBackgroundVideo();
    globalRefreshKey.value++;
    // 性能调度已被用户启用时，优先应用新电源侧的组合；未启用才沿用独立 CPU 自动启用链路。
    const scheduleOwner = await restorePerformanceScheduleIfConfigured().catch(() => 'none' as const);
    if (scheduleOwner !== 'auto') {
      if (scheduleOwner === 'none') await applyCpuAutoEnable().catch(() => {});
    }
  });
  // 系统唤醒（S3 用户唤醒 / S0 自动唤醒）：恢复性能调度或独立 CPU 自动启用状态，最后按唤醒规则处理 TDP。
  stopResumeWatch = on('power.resumed', async () => {
    onAcPower.value = (await detectPowerMode().catch(() => 'dc')) === 'ac';
    void reconcileBackgroundVideo();
    const scheduleOwner = await restorePerformanceScheduleIfConfigured().catch(() => 'none' as const);
    if (scheduleOwner !== 'auto') {
      if (scheduleOwner === 'none') await applyCpuAutoEnable().catch(() => {});
      await applyAutoTdpIfNeeded('wake').catch(() => {});
    }
  });
  // 性能调度统一负责自动模式 CPU/TDP 应用；不再加载或回刷 AC/DC CPU 锁定配置。
  // 性能调度由用户首次选择模式后才接管启动恢复；未配置时保持原 CPU 自动启用链路不变。
  const scheduleOwnerAtBoot = await restorePerformanceScheduleIfConfigured().catch(() => 'none' as const);
  if (scheduleOwnerAtBoot !== 'auto') {
    if (scheduleOwnerAtBoot === 'none') await applyCpuAutoEnable().catch(() => {});
    await applyProgramControls().catch(() => {});
  }

  // ── 任务栏常驻偏好：启动期按持久化套用（默认不常驻，由设置/总闸驱动）──
  applyTrayResident().catch(() => {});
  // ── 音乐播放：启动期恢复已配置的音乐目录（不自动播放）──
  void initMusic();
  // ── 开机启动自愈：若用户曾开启但计划任务被误删，自动重建 ──
  healBootTask().catch(() => {});

  // ── CPU「30秒自行启用」：打开程序 30 秒后，按记录状态自动套用 CCD / 降压 ──
  // 延迟 30 秒是开机保护：避免降压过猛（如 risk 档）在开机瞬间直接死机。
  window.setTimeout(() => {
    applyCpuAutostart().catch(() => {});
  }, 30000);

  const home = router.currentRoute.value.fullPath;

  // 沙漏显示的同时立即开始后台顺序预加载（并行，不再空转等待）
  const preload = (async () => {
    for (const r of PRELOAD_ROUTES) {
      if (r === home) continue; // 首页已挂载，跳过
      await router.push(r);
      // 等待组件挂载 + refresh 启动（注册表读取很快）
      await new Promise((res) => setTimeout(res, 150));
    }
    await router.push(home); // 回到首页
  })().catch(() => {});

  // 沙漏至少存在 2 秒；且等预加载结束（数据就绪）后再隐藏，避免闪烁/卡死
  await new Promise((res) => setTimeout(res, 2000));
  await preload;
  preloading.value = false; // 显示内容
});
onUnmounted(() => {
  clearBackgroundRetry();
  destroyHls();
  stopDynamicBackground?.();
  window.removeEventListener('background:changed', onBackgroundChanged as EventListener);
  window.removeEventListener('background:opacity-changed', onBackgroundOpacityChanged as EventListener);
  window.removeEventListener('background:blur-changed', onBackgroundBlurChanged as EventListener);
  window.removeEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.removeEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  window.removeEventListener('ipc:gamepad.tdp-delta', onGamepadTdpDelta as EventListener);
  window.removeEventListener('ipc:gamepad.brightness', onGamepadBrightness as EventListener);
  window.removeEventListener('ipc:gamepad.mouse-toggle', onGamepadMouseToggle as EventListener);
  window.removeEventListener('ipc:window.minimized', onBackgroundWindowHidden as EventListener);
  window.removeEventListener('ipc:window.restored', onBackgroundWindowShown as EventListener);
  window.removeEventListener('ipc:window.maximized', onBackgroundWindowShown as EventListener);
  window.removeEventListener('background:video-battery-pause-changed', onBackgroundVideoPauseChanged as EventListener);
  window.removeEventListener('dynamic-background:settings-changed', onDynamicBackgroundSettingChanged);
  window.removeEventListener('dynamic-background:loaded', onDynamicBackgroundLoaded as EventListener);
  document.removeEventListener('visibilitychange', onBackgroundVisibilityChange);
  window.removeEventListener('ipc:window.minimized', pauseJoyxoffForWindow as EventListener);
  window.removeEventListener('ipc:window.restored', resumeJoyxoffForWindow as EventListener);
  window.removeEventListener('ipc:window.maximized', resumeJoyxoffForWindow as EventListener);
  stopGamepad?.();
  stopAcWatch?.();
  stopPowerSourceWatch?.();
  stopResumeWatch?.();
  stopJoyxoffScheduleForWindow();
  joyxoffPausedByWindow = true;
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
        v-if="backgroundUrl && backgroundKind === 'video'"
        :key="backgroundVideoGeneration"
        ref="backgroundVideo"
        class="user-background-video"
        :src="isHlsUrl(backgroundUrl) ? undefined : backgroundUrl"
        :style="{ opacity: String(backgroundOpacity) }"
        muted
        autoplay
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
        <main class="app-main" :class="{ 'is-preloading': preloading }">
          <!-- 顶部监控条：独立于页面滚动层，左右顶满 app-main，不受滚动条宽度影响 -->
          <TopMonitorBar />
          <div class="app-content">
            <router-view v-slot="{ Component }">
              <KeepAlive>
                <component :is="Component" />
              </KeepAlive>
            </router-view>
          </div>
        </main>
      </div>
    </div>

    <div v-if="preloading" class="splash">
      <div class="splash-spin"><AppIcon name="refresh" /></div>
      <div class="splash-text">正在预加载数据…</div>
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
  padding: 12px;
}
/* 预加载期间隐藏内容区（导航在后台进行，无闪烁） */
.app-main.is-preloading {
  visibility: hidden;
}
.splash {
  position: fixed;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  /* 启动遮罩保持近实色，避免预加载期透出桌面产生闪烁 */
  background: rgba(11, 14, 19, 0.9);
  z-index: 50;
}
.splash-spin {
  font-size: 30px;
  animation: spin 1.1s linear infinite;
}
.splash-text {
  font-size: 13px;
  color: var(--text-dim);
}
@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>
