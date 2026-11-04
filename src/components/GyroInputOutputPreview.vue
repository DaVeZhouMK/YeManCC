<script setup lang="ts">
import { computed, useId } from 'vue';
import GyroStatusPreview from './GyroStatusPreview.vue';
import {formatGyroAxis} from '@/bridge/gyroAxisFormat';
const props=defineProps<{
 persona:string;trigger:string;mode:'off'|'suppress'|'on'|'toggle';aimingTrigger:string;
 inputLabel:string;outputStick:string;outputAxis:string;steam:boolean;
 motion:{x:number;y:number};receiving:boolean;outputShape?:string;
 axes:{x:number;y:number;z:number};connected:boolean;power:'on'|'off'|'unknown';
 calibration:'uncalibrated'|'waiting-still'|'calibrating'|'already-calibrated';calibrationText:string;calibrationError:string;
}>();
const gradient=useId().replace(/[^a-zA-Z0-9_-]/g,'');
const triggerText=computed(()=>props.steam?'Steam 内设置':props.mode==='on'?'常开':`${props.trigger || '未选择'} · ${props.mode==='suppress'?'按住抑制':props.mode==='toggle'?'切换':'按住触发'}`);
const outputText=computed(()=>props.steam?'Steam 体感':`${props.outputStick==='left'?'左摇杆':'右摇杆'} · ${props.outputAxis==='x'?'横向':'双轴'}`);
const squareOutput=computed(()=>props.outputShape==='square');
const available=computed(()=>props.receiving&&!props.steam);
// Keep the admitted native contribution as this view's only position source.
// The calibration page supplies the visual, not a second correction algorithm.
const point=computed(()=>({x:available.value?Math.max(-1,Math.min(1,props.motion.x)):0,y:available.value&&props.outputAxis!=='x'?Math.max(-1,Math.min(1,props.motion.y)):0}));
const coordinate=(v:number)=>available.value?`${v>=0?'+':''}${(v*100).toFixed(0)}%`:'—';
</script>
<template>
 <div class="gyro-input-output-preview" :class="{receiving:available,external:steam}">
  <div class="gyro-preview-layout">
   <div class="gyro-stick-preview" :aria-label="`摇杆预览，${outputText}`">
    <GyroStatusPreview :axes="axes" :connected="connected" :power="power" :calibration="calibration" :calibration-text="calibrationText" :calibration-error="calibrationError">
     <template #power>
    <svg class="stick-visual" viewBox="0 0 220 178" role="img" :aria-label="`陀螺仪摇杆输出，X ${coordinate(point.x)}，Y ${coordinate(point.y)}`">
     <defs>
      <radialGradient :id="gradient+'-recess'"><stop stop-color="#182638"/><stop offset=".76" stop-color="#111b29"/><stop offset="1" stop-color="#293a50"/></radialGradient>
      <linearGradient :id="gradient+'-cap'" x2=".3" y2="1"><stop stop-color="#455970"/><stop offset="1" stop-color="#192738"/></linearGradient>
      <radialGradient :id="gradient+'-face'" cx=".4" cy=".25"><stop stop-color="#384c64"/><stop offset="1" stop-color="#233247"/></radialGradient>
     </defs>
     <rect v-if="squareOutput" x="37" y="15" width="146" height="146" rx="5" :fill="`url(#${gradient}-recess)`" stroke="#7790aa" stroke-width="1.2"/>
     <circle v-else cx="110" cy="88" r="73" :fill="`url(#${gradient}-recess)`" stroke="#7790aa" stroke-width="1.2"/>
     <rect v-if="squareOutput" x="40" y="18" width="140" height="140" rx="2" fill="none" stroke="var(--accent)" stroke-width=".8" opacity=".7"/>
     <circle v-else cx="110" cy="88" r="70" fill="none" stroke="var(--accent)" stroke-width=".8" opacity=".7"/>
     <path d="M33 88H187M110 11V165" class="axis-guide"/>
     <circle cx="110" cy="88" r="35" class="mid-ring"/>
     <path :d="`M110 88L${110+point.x*36} ${88-point.y*36}`" stroke="#172230" stroke-width="20" stroke-linecap="round"/>
     <g :transform="`translate(${110+point.x*36} ${88-point.y*36})`">
      <ellipse cy="5" rx="29" ry="27" fill="#050b13" opacity=".65"/>
      <circle r="29" :fill="`url(#${gradient}-cap)`" stroke="#5a6c80" stroke-width="1"/>
      <circle r="23" :fill="`url(#${gradient}-face)`" stroke="#152130" stroke-width="1.2"/>
      <path d="M-16 -7Q0 -20 16 -7" stroke="#6a7f97" stroke-width="1" fill="none" opacity=".45"/>
      <path d="M-7 0H7M0 -7V7" stroke="#93a3b4" stroke-width="1" opacity=".35"/>
     </g>
     <circle v-if="available" :cx="110+point.x*70" :cy="88-point.y*70" r="3.8" fill="var(--accent)" stroke="#07101e" stroke-width="1"/>
     <text x="199" y="92" class="axis-letter">X</text><text x="106" y="9" class="axis-letter">Y</text>
    </svg>
     </template>
     <template #rates><span v-for="axis in (['x','y','z'] as const)" :key="axis" class="gyro-xyz-axis">{{axis.toUpperCase()}} <b>{{connected?formatGyroAxis(axes[axis]).trim():'—'}}</b></span></template>
    </GyroStatusPreview>
   </div>
   <div class="gyro-flow-summary" aria-label="当前陀螺仪输入输出关系">
    <div class="gyro-info-row"><span class="gyro-info-icon" aria-hidden="true"><svg viewBox="0 0 20 20"><path d="M4 12V8a6 6 0 0 1 12 0v4M4 8h12M7 15h6M10 12v5"/></svg></span><div><small>触发</small><strong>{{triggerText}}</strong></div></div>
    <div class="gyro-info-row"><span class="gyro-info-icon" aria-hidden="true"><svg viewBox="0 0 20 20"><path d="M10 3v14M3 10h14M10 3l-2 3m2-3 2 3M17 10l-3-2m3 2-3 2"/><circle cx="10" cy="10" r="6"/></svg></span><div><small>输入</small><strong>{{inputLabel}}</strong></div></div>
    <div class="gyro-info-row"><span class="gyro-info-icon" aria-hidden="true"><svg viewBox="0 0 20 20"><circle cx="10" cy="8" r="4"/><path d="M10 12v3M5 16h10M3 18h14M8 8h4m-2-2v4"/></svg></span><div><small>输出</small><strong>{{outputText}}</strong></div></div>
   </div>
  </div>
 </div>
