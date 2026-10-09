<script setup lang="ts">
import { computed,ref,watch } from 'vue';
import Slider from './Slider.vue';
import Dropdown from './Dropdown.vue';
import AppIcon from './AppIcon.vue';
import {FPS_MIN,FPS_CEILINGS} from '@/bridge/yeman';
import {frameRatePair,frameRateSetting,type FrameRatePair,type FrameRateSide,type FrameRateSetting} from '@/bridge/frameRateModel';
const props=withDefaults(defineProps<{values:FrameRatePair;disabled?:boolean;labelPrefix?:string;side?:FrameRateSide;hideSideLabel?:boolean}>(),{disabled:false,labelPrefix:'',hideSideLabel:false});
const emit=defineEmits<{(e:'commit',side:FrameRateSide,value:FrameRateSetting):void}>();
const draft=ref(frameRatePair(props.values));
watch(()=>props.values,value=>{draft.value=frameRatePair(value);},{deep:true});
const sides=computed<FrameRateSide[]>(()=>props.side?[props.side]:['ac','dc']);
const options=FPS_CEILINGS.map(value=>({value,label:value===0?'不锁帧':`${value} FPS`}));
function commit(side:FrameRateSide){if(props.disabled)return;draft.value[side]=frameRateSetting(draft.value[side]);emit('commit',side,{...draft.value[side]});}
function ceiling(side:FrameRateSide,value:string|number){
  if(props.disabled)return;
  const limit=Number(value);if(!FPS_CEILINGS.includes(limit))return;
  const current=draft.value[side];
  if(limit===0){if(current.fps>0)current.lastFps=current.fps;current.fps=0;current.ceiling=0;}
  else{current.ceiling=limit;current.fps=Math.min(limit,current.fps || current.lastFps || 90);current.lastFps=current.fps;}
  commit(side);
}
</script>
<template>
  <div class="frame-rate-pair">
    <div v-for="(side,index) in sides" :key="side" class="frame-rate-row" :class="[side,{'side-label-hidden':hideSideLabel}]" :data-gp-game-row="labelPrefix+'frame-'+side">
      <div v-if="!hideSideLabel" class="frame-rate-side"><AppIcon :name="side==='ac'?'plug':'battery'"/><span>{{side==='ac'?'插电 AC':'电池 DC'}}</span></div>
      <div class="frame-rate-controls">
        <Slider :model-value="draft[side].fps" :min="draft[side].fps===0?0:FPS_MIN" :max="draft[side].ceiling || FPS_MIN" :step="5" :label="labelPrefix ? '帧率上限[游戏专属]' : hideSideLabel ? '帧率上限' : (side==='ac'?'插电帧率上限':'电池帧率上限')" :unit="draft[side].fps===0?undefined:'FPS'" :value-text="draft[side].fps===0?'不锁帧':undefined" :color="side==='dc'?'dc':'accent'" :disabled="disabled || draft[side].fps===0" :gp-row="index" :gp-col="0" @update:model-value="v=>draft[side].fps=v" @commit="()=>commit(side)" />
        <Dropdown :model-value="draft[side].ceiling" :options="options" :disabled="disabled" :color="side==='dc'?'dc':'accent'" width="104px" :aria-label="labelPrefix+(side==='ac'?'插电锁帧上限':'电池锁帧上限')" :gp-row="index" :gp-col="1" @update:model-value="v=>ceiling(side,v)" />
      </div>
    </div>
  </div>
</template>
<style scoped>
.frame-rate-pair {display:grid;gap:14px;}
.frame-rate-row {display:grid;grid-template-columns:78px minmax(0,1fr);gap:12px;align-items:center;}
.frame-rate-row.side-label-hidden {grid-template-columns:minmax(0,1fr);gap:0;}
.frame-rate-side {display:flex;align-items:center;gap:6px;font-size:11px;font-weight:600;white-space:nowrap;}
.frame-rate-side :deep(svg){width:16px;height:16px;}
.frame-rate-row.ac .frame-rate-side {color:var(--accent);}
.frame-rate-row.dc .frame-rate-side {color:var(--dc-accent);}
.frame-rate-controls {display:flex;align-items:center;gap:12px;min-width:0;}
.frame-rate-controls :deep(.slider) {flex:1;min-width:0;}
@media(max-width:480px){.frame-rate-row{grid-template-columns:minmax(0,1fr);gap:5px;}.frame-rate-controls{gap:10px;}}
</style>
