import {
  FanHostLifecycle,
  evaluateFanDeviceGate,
  isLegacyUnauthenticatedFanHostHealth,
  resolveFanEntryAction,
  resolveFanHostConfig,
  resolveFanHostDeployment,
  type FanHostDeploymentIo,
  type FanHostLauncher,
  type FanHostProcess,
} from '../src/bridge/fanHost';
import type { FanApiAdapter, FanHandshake, FanLease, FanNode, FanPreset, FanState } from '../src/bridge/fanApi';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const assert = (condition: unknown, message: string): asserts condition => {
  if (!condition) throw new Error(message);
};

/**
 * FAN-927 §3（2026-09-27 用户批准）：自动唤醒现在**先判意图**——本 run 没有已确认的活动控制
 * 意图时就 NoWork（零 HTTP/零进程/零启动/零等待）。本文件绝大多数用例验证的是"**已有**活动
 * 意图时"的恢复准入/重放/租约语义，因此统一注入一份 test-only 的 native 活动快照
 * （生产实现是 `app.fanActivity()`，node 自测没有 native IPC）。
 * 证据来源因此与被测判据一致：允许恢复的开关就是生产那条意图门，而不是绕过它。
 * 需要验证"无意图 ⇒ NoWork"的用例请直接 `newFanLifecycle({ ..., readNativeActivity: async () => null })`。
 */
const ACTIVE_NATIVE_INTENT = async () => ({
  schemaVersion: 1,
  active: true,
  recoverable: false,
  revision: 1,
  curve: [] as Array<{ tempC: number; dutyPercent: number }>,
});
function newFanLifecycle(options: ConstructorParameters<typeof FanHostLifecycle>[0]): FanHostLifecycle {
  return new FanHostLifecycle({ readNativeActivity: ACTIVE_NATIVE_INTENT, ...options });
}

function state(name = 'Ready'): FanState {
  return {
    state: name,
    hardwareWrites: false,
    hardwareWritesObserved: false,
    // Every cleanup endpoint in the real Host must explicitly attest OEM
    // ownership. Make the fake adapter model the same contract so a missing
    // field can never be accepted accidentally by lifecycle regression tests.
    oemRestoreConfirmed: ['OEM', 'Released', 'Closed', 'Stopped', 'AwaitingControl', 'Suspended'].includes(name),
  };
}

class FakeAdapter implements FanApiAdapter {
  readonly enabled = true;
  readonly calls: string[] = [];
  enableFailures = 0;
  enableFailureMessage = 'TRANSIENT_WRITE_FAILURE';
  openFailures = 0;
  openEventsFailures = 0;
  handshakeFailures = 0;
  suspendFailures = 0;
  restoreFailures = 0;
  closeFailures = 0;
  closePendingOnce = false;
  recoveryStateAfterRestoreFailure: string | null = null;
  recoveryStateAfterCloseFailure: string | null = null;
  remoteTelemetry = false;
  closeSessionOnEnableFailure = false;
  closeSessionOnOpenEventsFailure = false;
  closeBoundaryWithoutHardwareCallbackOnEnableFailure = false;
  private closedHcBoundaryAfterEnableFailure = false;
  private remoteOpen = false;
  private remoteOpenEvents = false;
  private cleanupPendingReads = 0;
  private pendingCloseCompleted = false;
  private recoveryState: string | null = null;
  heartbeatFailures = 0;
  heartbeatFailureMessage = 'LEASE_INVALID';
  unconfirmedCleanupResponses = false;
  directHcCloseWithoutHardwareCallback = false;
  autoHostResume = false;
  routeLossClosesHost = false;
  private autoResumePolls = 0;
  autoResumeCurve: readonly FanNode[] = [];
  readonly handshakeTimeouts: Array<number | undefined> = [];
  readonly stateTimeouts: Array<number | undefined> = [];
  handshakeResult: FanHandshake = {
    ok: true,
    supported: true,
    deviceClass: 'HandheldCompanion.Devices.GPDWin5',
    fanRouteWriteReady: true,
    deviceIdentity: { manufacturer: 'GPD', model: 'G1618-05', product: 'G1618-05', bios: '2.20' },
  };
  lease: FanLease = { leaseId: 'lease-1', generation: 1 };
  async handshake(timeoutMs?: number) {
    this.calls.push('handshake');
    this.handshakeTimeouts.push(timeoutMs);
    if (this.handshakeFailures-- > 0) throw new Error('HANDSHAKE_TRANSIENT_FAILURE');
    return this.handshakeResult;
  }
  async getState(timeoutMs?: number) {
    this.calls.push('state');
    this.stateTimeouts.push(timeoutMs);
    if (this.autoHostResume) {
      this.autoResumePolls += 1;
      if (this.autoResumePolls < 2) return state('Resuming');
      const resumed = state('Ready');
      resumed.protocolVersion = '2';
      resumed.hardwareWritesEnabled = true;
      resumed.activeCurve = this.autoResumeCurve;
      resumed.openCalled = true;
      resumed.openEventsCalled = true;
      resumed.lease = { leaseId: 'resume-lease-1', generation: 1 };
      resumed.leaseGeneration = 1;
      return resumed;
    }
    if (this.cleanupPendingReads > 0) {
      this.cleanupPendingReads -= 1;
      const pending = state('Closed');
      pending.hcCloseCleanupPending = true;
      if (this.cleanupPendingReads === 0) this.pendingCloseCompleted = true;
      return pending;
    }
    if (this.recoveryState) {
      const recovered = state(this.recoveryState);
      if (this.recoveryState === 'AwaitingControl' && this.routeLossClosesHost) {
        recovered.oemRestoreConfirmed = false;
        recovered.hcVirtualCloseReturned = true;
        recovered.hcDeviceManagerStopCompleted = true;
        recovered.openCalled = false;
        recovered.openEventsCalled = false;
      }
      return this.withRemoteSession(recovered);
    }
    if (this.pendingCloseCompleted) return this.withRemoteSession(state('Stopped'));
    // 920 F-3 保真度：真实 Host 的 /api/state **始终**返回 openCalled /
    // openEventsCalled（Program.cs L5527-5528 / L6747-6748），前端正是靠它
    // 在失败后回灌会话标志。替身此前只在 open/openEvents/enable/cleanup 响应里
    // 写这两个字段，等于把 Host 的权威快照弱化成“成功路径才有”——那会让
    // F-3 无法被任何场景检出。这里补齐为与真实 Host 一致。
    return this.withRemoteSession(state());
  }
  async enable(_nodes: readonly FanNode[]) {
    this.calls.push('enable');
    if (this.enableFailures-- > 0) {
      if (this.closeSessionOnEnableFailure) {
        this.remoteOpen = false;
        this.remoteOpenEvents = false;
        if (this.closeBoundaryWithoutHardwareCallbackOnEnableFailure)
          this.closedHcBoundaryAfterEnableFailure = true;
      }
      throw new Error(this.enableFailureMessage);
    }
    return this.withRemoteSession(state('Applied'));
  }
  lastPresetNodes: readonly FanNode[] | undefined;
  async applyPreset(_name: FanPreset, _leaseId?: string, nodes?: readonly FanNode[]) { this.calls.push('preset'); this.lastPresetNodes = nodes; return state('Applied'); }
  async disable() { this.calls.push('disable'); return this.cleanupState('OEM'); }
  async open() {
    this.calls.push('open');
    if (this.openFailures-- > 0) throw new Error('OPEN_PARTIAL_FAILURE');
    this.remoteOpen = true;
    this.remoteOpenEvents = false;
    return this.withRemoteSession(state('Open'));
  }
  async openEvents() {
    this.calls.push('open-events');
    if (this.openEventsFailures-- > 0) {
      if (this.closeSessionOnOpenEventsFailure) {
        this.remoteOpen = false;
        this.remoteOpenEvents = false;
      }
      throw new Error('OPEN_EVENTS_FAILURE');
    }
    this.remoteOpenEvents = true;
    return this.withRemoteSession(state('Events'));
  }
  async acquireControl() { this.calls.push('acquire'); return this.lease; }
  async heartbeat(_leaseId: string) {
    this.calls.push('heartbeat');
    if (this.heartbeatFailures-- > 0) {
      if (this.routeLossClosesHost && /HC_SESSION_ROUTE_LOST|HC_SESSION_UNAVAILABLE|LEASE_INVALID/i.test(this.heartbeatFailureMessage)) {
        // Model the resident C# recovery owner: the failed HID marker stops
        // writes, then one virtual Close/unbind returns AwaitingControl before
        // the bridge is allowed to probe/reopen the route.
        this.remoteOpen = false;
        this.remoteOpenEvents = false;
        this.recoveryState = 'AwaitingControl';
        this.pendingCloseCompleted = true;
      }
      throw new Error(this.heartbeatFailureMessage);
    }
    return this.lease;
  }
  async releaseControl(_leaseId: string) { this.calls.push('release'); return this.cleanupState('Released'); }
  async restoreOem(_leaseId?: string) {
    this.calls.push('restore');
    if (this.closedHcBoundaryAfterEnableFailure) {
      const recovered = state('AwaitingControl');
      recovered.oemRestoreConfirmed = false;
      recovered.hcVirtualCloseReturned = true;
      recovered.hcDeviceManagerStopCompleted = true;
      recovered.openCalled = false;
      recovered.openEventsCalled = false;
      this.closedHcBoundaryAfterEnableFailure = false;
      return recovered;
    }
    if (this.restoreFailures-- > 0) {
      if (this.recoveryStateAfterRestoreFailure) this.recoveryState = this.recoveryStateAfterRestoreFailure;
      throw new Error('RESTORE_TRANSIENT_FAILURE');
    }
    return this.cleanupState('OEM');
  }
  async suspend(_request: { generation: number; source: string; leaseId?: string; reason?: string }) {
    this.calls.push('suspend');
    if (this.suspendFailures-- > 0) throw new Error('SUSPEND_TRANSIENT_FAILURE');
    if (this.directHcCloseWithoutHardwareCallback) {
      const suspended = state('Suspended');
      suspended.oemRestoreConfirmed = false;
      suspended.hcVirtualCloseReturned = true;
      suspended.hcDeviceManagerStopCompleted = false;
      return suspended;
    }
    return state('Suspended');
  }
  async resume() {
    this.calls.push('resume');
    if (this.autoHostResume) { this.autoResumePolls = 0; return state('Resuming'); }
    return state('Resumed');
  }
  async close() {
    this.calls.push('close');
    if (this.closeFailures-- > 0) {
      if (this.recoveryStateAfterCloseFailure) this.recoveryState = this.recoveryStateAfterCloseFailure;
      throw new Error('CLOSE_TRANSIENT_FAILURE');
    }
    if (this.closePendingOnce) {
      this.closePendingOnce = false;
      this.cleanupPendingReads = 1;
      this.pendingCloseCompleted = false;
      const pending = state('Closed');
      pending.hcCloseCleanupPending = true;
      return pending;
    }
    this.remoteOpen = false;
    this.remoteOpenEvents = false;
    return this.cleanupState('Stopped');
  }
  async shutdown() { this.calls.push('shutdown'); }
  private withRemoteSession(result: FanState): FanState {
    if (this.remoteTelemetry) {
      result.openCalled = this.remoteOpen;
      result.openEventsCalled = this.remoteOpenEvents;
    }
    return result;
  }
  private cleanupState(name: string): FanState {
    const result = state(name);
    if (this.directHcCloseWithoutHardwareCallback && name === 'Stopped') {
      result.oemRestoreConfirmed = false;
      result.hcVirtualCloseReturned = true;
      result.hcDeviceManagerStopCompleted = true;
    }
    if (this.unconfirmedCleanupResponses) delete result.oemRestoreConfirmed;
    return this.withRemoteSession(result);
  }
}

class FakeLauncher implements FanHostLauncher {
  readonly calls: string[] = [];
  async start(_config: ReturnType<typeof resolveFanHostConfig>): Promise<FanHostProcess> {
    this.calls.push('start');
    return { pid: 1234, executable: 'fake-host.exe' };
  }
  async stop(_process: FanHostProcess): Promise<void> { this.calls.push('stop'); }
}

/** FAN-927 §5.1：报告"已成会话仍活着"的宿主快照（渲染器重建后只接上、不重开 HC）。 */
class LiveSessionAdapter extends FakeAdapter {
  async getState(_timeoutMs?: number): Promise<FanState> {
    this.calls.push('state');
    const live = state('Ready');
    live.protocolVersion = '2';
    live.hardwareWritesEnabled = true;
    live.openCalled = true;
    live.openEventsCalled = true;
    return live;
  }
}

class ExpiringLeaseAdapter extends FakeAdapter {
  private nextLease = 1;
  async acquireControl() {
    this.calls.push('acquire');
    return { leaseId: `lease-${this.nextLease++}`, generation: this.nextLease };
  }
  async heartbeat(_leaseId: string) {
    this.calls.push('heartbeat-invalid');
    throw new Error('LEASE_INVALID');
  }
}

/** 支持会话令牌重绑的适配器：现行启动握手唯一允许的那一次重试前提
 * （fanHost.ts startInternal 的 handshake-retry 分支要求 setSessionToken 存在）。 */
class TokenRebindAdapter extends FakeAdapter {
  readonly tokenRebinds: string[] = [];
  setSessionToken(token: string): void { this.tokenRebinds.push(token); }
}

