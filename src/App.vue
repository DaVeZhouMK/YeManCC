<script setup lang="ts">
import { ref, computed, provide, onMounted, onUnmounted } from 'vue';
import { useRouter } from 'vue-router';
import NavRail from '@/components/NavRail.vue';
import DebugView from '@/components/DebugView.vue';
import { useDebugStore } from '@/stores/debug';
import { startGamepad } from '@/gamepad/engine';
import { on } from '@/bridge/ipc';

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
  transform: `scale(${uiScale.value})`,
  transformOrigin: 'top left',
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
onMounted(async () => {
  window.addEventListener('ipc:gamepad-start', onGamepadStart as EventListener);
  window.addEventListener('ipc:gamepad.refresh', onGamepadRefresh as EventListener);
  stopGamepad = startGamepad({ router, onAction: (l) => store.pushGamepad(l) });
  stopAcWatch = on('power.acChanged', () => { globalRefreshKey.value++; });

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
      <div class="splash-spin">⏳</div>
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
  background: var(--bg);
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
  background: var(--bg);
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
