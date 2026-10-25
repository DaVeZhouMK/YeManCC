<script setup lang="ts">
import Toggle from './Toggle.vue';
defineProps<{modelValue:boolean;disabled?:boolean}>();
const emit=defineEmits<{(event:'update:modelValue',value:boolean):void}>();
</script>
<template>
  <div class="steam-wallpaper-playback steam-option-row" data-steam-wallpaper-setting="enabled" :data-state="modelValue?'on':'off'" :aria-busy="disabled || undefined">
    <div class="steam-wallpaper-playback-copy">
      <div class="steam-wallpaper-playback-label">动态壁纸</div>
      <div class="steam-wallpaper-playback-help">{{modelValue?'Steam 大屏播放当前游戏的动态壁纸':'已关闭播放，恢复 Steam 静态背景'}}</div>
    </div>
    <div class="steam-wallpaper-playback-control">
      <span class="steam-wallpaper-playback-state" aria-hidden="true">{{modelValue?'ON':'OFF'}}</span>
      <Toggle :model-value="modelValue" :disabled="disabled" aria-label="动态壁纸播放" @update:model-value="value=>emit('update:modelValue',value)" />
    </div>
  </div>
</template>
<style scoped>
.steam-wallpaper-playback{display:flex;align-items:center;justify-content:space-between;gap:16px;min-width:0;padding:8px 0}.steam-wallpaper-playback-copy{flex:1;min-width:0}.steam-wallpaper-playback-label{font-size:15px;line-height:1.4;font-weight:550}.steam-wallpaper-playback-help{font-size:12px;line-height:1.5;margin-top:4px;color:var(--text-dim)}.steam-wallpaper-playback-control{display:flex;align-items:center;gap:8px;flex:none}.steam-wallpaper-playback-state{font-size:12px;font-weight:600;color:var(--text-dim)}[data-state="on"] .steam-wallpaper-playback-state{color:var(--accent)}
</style>
