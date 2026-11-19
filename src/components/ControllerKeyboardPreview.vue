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
const fillElements = new Map<string, readonly SVGElement[]>();
const blinkBase = ref<SVGSVGElement|null>(null), blinkLayer = ref<SVGSVGElement|null>(null);
const blinking = ref(false);
// Build a fill-only projection lazily; opening the keyboard does not clone all keys.
function copyFill(element: Element, layer: SVGSVGElement): SVGElement {
  const fill = element.cloneNode(false) as SVGElement;
  for (const attr of ['id','data-key-code','data-selected','data-key-focus','data-live']) fill.removeAttribute(attr);
  fill.setAttribute('stroke', 'none');
  let branch: SVGElement = fill;
  for (let parent = element.parentElement; parent && parent.tagName.toLowerCase() !== 'svg'; parent = parent.parentElement) {
    const group = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    if (parent.hasAttribute('transform')) group.setAttribute('transform', parent.getAttribute('transform')!);
    group.append(branch); branch = group;
  }
  layer.append(branch);
  return fill;
}
function updateArtwork() {
  const selected = new Set(props.inputs.map(input => input.code));
  const pressed = new Set(liveCodes.value);
  let blinkCount = 0;
  for (const [code, element] of keyElements) {
    const flags = [selected.has(code), props.focusedCode === code, pressed.has(code)];
    const previous = previousFlags.get(code);
    for (const [index, attribute] of ['data-selected', 'data-key-focus', 'data-live'].entries()) {
      if (flags[index] !== (previous?.[index] ?? false)) element.setAttribute(attribute, String(flags[index]));
    }
    const blink = flags[0] && !flags[2];
    if (blink) {
      blinkCount++;
      if (!fillElements.has(code)) fillElements.set(code, [copyFill(element, blinkBase.value!), copyFill(element, blinkLayer.value!)]);
    }
    if (blink !== Boolean(previous?.[0] && !previous?.[2])) {
      for (const fill of fillElements.get(code) || []) fill.style.display = blink ? '' : 'none';
    }
    previousFlags.set(code, flags);
  }
  blinking.value = blinkCount > 0;
}
onMounted(() => {
  const background = root.value?.querySelector('.keyboard-asset #rect5300');
  if (background) copyFill(background, blinkBase.value!);
  root.value?.querySelectorAll('.keyboard-asset [data-key-code]').forEach(element => {
    const code = element.getAttribute('data-key-code')!;
    keyElements.set(code, element);

  });
  updateArtwork();
});
watch([() => props.inputs.map(input => input.code), () => props.focusedCode, liveCodes], updateArtwork, { flush: 'post' });
function enabled(code:string){return !props.enabledCodes||props.enabledCodes.includes(code)}
</script>
<template><div ref="root" class="keyboard-preview" :class="{selectable}" aria-label="键盘输入预览"><div class="keyboard-board"><svg ref="blinkBase" class="keyboard-blink-base" viewBox="0 0 684 225" aria-hidden="true"/><svg ref="blinkLayer" class="keyboard-blink-layer" :class="{blinking}" viewBox="0 0 684 225" aria-hidden="true"/><div class="keyboard-asset" v-html="artwork"/><div v-if="selectable" class="keyboard-hit-layer"><button v-for="key in KEYBOARD_KEY_LAYOUT" :key="key.code" v-memo="[enabled(key.code), inputs.some(i=>i.code===key.code)]" type="button" :data-key-code="key.code" :aria-label="'选择 '+key.code" :disabled="!enabled(key.code)" :aria-pressed="inputs.some(i=>i.code===key.code)" :style="{left:(key.x/684*100)+'%',top:(key.y/225*100)+'%',width:(key.w/684*100)+'%',height:(key.h/225*100)+'%'}" @focus="emit('focus-key',key.code)" @click="emit('select',key.code)"/></div></div><div v-if="!selectable" class="keyboard-pressed" aria-live="polite"><span v-for="input in inputs" :key="input.code">{{formatControllerShortcutInputs([input])}}</span></div></div></template>
<style scoped>
.keyboard-preview{display:grid;align-content:center;gap:24px;width:100%;height:100%;box-sizing:border-box;padding:14px 5px}.keyboard-board{position:relative;width:100%;aspect-ratio:684/225}.keyboard-asset{width:100%;color:var(--text)}.keyboard-asset :deep(svg){display:block;width:100%;height:auto;overflow:visible}.keyboard-asset :deep(path){fill:var(--text);stroke:none}.keyboard-asset :deep(path[id^="rect"]){fill:var(--bg-panel);stroke:var(--text-dim);stroke-width:1.5}.keyboard-asset :deep(path[data-selected=true]){fill:transparent;stroke:var(--accent)}.keyboard-asset :deep(path[data-key-focus=true]){fill:color-mix(in srgb,var(--accent) 35%,var(--bg-panel));stroke:var(--accent);stroke-width:2}.keyboard-asset :deep(path[data-live=true]){fill:var(--accent);stroke:var(--accent);stroke-width:2}.keyboard-pressed{display:flex;justify-content:center;flex-wrap:wrap;gap:7px}.keyboard-pressed span{font-size:14px;font-weight:600;border:1px solid var(--accent);border-radius:7px;padding:8px 12px;background:color-mix(in srgb,var(--accent) 18%,var(--bg-input));color:var(--accent)}.keyboard-hit-layer{position:absolute;inset:0}.keyboard-hit-layer button{position:absolute;min-width:0;min-height:0;padding:0;border:0;border-radius:3px;background:transparent;cursor:pointer}.keyboard-hit-layer button:disabled{cursor:default;background:color-mix(in srgb,var(--bg-panel) 65%,transparent)}.keyboard-hit-layer button:focus-visible{outline:2px solid var(--accent);outline-offset:1px}.keyboard-preview.selectable{display:block;height:auto;padding:0}
.keyboard-asset :deep(path[data-selected=true][data-key-focus=true]:not([data-live=true])){fill:transparent}.keyboard-asset{position:relative}.keyboard-asset :deep(#rect5300){fill:transparent}.keyboard-blink-base,.keyboard-blink-layer{position:absolute;inset:0;width:100%;height:100%;overflow:visible;pointer-events:none}
.keyboard-blink-base{fill:var(--bg-panel)}.keyboard-blink-layer{fill:var(--accent);opacity:.22}
.keyboard-blink-layer.blinking{animation:keyboard-mapped-fill .85s ease-in-out infinite;will-change:opacity}
@keyframes keyboard-mapped-fill{0%,100%{opacity:1}50%{opacity:.22}}
@media(prefers-reduced-motion:reduce){.keyboard-blink-layer.blinking{animation:none}}
</style>
