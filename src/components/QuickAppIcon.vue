<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { isDefaultQuickAppPath } from '@/bridge/quickAppIcons';
import { getQuickAppFileIcon } from '@/bridge/quickAppFileIcons';
import type { QuickAppIconKind, QuickAppIconVariant } from '@/bridge/quickAppIcons';

const props = withDefaults(defineProps<{
  kind: QuickAppIconKind;
  variant?: QuickAppIconVariant;
  appPath?: string;
}>(), { variant: 'a' });

const PATHS: Record<string, string> = {
  'computer-a': '<rect x="3" y="4" width="14" height="11" rx="1.6"/><path d="M7 19h6M9 15v4M5 20h14"/><path d="M19 9h2v10h-6V9h4Z"/>',
  'computer-b': '<rect x="3" y="4" width="18" height="12" rx="1.8"/><path d="M8 20h8M10 16v4M14 16v4"/><path d="M6.5 8.5h11M6.5 11.5h6"/>',
  'browser-a': '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18M7 6.5h.01M10 6.5h.01M13 6.5h.01"/><circle cx="12" cy="14" r="3.8"/><path d="M8.2 14h7.6M12 10.2a6 6 0 0 1 0 7.6"/>',
  'browser-b': '<circle cx="12" cy="12" r="8.5"/><path d="M3.7 12h16.6M12 3.5a13 13 0 0 1 0 17M12 3.5a13 13 0 0 0 0 17"/><path d="m12 12 5-3"/>',
  'task-a': '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 8h2M12 8h5M7 12h2M12 12h5M7 16h2M12 16h5"/><path d="m6.5 8 .7.7 1.3-1.4M6.5 12l.7.7 1.3-1.4M6.5 16l.7.7 1.3-1.4"/>',
  'task-b': '<path d="M5 4h14v16H5z"/><path d="M8 8h8M8 12h8M8 16h5"/><path d="m8 12 1.2 1.2L11 11.4"/>',
  'download-a': '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4M12 7v5M9 10l3 3 3-3"/>',
  'download-b': '<path d="M5 4h14v10H5z"/><path d="M8 20h8M12 14v6M9 17l3 3 3-3"/><path d="M8 8h8"/>',
  'keyboard-a': '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M5.5 10h2M9.5 10h2M13.5 10h2M17.5 10h2M5.5 13h2M9.5 13h2M13.5 13h2M17.5 13h2M8 16h8"/>',
  'keyboard-b': '<rect x="3" y="6" width="18" height="12" rx="2"/><path d="M6 10h2M10 10h2M14 10h2M18 10h.01M6 13h2M10 13h2M14 13h2M18 13h.01"/><path d="M8 16h8"/><circle cx="18" cy="16" r="1"/>',
  'generic-a': '<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M8 7h8M8 11h8M8 15h4"/><path d="M17 15v5M14.5 17.5h5"/>',
  'generic-b': '<path d="M6 3h9l4 4v14H6z"/><path d="M15 3v5h4M9 12h6M9 16h4"/><circle cx="18" cy="18" r="2.5"/><path d="M18 16.5v3M16.5 18h3"/>',
};
const markup = computed(() => PATHS[`${props.kind}-${props.variant}`] || PATHS['generic-a']);
const fileIcon = ref<string | null>(null);
watch(() => props.appPath, async (path, _previous, onCleanup) => {
  let stale = false;
  onCleanup(() => { stale = true; });
  fileIcon.value = null;
  if (!path || isDefaultQuickAppPath(path)) return;
  const icon = await getQuickAppFileIcon(path);
  if (!stale) fileIcon.value = icon;
}, { immediate: true });
</script>

<template>
  <img v-if="fileIcon" class="quick-app-icon quick-app-file-icon" :src="fileIcon" alt="" aria-hidden="true" draggable="false" @error="fileIcon = null" />
  <svg v-else class="quick-app-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" v-html="markup" />
</template>

<style scoped>
.quick-app-icon { width: 22px; height: 22px; display: block; }
.quick-app-file-icon { object-fit: contain; filter: grayscale(1); }
</style>
