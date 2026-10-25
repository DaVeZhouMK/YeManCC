<script setup lang="ts">
// [AI:USER:PERFORMANCE] 调整参数→作用于当前编辑的AC/DC与公用/游戏专属范围；供电和游戏切换沿各自配置。
// 切自动/手动→使用对应设置；保存成功、已下发上限与实测功耗分别呈现，不能互当成功证明。
// 滑块先聚焦、按A再编辑；取消不误提交。UI最低1280×720/1280×800，具体执行读 bridge/performanceSchedule.ts。
import GameProfileRemoveDialog from './GameProfileRemoveDialog.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { tryAcquireQuickAction } from '@/bridge/quickActionLock';
import GameProfileModeBanner from '@/components/GameProfileModeBanner.vue';
import { computed, nextTick, onMounted, onBeforeUnmount, onDeactivated, ref, watch } from 'vue';
import Slider from './Slider.vue';
import { EDIT_REPEAT_BASE_MS } from '@/gamepad/sliderRepeat';
import type { PerformancePanelSource } from '@/bridge/performancePanelSource';
import Dropdown from './Dropdown.vue';
import AppIcon from './AppIcon.vue';
import { powerSourceMode } from '@/bridge/powerSource';
import { subscribeMonitorTdpState } from '@/bridge/monitorAppliedTdp';
import { subscribePolicyGameStatus } from '@/bridge/gamePolicyTarget';
import { getContinuousPerformanceSnapshot, updateContinuousPerformanceControls,
  loadGameCustomConfig, removeGameCustomProfile, restorePerformanceScheduleIfConfigured,
  type GameCustomProfileTarget, onPerformanceScheduleChanged, onGameCustomConfigChanged } from '@/bridge/performanceSchedule';
import { onFrameRatesChanged, frameRateSetting, FRAME_RATE_CEILINGS,
  type FrameRatePair, type FrameRateSetting } from '@/bridge/frameRateLimits';
import { CONTROL_CORE_OPTIONS, SAVING_LEVELS, SAVING_RULES, automaticCpuControls, manualCpuMinimum, patchContinuousBank,
  type ControlSide, type ContinuousPerformanceSnapshot, type ContinuousSideControls, type ContinuousSidePatch } from '@/bridge/performanceControls';
const props = withDefaults(defineProps<{ scope?: 'global' | 'dedicated' | 'effective'; showSimulation?: boolean; panelSource?: PerformancePanelSource; readonlyPreview?: boolean }>(), { scope: 'global', showSimulation: false });
const emit = defineEmits<{ (e: 'simulation'): void; (e: 'changed'): void }>();
const snapshot = ref<ContinuousPerformanceSnapshot | null>(null), draft = ref<ContinuousSideControls | null>(null);
const frames = ref<FrameRatePair | null>(null), frame = ref<FrameRateSetting | null>(null);
const panelElement=ref<HTMLElement|null>(null);
const removal = ref<{target:GameCustomProfileTarget;gameName:string;revision:string}|null>(null);
const removalError = ref(''), removalBusy = ref(false);
async function requestRemoval() {
  if (props.readonlyPreview || saving.value || removalBusy.value || snapshot.value?.identity.scope !== 'dedicated') return;
  const identity = {...snapshot.value.identity}, gameName = snapshot.value.gameName || identity.exe;
  removalBusy.value = true; note.value = '';
  try {
    const config = await loadGameCustomConfig();
    const current = snapshot.value?.identity, entry = config.entries[identity.exe];
    if (!alive || !current || current.scope !== 'dedicated' || current.exe !== identity.exe || current.pid !== identity.pid || current.processCreated !== identity.processCreated || current.generation !== identity.generation || !entry) throw new Error('当前游戏已变化，请重新打开专属配置');
    removalError.value = '';
    removal.value = {target:identity,gameName,revision:JSON.stringify([entry,config.rtss[identity.exe]])};
  } catch(error) { if(alive) note.value = (error as Error).message; }
  finally { removalBusy.value = false; }
}
function cancelRemoval() {
  if(removalBusy.value)return; removal.value=null;
  void nextTick(()=>focusGamepadElement(panelElement.value?.querySelector<HTMLElement>('[data-performance-remove]') || panelElement.value?.querySelector<HTMLElement>('.control-modes button') || null));
}
async function confirmRemoval() {
  if(props.readonlyPreview)return;
  const request=removal.value;if(!request || removalBusy.value)return;
  const release=tryAcquireQuickAction('performance-profile-remove');
  if(!release){removalError.value='已有其它快捷操作正在执行，请稍候';return;}
  removalBusy.value=true;removalError.value='';let deleted=false;
  try {
    await removeGameCustomProfile(request.target,request.revision);deleted=true;removal.value=null;
    await restorePerformanceScheduleIfConfigured();
    if(alive)note.value=`已删除 ${request.gameName} 专属预设，恢复公用设置`;
  } catch(error) {
    if(alive){if(deleted)note.value=`专属预设已删除，恢复公用调度未完成：${(error as Error).message}`;else removalError.value=(error as Error).message;}
  } finally {
    removalBusy.value=false;release();
    if(deleted && alive){await load(true);await nextTick();focusGamepadElement(panelElement.value?.querySelector<HTMLElement>('.control-modes button') || null);emit('changed');}
  }
}
const note = ref(''), saving = ref(false), editSide = ref<ControlSide | null>(null);
// Reads, saves and the local edit have separate revisions. A late read/ACK
// may update the saved identity, but must not replace a newer slider draft.
let epoch = 0, readEpoch = 0, draftRevision = 0, alive = true;
let pending: ContinuousSidePatch | null = null;
let editingInput: HTMLInputElement | null = null, refreshPending = false, contextInvalidated = false;
const performanceSliderRepeat = { accelerate: true, repeatIntervalMs: EDIT_REPEAT_BASE_MS };
const stops: (() => void)[] = [];
const side = computed(() => snapshot.value?.side || null);
const actualSide = computed(() => snapshot.value?.actualSide ?? powerSourceMode.value ?? snapshot.value?.side);
const color = computed(() => side.value === 'dc' ? 'dc' : 'accent');
const minimum = computed(() => snapshot.value?.hardwareMinimum ?? 1);
const maximum = computed(() => Math.min(150, snapshot.value?.hardwareMaximum ?? 150));
const runtimeTdp = ref<{mode: string | null; watts: number | null} | null>(null);
const coreMaximum = computed(() => Math.max(0, Math.min(draft.value?.tdp ?? 0, maximum.value)));
// Slider range only; keep saved coreSplit and the high-power interval intact.
const coreSliderMaximum = computed(() => Math.min(25, coreMaximum.value));
const coreSliderDisplay = computed(() => Math.min(draft.value?.auto.coreSplit ?? 0, coreSliderMaximum.value));
const coreDisplay = computed(() => Math.min(draft.value?.auto.coreSplit ?? 0, coreMaximum.value));
const coreRuntime = computed(() => props.panelSource || side.value !== actualSide.value || runtimeTdp.value?.mode !== actualSide.value ? null : runtimeTdp.value?.watts ?? null);
const cpu = computed(() => draft.value ? automaticCpuControls(draft.value) : null);
const minimumState = computed(() => draft.value ? manualCpuMinimum(draft.value) : 0);
const ceilingOptions = computed(() => [...new Set([16, 30, 45, 55, 120, 150, draft.value?.tdpCeiling || 45, maximum.value])]
  .sort((a,b) => a-b).filter(v => v >= minimum.value && v <= maximum.value).map(value => ({value, label: `最大 ${value} W`})));
