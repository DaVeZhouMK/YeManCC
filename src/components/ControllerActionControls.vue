<script setup lang="ts">
import SwitchControl from './SwitchControl.vue';
withDefaults(defineProps<{ name: string; enabled: boolean; disabled?: boolean }>(), { disabled: false });
const emit = defineEmits<{
  (event: 'toggle'): void;
  (event: 'remove'): void;
  (event: 'cancel'): void;
}>();
</script>

<template>
  <div class="action-controls" :aria-label="`${name}的动作操作`" @click.stop>
    <div class="action-enable-row">
      <span>启用动作</span>
      <SwitchControl :model-value="enabled" :label="`${name}：启用动作`" :disabled="disabled" @update:model-value="emit('toggle')" />
    </div>
    <div class="action-control-buttons">
      <button type="button" class="action-delete" :disabled="disabled" @click="emit('remove')">删除动作</button>
      <button type="button" class="action-manage-cancel" :aria-label="`取消${name}的动作操作`" :disabled="disabled" @click="emit('cancel')">取消</button>
    </div>
  </div>
</template>

<style scoped>
.action-controls{display:grid;gap:9px;min-width:0;color:var(--text);font-size:12px}
.action-enable-row{display:flex;align-items:center;justify-content:space-between;gap:8px;min-height:24px}
.action-control-buttons{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,.7fr);gap:6px}
.action-control-buttons button{min-width:0;min-height:32px;padding:5px 7px;border:1px solid rgba(255,255,255,.18);border-radius:7px;background:var(--bg-input);color:var(--text);font:inherit;cursor:var(--cursor-interaction,pointer)}
.action-control-buttons .action-delete{color:#ffb2b8;border-color:color-mix(in srgb,var(--danger) 40%,transparent);background:color-mix(in srgb,var(--danger) 8%,var(--bg-input))}
.action-controls button:disabled{opacity:.45;cursor:default}
@media(pointer:coarse){.action-control-buttons button{min-height:40px}.action-enable-row{min-height:40px}}
</style>
