import { ref } from 'vue';
import { invoke, isNativeRuntime } from './ipc';

/** Fan-only diagnostics. Disabled by default and isolated from app.log. */
export const fanDiagnosticLoggingEnabled = ref(false);
export const fanDiagnosticLogPath = ref<string | null>(null);

/** Read the current native log path without changing the logging switch. */
export async function getFanDiagnosticLogPath(): Promise<string | null> {
  if (!isNativeRuntime) return fanDiagnosticLogPath.value;
  try {
    const path = await invoke<string | null>('fanLog.getPath');
    fanDiagnosticLogPath.value = typeof path === 'string' && path.trim() ? path : null;
  } catch {
    // Older shells may not expose the optional path query. Logging itself
    // remains independent and the UI can show its documented fallback path.
  }
  return fanDiagnosticLogPath.value;
}

export async function clearFanDiagnosticLogs(): Promise<boolean> {
  if (!isNativeRuntime) return false;
  const result = await invoke<{ ok?: boolean }>('fanLog.clear');
  return result.ok === true;
}

export interface FanDiagnosticExportResult {
  ok: boolean;
  path?: string;
  files?: string[];
  reason?: string;
}

export async function exportFanDiagnosticLogs(): Promise<FanDiagnosticExportResult> {
  if (!isNativeRuntime) return { ok: false, reason: '当前为预览环境' };
  return invoke<FanDiagnosticExportResult>('fanLog.export');
}

function sanitize(value: unknown, key = ''): unknown {
  if (/(token|lease.?id|authorization|session|password|secret)/i.test(key)) return '[redacted]';
  if (value === null || value === undefined) return value;
  if (typeof value === 'string') {
    return value.replace(/(X-YeMan-Fan-Session|sessionToken|leaseId)\s*[:=]\s*[^,\s}]+/gi, '$1=[redacted]');
  }
  if (Array.isArray(value)) return value.map((item) => sanitize(item, key));
  if (typeof value === 'object') {
    return Object.fromEntries(Object.entries(value as Record<string, unknown>)
      .map(([childKey, childValue]) => [childKey, sanitize(childValue, childKey)]));
  }
  return value;
}

export async function configureFanDiagnosticLogging(enabled: boolean): Promise<string | null> {
  if (isNativeRuntime) {
    try {
      const result = await invoke<{ enabled: boolean; path?: string | null }>('fanLog.setEnabled', { enabled });
      if (result.enabled !== enabled) throw new Error('风扇诊断日志开关未能应用');
      fanDiagnosticLogPath.value = result.path ?? null;
    } catch (error) {
      // An older native shell may not know the optional diagnostics command.
      // Keeping logging disabled remains safe and must not hide the Fan page.
      if (enabled) throw error;
      fanDiagnosticLogPath.value = null;
    }
  }
  fanDiagnosticLoggingEnabled.value = enabled;
  return fanDiagnosticLogPath.value;
}

export function fanDiagnosticLog(event: string, details?: unknown): void {
  if (!fanDiagnosticLoggingEnabled.value || !event || !isNativeRuntime) return;
  void invoke('fanLog.write', { event, details: sanitize(details ?? {}) }).catch(() => {});
}
