import { APP_VERSION } from '@/version';
import { invoke, isNativeRuntime, IpcTimeoutError } from '@/bridge/ipc';
import { PersistentFrontendErrorLog, sanitizeFrontendError, type FrontendErrorRecord } from './repairModel';

const STORAGE_KEY = 'yemancc.frontend-errors.v1';

let reporting = false;

// Fixed counters only. Snapshot construction happens when logs are exported.
// A rejected response/timeout never proves whether Native wrote the payload.
const deliveryChannels = ['frontend-error', 'gamepad-admission', 'controller-preview', 'controller-calibration', 'cpu-ccd-visibility', 'other-trace'] as const;
const deliveryOutcomes = ['native-not-ok', 'response-timeout', 'invoke-rejected', 'sender-exception'] as const;
type DeliveryChannel = typeof deliveryChannels[number];
type DeliveryOutcome = typeof deliveryOutcomes[number];
const deliveryCounts = deliveryChannels.map(() => deliveryOutcomes.map(() => 0));
let failedDiagnosticDeliveries = 0;
const MAX_DIAGNOSTIC_REQUESTS = 32;
const capacityRejectedByChannel = deliveryChannels.map(() => 0);
let inFlightRequests = 0;
const admission = { offeredRequests: 0, admittedRequests: 0, settledRequests: 0,
  capacityRejectedRequests: 0, highWaterRequests: 0 };
const addOne = (value: number): number => Math.min(Number.MAX_SAFE_INTEGER, value + 1);

// Four fixed counters only;no payload/stringification/sender work on input.
// A pass means only this completed session comparison,prior to all action gates.
const overlaySessionEntryCounts = [0, 0, 0, 0];
export function observeGamepadOverlaySessionEntry(active: boolean, rejected: boolean): void {
  const index = active ? (rejected ? 1 : 0) : (rejected ? 3 : 2);
  const value = overlaySessionEntryCounts[index];
  if (value < Number.MAX_SAFE_INTEGER) overlaySessionEntryCounts[index] = value + 1;
}

// Reserve before reading/stringifying a trace payload. The original bridge
// owns timeout and retirement. Capacity has no queue, retry or extra timer.
function sendDiagnostic(channel: DeliveryChannel, command: string, build: () => object): void {
  if (!isNativeRuntime) return;
  admission.offeredRequests = addOne(admission.offeredRequests);
  if (inFlightRequests >= MAX_DIAGNOSTIC_REQUESTS) {
    admission.capacityRejectedRequests = addOne(admission.capacityRejectedRequests);
    const index = deliveryChannels.indexOf(channel);
    capacityRejectedByChannel[index] = addOne(capacityRejectedByChannel[index]);
    failedDiagnosticDeliveries = addOne(failedDiagnosticDeliveries);
    return;
  }
  ++inFlightRequests;
  admission.highWaterRequests = Math.max(admission.highWaterRequests, inFlightRequests);
  admission.admittedRequests = addOne(admission.admittedRequests);
  const release = (): void => {
    --inFlightRequests;
    admission.settledRequests = addOne(admission.settledRequests);
  };
  try {
    const payload = build();
    void invoke<{ ok?: boolean }>(command, payload, { timeoutMs: 1000 }).then(result => {
      try { if (result?.ok !== true) recordDeliveryFailure(channel, 'native-not-ok'); }
      catch { recordDeliveryFailure(channel, 'sender-exception'); }
      finally { release(); }
    }, error => {
      try { recordInvokeFailure(channel, error); }
      finally { release(); }
    });
  } catch {
    recordDeliveryFailure(channel, 'sender-exception');
    release();
  }
}
function recordDeliveryFailure(channel: DeliveryChannel, outcome: DeliveryOutcome): void {
  if (failedDiagnosticDeliveries < Number.MAX_SAFE_INTEGER) ++failedDiagnosticDeliveries;
  const row = deliveryCounts[deliveryChannels.indexOf(channel)];
  const index = deliveryOutcomes.indexOf(outcome);
  if (row[index] < Number.MAX_SAFE_INTEGER) ++row[index];
}
function recordInvokeFailure(channel: DeliveryChannel, error: unknown): void {
  recordDeliveryFailure(channel, error instanceof IpcTimeoutError ? 'response-timeout' : 'invoke-rejected');
}
// A denied profile store must not prevent the Native diagnostic sender loading.
const log = new PersistentFrontendErrorLog((() => {
  try { return typeof window !== 'undefined' ? window.localStorage ?? null : null; }
  catch { recordDeliveryFailure('frontend-error', 'sender-exception'); return null; }
})(), STORAGE_KEY);

