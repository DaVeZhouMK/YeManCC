<script setup lang="ts">
import { computed } from 'vue';
import ControllerShortcutEditorView from '@/views/ControllerShortcutEditorView.vue';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';

// The native shell appends this marker when it creates the editor window. It
// lets the editor close only its own top-level window, never the main YMCC UI.
const childId = computed<number | null>(() => {
  const match = window.location.hash.match(/[?&]__ymccChild=(\d+)/);
  if (!match) return null;
  const id = Number(match[1]);
  return Number.isSafeInteger(id) && id > 0 ? id : null;
});

async function closeEditor(): Promise<void> {
  if (isNativeRuntime && childId.value !== null) {
    await windowApi.closeChild(childId.value).catch(() => undefined);
    return;
  }
  window.close();
}
</script>

<template>
  <ControllerShortcutEditorView @close="void closeEditor()" />
</template>
