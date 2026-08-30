<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import AppIcon from '@/components/AppIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import Dropdown from '@/components/Dropdown.vue';
import Toggle from '@/components/Toggle.vue';
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
import { detectedGameName, type DetectedGame } from '@/bridge/gamedetect';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
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

const config = ref<GameCustomConfig>({ version: 1, entries: {}, rtss: {} });
const schedule = ref<PerformanceScheduleConfig | null>(null);
const powerSide = ref<PowerSide>('ac');
const selectedModes = ref<Record<PowerSide, ScheduleMode>>({ ac: 'performance', dc: 'performance' });
const busy = ref(false);
const error = ref('');
const message = ref('');
const expanded = ref(false);
const lastAppliedIdentity = ref('');
const lastCorePolicyIdentity = ref('');
const corePolicyCapabilities = ref<GameCorePolicyCapabilities | null>(null);
const corePolicyEnabled = ref(false);
const corePolicyMode = ref<GameCorePolicyMode>('default');
const hyperThreadPolicyEnabled = ref(false);
const hyperThreadPolicy = ref<GameHyperThreadMode>('default');
const corePolicyStatus = ref('');
let stopScheduleListener: (() => void) | null = null;
let loadRevision = 0;
let transientApplyRetryTimer: ReturnType<typeof setTimeout> | null = null;
let transientApplyRetryCount = 0;

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
const corePolicyVisible = computed(() => hasConfiguration.value && (
  corePolicyCapabilities.value?.heterogeneous === true ||
  corePolicyCapabilities.value?.smtAvailable === true
));
const corePolicyCardClass = computed(() => {
  const count = Number(corePolicyCapabilities.value?.heterogeneous === true) +
    Number(corePolicyCapabilities.value?.smtAvailable === true);
  return count === 1 ? 'single' : 'double';
});

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
  return {
    displayName: current?.displayName || gameName.value,
    enabled: current?.enabled ?? false,
    acMode: modes.ac,
    dcMode: modes.dc,
    ac: cloneProfile(currentSchedule.profiles.ac[modes.ac]),
    dc: cloneProfile(currentSchedule.profiles.dc[modes.dc]),
    corePolicyEnabled: current?.corePolicyEnabled ?? false,
    corePolicyMode: isCorePolicyMode(current?.corePolicyMode) ? current.corePolicyMode : 'default',
    hyperThreadPolicyEnabled: current?.hyperThreadPolicyEnabled ?? false,
    hyperThreadPolicy: isHyperThreadMode(current?.hyperThreadPolicy) ? current.hyperThreadPolicy : 'default',
  };
}

function isTransientApplyError(error: unknown): boolean {
  const messageText = error instanceof Error ? error.message : String(error ?? '');
  return messageText === 'outdated' ||
    messageText === 'WebView2 is recovering' ||
    messageText.includes('IPC worker queue is full or stopping');
}

function scheduleTransientApplyRetry(): void {
  if (transientApplyRetryCount >= 2 || transientApplyRetryTimer !== null) return;
  transientApplyRetryCount += 1;
  transientApplyRetryTimer = setTimeout(() => {
    transientApplyRetryTimer = null;
    if (props.open && customEnabled.value && props.game) void applyExistingProfile();
  }, 350 * transientApplyRetryCount);
}

async function applyExistingProfile(): Promise<void> {
  // A user action (creating/changing/deleting a profile) owns the scheduler
  // operation. Do not let the background refresh start a competing write.
  if (busy.value) return;
  const target = props.game;
  const current = entry.value;
  const currentSchedule = schedule.value;
  if (target && current && current.enabled !== false && currentSchedule && scheduleEnabled.value) {
    const identity = `${target.pid}:${target.processCreated}:${key.value}`;
    if (lastAppliedIdentity.value !== identity) {
      lastAppliedIdentity.value = identity;
      try {
        const profiles = profilesForEntry(current, currentSchedule);
        const applied = await applyGameCustomProfiles(profiles.ac, profiles.dc, {
          pid: target.pid,
          processCreated: target.processCreated,
        });
        if (!applied) lastAppliedIdentity.value = '';
        else transientApplyRetryCount = 0;
      } catch (e) {
        lastAppliedIdentity.value = '';
        // These errors are transient during WebView/native recovery or while the
        // native worker queue is draining. Retry quietly instead of presenting a
        // red application failure for an operation that has not permanently failed.
        if (isTransientApplyError(e)) {
          scheduleTransientApplyRetry();
        } else {
          transientApplyRetryCount = 0;
          announce('', `应用当前游戏专属配置失败：${(e as Error).message}`);
        }
      }
    }
  }
  await applyCurrentCorePolicy(target, current);
}

function corePolicyTarget(target: DetectedGame | null): { pid: number; processCreated: string } | undefined {
  return target ? { pid: target.pid, processCreated: target.processCreated } : undefined;
}

