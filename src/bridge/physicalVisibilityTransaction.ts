import type { ClosureState, InputPersona } from './inputContracts';

export type VisibilityTransactionState = 'unadmitted' | 'snapshot-captured' | 'hidden' | 'restore-required' | 'restored' | 'release-incomplete' | 'rejected';

/** Identity is supplied by the HC DeviceManager XUSB -> PnP mapping; never by a name or HID scan. */
export interface ResolvedHcPhysicalControllerV1 {
  schemaVersion: 1;
  source: 'hc-device-manager-xusb-pnp';
  controllerId: string;
  deviceInstanceId: string;
  baseContainerDeviceInstanceId: string;
  vid: number;
  pid: number;
  physicalIdentity: string;
}

export interface VirtualTargetIdentityV1 {
  targetId: string;
  persona: Exclude<InputPersona, 'disabled'>;
  deviceInstanceId: string | null;
  baseContainerDeviceInstanceId: string | null;
}

export interface VisibilityTransactionRequestV1 {
  schemaVersion: 1;
  runId: string;
  requestId: string;
  epoch: number;
  powerGeneration: number;
  owner: 'game-consumer' | 'ymcc-frontend' | 'none';
  targetId: string;
  persona: Exclude<InputPersona, 'disabled'>;
  configRevision: number;
  configHash: string;
  providerId: string | null;
  physical: ResolvedHcPhysicalControllerV1;
  calibrationId: string | null;
  visibilityTransactionId: string;
  timestampUtc: string;
  ymccAllowlist: readonly string[];
  virtualTarget: VirtualTargetIdentityV1;
}

export interface VisibilitySnapshotV1 {
  schemaVersion: 1;
  hiddenBefore: readonly string[];
  allowlistBefore: readonly string[];
  cloakingBefore: boolean;
}

export interface VisibilityTransactionTraceV1 {
  schemaVersion: 1;
  state: VisibilityTransactionState;
  event: string;
  closure: ClosureState;
  pXinput: 'UNENCLOSED';
  requestId: string;
  visibilityTransactionId: string;
  targetIds: readonly string[];
  virtualTargetExcluded: boolean;
  reason: string | null;
}

export interface VisibilityBackendV1 {
  captureSnapshot(): VisibilitySnapshotV1;
  ensureAllowlist(paths: readonly string[]): boolean;
  hidePath(path: string): boolean;
  unhidePath(path: string): boolean;
  cyclePort(path: string): boolean;
  /** Restore the complete pre-mutation HidHide state, not only YMCC's hide diff. */
  restoreSnapshot(snapshot: VisibilitySnapshotV1): boolean;
}

export interface VisibilityTransactionResultV1 {
  state: VisibilityTransactionState;
  closure: ClosureState;
  targetIds: readonly string[];
  virtualTargetExcluded: boolean;
  trace: readonly VisibilityTransactionTraceV1[];
}

function unique(ids: readonly string[]): string[] {
  return [...new Set(ids.map((id) => id.trim()).filter(Boolean))];
}

function requestTargets(request: VisibilityTransactionRequestV1): string[] {
  return unique([request.physical.baseContainerDeviceInstanceId, request.physical.deviceInstanceId]);
}

function targetIsVirtual(request: VisibilityTransactionRequestV1, targets: readonly string[]): boolean {
  const virtual = new Set(unique([request.virtualTarget.deviceInstanceId ?? '', request.virtualTarget.baseContainerDeviceInstanceId ?? '']));
  return targets.some((target) => virtual.has(target)) ||
    request.physical.deviceInstanceId === request.virtualTarget.deviceInstanceId ||
    request.physical.baseContainerDeviceInstanceId === request.virtualTarget.baseContainerDeviceInstanceId;
}

function validSnapshot(snapshot: VisibilitySnapshotV1): boolean {
  return snapshot.schemaVersion === 1 && Array.isArray(snapshot.hiddenBefore) && Array.isArray(snapshot.allowlistBefore) &&
    snapshot.hiddenBefore.every((path) => typeof path === 'string' && !!path.trim()) &&
    snapshot.allowlistBefore.every((path) => typeof path === 'string' && !!path.trim()) &&
    typeof snapshot.cloakingBefore === 'boolean';
}

