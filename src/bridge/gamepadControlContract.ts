/**
 * Stage 1/2 GamepadControl contract and source-admission state machine.
 *
 * This file is intentionally a pure contract/state-machine skeleton.  It has
 * no HID, XInput, HidHide, InputHost, native, process, or filesystem access.
 * A later runtime adapter may implement the ports, but it must not widen the
 * ownership rules encoded here.
 *
 * Fixed product vocabulary:
 *   S = one selected physical source
 *   B = one complete logical frame
 *   V = GamepadControl's internal output state
 *   E = one public endpoint
 *   C = normal consumers/observations of E or an allowlisted B projection
 */

export type GamepadControlMode =
  | 'hold'
  | 'physical-pass-through'
  | 'virtual-xbox360'
  | 'virtual-dualshock4'
  | 'virtual-dualsense'
  | 'virtual-steamdeck'
  | 'virtual-elite'
  | 'virtual-dualsense-edge';

export type GamepadControlPhase =
  | 'disabled'
  | 'verified'
  | 'opening'
  | 'target-ready'
  | 'neutralized'
  | 'admitted'
  | 'active'
  | 'quiescing'
  | 'safe-zero'
  | 'released'
  | 'faulted';

export type PhysicalSourceState =
  | 'physical-local-present'
  | 'physical-external-present'
  | 'physical-selected'
  | 'physical-suppressed'
  | 'physical-unadmitted'
  | 'physical-removed'
  | 'physical-ambiguous';

export type SourceKind = 'local' | 'external';
export type EndpointKind = 'physical' | 'virtual';
export type VirtualPersona = 'xbox360' | 'dualshock4' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge';
export type EndpointTransport = 'physical-hid' | 'virtual-hid';
export type EndpointWriter = 'none' | 'hid-virtual-writer';

export type ConsumerKind =
  | 'ymcc-normal'
  | 'ymcc-projection'
  | 'os'
  | 'steam'
  | 'game';

export type ConsumerPath =
  | 'normal-os'
  | 'b-projection'
  | 'external-observation';

export interface PhysicalSourceIdentity {
  /** Exact stable identity. Display names and XInput slots are not identities. */
  readonly identity: string;
  readonly kind: SourceKind;
  /** The physical public endpoint belonging to this source. */
  readonly physicalEndpointIdentity: string;
  readonly generation: number;
}

export interface PhysicalSourceRecord extends PhysicalSourceIdentity {
  readonly state: PhysicalSourceState;
}

export interface SourceBinding {
  readonly sourceIdentity: string;
  readonly sourceGeneration: number;
  readonly kind: SourceKind;
  readonly physicalEndpointIdentity: string;
}

export interface StickValue {
  readonly x: number;
  readonly y: number;
}

export interface TriggerValue {
  readonly left: number;
  readonly right: number;
}

export interface GyroContribution {
  readonly x: number;
  readonly y: number;
  readonly admitted: boolean;
}

export interface LogicalFrame {
  readonly frameId: number;
  readonly frameHash: string;
  readonly epoch: number;
  readonly sourceIdentity: string;
  readonly sourceGeneration: number;
  readonly buttons: Readonly<Record<string, boolean>>;
  readonly leftStick: StickValue;
  readonly rightStick: StickValue;
  readonly triggers: TriggerValue;
  /** Gyro is only a contribution to B, never a second stick frame. */
  readonly gyroContribution: GyroContribution;
  readonly neutral: boolean;
}

export interface PublicEndpoint {
  readonly identity: string;
  readonly kind: EndpointKind;
  /** Runtime target is HID; HC VirtualManager is reference behavior only. */
  readonly transport: EndpointTransport;
  readonly persona: 'physical' | VirtualPersona;
  readonly endpointEpoch: number;
  readonly writer: EndpointWriter;
  readonly sourceIdentity: string;
}

export interface ConsumerReceipt {
  readonly kind: ConsumerKind;
  readonly path: ConsumerPath;
  readonly observed: boolean;
  readonly endpointIdentity: string;
  readonly endpointEpoch: number;
  readonly frameId?: number;
  readonly processIdentity?: string;
}

export interface ControlLease {
  readonly identity: string;
  readonly epoch: number;
  readonly previousMode: Exclude<GamepadControlMode, 'hold'>;
}

export interface GamepadControlCoordinatorCommand {
  readonly runId: string;
  readonly epoch: number;
  readonly powerGeneration: number;
  readonly configRevision: number;
  readonly ownerIdentity: 'HardwareCoordinatorPlan';
  /** Source-admission policy restored by the Coordinator on a new run. */
  readonly suppressedPhysicalSourceIdentities?: readonly string[];
}

