<script setup lang="ts">
import { ref, reactive, computed, inject, watch, onBeforeUnmount, type Ref } from 'vue';
import { fs, shell } from '@/bridge/api';
import Toggle from '@/components/Toggle.vue';
import Slider from '@/components/Slider.vue';
import SegButton from '@/components/SegButton.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import {
  sleepGuardGet,
  sleepGuardSet,
  sleepGuardSetConfig,
  getPowerBtnIdx,
  setPowerBtnIdx,
  PW,
  getActiveScheme,
  reactivateCurrentScheme,
  readHibernateState,
  setHibernate,
  readHibernateSize,
  setHibernateSize,
  readTotalMemoryGB,
  getSleepPowerPlanOptimizationEnabled,
  setSleepPowerPlanOptimizationEnabled,
  type SleepGuardStatus,
  type PowerBtnIdx,
} from '@/bridge/yeman';

const cfg = reactive<SleepGuardStatus>({
  enabled: false,
  mode: 'off',
  suspended: 0,
  pauseResume: true,
  killListEnabled: false,
  resleepEnabled: false,
  overheatSleepEnabled: false,
  overheatTempC: 95,
});
const SLEEP_KILL_LIST = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\睡眠击杀名单.txt';

// 电源按钮：0=S3睡眠, 1=S4休眠, 2=不操作
const acBtnIdx = ref<PowerBtnIdx>(2);
const dcBtnIdx = ref<PowerBtnIdx>(2);
const sleepPowerPlanOptimizationEnabled = ref(true);

// 系统休眠
const hibernateOn = ref<boolean | null>(null); // null=未检测（不强制设默认值）
const hibernateSupported = ref<boolean | null>(null); // null=能力未知；未知时仍允许安全开启重试
const hibLoading = ref(true); // 独立加载；失败时结束 loading 并保留“未知但可安全开启”状态
const hibSize = ref(50);
const memGB = ref<number | null>(null); // 物理内存总量（GB），用于预估休眠文件大小
const confirmOff = ref(false); // 关闭休眠二次确认（避免整行误触关闭休眠这种销毁性操作）

const busy = ref(false);
const msg = ref('');
const activeSchemeGuid = ref('');
const isYemanScheme = computed(() => activeSchemeGuid.value === PW.YEMAN.toLowerCase());

let msgTimer: number | null = null;
watch(msg, (value) => {
  if (msgTimer !== null) window.clearTimeout(msgTimer);
  msgTimer = null;
  if (!value) return;
  msgTimer = window.setTimeout(() => {
    if (msg.value === value) msg.value = '';
    msgTimer = null;
  }, 10000);
});

onBeforeUnmount(() => {
  if (msgTimer !== null) window.clearTimeout(msgTimer);
  if (pwrActivateTimer !== null) window.clearTimeout(pwrActivateTimer);
});

async function refreshHibernateState() {
  hibLoading.value = true;
  try {
    const state = await readHibernateState();
    hibernateSupported.value = state.supportedKnown ? state.supported : null;
    hibernateOn.value = state.enabledKnown ? state.enabled : null;
  } catch {
    hibernateSupported.value = null;
    hibernateOn.value = null;
  } finally {
    hibLoading.value = false;
  }
  return hibernateOn.value;
}

