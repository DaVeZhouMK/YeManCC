<script setup lang="ts">
import { ROUTES } from '@/router';
import { useRouter, useRoute } from 'vue-router';
import { inject, ref } from 'vue';
import { shell, windowApi } from '@/bridge/api';

const router = useRouter();
const route = useRoute();

// 全局刷新触发器
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');

function go(path: string) {
  router.push(path);
}
function refreshAll() {
  if (globalRefreshKey) globalRefreshKey.value++;
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
  try {
    const { app } = await import('@/bridge/api');
    await app.exit(0);
  } catch {
    /* ignore */
  }
}
</script>

<template>
  <nav class="navrail">
    <div class="nav-items">
      <button
        v-for="r in ROUTES"
        :key="r.path"
        class="nav-item"
        data-gp-ignore
        :class="{ active: route.path === r.path }"
        @click="go(r.path)"
      >
        <span class="nav-icon">{{ r.icon }}</span>
        <span class="nav-label">{{ r.title }}</span>
      </button>
    </div>
    <div class="nav-foot app-region-no-drag">
      <button class="nav-item nav-foot-btn" data-gp-ignore @click="refreshAll">
        <span class="nav-icon">🔄</span>
        <span class="nav-label">手动刷新</span>
      </button>
      <button class="nav-item nav-foot-btn" data-gp-ignore @click="minimize">
        <span class="nav-icon">➖</span>
        <span class="nav-label">最小化</span>
      </button>
      <button class="nav-quit" data-gp-ignore @click="quit">退出</button>
    </div>
  </nav>
</template>

<style scoped>
.navrail {
  width: var(--nav-w);
  flex: 0 0 var(--nav-w);
  background: #0c111a;
  border-right: 1px solid #1c2533;
  display: flex;
  flex-direction: column;
  padding: 8px 6px;
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
  gap: 4px;
  padding: 10px 4px;
  border: 1px solid transparent;
  border-radius: var(--radius-ctrl);
  background: transparent;
  color: var(--text-dim);
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
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
  font-size: 18px;
  line-height: 1;
}
.nav-label {
  font-size: 11px;
  text-align: center;
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
