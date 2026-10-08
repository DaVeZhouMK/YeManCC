import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX,
  HC_XBOX_ROG_ALLY_X_GYRO_MATRIX,
  applyHcAxisMatrix,
  createMotionSample,
  evaluateMotionPersona,
  processPairedMotion,
  type MotionSampleInputV1,
  type ProviderCapabilityMatrixV1,
} from '../src/bridge/motionSample';

const capability: ProviderCapabilityMatrixV1 = {
  schemaVersion: 1, providerId: 'fixture-imu', sensorFamily: 'windows', ingressTransform: 'hc-windows-threshold-then-matrix', pairingPolicyId: 'fixture-atomic-v1',
  pairingStrategy: 'provider-atomic', timestampSource: 'fixture-tick', allowedSkewMs: 0, pairProofRequired: true,
};
const input: MotionSampleInputV1 = {
  schemaVersion: 1, runId: 't12-run', epoch: 22, powerGeneration: 5, providerId: 'fixture-imu',
  physicalIdentity: 'imu:fixture', configRevision: 44, configHash: 'sha256:'.padEnd(71, 'b'),
  sampleSequence: 313, timestampUtc: '2026-09-04T12:20:00.000Z',
  gyroRaw: { x: 10, y: 20, z: 2500 }, gyroUnit: 'deg/s', gyroTimestampUtc: '2026-09-04T12:20:00.000Z',
  accelRaw: { x: 0.1, y: 0.2, z: 9.2 }, accelUnit: 'g', accelTimestampUtc: '2026-09-04T12:20:00.000Z',
  pairingPolicyId: 'fixture-atomic-v1', calibrationEpoch: 7,
  gyroMatrix: HC_XBOX_ROG_ALLY_X_GYRO_MATRIX, accelMatrix: HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX, gyroThreshold: 2000,
  pairProof: {
    schemaVersion: 1, runId: 't12-run', epoch: 22, pairingPolicyId: 'fixture-atomic-v1', providerId: 'fixture-imu', physicalIdentity: 'imu:fixture',
    gyroSourceIdentity: 'sensor:fixture:gyro', accelSourceIdentity: 'sensor:fixture:accel', gyroUnit: 'deg/s', accelUnit: 'g',
    gyroMatrixId: HC_XBOX_ROG_ALLY_X_GYRO_MATRIX.id, gyroMatrixHash: HC_XBOX_ROG_ALLY_X_GYRO_MATRIX.sourceSha256,
    accelMatrixId: HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX.id, accelMatrixHash: HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX.sourceSha256,
    hardwareFrameId: 'fixture-frame-313', gyroSequence: 313, accelSequence: 313,
    monotonicTimeSource: 'fixture-tick', monotonicTimeUnit: 'ms', gyroMonotonicTimestamp: 12000, accelMonotonicTimestamp: 12000,
    calibrationEpoch: 7, powerGeneration: 5, sensorAbi: 'fixture-imu-abi-v1',
    sampleHash: 'sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA',
  },
};

const directGyro = applyHcAxisMatrix({ x: 10, y: 20, z: 30 }, HC_XBOX_ROG_ALLY_X_GYRO_MATRIX);
const directAccel = applyHcAxisMatrix({ x: 0.1, y: 0.2, z: 9.2 }, HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX);
if (directGyro.x !== 10 || directGyro.y !== 30 || directGyro.z !== -20) throw new Error('T12 ROG Ally X gyro matrix diverges from HC');
if (directAccel.x !== -0.1 || directAccel.y !== -9.2 || directAccel.z !== 0.2) throw new Error('T12 ROG Ally X accel matrix diverges from HC');

