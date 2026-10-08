import {
  InputLifecycleMock,
  type InputLifecycleSnapshot,
} from './inputLifecycleMock';
import {
  PhysicalInputOwnership,
  type FrameDisposition,
  type OwnershipSnapshot,
} from './physicalInputOwnership';
import {
  assembleCanonicalFrame,
  type CanonicalFrame,
  type ControllerFrame,
  type MotionFrame,
} from './virtualReportAssembler';

export interface InputCoordinatorTraceEvent {
  event: string;
  /**
   * Explicit lifecycle classification.  A bare "disconnect" is not enough:
   * HC treats a matched physical-source removal as no-write/target-retained,
   * while a virtual-target/backend fault is a release path.
   */
  eventClass: InputCoordinatorEventClass;
  epoch: number;
  lifecyclePhase: InputLifecycleSnapshot['phase'];
  owner: OwnershipSnapshot['owner'];
  disposition: FrameDisposition | 'none';
  inputSequence: number | null;
  neutral: boolean;
  gameInputCount: number;
  ymccActionCount: number;
  heldCleared: boolean;
  chordCleared: boolean;
  shiftCleared: boolean;
  admissionStopped: boolean;
  sourceAdmission: InputLifecycleSnapshot['sourceAdmission'];
  targetSession: InputLifecycleSnapshot['targetSession'];
}

export interface InputCoordinatorFrameResult {
  disposition: FrameDisposition;
  frame: CanonicalFrame | null;
  trace: InputCoordinatorTraceEvent;
}

export type InputCoordinatorStopReason = 'close' | 'blur' | 'suspend' | 'virtual-target-fault' | 'crash';
export type InputCoordinatorEventClass =
  | 'internal'
  | 'project-close'
  | 'ui-blur'
  | 'system-suspend'
  | 'physical-source-remove'
  | 'physical-source-insert'
  | 'virtual-target-pnp-remove'
  | 'host-fault';

/**
 * F1/F2 integration mock. It joins the lifecycle and physical-owner policies
 * around the pure assembler, while deliberately having no process, HID, PnP,
 * power, or filesystem side effects.
 */
export class InputCoordinatorMock {
  readonly lifecycle = new InputLifecycleMock();
  readonly ownership = new PhysicalInputOwnership();
  private readonly trace: InputCoordinatorTraceEvent[] = [];
  private gameInputCount = 0;
  private ymccActionCount = 0;
  private heldCleared = true;
  private chordCleared = true;
  private shiftCleared = true;

  start(configRevision = 0, physicalSourceIdentity?: string, virtualTargetIdentity?: string): InputLifecycleSnapshot {
    let state = this.lifecycle.transition('enable', {
      configRevision,
      ...(physicalSourceIdentity ? { physicalSourceIdentity } : {}),
      ...(virtualTargetIdentity ? { virtualTargetIdentity } : {}),
    });
    for (const event of ['verified', 'target-ready', 'neutral', 'admit'] as const) {
      state = this.lifecycle.transition(event, { configRevision });
    }
    this.ownership.atomicSwitch('game-consumer', state.epoch);
    this.record('start', 'none', null, false);
    return state;
  }

  summon(): OwnershipSnapshot {
    return this.handoff('summon', 'ymcc-frontend');
  }

  dismiss(): OwnershipSnapshot {
    return this.handoff('dismiss', 'game-consumer');
  }

  /**
   * Revoke admission and clear held/chord/shift state before releasing the
   * owner.  Physical source removal is intentionally not accepted here:
   * callers must use `physicalSourceRemove(identity)` so it cannot be
   * confused with virtual-target/backend fault release.
   */
  stop(reason: InputCoordinatorStopReason, eventClass?: InputCoordinatorEventClass): InputLifecycleSnapshot {
    const expectedClass: InputCoordinatorEventClass =
      reason === 'close' ? 'project-close' :
      reason === 'blur' ? 'ui-blur' :
      reason === 'suspend' ? 'system-suspend' :
      reason === 'virtual-target-fault' ? 'virtual-target-pnp-remove' :
      'host-fault';
    if (eventClass !== undefined && eventClass !== expectedClass) throw new Error('stop-reason-event-class-mismatch');
    const classified = eventClass ?? expectedClass;
    if (!['project-close', 'ui-blur', 'system-suspend', 'virtual-target-pnp-remove', 'host-fault'].includes(classified)) {
      throw new Error('stop-event-class-requires-dedicated-api');
    }

    const lifecycleEvent = classified === 'system-suspend' ? 'suspend' : classified === 'host-fault' ? 'crash' : classified === 'virtual-target-pnp-remove' ? 'virtual-target-fault' : 'close';
    const current = this.lifecycle.snapshot();
    let state = this.lifecycle.transition(lifecycleEvent, reason === 'suspend' ? { powerGeneration: current.powerGeneration + 1 } : {});
    const ownershipEvent = classified === 'system-suspend' ? 'suspend' : classified === 'host-fault' ? 'crash' : classified === 'virtual-target-pnp-remove' ? 'virtual-target-fault' : classified === 'ui-blur' ? 'blur' : 'close';
    this.ownership.transition(ownershipEvent);
    this.clearInputState();
    this.gameInputCount = 0;
    this.ymccActionCount = 0;
    this.record(`stop-${reason}-revoke`, 'drop', null, false, classified);
    state = this.lifecycle.transition('safe-zero');
    this.record(`stop-${reason}-neutral`, 'drop', null, true, classified);
    state = this.lifecycle.transition('release');
    state = this.lifecycle.rotateEpoch();
    this.ownership.transition('release');
    this.record(`stop-${reason}-complete`, 'drop', null, true, classified);
    return state;
  }

