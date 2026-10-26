<script setup lang="ts">
// [AI:USER:SETTINGS] 外观、诊断日志、更新各按对应控件作用；导出日志→生成日志包，不代表问题已诊断或功能已验收。
// 更新进度/安装结果与设置保存分别呈现；修改本项保留其他模块用户配置，具体持久化读 bridge/settingsRepository.ts。
import DiagnosticLogPauseClearButton from '@/components/DiagnosticLogPauseClearButton.vue';
import SettingsSections from '@/components/SettingsSections.vue';
import BatteryChargeSettings from '@/components/BatteryChargeSettings.vue';
import GpuDedicatedMemorySettings from '@/components/GpuDedicatedMemorySettings.vue';
import AmdFsrUpgradeSettings from '@/components/AmdFsrUpgradeSettings.vue';
import UsbipUpgradeStatus from '@/components/UsbipUpgradeStatus.vue';
import BackgroundEffectPreview from '@/components/BackgroundEffectPreview.vue';
import Slider from '@/components/Slider.vue';
import { updatePredownloadState, setUpdatePredownloadEnabled } from '@/bridge/updatePredownload';
import IntegratedPage from '@/components/IntegratedPage.vue';
import { tdpDiagnosticDeliveryFailures } from '@/bridge/tdpStaticDiagnostics';
import { frontendDiagnosticDeliveryFailures } from '@/robust/frontendDiagnostics';
import { ref, onMounted, onActivated, onBeforeUnmount, computed } from 'vue';
import Toggle from '@/components/Toggle.vue';
import { radialTestingEnabled } from '@/radial/testingState';
import { setRadialTestingPreference } from '@/radial/testing';
import SegButton from '@/components/SegButton.vue';
import { autocloseGet, autocloseSet, msiControllerShieldGet, msiControllerShieldSet, updateAccelGet, updateAccelToggle, sleepFactsGet, sleepFactsSetEnabled, sleepFactsClearLog, sleepFactsExportLog, type AutoCloseConfig, type MsiControllerShieldState, type UpdateAccelState, type SleepFactStatus } from '@/bridge/yeman';
import { shell, fs, dialog, domainLogs } from '@/bridge/api';
import { invoke } from '@/bridge/ipc';
import { APP_VERSION } from '@/version';
import InlineIcon from '@/components/InlineIcon.vue';
import { getTheme, setTheme, type ThemeName } from '@/bridge/theme';
import {
  BACKGROUND_OPACITY_MAX,
  BACKGROUND_OPACITY_MIN,
  BACKGROUND_BLUR_MAX,
  BACKGROUND_BLUR_MIN,
  backgroundGet,
  backgroundInstall,
  backgroundClear,
  getBackgroundOpacity,
  getBackgroundPosition,
  previewBackgroundPosition,
  setBackgroundPosition,
  notifyBackgroundChanged,
  previewBackgroundOpacity,
  setBackgroundOpacity,
  getBackgroundBlur,
  previewBackgroundBlur,
  setBackgroundBlur,
} from '@/bridge/background';
import type { BackgroundPosition } from '@/bridge/backgroundPosition';
import {
  getDynamicBackgroundConfig,
  refreshDynamicBackground,
  setDynamicBackgroundEnabled,
} from '@/bridge/dynamicBackground';
import { cleanGameTitle, detectGame } from '@/bridge/gamedetect';
import { getUiSetting, loadUiSettings, setUiSettings } from '@/bridge/uiSettings';
import {
  getFanFeatureSettings,
  initializeFanFeature,
  setFanDiagnosticLoggingEnabled,
} from '@/bridge/fanFeature';
import { clearFanDiagnosticLogs, exportFanDiagnosticLogs, fanDiagnosticLog } from '@/bridge/fanDiagnostics';
import {
  clearInputDiagnosticLogs,
  checkInputCaptureFileExists,
  exportInputDiagnosticLogs,
  initializeInputDiagnosticsUi,
  inputDiagnosticsLogFiles,
  inputDiagnosticsLoggingEnabled,
  makeUiInputDiagnostic,
  setInputDiagnosticsLoggingEnabled,
  writeInputDiagnostic,
} from '@/bridge/inputDiagnosticsUi';
import {
  ensureUpdateManager,
  checkForUpdate,
  downloadAndInstall,
  updateInfo,
  updateSnapshot,
  updateCacheState, clearUpdateCache,
} from '@/bridge/updateManager';

const errMsg = ref('');
const fxSoundAutoRepair = ref(getUiSetting('fxSoundAutoRepair'));
const fxSoundRepairBusy = ref(false);
async function onFxSoundRepairChange(enabled: boolean): Promise<void> {
  if (fxSoundRepairBusy.value) return;
  fxSoundRepairBusy.value = true;
  try {
    await setUiSettings({ fxSoundAutoRepair: enabled });
    fxSoundAutoRepair.value = getUiSetting('fxSoundAutoRepair');
  } catch (error) {
    fxSoundAutoRepair.value = getUiSetting('fxSoundAutoRepair');
    errMsg.value = 'FxSound 修复设置保存失败：' + (error as Error).message;
  } finally { fxSoundRepairBusy.value = false; }
}
const radialTestingBusy = ref(false);
async function onRadialTestingChange(enabled: boolean): Promise<void> {
  if (radialTestingBusy.value) return;
  radialTestingBusy.value = true;
  try { await setRadialTestingPreference(enabled); }
  catch (error) { errMsg.value = (error as Error).message; }
  finally { radialTestingBusy.value = false; }
}
const settingsSections=[{id:'appearance',title:'外观',description:'',icon:'layers'},{id:'device',title:'设备',description:'',icon:'battery'},{id:'diagnostics',title:'日志修复',description:'',icon:'signal'},{id:'updates',title:'更新',description:'',icon:'tools'}];
async function onUpdatePredownloadChange(enabled:boolean){try{await setUpdatePredownloadEnabled(enabled);}catch(error){errMsg.value=(error as Error).message;}}
const backgroundFileLabel=ref('未选择背景文件'),backgroundPreviewUrl=ref('');

const theme = ref<ThemeName>(getTheme());
const bgEnabled = ref(false);
const bgBusy = ref(false);
const bgKind = ref<'image' | 'video'>('image');
const videoBatteryPause = ref(getUiSetting('videoBatteryPause'));
const windowPlacement = ref(getUiSetting('windowPlacement'));
const bgOpacity = ref(Math.round(getBackgroundOpacity() * 100));
const bgOpacityMax = Math.round(BACKGROUND_OPACITY_MAX * 100);
const bgOpacityMin = Math.round(BACKGROUND_OPACITY_MIN * 100);
const bgBlur = ref(getBackgroundBlur());
const bgPosition = ref(getBackgroundPosition());
let bgPositionRevision = 0, bgPositionEditing = false;
function previewPosition(value: BackgroundPosition) {
  ++bgPositionRevision; bgPositionEditing = true;
  bgPosition.value = previewBackgroundPosition(value);
}
async function savePosition(value: BackgroundPosition) {
  const revision = bgPositionRevision;
  try { await setBackgroundPosition(value); }
  catch (error) {
    if (revision !== bgPositionRevision) return;
    bgPosition.value = previewBackgroundPosition(getBackgroundPosition());
    errMsg.value = `壁纸位置保存失败：${(error as Error).message}`;
  } finally { if (revision === bgPositionRevision) bgPositionEditing = false; }
}
const bgBlurMin = BACKGROUND_BLUR_MIN;
const bgBlurMax = BACKGROUND_BLUR_MAX;
const dynamicEnabled = ref(getDynamicBackgroundConfig().enabled);
// The fallback motion also previews visibility and blur without a video asset.
const backgroundControlEnabled = true;
const dynamicStatus = ref('等待识别当前游戏');
let dynamicToggleGeneration = 0;
const onDynamicProgress = (event: Event) => {
  dynamicStatus.value = (event as CustomEvent<string>).detail || '正在处理动态背景';
};

function previewOpacity(value: string | number): void {
  const percent = Number(value);
  if (!Number.isFinite(percent)) return;
  bgOpacity.value = percent;
  previewBackgroundOpacity(percent / 100);
}


async function saveOpacity(): Promise<void> {
  try { bgOpacity.value = Math.round(await setBackgroundOpacity(bgOpacity.value / 100) * 100); }
  catch (error) {
    bgOpacity.value = Math.round(getBackgroundOpacity() * 100);
    previewBackgroundOpacity(bgOpacity.value / 100);
    errMsg.value = `背景透明度保存失败：${(error as Error).message}`;
  }
}

function previewBlur(value: string | number): void {
  const blur = Number(value);
  if (!Number.isFinite(blur)) return;
  bgBlur.value = blur;
  previewBackgroundBlur(blur);
}


async function saveBlur(): Promise<void> {
  try { bgBlur.value = await setBackgroundBlur(bgBlur.value); }
  catch (error) {
    bgBlur.value = getBackgroundBlur();
    previewBackgroundBlur(bgBlur.value);
    errMsg.value = `背景模糊度保存失败：${(error as Error).message}`;
  }
}

async function loadBackgroundState() {
  const state = await backgroundGet().catch(() => null);
  bgEnabled.value = Boolean(state?.enabled);
  bgKind.value = state?.kind === 'video' ? 'video' : 'image';
  backgroundFileLabel.value=state?.file?.split(/[\\/]/).pop() || '已选择背景';backgroundPreviewUrl.value=state?.enabled ? state.url || '' : '';
}

async function chooseBackground() {
  if (bgBusy.value) return;
  bgBusy.value = true;
  errMsg.value = '';
  try {
    const picked = await dialog.openFile([
      { name: '背景文件', extensions: ['jpg', 'jpeg', 'png', 'mp4'] },
    ]);
    if (!picked) return;
    const state = await backgroundInstall(picked);
    backgroundFileLabel.value=(state.file || picked).split(/[\\/]/).pop() || '已选择背景';backgroundPreviewUrl.value=state.url || '';
    bgKind.value = state.kind === 'video' ? 'video' : 'image';
    bgEnabled.value = Boolean(state.enabled);
    notifyBackgroundChanged(state);
  } catch (e) {
    errMsg.value = '背景图片设置失败：' + (e as Error).message;
  } finally {
    bgBusy.value = false;
  }
}

async function clearBackground() {
  if (bgBusy.value || !bgEnabled.value) return;
  bgBusy.value = true;
  errMsg.value = '';
  try {
    const state = await backgroundClear();
    bgEnabled.value = false;
    bgKind.value = 'image';backgroundPreviewUrl.value='';backgroundFileLabel.value='未选择背景文件';
    notifyBackgroundChanged(state);
  } catch (e) {
    errMsg.value = '清除背景图片失败：' + (e as Error).message;
  } finally {
    bgBusy.value = false;
  }
}

