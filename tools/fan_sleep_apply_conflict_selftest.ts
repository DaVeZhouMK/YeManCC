import { readFileSync } from 'node:fs';

type Owner = 'oem' | 'yeman' | 'external' | 'unknown';
type State = 'ready' | 'suspending' | 'suspended' | 'awaiting-control' | 'fault-locked' | 'conflict-locked';
type Nodes = readonly [number, number, number, number];

const assert = (value: unknown, message: string): asserts value => {
  if (!value) throw new Error(message);
};

/**
 * Pure event-order model. It never loads HC, opens a device, calls EC/ACPI/HID,
 * starts a Host, or sleeps this machine. The model represents only the safety
 * boundary between an apply request and a power transaction.
 */
class SleepApplyModel {
  state: State = 'ready';
  owner: Owner = 'yeman';
  generation = 1;
  gateOpen = true;
  lease = true;
  nodes: Nodes = [0, 30, 70, 100];
  writes = 0;
  restores = 0;
  closes = 0;
  staleRejected = 0;
  cleanupQueued = false;
  rejectedAfterSuspend = 0;
  internalSleepMarker = false;
  resumeCommits = 0;
  writeAfterSuspend = 0;

  captureGeneration(): number { return this.generation; }

  apply(next: Nodes, capturedGeneration = this.generation): boolean {
    if (capturedGeneration !== this.generation || !this.gateOpen || this.state !== 'ready') {
      this.staleRejected += 1;
      if (this.state === 'suspended') this.rejectedAfterSuspend += 1;
      return false;
    }
    this.nodes = next;
    this.writes += 1;
    return true;
  }

  query(): number {
    this.generation += 1;
    this.gateOpen = false;
    this.state = 'suspending';
    return this.generation;
  }

  queryCancelled(): void {
    this.gateOpen = true;
    this.state = 'ready';
  }

  confirmedSuspend(): void {
    this.gateOpen = false;
    this.queueCleanup();
    this.state = 'suspended';
  }

  programSleepRequest(): void {
    // Mirrors sgMarkInternalSleepRequest + SetSuspendState without calling it.
    this.internalSleepMarker = true;
    this.query();
  }

  kernelPower506(): void {
    // The internal marker proves this is the same request, not a second
    // transaction. A user 506 without a query owns a fresh generation.
    if (!this.internalSleepMarker) this.query();
    this.queueCleanup();
  }

  private queueCleanup(): void {
    if (this.cleanupQueued) return;
    this.cleanupQueued = true;
  }

  runCleanup(options: { hostAvailable?: boolean; externalActive?: boolean } = {}): void {
    if (!this.cleanupQueued) return;
    this.cleanupQueued = false;
    if (options.externalActive) {
      this.owner = 'external';
      this.lease = false;
      this.state = 'conflict-locked';
      return;
    }
    if (options.hostAvailable === false) {
      this.owner = 'unknown';
      this.lease = false;
      this.state = 'fault-locked';
      return;
    }
    // The model records the HC-required order: OEM restore before close.
    this.restores += 1;
    this.owner = 'oem';
    this.lease = false;
    this.closes += 1;
  }

  resume(): void {
    if (this.state !== 'suspended') return;
    this.state = 'awaiting-control';
    this.resumeCommits += 1;
    this.internalSleepMarker = false;
  }

  externalTakeover(): void {
    this.owner = 'external';
    this.lease = false;
    this.state = 'conflict-locked';
  }
}

/** Renderer/Host race model: a fast Host rebuild can answer Ready before the
 * renderer's resume request arrives. The UI must adopt, not write twice. */
class ResumeAdoptionModel {
  writes = 0;
  state: 'ready' | 'awaiting-control' = 'ready';

