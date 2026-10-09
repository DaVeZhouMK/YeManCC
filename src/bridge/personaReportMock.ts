import type { CanonicalFrame } from './virtualReportAssembler';

export type EncodedVirtualReport = {
  schemaVersion: 1;
  persona: 'dualshock4' | 'xbox360';
  outputMode: 'virtual-stick';
  runId: string;
  epoch: number;
  targetId: string;
  inputSequence: number;
  neutral: boolean;
  buttons: number;
  leftStick: { x: number; y: number };
  rightStick: { x: number; y: number };
  triggers: { left: number; right: number };
};

export type PersonaEncodeResult =
  | { ok: true; report: EncodedVirtualReport }
  | { ok: false; reason: 'persona-mode-conflict' | 'invalid-frame' };

const clamp = (value: number) => Math.max(-1, Math.min(1, value));
const finite = (value: number) => Number.isFinite(value);

/**
 * F3 fixture-only persona encoder. DS4 IMU is deliberately not synthesized
 * from a virtual-stick frame, so one gyro sample cannot reach two outputs.
 */
export function encodePersonaReport(frame: CanonicalFrame): PersonaEncodeResult {
  if (frame.persona !== 'dualshock4' && frame.persona !== 'xbox360') {
    return { ok: false, reason: 'persona-mode-conflict' };
  }
  const values = [
    frame.buttons, frame.inputSequence, frame.epoch,
    frame.axes.leftX ?? 0, frame.axes.leftY ?? 0,
    frame.rightStick.x, frame.rightStick.y,
    frame.triggers.left ?? 0, frame.triggers.right ?? 0,
  ];
  if (!values.every(finite)) return { ok: false, reason: 'invalid-frame' };

  return {
    ok: true,
    report: {
      schemaVersion: 1,
      persona: frame.persona,
      outputMode: 'virtual-stick',
      runId: frame.runId,
      epoch: frame.epoch,
      targetId: frame.targetId,
      inputSequence: frame.inputSequence,
      neutral: frame.neutral,
      buttons: frame.buttons,
      leftStick: { x: clamp(frame.axes.leftX ?? 0), y: clamp(frame.axes.leftY ?? 0) },
      rightStick: { x: clamp(frame.rightStick.x), y: clamp(frame.rightStick.y) },
      triggers: { left: clamp(frame.triggers.left ?? 0), right: clamp(frame.triggers.right ?? 0) },
    },
  };
}
