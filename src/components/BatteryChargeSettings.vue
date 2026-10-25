<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import Dropdown from './Dropdown.vue';
import { getUiVisibilityState, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import InlineIcon from './InlineIcon.vue';
import { batteryCharge, batteryChargeOptions, batteryChargeStatus, type BatteryChargeState } from '@/bridge/batteryCharge';
const state = ref<BatteryChargeState | null>(null);
const selected = ref<number | string>('');
const busy = ref(false), loading = ref(false), dirty = ref(false), error = ref('');
const mayRunSoftware = ref(false);
let active = false, inPage = true, generation = 0;
let readFailures = 0, refreshQueued = false, powerPaused = false, powerGeneration = 0;
let stopVisibility: (() => void) | undefined;
const retryDelays = [1500, 5000, 15000];
const readEvents = ['focus', 'ipc:window.focus', 'ipc:power.sourceChanged', 'ipc:power.acChanged'];
const showEvents = ['ipc:window.shown', 'ipc:window.restored', 'ipc:window.maximized', 'ipc:window.summoned'];
const hideEvents = ['ipc:window.hidden', 'ipc:window.minimized'];
const pauseEvents = ['ipc:power.suspending', 'ipc:power.resuming'];
const resumeEvents = ['ipc:power.resume-ready', 'ipc:power.resumed'];
let timer: ReturnType<typeof setTimeout> | undefined;
const options = computed(() => {
  const items = state.value ? batteryChargeOptions(state.value) : [];
  return mayRunSoftware.value && !items.some(option => option.value === 100)
    ? [...items, { value: 100, label: '100%' }] : items;
});
const software = computed(() => state.value?.kind === 'software-bypass-threshold');
const canRecover = computed(() => state.value?.verification === 'unconfirmed' && !software.value && Boolean(state.value.choices.length));
const canSelect = computed(() => Boolean(state.value?.supported || canRecover.value || mayRunSoftware.value));
const canApply = computed(() => !busy.value && !loading.value && typeof selected.value === 'number' && selected.value >= 50 &&
  Boolean((mayRunSoftware.value && selected.value === 100) ||
    ((state.value?.supported || canRecover.value) && state.value?.choices.includes(selected.value))));
function accept(next: BatteryChargeState) {
  state.value = next;
  if (next.kind === 'software-bypass-threshold') mayRunSoftware.value = next.managed || (mayRunSoftware.value && !next.ok);
  else if (next.ok) mayRunSoftware.value = false;
  if (!dirty.value) selected.value = next.currentLimit ?? next.requestedLimit ?? '';
}
function stopTimer() { if (timer) clearTimeout(timer); timer = undefined; }
function scheduleRead() {
  stopTimer();
  if (!active || loading.value || busy.value) return;
  if (refreshQueued) { refreshQueued = false; void refresh(); return; }
  // Temporary failures get three bounded read-only attempts. Exhaustion stays
  // idle until a real foreground/power/route edge renews the budget.
  const delay = readFailures > 0 ? retryDelays[readFailures - 1] :
    (state.value?.supported || mayRunSoftware.value) ? 15000 : undefined;
  if (delay !== undefined) timer = setTimeout(() => { timer = undefined; void refresh(); }, delay);
}
function requestRead() {
  if (!active) return;
  readFailures = 0;
  if (loading.value || busy.value) { refreshQueued = true; return; }
  void refresh();
}
async function refresh() {
  if (!active || loading.value || busy.value) return;
  const epoch = generation;
  refreshQueued = false; loading.value = true; stopTimer();
  try {
    const next = await batteryCharge.get();
    if (active && generation === epoch) {
      accept(next); error.value = '';
      // Known unsupported hardware stays idle. An unknown transport/host or a
      // temporarily unavailable known route can recover without a route switch.
      readFailures = !next.ok && (next.kind !== 'unsupported' || !next.model) ? readFailures + 1 : 0;
    }
  } catch (e) {
    if (active && generation === epoch) {
      readFailures++;
      error.value = '当前读取失败';
      if (state.value) state.value = { ...state.value, supported: false, currentLimit: null };
    }
  } finally {
    loading.value = false;
    if (active && generation !== epoch) requestRead();
    else scheduleRead();
  }
}
async function apply() {
  if (!canApply.value) return;
  const epoch = generation, target = Number(selected.value);
  const release = mayRunSoftware.value && target === 100;
  let fence = state.value?.fence ?? '';
  busy.value = true; stopTimer(); error.value = '';
  // Keep recovery available even if navigation hides the page before the write reply.
  if (!release && software.value && target < 100) mayRunSoftware.value = true;
  try {
    // The explicit Apply also acknowledges an uncertain firmware result with
    // a fresh read. Background reads never clear recovery or replay a write.
    if (!release && canRecover.value) {
      const fresh = await batteryCharge.refresh();
      if (!active || generation !== epoch) return;
      accept(fresh);
      if (!fresh.ok || !fresh.supported || !fresh.choices.includes(target)) { error.value = '当前设置未确认'; return; }
      fence = fresh.fence;
    }
    const next = await (release ? batteryCharge.release() : batteryCharge.set(fence, target));
    if (active && generation === epoch) {
      dirty.value = !next.ok; accept(next);
      if (release && next.ok) mayRunSoftware.value = false;
      if (!next.ok) error.value = batteryChargeStatus(next);
    }
  } catch (e) {
    if (active && generation === epoch) {
      error.value = '当前设置未确认';
      if (software.value && target < 100) mayRunSoftware.value = true;
      if (state.value) state.value = { ...state.value, ok: false, supported: false, currentLimit: null, verification: 'unconfirmed' };
    }
  } finally {
    busy.value = false;
    if (active && generation !== epoch) void refresh();
    else scheduleRead();
  }
}
function activate() {
  const visibility = getUiVisibilityState();
  // BatteryHost owns availability. A separate fan/input recovery gate must not
  // leave a visible, independently readable charging card permanently stale.
  if (!inPage || !visibility.documentVisible || !visibility.nativeWindowVisible || powerPaused || active) return;
  active = true; generation++; requestRead();
}
function suspend() { active = false; generation++; refreshQueued = false; stopTimer(); }
function appeared() { if (!active) activate(); else if (!loading.value && !busy.value) requestRead(); }
function acceptPowerEvent(event: Event) {
  const value = (event as CustomEvent<{ generation?: number }>).detail?.generation;
  if (typeof value === 'number' && Number.isSafeInteger(value) && value > 0) {
    if (value < powerGeneration) return false;
    powerGeneration = value;
  }
  return true;
}
function powerPause(event: Event) { if (acceptPowerEvent(event)) { powerPaused = true; suspend(); } }
function powerResume(event: Event) { if (acceptPowerEvent(event)) { powerPaused = false; appeared(); } }
onMounted(() => {
  stopVisibility = onUiVisibilityChange(({ documentVisible, nativeWindowVisible }) => {
    if (documentVisible && nativeWindowVisible) activate(); else suspend();
  });
  for (const event of readEvents) window.addEventListener(event, requestRead);
  for (const event of showEvents) window.addEventListener(event, appeared);
  for (const event of hideEvents) window.addEventListener(event, suspend);
  for (const event of pauseEvents) window.addEventListener(event, powerPause);
  for (const event of resumeEvents) window.addEventListener(event, powerResume);
});
onActivated(() => { inPage = true; activate(); });
onDeactivated(() => { inPage = false; suspend(); });
onBeforeUnmount(() => {
  inPage = false; suspend(); stopVisibility?.(); stopVisibility = undefined;
  for (const event of readEvents) window.removeEventListener(event, requestRead);
  for (const event of showEvents) window.removeEventListener(event, appeared);
  for (const event of hideEvents) window.removeEventListener(event, suspend);
  for (const event of pauseEvents) window.removeEventListener(event, powerPause);
  for (const event of resumeEvents) window.removeEventListener(event, powerResume);
});
</script>
<template>
  <section class="card battery-charge-card" aria-label="充电上限" data-testid="battery-charge-settings">
    <h3 class="card-title"><InlineIcon name="battery" /> 充电上限</h3>
    <p class="description">部分厂家理解的[旁路供电]</p>
    <div class="charge-controls">
      <Dropdown v-if="options.length" :model-value="selected" :options="options" :disabled="busy || loading || !canSelect" aria-label="充电上限档位" placeholder="请选择充电上限" @update:model-value="selected = $event; dirty = true" />
      <span class="charge-status" role="status" aria-live="polite">{{ error || (state ? batteryChargeStatus(state) : '当前读取中…') }}</span>
      <button v-if="options.length" type="button" :disabled="!canApply" @click="apply()">{{ busy ? '正在应用…' : '应用' }}</button>
    </div>
  </section>
</template>
<style scoped>
.description, .charge-status { color: var(--text-dim); font-size: 12px; line-height: 1.6; }
.description { margin: 4px 0 0; }
.charge-status { flex-shrink: 0; }
.charge-controls { display: flex; gap: 12px; align-items: center; max-width: 440px; margin-top: 12px; }
.charge-controls > :first-child { flex: 1; min-width: 0; }
button { flex-shrink: 0; padding: 8px 15px; min-height: 36px; border: 1px solid color-mix(in srgb, var(--text-dim) 28%, transparent); border-radius: 6px; background: var(--bg-input); color: var(--text); cursor: pointer; }
button:disabled { opacity: .45; cursor: default; }
</style>