async function main(): Promise<void> {
  assert(isLegacyUnauthenticatedFanHostHealth({ status: 200, body: '{"host":"YeManFanHost","protocolVersion":2}' }, 2),
    'legacy host health classifier must accept an unauthenticated protocol-2 response');
  assert(!isLegacyUnauthenticatedFanHostHealth({ status: 401, body: '{"error":"API_SESSION_REQUIRED"}' }, 2),
    'legacy host health classifier must reject an authenticated token mismatch response');
  assert(!isLegacyUnauthenticatedFanHostHealth({ status: 200, body: '{"host":"other-service","protocolVersion":2}' }, 2),
    'legacy host health classifier must reject an unrelated loopback service');
  // Validate the active worktree source. A stale absolute migration copy can
  // otherwise make lifecycle gates pass while the packaged FanHost diverges.
  const hostSource = readFileSync('FanLab/real-host/Program.cs', 'utf8');
  // Read the bridge under test from the active worktree as well.
  const bridgeSource = readFileSync('src/bridge/fanHost.ts', 'utf8');
  const apiSource = readFileSync('src/bridge/api.ts', 'utf8');
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const profileCallbackSources = [
    'deps/handheldcompanion-runtime/source/Devices/MSI/ClawA1M.cs',
    'deps/handheldcompanion-runtime/source/Devices/Lenovo/LegionGo.cs',
    'deps/handheldcompanion-runtime/source/Devices/Lenovo/LegionGoTablet2.cs',
    'deps/handheldcompanion-runtime/source/Devices/ASUS/ROGAlly.cs',
  ].map((path) => readFileSync(path, 'utf8'));
  const deviceMatrix = readFileSync('FanLab/evidence/HC-DEVICE-MATRIX-BATCH03-20260817.md', 'utf8');
  const mappedMatrixClasses = [...deviceMatrix.matchAll(/^\| `([^`]+)` .*\| `SourceMappedFan` \|$/gm)].map((match) => match[1]);
  const unsupportedMatrixClasses = [...deviceMatrix.matchAll(/^\| `([^`]+)` .*\| `UnsupportedNoFanCapability` \|$/gm)].map((match) => match[1]);
  const routeRegistryStart = hostSource.indexOf('private static IReadOnlyDictionary<string, FanRoute> BuildFanRoutes()');
  const routeRegistryEnd = hostSource.indexOf('\n    private void LoadAssemblyAndFactory()', routeRegistryStart);
  const routeRegistrySource = hostSource.slice(routeRegistryStart, routeRegistryEnd);
  assert(mappedMatrixClasses.length === 70 && unsupportedMatrixClasses.length === 10,
    'frozen HC Batch 03 device matrix must remain 70 mapped fan classes plus 10 unsupported classes');
  const routeMentionsClass = (name: string) =>
    routeRegistrySource.includes(`"${name}"`) ||
    routeRegistrySource.includes(`.Devices.${name}"]`);
  const absentMappedClasses = mappedMatrixClasses.filter((name) => !routeMentionsClass(name));
  const includedUnsupportedClasses = unsupportedMatrixClasses.filter((name) => routeMentionsClass(name));
  assert(routeRegistryStart >= 0 && routeRegistryEnd > routeRegistryStart &&
    absentMappedClasses.length === 0 && includedUnsupportedClasses.length === 0,
  `Host route registry drifted from HC Batch 03 matrix; missing=${absentMappedClasses.join(',')}; unsupported=${includedUnsupportedClasses.join(',')}`);
  assert(routeRegistrySource.includes('routes["HandheldCompanion.Devices.GPDWin4"] = new FanRoute') &&
    routeRegistrySource.includes('FanRestoreStrategy.GpdWin4HcRelease') &&
    !/GPDWin4_20(?:23|24).*GpdWin4HcRelease/.test(routeRegistrySource),
  'GPD Win4 0x1060 unlock must remain restricted to the old GPDWin4 factory route');
  // FAN-938 R6 D8（裁决 §P5）：公共恢复 owner 不变量必须**参数化覆盖全登记路线**。上文的
  // mappedMatrixClasses（70 条 SourceMappedFan 类）已被断言等于 Host 登记路线集合，即登记域。
  // 逐条登记路线断言：①恢复 owner 是唯一的公共路径——桥侧恢复 owner 区不按任何登记路线名分支
  // （即公共修复对全登记路线生效，不要求逐机型改写设备代码）；②逐路线 writer/restore 仍归
  // Host/HC，桥侧公共 owner 区未复制或越权改写某条路线的 writer/restore 常量。
  const recoveryOwnerRegion = bridgeSource.slice(
    bridgeSource.indexOf('private noteRecoveryIntentPending'),
    bridgeSource.indexOf('async close(): Promise<void>'),
  );
  assert(recoveryOwnerRegion.includes('private scheduleRecoveryTick') &&
    recoveryOwnerRegion.includes('private async runRecoveryTick') &&
    recoveryOwnerRegion.includes('private async replayResumeCurve'),
  'D8: the bridge must own exactly one common recovery owner (noteRecoveryIntentPending -> scheduleRecoveryTick/runRecoveryTick -> replayResumeCurve)');
  const d8PerRouteRecoveryOverrides = mappedMatrixClasses.filter((name) => recoveryOwnerRegion.includes(name));
  assert(d8PerRouteRecoveryOverrides.length === 0,
    `D8: the common recovery owner must not branch on any registered route; per-route recovery overrides found for ${d8PerRouteRecoveryOverrides.join(',')}`);
  const asusRestoreStart = hostSource.indexOf('private void MarkHcOemReleaseCallbackCompleted()');
  const asusRestoreBody = hostSource.slice(asusRestoreStart, asusRestoreStart + 1600);
  const hcOpenCoreStart = hostSource.indexOf('private void OpenCore()');
  const hcOpenCoreEnd = hostSource.indexOf('\n    public void OpenEvents()', hcOpenCoreStart);
  const hcOpenCoreBody = hostSource.slice(hcOpenCoreStart, hcOpenCoreEnd);
  // Stale-anchor remap (2026-09-26, FAN-926R batch): the section16 batch split
  // `OpenEventsCore()` into `OpenEventsBeginCore()` (invokes the HC virtual
  // Device method and begins the bounded restore-open) and
  // `OpenEventsFinalizeCore()` (post-boundary bookkeeping). The assertion below
  // is about the invocation boundary, so it must anchor on the Begin core.
  const hcOpenEventsCoreStart = hostSource.indexOf('private HcDeviceOpenNeed OpenEventsBeginCore()');
  const hcOpenEventsCoreEnd = hostSource.indexOf('\n    private void OpenEventsFinalizeCore()', hcOpenEventsCoreStart);
  const hcOpenEventsCoreBody = hostSource.slice(hcOpenEventsCoreStart, hcOpenEventsCoreEnd);
  const releaseStart = hostSource.indexOf('public object Release(JsonElement body)');
  const releaseEnd = hostSource.indexOf('\n    public object Suspend(JsonElement body)', releaseStart);
  const releaseBody = hostSource.slice(releaseStart, releaseEnd);
  const acquireStart = hostSource.indexOf('public FanLease AcquireControl()');
  const acquireEnd = hostSource.indexOf('\n    public FanLease Heartbeat', acquireStart);
  const acquireBody = hostSource.slice(acquireStart, acquireEnd);
  const ensureLeaseStart = hostSource.indexOf('private void EnsureLease(string leaseId)');
  const ensureLeaseEnd = hostSource.indexOf('\n    private void LogIgnoredLeaseForSafetyCleanup', ensureLeaseStart);
  const ensureLeaseBody = hostSource.slice(ensureLeaseStart, ensureLeaseEnd);
  // R1（FAN-926R 唤醒租约裁决 §3.2.5）**因本裁决改要求**：`ExpireLease` 现在必须按
  // "创建它的那个租约"自证身份（签名带 callbackLeaseId/generation），迟到回调不得处置新租约。
  // 锚点同步到新的生产入口，并把"缺失/重复"变成清晰测试错误，避免再次静默取到空串。
  const expireLeaseStart = hostSource.indexOf('private void ExpireLease(string callbackLeaseId, long callbackLeaseGeneration)');
  if (expireLeaseStart < 0 || hostSource.indexOf('private void ExpireLease(', expireLeaseStart + 1) >= 0) {
    throw new Error('R1: ExpireLease(string,long) 生产入口必须恰好存在一处');
  }
  const expireLeaseEnd = hostSource.indexOf('\n    // Lease loss is an emergency cleanup path', expireLeaseStart);
  if (expireLeaseEnd <= expireLeaseStart) {
    throw new Error('R1: ExpireLease 边界定位失败（end<=start 或锚点缺失）');
  }
  const expireLeaseBody = hostSource.slice(expireLeaseStart, expireLeaseEnd);
  const timeoutReturnedStart = hostSource.indexOf('private void OnTimedOutHcOperationReturned(bool operationSucceeded)');
  const timeoutReturnedEnd = hostSource.indexOf('\n    private void OnBackendFanDispatchFailure', timeoutReturnedStart);
  const timeoutReturnedBody = hostSource.slice(timeoutReturnedStart, timeoutReturnedEnd);
  assert(hostSource.includes('VerifyActiveCurveSession') &&
    hostSource.includes('realBackend?.VerifyActiveCurveSession()') &&
     hostSource.includes('hcOemReleaseCallbackCompleted') &&
    hostSource.includes('private void OpenHcDevice()') &&
    hostSource.includes('Invoke(device, "Open")') &&
    !hostSource.includes('StartHcDeviceManager();') &&
    hostSource.includes('Invoke(device!, "OpenEvents")') &&
    // Stale-assertion remap (2026-09-26, FAN-926R batch). The section16 batch
    // replaced the polling helper `EnsureHcDeviceOpenForRestore()` with the
    // bounded, callback-verified `BeginHcDeviceOpenForRestore()` state machine.
    // The original intent - "a restore must reach the HC device through one
    // bounded, callback-confirmed open path and must not invent a HID/ACPI
    // wait" - is preserved and tightened here: the new contract is pinned
    // explicitly and the removed helper must not come back as a call.
    hostSource.includes('private HcDeviceOpenNeed BeginHcDeviceOpenForRestore()') &&
    hostSource.includes('return BeginHcDeviceOpenForRestore();') &&
    hostSource.includes('private enum HcDeviceOpenNeed { None, WaitForDeviceInserted }') &&
    hostSource.includes('InvokeHcDeviceInserted(device);') &&
    hostSource.includes('hc-device-open-confirmed') &&
    hostSource.includes('hc-device-inserted.dispatched') &&
    !hostSource.includes('EnsureHcDeviceOpenForRestore();') &&
    !hostSource.includes('HC_DEVICE_NOT_OPEN_FOR_RESTORE') &&
    !hostSource.includes('private void WaitForHcDeviceReady()') &&
    !hostSource.includes('HC_DEVICE_OPEN_TIMEOUT') &&
    hostSource.includes('private void CloseHcDevice()') &&
    hostSource.includes('Invoke(device!, "Close")') &&
    hostSource.includes('CaptureHcProfileTemplate();') &&
    (hostSource.includes('CloneHcPowerProfilePreservingFanState(') ||
      hostSource.includes('CloneHcPowerProfile(')) &&
    hostSource.includes('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));') &&
    hostSource.includes('restore.close-hc-failure') &&
    hostSource.includes('HC Close 资源清理失败，等待重试') &&
    asusRestoreStart >= 0 &&
    hostSource.includes('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));') &&
    hostSource.includes('private bool ConfirmAsusOemReadback(') &&
    hostSource.includes('private bool TryReadAsusDefaults(') &&
    !hostSource.includes('private bool TryWriteAsusDefaultsDirect(') &&
    hostSource.includes('InvokeStaticMember(acpi, "GetFanCurve"') &&
    hostSource.includes('restore.asus-default-readback-unconfirmed') &&
    !hostSource.includes('restore.asus-default-direct-fallback') &&
    !hostSource.includes('CaptureAsusBaseline') &&
    !hostSource.includes('OpenFanOnlyDevice') &&
    !hostSource.includes('CloseDeviceWithoutManagerFactory') &&
    hostSource.includes('public bool HcVirtualCloseReturned { get; set; }') &&
    hostSource.includes('public bool HcDeviceManagerStopCompleted { get; set; }') &&
    hostSource.includes('public bool OemPhysicalOwnershipConfirmed { get; set; }') &&
    hostSource.includes('hc-callback-only-physical-unknown'),
  'ROG restore must use HC Hardware profile first; default curve readback is diagnostic-only');
  assert(releaseStart >= 0 && releaseEnd > releaseStart &&
    releaseBody.includes('RestoreHardware(close: false)') &&
    !releaseBody.includes('RestoreHardware(close: true)') &&
    acquireStart >= 0 && acquireEnd > acquireStart &&
    acquireBody.includes('RestoreHardware(close: false)') &&
    !acquireBody.includes('RestoreHardware(close: true)') &&
    ensureLeaseStart >= 0 && ensureLeaseEnd > ensureLeaseStart &&
    ensureLeaseBody.includes('RestoreHardware(close: false)') &&
    !ensureLeaseBody.includes('RestoreHardware(close: true)') &&
    expireLeaseStart >= 0 && expireLeaseEnd > expireLeaseStart &&
    // R1（FAN-926R §3.2.9）**因本裁决改要求**：ExpireLease 的释放调用现在带**责任来源**
    // `cause: "lease-expiry"`，字面量 `RestoreHardware(close: false)` 后面是逗号而不是右括号。
    // 断言意图不变（必须 close:false 释放、绝不得 close:true），锚点放宽到不带右括号的前缀。
    expireLeaseBody.includes('RestoreHardware(close: false') &&
    !expireLeaseBody.includes('RestoreHardware(close: true') &&
    timeoutReturnedStart >= 0 && timeoutReturnedEnd > timeoutReturnedStart &&
    timeoutReturnedBody.includes('var completedHcClose =') &&
    timeoutReturnedBody.includes('(completedHcClose || backend.OemRestoreVerified)') &&
    timeoutReturnedBody.includes('hc.timeout-returned-after-completed-boundary') &&
    timeoutReturnedBody.includes('hc.timeout-returned-close-failure-after-oem-restore') &&
    hostSource.includes('TimedOutOperationReturned?.Invoke(workItem.Failure is null)') &&
    hostSource.includes('var timedOut = realBackend.OperationTimedOut;') &&
    hostSource.includes('state.HcCloseCleanupPending = true;') &&
    bridgeSource.includes('function assertHcSessionClosed(state: FanState, context: string): void') &&
    bridgeSource.includes('HC Open/OpenEvents 会话仍未释放') &&
    bridgeSource.includes('HC DeviceManager 清理尚未完成') &&
    hostSource.includes('return false;'),
  'HC profile release must not close the device, and any post-restore Close exception must preserve OEM proof without reporting success');
  const verifySessionStart = hostSource.indexOf('private void VerifyActiveCurveSessionCore()');
  const verifySessionEnd = hostSource.indexOf('\n    public void RestoreOem()', verifySessionStart);
  const verifySessionBody = hostSource.slice(verifySessionStart, verifySessionEnd);
  assert(verifySessionStart >= 0 && verifySessionEnd > verifySessionStart &&
    verifySessionBody.includes('EnsureHcSessionReadyForControl()') &&
    !verifySessionBody.includes('GetFan') &&
    !verifySessionBody.includes('GetSmartFanMode') &&
    !verifySessionBody.includes('GetShiftValue') &&
    !hostSource.includes('FAN_ROUTE_CONFLICT') &&
    !hostSource.includes('CaptureGenericEcBaseline') &&
    !hostSource.includes('ConfirmAppliedMsiCurve') &&
    !hostSource.includes('ConfirmAppliedLenovoCurve'),
  'lease heartbeat must validate only the live HC session, never infer external ownership from vendor readback');
  assert(hostSource.includes('HC_SESSION_ROUTE_LOST') &&
    hostSource.includes('skipOemRestore: routeLost') &&
    hostSource.includes('CloseHcSessionForLifecycle(stopDeviceManager, skipOemRestore)'),
  'HID route loss must use the resident HC Close/unbind owner without a second OEM callback');
  const systemPendingStart = hostSource.indexOf('private object SuspendUnlocked()');
  const systemPendingEnd = hostSource.indexOf('private ResumeAdmission StartAutomaticResumeUnlocked(', systemPendingStart);
  const systemPendingBody = hostSource.slice(systemPendingStart, systemPendingEnd);
  // D1（FAN-926R 重钉/恢复/导出裁决 §3）**因本裁决改要求**：SystemPending 不再"无条件
  // skipOemRestore=true / Close-first"。现行合同 = "有写历史且会话仍有效 ⇒ 关闭 HC 前做一次
  // **有界**默认模式恢复；否则显式 not-attempted"，并由三轴决策事件记账。
  // 本条要证明的意图逐条保留并收紧：① SuspendUnlocked 仍只走 CloseForSystemPending；
  // ② 不得在系统电源通知路径上做无条件 close:true 复原或直接 RestoreOem；
  // ③ 撤回"默认模式是否已交还"的证据口径（not-observed）与恢复重试的 Close-first 归属不变；
  // ④ 不得回到无条件 skipOemRestore/clearOemEvidence=true。
  assert(systemPendingStart >= 0 && systemPendingEnd > systemPendingStart &&
    systemPendingBody.includes('CloseForSystemPending()') &&
    !systemPendingBody.includes('RestoreHardware(close: true') &&
    !systemPendingBody.includes('RestoreOem(') &&
    hostSource.includes('power.suspend-preclose-release-decision') &&
    hostSource.includes('skipOemRestore: !preCloseReleaseAttempted') &&
    hostSource.includes('clearOemEvidence: false') &&
    !hostSource.includes('skipOemRestore: true') &&
    !hostSource.includes('clearOemEvidence: true') &&
    hostSource.includes('state.OemRestoreEvidence = "not-observed"') &&
    hostSource.includes('recoveryRetryUsesSystemPendingClose') &&
    hostSource.includes('realBackend.Close(stopDeviceManager);') &&
    !hostSource.includes('ExecuteRestoreBeforeCloseBoundary'),
  'F4 SystemPending must stay on the bounded, explicitly-decided HC close-first contract: no unconditional pre-close OEM/default write and no fabricated physical ownership');
  // 920 §8.0-D B5 remap: the old assertion pinned deleted symbols (recoverAfterHidRemoval /
  // private recoveryOwner / 'Open -> OpenEvents -> lease'). The deletion is self-documented in
  // fanHost.ts (E9 T0 trim, L1923-1924). Intent preserved: the bridge must keep exactly one
  // recovery owner and must rebuild only after a stable route snapshot.
  // Current equivalent: recovery ownership belongs to the Host's serialized recovery worker
  // (no second frontend guard/retry owner); rebuild reads a read-only getState() snapshot
  // first and then re-writes the curve through the event-driven applyMutation path.
  // 920-v1.2 W1: the single bounded retry is the shared mutateWithBoundedRetry helper, and
  // resume() re-writes through it (v2 no longer calls applyMutation with a bare single shot).
  // G7（FAN-926R 执行单 §3.3）**因本裁决改要求**：resume() 现在把"准入 + 单次入队执行"整个
  // 交给 mutateWithBoundedRetry（重试/等待一律在队列**外**），重放体抽到 replayResumeCurve()，
  // 因此箭头函数调用点变成"重放体内部的 await this.applyMutation(this.desiredCurve…)"。
  // 断言意图不变：唯一恢复 owner + 先只读快照再走同一 applyMutation 重写曲线。
  assert(bridgeSource.includes('recovery worker is the only retry owner') &&
    bridgeSource.includes('recoverLockedHostBeforeStart / recoverAfterMutationFailure / recoverAfterHidRemoval / isObservedRouteLoss 已删') &&
    !bridgeSource.includes('private recoveryOwner') &&
    !bridgeSource.includes('recoverAfterHidRemoval(') &&
    bridgeSource.includes('const remote = await this.adapter.getState().catch(() => null);') &&
    bridgeSource.includes('mutateWithBoundedRetry(') &&
    (bridgeSource.includes('this.applyMutation(this.desiredCurve as readonly FanNode[])') || bridgeSource.includes('this.applyMutation(cloneFanNodes(latestCurve))') || bridgeSource.includes('this.applyMutation(this.desiredCurve as readonly FanNode[], intentRevision)')) &&
    bridgeSource.includes('() => this.replayResumeCurve(generation, intentRevision)'),
  'bridge must retain one recovery owner (Host serialized worker + the single shared mutateWithBoundedRetry) and rebuild only after a read-only route snapshot');
  // 920 §8.0-D B5 remap: the old assertion demanded "a resident ten-second guard with unbounded
  // retry". Its `!RECOVERY_WINDOW_MS` / `!RECOVERY_MAX_ATTEMPTS` clauses directly contradicted
  // 920 §6.2 (one 60-second budget per recovery event, never refreshed, no unbounded retry), and
  // the guard keep-alive chain was deleted in E9 (self-documented in fanHost.ts L2026-2028).
  // Intent preserved: no second resident retry owner, no unbounded retry, no AC/DC-triggered
  // fan recovery.
  assert(bridgeSource.includes('YMCC no longer schedules a') &&
    bridgeSource.includes('resident fan-guard loop.') &&
    bridgeSource.includes('armFanGuard / disarmFanGuard / emitFanGuardState 已删（guard 保活链不复活）') &&
    bridgeSource.includes('private desiredCurve: FanNode[] | null = null;') &&
    !bridgeSource.includes('private fanGuardArmed = false;') &&
    !bridgeSource.includes('runFanGuardOnce') &&
    !bridgeSource.includes('scheduleFanGuard') &&
    !bridgeSource.includes('FAN_GUARD_RESUME_PENDING') &&
    !bridgeSource.includes('const RECOVERY_WINDOW_MS = 60_000;') &&
    !bridgeSource.includes('const RECOVERY_MAX_ATTEMPTS = 3;') &&
    !bridgeSource.includes('notifyPowerSourceChanged('),
  'fan recovery must not add a second resident guard/retry owner, must not retry unboundedly, and must not be AC/DC triggered');
  assert(bridgeSource.includes('private async findExactHostOwner(config: FanHostConfig): Promise<number>') &&
    bridgeSource.includes('const verifiedOwner = await this.findExactHostOwner(config);') &&
    bridgeSource.includes('proc.findExact(config.hostExecutable)') &&
    bridgeSource.includes('let legacyHealth: { status: number; body: string } | undefined;') &&
    bridgeSource.includes('旧 Fan Host 会话令牌不匹配') &&
    bridgeSource.includes('isLegacyUnauthenticatedFanHostHealth(legacyHealth, config.protocolVersion)') &&
    bridgeSource.includes('旧 Fan Host 健康响应来自非当前 YeManFanHost') &&
    bridgeSource.includes('const liveWrites = closedState?.hardwareWritesEnabled === true || closedState?.hardwareWrites === true;') &&
    bridgeSource.includes('function hasExplicitIncompleteHcCloseEvidence(state: unknown, requireDeviceManagerStop: boolean): boolean') &&
    (bridgeSource.match(/hasExplicitIncompleteHcCloseEvidence\(closedState, true\)/g) ?? []).length >= 2 &&
    bridgeSource.includes('function hasAcceptedStoppedHcCloseEvidence(state: unknown): boolean') &&
    bridgeSource.includes("typeof remote.state === 'string'") &&
    bridgeSource.includes('HC Close 资源清理在等待窗口内未完成'),
  'startup recovery must verify the exact loopback Host process before sending close/shutdown and reject live-write or incomplete HC Close telemetry');
  const adoptResident = bridgeSource.indexOf('tryAdoptAuthenticatedResidentHost(config)');
  const prePayloadRecovery = bridgeSource.indexOf('recoverPreviousHostBeforePayloadMutation(config)');
  const adoptAfterRecover = bridgeSource.indexOf('resident-host-adopted-after-recover');
  const liveSessionKept = bridgeSource.indexOf('resident-host-live-session-kept');
  // FAN-927 §2（2026-09-27 用户批准）：部署验证收敛到 ensureFanHostDeployment 的唯一入口
  // （正常开启只核对小部署记录；记录缺失/不匹配才在内部调用 installAndVerifyPayload 做一次
  // 完整验证）。锚点从旧的"每次启动都跑安装器"改为这个唯一入口，顺序语义保持不变。
  const payloadInstall = bridgeSource.indexOf('ensureFanHostDeployment(config, fanStateDirectory)');
  const tokenCreation = bridgeSource.indexOf('config.sessionToken = createSessionToken()');
  assert(adoptResident >= 0 && prePayloadRecovery > adoptResident && adoptAfterRecover > prePayloadRecovery && liveSessionKept >= 0 && payloadInstall > adoptAfterRecover && tokenCreation > payloadInstall &&
    bridgeSource.includes('private async tryAdoptAuthenticatedResidentHost(config: FanHostConfig): Promise<FanHostProcess | null>') &&
    bridgeSource.includes('resident-host-adopted') &&
    bridgeSource.includes('resident-host-live-unresolved-identity') &&
    bridgeSource.includes('isLiveResidentFanState(remoteState)') &&
    bridgeSource.includes('rememberDesiredCurve') &&
    bridgeSource.includes('adopted?: boolean') &&
    bridgeSource.includes('private async recoverPreviousHostBeforePayloadMutation(config: FanHostConfig): Promise<void>') &&
    bridgeSource.includes('会话文件缺失但端点要求会话令牌') &&
    bridgeSource.includes('已拒绝无认证关闭请求') &&
    bridgeSource.includes('旧 Fan Host 会话文件缺失且健康端点不可用') &&
    bridgeSource.includes('await this.recoverLegacyUnauthenticatedHost(config);'),
  'startup must adopt a live authenticated Host before Close/shutdown, then recover leftovers before mutating payload ACL/files');
  assert(apiSource.includes("fanStateDir: () => invoke<string>('app.fanStateDir'") &&
    nativeSource.includes('static std::wstring fan_host_state_dir()') &&
    nativeSource.includes('ipc_on("app.fanStateDir"') &&
    nativeSource.includes('const auto path = fan_host_state_dir() + L"\\\\YeManFanHost.session";') &&
    bridgeSource.includes('const fanStateDirectory = await app.fanStateDir();') &&
    bridgeSource.includes("const legacyPath = joinWindowsPath(legacyDataDirectory, 'fan-host\\\\YeManFanHost.session');") &&
    bridgeSource.includes('Fan Host 会话令牌未可靠落盘') &&
    bridgeSource.includes('const persistedToken = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim();'),
  'renderer, native exit recovery and emergency tooling must share one stable session location, and the capability must be read back before any Host can launch');

  const keepaliveMayStart = nativeSource.indexOf('static bool fanLeaseKeepaliveMayRun()');
  const keepaliveTickStart = nativeSource.indexOf('static void fanLeaseKeepaliveTick()');
  const keepalivePostStart = nativeSource.indexOf('static bool fanLeaseKeepalivePostHeartbeat(');
  const keepaliveObserveStart = nativeSource.lastIndexOf('static void fanLeaseKeepaliveObserveHttp(');
  // FAN-927 §3/§5：keepalive 观察入口之后新增了**独立的** Fan 活动记录区（它必须认
  // /api/enable 与 /api/preset 才可能记录活动意图）。这里把"keepalive tick 体内不得出现
  // enable/preset"的旧锚点收窄到该新区块之前，保持原判据不放松：它约束的是续租 tick，
  // 不是活动记录这一独立职责。
  const fanActivitySectionStart = nativeSource.indexOf('// ── FAN-927（2026-09-27 用户批准 §3/§5）：Fan 活动记录', keepaliveTickStart);
  const keepaliveTickBody = nativeSource.slice(
    keepaliveTickStart,
    fanActivitySectionStart > keepaliveTickStart ? fanActivitySectionStart : keepaliveObserveStart,
  );
  const keepalivePostBody = nativeSource.slice(keepalivePostStart, keepaliveTickStart);
  const keepaliveMayBody = nativeSource.slice(keepaliveMayStart, keepalivePostStart);
  assert(keepaliveMayStart >= 0 && keepaliveTickStart > keepaliveMayStart && keepalivePostStart > keepaliveMayStart &&
    keepaliveObserveStart > keepaliveTickStart && keepalivePostStart < keepaliveTickStart &&
    keepaliveMayBody.includes('PowerLifecycle::Ready') &&
    keepaliveTickBody.includes('fanLeaseKeepaliveMayRun()') &&
    keepaliveTickBody.includes('fanLeaseKeepalivePostHeartbeat') &&
    keepalivePostBody.includes('L"/api/heartbeat"') &&
    !keepaliveTickBody.includes('/api/enable') &&
    !keepaliveTickBody.includes('/api/preset') &&
    !keepalivePostBody.includes('/api/enable') &&
    nativeSource.includes('fanLeaseKeepaliveDisarm("power-suspend")') &&
    nativeSource.includes('fanLeaseKeepaliveShutdown();') &&
    nativeSource.includes('native-lease-keepalive-success'),
  'native fan keepalive may only POST /api/heartbeat while Ready and must disarm on confirmed sleep/exit');
  assert(hostSource.includes('private bool HasLiveHardwareSession()') &&
    hostSource.includes('internal static bool HasLiveHardwareSessionForSelfTest(') &&
    hostSource.includes('if (!HasLiveHardwareSession()) return;') &&
    hostSource.includes('state.UnknownState = HasLiveHardwareSession();') &&
    !hostSource.includes('state.UnknownState = state.OpenCalled || state.HardwareWritesObserved;') &&
    !hostSource.includes('var hardwareSessionActive = state.OpenCalled || state.HardwareWritesObserved'),
  'historical HardwareWritesObserved must never be used as the live HC ownership gate');
  const staleHandlerStart = hostSource.indexOf('private void OnBackendTemperatureMonitorStale()');
  const staleHandlerEnd = hostSource.indexOf('\n    internal void MarkOperationTimeoutForSelfTest()', staleHandlerStart);
  const staleHandlerBody = hostSource.slice(staleHandlerStart, staleHandlerEnd);
  assert(hostSource.includes('public Action<Exception>? FanDispatchFailure { get; set; }') &&
    hostSource.includes('public Action? TemperatureMonitorStale { get; set; }') &&
    hostSource.includes('TemperatureMonitorStale?.Invoke()') &&
    hostSource.includes('FanDispatchFailure?.Invoke(ex)') &&
    hostSource.includes('realBackend.TemperatureMonitorStale = OnBackendTemperatureMonitorStale;') &&
    staleHandlerStart >= 0 && staleHandlerEnd > staleHandlerStart &&
    staleHandlerBody.includes('RestoreHardware(close: false)') &&
    staleHandlerBody.indexOf('state.State = "Ready";') > staleHandlerBody.indexOf('RestoreHardware(close: false)') &&
    staleHandlerBody.indexOf('state.UnknownState = false;') > staleHandlerBody.indexOf('RestoreHardware(close: false)') &&
    staleHandlerBody.indexOf('FaultLocked') > staleHandlerBody.indexOf('return;'),
  'a stale isolated temperature sample must restore OEM and remain resumable; only failed recovery may fault-lock');
  assert(hcOpenCoreStart >= 0 && hcOpenCoreEnd > hcOpenCoreStart &&
    hcOpenCoreBody.indexOf('StartHcDeviceManager();') < 0 &&
    hcOpenCoreBody.indexOf('WaitForHcDeviceReadyBeforeOpen();') >= 0 &&
    hcOpenCoreBody.indexOf('OpenHcDevice();') > hcOpenCoreBody.indexOf('WaitForHcDeviceReadyBeforeOpen();') &&
    hcOpenEventsCoreStart >= 0 && hcOpenEventsCoreEnd > hcOpenEventsCoreStart &&
    !hcOpenEventsCoreBody.includes('StartHcDeviceManager();') &&
    hcOpenEventsCoreBody.includes('Invoke(device!, "OpenEvents")') &&
    hostSource.includes('public bool HcCloseIsOwed => IsOpen || (openAttempted && hcOpenInvocationStarted);') &&
    (hostSource.includes('hcDeviceManagerLifecycle = "not-started/no-stop-required";') ||
      hostSource.includes('ManagerFactoryNotStarted')),
  'fan-only activation order must be IsReady probe -> Open -> OpenEvents; a failed Open must not manufacture a DeviceManager stop or Close write');
  assert(hcOpenCoreBody.includes('openAttempted = false;') &&
    hcOpenCoreBody.includes('oemBaselineCaptured = false;') &&
    hostSource.includes('A failed HC Open() is not an active fan session') &&
    hostSource.includes('MainWindow simply stops before OpenEvents') &&
    hostSource.includes('if (realBackend.IsOpen || realBackend.OpenAttempted)') &&
    hostSource.includes('state.OpenCalled = false;') &&
    hostSource.includes('state.State = "AwaitingControl";'),
  'a failed HC Open clears the Host session boundary and cannot enter fabricated restore');
  assert(hostSource.includes('HOST_EVENTS_NOT_OPEN') &&
    hostSource.includes('if (realBackend is not null && (!state.OpenCalled || !state.OpenEventsCalled))'),
  'lease admission cannot precede HC OpenEvents');
  const directHostWriters = [
    'WriteEcByte', 'ECRamDirectWriteByte', 'WriteMsiWmiFanTable',
    'WriteMsiWmiData', 'ApplyMsiFanCurve', 'ApplyMsiHcDefaultRelease',
    'ApplyLenovoFanCurve', 'ApplyLenovoHcDefaultTable',
    'ApplyLegionGo2FanCurve', 'ApplyLegionGo2OemRelease',
    'ApplySmartFanModeBaseline', 'InvokeSetFanControl',
  ];
  assert(directHostWriters.every((name) => !hostSource.includes(name)) &&
    hostSource.includes('ApplyPowerProfile(profile);') &&
    hostSource.includes('ApplyPowerProfile(BuildPowerProfile(Array.Empty<double>(), software: false));'),
  'all non-temperature fan writes must pass through HC PowerProfileManager_Applied, never a Host-side vendor writer');
  assert(profileCallbackSources.every((source) => (source.match(/\bsource\b/g) ?? []).length === 1) &&
    /Enum\.Parse\(updateType,\s*"Background"(?:,\s*ignoreCase:\s*false)?\)/.test(hostSource),
  'HC device fan callbacks must remain UpdateSource-independent; Host uses the upstream Background context');
  const temperatureDispatchStart = hostSource.indexOf('private void OnHcCpuTemperatureChanged(float? value)');
  const temperatureDispatchBody = hostSource.slice(temperatureDispatchStart, temperatureDispatchStart + 2600);
  const hcPowerProfileDispatches = hostSource.match(/Invoke\("PowerProfileManager_Applied"/g) ?? [];
  const hcFanDutyDispatches = hostSource.match(/Invoke\("SetFanDuty"/g) ?? [];
  assert(hcPowerProfileDispatches.length === 1 &&
    hcFanDutyDispatches.length === 1 &&
    temperatureDispatchStart >= 0 &&
    temperatureDispatchBody.includes('Invoke(activeFanProfile, "SetTemperature", temp)') &&
    temperatureDispatchBody.includes('Invoke("SetFanDuty", duty)') &&
    !temperatureDispatchBody.includes('temp < 0') && !temperatureDispatchBody.includes('temp > 100'),
  'the only Host fan dispatches must be HC PowerProfileManager_Applied plus the allowed HC FanProfile/SetFanDuty temperature callback, with HC owning range handling');
  assert(hostSource.includes('HWiNFOTemperatureMonitor') &&
    hostSource.includes('ReadSnapshot') &&
    hostSource.includes('SelectTemperatureForSelfTest') &&
    hostSource.includes('HWiNFO.shared-memory') &&
    hostSource.includes('SharedMemoryNames') &&
    hostSource.includes('ReadAnsi') &&
    hostSource.includes('legacyLhmAssembly') &&
    hostSource.includes('Do not make startup depend on being able to hash that optional file') &&
    hostSource.includes('OnHcCpuTemperatureSampled'),
  'temperature monitor must consume the existing fresh HWiNFO snapshot, keep invalid/stale data out of HC fan dispatch, and avoid making startup depend on hashing the legacy HC monitor assembly');
  const engineCloseStart = hostSource.indexOf('public object Close()');
  const engineCloseBody = hostSource.slice(engineCloseStart, engineCloseStart + 1800);
  assert(engineCloseStart >= 0 &&
    engineCloseBody.indexOf('BlockWritesForClose();') >= 0 &&
    engineCloseBody.indexOf('BlockWritesForClose();') < engineCloseBody.indexOf('lock (gate)') &&
    hostSource.includes('Volatile.Write(ref closeWriteBlocked, 1);') &&
    hostSource.includes('realBackend?.BlockWritesForClose();') &&
    hostSource.includes('if (Volatile.Read(ref closeWriteBlocked) == 0)') &&
    hostSource.includes('"HOST_CLOSING"'),
  'close must block future temperature/API writes before waiting on the Host engine lock, and a late Resume must not reopen that gate');

  const disabledAdapter = new FakeAdapter();
  const disabledLauncher = new FakeLauncher();
  const disabled = newFanLifecycle({ enabled: false, adapter: disabledAdapter, launcher: disabledLauncher });
  const disabledGate = await disabled.start();
  await disabled.suspend();
  await disabled.resume();
  await disabled.close();
  assert(disabled.state === 'disabled', 'dark launch must remain disabled');
  assert(disabledAdapter.calls.length === 0 && disabledLauncher.calls.length === 0,
    'dark launch must not call adapter or launcher');
  assert(!evaluateFanDeviceGate({ ok: false, supported: false, reason: 'unsupported' }).allowed,
    'unsupported handshake must fail closed');
  assert(evaluateFanDeviceGate({
    ok: true,
    supported: true,
    deviceClass: 'HandheldCompanion.Devices.GPDWin4',
    fanRoute: 'GenericDuty',
  }).allowed && !evaluateFanDeviceGate({
    ok: true,
    supported: true,
    deviceClass: 'HandheldCompanion.Devices.GPDWin4',
    fanRoute: 'GenericDuty',
  }).writeReady, 'mapped but unverified HC route must pass handshake-only gate without enabling writes');
  assert(!disabledGate.allowed, 'disabled gate result must be denied');

  // A mapped-but-unverified route may remain visible for handshake UX, but a
  // control request must stop before Open() and never enter recovery/fault
  // handling as if hardware had been touched.
  const readOnlyAdapter = new FakeAdapter();
  readOnlyAdapter.handshakeResult = {
    ...readOnlyAdapter.handshakeResult,
    deviceClass: 'HandheldCompanion.Devices.GPDWin4',
    fanRoute: 'GenericDuty',
    fanRouteWriteReady: false,
  };
  const readOnlyLifecycle = newFanLifecycle({ enabled: true, adapter: readOnlyAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  const readOnlyGate = await readOnlyLifecycle.start();
  assert(readOnlyGate.allowed && !readOnlyGate.writeReady, 'unverified mapped route must be handshake-visible but not write-ready');
  let readOnlyRejected = false;
  try { await readOnlyLifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 50 }, { tempC: 100, dutyPercent: 90 }]); }
  catch { readOnlyRejected = true; }
  assert(readOnlyRejected && !readOnlyAdapter.calls.includes('open'), 'unverified route must reject before Open()');
  await readOnlyLifecycle.close();

  const adapter = new FakeAdapter();
  const launcher = new FakeLauncher();
  const lifecycle = newFanLifecycle({ enabled: true, adapter, launcher, heartbeatIntervalMs: 0 });
  const gate = await lifecycle.start();
  assert(gate.allowed && lifecycle.state === 'awaiting-control', 'authorized device must handshake without enabling control');
  assert(lifecycle.coordinatorSnapshot.phase === 'awaiting-hc-ready',
    'handshake must establish only a FanHost session, not HC SystemReady');
  assert(adapter.calls.slice(0, 1).join(',') === 'handshake',
    'startup call order must begin with handshake-only');
  await lifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(lifecycle.state === 'ready', 'explicit apply must enable control');
  assert(lifecycle.coordinatorSnapshot.phase === 'ready',
    'coordinator must become ready only after Open/OpenEvents/lease admission');
  assert(adapter.calls.slice(0, 5).join(',') === 'handshake,open,open-events,acquire,enable',
    'first apply must open -> open-events -> acquire -> enable');
  lifecycle.setPowerGeneration(1);
  await lifecycle.suspend();
  assert(lifecycle.state === 'suspended', 'suspend must reach suspended');
  const suspendCalls = adapter.calls.slice(5).join(',');
  // 920 §8.0-D B5 remap: suspend used to send exactly one adapter.suspend (the HC virtual Close).
  // E9 moved that single Close owner to the Host/native F4 path, so the frontend now sends zero
  // messages and only clears its own lease/session (fanHost.ts L1800-1810, self-documented:
  // "suspend 零消息 ... 不发 adapter.suspend/协商"). Intent preserved: exactly one owner performs
  // the HC Close, and the frontend must not duplicate it.
  assert(suspendCalls === '',
    `suspend must send no adapter message; the Host is the single HC Close owner (calls=${suspendCalls})`);
  lifecycle.observePowerBoundary('resuming', 1);
  lifecycle.observePowerBoundary('resume-ready', 1);
  await lifecycle.resume();
  assert(lifecycle.state === 'ready', 'resume must rebuild through one serialized event-driven attempt');
  // 920 §8.0-D B5 remap: the old expectation was 'state,resume,handshake,open,open-events,acquire,enable'
  // (guard + adapter.resume negotiation window). E9 replaced that with: one read-only state snapshot,
  // re-acquire the lease, then rewrite the curve via applyMutation (fanHost.ts L1830-1842,
  // self-documented "无 adapter.resume/协商/观察窗"). suspend() now adds zero calls, so the resume
  // slice starts at index 5 instead of 6.
  // R2（FAN-926R 唤醒租约与有界等待裁决 §4.2）**因本裁决改要求**：resume 在**入队前**必须先做
  // 一次只读的恢复期准入探测（只在睡眠/恢复窗内发生），确认宿主是"恢复中"才进入有界等待；
  // 因此该窗口内的读取次数由 1 次变为 2 次（探测 + 入队后按权威快照回灌会话标志）。
  // 这不是放宽：宿主侧仍必须由同一序列化事务完成 Open/OpenEvents/lease/Enable，
  // 且"宿主自己重建"的用例（slowResume）必须在协调器会话完整后才算成功。
  assert(adapter.calls.slice(5).join(',') === 'state,state,acquire,enable',
    `resume must probe the resume window once before queueing, then read one state snapshot, re-acquire the lease and rewrite the curve (calls=${adapter.calls.slice(5).join(',')})`);

  // Without an armed guard the resume() path must still wait for the Host's own
  // SystemReady rebuild to leave Resuming, then complete the coordinator
  // session before the caller reports F5 success. A Host that answered the
  // first /api/resume with 'Resuming' must never be treated as already
  // recovered (HC SystemReady keeps the device open in its own task).
  const slowResumeAdapter = new FakeAdapter();
  slowResumeAdapter.autoHostResume = true;
  slowResumeAdapter.autoResumeCurve = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 }];
  const slowResume = newFanLifecycle({ enabled: true, adapter: slowResumeAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  const slowResumeGate = await slowResume.start();
  assert(slowResumeGate.allowed && slowResume.state === 'awaiting-control', 'slow-resume host must handshake first');
  // applyPreset without explicit nodes deliberately does not arm the guard, so
  // resume() exercises the direct (non-guard) SystemReady observation path.
  await slowResume.applyPreset('balanced');
  assert(slowResume.state === 'ready', 'slow-resume preset must enable control');
  slowResume.setPowerGeneration(1);
  await slowResume.suspend();
  assert(slowResume.state === 'suspended', 'slow-resume host must suspend');
  slowResume.observePowerBoundary('resuming', 1);
  slowResume.observePowerBoundary('resume-ready', 1);
  await slowResume.resume();
  // 920 §8.0-D B5 remap: this scenario applies a preset without explicit nodes, so no
  // desiredCurve exists at resume time. E9's event-driven resume restores the coordinator
  // session only and then waits for the user (fanHost.ts L1843-1846 "无曲线：仅恢复协调状态"),
  // so 'awaiting-control' is the intended outcome rather than a half-open session.
  // Intent preserved: the coordinator session must be complete (phase=ready) before the caller
  // reports F5 success, and the state must be a control-capable one (not suspended/unknown).
  assert(slowResume.state === 'awaiting-control' && slowResume.coordinatorSnapshot.phase === 'ready' &&
    slowResume.controlReady === true,
  `a Host that resumes with its own rebuild must complete the coordinator session before F5 success (state=${slowResume.state}, phase=${slowResume.coordinatorSnapshot.phase}, controlReady=${slowResume.controlReady})`);
  await slowResume.close();

  // WebView2 recreation after sleep must reattach the Suspended Host, keep the
  // Fan handshake visible, and let the coordinator resume complete SystemReady
  // without Close/shutdown/relaunch.
  // G7（FAN-926R 执行单 §3.2）**因本裁决改要求**：本代唤醒窗内宿主尚报 Suspended 时，
  // 桥只能**只读等待**，不得在它仍报 Suspended 时写入/重建。旧替身"永久 Suspended 也
  // 照写"正是被本裁决关闭的形态。现在替身像真实 Host 一样走完自己的 resume（resume-ready
  // 之后在有限轮询内转为可接管）；断言意图不变：复用常驻 Host 完成重建，不得
  // Close/shutdown/重新拉起。
  class AdoptedSuspendedAdapter extends FakeAdapter {
    private probes = 0;
    async getState(timeoutMs?: number) {
      this.calls.push('state');
      this.stateTimeouts.push(timeoutMs);
      this.probes += 1;
      return state(this.probes >= 2 ? 'Ready' : 'Suspended');
    }
    async resume() {
      this.calls.push('resume');
      return state('Resumed');
    }
  }
  class AdoptedLauncher implements FanHostLauncher {
    readonly calls: string[] = [];
    async start(_config: ReturnType<typeof resolveFanHostConfig>): Promise<FanHostProcess> {
      this.calls.push('start');
      return { pid: 16040, executable: 'fake-host.exe', adopted: true };
    }
    async stop(_process: FanHostProcess): Promise<void> { this.calls.push('stop'); }
  }
  const adoptedAdapter = new AdoptedSuspendedAdapter();
  const adoptedLauncher = new AdoptedLauncher();
  const adoptedLifecycle = newFanLifecycle({
    enabled: true, adapter: adoptedAdapter, launcher: adoptedLauncher, heartbeatIntervalMs: 0,
  });
  const adoptedGate = await adoptedLifecycle.start();
  assert(adoptedGate.allowed && adoptedLifecycle.state === 'suspended',
    `adopted Suspended Host must handshake without fault-locking (state=${adoptedLifecycle.state})`);
  assert(adoptedLauncher.calls.join(',') === 'start' && !adoptedAdapter.calls.includes('close') && !adoptedAdapter.calls.includes('shutdown'),
    `adopted start must not Close/shutdown the resident Host (launcher=${adoptedLauncher.calls.join(',')}, calls=${adoptedAdapter.calls.join(',')})`);
  adoptedLifecycle.rememberDesiredCurve([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 30 }, { tempC: 70, dutyPercent: 75 }, { tempC: 100, dutyPercent: 100 }]);
  adoptedLifecycle.setPowerGeneration(2);
  adoptedLifecycle.observePowerBoundary('resuming', 2);
  adoptedLifecycle.observePowerBoundary('resume-ready', 2);
  await adoptedLifecycle.resume();
  assert(adoptedLifecycle.state === 'ready',
    `coordinator resume after adopted sleep must rebuild control (state=${adoptedLifecycle.state}, calls=${adoptedAdapter.calls.join(',')})`);
  // 920 §8.0-D B5 remap: adapter.resume is no longer called (E9 event-driven resume,
  // fanHost.ts L1814-1842). Intent preserved: an adopted resume must use the resident Host
  // (read one read-only state snapshot from it) and must never launch or stop a replacement.
  assert(adoptedAdapter.calls.includes('state') && !adoptedLauncher.calls.includes('stop') &&
    adoptedLauncher.calls.join(',') === 'start',
    `adopted resume must use the resident Host (state snapshot) instead of launching a replacement (launcher=${adoptedLauncher.calls.join(',')}, calls=${adoptedAdapter.calls.join(',')})`);
  await adoptedLifecycle.close();

  // A protocol-2 resident may be transport-healthy while its HC lifecycle
  // evidence is unavailable. Never promote that snapshot to SystemReady: the
  // next explicit control action must use the normal Open/OpenEvents path.
  class AdoptedReadyAdapter extends FakeAdapter {
    constructor(private readonly includeLifecycleTelemetry: boolean) { super(); }
    async getState(timeoutMs?: number): Promise<FanState> {
      this.calls.push('state');
      this.stateTimeouts.push(timeoutMs);
      const remote = state('Ready');
      remote.protocolVersion = '2';
      remote.hardwareWritesEnabled = true;
      remote.lease = { leaseId: 'resident-lease', generation: 37 };
      remote.leaseGeneration = 37;
      if (this.includeLifecycleTelemetry) {
        remote.openCalled = true;
        remote.openEventsCalled = true;
      }
      return remote;
    }
  }
  const incompleteResident = newFanLifecycle({
    enabled: true, adapter: new AdoptedReadyAdapter(false), launcher: new AdoptedLauncher(), heartbeatIntervalMs: 0,
  });
  const incompleteResidentGate = await incompleteResident.start();
  assert(incompleteResidentGate.allowed && incompleteResident.state === 'awaiting-control' &&
    incompleteResident.coordinatorSnapshot.phase === 'awaiting-hc-ready',
  'a protocol-2 resident without explicit Open/OpenEvents telemetry must not admit HC ready');
  await incompleteResident.close();

  const completeResidentAdapter = new AdoptedReadyAdapter(true);
  const completeResident = newFanLifecycle({
    enabled: true, adapter: completeResidentAdapter, launcher: new AdoptedLauncher(), heartbeatIntervalMs: 0,
  });
  const completeResidentGate = await completeResident.start();
  assert(completeResidentGate.allowed && completeResident.state === 'ready' &&
    completeResident.coordinatorSnapshot.phase === 'ready' &&
    completeResidentAdapter.calls.join(',') === 'handshake,state',
  'only a complete remote F5 snapshot may restore ready without duplicate Open/OpenEvents');
  await completeResident.close();

  // A failed initial enable arms the resident guard for observation, but the
  // timer must not become a second HC recovery owner. Only the application
  // power transaction may call the explicit resume path.
  const guardRetryAdapter = new FakeAdapter();
  guardRetryAdapter.handshakeFailures = 3;
  const guardRetryLifecycle = newFanLifecycle({
    enabled: true,
    adapter: guardRetryAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
    fanGuardIntervalMs: 5,
  });
  const guardRetryCurve = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }];
  let guardInitialFailure = false;
  try { await guardRetryLifecycle.apply(guardRetryCurve); } catch { guardInitialFailure = true; }
  assert(guardInitialFailure, 'the first guarded startup should expose its immediate failure to the caller');
  const guardRetryInitialCalls = guardRetryAdapter.calls.length;
  await new Promise<void>((resolve) => setTimeout(resolve, 100));
  assert(guardRetryLifecycle.state !== 'ready',
    `the observation-only guard must not recover a failed startup (state=${guardRetryLifecycle.state}, calls=${guardRetryAdapter.calls.join(',')})`);
  assert(guardRetryAdapter.calls.slice(guardRetryInitialCalls).every((call) => call === 'state') &&
    !guardRetryAdapter.calls.includes('enable') && !guardRetryAdapter.calls.includes('resume'),
    `the ten-second guard must observe only and never start, resume, or write HC (calls=${guardRetryAdapter.calls.join(',')})`);
  await guardRetryLifecycle.disable();
  const callsAfterGuardDisable = guardRetryAdapter.calls.length;
  await new Promise<void>((resolve) => setTimeout(resolve, 15));
  assert(guardRetryAdapter.calls.length === callsAfterGuardDisable, 'manual disable must disarm the resident fan guard');
  await guardRetryLifecycle.close();

  // The explicit coordinator resume may perform its HC recovery attempt, but
  // the resident guard must not turn an Enable failure into automatic writes.
  const guardGiveUpAdapter = new FakeAdapter();
  const guardGiveUpLifecycle = newFanLifecycle({
    enabled: true,
    adapter: guardGiveUpAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
    fanGuardIntervalMs: 5,
  });
  await guardGiveUpLifecycle.start();
  await guardGiveUpLifecycle.apply(guardRetryCurve);
  guardGiveUpLifecycle.setPowerGeneration(1);
  await guardGiveUpLifecycle.suspend();
  guardGiveUpLifecycle.observePowerBoundary('resuming', 1);
  guardGiveUpLifecycle.observePowerBoundary('resume-ready', 1);
  guardGiveUpAdapter.enableFailures = 5;
  let firstGuardEnableRejected = false;
  try { await guardGiveUpLifecycle.resume(); } catch { firstGuardEnableRejected = true; }
  const explicitResumeEnableCount = guardGiveUpAdapter.calls.filter((call) => call === 'enable').length;
  await new Promise<void>((resolve) => setTimeout(resolve, 100));
  const guardedEnableCount = guardGiveUpAdapter.calls.filter((call) => call === 'enable').length;
  await new Promise<void>((resolve) => setTimeout(resolve, 25));
  assert(firstGuardEnableRejected && guardedEnableCount === explicitResumeEnableCount &&
    guardGiveUpAdapter.calls.filter((call) => call === 'enable').length === guardedEnableCount,
    `the observation-only guard must not retry failed Enable automatically (count=${guardGiveUpAdapter.calls.filter((call) => call === 'enable').length}, calls=${guardGiveUpAdapter.calls.join(',')})`);
  await guardGiveUpLifecycle.close();
  await lifecycle.close();
  assert(lifecycle.state === 'stopped', 'close must stop a resumed host');
  assert(adapter.calls.slice(-2).join(',') === 'close,shutdown', 'close must confirm then request Host shutdown');
  assert(launcher.calls.join(',') === 'start,stop', 'host process must be stopped exactly once');

  // HC Window_Closed does not require a separate Hardware-profile callback.
  // A ROG Close can therefore return with no generic OEM acknowledgement;
  // the complete virtual Close + DeviceManager Stop boundary must still let
  // the parent process shut down without posting a second close request.
  const directCloseAdapter = new FakeAdapter();
  directCloseAdapter.directHcCloseWithoutHardwareCallback = true;
  const directCloseLauncher = new FakeLauncher();
  const directClose = newFanLifecycle({ enabled: true, adapter: directCloseAdapter, launcher: directCloseLauncher, heartbeatIntervalMs: 0 });
  await directClose.start();
  await directClose.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  await directClose.close();
  assert(directClose.state === 'stopped' && directCloseAdapter.calls.slice(-2).join(',') === 'close,shutdown' &&
    directCloseAdapter.calls.filter((call) => call === 'close').length === 1 &&
    directCloseLauncher.calls.join(',') === 'start,stop',
  'a complete HC Window_Closed boundary without a Hardware callback must stop once, not fault-lock or re-close');

  // A curve remembered before sleep is never permission to write after the
  // next handshake downgrades the route. The guard reports this attempt as a
  // failure, keeps its timer armed, and never writes through the downgraded
  // route.
  const resumeReadOnlyAdapter = new FakeAdapter();
  const resumeReadOnly = newFanLifecycle({ enabled: true, adapter: resumeReadOnlyAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await resumeReadOnly.start();
  await resumeReadOnly.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  resumeReadOnly.setPowerGeneration(1);
  await resumeReadOnly.suspend();
  resumeReadOnly.observePowerBoundary('resuming', 1);
  resumeReadOnly.observePowerBoundary('resume-ready', 1);
  // 920 §8.0-D B5 remap (finding F-1, characterised in VALIDATION §4.1):
  // the old lever overrode handshakeResult.fanRouteWriteReady, which only worked because the old resume
  // re-handshaked and re-latched writeReady. The event-driven resume no longer re-handshakes
  // (fanHost.ts L1814-1842), so writeReady keeps its startup value (L1193/L1461) and the write-readiness
  // authority is now the Host, surfacing here as a rejected enable.
  // Intent preserved: a route that CANNOT write must still reject after the bounded retry, must not
  // reopen HC, and the UI must drop control.
  // 920-v1.2 W1: resume() now shares the single bounded retry (mutateWithBoundedRetry). One transient
  // enable failure therefore auto-recovers (that is the intended T0-R behaviour); a *persistent* refusal
  // is modelled by enableFailures=2 so both bounded attempts fail and the rejection outcome holds.
  resumeReadOnlyAdapter.enableFailures = 2;
  let resumeReadOnlyRejected = false;
  try { await resumeReadOnly.resume(); } catch { resumeReadOnlyRejected = true; }
  const postResumeReadOnlyCalls = resumeReadOnlyAdapter.calls.slice(5);
  const fanViewSource = readFileSync('src/views/FanView.vue', 'utf8');
  assert(resumeReadOnlyRejected &&
    postResumeReadOnlyCalls.filter((call) => call === 'enable').length === 2 &&
    !postResumeReadOnlyCalls.includes('open') &&
    fanViewSource.includes('controlActive.value = false;') &&
    fanViewSource.includes('setFanControlActive(false);') &&
    fanViewSource.includes('stopTelemetry();'),
  `a resume that cannot write must be rejected after the bounded retry, must not reopen HC, and the UI must drop control (rejected=${resumeReadOnlyRejected}, state=${resumeReadOnly.state}, calls=${postResumeReadOnlyCalls.join(',')})`);
  await resumeReadOnly.close();

  // The normal application-exit path starts from active software control.
  // HC Window_Closed delegates the ownership handoff to CurrentDevice.Close,
  // then the authenticated Host may shut down.
  const directExitAdapter = new FakeAdapter();
  directExitAdapter.handshakeResult = {
    ...directExitAdapter.handshakeResult,
    deviceClass: 'HandheldCompanion.Devices.XboxROGAllyX',
    deviceIdentity: { manufacturer: 'ASUSTEK COMPUTER INC.', model: 'ROG Xbox Ally X RC73XA_RC73XA', product: 'RC73XA' },
  };
  const directExitLauncher = new FakeLauncher();
  const directExit = newFanLifecycle({ enabled: true, adapter: directExitAdapter, launcher: directExitLauncher, heartbeatIntervalMs: 0 });
  await directExit.start();
  await directExit.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 42, dutyPercent: 28 }, { tempC: 70, dutyPercent: 58 }, { tempC: 100, dutyPercent: 100 }]);
  await directExit.close();
  assert(directExit.state === 'stopped', 'active-control application exit must reach stopped');
  assert(directExitAdapter.calls.join(',') === 'handshake,open,open-events,acquire,enable,close,shutdown',
    'active-control exit must call the Host HC Close boundary once before shutdown');
  assert(directExitLauncher.calls.join(',') === 'start,stop',
    'active-control exit must stop its Host once after confirmed restore');

  // Power notifications and a UI click may arrive almost together. The
  // lifecycle queue must finish the already-admitted HC curve request, then
  // follow HC SystemPending's one Close boundary. There must be no write
  // after the suspend transaction starts.
  const enableThenSleepAdapter = new FakeAdapter();
  const enableThenSleep = newFanLifecycle({ enabled: true, adapter: enableThenSleepAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await enableThenSleep.start();
  const enableBeforeSleep = enableThenSleep.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  enableThenSleep.setPowerGeneration(1);
  const sleepAfterEnable = enableThenSleep.suspend();
  await Promise.all([enableBeforeSleep, sleepAfterEnable]);
  // 920 §8.0-D B5 remap: suspend() no longer sends adapter.suspend (E9 zero-message suspend,
  // fanHost.ts L1800-1810); the single HC Close owner is the Host/native F4 path.
  assert(enableThenSleep.state === 'suspended' &&
    enableThenSleepAdapter.calls.join(',') === 'handshake,open,open-events,acquire,enable',
  'enable followed immediately by sleep must serialize into one HC Close boundary owned by the Host');
  await enableThenSleep.close();
  assert(enableThenSleepAdapter.calls.slice(-2).join(',') === 'close,shutdown',
    'closing a suspended Host must still require authenticated Host close/shutdown');

  // The opposite ordering is more important: after sleep has been admitted,
  // an already queued UI write must not open HC or acquire a lease.
  const sleepThenEnableAdapter = new FakeAdapter();
  const sleepThenEnable = newFanLifecycle({ enabled: true, adapter: sleepThenEnableAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await sleepThenEnable.start();
  sleepThenEnable.setPowerGeneration(1);
  const sleepBeforeEnable = sleepThenEnable.suspend();
  const enableAfterSleep = sleepThenEnable.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  await sleepBeforeEnable;
  let enableAfterSleepRejected = false;
  try { await enableAfterSleep; } catch { enableAfterSleepRejected = true; }
  // 920 §8.0-D B5 remap: suspend() sends no adapter message now, so the sequence is handshake-only.
  // R2/G7（FAN-926R §4.1/§3.2）**因本裁决改要求**：睡眠窗内允许**只读** `/api/state` 准入探测
  // （这正是"只读等待"的定义），因此 `calls` 不再恒等于 handshake。断言意图不变且更强的部分
  // 保留：睡眠已准入后，排队中的 UI 写入**不得** Open HC、不得 acquire 租约、不得写曲线。
  assert(enableAfterSleepRejected && sleepThenEnable.state === 'suspended' &&
    sleepThenEnableAdapter.calls.filter((call) => call === 'open' || call === 'open-events').length === 0 &&
    sleepThenEnableAdapter.calls.filter((call) => call === 'acquire' || call === 'enable').length === 0 &&
    sleepThenEnableAdapter.stateTimeouts.every((timeout) => timeout === undefined || timeout <= 500),
  `sleep admitted before enable must block HC Open, lease acquisition and curve writes (calls=${sleepThenEnableAdapter.calls.join(',')})`);
  await sleepThenEnable.close();

  // Application shutdown cannot leapfrog an active write either. It is
  // queued behind it and then performs the HC Close boundary once.
  const enableThenCloseAdapter = new FakeAdapter();
  const enableThenCloseLauncher = new FakeLauncher();
  const enableThenClose = newFanLifecycle({ enabled: true, adapter: enableThenCloseAdapter, launcher: enableThenCloseLauncher, heartbeatIntervalMs: 0 });
  await enableThenClose.start();
  const enableBeforeClose = enableThenClose.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  const closeAfterEnable = enableThenClose.close();
  await Promise.all([enableBeforeClose, closeAfterEnable]);
  assert(enableThenClose.state === 'stopped' &&
    enableThenCloseAdapter.calls.join(',') === 'handshake,open,open-events,acquire,enable,close,shutdown' &&
    enableThenCloseLauncher.calls.join(',') === 'start,stop',
  'enable followed immediately by exit must use one HC Close before stopping the Host');

  // A Host may still be unwinding its HC virtual Close. The frontend must
  // observe the pending marker and wait for remote completion; it must never
  // issue a second close, shutdown, or terminate on that first response.
  const pendingCloseAdapter = new FakeAdapter();
  const pendingCloseLauncher = new FakeLauncher();
  pendingCloseAdapter.closePendingOnce = true;
  const pendingClose = newFanLifecycle({ enabled: true, adapter: pendingCloseAdapter, launcher: pendingCloseLauncher, heartbeatIntervalMs: 0 });
  await pendingClose.start();
  await pendingClose.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  await pendingClose.close();
  assert(pendingClose.state === 'stopped' && pendingCloseLauncher.calls.join(',') === 'start,stop' &&
    pendingCloseAdapter.calls.includes('state') && pendingCloseAdapter.calls.filter((call) => call === 'close').length === 1,
  'HC_CLOSE_PENDING must wait for remote cleanup without issuing a second close before shutdown/launcher stop');

  // A suspend transport failure must retry restore/recovery and then retry
  // the suspend endpoint; it must not leave the curve active across sleep.
  // 920 §8.0-D B5 remap: suspend() no longer sends adapter.suspend (E9 zero-message suspend,
  // fanHost.ts L1800-1810), so the old "retry the suspend endpoint" premise is obsolete.
  // Intent preserved: suspend must reach the suspended state and must not leave the curve active.
  const suspendRetryAdapter = new FakeAdapter();
  suspendRetryAdapter.suspendFailures = 1;
  const suspendRetry = newFanLifecycle({ enabled: true, adapter: suspendRetryAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await suspendRetry.start();
  await suspendRetry.applyPreset('balanced');
  suspendRetry.setPowerGeneration(1);
  await suspendRetry.suspend();
  assert(suspendRetry.state === 'suspended' && suspendRetryAdapter.calls.filter((call) => call === 'suspend').length === 0,
    `suspend must reach the suspended state without sending any adapter message; the Host owns the HC Close (calls=${suspendRetryAdapter.calls.join(',')})`);
  suspendRetry.observePowerBoundary('resuming', 1);
  suspendRetry.observePowerBoundary('resume-ready', 1);
  await suspendRetry.resume();
  await suspendRetry.close();

  // A transient HC write failure may retry once after OEM restore. This is
  // distinct from a known external controller conflict below.
  const retryAdapter = new FakeAdapter();
  retryAdapter.enableFailures = 1;
  const retryLauncher = new FakeLauncher();
  const retry = newFanLifecycle({ enabled: true, adapter: retryAdapter, launcher: retryLauncher, heartbeatIntervalMs: 0 });
  await retry.start();
  await retry.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(retry.state === 'ready' && retryAdapter.calls.filter((call) => call === 'enable').length === 2,
    'transient write failure must restore and retry the same curve once');
  await retry.applyPreset('balanced');
  assert(retry.state === 'ready', 'control must remain usable after transient recovery');
  await retry.close();

  // A real Host closes its HC device session when Enable fails after touching
  // the route. The frontend must adopt the returned open/open-events=false
  // flags and recreate the exact HC Open -> OpenEvents pair on retry.
  const remoteCloseAdapter = new FakeAdapter();
  remoteCloseAdapter.remoteTelemetry = true;
  remoteCloseAdapter.closeSessionOnEnableFailure = true;
  remoteCloseAdapter.enableFailures = 1;
  const remoteClose = newFanLifecycle({ enabled: true, adapter: remoteCloseAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await remoteClose.start();
  await remoteClose.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  assert(remoteClose.state === 'ready' && remoteCloseAdapter.calls.filter((call) => call === 'open').length === 2 &&
    remoteCloseAdapter.calls.filter((call) => call === 'open-events').length === 2,
  'a Host-side failed Enable that closed HC must be reopened before the next curve write');
  await remoteClose.close();

  // HC may complete the virtual Close + DeviceManager.Stop boundary while a
  // failed write is being reported, without a generic Hardware callback. That
  // lifecycle evidence is enough for observer-only recovery; historical
  // HardwareWritesObserved must not lock the next explicit retry.
  const closedBoundaryAdapter = new FakeAdapter();
  closedBoundaryAdapter.remoteTelemetry = true;
  closedBoundaryAdapter.closeSessionOnEnableFailure = true;
  closedBoundaryAdapter.closeBoundaryWithoutHardwareCallbackOnEnableFailure = true;
  closedBoundaryAdapter.enableFailures = 1;
  const closedBoundary = newFanLifecycle({ enabled: true, adapter: closedBoundaryAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await closedBoundary.start();
  await closedBoundary.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  assert(closedBoundary.state === 'ready' && closedBoundaryAdapter.calls.filter((call) => call === 'open').length === 2 &&
    closedBoundaryAdapter.calls.filter((call) => call === 'open-events').length === 2 &&
    !closedBoundaryAdapter.calls.includes('close'),
  'a completed HC Close without Hardware callback must be accepted by recovery without a duplicate Close');
  await closedBoundary.close();

  // A known route conflict is a third-party/OEM controller ownership signal,
  // not a reason to retry and race it.
  // 920 §8.0-D B5 remap: the old assertion required `state === 'conflict-locked'`
  // plus a frontend `restore,release` on the failed write. Both are E9 deletions
  // (`'conflict-locked'` has no assignment anywhere in fanHost.ts; the frontend
  // auto-recovery was replaced by "keep current state + throw", fanHost.ts L1574-1577).
  // Intent preserved: a conflict must never be retried or raced (exactly one write
  // attempt), the resident Host must stay reachable, and OEM handback must happen on
  // the explicit path before the Host is closed.
  const initialConflictAdapter = new FakeAdapter();
  initialConflictAdapter.enableFailures = 1;
  initialConflictAdapter.enableFailureMessage = 'FAN_ROUTE_CONFLICT: external fan controller owns the route';
  const initialConflictLauncher = new FakeLauncher();
  const initialConflict = newFanLifecycle({ enabled: true, adapter: initialConflictAdapter, launcher: initialConflictLauncher, heartbeatIntervalMs: 0 });
  await initialConflict.start();
  let initialConflictRejected = false;
  try {
    await initialConflict.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  } catch { initialConflictRejected = true; }
  assert(initialConflictRejected && initialConflictAdapter.calls.filter((call) => call === 'enable').length === 1 &&
    !initialConflictAdapter.calls.includes('restore') && !initialConflictAdapter.calls.includes('release'),
  'known external conflict must fail the first write without retrying or a frontend auto-restore');
  await initialConflict.disable();
  assert(initialConflict.state === 'awaiting-control' &&
    initialConflictAdapter.calls.slice(-3).join(',') === 'restore,release,state',
  'explicit disable after a conflict must restore OEM and release the lease before returning to awaiting-control');
  await initialConflict.close();
  assert(initialConflict.state === 'stopped' && initialConflictAdapter.calls.slice(-2).join(',') === 'close,shutdown' &&
    initialConflictLauncher.calls.join(',') === 'start,stop',
  'conflict teardown must close the resident Host only after OEM restore confirmation');

  // Simulate another controller taking over after YeMan already enabled a
  // curve. The failed heartbeat is the observable boundary.
  // 920 §8.0-D B5 remap: the old assertion required `conflict-locked` plus a
  // frontend `heartbeat,restore,release`. Both are E9 deletions. The surviving
  // mechanism is 批85 F7-a (2026-09-12 user-authorised, fanHost.ts L1747-1775):
  // one lease self-heal — reacquire + rewrite the active curve, never a loop and
  // never a lock. Intent preserved: a failed heartbeat must not silently drop
  // ownership (the curve stays written) and must not touch OEM/Close.
  const midTakeoverAdapter = new FakeAdapter();
  const midTakeoverLauncher = new FakeLauncher();
  const midTakeover = newFanLifecycle({ enabled: true, adapter: midTakeoverAdapter, launcher: midTakeoverLauncher, heartbeatIntervalMs: 0 });
  await midTakeover.start();
  await midTakeover.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  midTakeoverAdapter.heartbeatFailures = 1;
  midTakeoverAdapter.heartbeatFailureMessage = 'FAN_ROUTE_CONFLICT: OEM reclaimed fan control';
  let midTakeoverRejected = false;
  try { await midTakeover.heartbeat(); } catch { midTakeoverRejected = true; }
  // 按顺序断言而不是按尾部切片：F-3 修复后自愈路径会先做一次权威快照读取
  // （heartbeat → state → acquire → enable），切片会随实现细节抖动。
  const midCalls = midTakeoverAdapter.calls;
  const midHeartbeatAt = midCalls.indexOf('heartbeat');
  const midAcquireAt = midCalls.indexOf('acquire', midHeartbeatAt + 1);
  const midEnableAt = midCalls.indexOf('enable', midAcquireAt + 1);
  assert(midTakeoverRejected === false && midTakeover.state === 'ready' &&
    midTakeover.currentLease?.leaseId === 'lease-1' &&
    midHeartbeatAt >= 0 && midAcquireAt > midHeartbeatAt && midEnableAt > midAcquireAt,
  'a failed lease heartbeat must self-heal by reacquiring and rewriting the active curve');
  assert(!midTakeoverAdapter.calls.includes('restore') && !midTakeoverAdapter.calls.includes('release') &&
    !midTakeoverAdapter.calls.includes('close'),
  'lease self-heal must not restore/release/close the resident HC session');
  await midTakeover.close();
  assert(midTakeover.state === 'stopped' && midTakeoverAdapter.calls.slice(-2).join(',') === 'close,shutdown' &&
    midTakeoverLauncher.calls.join(',') === 'start,stop',
  'mid-session takeover close must complete a confirmed Host shutdown');

  // HID removal is owned by the resident Host's one Close/unbind boundary.
  // Once that boundary is observable, the bridge may probe a stable route and
  // replay the acknowledged curve through the canonical Open -> OpenEvents ->
  // lease -> curve order. It must not issue a second restore/release/Close.
  // 920 §8.0-D B5：**本场景保留原断言，不得改写**。临时探针实测（2026-09-20）
  // 现行增量只有 `heartbeat,acquire,enable`——Host 已把会话关到 AwaitingControl
  // （openCalled=false）而前端 `opened` 标志未回灌，重写直接落在已关闭会话上。
  // 这与 F-3 同源（syncRemoteSessionState 只在成功响应路径调用），是**真实缺口**，
  // 改写它等于把缺口写成期望行为。详见 VALIDATION §4.4 / §4.5。
  const hidRemovalAdapter = new FakeAdapter();
  const hidRemoval = newFanLifecycle({ enabled: true, adapter: hidRemovalAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await hidRemoval.start();
  const hidCurve = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }];
  await hidRemoval.apply(hidCurve);
  hidRemovalAdapter.routeLossClosesHost = true;
  hidRemovalAdapter.heartbeatFailures = 1;
  hidRemovalAdapter.heartbeatFailureMessage = 'HC_SESSION_UNAVAILABLE: hc-device-is-open-false';
  const beforeHidRecovery = hidRemovalAdapter.calls.length;
  const recoveredLease = await hidRemoval.heartbeat();
  const hidRecoveryCalls = hidRemovalAdapter.calls.slice(beforeHidRecovery);
  const openAt = hidRecoveryCalls.indexOf('open');
  const eventsAt = hidRecoveryCalls.indexOf('open-events');
  const acquireAt = hidRecoveryCalls.indexOf('acquire');
  const enableAt = hidRecoveryCalls.indexOf('enable');
  assert(hidRemoval.state === 'ready' && recoveredLease.leaseId === 'lease-1' && hidRemoval.currentLease?.leaseId === 'lease-1' &&
    openAt >= 0 && eventsAt > openAt && acquireAt > eventsAt && enableAt > acquireAt &&
    !hidRecoveryCalls.includes('restore') && !hidRecoveryCalls.includes('release') && !hidRecoveryCalls.includes('close'),
  `HID route loss did not preserve the single Close owner/rebuild order: ${hidRecoveryCalls.join(',')}`);
  await hidRemoval.close();

  // Open() can touch EC and still reject.
  // 920 §8.0-D B5 remap（本轮，F-3 修复后）：旧断言要求**前端**在自动重试前先
  // restore OEM。读真实 Host 后确认该责任已不在前端——Program.cs L6146-6162：
  // Open 失败时若 `IsOpen || OpenAttempted` 则 Host 自己走
  // RecoverAfterHardwareFailure（RestoreHardware(close:true) → 清 OpenCalled/
  // OpenEventsCalled），否则 Host 自己清两个会话标志并把状态置 AwaitingControl。
  // 前端的职责因此是：采纳 Host 报告的“会话已关闭”，让那次重试**重新建立**
  // Open/OpenEvents，而不是写进半开会话。这里同时打开替身的会话遥测
  // （真实 /api/state 始终返回这两个字段）。
  const openFailureAdapter = new FakeAdapter();
  openFailureAdapter.remoteTelemetry = true;
  openFailureAdapter.openFailures = 1;
  const openFailure = newFanLifecycle({ enabled: true, adapter: openFailureAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await openFailure.start();
  await openFailure.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(openFailure.state === 'ready' &&
    openFailureAdapter.calls.filter((call) => call === 'open').length === 2 &&
    openFailureAdapter.calls.filter((call) => call === 'open-events').length === 1 &&
    !openFailureAdapter.calls.includes('restore'),
  'a partial Open failure must make the retry re-establish Open/OpenEvents instead of writing into a half-open session');
  await openFailure.applyPreset('soft');
  await openFailure.close();

  // OpenEvents is a separate HC boundary; its failure must not leave a
  // half-subscribed session behind.
  // 920 §8.0-D B5 remap: the old assertion required a frontend `restore` before
  // the retry (deleted in E9). The surviving mechanism is to retry the
  // subscription itself — `this.eventsOpened = true` is assigned *after* the
  // await (fanHost.ts L1623-1627), so a failed OpenEvents leaves no false
  // "subscribed" flag and the retry re-issues it on the same Open session.
  // Probe (2026-09-20): open=1, open-events=2, no restore, no close, state=ready.
  const eventsFailureAdapter = new FakeAdapter();
  eventsFailureAdapter.openEventsFailures = 1;
  const eventsFailure = newFanLifecycle({ enabled: true, adapter: eventsFailureAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await eventsFailure.start();
  await eventsFailure.applyPreset('balanced');
  assert(eventsFailure.state === 'ready' &&
    eventsFailureAdapter.calls.filter((call) => call === 'open').length === 1 &&
    eventsFailureAdapter.calls.filter((call) => call === 'open-events').length === 2 &&
    !eventsFailureAdapter.calls.includes('restore') && !eventsFailureAdapter.calls.includes('close'),
  'an OpenEvents failure must retry the subscription on the same Open session without a duplicate Open or OEM restore');
  await eventsFailure.applyPreset('balanced');
  await eventsFailure.close();

  // 920 §8.0-D B5：**本场景保留原断言，不得改写**（F-3 同类）。探针实测
  // （2026-09-20）：open=1、open-events=2——Host 在 OpenEvents 失败时已关闭
  // HC 会话，前端 `opened` 未回灌 ⇒ 重试没有重建 Open/OpenEvents 对。
  // 详见 VALIDATION §4.4 / §4.5。
  const remoteEventsCloseAdapter = new FakeAdapter();
  remoteEventsCloseAdapter.remoteTelemetry = true;
  remoteEventsCloseAdapter.closeSessionOnOpenEventsFailure = true;
  remoteEventsCloseAdapter.openEventsFailures = 1;
  const remoteEventsClose = newFanLifecycle({ enabled: true, adapter: remoteEventsCloseAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await remoteEventsClose.start();
  await remoteEventsClose.applyPreset('balanced');
  assert(remoteEventsClose.state === 'ready' && remoteEventsCloseAdapter.calls.filter((call) => call === 'open').length === 2 &&
    remoteEventsCloseAdapter.calls.filter((call) => call === 'open-events').length === 2,
  'a Host-side failed OpenEvents that closed HC must be reopened before retry');
  await remoteEventsClose.close();

  // Startup handshake is retried at most once, but never skips the Gate or
  // opens hardware during the transient failure window.
  // 920 §8.0-D B5 remap: the old assertion demanded three bounded attempts
  // (deleted in E9). The surviving mechanism (fanHost.ts startInternal) is a
  // single token-rebind retry, and only for adapters that expose
  // setSessionToken; otherwise the failure is surfaced immediately.
  // Probe (2026-09-20): plain adapter → 1 handshake, start() rejects.
  const handshakeRetryAdapter = new FakeAdapter();
  handshakeRetryAdapter.handshakeFailures = 2;
  const handshakeRetryLauncher = new FakeLauncher();
  const handshakeRetry = newFanLifecycle({ enabled: true, adapter: handshakeRetryAdapter, launcher: handshakeRetryLauncher, heartbeatIntervalMs: 0 });
  let handshakeRetryRejected = false;
  try { await handshakeRetry.start(); } catch { handshakeRetryRejected = true; }
  assert(handshakeRetryRejected && handshakeRetryAdapter.calls.filter((call) => call === 'handshake').length === 1 &&
    !handshakeRetryAdapter.calls.includes('open') && !handshakeRetryAdapter.calls.includes('open-events'),
  'a startup handshake failure without token rebind must fail fast without opening hardware');
  await handshakeRetry.close();

  const rebindAdapter = new TokenRebindAdapter();
  rebindAdapter.handshakeFailures = 1;
  const rebind = newFanLifecycle({ enabled: true, adapter: rebindAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  const rebindGate = await rebind.start();
  assert(rebindGate.allowed && rebindAdapter.calls.filter((call) => call === 'handshake').length === 2 &&
    rebindAdapter.tokenRebinds.length >= 1 && !rebindAdapter.calls.includes('open'),
  'a token-capable adapter may retry the startup handshake exactly once and must never open hardware during the transient window');
  await rebind.close();

  // A transient write failure must be retried on the resident Host.
  // 920 §8.0-D B5 remap: the old assertion required the frontend to stop and
  // restart the Host process (`start,stop,start`) via an independent close
  // fallback after restore/release failed. That fallback was deleted in E9
  // (fanHost.ts L1876-1877: keep current state + throw; no
  // waitForHostRecovery/finalizeConfirmedClose). Intent preserved: exactly one
  // bounded retry, on the resident Host, with no OEM restore.
  // Probe (2026-09-20): launcher stays `start`, enable ×2, no restore.
  const fallbackAdapter = new FakeAdapter();
  fallbackAdapter.enableFailures = 1;
  // Retained deliberately: a Host whose restore always fails must never be asked
  // to restore on this path.
  fallbackAdapter.restoreFailures = 3;
  const fallbackLauncher = new FakeLauncher();
  const fallback = newFanLifecycle({ enabled: true, adapter: fallbackAdapter, launcher: fallbackLauncher, heartbeatIntervalMs: 0 });
  await fallback.start();
  await fallback.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(fallback.state === 'ready' && fallbackLauncher.calls.join(',') === 'start' &&
    fallbackAdapter.calls.filter((call) => call === 'enable').length === 2 &&
    !fallbackAdapter.calls.includes('restore'),
  'a transient write failure must retry once on the resident Host without restarting it or restoring OEM');
  await fallback.close();
  assert(fallbackLauncher.calls.join(',') === 'start,stop', 'the resident Host must be stopped exactly once by close');

  // Disable itself is already a restore boundary. If that one HC profile call
  // fails, the failure must be surfaced without a duplicate restore and without
  // an automatic Close.
  // 920 §8.0-D B5 remap: the old assertion required the frontend to poll Host
  // recovery and adopt an `AwaitingControl` snapshot (deleted in E9, fanHost.ts
  // L1712-1714: keep current state + throw; no waitForHostRecovery/adoptRecoveredHost).
  // Intent preserved: exactly one restore attempt, no duplicate restore, no
  // automatic Close, and the resident Host must still be closable afterwards.
  // Probe (2026-09-20): disable throws, restore ×1, no close, state unchanged.
  const disableRecoveryAdapter = new FakeAdapter();
  disableRecoveryAdapter.restoreFailures = 1;
  disableRecoveryAdapter.recoveryStateAfterRestoreFailure = 'AwaitingControl';
  const disableRecovery = newFanLifecycle({ enabled: true, adapter: disableRecoveryAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await disableRecovery.start();
  await disableRecovery.applyPreset('balanced');
  let disableRecoveryRejected = false;
  try { await disableRecovery.disable(); } catch { disableRecoveryRejected = true; }
  assert(disableRecoveryRejected &&
    disableRecoveryAdapter.calls.filter((call) => call === 'restore').length === 1 &&
    !disableRecoveryAdapter.calls.includes('close'),
  'a failed disable restore must surface the error with no duplicate restore and no automatic Close');
  await disableRecovery.close();
  assert(disableRecovery.state === 'stopped' && disableRecoveryAdapter.calls.slice(-2).join(',') === 'close,shutdown',
  'the resident Host must still reach a confirmed close after a failed disable restore');

  // A lost/failed close response must never cause a second frontend Close.
  // The resident Host owns recovery and the frontend may only observe its
  // already-confirmed stopped state before requesting shutdown.
  // 920 §8.0-D B5：**本场景保留原断言，不得改写**。探针实测（2026-09-20）：
  // close 失败后 `closeHostAfterRestore()` 直接抛错（`handshake,close`，无 state
  // 观察、无 shutdown），生命周期停在 awaiting-control；随后 applyPreset 又被
  // 协调器以 `native-resume-ready-not-observed` 拒绝。这正是本场景注释所描述的
  // 期望行为缺失 ⇒ 与 **F-2** 同源的真实缺口（close 路径缺少等待/观察/恢复）。
  // 详见 VALIDATION §4.3 / §4.5。
  const closeRetryAdapter = new FakeAdapter();
  closeRetryAdapter.closeFailures = 1;
  closeRetryAdapter.recoveryStateAfterCloseFailure = 'Stopped';
  const closeRetryLauncher = new FakeLauncher();
  const closeRetry = newFanLifecycle({ enabled: true, adapter: closeRetryAdapter, launcher: closeRetryLauncher, heartbeatIntervalMs: 0 });
  await closeRetry.start();
  await closeRetry.close();
  assert(closeRetry.state === 'stopped', 'Host-owned close recovery must reach stopped');
  assert(closeRetryAdapter.calls.filter((call) => call === 'close').length === 1 &&
    closeRetryAdapter.calls.slice(-3).join(',') === 'close,state,shutdown',
  'close must issue one request, then only observe Host recovery before shutdown');

  // Repeated open after a stopped Host must transparently start/handshake
  // again instead of sending Open() to a dead process.
  await closeRetry.applyPreset('soft');
  assert(closeRetry.state === 'ready' && closeRetryLauncher.calls.join(',') === 'start,stop,start',
    'a stopped Host must be restarted before the next control request');
  await closeRetry.close();

  const presetAdapter = new FakeAdapter();
  const presetLauncher = new FakeLauncher();
  const presetLifecycle = newFanLifecycle({
    enabled: true,
    adapter: presetAdapter,
    launcher: presetLauncher,
    heartbeatIntervalMs: 0,
  });
  await presetLifecycle.start();
  const customPresetNodes = [{ tempC: 0, dutyPercent: 0 }, { tempC: 42, dutyPercent: 22 }, { tempC: 70, dutyPercent: 55 }, { tempC: 100, dutyPercent: 100 }];
  await presetLifecycle.applyPreset('balanced', customPresetNodes);
  assert(presetAdapter.calls.slice(0, 5).join(',') === 'handshake,open,open-events,acquire,preset',
    'preset path must open -> events -> acquire -> preset');
  assert(presetAdapter.lastPresetNodes?.[1]?.tempC === 42 && presetAdapter.lastPresetNodes?.[2]?.dutyPercent === 55,
    'preset path must pass the UI-owned curve nodes to the adapter');
  await presetLifecycle.disable();
  assert(presetLifecycle.state === 'awaiting-control', 'disable must return to awaiting-control');
  assert(presetAdapter.calls.slice(5).join(',') === 'restore,release,state',
    'disable must restore OEM -> release lease before state readback');
  await presetLifecycle.close();
  assert(presetAdapter.calls.slice(-2).join(',') === 'close,shutdown',
    'close must confirm then request Host shutdown');
  assert(presetLauncher.calls.join(',') === 'start,stop', 'preset lifecycle must stop its Host exactly once');

  // Regression for the real ROG lease interruption: the first apply starts in
  // awaiting-control, so its lease renewal must be armed only after the
  // write succeeds. Otherwise the Host expires the lease after 15 seconds
  // and safely returns to OEM despite the UI still showing control enabled.
  const initialHeartbeatAdapter = new FakeAdapter();
  const initialHeartbeatLifecycle = newFanLifecycle({
    enabled: true,
    adapter: initialHeartbeatAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 5,
  });
  await initialHeartbeatLifecycle.start();
  await initialHeartbeatLifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  await new Promise<void>((resolve) => setTimeout(resolve, 30));
  assert(initialHeartbeatAdapter.calls.includes('heartbeat'),
    'the initial fan apply must arm lease renewal before the Host expiry window');
  await initialHeartbeatLifecycle.close();

  // FAN-927 §4（2026-09-27 用户批准）：**明确修订** 2026-09-12 第七十七批 C-②"每次 mutation
  // 前 heartbeat 预检"。当前实例/代际内经 acquire/adopt 确认的 lease 直接携带写入——普通
  // apply/preset 不再额外花一个往返；Host 的 `Enable → EnsureLeaseFromBody` 写入点授权仍是
  // 权威，周期续租保留。旧断言 `heartbeat-invalid,acquire,preset` 记录的是被修订掉的那次预检。
  const expiringAdapter = new ExpiringLeaseAdapter();
  const expiringLifecycle = newFanLifecycle({
    enabled: true,
    adapter: expiringAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 60_000,
  });
  await expiringLifecycle.start();
  await expiringLifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  const afterApplyCalls = expiringAdapter.calls.length;
  await expiringLifecycle.applyPreset('balanced');
  assert(expiringAdapter.calls.slice(afterApplyCalls).join(',') === 'preset',
    `同代有效 lease 的一次调节必须只有 1 次写请求、0 次预检 heartbeat（calls=${expiringAdapter.calls.slice(afterApplyCalls).join(',')}）`);
  // 睡眠必已作废租约 ⇒ 同步丢弃旧 token；唤醒直接 fresh acquire 并重写曲线，
  // 绝不先用明知失效的旧 token 去换一次 LEASE_INVALID。
  expiringLifecycle.setPowerGeneration(1);
  await expiringLifecycle.suspend();
  expiringLifecycle.observePowerBoundary('resuming', 1);
  expiringLifecycle.observePowerBoundary('resume-ready', 1);
  const beforeResumeCalls = expiringAdapter.calls.length;
  await expiringLifecycle.resume();
  const resumeCalls = expiringAdapter.calls.slice(beforeResumeCalls).join(',');
  assert(!resumeCalls.includes('heartbeat-invalid') && resumeCalls.includes('acquire') && resumeCalls.includes('enable'),
    `睡眠后必须 0 次旧 token 探测、1 次新申请并重写曲线（calls=${resumeCalls}）`);
  await expiringLifecycle.close();

  const rejectedAdapter = new FakeAdapter();
  rejectedAdapter.handshakeResult = {
    ok: true,
    supported: true,
    deviceClass: 'HandheldCompanion.Devices.GPDWin5',
    deviceIdentity: { manufacturer: 'GPD', model: 'WRONG' },
  };
  const rejectedLauncher = new FakeLauncher();
  const rejected = newFanLifecycle({
    enabled: true,
    adapter: rejectedAdapter,
    launcher: rejectedLauncher,
    heartbeatIntervalMs: 0,
    savedIdentity: { manufacturer: 'GPD', model: 'G1618-05' },
  });
  // 920 §8.0-D B5 remap: the old assertion required `state === 'conflict-locked'`
  // and a bare `handshake` call list. E9 deleted the lock (fanHost.ts L1449-1458:
  // the device gate denies → safe startup rollback → throw). Intent preserved:
  // an identity mismatch must admit no control at all — no hardware open, no
  // lease — and the started Host must be rolled back.
  // Probe (2026-09-20): start() throws; calls = `handshake,shutdown`; launcher = `start,stop`.
  let rejectedThrew = false;
  try { await rejected.start(); } catch { rejectedThrew = true; }
  assert(rejectedThrew, 'identity mismatch must be rejected instead of admitting control');
  assert(!rejectedAdapter.calls.includes('open') && !rejectedAdapter.calls.includes('open-events') &&
    !rejectedAdapter.calls.includes('acquire'),
  'identity mismatch must not open hardware or acquire a lease');
  let rejectedApplyThrew = false;
  try {
    await rejected.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 100, dutyPercent: 100 }]);
  } catch { rejectedApplyThrew = true; }
  assert(rejectedApplyThrew && !rejectedAdapter.calls.includes('enable'),
    'a rejected identity must keep control unavailable');
  assert(rejectedLauncher.calls.join(',') === 'start,stop', 'rejected host must be stopped');

  // HC handshake identity uses long field names; the persisted WMI identity
  // uses short names. A restart/recovery must normalize both representations
  // instead of falsely hiding the already accepted Fan route.
  const aliasAdapter = new FakeAdapter();
  aliasAdapter.handshakeResult = {
    ...aliasAdapter.handshakeResult,
    deviceIdentity: {
      ManufacturerName: 'GPD',
      SystemModel: 'G1618-05',
      ProductName: 'G1618-05',
      Version: '2.20',
    },
  };
  const aliasLifecycle = newFanLifecycle({
    enabled: true,
    adapter: aliasAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
    savedIdentity: { manufacturer: 'GPD', model: 'G1618-05', product: 'G1618-05', bios: '2.20' },
  });
  const aliasGate = await aliasLifecycle.start();
  assert(aliasGate.allowed, 'HC long-form identity must match persisted short-form identity');
  await aliasLifecycle.close();

  // ---------------------------------------------------------------------------
  // FAN-926R U2/U3: on-demand admission. Shell startup must not spawn the Fan
  // Host, run a handshake or touch HC; the inner-page switch is the only
  // trigger, it publishes Starting before the round trip, and it is idempotent.
  // ---------------------------------------------------------------------------
  const dormantAdapter = new FakeAdapter();
  const dormantLauncher = new FakeLauncher();
  const dormant = newFanLifecycle({ enabled: true, adapter: dormantAdapter, launcher: dormantLauncher, heartbeatIntervalMs: 0 });
  assert(dormant.phase === 'stopped' && dormant.state === 'stopped' && dormant.processId === null &&
    dormant.currentLease === null && dormantAdapter.calls.length === 0 && dormantLauncher.calls.length === 0,
  `constructing the lifecycle must not start anything (phase=${dormant.phase}, adapter=${dormantAdapter.calls.join(',')}, launcher=${dormantLauncher.calls.join(',')})`);
  const dormantDisabled = newFanLifecycle({ enabled: false, adapter: new FakeAdapter(), launcher: new FakeLauncher() });
  assert(dormantDisabled.phase === 'disabled', 'a disabled lifecycle must report the disabled phase');

  await dormant.start();
  assert(dormant.phase === 'ready' && dormantLauncher.calls.join(',') === 'start' &&
    dormantAdapter.calls.join(',') === 'handshake',
  `the first start must spawn the Host and stop at the handshake (phase=${dormant.phase}, adapter=${dormantAdapter.calls.join(',')})`);
  await dormant.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  assert(dormant.phase === 'active', 'live write authority must project the active phase');
  await dormant.disable();
  assert(dormant.phase === 'ready', 'disable must fall back to the handshake-only phase');
  dormant.setPowerGeneration(1);
  await dormant.suspend();
  assert(dormant.phase === 'recovering', 'the sleep/rebuild window must project the recovering phase');
  await dormant.close();
  assert(dormant.phase === 'stopped' && dormantLauncher.calls.join(',') === 'start,stop',
    'a closed lifecycle must return to stopped and stop its Host');

  const shellSource = readFileSync('src/App.vue', 'utf8');
  const switchSource = readFileSync('src/views/FanView.vue', 'utf8');
  const powerSource = readFileSync('src/views/PowerView.vue', 'utf8');
  const bootRegion = shellSource.slice(shellSource.indexOf('app-startup-ready'), shellSource.indexOf('void syncMouseModeAtStartup();'));
  assert(bootRegion.length > 0 &&
    !/fanHostLifecycle\.start\(|recordFanHandshake\(|FAN_REAL_HOST_ENABLED/.test(bootRegion),
  'shell startup must not spawn the Fan Host, handshake, or read the real-host switch');
  assert(!/fan-feature:visibility/.test(shellSource),
    'the removed visibility gate must not come back');
  const mountRegion = switchSource.slice(switchSource.indexOf('onMounted(() => {'), switchSource.indexOf('onUnmounted(() => {'));
  assert(mountRegion.includes('adoptOnDemandEntryState()') &&
    !mountRegion.includes('fanHostLifecycle.start') && !mountRegion.includes('ensureSupported'),
  'mounting the fan page must adopt/resume only - never start or handshake');
  assert(switchSource.includes('busy || connecting') && switchSource.includes('connecting.value = true;') &&
    switchSource.includes('await nextTick();'),
  'the switch must publish a Starting state and stay clickable before the handshake');
  // FAN-926R-WAKEUI (user feedback item 1): while the match is in flight the page
  // must use the matching vocabulary, and the switch may only be unpressable
  // DURING that flight - never pre-disabled, because the first click is what
  // starts the match.
  // FAN-926R UX-1 (2026-09-27 user feedback): SUPERSEDES the earlier "匹配中" wording
  // anchor. The first click must now say "启用中…" and the progress line must name the
  // current start stage (bridge `startStage` -> START_STAGE_TEXT), because the real cold
  // start takes ~18 s and a single undifferentiated label reads as "stuck". The intent is
  // preserved: explicit in-flight vocabulary + an actionable pre-start hint.
  assert(switchSource.includes('启用中：正在启动风扇服务…') && switchSource.includes('关闭中：正在交回 OEM 默认转速…') &&
    switchSource.includes('START_STAGE_TEXT') &&
    switchSource.includes('正在启动风扇服务…'),
  'the inner page must name the current stage before and during a start');
  const toggleAt = switchSource.indexOf('@click="toggleControl"');
  assert(toggleAt > 0, 'the enable toggle must still be wired to toggleControl');
  const toggleMarkup = switchSource.slice(Math.max(0, toggleAt - 400), toggleAt);
  const toggleDisabled = /:disabled="([^"]+)"/.exec(toggleMarkup);
  assert(toggleDisabled !== null && toggleDisabled[1].includes('busy || connecting'),
    `the enable toggle must only be blocked while a start is in flight, got ${toggleDisabled === null ? 'no binding' : toggleDisabled[1]}`);
  // FAN-926R-WAKEUI (user feedback item 3): the evidence shows a lost session does
  // NOT come back by itself (resume-recovery-exhausted, retrigger=next-resume-ready),
  // so the promise of a ten second guard recovery must be gone, and the suspend
  // text must not claim a proven OEM restore.
  assert(!switchSource.includes('10 秒守护恢复中') && !switchSource.includes('已进入恢复等待'),
    'the copy must not promise an automatic recovery that cannot happen');
  assert(!switchSource.includes('睡眠前已恢复 OEM 控制') && switchSource.includes('未证实'),
    'the suspend copy must not claim a proven OEM restore, and must state the unproven ownership');
  assert(switchSource.includes('请重新打开开关'),
    'the stopped-session copy must state the actionable step');
  // Negative control: re-adding the old boot-time start must be caught by the same rule.
  const mutantBoot = bootRegion.replace('fanDiagnosticLog(', 'await fanHostLifecycle.start(); fanDiagnosticLog(');
  assert(/fanHostLifecycle\.start\(|recordFanHandshake\(|FAN_REAL_HOST_ENABLED/.test(mutantBoot),
    'negative control: a re-added boot-time start must be detected');

  // ---------------------------------------------------------------------------
  // FAN-926R §2.1 / FAN-932 §2.3 (adjudication): the inner-page switch is the only
  // PAGE-ENTRY start trigger. Merely entering the page starts nothing, for every
  // state of the persisted startupDesired.fanControl preference; the preference is
  // not an input to the entry decision at all. FAN-932 only adds a separate App-level
  // boot/wake entry (see the ensureFanAutoStart cases above); page entry stays inert.
  // These cases drive the production chain: the shared entry policy plus the real
  // FanHostLifecycle, not a source grep.
  // ---------------------------------------------------------------------------
  for (const setting of ['unset', 'false', 'true']) {
    const entryAdapter = new FakeAdapter();
    const entryLauncher = new FakeLauncher();
    const entryLifecycle = newFanLifecycle({
      enabled: true, adapter: entryAdapter, launcher: entryLauncher, heartbeatIntervalMs: 0,
    });
    // The page resolves the entry action from the shared policy and then acts.
    const action = resolveFanEntryAction(entryLifecycle.phase);
    if (action === 'adopt-resident-session') await entryLifecycle.getState();
    assert(action === 'idle' && entryLifecycle.phase === 'stopped' &&
      entryLauncher.calls.length === 0 && entryAdapter.calls.length === 0 &&
      entryLifecycle.processId === null && entryLifecycle.currentLease === null,
    `cold start with startupDesired.fanControl=${setting} must not start, handshake, lease or adopt anything (action=${action}, adapter=[${entryAdapter.calls.join(',')}], launcher=[${entryLauncher.calls.join(',')}])`);
  }
  assert(resolveFanEntryAction('ready') === 'adopt-resident-session' &&
    resolveFanEntryAction('active') === 'adopt-resident-session' &&
    resolveFanEntryAction('recovering') === 'adopt-resident-session' &&
    resolveFanEntryAction('failed') === 'adopt-resident-session',
  'a live/recovering session must be adopted read-only, never restarted or re-handshaked');
  const entryRegion = switchSource.slice(switchSource.indexOf('async function adoptOnDemandEntryState()'),
    switchSource.indexOf('onUnmounted(() => {'));
  assert(entryRegion.length > 0 && !entryRegion.includes('startupDesired') &&
    !entryRegion.includes('requestCurveApply') && !entryRegion.includes('fanHostLifecycle.start'),
  'page entry must not read the boot preference and must not be a second start path');

  // The explicit switch-on path is the one that starts and writes; re-entering the
  // page with that live session adopts it instead of starting a second one.
  const switchAdapter = new FakeAdapter();
  const switchLauncher = new FakeLauncher();
  const switchLifecycle = newFanLifecycle({
    enabled: true, adapter: switchAdapter, launcher: switchLauncher, heartbeatIntervalMs: 0,
  });
  assert(resolveFanEntryAction(switchLifecycle.phase) === 'idle' && switchLauncher.calls.length === 0,
    'entering the page before any explicit enable must not start the Host');
  await switchLifecycle.start();
  await switchLifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  assert(switchLifecycle.phase === 'active' && switchLauncher.calls.join(',') === 'start' &&
    switchAdapter.calls.join(',') === 'handshake,open,open-events,acquire,enable',
  `the explicit switch-on path must be the one that spawns, handshakes and writes (phase=${switchLifecycle.phase}, calls=${switchAdapter.calls.join(',')})`);
  assert(resolveFanEntryAction(switchLifecycle.phase) === 'adopt-resident-session' &&
    switchLauncher.calls.join(',') === 'start' && switchLifecycle.phase === 'active',
  're-entering the page with an active session must adopt it without a second start or handshake');
  await switchLifecycle.close();
  assert(switchLifecycle.phase === 'stopped' && switchLauncher.calls.join(',') === 'start,stop',
    'the explicitly enabled session must also converge on close');

  // FAN-932: the boot/wake fan preference is now a real, operable switch with the
  // fixed copy, and boot/wake auto-start is driven by ONE shared serialized entry.
  const powerRowStart = powerSource.indexOf('label="\u5f00\u673a/\u4f11\u7720\u5524\u9192\u542f\u52a8\u98ce\u6247"');
  assert(powerRowStart > 0, 'the startup page must still render the boot/wake fan row');
  const powerRow = powerSource.slice(powerSource.lastIndexOf('<Toggle', powerRowStart), powerSource.indexOf('/>', powerRowStart));
  assert(powerRow.includes('v-model="fanControlOnBoot"') && !powerRow.includes(':disabled="true"') &&
    powerRow.includes('\u6839\u636e\u9002\u914d\u673a\u578b\u81ea\u52a8\u542f\u52a8\u98ce\u6247'),
  'the boot/wake fan row must be an operable bound switch with the fixed copy');

  // FAN-932 §2.1: boot and wake share one serialized, generation-deduped entry.
  const autoStartStart = shellSource.indexOf('function ensureFanAutoStart(');
  const autoStartEnd = shellSource.indexOf('const router = useRouter();', autoStartStart);
  assert(autoStartStart >= 0 && autoStartEnd > autoStartStart,
    'the single shared auto-start orchestration entry must exist');
  const autoStartBody = shellSource.slice(autoStartStart, autoStartEnd);
  assert(autoStartBody.includes('fanHostLifecycle.hasControlIntent') &&
    autoStartBody.includes("readSettingsSection<{ fanControl?: boolean }>('startupDesired')") &&
    autoStartBody.includes('startup.fanControl !== true') &&
    autoStartBody.includes('fanHostLifecycle.start()') &&
    autoStartBody.includes('gate.allowed') && autoStartBody.includes('gate.writeReady') &&
    autoStartBody.includes('getFanPresetCurve(') && autoStartBody.includes('fanHostLifecycle.apply('),
  'the shared entry must read the preference, defer to an existing intent, gate on allowed+writeReady and apply the saved curve');
  assert(autoStartBody.indexOf('fanHostLifecycle.start()') < autoStartBody.indexOf('gate.writeReady') &&
    autoStartBody.indexOf('gate.writeReady') < autoStartBody.indexOf('fanHostLifecycle.apply('),
  'the shared entry must start, then gate on writeReady, then apply (never apply before admission)');
  // Negative control: an apply issued before the admission gate must be rejected.
  const mutantAutoStart = autoStartBody.replace(
    'const gate = await fanHostLifecycle.start();',
    'await fanHostLifecycle.apply(getFanPresetCurve(getFanFeatureSettings().preset)); const gate = await fanHostLifecycle.start();');
  assert(mutantAutoStart.indexOf('fanHostLifecycle.apply(') < mutantAutoStart.indexOf('gate.writeReady'),
    'negative control: an apply issued before the admission gate must be detected');
  assert(shellSource.includes("ensureFanAutoStart('boot')") &&
    shellSource.includes("ensureFanAutoStart('resume', resumeGeneration)"),
  'boot and wake must both drive the single shared auto-start entry');
  // Boot must wait for the renderer reattach decision before it may auto-start.
  const reattachHook = shellSource.indexOf('const fanReattach = fanHostLifecycle.reattachAfterRendererRestart()');
  assert(reattachHook > 0 &&
    shellSource.indexOf("fanReattach.then(() => ensureFanAutoStart('boot'))") > reattachHook,
  'the boot auto-start must run only after the renderer reattach decision resolves');
  // Wake must sequence resume() before the auto-start (never two concurrent writes).
  const resumeReadyIdx = shellSource.indexOf('void fanHostLifecycle.resume().catch(');
  assert(resumeReadyIdx > 0 &&
    shellSource.indexOf("ensureFanAutoStart('resume', resumeGeneration)", resumeReadyIdx) > resumeReadyIdx,
  'wake must complete resume() before it may auto-start');

  // FAN-932 §2.1(3): an already-established intent reports itself, which is what
  // makes the shared entry a no-op instead of a second, curve-overwriting write.
  {
    const intentAdapter = new FakeAdapter();
    const intentLifecycle = newFanLifecycle({
      enabled: true, adapter: intentAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0,
    });
    assert(intentLifecycle.hasControlIntent === false, 'a fresh lifecycle must report no control intent');
    await intentLifecycle.start();
    assert(intentLifecycle.hasControlIntent === false, 'a handshake-only session is not yet a control intent');
    await intentLifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 100, dutyPercent: 100 }]);
    assert(intentLifecycle.hasControlIntent === true, 'applying a curve must register an active control intent');
    await intentLifecycle.close();
    assert(intentLifecycle.hasControlIntent === false, 'closing the session must clear the active control intent');
  }

  // ---------------------------------------------------------------------------
  // FAN-927（2026-09-27 用户批准）§3/§5 承重验证
  // ---------------------------------------------------------------------------
  // B 组：**从未开启**就该 NoWork —— 零 Fan HTTP、零进程枚举、零 Host 启动、零等待任务。
  {
    const noIntentAdapter = new FakeAdapter();
    const noIntentLauncher = new FakeLauncher();
    const noIntent = new FanHostLifecycle({
      enabled: true,
      adapter: noIntentAdapter,
      launcher: noIntentLauncher,
      heartbeatIntervalMs: 60_000,
      readNativeActivity: async () => null,
    });
    noIntent.setPowerGeneration(1);
    noIntent.observePowerBoundary('resuming', 1);
    noIntent.observePowerBoundary('resume-ready', 1);
    // 连续多次电源通知：幂等 NoWork，绝不累积等待任务或请求。
    for (let i = 0; i < 3; i += 1) await noIntent.resume();
    assert(noIntentAdapter.calls.length === 0 && noIntentLauncher.calls.length === 0 &&
      noIntent.state === 'stopped',
    `no active control intent must be a zero-work NoWork (adapter=${noIntentAdapter.calls.join(',')}, launcher=${noIntentLauncher.calls.join(',')}, state=${noIntent.state})`);
    // side栏常驻 / savedIdentity / configured / 默认 enabled 开关都不是活动意图。
    noIntent.setSavedIdentity({ manufacturer: 'GPD', model: 'G1618-05' });
    await noIntent.resume();
    assert(noIntentAdapter.calls.length === 0 && noIntentLauncher.calls.length === 0,
      'savedIdentity/config must never be promoted to an active control intent');
  }

  // B 组：本地 JS 为空、但 native 有活动记录 ⇒ 可重接（不据"process 为空"判无 Host）。
  {
    const reattachAdapter = new FakeAdapter();
    const reattachLauncher = new FakeLauncher();
    const reattach = new FanHostLifecycle({
      enabled: true,
      adapter: reattachAdapter,
      launcher: reattachLauncher,
      heartbeatIntervalMs: 60_000,
      readNativeActivity: async () => ({
        schemaVersion: 1,
        active: true,
        recoverable: false,
        revision: 4,
        curve: [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 }],
      }),
    });
    assert(await reattach.reattachRenderer(),
      'a same-run renderer rebuild must be able to adopt the native-confirmed intent');
    assert(reattachAdapter.calls.length === 0 && reattachLauncher.calls.length === 0,
      'adopting the recorded intent must not start the Host or issue any request');
  }

  // E 组（§5.1）：同 run 重接**不得**重复 Open / 重复下发同一条曲线。
  {
    const liveAdapter = new LiveSessionAdapter();
    const live = new FanHostLifecycle({
      enabled: true,
      adapter: liveAdapter,
      launcher: new FakeLauncher(),
      heartbeatIntervalMs: 60_000,
      readNativeActivity: async () => ({
        schemaVersion: 1,
        active: true,
        recoverable: false,
        revision: 2,
        curve: [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 }],
      }),
    });
    // 会话仍在：只接上（同步意图 + 客户端 lease），不重开 HC、不重发曲线。
    liveAdapter.calls.length = 0;
    const bound = await live.reattachAfterRendererRestart();
    assert(bound, 'a live resident session must be re-attachable');
    assert(!liveAdapter.calls.includes('open') && !liveAdapter.calls.includes('open-events') &&
      !liveAdapter.calls.includes('enable') && !liveAdapter.calls.includes('preset'),
    `re-attach must not re-open HC or re-send the same curve (calls=${liveAdapter.calls.join(',')})`);
    // R1 持有曲线但没有客户端 lease ⇒ 只可 fresh acquire，不得据此判 Host 不健康。
    assert(liveAdapter.calls.includes('acquire') && live.currentLease !== null,
      `a missing client lease must be freshly acquired, not treated as an unhealthy Host (calls=${liveAdapter.calls.join(',')})`);
    await live.close();
  }

  // E 组（§5.2）：崩溃前仍有效的记录 ⇒ 一次有界恢复并**实际再调**。
  {
    const crashAdapter = new FakeAdapter();
    const crashLauncher = new FakeLauncher();
    const crashCurve = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 }];
    const crash = new FanHostLifecycle({
      enabled: true,
      adapter: crashAdapter,
      launcher: crashLauncher,
      heartbeatIntervalMs: 60_000,
      readNativeActivity: async () => ({
        schemaVersion: 1,
        active: false,
        recoverable: true,
        revision: 9,
        curve: crashCurve,
        deviceClass: 'HandheldCompanion.Devices.GPDWin5',
      }),
    });
    assert(await crash.recoverAfterUncleanExit(),
      'an unclean prior run with a still-valid record must recover once');
    assert(crashAdapter.calls.includes('enable') && crashAdapter.calls.filter((call) => call === 'enable').length === 1,
      `crash recovery must actually re-apply the curve exactly once (calls=${crashAdapter.calls.join(',')})`);
    assert(crashLauncher.calls.join(',') === 'start',
      `crash recovery must take the controlled start path once (launcher=${crashLauncher.calls.join(',')})`);
    await crash.close();
  }

  // E 组：资格不成立时**一律不自动开启**（正常退出 / 曾配置但未启用 / 已 disable /
  // 旧 P2 记录 / 损坏 / 跨系统启动都由 native 记录裁定；前端另拒"不同设备"）。
  {
    const ineligibleAdapter = new FakeAdapter();
    const ineligibleLauncher = new FakeLauncher();
    const ineligible = new FanHostLifecycle({
      enabled: true,
      adapter: ineligibleAdapter,
      launcher: ineligibleLauncher,
      heartbeatIntervalMs: 60_000,
      readNativeActivity: async () => ({
        schemaVersion: 1,
        active: false,
        recoverable: false,
        revision: 0,
        curve: [{ tempC: 0, dutyPercent: 0 }, { tempC: 100, dutyPercent: 100 }],
      }),
    });
    assert((await ineligible.recoverAfterUncleanExit()) === false &&
      ineligibleAdapter.calls.length === 0 && ineligibleLauncher.calls.length === 0,
    'a normal-exit / configured-only / disabled record must not auto-open');
  }
  {
    const mismatchAdapter = new FakeAdapter();
    const mismatch = new FanHostLifecycle({
      enabled: true,
      adapter: mismatchAdapter,
      launcher: new FakeLauncher(),
      heartbeatIntervalMs: 60_000,
      readNativeActivity: async () => ({
        schemaVersion: 1,
        active: false,
        recoverable: true,
        revision: 3,
        curve: [{ tempC: 0, dutyPercent: 0 }, { tempC: 100, dutyPercent: 100 }],
        deviceClass: 'HandheldCompanion.Devices.ROGAlly',
      }),
    });
    // 记录里的设备类与本次握手不同 ⇒ 不写硬件（FakeAdapter 握手是 GPDWin5）。
    assert((await mismatch.recoverAfterUncleanExit()) === false && !mismatchAdapter.calls.includes('enable'),
      'a device mismatch must not auto-apply the recorded curve');
    await mismatch.close();
  }

  // A 组变异控制：把"运行期逐文件 certutil / 依赖全扫"加回必须被抓（本仓源码判据）。
  assert(!bridgeSource.includes('certutil.exe') && !bridgeSource.includes('validateFanHostDependencies'),
    'runtime per-file hashing must stay removed from the bridge');
  assert(bridgeSource.includes('ensureFanHostDeployment') &&
    bridgeSource.includes('FAN_HOST_DEPLOYMENT_RECORD_FILE') &&
    bridgeSource.includes('deployment-record-hit'),
  'the launcher must gate on the small deployment record, not on a per-file scan');

  // ---------------------------------------------------------------------------
  // A 组（§7 调用次数验收）：部署边界判定用**实际调用次数**证明
  // "正常开启：逐文件 certutil=0、重复安装验证=0、全文件遍历/哈希=0"，
  // 而"记录缺失/不匹配"才允许**恰好一次**完整初始化（含便携包/旧安装首次开启）。
  // ---------------------------------------------------------------------------
  {
    const root = mkdtempSync(join(tmpdir(), 'fan927-deploy-'));
    const payloadRoot = join(root, 'PowerControl', 'fan-host-v2');
    const runtimeRoot = join(root, 'PowerControl', 'handheldcompanion-runtime', 'HC-CANDIDATE-0.32.4.0-06c0b954-20260902');
    const stateRoot = join(root, 'state');
    mkdirSync(payloadRoot, { recursive: true });
    mkdirSync(runtimeRoot, { recursive: true });
    mkdirSync(stateRoot, { recursive: true });
    const runtimeId = 'HC-CANDIDATE-0.32.4.0-06c0b954-20260902';
    const payloadManifestPath = join(payloadRoot, 'YeManFanHost.payload.json');
    const runtimeManifestPath = join(runtimeRoot, 'HandheldCompanion.runtime.json');
    writeFileSync(payloadManifestPath, JSON.stringify({
      schemaVersion: 2,
      runtimeManifest: `..\\handheldcompanion-runtime\\${runtimeId}\\HandheldCompanion.runtime.json`,
      runtimeId,
      files: [{ path: 'YeManFanHost.dll', sha256: 'a'.repeat(64) }],
    }));
    writeFileSync(runtimeManifestPath, JSON.stringify({
      schemaVersion: 1, runtimeId, files: [{ path: 'HandheldCompanion.dll', sha256: 'b'.repeat(64) }],
    }));
    const sha256FileSync = (path: string) => createHash('sha256').update(readFileSync(path)).digest('hex');
    const recordPath = join(stateRoot, 'YeManFanHost.deployment.json');
    const writeRecord = (overrides: Record<string, unknown> = {}) => writeFileSync(recordPath, JSON.stringify({
      schemaVersion: 1,
      validationRulesVersion: 1,
      payloadRoot: payloadRoot.replace(/[\\/]+$/, '').replace(/\//g, '\\').toLowerCase(),
      runtimeRoot: runtimeRoot.replace(/[\\/]+$/, '').replace(/\//g, '\\').toLowerCase(),
      payloadManifestSha256: sha256FileSync(payloadManifestPath),
      runtimeManifestSha256: sha256FileSync(runtimeManifestPath),
      runtimeId,
      result: 'ok',
      ...overrides,
    }));
    const config = resolveFanHostConfig(join(root, 'PowerControl'));
    config.hcRuntimeDirectory = runtimeRoot;
    config.hcAssemblyPath = join(runtimeRoot, 'HandheldCompanion.dll');
    const counters = { exists: 0, readPaths: [] as string[], hashPaths: [] as string[], installerRuns: 0, installerCalls: 0 };
    const io: FanHostDeploymentIo = {
      exists: async (path) => { counters.exists += 1; return existsSync(path); },
      readTextFile: async (path) => { counters.readPaths.push(path); return readFileSync(path, 'utf8'); },
      sha256File: async (path) => { counters.hashPaths.push(path); return sha256FileSync(path); },
      runInstaller: async () => {
        counters.installerCalls += 1;
        // 模拟安装脚本的完整验证：只有它成功才会写出部署记录。
        if (counters.installerRuns++ === 0) writeRecord();
      },
    };
    // 记录命中：必须**零安装器调用**，且只对两个小清单取摘要（不遍历依赖目录、不读 DLL）。
    writeRecord();
    counters.readPaths.length = 0; counters.hashPaths.length = 0;
    assert(await resolveFanHostDeployment(config, stateRoot, io) === 'record-hit',
      'a matching deployment record must take the fast path');
    assert(counters.installerCalls === 0,
      `a matching deployment record must never run the installer (calls=${counters.installerCalls})`);
    assert(counters.hashPaths.length === 2 &&
      counters.hashPaths.every((path) => path === payloadManifestPath || path === runtimeManifestPath),
    `the fast path may hash only the two small manifests (hashed=${counters.hashPaths.length})`);
    assert(counters.readPaths.every((path) => path === payloadManifestPath || path === recordPath),
      `the fast path must not read dependency files (read=${counters.readPaths.join(',')})`);
    // 便携包/旧安装（记录缺失）：恰好一次完整初始化，成功后即可继续。
    const absentRoot = join(root, 'state-absent');
    mkdirSync(absentRoot, { recursive: true });
    counters.installerCalls = 0; counters.installerRuns = 0;
    const absentIo: FanHostDeploymentIo = { ...io, runInstaller: async () => {
      counters.installerCalls += 1;
      writeFileSync(join(absentRoot, 'YeManFanHost.deployment.json'), readFileSync(recordPath));
    } };
    assert(await resolveFanHostDeployment(config, absentRoot, absentIo) === 'initialized',
      'a portable/legacy install without a record must initialize exactly once');
    assert(counters.installerCalls === 1,
      `a missing record must run the full validation exactly once (calls=${counters.installerCalls})`);
    // 记录不匹配（包代描述变更）：同样只允许一次重新验证。
    writeRecord({ runtimeId: 'HC-CANDIDATE-0.0.0.0-deadbeef-20990101' });
    counters.installerCalls = 0;
    counters.installerRuns = 0;
    assert(await resolveFanHostDeployment(config, stateRoot, io) === 'initialized',
      'a stale package-generation record must trigger one revalidation');
    assert(counters.installerCalls === 1,
      `an identity mismatch must run the full validation exactly once (calls=${counters.installerCalls})`);
    // 安装器没有产出匹配记录 ⇒ 必须诚实拒绝，不能放行启动。
    writeRecord({ result: 'failed' });
    let refused = false;
    try { await resolveFanHostDeployment(config, stateRoot, { ...io, runInstaller: async () => { counters.installerCalls += 1; } }); }
    catch { refused = true; }
    assert(refused, 'a missing/unowned deployment record after validation must refuse to start');
  }

  // ---------------------------------------------------------------------------
  // FAN-929（2026-09-27 用户裁决）：运行期周期成本消除的承重验证
  // ---------------------------------------------------------------------------
  // ① Host profile watchdog 必须是**事件驱动**（一次性武装），不得再有常驻 1 Hz；
  // ② AC/DC 语义改为**写时电源档对齐**（ApplyCurveCore 内），不再靠轮询；
  // ③ 周期诊断写（每 5 tick 一条）撤销，只在 tick 真的动作时留证；
  // ④ 渲染侧运行期自证回读从 1 Hz 降到 5 s；
  // ⑤ AC/DC 由 App.vue 事件触发一次曲线重放。
  assert(!hostSource.includes('new Timer(_ => PowerProfileWatchdogTick(), null, 1000, 1000)') &&
    hostSource.includes('new Timer(_ => PowerProfileWatchdogTick(), null, Timeout.Infinite, Timeout.Infinite)') &&
    hostSource.includes('powerProfileWatchdog.Change(0, Timeout.Infinite);') &&
    !hostSource.includes('powerProfileWatchdog.Change(0, 1000);'),
  'the profile watchdog must be armed event-driven (one-shot), never as a resident 1 Hz poll');
  assert(!hostSource.includes('tick % 5 == 0') && hostSource.includes('if (acted)') &&
    hostSource.includes('private bool PowerProfileWatchdogTickCore()') &&
    hostSource.includes('mode = "event-driven"') && hostSource.includes('intervalMs = 0'),
  'the periodic watchdog diagnostic write must be event-only and the diagnostics must state event-driven');
  // FAN-931 P1.5（2026-09-27 裁决）：一次性武装必须为两条"有界延迟复核"补上**消费者**——
  // 否则 ①discard→等 Applied 的 2 s 截止点永远不会被报告（只有截止之后那一拍才看得到），
  // ②克隆失败的有限重试永远不会真的发生（retry-exhausted 也不可达）。
  // 同时必须仍是边沿驱动：无 pending、本拍未消费到 discard 边沿 ⇒ 0 tick，不得变成 2 s 常驻轮询。
  assert(hostSource.includes('private void ArmBoundedProfileWatchdogFollowUp()') &&
    hostSource.includes('private void RequestPowerProfileWatchdogTick(int delayMs = 0)') &&
    hostSource.includes('externalProfileQueue.DiscardedAppliedDeadlineRemainingMs(') &&
    hostSource.includes('externalProfileQueue.HasRetryPending()'),
  'the one-shot profile watchdog must still have consumers for its bounded follow-ups (discard deadline + retry)');
  const followUpRegion = hostSource.slice(hostSource.indexOf('private void ArmBoundedProfileWatchdogFollowUp()'),
    hostSource.indexOf('private void UnsubscribeExternalProfileEvents()'));
  assert(followUpRegion.includes('if (profileWatchdogDiscardBoundaryConsumed)'),
    'the discard-deadline follow-up must only be armed when this tick really consumed a discard boundary (no resident re-arm loop)');
  assert(!/powerProfileWatchdog\.Change\(0,\s*\d/.test(hostSource),
    'negative control: a fixed-interval resident watchdog arm (Change(0, <ms>)) must be rejected');
  assert(hostSource.includes('if (profilePowerLineOnline is null || profilePowerLineOnline.Value != IsPowerLineOnline())') &&
    (hostSource.match(/CaptureHcProfileTemplate\(\);/g) ?? []).length >= 3,
  'the AC/DC power-line alignment must happen at write time inside ApplyCurveCore');
  assert(bridgeSource.includes('async reapplyActiveCurve(reason: string): Promise<boolean>') &&
    shellSource.includes("fanHostLifecycle.reapplyActiveCurve('ac-changed')"),
  'AC/DC must drive exactly one curve replay from the renderer event, not from Host polling');
  const telemetryInterval = /telemetryTimer = window\.setInterval\(\(\) => \{ void refreshTelemetry\(\); \}, (\d+)\);/.exec(switchSource);
  assert(telemetryInterval !== null && telemetryInterval[1] === '5000',
    `the running-phase self-proof readback must be 5 s, got ${telemetryInterval === null ? 'no interval' : telemetryInterval[1]}`);
  // FAN-931 P1.4（2026-09-27 裁决）：展示轮询只在「本页激活**且**可见」时运行。
  // KeepAlive 缓存下切走只触发 onDeactivated（不触发 onUnmounted），旧实现让隐藏页继续
  // 每 5 s 读一次 Host 状态；document 隐藏同样要停。
  assert(switchSource.includes('onActivated(() => {') && switchSource.includes('onDeactivated(() => {') &&
    switchSource.includes("document.addEventListener('visibilitychange', onDocumentVisibilityChange)") &&
    switchSource.includes("document.removeEventListener('visibilitychange', onDocumentVisibilityChange)"),
  'the cached fan page must stop display polling on deactivate/hide and re-read on re-activate');
  const startTelemetryRegion = switchSource.slice(switchSource.indexOf('function startTelemetry()'),
    switchSource.indexOf('function stopTelemetry()'));
  assert(startTelemetryRegion.includes('if (!viewActive || !viewVisible) return;'),
    'startTelemetry must refuse to revive a hidden/deactivated page timer (async enable/adopt returns)');
  const stopTelemetryRegion = switchSource.slice(switchSource.indexOf('function stopTelemetry()'),
    switchSource.indexOf('function onDocumentVisibilityChange()'));
  assert(stopTelemetryRegion.includes('telemetryGeneration += 1;'),
    'stopping telemetry must invalidate in-flight results (view generation)');
  const refreshRegion = switchSource.slice(switchSource.indexOf('async function refreshTelemetry()'),
    switchSource.indexOf('function startTelemetry()'));
  assert(refreshRegion.includes('if (telemetryInFlightGeneration === telemetryGeneration) return;') &&
    refreshRegion.includes('if (generation !== telemetryGeneration) return;'),
  'refreshTelemetry must keep at most one GET in flight and discard late results by view generation');
  // 负例：退回"无条件起定时器"的旧形态（隐藏页仍取数）必须被本规则抓住。
  assert(!/function startTelemetry\(\): void \{\s*if \(telemetryTimer !== null\) return;/.test(switchSource),
    'negative control: an unconditional startTelemetry (old hidden-page-polling shape) must be rejected');
  // 功能性：AC/DC 重放只在**有活动曲线**时发一次写，且不重开 HC；无曲线时零请求。
  {
    const acAdapter = new FakeAdapter();
    const acLauncher = new FakeLauncher();
    const ac = newFanLifecycle({ enabled: true, adapter: acAdapter, launcher: acLauncher, heartbeatIntervalMs: 60_000 });
    const acCurve = [{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 25 }, { tempC: 70, dutyPercent: 70 }, { tempC: 100, dutyPercent: 100 }];
    assert((await ac.reapplyActiveCurve('ac-changed')) === false && acAdapter.calls.length === 0,
      'AC/DC replay without an active curve must issue zero requests');
    await ac.start();
    await ac.apply(acCurve);
    const beforeReplay = acAdapter.calls.length;
    assert(await ac.reapplyActiveCurve('ac-changed') === true,
      'AC/DC replay must re-apply the active curve');
    const replayCalls = acAdapter.calls.slice(beforeReplay);
    assert(replayCalls.filter((call) => call === 'enable').length <= 1 && !replayCalls.includes('open'),
      `AC/DC replay must reuse the open HC session with at most one write (calls=${replayCalls.join(',')})`);
    await ac.close();
  }

  console.log('fan host lifecycle selftest: PASS (dark launch, gate, lease, restore, suspend/resume, close, FAN-926R on-demand admission)');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
  // 失败路径必须强制退出：各场景以 heartbeatIntervalMs: 0 启停租约续租定时器，
  // 当断言在 close() 成功之前抛出时该定时器仍然存活，Node 事件循环不会清空 ⇒
  // 进程永久挂起、退出码不可见（实测：F-2 断言处挂起 30 s 未退出）。
  // 这只恢复失败可观测性，不改变任何断言与生产行为（与 game_valve_race / m2 / speedhack 套件一致）。
  process.exit(1);
});