async function refresh() {
  // 与整页其他检测解耦：AC/DC、电源方案或内存 IPC 异常不能拖死系统休眠开关。
  const hibernateRefresh = refreshHibernateState();
  try {
    const s = await sleepGuardGet();
    cfg.enabled = s.enabled;
    cfg.mode = s.mode;
    cfg.suspended = s.suspended;
    cfg.pauseResume = s.pauseResume;
    cfg.killListEnabled = !!s.killListEnabled;
    cfg.resleepEnabled = !!s.resleepEnabled;
    cfg.overheatSleepEnabled = !!s.overheatSleepEnabled;
    cfg.overheatTempC = Math.max(85, Math.min(100, Number(s.overheatTempC) || 95));
  } catch {
    /* 检测不到保持现状，绝不假定关闭 */
  }
  try {
    sleepPowerPlanOptimizationEnabled.value = await getSleepPowerPlanOptimizationEnabled();
  } catch {
    /* 配置读取失败保持现状，绝不假定关闭 */
  }

  // 并行加载电源按钮、系统休眠状态
  const [schemeRes, acBtnRes, dcBtnRes, hibSizeRes, memRes] = await Promise.allSettled([
    getActiveScheme(),
    getPowerBtnIdx(true),
    getPowerBtnIdx(false),
    readHibernateSize(),
    readTotalMemoryGB(),
  ]);
  if (schemeRes.status === 'fulfilled') activeSchemeGuid.value = schemeRes.value.toLowerCase();
  if (acBtnRes.status === 'fulfilled') acBtnIdx.value = acBtnRes.value;
  if (dcBtnRes.status === 'fulfilled') dcBtnIdx.value = dcBtnRes.value;
  if (hibSizeRes.status === 'fulfilled') hibSize.value = hibSizeRes.value;
  if (memRes.status === 'fulfilled') memGB.value = memRes.value;
  await hibernateRefresh;
}

