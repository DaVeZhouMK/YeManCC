import type {
  GamepadControl,
  GamepadControlCoordinatorCommand,
  GamepadControlCoordinatorPort,
  GamepadControlSnapshot,
} from './gamepadControlContract';

/**
 * Stage 0 shared-hardware transaction contract.
 *
 * This module is deliberately pure state logic. It does not import HC, start
 * a process, subscribe to Windows power events, or dispatch hardware writes.
 * Native remains the future authority for power generation; a future platform
 * host will be the only HC device-session owner.
 */
export const HARDWARE_RESOURCES = ['fan', 'controller', 'motion', 'virtual-gamepad', 'hidhide'] as const;
export type HardwareResource = typeof HARDWARE_RESOURCES[number];
export type RuntimeOwnerMode = 'legacy-direct-hc' | 'platform-owner';
export type CoordinatorPhase = 'idle' | 'ready' | 'suspended' | 'resuming' | 'stopping' | 'stopped' | 'faulted';
export type SessionRetireReason = 'system-pending' | 'device-change' | 'owner-crash' | 'shutdown' | 'owner-switch';
export type CommandKind = 'read' | 'write';

export interface HardwareParticipant {
  id: string;
  resources: readonly HardwareResource[];
  enabled: boolean;
  implementation: 'fan-host' | 'input-host-placeholder';
}

export interface RuntimeSession {
  epoch: string;
  powerGeneration: number;
  ownerIdentity: string;
  writeAdmission: 'none' | 'hc-system-ready' | 'explicit-rearm';
  recoveryRequired: boolean;
}

export interface HardwareCommand {
  requestId: string;
  callerIdentity: string;
  epoch: string;
  resource: HardwareResource;
  kind: CommandKind;
}

export interface CommandAdmission {
  accepted: boolean;
  reason?: string;
  duplicate?: boolean;
}

/**
 * Fan-only admission state. This is a coordinator boundary, not an HC device
 * owner: FanHost remains the sole process that opens, closes, and restores the
 * HC device session. The gate only rejects stale or power-unsafe curve writes
 * before they reach that existing Host route.
 */
export type FanCoordinatorPhase = 'awaiting-host' | 'awaiting-hc-ready' | 'ready' | 'suspending' | 'suspended' | 'resuming' | 'stopping' | 'stopped' | 'faulted';
export type FanCoordinatorPowerEvent = 'suspending' | 'resuming' | 'resume-ready' | 'resumed' | 'shutdown' | 'manual-resume-start' | 'manual-resume-ready';

export interface FanCoordinatorSnapshot {
  phase: FanCoordinatorPhase;
  powerGeneration: number;
  hostSession: string | null;
  wakeReadyGeneration: number | null;
}

export class FanCoordinatorGate {
  private phase: FanCoordinatorPhase = 'awaiting-host';
  private powerGeneration = 0;
  private hostSession: string | null = null;
  // `power.resume-ready` is the native-owned F5 admission boundary. It lets
  // FanHost rebuild its isolated HC device session while native remains
  // Resuming; `power.resumed` is emitted only after the later native commit.
  private wakeReadyGeneration: number | null = 0;
  private readonly requests = new Map<string, CommandAdmission>();

  observeNativePower(event: FanCoordinatorPowerEvent, powerGeneration: number): void {
    this.advancePowerGeneration(powerGeneration);
    if (event === 'suspending') {
      this.phase = 'suspending';
      this.wakeReadyGeneration = null;
    } else if (event === 'resuming' || event === 'manual-resume-start') {
      // `manual-resume-start` is a Fan-local explicit-control rescue. It is
      // never emitted by native and does not claim that the OS broadcast a
      // wake; it only opens the coordinator's bounded Host recovery window
      // after the user has interacted with the live renderer.
      this.phase = 'resuming';
      this.wakeReadyGeneration = null;
    } else if (event === 'resume-ready' || event === 'manual-resume-ready') {
      // `manual-resume-ready` is the Fan-local completion of the explicit
      // `/api/resume` rescue. It is valid only after Host state has proved
      // Open/OpenEvents; it does not replace native `power.resume-ready` for
      // automatic wake admission.

      // A renderer can be recreated after the earlier `resuming` broadcast.
      // This authoritative native edge is therefore allowed to establish the
      // local Resuming phase by itself, but it never represents a commit.
      this.phase = 'resuming';
      this.wakeReadyGeneration = this.powerGeneration;
    } else if (event === 'resumed') {
      // Native emits this only after power.resumeComplete has committed the
      // generation. Preserve a completed F5 session instead of forcing it
      // through a second Open/OpenEvents admission merely because the commit
      // notification reached the renderer.
      if (!this.hostSession) {
        this.phase = 'awaiting-host';
      } else if (this.phase !== 'ready' && this.phase !== 'awaiting-hc-ready') {
        this.phase = 'awaiting-hc-ready';
      }
    } else {
      this.phase = 'stopping';
      this.wakeReadyGeneration = null;
    }
    this.requests.clear();
  }

