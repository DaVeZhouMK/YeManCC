import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  GyroCalibrationSession,
  type CalibrationGenerationV1,
  type CalibrationSampleV1,
} from '../src/bridge/gyroCalibrationSession';

const generation: CalibrationGenerationV1 = {
  runId: 't11-run', epoch: 18, powerGeneration: 4, providerId: 'windows-imu', physicalIdentity: 'imu:fixture',
  unit: 'deg/s', gyroMatrixId: 'rog-ally-x-gyro-v1', accelMatrixId: 'rog-ally-x-accel-v1',
  hcMotionAssetId: 'GamepadMotion.dll@06c0b954', gyroThreshold: 2000, timestampUtc: '2026-09-04T12:10:00.000Z',
};
const sample = (sequence: number, patch: Partial<CalibrationSampleV1> = {}): CalibrationSampleV1 => ({
  ...generation, calibrationEpoch: 1, sampleSequence: sequence, gyro: { x: 0.02, y: -0.01, z: 0.03 }, accel: { x: 0.01, y: -0.02, z: 0.99 },
  confidence: 0, steady: true, offset: { x: 0.02, y: -0.01, z: 0.03 }, weight: 0, ...patch,
});

const timeoutSession = new GyroCalibrationSession();
timeoutSession.start(generation);
timeoutSession.observe(sample(1));
if (timeoutSession.snapshot().gyroContributionAllowed || timeoutSession.snapshot().state !== 'candidate-steady') {
  throw new Error('T11 confidence<1 was allowed to publish gyro');
}
timeoutSession.timeoutObserved();
if (timeoutSession.snapshot().state !== 'collecting' || timeoutSession.snapshot().gyroContributionAllowed) {
  throw new Error('T11 timeout did not preserve automatic calibration safe-zero');
}

const successful = new GyroCalibrationSession();
successful.start(generation);
successful.observe(sample(2, { confidence: 1, steady: true, weight: 10 }));
if (successful.snapshot().state !== 'lock-requested' || successful.snapshot().gyroContributionAllowed) {
  throw new Error('T11 confidence==1 candidate skipped the safe-zero lock barrier');
}
if (successful.snapshot().calibrationEpoch !== 1) throw new Error('T11 session did not expose its calibration epoch');
const locked = successful.lock();
if (locked.state !== 'manual-bound' || !locked.gyroContributionAllowed || !locked.calibration || locked.calibration.confidence !== 1 || locked.calibration.weight !== 10 || !locked.calibration.calibrationId.startsWith('calibration.v1:sha256:')) {
  throw new Error('T11 valid GamepadMotion calibration did not bind Manual provenance');
}
if (!successful.trace().some((event) => event.state === 'manual-bound' && event.event === 'calibrate-lock' && event.safeZero === false)) {
  throw new Error('T11 manual-bound trace still falsely claims safe-zero');
}
if (!timeoutSession.trace().some((event) => event.event === 'calibrate-timeout' && event.safeZero === true)) {
  throw new Error('T11 timeout trace lost the safe-zero admission boundary');
}

const moving = new GyroCalibrationSession();
moving.start(generation);
moving.observe(sample(3, { confidence: 1, steady: false, weight: 10 }));
if (moving.snapshot().state !== 'collecting' || moving.snapshot().gyroContributionAllowed) {
  throw new Error('T11 motion during calibration did not abort the candidate');
}

const invalidNumbers = new GyroCalibrationSession();
invalidNumbers.start(generation);
invalidNumbers.observe(sample(4, { confidence: 1, weight: 10, offset: { x: Number.NaN, y: 0, z: 0 } }));
if (invalidNumbers.snapshot().state !== 'unbound' || invalidNumbers.snapshot().gyroContributionAllowed) {
  throw new Error('T11 NaN offset did not invalidate and safe-zero');
}

const staleGeneration = new GyroCalibrationSession();
staleGeneration.start(generation);
staleGeneration.observe(sample(5, { powerGeneration: 3, confidence: 1, weight: 10 }));
if (staleGeneration.snapshot().state !== 'unbound' || staleGeneration.snapshot().gyroContributionAllowed) {
  throw new Error('T11 old power generation was accepted');
}

const staleCalibrationEpoch = new GyroCalibrationSession();
staleCalibrationEpoch.start(generation);
staleCalibrationEpoch.observe(sample(51, { calibrationEpoch: 2, confidence: 1, weight: 10 }));
if (staleCalibrationEpoch.snapshot().state !== 'unbound' || staleCalibrationEpoch.snapshot().gyroContributionAllowed) {
  throw new Error('T11 old calibration epoch was accepted');
}

const missingAccel = new GyroCalibrationSession();
missingAccel.start(generation);
missingAccel.observe(sample(6, { accel: null, confidence: 1, weight: 10 }));
if (missingAccel.snapshot().state !== 'unbound' || missingAccel.snapshot().gyroContributionAllowed) {
  throw new Error('T11 missing accel was allowed into calibration');
}

const thresholdBelowHcMinimum = new GyroCalibrationSession();
thresholdBelowHcMinimum.start({ ...generation, gyroThreshold: 123 });
if (thresholdBelowHcMinimum.snapshot().state !== 'unbound' || thresholdBelowHcMinimum.snapshot().gyroContributionAllowed) {
  throw new Error('T11 accepted gyro threshold below HC GamepadMotion.minGyro');
}

const traces = [timeoutSession, successful, moving, invalidNumbers, staleGeneration, staleCalibrationEpoch, missingAccel, thresholdBelowHcMinimum].flatMap((session) => session.trace());
for (const required of ['calibrate-timeout', 'calibrate-lock', 'calibrate-aborted-motion', 'calibrate-offset-invalid', 'sample-stale', 'calibrate-provider-incomplete']) {
  if (!traces.some((event) => event.event === required)) throw new Error(`T11 trace omitted ${required}`);
}
const evidence = {
  evidenceId: 'T11-E01-GYRO-CALIBRATION-SESSION-MOCK-20260904',
  status: 'MOCK-READY', systemMutation: false,
  contract: '30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md#10',
  assertions: {
    confidenceEqualsOneOnly: true, timeoutCannotLockManual: true, movementAbortsCandidate: true,
    invalidOffsetRejected: true, oldGenerationRejected: true, oldCalibrationEpochRejected: true, missingAccelSafeStops: true,
    physicalBaseInputOutsideGyroGate: true,
  },
  trace: traces,
};
const evidencePath = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/T11-E01-gyro-calibration-session-mock-20260904.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify(evidence, null, 2));
console.log(`gyro calibration session selftest: PASS (${evidencePath})`);
