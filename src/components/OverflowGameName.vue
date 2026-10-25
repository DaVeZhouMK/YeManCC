<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
const props = withDefaults(defineProps<{ name: string; interactive?: boolean }>(), { interactive: true });
const viewport = ref<HTMLElement | null>(null), text = ref<HTMLElement | null>(null), overflow = ref(false);
let observer: ResizeObserver | null = null, timer: ReturnType<typeof setInterval> | undefined;
let holdUntil = 0, atEnd = false;
const hovered = ref(false);
const focusable = computed(() => overflow.value && props.interactive);
function measure(): void { const el = viewport.value; overflow.value = !!el && el.scrollWidth > el.clientWidth + 2; }
function pause(): void { holdUntil = performance.now() + 6000; }
function advance(delta: number): boolean {
  const el = viewport.value;
  if (!el || !overflow.value) return false;
  const next = Math.max(0, Math.min(el.scrollWidth - el.clientWidth, el.scrollLeft + delta));
  if (Math.abs(next - el.scrollLeft) < .5) return false;
  pause(); el.scrollLeft = next; atEnd = false; return true;
}
function wheel(event: WheelEvent): void { if (advance(event.deltaX || event.deltaY)) event.preventDefault(); }
function key(event: KeyboardEvent): void {
  const delta = event.key === 'ArrowRight' ? 70 : event.key === 'ArrowLeft' ? -70 : 0;
  if (delta && advance(delta)) { event.preventDefault(); event.stopPropagation(); }
}
function spatial(event: Event): void {
  const dx = (event as CustomEvent<{ dx: number }>).detail?.dx || 0;
  if (dx && advance(dx * 70)) event.preventDefault();
}
function tick(): void {
  const el = viewport.value, now = performance.now();
  if (!el || !overflow.value || !el.clientWidth || now < holdUntil || hovered.value || matchMedia('(prefers-reduced-motion: reduce)').matches || el.contains(document.activeElement)) return;
  const maximum = el.scrollWidth - el.clientWidth;
  if (atEnd) { el.scrollLeft = 0; atEnd = false; holdUntil = now + 2200; return; }
  el.scrollLeft = Math.min(maximum, el.scrollLeft + 1.5);
  if (el.scrollLeft >= maximum - 1) { atEnd = true; holdUntil = now + 2200; }
}
watch(() => props.name, () => { if (viewport.value) viewport.value.scrollLeft = 0; atEnd = false; holdUntil = performance.now() + 2600; void nextTick(measure); });
onMounted(() => { observer = new ResizeObserver(measure); if (viewport.value) observer.observe(viewport.value); if (text.value) observer.observe(text.value); measure(); holdUntil = performance.now() + 2600; timer = setInterval(tick, 50); });
onBeforeUnmount(() => { observer?.disconnect(); if (timer) clearInterval(timer); });
</script>
<template>
  <span ref="viewport" class="overflow-game-name" :class="{ overflowing: overflow }" :tabindex="focusable ? 0 : undefined" :role="focusable ? 'region' : undefined" :aria-label="focusable ? `游戏名称：${name}，左右滚动查看` : undefined" :data-gp-inline-edit="focusable ? '' : undefined" :title="name" @wheel="wheel" @keydown="key" @gp:spatial-edit="spatial" @pointerenter="hovered = true" @pointerleave="hovered = false; pause()" @pointerdown="pause" @focus="pause" @touchstart.passive="pause"><span ref="text" class="overflow-game-name-text">{{ name }}</span></span>
</template>
<style scoped>
.overflow-game-name{display:block;min-width:0;max-width:100%;overflow-x:auto;overflow-y:hidden;white-space:nowrap;text-overflow:clip!important;scrollbar-width:none;touch-action:pan-x;overscroll-behavior-x:contain;line-height:inherit}.overflow-game-name::-webkit-scrollbar{display:none}.overflow-game-name-text{display:inline-block;white-space:nowrap;min-width:100%}.overflow-game-name.overflowing:focus-visible{outline:2px solid var(--accent);outline-offset:3px;border-radius:3px}
</style>