// 睡眠时暂停游戏（与唤醒自动恢复绑定）。
async function onPauseResume(v: boolean) {
  cfg.pauseResume = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseResume: v, killListEnabled: cfg.killListEnabled, resleepEnabled: cfg.resleepEnabled, overheatSleepEnabled: cfg.overheatSleepEnabled, overheatTempC: cfg.overheatTempC });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onKillList(v: boolean) {
  cfg.killListEnabled = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseResume: cfg.pauseResume, killListEnabled: v, resleepEnabled: cfg.resleepEnabled, overheatSleepEnabled: cfg.overheatSleepEnabled, overheatTempC: cfg.overheatTempC });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onResleep(v: boolean) {
  cfg.resleepEnabled = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({
      mode: 'custom',
      pauseResume: cfg.pauseResume,
      killListEnabled: cfg.killListEnabled,
      resleepEnabled: v,
      overheatSleepEnabled: cfg.overheatSleepEnabled,
      overheatTempC: cfg.overheatTempC,
    });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onSleepPowerPlanOptimization(v: boolean) {
  const previous = sleepPowerPlanOptimizationEnabled.value;
  sleepPowerPlanOptimizationEnabled.value = v;
  try {
    await setSleepPowerPlanOptimizationEnabled(v);
    msg.value = '';
  } catch {
    sleepPowerPlanOptimizationEnabled.value = previous;
    msg.value = '设置保存失败';
  }
}

async function onOverheatSleep(v: boolean) {
  cfg.overheatSleepEnabled = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({
      mode: 'custom',
      pauseResume: cfg.pauseResume,
      killListEnabled: cfg.killListEnabled,
      resleepEnabled: cfg.resleepEnabled,
      overheatSleepEnabled: v,
      overheatTempC: cfg.overheatTempC,
    });
    await syncGuardEnabled();
  } catch {
    msg.value = '璁剧疆淇濆瓨澶辫触';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onOverheatTemp(v: number) {
  cfg.overheatTempC = Math.max(85, Math.min(100, Math.round(v)));
  try {
    await sleepGuardSetConfig({ overheatTempC: cfg.overheatTempC });
  } catch {
    msg.value = '璁剧疆淇濆瓨澶辫触';
    await refresh();
  }
}

async function openKillList() {
  try {
    if (!(await fs.exists(SLEEP_KILL_LIST))) {
      await fs.writeTextFile(SLEEP_KILL_LIST, '# 每行一个 exe 名称，可带或不带 .exe\n# 例如：SomeTool.exe\n');
    }
    await shell.open(SLEEP_KILL_LIST);
  } catch {
    msg.value = '无法打开睡眠击杀名单';
  }
}

// 总闸（Enable.txt）：任一子功能生效即开启，全部关闭即关闭
async function syncGuardEnabled() {
  const active = cfg.pauseResume || cfg.killListEnabled || cfg.resleepEnabled || cfg.overheatSleepEnabled;
  if (active === cfg.enabled) return;
  const previous = cfg.enabled;
  try {
    await sleepGuardSet(active);
    cfg.enabled = active;
  } catch {
    cfg.enabled = previous;
    msg.value = '守护总闸设置失败';
    throw new Error('守护总闸设置失败');
  }
}

// ── 电源按钮按下操作 ──
async function onPwrBtn(ac: boolean, idx: PowerBtnIdx) {
  msg.value = '';
  busy.value = true;
  try {
    await setPowerBtnIdx(ac, idx);
    if (ac) acBtnIdx.value = idx; else dcBtnIdx.value = idx;
    // 联动：电源按钮选「S4 休眠到硬盘」时，若系统休眠当前为关闭，则自动开启休眠——否则 S4 不会生效。
    // 仅"开启"，绝不在选择其它按钮时关闭休眠；检测未知时不乱写。
    if (idx === 1) {
      if (hibernateOn.value === false) {
        await setHibernate(true);
        await setHibernateSize(hibSize.value);
        const enabled = await refreshHibernateState();
        if (enabled !== true) throw new Error('Windows 回读仍显示休眠未开启');
      } else if (hibernateOn.value === null) {
        msg.value = '休眠状态未知，未随电源按钮设置自动修改；系统休眠开关仍可单独点击开启。';
      }
    }
    schedulePwrActivate();
  } catch (e) {
    msg.value = '电源按钮设置失败：' + (e as Error).message + '（需要管理员权限修改电源方案）';
  } finally {
    busy.value = false;
  }
}

let pwrActivateTimer: number | null = null;
function schedulePwrActivate() {
  if (pwrActivateTimer !== null) window.clearTimeout(pwrActivateTimer);
  pwrActivateTimer = window.setTimeout(() => {
    pwrActivateTimer = null;
    reactivateCurrentScheme().catch(() => {});
  }, 2000);
}

// ── 系统休眠 ──
async function onHibernate(v: boolean) {
  if (hibernateSupported.value === false) {
    msg.value = '此设备的 Windows 电源能力明确不支持 S4 休眠。';
    return;
  }
  // 状态未知时开关显示为关闭，只允许走“开启并回读”，绝不会误触发关闭。
  if (hibernateOn.value === null && !v) return;
  if (v) {
    msg.value = '';
    busy.value = true;
    try {
      await setHibernate(true);
      await setHibernateSize(hibSize.value);
      const enabled = await refreshHibernateState();
      if (enabled !== true) throw new Error('Windows 回读仍显示休眠未开启');
    } catch (e) {
      await refreshHibernateState();
      msg.value = '休眠开启失败：' + (e as Error).message + '（可能需要管理员权限）';
    } finally {
      busy.value = false;
    }
    return;
  }
  confirmOff.value = true;
}
async function confirmHibernateOff() {
  msg.value = '';
  busy.value = true;
  try {
    await setHibernate(false);
    const enabled = await refreshHibernateState();
    if (enabled !== false) throw new Error('Windows 回读仍显示休眠未关闭');
  } catch (e) {
    await refreshHibernateState();
    msg.value = '休眠关闭失败：' + (e as Error).message + '（可能需要管理员权限）';
  } finally {
    confirmOff.value = false;
    busy.value = false;
  }
}
function cancelHibernateOff() {
  confirmOff.value = false;
}
async function onHibSize(v: number) {
  msg.value = '';
  try {
    await setHibernateSize(v);
  } catch (e) {
    msg.value = '休眠大小设置失败：' + (e as Error).message;
  }
}

const hibDesc = computed(() => {
  if (hibLoading.value) return '检测中…';
  if (hibernateSupported.value === false) return '此设备不支持 Windows S4 休眠';
  if (hibernateOn.value === null) return 'Windows 状态读取失败；可点击开关安全开启并重新检测';
  if (memGB.value !== null) {
    const est = (memGB.value * hibSize.value) / 100;
    return `休眠文件大小预估为 ${est.toFixed(1)} G`;
  }
  return hibernateOn.value ? '休眠已开启' : '休眠已关闭';
});

const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => refresh());
}
refresh();
</script>

