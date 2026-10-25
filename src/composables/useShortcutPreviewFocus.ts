import { computed, ref } from 'vue';

/** Display selection only. The latest mouse or controller focus owns the preview. */
export function useShortcutPreviewFocus() {
  const hoveredId = ref('');
  const focusedId = ref('');
  const source = ref<'mouse' | 'focus'>('mouse');
  const activeId = computed(() => source.value === 'focus'
    ? focusedId.value || hoveredId.value
    : hoveredId.value || focusedId.value);
  return {
    activeId,
    enter(id: string) { hoveredId.value = id; source.value = 'mouse'; },
    leave(id: string) { if (hoveredId.value === id) hoveredId.value = ''; },
    focus(id: string) { focusedId.value = id; source.value = 'focus'; },
    blur(id: string) { if (focusedId.value === id) focusedId.value = ''; },
    reset() { hoveredId.value = ''; focusedId.value = ''; },
  };
}
