<script setup lang="ts">
import { ref, nextTick, onMounted, onBeforeUnmount, onActivated, onDeactivated, inject, watch, type Ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { dialog, shell } from '@/bridge/api';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { focusGamepadElement } from '@/gamepad/focus';
import {
  STEAM_ADDONS,
  type SteamAddonKey,
  type SteamCustomAddon,
  steamAddonExists,
  steamAddonSet,
  steamCustomAddonAdd,
  steamCustomAddonRemove,
  steamCustomAddonSet,
  steamCustomAddons,
  steamExeExists,
  steamRunning,
  steamStop,
  launchSteam,
} from '@/bridge/yeman';
import {
  type CustomSteamLibrarySummary,
  launchCustomSteamLibrary,
  readCustomSteamLibrarySummary,
} from '@/bridge/customSteamLibrary';

const running = ref(false);
const addonStates: Record<string, boolean> = {};
const states = ref({ ...addonStates });

const busy = ref(false);
const errMsg = ref('');
const customAddons = ref<SteamCustomAddon[]>([]);
const steamRunKnown = ref(false);
const steamChecking = ref(false);
const addonsExpanded = ref(false);
const steamLaunchPopupOpen = ref(false);
const steamStateButtonEl = ref<HTMLButtonElement | null>(null);
const steamLaunchPopupPanelEl = ref<HTMLElement | null>(null);
const steamLaunchPopupCancelEl = ref<HTMLButtonElement | null>(null);
let noticeTimer: ReturnType<typeof setTimeout> | null = null;
let steamPollTimer: ReturnType<typeof setInterval> | null = null;
let steamPollBusy = false;
let steamPollGeneration = 0;
let steamActive = false;
let stopUiVisibility: (() => void) | null = null;
const customLibrarySummary = ref<CustomSteamLibrarySummary | null>(null);
const customLibraryBusy = ref(false);
const customLibraryRunning = ref(false);

function restoreSteamStateFocus() {
  nextTick(() => {
    if (steamStateButtonEl.value && !steamStateButtonEl.value.disabled) {
      focusGamepadElement(steamStateButtonEl.value);
    }
  });
}

function closeSteamLaunchPopup(restoreFocus = true) {
  if (!steamLaunchPopupOpen.value) return;
  steamLaunchPopupOpen.value = false;
  if (restoreFocus) restoreSteamStateFocus();
}

function openSteamLaunchPopup() {
  if (busy.value || steamChecking.value) return;
  if (steamLaunchPopupOpen.value) {
    closeSteamLaunchPopup();
    return;
  }
  steamLaunchPopupOpen.value = true;
  nextTick(() => focusGamepadElement(steamLaunchPopupCancelEl.value));
}

function onSteamLaunchPopupBack(e: Event) {
  if (!steamLaunchPopupOpen.value) return;
  e.preventDefault();
  closeSteamLaunchPopup();
}

function handleSteamLaunchPopupEsc() {
  closeSteamLaunchPopup();
}

function cancelSteamLaunchPopup() {
  closeSteamLaunchPopup();
}

const STEAM_POLL_INTERVAL_MS = 2000;
const STEAM_POLL_TIMEOUT_MS = 20000;
function showNotice(message: string) {
  if (noticeTimer) clearTimeout(noticeTimer);
  errMsg.value = message;
  noticeTimer = setTimeout(() => {
    errMsg.value = '';
    noticeTimer = null;
  }, 5000);
}

async function detectSteamRunning(timeoutMs = 1500): Promise<boolean | null> {
  try {
    return await Promise.race([
      steamRunning(),
      new Promise<null>((resolve) => setTimeout(() => resolve(null), timeoutMs)),
    ]);
  } catch {
    return null;
  }
}

async function refreshSteamState() {
  const live = await detectSteamRunning();
  if (live !== null) {
    running.value = live;
    steamRunKnown.value = true;
  }
  return live;
}

function stopSteamPolling() {
  steamPollGeneration += 1;
  if (steamPollTimer) {
    clearInterval(steamPollTimer);
    steamPollTimer = null;
  }
  steamPollBusy = false;
  steamChecking.value = false;
}

function pollSteamState(
  target: boolean,
  onTimeout: () => void,
  onMatched: () => void = () => {},
) {
  stopSteamPolling();
  const generation = steamPollGeneration;
  const startedAt = Date.now();
  steamChecking.value = true;

  const check = async () => {
    if (!steamActive || !isUiVisible() || generation !== steamPollGeneration || steamPollBusy) return;
    steamPollBusy = true;
    try {
      const live = await refreshSteamState();
      if (generation !== steamPollGeneration) return;
      if (live === target) {
        stopSteamPolling();
        onMatched();
        return;
      }
      if (Date.now() - startedAt >= STEAM_POLL_TIMEOUT_MS) {
        stopSteamPolling();
        onTimeout();
      }
    } finally {
      steamPollBusy = false;
    }
  };

  // 立即检测一次，之后严格每 2 秒检测，避免点击后额外等待一轮。
  steamPollTimer = setInterval(check, STEAM_POLL_INTERVAL_MS);
  void check();
}

// 并行异步加载；状态检测失败时保持未确认，不把 Steam 错误显示为运行中。
async function refresh() {
  const [runRes, customRes, ...addonRes] = await Promise.allSettled([
    detectSteamRunning(),
    steamCustomAddons(),
    ...STEAM_ADDONS.map((a) => steamAddonExists(a.key).catch(() => false)),
  ]);
  if (runRes.status === 'fulfilled' && runRes.value !== null) {
    running.value = runRes.value;
  } else {
    // 初始状态检测失败时不假装运行中；允许用户点击启动，点击时再给临时提示。
    running.value = false;
  }
  steamRunKnown.value = true;
  if (customRes.status === 'fulfilled') customAddons.value = customRes.value;
  const next: Record<string, boolean> = {};
  STEAM_ADDONS.forEach((a, i) => {
    next[a.key] = addonRes[i]?.status === 'fulfilled' ? addonRes[i].value : false;
  });
  states.value = next;
}

// 点击启动前只做一次真实检测；检测失败时不亮起运行状态，也不产生常驻提示。
async function canStartSteam(): Promise<boolean> {
  const live = await detectSteamRunning();
  if (live === null) {
    // 检测失败时不改变已有状态，避免把未知状态误显示为运行中或未运行。
    steamRunKnown.value = false;
    showNotice('Steam 运行状态验证失败，请稍后重试。');
    return false;
  }
  steamRunKnown.value = true;
  running.value = live;
  if (live) {
    showNotice('Steam 已经启动，无法重复启动，请先关闭 Steam。');
    return false;
  }
  return true;
}

async function launch() {
  if (busy.value || steamChecking.value) return;
  busy.value = true;
  try {
    if (!(await canStartSteam())) {
      busy.value = false;
      return;
    }
    await launchSteam();
    // 启动命令返回不等于 Steam 已运行，只有真实检测到 steam.exe 才点亮状态。
    pollSteamState(
      true,
      () => {
        busy.value = false;
        showNotice('Steam 启动后暂未检测到运行进程，请稍后确认。');
      },
      () => { busy.value = false; },
    );
  } catch (e) {
    showNotice('Steam 启动失败：' + (e as Error).message);
    busy.value = false;
  }
}

async function onAddon(key: SteamAddonKey, v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    if (v) {
      const exeOk = await steamExeExists(key);
      if (!exeOk) {
        const cfg = STEAM_ADDONS.find((a) => a.key === key);
        errMsg.value = '未检测到：' + (cfg?.name ?? key) + '\n请先安装对应程序。';
        states.value = { ...states.value, [key]: false };
        return;
      }
    }
    await steamAddonSet(key, v);
    states.value = { ...states.value, [key]: v };
  } catch (e) {
    states.value = { ...states.value, [key]: !v };
    errMsg.value = '写入失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function addCustomAddon() {
  errMsg.value = '';
  try {
    const picked = await dialog.openFile([
      { name: '所有程序文件', extensions: ['*'] },
      { name: '可执行程序', extensions: ['exe'] },
      { name: '快捷方式', extensions: ['lnk'] },
      { name: '脚本/批处理', extensions: ['bat', 'cmd', 'ps1'] },
    ]);
    if (!picked) return;
    let added = await steamCustomAddonAdd(picked);
    if (!added.enabled) {
      await steamCustomAddonSet(added, true);
      added = { ...added, enabled: true };
    }
    customAddons.value = [...customAddons.value.filter((a) => a.id !== added.id), added];
  } catch (e) {
    errMsg.value = '添加联动项失败：' + (e as Error).message;
  }
}

async function launchCustomAddon(addon: SteamCustomAddon) {
  errMsg.value = '';
  try {
    await shell.execute(addon.exe, []);
  } catch (e) {
    errMsg.value = '启动失败：' + (e as Error).message;
  }
}

async function launchFixedAddon(exe: string) {
  errMsg.value = '';
  try {
    await shell.execute(exe, []);
  } catch (e) {
    errMsg.value = '启动失败：' + (e as Error).message;
  }
}

async function onCustomAddon(addon: SteamCustomAddon, enabled: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await steamCustomAddonSet(addon, enabled);
    customAddons.value = customAddons.value.map((a) => a.id === addon.id ? { ...a, enabled } : a);
  } catch (e) {
    errMsg.value = '切换联动项失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function removeCustomAddon(addon: SteamCustomAddon) {
  errMsg.value = '';
  busy.value = true;
  try {
    await steamCustomAddonRemove(addon);
    customAddons.value = customAddons.value.filter((a) => a.id !== addon.id);
  } catch (e) {
    errMsg.value = '删除联动项失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function closeSteam() {
  if (busy.value || steamChecking.value) return;
  busy.value = true;
  try {
    stopSteamPolling();
    await steamStop();
    // taskkill 返回只代表结束命令已发出，继续以真实 steam.exe 状态等待退出。
    pollSteamState(
      false,
      () => {
        busy.value = false;
        showNotice('Steam 关闭后仍检测到运行进程，请稍后确认。');
      },
      () => { busy.value = false; },
    );
  } catch (e) {
    showNotice('关闭 Steam 失败：' + (e as Error).message);
    busy.value = false;
  }
}

async function launchBigPicture() {
  if (busy.value || steamChecking.value) return;
  busy.value = true;
  try {
    if (!(await canStartSteam())) {
      busy.value = false;
      return;
    }
    await shell.open('steam://open/bigpicture');
    // URI 调用只负责发起请求，状态仍由真实 steam.exe 检测决定。
    pollSteamState(
      true,
      () => {
        busy.value = false;
        showNotice('Steam 启动后暂未检测到运行进程，请稍后确认。');
      },
      () => { busy.value = false; },
    );
  } catch (e) {
    showNotice('普通启动 Steam 大屏失败：' + (e as Error).message);
    busy.value = false;
  }
}

async function refreshCustomLibrarySummary() {
  customLibrarySummary.value = await readCustomSteamLibrarySummary();
}

async function openCustomLibrary() {
  if (customLibraryBusy.value) return;
  customLibraryBusy.value = true;
  errMsg.value = '';
  try {
    const result = await launchCustomSteamLibrary();
    if (!result.ok) throw new Error(result.reason || '自定义游戏库启动失败');
    customLibraryRunning.value = true;
  } catch (error) {
    showNotice(error instanceof Error ? error.message : String(error));
  } finally {
    customLibraryBusy.value = false;
  }
}

function onCustomLibraryClosed() {
  customLibraryRunning.value = false;
  customLibraryBusy.value = false;
  void refreshCustomLibrarySummary();
}

function onCustomLibraryConflict(event: Event) {
  const detail = (event as CustomEvent<{ inputOwner?: string }>).detail;
  customLibraryRunning.value = false;
  customLibraryBusy.value = false;
  showNotice(`自定义游戏库未接管输入（${detail?.inputOwner || 'unknown'}），已阻止主程序重复响应。`);
}

function launchLinkedFromPopup() {
  closeSteamLaunchPopup(false);
  void launch();
}

function launchNormalFromPopup() {
  closeSteamLaunchPopup(false);
  void launchBigPicture();
}

function closeSteamFromPopup() {
  closeSteamLaunchPopup(false);
  void closeSteam();
}

// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => {
    if (steamActive) void refresh();
  });
}

