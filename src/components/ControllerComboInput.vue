<script setup lang="ts">
import { computed } from 'vue';
import { controllerOemDisplayCatalog } from '@/bridge/controllerOemPresentation';
import { CONTROLLER_BUTTONS, CONTROLLER_SHORTCUT_MAX_INPUTS, formatControllerShortcutInputs, isHandheldExtendedKey, type ControllerShortcutInput, type ControllerShortcutRecordMode, type OemKeysSnapshot } from '@/bridge/controllerShortcutRules';
import { CONTROLLER_DIAGRAMS, controllerDiagramPersona } from '@/bridge/controllerDeviceDiagram';
const props = defineProps<{ variant?: 'quick' | 'confirm' | 'picker'; mode?: ControllerShortcutRecordMode; persona: string; inputs: readonly ControllerShortcutInput[]; captured: readonly ControllerShortcutInput[]; candidate?: readonly ControllerShortcutInput[] | null; catalog: OemKeysSnapshot; recording: boolean; busy: boolean; recordingBusy: boolean; status: string }>();
const emit = defineEmits<{ (event: 'record'): void; (event: 'cancel-recording'): void; (event: 'clear'): void }>();
const visibleInputs = computed(() => props.recording ? props.captured : props.inputs);
const displayCatalog = computed(() => controllerOemDisplayCatalog(props.persona, props.catalog));
function label(input: ControllerShortcutInput): string {
 if (input.source === 'controller') { const b=CONTROLLER_BUTTONS.find(b=>b.value===input.code),c=CONTROLLER_DIAGRAMS[controllerDiagramPersona(props.persona)].controls.find(c=>c.buttonIndex===b?.gamepadIndex);return input.code==='leftThumb'?'L3':input.code==='rightThumb'?'R3':c?.label.split(' / ')[0]||b?.label||input.code; }
 if (input.source === 'oem') return displayCatalog.value.keys.find(k=>k.keyId===input.code)?.label || input.code;
 return isHandheldExtendedKey(input)?`掌机 ${input.code}`:formatControllerShortcutInputs([input]);
}
const traditionalKeys=computed(()=>visibleInputs.value.filter(i=>(i.source==='keyboard'||i.source==='mouse')&&!isHandheldExtendedKey(i)));
const mixed=computed(()=>traditionalKeys.value.length>0&&visibleInputs.value.some(i=>i.source==='controller'||i.source==='oem'||isHandheldExtendedKey(i)));
</script>
<template>
 <section class="combo-input-editor" data-input-flow="record" :data-recording="recording" data-record-control>
  <div class="combo-section-head"><strong>录制输入</strong><span>最多 {{ CONTROLLER_SHORTCUT_MAX_INPUTS }} 键</span></div>
  <div class="combo-current" :class="{capturing:recording}" aria-live="polite"><span class="combo-current-caption">{{recording?'正在识别':'当前组合'}}</span><div class="combo-chips"><template v-for="(input,index) in visibleInputs" :key="input.source+':'+input.code"><span v-if="index" class="combo-plus" aria-hidden="true">＋</span><span class="combo-chip" :data-input-source="input.source" :data-handheld-extended="isHandheldExtendedKey(input)">{{label(input)}}</span></template><span v-if="!visibleInputs.length" class="combo-empty">{{recording?'按住所需按键，再全部松开':'还没有录制组合'}}</span></div></div>
  <div class="record-actions"><button type="button" class="record-action" :class="{recording}" :disabled="busy||recordingBusy" @click="recording?emit('cancel-recording'):emit('record')">{{recording?'取消录制':inputs.length?'重新录制输入':'录制输入'}}</button><button type="button" class="clear-combination" :disabled="busy||recording||recordingBusy||!inputs.length" @click="emit('clear')">清空</button></div>
  <p v-if="mixed" class="mixed-notice" role="status">混合输入 · 含键盘：{{traditionalKeys.map(label).join(' + ')}}。可能来自掌机键盘映射，可保留使用。</p>
  <p class="capture-status" aria-live="polite">{{recording?status:'手柄、背键、专用键与键鼠均可录制和组合。'}}</p>
 </section>
</template>
<style scoped>
.combo-input-editor{display:grid;gap:10px;min-width:0}.combo-section-head{display:flex;align-items:center;justify-content:space-between;gap:8px}.combo-section-head strong{font-size:14px}.combo-section-head>span,.combo-current-caption{font-size:11px;color:var(--text-dim)}.combo-current{display:grid;gap:8px;min-height:62px;padding:10px;background:var(--bg-panel);border:1px solid var(--border);border-radius:8px}.combo-current.capturing{border-color:var(--accent)}.combo-chips{display:flex;align-items:center;flex-wrap:wrap;gap:5px;min-height:26px}.combo-chip{padding:5px 8px;border-radius:6px;background:color-mix(in srgb,var(--accent) 15%,var(--bg-input));color:var(--text);font-size:12px;font-weight:600}.combo-plus,.combo-empty{color:var(--text-dim);font-size:12px}.record-actions{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px}.record-actions button{min-height:40px;border:1px solid var(--border);border-radius:7px;background:var(--bg-input);color:var(--text);padding:8px 12px;font:inherit;font-size:12px;cursor:pointer}.record-actions .record-action{background:var(--accent);color:var(--text-on-accent,#fff);border-color:var(--accent);font-weight:700}.record-actions .recording{background:var(--bg-panel);color:var(--text)}button:disabled{opacity:.45;cursor:default}.capture-status,.mixed-notice{margin:0;font-size:11px;line-height:1.5;color:var(--text-dim)}.mixed-notice{color:var(--accent)}
</style>
