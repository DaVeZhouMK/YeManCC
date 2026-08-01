<script setup lang="ts">
import { ref, reactive, computed, onMounted, nextTick, inject, type Ref } from 'vue';
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
  FPS_MAX_DEFAULT,
  taskExists,
  toggleTask,
} from '@/bridge/yeman';
import { fs } from '@/bridge/api';

// 自动CPU浮动优化的「自动启用」模式（与 CpuView 共用同一文件）
const AUTO_ENABLE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_auto_enable.json';

const rtssOn = ref(false);
const monOn = ref(false);
const lockOn = ref(false);
const fps = ref(60); // 默认 60（掌机常用）
const fpsCeiling = ref(200); // 下拉上限（滑块最大值）
const tasks = reactive({ acRestore: false, bootRtss: false });
type TaskKey = keyof typeof tasks;
const dcMode = ref('off'); // 'off' | '30' | '45' | '60' | '90' | '120'
const overlay = ref<'W' | 'L'>('W');
// 自动CPU浮动优化「自动启用」模式：dc(使用电池) / always(总是) 时会接管电池调度逻辑，
// 与下方 DC 电池锁帧任务冲突 → 锁定该任务并提示前往 CPU调度 修改。
const autoEnableMode = ref<string>('never');
const dcConflict = computed(() => autoEnableMode.value === 'dc' || autoEnableMode.value === 'always');
// 自动CPU浮动优化（接通电源 / 总是）会接管插电调度逻辑，与「插电恢复锁帧」任务冲突 → 同样锁定
const acConflict = computed(() => autoEnableMode.value === 'ac' || autoEnableMode.value === 'always');
const busy = ref(false);
const errMsg = ref('');
const confirmingReset = ref(false);
const monitorHint = ref('');
const zoomPct = ref(100); // OSD 缩放百分比（= ZoomRatio × 20），默认 100%
const zoomHint = ref('');

// DC 电池模式锁帧选项
const dcOpts = [
  { value: 'off', label: '不启用' },
  { value: '30', label: '30 FPS' },
  { value: '45', label: '45 FPS' },
  { value: '60', label: '60 FPS' },
  { value: '90', label: '90 FPS' },
  { value: '120', label: '120 FPS' },
];

// FPS 上限档位（对齐 TDP 页面 ceilingOpts 下拉逻辑）
const fpsCeilingOpts = FPS_CEILINGS.map((v) => ({ value: v, label: v + ' FPS' }));

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
  const [rtssRes, monRes, limRes, layRes, acTaskRes, bootTaskRes, dcFpsRes, zoomRes, aeRes] = await Promise.allSettled([
    rtssRunning(),
    monitorOn(),
    readRtssLimit(),
    readOverlayLayout(),
    safeExists('锁帧-插电AC模式锁帧'),
    safeExists('监控-开机启动监控锁帧软件RTSS'),
    readFps('dc'),
    readRtssZoom(),
    fs.readTextFile(AUTO_ENABLE_FILE, 64),
  ]);
  // 逐项赋值（不阻塞 UI）
  if (rtssRes.status === 'fulfilled') rtssOn.value = rtssRes.value;
  if (monRes.status === 'fulfilled') monOn.value = monRes.value;
  if (limRes.status === 'fulfilled') {
    const lim = limRes.value;
    fps.value = lim > 0 ? lim : 90;
    fpsCeiling.value = lim > 0 ? Math.max(lim, FPS_CEILINGS[0]) : FPS_MAX_DEFAULT;
    lockOn.value = lim > 0;
  }
  if (layRes.status === 'fulfilled') {
    overlay.value = layRes.value === 'YeManOBS-L-1.ovl' ? 'L' : 'W';
  }
  if (acTaskRes.status === 'fulfilled') tasks.acRestore = acTaskRes.value;
  if (bootTaskRes.status === 'fulfilled') tasks.bootRtss = bootTaskRes.value;
  if (aeRes.status === 'fulfilled') {
    try { autoEnableMode.value = (JSON.parse(aeRes.value as string).mode as string) ?? 'never'; } catch { autoEnableMode.value = 'never'; }
  } else {
    autoEnableMode.value = 'never';
  }
  if (dcFpsRes.status === 'fulfilled') {
    const d = dcFpsRes.value;
    dcMode.value = d != null && [30, 45, 60, 90, 120].includes(d) ? String(d) : 'off';
  }
  if (zoomRes.status === 'fulfilled') zoomPct.value = zoomRes.value * 20;

  // ── 冲突处理：自动CPU浮动优化（使用电池 / 总是）已接管电池调度逻辑，
  // DC 电池锁帧任务与其冲突 → 锁定该任务（关闭实际计划任务 + 禁用 UI）并提示前往 CPU调度。
  if (dcConflict.value && dcMode.value !== 'off') {
    // ⚠️ 只关实际计划任务，绝不 saveFps('dc',0) —— 否则会抹掉用户设过的 DC 锁帧值（如 60）。
    // 冲突解除后 refresh 会读回 FPS-dc.txt 恢复原值。
    try {
      await toggleTask('锁帧-离电DC模式锁帧', false);
      dcMode.value = 'off';
    } catch { /* 忽略：下次刷新再试 */ }
  }
  if (acConflict.value && tasks.acRestore) {
    // 同理 DC：只关实际计划任务，绝不 saveFps('ac',0) —— 保留用户手动 AC 锁帧值（FPS-ac.txt）
    try {
      await toggleTask('锁帧-插电AC模式锁帧', false);
      tasks.acRestore = false;
    } catch { /* 忽略：下次刷新再试 */ }
  }
}

