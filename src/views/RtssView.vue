<script setup lang="ts">
import { ref, reactive, computed, onMounted, onUnmounted, onActivated, onDeactivated, nextTick, inject, watch, type Ref } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import StateCard from '@/components/StateCard.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import SegButton from '@/components/SegButton.vue';
import {
  rtssRunning,
  readRtssLimit,
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
  readFps,
  FPS_MIN,
  FPS_CEILINGS,
  taskExists,
  toggleTask,
  BOOT_RTSS_TASK,
  toggleBootRtss,
  readBootRtssState,
  BOOT_MIRROR_CHANGED_EVENT,
} from '@/bridge/yeman';
import { detectGame, type DetectedGame, clearGameCache } from '@/bridge/gamedetect';
import { readSettingsSection } from '@/bridge/settingsRepository';
import { onUiVisibilityChange } from '@/bridge/uiLifecycle';

// 自动浮动优化的「自动启用」模式（与 CpuView 共用同一文件）
const AUTO_ENABLE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_auto_enable.json';

const rtssOn = ref(false);
const monOn = ref(false);
const lockOn = ref(false);
const fps = ref(60); // 默认 60（掌机常用）
const fpsCeiling = ref(200); // 下拉上限（滑块最大值）
const tasks = reactive({ bootRtss: false });
type TaskKey = keyof typeof tasks;
const overlay = ref<'W' | 'L' | 'J'>('W');
// 自动浮动优化「自动启用」模式：ac/always 时会接管插电调度与锁帧逻辑。
// （插电恢复锁帧任务已移除、DC 电池模式锁帧任务已移除：锁帧节能完全交给自动浮动优化）
const autoEnableMode = ref<string>('never');
// 自动浮动优化（接通电源 / 总是）会接管锁帧调度 → 锁帧卡片置灰
const acConflict = computed(() => autoEnableMode.value === 'ac' || autoEnableMode.value === 'always');
const busy = ref(false);
const errMsg = ref('');
const confirmingReset = ref(false);
const monitorHint = ref('');
const zoomPct = ref(100); // OSD 缩放百分比（= ZoomRatio × 20），默认 100%
const zoomHint = ref('');

// ── 游戏运行中拦截：关闭 RTSS / 切换布局（RTSS 重启）/ 复位 都会让游戏闪退。
// 检测到游戏时延迟执行该操作，顶部条幅提示并轮询；游戏关闭后自动继续，用户可取消。
type PendingRtssAction = 'stopRtss' | 'changeOverlay' | 'toggleMonitor' | 'reset';
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
      else if (pending === 'changeOverlay' && arg && arg !== 'off') await doSetOverlay(arg);
      else if (pending === 'toggleMonitor' && arg) {
        await setOverlayLayout(arg);
        monOn.value = arg !== 'off';
      }
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

// FPS 上限档位（对齐 TDP 页面 ceilingOpts 下拉逻辑；0 = 不锁帧）
const fpsCeilingOpts = FPS_CEILINGS.map((v) => ({ value: v, label: v === 0 ? '不锁帧' : v + ' FPS' }));
function smallestFpsCeiling(val: number): number {
  for (const c of FPS_CEILINGS) if (c >= val) return c;
  return FPS_CEILINGS[FPS_CEILINGS.length - 1];
}

async function safeExists(name: string): Promise<boolean> {
  try {
    return await taskExists(name);
  } catch {
    return false;
  }
}

