// A single foreground read repairs missed AC/DC notifications without resident polling.
// Keep this UI snapshot separate from hardware-write detection/ownership checks.
import { readonly, ref } from 'vue';
import { invoke, on } from './ipc';
import { detectPowerModeReliable } from './yeman';
import { isUiVisible, onUiVisibilityChange } from './uiLifecycle';

export type PowerSourceMode = 'ac' | 'dc';
let sourceVersion = 0;
const sourceMode = ref<PowerSourceMode | null>(null);
export const powerSourceMode = readonly(sourceMode);

export function rememberPowerSourceMode(mode: PowerSourceMode): void {
  ++sourceVersion;
  sourceMode.value = mode;
}

interface PowerSourceSnapshot {
  known: boolean;
  acLine: number;
}

/** One foreground/mirror-open read; native plug/unplug notifications always win. */
export async function refreshPowerSourceSnapshot(): Promise<void> {
  const version = sourceVersion;
  const result = await invoke<PowerSourceSnapshot>('power.sourceSnapshot', {}, { timeoutMs: 2000 }).catch(() => null);
  if (version !== sourceVersion || !result?.known) return;
  if (result.acLine === 0 || result.acLine === 1) rememberPowerSourceMode(result.acLine === 1 ? 'ac' : 'dc');
}

/** App owns one binding. Every display consumes powerSourceMode; pages do not query. */
export function startForegroundPowerSourceRefresh(
  onForegroundSnapshot: (mode: PowerSourceMode) => void,
): () => void {
  let active = true;
  let visibilityEpoch = 0;
  let notificationVersion = 0;
  let queued = false;
  let inFlightEpoch: number | null = null;
  let followup = false;

  const readOnce = async (isCurrent: () => boolean): Promise<PowerSourceMode | null> => {
    try {
      // Dedicated Win32 read: no CPU enumeration, registry, monitoring or writes.
      const snapshot = await invoke<PowerSourceSnapshot>('power.sourceSnapshot', {}, { timeoutMs: 2000 });
      if (!snapshot?.known) return null;
      return snapshot.acLine === 1 ? 'ac' : snapshot.acLine === 0 ? 'dc' : null;
    } catch {
      // Preserve compatibility with an older native shell; its existing probe is
      // single-flight with bounded fallback. Failure is never authoritative AC.
      return isCurrent() ? detectPowerModeReliable() : null;
    }
  };

  const request = (): void => {
    if (!active || !isUiVisible()) return;
    if (inFlightEpoch !== null) {
      // Repeated shown/restored/maximized events share the same read. Only a
      // hide/show or suspend/resume boundary demands a fresh read afterwards.
      if (inFlightEpoch !== visibilityEpoch) followup = true;
      return;
    }
    if (queued) return;
    queued = true;
    queueMicrotask(() => {
      queued = false;
      if (!active || !isUiVisible()) return;
      const epoch = visibilityEpoch;
      const version = notificationVersion;
      inFlightEpoch = epoch;
      const isCurrent = () => active && isUiVisible() && epoch === visibilityEpoch &&
        version === notificationVersion;
      void readOnce(isCurrent).then((mode) => {
        // A delayed pre-sleep answer or a newer native plug/unplug broadcast
        // cannot roll the UI back to its previous power source.
        if (!isCurrent() || !mode) return;
        rememberPowerSourceMode(mode);
        onForegroundSnapshot(mode);
      }).catch(() => {
        // One-shot means failures keep the last known value, with no retry loop.
      }).finally(() => {
        inFlightEpoch = null;
        if (followup) {
          followup = false;
          request();
        }
      });
    });
  };

  const onNativeSource = ({ ac }: { ac: boolean }): void => {
    if (!active || typeof ac !== 'boolean') return;
    ++notificationVersion;
    rememberPowerSourceMode(ac ? 'ac' : 'dc');
  };
  const stopSource = on<{ ac: boolean }>('power.sourceChanged', onNativeSource);
  const stopSettled = on<{ ac: boolean }>('power.acChanged', onNativeSource);
  const stopVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) {
      ++visibilityEpoch;
      return;
    }
    request();
  });
  // uiLifecycle suppresses visible->visible notifications, so maximize/show
  // also request directly. Do not bind restored here: native emits it for
  // every normal WM_SIZE, not just an actual restore. Visibility already
  // handles minimized/hidden -> restored, without querying on resize.
  const showEvents = ['ipc:window.shown', 'ipc:window.maximized', 'ipc:window.summoned'];
  for (const event of showEvents) window.addEventListener(event, request);
  window.addEventListener('focus', request);

  return () => {
    active = false;
    ++visibilityEpoch;
    followup = false;
    stopVisibility();
    stopSource();
    stopSettled();
    for (const event of showEvents) window.removeEventListener(event, request);
    window.removeEventListener('focus', request);
  };
}
