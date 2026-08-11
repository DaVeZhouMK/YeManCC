<script setup lang="ts">
import { ref, reactive, computed, onMounted, onUnmounted, onActivated, nextTick, inject, watch, type Ref } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown, { type DropdownOption } from '@/components/Dropdown.vue';
import SegButton from '@/components/SegButton.vue';
import {
  TDP_CEILINGS,
  TDP_MIN,
  FPS_CEILINGS,
  smallestCeiling,
  detectPowerMode,
  detectCpuName,
  detectVendor,
  PW,
  getActiveScheme,
  readTdp,
  setTdp,
  setRtssLimit,
  type Vendor,
} from '@/bridge/yeman';
import {
  FLOAT_PROFILES,
  TDP_FLOAT_STRATEGIES,
  FPS_TARGET_MIN,
  FPS_TARGET_MAX,
  FPS_TARGET_STEP,
  type FloatProfile,
  type FloatInfo,
  type FpsTarget,
  type TdpFloatStrategy,
  setFloatProfile,
  setFloatTarget,
  setTdpFloatStrategy,
  notifyTdpMaxChanged,
  getTdpTarget,
  getFloatInfo,
  onFloatUpdate,
  applyCpuAutoEnable,
} from '@/bridge/autofloat';
import { getPerformanceScheduleOwnership } from '@/bridge/performanceSchedule';
import { readSettingsSection, saveSettingsSection } from '@/bridge/settingsRepository';
import { readTdpAutoApply, writeTdpAutoApply } from '@/bridge/tdpAutoApply';
import InlineIcon from '@/components/InlineIcon.vue';

const powerMode = ref<'ac' | 'dc'>('ac');
const cpuName = ref('检测中…');
const vendor = ref<Vendor>('unknown');
const activeSchemeGuid = ref('');
const isYemanScheme = computed(() => activeSchemeGuid.value === PW.YEMAN.toLowerCase());

// ── TDP 最大值：滑块(左) + 上限下拉(右)── 即时下发硬件并由程序记录
const quickTdp = ref(200);
const quickCeiling = ref(200);

const errMsg = ref('');

const ceilingOpts = TDP_CEILINGS.map((v) => ({ value: v, label: v + ' W' }));
const modeLabel = computed(() => (powerMode.value === 'ac' ? '当前电源为 AC 模式' : '当前电源为 DC 模式'));
const modeIcon = computed(() => (powerMode.value === 'ac' ? 'plug' : 'battery'));

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
  // 程序配置是 TDP 最大值的唯一真相源
  const res = await readTdp('ac').catch(() => null);
  if (res != null) {
    quickTdp.value = res;
    quickCeiling.value = smallestCeiling(res);
    // 目标TDP按钮立即按当前 TDP 最大值计算显示（不必等浮动优化开启/实际应用）
    floatInfo.value = { ...floatInfo.value, tdpMax: res };
  }
}


// ── TDP 最大值：滑块(左) + 上限下拉(右) 提交 ──
// 滑块 commit 即时保存配置并异步下发硬件；打开页面时仅读程序配置，不自动下发硬件。
async function onQuickCommit(v: number) {
  quickTdp.value = v;
  if (await scheduleOwnsTdp()) return;
  await applyTdp(v, { save: true });
}

async function refreshActiveScheme() {
  try {
    activeSchemeGuid.value = (await getActiveScheme()).toLowerCase();
  } catch {
    activeSchemeGuid.value = '';
  }
}
// 选上限档：只改变滑块最大值范围。若当前 TDP 值超出新上限则钳制到新上限
// 并保存+异步下发；否则只改范围，不触发硬件（下拉不应当调 YeManTdpCtl.exe）。
async function onQuickCeiling(v: number) {
  quickCeiling.value = v;
  if (quickTdp.value > v) {
    if (await scheduleOwnsTdp()) return;
    quickTdp.value = v;
    // 被新上限钳制：记录新的 TDP 最大值并异步下发硬件
    void setTdp('ac', v, { apply: true, save: true, vendor: vendor.value }).catch((e) => {
      if (!errMsg.value) errMsg.value = 'TDP 下发失败：' + (e as Error).message;
    });
    void notifyTdpMaxChanged(v).catch(() => {});
  }
}

