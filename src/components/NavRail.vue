<script setup lang="ts">
import { ROUTES } from '@/router';
import { radialTestingEnabled } from '@/radial/testingState';
import { useRouter, useRoute } from 'vue-router';
import { computed } from 'vue';
import {mouseModeState} from '@/bridge/mouseModeState';
import {manualSidebarMouse} from '@/bridge/mouseModePresentation';
const manualMouse = computed(()=>manualSidebarMouse(mouseModeState.value));
import { app, shell, windowApi } from '@/bridge/api';
import AppIcon from '@/components/AppIcon.vue';
import { fanHostLifecycle } from '@/bridge/fanHost';
import { residentNavigationRoutes } from '@/bridge/residentNavigation';
import {
  fanNavigationMotion,
  fanNavigationSpinDuration,
} from '@/bridge/fanFeature';

const router = useRouter();
const route = useRoute();
// Retired pages stay absent in every performance mode and device state.
const visibleRoutes = computed(() => residentNavigationRoutes(ROUTES, false, radialTestingEnabled.value));

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
        :key="r.path"
        class="nav-item"
        data-gp-ignore
        :class="{ active: route.path === r.path }"
        @click="go(r.path)"
      >
        <span class="nav-icon"><AppIcon :key="r.path + (r.path === '/button-mapping' && manualMouse ? ':mouse' : ':default')" :name="r.path === '/button-mapping' && manualMouse ? 'mouse' : r.icon" :stroke-width="['gauge', 'gamepad', 'rotate'].includes(r.icon) ? 1.2 : undefined" :class="{ spinning: r.path === '/fan' && fanNavigationMotion }" :style="r.path === '/fan' ? { '--fan-spin-duration': fanNavigationSpinDuration } : undefined" /></span>
        <span class="nav-label">{{ r.path === '/button-mapping' && manualMouse ? '模拟鼠标' : r.title }}</span>
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
  flex-shrink: 0;
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
  background: color-mix(in srgb, var(--text) 6%, var(--bg-input));
  color: var(--text);
}
.nav-item.active {
  background: var(--accent-surface);
  border-color: var(--accent-border);
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
  background: color-mix(in srgb, var(--text) 6%, var(--bg-input));
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

/* Keep navigation ownership unchanged; only the selected item emits light. */
.nav-items>.nav-item{position:relative;isolation:isolate;border-radius:0}
.nav-items>.nav-item.active{border-color:transparent;border-radius:0;background-color:color-mix(in srgb,var(--accent) 22%,var(--bg-panel));background-image:linear-gradient(100deg,color-mix(in srgb,var(--accent) 32%,transparent),color-mix(in srgb,var(--accent) 8%,transparent) 65%,transparent);color:var(--text);box-shadow:inset 3px 0 var(--accent),inset 0 1px 0 #b9e8ff1f,0 0 14px color-mix(in srgb,var(--accent) 10%,transparent)}
.nav-items>.nav-item.active::before{content:"";position:absolute;left:0;top:2px;bottom:2px;width:3px;border-radius:2px;background:linear-gradient(180deg,color-mix(in srgb,var(--accent) 35%,transparent),color-mix(in srgb,var(--accent) 45%,white) 42%,var(--accent) 78%,color-mix(in srgb,var(--accent) 40%,transparent));box-shadow:0 0 7px color-mix(in srgb,var(--accent) 60%,transparent),0 0 2px #d2f3ff80;pointer-events:none;opacity:.82}
.nav-items>.nav-item.active .nav-icon,.nav-items>.nav-item.active .nav-label{color:var(--text)}
.nav-items>.nav-item.active:focus-visible{outline:2px solid var(--accent);outline-offset:-2px}



</style>
