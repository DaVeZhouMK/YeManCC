<script setup lang="ts">
import { ref, onMounted, onActivated, onBeforeUnmount, computed } from 'vue';
import Toggle from '@/components/Toggle.vue';
import SegButton from '@/components/SegButton.vue';
import { autocloseGet, autocloseSet, updateAccelGet, updateAccelToggle, sleepFactsGet, sleepFactsSetEnabled, sleepFactsClearLog, sleepFactsExportLog, type AutoCloseConfig, type UpdateAccelState, type SleepFactStatus } from '@/bridge/yeman';
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
  notifyBackgroundChanged,
  previewBackgroundOpacity,
  setBackgroundOpacity,
  getBackgroundBlur,
  previewBackgroundBlur,
  setBackgroundBlur,
} from '@/bridge/background';
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
} from '@/bridge/updateManager';

const errMsg = ref('');

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
const bgBlurMin = BACKGROUND_BLUR_MIN;
const bgBlurMax = BACKGROUND_BLUR_MAX;
const dynamicEnabled = ref(getDynamicBackgroundConfig().enabled);
const backgroundControlEnabled = computed(() => bgEnabled.value || dynamicEnabled.value);
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

function onOpacityInput(e: Event): void {
  previewOpacity((e.target as HTMLInputElement).value);
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

function onBlurInput(e: Event): void {
  previewBlur((e.target as HTMLInputElement).value);
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
    bgKind.value = 'image';
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
function onWindowPlacementChange(value: string | number): void {
  const next = String(value) as 'left' | 'right' | 'center';
  windowPlacement.value = next;
  void setUiSettings({ windowPlacement: next });
}

// ── 识别软件关闭野蛮系统前端：总开关 + 可编辑进程名列表 ──
const autoClose = ref<AutoCloseConfig>({ enabled: false, procs: [] });
const acBusy = ref(false);

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
    const r = await invoke<{ ok?: boolean; path?: string; count?: number; reason?: string }>(
      'logs.exportAll',
      {},
      { timeoutMs: 150000 },
    );
    if (r.ok !== true || !r.path) throw new Error(r.reason || '导出失败');
    detailLogStatus.value = `已导出 ${r.count ?? 0} 个日志文件到桌面：${r.path}`;
  } catch (e) {
    detailLogStatus.value = '导出失败：' + (e instanceof Error ? e.message : String(e));
  } finally {
    detailLogBusy.value = false;
  }
}

// 版本号：构建期由 version.json 注入（scripts/write-version.mjs → src/version.ts）
const appVersion = APP_VERSION;

const updateBusy = computed(() => ['checking', 'downloading', 'validating', 'installing'].includes(updateSnapshot.phase));
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

