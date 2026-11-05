// One frontend consumer of the native AC/DC owner. No WMI, shell, sensors or polling.
import { readonly, ref } from 'vue';
import { tdpRuntimeStore } from './tdpRuntimeState';
import { invoke, on } from './ipc';
import { isUiVisible, onUiVisibilityChange } from './uiLifecycle';

export type PowerSourceMode = 'ac' | 'dc';
export interface PowerSourceSnapshot {
  known: boolean; acLine?: number; ac?: boolean;
  batteryKnown?: boolean; hasBattery?: boolean; revision?: number; generation?: number;
}
let sourceVersion = 0, generation = 0, nativeRevision = -1, committedGeneration = 0;
let nativeMode: PowerSourceMode | null | undefined, nativeBattery: boolean | null = null;
let suspended = false, readEpoch = 0, attemptedVersion = -1;
let readInFlight: { promise: Promise<boolean>; reconcile: boolean; epoch: number } | null = null;
const sourceMode = ref<PowerSourceMode | null>(null);
const batteryPresent = ref<boolean | null>(null);
export const powerSourceMode = readonly(sourceMode);
export const powerSourceBatteryPresent = readonly(batteryPresent);
type SourceNotification = 'immediate' | 'settled';
type SourceHandler = (mode: PowerSourceMode) => void | Promise<void>;
const notifications = { immediate: new Set<SourceHandler>(), settled: new Set<SourceHandler>() };
let lastKnownMode: PowerSourceMode | null = null, sourceTransition = 0;
const notifiedTransition = { immediate: -1, settled: -1 };
/** Application write owners receive only the receipt accepted by this source. */
export function onPowerSourceNotification(kind: SourceNotification, handler: SourceHandler): () => void {
  notifications[kind].add(handler); return () => { notifications[kind].delete(handler); };
}
function callConsumer(handler: SourceHandler, mode: PowerSourceMode): void {
  try { void Promise.resolve(handler(mode)).catch(() => { /* isolate asynchronous consumers */ }); }
  catch { /* one consumer must not block the other owners */ }
}
function notify(kind: SourceNotification, mode: PowerSourceMode): void {
  const transition = sourceTransition;
  if (notifiedTransition[kind] === transition) return;
  notifiedTransition[kind] = transition;
  for (const handler of [...notifications[kind]]) {
    if (suspended || sourceTransition !== transition || sourceMode.value !== mode) break;
    if (notifications[kind].has(handler)) callConsumer(handler, mode);
  }
}
function setMode(mode: PowerSourceMode | null): void {
  sourceMode.value = mode;
  if (mode) {
    if (lastKnownMode !== mode) { lastKnownMode = mode; ++sourceTransition; }
    if (tdpRuntimeStore.get().mode !== mode) tdpRuntimeStore.powerMode(mode);
  } else {
    const current = tdpRuntimeStore.get();
    if (current.mode !== null || current.status !== 'unknown') tdpRuntimeStore.invalidatePower();
  }
}
export function rememberPowerSourceMode(mode: PowerSourceMode): void {
  if (suspended || (mode !== 'ac' && mode !== 'dc') || (sourceMode.value !== null && sourceMode.value !== mode)) return;
  ++sourceVersion; setMode(mode);
}
function validGeneration(value: unknown): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value > 0;
}
function accept(state: PowerSourceSnapshot): boolean {
  if (suspended || !state || typeof state !== 'object' || Array.isArray(state)) return false;
  // Validate the entire receipt before advancing either fence or battery proof.
  if ('known' in state && typeof state.known !== 'boolean') return false;
  if ('ac' in state && typeof state.ac !== 'boolean') return false;
  if ('acLine' in state && state.acLine !== 0 && state.acLine !== 1 && state.acLine !== 255) return false;
  const lineMode = state.acLine === 1 ? 'ac' : state.acLine === 0 ? 'dc' : null;
  const boolMode = typeof state.ac === 'boolean' ? state.ac ? 'ac' : 'dc' : null;
  if ((lineMode && boolMode && lineMode !== boolMode) || (state.acLine === 255 && boolMode)) return false;
  const mode = lineMode ?? boolMode;
  if (state.known === false ? mode !== null : mode === null && state.acLine !== 255) return false;
  if (state.known === true && mode === null) return false;
  if ('batteryKnown' in state && typeof state.batteryKnown !== 'boolean') return false;
  if ('hasBattery' in state && typeof state.hasBattery !== 'boolean') return false;
  if (state.batteryKnown === true && typeof state.hasBattery !== 'boolean') return false;
  if ('generation' in state ? !validGeneration(state.generation) : generation > 0) return false;
  if ('revision' in state ? !Number.isSafeInteger(state.revision) || state.revision! < 0 : nativeRevision >= 0) return false;
  const nextGeneration = state.generation ?? generation;
  const nextRevision = state.revision ?? -1;
  if (nextGeneration < generation || (nextGeneration === generation && nextRevision < nativeRevision)) return false;
  const sameRevision = nextGeneration === generation && nextRevision >= 0 && nextRevision === nativeRevision;
  const battery = state.batteryKnown === true ? state.hasBattery! : null;
  if (sameRevision && ((nativeMode !== undefined && nativeMode !== mode) ||
    (nativeBattery !== null && battery !== null && nativeBattery !== battery))) return false;
  if (nextGeneration > generation) batteryPresent.value = null;
  generation = nextGeneration; nativeRevision = nextRevision; nativeMode = mode;
  nativeBattery = sameRevision && battery === null ? nativeBattery : battery;
  if ('batteryKnown' in state) batteryPresent.value = battery;
  ++sourceVersion; setMode(mode); return mode !== null;
}
/** Only a foreground boundary asks the native owner to reconcile. */
async function refreshSnapshot(reconcile: boolean, boundaryEpoch = readEpoch): Promise<boolean> {
  if (suspended || boundaryEpoch !== readEpoch) return false;
  if (reconcile && batteryPresent.value === false && sourceMode.value === 'ac') return true;
  if (readInFlight) {
    const read = readInFlight, reliable = await read.promise;
    if (boundaryEpoch !== readEpoch) return false;
    // Joining a passive or previous-boundary read cannot satisfy a fresh owner reconciliation.
    if (reconcile && !suspended && (!read.reconcile || read.epoch !== readEpoch)) return refreshSnapshot(true, boundaryEpoch);
    return reliable && read.epoch === readEpoch && sourceMode.value !== null;
  }
  if (!reconcile && (sourceMode.value !== null || attemptedVersion === sourceVersion)) return sourceMode.value !== null;
  const version = sourceVersion, epoch = boundaryEpoch;
  attemptedVersion = version;
  const current = () => epoch === readEpoch && !suspended;
  const request = invoke<PowerSourceSnapshot>('power.sourceSnapshot', reconcile && batteryPresent.value !== false ? { refresh: true } : {}, { timeoutMs: 2000 })
    .then(state => {
      if (!current()) return false;
      if (version !== sourceVersion) return sourceMode.value !== null;
      const reliable = accept(state); attemptedVersion = sourceVersion; return reliable;
    })
    .catch(() => {
      if (!current()) return false;
      if (version !== sourceVersion) return sourceMode.value !== null;
      ++sourceVersion; setMode(null); attemptedVersion = sourceVersion; return false;
    })
    .finally(() => { if (readInFlight?.promise === request) readInFlight = null; });
  readInFlight = { promise: request, reconcile, epoch }; return request;
}
/** All consumers share one passive read; failures stay unknown until the next real boundary. */
export async function refreshPowerSourceSnapshot(reconcile = false): Promise<void> { await refreshSnapshot(reconcile); }
export async function readPowerSourceMode(): Promise<PowerSourceMode | null> {
  if (sourceMode.value === null) await refreshPowerSourceSnapshot();
  return sourceMode.value;
}

