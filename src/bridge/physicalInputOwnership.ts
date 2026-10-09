/**
 * PhysicalInputOwnership.v1. Pure owner arbitration for the YMCC semantic
 * pipeline. It intentionally does not hide devices or read virtual HID.
 */
export type PhysicalInputOwner = 'none' | 'game-consumer' | 'ymcc-frontend';
export type OwnershipEvent = 'summon' | 'dismiss' | 'modal-open' | 'modal-close' | 'suspend' | 'release' | 'close' | 'blur' | 'virtual-target-fault' | 'crash' | 'physical-source-remove';

export interface OwnershipSnapshot {
  schemaVersion: 1;
  owner: PhysicalInputOwner;
  epoch: number;
  frontendSummonVirtualControl: boolean;
  releaseRequired: boolean;
}

export type FrameDisposition = 'game-report' | 'ymcc-semantic-only' | 'drop';

export class PhysicalInputOwnership {
  private owner: PhysicalInputOwner = 'none';
  private epoch = 0;
  private releaseRequired = false;

  transition(event: OwnershipEvent): OwnershipSnapshot {
    switch (event) {
      case 'summon':
        this.epoch += 1;
        this.owner = 'ymcc-frontend';
        this.releaseRequired = false;
        break;
      case 'close': case 'blur': case 'virtual-target-fault': case 'crash':
        this.epoch += 1;
        this.owner = 'none';
        this.releaseRequired = true;
        break;
      case 'physical-source-remove':
        // HC ControllerManager's physical-source removal is identity-bound:
        // it suppresses publication for the removed source but does not infer
        // virtual-target release, a new epoch, or automatic rearm.
        // Source admission is tracked by the coordinator/lifecycle mock; the
        // owner mirror itself remains unchanged so source loss is not
        // mistaken for owner release.
        break;
      case 'dismiss':
        this.epoch += 1;
        this.owner = 'game-consumer';
        this.releaseRequired = false;
        break;
      case 'modal-open':
        // A modal has its own focus/semantic boundary. Revoke the frame owner
        // and require the normal release path rather than letting held input
        // leak into either the game or the modal.
        this.epoch += 1;
        this.owner = 'none';
        this.releaseRequired = true;
        break;
      case 'modal-close':
        break;
      case 'suspend':
        this.epoch += 1;
        this.owner = 'none';
        this.releaseRequired = true;
        break;
      case 'release':
        this.epoch += 1;
        this.owner = 'none';
        this.releaseRequired = false;
        break;
    }
    return this.snapshot();
  }

  /**
   * Stop all frame admission before the lifecycle's neutral/release barrier.
   * This is deliberately separate from assigning the next consumer: a frame
   * received during a handoff is dropped, never redirected.
   */
  revokeAdmission(): OwnershipSnapshot {
    this.owner = 'none';
    this.releaseRequired = true;
    return this.snapshot();
  }

  /** Commit the next owner after its lifecycle epoch has been neutralized. */
  atomicSwitch(owner: Exclude<PhysicalInputOwner, 'none'>, lifecycleEpoch?: number): OwnershipSnapshot {
    if (lifecycleEpoch !== undefined) {
      if (lifecycleEpoch < this.epoch) throw new Error('stale-owner-epoch');
      this.epoch = lifecycleEpoch;
    } else {
      this.epoch += 1;
    }
    this.owner = owner;
    this.releaseRequired = false;
    return this.snapshot();
  }

  /** A frame may be consumed by at most one semantic route. */
  canConsume(owner: Exclude<PhysicalInputOwner, 'none'>, frameEpoch: number): boolean {
    return !this.releaseRequired && frameEpoch === this.epoch && this.owner === owner;
  }

  /**
   * One physical frame has exactly one destination. In the foreground path,
   * the virtual backend must receive neutral/no report; its UI action is a
   * semantic dispatch, never a virtual-HID readback loop.
   */
  routeFrame(frameEpoch: number): FrameDisposition {
    if (this.releaseRequired || frameEpoch !== this.epoch) return 'drop';
    if (this.owner === 'ymcc-frontend') return 'ymcc-semantic-only';
    if (this.owner === 'game-consumer') return 'game-report';
    return 'drop';
  }

  snapshot(): OwnershipSnapshot {
    return {
      schemaVersion: 1,
      owner: this.owner,
      epoch: this.epoch,
      frontendSummonVirtualControl: this.owner === 'ymcc-frontend',
      releaseRequired: this.releaseRequired,
    };
  }
}
