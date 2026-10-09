import { computeInputConfigHash } from './inputConfigHash';
import { HC_MIN_GYRO_THRESHOLD_DPS } from './hcInputUtils';

export type CalibrationSessionState =
  | 'unbound'
  | 'start-requested'
  | 'collecting'
  | 'candidate-steady'
  | 'lock-requested'
  | 'manual-bound'
  | 'abort-requested'
  | 'timeout-observed'
  | 'invalidated';

export type CalibrationEvent =
  | 'calibrate-start'
  | 'calibrate-lock'
  | 'calibrate-abort'
  | 'calibrate-timeout'
  | 'calibrate-provider-incomplete'
  | 'calibrate-offset-invalid'
  | 'calibrate-aborted-motion'
  | 'calibration-invalidated'
  | 'sample-stale';

export interface CalibrationGenerationV1 {
  runId: string;
  epoch: number;
  powerGeneration: number;
  providerId: string;
  physicalIdentity: string;
  unit: 'deg/s';
  gyroMatrixId: string;
  accelMatrixId: string;
  hcMotionAssetId: string;
  gyroThreshold: number;
  timestampUtc: string;
}

export interface CalibrationSampleV1 extends CalibrationGenerationV1 {
  /** Session-local calibration generation; stale samples cannot cross a restart. */
  calibrationEpoch: number;
  sampleSequence: number;
  gyro: { x: number; y: number; z: number } | null;
  accel: { x: number; y: number; z: number } | null;
  confidence: number;
  steady: boolean;
  offset: { x: number; y: number; z: number } | null;
  weight: number | null;
}

export interface ManualCalibrationV1 extends CalibrationGenerationV1 {
  schemaVersion: 1;
  calibrationId: string;
  calibrationEpoch: number;
  mode: 'manual';
  offsetXYZ: { x: number; y: number; z: number };
  weight: number;
  confidence: 1;
  createdAt: string;
  source: 'calibrate-lock';
}

export interface CalibrationTraceV1 {
  schemaVersion: 1;
  event: CalibrationEvent;
  state: CalibrationSessionState;
  runId: string | null;
  epoch: number | null;
  powerGeneration: number | null;
  providerId: string | null;
  physicalIdentity: string | null;
  gyroMatrixId: string | null;
  accelMatrixId: string | null;
  sampleSequence: number | null;
  calibrationEpoch: number | null;
  confidence: number | null;
  steady: boolean | null;
  calibrationId: string | null;
  /** True only while this trace cannot admit a virtual gyro contribution. */
  safeZero: boolean;
  reason: string | null;
}

export interface CalibrationSessionSnapshotV1 {
  schemaVersion: 1;
  state: CalibrationSessionState;
  generation: CalibrationGenerationV1 | null;
  calibration: ManualCalibrationV1 | null;
  calibrationEpoch: number | null;
  lastSampleSequence: number | null;
  gyroContributionAllowed: boolean;
}

const finite = (value: number) => Number.isFinite(value);
const finiteVector = (value: { x: number; y: number; z: number } | null): value is { x: number; y: number; z: number } =>
  !!value && finite(value.x) && finite(value.y) && finite(value.z);

function validGeneration(value: CalibrationGenerationV1): boolean {
  return !!value.runId && Number.isSafeInteger(value.epoch) && value.epoch >= 0 &&
    Number.isSafeInteger(value.powerGeneration) && value.powerGeneration >= 0 &&
    !!value.providerId && !!value.physicalIdentity && value.unit === 'deg/s' &&
    !!value.gyroMatrixId && !!value.accelMatrixId && !!value.hcMotionAssetId &&
    finite(value.gyroThreshold) && value.gyroThreshold >= HC_MIN_GYRO_THRESHOLD_DPS && !!value.timestampUtc;
}

