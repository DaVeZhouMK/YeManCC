<script setup lang="ts">
import { computed, nextTick, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, shallowRef, watch } from 'vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { useStickPresentation } from '@/bridge/stickPresentation';
import { EDIT_REPEAT_BASE_MS } from '@/gamepad/sliderRepeat';
import { CalibrationEditor, calibrationApplyAvailable, createControllerCalibrationPort, type CalibrationEditorState, type ControllerCalibrationPort } from '@/bridge/controllerCalibration';
import { mapCalibrationStick, mapCalibrationTrigger, sameSource, type AxisCalibrationConfig, type StickOutputShape, type StickSide } from '@/bridge/controllerCalibrationContract';

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
const shapeOptions = [{ value: 'original', label: '保持原样' }, { value: 'circle-to-square', label: '方形输出', sub: '圆形 → 方形' }, { value: 'square-to-circle', label: '圆形输出', sub: '方形 → 圆形' }];
const axes = computed(() => state.value.available ? state.value.snapshot?.axes : undefined);
const rawStick = computed(() => axes.value?.raw[stickSide.value] || null);
const rawTrigger = computed(() => axes.value?.raw[triggerSide.value] ?? null);
const centered = computed(() => stick.value.centerX !== 0 || stick.value.centerY !== 0);
const centerMeasured = computed(() => {
  const receipt = state.value.centerReceipt;
  return state.value.available && !!receipt && receipt.side === stickSide.value &&
    sameSource(receipt.source, state.value.snapshot?.source || null) &&
    receipt.centerX === stick.value.centerX && receipt.centerY === stick.value.centerY;
});
const signed = (value: number) => (value >= 0 ? '+' : '−') + Math.abs(value).toFixed(3);
const magnitude = (value: { x: number; y: number } | null) => value ? (Math.hypot(value.x, value.y) * 100).toFixed(1) + '%' : '—';
const routeText = (target: 'virtual' | 'local') => {
  const route = state.value.available ? state.value.snapshot?.routes[target] : null;
  if (!route) return '尚未取得状态';
  if (target === 'local' && !localSupported.value) return '未提供本机修正通路';
  if (route.reason === 'master-disabled') return '修正已关闭';
  if (route.reason === 'no-saved-source-bound-calibration') return '未启用修正 · 无提交回执';
  if (route.reason === 'profile-belongs-to-another-or-unconfirmed-source' || route.reason === 'physical-source-unconfirmed') return '等待当前实体手柄来源';
  if (route.reason === 'current-local-submit-accepted-expected-neutral') return '当前通路已提交零帧';
  if (route.reason === 'no-verified-local-axis-writer') return '未提供本机修正通路';
  return ({ bypass: '保持原始', pending: '等待生效确认', active: '当前通路已确认', unsupported: '当前通路不支持', rejected: '未应用', stopped: '通路未运行' })[route.state];
};
// These projections use current pre-calibration native samples and the unsaved draft.
// Actual route values below always come from native snapshot axes and receipts.
const correctionSelected = computed(() => state.value.draft.enabled && (state.value.draft.applyToVirtual || state.value.draft.applyToLocal));
const stickCorrectionActive = computed(() => correctionSelected.value && state.value.draft.sticksEnabled && stick.value.enabled && sticksSupported.value);
const triggerCorrectionActive = computed(() => correctionSelected.value && state.value.draft.triggersEnabled && triggersSupported.value);
const previewStick = computed(() => {
  const value = rawStick.value;
  if (!value) return null;
  return stickCorrectionActive.value ? mapCalibrationStick(value, stick.value) : { ...value };
});
const sampleReceivedAt = shallowRef(-Infinity);
watch(() => {
  const sample = state.value.snapshot?.sample;
  return sample ? JSON.stringify([sample.source,sample.readSequence,sample.readStartedTick,sample.readCompletedTick]) : '';
}, key => { sampleReceivedAt.value = key ? performance.now() : -Infinity; }, { immediate: true, flush: 'sync' });
const displayIdentity = computed(() => JSON.stringify([stickSide.value,state.value.snapshot?.source]));
const displayedRaw = useStickPresentation(() => rawStick.value, () => state.value.available,
  () => displayIdentity.value, 100, () => sampleReceivedAt.value);