<template>
  <div class="sleep-guard">
    <Transition name="notice-fade">
      <div v-if="msg" class="msg" :class="{ 'msg-ok': /已启用|已关闭|已恢复|已暂停|已尝试恢复|完成/.test(msg) }" role="status">
        {{ msg }}
      </div>
    </Transition>

    <!-- 顶部：电源按钮与系统休眠 合并为一个气泡 -->
    <div class="card">
      <h3 class="card-title"><InlineIcon name="plug" /> 电源按钮与系统休眠</h3>
      <div class="pwr-row">
        <span class="pwr-label"><InlineIcon name="bolt" /> AC 插电</span>
        <SegButton
          :model-value="acBtnIdx"
          :options="[
            { value: 0, label: 'S3 睡眠' },
            { value: 1, label: 'S4 休眠' },
            { value: 2, label: '不操作' },
            { value: 3, label: '关闭显示器' },
          ]"
          color="ac"
          :disabled="busy || !isYemanScheme"
          @update:model-value="(v: number) => onPwrBtn(true, v as PowerBtnIdx)"
        />
      </div>
      <div class="pwr-row">
        <span class="pwr-label"><InlineIcon name="battery" /> DC 离电</span>
        <SegButton
          :model-value="dcBtnIdx"
          :options="[
            { value: 0, label: 'S3 睡眠' },
            { value: 1, label: 'S4 休眠' },
            { value: 2, label: '不操作' },
            { value: 3, label: '关闭显示器' },
          ]"
          color="dc"
          :disabled="busy || !isYemanScheme"
          @update:model-value="(v: number) => onPwrBtn(false, v as PowerBtnIdx)"
        />
      </div>
      <p v-if="!isYemanScheme" class="pwr-warn">当前电源方案不是野蛮系统电源，AC / DC 电源按钮设置不可用</p>
      <p v-if="acBtnIdx === 0 || dcBtnIdx === 0" class="pwr-warn">S3 状态插入 USB4 供电可能导致唤醒！</p>
      <Toggle
        :model-value="hibernateOn === true"
        label="系统休眠开关"
        :description="hibDesc"
        color="accent"
        :disabled="busy || hibLoading || hibernateSupported === false"
        @update:model-value="onHibernate"
      />
      <div v-if="confirmOff" class="confirm-bar">
        <span class="confirm-text">确认关闭系统休眠？将删除 hiberfil.sys 休眠文件（S4 电源按钮会失效）。</span>
        <div class="confirm-actions">
          <button class="mini-btn danger" @click="confirmHibernateOff">确认关闭</button>
          <button class="mini-btn" @click="cancelHibernateOff">取消</button>
        </div>
      </div>
      <Slider
        v-model="hibSize"
        :min="30"
        :max="100"
        :step="5"
        label="休眠文件大小百分比"
        unit="%"
        color="accent"
        :disabled="!hibernateOn"
        @commit="onHibSize"
      />
    </div>

    <!-- 睡眠操作（总开关已移除，子项直接驱动 guard，始终显示） -->
    <div class="card">
      <h3 class="card-title"><InlineIcon name="sleep" /> 睡眠操作</h3>
      <Toggle
        :model-value="cfg.pauseResume"
        label="睡眠时暂停游戏"
        description="入睡冻结当前最大内存进程，唤醒自动恢复"
        :disabled="busy"
        @update:model-value="onPauseResume"
      />
      <Toggle
        :model-value="sleepPowerPlanOptimizationEnabled"
        label="睡眠电源计划优化"
        description="开启后在程序启动30秒及电源变更时后台优化睡眠相关电源计划"
        :disabled="busy"
        @update:model-value="onSleepPowerPlanOptimization"
      />
      <Toggle
        :model-value="cfg.resleepEnabled"
        label="入睡失败重睡"
        description="触发睡眠后 30 秒内，连续 10 秒无手柄/键盘输入时重新睡眠；重睡后 5 分钟内不重复触发"
        :disabled="busy"
        @update:model-value="onResleep"
      />
      <Toggle
        :model-value="cfg.overheatSleepEnabled"
        label="过热自动睡眠"
        description="电池设备温度超过阈值后，10秒无手柄、键盘、鼠标输入自动睡眠"
        :disabled="busy"
        @update:model-value="onOverheatSleep"
      />
      <Slider
        v-model="cfg.overheatTempC"
        :min="85"
        :max="100"
        :step="1"
        label="过热温度"
        unit="°C"
        color="accent"
        :disabled="busy || !cfg.overheatSleepEnabled"
        @commit="onOverheatTemp"
      />
      <div class="kill-list-row">
        <div class="kill-list-text">
          <div class="kill-list-label">睡眠时清除指定程序</div>
          <div class="kill-list-hint">入睡前结束名单中的进程防止影响睡眠</div>
        </div>
        <div class="kill-list-actions">
          <button class="kill-list-edit" :disabled="busy" @click="openKillList">编辑名单</button>
          <Toggle
            :model-value="cfg.killListEnabled"
            :disabled="busy"
            @update:model-value="onKillList"
          />
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.sleep-guard { padding: 2px; }
.muted { color: var(--text-dim); }

