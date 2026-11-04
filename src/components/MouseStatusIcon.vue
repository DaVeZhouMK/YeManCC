<script setup lang="ts">
import {computed} from 'vue';
import {mouseModeState} from '@/bridge/mouseModeState';
const state=computed(()=>{
  const s=mouseModeState.value;
  if(s?.provider==='steam')return 'sd';
  if(!s||s.pending)return 'pending';
  if(!s.ok||!s.stateKnown)return 'unknown';
  return s.mode==='off'?'off':s.mode==='auto'?'auto':s.on?'on':'unknown';
});
const label=computed(()=>({on:'ON',auto:'A',sd:'SD',pending:'…',unknown:'?',off:''})[state.value]);
const description=computed(()=>({on:'鼠标开启',auto:'鼠标自动',sd:'鼠标由 Steam 管理',pending:'鼠标状态待确认',unknown:'鼠标状态待确认',off:'鼠标关闭'})[state.value]);
</script>
<template>
  <svg class="mouse-status-icon" :class="{'is-off':state==='off'}" :data-mouse-icon-state="state" viewBox="0 0 36 36" role="img" :aria-label="description">
    <g :transform="state==='off' ? 'translate(18 18) scale(.7) translate(-18 -18)' : undefined">
    <path class="mouse-body" fill-rule="evenodd" clip-rule="evenodd" d="M14 3C10.6863 3 8 5.68629 8 9V23C8 28.5228 12.4772 33 18 33C23.5228 33 28 28.5228 28 23V9C28 5.68629 25.3137 3 22 3H21V14C21 15.6569 19.6569 17 18 17C16.3431 17 15 15.6569 15 14V3H14Z"/>
    <path class="mouse-body" d="M17 5H19V11H17V5Z"/>
    </g>
    <text v-if="label" x="18" y="26" text-anchor="middle" :class="{'one-letter':state==='auto'}">{{label}}</text>
    <g v-if="state==='off'" class="mouse-prohibition" fill="none" stroke-linecap="round"><circle cx="18" cy="18" r="15"/><path d="M7.4 28.6L28.6 7.4"/></g>
  </svg>
</template>
<style scoped>
.mouse-status-icon{display:block;width:1em;height:1em;flex:none;color:inherit;overflow:visible}
.mouse-body{fill:currentColor}.is-off .mouse-body{opacity:.65}
text{fill:#10151c;stroke:none;font-family:'Segoe UI','Microsoft YaHei',sans-serif;font-size:7px;font-weight:500;letter-spacing:-.25px}
text.one-letter{font-size:10px;letter-spacing:0}
.mouse-prohibition{stroke:currentColor;stroke-width:1.55;paint-order:stroke}
.mouse-prohibition path{filter:drop-shadow(0 0 .6px #10151c)}
</style>
