<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, onActivated, onDeactivated } from 'vue';
import {
  oneClickFrameGen,
  optiscalerStatus,
  oneClickOptiScaler,
  dirnameOf,
  LS_PRIMARY,
} from '@/bridge/quickapp';
import {
  subscribeGameStatus,
  detectedGameName,
  type DetectedGame,
} from '@/bridge/gamedetect';
import {
  SPEED_PRESETS,
  applyGameSpeed,
  clearGameSpeed,
  isMinecraftTarget,
} from '@/bridge/speedhack';
import {
  closeGame,
} from '@/bridge/gameproc';
import { fs, shell, dialog, display, registry, type DisplayMode, type DisplayTopology } from '@/bridge/api';
import { readSettingsSection, saveSettingsSection } from '@/bridge/settingsRepository';
import {
  folder,
  baseUrl,
  tracks,
  index,
  playing,
  mode,
  error as musicError,
  currentName,
  hasFolder,
  chooseFolder,
  scanFolder,
  togglePlay,
  playNext,
  playPrev,
  setMode,
  volume,
  muted,
  setVolume,
  persistVolume,
  toggleMute,
} from '@/bridge/music';
import InlineIcon from '@/components/InlineIcon.vue';
import Dropdown from '@/components/Dropdown.vue';

const busy = ref(false);
const errMsg = ref('');
const statusMsg = ref('');

const displayModes = ref<DisplayMode[]>([]);
const displayCurrent = ref('');
const displayBusy = ref(false);
const scaleBusy = ref(false);
const scalePct = ref(100);
const scaleOptions = [100, 125, 150, 175, 200, 225, 250, 300].map((value) => ({
  value,
  label: `${value}%`,
}));
const displayOptions = computed(() => displayModes.value.map((m) => ({
  value: m.id,
  label: `${m.width} × ${m.height} · ${m.refresh}Hz${m.orientation === 1 || m.orientation === 3 ? ' · 竖屏' : ''}`,
})));
const displayTopology = ref<DisplayTopology | null>(null);
const displayTopologyOptions: Array<{ value: DisplayTopology; label: string; detail: string }> = [
  { value: 'internal', label: '仅显示 1', detail: '关闭外接显示器' },
  { value: 'external', label: '仅显示 2', detail: '只使用外接显示器' },
  { value: 'clone', label: '复制', detail: '两个屏幕显示相同内容' },
  { value: 'extend', label: '扩展', detail: '扩展桌面到两个屏幕' },
];

async function refreshScale() {
  try {
    const v = await registry.read('HKCU', 'Control Panel\\Desktop', 'LogPixels');
    const raw = Number(v) || 96;
    scalePct.value = scaleOptions.reduce((best, option) =>
      Math.abs(option.value * 96 / 100 - raw) < Math.abs(best * 96 / 100 - raw) ? option.value : best,
    100);
  } catch {
    scalePct.value = 100;
  }
}

async function applyScale(value: string | number) {
  const pct = Number(value);
  if (!scaleOptions.some((option) => option.value === pct)) return;
  scaleBusy.value = true;
  errMsg.value = '';
  try {
    const ok = await registry.write('HKCU', 'Control Panel\\Desktop', 'LogPixels', Math.round(pct * 96 / 100));
    if (!ok) throw new Error('注册表写入失败');
    await registry.write('HKCU', 'Control Panel\\Desktop', 'Win8DpiScaling', 1);
    scalePct.value = pct;
    statusMsg.value = `Windows缩放已设置为 ${pct}%，注销或重启后完全生效`;
  } catch (e) {
    errMsg.value = 'Windows缩放设置失败：' + (e as Error).message;
    await refreshScale();
  } finally {
    scaleBusy.value = false;
  }
}

async function refreshDisplayModes() {
  displayBusy.value = true;
  try {
    const r = await display.getModes();
    displayModes.value = Array.isArray(r.modes) ? r.modes : [];
    displayCurrent.value = r.current || displayModes.value[0]?.id || '';
  } catch (e) {
    errMsg.value = '读取当前显示器分辨率失败：' + (e as Error).message;
  } finally {
    displayBusy.value = false;
  }
}

async function applyDisplayMode(id: string | number) {
  const selected = displayModes.value.find((m) => m.id === String(id));
  if (!selected || selected.id === displayCurrent.value) return;
  displayBusy.value = true;
  errMsg.value = '';
  try {
    const applied = await display.setMode(selected);
    displayCurrent.value = applied.id;
    statusMsg.value = `已切换当前显示器：${applied.width} × ${applied.height} · ${applied.refresh}Hz`;
  } catch (e) {
    errMsg.value = '分辨率切换失败：' + (e as Error).message;
    await refreshDisplayModes();
  } finally {
    displayBusy.value = false;
  }
}