.card {
  background: color-mix(in srgb, var(--bg-panel) 72%, transparent);
  border-radius: var(--radius);
  padding: 12px 14px;
  margin-bottom: 10px;
}
.card-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
  margin: 0 0 10px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.kill-list-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 8px 0; }
.kill-list-text { min-width: 0; flex: 1; text-align: left; }
.kill-list-label { font-size: 13px; }
.kill-list-hint { font-size: 11px; color: var(--text-dim); line-height: 1.3; }
.kill-list-actions { display: flex; align-items: center; gap: 8px; flex: 0 0 auto; }
.kill-list-edit {
  min-height: var(--btn-min-h);
  padding: var(--btn-py) var(--btn-px);
  border: 1px solid #2a3342;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  font-size: var(--btn-font-size);
  cursor: pointer;
}
.kill-list-edit:disabled { opacity: 0.4; cursor: not-allowed; }
.kill-list-edit:focus-visible { box-shadow: var(--focus-ring); }

.msg {
  font-size: 12px;
  color: var(--danger);
  margin-bottom: 10px;
  padding: 8px 10px;
  border-radius: var(--radius-ctrl);
  background: rgba(255, 90, 90, 0.08);
  border: 1px solid rgba(255, 90, 90, 0.25);
}
.msg-ok { color: var(--ok); background: rgba(80, 200, 120, 0.08); border-color: rgba(80, 200, 120, 0.25); }
.notice-fade-enter-active,
.notice-fade-leave-active { transition: opacity 0.15s ease; }
.notice-fade-enter-from,
.notice-fade-leave-to { opacity: 0; }

/* 电源按钮 / 系统休眠 */
.pwr-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}
.pwr-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
}
.pwr-warn {
  margin-top: 4px;
  font-size: 11px;
  color: var(--accent-2, #f5b93d);
  line-height: 1.3;
}
.sleep-plan-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 8px 0 0; border-top: 1px solid color-mix(in srgb, var(--text-dim) 18%, transparent); }
.confirm-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  margin-bottom: 8px;
  font-size: 11px;
  line-height: 1.4;
}
.confirm-text {
  color: #ff9ea1;
  display: block;
  margin-bottom: 8px;
}
.confirm-actions {
  display: flex;
  gap: 8px;
  justify-content: flex-end;
}
.mini-btn {
  background: var(--bg-input);
  color: var(--text);
  border: 1px solid #2a3342;
  border-radius: 6px;
  padding: 5px 8px;
  font-size: 11px;
  cursor: pointer;
}
.mini-btn.danger {
  background: rgba(229, 72, 77, 0.85);
  color: #fff;
  border-color: rgba(229, 72, 77, 0.85);
}

</style>
