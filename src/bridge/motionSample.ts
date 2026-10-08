import type { InputPersona } from './inputContracts';
import { HC_MIN_GYRO_THRESHOLD_DPS } from './hcInputUtils';

export type Vec3 = { x: number; y: number; z: number };
export type MotionSampleRejection =
  | 'sample-pair-unproven'
  | 'sample-invalid'
  | 'matrix-unverified'
  | 'provider-ingress-unverified'
  | 'persona-motion-unsupported'
  | 'descriptor-unverified'
  | 'direct-imu-path-unresolved';

export interface HcAxisMatrixV1 {
  schemaVersion: 1;
  id: string;
  source: string;
  /** SHA-256 of the locked HC source file that defines this matrix. */
  sourceSha256: string;
  /** Each output axis identifies its source input axis and sign after HC remap. */
  output: readonly [readonly ['x' | 'y' | 'z', number], readonly ['x' | 'y' | 'z', number], readonly ['x' | 'y' | 'z', number]];
}

// HC Devices/ASUS/XboxROGAllyX.cs at frozen commit 06c0b954:
// Gyro Axis(1, 1, -1), AxisSwap Y<->Z => (x, z, -y).
export const HC_XBOX_ROG_ALLY_X_GYRO_MATRIX: HcAxisMatrixV1 = {
  schemaVersion: 1,
  id: 'hc-06c0b954:xbox-rog-ally-x:gyro:x-z-neg-y',
  source: 'Devices/ASUS/XboxROGAllyX.cs#GyroMatrix',
  sourceSha256: 'sha256:404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE',
  output: [['x', 1], ['z', 1], ['y', -1]],
};

// HC Devices/ASUS/XboxROGAllyX.cs at frozen commit 06c0b954:
// Accel Axis(-1, -1, 1), AxisSwap Y<->Z => (-x, -z, y).
export const HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX: HcAxisMatrixV1 = {
  schemaVersion: 1,
  id: 'hc-06c0b954:xbox-rog-ally-x:accel:neg-x-neg-z-y',
  source: 'Devices/ASUS/XboxROGAllyX.cs#AcceleroMatrix',
  sourceSha256: 'sha256:404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE',
  output: [['x', -1], ['z', -1], ['y', 1]],
};

/**
 * Current audited matrix registry. A MatrixV1 supplied by a producer is only
 * provenance when it exactly matches one entry here; a self-consistent custom
 * id/hash/output tuple must not become a new HC SKU at the motion boundary.
 *
 * The registry intentionally contains only Xbox ROG Ally X because that is the
 * only ROG identity currently represented by a frozen YMCC fixture. HC also
 * has ROG Ally, ROG Ally X, and Xbox ROG Ally matrices, but YMCC has no
 * verified identity-selection path for them yet. Those SKUs therefore remain
 * fail-closed rather than borrowing this matrix.
 */
const LOCKED_HC_AXIS_MATRICES: readonly HcAxisMatrixV1[] = [
  HC_XBOX_ROG_ALLY_X_GYRO_MATRIX,
  HC_XBOX_ROG_ALLY_X_ACCEL_MATRIX,
];

export interface ProviderCapabilityMatrixV1 {
  schemaVersion: 1;
  providerId: string;
  /** Mirrors the locked HC SensorFamily branch declared by the producer. */
  sensorFamily: 'windows' | 'serial-usb-imu' | 'controller';
  /** HC Windows IMU applies threshold→matrix; other adapters need their own proof. */
  ingressTransform: 'hc-windows-threshold-then-matrix' | 'provider-pretransformed';
  pairingPolicyId: string;
  pairingStrategy: 'provider-atomic' | 'coordinator-latched-proven';
  timestampSource: string;
  allowedSkewMs: number | null;
  pairProofRequired: true;
}

/**
 * The indivisible proof required before gyro+accel can enter GamepadMotion.
 * A caller cannot upgrade a pair by setting a boolean: source identities,
 * matrices, monotonic timing, ABI and both sample sequences remain bound.
 */
