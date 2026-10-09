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
      void invoke('diagnostics.frontendError', record, { timeoutMs: 1000 }).catch(() => {});
    }
  } finally {
    reporting = false;
  }
}

export function readFrontendErrorLog(): FrontendErrorRecord[] {
  return log.read();
}
