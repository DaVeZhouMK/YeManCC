import type { DetectedGame } from './gamedetect';

export const GAME_POLICY_RELEASE_DELAY_MS = 10_000;
export function samePolicyGame(a: DetectedGame | null, b: DetectedGame | null): boolean {
  return a === b || !!a && !!b && a.pid === b.pid && a.processCreated === b.processCreated;
}
export function gamePolicyKey(game: DetectedGame | null): string {
  const raw = game?.path?.split(/[\\/]/).pop() || game?.name || '';
  const key = raw.toLowerCase().trim();
  return key ? (key.endsWith('.exe') ? key : `${key}.exe`) : '';
}

/** A single grace period for ALL policy owners, not a debounce per candidate. */
export class GamePolicyHysteresis {
  current: DetectedGame | null = null;
  initialized = false;
  missingSince: number | null = null;
  observe(game: DetectedGame | null, now: number): boolean {
    if (!this.initialized) {
      this.initialized = true;
      this.current = game;
      return true;
    }
    if (samePolicyGame(this.current, game)) {
      this.current = game; // title/source changes are NOT a policy switch.
      this.missingSince = null;
      return false;
    }
    if (!this.current) {
      this.current = game; // first entry is immediate.
      this.missingSince = null;
      return true;
    }
    if (this.missingSince === null) this.missingSince = now;
    if (now - this.missingSince < GAME_POLICY_RELEASE_DELAY_MS) return false;
    this.current = game; // choose the latest confirmed candidate, including null.
    this.missingSince = null;
    return true;
  }
}