export interface SamplePairProofV1 {
  schemaVersion: 1;
  runId: string;
  /** Lifecycle epoch must match the MotionSample; proof reuse across reopen is rejected. */
  epoch: number;
  pairingPolicyId: string;
  providerId: string;
  physicalIdentity: string;
  gyroSourceIdentity: string;
  accelSourceIdentity: string;
  gyroUnit: 'deg/s';
  accelUnit: 'g';
  gyroMatrixId: string;
  gyroMatrixHash: string;
  accelMatrixId: string;
  accelMatrixHash: string;
  hardwareFrameId: string;
  gyroSequence: number;
  accelSequence: number;
  monotonicTimeSource: string;
  monotonicTimeUnit: 'ms';
  gyroMonotonicTimestamp: number;
  accelMonotonicTimestamp: number;
  calibrationEpoch: number;
  powerGeneration: number;
  sensorAbi: string;
  sampleHash: string;
}

export interface MotionSampleInputV1 {
  schemaVersion: 1;
  runId: string;
  epoch: number;
  powerGeneration: number;
  providerId: string;
  physicalIdentity: string;
  configRevision: number;
  configHash: string;
  sampleSequence: number;
  timestampUtc: string;
  gyroRaw: Vec3 | null;
  gyroUnit: 'deg/s';
  gyroTimestampUtc: string | null;
  accelRaw: Vec3 | null;
  accelUnit: 'g';
  accelTimestampUtc: string | null;
  pairingPolicyId: string;
  pairProof: SamplePairProofV1 | null;
  /** Active calibration session generation for this exact sample. */
  calibrationEpoch: number;
  gyroMatrix: HcAxisMatrixV1;
  accelMatrix: HcAxisMatrixV1;
  gyroThreshold: number;
}

export interface MotionSampleV1 extends MotionSampleInputV1 {
  gyroInput: Vec3;
  accelInput: Vec3;
}

export interface GamepadMotionOutputV1 {
  gyroCalibrated: Vec3;
  accelGravity: Vec3;
  gyroDsu: Vec3;
  accelDsu: Vec3;
}

export interface GamepadMotionPortV1 {
  processMotion(input: { gyro: Vec3; accel: Vec3; sampleSequence: number }): GamepadMotionOutputV1;
}

export interface MotionPersonaRequestV1 {
  persona: InputPersona;
  outputMode: 'virtual-stick' | 'ds4-imu' | 'disabled';
  descriptorVerified: boolean;
  mappedGyroXY: { x: number; y: number };
  rawDsu: { gyro: Vec3; accel: Vec3 };
}

export type MotionPersonaResult =
  | { ok: true; reportSemantic: 'virtual-stick' | 'ds4-imu' | 'none'; gyroContributionActive: boolean; imuEncoded: false }
  | { ok: false; reason: MotionSampleRejection };

const finite = (value: number) => Number.isFinite(value);
const finiteVec3 = (value: Vec3 | null): value is Vec3 => !!value && finite(value.x) && finite(value.y) && finite(value.z);
const sha256 = (value: unknown): value is string => typeof value === 'string' && /^sha256:[A-Fa-f0-9]{64}$/.test(value);

function sameMatrix(expected: HcAxisMatrixV1, candidate: unknown): boolean {
  if (!candidate || typeof candidate !== 'object') return false;
  const raw = candidate as Partial<HcAxisMatrixV1>;
  if (raw.schemaVersion !== expected.schemaVersion || raw.id !== expected.id || raw.source !== expected.source ||
    raw.sourceSha256 !== expected.sourceSha256 || !Array.isArray(raw.output) || raw.output.length !== expected.output.length) return false;
  return raw.output.every((axis, index) => Array.isArray(axis) && axis.length === 2 &&
    axis[0] === expected.output[index][0] && axis[1] === expected.output[index][1]);
}

/** True only for an exact entry in the local locked-HC matrix registry. */
export function isLockedHcAxisMatrix(matrix: unknown): matrix is HcAxisMatrixV1 {
  return LOCKED_HC_AXIS_MATRICES.some((entry) => sameMatrix(entry, matrix));
}

