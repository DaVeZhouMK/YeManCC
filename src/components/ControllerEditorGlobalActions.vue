<script setup lang="ts">
withDefaults(defineProps<{ mode?: 'all' | 'draft' | 'commit'; dirty: boolean; pending?: boolean; busy: boolean; recording: boolean }>(), { mode: 'all', pending: false });
const emit = defineEmits<{
  (event: 'restore'): void;
  (event: 'discard'): void;
  (event: 'apply'): void;
  (event: 'close'): void;
}>();
</script>

<template>
  <div class="editor-global-actions" role="group" :aria-label="mode === 'all' ? '全局操作' : mode === 'draft' ? '全局操作：草稿管理' : '全局操作：应用与关闭'">
    <template v-if="mode !== 'commit'">
      <button class="global-action reset-all-action" data-editor-action="restore" type="button" :disabled="busy || recording" @click="emit('restore')">恢复默认</button>
      <button class="global-action discard-all-action" data-editor-action="discard" type="button" :disabled="busy || recording || (!dirty && !pending)" @click="emit('discard')">取消修改</button>
    </template>
    <template v-if="mode !== 'draft'">
      <button class="global-action apply-action" data-editor-action="apply" type="button" :disabled="busy || recording || !dirty || pending" @click="emit('apply')">{{ busy ? '保存中…' : '应用修改' }}</button>
      <button class="global-action close-editor-action cancel" data-editor-action="close" type="button" aria-label="关闭页面" :disabled="busy" @click="emit('close')"><strong class="quick-key-b" aria-hidden="true">B</strong>关闭页面</button>
    </template>
  </div>
</template>

<style scoped>
/* Same neutral surface, radius and red B cue as YMCC's GameQuickActions footer. */
.editor-global-actions{display:flex;align-items:center;gap:8px;min-width:0}
.global-action{display:inline-flex;align-items:center;justify-content:center;gap:6px;box-sizing:border-box;flex:0 0 112px;width:112px;height:40px;min-height:40px;padding:0 12px;border:1px solid rgba(255,255,255,.08);border-radius:7px;background:var(--bg-input);color:var(--text);font-family:inherit;font-size:12px;font-weight:600;line-height:1.25;white-space:nowrap;cursor:var(--cursor-interaction,pointer)}
.quick-key-b{color:var(--danger);font-weight:800}
.global-action:disabled{color:var(--text-dim);opacity:.6;cursor:default}
.global-action.focused,.global-action:focus-visible{box-shadow:var(--focus-ring)}
@media(max-width:650px){.editor-global-actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));width:100%}.global-action{width:100%;min-width:0}}
</style>
