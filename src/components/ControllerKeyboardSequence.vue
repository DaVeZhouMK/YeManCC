<script setup lang="ts">
import { computed, nextTick, ref } from 'vue';
import ControllerOutputKeyboard from './ControllerOutputKeyboard.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { keyboardSequenceFor, keyboardOutputKeyLabel, KEYBOARD_SEQUENCE_LIMIT, type KeyboardOutputKey, type KeyboardOutputStep, type KeyboardOutputSequence } from '@/bridge/controllerKeyboardSequence';
import type { ControllerShortcutRule } from '@/bridge/controllerShortcutRules';
const props = defineProps<{ rule: ControllerShortcutRule; disabled: boolean }>();
const emit = defineEmits<{ (e: 'update', value: string): void; (e: 'open', value: boolean): void }>();
const root = ref<HTMLElement | null>(null), editing = ref<number | null>(null);
const sequence = computed(() => keyboardSequenceFor(props.rule));
const initialKeys = computed(() => { const step = editing.value === null ? null : sequence.value.steps[editing.value]; return step && 'keys' in step ? step.keys : []; });
function commit(value: KeyboardOutputSequence) { emit('update', JSON.stringify(value)); }
function change(patch: Partial<KeyboardOutputSequence>) { commit({ ...sequence.value, ...patch }); }
function updateStep(index: number, step: KeyboardOutputStep) { change({ steps: sequence.value.steps.map((s, i) => i === index ? step : s) }); }
function open(index: number) { editing.value = index; emit('open', true); }
function close() { editing.value = null; emit('open', false); void nextTick(() => focusGamepadElement(root.value?.querySelector<HTMLElement>('.sequence-add-key') || null)); }
function pick(keys: KeyboardOutputKey[]) {
  if (editing.value === null) return;
  const steps = [...sequence.value.steps], previous = steps[editing.value];
  steps[editing.value] = { keys, holdMs: previous && 'keys' in previous ? previous.holdMs : 80 };
  change({ steps }); close();
}
function restore(index:number){void nextTick(()=>focusGamepadElement(root.value?.querySelectorAll<HTMLElement>('.sequence-step button:not(:disabled)')[Math.max(0,index)]||root.value?.querySelector<HTMLElement>('.sequence-add-key')||null))}
function remove(index: number) { change({ steps: sequence.value.steps.filter((_, i) => i !== index) }); restore(0); }
function move(index: number, delta: number) { const steps = [...sequence.value.steps], target = index + delta; if (target < 0 || target >= steps.length) return; [steps[index], steps[target]] = [steps[target], steps[index]]; change({ steps }); restore(0); }
function delay(index:number,event:Event){const value=Number((event.target as HTMLInputElement).value);if(Number.isFinite(value))updateStep(index,{delayMs:Math.max(10,Math.min(60000,Math.round(value*1000)))})}
function number(event: Event, fallback: number, min: number, max: number) { const n = Number((event.target as HTMLInputElement).value); return Number.isFinite(n) ? Math.max(min, Math.min(max, Math.round(n))) : fallback; }
</script>
<template>
 <section ref="root" class="keyboard-sequence" aria-label="键盘组合与顺序">
  <header><strong>组合按键 / 简单宏</strong><span>按顺序执行</span></header>
  <div class="sequence-steps">
   <div v-for="(step,index) in sequence.steps" :key="index" class="sequence-step">
    <b class="sequence-index">{{index+1}}</b>
    <template v-if="'keys' in step"><button type="button" class="sequence-key" :disabled="disabled" @click="open(index)">{{step.keys.map(keyboardOutputKeyLabel).join(' + ')}}</button><label>按住<input type="number" min="40" max="2000" step="10" :disabled="disabled" :value="step.holdMs" @change="updateStep(index,{...step,holdMs:number($event,80,40,2000)})">ms</label></template>
    <template v-else><span class="sequence-wait">等待</span><label><input type="number" min="0.01" max="60" step="0.1" :disabled="disabled" :value="step.delayMs/1000" @change="delay(index,$event)">秒</label></template>
    <div class="sequence-row-actions"><button type="button" aria-label="上移步骤" :disabled="disabled||index===0" @click="move(index,-1)">↑</button><button type="button" aria-label="下移步骤" :disabled="disabled||index===sequence.steps.length-1" @click="move(index,1)">↓</button><button type="button" aria-label="复制步骤" :disabled="disabled||sequence.steps.length>=KEYBOARD_SEQUENCE_LIMIT" @click="change({steps:[...sequence.steps.slice(0,index+1),JSON.parse(JSON.stringify(step)),...sequence.steps.slice(index+1)]})">复制</button><button type="button" aria-label="删除步骤" :disabled="disabled||('keys' in step&&sequence.steps.filter(s=>'keys' in s).length===1)" @click="remove(index)">×</button></div>
   </div>
  </div>
  <div class="sequence-add"><button type="button" class="sequence-add-key" :disabled="disabled||sequence.steps.length>=KEYBOARD_SEQUENCE_LIMIT" @click="open(sequence.steps.length)">＋ 按键 / 组合</button><button type="button" class="sequence-add-delay" :disabled="disabled||sequence.steps.length>=KEYBOARD_SEQUENCE_LIMIT" @click="change({steps:[...sequence.steps,{delayMs:1000}]})">＋ 等待</button></div>
  <div class="sequence-ending"><button type="button" :aria-pressed="sequence.mode==='once'" :disabled="disabled" @click="change({mode:'once'})">执行一次</button><button type="button" :aria-pressed="sequence.mode==='toggle'" :disabled="disabled" @click="change({mode:'toggle'});">循环 · 再按停止</button></div>
  <div class="sequence-options"><label v-if="sequence.mode==='once'">执行<input type="number" min="1" max="100" :disabled="disabled" :value="sequence.repeatCount" @change="change({repeatCount:number($event,1,1,100)})">次</label><label>轮次间隔<input type="number" min="100" max="60000" step="50" :disabled="disabled" :value="sequence.intervalMs" @change="change({intervalMs:number($event,150,100,60000)})">ms</label></div>
  <p>{{sequence.mode==='toggle'?'触发后循环，再次使用同一输入立即停止并松开按键。':'每次触发执行所设次数，执行期间不会堆积重复任务。'}} 切换窗口、进入录制或修改规则时停止。</p>
  <ControllerOutputKeyboard v-if="editing!==null" :action-id="rule.actionId" :code="initialKeys[0]?.code||'KeyA'" :initial-keys="initialKeys" chord-only @combo="pick" @select="(actionId,code)=>pick([{actionId,code}])" @close="close"/>
 </section>
