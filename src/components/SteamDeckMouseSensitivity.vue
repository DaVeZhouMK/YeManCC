<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import Slider from '@/components/Slider.vue';
import { on } from '@/bridge/ipc';
import { STEAM_DECK_MOUSE_DEFAULT, steamDeckMouseGet, steamDeckMouseSet, steamDeckMouseMessage, type SteamDeckMouseState } from '@/bridge/steamDeckMouse';

const props = withDefaults(defineProps<{ disabled?: boolean }>(), { disabled: false });
const state = ref<SteamDeckMouseState | null>(null);
const value = ref(STEAM_DECK_MOUSE_DEFAULT);
const loading = ref(true);
const saving = ref(false);
const failure = ref('');
let generation = 0;
let debounce: ReturnType<typeof window.setTimeout> | undefined;
let queuedPercent: number | undefined;
let disposed = false;
let stopUpdated: (() => void) | undefined;
const hint = computed(() => {
  if (failure.value) return failure.value;
  if (loading.value) return '正在读取 Steam 桌面布局…';
  if (state.value?.waitingAccount) return '待办已保留，等待原 Steam 账号连接。';
  if (!state.value?.available) return steamDeckMouseMessage(state.value?.reason);
  if (state.value.error) return steamDeckMouseMessage(state.value.error);
  if (state.value.pending) return `已排队${state.value.desiredPercent ?? state.value.percent}% 重新启动Steam后应用`;
  const outside = state.value.percent > 300 ? '；当前 Steam 值超出滑块范围，不会自动改写。' : '';
  if (state.value.steamRunning && state.value.liveAvailable) return `桌面布局 · 右摇杆（${state.value.percent}%）${saving.value ? '；正在实时应用…' : state.value.appliedLive ? '；已通过 Steam 实时应用并保存。' : '；可实时调节，无需重启 Steam。'}${outside}`;
  if (state.value.steamRunning) return `桌面布局 · 右摇杆（文件值 ${state.value.percent}%）；${steamDeckMouseMessage(state.value.liveReason)}${outside}`;
  return `桌面布局 · 右摇杆（文件值 ${state.value.percent}%）；Steam 未运行，保存后下次启动加载。${outside}`;
});
function accept(next: SteamDeckMouseState): void {
  state.value = next;
  if (!next.error) failure.value = ''; // A recovered native receipt replaces a stale local failure.
  if (queuedPercent === undefined && debounce === undefined) value.value = next.pending ? next.desiredPercent ?? next.percent : next.percent;
}
async function refresh(): Promise<void> {
  const ticket = ++generation;
  loading.value = true;
  try {
    const next = await steamDeckMouseGet();
    if (!disposed && ticket === generation && !saving.value) { accept(next); failure.value = ''; }
  } catch (error) {
    if (!disposed && ticket === generation) { state.value = null; failure.value = `无法读取 Steam 桌面布局：${error instanceof Error ? error.message : String(error)}`; }
  } finally { if (!disposed && ticket === generation) loading.value = false; }
}
async function commit(percent: number): Promise<void> {
  if (debounce !== undefined) { window.clearTimeout(debounce); debounce = undefined; }
  if (disposed || props.disabled || loading.value || !state.value?.available) return;
  if (saving.value) { queuedPercent = percent; return; }
  queuedPercent = undefined;
  ++generation; // Do not let an older get overwrite this save receipt.
  saving.value = true;
  failure.value = '';
  try {
    const next = await steamDeckMouseSet(percent);
    if (disposed) return;
    if (!next.ok) {
      if (next.available) state.value = next; // Preserve actual file readback even on an uncertain/live-save error.
      failure.value = steamDeckMouseMessage(next.reason || next.error);
      if (queuedPercent === undefined) value.value = state.value.pending ? state.value.desiredPercent ?? state.value.percent : state.value.percent;
      return;
    }
    accept(next);
  } catch (error) {
    if (!disposed) {
      failure.value = `灵敏度保存失败：${error instanceof Error ? error.message : String(error)}`;
      if (queuedPercent === undefined) value.value = state.value?.pending ? state.value.desiredPercent ?? state.value.percent : state.value?.percent ?? STEAM_DECK_MOUSE_DEFAULT;
    }
  } finally {
    if (!disposed) {
      saving.value = false;
      const next = queuedPercent;
      queuedPercent = undefined;
      if (next !== undefined && next !== percent && !props.disabled) void commit(next);
    }
  }
}
function draft(percent: number): void {
  value.value = percent;
  if (debounce !== undefined) window.clearTimeout(debounce);
  if (props.disabled || loading.value || disposed || !state.value?.available) return;
  debounce = window.setTimeout(() => { debounce = undefined; void commit(percent); }, 180);
}
watch(() => props.disabled, (disabled) => {
  if (disabled) { if (debounce !== undefined) window.clearTimeout(debounce); debounce = undefined; queuedPercent = undefined; }
});
onMounted(() => {
  stopUpdated = on<SteamDeckMouseState>('steamDeckMouse.updated', (next) => { if (!disposed && !saving.value && debounce === undefined && queuedPercent === undefined) { ++generation; loading.value = false; accept(next); } });
  void refresh();
});
onActivated(() => { if (!saving.value) void refresh(); });
onBeforeUnmount(() => { disposed = true; ++generation; if (debounce !== undefined) window.clearTimeout(debounce); queuedPercent = undefined; stopUpdated?.(); });
</script>

<template>
  <div class="steamdeck-mouse-bubble">
    <Slider :model-value="value" @update:model-value="draft" label="SteamDeck鼠标灵敏度" unit="%" :min="1" :max="300" :step="5" :step-base="0" :accelerate="false"
      :disabled="disabled || loading || !state?.available" @commit="commit" />
    <p class="sensitivity-hint" role="status" aria-live="polite">{{ hint }}</p>
  </div>
</template>

<style scoped>
.steamdeck-mouse-bubble{margin-top:10px;padding:10px;border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent)}
.sensitivity-hint{margin:6px 0 0;color:var(--text-dim);font-size:10px;line-height:1.5;overflow-wrap:anywhere}
</style>
