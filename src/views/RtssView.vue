<script setup lang="ts">
import { ref, reactive, onMounted, onUnmounted, onActivated, onDeactivated, nextTick, inject, watch, type Ref } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import SteamMonitorSettings from '@/components/SteamMonitorSettings.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import SegButton from '@/components/SegButton.vue';
import {
  rtssRunning,
  setRtssLimit,
  toggleRtss,
  readOverlayLayout,
  setOverlayLayout,
  readRtssZoom,
  setRtssZoom,
  RTSS_ZOOM_MIN,
  RTSS_ZOOM_MAX,
  monitorOn,
  saveFps,
  taskExists,
  toggleTask,
  BOOT_RTSS_TASK,
  toggleBootRtss,
  readBootRtssState,
  BOOT_MIRROR_CHANGED_EVENT,
} from '@/bridge/yeman';
import { detectGame, getCurrentGame, isGameStatusCurrent, subscribeGameStatus, subscribeGameTargetReadiness, type DetectedGame } from '@/bridge/gamedetect';
import { getUiVisibilityState, onUiVisibilityChange } from '@/bridge/uiLifecycle';

const rtssOn = ref(false);
const monOn = ref(false);
const tasks = reactive({ bootRtss: false });
type TaskKey = keyof typeof tasks;
const overlay = ref<'W' | 'L' | 'J'>('W');
let overlayRevision = 0;
const busy = ref(false);
const errMsg = ref('');
const confirmingReset = ref(false);
const monitorHint = ref('');
const zoomPct = ref(100); // OSD 缩放百分比（= ZoomRatio × 20），默认 100%
const zoomHint = ref('');

// RTSS effects consume the unique game owner's current no-game result.
// The original 2s wait belongs only to this explicit request.
type PendingRtssAction = 'stopRtss' | 'reset';
interface PendingRtssRequest {
  action: PendingRtssAction;
  epoch: number;
  noGameRevision: number | null;
  effectsStarted: boolean;
}
const gameRunningWarn = ref<{ game: DetectedGame; pending: PendingRtssAction } | null>(null);
let gameWatchTimer: number | null = null;
let gameWatchRead: PendingRtssRequest | null = null;
let stopUiVisibility: (() => void) | null = null;
let rtssPageActive = false;
let rtssPageDisposed = false;
let rtssRequestEpoch = 0;
let rtssGameRevision = 0;
let pendingRtssRequest: PendingRtssRequest | null = null;
let stopRtssObservations: Array<() => void> = [];

