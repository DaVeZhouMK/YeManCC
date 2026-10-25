<script setup lang="ts">
import { computed, nextTick, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, watch } from 'vue';
import { focusGamepadElement } from '@/gamepad/focus';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { dragBackgroundPosition, normalizeBackgroundPosition, type BackgroundPosition } from '@/bridge/backgroundPosition';

const props = defineProps<{ opacity: number; blur: number; theme: string; mediaUrl?: string; mediaKind?: string; position?: BackgroundPosition }>();
const emit = defineEmits<{ 'update:position': [value: BackgroundPosition]; change: [value: BackgroundPosition] }>();
const entry = ref<HTMLButtonElement | null>(null), editing = ref(false), editDraft = ref<BackgroundPosition | null>(null);
let editOrigin: BackgroundPosition | null = null;
const root = ref<HTMLElement | null>(null), video = ref<HTMLVideoElement | null>(null), image = ref<HTMLImageElement | null>(null);
const onScreen = ref(false), active = ref(true), uiVisible = ref(isUiVisible()), mediaError = ref(false), dragging = ref(false);
const reduced = window.matchMedia('(prefers-reduced-motion:reduce)'), reduceMotion = ref(reduced.matches);
const position = computed(() => normalizeBackgroundPosition(editDraft.value || props.position));
const hasMedia = computed(() => Boolean(props.mediaUrl) && !mediaError.value);
const running = computed(() => active.value && onScreen.value && uiVisible.value && !reduceMotion.value);
const mediaStyle = computed(() => ({
  opacity: String(Number.isFinite(props.opacity) ? Math.min(.8, Math.max(.2, props.opacity)) : .64),
  filter: props.blur > 0 ? `blur(${Math.min(24, props.blur)}px)` : 'none',
  objectPosition: `${position.value.x}% ${position.value.y}%`,
}));
const centered = computed(() => position.value.x === 50 && position.value.y === 50);
let intersection: IntersectionObserver | undefined, stopVisibility: (() => void) | undefined;
let drag: { id: number; x: number; y: number; origin: BackgroundPosition; latest: BackgroundPosition; viewport: { width: number; height: number }; media: { width: number; height: number } } | null = null;
let keyboardDraft = false;
function playback() {
  const element = video.value;
  if (!element) return;
  if (running.value && hasMedia.value) void element.play().catch(() => {});
  else element.pause();
}
function motionChanged() { reduceMotion.value = reduced.matches; }
function update(value: BackgroundPosition) { emit('update:position', normalizeBackgroundPosition(value)); }
function startDrag(event: PointerEvent) {
  if (drag || !event.isPrimary || !hasMedia.value || event.button !== 0 || !root.value || (event.target as HTMLElement).closest('button')) return;
  finishEdit(false);
  const rect = root.value.getBoundingClientRect();
  const media = video.value ? { width: video.value.videoWidth, height: video.value.videoHeight }
    : { width: image.value?.naturalWidth || 0, height: image.value?.naturalHeight || 0 };
  if (!media.width || !media.height) return;
  event.preventDefault();
  root.value.focus({ preventScroll: true });
  root.value.setPointerCapture(event.pointerId);
  drag = { id: event.pointerId, x: event.clientX, y: event.clientY, origin: { ...position.value }, latest: { ...position.value }, viewport: rect, media };
  dragging.value = true;
}
function moveDrag(event: PointerEvent) {
  if (!drag || drag.id !== event.pointerId) return;
  drag.latest = dragBackgroundPosition(drag.origin, event.clientX - drag.x, event.clientY - drag.y, drag.viewport, drag.media);
  update(drag.latest);
}
function finishDrag(commit: boolean) {
  const owned = drag;
  if (!owned) return;
  drag = null; dragging.value = false;
  if (root.value?.hasPointerCapture(owned.id)) root.value.releasePointerCapture(owned.id);
  if (commit) emit('change', { ...owned.latest });
  else update(owned.origin);
}
function endDrag(event: PointerEvent) { if (drag?.id === event.pointerId) { moveDrag(event); finishDrag(true); } }
function cancelDrag(event?: PointerEvent) { if (!event || drag?.id === event.pointerId) finishDrag(false); }
const positionKeys = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home'];
function keydown(event: KeyboardEvent) {
  if (event.defaultPrevented) return;
  if (event.key === 'Escape' && editing.value) { event.preventDefault(); event.stopPropagation(); finishEdit(false); return; }
  if (event.key === 'Escape' && drag) { event.preventDefault(); event.stopPropagation(); cancelDrag(); return; }
  if (!hasMedia.value || !positionKeys.includes(event.key)) return;
  event.preventDefault(); event.stopPropagation();
  const step = event.shiftKey ? 10 : 2;
  const next = normalizeBackgroundPosition(event.key === 'Home' ? { x: 50, y: 50 } : {
    x: position.value.x + (event.key === 'ArrowLeft' ? -step : event.key === 'ArrowRight' ? step : 0),
    y: position.value.y + (event.key === 'ArrowUp' ? -step : event.key === 'ArrowDown' ? step : 0),
  });
  if (editing.value) editDraft.value = next;
  update(next);
  if (!editing.value) keyboardDraft = true;
}
function commitKeyboard(event?: KeyboardEvent) {
  if (!keyboardDraft || (event && !positionKeys.includes(event.key))) return;
  keyboardDraft = false; emit('change', { ...position.value });
}
function center() { finishEdit(false); cancelDrag(); update({ x: 50, y: 50 }); emit('change', { x: 50, y: 50 }); }
function beginEdit() {
  if (!hasMedia.value || !running.value || !entry.value) return;
  cancelDrag(); commitKeyboard();
  editOrigin = { ...position.value }; editDraft.value = { ...position.value }; editing.value = true;
  focusGamepadElement(entry.value);
}
function finishEdit(commit: boolean) {
  if (!editing.value) return;
  const value = commit ? { ...position.value } : editOrigin;
  editing.value = false; editDraft.value = null; editOrigin = null;
  if (value) { update(value); if (commit) emit('change', value); }
}
function toggleEdit() { if (editing.value) finishEdit(true); else beginEdit(); }
function pageCommand(event: Event) {
  const command = event as CustomEvent<{ action: string }>, action = command.detail?.action;
  if (!active.value || !uiVisible.value || !hasMedia.value || !root.value) return;
  const foreignModal = [...document.querySelectorAll<HTMLElement>('[data-gp-modal]')].some(modal => {
    const rect = modal.getBoundingClientRect(), style = getComputedStyle(modal);
    return rect.width > 0 && rect.height > 0 && style.visibility !== 'hidden' && style.display !== 'none' && !modal.contains(root.value);
  });
  if (foreignModal) { finishEdit(false); return; }
  if (!editing.value) {
    if (action === 'confirm' && document.activeElement === entry.value) { command.preventDefault(); beginEdit(); }
    return;
  }
  if (!root.value.contains(document.activeElement)) { finishEdit(false); return; }
  command.preventDefault();
  if (action === 'confirm') finishEdit(true);
  else if (action === 'back') finishEdit(false);
  else if (['nav-left', 'nav-right', 'nav-up', 'nav-down'].includes(action)) {
    editDraft.value = normalizeBackgroundPosition({
      x: position.value.x + (action === 'nav-left' ? -2 : action === 'nav-right' ? 2 : 0),
      y: position.value.y + (action === 'nav-up' ? -2 : action === 'nav-down' ? 2 : 0),
    });
    update(editDraft.value);
  }
}
function focusout(event: FocusEvent) { if (!root.value?.contains(event.relatedTarget as Node | null)) finishEdit(false); }
function suspend() { finishEdit(false); cancelDrag(); commitKeyboard(); video.value?.pause(); }
watch(hasMedia, value => { if (!value) suspend(); });
watch(running, () => { if (!running.value) suspend(); playback(); });
watch(() => [props.mediaUrl, props.mediaKind], async () => { finishEdit(false); cancelDrag(); mediaError.value = false; await nextTick(); playback(); });
onMounted(() => {
  intersection = new IntersectionObserver(entries => { onScreen.value = entries.some(entry => entry.isIntersecting); });
  if (root.value) intersection.observe(root.value);
  stopVisibility = onUiVisibilityChange(state => { uiVisible.value = state.visible; });
  reduced.addEventListener('change', motionChanged);
  window.addEventListener('gp:page-command', pageCommand);
});
onActivated(() => { active.value = true; playback(); });
onDeactivated(() => { active.value = false; suspend(); });
onBeforeUnmount(() => { active.value = false; suspend(); intersection?.disconnect(); stopVisibility?.(); reduced.removeEventListener('change', motionChanged); window.removeEventListener('gp:page-command', pageCommand); });
</script>