async function applyDisplayTopology(topology: DisplayTopology) {
  displayBusy.value = true;
  errMsg.value = '';
  try {
    await display.setTopology(topology);
    displayTopology.value = topology;
    const label = displayTopologyOptions.find((o) => o.value === topology)?.label || topology;
    statusMsg.value = `已切换显示器模式：${label}`;
  } catch (e) {
    errMsg.value = '显示器切换失败：' + (e as Error).message;
  } finally {
    displayBusy.value = false;
  }
}

const game = ref<DetectedGame | null>(null);

// ── 全局游戏状态订阅：页面激活期间跟随游戏退出/切换自动更新 ──
let unsubGame: (() => void) | null = null;

// 全局状态变化 → 同步本页（游戏退出时自动归零，不再需要手动刷新）
function applyGameStatus(g: DetectedGame | null) {
  const previous = game.value;
  const targetChanged = !!previous && (!g || previous.pid !== g.pid);
  if (targetChanged && activeSpeed.value !== null && previous) {
    // 游戏切换时尽力解除旧目标；变速链路只按旧 PID 操作。
    void clearGameSpeed(previous.pid).catch(() => {});
  }
  if (!g || targetChanged) activeSpeed.value = null;
  game.value = g;
  if (!g) {
    // 游戏已退出：变速目标一并归零
    activeSpeed.value = null;
    return;
  }
}

// ── 游戏加速状态 ──
const speedBusy = ref(false);
const activeSpeed = ref<number | null>(null); // 当前生效的倍率，null=未加速

async function onSpeedApply(factor: number) {
  if (factor === 1) { await onSpeedOff(); return; }
  errMsg.value = '';
  statusMsg.value = '';
  if (!game.value) {
    errMsg.value = '请先刷新识别当前游戏，再加速。';
    return;
  }
  speedBusy.value = true;
  try {
    const target = game.value;
    if (isMinecraftTarget(target)) {
      activeSpeed.value = null;
      statusMsg.value = `${detectedGameName(target) || target.name} 暂不支持安全变速，已保持 1×`;
      return;
    }
    const r = await applyGameSpeed(target.pid, factor, target);
    if (r.skipped) {
      activeSpeed.value = null;
      statusMsg.value = r.reason === 'bridge_conflict'
        ? '检测到独立 OpenSpeedy 正在运行，为避免管道冲突已保持 1×'
        : `${detectedGameName(target) || target.name} 暂不支持安全变速，已保持 1×`;
    } else if (r.ok) {
      activeSpeed.value = factor;
      statusMsg.value = `已对 ${detectedGameName(game.value) || game.value.name} 应用 ${factor}× 变速`;
    } else {
      activeSpeed.value = null;
      if (r.safeFallback) {
        statusMsg.value = '变速执行失败，已自动回退并保持 1×';
      } else {
        errMsg.value = '变速失败：' + (r.msgs.join('; ') || '未知错误');
      }
    }
  } catch (e) {
    activeSpeed.value = null;
    statusMsg.value = '变速执行异常，未继续改变速度；当前按 1× 处理';
  } finally {
    speedBusy.value = false;
  }
}

async function onSpeedOff() {
  errMsg.value = '';
  statusMsg.value = '';
  if (!game.value) {
    errMsg.value = '没有可关闭的加速目标。';
    return;
  }
  if (isMinecraftTarget(game.value)) {
    activeSpeed.value = null;
    statusMsg.value = 'Minecraft 未执行变速，保持 1×';
    return;
  }
  speedBusy.value = true;
  try {
    const r = await clearGameSpeed(game.value.pid);
    if (r.ok) {
      activeSpeed.value = null;
      statusMsg.value = '已关闭变速，恢复原始速度。';
    } else if (r.safeFallback) {
      activeSpeed.value = null;
      statusMsg.value = '关闭变速时出现异常，但已强制回退到 1×';
    } else {
      errMsg.value = '关闭变速失败：' + (r.msgs.join('; ') || '未知错误');
    }
  } catch (e) {
    errMsg.value = '关闭变速失败：' + (e as Error).message;
  } finally {
    speedBusy.value = false;
  }
}