export interface GamepadControlSnapshot {
  readonly runId: string | null;
  readonly epoch: number;
  readonly powerGeneration: number;
  readonly configRevision: number;
  readonly phase: GamepadControlPhase;
  readonly mode: GamepadControlMode;
  readonly sources: readonly PhysicalSourceRecord[];
  /** Exact source identities that must remain unadmitted across re-enumeration. */
  readonly suppressedPhysicalSourceIdentities: readonly string[];
  readonly selectedSource: SourceBinding | null;
  readonly endpoint: PublicEndpoint | null;
  readonly pendingPersona: VirtualPersona | null;
  readonly neutralBarrier: boolean;
  readonly antiEchoVerified: boolean;
  readonly consumerEvidenceReady: boolean;
  readonly controlLease: ControlLease | null;
  readonly projectPublishCount: number;
  readonly externalConsumerInputCount: number;
  readonly lastFrame: LogicalFrame | null;
  readonly failClosedReason: string | null;
}

export interface LogicalFrameInput {
  readonly frameId: number;
  readonly sourceIdentity: string;
  readonly sourceGeneration: number;
  readonly buttons?: Readonly<Record<string, boolean>>;
  readonly leftStick?: StickValue;
  readonly rightStick?: StickValue;
  readonly triggers?: TriggerValue;
  readonly gyroContribution?: GyroContribution;
}

export interface FramePublication {
  readonly accepted: boolean;
  readonly disposition: 'no-write' | 'virtual-public-write' | 'b-projection-only' | 'drop';
  readonly frame: LogicalFrame | null;
  readonly reason?: string;
}

const ZERO_STICK: StickValue = Object.freeze({ x: 0, y: 0 });
const ZERO_TRIGGERS: TriggerValue = Object.freeze({ left: 0, right: 0 });
const ZERO_GYRO: GyroContribution = Object.freeze({ x: 0, y: 0, admitted: false });

function validIdentity(value: string): boolean {
  return /^[A-Za-z0-9][A-Za-z0-9._:/-]*$/.test(value);
}

function validGeneration(value: number): boolean {
  return Number.isSafeInteger(value) && value >= 0;
}

function clampUnit(value: number): number {
  return Math.max(-1, Math.min(1, Number.isFinite(value) ? value : 0));
}

function normalizeStick(value: StickValue | undefined): StickValue {
  return Object.freeze({ x: clampUnit(value?.x ?? 0), y: clampUnit(value?.y ?? 0) });
}

function normalizeTriggers(value: TriggerValue | undefined): TriggerValue {
  return Object.freeze({
    left: clampUnit(value?.left ?? 0),
    right: clampUnit(value?.right ?? 0),
  });
}

function normalizeGyro(value: GyroContribution | undefined): GyroContribution {
  return Object.freeze({
    x: clampUnit(value?.x ?? 0),
    y: clampUnit(value?.y ?? 0),
    admitted: value?.admitted === true,
  });
}

function stableButtons(buttons: Readonly<Record<string, boolean>> | undefined): Readonly<Record<string, boolean>> {
  const result: Record<string, boolean> = {};
  for (const key of Object.keys(buttons ?? {}).sort()) result[key] = buttons?.[key] === true;
  return Object.freeze(result);
}

