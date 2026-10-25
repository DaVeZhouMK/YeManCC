// Passive receipts from the existing YMCC hardware-write path. No settings,
// sensors, IPC, intervals or fresh hardware probes are allowed in this store.
// write-ack means YMCC's backend accepted a write; NOT a continuous register read.
export type TdpPowerMode = 'ac' | 'dc';
export type TdpRuntimeStatus = 'unknown' | 'pending' | 'applied' | 'failed';
export interface TdpRuntimeSnapshot {
  readonly mode: TdpPowerMode | null;
  readonly status: TdpRuntimeStatus;
  readonly watts: number | null;
  readonly proof: 'write-ack' | null;
  readonly revision: number;
  readonly confirmedAt: number | null;
}
export interface TdpWriteReceipt {
  readonly id: number;
  readonly powerEpoch: number;
  readonly mode: TdpPowerMode | null;
  readonly requestedMode: TdpPowerMode;
  readonly watts: number;
}
export function createTdpRuntimeStore(now: () => number = Date.now) {
  let epoch = 0, nextId = 0, owner = 0;
  let current: TdpRuntimeSnapshot = Object.freeze({ mode: null, status: 'unknown', watts: null, proof: null, revision: 0, confirmedAt: null });
  let nextObserverId = 0;
  const listeners = new Set<{ readonly id: number; readonly listener: (state: TdpRuntimeSnapshot) => void }>();
  function publish(status: TdpRuntimeStatus, watts: number | null = null) {
    current = Object.freeze({ mode: current.mode, status, watts, proof: status === 'applied' ? 'write-ack' : null,
      revision: current.revision + 1, confirmedAt: status === 'applied' ? now() : null });
    // Newly registered observers already receive current once in observe().
    // Fence this traversal without an array copy; rebinds cannot extend it.
    // A nested publication owns the remaining latest-state notifications.
    const published = current, boundary = nextObserverId;
    for (const subscription of listeners) {
      if (subscription.id > boundary || current !== published) break;
      try { subscription.listener(published); } catch { /* displays must never interrupt hardware control */ }
    }
  }
  function invalidate() { ++epoch; owner = 0; publish('unknown'); }
  return {
    get: () => current,
    observe(listener: (state: TdpRuntimeSnapshot) => void) {
      const subscription = { id: ++nextObserverId, listener };
      listeners.add(subscription); try { listener(current); } catch { /* isolate display */ }
      return () => { listeners.delete(subscription); };
    },
    powerMode(mode: TdpPowerMode) {
      if (current.mode === mode) return;
      // There is one physical limit. Never resurrect an AC/DC historical cache.
      current = Object.freeze({ ...current, mode }); invalidate();
    },
    invalidate,
    invalidatePower() {
      // Previous power-side knowledge cannot survive a missed sleep/plug edge.
      current = Object.freeze({ ...current, mode: null }); invalidate();
    },
    begin(mode: TdpPowerMode, watts: number): TdpWriteReceipt {
      // setTdp's mode selects saved policy, not a separate hardware register.
      // Legacy/global UI callers can pass 'ac' while the physical machine is DC.
      // Bind the actual write to the observed power side, never the policy key.
      const receipt = Object.freeze({ id: ++nextId, powerEpoch: epoch, mode: current.mode, requestedMode: mode, watts });
      owner = receipt.id; publish('pending'); return receipt;
    },
    isCurrent(receipt: TdpWriteReceipt): boolean { return receipt.id === owner && receipt.powerEpoch === epoch; },
    finishUnknown(receipt: TdpWriteReceipt) {
      if (receipt.id !== owner || receipt.powerEpoch !== epoch) return;
      owner = 0; publish('unknown');
    },
    finish(receipt: TdpWriteReceipt, applied: boolean, acceptedWatts = receipt.watts) {
      // A power/sleep boundary or a newer queued write owns the display now.
      if (receipt.id !== owner || receipt.powerEpoch !== epoch) return;
      owner = 0;
      const valid = applied && current.mode !== null && current.mode === receipt.mode && Number.isFinite(acceptedWatts) && acceptedWatts >= 2 && acceptedWatts <= 200;
      publish(valid ? 'applied' : applied ? 'unknown' : 'failed', valid ? Math.round(acceptedWatts) : null);
    },
  };
}
export const tdpRuntimeStore = createTdpRuntimeStore();
