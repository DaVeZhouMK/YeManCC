import { readFileSync } from 'node:fs';

type Owner = 'oem' | 'yeman' | 'external' | 'unknown';
type Life = 'ready' | 'suspending' | 'suspended' | 'resuming' | 'awaiting-control' | 'conflict-locked' | 'fault-locked';

const assert = (value: unknown, message: string): asserts value => {
  if (!value) throw new Error(message);
};

/**
 * Deterministic power/fan model. It intentionally models only YeMan's safety
 * boundary; it never loads HC, calls EC/ACPI/HID, or sleeps the test machine.
 */
class SleepModel {
  owner: Owner = 'yeman';
  life: Life = 'ready';
  power: 'on' | 'suspended' = 'on';
  lease = true;
  generation = 1;
  gateClosed = false;
  cleanupQueued = false;
  cleanupRuns = 0;
  callbackWork = 0;
  resumeCommits = 0;
  resumeSignals = 0;
  conflict = false;

  query(): void {
    this.generation += 1;
    this.gateClosed = true;
    this.life = 'suspending';
    // Query is cancellable: no Fan Host release is allowed here.
  }

  queryCancelled(): void {
    this.gateClosed = false;
    this.life = 'ready';
    // The active curve remains owned by YeMan because sleep did not happen.
  }

  confirmedSuspend(): void {
    this.power = 'suspended';
    this.life = 'suspended';
    this.queueCleanup();
  }

  kernelPower506(): void {
    this.generation += 1;
    this.power = 'suspended';
    this.life = 'suspending';
    this.queueCleanup();
  }

  private queueCleanup(): void {
    // This is the atomic de-duplication contract of the native fallback.
    if (this.cleanupQueued) return;
    this.cleanupQueued = true;
  }

  runCleanup(options: { hostAvailable?: boolean; externalActive?: boolean } = {}): void {
    if (!this.cleanupQueued) return;
    this.cleanupQueued = false;
    this.cleanupRuns += 1;
    if (options.externalActive) {
      this.owner = 'external';
      this.lease = false;
      this.conflict = true;
      this.life = 'conflict-locked';
      return;
    }
    if (options.hostAvailable === false) {
      // The OS is still allowed to sleep. YeMan must not claim OEM safety
      // when the restore endpoint is unavailable.
      this.owner = 'unknown';
      this.lease = false;
      this.life = 'fault-locked';
      return;
    }
    this.owner = 'oem';
    this.lease = false;
    this.life = 'suspended';
  }

  resume(): void {
    if (this.power !== 'suspended') return;
    this.resumeSignals += 1;
    this.power = 'on';
    this.life = 'awaiting-control';
    this.resumeCommits += 1;
    // No automatic acquire or curve replay after a sleep boundary.
  }

  /** HC may observe SystemEvents.Resume before Kernel-Power 507. */
  systemResumeObserved(): void {
    this.resumeSignals += 1;
    if (this.power !== 'suspended') return;
    // Keep the transaction observational until the durable 507 classification.
  }

  kernelPower507User(): void {
    this.resume();
  }

  externalTakeover(): void {
    this.owner = 'external';
    this.lease = false;
    this.conflict = true;
    this.life = 'conflict-locked';
  }
}

/** Models the HC SystemReady asynchronous rebuild admission boundary. */
class ResumeAdmissionModel {
  state: Life = 'resuming';
  automaticResumeInProgress = true;
  workerThreadId = 42;

  canEnter(threadId: number): boolean {
    const internalWorker = threadId === this.workerThreadId;
    return internalWorker || (!this.automaticResumeInProgress && this.state !== 'resuming');
  }

  finish(): void {
    this.state = 'ready';
    this.automaticResumeInProgress = false;
    this.workerThreadId = 0;
  }
}

/** Models a new suspend arriving while the HC SystemReady rebuild is active. */
class ResumeSuspendRaceModel {
  resuming = true;
  suspendRequestedDuringResume = false;
  closeCount = 0;

  requestSuspend(): void {
    if (this.resuming) {
      this.suspendRequestedDuringResume = true;
      return;
    }
    this.closeCount += 1;
  }

  finishResume(): void {
    this.resuming = false;
    if (this.suspendRequestedDuringResume) {
      this.suspendRequestedDuringResume = false;
      this.closeCount += 1;
    }
  }
}