  resumeResponse(responseState: 'Ready' | 'Resuming', remoteReady: boolean): void {
    if (responseState === 'Resuming' && !remoteReady) {
      this.state = 'awaiting-control';
      this.writes += 1;
      return;
    }
    if (remoteReady) {
      this.state = 'ready';
      return;
    }
    this.state = 'awaiting-control';
    this.writes += 1;
  }
}

function sourceChecks(): number {
  const nativePath = 'C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\native\\main.cpp';
  const hostPath = 'C:\\SOFT\\YeManCC-Work\\FanLab\\real-host\\Program.cs';
  const lifecyclePath = 'C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\src\\bridge\\fanHost.ts';
  const native = readFileSync(nativePath, 'utf8');
  const host = readFileSync(hostPath, 'utf8');
  const lifecycle = readFileSync(lifecyclePath, 'utf8');
  const queryStart = native.indexOf('else if (w == PBT_APMQUERYSUSPEND)');
  const suspendStart = native.indexOf('else if (w == PBT_APMSUSPEND)', queryStart);
  const queryBody = native.slice(queryStart, suspendStart);
  const suspendEnd = native.indexOf('else if (w == PBT_APMQUERYSUSPENDFAILED', suspendStart);
  const suspendBody = native.slice(suspendStart, suspendEnd);
  assert(queryStart >= 0 && suspendStart > queryStart && suspendEnd > suspendStart,
    'native sleep branches missing');
  assert(!queryBody.includes('fanHostScheduleEmergencySuspend'),
    'cancelable query must not release Fan Host');
  assert(queryBody.includes('closeHardwareWriteGate') && queryBody.includes('sgQueueWork(SgWork::Suspend'),
    'query must close gate and queue pause work');
  assert(queryBody.includes('return TRUE'), 'query must return immediately to Windows');
  assert(suspendBody.includes('fanHostScheduleEmergencySuspend("suspend-confirmed"') &&
    suspendBody.includes('fanHostScheduleEmergencySuspend("suspend-broadcast"'),
    'confirmed/bare suspend must only enqueue native Fan cleanup');
  assert(!suspendBody.includes('fanHostEmergencySuspend(') && !suspendBody.includes('WinHttpOpen('),
    'suspend callback must not perform synchronous HTTP cleanup');
  assert(native.includes('SetSuspendState(FALSE, FALSE, FALSE)') &&
    native.includes('sgMarkInternalSleepRequest') && native.includes('sgInternalSleepRequestMatches506'),
    'programmatic sleep request must use the existing internal marker path');
  const powerStart = native.indexOf('case WM_POWERBROADCAST:');
  const trayStart = native.indexOf('case WM_TRAYICON:', powerStart);
  const powerBody = native.slice(powerStart, trayStart);
  assert(powerStart >= 0 && trayStart > powerStart && !powerBody.includes('sgRequestSystemSleep('),
    'programmatic SetSuspendState must not run synchronously in a power callback');
  assert(lifecycle.includes('async apply(nodes') && lifecycle.includes('return this.enqueue(async () =>'),
    'curve apply must be serialized through the lifecycle queue');
  assert(lifecycle.includes('async applyPreset') && lifecycle.includes('async suspend()') &&
    lifecycle.includes('async resume()') && lifecycle.includes('async close()'),
    'preset and power lifecycle operations must be explicit methods');
  assert(host.includes('SystemEvents.PowerModeChanged += OnPowerModeChanged') &&
    host.includes('Channel.CreateUnbounded<PowerTransition>') &&
    host.includes('powerTransitions.Writer.TryWrite') &&
    host.includes('ReadAllAsync(cancellationToken)') &&
    host.includes('cancellationToken.IsCancellationRequested'),
    'Host power observer must queue cancellable asynchronous work');
  assert(host.includes('RestoreOemCore') && host.includes('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false))'),
    'Host OEM restore path must remain explicit');
  const apiResumeStart = host.indexOf('public object Resume()');
  const systemResumeStart = host.indexOf('public object ResumeForSystemPower()');
  const systemResumeEnd = host.indexOf('\n    public object Close()', systemResumeStart);
  const apiResumeBody = host.slice(apiResumeStart, systemResumeStart);
  const systemResumeBody = host.slice(systemResumeStart, systemResumeEnd);
  assert(apiResumeStart >= 0 && systemResumeStart > apiResumeStart && systemResumeEnd > systemResumeStart &&
    apiResumeBody.includes('state.PowerState != "Suspended" || state.State != "Suspended"') &&
    apiResumeBody.includes('api.resume-ignored-not-suspended') &&
    systemResumeBody.includes('state.PowerState != "Suspended" || state.State != "Suspended"') &&
    systemResumeBody.includes('power.resume-ignored-not-suspended'),
    'duplicate resume must never clear an active Host session unless a completed suspend boundary exists');
  const app = readFileSync('C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\src\\App.vue', 'utf8');
  const fanView = readFileSync('C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\src\\views\\FanView.vue', 'utf8');
  const fanViewResumeStart = fanView.indexOf('async function resumeFanControlAfterWake');
  const fanViewResumeEnd = fanView.indexOf('\nfunction onFanPowerSuspending', fanViewResumeStart);
  const fanViewResumeBody = fanView.slice(fanViewResumeStart, fanViewResumeEnd);
  assert(lifecycle.includes('private activeCurve: FanNode[] | null') &&
    lifecycle.includes('private resumeCurve: FanNode[] | null') &&
    lifecycle.includes('await this.applyMutation(curveToResume)') &&
    lifecycle.includes('唤醒后风扇数据路线未通过真实写入/恢复验证') &&
    app.includes('fanHostLifecycle.resume()') &&
    app.includes("setFanControlActive(fanHostLifecycle.state === 'ready')") &&
    fanViewResumeStart >= 0 && fanViewResumeEnd > fanViewResumeStart &&
    !fanViewResumeBody.includes('await requestCurveApply()') &&
    lifecycle.includes('Always perform one read-only state') &&
    lifecycle.includes('responseState: resumedState.state'),
    'wake replay must be owned by the global lifecycle and must not issue a duplicate FanView curve write');
  return 16;
}

