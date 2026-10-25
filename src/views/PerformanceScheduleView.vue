<script setup lang="ts">
import { onDeactivated, ref } from 'vue';
import ContinuousPerformancePanel from '@/components/ContinuousPerformancePanel.vue';
import PerformanceSimulationWindow from '@/components/PerformanceSimulationWindow.vue';
const simulationOpen = ref(false);
onDeactivated(() => { simulationOpen.value = false; });
</script>
<template>
  <div class="performance-schedule-page">
    <ContinuousPerformancePanel scope="effective" show-simulation @simulation="simulationOpen = true" />
    <PerformanceSimulationWindow v-if="simulationOpen" @close="simulationOpen = false" />
  </div>
</template>
<style scoped>
.performance-schedule-page{width:100%;min-width:0;padding:4px 0 20px}
</style>