function rtssPageCurrent(epoch: number): boolean {
  return !rtssPageDisposed && rtssPageActive && epoch === rtssRequestEpoch &&
    getUiVisibilityState().visible;
}
function rtssRequestCurrent(request: PendingRtssRequest): boolean {
  return pendingRtssRequest === request && rtssPageCurrent(request.epoch);
}
function rtssNoGameCurrent(request: PendingRtssRequest): boolean {
  return rtssRequestCurrent(request) && request.noGameRevision !== null &&
    request.noGameRevision === rtssGameRevision && isGameStatusCurrent() &&
    getCurrentGame() === null;
}
function requireRtssNoGame(request: PendingRtssRequest): void {
  if (!rtssNoGameCurrent(request)) throw Error('RTSS_NO_GAME_REQUEST_EXPIRED');
}
function stopGameWatch() {
  if (gameWatchTimer !== null) {
    window.clearInterval(gameWatchTimer);
    gameWatchTimer = null;
  }
}
function finishRtssRequest(request: PendingRtssRequest): void {
  if (pendingRtssRequest !== request) return;
  pendingRtssRequest = null;
  gameRunningWarn.value = null;
  confirmingReset.value = false;
  stopGameWatch();
  for (const stop of stopRtssObservations) stop();
  stopRtssObservations = [];
}
function cancelGameWatch(): void {
  ++rtssRequestEpoch;
  const request = pendingRtssRequest;
  if (request) finishRtssRequest(request);
  else {
    gameRunningWarn.value = null;
    confirmingReset.value = false;
    stopGameWatch();
  }
}
function beginRtssRequest(action: PendingRtssAction): PendingRtssRequest {
  cancelGameWatch();
  const request: PendingRtssRequest = {
    action, epoch: rtssRequestEpoch, noGameRevision: null, effectsStarted: false,
  };
  pendingRtssRequest = request;
  // Invalidation counters only: no independent facts, selector or timer.
  stopRtssObservations = [
    subscribeGameStatus(() => { ++rtssGameRevision; }),
    subscribeGameTargetReadiness(() => { ++rtssGameRevision; }),
  ];
  return request;
}
async function guardGameRunning(request: PendingRtssRequest): Promise<boolean> {
  if (!rtssRequestCurrent(request)) return true;
  request.noGameRevision = null;
  try {
    const game = await detectGame(true);
    if (!rtssRequestCurrent(request)) return true;
    if (!isGameStatusCurrent()) throw Error('GAME_TARGET_OBSERVATION_UNAVAILABLE');
    if (game) {
      confirmingReset.value = false;
      gameRunningWarn.value = { game, pending: request.action };
      startGameWatch(request);
      return true;
    }
    if (getCurrentGame() !== null) throw Error('GAME_TARGET_SELECTION_CHANGED');
    request.noGameRevision = rtssGameRevision;
    gameRunningWarn.value = null;
    stopGameWatch();
    return false;
  } catch {
    if (rtssRequestCurrent(request)) {
      errMsg.value = '游戏状态待确认，未执行 RTSS 关闭或复位。';
      // Waiting may reuse its original timer; initial unknown starts no retry.
      if (!gameRunningWarn.value) finishRtssRequest(request);
    }
    return true;
  }
}
function startGameWatch(request: PendingRtssRequest): void {
  if (gameWatchTimer !== null || !rtssRequestCurrent(request)) return;
  gameWatchTimer = window.setInterval(async () => {
    if (!rtssRequestCurrent(request) || busy.value || gameWatchRead) return;
    gameWatchRead = request;
    try {
      if (request.action === 'stopRtss') await doToggleRtssOff(request);
      else await doReset();
    } finally {
      if (gameWatchRead === request) gameWatchRead = null;
    }
  }, 2000);
}
// DC 电池模式锁帧任务已移除（锁帧节能完全交给自动浮动优化），保留 FPS 上限档位常量不再需要 dcOpts

async function refresh() {
  if (busy.value) return;
  const revision = overlayRevision;
  errMsg.value = '';
  // 并行异步加载所有数据（不串行等待，不阻塞渲染）
  const [rtssRes, monRes, layRes, bootTaskRes, zoomRes] = await Promise.allSettled([
    rtssRunning(), monitorOn(), readOverlayLayout(), readBootRtssState(), readRtssZoom(),
  ]);
  if (rtssRes.status === 'fulfilled') rtssOn.value = rtssRes.value;
  if (!busy.value && revision === overlayRevision && monRes.status === 'fulfilled') monOn.value = monRes.value;
  if (!busy.value && revision === overlayRevision && layRes.status === 'fulfilled') {
    if (layRes.value === 'YeManOBS-L-1.ovl') overlay.value = 'L';
    else if (layRes.value === 'YeManOBS-JJ-1.ovl') overlay.value = 'J';
    else if (layRes.value === 'YeManOBS-W-1.ovl') overlay.value = 'W';
    // Empty.ovl must not discard the last visible choice in this page.
  }
  if (bootTaskRes.status === 'fulfilled') tasks.bootRtss = bootTaskRes.value;
  if (zoomRes.status === 'fulfilled') zoomPct.value = zoomRes.value * 20;
}

