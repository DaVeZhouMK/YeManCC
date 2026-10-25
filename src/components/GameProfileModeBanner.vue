<script setup lang="ts">
import OverflowGameName from './OverflowGameName.vue';
defineProps<{ gameName: string; compact?: boolean }>();
</script>
<template><div class="profile-editing-notice" :class="{compact,'with-actions':$slots.actions}" role="status" aria-atomic="true" aria-label="当前编辑专属游戏配置"><strong class="profile-mode-label">专属模式</strong><OverflowGameName :name="gameName" /><div v-if="$slots.actions" class="profile-actions"><slot name="actions" /></div></div></template>
<style scoped>
.profile-editing-notice{display:grid;grid-template-columns:auto minmax(0,1fr);align-items:center;gap:10px;margin:4px 0 10px;padding:9px 11px;border:1px solid var(--accent-border);border-radius:10px;background:var(--accent-surface);color:var(--text);font-size:14px;line-height:1.5;min-width:0;box-sizing:border-box}.profile-mode-label{color:var(--accent);font-weight:700;white-space:nowrap}.profile-editing-notice :deep(.overflow-game-name){font-weight:600;min-width:0}
.profile-editing-notice.compact{margin:0;padding:5px 7px;gap:6px;min-height:36px;font-size:12px;border-radius:7px;line-height:1.35}.profile-editing-notice.compact :deep(.overflow-game-name){font-size:12px}
.profile-editing-notice.with-actions{grid-template-columns:auto minmax(0,1fr) auto}.profile-actions{display:flex;align-items:center;min-width:0}
</style>
