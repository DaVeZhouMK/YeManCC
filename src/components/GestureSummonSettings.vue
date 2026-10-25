<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import Toggle from './Toggle.vue';
import { invoke, on } from '@/bridge/ipc';
import { getUiSetting, loadUiSettings, setUiSettings } from '@/bridge/uiSettings';

const props=withDefaults(defineProps<{compact?:boolean}>(),{compact:false});
const enabled = ref(getUiSetting('gestureSummonEnabled'));
const side = ref(getUiSetting('windowPlacement'));
const busy = ref(false);
const error = ref('');
const unavailable = ref(false);
const direction = computed(() => side.value === 'left' ? '左侧边缘向右' : '右侧边缘向左');
let subscribed = false;
let stopStatus: (() => void) | undefined;
let statusGeneration = 0;
function sync(): void {
  enabled.value = getUiSetting('gestureSummonEnabled');
  side.value = getUiSetting('windowPlacement');
  if (!enabled.value) unavailable.value = false;
}
type GestureStatus = { enabled?: boolean; available?: boolean };
function applyStatus(status: GestureStatus): void {
  unavailable.value = enabled.value && status.enabled === true && status.available === false;
}
async function refreshStatus(): Promise<void> {
  const generation = ++statusGeneration;
  try {
    const status = await invoke<GestureStatus>('window.gestureSummonStatus', {});
    if (subscribed && generation === statusGeneration) applyStatus(status);
  } catch { /* A browser preview or older host has no native status. */ }
}
function subscribe(): void {
  if (subscribed) return;
  subscribed = true;
  window.addEventListener('ui-settings:loaded', sync);
  window.addEventListener('ui-settings:changed', sync);
  stopStatus = on<GestureStatus>('window.gestureSummonStatus', status => { ++statusGeneration; applyStatus(status); });
  sync();
  void refreshStatus();
}
function unsubscribe(): void {
  subscribed = false;
  ++statusGeneration;
  window.removeEventListener('ui-settings:loaded', sync);
  window.removeEventListener('ui-settings:changed', sync);
  stopStatus?.();
  stopStatus = undefined;
}
async function change(value: boolean): Promise<void> {
  if (busy.value) return;
  busy.value = true;
  error.value = '';
  try {
    await setUiSettings({ gestureSummonEnabled: value });
    sync();
    await refreshStatus();
  } catch (cause) {
    sync();
    error.value = '手势呼出设置保存失败：' + (cause instanceof Error ? cause.message : String(cause));
  } finally { busy.value = false; }
}
onMounted(async () => {
  subscribe();
  try { await loadUiSettings(); sync(); } catch (cause) { error.value = '手势呼出设置读取失败：' + String(cause); }
});
onActivated(subscribe);
onDeactivated(unsubscribe);
onBeforeUnmount(unsubscribe);
</script>

<template>
  <div class="gesture-summon-settings" :class="{compact:props.compact}">
    <button v-if="props.compact" type="button" class="gesture-compact-button" :class="{active:enabled}" :aria-pressed="enabled" :disabled="busy" @click="change(!enabled)">手势呼出 <span>{{enabled?'开启':'关闭'}}</span></button>
    <template v-else>
    <div class="card-header-row">
      <h4 class="gesture-title">手势呼出</h4>
      <Toggle :model-value="enabled" aria-label="手势呼出" compact color="accent" :disabled="busy" @update:model-value="change" />
    </div>
    <p class="muted body">开启后，从触摸屏{{ direction }}滑动（从外向内）即可呼出 YMCC，默认关闭。</p>
    <p class="muted body">仅使用所选边缘居中 80% 的区域，上下各留空 10%；单指向内滑动一小段后松手。</p>
    <p class="muted body">另一侧留给监控呼出；底部上滑继续使用 Windows 或 Start11 开始菜单。</p>
    <p class="muted body">保留 Windows 侧边手势，YMCC 不屏蔽系统手势，空闲时不持续扫描。</p>
    </template>
    <p v-if="unavailable" class="gesture-status" role="status">当前系统无法启用边缘触摸区域，请关闭后重试。</p>
    <p v-if="error" class="gesture-error" role="alert">{{ error }}</p>
  </div>
</template>

<style scoped>
.gesture-summon-settings { margin-top: 14px; padding-top: 14px; border-top: 1px solid rgba(255, 255, 255, 0.08); }
.card-header-row { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
.gesture-title { margin: 0; font-size: 12px; font-weight: 600; }
.gesture-status, .gesture-error { margin: 8px 0 0; font-size: 13px; line-height: 1.5; }
.gesture-status { color: var(--text-dim); }
.gesture-error { color: var(--danger); }
.gesture-summon-settings.compact{margin:0;padding:0;border:0;min-width:0}.gesture-compact-button{display:flex;align-items:center;justify-content:center;gap:5px;width:100%;min-height:40px;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt);color:var(--text);font:inherit;font-size:13px;font-weight:650;cursor:pointer;white-space:nowrap}.gesture-compact-button.active{background:var(--accent-surface);border-color:var(--accent-border);color:var(--accent)}.gesture-compact-button span{font-size:11px;color:var(--text-dim)}.gesture-compact-button.active span{color:var(--accent)}.gesture-compact-button:disabled{opacity:.5}.compact :is(.gesture-status,.gesture-error){font-size:12px;margin:5px 0 0}
</style>