async function toggleRtssOn() {
  if (busy.value || !rtssPageCurrent(rtssRequestEpoch)) return;
  errMsg.value = '';
  if (rtssOn.value) {
    await doToggleRtssOff(beginRtssRequest('stopRtss'));
  } else {
    cancelGameWatch();
    const epoch = rtssRequestEpoch;
    busy.value = true;
    try {
      await toggleRtss(true, async () => rtssPageCurrent(epoch));
      if (rtssPageCurrent(epoch)) rtssOn.value = true;
    } catch (e) {
      if (rtssPageCurrent(epoch)) errMsg.value = 'RTSS 启动失败：' + (e as Error).message;
    } finally {
      busy.value = false;
    }
  }
}
async function doToggleRtssOff(request: PendingRtssRequest) {
  if (busy.value || !rtssRequestCurrent(request) || request.effectsStarted) return;
  busy.value = true;
  try {
    if (await guardGameRunning(request)) return;
    requireRtssNoGame(request);
    request.effectsStarted = true; // Never replay an uncertain dispatched effect.
    await toggleRtss(false, async () => rtssNoGameCurrent(request), true);
    requireRtssNoGame(request);
    rtssOn.value = false;
  } catch (e) {
    if (rtssRequestCurrent(request)) errMsg.value = 'RTSS 关闭失败：' + (e as Error).message;
  } finally {
    if (request.effectsStarted) finishRtssRequest(request);
    busy.value = false;
  }
}
async function onOverlay(v: 'W' | 'L' | 'J') {
  if (busy.value) return;
  ++overlayRevision;
  errMsg.value = '';
  busy.value = true;
  try {
    const result = await setOverlayLayout(v);
    overlay.value = v;
    monOn.value = true; // Choosing a visible template also enables monitoring.
    monitorHint.value = result.mode === 'live' ? '监控样式已完整切换，无需重启' : '监控样式已保存，将在 RTSS 启动后生效';
    setTimeout(() => (monitorHint.value = ''), 6000);
  } catch (e) {
    errMsg.value = '布局切换失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function toggleMonitor() {
  if (busy.value) return;
  ++overlayRevision;
  errMsg.value = '';
  busy.value = true;
  const enable = !monOn.value;
  try {
    const result = await setOverlayLayout(enable ? overlay.value : 'off');
    monOn.value = enable;
    monitorHint.value = result.mode === 'live'
      ? (enable ? '监控数据已实时开启，无需重启' : '监控数据已实时关闭，无需重启')
      : '监控状态已保存，将在 RTSS 启动后生效';
    setTimeout(() => (monitorHint.value = ''), 6000);
  } catch (e) {
    errMsg.value = '监控切换失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// RTSS OSD 缩放：拖动结束（commit）后才下发，与程序内其它滑块一致（拖动中不写，延迟生效）。
// OSD 缩放无需像电源方案那样等 2 秒激活——写入 + 重载后 RTSS 实时读取，游戏内监控即时缩放。
let zoomTimer: number | null = null;
function onZoomCommit(z: number) {
  zoomPct.value = z;
  const ratio = Math.round(z / 20); // 百分比 → ZoomRatio 整数
  if (zoomTimer !== null) window.clearTimeout(zoomTimer);
  zoomTimer = window.setTimeout(() => {
    zoomTimer = null;
    errMsg.value = '';
    setRtssZoom(ratio)
      .then(() => {
        zoomHint.value = 'OSD 缩放已更新，游戏中监控即时生效';
        setTimeout(() => (zoomHint.value = ''), 4000);
      })
      .catch((e) => {
        errMsg.value = 'OSD 缩放设置失败：' + (e as Error).message;
      });
  }, 500);
}

async function toggleTaskSafe(name: string, on: boolean, key: TaskKey) {
  errMsg.value = '';
  busy.value = true;
  try {
    tasks[key] = name === BOOT_RTSS_TASK ? await toggleBootRtss(on) : await toggleTask(name, on);
  } catch (e) {
    tasks[key] = !on; // 回滚
    errMsg.value = '任务计划操作失败：' + (e as Error).message + '（需管理员权限）';
  } finally {
    busy.value = false;
  }
}

async function resetAll() {
  if (busy.value || !rtssPageCurrent(rtssRequestEpoch)) return;
  const request = beginRtssRequest('reset');
  errMsg.value = '';
  busy.value = true;
  try {
    if (await guardGameRunning(request)) return;
    if (rtssRequestCurrent(request)) confirmingReset.value = true;
  } finally {
    busy.value = false;
  }
}
async function doReset() {
  const request = pendingRtssRequest;
  if (busy.value || !request || request.action !== 'reset' ||
      !rtssRequestCurrent(request) || request.effectsStarted) return;
  errMsg.value = '';
  busy.value = true;
  try {
    // Manual confirmation and automatic wait completion share this fresh gate.
    if (await guardGameRunning(request)) return;
    requireRtssNoGame(request);
    confirmingReset.value = false;
    request.effectsStarted = true;
    const current = async () => rtssNoGameCurrent(request);
    await toggleRtss(false, current, true);
    requireRtssNoGame(request);
    rtssOn.value = false;
    await setRtssLimit(0, () => rtssNoGameCurrent(request));
    requireRtssNoGame(request);
    await saveFps('ac', 0);
    requireRtssNoGame(request);
    await saveFps('dc', 0);
    requireRtssNoGame(request);
    // Original global reset only; dedicated records remain independent.
    await toggleBootRtss(false);
    requireRtssNoGame(request);
    tasks.bootRtss = false;
    await setOverlayLayout('off', undefined, current);
    requireRtssNoGame(request);
    monOn.value = false;
  } catch (e) {
    if (rtssRequestCurrent(request)) errMsg.value = '复位失败：' + (e as Error).message;
  } finally {
    if (request.effectsStarted) finishRtssRequest(request);
    busy.value = false;
  }
}
// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => refresh());
}

onMounted(() => nextTick(refresh));
async function onBootMirrorChanged() {
  try {
    tasks.bootRtss = await readBootRtssState();
  } catch {
  }
}
onMounted(() => window.addEventListener(BOOT_MIRROR_CHANGED_EVENT, onBootMirrorChanged));
onUnmounted(() => window.removeEventListener(BOOT_MIRROR_CHANGED_EVENT, onBootMirrorChanged));
onMounted(() => {
  rtssPageActive = true;
  stopUiVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) cancelGameWatch();
    // Showing the page cannot revive a cancelled destructive request.
  });
});
onActivated(() => {
  rtssPageActive = true;
  refresh().catch(() => {});
});
onDeactivated(() => {
  rtssPageActive = false;
  cancelGameWatch();
  if (zoomTimer !== null) { window.clearTimeout(zoomTimer); zoomTimer = null; }
});
onUnmounted(() => {
  rtssPageDisposed = true;
  rtssPageActive = false;
  cancelGameWatch();
  if (zoomTimer !== null) { window.clearTimeout(zoomTimer); zoomTimer = null; }
  stopUiVisibility?.();
  stopUiVisibility = null;
});
</script>

