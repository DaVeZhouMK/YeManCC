import { detectGame, type DetectedGame } from './gamedetect';

export interface LockedGameTarget extends DetectedGame {
  lockGeneration: number;
  lockReason: 'manual' | 'action';
}

let lockedTarget: LockedGameTarget | null = null;
let lockGeneration = 0;
let validationInFlight: Promise<LockedGameTarget | null> | null = null;

function cloneTarget(target: LockedGameTarget | null): LockedGameTarget | null {
  return target ? { ...target } : null;
}

export function getLockedGameTarget(): LockedGameTarget | null {
  return cloneTarget(lockedTarget);
}

export function isGameTargetSame(
  a: Pick<DetectedGame, 'pid' | 'processCreated'> | null,
  b: Pick<DetectedGame, 'pid' | 'processCreated'> | null,
): boolean {
  return !!a && !!b && a.pid === b.pid && String(a.processCreated) === String(b.processCreated);
}

export function lockGameTarget(
  target: DetectedGame,
  reason: 'manual' | 'action' = 'manual',
): LockedGameTarget {
  const next: LockedGameTarget = {
    ...target,
    lockGeneration: ++lockGeneration,
    lockReason: reason,
  };
  lockedTarget = next;
  return cloneTarget(next)!;
}

export function unlockGameTarget(): void {
  lockedTarget = null;
  lockGeneration += 1;
}

export async function validateLockedGameTarget(): Promise<LockedGameTarget | null> {
  if (validationInFlight) return validationInFlight;
  validationInFlight = (async () => {
    const current = lockedTarget;
    if (!current) return null;
    try {
      const detected = await detectGame(true, current.pid);
      const samePath = !current.path || !detected?.path ||
        current.path.toLowerCase() === detected.path.toLowerCase();
      if (!detected || !isGameTargetSame(current, detected) || !samePath) {
        unlockGameTarget();
        return null;
      }
      lockedTarget = { ...current, ...detected, lockGeneration: current.lockGeneration };
      return cloneTarget(lockedTarget);
    } catch {
      // Transport failure is not proof that the locked game exited.
      return null;
    } finally {
      validationInFlight = null;
    }
  })();
  return validationInFlight;
}

export async function captureAndLockGameTarget(
  target?: DetectedGame | null,
  reason: 'manual' | 'action' = 'action',
): Promise<LockedGameTarget | null> {
  if (lockedTarget) return validateLockedGameTarget();
  const candidate = target || await detectGame(true);
  return candidate ? lockGameTarget(candidate, reason) : null;
}