function samePaths(left: readonly string[], right: readonly string[]): boolean {
  const normalize = (paths: readonly string[]) => unique(paths).map((path) => path.toUpperCase()).sort();
  return JSON.stringify(normalize(left)) === JSON.stringify(normalize(right));
}

function sameSnapshot(left: VisibilitySnapshotV1, right: VisibilitySnapshotV1): boolean {
  return left.schemaVersion === 1 && right.schemaVersion === 1 &&
    left.cloakingBefore === right.cloakingBefore &&
    samePaths(left.hiddenBefore, right.hiddenBefore) &&
    samePaths(left.allowlistBefore, right.allowlistBefore);
}

/**
 * T13 mock-only P-HID transaction. The plan follows HC IController.HideHID:
 * a selected physical controller's container + instance, then CyclePort. It
 * deliberately contains no ROG OEM Disable operation and always reports the
 * XInput plane as UNENCLOSED.
 */
export class PhysicalVisibilityTransaction {
  private state: VisibilityTransactionState = 'unadmitted';
  private request: VisibilityTransactionRequestV1 | null = null;
  private snapshot: VisibilitySnapshotV1 | null = null;
  private hiddenByTransaction: string[] = [];
  private readonly traces: VisibilityTransactionTraceV1[] = [];

  apply(request: VisibilityTransactionRequestV1, backend: VisibilityBackendV1): VisibilityTransactionResultV1 {
    const targets = requestTargets(request);
    // T13 has to run after Coordinator revoke + neutral and before its next
    // owner decision. Reusing a live/terminal journal would overwrite the
    // only restore proof, so each transaction object admits exactly once.
    if (this.state !== 'unadmitted') {
      this.record('request-rejected', targets, !targetIsVirtual(request, targets), 'visibility-transaction-reuse');
      return this.result(targets, !targetIsVirtual(request, targets));
    }
    const invalid = request.schemaVersion !== 1 || !request.runId || !request.requestId || !request.visibilityTransactionId || !request.targetId ||
      request.owner !== 'none' || !Number.isSafeInteger(request.epoch) || request.epoch < 0 ||
      !Number.isSafeInteger(request.powerGeneration) || request.powerGeneration < 0 ||
      !Number.isSafeInteger(request.configRevision) || request.configRevision < 0 ||
      !request.configHash.startsWith('sha256:') || request.physical.schemaVersion !== 1 || request.physical.source !== 'hc-device-manager-xusb-pnp' ||
      !request.physical.controllerId || !request.physical.physicalIdentity || !request.physical.deviceInstanceId || !request.physical.baseContainerDeviceInstanceId ||
      request.virtualTarget.targetId !== request.targetId || request.virtualTarget.persona !== request.persona ||
      targets.length !== 2 || !request.ymccAllowlist.length;
    const virtualTargetExcluded = !targetIsVirtual(request, targets);
    if (invalid || !virtualTargetExcluded) {
      this.state = 'rejected';
      this.request = request;
      this.record('request-rejected', targets, virtualTargetExcluded, invalid ? 'identity-owner-or-allowlist-invalid' : 'virtual-target-in-hide-list');
      return this.result(targets, virtualTargetExcluded);
    }
    this.request = request;
    const snapshot = backend.captureSnapshot();
    if (!validSnapshot(snapshot)) {
      this.state = 'rejected';
      this.record('snapshot-rejected', targets, true, 'visibility-snapshot-invalid');
      return this.result(targets, true);
    }
    this.snapshot = {
      schemaVersion: 1,
      hiddenBefore: [...snapshot.hiddenBefore],
      allowlistBefore: [...snapshot.allowlistBefore],
      cloakingBefore: snapshot.cloakingBefore,
    };
    this.hiddenByTransaction = [];
    this.state = 'snapshot-captured';
    this.record('snapshot-captured', targets, true, null);

    if (!backend.ensureAllowlist(request.ymccAllowlist)) {
      // Allowlist failure may be a partial mutation. Restore the complete
      // before snapshot rather than calling it a harmless rejected request.
      this.state = 'restore-required';
      this.record('allowlist-failed', targets, true, 'ymcc-allowlist-not-confirmed');
      return this.restore(backend);
    }
    for (const target of targets) {
      if (!backend.hidePath(target)) {
        this.state = 'restore-required';
        this.record('hide-failed', targets, true, `hide-failed:${target}`);
        this.restore(backend);
        return this.result(targets, true);
      }
      this.hiddenByTransaction.push(target);
    }
    for (const target of targets) {
      if (!backend.cyclePort(target)) {
        this.state = 'restore-required';
        this.record('cycle-failed', targets, true, `cycle-failed:${target}`);
        this.restore(backend);
        return this.result(targets, true);
      }
    }
    this.state = 'hidden';
    this.record('hide-and-cycle-complete', targets, true, null);
    return this.result(targets, true);
  }

