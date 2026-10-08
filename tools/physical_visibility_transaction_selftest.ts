import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  PhysicalVisibilityTransaction,
  type VisibilityBackendV1,
  type VisibilityTransactionRequestV1,
} from '../src/bridge/physicalVisibilityTransaction';

const request: VisibilityTransactionRequestV1 = {
  schemaVersion: 1, runId: 't13-run', requestId: 't13-request', epoch: 30, powerGeneration: 6,
  owner: 'none', targetId: 'virtual-ds4-1', persona: 'dualshock4', configRevision: 50,
  configHash: 'sha256:'.padEnd(71, 'c'), providerId: 'windows-imu', calibrationId: 'calibration-50',
  visibilityTransactionId: 'visibility-50', timestampUtc: '2026-09-04T12:30:00.000Z',
  ymccAllowlist: ['C:\\SOFT\\YeMan\\YeManCC\\YeManCC.exe', 'C:\\SOFT\\YeMan\\YeManCC\\InputHost\\YeManInputHost.exe'],
  physical: {
    schemaVersion: 1, source: 'hc-device-manager-xusb-pnp', controllerId: 'rog-xbox-adaptive-0',
    deviceInstanceId: 'HID\\VID_0B05&PID_1B4C&IG_00\\PHYSICAL', baseContainerDeviceInstanceId: 'USB\\VID_0B05&PID_1B4C\\CONTAINER',
    vid: 0x0b05, pid: 0x1b4c, physicalIdentity: 'rog:fixture',
  },
  virtualTarget: {
    targetId: 'virtual-ds4-1', persona: 'dualshock4', deviceInstanceId: 'ROOT\\HIDMAESTRO\\DS4', baseContainerDeviceInstanceId: 'ROOT\\HIDMAESTRO\\DS4',
  },
};

const calls: string[] = [];
let snapshot = { schemaVersion: 1 as const, hiddenBefore: ['HID\\OTHER'], allowlistBefore: [], cloakingBefore: false };
const backend: VisibilityBackendV1 = {
  captureSnapshot: () => ({ ...snapshot, hiddenBefore: [...snapshot.hiddenBefore], allowlistBefore: [...snapshot.allowlistBefore] }),
  ensureAllowlist: (paths) => { calls.push(`allow:${paths.length}`); snapshot = { ...snapshot, allowlistBefore: [...paths], cloakingBefore: true }; return true; },
  hidePath: (path) => { calls.push(`hide:${path}`); return true; },
  unhidePath: (path) => { calls.push(`unhide:${path}`); return true; },
  cyclePort: (path) => { calls.push(`cycle:${path}`); return true; },
  restoreSnapshot: (before) => { calls.push('restore-snapshot'); snapshot = { ...before, hiddenBefore: [...before.hiddenBefore], allowlistBefore: [...before.allowlistBefore] }; return true; },
};
const transaction = new PhysicalVisibilityTransaction();
const missingJournal = new PhysicalVisibilityTransaction().restore(backend);
if (missingJournal.state !== 'release-incomplete' || missingJournal.closure !== 'UNENCLOSED' ||
  !missingJournal.trace.some((event) => event.event === 'restore-journal-missing')) {
  throw new Error('T13 turned an unbound restore into a release receipt');
}
const hidden = transaction.apply(request, backend);
const expectedTargets = [request.physical.baseContainerDeviceInstanceId, request.physical.deviceInstanceId];
if (hidden.state !== 'hidden' || !hidden.virtualTargetExcluded || JSON.stringify(hidden.targetIds) !== JSON.stringify(expectedTargets)) {
  throw new Error('T13 did not plan HC container + instance targets with virtual exclusion');
}
if (calls.some((call) => call.includes('HIDMAESTRO')) || calls.filter((call) => call.startsWith('hide:')).length !== 2 || calls.filter((call) => call.startsWith('cycle:')).length !== 2) {
  throw new Error('T13 hid a virtual target or skipped an HC-style hide/cycle action');
}
if (!hidden.trace.every((event) => event.pXinput === 'UNENCLOSED')) throw new Error('T13 incorrectly claimed HidHide can close XInput');
const restored = transaction.restore(backend);
if (restored.state !== 'restored' || calls.filter((call) => call.startsWith('unhide:')).length !== 2 || calls.filter((call) => call.startsWith('cycle:')).length !== 4 || calls.filter((call) => call === 'restore-snapshot').length !== 1) {
  throw new Error('T13 did not restore its hidden diff, cycle it, and restore the before snapshot');
}
if (calls.indexOf(`unhide:${request.physical.baseContainerDeviceInstanceId}`) > calls.indexOf(`unhide:${request.physical.deviceInstanceId}`)) {
  throw new Error('T13 restore order diverges from HC UnhideHID container -> instance order');
}

