/** EXACT_HC subset of HandheldCompanion/Utils/InputUtils.cs + Profile.cs + AxisActions/LayoutManager blend. */
export const HC_SHORT_MAX = 32767;
export const HC_SHORT_MIN = -32768;
/** GamepadMotion.minGyro: threshold-calibration lower bound, in deg/s. */
export const HC_MIN_GYRO_THRESHOLD_DPS = 124;
/** IMUCalibration.thresholdG default, in deg/s. This is not a deadzone. */
export const HC_GYRO_THRESHOLD_DPS = 2000;
export const HC_SENSITIVITY_ARRAY_SIZE = 49;
export const HC_SENSITIVITY_NEAREST = 2;
export const HC_DEFAULT_GYRO_WEIGHT = 1.2;
export const HC_DEFAULT_AXIS_ANTI_DEADZONE = 15;
export const HC_DEFAULT_MOTION_SENSITIVITY = 1;
export const HC_DEFAULT_GYROMETER_MULTIPLIER = 1;
export const HC_VELOCITY_DECAY = 0.9;
export const HC_VELOCITY_FPS = 60;

/** XInput button-name set parsed by native inputCaptureMotionTriggerPressed
 *  (main.cpp:5312-5332). YMCC exposes triggers as free text; names outside this
 *  set fail closed in native (triggerResolved=false -> motion off). Keep this
 *  list in sync with native so UI preview and selftest match runtime semantics. */
export const HC_MOTION_TRIGGER_NAMES: ReadonlySet<string> = new Set([
  'a', 'b', 'x', 'y', 'lb', 'l1', 'rb', 'r1', 'back', 'select', 'start', 'menu',
  'leftthumb', 'ls', 'rightthumb', 'rs', 'dpadup', 'dpaddown', 'dpadleft', 'dpadright',
  'lt', 'l2', 'rt', 'r2',
]);

/** Mirrors native triggerResolved (main.cpp:5154-5167): empty/none/disabled or a
 *  known XInput name. Case-insensitive; whitespace trimmed. */
export function isResolvedMotionTrigger(trigger: string | null | undefined): boolean {
  const key = (trigger ?? '').trim().toLowerCase();
  if (!key || key === 'none' || key === 'disabled') return true;
  return HC_MOTION_TRIGGER_NAMES.has(key);
}

export type Vec2 = { x: number; y: number };
export type SensitivityNode = readonly [number, number];
export type HcOutputShape = 'default' | 'circle' | 'cross' | 'square';

export function hcClampToShort(value: number): number {
  if (!Number.isFinite(value)) return 0;
  const clamped = Math.max(HC_SHORT_MIN, Math.min(HC_SHORT_MAX, value));
  return Math.trunc(clamped);
}

export function hcShortToUnit(value: number): number {
  return Math.max(-1, Math.min(1, value / HC_SHORT_MAX));
}

export function hcUnitToShort(value: number): number {
  return hcClampToShort(value * HC_SHORT_MAX);
}

export function createDefaultMotionSensitivityArray(): SensitivityNode[] {
  const nodes: SensitivityNode[] = [];
  for (let i = 0; i < HC_SENSITIVITY_ARRAY_SIZE; i += 1) {
    nodes.push([i / (HC_SENSITIVITY_ARRAY_SIZE - 1), 0.5]);
  }
  return nodes;
}

