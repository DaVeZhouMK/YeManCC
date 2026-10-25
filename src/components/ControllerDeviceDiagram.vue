<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch, type CSSProperties } from 'vue';
import { CONTROLLER_DIAGRAMS, controllerDiagramPersona, diagramAxis, diagramControlState, diagramOemKey, type DiagramControl } from '@/bridge/controllerDeviceDiagram';
import ControllerVectorArtwork from '@/components/ControllerVectorArtwork';
import { controllerOemDisplayCatalog } from '@/bridge/controllerOemPresentation';
import { OEM_EMPTY_SNAPSHOT, type OemKeysSnapshot } from '@/bridge/controllerShortcutRules';

const props = withDefaults(defineProps<{
  persona?: string;
  imageSrc?: string;
  revision?: number;
  buttons?: readonly boolean[];
  axes?: readonly number[];
  mappedButtons?: readonly number[];
  pressedOemKeys?: readonly string[];
  mappedOemKeys?: readonly string[];
  oemKeys?: OemKeysSnapshot;
  interactive?: boolean;
}>(), { persona: 'steamdeck', revision: 0, buttons: () => [], axes: () => [], mappedButtons: () => [], pressedOemKeys: () => [], mappedOemKeys: () => [], oemKeys: () => OEM_EMPTY_SNAPSHOT, interactive: false });
const emit = defineEmits<{
  (event: 'image-error'): void;
  (event: 'simulate', value: { code: string; buttonIndex?: number; pressed: boolean; axes: readonly number[] }): void;
}>();
const persona = computed(() => controllerDiagramPersona(props.persona));
const model = computed(() => CONTROLLER_DIAGRAMS[persona.value]);
const displayOemKeys = computed(() => controllerOemDisplayCatalog(persona.value, props.oemKeys));
const held = ref(new Set<string>());
const localAxes = ref([0, 0, 0, 0]);
const dragging = reactive(new Map<number, { control: DiagramControl; element: HTMLElement }>());
const pulseTimers = new Set<ReturnType<typeof setTimeout>>();

// Axis samples must not rewrite every fixed hit target's style and event props.
const controlStyles = computed(() => new Map(model.value.controls.map(control => {
  const [x, y, width, height] = control.box;
  return [control, { left: `${x / model.value.width * 100}%`, top: `${y / model.value.height * 100}%`, width: `${width / model.value.width * 100}%`, height: `${height / model.value.height * 100}%`, clipPath: control.hitClip } as CSSProperties];
})));
const controlStates = computed(() => new Map(model.value.controls.map(control => {
  const real = diagramControlState(control, props.buttons, props.mappedButtons, props.pressedOemKeys, props.mappedOemKeys, displayOemKeys.value);
  return [control, { ...real, live: real.live || (real.available && held.value.has(control.code)) }];
})));
function style(control: DiagramControl): CSSProperties { return controlStyles.value.get(control)!; }
function state(control: DiagramControl) { return controlStates.value.get(control)!; }
function label(control: DiagramControl): string {
  const key = diagramOemKey(control, displayOemKeys.value);
  const name = control.backIndex != null && persona.value === 'steamdeck' ? control.code
    : control.backIndex != null ? `背部按键 ${control.backIndex}${key ? ` · ${key.label}` : ''}` : control.label;
  return !state(control).available ? `${name}，${key ? '输入采集暂不可用' : '未识别'}` : name;
}
function axis(control: DiagramControl, component: number): number {
  const index = (control.stick || 0) * 2 + component;
  const isDragging = Array.from(dragging.values()).some(item => item.control.code === control.code);
  return diagramAxis(isDragging ? localAxes.value[index] : props.axes[index]);
}
function moved(control: DiagramControl): boolean {
  return control.stick != null && Math.hypot(axis(control, 0), axis(control, 1)) > .002;
}
const paintedControls = computed(() => new Set(model.value.controls.filter(control => {
  const value = state(control);
  return value.mapped || value.live;
}).map(control => control.code)));
const liveControls = computed(()=>new Set(model.value.controls.filter(control=>state(control).live).map(control=>control.code)));
const availableControls = computed(() => new Set(model.value.controls.filter(control => state(control).available).map(control => control.code)));
const presentationAxes = computed(() => {
  const left = model.value.controls.find(control => control.stick === 0)!;
  const right = model.value.controls.find(control => control.stick === 1)!;
  return [axis(left, 0), axis(left, 1), axis(right, 0), axis(right, 1)];
});
function setHeld(control: DiagramControl, pressed: boolean) {
  if (pressed && (!props.interactive || !state(control).available)) return;
  const next = new Set(held.value);
  if (pressed) next.add(control.code); else next.delete(control.code);
  held.value = next;
  emit('simulate', { code: control.code, buttonIndex: control.buttonIndex, pressed, axes: [...localAxes.value] });
}
function moveStick(control: DiagramControl, event: PointerEvent, element: HTMLElement) {
  const rect = element.getBoundingClientRect();
  let x = (event.clientX - rect.left - rect.width / 2) / (rect.width / 2);
  let y = -(event.clientY - rect.top - rect.height / 2) / (rect.height / 2);
  const length = Math.hypot(x, y);
  if (length > 1) { x /= length; y /= length; }
  const next = [...localAxes.value]; const start = control.stick! * 2;
  next[start] = diagramAxis(x); next[start + 1] = diagramAxis(y); localAxes.value = next;
  emit('simulate', { code: control.code, buttonIndex: control.buttonIndex, pressed: false, axes: [...next] });
}
function pointerDown(control: DiagramControl, event: PointerEvent) {
  if (!props.interactive || !state(control).available || event.button !== 0) return;
  event.preventDefault();
  const element = event.currentTarget as HTMLElement;
  element.setPointerCapture(event.pointerId);
  dragging.set(event.pointerId, { control, element });
  if (control.stick != null) moveStick(control, event, element); else setHeld(control, true);
}
function pointerMove(event: PointerEvent) {
  const item = dragging.get(event.pointerId);
  if (item?.control.stick != null) moveStick(item.control, event, item.element);
}
function pointerEnd(event: PointerEvent) {
  const item = dragging.get(event.pointerId); if (!item) return;
  dragging.delete(event.pointerId);
  if (item.control.stick != null) {
    const next = [...localAxes.value], start = item.control.stick * 2;
    next[start] = 0; next[start + 1] = 0; localAxes.value = next;
    emit('simulate', { code: item.control.code, buttonIndex: item.control.buttonIndex, pressed: false, axes: [...next] });
  } else setHeld(item.control, false);
}
function pulse(control: DiagramControl) {
  if (!props.interactive || control.stick == null) return;
  setHeld(control, true);
  const timer = setTimeout(() => { setHeld(control, false); pulseTimers.delete(timer); }, 180);
  pulseTimers.add(timer);
}
function resetSimulation() {
  for (const timer of pulseTimers) clearTimeout(timer);
  pulseTimers.clear();
  dragging.clear(); held.value = new Set(); localAxes.value = [0, 0, 0, 0];
}
watch(() => [props.persona, props.interactive, props.oemKeys], resetSimulation);
onMounted(() => window.addEventListener('blur', resetSimulation));
onBeforeUnmount(() => {
  window.removeEventListener('blur', resetSimulation);
  for (const timer of pulseTimers) clearTimeout(timer);
  pulseTimers.clear(); resetSimulation();
});
</script>

