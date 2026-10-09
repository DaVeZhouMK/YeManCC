import { InputCoordinatorMock } from '../src/bridge/inputCoordinatorMock';
import type { ControllerFrame } from '../src/bridge/virtualReportAssembler';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const controller: ControllerFrame = {
  runId: 'run-1', epoch: 1, powerGeneration: 0, targetId: 'target-1', persona: 'xbox360', configRevision: 1,
  inputSequence: 1, timestamp: 10, buttons: 0x20, axes: {}, triggers: {}, rightStick: { x: 0.4, y: -0.2 },
};
const c = new InputCoordinatorMock();
const terminationTraces: Array<{ reason: string; trace: readonly unknown[] }> = [];
c.start(1);
if (c.lifecycle.snapshot().epoch !== c.ownership.snapshot().epoch) throw new Error('start epoch split');
const game = c.submit(controller);
if (game.disposition !== 'game-report' || !game.frame || game.trace.owner !== 'game-consumer') throw new Error('game route integration failed');
const sourceBoundary = new InputCoordinatorMock();
sourceBoundary.start(1, 'physical-pad-1', 'virtual-target-1');
const sourceEpoch = sourceBoundary.lifecycle.snapshot().epoch;
sourceBoundary.submit({ ...controller, epoch: sourceEpoch, inputSequence: 10 });
const sourceRemoved = sourceBoundary.physicalSourceRemove('physical-pad-1');
if (sourceRemoved.sourceAdmission !== 'suppressed' || sourceRemoved.targetSession !== 'retained' || sourceRemoved.virtualTargetIdentity !== 'virtual-target-1' || sourceRemoved.epoch !== sourceEpoch) throw new Error('physical source remove released, rotated lifecycle, or crossed target identity domain');
const sourceBlocked = sourceBoundary.submit({ ...controller, epoch: sourceEpoch, inputSequence: 11 });
if (sourceBlocked.disposition !== 'drop' || sourceBlocked.trace.eventClass !== 'internal' || !sourceBlocked.trace.admissionStopped) throw new Error('physical source no-write gate failed');
const sourceCandidate = sourceBoundary.physicalSourceInsert('physical-pad-2');
if (!sourceCandidate.rearmRequired || sourceCandidate.sourceAdmission !== 'suppressed' || sourceCandidate.epoch !== sourceEpoch) throw new Error('physical source insert auto-rearmed or rotated lifecycle');
const sourceInsertTrace = sourceBoundary.snapshotTrace().find((item) => item.event === 'source-insert-candidate');
if (sourceInsertTrace?.eventClass !== 'physical-source-insert') throw new Error('physical source insert trace classification failed');
const sourceRearmed = sourceBoundary.explicitRearm('game-consumer');
if (sourceRearmed.phase !== 'admitted' || sourceRearmed.sourceAdmission !== 'admitted' || sourceRearmed.epoch !== sourceEpoch + 1 || sourceBoundary.ownership.snapshot().owner !== 'game-consumer') throw new Error('explicit source rearm transaction incomplete');
let stopClassMismatchRejected = false;
const beforeStopClassMismatch = sourceBoundary.lifecycle.snapshot();
try { sourceBoundary.stop('close', 'host-fault'); } catch { stopClassMismatchRejected = true; }
if (!stopClassMismatchRejected) throw new Error('stop reason/event class mismatch was accepted');
const afterStopClassMismatch = sourceBoundary.lifecycle.snapshot();
if (afterStopClassMismatch.epoch !== beforeStopClassMismatch.epoch || afterStopClassMismatch.phase !== beforeStopClassMismatch.phase) throw new Error('rejected stop class mismatch mutated lifecycle state');
const identityBoundary = new InputCoordinatorMock();
identityBoundary.start(1, 'physical-pad-1');
let identityMismatchRejected = false;
try { identityBoundary.physicalSourceRemove('physical-pad-2'); } catch { identityMismatchRejected = true; }
if (!identityMismatchRejected || identityBoundary.lifecycle.snapshot().physicalSourceIdentity !== 'physical-pad-1') throw new Error('physical source identity mismatch was accepted or mutated state');
let topologyAmbiguousRejected = false;
try { identityBoundary.physicalSourceInsert('physical-pad-2'); } catch { topologyAmbiguousRejected = true; }
if (!topologyAmbiguousRejected || identityBoundary.lifecycle.snapshot().physicalSourceIdentity !== 'physical-pad-1') throw new Error('second active source silently replaced selected identity');
c.summon();
if (c.lifecycle.snapshot().epoch !== c.ownership.snapshot().epoch) throw new Error('summon epoch split');
const summonEvents = c.snapshotTrace().filter((item) => item.event.startsWith('summon-'));
if (summonEvents.map((item) => item.event).join(',') !== 'summon-revoke,summon-neutral,summon-commit') throw new Error('summon did not preserve atomic handoff order');
if (summonEvents[0]?.owner !== 'none' || !summonEvents[0]?.admissionStopped || !summonEvents[1]?.neutral || summonEvents[2]?.owner !== 'ymcc-frontend') throw new Error('summon barrier invariants failed');
const foreground = c.submit({ ...controller, epoch: 2, inputSequence: 2 });
if (foreground.disposition !== 'ymcc-semantic-only' || !foreground.frame || foreground.trace.owner !== 'ymcc-frontend' || !foreground.trace.neutral) throw new Error('foreground semantic route integration failed');
if (foreground.trace.gameInputCount !== 0 || foreground.trace.ymccActionCount !== 1) throw new Error('foreground input counters violate single-owner contract');
c.modalOpen();
if (c.lifecycle.snapshot().epoch !== c.ownership.snapshot().epoch) throw new Error('modal epoch split');
const blocked = c.submit({ ...controller, epoch: 3, inputSequence: 3 });
if (blocked.disposition !== 'drop' || blocked.frame !== null) throw new Error('modal frame was not dropped');
c.release();
if (!c.lifecycle.hasCompleteReleaseProof()) throw new Error('coordinator release proof incomplete');
if (!c.snapshotTrace().some((item) => item.event === 'modal-open' && item.gameInputCount === 0 && item.ymccActionCount > 0)) throw new Error('modal trace lacks zero-game-input evidence');