  /**
   * HC physical-source removal: suppress future publication for the removed
   * source, retain the current virtual target/session, and do not create a new
   * epoch or release proof.  Matching physical identity is an external gate;
   * this mock only models the post-match no-write disposition.
   */
  physicalSourceRemove(physicalSourceIdentity: string): InputLifecycleSnapshot {
    if (!physicalSourceIdentity.trim()) throw new Error('source-identity-required');
    this.lifecycle.transition('physical-source-remove', { physicalSourceIdentity });
    this.ownership.transition('physical-source-remove');
    this.clearInputState();
    this.gameInputCount = 0;
    this.ymccActionCount = 0;
    this.record('source-remove-no-write', 'drop', null, true, 'physical-source-remove');
    return this.lifecycle.snapshot();
  }

  /** A source insert is only a fresh candidate; it does not auto-rearm. */
  physicalSourceInsert(physicalSourceIdentity: string): InputLifecycleSnapshot {
    if (!physicalSourceIdentity.trim()) throw new Error('source-identity-required');
    const state = this.lifecycle.transition('physical-source-insert', { physicalSourceIdentity });
    this.record('source-insert-candidate', 'drop', null, false, 'physical-source-insert');
    return state;
  }

  /**
   * Complete the explicit source/fault recovery transaction. This is a mock
   * of the Coordinator boundary only; it does not claim a real PnP or HID
   * receipt.
   */
  explicitRearm(owner: Exclude<OwnershipSnapshot['owner'], 'none'> = 'game-consumer'): InputLifecycleSnapshot {
    return this.completeRecovery('rearm', owner);
  }

  /** Complete a power-resume transaction; generic released/faulted states are not eligible. */
  resume(owner: Exclude<OwnershipSnapshot['owner'], 'none'> = 'game-consumer'): InputLifecycleSnapshot {
    return this.completeRecovery('resume', owner);
  }

  modalOpen(): OwnershipSnapshot {
    if (['active', 'admitted'].includes(this.lifecycle.snapshot().phase)) {
      this.lifecycle.transition('reconfigure');
      this.lifecycle.transition('safe-zero');
      this.lifecycle.transition('release');
    }
    const owner = this.ownership.transition('modal-open');
    if (this.lifecycle.snapshot().epoch !== owner.epoch) this.lifecycle.rotateEpoch();
    this.clearInputState();
    this.record('modal-open', 'drop', null, false);
    return owner;
  }

  submit(controller: ControllerFrame, motion?: MotionFrame): InputCoordinatorFrameResult {
    const lifecycle = this.lifecycle.snapshot();
    const disposition = lifecycle.sourceAdmission === 'admitted' && ['admitted', 'active'].includes(lifecycle.phase) && lifecycle.epoch === controller.epoch
      ? this.ownership.routeFrame(controller.epoch)
      : 'drop';
    if (disposition === 'drop') {
      const trace = this.record('frame-rejected', disposition, controller.inputSequence, false);
      return { disposition, frame: null, trace };
    }
    const assembled = assembleCanonicalFrame(controller, motion);
    if (!assembled.ok) {
      const trace = this.record(`frame-${(assembled as { ok: false; reason: string }).reason}`, 'drop', controller.inputSequence, false);
      return { disposition: 'drop', frame: null, trace };
    }
    if (this.lifecycle.snapshot().phase === 'admitted') {
      this.lifecycle.transition('frame', { configRevision: controller.configRevision });
    }
    // Foreground-exclusive routing is semantic-only: a canonical frame is
    // available to the UI dispatcher, but never treated as a game report.
    if (disposition === 'game-report') this.gameInputCount += 1;
    if (disposition === 'ymcc-semantic-only') { this.ymccActionCount += 1; this.markInputStateHeld(); }
    const trace = this.record('frame', disposition, controller.inputSequence, disposition === 'ymcc-semantic-only' ? true : assembled.frame.neutral);
    return { disposition, frame: assembled.frame, trace };
  }