const ownerViolation = new PhysicalVisibilityTransaction().apply({ ...request, owner: 'game-consumer', requestId: 't13-owner-negative', visibilityTransactionId: 'visibility-owner-negative' }, backend);
if (ownerViolation.state !== 'rejected' || !ownerViolation.trace.some((event) => event.reason === 'identity-owner-or-allowlist-invalid')) {
  throw new Error('T13 admitted P-HID mutation before Coordinator neutral/owner=none');
}

const invalidSnapshotBackend: VisibilityBackendV1 = {
  ...backend,
  captureSnapshot: () => ({ schemaVersion: 2, hiddenBefore: [], allowlistBefore: [], cloakingBefore: false } as unknown as ReturnType<VisibilityBackendV1['captureSnapshot']>),
};
const invalidSnapshot = new PhysicalVisibilityTransaction().apply({ ...request, requestId: 't13-snapshot-negative', visibilityTransactionId: 'visibility-snapshot-negative' }, invalidSnapshotBackend);
if (invalidSnapshot.state !== 'rejected' || !invalidSnapshot.trace.some((event) => event.reason === 'visibility-snapshot-invalid')) {
  throw new Error('T13 admitted a mutation without a valid before snapshot journal');
}

const virtualIdentityRequest: VisibilityTransactionRequestV1 = {
  ...request,
  requestId: 't13-virtual-negative', visibilityTransactionId: 'visibility-negative',
  virtualTarget: { ...request.virtualTarget, deviceInstanceId: request.physical.deviceInstanceId },
};
const rejected = new PhysicalVisibilityTransaction().apply(virtualIdentityRequest, backend);
if (rejected.state !== 'rejected' || rejected.virtualTargetExcluded) throw new Error('T13 did not reject a virtual target in the hide list');

const failedCalls: string[] = [];
const rollbackBackend: VisibilityBackendV1 = {
  captureSnapshot: backend.captureSnapshot,
  ensureAllowlist: () => true,
  hidePath: (path) => { failedCalls.push(`hide:${path}`); return failedCalls.filter((item) => item.startsWith('hide:')).length === 1; },
  unhidePath: (path) => { failedCalls.push(`unhide:${path}`); return true; },
  cyclePort: (path) => { failedCalls.push(`cycle:${path}`); return true; },
  restoreSnapshot: (before) => { failedCalls.push('restore-snapshot'); snapshot = { ...before, hiddenBefore: [...before.hiddenBefore], allowlistBefore: [...before.allowlistBefore] }; return true; },
};
const rolledBack = new PhysicalVisibilityTransaction().apply({ ...request, requestId: 't13-rollback', visibilityTransactionId: 'visibility-rollback' }, rollbackBackend);
if (rolledBack.state !== 'restored' || !failedCalls.some((call) => call.startsWith('unhide:')) || !failedCalls.includes('restore-snapshot')) throw new Error('T13 hide failure did not restore its diff and before snapshot');

let unverifiedCaptureCount = 0;
const unverifiedSnapshotBackend: VisibilityBackendV1 = {
  ...backend,
  restoreSnapshot: () => true,
  captureSnapshot: () => {
    unverifiedCaptureCount += 1;
    return unverifiedCaptureCount === 1
      ? { schemaVersion: 1, hiddenBefore: ['HID\\OTHER'], allowlistBefore: [], cloakingBefore: false }
      : { schemaVersion: 1, hiddenBefore: [], allowlistBefore: ['C:\\MUTATED.exe'], cloakingBefore: true };
  },
};
const unverifiedTransaction = new PhysicalVisibilityTransaction();
const unverifiedHidden = unverifiedTransaction.apply({ ...request, requestId: 't13-restore-negative', visibilityTransactionId: 'visibility-restore-negative' }, unverifiedSnapshotBackend);
if (unverifiedHidden.state !== 'hidden') throw new Error('T13 exact-restore negative did not enter the mock hidden state');
const releaseIncomplete = unverifiedTransaction.restore(unverifiedSnapshotBackend);
if (releaseIncomplete.state !== 'release-incomplete' || !releaseIncomplete.trace.some((event) => event.event === 'restore-incomplete')) {
  throw new Error('T13 claimed restore without an exact after-snapshot receipt');
}

const evidence = {
  evidenceId: 'T13-E01-PHYSICAL-VISIBILITY-TRANSACTION-MOCK-20260904', status: 'MOCK-READY', systemMutation: false,
  contract: '30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md#12',
  assertions: {
    hcResolvedIdentityOnly: true, instanceAndContainerOnly: true, ymccAllowlistRequired: true,
    virtualTargetExcluded: true, hideThenCycle: true, restoreThenCycle: true, exactSnapshotRestore: true,
    pXinputUnenclosed: true, rogOemDisableAbsentFromTransaction: true,
  },
  trace: transaction.trace(), calls, failedCalls,
};
const evidencePath = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/T13-E01-physical-visibility-transaction-mock-20260904.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify(evidence, null, 2));
console.log(`physical visibility transaction selftest: PASS (${evidencePath})`);
