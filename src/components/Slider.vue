<script setup lang="ts">
import { computed } from 'vue';
import InlineIcon from './InlineIcon.vue';

const props = withDefaults(
  defineProps<{
    modelValue: number;
    min?: number;
    max?: number;
    step?: number;
    label?: string;
    icon?: string;
    unit?: string;
    color?: 'ac' | 'dc' | 'accent';
    disabled?: boolean;
    hint?: string;
    valueText?: string;
    /** Optional spatial-navigation coordinates for the range input. */
    gpRow?: number | string;
    gpCol?: number | string;
  }>(),
  { min: 0, max: 100, step: 1, color: 'accent', disabled: false }
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
  props.color === 'dc' ? 'color-mix(in srgb, var(--dc-accent) 32%, transparent)' : 'rgba(46, 166, 255, 0.32)'
);

function onInput(e: Event) {
  emit('update:modelValue', Number((e.target as HTMLInputElement).value));
}
function onChange(e: Event) {
  emit('commit', Number((e.target as HTMLInputElement).value));
}
// 键盘左右/上下：加快100%（基础×2）+ 线性加速；仅在滑块聚焦时生效（@keydown 仅对聚焦元素触发）
let keyAccelStart = 0;
function onKeydown(e: KeyboardEvent) {
  const dir =
    e.key === 'ArrowRight' || e.key === 'ArrowUp' ? 1 :
    e.key === 'ArrowLeft' || e.key === 'ArrowDown' ? -1 : 0;
  if (!dir) return;
  e.preventDefault();
  const step = props.step || 1;
  const now = performance.now();
  if (keyAccelStart === 0) keyAccelStart = now;
  const elapsed = (now - keyAccelStart) / 1000;
  const steps = Math.min(12, 2 + Math.floor(elapsed * 4)); // 基础2步，每秒+4，上限12
  let v = props.modelValue + dir * step * steps;
  v = Math.max(props.min, Math.min(props.max, v));
  emit('update:modelValue', v);
}
function onKeyup(e: KeyboardEvent) {
  if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft' && e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
  keyAccelStart = 0;
  emit('commit', props.modelValue);
}
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
        :step="step"
        :value="modelValue"
        :disabled="disabled"
        :aria-label="label || '滑块'"
        :data-gp-row="gpRow"
        :data-gp-col="gpCol"
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
