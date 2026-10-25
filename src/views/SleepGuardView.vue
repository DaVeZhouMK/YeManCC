<script setup lang="ts">
// [AI:USER:SLEEP] 开启睡眠相关选项→为之后真实发生的睡眠/唤醒启用对应行为；保存电源键/超时设置不等于立即睡眠。
// 游戏恢复只针对本轮暂停的原游戏；用户取消或关闭后不能被旧恢复重新接管，未恢复须留具体原因。
// 插电/电池分别设置；系统睡眠、仅关屏与日志推断不能混写，实际执行继续查 native/main.cpp 与 sleep_recovery_*。
import IntegratedPage from '@/components/IntegratedPage.vue';
import SettingsSections from '@/components/SettingsSections.vue';
import { ref, reactive, computed, inject, watch, nextTick, onMounted, onBeforeUnmount, type Ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import Slider from '@/components/Slider.vue';
import SegButton from '@/components/SegButton.vue';
import Dropdown from '@/components/Dropdown.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { focusGamepadElement, getGamepadPopupPlacement } from '@/gamepad/focus';
import { wakePassword } from '@/bridge/api';
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
  readSleepTimeouts,
  setSleepTimeout,
  getSleepPowerPlanOptimizationEnabled,
  setSleepPowerPlanOptimizationEnabled,
  getStaticWakeStatus,
  scanStaticWakeSources,
  restoreStaticWakeSources,
  type StaticWakeStatus,
  type SleepGuardStatus,
  type SleepTimeoutSettings,
  type PowerBtnIdx,
} from '@/bridge/yeman';

const sleepSections=[{id:'protection',title:'睡眠保护',description:'',icon:'sleep'},{id:'power',title:'按键与休眠',description:'',icon:'power'},{id:'timeouts',title:'空闲超时',description:'',icon:'monitor'}];
const powerButtonOptions=[{value:0,label:'睡眠'},{value:1,label:'休眠'},{value:2,label:'不操作'},{value:3,label:'关闭屏幕'}];
const timeoutRows=[{key:'screen',label:'关闭屏幕',icon:'monitor'},{key:'sleep',label:'进入睡眠',icon:'sleep'},{key:'hibernate',label:'进入休眠',icon:'power'}] as const;
const timeoutValue=(key:string,side:'ac'|'dc')=>sleepTimeouts[`${side}${key[0].toUpperCase()}${key.slice(1)}` as keyof SleepTimeoutSettings] ?? -1;
const cfg = reactive<SleepGuardStatus>({
  enabled: false,
  mode: 'off',
  suspended: 0,
  pauseGameOnSleep: true,
  retryOnEntryFailure: true,
  retryOnNonUserWake: true,
  joyXoffAutoClose: true,
});

// 电源按钮：0=S3睡眠, 1=S4休眠, 2=不操作
const acBtnIdx = ref<PowerBtnIdx>(2);
const dcBtnIdx = ref<PowerBtnIdx>(2);
const sleepPowerPlanOptimizationEnabled = ref(true);
let sleepPlanRevision = 0;
const staticWakeStatus = ref<StaticWakeStatus | null>(null);
const staticWakeLoading = ref(false);
const staticWakeAction = ref<'scan' | 'restore' | null>(null);
const staticWakeError = ref('');
let staticWakeRevision = 0;
const staticWakeNeedsRestore = computed(() => !sleepPowerPlanOptimizationEnabled.value &&
  (staticWakeStatus.value?.enabled || (staticWakeStatus.value?.restorePending ?? 0) > 0 ||
    staticWakeStatus.value?.phase === 'restore-pending' || staticWakeError.value !== ''));
