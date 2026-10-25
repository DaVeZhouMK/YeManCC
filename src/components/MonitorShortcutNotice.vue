<script setup lang="ts">
import { computed } from 'vue';
import type { ShortcutActionFeedback } from '../bridge/shortcutActionFeedback';
import { confirmedNoticeRange } from '../bridge/monitorShortcutNotice';
const props=defineProps<{feedback:ShortcutActionFeedback;opacity:number}>();
const state=computed(()=>props.feedback.actionId==='app.updateReady'?'下载完成':({applied:'已应用',saved:'已保存',pending:'等待确认',failed:'操作失败'})[props.feedback.state]);
const range=computed(()=>confirmedNoticeRange(props.feedback));
const percent=computed(()=>range.value?(range.value.value-range.value.min)/(range.value.max-range.value.min)*100:0);
const label=computed(()=>props.feedback.actionId==='display.frameRateControl'&&props.feedback.value?.number===0?'不限':props.feedback.value?.text??'');
// Only confirmed results may present their value as effective. Pending/saved/
// failed outcomes retain an explicit readable status rather than a success value.
const result=computed(()=>props.feedback.state==='applied'?(label.value||state.value):state.value);
const compactTemplate=computed(()=>props.feedback.actionId==='monitor.template'&&props.feedback.state==='applied');
const detail=computed(()=>range.value||compactTemplate.value?'':props.feedback.message);
const accessibleText=computed(()=>`${props.feedback.title}，${result.value}${detail.value?'，'+detail.value:''}`);
</script>
<template>
 <div v-if="feedback.actionId!=='mouse.toggle'" class="shortcut-notice" :class="{'shortcut-notice--compact':compactTemplate,'shortcut-notice--adjust':!!range,'shortcut-notice--attention':feedback.state!=='applied'}" :style="{'--notice-opacity':opacity}" role="status" aria-live="polite" :aria-label="accessibleText" :data-feedback-state="feedback.state">
  <div class="shortcut-notice-head">
   <svg class="shortcut-notice-icon" viewBox="0 0 24 24" aria-hidden="true"><path v-if="feedback.state==='applied'" d="m5 12 4 4L19 6"/><path v-else-if="feedback.state==='failed'" d="m12 3 10 18H2L12 3zM12 9v5m0 3v.5"/><path v-else-if="feedback.state==='saved'" d="M5 3h12l3 3v15H4V3zM8 3v6h8V3M8 21v-7h8v7"/><path v-else d="M12 7v5l3 2M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0"/></svg>
   <strong class="shortcut-notice-title">{{ feedback.title }}</strong>
   <b class="shortcut-notice-result">{{ result }}</b>
  </div>
  <div v-if="range" class="shortcut-notice-track" role="progressbar" :aria-label="feedback.title" :aria-valuemin="range.min" :aria-valuemax="range.max" :aria-valuenow="range.value" :aria-valuetext="label"><i :style="{width:percent+'%'}"/></div>
  <p v-else-if="detail" class="shortcut-notice-message">{{ detail }}</p>
 </div>
</template>
<style>
.shortcut-notice{--notice-opacity:.8;--notice-base:var(--bg-solid);box-sizing:border-box;position:relative;display:flex;flex-direction:column;justify-content:center;width:459px;min-height:153px;padding:24px;color:var(--text);background:color-mix(in srgb,var(--notice-base) calc(var(--notice-opacity) * 100%),transparent);border:1px solid color-mix(in srgb,var(--accent) calc(var(--notice-opacity) * 44%),transparent);border-radius:18px;box-shadow:0 8px 24px rgb(0 0 0 / calc(var(--notice-opacity) * .25));pointer-events:none;font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;font-variant-numeric:tabular-nums;isolation:isolate}
.shortcut-notice--compact{min-height:96px;padding:20px 24px}
.shortcut-notice-head{display:grid;grid-template-columns:33px minmax(0,1fr) auto;align-items:center;gap:13px;min-width:0}
.shortcut-notice-icon{display:block;width:33px;height:33px;fill:none;stroke:var(--accent);stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.shortcut-notice-title{display:block;min-width:0;font-size:23px;font-weight:550;line-height:1.35;overflow-wrap:anywhere}
.shortcut-notice-result{display:block;max-width:220px;font-size:35.25px;font-weight:600;line-height:1.15;color:var(--accent);text-align:right;overflow-wrap:anywhere}
.shortcut-notice-track{height:6px;flex:none;margin:23px 0 0;border-radius:6px;background:color-mix(in srgb,var(--text) 15%,transparent);overflow:hidden}
.shortcut-notice-track i{display:block;height:100%;border-radius:inherit;background:var(--accent)}
.shortcut-notice-message{margin:14px 0 0;padding:0 0 0 46px;font-size:17px;font-weight:400;line-height:1.5;color:var(--text);overflow-wrap:anywhere}
.shortcut-notice--attention .shortcut-notice-result{font-size:23px;max-width:190px}
</style>
