import { APP_VERSION } from '@/version';
import { invoke, isNativeRuntime } from '@/bridge/ipc';
import { PersistentFrontendErrorLog, sanitizeFrontendError, type FrontendErrorRecord } from './repairModel';

const STORAGE_KEY = 'yemancc.frontend-errors.v1';
const log = new PersistentFrontendErrorLog(
  typeof window !== 'undefined' && window.localStorage ? window.localStorage : null,
  STORAGE_KEY,
);

let reporting = false;

function errorParts(error: unknown): { message: string; stack?: string } {
  if (error instanceof Error) return { message: error.message, stack: error.stack };
  return { message: String(error) };
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
    if (isNativeRuntime) {
      void invoke<{ ok?: boolean }>('diagnostics.frontendError', record, { timeoutMs: 1000 })
        .then(result => { if (result?.ok !== true) ++failedDiagnosticDeliveries; })
        .catch(() => { ++failedDiagnosticDeliveries; });
    }
  } finally {
    reporting = false;
  }
}

export function readFrontendErrorLog(): FrontendErrorRecord[] {
  return log.read();
}

// Named state tracing is evidence, not an error and not a localStorage error entry.
// No timer/subscription/control action. Native owns the paths, caps and lane policy.
let failedDiagnosticDeliveries = 0;
export function reportFrontendTrace(where: 'gamepad-admission' | 'controller-preview', detail: Record<string, unknown>): void {
  try {
    if (!isNativeRuntime) return;
    const record = { where, message: JSON.stringify(detail),
      route: typeof window !== 'undefined' ? window.location?.hash ?? '' : '', build: APP_VERSION };
    void invoke<{ ok?: boolean }>('diagnostics.frontendTrace', record, { timeoutMs: 1000 })
      .then(result => { if (result?.ok !== true) ++failedDiagnosticDeliveries; })
      .catch(() => { ++failedDiagnosticDeliveries; });
  } catch { ++failedDiagnosticDeliveries; }
}
export function frontendDiagnosticDeliveryFailures(): number { return failedDiagnosticDeliveries; }