/** InputUtils.ApplyCustomSensitivity(AngularValue, MaxValue, Nodes) */
export function applyCustomSensitivity(angularValue: number, maxValue: number, nodes: readonly SensitivityNode[]): number {
  if (!Number.isFinite(angularValue) || !Number.isFinite(maxValue) || maxValue === 0) return 0;
  const posAbs = Math.abs(angularValue / maxValue);
  if (posAbs <= 0) return 0;
  if (posAbs >= 1) return 1;
  if (!nodes.length) return posAbs;
  let best1Dist = Number.POSITIVE_INFINITY;
  let best2Dist = Number.POSITIVE_INFINITY;
  let best1 = 0;
  let best2 = 0;
  for (const [key, value] of nodes) {
    const distance = Math.abs(key - posAbs);
    if (distance < best1Dist) {
      best2Dist = best1Dist;
      best2 = best1;
      best1Dist = distance;
      best1 = value;
    } else if (distance < best2Dist) {
      best2Dist = distance;
      best2 = value;
    }
  }
  const k = Math.min(HC_SENSITIVITY_NEAREST, Math.max(1, nodes.length));
  const w1 = 1 / (1 + best1Dist);
  let sum = best1 * w1;
  if (k > 1 && best2Dist < Number.POSITIVE_INFINITY) sum += best2 * (1 / (1 + best2Dist));
  return (sum / k) * 2;
}

/** Profile.GetSensitivityX/Y = MotionSensivity * 1000 */
export function getHcSensitivity(motionSensitivity = HC_DEFAULT_MOTION_SENSITIVITY): number {
  return motionSensitivity * 1000;
}

export function thumbScaledRadialInnerOuterDeadzone(value: Vec2, innerPercent: number, outerPercent: number): Vec2 {
  if ((innerPercent === 0 && outerPercent === 0) || (value.x === 0 && value.y === 0)) return { ...value };
  const stickX = value.x / HC_SHORT_MAX;
  const stickY = value.y / HC_SHORT_MAX;
  const len = Math.hypot(stickX, stickY);
  const inner = innerPercent / 100;
  const outer = outerPercent / 100;
  if (len <= inner) return { x: 0, y: 0 };
  if (len >= 1 - outer) {
    const mul = 1 / len;
    return { x: stickX * mul * HC_SHORT_MAX, y: stickY * mul * HC_SHORT_MAX };
  }
  const mappedLen = (len - inner) / (1 - inner - outer);
  const inv = mappedLen / len;
  return { x: stickX * inv * HC_SHORT_MAX, y: stickY * inv * HC_SHORT_MAX };
}

export function applyAntiDeadzone(value: Vec2, deadzonePercent: number): Vec2 {
  if (deadzonePercent === 0 || (value.x === 0 && value.y === 0)) return { ...value };
  const stickX = value.x / HC_SHORT_MAX;
  const stickY = value.y / HC_SHORT_MAX;
  const len = Math.hypot(stickX, stickY);
  if (len <= 0) return { x: 0, y: 0 };
  const dz = deadzonePercent / 100;
  const mul = ((1 - dz) * len + dz) / len;
  return { x: stickX * mul * HC_SHORT_MAX, y: stickY * mul * HC_SHORT_MAX };
}

export function improveCircularity(value: Vec2): Vec2 {
  const stickX = value.x / HC_SHORT_MAX;
  const stickY = value.y / HC_SHORT_MAX;
  const len = Math.hypot(stickX, stickY);
  if (len <= 1) return { ...value };
  const mul = 1 / len;
  return { x: stickX * mul * HC_SHORT_MAX, y: stickY * mul * HC_SHORT_MAX };
}

export function improveSquare(value: Vec2): Vec2 {
  let stickX = value.x / HC_SHORT_MAX;
  let stickY = value.y / HC_SHORT_MAX;
  let len = Math.hypot(stickX, stickY);
  if (len < 1e-5) return { x: 0, y: 0 };
  if (len > 1) {
    stickX /= len;
    stickY /= len;
    len = 1;
  }
  const denom = Math.max(Math.abs(stickX), Math.abs(stickY));
  if (denom > 1e-5) {
    const scale = len / denom;
    stickX *= scale;
    stickY *= scale;
  }
  return { x: stickX * HC_SHORT_MAX, y: stickY * HC_SHORT_MAX };
}

function applyAxisDeadzone(value: number, deadzone: number): number {
  const absVal = Math.abs(value);
  if (absVal < deadzone) return 0;
  const range = 1 - deadzone;
  return ((absVal - deadzone) / range) * Math.sign(value);
}

