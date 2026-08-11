<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, onActivated, onDeactivated, nextTick, inject, watch, type Ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import StateCard from '@/components/StateCard.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { dialog, shell } from '@/bridge/api';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
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
  steamMasterOn,
  steamMasterSet,
  steamRunning,
  steamStop,
  launchSteam,
} from '@/bridge/yeman';

const running = ref(false);
const master = ref(false);
const addonStates: Record<string, boolean> = {};
const states = ref({ ...addonStates });

const busy = ref(false);
const errMsg = ref('');
const customAddons = ref<SteamCustomAddon[]>([]);
const steamRunKnown = ref(false);
const steamChecking = ref(false);
let noticeTimer: ReturnType<typeof setTimeout> | null = null;
let steamPollTimer: ReturnType<typeof setInterval> | null = null;
let steamPollBusy = false;
let steamPollGeneration = 0;
let steamActive = false;
let stopUiVisibility: (() => void) | null = null;

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
  const [runRes, masterRes, customRes, ...addonRes] = await Promise.allSettled([
    detectSteamRunning(),
    steamMasterOn(),
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
  if (masterRes.status === 'fulfilled') master.value = masterRes.value;
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

async function onMaster(v: boolean) {
  errMsg.value = '';
  busy.value = true;
  try {
    await steamMasterSet(v);
    master.value = v;
  } catch (e) {
    master.value = !v;
    errMsg.value = '写入 .earlystart 失败：' + (e as Error).message;
  } finally {
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
  stopUiVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) stopSteamPolling();
  });
  void nextTick(refresh);
});
onActivated(() => {
  steamActive = true;
  if (!stopUiVisibility) {
    stopUiVisibility = onUiVisibilityChange(({ visible }) => {
      if (!visible) stopSteamPolling();
    });
  }
  void refresh();
});
onDeactivated(() => {
  steamActive = false;
  stopSteamPolling();
});
onBeforeUnmount(() => {
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
        <StateCard title="Steam" :state="running ? 'on' : 'off'" :text="running ? (steamChecking ? '正在检测退出…' : '运行中') : '未运行'" />
        <button v-if="running" class="steam-close-btn" :class="{ 'is-busy': busy || steamChecking }" :disabled="busy || steamChecking" @click="closeSteam"><InlineIcon name="close" />{{ busy || steamChecking ? ' 正在关闭…' : ' 关闭' }}</button>
      </div>
      <div class="btn-row">
        <button data-gp-group="steam-big-picture" class="action-btn" :class="{ 'is-busy': busy || steamChecking }" :disabled="busy || steamChecking" @click="launch"><InlineIcon name="play" />{{ busy || steamChecking ? ' 正在验证/启动…' : ' 联动启动Steam大屏' }}</button>
        <button data-gp-group="steam-big-picture" class="action-btn ghost" :class="{ 'is-busy': busy || steamChecking }" :disabled="busy || steamChecking" @click="launchBigPicture"><InlineIcon name="fullscreen" />{{ busy || steamChecking ? ' 正在验证/启动…' : ' 普通启动Steam大屏模式' }}</button>
      </div>
      <Toggle v-model="master" label="Steam 高级开机启动 (.earlystart)" description="写入用户目录 .earlystart" color="accent" :disabled="busy" @update:model-value="onMaster" />
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="link" /> 固定联动启动项</h3>
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
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="link" /> 自选联动启动项</h3>
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
.btn-row {
  display: flex;
  gap: 8px;
  margin: 4px 0 6px;
}
.action-btn {
  flex: 1;
  width: 100%;
  background: var(--accent);
  color: #06121d;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: var(--btn-py) var(--btn-px);
  min-height: var(--btn-min-h);
  font-weight: 700;
  font-size: var(--btn-font-size);
  cursor: pointer;
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.action-btn.ghost {
  background: var(--bg-input);
  color: var(--text);
}
.action-btn.is-busy {
  cursor: wait;
  opacity: 0.78;
  animation: steam-button-pulse 1.2s ease-in-out infinite;
}
.action-btn:disabled,
.steam-close-btn:disabled,
.addon-launch-btn:disabled {
  cursor: wait;
  opacity: 0.58;
}
@keyframes steam-button-pulse {
  0%, 100% { filter: brightness(0.92); }
  50% { filter: brightness(1.14); }
}
.states-row {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
}
.states-row .state-card {
  flex: 1;
}
.steam-close-btn {
  flex: 0 0 auto;
  align-self: stretch;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 1px solid rgba(229, 72, 77, 0.55);
  border-radius: var(--radius-ctrl);
  background: rgba(229, 72, 77, 0.12);
  color: #ff9ea1;
  padding: 7px 10px;
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
}
.steam-close-btn:hover {
  background: rgba(229, 72, 77, 0.2);
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
</style>
