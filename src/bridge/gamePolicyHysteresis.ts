import type { DetectedGame } from './gamedetect';

// Kept for compatibility; the unique game target has no independent grace PID.
export const GAME_POLICY_RELEASE_DELAY_MS = 0;
export function samePolicyGame(a: DetectedGame | null, b: DetectedGame | null): boolean {
  return a === b || !!a && !!b && a.pid === b.pid && a.processCreated === b.processCreated &&
    (a.identityGeneration === undefined || b.identityGeneration === undefined || a.identityGeneration === b.identityGeneration) &&
    (!a.path || !b.path || a.path.toLowerCase() === b.path.toLowerCase());
}
export function gamePolicyKey(game: DetectedGame | null): string {
  const raw = game?.path?.split(/[\\/]/).pop() || game?.name || '';
  const key = raw.toLowerCase().trim();
  return key ? (key.endsWith('.exe') ? key : `${key}.exe`) : '';
}

/** Legacy model API retained for consumers/tests; observations now project immediately. */
export class GamePolicyHysteresis {
  current: DetectedGame | null = null;
  initialized = false;
  missingSince: number | null = null;
  observe(game: DetectedGame | null, _now: number): boolean {
    const changed = !this.initialized || !samePolicyGame(this.current, game);
    this.initialized = true;
    this.current = game ? { ...game } : null;
    this.missingSince = null;
    return changed;
  }
}