const staticWakeSummary = computed(() => {
  if (staticWakeAction.value) return staticWakeAction.value === 'scan' ? '正在扫描并屏蔽 Windows 支持项…' : '正在恢复本功能修改项…';
  if (staticWakeError.value) return staticWakeError.value;
  if (staticWakeLoading.value) return '正在读取屏蔽状态…';
  const s = staticWakeStatus.value;
  if (!s) return '屏蔽状态未知';
  if (!s.enabled && s.phase === 'idle' && !s.restorePending && !s.failed && !s.lastError)
    return s.lastScanEpoch > 0 ? '本功能修改项已恢复' : '尚未扫描';
  const phase = s.phase === 'restore-pending' || s.restorePending > 0 ? '恢复待完成' :
    s.phase === 'partial' || s.failed > 0 ? '部分完成' : s.phase === 'applied' ? '已应用支持项' : '尚未应用';
  // Diagnostic source health belongs to retry repair; it does not decide static coverage.
  const warnings = [s.failed ? '失败 ' + s.failed : '', s.restorePending ? '待恢复 ' + s.restorePending : ''].filter(Boolean);
  return [phase, ...warnings, '设备 ' + s.devices, '软件 ' + s.software, '定时器 ' + s.timers,
    s.protectedDevices ? '保护 ' + s.protectedDevices : '', s.lastError].filter(Boolean).join(' · ');
});
// Default off, but do not read/apply Windows indices until a user toggles it.
// Reuse the existing sleepGuard snapshot for the remembered UI choice only.
const wakePasswordOn = ref(false);
let wakePasswordRevision = 0;

// Windows 11“屏幕、睡眠和休眠超时”卡片：数值统一使用分钟，0=从不。
const timeoutExpanded = ref(false);
const timeoutLoading = ref(true);
const sleepTimeouts = reactive<SleepTimeoutSettings>({
  acScreen: null,
  dcScreen: null,
  acSleep: null,
  dcSleep: null,
  acHibernate: null,
  dcHibernate: null,
});
const timeoutOptions = [
  { value: 0, label: '从不' },
  { value: 1, label: '1 分钟' },
  { value: 2, label: '2 分钟' },
  { value: 3, label: '3 分钟' },
  { value: 5, label: '5 分钟' },
  { value: 10, label: '10 分钟' },
  { value: 15, label: '15 分钟' },
  { value: 20, label: '20 分钟' },
  { value: 25, label: '25 分钟' },
  { value: 30, label: '30 分钟' },
  { value: 45, label: '45 分钟' },
  { value: 60, label: '1 小时' },
  { value: 120, label: '2 小时' },
  { value: 180, label: '3 小时' },
  { value: 240, label: '4 小时' },
  { value: 300, label: '5 小时' },
  { value: 480, label: '8 小时' },
  { value: 720, label: '12 小时' },
  { value: 1440, label: '24 小时' },
  { value: 2880, label: '48 小时' },
  { value: 5760, label: '96 小时' },
];

function focusTimeoutEntry(): void {
  nextTick(() => {
    const entry = document.querySelector<HTMLElement>('[data-gp-timeout-entry]');
    if (entry) focusGamepadElement(entry);
  });
}

function focusOpenedTimeoutBody(): void {
  nextTick(() => {
    const first = document.querySelector<HTMLElement>(
      '[data-gp-timeout-body] [data-gp-dropdown] .dd-trigger:not(:disabled), [data-gp-timeout-body] button:not(:disabled), [data-gp-timeout-body] select:not(:disabled), [data-gp-timeout-body] input:not(:disabled), [data-gp-timeout-body] [tabindex]:not([tabindex="-1"])',
    );
    if (first) focusGamepadElement(first);
  });
}

function disableLeavingTimeoutBody(el: Element): void {
  if (!(el instanceof HTMLElement)) return;
  // Transition 离场期间仍可能有一帧保留 DOM；先把整块内页从手柄/键盘
  // 候选中摘掉，避免关闭瞬间 A 或方向键穿透到旧下拉菜单。
  el.inert = true;
  el.setAttribute('aria-hidden', 'true');
}

function closeTimeoutMenu(): void {
  if (!timeoutExpanded.value) return;
  timeoutExpanded.value = false;
  focusTimeoutEntry();
}

function onTimeoutGamepadBack(event: Event): void {
  if (event.defaultPrevented || !timeoutExpanded.value) return;
  // 下拉选项菜单优先消费 B；只有下拉已关闭时，B 才收起这一层气泡。
  if (document.querySelector('[data-gp-timeout-body] [aria-expanded="true"][aria-haspopup="listbox"]')) return;
  event.preventDefault();
  closeTimeoutMenu();
}