async function applyTdp(
  v: number,
  opts: { apply?: boolean; save?: boolean } = {}
) {
  errMsg.value = '';
  if (await scheduleOwnsTdp()) return;
  try {
    const apply = opts.apply ?? true;
    const save = opts.save ?? true;
    // 先记录程序配置（fs 写 JSON，毫秒级，await 保证 UI 值已落盘）；
    // 硬件下发异步执行，不阻塞 UI —— 之前「TDP 下拉/滑块卡死」根因就是同步等待
    // YeManTdpCtl.exe（PyInstaller 启动 1~3 秒）导致界面冻结。
    await setTdp('ac', v, { apply: false, vendor: vendor.value, save });
    if (apply) {
      void setTdp('ac', v, { apply: true, save: false, vendor: vendor.value }).catch((e) => {
        if (!errMsg.value) errMsg.value = 'TDP 下发失败：' + (e as Error).message + '（可能需要以管理员身份运行）';
      });
    }
    if (save) void notifyTdpMaxChanged(v).catch(() => {}); // 浮动运行时同步基准，免每轮读 tdp.txt
  } catch (e) {
    errMsg.value = 'TDP 保存失败：' + (e as Error).message + '（可能需要以管理员身份运行）';
  }
}

async function scheduleOwnsTdp(): Promise<boolean> {
  try {
    if (await getPerformanceScheduleOwnership() === 'auto') {
      errMsg.value = '性能调度自动模式已接管 TDP，请在性能调度页编辑组合';
      return true;
    }
  } catch { /* 读取失败时保留原有手动入口 */ }
  return false;
}

// ── 启动 / 唤醒自动应用 TDP ──
// 语义：两个开关都不优先于「帧数目标浮动优化」；浮动开启(enabled)时由浮动接管 TDP，
// 这两项实际不生效（applyAutoTdpIfNeeded 内部已按浮动状态短路）。仅浮动未开启时，
// 开机/唤醒才把程序记录的 TDP 最大值重新下发硬件。
const bootApplyTdp = ref(true);
const wakeApplyTdp = ref(true);
async function loadAutoApply() {
  try {
    const cfg = await readTdpAutoApply();
    bootApplyTdp.value = cfg.boot;
    wakeApplyTdp.value = cfg.wake;
  } catch {
    /* 保持默认开启 */
  }
}
async function onBootApplyTdp(v: boolean) {
  bootApplyTdp.value = v;
  await writeTdpAutoApply({ boot: v, wake: wakeApplyTdp.value }).catch(() => {});
}
async function onWakeApplyTdp(v: boolean) {
  wakeApplyTdp.value = v;
  await writeTdpAutoApply({ boot: bootApplyTdp.value, wake: v }).catch(() => {});
}

// ── 统一刷新入口（供全局触发器调用）──
async function refresh() {
  await Promise.allSettled([
    refreshModeAndCpu(),
    refreshActiveScheme(),
    loadValues(),
  ]);
}

// ── 帧数目标浮动 TDP + CPU 优化 ──
const floatInfo = ref<FloatInfo>(getFloatInfo());
const floatOn = computed(() => floatInfo.value.enabled);
const offFloatUpdate = onFloatUpdate((info) => {
  // 合并 tdpMax 显示：浮动未启用时模块内 tdpMax=0，若整包替换会把 loadValues
  // 从 control-config.json 读到的显示值清成 0（并发写竞态 → 2026-08-05 修复）。
  floatInfo.value = info.tdpMax > 0 ? info : { ...info, tdpMax: floatInfo.value.tdpMax };
  // HWiNFO 报错仅在「一次完整自动修复（约 10 秒轮询）」结束且仍不可用时才弹出；
  // 修复成功后自动清除（只清 HWiNFO 相关提示，不干扰 TDP 下发错误）
  if (info.enabled && info.hwinfoErr) {
    if (errMsg.value === '') {
      errMsg.value = 'HWiNFO 未开启或共享内存不可用 —— 已尝试自动修复，请手动打开 HWiNFO 并在「传感器设置」中启用「共享内存支持」。';
    }
  } else if (!info.hwinfoErr && errMsg.value.includes('HWiNFO')) {
    errMsg.value = '';
  }
});
onUnmounted(offFloatUpdate);

