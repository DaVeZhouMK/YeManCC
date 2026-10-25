<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, shallowRef } from 'vue';
import { useRoute } from 'vue-router';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';
import MonitorHudTemplate from '@/components/MonitorHudTemplate.vue';
import { boundedHudData, hudFromUniformSnapshot, normalizeMonitorOutput } from '@/bridge/monitorHudModel';
import { HUD_PREVIEW_MAX_MS, type HudPreviewPacket } from '@/bridge/monitorHudPreview';
import { createHudAgeLease } from '@/bridge/monitorHudFreshness';
const route = useRoute(), prefs = ref(normalizeMonitorOutput()), data = shallowRef(hudFromUniformSnapshot(null));
const connected = ref(false), status = ref('等待主窗口提供 YMCC 统一快照…');
const token = typeof route.query.channel === 'string' ? route.query.channel : '';
const childId = Number(route.query.__ymccChild);
let channel: BroadcastChannel | null = null, deadline: number | null = null, seq = 0;
const nativeChild = isNativeRuntime && Number.isSafeInteger(childId) && childId > 0 && /^[a-zA-Z0-9-]{16,80}$/.test(token);
const displayData = computed(() => data.value);
const ageLease = createHudAgeLease({ now: () => Date.now(), emit: value => { data.value = value; status.value = value.stale ? '统一快照已过期，暂停保留旧读数' : '来自 YMCC 统一快照 · 缺失字段为 —'; },
  setTimer: (fn, ms) => window.setTimeout(fn, ms), clearTimer: id => window.clearTimeout(id as number) });
async function closePreview() { if (nativeChild) await windowApi.closeChild(childId).catch(() => false); }
function receive(raw: HudPreviewPacket) {
  if (raw?.token !== token || !Number.isSafeInteger(raw.seq) || raw.seq <= seq) return;
  if (raw.kind === 'ymcc-hud-preview-appearance') {
    seq = raw.seq; prefs.value = normalizeMonitorOutput(raw.preferences);
    return; // Appearance never refreshes data/freshness or starts collection.
  }
  if (raw.kind !== 'ymcc-hud-preview' || raw.data?.source !== 'ymcc-uniform' || !Number.isFinite(raw.expiresAt)) return;
  seq = raw.seq;
  ageLease.accept(boundedHudData(raw.data, raw.data.stale === true || raw.expiresAt <= Date.now()));
  // The shared age lease alone owns sensor expiry; a whole-packet timeout
  // would bypass valid per-family receipts during a brief producer gap.
  connected.value = true;
}
onMounted(() => {
  if (!nativeChild) { status.value = '不是 YMCC 原生子窗口：不启动假窗口或示例采集。'; return; }
  channel = new BroadcastChannel('ymcc-hud-preview-' + token);
  channel.onmessage = event => receive(event.data);
  channel.postMessage({ kind: 'ymcc-hud-preview-ready', token });
  deadline = window.setTimeout(() => { void closePreview(); }, HUD_PREVIEW_MAX_MS);
});
onUnmounted(() => { ageLease.dispose(); if (deadline !== null) window.clearTimeout(deadline); channel?.close(); });
</script>
<template>
  <div class="native-hud-preview">
    <header><div><strong>YMCC 真实窗口预览</strong><p>桌面子窗口 · 不代表 Steam 游戏叠加层</p></div><button type="button" :disabled="!nativeChild" @click="closePreview">关闭预览</button></header>
    <p class="native-preview-status" role="status">{{ status }}</p>
    <main><MonitorHudTemplate v-if="nativeChild && prefs.enabled" :appearance="prefs.ymcc" :fields="prefs.ymcc.fields" :data="displayData" /><p v-else-if="nativeChild">监控显示已关闭</p><p v-else>请从 YMCC 监控页打开此窗口。</p></main>
    <footer>{{ connected ? '真实数据，不使用fixture' : '无数据时显示 —' }} · 5 分钟自动关闭 · 关闭不会退出 YMCC / RTSS</footer>
  </div>
</template>
<style scoped>
.native-hud-preview { height:100%;box-sizing:border-box;padding:18px;overflow:auto;background:var(--bg-solid);color:var(--text);font-family:'Segoe UI','Microsoft YaHei UI',sans-serif; }.native-hud-preview header { display:flex;justify-content:space-between;align-items:flex-start;gap:12px; }.native-hud-preview strong { font-size:14px;font-weight:600; }.native-hud-preview header p,.native-preview-status,.native-hud-preview footer { font-size:11px;color:var(--text-dim);line-height:1.7; }.native-hud-preview header p { margin:5px 0; }.native-hud-preview button { padding:7px 10px;background:var(--bg-input);border:1px solid rgba(139,156,178,.22);border-radius:8px;color:var(--text); }.native-hud-preview main { min-height:360px;margin:16px 0;padding:12px;border-radius:12px;background:linear-gradient(150deg,var(--bg-input),var(--bg-solid)); }.native-hud-preview main>p { font-size:12px; }
</style>