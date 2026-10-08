import type { InputPersona } from './inputContracts';

/** T10 result vocabulary. `active` is reachable only after the first matching frame. */
export type GyroConfigActivationResult =
  | 'accepted-pending'
  | 'quiesced'
  | 'acknowledged'
  | 'active'
  | 'rejected'
  | 'safe-stop'
  | 'release-incomplete';

export type GyroConfigActivationPhase = 'idle' | GyroConfigActivationResult;
export type GyroConfigActivationOwner = 'none' | 'game-consumer' | 'ymcc-frontend';

/** Fields shared by every T10 request, ACK, neutral barrier, and first frame. */
export interface GyroConfigActivationIdentityV1 {
  runId: string;
  requestId: string;
  epoch: number;
  powerGeneration: number;
  owner: GyroConfigActivationOwner;
  targetId: string | null;
  persona: InputPersona;
  configRevision: number;
  configHash: string;
  providerId: string | null;
  physicalIdentity: string | null;
  calibrationId: string | null;
  visibilityTransactionId: string | null;
  sampleSequence: number | null;
  timestampUtc: string;
}

export interface GyroConfigActivationRequestV1 extends GyroConfigActivationIdentityV1 {
  schemaVersion: 1;
  neutralProofId: string | null;
}

export interface GyroConfigHostAckV1 extends GyroConfigActivationIdentityV1 {
  schemaVersion: 1;
  hostInstanceId: string;
  neutralProofId: string;
}

export interface GyroConfigFirstFrameV1 extends GyroConfigActivationIdentityV1 {
  schemaVersion: 1;
  mapped: { x: number; y: number };
  report: { id: number | null; length: number | null; hash: string | null };
}

export interface GyroConfigActivationTraceV1 extends GyroConfigActivationIdentityV1 {
  schemaVersion: 1;
  result: GyroConfigActivationResult;
  event: string;
  reason: string | null;
  neutralProofId: string | null;
  hostInstanceId: string | null;
}

export interface GyroConfigActivationSnapshotV1 {
  schemaVersion: 1;
  phase: GyroConfigActivationPhase;
  request: GyroConfigActivationRequestV1 | null;
  hostInstanceId: string | null;
  lastActiveSampleSequence: number | null;
}

type Outcome = { result: GyroConfigActivationResult; trace: GyroConfigActivationTraceV1 };
const finiteInteger = (value: number | null) => value === null || (Number.isSafeInteger(value) && value >= 0);
const finiteVector = (value: { x: number; y: number }) => Number.isFinite(value.x) && Number.isFinite(value.y);

function validIdentity(value: GyroConfigActivationIdentityV1): boolean {
  return value.runId.length > 0 && value.requestId.length > 0 &&
    Number.isSafeInteger(value.epoch) && value.epoch >= 0 &&
    Number.isSafeInteger(value.powerGeneration) && value.powerGeneration >= 0 &&
    Number.isSafeInteger(value.configRevision) && value.configRevision >= 0 &&
    value.configHash.startsWith('sha256:') && value.configHash.length === 71 &&
    finiteInteger(value.sampleSequence) && value.timestampUtc.length > 0 &&
    value.persona !== 'disabled';
}

function sameGeneration(expected: GyroConfigActivationIdentityV1, candidate: GyroConfigActivationIdentityV1): boolean {
  return expected.runId === candidate.runId &&
    expected.requestId === candidate.requestId &&
    expected.epoch === candidate.epoch &&
    expected.powerGeneration === candidate.powerGeneration &&
    expected.targetId === candidate.targetId &&
    expected.persona === candidate.persona &&
    expected.configRevision === candidate.configRevision &&
    expected.configHash === candidate.configHash &&
    expected.providerId === candidate.providerId &&
    expected.physicalIdentity === candidate.physicalIdentity &&
    expected.calibrationId === candidate.calibrationId &&
    expected.visibilityTransactionId === candidate.visibilityTransactionId;
}

/**
 * Pure T10 coordinator slice. It intentionally owns no process or report
 * handle: a future native Host adapter must feed its neutral proof, exact ACK,
 * and same-generation first frame through this transaction before output is
 * published. This makes stale ACK/frame handling replayable without devices.
 */
export class GyroConfigActivation {
  private phase: GyroConfigActivationPhase = 'idle';
  private request: GyroConfigActivationRequestV1 | null = null;
  private hostInstanceId: string | null = null;
  private lastActiveSampleSequence: number | null = null;
  private readonly traces: GyroConfigActivationTraceV1[] = [];

  requestActivation(request: GyroConfigActivationRequestV1): Outcome {
    if (!validIdentity(request)) return this.reject(request, 'request-invalid');
    this.phase = 'accepted-pending';
    this.request = { ...request };
    this.hostInstanceId = null;
    this.lastActiveSampleSequence = null;
    return this.record('accepted-pending', 'request-accepted', null, request.neutralProofId, null);
  }

