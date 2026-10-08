/**
 * Renderer-side mirror of the native owner transaction. It is observability
 * only: it cannot hide a device or revoke another process' XInput access.
 */
export type RuntimeInputOwner = 'none' | 'game-consumer' | 'ymcc-frontend';
export type RuntimeStopReason = 'close' | 'blur' | 'suspend' | 'virtual-target-fault' | 'crash';
export interface RuntimeOwnerSnapshot {
  schemaVersion: 1;
  owner: RuntimeInputOwner;
  epoch: number;
  gameInputCount: number;
  ymccActionCount: number;
  heldCleared: boolean;
  chordCleared: boolean;
  shiftCleared: boolean;
  sourceAdmission: boolean;
  sourcePresent: boolean;
  rearmRequired: boolean;
}

export class InputOwnerRuntime {
  private state: RuntimeOwnerSnapshot = {
    schemaVersion: 1, owner: 'none', epoch: 0, gameInputCount: 0, ymccActionCount: 0,
    heldCleared: true, chordCleared: true, shiftCleared: true, sourceAdmission: false,
    // The renderer mirror starts in an optimistic local-ready state; native
    // remains the authority for whether a physical source really exists.
    sourcePresent: true, rearmRequired: false,
  };

  start(): RuntimeOwnerSnapshot { return this.commit('game-consumer'); }
  summon(): RuntimeOwnerSnapshot { return this.commit('ymcc-frontend'); }
  dismiss(): RuntimeOwnerSnapshot { return this.commit('game-consumer'); }
  semanticAction(): RuntimeOwnerSnapshot {
    if (this.state.owner !== 'ymcc-frontend' || !this.state.sourceAdmission) return this.snapshot();
    this.state.ymccActionCount += 1;
    this.state.heldCleared = false;
    this.state.chordCleared = false;
    this.state.shiftCleared = false;
    return this.snapshot();
  }
  stop(_reason: RuntimeStopReason): RuntimeOwnerSnapshot {
    this.state = { ...this.state, owner: 'none', epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true, sourceAdmission: false };
    return this.snapshot();
  }
  /** HC physical-source removal: no release/new epoch; only suppress source publication. */
  sourceRemoved(): RuntimeOwnerSnapshot {
    this.state = { ...this.state, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true, sourceAdmission: false, sourcePresent: false, rearmRequired: true };
    return this.snapshot();
  }
  /** Fresh insertion is a candidate only; Coordinator must explicitly rearm. */
  sourceInserted(): RuntimeOwnerSnapshot {
    this.state = { ...this.state, sourceAdmission: false, sourcePresent: true, rearmRequired: true, heldCleared: true, chordCleared: true, shiftCleared: true };
    return this.snapshot();
  }
  /** Explicit Coordinator rearm creates a new renderer epoch. */
  rearmSource(): RuntimeOwnerSnapshot {
    if (!this.state.sourcePresent) throw new Error('rearm-without-physical-source');
    this.state = { ...this.state, epoch: this.state.epoch + 1, sourceAdmission: true, sourcePresent: true, rearmRequired: false, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true };
    return this.snapshot();
  }
  snapshot(): RuntimeOwnerSnapshot { return { ...this.state }; }
  private commit(owner: Exclude<RuntimeInputOwner, 'none'>): RuntimeOwnerSnapshot {
    // A physical-source insert is only a fresh candidate. Do not let a focus,
    // summon, or dismiss callback silently rotate the epoch and masquerade as
    // the explicit Coordinator rearm required by the HC-aligned contract.
    if (this.state.rearmRequired) return this.snapshot();
    this.state = { ...this.state, owner, epoch: this.state.epoch + 1, gameInputCount: 0, ymccActionCount: 0, heldCleared: true, chordCleared: true, shiftCleared: true, sourceAdmission: this.state.sourcePresent && !this.state.rearmRequired };
    return this.snapshot();
  }
}
