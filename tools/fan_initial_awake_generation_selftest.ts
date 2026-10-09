/** FAN-943: real bridge reproduction; all adapters are inert, no HC/native/device I/O. */
import { NoIoHost } from './fixtures/fan_no_io_host';
import { FanHostLifecycle, type FanHostLauncher } from '../src/bridge/fanHost';
import { FanApiError, type FanApiAdapter, type FanState, type FanLease, type FanNode } from '../src/bridge/fanApi';
import type { PowerLifecycleState } from '../src/bridge/api';

let assertions = 0;
function check(value: unknown, message: string): asserts value {
  assertions += 1;
  if (!value) throw new Error(message);
}
const curve: FanNode[] = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 100, dutyPercent: 90 }];
const launcher: FanHostLauncher = { start: async () => ({ pid: 943, executable: 'no-io-fixture.exe' }), stop: async () => {} };
const coldNative = (generation = 1, resumeReady = false): PowerLifecycleState => ({ generation, phase: 'ready', resumeReady,
  hardwareWritesAllowed: true, inputReady: true, hibernateAvailable: true });

function make(host: NoIoHost, read: () => Promise<PowerLifecycleState | null>) { return new FanHostLifecycle({ enabled: true,
  launcher, adapter: host, heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 800, readNativePowerState: read }); }
async function reject(work: Promise<unknown>, code: string) { await work.then(() => { throw new Error(`expected ${code}`); },
  error => check(error instanceof FanApiError && error.errorCode === code, `preserve ${code}: ${String(error)}`)); }
async function main() {
  // Exact log sequence: first enable succeeds at renderer generation 0. The next
  // preset adopts native's initial 1, while Host legitimately has NO resume receipt (0/0).
  const host = new NoIoHost(); let native: PowerLifecycleState | null = null;
  const life = make(host, async () => native);
  await life.start(); await life.apply(curve);
  check(host.calls.filter(c => c === 'enable').length === 1, 'first enable succeeded');
  native = coldNative();
  await life.start(); await life.applyPreset('aggressive', curve);
  check(!host.calls.includes('resume'), 'initial awake native 1 / Host no receipt 0 must not fabricate resume');
  check(host.calls.filter(c => c === 'preset').length === 1, 'next preset really submitted');
  check(life.controlReady, 'next preset remains usable');
  const token = life.currentLease?.leaseId;
  await life.disable(); await life.start(); await life.apply(curve);
  check(life.currentLease?.leaseId !== token, 'disable then re-enable uses fresh lease');
  check(host.calls.filter(c => c === 'enable').length === 2, 're-enable actually submitted');
  check(host.calls.filter(c => c === 'open').length === 1, 'reuse current open session without duplicate Open');
  check(!host.calls.includes('resume'), 'no phantom resume after disabling');
  await life.close();

  // Genuine sleep/new generation revokes the baseline, not just the old lease.
  const wake = new NoIoHost(); const wakeLife = make(wake, async () => coldNative());
  await wakeLife.start({ manualRecovery: true }); await wakeLife.apply(curve);
  wakeLife.setPowerGeneration(2); wakeLife.observePowerBoundary('suspending', 2); await wakeLife.suspend();
  wakeLife.observePowerBoundary('resuming', 2); wakeLife.observePowerBoundary('resume-ready', 2);
  wake.completeResume = true;
  const before = wake.calls.filter(c => c === 'enable').length;
  await wakeLife.start(); await wakeLife.apply(curve);
  check(wake.calls.includes('resume'), 'real sleep requires existing resume owner');
  check(wake.generation === 2 && wake.attempt === 1, 'resume receipt belongs to generation 2');
  check(wake.calls.filter(c => c === 'enable').length === before + 1, 'real wake writes once after admission');
  await wakeLife.close();

  // Zero is NOT a free pass for a true wake, even if an old Host still says Ready/Open.
  for (const mode of ['wake-fact', 'generation-2', 'observed-sleep', 'real-old-receipt', 'contradictory-zero'] as const) {
    const old = new NoIoHost(); old.opened = old.events = true; old.stateName = 'Ready';
    let staleNative: PowerLifecycleState | null = null;
    const staleLife = make(old, async () => staleNative);
    await staleLife.start();
    staleNative = coldNative(mode === 'generation-2' ? 2 : 1, mode === 'wake-fact');
    if (mode === 'real-old-receipt') { old.attempt = 1; }
    if (mode === 'contradictory-zero') { const original = old.snapshot.bind(old); old.snapshot = () => ({ ...original(), powerOperationGeneration: 2 }); }
    if (mode === 'observed-sleep') { staleLife.observePowerBoundary('suspending', 1); staleLife.observePowerBoundary('resuming', 1); staleLife.observePowerBoundary('resume-ready', 1); }
    await reject(staleLife.start({ manualRecovery: true }), 'HC_RESUME_RESULT_EXPIRED');
    check(!old.calls.includes('acquire') && !old.calls.includes('enable'), `${mode}: no writes from stale/no receipt`);
    await staleLife.close();
  }
  // Expiry and real terminal resume errors must survive; never become a 15s Ready deadline.
  for (const code of ['HC_RESUME_RESULT_EXPIRED', 'API_SESSION_REQUIRED', 'FAN_ROUTE_CONFLICT']) {
    const blocked = new NoIoHost(); blocked.resumeFailure = new FanApiError(code, code === 'API_SESSION_REQUIRED' ? 401 : 409, code);
    let blockedNative: PowerLifecycleState | null = null;
    const blockedLife = make(blocked, async () => blockedNative);
    // These are errors from an EXISTING HC session, unlike a never-opened cold
    // Host that has no earlier power operation and must enter its original Open.
    await blockedLife.start(); await blockedLife.apply(curve);
    const writeCount = blocked.calls.filter(c => c === 'enable' || c === 'acquire').length;
    blockedNative = coldNative(2, true);
    const now = performance.now(); await reject(blockedLife.start({ manualRecovery: true }), code);
    check(performance.now() - now < 500, `${code}: no bounded recovery wait`);
    check(blocked.calls.filter(c => c === 'enable' || c === 'acquire').length === writeCount, `${code}: no additional write`);
    await blockedLife.close();
  }
  console.log(JSON.stringify({ ok: true, assertions, fixture: 'no HC/native/device I/O', cases: 'initial awake 1/no receipt 0, preset, disable/re-enable, real wake, stale/contradictory receipt, terminal resume errors' }));
}
void main();
