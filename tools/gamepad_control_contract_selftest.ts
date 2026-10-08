import {
  GamepadControl,
  createLogicalFrame,
  type ConsumerReceipt,
  type GamepadControlCoordinatorCommand,
  type PhysicalSourceIdentity,
} from '../src/bridge/gamepadControlContract';
import { HardwareCoordinatorPlan } from '../src/bridge/hardwareCoordinator';

const command: GamepadControlCoordinatorCommand = {
  runId: 'stage1-run',
  epoch: 1,
  powerGeneration: 0,
  configRevision: 1,
  ownerIdentity: 'HardwareCoordinatorPlan',
};

// Coordinator owns the only GamepadControl lifecycle call boundary.
const coordinator = new HardwareCoordinatorPlan();
const coordinatedControl = new GamepadControl();
coordinator.attachGamepadControl(coordinatedControl);
coordinator.beginGamepadControl(command);
if (coordinatedControl.snapshot().runId !== command.runId) throw new Error('coordinator did not begin GamepadControl transaction');
coordinator.endGamepadControl(command);
if (coordinatedControl.snapshot().phase !== 'released') throw new Error('coordinator did not release GamepadControl transaction');

const local: PhysicalSourceIdentity = {
  identity: 'physical-local-0',
  kind: 'local',
  physicalEndpointIdentity: 'hid-local-0',
  generation: 1,
};
const external: PhysicalSourceIdentity = {
  identity: 'physical-external-0',
  kind: 'external',
  physicalEndpointIdentity: 'hid-external-0',
  generation: 3,
};

function evidence(control: GamepadControl, game = true): void {
  const state = control.snapshot();
  if (!state.endpoint) throw new Error('test endpoint missing');
  const base = {
    observed: true as const,
    endpointIdentity: state.endpoint.identity,
    endpointEpoch: state.endpoint.endpointEpoch,
  };
  const ymcc: ConsumerReceipt = { ...base, kind: 'ymcc-normal', path: 'normal-os' };
  control.recordConsumerEvidence(ymcc);
  if (game) control.recordConsumerEvidence({ ...base, kind: 'game', path: 'external-observation', processIdentity: 'mock-game' });
}

function neutral(control: GamepadControl, source = local, frameId = 1) {
  const frame = createLogicalFrame(control.snapshot().epoch, {
    frameId,
    sourceIdentity: source.identity,
    sourceGeneration: source.generation,
  });
  control.submitNeutral(frame);
  return frame;
}

function openWithSource(control: GamepadControl, source: PhysicalSourceIdentity = local): void {
  control.beginTransaction(command);
  control.discoverSources([local, external]);
  control.selectPhysicalSource(source.identity);
}

// 1. Physical pass-through: one selected S, one physical E, zero writer.
const passThrough = new GamepadControl();
openWithSource(passThrough);
passThrough.preparePhysicalPassThrough();
neutral(passThrough);
evidence(passThrough);
passThrough.admit();
passThrough.activate();
const physicalFrame = passThrough.submitFrame({
  frameId: 2,
  sourceIdentity: local.identity,
  sourceGeneration: local.generation,
  buttons: { south: true },
});
if (!physicalFrame.accepted || physicalFrame.disposition !== 'no-write') throw new Error('physical pass-through became a writer');
if (passThrough.snapshot().endpoint?.writer !== 'none' || passThrough.snapshot().mode !== 'physical-pass-through') throw new Error('physical endpoint contract failed');

// 2. Two physical candidates still produce one selected source only.
const selected = passThrough.snapshot();
if (selected.sources.filter((source) => source.state === 'physical-selected').length !== 1) throw new Error('multiple selected physical sources');
if (selected.sources.some((source) => source.state === 'physical-selected' && source.identity !== local.identity)) throw new Error('wrong selected source');
if (selected.sources.find((source) => source.identity === external.identity)?.state !== 'physical-suppressed') throw new Error('non-selected external source was not suppressed');

