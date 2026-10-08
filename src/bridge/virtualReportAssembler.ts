/** Stage F3/T5 assembler. HC LayoutManager gyro blend; no HID/backend IO. */
import {
  applyHcGyroAxisModifiers,
  blendGyroShortIntoStick,
  hcClampToShort,
  hcShortToUnit,
  hcUnitToShort,
  HC_DEFAULT_AXIS_ANTI_DEADZONE,
  HC_DEFAULT_GYRO_WEIGHT,
  type HcOutputShape,
  type ResponseCurvePoint,
} from './hcInputUtils';

export type InputPersona = 'disabled' | 'dualshock4' | 'xbox360';

export interface ControllerFrame {
  runId: string; epoch: number; powerGeneration: number; targetId: string;
  persona: InputPersona; configRevision: number;
  inputSequence: number; timestamp: number;
  buttons: number; axes: Record<string, number>; triggers: Record<string, number>;
  rightStick: { x: number; y: number };
}

export interface MotionFrame {
  runId: string; epoch: number; powerGeneration: number; targetId: string;
  configRevision: number; sampleSequence: number; timestamp: number;
  valid: boolean; contribution: { x: number; y: number };
  gyroShort?: { x: number; y: number };
}

export interface GyroAxisBlendConfig {
  gyroWeight?: number;
  innerDeadzone?: number;
  outerDeadzone?: number;
  antiDeadzone?: number;
  outputShape?: HcOutputShape;
  responseCurvePoints?: readonly ResponseCurvePoint[] | null;
}

export interface CanonicalFrame {
  schemaVersion: 1; runId: string; epoch: number; powerGeneration: number;
  targetId: string; persona: Exclude<InputPersona, 'disabled'>; configRevision: number;
  inputSequence: number; sampleSequence: number; timestamp: number; neutral: boolean;
  buttons: number; axes: Record<string, number>; triggers: Record<string, number>;
  rightStickBase: { x: number; y: number };
  gyroContribution: { x: number; y: number };
  rightStick: { x: number; y: number };
}

export type AssembleResult = { ok: true; frame: CanonicalFrame } | { ok: false; reason: 'frame-stale' | 'motion-invalid' | 'invalid-value' | 'persona-disabled' };

const finite = (v: number) => Number.isFinite(v);
const clamp = (v: number) => Math.max(-1, Math.min(1, v));
const sameGeneration = (c: ControllerFrame, m: MotionFrame) =>
  c.runId === m.runId && c.epoch === m.epoch && c.powerGeneration === m.powerGeneration &&
  c.targetId === m.targetId && c.configRevision === m.configRevision;

function gyroShortFromMotion(motion: MotionFrame): { x: number; y: number } {
  if (motion.gyroShort && [motion.gyroShort.x, motion.gyroShort.y].every(finite)) {
    return { x: hcClampToShort(motion.gyroShort.x), y: hcClampToShort(motion.gyroShort.y) };
  }
  return { x: hcUnitToShort(motion.contribution.x), y: hcUnitToShort(motion.contribution.y) };
}

/**
 * Merge a controller frame and optional gyro contribution.
 * Gyro-off leaves the physical right stick unchanged. When gyro is present,
 * AxisActions modifiers run on the gyro vector, then LayoutManager blends:
 * current + gyro * (gyroWeight - stickNorm).
 */
export function assembleCanonicalFrame(controller: ControllerFrame, motion?: MotionFrame, blend?: GyroAxisBlendConfig): AssembleResult {
  if (controller.persona === 'disabled') return { ok: false, reason: 'persona-disabled' };
  const base = controller.rightStick;
  if (![base.x, base.y, controller.timestamp].every(finite) || !Number.isSafeInteger(controller.inputSequence) || controller.inputSequence < 0) {
    return { ok: false, reason: 'invalid-value' };
  }
  let contribution = { x: 0, y: 0 };
  let sampleSequence = 0;
  let rightStick = { x: clamp(base.x), y: clamp(base.y) };
  if (motion) {
    if (!sameGeneration(controller, motion)) return { ok: false, reason: 'frame-stale' };
    if (!motion.valid || ![motion.contribution.x, motion.contribution.y, motion.timestamp].every(finite) ||
      !Number.isSafeInteger(motion.sampleSequence) || motion.sampleSequence < 0) return { ok: false, reason: 'motion-invalid' };
    const modified = applyHcGyroAxisModifiers(gyroShortFromMotion(motion), {
      innerDeadzone: blend?.innerDeadzone ?? 0,
      outerDeadzone: blend?.outerDeadzone ?? 0,
      antiDeadzone: blend?.antiDeadzone ?? HC_DEFAULT_AXIS_ANTI_DEADZONE,
      outputShape: blend?.outputShape ?? 'default',
      responseCurvePoints: blend?.responseCurvePoints ?? null,
    });
    contribution = { x: hcShortToUnit(modified.x), y: hcShortToUnit(modified.y) };
    sampleSequence = motion.sampleSequence;
    const blended = blendGyroShortIntoStick(
      { x: hcUnitToShort(base.x), y: hcUnitToShort(base.y) },
      modified,
      blend?.gyroWeight ?? HC_DEFAULT_GYRO_WEIGHT,
    );
    rightStick = { x: hcShortToUnit(blended.x), y: hcShortToUnit(blended.y) };
  }
  const result: CanonicalFrame = {
    schemaVersion: 1, runId: controller.runId, epoch: controller.epoch,
    powerGeneration: controller.powerGeneration, targetId: controller.targetId,
    persona: controller.persona, configRevision: controller.configRevision,
    inputSequence: controller.inputSequence, sampleSequence,
    timestamp: Math.max(controller.timestamp, motion?.timestamp ?? 0), neutral: false,
    buttons: controller.buttons, axes: { ...controller.axes }, triggers: { ...controller.triggers },
    rightStickBase: { x: clamp(base.x), y: clamp(base.y) }, gyroContribution: contribution,
    rightStick,
  };
  return { ok: true, frame: result };
}

export function neutralCanonicalFrame(controller: ControllerFrame): CanonicalFrame {
  const result = assembleCanonicalFrame({ ...controller, buttons: 0, axes: {}, triggers: {}, rightStick: { x: 0, y: 0 } });
  if (result.ok) return { ...result.frame, neutral: true };
  throw new Error(`cannot create neutral frame: ${(result as Exclude<AssembleResult, { ok: true }>).reason}`);
}
