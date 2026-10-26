<script setup lang="ts">
import { computed, nextTick, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, shallowRef, watch } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { EDIT_REPEAT_BASE_MS } from '@/gamepad/sliderRepeat';
import { CalibrationEditor, calibrationApplyAvailable, createControllerCalibrationPort, type CalibrationEditorState, type ControllerCalibrationPort } from '@/bridge/controllerCalibration';
import { sameSource, type AxisCalibrationConfig, type StickOutputShape, type StickSide } from '@/bridge/controllerCalibrationContract';

const props = defineProps<{ port?: ControllerCalibrationPort; disabled?: boolean }>();
const editor = new CalibrationEditor(props.port || createControllerCalibrationPort(), value => { state.value = value; });
const state = shallowRef<CalibrationEditorState>(editor.value);
const root = ref<HTMLElement | null>(null);
const stickSelect = ref<InstanceType<typeof Dropdown> | null>(null);
const triggerSelect = ref<InstanceType<typeof Dropdown> | null>(null);
const shapeSelect = ref<InstanceType<typeof Dropdown> | null>(null);
const stickSide = ref<StickSide>('left');
const triggerSide = ref<'lt' | 'rt'>('lt');
const stick = computed(() => state.value.draft.sticks[stickSide.value]);
const trigger = computed(() => state.value.draft.triggers[triggerSide.value]);
const gateLocked = computed(() => !!props.disabled || state.value.loading || state.value.applying || !state.value.snapshot || !state.value.available);
const locked = computed(() => gateLocked.value || state.value.sampling);
const childrenLocked = computed(() => locked.value || !state.value.draft.enabled);
const sticksSupported = computed(() => state.value.snapshot?.capabilities.sticks === 'standard-independent');
const triggersSupported = computed(() => state.value.snapshot?.capabilities[triggerSide.value] === 'independent-analog');
const localSupported = computed(() => state.value.snapshot?.capabilities.localWrite === 'available');
const virtualSupported = computed(() => state.value.snapshot?.capabilities.virtualWrite === 'available');
const canApply = computed(() => !locked.value && state.value.dirty &&
  (state.value.draft.enabled || state.value.snapshot?.config.enabled) && calibrationApplyAvailable(state.value.snapshot, state.value.draft));
