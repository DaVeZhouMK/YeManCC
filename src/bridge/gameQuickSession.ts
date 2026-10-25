import {
  getCurrentGame,
  isCurrentGameTarget,
  refreshGameStatusStrict,
  validateGameTarget,
  type DetectedGame,
} from './gamedetect';

export interface LockedGameTarget extends DetectedGame {
  lockGeneration: number;
  lockReason: 'manual' | 'action';
}

type GameIdentity = Pick<DetectedGame, 'pid' | 'processCreated'> &
  Partial<Pick<DetectedGame, 'path' | 'identityGeneration'>>;

let lockedTarget: LockedGameTarget | null = null;
let lockGeneration = 0;
let validationInFlight: { generation: number; promise: Promise<LockedGameTarget | null> } | null = null;

function cloneTarget(target: LockedGameTarget | null): LockedGameTarget | null {
  return target ? { ...target } : null;
}

export function getLockedGameTarget(): LockedGameTarget | null {
  return cloneTarget(lockedTarget);
}

export function isGameTargetSame(a: GameIdentity | null, b: GameIdentity | null): boolean {
  if (!a || !b || a.pid !== b.pid || String(a.processCreated) !== String(b.processCreated)) return false;
  const normalizePath = (path: string) => path.trim().split('/').join('\\').toLowerCase();
  if (a.path !== undefined && b.path !== undefined && normalizePath(a.path) !== normalizePath(b.path)) return false;
  return a.identityGeneration === undefined || b.identityGeneration === undefined ||
    a.identityGeneration === b.identityGeneration;
}

export function lockGameTarget(
  target: DetectedGame,
  reason: 'manual' | 'action' = 'manual',
): LockedGameTarget {
  // A quick session captures the broadcast target; it cannot elect a PID.
  const authoritative = getCurrentGame();
  if (!authoritative || !isCurrentGameTarget(target)) throw new Error('游戏目标已变化，请重新获取游戏');
  const next: LockedGameTarget = {
    ...authoritative,
    lockGeneration: ++lockGeneration,
    lockReason: reason,
  };
  lockedTarget = next;
  return cloneTarget(next)!;
}

export function unlockGameTarget(expectedGeneration?: number): boolean {
  if (expectedGeneration !== undefined && lockedTarget?.lockGeneration !== expectedGeneration) return false;
  lockedTarget = null;
  lockGeneration += 1;
  return true;
}

export async function validateLockedGameTarget(): Promise<LockedGameTarget | null> {
  const current = lockedTarget;
  if (!current) return null;
  if (validationInFlight?.generation === current.lockGeneration) return validationInFlight.promise;
  const promise = (async () => {
    try {
      // Never pass the old PID back to the recognition arbiter.
      const detected = await validateGameTarget(current);
      if (lockGeneration !== current.lockGeneration || lockedTarget?.lockGeneration !== current.lockGeneration) return null;
      if (!detected || !isGameTargetSame(current, detected) || !isCurrentGameTarget(detected)) {
        unlockGameTarget(current.lockGeneration);
        return null;
      }
      lockedTarget = { ...current, ...detected, lockGeneration: current.lockGeneration };
      return cloneTarget(lockedTarget);
    } catch {
      // A transport failure gives no evidence of exit. It also grants no
      // authority to continue an action; callers must retry validation.
      return null;
    }
  })();
  validationInFlight = { generation: current.lockGeneration, promise };
  try {
    return await promise;
  } finally {
    // An old request must not remove the newer lock's in-flight validation.
    if (validationInFlight?.promise === promise) validationInFlight = null;
  }
}

export async function captureAndLockGameTarget(
  target?: DetectedGame | null,
  reason: 'manual' | 'action' = 'action',
): Promise<LockedGameTarget | null> {
  const existing = lockedTarget;
  if (existing) {
    if (!target || isGameTargetSame(existing, target)) return validateLockedGameTarget();
    // Only a confirmed different current target can replace this capture.
    // Dirty/unknown transport state alone must not discard the old lock.
    if (!isCurrentGameTarget(target)) return null;
    unlockGameTarget(existing.lockGeneration);
  }
  const generation = lockGeneration;
  const candidate = target || getCurrentGame() || await refreshGameStatusStrict();
  if (lockGeneration !== generation || !candidate || !isCurrentGameTarget(candidate)) return null;
  const checked = await validateGameTarget(candidate);
  if (lockGeneration !== generation || !checked || !isGameTargetSame(candidate, checked) || !isCurrentGameTarget(checked)) return null;
  return lockGameTarget(checked, reason);
}
