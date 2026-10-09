import { FeedbackParticipantMock } from '../src/bridge/feedbackParticipantMock';
import { assembleCanonicalFrame, type ControllerFrame } from '../src/bridge/virtualReportAssembler';

const fb = new FeedbackParticipantMock();
const missing = fb.verify('none', 't1', 1, 1);
if (missing.phase !== 'disabled' || missing.transportWrites !== 0) throw new Error('T6 missing capability was not disabled');
if (fb.rumble({ targetId: 't1', epoch: 1, configRevision: 1, largeMotor: 255, smallMotor: 255, delayMs: 125 }).reason !== 'capability-missing') throw new Error('T6 rumble without capability leaked');

const ready = fb.verify('rumble', 't1', 3, 4);
if (ready.phase !== 'capability-verified') throw new Error('T6 rumble capability not verified');
fb.ready();
const first = fb.rumble({ targetId: 't1', epoch: 3, configRevision: 4, largeMotor: 200, smallMotor: 10, delayMs: 125 });
if (first.phase !== 'active' || first.lastVibration.large !== 200 || first.cancelledPrevious) throw new Error('T6 first rumble failed');
const second = fb.rumble({ targetId: 't1', epoch: 3, configRevision: 4, largeMotor: 1, smallMotor: 2, delayMs: 40 });
if (!second.cancelledPrevious || second.lastVibration.small !== 2) throw new Error('T6 did not cancel previous rumble');
if (fb.rumble({ targetId: 't1', epoch: 2, configRevision: 4, largeMotor: 255, smallMotor: 255, delayMs: 10 }).reason !== 'stale-command') throw new Error('T6 accepted stale epoch');
const haptic = fb.setHaptic('high', 'b1');
if (haptic.lastVibration.large !== 0 || haptic.lastVibration.small !== 255 || haptic.lastHapticReleased) throw new Error('T6 face-button haptic did not use right motor');
const directional = fb.setHaptic('medium', 'rightPadClickRight', true);
if (directional.lastVibration.large !== 0 || directional.lastVibration.small !== 255 || !directional.lastHapticReleased) throw new Error('T6 right-pad directional/released haptic parity failed');
const stopped = fb.stopRumble();
if (!stopped.safeZeroSent || stopped.lastVibration.large !== 0 || stopped.lastVibration.small !== 0) throw new Error('T6 StopRumble did not safe-zero');
const suspended = fb.suspend();
if (suspended.phase !== 'safe-zero' || !suspended.safeZeroSent) throw new Error('T6 suspend missed safe-zero');
fb.release();
if (fb.snapshot(null).phase !== 'released') throw new Error('T6 release incomplete');

const controller: ControllerFrame = {
  runId: 'run-1', epoch: 3, powerGeneration: 7, targetId: 'target-1', persona: 'xbox360', configRevision: 4,
  inputSequence: 1, timestamp: 1, buttons: 0, axes: {}, triggers: {}, rightStick: { x: 0.25, y: 0 },
};
const assembled = assembleCanonicalFrame(controller);
if (!assembled.ok || assembled.frame.rightStick.x !== 0.25) throw new Error('T6 feedback mock changed T5 stick path');
if (fb.snapshot(null).transportWrites !== 0) throw new Error('T6 mock wrote a transport');
console.log('feedback participant mock selftest: PASS');
