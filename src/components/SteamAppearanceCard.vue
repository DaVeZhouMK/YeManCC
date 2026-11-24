<script setup lang="ts">
import SwitchControl from './SwitchControl.vue';
import { computed, nextTick, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, shallowRef, watch } from 'vue';
import SteamAppearanceEditor from './SteamAppearanceEditor.vue';
import { STEAM_APPEARANCE_THEMES, STEAM_APPEARANCE_DEFAULT_ENABLED, type AppearanceThemeName, type AppearanceSnapshot } from '@/bridge/steamAppearance';
import { steamAppearanceDraft } from '@/bridge/steamAppearanceDraft';
import { createSteamReadBudget } from '@/bridge/steamReadBudget';
import type { SteamUiState } from '@/bridge/steamUi';
import { getUiVisibilityState, isUiPowerReady, isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { onSharedPowerLifecycle } from '@/bridge/powerLifecycleState';
import { focusGamepadElement } from '@/gamepad/focus';
const props = defineProps<{ steamState: SteamUiState | null; steamStateStale: boolean; sectionVisible: boolean }>();
// The draft owner publishes a stable confirmed snapshot and separate pending choices.
const draft = shallowRef(steamAppearanceDraft.getViewState());
const snapshot = computed(() => draft.value.snapshot);
// Slow writes never disable switches. Only an explicitly confirmed preset reset locks them.
const busy = computed(() => draft.value.locked), reading = ref(false), editorOpen = ref(false);
const observationBlocked = computed(() => busy.value || draft.value.saving || draft.value.pendingCount > 0);
const notice = computed(() => draft.value.message), editButton = ref<HTMLButtonElement | null>(null);
const appearanceReadBudget = createSteamReadBudget();
let appearanceReadTimer: ReturnType<typeof setTimeout> | null = null;
type GuardProgress = 'resume' | 'account-recheck' | 'manual-entry';
let appearanceReadQueued: 'normal' | 'power-return' | GuardProgress | null = null;
const GUARD_OBSERVATION_MAX_MS = 4500;
let deferredObservationUntil = 0;
let earlyResume: { generation: number; until: number } | null = null;
let observationEpoch = 0;
let backendCompletionEpoch = 0;
type ActiveRead = { completion: number; dispatched: boolean; deferredCompletion: boolean };
let activeRead: ActiveRead | undefined;
let stopVisibility: (() => void) | null = null;
let stopPower: (() => void) | null = null;
type StatusSession = Pick<AppearanceSnapshot, 'steamRunning' | 'steamPid' | 'steamBirth'>;
let observedSession: (StatusSession & { afterSteamEpoch: number }) | undefined;
let lastKnownSteam: SteamUiState | null = null;
let steamEpoch = 0, readSteamEpoch = 0;
let steamWhileReading: SteamUiState | null = null;
const hasSteamIdentity = (state: SteamUiState) => state.pid > 0 && state.birth !== '0';
function coveredByStatus(state: SteamUiState) {
  return observedSession?.steamRunning === state.running &&
    (!state.running || hasSteamIdentity(state) && state.pid === observedSession.steamPid && state.birth === observedSession.steamBirth);
}
function matchesDisplayedSteam(state: SteamUiState | null) {
  const displayed = draft.value.snapshot;
  return state?.known && state.running && hasSteamIdentity(state) &&
    displayed?.steamRunning && displayed.steamPid === state.pid && displayed.steamBirth === state.birth;
}
function matchesDisplayedBigPicture(state: SteamUiState | null) {
  return state?.mode === 'bigpicture' && matchesDisplayedSteam(state);
}
watch(() => props.steamState, state => {
  if ((earlyResume || appearanceReadQueued === 'resume') && !matchesDisplayedBigPicture(state)) {
    earlyResume = null;
    if (appearanceReadQueued === 'resume') queueObservation();
  }
  if (appearanceReadQueued === 'manual-entry' && !matchesDisplayedSteam(state)) queueObservation();
  if (!state?.known) return;
  const previous = lastKnownSteam;
  if (previous?.running && state.running && !hasSteamIdentity(state)) return;
  lastKnownSteam = state;
  if (!previous) {
    ++steamEpoch;
    // A first observation is not an edge. Let this card's in-flight Status
    // answer it, rather than adding a trailing read for mount order alone.
    if (reading.value) steamWhileReading = state;
    else if (!coveredByStatus(state)) queueObservation();
  } else if (previous.running !== state.running ||
      state.running && (previous.pid !== state.pid || previous.birth !== state.birth)) {
    const previousEpoch = steamEpoch++;
    steamWhileReading = null;
    // A fresh Status may see the stop or new lifetime before the mode event does.
    // Only let a read started after the preceding edge cover this one.
    if (!state.running || hasSteamIdentity(state)) {
      if (reading.value && readSteamEpoch === previousEpoch) { steamWhileReading = state; return; }
      if (!reading.value && observedSession?.afterSteamEpoch === previousEpoch && coveredByStatus(state)) return;
    }
    queueObservation();
  }
});
watch(() => props.steamStateStale, stale => {
  if (stale && appearanceReadQueued === 'manual-entry') queueObservation();
});
function readVisible() {
  return props.sectionVisible && isUiVisible();
}
function clearReadTimer() { if (appearanceReadTimer) { clearTimeout(appearanceReadTimer); appearanceReadTimer = null; } }
function cancelAutomaticRead() { appearanceReadQueued = null; deferredObservationUntil = 0; clearReadTimer(); }
function retireObservation() { ++observationEpoch; observedSession = undefined; steamWhileReading = null; earlyResume = null; cancelAutomaticRead(); }
function visibilityChanged(powerGeneration?: number) {
  if (!readVisible()) { retireObservation(); return; }
  const hint = earlyResume;
  earlyResume = null;
  appearanceReadBudget.renew();
  if (hint && hint.generation === powerGeneration && hint.until > performance.now() && deferResumeObservation(hint.until)) return;
  queueObservation(powerGeneration === undefined ? 'normal' : 'power-return');
}
watch(() => props.sectionVisible, visible => {
  if (!visible) editorOpen.value = false;
  visibilityChanged();
});
let alive = true, active = true, stopDraft: (() => void) | null = null, returnFocus: HTMLElement | null = null;
// Count individual choices, including defaults during a slow first read.
// The group remains enabled when some effects are disabled; it has no mixed state.
const enabled = computed(() => {
  const { changes, snapshot: actual } = draft.value;
  let count = 0;
  for (const theme of STEAM_APPEARANCE_THEMES) {
    if (changes[theme.name] ?? actual?.themes.find(row => row.name === theme.name)?.configured ?? STEAM_APPEARANCE_DEFAULT_ENABLED) count++;
  }
  return count;
});
const masterOn = computed(() => enabled.value > 0);
const backendMatches = computed(() => !!snapshot.value?.backendReady && snapshot.value.configuredMatchesBackend &&
  snapshot.value.themes.every(row => row.installed && !row.error && row.backendEnabled === row.configured));
const idleConfirmed = computed(() => !!snapshot.value?.backendIdle && snapshot.value.steamRunning &&
  !snapshot.value.backendReady && snapshot.value.backendProcessCount === 0);
const ready = computed(() => {
  const state = snapshot.value;
  return !!state && draft.value.phase === 'idle' && !busy.value && !draft.value.pendingCount && !draft.value.saving &&
    state.steamRunning && !state.packError && state.packPrepared !== false && !state.packRepairRequired &&
    !state.backendReloadRequired && !state.foreignActiveCount && state.themes.every(row => row.installed && !row.error) &&
    (!state.backendIdle && backendMatches.value || idleConfirmed.value);
});
const status = computed(() => {
  if (busy.value) return '正在恢复预设';
  if (draft.value.phase === 'error') return '外观控制暂未确认 · 选择保留，可重新应用';
  if (draft.value.phase === 'restored') return '上次未提交选择已恢复 · 请手动应用';
  if (draft.value.saving) return '后台应用中 · 可以继续调整开关';
  if (draft.value.pendingCount) return '已选择 · 停止操作 3 秒后自动应用';
  if (draft.value.phase === 'saved-unconfirmed') return '配置已保存 · 后台应用尚未确认';
  if (!snapshot.value) return reading.value ? '正在读取外观配置' : '外观状态暂未确认 · 点击开关重新检测';
  if (snapshot.value.packError) return '外观资源或备份需修复 · ' + snapshot.value.packError;
  if (snapshot.value.packUpdateAvailable) return '新版主题资源待应用 · 重新应用将保留当前开关与参数';
  if (snapshot.value.packRepairRequired) return '已接管主题资源待修复 · 请重新应用，保留当前设置';
  if (snapshot.value.packPrepared === false) return snapshot.value.packAvailable ? '沿用当前开关 · 首次修改时备份并接管预设' : '自带主题资源缺失';
  if (snapshot.value.themes.some(row => !row.installed || row.error)) return '主题文件缺失或配置不可读 · 请重新应用修复';
  if (snapshot.value.backendReloadRequired) return '预设已保存 · 等待控制后端重新加载';
  if (snapshot.value.foreignActiveCount) return '另有 ' + snapshot.value.foreignActiveCount + ' 个外观待停用 · YMCC 优先';
  if (!snapshot.value.steamRunning) return '配置已保存 · 等待 Steam 启动';
  if (snapshot.value.backendIdle) return idleConfirmed.value ? '外观已应用 · CSS 后台已退出，修改时按需启动' : 'CSS 后台退出状态暂未确认 · 请重新检测';
  if (!snapshot.value.backendReady) return '美化控制接口未就绪 · 不代表当前效果已关闭';
  return backendMatches.value ? '配置与加载器一致' : '配置待重新应用';
});
async function read(automatic = false) {
  if (!alive || !active || !readVisible()) return;
  if (!automatic) appearanceReadBudget.renew();
  if (reading.value) return;
  // Keep UI-only observations in the card. The draft owner may finish a save
  // after the window is hidden or after this view has gone away.
  if (observationBlocked.value) { queueObservation(); return; }
  if (automatic) {
    const delay = appearanceReadBudget.delay(performance.now());
    if (delay === null) return;
    if (delay > 0) { queueObservation(); return; }
  }
  cancelAutomaticRead();
  appearanceReadBudget.started(performance.now());
  const afterSteamEpoch = readSteamEpoch = steamEpoch;
  reading.value = true;
  const epoch = observationEpoch;
  const observation: ActiveRead = { completion: backendCompletionEpoch, dispatched: false, deferredCompletion: false };
  activeRead = observation;
  const current = () => epoch === observationEpoch && alive && active && readVisible();
  let coveredCompletion = false;
  try {
    const result = await steamAppearanceDraft.read(current, {
      current: () => current() && observation.completion === backendCompletionEpoch,
      recheck: queueObservation,
      beforeStatusDispatch: () => {
        if (observation.dispatched) return;
        observation.dispatched = true;
        if (observation.deferredCompletion) observation.completion = backendCompletionEpoch;
      },
    });
    if (!current()) return;
    coveredCompletion = observation.dispatched && !!result;
    observedSession = result && { steamRunning: result.steamRunning, steamPid: result.steamPid, steamBirth: result.steamBirth, afterSteamEpoch };
    appearanceReadBudget.finished(draft.value.phase !== 'error');
  } finally {
    activeRead = undefined;
    if (alive) {
      const steam = steamWhileReading;
      steamWhileReading = null;
      reading.value = false;
      if (observation.deferredCompletion && !coveredCompletion && current()) queueObservation();
      if (steam && !coveredByStatus(steam)) queueObservation();
      scheduleObservation();
    }
  }
}
function setOne(name: AppearanceThemeName, value: boolean) { steamAppearanceDraft.set({ [name]: value }); }
function setAll(value: boolean) { steamAppearanceDraft.set(Object.fromEntries(STEAM_APPEARANCE_THEMES.map(row => [row.name, value]))); }
function refresh() { void steamAppearanceDraft.flush(); }
function reset(mode: 'preset' | 'previous') { void steamAppearanceDraft.reset(mode); }
async function openEditor() { returnFocus = document.activeElement as HTMLElement; editorOpen.value = true; if (!snapshot.value) await read(); }
async function closeEditor() {
  editorOpen.value = false; await nextTick();
  const target = returnFocus?.isConnected ? returnFocus : editButton.value;
  if (target) focusGamepadElement(target);
}
function scheduleObservation() {
  if (!appearanceReadQueued || !alive || !active || !readVisible() || reading.value || observationBlocked.value || appearanceReadTimer) return;
  const now = performance.now(), delay = appearanceReadBudget.delay(now);
  if (delay === null) { cancelAutomaticRead(); return; }
  appearanceReadTimer = setTimeout(() => {
    appearanceReadTimer = null;
    void read(true);
  }, Math.max(350, delay, deferredObservationUntil - now));
}
function queueObservation(kind: 'normal' | 'power-return' = 'normal') {
  if (!alive || !active || !readVisible()) return;
  if (deferredObservationUntil) { deferredObservationUntil = 0; clearReadTimer(); kind = 'normal'; }
  if (!appearanceReadQueued || kind === 'normal') appearanceReadQueued = kind;
  scheduleObservation();
}
function canDeferResumeObservation() {
  return (!appearanceReadQueued || appearanceReadQueued === 'power-return') && !reading.value &&
    !observationBlocked.value && ready.value && matchesDisplayedBigPicture(props.steamState) && alive && active && props.sectionVisible;
}
function deferGuardObservation(kind: GuardProgress, until: number) {
  clearReadTimer();
  appearanceReadQueued = kind;
  deferredObservationUntil = until;
  scheduleObservation();
}
function deferResumeObservation(until: number) {
  if (!readVisible() || !canDeferResumeObservation()) return false;
  deferGuardObservation('resume', until);
  return true;
}
function backendChanged(event: Event) {
  if (event instanceof CustomEvent) {
    const state = event.detail;
    // The guard already waits for these helpers and reports their outcome.
    if ((state?.phase === 'applying' && state.reason === 'temporary-css-use') ||
        (state?.phase === 'verifying' && state.reason === 'manual-use-read-only-confirmation')) return;
    if (state?.linked === true && state.steamRunning === true) {
      if ((state.phase === 'ready' && state.reason === 'appearance-applied-resident') ||
          (state.phase === 'waiting-appearance' && state.reason === 'appearance-unconfirmed')) {
        ++backendCompletionEpoch;
        // A successful Status dispatched after the outcome can cover it. An
        // earlier receipt still cannot authorize a write after that outcome.
        if (activeRead && !activeRead.dispatched && !appearanceReadQueued && readVisible()) {
          earlyResume = null;
          activeRead.deferredCompletion = true;
          return;
        }
      }
      if (state.phase === 'applying' && state.reason === 'appearance-account-recheck') {
        // Wait for the probe outcome without spending the read budget on progress.
        // Keep one fallback if it never arrives, without delaying another read.
        if (!appearanceReadQueued && alive && active && readVisible()) {
          deferGuardObservation('account-recheck', performance.now() + GUARD_OBSERVATION_MAX_MS);
        }
        return;
      }
      const resumeStart = state.phase === 'waiting-steam-ui' && state.reason === 'resume-css-settle-delay';
      const waitingForMode = state.phase === 'waiting-steam-ui' && state.reason === 'appearance-needs-confirmed-bigpicture';
      const manualEntry = state.phase === 'applying' && state.reason === 'manual-bigpicture-entry';
      if (appearanceReadQueued === 'manual-entry' && (manualEntry || waitingForMode)) return;
      if (manualEntry && !appearanceReadQueued && !reading.value && !observationBlocked.value &&
          alive && active && readVisible() && !props.steamStateStale && matchesDisplayedSteam(props.steamState)) {
        // An explicit mode switch reports progress before its CSS result. Do
        // not spend the read budget until that result or the bounded fallback.
        deferGuardObservation('manual-entry', performance.now() + GUARD_OBSERVATION_MAX_MS);
        return;
      }
      const resumeProgress = (state.phase === 'applying' && state.reason === 'resume-css-recheck') ||
        waitingForMode;
      if ((resumeStart || resumeProgress) && appearanceReadQueued === 'resume') return;
      if (resumeStart) {
        const until = performance.now() + GUARD_OBSERVATION_MAX_MS;
        if (deferResumeObservation(until)) return;
        // The guard can post before the same power generation reaches the UI.
        // Retain only a scheduling hint; never start a read or timer here.
        if (!earlyResume && canDeferResumeObservation()) {
          const space = getUiVisibilityState();
          if (!space.powerReady && space.powerGeneration > 0 && space.documentVisible && space.nativeWindowVisible) {
            earlyResume = { generation: space.powerGeneration, until };
          }
        }
        if (earlyResume) return;
      }
      if (resumeProgress && earlyResume) return;
    }
  }
  earlyResume = null;
  queueObservation();
}
function explicitRefresh() { void read(); }
onMounted(() => {
  stopDraft = steamAppearanceDraft.subscribe(value => {
    if (!alive) return;
    draft.value = value;
    if (observationBlocked.value) clearReadTimer();
    else scheduleObservation();
  });
  // The subscription synchronously returns its current sample, not a new edge.
  stopVisibility = onUiVisibilityChange((state, source) => {
    if (source !== 'initial') visibilityChanged(source === 'power' ? state.powerGeneration : undefined);
  });
  let powerReady = false, suspendSerial = 0;
  stopPower = onSharedPowerLifecycle(state => {
    const ready = isUiPowerReady(state), serial = state.suspendSerial ?? suspendSerial;
    const mergedSuspend = ready && powerReady && serial !== suspendSerial;
    powerReady = ready;
    suspendSerial = serial;
    // Regular suspend/Ready already retires the read via presentation visibility.
    if (mergedSuspend) { retireObservation(); visibilityChanged(state.generation); }
  }, { observeSuspendSerial: true });
  void read(true);
  window.addEventListener('global:refresh', explicitRefresh); window.addEventListener('ipc:steamAppearance.backendChanged', backendChanged);
});
onActivated(() => { const returning = !active; active = true; if (returning) visibilityChanged(); });
onDeactivated(() => { active = false; editorOpen.value = false; retireObservation(); });
onBeforeUnmount(() => { alive = false; retireObservation(); stopPower?.(); stopVisibility?.(); stopDraft?.(); window.removeEventListener('global:refresh', explicitRefresh); window.removeEventListener('ipc:steamAppearance.backendChanged', backendChanged); });
</script>

<template>
  <section class="card steam-appearance-card" aria-label="Steam 自定义外观">
    <div class="appearance-card-head">
      <div><h3>Steam 自定义外观</h3><p>使用 YMCC 自带预设，即时切换效果。</p></div>
      <div class="appearance-header-status">
        <SwitchControl class="appearance-master" :model-value="masterOn" label="全部外观效果" title="全部外观效果" :disabled="busy" :gp-row="1" :gp-col="1" @update:model-value="setAll" />
        <span class="appearance-count" :aria-label="`${enabled} 项效果${draft.pendingCount ? '已选择，待应用' : snapshot ? '已配置开启' : '默认开启，状态待确认'}，共 ${STEAM_APPEARANCE_THEMES.length} 项`">{{ enabled }} / {{ STEAM_APPEARANCE_THEMES.length }}</span>
      </div>
    </div>
    <div class="appearance-card-controls">
      <button ref="editButton" class="appearance-edit" data-gp-row="2" data-gp-col="0" @click="openEditor">编辑外观 <span>›</span></button>
      <button class="appearance-refresh" :disabled="busy" data-gp-row="2" data-gp-col="1" @click="refresh">{{ busy ? '处理中…' : '重新应用' }}</button>
    </div>
    <div class="appearance-status-line"><span class="appearance-status-dot" :class="{ ready }"></span><span>{{ status }}</span><span v-if="reading"> · 检测中</span></div>
    <p v-if="notice" class="appearance-notice" role="status">{{ notice }}</p>
    <SteamAppearanceEditor v-if="editorOpen" :snapshot="snapshot" :changes="draft.changes" :busy="busy" :notice="notice" :pending="draft.pendingCount > 0" :saving="draft.saving" @close="closeEditor" @change="setOne" @refresh="refresh" @reset="reset" />
  </section>
</template>

<style scoped>
.steam-appearance-card{margin-top:12px}.appearance-card-head{display:flex;align-items:center;justify-content:space-between;gap:12px}.appearance-card-head h3{font-size:14px;margin:0 0 6px;font-weight:500}.appearance-card-head p{font-size:11px;color:var(--text-dim);margin:0}.appearance-count{font-size:11px;color:var(--accent);white-space:nowrap;background:color-mix(in srgb,var(--accent) 9%,transparent);padding:5px 8px;border-radius:6px}.appearance-card-controls{display:flex;align-items:center;gap:8px;margin-top:16px}.appearance-card-controls>button{border-radius:var(--radius-ctrl);padding:9px 11px;background:var(--bg-input);border:1px solid color-mix(in srgb,var(--text-dim) 20%,transparent);color:var(--text);font-size:12px;cursor:pointer;min-height:36px}.appearance-header-status{display:flex;align-items:center;gap:9px;flex-shrink:0}.appearance-card-controls>button{flex:1;justify-content:center}.appearance-edit{display:flex;align-items:center;gap:12px}.appearance-edit>span{color:var(--text-dim);font-size:18px;line-height:12px}.appearance-refresh{white-space:nowrap}.appearance-status-line{display:flex;align-items:center;gap:6px;font-size:10px;color:var(--text-dim);margin-top:11px}.appearance-status-dot{width:5px;height:5px;border-radius:50%;background:var(--text-dim)}.appearance-status-dot.ready{background:var(--ok)}.appearance-notice{font-size:11px;line-height:1.5;color:var(--accent);margin:9px 0 0;overflow-wrap:anywhere}.appearance-card-controls>button:disabled{opacity:.45;cursor:wait}.appearance-card-controls>button:focus-visible,.appearance-card-controls>button.focused{outline:none;box-shadow:var(--focus-ring)}
@media(max-width:460px){.appearance-card-controls{flex-wrap:wrap}.appearance-card-controls>button{flex:1}.appearance-edit{flex:1;justify-content:space-between}}
</style>