<template>
  <div class="background-effect-preview" :data-theme="theme">
    <div ref="root" class="wallpaper-preview-frame" :class="{ 'is-running': running, 'is-draggable': hasMedia, 'is-dragging': dragging, 'is-editing': editing }"
      role="group" tabindex="-1" aria-label="动态预览，拖动调整壁纸位置" @focusout="focusout"
      @pointerdown="startDrag" @pointermove="moveDrag" @pointerup="endDrag" @pointercancel="cancelDrag" @lostpointercapture="cancelDrag"
      @keydown="keydown" @keyup="commitKeyboard" @blur="commitKeyboard()">
      <video v-if="hasMedia && mediaKind === 'video'" ref="video" class="wallpaper-preview-media" :src="mediaUrl" :style="mediaStyle"
        muted loop playsinline preload="metadata" disablepictureinpicture tabindex="-1" aria-hidden="true" @canplay="playback" @error="mediaError = true" />
      <img v-else-if="hasMedia" ref="image" class="wallpaper-preview-media" :src="mediaUrl" :style="mediaStyle" alt="" draggable="false" @error="mediaError = true" />
      <div v-else class="wallpaper-preview-ambient" :style="{ opacity: mediaStyle.opacity, filter: mediaStyle.filter }" aria-hidden="true"><span /></div>
      <button ref="entry" type="button" class="wallpaper-preview-label" :disabled="!hasMedia" :aria-pressed="editing" aria-label="动态预览，按 A 调整壁纸位置" @click="toggleEdit">动态预览</button>
      <span class="wallpaper-preview-hint">{{ editing ? '方向键 / 摇杆移动 · A 保存 · B 取消' : hasMedia ? '拖动调整位置 · A 手柄调整' : mediaError ? '壁纸暂不可用 · 动态效果' : '默认动态效果' }}</span>
    </div>
    <div class="wallpaper-preview-position"><span>{{ hasMedia ? `位置 ${Math.round(position.x)}% · ${Math.round(position.y)}%` : '选择壁纸后可拖动调整位置' }}</span><button type="button" :disabled="!hasMedia || centered" @click="center">居中</button></div>
  </div>