async function onDynamicToggle(value: boolean): Promise<void> {
  const generation = ++dynamicToggleGeneration;
  dynamicEnabled.value = value;
  try {
    await setDynamicBackgroundEnabled(value);
  } catch (error) {
    if (generation !== dynamicToggleGeneration) return;
    dynamicEnabled.value = !value;
    dynamicStatus.value = '设置保存失败';
    errMsg.value = `动态壁纸开关保存失败：${(error as Error).message || '未知错误'}`;
    return;
  }
  if (generation !== dynamicToggleGeneration) return;
  window.dispatchEvent(new CustomEvent('dynamic-background:settings-changed'));
  dynamicStatus.value = value ? '正在识别当前游戏' : '已关闭';
  if (value) void syncDynamicRecognition(generation);
}

async function syncDynamicRecognition(generation = dynamicToggleGeneration): Promise<void> {
  if (generation !== dynamicToggleGeneration || !getDynamicBackgroundConfig().enabled) return;
  dynamicStatus.value = '正在识别当前游戏';
  const game = await detectGame(true).catch(() => null);
  if (generation !== dynamicToggleGeneration || !getDynamicBackgroundConfig().enabled) return;
  if (!game) {
    dynamicStatus.value = '未识别到当前游戏进程';
    return;
  }
  const title = cleanGameTitle(game.title || game.name);
  dynamicStatus.value = `已识别当前游戏：${title}`;
  try {
    const result = await refreshDynamicBackground(game);
    if (result && generation === dynamicToggleGeneration && getDynamicBackgroundConfig().enabled) {
      window.dispatchEvent(new CustomEvent('dynamic-background:loaded', { detail: result.state }));
    }
  } catch (error) {
    if (generation !== dynamicToggleGeneration || !getDynamicBackgroundConfig().enabled) return;
    dynamicStatus.value = `失败：${(error as Error).message || 'Steam 连接失败'}`;
  }
}

function onVideoBatteryPauseChange(value: boolean): void {
  videoBatteryPause.value = value;
  void setUiSettings({ videoBatteryPause: value });
  window.dispatchEvent(new CustomEvent('background:video-battery-pause-changed', { detail: { enabled: value } }));
}

function onThemeChange(value: string | number): void {
  const next = value as ThemeName;
  theme.value = next;
  setTheme(next);
}
async function onWindowPlacementChange(value: string | number): Promise<void> {
  if (value !== 'left' && value !== 'right') return;
  errMsg.value = '';
  try {
    await setUiSettings({ windowPlacement: value });
    windowPlacement.value = getUiSetting('windowPlacement');
  } catch (error) {
    windowPlacement.value = getUiSetting('windowPlacement');
    errMsg.value = '窗口位置保存失败：' + (error instanceof Error ? error.message : String(error));
  }
}

// ── 识别软件关闭野蛮系统前端：总开关 + 可编辑进程名列表 ──
const autoClose = ref<AutoCloseConfig>({ enabled: false, procs: [] });
const acBusy = ref(false);

// ── 微星可选前端屏蔽：仅从成功的 IPC 快照展示执行结果，不轮询 ──
const msiControllerShield = ref<MsiControllerShieldState>({ ok: false, supported: false, enabled: false });
const msiShieldBusy = ref(false);
const msiShieldStatus = ref('');
const msiShieldSnapshotCurrent = ref(false);
const msiShieldExecutionStatus = computed(() => {
  const state = msiControllerShield.value;
  if (!msiShieldSnapshotCurrent.value || state.ok !== true) return '总体 待确认';
  // Exhaustion is terminal even if an older backend still reports pending=true.
  if (state.lastOutcome === 'retry-exhausted') return '总体 执行失败';
  const epochPending = state.requestedEpoch !== undefined && state.completedEpoch !== undefined && state.requestedEpoch !== state.completedEpoch;
  if (state.pending === true || epochPending || state.lastOutcome === 'pending' || state.lastOutcome === 'retrying') return '总体 执行中';
  if (state.lastOutcomeKnown !== false && state.lastOutcomeOk !== false && (state.lastOutcome === 'applied-or-restored' || state.applied === true)) {
    if (state.enabled) return state.applied === true ? '总体 OK（屏蔽已执行）' : '总体 待确认';
    if (state.applied === true) return '总体 待确认';
    if (state.shortcutOwnerActive === true || state.isolationOwnerActive === true) return '总体 仍在屏蔽';
    if (state.shortcutOwnerActive === undefined || state.isolationOwnerActive === undefined) return '总体 待确认';
    return '总体 OK（屏蔽已关闭）';
  }
  return '总体 待确认';
});
const msiShieldShortcutStatus = computed(() => {
  if (!msiShieldSnapshotCurrent.value) return '待确认';
  const active = msiControllerShield.value.shortcutOwnerActive;
  return active === true ? '开启' : active === false ? '关闭' : '待确认';
});
const msiShieldServiceStatus = computed(() => {
  const state = msiControllerShield.value;
  const observation = state.protectedServiceObservation;
  // The service read belongs to the last operation, never a live query by refresh.
  if (!msiShieldSnapshotCurrent.value || state.pending === true || !observation ||
      state.requestedEpoch === undefined || state.completedEpoch !== state.requestedEpoch ||
      observation.observedRequestedEpoch !== state.requestedEpoch || observation.stateRead !== true) return '待确认';
  if (observation.serviceState === 1) return '关闭（执行时）';
  if (observation.serviceState === 4) return '开启（执行时）';
  if (observation.serviceState === 7) return '暂停（执行时）';
  return [2, 3, 5, 6].includes(Number(observation.serviceState)) ? '切换中（执行时）' : '待确认';
});

async function refreshMsiControllerShield(): Promise<void> {
  // Shared guard serializes initial/activation reads, explicit refresh and set.
  if (msiShieldBusy.value) return;
  msiShieldBusy.value = true;
  msiShieldSnapshotCurrent.value = false;
  msiShieldStatus.value = '';
  try {
    const next = await msiControllerShieldGet();
    if (next.ok !== true) {
      // Admission can be known even when the execution snapshot is unavailable.
      // Keep that native capability visible without claiming an applied state.
      if (next.supported === true) msiControllerShield.value = { ...msiControllerShield.value, supported: true };
      msiShieldStatus.value = '读取失败，请重新进入设置';
      console.warn('[msi-controller-shield] read failed', next.reason);
      return;
    }
    msiControllerShield.value = next;
    msiShieldSnapshotCurrent.value = true;
  } catch (e) {
    msiShieldStatus.value = '读取失败，请重新进入设置';
    console.warn('[msi-controller-shield] read failed', e);
  } finally {
    msiShieldBusy.value = false;
  }
}

// ── 更新加速器：手动按钮 + 文件/运行状态 ──
const updateAccel = ref<UpdateAccelState>({
  exists: false,
  running: false,
});
const uaBusy = ref(false);
const fanDiagnosticLoggingEnabled = ref(getFanFeatureSettings().diagnosticLoggingEnabled === true);
const fanLogBusy = ref(false);
const fanLogStatus = ref('');
const sleepFacts = ref<SleepFactStatus | null>(null);
const sleepLogBusy = ref(false);
const sleepLogStatus = ref('');
const inputLogBusy = ref(false);
const inputLogStatus = ref('');

// ── 诊断日志（2026-09-17 合并批：旧 5 开关 → 1 个详细开关 + 整包导出）──
// 默认档（常开、无 UI）：仅错误与关键事件；详细档：全量诊断明细。
const detailLoggingEnabled = ref(false);
const detailLogBusy = ref(false);
const detailMaintenanceBusy = ref(false);
// Export feedback belongs below the diagnostic card's buttons, not in a popup.
const detailLogStatus = ref('');
async function loadDetailLogging(): Promise<void> {
  try {
    const r = await invoke<{ gyro?: boolean; virtual?: boolean }>('logs.domainGetEnabled');
    detailLoggingEnabled.value = r.virtual === true;
  } catch {
    /* 保持默认 */
  }
}
async function toggleDetailLogging(): Promise<void> {
  if (detailLogBusy.value) return;
  detailLogBusy.value = true;
  errMsg.value = '';
  try {
    const r = await invoke<{ ok?: boolean; virtual?: boolean; reason?: string }>('logs.domainSetEnabled', {
      domain: 'virtual',
      enabled: !detailLoggingEnabled.value,
    });
    if (r.ok !== true || typeof r.virtual !== 'boolean') throw new Error(r.reason || '日志设置未能确认保存');
    detailLoggingEnabled.value = r.virtual;
  } catch (e) {
    // Failed changes still need to be visible; successful changes have no extra feedback.
    errMsg.value = '日志设置失败：' + (e instanceof Error ? e.message : String(e));
  } finally {
    detailLogBusy.value = false;
  }
}
async function exportAllLogs(): Promise<void> {
  if (detailLogBusy.value) return;
  detailLogBusy.value = true;
  detailLogStatus.value = '正在打包全部日志…';
  try {
    const r = await invoke<{
      ok?: boolean; path?: string; count?: number; reason?: string;
      lastError?: string; win32Error?: number; attempts?: number; cleanupOk?: boolean;
      snapshotBoundaryWarnings?: number; snapshotInspectionFailures?: number;
      steamLogs?: { status?: string; copied?: number; skipped?: number; discoveryIssues?: number };
    }>(
      'logs.exportAll',
      { tdpDiagnosticDeliveryFailures: tdpDiagnosticDeliveryFailures(), frontendDiagnosticDeliveryFailures: frontendDiagnosticDeliveryFailures() },
      { timeoutMs: 150000 },
    );
    if (r.ok !== true || !r.path) {
      const details = [r.reason || '导出失败'];
      if (r.lastError) details.push(`失败阶段：${r.lastError}`);
      if (typeof r.win32Error === 'number') details.push(`Win32 代码：${r.win32Error}`);
      if (typeof r.attempts === 'number') details.push(`尝试次数：${r.attempts}`);
      if (r.cleanupOk === false) details.push('临时文件清理未完成');
      throw new Error(details.join('；'));
    }
    detailLogStatus.value = `已导出 ${r.count ?? 0} 个日志文件到桌面：${r.path}`;
    if ((r.snapshotBoundaryWarnings ?? 0) > 0) detailLogStatus.value += `；${r.snapshotBoundaryWarnings} 个日志存在快照半条尾记录，原始内容已保留，详见 log-export-snapshot.json`;
    if ((r.snapshotInspectionFailures ?? 0) > 0) detailLogStatus.value += `；${r.snapshotInspectionFailures} 项快照检查未完成，详见 log-export-snapshot.json`;
    if (r.steamLogs) {
      const steam = r.steamLogs;
      const issues = (steam.skipped ?? 0) + (steam.discoveryIssues ?? 0);
      detailLogStatus.value += `；Steam 日志已复制 ${steam.copied ?? 0} 个`;
      if (issues > 0) detailLogStatus.value += `，收集不完整（${issues} 项），详见包内 steam/steam-log-export.json`;
      else if (steam.status !== 'collected') detailLogStatus.value += `（${steam.status || '状态未知'}）`;
    }
    if (r.cleanupOk === false) detailLogStatus.value += '；临时文件清理未完成';
  } catch (e) {
    detailLogStatus.value = '导出失败：' + (e instanceof Error ? e.message : String(e));
  } finally {
    detailLogBusy.value = false;
  }
}