async function onLaunchLs() {
  errMsg.value = '';
  statusMsg.value = '';
  if (!game.value) {
    errMsg.value = '请先刷新识别当前游戏，再一键插帧。';
    return;
  }
  busy.value = true;
  try {
    const ls = await oneClickFrameGen(game.value.path);
    const src = ls.source === 'primary' ? '主程序' : 'Steam 版';
    const action = ls.alreadyHadProfile ? '直接' : '写入配置并';
    statusMsg.value =
      action + '最小化启动 Lossless Scaling（' + src + '）';
  } catch (e) {
    errMsg.value = '插帧失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// ── 公共 OptiScaler 安装/卸载逻辑 ──
// gamePath: 游戏 exe 全路径；gameName: 显示名；runningPid: 若该游戏正在运行则需先关闭
async function applyOptiScalerTo(
  gamePath: string,
  gameName: string,
  runningPid: number | null
) {
  // 1) 刷新该目录的 OptiScaler 安装状态
  let localStatus = false;
  try {
    localStatus = await optiscalerStatus(gamePath);
  } catch {
    localStatus = false;
  }

  // 2) 游戏正在运行 -> 文件被占用无法热更改，必须先确认关闭
  if (runningPid) {
    const ok = await dialog.confirm(
      '游戏正在运行',
      '「' + gameName + '」正在运行，其文件被占用无法热更改。\n是否先关闭游戏再应用？'
    );
    if (!ok) {
      errMsg.value = '已取消：游戏运行时无法修改文件，请先关闭游戏。';
      return;
    }
    busy.value = true;
    try {
      const cr = await closeGame(runningPid, gameName);
      if (!cr.ok) {
        errMsg.value =
          '关闭游戏失败：' +
          (cr.msgs?.join('；') || '未知错误') +
          '，文件仍可能被占用。';
        return;
      }
      statusMsg.value = '已关闭 ' + gameName + '，正在应用…';
      await sleep(900); // 等待进程完全释放被占用的文件
      if (game.value && game.value.path === gamePath) {
        game.value = null; // 当前识别游戏已退，识别状态归零
      }
    } catch (e) {
      errMsg.value = '关闭游戏失败：' + (e as Error).message;
      return;
    } finally {
      busy.value = false;
    }
  }

  // 3) 应用 安装 / 卸载
  busy.value = true;
  try {
    if (localStatus) {
      // 已安装 -> 弹窗确认后卸载
      const ok = await dialog.confirm(
        '确认卸载 OptiScaler',
        '确定要从「' +
          gameName +
          '」卸载 OptiScaler (FSR4.1) 吗？\n游戏目录中被覆盖的原始文件会自动还原。'
      );
      if (!ok) {
        statusMsg.value = '已取消卸载。';
        return;
      }
      const r = await oneClickOptiScaler(gamePath, true);
      if (r.ok) {
        statusMsg.value =
          '已卸载 OptiScaler' +
          (r.restored ? '（还原 ' + r.restored + ' 个原文件）' : '') +
          (r.removed ? '，清理 ' + r.removed + ' 个文件' : '') +
          '。';
      } else {
        errMsg.value =
          '卸载失败：' +
          (r.msgs?.join('；') || '未知错误') +
          (runningPid ? '（若仍提示文件被占用，请确认游戏已完全退出）' : '');
      }
    } else {
      // 未安装 -> 直接安装
      const r = await oneClickOptiScaler(gamePath, false);
      if (r.ok) {
        statusMsg.value =
          '已为 ' + gameName + ' 安装 OptiScaler (FSR4.1)' +
          (r.written ? '，写入 ' + r.written + ' 个文件' : '') +
          '。';
      } else {
        errMsg.value =
          '安装失败：' +
          (r.msgs?.join('；') || '未知错误') +
          (runningPid ? '（若仍提示文件被占用，请确认游戏已完全退出）' : '');
      }
    }
  } catch (e) {
    errMsg.value = '操作失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

// 唯一按钮：任意选择 exe 安装/卸载（强制选择）
async function onOptiScalerAny() {
  errMsg.value = '';
  // 引导玩家参考程序读取到的真实进程地址去选 exe
  if (game.value && game.value.path) {
    statusMsg.value =
      '当前识别到的真实游戏路径：' + game.value.path + '\n请选择该目录下的游戏 exe';
  } else {
    statusMsg.value = '请选择游戏主程序（.exe）…';
  }
  const picked = await dialog.openFile([
    { name: '游戏可执行文件', extensions: ['exe'] },
  ]);
  if (!picked) {
    statusMsg.value = '已取消选择。';
    return;
  }
  const gameName = basename(picked);
  // 如果选中的正好是当前识别到的游戏，则复用其 pid 做「运行中关闭」逻辑
  const runningPid = game.value && game.value.path === picked ? game.value.pid : null;
  await applyOptiScalerTo(picked, gameName, runningPid);
}

// ── 启动应用（图标形式，自动衍生）──
const LAUNCH_APPS_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\launch_apps.json';
interface LaunchApp {
  name: string;
  path: string;
  // 允许未来版本或用户自行添加字段；读写时原样保留，避免升级丢数据。
  [key: string]: unknown;
}
const launchApps = ref<LaunchApp[]>([]);
const launchBusy = ref(false);
const menuOpen = ref(false);
const menuPos = ref({ x: 0, y: 0 });
const menuAppIndex = ref(-1);

// 确定性颜色（基于路径 hash）— 明亮系
function iconColor(path: string): string {
  let h = 0;
  for (let i = 0; i < path.length; i++) h = ((h << 5) - h + path.charCodeAt(i)) | 0;
  const colors = ['#4caf50','#2196f3','#9c27b0','#ff9800','#00bcd4','#f44336','#3f51b5','#ff5722','#8bc34a','#03a9f4','#e91e63','#cddc39','#009688','#673ab7','#795548'];
  return colors[Math.abs(h) % colors.length];
}

function normalizeLaunchApp(value: unknown): LaunchApp | null {
  if (!value || typeof value !== 'object') return null;
  const item = value as Record<string, unknown>;
  const path = typeof item.path === 'string' ? item.path.trim() : '';
  if (!path) return null;
  const name = typeof item.name === 'string' && item.name.trim()
    ? item.name
    : basename(path);
  // 展开原对象，保留用户/未来版本字段，只规范 name/path。
  return { ...item, name, path };
}

async function loadLaunchApps() {
  try {
    const quickApps = await readSettingsSection<any>('quickApps');
    const parsed: unknown = quickApps.apps;
    if (Array.isArray(parsed)) {
      launchApps.value = parsed
        .map(normalizeLaunchApp)
        .filter((a): a is LaunchApp => a !== null);
    } else if (parsed && typeof parsed === 'object' && Array.isArray((parsed as any).apps)) {
      // 兼容未来可能采用 { apps: [...] } 的包裹格式。
      launchApps.value = (parsed as any).apps
        .map(normalizeLaunchApp)
        .filter((a: LaunchApp | null): a is LaunchApp => a !== null);
    }
  } catch { /* 文件还不存在或 JSON 损坏时，用空列表，不覆盖原文件 */ }
}

async function saveLaunchApps() {
  try {
    // 原子写入：避免程序退出/断电时把 launch_apps.json 写成半截 JSON。
    await saveSettingsSection('quickApps', { apps: launchApps.value });
  } catch { /* 忽略写入失败，内存中的用户列表不丢 */ }
}

async function addLaunchApp() {
  // 不限定 exe：默认定位「所有文件」，并可切换到 exe / 快捷方式 / 脚本等
  const picked = await dialog.openFile([
    { name: '所有文件', extensions: ['*'] },
    { name: '可执行程序', extensions: ['exe'] },
    { name: '快捷方式', extensions: ['lnk'] },
    { name: '脚本/批处理', extensions: ['bat', 'cmd', 'ps1'] },
  ]);
  if (!picked) return;
  const name = basename(picked);
  if (launchApps.value.some(a => a.path === picked)) {
    errMsg.value = '该应用已在列表中。';
    return;
  }
  launchApps.value.push({ name, path: picked });
  await saveLaunchApps();
  statusMsg.value = `已添加：${name}`;
}

function openMenu(index: number, event: MouseEvent) {
  menuAppIndex.value = index;
  const target = event.currentTarget instanceof HTMLElement ? event.currentTarget : null;
  const rect = target?.getBoundingClientRect();
  // 手柄 A 键/键盘触发 HTMLElement.click() 时，合成 MouseEvent 的 clientX/clientY 为 0。
  // 此时用启动卡片中心定位；真实鼠标/触摸点击仍优先使用事件坐标。
  const hasPointerPosition = Number.isFinite(event.clientX) && Number.isFinite(event.clientY)
    && (event.clientX !== 0 || event.clientY !== 0);
  const rawX = hasPointerPosition ? event.clientX : (rect ? rect.left + rect.width / 2 : window.innerWidth / 2);
  const rawY = hasPointerPosition ? event.clientY : (rect ? rect.top + rect.height / 2 : window.innerHeight / 2);
  // 菜单自身使用 translate(-50%, -50%)，这里按菜单的大致尺寸做边界夹取，避免贴边溢出。
  const x = Math.max(60, Math.min(window.innerWidth - 60, rawX));
  const y = Math.max(72, Math.min(window.innerHeight - 72, rawY));
  menuPos.value = { x, y };
  menuOpen.value = true;
}
function closeMenu() {
  menuOpen.value = false;
  menuAppIndex.value = -1;
}

function onGamepadBack(e: Event) {
  if (!menuOpen.value) return;
  closeMenu();
  // 菜单打开时，B 只做取消，不继续执行全局失焦/返回。
  e.preventDefault();
}

async function launchApp(index: number) {
  closeMenu();
  const app = launchApps.value[index];
  if (!app) return;
  errMsg.value = '';
  statusMsg.value = '';
  launchBusy.value = true;
  try {
    await shell.execute(app.path, []);
    statusMsg.value = `已启动：${app.name}`;
  } catch (e) {
    errMsg.value = `启动失败：${(e as Error).message}`;
  } finally {
    launchBusy.value = false;
  }
}

async function renameApp(index: number) {
  closeMenu();
  const app = launchApps.value[index];
  if (!app) return;
  const input = window.prompt('自定义显示名称（留空则使用原文件名）', app.name);
  if (input === null) return;
  const name = input.trim() || basename(app.path);
  launchApps.value[index].name = name;
  await saveLaunchApps();
  statusMsg.value = `已重命名：${name}`;
}

async function deleteApp(index: number) {
  closeMenu();
  const app = launchApps.value[index];
  if (!app) return;
  launchApps.value.splice(index, 1);
  await saveLaunchApps();
  statusMsg.value = `已删除：${app.name}`;
}

function basename(p: string): string {
  const m = p.match(/[^\\\/]+$/);
  return m ? m[0] : p;
}

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}

// ── 音乐播放：顺序/随机切换 ──
async function toggleMusicMode() {
  await setMode(mode.value === 'sequential' ? 'random' : 'sequential');
}

// ── 音量：拖动实时调（不落盘），松手落盘 ──
function onVolumeInput(e: Event) {
  const el = e.target as HTMLInputElement;
  setVolume(Number(el.value) / 100);
}
function onVolumeChange() {
  persistVolume();
}

onMounted(async () => {
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  await loadLaunchApps();
  await refreshDisplayModes();
  await refreshScale();
});

// KeepAlive：每次进入本页订阅全局游戏状态（立即检测 + 3 秒轮询），退出游戏自动更新
onActivated(() => {
  void refreshDisplayModes();
  void refreshScale();
  if (!unsubGame) {
    unsubGame = subscribeGameStatus(applyGameStatus);
  }
});

onDeactivated(() => {
  // 离开本页退订，停止轮询（避免后台空转 PowerShell）
  if (unsubGame) {
    unsubGame();
    unsubGame = null;
  }
  closeMenu();
});

onUnmounted(() => {
  if (unsubGame) {
    unsubGame();
    unsubGame = null;
  }
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
});
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="statusMsg" class="ok-bar">{{ statusMsg }}</div>

    <!-- 第一排：音乐播放器 -->
    <section class="card">
      <h3 class="card-title"><InlineIcon name="music" /> 音乐播放</h3>

      <div v-if="!hasFolder" class="music-empty">
        <button class="quick-btn" :disabled="busy" @click="chooseFolder">
          <InlineIcon name="folder" /> 选择音乐文件夹
          <span class="quick-sub">播放该目录内的音乐文件（MP3/M4A/WAV/OGG/FLAC）</span>
        </button>
      </div>

      <template v-else>
        <div class="music-now">
          <span class="music-name">{{ currentName || '未选择曲目' }}</span>
          <span class="music-folder">{{ folder }}</span>
        </div>
        <div class="music-ctrl">
          <button class="music-btn" @click="playPrev" title="上一首"><InlineIcon name="prev" /></button>
          <button class="music-btn play" @click="togglePlay" :title="playing ? '暂停' : '播放'"><InlineIcon :name="playing ? 'pause' : 'play'" /></button>
          <button class="music-btn" @click="playNext" title="下一首"><InlineIcon name="next" /></button>
          <button class="music-btn mode" :class="{ on: mode === 'random' }" @click="toggleMusicMode" title="播放模式">{{ mode === 'random' ? '随机' : '顺序' }}</button>
        </div>
        <div class="music-volume">
          <button class="vol-btn" @click="toggleMute" :title="muted || volume === 0 ? '取消静音' : '静音'"><InlineIcon :name="muted || volume === 0 ? 'mute' : 'volume'" /></button>
          <input class="vol-slider" type="range" min="0" max="100" step="1" :value="Math.round(volume * 100)" @input="onVolumeInput" @change="onVolumeChange" />
          <span class="vol-num">{{ Math.round(volume * 100) }}%</span>
        </div>
        <div class="music-actions">
          <button class="quick-btn slim" @click="chooseFolder"><InlineIcon name="folder" /> 更换文件夹</button>
          <button class="quick-btn slim" :disabled="busy" @click="scanFolder"><InlineIcon name="refresh" /> 重新扫描</button>
        </div>
      </template>
      <div v-if="musicError" class="music-err">{{ musicError }}</div>
    </section>

    <!-- 第二排：显示设置，分辨率和多显示器各自独立气泡 -->
    <div class="display-bubbles display-settings-bubbles">
      <section class="card display-block">
        <div class="sub-head">
          <span class="sub-title"><InlineIcon name="fullscreen" /> 屏幕分辨率调节</span>
          <button class="add-app-btn" :disabled="displayBusy" @click="refreshDisplayModes">刷新</button>
        </div>
        <div class="display-row">
          <div class="display-control">
            <span class="display-control-label">分辨率</span>
            <Dropdown :model-value="displayCurrent" :options="displayOptions" :disabled="displayBusy || displayOptions.length === 0" color="accent" aria-label="当前显示器分辨率" @change="applyDisplayMode" />
          </div>
          <div class="display-control">
            <span class="display-control-label">缩放和布局</span>
            <Dropdown :model-value="scalePct" :options="scaleOptions" :disabled="scaleBusy" color="accent" aria-label="Windows缩放和布局" @change="applyScale" />
          </div>
        </div>
        <span class="quick-sub display-tip">只调整本程序所在显示器，支持横屏/竖屏模式</span>
      </section>

      <section class="card display-block monitor-switch-block">
        <div class="sub-head">
          <span class="sub-title"><InlineIcon name="monitor" /> 多显示器切换</span>
        </div>
        <div class="topology-grid">
          <button v-for="option in displayTopologyOptions" :key="option.value" class="topology-btn" :class="{ on: displayTopology === option.value }" :disabled="displayBusy" @click="applyDisplayTopology(option.value)">
            <strong>{{ option.label }}</strong><small>{{ option.detail }}</small>
          </button>
        </div>
      </section>
    </div>

    <!-- 剩余快捷功能：插帧并排，随后游戏加速，最后应用启动 -->
    <section class="card quick-functions-card">
      <h3 class="card-title"><InlineIcon name="settings" /> 快捷功能</h3>
      <div class="frame-actions">
        <button class="quick-btn" :disabled="busy" @click="onLaunchLs"><InlineIcon name="rocket" /> 小黄鸭一键插帧<span class="quick-sub">写入 LS 插帧预设并启动</span></button>
        <button class="quick-btn opti-btn opti-any" :disabled="busy" @click="onOptiScalerAny"><InlineIcon name="bolt" /> 一键安装FSR4.1<span class="quick-sub">选择游戏 exe 后安装</span></button>
      </div>

      <div class="sub-block">
        <div class="sub-head">
          <span class="sub-title"><InlineIcon name="speed" /> 游戏加速</span>
          <span class="sub-tip">{{ activeSpeed ? '当前 ' + activeSpeed + '×，点 1× 还原' : '选择倍率加速当前游戏（1× = 关闭）' }}</span>
        </div>
        <div class="speed-grid">
          <button
            v-for="f in SPEED_PRESETS"
            :key="f"
            class="speed-btn"
            :class="{ on: activeSpeed === f, off: f === 1 && !activeSpeed }"
            :disabled="speedBusy || busy || !game"
            @click.stop="onSpeedApply(f)"
          >{{ f }}×</button>
        </div>
      </div>

      <div class="sub-block launch-block">
        <div class="sub-head">
          <span class="sub-title"><InlineIcon name="rocket" /> 启动应用</span>
          <button class="add-app-btn" :disabled="launchBusy" @click="addLaunchApp">+ 添加应用</button>
        </div>
        <div class="launch-grid">
          <button
            v-for="(app, i) in launchApps"
            :key="app.path"
            class="launch-card"
            type="button"
            :disabled="launchBusy"
            @click="openMenu(i, $event)"
            @contextmenu.prevent="openMenu(i, $event)"
          >
            <div class="launch-icon" :style="{ background: iconColor(app.path) }">
              {{ app.name.charAt(0).toUpperCase() }}
            </div>
            <span class="launch-name">{{ app.name.replace(/\.exe$/i, '') }}</span>
          </button>
        </div>
        <!-- 点击/右键弹出菜单 -->
        <Teleport to="body">
          <div v-if="menuOpen" class="launch-menu-mask" @click="closeMenu" @contextmenu.prevent="closeMenu"></div>
          <Transition name="pop">
            <div v-if="menuOpen" class="launch-menu" :style="{ left: menuPos.x + 'px', top: menuPos.y + 'px' }">
              <button class="launch-menu-item" @click="launchApp(menuAppIndex)"><InlineIcon name="play" /> 启动</button>
              <button class="launch-menu-item" @click="renameApp(menuAppIndex)"><InlineIcon name="edit" /> 重命名</button>
              <button class="launch-menu-item launch-menu-danger" @click="deleteApp(menuAppIndex)"><InlineIcon name="close" /> 删除</button>
              <button class="launch-menu-item launch-menu-cancel" @click="closeMenu"><InlineIcon name="close" /> 取消</button>
            </div>
          </Transition>
        </Teleport>
      </div>
    </section>

  </div>
</template>

<style scoped>
.page {
  padding-bottom: 20px;
  display: flex;
  flex-direction: column;
}
.quick-functions-card { order: 1; }
.display-settings-bubbles { order: 2; }
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
.ok-bar {
  background: rgba(46, 166, 255, 0.12);
  border: 1px solid rgba(46, 166, 255, 0.4);
  color: #9fd0ff;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
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
/* ── 一键插帧等整行按钮 ── */
.quick-btn {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 11px 12px;
  border-radius: var(--radius-ctrl, 8px);
  border: 1px solid rgba(46, 166, 255, 0.4);
  background: rgba(46, 166, 255, 0.1);
  color: var(--text);
  font-size: 13px;
  font-weight: 700;
  cursor: pointer;
  text-align: left;
  transition: border-color 0.12s, background 0.12s;
}
.quick-btn:hover:not(:disabled) {
  background: rgba(46, 166, 255, 0.18);
}
.quick-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.quick-sub {
  font-size: 11px;
  font-weight: 400;
  color: var(--text-dim);
}
/* ── OptiScaler FSR4.1 按钮 ── */
.opti-btn {
  border-color: rgba(124, 179, 255, 0.4);
  background: rgba(124, 179, 255, 0.1);
}
.opti-btn:hover:not(:disabled) {
  background: rgba(124, 179, 255, 0.18);
}
.opti-btn.installed {
  border-color: rgba(245, 185, 61, 0.55);
  background: rgba(245, 185, 61, 0.14);
  color: #f5c95d;
}
.opti-btn.installed:hover:not(:disabled) {
  background: rgba(245, 185, 61, 0.22);
}
/* 右侧「任意安装」按钮：紫灰区分 */
.opti-btn.opti-any {
  border-color: rgba(167, 139, 250, 0.45);
  background: rgba(167, 139, 250, 0.1);
}
.opti-btn.opti-any:hover:not(:disabled) {
  background: rgba(167, 139, 250, 0.18);
}
.frame-actions {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
}
.frame-actions .quick-btn {
  min-width: 0;
  min-height: 54px;
}
.display-bubbles {
  display: grid;
  grid-template-columns: minmax(0, 1fr);
  gap: 10px;
}
.display-bubbles .card {
  min-width: 0;
}
.monitor-switch-block .sub-head {
  margin-bottom: 8px;
}
.topology-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 6px;
}
.topology-btn {
  min-width: 0;
  min-height: 46px;
  padding: 6px 4px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  border: 1px solid #2a3342;
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  cursor: pointer;
}
.topology-btn strong { font-size: 11px; }
.topology-btn small { font-size: 9px; color: var(--text-dim); white-space: nowrap; }
.topology-btn:hover:not(:disabled),
.topology-btn.on {
  border-color: var(--accent);
  background: rgba(46, 166, 255, 0.14);
}
.topology-btn:disabled { opacity: 0.45; cursor: not-allowed; }
/* ── 子块（游戏加速 / 启动应用） ── */
.sub-block {
  margin-top: 12px;
}
.sub-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}
.sub-title {
  font-size: 12px;
  font-weight: 700;
  color: var(--text);
}
.sub-tip {
  font-size: 10px;
  color: var(--text-dim);
}
/* 游戏加速倍率：页面同宽均分 */
.speed-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(0, 1fr));
  gap: 6px;
}
.speed-btn {
  padding: 9px 0;
  font-size: 14px;
  font-weight: 700;
  border-radius: 8px;
  border: 1px solid #2a3342;
  background: var(--bg-input);
  color: var(--text);
  cursor: pointer;
  text-align: center;
  transition: border-color 0.12s, background 0.12s;
}
.speed-btn:hover:not(:disabled) {
  border-color: var(--accent);
  background: rgba(46, 166, 255, 0.12);
}
.speed-btn.on {
  border-color: #f5b93d;
  background: rgba(245, 185, 61, 0.2);
  color: #f5b93d;
}
.speed-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.speed-btn.off {
  border-color: rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
}
.speed-btn.off:hover:not(:disabled) {
  background: rgba(229, 72, 77, 0.12);
}
/* ── 启动应用（图标形式）── */
.launch-block { margin-top: 12px; }
.add-app-btn {
  flex: 0 0 auto;
  padding: 5px 12px;
  font-size: 11px;
  font-weight: 700;
  color: var(--text);
  background: rgba(46, 166, 255, 0.1);
  border: 1px solid rgba(46, 166, 255, 0.4);
  border-radius: var(--radius-ctrl);
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s;
}
.add-app-btn:hover:not(:disabled) {
  background: rgba(46, 166, 255, 0.2);
  border-color: var(--accent);
}
.add-app-btn:disabled { opacity: 0.5; cursor: not-allowed; }
.launch-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr); /* 一行 3 个：卡片长度较 4 列加长约 50% */
  gap: 10px;
  margin-top: 8px;
}
.launch-card {
  display: flex;
  flex-direction: row;
  align-items: center;
  gap: 8px;
  width: 100%;
  min-width: 0;
  padding: 8px 12px;
  border-radius: 10px;
  background: transparent;
  border: 1px solid transparent;
  cursor: pointer;
  font-family: inherit;
  transition: background 0.12s, border-color 0.12s, transform 0.1s;
}
.launch-card:hover { background: rgba(46, 166, 255, 0.12); border-color: #3a4a5e; }
.launch-card:focus-visible { outline: none; box-shadow: 0 0 0 2px rgba(46,166,255,0.4); border-color: var(--accent); }
.launch-card:disabled { opacity: 0.45; cursor: not-allowed; }
.launch-card:active:not(:disabled) { transform: scale(0.97); }
.launch-icon {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 15px;
  font-weight: 800;
  color: #fff;
  text-shadow: 0 1px 3px rgba(0,0,0,0.35);
  box-shadow: 0 2px 6px rgba(0,0,0,0.25), inset 0 1px 0 rgba(255,255,255,0.18);
  flex-shrink: 0;
}
.launch-name {
  font-size: 10px;
  color: var(--text);
  text-align: left;
  line-height: 1.2;
  flex: 1 1 auto;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
/* 弹出菜单 */
.launch-menu-mask {
  position: fixed; inset: 0; z-index: 999;
  background: transparent;
}
.launch-menu {
  position: fixed;
  z-index: 1000;
  background: #161d29;
  border: 1px solid #2a3342;
  border-radius: 10px;
  padding: 5px;
  box-shadow: 0 8px 24px rgba(0,0,0,0.5);
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 110px;
  transform: translate(-50%, -50%);
}
.launch-menu-item {
  width: 100%;
  padding: 9px 14px;
  background: transparent;
  border: none;
  border-radius: 6px;
  color: var(--text);
  font-size: 12px;
  font-weight: 600;
  font-family: inherit;
  text-align: left;
  cursor: pointer;
  transition: background 0.1s;
}
.launch-menu-item:hover { background: rgba(46, 166, 255, 0.14); }
.launch-menu-danger:hover { background: rgba(229, 72, 77, 0.18); color: #ff9ea1; }
.launch-menu-cancel { color: var(--text-dim); }
.pop-enter-active,
.pop-leave-active { transition: opacity 0.12s ease, transform 0.12s ease; }
.pop-enter-from,
.pop-leave-to { opacity: 0; transform: scale(0.95); }
/* ── 音乐播放 ── */
.display-row { display: grid; grid-template-columns: minmax(0, 2fr) minmax(0, 1fr); gap: 8px; }
.display-control { min-width: 0; }
.display-control-label { display: block; margin: 0 0 5px; font-size: 11px; color: var(--text-dim); }
.display-tip { display: block; margin-top: 6px; line-height: 1.35; }
.music-empty { margin-top: 4px; }
.music-now {
  display: flex;
  flex-direction: column;
  gap: 3px;
  margin: 4px 0 10px;
}
.music-name {
  font-size: 13px;
  font-weight: 700;
  color: var(--text);
  word-break: break-all;
}
.music-folder {
  font-size: 10px;
  color: var(--text-dim);
  word-break: break-all;
}
.music-ctrl {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
}
.music-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  padding: 11px 4px;
  border-radius: var(--radius-ctrl, 8px);
  border: 1px solid var(--border, #1c2533);
  background: var(--bg-input, #0e1622);
  color: var(--text);
  font-size: 12px;
  font-weight: 700;
  cursor: pointer;
  transition: border-color 0.12s, background 0.12s, color 0.12s;
}
.music-btn:hover:not(:disabled) {
  border-color: var(--accent);
  background: rgba(46, 166, 255, 0.12);
}
.music-btn:disabled { opacity: 0.45; cursor: not-allowed; }
.music-btn.play { font-size: 16px; }
.music-btn.mode.on {
  border-color: rgba(245, 185, 61, 0.5);
  background: rgba(245, 185, 61, 0.15);
  color: #f5b93d;
}
.music-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin-top: 10px;
}
.music-actions .quick-btn.slim {
  padding: 9px 10px;
  font-size: 12px;
  justify-content: center;
  line-height: 1;
}
/* ── 音量 ── */
.music-volume {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 10px;
}
.vol-btn {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: 8px;
  border: 1px solid var(--border, #1c2533);
  background: var(--bg-input, #0e1622);
  color: var(--text);
  cursor: pointer;
  transition: border-color 0.12s, background 0.12s;
}
.vol-btn:hover { border-color: var(--accent); background: rgba(46, 166, 255, 0.12); }
.vol-slider {
  flex: 1 1 auto;
  min-width: 0;
  accent-color: var(--accent);
  height: 4px;
  cursor: pointer;
}
.vol-slider:focus-visible {
  outline: 2px solid var(--accent);
  outline-offset: 3px;
}
.vol-num {
  flex: 0 0 auto;
  width: 38px;
  text-align: right;
  font-size: 11px;
  color: var(--text-dim);
  font-variant-numeric: tabular-nums;
}
.music-err {
  margin-top: 10px;
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 7px 10px;
  font-size: 11px;
  line-height: 1.4;
}
</style>
