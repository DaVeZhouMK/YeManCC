import type { GamepadMotionPlaneTelemetryV1, GyroMotionConfigV1 } from './inputContracts';
import type { MotionFrame } from './virtualReportAssembler';
import {
  applyCustomSensitivity,
  createDefaultMotionSensitivityArray,
  getHcSensitivity,
  hcClampToShort,
  hcInclinationAngles,
  hcShortToUnit,
  hcSteering,
  HC_DEFAULT_STEERING_DEADZONE,
  HC_DEFAULT_STEERING_MAX_ANGLE,
  HC_DEFAULT_STEERING_POWER,
  HC_GYRO_THRESHOLD_DPS,
  isResolvedMotionTrigger,
  HC_VELOCITY_DECAY,
  HC_VELOCITY_FPS,
  type SensitivityNode,
  type Vec2,
} from './hcInputUtils';

export type HcMotionSample = {
  sequence: number;
  timestamp: number;
  deltaSeconds?: number;
  // HC LocalSpace and JoystickSteering consume the Default plane, while
  // Player/World consume GamepadMotion spaces. Raw diagnostic axes are not
  // accepted as a substitute for either data plane.
  gamepadMotionPlane?: GamepadMotionPlaneTelemetryV1;
};
export type MotionContext = Omit<MotionFrame, 'sampleSequence' | 'timestamp' | 'valid' | 'contribution' | 'gyroShort'>;
export type MotionMapResult =
  | { ok: true; frame: MotionFrame }
  | { ok: false; reason: 'unclosed-config' | 'sample-invalid' | 'unit-unsupported' | 'sample-stale' | 'curve-unsupported' | 'default-plane-unproven' | 'gamepad-motion-space-unproven' };
export type GyroGateContext = {
  pressed?: ReadonlySet<string>;
  toggleOn?: boolean;
  lastSequence?: number;
  accumulatedDisplacement?: Vec2;
};

const DEFAULT_SENSITIVITY_ARRAY = createDefaultMotionSensitivityArray();

function motionActive(config: GyroMotionConfigV1, gate?: GyroGateContext): boolean {
  // Native fail-closed: an unresolved trigger name gates motion OFF regardless
  // of mode (main.cpp:5154-5184 nextMotionEnabled &&= triggerResolved). Keep the
  // mapper identical so UI preview cannot show output where runtime is zero.
  if (!isResolvedMotionTrigger(config.motionTrigger)) return false;
  // Native normalizes the configured trigger to lower case (main.cpp:5152
  // ascii_lower) before comparing to XInput names. The pressed set carries the
  // same key vocabulary; accept the lower-cased key and the raw spelling so a
  // persisted uppercase name like "RT" still matches a gate that says "rt".
  const raw = (config.motionTrigger || '').trim();
  const trigger = raw.toLowerCase();
  const pressed = !!raw && (!!gate?.pressed?.has(trigger) || !!gate?.pressed?.has(raw));
  const mode = config.motionMode ?? 'off';
  if (mode === 'on') return !pressed;
  if (mode === 'toggle') return !!gate?.toggleOn;
  return pressed;
}

function sensitivityNodes(config: GyroMotionConfigV1): readonly SensitivityNode[] {
  return config.motionSensitivityArray?.length ? config.motionSensitivityArray : DEFAULT_SENSITIVITY_ARRAY;
}

function zeroFrame(context: MotionContext, sample: HcMotionSample): MotionFrame {
  return {
    ...context,
    sampleSequence: sample.sequence,
    timestamp: sample.timestamp,
    valid: true,
    contribution: { x: 0, y: 0 },
    gyroShort: { x: 0, y: 0 },
  };
}

/**
 * G1/T4 fixture: HandheldCompanion MotionManager.ProcessMotion mapping stage.
 * The sample is deliberately post-SetupMotion: Default and Player/World values
 * must originate from the same admitted GamepadMotion plane. This fixture does
 * not synthesize that plane from raw sensor telemetry.
 * Deadzone/outputShape/gyroWeight belong to AxisActions + LayoutManager (T5), not this mapper.
 */
