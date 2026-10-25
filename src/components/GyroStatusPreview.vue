<script setup lang="ts">
import {computed} from 'vue';
const props=defineProps<{axes:{x:number;y:number;z:number};orientation?:{w:number;x:number;y:number;z:number}|null;connected:boolean;power:'on'|'off'|'unknown';calibration:'uncalibrated'|'waiting-still'|'calibrating'|'already-calibrated';calibrationText:string;calibrationError:string}>();
const rates=computed(()=>[props.axes.x,props.axes.y,props.axes.z].map(v=>Number.isFinite(v)?v:0));
const calibrationTone=computed(()=>props.calibrationError?'warning':props.calibration==='already-calibrated'?'on':props.calibration==='calibrating'||props.calibration==='waiting-still'?'pending':'off');
// This background consumes sensor presentation only. It never reads stick axes,
// integrates rates into a pose, or writes motion/calibration settings.
const motion=computed(()=>{
 if(!props.connected)return {source:'none',x:0,y:0,z:0};
 const q=props.orientation;
 if(q&&[q.w,q.x,q.y,q.z].every(Number.isFinite)){
  const norm=Math.hypot(q.w,q.x,q.y,q.z);
  if(norm>1e-6){const w=q.w/norm,x=q.x/norm,y=q.y/norm,z=q.z/norm,degree=180/Math.PI;
   return {source:'orientation',x:Math.atan2(2*(w*x+y*z),1-2*(x*x+y*y))*degree,y:Math.asin(Math.max(-1,Math.min(1,2*(w*y-z*x))))*degree,z:Math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))*degree};}
 }
 const clamp=(value:number)=>Math.max(-24,Math.min(24,value/3));
 return {source:'angular-velocity',x:clamp(rates.value[0]),y:clamp(rates.value[1]),z:clamp(rates.value[2])};
});
const motionStyle=computed(()=>({transform:`perspective(480px) rotateX(${motion.value.x}deg) rotateY(${motion.value.y}deg) rotateZ(${motion.value.z}deg)`}));
</script>
<template>
 <div class="gyro-status-preview">
  <svg class="gyro-space-scene" :class="{receiving:connected}" :style="motionStyle" :data-motion-source="motion.source" viewBox="0 0 400 166" aria-hidden="true" focusable="false">
   <circle class="gyro-space-boundary" cx="200" cy="83" r="66"/>
   <ellipse class="gyro-space-ring ring-x" cx="200" cy="83" rx="73" ry="24"/>
   <ellipse class="gyro-space-ring ring-y" cx="200" cy="83" rx="73" ry="24" transform="rotate(60 200 83)"/>
   <ellipse class="gyro-space-ring ring-z" cx="200" cy="83" rx="73" ry="24" transform="rotate(-60 200 83)"/>
   <path class="gyro-space-axis" d="M121 83h158M200 6v154M145 140 255 26"/>
   <circle class="gyro-space-center-halo" cx="200" cy="83" r="11"/><circle class="gyro-space-center" cx="200" cy="83" r="4"/>
   <g class="gyro-space-labels"><text x="283" y="87">X</text><text x="208" y="12">Y</text><text x="136" y="151">Z</text></g>
  </svg>
  <div class="gyro-preview-controls">
   <div class="gyro-integrated-power"><slot name="power"/></div>
   <div class="gyro-state-line" aria-live="polite"><span class="gyro-state" :data-tone="connected ? 'on' : 'off'"><i/>{{connected?'传感器已连接':'等待传感器输入'}}</span><span class="gyro-state" :data-tone="calibrationTone" :title="calibrationError || calibrationText"><i/>{{calibrationError?'校准未完成':calibrationText}}</span></div>
   <div class="gyro-space-caption"><span>角速度 °/s</span><div class="gyro-rate-values"><slot name="rates"><span v-for="(axis,index) in ['X','Y','Z']" :key="axis">{{axis}} <b>{{rates[index].toFixed(1)}}</b></span></slot></div></div>
  </div>
 </div>
</template>
<style scoped>
.gyro-status-preview{position:relative;isolation:isolate;min-width:0;background:transparent;border:0;padding:0;overflow:hidden}.gyro-preview-controls{position:relative;z-index:1}.gyro-state-line{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:10px 0}.gyro-state{display:flex;align-items:center;gap:6px;font-size:13px;font-weight:650;color:var(--text-dim)}.gyro-state i{width:6px;height:6px;flex:0 0 auto;border-radius:50%;background:#667282}.gyro-state[data-tone=on]{color:var(--accent)}.gyro-state[data-tone=on] i{background:var(--accent);box-shadow:0 0 7px color-mix(in srgb,var(--accent) 60%,transparent)}.gyro-state[data-tone=warning]{color:#e5b66e}.gyro-state[data-tone=warning] i{background:#e5b66e}.gyro-state[data-tone=pending]{color:#b3c6dc}.gyro-space-scene{position:absolute;z-index:0;inset:0;display:block;width:100%;height:100%;color:var(--accent);opacity:.3;pointer-events:none;transform-origin:50% 50%;transition:transform .12s ease-out}.gyro-space-boundary{fill:none;stroke:var(--accent);stroke-width:1}.gyro-space-ring{fill:none;stroke:var(--accent);stroke-width:1.3}.ring-y{stroke-opacity:.8}.ring-z{stroke-opacity:.6}.gyro-space-axis{fill:none;stroke:var(--accent);stroke-opacity:.65;stroke-width:.8;stroke-dasharray:3 5}.gyro-space-center-halo{fill:none;stroke:var(--accent);stroke-width:1}.gyro-space-center{fill:var(--accent)}.gyro-space-labels text{fill:var(--accent);font:12px 'Segoe UI',sans-serif;font-weight:600}.gyro-space-caption{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;font-size:12px;color:var(--text-dim);margin-top:10px;padding-top:8px;border-top:1px solid rgba(140,163,192,.15)}.gyro-rate-values{display:flex;gap:15px;font-variant-numeric:tabular-nums}.gyro-rate-values span{white-space:nowrap}.gyro-rate-values b{font-weight:550;color:var(--text)}.gyro-integrated-power{margin:0}

.gyro-status-preview{overflow:visible}
.gyro-space-scene{inset:40px 0 0;height:calc(100% - 36px);opacity:.21}
.gyro-state-line{min-height:43px;padding:9px 0 7px;gap:12px}
.gyro-state{font-size:12px;font-weight:500;line-height:1.4}
.gyro-state:last-child{justify-content:flex-end;min-width:0;text-align:right}
.gyro-space-caption{min-height:29px;margin-top:0;padding-top:7px;flex-wrap:nowrap;gap:8px}
.gyro-space-caption>span{font-size:11px;white-space:nowrap}
.gyro-rate-values{gap:14px;font-size:12px}

@media(prefers-reduced-motion:reduce){.gyro-space-scene{transition:none}}
</style>