function hashFrame(input: Omit<LogicalFrame, 'frameHash' | 'neutral'>): string {
  const payload = JSON.stringify({
    frameId: input.frameId,
    epoch: input.epoch,
    sourceIdentity: input.sourceIdentity,
    sourceGeneration: input.sourceGeneration,
    buttons: input.buttons,
    leftStick: input.leftStick,
    rightStick: input.rightStick,
    triggers: input.triggers,
    gyroContribution: input.gyroContribution,
  });
  let hash = 2166136261;
  for (let index = 0; index < payload.length; index += 1) {
    hash ^= payload.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(16).padStart(8, '0');
}

export function isNeutralFrame(frame: Pick<LogicalFrame, 'buttons' | 'leftStick' | 'rightStick' | 'triggers' | 'gyroContribution'>): boolean {
  const buttonsNeutral = Object.values(frame.buttons).every((pressed) => !pressed);
  const axesNeutral = [frame.leftStick, frame.rightStick].every((stick) => Math.abs(stick.x) <= 0.05 && Math.abs(stick.y) <= 0.05);
  const triggersNeutral = frame.triggers.left <= 0.05 && frame.triggers.right <= 0.05;
  const gyroNeutral = Math.abs(frame.gyroContribution.x) <= 0.05
    && Math.abs(frame.gyroContribution.y) <= 0.05;
  return buttonsNeutral && axesNeutral && triggersNeutral && gyroNeutral;
}

export function createLogicalFrame(epoch: number, input: LogicalFrameInput): LogicalFrame {
  if (!Number.isSafeInteger(epoch) || epoch < 0) throw new Error('invalid-frame-epoch');
  if (!validIdentity(input.sourceIdentity)) throw new Error('frame-source-identity-required');
  if (!validGeneration(input.sourceGeneration)) throw new Error('frame-source-generation-invalid');
  if (!Number.isSafeInteger(input.frameId) || input.frameId < 0) throw new Error('frame-id-invalid');
  const withoutHash = {
    frameId: input.frameId,
    epoch,
    sourceIdentity: input.sourceIdentity,
    sourceGeneration: input.sourceGeneration,
    buttons: stableButtons(input.buttons),
    leftStick: normalizeStick(input.leftStick),
    rightStick: normalizeStick(input.rightStick),
    triggers: normalizeTriggers(input.triggers),
    gyroContribution: normalizeGyro(input.gyroContribution),
  } as const;
  const neutral = isNeutralFrame(withoutHash);
  return Object.freeze({ ...withoutHash, frameHash: hashFrame(withoutHash), neutral });
}

function cloneSources(sources: readonly PhysicalSourceRecord[]): readonly PhysicalSourceRecord[] {
  return sources.map((source) => ({ ...source }));
}

function sourceState(source: PhysicalSourceIdentity): PhysicalSourceState {
  return source.kind === 'local' ? 'physical-local-present' : 'physical-external-present';
}

function cloneEndpoint(endpoint: PublicEndpoint | null): PublicEndpoint | null {
  return endpoint ? { ...endpoint } : null;
}

function cloneFrame(frame: LogicalFrame | null): LogicalFrame | null {
  return frame ? {
    ...frame,
    buttons: { ...frame.buttons },
    leftStick: { ...frame.leftStick },
    rightStick: { ...frame.rightStick },
    triggers: { ...frame.triggers },
    gyroContribution: { ...frame.gyroContribution },
  } : null;
}

/**
 * Pure contract/mock. Every method is deterministic and records only local
 * contract state.  It is deliberately not a device adapter.
 */
export class GamepadControl {
  private runId: string | null = null;
  private epoch = 0;
  private powerGeneration = 0;
  private configRevision = 0;
  private phase: GamepadControlPhase = 'disabled';
  private mode: GamepadControlMode = 'hold';
  private endpointEpoch = 0;
  private sources: PhysicalSourceRecord[] = [];
  private readonly suppressedPhysicalSourceIdentities = new Set<string>();
  private selectedSource: SourceBinding | null = null;
  private endpoint: PublicEndpoint | null = null;
  private pendingPersona: VirtualPersona | null = null;
  private neutralBarrier = false;
  private antiEchoVerified = false;
  private consumerEvidenceReady = false;
  private controlLease: ControlLease | null = null;
  private previousMode: Exclude<GamepadControlMode, 'hold'> | null = null;
  private projectPublishCount = 0;
  private externalConsumerInputCount = 0;
  private lastFrame: LogicalFrame | null = null;
  private failClosedReason: string | null = null;
  private readonly consumerReceipts = new Map<ConsumerKind, ConsumerReceipt>();

  beginTransaction(command: GamepadControlCoordinatorCommand): GamepadControlSnapshot {
    if (command.ownerIdentity !== 'HardwareCoordinatorPlan') throw new Error('coordinator-owner-required');
    if (!validIdentity(command.runId) || !Number.isSafeInteger(command.epoch) || command.epoch < 0) throw new Error('transaction-identity-invalid');
    if (!validGeneration(command.powerGeneration) || !Number.isSafeInteger(command.configRevision) || command.configRevision < 0) throw new Error('transaction-generation-invalid');
    const requestedSuppression = new Set<string>();
    for (const identity of command.suppressedPhysicalSourceIdentities ?? []) {
      if (!validIdentity(identity) || requestedSuppression.has(identity)) throw new Error('suppression-policy-invalid');
      requestedSuppression.add(identity);
    }
    if (this.phase !== 'disabled' && this.phase !== 'released' && this.phase !== 'faulted') throw new Error('transaction-already-open');
    this.runId = command.runId;
    this.epoch = command.epoch;
    this.powerGeneration = command.powerGeneration;
    this.configRevision = command.configRevision;
    this.phase = 'verified';
    this.mode = 'hold';
    this.endpointEpoch = 0;
    this.sources = [];
    this.suppressedPhysicalSourceIdentities.clear();
    for (const identity of requestedSuppression) this.suppressedPhysicalSourceIdentities.add(identity);
    this.selectedSource = null;
    this.endpoint = null;
    this.pendingPersona = null;
    this.neutralBarrier = false;
    this.antiEchoVerified = false;
    this.consumerEvidenceReady = false;
    this.controlLease = null;
    this.previousMode = null;
    this.projectPublishCount = 0;
    this.externalConsumerInputCount = 0;
    this.lastFrame = null;
    this.failClosedReason = null;
    this.consumerReceipts.clear();
    return this.snapshot();
  }

  /** Source discovery is identity-only; it never selects by order or slot. */
  discoverSources(sources: readonly PhysicalSourceIdentity[]): GamepadControlSnapshot {
    this.requirePhase('verified', 'opening', 'target-ready', 'neutralized', 'admitted', 'active');
    const identities = new Set<string>();
    const physicalEndpoints = new Set<string>();
    const next: PhysicalSourceRecord[] = [];
    for (const source of sources) {
      if (!validIdentity(source.identity) || !validIdentity(source.physicalEndpointIdentity) || !validGeneration(source.generation)) {
        return this.failClosed('source-identity-or-generation-missing');
      }
      if (identities.has(source.identity) || physicalEndpoints.has(source.physicalEndpointIdentity)) {
        return this.failClosed('source-identity-ambiguous');
      }
      identities.add(source.identity);
      physicalEndpoints.add(source.physicalEndpointIdentity);
      next.push({ ...source, state: sourceState(source) });
    }
    if (next.length === 0) {
      this.sources = [];
      this.selectedSource = null;
      this.releaseEndpointToHold('no-physical-source');
      return this.snapshot();
    }
    const selectedBeforeDiscovery = this.selectedSource;
    const selectedAfterDiscovery = selectedBeforeDiscovery
      ? next.find((source) => source.identity === selectedBeforeDiscovery.sourceIdentity)
      : undefined;
    const selectedBindingStillValid = !!selectedBeforeDiscovery
      && !!selectedAfterDiscovery
      && selectedBeforeDiscovery.sourceGeneration === selectedAfterDiscovery.generation
      && selectedBeforeDiscovery.physicalEndpointIdentity === selectedAfterDiscovery.physicalEndpointIdentity;
    this.sources = next.map((source) => {
      if (selectedBindingStillValid
        && selectedBeforeDiscovery?.sourceIdentity === source.identity
        && selectedBeforeDiscovery.sourceGeneration === source.generation
        && selectedBeforeDiscovery.physicalEndpointIdentity === source.physicalEndpointIdentity) {
        return { ...source, state: 'physical-selected' };
      }
      if (this.suppressedPhysicalSourceIdentities.has(source.identity)) return { ...source, state: 'physical-suppressed' };
      return source;
    });
    this.phase = 'opening';
    if (this.selectedSource && !selectedBindingStillValid) {
      this.selectedSource = null;
      this.releaseEndpointToHold(selectedAfterDiscovery ? 'selected-source-binding-changed' : 'selected-source-disappeared');
    }
    return this.snapshot();
  }

  selectPhysicalSource(identity: string): GamepadControlSnapshot {
    if (!validIdentity(identity)) return this.failClosed('selected-source-identity-required');
    const selected = this.sources.find((source) => source.identity === identity);
    if (!selected || this.suppressedPhysicalSourceIdentities.has(identity)
      || selected.state === 'physical-suppressed' || selected.state === 'physical-removed') {
      return this.failClosed('selected-source-not-admissible');
    }
    this.sources = this.sources.map((source) => ({
      ...source,
      state: source.identity === identity ? 'physical-selected' : 'physical-suppressed',
    }));
    for (const source of this.sources) {
      if (source.identity !== identity) this.suppressedPhysicalSourceIdentities.add(source.identity);
    }
    this.selectedSource = {
      sourceIdentity: selected.identity,
      sourceGeneration: selected.generation,
      kind: selected.kind,
      physicalEndpointIdentity: selected.physicalEndpointIdentity,
    };
    this.phase = 'target-ready';
    this.mode = 'hold';
    this.pendingPersona = null;
    this.endpoint = null;
    this.neutralBarrier = false;
    this.antiEchoVerified = false;
    this.consumerEvidenceReady = false;
    this.consumerReceipts.clear();
    this.failClosedReason = null;
    return this.snapshot();
  }

  /**
   * Exact single-device suppression.  It cannot select another source and it
   * cannot create a virtual endpoint.  A selected-source suppression enters
   * hold/no-write until the Coordinator explicitly starts a new transaction.
   */
  suppressPhysicalSource(identity: string): GamepadControlSnapshot {
    if (!validIdentity(identity)) return this.failClosed('suppression-identity-required');
    const source = this.sources.find((item) => item.identity === identity);
    if (!source) return this.failClosed('suppression-source-not-found');
    this.suppressedPhysicalSourceIdentities.add(identity);
    this.sources = this.sources.map((item) => item.identity === identity
      ? { ...item, state: 'physical-suppressed' }
      : { ...item });
    if (this.selectedSource?.sourceIdentity === identity) {
      this.selectedSource = null;
      this.releaseEndpointToHold('selected-source-suppressed');
    }
    return this.snapshot();
  }

  /**
   * Remove an exact admission suppression.  This never selects the source and
   * never creates an endpoint; the Coordinator must explicitly select it.
   */
  unsuppressPhysicalSource(identity: string): GamepadControlSnapshot {
    if (!validIdentity(identity)) return this.failClosed('unsuppression-identity-required');
    const source = this.sources.find((item) => item.identity === identity);
    if (!source) return this.failClosed('unsuppression-source-not-found');
    if (this.selectedSource?.sourceIdentity === identity) return this.failClosed('selected-source-cannot-be-unsuppressed');
    this.suppressedPhysicalSourceIdentities.delete(identity);
    this.sources = this.sources.map((item) => item.identity === identity
      ? { ...item, state: sourceState(item) }
      : { ...item });
    this.failClosedReason = null;
    return this.snapshot();
  }

  /**
   * Build the one physical E without taking ownership of the physical writer.
   * The method only binds the public identity for the mock and never submits a
   * physical report or a gyro report.
   */
  preparePhysicalPassThrough(): GamepadControlSnapshot {
    const source = this.requireSelectedSource();
    this.phase = 'target-ready';
    this.mode = 'hold';
    this.pendingPersona = null;
    this.endpoint = {
      identity: source.physicalEndpointIdentity,
      kind: 'physical',
      transport: 'physical-hid',
      persona: 'physical',
      endpointEpoch: this.nextEndpointEpoch(),
      writer: 'none',
      sourceIdentity: source.identity,
    };
    this.antiEchoVerified = true;
    this.neutralBarrier = false;
    this.consumerEvidenceReady = false;
    this.consumerReceipts.clear();
    this.failClosedReason = null;
    return this.snapshot();
  }

  prepareVirtualEndpoint(persona: VirtualPersona, endpointIdentity: string): GamepadControlSnapshot {
    const source = this.requireSelectedSource();
    if (!validIdentity(endpointIdentity)) return this.failClosed('virtual-endpoint-identity-required');
    if (source.identity === endpointIdentity || source.physicalEndpointIdentity === endpointIdentity) {
      return this.failClosed('virtual-endpoint-self-echo');
    }
    if (this.endpoint) return this.failClosed('endpoint-transition-requires-release');
    this.phase = 'opening';
    this.mode = 'hold';
    this.pendingPersona = persona;
    this.endpoint = {
      identity: endpointIdentity,
      kind: 'virtual',
      transport: 'virtual-hid',
      persona,
      endpointEpoch: this.nextEndpointEpoch(),
      writer: 'hid-virtual-writer',
      sourceIdentity: source.identity,
    };
    this.antiEchoVerified = this.endpoint.identity !== source.identity && this.endpoint.identity !== source.physicalEndpointIdentity;
    this.neutralBarrier = false;
    this.consumerEvidenceReady = false;
    this.consumerReceipts.clear();
    this.failClosedReason = null;
    return this.snapshot();
  }

  /** A neutral B is required for every endpoint transition, including no-write physical pass-through. */
  submitNeutral(frame: LogicalFrame): GamepadControlSnapshot {
    if (!this.endpoint) return this.failClosed('neutral-without-endpoint');
    if (!this.sameSelectedSource(frame) || !frame.neutral) return this.failClosed('same-source-full-neutral-required');
    if (this.endpoint.kind === 'virtual' && !this.antiEchoVerified) return this.failClosed('anti-echo-not-verified');
    this.lastFrame = frame;
    this.neutralBarrier = true;
    this.phase = 'neutralized';
    this.failClosedReason = null;
    return this.snapshot();
  }

  /** Consumer receipts are observations, not generated by GamepadControl. */
  recordConsumerEvidence(receipt: ConsumerReceipt): GamepadControlSnapshot {
    if (!this.endpoint) return this.failClosed('consumer-evidence-without-endpoint');
    if (!receipt.observed || receipt.endpointIdentity !== this.endpoint.identity || receipt.endpointEpoch !== this.endpoint.endpointEpoch) {
      return this.failClosed('consumer-evidence-endpoint-mismatch');
    }
    if (receipt.path === 'b-projection' && receipt.kind !== 'ymcc-projection') return this.failClosed('consumer-path-kind-mismatch');
    if (receipt.path !== 'b-projection' && receipt.kind === 'ymcc-projection') return this.failClosed('projection-path-required');
    this.consumerReceipts.set(receipt.kind, { ...receipt });
    this.consumerEvidenceReady = this.consumerReceipts.has('ymcc-normal') && this.consumerReceipts.has('game');
    this.failClosedReason = null;
    return this.snapshot();
  }

  /** Admit the single endpoint after neutral, identity and consumer gates. */
  admit(): GamepadControlSnapshot {
    if (!this.endpoint) return this.failClosed('admit-without-endpoint');
    if (!this.selectedSource) return this.failClosed('admit-without-selected-source');
    if (!this.neutralBarrier) return this.failClosed('admit-without-neutral');
    if (!this.antiEchoVerified) return this.failClosed('admit-without-anti-echo');
    if (!this.consumerEvidenceReady) return this.failClosed('admit-without-consumer-evidence');
    if (this.endpoint.kind === 'virtual' && this.pendingPersona === null) return this.failClosed('virtual-persona-missing');
    this.mode = this.endpoint.kind === 'physical'
      ? 'physical-pass-through'
      : this.pendingPersona === 'xbox360' ? 'virtual-xbox360'
      : this.pendingPersona === 'steamdeck' ? 'virtual-steamdeck'
      : this.pendingPersona === 'dualsense' ? 'virtual-dualsense'
      : this.pendingPersona === 'elite' ? 'virtual-elite'                    // 批112
      : this.pendingPersona === 'dualsense-edge' ? 'virtual-dualsense-edge'  // 批112
      : 'virtual-dualshock4';
    this.phase = 'admitted';
    this.failClosedReason = null;
    return this.snapshot();
  }

  activate(): GamepadControlSnapshot {
    if (this.phase !== 'admitted') return this.failClosed('activate-before-admit');
    this.phase = 'active';
    return this.snapshot();
  }

  submitFrame(input: LogicalFrameInput): FramePublication {
    const frame = createLogicalFrame(this.epoch, input);
    if (this.phase !== 'active') return this.dropFrame(frame, `frame-not-active:${this.phase}`);
    // Anti-echo is checked before ordinary source admission.  A virtual
    // endpoint must never be accepted as a new source, even when the frame
    // would otherwise be rejected as a generic source mismatch.
    if (this.endpoint?.kind === 'virtual' && frame.sourceIdentity === this.endpoint.identity) {
      this.failClosedReason = 'virtual-self-echo-or-identity-ambiguity';
      this.mode = 'hold';
      this.phase = 'faulted';
      return this.dropFrame(frame, this.failClosedReason);
    }
    if (!this.selectedSource || !this.sameSelectedSource(frame)) return this.dropFrame(frame, 'frame-source-mismatch');
    this.lastFrame = frame;
    if (this.controlLease) return { accepted: true, disposition: 'b-projection-only', frame };
    if (this.mode === 'physical-pass-through') {
      // E is the selected physical endpoint. GamepadControl observes B but
      // deliberately does not write the entity's report or gyro.
      return { accepted: true, disposition: 'no-write', frame };
    }
    if (!this.endpoint || this.endpoint.kind !== 'virtual') return this.dropFrame(frame, 'virtual-endpoint-missing');
    if (!this.antiEchoVerified || this.endpoint.identity === frame.sourceIdentity || this.endpoint.identity === this.selectedSource.sourceIdentity) {
      this.failClosedReason = 'virtual-self-echo-or-identity-ambiguity';
      this.mode = 'hold';
      this.phase = 'faulted';
      return this.dropFrame(frame, this.failClosedReason);
    }
    return { accepted: true, disposition: 'virtual-public-write', frame };
  }

  requestControlLease(identity: string): GamepadControlSnapshot {
    if (!validIdentity(identity)) return this.failClosed('control-lease-identity-required');
    if (this.phase !== 'active' || this.mode === 'hold' || this.controlLease) return this.failClosed('control-lease-not-admissible');
    this.previousMode = this.mode;
    this.controlLease = { identity, epoch: this.epoch, previousMode: this.mode };
    this.mode = 'hold';
    return this.snapshot();
  }

  consumeProjection(frame: LogicalFrame, semanticAction: string): GamepadControlSnapshot {
    if (!this.controlLease || this.controlLease.epoch !== this.epoch) return this.failClosed('control-lease-missing');
    if (!semanticAction.trim() || !this.sameSelectedSource(frame)) return this.failClosed('projection-frame-or-action-invalid');
    this.projectPublishCount += 1;
    this.lastFrame = frame;
    return this.snapshot();
  }

  releaseControlLease(neutral: LogicalFrame): GamepadControlSnapshot {
    if (!this.controlLease) return this.failClosed('control-lease-missing');
    if (!neutral.neutral || !this.sameSelectedSource(neutral)) return this.failClosed('lease-release-neutral-required');
    this.lastFrame = neutral;
    this.neutralBarrier = true;
    this.mode = this.previousMode ?? this.controlLease.previousMode;
    this.previousMode = null;
    this.controlLease = null;
    this.failClosedReason = null;
    return this.snapshot();
  }

  /** External observation is kept separate from project projection counters. */
  recordExternalConsumerInput(kind: 'steam' | 'game', frameId: number): GamepadControlSnapshot {
    if (!this.endpoint || !Number.isSafeInteger(frameId) || frameId < 0) return this.failClosed('external-consumer-observation-invalid');
    if (!this.consumerReceipts.has(kind)) return this.failClosed('external-consumer-evidence-missing');
    this.externalConsumerInputCount += 1;
    return this.snapshot();
  }

  /** Release only a virtual endpoint and restore the physical public endpoint. */
  disableVirtual(neutral: LogicalFrame): GamepadControlSnapshot {
    if (this.mode !== 'virtual-xbox360' && this.mode !== 'virtual-dualshock4' && this.mode !== 'virtual-steamdeck' && this.mode !== 'virtual-dualsense' && this.mode !== 'virtual-elite' && this.mode !== 'virtual-dualsense-edge') return this.failClosed('virtual-mode-not-active');
    const source = this.requireSelectedSource();
    if (!neutral.neutral || !this.sameSelectedSource(neutral)) return this.failClosed('virtual-release-neutral-required');
    if (!this.endpoint || this.endpoint.kind !== 'virtual') return this.failClosed('virtual-release-endpoint-missing');
    this.mode = 'hold';
    this.phase = 'quiescing';
    this.lastFrame = neutral;
    this.neutralBarrier = true;
    this.endpoint = null;
    this.pendingPersona = null;
    this.neutralBarrier = false;
    this.antiEchoVerified = false;
    this.consumerEvidenceReady = false;
    this.consumerReceipts.clear();
    return this.preparePhysicalPassThroughFrom(source);
  }

  fault(reason: string): GamepadControlSnapshot {
    this.mode = 'hold';
    this.phase = 'faulted';
    this.endpoint = null;
    this.pendingPersona = null;
    this.neutralBarrier = false;
    this.antiEchoVerified = false;
    this.consumerEvidenceReady = false;
    this.consumerReceipts.clear();
    this.failClosedReason = reason.trim() || 'faulted';
    return this.snapshot();
  }

  release(): GamepadControlSnapshot {
    this.mode = 'hold';
    this.phase = 'released';
    this.endpoint = null;
    this.selectedSource = null;
    this.pendingPersona = null;
    this.neutralBarrier = false;
    this.antiEchoVerified = false;
    this.consumerEvidenceReady = false;
    this.controlLease = null;
    this.consumerReceipts.clear();
    return this.snapshot();
  }

  snapshot(): GamepadControlSnapshot {
    return {
      runId: this.runId,
      epoch: this.epoch,
      powerGeneration: this.powerGeneration,
      configRevision: this.configRevision,
      phase: this.phase,
      mode: this.mode,
      sources: cloneSources(this.sources),
      suppressedPhysicalSourceIdentities: [...this.suppressedPhysicalSourceIdentities].sort(),
      selectedSource: this.selectedSource ? { ...this.selectedSource } : null,
      endpoint: cloneEndpoint(this.endpoint),
      pendingPersona: this.pendingPersona,
      neutralBarrier: this.neutralBarrier,
      antiEchoVerified: this.antiEchoVerified,
      consumerEvidenceReady: this.consumerEvidenceReady,
      controlLease: this.controlLease ? { ...this.controlLease } : null,
      projectPublishCount: this.projectPublishCount,
      externalConsumerInputCount: this.externalConsumerInputCount,
      lastFrame: cloneFrame(this.lastFrame),
      failClosedReason: this.failClosedReason,
    };
  }

  private preparePhysicalPassThroughFrom(source: PhysicalSourceIdentity): GamepadControlSnapshot {
    this.phase = 'target-ready';
    this.endpoint = {
      identity: source.physicalEndpointIdentity,
      kind: 'physical',
      transport: 'physical-hid',
      persona: 'physical',
      endpointEpoch: this.nextEndpointEpoch(),
      writer: 'none',
      sourceIdentity: source.identity,
    };
    this.antiEchoVerified = true;
    this.consumerReceipts.clear();
    this.failClosedReason = null;
    return this.snapshot();
  }

  private nextEndpointEpoch(): number {
    this.endpointEpoch += 1;
    return this.endpointEpoch;
  }

  private requireSelectedSource(): PhysicalSourceIdentity {
    if (!this.selectedSource) throw new Error('selected-physical-source-required');
    const source = this.sources.find((item) => item.identity === this.selectedSource?.sourceIdentity);
    if (!source || source.state !== 'physical-selected'
      || source.generation !== this.selectedSource.sourceGeneration
      || source.physicalEndpointIdentity !== this.selectedSource.physicalEndpointIdentity) {
      throw new Error('selected-physical-source-stale');
    }
    return source;
  }

  private sameSelectedSource(frame: LogicalFrame): boolean {
    return !!this.selectedSource
      && frame.epoch === this.epoch
      && frame.sourceIdentity === this.selectedSource.sourceIdentity
      && frame.sourceGeneration === this.selectedSource.sourceGeneration;
  }

  private dropFrame(frame: LogicalFrame, reason: string): FramePublication {
    this.failClosedReason = reason;
    return { accepted: false, disposition: 'drop', frame: null };
  }

  private releaseEndpointToHold(reason: string): void {
    this.mode = 'hold';
    this.phase = 'safe-zero';
    this.endpoint = null;
    this.pendingPersona = null;
    this.neutralBarrier = false;
    this.antiEchoVerified = false;
    this.consumerEvidenceReady = false;
    this.controlLease = null;
    this.consumerReceipts.clear();
    this.failClosedReason = reason;
  }

  private failClosed(reason: string): GamepadControlSnapshot {
    this.mode = 'hold';
    this.failClosedReason = reason;
    return this.snapshot();
  }

  private requirePhase(...allowed: GamepadControlPhase[]): void {
    if (!allowed.includes(this.phase)) throw new Error(`invalid-phase:${this.phase}`);
  }
}

/**
 * Narrow coordinator-facing port.  A real HardwareCoordinatorPlan adapter may
 * call only these transaction methods; it must never let YMCC or an endpoint
 * writer call GamepadControl directly.
 */
export interface GamepadControlCoordinatorPort {
  beginGamepadControl(command: GamepadControlCoordinatorCommand): GamepadControlSnapshot;
  endGamepadControl(command: GamepadControlCoordinatorCommand): GamepadControlSnapshot;
}

/** Stage 1 adapter drafts. Implementations must not be supplied by YMCC. */
export interface GamepadControlSourcePort {
  enumeratePhysicalSources(): readonly PhysicalSourceIdentity[];
  readSelectedSource(binding: SourceBinding): LogicalFrameInput | null;
}

export interface GamepadControlOutputPort {
  prepareVirtualEndpoint(endpoint: PublicEndpoint): void;
  submitVirtualNeutral(endpoint: PublicEndpoint, frame: LogicalFrame): void;
  submitVirtualFrame(endpoint: PublicEndpoint, frame: LogicalFrame): void;
  releaseVirtualEndpoint(endpoint: PublicEndpoint): void;
}

export interface GamepadControlConsumerPort {
  consumeYmccProjection(frame: LogicalFrame, semanticAction: string): void;
  observePublicEndpoint(endpoint: PublicEndpoint): ConsumerReceipt | null;
}
