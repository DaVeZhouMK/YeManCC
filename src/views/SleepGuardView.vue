<script setup lang="ts">
import { ref, reactive, computed, inject, watch, nextTick, onMounted, onBeforeUnmount, type Ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import Slider from '@/components/Slider.vue';
import SegButton from '@/components/SegButton.vue';
import Dropdown from '@/components/Dropdown.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
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
  sleepFactsGet,
  sleepFactsSetEnabled,
  sleepFactsOpenLog,
  type SleepGuardStatus,
  type SleepFactStatus,
  type SleepTimeoutSettings,
  type PowerBtnIdx,
} from '@/bridge/yeman';

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
  if (!timeoutExpanded.value) return;
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

const busy = ref(false);
const msg = ref('');
const activeSchemeGuid = ref('');
const sleepFacts = ref<SleepFactStatus | null>(null);
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
});

onMounted(() => {
  window.addEventListener('ipc:gamepad-back', onTimeoutGamepadBack);
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

async function refresh() {
  // 与整页其他检测解耦：AC/DC、电源方案或内存 IPC 异常不能拖死系统休眠开关。
  const hibernateRefresh = refreshHibernateState();
  try {
    const s = await sleepGuardGet();
    cfg.enabled = s.enabled;
    cfg.mode = s.mode;
    cfg.suspended = s.suspended;
    cfg.pauseGameOnSleep = s.pauseGameOnSleep;
    cfg.retryOnEntryFailure = s.retryOnEntryFailure;
    cfg.retryOnNonUserWake = s.retryOnNonUserWake;
    cfg.joyXoffAutoClose = true;
  } catch {
    /* 检测不到保持现状，绝不假定关闭 */
  }
  try {
    sleepPowerPlanOptimizationEnabled.value = await getSleepPowerPlanOptimizationEnabled();
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
  await refreshSleepFacts();
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

async function refreshSleepFacts() {
  try {
    sleepFacts.value = await sleepFactsGet();
  } catch {
    // The monitor is diagnostic-only; it must not make the sleep settings page unusable.
  }
}

async function onSleepFactMonitor(enabled: boolean) {
  busy.value = true;
  try {
    sleepFacts.value = await sleepFactsSetEnabled(enabled);
    msg.value = enabled ? '睡眠日志记录已开启' : '睡眠日志记录已关闭';
  } catch (e) {
    msg.value = '睡眠日志记录设置失败: ' + (e as Error).message;
    await refreshSleepFacts();
  } finally {
    busy.value = false;
  }
}

async function openSleepFactLog() {
  try {
    await sleepFactsOpenLog();
  } catch (e) {
    msg.value = '打开睡眠日志失败: ' + (e as Error).message;
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

    <!-- Windows 11 风格：默认折叠，标题区域（含图标）可用鼠标或手柄展开。 -->
    <div
      class="card timeout-card"
      :class="{ expanded: timeoutExpanded }"
      data-gp-timeout-panel
      :data-gp-expanded="timeoutExpanded"
    >
      <button
        type="button"
        class="timeout-header"
        :class="{ active: timeoutExpanded }"
        data-gp-timeout-entry
        :data-gp-row="timeoutExpanded ? -1 : undefined"
        :data-gp-col="timeoutExpanded ? 0 : undefined"
        :aria-expanded="timeoutExpanded"
        aria-controls="sleep-timeout-settings"
        @click="toggleTimeoutExpanded"
      >
        <span class="timeout-header-main">
          <span class="timeout-header-icon"><InlineIcon name="monitor" /></span>
          <span class="timeout-header-copy">
            <span class="timeout-title">屏幕、睡眠和休眠超时</span>
            <span class="timeout-subtitle">选择设备在指定时间内处于空闲状态时会发生什么情况</span>
          </span>
        </span>
        <svg class="timeout-caret" :class="{ open: timeoutExpanded }" viewBox="0 0 24 24" aria-hidden="true">
          <path d="m5 9 7 7 7-7" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" />
        </svg>
      </button>

      <Transition name="timeout-submenu-pop" @before-leave="disableLeavingTimeoutBody">
      <div v-if="timeoutExpanded" id="sleep-timeout-settings" class="timeout-body" data-gp-timeout-body>
        <div class="timeout-grid">
          <div class="timeout-grid-head" aria-hidden="true">
            <span>设置项目</span>
            <span class="timeout-side-ac">已接通电源</span>
            <span class="timeout-side-dc">使用电池</span>
          </div>

          <div class="timeout-row">
            <div class="timeout-label">
              <span>在此时间后关闭我的屏幕</span>
            </div>
            <Dropdown
              :model-value="sleepTimeouts.acScreen ?? -1"
              :options="timeoutOptions"
              width="96px"
              aria-label="已接通电源：关闭屏幕超时"
              placeholder="读取失败"
              gp-row="0"
              gp-col="0"
              :disabled="busy || timeoutLoading || sleepTimeouts.acScreen === null"
              @update:model-value="(v) => onSleepTimeout('screen', true, v)"
            />
            <Dropdown
              :model-value="sleepTimeouts.dcScreen ?? -1"
              :options="timeoutOptions"
              width="96px"
              color="dc"
              aria-label="使用电池：关闭屏幕超时"
              placeholder="读取失败"
              gp-row="0"
              gp-col="1"
              :disabled="busy || timeoutLoading || sleepTimeouts.dcScreen === null"
              @update:model-value="(v) => onSleepTimeout('screen', false, v)"
            />
          </div>

          <div class="timeout-row">
            <div class="timeout-label"><span>使我的设备在以下时间后进入睡眠状态</span></div>
            <Dropdown
              :model-value="sleepTimeouts.acSleep ?? -1"
              :options="timeoutOptions"
              width="96px"
              aria-label="已接通电源：睡眠超时"
              placeholder="读取失败"
              gp-row="1"
              gp-col="0"
              :disabled="busy || timeoutLoading || sleepTimeouts.acSleep === null"
              @update:model-value="(v) => onSleepTimeout('sleep', true, v)"
            />
            <Dropdown
              :model-value="sleepTimeouts.dcSleep ?? -1"
              :options="timeoutOptions"
              width="96px"
              color="dc"
              aria-label="使用电池：睡眠超时"
              placeholder="读取失败"
              gp-row="1"
              gp-col="1"
              :disabled="busy || timeoutLoading || sleepTimeouts.dcSleep === null"
              @update:model-value="(v) => onSleepTimeout('sleep', false, v)"
            />
          </div>

          <div class="timeout-row">
            <div class="timeout-label"><span>使我的设备在以下时间后休眠</span></div>
            <Dropdown
              :model-value="sleepTimeouts.acHibernate ?? -1"
              :options="timeoutOptions"
              width="96px"
              aria-label="已接通电源：休眠超时"
              placeholder="读取失败"
              gp-row="2"
              gp-col="0"
              :disabled="busy || timeoutLoading || sleepTimeouts.acHibernate === null"
              @update:model-value="(v) => onSleepTimeout('hibernate', true, v)"
            />
            <Dropdown
              :model-value="sleepTimeouts.dcHibernate ?? -1"
              :options="timeoutOptions"
              width="96px"
              color="dc"
              aria-label="使用电池：休眠超时"
              placeholder="读取失败"
              gp-row="2"
              gp-col="1"
              :disabled="busy || timeoutLoading || sleepTimeouts.dcHibernate === null"
              @update:model-value="(v) => onSleepTimeout('hibernate', false, v)"
            />
          </div>
        </div>
      </div>
      </Transition>
    </div>

    <!-- 睡眠操作（总开关已移除，子项直接驱动 guard，始终显示） -->
    <div class="card">
      <h3 class="card-title"><InlineIcon name="sleep" /> 睡眠操作</h3>
      <Toggle
        :model-value="cfg.pauseGameOnSleep"
        label="睡眠时暂停游戏"
        description="只冻结本轮标记的游戏 PID，唤醒时只恢复同一批 PID"
        :disabled="busy"
        @update:model-value="onPauseGame"
      />
      <Toggle
        :model-value="sleepPowerPlanOptimizationEnabled"
        label="睡眠电源计划优化"
        description="开启后在程序启动30秒及电源变更时后台优化睡眠相关电源计划"
        :disabled="busy"
        @update:model-value="onSleepPowerPlanOptimization"
      />
      <Toggle
        :model-value="cfg.retryOnEntryFailure"
        label="睡眠失效强制睡眠"
        description="当睡眠发起后触发报错无法睡眠，将强制入睡"
        :disabled="busy"
        @update:model-value="onEntryFailure"
      />
      <Toggle
        :model-value="cfg.retryOnNonUserWake"
        label="意外唤醒重睡"
        description="插入供电和非用户唤醒"
        :disabled="busy"
        @update:model-value="onNonUserWake"
      />
      <Toggle
        :model-value="sleepFacts?.enabled === true"
        label="睡眠日志记录"
        description="记录 Kernel-Power、AC/DC 与设备变化；开启后，任何状态的 device-change code=7 都会写入日志"
        :disabled="busy"
        @update:model-value="onSleepFactMonitor"
      />
      <div class="fact-actions">
        <button class="mini-btn" :disabled="busy" @click="openSleepFactLog">显示日志</button>
        <span class="fact-path">{{ sleepFacts?.logPath || 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\sleep-facts.log' }}</span>
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
.fact-actions { display: flex; align-items: center; gap: 10px; padding-top: 8px; }
.fact-path { min-width: 0; color: var(--text-dim); font-size: 10px; overflow-wrap: anywhere; }

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

/* Windows 11 风格的“屏幕、睡眠和休眠超时”二级气泡 */
.timeout-card { padding: 0; background: transparent; overflow: visible; }
.timeout-header {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  min-height: 54px;
  padding: 10px 12px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  text-align: left;
  font: inherit;
  box-shadow: 0 12px 30px rgba(0,0,0,.28);
  cursor: pointer;
}
.timeout-header.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
}
.timeout-header:hover { background: color-mix(in srgb, var(--bg-input) 72%, transparent); }
.timeout-header:focus-visible {
  outline: none;
  box-shadow: inset 0 0 0 2px var(--accent), 0 12px 30px rgba(0,0,0,.28);
}
.timeout-header.focused {
  box-shadow: inset 0 0 0 2px var(--accent), inset 0 0 10px color-mix(in srgb, var(--accent) 35%, transparent);
}
.timeout-header-main { display: flex; align-items: center; gap: 12px; min-width: 0; }
.timeout-header-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  flex: 0 0 auto;
  color: var(--text-dim);
}
.timeout-header-copy { min-width: 0; display: flex; flex-direction: column; gap: 3px; }
.timeout-title { font-size: 13px; font-weight: 600; line-height: 1.25; }
.timeout-subtitle { color: var(--text-dim); font-size: 11px; line-height: 1.35; }
.timeout-caret { width: 18px; height: 18px; flex: 0 0 auto; color: var(--text-dim); transition: transform .18s ease, color .18s ease; }
.timeout-caret.open { transform: rotate(180deg); color: var(--accent); }
.timeout-body {
  margin: 8px 0 0;
  padding: 8px;
  border: 1px solid rgba(255,255,255,.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  box-shadow: 0 16px 40px rgba(0,0,0,.4);
}
.timeout-submenu-pop-enter-active,
.timeout-submenu-pop-leave-active {
  transition: opacity .16s ease, transform .16s ease;
  transform-origin: top center;
}
.timeout-submenu-pop-enter-from,
.timeout-submenu-pop-leave-to {
  opacity: 0;
  transform: translateY(-5px) scale(.985);
}
.timeout-grid {
  display: grid;
  grid-template-columns: minmax(0, 1fr);
  margin-top: 10px;
  background: color-mix(in srgb, var(--bg-input) 52%, transparent);
  border-radius: var(--radius-ctrl);
  overflow: hidden;
}
.timeout-grid-head,
.timeout-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(96px, 96px) minmax(96px, 96px);
  align-items: center;
  gap: 12px;
  padding: 9px 12px;
}
.timeout-grid-head {
  padding-top: 11px;
  padding-bottom: 8px;
  color: var(--text-dim);
  font-size: 11px;
  font-weight: 600;
}
.timeout-grid-head span:not(:first-child) { text-align: center; }
.timeout-side-ac { color: var(--accent); }
.timeout-side-dc { color: var(--dc-accent); }
.timeout-row {
  min-height: 58px;
  border-top: 1px solid color-mix(in srgb, var(--text-dim) 14%, transparent);
}
.timeout-row :deep(.dd) { width: 100% !important; }
.timeout-label { min-width: 0; flex: 1 1 auto; display: flex; flex-direction: column; gap: 3px; }
.timeout-label span { color: var(--text); font-size: 12px; line-height: 1.35; }
.timeout-label small { color: var(--text-dim); font-size: 10px; line-height: 1.3; }

.confirm-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  border-radius: var(--radius-ctrl);
  padding: 12px 14px;
  margin-bottom: 10px;
  font-size: 13px;
  line-height: 1.5;
}
.confirm-text {
  color: #ff9ea1;
  display: block;
  margin-bottom: 10px;
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
  border-radius: 7px;
  padding: 8px 12px;
  min-height: 36px;
  font-size: 13px;
  cursor: pointer;
}
.mini-btn.danger {
  background: rgba(229, 72, 77, 0.85);
  color: #fff;
  border-color: rgba(229, 72, 77, 0.85);
}

@media (max-width: 520px) {
  .timeout-grid-head,
  .timeout-row { grid-template-columns: minmax(0, 1fr) minmax(96px, 96px) minmax(96px, 96px); gap: 6px; padding-left: 8px; padding-right: 8px; }
  .timeout-row :deep(.dd-trigger) { min-height: 36px; }
}

</style>
