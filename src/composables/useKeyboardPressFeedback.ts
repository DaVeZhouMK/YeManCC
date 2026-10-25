import { onMounted, onBeforeUnmount, shallowRef, type Ref } from 'vue';
import { keyboardCodePattern } from '@/bridge/controllerShortcutRules';
import { createPressFeedback } from '@/bridge/controllerPressFeedback';

export function useKeyboardPressFeedback(root: Ref<HTMLElement | null>) {
  const liveCodes = shallowRef<readonly string[]>([]), held = new Set<string>();
  const feedback = createPressFeedback(keys => { liveCodes.value = keys; });
  function down(event: KeyboardEvent) {
    if (!root.value?.getClientRects().length || event.isComposing || !keyboardCodePattern.test(event.code)) return;
    held.add(event.code); feedback.accept([...held]);
  }
  function up(event: KeyboardEvent) { held.delete(event.code); feedback.accept([...held]); }
  function reset() { held.clear(); feedback.reset(); }
  onMounted(() => { window.addEventListener('keydown', down, true); window.addEventListener('keyup', up, true); window.addEventListener('blur', reset); });
  onBeforeUnmount(() => { window.removeEventListener('keydown', down, true); window.removeEventListener('keyup', up, true); window.removeEventListener('blur', reset); reset(); });
  return liveCodes;
}
