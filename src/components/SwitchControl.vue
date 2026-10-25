<script setup lang="ts">
import { computed } from 'vue';
const props = withDefaults(defineProps<{ modelValue: boolean; label?: string; disabled?: boolean; color?: 'ac'|'dc'|'accent'|'danger'; mixed?: boolean; gpRow?: number|string; gpCol?: number|string }>(), { disabled:false, color:'accent', mixed:false });
const emit = defineEmits<{ (event:'update:modelValue',value:boolean):void }>();
const colorValue = computed(() => props.color==='dc'?'var(--dc-accent)':props.color==='danger'?'var(--danger)':'var(--accent)');
function toggle(event: MouseEvent): void { event.stopPropagation(); if(!props.disabled) emit('update:modelValue', !props.modelValue); }
</script>
<template>
  <button type="button" class="ymcc-switch" :class="{on:modelValue,mixed}" role="switch" :aria-checked="modelValue" :aria-label="label" :disabled="disabled" :data-gp-row="gpRow" :data-gp-col="gpCol" :style="{'--switch-color':colorValue}" @click="toggle"><span class="ymcc-switch-track" aria-hidden="true"><span class="ymcc-switch-knob" /></span></button>
</template>
<style scoped>
.ymcc-switch{box-sizing:border-box;display:inline-flex;align-items:center;justify-content:center;flex:0 0 52px;width:52px;min-width:52px;height:36px;min-height:36px;max-height:36px;padding:0;margin:0;border:0;border-radius:9px;background:transparent;color:inherit;cursor:pointer;touch-action:manipulation;vertical-align:middle}
.ymcc-switch-track{box-sizing:border-box;position:relative;display:block;flex:0 0 48px;width:48px;height:26px;border:1px solid color-mix(in srgb,var(--text) 18%,var(--bg-input));border-radius:999px;background:var(--bg-input);transition:background .15s,border-color .15s}
.ymcc-switch-knob{box-sizing:border-box;display:block;position:absolute;top:2px;left:2px;width:20px;height:20px;border-radius:50%;background:#cfd8e3;box-shadow:0 1px 3px #0004;transition:transform .15s,background .15s}
.on .ymcc-switch-track{background:var(--switch-color);border-color:var(--switch-color)}.on .ymcc-switch-knob{transform:translateX(22px);background:#fff}.mixed:not(.on) .ymcc-switch-track{border-color:var(--switch-color);background:color-mix(in srgb,var(--switch-color) 18%,var(--bg-input))}.mixed:not(.on) .ymcc-switch-knob{transform:translateX(11px)}
.ymcc-switch:focus-visible,.ymcc-switch.focused{outline:2px solid var(--accent);outline-offset:2px;box-shadow:none}.ymcc-switch:disabled{opacity:.45;cursor:default}@media(prefers-reduced-motion:reduce){.ymcc-switch-track,.ymcc-switch-knob{transition:none}}
</style>
