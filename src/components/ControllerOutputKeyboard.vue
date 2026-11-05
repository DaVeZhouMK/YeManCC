<script setup lang="ts">
import { computed, nextTick, onMounted, onBeforeUnmount, ref } from 'vue';
import ControllerKeyboardPreview from '@/components/ControllerKeyboardPreview.vue';
import KeyboardKeyGlyph from '@/components/KeyboardKeyGlyph.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { KEYBOARD_KEY_LAYOUT } from '@/bridge/keyboardKeyLayout';
import { type KeyboardOutputKey, keyboardOutputKeyLabel } from '@/bridge/controllerKeyboardSequence';
import { CONTROLLER_SHORTCUT_ACTIONS } from '@/bridge/controllerShortcutRules';
const props=defineProps<{actionId:string;code:string;fixedAction?:boolean;initialKeys?:KeyboardOutputKey[];chordOnly?:boolean}>();
const emit=defineEmits<{(event:'select',actionId:string,code:string):void;(event:'combo',keys:KeyboardOutputKey[]):void;(event:'close'):void}>();
const root=ref<HTMLElement|null>(null),focused=ref(props.code||'KeyA'),symbols=ref(props.actionId==='keyboard.symbol');
const comboMode=ref(!!props.chordOnly||(props.initialKeys?.length||0)>1), selectedKeys=ref<KeyboardOutputKey[]>(props.initialKeys?.map(k=>({...k}))||[]);
const previewInputs=computed(()=>comboMode.value?selectedKeys.value.map(k=>({source:'keyboard' as const,code:k.code})):props.code?[{source:'keyboard' as const,code:props.code}]:[]);
const canConfirm=computed(()=>{const keys=new Set(selectedKeys.value.map(k=>k.code));if(selectedKeys.value.some(k=>k.actionId==='keyboard.symbol'))keys.add('ShiftLeft');return keys.size>0&&keys.size<=4});
function confirmCombo(){if(canConfirm.value)emit('combo',selectedKeys.value.map(k=>({...k})))}
const previous=document.activeElement instanceof HTMLElement?document.activeElement:null;
function outputFor(code:string){const ids=symbols.value?['keyboard.symbol','keyboard.letter','keyboard.numeric','keyboard.leftFunction','keyboard.rightFunction']:['keyboard.letter','keyboard.numeric','keyboard.leftFunction','keyboard.rightFunction'];for(const id of ids){const a=CONTROLLER_SHORTCUT_ACTIONS.find(a=>a.id===id),o=a?.parameters?.find(p=>p.id==='key')?.options?.find(o=>o.value===code);if(o)return{actionId:id,code,label:o.label}}return undefined}
const enabledCodes=computed<string[]>(()=>KEYBOARD_KEY_LAYOUT.filter(k=>outputFor(k.code)).map(k=>k.code));
const label=computed(()=>outputFor(focused.value)?.label||focused.value);
function focusCode(code:string){focused.value=code;void nextTick(()=>focusGamepadElement(root.value?.querySelector<HTMLElement>('[data-key-code="'+code+'"]:is(button)')||null))}
function move(dx:number,dy:number){const from=KEYBOARD_KEY_LAYOUT.find(k=>k.code===focused.value);if(!from)return false;const x=from.x+from.w/2,y=from.y+from.h/2;const targets=KEYBOARD_KEY_LAYOUT.filter(k=>enabledCodes.value.includes(k.code)&&k.code!==from.code).map(k=>({k,dx:k.x+k.w/2-x,dy:k.y+k.h/2-y})).filter(v=>dx?v.dx*dx>1: v.dy*dy>1).sort((a,b)=>{const score=(v:typeof a)=>dx?Math.abs(v.dx)+Math.abs(v.dy)*5:Math.abs(v.dy)+Math.abs(v.dx)*2;return score(a)-score(b)});if(targets[0]){focusCode(targets[0].k.code);return true}return false}
function navigate(dx:number,dy:number){if(move(dx,dy))return;if(dy>0&&comboMode.value)focusGamepadElement(root.value?.querySelector<HTMLElement>('.keyboard-combo-confirm:not(:disabled)')||null);else if(dy<0)focusGamepadElement(root.value?.querySelector<HTMLElement>('header button')||null)}
function spatial(event:Event){
  const e=event as CustomEvent<{dx:number;dy:number}>;
  // The shared engine dispatches on the inline-edit container; direct keyboard
  // events originate from a key. Resolve both within this keyboard's own root.
  const active=(root.value?.getRootNode() as Document|ShadowRoot|undefined)?.activeElement;
  const source=(event.target as Element)?.closest?.('.keyboard-hit-layer')?event.target:active;
  if(!(source instanceof Element)||!root.value?.contains(source)||!source.closest('.keyboard-hit-layer'))return;
  event.preventDefault();navigate(e.detail.dx,e.detail.dy);
}
function keydown(event:KeyboardEvent){const direction:Record<string,[number,number]>={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]};if(event.key==='Escape'){event.preventDefault();event.stopPropagation();emit('close')}else if(direction[event.key]&&(event.target as Element)?.closest('.keyboard-hit-layer')){event.preventDefault();navigate(...direction[event.key])}}
function back(event:Event){if(!root.value?.getClientRects().length)return;event.preventDefault();event.stopImmediatePropagation();emit('close')}
function select(code:string){const output=outputFor(code);if(!output)return;if(!comboMode.value){emit('select',output.actionId,output.code);return}const index=selectedKeys.value.findIndex(k=>k.code===code);if(index>=0)selectedKeys.value.splice(index,1);else if(selectedKeys.value.length<4)selectedKeys.value.push({actionId:output.actionId,code:output.code})}
onMounted(()=>{window.addEventListener('ipc:gamepad-back',back,true);const code=enabledCodes.value.includes(focused.value)?focused.value:enabledCodes.value[0];if(code)focusCode(code)});
onBeforeUnmount(()=>{window.removeEventListener('ipc:gamepad-back',back,true);if(previous?.isConnected)focusGamepadElement(previous)});
</script>
<template><div class="keyboard-output-overlay" @pointerdown.self="emit('close')"><section ref="root" class="keyboard-output-dialog" role="dialog" aria-modal="true" aria-label="选择键盘输出" data-gp-modal="keyboard-output" data-gp-inline-edit @gp:spatial-edit="spatial" @keydown="keydown"><header><div><h3>键盘输出</h3><p>{{fixedAction?'默认快捷：更换键盘输出':'选择要发送的按键'}}</p></div><button v-if="!chordOnly" type="button" :aria-pressed="comboMode" @click="comboMode=!comboMode">{{comboMode?'组合键':'单个按键'}}</button><button type="button" :aria-pressed="symbols" @click="symbols=!symbols">符号层 {{symbols?'开':'关'}}</button><button type="button" class="keyboard-close" @click="emit('close')"><span class="controller-key" data-controller-key="B">B</span> 返回</button></header><ControllerKeyboardPreview :inputs="previewInputs" selectable :focused-code="focused" :enabled-codes="enabledCodes" @select="select" @focus-key="focused=$event"/><div v-if="comboMode" class="keyboard-chord"><span>{{selectedKeys.length?selectedKeys.map(keyboardOutputKeyLabel).join(' + '):'选择 1 至 4 个键，例如 Ctrl + S'}}</span><button class="keyboard-combo-confirm" type="button" :disabled="!canConfirm" @click="confirmCombo">使用组合按键</button></div><footer><span class="keyboard-current"><KeyboardKeyGlyph :code="focused" :label="label"/><b>{{label}}</b></span><span>十字键 / 摇杆移动 <span class="controller-key" data-controller-key="A">A</span> 选择 <span class="controller-key" data-controller-key="B">B</span> 返回</span></footer></section></div></template>
<style scoped>
.keyboard-output-overlay{position:fixed;inset:0;z-index:50;display:grid;place-items:center;background:rgba(0,0,0,.65)}.keyboard-output-dialog{width:calc(100% - 72px);max-width:1180px;box-sizing:border-box;padding:22px 24px;border:1px solid var(--border);border-radius:14px;background:var(--bg-solid);color:var(--text);display:grid;gap:22px}.keyboard-output-dialog header{display:flex;align-items:center;gap:12px}.keyboard-output-dialog header>div{flex:1}.keyboard-output-dialog h3{font-size:20px;margin:0 0 6px}.keyboard-output-dialog p{font-size:12px;margin:0;color:var(--text-dim)}.keyboard-output-dialog button:not([data-key-code]){min-height:40px;padding:8px 14px;border:1px solid var(--border);border-radius:7px;background:var(--bg-input);color:var(--text);font:inherit;font-size:12px;cursor:pointer}.keyboard-close b{color:#f04e54;margin-right:5px}.keyboard-output-dialog footer{display:flex;align-items:center;justify-content:space-between;font-size:12px;color:var(--text-dim)}.keyboard-current{display:flex;align-items:center;gap:10px;color:var(--text);font-size:16px}.keyboard-output-dialog footer>span:last-child b{padding:3px 7px;margin:0 5px;background:var(--bg-input);border-radius:50%;color:var(--text)}
.keyboard-chord{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 14px;border:1px solid var(--accent);border-radius:8px;color:var(--accent);font-size:15px}.keyboard-output-dialog button[aria-pressed=true]{border-color:var(--accent);color:var(--accent)}.keyboard-output-dialog{max-height:calc(100% - 32px);overflow:auto;gap:14px}
</style>
