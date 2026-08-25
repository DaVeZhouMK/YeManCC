<script setup lang="ts">
import { ROUTES } from '@/router';
import { useRouter, useRoute } from 'vue-router';
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { app, shell, windowApi } from '@/bridge/api';
import AppIcon from '@/components/AppIcon.vue';
import { fanHostLifecycle } from '@/bridge/fanHost';
import {
  loadPerformanceSchedule,
  onPerformanceScheduleChanged,
} from '@/bridge/performanceSchedule';
import {
  fanFeatureEnabled,
  fanNavigationMotion,
  fanNavigationSpinDuration,
} from '@/bridge/fanFeature';

const router = useRouter();
const route = useRoute();
const quickModeEnabled = ref(false);
let stopScheduleWatch: (() => void) | null = null;

const visibleRoutes = computed(() => ROUTES.filter((item) => {
  if (item.hidden) return false;
  if (item.feature === 'fan' && !fanFeatureEnabled.value) return false;
  if (!quickModeEnabled.value) return true;
  return item.path !== '/tdp' && item.path !== '/cpu';
}));

onMounted(async () => {
  const config = await loadPerformanceSchedule().catch(() => null);
  quickModeEnabled.value = config?.enabled === true;
  window.dispatchEvent(new CustomEvent('performance-schedule:visibility', { detail: { enabled: quickModeEnabled.value } }));
  if (quickModeEnabled.value && (route.path === '/tdp' || route.path === '/cpu')) {
    void router.replace('/schedule');
  }
  stopScheduleWatch = onPerformanceScheduleChanged((next) => {
    quickModeEnabled.value = next.enabled;
    window.dispatchEvent(new CustomEvent('performance-schedule:visibility', { detail: { enabled: next.enabled } }));
    if (next.enabled && (route.path === '/tdp' || route.path === '/cpu')) {
      void router.replace('/schedule');
    }
  });
});

onUnmounted(() => {
  stopScheduleWatch?.();
});

function go(path: string) {
  router.push(path);
}
async function openHome() {
  try {
    await shell.open('https://github.com/');
  } catch {
    /* ignore outside native shell */
  }
}
async function minimize() {
  try {
    await windowApi.minimize();
  } catch {
    /* ignore */
  }
}
async function quit() {
  // Start the normal lifecycle close, but never make the UI wait behind an
  // ACPI/HID call.  Native app exit immediately sends the authenticated
  // /api/parent-exit handoff; the Host's closeBoundaryClaimed gate serializes
  // whichever owner reaches CurrentDevice.Close first.  A failed renderer
  // close therefore cannot leave the Exit button dead or bypass recovery.
  void fanHostLifecycle.close().catch(() => {});
  try { await app.exit(0); } catch { /* native shell may already be exiting */ }
}
</script>

<template>
  <nav class="navrail">
    <div class="nav-brand app-region-drag">
      <AppIcon name="yeman" class="nb-bolt" />
      <div class="nb-titles">
        <span class="nb-title">YeManCC</span>
        <span class="nb-sub">野蛮系统控制中心</span>
      </div>
    </div>
    <div class="nav-items">
      <button
        v-for="r in visibleRoutes"
        :key="`${route.fullPath}:${r.path}`"
        class="nav-item"
        data-gp-ignore
        :class="{ active: route.path === r.path }"
        @click="go(r.path)"
      >
        <span class="nav-icon"><AppIcon :name="r.icon" :class="{ spinning: r.path === '/fan' && fanNavigationMotion }" :style="r.path === '/fan' ? { '--fan-spin-duration': fanNavigationSpinDuration } : undefined" /></span>
        <span class="nav-label">{{ r.title }}</span>
      </button>
    </div>
    <div class="nav-foot app-region-no-drag">
      <button class="nav-item nav-foot-btn" data-gp-ignore @click="minimize">
        <span class="nav-icon"><AppIcon name="minimize" /></span>
        <span class="nav-label">最小化</span>
      </button>
      <button class="nav-quit" data-gp-ignore @click="quit">退出</button>
    </div>
  </nav>
</template>

<style scoped>
.nav-brand {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 6px 3px 10px;
  margin-bottom: 6px;
  border-bottom: 1px solid #1c2533;
  min-width: 0;
  overflow: hidden;
}
.nb-bolt {
  flex: 0 0 auto;
  color: var(--accent-2);
  font-size: 16px;
  line-height: 1;
}
.nb-titles {
  display: flex;
  flex: 1 1 auto;
  min-width: 0;
  flex-direction: column;
  line-height: 1.15;
  white-space: nowrap;
}
.nb-title {
  font-size: 11.7px;
  font-weight: 700;
  letter-spacing: 0;
  color: var(--text);
  white-space: nowrap;
}
.nb-sub {
  font-size: 8px;
  font-weight: 500;
  letter-spacing: 0;
  color: var(--text-dim);
  white-space: nowrap;
}
.navrail {
  width: var(--nav-w);
  flex: 0 0 var(--nav-w);
  background: color-mix(in srgb, var(--bg-nav) 70%, transparent);
  position: relative;
  z-index: 1;
  border-right: 1px solid #1c2533;
  display: flex;
  flex-direction: column;
  padding: 8px 4px;
}
.nav-items {
  flex: 1 1 auto;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.nav-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  padding: 9px 2px;
  border: 1px solid transparent;
  border-radius: var(--radius-ctrl);
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
  white-space: nowrap;
}
.nav-item:hover {
  background: #131c28;
  color: var(--text);
}
.nav-item.active {
  background: #162434;
  border-color: rgba(46, 166, 255, 0.35);
  color: var(--accent);
}
.nav-item:focus-visible {
  box-shadow: var(--focus-ring);
}
.nav-icon {
  font-size: 17px;
  line-height: 1;
}
.nav-icon .spinning {
  animation: nav-fan-spin var(--fan-spin-duration, 1.1s) linear infinite;
}
@keyframes nav-fan-spin {
  to { transform: rotate(360deg); }
}
.nav-label {
  font-size: 10px;
  text-align: center;
  white-space: nowrap;
}
.nav-foot {
  flex: 0 0 auto;
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-top: 8px;
}
.nav-foot-btn {
  /* 与上方导航项完全一致：字体/气泡/图标，仅不显示 active 高亮 */
  width: 100%;
}
.nav-foot-btn:hover {
  background: #131c28;
  color: var(--text);
}
.nav-quit {
  font-size: 12px;
  font-weight: 600;
  color: #fff;
  background: var(--danger);
  border: none;
  border-radius: var(--radius-ctrl);
  padding: 8px;
  cursor: pointer;
}
.nav-quit:hover {
  filter: brightness(1.1);
}
.nav-quit:focus-visible {
  box-shadow: var(--focus-ring);
}
</style>
