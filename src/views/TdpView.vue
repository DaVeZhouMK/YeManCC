<script setup lang="ts">
import { ref, reactive, computed, onMounted, nextTick, inject } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import {
  TDP_CEILINGS,
  TDP_MIN,
  smallestCeiling,
  detectPowerMode,
  detectCpuName,
  detectVendor,
  readTdp,
  setTdp,
  taskExists,
  toggleTask,
  type Vendor,
} from '@/bridge/yeman';
import { useWakeTaskStore } from '@/stores/wakeTask';

const powerMode = ref<'ac' | 'dc'>('ac');
const cpuName = ref('检测中…');
const vendor = ref<Vendor>('unknown');

const acTdp = ref(300);
const dcTdp = ref(35);
const acCeiling = ref(300);
const dcCeiling = ref(300);

const tasks = reactive({ acRestore: false, dcRestore: false, bootTdp: false });
type TaskKey = keyof typeof tasks;

// 唤醒后恢复 TDP + 电源预设：与睡眠优化页共用同一任务，单一真相
const wakeStore = useWakeTaskStore();

const busy = ref(false);
const errMsg = ref('');

const ceilingOpts = TDP_CEILINGS.map((v) => ({ value: v, label: v + ' W' }));
const modeLabel = computed(() => (powerMode.value === 'ac' ? '🔌 当前电源为 AC 模式' : '🔋 当前电源为 DC 模式'));

async function refreshModeAndCpu() {
  // 并行检测（不串行等待）
  const [modeRes, cpuRes, vendorRes] = await Promise.allSettled([
    detectPowerMode(),
    detectCpuName(),
    detectVendor(),
  ]);
  if (modeRes.status === 'fulfilled') powerMode.value = modeRes.value;
  if (cpuRes.status === 'fulfilled') cpuName.value = cpuRes.value;
  if (vendorRes.status === 'fulfilled') vendor.value = vendorRes.value;
}

async function loadValues() {
  const [aRes, dRes] = await Promise.allSettled([readTdp('ac'), readTdp('dc')]);
  if (aRes.status === 'fulfilled' && aRes.value != null) {
    acTdp.value = aRes.value;
    acCeiling.value = smallestCeiling(aRes.value);
  }
  if (dRes.status === 'fulfilled' && dRes.value != null) {
    dcTdp.value = dRes.value;
    dcCeiling.value = smallestCeiling(dRes.value);
  }
}

async function loadTasks() {
  const [ac, dc, boot] = await Promise.allSettled([
    safeExists('TDP-插电AC模式TDP调节'),
    safeExists('TDP-离电DC模式TDP调节'),
    safeExists('TDP-开机启动野蛮快设TDP挡位'),
  ]);
  if (ac.status === 'fulfilled') tasks.acRestore = ac.value;
  if (dc.status === 'fulfilled') tasks.dcRestore = dc.value;
  if (boot.status === 'fulfilled') tasks.bootTdp = boot.value;
  // 唤醒后恢复任务与睡眠优化页共享，统一由 wakeStore 持有真相
  await wakeStore.init();
}
async function safeExists(name: string): Promise<boolean> {
  try {
    return await taskExists(name);
  } catch {
    return false;
  }
}

async function onAcCommit(v: number) {
  await applyTdp('ac', v);
}
async function onDcCommit(v: number) {
  await applyTdp('dc', v);
}
async function applyTdp(mode: 'ac' | 'dc', v: number) {
  errMsg.value = '';
  try {
    const apply = mode === powerMode.value;
    await setTdp(mode, v, { apply, vendor: vendor.value });
  } catch (e) {
    errMsg.value = 'TDP 下发失败：' + (e as Error).message + '（可能需要以管理员身份运行）';
  }
}

// 选上限档：把当前值直接设为该上限（省去再拖动一次，对齐 HTA ddSelect）
function onCeiling(mode: 'ac' | 'dc', val: number) {
  if (mode === 'ac') {
    acCeiling.value = val;
    acTdp.value = val;
    onAcCommit(val);
  } else {
    dcCeiling.value = val;
    dcTdp.value = val;
    onDcCommit(val);
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
    // 不再调用 dialog.message()——WebView2 原生对话框会阻塞渲染线程导致 UI 卡死
  } finally {
    busy.value = false;
  }
}

// 唤醒后恢复 TDP + 电源预设：与睡眠优化页共用，经 wakeStore 切换并即时同步两页
async function onWakeTdp(v: boolean) {
  errMsg.value = '';
  try {
    await wakeStore.set(v);
  } catch (e) {
    errMsg.value = '任务计划操作失败：' + (e as Error).message + '（需管理员权限）';
  }
}