  /**
   * Record a newer native generation without inventing a power-boundary
   * event. A generation change retires the old fan session and requests, while
   * the subsequent real suspending/resuming/resume-ready/resumed event remains responsible
   * for changing the coordinator phase.
   */
  observePowerGeneration(powerGeneration: number): void {
    if (powerGeneration <= this.powerGeneration) return;
    this.advancePowerGeneration(powerGeneration);
    this.hostSession = null;
    this.requests.clear();
    // A generation reported during an ordinary ready/start boundary is the
    // current baseline. During sleep/resume, however, the F5 barrier must
    // remain closed until the real `resume-ready` event arrives.
    if (this.phase !== 'suspending' && this.phase !== 'suspended' && this.phase !== 'resuming') {
      this.wakeReadyGeneration = powerGeneration;
    }
  }

  beginFanHostSession(session: string, powerGeneration: number): void {
    if (!isIdentity(session)) throw new Error('Invalid FanHost coordinator session');
    this.advancePowerGeneration(powerGeneration);
    if (this.phase === 'suspending' || this.phase === 'suspended' || this.phase === 'stopping' ||
        (this.phase === 'resuming' && !this.isWakeReady(powerGeneration))) {
      throw new Error(`FanHost session cannot begin while ${this.phase}`);
    }
    this.hostSession = session;
    this.phase = 'awaiting-hc-ready';
    this.requests.clear();
  }

  admitHcReady(session: string, powerGeneration: number): void {
    this.requireCurrentSession(session, powerGeneration);
    if (!this.isWakeReady(powerGeneration)) {
      throw new Error(`native-resume-ready-not-observed:generation=${powerGeneration}:ready=${this.wakeReadyGeneration ?? 'none'}`);
    }
    if (this.phase !== 'awaiting-hc-ready' && this.phase !== 'resuming' && this.phase !== 'ready') {
      throw new Error(`HC ready is invalid while ${this.phase}`);
    }
    this.phase = 'ready';
  }

  admitFanWrite(requestId: string, session: string, powerGeneration: number): CommandAdmission {
    if (!isIdentity(requestId)) return { accepted: false, reason: 'invalid-request-id' };
    const previous = this.requests.get(requestId);
    if (previous) return { ...previous, duplicate: true };
    let result: CommandAdmission;
    try {
      this.requireCurrentSession(session, powerGeneration);
      result = this.phase === 'ready'
        ? { accepted: true }
        : { accepted: false, reason: `fan-write-not-admitted:${this.phase}` };
    } catch (error) {
      result = { accepted: false, reason: error instanceof Error ? error.message : String(error) };
    }
    this.requests.set(requestId, result);
    return result;
  }

  releaseFanControl(): void {
    if (!this.hostSession || this.phase === 'stopped' || this.phase === 'suspended' || this.phase === 'stopping') return;
    this.phase = 'awaiting-hc-ready';
    this.requests.clear();
  }

  markSuspended(): void {
    this.phase = 'suspended';
    this.hostSession = null;
    this.requests.clear();
  }

  markStopped(): void {
    this.phase = 'stopped';
    this.hostSession = null;
    // A fully closed session is a fresh startup boundary, not a power
    // transition.  Permit a later explicit start in the same power
    // generation; sleep/resume still clears this value through the power
    // events above.
    this.wakeReadyGeneration = this.powerGeneration;
    this.requests.clear();
  }

  markFaulted(): void {
    this.phase = 'faulted';
    this.requests.clear();
  }

  snapshot(): FanCoordinatorSnapshot {
    return {
      phase: this.phase,
      powerGeneration: this.powerGeneration,
      hostSession: this.hostSession,
      wakeReadyGeneration: this.wakeReadyGeneration,
    };
  }

  isWakeReady(powerGeneration = this.powerGeneration): boolean {
    return this.wakeReadyGeneration === powerGeneration;
  }

  private advancePowerGeneration(powerGeneration: number): void {
    if (!Number.isSafeInteger(powerGeneration) || powerGeneration < this.powerGeneration) {
      throw new Error('Stale native power generation');
    }
    this.powerGeneration = powerGeneration;
  }

  private requireCurrentSession(session: string, powerGeneration: number): void {
    if (!isIdentity(session) || !this.hostSession || session !== this.hostSession) throw new Error('stale-or-missing-fan-session');
    if (!Number.isSafeInteger(powerGeneration) || powerGeneration !== this.powerGeneration) throw new Error('stale-fan-power-generation');
  }
}

