import { invoke } from './ipc';

export const CURRENT_DIAGNOSTIC_LOG_FILES = [
  'gyro-motion.log', 'virtual-gamepad.log', 'focus-trace.log',
  'app.log', 'native-lifecycle.log', 'native-detail.log',
  'fan-api.log', 'fan-lifecycle.log', 'hardware-coordinator.log', 'sleep-facts.log',
  'tdp.log', 'tdp-detail.log', 'frontend-errors.log', 'frontend-trace.log',
  'frontend-trace-detail.log', 'webview-failures.log',
] as const;
export const DIAGNOSTIC_LOG_DELETE_SCOPE = '仅本进程的应用、原生事件/明细、风扇/协调、陀螺仪/虚拟手柄/焦点、睡眠事实、TDP和界面当前日志（16项）';
export const DIAGNOSTIC_LOG_PRESERVED_SCOPE = 'InputHost/FanHost等外部宿主、监控明细、独立输入录制、Steam/侧边栏及其他独立日志保留；历史轮转、导出证据和配置不删除';

export interface DiagnosticLogGates { gyro: boolean; virtual: boolean }
export interface DiagnosticLogMaintenanceReceipt {
  protocol: 'ymcc.current-diagnostic-logs.v1';
  ok: boolean;
  busy: boolean;
  captured: boolean;
  pauseAcknowledged: boolean;
  deleteAttempted: boolean;
  restored: boolean;
  originalGates: DiagnosticLogGates;
  restoredGates: DiagnosticLogGates;
  files: Array<{ name: string; state: 'deleted' | 'missing' | 'error'; error: string }>;
  pauseError: string;
  deleteError: string;
  restoreError: string;
}
export interface DiagnosticLogMaintenanceOutcome {
  ok: boolean;
  status: string;
  error: string;
  /** Refresh the parent's existing gate owner, including after uncertain IPC results. */
  changed: boolean;
  receipt?: DiagnosticLogMaintenanceReceipt;
}
export type DiagnosticLogMaintenanceTransport = (
  command: string, args: object, options: { timeoutMs: number },
) => Promise<unknown>;

function isObject(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}
function isGates(value: unknown): value is DiagnosticLogGates {
  return isObject(value) && typeof value.gyro === 'boolean' && typeof value.virtual === 'boolean';
}
function sameGates(a: DiagnosticLogGates, b: DiagnosticLogGates): boolean {
  return a.gyro === b.gyro && a.virtual === b.virtual;
}
function readReceipt(value: unknown): DiagnosticLogMaintenanceReceipt {
  const boolKeys = ['ok', 'busy', 'captured', 'pauseAcknowledged', 'deleteAttempted', 'restored'];
  const textKeys = ['pauseError', 'deleteError', 'restoreError'];
  if (!isObject(value) || value.protocol !== 'ymcc.current-diagnostic-logs.v1' ||
      boolKeys.some(key => typeof value[key] !== 'boolean') ||
      textKeys.some(key => typeof value[key] !== 'string') ||
      !isGates(value.originalGates) || !isGates(value.restoredGates) || !Array.isArray(value.files)) {
    throw new Error('原生删除回执不完整，无法确认删除及恢复结果');
  }
  const receipt = value as unknown as DiagnosticLogMaintenanceReceipt;
  const names = new Set<string>();
  for (const file of receipt.files) {
    if (!isObject(file) || !(CURRENT_DIAGNOSTIC_LOG_FILES as readonly unknown[]).includes(file.name) ||
        names.has(file.name) || !['deleted', 'missing', 'error'].includes(file.state) || typeof file.error !== 'string') {
      throw new Error('原生删除回执的文件范围无法确认');
    }
    names.add(file.name);
  }
  if ((receipt.pauseAcknowledged && !receipt.captured) ||
      (receipt.deleteAttempted && !receipt.pauseAcknowledged) ||
      (receipt.files.length > 0 && !receipt.deleteAttempted) ||
      (receipt.restored && (!receipt.captured || !sameGates(receipt.originalGates, receipt.restoredGates))) ||
      (receipt.busy && (receipt.captured || receipt.deleteAttempted || receipt.files.length > 0))) {
    throw new Error('原生暂停、删除、恢复顺序回执不一致');
  }
  const success = receipt.captured && receipt.pauseAcknowledged && receipt.deleteAttempted && receipt.restored &&
    names.size === CURRENT_DIAGNOSTIC_LOG_FILES.length && receipt.files.every(file => file.state !== 'error') &&
    !receipt.pauseError && !receipt.deleteError && !receipt.restoreError && !receipt.busy;
  if (receipt.ok !== success) throw new Error('原生删除结果与逐项回执不一致');
  return receipt;
}

function describeReceipt(receipt: DiagnosticLogMaintenanceReceipt): DiagnosticLogMaintenanceOutcome {
  if (receipt.busy) return { ok: false, status: '', error: '另一项日志删除仍在处理中，请稍后重试', changed: false, receipt };
  const errors: string[] = [];
  if (!receipt.pauseAcknowledged) errors.push('暂停确认失败，未删除日志：' + (receipt.pauseError || '未取得停止写入确认'));
  else if (!receipt.deleteAttempted || receipt.deleteError || receipt.files.length !== CURRENT_DIAGNOSTIC_LOG_FILES.length) {
    errors.push('删除失败：' + (receipt.deleteError || '当前文件删除未完成'));
  }
  if (receipt.captured && !receipt.restored) errors.push('恢复原日志开关失败：' + (receipt.restoreError || '未取得恢复确认'));
  const deleted = receipt.files.filter(file => file.state === 'deleted').length;
  const restoration = receipt.restored
    ? '原日志开关已恢复（陀螺仪' + (receipt.originalGates.gyro ? '开' : '关') + '、虚拟手柄' + (receipt.originalGates.virtual ? '开' : '关') + '）'
    : receipt.captured ? '原日志开关恢复未确认' : '未能保存原日志开关，未执行删除';
  const deletion = receipt.pauseAcknowledged && receipt.deleteAttempted
    ? '已删除 ' + deleted + ' 个当前日志文件' + (deleted === 0 && receipt.ok ? '（文件原本不存在）' : '')
    : '未删除日志';
  return { ok: receipt.ok, status: deletion + '；' + restoration + '。' + DIAGNOSTIC_LOG_DELETE_SCOPE + '；' + DIAGNOSTIC_LOG_PRESERVED_SCOPE + '。',
    error: errors.join('；'), changed: receipt.captured, receipt };
}

let inFlight = false;
export function isDiagnosticLogMaintenanceBusy(): boolean { return inFlight; }

/** One native transaction owns capture -> stop -> ack -> clear -> restore.
 * Never fall back to log.clear/domainClear or retry an uncertain destructive IPC.
 * The native owner restores even if this component or renderer disappears.
 */
export async function deleteCurrentDiagnosticLogs(
  transport: DiagnosticLogMaintenanceTransport = invoke,
): Promise<DiagnosticLogMaintenanceOutcome> {
  if (inFlight) return { ok: false, status: '', error: '日志删除正在处理中，请勿重复操作', changed: false };
  inFlight = true;
  try {
    return describeReceipt(readReceipt(await transport('logs.deleteCurrent', {}, { timeoutMs: 30000 })));
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return { ok: false, status: '删除及原开关恢复结果未能确认，请刷新高频日志状态。',
      error: '日志删除未获确认：' + message, changed: true };
  } finally { inFlight = false; }
}