const paired = createMotionSample(input, capability);
if (!paired.ok) throw new Error(`T12 same-generation fixture did not admit: ${paired.reason}`);
if (paired.sample.gyroInput.x !== 10 || paired.sample.gyroInput.y !== 0 || paired.sample.gyroInput.z !== -20) {
  throw new Error('T12 applied gyro threshold or matrix in the wrong HC order');
}
if (paired.sample.accelInput.x !== -0.1 || paired.sample.accelInput.y !== -9.2 || paired.sample.accelInput.z !== 0.2) {
  throw new Error('T12 incorrectly applied gyro threshold to accel or reused the gyro matrix');
}
let portInput: unknown = null;
const output = processPairedMotion(paired.sample, {
  processMotion(value) {
    portInput = value;
    return { gyroCalibrated: value.gyro, accelGravity: value.accel, gyroDsu: value.gyro, accelDsu: value.accel };
  },
});
if (!portInput || JSON.stringify(output.gyroCalibrated) !== JSON.stringify(paired.sample.gyroInput) || JSON.stringify(output.accelGravity) !== JSON.stringify(paired.sample.accelInput)) {
  throw new Error('T12 did not pass the exact paired and separately remapped inputs to GamepadMotion');
}

for (const [name, candidate, expected] of [
  ['pair-proof', { ...input, pairProof: null }, 'sample-pair-unproven'],
  ['pair-policy', { ...input, pairProof: { ...input.pairProof!, pairingPolicyId: 'other-policy' } }, 'sample-pair-unproven'],
  ['pair-epoch', { ...input, pairProof: { ...input.pairProof!, epoch: 21 } }, 'sample-pair-unproven'],
  ['pair-sequence', { ...input, pairProof: { ...input.pairProof!, accelSequence: 312 } }, 'sample-pair-unproven'],
  ['pair-calibration-epoch', { ...input, calibrationEpoch: 8 }, 'sample-pair-unproven'],
  ['pair-negative-monotonic-time', { ...input, pairProof: { ...input.pairProof!, gyroMonotonicTimestamp: -1 } }, 'sample-pair-unproven'],
  ['pair-matrix', { ...input, pairProof: { ...input.pairProof!, gyroMatrixHash: 'sha256:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB' } }, 'sample-pair-unproven'],
  ['matrix-custom-id', { ...input, gyroMatrix: { ...HC_XBOX_ROG_ALLY_X_GYRO_MATRIX, id: 'unverified-rog-gyro' }, pairProof: { ...input.pairProof!, gyroMatrixId: 'unverified-rog-gyro' } }, 'matrix-unverified'],
  ['matrix-output-mutated', { ...input, accelMatrix: { ...HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX, output: [['x', -1], ['z', 1], ['y', 1]] }, pairProof: { ...input.pairProof!, accelMatrixHash: HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX.sourceSha256 } }, 'matrix-unverified'],
  ['matrix-malformed', { ...input, gyroMatrix: null as unknown as typeof HC_XBOX_ROG_ALLY_X_GYRO_MATRIX }, 'matrix-unverified'],
  ['threshold-below-hc-minimum', { ...input, gyroThreshold: 123 }, 'sample-invalid'],
  ['sample-schema', { ...input, schemaVersion: 2 as unknown as 1 }, 'sample-invalid'],
  ['config-hash', { ...input, configHash: 'sha256:not-a-canonical-hash' }, 'sample-invalid'],
  ['missing-accel', { ...input, accelRaw: null }, 'sample-invalid'],
] as const) {
  const result = createMotionSample(candidate, capability);
  if (result.ok || result.reason !== expected) throw new Error(`T12 ${name} did not SAFE_STOP as ${expected}`);
}

