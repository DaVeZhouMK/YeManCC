<script setup lang="ts">
interface Opt {
  value: string | number;
  label: string;
  detail?: string;
}
const props = withDefaults(
  defineProps<{
    modelValue: string | number;
    options: Opt[];
    color?: 'ac' | 'dc' | 'accent';
    full?: boolean;
    disabled?: boolean;
    gpRow?: number | string;
    gpColStart?: number | string;
  }>(),
  { color: 'accent', full: false, disabled: false }
);
const emit = defineEmits<{ (e: 'update:modelValue', v: string | number): void }>();
function pick(v: string | number) {
  if (props.disabled) return;
  emit('update:modelValue', v);
}
const accentVar = props.color === 'dc' ? 'var(--dc-accent)' : 'var(--accent)';
</script>

<template>
  <div class="seg" :class="{ full: full, disabled: disabled }" :style="{ '--seg-accent': accentVar }">
    <button
      v-for="(o, i) in options"
      :key="o.value"
      type="button"
      class="seg-btn"
      :class="{ active: o.value === modelValue }"
      :disabled="disabled"
      :data-gp-row="gpRow"
      :data-gp-col="gpRow == null ? undefined : Number(gpColStart ?? 0) + i"
      @click="pick(o.value)"
    >
      <span class="seg-main">{{ o.label }}</span>
      <span v-if="o.detail" class="seg-detail">{{ o.detail }}</span>
    </button>
  </div>
</template>

<style scoped>
.seg {
  display: inline-flex;
  align-items: center;
  background: transparent;
  padding: 0;
  gap: 7px;
  flex-wrap: nowrap;
}
.seg.full {
  display: flex;
  width: 100%;
}
.seg.full .seg-btn {
  flex: 1 1 0;
  min-width: 0;
}
.seg-btn {
  display: inline-flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  min-height: 38px;
  padding: 0 8px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 8px;
  background: var(--bg-input);
  color: var(--text);
  font-size: 11px;
  font-weight: 700;
  line-height: var(--btn-line-height);
  cursor: pointer;
  white-space: nowrap;
  transition: background 0.12s, color 0.12s, border-color 0.12s;
}
.seg-main {
  line-height: 1.15;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}
.seg-detail {
  max-width: 100%;
  font-size: 10px;
  line-height: 1.15;
  font-weight: 500;
  opacity: 0.78;
  text-align: center;
  white-space: normal;
  font-variant-numeric: tabular-nums;
}
.seg-btn:hover:not(:disabled) {
  color: var(--text);
}
.seg-btn.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, var(--bg-input));
  font-weight: 700;
}
.seg-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
.seg.disabled {
  opacity: 0.45;
  pointer-events: none;
}
.seg.disabled .seg-btn {
  cursor: default;
}
</style>
