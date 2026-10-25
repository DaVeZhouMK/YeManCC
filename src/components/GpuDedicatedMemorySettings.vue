<script setup lang="ts">
import { computed, ref, onMounted, onActivated, onDeactivated, onBeforeUnmount } from 'vue';
import Dropdown from './Dropdown.vue';
import { gpuDedicatedMemory, parseDedicatedMemorySnapshot, formatDedicatedCapacity, dedicatedOptionLabel,
  dedicatedMemoryStatus, type DedicatedMemorySnapshot } from '@/bridge/gpuDedicatedMemory';
const snapshot = ref<DedicatedMemorySnapshot | null>(null);
const draft = ref(''), busy = ref(false), error = ref(''), clock = ref(Date.now());
let inPage = true, active = false, generation = 0, receivedAt = 0;
let pendingRead: object | null = null;
let timer: ReturnType<typeof setInterval> | undefined;
const visible = computed(() => !!snapshot.value && clock.value - receivedAt >= 0 && clock.value - receivedAt <= 30000);
const options = computed(() => (snapshot.value?.options ?? []).map(o => ({ value: o.id, label: dedicatedOptionLabel(o) })));
const dirty = computed(() => !!snapshot.value && draft.value !== snapshot.value.selected);
const status = computed(() => snapshot.value ? dedicatedMemoryStatus(snapshot.value) : '');
function clearTimer() { if (timer !== undefined) clearInterval(timer); timer = undefined; }
function accept(raw: unknown, fromWrite = false) {
  const next = parseDedicatedMemorySnapshot(raw, !fromWrite);
  const previous = snapshot.value;
  const keepDraft = !fromWrite && previous && draft.value !== previous.selected && next?.pnpId === previous.pnpId &&
    next.driverVersion === previous.driverVersion && next.provider === previous.provider && next.luid === previous.luid &&
    next.options.some(o => o.id === draft.value);
  snapshot.value = next;
  receivedAt = Date.now(); clock.value = receivedAt;
  if (!next) { clearTimer(); draft.value = ''; return; }
  if (!keepDraft) draft.value = next.selected;
  if (active && timer === undefined) timer = setInterval(() => { clock.value = Date.now(); void refresh(); }, 15000);
}
async function refresh() {
  if (!active || busy.value || pendingRead) return;
  const ticket = {}, epoch = generation; pendingRead = ticket;
  try {
    const raw = await gpuDedicatedMemory.read();
    if (active && epoch === generation && pendingRead === ticket) accept(raw);
  } catch {
    if (active && epoch === generation && pendingRead === ticket) accept(null);
  } finally { if (pendingRead === ticket) pendingRead = null; }
}
function readCurrent() { error.value = ''; void refresh(); }
function select(raw: string | number) { draft.value = String(raw); error.value = ''; }
async function save() {
  const current = snapshot.value;
  if (!active || !visible.value || !current || !dirty.value || busy.value || Date.now() - receivedAt < 0 || Date.now() - receivedAt > 30000) return;
  busy.value = true; error.value = '';
  const epoch = ++generation; pendingRead = null;
  try {
    const raw = await gpuDedicatedMemory.save(current.fence, draft.value);
    if (active && epoch === generation) {
      accept(raw, true);
      if (raw && typeof raw === 'object' && 'ok' in raw && raw.ok !== true)
        error.value = '保存未确认，请读取';
    }
  } catch {
    if (active && epoch === generation) {
      snapshot.value = null; clearTimer();
      error.value = '保存未确认';
    }
  } finally {
    busy.value = false;
    if (active) void refresh();
  }
}
function activate() {
  if (!inPage || document.hidden || active) return;
  active = true; ++generation; void refresh();
}
function suspend() {
  active = false; ++generation; pendingRead = null; snapshot.value = null; clearTimer();
}
function visibilityChanged() { if (document.hidden) suspend(); else activate(); }
function focusChanged() { if (active) void refresh(); }
onMounted(() => { activate(); document.addEventListener('visibilitychange', visibilityChanged); window.addEventListener('focus', focusChanged); });
onActivated(() => { inPage = true; activate(); });
onDeactivated(() => { inPage = false; suspend(); });
onBeforeUnmount(() => {
  inPage = false; suspend(); document.removeEventListener('visibilitychange', visibilityChanged); window.removeEventListener('focus', focusChanged);
});
</script>
<template>
  <section v-if="visible && snapshot" class="card dedicated-memory" aria-label="专用显存设置">
    <h3 class="card-title">专用显存</h3>
    <div class="memory-current">
      <span>当前</span><strong data-testid="dedicated-current">{{ formatDedicatedCapacity(snapshot.dedicatedBytes) }}</strong>
      <button type="button" :disabled="busy" @click="readCurrent">读取</button>
    </div>
    <div class="memory-edit">
      <Dropdown :model-value="draft" :options="options" :disabled="busy" width="140px" aria-label="专用显存容量" @update:model-value="select" />
      <button type="button" :disabled="busy || !dirty" @click="save">{{ busy ? '保存中' : '保存' }}</button>
    </div>
    <p v-if="status" class="memory-status" role="status">{{ status }}</p>
    <p v-if="error" class="memory-error" role="alert">{{ error }}</p>
  </section>
</template>
<style scoped>
.dedicated-memory { display: flex; flex-direction: column; gap: 12px; }
.card-title { margin: 0; font-size: 14px; font-weight: 600; }
.memory-current, .memory-edit { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; }
.memory-current strong { color: var(--accent); font-size: 20px; }
.memory-status, .memory-error { margin: 0; font-size: 13px; line-height: 1.6; }
.memory-error { color: var(--danger); }
button { padding: 6px 12px; border: 1px solid color-mix(in srgb, var(--text-dim) 28%, transparent); border-radius: 6px; background: var(--bg-input); color: var(--text); cursor: pointer; }
button:disabled { opacity: .45; cursor: default; }
</style>
