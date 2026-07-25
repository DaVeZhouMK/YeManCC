<script setup lang="ts">
type State = 'on' | 'off' | 'ac' | 'dc' | 'warn';
withDefaults(
  defineProps<{
    title?: string;
    state?: State;
    text?: string;
    value?: string;
  }>(),
  { state: 'off' }
);
const dotColor: Record<State, string> = {
  on: 'var(--ok)',
  off: '#46506280',
  ac: 'var(--accent)',
  dc: 'var(--accent-2)',
  warn: 'var(--danger)',
};
</script>

<template>
  <div class="state-card">
    <span class="dot" :style="{ background: dotColor[state] }"></span>
    <div class="sc-body">
      <div class="sc-title" v-if="title">{{ title }}</div>
      <div class="sc-text" v-if="text">{{ text }}</div>
    </div>
    <div class="sc-value" v-if="value">{{ value }}</div>
  </div>
</template>

<style scoped>
.state-card {
  display: flex;
  align-items: center;
  gap: 10px;
  background: var(--bg-input);
  border-radius: var(--radius-ctrl);
  padding: 10px 12px;
}
.dot {
  width: 9px;
  height: 9px;
  border-radius: 50%;
  flex: 0 0 auto;
  box-shadow: 0 0 6px currentColor;
}
.sc-body {
  flex: 1 1 auto;
  min-width: 0;
}
.sc-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text);
}
.sc-text {
  font-size: 13px;
  font-weight: 700;
  margin-top: 2px;
  color: var(--text);
}
.sc-value {
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}
</style>
