// Transport lifetime only; never renews sensor validity. Keep the earliest
// scheduled wake and read the latest already-admitted expiry when it fires.
export function createHudTransportLease(deps: {
  now(): number; expire(): void;
  setTimer(fn: () => void, ms: number): unknown; clearTimer(id: unknown): void;
}) {
  let deadline = 0, timer: unknown = null, timerAt = 0, token = 0, disposed = false;
  function cancel() { ++token; if (timer !== null) deps.clearTimer(timer); timer = null; timerAt = 0; }
  function arm() {
    if (disposed || deadline <= 0) return;
    const now = deps.now();
    // Shortened leases must preempt. Renewals keep the earlier wake rather
    // than cancel/recreate a timeout at every 200ms transport packet.
    if (timer !== null && timerAt > now && timerAt <= deadline) return;
    cancel(); timerAt = deadline; const own = ++token;
    timer = deps.setTimer(() => {
      if (disposed || own !== token) return;
      timer = null; timerAt = 0;
      if (deps.now() < deadline) { arm(); return; }
      deadline = 0; deps.expire();
    }, Math.max(1, deadline - now));
  }
  return {
    accept(expiresAt: number) {
      const now = deps.now();
      if (disposed || !Number.isFinite(expiresAt) || expiresAt <= now || expiresAt > now + 7000) return false;
      deadline = expiresAt; arm(); return true;
    },
    dispose() { if (disposed) return; disposed = true; deadline = 0; cancel(); },
  };
}