/** App owns the event lifetime. Every page/action observes the same receipt. */
export function startForegroundPowerSourceRefresh(onForegroundSnapshot: SourceHandler): () => void {
  let active = true, visibilityEpoch = 0, queued = false, inFlightEpoch: number | null = null, followup = false;
  const fenceRead = (): void => { ++visibilityEpoch; ++readEpoch; };
  const request = (): void => {
    if (!active || suspended || !isUiVisible()) return;
    if (inFlightEpoch !== null) { if (inFlightEpoch !== visibilityEpoch) followup = true; return; }
    if (queued) return;
    queued = true;
    queueMicrotask(() => {
      queued = false;
      if (!active || suspended || !isUiVisible()) return;
      const epoch = visibilityEpoch; inFlightEpoch = epoch;
      void refreshSnapshot(true).then(reliable => {
        if (reliable && active && !suspended && isUiVisible() && epoch === visibilityEpoch && sourceMode.value) callConsumer(onForegroundSnapshot, sourceMode.value);
      }).finally(() => { inFlightEpoch = null; if (followup) { followup = false; request(); } });
    });
  };
  const observe = (state: PowerSourceSnapshot, repair = false, kind?: SourceNotification) => {
    if (!active) return;
    const before = sourceMode.value;
    if (!accept(state) || !sourceMode.value) return;
    const mode = sourceMode.value, transition = sourceTransition;
    if (kind) notify(kind, mode);
    if (repair && before !== mode && sourceTransition === transition && sourceMode.value === mode) callConsumer(onForegroundSnapshot, mode);
  };
  const lifecycleGeneration = (state?: { generation?: number }): number | null | undefined => {
    if (state == null) return generation === 0 ? undefined : null;
    if (!state || typeof state !== 'object' || Array.isArray(state)) return null;
    if (!('generation' in state)) return generation === 0 ? undefined : null;
    return validGeneration(state.generation) ? state.generation : null;
  };
  const suspend = (state?: { generation?: number }): void => {
    const next = lifecycleGeneration(state);
    if (next === null || (next !== undefined && (next < generation || next <= committedGeneration))) return;
    if (suspended && (next === undefined || next === generation)) return;
    if (next !== undefined && next > generation) { generation = next; nativeRevision = -1; nativeMode = undefined; nativeBattery = null; }
    suspended = true; ++sourceVersion; attemptedVersion = -1; batteryPresent.value = null; setMode(null); fenceRead();
  };
  const resume = (state?: { generation?: number }): void => {
    const next = lifecycleGeneration(state);
    if (next === null || (next !== undefined && (next < generation || (!suspended && next <= committedGeneration)))) return;
    if (next !== undefined) {
      committedGeneration = Math.max(committedGeneration, next);
      if (next > generation) { generation = next; nativeRevision = -1; nativeMode = undefined; nativeBattery = null; batteryPresent.value = null; setMode(null); }
    }
    suspended = false; ++sourceVersion; attemptedVersion = -1; fenceRead(); request();
  };
  const stops = [
    on<PowerSourceSnapshot>('power.sourceChanged', state => observe(state, false, 'immediate')),
    on<PowerSourceSnapshot>('power.acChanged', state => observe(state, false, 'settled')),
    on<PowerSourceSnapshot>('power.sourceHeartbeat', state => observe(state, true)),
    on<{ generation?: number }>('power.suspending', suspend),
    on<{ generation?: number }>('power.resuming', suspend),
    on<{ generation?: number }>('power.resumed', resume),
    onUiVisibilityChange(({ visible, powerReady, powerGeneration }) => {
      if (!visible) { fenceRead(); return; }
      // The existing lifecycle reader can recover a missed committed resume event.
      if (suspended && powerReady === true && validGeneration(powerGeneration) && powerGeneration >= generation) resume({ generation: powerGeneration });
      else request();
    }),
  ];
  const showEvents = ['ipc:window.shown', 'ipc:window.maximized', 'ipc:window.summoned'];
  for (const event of showEvents) window.addEventListener(event, request);
  window.addEventListener('focus', request);
  return () => { active = false; fenceRead(); followup = false; stops.forEach(stop => stop());
    for (const event of showEvents) window.removeEventListener(event, request); window.removeEventListener('focus', request); };
}