function toggleTimeoutExpanded(event?: MouseEvent): void {
  if (timeoutExpanded.value) {
    closeTimeoutMenu();
    return;
  }
  timeoutExpanded.value = true;
  // 手柄 A / 键盘 Enter 触发的 click detail 为 0；鼠标点击保留原焦点，
  // 避免用户只是展开卡片时鼠标指针突然跳到第一个下拉。
  if (timeoutExpanded.value && event?.detail === 0) {
    focusOpenedTimeoutBody();
  }
}

// 系统休眠
const hibernateOn = ref<boolean | null>(null); // null=未检测（不强制设默认值）
const hibernateSupported = ref<boolean | null>(null); // null=能力未知；未知时仍允许安全开启重试
const hibLoading = ref(true); // 独立加载；失败时结束 loading 并保留“未知但可安全开启”状态
const hibSize = ref(50);
const memGB = ref<number | null>(null); // 物理内存总量（GB），用于预估休眠文件大小
const confirmOff = ref(false); // 关闭休眠二次确认（避免整行误触关闭休眠这种销毁性操作）
function onHibernateConfirmBack(event: Event): void {
  if (event.defaultPrevented || !confirmOff.value || document.querySelector('.dd-open')) return;
  const layers = [...document.querySelectorAll<HTMLElement>('[data-gp-modal]')].filter(el => {
    const rect=el.getBoundingClientRect(), style=getComputedStyle(el);
    return rect.width>0 && rect.height>0 && style.display!=='none' && style.visibility!=='hidden';
  });
  if (!layers[layers.length-1]?.matches('.sleep-confirm')) return;
  event.preventDefault(); cancelHibernateOff();
  void nextTick(() => focusGamepadElement(document.querySelector<HTMLElement>('[aria-label="系统休眠开关"]')));
}


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
  window.removeEventListener('ipc:gamepad-back', onTimeoutGamepadBack);
  window.removeEventListener('ipc:gamepad-back', onHibernateConfirmBack);
});