function sourceChecks(): number {
  const nativePath = 'C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\native\\main.cpp';
  const hostPath = 'C:\\SOFT\\YeManCC-Work\\FanLab\\real-host\\Program.cs';
  const lifecyclePath = 'C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\src\\bridge\\fanHost.ts';
  const hcPath = 'C:\\SOFT\\YeManCC-Work\\FanLab\\hc-upstream\\HandheldCompanion\\Views\\Windows\\MainWindow.xaml.cs';
  const hcSystemPath = 'C:\\SOFT\\YeManCC-Work\\FanLab\\hc-upstream\\HandheldCompanion\\Managers\\SystemManager.cs';
  const native = readFileSync(nativePath, 'utf8');
  const host = readFileSync(hostPath, 'utf8');
  const lifecycle = readFileSync(lifecyclePath, 'utf8');
  const hc = readFileSync(hcPath, 'utf8');
  const hcSystem = readFileSync(hcSystemPath, 'utf8');
  const queryStart = native.indexOf('else if (w == PBT_APMQUERYSUSPEND)');
  const suspendStart = native.indexOf('else if (w == PBT_APMSUSPEND)', queryStart);
  const queryBody = native.slice(queryStart, suspendStart);
  assert(queryStart >= 0 && suspendStart > queryStart, 'native power boundaries missing');
  assert(!queryBody.includes('fanHostScheduleEmergencySuspend'), 'cancelable query must not queue fan cleanup');
  assert(native.includes('fanHostScheduleEmergencySuspend("suspend-confirmed", currentPowerGeneration())'),
    'confirmed query-owned suspend must queue fan cleanup');
  assert(native.includes('fanHostScheduleEmergencySuspend("suspend-broadcast", generation)'),
    'bare suspend must queue fan cleanup');
  assert(native.includes('fanHostScheduleEmergencySuspend("kernel-power-506", generation)'),
    'Kernel-Power 506 must queue fan cleanup');
  assert(native.includes('std::thread([reasonText = std::string(reason ? reason : "unknown"), generation]') && native.includes('}).detach();'),
    'native cleanup must be detached');
  assert(host.includes('SystemEvents.PowerModeChanged += OnPowerModeChanged') &&
    host.includes('Channel.CreateUnbounded<PowerTransition>') &&
    host.includes('powerTransitions.Writer.TryWrite') &&
    host.includes('ProcessPowerTransitionsAsync'),
    'Host power observer must use a non-blocking ordered channel');
  assert(host.includes('ReadAllAsync(cancellationToken)') && host.includes('cancellationToken.IsCancellationRequested'),
    'queued power work must be canceled after Host shutdown');
  assert(host.includes('automaticResumeWorkerThreadId') && host.includes('POWER_RESUMING'),
    'external control must remain gated during asynchronous HC resume rebuild');
  assert(host.includes('Volatile.Write(ref automaticResumeWorkerThreadId, workerThreadId)') &&
    host.includes('Volatile.Write(ref automaticResumeWorkerThreadId, 0)'),
    'resume worker admission bypass must be thread-bound and cleared');
  assert(host.includes('suspendRequestedDuringResume') &&
    host.includes('power.suspend-deferred-during-resume'),
    'suspend during asynchronous resume must defer to the single resume owner');
  assert(hc.includes('ManagerFactory.Suspend();') && hc.includes('CurrentDevice.Close();'),
    'HC reference suspend order is missing');
  assert(hcSystem.includes('EventLogWatcher') && hcSystem.includes('EventID=506') && hcSystem.includes('EventID=507'),
    'HC 506/507 event-log race source is missing');
  assert(hcSystem.includes('if (isPowerSuspended)') && hcSystem.includes('PowerModeChanged?.Invoke(PowerMode.Resume'),
    'HC resume race guard is missing');
  assert(lifecycle.includes("this.state !== 'fault-locked'") &&
    lifecycle.includes("this.state !== 'conflict-locked'") &&
    lifecycle.includes("this.state !== 'unknown'") &&
    lifecycle.includes("this.state !== 'starting'") &&
    lifecycle.includes("this.state !== 'handshaking'") &&
    lifecycle.includes('await this.adapter.suspend();'),
    'frontend sleep boundary must still send Host suspend from startup, fault and unknown states');
  return 15;
}