</template>
<style scoped>
.keyboard-sequence{display:grid;gap:10px;margin-top:12px;padding-top:12px;border-top:1px solid var(--border);font-size:12px;min-width:0}.keyboard-sequence header{display:flex;justify-content:space-between;gap:8px}.keyboard-sequence header span,.keyboard-sequence p{color:var(--text-dim)}.keyboard-sequence p{margin:0;line-height:1.55;font-size:11px}.sequence-steps{display:grid;gap:8px}.sequence-step{display:flex;align-items:center;gap:7px;flex-wrap:wrap;padding:9px;border:1px solid var(--border);border-radius:8px;background:var(--bg-panel)}button,input{font:inherit;color:var(--text);border:1px solid var(--border);border-radius:6px;background:var(--bg-input)}button{min-height:34px;padding:6px 9px;cursor:pointer}button:disabled{opacity:.4;cursor:default}button[aria-pressed=true]{color:var(--accent);border-color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,var(--bg-input))}.sequence-index{color:var(--text-dim)}.sequence-key{flex:1;min-width:100px;text-align:left;color:var(--accent)}label{display:flex;align-items:center;gap:5px;color:var(--text-dim);white-space:nowrap}input{width:65px;min-height:32px;padding:5px}.sequence-row-actions{display:flex;gap:4px;margin-left:auto}.sequence-row-actions button{padding:4px 7px;min-width:28px}.sequence-add,.sequence-ending{display:grid;grid-template-columns:1fr 1fr;gap:7px}.sequence-options{display:flex;flex-wrap:wrap;gap:12px}.sequence-wait{flex:1}
</style>