for (const reason of ['close', 'blur', 'suspend', 'virtual-target-fault', 'crash'] as const) {
  const stopped = new InputCoordinatorMock();
  stopped.start(1);
  stopped.summon();
  stopped.submit({ ...controller, epoch: 2, inputSequence: 4 });
  const stoppedState = stopped.stop(reason);
  if (stoppedState.phase !== 'released' || stopped.ownership.snapshot().owner !== 'none' || stopped.ownership.snapshot().releaseRequired) throw new Error(`${reason} did not complete owner release`);
  if (stopped.lifecycle.snapshot().epoch !== stopped.ownership.snapshot().epoch || stopped.lifecycle.snapshot().epoch !== 4) throw new Error(`${reason} did not create an aligned fresh epoch`);
  const end = stopped.snapshotTrace().at(-1);
  if (!stopped.lifecycle.hasCompleteReleaseProof() || !end?.heldCleared || !end?.chordCleared || !end?.shiftCleared || end.event !== `stop-${reason}-complete`) throw new Error(`${reason} did not clear input state`);
  if (reason === 'suspend' && stoppedState.powerGeneration !== 1) throw new Error('suspend did not advance power generation');
  terminationTraces.push({ reason, trace: stopped.snapshotTrace() });
}
const resumed = new InputCoordinatorMock();
resumed.start(1);
resumed.stop('suspend');
const resumedState = resumed.resume('game-consumer');
if (resumedState.phase !== 'admitted' || resumedState.sourceAdmission !== 'admitted' || resumed.ownership.snapshot().owner !== 'game-consumer' || resumedState.epoch !== resumed.ownership.snapshot().epoch) throw new Error('coordinator power resume transaction incomplete');
const restored = new InputCoordinatorMock();
restored.start(1);
restored.summon();
restored.dismiss();
if (restored.ownership.snapshot().owner !== 'game-consumer' || restored.lifecycle.snapshot().epoch !== 3 || restored.ownership.snapshot().epoch !== 3) throw new Error('dismiss did not restore game consumer in a fresh epoch');
const restoredEvents = restored.snapshotTrace().filter((item) => item.event.startsWith('dismiss-'));
if (restoredEvents.map((item) => item.event).join(',') !== 'dismiss-revoke,dismiss-neutral,dismiss-commit') throw new Error('dismiss handoff order failed');
const events = c.snapshotTrace();
if (events.filter((item) => item.inputSequence === 2).length !== 1) throw new Error('frame trace duplicated');
const evidencePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/S-20-input-coordinator-mock-trace.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({
  evidenceId: 'S-20-INPUT-COORDINATOR-MOCK-20260903',
  status: 'PASS',
  systemMutation: false,
  contract: { gameInputCountDuringForeground: 0, ymccActionCountDuringForeground: '>0', physicalSourceRemoveNoWrite: true, physicalInsertRequiresExplicitRearm: true, explicitRearmTransaction: true, powerResumeTransaction: true, topologyAmbiguityRejected: true, neutralBarrier: true, heldChordShiftClearedOnTermination: true, terminationReasons: ['close', 'blur', 'suspend', 'virtual-target-fault', 'crash'] },
  trace: events,
  sourceLifecycleTrace: sourceBoundary.snapshotTrace(),
  resumeTrace: resumed.snapshotTrace(),
  terminationTraces,
}, null, 2));
console.log(`input coordinator mock selftest: PASS (${evidencePath})`);