// 帧数目标：滑块(实际值) + 上限下拉(右) —— 与「监控/锁帧」页同款组合。
// 浮动目标必须保持有效帧率（0 会破坏 CPU/TDP 控制循环判定），故上限下拉不含「不锁帧(0)」；
// 档位数值与锁帧页共用 FPS_CEILINGS（30/60/90/120/200/300），滑块步进 5 一致。
const fpsTargetVal = computed<number>({
  get: () => {
    const t = Number(floatInfo.value.target);
    return Number.isFinite(t) && t >= FPS_TARGET_MIN && t <= FPS_TARGET_MAX ? t : 60;
  },
  set: (v: number) => {
    floatInfo.value = { ...floatInfo.value, target: v };
  },
});
const fpsCeiling = ref(300); // 帧数目标上限（滑块最大值），FPS_CEILINGS 单一数据源
const fpsCeilingOpts: DropdownOption[] = FPS_CEILINGS
  .filter((v) => v > 0)
  .map((v) => ({ value: v, label: `${v} FPS` }));
function smallestFpsCeiling(val: number): number {
  const list = fpsCeilingOpts.map((o) => Number(o.value));
  for (const c of list) if (c >= val) return c;
  return list[list.length - 1];
}
// 帧数目标上限：只改滑块范围；当前目标超出新上限时钳制并保存（与监控/锁帧页一致）
function onFpsCeiling(val: number) {
  fpsCeiling.value = val;
  const cur = fpsTargetVal.value;
  if (cur > val) onFloatTarget(val);
}
// 滑块松手才提交（拖动过程只改本地显示，避免拖动每步都触发 RTSS 重载）
function onFloatTargetCommit(v: number) {
  onFloatTarget(v);
}
// 目标值变化（手柄/外部）时确保上限 >= 目标，否则滑块被夹住选不上去
watch(() => floatInfo.value.target, (t) => {
  const num = Number(t);
  if (Number.isFinite(num) && num >= FPS_TARGET_MIN) {
    const c = smallestFpsCeiling(num);
    if (fpsCeiling.value !== c) fpsCeiling.value = c;
  }
});
fpsCeiling.value = smallestFpsCeiling(fpsTargetVal.value);
// 固定模板按钮（单行，SteamOS 风格）：
//  TDP 目标值：单行「TDP xxW」（去掉「目标」前缀与「激进TDP压制」第二排）
//  CPU 浮动值：单行「min-max」（去掉「激进CPU压制」第二排小字，不显示 GHz 单位）
const FLOAT_PROFILE_OPTS = (Object.keys(FLOAT_PROFILES) as FloatProfile[]).map((k) => ({
  value: k,
  label: k === 'none'
    ? '无压制'
    : `${(FLOAT_PROFILES[k].min / 1000).toFixed(1)}-${(FLOAT_PROFILES[k].max / 1000).toFixed(1)}`,
}));
const tdpStrategyOpts = computed(() => (Object.keys(TDP_FLOAT_STRATEGIES) as TdpFloatStrategy[]).map((k) => {
  if (k === 'none') return { value: k, label: '无下降' };
  // 优先用浮动运行的实时基准；未开启时回退到页面读到的 TDP 最大值（打开即显示）
  const max = floatInfo.value.tdpMax > 0 ? floatInfo.value.tdpMax : quickTdp.value;
  const target = getTdpTarget(max, k);
  const has = max > 0 && target > 0;
  return {
    value: k,
    label: has ? `TDP ${target}W` : 'TDP --W',
  };
}));

async function syncRtssFps(target: number) {
  // 自动浮动只临时覆盖 RTSS；用户的 FPS 帧率上限由程序配置单独记录。
  // 无条件应用：即使当前限制为 0 也启用，关闭时由 autofloat 恢复原值。
  try {
    await setRtssLimit(target);
  } catch { /* 忽略 */ }
}

function refreshRtssAfterFloatChange() {
  // RTSS 页面通过全局刷新触发器重新读取实时锁帧值，避免必须重启应用才能看到新数字。
  if (globalRefreshKey) globalRefreshKey.value++;
}

