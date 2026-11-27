// One five-second wake check, with one continuation for a confirmed failure
// before launch in the public power gate or its prerequisite reads.
export interface NetworkAccelerationWakeClock {
  setTimeout: (callback: () => void, delayMs: number) => ReturnType<typeof setTimeout>;
  clearTimeout: (timer: ReturnType<typeof setTimeout>) => void;
}
export function createNetworkAccelerationWakeRecovery(
  recheck: (generation: number, suspendSerial: number) => Promise<unknown>,
  clock: NetworkAccelerationWakeClock = {
    setTimeout: (callback, delayMs) => setTimeout(callback, delayMs),
    clearTimeout: timer => clearTimeout(timer),
  },
) {
  let timer: ReturnType<typeof setTimeout> | null = null;
  let generation = 0, suspendSerial = 0, attempts = 0, epoch = 0, disposed = false;
  let ready = false, inFlight = false, finished = false;
  const cancel = () => {
    ready = false;
    ++epoch;
    if (timer !== null) { clock.clearTimeout(timer); timer = null; }
  };
  const arm = () => {
    if (disposed || !ready || inFlight || finished || attempts >= 2 || timer !== null) return;
    const ticket = epoch, next = generation, serial = suspendSerial;
    const ownsScope = () => !disposed && generation === next && suspendSerial === serial;
    timer = clock.setTimeout(() => {
      if (!ownsScope() || !ready || ticket !== epoch) return;
      timer = null;
      inFlight = true;
      ++attempts;
      void recheck(next, serial).then(value => {
        if (!ownsScope()) return;
        inFlight = false;
        const reply = value as { generation?: number; suspendSerial?: number; ok?: boolean; reason?: string; submitted?: boolean } | null;
        finished = !(reply?.generation === next && reply.suspendSerial === serial && reply.ok === false && reply.submitted === false &&
          (reply.reason === 'power-deferred-before-launch' || reply.reason === 'startup-state-unavailable' ||
            reply.reason === 'process-state-unavailable'));
        arm();
      }, () => {
        if (!ownsScope()) return;
        inFlight = false;
        finished = true;
      });
    }, 5000);
  };
  return {
    schedule(next: number, serial: number | undefined): void {
      if (disposed || !Number.isSafeInteger(next) || next <= 0 || next < generation ||
          serial === undefined || !Number.isSafeInteger(serial) || serial < suspendSerial) return;
      if (next !== generation || serial !== suspendSerial) {
        cancel(); generation = next; suspendSerial = serial; attempts = 0; inFlight = false; finished = false;
      }
      ready = true;
      arm();
    },
    cancel,
    dispose(): void { disposed = true; cancel(); },
  };
}