// 版本号：构建期由 version.json 注入（scripts/write-version.mjs → src/version.ts）
const appVersion = APP_VERSION;

const updateBusy = computed(() => updateCacheState.busy || ['checking', 'downloading', 'validating', 'installing'].includes(updateSnapshot.phase));
const updateCacheBlocked = computed(() => updateCacheState.busy || updateSnapshot.phase === 'installing' ||
  (!updateSnapshot.background && ['downloading', 'validating'].includes(updateSnapshot.phase)));
const updateProgressPercent = computed(() => {
  const n = Number(updateSnapshot.percent);
  return Number.isFinite(n) ? Math.max(0, Math.min(100, n)) : 0;
});
const updateStatusText = computed(() => {
  if (updateSnapshot.phase === 'checking') return '正在检查更新';
  if (updateSnapshot.phase === 'available') return `发现新版本 ${updateSnapshot.version || updateInfo.value?.version || ''}`;
  if (updateSnapshot.phase === 'downloading') return updateSnapshot.message || '正在下载更新包';
  if (updateSnapshot.phase === 'validating') return '正在校验更新包';
  if (updateSnapshot.phase === 'downloaded') return '更新包已准备完成';
  if (updateSnapshot.phase === 'installing') return '正在安装并重启程序';
  if (updateSnapshot.phase === 'latest') return '当前已是最新版本';
  if (updateSnapshot.phase === 'interrupted') {
    return updateSnapshot.stage === 'install' ? '上次安装未完成，可重试安装' : '上次下载未完成，可继续下载';
  }
  if (updateSnapshot.phase === 'completed') return '更新已完成';
  if (updateSnapshot.phase === 'failed') return updateSnapshot.error || '更新失败';
  return '尚未检查更新';
});
function formatBytes(value: number | undefined): string {
  const n = Number(value) || 0;
  if (n < 1024) return `${Math.round(n)} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GB`;
}
function formatSpeed(value: number | undefined): string {
  const n = Number(value) || 0;
  return n > 0 ? `${formatBytes(n)}/s` : '--';
}
function formatEta(value: number | undefined): string {
  const n = Math.max(0, Math.round(Number(value) || 0));
  if (!n) return '--';
  if (n < 60) return `${n} 秒`;
  return `${Math.floor(n / 60)} 分 ${n % 60} 秒`;
}

async function load() {
  errMsg.value = '';
  void loadDetailLogging();
  try {
    autoClose.value = await autocloseGet();
  } catch {
    /* 保持默认值 */
  }
  await refreshMsiControllerShield();
  try {
    updateAccel.value = await updateAccelGet();
  } catch {
    /* 保持默认值 */
  }
}

async function saveAutoClose(opts: { procsOnly?: boolean } = {}) {
  acBusy.value = true;
  const snapshot = { enabled: autoClose.value.enabled, procs: [...autoClose.value.procs] };
  try {
    const next = await autocloseSet(snapshot);
    autoClose.value = next;
  } catch (e) {
    errMsg.value = '自动关闭设置失败：' + (e as Error).message;
  } finally {
    acBusy.value = false;
  }
}

async function onAutoCloseToggle(v: boolean) {
  errMsg.value = '';
  const prev = autoClose.value.enabled;
  autoClose.value.enabled = v; // 乐观更新
  try {
    const next = await autocloseSet({ enabled: v, procs: [...autoClose.value.procs] });
    autoClose.value = next;
  } catch (e) {
    autoClose.value.enabled = prev;
    errMsg.value = '自动关闭开关失败：' + (e as Error).message;
  }
}

function onAcProcAdd() {
  autoClose.value.procs.push('');
}

function onAcProcRemove(idx: number) {
  autoClose.value.procs.splice(idx, 1);
  saveAutoClose();
}

async function onAcProcInput(idx: number, val: string) {
  autoClose.value.procs[idx] = val;
  // 输入即存（失焦/回车时触发 @change），避免退出页面丢失
  await saveAutoClose();
}

async function onMsiControllerShieldToggle(enabled: boolean): Promise<void> {
  if (msiShieldBusy.value) return;
  msiShieldBusy.value = true;
  msiShieldSnapshotCurrent.value = false;
  msiShieldStatus.value = '';
  errMsg.value = '';
  try {
    const next = await msiControllerShieldSet(enabled);
    if (next.ok !== true) {
      console.warn('[msi-controller-shield] set failed', next.reason);
      // A rejected write can still read back the currently stored request.
      // Use that scalar, not !enabled or stale UI state; no runtime success.
      if (next.supported === true) {
        msiControllerShield.value = { ...msiControllerShield.value, enabled: next.enabled };
        msiShieldStatus.value = '设置失败，请重新进入设置';
      } else {
        msiShieldStatus.value = '设置失败，请重新进入设置';
      }
      return;
    }
    msiControllerShield.value = next;
    msiShieldSnapshotCurrent.value = true;
    msiShieldStatus.value = next.enabled === enabled
      ? ''
      : '开关未更新，请重新进入设置';
    if (next.enabled !== enabled) msiShieldSnapshotCurrent.value = false;
  } catch (e) {
    msiShieldStatus.value = '设置失败，请重新进入设置';
    console.warn('[msi-controller-shield] set failed', e);
  } finally {
    msiShieldBusy.value = false;
  }
}

async function onUpdateAccelToggle() {
  errMsg.value = '';
  uaBusy.value = true;
  try {
    const next = await updateAccelToggle();
    updateAccel.value = next;
    if (!next.ok) {
      errMsg.value = next.running
        ? '停止加速失败'
        : next.exists
          ? '启动加速失败'
          : '加速器文件未找到';
    }
  } catch (e) {
    errMsg.value = '更新加速操作失败：' + (e as Error).message;
  } finally {
    uaBusy.value = false;
  }
}

async function toggleFanDiagnosticLogging(): Promise<void> {
  if (fanLogBusy.value) return;
  const next = !fanDiagnosticLoggingEnabled.value;
  fanLogBusy.value = true;
  fanLogStatus.value = '';
  try {
    await setFanDiagnosticLoggingEnabled(next);
    fanDiagnosticLoggingEnabled.value = next;
    fanDiagnosticLog('ui.diagnostic-logging-changed', { enabled: next, surface: 'settings' });
    fanLogStatus.value = next ? '风扇日志已开启' : '风扇日志已关闭';
  } catch (error) {
    fanLogStatus.value = `风扇日志设置失败：${error instanceof Error ? error.message : String(error)}`;
  } finally {
    fanLogBusy.value = false;
  }
}

async function clearFanLogs(): Promise<void> {
  if (fanLogBusy.value) return;
  fanLogBusy.value = true;
  fanLogStatus.value = '';
  try {
    fanLogStatus.value = (await clearFanDiagnosticLogs()) ? '风扇日志已清空' : '风扇日志清空失败';
  } catch (error) {
    fanLogStatus.value = `风扇日志清空失败：${error instanceof Error ? error.message : String(error)}`;
  } finally {
    fanLogBusy.value = false;
  }
}

async function exportFanLogs(): Promise<void> {
  if (fanLogBusy.value) return;
  fanLogBusy.value = true;
  fanLogStatus.value = '';
  try {
    const result = await exportFanDiagnosticLogs();
    fanLogStatus.value = result.ok && result.path
      ? `风扇日志已导出到桌面：${result.path}`
      : (result.reason || '暂无风扇日志可导出');
  } catch (error) {
    fanLogStatus.value = `风扇日志导出失败：${error instanceof Error ? error.message : String(error)}`;
  } finally {
    fanLogBusy.value = false;
  }
}

async function toggleInputDiagnosticLogging(): Promise<void> {
  if (inputLogBusy.value) return;
  inputLogBusy.value = true;
  inputLogStatus.value = '';
  try {
    await setInputDiagnosticsLoggingEnabled(!inputDiagnosticsLoggingEnabled.value);
    if (inputDiagnosticsLoggingEnabled.value) {
      writeInputDiagnostic(makeUiInputDiagnostic('input-diagnostic-logging-changed', 'set-enabled', 'ok'));
    }
    inputLogStatus.value = inputDiagnosticsLoggingEnabled.value ? '输入日志已开启' : '输入日志已关闭';
  } catch (error) {
    inputLogStatus.value = `输入日志设置失败：${error instanceof Error ? error.message : String(error)}`;
  } finally { inputLogBusy.value = false; }
}
async function clearInputLogs(): Promise<void> {
  if (inputLogBusy.value) return;
  inputLogBusy.value = true;
  inputLogStatus.value = '';
  try {
    await clearInputDiagnosticLogs();
    await refreshNativeInputLogFiles();
    inputLogStatus.value = '输入日志已清空';
  } catch (error) {
    inputLogStatus.value = `输入日志清空失败：${error instanceof Error ? error.message : String(error)}`;
  } finally { inputLogBusy.value = false; }
}
async function exportInputLogs(): Promise<void> {
  if (inputLogBusy.value) return;
  inputLogBusy.value = true;
  inputLogStatus.value = '';
  try { inputLogStatus.value = `输入日志已导出：${await exportInputDiagnosticLogs()}`; }
  catch (error) { inputLogStatus.value = `输入日志导出失败：${error instanceof Error ? error.message : String(error)}`; }
  finally { inputLogBusy.value = false; }
}
async function refreshNativeInputLogFiles(): Promise<void> {
  try { inputDiagnosticsLogFiles.value = await checkInputCaptureFileExists(); }
  catch { inputDiagnosticsLogFiles.value = false; }
}