const stickParametersLocked = computed(() => locked.value || !state.value.draft.enabled || !state.value.draft.sticksEnabled || !stick.value.enabled || !sticksSupported.value);
const triggerParametersLocked = computed(() => locked.value || !state.value.draft.enabled || !state.value.draft.triggersEnabled || !triggersSupported.value);
const stickOptions = [{ value: 'left', label: '左摇杆' }, { value: 'right', label: '右摇杆' }];
const triggerOptions = [{ value: 'lt', label: '左扳机 LT' }, { value: 'rt', label: '右扳机 RT' }];
const shapeOptions = [{ value: 'original', label: '保持原样' }, { value: 'circle-to-square', label: '圆形 → 方形' }, { value: 'square-to-circle', label: '方形 → 圆形' }];
const axes = computed(() => state.value.available ? state.value.snapshot?.axes : undefined);
const rawStick = computed(() => axes.value?.raw[stickSide.value] || null);
const virtualStick = computed(() => axes.value?.virtual?.[stickSide.value] || null);
const localStick = computed(() => axes.value?.local?.[stickSide.value] || null);
const rawTrigger = computed(() => axes.value?.raw[triggerSide.value] ?? null);
const centered = computed(() => stick.value.centerX !== 0 || stick.value.centerY !== 0);
const signed = (value: number) => (value >= 0 ? '+' : '−') + Math.abs(value).toFixed(3);
const magnitude = (value: { x: number; y: number } | null) => value ? (Math.hypot(value.x, value.y) * 100).toFixed(1) + '%' : '—';
const routeText = (target: 'virtual' | 'local') => {
  const route = state.value.available ? state.value.snapshot?.routes[target] : null;
  if (!route) return '尚未取得状态';
  return ({ bypass: '保持原始', pending: '等待生效确认', active: '当前通路已确认', unsupported: '当前通路不支持', rejected: '未应用', stopped: '通路未运行' })[route.state];
};
function updateGate(edit: (config: AxisCalibrationConfig) => void) { if (childrenLocked.value) return; editor.cancelCenter(); editor.update(edit); }
function setMaster(enabled: boolean) { if (gateLocked.value) return; retireEdits(true); editor.setEnabled(enabled); }
function selectStick(value: string | number) { if (!childrenLocked.value && (value === 'left' || value === 'right')) stickSide.value = value; }
function selectTrigger(value: string | number) { if (!childrenLocked.value && (value === 'lt' || value === 'rt')) triggerSide.value = value; }
function updateShape(value: string | number) { if (!stickParametersLocked.value) editor.update(config => { config.sticks[stickSide.value].outputShape = value as StickOutputShape; }); }
function updateStick(key: 'innerDeadzonePct' | 'outerDeadzonePct' | 'antiDeadzonePct', value: number) {
  if (!stickParametersLocked.value) editor.update(config => { config.sticks[stickSide.value][key] = value; });
}
function triggerChange(key: 'fullScalePct' | 'startDeadzonePct', value: number) {
  if (triggerParametersLocked.value) return;
  editor.update(config => {
    const trigger = config.triggers[triggerSide.value]; trigger[key] = value;
    if (key === 'fullScalePct') trigger.startDeadzonePct = Math.min(trigger.startDeadzonePct, value - 1);
    else trigger.fullScalePct = Math.max(trigger.fullScalePct, value + 1);
  });
}
// Reuse Slider's existing edit boundary. A commits the draft only; B restores both linked trigger values.
let sliderDraft: AxisCalibrationConfig | null = null;
let sliderInput: HTMLInputElement | null = null;
function beginDraft(event: Event) {
  if (childrenLocked.value) { event.stopPropagation(); retireEdits(); return; }
  sliderInput = event.target instanceof HTMLInputElement ? event.target : null;
  sliderDraft = structuredClone(state.value.draft);
}
function cancelDraft() { const draft = sliderDraft; retireEdits(); if (draft && !childrenLocked.value) editor.restoreDraft(draft); }
function commitDraft() { sliderDraft = null; sliderInput = null; }
function retireEdits(restore = false, focusMaster = false) {
  const draft = sliderDraft, oldInput = sliderInput; sliderDraft = null; sliderInput = null;
  if (restore && draft && state.value.draft.enabled) editor.restoreDraft(draft);
  const panel = root.value;
  const ownedFocus = !!panel?.contains(document.activeElement) || !!panel?.querySelector('.dd-open');
  // The shared owner resets native cadence and every local range through its existing event.
  if (panel) {
    // KeepAlive can detach the panel before blur/deactivated; its retirement must still reach the existing owner.
    const target = panel.isConnected ? panel : window;
    target.dispatchEvent(new CustomEvent('gp:slider-edit-invalidated', { bubbles: true, detail: { panel, input: oldInput } }));
  }
  stickSelect.value?.closeMenu(); triggerSelect.value?.closeMenu(); shapeSelect.value?.closeMenu();
  if (focusMaster && ownedFocus) void nextTick(() => {
    if (!state.value.draft.enabled && root.value?.isConnected) focusGamepadElement(root.value.querySelector<HTMLElement>('.calibration-master [role="switch"]'));
  });
}
let stopVisibility: (() => void) | null = null;
let nativeBlurSubscribed = false;
function onNativeBlur() { editor.tracePage('native-blur-local-edit'); retireEdits(true); }
function mount(reason = 'mount') {
  if (!nativeBlurSubscribed) {
    window.addEventListener('ipc:window.blur', onNativeBlur);
    nativeBlurSubscribed = true;
  }
  if (!stopVisibility) stopVisibility = onUiVisibilityChange(value => editor.setObservationVisible(value.visible));
  void editor.mount(reason);
}
function dispose(reason = 'dispose') {
  retireEdits();
  if (nativeBlurSubscribed) {
    window.removeEventListener('ipc:window.blur', onNativeBlur);
    nativeBlurSubscribed = false;
  }
  stopVisibility?.(); stopVisibility = null; editor.dispose(reason);
}
watch(stickSide, () => { sliderDraft = null; editor.cancelCenter(); });
watch(triggerSide, () => { sliderDraft = null; });
watch(() => props.disabled, disabled => { if (disabled) { retireEdits(); editor.cancelCenter(); } });
watch(() => state.value.draft.enabled, enabled => { if (!enabled) retireEdits(false, true); }, { flush: 'sync' });
watch(() => state.value.snapshot?.source, (current, previous) => {
  if (!(current == null && previous == null) && !sameSource(current || null, previous || null)) retireEdits();
}, { flush: 'sync' });
watch(gateLocked, unavailable => { if (unavailable) retireEdits(); }, { flush: 'sync' });
watch(() => state.value.applying, (applying, previous) => {
  if (previous && !applying && !state.value.draft.enabled) void nextTick(() => {
    if (root.value?.isConnected && !gateLocked.value) focusGamepadElement(root.value.querySelector<HTMLElement>('.calibration-master [role="switch"]'));
  });
});
onMounted(() => mount('mounted')); onActivated(() => mount('activated')); onDeactivated(() => dispose('deactivated')); onBeforeUnmount(() => dispose('before-unmount'));
</script>