</template>
<style scoped>
.gyro-input-output-preview{min-width:0}
.gyro-preview-layout{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(0,1fr);gap:12px;align-items:stretch;min-width:0}
.gyro-stick-preview{min-width:0;padding:10px 9px 9px;border:1px solid var(--console-line);border-radius:10px;background:color-mix(in srgb,var(--console-surface-alt) 65%,transparent)}
.gyro-stick-preview :deep(.gyro-status-preview){margin:0;overflow:visible}
.gyro-stick-preview :deep(.gyro-space-scene){display:none}
.gyro-stick-preview :deep(.gyro-preview-controls){display:flex;flex-direction:column}
.gyro-stick-preview :deep(.gyro-state-line){order:0;min-height:0;padding:0 0 5px;gap:6px;flex-direction:row-reverse}
.gyro-stick-preview :deep(.gyro-state){font-size:11px;line-height:1.4;gap:4px;white-space:nowrap}
.gyro-stick-preview :deep(.gyro-state i){width:5px;height:5px}
.gyro-stick-preview :deep(.gyro-integrated-power){order:1;min-width:0}
.gyro-stick-preview :deep(.gyro-space-caption){order:2;display:flex;flex-direction:column;align-items:stretch;margin-top:2px;padding-top:7px;font-size:11px;flex-wrap:nowrap;gap:5px;min-width:0}
.gyro-stick-preview :deep(.gyro-space-caption>span){font-size:11px;color:var(--text-dim)}
.gyro-stick-preview :deep(.gyro-rate-values){display:grid;grid-template-columns:repeat(3,minmax(0,1fr));min-width:0;gap:8px}
.gyro-stick-preview :deep(.gyro-xyz-axis){display:flex;justify-content:space-between;align-items:center;gap:4px;min-width:0;color:var(--text-dim);font-size:11px}
.gyro-stick-preview :deep(.gyro-xyz-axis b){font-family:var(--font-mono,Consolas,monospace);font-variant-numeric:tabular-nums;color:var(--text);white-space:nowrap;text-align:right;font-size:12px;min-width:0;font-weight:600}
.stick-visual{display:block;width:100%;height:128px;min-width:0;user-select:none;opacity:.72}.receiving .stick-visual{opacity:1}
.axis-guide{stroke:#4b6078;stroke-width:.7;stroke-dasharray:2 4;opacity:.65}.mid-ring{stroke:#51667e;stroke-width:.65;fill:none;opacity:.3}.axis-letter{font-size:11px;fill:#93a4b8;font-family:inherit}
.gyro-flow-summary{display:grid;grid-template-rows:repeat(3,minmax(0,1fr));min-width:0;border:1px solid var(--console-line);border-radius:10px;background:var(--console-surface-alt);overflow:hidden}
.gyro-info-row{display:flex;align-items:center;gap:8px;min-width:0;padding:10px 9px}.gyro-info-row+.gyro-info-row{border-top:1px solid var(--console-line)}.gyro-info-row>div{display:flex;flex-direction:column;gap:4px;min-width:0;justify-content:center}
.gyro-info-icon{flex:0 0 21px;height:21px;color:var(--text-dim)}.gyro-info-icon svg{display:block;width:100%;height:100%;fill:none;stroke:currentColor;stroke-width:1.15;stroke-linecap:round;stroke-linejoin:round;opacity:.8}
.gyro-flow-summary small{font-size:12px;line-height:1.3;color:var(--text-dim)}.gyro-flow-summary strong{font-size:14px;font-weight:600;line-height:1.4;color:var(--text);overflow-wrap:anywhere}
</style>
