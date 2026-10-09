<script setup lang="ts">
import {ref,onMounted,onActivated,onUnmounted} from 'vue';
import FrameRatePair from './FrameRatePair.vue';
import {frameRatePair,loadGlobalFrameRates,saveGlobalFrameRate,applyIndependentFrameRates,onFrameRatesChanged,type FrameRateSide,type FrameRateSetting} from '@/bridge/frameRateLimits';
const pair=ref(frameRatePair({})),busy=ref(false),error=ref('');
let firstActivation=true,revision=0,disposed=false,off:(()=>void)|undefined;
async function refresh(){if(busy.value)return;const rev=revision;try{const loaded=await loadGlobalFrameRates();if(!disposed && rev===revision)pair.value=loaded;}catch(e){error.value='锁帧读取失败：'+(e as Error).message;}}
async function commit(side:FrameRateSide,value:FrameRateSetting){
  if(busy.value)return;++revision;busy.value=true;error.value='';
  try{pair.value=await saveGlobalFrameRate(side,value);await applyIndependentFrameRates();}
  catch(e){error.value='锁帧保存失败：'+(e as Error).message;}
  finally{busy.value=false;if(error.value)await refresh();}
}
onMounted(()=>{void refresh();off=onFrameRatesChanged(value=>{pair.value=value;});});
onActivated(()=>{if(firstActivation){firstActivation=false;return;}void refresh();});
onUnmounted(()=>{disposed=true;++revision;off?.();});
</script>
<template>
  <slot :pair="pair" :busy="busy" :error="error" :commit="commit">
    <section class="card rtss-frame-limit" data-gp-group="rtss-frame-limit">
      <FrameRatePair :values="pair" :disabled="busy" @commit="commit"/>
      <p v-if="error" class="frame-error" role="status">{{error}}</p>
    </section>
  </slot>
</template>
<style scoped>
.rtss-frame-limit {padding:12px 14px;}
.frame-error {color:var(--danger);font-size:12px;margin:8px 0 0;}
</style>
