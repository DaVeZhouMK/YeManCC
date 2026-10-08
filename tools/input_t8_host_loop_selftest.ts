import { InputCoordinatorMock } from '../src/bridge/inputCoordinatorMock';
import { ExternalInputAdapterMock } from '../src/bridge/externalInputAdapterMock';
import { FeedbackParticipantMock } from '../src/bridge/feedbackParticipantMock';
import { mapGyroSample } from '../src/bridge/gyroMotionMapperMock';
import { assembleCanonicalFrame, type ControllerFrame } from '../src/bridge/virtualReportAssembler';
import { encodePersonaReport } from '../src/bridge/personaReportMock';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const coordinator = new InputCoordinatorMock();
const adapter = new ExternalInputAdapterMock();
const feedback = new FeedbackParticipantMock();
coordinator.start(4);
if (!adapter.create(coordinator.lifecycle.snapshot().epoch).ok) throw new Error('T8 adapter create failed');
feedback.verify('rumble', 'target-1', coordinator.lifecycle.snapshot().epoch, 4);
feedback.ready();

const controller: ControllerFrame = {
  runId: 'run-t8', epoch: coordinator.lifecycle.snapshot().epoch, powerGeneration: coordinator.lifecycle.snapshot().powerGeneration,
  targetId: 'target-1', persona: 'xbox360', configRevision: 4, inputSequence: 1, timestamp: 10,
  buttons: 0x10, axes: { leftX: 0.2, leftY: 0 }, triggers: { left: 0, right: 0 }, rightStick: { x: 0.3, y: -0.1 },
};
const motionConfig = {
  schemaVersion: 1 as const, revision: 1, closure: 'CLOSED' as const, provider: 'windows-imu', unit: 'deg/s',
  calibrationId: 't8', outputMode: 'virtual-stick' as const, motionMode: 'on' as const, motionInput: 'local-space' as const,
  gyroMultiplier: 1, motionSensitivityX: 1, motionSensitivityY: 1, invertHorizontal: false, invertVertical: false,
};
const mapped = mapGyroSample(
  { runId: controller.runId, epoch: controller.epoch, powerGeneration: controller.powerGeneration, targetId: controller.targetId, configRevision: controller.configRevision },
  motionConfig,
  { sequence: 1, timestamp: 10, gamepadMotionPlane: { processProven: true, pairProven: true, calibrationLocked: true, steeringAxis: 'roll', defaultGyro: { x: 20, y: 0, z: 8 }, defaultAccel: { x: 0, y: 0, z: 1 } } },
);
if (!mapped.ok) throw new Error('T8 motion mapping failed');
const assembled = assembleCanonicalFrame(controller, mapped.frame, { gyroWeight: 1.2, innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default' });
if (!assembled.ok) throw new Error('T8 assemble failed');
if (Math.abs(assembled.frame.rightStickBase.x - 0.3) > 1e-9 || assembled.frame.buttons !== 0x10) throw new Error('T8 lost physical controls');
const encoded = encodePersonaReport(assembled.frame);
if (!encoded.ok || encoded.report.rightStick.x === 0.3 && mapped.frame.contribution.x !== 0) throw new Error('T8 encoded gyro-off stick');
if (!adapter.submit(encoded.report).ok) throw new Error('T8 first submit failed');

const gyroOff = assembleCanonicalFrame(controller);
if (!gyroOff.ok || Math.abs(gyroOff.frame.rightStick.x - 0.3) > 1e-9) throw new Error('T8 gyro-off changed base stick');
const gyroOffReport = encodePersonaReport(gyroOff.frame);
if (!gyroOffReport.ok || !adapter.submit(gyroOffReport.report).ok) throw new Error('T8 gyro-off submit failed');

adapter.failOnceForTest();
const failed = adapter.submit(encoded.report);
if (failed.ok || failed.reason !== 'transport-failed' || !adapter.snapshot().released) throw new Error('T8 transport failure did not release');

const coordinator2 = new InputCoordinatorMock();
const adapter2 = new ExternalInputAdapterMock();
coordinator2.start(5);
const epoch = coordinator2.lifecycle.snapshot().epoch;
adapter2.create(epoch);
const live: ControllerFrame = { ...controller, epoch, configRevision: 5 };
const liveSubmit = coordinator2.submit(live, mapped.ok ? { ...mapped.frame, epoch, configRevision: 5 } : undefined);
if (liveSubmit.disposition !== 'game-report' || !liveSubmit.frame) throw new Error('T8 game owner did not consume frame');
const encodedLive = encodePersonaReport(liveSubmit.frame);
if (!encodedLive.ok || !adapter2.submit(encodedLive.report).ok) throw new Error('T8 owner-path submit failed');

coordinator2.summon();
const frontend = coordinator2.submit({ ...live, epoch: coordinator2.lifecycle.snapshot().epoch }, undefined);
if (frontend.disposition !== 'ymcc-semantic-only' || coordinator2.snapshotTrace().at(-1)?.gameInputCount !== 0) throw new Error('T8 summon did not isolate game input');
adapter2.abort(epoch, 'summon-neutral');
if (!adapter2.snapshot().released) throw new Error('T8 summon did not release previous writer');

feedback.suspend();
if (!feedback.snapshot(null).safeZeroSent) throw new Error('T8 feedback suspend missed zero');
coordinator2.stop('suspend');
if (coordinator2.lifecycle.snapshot().phase !== 'released' && coordinator2.lifecycle.snapshot().phase !== 'stopped') {
  const phase = coordinator2.lifecycle.snapshot().phase;
  if (!['released', 'stopped', 'idle'].includes(phase) && coordinator2.snapshotTrace().every((item) => item.event !== 'stop-suspend-complete')) {
    throw new Error(`T8 suspend incomplete phase=${phase}`);
  }
}

const evidencePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/S-28-input-t8-host-loop-mock-trace.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({
  evidenceId: 'S-28-INPUT-T8-HOST-LOOP-MOCK-20260904',
  status: 'PASS',
  systemMutation: false,
  hidMaestroInvoked: false,
  assertions: {
    physicalStickPreserved: true,
    gyroOffUnchanged: true,
    transportFailureReleased: true,
    summonGameInputCountZero: true,
    feedbackSafeZero: true,
  },
}, null, 2));
console.log(`input T8 host loop mock selftest: PASS (${evidencePath})`);
