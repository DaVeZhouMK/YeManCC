<script setup lang="ts">
// [AI:USER:STEAM] 点击启动/切换→请求所选Steam界面；等待确认、运行中、模式未知与失败分别呈现，不把请求成功当模式已切换。
// 联动项按用户选择生效，切页不代表关闭Steam；后台下载/动态壁纸与Steam模式分别设置。
// 修改先查 STEAM-UPGRADE.md 的现行入口；用户动作不能推导为自动重启Steam/游戏，具体执行读现有桥接和原生实现。
import IntegratedPage from '@/components/IntegratedPage.vue';
import SettingsSections from '@/components/SettingsSections.vue';
const steamSections=[{id:'library',title:'大屏与游戏',description:'',icon:'gamepad'},{id:'wallpaper',title:'动态壁纸',description:'',icon:'layers'},{id:'integration',title:'联动与状态',description:'',icon:'tools'}];
import { computed, ref, onMounted, onBeforeUnmount, onActivated, onDeactivated, inject, watch, type Ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import SteamWallpaperPlaybackToggle from '@/components/SteamWallpaperPlaybackToggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import {getUiSetting} from '@/bridge/uiSettings';
import {sharedGameMedia,steamWallpaperEnabled,setSteamWallpaperEnabled} from '@/bridge/sharedGameMedia';
import {gameMediaDownloads,backgroundDownloadEnabled,setBackgroundDownloadEnabled,downloadDescription,setDownloadCpuThreshold} from '@/bridge/gameMediaDownloads';
const downloadCpuThreshold=ref(getUiSetting('steamWallpaperDownloadCpuThreshold'));
const downloadCpuOptions=[10,15,20,30,50].map(value=>({value,label:value+'%'}));
async function changeDownloadCpuThreshold(value:string|number){await setDownloadCpuThreshold(Number(value));downloadCpuThreshold.value=getUiSetting('steamWallpaperDownloadCpuThreshold');}
const wallpaperDownloadEnabled=ref(backgroundDownloadEnabled());
const wallpaperDownloads=ref(gameMediaDownloads.state);const wallpaperDownloadBusy=ref(false),wallpaperDownloadError=ref('');
async function requestWallpaperDownloads(){if(wallpaperDownloadBusy.value)return;wallpaperDownloadBusy.value=true;wallpaperDownloadError.value='';try{if(Number(wallpaperDownloads.value.manualUntil||0)>Date.now())await gameMediaDownloads.cancelManual();else await gameMediaDownloads.requestNow();}catch(error){wallpaperDownloadError.value=String(error instanceof Error?error.message:error);}finally{wallpaperDownloadBusy.value=false;}}
let stopWallpaperDownloads:(()=>void)|null=null;
async function changeWallpaperDownloads(enabled:boolean){await setBackgroundDownloadEnabled(enabled);wallpaperDownloadEnabled.value=enabled;}
const wallpaperEnabled=ref(steamWallpaperEnabled());
const wallpaperBusy=ref(false),wallpaperError=ref('');
const wallpaperPreparation=ref(sharedGameMedia.preparationDescription());
let stopWallpaperPreparation:(()=>void)|null=null;
async function changeWallpaper(enabled:boolean){if(wallpaperBusy.value)return;wallpaperBusy.value=true;wallpaperError.value='';try{await setSteamWallpaperEnabled(enabled);wallpaperEnabled.value=enabled;}catch(error){wallpaperError.value=String(error instanceof Error?error.message:error);}finally{wallpaperBusy.value=false;}}
const syncWallpaper=()=>{wallpaperEnabled.value=steamWallpaperEnabled();wallpaperDownloadEnabled.value=backgroundDownloadEnabled();downloadCpuThreshold.value=getUiSetting('steamWallpaperDownloadCpuThreshold');};
onMounted(()=>{stopWallpaperDownloads=gameMediaDownloads.subscribe(()=>{wallpaperDownloads.value=gameMediaDownloads.state;});window.addEventListener('ui-settings:changed',syncWallpaper);window.addEventListener('ui-settings:loaded',syncWallpaper);syncWallpaper();stopWallpaperPreparation=sharedGameMedia.subscribe(reason=>{if(reason==='policy')wallpaperPreparation.value=sharedGameMedia.preparationDescription();});});
onBeforeUnmount(()=>{stopWallpaperDownloads?.();stopWallpaperPreparation?.();window.removeEventListener('ui-settings:changed',syncWallpaper);window.removeEventListener('ui-settings:loaded',syncWallpaper);});
import SteamAppearanceCard from '@/components/SteamAppearanceCard.vue';
import SteamQuickAccessMapping from '@/components/SteamQuickAccessMapping.vue';
import { deckySidebar, deckySidebarCanRepair, deckyConnectionSummary, deckySidebarDescription, deckySidebarSetupFailure, type DeckySidebarState } from '@/bridge/deckySidebar';
import InlineIcon from '@/components/InlineIcon.vue';
import { dialog, shell } from '@/bridge/api';
import { isUiVisible, onUiVisibilityChange, getUiVisibilityState } from '@/bridge/uiLifecycle';
import { steamUi, type SteamUiState } from '@/bridge/steamUi';
import { createSteamReadBudget } from '@/bridge/steamReadBudget';
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
  launchSteam,
} from '@/bridge/yeman';
import {
  type CustomSteamLibrarySummary,
  launchCustomSteamLibrary,
  readCustomSteamLibrarySummary,
} from '@/bridge/customSteamLibrary';

