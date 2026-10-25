<script setup lang="ts">
import { computed, nextTick, onMounted, onBeforeUnmount, ref, watch } from 'vue';
import ControllerOutputKeyboard from '@/components/ControllerOutputKeyboard.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import InlineIcon from '@/components/InlineIcon.vue';
import { CONTROLLER_SHORTCUT_ACTIONS, actionById, type ControllerShortcutRule } from '@/bridge/controllerShortcutRules';
const props=defineProps<{variant:'tiles'|'steps'|'library';rule:ControllerShortcutRule;disabled:boolean;unselected?:boolean;summaryLabel?:string}>();
const root=ref<HTMLElement|null>(null);
const emit=defineEmits<{(event:'open',value:boolean):void;(event:'action',id:string):void;(event:'key',actionId:string,code:string):void}>();
const keyboardOpen=ref(false);
const opened=ref(false),group=ref('系统动作'),step=ref(0);
const groups=['系统动作','键盘按键','输入功能'];

const available=CONTROLLER_SHORTCUT_ACTIONS.filter(a=>a.available);
const current=computed(()=>actionById(props.rule.actionId));
const isKeyboard=computed(()=>props.rule.actionId.startsWith('keyboard.'));
const currentKey=computed(()=>current.value?.parameters?.find(p=>p.id==='key')?.options?.find(o=>o.value===props.rule.params.key)?.label||String(props.rule.params.key||''));
const summary=computed(()=>props.unselected?'选择要执行的动作':props.summaryLabel?props.summaryLabel:isKeyboard.value?`键盘 · ${currentKey.value}`:current.value?.label||'选择输出动作');
const iconFor=(id:string)=>id.startsWith('keyboard.')?'keyboard':id.startsWith('input.')||id.startsWith('mouse.')?'gamepad':id.startsWith('display.')?'monitor':id.startsWith('power.')?'bolt':id.startsWith('window.')?'monitor':'settings';
const shown=computed(()=>available.filter(a=>group.value==='输入功能'?a.category==='输入':a.category!=='输入'&&!a.id.startsWith('keyboard.')));

const canChange=computed(()=>props.rule.origin!=='native-default');
watch(keyboardOpen,value=>{emit('open',value||opened.value);if(!value)void nextTick(()=>focusGamepadElement(root.value?.querySelector<HTMLElement>(opened.value?'.output-categories button[aria-pressed=true], .output-types button':'.output-summary')||null))});
watch(opened,value=>{emit('open',value||keyboardOpen.value);void nextTick(()=>{const target=value?root.value?.querySelector<HTMLElement>('.output-types button, .output-categories button[aria-pressed=true], .output-choice'):root.value?.querySelector<HTMLElement>('.output-summary');focusGamepadElement(target || null)})});
onMounted(()=>window.addEventListener('ipc:gamepad-back',back));
onBeforeUnmount(()=>window.removeEventListener('ipc:gamepad-back',back));
watch(()=>props.rule.id,()=>{opened.value=false;keyboardOpen.value=false;step.value=0});
function toggle(){if(props.disabled||(!canChange.value&&!isKeyboard.value))return;if(isKeyboard.value&&!props.unselected){keyboardOpen.value=true;return}opened.value=!opened.value;group.value=isKeyboard.value?'键盘按键':current.value?.category==='输入'?'输入功能':'系统动作';step.value=0;}
function chooseGroup(value:string){if(value==='键盘按键'){keyboardOpen.value=true;return}group.value=value;step.value=1;}
function pick(id:string){emit('action',id);opened.value=false;step.value=0;}
function pickKey(actionId:string,code:string){emit('key',actionId,code);keyboardOpen.value=false;opened.value=false;step.value=0;}
function escapeOutput(event:KeyboardEvent){if(opened.value){event.preventDefault();event.stopPropagation();opened.value=false}}
function back(event:Event){if(keyboardOpen.value)return;if(!opened.value||!root.value?.getClientRects().length)return;event.preventDefault();event.stopImmediatePropagation();if(props.variant==='steps'&&step.value){step.value=0;return}opened.value=false;}
</script>
<template>
 <section ref="root" class="output-action-editor" :data-output-variant="variant" @keydown.esc="escapeOutput" @ipc:gamepad-back="back">
  <div class="output-heading"><strong>输出动作</strong><button v-if="isKeyboard&&canChange" class="output-type-change" type="button" :disabled="disabled" @click="opened=!opened;step=0;group='系统动作'">更换输出类型</button><span v-else>执行什么</span></div>
  <button type="button" class="output-summary" :disabled="disabled||(!canChange&&!isKeyboard)" :aria-expanded="opened||keyboardOpen" aria-haspopup="dialog" @click="toggle"><InlineIcon :name="iconFor(rule.actionId)"/><span class="output-summary-copy"><b>{{summary}}</b><small>{{canChange?'选择功能或键盘按键':isKeyboard?'默认键盘动作 · 可更换按键':'默认动作 · 可编辑输入与触发'}}</small></span><span v-if="canChange || isKeyboard" class="output-change">点击更换动作</span></button>
  <div v-if="opened&&!keyboardOpen" class="output-picker" :class="{'output-library-overlay':variant==='library'}" :data-gp-modal="variant==='library'?'output-actions':undefined" role="dialog" aria-label="选择输出动作">
   <div class="picker-head"><strong>{{variant==='steps'&&step===0&&canChange?'先选输出类型':'选择输出动作'}}</strong><button type="button" @click="opened=false">取消</button></div>
   <template v-if="variant==='steps'&&step===0&&canChange"><div class="output-types"><button v-for="g in groups" :key="g" type="button" @click="chooseGroup(g)"><InlineIcon :name="g==='键盘按键'?'keyboard':g==='输入功能'?'gamepad':'settings'"/><span>{{g}}</span><span aria-hidden="true">›</span></button></div></template>
   <template v-else>
    <div class="output-categories"><button v-if="variant==='steps'" type="button" @click="step=0">‹ 类型</button><button v-for="g in (!canChange?['键盘按键']:variant==='steps'?[group]:groups)" :key="g" type="button" :aria-pressed="group===g" @click="chooseGroup(g)">{{g}}</button></div>
    <div class="output-action-grid"><button v-for="action in shown" :key="action.id" type="button" class="output-choice" :aria-pressed="rule.actionId===action.id" @click="pick(action.id)"><InlineIcon :name="iconFor(action.id)"/><span>{{action.label}}</span></button></div>
   </template>
  </div>
  <slot v-if="!opened||variant==='library'"/>
  <ControllerOutputKeyboard v-if="keyboardOpen" :action-id="rule.actionId" :code="String(rule.params.key||'')" :fixed-action="!canChange" @select="pickKey" @close="keyboardOpen=false"/>
 </section>