/**
 * Stage 0 carries evidence supplied by a future lifecycle implementation; it
 * does not claim to inspect HC, a listener, or OEM state itself.
 */
export interface OwnerReleaseProof {
  ownerIdentity: string;
  sessionRetired: boolean;
  listenerReleased: boolean;
  writeRouteReleased: boolean;
  oemRestoreAttempted: boolean;
}

export interface CoordinatorSnapshot {
  ownerMode: RuntimeOwnerMode;
  phase: CoordinatorPhase;
  powerGeneration: number;
  session: RuntimeSession | null;
  owners: Readonly<Record<HardwareResource, string | null>>;
  participants: readonly HardwareParticipant[];
}

function emptyOwners(): Record<HardwareResource, string | null> {
  return { fan: null, controller: null, motion: null, 'virtual-gamepad': null, hidhide: null };
}

function isIdentity(value: string): boolean {
  return /^[A-Za-z0-9._:-]+$/.test(value);
}

function validateParticipant(participant: HardwareParticipant): void {
  if (!/^[a-z][a-z0-9-]*$/.test(participant.id)) throw new Error(`Invalid hardware participant id: ${participant.id}`);
  if (participant.resources.length === 0) throw new Error(`Hardware participant ${participant.id} must declare a resource`);
  if (new Set(participant.resources).size !== participant.resources.length) throw new Error(`Hardware participant ${participant.id} declares a resource more than once`);
}

function sameCommand(left: HardwareCommand, right: HardwareCommand): boolean {
  return left.callerIdentity === right.callerIdentity
    && left.epoch === right.epoch
    && left.resource === right.resource
    && left.kind === right.kind;
}

function isReleaseProof(proof: OwnerReleaseProof | null | undefined): proof is OwnerReleaseProof {
  return Boolean(proof)
    && isIdentity(proof.ownerIdentity)
    && proof.sessionRetired
    && proof.listenerReleased
    && proof.writeRouteReleased
    && proof.oemRestoreAttempted;
}

/** Encodes cross-process ownership without claiming HC static state is shared. */
export class HardwareCoordinatorPlan implements GamepadControlCoordinatorPort {
  private ownerMode: RuntimeOwnerMode = 'legacy-direct-hc';
  private phase: CoordinatorPhase = 'idle';
  private powerGeneration = 0;
  private session: RuntimeSession | null = null;
  private recoveryRearmRequired = false;
  private readonly owners = emptyOwners();
  private readonly participants = new Map<string, HardwareParticipant>();
  private readonly requests = new Map<string, { command: HardwareCommand; result: CommandAdmission }>();
  private gamepadControl: GamepadControl | null = null;

  /** Stage 1 wiring only: coordinator owns the call boundary; no device I/O. */
  attachGamepadControl(control: GamepadControl): void {
    if (this.gamepadControl && this.gamepadControl !== control) throw new Error('GamepadControl already attached');
    this.gamepadControl = control;
  }

  beginGamepadControl(command: GamepadControlCoordinatorCommand): GamepadControlSnapshot {
    if (!this.gamepadControl) throw new Error('GamepadControl is not attached');
    return this.gamepadControl.beginTransaction(command);
  }

  endGamepadControl(command: GamepadControlCoordinatorCommand): GamepadControlSnapshot {
    if (!this.gamepadControl) throw new Error('GamepadControl is not attached');
    const snapshot = this.gamepadControl.snapshot();
    if (snapshot.runId !== command.runId || snapshot.epoch !== command.epoch || snapshot.powerGeneration !== command.powerGeneration) {
      throw new Error('stale-gamepad-control-transaction');
    }
    return this.gamepadControl.release();
  }

  register(participant: HardwareParticipant): void {
    validateParticipant(participant);
    if (this.participants.has(participant.id)) throw new Error(`Hardware participant already registered: ${participant.id}`);
    for (const resource of participant.resources) {
      const owner = this.owners[resource];
      if (owner !== null) throw new Error(`Hardware resource ${resource} is already owned by ${owner}`);
    }
    this.participants.set(participant.id, { ...participant, resources: [...participant.resources] });
    for (const resource of participant.resources) this.owners[resource] = participant.id;
  }

  setParticipantEnabled(participantId: string, enabled: boolean): void {
    const participant = this.participants.get(participantId);
    if (!participant) throw new Error(`Unknown hardware participant ${participantId}`);
    participant.enabled = enabled;
  }

  /** A mode change is illegal without evidence that the old HC owner released. */
  switchOwnerMode(next: RuntimeOwnerMode, previousOwnerRelease: OwnerReleaseProof | null): void {
    if (next === this.ownerMode) return;
    if (!isReleaseProof(previousOwnerRelease)) {
      throw new Error(`Refusing ${this.ownerMode} -> ${next} without previous-owner release proof`);
    }
    this.retireSession();
    this.recoveryRearmRequired = false;
    this.ownerMode = next;
    this.phase = 'stopped';
  }