const deckyState = ref<DeckySidebarState | null>(null);
const deckyBusy = ref(false);
let stopDeckyState: (() => void) | null = null;
let deckyViewEpoch = 0;
function acceptDeckyState(state: DeckySidebarState) {
  if (deckyState.value && state.revision < deckyState.value.revision) return;
  if(deckyState.value&&state.revision===deckyState.value.revision&&
    (state.connectionRevision??0)<(deckyState.value.connectionRevision??0))return;
  deckyState.value = state;
  if(!deckySidebarCanRepair(state)||state.mirrorConnected||state.powerReady===false)deckyRepairMessage.value='';
  if (state.enabled && (state.phase === 'failed' || state.phase === 'unavailable')) showNotice(deckySidebarDescription(state));
}
const deckyConnection = computed(()=>deckyConnectionSummary(deckyState.value));
const deckyRepairMessage = ref('');
async function repairDeckyConnection() {
  const state=deckyState.value,steam=steamUiState.value;
  if(deckyBusy.value||!state||!deckySidebarCanRepair(state)||!steam?.known||!steam.running||steam.mode!=='bigpicture')return;
  deckyBusy.value=true;deckyRepairMessage.value='正在修复连接…';const epoch=deckyViewEpoch;
  try {
    const result=await deckySidebar.repairConnection(steam.pid,steam.birth,state.revision);
    if(epoch!==deckyViewEpoch)return;
    deckyRepairMessage.value=result.ok?'已请求恢复，等待连接':result.mutated?'正在确认恢复结果，请稍候':result.reason==='decky-repair-wait'||result.reason==='decky-repair-already-requested'?'已有恢复请求，请稍候':'暂时无法修复，请检查连接状态';
  } catch { if(epoch===deckyViewEpoch)deckyRepairMessage.value='正在确认恢复结果，请稍候'; }
  finally {if(epoch===deckyViewEpoch)deckyBusy.value=false;}
}
async function setDeckyEnabled(enabled: boolean) {
  if (deckyBusy.value) return;
  deckyBusy.value = true;
  const epoch = deckyViewEpoch;
  try { const state = await deckySidebar.setEnabled(enabled); if (epoch === deckyViewEpoch) acceptDeckyState(state); }
  catch (error) {
    const environment=await deckySidebar.environment().catch(()=>null);
    if(epoch===deckyViewEpoch)showNotice(deckySidebarSetupFailure(error,environment));
  }
  finally { if (epoch === deckyViewEpoch) deckyBusy.value = false; }
}
onMounted(() => {
  const epoch = ++deckyViewEpoch;
  stopDeckyState = deckySidebar.subscribe(acceptDeckyState);
  void deckySidebar.get().then(state => { if (epoch === deckyViewEpoch) acceptDeckyState(state); }).catch(() => {});
});
onBeforeUnmount(() => { ++deckyViewEpoch; stopDeckyState?.(); stopDeckyState = null; });

const running = ref(false);
const steamUiState = ref<SteamUiState | null>(null);
const steamPendingTarget = ref<'running' | 'desktop' | 'bigpicture' | null>(null);
let steamReadEpoch = 0, steamObservationEpoch = 0, steamStateReadBusy = false, steamStateRefreshQueued = false;
let stopSteamUiState: (() => void) | null = null;
let steamEventTimer: ReturnType<typeof setTimeout> | null = null;
const steamReadBudget = createSteamReadBudget();
let steamForceObservation = false;
watch(running, () => window.dispatchEvent(new Event('steam-appearance:environment')));
const addonStates: Record<string, boolean> = {};
const states = ref({ ...addonStates });

const busy = ref(false);
const errMsg = ref('');
const customAddons = ref<SteamCustomAddon[]>([]);
const steamRunKnown = ref(false);
const steamStateStale = ref(false);
const steamChecking = ref(false);
let noticeTimer: ReturnType<typeof setTimeout> | null = null;
let steamPollTimer: ReturnType<typeof setInterval> | null = null;
let steamPollDeadlineTimer: ReturnType<typeof setTimeout> | null = null;
let steamFirstPollTimer: ReturnType<typeof setTimeout> | null = null;
let steamPollBusy = false;
let steamPollGeneration = 0;
let steamActive = false;
let steamPowerIntent = 0, steamPowerPending = false;
let stopUiVisibility: (() => void) | null = null;
const customLibrarySummary = ref<CustomSteamLibrarySummary | null>(null);
const customLibraryBusy = ref(false);
const customLibraryRunning = ref(false);

const STEAM_FIRST_LAUNCH_CHECK_MS = 5000;
const STEAM_POLL_INTERVAL_MS = 4000;
const STEAM_POLL_TIMEOUT_MS = 40000;
function showNotice(message: string) {
  if (noticeTimer) clearTimeout(noticeTimer);
  errMsg.value = message;
  noticeTimer = setTimeout(() => {
    errMsg.value = '';
    noticeTimer = null;
  }, 5000);
}

function steamReadVisible() {
  // Process/status reads are independent of fan/input recovery readiness.
  if (typeof getUiVisibilityState !== 'function') return isUiVisible();
  const state = getUiVisibilityState();
  return state.documentVisible && state.nativeWindowVisible;
}

