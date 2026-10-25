<script setup lang="ts">
import {computed,shallowRef,watch,onUnmounted} from 'vue';
import AppIcon from './AppIcon.vue';
import {mouseModeText,type MouseModePresentation} from '@/bridge/mouseModePresentation';
import {createMouseStatusHint} from '@/bridge/mouseStatusHint';
const props=withDefaults(defineProps<{state:MouseModePresentation|null;enabled?:boolean}>(),{enabled:true});
const shown=shallowRef<MouseModePresentation|null>(null);
const hint=createMouseStatusHint({emit:value=>shown.value=value,setTimer:(fn,ms)=>setTimeout(fn,ms),clearTimer:id=>clearTimeout(id as ReturnType<typeof setTimeout>)});
watch(()=>[props.state,props.enabled] as const,([state,enabled])=>hint.accept(state,enabled),{immediate:true,flush:'sync'});
onUnmounted(()=>hint.dispose());
const label=computed(()=>mouseModeText(shown.value));
</script>
<template>
 <div v-if="shown" class="mouse-mode-watermark" :class="{'mouse-mode-watermark--active':shown.on}" role="status" aria-live="polite" :aria-label="label.title" data-mouse-watermark :data-mouse-mode="shown.mode">
  <span class="mouse-watermark-icon"><AppIcon :name="label.icon"/></span><span class="mouse-watermark-text"><strong>{{label.title}}</strong></span>
 </div>
</template>
<style scoped>
.mouse-mode-watermark{position:absolute;right:20px;bottom:16px;display:flex;align-items:center;gap:9px;color:#dbeaf8;opacity:.86;pointer-events:none;font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;filter:drop-shadow(0 2px 3px #0009);z-index:2;max-width:240px}
.mouse-watermark-icon{position:relative;display:flex;width:40px;height:40px;font-size:40px;flex:none}.mouse-watermark-icon>.app-icon{width:100%;height:100%}.mouse-watermark-text{display:flex;align-items:center}.mouse-watermark-text strong{font-size:13px;font-weight:500;line-height:1.25}
</style>