async function refresh() {
  errMsg.value = '';
  // 并行异步加载所有数据（不串行等待，不阻塞渲染）
  const [rtssRes, monRes, limRes, fpsCfgRes, layRes, bootTaskRes, zoomRes, aeRes] = await Promise.allSettled([
    rtssRunning(),
    monitorOn(),
    readRtssLimit(),
    readFps('ac'),
    readOverlayLayout(),
    readBootRtssState(),
    readRtssZoom(),
    readSettingsSection<any>('cpu'),
  ]);
  // 逐项赋值（不阻塞 UI）
  if (rtssRes.status === 'fulfilled') rtssOn.value = rtssRes.value;
  if (monRes.status === 'fulfilled') monOn.value = monRes.value;
  if (fpsCfgRes.status === 'fulfilled' && fpsCfgRes.value != null) {
    const configured = fpsCfgRes.value;
    fps.value = configured > 0 ? configured : 90;
  } else if (limRes.status === 'fulfilled') {
    const lim = limRes.value;
    fps.value = lim > 0 ? lim : 90;
  }
  fpsCeiling.value = smallestFpsCeiling(fps.value);
  if (limRes.status === 'fulfilled') lockOn.value = limRes.value > 0;
  if (layRes.status === 'fulfilled') {
    overlay.value = layRes.value === 'YeManOBS-L-1.ovl' ? 'L' : layRes.value === 'YeManOBS-JJ-1.ovl' ? 'J' : 'W';
  }
  if (bootTaskRes.status === 'fulfilled') tasks.bootRtss = bootTaskRes.value;
  if (aeRes.status === 'fulfilled') autoEnableMode.value = aeRes.value.autoEnable?.mode || 'never';
  else autoEnableMode.value = 'never';
  if (zoomRes.status === 'fulfilled') zoomPct.value = zoomRes.value * 20;
}

async function onFpsCommit(v: number) {
  errMsg.value = '';
  try {
    await saveFps('ac', v);
    void setRtssLimit(v).catch(() => {});
    lockOn.value = v > 0;
  } catch (e) {
    errMsg.value = '锁帧保存失败：' + (e as Error).message;
  }
}

// FPS 上限档位选择（对齐 TDP onQuickCeiling：只改范围；若当前值超出新上限才钳制并提交；
// 选「不锁帧(0)」立即解锁并联动右上角 FPS 锁帧状态为已关闭）
function onFpsCeiling(val: number) {
  fpsCeiling.value = val;
  if (val === 0) {
    fps.value = 0;
    void onFpsCommit(0);
    return;
  }
  if (fps.value > val) {
    fps.value = val;
    void onFpsCommit(val);
  }
}

async function toggleLock() {
  errMsg.value = '';
  try {
    if (lockOn.value) {
      await saveFps('ac', 0);
      void setRtssLimit(0).catch(() => {});
      lockOn.value = false;
    } else {
      // 从不锁帧(0)重新开启时，用默认 90 起步，避免 saveFps(0) 导致仍无法锁帧
      const target = fps.value > 0 ? fps.value : 90;
      fps.value = target;
      fpsCeiling.value = smallestFpsCeiling(target);
      await saveFps('ac', target);
      void setRtssLimit(target).catch(() => {});
      lockOn.value = true;
    }
  } catch (e) {
    errMsg.value = '锁帧切换失败：' + (e as Error).message;
  }
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
  errMsg.value = '';
  // 切换布局会让 RTSS 重启 → 同样会让运行中的游戏闪退
  if (await guardGameRunning('changeOverlay', v)) return;
  await doSetOverlay(v);
}

async function doSetOverlay(v: 'W' | 'L' | 'J') {
  try {
    overlay.value = v;
    await setOverlayLayout(v);
    monitorHint.value = '监控样式已切换，RTSS 重启中（约 1~2 秒后生效）';
    setTimeout(() => (monitorHint.value = ''), 6000);
  } catch (e) {
    errMsg.value = '布局切换失败：' + (e as Error).message;
  }
}