const steamReadonlyEvents = ['ipc:power.resume-ready', 'ipc:power.resumed', 'ipc:window.shown', 'ipc:window.restored', 'ipc:window.maximized', 'ipc:window.summoned'];
async function detectSteamState(automatic = false): Promise<SteamUiState | null> {
  try { return await steamUi.read(automatic ? { automatic: true } : undefined); } catch { return null; }
}
function acceptSteamState(state: SteamUiState | null) {
  if (!state?.known) {
    // Wake/CEF errors cannot turn a last-confirmed running process into absence.
    steamStateStale.value = true;
    if (!steamRunKnown.value) steamUiState.value = state;
    return;
  }
  steamStateStale.value = false; steamRunKnown.value = true;
  steamUiState.value = state; running.value = state.running;
}
async function refreshSteamState(options?: { automatic: true }): Promise<SteamUiState | null> {
  const automatic = options?.automatic === true;
  if (!automatic) {
    steamReadBudget.renew();

  } else {
    if (!steamActive || !steamReadVisible()) return null;
    // Global refreshes also respect the launch/mode confirmation owner.
    if (steamPowerPending || steamChecking.value) { steamStateRefreshQueued = true; return null; }
    const delay = steamReadBudget.delay(performance.now());
    if (delay === null) return null;
    if (delay > 0) { queueSteamStateRefresh(); return null; }
  }
  if (steamStateReadBusy) { steamStateRefreshQueued = true; return null; }
  steamStateReadBusy = true;
  const epoch = steamObservationEpoch;
  steamReadBudget.started(performance.now());
  try {
    const state = await detectSteamState(automatic);
    if (epoch === steamObservationEpoch) steamReadBudget.finished(!!state?.known && (!state.running || state.mode !== 'unknown'));
    if (epoch === steamObservationEpoch && steamActive && steamReadVisible()) acceptSteamState(state);
    return epoch === steamObservationEpoch ? state : null;
  } finally {
    steamStateReadBusy = false;
    if (steamStateRefreshQueued) { steamStateRefreshQueued = false; queueSteamStateRefresh(); }
  }
}
function queueSteamStateRefresh() {
  if (!steamActive || !steamReadVisible()) return;
  if (steamStateReadBusy || steamChecking.value || steamPowerPending) { steamStateRefreshQueued = true; return; }
  if (steamEventTimer) return;
  const delay = steamReadBudget.delay(performance.now());
  if (delay === null && !steamForceObservation) return; // Failed searches stop, not an idle retry loop.
  // Actual events create at most one trailing read; event storms cannot exceed
  // one automatic query per five seconds. An idle page has no timer at all.
  steamEventTimer = setTimeout(function runSteamObservation() {
    steamEventTimer = null;
    if (!steamActive || !steamReadVisible()) return;
    // A manual read may have happened while the trailing event timer waited.
    const wait = steamReadBudget.delay(performance.now());
    if (wait !== null && wait > 0) { queueSteamStateRefresh(); return; }
    const force = steamForceObservation; steamForceObservation = false;
    void refreshSteamState(force ? undefined : { automatic: true });
  }, Math.max(350, delay ?? 0));
}

function queueSteamResumeRefresh() {
  steamReadBudget.renew(); steamForceObservation = true;
  queueSteamStateRefresh(); // One renewed observation only; never a launch/mode action.
}
function stopSteamPolling() {
  steamPollGeneration += 1;
  if (steamFirstPollTimer) { clearTimeout(steamFirstPollTimer); steamFirstPollTimer = null; }
  if (steamPollDeadlineTimer) { clearTimeout(steamPollDeadlineTimer); steamPollDeadlineTimer = null; }
  if (steamPollTimer) {
    clearInterval(steamPollTimer);
    steamPollTimer = null;
  }
  steamPollBusy = false;
  steamChecking.value = false;
}

function finishSteamPower(intent: number) {
  if (intent !== steamPowerIntent) return;
  steamPowerPending = false; busy.value = false; steamPendingTarget.value = null;
  if (steamStateRefreshQueued) { steamStateRefreshQueued = false; queueSteamStateRefresh(); }
}
function cancelSteamPowerWait() {
  ++steamPowerIntent; ++steamReadEpoch;
  const readsVisible = steamActive && steamReadVisible();
  // A late global power gate cancels actions, not an already-queued/read-only
  // wake observation. Truly hiding/leaving the view invalidates both.
  if (!readsVisible) {
    ++steamObservationEpoch;
    if (steamEventTimer) { clearTimeout(steamEventTimer); steamEventTimer = null; }
    steamStateRefreshQueued = false; steamForceObservation = false;
  }
  steamPendingTarget.value = null;
  stopSteamPolling();
  if (steamPowerPending) { steamPowerPending = false; busy.value = false; }
  if (readsVisible && steamStateRefreshQueued) { steamStateRefreshQueued = false; queueSteamStateRefresh(); }
}
function pollSteamState(
  target: 'running' | 'desktop' | 'bigpicture',
  onTimeout: () => void,
  onMatched: (state: SteamUiState) => void = () => {},
  expectedSession?: SteamUiState,
) {
  stopSteamPolling();
  const generation = steamPollGeneration;
  const startedAt = performance.now();
  steamChecking.value = true;

  const check = async () => {
    if (!steamActive || !isUiVisible() || generation !== steamPollGeneration || steamPollBusy) return;
    if (performance.now() - startedAt >= STEAM_POLL_TIMEOUT_MS) { stopSteamPolling(); onTimeout(); return; }
    steamPollBusy = true;
    try {
      const live = await refreshSteamState();
      if (generation !== steamPollGeneration) return;
      if (live?.known && live.running && expectedSession &&
          (live.pid !== expectedSession.pid || live.birth !== expectedSession.birth)) {
        stopSteamPolling(); onTimeout();
        showNotice('Steam 会话已改变，已停止本次模式确认；请按当前状态重试。');
        return;
      }
      if (live?.known && live.running && (target === 'running' || live.mode === target)) {
        stopSteamPolling();
        onMatched(live);
        return;
      }
      if ((live?.known && !live.running && target !== 'running') || performance.now() - startedAt >= STEAM_POLL_TIMEOUT_MS) {
        stopSteamPolling();
        onTimeout();
      }
    } finally {
      if (generation === steamPollGeneration) steamPollBusy = false;
    }
  };

  // A launch receipt is only submission. Let the client initialize before the
  // first read; later checks stay bounded and never launch/switch again.
  steamPollDeadlineTimer = setTimeout(function expireSteamConfirmation() {
    if (generation !== steamPollGeneration) return;
    stopSteamPolling(); onTimeout();
  }, STEAM_POLL_TIMEOUT_MS);
  if (target === 'running') {
    steamFirstPollTimer = setTimeout(() => {
      steamFirstPollTimer = null;
      if (generation !== steamPollGeneration || !steamActive || !isUiVisible()) return;
      steamPollTimer = setInterval(check, STEAM_POLL_INTERVAL_MS);
      void check();
    }, STEAM_FIRST_LAUNCH_CHECK_MS);
  } else {
    steamPollTimer = setInterval(check, STEAM_POLL_INTERVAL_MS);
    void check();
  }
}