// 2b. Selecting the external source automatically suppresses the local source,
// and exact suppression survives the same-source re-enumeration lifecycle.
const externalSelection = new GamepadControl();
externalSelection.beginTransaction(command);
externalSelection.discoverSources([local, external]);
const selectedExternal = externalSelection.selectPhysicalSource(external.identity);
if (selectedExternal.selectedSource?.sourceIdentity !== external.identity) throw new Error('external source was not selected');
if (selectedExternal.sources.find((source) => source.identity === local.identity)?.state !== 'physical-suppressed') throw new Error('local source was not auto-suppressed');
if (!selectedExternal.suppressedPhysicalSourceIdentities.includes(local.identity)) throw new Error('local suppression policy was not recorded');
const rediscoveredExternal = externalSelection.discoverSources([external, local]);
if (rediscoveredExternal.sources.find((source) => source.identity === local.identity)?.state !== 'physical-suppressed') throw new Error('local suppression was lost after re-enumeration');
if (rediscoveredExternal.sources.find((source) => source.identity === external.identity)?.state !== 'physical-selected') throw new Error('selected external source was lost after re-enumeration');

// A source cannot be selected again until the Coordinator explicitly removes
// its exact admission suppression; unsuppression itself never auto-selects.
externalSelection.unsuppressPhysicalSource(local.identity);
if (externalSelection.snapshot().sources.find((source) => source.identity === local.identity)?.state !== 'physical-local-present') throw new Error('explicit unsuppression did not restore local admission');
externalSelection.selectPhysicalSource(local.identity);
if (externalSelection.snapshot().selectedSource?.sourceIdentity !== local.identity) throw new Error('explicitly re-admitted local source was not selectable');
if (externalSelection.snapshot().sources.find((source) => source.identity === external.identity)?.state !== 'physical-suppressed') throw new Error('old external source was not suppressed after source switch');

// The Coordinator may restore the exact source-admission policy at a new
// transaction boundary; it still does not select a source implicitly.
const restoredPolicy = new GamepadControl();
restoredPolicy.beginTransaction({ ...command, suppressedPhysicalSourceIdentities: [local.identity] });
const restoredPolicySnapshot = restoredPolicy.discoverSources([local, external]);
if (restoredPolicySnapshot.sources.find((source) => source.identity === local.identity)?.state !== 'physical-suppressed') throw new Error('Coordinator suppression policy was not restored');
if (restoredPolicySnapshot.selectedSource !== null || restoredPolicySnapshot.endpoint !== null) throw new Error('restored suppression policy implicitly selected an endpoint');

// A changed generation is a stale binding, even when the display identity is
// unchanged.  The endpoint must be released instead of silently rebinding.
const changedGeneration = new GamepadControl();
changedGeneration.beginTransaction(command);
changedGeneration.discoverSources([local, external]);
changedGeneration.selectPhysicalSource(external.identity);
const staleBinding = changedGeneration.discoverSources([{ ...external, generation: external.generation + 1 }, local]);
if (staleBinding.selectedSource !== null || staleBinding.endpoint !== null || staleBinding.phase !== 'safe-zero') throw new Error('changed source generation did not fail closed');

// 3. Exact suppression never creates a virtual endpoint and never auto-selects the other source.
const suppression = new GamepadControl();
openWithSource(suppression);
const suppressed = suppression.suppressPhysicalSource(local.identity);
if (suppressed.endpoint !== null || suppressed.mode !== 'hold') throw new Error('suppression created or retained public endpoint');
if (suppressed.sources.find((source) => source.identity === local.identity)?.state !== 'physical-suppressed') throw new Error('exact source was not suppressed');
if (suppressed.sources.find((source) => source.identity === external.identity)?.state === 'physical-selected') throw new Error('suppression auto-selected another source');

// 4. Virtual transition has one endpoint, same S, neutral barrier and anti-echo.
const virtual = new GamepadControl();
openWithSource(virtual, external);
virtual.prepareVirtualEndpoint('dualshock4', 'virtual-ds4-0');
neutral(virtual, external);
evidence(virtual);
virtual.admit();
virtual.activate();
const virtualFrame = virtual.submitFrame({
  frameId: 2,
  sourceIdentity: external.identity,
  sourceGeneration: external.generation,
  buttons: { east: true },
});
if (!virtualFrame.accepted || virtualFrame.disposition !== 'virtual-public-write') throw new Error('virtual output did not use the single endpoint');
if (virtual.snapshot().endpoint?.identity !== 'virtual-ds4-0' || virtual.snapshot().endpoint?.sourceIdentity !== external.identity) throw new Error('virtual endpoint identity mismatch');

