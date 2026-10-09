/** Inert adapter: no HC assembly, IPC, driver, network or device callbacks. */
import { FanApiError, type FanApiAdapter, type FanState, type FanLease } from '../../src/bridge/fanApi';
export class NoIoHost implements FanApiAdapter {
  readonly enabled = true;
  factoryType = 'HandheldCompanion.Devices.OneXPlayerX1Mini';
  calls: string[] = [];
  generation = 0;
  attempt = 0;
  stateName = 'AwaitingControl';
  powerState = 'Unknown';
  opened = false;
  events = false;
  closed = false;
  lease: FanLease | null = null;
  sequence = 0;
  resumeFailure = new FanApiError('no matching resume receipt', 409, 'HC_RESUME_RESULT_EXPIRED');
  completeResume = false;
  snapshot(): FanState { return { state: this.stateName, powerState: this.powerState, protocolVersion: '2',
    fanCapabilitySupported: true, fanControlCapabilityDeclared: false, fanCapabilityEvidence: 'hc-x1-inherited-ec-contract',
    hardwareWrites: false, hardwareWritesEnabled: !!this.lease, hardwareWritesObserved: false,
    unknownState: false, hcCloseCleanupPending: false, openCalled: this.opened, openEventsCalled: this.events, closeCalled: this.closed,
    resumePhase: this.stateName, resumePhaseGeneration: this.generation, powerOperationGeneration: this.generation,
    powerOperationAttempt: this.attempt, ...(this.attempt ? { powerOperationStatus: 'completed' } : {}),
    controlAccepting: ['Ready', 'AwaitingControl'].includes(this.stateName),
    lease: this.lease ? { ...this.lease } : null, leaseGeneration: this.lease?.generation ?? null }; }
  async handshake() { this.calls.push('handshake'); return { ok: true, supported: true, hardwareWritesEnabled: true,
    fanRoute: 'GenericDuty', fanRouteWriteReady: true, deviceClass: this.factoryType }; }
  async getState() { this.calls.push('state'); return this.snapshot(); }
  async open() { this.calls.push('open'); this.closed = false; this.opened = true; return this.snapshot(); }
  async openEvents() { this.calls.push('events'); this.events = true; return this.snapshot(); }
  async acquireControl() { this.calls.push('acquire'); this.lease = { leaseId: `lease-${++this.sequence}`, generation: this.sequence }; return { ...this.lease }; }
  async heartbeat() { this.calls.push('heartbeat'); return { ...this.lease! }; }
  async enable() { this.calls.push('enable'); this.stateName = 'Ready'; return this.snapshot(); }
  async applyPreset() { this.calls.push('preset'); this.stateName = 'Ready'; return this.snapshot(); }
  async disable() { this.calls.push('disable'); this.lease = null; this.stateName = 'AwaitingControl'; return this.snapshot(); }
  async restoreOem() { this.calls.push('restore'); this.stateName = 'Ready'; return { ...this.snapshot(), oemRestoreConfirmed: true }; }
  async releaseControl() { this.calls.push('release'); this.lease = null; return { ...this.snapshot(), oemRestoreConfirmed: true }; }
  async suspend() { this.calls.push('suspend'); this.stateName = 'Suspended'; this.powerState = 'Suspended'; this.opened = this.events = false; return this.snapshot(); }
  async resume(request: { generation: number }) { this.calls.push('resume'); if (!this.completeResume) throw this.resumeFailure;
    this.generation = request.generation; this.attempt += 1; this.stateName = 'AwaitingControl'; this.powerState = 'On';
    this.opened = this.events = true; return this.snapshot(); }
  async close() { this.calls.push('close'); this.closed = true; this.lease = null; this.stateName = 'Stopped'; this.opened = this.events = false;
    return { ...this.snapshot(), oemRestoreConfirmed: true, hcVirtualCloseReturned: true, hcDeviceManagerStopCompleted: true, hardwareWritesEnabled: false }; }
  async shutdown() { this.calls.push('shutdown'); }
}
