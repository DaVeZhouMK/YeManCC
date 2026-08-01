<script setup lang="ts">
import { ref, computed, provide, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import NavRail from '@/components/NavRail.vue';
import DebugView from '@/components/DebugView.vue';
import { useDebugStore } from '@/stores/debug';
import { startGamepad } from '@/gamepad/engine';
import { on } from '@/bridge/ipc';
import { applyCpuAutostart } from '@/bridge/autostart';
import { applyTrayResident } from '@/bridge/trayResident';
import { loadCpuLock, applyCpuLock, isCpuLocked } from '@/bridge/cpulock';
import { getFloatInfo, applyCpuAutoEnable } from '@/bridge/autofloat';
import { fs } from '@/bridge/api';
import { toggleTask, taskExists } from '@/bridge/yeman';
import AppIcon from '@/components/AppIcon.vue';

const BOOT_TASK = '野蛮控制中心-开机启动';
const BOOT_CFG = 'C:\\SOFT\\YeMan\\PowerControl\\boot_config.json';
// 开机启动自愈：若 boot_config.json 记录"开"但计划任务被删除（Windows 更新/杀软），自动重建
async function healBootTask() {
  try {
    const txt = await fs.readTextFile(BOOT_CFG);
    const j = JSON.parse(txt) as { bootOn?: boolean };
    if (j.bootOn === true && !(await taskExists(BOOT_TASK))) {
      await toggleTask(BOOT_TASK, true);
    }
  } catch { /* 配置文件不存在或无权限，忽略 */ }
}

const router = useRouter();
const store = useDebugStore();
// 启动耗时采样：模块加载起点
const APP_START = performance.now();
(window as any).__appStart = APP_START;
const showDebug = ref(false);
function toggleDebug() {
  showDebug.value = !showDebug.value;
}

// ── 全局刷新触发器：支持页刷新按钮触发所有已挂载页面 refresh() ──
const globalRefreshKey = ref(0);
provide('globalRefreshKey', globalRefreshKey);

// ── UI 缩放层：设计基准 580×780，按窗口实际高度等比放大填满（配合壳的 fullHeight 模式）──
const BASE_W = 580;
const BASE_H = 780;
const uiScale = ref(1);
function updateScale() {
  uiScale.value = window.innerHeight > 0 ? window.innerHeight / BASE_H : 1;
}
const scalerStyle = computed(() => ({
  width: BASE_W + 'px',
  height: BASE_H + 'px',
  // zoom 同时参与布局和命中测试；transform 只放大绘制层，会让 WebView2
  // 看到的坐标与鼠标点击坐标分离，表现为内容区"看得到但点不到"。
  zoom: uiScale.value,
}));

// ── 启动顺序预加载（TDP → 支持）──
// KeepAlive 只挂载当前页，故需在启动时顺序导航各路由，触发各页 onMounted→refresh
// 预加载期间内容区隐藏（visibility:hidden），避免页面闪烁
const preloading = ref(true);
const PRELOAD_ROUTES = ['/tdp', '/cpu', '/rtss', '/power', '/steam', '/sleep', '/settings'];

// M8: Start button toggles the debug panel via the same 'ipc:gamepad-start' event.
function onGamepadStart() {
  toggleDebug();
}
function onGamepadRefresh() {
  globalRefreshKey.value++;
}
let stopGamepad: (() => void) | null = null;
// 壳订阅系统 AC/DC 插拔事件（推送式，已在 native 侧做 5s 尾防抖 + 频繁切换熔断），
// 到达即刷新一次所有已挂载页面数据。
let stopAcWatch: (() => void) | null = null;
let stopCloseWatch: (() => void) | null = null;
onMounted(async () => {
  window.addEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.addEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  stopGamepad = startGamepad({ router, onAction: (l) => store.pushGamepad(l) });
  // AC/DC 插拔：刷新数据；若用户锁定了主频/积极性，则重新套用锁定值
  // （Windows 切换电源来源时会按方案重算 CPU 策略，必须复写才守得住）
  // 优先级：自动CPU浮动优化开启时它最大 —— 此时不套用锁定值，由浮动自己下一轮写回。
  stopAcWatch = on('power.acChanged', async () => {
    globalRefreshKey.value++;
    // 按「自动启用」设置 + 新电源状态自动开关浮动优化；之后若浮动仍未开启，再套用锁定值
    await applyCpuAutoEnable().catch(() => {});
    if (!getFloatInfo().enabled) applyCpuLock().catch(() => {});
  });
  // 锁定配置提前载入到模块缓存，供 autofloat 每秒 tick 同步判定
  // 必须先 await 完成，否则 applyCpuAutoEnable→disableFloat 的 isCpuLocked() 会读到过期缓存、漏掉锁定值回刷
  await loadCpuLock().catch(() => {});
  // 退出前：若已锁定，把锁定值刷回系统（防止浮动改动后无法快速恢复到配置文件）
  stopCloseWatch = on('window.closing', () => {
    if (isCpuLocked()) applyCpuLock().catch(() => {});
  });
  // 按「自动启用」设置 + 当前电源状态，在启动时自动启用/禁用浮动优化
  await applyCpuAutoEnable().catch(() => {});

  // ── 任务栏常驻偏好：启动期按持久化套用（默认不常驻，由设置/总闸驱动）──
  applyTrayResident().catch(() => {});
  // ── 开机启动自愈：若用户曾开启但计划任务被误删，自动重建 ──
  healBootTask().catch(() => {});

  // ── CPU「30秒自行启用」：打开程序 30 秒后，按记录状态自动套用 CCD / 降压 ──
  // 延迟 30 秒是开机保护：避免降压过猛（如 risk 档）在开机瞬间直接死机。
  window.setTimeout(() => {
    applyCpuAutostart().catch(() => {});
  }, 30000);

  const home = router.currentRoute.value.fullPath;

  // 沙漏显示的同时立即开始后台顺序预加载（并行，不再空转等待）
  const preload = (async () => {
    for (const r of PRELOAD_ROUTES) {
      if (r === home) continue; // 首页已挂载，跳过
      await router.push(r);
      // 等待组件挂载 + refresh 启动（注册表读取很快）
      await new Promise((res) => setTimeout(res, 150));
    }
    await router.push(home); // 回到首页
  })().catch(() => {});

  // 沙漏至少存在 2 秒；且等预加载结束（数据就绪）后再隐藏，避免闪烁/卡死
  await new Promise((res) => setTimeout(res, 2000));
  await preload;
  preloading.value = false; // 显示内容
  (window as any).__startupTotalMs = performance.now() - APP_START;
  // 给最后的收尾 IPC 留一点时间再输出采样报告
  setTimeout(() => (window as any).__flushStartupProfile?.(), 600);
});
onUnmounted(() => {
  window.removeEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.removeEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  stopGamepad?.();
  stopAcWatch?.();
  stopCloseWatch?.();
});

// ── UI 缩放：监听窗口尺寸 / 原生 resize 事件，保持设计比例填满窗口 ──
onMounted(() => {
  updateScale();
  // 首帧后再校准一次（WebView2 宿主尺寸刚就绪时 innerHeight 可能未稳定）
  requestAnimationFrame(updateScale);
  window.addEventListener('resize', updateScale);
  window.addEventListener('ipc:window.resized', updateScale as EventListener);
});
onUnmounted(() => {
  window.removeEventListener('resize', updateScale);
  window.removeEventListener('ipc:window.resized', updateScale as EventListener);
});
</script>

<template>
  <div class="app-shell" :style="scalerStyle">
    <div class="app-body">
      <NavRail />
      <main class="app-main" :class="{ 'is-preloading': preloading }">
        <router-view v-slot="{ Component }">
          <KeepAlive>
            <component :is="Component" />
          </KeepAlive>
        </router-view>
      </main>
    </div>

    <div v-if="preloading" class="splash">
      <div class="splash-spin"><AppIcon name="refresh" /></div>
      <div class="splash-text">正在预加载数据…</div>
    </div>

    <DebugView v-if="showDebug" @close="showDebug = false" />
  </div>
</template>

<style scoped>
.app-shell {
  height: 100%;
  display: flex;
  flex-direction: column;
  /* 根节点透明；可辨识度由导航、卡片和控件底板分块提供。 */
  background: transparent;
}
.app-body {
  flex: 1 1 auto;
  display: flex;
  min-height: 0;
}
.app-main {
  flex: 1 1 auto;
  min-width: 0;
  overflow-y: auto;
  padding: 12px;
  /* 主内容区提供稳定的分块底板；接近实体（仅 3% 透），避免大块透出桌面。 */
  background: rgba(11, 16, 24, 0.97);
  position: relative;
  z-index: 0;
}
/* 预加载期间隐藏内容区（导航在后台进行，无闪烁） */
.app-main.is-preloading {
  visibility: hidden;
}
.splash {
  position: fixed;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  /* 启动遮罩保持近实色，避免预加载期透出桌面产生闪烁 */
  background: rgba(11, 14, 19, 0.9);
  z-index: 50;
}
.splash-spin {
  font-size: 30px;
  animation: spin 1.1s linear infinite;
}
.splash-text {
  font-size: 13px;
  color: var(--text-dim);
}
@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>
