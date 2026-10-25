<script lang="ts">
import { KEYBOARD_SVG } from '@/bridge/keyboardPreviewAsset';
import { KEYBOARD_KEY_LAYOUT } from '@/bridge/keyboardKeyLayout';
const keyboardCodesById = new Map<string, string>(KEYBOARD_KEY_LAYOUT.map(key => [key.id, key.code]));
const artwork = KEYBOARD_SVG.replace(/id="([^"]+)"/g, (original, id: string) => {
  const code = keyboardCodesById.get(id);
  return code ? `${original} data-key-code="${code}" data-selected="false" data-key-focus="false" data-live="false"` : original;
});
</script>
<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useKeyboardPressFeedback } from '@/composables/useKeyboardPressFeedback';
import { formatControllerShortcutInputs, type ControllerShortcutInput } from '@/bridge/controllerShortcutRules';
const props=withDefaults(defineProps<{inputs:readonly ControllerShortcutInput[];selectable?:boolean;focusedCode?:string;enabledCodes?:readonly string[];pressedCodes?:readonly string[]}>(),{selectable:false,focusedCode:'',pressedCodes:()=>[]});
const root=ref<HTMLElement|null>(null);
const localLiveCodes=useKeyboardPressFeedback(root);
const liveCodes=computed(()=>[...localLiveCodes.value,...props.pressedCodes]);
const emit=defineEmits<{(event:'select',code:string):void;(event:'focus-key',code:string):void}>();
// The original artwork is inserted once. Selection/live changes only touch key flags,
// preserving the SVG nodes and the current phase of unrelated blink animations.
const keyElements = new Map<string, Element>();
const previousFlags = new Map<string, readonly boolean[]>();
function updateArtwork() {
  const selected = new Set(props.inputs.map(input => input.code));
  const pressed = new Set(liveCodes.value);
  for (const [code, element] of keyElements) {
    const flags = [selected.has(code), props.focusedCode === code, pressed.has(code)];
    const previous = previousFlags.get(code);
    for (const [index, attribute] of ['data-selected', 'data-key-focus', 'data-live'].entries()) {
      if (flags[index] !== (previous?.[index] ?? false)) element.setAttribute(attribute, String(flags[index]));
    }
    previousFlags.set(code, flags);
  }
}
onMounted(() => {
  root.value?.querySelectorAll('.keyboard-asset [data-key-code]').forEach(element => {
    keyElements.set(element.getAttribute('data-key-code')!, element);
  });
  updateArtwork();
});
watch([() => props.inputs.map(input => input.code), () => props.focusedCode, liveCodes], updateArtwork, { flush: 'post' });
function enabled(code:string){return !props.enabledCodes||props.enabledCodes.includes(code)}
</script>
<template><div ref="root" class="keyboard-preview" :class="{selectable}" aria-label="键盘输入预览"><div class="keyboard-board"><div class="keyboard-asset" v-html="artwork"/><div v-if="selectable" class="keyboard-hit-layer"><button v-for="key in KEYBOARD_KEY_LAYOUT" :key="key.code" type="button" :data-key-code="key.code" :aria-label="'选择 '+key.code" :disabled="!enabled(key.code)" :aria-pressed="inputs.some(i=>i.code===key.code)" :style="{left:(key.x/684*100)+'%',top:(key.y/225*100)+'%',width:(key.w/684*100)+'%',height:(key.h/225*100)+'%'}" @focus="emit('focus-key',key.code)" @click="emit('select',key.code)"/></div></div><div v-if="!selectable" class="keyboard-pressed" aria-live="polite"><span v-for="input in inputs" :key="input.code">{{formatControllerShortcutInputs([input])}}</span></div></div></template>
<style scoped>
.keyboard-preview{display:grid;align-content:center;gap:24px;width:100%;height:100%;box-sizing:border-box;padding:14px 5px}.keyboard-board{position:relative;width:100%;aspect-ratio:684/225}.keyboard-asset{width:100%;color:var(--text)}.keyboard-asset :deep(svg){display:block;width:100%;height:auto;overflow:visible}.keyboard-asset :deep(path){fill:var(--text);stroke:none}.keyboard-asset :deep(path[id^="rect"]){fill:var(--bg-panel);stroke:var(--text-dim);stroke-width:1.5}.keyboard-asset :deep(path[data-selected=true]){fill:color-mix(in srgb,var(--accent) 22%,var(--bg-panel));stroke:var(--accent)}.keyboard-asset :deep(path[data-key-focus=true]){fill:color-mix(in srgb,var(--accent) 35%,var(--bg-panel));stroke:var(--accent);stroke-width:2}.keyboard-asset :deep(path[data-live=true]){fill:var(--accent);stroke:var(--accent);stroke-width:2}.keyboard-pressed{display:flex;justify-content:center;flex-wrap:wrap;gap:7px}.keyboard-pressed span{font-size:14px;font-weight:600;border:1px solid var(--accent);border-radius:7px;padding:8px 12px;background:color-mix(in srgb,var(--accent) 18%,var(--bg-input));color:var(--accent)}.keyboard-hit-layer{position:absolute;inset:0}.keyboard-hit-layer button{position:absolute;min-width:0;min-height:0;padding:0;border:0;border-radius:3px;background:transparent;cursor:pointer}.keyboard-hit-layer button:disabled{cursor:default;background:color-mix(in srgb,var(--bg-panel) 65%,transparent)}.keyboard-hit-layer button:focus-visible{outline:2px solid var(--accent);outline-offset:1px}.keyboard-preview.selectable{display:block;height:auto;padding:0}
.keyboard-asset :deep(path[data-selected="true"]:not([data-live="true"])){animation:keyboard-mapped-fill .85s ease-in-out infinite}
@keyframes keyboard-mapped-fill{0%,100%{fill:var(--accent)}50%{fill:color-mix(in srgb,var(--accent) 22%,var(--bg-panel))}}
@media(prefers-reduced-motion:reduce){.keyboard-asset :deep(path[data-selected="true"]:not([data-live="true"])){animation:none}}
</style>
