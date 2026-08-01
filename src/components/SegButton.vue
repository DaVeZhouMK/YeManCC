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
    disabled?: boolean;
  }>(),
  { color: 'accent', full: false, disabled: false }
);
const emit = defineEmits<{ (e: 'update:modelValue', v: string | number): void }>();
function pick(v: string | number) {
  if (props.disabled) return;
  emit('update:modelValue', v);
}
const accentVar = props.color === 'dc' ? 'var(--accent-2)' : 'var(--accent)';
</script>

<template>
  <div class="seg" :class="{ full: full, disabled: disabled }" :style="{ '--seg-accent': accentVar }">
    <button
      v-for="o in options"
      :key="o.value"
      type="button"
      class="seg-btn"
      :class="{ active: o.value === modelValue }"
      :disabled="disabled"
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
/* 禁用态：整体变暗 + 取消鼠标响应，视觉上明确表达"已锁定" */
.seg.disabled {
  opacity: 0.45;
  pointer-events: none;
}
.seg.disabled .seg-btn {
  cursor: default;
}
.seg.disabled .seg-btn.active {
  opacity: 0.65;
}
</style>
