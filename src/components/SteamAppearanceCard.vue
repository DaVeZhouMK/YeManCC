<script setup lang="ts">
import SwitchControl from './SwitchControl.vue';
import { computed, inject, nextTick, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, watch, type Ref } from 'vue';
import SteamAppearanceEditor from './SteamAppearanceEditor.vue';
import { STEAM_APPEARANCE_THEMES, STEAM_APPEARANCE_DEFAULT_ENABLED, type AppearanceThemeName, type AppearanceSnapshot } from '@/bridge/steamAppearance';
import { steamAppearanceDraft } from '@/bridge/steamAppearanceDraft';
import { createSteamReadBudget } from '@/bridge/steamReadBudget';
import { getUiVisibilityState, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { focusGamepadElement } from '@/gamepad/focus';
const draft = ref(steamAppearanceDraft.getState());
const snapshot = computed<AppearanceSnapshot | null>(() => {
  const actual = draft.value.snapshot;
  return actual ? { ...actual, configuredMatchesBackend: draft.value.pendingCount || draft.value.saving ? false : actual.configuredMatchesBackend,
    themes: actual.themes.map(row => ({ ...row, configured: draft.value.changes[row.name] ?? row.configured })) } : null;
});
// Slow writes never disable switches. Only an explicitly confirmed preset reset locks them.
const busy = computed(() => draft.value.locked), reading = ref(false), editorOpen = ref(false);
const notice = computed(() => draft.value.message), editButton = ref<HTMLButtonElement | null>(null);
const appearanceReadBudget = createSteamReadBudget();
let appearanceReadTimer: ReturnType<typeof setTimeout> | null = null;
let appearanceReadQueued = false;
let stopVisibility: (() => void) | null = null;
function readVisible() { const state = getUiVisibilityState(); return state.documentVisible && state.nativeWindowVisible; }
function cancelAutomaticRead() { appearanceReadQueued = false; if (appearanceReadTimer) { clearTimeout(appearanceReadTimer); appearanceReadTimer = null; } }
function visibilityChanged() {
  if (!readVisible()) { cancelAutomaticRead(); return; }
  appearanceReadBudget.renew(); globalRefresh(new Event('ipc:steamAppearance.backendChanged'));
}
let alive = true, active = true, stopDraft: (() => void) | null = null, returnFocus: HTMLElement | null = null;
// Count individual choices, including defaults during a slow first read.
// The group remains enabled when some effects are disabled; it has no mixed state.
const enabled = computed(() => STEAM_APPEARANCE_THEMES.filter(theme =>
  draft.value.changes[theme.name] ?? snapshot.value?.themes.find(row => row.name === theme.name)?.configured ?? STEAM_APPEARANCE_DEFAULT_ENABLED).length);
const masterOn = computed(() => STEAM_APPEARANCE_THEMES.some(theme =>
  draft.value.changes[theme.name] ?? snapshot.value?.themes.find(row => row.name === theme.name)?.configured ?? STEAM_APPEARANCE_DEFAULT_ENABLED));
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
  if (automatic) {
    const delay = appearanceReadBudget.delay(performance.now());
    if (delay === null) return;
    if (delay > 0) { globalRefresh(new Event('ipc:steamAppearance.backendChanged')); return; }
  }
  appearanceReadBudget.started(performance.now());
  reading.value = true;
  try { await steamAppearanceDraft.read(); appearanceReadBudget.finished(steamAppearanceDraft.getState().phase !== 'error'); } finally { if (alive) { reading.value = false; if (appearanceReadQueued) { appearanceReadQueued = false; globalRefresh(new Event('ipc:steamAppearance.backendChanged')); } } }
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
function globalRefresh(event?: Event) {
  if (!alive || !active || !readVisible()) return;
  const automatic = event?.type === 'steam-appearance:environment' || event?.type === 'ipc:steamAppearance.backendChanged';
  if (!automatic) { if (appearanceReadTimer) { clearTimeout(appearanceReadTimer); appearanceReadTimer = null; } void read(); return; }
  if (reading.value) { appearanceReadQueued = true; return; }
  if (appearanceReadTimer) return;
  const delay = appearanceReadBudget.delay(performance.now());
  if (delay === null) return;
  appearanceReadTimer = setTimeout(function runAppearanceObservation() { appearanceReadTimer = null; if (alive && active && readVisible()) void read(true); }, Math.max(350, delay));
}
const refreshKey = inject<Ref<number>>('globalRefreshKey');
if (refreshKey) watch(refreshKey, () => globalRefresh());
onMounted(() => {
  stopDraft = steamAppearanceDraft.subscribe(value => { if (alive) draft.value = value; }); void read(true);
  stopVisibility = onUiVisibilityChange(visibilityChanged);
  document.addEventListener('visibilitychange', visibilityChanged);
  window.addEventListener('global:refresh', globalRefresh); window.addEventListener('steam-appearance:environment', globalRefresh); window.addEventListener('ipc:steamAppearance.backendChanged', globalRefresh);
});
onActivated(() => { const returning = !active; active = true; if (returning) visibilityChanged(); });
onDeactivated(() => { active = false; editorOpen.value = false; cancelAutomaticRead(); });
onBeforeUnmount(() => { alive = false; cancelAutomaticRead(); stopVisibility?.(); document.removeEventListener('visibilitychange', visibilityChanged); stopDraft?.(); window.removeEventListener('global:refresh', globalRefresh); window.removeEventListener('steam-appearance:environment', globalRefresh); window.removeEventListener('ipc:steamAppearance.backendChanged', globalRefresh); });
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