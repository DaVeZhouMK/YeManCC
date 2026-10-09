/** FAN-943: parameterize the production common lifecycle across the frozen HC route domain.
 * These are software contracts using inert adapters, NOT physical device tests. */
import { readFileSync } from 'node:fs';
import { FanHostLifecycle, type FanHostLauncher } from '../src/bridge/fanHost';
import { FanApiError, type FanState, type FanNode } from '../src/bridge/fanApi';
import { resolveFanSnapshotGeneration, isColdUnopenedFanHost, isCompleteAdmissibleHcSession } from '../src/bridge/fanSessionEvidence';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { PowerLifecycleState } from '../src/bridge/api';
let assertions = 0;
function check(value: unknown, message: string): asserts value { ++assertions; if (!value) throw new Error(message); }
const curve: FanNode[] = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 100, dutyPercent: 90 }];
const launcher: FanHostLauncher = { start: async () => ({ pid: 943, executable: 'no-io.exe' }), stop: async () => {} };
const native = (generation: number): PowerLifecycleState => ({ generation, phase: 'ready', resumeReady: generation > 1,
  hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true });
const matrix = readFileSync('FanLab/evidence/HC-DEVICE-MATRIX-BATCH03-20260817.md', 'utf8');
const families = [...matrix.matchAll(/^\| `([^`]+)` .*\| `SourceMappedFan` \|$/gm)].map(m => m[1]);
const unsupported = [...matrix.matchAll(/^\| `([^`]+)` .*\| `UnsupportedNoFanCapability` \|$/gm)].map(m => m[1]);
function make(host: NoIoHost, read: () => Promise<PowerLifecycleState | null>) { return new FanHostLifecycle({ enabled: true, launcher,
  adapter: host, heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800, readNativePowerState: read }); }
const current = (generation: number): FanState => ({ state: 'Ready', resumePhase: 'Ready', powerState: 'On', hardwareWrites: false,
  resumePhaseGeneration: generation, powerOperationGeneration: generation, powerOperationAttempt: 1, powerOperationStatus: 'completed',
  fanCapabilitySupported: true, controlAccepting: true, openCalled: true, openEventsCalled: true, unknownState: false, hcCloseCleanupPending: false });
