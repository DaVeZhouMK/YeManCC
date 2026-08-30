import {
  FanHostLifecycle,
  evaluateFanDeviceGate,
  isLegacyUnauthenticatedFanHostHealth,
  resolveFanHostConfig,
  type FanHostLauncher,
  type FanHostProcess,
} from '../src/bridge/fanHost';
import type { FanApiAdapter, FanHandshake, FanLease, FanNode, FanPreset, FanState } from '../src/bridge/fanApi';
import { readFileSync } from 'node:fs';

const assert = (condition: unknown, message: string): asserts condition => {
  if (!condition) throw new Error(message);
};

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
      resumed.hardwareWritesEnabled = true;
      resumed.activeCurve = this.autoResumeCurve;
      resumed.openCalled = true;
      resumed.openEventsCalled = true;
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
      return recovered;
    }
    if (this.pendingCloseCompleted) return state('Stopped');
    return state();
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
  async suspend() {
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

async function main(): Promise<void> {
  assert(isLegacyUnauthenticatedFanHostHealth({ status: 200, body: '{"host":"YeManFanHost","protocolVersion":2}' }, 2),
    'legacy host health classifier must accept an unauthenticated protocol-2 response');
  assert(!isLegacyUnauthenticatedFanHostHealth({ status: 401, body: '{"error":"API_SESSION_REQUIRED"}' }, 2),
    'legacy host health classifier must reject an authenticated token mismatch response');
  assert(!isLegacyUnauthenticatedFanHostHealth({ status: 200, body: '{"host":"other-service","protocolVersion":2}' }, 2),
    'legacy host health classifier must reject an unrelated loopback service');
  const hostSource = readFileSync('C:\\SOFT\\YeManCC-Work\\FanLab\\real-host\\Program.cs', 'utf8');
  const bridgeSource = readFileSync('C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\src\\bridge\\fanHost.ts', 'utf8');
  const apiSource = readFileSync('C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\src\\bridge\\api.ts', 'utf8');
  const nativeSource = readFileSync('C:\\SOFT\\YeManCC-Work\\YeManCC-source\\YeManCC3\\native\\main.cpp', 'utf8');
  const profileCallbackSources = [
    'C:\\SOFT\\YeManCC-Work\\FanLab\\hc-upstream\\HandheldCompanion\\Devices\\MSI\\ClawA1M.cs',
    'C:\\SOFT\\YeManCC-Work\\FanLab\\hc-upstream\\HandheldCompanion\\Devices\\Lenovo\\LegionGo.cs',
    'C:\\SOFT\\YeManCC-Work\\FanLab\\hc-upstream\\HandheldCompanion\\Devices\\Lenovo\\LegionGoTablet2.cs',
    'C:\\SOFT\\YeManCC-Work\\FanLab\\hc-upstream\\HandheldCompanion\\Devices\\ASUS\\ROGAlly.cs',
  ].map((path) => readFileSync(path, 'utf8'));
  const deviceMatrix = readFileSync('C:\\SOFT\\YeManCC-Work\\FanLab\\HC-DEVICE-MATRIX-BATCH03-20260817.md', 'utf8');
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
  const asusRestoreStart = hostSource.indexOf('private void MarkHcOemReleaseCallbackCompleted()');
  const asusRestoreBody = hostSource.slice(asusRestoreStart, asusRestoreStart + 1600);
  const hcOpenCoreStart = hostSource.indexOf('private void OpenCore()');
  const hcOpenCoreEnd = hostSource.indexOf('\n    public void OpenEvents()', hcOpenCoreStart);
  const hcOpenCoreBody = hostSource.slice(hcOpenCoreStart, hcOpenCoreEnd);
  const hcOpenEventsCoreStart = hostSource.indexOf('private void OpenEventsCore()');
  const hcOpenEventsCoreEnd = hostSource.indexOf('\n    private void OpenHcDevice()', hcOpenEventsCoreStart);
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
  const expireLeaseStart = hostSource.indexOf('private void ExpireLease()');
  const expireLeaseEnd = hostSource.indexOf('\n    // Lease loss is an emergency cleanup path', expireLeaseStart);
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
    hostSource.includes('EnsureHcDeviceOpenForRestore();') &&
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
    expireLeaseBody.includes('RestoreHardware(close: false)') &&
    !expireLeaseBody.includes('RestoreHardware(close: true)') &&
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
  assert(bridgeSource.includes('recoverAfterHidRemoval') &&
    bridgeSource.includes('Open -> OpenEvents -> lease') &&
    bridgeSource.includes('private recoveryOwner'),
  'bridge must retain one recovery owner and rebuild only after a stable route');
  assert(bridgeSource.includes('const FAN_GUARD_INTERVAL_MS = 10_000;') &&
    bridgeSource.includes('const FAN_GUARD_MAX_CONSECUTIVE_ENABLE_FAILURES = 5;') &&
    bridgeSource.includes('private fanGuardArmed = false;') &&
    bridgeSource.includes('private desiredCurve: FanNode[] | null = null;') &&
    bridgeSource.includes('private async runFanGuardOnce(source = \'timer\')') &&
    bridgeSource.includes('private scheduleFanGuard(): void') &&
    bridgeSource.includes('private consecutiveFanGuardEnableFailures = 0;') &&
    bridgeSource.includes('FAN_GUARD_RESUME_PENDING') &&
    bridgeSource.includes('this.scheduleFanGuard();') &&
    !bridgeSource.includes('const RECOVERY_WINDOW_MS = 60_000;') &&
    !bridgeSource.includes('const RECOVERY_MAX_ATTEMPTS = 3;') &&
    !bridgeSource.includes('notifyPowerSourceChanged('),
  'fan recovery must use one resident ten-second guard with unbounded retry and no AC/DC trigger');
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
  const prePayloadRecovery = bridgeSource.indexOf('recoverPreviousHostBeforePayloadMutation(config)');
  const payloadInstall = bridgeSource.indexOf('installAndVerifyPayload(config, fanStateDirectory)');
  const tokenCreation = bridgeSource.indexOf('config.sessionToken = createSessionToken()');
  assert(prePayloadRecovery >= 0 && payloadInstall > prePayloadRecovery && tokenCreation > payloadInstall &&
    bridgeSource.includes('private async recoverPreviousHostBeforePayloadMutation(config: FanHostConfig): Promise<void>') &&
    bridgeSource.includes('会话文件缺失但端点要求会话令牌') &&
    bridgeSource.includes('已拒绝无认证关闭请求') &&
    bridgeSource.includes('旧 Fan Host 会话文件缺失且健康端点不可用') &&
    bridgeSource.includes('await this.recoverLegacyUnauthenticatedHost(config);'),
  'startup must recover an exact resident Host before mutating payload ACL/files, distinguish legacy health from a missing-token authenticated Host, then create a new token only after payload verification');
  assert(apiSource.includes("fanStateDir: () => invoke<string>('app.fanStateDir')") &&
    nativeSource.includes('static std::wstring fan_host_state_dir()') &&
    nativeSource.includes('ipc_on("app.fanStateDir"') &&
    nativeSource.includes('const auto path = fan_host_state_dir() + L"\\\\YeManFanHost.session";') &&
    bridgeSource.includes('const fanStateDirectory = await app.fanStateDir();') &&
    bridgeSource.includes("const legacyPath = joinWindowsPath(legacyDataDirectory, 'fan-host\\\\YeManFanHost.session');") &&
    bridgeSource.includes('Fan Host 会话令牌未可靠落盘') &&
    bridgeSource.includes('const persistedToken = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim();'),
  'renderer, native exit recovery and emergency tooling must share one stable session location, and the capability must be read back before any Host can launch');
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
    hostSource.includes('if (IsOpen || (openAttempted && hcOpenInvocationStarted))') &&
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
  const disabled = new FanHostLifecycle({ enabled: false, adapter: disabledAdapter, launcher: disabledLauncher });
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
  const readOnlyLifecycle = new FanHostLifecycle({ enabled: true, adapter: readOnlyAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  const readOnlyGate = await readOnlyLifecycle.start();
  assert(readOnlyGate.allowed && !readOnlyGate.writeReady, 'unverified mapped route must be handshake-visible but not write-ready');
  let readOnlyRejected = false;
  try { await readOnlyLifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 50 }, { tempC: 100, dutyPercent: 90 }]); }
  catch { readOnlyRejected = true; }
  assert(readOnlyRejected && !readOnlyAdapter.calls.includes('open'), 'unverified route must reject before Open()');
  await readOnlyLifecycle.close();

  const adapter = new FakeAdapter();
  const launcher = new FakeLauncher();
  const lifecycle = new FanHostLifecycle({ enabled: true, adapter, launcher, heartbeatIntervalMs: 0 });
  const gate = await lifecycle.start();
  assert(gate.allowed && lifecycle.state === 'awaiting-control', 'authorized device must handshake without enabling control');
  assert(adapter.calls.slice(0, 1).join(',') === 'handshake',
    'startup call order must begin with handshake-only');
  await lifecycle.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(lifecycle.state === 'ready', 'explicit apply must enable control');
  assert(adapter.calls.slice(0, 5).join(',') === 'handshake,open,open-events,acquire,enable',
    'first apply must open -> open-events -> acquire -> enable');
  await lifecycle.suspend();
  assert(lifecycle.state === 'suspended', 'suspend must reach suspended');
  const suspendCalls = adapter.calls.slice(5).join(',');
  assert(suspendCalls === 'suspend', 'suspend must delegate one HC virtual Close to the Host');
  await lifecycle.resume();
  assert(lifecycle.state === 'ready', 'the guard may rebuild after a resume response, but it must finish one serialized attempt');
  assert(adapter.calls.slice(6).join(',') === 'state,resume,handshake,open,open-events,acquire,enable',
    `the guard must use one serialized state/resume/rebuild attempt (calls=${adapter.calls.slice(6).join(',')})`);

  // A failed initial enable arms the resident guard before the first write.
  // The failed attempt must not cancel the timer: the next ten-second tick
  // retries the same curve until the user disables or the process closes.
  const guardRetryAdapter = new FakeAdapter();
  guardRetryAdapter.handshakeFailures = 3;
  const guardRetryLifecycle = new FanHostLifecycle({
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
  await new Promise<void>((resolve) => setTimeout(resolve, 100));
  assert(guardRetryLifecycle.state === 'ready',
    `a failed guarded startup must be retried by the resident timer (state=${guardRetryLifecycle.state}, calls=${guardRetryAdapter.calls.join(',')})`);
  assert(guardRetryAdapter.calls.filter((call) => call === 'handshake').length >= 4 && guardRetryAdapter.calls.includes('enable'),
    'the ten-second guard retry must continue through handshake and enable after the first failed attempt');
  await guardRetryLifecycle.disable();
  const callsAfterGuardDisable = guardRetryAdapter.calls.length;
  await new Promise<void>((resolve) => setTimeout(resolve, 15));
  assert(guardRetryAdapter.calls.length === callsAfterGuardDisable, 'manual disable must disarm the resident fan guard');
  await guardRetryLifecycle.close();

  // The guard is unbounded for transport/handshake/startup failures, but a
  // real Enable failure has a five-consecutive-attempt safety cap. Once the
  // cap is reached no sixth hardware write is sent automatically.
  const guardGiveUpAdapter = new FakeAdapter();
  const guardGiveUpLifecycle = new FanHostLifecycle({
    enabled: true,
    adapter: guardGiveUpAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
    fanGuardIntervalMs: 5,
  });
  await guardGiveUpLifecycle.start();
  await guardGiveUpLifecycle.apply(guardRetryCurve);
  await guardGiveUpLifecycle.suspend();
  guardGiveUpAdapter.enableFailures = 5;
  let firstGuardEnableRejected = false;
  try { await guardGiveUpLifecycle.resume(); } catch { firstGuardEnableRejected = true; }
  await new Promise<void>((resolve) => setTimeout(resolve, 100));
  const guardedEnableCount = guardGiveUpAdapter.calls.filter((call) => call === 'enable').length;
  await new Promise<void>((resolve) => setTimeout(resolve, 25));
  assert(firstGuardEnableRejected && guardedEnableCount === 6 &&
    guardGiveUpAdapter.calls.filter((call) => call === 'enable').length === guardedEnableCount,
    `five consecutive guarded Enable failures must stop automatic writes (count=${guardGiveUpAdapter.calls.filter((call) => call === 'enable').length}, calls=${guardGiveUpAdapter.calls.join(',')})`);
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
  const directClose = new FanHostLifecycle({ enabled: true, adapter: directCloseAdapter, launcher: directCloseLauncher, heartbeatIntervalMs: 0 });
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
  const resumeReadOnly = new FanHostLifecycle({ enabled: true, adapter: resumeReadOnlyAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await resumeReadOnly.start();
  await resumeReadOnly.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  await resumeReadOnly.suspend();
  resumeReadOnlyAdapter.handshakeResult = { ...resumeReadOnlyAdapter.handshakeResult, fanRouteWriteReady: false };
  let resumeReadOnlyRejected = false;
  try { await resumeReadOnly.resume(); } catch { resumeReadOnlyRejected = true; }
  const postResumeReadOnlyCalls = resumeReadOnlyAdapter.calls.slice(6);
  assert(resumeReadOnlyRejected && resumeReadOnly.state !== 'ready' &&
    postResumeReadOnlyCalls.every((call) => call === 'state' || call === 'resume' || call === 'handshake') &&
    !postResumeReadOnlyCalls.includes('open') && !postResumeReadOnlyCalls.includes('acquire') && !postResumeReadOnlyCalls.includes('enable'),
  `write-readiness loss after sleep must keep the guard retryable without reopening or writing HC (rejected=${resumeReadOnlyRejected}, state=${resumeReadOnly.state}, calls=${postResumeReadOnlyCalls.join(',')})`);
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
  const directExit = new FanHostLifecycle({ enabled: true, adapter: directExitAdapter, launcher: directExitLauncher, heartbeatIntervalMs: 0 });
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
  const enableThenSleep = new FanHostLifecycle({ enabled: true, adapter: enableThenSleepAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await enableThenSleep.start();
  const enableBeforeSleep = enableThenSleep.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  const sleepAfterEnable = enableThenSleep.suspend();
  await Promise.all([enableBeforeSleep, sleepAfterEnable]);
  assert(enableThenSleep.state === 'suspended' &&
    enableThenSleepAdapter.calls.join(',') === 'handshake,open,open-events,acquire,enable,suspend',
  'enable followed immediately by sleep must serialize into one HC Close boundary');
  await enableThenSleep.close();
  assert(enableThenSleepAdapter.calls.slice(-2).join(',') === 'close,shutdown',
    'closing a suspended Host must still require authenticated Host close/shutdown');

  // The opposite ordering is more important: after sleep has been admitted,
  // an already queued UI write must not open HC or acquire a lease.
  const sleepThenEnableAdapter = new FakeAdapter();
  const sleepThenEnable = new FanHostLifecycle({ enabled: true, adapter: sleepThenEnableAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await sleepThenEnable.start();
  const sleepBeforeEnable = sleepThenEnable.suspend();
  const enableAfterSleep = sleepThenEnable.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  await sleepBeforeEnable;
  let enableAfterSleepRejected = false;
  try { await enableAfterSleep; } catch { enableAfterSleepRejected = true; }
  assert(enableAfterSleepRejected && sleepThenEnable.state === 'suspended' &&
    sleepThenEnableAdapter.calls.join(',') === 'handshake,suspend' && !sleepThenEnableAdapter.calls.includes('open'),
  'sleep admitted before enable must block HC Open, lease acquisition and curve writes');
  await sleepThenEnable.close();

  // Application shutdown cannot leapfrog an active write either. It is
  // queued behind it and then performs the HC Close boundary once.
  const enableThenCloseAdapter = new FakeAdapter();
  const enableThenCloseLauncher = new FakeLauncher();
  const enableThenClose = new FanHostLifecycle({ enabled: true, adapter: enableThenCloseAdapter, launcher: enableThenCloseLauncher, heartbeatIntervalMs: 0 });
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
  const pendingClose = new FanHostLifecycle({ enabled: true, adapter: pendingCloseAdapter, launcher: pendingCloseLauncher, heartbeatIntervalMs: 0 });
  await pendingClose.start();
  await pendingClose.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  await pendingClose.close();
  assert(pendingClose.state === 'stopped' && pendingCloseLauncher.calls.join(',') === 'start,stop' &&
    pendingCloseAdapter.calls.includes('state') && pendingCloseAdapter.calls.filter((call) => call === 'close').length === 1,
  'HC_CLOSE_PENDING must wait for remote cleanup without issuing a second close before shutdown/launcher stop');

  // A suspend transport failure must retry restore/recovery and then retry
  // the suspend endpoint; it must not leave the curve active across sleep.
  const suspendRetryAdapter = new FakeAdapter();
  suspendRetryAdapter.suspendFailures = 1;
  const suspendRetry = new FanHostLifecycle({ enabled: true, adapter: suspendRetryAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await suspendRetry.start();
  await suspendRetry.applyPreset('balanced');
  await suspendRetry.suspend();
  assert(suspendRetry.state === 'suspended' && suspendRetryAdapter.calls.filter((call) => call === 'suspend').length === 2,
    'suspend failure must recover and retry before entering suspended');
  await suspendRetry.resume();
  await suspendRetry.close();

  // A transient HC write failure may retry once after OEM restore. This is
  // distinct from a known external controller conflict below.
  const retryAdapter = new FakeAdapter();
  retryAdapter.enableFailures = 1;
  const retryLauncher = new FakeLauncher();
  const retry = new FanHostLifecycle({ enabled: true, adapter: retryAdapter, launcher: retryLauncher, heartbeatIntervalMs: 0 });
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
  const remoteClose = new FanHostLifecycle({ enabled: true, adapter: remoteCloseAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
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
  const closedBoundary = new FanHostLifecycle({ enabled: true, adapter: closedBoundaryAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await closedBoundary.start();
  await closedBoundary.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 70, dutyPercent: 60 }, { tempC: 100, dutyPercent: 100 }]);
  assert(closedBoundary.state === 'ready' && closedBoundaryAdapter.calls.filter((call) => call === 'open').length === 2 &&
    closedBoundaryAdapter.calls.filter((call) => call === 'open-events').length === 2 &&
    !closedBoundaryAdapter.calls.includes('close'),
  'a completed HC Close without Hardware callback must be accepted by recovery without a duplicate Close');
  await closedBoundary.close();

  // A known route conflict is a third-party/OEM controller ownership signal,
  // not a reason to retry and race it. The first failed write must restore
  // OEM, release the lease, lock the lifecycle, and make Close safe.
  const initialConflictAdapter = new FakeAdapter();
  initialConflictAdapter.enableFailures = 1;
  initialConflictAdapter.enableFailureMessage = 'FAN_ROUTE_CONFLICT: external fan controller owns the route';
  const initialConflictLauncher = new FakeLauncher();
  const initialConflict = new FanHostLifecycle({ enabled: true, adapter: initialConflictAdapter, launcher: initialConflictLauncher, heartbeatIntervalMs: 0 });
  await initialConflict.start();
  let initialConflictRejected = false;
  try {
    await initialConflict.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  } catch { initialConflictRejected = true; }
  assert(initialConflictRejected && initialConflict.state === 'conflict-locked',
    'known external conflict must lock control instead of retrying');
  assert(initialConflictAdapter.calls.filter((call) => call === 'enable').length === 1 &&
    initialConflictAdapter.calls.slice(-2).join(',') === 'restore,release',
  'known external conflict must restore/release without a second write');
  await initialConflict.disable();
  assert(initialConflict.state === 'stopped' && initialConflictAdapter.calls.slice(-2).join(',') === 'close,shutdown' &&
    initialConflictLauncher.calls.join(',') === 'start,stop',
  'conflict disable must close the resident Host only after OEM restore confirmation');

  // Simulate another controller taking over after YeMan already enabled a
  // curve. The failed heartbeat is the observable boundary: stop scheduling,
  // restore OEM, release the lease and require an explicit later retry.
  const midTakeoverAdapter = new FakeAdapter();
  const midTakeoverLauncher = new FakeLauncher();
  const midTakeover = new FanHostLifecycle({ enabled: true, adapter: midTakeoverAdapter, launcher: midTakeoverLauncher, heartbeatIntervalMs: 0 });
  await midTakeover.start();
  await midTakeover.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  midTakeoverAdapter.heartbeatFailures = 1;
  midTakeoverAdapter.heartbeatFailureMessage = 'FAN_ROUTE_CONFLICT: OEM reclaimed fan control';
  let midTakeoverRejected = false;
  try { await midTakeover.heartbeat(); } catch { midTakeoverRejected = true; }
  assert(midTakeoverRejected && midTakeover.state === 'conflict-locked' && midTakeover.currentLease === null,
    'mid-session takeover must stop YeMan ownership and lock the lifecycle');
  assert(midTakeoverAdapter.calls.slice(-3).join(',') === 'heartbeat,restore,release',
    'mid-session takeover must restore OEM before releasing its lease');
  await midTakeover.close();
  assert(midTakeover.state === 'stopped' && midTakeoverAdapter.calls.slice(-2).join(',') === 'close,shutdown' &&
    midTakeoverLauncher.calls.join(',') === 'start,stop',
  'mid-session takeover close must complete a confirmed Host shutdown');

  // HID removal is owned by the resident Host's one Close/unbind boundary.
  // Once that boundary is observable, the bridge may probe a stable route and
  // replay the acknowledged curve through the canonical Open -> OpenEvents ->
  // lease -> curve order. It must not issue a second restore/release/Close.
  const hidRemovalAdapter = new FakeAdapter();
  const hidRemoval = new FanHostLifecycle({ enabled: true, adapter: hidRemovalAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
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

  // Open() can touch EC and still reject. The lifecycle must mark the session
  // before awaiting it, restore OEM, and leave a clean reacquisition point.
  const openFailureAdapter = new FakeAdapter();
  openFailureAdapter.openFailures = 1;
  const openFailure = new FanHostLifecycle({ enabled: true, adapter: openFailureAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await openFailure.start();
  await openFailure.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(openFailure.state === 'ready' && openFailureAdapter.calls.includes('restore'),
    'partial Open failure must enter OEM restore before automatic retry');
  await openFailure.applyPreset('soft');
  await openFailure.close();

  // OpenEvents is a separate HC boundary; its failure must use the same
  // restore path rather than leaving an Open session active.
  const eventsFailureAdapter = new FakeAdapter();
  eventsFailureAdapter.openEventsFailures = 1;
  const eventsFailure = new FanHostLifecycle({ enabled: true, adapter: eventsFailureAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await eventsFailure.start();
  await eventsFailure.applyPreset('balanced');
  assert(eventsFailure.state === 'ready' && eventsFailureAdapter.calls.includes('restore'),
    'OpenEvents failure must restore before automatic retry');
  await eventsFailure.applyPreset('balanced');
  await eventsFailure.close();

  const remoteEventsCloseAdapter = new FakeAdapter();
  remoteEventsCloseAdapter.remoteTelemetry = true;
  remoteEventsCloseAdapter.closeSessionOnOpenEventsFailure = true;
  remoteEventsCloseAdapter.openEventsFailures = 1;
  const remoteEventsClose = new FanHostLifecycle({ enabled: true, adapter: remoteEventsCloseAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await remoteEventsClose.start();
  await remoteEventsClose.applyPreset('balanced');
  assert(remoteEventsClose.state === 'ready' && remoteEventsCloseAdapter.calls.filter((call) => call === 'open').length === 2 &&
    remoteEventsCloseAdapter.calls.filter((call) => call === 'open-events').length === 2,
  'a Host-side failed OpenEvents that closed HC must be reopened before retry');
  await remoteEventsClose.close();

  // Startup handshake is retried, but never skips the Gate or opens hardware
  // during the transient failure window.
  const handshakeRetryAdapter = new FakeAdapter();
  handshakeRetryAdapter.handshakeFailures = 2;
  const handshakeRetryLauncher = new FakeLauncher();
  const handshakeRetry = new FanHostLifecycle({ enabled: true, adapter: handshakeRetryAdapter, launcher: handshakeRetryLauncher, heartbeatIntervalMs: 0 });
  const handshakeRetryGate = await handshakeRetry.start();
  assert(handshakeRetryGate.allowed && handshakeRetryAdapter.calls.filter((call) => call === 'handshake').length === 3,
    'startup handshake must retry three bounded attempts');
  await handshakeRetry.close();

  // If restore/release itself fails once, the independent close fallback must
  // confirm OEM and mark the process stopped so the next apply restarts it.
  const fallbackAdapter = new FakeAdapter();
  fallbackAdapter.enableFailures = 1;
  fallbackAdapter.restoreFailures = 3;
  fallbackAdapter.recoveryStateAfterRestoreFailure = 'Stopped';
  const fallbackLauncher = new FakeLauncher();
  const fallback = new FanHostLifecycle({ enabled: true, adapter: fallbackAdapter, launcher: fallbackLauncher, heartbeatIntervalMs: 0 });
  await fallback.start();
  await fallback.apply([{ tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 }, { tempC: 60, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 }]);
  assert(fallback.state === 'ready' && fallbackLauncher.calls.join(',') === 'start,stop,start',
    'safe close fallback must allow an automatic clean restart and retry');
  await fallback.close();

  // Disable itself is already a restore boundary.  If that one HC profile
  // call fails, the UI must poll Host recovery rather than issue a second
  // restore request (the source of the old ROG close/high-CPU loop).
  const disableRecoveryAdapter = new FakeAdapter();
  disableRecoveryAdapter.restoreFailures = 1;
  disableRecoveryAdapter.recoveryStateAfterRestoreFailure = 'AwaitingControl';
  const disableRecovery = new FanHostLifecycle({ enabled: true, adapter: disableRecoveryAdapter, launcher: new FakeLauncher(), heartbeatIntervalMs: 0 });
  await disableRecovery.start();
  await disableRecovery.applyPreset('balanced');
  const disabledState = await disableRecovery.disable();
  assert(disabledState.state === 'AwaitingControl' && disableRecovery.state === 'awaiting-control' &&
    disableRecoveryAdapter.calls.filter((call) => call === 'restore').length === 1 &&
    !disableRecoveryAdapter.calls.includes('close'),
  'disable restore failure must use Host-owned recovery polling without a duplicate restore/close');
  await disableRecovery.close();

  // A lost/failed close response must never cause a second frontend Close.
  // The resident Host owns recovery and the frontend may only observe its
  // already-confirmed stopped state before requesting shutdown.
  const closeRetryAdapter = new FakeAdapter();
  closeRetryAdapter.closeFailures = 1;
  closeRetryAdapter.recoveryStateAfterCloseFailure = 'Stopped';
  const closeRetryLauncher = new FakeLauncher();
  const closeRetry = new FanHostLifecycle({ enabled: true, adapter: closeRetryAdapter, launcher: closeRetryLauncher, heartbeatIntervalMs: 0 });
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
  const presetLifecycle = new FanHostLifecycle({
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
  const initialHeartbeatLifecycle = new FanHostLifecycle({
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

  const expiringAdapter = new ExpiringLeaseAdapter();
  const expiringLifecycle = new FanHostLifecycle({
    enabled: true,
    adapter: expiringAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
  });
  await expiringLifecycle.start();
  await expiringLifecycle.applyPreset('balanced');
  await expiringLifecycle.applyPreset('balanced');
  assert(expiringAdapter.calls.slice(5).join(',') === 'heartbeat-invalid,restore,release,acquire,preset',
    'a stale lease must restore/release before reacquiring before the next preset');
  await expiringLifecycle.close();

  const rejectedAdapter = new FakeAdapter();
  rejectedAdapter.handshakeResult = {
    ok: true,
    supported: true,
    deviceClass: 'HandheldCompanion.Devices.GPDWin5',
    deviceIdentity: { manufacturer: 'GPD', model: 'WRONG' },
  };
  const rejectedLauncher = new FakeLauncher();
  const rejected = new FanHostLifecycle({
    enabled: true,
    adapter: rejectedAdapter,
    launcher: rejectedLauncher,
    heartbeatIntervalMs: 0,
    savedIdentity: { manufacturer: 'GPD', model: 'G1618-05' },
  });
  const rejectedGate = await rejected.start();
  assert(!rejectedGate.allowed && rejected.state === 'conflict-locked', 'identity mismatch must lock out');
  assert(rejectedAdapter.calls.join(',') === 'handshake', 'identity mismatch must not open or acquire');
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
  const aliasLifecycle = new FanHostLifecycle({
    enabled: true,
    adapter: aliasAdapter,
    launcher: new FakeLauncher(),
    heartbeatIntervalMs: 0,
    savedIdentity: { manufacturer: 'GPD', model: 'G1618-05', product: 'G1618-05', bios: '2.20' },
  });
  const aliasGate = await aliasLifecycle.start();
  assert(aliasGate.allowed, 'HC long-form identity must match persisted short-form identity');
  await aliasLifecycle.close();

  console.log('fan host lifecycle selftest: PASS (dark launch, gate, lease, restore, suspend/resume, close)');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
