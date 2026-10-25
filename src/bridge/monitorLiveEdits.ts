// Event-driven UI edits only. No polling, data collection or idle timer.
export interface MonitorLiveEditIntent { preferences?: boolean; output?: boolean; steam?: Record<string, number>; rtss?: boolean; immediate?: boolean; }
export interface MonitorLiveEditDeps<P> {
  snapshot(): P;
  save(preferences: P): Promise<void>;
  apply(preferences: P): Promise<void>;
  steam(patch: Record<string, number>, preferences: P): Promise<void>;
  rtss(preferences: P): Promise<void>;
  busy(value: boolean): void;
  failure(error: unknown): void;
  timer(callback: () => void, delay: number): unknown;
  clearTimer(handle: unknown): void;
}
export function createMonitorLiveEdits<P>(deps: MonitorLiveEditDeps<P>) {
  type Pending = { snapshot: P; preferences: boolean; output: boolean; steam: Record<string, number>; rtss: boolean };
  let pending: Pending | null = null, timer: unknown = null, running: Promise<void> | null = null, disposed = false;
  function clear() { if (timer !== null) { deps.clearTimer(timer); timer = null; } }
  async function drain() {
    while (pending) {
      clear(); const edit = pending; pending = null;
      deps.busy(edit.output || edit.rtss || Object.keys(edit.steam).length > 0);
      try {
        if (edit.preferences) await deps.save(edit.snapshot);
        if (edit.output) await deps.apply(edit.snapshot);
        if (Object.keys(edit.steam).length) await deps.steam(edit.steam, edit.snapshot);
        if (edit.rtss) await deps.rtss(edit.snapshot);
      } catch (error) { deps.failure(error); }
      finally { deps.busy(false); }
    }
  }
  function flush(): Promise<void> {
    clear();
    if (running) return running;
    running = Promise.resolve().then(drain).finally(() => { running = null; if (pending) void flush(); });
    return running;
  }
  function request(intent: MonitorLiveEditIntent = {}) {
    if (disposed) return;
    const snapshot = deps.snapshot();
    pending = { snapshot, preferences: !!intent.preferences || !!pending?.preferences,
      output: !!intent.output || !!pending?.output, steam: { ...pending?.steam, ...intent.steam }, rtss: !!intent.rtss || !!pending?.rtss };
    clear();
    if (intent.immediate) { void flush(); return; }
    timer = deps.timer(() => { timer = null; void flush(); }, 150);
  }
  return { request, flush,
    dispose() { disposed = true; return flush(); },
    inspect: () => ({ pending: pending !== null, timer: timer !== null, running: running !== null, disposed }),
  };
}