// ── 日志域：陀螺仪 / 虚拟手柄（独立文件；单文件 ≤5MB 自动切分）──
const gyroDomainLogEnabled = ref(false);
const virtualDomainLogEnabled = ref(false);
const domainLogBusy = ref(false);
const domainLogStatus = ref<Record<'gyro' | 'virtual', string>>({ gyro: '', virtual: '' });
async function refreshDomainLogStates(): Promise<void> {
  try {
    const state = await domainLogs.getEnabled();
    gyroDomainLogEnabled.value = state.gyro;
    virtualDomainLogEnabled.value = state.virtual;
  } catch {
    /* 读取失败保持默认 */
  }
}
async function toggleDomainLog(domain: 'gyro' | 'virtual'): Promise<void> {
  if (domainLogBusy.value) return;
  const next = domain === 'gyro' ? !gyroDomainLogEnabled.value : !virtualDomainLogEnabled.value;
  domainLogBusy.value = true;
  domainLogStatus.value[domain] = '';
  try {
    const state = await domainLogs.setEnabled(domain, next);
    gyroDomainLogEnabled.value = state.gyro;
    virtualDomainLogEnabled.value = state.virtual;
    const name = domain === 'gyro' ? '陀螺仪日志' : '虚拟手柄日志';
    domainLogStatus.value[domain] = next ? `${name}已开启` : `${name}已关闭`;
  } catch (error) {
    domainLogStatus.value[domain] = `日志设置失败：${error instanceof Error ? error.message : String(error)}`;
  } finally { domainLogBusy.value = false; }
}
async function clearDomainLog(domain: 'gyro' | 'virtual'): Promise<void> {
  if (domainLogBusy.value) return;
  domainLogBusy.value = true;
  domainLogStatus.value[domain] = '';
  try {
    const result = await domainLogs.clear(domain);
    domainLogStatus.value[domain] = result.ok ? '日志已清空' : `日志清空失败：${result.reason || '未知原因'}`;
  } catch (error) {
    domainLogStatus.value[domain] = `日志清空失败：${error instanceof Error ? error.message : String(error)}`;
  } finally { domainLogBusy.value = false; }
}
async function exportDomainLog(domain: 'gyro' | 'virtual'): Promise<void> {
  if (domainLogBusy.value) return;
  domainLogBusy.value = true;
  domainLogStatus.value[domain] = '';
  try {
    const result = await domainLogs.export(domain);
    domainLogStatus.value[domain] = result.ok && result.path
      ? `已导出到桌面：${result.path}`
      : `暂无日志可导出（${result.reason || '未知原因'}）`;
  } catch (error) {
    domainLogStatus.value[domain] = `日志导出失败：${error instanceof Error ? error.message : String(error)}`;
  } finally { domainLogBusy.value = false; }
}

async function refreshSleepFacts(): Promise<void> {
  try {
    sleepFacts.value = await sleepFactsGet();
  } catch {
    // 睡眠日志是诊断项，读取失败不能阻塞设置页其它功能。
  }
}

async function toggleSleepFactLogging(): Promise<void> {
  if (sleepLogBusy.value) return;
  const next = sleepFacts.value?.enabled !== true;
  sleepLogBusy.value = true;
  sleepLogStatus.value = '';
  try {
    sleepFacts.value = await sleepFactsSetEnabled(next);
    sleepLogStatus.value = next ? '睡眠日志已开启' : '睡眠日志已关闭';
  } catch (error) {
    sleepLogStatus.value = `睡眠日志设置失败：${error instanceof Error ? error.message : String(error)}`;
  } finally {
    sleepLogBusy.value = false;
  }
}

async function clearSleepLogs(): Promise<void> {
  if (sleepLogBusy.value) return;
  sleepLogBusy.value = true;
  sleepLogStatus.value = '';
  try {
    const result = await sleepFactsClearLog();
    sleepLogStatus.value = result.ok ? '睡眠日志已清空' : '睡眠日志清空失败';
  } catch (error) {
    sleepLogStatus.value = `睡眠日志清空失败：${error instanceof Error ? error.message : String(error)}`;
  } finally {
    sleepLogBusy.value = false;
  }
}

async function exportSleepLogs(): Promise<void> {
  if (sleepLogBusy.value) return;
  sleepLogBusy.value = true;
  sleepLogStatus.value = '';
  try {
    const result = await sleepFactsExportLog();
    sleepLogStatus.value = result.ok && result.path
      ? `睡眠日志已导出到桌面：${result.path}`
      : (result.reason || '暂无睡眠日志可导出');
  } catch (error) {
    sleepLogStatus.value = `睡眠日志导出失败：${error instanceof Error ? error.message : String(error)}`;
  } finally {
    sleepLogBusy.value = false;
  }
}

// ── 以下原属「支持」页 ──
async function openSupport() {
  try {
    await shell.open('C:\\SOFT\\YeMan\\YeManCC\\YeMan-Support.html');
  } catch {
    /* ignore */
  }
}

async function openHome() {
  try {
    await shell.open('https://link3.cc/yeman');
  } catch {
    /* ignore */
  }
}

async function openGithub() {
  try {
    await shell.open('https://github.com/DaVeZhouMK/YeManCC');
  } catch {
    /* ignore */
  }
}

function syncAppearanceSettings(): void {
  fxSoundAutoRepair.value = getUiSetting('fxSoundAutoRepair');
  theme.value=getTheme();windowPlacement.value=getUiSetting('windowPlacement');
  bgOpacity.value=Math.round(getBackgroundOpacity()*100);bgBlur.value=getBackgroundBlur();
  if (!bgPositionEditing) bgPosition.value=getBackgroundPosition();
  videoBatteryPause.value=getUiSetting('videoBatteryPause');dynamicEnabled.value=getDynamicBackgroundConfig().enabled;
}
onMounted(async () => {
  const fanSettings = await initializeFanFeature();
  await initializeInputDiagnosticsUi();
  await refreshNativeInputLogFiles();
  await refreshDomainLogStates();
  fanDiagnosticLoggingEnabled.value = fanSettings.diagnosticLoggingEnabled === true;
  await ensureUpdateManager();
  await loadUiSettings();
  fxSoundAutoRepair.value = getUiSetting('fxSoundAutoRepair');
  videoBatteryPause.value = getUiSetting('videoBatteryPause');
  windowPlacement.value = getUiSetting('windowPlacement');
  bgOpacity.value = Math.round(getBackgroundOpacity() * 100);
  bgBlur.value = getBackgroundBlur();
  bgPosition.value = getBackgroundPosition();
  dynamicEnabled.value = getDynamicBackgroundConfig().enabled;
  load();
  void refreshSleepFacts();
  void loadBackgroundState();
  window.addEventListener('dynamic-background:progress', onDynamicProgress);
  window.addEventListener('ui-settings:changed',syncAppearanceSettings);
});
// KeepAlive 缓存下切回本页时主动刷新，避免「手柄设置/背景状态在其它页面变更后不刷新」
// （2026-08-05 补充 onActivated）。
onActivated(() => {
  syncAppearanceSettings();
  void ensureUpdateManager();
  load();
  void refreshSleepFacts();
  void refreshNativeInputLogFiles();
  void loadBackgroundState();
});
onBeforeUnmount(() => {
  window.removeEventListener('dynamic-background:progress', onDynamicProgress);
  window.removeEventListener('ui-settings:changed',syncAppearanceSettings);
});
</script>

<template>
<IntegratedPage title="设置" hide-heading><div class="page modern-settings-page settings-console-page settings-density-preview"><div v-if="errMsg" class="err-bar page-error" role="alert">{{errMsg}}</div>
<SettingsSections :sections="settingsSections" label="设置" class="settings-layout settings-layout-b settings-console">
<template #appearance="{ active }"><div class="appearance-flow"><div class="setting-position">

 <section class="card window-position-settings"><h3 class="card-title"><InlineIcon name="target" />窗口位置</h3><div class="window-placement-options" role="group" aria-label="窗口位置"><button type="button" :class="{active:windowPlacement==='left'}" :aria-pressed="windowPlacement==='left'" @click="onWindowPlacementChange('left')">左侧</button><button type="button" :class="{active:windowPlacement==='right'}" :aria-pressed="windowPlacement==='right'" @click="onWindowPlacementChange('right')">右侧</button></div></section></div>
 <div class="setting-color"><section class="card interface-color-settings">
      <h3 class="card-title"><InlineIcon name="star" /> 界面颜色</h3>
      <SegButton
        :model-value="theme"
        :options="[
          { value: 'blue-black', label: '蓝黑' },
          { value: 'red-black', label: '红黑' },
          { value: 'cyberpunk', label: '黄黑' },
        ]"
        color="accent"
        full
        @update:model-value="onThemeChange"
      />
    </section></div>
 <div class="setting-wallpaper"><section class="card background-settings-card" aria-label="动态壁纸">
 <h3 class="card-title"><InlineIcon name="monitor" />动态壁纸</h3>
 <div class="wallpaper-editor-layout">
  <div class="wallpaper-preview-column"><BackgroundEffectPreview v-if="active" :opacity="bgOpacity / 100" :blur="bgBlur" :theme="theme" :media-url="backgroundPreviewUrl" :media-kind="bgKind" :position="bgPosition" @update:position="previewPosition" @change="savePosition" /></div>
  <div class="wallpaper-controls-column">
   <div class="background-choice-summary"><div class="background-source-line"><div><strong>{{bgEnabled?(bgKind==='video'?'视频背景':'图片背景'):'动态效果'}}</strong><span :title="backgroundFileLabel">{{bgEnabled?backgroundFileLabel:'未选择壁纸'}}</span></div></div></div>
   <div class="background-actions"><button type="button" class="bg-icon-btn" :disabled="bgBusy" @click="chooseBackground"><InlineIcon name="edit"/>{{bgBusy?'处理中…':bgEnabled?'更换背景':'选择背景'}}</button><button type="button" class="bg-icon-btn danger" :disabled="bgBusy || !bgEnabled" @click="clearBackground"><InlineIcon name="trash"/>清除背景</button></div>
   <div class="background-tuning"><Slider :model-value="bgOpacity" label="背景可见度" unit="%" :min="bgOpacityMin" :max="bgOpacityMax" :step="1" :accelerate="false" :disabled="!backgroundControlEnabled" @update:model-value="previewOpacity" @commit="saveOpacity"/><Slider :model-value="bgBlur" label="背景模糊" unit="px" :min="bgBlurMin" :max="bgBlurMax" :step="1" :accelerate="false" :disabled="!backgroundControlEnabled" @update:model-value="previewBlur" @commit="saveBlur"/></div>
 </div>
 <div class="settings-option-list wallpaper-footer"><Toggle :model-value="videoBatteryPause" label="离电暂停视频" color="accent" @update:model-value="onVideoBatteryPauseChange"/><Toggle :model-value="dynamicEnabled" label="跟随当前游戏背景" color="accent" @update:model-value="onDynamicToggle"/></div>
 <p v-if="dynamicEnabled" class="background-feedback" role="status">{{dynamicStatus}}</p>
 </div>
