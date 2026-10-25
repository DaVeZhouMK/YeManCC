import type { FanState } from './fanApi';

export interface FanSessionEvidenceContext {
  powerGeneration: number;
  initialAwakeGeneration: number | null;
  /** Recorded only after this renderer's authenticated Open/OpenEvents completed. */
  explicitOpenGeneration: number | null;
}
export interface FanSnapshotGenerationEvidence {
  generation: number | null;
  freshness: 'current' | 'stale' | 'future' | 'missing' | 'invalid';
  source: 'host-receipt' | 'initial-awake' | 'explicit-open' | 'legacy';
}

/** A zero power-operation receipt is not a sleep cycle. It may be bound only
 * to native's verified initial-awake baseline or this exact HC session's
 * completed Open/OpenEvents. Neither path creates a resume/physical receipt. */
export function isNoPowerOperationReceipt(remote: FanState): boolean {
  return remote.resumePhaseGeneration === 0 && remote.powerOperationGeneration === 0
    && remote.powerOperationAttempt === 0 && (remote.powerOperationStatus === null || remote.powerOperationStatus === undefined)
    && remote.powerState === 'Unknown';
}
/** A never-opened authenticated Host has no prior power operation to resume.
 * This admits only the original Open/OpenEvents chain, never a control write. */
export function isColdUnopenedFanHost(remote: FanState): boolean {
  const idlePhase = (remote.state === 'AwaitingControl' && remote.resumePhase === 'AwaitingControl'
      && remote.controlAccepting === true)
    || (remote.state === 'Stopped' && remote.resumePhase === 'Stopped' && remote.controlAccepting === false);
  return idlePhase && isNoPowerOperationReceipt(remote) && remote.fanCapabilitySupported === true
    && remote.openCalled === false && remote.openEventsCalled === false && remote.closeCalled === false
    && remote.hardwareWritesEnabled === false && remote.hardwareWritesObserved === false && remote.lease === null
    && remote.unknownState === false && remote.hcCloseCleanupPending === false;
}
export function isCompleteAdmissibleHcSession(remote: FanState): boolean {
  return (remote.state === 'Ready' || remote.state === 'AwaitingControl')
    && (remote.resumePhase === 'Ready' || remote.resumePhase === 'AwaitingControl')
    && remote.fanCapabilitySupported === true && remote.controlAccepting === true
    && remote.openCalled === true && remote.openEventsCalled === true
    && remote.unknownState === false && remote.hcCloseCleanupPending === false;
}
export function resolveFanSnapshotGeneration(
  remote: FanState, context: FanSessionEvidenceContext,
): FanSnapshotGenerationEvidence {
  return resolveFanSnapshotGenerationValues(
    remote, context.powerGeneration, context.initialAwakeGeneration, context.explicitOpenGeneration,
  );
}

/** Hot-path form for FanHost: avoid allocating a context object per snapshot. */
export function resolveFanSnapshotGenerationValues(
  remote: FanState,
  powerGeneration: number,
  initialAwakeGeneration: number | null,
  explicitOpenGeneration: number | null,
): FanSnapshotGenerationEvidence {
  const phase = remote.resumePhaseGeneration;
  const operation = remote.powerOperationGeneration;
  // Keep the two-field contract explicit without allocating an array for every
  // state/admission check. Undefined means the field is absent; null remains
  // present and therefore invalid, matching the previous Number.isSafeInteger
  // classification.
  const phasePresent = phase !== undefined;
  const operationPresent = operation !== undefined;
  const presentCount = (phasePresent ? 1 : 0) + (operationPresent ? 1 : 0);
  const phaseInvalid = phasePresent && (!Number.isSafeInteger(phase) || Number(phase) < 0);
  const operationInvalid = operationPresent && (!Number.isSafeInteger(operation) || Number(operation) < 0);
  if (phaseInvalid || operationInvalid || (presentCount === 2 && phase !== operation)) {
    return { generation: null, freshness: 'invalid', source: 'host-receipt' };
  }
  if (presentCount === 0) return { generation: null, freshness: 'missing', source: 'legacy' };
  let generation = Number(phasePresent ? phase : operation);
  let source: FanSnapshotGenerationEvidence['source'] = 'host-receipt';
  if (generation === 0 && isNoPowerOperationReceipt(remote) && isCompleteAdmissibleHcSession(remote)) {
    if (explicitOpenGeneration === powerGeneration) {
      generation = powerGeneration; source = 'explicit-open';
    } else if (powerGeneration === 1 && initialAwakeGeneration === 1) {
      generation = 1; source = 'initial-awake';
    }
  }
  return { generation, source, freshness: generation === powerGeneration ? 'current'
    : generation < powerGeneration ? 'stale' : 'future' };
}
export function hasBlockingFanTransition(remote: FanState): boolean {
  return remote.state === 'Resuming' || remote.resumePhase === 'Resuming'
    || remote.powerState === 'Resuming' || remote.powerOperationStatus === 'pending'
    || remote.powerOperationStatus === 'executing';
}
export function hasFailedFanOperation(remote: FanState): boolean {
  // Explicit OFF cancels that older resume; it is not a fault preventing a
  // later real manual Open. Its waiter still receives superseded from Host.
  return remote.powerOperationStatus === 'failed'
    || (remote.powerOperationStatus === 'superseded' && remote.powerOperationOutcome !== 'user-cancelled');
}