export function mapGyroSample(context: MotionContext, config: GyroMotionConfigV1, sample: HcMotionSample, gate?: GyroGateContext): MotionMapResult {
  if (config.closure !== 'CLOSED' || !config.provider || !config.calibrationId || !config.unit) return { ok: false, reason: 'unclosed-config' };
  if (config.unit !== 'deg/s') return { ok: false, reason: 'unit-unsupported' };
  if (config.outputShape && !['default', 'circle', 'cross', 'square'].includes(config.outputShape)) return { ok: false, reason: 'curve-unsupported' };
  const motionInput = config.motionInput ?? 'local-space';
  if (motionInput !== 'local-space' && motionInput !== 'joystick-steering' && motionInput !== 'player-space' && motionInput !== 'world-space') return { ok: false, reason: 'unclosed-config' };
  if (![sample.sequence, sample.timestamp].every(Number.isFinite) || sample.sequence < 0) return { ok: false, reason: 'sample-invalid' };
  if (gate?.lastSequence !== undefined && sample.sequence <= gate.lastSequence) return { ok: false, reason: 'sample-stale' };
  // `enabled` is a persisted admission bit, not merely a UI hint. Keep
  // disabled motion at a zero contribution while preserving the physical
  // controller frame for the caller.
  if (config.enabled === false) {
    if (gate) gate.accumulatedDisplacement = { x: 0, y: 0 };
    return { ok: true, frame: zeroFrame(context, sample) };
  }
  if (config.outputMode !== 'virtual-stick') return { ok: true, frame: zeroFrame(context, sample) };
  if (!motionActive(config, gate)) {
    if (gate) gate.accumulatedDisplacement = { x: 0, y: 0 };
    return { ok: true, frame: zeroFrame(context, sample) };
  }

  let outputX = 0;
  let outputY = 0;
  const steering = motionInput === 'joystick-steering';
  const plane = sample.gamepadMotionPlane;
  if (steering) {
    if (!plane) return { ok: false, reason: 'default-plane-unproven' };
    const angles = hcInclinationAngles(plane.defaultAccel);
    if (![angles.x, angles.y].every(Number.isFinite)) return { ok: false, reason: 'sample-invalid' };
    outputX = hcSteering(
      angles.y,
      config.steeringMaxAngle ?? HC_DEFAULT_STEERING_MAX_ANGLE,
      config.steeringPower ?? HC_DEFAULT_STEERING_POWER,
      config.steeringDeadzone ?? HC_DEFAULT_STEERING_DEADZONE,
    );
  } else if (motionInput === 'player-space') {
    if (!plane?.playerSpace) return { ok: false, reason: 'gamepad-motion-space-unproven' };
    outputX = -plane.playerSpace.y;
    outputY = plane.playerSpace.x;
  } else if (motionInput === 'world-space') {
    if (!plane?.worldSpace) return { ok: false, reason: 'gamepad-motion-space-unproven' };
    outputX = -plane.worldSpace.y;
    outputY = plane.worldSpace.x;
  } else {
    if (!plane) return { ok: false, reason: 'default-plane-unproven' };
    outputX = plane.defaultGyro.z;
    outputY = plane.defaultGyro.x;
  }
  if (config.invertHorizontal) outputX *= -1;
  if (config.invertVertical) outputY *= -1;

  const threshold = config.gyroThreshold ?? HC_GYRO_THRESHOLD_DPS;
  const nodes = sensitivityNodes(config);
  outputX *= applyCustomSensitivity(outputX, threshold, nodes);
  outputY *= applyCustomSensitivity(outputY, threshold, nodes);

  // HC MotionManager.cs:296-298: when the aiming trigger is held, the motion
  // output is multiplied by AimingSightsMultiplier (default 1.0 = no-op).
  // Native normalizes the trigger name to lower case (main.cpp:5262) before
  // parsing; resolve the same vocabulary, and require a resolvable name so an
  // unknown string does not silently no-op the multiplier.
  const aimingRaw = (config.aimingSightsTrigger || '').trim();
  const aimingKey = aimingRaw.toLowerCase();
  const aimingTrigger = aimingRaw && isResolvedMotionTrigger(aimingKey) ? aimingKey : '';
  if (aimingTrigger && (!!gate?.pressed?.has(aimingTrigger) || !!gate?.pressed?.has(aimingRaw)) && (config.aimingSightsMultiplier ?? 1) !== 1) {
    const aiming = config.aimingSightsMultiplier ?? 1;
    outputX *= aiming;
    outputY *= aiming;
  }

  const sensitivityX = getHcSensitivity(config.motionSensitivityX);
  const sensitivityY = getHcSensitivity(config.motionSensitivityY);
  const velocityMode = config.velocityMode === 'velocity' && !steering;
  if (velocityMode) {
    const delta = Number.isFinite(sample.deltaSeconds) && (sample.deltaSeconds as number) > 0 ? sample.deltaSeconds as number : 1 / HC_VELOCITY_FPS;
    const scale = config.velocityScale ?? 1;
    const accumulated = gate?.accumulatedDisplacement ?? { x: 0, y: 0 };
    accumulated.x += outputX * delta * HC_VELOCITY_FPS * scale;
    accumulated.y += outputY * delta * HC_VELOCITY_FPS * scale;
    outputX = accumulated.x;
    outputY = accumulated.y;
    outputX *= sensitivityX;
    outputY *= sensitivityY;
    const shortX = hcClampToShort(outputX);
    const shortY = hcClampToShort(outputY);
    if (sensitivityX !== 0) accumulated.x -= shortX / sensitivityX;
    if (sensitivityY !== 0) accumulated.y -= shortY / sensitivityY;
    accumulated.x *= HC_VELOCITY_DECAY;
    accumulated.y *= HC_VELOCITY_DECAY;
    if (gate) gate.accumulatedDisplacement = accumulated;
    return {
      ok: true,
      frame: {
        ...context,
        sampleSequence: sample.sequence,
        timestamp: sample.timestamp,
        valid: true,
        contribution: { x: hcShortToUnit(shortX), y: hcShortToUnit(shortY) },
        gyroShort: { x: shortX, y: shortY },
      },
    };
  }

  if (gate) gate.accumulatedDisplacement = { x: 0, y: 0 };
  if (!steering) {
    outputX *= sensitivityX;
    outputY *= sensitivityY;
  }
  const shortX = hcClampToShort(outputX);
  const shortY = hcClampToShort(outputY);
  return {
    ok: true,
    frame: {
      ...context,
      sampleSequence: sample.sequence,
      timestamp: sample.timestamp,
      valid: true,
      contribution: { x: hcShortToUnit(shortX), y: hcShortToUnit(shortY) },
      gyroShort: { x: shortX, y: shortY },
    },
  };
}
