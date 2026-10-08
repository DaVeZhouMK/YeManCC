<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import Dropdown from '@/components/Dropdown.vue';
import {
  FLOAT_PROFILES,
  getTdpTarget,
  type FloatProfile,
} from '@/bridge/autofloat';
import {
  applyGameCustomProfiles,
  applyPerformanceScheduleForCurrentPower,
  loadGameCustomConfig,
  loadPerformanceSchedule,
  onPerformanceScheduleChanged,
  resolveGameCustomProfiles,
  saveGameCustomConfig,
  type GameCustomConfig,
  type GameCustomProfile,
  type PerformanceScheduleConfig,
  type PowerSide,
  type ScheduleMode,
  type ScheduleProfile,
} from '@/bridge/performanceSchedule';
import { detectPowerMode } from '@/bridge/yeman';
import { powerSourceMode } from '@/bridge/powerSource';
import { on } from '@/bridge/ipc';
import { detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
import { closeJoyxoffIfRunning } from '@/bridge/gameproc';
import {
  applyGameCorePolicy,
  clearGameCorePolicy,
  detectGameCorePolicy,
  type GameCorePolicyCapabilities,
  type GameCorePolicyMode,
  type GameHyperThreadMode,
} from '@/bridge/gameCorePolicy';

const props = defineProps<{ game: DetectedGame | null; open: boolean }>();
const emit = defineEmits<{
  (e: 'status', value: { message?: string; error?: string }): void;
  (e: 'close'): void;
  (e: 'changed'): void;
}>();

const POWER_SIDES: PowerSide[] = ['ac', 'dc'];
const MODE_ORDER: ScheduleMode[] = [
  'eco',
  'balanced',
  'medium',
  'performance',
  'elite',
  'extreme',
];
const MODE_META: Record<ScheduleMode, { label: string }> = {
  eco: { label: '节能' },
  balanced: { label: '平衡' },
  medium: { label: '中等' },
  performance: { label: '高性能' },
  elite: { label: '精睿' },
  extreme: { label: '极致性能' },
};
const CPU_PRESET_LABEL: Record<ScheduleProfile['cpuPreset'], string> = {
  balanced: '平衡',
  turbo: '高性能',
  elite: '精睿',
  extreme: '极致性能',
};
const CPU_FLOAT_LABEL: Record<FloatProfile, string> = {
  none: 'CPU挡位',
  eco: `${(FLOAT_PROFILES.eco.min / 1000).toFixed(1)}Ghz-${(FLOAT_PROFILES.eco.max / 1000).toFixed(1)}Ghz`,
  bal: `${(FLOAT_PROFILES.bal.min / 1000).toFixed(1)}Ghz-${(FLOAT_PROFILES.bal.max / 1000).toFixed(1)}Ghz`,
  perf: `${(FLOAT_PROFILES.perf.min / 1000).toFixed(1)}Ghz-${(FLOAT_PROFILES.perf.max / 1000).toFixed(1)}Ghz`,
  aggressive: `${(FLOAT_PROFILES.aggressive.min / 1000).toFixed(1)}Ghz-${(FLOAT_PROFILES.aggressive.max / 1000).toFixed(1)}Ghz`,
};

// A3：当前游戏专属的手柄人格 / 陀螺仪开关，'follow' = 遵循全局（不下发覆盖）。
type PadPersonaOverride = NonNullable<GameCustomProfile['padPersona']>;
type GyroOverride = NonNullable<GameCustomProfile['gyroOverride']>;

// 与手柄页同一组 4 档（显示名对齐：本机手柄 / SteamDeck / PS5 / Xbox），
// 末尾追加「无操作」= 不下发覆盖、遵循全局（默认档）。
const PAD_PERSONA_OPTIONS: Array<{ value: PadPersonaOverride; label: string; sub: string }> = [
  { value: 'disabled', label: '本机手柄', sub: '关闭虚拟手柄，保持本机手柄' },
  { value: 'steamdeck', label: 'SteamDeck', sub: '虚拟手柄：SteamDeck 布局' },
  { value: 'dualsense-edge', label: 'PS5', sub: '虚拟手柄：DualSense Edge 布局' },
  { value: 'elite', label: 'Xbox', sub: '虚拟手柄：Elite 布局' },
  { value: 'follow', label: '无操作', sub: '不下发覆盖，遵循全局手柄设置' },
];
// 2026-09-30 用户裁决：去掉「陀螺仪开启」——改为直接用陀螺仪页的四个预设，
// 选中任一预设即等同于启用陀螺仪（并展开该预设参数），不新建预设。
const GYRO_OVERRIDE_OPTIONS: Array<{ value: GyroOverride; label: string; sub: string }> = [
  { value: 'follow', label: '遵循全局', sub: '跟随陀螺仪页面开关与预设' },
  { value: 'off', label: '陀螺仪关闭', sub: '本游戏强制关闭陀螺仪' },
  { value: 'fps', label: 'FPS射击', sub: '启用陀螺仪并使用 FPS 射击预设' },
  { value: 'racing', label: '赛车', sub: '启用陀螺仪并使用赛车预设' },
  { value: 'custom', label: '自定义', sub: '启用陀螺仪并使用自定义预设' },
  { value: 'steam', label: 'Steam', sub: '启用陀螺仪并使用 Steam 预设' },
];
// 旧档值（选项不再提供）：当前存档正停在 'on' 时如实显示并可切走，
// 与手柄人格旧档同一口径——不做静默改写。
const GYRO_LEGACY_OPTIONS: Array<{ value: GyroOverride; label: string; sub: string }> = [
  { value: 'on', label: '陀螺仪开启（旧档）', sub: '旧配置：启用陀螺仪但不指定预设' },
];
const gyroOverrideOptions = computed(() => [
  ...GYRO_OVERRIDE_OPTIONS,
  ...GYRO_LEGACY_OPTIONS.filter((item) => item.value === gyroOverride.value),
]);

function isPadPersonaOverride(value: unknown): value is PadPersonaOverride {
  return PAD_PERSONA_OPTIONS.some((item) => item.value === value);
}

function isGyroOverride(value: unknown): value is GyroOverride {
  return GYRO_OVERRIDE_OPTIONS.some((item) => item.value === value) ||
    GYRO_LEGACY_OPTIONS.some((item) => item.value === value);
}

const config = ref<GameCustomConfig>({ version: 1, entries: {}, rtss: {} });
const schedule = ref<PerformanceScheduleConfig | null>(null);
const detectedPowerSide = ref<PowerSide>('ac');
const powerSide = computed<PowerSide>(() => powerSourceMode.value ?? detectedPowerSide.value);
const selectedModes = ref<Record<PowerSide, ScheduleMode>>({ ac: 'performance', dc: 'performance' });
const busy = ref(false);
const error = ref('');
const message = ref('');
const expanded = ref(false);
const lastAppliedIdentity = ref('');
const lastCorePolicyIdentity = ref('');
const corePolicyCapabilities = ref<GameCorePolicyCapabilities | null>(null);
// A1（2026-09-29 用户裁决）：去掉「分配核心 / 超线程控制」开关，改用下拉档位
// 派生启用状态——非「Windows 默认」档即视为该策略已启用。
const corePolicyMode = ref<GameCorePolicyMode>('big-small');
const hyperThreadPolicy = ref<GameHyperThreadMode>('default');
// A2：原先落盘后回显的「已应用到当前游戏：CPU Set …」小字已删除；这里只保留
// 布尔状态，用于拼接 announce 的成功后缀。
const corePolicyApplied = ref(false);
// A3：当前游戏专属手柄人格与陀螺仪开关，'follow' = 遵循全局。
const padPersona = ref<PadPersonaOverride>('follow');
const gyroOverride = ref<GyroOverride>('follow');
let stopScheduleListener: (() => void) | null = null;
let stopPowerSideListener: (() => void) | null = null;
let stopPowerSideSettledListener: (() => void) | null = null;
let loadRevision = 0;

const key = computed(() => {
  const target = props.game;
  const raw = target?.path?.split(/[\\/]/).pop() || target?.name || '';
  const value = raw.toLowerCase().trim();
  return value ? (value.endsWith('.exe') ? value : `${value}.exe`) : '';
});
const entry = computed(() => key.value ? config.value.entries[key.value] : undefined);
const hasConfiguration = computed(() => !!entry.value);
const customEnabled = computed(() => entry.value?.enabled === true);
const gameAvailable = computed(() => !!props.game && !!key.value);
const gameName = computed(() => detectedGameName(props.game) || props.game?.name || key.value || '当前游戏');
const scheduleEnabled = computed(() => schedule.value?.enabled === true);
// 功能位固定显示：核心调度 / 超线程按硬件能力决定可否操作，陀螺仪在本机手柄时不可操作。
const corePolicySupported = computed(() => corePolicyCapabilities.value?.heterogeneous === true);
const hyperThreadSupported = computed(() => corePolicyCapabilities.value?.smtAvailable === true);
// A1：不再有「分配核心 / 超线程控制」开关，启用状态由下拉档位派生——
// 非「Windows 默认」档即视为启用。
const corePolicyActive = computed(() =>
  corePolicyCapabilities.value?.heterogeneous === true && corePolicyMode.value !== 'default');
const hyperThreadActive = computed(() =>
  corePolicyCapabilities.value?.smtAvailable === true && hyperThreadPolicy.value !== 'default');
// A3：选「本机手柄」时陀螺仪无法操作——陀螺仪下拉禁用并暗调。
const gyroLockedByNativePad = computed(() => padPersona.value === 'disabled');

function isScheduleMode(value: unknown): value is ScheduleMode {
  return typeof value === 'string' && MODE_ORDER.includes(value as ScheduleMode);
}

function entryMode(value: GameCustomProfile | undefined, side: PowerSide): ScheduleMode | undefined {
  const mode = side === 'ac' ? value?.acMode : value?.dcMode;
  return isScheduleMode(mode) ? mode : undefined;
}

function syncSelectedModes(): void {
  const current = schedule.value;
  if (!current) return;
  selectedModes.value = {
    ac: entryMode(entry.value, 'ac') || current.active.ac,
    dc: entryMode(entry.value, 'dc') || current.active.dc,
  };
}

function modeOptionsFor(side: PowerSide) {
  return MODE_ORDER.map((mode) => ({
    value: mode,
    label: MODE_META[mode].label,
    sub: modeDetail(side, mode),
  }));
}

function modeDetail(side: PowerSide, mode: ScheduleMode): string {
  const profile = schedule.value?.profiles?.[side]?.[mode];
  if (!profile) return '未配置';
  const fps = profile.fpsTarget > 0 ? `${profile.fpsTarget} FPS` : '不锁帧';
  const cpu = profile.fpsTarget > 0 && profile.cpuTarget !== 'none'
    ? `CPU浮动值 ${CPU_FLOAT_LABEL[profile.cpuTarget]}`
    : `CPU挡位 ${CPU_PRESET_LABEL[profile.cpuPreset]}`;
  const floating = profile.fpsTarget > 0
    ? ` · 浮动执行${getTdpTarget(profile.tdpMax, profile.tdpStrategy)}W`
    : '';
  return `${fps} · ${profile.tdpMax}W · ${cpu}${floating}`;
}

function announce(messageText = '', errorText = ''): void {
  message.value = messageText;
  error.value = errorText;
  emit('status', { message: messageText, error: errorText });
}

function cloneProfile(profile: ScheduleProfile): ScheduleProfile {
  return { ...profile };
}

function profilesForEntry(value: GameCustomProfile, current: PerformanceScheduleConfig): { ac: ScheduleProfile; dc: ScheduleProfile } {
  const resolved = resolveGameCustomProfiles(value, current);
  return { ac: cloneProfile(resolved.ac), dc: cloneProfile(resolved.dc) };
}

function makeEntry(
  current: GameCustomProfile | undefined,
  currentSchedule: PerformanceScheduleConfig,
  modes: Record<PowerSide, ScheduleMode>,
): GameCustomProfile {
  // A1：新档默认「大核为主」并立即生效——corePolicyEnabled 由档位派生写盘，
  // 保持与旧字段兼容（老配置文件缺字段时回落到 Windows 默认 → 未启用）。
  const corePolicyMode = isCorePolicyMode(current?.corePolicyMode) ? current.corePolicyMode : 'big-small';
  const hyperThreadPolicy = isHyperThreadMode(current?.hyperThreadPolicy) ? current.hyperThreadPolicy : 'default';
  return {
    displayName: current?.displayName || gameName.value,
    enabled: current?.enabled ?? false,
    acMode: modes.ac,
    dcMode: modes.dc,
    ac: cloneProfile(currentSchedule.profiles.ac[modes.ac]),
    dc: cloneProfile(currentSchedule.profiles.dc[modes.dc]),
    corePolicyEnabled: corePolicyMode !== 'default',
    corePolicyMode,
    hyperThreadPolicyEnabled: hyperThreadPolicy !== 'default',
    hyperThreadPolicy,
    padPersona: isPadPersonaOverride(current?.padPersona) ? current.padPersona : 'follow',
    gyroOverride: isGyroOverride(current?.gyroOverride) ? current.gyroOverride : 'follow',
  };
}

function corePolicyTarget(target: DetectedGame | null): { pid: number; processCreated: string } | undefined {
  return target ? { pid: target.pid, processCreated: target.processCreated } : undefined;
}

function corePolicyIsActive(): boolean {
  return corePolicyActive.value || hyperThreadActive.value;
}

async function applyCurrentCorePolicy(
  target: DetectedGame | null = props.game,
  current: GameCustomProfile | undefined = entry.value,
): Promise<void> {
  const identity = target ? `${target.pid}:${target.processCreated}:${key.value}` : '';
  // 探测失败时保持已有进程限制，避免一次短暂 IPC/系统探测错误把用户
  // 已经启用的专属策略误清掉；下一次识别刷新会继续尝试。
  if (target && current && !corePolicyCapabilities.value) return;
  if (!target || !current || current.enabled === false || !corePolicyIsActive()) {
    const cleared = await clearGameCorePolicy(current ? corePolicyTarget(target) : undefined);
    if (!cleared) throw new Error('恢复游戏原始 CPU 设置失败');
    lastCorePolicyIdentity.value = '';
    corePolicyApplied.value = false;
    return;
  }
  if (lastCorePolicyIdentity.value === identity) return;
  const result = await applyGameCorePolicy(
    corePolicyTarget(target)!,
    corePolicyActive.value ? corePolicyMode.value : 'default',
    hyperThreadActive.value ? hyperThreadPolicy.value : 'default',
  );
  if (!result?.ok || !result.applied) {
    throw new Error(result?.error || '当前游戏 CPU 设置未应用');
  }
  lastCorePolicyIdentity.value = identity;
  corePolicyApplied.value = true;
}

// A1/A3：气泡内下拉共用同一保存流程——写盘后立即下发。
// enabled 字段由档位派生写盘，保持与老配置字段兼容。
// 功能位始终可操作：尚未创建专属条目时，改动任一下拉即自动创建条目。
async function saveGameCustomPatch(patch: Partial<GameCustomProfile>, successMessage: string): Promise<void> {
  const currentSchedule = schedule.value;
  if (busy.value || !props.game || !key.value || !currentSchedule) return;
  loadRevision += 1;
  const release = tryAcquireQuickAction('top-custom-core-policy');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  const previous = entry.value;
  const nextEntry: GameCustomProfile = {
    ...makeEntry(previous, currentSchedule, selectedModes.value),
    ...patch,
  };
  if ('corePolicyMode' in patch) nextEntry.corePolicyEnabled = nextEntry.corePolicyMode !== 'default';
  if ('hyperThreadPolicy' in patch) nextEntry.hyperThreadPolicyEnabled = nextEntry.hyperThreadPolicy !== 'default';
  const next: GameCustomConfig = {
    ...config.value,
    entries: { ...config.value.entries, [key.value]: nextEntry },
  };
  busy.value = true;
  try {
    await saveGameCustomConfig(next);
    config.value = next;
    syncCorePolicyFromEntry();
    // Input-only edits are reconciled by the input owner, not process affinity.
    const coreChanged = 'corePolicyMode' in patch || 'corePolicyEnabled' in patch ||
      'hyperThreadPolicy' in patch || 'hyperThreadPolicyEnabled' in patch;
    if (coreChanged) {
      lastCorePolicyIdentity.value = '';
      await applyCurrentCorePolicy(props.game, nextEntry);
    }
    const suffix = coreChanged && corePolicyApplied.value ? '，并已应用到当前游戏' : '';
    announce(`${successMessage}${suffix}`);
    emit('changed');
  } catch (e) {
    // 保存失败时回滚：原本没有条目则删除刚创建的条目。
    const rollbackEntries = { ...config.value.entries };
    if (previous) rollbackEntries[key.value] = previous;
    else delete rollbackEntries[key.value];
    try {
      await saveGameCustomConfig({ ...config.value, entries: rollbackEntries });
    } catch { /* 下一次 load 会重新读取磁盘状态。 */ }
    config.value = { ...config.value, entries: rollbackEntries };
    syncCorePolicyFromEntry();
    lastCorePolicyIdentity.value = '';
    announce('', `保存专属设置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

function selectCorePolicyMode(value: string | number): void {
  if (!isCorePolicyMode(value)) return;
  void saveGameCustomPatch({ corePolicyMode: value }, `已选择核心调度：${corePolicyModeOptions.value.find((item) => item.value === value)?.label || value}`);
}

function selectHyperThreadPolicy(value: string | number): void {
  if (!isHyperThreadMode(value)) return;
  void saveGameCustomPatch({ hyperThreadPolicy: value }, `已选择超线程开关：${hyperThreadModeOptions.find((item) => item.value === value)?.label || value}`);
}

function selectPadPersona(value: string | number): void {
  if (!isPadPersonaOverride(value)) return;
  // 本机手柄时陀螺仪无法操作：选择本机手柄会把陀螺仪覆盖一并回落为「遵循全局」，
  // 运行时会强制关闭陀螺仪；此处同步清掉 UI 上的陀螺仪覆盖，避免留下非法组合。
  const patch: Partial<GameCustomProfile> = { padPersona: value };
  if (value === 'disabled') patch.gyroOverride = 'follow';
  const label = PAD_PERSONA_OPTIONS.find((item) => item.value === value)?.label || value;
  if (value === 'steamdeck') {
    void selectSteamDeckPad(label);
    return;
  }
  void saveGameCustomPatch(patch, `已选择手柄：${label}`);
}

// 2026-09-30 用户裁决：切到 SteamDeck 虚拟手柄时鼠标交给 Steam —— JoyXoff 若在跑
// 先强行关掉，再把切换结果合成一条顶部提示（避免两条状态互相覆盖）。
async function selectSteamDeckPad(label: string): Promise<void> {
  const closed = await closeJoyxoffIfRunning().catch(() => false);
  await saveGameCustomPatch(
    { padPersona: 'steamdeck' },
    closed ? `已选择手柄：${label}，并已关闭 JoyXoff` : `已选择手柄：${label}`,
  );
}

function selectGyroOverride(value: string | number): void {
  if (!isGyroOverride(value)) return;
  if (gyroLockedByNativePad.value) return;
  void saveGameCustomPatch({ gyroOverride: value }, `已选择陀螺仪：${gyroOverrideOptions.value.find((item) => item.value === value)?.label || value}`);
}

async function load(): Promise<void> {
  if (!props.open) return;
  const revision = ++loadRevision;
  const [loaded, loadedSchedule] = await Promise.all([loadGameCustomConfig(), loadPerformanceSchedule()]);
  if (revision !== loadRevision || busy.value || !props.open) return;
  config.value = loaded;
  schedule.value = loadedSchedule;
  syncSelectedModes();
  syncCorePolicyFromEntry();
  if (!entry.value) lastAppliedIdentity.value = '';
  if (!entry.value) lastCorePolicyIdentity.value = '';
  detectedPowerSide.value = await detectPowerMode().catch(() => 'ac');
  corePolicyCapabilities.value = await detectGameCorePolicy();
  if (revision !== loadRevision || busy.value || !props.open) return;
  // Automatic policy application is owned by the resident confirmed-target watcher.
}

async function createConfiguration(): Promise<void> {
  if (hasConfiguration.value || busy.value) {
    expanded.value = true;
    return;
  }
  if (!gameAvailable.value) return;

  loadRevision += 1;
  const release = tryAcquireQuickAction('top-custom-profile-create');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  expanded.value = true;
  try {
    // 点击整个气泡即完成“添加”：初始使用自动优化当前 AC/DC 挡位，
    // 随后页面立即进入可删除、可切换挡位的已配置状态。
    const currentSchedule = await loadPerformanceSchedule();
    schedule.value = currentSchedule;
    const modes: Record<PowerSide, ScheduleMode> = {
      ac: currentSchedule.active.ac,
      dc: currentSchedule.active.dc,
    };
    const nextEntry = makeEntry(undefined, currentSchedule, modes);
    const next = {
      ...config.value,
      entries: { ...config.value.entries, [key.value]: nextEntry },
    };
    await saveGameCustomConfig(next);
    config.value = next;
    selectedModes.value = modes;
    syncCorePolicyFromEntry();
    lastAppliedIdentity.value = '';

    // 创建只保存配置，不改变当前性能状态；用户稍后点击“未启用专属配置”
    // 才会真正下发并成为当前游戏的专属覆盖。
    announce(`已添加 ${gameName.value} 专属配置，当前未启用`);
    emit('changed');
  } catch (e) {
    announce('', `添加专属配置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
    if (expanded.value) focusExpandedEntry();
  }
}

function focusExpandedEntry(): void {
  nextTick(() => {
    const first = document.querySelector<HTMLElement>('[data-gp-custom-body] button:not(:disabled), [data-gp-custom-body] select:not(:disabled), [data-gp-custom-body] input:not(:disabled), [data-gp-custom-body] [tabindex]:not([tabindex="-1"])');
    if (first) focusGamepadElement(first);
  });
}

function isCorePolicyMode(value: unknown): value is GameCorePolicyMode {
  return value === 'default' || value === 'only-big' || value === 'big-small' ||
    value === 'only-small' || value === 'small-super-small' || value === 'all';
}

function isHyperThreadMode(value: unknown): value is GameHyperThreadMode {
  return value === 'default' || value === 'on' || value === 'off';
}

function syncCorePolicyFromEntry(): void {
  const current = entry.value;
  // A1：档位是唯一事实来源；老配置缺字段（或只有旧 enabled 开关）时回落到
  // Windows 默认档 = 未启用。
  corePolicyMode.value = isCorePolicyMode(current?.corePolicyMode)
    ? current!.corePolicyMode!
    : 'default';
  hyperThreadPolicy.value = isHyperThreadMode(current?.hyperThreadPolicy)
    ? current!.hyperThreadPolicy!
    : 'default';
  // A3：同步专属手柄 / 陀螺仪覆盖。
  padPersona.value = isPadPersonaOverride(current?.padPersona) ? current!.padPersona! : 'follow';
  gyroOverride.value = isGyroOverride(current?.gyroOverride) ? current!.gyroOverride! : 'follow';
}

const corePolicyModeOptions = computed(() => {
  const classes = corePolicyCapabilities.value?.efficiencyClasses.length ?? 0;
  const options: Array<{ value: GameCorePolicyMode; label: string; sub: string }> = [
    { value: 'default', label: 'Windows 默认调度', sub: '不限制当前游戏核心' },
    {
      value: 'big-small',
      label: '大核为主',
      sub: classes >= 3 ? '大核 + 小核，排除超小' : '大核 + 小核',
    },
    { value: 'only-big', label: '仅大核', sub: '只允许性能等级最高的核心' },
  ];
  if (classes >= 3) {
    options.push(
      { value: 'only-small', label: '仅小核', sub: '只允许中间效率等级核心' },
      { value: 'small-super-small', label: '小核 + 超小核', sub: '排除大核' },
    );
  } else {
    options.push({ value: 'only-small', label: '仅小核', sub: '只允许效率等级核心' });
  }
  options.push({ value: 'all', label: '全部核心', sub: '允许所有已识别核心' });
  return options;
});

const hyperThreadModeOptions = [
  { value: 'default' as GameHyperThreadMode, label: 'Windows 默认', sub: '不改变超线程分配' },
  { value: 'on' as GameHyperThreadMode, label: '允许超线程', sub: '允许每个物理核的全部线程' },
  { value: 'off' as GameHyperThreadMode, label: '本游戏不使用超线程', sub: '每个物理核只保留一个线程' },
];

function disableLeavingBody(el: Element): void {
  if (!(el instanceof HTMLElement)) return;
  el.inert = true;
  el.setAttribute('aria-hidden', 'true');
}

function onGamepadCustomBack(): void {
  if (!expanded.value) return;
  // B 在专属配置气泡内必须真实收起该气泡，而不是只依赖全局页面返回。
  // preventDefault 由外层识别控制器负责；这里保证组件自身状态先变更。
  closePanel();
}

function closePanel(): void {
  expanded.value = false;
  emit('close');
  nextTick(() => {
    const entryEl = document.querySelector<HTMLElement>('[data-gp-custom-entry]');
    if (entryEl) focusGamepadElement(entryEl);
  });
}

function togglePanel(): void {
  // 没有扫描到真实游戏时，专属配置只是禁用态入口：绝不展开、创建或
  // 抢占手柄焦点，避免误触后进入没有可用目标的死路。
  if (!gameAvailable.value) return;
  if (!hasConfiguration.value) {
    void createConfiguration();
    return;
  }
  expanded.value = !expanded.value;
  if (expanded.value) focusExpandedEntry();
}

async function selectMode(side: PowerSide, rawMode: string | number): Promise<void> {
  const mode = rawMode as ScheduleMode;
  if (!isScheduleMode(mode) || busy.value || !props.game || !key.value) return;
  loadRevision += 1;
  const release = tryAcquireQuickAction('top-custom-profile-select-mode');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  busy.value = true;
  try {
    // 重新读取当前自动优化配置，确保这里使用的是自动优化页刚保存的最新组合。
    const currentSchedule = await loadPerformanceSchedule();
    schedule.value = currentSchedule;
    const modes: Record<PowerSide, ScheduleMode> = {
      ...selectedModes.value,
      [side]: mode,
    };
    const current = config.value.entries[key.value];
    const nextEntry = makeEntry(current, currentSchedule, modes);
    const next = {
      ...config.value,
      entries: { ...config.value.entries, [key.value]: nextEntry },
    };
    await saveGameCustomConfig(next);
    config.value = next;
    selectedModes.value = modes;
    syncCorePolicyFromEntry();
    lastAppliedIdentity.value = '';

    let applied = false;
    if (nextEntry.enabled !== false) {
      const profiles = profilesForEntry(nextEntry, currentSchedule);
      applied = await applyGameCustomProfiles(profiles.ac, profiles.dc, {
        pid: props.game.pid,
        processCreated: props.game.processCreated,
      });
    }
    await applyCurrentCorePolicy(props.game, nextEntry);
    const suffix = nextEntry.enabled === false
      ? '（已保存，当前未启用）'
      : (applied ? '并已应用' : '（已保存，当前未下发）');
    announce(`${side.toUpperCase()} 已选择${MODE_META[mode].label}${suffix}`);
    emit('changed');
  } catch (e) {
    announce('', `保存专属配置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

async function toggleCustomEnabled(): Promise<void> {
  const current = entry.value;
  if (!current || busy.value || !props.game || !key.value) return;
  loadRevision += 1;
  const release = tryAcquireQuickAction('top-custom-profile-toggle');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  const previous = current;
  const nextEnabled = current.enabled === false;
  const nextEntry = { ...current, enabled: nextEnabled };
  const next = {
    ...config.value,
    entries: { ...config.value.entries, [key.value]: nextEntry },
  };
  busy.value = true;
  try {
    const currentSchedule = await loadPerformanceSchedule();
    await saveGameCustomConfig(next);
    config.value = next;
    lastAppliedIdentity.value = '';
    syncCorePolicyFromEntry();

    if (nextEnabled) {
      const profiles = profilesForEntry(nextEntry, currentSchedule);
      const applied = await applyGameCustomProfiles(profiles.ac, profiles.dc, {
        pid: props.game.pid,
        processCreated: props.game.processCreated,
      });
      if (!applied) throw new Error('锁定游戏已变化，专属配置未应用');
      lastAppliedIdentity.value = `${props.game.pid}:${props.game.processCreated}:${key.value}`;
      await applyCurrentCorePolicy(props.game, nextEntry);
      announce('已启用并应用专属配置');
    } else {
      // 关闭后只恢复当前电源侧的普通自动档位；手动全局模式不主动改硬件。
      if (currentSchedule.enabled) await applyPerformanceScheduleForCurrentPower();
      announce('已关闭专属配置，已恢复普通自动调度');
    }
    emit('changed');
  } catch (e) {
    // 应用失败时回滚“启用”标志，避免界面显示已启用但硬件未切换。
    try {
      await saveGameCustomConfig({
        ...config.value,
        entries: { ...config.value.entries, [key.value]: previous },
      });
    } catch { /* 保留原错误，下一次 load 会重新读取磁盘状态。 */ }
    config.value = {
      ...config.value,
      entries: { ...config.value.entries, [key.value]: previous },
    };
    announce('', `${nextEnabled ? '启用' : '关闭'}专属配置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

watch(() => [props.open, props.game?.pid, props.game?.processCreated], () => {
  if (props.open) void load();
});

watch(() => gameAvailable.value, (available) => {
  if (available) return;
  // Raw recognition loss must never clear the confirmed game policy here.
  lastCorePolicyIdentity.value = '';
  corePolicyApplied.value = false;
  const activeElement = document.activeElement as HTMLElement | null;
  const focusWasInCustom = !!activeElement?.closest('[data-gp-custom-entry], [data-gp-custom-body]');
  expanded.value = false;
  if (focusWasInCustom) {
    nextTick(() => {
      const fallback = document.querySelector<HTMLElement>(
        '[data-gp-game-rules-entry], [data-gp-game-quick-menu] [data-gp-game-quick-footer] button',
      );
      if (fallback) focusGamepadElement(fallback);
    });
  }
});

onMounted(() => {
  window.addEventListener('game-quick-custom-back', onGamepadCustomBack);
  const syncPowerSide = ({ ac }: { ac: boolean }): void => {
    // sourceChanged is immediate; acChanged is the final debounced confirmation.
    // Listening to both keeps the card responsive without losing the settled
    // state after a noisy adapter transition.
    detectedPowerSide.value = Boolean(ac) ? 'ac' : 'dc';
  };
  stopPowerSideListener = on<{ ac: boolean }>('power.sourceChanged', syncPowerSide);
  stopPowerSideSettledListener = on<{ ac: boolean }>('power.acChanged', syncPowerSide);
  stopScheduleListener = onPerformanceScheduleChanged((next) => {
    schedule.value = next;
    syncSelectedModes();
  });
  void load();
});

onUnmounted(() => {
  window.removeEventListener('game-quick-custom-back', onGamepadCustomBack);
  stopScheduleListener?.();
  stopScheduleListener = null;
  stopPowerSideListener?.();
  stopPowerSideListener = null;
  stopPowerSideSettledListener?.();
  stopPowerSideSettledListener = null;
});
</script>

<template>
  <div
    class="custom-top-panel"
    :class="{ expanded, disabled: !gameAvailable }"
    data-gp-custom-panel
    :data-gp-expanded="expanded && gameAvailable"
  >
    <button
      type="button"
      :class="{ active: expanded }"
      class="custom-top-head game-menu-steam-row"
      :tabindex="!gameAvailable ? -1 : 0"
      data-gp-custom-entry
      data-gp-game-row="custom-entry"
      :aria-expanded="expanded"
      :aria-disabled="!gameAvailable"
      @click.stop="togglePanel"
    >
      <div class="custom-top-label">
        <AppIcon name="settings" class="custom-top-label-icon" />
        <strong>专属配置</strong>
        <small>{{ hasConfiguration ? gameName : '未配置' }}</small>
        <span v-if="customEnabled" class="custom-top-active">当前启用</span>
      </div>
      <div class="custom-top-head-actions">
        <span class="custom-top-chevron" aria-hidden="true">{{ expanded ? '⌃' : '⌄' }}</span>
      </div>
    </button>

    <Transition name="custom-submenu-pop" @before-leave="disableLeavingBody">
      <div v-if="expanded && gameAvailable" class="custom-top-body" data-gp-custom-body>
      <div class="power-mode-list">
        <div
          v-for="item in POWER_SIDES"
          :key="item"
          class="power-mode-row"
          :data-gp-game-row="`custom-${item}`"
          :class="[{ current: powerSide === item }, item]"
        >
          <AppIcon :name="item === 'ac' ? 'plug' : 'battery'" class="side-icon" :aria-label="item === 'ac' ? '交流电' : '电池'" />
          <div class="side-name">
            <strong>{{ item.toUpperCase() }}</strong>
            <small>{{ item === 'ac' ? '交流电' : '电池' }}</small>
          </div>
          <div class="mode-picker">
            <Dropdown
              :model-value="selectedModes[item]"
              :options="modeOptionsFor(item)"
              :disabled="busy || !game || !schedule"
              :color="item === 'dc' ? 'dc' : 'accent'"
              :aria-label="`${item.toUpperCase()} 专属性能档位`"
              @update:model-value="selectMode(item, $event)"
            />
            <small>{{ modeDetail(item, selectedModes[item]) }}</small>
          </div>
        </div>
      </div>

      <div class="game-setting-bubble" data-gp-group="custom-core-policy">
        <div class="game-setting-title"><strong>核心线程控制</strong></div>
        <div class="game-setting-fields">
          <div
            class="game-setting-field"
            :class="{ locked: !corePolicySupported }"
            data-gp-game-row="custom-core-big-picker"
          >
            <small>核心调度</small>
            <Dropdown
              :model-value="corePolicyMode"
              :options="corePolicyModeOptions"
              :disabled="busy || !corePolicySupported"
              color="accent"
              aria-label="当前游戏核心调度"
              @update:model-value="selectCorePolicyMode"
            />
          </div>
          <div
            class="game-setting-field"
            :class="{ locked: !hyperThreadSupported }"
            data-gp-game-row="custom-core-smt-picker"
          >
            <small>超线程开关</small>
            <Dropdown
              :model-value="hyperThreadPolicy"
              :options="hyperThreadModeOptions"
              :disabled="busy || !hyperThreadSupported"
              color="dc"
              aria-label="当前游戏超线程开关"
              @update:model-value="selectHyperThreadPolicy"
            />
          </div>
        </div>
      </div>

      <div class="game-setting-bubble" data-gp-group="custom-input-override">
        <div class="game-setting-fields">
          <div class="game-setting-field" data-gp-game-row="custom-input-pad">
            <small>选择手柄</small>
            <Dropdown
              :model-value="padPersona"
              :options="PAD_PERSONA_OPTIONS"
              :disabled="busy"
              color="accent"
              aria-label="当前游戏专属手柄"
              @update:model-value="selectPadPersona"
            />
          </div>
          <div
            class="game-setting-field"
            :class="{ locked: gyroLockedByNativePad }"
            data-gp-game-row="custom-input-gyro"
          >
            <small>陀螺仪开关</small>
            <Dropdown
              :model-value="gyroOverride"
              :options="gyroOverrideOptions"
              :disabled="busy || gyroLockedByNativePad"
              color="dc"
              aria-label="当前游戏专属陀螺仪开关"
              @update:model-value="selectGyroOverride"
            />
          </div>
        </div>
      </div>

      <div v-if="hasConfiguration" class="custom-top-actions" data-gp-custom-actions data-gp-game-row="custom-actions">
        <button
          type="button"
          data-gp-custom-action
          :class="{ danger: customEnabled }"
          :disabled="busy"
          @click.stop="toggleCustomEnabled"
        >
          <AppIcon :name="customEnabled ? 'close' : 'bolt'" />{{ customEnabled ? '关闭专属配置' : '点击启用配置' }}
        </button>
        <button type="button" data-gp-custom-action class="custom-close-action" @click.stop="closePanel"><strong>B</strong>关闭页面</button>
      </div>

      <small v-if="!game" class="custom-top-hint">识别到游戏后可选择专属性能档位</small>

      </div>
    </Transition>
  </div>
</template>

<style scoped>
.custom-top-panel {
  display: block;
}
.custom-top-panel.disabled {
  opacity: .45;
  filter: saturate(.65);
}
.custom-top-panel.disabled .custom-top-head {
  cursor: default;
}
.custom-top-panel.expanded {
  display: block;
}
.custom-top-head {
  width: 100%;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  box-sizing: border-box;
  display: grid;
  grid-template-columns: minmax(0, 1fr) 22px;
  align-items: center;
  gap: 8px;
  min-height: 48px;
  padding: 7px 9px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 12px 30px rgba(0,0,0,.28);
  cursor: pointer;
}
.custom-top-head.game-menu-steam-row {
  display: flex;
  align-items: center;
  width: 100%;
  min-height: 54px;
  gap: 10px;
  padding: 10px 12px;
  border: 0;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
}
.custom-top-head.game-menu-steam-row .custom-top-label {
  flex: 1 1 auto;
}
.custom-top-head.game-menu-steam-row .custom-top-head-actions {
  margin-left: auto;
}
.custom-top-head.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
}
/* 与游戏黑/白名单一致：父气泡 overflow:hidden 会裁掉全局外发光，
 * 手柄焦点改用内描边。 */
.custom-top-head.focused {
  box-shadow: inset 0 0 0 2px var(--accent), inset 0 0 10px color-mix(in srgb, var(--accent) 35%, transparent);
}
.custom-top-head > div:first-child { min-width: 0; }
.custom-top-label { display: flex; align-items: center; gap: 7px; min-width: 0; text-align: center; }
.custom-top-label small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.custom-top-active {
  flex: 0 0 auto;
  padding: 2px 6px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--ok) 16%, var(--bg-input));
  color: var(--ok);
  font-size: 10px;
  font-weight: 800;
  line-height: 1.2;
  white-space: nowrap;
}
.custom-top-label-icon { width: 15px; height: 15px; color: var(--accent); flex: 0 0 auto; }
.custom-top-head strong { font-size: 13px; }
.custom-top-head-actions { display: inline-flex; align-items: center; justify-content: flex-end; gap: 7px; min-width: 0; }
.custom-top-head-actions button,
.custom-top-actions button,
.custom-delete-confirm button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  min-height: 30px;
  padding: 5px 8px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: 7px;
  background: color-mix(in srgb, var(--bg-solid) 38%, transparent);
  color: var(--text);
  font-size: 10px;
  cursor: pointer;
}
.custom-top-head-actions button :deep(svg), .custom-top-actions button :deep(svg) { width: 13px; height: 13px; }
.custom-top-chevron { display: inline-flex; align-items: center; justify-content: center; width: 22px; color: var(--accent); font-size: 16px; line-height: 1; }
.custom-top-head small { color: var(--text-dim); font-size: 10px; }
.custom-top-body {
  display: grid;
  gap: 8px;
  margin: 8px 0 0;
  padding: 8px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 16px 40px rgba(0,0,0,.4);
}
.custom-submenu-pop-enter-active, .custom-submenu-pop-leave-active {
  transition: opacity .16s ease, transform .16s ease;
  transform-origin: top center;
}
.custom-submenu-pop-enter-from, .custom-submenu-pop-leave-to {
  opacity: 0;
  transform: translateY(-5px) scale(.985);
}
.custom-top-actions { display: flex; gap: 5px; flex-wrap: nowrap; justify-content: flex-end; min-width: 0; }
.custom-top-actions button { background: var(--bg-input); white-space: nowrap; }
.custom-top-actions .danger, .custom-delete-confirm .danger { color: var(--danger); }
.custom-close-action strong { margin-right: 4px; color: var(--danger); font-weight: 800; }
.power-mode-list { display: grid; gap: 8px; }
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
.side-icon { width: 14px; height: 14px; justify-self: center; color: var(--text-dim); }
.power-mode-row.current.ac .side-icon { color: var(--accent); }
.power-mode-row.current.dc .side-icon { color: var(--dc-accent); }
.power-mode-row.current.ac { border-color: color-mix(in srgb, var(--accent) 42%, transparent); }
.power-mode-row.current.dc { border-color: color-mix(in srgb, var(--dc-accent) 42%, transparent); }
.side-name { display: flex; flex-direction: column; align-items: center; gap: 2px; text-align: center; }
.side-name strong { font-size: 13px; }
.side-name small, .mode-picker > small, .custom-top-hint { color: var(--text-dim); font-size: 11px; white-space: nowrap; }
.mode-picker { min-width: 0; display: grid; gap: 4px; }
.mode-picker > small { overflow: hidden; text-overflow: ellipsis; }
/* 设置气泡：一行内「左标题 + 右侧若干平行下拉」。 */
.game-setting-bubble {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 9px 10px;
  border: 1px solid rgba(255,255,255,.055);
  border-radius: 9px;
  background: var(--bg-input);
}
.game-setting-title { flex: 0 0 auto; min-width: 0; }
.game-setting-title strong { font-size: 12px; }
.game-setting-fields {
  flex: 1 1 auto;
  min-width: 0;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
  align-items: end;
}
.game-setting-field { min-width: 0; display: grid; grid-template-rows: auto minmax(34px, auto); gap: 4px; }
.game-setting-field > small { color: var(--text-dim); font-size: 10px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.game-setting-field :deep(.dd), .game-setting-field :deep(.dd-trigger) { width: 100%; }
/* A3：选「本机手柄」时陀螺仪无法操作，整块暗调。 */
.game-setting-field.locked { opacity: .45; filter: saturate(.6); }
.custom-top-hint { display: block; }
.custom-delete-confirm {
  display: grid;
  gap: 5px;
  padding: 10px;
  border: 1px solid color-mix(in srgb, var(--danger) 35%, transparent);
  border-radius: 8px;
  background: var(--bg-solid);
}
.custom-delete-confirm small { color: var(--text-dim); }
.custom-delete-confirm > div { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
button:disabled { opacity: .45; cursor: default; }
</style>