function sameGeneration(expected: CalibrationGenerationV1, sample: CalibrationSampleV1, calibrationEpoch: number): boolean {
  return expected.runId === sample.runId && expected.epoch === sample.epoch &&
    expected.powerGeneration === sample.powerGeneration && expected.providerId === sample.providerId &&
    expected.physicalIdentity === sample.physicalIdentity && expected.unit === sample.unit &&
    expected.gyroMatrixId === sample.gyroMatrixId && expected.accelMatrixId === sample.accelMatrixId &&
    expected.hcMotionAssetId === sample.hcMotionAssetId && expected.gyroThreshold === sample.gyroThreshold &&
    Number.isSafeInteger(sample.calibrationEpoch) && sample.calibrationEpoch >= 0 && sample.calibrationEpoch === calibrationEpoch;
}

/**
 * T11's GamepadMotion-facing calibration sub-state machine. It does not
 * measure stillness itself: `confidence` and `steady` are observations from
 * the locked GamepadMotion ABI. Until a valid Manual calibration is bound,
 * all virtual motion publication is safe-zero while physical base input is
 * intentionally outside this gate.
 */
export class GyroCalibrationSession {
  private state: CalibrationSessionState = 'unbound';
  private generation: CalibrationGenerationV1 | null = null;
  private candidate: CalibrationSampleV1 | null = null;
  private calibration: ManualCalibrationV1 | null = null;
  private lastSampleSequence: number | null = null;
  private calibrationEpoch = 0;
  private readonly traces: CalibrationTraceV1[] = [];

  start(generation: CalibrationGenerationV1): CalibrationSessionSnapshotV1 {
    if (!validGeneration(generation)) {
      this.transitionInvalidation('calibrate-provider-incomplete', null, 'generation-invalid');
      return this.snapshot();
    }
    this.calibrationEpoch += 1;
    this.generation = { ...generation };
    this.candidate = null;
    this.calibration = null;
    this.lastSampleSequence = null;
    this.state = 'start-requested';
    this.record('calibrate-start', null, 'ResetContinuousCalibration + Stillness|SensorFusion');
    this.state = 'collecting';
    return this.snapshot();
  }

  observe(sample: CalibrationSampleV1): CalibrationSessionSnapshotV1 {
    const expected = this.generation;
    if (!expected || !['collecting', 'candidate-steady', 'timeout-observed', 'lock-requested'].includes(this.state)) {
      this.transitionInvalidation('calibration-invalidated', sample, 'sample-without-calibration-session');
      return this.snapshot();
    }
    if (!sameGeneration(expected, sample, this.calibrationEpoch) || !Number.isSafeInteger(sample.sampleSequence) || sample.sampleSequence < 0 ||
        (this.lastSampleSequence !== null && sample.sampleSequence <= this.lastSampleSequence)) {
      this.transitionInvalidation('sample-stale', sample, 'generation-or-sequence-mismatch');
      return this.snapshot();
    }
    this.lastSampleSequence = sample.sampleSequence;
    if (!finiteVector(sample.gyro) || !finiteVector(sample.accel)) {
      this.transitionInvalidation('calibrate-provider-incomplete', sample, 'gyro-or-accel-missing-or-non-finite');
      return this.snapshot();
    }
    if (!finite(sample.confidence) || sample.confidence < 0 || sample.confidence > 1) {
      this.transitionInvalidation('calibrate-offset-invalid', sample, 'confidence-invalid');
      return this.snapshot();
    }
    if (!sample.steady) {
      this.candidate = null;
      this.state = 'collecting';
      this.record('calibrate-aborted-motion', sample, 'GamepadMotion-not-steady');
      return this.snapshot();
    }
    if (sample.confidence === 1 && (!finiteVector(sample.offset) || !finite(sample.weight ?? Number.NaN) || (sample.weight ?? 0) <= 0)) {
      this.transitionInvalidation('calibrate-offset-invalid', sample, 'confidence-1-offset-or-weight-invalid');
      return this.snapshot();
    }
    this.candidate = { ...sample, gyro: { ...sample.gyro }, accel: { ...sample.accel }, offset: sample.offset ? { ...sample.offset } : null };
    this.state = 'candidate-steady';
    if (sample.confidence === 1) {
      this.state = 'lock-requested';
      this.record('calibrate-lock', sample, 'confidence-1-candidate-awaiting-offset-validation');
    }
    return this.snapshot();
  }

