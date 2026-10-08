import { assembleCanonicalFrame, neutralCanonicalFrame, type ControllerFrame, type MotionFrame } from '../src/bridge/virtualReportAssembler';
import { applyHcGyroAxisModifiers, blendGyroShortIntoStick, hcShortToUnit, hcUnitToShort, HC_DEFAULT_AXIS_ANTI_DEADZONE, HC_DEFAULT_GYRO_WEIGHT, improveSquare } from '../src/bridge/hcInputUtils';

const controller: ControllerFrame = {
  runId: 'run-1', epoch: 3, powerGeneration: 7, targetId: 'target-1', persona: 'xbox360', configRevision: 4,
  inputSequence: 12, timestamp: 100, buttons: 0x20, axes: { leftX: 0.25 }, triggers: { left: 0.7 }, rightStick: { x: 0.4, y: -0.2 },
};
const motion: MotionFrame = { runId: 'run-1', epoch: 3, powerGeneration: 7, targetId: 'target-1', configRevision: 4, sampleSequence: 9, timestamp: 101, valid: true, contribution: { x: 0.2, y: -0.3 } };
const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;

const blend = { gyroWeight: HC_DEFAULT_GYRO_WEIGHT, innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: HC_DEFAULT_AXIS_ANTI_DEADZONE, outputShape: 'default' as const };
const merged = assembleCanonicalFrame(controller, motion, blend);
const modified = applyHcGyroAxisModifiers({ x: hcUnitToShort(0.2), y: hcUnitToShort(-0.3) }, blend);
const expectedStick = blendGyroShortIntoStick({ x: hcUnitToShort(0.4), y: hcUnitToShort(-0.2) }, modified, HC_DEFAULT_GYRO_WEIGHT);
if (!merged.ok || !near(merged.frame.rightStick.x, hcShortToUnit(expectedStick.x)) || !near(merged.frame.rightStick.y, hcShortToUnit(expectedStick.y))) throw new Error('S-27 HC LayoutManager blend failed');
if (merged.frame.buttons !== 0x20 || !near(merged.frame.triggers.left, 0.7) || !near(merged.frame.rightStickBase.x, 0.4) || !near(merged.frame.rightStickBase.y, -0.2)) throw new Error('S-27 physical controls were dropped');
if (Math.abs(merged.frame.rightStick.x - 0.6) < 1e-6 && Math.abs(merged.frame.rightStick.y + 0.5) < 1e-6) throw new Error('S-27 still used invented base+contribution add');

const stale = assembleCanonicalFrame(controller, { ...motion, epoch: 2 });
if (stale.ok || stale.reason !== 'frame-stale') throw new Error('stale motion was admitted');
const invalid = assembleCanonicalFrame(controller, { ...motion, valid: false });
if (invalid.ok || invalid.reason !== 'motion-invalid') throw new Error('invalid motion was admitted');
const invalidControllerTime = assembleCanonicalFrame({ ...controller, timestamp: Number.NaN });
if (invalidControllerTime.ok || invalidControllerTime.reason !== 'invalid-value') throw new Error('non-finite controller timestamp was admitted');
const invalidInputSequence = assembleCanonicalFrame({ ...controller, inputSequence: -1 });
if (invalidInputSequence.ok || invalidInputSequence.reason !== 'invalid-value') throw new Error('negative controller input sequence was admitted');
const invalidMotionTime = assembleCanonicalFrame(controller, { ...motion, timestamp: Number.NaN });
if (invalidMotionTime.ok || invalidMotionTime.reason !== 'motion-invalid') throw new Error('non-finite motion timestamp was admitted');
const invalidSampleSequence = assembleCanonicalFrame(controller, { ...motion, sampleSequence: -1 });
if (invalidSampleSequence.ok || invalidSampleSequence.reason !== 'motion-invalid') throw new Error('negative motion sample sequence was admitted');
const baseOnly = assembleCanonicalFrame(controller);
if (!baseOnly.ok || !near(baseOnly.frame.rightStick.x, 0.4) || !near(baseOnly.frame.rightStick.y, -0.2)) throw new Error('gyro-off changed base stick');
if (!neutralCanonicalFrame(controller).neutral) throw new Error('neutral frame missing flag');

const inner = assembleCanonicalFrame(controller, { ...motion, contribution: { x: 0.02, y: 0 }, gyroShort: { x: 655, y: 0 } }, { gyroWeight: 1.2, innerDeadzone: 10, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
if (!inner.ok || inner.frame.gyroContribution.x !== 0 || inner.frame.gyroContribution.y !== 0) throw new Error('T5 inner deadzone did not use HC radial deadzone');
if (!near(inner.frame.rightStick.x, hcShortToUnit(hcUnitToShort(0.4))) || !near(inner.frame.rightStick.y, hcShortToUnit(hcUnitToShort(-0.2)))) throw new Error('T5 zeroed gyro overwrote physical stick');

const square = improveSquare({ x: hcUnitToShort(0.6), y: hcUnitToShort(0.2) });
if (Math.abs(hcShortToUnit(square.x) - 0.66) < 1e-6 && Math.abs(hcShortToUnit(square.y) - 0.22) < 1e-6) throw new Error('ImproveSquare still used invented *1.1');
console.log('assisted stick assembler selftest: PASS');
