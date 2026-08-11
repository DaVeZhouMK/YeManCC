<script setup lang="ts">
import { computed, nextTick, onActivated, onDeactivated, onMounted, onUnmounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import Dropdown from '@/components/Dropdown.vue';
import Slider from '@/components/Slider.vue';
import {
  readPowerParams,
  ensureRememberedYemanSchemeActive,
  rebuildYemanScheme,
  TDP_CEILINGS,
  TDP_MIN,
  FPS_CEILINGS,
  type PowerParams,
} from '@/bridge/yeman';
import {
  FLOAT_PROFILES,
  FPS_TARGET_MIN,
  getFloatInfo,
  onFloatUpdate,
  TDP_FLOAT_EXECUTION_LABELS,
  TDP_FLOAT_STRATEGY_ORDER,
  getTdpTarget,
  type FloatInfo,
  type FloatProfile,
  type TdpFloatStrategy,
} from '@/bridge/autofloat';
import {
  closeGame,
  hasSuspendedState,
  QUICKAPP_SUSPENDED_EVENT,
  resumeGame,
  suspendGame,
  toggleMouseMode,
} from '@/bridge/gameproc';
import { isMouseModeSuppressed } from '@/gamepad/engine';
import { getUiSetting, loadUiSettings, setUiSettings } from '@/bridge/uiSettings';
import {
  refreshGameStatus,
  subscribeGameStatus,
  detectedGameName,
  type DetectedGame,
} from '@/bridge/gamedetect';
import { topMonitorData, type TopMonData } from '@/bridge/topmon';
import { registerScheduledTask } from '@/scheduler';
import { openOrSearchGameTrainer } from '@/bridge/gameTrainer';
import {
  applyGameCustomProfiles,
  applyPerformanceSchedule,
  defaultPerformanceScheduleConfig,
  disablePerformanceSchedule,
  getCustomProfileFor,
  getGameCustomConfig,
  getPerformanceScheduleWarning,
  loadGameCustomConfig,
  loadPerformanceSchedule,
  onPerformanceScheduleWarning,
  resetPerformanceScheduleProfiles,
  saveGameCustomConfig,
  savePerformanceSchedule,
  SkipApplyError,
  snapshotPerformanceSchedule,
  CORE_MODE_OPTIONS,
  type CpuPreset,
  type CoreMode,
  type GameCustomConfig,
  type GameCustomProfile,
  type PerformanceScheduleConfig,
  type PowerSide,
  type ScheduleMode,
  type ScheduleProfile,
} from '@/bridge/performanceSchedule';
import { detectCoreArchitecture, type CoreArchitectureInfo } from '@/bridge/yeman';

const config = ref<PerformanceScheduleConfig>(defaultPerformanceScheduleConfig());
const selectedSide = ref<PowerSide>('ac');
const powerSide = ref<PowerSide>('ac');
const editing = ref<ScheduleMode | null>(null);
const editingDraft = ref<ScheduleProfile | null>(null);
// 编辑面板独立的 TDP / FPS 上限（与草稿值解耦，避免下拉选不上去）
const editingTdpCeiling = ref(TDP_CEILINGS[TDP_CEILINGS.length - 1]);
const editingFpsCeiling = ref(200); // FPS_CEILINGS 最后一个值（由 yeman.ts 单一数据源导入）
const coreArchitecture = ref<CoreArchitectureInfo | null>(null);
const isHeterogeneousCpu = computed(() => coreArchitecture.value?.heterogeneous === true);
const busy = ref(false);
const applyingSide = ref<Record<PowerSide, boolean>>({ ac: false, dc: false });
const pauseBusy = ref(false);
const pauseButtonEl = ref<HTMLButtonElement | null>(null);
const closeBusy = ref(false);
const mouseBusy = ref(false);
const mouseNotice = ref('');
const statusMsg = ref('');
const errMsg = ref('');
const warningMsg = ref('');
const trainerBusy = ref(false);
const game = ref<DetectedGame | null>(null);
// 空名称也要保留为合法键「.exe」：不能用 truthiness 拒绝该配置。
function normalizeCustomExeKey(raw: string): string {
  const base = raw.trim().toLowerCase();
  return base.endsWith('.exe') ? base : `${base}.exe`;
}
// 自定义游戏模式只绑定已识别游戏；未识别时不显示/不弹出自定义卡片。
const gameCustomConfig = ref<GameCustomConfig>(getGameCustomConfig());
const customEditing = ref<PowerSide | null>(null); // 正在编辑自定义模式的哪一侧（ac/dc）
const gameExeKey = computed(() => {
  const g = game.value;
  if (!g) return '';
  const fromPath = g.path ? g.path.split(/[\\/]/).pop() : '';
  const base = (fromPath || g.name || '').toLowerCase().trim();
  if (!base) return ''; // 未识别到有效 exe 时隐藏自定义卡片
  return normalizeCustomExeKey(base);
});
const customActive = computed(() => !!gameExeKey.value && !!gameCustomConfig.value.entries[gameExeKey.value]);
const paused = ref(false);
const mouseOn = ref(false);
const showMonitor = ref(getUiSetting('scheduleMonitor'));
watch(showMonitor, (v) => {
  void setUiSettings({ scheduleMonitor: v });
});
// 顶部监控条是全局唯一采样消费者；本页直接消费共享快照，避免再次读取 topmon.json。
const topMon = topMonitorData;
const floatInfo = ref<FloatInfo>(getFloatInfo());
const manualParams = ref<PowerParams | null>(null);

type MonitorPoint = {
  ts: number;
  fps: number;
  fps1: number;
  targetFps: number;
  actualW: number;
  appliedW: number;
  targetW: number;
  cpuFreqMhz: number; // 系统 CPU 主频（topMon.freqMhz）
  cpuUsage: number;   // 系统 CPU 占用 %（topMon.cpuUsage）
  gpuPowerW: number;  // 显卡瓦数 W（topMon.gpuPowerW，多 GPU 取最高）
  gpuClockMhz: number;// 显卡主频 MHz（topMon.gpuClockMhz，多 GPU 取最高）
};

const monitorHistory = ref<MonitorPoint[]>([]);
const MONITOR_MAX_POINTS = 60;

let unsubGame: (() => void) | null = null;
let stopTopMon: (() => void) | null = null;
let stopMonitorChart: (() => void) | null = null;
let offFloatUpdate: (() => void) | null = null;
let offScheduleWarning: (() => void) | null = null;
let statusHideTimer: ReturnType<typeof setTimeout> | null = null;
type ModeApplyRequest = { side: PowerSide; mode: ScheduleMode; revision: number };
const pendingModeApply: Record<PowerSide, ModeApplyRequest | null> = { ac: null, dc: null };
let modeApplyRunning = false;
let modeApplyRevision = 0;
const MODE_APPLY_SETTLE_MS = 120;

const MODE_META: Record<ScheduleMode, { label: string }> = {
  eco: { label: '节能' },
  balanced: { label: '平衡' },
  medium: { label: '中等' },
  performance: { label: '高性能' },
  elite: { label: '精睿' },
  extreme: { label: '极致性能' },
};
const MODE_ORDER: ScheduleMode[] = [
  'eco',
  'balanced',
  'medium',
  'performance',
  'elite',
  'extreme',
];
const POWER_SIDES: PowerSide[] = ['ac', 'dc'];
const modeOptionSub = (side: PowerSide, mode: ScheduleMode): string => {
  return modeDetail(side, mode);
};
const modeOptionsFor = (side: PowerSide) => MODE_ORDER.map((mode) => ({
  value: mode,
  label: MODE_META[mode].label,
  sub: modeOptionSub(side, mode),
}));
const CPU_OPTS = [
  { value: 'balanced', label: '平衡', sub: 'Performance' },
  { value: 'turbo', label: '高性能', sub: 'Turbo' },
  { value: 'elite', label: '精睿', sub: 'Elite' },
  { value: 'extreme', label: '极致', sub: 'Extreme' },
];
// CPU 浮动值：选项显示真实 CPU 范围数字（无 GHz 单位）。
// 小标按 CPU 浮动档位显示对应的调度语义，不显示执行瓦数。
// CPU 浮动值：按压制递增排序（无 → 小 → 中 → 大 → 激进），与 TDP 浮动幅度同向。
// 小标为对应压制语义（不是执行瓦数），避免把 TDP 瓦数误当成 CPU 选项属性。
const CPU_FLOAT_ORDER: FloatProfile[] = ['none', 'eco', 'bal', 'perf', 'aggressive'];
const CPU_FLOAT_LABEL: Record<FloatProfile, string> = {
  none: '无下降',
  eco: '小浮动',
  bal: '中浮动',
  perf: '大浮动',
  aggressive: '激进浮动',
};
const CPU_FLOAT_OPTS = computed(() => CPU_FLOAT_ORDER.map((k) => ({
  value: k,
  label: k === 'none'
    ? '无压制'
    : `${(FLOAT_PROFILES[k].min / 1000).toFixed(1)}-${(FLOAT_PROFILES[k].max / 1000).toFixed(1)}`,
  sub: CPU_FLOAT_LABEL[k],
})));
const CORE_MODE_OPTS = CORE_MODE_OPTIONS;
// TDP 浮动幅度：无下降 → 小 → 中 → 大 → 激进（与 CPU 浮动值同向）。
// 主体显示实际执行瓦数，小字显示浮动策略；不要把策略名称和瓦数顺序反置。
const TDP_STRATEGY_OPTS = computed(() => {
  const maxW = editingDraft.value?.tdpMax ?? 200;
  return TDP_FLOAT_STRATEGY_ORDER.map((strategy) => ({
    value: strategy,
    label: `${getTdpTarget(maxW, strategy)}W`,
    sub: TDP_FLOAT_EXECUTION_LABELS[strategy],
  }));
});
const editModeOptions = computed(() => MODE_ORDER.map((mode) => ({
  value: mode,
  label: MODE_META[mode].label,
  sub: modeOptionSub(selectedSide.value, mode),
})));
const EDIT_SIDE_OPTIONS = POWER_SIDES.map((side) => ({
  value: side,
  label: side.toUpperCase(),
  sub: side === 'ac' ? '交流电配置' : '电池配置',
}));
const STRATEGY_LABEL: Record<TdpFloatStrategy, string> = {
  none: '无下降',
  aggressive: '激进浮动',
  large: '大幅浮动',
  medium: '中幅浮动',
  small: '小幅浮动',
};

const activeMode = computed(() => config.value.active[powerSide.value]);
const activeProfile = computed<ScheduleProfile>(() => config.value.profiles[powerSide.value][activeMode.value]);
const sideColor = computed(() => selectedSide.value === 'dc' ? 'dc' : 'accent');

async function refreshCoreArchitectureForAuto(): Promise<void> {
  // Manual mode intentionally does not probe or refresh hybrid-core state.
  if (!config.value.enabled) {
    coreArchitecture.value = null;
    return;
  }
  coreArchitecture.value = await detectCoreArchitecture();
}

const gameName = computed(() => {
  const detected = detectedGameName(game.value);
  const monitored = floatInfo.value.status?.game?.trim();
  return detected || monitored || '未识别游戏';
});
const powerText = computed(() => {
  const v = floatInfo.value.status?.packagePower || topMon.value?.tdpW || 0;
  return v > 0 ? v.toFixed(1) + 'W' : '--';
});
const executionPowerText = computed(() => {
  let applied = Number(floatInfo.value.tdpApplied) || 0;
  const actual = Number(floatInfo.value.status?.packagePower) || 0;
  if (actual > 0 && applied > 0 && applied - actual > 3) applied = actual + 3;
  return applied > 0 ? `${Math.round(applied)}W` : '--';
});
const targetFpsText = computed(() =>
  activeProfile.value.fpsTarget > 0 ? String(activeProfile.value.fpsTarget) : '不锁帧'
);
const latestMonitor = computed(() => {
  const points = monitorHistory.value;
  return points.length > 0 ? points[points.length - 1] : null;
});

// 8 项核心指标磁贴（FPS / 1%Low / 当前功耗 / 执行瓦数 / CPU主频 / CPU占用 / 显卡主频 / 显卡瓦数【均 HWiNFO，多 GPU 取最高】）
const monitorTiles = computed(() => {
  const m = latestMonitor.value;
  const num = (v: number | undefined, digits = 0, unit?: string) => {
    const ok = v !== undefined && v > 0;
    return { value: ok ? v!.toFixed(digits) : '--', unit: ok ? (unit ?? '') : '' };
  };
  const cpuFreq = num(m?.cpuFreqMhz, 1);
  const cpuFreqV = cpuFreq.value === '--' ? '--' : (Number(cpuFreq.value) / 1000).toFixed(1);
  const gpuClock = num(m?.gpuClockMhz, 1);
  const gpuClockV = gpuClock.value === '--' ? '--' : (Number(gpuClock.value) / 1000).toFixed(1);
  return [
    { key: 'fps',     label: 'FPS',        value: num(m?.fps).value,            unit: '' },
    { key: 'fps1',    label: '1% Low',     value: num(m?.fps1).value,           unit: '' },
    { key: 'power',   label: '当前功耗',   value: num(m?.actualW, 1).value,     unit: 'W' },
    { key: 'applied', label: '执行瓦数',   value: num(m?.appliedW).value,       unit: 'W' },
    { key: 'freq',    label: 'CPU主频',    value: cpuFreqV,                     unit: 'GHz' },
    { key: 'usage',   label: 'CPU占用',    value: num(m?.cpuUsage).value,       unit: '%' },
    { key: 'gpuclk',  label: '显卡主频',   value: gpuClockV,                    unit: 'GHz' },
    { key: 'gpu',     label: '显卡瓦数',   value: num(m?.gpuPowerW, 1).value,   unit: 'W' },
  ];
});
// 监控磁贴（label/tag/value/unit 由 monitorTiles computed 提供，无图标、扁平 2 排）
function sampleMonitor(): void {
  const info = floatInfo.value;
  const status = info.status;
  const point: MonitorPoint = {
    ts: Date.now(),
    fps: Math.max(0, Number(status?.fps) || 0),
    fps1: Math.max(0, Number(status?.fps1) || 0),
    targetFps: Math.max(0, Number(info.target) || activeProfile.value.fpsTarget),
    actualW: Math.max(0, Number(status?.packagePower) || Number(topMon.value?.tdpW) || 0),
    appliedW: Math.max(0, Number(info.tdpApplied) || 0),
    targetW: Math.max(0, Number(info.tdpTarget) || activeProfile.value.tdpMax),
    cpuFreqMhz: Math.max(0, Number(topMon.value?.freqMhz) || 0),
    cpuUsage: Math.max(0, Number(topMon.value?.cpuUsage) || 0),
    gpuPowerW: Math.max(0, Number(topMon.value?.gpuPowerW) || 0),
    gpuClockMhz: Math.max(0, Number(topMon.value?.gpuClockMhz) || 0),
  };
  monitorHistory.value = [...monitorHistory.value.slice(-(MONITOR_MAX_POINTS - 1)), point];
}

const remainText = computed(() => {
  const min = Number(topMon.value?.remainMin);
  if (powerSide.value === 'ac') return '交流供电';
  if (!Number.isFinite(min) || min < 0) return '暂无数据';
  const h = Math.floor(min / 60);
  const m = Math.round(min % 60);
  return h > 0 ? `${h}小时${m}分` : `${m}分钟`;
});

// 编辑面板：TDP / 帧数目标上限下拉（与监控锁帧 / TDP 功耗页共用 FPS_CEILINGS 单一数据源：0=不锁帧，上限 300）。
const tdpCeilingOpts = TDP_CEILINGS.map((v) => ({ value: v, label: `${v} W` }));
const fpsCeilingOpts = FPS_CEILINGS.map((v) => ({ value: v, label: v === 0 ? '不锁帧' : `${v} FPS` }));
function smallestCeiling(list: number[], val: number): number {
  for (const c of list) if (c >= val) return c;
  return list[list.length - 1];
}
// 上限：由编辑面板下拉独立维护（与草稿值解耦），仅当滑块值超出时才自动抬升
const tdpCeiling = computed(() => editingTdpCeiling.value);
const fpsCeiling = computed(() => editingFpsCeiling.value);
// 改上限：下拉存储新上限；若 TDP 当前值超出则钳制到新上限
function onTdpCeilingChange(v: number | string) {
  const next = Number(v);
  if (!editingDraft.value) return;
  editingTdpCeiling.value = next;
  if (editingDraft.value.tdpMax > next) {
    updateEditing('tdpMax', Math.max(TDP_MIN, next));
  }
}
function onFpsCeilingChange(v: number | string) {
  const next = Number(v);
  if (!editingDraft.value) return;
  editingFpsCeiling.value = next;
  if (next === 0) {
    updateEditing('fpsTarget', 0);
    return;
  }
  // 从「不锁帧(0)」切回具体档位，或当前值超出新上限时，把目标帧数提升到该档位
  const current = editingDraft.value.fpsTarget;
  if (current === 0 || current > next) {
    updateEditing('fpsTarget', Math.max(FPS_TARGET_MIN, next));
  }
}

watch([statusMsg, errMsg], ([status, error]) => {
  if (statusHideTimer !== null) {
    clearTimeout(statusHideTimer);
    statusHideTimer = null;
  }
  if (!status && !error) return;
  statusHideTimer = setTimeout(() => {
    statusMsg.value = '';
    errMsg.value = '';
    statusHideTimer = null;
  }, 10_000);
});

// 滑块拖动超出当前上限时，自动抬升上限下拉（避免上限卡住导致滑块不能再往右拖）
watch(() => editingDraft.value?.tdpMax, (val) => {
  if (val === undefined || val === null) return;
  if (val > editingTdpCeiling.value) {
    editingTdpCeiling.value = smallestCeiling(TDP_CEILINGS, val);
  }
});
watch(() => editingDraft.value?.fpsTarget, (val) => {
  if (val === undefined || val === null) return;
  if (val === 0) {
    editingFpsCeiling.value = 0;
  } else if (val > editingFpsCeiling.value) {
    editingFpsCeiling.value = smallestCeiling(FPS_CEILINGS, val);
  }
});

function modeProfile(side: PowerSide, mode: ScheduleMode): ScheduleProfile {
  return config.value.profiles[side][mode];
}

// 摘要反映当前真正生效的 CPU 控制项：cpuTarget≠无压制 → CPU 浮动值；否则 → CPU 挡位。
function cpuControlLabel(p: ScheduleProfile): string {
  if (p.cpuTarget && p.cpuTarget !== 'none') {
  const t = CPU_FLOAT_OPTS.value.find((o) => o.value === p.cpuTarget);
  return `CPU浮动值 ${t ? t.label : p.cpuTarget}`;
  }
  const cpuOpt = CPU_OPTS.find((o) => o.value === p.cpuPreset);
  return `CPU挡位 ${cpuOpt ? cpuOpt.label : p.cpuPreset}`;
}

function modeDetail(side: PowerSide, mode: ScheduleMode): string {
  const p = modeProfile(side, mode);
  const fps = p.fpsTarget > 0 ? `${p.fpsTarget} FPS` : '不锁帧';
  return `${fps} · ${p.tdpMax}W · ${cpuControlLabel(p)} · 浮动执行${getTdpTarget(p.tdpMax, p.tdpStrategy)}W`;
}

// 自定义模式：取当前游戏某侧档位的详情文字
function customProfileFor(side: PowerSide): ScheduleProfile | null {
  if (!gameExeKey.value) return null;
  return getCustomProfileFor(gameCustomConfig.value, gameExeKey.value, side);
}
function customDetail(side: PowerSide): string {
  const p = customProfileFor(side);
  if (!p) return '未设置自定义档位';
  const fps = p.fpsTarget > 0 ? `${p.fpsTarget} FPS` : '不锁帧';
  return `${fps} · ${p.tdpMax}W · ${cpuControlLabel(p)} · 浮动执行${getTdpTarget(p.tdpMax, p.tdpStrategy)}W`;
}

function cloneProfile(side: PowerSide, mode: ScheduleMode): ScheduleProfile {
  return { ...config.value.profiles[side][mode] };
}

function loadEditingDraft() {
  if (customEditing.value) {
    const side = customEditing.value;
    const p = customProfileFor(side);
    // 若当前游戏尚未设置该侧自定义档，则以当前自动优化 active 档位为初始值
    const fallback = config.value.profiles[side][config.value.active[side]];
    editingDraft.value = p ? { ...p } : { ...fallback };
  } else {
    if (!editing.value) {
      editingDraft.value = null;
      return;
    }
    editingDraft.value = cloneProfile(selectedSide.value, editing.value);
  }
  // 初始化上限下拉：取当前值最近的上限档位
  editingTdpCeiling.value = smallestCeiling(TDP_CEILINGS, editingDraft.value.tdpMax);
  editingFpsCeiling.value = smallestCeiling(FPS_CEILINGS, editingDraft.value.fpsTarget);
}

function selectCustomEditingSide(side: PowerSide) {
  if (!customEditing.value || !POWER_SIDES.includes(side)) return;
  selectedSide.value = side;
  customEditing.value = side;
  loadEditingDraft();
}

async function resetCustomProfile() {
  const side = customEditing.value;
  if (!side || !gameExeKey.value || busy.value) return;
  await flushPendingModeApplies();
  busy.value = true;
  errMsg.value = '';
  try {
    const fallback = config.value.profiles[side][config.value.active[side]];
    const key = gameExeKey.value;
    const existing = gameCustomConfig.value.entries[key];
    const entry: GameCustomProfile = existing
      ? { ...existing, [side]: { ...fallback } }
      : {
          displayName: detectedGameName(game.value) || key,
          ac: { ...fallback },
          dc: { ...fallback },
        };
    gameCustomConfig.value = {
      ...gameCustomConfig.value,
      entries: { ...gameCustomConfig.value.entries, [key]: entry },
    };
    await saveGameCustomConfig(gameCustomConfig.value);
    editingDraft.value = { ...fallback };
    if (config.value.enabled) {
      const e = gameCustomConfig.value.entries[key];
      await applyGameCustomProfiles(e.ac, e.dc);
    }
    statusMsg.value = `自定义 ${side.toUpperCase()} 已恢复为自动优化默认`;
  } catch (e) {
    errMsg.value = '恢复默认失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

function openEditor(side: PowerSide = powerSide.value, mode: ScheduleMode = config.value.active[side]) {
  selectedSide.value = side;
  editing.value = mode;
  loadEditingDraft();
  scrollEditorIntoView();
}

// 编辑自定义游戏模式：只编辑指定侧的 AC/DC 档位（不切换全局档位）；同侧已打开时再次点击关闭
function openCustomEditor(side: PowerSide = powerSide.value) {
  if (customEditing.value === side && editingDraft.value) {
    closeEditor();
    return;
  }
  if (editing.value) closeEditor(); // 两个入口互斥，同一面板只显示一种编辑上下文
  selectedSide.value = side;
  customEditing.value = side;
  loadEditingDraft();
  scrollEditorIntoView();
}

function closeEditor() {
  editing.value = null;
  customEditing.value = null;
  editingDraft.value = null;
}

// 编辑按钮：再点一次关闭（双击开关），不再依赖收回气泡
function toggleEditor() {
  if (busy.value) return;
  if (editing.value) {
    closeEditor();
    return;
  }
  if (customEditing.value) closeEditor(); // 两个入口互斥，先关自定义编辑再开性能组合编辑
  openEditor();
}

function selectEditingSide(rawSide: string | number) {
  const side = rawSide as PowerSide;
  if (!POWER_SIDES.includes(side)) return;
  selectedSide.value = side;
  loadEditingDraft();
}

function selectEditingMode(rawMode: string | number) {
  const mode = rawMode as ScheduleMode;
  if (!MODE_ORDER.includes(mode)) return;
  editing.value = mode;
  loadEditingDraft();
}

function onMouseModeSync(e: Event) {
  mouseOn.value = Boolean((e as CustomEvent<{ on?: boolean }>).detail?.on);
}

function focusPauseButton() {
  nextTick(() => {
    const el = pauseButtonEl.value;
    if (!el || el.disabled) return;
    el.focus({ preventScroll: true });
    el.classList.add('focused');
  });
}

function onSuspendedStateSync(e: Event) {
  paused.value = Boolean((e as CustomEvent<{ suspended?: boolean }>).detail?.suspended);
  focusPauseButton();
}

function onGamepadBack(e: Event) {
  // 配置重制确认弹窗打开时，B 键只负责取消弹窗，不再穿透到编辑面板/页面。
  if (resetConfirmOpen.value) {
    e.preventDefault();
    cancelResetConfirm();
    return;
  }
  if (e.defaultPrevented || (!editing.value && !customEditing.value)) return;
  if (document.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]')) return;
  closeEditor();
  e.preventDefault();
}

function syncMouseStateFromApp() {
  // Native 状态只在程序启动同步一次；之后仅由真实开/关事件更新。
  // 页面重新激活时只读取内存态，不再检测 JoyXoff 进程或 Game Bar 接口。
  mouseOn.value = isMouseModeSuppressed();
}

function updateEditing<K extends keyof ScheduleProfile>(key: K, value: ScheduleProfile[K]) {
  if (!editingDraft.value) return;
  if (!editing.value && !customEditing.value) return;
  editingDraft.value = { ...editingDraft.value, [key]: value };
}

async function drainModeApplyQueue(): Promise<void> {
  if (modeApplyRunning) return;
  modeApplyRunning = true;
  try {
    for (;;) {
      const next = Object.values(pendingModeApply)
        .filter((request): request is ModeApplyRequest => request !== null)
        .sort((a, b) => a.revision - b.revision)[0];
      if (!next) break;
      const { side, mode, revision } = next;
      pendingModeApply[side] = null;
      applyingSide.value = { ...applyingSide.value, [side]: true };
      // 短尾随窗口只用于合并连续操作；界面已即时切换，硬件只应用用户停下后的最后档位。
      await new Promise((resolve) => setTimeout(resolve, MODE_APPLY_SETTLE_MS));
      if (pendingModeApply[side]) continue;
      try {
        // config.value 是 Vue 深层响应式 Proxy，不能直接 structuredClone；
        // 通过桥层规范化生成纯对象快照，并把快照失败也纳入本请求的错误清理范围。
        const snapshot = snapshotPerformanceSchedule(config.value);
        const shouldApply = snapshot.enabled && side === powerSide.value;
        if (shouldApply) {
          await applyPerformanceSchedule(side, mode, snapshot);
          await savePerformanceSchedule(snapshot);
        } else {
          await savePerformanceSchedule(snapshot);
        }
        // 连续切换期间仅让最后一次请求发布完成状态，旧请求完成不覆盖新选择的提示。
        if (revision === modeApplyRevision && config.value.active[side] === mode) {
          statusMsg.value = shouldApply
            ? `${side.toUpperCase()} ${MODE_META[mode].label} 已启用`
            : `${side.toUpperCase()} 已保存 ${MODE_META[mode].label}，切换电源后自动启用`;
          errMsg.value = '';
        }
      } catch (e) {
        if (!(e instanceof SkipApplyError) && revision === modeApplyRevision && config.value.active[side] === mode) {
          errMsg.value = '档位切换失败：' + (e as Error).message;
        }
      } finally {
        if (!pendingModeApply[side]) {
          applyingSide.value = { ...applyingSide.value, [side]: false };
        }
      }
    }
  } finally {
    modeApplyRunning = false;
    // finally 与新请求入队可能同一事件循环交错；再检查一次，保证队列不会遗留。
    if (pendingModeApply.ac || pendingModeApply.dc) void drainModeApplyQueue();
  }
}

function selectMode(side: PowerSide, rawMode: string | number) {
  const mode = rawMode as ScheduleMode;
  if (!MODE_ORDER.includes(mode) || config.value.active[side] === mode) return;
  selectedSide.value = side;
  config.value.active[side] = mode;
  config.value.configured = true;
  errMsg.value = '';
  statusMsg.value = `${side.toUpperCase()} ${MODE_META[mode].label} 正在后台应用…`;
  pendingModeApply[side] = { side, mode, revision: ++modeApplyRevision };
  applyingSide.value = { ...applyingSide.value, [side]: true };
  void drainModeApplyQueue();
}

async function flushPendingModeApplies(): Promise<void> {
  if (!modeApplyRunning && !pendingModeApply.ac && !pendingModeApply.dc) return;
  await drainModeApplyQueue();
  while (modeApplyRunning) {
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
}

// ── 配置重制：自绘确认弹窗（锚定「配置重制」按钮，支持手柄 A 确认 / B 或 X 取消）──
const resetConfirmOpen = ref(false);
const resetConfirmStyle = ref<Record<string, string>>({});
const resetConfirmAbove = ref(false);
const resetTriggerEl = ref<HTMLElement | null>(null);
const resetPanelEl = ref<HTMLElement | null>(null);
const resetCancelEl = ref<HTMLElement | null>(null);
const editorPanelEl = ref<HTMLElement | null>(null);

// 内联编辑面板打开后滚动到可视区域（自定义模式入口在上方，面板在工具行下方）
function scrollEditorIntoView() {
  nextTick(() => {
    editorPanelEl.value?.scrollIntoView({ behavior: 'auto', block: 'nearest' });
    // 编辑面板打开后，焦点必须落在面板内的第一个真实控件；否则旧的“编辑”按钮
    // 仍是 activeElement，手柄第一次下移会先经过顶部工具行。
    const first = editorPanelEl.value?.querySelector<HTMLElement>(
      '.editor-side-tab, .editor-targets [data-gp-dropdown] .dd-trigger, .editor-grid [data-gp-dropdown] .dd-trigger, input, button'
    );
    if (first && !first.hasAttribute('disabled')) {
      document.querySelectorAll('.focused').forEach((n) => n.classList.remove('focused'));
      first.focus({ preventScroll: true });
      first.classList.add('focused');
    }
  });
}

function requestResetProfiles() {
  if (busy.value) return;
  void flushPendingModeApplies();
  const trigger = resetTriggerEl.value;
  if (!trigger) {
    void doResetProfiles();
    return;
  }
  const r = trigger.getBoundingClientRect();
  const style: Record<string, string> = { position: 'fixed' };
  // 垂直：优先弹出在按钮下方，空间不足（接近视口底部）时向上翻转
  const POP_H = 172;
  const spaceBelow = window.innerHeight - r.bottom;
  const above = spaceBelow < POP_H + 8 && r.top > spaceBelow;
  resetConfirmAbove.value = above;
  if (above) {
    style.bottom = window.innerHeight - r.top + 10 + 'px';
    style.top = 'auto';
  } else {
    style.top = r.bottom + 8 + 'px';
    style.bottom = 'auto';
  }
  // 水平：右对齐按钮，防溢出视口左右边界
  const POP_W = 284;
  const left = Math.max(8, Math.min(r.right - POP_W, window.innerWidth - POP_W - 8));
  style.left = left + 'px';
  style.width = POP_W + 'px';
  resetConfirmStyle.value = style;
  resetConfirmOpen.value = true;
  nextTick(() => {
    const cancel = resetCancelEl.value;
    cancel?.focus({ preventScroll: true });
    cancel?.classList.add('focused');
  });
}

function cancelResetConfirm() {
  if (!resetConfirmOpen.value) return;
  resetConfirmOpen.value = false;
  const trigger = resetTriggerEl.value;
  trigger?.focus({ preventScroll: true });
  trigger?.classList.add('focused');
}

function confirmResetProfiles() {
  if (!resetConfirmOpen.value) return;
  resetConfirmOpen.value = false;
  void doResetProfiles();
}

function onResetDocPointer(e: PointerEvent) {
  if (!resetConfirmOpen.value) return;
  const t = e.target as Node;
  if (resetPanelEl.value && resetPanelEl.value.contains(t)) return;
  cancelResetConfirm();
}

async function doResetProfiles() {
  if (busy.value) return;
  await flushPendingModeApplies();
  busy.value = true;
  errMsg.value = '';
  try {
    await rebuildYemanScheme(config.value.enabled ? 'auto' : 'yeman');
    config.value = resetPerformanceScheduleProfiles(config.value);
    await savePerformanceSchedule(config.value);
    editing.value = null;
    if (config.value.enabled) {
      await applyPerformanceSchedule(
        powerSide.value,
        config.value.active[powerSide.value],
        config.value,
      );
    }
    statusMsg.value = 'AC / DC 全部自动优化设置已恢复预设';
  } catch (e) {
    errMsg.value = '配置重制失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// ── 自定义游戏模式：添加 / 删除 ──
async function addCustomMode() {
  if (busy.value || !gameExeKey.value) return;
  await flushPendingModeApplies();
  busy.value = true;
  errMsg.value = '';
  try {
    const key = gameExeKey.value;
    const entry: GameCustomProfile = {
      displayName: detectedGameName(game.value) || key,
      ac: { ...config.value.profiles.ac[config.value.active.ac] },
      dc: { ...config.value.profiles.dc[config.value.active.dc] },
    };
    gameCustomConfig.value = {
      ...gameCustomConfig.value,
      entries: { ...gameCustomConfig.value.entries, [key]: entry },
    };
    await saveGameCustomConfig(gameCustomConfig.value);
    if (config.value.enabled) {
      await applyGameCustomProfiles(entry.ac, entry.dc);
    }
    statusMsg.value = `已为「${entry.displayName}」添加自定义模式`;
  } catch (e) {
    errMsg.value = '添加自定义失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function deleteCustomMode() {
  if (busy.value || !gameExeKey.value) return;
  await flushPendingModeApplies();
  busy.value = true;
  errMsg.value = '';
  try {
    const key = gameExeKey.value;
    const entries = { ...gameCustomConfig.value.entries };
    delete entries[key];
    gameCustomConfig.value = { ...gameCustomConfig.value, entries };
    await saveGameCustomConfig(gameCustomConfig.value);
    customEditing.value = null;
    if (config.value.enabled) {
      const side = powerSide.value;
      await applyPerformanceSchedule(side, config.value.active[side], config.value);
    }
    statusMsg.value = '已删除自定义模式，恢复自动优化';
  } catch (e) {
    errMsg.value = '删除自定义失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// 删除自定义确认弹窗（复用自绘弹窗模式，锚定「删除自定义」按钮）
const customDeleteOpen = ref(false);
const customDeleteStyle = ref<Record<string, string>>({});
const customDeleteAbove = ref(false);
const customDeleteTriggerEl = ref<HTMLElement | null>(null);
const customDeletePanelEl = ref<HTMLElement | null>(null);
const customDeleteCancelEl = ref<HTMLElement | null>(null);

function requestDeleteCustom() {
  if (busy.value || !customActive.value) return;
  const trigger = customDeleteTriggerEl.value;
  if (!trigger) {
    void deleteCustomMode();
    return;
  }
  const r = trigger.getBoundingClientRect();
  const style: Record<string, string> = { position: 'fixed' };
  const POP_H = 172;
  const spaceBelow = window.innerHeight - r.bottom;
  const above = spaceBelow < POP_H + 8 && r.top > spaceBelow;
  customDeleteAbove.value = above;
  if (above) {
    style.bottom = window.innerHeight - r.top + 10 + 'px';
    style.top = 'auto';
  } else {
    style.top = r.bottom + 8 + 'px';
    style.bottom = 'auto';
  }
  const POP_W = 284;
  const left = Math.max(8, Math.min(r.right - POP_W, window.innerWidth - POP_W - 8));
  style.left = left + 'px';
  style.width = POP_W + 'px';
  customDeleteStyle.value = style;
  customDeleteOpen.value = true;
  nextTick(() => {
    const cancel = customDeleteCancelEl.value;
    cancel?.focus({ preventScroll: true });
    cancel?.classList.add('focused');
  });
}

function cancelDeleteCustom() {
  if (!customDeleteOpen.value) return;
  customDeleteOpen.value = false;
  const trigger = customDeleteTriggerEl.value;
  trigger?.focus({ preventScroll: true });
  trigger?.classList.add('focused');
}

function confirmDeleteCustom() {
  if (!customDeleteOpen.value) return;
  customDeleteOpen.value = false;
  void deleteCustomMode();
}

function onCustomDeleteDocPointer(e: PointerEvent) {
  if (!customDeleteOpen.value) return;
  const t = e.target as Node;
  if (customDeletePanelEl.value && customDeletePanelEl.value.contains(t)) return;
  cancelDeleteCustom();
}

async function enterManualMode() {
  if (busy.value) return;
  await flushPendingModeApplies();
  busy.value = true;
  try {
    await disablePerformanceSchedule(config.value);
    config.value.enabled = false;
    config.value.configured = true;
    coreArchitecture.value = null;
    editing.value = null;
    statusMsg.value = '自动优化已关闭，现由 TDP 功耗与 CPU 调度页面控制';
  } catch (e) {
    errMsg.value = '关闭自动优化失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function enableQuickMode() {
  if (busy.value) return;
  await flushPendingModeApplies();
  busy.value = true;
  errMsg.value = '';
  try {
    config.value.enabled = true;
    config.value.configured = true;
    const mode = config.value.active[powerSide.value];
    // 先应用硬件，成功后才持久化 enabled=true（失败时磁盘保持旧值，避免重启误恢复）。
    await applyPerformanceSchedule(powerSide.value, mode, config.value);
    await savePerformanceSchedule(config.value);
    await refreshCoreArchitectureForAuto();
    statusMsg.value = `自动优化已开启：${MODE_META[mode].label}`;
  } catch (e) {
    config.value.enabled = false;
    errMsg.value = '自动优化开启失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function saveEditor() {
  const draft = editingDraft.value;
  if (!draft) return;
  await flushPendingModeApplies();
  busy.value = true;
  errMsg.value = '';
  try {
    if (config.value.enabled) await ensureRememberedYemanSchemeActive();
    if (customEditing.value) {
      const key = gameExeKey.value;
      if (!key) throw new Error('未识别到游戏');
      const side = customEditing.value;
      const existing = gameCustomConfig.value.entries[key];
      // 新建时：被编辑侧写入草稿；未编辑侧沿用「当前自动档位」配置，
      // 避免「编辑 AC 侧却把 DC 也改成同一份值」（2026-08-05 修复）。
      const sideProfileFor = (s: PowerSide): ScheduleProfile => {
        const m = config.value.active[s];
        return config.value.profiles[s][m] ?? { ...draft };
      };
      const entry: GameCustomProfile = existing
        ? { ...existing, [side]: { ...draft } }
        : {
            displayName: detectedGameName(game.value) || key,
            ac: side === 'ac' ? { ...draft } : sideProfileFor('ac'),
            dc: side === 'dc' ? { ...draft } : sideProfileFor('dc'),
          };
      gameCustomConfig.value = {
        ...gameCustomConfig.value,
        entries: { ...gameCustomConfig.value.entries, [key]: entry },
      };
      await saveGameCustomConfig(gameCustomConfig.value);
      if (config.value.enabled && side === powerSide.value) {
        const e = gameCustomConfig.value.entries[key];
        await applyGameCustomProfiles(e.ac, e.dc);
      }
      statusMsg.value = `自定义 ${side.toUpperCase()} 配置已保存`;
    } else {
      const mode = editing.value;
      if (!mode) throw new Error('未选择档位');
      const side = selectedSide.value;
      config.value.profiles[side][mode] = { ...draft };
      // AC/DC 共享同名档位的定义 — 编辑 AC 的「高性能」也同时更新 DC 的「高性能」
      const otherSide: PowerSide = side === 'ac' ? 'dc' : 'ac';
      config.value.profiles[otherSide][mode] = { ...draft };
      if (
        config.value.enabled &&
        side === powerSide.value &&
        config.value.active[side] === mode
      ) {
        // 当前正运行的目标档位：先应用硬件，成功后落盘，失败时配置不写盘。
        await applyPerformanceSchedule(side, mode, config.value);
        await savePerformanceSchedule(config.value);
        statusMsg.value = `${MODE_META[mode].label} 配置已保存并启用`;
      } else {
        await savePerformanceSchedule(config.value);
        statusMsg.value = `${MODE_META[mode].label} 配置已保存`;
      }
    }
    closeEditor();
  } catch (e) {
    errMsg.value = '保存组合失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function refreshStatus() {
  const params = await readPowerParams().catch(() => null);
  const tm = topMon.value;
  manualParams.value = params;
  if (tm) {
    const newSide = tm.ac === 0 ? 'dc' : 'ac';
    if (newSide !== powerSide.value) {
      powerSide.value = newSide;
      // 电源切换：自定义模式重新应用当前侧，避免回退到自动档位
      if (config.value.enabled && customActive.value && gameExeKey.value) {
        const e = gameCustomConfig.value.entries[gameExeKey.value];
        if (e) void applyGameCustomProfiles(e.ac, e.dc).catch(() => {});
      }
    }
  }
}

function syncGame(g: DetectedGame | null) {
  game.value = g;
  if (!g) paused.value = false;
  // 新游戏识别后：若该游戏已有自定义配置，则立即应用其 AC/DC 档位（覆盖自动模式）
  if (g && gameExeKey.value && gameCustomConfig.value.entries[gameExeKey.value] && config.value.enabled) {
    const e = gameCustomConfig.value.entries[gameExeKey.value];
    void applyGameCustomProfiles(e.ac, e.dc).catch(() => {});
  }
  if (g) {
    void hasSuspendedState(g.pid)
      .then((r) => { paused.value = r.suspended; })
      .catch(() => { paused.value = false; });
  }
}

async function openGameTrainer(): Promise<void> {
  if (trainerBusy.value || !game.value) return;
  const gameName = detectedGameName(game.value);
  if (!gameName) {
    errMsg.value = '未识别到真实游戏名，无法搜索修改器';
    return;
  }
  trainerBusy.value = true;
  errMsg.value = '';
  statusMsg.value = `正在连接 GCM，准备下载 ${gameName}`;
  try {
    const result = await openOrSearchGameTrainer(gameName, (progress) => {
      if (progress.state === 'downloading') {
        const elapsed = Number(progress.elapsedSeconds) || 0;
        statusMsg.value = progress.message || `正在下载修改器，已等待 ${elapsed} 秒`;
      } else if (progress.message) {
        statusMsg.value = progress.message;
      }
    }, game.value.path);
    if (result.action === 'opened' && result.trainer?.origin === 'cheat_engine') {
      statusMsg.value = '已打开 Cheat Engine';
    } else if (result.action === 'opened' && result.trainer) {
      statusMsg.value = `已打开${result.trainer.origin === 'fling_main' || result.trainer.origin === 'fling_archive' ? '风灵' : ''}修改器`;
    } else {
      const count = result.search?.results?.length || 0;
      statusMsg.value = result.search?.downloaded
        ? `已下载并打开${result.search.origin === 'fling_main' || result.search.origin === 'fling_archive' ? '风灵' : ''}修改器`
        : count > 0
        ? `GCM 已搜索「${gameName}」，找到 ${count} 个结果并保持后台运行`
        : `GCM 已搜索「${gameName}」，暂未找到修改器`;
    }
  } catch (e) {
    errMsg.value = '游戏修改器操作失败：' + (e as Error).message;
  } finally {
    trainerBusy.value = false;
  }
}

// 暂停/继续合并：只操作当前识别游戏的 PID 及其子进程树。
async function togglePause() {
  if (pauseBusy.value) return;
  pauseBusy.value = true;
  errMsg.value = '';
  try {
    if (paused.value) {
      const r = await resumeGame();
      paused.value = r.failCount > 0;
      statusMsg.value = r.okCount > 0
        ? `已继续 ${r.okCount} 个被冻结的进程`
        : (r.ok ? '游戏已继续' : `继续失败：${r.msgs.join('；') || '没有待恢复的进程'}`);
    } else {
      if (!game.value?.pid) {
        statusMsg.value = '暂停失败：没有可暂停的游戏进程';
        return;
      }
      const r = await suspendGame(game.value.pid, game.value.name || game.value.title || '当前游戏');
      paused.value = r.ok;
      statusMsg.value = r.ok
        ? `已暂停 ${game.value.name || game.value.title || '当前游戏'}`
        : `暂停失败：${r.msgs.join('；') || '没有可暂停的游戏进程'}`;
    }
  } catch (e) {
    errMsg.value = '暂停/继续失败：' + (e as Error).message;
  } finally {
    pauseBusy.value = false;
    focusPauseButton();
  }
}

async function closeCurrentGame() {
  if (!game.value || closeBusy.value) return;
  closeBusy.value = true;
  try {
    const current = game.value;
    const r = await closeGame(current.pid, current.name);
    if (!r.ok) throw new Error(r.msgs.join('；') || '关闭失败');
    statusMsg.value = `已关闭 ${current.name}`;
    await refreshGameStatus();
    // 关闭后加速识别收敛：+1s 起每 1s 刷新一次、持续 5s，
    // 让页面更快从「残留/部分状态」收敛到「未识别游戏」，不必等 5s 陈旧状态回退。
    // 仅新增节奏，不改其他识别/调度逻辑；不阻塞 closeBusy，按钮可立即再点。
    void pollGameAfterClose();
  } catch (e) {
    errMsg.value = '关闭游戏失败：' + (e as Error).message;
  } finally {
    closeBusy.value = false;
  }
}

// 关闭游戏后加速轮询：不改动全局 POLL_MS 与 autofloat 5s 陈旧窗口，
// 仅在此窗口内主动多跑几次识别，让「未识别游戏」更快出现。
async function pollGameAfterClose() {
  for (let i = 1; i <= 5; i++) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    await refreshGameStatus();
  }
}

async function toggleMouse() {
  if (mouseBusy.value) return;
  mouseBusy.value = true;
  mouseNotice.value = '';
  try {
    const result = await toggleMouseMode();
    if (!result.ok) {
      mouseNotice.value = result.error || '模拟鼠标切换失败';
      return;
    }
    mouseOn.value = result.on;
    window.dispatchEvent(new CustomEvent('gp:mouse-mode', {
      detail: { on: result.on, backend: result.backend },
    }));
    statusMsg.value = mouseOn.value ? '模拟鼠标已开启' : '模拟鼠标已关闭';
  } catch (e) {
    mouseNotice.value = '模拟鼠标切换失败：' + (e as Error).message;
  } finally {
    mouseBusy.value = false;
  }
}

onMounted(async () => {
  // 注意：KeepAlive 下 onMounted 之后紧跟 onActivated，监听注册统一放在 onActivated，
  // 避免重复注册（同一 handler 注册两次 → 事件触发两次）。
  config.value = await loadPerformanceSchedule();
  await refreshCoreArchitectureForAuto();
  gameCustomConfig.value = await loadGameCustomConfig();
  await refreshStatus();
  selectedSide.value = powerSide.value;
  syncMouseStateFromApp();
  offFloatUpdate = onFloatUpdate((info) => {
    floatInfo.value = info;
  });
  warningMsg.value = getPerformanceScheduleWarning()?.message ?? '';
  offScheduleWarning = onPerformanceScheduleWarning((warning) => {
    warningMsg.value = warning.message;
  });
  sampleMonitor();
});

function onGamepadPerformanceModeChanged(e: Event): void {
  const detail = (e as CustomEvent<{ side?: PowerSide; mode?: ScheduleMode }>).detail || {};
  if (detail.side && detail.mode && POWER_SIDES.includes(detail.side)) {
    config.value.active[detail.side] = detail.mode;
    statusMsg.value = `${detail.side.toUpperCase()} ${MODE_META[detail.mode].label} 已启用`;
    errMsg.value = '';
  }
  void loadPerformanceSchedule().then((next) => {
    config.value = next;
    if (detail.side) selectedSide.value = detail.side;
    void refreshStatus();
  }).catch(() => {});
}

// 应用级事件监听统一封装：KeepAlive 下失活/激活来回切换时成对注册/移除，
// 避免隐藏页继续处理 gp:mouse-mode / 游戏暂停 / 手柄返回 等事件改状态（2026-08-05 修复）。
function registerDocListeners(): void {
  window.addEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
  window.addEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  window.addEventListener('gamepad:performance-mode-changed', onGamepadPerformanceModeChanged);
  document.addEventListener('pointerdown', onResetDocPointer);
  document.addEventListener('pointerdown', onCustomDeleteDocPointer);
}
function unregisterDocListeners(): void {
  window.removeEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
  window.removeEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener('gamepad:performance-mode-changed', onGamepadPerformanceModeChanged);
  document.removeEventListener('pointerdown', onResetDocPointer);
  document.removeEventListener('pointerdown', onCustomDeleteDocPointer);
}

onActivated(async () => {
  await loadUiSettings();
  showMonitor.value = getUiSetting('scheduleMonitor');
  registerDocListeners(); // 失活时已移除，重新激活后必须恢复监听
  config.value = await loadPerformanceSchedule();
  await refreshCoreArchitectureForAuto();
  selectedSide.value = powerSide.value;
  await refreshStatus();
  syncMouseStateFromApp();
  if (!unsubGame) unsubGame = subscribeGameStatus(syncGame);
  if (!stopTopMon) {
    stopTopMon = registerScheduledTask('performance-schedule-status', 2000, refreshStatus, {
      pauseWhenHidden: true,
      runImmediately: true,
    });
  }
  if (!stopMonitorChart) {
    stopMonitorChart = registerScheduledTask('performance-schedule-chart', 500, () => {
      floatInfo.value = getFloatInfo();
      sampleMonitor();
    }, {
      pauseWhenHidden: true,
      runImmediately: true,
    });
  }
  if (!gameCustomConfig.value.entries) gameCustomConfig.value = await loadGameCustomConfig();
});

onDeactivated(() => {
  unregisterDocListeners(); // 隐藏页不再接收应用级事件
  unsubGame?.();
  unsubGame = null;
  stopTopMon?.();
  stopTopMon = null;
  stopMonitorChart?.();
  stopMonitorChart = null;
  editing.value = null;
  customEditing.value = null;
  if (resetConfirmOpen.value) resetConfirmOpen.value = false;
  if (customDeleteOpen.value) customDeleteOpen.value = false;
});

onUnmounted(() => {
  unregisterDocListeners();
  unsubGame?.();
  stopTopMon?.();
  stopMonitorChart?.();
  offFloatUpdate?.();
  offScheduleWarning?.();
  window.removeEventListener('gp:mouse-mode', onMouseModeSync as EventListener);
  window.removeEventListener(QUICKAPP_SUSPENDED_EVENT, onSuspendedStateSync as EventListener);
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
  window.removeEventListener('gamepad:performance-mode-changed', onGamepadPerformanceModeChanged);
  document.removeEventListener('pointerdown', onResetDocPointer);
  document.removeEventListener('pointerdown', onCustomDeleteDocPointer);
  if (statusHideTimer !== null) clearTimeout(statusHideTimer);
});
</script>

<template>
  <section class="schedule-view">
    <header class="page-head">
      <div class="page-title-block">
        <h1><AppIcon name="gamepad" />{{ gameName }}</h1>
        <p>预计续航 {{ remainText }}</p>
        <div v-if="statusMsg" class="head-notice ok">{{ statusMsg }}</div>
        <div v-if="warningMsg" class="head-notice warning">{{ warningMsg }}</div>
        <div v-if="errMsg" class="head-notice error">{{ errMsg }}</div>
      </div>
      <span v-if="customActive" class="mode-label">自定义模式</span>
    </header>

    <div class="hero card">
      <div class="hero-values">
        <div class="hero-main">
          <span class="hero-label">当前功耗</span>
          <strong>{{ powerText }}</strong>
        </div>
        <div class="hero-main execution-power">
          <span class="hero-label">执行瓦数</span>
          <strong>{{ executionPowerText }}</strong>
        </div>
        <div class="hero-stat">
          <span>目标 FPS</span>
          <b>{{ targetFpsText }}</b>
        </div>
      </div>
      <div class="game-actions" data-gp-group="schedule-game-actions">
        <button ref="pauseButtonEl" type="button" data-gp-group="schedule-game-actions" :disabled="pauseBusy" @click="togglePause">
          <AppIcon :name="paused ? 'play' : 'pause'" />
          {{ paused ? '继续游戏' : '暂停游戏' }}
        </button>
        <button type="button" data-gp-group="schedule-game-actions" class="danger" :disabled="!game || closeBusy" @click="closeCurrentGame">
          <AppIcon name="close" />关闭游戏
        </button>
        <div class="mouse-action-anchor">
          <button type="button" data-gp-group="schedule-game-actions" :class="{ active: mouseOn }" :disabled="mouseBusy" @click="toggleMouse">
            <AppIcon name="mouse" />{{ mouseOn ? '模拟鼠标已开启' : '模拟鼠标已关闭' }}
          </button>
          <Transition name="mouse-pop">
            <div v-if="mouseNotice" class="mouse-action-popover" role="alert">
              <AppIcon name="warning" />
              <span>{{ mouseNotice }}</span>
              <button type="button" aria-label="关闭提示" @click.stop="mouseNotice = ''">×</button>
            </div>
          </Transition>
        </div>
      </div>
    </div>

    <div v-if="showMonitor" class="monitor-chart card">
      <div class="chart-head">
        <div class="section-title">监控</div>
        <div class="chart-live" :class="{ on: floatInfo.enabled }">
          {{ floatInfo.enabled ? '优化运行中' : '优化未开启' }}
        </div>
      </div>

      <div class="monitor-tiles">
        <div v-for="t in monitorTiles" :key="t.key" class="m-tile">
          <div class="m-meta">
            <div class="m-label">{{ t.label }}</div>
            <div class="m-value">{{ t.value }}<small v-if="t.unit">{{ t.unit }}</small></div>
          </div>
        </div>
      </div>
    </div>

    <!-- 自动优化：仅自动模式（config.enabled）显示；手动模式隐藏，手动模式由 TDP 功耗 / CPU 调度页面直接控制 -->
    <div v-if="config.enabled" class="schedule-card card">
      <div class="section-head">
        <div class="section-title">自动优化</div>
        <div class="current-badge" :class="powerSide">当前 {{ powerSide.toUpperCase() }}</div>
      </div>

      <div class="power-mode-list">
        <div
          v-for="side in POWER_SIDES"
          :key="side"
          class="power-mode-row"
          :class="[{ current: powerSide === side }, side]"
        >
          <AppIcon :name="side === 'ac' ? 'plug' : 'battery'" class="side-icon" :aria-label="side === 'ac' ? '交流电' : '电池'" />
          <div class="side-name">
            <strong>{{ side.toUpperCase() }}</strong>
            <small>{{ side === 'ac' ? '交流电' : '电池' }}</small>
          </div>
          <div class="mode-picker">
            <Dropdown
              :model-value="config.active[side]"
              :options="modeOptionsFor(side)"
              :disabled="busy"
              :color="side === 'dc' ? 'dc' : 'accent'"
              :aria-label="`${side.toUpperCase()} 性能档位`"
              @update:model-value="selectMode(side, $event)"
            />
            <small>{{ modeDetail(side, config.active[side]) }}</small>
          </div>
        </div>
      </div>
    </div>

    <!-- 自定义游戏模式：基于当前识别的游戏 exe，创建/编辑/删除一套专属 AC/DC 档位（覆盖自动模式）；自动/手动模式均显示 -->
    <div v-if="game && gameExeKey" class="custom-card card">
      <div class="section-head">
        <div class="section-title">自定义模式</div>
        <!-- 自定义模式编辑/保存/应用入口：自动/手动模式均可用；手动模式仅停止自动套用，不限制编辑 -->
        <div class="custom-actions" data-gp-group="custom-actions">
          <button
            type="button"
            class="tool-mode-btn trainer-button"
            :disabled="busy || trainerBusy"
            data-gp-group="custom-actions"
            @click="openGameTrainer"
          >
            <AppIcon name="play" />{{ trainerBusy ? '处理中…' : '游戏修改器' }}
          </button>
          <button
            v-if="!customActive"
            type="button"
            class="tool-mode-btn"
            :disabled="busy"
            data-gp-group="custom-actions"
            @click="addCustomMode"
          >
            <AppIcon name="plus" />添加自定义
          </button>
          <template v-else>
            <button
              type="button"
              class="tool-mode-btn"
              :class="{ active: !!customEditing }"
              :disabled="busy"
              data-gp-group="custom-actions"
              @click="openCustomEditor()"
            >
              <AppIcon name="edit" />编辑自定义
            </button>
            <button
              ref="customDeleteTriggerEl"
              type="button"
              class="tool-mode-btn danger"
              :disabled="busy"
              data-gp-group="custom-actions"
              @click="requestDeleteCustom"
            >
              <AppIcon name="close" />删除自定义
            </button>
          </template>
        </div>
      </div>

      <div class="custom-body">
        <div class="custom-game">
          <AppIcon name="gamepad" />
          <span>当前游戏：<b>{{ gameCustomConfig.entries[gameExeKey]?.displayName || gameExeKey }}</b></span>
        </div>

        <div v-if="customActive" class="power-mode-list">
          <button
            v-for="side in POWER_SIDES"
            :key="side"
            type="button"
            class="power-mode-row custom-row"
            :class="[{ current: powerSide === side }, side]"
            :disabled="busy"
            data-gp-group="custom-rows"
            @click="openCustomEditor(side)"
          >
            <AppIcon :name="side === 'ac' ? 'plug' : 'battery'" class="side-icon" :aria-label="side === 'ac' ? '交流电' : '电池'" />
            <div class="side-name">
              <strong>{{ side.toUpperCase() }}</strong>
              <small>{{ side === 'ac' ? '交流电' : '电池' }}</small>
            </div>
            <div class="mode-picker">
              <small>{{ customDetail(side) }}</small>
            </div>
          </button>
        </div>
      </div>
    </div>

    <div class="schedule-tools card">
      <div class="tool-actions" data-gp-group="schedule-tools">
        <!-- 自动模式：切换为手动模式在最前 -->
        <button v-if="config.enabled" type="button" data-gp-group="schedule-tools" class="tool-mode-btn" :disabled="busy" @click="enterManualMode">
          <AppIcon name="settings" />切换为手动模式
        </button>
        <!-- 手动模式：切换为自动模式在最前 -->
        <button v-else type="button" data-gp-group="schedule-tools" class="tool-mode-btn" :disabled="busy" @click="enableQuickMode">
          <AppIcon name="rocket" />切换为自动模式
        </button>
        <!-- 编辑：再点一次关闭（双击开关）；仅自动模式下可编辑性能组合，手动模式隐藏（手动模式由 TDP 功耗 / CPU 调度页面直接控制） -->
        <button v-if="config.enabled" type="button" data-gp-group="schedule-tools" :class="{ active: !!editing }" :disabled="busy" @click="toggleEditor">
          <AppIcon name="edit" />编辑性能组合
        </button>
        <!-- 监控：状态按钮，点击切换 -->
        <button type="button" data-gp-group="schedule-tools" class="tool-monitor-btn" :class="{ active: showMonitor }" :disabled="busy" @click="showMonitor = !showMonitor">
          <AppIcon name="monitor" />{{ showMonitor ? '监控已打开' : '监控已关闭' }}
        </button>
      </div>
    </div>

    <!-- 编辑面板：内联展开在工具行下方（编辑性能组合 / 自定义模式共用同一面板） -->
    <div v-if="editing || customEditing" ref="editorPanelEl" class="editor-card card">
      <div class="section-head editor-head">
        <div>
          <div class="section-title">
            {{ customEditing ? '编辑自定义档位' : '编辑性能组合' }}
          </div>
          <div class="section-sub">
            {{ customEditing ? '仅对当前游戏生效，覆盖自动优化档位' : '可在这里切换 AC / DC 与任意档位' }}
          </div>
        </div>
        <div v-if="customEditing" class="editor-side-tabs" data-gp-group="custom-side-tabs">
          <button
            v-for="side in POWER_SIDES"
            :key="side"
            type="button"
            class="editor-side-tab"
            :class="[{ active: selectedSide === side }, side]"
            :data-gp-group="'custom-side-tabs'"
            @click="selectCustomEditingSide(side)"
          >
            {{ side.toUpperCase() }}
          </button>
        </div>
      </div>

      <div class="editor-targets">
        <label v-if="!customEditing">
          <span>供电配置</span>
          <Dropdown
            :model-value="selectedSide"
            :options="EDIT_SIDE_OPTIONS"
            :color="sideColor"
            :disabled="busy"
            aria-label="选择要编辑的供电配置"
            @update:model-value="selectEditingSide"
          />
        </label>
        <label v-if="!customEditing">
          <span>编辑档位</span>
          <Dropdown
            :model-value="editing"
            :options="editModeOptions"
            :color="sideColor"
            :disabled="busy"
            aria-label="选择要编辑的性能档位"
            @update:model-value="selectEditingMode"
          />
        </label>
        <label v-else class="editor-target-game">
          <span>当前游戏</span>
          <div class="editor-game-name">{{ gameCustomConfig.entries[gameExeKey]?.displayName || gameExeKey }}</div>
        </label>
      </div>

      <div v-if="editingDraft" class="editor-grid">
        <label v-if="isHeterogeneousCpu">
          <span>大小核心调度</span>
          <Dropdown
            :model-value="editingDraft.coreMode"
            :options="CORE_MODE_OPTS"
            :color="sideColor"
            :disabled="busy"
            @update:model-value="updateEditing('coreMode', $event as CoreMode)"
          />
        </label>
        <label>
          <span>CPU 浮动值</span>
          <Dropdown
            :model-value="editingDraft.cpuTarget"
            :options="CPU_FLOAT_OPTS"
            :color="sideColor"
            :disabled="busy"
            @update:model-value="updateEditing('cpuTarget', $event as FloatProfile)"
          />
        </label>
        <label v-if="editingDraft.cpuTarget === 'none'">
          <span>CPU 挡位</span>
          <Dropdown
            :model-value="editingDraft.cpuPreset"
            :options="CPU_OPTS"
            :color="sideColor"
            :disabled="busy"
            @update:model-value="updateEditing('cpuPreset', $event as CpuPreset)"
          />
        </label>
        <label>
          <span>TDP 浮动幅度</span>
          <Dropdown
            :model-value="editingDraft.tdpStrategy"
            :options="TDP_STRATEGY_OPTS"
            :color="sideColor"
            :disabled="busy"
            @update:model-value="updateEditing('tdpStrategy', $event as TdpFloatStrategy)"
          />
        </label>
      </div>

      <template v-if="editingDraft">
        <div class="editor-combo">
          <Slider
            :model-value="editingDraft.tdpMax"
            :min="TDP_MIN"
            :max="tdpCeiling"
            :step="1"
            label="TDP 最大值"
            unit="W"
            :color="sideColor"
            :disabled="busy"
            @update:model-value="updateEditing('tdpMax', $event)"
          />
          <Dropdown
            :model-value="tdpCeiling"
            :options="tdpCeilingOpts"
            :color="sideColor"
            :disabled="busy"
            width="104px"
            aria-label="TDP 最大值上限"
            @update:model-value="onTdpCeilingChange"
          />
        </div>
        <div class="editor-combo">
          <Slider
            :model-value="editingDraft.fpsTarget"
            :min="FPS_TARGET_MIN"
            :max="fpsCeiling > 0 ? fpsCeiling : FPS_TARGET_MIN"
            :step="5"
            label="帧数目标"
            :unit="editingDraft.fpsTarget === 0 ? undefined : 'FPS'"
            :value-text="editingDraft.fpsTarget === 0 ? '不锁帧' : undefined"
            :color="sideColor"
            :disabled="busy || editingDraft.fpsTarget === 0"
            @update:model-value="updateEditing('fpsTarget', $event)"
          />
          <Dropdown
            :model-value="fpsCeiling"
            :options="fpsCeilingOpts"
            :color="sideColor"
            :disabled="busy"
            width="104px"
            aria-label="帧数目标上限"
            @update:model-value="onFpsCeilingChange"
          />
        </div>
      </template>
      <div class="editor-actions">
        <!-- 配置重制：编辑面板左下角，确认弹窗仍锚定此按钮弹出 -->
        <button
          v-if="!customEditing"
          ref="resetTriggerEl"
          type="button"
          class="editor-reset"
          :disabled="busy"
          @click="requestResetProfiles"
        >
          <AppIcon name="refresh" />配置重制
        </button>
        <button
          v-else
          type="button"
          class="editor-reset"
          :disabled="busy"
          @click="resetCustomProfile"
        >
          <AppIcon name="refresh" />恢复默认
        </button>
        <div class="editor-actions-right" data-gp-group="schedule-editor-actions">
          <button type="button" data-gp-group="schedule-editor-actions" class="primary" :disabled="busy" @click="saveEditor">保存组合</button>
          <button type="button" data-gp-group="schedule-editor-actions" class="editor-cancel" @click="closeEditor">
            取消 <small class="gp-hint">B</small>
          </button>
        </div>
      </div>
    </div>

    <!-- 配置重制确认：自绘浮层（锚定「配置重制」按钮；手柄 A=确认 B=取消，方向键在按钮间移动） -->
    <Teleport to="body">
      <Transition name="rc-pop">
        <div
          v-if="resetConfirmOpen"
          ref="resetPanelEl"
          class="reset-confirm"
          :class="{ above: resetConfirmAbove }"
          :style="resetConfirmStyle"
          role="alertdialog"
          aria-modal="true"
          aria-label="确认配置重制"
          data-gp-modal
          @pointerdown.stop
          @keydown.esc.prevent="cancelResetConfirm"
        >
          <div class="rc-title"><AppIcon name="warning" />确认配置重制</div>
          <p class="rc-desc">将重制 AC / DC 下节能、平衡、高性能、极致性能的 CPU、大小核心、TDP、帧率和浮动设置。此操作不可撤销，是否继续？</p>
          <div class="rc-actions" data-gp-group="schedule-reset-confirm">
            <button ref="resetCancelEl" type="button" data-gp-group="schedule-reset-confirm" @click="cancelResetConfirm">取消</button>
            <button type="button" data-gp-group="schedule-reset-confirm" class="danger" @click="confirmResetProfiles">确认重制</button>
          </div>
        </div>
      </Transition>
    </Teleport>

    <!-- 删除自定义确认：自绘浮层（锚定「删除自定义」按钮） -->
    <Teleport to="body">
      <Transition name="rc-pop">
        <div
          v-if="customDeleteOpen"
          ref="customDeletePanelEl"
          class="reset-confirm"
          :class="{ above: customDeleteAbove }"
          :style="customDeleteStyle"
          role="alertdialog"
          aria-modal="true"
          aria-label="确认删除自定义模式"
          data-gp-modal
          @pointerdown.stop
          @keydown.esc.prevent="cancelDeleteCustom"
        >
          <div class="rc-title"><AppIcon name="warning" />确认删除自定义模式</div>
          <p class="rc-desc">将删除「{{ gameCustomConfig.entries[gameExeKey]?.displayName || gameExeKey }}」的专属性能档位，恢复为自动优化。此操作不可撤销，是否继续？</p>
          <div class="rc-actions" data-gp-group="schedule-custom-delete">
            <button ref="customDeleteCancelEl" type="button" data-gp-group="schedule-custom-delete" @click="cancelDeleteCustom">取消</button>
            <button type="button" data-gp-group="schedule-custom-delete" class="danger" @click="confirmDeleteCustom">确认删除</button>
          </div>
        </div>
      </Transition>
    </Teleport>
  </section>
</template>

<style scoped>
.schedule-view {
  padding: 0 12px 18px;
  width: 100%;
}
.page-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin: 0 0 10px;
}
.page-title-block {
  min-width: 0;
  flex: 1;
}
.page-head h1 {
  margin: 0;
  display: flex;
  align-items: center;
  gap: 7px;
  max-width: 470px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 18px;
  line-height: 1.2;
}
.page-head h1 :deep(svg) { color: var(--accent); }
.page-head p {
  margin: 5px 0 0;
  color: var(--text-dim);
  font-size: 11px;
}
.head-notice {
  display: inline-block;
  max-width: 100%;
  margin-top: 5px;
  padding: 4px 8px;
  border-radius: 7px;
  font-size: 10px;
  line-height: 1.25;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.head-notice.ok {
  background: color-mix(in srgb, var(--ok) 12%, transparent);
  color: var(--ok);
}
.head-notice.warning {
  background: color-mix(in srgb, #f5b942 14%, transparent);
  color: #f5b942;
}
.head-notice.error {
  background: color-mix(in srgb, var(--danger) 12%, transparent);
  color: var(--danger);
}
.current-badge {
  border-radius: 999px;
  padding: 5px 9px;
  font-size: 10px;
  font-weight: 700;
  background: color-mix(in srgb, var(--accent) 16%, var(--bg-input));
  color: var(--accent);
}
.current-badge.dc {
  background: color-mix(in srgb, var(--dc-accent) 16%, var(--bg-input));
  color: var(--dc-accent);
}
.current-badge.active {
  background: color-mix(in srgb, var(--ok) 16%, var(--bg-input));
  color: var(--ok);
}
/* 顶部标题栏：自定义模式标识 */
.mode-label {
  border-radius: 999px;
  padding: 5px 10px;
  font-size: 11px;
  font-weight: 700;
  background: color-mix(in srgb, var(--ok) 18%, var(--bg-input));
  color: var(--ok);
}
.hero { padding: 13px 14px; }
.game-line, .section-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}
.game-name {
  min-width: 0;
  font-size: 15px;
  font-weight: 700;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.edit-btn {
  display: none;
}
.hero-values {
  display: grid;
  grid-template-columns: 1.2fr 1.2fr .8fr;
  gap: 7px;
  margin: 0 0 12px;
}
.hero-main, .hero-stat {
  min-width: 0;
  min-height: 58px;
  border-radius: 9px;
  background: var(--bg-input);
  display: flex;
  flex-direction: column;
  justify-content: center;
  padding: 8px 10px;
}
.hero-label, .hero-stat span {
  font-size: 9px;
  color: var(--text-dim);
  margin-bottom: 3px;
}
.hero-main strong {
  color: var(--accent);
  font-size: 27px;
  line-height: 1;
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}
.execution-power strong { color: var(--text); }
.hero-stat b {
  font-size: 18px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-variant-numeric: tabular-nums;
}
.game-actions {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 7px;
}
.mouse-action-anchor {
  position: relative;
  min-width: 0;
}
.mouse-action-anchor > button {
  width: 100%;
}
.mouse-action-popover {
  position: absolute;
  z-index: 35;
  top: calc(100% + 7px);
  right: 0;
  width: 260px;
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  gap: 7px;
  padding: 8px 9px;
  border: 1px solid color-mix(in srgb, #f5b93d 58%, transparent);
  border-radius: 9px;
  background: color-mix(in srgb, var(--bg-card) 94%, #17120a);
  color: #ffd47a;
  box-shadow: 0 8px 22px rgba(0, 0, 0, 0.42);
  font-size: 10px;
  line-height: 1.35;
}
.game-actions .mouse-action-popover > :deep(svg) {
  width: 15px;
  height: 15px;
}
.game-actions .mouse-action-popover > button {
  width: 22px;
  min-height: 22px;
  height: 22px;
  padding: 0;
  border: 0;
  border-radius: 6px;
  background: rgba(255, 255, 255, 0.08);
  color: inherit;
}
.mouse-pop-enter-active,
.mouse-pop-leave-active { transition: opacity 0.12s, transform 0.12s; }
.mouse-pop-enter-from,
.mouse-pop-leave-to { opacity: 0; transform: translateY(-3px); }
.game-actions button, .editor-actions button, .tool-actions button, .custom-actions button {
  min-height: 34px;
  border: 1px solid rgba(255,255,255,.06);
  border-radius: 8px;
  background: var(--bg-input);
  color: var(--text);
  cursor: pointer;
}
.game-actions button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  font-size: 11px;
}
.game-actions button :deep(svg) { width: 14px; height: 14px; }
.game-actions button.active { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, transparent); }
.game-actions button.danger { color: var(--danger); }
button:disabled { opacity: .42; cursor: default; }
.section-title { font-size: 13px; font-weight: 700; }
.section-sub { color: var(--text-dim); font-size: 10px; margin-top: 3px; }
.power-mode-list {
  display: grid;
  gap: 8px;
  margin-top: 11px;
}
.power-mode-row {
  display: grid;
  grid-template-columns: 22px 52px minmax(0, 1fr);
  align-items: center;
  gap: 10px;
  padding: 10px 11px;
  border-radius: 9px;
  background: var(--bg-input);
  border: 1px solid rgba(255,255,255,.055);
}
.side-icon {
  width: 14px;
  height: 14px;
  justify-self: center;
  color: var(--text-dim);
}
.power-mode-row.current.ac .side-icon { color: var(--accent); }
.power-mode-row.current.dc .side-icon { color: var(--dc-accent); }
.power-mode-row.current.ac { border-color: color-mix(in srgb, var(--accent) 42%, transparent); }
.power-mode-row.current.dc { border-color: color-mix(in srgb, var(--dc-accent) 42%, transparent); }
.side-name {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.side-name strong { font-size: 13px; }
.side-name small, .mode-picker > small {
  color: var(--text-dim);
  font-size: 11px;
  white-space: nowrap;
}
.mode-picker {
  min-width: 0;
  display: grid;
  gap: 4px;
}
.mode-picker > small {
  overflow: hidden;
  text-overflow: ellipsis;
}
.mode-picker > small.applying {
  color: var(--accent);
  font-weight: 700;
}
.power-mode-row.dc .mode-picker > small.applying { color: var(--dc-accent); }
.schedule-tools {
  padding: 10px 12px;
}
/* ── 自定义游戏模式卡片 ── */
.custom-card { padding: 11px 12px; }
.custom-card .section-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.custom-body { margin-top: 8px; display: grid; gap: 7px; }
.custom-game {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--text);
}
.custom-game :deep(svg) { width: 14px; height: 14px; color: var(--text-dim); }
.custom-game b { color: var(--text); font-weight: 700; }
/* 自定义 AC/DC 行：点击进入编辑。整行内容单排水平居中，减少留白。 */
.custom-row {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 7px 10px;
  width: 100%;
  text-align: center;
  cursor: pointer;
  color: var(--text);
  font: inherit;
  background: var(--bg-input);
  border: 1px solid rgba(255,255,255,.055);
}
.custom-row .side-name {
  display: inline-flex;
  align-items: baseline;
  gap: 5px;
  flex: 0 0 auto;
}
.custom-row .side-name strong { font-size: 13px; }
.custom-row .side-name small { font-size: 11px; color: var(--text-dim); }
.custom-row .mode-picker {
  min-width: 0;
  flex: 0 1 auto;
  text-align: center;
}
.custom-row .mode-picker > small {
  text-align: center;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  display: block;
}
.custom-row .side-icon {
  flex: 0 0 auto;
  width: 14px;
  height: 14px;
  color: var(--text-dim);
}
.custom-row.ac .side-name strong,
.custom-row.ac .mode-picker > small { color: var(--text); }
.custom-row.dc .side-name strong,
.custom-row.dc .mode-picker > small { color: var(--text); }
.custom-row.current.ac .side-name strong,
.custom-row.current.ac .mode-picker > small { color: var(--accent); }
.custom-row.current.dc .side-name strong,
.custom-row.current.dc .mode-picker > small { color: var(--dc-accent); }
.custom-row.current.ac { border-color: color-mix(in srgb, var(--accent) 42%, transparent); }
.custom-row.current.dc { border-color: color-mix(in srgb, var(--dc-accent) 42%, transparent); }
.custom-row.current.ac .side-icon { color: var(--accent); }
.custom-row.current.dc .side-icon { color: var(--dc-accent); }

.custom-actions {
  display: flex;
  align-items: center;
  align-self: center;
  gap: 7px;
  flex: 0 0 auto;
}
.custom-actions button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  text-align: center;
  gap: 5px;
  min-height: 38px;
  padding: 0 8px;
  font-size: 11px;
  font-weight: 700;
}
.custom-actions button :deep(svg) { width: 13px; height: 13px; display: block; flex-shrink: 0; }
.custom-actions button.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
}
.custom-actions button.danger { color: var(--danger); }
.custom-actions .trainer-button { color: var(--accent); }
button:disabled { opacity: .42; cursor: default; }
.tool-actions {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
}
/* 模式切换按钮：与同行按钮等高（参考最左侧「切换为手动模式」尺寸，统一为 38px） */
.tool-actions .tool-mode-btn {
  font-weight: 700;
}
.tool-actions .tool-mode-btn :deep(svg) { width: 15px; height: 15px; }
/* 监控状态按钮（已打开/已关闭） */
.tool-actions .tool-monitor-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  min-height: 38px;
  padding: 0 8px;
  font-size: 11px;
}
.tool-actions .tool-monitor-btn :deep(svg) { width: 13px; height: 13px; }
.tool-actions button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  flex: 1 1 0;
  min-width: 0;
  min-height: 38px;
  padding: 0 8px;
  font-size: 11px;
}
.tool-actions button :deep(svg) { width: 13px; height: 13px; }
.tool-actions button.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
}
.tool-actions .primary {
  background: var(--accent);
  color: #06121d;
  border-color: var(--accent);
  font-weight: 700;
}
/* 编辑面板：内联展开在工具行下方（不再是浮层弹窗） */
.editor-card {
  width: 100%;
  margin-top: 10px;
  padding: 16px 18px;
  border: 1px solid color-mix(in srgb, var(--accent) 35%, transparent);
}
.editor-head { align-items: center; gap: 12px; }
.editor-head .section-title { font-size: 16px; }
.editor-head .section-sub { font-size: 12px; margin-top: 4px; }
.editor-side-tabs {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  padding: 4px;
  border-radius: 10px;
  background: color-mix(in srgb, var(--bg-input) 80%, transparent);
  border: 1px solid rgba(255,255,255,.06);
}
.editor-side-tab {
  min-width: 52px;
  min-height: 38px;
  padding: 0 14px;
  border-radius: 8px;
  border: 1px solid transparent;
  background: transparent;
  color: var(--text-dim);
  font-size: 14px;
  font-weight: 700;
  cursor: pointer;
  transition: background 0.12s, color 0.12s, border-color 0.12s;
}
.editor-side-tab.active.ac {
  background: color-mix(in srgb, var(--accent) 14%, var(--bg-input));
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
}
.editor-side-tab.active.dc {
  background: color-mix(in srgb, var(--dc-accent) 14%, var(--bg-input));
  color: var(--dc-accent);
  border-color: color-mix(in srgb, var(--dc-accent) 45%, transparent);
}
.editor-targets {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
  margin: 14px 0 10px;
  padding: 11px;
  border-radius: 12px;
  background: color-mix(in srgb, var(--bg-input) 90%, transparent);
}
.editor-game-name {
  display: flex;
  align-items: center;
  min-height: 40px;
  padding: 9px 12px;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  border: 1px solid #2a3342;
  color: var(--text);
  font-size: 15px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.editor-targets label > span,
.editor-grid label > span {
  display: block;
  color: var(--text-dim);
  font-size: 12px;
  margin-bottom: 6px;
  white-space: nowrap;
}
.editor-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
  margin: 14px 0 11px;
}
.editor-combo {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: end;
  gap: 10px;
  margin: 10px 0;
}
.editor-combo:first-of-type { margin-top: 14px; }
.editor-combo:last-of-type { margin-bottom: 11px; }
.editor-card :deep(.dd-trigger) {
  min-height: 34px;
  padding: 7px 10px;
  font-size: 13px;
}
.editor-card :deep(.slider) { padding: 5px 0; }
.editor-card :deep(.slider-label) { font-size: 13px; }
.editor-card :deep(.slider-val) { font-size: 14px; }
.editor-card :deep(.slider-track-wrap) { height: 22px; }
.editor-card :deep(input[type='range']) { height: 6px; }
.editor-card :deep(.slider-track-bg),
.editor-card :deep(.slider-fill) { height: 6px; }
.editor-card :deep(input[type='range']::-webkit-slider-thumb) {
  width: 16px;
  height: 16px;
}
.editor-card :deep(input[type='range']::-moz-range-thumb) {
  width: 16px;
  height: 16px;
}
.editor-actions {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 7px;
  margin-top: 9px;
}
.editor-actions-right {
  display: flex;
  align-items: center;
  gap: 7px;
}
.editor-actions button {
  min-width: auto;
  min-height: 32px;
  padding: 0 10px;
  font-size: 11px;
}
.editor-actions .editor-reset {
  min-width: auto;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
}
.editor-actions .editor-reset :deep(svg) { width: 13px; height: 13px; }
.editor-actions .primary { background: var(--accent); color: #06121d; border-color: var(--accent); font-weight: 700; }
/* 取消按钮：采用 DC（琥珀黄）语义色，与主操作「保存组合」的蓝色区分；非统一灰色按钮 */
.editor-actions .editor-cancel {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  color: var(--dc-accent);
  border-color: color-mix(in srgb, var(--dc-accent) 45%, transparent);
  background: color-mix(in srgb, var(--dc-accent) 10%, var(--bg-input));
}
.editor-actions .editor-cancel .gp-hint {
  color: var(--dc-accent);
  border-color: color-mix(in srgb, var(--dc-accent) 55%, transparent);
}
.gp-hint {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 15px;
  height: 15px;
  padding: 0 3px;
  border: 1px solid color-mix(in srgb, var(--text-dim) 55%, transparent);
  border-radius: 4px;
  font-size: 9px;
  line-height: 1;
  color: var(--text-dim);
}
.monitor-chart { padding: 12px 14px; }
.chart-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 10px;
}
.chart-live {
  flex: 0 0 auto;
  border-radius: 999px;
  padding: 4px 8px;
  background: var(--bg-input);
  color: var(--text-dim);
  font-size: 9px;
}
.chart-live.on {
  color: var(--ok);
  background: color-mix(in srgb, var(--ok) 12%, var(--bg-input));
}
.monitor-tiles {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 6px;
  margin: 10px 0 8px;
}
.m-tile {
  min-height: 38px;
  border-radius: 7px;
  padding: 6px 8px;
  background: var(--bg-input);
  display: flex;
  align-items: center;
}
.m-meta { min-width: 0; }
.m-label {
  display: flex; align-items: center; gap: 4px;
  font-size: 10px; color: var(--text-dim); line-height: 1.1; white-space: nowrap;
}
.m-value {
  margin-top: 2px;
  font-size: 15px;
  font-weight: 700;
  line-height: 1;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}
.m-value small {
  font-size: 9px;
  font-weight: 600;
  margin-left: 2px;
  color: var(--text-dim);
}

/* ── 配置重制确认浮层（自绘，锚定触发按钮）── */
.reset-confirm {
  z-index: 1200;
  background: #161d29;
  border: 1px solid #2a3342;
  border-radius: 12px;
  padding: 12px 12px 11px;
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.55);
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.reset-confirm::before {
  content: '';
  position: absolute;
  width: 11px;
  height: 11px;
  background: #161d29;
  border-left: 1px solid #2a3342;
  border-top: 1px solid #2a3342;
  transform: rotate(45deg);
  top: -6px;
  right: 26px;
}
.reset-confirm.above::before {
  top: auto;
  bottom: -6px;
  border-left: none;
  border-top: none;
  border-right: 1px solid #2a3342;
  border-bottom: 1px solid #2a3342;
}
.rc-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  font-weight: 700;
  color: var(--text);
}
.rc-title :deep(svg) {
  width: 14px;
  height: 14px;
  color: var(--danger);
}
.rc-desc {
  margin: 0;
  color: var(--text-dim);
  font-size: 10px;
  line-height: 1.55;
}
.rc-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 7px;
  margin-top: 2px;
}
.rc-actions button {
  min-height: 34px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: 8px;
  background: var(--bg-input);
  color: var(--text);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
}
.rc-actions button.danger {
  color: var(--danger);
  border-color: color-mix(in srgb, var(--danger) 46%, transparent);
  background: color-mix(in srgb, var(--danger) 8%, var(--bg-input));
}
.rc-actions button.danger:hover {
  background: color-mix(in srgb, var(--danger) 16%, var(--bg-input));
  border-color: var(--danger);
}
.rc-pop-enter-active,
.rc-pop-leave-active {
  transition: opacity 0.14s ease, transform 0.14s ease;
}
.rc-pop-enter-from,
.rc-pop-leave-to {
  opacity: 0;
  transform: translateY(-5px);
}
</style>