export function crossDeadzoneMapping(value: Vec2, xPercent: number, yPercent: number): Vec2 {
  const stickX = applyAxisDeadzone(value.x / HC_SHORT_MAX, xPercent / 100);
  const stickY = applyAxisDeadzone(value.y / HC_SHORT_MAX, yPercent / 100);
  return { x: stickX * HC_SHORT_MAX, y: stickY * HC_SHORT_MAX };
}

/**
 * HC AxisActions.ApplyAxisModifiers (Actions/AxisActions.cs:81-99). The full
 * short-space pipeline on a gyro contribution is:
 *   radial inner/outer deadzone -> anti-deadzone -> response curve -> output shape.
 * ResponseCurvePoints defaults to a 6-point linear identity curve
 * (0,0),(0.2,0.2),...,(1,1), so absent/custom points are optional. AxisActions-local
 * inversion is a separate, unrepresented layout control; callers must not treat
 * this helper as full AxisActions parity outside these four steps.
 */
export type ResponseCurvePoint = readonly [number, number];

/** HC InputUtils.InterpolateResponseCurve (InputUtils.cs:494-518). */
function interpolateResponseCurve(inputValue: number, points: readonly ResponseCurvePoint[]): number {
  if (points.length < 2) return inputValue;
  if (inputValue <= 0) return points[0][1];
  if (inputValue >= 1) return points[points.length - 1][1];
  for (let i = 0; i < points.length - 1; i++) {
    const p1 = points[i];
    const p2 = points[i + 1];
    if (inputValue >= p1[0] && inputValue <= p2[0]) {
      if (p2[0] === p1[0]) return p1[1];
      const t = (inputValue - p1[0]) / (p2[0] - p1[0]);
      return p1[1] + t * (p2[1] - p1[1]);
    }
  }
  return inputValue; // Fallback to identity
}

/** HC InputUtils.ApplyResponseCurve (InputUtils.cs:469-489): magnitude multiplier. */
export function applyResponseCurve(value: Vec2, points: readonly ResponseCurvePoint[] | undefined | null): Vec2 {
  if (!points || points.length < 2) return { ...value };
  const stickX = value.x / HC_SHORT_MAX;
  const stickY = value.y / HC_SHORT_MAX;
  const magnitude = Math.hypot(stickX, stickY);
  if (magnitude === 0) return { ...value };
  const normalizedMagnitude = Math.min(1, magnitude);
  const curvedMagnitude = interpolateResponseCurve(normalizedMagnitude, points);
  const multiplier = curvedMagnitude / normalizedMagnitude;
  return { x: stickX * multiplier * HC_SHORT_MAX, y: stickY * multiplier * HC_SHORT_MAX };
}

/** HC default 6-point linear identity curve (AxisActions.cs:36-44). */
export const HC_DEFAULT_RESPONSE_CURVE: readonly ResponseCurvePoint[] = [
  [0, 0], [0.2, 0.2], [0.4, 0.4], [0.6, 0.6], [0.8, 0.8], [1, 1],
];

/**
 * HC AxisActions.ApplyAxisModifiers subset on a gyro contribution (short
 * space): radial deadzones, anti-deadzone, response curve, and output shape.
 * ResponseCurvePoints and AxisActions-local inversion are separate, unrepresented
 * layout controls; callers must not treat this helper as full AxisActions parity.
 */