onMounted(() => {
  steamActive = true;
  window.addEventListener('ipc:gamepad-back', onSteamLaunchPopupBack);
  window.addEventListener('customSteamLibrary:closed', onCustomLibraryClosed);
  window.addEventListener('customSteamLibrary:conflict', onCustomLibraryConflict);
  stopUiVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) {
      closeSteamLaunchPopup(false);
      stopSteamPolling();
    }
  });
  void refresh();
  void refreshCustomLibrarySummary();
});
onActivated(() => {
  steamActive = true;
  window.addEventListener('ipc:gamepad-back', onSteamLaunchPopupBack);
  if (!stopUiVisibility) {
    stopUiVisibility = onUiVisibilityChange(({ visible }) => {
      if (!visible) {
        closeSteamLaunchPopup(false);
        stopSteamPolling();
      }
    });
  }
  void refresh();
  void refreshCustomLibrarySummary();
});
onDeactivated(() => {
  steamActive = false;
  closeSteamLaunchPopup(false);
  window.removeEventListener('ipc:gamepad-back', onSteamLaunchPopupBack);
  stopSteamPolling();
});
onBeforeUnmount(() => {
  closeSteamLaunchPopup(false);
  window.removeEventListener('ipc:gamepad-back', onSteamLaunchPopupBack);
  window.removeEventListener('customSteamLibrary:closed', onCustomLibraryClosed);
  window.removeEventListener('customSteamLibrary:conflict', onCustomLibraryConflict);
  stopSteamPolling();
  steamActive = false;
  stopSteamPolling();
  if (noticeTimer) clearTimeout(noticeTimer);
  stopUiVisibility?.();
  stopUiVisibility = null;
});
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="steam" /> Steam 大屏</h3>
      <div class="states-row">
        <button
          ref="steamStateButtonEl"
          type="button"
          class="state-card steam-state-card"
          :class="{ clickable: !busy && !steamChecking }"
          :disabled="busy || steamChecking"
          @click="openSteamLaunchPopup"
        >
          <span class="dot" :class="{ on: running }"></span>
          <span class="sc-body">
            <span class="sc-title">Steam</span>
            <span class="sc-text">{{ running ? (steamChecking ? '正在检测退出…' : '运行中') : '未启动' }}</span>
          </span>
        </button>
      </div>
      <Transition name="steam-launch-pop">
        <div
          v-if="steamLaunchPopupOpen"
          ref="steamLaunchPopupPanelEl"
          class="steam-launch-popup"
          role="dialog"
          aria-modal="true"
          aria-label="选择 Steam 大屏启动方式"
          data-gp-modal
          @keydown.esc.prevent="handleSteamLaunchPopupEsc"
        >
          <div class="steam-launch-popup-title"><InlineIcon name="steam" /> Steam {{ running ? '运行中' : '未启动' }}</div>
          <div class="steam-launch-popup-actions">
            <button type="button" :disabled="running || busy || steamChecking" @click="launchLinkedFromPopup"><InlineIcon name="play" /> 联动启动大屏</button>
            <button type="button" :disabled="running || busy || steamChecking" @click="launchNormalFromPopup"><InlineIcon name="fullscreen" /> 普通大屏</button>
            <button type="button" class="close" :disabled="!running || busy || steamChecking" @click="closeSteamFromPopup"><InlineIcon name="close" /> 关闭Steam大屏</button>
            <button ref="steamLaunchPopupCancelEl" type="button" class="cancel" @click="cancelSteamLaunchPopup">按<strong>B</strong>取消</button>
          </div>
        </div>
      </Transition>
    </section>

    <section class="card">
      <button
        type="button"
        class="card-title addon-card-toggle"
        :aria-expanded="addonsExpanded"
        aria-controls="steam-addon-list"
        @click="addonsExpanded = !addonsExpanded"
      >
        <span><InlineIcon name="link" /> 联动启动项</span>
        <span class="addon-card-chevron" aria-hidden="true">{{ addonsExpanded ? '▴' : '▾' }}</span>
      </button>
      <div v-if="addonsExpanded" id="steam-addon-list" class="addon-list">
        <div v-for="l in STEAM_ADDONS" :key="l.key" class="addon-row">
          <button class="addon-launch-btn" :disabled="busy" title="启动程序" @click="launchFixedAddon(l.exe)">
            <InlineIcon name="play" /> 启动
          </button>
          <Toggle
            v-model="states[l.key]"
            :label="l.name"
            color="accent"
            :disabled="busy"
            @update:model-value="(v: boolean) => onAddon(l.key, v)"
          />
        </div>
        <div class="custom-addon-divider">自选联动启动项</div>
        <div v-if="customAddons.length === 0" class="empty-addon">暂无自选程序</div>
        <div v-for="addon in customAddons" :key="addon.id" class="custom-addon-row">
          <button class="addon-launch-btn" :disabled="busy" title="启动程序" @click="launchCustomAddon(addon)">
            <InlineIcon name="play" /> 启动
          </button>
          <Toggle
            :model-value="addon.enabled"
            :label="addon.name"
            color="accent"
            :disabled="busy"
            @update:model-value="(v: boolean) => onCustomAddon(addon, v)"
          />
          <button
            class="custom-addon-delete"
            :disabled="busy"
            title="删除联动启动项"
            @click="removeCustomAddon(addon)"
          >
            <InlineIcon name="trash" />
          </button>
        </div>
        <button class="add-addon-btn" :disabled="busy" @click="addCustomAddon">
          <span class="add-addon-plus">+</span> 添加程序
        </button>
      </div>
    </section>

    <section class="card custom-library-entry-card" aria-label="自定义游戏库">
      <h2>Steam自定义游戏库</h2>
      <p class="custom-library-subtitle">扫描非Steam游戏加入Steam大屏</p>
      <div v-if="customLibrarySummary" class="custom-library-summary" aria-label="游戏库分类统计">
        <div><strong>{{ customLibrarySummary.waiting }}</strong><span>等待加入</span></div>
        <div><strong>{{ customLibrarySummary.joined }}</strong><span>已加入</span></div>
        <div><strong>{{ customLibrarySummary.needs }}</strong><span>需处理</span></div>
        <div><strong>{{ customLibrarySummary.excluded }}</strong><span>不加入</span></div>
      </div>
      <div v-else class="custom-library-summary custom-library-summary-empty" aria-label="游戏库分类统计读取中">
        <div><strong>—</strong><span>等待加入</span></div>
        <div><strong>—</strong><span>已加入</span></div>
        <div><strong>—</strong><span>需处理</span></div>
        <div><strong>—</strong><span>不加入</span></div>
      </div>
      <button class="custom-library-open-button" type="button" :disabled="customLibraryBusy" @click="openCustomLibrary">
        <InlineIcon name="play" />
        {{ customLibraryRunning ? '重新打开自定义游戏库' : '打开自定义游戏库' }}
      </button>
    </section>

  </div>