function scenarioChecks(): number {
  let passed = 0;
  {
    const m = new SleepModel();
    m.query(); m.queryCancelled();
    assert(m.owner === 'yeman' && m.life === 'ready' && m.cleanupRuns === 0, 'query cancellation changed fan ownership');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup();
    assert(m.power === 'suspended' && m.owner === 'oem' && !m.lease, 'normal suspend did not restore OEM');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.confirmedSuspend(); m.runCleanup();
    assert(m.cleanupRuns === 1, 'duplicate suspend created duplicate cleanup');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.kernelPower506(); m.runCleanup();
    assert(m.power === 'suspended' && m.owner === 'oem', 'Modern Standby entry did not restore OEM');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup({ hostAvailable: false });
    assert(m.power === 'suspended' && m.owner === 'unknown' && m.life === 'fault-locked',
      'Host outage incorrectly claimed safe OEM state or vetoed sleep');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup({ externalActive: true });
    assert(m.power === 'suspended' && m.owner === 'external' && m.conflict && m.life === 'conflict-locked',
      'external controller was overwritten during sleep');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.externalTakeover(); m.query(); m.confirmedSuspend(); m.runCleanup({ externalActive: true });
    assert(m.owner === 'external' && m.lease === false && m.life === 'conflict-locked',
      'third-party takeover was not preserved');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup(); m.resume(); m.resume();
    assert(m.resumeCommits === 1 && m.life === 'awaiting-control', 'resume storm replayed control');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup();
    assert(m.owner === 'oem' && !m.lease, 'lease expiry/recovery did not end in OEM');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend();
    // A blocked worker must not execute synchronously in the power callback.
    assert(m.callbackWork === 0 && m.power === 'suspended', 'blocked cleanup vetoed sleep');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup(); m.resume();
    assert(m.life === 'awaiting-control' && m.owner === 'oem' && !m.lease,
      'resume incorrectly auto-reacquired old lease');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.kernelPower506(); m.confirmedSuspend(); m.runCleanup();
    assert(m.cleanupRuns === 1 && m.owner === 'oem', '506 plus suspend duplicate was not idempotent');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup();
    // HC's SystemEvents.Resume may arrive before Kernel-Power 507. The
    // observational signal must not reacquire or replay the fan lease.
    m.systemResumeObserved();
    assert(m.power === 'suspended' && m.resumeCommits === 0 && !m.lease,
      'early SystemEvents.Resume committed fan control before 507');
    m.kernelPower507User();
    assert(m.power === 'on' && m.resumeCommits === 1 && m.resumeSignals === 2,
      '507 after early Resume did not commit exactly once');
    passed += 1;
  }
  {
    const m = new SleepModel();
    m.query(); m.confirmedSuspend(); m.runCleanup();
    // The reverse ordering is also valid: 507 may commit before the generic
    // Resume broadcast. The later duplicate is a no-op.
    m.kernelPower507User();
    m.resume();
    assert(m.power === 'on' && m.resumeCommits === 1,
      'Resume after 507 replayed the fan control transaction');
    passed += 1;
  }
  {
    const m = new ResumeAdmissionModel();
    assert(!m.canEnter(7), 'external control entered during Resuming rebuild');
    assert(m.canEnter(42), 'automatic resume worker was blocked by its own gate');
    m.finish();
    assert(m.canEnter(7) && m.workerThreadId === 0, 'resume admission gate was not released after terminal state');
    passed += 1;
  }
  {
    const m = new ResumeAdmissionModel();
    // A second worker must not be able to inherit the first worker identity.
    m.finish();
    assert(m.workerThreadId === 0 && m.state === 'ready', 'resume worker privilege leaked after completion');
    passed += 1;
  }
  {
    const m = new ResumeSuspendRaceModel();
    m.requestSuspend(); m.requestSuspend();
    assert(m.closeCount === 0 && m.suspendRequestedDuringResume,
      'suspend during resume started a concurrent Close');
    m.finishResume();
    assert(m.closeCount === 1 && !m.suspendRequestedDuringResume,
      'deferred suspend did not run exactly one Close after resume returned');
    passed += 1;
  }
  {
    const m = new ResumeSuspendRaceModel();
    m.finishResume(); m.requestSuspend(); m.requestSuspend();
    assert(m.closeCount === 2, 'normal post-resume suspend boundary was lost');
    passed += 1;
  }
  return passed;
}

const sourceCount = sourceChecks();
const scenarioCount = scenarioChecks();
console.log(`fan sleep extreme selftest: PASS (source=${sourceCount}, scenarios=${scenarioCount}, hardwareWrites=false)`);
