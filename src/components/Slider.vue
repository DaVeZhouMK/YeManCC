<script setup lang="ts">
import { computed, onBeforeUnmount, onDeactivated } from 'vue';
import InlineIcon from './InlineIcon.vue';
import { editRepeatInterval } from '@/gamepad/sliderRepeat';

const props = withDefaults(
  defineProps<{
    modelValue: number;
    min?: number;
    max?: number;
    step?: number;
    /** Optional step-grid origin; snap only user input, never existing values. */
    stepBase?: number;
    /** Keep existing accelerated keyboard behavior unless explicitly disabled. */
    accelerate?: boolean;
    /** Opt-in continuous editing: accelerate the interval, keeping each step precise. */
    repeatIntervalMs?: number;
    label?: string;
    ariaLabel?:string;
    icon?: string;
    unit?: string;
    color?: 'ac' | 'dc' | 'accent';
    disabled?: boolean;
    hint?: string;
    valueText?: string;
    /** Optional spatial-navigation coordinates for the range input. */
    gpRow?: number | string;
    gpCol?: number | string;
    /** Preferred controller entry target; focusing never starts range editing. */
    gpInitialFocus?: boolean;
  }>(),
  { min: 0, max: 100, step: 1, color: 'accent', disabled: false, accelerate: true }
);

const displayValue = computed(() => props.valueText ?? String(props.modelValue));

const emit = defineEmits<{
  (e: 'update:modelValue', v: number): void;
  (e: 'commit', v: number): void;
}>();

// min===max 时避免除零（如 TDP 上限被钳到单值档）：填充条按 0% 处理而非 Infinity
const pct = computed(() => {
  const span = props.max - props.min;
  if (span <= 0) return 0;
  return Math.max(0, Math.min(100, ((props.modelValue - props.min) / span) * 100));
});
const accentVar = computed(() =>
  props.color === 'dc' ? 'var(--dc-accent)' : 'var(--accent)'
);
const thumbGlow = computed(() =>
  props.color === 'dc' ? 'color-mix(in srgb, var(--dc-accent) 32%, transparent)' : 'color-mix(in srgb, var(--accent) 32%, transparent)'
);

