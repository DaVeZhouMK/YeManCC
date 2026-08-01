<script setup lang="ts">
import { ref, onMounted, onUnmounted, onDeactivated, nextTick } from 'vue';
import {
  type GameProc,
  detectForegroundGame,
  oneClickFrameGen,
  optiscalerStatus,
  oneClickOptiScaler,
  dirnameOf,
  LS_PRIMARY,
} from '@/bridge/quickapp';
import {
  SPEED_PRESETS,
  applyGameSpeed,
  clearGameSpeed,
} from '@/bridge/speedhack';
import {
  closeGame,
  isJoyxoffRunning,
  toggleJoyxoff,
  hasSuspendedState,
} from '@/bridge/gameproc';
import { sleepGuardSuspendCurrent, sleepGuardRecoverAll } from '@/bridge/yeman';
import { fs, shell, dialog } from '@/bridge/api';
import InlineIcon from '@/components/InlineIcon.vue';

const busy = ref(false);
const errMsg = ref('');
const statusMsg = ref('');

const game = ref<GameProc | null>(null);

// ── 游戏控制状态 ──
const ctrlBusy = ref(false);
const paused = ref(false); // 当前游戏是否已暂停（与 native Sleep\\suspended\\<pid>.txt 标记同步）
const mouseOn = ref(false); // JoyXoff 模拟鼠标是否开启

// ── 游戏加速状态 ──
const speedBusy = ref(false);
const activeSpeed = ref<number | null>(null); // 当前生效的倍率，null=未加速