export function frontendDiagnosticDeliveryEvidence() {
  return {
    schema: 1, scope: 'current-renderer-module-lifetime;not-native-write-proof',
    failureCount: failedDiagnosticDeliveries, counterLimit: Number.MAX_SAFE_INTEGER,
    channels: deliveryChannels.map((channel, i) => ({ channel,
      counts: Object.fromEntries(deliveryOutcomes.map((outcome, j) => [outcome, deliveryCounts[i][j]])),
      capacityRejectedRequests: capacityRejectedByChannel[i],
    })),
    overlaySessionEntry: { schema: 1, counterLimit: Number.MAX_SAFE_INTEGER,
      scope: 'completed-renderer-native-ui-input-session-gate;module-lifetime',
      counts: { activePass: overlaySessionEntryCounts[0], activeReject: overlaySessionEntryCounts[1],
        inactivePass: overlaySessionEntryCounts[2], inactiveReject: overlaySessionEntryCounts[3] },
    },
    senderAdmission: { schema: 1, maxInFlightRequests: MAX_DIAGNOSTIC_REQUESTS,
      inFlightRequests, ...admission },
    snapshotAtomicWithNative: false, nativeWriteOutcome: 'UNKNOWN',
  };
}

function errorParts(error: unknown): { message: string; stack?: string } {
  try {
    if (error instanceof Error) {
      const message = error.message, stack = error.stack;
      if (typeof message !== 'string' || (stack !== undefined && typeof stack !== 'string')) throw new Error('Unreadable diagnostic error');
      return { message, stack };
    }
    return { message: String(error) };
  } catch {
    recordDeliveryFailure('frontend-error', 'sender-exception');
    return { message: 'frontend-error-text-unavailable' };
  }
}

export function reportFrontendError(where: string, error: unknown, extra: Partial<FrontendErrorRecord> = {}): void {
  if (reporting) return;
  reporting = true;
  try {
    const parts = errorParts(error);
    const record = sanitizeFrontendError({
      ts: Date.now(),
      where,
      message: parts.message,
      stack: parts.stack,
      route: typeof window !== 'undefined' ? window.location.hash : undefined,
      build: APP_VERSION,
      ...extra,
    });
    log.append(record);
    // Native owns the durable log in production so it survives a dead or
    // replaced WebView profile. A missing/old native command is expected in
    // browser-only development and must never recurse into error reporting.
    sendDiagnostic('frontend-error', 'diagnostics.frontendError', () => record);
  } catch {
    recordDeliveryFailure('frontend-error', 'sender-exception');
  } finally {
    reporting = false;
  }
}

export function readFrontendErrorLog(): FrontendErrorRecord[] {
  return log.read();
}

// Named state tracing is evidence, not an error and not a localStorage error entry.
// No timer/subscription/control action. Native owns the paths, caps and lane policy.
export function reportFrontendTrace(where: 'gamepad-admission' | 'controller-preview' | 'controller-calibration' | 'cpu-ccd-visibility', detail: Record<string, unknown>): void {
  const channel: DeliveryChannel = deliveryChannels.includes(where) ? where : 'other-trace';
  try {
    sendDiagnostic(channel, 'diagnostics.frontendTrace', () => ({
      where, message: JSON.stringify(detail),
      route: typeof window !== 'undefined' ? window.location?.hash ?? '' : '', build: APP_VERSION,
    }));
  } catch { recordDeliveryFailure(channel, 'sender-exception'); }
}
export function frontendDiagnosticDeliveryFailures(): number { return failedDiagnosticDeliveries; }
