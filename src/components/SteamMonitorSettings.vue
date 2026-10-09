<script setup lang="ts">
import { reactive, ref, onMounted, onActivated, onDeactivated, onUnmounted } from 'vue';
import Dropdown from './Dropdown.vue';
import Slider from './Slider.vue';
import InlineIcon from './InlineIcon.vue';
import { on } from '@/bridge/ipc';
import { steamSettings, steamMonitorMessage, STEAM_MONITOR_POSITIONS, STEAM_MONITOR_DETAILS, type SteamMonitorValues, type SteamMonitorState } from '@/bridge/steamSettings';
const values=reactive<SteamMonitorValues>({position:0,detail:1,scale:1,saturation:1,opacity:1});
const available=ref(false), status=ref('读取 Steam 监控设置…'), saving=ref(false);
let firstActivation=true, active=true, version=0, queued:Partial<SteamMonitorValues>={}, off:(()=>void)|undefined;
function accept(state:SteamMonitorState){
  if(typeof state.available==='boolean')available.value=state.available;
  for(const key of ['position','detail','scale','saturation','opacity'] as const)if(typeof state[key]==='number')values[key]=state[key];
  status.value=steamMonitorMessage(state);
}
async function refresh(){
  if(saving.value || !active)return;
  const rev=version;
  try{const state=await steamSettings.get<SteamMonitorState>('monitor');if(rev===version && active)accept(state);}
  catch(e){if(rev===version && active){available.value=false;status.value='Steam 设置读取失败：'+(e as Error).message;}}
}
async function change(patch:Partial<SteamMonitorValues>){
  if(!available.value)return;
  Object.assign(values,patch);Object.assign(queued,patch);++version;
  if(saving.value)return;
  saving.value=true;
  try{
    while(Object.keys(queued).length){
      const update=queued;queued={};const rev=version;
      const state=await steamSettings.set<SteamMonitorState>({monitor:update});
      if(rev===version)accept(state);
    }
  }catch(e){status.value='Steam 设置保存失败：'+(e as Error).message;}
  finally{saving.value=false;}
}
function percent(key:'scale'|'saturation'|'opacity',value:number){void change({[key]:Math.round(value)/100});}
onMounted(()=>{
  void refresh();
  off=on<{monitor?:SteamMonitorState}>('steam.settings.updated',state=>{if(active && !saving.value && state.monitor)accept(state.monitor);});
});
onActivated(()=>{active=true;if(firstActivation){firstActivation=false;return;}void refresh();});
onDeactivated(()=>{active=false;++version;});
onUnmounted(()=>{active=false;++version;off?.();});
</script>
<template>
  <section class="card steam-monitor" data-gp-group="steam-monitor">
    <h3 class="card-title"><InlineIcon name="steam" /> Steam监控</h3>
    <div class="steam-monitor-selects">
      <label><span>Steam监控位置</span><Dropdown :model-value="values.position" :options="STEAM_MONITOR_POSITIONS" :disabled="!available" width="100%" color="accent" aria-label="Steam监控位置" :gp-row="0" :gp-col="0" @update:model-value="v=>change({position:Number(v)})" /></label>
      <label><span>性能详情等级</span><Dropdown :model-value="values.detail" :options="STEAM_MONITOR_DETAILS" :disabled="!available" width="100%" color="accent" aria-label="性能详情等级" :gp-row="0" :gp-col="1" @update:model-value="v=>change({detail:Number(v)})" /></label>
    </div>
    <div class="steam-monitor-sliders">
      <Slider :model-value="values.scale*100" :min="20" :max="140" :step="10" :accelerate="false" label="文字大小缩放" unit="%" :value-text="Math.round(values.scale*100)+'%'" :disabled="!available" color="accent" :gp-row="1" :gp-col="0" @update:model-value="v=>values.scale=v/100" @commit="v=>percent('scale',v)" />
      <Slider :model-value="values.saturation*100" :min="0" :max="100" :step="10" :accelerate="false" label="文字对比度/饱和度" unit="%" :value-text="Math.round(values.saturation*100)+'%'" :disabled="!available" color="accent" :gp-row="2" :gp-col="0" @update:model-value="v=>values.saturation=v/100" @commit="v=>percent('saturation',v)" />
      <Slider :model-value="values.opacity*100" :min="0" :max="100" :step="10" :accelerate="false" label="背景不透明度" unit="%" :value-text="Math.round(values.opacity*100)+'%'" :disabled="!available" color="accent" :gp-row="3" :gp-col="0" @update:model-value="v=>values.opacity=v/100" @commit="v=>percent('opacity',v)" />
    </div>
    <p class="steam-monitor-status" role="status">{{status}}</p>
  </section>
</template>
<style scoped>
.steam-monitor { padding:12px 14px;margin-bottom:10px; }
.card-title { display:flex;align-items:center;gap:6px;font-size:12px;font-weight:600;margin:0 0 12px; }
.steam-monitor-selects { display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:14px;align-items:start; }
.steam-monitor-selects label { display:flex;flex-direction:column;gap:8px;min-width:0; }
.steam-monitor-selects label>span { font-size:12px;line-height:18px; }
.steam-monitor-sliders { display:grid;grid-template-columns:minmax(0,1fr);gap:12px;margin-top:16px; }
.steam-monitor-status { color:var(--text-dim);font-size:11px;line-height:1.6;margin:12px 0 0; }
</style>
