<script setup lang="ts">
import { computed, useAttrs } from 'vue';
import SwitchControl from './SwitchControl.vue';
defineOptions({inheritAttrs:false});
const props=withDefaults(defineProps<{ modelValue:boolean;label?:string;description?:string;color?:'ac'|'dc'|'accent'|'danger';disabled?:boolean;compact?:boolean;gpRow?:number|string;gpCol?:number|string }>(),{color:'accent',disabled:false,compact:false});
const emit=defineEmits<{(event:'update:modelValue',value:boolean):void}>();
const attrs=useAttrs();
const accessibleLabel=computed(()=>String(attrs['aria-label']||props.label||props.description||''));
const rootAttrs=computed(()=>Object.fromEntries(Object.entries(attrs).filter(([key])=>!['aria-label','aria-describedby','title'].includes(key))));
function toggle(){if(!props.disabled)emit('update:modelValue',!props.modelValue)}
</script>
<template>
  <div v-bind="rootAttrs" class="toggle-row" :class="{disabled,compact,standalone:!label&&!description}" @click="toggle">
    <div v-if="label||description" class="toggle-text"><div v-if="label" class="toggle-label">{{label}}</div><div v-if="description" class="toggle-desc muted">{{description}}</div></div>
    <SwitchControl :model-value="modelValue" :label="accessibleLabel" :disabled="disabled" :color="color" :gp-row="gpRow" :gp-col="gpCol" :aria-describedby="attrs['aria-describedby']" :title="attrs.title" @update:model-value="value=>emit('update:modelValue',value)" />
  </div>
</template>
<style scoped>
.toggle-row{display:flex;align-items:center;justify-content:space-between;gap:10px;min-width:0;min-height:40px;padding:5px 0;box-sizing:border-box;cursor:pointer;border-radius:8px}.toggle-row.compact{padding:2px 0}.toggle-row.standalone{display:inline-flex;flex:0 0 auto;min-height:36px;padding:0}.toggle-text{flex:1;min-width:0;text-align:left}.toggle-label{font-size:14px;line-height:1.4;overflow-wrap:anywhere}.toggle-desc{font-size:12px;line-height:1.5;margin-top:3px;color:var(--text-dim)}.toggle-row.disabled{cursor:default}.toggle-row.disabled .toggle-text{opacity:.45}.toggle-row:has(.ymcc-switch.focused),.toggle-row:has(.ymcc-switch:focus-visible){background:color-mix(in srgb,var(--accent) 7%,transparent)}
/* Keep the existing row density; compact changes spacing, never switch size. */
:global(.app-root) .toggle-row:not(.standalone){padding:3px 0}
:global(.app-root) .toggle-row.compact,:global(.app-root) .toggle-row.standalone{padding:0}
</style>