<template>
  <figure class="device-diagram" :style="{ '--device-ratio': String(model.width / model.height) }" :data-diagram-persona="persona" :aria-label="`${persona === 'ps5' ? 'PS5' : persona === 'steamdeck' ? 'Steam Deck' : 'Xbox'} 按键与摇杆输入镜像`">
    <ControllerVectorArtwork :key="`${persona}-${revision}`" :model="model" :active="paintedControls" :live="liveControls" :available="availableControls" :axes="presentationAxes" />
    <button v-for="control in model.controls" :key="`${persona}-${control.code}`" v-memo="[model, displayOemKeys, interactive, state(control).live, state(control).mapped, state(control).available, moved(control)]" class="device-key" :class="{ live: state(control).live, mapped: state(control).mapped, unavailable: !state(control).available, stick: control.stick != null, displaced: moved(control) }"
      type="button" data-gp-ignore :data-control="control.code" :data-back-index="control.backIndex" :data-oem-key="diagramOemKey(control, displayOemKeys)?.keyId" :data-button-index="control.buttonIndex" :data-axis-index="control.stick != null ? control.stick * 2 : undefined" :style="style(control)" :disabled="!interactive || !state(control).available" :aria-label="control.stick != null ? `${label(control)}；拖动方向，双击按下` : label(control)" :aria-pressed="state(control).live" :data-tooltip="label(control)"
      @pointerdown="pointerDown(control, $event)" @pointermove="pointerMove" @pointerup="pointerEnd" @pointercancel="pointerEnd" @lostpointercapture="pointerEnd" @dblclick="pulse(control)"
      @keydown.space.prevent="setHeld(control, true)" @keyup.space.prevent="setHeld(control, false)" @keydown.enter.prevent="setHeld(control, true)" @keyup.enter.prevent="setHeld(control, false)">
    </button>
  </figure>
</template>

<style scoped>
.device-diagram{position:relative;display:block;width:100%;margin:0;color:#d1d6dc;overflow:visible;user-select:none}
.device-diagram :deep(.device-base){display:block;width:100%;height:auto;max-width:100%;overflow:visible;pointer-events:none;filter:none}
.device-key{position:absolute;margin:0;padding:0;min-width:0;min-height:0;border:0;border-radius:0;background:transparent;opacity:1;touch-action:none;cursor:var(--cursor-interaction,pointer);appearance:none;-webkit-appearance:none}
.device-key:disabled{opacity:1;cursor:default}
.device-key.stick{cursor:grab}.device-key.stick:active{cursor:grabbing}
/* Animate the fill only; the Steam outlines, glyphs and stick transforms stay intact. */
.device-diagram :deep([data-fill-active="true"][data-fill-live="false"]){animation:controller-mapped-fill .85s ease-in-out infinite}
@keyframes controller-mapped-fill{0%,100%{fill:var(--diagram-live-color,#4897d2)}50%{fill:color-mix(in srgb,var(--diagram-live-color,#4897d2) 25%,transparent)}}
@media(prefers-reduced-motion:reduce){.device-diagram :deep([data-fill-active="true"][data-fill-live="false"]){animation:none}}
</style>
