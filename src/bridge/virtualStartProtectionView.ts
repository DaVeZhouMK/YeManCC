import type { VirtualStartProtectionState } from './virtualOutputStatus';

export const VIRTUAL_START_MAX_WAIT_MS = 30_000;
const TERMINAL_SETTLE_MS = 10_000;

export interface VirtualStartProtectionViewSnapshot {
  locked: boolean;
  requestId: number;
  deadline: number | null;
  expired: boolean;
  terminal: boolean;
}

// Bounds cached UI observations only. Expiry neither proves target success nor
// admits a backend request; native rechecks every explicit request itself.
export class VirtualStartProtectionView {
  private requestId = -1;
  private deadline: number | null = null;
  private released = true;
  private expired = false;
  private terminal = false;

  observe(state: VirtualStartProtectionState | undefined, now: number, wallNow = Date.now()): VirtualStartProtectionViewSnapshot {
    this.read(now);
    if (!state || !Number.isSafeInteger(state.requestId) || state.requestId < 0 ||
        state.requestId < this.requestId) return this.read(now);
    const fresh = state.requestId !== this.requestId;
    if (fresh) {
      this.requestId = state.requestId;
      this.deadline = null;
      this.released = false;
      this.expired = false;
      this.terminal = false;
    }
    if (!state.locked) {
      this.terminal = state.phase === 'success' || state.phase === 'failure';
      this.released = true;
      this.deadline = null;
      this.expired = false;
      return this.read(now);
    }
    // A delayed or duplicate observation cannot revive the same completed lease.
    if (this.released) return this.read(now);
    const elapsed = Number.isFinite(state.elapsedMs) ? Math.max(0, state.elapsedMs) : 0;
    let budget = Math.max(0, VIRTUAL_START_MAX_WAIT_MS - elapsed);
    if (Number.isFinite(state.remainingMs) && state.remainingMs > 0)
      budget = Math.min(budget, state.remainingMs);
    if (state.phase === 'success' || state.phase === 'failure' || state.phase === 'cancelled') {
      this.terminal = true;
      budget = Math.min(budget, TERMINAL_SETTLE_MS);
    }
    // An old payload must spend producer time already elapsed in transit.
    // Convert it once to a monotonic budget; wall rollback or later payloads
    // can never extend the retained deadline for this same request.
    const expiresAtUnixMs = state.expiresAtUnixMs;
    if (typeof expiresAtUnixMs === 'number' && Number.isSafeInteger(expiresAtUnixMs) && expiresAtUnixMs > 0 && Number.isFinite(wallNow))
      budget = Math.min(budget, Math.max(0, expiresAtUnixMs - wallNow));
    const deadline = now + budget;
    this.deadline = this.deadline === null ? deadline : Math.min(this.deadline, deadline);
    return this.read(now);
  }

  read(now: number): VirtualStartProtectionViewSnapshot {
    if (this.deadline !== null && now >= this.deadline) {
      this.deadline = null;
      this.released = true;
      this.expired = true;
    }
    return { locked: !this.released && this.deadline !== null,
      requestId: Math.max(0, this.requestId), deadline: this.deadline, expired: this.expired, terminal: this.terminal };
  }
}