onMounted(() => {
  window.addEventListener('ipc:gamepad-back', onTimeoutGamepadBack);
  window.addEventListener('ipc:gamepad-back', onHibernateConfirmBack);
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

async function refreshSleepTimeouts() {
  timeoutLoading.value = true;
  try {
    Object.assign(sleepTimeouts, await readSleepTimeouts());
  } catch {
    Object.assign(sleepTimeouts, {
      acScreen: null,
      dcScreen: null,
      acSleep: null,
      dcSleep: null,
      acHibernate: null,
      dcHibernate: null,
    });
  } finally {
    timeoutLoading.value = false;
  }
}

async function onWakePassword(enabled: boolean) {
  if (busy.value) return;
  const previous = wakePasswordOn.value;
  ++wakePasswordRevision; // An older sleep-config refresh must not override ACK.
  busy.value = true;
  msg.value = '';
  try {
    const state = await wakePassword.set(enabled);
    if (!state.ok || !state.known || state.ac !== Number(enabled) || state.dc !== Number(enabled)) {
      wakePasswordOn.value = state.known ? state.enabled : previous;
      msg.value = '唤醒密码设置失败：错误 ' + state.win32Error +
        (state.stage ? '，' + state.stage : '') +
        (state.rollbackOk === false ? '；恢复原值失败，请重试' : '') +
        '；无效可能是组策略锁定了';
      return;
    }
    wakePasswordOn.value = state.enabled;
    if (state.preferenceSaved === false) msg.value = '电源计划已设置，但开关状态记忆保存失败；下次打开可能显示默认值。';
  } catch (e) {
    wakePasswordOn.value = previous;
    msg.value = '唤醒密码设置失败：' + (e as Error).message + '；无效可能是组策略锁定了';
  } finally {
    ++wakePasswordRevision; // Also invalidate snapshots taken during the write.
    busy.value = false;
  }
}

async function refreshStaticWakeStatus() {
  if (staticWakeAction.value) return;
  const revision = ++staticWakeRevision;
  staticWakeLoading.value = true;
  try {
    const status = await getStaticWakeStatus();
    if (revision !== staticWakeRevision) return;
    staticWakeStatus.value = status;
    // A memory snapshot cannot prove a timed-out write has stopped.
    if (!staticWakeError.value.includes('结果未确认')) staticWakeError.value = '';
  } catch {
    if (revision === staticWakeRevision && !staticWakeError.value) staticWakeError.value = '屏蔽状态读取失败';
  } finally {
    if (revision === staticWakeRevision) staticWakeLoading.value = false;
  }
}

async function refresh() {
  // 与整页其他检测解耦：AC/DC、电源方案或内存 IPC 异常不能拖死系统休眠开关。
  const hibernateRefresh = refreshHibernateState();
  void refreshStaticWakeStatus();
  const planRevision = sleepPlanRevision;
  const wakeRevision = wakePasswordRevision;
  try {
    const s = await sleepGuardGet();
    cfg.enabled = s.enabled;
    cfg.mode = s.mode;
    cfg.suspended = s.suspended;
    cfg.pauseGameOnSleep = s.pauseGameOnSleep;
    cfg.retryOnEntryFailure = s.retryOnEntryFailure;
    cfg.retryOnNonUserWake = s.retryOnNonUserWake;
    cfg.joyXoffAutoClose = true;
    if (wakeRevision === wakePasswordRevision && !busy.value) wakePasswordOn.value = s.wakePasswordEnabled === true;
  } catch {
    /* 检测不到保持现状，绝不假定关闭 */
  }
  try {
    const enabled = await getSleepPowerPlanOptimizationEnabled();
    if (planRevision === sleepPlanRevision) sleepPowerPlanOptimizationEnabled.value = enabled;
  } catch {
    /* 配置读取失败保持现状，绝不假定关闭 */
  }
  // 并行加载电源按钮、系统休眠状态
  const [schemeRes, acBtnRes, dcBtnRes, hibSizeRes, memRes, timeoutRes] = await Promise.allSettled([
    getActiveScheme(),
    getPowerBtnIdx(true),
    getPowerBtnIdx(false),
    readHibernateSize(),
    readTotalMemoryGB(),
    refreshSleepTimeouts(),
  ]);
  if (schemeRes.status === 'fulfilled') activeSchemeGuid.value = schemeRes.value.toLowerCase();
  if (acBtnRes.status === 'fulfilled') acBtnIdx.value = acBtnRes.value;
  if (dcBtnRes.status === 'fulfilled') dcBtnIdx.value = dcBtnRes.value;
  if (hibSizeRes.status === 'fulfilled') hibSize.value = hibSizeRes.value;
  if (memRes.status === 'fulfilled') memGB.value = memRes.value;
  if (timeoutRes.status === 'rejected') timeoutLoading.value = false;
  await hibernateRefresh;
}

async function onSleepTimeout(field: 'screen' | 'sleep' | 'hibernate', ac: boolean, value: string | number) {
  const key = `${ac ? 'ac' : 'dc'}${field[0].toUpperCase()}${field.slice(1)}` as keyof SleepTimeoutSettings;
  const previous = sleepTimeouts[key];
  const next = Number(value);
  if (!Number.isFinite(next)) return;
  sleepTimeouts[key] = next;
  busy.value = true;
  try {
    await setSleepTimeout(field, ac, next);
    msg.value = '';
  } catch (e) {
    sleepTimeouts[key] = previous;
    msg.value = '屏幕、睡眠和休眠超时设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// 睡眠时暂停游戏（与唤醒自动恢复绑定）。
async function onPauseGame(v: boolean) {
  cfg.pauseGameOnSleep = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseGameOnSleep: v, retryOnEntryFailure: cfg.retryOnEntryFailure, retryOnNonUserWake: cfg.retryOnNonUserWake });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onEntryFailure(v: boolean) {
  cfg.retryOnEntryFailure = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseGameOnSleep: cfg.pauseGameOnSleep, retryOnEntryFailure: v, retryOnNonUserWake: cfg.retryOnNonUserWake });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function onNonUserWake(v: boolean) {
  cfg.retryOnNonUserWake = v;
  busy.value = true;
  try {
    await sleepGuardSetConfig({ mode: 'custom', pauseGameOnSleep: cfg.pauseGameOnSleep, retryOnEntryFailure: cfg.retryOnEntryFailure, retryOnNonUserWake: v });
    await syncGuardEnabled();
  } catch {
    msg.value = '设置保存失败';
    await refresh();
  } finally {
    busy.value = false;
  }
}

async function applyStaticWake(action: 'scan' | 'restore') {
  ++staticWakeRevision; // Invalidate reads started before this explicit action.
  staticWakeLoading.value = false;
  staticWakeAction.value = action;
  staticWakeError.value = '';
  try {
    const result = await (action === 'scan' ? scanStaticWakeSources() : restoreStaticWakeSources());
    staticWakeStatus.value = result.status;
    if (!result.ok) staticWakeError.value = (action === 'scan' ? '屏蔽未完成' : '恢复未完成') +
      ' · 失败 ' + result.status.failed + ' · 待恢复 ' + result.status.restorePending +
      ' · 设备 ' + result.status.devices + ' · 软件 ' + result.status.software + ' · 定时器 ' + result.status.timers +
      (result.status.lastError ? ' · ' + result.status.lastError : '');
  } catch {
    // A local IPC timeout does not cancel native. Never retry/scan in the background.
    staticWakeError.value = action === 'scan' ? '扫描结果未确认，请查看普通日志' : '恢复结果未确认，请查看普通日志';
  } finally {
    staticWakeAction.value = null;
  }
}

async function onStaticWakeAction() {
  if (busy.value || staticWakeLoading.value || staticWakeError.value.includes('结果未确认')) return;
  const action = staticWakeNeedsRestore.value ? 'restore' : 'scan';
  if (action === 'scan' && !sleepPowerPlanOptimizationEnabled.value) return;
  busy.value = true;
  try { await applyStaticWake(action); }
  finally { busy.value = false; }
}

async function onSleepPowerPlanOptimization(v: boolean) {
  if (busy.value || staticWakeError.value.includes('结果未确认')) return;
  ++sleepPlanRevision;
  busy.value = true;
  try {
    await setSleepPowerPlanOptimizationEnabled(v);
    sleepPowerPlanOptimizationEnabled.value = v;
    msg.value = '';
    // Keep the original preference/boot/power-change repair path. Enabling it
    // never scans; disabling it also restores only native-owned static changes.
    if (!v) await applyStaticWake('restore');
  } catch {
    msg.value = '设置保存失败';
  } finally {
    ++sleepPlanRevision;
    busy.value = false;
  }
}

// 总闸（Enable.txt）：任一子功能生效即开启，全部关闭即关闭
async function syncGuardEnabled() {
  const active = cfg.pauseGameOnSleep || cfg.retryOnEntryFailure || cfg.retryOnNonUserWake;
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
  await nextTick();
  focusGamepadElement(document.querySelector<HTMLElement>('[data-sleep-hibernate-cancel]'));
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
  if (hibernateSupported.value === false) return '此设备不支持休眠';
  if (hibernateOn.value === null) return '状态未知，可点击重新检测';
  if (memGB.value !== null) {
    const est = (memGB.value * hibSize.value) / 100;
    return `预计占用 ${est.toFixed(1)} GB`;
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
 <IntegratedPage title="睡眠优化">
  <div class="sleep-console">
   <Transition name="notice-fade"><div v-if="msg" class="sleep-notice" :class="{'is-ok':/已启用|已关闭|已恢复|已暂停|完成/.test(msg)}" role="status">{{msg}}</div></Transition>
   <SettingsSections :sections="sleepSections" label="睡眠优化">
    <template #protection>
     <div class="sleep-group">
      <div class="sleep-group-head"><h3><InlineIcon name="gamepad"/>游戏恢复</h3><span>入睡暂停，唤醒恢复</span></div>
      <Toggle :model-value="cfg.pauseGameOnSleep" label="睡眠暂停游戏" description="唤醒后恢复原游戏" :disabled="busy" @update:model-value="onPauseGame" />
     </div>
     <div class="sleep-group">
      <div class="sleep-group-head"><h3><InlineIcon name="sleep"/>系统保护</h3><span>按需开启</span></div>
      <Toggle :model-value="wakePasswordOn" label="唤醒需要密码" description="唤醒后验证登录身份" :disabled="busy" @update:model-value="onWakePassword" />
      <Toggle :model-value="sleepPowerPlanOptimizationEnabled" label="睡眠电源修复" description="启动及电源变动时修正睡眠设置" :disabled="busy||staticWakeError.includes('结果未确认')" @update:model-value="onSleepPowerPlanOptimization" />
      <div class="sleep-static-wake">
       <div class="sleep-static-action"><button :disabled="busy||staticWakeLoading||staticWakeError.includes('结果未确认')||(!sleepPowerPlanOptimizationEnabled&&!staticWakeNeedsRestore)" @click="onStaticWakeAction">{{staticWakeAction==='scan'?'扫描中…':staticWakeAction==='restore'?'恢复中…':staticWakeNeedsRestore?'重试恢复':'扫描并屏蔽'}}</button><span>Windows 支持项，人工电源键优先</span></div>
       <p role="status" aria-label="静态唤醒屏蔽状态" :title="staticWakeSummary">{{staticWakeSummary}}</p>
      </div>
      <Toggle :model-value="cfg.retryOnEntryFailure" label="睡眠异常重睡" description="持续异常时重睡，超时休眠或关机" :disabled="busy" @update:model-value="onEntryFailure" />
      <Toggle :model-value="cfg.retryOnNonUserWake" label="意外唤醒重睡" description="电源变动唤醒后重睡，电源键唤醒优先" :disabled="busy" @update:model-value="onNonUserWake" />
     </div>
    </template>
    <template #power>
     <div class="sleep-group">
      <div class="sleep-group-head"><h3><InlineIcon name="power"/>电源键操作</h3><span>插电与电池分别设置</span></div>
      <div class="sleep-power-grid">
       <label><span class="supply-label ac"><InlineIcon name="plug"/>插电 · AC</span><Dropdown :model-value="acBtnIdx" :options="powerButtonOptions" width="100%" aria-label="插电时电源按钮操作" :disabled="busy||!isYemanScheme" @update:model-value="v=>onPwrBtn(true,Number(v) as PowerBtnIdx)"/></label>
       <label><span class="supply-label dc"><InlineIcon name="battery"/>电池 · DC</span><Dropdown :model-value="dcBtnIdx" :options="powerButtonOptions" width="100%" color="dc" aria-label="电池时电源按钮操作" :disabled="busy||!isYemanScheme" @update:model-value="v=>onPwrBtn(false,Number(v) as PowerBtnIdx)"/></label>
      </div>
      <p v-if="!isYemanScheme" class="sleep-warning">使用野蛮电源计划后可修改按键</p>
      <p v-if="acBtnIdx===0||dcBtnIdx===0" class="sleep-warning">USB4 电源变动可能触发唤醒</p>
     </div>
     <div class="sleep-group">
      <div class="sleep-group-head"><h3><InlineIcon name="sleep"/>系统休眠</h3><span>保存会话到硬盘</span></div>
      <Toggle :model-value="hibernateOn===true" label="系统休眠开关" :description="hibDesc" :disabled="busy||hibLoading||hibernateSupported===false" @update:model-value="onHibernate"/>
      <div v-if="confirmOff" class="sleep-confirm" role="alertdialog" aria-modal="true" aria-label="关闭系统休眠确认" data-gp-modal data-gp-page-lock>
       <strong>关闭系统休眠？</strong><p>删除休眠文件，休眠按键将失效</p>
       <div><button class="sleep-danger" data-gp-row="0" data-gp-col="0" @click="confirmHibernateOff">确认关闭</button><button data-sleep-hibernate-cancel data-gp-row="0" data-gp-col="1" @click="cancelHibernateOff">取消</button></div>
      </div>
      <div class="sleep-hibernate-size"><Slider v-model="hibSize" :min="30" :max="100" :step="5" label="休眠文件大小" unit="%" :disabled="!hibernateOn" @commit="onHibSize"/><div class="sleep-scale"><span>30%</span><span v-if="memGB">物理内存 {{memGB.toFixed(0)}} GB</span><span>100%</span></div></div>
     </div>
    </template>
    <template #timeouts>
     <div class="sleep-group-head"><h3><InlineIcon name="monitor"/>空闲超时</h3></div>
     <div class="sleep-time-table" role="group" aria-label="插电与电池空闲超时">
      <div class="sleep-time-head"><span>设置项目</span><span class="ac"><InlineIcon name="plug"/>插电</span><span class="dc"><InlineIcon name="battery"/>电池</span></div>
      <div v-for="row in timeoutRows" :key="row.key" class="sleep-time-row">
       <span class="sleep-time-label"><InlineIcon :name="row.icon"/>{{row.label}}</span>
       <Dropdown :model-value="timeoutValue(row.key,'ac')" :options="timeoutOptions" width="100%" :aria-label="'已接通电源：'+row.label+'超时'" :disabled="busy||timeoutLoading||!isYemanScheme||(row.key==='hibernate'&&!hibernateOn)" @update:model-value="v=>onSleepTimeout(row.key,true,v)"/>
       <Dropdown :model-value="timeoutValue(row.key,'dc')" :options="timeoutOptions" width="100%" color="dc" :aria-label="'使用电池：'+row.label+'超时'" :disabled="busy||timeoutLoading||!isYemanScheme||(row.key==='hibernate'&&!hibernateOn)" @update:model-value="v=>onSleepTimeout(row.key,false,v)"/>
      </div>
     </div>
     <div class="sleep-time-help"><InlineIcon name="monitor"/><span>设备空闲时计时，选择“从不”可关闭</span></div>
     <p v-if="!isYemanScheme" class="sleep-warning">使用野蛮电源计划后可修改超时</p>
     <p v-if="!hibernateOn" class="sleep-warning">开启系统休眠后可设置休眠超时</p>
    </template>
   </SettingsSections>
  </div>
 </IntegratedPage>
</template>
<style scoped>
.sleep-console{min-width:0;font-size:15px}
.sleep-console :deep(.toggle-label),.sleep-console :deep(.toggle-desc){white-space:nowrap;overflow:hidden;text-overflow:ellipsis;overflow-wrap:normal}
.sleep-group-head h3,.sleep-group-head>span,.sleep-warning,.sleep-time-help>span,.sleep-confirm p{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sleep-time-help>span{min-width:0}

.sleep-console :deep(.settings-section-heading){display:none}
.sleep-console :deep(.settings-section-tabs){gap:6px;margin-bottom:12px}
.sleep-console :deep(.settings-section-tabs button){font-size:15px;min-height:42px;padding:8px 5px}
.sleep-console :deep(.settings-section-panel){padding:14px;background:var(--console-surface);border-color:var(--console-line);border-radius:12px;backdrop-filter:blur(12px)}
.sleep-group+.sleep-group{margin-top:13px;padding-top:14px;border-top:1px solid var(--console-line)}
.sleep-group-head{display:flex;align-items:center;justify-content:space-between;gap:8px;min-height:28px;margin-bottom:5px}
.sleep-group-head h3{display:flex;align-items:center;gap:8px;margin:0!important;font-size:16px!important;font-weight:650;color:var(--text)}
.sleep-group-head h3 :deep(svg){color:var(--accent)}
.sleep-group-head>span{font-size:12px;white-space:nowrap;color:var(--text-dim)}
.sleep-console :deep(.toggle-row){min-height:63px;padding:10px 0;gap:12px;border-radius:5px}
.sleep-console :deep(.toggle-label){font-size:15.5px;line-height:1.45;font-weight:550}
.sleep-console :deep(.toggle-desc){font-size:12.5px;line-height:1.5;margin-top:3px}
.sleep-console :deep(.sleep-recovery-auto){border-top:1px solid var(--console-line)}
.sleep-console :deep(.toggle-row+.toggle-row){border-top:1px solid var(--console-line)}
.sleep-console :deep(.dd-trigger){font-size:15px;min-height:40px;padding:8px 10px;border-color:var(--console-line);background:var(--console-surface-alt)}
.sleep-static-wake{padding:0 0 12px;min-width:0}
.sleep-static-action{display:flex;align-items:center;gap:10px;min-width:0}
.sleep-static-action button{flex:none;min-height:36px;padding:7px 12px;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt);color:var(--text);font:inherit;font-size:13px;cursor:pointer}
.sleep-static-action button:disabled{opacity:.5;cursor:default}
.sleep-static-action span,.sleep-static-wake p{min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-size:12px;line-height:1.5;color:var(--text-dim)}
.sleep-static-wake p{margin:6px 0 0}
.sleep-power-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:12px 0 8px}
.sleep-power-grid label{min-width:0;display:grid;gap:8px}
.supply-label{display:flex;align-items:center;gap:6px;font-size:14px;font-weight:600}
.ac{color:var(--accent)}.dc{color:var(--dc-accent)}
.sleep-warning{font-size:12.5px!important;line-height:1.55!important;color:var(--accent-2,#e8b94f);margin:10px 0 0;padding:9px 10px;background:color-mix(in srgb,var(--accent-2,#e8b94f) 8%,transparent);border-radius:8px}
.sleep-hibernate-size{padding-top:14px;margin-top:6px;border-top:1px solid var(--console-line)}
.sleep-hibernate-size :deep(.slider-label){font-size:15px}.sleep-scale{display:flex;justify-content:space-between;gap:8px;color:var(--text-dim);font-size:12px;margin-top:5px}
.sleep-time-table{min-width:0;margin-top:10px}
.sleep-time-head,.sleep-time-row{display:grid;grid-template-columns:minmax(90px,.88fr) repeat(2,minmax(0,1fr));align-items:center;gap:10px}
.sleep-time-head{font-size:13px;color:var(--text-dim);padding:8px 0 12px}
.sleep-time-head>span:not(:first-child){display:flex;align-items:center;justify-content:center;gap:5px;font-size:14px;font-weight:600}
.sleep-time-row{padding:15px 0;border-top:1px solid var(--console-line);min-height:72px}
.sleep-time-label{display:flex;align-items:center;gap:5px;font-size:14px;white-space:nowrap}
.sleep-time-label :deep(svg){color:var(--text-dim);width:15px;height:15px}
.sleep-time-row :deep(.dd-trigger){font-size:14px;padding:8px;min-height:40px}
.sleep-time-help{display:flex;align-items:flex-start;gap:7px;margin-top:10px;color:var(--text-dim);font-size:12.5px;line-height:1.6;border-radius:8px;padding:10px;background:var(--console-surface-alt)}
.sleep-time-help :deep(svg){width:15px;height:15px;margin-top:3px;flex-shrink:0}
.sleep-confirm{padding:12px;border:1px solid color-mix(in srgb,var(--danger) 45%,transparent);border-radius:9px;background:color-mix(in srgb,var(--danger) 8%,var(--bg-panel));margin:8px 0}
.sleep-confirm strong{font-size:15px}.sleep-confirm p{font-size:13px;color:var(--text-dim);line-height:1.5;margin:6px 0 12px}
.sleep-confirm>div{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.sleep-confirm button{min-height:38px;font-size:14px;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt);color:var(--text)}
.sleep-confirm button.sleep-danger{color:var(--danger);border-color:var(--danger)}
.sleep-notice{padding:10px 12px;margin-bottom:10px;font-size:13px;line-height:1.55;color:var(--danger);border:1px solid currentColor;border-radius:9px;background:var(--bg-panel)}
.sleep-notice.is-ok{color:var(--ok)}
@media(min-height:780px){.sleep-console :deep(.toggle-row){min-height:68px}}
</style>