const frameOptions = FRAME_RATE_CEILINGS.map(value => ({value, label: value === 0 ? '关闭锁帧' : `${value} FPS`}));
const autoFrequency = computed(() => !cpu.value ? '' : cpu.value.maxMHz === 0 ? `${(cpu.value.minMHz/1000).toFixed(1)} GHz–不限` : `${(cpu.value.minMHz/1000).toFixed(1)}–${(cpu.value.maxMHz/1000).toFixed(1)} GHz`);
function merge(a: ContinuousSidePatch | null, b: ContinuousSidePatch): ContinuousSidePatch {
  return { ...a, ...b, ...(a?.auto || b.auto ? { auto: { ...a?.auto, ...b.auto } } : {}), ...(a?.manual || b.manual ? { manual: { ...a?.manual, ...b.manual } } : {}) };
}
function invalidateDraft() {
  const input = editingInput;
  epoch++; readEpoch++; draftRevision++; pending = null; editingInput = null;
  contextInvalidated = true;
  window.dispatchEvent(new CustomEvent('gp:slider-edit-invalidated', { detail: { panel: panelElement.value, input } }));
}
function flushRefresh() {
  if (alive && refreshPending && !editingInput && !saving.value) void load();
}
function cancelActiveDraft() {
  if (editingInput) void load(true);
}
function beginSliderEdit(event: Event) {
  const input = event.target;
  if (!(input instanceof HTMLInputElement) || input.type !== 'range' || editingInput === input) return;
  editingInput = input; draftRevision++;
}
function endSliderEdit(event: Event) {
  if (editingInput !== event.target) return;
  editingInput = null;
  // Slider emits its commit synchronously after edit-end. Let it acquire the
  // save guard before a deferred status refresh can read the old setting.
  void nextTick(flushRefresh);
}
function sameEditContext(a: ContinuousPerformanceSnapshot['identity'], b: ContinuousPerformanceSnapshot['identity']) {
  return a.scope === b.scope && a.side === b.side && a.actualSide === b.actualSide &&
    a.exe === b.exe && a.pid === b.pid && a.processCreated === b.processCreated && a.generation === b.generation;
}
async function load(force = false, checkContext = false) {
  if (!alive) return;
  if (force) invalidateDraft();
  if ((editingInput || saving.value) && !checkContext) { refreshPending = true; return; }
  const ticket = ++readEpoch, context = epoch, revision = draftRevision;
  refreshPending = false;
  try {
    const value = await (props.panelSource ? props.panelSource.read(editSide.value ?? undefined) : getContinuousPerformanceSnapshot(props.scope, editSide.value ?? undefined));
    if (!alive || ticket !== readEpoch || context !== epoch) return;
    if (snapshot.value && !sameEditContext(snapshot.value.identity, value.identity)) {
      invalidateDraft();
      if (saving.value) { refreshPending = true; return; }
    } else if (editingInput || saving.value || revision !== draftRevision) { refreshPending = true; return; }
    snapshot.value = value; draft.value = value.bank ? structuredClone(value.bank) : null;
    frames.value = structuredClone(value.frameRates);
    frame.value = value.side ? { ...frames.value[value.side] } : null;
    contextInvalidated = false;
  } catch (error) { if (alive && ticket === readEpoch && context === epoch) note.value = (error as Error).message; }
}
function change(patch: ContinuousSidePatch) {
  if (props.readonlyPreview || contextInvalidated) return;
  if (!snapshot.value?.side || !draft.value) return;
  draftRevision++;
  draft.value = patchContinuousBank(draft.value, patch, snapshot.value.side);
}
function changeFrame(value: number) {
  if (props.readonlyPreview || contextInvalidated || !frame.value) return;
  draftRevision++; frame.value.fps = value;
}
async function commit(patch: ContinuousSidePatch) {
  if (props.readonlyPreview || contextInvalidated) return;
  if (removalBusy.value || !snapshot.value?.side || !draft.value) return;
  change(patch); pending = merge(pending, patch);
  if (saving.value) return;
  saving.value = true; const ticket = epoch; note.value = '';
  try {
    while (pending && alive && ticket === epoch) {
      const submitted: ContinuousSidePatch = pending, revision = draftRevision, identity = snapshot.value!.identity; pending = null;
      const next = await (props.panelSource ? props.panelSource.commit(identity, submitted) : updateContinuousPerformanceControls(identity, submitted));
      if (!alive || ticket !== epoch) return;
      if (!sameEditContext(identity, next.identity)) { await load(true); return; }
      snapshot.value = next;
      // Keep the next edit exactly where the user moved it, while retaining
      // the new saved identity for its eventual compare-and-save.
      if (revision === draftRevision && !editingInput) {
        draft.value = next.bank ? structuredClone(next.bank) : null;
        frames.value = structuredClone(next.frameRates);
        frame.value = next.side ? { ...frames.value[next.side] } : null;
      }
      emit('changed');
    }
    note.value = props.panelSource ? '' : '设置已保存';
  } catch (error) {
    if (alive && ticket === epoch) { note.value = (error as Error).message; await load(true); }
  } finally {
    saving.value = false;
    flushRefresh();
  }
}
function saveFrame(value: FrameRateSetting) { return commit({frameRate:{...value}}); }
function frameCeiling(value: string | number) {
  if (!frame.value) return;
  const max = Number(value), prior = frame.value;
  const next = max === 0 ? { fps: 0, ceiling: 0, lastFps: prior.fps || prior.lastFps } : {
    fps: Math.min(max, prior.fps || prior.lastFps), ceiling: max, lastFps: Math.min(max, prior.fps || prior.lastFps) };
  frame.value = frameRateSetting(next); void saveFrame(frame.value);
}
function savingLevel(value: number) { void commit({ auto: { saving: SAVING_LEVELS[Math.max(0, Math.min(4, value))] } }); }
function selectSide(value: ControlSide) { if (saving.value) return; editSide.value = value; note.value = ''; void load(true); }
const savingText = computed(() => !draft.value || draft.value.auto.saving === 'none' ? '无浮动' : `${SAVING_RULES[draft.value.auto.saving].label} · 允许下探 ${SAVING_RULES[draft.value.auto.saving].ratio*100}%`);
const targetTdp = computed(() => Math.max(minimum.value, cpu.value?.targetTdp ?? 0));
const topology = computed(() => snapshot.value?.coreGroups?.map(g => `${g.label} ${g.physical}核/${snapshot.value?.hyperThreadMode==='off'?g.physical:g.logical}线程`) ?? []);
function toggleSmt() { if (snapshot.value?.smtAvailable) void commit({ hyperThreadMode: snapshot.value.hyperThreadMode === 'off' ? 'on' : 'off' }); }
watch(() => props.scope, () => void load(true));
watch(powerSourceMode, () => { if (!props.readonlyPreview && !props.panelSource) { note.value = ''; void load(true); } });
onDeactivated(cancelActiveDraft);
onMounted(() => {
  if (props.readonlyPreview) { void load(); return; }
  const onVisibility = () => { if (document.visibilityState === 'hidden') cancelActiveDraft(); };
  window.addEventListener('ipc:window.hidden', cancelActiveDraft);
  window.addEventListener('ipc:window.minimized', cancelActiveDraft);
  document.addEventListener('visibilitychange', onVisibility);
  stops.push(() => {
    window.removeEventListener('ipc:window.hidden', cancelActiveDraft);
    window.removeEventListener('ipc:window.minimized', cancelActiveDraft);
    document.removeEventListener('visibilitychange', onVisibility);
  });
  if (!props.panelSource) stops.push(subscribeMonitorTdpState(state => { runtimeTdp.value = state.status === 'applied' ? {mode:state.mode,watts:state.watts} : null; }));
  if (props.panelSource) stops.push(props.panelSource.subscribe(() => { void load(); }));
  else stops.push(subscribePolicyGameStatus(game => {
    const identity = snapshot.value?.identity;
    const changed = !identity || identity.pid !== (game?.pid || 0) || identity.processCreated !== String(game?.processCreated || '');
    void load(changed);
  }), onPerformanceScheduleChanged(() => { void load(); }),
    onGameCustomConfigChanged(() => { void load(false, true); }), onFrameRatesChanged(() => { void load(); }));
  void load();
});
onBeforeUnmount(() => { invalidateDraft(); alive = false; epoch++; pending = null; for (const stop of stops) stop(); });
defineExpose({ reload: () => load(true) });
</script>

