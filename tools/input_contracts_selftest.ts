import { normalizeGyroTelemetry, validateInputSnapshot } from '../src/bridge/inputContracts';
import { normalizeSettings } from '../src/bridge/settingsRepository';
const base = { schemaVersion: 1, revision: 0, outputTarget: { schemaVersion: 1, revision: 0, closure: 'CLOSED' as const, persona: 'xbox360' as const, descriptorHash: 'sha', buttonMappingEnabled: true, gyroEnabled: false, visibilityPolicy: 'hidden' as const }, gyroMotion: { schemaVersion: 1, revision: 0, closure: 'NOT-APPLICABLE' as const } };
if (validateInputSnapshot({ ...base, outputTarget: { ...base.outputTarget, gyroEnabled: true }, gyroMotion: { schemaVersion: 1, revision: 0, closure: 'CLOSED' as const } }).ok) throw new Error('missing closed gyro data should not be admitted');
const valid = { ...base, gyroMotion: { schemaVersion: 1, revision: 0, closure: 'CLOSED' as const, provider: 'hc-gamepad-motion', unit: 'deg/s', calibrationId: 'cal-1' } };
if (!validateInputSnapshot(valid).ok) throw new Error('valid snapshot rejected');
if (validateInputSnapshot({ ...valid, outputTarget: { ...valid.outputTarget, gyroEnabled: true }, gyroMotion: { ...valid.gyroMotion, enabled: false } }).ok) throw new Error('mismatched gyro enabled gates accepted');
if (validateInputSnapshot({ ...valid, outputTarget: { ...valid.outputTarget, gyroEnabled: false }, gyroMotion: { ...valid.gyroMotion, enabled: true } }).ok) throw new Error('motion enabled while target gate disabled accepted');
if (validateInputSnapshot({ ...valid, outputTarget: { ...valid.outputTarget, gyroEnabled: true }, gyroMotion: { ...valid.gyroMotion, enabled: true, outputMode: 'ds4-imu' as const } }).ok) throw new Error('xbox360 DS4 IMU request accepted');
if (validateInputSnapshot({ ...valid, gyroMotion: { ...valid.gyroMotion, unit: 'rad/s' } }).ok) throw new Error('non-HC gyro unit accepted');
if (validateInputSnapshot({ ...base, outputTarget: { ...base.outputTarget, gyroEnabled: true } }).ok) throw new Error('enabled gyro without CLOSED motion contract accepted');
if (validateInputSnapshot({ ...valid, revision: -1 }).ok) throw new Error('negative revision accepted');
if (validateInputSnapshot({ ...valid, outputTarget: { ...valid.outputTarget, persona: 'disabled', buttonMappingEnabled: true } }).ok) throw new Error('disabled persona conflict accepted');
if (validateInputSnapshot({ ...valid, outputTarget: { ...valid.outputTarget, closure: 'CLOSED', descriptorHash: null } }).ok) throw new Error('missing descriptor accepted');
if (validateInputSnapshot({ ...valid, capability: { realRuntimeAuthorized: true } }).ok) throw new Error('missing runtime asset accepted');
const telemetry = normalizeGyroTelemetry({ gyro: { x: 4, y: -2, z: 'bad' }, output: { x: 0.25, y: -0.5 }, sequence: 7, timestamp: '2026-09-03T00:00:00Z' });
if (!telemetry || telemetry.schemaVersion !== 1 || telemetry.gyro.x !== 4 || telemetry.gyro.y !== -2 || telemetry.gyro.z !== 0 || telemetry.sequence !== 7) throw new Error('gyro telemetry normalization failed');
const hostFrame = normalizeGyroTelemetry({
  gyro: { x: 1, y: 2, z: 3 }, output: { x: 0.5, y: -0.25 }, outputKind: 'native-canonical-pipe-frame', sequence: 8,
  hostFrame: { hostSubmission: 'frame-accepted', hostActive: true, firstFrame: true, lifecycle: 'host-active', persona: 'dualshock4', rightStick: { x: 0.5, y: -0.25 }, gyroDps: { x: 0, y: 0, z: 0 } },
});
if (!hostFrame || hostFrame.outputKind !== 'native-canonical-pipe-frame' || hostFrame.hostFrame?.hostSubmission !== 'frame-accepted' || !hostFrame.hostFrame.hostActive || !hostFrame.hostFrame.firstFrame || hostFrame.hostFrame.lifecycle !== 'host-active' || hostFrame.hostFrame.rightStick.x !== 0.5 || hostFrame.hostFrame.rightStick.y !== -0.25) throw new Error('native host-frame telemetry normalization failed');
const listeningSuppressedFrame = normalizeGyroTelemetry({
  gyro: { x: 0, y: 0, z: 0 }, output: { x: 0, y: 0 }, outputKind: 'native-canonical-pipe-frame', sequence: 9,
  hostFrame: { hostSubmission: 'publication-suppressed', publicationReason: 'shortcut-recording', hostActive: true, firstFrame: false, lifecycle: 'host-active', persona: 'dualshock4', rightStick: { x: 0, y: 0 }, gyroDps: { x: 0, y: 0, z: 0 } },
});
if (!listeningSuppressedFrame || listeningSuppressedFrame.hostFrame?.hostSubmission !== 'publication-suppressed' || listeningSuppressedFrame.hostFrame.publicationReason !== 'shortcut-recording' || !listeningSuppressedFrame.hostFrame.hostActive || listeningSuppressedFrame.hostFrame.firstFrame) throw new Error('native listening publication suppression telemetry normalization failed');
const sourceAbsentSuppressedFrame = normalizeGyroTelemetry({
  gyro: { x: 0, y: 0, z: 0 }, output: { x: 0, y: 0 }, outputKind: 'native-canonical-pipe-frame', sequence: 10,
  hostFrame: { hostSubmission: 'publication-suppressed', publicationReason: 'physical-source-absent', hostActive: true, firstFrame: false, lifecycle: 'host-active', persona: 'dualshock4', rightStick: { x: 0, y: 0 }, gyroDps: { x: 0, y: 0, z: 0 } },
});
if (!sourceAbsentSuppressedFrame || sourceAbsentSuppressedFrame.hostFrame?.hostSubmission !== 'publication-suppressed' || sourceAbsentSuppressedFrame.hostFrame.publicationReason !== 'physical-source-absent' || !sourceAbsentSuppressedFrame.hostFrame.hostActive || sourceAbsentSuppressedFrame.hostFrame.firstFrame) throw new Error('physical-source no-write telemetry normalization failed');
const stagedMotionTelemetry = normalizeGyroTelemetry({
  gyro: { x: 1, y: 2, z: 3 }, output: { x: 0.4, y: -0.25 },
  motionContribution: { x: 0.8, y: -0.5 }, layoutContribution: { x: 0.4, y: -0.25 },
  motionManager: { mapped: true, triggered: true, velocityMode: true, gyroShort: { x: 26214, y: -16384 } },
  layoutManager: { physicalStick: { x: 0.2, y: 0 }, stickNorm: 0.2, gyroWeight: 1.2, weightFactor: 1, mergeOrder: 'physical-stick-plus-motion-contribution' },
});
if (!stagedMotionTelemetry || stagedMotionTelemetry.motionContribution?.x !== 0.8 || stagedMotionTelemetry.layoutContribution?.x !== 0.4 || stagedMotionTelemetry.motionManager?.gyroShort.x !== 26214 || stagedMotionTelemetry.layoutManager?.weightFactor !== 1) throw new Error('MotionManager/LayoutManager staged telemetry normalization failed');
const malformedStagedTelemetry = normalizeGyroTelemetry({
  gyro: { x: 0, y: 0, z: 0 }, output: { x: 0, y: 0 },
  motionManager: { mapped: 'yes', triggered: true, velocityMode: false, gyroShort: { x: 1, y: 2 } },
  layoutManager: { physicalStick: { x: 0, y: 0 }, stickNorm: 0, gyroWeight: 1.2, weightFactor: 1.2, mergeOrder: 'different-order' },
});
if (!malformedStagedTelemetry || malformedStagedTelemetry.motionManager !== undefined || malformedStagedTelemetry.layoutManager !== undefined) throw new Error('malformed staged telemetry accepted');
const normalizedCurveSettings = normalizeSettings({ input: { gyroMotion: { motionSensitivityArray: [[1, 0.2], [0, 0.8], [0.5, 0.5]] } } });
const normalizedCurve = normalizedCurveSettings.input.gyroMotion.motionSensitivityArray as [number, number][] | undefined;
if (!normalizedCurve || normalizedCurve.map(([key]) => key).join(',') !== '0,0.5,1') throw new Error('HC sorted sensitivity curve normalization failed');
const duplicateCurveSettings = normalizeSettings({ input: { gyroMotion: { motionSensitivityArray: [[0, 0.5], [0, 0.25]] } } });
if (duplicateCurveSettings.input.gyroMotion.motionSensitivityArray !== undefined) throw new Error('duplicate HC sensitivity curve key accepted');
const steeringRoll = normalizeSettings({ input: { gyroMotion: { steeringAxis: 'roll' } } });
const steeringYaw = normalizeSettings({ input: { gyroMotion: { steeringAxis: 'yaw' } } });
const steeringAuto = normalizeSettings({ input: { gyroMotion: { steeringAxis: 'auto' } } });
const steeringInvalid = normalizeSettings({ input: { gyroMotion: { steeringAxis: 'pitch' } } });
if (steeringRoll.input.gyroMotion.steeringAxis !== 'roll' || steeringYaw.input.gyroMotion.steeringAxis !== 'yaw' ||
    steeringAuto.input.gyroMotion.steeringAxis !== 'auto' || steeringInvalid.input.gyroMotion.steeringAxis !== 'roll') {
  throw new Error('HC Profile.SteeringAxis normalization failed');
}
const unknownHostFrame = normalizeGyroTelemetry({ gyro: { x: 0, y: 0, z: 0 }, output: { x: 0, y: 0 }, hostFrame: { hostSubmission: 'consumer-observed' } });
if (!unknownHostFrame || unknownHostFrame.hostFrame !== undefined) throw new Error('unknown host-frame state accepted');
const rawOnlyPlane = normalizeGyroTelemetry({ gyro: { x: 8, y: 0, z: 1 }, output: { x: 0, y: 0 }, gamepadMotionPlane: { defaultGyro: { x: 8, y: 0, z: 1 }, defaultAccel: { x: 0, y: 0, z: 1 } } });
if (!rawOnlyPlane || rawOnlyPlane.gamepadMotionPlane !== undefined) throw new Error('unproven GamepadMotion plane accepted');
const admittedPlane = normalizeGyroTelemetry({
  gyro: { x: 8, y: 0, z: 1 }, output: { x: 0, y: 0 }, timestampUtc: '2026-09-05T00:00:00.000Z',
  gamepadMotionPlane: {
    processProven: true, pairProven: true, calibrationLocked: true, steeringAxis: 'yaw',
    defaultGyro: { x: 8, y: 0, z: 1 }, defaultAccel: { x: 0, y: 0, z: 1 }, playerSpace: { x: 2, y: -3 },
  },
});
if (!admittedPlane || admittedPlane.gamepadMotionPlane?.steeringAxis !== 'yaw' || admittedPlane.gamepadMotionPlane.playerSpace?.y !== -3) throw new Error('admitted GamepadMotion plane normalization failed');
const timestampMissingPlane = normalizeGyroTelemetry({
  gyro: { x: 8, y: 0, z: 1 }, output: { x: 0, y: 0 },
  gamepadMotionPlane: {
    processProven: true, pairProven: true, calibrationLocked: true, steeringAxis: 'yaw',
    defaultGyro: { x: 8, y: 0, z: 1 }, defaultAccel: { x: 0, y: 0, z: 1 },
  },
});
if (!timestampMissingPlane || timestampMissingPlane.timestampUtc !== '' || timestampMissingPlane.gamepadMotionPlane !== undefined) throw new Error('missing timestamp admitted GamepadMotion plane');
const malformedTimestampPlane = normalizeGyroTelemetry({
  gyro: { x: 8, y: 0, z: 1 }, output: { x: 0, y: 0 }, timestampUtc: 'not-a-time',
  gamepadMotionPlane: {
    processProven: true, pairProven: true, calibrationLocked: true, steeringAxis: 'roll',
    defaultGyro: { x: 8, y: 0, z: 1 }, defaultAccel: { x: 0, y: 0, z: 1 },
  },
});
if (!malformedTimestampPlane || malformedTimestampPlane.timestampUtc !== '' || malformedTimestampPlane.gamepadMotionPlane !== undefined) throw new Error('malformed timestamp admitted GamepadMotion plane');
if (normalizeGyroTelemetry({ sequence: -1 }) !== null) throw new Error('empty gyro telemetry accepted');
console.log('input contracts selftest: PASS');
