// Static operation snapshots only: no subscriptions, timers or hardware probes.
import { invoke } from './ipc';
export type TdpRequestSource = 'ui' | 'decky' | 'hotkey' | 'schedule' | 'startup' | 'resume' | 'autofloat' | 'unspecified';
export interface TdpObservationContext {
  operationId: string; source: TdpRequestSource; requestedW: number | null; clampedW: number | null;
  requestedMode: 'ac' | 'dc' | null; observedMode: 'ac' | 'dc' | null; apply: boolean; save: boolean; vendor?: string; configSaved?: boolean; policy?: string;
}
let sequence = 0;
let failedLogDeliveries = 0;
export function beginTdpObservation(detail: Omit<TdpObservationContext, 'operationId'>) {
  const started = performance.now();
  const context: TdpObservationContext = { ...detail, operationId: `tdp:${Date.now()}:${++sequence}` };
  const state: Record<string, unknown> = { configSaved: false, processStarted: 'not-observed', hardwareCommandReturned: 'not-observed', readbackMatched: 'not-observed' };
  const emit = (event: string, fields: Record<string, unknown> = {}): void => {
    try {
      Object.assign(state, fields);
      if (typeof fields.configSaved === 'boolean') context.configSaved = fields.configSaved;
      void invoke('tdp.observe', { event, detail: { ...context, ...state, durationMs: performance.now() - started } })
        .catch(() => { ++failedLogDeliveries; });
    } catch { ++failedLogDeliveries; }
  };
  emit('tdp.request', { configSaved: false, processStarted: false, hardwareCommandReturned: 'not-observed', readbackMatched: 'not-observed' });
  return { context, state, emit };
}
export function tdpDiagnosticDeliveryFailures(): number { return failedLogDeliveries; }
// Operator annotation is a marker, not an OEM mode request or a hardware fact.
export function markTdpOperatorMode(marker: 'MSI Center M 智能模式' | 'YMCC 自动优化'): void {
  try { void invoke('tdp.observe', { event: 'tdp.operator-marker', detail: { marker, source: 'ui' } }).catch(() => { ++failedLogDeliveries; }); }
  catch { ++failedLogDeliveries; }
}

export function tdpErrorText(error: unknown): string {
  try { return error instanceof Error ? error.message : String(error); } catch { return 'error-text-unavailable'; }
}
