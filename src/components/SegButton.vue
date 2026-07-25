<script setup lang="ts">
interface Opt {
  value: string | number;
  label: string;
}
const props = withDefaults(
  defineProps<{
    modelValue: string | number;
    options: Opt[];
    color?: 'ac' | 'dc' | 'accent';
    full?: boolean;
  }>(),
  { color: 'accent', full: false }
);
const emit = defineEmits<{ (e: 'update:modelValue', v: string | number): void }>();
function pick(v: string | number) {
  emit('update:modelValue', v);
}
const accentVar = props.color === 'dc' ? 'var(--accent-2)' : 'var(--accent)';
</script>

<template>
  <div class="seg" :class="{ full: full }" :style="{ '--seg-accent': accentVar }">
    <button
      v-for="o in options"
      :key="o.value"
      type="button"
      class="seg-btn"
      :class="{ active: o.value === modelValue }"
      @click="pick(o.value)"
    >
      {{ o.label }}
    </button>
  </div>
</template>

<style scoped>
.seg {
  display: inline-flex;
  background: var(--bg-input);
  border-radius: var(--radius-ctrl);
  padding: 3px;
  gap: 2px;
  flex-wrap: nowrap;
}
.seg.full {
  display: flex;
  width: 100%;
}
.seg.full .seg-btn {
  flex: 1 1 0;
  justify-content: center;
  padding-left: 4px;
  padding-right: 4px;
}
.seg-btn {
  border: none;
  background: transparent;
  color: var(--text);
  font-size: 12px;
  padding: 6px 10px;
  border-radius: 6px;
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
  white-space: nowrap;
  display: inline-flex;
  align-items: center;
}
.seg-btn:hover {
  color: var(--text);
}
.seg-btn.active {
  background: var(--seg-accent);
  color: #06121d;
  font-weight: 600;
}
.seg-btn:focus-visible {
  box-shadow: var(--focus-ring);
}
</style>
