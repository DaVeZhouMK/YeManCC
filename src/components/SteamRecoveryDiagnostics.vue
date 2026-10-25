<script setup lang="ts">
import { computed, inject, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, watch, type Ref } from 'vue';
import InlineIcon from '@/components/InlineIcon.vue';
import SleepRecoveryPanel from '@/components/SleepRecoveryPanel.vue';
import { getSteamGameLink, type SteamGameLinkSnapshot } from '@/bridge/steamGameLink';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';

const snapshot = ref<SteamGameLinkSnapshot | null>(null);
const busy = ref(false);
const error = ref('');
const fresh = computed(() => snapshot.value?.fresh === true);
const states = computed(() => [
  { label: 'Steam 传输', connected: snapshot.value?.transportConnected, text: '已连通' },
  { label: '原实例联合', connected: snapshot.value?.linked, text: '已联合确认' },
  { label: '原游戏身份', connected: snapshot.value?.contactConnected, text: '同一原实例已联系' },
]);
const note = computed(() => {
  if (error.value) return error.value;
  if (!snapshot.value) return busy.value ? '正在读取 Steam 联系状态…' : '尚未读取 Steam 联系状态。';
  if (!fresh.value) return '当前联系状态待复核，旧联系不会继续沿用。';
  return '联系状态不代表游戏内手柄、Overlay 或大屏“继续”已经恢复。';
});
let active = false;
let revision = 0;
let stopVisibility: (() => void) | undefined;
const readEvents = ['ipc:power.resume-ready', 'ipc:power.resumed'];
async function refresh(): Promise<void> {
  if (!active || busy.value) return;
  const turn = ++revision;
  busy.value = true;
  error.value = '';
  try {
    const value = await getSteamGameLink();
    if (active && turn === revision) snapshot.value = value;
  } catch {
    if (active && turn === revision) {
      snapshot.value = null;
      error.value = 'Steam 联系状态读取失败，请重新检查。';
    }
  } finally {
    if (active && turn === revision) busy.value = false;
  }
}
function refreshIfVisible(): void {
  if (active && isUiVisible()) void refresh();
}
function activate(): void {
  if (active) return;
  active = true;
  for (const event of readEvents) window.addEventListener(event, refreshIfVisible);
  document.addEventListener('visibilitychange', refreshIfVisible);
  stopVisibility = onUiVisibilityChange(({ visible }) => { if (visible) refreshIfVisible(); });
  void refresh(); // Read-only. Entering diagnostics never starts an inspection or repair.
}
function deactivate(): void {
  active = false;
  ++revision;
  busy.value = false;
  for (const event of readEvents) window.removeEventListener(event, refreshIfVisible);
  document.removeEventListener('visibilitychange', refreshIfVisible);
  stopVisibility?.(); stopVisibility = undefined;
}
const globalRefreshKey = inject<Ref<number>>('globalRefreshKey');
if (globalRefreshKey) watch(globalRefreshKey, refreshIfVisible);
onMounted(activate); onActivated(activate); onDeactivated(deactivate); onBeforeUnmount(deactivate);
</script>

<template>
  <section class="steam-recovery-diagnostics" aria-label="非 Steam 游戏修复">
    <div class="recovery-heading">
      <h3><InlineIcon name="tools" />非 Steam 游戏修复</h3>
      <button type="button" class="recovery-refresh" :disabled="busy" @click="refresh">
        <InlineIcon name="refresh" />{{ busy ? '读取中…' : '检查状态' }}
      </button>
    </div>
    <SleepRecoveryPanel />
    <div class="recovery-link-states" aria-label="Steam 两端联系（只读）">
      <div v-for="state in states" :key="state.label" class="recovery-link-state">
        <span>{{ state.label }}</span>
        <strong :class="{ connected: fresh && state.connected }">{{ !snapshot ? '未读取' : !fresh ? '待复核' : state.connected ? state.text : '未确认' }}</strong>
      </div>
    </div>
    <p class="recovery-note" :class="{ error }" role="status" aria-live="polite">{{ note }}</p>
  </section>
</template>

<style scoped>
.steam-recovery-diagnostics { min-width:0; padding-bottom:14px; margin-bottom:14px; border-bottom:1px solid var(--console-line); }
.recovery-heading { display:flex; align-items:center; justify-content:space-between; gap:8px; margin-bottom:10px; }
.recovery-heading h3 { display:flex; align-items:center; gap:6px; margin:0; font-size:16px; line-height:1.4; color:var(--text); }
.recovery-heading h3 svg { width:18px; height:18px; color:var(--accent); }
.recovery-refresh { display:flex; align-items:center; justify-content:center; gap:5px; flex-shrink:0; min-height:34px; padding:5px 9px; border:1px solid var(--console-line); border-radius:8px; background:var(--console-surface-alt); color:var(--text); font:inherit; font-size:13px; cursor:pointer; }
.recovery-refresh svg { width:15px; height:15px; }
.recovery-refresh:disabled { opacity:.5; cursor:default; }
.recovery-refresh:focus-visible,.recovery-refresh.focused { outline:2px solid var(--accent); outline-offset:2px; }
.recovery-link-states { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:7px; margin-top:10px; }
.recovery-link-state { min-width:0; padding:8px; border:1px solid var(--console-line); border-radius:8px; background:var(--console-surface-alt); }
.recovery-link-state span,.recovery-link-state strong { display:block; font-size:12px; line-height:1.5; overflow-wrap:anywhere; }
.recovery-link-state span { color:var(--text-dim); }
.recovery-link-state strong { margin-top:3px; color:var(--text); font-weight:600; }
.recovery-link-state strong.connected { color:var(--ok); }
.recovery-note { margin:8px 0 0; color:var(--text-dim); font-size:12px; line-height:1.5; }
.recovery-note.error { color:var(--danger,#e95564); }
</style>