  confirmNeutral(requestId: string, neutralProofId: string): Outcome {
    const expected = this.request;
    if (!expected || this.phase !== 'accepted-pending' || requestId !== expected.requestId || !neutralProofId) {
      return this.transitionSafeStop(expected, 'neutral-proof-missing-or-stale');
    }
    this.phase = 'quiesced';
    this.request = { ...expected, neutralProofId };
    return this.record('quiesced', 'neutral-confirmed', null, neutralProofId, null);
  }

  acknowledge(ack: GyroConfigHostAckV1): Outcome {
    const expected = this.request;
    if (!expected || this.phase !== 'quiesced') return this.reject(ack, 'ack-without-quiesce');
    if (!validIdentity(ack) || !ack.hostInstanceId || !ack.neutralProofId || !sameGeneration(expected, ack) || ack.neutralProofId !== expected.neutralProofId) {
      // A stale ACK must not destroy a newer saved-pending request. The UI
      // retains it; this trace gives the stale Host a deterministic rejection.
      return this.reject(ack, 'stale-ack-rejected');
    }
    this.phase = 'acknowledged';
    this.hostInstanceId = ack.hostInstanceId;
    return this.record('acknowledged', 'host-acknowledged', null, ack.neutralProofId, ack.hostInstanceId);
  }

  publishFirstFrame(frame: GyroConfigFirstFrameV1): Outcome {
    const expected = this.request;
    if (!expected || this.phase !== 'acknowledged') return this.transitionSafeStop(expected, 'first-frame-before-ack');
    const sampleValid = frame.sampleSequence !== null && frame.sampleSequence >= 0 &&
      finiteVector(frame.mapped) && frame.report.length !== null && frame.report.length > 0 && !!frame.report.hash;
    if (!validIdentity(frame) || !sameGeneration(expected, frame) || !sampleValid) {
      return this.transitionSafeStop(expected, 'first-frame-generation-or-data-invalid');
    }
    this.phase = 'active';
    this.lastActiveSampleSequence = frame.sampleSequence;
    return this.record('active', 'first-same-generation-frame', null, expected.neutralProofId, this.hostInstanceId);
  }

  safeStop(reason: string): Outcome {
    return this.transitionSafeStop(this.request, reason);
  }

  snapshot(): GyroConfigActivationSnapshotV1 {
    return {
      schemaVersion: 1,
      phase: this.phase,
      request: this.request ? { ...this.request } : null,
      hostInstanceId: this.hostInstanceId,
      lastActiveSampleSequence: this.lastActiveSampleSequence,
    };
  }

  trace(): readonly GyroConfigActivationTraceV1[] {
    return this.traces.map((item) => ({ ...item }));
  }

  private reject(identity: GyroConfigActivationIdentityV1, reason: string): Outcome {
    return this.recordWithIdentity(identity, 'rejected', 'request-rejected', reason, null, null);
  }

  private transitionSafeStop(expected: GyroConfigActivationRequestV1 | null, reason: string): Outcome {
    if (expected) {
      this.phase = 'safe-stop';
      this.hostInstanceId = null;
      this.lastActiveSampleSequence = null;
      return this.record('safe-stop', 'publication-blocked', reason, expected.neutralProofId, null);
    }
    const missing: GyroConfigActivationIdentityV1 = {
      runId: 'unbound', requestId: 'unbound', epoch: 0, powerGeneration: 0, owner: 'none', targetId: null,
      persona: 'disabled', configRevision: 0, configHash: 'sha256:'.padEnd(71, '0'), providerId: null,
      physicalIdentity: null, calibrationId: null, visibilityTransactionId: null, sampleSequence: null,
      timestampUtc: new Date(0).toISOString(),
    };
    this.phase = 'safe-stop';
    return this.recordWithIdentity(missing, 'safe-stop', 'publication-blocked', reason, null, null);
  }

  private record(
    result: GyroConfigActivationResult,
    event: string,
    reason: string | null,
    neutralProofId: string | null,
    hostInstanceId: string | null,
  ): Outcome {
    if (!this.request) throw new Error('activation-trace-without-request');
    return this.recordWithIdentity(this.request, result, event, reason, neutralProofId, hostInstanceId);
  }

  private recordWithIdentity(
    identity: GyroConfigActivationIdentityV1,
    result: GyroConfigActivationResult,
    event: string,
    reason: string | null,
    neutralProofId: string | null,
    hostInstanceId: string | null,
  ): Outcome {
    const trace: GyroConfigActivationTraceV1 = {
      schemaVersion: 1, ...identity, result, event, reason, neutralProofId, hostInstanceId,
    };
    this.traces.push(trace);
    return { result, trace };
  }
}