<template>
  <div ref="root" class="calibration-panel" aria-label="手柄校对" :aria-busy="state.loading || state.applying">
    <section class="card calibration-scope" aria-label="应用范围">
      <div class="calibration-master"><Toggle :model-value="state.draft.enabled" label="手柄校对总开关" :gp-row="0" :gp-col="0" :disabled="gateLocked" @update:model-value="setMaster" /></div>
      <div><Toggle :model-value="state.draft.applyToVirtual" label="应用到虚拟手柄" :gp-row="1" :gp-col="0" :disabled="childrenLocked || (!virtualSupported && !state.draft.applyToVirtual)" @update:model-value="v => editor.update(c => { c.applyToVirtual = v; })" /></div>
      <div><Toggle :model-value="state.draft.applyToLocal" label="应用到本机手柄" :gp-row="1" :gp-col="1" :disabled="childrenLocked || (!localSupported && !state.draft.applyToLocal)" @update:model-value="v => editor.update(c => { c.applyToLocal = v; })" /></div>
      <p class="hint">调节后点击应用；本机通路仅在设备支持时可用。</p>
    </section>
    <section class="card calibration-stick" aria-label="摇杆调节">
      <div class="calibration-heading"><h3><InlineIcon name="gamepad" />摇杆</h3><Toggle :model-value="state.draft.sticksEnabled" label="摇杆校对" :gp-row="3" :gp-col="0" :disabled="childrenLocked" @update:model-value="v => updateGate(c => { c.sticksEnabled = v; })" /><Dropdown ref="stickSelect" :model-value="stickSide" @update:model-value="selectStick" :options="stickOptions" aria-label="选择摇杆" width="126px" :disabled="childrenLocked" :gp-row="3" :gp-col="1" /></div>
      <div class="calibration-center"><div><strong>自动校准摇杆轴心</strong><p class="hint">松开后采样，修正当前摇杆中心。</p></div><Toggle :key="stickSide + 'enabled'" :model-value="stick.enabled" :label="stickSide === 'left' ? '启用左摇杆' : '启用右摇杆'" :gp-row="4" :gp-col="0" :disabled="childrenLocked" @update:model-value="v => updateGate(c => { c.sticks[stickSide].enabled = v; })" /><button type="button" class="btn" data-calibrate :disabled="gateLocked || !state.draft.enabled || !state.draft.sticksEnabled || !stick.enabled || !sticksSupported || !state.snapshot?.source" :data-gp-row="4" :data-gp-col="1" @click="editor.startCenter(stickSide)">{{ state.sampling ? '取消校准' : centered ? '重新校准' : '开始校准' }}</button></div>
      <div class="calibration-axis"><span>X <b>{{ signed(stick.centerX) }}</b></span><span>Y <b>{{ signed(stick.centerY) }}</b></span><span :class="{ calibrated: centered }">{{ state.sampling ? '正在采样' : centered ? '已调整轴心' : '未调整轴心' }}</span></div>
      <div class="calibration-stick-controls">
        <div class="calibration-slider" data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="stickSide + 'inner'" :model-value="stick.innerDeadzonePct" label="内圈死区" :min="0" :max="25" :step="1" unit="%" :value-text="stick.innerDeadzonePct + '%'" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="stickParametersLocked" :gp-row="5" :gp-col="0" @update:model-value="v => updateStick('innerDeadzonePct', v)" @commit="commitDraft" /><p class="hint">中心 {{ stick.innerDeadzonePct }}% 内保持零输出。</p></div>
        <div class="calibration-slider" data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="stickSide + 'outer'" :model-value="stick.outerDeadzonePct" label="外圈死区" :min="0" :max="25" :step="1" unit="%" :value-text="stick.outerDeadzonePct + '%'" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="stickParametersLocked" :gp-row="5" :gp-col="1" @update:model-value="v => updateStick('outerDeadzonePct', v)" @commit="commitDraft" /><p class="hint">推到 {{ 100 - stick.outerDeadzonePct }}% 有效半径时，达到满输出。</p></div>
        <div class="calibration-slider" data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="stickSide + 'anti'" :model-value="stick.antiDeadzonePct" label="反死区" :min="0" :max="25" :step="1" unit="%" :value-text="stick.antiDeadzonePct + '%'" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="stickParametersLocked" :gp-row="6" :gp-col="0" @update:model-value="v => updateStick('antiDeadzonePct', v)" @commit="commitDraft" /><p class="hint">越过内圈后，从 {{ stick.antiDeadzonePct }}% 输出起步。</p></div>
        <div class="calibration-shape"><label class="calibration-shape-label">形状转换</label><Dropdown ref="shapeSelect" :key="stickSide + 'shape'" :model-value="stick.outputShape || 'original'" :options="shapeOptions" :disabled="stickParametersLocked" aria-label="形状转换" :gp-row="6" :gp-col="1" @update:model-value="updateShape" /><p class="hint">{{ stick.outputShape === 'circle-to-square' ? '圆形输入映射为方形输出。' : stick.outputShape === 'square-to-circle' ? '方形输入映射为圆形输出。' : '保持摇杆原始输出形状。' }}</p></div>
      </div>
    </section>
    <section class="card calibration-trigger" aria-label="扳机调节">
      <div class="calibration-heading"><h3>扳机</h3><Toggle :model-value="state.draft.triggersEnabled" label="扳机校对" :gp-row="7" :gp-col="0" :disabled="childrenLocked" @update:model-value="v => updateGate(c => { c.triggersEnabled = v; })" /><Dropdown ref="triggerSelect" :model-value="triggerSide" @update:model-value="selectTrigger" :options="triggerOptions" aria-label="选择扳机" width="126px" :disabled="childrenLocked" :gp-row="7" :gp-col="1" /></div>
      <div class="calibration-trigger-controls">
        <div class="calibration-slider" data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="triggerSide + 'max'" :model-value="trigger.fullScalePct" label="扳机键最大值" :min="1" :max="100" :step="1" unit="%" :value-text="trigger.fullScalePct + '%'" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="triggerParametersLocked" :gp-row="8" :gp-col="0" @update:model-value="v => triggerChange('fullScalePct', v)" @commit="commitDraft" /><p class="hint">输入达到 {{ trigger.fullScalePct }}% 行程时，输出为 100%。</p></div>
        <div class="calibration-slider" data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="triggerSide + 'start'" :model-value="trigger.startDeadzonePct" label="扳机起始死区" :min="0" :max="99" :step="1" unit="%" :value-text="trigger.startDeadzonePct + '%'" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="triggerParametersLocked" :gp-row="8" :gp-col="1" @update:model-value="v => triggerChange('startDeadzonePct', v)" @commit="commitDraft" /><p class="hint">前 {{ trigger.startDeadzonePct }}% 行程不响应。</p></div>
      </div>
      <div class="calibration-trigger-readout"><span>{{ triggerSide === 'lt' ? 'LT' : 'RT' }} 原始输入</span><b>{{ rawTrigger === null ? '—' : Math.round(rawTrigger * 100) + '%' }}</b><div class="calibration-meter"><i :style="{ width: (rawTrigger ?? 0) * 100 + '%' }" /></div></div>
    </section>
    <section class="card calibration-live" aria-label="输入与输出"><div class="calibration-heading"><h3>输入与输出</h3></div>
      <div class="calibration-signal-view"><svg class="calibration-graph" viewBox="0 0 200 200" role="img" aria-label="当前摇杆输入输出，只读">
        <circle cx="100" cy="100" r="76" class="calibration-circle" /><rect v-if="stick.outputShape === 'circle-to-square'" x="24" y="24" width="152" height="152" rx="1" class="calibration-square" /><circle cx="100" cy="100" :r="76 * (1 - stick.outerDeadzonePct / 100)" class="calibration-outer" /><circle cx="100" cy="100" r="38" class="calibration-grid" /><circle cx="100" cy="100" :r="76 * stick.innerDeadzonePct / 100" class="calibration-inner" /><path d="M16 100H184M100 16V184" class="calibration-grid" />
        <circle v-if="rawStick" :cx="100 + rawStick.x * 76" :cy="100 - rawStick.y * 76" r="4" class="calibration-raw-dot" /><circle v-if="localStick" :cx="100 + localStick.x * 76" :cy="100 - localStick.y * 76" r="6" class="calibration-local-dot" /><circle v-if="virtualStick" :cx="100 + virtualStick.x * 76" :cy="100 - virtualStick.y * 76" r="3.5" class="calibration-virtual-dot" />
      </svg><div class="calibration-routes"><div><span><i class="calibration-legend raw" />原始输入</span><b>{{ magnitude(rawStick) }}</b></div><div><span><i class="calibration-legend virtual" />虚拟手柄</span><b>{{ magnitude(virtualStick) }}</b><small>{{ routeText('virtual') }}</small></div><div><span><i class="calibration-legend local" />本机手柄</span><b>{{ magnitude(localStick) }}</b><small>{{ routeText('local') }}</small></div></div></div>
    </section>
    <div class="calibration-bottom"><p class="hint" role="status" aria-live="polite">{{ state.feedback }}</p><div class="calibration-bottom-actions"><button type="button" class="btn" :data-gp-row="9" :data-gp-col="0" :disabled="childrenLocked" @click="editor.reset()">恢复默认</button><button type="button" class="btn" data-calibration-apply :class="{ active: state.dirty }" :disabled="!canApply" :data-gp-row="9" :data-gp-col="1" @click="editor.apply()">应用</button></div></div>
  </div>