  lock(): CalibrationSessionSnapshotV1 {
    const candidate = this.candidate;
    const generation = this.generation;
    if (!generation || !candidate || this.state !== 'lock-requested' || candidate.confidence !== 1 ||
        !candidate.steady || !finiteVector(candidate.offset) || !finite(candidate.weight ?? Number.NaN) || (candidate.weight ?? 0) <= 0) {
      this.transitionInvalidation('calibrate-offset-invalid', candidate, 'manual-lock-preconditions-failed');
      return this.snapshot();
    }
    const provenance = {
      schemaVersion: 1 as const,
      ...generation,
      calibrationEpoch: this.calibrationEpoch,
      offsetXYZ: candidate.offset,
      weight: candidate.weight,
      confidence: 1 as const,
      createdAt: candidate.timestampUtc,
      source: 'calibrate-lock' as const,
    };
    this.calibration = {
      ...provenance,
      calibrationId: `calibration.v1:${computeInputConfigHash(provenance)}`,
      mode: 'manual',
    };
    this.state = 'manual-bound';
    this.record('calibrate-lock', candidate, 'SetCalibrationOffset(confidence*10) + Manual');
    return this.snapshot();
  }

  timeoutObserved(): CalibrationSessionSnapshotV1 {
    if (this.generation && ['collecting', 'candidate-steady', 'timeout-observed'].includes(this.state)) {
      // HC has a five-second UI observation window. The contract deliberately
      // does not promote it to a Manual calibration command.
      this.state = 'timeout-observed';
      this.record('calibrate-timeout', this.candidate, 'safe-zero-continues; no-manual-lock');
      this.state = 'collecting';
    }
    return this.snapshot();
  }

  abort(): CalibrationSessionSnapshotV1 {
    if (this.generation) {
      this.state = 'abort-requested';
      this.record('calibrate-abort', this.candidate, 'user-cancelled');
    }
    this.clearToUnbound();
    return this.snapshot();
  }

  invalidate(reason: 'calibration-invalidated' | 'calibrate-provider-incomplete', sample: CalibrationSampleV1 | null = null): CalibrationSessionSnapshotV1 {
    this.transitionInvalidation(reason, sample, 'external-invalidation');
    return this.snapshot();
  }

  snapshot(): CalibrationSessionSnapshotV1 {
    return {
      schemaVersion: 1,
      state: this.state,
      generation: this.generation ? { ...this.generation } : null,
      calibration: this.calibration ? { ...this.calibration, offsetXYZ: { ...this.calibration.offsetXYZ } } : null,
      calibrationEpoch: this.generation ? this.calibrationEpoch : null,
      lastSampleSequence: this.lastSampleSequence,
      gyroContributionAllowed: this.state === 'manual-bound' && this.calibration !== null,
    };
  }

  trace(): readonly CalibrationTraceV1[] {
    return this.traces.map((item) => ({ ...item }));
  }

  private transitionInvalidation(event: CalibrationEvent, sample: CalibrationSampleV1 | null, reason: string): void {
    this.state = 'invalidated';
    this.record(event, sample, reason);
    this.clearToUnbound();
  }

  private clearToUnbound(): void {
    this.state = 'unbound';
    this.generation = null;
    this.candidate = null;
    this.calibration = null;
    this.lastSampleSequence = null;
  }

  private record(event: CalibrationEvent, sample: CalibrationSampleV1 | null, reason: string | null): void {
    const generation = this.generation;
    this.traces.push({
      schemaVersion: 1, event, state: this.state,
      runId: generation?.runId ?? null, epoch: generation?.epoch ?? null, powerGeneration: generation?.powerGeneration ?? null,
      providerId: generation?.providerId ?? null, physicalIdentity: generation?.physicalIdentity ?? null,
      gyroMatrixId: generation?.gyroMatrixId ?? null, accelMatrixId: generation?.accelMatrixId ?? null,
      sampleSequence: sample?.sampleSequence ?? null, calibrationEpoch: generation ? this.calibrationEpoch : null,
      confidence: sample?.confidence ?? null, steady: sample?.steady ?? null,
      calibrationId: this.calibration?.calibrationId ?? null,
      safeZero: this.state !== 'manual-bound' || this.calibration === null,
      reason,
    });
  }
}