<template>
  <div class="page" data-gp-scope="monitor">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="monitorHint" class="info-bar">{{ monitorHint }}</div>
    <div v-if="zoomHint" class="info-bar">{{ zoomHint }}</div>
    <div v-if="gameRunningWarn" class="game-warn-bar">
      <div class="game-warn-text">
        <InlineIcon name="warning" />
        检测到游戏「<strong>{{ gameRunningWarn.game.name }}</strong>」正在运行（PID {{ gameRunningWarn.game.pid }}）。
        当前操作会导致游戏闪退（停止 RTSS / 复位）。请先关闭游戏，正在监控等待…
      </div>
      <button class="action-btn ghost" data-gp-row="-1" data-gp-col="0" @click="cancelGameWatch">取消</button>
    </div>

    <section class="card">
      <div class="monitor-template-head" data-gp-group="monitor-template">
        <h3 class="card-title"><InlineIcon name="monitor" /> 监控模板</h3>
        <div class="monitor-template-switches">
          <Toggle :model-value="rtssOn" label="RTSS" compact :disabled="busy" :gp-row="0" :gp-col="0" @update:model-value="toggleRtssOn" />
          <Toggle :model-value="monOn" label="监控数据" compact :disabled="busy" :gp-row="0" :gp-col="1" @update:model-value="toggleMonitor" />
        </div>
      </div>
      <Slider
        v-model="zoomPct"
        :min="RTSS_ZOOM_MIN * 20"
        :max="RTSS_ZOOM_MAX * 20"
        :step="20"
        gp-row="1"
        gp-col="0"
        label="监控大小"
        unit="%"
        color="accent"
        :disabled="busy"
        @commit="onZoomCommit"
      />
      <SegButton
        gp-row="2"
        :model-value="overlay"
        :disabled="busy"
        :options="[
          { value: 'W', label: '横版监控' },
          { value: 'L', label: '竖版监控' },
          { value: 'J', label: '极端简单' },
        ]"
        color="accent"
        full
        @update:model-value="(v: string) => onOverlay(v as 'W' | 'L' | 'J')"
      />
    </section>

    <SteamMonitorSettings :gp-row-start="3" />

    <section class="card">
      <div v-if="confirmingReset" class="confirm-bar">
        <span class="confirm-text">确认复位 RTSS 全部设置？将关闭 RTSS、清除锁帧与所有相关任务、关闭监控显示。</span>
        <div class="confirm-actions">
          <button class="action-btn" data-gp-row="7" data-gp-col="0" :disabled="busy" @click="doReset">确认复位</button>
          <button class="action-btn ghost" data-gp-row="7" data-gp-col="1" @click="cancelGameWatch">取消</button>
        </div>
      </div>
      <button class="danger-btn" data-gp-row="7" data-gp-col="0" :disabled="busy || confirmingReset" @click="resetAll">复位 RTSS 全部设置</button>
    </section>
  </div>
