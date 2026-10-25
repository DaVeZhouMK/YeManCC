<script setup lang="ts">
import {computed} from 'vue';
const props=defineProps<{value:string}>();
const digits=computed(()=>props.value.split(''));
const segments=['5,2 39,2 34,7 10,7','41,4 41,39 36,34 36,10','41,43 41,78 36,72 36,49','5,80 39,80 34,75 10,75','3,43 8,49 8,72 3,78','3,4 8,10 8,34 3,39','6,41 11,37 33,37 38,41 33,45 11,45'];
const sets:Record<string,number[]>={'0':[0,1,2,3,4,5],'1':[1,2],'2':[0,1,6,4,3],'3':[0,1,6,2,3],'4':[5,6,1,2],'5':[0,5,6,2,3],'6':[0,5,6,4,2,3],'7':[0,1,2],'8':[0,1,2,3,4,5,6],'9':[0,1,2,3,5,6]};
</script>
<template>
  <svg class="hud-honeycomb-digits" viewBox="0 0 147 82" aria-hidden="true" style="display:block;height:100%;width:100%;overflow:visible">
    <g v-for="(digit,index) in digits" :key="index" :transform="`translate(${(3-digits.length)*24.5+index*49+4},0) skewX(-5)`"><polygon v-for="(points,i) in segments" :key="i" :points="points" fill="var(--hud-accent)" :opacity="sets[digit]?.includes(i)?1:.065"/></g>
  </svg>
</template>