const displayedPreview = useStickPresentation(() => previewStick.value, () => state.value.available,
  () => displayIdentity.value, 100, () => sampleReceivedAt.value);
const previewTrigger = computed(() => rawTrigger.value === null ? null : triggerCorrectionActive.value ? mapCalibrationTrigger(rawTrigger.value, trigger.value) : rawTrigger.value);
const squareOutput = computed(() => stickCorrectionActive.value && stick.value.outputShape === 'circle-to-square');
const outputCaption = computed(() => squareOutput.value ? '方形输出' : stickCorrectionActive.value && stick.value.outputShape === 'square-to-circle' ? '圆形输出' : '保持原始形状');
const percent = (value: number | null) => value === null ? '—' : (value * 100).toFixed(1) + '%';
const stickStatus = computed(() => !rawStick.value ? '等待手柄输入' : !sticksSupported.value ? '不支持独立摇杆修正' : !correctionSelected.value || !state.value.draft.sticksEnabled || !stick.value.enabled ? '保持原始输入' : previewStick.value && Math.hypot(previewStick.value.x, previewStick.value.y) === 0 ? '内圈死区 · 零输出' : state.value.dirty ? '修正预览 · 待应用' : '修正预览');
const triggerStatus = computed(() => rawTrigger.value === null ? '等待扳机输入' : !triggersSupported.value ? '不支持独立扳机修正' : !correctionSelected.value || !state.value.draft.triggersEnabled ? '保持原始输入' : previewTrigger.value === 0 ? '起始死区 · 零输出' : previewTrigger.value === 1 ? '已达到满输出' : state.value.dirty ? '修正预览 · 待应用' : '修正预览');
const routeAxes = (target: 'virtual' | 'local') => state.value.available ? axes.value?.[target] || null : null;
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
 <div ref="root" class="calibration-panel calibration-graphics" aria-label="手柄校对" :aria-busy="state.loading || state.applying">
  <section class="calibration-scope" aria-label="修正范围">
   <div class="calibration-master"><Toggle :model-value="state.draft.enabled" label="开启修正" compact :gp-row="0" :gp-col="0" :disabled="gateLocked" @update:model-value="setMaster" /></div>
   <Toggle :model-value="state.draft.applyToVirtual" label="虚拟手柄" compact :gp-row="0" :gp-col="1" :disabled="childrenLocked || (!virtualSupported && !state.draft.applyToVirtual)" @update:model-value="v => updateGate(c => { c.applyToVirtual = v; })" />
   <Toggle :model-value="state.draft.applyToLocal" label="本机手柄" compact :gp-row="0" :gp-col="2" :disabled="childrenLocked || (!localSupported && !state.draft.applyToLocal)" @update:model-value="v => updateGate(c => { c.applyToLocal = v; })" />
  </section>
  <div class="calibration-workbench">
   <section class="axis-column calibration-stick" aria-label="摇杆修正">
    <div class="axis-heading"><Dropdown ref="stickSelect" :model-value="stickSide" :options="stickOptions" aria-label="选择摇杆" :gp-row="1" :gp-col="0" :disabled="childrenLocked" @update:model-value="selectStick" /><Toggle :model-value="state.draft.sticksEnabled" aria-label="启用摇杆修正" :gp-row="1" :gp-col="1" :disabled="childrenLocked" @update:model-value="v => updateGate(c => { c.sticksEnabled = v; })" /></div>
    <div class="visual-well"><svg class="stick-visual" :data-sample-x="rawStick?.x" :data-sample-y="rawStick?.y" :data-preview-x="previewStick?.x" :data-preview-y="previewStick?.y" viewBox="0 0 220 178" role="img" aria-label="摇杆实时输入与修正预览">
            <defs>
              <radialGradient id="stick-recess"><stop stop-color="#182638" /><stop offset=".76" stop-color="#111b29" /><stop offset="1" stop-color="#293a50" /></radialGradient>
              <linearGradient id="stick-cap" x2=".3" y2="1"><stop stop-color="#455970" /><stop offset="1" stop-color="#192738" /></linearGradient>
              <radialGradient id="stick-face" cx=".4" cy=".25"><stop stop-color="#384c64" /><stop offset="1" stop-color="#233247" /></radialGradient>
            </defs>
            <rect v-if="squareOutput" x="37" y="15" width="146" height="146" rx="5" fill="url(#stick-recess)" stroke="#7790aa" stroke-width="1.2" />
            <circle v-else cx="110" cy="88" r="73" fill="url(#stick-recess)" stroke="#7790aa" stroke-width="1.2" />
            <rect v-if="squareOutput" x="40" y="18" width="140" height="140" rx="2" fill="none" stroke="var(--accent)" stroke-width=".8" opacity=".7" />
            <circle v-else cx="110" cy="88" r="70" fill="none" stroke="var(--accent)" stroke-width=".8" opacity=".7" />
            <path d="M33 88H187M110 11V165" class="axis-guide" />
            <circle cx="110" cy="88" r="35" class="mid-ring" />
            <circle v-if="stickCorrectionActive" cx="110" cy="88" :r="70 * (1 - stick.outerDeadzonePct / 100)" class="full-ring" />
            <path :d="`M110 88L${110 + displayedRaw.x * 36} ${88 - displayedRaw.y * 36}`" stroke="#172230" stroke-width="20" stroke-linecap="round" />
            <g :transform="`translate(${110 + displayedRaw.x * 36} ${88 - displayedRaw.y * 36})`">
              <ellipse cy="5" rx="29" ry="27" fill="#050b13" opacity=".65" />
              <circle r="29" fill="url(#stick-cap)" stroke="#5a6c80" stroke-width="1" />
              <circle r="23" fill="url(#stick-face)" stroke="#152130" stroke-width="1.2" />
              <path d="M-16 -7Q0 -20 16 -7" stroke="#6a7f97" stroke-width="1" fill="none" opacity=".45" />
              <path d="M-7 0H7M0 -7V7" stroke="#93a3b4" stroke-width="1" opacity=".35" />
            </g>
            <circle v-if="stickCorrectionActive && stick.innerDeadzonePct > 0" :cx="110 + stick.centerX * 70" :cy="88 - stick.centerY * 70" :r="70 * stick.innerDeadzonePct / 100" class="deadzone-fill" />
            <path :d="`M${110 + displayedRaw.x * 70} ${88 - displayedRaw.y * 70}L${110 + displayedPreview.x * 70} ${88 - displayedPreview.y * 70}`" stroke="var(--accent)" stroke-width="1.3" opacity=".8" />
            <circle v-if="rawStick" :cx="110 + displayedRaw.x * 70" :cy="88 - displayedRaw.y * 70" r="5" fill="#0e1928" stroke="#d6e2ed" stroke-width="1.7" />
            <circle v-if="previewStick" :cx="110 + displayedPreview.x * 70" :cy="88 - displayedPreview.y * 70" r="3.6" fill="var(--accent)" stroke="#07101e" stroke-width="1" />
            <text x="199" y="92" class="axis-letter">X</text><text x="106" y="9" class="axis-letter">Y</text>
            <text x="110" y="173" text-anchor="middle" class="drag-hint">{{ rawStick ? outputCaption : '等待手柄输入' }}</text>
          </svg></div>
    <div class="signal-pair"><span><i class="raw-dot" />输入 <b data-stick-input>{{ magnitude(rawStick) }}</b></span><span><i class="out-dot" />预览 <b data-stick-preview>{{ magnitude(previewStick) }}</b></span></div>
    <div class="axis-status" :class="{zero: previewStick && previewStick.x === 0 && previewStick.y === 0 && correctionSelected}">{{ stickStatus }}</div>
    <div class="axis-controls calibration-stick-controls">
     <div data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="stickSide+'inner'" :model-value="stick.innerDeadzonePct" label="内圈死区" :min="0" :max="25" unit="%" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="stickParametersLocked" :gp-row="2" :gp-col="0" @update:model-value="v => updateStick('innerDeadzonePct',v)" @commit="commitDraft" /></div>
     <div data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="stickSide+'outer'" :model-value="stick.outerDeadzonePct" label="外圈死区" :min="0" :max="25" unit="%" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="stickParametersLocked" :gp-row="3" :gp-col="0" @update:model-value="v => updateStick('outerDeadzonePct',v)" @commit="commitDraft" /></div>
     <div data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="stickSide+'anti'" :model-value="stick.antiDeadzonePct" label="反死区" :min="0" :max="25" unit="%" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="stickParametersLocked" :gp-row="4" :gp-col="0" @update:model-value="v => updateStick('antiDeadzonePct',v)" @commit="commitDraft" /></div>
     <div class="shape-control"><span>输出形状</span><Dropdown ref="shapeSelect" :model-value="stick.outputShape || 'original'" :options="shapeOptions" aria-label="摇杆输出形状" :disabled="stickParametersLocked" :gp-row="5" :gp-col="0" @update:model-value="updateShape" /></div>
    </div>
   </section>
   <section class="axis-column calibration-trigger" aria-label="扳机修正">
    <div class="axis-heading"><Dropdown ref="triggerSelect" :model-value="triggerSide" :options="triggerOptions" aria-label="选择扳机" :gp-row="1" :gp-col="2" :disabled="childrenLocked" @update:model-value="selectTrigger" /><Toggle :model-value="state.draft.triggersEnabled" aria-label="启用扳机修正" :gp-row="1" :gp-col="3" :disabled="childrenLocked" @update:model-value="v => updateGate(c => { c.triggersEnabled = v; })" /></div>
    <div class="visual-well"><svg class="trigger-visual" viewBox="0 0 220 178" role="img" aria-label="扳机实时输入与修正预览">
            <defs><linearGradient id="trigger-body" x2=".5" y2="1"><stop stop-color="#3b5069" /><stop offset="1" stop-color="#172538" /></linearGradient><linearGradient id="trigger-lever" x2=".2" y2="1"><stop stop-color="#657c95" /><stop offset=".4" stop-color="#405670" /><stop offset="1" stop-color="#23354b" /></linearGradient></defs>
            <!-- Original silhouette: installed Steam / GenericTriggerIcon (32 × 32). -->
            <path d="M36 27H112Q120 27 120 36V143H28V36Q28 27 36 27Z" fill="#121f2e" opacity=".65" />
            <g transform="translate(35 40) scale(2.4)"><path d="M28.282 0H6.53576C3.98766 0 2.25959 2.72067 3.08048 5.13292C3.98027 7.77704 4.82962 11.0701 4.82962 14.08C4.82962 17.7831 3.11555 23.2002 1.67094 27.1082C0.810881 29.4348 2.50354 32.0001 4.98403 32.0001H16.3496C24.0297 32.0001 31.7097 25.6 31.7097 12.8V3.42771C31.7097 1.53464 30.175 0 28.282 0Z" fill="none" stroke="#788ea7" stroke-width=".45" stroke-dasharray="1 1.5" opacity=".6" /></g>
            <g :transform="`translate(35 40) rotate(${-(rawTrigger ?? 0) * 18} 68 0) scale(2.4)`">
              <path d="M28.282 0H6.53576C3.98766 0 2.25959 2.72067 3.08048 5.13292C3.98027 7.77704 4.82962 11.0701 4.82962 14.08C4.82962 17.7831 3.11555 23.2002 1.67094 27.1082C0.810881 29.4348 2.50354 32.0001 4.98403 32.0001H16.3496C24.0297 32.0001 31.7097 25.6 31.7097 12.8V3.42771C31.7097 1.53464 30.175 0 28.282 0Z" fill="url(#trigger-lever)" stroke="#92a9c2" stroke-width=".45" />
              <path d="M8 3H27" stroke="#c9d6e4" stroke-width=".5" opacity=".5" />
              <circle cx="27" cy="4" r="1.3" fill="#192a3d" stroke="#a8b9cb" stroke-width=".35" />
              <text x="17" y="20" text-anchor="middle" fill="#b3c4d8" font-size="6" font-family="inherit">{{ triggerSide.toUpperCase() }}</text>
            </g>
            <text x="65" y="152" text-anchor="middle" class="trigger-caption">{{ triggerSide.toUpperCase() }} 按压行程</text>
            <path d="M112 58Q126 77 116 101" fill="none" stroke="var(--accent)" stroke-width="1.5" opacity=".8" /><path d="M111 95L116 103L122 97" fill="none" stroke="var(--accent)" stroke-width="1.5" />
            <rect x="142" y="30" width="20" height="112" rx="8" fill="#101a29" stroke="#3b4e65" />
            <rect v-if="triggerCorrectionActive" x="143" y="31" width="18" :height="110 * trigger.startDeadzonePct / 100" rx="6" fill="#ff5364" opacity=".45" />
            <rect v-if="triggerCorrectionActive" x="143" :y="30 + 112 * trigger.fullScalePct / 100" width="18" :height="112 * (1 - trigger.fullScalePct / 100)" rx="6" fill="var(--accent)" opacity=".2" />
            <rect x="149" y="31" width="6" :height="110 * (rawTrigger ?? 0)" rx="3" fill="#d6e2ed" />
            <path v-if="triggerCorrectionActive" :d="`M137 ${30 + 112 * trigger.startDeadzonePct / 100}H166`" stroke="#ff6b77" stroke-width="1" stroke-dasharray="2 2" />
            <path :d="`M137 ${30 + 112 * trigger.fullScalePct / 100}H166`" stroke="var(--accent)" stroke-width="1" stroke-dasharray="2 2" />
            <rect x="179" y="30" width="10" height="112" rx="5" fill="#101a29" stroke="#30445c" />
            <rect x="180" y="31" width="8" :height="110 * (previewTrigger ?? 0)" rx="4" fill="var(--accent)" />
            <text x="152" y="21" text-anchor="middle" class="axis-letter">输入</text><text x="184" y="21" text-anchor="middle" class="axis-letter">输出</text>
            <text x="110" y="173" text-anchor="middle" class="drag-hint">{{ rawTrigger === null ? '等待扳机输入' : '实时输入与修正预览' }}</text>
          </svg></div>
    <div class="signal-pair"><span><i class="raw-dot" />输入 <b data-trigger-input>{{ percent(rawTrigger) }}</b></span><span><i class="out-dot" />预览 <b data-trigger-preview>{{ percent(previewTrigger) }}</b></span></div>
    <div class="axis-status" :class="{zero: previewTrigger === 0 && correctionSelected}">{{ triggerStatus }}</div>
    <div class="axis-controls calibration-trigger-controls">
     <div data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="triggerSide+'start'" :model-value="trigger.startDeadzonePct" label="起始死区" :min="0" :max="99" unit="%" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="triggerParametersLocked" :gp-row="2" :gp-col="2" @update:model-value="v => triggerChange('startDeadzonePct',v)" @commit="commitDraft" /></div>
     <div data-gp-slider-draft @gp:slider-edit-start="beginDraft" @gp:slider-edit-cancel="cancelDraft"><Slider :key="triggerSide+'max'" :model-value="trigger.fullScalePct" label="满输出行程" :min="1" :max="100" unit="%" :repeat-interval-ms="EDIT_REPEAT_BASE_MS" :disabled="triggerParametersLocked" :gp-row="3" :gp-col="2" @update:model-value="v => triggerChange('fullScalePct',v)" @commit="commitDraft" /></div>
     <div class="route-readouts" aria-label="设备实际输出"><h4>设备实际输出 <small>摇杆 / {{ triggerSide.toUpperCase() }}</small></h4><div v-for="target in (['virtual','local'] as const)" :key="target" :data-calibration-route="target"><span>{{ target==='virtual'?'虚拟':'本机' }}</span><b>{{ magnitude(routeAxes(target)?.[stickSide] || null) }} / {{ percent(routeAxes(target)?.[triggerSide] ?? null) }}</b><small>{{ routeText(target) }}</small></div></div>
    </div>
   </section>
  </div>
  <div class="calibration-center"><div class="center-title"><span>{{ state.sampling ? '正在采样轴心' : centerMeasured ? '轴心已采样' : centered ? '轴心已调整' : '自动校准轴心' }}</span><small>X {{ signed(stick.centerX) }} · Y {{ signed(stick.centerY) }}</small></div><Toggle :key="stickSide+'enabled'" :model-value="stick.enabled" label="此摇杆" compact :gp-row="6" :gp-col="0" :disabled="childrenLocked" @update:model-value="v => updateGate(c => { c.sticks[stickSide].enabled = v; })" /><button type="button" class="btn" data-calibrate :data-gp-row="6" :data-gp-col="2" :disabled="gateLocked || !state.draft.enabled || !state.draft.sticksEnabled || !stick.enabled || !sticksSupported || !state.snapshot?.source" @click="editor.startCenter(stickSide)">{{ state.sampling ? '取消校准' : centered || centerMeasured ? '重新校准' : '开始校准' }}</button></div>
  <div class="calibration-bottom"><p role="status" aria-live="polite">{{state.feedback}}</p><div class="calibration-bottom-actions"><button type="button" class="btn" :data-gp-row="7" :data-gp-col="0" :disabled="childrenLocked" @click="editor.reset()">恢复默认</button><button type="button" class="btn" data-calibration-apply :class="{active:state.dirty}" :data-gp-row="7" :data-gp-col="2" :disabled="!canApply" @click="editor.apply()">应用</button></div></div>
 </div>
