import { onScopeDispose, readonly, shallowRef } from 'vue';
import { getLastGamepadState, on } from './ipc';
import { readVirtualTargetPolicy } from './virtualTargetPolicy';

/** Read the existing IPC snapshot; no discovery, native query, timer or settings write. */
export function currentVirtualTargetPolicy() {
  return readVirtualTargetPolicy(getLastGamepadState());
}
export function useVirtualTargetPolicy() {
  const policy = shallowRef(currentVirtualTargetPolicy());
  const stop = on('gamepad.state', state => {
    const next = readVirtualTargetPolicy(state);
    const previous = policy.value;
    if (next?.steamDeckSelfOutputBlocked === previous?.steamDeckSelfOutputBlocked &&
        next?.reason === previous?.reason) return;
    policy.value = next;
  });
  onScopeDispose(stop);
  return readonly(policy);
}