</template>

<style scoped>
.page {
  padding-bottom: 20px;
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
.states-row {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
}
.states-row > * {
  flex: 1 1 0;
  min-width: 0;
}
.steam-state-card {
  width: 100%;
  min-height: 54px;
  display: flex;
  align-items: center;
  gap: 10px;
  border: 0;
  background: var(--bg-input);
  border-radius: var(--radius-ctrl);
  padding: 10px 12px;
  color: var(--text);
  text-align: left;
}
.steam-state-card.clickable {
  cursor: pointer;
}
.steam-state-card.clickable:hover {
  background: color-mix(in srgb, var(--bg-input) 86%, var(--accent));
}
.steam-state-card:disabled {
  cursor: default;
}
.dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  flex: 0 0 auto;
  background: #46506280;
  box-shadow: 0 0 6px currentColor;
}
.dot.on {
  background: var(--ok);
  color: var(--ok);
}
.sc-body {
  flex: 1 1 auto;
  min-width: 0;
}
.sc-title,
.sc-text {
  display: block;
}
.sc-title {
  font-size: 12px;
  font-weight: 600;
}
.sc-text {
  font-size: 13px;
  font-weight: 700;
  margin-top: 2px;
}
.steam-state-card:disabled,
.addon-launch-btn:disabled {
  opacity: 0.58;
}
.addon-card-toggle {
  width: 100%;
  justify-content: space-between;
  border: 0;
  padding: 0;
  background: transparent;
  color: var(--text);
  text-align: left;
  cursor: pointer;
}
.addon-card-toggle > span:first-child {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.addon-card-chevron {
  color: var(--text-dim);
  font-size: 14px;
  line-height: 1;
}
.addon-list {
  display: grid;
  gap: 4px;
}
.custom-addon-divider {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid rgba(255, 255, 255, 0.08);
  color: var(--text-dim);
  font-size: 11px;
  font-weight: 700;
}
.addon-row {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  align-items: center;
  gap: 8px;
}
.addon-row :deep(.toggle-row) {
  min-width: 0;
}
.addon-launch-btn {
  flex: 0 0 auto;
  border: 1px solid rgba(46, 166, 255, 0.45);
  border-radius: var(--radius-ctrl);
  background: rgba(46, 166, 255, 0.1);
  color: var(--accent);
  padding: 6px 8px;
  font-size: 10px;
  font-weight: 700;
  cursor: pointer;
  white-space: nowrap;
}
.addon-launch-btn:hover {
  background: rgba(46, 166, 255, 0.18);
}
.custom-addon-row {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr) 30px;
  align-items: center;
  gap: 8px;
  min-height: 36px;
  padding: 5px 0;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
.custom-addon-row :deep(.toggle-row) {
  min-width: 0;
}
.custom-addon-row :deep(.toggle-label) {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.custom-addon-delete,
.add-addon-btn {
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text-dim);
  cursor: pointer;
}
.empty-addon {
  padding: 10px 2px 8px;
  color: var(--text-dim);
  font-size: 11px;
  text-align: center;
}
.custom-addon-delete {
  width: 30px;
  height: 28px;
  padding: 0;
  color: #ff9ea1;
}
.custom-addon-delete:hover {
  background: rgba(229, 72, 77, 0.16);
}
.add-addon-btn {
  width: 100%;
  margin-top: 8px;
  padding: 8px;
  color: var(--accent);
  border-color: rgba(46, 166, 255, 0.45);
  font-size: 11px;
  font-weight: 700;
}
.add-addon-btn:hover {
  background: rgba(46, 166, 255, 0.12);
}
.add-addon-plus {
  font-size: 16px;
  line-height: 0;
  vertical-align: -1px;
}
.custom-addon-delete:disabled,
.add-addon-btn:disabled {
  opacity: 0.5;
  cursor: default;
}
.steam-launch-popup {
  width: 100%;
  margin-top: 8px;
  padding: 14px;
  border: 1px solid #2a3342;
  border-radius: 12px;
  background: #161d29;
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.55);
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.steam-launch-popup-title {
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 16px;
  font-weight: 700;
  color: var(--text);
}
.steam-launch-popup-title :deep(svg) {
  width: 20px;
  height: 20px;
  color: var(--accent);
}
.steam-launch-popup-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin-top: 3px;
}
.steam-launch-popup-actions button {
  min-height: 44px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 9px;
  background: var(--bg-input);
  color: var(--text);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
}
.steam-launch-popup-actions button:first-child {
  background: var(--accent);
  color: #07131d;
  font-weight: 700;
}
.steam-launch-popup-actions button.close {
  color: var(--danger);
  border-color: color-mix(in srgb, var(--danger) 46%, transparent);
  background: color-mix(in srgb, var(--danger) 8%, var(--bg-input));
}
.steam-launch-popup-actions button.close:hover:not(:disabled) {
  background: color-mix(in srgb, var(--danger) 16%, var(--bg-input));
  border-color: var(--danger);
}
.steam-launch-popup-actions button.cancel {
  color: var(--text);
}
.steam-launch-popup-actions button.cancel strong {
  color: var(--danger);
  font-weight: 800;
}
.steam-launch-popup-actions button:disabled {
  opacity: 0.42;
  cursor: default;
}
.steam-launch-pop-enter-active,
.steam-launch-pop-leave-active {
  transition: opacity 0.12s ease, transform 0.12s ease;
}
.steam-launch-pop-enter-from,
.steam-launch-pop-leave-to {
  opacity: 0;
  transform: translateY(-4px);
}
.custom-library-entry-card {
  padding: 12px 14px;
}
.custom-library-entry-card h2 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  line-height: 1.25;
}
.custom-library-subtitle {
  margin: 4px 0 10px;
  color: var(--text-dim);
  font-size: 11px;
}
.custom-library-summary {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 6px;
  margin-bottom: 10px;
}
.custom-library-summary > div {
  min-width: 0;
  min-height: 46px;
  padding: 8px 9px;
  border: 1px solid var(--border);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
}
.custom-library-summary strong {
  display: block;
  color: var(--accent);
  font-size: 18px;
  line-height: 1;
  font-variant-numeric: tabular-nums;
}
.custom-library-summary span {
  display: block;
  margin-top: 4px;
  color: var(--text-dim);
  font-size: 10px;
}
.custom-library-summary-empty {
  opacity: 0.72;
}
.custom-library-open-button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  width: 100%;
  min-height: 40px;
  border: 1px solid rgba(46, 166, 255, 0.65);
  border-radius: var(--radius-ctrl);
  background: linear-gradient(180deg, #2997cd, #187ca9);
  color: #fff;
  font-size: 12px;
  font-weight: 800;
  cursor: pointer;
}
.custom-library-open-button:hover:not(:disabled) {
  filter: brightness(1.08);
}
.custom-library-open-button:focus-visible {
  box-shadow: var(--focus-ring);
}
.custom-library-open-button:disabled {
  opacity: 0.55;
  cursor: default;
}
.custom-library-path {
  margin: 9px 0 0;
  overflow: hidden;
  color: var(--text-dim);
  font-family: Consolas, monospace;
  font-size: 10px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
@media (max-width: 560px) {
  .custom-library-summary {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
