<script setup lang="ts">
import {computed,onMounted,ref,watch,nextTick} from 'vue';
import Slider from './Slider.vue';
import {undervoltState as uv,undervoltAvailable,refreshUndervoltRuntime,setUndervoltEnabled,applyUndervoltOffset,cancelUndervoltTrial,resetUndervoltDefaults} from '@/bridge/undervoltRuntime';
const draft=ref(0);
watch(()=>uv.value,value=>draft.value=value,{immediate:true});
onMounted(()=>{void refreshUndervoltRuntime();});
const capability=computed(()=>uv.capability);
const unit=computed(()=>capability.value?.unit==='mv'?'mV':'CO 步进');
const supported=computed(()=>undervoltAvailable.value);
const status=computed(()=>uv.pending?'正在试用 · 尚未保存':uv.stage==='saved'?'参数已自动保存':uv.stage==='interrupted'||uv.stage==='suspended'?'本次试用未保存':'即时试用 · 延后保存');
function format(value:number){return `${value} ${unit.value}`;}
function toggle(){void setUndervoltEnabled(!uv.enabled);}
function commit(value:number){void applyUndervoltOffset(value);}
</script>
<template>
 <section class="card cpu-setting" aria-label="电压偏移" data-cpu-setting="voltage" :aria-busy="uv.busy">
  <div class="cpu-setting-heading"><h3 class="card-title">电压偏移</h3><span>{{!uv.ready?'支持情况待确认':supported?'通道可用':'不支持'}}</span></div>
  <p class="cpu-setting-hint">{{capability?.vendor==='intel'?'Intel 核心／缓存电压域':'全核 Curve Optimizer'}} · {{unit}}</p>
  <p v-if="uv.ready&&!supported" class="cpu-setting-hint">此设备不支持电压偏移，不能启用。</p>
  <button type="button" class="uv-setting-toggle" aria-label="降压" :class="{active:uv.enabled&&supported}" :disabled="uv.busy" :aria-pressed="uv.enabled&&supported" @click="toggle"><span>降压</span><b>{{!uv.ready?'待确认':!supported?'不支持':uv.enabled?'开启':'关闭'}}</b></button>
  <Slider v-model="draft" :min="capability?.offset.min ?? 0" :max="Math.min(0,capability?.offset.max ?? 0)" :step="capability?.offset.step || 1" :disabled="!supported||!uv.enabled||uv.busy" :accelerate="false" label="电压偏移" :value-text="format(draft)" @commit="commit" @gp:slider-edit-cancel="draft=uv.value" />
  <p class="cpu-setting-hint">调整即刻提交，性能调度与这里共用同一开关。</p>
  <div class="uv-trial" role="status" aria-live="polite"><div><strong>{{status}}</strong><b>{{uv.pending?uv.remaining+' 秒':'30 秒'}}</b></div><div v-if="uv.pending" class="uv-progress"><i :style="{width:(30-uv.remaining)/30*100+'%'}" /></div><p>运行满 30 秒后自动保存；切换页面、隐藏到后台仍继续。期间死机或退出，本次设置不保存；再次调整会重新计时。</p></div>
  <div class="cpu-setting-controls"><button type="button" :disabled="!supported||uv.busy" @click="resetUndervoltDefaults">恢复默认</button><button type="button" :disabled="!uv.pending||uv.busy" @click="cancelUndervoltTrial">取消本次调整</button></div>
  <p class="cpu-setting-hint">已保存：{{uv.saved?.enabled?'开启':'关闭'}} · {{format(uv.saved?.value ?? 0)}}</p>
  <p v-if="uv.receipt" class="cpu-setting-hint">{{uv.receipt.accepted?'本次请求已接受；不代表实际偏移或稳定性已验证。':'本次请求未确认，本次不保存。'}} {{capability?.readbackCapability===false?'此通道无法回读当前偏移。':''}}</p>
  <p v-if="uv.error" class="cpu-setting-error" role="alert">{{uv.error}}</p>
  <p v-if="uv.notice" class="cpu-setting-hint" role="status">{{uv.notice}}</p>
 </section>
</template>
<style scoped>
.cpu-setting{display:flex;flex-direction:column;gap:11px;padding:14px;border:1px solid var(--console-line);border-radius:12px;background:var(--console-surface);min-width:0}.cpu-setting-heading{display:flex;align-items:center;justify-content:space-between;gap:8px}.card-title{margin:0;font-size:15px;font-weight:650;color:var(--text)}.cpu-setting-heading>span{font-size:11px;color:var(--text-dim)}.cpu-setting-hint{margin:0;font-size:11px;line-height:1.55;color:var(--text-dim)}button{min-height:34px;padding:6px 10px;border:1px solid var(--console-line);border-radius:8px;background:var(--bg-input);color:var(--text);font:inherit;font-size:12px;cursor:pointer}button:disabled{opacity:.45;cursor:default}.uv-setting-toggle{display:flex;align-items:center;justify-content:space-between}.uv-setting-toggle.active{color:var(--accent);border-color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,var(--bg-input))}.cpu-setting-controls{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.uv-trial{padding:10px;border:1px solid color-mix(in srgb,var(--accent) 22%,transparent);border-radius:8px;background:color-mix(in srgb,var(--accent) 7%,var(--bg-input))}.uv-trial>div:first-child{display:flex;justify-content:space-between;gap:8px;font-size:12px}.uv-trial b{color:var(--accent)}.uv-trial p{font-size:11px;line-height:1.6;color:var(--text-dim);margin:7px 0 0}.uv-progress{height:4px;background:var(--bg-input);border-radius:3px;overflow:hidden;margin-top:9px}.uv-progress i{display:block;height:100%;background:var(--accent)}.cpu-setting-error{margin:0;color:var(--danger);font-size:12px;line-height:1.5}button:focus-visible,button.focused{outline:2px solid var(--accent);outline-offset:2px}
</style>