// Explicit stepBase avoids a min=1 native range shifting the 5% grid.
// Use native free movement only for opted-in sliders, then snap user events.
const rangeStep = computed(() => props.stepBase === undefined ? props.step : 'any');
function inputValue(e: Event): number {
  const input = e.target as HTMLInputElement;
  const raw = Number(input.value);
  if (props.stepBase === undefined) return raw;
  const snapped = props.stepBase + Math.round((raw - props.stepBase) / (props.step || 1)) * (props.step || 1);
  const value = Math.max(props.min, Math.min(props.max, snapped));
  input.value = String(value);
  return value;
}
let editing=false, editStartValue=props.modelValue;
let pointerCommit: number | null = null;
function beginEdit(e: Event) {
  if(props.disabled || editing)return;
  editing=true; editStartValue=props.modelValue; pointerCommit=null;
  (e.target as HTMLElement).dispatchEvent(new CustomEvent('gp:slider-edit-start',{bubbles:true}));
}
function resumeEdit() { if(!editing)editStartValue=props.modelValue; editing=true; pointerCommit=null; }
function endEdit(e: Event) {
  if(!editing)return;
  editing=false;stopKeyRepeat();
  (e.target as HTMLElement).dispatchEvent(new CustomEvent('gp:slider-edit-end',{bubbles:true}));
}
function resetEdit(_e: Event) { editing=false;pointerCommit=null;keyAccelStart=0;stopKeyRepeat(); }
function cancelEdit(e: Event) {
  if(!editing)return;
  resetEdit(e);
  (e.target as HTMLElement).dispatchEvent(new CustomEvent('gp:slider-edit-cancel',{bubbles:true}));
}
function onInput(e: Event) {
  beginEdit(e);
  emit('update:modelValue', inputValue(e));
}
function onPointerup(e: Event) {
  if(!editing)return;
  const value=inputValue(e), changed=value!==editStartValue;
  endEdit(e);
  // Complete before a parent's deferred refresh. A controlled range can lose
  // the browser's later change event when Vue updates its value during drag.
  if(changed) { pointerCommit=value; emit('commit',value); }
}
function onChange(e: Event) {
  const value=inputValue(e);
  if(pointerCommit===value) return;
  pointerCommit=null;
  endEdit(e);
  emit('commit',value);
}
// Opted-in performance sliders repeat one grid step on a bounded hold timer.
// OS autorepeat is ignored for these controls, so it cannot add a second clock.
let keyAccelStart = 0, keyDirection = 0, keyStarted = 0, keyValue = 0;
let keyInput: HTMLInputElement | null = null;
let keyRepeatTimer: ReturnType<typeof setTimeout> | null = null;
function stopKeyRepeat() {
  if (keyRepeatTimer !== null) clearTimeout(keyRepeatTimer);
  keyRepeatTimer = null; keyDirection = 0; keyInput = null;
}
function keyStep(direction: number, steps: number, base: number): number {
  const step = props.step || 1;
  let value = base + direction * step * steps;
  if (props.stepBase !== undefined) {
    const index = (base - props.stepBase) / step;
    value = props.stepBase + (direction > 0 ? Math.floor(index) + steps : Math.ceil(index) - steps) * step;
  }
  return Math.max(props.min, Math.min(props.max, value));
}
function repeatKeyStep() {
  if (!keyInput || !keyDirection) return;
  keyRepeatTimer = setTimeout(() => {
    keyRepeatTimer = null;
    if (!keyInput || !keyDirection || props.disabled || document.visibilityState === 'hidden' ||
        document.activeElement !== keyInput || !keyInput.isConnected) { stopKeyRepeat(); return; }
    keyValue = keyStep(keyDirection, 1, keyValue);
    emit('update:modelValue', keyValue);
    repeatKeyStep(); // schedule one next step, never replay missed ticks
  }, editRepeatInterval(performance.now() - keyStarted, props.repeatIntervalMs, props.accelerate));
}
function keyDirectionFor(key: string): number {
  return key === 'ArrowRight' || key === 'ArrowUp' ? 1 : key === 'ArrowLeft' || key === 'ArrowDown' ? -1 : 0;
}
function onKeydown(e: KeyboardEvent) {
  const dir = keyDirectionFor(e.key);
  if (!dir) return;
  e.preventDefault();
  if (props.disabled || (props.repeatIntervalMs && e.repeat)) return;
  beginEdit(e);
  if (props.repeatIntervalMs) {
    const input = e.target as HTMLInputElement;
    if (keyInput === input && keyDirection === dir) return;
    stopKeyRepeat();
    keyInput = input; keyDirection = dir; keyStarted = performance.now();
    keyValue = keyStep(dir, 1, props.modelValue);
    emit('update:modelValue', keyValue);
    repeatKeyStep();
    return;
  }
  // Other pages keep their existing keyboard repeat/step behaviour.
  const now = performance.now();
  if (keyAccelStart === 0) keyAccelStart = now;
  const elapsed = (now - keyAccelStart) / 1000;
  emit('update:modelValue', keyStep(dir, props.accelerate ? Math.min(12, 2 + Math.floor(elapsed * 4)) : 1, props.modelValue));
}
function onKeyup(e: KeyboardEvent) {
  const dir = keyDirectionFor(e.key);
  if (!dir || (props.repeatIntervalMs && keyDirection !== dir)) return;
  const value = props.repeatIntervalMs && keyInput ? keyValue : props.modelValue;
  keyAccelStart = 0;
  endEdit(e);
  emit('commit', value);
}
function stopHiddenKeyRepeat() { if (document.visibilityState === 'hidden') stopKeyRepeat(); }
// No timer remains when the page/slider loses its input session.
if (props.repeatIntervalMs) document.addEventListener('visibilitychange', stopHiddenKeyRepeat);
onDeactivated(stopKeyRepeat);
onBeforeUnmount(() => {
  stopKeyRepeat();
  document.removeEventListener('visibilitychange', stopHiddenKeyRepeat);
});
</script>

