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
import { detectGame, type DetectedGame, clearGameCache } from '@/bridge/gamedetect';
import { onUiVisibilityChange } from '@/bridge/uiLifecycle';

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

// ── 游戏运行中拦截：关闭 RTSS / 复位会卸载钩子，可能让游戏闪退。
// 检测到游戏时延迟执行该操作，顶部条幅提示并轮询；游戏关闭后自动继续，用户可取消。
type PendingRtssAction = 'stopRtss' | 'reset';
const gameRunningWarn = ref<{ game: DetectedGame; pending: PendingRtssAction; pendingArg?: 'W' | 'L' | 'J' | 'off' } | null>(null);
let gameWatchTimer: number | null = null;
let stopUiVisibility: (() => void) | null = null;

async function guardGameRunning(action: PendingRtssAction, arg?: 'W' | 'L' | 'J' | 'off'): Promise<boolean> {
  // 游戏运行中 → true（已挂起，调用方应直接 return）；未运行 → false（调用方继续）
  const game = await detectGame();
  if (!game) { clearGameCache(); return false; }
  gameRunningWarn.value = { game, pending: action, pendingArg: arg };
  startGameWatch();
  return true;
}

function startGameWatch() {
  stopGameWatch();
  gameWatchTimer = window.setInterval(async () => {
    const warn = gameRunningWarn.value;
    if (!warn) { stopGameWatch(); return; }
    clearGameCache();
    const game = await detectGame(true); // 强制重跑，绕开 5 秒缓存
    if (!game) {
      const pending = warn.pending;
      const arg = warn.pendingArg;
      gameRunningWarn.value = null;
      stopGameWatch();
      if (pending === 'stopRtss') await doToggleRtssOff();
      else if (pending === 'reset') { confirmingReset.value = true; await doReset(); }
    } else {
      // 仍在跑：刷新当前游戏信息
      gameRunningWarn.value = { game, pending: warn.pending, pendingArg: warn.pendingArg };
    }
  }, 2000);
}

function stopGameWatch() {
  if (gameWatchTimer !== null) {
    window.clearInterval(gameWatchTimer);
    gameWatchTimer = null;
  }
}

function cancelGameWatch() {
  gameRunningWarn.value = null;
  stopGameWatch();
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
  errMsg.value = '';
  if (rtssOn.value) {
    // 关闭 RTSS：游戏在跑则延迟到游戏关闭后执行（避免闪退游戏）
    if (await guardGameRunning('stopRtss')) return;
    await doToggleRtssOff();
  } else {
    // 启动 RTSS：与运行中的游戏无冲突，可直接执行
    busy.value = true;
    try {
      await toggleRtss(true);
      rtssOn.value = true;
    } catch (e) {
      errMsg.value = 'RTSS 启动失败：' + (e as Error).message;
    } finally {
      busy.value = false;
    }
  }
}

async function doToggleRtssOff() {
  busy.value = true;
  try {
    await toggleRtss(false);
    rtssOn.value = false;
  } catch (e) {
    errMsg.value = 'RTSS 关闭失败：' + (e as Error).message;
  } finally {
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
  // 复位会关闭 RTSS：游戏在跑则延迟到游戏关闭后自动执行
  if (await guardGameRunning('reset')) return;
  // 两步内联确认：避免调用原生 dialog.confirm（会阻塞 WebView2 渲染线程）
  confirmingReset.value = true;
}

async function doReset() {
  confirmingReset.value = false;
  errMsg.value = '';
  busy.value = true;
  try {
    await toggleRtss(false);
    rtssOn.value = false;
    await setRtssLimit(0);
    await saveFps('ac', 0);
    await saveFps('dc', 0);
    // 显式复位清除全局 AC/DC 帧率；专用游戏记录保持独立，不删除。
    await toggleBootRtss(false);
    tasks.bootRtss = false;
    await setOverlayLayout('off');
    monOn.value = false;
  } catch (e) {
    errMsg.value = '复位失败：' + (e as Error).message;
  } finally {
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
  stopUiVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) {
      stopGameWatch();
    } else if (gameRunningWarn.value) {
      startGameWatch();
    }
  });
});
onActivated(() => {
  refresh().catch(() => {});
});
// KeepAlive 缓存下失活/卸载时清理轮询与防抖定时器，避免隐藏页继续每 2 秒
// 跑 PowerShell 检测游戏、或卸载后回调访问已销毁组件（2026-08-05 修复）。
onDeactivated(() => {
  stopGameWatch();
  if (zoomTimer !== null) { window.clearTimeout(zoomTimer); zoomTimer = null; }
});
onUnmounted(() => {
  stopGameWatch();
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
          <button class="action-btn ghost" data-gp-row="7" data-gp-col="1" @click="confirmingReset = false">取消</button>
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