// 并行异步加载；状态检测失败时保持未确认，不把 Steam 错误显示为运行中。
async function refresh() {
  const [, customRes, ...addonRes] = await Promise.allSettled([
    refreshSteamState({ automatic: true }),
    steamCustomAddons(),
    ...STEAM_ADDONS.map((a) => steamAddonExists(a.key).catch(() => false)),
  ]);
  if (customRes.status === 'fulfilled') customAddons.value = customRes.value;
  const next: Record<string, boolean> = {};
  STEAM_ADDONS.forEach((a, i) => {
    next[a.key] = addonRes[i]?.status === 'fulfilled' ? addonRes[i].value : false;
  });
  states.value = next;
}

async function launch() {
  if (busy.value) return;
  stopSteamPolling();
  const intent = ++steamPowerIntent;
  ++steamReadEpoch; ++steamObservationEpoch;
  if (steamEventTimer) { clearTimeout(steamEventTimer); steamEventTimer = null; }
  steamPowerPending = true; busy.value = true; steamPendingTarget.value = 'running';
  try {
    if (intent !== steamPowerIntent || !steamActive || !isUiVisible()) return;
    await launchSteam({ userInitiated: true,
      isCurrent: () => intent === steamPowerIntent && steamActive && isUiVisible() });
    if (intent !== steamPowerIntent || !steamActive || !isUiVisible()) return;
    // Command return is not proof Steam is running; this is a finite confirmation window.
    // Confirmation only reports the actual mode. Never send another mode command
    // later: the user may already have chosen desktop outside YMCC.
    steamPendingTarget.value = 'running';
    busy.value = false; // Observation does not impose a 40-second button lock.
    pollSteamState('running',
      () => { finishSteamPower(intent); showNotice('Steam 启动后暂未检测到运行进程，请稍后确认。'); },
      () => finishSteamPower(intent));
  } catch (error) {
    if (intent === steamPowerIntent) showNotice('Steam 启动失败：' + (error as Error).message);
  } finally {
    if (intent === steamPowerIntent && !steamChecking.value) finishSteamPower(intent);
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

async function switchSteamMode() {
  if (busy.value) return;
  stopSteamPolling();
  const original = steamUiState.value;
  const target = original?.mode === 'desktop' ? 'bigpicture' : original?.mode === 'bigpicture' ? 'desktop' : null;
  const intent = ++steamPowerIntent;
  ++steamReadEpoch; ++steamObservationEpoch;
  if (steamEventTimer) { clearTimeout(steamEventTimer); steamEventTimer = null; }
  steamPowerPending = true; busy.value = true; steamPendingTarget.value = target;
  try {
    const state = await detectSteamState();
    if (intent !== steamPowerIntent || !steamActive || !isUiVisible()) return;
    acceptSteamState(state);
    if (!state?.known || !state.running) { showNotice('Steam 运行状态已变化，请按当前状态重试。'); return; }
    if (!target || state.mode === 'unknown') { showNotice(state.reason === 'native-ui-mode-unavailable'
      ? '当前原生程序不支持模式识别，请完整更新 YMCC；Steam 未被关闭。'
      : 'Steam 界面模式暂未确认，请稍后重新检测；Steam 未被关闭。'); return; }
    if (original && (state.pid !== original.pid || state.birth !== original.birth)) {
      showNotice('Steam 会话已改变，请按当前状态重新点击切换。'); return;
    }
    // Preserve the clicked direction even if Steam changed externally while
    // reading. An already-completed target is a no-op, never an inverse toggle.
    if (state.mode === target) return;
    await steamUi.switchMode(state, target, { userInitiated: true });
    if (intent !== steamPowerIntent || !steamActive || !isUiVisible()) return;
    steamPendingTarget.value = target;
    busy.value = false;
    pollSteamState(target,
      () => { finishSteamPower(intent); showNotice('已提交模式切换，但尚未确认目标界面，请稍后重新检测。'); },
      () => finishSteamPower(intent), state);
  } catch (error) {
    if (intent === steamPowerIntent) showNotice('Steam 模式切换失败：' + (error as Error).message);
  } finally {
    if (intent === steamPowerIntent && !steamChecking.value) finishSteamPower(intent);
  }
}
async function onSteamStateClick() {
  if (busy.value || steamChecking.value) return;
  const original = steamUiState.value;
  // Desktop/stopped/unknown keep their existing read-only status action.
  if (!steamRunKnown.value || !running.value || original?.mode !== 'bigpicture') {
    await refreshSteamState();
    return;
  }
  const intent = ++steamPowerIntent;
  ++steamReadEpoch; ++steamObservationEpoch;
  if (steamEventTimer) { clearTimeout(steamEventTimer); steamEventTimer = null; }
  steamPowerPending = true; busy.value = true;
  try {
    const state = await refreshSteamState();
    if (intent !== steamPowerIntent || !steamActive || !isUiVisible()) return;
    if (!state?.known || !state.running || state.pid !== original.pid || state.birth !== original.birth) {
      showNotice('Steam 运行状态或会话已改变，请按当前状态重试。'); return;
    }
    if (state.mode !== 'bigpicture') { showNotice('Steam 已不在大屏模式，已更新当前状态。'); return; }
    await steamUi.showBigPicture(state, { userInitiated: true });
  } catch (error) {
    if (intent === steamPowerIntent) showNotice('显示 Steam 大屏失败：' + (error as Error).message);
  } finally {
    finishSteamPower(intent);
  }
}
function steamStateText() {
  if (!steamRunKnown.value) return '状态未确认';
  if (!running.value) return steamStateStale.value ? '未启动（状态待复核）' : '未启动';
  if (steamUiState.value?.mode === 'desktop') return steamStateStale.value ? '桌面Steam运行中（状态待复核）' : '桌面Steam运行中';
  if (steamUiState.value?.mode === 'bigpicture') return steamStateStale.value ? '大屏Steam运行中（状态待复核）' : '大屏Steam运行中';
  return steamStateStale.value ? 'Steam运行中（状态待复核）' : 'Steam运行中（模式未确认）';
}
function steamActionText() {
  if (steamChecking.value || busy.value && steamPowerPending) {
    if (steamPendingTarget.value === 'running') return busy.value ? '正在启动Steam…' : '再次启动 Steam';
    if (steamPendingTarget.value === 'bigpicture') return '正在切换到Steam大屏…';
    if (steamPendingTarget.value === 'desktop') return '正在切换到Steam桌面…';
    return '正在确认模式…';
  }
  if (!running.value) return '开启 Steam';
  if (steamUiState.value?.mode === 'desktop') return '点击启动为Steam大屏';
  if (steamUiState.value?.mode === 'bigpicture') return '点击启动为Steam桌面';
  return '重新识别Steam模式';
}
// Stopped: explicit native launch. Running: explicit UI mode change only.
async function toggleSteamPower() {
  if (busy.value) return;
  if (running.value) await switchSteamMode();
  else await launch();
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

// ── 全局刷新监听（App 预加载 / 支持页刷新按钮）──
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) {
  // watch 已在顶部静态导入；动态 import('vue') 会造成异步微任务延迟注册，
  // 刷新事件可能在注册前触发而丢失（2026-08-05 修复）。
  watch(globalRefreshKey, () => {
    if (steamActive) { steamReadBudget.renew(); void refresh(); }
  });
}

onMounted(() => {
  steamActive = true;
  if (!stopSteamUiState) stopSteamUiState = steamUi.subscribe(queueSteamStateRefresh);
  for (const event of steamReadonlyEvents) window.addEventListener(event, queueSteamResumeRefresh);
  // document visibility can return after resume-ready while powerReady is still false.
  // The aggregate lifecycle suppresses that unchanged false state; read-only Steam
  // detection must not miss the document's own visibility edge.
  document.addEventListener('visibilitychange', queueSteamResumeRefresh);
  window.addEventListener('customSteamLibrary:closed', onCustomLibraryClosed);
  window.addEventListener('customSteamLibrary:conflict', onCustomLibraryConflict);
  stopUiVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) cancelSteamPowerWait();
    else if (steamActive) queueSteamResumeRefresh();
  });
  void refresh();

  void refreshCustomLibrarySummary();
});
onActivated(() => {
  steamActive = true;
  if (!stopSteamUiState) stopSteamUiState = steamUi.subscribe(queueSteamStateRefresh);
  if (!stopUiVisibility) {
    stopUiVisibility = onUiVisibilityChange(({ visible }) => {
      if (!visible) cancelSteamPowerWait();
      else if (steamActive) queueSteamResumeRefresh();
    });
  }
  void refresh();

  void refreshCustomLibrarySummary();
});
onDeactivated(() => {
  steamActive = false;
  cancelSteamPowerWait();
});
onBeforeUnmount(() => {
  stopSteamUiState?.(); stopSteamUiState = null;
  for (const event of steamReadonlyEvents) window.removeEventListener(event, queueSteamResumeRefresh);
  document.removeEventListener('visibilitychange', queueSteamResumeRefresh);
  window.removeEventListener('customSteamLibrary:closed', onCustomLibraryClosed);
  window.removeEventListener('customSteamLibrary:conflict', onCustomLibraryConflict);
  steamActive = false;
  cancelSteamPowerWait();
  if (noticeTimer) clearTimeout(noticeTimer);
  stopUiVisibility?.();
  stopUiVisibility = null;
});
</script>

