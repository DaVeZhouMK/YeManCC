// Static operation snapshots only: no subscriptions, timers or hardware probes.
import { invoke } from './ipc';
export type TdpRequestSource = 'ui' | 'decky' | 'hotkey' | 'schedule' | 'startup' | 'resume' | 'autofloat' | 'unspecified';
type TdpLifecycleStep = 'power-write-gate' | 'config-saved' | 'daemon-write-receipt' | 'completion-currentness';
export interface TdpObservationContext {
  operationId: string; source: TdpRequestSource; requestedW: number | null; clampedW: number | null;
  requestedMode: 'ac' | 'dc' | null; observedMode: 'ac' | 'dc' | null; apply: boolean; save: boolean; vendor?: string; configSaved?: boolean; policy?: string;
}
let sequence = 0;
let failedLogDeliveries = 0;
const MAX_DIAGNOSTIC_REQUESTS = 32;
let inFlightRequests = 0, inFlightRows = 0;
const delivery = { offeredRequests: 0, offeredRows: 0, admittedRequests: 0, admittedRows: 0,
  ackAcceptedRequests: 0, ackAcceptedRows: 0, nativeNotOkRequests: 0, nativeNotOkRows: 0,
  unrecognizedReplyRequests: 0, unrecognizedReplyRows: 0, invokeRejectedRequests: 0, invokeRejectedRows: 0,
  capacityRejectedRequests: 0, capacityRejectedRows: 0, senderExceptionRows: 0, highWaterRequests: 0 };
const add = (value: number, n: number): number => Math.min(Number.MAX_SAFE_INTEGER, value + n);
function failedRows(n: number): void { failedLogDeliveries = add(failedLogDeliveries, n); }
function sendDiagnostic(rows: Array<{ event: string; detail: Record<string, unknown> }>): void {
  const n = rows.length;
  delivery.offeredRequests = add(delivery.offeredRequests, 1); delivery.offeredRows = add(delivery.offeredRows, n);
  if (inFlightRequests >= MAX_DIAGNOSTIC_REQUESTS) {
    delivery.capacityRejectedRequests = add(delivery.capacityRejectedRequests, 1);
    delivery.capacityRejectedRows = add(delivery.capacityRejectedRows, n); failedRows(n); return;
  }
  ++inFlightRequests; inFlightRows += n;
  delivery.highWaterRequests = Math.max(delivery.highWaterRequests, inFlightRequests);
  delivery.admittedRequests = add(delivery.admittedRequests, 1); delivery.admittedRows = add(delivery.admittedRows, n);
  const release = (): void => { --inFlightRequests; inFlightRows -= n; };
  try {
    const payload = n === 1 ? rows[0] : { events: rows };
    void invoke<{ ok?: boolean }>('tdp.observe', payload).then(result => {
      try {
        if (result?.ok === true) {
          delivery.ackAcceptedRequests = add(delivery.ackAcceptedRequests, 1); delivery.ackAcceptedRows = add(delivery.ackAcceptedRows, n);
        } else if (result?.ok === false) {
          delivery.nativeNotOkRequests = add(delivery.nativeNotOkRequests, 1); delivery.nativeNotOkRows = add(delivery.nativeNotOkRows, n); failedRows(n);
        } else {
          delivery.unrecognizedReplyRequests = add(delivery.unrecognizedReplyRequests, 1); delivery.unrecognizedReplyRows = add(delivery.unrecognizedReplyRows, n); failedRows(n);
        }
      } catch { delivery.senderExceptionRows = add(delivery.senderExceptionRows, n); failedRows(n); }
      finally { release(); }
    }, () => {
      delivery.invokeRejectedRequests = add(delivery.invokeRejectedRequests, 1); delivery.invokeRejectedRows = add(delivery.invokeRejectedRows, n); failedRows(n); release();
    });
  } catch { delivery.senderExceptionRows = add(delivery.senderExceptionRows, n); failedRows(n); release(); }
}
export function tdpDiagnosticDeliveryEvidence() {
  return { schema: 1, scope: 'current-renderer-module-diagnostic-sender', maxInFlightRequests: MAX_DIAGNOSTIC_REQUESTS,
    maxRowsPerRequest: 16, inFlightRequests, inFlightRows, unconfirmedRows: failedLogDeliveries, ...delivery,
    nativeWriteOutcome: 'UNKNOWN', requestRecoveryTriggered: false, snapshotAtomicWithNative: false };
}
export function beginTdpObservation(detail: Omit<TdpObservationContext, 'operationId'>) {
  const started = performance.now();
  const context: TdpObservationContext = { ...detail, operationId: `tdp:${Date.now()}:${++sequence}` };
  const state: Record<string, unknown> = { configSaved: false, processStarted: 'not-observed', hardwareCommandReturned: 'not-observed', readbackMatched: 'not-observed' };
  // Keep the initial request immediately observable, then deliver bounded
  // causal rows together at completion. No timer, observer or background work.
  const pending: Array<{ event: string; detail: Record<string, unknown> }> = [];
  const deliver = sendDiagnostic;
  const emit = (event: string, fields: Record<string, unknown> = {}, lifecycleStep?: TdpLifecycleStep): void => {
    try {
      Object.assign(state, fields);
      if (typeof fields.configSaved === 'boolean') context.configSaved = fields.configSaved;
      const row = { event, detail: { ...context, ...state, durationMs: performance.now() - started } };
      if (lifecycleStep) Object.assign(row.detail, { lifecycleStep });
      if (event === 'tdp.request') { deliver([row]); return; }
      pending.push(row);
      if (pending.length >= 16 || ['tdp.completed', 'tdp.failed', 'tdp.skipped', 'tdp.unverified'].includes(event))
        deliver(pending.splice(0));
    } catch { delivery.senderExceptionRows = add(delivery.senderExceptionRows, 1); failedRows(1); }
  };
  emit('tdp.request', { configSaved: false, processStarted: false, hardwareCommandReturned: 'not-observed', readbackMatched: 'not-observed' });
  const emitLifecycle = (step: TdpLifecycleStep, fields: Record<string, unknown>): void => emit('tdp.lifecycle', fields, step);
  return { context, state, emit, emitLifecycle };
}
export function tdpDiagnosticDeliveryFailures(): number { return failedLogDeliveries; }
// Operator annotation is a marker, not an OEM mode request or a hardware fact.
export function markTdpOperatorMode(marker: 'MSI Center M 智能模式' | 'YMCC 自动优化'): void {
  try { sendDiagnostic([{ event: 'tdp.operator-marker', detail: { marker, source: 'ui' } }]); }
  catch { delivery.senderExceptionRows = add(delivery.senderExceptionRows, 1); failedRows(1); }
}

export function tdpErrorText(error: unknown): string {
  try { return error instanceof Error ? error.message : String(error); } catch { return 'error-text-unavailable'; }
}