function corePolicyIsActive(): boolean {
  const coreActive = corePolicyEnabled.value &&
    corePolicyCapabilities.value?.heterogeneous === true &&
    corePolicyMode.value !== 'default';
  const hyperThreadActive = hyperThreadPolicyEnabled.value &&
    corePolicyCapabilities.value?.smtAvailable === true &&
    hyperThreadPolicy.value !== 'default';
  return coreActive || hyperThreadActive;
}

async function applyCurrentCorePolicy(
  target: DetectedGame | null = props.game,
  current: GameCustomProfile | undefined = entry.value,
): Promise<void> {
  const identity = target ? `${target.pid}:${target.processCreated}:${key.value}` : '';
  // 探测失败时保持已有进程限制，避免一次短暂 IPC/系统探测错误把用户
  // 已经启用的专属策略误清掉；下一次识别刷新会继续尝试。
  if (target && current && !corePolicyCapabilities.value) return;
  if (!target || !current || !corePolicyIsActive()) {
    const cleared = await clearGameCorePolicy(current ? corePolicyTarget(target) : undefined);
    if (!cleared) throw new Error('恢复游戏原始 CPU 设置失败');
    lastCorePolicyIdentity.value = '';
    corePolicyStatus.value = '';
    return;
  }
  if (lastCorePolicyIdentity.value === identity) return;
  const result = await applyGameCorePolicy(
    corePolicyTarget(target)!,
    corePolicyEnabled.value ? corePolicyMode.value : 'default',
    hyperThreadPolicyEnabled.value ? hyperThreadPolicy.value : 'default',
  );
  if (!result?.ok || !result.applied) {
    throw new Error(result?.error || '当前游戏 CPU 设置未应用');
  }
  lastCorePolicyIdentity.value = identity;
  corePolicyStatus.value = `已应用到当前游戏：CPU Set ${result.cpuSetCount} 个 · 亲和性 ${result.affinityMask}`;
}

async function saveCorePolicyPatch(
  patch: Partial<Pick<GameCustomProfile, 'corePolicyEnabled' | 'corePolicyMode' | 'hyperThreadPolicyEnabled' | 'hyperThreadPolicy'>>,
  successMessage: string,
): Promise<void> {
  const current = entry.value;
  if (!current || busy.value || !props.game || !key.value) return;
  loadRevision += 1;
  const release = tryAcquireQuickAction('top-custom-core-policy');
  if (!release) {
    announce('', '已有其它快捷操作正在执行，请稍候');
    return;
  }
  const previous = current;
  const nextEntry: GameCustomProfile = { ...current, ...patch };
  const next: GameCustomConfig = {
    ...config.value,
    entries: { ...config.value.entries, [key.value]: nextEntry },
  };
  busy.value = true;
  try {
    await saveGameCustomConfig(next);
    config.value = next;
    syncCorePolicyFromEntry();
    lastCorePolicyIdentity.value = '';
    await applyCurrentCorePolicy(props.game, nextEntry);
    announce(`${successMessage}${corePolicyStatus.value ? '，并已应用到当前游戏' : ''}`);
    emit('changed');
  } catch (e) {
    try {
      await saveGameCustomConfig({
        ...config.value,
        entries: { ...config.value.entries, [key.value]: previous },
      });
    } catch { /* 下一次 load 会重新读取磁盘状态。 */ }
    config.value = {
      ...config.value,
      entries: { ...config.value.entries, [key.value]: previous },
    };
    syncCorePolicyFromEntry();
    lastCorePolicyIdentity.value = '';
    announce('', `保存专属核心设置失败：${(e as Error).message}`);
  } finally {
    busy.value = false;
    release();
  }
}

function toggleCorePolicy(value: boolean): void {
  const nextMode = value && corePolicyMode.value === 'default'
    ? 'big-small' as GameCorePolicyMode
    : corePolicyMode.value;
  void saveCorePolicyPatch(
    { corePolicyEnabled: value, corePolicyMode: nextMode },
    value ? '已开启当前游戏分配核心（大核为主）' : '已关闭当前游戏分配核心',
  );
}

function selectCorePolicyMode(value: string | number): void {
  if (!isCorePolicyMode(value)) return;
  void saveCorePolicyPatch({ corePolicyMode: value }, `已选择核心分配：${corePolicyModeOptions.value.find((item) => item.value === value)?.label || value}`);
}

function toggleHyperThreadPolicy(value: boolean): void {
  void saveCorePolicyPatch({ hyperThreadPolicyEnabled: value }, value ? '已开启当前游戏超线程控制' : '已关闭当前游戏超线程控制');
}