async function onSpeedApply(factor: number) {
  if (factor === 1) { onSpeedOff(); return; }
  errMsg.value = '';
  statusMsg.value = '';
  if (!game.value) {
    errMsg.value = '请先刷新识别当前游戏，再加速。';
    return;
  }
  speedBusy.value = true;
  try {
    const r = await applyGameSpeed(game.value.pid, factor);
    if (r.ok) {
      activeSpeed.value = factor;
      statusMsg.value = `已对 ${game.value.name} 应用 ${factor}× 变速`;
    } else {
      errMsg.value = '变速失败：' + (r.msgs.join('; ') || '未知错误');
      activeSpeed.value = null;
    }
  } catch (e) {
    errMsg.value = '变速失败：' + (e as Error).message;
    activeSpeed.value = null;
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
  speedBusy.value = true;
  try {
    const r = await clearGameSpeed(game.value.pid);
    if (r.ok) {
      activeSpeed.value = null;
      statusMsg.value = '已关闭变速，恢复原始速度。';
    } else {
      errMsg.value = '关闭变速失败：' + (r.msgs.join('; ') || '未知错误');
    }
  } catch (e) {
    errMsg.value = '关闭变速失败：' + (e as Error).message;
  } finally {
    speedBusy.value = false;
  }
}

async function onDetect() {
  errMsg.value = '';
  statusMsg.value = '';
  busy.value = true;
  try {
    const g = await detectForegroundGame();
    if (!g) {
      errMsg.value = '未检测到前台游戏窗口，请切换到游戏后重试。';
      game.value = null;
      return;
    }
    game.value = g;
    statusMsg.value = '已识别：' + (g.title || g.name);
    // 同步暂停状态（跨刷新 / 跨页面保持）
    try {
      paused.value = (await hasSuspendedState()).suspended;
    } catch {
      paused.value = false;
    }
  } catch (e) {
    errMsg.value = '识别失败：' + (e as Error).message;
    game.value = null;
  } finally {
    busy.value = false;
  }
}

// ── 游戏控制：暂停 / 继续 / 关闭 / 模拟鼠标 ──
// 暂停/继续改为复用 native 睡眠守护的 NtSuspend 枚举（取最大工作集进程），
// 与「睡眠优化」页手动暂停效果完全一致；避免 detectForegroundGame 识别的根 pid
// 已退出/是 launcher 导致暂停失败。
async function onPause() {
  if (paused.value || ctrlBusy.value) return;
  ctrlBusy.value = true;
  errMsg.value = '';
  statusMsg.value = '';
  try {
    const r = await sleepGuardSuspendCurrent();
    if (r.paused) {
      paused.value = true;
      statusMsg.value = '已暂停 ' + (r.name || '当前游戏') + (r.pid ? '（PID ' + r.pid + '）' : '');
    } else {
      errMsg.value = '暂停失败：没有可暂停的游戏进程';
    }
  } catch (e) {
    errMsg.value = '暂停失败：' + (e as Error).message;
  } finally {
    ctrlBusy.value = false;
  }
}

async function onResume() {
  if (!paused.value || ctrlBusy.value) return;
  ctrlBusy.value = true;
  errMsg.value = '';
  statusMsg.value = '';
  try {
    const r = await sleepGuardRecoverAll();
    paused.value = false;
    if (r.resumed > 0) {
      statusMsg.value = '已继续 ' + r.resumed + ' 个被冻结的进程';
    } else {
      statusMsg.value = '没有待恢复的进程';
    }
  } catch (e) {
    paused.value = false;
    errMsg.value = '继续失败：' + (e as Error).message;
  } finally {
    ctrlBusy.value = false;
  }
}

async function onClose() {
  if (!game.value || ctrlBusy.value) return;
  ctrlBusy.value = true;
  errMsg.value = '';
  statusMsg.value = '';
  try {
    const r = await closeGame(game.value.pid, game.value.name);
    paused.value = false;
    if (r.ok) {
      statusMsg.value = '已关闭 ' + game.value.name;
    } else {
      errMsg.value = '关闭失败：' + (r.msgs.join('；') || '未找到可关闭的游戏');
    }
  } catch (e) {
    errMsg.value = '关闭失败：' + (e as Error).message;
  } finally {
    ctrlBusy.value = false;
  }
}

async function onToggleMouse() {
  if (ctrlBusy.value) return;
  ctrlBusy.value = true;
  errMsg.value = '';
  statusMsg.value = '';
  try {
    const on = await toggleJoyxoff();
    mouseOn.value = on;
    statusMsg.value = on ? '模拟鼠标已开启' : '模拟鼠标已关闭';
  } catch (e) {
    errMsg.value = '模拟鼠标切换失败：' + (e as Error).message;
  } finally {
    ctrlBusy.value = false;
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
    const raw = await fs.readTextFile(LAUNCH_APPS_FILE, 65536);
    const parsed: unknown = JSON.parse(raw);
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
    await fs.writeTextFileAtomic(LAUNCH_APPS_FILE, JSON.stringify(launchApps.value, null, 2));
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

onMounted(async () => {
  window.addEventListener('ipc:gamepad-back', onGamepadBack);
  await nextTick(onDetect);
  await loadLaunchApps();
  try {
    mouseOn.value = await isJoyxoffRunning();
  } catch {
    mouseOn.value = false;
  }
});

onDeactivated(() => {
  closeMenu();
});

onUnmounted(() => {
  window.removeEventListener('ipc:gamepad-back', onGamepadBack);
});
</script>

<template>
  <div class="page">
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div v-if="statusMsg" class="ok-bar">{{ statusMsg }}</div>

    <!-- 顶部：游戏控制识别（识别 + 4 个控制按钮融合在同一卡片） -->
    <section class="card detect-card">
      <h3 class="card-title"><InlineIcon name="gamepad" /> 游戏控制识别</h3>
      <div class="detect-body">
        <div class="detect-info">
          <span class="detect-name">{{ game ? (game.title || game.name) : '未识别' }}</span>
          <span v-if="game" class="detect-path">{{ game.path }}</span>
        </div>
        <button class="action-btn" :disabled="busy" @click="onDetect">
          {{ busy ? '识别中…' : '刷新当前识别游戏' }}
        </button>
      </div>

      <!-- 4 个并排控制按钮 -->
      <div class="ctrl-grid">
        <button
          class="ctrl-btn"
          :disabled="paused || ctrlBusy || busy"
          @click="onPause"
        >
          <span class="ctrl-icon"><InlineIcon name="pause" /></span>
          <span class="ctrl-label">暂停游戏</span>
        </button>
        <button
          class="ctrl-btn"
          :disabled="!paused || ctrlBusy || busy"
          @click="onResume"
        >
          <span class="ctrl-icon"><InlineIcon name="play" /></span>
          <span class="ctrl-label">继续游戏</span>
        </button>
        <button
          class="ctrl-btn ctrl-danger"
          :disabled="!game || ctrlBusy || busy"
          @click="onClose"
        >
          <span class="ctrl-icon"><InlineIcon name="close" /></span>
          <span class="ctrl-label">关闭游戏</span>
        </button>
        <button
          class="ctrl-btn"
          :class="{ on: mouseOn }"
          :disabled="ctrlBusy"
          @click="onToggleMouse"
        >
          <span class="ctrl-icon"><InlineIcon name="mouse" /></span>
          <span class="ctrl-label">{{ mouseOn ? '模拟鼠标已开启' : '模拟鼠标已关闭' }}</span>
        </button>
      </div>
    </section>

    <!-- 快捷功能（横向紧凑，对齐整体风格） -->
    <section class="card">
      <h3 class="card-title"><InlineIcon name="settings" /> 快捷功能</h3>

      <button class="quick-btn" :disabled="busy" @click="onLaunchLs">
        <InlineIcon name="rocket" /> 小黄鸭一键插帧　<span class="quick-sub">写入 LS 插帧预设并启动</span>
      </button>

      <!-- 参考上方识别到的真实进程路径，选择该游戏 exe 安装/卸载 -->
      <button
        class="quick-btn opti-btn opti-any"
        :disabled="busy"
        @click="onOptiScalerAny"
      >
        <InlineIcon name="bolt" /> 一键安装FSR4.1插帧
        <span class="quick-sub">参考识别真实exe路径，游戏内选择DLSS开启即可</span>
      </button>

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
  background: var(--bg-panel);
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
.detect-body {
  display: flex;
  align-items: center;
  gap: 10px;
}
.detect-info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.detect-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
  word-break: break-all;
}
.detect-path {
  font-size: 10px;
  color: var(--text-dim);
  word-break: break-all;
}
.action-btn {
  flex: 0 0 auto;
  background: var(--accent);
  color: #06121d;
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 10px 14px;
  font-weight: 700;
  font-size: 12px;
  cursor: pointer;
}
.action-btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
}
.action-btn:focus-visible {
  box-shadow: var(--focus-ring);
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
/* ── 游戏控制（识别卡片内 4 个并排按钮） ── */
.ctrl-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
  margin-top: 12px;
}
.ctrl-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 3px;
  padding: 11px 4px;
  border-radius: var(--radius-ctrl, 8px);
  border: 1px solid var(--border, #1c2533);
  background: var(--bg-input, #0e1622);
  color: var(--text);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: border-color 0.12s, background 0.12s, color 0.12s;
}
.ctrl-btn:hover:not(:disabled) {
  border-color: var(--accent);
  background: rgba(46, 166, 255, 0.12);
}
.ctrl-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.ctrl-btn.on {
  border-color: rgba(245, 185, 61, 0.5);
  background: rgba(245, 185, 61, 0.15);
  color: #f5b93d;
}
.ctrl-danger:hover:not(:disabled) {
  border-color: rgba(229, 72, 77, 0.5);
  background: rgba(229, 72, 77, 0.12);
}
.ctrl-icon {
  font-size: 18px;
  line-height: 1;
}
.ctrl-label {
  font-size: 11px;
  line-height: 1.25;
  text-align: center;
}
</style>
