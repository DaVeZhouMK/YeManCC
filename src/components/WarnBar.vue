<script setup lang="ts">
import InlineIcon from './InlineIcon.vue';

defineProps<{ text: string }>();
</script>

<template>
  <div class="warn-bar">
    <span class="warn-icon"><InlineIcon name="warning" /></span>
    <span class="warn-text">{{ text }}</span>
  </div>
</template>

<style scoped>
.warn-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 9px 12px;
  font-size: 12px;
  margin-bottom: 10px;
}
.warn-icon {
  flex: 0 0 auto;
}
.warn-text {
  line-height: 1.4;
}
</style>