<template>
 <IntegratedPage title="Steam 大屏">
  <div class="page steam-console" data-gp-scope="steam-page" data-gp-spatial="nearest">
   <div v-if="errMsg" class="err-bar" role="alert">{{errMsg}}</div>
   <SettingsSections :sections="steamSections" label="Steam 大屏">
    <template #top>
     <div class="steam-session-bar" aria-label="Steam 运行与大屏操作">
      <div class="states-row steam-session-actions" data-gp-row="0">
        <button
          type="button"
          class="state-card steam-state-card"
          data-gp-row="0"
          data-gp-col="0"
          :class="{ clickable: !busy && !steamChecking }"
          :disabled="busy || steamChecking"
          :title="steamUiState?.mode === 'bigpicture' ? '点击切换到 Steam 大屏窗口' : '检测 Steam 运行状态'"
          @click="onSteamStateClick"
        >
          <span class="dot" :class="{ on: running }"></span>
          <span class="sc-body">
            <span class="sc-title">Steam</span>
            <span class="sc-detail">
              <span class="sc-text">{{ steamStateText() }}</span>
              <span v-if="running && steamUiState?.mode === 'bigpicture'" class="sc-action">点击显示大屏</span>
            </span>
          </span>
        </button>
        <button
          type="button"
          class="steam-power-button"
          data-gp-row="0"
          data-gp-col="1"
          :disabled="busy"
          :title="steamActionText()"
          @click="toggleSteamPower"
        >
          <InlineIcon name="play" />
          {{ steamActionText() }}
        </button>
      </div>

     </div>
    </template>
    <template #library>
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
      <button class="custom-library-open-button" type="button" data-gp-row="3" data-gp-col="0" :disabled="customLibraryBusy" @click="openCustomLibrary">
        <InlineIcon name="play" />
        {{ customLibraryRunning ? '重新打开自定义游戏库' : '打开自定义游戏库' }}
      </button>
    </section>


     <SteamAppearanceCard />
    </template>
    <template #wallpaper>
     <section class="steam-group wallpaper-settings" aria-label="动态壁纸与下载设置">
      <div class="steam-group-head"><h3><InlineIcon name="monitor"/>动态壁纸</h3><span>播放与下载</span></div>
      <SteamWallpaperPlaybackToggle :model-value="wallpaperEnabled" :disabled="wallpaperBusy" @update:model-value="changeWallpaper" />
      <p v-if="wallpaperError" class="steam-section-note" role="alert">动态壁纸设置失败：{{wallpaperError}}</p>
      <Toggle class="steam-option-row" data-steam-wallpaper-setting="download" :model-value="wallpaperDownloadEnabled" label="后台下载" description="电池模式暂停，接通电源后继续" @update:model-value="changeWallpaperDownloads" />
      <div class="steam-option-row steam-option-choice" data-steam-wallpaper-setting="cpu">
       <div class="steam-option-copy"><label id="steam-download-cpu-label">CPU 暂停阈值</label><span id="steam-download-cpu-help">占用超过此值时，暂停后台下载</span></div>
       <Dropdown :model-value="downloadCpuThreshold" :options="downloadCpuOptions" width="112px" aria-label="自动下载 CPU 暂停阈值" aria-describedby="steam-download-cpu-help" @update:model-value="changeDownloadCpuThreshold" />
      </div>
      <div class="steam-wallpaper-status" role="status" aria-live="polite">
       <div class="steam-download-status-head"><span><InlineIcon name="download"/>下载状态</span><span>{{wallpaperDownloadEnabled?'后台下载已开启':'后台下载已关闭'}}</span></div>
       <p>{{downloadDescription(wallpaperDownloads)}}</p>
       <button type="button" class="button steam-wallpaper-download-now" data-gp-id="steam-wallpaper-download-now" :disabled="wallpaperDownloadBusy" @click="requestWallpaperDownloads">{{wallpaperDownloadBusy?'正在提交…':Number(wallpaperDownloads.manualUntil||0)>Date.now()?'停止本轮补齐':'立即补齐壁纸'}}</button>
       <p v-if="wallpaperDownloadError" role="alert">{{wallpaperDownloadError}}</p>
       <p class="steam-section-note">手动补齐本轮忽略 CPU 与游戏暂停；电池、启动和磁盘保护保留。</p>
       <progress v-if="wallpaperDownloadEnabled && (wallpaperDownloads.total || wallpaperDownloads.workerPid)" :value="wallpaperDownloads.totalBytes ? wallpaperDownloads.bytes : wallpaperDownloads.current?.appId ? undefined : wallpaperDownloads.completed" :max="wallpaperDownloads.totalBytes || wallpaperDownloads.total || 1" aria-label="动态壁纸下载进度"></progress>
       <p v-if="wallpaperEnabled && wallpaperPreparation" class="steam-preparation-state">{{wallpaperPreparation}}</p>
      </div>
      <p class="steam-section-note">壁纸素材可在“自定义游戏库”中编辑。</p>
     </section>
    </template>
    <template #integration>
     <section class="steam-group steam-plugin-settings" aria-label="大屏快捷入口">
      <div class="steam-group-head"><h3><InlineIcon name="steam"/>大屏快捷入口</h3></div>
      <Toggle
        :model-value="deckyState?.enabled ?? true"
        label="Steam 插件商店" description="通过 Steam 三点键呼出"
        :disabled="deckyBusy || !deckyState || busy || steamChecking"
        :gp-row="0"
        :gp-col="2"
        @update:model-value="setDeckyEnabled"
      />
      <div class="decky-connection" :data-tone="deckyConnection.tone">
        <div class="decky-connection-copy" role="status" aria-live="polite">
          <span class="decky-connection-label"><i aria-hidden="true"></i>{{deckyConnection.label}}</span>
          <span class="decky-connection-detail">{{deckyRepairMessage || deckyConnection.detail}}</span>
        </div>
        <button v-if="deckySidebarCanRepair(deckyState)" type="button" class="decky-repair-button"
          data-gp-id="steam-decky-repair" data-gp-row="1" data-gp-col="0"
          :disabled="deckyBusy || busy || steamChecking || deckyState?.powerReady === false || !steamUiState?.known || !steamUiState.running || steamUiState.mode!=='bigpicture'"
          @click="repairDeckyConnection">{{deckyBusy?'修复中…':'修复连接'}}</button>
      </div>
      <SteamQuickAccessMapping :enabled="deckyState?.enabled ?? true" />

     </section>
    <section class="card addon-card">
      <h3 class="card-title addon-card-title"><InlineIcon name="link" /> 联动启动项</h3>
      <div id="steam-addon-list" class="addon-list">
        <div v-for="(l, index) in STEAM_ADDONS" :key="l.key" class="addon-row" :data-gp-row="4 + index">
          <button class="addon-launch-btn" :data-gp-row="4 + index" data-gp-col="1" :disabled="busy" title="启动程序" @click="launchFixedAddon(l.exe)">
            <InlineIcon name="play" /> 启动
          </button>
          <Toggle
            v-model="states[l.key]"
            :label="l.name"
            color="accent"
            :gp-row="4 + index"
            :gp-col="0"
            :disabled="busy"
            @update:model-value="(v: boolean) => onAddon(l.key, v)"
          />
        </div>
        <div class="custom-addon-divider">自选联动启动项</div>
        <div v-if="customAddons.length === 0" class="empty-addon">暂无自选程序</div>
        <div v-for="(addon, index) in customAddons" :key="addon.id" class="custom-addon-row" :data-gp-row="4 + STEAM_ADDONS.length + index">
          <button class="addon-launch-btn" :data-gp-row="4 + STEAM_ADDONS.length + index" data-gp-col="1" :disabled="busy" title="启动程序" @click="launchCustomAddon(addon)">
            <InlineIcon name="play" /> 启动
          </button>
          <Toggle
            :model-value="addon.enabled"
            :label="addon.name"
            color="accent"
            :gp-row="4 + STEAM_ADDONS.length + index"
            :gp-col="0"
            :disabled="busy"
            @update:model-value="(v: boolean) => onCustomAddon(addon, v)"
          />
          <button
            class="custom-addon-delete"
            :data-gp-row="4 + STEAM_ADDONS.length + index"
            data-gp-col="2"
            :disabled="busy"
            title="删除联动启动项"
            @click="removeCustomAddon(addon)"
          >
            <InlineIcon name="trash" />
          </button>
        </div>
        <button class="add-addon-btn" :data-gp-row="4 + STEAM_ADDONS.length + customAddons.length" data-gp-col="0" :disabled="busy" @click="addCustomAddon">
          <span class="add-addon-plus">+</span> 添加程序
        </button>
      </div>
    </section>


    </template>
   </SettingsSections>
  </div>
 </IntegratedPage>