<template>
  <div class="slider" :class="{ disabled }" :style="{ '--thumb-color': accentVar, '--thumb-glow': thumbGlow }">
    <div class="slider-head" v-if="label">
      <span class="slider-label">
        <InlineIcon v-if="icon" :name="icon" class="slider-ic" />
        {{ label }}
      </span>
      <span class="slider-val" :style="{ color: accentVar }"
        >{{ displayValue }}<small v-if="unit && !valueText"> {{ unit }}</small></span
      >
    </div>
    <div class="slider-track-wrap">
      <div class="slider-track-bg"></div>
      <div class="slider-fill" :style="{ width: pct + '%', background: accentVar }"></div>
      <input
        type="range"
        :min="min"
        :max="max"
        :step="rangeStep"
        :value="modelValue"
        :disabled="disabled"
        :aria-label="ariaLabel || label || '滑块'"
        :data-gp-initial-focus="gpInitialFocus ? 'true' : undefined"
        :data-gp-row="gpRow"
        :data-gp-col="gpCol"
        :data-gp-step="stepBase === undefined ? undefined : step"
        :data-gp-step-base="stepBase"
        :data-gp-accelerate="accelerate ? undefined : 'false'"
        :data-gp-repeat-interval-ms="repeatIntervalMs"
        @pointerdown="beginEdit"
        @pointerup="onPointerup"
        @pointercancel="cancelEdit"
        @blur="cancelEdit"
        @gp:slider-edit-reset="resetEdit"
        @gp:slider-edit-start="resumeEdit"
        @input="onInput"
        @change="onChange"
        @keydown="onKeydown"
        @keyup="onKeyup"
      />
    </div>
    <div class="slider-hint muted" v-if="hint">{{ hint }}</div>
  </div>
</template>

<style scoped>
.slider {
  padding: 4px 0;
}
.slider.disabled {
  opacity: 0.45;
}
.slider-head {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  margin-bottom: 8px;
}
.slider-label {
  color: var(--text);
  font-size: 13px;
}
.slider-ic {
  margin-right: 6px;
  opacity: 0.85;
}
.slider-val {
  font-variant-numeric: tabular-nums;
  font-weight: 600;
  font-size: 14px;
}
.slider-val small {
  font-size: 11px;
  opacity: 0.8;
}
.slider-track-wrap {
  position: relative;
  height: 22px;
  display: flex;
  align-items: center;
}
.slider-track-bg {
  position: absolute;
  left: 0;
  top: 50%;
  transform: translateY(-50%);
  width: 100%;
  height: 6px;
  border-radius: 3px;
  background: rgba(255, 255, 255, 0.045);
  border: 1px solid rgba(255, 255, 255, 0.057);
  pointer-events: none;
  z-index: 0;
}
.slider-fill {
  position: absolute;
  left: 0;
  top: 50%;
  transform: translateY(-50%);
  height: 6px;
  border-radius: 3px;
  pointer-events: none;
  z-index: 1;
}
input[type='range'] {
  -webkit-appearance: none;
  appearance: none;
  width: 100%;
  height: 6px;
  border-radius: 3px;
  background: transparent;
  outline: none;
  margin: 0;
  position: relative;
  z-index: 2;
}
input[type='range']:focus-visible {
  box-shadow: var(--focus-ring);
}
input[type='range']::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--thumb-color);
  cursor: pointer;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.4);
  transition: box-shadow 0.12s, transform 0.08s;
}
input[type='range']:hover::-webkit-slider-thumb {
  box-shadow: 0 0 0 4px var(--thumb-glow), 0 1px 4px rgba(0, 0, 0, 0.4);
}
input[type='range']:focus-visible::-webkit-slider-thumb {
  transform: scale(1.12);
  box-shadow: 0 0 0 5px var(--thumb-glow), 0 0 10px 2px var(--thumb-color);
}
input[type='range']:disabled::-webkit-slider-thumb {
  cursor: not-allowed;
}
input[type='range']::-moz-range-thumb {
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  border: 3px solid var(--thumb-color);
  cursor: pointer;
  transition: box-shadow 0.12s, transform 0.08s;
}
input[type='range']:hover::-moz-range-thumb {
  box-shadow: 0 0 0 4px var(--thumb-glow);
}
input[type='range']:focus-visible::-moz-range-thumb {
  transform: scale(1.12);
  box-shadow: 0 0 0 5px var(--thumb-glow), 0 0 10px 2px var(--thumb-color);
}
.slider-hint {
  font-size: 11px;
  margin-top: 4px;
}
</style>
