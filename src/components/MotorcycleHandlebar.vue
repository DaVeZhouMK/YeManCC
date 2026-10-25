<script setup lang="ts">
import {computed} from 'vue';
const props=defineProps<{steering:number}>();
const value=computed(()=>Math.max(-1,Math.min(1,Number.isFinite(props.steering)?props.steering:0)));
const angle=computed(()=>Math.round(value.value*350)/10);
const direction=computed(()=>Math.abs(value.value)<.01?'回正':value.value<0?'向左倾斜':'向右倾斜');
</script>
<template><div class="motorcycle-handlebar" role="img" :aria-label="`车把：${direction}，${Math.round(Math.abs(value)*100)}%`" :data-steering="value.toFixed(3)" :data-angle="angle"><span>车把 · 倾斜</span><svg viewBox="0 0 120 60" aria-hidden="true"><path d="M60 7V15" class="neutral"/><g :transform="`rotate(${angle} 60 32)`"><path d="M12 24H30L43 37H77L90 24H108M60 37V53" class="bar"/><path d="M7 24H24M96 24H113" class="grip"/><circle cx="60" cy="35" r="7"/></g></svg><b>{{direction}}</b></div></template>
<style scoped>.motorcycle-handlebar{position:absolute;z-index:2;right:12px;bottom:140px;width:106px;padding:7px;border:1px solid #acc3cf44;border-radius:10px;background:#13212cba;color:#dfeaf0;text-align:center;pointer-events:none;user-select:none}.motorcycle-handlebar span,.motorcycle-handlebar b{font-size:11px;line-height:16px;font-weight:500}.motorcycle-handlebar svg{display:block;width:100%;height:52px;fill:#293d4e;stroke:#c6d2db;stroke-width:3;stroke-linecap:round}.motorcycle-handlebar .bar{fill:none;stroke-width:5}.motorcycle-handlebar .grip{stroke:#80d9c9;stroke-width:8}.motorcycle-handlebar .neutral{stroke:#79d7c4;stroke-width:2}@media(max-width:760px){.motorcycle-handlebar{width:84px;right:8px;bottom:150px;padding:4px}.motorcycle-handlebar svg{height:42px}}</style>