</template>

<style scoped>
.decky-connection { display:flex; align-items:center; justify-content:space-between; gap:12px; padding:12px 0 2px; }
.decky-connection-copy { display:grid; gap:5px; min-width:0; }
.decky-connection-label { display:flex; align-items:center; gap:7px; color:var(--text); font-size:13px; line-height:1.4; }
.decky-connection-label i { width:6px; height:6px; flex:0 0 6px; border-radius:50%; background:var(--text-dim); }
.decky-connection[data-tone="connected"] i { background:#65bb8a; }
.decky-connection[data-tone="pending"] i { background:var(--accent); }
.decky-connection[data-tone="warning"] i { background:#cead6f; }
.decky-connection-detail { color:var(--text-dim); font-size:12px; line-height:1.45; overflow-wrap:anywhere; }
.decky-repair-button { display:inline-flex; flex:0 0 auto; align-items:center; justify-content:center; min-height:34px; padding:6px 12px; border:1px solid var(--console-line); border-radius:7px; color:var(--text); background:var(--console-surface-alt); font:inherit; font-size:12px; cursor:pointer; }
.decky-repair-button:hover { border-color:var(--accent); }
.decky-repair-button:disabled { opacity:.55; cursor:wait; }

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
  border: 2px solid color-mix(in srgb, var(--accent) 60%, var(--bg-input));
  background: var(--bg-input);
  border-radius: var(--radius-ctrl);
  padding: 10px 12px;
  color: var(--text);
  text-align: left;
}
.steam-state-card .sc-detail { display: flex; flex-direction: column; align-items: flex-end; min-width: 0; }
.steam-state-card .sc-action {
  color: var(--accent);
  font-size: 11px;
  line-height: 1.4;
}
.steam-state-card.clickable {
  cursor: pointer;
}
.steam-state-card.clickable:hover {
  border-color: var(--accent);
  background: color-mix(in srgb, var(--bg-input) 86%, var(--accent));
}
.steam-state-card:disabled {
  cursor: default;
}
.steam-power-button {
  min-height: 54px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  border: 2px solid var(--accent);
  border-radius: var(--radius-ctrl);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
  color: var(--accent);
  padding: 10px 12px;
  font-size: 13px;
  font-weight: 700;
  white-space: nowrap;
  cursor: pointer;
}
.steam-power-button:hover:not(:disabled) {
  border-color: color-mix(in srgb, var(--accent) 80%, var(--text));
  background: color-mix(in srgb, var(--accent) 18%, var(--bg-input));
}
.steam-state-card:focus-visible,
.steam-power-button:focus-visible {
  box-shadow: var(--focus-ring);
}
.steam-power-button:disabled {
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
.steam-power-button:disabled,
.addon-launch-btn:disabled {
  opacity: 0.58;
}
.addon-card-title { margin-bottom: 10px; }
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
  min-height: 42px;
  padding: 6px 8px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 10px;
  background: color-mix(in srgb, var(--bg-input) 88%, transparent);
}
.addon-row :deep(.toggle-row) {
  min-width: 0;
}
.addon-launch-btn {
  flex: 0 0 auto;
  border: 1px solid color-mix(in srgb, var(--accent) 76%, var(--bg-input));
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
  padding: 6px 8px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 10px;
  background: color-mix(in srgb, var(--bg-input) 88%, transparent);
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
  border: 1px solid color-mix(in srgb, var(--text) 42%, var(--bg-input));
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
  border-color: color-mix(in srgb, var(--accent) 76%, var(--bg-input));
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
  border: 2px solid var(--accent);
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

<style scoped>
.wallpaper-download-cpu{display:flex;align-items:center;justify-content:space-between;gap:16px;font-size:13px;margin:6px 0 10px;}
.wallpaper-download-progress{display:grid;gap:8px;color:var(--text-muted,#9eafbf);font-size:12px;margin:0 0 8px;}
.wallpaper-download-progress progress{width:100%;height:5px;accent-color:#5cafe8;}
</style>

<style scoped>

/* Steam console: shared rows and action columns, without changing the shell zoom. */
.steam-console{min-width:0}
.steam-console :deep(.settings-section-tabs){gap:6px;margin-bottom:12px}
.steam-console :deep(.settings-section-tabs button){min-height:40px;font-size:14px;padding:8px 5px}
.steam-console :deep(.settings-section-heading){display:none}
.steam-console :deep(.settings-section-panel){padding:14px;border-color:var(--console-line);background:var(--console-surface);backdrop-filter:blur(12px)}
.steam-console :deep(.settings-section-panel>.card){margin:0;padding:0;border:0;border-radius:0;background:transparent;box-shadow:none;backdrop-filter:none}
.steam-console :deep(.settings-section-panel>.card+.card),.steam-console .steam-group+.card{padding-top:16px!important;margin-top:16px!important;border-top:1px solid var(--console-line)!important}
.steam-console .steam-session-actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin:0}
.steam-console .steam-session-actions>button{height:52px;min-width:0;padding:8px 12px;border-radius:9px;border:1px solid var(--console-line);background:var(--console-surface);font-size:14px;line-height:1.4;color:var(--text);box-sizing:border-box;justify-content:center}
.steam-console .steam-session-actions>.steam-state-card{display:flex;align-items:center;justify-content:flex-start;gap:9px}
.steam-console .steam-session-actions .sc-body{display:flex;align-items:center;justify-content:space-between;gap:8px;flex:1;min-width:0}
.steam-console .steam-session-actions .sc-title{font-size:14px;font-weight:650}.steam-console .steam-session-actions .sc-text{font-size:12px;margin:0;white-space:nowrap;color:var(--text-dim)}
.steam-console .steam-session-actions>.steam-power-button{display:flex;align-items:center;gap:7px;color:var(--accent);background:var(--accent-surface);border-color:var(--accent-border)}
.steam-group-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:0 0 5px;min-height:28px}
.steam-group-head h3{display:flex;align-items:center;gap:7px;margin:0!important;font-size:16px!important;font-weight:650}
.steam-group-head h3 :deep(svg){color:var(--accent)}.steam-group-head>span{color:var(--text-dim);font-size:12px}
.steam-console .wallpaper-settings .steam-option-row{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:68px;padding:12px 0;border-radius:0;border-bottom:1px solid var(--console-line);box-sizing:border-box}
.steam-console .wallpaper-settings :deep(.toggle-row+.toggle-row){border-top:0}
.steam-console .wallpaper-settings :deep(.toggle-label){font-size:15px;line-height:1.4;font-weight:550}
.steam-console .wallpaper-settings :deep(.toggle-desc){font-size:12px;line-height:1.5;margin-top:4px}
.steam-option-copy{display:grid;gap:4px;min-width:0}.steam-option-copy label{font-size:15px;line-height:1.4;font-weight:550;color:var(--text)}.steam-option-copy>span{font-size:12px;line-height:1.5;color:var(--text-dim)}
.steam-option-choice :deep(.dd){flex:0 0 112px;width:112px!important}.steam-option-choice :deep(.dd-trigger){height:36px;min-width:0;padding:7px 10px;font-size:14px}
.steam-wallpaper-status{display:grid;gap:9px;padding:12px;margin-top:14px;border:1px solid var(--console-line);border-radius:9px;background:var(--console-surface-alt);min-width:0}
.steam-download-status-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:var(--text-dim);font-size:12px}.steam-download-status-head>span:first-child{display:flex;align-items:center;gap:6px;color:var(--text);font-size:13px;font-weight:600}
.steam-wallpaper-status p{font-size:13px!important;line-height:1.55!important;margin:0;overflow-wrap:anywhere;color:var(--text-dim)}
.steam-wallpaper-download-now{justify-self:start;min-height:34px;padding:6px 14px;border:1px solid var(--console-line);border-radius:8px;color:var(--text);background:var(--console-surface);font:inherit;cursor:pointer}.steam-wallpaper-download-now:disabled{opacity:.55;cursor:wait}
.steam-wallpaper-status progress{display:block;width:100%;height:5px;accent-color:var(--accent)}.steam-wallpaper-status p.steam-preparation-state{padding-top:8px;border-top:1px solid var(--console-line);font-size:12px!important}
.steam-section-note{margin:10px 0 0;font-size:12px!important;color:var(--text-dim);line-height:1.5}
.steam-console .custom-library-entry-card h2{display:flex;align-items:center;margin:0;font-size:16px;line-height:1.4;font-weight:650}
.steam-console .custom-library-subtitle{font-size:12px!important;line-height:1.5;margin:6px 0 12px}
.steam-console .custom-library-summary{grid-template-columns:repeat(4,minmax(0,1fr));gap:7px;margin:0 0 12px}
.steam-console .custom-library-summary>div{padding:10px 3px;gap:5px}.steam-console .custom-library-summary strong{font-size:21px}.steam-console .custom-library-summary span{font-size:12px}
.steam-console .custom-library-open-button{height:40px;min-height:40px;gap:7px;border-radius:8px;font-size:14px;padding:8px 12px}
.steam-console :deep(.appearance-card-head h3){font-size:16px;margin:0!important}.steam-console :deep(.appearance-card-head p){font-size:12px!important;margin-top:6px}
.steam-console :deep(.appearance-card-controls){display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px;margin-top:12px}
.steam-console :deep(.appearance-card-controls>button){height:40px;min-width:0;font-size:14px;justify-content:center;padding:8px 10px}
.steam-console :deep(.appearance-status-line){font-size:12px;line-height:1.5;margin-top:10px;overflow-wrap:anywhere}
.steam-console .steam-plugin-settings>:deep(.toggle-row){min-height:54px}
.steam-console :deep(.steam-quick-mapping){margin-top:10px;padding:12px;border-radius:9px;border:1px solid var(--console-line);background:var(--console-surface-alt)}
.steam-console :deep(.mapping-row){display:grid;grid-template-columns:minmax(0,1fr) auto;gap:6px 10px}.steam-console :deep(.mapping-label){font-size:13px}.steam-console :deep(.mapping-value){grid-column:1;grid-row:2;font-size:12px}.steam-console :deep(.mapping-button){grid-column:2;grid-row:1 / span 2;height:36px;font-size:13px}
.steam-console :deep(.mapping-row:has(.mapping-button+.mapping-button)){grid-template-columns:minmax(0,1fr) auto auto}
.steam-console :deep(.mapping-button+.mapping-button){grid-column:3}
.steam-console :deep(.mapping-hint){font-size:12px;line-height:1.5;margin-top:10px}
.steam-console .addon-list{gap:8px}.steam-console .addon-row,.steam-console .custom-addon-row{display:grid;grid-template-columns:minmax(0,1fr) 64px 32px;gap:8px;min-height:54px;padding:6px 8px;border-color:var(--console-line);background:var(--console-surface-alt)}
.steam-console .addon-row>:deep(.toggle-row),.steam-console .custom-addon-row>:deep(.toggle-row){grid-column:1;grid-row:1;min-height:40px;padding:0}
.steam-console .addon-launch-btn{grid-column:2;grid-row:1;height:36px;padding:6px;font-size:13px;text-align:center;border-radius:7px}
.steam-console .custom-addon-delete{grid-column:3;grid-row:1;height:36px;min-width:0;padding:6px;display:flex;align-items:center;justify-content:center}
.steam-console .add-addon-btn{min-height:40px;font-size:14px;margin:0;padding:8px}

</style>
