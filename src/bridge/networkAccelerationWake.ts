// One five-second wake check, never an interval or an application startup action.
// Native rechecks the current generation, startup task and the same update-page
// process state before accepting a single launch request.
export interface NetworkAccelerationWakeClock {
  setTimeout: (callback: () => void, delayMs: number) => ReturnType<typeof setTimeout>;
  clearTimeout: (timer: ReturnType<typeof setTimeout>) => void;
}
export function createNetworkAccelerationWakeRecovery(
  recheck: (generation: number) => Promise<unknown>,
  clock: NetworkAccelerationWakeClock = {
    setTimeout: (callback, delayMs) => setTimeout(callback, delayMs),
    clearTimeout: timer => clearTimeout(timer),
  },
) {
  let timer: ReturnType<typeof setTimeout> | null = null;
  let generation = 0, epoch = 0, disposed = false;
  const cancel = () => {
    ++epoch;
    if (timer !== null) { clock.clearTimeout(timer); timer = null; }
  };
  return {
    schedule(next: number): void {
      if (disposed || !Number.isSafeInteger(next) || next <= 0 || next <= generation) return;
      cancel(); generation = next;
      const ticket = epoch;
      timer = clock.setTimeout(() => {
        if (disposed || ticket !== epoch || generation !== next) return;
        timer = null;
        // One attempt; read/launch failure is not permission to retry forever.
        void recheck(next).catch(() => {});
      }, 5000);
    },
    cancel,
    dispose(): void { disposed = true; cancel(); },
  };
}