  suspend(): InputLifecycleSnapshot {
    return this.stop('suspend');
  }

  release(): InputLifecycleSnapshot {
    let state = this.lifecycle.snapshot();
    if (state.phase === 'quiescing' || state.phase === 'faulted') {
      state = this.lifecycle.transition('safe-zero');
      state = this.lifecycle.transition('release');
    }
    this.ownership.transition('release');
    this.clearInputState();
    if (state.phase === 'released') this.lifecycle.rotateEpoch();
    this.record('release', 'drop', null, true);
    return state;
  }

  snapshotTrace(): readonly InputCoordinatorTraceEvent[] {
    return this.trace.map((event) => ({ ...event }));
  }

  private record(event: string, disposition: FrameDisposition | 'none', inputSequence: number | null, neutral: boolean, eventClass: InputCoordinatorEventClass = 'internal'): InputCoordinatorTraceEvent {
    const lifecycle = this.lifecycle.snapshot();
    const ownership = this.ownership.snapshot();
    const item: InputCoordinatorTraceEvent = {
      event, eventClass, epoch: ownership.epoch, lifecyclePhase: lifecycle.phase, owner: ownership.owner,
      disposition, inputSequence, neutral,
      gameInputCount: this.gameInputCount,
      ymccActionCount: this.ymccActionCount,
      heldCleared: this.heldCleared,
      chordCleared: this.chordCleared,
      shiftCleared: this.shiftCleared,
      admissionStopped: lifecycle.releaseProof?.frameAdmissionStopped === true || lifecycle.sourceAdmission === 'suppressed',
      sourceAdmission: lifecycle.sourceAdmission,
      targetSession: lifecycle.targetSession,
    };
    this.trace.push(item);
    return item;
  }

  /**
   * One synchronous owner transaction. Between revoke and commit, admission
   * is stopped and every physical frame routes to drop; the new owner is not
   * observable until a fresh epoch has reached the neutral barrier.
   */
  private handoff(event: 'summon' | 'dismiss', owner: 'ymcc-frontend' | 'game-consumer'): OwnershipSnapshot {
    const initial = this.lifecycle.snapshot();
    if (!['admitted', 'active'].includes(initial.phase)) throw new Error('owner-handoff-requires-admission');
    this.lifecycle.transition('reconfigure', { configRevision: initial.configRevision });
    this.ownership.revokeAdmission();
    this.clearInputState();
    this.record(`${event}-revoke`, 'drop', null, false);

    this.lifecycle.transition('safe-zero', { configRevision: initial.configRevision });
    this.record(`${event}-neutral`, 'drop', null, true);
    this.lifecycle.transition('release', { configRevision: initial.configRevision });

    // An owner handoff is an explicit rearm transaction, not a system-power
    // resume. Keeping these events distinct prevents close/blur release from
    // being misclassified as a power-resume recovery path.
    let state = this.lifecycle.transition('rearm', { configRevision: initial.configRevision });
    for (const lifecycleEvent of ['verified', 'target-ready', 'neutral', 'admit'] as const) {
      state = this.lifecycle.transition(lifecycleEvent, { configRevision: initial.configRevision });
    }
    const snapshot = this.ownership.atomicSwitch(owner, state.epoch);
    this.gameInputCount = 0;
    this.ymccActionCount = 0;
    this.clearInputState();
    this.record(`${event}-commit`, 'none', null, true);
    return snapshot;
  }

  private clearInputState(): void {
    this.heldCleared = true;
    this.chordCleared = true;
    this.shiftCleared = true;
  }

  private markInputStateHeld(): void {
    this.heldCleared = false;
    this.chordCleared = false;
    this.shiftCleared = false;
  }

  private completeRecovery(event: 'resume' | 'rearm', owner: Exclude<OwnershipSnapshot['owner'], 'none'>): InputLifecycleSnapshot {
    let state = this.lifecycle.transition(event);
    for (const lifecycleEvent of ['verified', 'target-ready', 'neutral', 'admit'] as const) {
      state = this.lifecycle.transition(lifecycleEvent, { configRevision: state.configRevision });
    }
    this.ownership.atomicSwitch(owner, state.epoch);
    this.gameInputCount = 0;
    this.ymccActionCount = 0;
    this.clearInputState();
    this.record(`${event}-commit`, 'none', null, true);
    return state;
  }
}
