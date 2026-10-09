import { encodePersonaReport } from '../src/bridge/personaReportMock';
import type { CanonicalFrame } from '../src/bridge/virtualReportAssembler';

const frame: CanonicalFrame = {
  schemaVersion: 1, runId: 'run', epoch: 2, powerGeneration: 1, targetId: 'target', persona: 'xbox360', configRevision: 1,
  inputSequence: 5, sampleSequence: 4, timestamp: 100, neutral: false, buttons: 0x1000,
  axes: { leftX: 0.25, leftY: -0.5 }, triggers: { left: 0.8, right: 0.1 },
  rightStickBase: { x: 0.2, y: -0.1 }, gyroContribution: { x: 0.2, y: 0.1 }, rightStick: { x: 0.4, y: 0 },
};
const xbox = encodePersonaReport(frame);
if (!xbox.ok || xbox.report.persona !== 'xbox360' || xbox.report.outputMode !== 'virtual-stick' || xbox.report.rightStick.x !== 0.4) throw new Error('F3 xbox encoding failed');
const ds4 = encodePersonaReport({ ...frame, persona: 'dualshock4' });
if (!ds4.ok || ds4.report.persona !== 'dualshock4' || 'imu' in ds4.report) throw new Error('F3 DS4 virtual-stick separation failed');
const invalid = encodePersonaReport({ ...frame, rightStick: { x: Number.NaN, y: 0 } });
if (invalid.ok || invalid.reason !== 'invalid-frame') throw new Error('F3 accepted invalid frame');
console.log('persona report mock selftest: PASS');