<template>
  <section ref="panelElement" class="continuous-performance" :class="[side,{'performance-compact':!panelSource,'integrated-ui':!panelSource}]" aria-label="连续性能调度" :inert="readonlyPreview || undefined" data-gp-scope="performance-controls" data-gp-performance-panel @gp:performance-edit-cancel="load(true)" @gp:slider-edit-start="beginSliderEdit" @gp:slider-edit-end="endSliderEdit" @gp:slider-edit-cancel="load(true)">
    <header v-if="!panelSource" class="integrated-heading"><div class="integrated-title"><AppIcon v-if="scope!=='dedicated'" name="gauge"/><h1 v-if="scope!=='dedicated'">性能调度</h1><span class="integrated-supply" role="status"><i :class="{battery:actualSide==='dc'}"/>{{actualSide==='ac'?'插电 · AC':actualSide==='dc'?'电池 · DC':'确认供电中'}}</span></div><button v-if="showSimulation" class="simulation-entry" data-performance-simulation-entry data-gp-row="-1" data-gp-col="3" @click="emit('simulation')"><AppIcon name="play"/>模拟测试/重置<span>↗</span></button></header>
    <div v-else class="supply-status" role="status"><AppIcon :name="actualSide === 'dc' ? 'battery' : 'plug'" /><strong>{{ actualSide === 'ac' ? '当前插电 AC 模式' : actualSide === 'dc' ? '当前电池 DC 模式' : '正在确认供电状态' }}</strong></div>
    <GameProfileModeBanner v-if="!panelSource && scope !== 'dedicated' && snapshot?.identity.scope === 'dedicated'" :game-name="snapshot.gameName || snapshot.identity.exe"><template #actions><button class="profile-delete" data-performance-remove data-gp-row="-0.5" data-gp-col="3" :disabled="saving || removalBusy" @click="requestRemoval">删除预设</button></template></GameProfileModeBanner>
    <template v-if="draft && snapshot && frame">
      <div class="integrated-switchboard" :class="{'simulation-switchboard':panelSource}">
      <div class="control-modes control-bubble" :class="{ three: showSimulation && !!panelSource }" data-gp-group="continuous-modes">
        <button :class="{ active: draft.mode === 'auto' }" data-gp-row="0" data-gp-col="0" @click="commit({mode:'auto'})">自动模式</button>
        <button :class="{ active: draft.mode === 'manual' }" data-gp-row="0" data-gp-col="1" @click="commit({mode:'manual'})">手动模式</button>
        <button v-if="showSimulation && panelSource" data-performance-simulation-entry data-gp-row="0" data-gp-col="2" @click="emit('simulation')">模拟测试/重置</button>
      </div>
      <div class="power-modes control-bubble" role="group" aria-label="编辑供电配置">
        <button :class="{active:side==='ac'}" :aria-pressed="side==='ac'" :disabled="saving || (panelSource?.sideLocked && side!=='ac')" :data-gp-row="panelSource?1:0" :data-gp-col="panelSource?0:2" @click="selectSide('ac')"><AppIcon name="plug" />交流电模式</button>
        <button :class="{active:side==='dc'}" :aria-pressed="side==='dc'" :disabled="saving || (panelSource?.sideLocked && side!=='dc')" :data-gp-row="panelSource?1:0" :data-gp-col="panelSource?1:3" @click="selectSide('dc')"><AppIcon name="battery" />电池模式</button>
      </div>
      </div>
      <div v-if="scope!=='dedicated'" class="parameter-summary control-bubble" aria-label="当前参数概览">
        <div v-if="!panelSource" class="tdp-summary range-summary"><span>{{draft.mode==='auto'?'浮动 TDP 区间':'手动 TDP'}}</span><b>{{draft.mode==='auto'?`${Math.min(targetTdp,draft.tdp)}–${draft.tdp}`:draft.tdp}}<small> W</small></b></div>
        <div v-else class="tdp-summary"><div><span>浮动目标</span><b>{{draft.mode==='auto'?targetTdp:draft.tdp}}<small> W</small></b></div><span class="budget-arrow">←</span><div><span>当前 TDP</span><b>{{draft.tdp}}<small> W</small></b></div></div>
        <div class="frequency-summary"><div class="frequency-heading"><span>CPU 频率</span><span class="turbo-badge" :class="{lit:draft.mode==='auto'?cpu?.turbo:draft.manual.turbo}">{{(draft.mode==='auto'?cpu?.turbo:draft.manual.turbo)?'开睿频':'关睿频'}}</span></div><b>{{draft.mode==='auto'?autoFrequency:draft.manual.unlimited?'不限':`${(draft.manual.frequencyMHz/1000).toFixed(1)} GHz`}}</b></div>
        <div class="aggression-summary"><span>CPU 积极性</span><b>{{draft.mode==='auto'?cpu?.aggressiveness:draft.manual.aggressiveness}}%</b></div>
      </div>
      <div class="control-bubble limits-bubble">
        <div class="slider-dropdown-row" data-gp-game-row="continuous-tdp">
          <div class="metric-label"><span>TDP</span><strong>{{draft.tdp}}<small> W</small></strong></div>
          <div class="range-body"><Slider class="bare-slider" :model-value="draft.tdp" :min="minimum" :max="Math.min(draft.tdpCeiling,maximum)" :step="1" v-bind="performanceSliderRepeat" label="TDP" :color="color" :gp-initial-focus="scope === 'effective' && !panelSource" :gp-row="2" :gp-col="0" @update:model-value="v => change({tdp:v})" @commit="v => commit({tdp:v})" /><div class="range-ends" aria-hidden="true"><span>{{minimum}} W</span><span>{{Math.min(draft.tdpCeiling,maximum)}} W</span></div></div>
          <Dropdown :model-value="draft.tdpCeiling" :options="ceilingOptions" :color="color" aria-label="最大 TDP" :gp-row="2" :gp-col="1" @update:model-value="v => commit({tdpCeiling:Number(v)})" />
        </div>
        <div class="slider-dropdown-row" data-gp-game-row="continuous-frame">
          <div class="metric-label"><span>目标帧率</span><strong>{{frame.fps===0?'关闭':frame.fps}}<small v-if="frame.fps"> FPS</small></strong></div>
          <div class="range-body"><Slider class="bare-slider" :model-value="frame.fps" :min="frame.fps === 0 ? 0 : 15" :max="frame.ceiling || 15" :step="5" v-bind="performanceSliderRepeat" :disabled="frame.fps===0" label="目标帧率" :color="color" :gp-row="3" :gp-col="0" @update:model-value="changeFrame" @commit="() => frame && saveFrame(frame)" /><div class="range-ends" aria-hidden="true"><span>15 FPS</span><span>{{frame.ceiling || 15}} FPS</span></div></div>
          <Dropdown :model-value="frame.ceiling" :options="frameOptions" :color="color" aria-label="帧率上限" :gp-row="3" :gp-col="1" @update:model-value="frameCeiling" />
        </div>
      </div>
      <div v-if="draft.mode === 'auto'" class="control-bubble auto-bubble">
        <div class="saving-section">
          <Slider :model-value="SAVING_LEVELS.indexOf(draft.auto.saving)" :min="0" :max="4" :step="1" v-bind="performanceSliderRepeat" label="浮动节能" :value-text="savingText" :color="color" :gp-row="4" :gp-col="0" @update:model-value="v => change({auto:{saving:SAVING_LEVELS[v]}})" @commit="savingLevel" />
          <div class="saving-ticks" aria-hidden="true"><span v-for="level in SAVING_LEVELS" :key="level">{{SAVING_RULES[level].label}}</span></div>
        </div>
        <div class="bias-section">
          <div class="bias-labels"><span>CPU 优先</span><output aria-label="当前CPU/GPU倾向">{{draft.auto.bias}}</output><span>GPU 优先</span></div>
          <Slider class="bare-slider bias-slider" :model-value="draft.auto.bias" :min="0" :max="100" :step="5" v-bind="performanceSliderRepeat" label="CPU / GPU 倾向" :color="color" :gp-row="5" :gp-col="0" @update:model-value="v => change({auto:{bias:v}})" @commit="v => commit({auto:{bias:v}})" />
          <div class="bias-ticks" aria-hidden="true"><span v-for="tick in 21" :key="tick" :class="{major:(tick-1)%5===0}"><i></i><small v-if="(tick-1)%5===0">{{(tick-1)*5}}</small></span></div>
        </div>
        <div class="core-section" :class="{unavailable:!snapshot.hybrid}">
          <div class="core-heading"><span>核心调度<small v-if="coreRuntime !== null"> · 已下发 {{coreRuntime}} W</small></span><b>{{draft.auto.coreSplit}}<small> W<span v-if="draft.auto.coreSplit > coreSliderMaximum">（范围外）</span></small></b></div>
          <div class="core-topology"><span v-if="!snapshot.hybrid">全核心 · 无混合核心</span><template v-else-if="topology.length"><span v-for="group in topology" :key="group">{{group}}</span></template><template v-else><span>大核 · 数量待读取</span><span>小核 · 数量待读取</span></template></div>
          <Slider class="bare-slider core-slider" :style="{'--core-split':`${(coreSliderMaximum ? coreSliderDisplay/coreSliderMaximum*100 : 0)}%`}" :model-value="coreSliderDisplay" :min="0" :max="coreSliderMaximum" :step="1" v-bind="performanceSliderRepeat" :disabled="!snapshot.hybrid" label="大小核心分界" :color="color" :gp-row="6" :gp-col="0" @update:model-value="v => change({auto:{coreSplit:v}})" @commit="v => commit({auto:{coreSplit:v}})" />
          <div class="auto-core-row">
            <div class="core-choice"><small>0–{{coreDisplay}} W</small><Dropdown class="core-active" :model-value="draft.auto.coreLow" :options="CONTROL_CORE_OPTIONS" :disabled="!snapshot.hybrid" :color="color" aria-label="低于分界的核心方案" :gp-row="7" :gp-col="0" @update:model-value="v => commit({auto:{coreLow:v as any}})" /></div>
            <div class="core-choice"><small>{{draft.auto.coreSplit >= coreMaximum ? '当前无高功耗区间' : coreDisplay+'–'+coreMaximum+' W'}}</small><Dropdown class="core-active" :model-value="draft.auto.coreHigh" :options="CONTROL_CORE_OPTIONS" :disabled="!snapshot.hybrid" :color="color" aria-label="高于分界的核心方案" :gp-row="7" :gp-col="1" @update:model-value="v => commit({auto:{coreHigh:v as any}})" /></div>
            <button class="toggle smt-button" :class="{active:snapshot.hyperThreadMode!=='off'}" :disabled="!snapshot.smtAvailable" :aria-pressed="snapshot.hyperThreadMode!=='off'" data-gp-row="7" data-gp-col="2" @click="toggleSmt">超线程 {{snapshot.smtAvailable?(snapshot.hyperThreadMode==='off'?'关闭':'开启'):'不支持'}}</button>
          </div>
        </div>
      </div>
      <div v-else class="control-bubble manual-bubble">
        <div class="manual-sliders">
          <div><div class="manual-frequency-heading"><span>CPU 频率</span><span>CPU 最小 <b>{{minimumState}}%</b></span></div><Slider :model-value="draft.manual.frequencyMHz" :min="800" :max="7200" :step="100" v-bind="performanceSliderRepeat" :disabled="draft.manual.unlimited" label="CPU 频率上限" :value-text="draft.manual.unlimited?'不限':`${(draft.manual.frequencyMHz/1000).toFixed(1)} GHz`" :color="color" :gp-row="4" :gp-col="0" @update:model-value="v => change({manual:{frequencyMHz:v}})" @commit="v => commit({manual:{frequencyMHz:v}})" /></div>
          <Slider :model-value="draft.manual.aggressiveness" :min="0" :max="100" :step="1" v-bind="performanceSliderRepeat" label="CPU 积极性" unit="%" :color="color" :gp-row="4" :gp-col="1" @update:model-value="v => change({manual:{aggressiveness:v}})" @commit="v => commit({manual:{aggressiveness:v}})" />
        </div>
        <div class="manual-actions-grid">
          <button class="toggle" :class="{active:draft.manual.unlimited}" :aria-pressed="draft.manual.unlimited" data-gp-row="5" data-gp-col="0" @click="commit({manual:{unlimited:!draft.manual.unlimited}})"><span>不限频率</span><b>{{draft.manual.unlimited?'开启':'关闭'}}</b></button>
          <button class="toggle" :class="{active:draft.manual.turbo}" :aria-pressed="draft.manual.turbo" data-gp-row="5" data-gp-col="1" @click="commit({manual:{turbo:!draft.manual.turbo}})"><span>睿频</span><b>{{draft.manual.turbo?'开启':'关闭'}}</b></button>
          <Dropdown class="core-active" :model-value="draft.manual.coreMode" :options="CONTROL_CORE_OPTIONS" :disabled="!snapshot.hybrid" :color="color" aria-label="大小核心" :gp-row="6" :gp-col="0" @update:model-value="v => commit({manual:{coreMode:v as any}})" />
          <button class="toggle" :class="{active:snapshot.hyperThreadMode!=='off'}" :disabled="!snapshot.smtAvailable" :aria-pressed="snapshot.hyperThreadMode!=='off'" data-gp-row="6" data-gp-col="1" @click="toggleSmt"><span>超线程</span><b>{{snapshot.smtAvailable?(snapshot.hyperThreadMode==='off'?'关闭':'开启'):'不支持'}}</b></button>
        </div>
      </div>
    </template>
    <p class="save-note" role="status" aria-live="polite">{{note}}</p>
  </section>
  <GameProfileRemoveDialog v-if="removal" :game-name="removal.gameName" :busy="removalBusy" :error="removalError" @cancel="cancelRemoval" @confirm="confirmRemoval" />