export function applyHcGyroAxisModifiers(
  value: Vec2,
  config: { innerDeadzone?: number; outerDeadzone?: number; antiDeadzone?: number; outputShape?: HcOutputShape; responseCurvePoints?: readonly ResponseCurvePoint[] | null },
): Vec2 {
  if (value.x === 0 && value.y === 0) return { x: 0, y: 0 };
  let out = thumbScaledRadialInnerOuterDeadzone(value, config.innerDeadzone ?? 0, config.outerDeadzone ?? 0);
  out = applyAntiDeadzone(out, config.antiDeadzone ?? 0);
  out = applyResponseCurve(out, config.responseCurvePoints && config.responseCurvePoints.length >= 2 ? config.responseCurvePoints : null);
  switch (config.outputShape) {
    case 'circle':
      out = improveCircularity(out);
      break;
    case 'cross':
      out = improveCircularity(crossDeadzoneMapping(out, config.innerDeadzone ?? 0, config.outerDeadzone ?? 0));
      break;
    case 'square':
      out = improveSquare(out);
      break;
    default:
      break;
  }
  return out;
}

/** LayoutManager.ProcessGyroActions: current + gyro * (gyroWeight - stickNorm) */
export function blendGyroIntoStick(current: Vec2, gyro: Vec2, gyroWeight = HC_DEFAULT_GYRO_WEIGHT): Vec2 {
  const currentShort = { x: hcUnitToShort(current.x), y: hcUnitToShort(current.y) };
  const gyroShort = { x: hcUnitToShort(gyro.x), y: hcUnitToShort(gyro.y) };
  const stickNorm = Math.min(1, Math.max(0, Math.hypot(currentShort.x, currentShort.y) / HC_SHORT_MAX));
  const weightFactor = gyroWeight - stickNorm;
  return {
    x: hcShortToUnit(hcClampToShort(currentShort.x + gyroShort.x * weightFactor)),
    y: hcShortToUnit(hcClampToShort(currentShort.y + gyroShort.y * weightFactor)),
  };
}

export function blendGyroShortIntoStick(currentShort: Vec2, gyroShort: Vec2, gyroWeight = HC_DEFAULT_GYRO_WEIGHT): Vec2 {
  const stickNorm = Math.min(1, Math.max(0, Math.hypot(currentShort.x, currentShort.y) / HC_SHORT_MAX));
  const weightFactor = gyroWeight - stickNorm;
  return {
    x: hcClampToShort(currentShort.x + gyroShort.x * weightFactor),
    y: hcClampToShort(currentShort.y + gyroShort.y * weightFactor),
  };
}


export const HC_DEFAULT_STEERING_MAX_ANGLE = 30;
export const HC_DEFAULT_STEERING_POWER = 1;
export const HC_DEFAULT_STEERING_DEADZONE = 0;

/** HandheldCompanion/Misc/Inclination.UpdateReport */
export function hcInclinationAngles(accel: Vec2 & { z: number }): Vec2 {
  const ax = accel.x, ay = accel.y, az = accel.z;
  const rad2deg = 180 / Math.PI;
  return {
    x: -(Math.atan(ay / Math.sqrt(ax * ax + az * az)) * rad2deg),
    y: -(Math.atan(ax / Math.sqrt(ay * ay + az * az)) * rad2deg),
  };
}

function angleToJoystickPos(angle: number, deviceAngleMax: number, deadzoneAngle: number): number {
  let result = ((Math.abs(angle) - deadzoneAngle) / (deviceAngleMax - deadzoneAngle)) * deviceAngleMax;
  result = Math.min(deviceAngleMax, Math.max(0, result)) / deviceAngleMax;
  return angle < 0 ? -result : result;
}

function directionRespectingPowerOf(joystickPos: number, power: number): number {
  const result = Math.abs(joystickPos) ** power;
  return joystickPos < 0 ? -result : result;
}

/** InputUtils.Steering: returns short-space yaw contribution. */
export function hcSteering(deviceAngle: number, deviceAngleMax = HC_DEFAULT_STEERING_MAX_ANGLE, power = HC_DEFAULT_STEERING_POWER, deadzoneAngle = HC_DEFAULT_STEERING_DEADZONE): number {
  const pos = directionRespectingPowerOf(angleToJoystickPos(deviceAngle, deviceAngleMax, deadzoneAngle), power);
  return -(pos * HC_SHORT_MAX);
}