// 5. Missing consumer evidence fails closed; YMCC projection never becomes game output.
const missingConsumer = new GamepadControl();
openWithSource(missingConsumer);
missingConsumer.prepareVirtualEndpoint('xbox360', 'virtual-x360-0');
neutral(missingConsumer);
missingConsumer.recordConsumerEvidence({
  kind: 'ymcc-normal', path: 'normal-os', observed: true,
  endpointIdentity: missingConsumer.snapshot().endpoint!.identity,
  endpointEpoch: missingConsumer.snapshot().endpoint!.endpointEpoch,
});
missingConsumer.admit();
if (missingConsumer.snapshot().mode !== 'hold' || missingConsumer.snapshot().failClosedReason !== 'admit-without-consumer-evidence') throw new Error('missing consumer evidence did not fail closed');

// 6. Lease keeps B alive, suppresses public virtual writes, and separates counters.
const lease = new GamepadControl();
openWithSource(lease);
lease.prepareVirtualEndpoint('xbox360', 'virtual-x360-lease');
neutral(lease);
evidence(lease);
lease.admit();
lease.activate();
lease.requestControlLease('ymcc-lease-1');
const leaseFrame = createLogicalFrame(lease.snapshot().epoch, {
  frameId: 3, sourceIdentity: local.identity, sourceGeneration: local.generation, buttons: { menu: true },
});
if (lease.submitFrame({ frameId: 3, sourceIdentity: local.identity, sourceGeneration: local.generation, buttons: { menu: true } }).disposition !== 'b-projection-only') throw new Error('lease did not suppress endpoint write');
lease.consumeProjection(leaseFrame, 'open-menu');
lease.releaseControlLease(neutral(lease, local, 4));
if (lease.snapshot().projectPublishCount !== 1 || lease.snapshot().externalConsumerInputCount !== 0) throw new Error('consumer counters were conflated');

// 7. Anti-echo failure closes the valve and never reads virtual E as S.
const antiEcho = new GamepadControl();
openWithSource(antiEcho);
antiEcho.prepareVirtualEndpoint('xbox360', 'virtual-echo');
neutral(antiEcho);
evidence(antiEcho);
antiEcho.admit();
antiEcho.activate();
const echoResult = antiEcho.submitFrame({ frameId: 5, sourceIdentity: 'virtual-echo', sourceGeneration: 1, buttons: { south: true } });
if (echoResult.accepted || antiEcho.snapshot().mode !== 'hold' || antiEcho.snapshot().phase !== 'faulted') throw new Error('virtual self-echo was not fail-closed');

// 8. Releasing virtual output restores physical pass-through in a fresh endpoint epoch.
const restore = new GamepadControl();
openWithSource(restore);
restore.prepareVirtualEndpoint('dualshock4', 'virtual-restore');
neutral(restore);
evidence(restore);
restore.admit();
restore.activate();
const oldEpoch = restore.snapshot().endpoint!.endpointEpoch;
restore.disableVirtual(neutral(restore, local, 6));
if (restore.snapshot().endpoint?.kind !== 'physical' || restore.snapshot().endpoint?.writer !== 'none' || restore.snapshot().endpoint!.endpointEpoch <= oldEpoch) throw new Error('virtual disable did not restore physical endpoint safely');
neutral(restore, local, 7);
evidence(restore);
restore.admit();
restore.activate();
const restoredFrame = restore.submitFrame({
  frameId: 8,
  sourceIdentity: local.identity,
  sourceGeneration: local.generation,
  buttons: { north: true },
});
if (!restoredFrame.accepted || restoredFrame.disposition !== 'no-write' || restore.snapshot().mode !== 'physical-pass-through') throw new Error('restored physical endpoint did not resume pass-through');

console.log('gamepad control contract selftest: PASS');