function scenarioChecks(): number {
  let passed = 0;
  {
    const m = new SleepApplyModel();
    const oldNodes = m.nodes;
    assert(m.apply([0, 30, 70, 100]), 'normal curve apply failed');
    m.query(); m.confirmedSuspend(); m.runCleanup();
    assert(m.owner === 'oem' && !m.lease && m.state === 'suspended' && m.restores === 1 && m.closes === 1,
      'normal apply then sleep did not restore OEM before close');
    assert(m.nodes !== oldNodes && m.writeAfterSuspend === 0, 'normal path wrote during suspend');
    passed += 1;
  }
  {
    const m = new ResumeAdoptionModel();
    m.resumeResponse('Ready', true);
    assert(m.writes === 0 && m.state === 'ready',
      'fast Host Ready response caused a duplicate resume curve write');
    passed += 1;
  }
  {
    const m = new ResumeAdoptionModel();
    m.resumeResponse('Resuming', false);
    assert(m.writes === 1 && m.state === 'awaiting-control',
      'non-ready Host resume response skipped the single replay path');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]);
    // An active curve is restored to OEM for sleep, then the same curve is
    // explicitly reapplied after resume. Duplicate resume notifications must
    // not create a second write or a second lease owner.
    m.programSleepRequest(); m.confirmedSuspend(); m.runCleanup();
    m.resume();
    assert(m.state === 'awaiting-control' && m.owner === 'oem' && !m.lease,
      'wake boundary must not silently retain the old lease');
    // Auto-resume performs the same handshake and fresh lease acquisition as
    // an explicit user click before it replays the remembered curve.
    m.state = 'ready';
    m.gateOpen = true;
    m.owner = 'yeman';
    m.lease = true;
    assert(m.apply(m.nodes), 'post-resume auto replay was rejected after handshake');
    m.resume();
    assert(m.writes === 2 && m.restores === 1 && m.closes === 1,
      'duplicate wake event must not duplicate restore or curve ownership');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]);
    // A spurious/duplicate resume while control is still active must leave
    // the HC session evidence intact. Treating it as a real wake used to
    // make later Close() believe no restore was required.
    m.resume();
    assert(m.state === 'ready' && m.owner === 'yeman' && m.lease && m.resumeCommits === 0,
      'out-of-boundary resume must be a no-op while YeMan owns the curve');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]);
    const captured = m.captureGeneration();
    m.query();
    assert(!m.apply([0, 40, 80, 100], captured), 'stale apply crossed the query generation');
    m.confirmedSuspend(); m.runCleanup();
    assert(m.staleRejected === 1 && m.writeAfterSuspend === 0 && m.owner === 'oem',
      'apply/query race was not rejected safely');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.query();
    assert(!m.apply([0, 25, 60, 100]), 'gate-closed apply was allowed during query');
    m.queryCancelled();
    assert(m.apply([0, 25, 60, 100]) && m.nodes[1] === 25,
      'query cancellation did not reopen normal parameter control');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]);
    m.programSleepRequest();
    m.kernelPower506();
    m.confirmedSuspend(); m.runCleanup();
    assert(m.restores === 1 && m.closes === 1 && m.state === 'suspended' && m.owner === 'oem',
      'program sleep plus 506 created duplicate cleanup');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]);
    m.programSleepRequest(); m.confirmedSuspend(); m.runCleanup();
    m.resume(); m.resume();
    assert(m.resumeCommits === 1 && m.state === 'awaiting-control' && !m.lease,
      'resume storm reacquired or replayed the old curve');
    assert(!m.apply([0, 50, 80, 100]), 'apply was allowed before explicit post-resume control');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]); m.programSleepRequest(); m.confirmedSuspend();
    m.runCleanup({ hostAvailable: false });
    assert(m.state === 'fault-locked' && m.owner === 'unknown' && m.writeAfterSuspend === 0,
      'Host outage claimed safe restore or allowed writes');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]); m.externalTakeover();
    m.programSleepRequest(); m.confirmedSuspend(); m.runCleanup({ externalActive: true });
    assert(m.owner === 'external' && m.state === 'conflict-locked' && m.writeAfterSuspend === 0,
      'third-party takeover was overwritten during sleep');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]); m.programSleepRequest();
    // A user-cancelled program sleep keeps the curve and permits a later edit.
    m.queryCancelled();
    assert(m.state === 'ready' && m.owner === 'yeman' && m.lease &&
      m.apply([0, 35, 75, 100]), 'cancelled program sleep lost the active curve');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    const captured = m.captureGeneration();
    m.apply([0, 30, 70, 100]);
    m.programSleepRequest(); m.confirmedSuspend(); m.runCleanup();
    assert(!m.apply([0, 45, 80, 100], captured) && m.rejectedAfterSuspend === 1 && m.writes === 1,
      'old in-flight parameter request wrote after confirmed suspend');
    passed += 1;
  }
  {
    const m = new SleepApplyModel();
    m.apply([0, 30, 70, 100]);
    m.query();
    m.queryCancelled();
    m.apply([0, 20, 60, 100]);
    m.query(); m.confirmedSuspend(); m.runCleanup();
    assert(m.writes === 2 && m.restores === 1 && m.owner === 'oem',
      'repeated edit/cancel/sleep sequence was not serial and idempotent');
    passed += 1;
  }
  return passed;
}

const sourceCount = sourceChecks();
const scenarioCount = scenarioChecks();
console.log(`fan sleep/apply conflict selftest: PASS (source=${sourceCount}, scenarios=${scenarioCount}, hardwareWrites=false)`);