function hasVerifiedPairProof(input: MotionSampleInputV1, capability: ProviderCapabilityMatrixV1): boolean {
  const proof = input.pairProof;
  const capabilityValid = capability.schemaVersion === 1 && !!capability.providerId && !!capability.pairingPolicyId &&
    !!capability.timestampSource && capability.pairProofRequired === true &&
    (capability.pairingStrategy === 'provider-atomic' || capability.pairingStrategy === 'coordinator-latched-proven') &&
    (capability.allowedSkewMs === null || (finite(capability.allowedSkewMs) && capability.allowedSkewMs >= 0));
  if (!proof || !capabilityValid || proof.schemaVersion !== 1 ||
    proof.runId !== input.runId || proof.epoch !== input.epoch ||
    proof.pairingPolicyId !== capability.pairingPolicyId || proof.pairingPolicyId !== input.pairingPolicyId ||
    proof.providerId !== capability.providerId || proof.providerId !== input.providerId ||
    proof.physicalIdentity !== input.physicalIdentity || proof.powerGeneration !== input.powerGeneration ||
    proof.gyroUnit !== input.gyroUnit || proof.accelUnit !== input.accelUnit ||
    proof.gyroMatrixId !== input.gyroMatrix.id || proof.gyroMatrixHash !== input.gyroMatrix.sourceSha256 ||
    proof.accelMatrixId !== input.accelMatrix.id || proof.accelMatrixHash !== input.accelMatrix.sourceSha256 ||
    proof.monotonicTimeSource !== capability.timestampSource || proof.monotonicTimeUnit !== 'ms' ||
    proof.gyroSequence !== input.sampleSequence || proof.accelSequence !== input.sampleSequence ||
    proof.calibrationEpoch !== input.calibrationEpoch) return false;
  if (!proof.runId || !Number.isSafeInteger(proof.epoch) || proof.epoch < 0 ||
    !proof.gyroSourceIdentity || !proof.accelSourceIdentity || !proof.hardwareFrameId || !proof.sensorAbi || !sha256(proof.sampleHash) ||
    !Number.isSafeInteger(proof.calibrationEpoch) || proof.calibrationEpoch < 0 ||
    !Number.isSafeInteger(proof.gyroSequence) || proof.gyroSequence < 0 ||
    !Number.isSafeInteger(proof.accelSequence) || proof.accelSequence < 0 ||
    !finite(proof.gyroMonotonicTimestamp) || proof.gyroMonotonicTimestamp < 0 ||
    !finite(proof.accelMonotonicTimestamp) || proof.accelMonotonicTimestamp < 0) return false;
  const skew = Math.abs(proof.gyroMonotonicTimestamp - proof.accelMonotonicTimestamp);
  return capability.allowedSkewMs === null || skew <= capability.allowedSkewMs;
}

export function applyHcAxisMatrix(raw: Vec3, matrix: HcAxisMatrixV1): Vec3 {
  const source = raw as Record<'x' | 'y' | 'z', number>;
  const [x, y, z] = matrix.output;
  return { x: source[x[0]] * x[1], y: source[y[0]] * y[1], z: source[z[0]] * z[1] };
}