async function toggleMonitor() {
  errMsg.value = '';
  // 开关监控也会重启 RTSS，运行中的游戏无法安全承受钩子卸载/重载。
  const target = monOn.value ? 'off' : 'W';
  if (await guardGameRunning('toggleMonitor', target)) return;
  const wasOff = !monOn.value;
  try {
    await setOverlayLayout(target);
    monOn.value = !monOn.value;
    if (wasOff && monOn.value) {
      monitorHint.value = '监控数据已开启，需重启已开启的游戏才能显示监控';
      setTimeout(() => (monitorHint.value = ''), 6000);
    } else {
      monitorHint.value = '';
    }
  } catch (e) {
    errMsg.value = '监控切换失败：' + (e as Error).message;
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
    lockOn.value = false;
    // 自动浮动优化已接管锁帧，复位时只关闭 RTSS 监控任务
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
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="monitorHint" class="info-bar">{{ monitorHint }}</div>
    <div v-if="zoomHint" class="info-bar">{{ zoomHint }}</div>
    <div v-if="gameRunningWarn" class="game-warn-bar">
      <div class="game-warn-text">
        <InlineIcon name="warning" />
        检测到游戏「<strong>{{ gameRunningWarn.game.name }}</strong>」正在运行（PID {{ gameRunningWarn.game.pid }}）。
        当前操作会导致游戏闪退（停止 RTSS / 切换布局 / 复位）。请先关闭游戏，正在监控等待…
      </div>
      <button class="action-btn ghost" @click="cancelGameWatch">取消</button>
    </div>

    <section class="card states">
      <StateCard
        title="RTSS"
        icon="rtss"
        :state="rtssOn ? 'on' : 'off'"
        :text="rtssOn ? '已启动' : '已关闭'"
        @click="toggleRtssOn"
        class="clickable"
      />
      <StateCard
        title="监控数据"
        icon="monitor"
        :state="monOn ? 'on' : 'off'"
        :text="monOn ? '已开启' : '已关闭'"
        @click="toggleMonitor"
        class="clickable"
      />
      <StateCard
        :icon="'target'" title="FPS 锁帧"
        :state="lockOn ? 'on' : 'off'"
        :text="lockOn ? '已开启' : '已关闭'"
        @click="toggleLock"
        class="clickable"
      />
    </section>

    <section class="card" :class="{ conflict: acConflict }">
      <h3 class="card-title"><InlineIcon name="target" /> RTSS 锁定帧率上限</h3>
      <div v-if="acConflict" class="conflict-bar">
        <InlineIcon name="warning" />
        被其他帧数目标类调节锁定
      </div>
      <template v-if="lockOn">
        <div class="lock-combo">
          <Slider
            :model-value="fps"
            :min="FPS_MIN"
            :max="fpsCeiling > 0 ? fpsCeiling : FPS_MIN"
            :step="5"
            label="FPS 帧率上限"
            unit="FPS"
            color="accent"
            :disabled="busy || acConflict || fpsCeiling === 0"
            aria-label="FPS 帧率上限"
            @update:model-value="(v: number) => (fps = v)"
            @commit="onFpsCommit"
          />
          <Dropdown
            :model-value="fpsCeiling"
            :options="fpsCeilingOpts"
            color="accent"
            width="120px"
            :disabled="busy || acConflict"
            aria-label="FPS 上限"
            @change="(v: number) => onFpsCeiling(v)"
          />
        </div>
      </template>
      <p v-else style="margin:10px 0 0;color:#8a8f98;font-size:13px;line-height:1.5;"><InlineIcon name="lock" /> 锁帧已关闭 —— 点击上方「FPS 锁帧」开启后可调节</p>
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="monitor" /> 监控模板</h3>
      <Slider
        v-model="zoomPct"
        :min="RTSS_ZOOM_MIN * 20"
        :max="RTSS_ZOOM_MAX * 20"
        :step="20"
        label="监控大小"
        unit="%"
        color="accent"
        :disabled="busy"
        @commit="onZoomCommit"
      />
      <SegButton
        v-model="overlay"
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

    <section class="card">
      <Toggle
        v-model="tasks.bootRtss"
        label="开机启动 RTSS 监控"
        description="登录后自动启动 RTSS"
        color="accent"
        :disabled="busy"
        @update:model-value="(v: boolean) => toggleTaskSafe(BOOT_RTSS_TASK, v, 'bootRtss')"
      />
      <div v-if="confirmingReset" class="confirm-bar">
        <span class="confirm-text">确认复位 RTSS 全部设置？将关闭 RTSS、清除锁帧与所有相关任务、关闭监控显示。</span>
        <div class="confirm-actions">
          <button class="action-btn" :disabled="busy" @click="doReset">确认复位</button>
          <button class="action-btn ghost" @click="confirmingReset = false">取消</button>
        </div>
      </div>
      <button class="danger-btn" :disabled="busy || confirmingReset" @click="resetAll">复位 RTSS 全部设置</button>
    </section>
  </div>
</template>

<style scoped>
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
