<script setup lang="ts">
import { computed } from 'vue';
import { ICONS } from '@/icons';
import classicMouse from '../assets/mouse-classic.png';

const props = defineProps<{ name: string; strokeWidth?: number }>();
const lightIconNames = new Set(['gauge', 'gamepad', 'rotate']);
const resolvedStrokeWidth = computed(() => props.strokeWidth ?? (lightIconNames.has(props.name) ? 1.2 : 1.6));
</script>

<template>
  <span v-if="name === 'mouse'" class="app-icon app-icon-mouse-classic" :style="{maskImage: 'url('+classicMouse+')', WebkitMaskImage: 'url('+classicMouse+')'}" aria-hidden="true"/>
  <svg v-else
    class="app-icon"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    :stroke-width="resolvedStrokeWidth"
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
.app-icon-mouse-classic{background:currentColor;mask-size:contain;-webkit-mask-size:contain;mask-position:center;-webkit-mask-position:center;mask-repeat:no-repeat;-webkit-mask-repeat:no-repeat;}
</style>