  beginPlatformSession(epoch: string, ownerIdentity: string, powerGeneration: number): void {
    if (this.ownerMode !== 'platform-owner') throw new Error('Platform session is unavailable while LegacyDirectHC owns hardware');
    if (!isIdentity(epoch) || !isIdentity(ownerIdentity)) throw new Error('Invalid session identity');
    if (!Number.isSafeInteger(powerGeneration) || powerGeneration < this.powerGeneration) throw new Error('Refusing stale power generation');
    this.retireSession();
    this.powerGeneration = powerGeneration;
    this.session = { epoch, ownerIdentity, powerGeneration, writeAdmission: 'none', recoveryRequired: this.recoveryRearmRequired };
    this.phase = 'resuming';
  }

  /** Only the authoritative native lifecycle may revoke an epoch. */
  observeNativeLifecycle(event: SessionRetireReason, powerGeneration: number): void {
    if (!Number.isSafeInteger(powerGeneration) || powerGeneration < this.powerGeneration) throw new Error('Refusing stale native lifecycle event');
    this.powerGeneration = powerGeneration;
    this.retireSession();
    this.recoveryRearmRequired = event === 'owner-crash' || event === 'device-change';
    this.phase = event === 'system-pending' ? 'suspended' : event === 'shutdown' ? 'stopping' : 'faulted';
  }

  /** Normal HC SystemReady allows writes only after a fresh session rebuild. */
  admitHcSystemReady(epoch: string): void {
    const session = this.requireCurrentSession(epoch);
    if (session.recoveryRequired) throw new Error('Abnormal recovery requires explicit rearm');
    if (this.phase !== 'resuming' && this.phase !== 'ready') throw new Error(`Cannot admit HC ready while ${this.phase}`);
    session.writeAdmission = 'hc-system-ready';
    this.phase = 'ready';
  }

  /** Crash/device-change recovery cannot reuse a cached fan write state. */
  explicitRearm(epoch: string): void {
    const session = this.requireCurrentSession(epoch);
    if (!session.recoveryRequired) throw new Error('Explicit rearm is only valid after abnormal recovery');
    if (this.phase !== 'resuming') throw new Error(`Cannot explicitly rearm while ${this.phase}`);
    session.writeAdmission = 'explicit-rearm';
    session.recoveryRequired = false;
    this.recoveryRearmRequired = false;
    this.phase = 'ready';
  }

  admit(command: HardwareCommand): CommandAdmission {
    const previous = this.requests.get(command.requestId);
    if (previous) {
      return sameCommand(previous.command, command)
        ? { ...previous.result, duplicate: true }
        : { accepted: false, reason: 'request-id-reused-with-different-command' };
    }
    const result = this.evaluate(command);
    this.requests.set(command.requestId, { command: { ...command }, result });
    return result;
  }

  snapshot(): CoordinatorSnapshot {
    return {
      ownerMode: this.ownerMode,
      phase: this.phase,
      powerGeneration: this.powerGeneration,
      session: this.session ? { ...this.session } : null,
      owners: { ...this.owners },
      participants: Array.from(this.participants.values(), (item) => ({ ...item, resources: [...item.resources] })),
    };
  }

  private evaluate(command: HardwareCommand): CommandAdmission {
    if (!isIdentity(command.requestId) || !isIdentity(command.callerIdentity)) return { accepted: false, reason: 'invalid-command-identity' };
    if (this.ownerMode !== 'platform-owner') return { accepted: false, reason: 'legacy-direct-hc-not-routed' };
    if (this.phase !== 'ready') return { accepted: false, reason: `lifecycle-not-ready:${this.phase}` };
    if (!this.session || command.epoch !== this.session.epoch) return { accepted: false, reason: 'stale-or-missing-session-epoch' };
    const owner = this.owners[command.resource];
    if (!owner || owner !== command.callerIdentity) return { accepted: false, reason: 'resource-not-owned-by-caller' };
    if (!this.participants.get(owner)?.enabled) return { accepted: false, reason: 'resource-owner-disabled' };
    if (command.kind === 'write' && this.session.writeAdmission === 'none') return { accepted: false, reason: 'write-admission-required' };
    return { accepted: true };
  }

  private requireCurrentSession(epoch: string): RuntimeSession {
    if (!this.session || this.session.epoch !== epoch) throw new Error('Stale or missing session epoch');
    return this.session;
  }

  private retireSession(): void {
    this.session = null;
    this.requests.clear();
  }
}

// Stage 0 intentionally exports no process-wide routed instance.