function selectHyperThreadPolicy(value: string | number): void {
  if (!isHyperThreadMode(value)) return;
  void saveCorePolicyPatch({ hyperThreadPolicy: value }, `已选择超线程：${hyperThreadModeOptions.find((item) => item.value === value)?.label || value}`);
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
  powerSide.value = await detectPowerMode().catch(() => 'ac');
  corePolicyCapabilities.value = await detectGameCorePolicy();
  if (revision !== loadRevision || busy.value || !props.open) return;
  await applyExistingProfile();
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
  corePolicyEnabled.value = current?.corePolicyEnabled === true;
  corePolicyMode.value = isCorePolicyMode(current?.corePolicyMode)
    ? current!.corePolicyMode!
    : 'default';
  hyperThreadPolicyEnabled.value = current?.hyperThreadPolicyEnabled === true;
  hyperThreadPolicy.value = isHyperThreadMode(current?.hyperThreadPolicy)
    ? current!.hyperThreadPolicy!
    : 'default';
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
  void clearGameCorePolicy().catch(() => {});
  lastCorePolicyIdentity.value = '';
  corePolicyStatus.value = '';
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
  stopScheduleListener = onPerformanceScheduleChanged((next) => {
    schedule.value = next;
    syncSelectedModes();
  });
  void load();
});

onUnmounted(() => {
  if (transientApplyRetryTimer !== null) clearTimeout(transientApplyRetryTimer);
  transientApplyRetryTimer = null;
  window.removeEventListener('game-quick-custom-back', onGamepadCustomBack);
  stopScheduleListener?.();
  stopScheduleListener = null;
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

      <div
        v-if="corePolicyVisible"
        class="game-core-policy-list"
        :class="corePolicyCardClass"
        data-gp-group="custom-core-policy"
      >
        <div v-if="corePolicyCapabilities?.heterogeneous" class="game-core-policy-card">
          <div class="game-core-policy-control" data-gp-game-row="custom-core-big-toggle">
            <Toggle
              :model-value="corePolicyEnabled"
              label="分配核心"
              description="CPU Set + 进程亲和性"
              color="accent"
              :disabled="busy"
              @update:model-value="toggleCorePolicy"
            />
          </div>
          <div class="game-core-policy-picker game-core-policy-control" data-gp-game-row="custom-core-big-picker">
            <small>核心组合</small>
            <Dropdown
              :model-value="corePolicyMode"
              :options="corePolicyModeOptions"
              :disabled="busy || !corePolicyEnabled"
              color="accent"
              aria-label="当前游戏大小核心组合"
              @update:model-value="selectCorePolicyMode"
            />
          </div>
        </div>

        <div v-if="corePolicyCapabilities?.smtAvailable" class="game-core-policy-card">
          <div class="game-core-policy-control" data-gp-game-row="custom-core-smt-toggle">
            <Toggle
              :model-value="hyperThreadPolicyEnabled"
              label="超线程控制"
              description="游戏超线程亲和性"
              color="dc"
              :disabled="busy"
              @update:model-value="toggleHyperThreadPolicy"
            />
          </div>
          <div class="game-core-policy-picker game-core-policy-control" data-gp-game-row="custom-core-smt-picker">
            <small>超线程组合</small>
            <Dropdown
              :model-value="hyperThreadPolicy"
              :options="hyperThreadModeOptions"
              :disabled="busy || !hyperThreadPolicyEnabled"
              color="dc"
              aria-label="当前游戏超线程控制"
              @update:model-value="selectHyperThreadPolicy"
            />
          </div>
        </div>
      </div>

      <small v-if="corePolicyStatus" class="custom-top-hint core-policy-status">{{ corePolicyStatus }}</small>
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
.game-core-policy-list {
  display: grid;
  gap: 8px;
  align-items: stretch;
}
.game-core-policy-list.double { grid-template-columns: repeat(2, minmax(0, 1fr)); align-items: stretch; }
.game-core-policy-list.single { grid-template-columns: minmax(0, 1fr); }
.game-core-policy-card {
  min-width: 0;
  display: grid;
  grid-template-rows: minmax(54px, auto) auto;
  align-content: start;
  gap: 5px;
  padding: 9px 10px 10px;
  border: 1px solid rgba(255,255,255,.055);
  border-radius: 9px;
  background: var(--bg-input);
}
.game-core-policy-control { min-width: 0; }
.game-core-policy-card :deep(.toggle-row) { min-height: 54px; box-sizing: border-box; padding: 0 0 5px; align-items: center; }
.game-core-policy-card :deep(.toggle-label) { font-size: 12px; }
.game-core-policy-card :deep(.toggle-desc) {
  white-space: normal;
  line-height: 1.25;
}
.game-core-policy-picker { display: grid; grid-template-rows: auto minmax(34px, auto); align-content: end; gap: 4px; min-width: 0; width: 100%; }
.game-core-policy-picker > small { color: var(--text-dim); font-size: 10px; }
.game-core-policy-picker :deep(.dd), .game-core-policy-picker :deep(.dd-trigger) { width: 100%; }
.core-policy-status { overflow: hidden; text-overflow: ellipsis; }
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