async function onFpsCommit(v: number) {
  errMsg.value = '';
  try {
    await saveFps('ac', v);
    await setRtssLimit(v);
    lockOn.value = v > 0;
  } catch (e) {
    errMsg.value = '锁帧下发失败：' + (e as Error).message;
  }
}

// FPS 上限档位选择（对齐 TDP onCeiling：直接设值+提交）
function onFpsCeiling(val: number) {
  fpsCeiling.value = val;
  fps.value = Math.min(fps.value, val); // 不超过上限
  void onFpsCommit(fps.value); // 提交当前帧率（已钳到上限），不要误把上限档位当锁帧值下发
}

async function toggleLock() {
  errMsg.value = '';
  try {
    if (lockOn.value) {
      await saveFps('ac', 0);
      await setRtssLimit(0);
      lockOn.value = false;
    } else {
      await saveFps('ac', fps.value);
      await setRtssLimit(fps.value);
      lockOn.value = true;
    }
  } catch (e) {
    errMsg.value = '锁帧切换失败：' + (e as Error).message;
  }
}

async function toggleRtssOn() {
  errMsg.value = '';
  busy.value = true;
  try {
    await toggleRtss(!rtssOn.value);
    rtssOn.value = !rtssOn.value;
  } catch (e) {
    errMsg.value = 'RTSS 启停失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function toggleMonitor() {
  errMsg.value = '';
  const wasOff = !monOn.value;
  try {
    await setOverlayLayout(monOn.value ? 'off' : 'W');
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

async function onOverlay(v: 'W' | 'L') {
  errMsg.value = '';
  try {
    overlay.value = v;
    await setOverlayLayout(v);
    monitorHint.value = '监控样式已切换，RTSS 重启中（约 1~2 秒后生效）';
    setTimeout(() => (monitorHint.value = ''), 6000);
  } catch (e) {
    errMsg.value = '布局切换失败：' + (e as Error).message;
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

async function onDcMode(v: string) {
  // 冲突态（自动CPU浮动优化 使用电池/总是）下 DC 锁帧任务已锁定，不可改
  if (dcConflict.value) return;
  errMsg.value = '';
  busy.value = true;
  try {
    dcMode.value = v;
    if (v === 'off') {
      await saveFps('dc', 0);
      await toggleTask('锁帧-离电DC模式锁帧', false);
    } else {
      const n = Number(v);
      await saveFps('dc', n);
      await toggleTask('锁帧-离电DC模式锁帧', true);
    }
  } catch (e) {
    errMsg.value = 'DC 锁帧任务失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function toggleTaskSafe(name: string, on: boolean, key: TaskKey) {
  errMsg.value = '';
  busy.value = true;
  try {
    tasks[key] = await toggleTask(name, on);
  } catch (e) {
    tasks[key] = !on; // 回滚
    errMsg.value = '任务计划操作失败：' + (e as Error).message + '（需管理员权限）';
  } finally {
    busy.value = false;
  }
}

async function resetAll() {
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
    lockOn.value = false;
    await toggleTask('锁帧-插电AC模式锁帧', false);
    await toggleTask('锁帧-离电DC模式锁帧', false);
    await toggleTask('监控-开机启动监控锁帧软件RTSS', false);
    tasks.acRestore = false;
    tasks.bootRtss = false;
    dcMode.value = 'off';
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
  import('vue').then(({ watch }) => watch(globalRefreshKey, () => refresh()));
}

onMounted(() => nextTick(refresh));
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="monitorHint" class="info-bar">{{ monitorHint }}</div>
    <div v-if="zoomHint" class="info-bar">{{ zoomHint }}</div>

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
        被【自动CPU浮动优化】锁定，请至【CPU调度】页面修改。
      </div>
      <template v-if="lockOn">
        <Slider v-model="fps" :min="FPS_MIN" :max="fpsCeiling" :step="5" :label="'FPS帧率上限'" :unit="'FPS'" color="accent" :disabled="busy || acConflict" @commit="onFpsCommit" />
        <div class="row">
          <span class="row-label">FPS 上限</span>
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
        <Toggle
          v-model="tasks.acRestore"
          label="插电恢复锁帧"
          description="电源事件 AC 时自动回到此锁帧"
          color="ac"
          :disabled="acConflict || busy"
          @update:model-value="(v: boolean) => toggleTaskSafe('锁帧-插电AC模式锁帧', v, 'acRestore')"
        />
      </template>
      <p v-else style="margin:10px 0 0;color:#8a8f98;font-size:13px;line-height:1.5;"><InlineIcon name="lock" /> 锁帧已关闭 —— 点击上方「FPS 锁帧」开启后可调节</p>
    </section>

    <section class="card" :class="{ conflict: dcConflict }">
      <h3 class="card-title"><InlineIcon name="battery" /> DC 电池模式锁帧任务</h3>
      <div v-if="dcConflict" class="conflict-bar">
        <InlineIcon name="warning" />
        被【自动CPU浮动优化】锁定，请至【CPU调度】页面修改。
      </div>
      <SegButton v-model="dcMode" :options="dcOpts" color="dc" full :disabled="dcConflict" @update:model-value="onDcMode" />
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
        ]"
        color="accent"
        full
        @update:model-value="(v: string) => onOverlay(v as 'W' | 'L')"
      />
    </section>

    <section class="card">
      <Toggle
        v-model="tasks.bootRtss"
        label="开机启动 RTSS 监控"
        description="登录后自动启动 RTSS + HWiNFO"
        color="accent"
        :disabled="busy"
        @update:model-value="(v: boolean) => toggleTaskSafe('监控-开机启动监控锁帧软件RTSS', v, 'bootRtss')"
      />
      <div v-if="confirmingReset" class="confirm-bar">
        <span class="confirm-text">确认复位 RTSS 全部设置？将关闭 RTSS/HWiNFO、清除锁帧与所有相关任务、关闭监控显示。</span>
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
.states .sc-body {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-end;
  min-height: 40px;
  width: 100%;
}
.states .sc-title {
  line-height: 18px;
  min-height: 18px;
}
.states .sc-text {
  line-height: 20px;
  min-height: 20px;
  margin-top: 4px;
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
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
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
.danger-btn {
  margin-top: 10px;
  width: 100%;
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.5);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 9px;
  cursor: pointer;
  font-size: 12px;
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
  padding: 10px;
  margin-bottom: 8px;
  font-size: 11px;
  line-height: 1.5;
}
.confirm-text {
  display: block;
  margin-bottom: 8px;
}
.confirm-actions {
  display: flex;
  gap: 8px;
}
.confirm-actions .action-btn {
  width: auto;
  flex: 1;
  margin: 0;
  background: #e5484d;
  color: #fff;
}
.confirm-actions .action-btn.ghost {
  background: var(--bg-input);
  color: var(--text);
}
</style>