async function onFloatTarget(v: number) {
  if (await scheduleOwnsTdp()) return;
  const t = v as FpsTarget;
  // 滑块只负责设置并保存帧数目标；是否启用浮动完全由电池栏目（autoEnable）决定，
  // 移动滑块不得自行启用浮动（否则会绕过 autoEnable 的逻辑）。
  floatInfo.value = { ...floatInfo.value, target: t };
  setFloatTarget(t);
  // 仅当浮动已由 autoEnable 逻辑启用时，实时把新目标应用到 RTSS 锁帧
  if (floatOn.value) {
    void syncRtssFps(t).finally(refreshRtssAfterFloatChange);
  }
}
async function onFloatProfile(v: string) {
  if (await scheduleOwnsTdp()) return;
  const p = v as FloatProfile;
  floatInfo.value = { ...floatInfo.value, profile: p };
  void setFloatProfile(p);
}
async function onTdpStrategy(v: string) {
  if (await scheduleOwnsTdp()) return;
  const strategy = v as TdpFloatStrategy;
  floatInfo.value = { ...floatInfo.value, tdpStrategy: strategy };
  setTdpFloatStrategy(strategy);
}

const FLOAT_ACTION_TEXT: Record<string, string> = {
  'down-aggr': '↓ 降积极性',
  'down-freq': '↓ 降主频',
  'down-tdp': '↓ 降 TDP',
  'up-freq': '↑ 升主频',
  'up-aggr': '↑ 升积极性',
  'up-tdp': '↑ 回调 TDP',
  hold: '稳定',
  idle: '待机',
  wait: '监控启动中…',
};
// 监控显示：固定两排（气泡字体），按总宽 4 格等比分配：
//   第一排 游戏名字(2格) + 实际TDP(2格)
//   第二排 FPS/1%Low + CPU主频+方向 + GPU占用 + 执行TDP+方向（四格各 1 格）
// 方向箭头：↑=升 / ↓=降 / -=稳定；未就绪时保留占位文本（横线）保证布局稳定。
// 注：FLOAT_ACTION_TEXT 目前不再用于气泡文字（已改为箭头方向），仅保留作调试/未来复用。
interface StatusCells {
  game: string;
  gameTone: 'on' | 'wait';
  tdpActual: string;
  fps: string;
  fps1: string;
  freq: string;
  cpuDir: '↑' | '↓' | '-';
  gpu: string;
  tdpApplied: string;
  tdpDir: '↑' | '↓' | '-';
}
function dirFromAction(a: string): '↑' | '↓' | '-' {
  if (a === 'up-freq' || a === 'up-aggr' || a === 'up-tdp') return '↑';
  if (a === 'down-freq' || a === 'down-aggr' || a === 'down-tdp') return '↓';
  return '-';
}
function dash(value: number, suffix: string, fixed = 0): string {
  return value > 0 ? `${value.toFixed(fixed)}${suffix}` : `--${suffix}`;
}
function intOrDash(value: number, suffix = ''): string {
  if (!Number.isFinite(value) || value <= 0) return `--${suffix}`;
  return `${Math.round(value)}${suffix}`;
}
const floatCells = computed<StatusCells>(() => {
  const i = floatInfo.value;
  // CPU 方向只看 CPU 动作；TDP 方向看 TDP 专用状态（up-tdp/down-tdp/hold…）
  const cpuDir = dirFromAction(i.lastAction);
  const tdpDir = i.tdpState === 'up-tdp' ? '↑' : i.tdpState === 'down-tdp' ? '↓' : '-';
  if (!i.enabled) {
    return {
      game: '待机', gameTone: 'wait', tdpActual: '--W',
      fps: '--', fps1: '--', freq: '--GHz', cpuDir: '-',
      gpu: '--%', tdpApplied: '--W', tdpDir: '-',
    };
  }
  const s = i.status;
  const hwinfoDown = i.hwinfoDown;
  if (hwinfoDown) {
    return {
      game: 'HWiNFO 不可用', gameTone: 'wait', tdpActual: '--W',
      fps: '--', fps1: '--', freq: '--GHz', cpuDir: '-',
      gpu: '--%', tdpApplied: dash(i.tdpApplied, 'W'), tdpDir,
    };
  }
  if (!s) {
    return {
      game: '监控启动中…', gameTone: 'wait', tdpActual: '--W',
      fps: '--', fps1: '--', freq: '--GHz', cpuDir: '-',
      gpu: '--%', tdpApplied: dash(i.tdpApplied, 'W'), tdpDir,
    };
  }
  const game = s.game && s.fps > 0 ? s.game : '待机';
  // 执行值：若比实际 TDP（packagePower）高出 >3W，说明还没降到位/读数滞后，
  // 直接从「实际TDP+3」开始显示，避免界面出现远高于实际功耗的离谱数字。
  const actualP = Number(s.packagePower) || 0;
  let appliedDisp = i.tdpApplied;
  if (actualP > 0 && appliedDisp > 0 && appliedDisp - actualP > 3) appliedDisp = actualP + 3;
  return {
    game,
    gameTone: s.game && s.fps > 0 ? 'on' : 'wait',
    tdpActual: dash(actualP, 'W', 1),
    fps: intOrDash(s.fps),
    fps1: intOrDash(s.fps1),
    freq: i.state.freq > 0 ? `${(i.state.freq / 1000).toFixed(1)}GHz` : '--GHz',
    cpuDir,
    gpu: intOrDash(s.gpu, '%'),
    tdpApplied: appliedDisp > 0 ? `${Math.round(appliedDisp)}W` : '--W',
    tdpDir,
  };
});