/** Build the one indivisible gyro+accel admission unit required before HC fusion. */
export function createMotionSample(input: MotionSampleInputV1, capability: ProviderCapabilityMatrixV1): { ok: true; sample: MotionSampleV1 } | { ok: false; reason: MotionSampleRejection } {
  const identityValid = !!input.runId && Number.isSafeInteger(input.epoch) && input.epoch >= 0 &&
    Number.isSafeInteger(input.powerGeneration) && input.powerGeneration >= 0 && !!input.providerId && !!input.physicalIdentity &&
    input.schemaVersion === 1 && Number.isSafeInteger(input.configRevision) && input.configRevision >= 0 && sha256(input.configHash) && !!input.pairingPolicyId &&
    Number.isSafeInteger(input.sampleSequence) && input.sampleSequence >= 0 && !!input.timestampUtc &&
    Number.isSafeInteger(input.calibrationEpoch) && input.calibrationEpoch >= 0 &&
    input.gyroUnit === 'deg/s' && input.accelUnit === 'g' && finite(input.gyroThreshold) && input.gyroThreshold >= HC_MIN_GYRO_THRESHOLD_DPS;
  if (!identityValid || !finiteVec3(input.gyroRaw) || !finiteVec3(input.accelRaw) || !input.gyroTimestampUtc || !input.accelTimestampUtc) {
    return { ok: false, reason: 'sample-invalid' };
  }
  // HC SerialUSBIMU and controller providers reach their own raw-vector
  // overloads, so they must not silently inherit the Windows remap/cutoff
  // branch below. Their adapter evidence and transform contract are absent.
  if (capability.sensorFamily !== 'windows' || capability.ingressTransform !== 'hc-windows-threshold-then-matrix') {
    return { ok: false, reason: 'provider-ingress-unverified' };
  }
  if (!isLockedHcAxisMatrix(input.gyroMatrix) || !isLockedHcAxisMatrix(input.accelMatrix)) {
    return { ok: false, reason: 'matrix-unverified' };
  }
  if (!hasVerifiedPairProof(input, capability)) {
    return { ok: false, reason: 'sample-pair-unproven' };
  }
  // HC IMUGyrometer threshold applies to gyro only; IMUAccelerometer does not
  // apply a corresponding 2000-style cutoff.
  const thresholdedGyro: Vec3 = {
    x: Math.abs(input.gyroRaw.x) >= input.gyroThreshold ? 0 : input.gyroRaw.x,
    y: Math.abs(input.gyroRaw.y) >= input.gyroThreshold ? 0 : input.gyroRaw.y,
    z: Math.abs(input.gyroRaw.z) >= input.gyroThreshold ? 0 : input.gyroRaw.z,
  };
  return {
    ok: true,
    sample: {
      ...input,
      gyroRaw: { ...input.gyroRaw }, accelRaw: { ...input.accelRaw },
      gyroInput: applyHcAxisMatrix(thresholdedGyro, input.gyroMatrix),
      accelInput: applyHcAxisMatrix(input.accelRaw, input.accelMatrix),
    },
  };
}

/** The port is never invoked before a proven pair has been constructed. */
export function processPairedMotion(sample: MotionSampleV1, port: GamepadMotionPortV1): GamepadMotionOutputV1 {
  return port.processMotion({ gyro: { ...sample.gyroInput }, accel: { ...sample.accelInput }, sampleSequence: sample.sampleSequence });
}

/**
 * Persona boundary only. A descriptor claim is not an encoder, independent
 * decoder/readback, or consumer observation. Until the complete T12 gate is
 * wired to a real DS4 report path, this helper must return no-report.
 */
export function evaluateMotionPersona(request: MotionPersonaRequestV1): MotionPersonaResult {
  if (request.outputMode === 'disabled') return { ok: true, reportSemantic: 'none', gyroContributionActive: false, imuEncoded: false };
  if (request.persona === 'disabled') return { ok: false, reason: 'persona-motion-unsupported' };
  if (request.persona === 'xbox360') {
    if (request.outputMode === 'ds4-imu') return { ok: false, reason: 'persona-motion-unsupported' };
    return { ok: true, reportSemantic: 'virtual-stick', gyroContributionActive: true, imuEncoded: false };
  }
  if (request.outputMode === 'virtual-stick') return { ok: true, reportSemantic: 'virtual-stick', gyroContributionActive: true, imuEncoded: false };
  // A missing descriptor fails at the metadata boundary. A claimed descriptor
  // alone still cannot admit a DS4 IMU report: YMCC has no connected encoder,
  // independent decoder/readback, or admitted consumer path for it.
  if (!request.descriptorVerified) return { ok: false, reason: 'descriptor-unverified' };
  return { ok: false, reason: 'direct-imu-path-unresolved' };
}