</template>
<style scoped>
.continuous-performance{--control-accent:var(--accent);--performance-density:1;--performance-space:calc(1px * var(--performance-density));--performance-control-h:calc((var(--btn-min-h,33px) + 2px) * var(--performance-density));--performance-track-h:calc(32px * var(--performance-density));--control-thumb:var(--control-accent);--control-thumb-ring:var(--control-accent);--surface:var(--bg-panel);--surface-alt:var(--bg-input);--border:color-mix(in srgb,var(--text) 15%,transparent);--text-secondary:var(--text-dim);--text-muted:var(--text-dim);display:grid;gap:calc(10 * var(--performance-space));min-width:0;width:100%;font-size:13px}.performance-compact{--performance-density:.8;line-height:1.1}
.continuous-performance.dc{--control-accent:var(--dc-accent)}
.supply-status{display:flex;gap:9px;align-items:center;min-height:calc(28 * var(--performance-space));color:var(--accent);font-size:15px}.supply-status svg{width:18px;height:18px}
.control-bubble{padding:calc(10 * var(--performance-space)) 12px;border:1px solid var(--border,#ffffff20);border-radius:12px;background:var(--surface,#171d27);min-width:0}
.control-modes,.power-modes{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.control-modes.three{grid-template-columns:repeat(3,minmax(0,1fr))}
.control-modes button,.power-modes button,.toggle{min-height:var(--performance-control-h);min-width:0;padding:calc(6 * var(--performance-space)) 10px;border:1px solid var(--border,#ffffff24);border-radius:var(--radius-ctrl,8px);background:var(--surface-alt,#ffffff08);color:var(--text-secondary,#b7bec7);font:inherit;font-size:var(--btn-font-size,12px);cursor:pointer}.control-modes button{padding-inline:4px}.power-modes button{display:flex;align-items:center;justify-content:center;gap:8px}.power-modes svg{width:17px;height:17px}
.control-modes .active,.power-modes .active,.toggle.active,.continuous-performance :deep(.core-active .dd-trigger:not(:disabled)){border-color:color-mix(in srgb,var(--control-accent) 55%,transparent);background:color-mix(in srgb,var(--control-accent) 12%,var(--surface,#171d27));color:var(--control-accent)}
.parameter-summary{display:grid;grid-template-columns:1.24fr 1.25fr .72fr;align-items:center;gap:10px;padding:calc(12 * var(--performance-space)) 12px}.parameter-summary span{font-size:12px;color:var(--text-secondary,#b7bec7);white-space:nowrap}.parameter-summary b{font-size:15px;color:var(--text,#edf0f6);white-space:nowrap}.parameter-summary small{font-size:12px;font-weight:400;color:var(--text-secondary)}.tdp-summary{display:flex;justify-content:space-between;align-items:flex-end;gap:5px;padding-right:12px;border-right:1px solid var(--border)}.tdp-summary>div{display:grid;gap:calc(5 * var(--performance-space))}.tdp-summary b{font-size:24px}.tdp-summary>div:first-child b{color:var(--control-accent)}.budget-arrow{padding-bottom:5px}.frequency-summary,.aggression-summary{display:grid;gap:calc(8 * var(--performance-space));text-align:center;min-width:0}.frequency-heading{display:flex;align-items:center;justify-content:center;gap:6px}.parameter-summary .turbo-badge{padding:calc(3 * var(--performance-space)) 5px;border-radius:4px;background:var(--surface-alt);font-size:11px}.parameter-summary .turbo-badge.lit{background:var(--control-accent);color:var(--bg-solid,#101722);font-weight:700}.aggression-summary b{font-size:18px}
.limits-bubble{padding-inline:16px}.slider-dropdown-row{display:grid;grid-template-columns:64px minmax(0,1fr) 106px;gap:12px;align-items:center;min-width:0;padding:calc(4 * var(--performance-space)) 0 calc(12 * var(--performance-space))}.slider-dropdown-row+.slider-dropdown-row{border-top:1px solid var(--border);padding-top:calc(12 * var(--performance-space));padding-bottom:calc(4 * var(--performance-space))}.metric-label{display:grid;gap:calc(6 * var(--performance-space))}.metric-label>span{font-size:13px;color:var(--text-secondary);white-space:nowrap}.metric-label strong{font-size:24px;line-height:calc(27 * var(--performance-space));font-weight:600;white-space:nowrap}.metric-label small{font-size:11px;font-weight:400;color:var(--text-secondary)}.range-body{position:relative;padding-block:calc(5 * var(--performance-space));min-width:0}.range-ends{position:absolute;left:0;right:0;top:calc(50% + 14 * var(--performance-space));display:flex;justify-content:space-between;font-size:10px;color:var(--text-muted,#99a1ad)}
.continuous-performance :deep(.slider){padding:0;min-width:0}.continuous-performance :deep(.slider-track-wrap){height:var(--performance-track-h)}.bare-slider :deep(.slider-head){display:none}.slider-dropdown-row :deep(.dd-trigger){height:var(--performance-control-h);min-height:var(--performance-control-h);padding-block:calc(6 * var(--performance-space))}.continuous-performance :deep(input[type=range]){height:var(--performance-track-h)}.continuous-performance :deep(.slider-track-bg),.continuous-performance :deep(.slider-fill){height:5px}.continuous-performance :deep(input[type=range]::-webkit-slider-thumb){width:18px;height:18px;border:2px solid var(--surface,#171d27);background:var(--control-thumb);box-shadow:0 0 0 2px var(--control-thumb-ring)}
.auto-bubble{padding:calc(12 * var(--performance-space)) 12px}.saving-section{padding-bottom:calc(12 * var(--performance-space));border-bottom:1px solid var(--border)}.saving-ticks{display:flex;justify-content:space-between;font-size:11px;color:var(--text-secondary);padding-top:calc(3 * var(--performance-space))}.bias-section{padding:calc(14 * var(--performance-space)) 0 calc(24 * var(--performance-space));border-bottom:1px solid var(--border)}.bias-labels{display:flex;justify-content:space-between;color:var(--control-accent);margin-bottom:calc(6 * var(--performance-space))}.bias-labels span:first-child{color:#b4a0ed}.bias-labels span:last-child{color:#63cfca}.bias-slider,.core-slider{--control-thumb:var(--text);--control-thumb-ring:var(--text-dim)}.bias-slider :deep(.slider-fill){display:none}.bias-slider :deep(.slider-track-bg){background:linear-gradient(90deg,#ac96e8,#50c8bf);border:0}.bias-ticks{display:flex;justify-content:space-between;height:calc(22 * var(--performance-space));margin-top:calc(2 * var(--performance-space))}.bias-ticks>span{position:relative;width:1px;height:6px;background:var(--text-muted,#778c99)}.bias-ticks>span.major{height:10px}.bias-ticks small{position:absolute;top:calc(12 * var(--performance-space));left:50%;transform:translateX(-50%);font-size:10px;color:var(--text-secondary)}.bias-ticks>span:first-child small{transform:none}.bias-ticks>span:last-child small{transform:translateX(-100%)}
.core-section{padding-top:calc(12 * var(--performance-space))}.core-heading{display:flex;justify-content:space-between;align-items:center;color:var(--text-secondary)}.core-heading b{color:var(--text);font-size:21px}.core-heading small{font-size:11px;font-weight:400}.core-topology{display:flex;justify-content:space-between;gap:8px;margin:calc(8 * var(--performance-space)) 0 calc(2 * var(--performance-space));flex-wrap:wrap;font-size:12px;color:var(--text-secondary)}.core-slider :deep(.slider-fill){display:none}.core-slider :deep(.slider-track-bg){background:linear-gradient(90deg,#5ed2c4 0 var(--core-split),#b6a1ef var(--core-split) 100%);border:0}.auto-core-row{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr) 86px;gap:8px;align-items:end;margin-top:calc(6 * var(--performance-space))}.core-choice{display:grid;gap:calc(6 * var(--performance-space));min-width:0}.core-choice>small{font-size:11px;color:var(--text-secondary);white-space:nowrap}.auto-core-row :deep(.dd-trigger){height:var(--performance-control-h);min-height:var(--performance-control-h);padding-block:calc(6 * var(--performance-space))}.smt-button{font-size:12px;white-space:nowrap;padding-inline:3px}.core-section.unavailable .core-heading,.core-section.unavailable .core-topology{opacity:.45}
.manual-sliders{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;align-items:end}.manual-frequency-heading{display:flex;justify-content:space-between;font-size:11px;color:var(--text-secondary);gap:5px;margin-bottom:6px}.manual-frequency-heading>span:first-child{display:none}.manual-actions-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:calc(8 * var(--performance-space)) 8px;align-items:stretch;margin-top:calc(12 * var(--performance-space))}.manual-actions-grid>*{width:100%;min-width:0;min-height:var(--performance-control-h)}.manual-actions-grid .toggle{display:flex;align-items:center;justify-content:space-between;gap:8px}.manual-actions-grid :deep(.dd-trigger){height:var(--performance-control-h);min-height:var(--performance-control-h);padding-block:calc(6 * var(--performance-space))}.continuous-performance :deep(.core-active .dd-trigger:disabled){opacity:.4;filter:saturate(.1)}button:disabled{opacity:.4;cursor:default}.save-note:empty{display:none}.save-note{min-height:calc(16 * var(--performance-space));margin:0;font-size:11px;color:var(--text-secondary,#b7bec7)}button:focus-visible{outline:2px solid var(--control-accent);outline-offset:3px}
.performance-compact :deep(.slider-head){margin-bottom:calc(8 * var(--performance-space))}
.profile-delete{min-height:34px;padding:5px 12px;border:1px solid color-mix(in srgb,var(--danger) 50%,transparent);border-radius:7px;background:color-mix(in srgb,var(--danger) 12%,var(--bg-input));color:var(--danger);font:inherit;font-size:13px;font-weight:650;white-space:nowrap;cursor:pointer}.profile-delete:disabled{opacity:.5}
</style>

<style scoped>
/* Accepted integrated console. Only formal global/dedicated panels use this layout. */
.integrated-switchboard.simulation-switchboard{display:contents}
.integrated-ui{--performance-control-h:32px;--performance-track-h:28px;--performance-density:1;--performance-space:1px;font-size:14px;color:var(--text);line-height:1.25;gap:9px}
.integrated-heading{display:flex;align-items:center;justify-content:space-between;gap:8px;min-height:34px}.integrated-title{display:flex;align-items:center;gap:8px;min-width:0}.integrated-title>svg{width:21px;height:21px;color:var(--control-accent)}.integrated-title h1{margin:0;font-size:20px;line-height:1.2;font-weight:650;letter-spacing:.2px;white-space:nowrap}.integrated-supply{display:flex;align-items:center;gap:5px;font-size:11px;color:var(--text-dim);white-space:nowrap}.integrated-supply i{width:5px;height:5px;border-radius:50%;background:var(--ok,#67c69f)}.integrated-supply i.battery{background:var(--dc-accent)}.simulation-entry{display:flex;align-items:center;gap:5px;padding:6px 8px;border:1px solid var(--border);border-radius:8px;background:color-mix(in srgb,var(--bg-input) 65%,transparent);color:var(--text-dim);font:inherit;font-size:11px;min-height:30px;white-space:nowrap;cursor:pointer}.simulation-entry svg{width:13px;height:13px}.simulation-entry>span{color:var(--control-accent);margin-left:3px}
.integrated-ui .integrated-switchboard{display:grid;grid-template-columns:1.03fr 1fr;gap:9px}.integrated-ui .control-modes,.integrated-ui .power-modes{padding:3px;gap:3px;border-radius:10px;background:color-mix(in srgb,var(--bg-solid) 72%,var(--bg-panel));border-color:var(--border)}.integrated-ui .control-modes button,.integrated-ui .power-modes button{min-height:32px;padding:5px 4px;border:1px solid transparent;border-radius:7px;font-size:12px;color:var(--text-dim);background:transparent}.integrated-ui .power-modes button{gap:5px}.integrated-ui .power-modes svg{width:15px;height:15px}.integrated-ui .control-modes .active,.integrated-ui .power-modes .active{border-color:color-mix(in srgb,var(--control-accent) 82%,transparent);color:var(--text);background:color-mix(in srgb,var(--control-accent) 34%,var(--bg-panel));font-weight:700;box-shadow:inset 0 1px 0 color-mix(in srgb,var(--text) 10%,transparent)}
.integrated-ui .parameter-summary{grid-template-columns:repeat(3,minmax(0,1fr));gap:0;padding:5px 0 4px;border:0;background:transparent;border-radius:0;min-width:0}.integrated-ui .parameter-summary>div{min-width:0;display:grid;gap:7px;text-align:center;justify-content:stretch;align-items:center;border:0;padding:0 5px}.integrated-ui .parameter-summary>div+div{border-left:1px solid var(--border)}.integrated-ui .parameter-summary span{font-size:12px}.integrated-ui .parameter-summary b{font-size:16px;font-weight:650;line-height:1.3;white-space:nowrap;font-variant-numeric:tabular-nums;color:var(--text)}.integrated-ui .parameter-summary small{font-size:11px;font-weight:400;color:var(--text-dim)}.integrated-ui .range-summary b{color:var(--control-accent)}.integrated-ui .frequency-heading{gap:4px;min-width:0}.integrated-ui .parameter-summary .turbo-badge{padding:2px 4px;font-size:9px;font-weight:600;border-radius:4px;line-height:1.2}.integrated-ui .parameter-summary .turbo-badge.lit{background:color-mix(in srgb,var(--control-accent) 14%,var(--bg-input));color:var(--control-accent)}
.integrated-ui .control-bubble{border-radius:13px}.integrated-ui .limits-bubble{padding:2px 14px}.integrated-ui .slider-dropdown-row{grid-template-columns:62px minmax(0,1fr) 104px;gap:10px;min-height:77px;padding:11px 0 14px}.integrated-ui .slider-dropdown-row+.slider-dropdown-row{padding-top:11px;padding-bottom:14px}.integrated-ui .metric-label{gap:5px}.integrated-ui .metric-label>span{font-size:12px}.integrated-ui .metric-label strong{font-size:27px;line-height:1.1;letter-spacing:-.5px;font-weight:600}.integrated-ui .metric-label small{font-size:11px;letter-spacing:0}.integrated-ui .range-body{padding-block:0}.integrated-ui .range-ends{top:calc(50% + 14px);font-size:10px}.integrated-ui :deep(.slider-track-wrap),.integrated-ui :deep(input[type=range]){height:28px}.integrated-ui :deep(.slider-track-bg),.integrated-ui :deep(.slider-fill){height:4px;border:0}.integrated-ui :deep(input[type=range]::-webkit-slider-thumb){width:16px;height:16px;border:2px solid var(--surface);box-shadow:0 0 0 1.5px var(--control-accent)}.integrated-ui :deep(.dd-trigger){height:32px;min-height:32px;padding:5px 9px;font-size:12px;border-radius:8px;background:color-mix(in srgb,var(--bg-input) 70%,transparent)}.integrated-ui :deep(.slider-label){font-size:13px;font-weight:500}.integrated-ui :deep(.slider-val){font-size:12px;font-weight:600}.integrated-ui :deep(.slider-head){margin-bottom:3px}
.integrated-ui .auto-bubble,.integrated-ui .manual-bubble{padding:11px 14px;background:color-mix(in srgb,var(--bg-panel) 80%,transparent)}.integrated-ui .saving-section{padding-bottom:8px}.integrated-ui .saving-ticks{font-size:11px;padding-top:0}.integrated-ui .bias-section{padding:10px 0 12px}.integrated-ui .bias-labels{font-size:13px;margin-bottom:2px}.integrated-ui .bias-ticks{height:16px;margin-top:0}.integrated-ui .bias-ticks>span{height:3px}.integrated-ui .bias-ticks>span.major{height:5px}.integrated-ui .bias-ticks small{top:7px;font-size:10px}.integrated-ui .core-section{padding-top:8px}.integrated-ui .core-heading{font-size:12px;min-height:22px}.integrated-ui .core-heading b{font-size:18px}.integrated-ui .core-heading small{font-size:11px}.integrated-ui .core-topology{font-size:11px;margin:4px 0 0}.integrated-ui .auto-core-row{grid-template-columns:repeat(2,minmax(0,1fr)) 91px;gap:7px;margin-top:4px}.integrated-ui .core-choice{gap:4px}.integrated-ui .core-choice>small{font-size:11px}.integrated-ui .smt-button{font-size:11px;padding-inline:5px}.integrated-ui .bias-slider :deep(input[type=range]::-webkit-slider-thumb),.integrated-ui .core-slider :deep(input[type=range]::-webkit-slider-thumb){box-shadow:0 0 0 1.5px var(--text-dim)}.integrated-ui .core-section.unavailable{display:grid;grid-template-columns:1fr auto;gap:7px;align-items:center}.integrated-ui .core-section.unavailable .core-heading,.integrated-ui .core-section.unavailable .core-slider,.integrated-ui .core-section.unavailable .core-choice{display:none}.integrated-ui .core-section.unavailable .core-topology{margin:0;color:var(--text-dim);opacity:.6}.integrated-ui .core-section.unavailable .auto-core-row{display:block;margin:0}.integrated-ui .core-section.unavailable .smt-button{min-width:92px;min-height:30px}
.integrated-ui .manual-sliders{gap:14px}.integrated-ui .manual-frequency-heading{font-size:11px;margin-bottom:5px}.integrated-ui .manual-actions-grid{gap:8px;margin-top:11px}.integrated-ui .manual-actions-grid .toggle{min-height:32px;padding:6px 9px;font-size:12px}.integrated-ui .save-note{font-size:11px;min-height:16px}.integrated-ui :deep(.core-active .dd-trigger:not(:disabled)){color:var(--control-accent)}.integrated-ui button:focus-visible{outline:2px solid var(--control-accent);outline-offset:2px}
.integrated-ui .save-note:empty{display:block;visibility:hidden}
</style>