</template>

<style scoped>
.monitor-template-head { display:flex; align-items:center; justify-content:space-between; gap:12px; margin-bottom:10px; }
.monitor-template-head .card-title { margin:0; flex-shrink:0; }
.monitor-template-switches { display:flex; align-items:center; justify-content:flex-end; gap:18px; }
@media(max-width:420px) { .monitor-template-switches { gap:10px; } }

.page {
  padding-bottom: 20px;
}
.states {
  display: flex;
  flex-direction: row;
  gap: 8px;
  background: transparent;
  padding: 0;
}
.states .state-card {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 12px 8px 10px;
  min-height: 74px;
  border: 1px solid #2a3342;
  transition: border-color 0.15s, box-shadow 0.15s;
  gap: 6px;
}
.states .state-card.clickable:hover {
  border-color: var(--accent);
  box-shadow: 0 0 10px rgba(46,166,255,.2);
}
.states :deep(.sc-body) {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-end;
  min-height: 40px;
  width: 100%;
}
.states :deep(.sc-title) {
  line-height: 18px;
  min-height: 18px;
  text-align: center;
}
.states :deep(.sc-text) {
  line-height: 20px;
  min-height: 20px;
  margin-top: 4px;
  text-align: center;
}
.clickable {
  cursor: pointer;
}
.err-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.info-bar {
  background: rgba(46, 166, 255, 0.10);
  border: 1px solid rgba(46, 166, 255, 0.35);
  color: #8fd1ff;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.lock-combo {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 120px;
  align-items: end;
  gap: 10px;
  margin-top: 8px;
}
.small {
  font-size: 11px;
  margin: 0 0 8px;
}
.hint-small {
  font-size: 10.5px;
  color: var(--text-dim, #8a97a8);
  line-height: 1.45;
  margin: 6px 2px 0;
}
.action-btn {
  flex: 1;
  width: 100%;
  margin: 0;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: var(--btn-py) var(--btn-px);
  min-height: var(--btn-min-h);
  background: var(--accent);
  color: #06121d;
  font-weight: 700;
  font-size: var(--btn-font-size);
  cursor: pointer;
}
.action-btn.ghost {
  background: var(--bg-input);
  color: var(--text);
}
.action-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.danger-btn {
  margin-top: 10px;
  width: 100%;
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.5);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: var(--btn-py) var(--btn-px);
  min-height: var(--btn-min-h);
  cursor: pointer;
  font-size: var(--btn-font-size);
  font-weight: 600;
}
.conflict-bar {
  background: rgba(245, 185, 61, 0.12);
  border: 1px solid rgba(245, 185, 61, 0.4);
  color: #f5b93d;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.45;
}
.game-warn-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  background: rgba(245, 185, 61, 0.12);
  border: 1px solid rgba(245, 185, 61, 0.45);
  color: #f5b93d;
  border-radius: var(--radius-ctrl);
  padding: 10px 12px;
  font-size: 12px;
  margin-bottom: 10px;
  line-height: 1.45;
}
.game-warn-text { flex: 1 1 auto; }
.game-warn-text strong { color: #fff; }
.card.conflict {
  opacity: 0.85;
}
.danger-btn:hover {
  background: var(--danger);
  color: #fff;
}
.danger-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.danger-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.confirm-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 12px 14px;
  margin-bottom: 10px;
  font-size: 13px;
  line-height: 1.55;
}
.confirm-text {
  display: block;
  margin-bottom: 10px;
}
.confirm-actions {
  display: flex;
  gap: 8px;
}
.confirm-actions .action-btn {
  width: auto;
  flex: 1;
  margin: 0;
  min-height: 36px;
  font-size: 13px;
  background: #e5484d;
  color: #fff;
}
.confirm-actions .action-btn.ghost {
  background: var(--bg-input);
  color: var(--text);
}
</style>
