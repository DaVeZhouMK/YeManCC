import { FanCoordinatorGate, HardwareCoordinatorPlan, type OwnerReleaseProof } from '../src/bridge/hardwareCoordinator';

function expect(value: unknown, message: string): void {
  if (!value) throw new Error(message);
}

function expectThrow(action: () => void, message: string): void {
  let thrown = false;
  try { action(); } catch { thrown = true; }
  expect(thrown, message);
}

const releaseProof: OwnerReleaseProof = {
  ownerIdentity: 'fan-host',
  sessionRetired: true,
  listenerReleased: true,
  writeRouteReleased: true,
  oemRestoreAttempted: true,
};

const coordinator = new HardwareCoordinatorPlan();
coordinator.register({ id: 'fan-host', resources: ['fan'], enabled: true, implementation: 'fan-host' });
coordinator.register({ id: 'input-host', resources: ['controller', 'motion', 'virtual-gamepad', 'hidhide'], enabled: false, implementation: 'input-host-placeholder' });

expect(coordinator.admit({ requestId: 'legacy-read', callerIdentity: 'fan-host', epoch: 'none', resource: 'fan', kind: 'read' }).reason === 'legacy-direct-hc-not-routed', 'LegacyDirectHC must reject platform requests');
expectThrow(() => coordinator.switchOwnerMode('platform-owner', null), 'Mode switch requires structured old-owner release proof');
coordinator.switchOwnerMode('platform-owner', releaseProof);

coordinator.beginPlatformSession('epoch-1', 'platform-host', 1);
expect(coordinator.admit({ requestId: 'before-ready', callerIdentity: 'fan-host', epoch: 'epoch-1', resource: 'fan', kind: 'write' }).reason === 'lifecycle-not-ready:resuming', 'Writes must wait for HC readiness');
coordinator.admitHcSystemReady('epoch-1');
expect(coordinator.admit({ requestId: 'fan-write', callerIdentity: 'fan-host', epoch: 'epoch-1', resource: 'fan', kind: 'write' }).accepted, 'Fan owner should write after HC readiness');
expect(coordinator.admit({ requestId: 'fan-write', callerIdentity: 'fan-host', epoch: 'epoch-1', resource: 'fan', kind: 'write' }).duplicate, 'Same request must be idempotent');

expect(coordinator.admit({ requestId: 'input-read', callerIdentity: 'input-host', epoch: 'epoch-1', resource: 'motion', kind: 'read' }).reason === 'resource-owner-disabled', 'Reserved input resources must remain disabled');
coordinator.setParticipantEnabled('input-host', true);
expect(coordinator.admit({ requestId: 'input-read-enabled', callerIdentity: 'input-host', epoch: 'epoch-1', resource: 'motion', kind: 'read' }).accepted, 'Enabled participant may read only its resource');
coordinator.setParticipantEnabled('input-host', false);

coordinator.observeNativeLifecycle('system-pending', 2);
expect(coordinator.admit({ requestId: 'late-write', callerIdentity: 'fan-host', epoch: 'epoch-1', resource: 'fan', kind: 'write' }).reason === 'lifecycle-not-ready:suspended', 'Sleep must reject late commands');
coordinator.beginPlatformSession('epoch-2', 'platform-host', 2);
coordinator.admitHcSystemReady('epoch-2');
expect(coordinator.admit({ requestId: 'fan-write', callerIdentity: 'fan-host', epoch: 'epoch-2', resource: 'fan', kind: 'write' }).accepted, 'Retired request cache must not cross a new epoch');

coordinator.observeNativeLifecycle('owner-crash', 3);
coordinator.beginPlatformSession('epoch-3', 'platform-host', 3);
expectThrow(() => coordinator.admitHcSystemReady('epoch-3'), 'Crash recovery must not reuse automatic write admission');
coordinator.explicitRearm('epoch-3');
expect(coordinator.admit({ requestId: 'rearmed-write', callerIdentity: 'fan-host', epoch: 'epoch-3', resource: 'fan', kind: 'write' }).accepted, 'Explicit abnormal recovery rearm should admit a new write');
expectThrow(() => coordinator.explicitRearm('epoch-3'), 'Normal ready state must not accept a second explicit rearm');

// FanHost remains the actual HC device owner. The fan-only gate has no process
// or hardware dependency; it rejects writes until that owner reports a fresh
// HC-ready session for the current native power generation.
const fanGate = new FanCoordinatorGate();
fanGate.beginFanHostSession('fan-host:1', 0);
expect(!fanGate.admitFanWrite('fan-before-ready', 'fan-host:1', 0).accepted, 'Fan writes must wait for HC readiness');
fanGate.admitHcReady('fan-host:1', 0);
expect(fanGate.admitFanWrite('fan-enable', 'fan-host:1', 0).accepted, 'Current HC-ready fan session should write');
expect(fanGate.admitFanWrite('fan-enable', 'fan-host:1', 0).duplicate, 'Fan write request ids must remain idempotent');

fanGate.observeNativePower('suspending', 1);
expect(!fanGate.admitFanWrite('fan-during-suspend', 'fan-host:1', 1).accepted, 'Suspend must close fan write admission');
fanGate.markSuspended();
fanGate.observeNativePower('resuming', 1);
expectThrow(() => fanGate.beginFanHostSession('fan-host:2', 1), 'Resuming must wait for native resume-ready');
expect(!fanGate.isWakeReady(1), 'Resuming must not open the F5 barrier');
fanGate.observeNativePower('resume-ready', 1);
expect(fanGate.isWakeReady(1), 'Native resume-ready should admit only the matching F5 generation');
fanGate.beginFanHostSession('fan-host:2', 1);
fanGate.admitHcReady('fan-host:2', 1);
expect(!fanGate.admitFanWrite('fan-stale-generation', 'fan-host:2', 0).accepted, 'Old power generation must not regain fan writes');
expect(fanGate.admitFanWrite('fan-resumed-enable', 'fan-host:2', 1).accepted, 'Fresh HC session after resume should write');
fanGate.observeNativePower('resumed', 1);
expect(fanGate.snapshot().phase === 'ready', 'Native resumed commit must preserve the completed FanHost F5 session');
fanGate.observeNativePower('shutdown', 1);
expect(!fanGate.admitFanWrite('fan-after-shutdown', 'fan-host:2', 1).accepted, 'Shutdown must close fan write admission');

console.log('hardware coordinator contract self-test passed');