const serialIngress = createMotionSample(input, {
  ...capability,
  sensorFamily: 'serial-usb-imu',
  ingressTransform: 'provider-pretransformed',
});
if (serialIngress.ok || serialIngress.reason !== 'provider-ingress-unverified') {
  throw new Error('T12 let a non-Windows provider reuse the Windows threshold/matrix branch');
}
// Custom user threshold gate (same semantic as native hcClip at main.cpp:9419-9421):
// raw >= gyroThreshold is zeroed before the matrix; raw < threshold survives.
const customGateInput: MotionSampleInputV1 = { ...input, gyroThreshold: 900, gyroRaw: { x: 899, y: 900, z: 901 } };
const customGate = createMotionSample(customGateInput, capability);
if (!customGate.ok) throw new Error(`T12 custom threshold gate did not admit: ${customGate.reason}`);
const gateExpected = applyHcAxisMatrix({ x: 899, y: 0, z: 0 }, HC_XBOX_ROG_ALLY_X_GYRO_MATRIX); // y=900 >= 900 -> 0, z=901 >= 900 -> 0
if (customGate.sample.gyroInput.x !== gateExpected.x || customGate.sample.gyroInput.y !== gateExpected.y || customGate.sample.gyroInput.z !== gateExpected.z) {
  throw new Error(`T12 custom threshold gate mismatch got=${JSON.stringify(customGate.sample.gyroInput)} want=${JSON.stringify(gateExpected)}`);
}
const malformedCapability = createMotionSample(input, { ...capability, allowedSkewMs: -1 });
if (malformedCapability.ok || malformedCapability.reason !== 'sample-pair-unproven') {
  throw new Error('T12 accepted a negative monotonic skew policy');
}

const x360Imu = evaluateMotionPersona({ persona: 'xbox360', outputMode: 'ds4-imu', descriptorVerified: false, mappedGyroXY: { x: 0, y: 0 }, rawDsu: { gyro: { x: 0, y: 0, z: 0 }, accel: { x: 0, y: 0, z: 1 } } });
if (x360Imu.ok || x360Imu.reason !== 'persona-motion-unsupported') throw new Error('T12 allowed DS4 IMU fields for X360');
const ds4NoDescriptor = evaluateMotionPersona({ persona: 'dualshock4', outputMode: 'ds4-imu', descriptorVerified: false, mappedGyroXY: { x: 0, y: 0 }, rawDsu: { gyro: { x: 0, y: 0, z: 0 }, accel: { x: 0, y: 0, z: 1 } } });
if (ds4NoDescriptor.ok || ds4NoDescriptor.reason !== 'descriptor-unverified') throw new Error('T12 encoded an unverified HIDMaestro DS4 IMU descriptor');
const ds4DescriptorOnly = evaluateMotionPersona({ persona: 'dualshock4', outputMode: 'ds4-imu', descriptorVerified: true, mappedGyroXY: { x: 0, y: 0 }, rawDsu: { gyro: { x: 0, y: 0, z: 0 }, accel: { x: 0, y: 0, z: 1 } } });
if (ds4DescriptorOnly.ok || ds4DescriptorOnly.reason !== 'direct-imu-path-unresolved') throw new Error('T12 admitted DS4 IMU from a descriptor claim without encoder/readback');
const ds4Stick = evaluateMotionPersona({ persona: 'dualshock4', outputMode: 'virtual-stick', descriptorVerified: false, mappedGyroXY: { x: 0.2, y: -0.2 }, rawDsu: { gyro: { x: 0, y: 0, z: 0 }, accel: { x: 0, y: 0, z: 1 } } });
if (!ds4Stick.ok || ds4Stick.reportSemantic !== 'virtual-stick' || ds4Stick.imuEncoded) throw new Error('T12 mixed DS4 virtual-stick and IMU semantics');

const evidence = {
  evidenceId: 'T12-E01-MOTION-SAMPLE-MOCK-20260904', status: 'MOCK-READY', systemMutation: false,
  contract: '30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md#11',
  assertions: {
    sameGenerationPairRequired: true, monotonicProofBoundsRequired: true, lockedHcMatrixRequired: true, auditedWindowsIngressRequired: true, separateRogMatrices: true, gyroOnlyThreshold: true,
    fusionPortReceivesPairedSample: true, x360HasNoImu: true, hidMaestroDs4DescriptorUnenclosed: true,
    virtualStickAndDs4ImuNotMixed: true,
  },
  sample: paired.sample, output,
};
const evidencePath = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/T12-E01-motion-sample-mock-20260904.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify(evidence, null, 2));
console.log(`motion sample selftest: PASS (${evidencePath})`);
