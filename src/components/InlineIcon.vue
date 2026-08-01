<script setup lang="ts">
import AppIcon from './AppIcon.vue';

defineProps<{ name: string; size?: string }>();
</script>

<template>
  <span class="inline-icon" :style="size ? { fontSize: size } : undefined">
    <AppIcon :name="name" />
  </span>
</template>

<style scoped>
.inline-icon {
  display: inline-flex;
  vertical-align: -0.18em;
  width: 1.1em;
  height: 1.1em;
  line-height: 1;
}
</style>