</template>

<style scoped>
.calibration-panel{display:grid;gap:10px;min-width:0}.calibration-panel .card{padding:12px!important}.calibration-panel h3{font-size:15px!important;margin:0!important}.calibration-panel .hint{font-size:11px!important;margin:4px 0 0;line-height:1.5!important}.calibration-heading{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:12px}.calibration-heading .inline-icon{color:var(--text-dim)}.calibration-scope{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:4px 12px}.calibration-scope>div{min-width:0}.calibration-scope>.calibration-master{grid-column:1/-1;border-bottom:1px solid var(--console-line);padding-bottom:8px;margin-bottom:4px}.calibration-scope .hint{grid-column:1/-1;padding-top:5px;border-top:1px solid var(--console-line);margin-top:6px}.calibration-center{display:flex;align-items:center;justify-content:space-between;gap:10px}.calibration-center strong{font-size:13px;font-weight:600}.calibration-panel .btn{font:inherit;font-size:12px!important;white-space:nowrap}.calibration-axis{display:flex;align-items:center;justify-content:space-between;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt);margin-top:10px;padding:9px 10px;gap:7px;font-size:11px;color:var(--text-dim)}.calibration-axis b{font-family:Consolas,monospace;font-size:12px;font-weight:500;color:var(--text);margin-left:6px}.calibrated{color:var(--accent)}.calibration-progress{height:3px;background:var(--bg-input);border-radius:3px;margin-top:7px;overflow:hidden}.calibration-progress i{display:block;height:100%;background:var(--accent)}.calibration-slider{margin-top:15px;padding-top:12px;border-top:1px solid var(--console-line)}.calibration-slider:first-of-type{margin-top:0}.calibration-stick-controls,.calibration-trigger-controls{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px 14px;align-items:start}.calibration-stick-controls{margin-top:14px}.calibration-stick-controls .calibration-slider,.calibration-trigger-controls .calibration-slider{margin:0;padding-top:12px;min-width:0}.calibration-shape{min-width:0;border-top:1px solid var(--console-line);padding-top:12px}.calibration-shape-label{display:block;font-size:13px;margin-bottom:8px}.calibration-square{fill:none;stroke:var(--accent-border);stroke-width:1;stroke-dasharray:3 3}.calibration-panel :deep(.slider-label),.calibration-panel :deep(.slider-val){font-size:13px}.calibration-trigger-readout{display:grid;grid-template-columns:1fr auto;gap:7px;font-size:11px;color:var(--text-dim);margin-top:14px}.calibration-trigger-readout b{color:var(--accent);font-family:Consolas,monospace;font-weight:500}.calibration-meter{grid-column:1/-1;height:4px;border-radius:5px;background:var(--bg-input);overflow:hidden}.calibration-meter i{display:block;height:100%;background:var(--accent)}.calibration-chip{font-size:10px;color:var(--text-dim);padding:3px 7px;border:1px solid var(--console-line);border-radius:5px}.calibration-signal-view{display:grid;grid-template-columns:148px minmax(0,1fr);gap:13px;align-items:center}.calibration-graph{width:148px;height:148px;touch-action:none;user-select:none}.calibration-circle{fill:var(--console-surface-alt);stroke:var(--console-line);stroke-width:1}.calibration-grid{fill:none;stroke:var(--console-line);stroke-width:1}.calibration-outer{fill:var(--console-surface);stroke:var(--accent-border);stroke-width:1;stroke-dasharray:3 3}.calibration-inner{fill:var(--accent-surface);stroke:var(--accent-border);stroke-width:1}.calibration-raw-dot{fill:var(--text-dim);stroke:var(--bg-panel);stroke-width:1}.calibration-virtual-dot{fill:var(--accent);stroke:var(--bg-panel);stroke-width:1}.calibration-local-dot{fill:none;stroke:var(--accent-2);stroke-width:1.5}.calibration-routes{display:grid;gap:9px}.calibration-routes>div{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:3px;font-size:11px;color:var(--text-dim)}.calibration-routes b{color:var(--text);font-family:Consolas,monospace;font-size:12px;font-weight:500}.calibration-routes small{font-size:10px;grid-column:1/-1}.calibration-legend{display:inline-block;height:5px;width:5px;border-radius:50%;margin-right:5px;background:var(--text-dim);vertical-align:middle}.calibration-legend.virtual{background:var(--accent)}.calibration-legend.local{background:var(--accent-2)}.calibration-bottom{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:8px}.calibration-bottom{flex-wrap:wrap}.calibration-bottom-actions{display:flex;align-items:center;gap:8px;margin-left:auto}.calibration-bottom p{flex-basis:100%;min-width:0;overflow-wrap:anywhere}.calibration-panel :deep(.dd-trigger){min-height:33px;font-size:12px}.calibration-panel :deep(.toggle-label){font-size:13px}.calibration-panel :deep(.toggle-row){min-height:36px}
.calibration-panel .btn:disabled{opacity:.45;cursor:default}
</style>
