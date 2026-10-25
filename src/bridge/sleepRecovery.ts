import { invoke, isNativeRuntime } from './ipc';

export type RecoveryCheck = { id: string; severity: 'warning' | 'info' | 'unknown'; detail: string };
export interface SleepRecoveryReport {
  ok: boolean;
  inspectionToken: string;
  expiresInMs: number;
  generation: number;
  healthVerified: false;
  state: { leaseCount: number; ownerPresent: boolean; markerLimit: boolean; inSuspend: boolean; gameSuspended: boolean; recoveryPending: boolean };
  games: { pid: number; created: string; identityMatches: boolean; threadsKnown: boolean; threadCount: number; suspendedThreads: number; allRunning: boolean }[];
  input: { epoch: number; alive: boolean; prepared: boolean; wanted: boolean; ready: boolean; quarantined: boolean; transportFault: boolean; faultHold: boolean; admissionRevoked: boolean; gameInput: 'unknown' };
  steam: { ok: boolean; reason?: string; runningAppIDs?: number[]; overlayEnabled?: boolean | null; controllers?: { index: number | null; type: number | null }[] | null; overviews?: { appid: number; overviewAvailable: boolean; name?: string; localDisplayStatus?: number | null }[]; gameInput?: 'unknown'; appBinding?: string };
  checks: RecoveryCheck[];
  log: { available: boolean; truncated: boolean; originalBytes?: number; copiedBytes?: number; reason?: string };
  repair: { allowed: boolean; reason: string; sleepTargets: number; inputRefresh: boolean; staleFlagReconcile: boolean };
}
export interface SleepRecoveryReceipt {
  ok: boolean; accepted: boolean; requestId?: string; gameQueued?: boolean; inputQueued?: boolean;
  flagsReconciled?: boolean; reason: string; healthVerified: false;
}
export interface SleepRecoveryFeedback {
  gameInput: 'unknown' | 'working' | 'broken';
  guide: 'unknown' | 'working' | 'broken';
  bigPicture: 'unknown' | 'continue' | 'start';
  overlay: 'unknown' | 'enabled' | 'disabled';
  note: string;
}
export function parseSleepRecoveryAppId(text: string): number | undefined {
  const clean = text.trim();
  if (!clean) return undefined;
  if (!/^\d+$/.test(clean)) throw new Error('请输入 1–4294967295 的 32 位 Steam AppID；不要填写 64 位快捷方式编号。');
  const value = Number(clean);
  if (!Number.isSafeInteger(value) || value <= 0 || value > 0xffffffff) throw new Error('AppID 超出 32 位范围。');
  return value;
}
function requireNative(): void {
  if (!isNativeRuntime) throw new Error('日志检查与恢复需要 YeManCC 原生程序，浏览器预览不会执行修复。');
}
export async function inspectSleepRecovery(appid?: number): Promise<SleepRecoveryReport> {
  requireNative();
  return invoke('sleepRecovery.inspect', { appid }, { timeoutMs: 30000 });
}
export async function repairSleepRecovery(inspectionToken: string): Promise<SleepRecoveryReceipt> {
  requireNative();
  return invoke('sleepRecovery.repair', { inspectionToken, confirmed: true }, { timeoutMs: 15000 });
}
export async function exportSleepRecoveryFeedback(feedback: SleepRecoveryFeedback): Promise<{ ok: boolean; path?: string; uploaded: false; reason?: string }> {
  requireNative();
  return invoke('sleepRecovery.exportFeedback', feedback, { timeoutMs: 60000 });
}


export interface AutomaticSleepRecoveryStatus {
  ok: boolean;
  enabled: boolean;
  phase: 'idle' | 'off' | 'waiting' | 'game-wait' | 'session-wait' | 'settle-wait' | 'inspecting' | 'confirm-wait' | 'verifying' | 'recorded' | 'blocked';
  generation: number;
  reason?: string;
  healthVerified: false;
  logPath?: string;
  scope?: 'steam';
  steamInputRecovery?: { status?: 'unknown' | 'context-current' | 'waiting-controller' | 'blocked' | 'stale-context'; reason?: string; contextMatched?: boolean; repairable?: boolean; gameInputVerified?: false } | null;
  steamRecovery?: { status?: 'unknown' | 'current' | 'blocked' | 'stale-ui'; reason?: string; associationVerified?: boolean; repairable?: boolean; appid?: number } | null;
}
export async function getAutomaticSleepRecovery(): Promise<AutomaticSleepRecoveryStatus> {
  requireNative();
  return invoke('sleepRecovery.autoGet', {}, { timeoutMs: 10000 });
}
export async function setAutomaticSleepRecovery(enabled: boolean): Promise<AutomaticSleepRecoveryStatus> {
  requireNative();
  return invoke('sleepRecovery.autoSet', { enabled }, { timeoutMs: 15000 });
}
