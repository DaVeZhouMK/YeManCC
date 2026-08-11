<script setup lang="ts">
import { ICONS } from '@/icons';

defineProps<{ name: string }>();
</script>

<template>
  <svg
    class="app-icon"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="1.6"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
    v-html="ICONS[name] || ''"
  />
</template>

<style scoped>
.app-icon {
  width: 1em;
  height: 1em;
  display: block;
}
</style>