</template>
<style scoped>
.output-action-editor{position:relative;display:grid;gap:9px;min-width:0}.output-heading{display:flex;align-items:center;justify-content:space-between}.output-heading strong{font-size:15px}.output-type-change{padding:4px 8px;font-size:11px}.output-heading>span{font-size:11px;color:var(--text-dim)}button{font:inherit;color:var(--text);border:1px solid var(--border);background:var(--bg-input);border-radius:7px;cursor:pointer}button:disabled{opacity:.65;cursor:default}.output-summary{display:grid;grid-template-columns:24px minmax(0,1fr);align-items:center;gap:11px;min-height:64px;width:100%;padding:12px;text-align:left;border-color:color-mix(in srgb,var(--accent) 42%,var(--border));background:color-mix(in srgb,var(--accent) 9%,var(--bg-panel))}.output-summary>.inline-icon{font-size:23px;flex-shrink:0;color:var(--accent)}.output-summary-copy{display:grid;gap:5px;min-width:0}.output-summary b{font-size:16px}.output-summary small{font-size:11px;color:var(--text-dim)}.output-change{grid-column:1/-1;display:flex;justify-content:center;align-items:center;min-height:40px;font-size:14px;font-weight:700;background:color-mix(in srgb,var(--accent) 20%,var(--bg-input));border:1px solid var(--accent);border-radius:7px;color:var(--accent);white-space:nowrap}.picker-head{display:flex;justify-content:space-between;align-items:center;gap:10px}.picker-head strong{font-size:13px}.picker-head button{padding:7px 12px}.output-picker{display:grid;align-content:start;gap:9px;min-width:0}.output-categories,.keyboard-categories{display:flex;gap:5px;flex-wrap:wrap}.output-categories button{flex:1;min-height:36px;padding:6px;font-size:12px;white-space:nowrap}.keyboard-categories button{padding:6px 8px;min-height:32px;font-size:11px}.output-action-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px}.output-choice{display:flex;align-items:center;gap:8px;min-height:43px;padding:8px;text-align:left;font-size:12px}.output-choice .inline-icon{font-size:17px;color:var(--text-dim);flex-shrink:0}.output-key-grid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:6px}.output-key{display:grid;place-items:center;gap:3px;min-height:54px;padding:5px 2px;font-size:12px}button[aria-pressed=true]{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 16%,var(--bg-input));color:var(--accent)}.output-types{display:grid;gap:8px}.output-types button{display:flex;align-items:center;gap:12px;padding:12px;min-height:56px;font-size:14px}.output-types button>span:last-child{margin-left:auto}.output-library-overlay{position:fixed;inset:100px 175px 90px;z-index:25;padding:18px 22px;overflow:auto;border:1px solid var(--border);border-radius:12px;background:var(--bg-solid);box-shadow:0 0 0 1500px rgba(0,0,0,.55)}.output-library-overlay .output-action-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.output-library-overlay .output-choice{min-height:62px;font-size:14px}.output-library-overlay .output-key-grid{grid-template-columns:repeat(11,minmax(0,1fr))}.output-library-overlay .output-key{min-height:48px;font-size:14px}
</style>