// ── 统一刷新入口（供全局触发器调用）──
async function refresh() {
  await Promise.allSettled([
    refreshModeAndCpu(),
    loadValues(),
    loadTasks(),
  ]);
}

// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  import('vue').then(({ watch }) => watch(globalRefreshKey, () => refresh()));
}

onMounted(async () => {
  await nextTick();
  // 并行异步加载（不串行等待，避免切页卡顿）
  await Promise.allSettled([
    refreshModeAndCpu(),
    loadValues(),
    loadTasks(),
  ]);
});
</script>

<template>
  <div class="page">
    <div class="statusbar">
      <div>
        <div class="sb-cpu">{{ cpuName }}</div>
        <div class="sb-sub muted">{{ modeLabel }}</div>
      </div>
    </div>

    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>

    <section class="card">
      <h3 class="card-title">🔌 AC 插电 TDP 上限</h3>
      <Slider
        v-model="acTdp"
        :min="TDP_MIN"
        :max="acCeiling"
        :step="1"
        :label="'TDP 上限'"
        :unit="'W'"
        color="ac"
        @commit="onAcCommit"
      />
      <Toggle
        v-model="tasks.acRestore"
        label="插电恢复 TDP"
        description="电源事件 AC 时自动回到此挡位"
        color="ac"
        :disabled="busy"
        @update:model-value="(v: boolean) => toggleTaskSafe('TDP-插电AC模式TDP调节', v, 'acRestore')"
      />
      <div class="row">
        <span class="row-label">AC滑块最大值</span>
        <Dropdown
          :model-value="acCeiling"
          :options="ceilingOpts"
          color="ac"
          aria-label="AC滑块最大值"
          width="120px"
          @change="(v: number) => onCeiling('ac', v)"
        />
      </div>
    </section>

    <section class="card">
      <h3 class="card-title">🔋 DC 离电 TDP 上限</h3>
      <Slider
        v-model="dcTdp"
        :min="TDP_MIN"
        :max="dcCeiling"
        :step="1"
        :label="'TDP 上限'"
        :unit="'W'"
        color="dc"
        @commit="onDcCommit"
      />
      <Toggle
        v-model="tasks.dcRestore"
        label="离电恢复 TDP"
        description="电源事件 DC 时自动回到此挡位"
        color="dc"
        :disabled="busy"
        @update:model-value="(v: boolean) => toggleTaskSafe('TDP-离电DC模式TDP调节', v, 'dcRestore')"
      />
      <div class="row">
        <span class="row-label">DC滑块最大值</span>
        <Dropdown
          :model-value="dcCeiling"
          :options="ceilingOpts"
          color="dc"
          aria-label="DC滑块最大值"
          width="120px"
          @change="(v: number) => onCeiling('dc', v)"
        />
      </div>
    </section>

    <section class="card">
      <h3 class="card-title">⚡ 任务计划启动 TDP 最大值</h3>
      <Toggle
        v-model="tasks.bootTdp"
        label="开机启动 TDP + 电源预设"
        description="登录后按 AC/DC 恢复 TDP 挡位"
        color="accent"
        :disabled="busy"
        @update:model-value="(v: boolean) => toggleTaskSafe('TDP-开机启动野蛮快设TDP挡位', v, 'bootTdp')"
      />
      <Toggle
        :model-value="wakeStore.on"
        label="唤醒后恢复 TDP + 电源预设"
        description="从睡眠/休眠唤醒后复位电源与 TDP（与睡眠优化页联动）"
        color="accent"
        :disabled="busy || wakeStore.busy"
        @update:model-value="onWakeTdp"
      />
    </section>
  </div>
</template>

<style scoped>
.page {
  padding-bottom: 20px;
}
.statusbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  background: var(--bg-panel);
  border-radius: var(--radius);
  padding: 12px 14px;
  margin-bottom: 10px;
}
.sb-cpu {
  font-size: 13px;
  font-weight: 600;
}
.sb-sub {
  font-size: 11px;
  margin-top: 2px;
}
.pm-chip {
  font-size: 13px;
  font-weight: 700;
  padding: 5px 10px;
  border-radius: 8px;
  background: var(--bg-input);
  color: var(--accent);
}
.pm-chip.dc {
  color: var(--accent-2);
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
.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-top: 8px;
}
</style>