</template>

<style scoped>
.calibration-panel{--calibration-line:color-mix(in srgb,var(--text) 10%,transparent);display:grid;gap:7px;min-width:0;color:var(--text)}
.calibration-panel .btn{min-height:30px!important;padding:5px 9px!important;font-size:12px!important;line-height:1.3;white-space:nowrap}.calibration-panel .btn:disabled{opacity:.45;cursor:default}
.calibration-panel :deep(.toggle-row){padding:0!important;min-height:32px!important;gap:6px!important}.calibration-panel :deep(.toggle-label){font-size:13px!important}
.calibration-scope{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:9px;padding:4px 9px;background:var(--console-surface);border:1px solid var(--calibration-line);border-radius:9px}.calibration-scope>div{min-width:0}
.calibration-workbench{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));border:1px solid var(--calibration-line);border-radius:12px;overflow:hidden;background:linear-gradient(140deg,color-mix(in srgb,var(--console-surface) 93%,var(--accent) 7%),var(--console-surface-alt))}.axis-column{min-width:0;padding:9px}.calibration-trigger{border-left:1px solid var(--calibration-line)}
.axis-heading{display:flex;justify-content:space-between;align-items:center;gap:7px;min-height:32px;margin-bottom:2px}.axis-heading :deep(.dropdown){flex:1;min-width:0}.axis-heading :deep(.toggle-row){flex-shrink:0}.calibration-panel :deep(.dd-trigger){min-height:31px;font-size:13px;padding:5px 7px}
.visual-well{margin:2px -5px 0;min-width:0}.visual-well svg{width:100%;height:149px;display:block;user-select:none}.axis-guide{stroke:#4b6078;stroke-width:.7;stroke-dasharray:2 4;opacity:.65}.mid-ring{stroke:#51667e;stroke-width:.65;fill:none;opacity:.3}.full-ring{fill:none;stroke:var(--accent);stroke-width:1.2;stroke-dasharray:3 3;opacity:.65}.deadzone-fill{fill:#ff5364;fill-opacity:.35;stroke:#ff7480;stroke-width:1;stroke-opacity:.9}.axis-letter{font-size:10px;fill:#93a4b8;font-family:inherit}.drag-hint{font-size:11px;fill:#9baabe;font-family:inherit}.trigger-caption{font-size:10px;fill:#9baabe;font-family:inherit}
.signal-pair{display:grid;grid-template-columns:1fr 1fr;gap:4px;margin-top:2px}.signal-pair>span{min-width:0;font-size:11px;color:var(--text-dim);white-space:nowrap}.signal-pair b{font-family:Consolas,monospace;font-size:16px;font-weight:600;color:var(--text);margin-left:2px}.signal-pair>span:last-child b{color:var(--accent)}.raw-dot,.out-dot{width:5px;height:5px;display:inline-block;vertical-align:middle;margin-right:3px;border-radius:50%;box-sizing:border-box}.raw-dot{border:1px solid #d6e2ed}.out-dot{background:var(--accent)}.axis-status{font-size:11px;color:var(--text-dim);margin-top:3px;min-height:20px;display:flex;align-items:center;justify-content:center;border-radius:5px;background:color-mix(in srgb,var(--accent) 5%,transparent)}.axis-status.zero{color:#ff929b;background:rgba(255,83,100,.08)}
.axis-controls{display:grid;grid-template-rows:repeat(3,44px) 53px;gap:3px;margin-top:8px;align-items:start}.axis-controls :deep(.slider){padding:0!important}.axis-controls :deep(.slider-label),.axis-controls :deep(.slider-val){font-size:13px!important}.axis-controls :deep(.slider-head){margin-bottom:3px!important}.shape-control{min-width:0}.shape-control>span{display:block;font-size:13px;margin-bottom:7px}
.route-readouts{grid-row:3/span 2;min-width:0;font-size:11px;padding-top:1px}.route-readouts h4{font-size:11px;font-weight:500;color:var(--text-dim);margin:0 0 5px;display:flex;justify-content:space-between;gap:3px}.route-readouts h4 small{font-size:10px}.route-readouts>div{display:grid;grid-template-columns:auto 1fr;gap:2px 4px;padding:3px 0;border-top:1px solid var(--calibration-line)}.route-readouts b{font:11px Consolas,monospace;justify-self:end;white-space:nowrap;color:var(--text)}.route-readouts small{grid-column:1/-1;color:var(--text-dim);font-size:10px}
.calibration-center{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:5px 9px;border:1px solid var(--calibration-line);border-radius:9px;background:var(--console-surface)}.center-title{font-size:12px;display:grid;gap:3px;min-width:0}.center-title small{font-size:10px;color:var(--text-dim);font-family:Consolas,monospace;white-space:nowrap}.calibration-center :deep(.toggle-label){font-size:12px!important}.calibration-center :deep(.toggle-row){flex-shrink:0}
.calibration-bottom{display:flex;align-items:center;justify-content:space-between;gap:8px}.calibration-bottom p{font-size:11px;line-height:1.5;color:var(--text-dim);min-width:0;margin:0;overflow-wrap:anywhere}.calibration-bottom-actions{display:flex;align-items:center;gap:7px;flex-shrink:0}
</style>