// ── 自动启用（CPU 优化按电源状态自动激活）──
const AUTO_ENABLE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_auto_enable.json';
const AUTO_ENABLE_OPTS = [
  { value: 'never', label: '从不' },
  { value: 'always', label: '总是' },
  { value: 'ac', label: '接通电源' },
  { value: 'dc', label: '使用电池' },
];
const autoEnable = ref<string>('never');
async function loadAutoEnable() {
  try {
    const cpu = await readSettingsSection<any>('cpu');
    if (typeof cpu.autoEnable?.mode === 'string') autoEnable.value = cpu.autoEnable.mode;
  } catch { /* 文件不存在或格式无效，就用默认 never */ }
}
async function saveAutoEnable(v: string) {
  if (await scheduleOwnsTdp()) return;
  const previous = autoEnable.value;
  autoEnable.value = v;
  try {
    await saveSettingsSection('cpu', { autoEnable: { mode: v } });
    // 只在新模式持久化成功后应用，避免重新读取旧磁盘配置。
    await applyCpuAutoEnable(v);
  } catch (e) {
    autoEnable.value = previous;
    errMsg.value = '保存自动启用模式失败：' + (e as Error).message;
    return;
  }
  // 模块状态变更后回显
  floatInfo.value = getFloatInfo();
  // 联动刷新：让「监控/锁帧」页的 AC/DC 锁帧任务锁定状态立即跟随本次下拉选择
  // （从不=两边都可调；总是=两边都锁；接通电源=锁 AC；使用电池=锁 DC）
  refreshRtssAfterFloatChange();
}

// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => refresh());
}

// KeepAlive 下切到本页时主动读取最新程序配置（滑块始终显示记录值），
// 解决「手柄在其它页面调节后切到 TDP 页不刷新」的问题。
onActivated(() => {
  refreshActiveScheme().catch(() => {});
  loadValues().catch(() => {});
  loadAutoApply().catch(() => {});
});

onMounted(async () => {
  await nextTick();
  // 并行异步加载（不串行等待，避免切页卡顿）
  await Promise.allSettled([
    refreshModeAndCpu(),
    refreshActiveScheme(),
    loadValues(),
    loadAutoApply(),
    loadAutoEnable(),
  ]);
  // 性能调度已接管时，TDP 页预加载只能回显，不能再按旧 cpu_auto_enable.json
  // 重新载入 autofloat.json 或关闭浮动，否则会覆盖刚恢复/热切换的调度目标。
  try {
    const scheduleOwner = await getPerformanceScheduleOwnership();
    if (scheduleOwner !== 'auto') await applyCpuAutoEnable();
    floatInfo.value = getFloatInfo();
  } catch {
    /* 忽略：恢复失败不影响其它功能 */
  }
});
</script>