const zero = (): FanState => ({ ...current(0), powerState: 'Unknown', powerOperationAttempt: 0, powerOperationStatus: undefined });
async function main() {
  check(families.length === 70 && unsupported.length === 10, 'frozen route domain stays 70 mapped / 10 unsupported');
  let transitions = 0;
  for (const family of families) {
    const host = new NoIoHost(); host.factoryType = family === 'OneXPlayerApex'
      ? 'HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex' : `HandheldCompanion.Devices.${family}`;
    let power: PowerLifecycleState | null = null; const life = make(host, async () => power);
    await life.start(); await life.apply(curve); power = native(1);
    await life.start(); await life.applyPreset('aggressive', curve); await life.disable(); await life.start(); await life.apply(curve);
    check(host.calls.filter(c => c === 'enable').length === 2 && host.calls.filter(c => c === 'preset').length === 1, `${family}: every submitted awake control effective`);
    check(!host.calls.includes('resume'), `${family}: no invented initial sleep/resume`);
    // The actual production classifier is the common entrance used by waits, adoption and rearm.
    const classify = (remote: FanState) => (life as any).classifyControlAdmission(remote, true);
    for (const [name, delta] of [
      ['explicit-false', { controlAccepting: false }], ['future', { resumePhaseGeneration: 2, powerOperationGeneration: 2 }],
      ['conflicting', { resumePhaseGeneration: 1, powerOperationGeneration: 2 }],
      ['pending', { powerOperationStatus: 'pending' }], ['executing', { powerOperationStatus: 'executing' }],
      ['failed', { powerOperationStatus: 'failed' }], ['superseded', { powerOperationStatus: 'superseded' }],
      ['not-open', { openCalled: false }], ['no-events', { openEventsCalled: false }],
      ['cleanup', { hcCloseCleanupPending: true }], ['unknown', { unknownState: true }],
      ['resuming-power', { powerState: 'Resuming' }], ['suspended-power', { powerState: 'Suspended' }],
    ] as Array<[string, Partial<FanState>]>) {
      check(classify({ ...current(1), ...delta }) !== 'accepting', `${family}: ${name} cannot pass Ready`); ++transitions;
    }
    const originalState = host.getState.bind(host);
    host.getState = async () => ({ ...await originalState(), controlAccepting: false });
    const reattachWrites = host.calls.filter(c => ['enable', 'acquire', 'preset'].includes(c)).length;
    check(!await life.reattachExistingControlSession(), `${family}: cached lease cannot bypass explicit Host denial during reattach`);
    check(host.calls.filter(c => ['enable', 'acquire', 'preset'].includes(c)).length === reattachWrites, `${family}: blocked reattach has no ownership/control writes`);
    host.getState = async () => { throw new FanApiError('denied read', 401, 'API_SESSION_REQUIRED'); };
    const callbacksBeforeDenial = host.calls.filter(c => ['resume', 'close', 'open', 'events', 'acquire', 'enable', 'preset'].includes(c)).length;
    await life.start({ manualRecovery: true }).then(() => { throw new Error('authorization refusal was swallowed'); },
      error => check(error instanceof FanApiError && error.errorCode === 'API_SESSION_REQUIRED', `${family}: state read denial preserves its original code`));
    check(host.calls.filter(c => ['resume', 'close', 'open', 'events', 'acquire', 'enable', 'preset'].includes(c)).length === callbacksBeforeDenial,
      `${family}: denied state read is not Host absence and triggers no replacement/close/resume`);
    host.getState = originalState;
    // Late snapshots cannot mutate current flags or lease, including a future/contradictory snapshot.
    const before = { opened: (life as any).opened, events: (life as any).eventsOpened, lease: life.currentLease?.leaseId };
    for (const delta of [{ resumePhaseGeneration: 0, powerOperationGeneration: 0 },
      { resumePhaseGeneration: 2, powerOperationGeneration: 2 }, { resumePhaseGeneration: 1, powerOperationGeneration: 2 }]) {
      (life as any).syncRemoteSessionState({ ...current(1), ...delta, openCalled: false, openEventsCalled: false, lease: null, leaseGeneration: null });
      check((life as any).opened === before.opened && (life as any).eventsOpened === before.events && life.currentLease?.leaseId === before.lease,
        `${family}: stale/future/conflicting snapshot cannot poison live session`);
    }
    // A release/expiry may remove lease during Restore; the saved cleanup token must remain valid.
    const restore = host.restoreOem.bind(host); host.restoreOem = async () => { const result = await restore(); host.lease = null; return { ...result, lease: null, leaseGeneration: null }; };
    await life.disable(); check(host.calls.includes('release'), `${family}: release follows Restore even if lease was cleared`);
    await life.close();

    const shell = new NoIoHost(); shell.factoryType = host.factoryType;
    let shellPower: PowerLifecycleState | null = null; const shellLife = make(shell, async () => shellPower);
    await shellLife.start(); shellPower = native(1);
    await shellLife.start({ manualRecovery: true }); await shellLife.apply(curve);
    check(!shell.calls.includes('resume') && shell.calls.filter(c => c === 'open').length === 1
      && shell.calls.filter(c => c === 'events').length === 1 && shell.calls.filter(c => c === 'enable').length === 1,
      `${family}: handshake-only cold Host enters the original Open/OpenEvents chain, not phantom Resume`);
    await shellLife.close();
    // FAN-944: the app has already slept, but this Host did not exist until the
    // first manual enable. A new Host has no resume receipt for that prior sleep.
    for (const shellState of ['AwaitingControl', 'Stopped']) {
      const afterWake = new NoIoHost(); afterWake.factoryType = host.factoryType; afterWake.stateName = shellState;
      if (shellState === 'Stopped') {
        const open = afterWake.open.bind(afterWake), events = afterWake.openEvents.bind(afterWake);
        afterWake.open = async () => { afterWake.stateName = 'Open'; return open(); };
        afterWake.openEvents = async () => { afterWake.stateName = 'AwaitingControl'; return events(); };
      }
      const afterWakeLife = make(afterWake, async () => native(2));
      afterWakeLife.observePowerBoundary('suspending', 2);
      afterWakeLife.observePowerBoundary('resuming', 2);
      afterWakeLife.observePowerBoundary('resume-ready', 2);
      await afterWakeLife.start({ manualRecovery: true }); await afterWakeLife.apply(curve);
      check(!afterWake.calls.includes('resume') && afterWake.calls.filter(c => c === 'open').length === 1
        && afterWake.calls.filter(c => c === 'events').length === 1 && afterWake.calls.filter(c => c === 'acquire').length === 1
        && afterWake.calls.filter(c => c === 'enable').length === 1,
        `${family}/${shellState}: first Host after prior sleep must Open once without inventing a resume receipt`);
      await afterWakeLife.start(); await afterWakeLife.applyPreset('soft', curve);
      check(!afterWake.calls.includes('resume') && afterWake.calls.filter(c => c === 'preset').length === 1,
        `${family}/${shellState}: the next manual adjustment remains usable after first-enable admission`);
      await afterWakeLife.close();
    }
    // A fresh Host opened AFTER a real wake still has zero power receipts. Its
    // own Open/OpenEvents bind this exact session; a later preset must not call phantom Resume.
    const fresh = new NoIoHost(); fresh.factoryType = host.factoryType;
    const freshLife = make(fresh, async () => native(2)); freshLife.setPowerGeneration(2);
    await freshLife.start(); await freshLife.apply(curve); await freshLife.start(); await freshLife.applyPreset('soft', curve);
    check(!fresh.calls.includes('resume') && fresh.calls.filter(c => c === 'preset').length === 1, `${family}: post-wake replacement has its own completed Open proof`);
    check(await freshLife.reattachExistingControlSession(), `${family}: active session reattach uses the same evidence`);
    // Positive old receipts may not borrow the new Open binding.
    check((freshLife as any).classifyControlAdmission(current(1), true) !== 'accepting', `${family}: positive old receipt stays denied`);
    freshLife.observePowerBoundary('suspending', 2); freshLife.observePowerBoundary('resuming', 2); freshLife.observePowerBoundary('resume-ready', 2);
    check((freshLife as any).classifyControlAdmission(zero(), true) !== 'accepting', `${family}: a real boundary revokes the explicit-open binding`);
    await freshLife.close();
  }
  for (const family of unsupported) {
    const host = new NoIoHost(); const original = host.handshake.bind(host); host.handshake = async () => ({ ...await original(), supported: false, fanRouteWriteReady: false, deviceClass: `HandheldCompanion.Devices.${family}` });
    const life = make(host, async () => null); await life.start().catch(() => undefined);
    check(!host.calls.some(c => ['open', 'events', 'acquire', 'enable', 'preset'].includes(c)), `${family}: unsupported handshake has no control callback`);
    await life.close();
  }
  // A later same-generation curve owns a fresh rearm context, not the
  // cancelled earlier request's waiter. There is still only one resume owner.
  {
    const host = new NoIoHost(); const life = make(host, async () => native(1));
    await life.start(); host.generation = 1; host.attempt = 1; host.stateName = 'Resuming';
    host.opened = host.events = false;
    life.setPowerGeneration(1); life.observePowerBoundary('resuming', 1); life.observePowerBoundary('resume-ready', 1);
    const old = (life as any).rearmAfterWakeForControlAdmission('old-intent', true);
    const oldResult = old.then(() => 'resolved', () => 'cancelled');
    for (let spins = 0; spins < 30 && !(life as any).resumeWaitTask; ++spins) await Promise.resolve();
    check(!!(life as any).resumeWaitTask, 'old same-generation rearm waiter actually exists');
    // Mirror apply's synchronous supersession before any microtask can clear the old owner.
    (life as any).controlIntentRevision += 1;
    (life as any).cancelResumeWait('new-curve-intent');
    const latestRearm = (life as any).rearmAfterWakeForControlAdmission('new-intent', true);
    host.stateName = 'Ready'; host.opened = host.events = true;
    await latestRearm;
    await life.apply(curve);
    check(await oldResult === 'cancelled', 'old intent cannot claim the new curve');
    check(host.calls.filter(c => c === 'enable').length === 1, 'latest curve submitted exactly once');
    check(host.calls.filter(c => c === 'resume').length <= 1, 'same-generation rearm never drives parallel resume owners');
    await life.close();
  }
  // A zero receipt is insufficient unless the entire never-opened contract is present.
  {
    const empty = new NoIoHost().snapshot();
    check(isColdUnopenedFanHost(empty), 'current unopened AwaitingControl shell is eligible only for original Open');
    check(isColdUnopenedFanHost({ ...empty, state: 'Stopped', resumePhase: 'Stopped', controlAccepting: false }),
      'production Stopped shell may enter Open without claiming it already accepts control');
    for (const delta of [
      { openCalled: true }, { openEventsCalled: true }, { closeCalled: true }, { closeCalled: undefined },
      { hardwareWritesObserved: true }, { hardwareWritesObserved: undefined }, { hardwareWritesEnabled: true },
      { lease: { leaseId: 'other-owner', generation: 1 } }, { lease: undefined },
      { unknownState: true }, { hcCloseCleanupPending: true }, { fanCapabilitySupported: false },
      { controlAccepting: false }, { powerOperationAttempt: 1 }, { powerOperationStatus: 'failed' },
      { powerOperationGeneration: 2 }, { resumePhaseGeneration: 2 }, { powerState: 'Suspended' },
      { state: 'Ready', resumePhase: 'Ready' }, { state: 'Stopped', resumePhase: 'Stopped', controlAccepting: true },
    ]) check(!isColdUnopenedFanHost({ ...empty, ...delta }), 'dirty/closed/leased/stale/unsupported shell must not bypass real recovery');
    for (const nativeDelta of [{ generation: 3 }, { phase: 'suspended' }, { phase: 'suspending' }, { hardwareWritesAllowed: false }]) {
      const blocked = new NoIoHost();
      const blockedLife = make(blocked, async () => ({ ...native(2), ...nativeDelta } as PowerLifecycleState));
      blockedLife.setPowerGeneration(2); blockedLife.observePowerBoundary('resuming', 2); blockedLife.observePowerBoundary('resume-ready', 2);
      try { await blockedLife.start({ manualRecovery: true }); } catch {}
      check(!blocked.calls.some(c => ['open', 'events', 'acquire', 'enable'].includes(c)),
        'non-current/sleeping/write-blocked native boundary does not admit cold Host writes');
      await blockedLife.close();
    }
  }
  for (let local = 0; local <= 71; ++local) for (let actual = 0; actual <= 71; ++actual) {
    const result = resolveFanSnapshotGeneration(current(actual), { powerGeneration: local, initialAwakeGeneration: null, explicitOpenGeneration: null });
    check((result.freshness === 'current') === (local === actual), 'receipt generations match exactly, not >=');
  }
  for (const value of [-1, 0.5, NaN, Infinity, '1', null]) {
    check(resolveFanSnapshotGeneration({ ...current(1), resumePhaseGeneration: value }, { powerGeneration: 1, initialAwakeGeneration: 1, explicitOpenGeneration: 1 }).freshness === 'invalid', 'malformed supplied generation cannot become missing legacy evidence');
  }
  check(isCompleteAdmissibleHcSession(zero()), 'zero itself can be a complete opened session, not a resume receipt');
  console.log(JSON.stringify({ ok: true, assertions, mappedFamilies: families.length, unsupportedFamilies: unsupported.length, transitionCases: transitions,
    hardwareCalls: 0, HCAssemblyLoaded: false, scope: 'software lifecycle only; no physical control or OEM ownership claim' }));
}
void main();
