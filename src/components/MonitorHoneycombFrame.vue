<script setup lang="ts">
import {computed,useId} from 'vue';
import {honeycombOutline,honeycombInnerLines} from '@/bridge/monitorHoneycombStyle';
const props=defineProps<{width:number;height:number;opacity:number}>();
const clipId='ymcc-honeycomb-'+useId();
const path=computed(()=>honeycombOutline(props.width,props.height));
const lines=computed(()=>honeycombInnerLines(props.width,props.height));
const hexes=computed(()=>[{x:24,y:30},{x:54,y:13},{x:54,y:47},{x:84,y:30},{x:84,y:64},{x:114,y:13},{x:props.width-66,y:props.height-63},{x:props.width-36,y:props.height-80},{x:props.width-6,y:props.height-63},{x:props.width-36,y:props.height-46},{x:props.width-6,y:props.height-29}]);
</script>
<template>
  <svg class="hud-honeycomb-frame" :viewBox="'0 0 '+width+' '+height" preserveAspectRatio="none" aria-hidden="true" style="position:absolute;inset:0;width:100%;height:100%;overflow:visible;pointer-events:none;z-index:-1">
    <defs><clipPath :id="clipId"><path :d="path"/></clipPath></defs>
    <path class="hud-honeycomb-fill" :d="path" :fill="'rgb(16 9 10 / '+opacity+')'"/>
    <g :clip-path="'url(#'+clipId+')'" opacity=".23"><polygon v-for="(p,i) in hexes" :key="i" :points="`${p.x-18},${p.y} ${p.x-9},${p.y-16} ${p.x+9},${p.y-16} ${p.x+18},${p.y} ${p.x+9},${p.y+16} ${p.x-9},${p.y+16}`" :fill="i===2||i===7?'var(--hud-accent)':'none'" :fill-opacity="i===2||i===7?.22:1" stroke="var(--hud-accent)" stroke-width="1"/></g>
    <path :d="path" fill="none" stroke="var(--hud-accent)" stroke-opacity=".11" stroke-width="7" vector-effect="non-scaling-stroke"/>
    <path class="hud-honeycomb-outline" :d="path" fill="none" stroke="var(--hud-accent)" stroke-width="1.4" vector-effect="non-scaling-stroke"/>
    <path :d="lines" fill="none" stroke="var(--hud-accent)" stroke-width="2.5" vector-effect="non-scaling-stroke"/>
  </svg>
</template>
