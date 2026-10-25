// One event-driven geometry transaction at a time. No polling or retry timer.
export interface HudLayout { width: number; height: number; position: number }
export function createHudLayoutLane(deps: {
  sequence(): number;
  send(layout: HudLayout & { seq: number }): Promise<boolean>;
  failure(error: unknown): void;
}) {
  let pending: HudLayout | null = null, running: Promise<void> | null = null, confirmed = '', revision = 0, disposed = false;
  const key = (shape: HudLayout) => JSON.stringify([shape.width, shape.height, shape.position]);
  async function drain() {
    while (pending && !disposed) {
      const shape = pending; pending = null;
      if (key(shape) === confirmed) continue;
      const seq = deps.sequence(), sentRevision = revision;
      try {
        if (!await deps.send({ ...shape, seq })) throw Error('HUD layout receipt rejected');
        if (disposed) return;
        // The native API ignores old-sequence layouts but returns a legacy true ACK.
        // A newer frame in flight therefore needs ONE latest-sequence resubmission.
        if (seq !== deps.sequence() || sentRevision !== revision) { confirmed = ''; pending ??= shape; }
        else confirmed = key(shape);
      } catch (error) { disposed = true; pending = null; deps.failure(error); }
    }
  }
  function flush(): Promise<void> {
    if (running) return running;
    running = Promise.resolve().then(drain).finally(() => { running = null; if (pending && !disposed) void flush(); });
    return running;
  }
  return {
    request(shape: HudLayout) { if (disposed) return; pending = { ...shape }; void flush(); },
    invalidate() { ++revision; confirmed = ''; },
    flush,
    dispose() { disposed = true; pending = null; },
  };
}
