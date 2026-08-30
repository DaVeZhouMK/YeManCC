<script setup lang="ts">
const props = withDefaults(
  defineProps<{
    modelValue: boolean;
    label?: string;
    description?: string;
    color?: 'ac' | 'dc' | 'accent' | 'danger';
    disabled?: boolean;
    compact?: boolean; // 紧凑模式：用于标题行内（隐藏描述、更小间距）
    gpRow?: number;
    gpCol?: number;
  }>(),
  { color: 'accent', disabled: false, compact: false }
);
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void }>();
function toggle() {
  if (props.disabled) return;
  emit('update:modelValue', !props.modelValue);
}
const onColor = props.color === 'dc' ? 'var(--dc-accent)' : props.color === 'danger' ? 'var(--danger)' : 'var(--accent)';
</script>

<template>
  <div class="toggle-row" :class="{ disabled, compact }" @click="toggle" role="switch" :aria-checked="modelValue">
    <div class="toggle-text" v-if="!compact || label">
      <div class="toggle-label" v-if="label">{{ label }}</div>
      <div class="toggle-desc muted" v-if="description">{{ description }}</div>
    </div>
    <button
      type="button"
      class="switch"
      :class="{ on: modelValue, compact }"
      :data-gp-row="gpRow"
      :data-gp-col="gpCol"
      :style="modelValue ? { background: onColor, borderColor: onColor } : {}"
      @click.stop="toggle"
    >
      <span class="knob"></span>
    </button>
  </div>
</template>

<style scoped>
.toggle-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 8px 0;
  cursor: pointer;
}
.toggle-row.disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.toggle-row.compact {
  padding: 0;
  gap: 6px;
}
.toggle-text {
  text-align: left;
  flex: 1;
}
.toggle-label {
  font-size: 13px;
}
.toggle-row.compact .toggle-label {
  font-size: 11px;
}
.toggle-desc {
  font-size: 11px;
  margin-top: 2px;
}
.switch {
  flex: 0 0 auto;
  width: 40px;
  height: 22px;
  border-radius: 11px;
  border: 1px solid #2a3342;
  background: var(--bg-input);
  position: relative;
  cursor: pointer;
  transition: background 0.15s, border-color 0.15s, box-shadow 0.15s;
}
.switch:focus-visible {
  box-shadow: var(--focus-ring);
}
/* 手柄选中且已开启：保留蓝色焦点提示，并在轨道内增加纯白内圈。 */
.switch.on.focused {
  box-shadow: inset 0 0 0 2px #fff, 0 0 0 2px rgba(46, 166, 255, 0.35);
}
.knob {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #cfd8e3;
  transition: transform 0.15s;
}
.switch.on .knob {
  transform: translateX(18px);
  background: #fff;
}
.switch.compact {
  width: 32px;
  height: 18px;
  border-radius: 9px;
}
.switch.compact .knob {
  width: 12px;
  height: 12px;
  top: 2px;
  left: 2px;
}
.switch.compact.on .knob {
  transform: translateX(14px);
}
</style>