</section></div></div></template>
 <!-- AI-NAV[battery]: keep the actual charge consumer mounted in this route.
      Query only the active device tab; model admission and writes stay in BatteryHost/Controller. -->
 <template #device="{ active }"><div class="device-flow"><BatteryChargeSettings v-if="active" />
 <GpuDedicatedMemorySettings v-if="active" />
 <AmdFsrUpgradeSettings v-if="active" />
    <section v-if="msiControllerShield.supported" class="card msi-controller-shield-card" aria-label="微星专属修复">
      <h3 class="card-title">微星专属修复</h3>
      <Toggle :model-value="msiControllerShield.enabled" label="微星前端屏蔽" color="accent" :disabled="msiShieldBusy" data-msi-controller-shield-toggle @update:model-value="onMsiControllerShieldToggle" />
      <p class="muted body" role="status">微星屏蔽 {{ msiControllerShield.enabled ? '开启' : '关闭' }} · {{ msiShieldExecutionStatus }}</p>
      <p class="muted body">专用键接管 {{ msiShieldShortcutStatus }} · 基础服务 {{ msiShieldServiceStatus }} · 重启 无需</p>
      <p class="muted body">开启后关闭 MSI Center M 前端及 Server/Updater 启动任务，保留基础服务。<br />按键效果 待验证<span v-if="msiShieldExecutionStatus === '总体 OK（屏蔽已关闭）'"> · 前端未运行时可手动启动</span></p>
      <p v-if="msiShieldStatus" class="muted body" role="status">{{ msiShieldStatus }}</p>
    </section>
 <section v-if="active" class="card usbip-settings-card" aria-label="USB/IP 配套">
   <h3 class="card-title"><InlineIcon name="tools" /> USB/IP 配套</h3>
   <UsbipUpgradeStatus />
 </section>
 </div></template>
 <template #diagnostics="{ active }"><div class="diagnostics-flow"><section class="card fan-log-card" aria-label="诊断日志">
      <h3 class="card-title">诊断日志</h3>
      <div class="fan-log-actions">
        <button class="fan-log-action" :class="{ enabled: detailLoggingEnabled }" type="button" :disabled="detailLogBusy || detailMaintenanceBusy" :aria-pressed="detailLoggingEnabled" @click="toggleDetailLogging">{{ detailLoggingEnabled ? '高频日志 · 开' : '高频日志 · 关' }}</button>
        <DiagnosticLogPauseClearButton :disabled="detailLogBusy" @busy-change="value=>detailMaintenanceBusy=value" @changed="loadDetailLogging" @status="value=>detailLogStatus=value" @error="value=>errMsg=value" />
        <button class="fan-log-action" type="button" :disabled="detailLogBusy || detailMaintenanceBusy" @click="exportAllLogs">导出日志</button>
      </div>
      <p v-if="detailLogStatus" class="muted body fan-log-status" role="status">{{ detailLogStatus }}</p>
    </section>
  <section class="card fxsound-repair-card" aria-label="FxSound 修复">
    <h3 class="card-title">音频修复</h3>
    <Toggle :model-value="fxSoundAutoRepair" label="FxSound 自动修复" color="accent" :disabled="fxSoundRepairBusy" data-fxsound-repair-toggle @update:model-value="onFxSoundRepairChange" />
    <p class="muted body">仅在启动 YMCC 时检查一次。若 FxSound 持续 3 秒未运行且占用默认输出，则恢复此前的系统播放设备；结束后不再干预，正常使用 FxSound 不受影响。</p>
  </section>
    <section class="card radial-testing-card" aria-label="测试功能">
      <h3 class="card-title">测试功能</h3>
      <Toggle :model-value="radialTestingEnabled" label="轮盘侧边栏" color="accent" :disabled="radialTestingBusy" data-radial-testing-toggle @update:model-value="onRadialTestingChange" />
      <p class="muted body">开启后显示“触摸轮盘 · 测试”入口。默认隐藏且不启动；监控长按、全屏长按与反馈需分别开启。</p>
    </section></div></template>
 <template #updates><div class="updates-flow"><section class="card update-card">
      <div class="card-header-row">
        <h3 class="card-title"><InlineIcon name="package" /> 版本和更新</h3>
        <div class="update-version-actions">
          <button
            class="action-btn outline update-accel-btn"
            :class="{ active: updateAccel.running }"
            :disabled="uaBusy || !updateAccel.exists"
            :title="updateAccel.exists ? (updateAccel.running ? '关闭更新加速' : '开启更新加速') : '更新加速文件未找到'"
            @click="onUpdateAccelToggle"
          >
            {{ updateAccel.running ? '加速已开启' : '加速未开启' }}
          </button>
          
        </div>
      </div>
      <div class="update-version-summary"><div><span>当前版本</span><strong>v{{appVersion}}</strong></div><span class="update-state-pill" :class="{ready:['latest','completed'].includes(updateSnapshot.phase)}">{{updateStatusText}}</span></div>
      <div class="update-main-row">
        <button class="action-btn outline update-check-btn" :disabled="updateBusy" @click="checkForUpdate(appVersion)">
          <InlineIcon name="refresh" /> {{ updateSnapshot.phase === 'checking' ? '检查中' : '检查更新' }}
        </button>
      </div>
      <div v-if="(['available', 'failed', 'interrupted'].includes(updateSnapshot.phase) || updateSnapshot.phase === 'downloaded') && updateInfo" class="update-available">
        <div class="update-available-head">
          <span>可更新至 <b class="ac-name">v{{ updateInfo.version }}</b></span>
          <button class="action-btn outline update-install-btn" :disabled="updateBusy" @click="downloadAndInstall">
            {{ updateSnapshot.phase === 'downloaded' ? '安装并重启' : updateSnapshot.stage === 'install' ? '重试安装' : updateSnapshot.phase === 'available' ? '下载并安装' : '继续下载' }}
          </button>
        </div>
        <p v-if="updateInfo.notes" class="muted body upd-notes">{{ updateInfo.notes }}</p>
      </div>
      <div v-if="['downloading', 'validating', 'downloaded', 'installing'].includes(updateSnapshot.phase)" class="update-progress-panel">
        <div class="update-progress-track"><span :style="{ width: `${updateProgressPercent}%` }" /></div>
        <div class="update-progress-meta">
          <span>{{ updateSnapshot.phase === 'downloading' ? `${updateProgressPercent.toFixed(0)}%` : updateStatusText }}</span>
          <span v-if="updateSnapshot.phase === 'downloading'">{{ formatBytes(updateSnapshot.downloadedBytes) }} / {{ updateSnapshot.totalBytes ? formatBytes(updateSnapshot.totalBytes) : '未知大小' }}</span>
        </div>
        <div v-if="updateSnapshot.phase === 'downloading'" class="update-progress-detail">
          <span>速度 {{ formatSpeed(updateSnapshot.speedBps) }}</span>
          <span>剩余 {{ formatEta(updateSnapshot.etaSeconds) }}</span>
          <span v-if="(updateSnapshot.resumedBytes || 0) > 0">已复用 {{ formatBytes(updateSnapshot.resumedBytes) }} 缓存</span>
        </div>
      </div>
 <div class="update-background-option"><Toggle :model-value="updatePredownloadState.enabled" label="自动后台预下载" :disabled="updateCacheState.busy || !updatePredownloadState.available || updatePredownloadState.busy" @update:model-value="onUpdatePredownloadChange"/><span class="update-feature-state">{{updatePredownloadState.available?(updatePredownloadState.message || (updatePredownloadState.enabled?'已启用':'未启用')):'待接入'}}</span></div>
      <div class="update-cache-maintenance">
        <button class="action-btn outline update-cache-clear-btn" :disabled="updateCacheBlocked" @click="clearUpdateCache">
          {{ updateCacheState.busy ? '正在清理更新缓存…' : '清理更新缓存' }}
        </button>
        <span class="update-feature-state" role="status">{{ updateCacheState.message || '删除已下载的更新包和断点缓存；开启自动预下载时，约 10 分钟后再检查' }}<template v-if="updateCacheState.bytesFreed > 0">（已删除 {{ formatBytes(updateCacheState.bytesFreed) }}）</template></span>
      </div>
    </section>
<section class="card support-settings-card">
      <h3 class="card-title"><InlineIcon name="globe" /> 野蛮系统支持</h3>
      <button class="action-btn outline" @click="openSupport"><InlineIcon name="rocket" /> 支持和软件官网 ↗</button>
      <button class="action-btn outline" @click="openHome"><InlineIcon name="home" /> 野蛮系统主页 ↗</button>
      <button class="action-btn outline" @click="openGithub"><InlineIcon name="link" /> github免费开源地址 ↗</button>
    </section></div></template>
</SettingsSections></div></IntegratedPage>
</template>

