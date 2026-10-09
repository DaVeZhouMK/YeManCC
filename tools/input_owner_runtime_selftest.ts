import { InputOwnerRuntime } from '../src/bridge/inputOwnerRuntime';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
const owner = new InputOwnerRuntime();
if (owner.start().owner !== 'game-consumer') throw new Error('runtime owner did not start in game mode');
const foreground = owner.summon();
if (foreground.owner !== 'ymcc-frontend' || foreground.gameInputCount !== 0 || foreground.ymccActionCount !== 0) throw new Error('foreground commit invariant failed');
const action = owner.semanticAction();
if (action.gameInputCount !== 0 || action.ymccActionCount !== 1 || action.heldCleared || action.chordCleared || action.shiftCleared) throw new Error('semantic action accounting failed');
const beforeSourceRemove = owner.snapshot();
const sourceRemoved = owner.sourceRemoved();
if (sourceRemoved.epoch !== beforeSourceRemove.epoch || sourceRemoved.owner !== 'ymcc-frontend' || sourceRemoved.sourceAdmission || sourceRemoved.sourcePresent || !sourceRemoved.rearmRequired || !sourceRemoved.heldCleared || !sourceRemoved.chordCleared || !sourceRemoved.shiftCleared) throw new Error('physical source removal incorrectly released owner/epoch');
if (owner.semanticAction().ymccActionCount !== 0) throw new Error('source-suppressed semantic action was accepted');
const sourceInserted = owner.sourceInserted();
if (sourceInserted.sourceAdmission || !sourceInserted.sourcePresent || !sourceInserted.rearmRequired) throw new Error('source insert auto-rearmed owner');
owner.stop('blur');
const blockedStart = owner.start();
if (blockedStart.owner !== 'none' || blockedStart.sourceAdmission || !blockedStart.rearmRequired || blockedStart.epoch !== sourceInserted.epoch + 1) throw new Error('focus/start path bypassed explicit source rearm');
const rearmed = owner.rearmSource();
if (!rearmed.sourceAdmission || rearmed.epoch !== beforeSourceRemove.epoch + 2) throw new Error('explicit source rearm invariant failed');
const stopped = owner.stop('blur');
if (stopped.owner !== 'none' || stopped.gameInputCount !== 0 || stopped.ymccActionCount !== 0 || !stopped.heldCleared || !stopped.chordCleared || !stopped.shiftCleared || stopped.epoch !== 5 || stopped.sourceAdmission) throw new Error('stop recovery invariant failed');
const evidencePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/S-21-input-owner-runtime-trace.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({
  evidenceId: 'S-21-INPUT-OWNER-RUNTIME-20260903', status: 'PASS', systemMutation: false,
  contract: { foregroundGameInputCount: 0, foregroundYmccActionCount: '>0', physicalSourceRemoveNoRelease: true, insertRequiresExplicitRearm: true, stopOwner: 'none', freshEpoch: true, heldChordShiftCleared: true },
  snapshots: { start: { owner: 'game-consumer', epoch: 1 }, foreground, action, beforeSourceRemove, sourceRemoved, sourceInserted, rearmed, stopped },
  limitation: 'Renderer mirror does not revoke another process XInput access; external game-consumer isolation remains UNENCLOSED.',
}, null, 2));
console.log(`input owner runtime selftest: PASS (${evidencePath})`);