  restore(backend: VisibilityBackendV1): VisibilityTransactionResultV1 {
    const request = this.request;
    const targets = request ? requestTargets(request) : [];
    const virtualTargetExcluded = request ? !targetIsVirtual(request, targets) : true;
    // A restore receipt without an admitted request + pre-mutation journal is
    // not evidence that anything was released. Fail closed instead of turning
    // an unbound/no-op call into a misleading `restored` result.
    if (!request || !this.snapshot) {
      this.state = 'release-incomplete';
      this.record('restore-journal-missing', targets, virtualTargetExcluded, 'visibility-journal-missing');
      return this.result(targets, virtualTargetExcluded);
    }
    let complete = true;
    // Match the HC helper's container -> instance UnhideHID order; only after
    // all paths have been restored may the PnP cycle be requested.
    for (const target of this.hiddenByTransaction) {
      complete = backend.unhidePath(target) && complete;
    }
    for (const target of this.hiddenByTransaction) {
      complete = backend.cyclePort(target) && complete;
    }
    // The HC helper supplies the container -> instance ordering above. The
    // project T13 contract is intentionally stricter: a transaction cannot
    // claim release until allowlist, cloak and every pre-existing hidden path
    // have been restored and independently read back as the before snapshot.
    complete = backend.restoreSnapshot(this.snapshot) && complete;
    let after: VisibilitySnapshotV1 | null = null;
    try {
      const candidate = backend.captureSnapshot();
      if (validSnapshot(candidate)) after = candidate;
    } catch { }
    complete = after !== null && sameSnapshot(after, this.snapshot) && complete;
    if (complete) this.hiddenByTransaction = [];
    this.state = complete ? 'restored' : 'release-incomplete';
    this.record(complete ? 'restore-complete' : 'restore-incomplete', targets, virtualTargetExcluded, complete ? null : 'unhide-cycle-snapshot-restore-or-verify-failed');
    return this.result(targets, virtualTargetExcluded);
  }

  trace(): readonly VisibilityTransactionTraceV1[] {
    return this.traces.map((event) => ({ ...event, targetIds: [...event.targetIds] }));
  }

  private result(targetIds: readonly string[], virtualTargetExcluded: boolean): VisibilityTransactionResultV1 {
    return {
      state: this.state,
      closure: this.state === 'hidden' || this.state === 'restored' ? 'NOT-OBSERVED' : 'UNENCLOSED',
      targetIds: [...targetIds], virtualTargetExcluded,
      trace: this.trace(),
    };
  }

  private record(event: string, targetIds: readonly string[], virtualTargetExcluded: boolean, reason: string | null): void {
    const request = this.request;
    this.traces.push({
      schemaVersion: 1, state: this.state, event,
      closure: this.state === 'hidden' || this.state === 'restored' ? 'NOT-OBSERVED' : 'UNENCLOSED',
      pXinput: 'UNENCLOSED', requestId: request?.requestId ?? 'unbound', visibilityTransactionId: request?.visibilityTransactionId ?? 'unbound',
      targetIds: [...targetIds], virtualTargetExcluded, reason,
    });
  }
}