onMounted(async () => {
  const fanSettings = await initializeFanFeature();
  await initializeInputDiagnosticsUi();
  await refreshNativeInputLogFiles();
  await refreshDomainLogStates();
  fanDiagnosticLoggingEnabled.value = fanSettings.diagnosticLoggingEnabled === true;
  await ensureUpdateManager();
  await loadUiSettings();
  videoBatteryPause.value = getUiSetting('videoBatteryPause');
  windowPlacement.value = getUiSetting('windowPlacement');
  bgOpacity.value = Math.round(getBackgroundOpacity() * 100);
  bgBlur.value = getBackgroundBlur();
  dynamicEnabled.value = getDynamicBackgroundConfig().enabled;
  load();
  void refreshSleepFacts();
  void loadBackgroundState();
  window.addEventListener('dynamic-background:progress', onDynamicProgress);
});
// KeepAlive 缓存下切回本页时主动刷新，避免「手柄设置/背景状态在其它页面变更后不刷新」
// （2026-08-05 补充 onActivated）。
onActivated(() => {
  void ensureUpdateManager();
  load();
  void refreshSleepFacts();
  void refreshNativeInputLogFiles();
  void loadBackgroundState();
});
onBeforeUnmount(() => {
  window.removeEventListener('dynamic-background:progress', onDynamicProgress);
});
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar page-error">{{ errMsg }}</div>
    <!-- ── 以下内容原属「支持」页面，已合并至设置下方 ── -->
    <section class="card">
      <h3 class="card-title"><InlineIcon name="palette" /> 界面颜色</h3>
      <SegButton
        :model-value="theme"
        :options="[
          { value: 'blue-black', label: '蓝黑' },
          { value: 'red-black', label: '红黑' },
          { value: 'cyberpunk', label: '赛博朋克' },
        ]"
        color="accent"
        full
        @update:model-value="onThemeChange"
      />
      <p class="muted body">当前颜色：{{ theme === 'blue-black' ? '蓝黑' : theme === 'red-black' ? '红黑' : '赛博朋克' }}</p>
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="monitor" /> 窗口呼出位置</h3>
      <SegButton
        :model-value="windowPlacement"
        :options="[
          { value: 'left', label: '左侧' },
          { value: 'right', label: '右侧' },
          { value: 'center', label: '居中' },
        ]"
        color="accent"
        full
        @update:model-value="onWindowPlacementChange"
      />
      <p class="muted body">主窗口启动与托盘呼出时对齐到的屏幕位置，默认右侧。</p>
    </section>

    <section class="card background-settings-card">
      <div class="card-header-row background-section-head">
        <h3 class="card-title"><InlineIcon name="monitor" /> 动态壁纸设置</h3>
      </div>
      <div class="bg-actions background-actions">
        <button class="bg-icon-btn" :disabled="bgBusy" title="选择背景文件" @click="chooseBackground">
          <InlineIcon name="edit" size="18px" />
          <span>{{ bgBusy ? '处理中…' : bgEnabled ? '更换背景' : '选择背景文件' }}</span>
        </button>
        <button class="bg-icon-btn danger" :disabled="bgBusy || !bgEnabled" title="清除背景文件" @click="clearBackground">
          <InlineIcon name="trash" size="18px" />
          <span>清除背景</span>
        </button>
      </div>
      <div class="bg-opacity-row" :class="{ disabled: !backgroundControlEnabled }">
        <label for="bg-opacity">图片可见度</label>
        <input
          id="bg-opacity"
          type="range"
          :min="bgOpacityMin"
          :max="bgOpacityMax"
          step="1"
          :value="bgOpacity"
          :disabled="!backgroundControlEnabled"
          @input="onOpacityInput"
          @change="saveOpacity"
        />
        <output for="bg-opacity">{{ bgOpacity }}%</output>
      </div>
      <div class="bg-opacity-row" :class="{ disabled: !backgroundControlEnabled }">
        <label for="bg-blur">背景图片模糊</label>
        <input
          id="bg-blur"
          type="range"
          :min="bgBlurMin"
          :max="bgBlurMax"
          step="1"
          :value="bgBlur"
          :disabled="!backgroundControlEnabled"
          @input="onBlurInput"
          @change="saveBlur"
        />
        <output for="bg-blur">{{ bgBlur }}px</output>
      </div>
      <Toggle
        :model-value="videoBatteryPause"
        label="离电禁止播放视频"
        description="始终可设置；离电时不启动视频，插电且窗口可见时恢复"
        color="accent"
        @update:model-value="onVideoBatteryPauseChange"
      />
      <Toggle
        :model-value="dynamicEnabled"
        label="当前游戏动态背景"
        color="accent"
        @update:model-value="onDynamicToggle"
      />
      <p class="muted body dynamic-status">当前游戏动态背景：{{ dynamicStatus }}</p>
    </section>

    <section class="card">
      <div class="card-header-row">
        <h3 class="card-title"><InlineIcon name="lock" /> 识别软件关闭野蛮系统前端</h3>
        <Toggle
          v-model="autoClose.enabled"
          color="accent"
          :disabled="acBusy"
          @update:model-value="onAutoCloseToggle"
        />
      </div>
      <template v-if="autoClose.enabled">
        <p class="muted body ac-sub">
          每 5 秒检测列表中的软件进程（如 OneXConsole / AYASpace 等厂商前端），发现即温和关闭（发关闭信号，非强杀），避免抢占野蛮系统前端
        </p>
        <div class="ac-list">
          <div v-for="(p, i) in autoClose.procs" :key="i" class="ac-row">
            <input
              class="ac-input"
              type="text"
              :placeholder="i === 0 ? 'OneXConsole' : '进程名，如 AYASpace'"
              :value="p"
              :disabled="acBusy"
              @input="onAcProcInput(i, ($event.target as HTMLInputElement).value)"
              @change="saveAutoClose()"
            />
            <button class="ac-del" :disabled="acBusy" title="移除" @click="onAcProcRemove(i)"><InlineIcon name="close" /></button>
          </div>
          <button class="ac-add" :disabled="acBusy" @click="onAcProcAdd">＋ 添加进程名</button>
        </div>
      </template>
    </section>

    <section class="card fan-log-card" aria-label="诊断日志">
      <h3 class="card-title">诊断日志</h3>
      <div class="fan-log-actions">
        <button class="fan-log-action" :class="{ enabled: detailLoggingEnabled }" type="button" :disabled="detailLogBusy" :aria-pressed="detailLoggingEnabled" @click="toggleDetailLogging">{{ detailLoggingEnabled ? '详细日志已开启' : '详细日志已关闭' }}</button>
        <button class="fan-log-action" type="button" :disabled="detailLogBusy" @click="exportAllLogs">导出全部日志到桌面</button>
      </div>
      <p v-if="detailLogStatus" class="muted body fan-log-status" role="status">{{ detailLogStatus }}</p>
    </section>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="globe" /> 野蛮系统支持</h3>
      <p class="muted body">此控制台处于早期测试阶段</p>
      <button class="action-btn outline" @click="openSupport"><InlineIcon name="rocket" /> 支持和软件官网 ↗</button>
      <button class="action-btn outline" @click="openHome"><InlineIcon name="home" /> 野蛮系统主页 ↗</button>
      <button class="action-btn outline" @click="openGithub"><InlineIcon name="link" /> github免费开源地址 ↗</button>
    </section>

    <section class="card update-card">
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
          <span class="version-badge">v{{ appVersion }}</span>
        </div>
      </div>
      <p class="muted body">更新任务由程序后台持续执行，切换页面不会中断。</p>
      <div class="update-main-row">
        <button class="action-btn outline update-check-btn" :disabled="updateBusy" @click="checkForUpdate(appVersion)">
          <InlineIcon name="refresh" /> {{ updateSnapshot.phase === 'checking' ? '检查中' : '检查更新' }}
        </button>
        <span class="upd-tag" :class="{ ok: ['latest', 'completed'].includes(updateSnapshot.phase), err: updateSnapshot.phase === 'failed' }">{{ updateStatusText }}</span>
      </div>
      <div v-if="['available', 'failed', 'interrupted'].includes(updateSnapshot.phase) && updateInfo" class="update-available">
        <div class="update-available-head">
          <span>可更新至 <b class="ac-name">v{{ updateInfo.version }}</b></span>
          <button class="action-btn outline update-install-btn" :disabled="updateBusy" @click="downloadAndInstall">
            {{ updateSnapshot.stage === 'install' ? '重试安装' : updateSnapshot.phase === 'available' ? '下载并安装' : '继续下载' }}
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
    </section>

  </div>
</template>

<style scoped>
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
</style>
