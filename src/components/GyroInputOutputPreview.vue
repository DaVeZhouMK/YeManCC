<script setup lang="ts">
import { computed } from 'vue';
import ControllerDeviceDiagram from './ControllerDeviceDiagram.vue';
const props=defineProps<{
 persona:string;trigger:string;mode:'off'|'suppress'|'on'|'toggle';aimingTrigger:string;
 inputLabel:string;outputStick:string;outputAxis:string;steam:boolean;
 motion:{x:number;y:number};receiving:boolean;
}>();
const triggerIndex:Record<string,number>={LB:4,RB:5,LT:6,RT:7};
const triggerText=computed(()=>props.steam?'Steam 内设置':props.mode==='on'?'常开':`${props.trigger || '未选择'} · ${props.mode==='suppress'?'按住抑制':props.mode==='toggle'?'切换':'按住触发'}`);
const outputText=computed(()=>props.steam?'Steam 体感':`${props.outputStick==='left'?'左摇杆':'右摇杆'} · ${props.outputAxis==='x'?'横向':'双轴'}`);
const mappedButtons=computed(()=>{
 if(props.steam)return [];
 const keys=[props.outputStick==='left'?10:11];
 if(props.mode!=='on'&&triggerIndex[props.trigger]!==undefined)keys.push(triggerIndex[props.trigger]);
 if(triggerIndex[props.aimingTrigger]!==undefined)keys.push(triggerIndex[props.aimingTrigger]);
 return [...new Set(keys)];
});
// Setting highlights do not claim a physical press. Only the admitted native
// gyro contribution moves the output stick; no mapping or input polling here.
const axes=computed(()=>{
 const result=[0,0,0,0];if(props.steam||!props.receiving)return result;
 const offset=props.outputStick==='left'?0:2;
 result[offset]=props.motion.x;result[offset+1]=props.outputAxis==='x'?0:props.motion.y;
 return result;
});
</script>
<template>
 <div class="gyro-input-output-preview">
  <div class="gyro-controller-figure" :aria-label="`手柄预览，${triggerText}，输出${outputText}`"><ControllerDeviceDiagram :persona="persona" :mapped-buttons="mappedButtons" :axes="axes" :interactive="false" /></div>
  <div class="gyro-flow-summary" aria-label="当前陀螺仪输入输出关系">
   <div><small>触发</small><strong>{{triggerText}}</strong></div><div><small>输入</small><strong>{{inputLabel}}</strong></div><div><small>输出</small><strong>{{outputText}}</strong></div>
  </div>
  <div class="gyro-preview-legend"><span>高亮：触发键与输出摇杆</span><span v-if="!steam" :class="{receiving}"><i/>{{receiving?'实际陀螺仪输出':'等待有效输出'}}</span><span v-else>由 Steam 处理体感</span></div>
 </div>
</template>
<style scoped>
.gyro-input-output-preview{min-width:0}.gyro-controller-figure{display:grid;place-items:center;padding:2px 8px 8px;min-width:0}.gyro-controller-figure :deep(.device-diagram){width:100%;max-width:350px}.gyro-flow-summary{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));border:1px solid var(--console-line);border-radius:9px;background:var(--console-surface-alt);min-width:0;overflow:hidden}.gyro-flow-summary>div{display:flex;flex-direction:column;justify-content:center;align-items:center;gap:5px;padding:9px 4px;text-align:center;min-width:0}.gyro-flow-summary>div+div{border-left:1px solid var(--console-line)}.gyro-flow-summary small{font-size:12px;color:var(--text-dim)}.gyro-flow-summary strong{font-size:13px;font-weight:600;color:var(--text);line-height:1.4;overflow-wrap:anywhere}.gyro-preview-legend{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:8px;font-size:11px;color:var(--text-dim)}.gyro-preview-legend span{display:flex;align-items:center;gap:5px}.gyro-preview-legend i{width:5px;height:5px;flex:0 0 auto;border-radius:50%;background:var(--text-dim)}.gyro-preview-legend .receiving{color:var(--accent)}.gyro-preview-legend .receiving i{background:var(--accent)}
</style>
