<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref, watch } from 'vue';
import { BOOT_MODE_PREVIEWS } from '@/bridge/bootModePreview';
import { BOOT_MODE_LABELS, type BootModeId } from '@/bridge/bootModeModel';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';

const props = withDefaults(defineProps<{ mode: BootModeId; active?: boolean }>(), { active: true });
const root = ref<HTMLElement | null>(null), video = ref<HTMLVideoElement | null>(null);
const pageActive = ref(true), onScreen = ref(false), uiVisible = ref(isUiVisible());
const videoFailed = ref(false), posterFailed = ref(false), playing = ref(false);
const media = computed(() => BOOT_MODE_PREVIEWS[props.mode]);
const running = computed(() => props.active && pageActive.value && onScreen.value && uiVisible.value && !videoFailed.value);
let observer: IntersectionObserver | undefined, stopVisibility: (() => void) | undefined;
let playGeneration = 0;

function release() {
  ++playGeneration;
  playing.value = false;
  const element = video.value;
  if (!element) return;
  element.pause();
  // Removing the source also releases a hidden preview's decoder and network work.
  if (element.hasAttribute('src')) {
    element.removeAttribute('src');
    element.load();
  }
}
function playback() {
  const element = video.value;
  if (!element || !running.value) { release(); return; }
  if (element.getAttribute('src') !== media.value.src) {
    release();
    element.src = media.value.src;
    element.load();
  }
  element.muted = true;
  const generation = ++playGeneration;
  void element.play().catch(error => {
    // load/pause legitimately abort an earlier play request during a mode switch.
    if (generation === playGeneration && running.value && error?.name !== 'AbortError') videoFailed.value = true;
  });
}
function started() {
  if (running.value && video.value?.getAttribute('src') === media.value.src) playing.value = true;
  else release();
}
function failed() {
  if (running.value && video.value?.getAttribute('src') === media.value.src) videoFailed.value = true;
}
watch(() => props.mode, () => { release(); videoFailed.value = false; posterFailed.value = false; }, { flush: 'sync' });
watch([video, running, () => media.value.src], playback, { flush: 'post' });
onMounted(() => {
  observer = new IntersectionObserver(entries => { onScreen.value = entries.some(entry => entry.isIntersecting); });
  if (root.value) observer.observe(root.value);
  stopVisibility = onUiVisibilityChange(state => { uiVisible.value = state.visible; });
});
onActivated(() => { pageActive.value = true; uiVisible.value = isUiVisible(); });
onDeactivated(() => { pageActive.value = false; release(); });
onBeforeUnmount(() => { pageActive.value = false; release(); observer?.disconnect(); stopVisibility?.(); });
</script>

<template>
  <div ref="root" class="boot-mode-media" :data-boot-preview="mode" :data-boot-playback="videoFailed ? 'poster' : playing ? 'playing' : 'idle'">
    <img v-if="!posterFailed" :src="media.fallback" :alt="BOOT_MODE_LABELS[mode] + '示意图'" @error="posterFailed = true">
    <div v-else class="boot-mode-media-fallback">
      <svg v-if="mode === 'global'" viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M2 2h9v9H2zm11 0h9v9h-9zM2 13h9v9H2zm11 0h9v9h-9z"/></svg>
      <span>{{ mode === 'global' ? '全局配置' : BOOT_MODE_LABELS[mode] }}<small>{{ mode === 'global' ? '自定义配置' : '模式预览' }}</small></span>
    </div>
    <video ref="video" class="boot-mode-video" :class="{ 'is-playing': playing }" :poster="media.fallback"
      autoplay muted loop playsinline preload="metadata" :controls="false" :tabindex="-1" aria-hidden="true"
      disablepictureinpicture disableremoteplayback @playing="started" @error="failed"/>
  </div>
</template>

<style scoped>
.boot-mode-media{position:relative;width:100%;height:100%;overflow:hidden;pointer-events:none}
.boot-mode-media>img,.boot-mode-video{display:block;width:100%;height:100%;object-fit:cover}
.boot-mode-video{position:absolute;inset:0;opacity:0}.boot-mode-video.is-playing{opacity:1}
.boot-mode-media-fallback{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;gap:16px;background:radial-gradient(ellipse,#165d98,#081b2a);color:#d9efff}
.boot-mode-media-fallback>svg{width:42px;height:42px;flex-shrink:0}.boot-mode-media-fallback>span{display:grid;gap:3px;font-size:20px}.boot-mode-media-fallback small{font-size:14px;color:#a6c5dd}
</style>
