import { watch } from 'vue';
import type { Router } from 'vue-router';
import { getUiSetting, setUiSettings, uiSettingsLoaded } from '@/bridge/uiSettings';
import { invoke, isNativeRuntime } from '@/bridge/ipc';
import { RADIAL_USER_AVAILABLE } from './availability';
import { radialTestingEnabled } from './testingState';
import { startRadialHost } from './host';

let pending: Promise<void> = Promise.resolve();
/** Serialize native admission with preference changes; stale enables never open UI. */
export function syncRadialTestingPreference(): Promise<void> {
  if (!getUiSetting('radialTestingEnabled')) radialTestingEnabled.value = false;
  const next = pending.catch(() => {}).then(async () => {
    const enabled = RADIAL_USER_AVAILABLE && getUiSetting('radialTestingEnabled');
    if (isNativeRuntime) {
      const receipt = await invoke<{ok: boolean; enabled: boolean; reason?: string}>(
        'radial.testing.configure', {enabled}, {timeoutMs: 3000});
      if (!receipt?.ok || receipt.enabled !== enabled) {
        radialTestingEnabled.value = false;
        throw new Error(receipt?.reason || '轮盘测试通道未确认，请重试。');
      }
    }
    radialTestingEnabled.value = enabled && getUiSetting('radialTestingEnabled');
  });
  pending = next;
  return next;
}
export async function setRadialTestingPreference(enabled: boolean): Promise<void> {
  await setUiSettings({radialTestingEnabled: enabled});
  await syncRadialTestingPreference();
}
export function startRadialTestingHost(router: Router): {stop(): void} {
  let host: ReturnType<typeof startRadialHost> | null = null;
  const stopWatch = watch(radialTestingEnabled, enabled => {
    if (enabled) host ??= startRadialHost(router);
    else {
      host?.stop(); host = null;
      if (router.currentRoute.value.path === '/radial') void router.replace('/settings');
    }
  }, {flush: 'sync', immediate: true});
  const changed = () => { void syncRadialTestingPreference().catch(error => console.warn('[radial-testing]', error)); };
  window.addEventListener('ui-settings:loaded', changed);
  window.addEventListener('ui-settings:changed', changed);
  if (uiSettingsLoaded()) changed();
  return {stop() {
    stopWatch(); host?.stop(); host = null;
    radialTestingEnabled.value = false;
    window.removeEventListener('ui-settings:loaded', changed);
    window.removeEventListener('ui-settings:changed', changed);
    if (isNativeRuntime) void invoke('radial.testing.configure', {enabled:false}, {timeoutMs:3000}).catch(() => {});
  }};
}
