<script setup lang="ts">
import { computed, onMounted, onActivated, onDeactivated, onUnmounted, ref, watch } from 'vue';
import MonitorHudTemplate from './MonitorHudTemplate.vue';
import type { HudAppearance, HudData, HudMetricToggles } from '@/bridge/monitorHudModel';
const props=withDefaults(defineProps<{ appearance: HudAppearance; fields: HudMetricToggles; data: HudData; originalSize?:boolean }>(),{originalSize:false});
const viewport = ref<HTMLElement | null>(null), content = ref<HTMLElement | null>(null);
const fit = ref(.8), measured = ref(false);
const contentStyle = computed(() => ({ transform: props.originalSize?'none':`translate(-50%, -50%) scale(${fit.value})`, left:props.originalSize?'8px':undefined, top:props.originalSize?'8px':undefined, visibility: measured.value ? 'visible' as const : 'hidden' as const }));
let observer: ResizeObserver | null = null, frame: number | null = null, active = true;
function measure() {
  const box = viewport.value, inner = content.value;
  if (!active || !box || !inner || !box.clientWidth || !box.clientHeight) return;
  // CSS dimensions are independent of this transform and of the shell's zoom.
  // Observing layout, rather than snapshot values, avoids work on every sample.
  const style = window.getComputedStyle(inner), width = parseFloat(style.width), height = parseFloat(style.height);
  if (!(width > 0) || !(height > 0)) { measured.value = false; return; }
  const next = props.originalSize ? 1 : Math.max(.001, Math.min(1, (box.clientWidth - 16) / width, (box.clientHeight - 16) / height));
  if (Math.abs(next - fit.value) > .00001) fit.value = next;
  measured.value = true;
}
function scheduleMeasure() {
  if (!active || frame !== null) return;
  frame = window.requestAnimationFrame(() => { frame = null; measure(); });
}
function stop() { observer?.disconnect(); observer = null; if (frame !== null) window.cancelAnimationFrame(frame); frame = null; }
function connect() {
  stop(); active = true;
  if (window.ResizeObserver && viewport.value && content.value) {
    observer = new window.ResizeObserver(scheduleMeasure);
    observer.observe(viewport.value); observer.observe(content.value);
  }
  scheduleMeasure();
}
watch(()=>props.originalSize,scheduleMeasure);
onMounted(connect); onActivated(connect);
onDeactivated(() => { active = false; stop(); });
onUnmounted(() => { active = false; stop(); });
</script>
<template>
  <div ref="viewport" class="monitor-preview-viewport">
    <div ref="content" class="monitor-preview-content" :style="contentStyle">
      <MonitorHudTemplate :appearance="appearance" :fields="fields" :data="data" @layout="scheduleMeasure" />
    </div>
  </div>
</template>
<style scoped>
.monitor-preview-viewport { position:relative;width:100%;height:100%;min-width:0;min-height:0; }
.monitor-preview-content { position:absolute;left:50%;top:50%;width:max-content;height:max-content;transform-origin:center; }
.monitor-preview-content :deep(.ymcc-hud) { max-width:none; }
</style>
