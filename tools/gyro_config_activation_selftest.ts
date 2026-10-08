import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { computeInputConfigHash } from '../src/bridge/inputConfigHash';
import {
  GyroConfigActivation,
  type GyroConfigActivationRequestV1,
  type GyroConfigFirstFrameV1,
  type GyroConfigHostAckV1,
} from '../src/bridge/gyroConfigActivation';

const baseInput = {
  schemaVersion: 1,
  revision: 41,
  outputTarget: { schemaVersion: 1, revision: 41, closure: 'UNENCLOSED', persona: 'dualshock4', descriptorHash: null, buttonMappingEnabled: true, gyroEnabled: true, visibilityPolicy: 'project-only' },
  buttonMapping: { schemaVersion: 1, revision: 41, closure: 'UNENCLOSED', profileId: 'default', rules: {} },
  gyroMotion: { schemaVersion: 1, revision: 41, closure: 'UNENCLOSED', enabled: true, provider: 'windows-imu', unit: 'deg/s', calibrationId: 'cal-41', outputMode: 'virtual-stick', motionInput: 'local-space', motionMode: 'on', gyroMultiplier: 1, accelerometerMultiplier: 1, gyroThreshold: 2000, motionSensitivityX: 1, motionSensitivityY: 1, gyroWeight: 1.2, innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 15, invertHorizontal: false, invertVertical: false },
  ownership: { schemaVersion: 1, mode: 'ymcc-semantic' },
  diagnostics: { inputLoggingEnabled: false },
  appliedRevision: 40,
  appliedConfigHash: 'sha256:'.padEnd(71, 'a'),
  pendingConfigHash: null,
  hostAcknowledgedRevision: null,
  hostAcknowledgedConfigHash: null,
  applyStatus: 'active',
};

const stableHash = computeInputConfigHash(baseInput);
const reorderedHash = computeInputConfigHash({ diagnostics: { inputLoggingEnabled: false }, ...baseInput });
const changedHash = computeInputConfigHash({ ...baseInput, gyroMotion: { ...baseInput.gyroMotion, gyroMultiplier: 1.1 } });
if (!stableHash.startsWith('sha256:') || stableHash.length !== 71) throw new Error('T10 config hash is not a SHA-256 fingerprint');
if (stableHash !== reorderedHash) throw new Error('T10 canonical config hash depends on key order');
if (stableHash === changedHash) throw new Error('T10 config hash ignored a complete input configuration field');
if (stableHash !== computeInputConfigHash({ ...baseInput, applyStatus: 'safe-stop', appliedRevision: null, appliedConfigHash: null })) {
  throw new Error('T10 config hash included Host apply bookkeeping');
}
// 2026-09-09: steeringAxis / outputShape / deadzone fields joined the durable
// schema and are consumed by native. T10 admission must be sensitive to them so
// a change re-enters the activation transaction instead of being skipped.
for (const mutation of [
  { steeringAxis: 'yaw' },
  { outputShape: 'circle' },
  { innerDeadzone: 5 },
  { antiDeadzone: 20 },
  { velocityMode: 'velocity' },
  { outputAxis: 'x' },
  { responseCurvePoints: [[0, 0], [0.5, 1], [1, 1]] },
] as const) {
  const mutatedHash = computeInputConfigHash({ ...baseInput, gyroMotion: { ...baseInput.gyroMotion, ...mutation } });
  if (mutatedHash === stableHash) throw new Error(`T10 config hash ignored new field ${JSON.stringify(mutation)}`);
}

const request: GyroConfigActivationRequestV1 = {
  schemaVersion: 1,
  runId: 't10-run', requestId: 't10-request-41', epoch: 12, powerGeneration: 7,
  owner: 'game-consumer', targetId: 'target-ds4-1', persona: 'dualshock4',
  configRevision: 41, configHash: stableHash, providerId: 'windows-imu', physicalIdentity: 'imu:fixture',
  calibrationId: 'cal-41', visibilityTransactionId: null, sampleSequence: null,
  timestampUtc: '2026-09-04T12:00:00.000Z', neutralProofId: null,
};
const activation = new GyroConfigActivation();
if (activation.requestActivation(request).result !== 'accepted-pending') throw new Error('T10 request was not accepted-pending');
if (activation.confirmNeutral(request.requestId, 'neutral-41').result !== 'quiesced') throw new Error('T10 neutral barrier did not quiesce');

const staleAck: GyroConfigHostAckV1 = { ...request, schemaVersion: 1, configHash: changedHash, hostInstanceId: 'host-old', neutralProofId: 'neutral-41' };
if (activation.acknowledge(staleAck).trace.reason !== 'stale-ack-rejected' || activation.snapshot().phase !== 'quiesced') {
  throw new Error('T10 stale ACK was not rejected without preserving the current request');
}
const ack: GyroConfigHostAckV1 = { ...request, schemaVersion: 1, hostInstanceId: 'host-41', neutralProofId: 'neutral-41' };
if (activation.acknowledge(ack).result !== 'acknowledged') throw new Error('T10 exact Host ACK did not enter acknowledged');
if (activation.snapshot().phase === 'active') throw new Error('T10 marked active before a same-generation frame');
const firstFrame: GyroConfigFirstFrameV1 = {
  ...request,
  schemaVersion: 1, sampleSequence: 9001,
  mapped: { x: 0.125, y: -0.25 }, report: { id: 1, length: 64, hash: 'report-41-9001' },
};
if (activation.publishFirstFrame(firstFrame).result !== 'active' || activation.snapshot().lastActiveSampleSequence !== 9001) {
  throw new Error('T10 first same-generation frame did not activate the configuration');
}

const nextRequest: GyroConfigActivationRequestV1 = {
  ...request, requestId: 't10-request-42', epoch: 13, configRevision: 42,
  configHash: changedHash, calibrationId: 'cal-42', timestampUtc: '2026-09-04T12:01:00.000Z',
};
if (activation.requestActivation(nextRequest).result !== 'accepted-pending' || activation.confirmNeutral(nextRequest.requestId, 'neutral-42').result !== 'quiesced') {
  throw new Error('T10 second configuration did not revoke and quiesce');
}
if (activation.acknowledge({ ...nextRequest, schemaVersion: 1, hostInstanceId: 'host-42', neutralProofId: 'neutral-42' }).result !== 'acknowledged') {
  throw new Error('T10 second Host ACK did not bind');
}
const staleFrame = activation.publishFirstFrame({ ...firstFrame, epoch: 12, configRevision: 41, configHash: stableHash });
if (staleFrame.result !== 'safe-stop' || activation.snapshot().phase !== 'safe-stop') {
  throw new Error('T10 stale first frame did not safe-stop publication');
}

const evidence = {
  evidenceId: 'T10-E01-GYRO-CONFIG-ACTIVATION-MOCK-20260904',
  status: 'MOCK-READY',
  systemMutation: false,
  contract: '30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md#9',
  assertions: {
    canonicalFullConfigHash: true,
    applyBookkeepingExcluded: true,
    durableRequestIsPending: true,
    neutralBeforeHostAck: true,
    staleAckRejected: true,
    exactAckDoesNotApply: true,
    sameGenerationFirstFrameRequired: true,
    staleFrameSafeStops: true,
  },
  trace: activation.trace(),
};
const evidencePath = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/T10-E01-gyro-config-activation-mock-20260904.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify(evidence, null, 2));
console.log(`gyro config activation selftest: PASS (${evidencePath})`);
