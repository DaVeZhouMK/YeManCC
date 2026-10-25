import { encodePersonaReport } from '../src/bridge/personaReportMock';
import type { CanonicalFrame } from '../src/bridge/virtualReportAssembler';
import { VIRTUAL_OUTPUT_PERSONAS } from '../src/bridge/controllerPersona';

const frame: CanonicalFrame = {
  schemaVersion: 1, runId: 'run', epoch: 2, powerGeneration: 1, targetId: 'target', persona: 'elite', configRevision: 1,
  inputSequence: 5, sampleSequence: 4, timestamp: 100, neutral: false, buttons: 0x1000,
  axes: { leftX: 0.25, leftY: -0.5 }, triggers: { left: 0.8, right: 0.1 },
  rightStickBase: { x: 0.2, y: -0.1 }, gyroContribution: { x: 0.2, y: 0.1 }, rightStick: { x: 0.4, y: 0 },
};
for (const persona of VIRTUAL_OUTPUT_PERSONAS) {
  const encoded = encodePersonaReport({ ...frame, persona });
  if (!encoded.ok || encoded.report.persona !== persona || encoded.report.outputMode !== 'virtual-stick' || encoded.report.rightStick.x !== 0.4 || encoded.report.leftStick.y !== -0.5 || encoded.report.triggers.left !== 0.8 || encoded.report.buttons !== frame.buttons) throw new Error(`F3 current persona mock failed: ${persona}`);
  if ('imu' in encoded.report || 'gyro' in encoded.report || 'hidBytes' in encoded.report) throw new Error('F3 mock claimed IMU or HID encoding');
  const neutral = encodePersonaReport({ ...frame, persona, neutral: true, buttons: 0, axes: {}, triggers: {}, rightStick: { x: 0, y: 0 } });
  if (!neutral.ok || !neutral.report.neutral || neutral.report.persona !== persona || neutral.report.buttons !== 0 || neutral.report.rightStick.x !== 0 || neutral.report.triggers.left !== 0) throw new Error(`F3 current persona mock neutral failed: ${persona}`);
}
for (const persona of ['xbox360', 'dualshock4', 'dualsense', 'disabled', 'physical', 'unknown', null, undefined, 0, true, {}, ['elite']] as unknown[]) {
  const rejected = encodePersonaReport({ ...frame, persona: persona as CanonicalFrame['persona'] });
  if (rejected.ok || rejected.reason !== 'persona-mode-conflict') throw new Error(`F3 unsupported runtime persona encoded: ${String(persona)}`);
}
const invalid = encodePersonaReport({ ...frame, rightStick: { x: Number.NaN, y: 0 } });
if (invalid.ok || invalid.reason !== 'invalid-frame') throw new Error('F3 accepted invalid frame');
console.log('persona report mock selftest: PASS');