<style scoped>
.update-cache-maintenance { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 12px; margin-top: 12px; }
.update-cache-clear-btn { flex: 0 0 auto; }
.page {
  padding-bottom: 20px;
}
.page-error {
  margin: 0 0 10px;
}
.card-header-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
.card-header-row .card-title {
  margin: 0;
}
.body {
  font-size: 12px;
  line-height: 1.6;
  margin: 0 0 10px;
}
.err-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-top: 10px;
  line-height: 1.4;
}
.tips {
  margin: 0;
  padding-left: 18px;
  font-size: 11px;
  line-height: 1.7;
}
.action-btn {
  width: 100%;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: var(--btn-py) var(--btn-px);
  min-height: var(--btn-min-h);
  font-weight: 700;
  font-size: 12px;
  cursor: pointer;
  margin-bottom: 8px;
}
.action-btn.outline {
  background: transparent;
  color: var(--accent);
  border: 1px solid var(--accent);
}
.action-btn.outline:hover {
  background: color-mix(in srgb, var(--accent) 10%, transparent);
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.fan-log-card {
  border-color: color-mix(in srgb, var(--accent) 28%, var(--border));
}
.fan-log-actions {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
  margin-top: 8px;
}
.fan-log-action {
  min-width: 0;
  min-height: var(--btn-min-h);
  padding: 0 8px;
  border: 1px solid #29384a;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text-dim);
  font: inherit;
  font-size: 11px;
  font-weight: 700;
  white-space: nowrap;
  cursor: pointer;
}
.fan-log-action.enabled {
  border-color: #2ea6ff;
  background: #1269a3;
  color: #fff;
}
.fan-log-action:hover:not(:disabled) {
  border-color: var(--accent);
  color: var(--text);
}
.fan-log-action:focus-visible {
  outline: 2px solid var(--accent);
  outline-offset: 2px;
}
.fan-log-action:disabled {
  opacity: 0.55;
  cursor: default;
}
.fan-log-status {
  margin-top: 8px;
  margin-bottom: 0;
}
.bg-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}
.background-settings-card {
  border-color: color-mix(in srgb, var(--accent) 28%, var(--border));
}
.background-section-head {
  margin-bottom: 10px;
}
.background-kind {
  color: var(--accent);
  font-size: 11px;
  font-weight: 700;
  padding: 4px 8px;
  border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
  border-radius: 999px;
}
.background-actions {
  margin-top: 2px;
}
.background-default-path {
  overflow-wrap: anywhere;
}
.bg-icon-btn {
  min-height: 34px;
  padding: 0 10px;
  border: 1px solid color-mix(in srgb, var(--accent) 55%, transparent);
  border-radius: var(--radius-ctrl);
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  color: var(--accent);
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
}
.bg-icon-btn:hover:not(:disabled) {
  background: color-mix(in srgb, var(--accent) 18%, transparent);
}
.bg-icon-btn.danger {
  border-color: color-mix(in srgb, var(--danger) 55%, transparent);
  background: color-mix(in srgb, var(--danger) 8%, transparent);
  color: #ff9ea1;
}
.bg-icon-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.bg-opacity-row {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr) 42px;
  align-items: center;
  gap: 10px;
  margin-top: 12px;
  color: var(--text);
  font-size: 12px;
  font-weight: 600;
}
.bg-opacity-row.disabled {
  opacity: 0.45;
}
.bg-opacity-row input[type='range'] {
  width: 100%;
  accent-color: var(--accent);
  cursor: pointer;
}
.bg-opacity-row input[type='range']:disabled {
  cursor: not-allowed;
}
.bg-opacity-row output {
  text-align: right;
  color: var(--accent);
  font-variant-numeric: tabular-nums;
}
.gp-toggles {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin: 4px 0 2px;
}
.mouse-backend-row {
  display: grid;
  grid-template-columns: 108px minmax(0, 1fr);
  align-items: center;
  gap: 9px;
  margin: 10px 0 6px;
}
.mouse-backend-label {
  color: var(--text);
  font-size: 12px;
  font-weight: 700;
  white-space: nowrap;
}
.mouse-backend-control {
  position: relative;
  min-width: 0;
}
.mouse-backend-buttons {
  display: grid;
  grid-template-columns: 0.82fr 1.18fr;
  gap: 6px;
  padding: 3px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.035);
}
.mouse-backend-buttons button {
  min-width: 0;
  min-height: 30px;
  padding: 5px 7px;
  border: 1px solid transparent;
  border-radius: 7px;
  background: transparent;
  color: var(--text-dim);
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
}
.mouse-backend-buttons button.active {
  border-color: color-mix(in srgb, var(--accent) 48%, transparent);
  background: color-mix(in srgb, var(--accent) 18%, transparent);
  color: var(--accent);
}
.mouse-backend-buttons button:disabled {
  opacity: 0.55;
  cursor: default;
}
.mouse-backend-popover {
  position: absolute;
  z-index: 25;
  top: calc(100% + 7px);
  right: 0;
  width: min(360px, calc(100vw - 24px));
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  gap: 10px;
  padding: 12px 14px;
  border: 1px solid color-mix(in srgb, #f5b93d 58%, transparent);
  border-radius: 10px;
  background: color-mix(in srgb, var(--bg-card) 94%, #17120a);
  color: #ffd47a;
  box-shadow: 0 8px 22px rgba(0, 0, 0, 0.42);
  font-size: 13px;
  line-height: 1.45;
}
.mouse-backend-popover > :deep(svg) {
  width: 18px;
  height: 18px;
}
.mouse-backend-popover > button {
  width: 28px;
  height: 28px;
  padding: 0;
  border: 0;
  border-radius: 7px;
  background: rgba(255, 255, 255, 0.08);
  color: inherit;
  cursor: pointer;
}

/* ── 微软鼠标组件修复确认浮层（与配置重制确认保持一致）── */
.gameinput-repair-confirm {
  z-index: 1200;
  background: #161d29;
  border: 1px solid #2a3342;
  border-radius: 12px;
  padding: 18px 20px 17px;
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.55);
  display: flex;
  flex-direction: column;
  gap: 12px;
  max-width: calc(100vw - 16px);
  min-height: 0;
  overflow-y: auto;
}
.gameinput-repair-confirm::before {
  content: '';
  position: absolute;
  width: 13px;
  height: 13px;
  background: #161d29;
  border-left: 1px solid #2a3342;
  border-top: 1px solid #2a3342;
  transform: rotate(45deg);
  top: -7px;
  right: 32px;
}
.gameinput-repair-confirm.above::before {
  top: auto;
  bottom: -7px;
  border-left: none;
  border-top: none;
  border-right: 1px solid #2a3342;
  border-bottom: 1px solid #2a3342;
}
.gameinput-repair-title {
  display: flex;
  align-items: center;
  gap: 9px;
  color: var(--text);
  font-size: 16px;
  font-weight: 700;
}
.gameinput-repair-title :deep(svg) {
  color: var(--danger);
}
.gameinput-repair-desc,
.gameinput-repair-status {
  margin: 0;
  color: var(--text-dim);
  font-size: 14px;
  line-height: 1.6;
}
.gameinput-repair-status {
  color: #ffd47a;
}
.gameinput-repair-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin-top: 3px;
}
.gameinput-repair-actions button {
  min-height: 44px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 9px;
  background: var(--bg-input);
  color: var(--text);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
}
.gameinput-repair-actions button small {
  margin-left: 4px;
  color: var(--text-dim);
  font-size: 11px;
}
.gameinput-repair-actions button.danger {
  color: var(--danger);
  border-color: color-mix(in srgb, var(--danger) 46%, transparent);
  background: color-mix(in srgb, var(--danger) 8%, var(--bg-input));
}
.gameinput-repair-actions button.danger:hover:not(:disabled) {
  background: color-mix(in srgb, var(--danger) 16%, var(--bg-input));
  border-color: var(--danger);
}
.gameinput-repair-actions button:disabled {
  cursor: default;
  opacity: 0.6;
}
.gameinput-repair-pop-enter-active,
.gameinput-repair-pop-leave-active {
  transition: opacity 0.14s ease, transform 0.14s ease;
}
.gameinput-repair-pop-enter-from,
.gameinput-repair-pop-leave-to {
  opacity: 0;
  transform: translateY(-5px);
}
.mouse-pop-enter-active,
.mouse-pop-leave-active { transition: opacity 0.12s, transform 0.12s; }
.mouse-pop-enter-from,
.mouse-pop-leave-to { opacity: 0; transform: translateY(-3px); }
.gp-toggle {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.04);
  border-radius: 10px;
  padding: 9px 11px;
  cursor: pointer;
  color: var(--text);
  font-size: 12px;
  font-weight: 600;
  transition: border-color 0.12s, background 0.12s, transform 0.08s;
}
.gp-toggle:active {
  transform: scale(0.98);
}
.gp-toggle.on {
  border-color: var(--accent);
  background: color-mix(in srgb, var(--accent) 14%, transparent);
  color: #cfe6ff;
}
.gp-toggle-state {
  font-size: 10px;
  font-weight: 700;
  padding: 2px 7px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.1);
  color: var(--text-dim);
}
.gp-toggle.on .gp-toggle-state {
  background: var(--accent);
  color: #fff;
}
.gp-hint {
  font-size: 12px;
  color: var(--text-dim);
  margin: 4px 0 2px;
  line-height: 1.5;
}
.gp-key {
  font-size: 1.3em;
  color: #f5b93d;
  font-weight: 700;
}
.gp-hint-name {
  color: var(--accent);
}
.gp-toggle.sel,
.gp-toggle.focused {
  border-color: var(--accent);
  box-shadow: 0 0 0 1px var(--accent), 0 0 8px color-mix(in srgb, var(--accent) 40%, transparent);
}
.gp-toggle.on.sel,
.gp-toggle.on.focused {
  background: rgba(46, 166, 255, 0.2);
  color: #cfe6ff;
}
.gp-toggle.sel .gp-toggle-state,
.gp-toggle.focused .gp-toggle-state {
  background: var(--accent);
  color: #06203a;
}
.ac-sub {
  margin: 8px 0 6px;
}
.ac-sub code {
  background: rgba(255, 255, 255, 0.08);
  border-radius: 4px;
  padding: 1px 5px;
  font-size: 11px;
  color: var(--accent-2);
}
.ac-collapsed {
  margin: 8px 0 2px;
  opacity: 0.7;
}
.ac-list {
  display: flex;
  flex-direction: column;
  gap: 7px;
  margin-top: 4px;
}
.ac-row {
  display: flex;
  align-items: center;
  gap: 7px;
}
.ac-input {
  flex: 1 1 auto;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: var(--radius-ctrl);
  color: var(--text);
  font-size: 12px;
  padding: 8px 10px;
  outline: none;
  transition: border-color 0.12s, box-shadow 0.12s;
}
.ac-input:focus {
  border-color: var(--accent);
  box-shadow: 0 0 0 1px var(--accent);
}
.ac-input:disabled {
  opacity: 0.55;
}
.ac-del {
  flex: 0 0 auto;
  width: 30px;
  height: 30px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(229, 72, 77, 0.1);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  cursor: pointer;
  font-size: 13px;
  line-height: 1;
  transition: background 0.12s;
}
.ac-del:hover:not(:disabled) {
  background: rgba(229, 72, 77, 0.22);
}
.ac-del:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.ac-add {
  align-self: flex-start;
  border: 1px dashed rgba(46, 166, 255, 0.5);
  background: transparent;
  color: var(--accent);
  border-radius: var(--radius-ctrl);
  padding: 7px 12px;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.12s;
}
.ac-add:hover:not(:disabled) {
  background: color-mix(in srgb, var(--accent) 10%, transparent);
}
.ac-add:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.ac-name {
  color: var(--accent-2);
}
.upd-tag {
  font-size: 12px;
  color: var(--text-dim, #9aa4b2);
  line-height: 1.45;
}
.upd-tag.ok {
  color: #4ec98b;
}
.upd-tag.err {
  color: #ff9ea1;
}
.update-card {
  border-color: color-mix(in srgb, var(--accent) 26%, var(--border));
}
.update-version-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}
.update-accel-btn,
.version-badge {
  color: var(--accent);
  border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
  border-radius: var(--radius-ctrl);
  height: 32px;
  min-height: 32px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 0 10px;
  font-size: 12px;
  line-height: 1;
}
.version-badge {
  font-size: 12px;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
}
.update-main-row,
.update-available-head,
.update-progress-meta,
.update-progress-detail {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
.update-main-row {
  margin-top: 10px;
}
.update-check-btn,
.update-install-btn,
.update-accel-btn {
  width: auto;
  margin: 0;
}
.update-accel-btn {
  color: var(--text-dim, #9aa4b2);
  border-color: color-mix(in srgb, var(--text-dim, #9aa4b2) 45%, transparent);
  margin-bottom: 0;
  font-weight: 700;
}
.update-accel-btn.active {
  color: #4ec98b;
  border-color: color-mix(in srgb, #4ec98b 55%, transparent);
}
.update-accel-btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
}
.update-available,
.update-progress-panel {
  margin-top: 10px;
  padding: 10px 12px;
  border: 1px solid color-mix(in srgb, var(--accent) 32%, var(--border));
  border-radius: var(--radius-card, 12px);
  background: color-mix(in srgb, var(--accent) 6%, transparent);
}
.upd-line {
  font-size: 13px;
  margin: 0 0 4px;
}
.upd-notes {
  white-space: pre-wrap;
  line-height: 1.5;
  margin: 0 0 10px;
  opacity: 0.85;
}
.upd-install {
  border-style: solid;
  background: color-mix(in srgb, var(--accent) 14%, transparent);
}
.update-progress-track {
  height: 8px;
  overflow: hidden;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.1);
}
.update-progress-track span {
  display: block;
  height: 100%;
  min-width: 2px;
  border-radius: inherit;
  background: var(--accent);
  transition: width 0.25s ease;
}
.update-progress-meta {
  margin-top: 8px;
  color: var(--text);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
.update-progress-detail {
  margin-top: 5px;
  color: var(--text-dim, #9aa4b2);
  font-size: 11px;
  font-variant-numeric: tabular-nums;
}
@media (max-width: 520px) {
  .update-main-row,
  .update-available-head {
    align-items: flex-start;
    flex-direction: column;
  }
  .update-check-btn,
  .update-install-btn {
    width: 100%;
  }
}

.modern-settings-page{min-width:0}.modern-settings-page :deep(.settings-section-panel){display:grid;gap:12px}.modern-settings-page :deep(.settings-section-heading){margin-bottom:0}.modern-settings-page :deep(.settings-section-panel>.card){padding:12px;background:var(--console-surface-alt);box-shadow:none}.modern-settings-page :deep(.settings-section-heading>span){display:none}
.window-placement-options{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.window-placement-options>button{min-height:40px;padding:7px;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt);color:var(--text);font:inherit;font-size:14px;font-weight:650;cursor:pointer}.window-placement-options>button.active{background:var(--accent-surface);border-color:var(--accent-border);color:var(--accent)}
.background-choice-summary{display:flex;align-items:center;gap:10px;padding:10px;border:1px solid var(--console-line);border-radius:10px;background:var(--console-faint);min-width:0}.background-thumbnail{display:grid;place-items:center;width:62px;height:49px;flex:0 0 auto;border:1px solid var(--accent-border);border-radius:8px;background:var(--accent-surface);color:var(--accent);overflow:hidden}.background-thumbnail img{width:100%;height:100%;object-fit:cover}.background-choice-summary>div:nth-child(2){display:flex;flex:1;min-width:0;flex-direction:column;gap:5px}.background-choice-summary strong{font-size:14px;font-weight:650}.background-choice-summary>div:nth-child(2)>span{font-size:12px;color:var(--text-dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.background-active-state{font-size:12px;white-space:nowrap;color:var(--text-dim)}.background-active-state.active{color:var(--accent)}.modern-settings-page .background-actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:10px 0 12px}.modern-settings-page .background-actions button{min-height:36px;font-size:14px}.background-tuning{display:grid;gap:12px;padding:12px 0;border-top:1px solid var(--console-line);border-bottom:1px solid var(--console-line)}.settings-option-list{display:grid;margin-top:8px}.settings-option-list :deep(.toggle-row){min-height:42px;padding:6px 0}.settings-option-list :deep(.toggle-row+.toggle-row){border-top:1px solid var(--console-line)}.background-feedback{font-size:12px;color:var(--text-dim);line-height:1.5;margin:6px 0 0}
.modern-settings-page .fan-log-actions{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.modern-settings-page .fan-log-action{min-width:0;min-height:40px;padding:6px 5px;font-size:13px;white-space:nowrap}.modern-settings-page .support-settings-card .action-btn{margin:0;min-height:38px;font-size:14px;text-align:left;padding-left:12px}.modern-settings-page .support-settings-card{display:grid;gap:8px}.modern-settings-page .support-settings-card .card-title{margin:0}
.update-version-summary{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px;border:1px solid var(--console-line);border-radius:10px;background:var(--console-faint);margin:10px 0}.update-version-summary>div{display:grid;gap:5px}.update-version-summary>div>span{font-size:12px;color:var(--text-dim)}.update-version-summary strong{font-size:23px;letter-spacing:.2px}.update-state-pill{font-size:12px;color:var(--text-dim);max-width:55%;text-align:right;overflow-wrap:anywhere}.update-state-pill.ready{color:var(--accent)}.modern-settings-page .update-main-row{display:grid;grid-template-columns:1fr}.modern-settings-page .update-check-btn{width:100%;margin:0;min-height:40px;font-size:14px}.modern-settings-page .update-version-actions .action-btn{margin:0;font-size:12px;min-height:32px}.update-background-option{border-top:1px solid var(--console-line);padding-top:12px;margin-top:12px}.update-background-option :deep(.toggle-row){padding:0}.update-feature-state{display:block;font-size:12px;color:var(--text-dim);margin-top:6px}
.modern-settings-page .fan-log-actions :deep(.diagnostic-log-delete-button){height:40px;min-height:40px;padding:6px 5px;font-size:13px}
.modern-settings-page .window-placement-options :deep(button),.modern-settings-page .fan-log-actions>button{height:40px;box-sizing:border-box}
</style>

<style scoped>
/* Settings preview uses the shared shell scale and removes repeated group spacing. */
.settings-density-preview :deep(.settings-section-panel){gap:12px;padding:12px;background:var(--console-surface)}
.settings-density-preview :deep(.settings-section-heading){padding:0 0 10px;margin:0;align-items:center}
.settings-density-preview :deep(.settings-section-heading h3){padding:0!important;margin:0!important;border:0!important;font-size:16px;line-height:1.35}
.settings-density-preview :deep(.settings-section-panel>.card){padding:0!important;margin:0!important;border:0!important;border-radius:0!important;background:transparent!important;box-shadow:none!important;backdrop-filter:none!important}
.settings-density-preview :deep(.settings-section-panel>.card+.card){border-top:1px solid var(--console-line)!important;padding-top:12px!important}
.settings-density-preview :deep(.card-title){margin:0 0 8px!important;padding:0!important;border:0!important;font-size:16px;line-height:1.35}
.settings-density-preview :deep(.card-header-row>.card-title){margin:0!important}
.settings-density-preview .window-position-settings{padding:12px!important}
.settings-density-preview .window-placement-options>button{height:36px;min-height:36px;font-size:14px;padding:5px 8px;box-sizing:border-box}
.settings-density-preview .background-choice-summary{padding:9px;gap:10px;border-radius:8px}
.settings-density-preview .background-thumbnail{width:52px;height:40px;border-radius:7px}
.settings-density-preview .background-actions{margin:9px 0 10px;gap:8px}
.settings-density-preview .background-actions>button{height:36px;min-height:36px;padding:6px 10px;font-size:14px}
.settings-density-preview .background-tuning{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;padding:10px 0}
.settings-density-preview .background-tuning :deep(.slider){min-width:0;padding:4px 0}
.settings-density-preview .background-tuning :deep(.slider-head){gap:6px;min-width:0}
.settings-density-preview .background-tuning :deep(.slider-label){font-size:14px;line-height:1.4}
.settings-density-preview .background-tuning :deep(.slider-val){font-size:14px;flex:0 0 auto}
.settings-density-preview .settings-option-list{margin-top:4px}
.settings-density-preview .settings-option-list :deep(.toggle-row){min-height:40px;padding:4px 0!important;gap:10px;border-radius:0}
.settings-density-preview .settings-option-list :deep(.toggle-row+.toggle-row){border-top:1px solid var(--console-line)}
.settings-density-preview .fan-log-actions{gap:8px;margin-top:0}
.settings-density-preview .fan-log-actions>button,.settings-density-preview .fan-log-actions :deep(.diagnostic-log-delete-button){height:36px;min-height:36px;font-size:13px;padding:5px 6px}
.settings-density-preview .support-settings-card{gap:8px}
.settings-density-preview .support-settings-card>.action-btn{height:36px;min-height:36px;margin:0;font-size:14px;padding:6px 10px}
.settings-density-preview .update-version-summary{padding:10px;gap:10px;margin:10px 0}
.settings-density-preview .update-main-row .update-check-btn{height:36px;min-height:36px;font-size:14px;padding:6px 10px}
.settings-density-preview .update-background-option{margin-top:10px;padding-top:10px}
.settings-density-preview .update-background-option :deep(.toggle-row){min-height:40px;padding:4px 0!important}
.settings-density-preview .update-feature-state{margin-top:3px}
</style>

<style scoped>
.settings-density-preview .window-placement-options{grid-template-columns:repeat(2,minmax(0,1fr))}
</style>

<style>

.modern-settings-page.settings-density-preview .settings-layout { --settings-gap:12px; }

.modern-settings-page.settings-density-preview .settings-layout .settings-section-tabs { margin-bottom:10px; gap:6px; }

.modern-settings-page.settings-density-preview .settings-layout .settings-section-tabs button { height:38px; min-height:38px; padding:5px 6px; font-size:14px; }

.modern-settings-page.settings-density-preview .settings-layout .settings-section-panel { padding:12px; gap:0; }

.modern-settings-page.settings-density-preview .settings-layout .settings-section-heading { display:none; }

.modern-settings-page.settings-density-preview .settings-layout .card { padding:0!important; margin:0!important; border:0!important; border-radius:0!important; background:transparent!important; box-shadow:none!important; backdrop-filter:none!important; min-width:0; }

.modern-settings-page.settings-density-preview .settings-layout .card-title { font-size:16px; line-height:1.4; }

.modern-settings-page.settings-density-preview .settings-layout .appearance-flow { display:grid; gap:12px; }

.modern-settings-page.settings-density-preview .settings-layout .appearance-flow>div { min-width:0; }

.modern-settings-page.settings-density-preview .settings-layout .setting-wallpaper { padding-top:12px; border-top:1px solid var(--console-line); }

.modern-settings-page.settings-density-preview .settings-layout .window-placement-options { gap:6px; margin:0; }

.modern-settings-page.settings-density-preview .settings-layout .window-placement-options>button { padding:5px 6px; }

.modern-settings-page.settings-density-preview .settings-layout .seg-btn { font-size:14px; min-height:36px; padding:5px 6px; }

.modern-settings-page.settings-density-preview .settings-layout .background-choice-summary { padding:8px; gap:9px; }

.modern-settings-page.settings-density-preview .settings-layout .background-thumbnail { width:44px; height:36px; }

.modern-settings-page.settings-density-preview .settings-layout .background-choice-summary strong { font-size:14px; line-height:1.4; }

.modern-settings-page.settings-density-preview .settings-layout .background-choice-summary>div:nth-child(2) { gap:3px; }

.modern-settings-page.settings-density-preview .settings-layout .background-actions { margin:8px 0; gap:8px; }

.modern-settings-page.settings-density-preview .settings-layout .background-tuning { padding:8px 0; gap:12px; border-top:0; }

.modern-settings-page.settings-density-preview .settings-layout .slider-label,
.modern-settings-page.settings-density-preview .settings-layout .slider-val,
.modern-settings-page.settings-density-preview .settings-layout .toggle-label { font-size:14px; }

.modern-settings-page.settings-density-preview .settings-layout .background-feedback { font-size:13px; line-height:1.5; margin:6px 0 0; overflow-wrap:anywhere; }

.modern-settings-page.settings-density-preview .settings-layout .settings-option-list { margin:0; }

.modern-settings-page.settings-density-preview .settings-layout .settings-option-list .toggle-row { min-height:40px; padding:4px 0!important; }

.modern-settings-page.settings-density-preview .settings-layout .device-flow { display:grid; gap:12px; }

.modern-settings-page.settings-density-preview .settings-layout .device-flow>.card { padding-top:12px!important; border-top:1px solid var(--console-line)!important; }

.modern-settings-page.settings-density-preview .settings-layout .device-flow .card-header-row { align-items:center; }

.modern-settings-page.settings-density-preview .settings-layout .device-flow .card-header-row .card-title { flex:1; line-height:1.45; }

.modern-settings-page.settings-density-preview .settings-layout .device-flow .card-header-row .toggle-row { min-height:36px; padding:0!important; }



.modern-settings-page.settings-density-preview .settings-layout .ac-sub { font-size:13px; line-height:1.6; margin:8px 0; }

.modern-settings-page.settings-density-preview .settings-layout .ac-list { gap:6px; }

.modern-settings-page.settings-density-preview .settings-layout .ac-input { min-height:34px; font-size:14px; }

.modern-settings-page.settings-density-preview .settings-layout .updates-flow { display:grid; gap:12px; }

.modern-settings-page.settings-density-preview .settings-layout .updates-flow>.card+.card { border-top:1px solid var(--console-line)!important; padding-top:12px!important; }

.modern-settings-page.settings-density-preview .settings-layout .update-version-summary { padding:9px; margin:9px 0; }

.modern-settings-page.settings-density-preview .settings-layout .update-background-option { margin-top:8px; padding-top:8px; }

.modern-settings-page.settings-density-preview .settings-layout .update-main-row { margin:0; }

.modern-settings-page.settings-density-preview .settings-layout .update-feature-state { font-size:13px; }

.modern-settings-page.settings-density-preview .settings-layout .support-settings-card { display:grid; gap:8px; }

.modern-settings-page.settings-density-preview .settings-layout .support-settings-card>.card-title { margin-bottom:0!important; }

.modern-settings-page.settings-density-preview .settings-layout .update-accel-btn { min-height:32px; font-size:13px; }


.modern-settings-page.settings-density-preview .settings-layout-b .settings-section-panel { padding:0; background:transparent; border:0; }

.modern-settings-page.settings-density-preview .settings-layout-b .appearance-flow { grid-template-columns:repeat(2,minmax(0,1fr)); gap:10px; }

.modern-settings-page.settings-density-preview .settings-layout-b .appearance-flow>div,
.modern-settings-page.settings-density-preview .settings-layout-b .device-flow>.card,
.modern-settings-page.settings-density-preview .settings-layout-b .diagnostics-flow,
.modern-settings-page.settings-density-preview .settings-layout-b .updates-flow>.card { padding:12px!important; border:1px solid var(--console-line)!important; border-radius:11px!important; background:var(--console-surface)!important; }

.modern-settings-page.settings-density-preview .settings-layout-b .diagnostics-flow { display:grid; gap:12px; }

.modern-settings-page.settings-density-preview .settings-layout-b .setting-wallpaper { grid-column:1/-1; }

.modern-settings-page.settings-density-preview .settings-layout-b .setting-position>.card,
.modern-settings-page.settings-density-preview .settings-layout-b .setting-color>.card { height:100%; display:flex; flex-direction:column; justify-content:space-between; }

.modern-settings-page.settings-density-preview .settings-layout-b .setting-position .window-placement-options,
.modern-settings-page.settings-density-preview .settings-layout-b .setting-color .seg-group { width:100%; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-settings-card { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); column-gap:12px; align-items:start; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-settings-card>.card-title { grid-column:1/-1; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-choice-summary { grid-column:1; grid-row:2; min-height:84px; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-thumbnail { width:36px; height:36px; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-active-state { display:none; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-actions { grid-column:1; grid-row:3; margin:8px 0; grid-template-columns:1fr; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-tuning { grid-column:2; grid-row:2/4; grid-template-columns:1fr; padding:0; gap:14px; align-content:start; }

.modern-settings-page.settings-density-preview .settings-layout-b .settings-option-list { grid-column:1/-1; display:grid; grid-template-columns:1fr 1fr; gap:12px; border-top:1px solid var(--console-line); padding-top:6px; }

.modern-settings-page.settings-density-preview .settings-layout-b .settings-option-list .toggle-row { gap:5px; border:0; }

.modern-settings-page.settings-density-preview .settings-layout-b .settings-option-list .toggle-label { flex:1; line-height:1.45; }

.modern-settings-page.settings-density-preview .settings-layout-b .background-feedback { grid-column:1/-1; }

.modern-settings-page.settings-density-preview .settings-layout-b .updates-flow .update-card { display:grid; grid-template-columns:1fr auto; gap:8px 12px; }

.modern-settings-page.settings-density-preview .settings-layout-b .update-card>.card-header-row,
.modern-settings-page.settings-density-preview .settings-layout-b .update-background-option,
.modern-settings-page.settings-density-preview .settings-layout-b .update-available,
.modern-settings-page.settings-density-preview .settings-layout-b .update-progress-panel { grid-column:1/-1; }

.modern-settings-page.settings-density-preview .settings-layout-b .update-version-summary { grid-column:1; margin:0; }

.modern-settings-page.settings-density-preview .settings-layout-b .update-main-row { grid-column:2; display:flex; align-items:stretch; }

.modern-settings-page.settings-density-preview .settings-layout-b .update-check-btn { height:100%; min-height:36px; }

.modern-settings-page.settings-density-preview .settings-layout-b .support-settings-card { grid-template-columns:1fr 1fr; }

.modern-settings-page.settings-density-preview .settings-layout-b .support-settings-card>.card-title,
.modern-settings-page.settings-density-preview .settings-layout-b .support-settings-card>.action-btn:last-child { grid-column:1/-1; }



.modern-settings-page.settings-density-preview .settings-layout .background-settings-card { display:block; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-editor-layout { display:grid; grid-template-columns:minmax(0,1.04fr) minmax(0,1fr); gap:12px; align-items:start; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-preview-column,
.modern-settings-page.settings-density-preview .settings-layout .wallpaper-controls-column { min-width:0; }

.modern-settings-page.settings-density-preview .settings-layout .mini-preview-caption { display:block; padding-top:7px; font-size:13px; text-align:center; color:var(--text-dim); }

.modern-settings-page.settings-density-preview .settings-layout .background-choice-summary { display:block; padding:0; margin:0 0 8px; min-height:0; border:0; border-radius:0; background:transparent; }

.modern-settings-page.settings-density-preview .settings-layout .background-source-line { display:flex; gap:7px; align-items:center; min-width:0; }

.modern-settings-page.settings-density-preview .settings-layout .background-source-line>div { display:flex; flex:1; min-width:0; flex-direction:column; gap:3px; }

.modern-settings-page.settings-density-preview .settings-layout .background-source-line strong { font-size:14px; line-height:1.4; }

.modern-settings-page.settings-density-preview .settings-layout .background-source-line span { font-size:12px; color:var(--text-dim); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }

.modern-settings-page.settings-density-preview .settings-layout .background-actions { display:grid; grid-template-columns:1fr; gap:7px; margin:8px 0; }

.modern-settings-page.settings-density-preview .settings-layout .background-tuning { grid-template-columns:1fr; gap:7px; padding:8px 0; }

.modern-settings-page.settings-density-preview .settings-layout .settings-option-list { display:grid; grid-template-columns:1fr; gap:0; margin:4px 0 0; padding:0; border:0; }

.modern-settings-page.settings-density-preview .settings-layout .settings-option-list .toggle-row { gap:6px; border-top:0; }

.modern-settings-page.settings-density-preview .settings-layout .settings-option-list .toggle-row+.toggle-row { border-top:1px solid var(--console-line); }

.modern-settings-page.settings-density-preview .settings-layout .settings-option-list .toggle-text { min-width:0; flex:1; }

.modern-settings-page.settings-density-preview .settings-layout .background-feedback { margin:6px 0 0; }

.wallpaper-mini-shell .mini-wallpaper-media { position:absolute; inset:0; width:100%; height:100%; object-fit:cover; z-index:0; pointer-events:none; }

.wallpaper-mini-shell .app-content { scrollbar-width:none; }

.wallpaper-mini-shell .app-content::-webkit-scrollbar { display:none; }



.modern-settings-page.settings-density-preview .settings-layout .wallpaper-editor-layout { grid-template-columns:repeat(2,minmax(0,1fr)); align-items:stretch; row-gap:12px; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-preview-column { display:flex; align-items:stretch; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-controls-column { display:flex; flex-direction:column; justify-content:space-between; gap:10px; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-controls-column .background-choice-summary { margin:0; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-controls-column .background-actions { margin:0; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-controls-column .background-tuning { flex:1; display:flex; flex-direction:column; justify-content:space-between; gap:10px; padding:0; border:0; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-controls-column .background-tuning .slider { padding:0; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-footer { grid-column:1/-1; display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:12px; margin:0; border-top:1px solid var(--console-line); padding-top:10px; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-footer .toggle-row { min-height:40px; margin:0; border:0!important; padding:0!important; }

.modern-settings-page.settings-density-preview .settings-layout .wallpaper-editor-layout>.background-feedback { grid-column:1/-1; margin:0; }



.modern-settings-page.settings-density-preview .settings-layout-b .setting-position>.card,
.modern-settings-page.settings-density-preview .settings-layout-b .setting-color>.card { display:grid; grid-template-columns:minmax(0,1fr); grid-template-rows:auto 36px; justify-content:stretch; align-content:space-between; gap:8px; }

.modern-settings-page.settings-density-preview .settings-layout-b .setting-position .card-title,
.modern-settings-page.settings-density-preview .settings-layout-b .setting-color .card-title { margin:0!important; }

.modern-settings-page.settings-density-preview .settings-layout .interface-color-settings .seg { min-height:36px; height:36px; flex-shrink:0; }

.modern-settings-page.settings-density-preview .settings-layout .interface-color-settings .seg-btn { height:36px!important; min-height:36px!important; max-height:36px; box-sizing:border-box; flex-shrink:0; }
</style>