<template>
  <div class="page">
    <div class="statusbar">
      <div>
        <div class="sb-cpu">{{ cpuName }}</div>
        <div class="sb-sub muted"><InlineIcon :name="modeIcon" /> {{ modeLabel }}</div>
      </div>
    </div>

    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>

    <!-- 快速切换 TDP：首行 label+当前值+上限下拉 水平对齐，第二行滑块轨道 -->
    <section class="card quick-card">
      <h3 class="card-title"><InlineIcon name="tdp" /> 快速切换 TDP</h3>
      <div class="quick-head">
        <span class="quick-label">TDP 最大值</span>
        <span class="quick-val">{{ quickTdp }} W</span>
        <span class="quick-sep">上限</span>
        <Dropdown
          class="quick-ceiling"
          :model-value="quickCeiling"
          :options="ceilingOpts"
          color="accent"
          width="92px"
          @change="onQuickCeiling"
        />
      </div>
      <Slider
        v-model="quickTdp"
        :min="TDP_MIN"
        :max="quickCeiling"
        :step="1"
        color="accent"
        @commit="onQuickCommit"
      />
      <div class="quick-auto">
        <Toggle
          v-model="bootApplyTdp"
          label="启动自动应用TDP"
          description="开机后自动把当前 TDP 最大值下发硬件"
          color="accent"
          @update:model-value="(v: boolean) => onBootApplyTdp(v)"
        />
        <Toggle
          v-model="wakeApplyTdp"
          label="唤醒自动应用TDP"
          description="从睡眠/休眠唤醒后自动应用 TDP 最大值"
          color="accent"
          @update:model-value="(v: boolean) => onWakeApplyTdp(v)"
        />
        <div v-if="floatOn" class="quick-auto-hint">
          帧数目标浮动优化开启时，以上两项由浮动接管、不生效
        </div>
      </div>
    </section>

    <!-- 帧数目标浮动 TDP + CPU 优化 -->
    <section class="card" :class="{ disabled: !isYemanScheme }">
      <div class="float-head">
        <span class="float-title"><InlineIcon name="cpufloat" /> 帧数目标浮动TDP+CPU优化</span>
        <div class="float-head-right">
          <Dropdown
            :model-value="autoEnable"
            :options="AUTO_ENABLE_OPTS"
            color="accent"
            width="120px"
            :disabled="!isYemanScheme"
            @update:model-value="(v: string) => saveAutoEnable(v)"
          />
        </div>
      </div>
      <template v-if="autoEnable !== 'never'">
        <div class="float-row">
          <span class="row-label">帧数目标</span>
          <div class="lock-combo">
            <Slider
              :model-value="fpsTargetVal"
              :min="FPS_TARGET_MIN"
              :max="fpsCeiling > 0 ? fpsCeiling : FPS_TARGET_MIN"
              :step="FPS_TARGET_STEP"
              label="帧数目标"
              unit="FPS"
              color="accent"
              :disabled="!isYemanScheme"
              aria-label="帧数目标"
              @update:model-value="(v: number) => (fpsTargetVal = v)"
              @commit="(v: number) => onFloatTargetCommit(v)"
            />
            <Dropdown
              :model-value="fpsCeiling"
              :options="fpsCeilingOpts"
              color="accent"
              width="110px"
              :disabled="!isYemanScheme"
              aria-label="帧数目标上限"
              @change="(v: string | number) => onFpsCeiling(Number(v))"
            />
          </div>
        </div>
        <div class="float-row">
          <span class="row-label">目标TDP值【TDP降低策略】</span>
          <SegButton
            :model-value="floatInfo.tdpStrategy"
            :options="tdpStrategyOpts"
            color="accent"
            full
            :disabled="!isYemanScheme"
            @update:model-value="(v: string | number) => onTdpStrategy(String(v))"
          />
        </div>
        <div class="float-row">
          <span class="row-label">CPU浮动值【CPU实际范围】</span>
          <SegButton
            :model-value="floatInfo.profile"
            :options="FLOAT_PROFILE_OPTS"
            color="accent"
            full
            :disabled="!isYemanScheme"
            @update:model-value="(v: string | number) => onFloatProfile(String(v))"
          />
        </div>
        <div v-if="floatOn" class="float-status">
          <span class="float-cell float-cell-name cell-span2" :data-tone="floatCells.gameTone"><span class="cell-txt">{{ floatCells.game }}</span></span>
          <span class="float-cell cell-span2"><span class="cell-txt">实际 TDP {{ floatCells.tdpActual }}</span></span>

          <span class="float-cell"><span class="cell-txt">FPS {{ floatCells.fps }} <em>({{ floatCells.fps1 }})</em></span></span>
          <span class="float-cell"><span class="cell-txt">CPU {{ floatCells.freq }} {{ floatCells.cpuDir }}</span></span>
          <span class="float-cell"><span class="cell-txt">GPU {{ floatCells.gpu }}</span></span>
          <span class="float-cell"><span class="cell-txt">执行 {{ floatCells.tdpApplied }} {{ floatCells.tdpDir }}</span></span>
        </div>
      </template>
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
  background: color-mix(in srgb, var(--bg-panel) 72%, transparent);
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
  color: var(--dc-accent);
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
/* ── AC/DC 浮动上限行 + 上限下拉 + 内联锁定按钮 ── */
.tdp-row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.tdp-row > .slider {
  flex: 1 1 auto;
  min-width: 0;
}
/* ── 快速切换 TDP：滑块(左) + 上限下拉(右) ── */
.quick-head {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
}
.quick-label {
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
}
.quick-val {
  font-size: 14px;
  font-weight: 700;
  color: var(--accent);
  font-variant-numeric: tabular-nums;
}
.quick-sep {
  flex: 1;
  text-align: right;
  font-size: 11px;
  color: var(--text-dim);
}
.quick-ceiling {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
}
.quick-ceiling :deep(.dd-trigger) {
  min-height: var(--btn-min-h);
  height: auto;
}
/* ── 快速切换 TDP：启动/唤醒自动应用开关 ── */
.quick-auto {
  margin-top: 10px;
  padding-top: 8px;
  border-top: 1px solid rgba(138, 151, 168, 0.18);
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.quick-auto-hint {
  margin-top: 4px;
  font-size: 10px;
  color: #ffb454;
  line-height: 1.4;
  padding-left: 2px;
}
/* ── 帧数目标浮动 TDP + CPU 优化 ── */
.float-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}
.float-title {
  font-size: 13px;
  font-weight: 700;
  color: var(--text);
}
.float-head-right {
  display: flex;
  align-items: center;
  gap: 8px;
}
.float-sub { font-size: 10px; font-weight: 400; color: #8a97a8; }
.float-row { display: flex; flex-direction: column; gap: 6px; margin-top: 12px; }
.lock-combo {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 110px;
  align-items: end;
  gap: 10px;
  margin-top: 8px;
}
.row-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-dim);
  letter-spacing: 0.02em;
}
.float-status {
  margin-top: 14px;
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  grid-auto-rows: minmax(34px, auto);
  gap: 6px;
  align-items: stretch;
}
.cell-span2 { grid-column: span 2; }
.float-cell {
  min-width: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 8px 10px;
  border-radius: 10px;
  background: rgba(9, 30, 47, 0.55);
  border: 1px solid rgba(138, 151, 168, 0.22);
  color: #8a97a8;
  font-size: 12px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  line-height: 1.2;
  overflow: hidden;
}
/* 单元格内文本统一截断（防止长文本撑破网格导致错位） */
.cell-txt {
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  text-align: center;
}
/* 监控气泡统一使用程序主色（青色），不再按 data-tone 上彩 */
.float-cell[data-tone='primary'] em {
  font-style: normal;
  opacity: 0.75;
  margin-left: 2px;
}
.float-cell-name[data-tone='on'] {
  color: #8a97a8;
}
.float-cell-name[data-tone='wait'] {
  color: #8a97a8;
  opacity: 0.75;
}
.float-hint { font-size: 10px; color: #8a97a8; margin-top: 6px; line-height: 1.4; text-align: center; }
</style>