</template>

<style scoped>
.background-effect-preview{width:100%;min-width:0}
.wallpaper-preview-frame{position:relative;isolation:isolate;overflow:hidden;width:100%;aspect-ratio:580/780;border:1px solid var(--console-line);border-radius:9px;background:#070d15;contain:paint;box-sizing:border-box;outline-offset:3px}
.wallpaper-preview-media,.wallpaper-preview-ambient{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;pointer-events:none;user-select:none}
.is-draggable{cursor:grab;touch-action:none}.is-dragging{cursor:grabbing}
.wallpaper-preview-label,.wallpaper-preview-hint{position:absolute;z-index:1;left:8px;right:8px;pointer-events:none;color:#f4f8ff;text-shadow:0 1px 3px #000;background:#08121bcc;border-radius:5px;padding:5px 7px;font-size:11px;line-height:1.5}
.wallpaper-preview-label{top:8px;right:auto;font-weight:600;border:1px solid #ffffff35;pointer-events:auto;cursor:pointer}.wallpaper-preview-label:disabled{cursor:default}.wallpaper-preview-label:focus-visible,.wallpaper-preview-label.focused{outline:2px solid var(--accent);outline-offset:2px}.is-editing{outline:2px solid var(--accent);outline-offset:2px}.wallpaper-preview-hint{bottom:8px;text-align:center}
.wallpaper-preview-position{display:flex;align-items:center;justify-content:space-between;gap:6px;margin-top:7px;font-size:11px;line-height:1.5;color:var(--text-dim)}
.wallpaper-preview-position button{flex:none;min-height:28px;padding:3px 8px;border:1px solid var(--console-line);border-radius:6px;background:var(--console-surface);color:var(--text);font-size:12px}
.wallpaper-preview-position button:disabled{opacity:.45}
.wallpaper-preview-ambient{background:linear-gradient(145deg,#09182b,#142e43 50%,#18172f)}
.wallpaper-preview-ambient span{position:absolute;inset:-20%;background:radial-gradient(ellipse at 28% 32%,#4189a58c,transparent 52%),radial-gradient(ellipse at 72% 68%,#926cbc80,transparent 50%);animation:wallpaper-preview-drift 12s ease-in-out infinite alternate;animation-play-state:paused;will-change:transform}
.is-running .wallpaper-preview-ambient span{animation-play-state:running}
@keyframes wallpaper-preview-drift{from{transform:translate(-4%,-3%) rotate(-6deg)}to{transform:translate(4%,3%) rotate(6deg)}}
@media(prefers-reduced-motion:reduce){.wallpaper-preview-ambient span{animation:none;will-change:auto}}
</style>
