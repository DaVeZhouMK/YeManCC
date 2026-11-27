import { fs } from './api';
import { invoke, IpcPostError, IpcRemoteError, on } from './ipc';
import { detectGame, getCurrentGame, isCurrentGameTarget, type GameTargetIdentity } from './gamedetect';
import { getSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, onSharedPowerLifecycle, sharedPowerWriteScope } from './powerLifecycleState';

export interface GameCtlResult {
  ok: boolean;
  okCount: number;
  failCount: number;
  msgs: string[];
}

interface SuspendedProcessIdentity {
  pid: number;
  processCreated?: string;
}

const SUSPEND_STATE = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\quickapp_suspended.json';
const manualPauseMarkerPath = (pid: number) => `C:\\SOFT\\YeMan\\PowerControl\\Sleep\\manual-suspended\\${pid}.txt`;
const PAUSE_RECORD_READ_TIMEOUT_MS = 5000;
const MANUAL_PAUSE_TIMEOUT_MS = 8000;
const RESUME_RECONCILE_DELAYS_MS = [500, 1500, 4000] as const;
export const QUICKAPP_SUSPENDED_EVENT = 'quickapp:suspended-state';

function emitSuspendedState(suspended?: boolean): void {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent(QUICKAPP_SUSPENDED_EVENT, { detail: suspended === undefined ? undefined : { suspended } }));
  }
}

let controlQueue = Promise.resolve();

function withControlLock<T>(operation: () => Promise<T>): Promise<T> {
  const next = controlQueue.then(operation, operation);
  controlQueue = next.then(() => undefined, () => undefined);
  return next;
}

type GameControlTarget = GameTargetIdentity & { path: string; identityGeneration: number };
function captureGameControlTarget(pid: number, birth: string | undefined, expected?: GameTargetIdentity): GameControlTarget | null {
  const target = expected ?? getCurrentGame();
  if (!target || target.pid !== pid || !target.path || !target.identityGeneration ||
      !Number.isSafeInteger(target.identityGeneration) || target.identityGeneration <= 0 ||
      !target.processCreated || birth !== undefined && target.processCreated !== birth || !isCurrentGameTarget(target)) return null;
  return { pid: target.pid, processCreated: target.processCreated, path: target.path, identityGeneration: target.identityGeneration };
}
function staleGameControl(): GameCtlResult {
  return { ok: false, okCount: 0, failCount: 1, msgs: ['游戏目标已变化或暂未确认，本次操作已取消，请重试。'] };
}

type ResumeResult = { resumed?: number; failedPids?: number[]; stalePids?: number[]; recoveryPending?: boolean;
  confirmationPendingPids?: number[] };
function isResumeResult(value: ResumeResult | undefined): value is ResumeResult {
  return !!value && Array.isArray(value.failedPids) && Array.isArray(value.stalePids) && Number.isInteger(value.resumed);
}
type ResumeOwner = {
  id: string;
  powerRevision: number;
  raw: string | undefined;
  settle: (result: ResumeResult) => ResumeSettlement;
  settlement?: ResumeSettlement;
  observeAgain?: boolean;
  observing?: boolean;
  statusRead?: AbortController;
  reconcileTimer?: ReturnType<typeof setTimeout>;
  receiptReads?: number;
  settlementRetries?: number;
  stop: () => void;
};
type ResumeSettlement = { result: GameCtlResult; record?: { raw: string | null; suspended: boolean }; refreshSuspendedState?: boolean };
let pendingResume: ResumeOwner | null = null;

function unsettledResume(message = '继续游戏的结果尚未确认，已保留暂停记录'): GameCtlResult {
  return { ok: false, okCount: 0, failCount: 1, msgs: [message] };
}

function releaseResume(owner: ResumeOwner): void {
  if (pendingResume === owner) pendingResume = null;
  owner.statusRead?.abort();
  cancelResumeReconcile(owner);
  owner.stop();
}

function resumePowerScope(power = getSharedPowerLifecycle()): string | undefined {
  return sharedPowerWriteScope(power)?.join(':');
}

function cancelResumeReconcile(owner: ResumeOwner): void {
  if (owner.reconcileTimer === undefined) return;
  clearTimeout(owner.reconcileTimer);
  owner.reconcileTimer = undefined;
}

function scheduleResumeReconcile(owner: ResumeOwner): void {
  if (pendingResume !== owner || owner.reconcileTimer !== undefined) return;
  const scope = resumePowerScope();
  const stage = owner.settlement ? 'settlementRetries' : 'receiptReads';
  const attempt = owner[stage] ?? 0;
  const delay = RESUME_RECONCILE_DELAYS_MS[attempt];
  if (!scope || delay === undefined) return;
  // Retry only the receipt read or the original compare-and-set, never game.resume.
  owner.reconcileTimer = setTimeout(() => {
    owner.reconcileTimer = undefined;
    if (pendingResume !== owner || resumePowerScope() !== scope) return;
    owner[stage] = attempt + 1;
    reconcilePendingGameResume(owner);
  }, delay);
}

async function settlePauseRecord(expected: string | undefined, raw: string | null): Promise<string | undefined> {
  return invoke<string>('game.pauseRecord.settle', { expected: expected ?? null, raw },
    { timeoutMs: PAUSE_RECORD_READ_TIMEOUT_MS }).catch(() => undefined);
}

async function finishPendingResume(owner: ResumeOwner): Promise<GameCtlResult> {
  const { result, record, refreshSuspendedState } = owner.settlement!;
  if (record) {
    const power = getSharedPowerLifecycle();
    if (power && !isSharedPowerLifecycleAdmitted(power)) return unsettledResume();
    const revision = owner.powerRevision;
    const commit = await settlePauseRecord(owner.raw, record.raw);
    // Native may have completed before revocation. Keep the same CAS for
    // readback under the new scope rather than publishing an old menu state.
    if (pendingResume !== owner || revision !== owner.powerRevision) return unsettledResume();
    if (commit === 'changed') {
      releaseResume(owner);
      // The saved pause changed elsewhere; ask the menu to reread its current target.
      emitSuspendedState();
      return unsettledResume('暂停记录已变化，已保留当前记录');
    }
    if (commit !== 'committed') {
      scheduleResumeReconcile(owner);
      return unsettledResume('游戏恢复已返回，暂停记录尚未确认');
    }
  }
  releaseResume(owner);
  if (record) emitSuspendedState(record.suspended);
  else if (refreshSuspendedState) emitSuspendedState(false);
  return result;
}

async function readPendingResume(owner: ResumeOwner): Promise<GameCtlResult> {
  const power = getSharedPowerLifecycle();
  if (pendingResume !== owner || (power && !isSharedPowerLifecycleAdmitted(power))) return unsettledResume();
  cancelResumeReconcile(owner);
  if (owner.settlement) return finishPendingResume(owner);
  const revision = owner.powerRevision;
  const read = new AbortController();
  owner.statusRead = read;
  const status = await invoke<{ requestId?: string; state?: string; result?: ResumeResult; error?: string }>(
    'game.resume.status', { requestId: owner.id }, { timeoutMs: 2000, signal: read.signal }).catch(() => null);
  if (owner.statusRead === read) owner.statusRead = undefined;
  if (pendingResume !== owner || revision !== owner.powerRevision || read.signal.aborted) return unsettledResume();
  if (status?.requestId !== owner.id || status.state === 'queued' || status.state === 'running') {
    scheduleResumeReconcile(owner);
    return unsettledResume();
  }
  if (status.state === 'unknown') {
    releaseResume(owner);
    return unsettledResume('原恢复结果不可用，已保留暂停记录，可重新确认后重试');
  }
  if (status.state !== 'completed') {
    scheduleResumeReconcile(owner);
    return unsettledResume();
  }
  if (typeof status.error === 'string') {
    releaseResume(owner);
    return unsettledResume(status.error);
  }
  const result = status.result;
  if (!isResumeResult(result)) {
    scheduleResumeReconcile(owner);
    return unsettledResume();
  }
  owner.settlement = owner.settle(result);
  return finishPendingResume(owner);
}

async function reconcileBeforeOtherGameControl(owner: ResumeOwner): Promise<GameCtlResult> {
  if (!resumePowerScope()) return unsettledResume();
  // The menu can already show the new disk state while the old CAS reply is missing.
  const result = await readPendingResume(owner);
  return unsettledResume(result.ok ? '上次继续游戏的结果已确认，请再次操作' : result.msgs.join('；'));
}

function reconcilePendingGameResume(owner: ResumeOwner): void {
  if (pendingResume !== owner || !resumePowerScope()) return;
  owner.observeAgain = true;
  if (owner.observing) return;
  owner.observing = true;
  void withControlLock(async () => {
    if (pendingResume !== owner) return;
    owner.observeAgain = false;
    if (!resumePowerScope()) return;
    await readPendingResume(owner);
  }).catch(() => {}).finally(() => {
    owner.observing = false;
    // A Ready or completion arriving during an old read owns one follow-up.
    if (owner.observeAgain) reconcilePendingGameResume(owner);
  });
}

async function submitGameResume(pids: SuspendedProcessIdentity[], raw: string | undefined,
  settle: ResumeOwner['settle'], preserveUnsentFallback = false): Promise<GameCtlResult> {
  const owner: ResumeOwner = { id: crypto.randomUUID(), powerRevision: 0, raw, settle, stop: () => {} };
  pendingResume = owner;
  let commandReply: AbortController | undefined = new AbortController();
  let completionSeen = false;
  const stopCompletion = on<{ requestId?: string }>('game.resume.completed', receipt => {
    if (completionSeen || receipt?.requestId !== owner.id) return;
    completionSeen = true;
    // Native saves the receipt before this notice. A status read begun before
    // completion can still be pending or carry an older snapshot.
    if (owner.statusRead) {
      owner.statusRead.abort();
      reconcilePendingGameResume(owner);
    } else if (commandReply) commandReply.abort();
    else reconcilePendingGameResume(owner);
  });
  let previousScope = resumePowerScope();
  const stopPower = onSharedPowerLifecycle(power => {
    const scope = resumePowerScope(power);
    const changed = scope !== previousScope;
    previousScope = scope;
    if (!changed) return;
    ++owner.powerRevision;
    // Release the old read's local wait; Native still owns the same command receipt.
    owner.statusRead?.abort();
    cancelResumeReconcile(owner);
    if (scope) reconcilePendingGameResume(owner);
  }, { observeSuspendSerial: true, observeNotificationSequence: true });
  owner.stop = () => { stopCompletion(); stopPower(); };
  const revision = owner.powerRevision;
  let result: ResumeResult;
  try {
    result = await invoke<ResumeResult>('game.resume', { pids, requestId: owner.id }, { timeoutMs: 8000, signal: commandReply.signal });
  } catch (error) {
    if (error instanceof IpcPostError) {
      if (preserveUnsentFallback) {
        owner.settlement = settle({ resumed: 0, failedPids: [], stalePids: [], recoveryPending: true });
        return finishPendingResume(owner);
      }
      releaseResume(owner);
      return unsettledResume(error.message);
    }
    if (!(error instanceof IpcRemoteError)) return readPendingResume(owner);
    releaseResume(owner);
    return unsettledResume(error.message);
  } finally { commandReply = undefined; }
  if (revision !== owner.powerRevision) {
    // Only current readback may settle a command that crossed a power boundary.
    reconcilePendingGameResume(owner);
    return unsettledResume();
  }
  if (!isResumeResult(result)) return readPendingResume(owner);
  owner.settlement = settle(result);
  return finishPendingResume(owner);
}

export async function suspendGame(
  rootPid: number,
  name: string,
  processCreated?: string,
  expectedTarget?: GameTargetIdentity,
): Promise<GameCtlResult> {
  const captured = captureGameControlTarget(rootPid, processCreated, expectedTarget);
  const scope = resumePowerScope();
  return withControlLock(async () => {
    if (!captured || !scope || resumePowerScope() !== scope || !isCurrentGameTarget(captured)) return staleGameControl();
    if (pendingResume) return reconcileBeforeOtherGameControl(pendingResume);
    let retired = false;
    const stopPower = onSharedPowerLifecycle(power => {
      if (resumePowerScope(power) !== scope) retired = true;
    }, { observeSuspendSerial: true, observeNotificationSequence: true });
    const nativeResult = await invoke<{
      paused?: boolean;
      pids?: number[];
      failedPids?: number[];
      processes?: SuspendedProcessIdentity[];
      okCount?: number;
      failCount?: number;
      name?: string;
      path?: string;
    }>('game.suspend', {
      pid: captured.pid, processCreated: captured.processCreated,
      path: captured.path, identityGeneration: captured.identityGeneration,
    }, { timeoutMs: MANUAL_PAUSE_TIMEOUT_MS }).catch(() => undefined).finally(stopPower);
    if (!nativeResult) return unsettledResume('暂停结果尚未确认，正在重新读取当前游戏状态');
    if (retired || resumePowerScope() !== scope || !isCurrentGameTarget(captured)) return staleGameControl();
    const nativeOkCount = Number(nativeResult?.okCount) || 0;
    const nativeFailCount = Number(nativeResult?.failCount) || 0;
    const nativeMessages: string[] = [];
    if (!nativeResult?.paused) {
      nativeMessages.push(`${name || '当前游戏'} 主进程未能暂停，请确认权限或进程状态`);
      return { ok: false, okCount: nativeOkCount, failCount: Math.max(1, nativeFailCount), msgs: nativeMessages };
    }
    if (nativeFailCount > 0) nativeMessages.push('当前游戏进程暂停失败，可稍后重试');
    // Match the requested game instance explicitly. A Steam launcher or any
    // other returned member must never become the saved pause owner by order.
    const returnedPids = Array.isArray(nativeResult.pids) ? nativeResult.pids : [];
    const returnedProcesses = Array.isArray(nativeResult.processes) ? nativeResult.processes : [];
    const matches = returnedProcesses.filter(process => process?.pid === rootPid);
    const confirmed = matches.length === 1 ? matches[0] : undefined;
    const validBirth = confirmed?.processCreated !== undefined &&
      typeof confirmed.processCreated === 'string' && /^[1-9][0-9]*$/.test(confirmed.processCreated);
    const validReceipt = Number.isSafeInteger(rootPid) && rootPid > 0 &&
      returnedPids.filter(pid => pid === rootPid).length === 1 &&
      (returnedProcesses.length === 0 || matches.length === 1 && validBirth) &&
      (!processCreated || !confirmed || confirmed.processCreated === String(processCreated));
    if (!validReceipt) {
      // Native markers retain any actual pause debt. Do not overwrite a prior
      // frontend record or claim another PID was this game's confirmed pause.
      return { ok: false, okCount: 0, failCount: Math.max(1, nativeFailCount),
        msgs: ['暂停回执与当前游戏进程不一致，已保留原恢复记录，请重试恢复。'] };
    }
    // Native owns both the pause and its record. A late renderer receipt cannot republish it.
    emitSuspendedState(true);
    return { ok: true, okCount: nativeOkCount, failCount: nativeFailCount, msgs: nativeMessages };
  });
}

export async function resumeGame(preflight?: AbortSignal, expectedTarget?: GameTargetIdentity): Promise<GameCtlResult> {
  const target = expectedTarget && captureGameControlTarget(expectedTarget.pid, expectedTarget.processCreated, expectedTarget);
  const scope = target && resumePowerScope();
  return withControlLock(async () => {
    if (pendingResume) return readPendingResume(pendingResume);
    if (expectedTarget && (!target || !isCurrentGameTarget(target))) return staleGameControl();
    const resumeFromNativeMarkers = (savedRaw: string | undefined, corruptRaw?: string): Promise<GameCtlResult> =>
      submitGameResume([], savedRaw, fallback => {
      const resumed = Number(fallback?.resumed) || 0;
      const failedPids = 'failedPids' in fallback && Array.isArray(fallback.failedPids)
        ? [...new Set(fallback.failedPids.filter((pid) => Number.isInteger(pid) && pid > 0))]
        : [];
      const pending = fallback.recoveryPending === true || failedPids.length > 0;
      if (pending) {
        // PID-only retry records still require the authoritative native marker.
        // Preserve malformed original bytes inside the recoverable record.
        const raw = JSON.stringify({
          root: failedPids[0] || 0,
          pids: failedPids,
          processes: failedPids.map((pid) => ({ pid })),
          recoveryPending: true,
          ...(corruptRaw !== undefined ? { recoveryOriginalRaw: corruptRaw } : {}),
          ts: Date.now(),
        });
        const failCount = Math.max(1, failedPids.length);
        return { record: { raw, suspended: true },
          result: { ok: resumed > 0, okCount: resumed, failCount, msgs: ['恢复尚未完成，已保留记录，可重试'] } };
      }
      return {
        record: { raw: null, suspended: false },
        result: { ok: resumed > 0, okCount: resumed, failCount: 0,
          msgs: resumed > 0 ? [] : [corruptRaw !== undefined ? '暂停状态损坏，已清除' : '没有已暂停的游戏'] },
      };
    }, true);
    // A sleeping renderer can retire these reads, but an already dispatched
    // resume and its record updates must keep their place in the control queue.
    if (preflight?.aborted) return staleGameControl();
    let raw: string | undefined;
    try {
      if (await fs.exists(SUSPEND_STATE, PAUSE_RECORD_READ_TIMEOUT_MS, preflight)) {
        raw = await fs.readTextFile(SUSPEND_STATE, undefined, { timeoutMs: PAUSE_RECORD_READ_TIMEOUT_MS, signal: preflight });
      }
    }
    catch (error) {
      if (preflight?.aborted) return staleGameControl();
      return { ok: false, okCount: 0, failCount: 1, msgs: [`暂停状态读取失败，已保留恢复记录：${(error as Error).message}`] };
    }
    if (preflight?.aborted) return staleGameControl();
    if (raw === undefined) return resumeFromNativeMarkers(undefined);
    let state: any;
    try {
      state = JSON.parse(raw);
    } catch {
      return resumeFromNativeMarkers(raw, raw);
    }
    const recordedProcesses: SuspendedProcessIdentity[] = Array.isArray(state.processes)
      ? state.processes
        .map((process: any) => {
          const pid = Number(process?.pid);
          if (!Number.isInteger(pid) || pid <= 0) return null;
          const processCreated = process?.processCreated !== undefined
            ? String(process.processCreated)
            : undefined;
          return processCreated ? { pid, processCreated } : { pid };
        })
        .filter((process: SuspendedProcessIdentity | null): process is SuspendedProcessIdentity => process !== null)
      : [];
    // Merge both legacy fields without allowing the PID-only field to erase a
    // process creation time saved in the richer identity record.
    const recordedPidsFromState = Array.isArray(state.pids)
      ? state.pids.filter((pid: unknown) => Number.isInteger(pid) && Number(pid) > 0)
      : [];
    const recordedByPid = new Map<number, SuspendedProcessIdentity>();
    for (const process of recordedProcesses) recordedByPid.set(process.pid, process);
    for (const pid of recordedPidsFromState) {
      if (!recordedByPid.has(pid)) recordedByPid.set(pid, { pid });
    }
    const recordedPids = [...recordedByPid.keys()];
    const recordedValues = recordedPids.length > 0
      ? recordedPids.map((pid) => recordedByPid.get(pid) || { pid })
      : [];
    const recordedRoot = Number(state.root);
    if (target && Number.isSafeInteger(recordedRoot) && recordedRoot > 0 &&
        (recordedRoot !== target.pid || recordedProcesses.some(process =>
          process.pid === target.pid && process.processCreated !== undefined && process.processCreated !== target.processCreated))) {
      const current = () => !preflight?.aborted && !!scope && resumePowerScope() === scope && isCurrentGameTarget(target);
      if (!current()) return staleGameControl();
      try {
        const mark = manualPauseMarkerPath(target.pid);
        if (!(await fs.exists(mark, PAUSE_RECORD_READ_TIMEOUT_MS, preflight)) || !current() ||
            !matchesManualPauseMarker(await fs.readTextFile(mark, 4096, {
              timeoutMs: PAUSE_RECORD_READ_TIMEOUT_MS, signal: preflight,
            }), target.pid, target.processCreated) || !current()) return staleGameControl();
      } catch { return staleGameControl(); }
      // This click owns only the exact native marker. The old menu record can
      // still name a different paused process, so leave it to its own recovery.
      return submitGameResume([{ pid: target.pid, processCreated: target.processCreated }], undefined, result => {
        const resumed = Number(result.resumed) || 0;
        const pending = result.recoveryPending === true || !!result.failedPids?.length || !!result.confirmationPendingPids?.length;
        return { refreshSuspendedState: !pending,
          result: { ok: !pending, okCount: resumed, failCount: Number(pending),
            msgs: pending ? ['恢复尚未完成，已保留记录，可重试'] : [] } };
      });
    }
    if (recordedValues.length === 0) {
      return resumeFromNativeMarkers(raw, typeof state.recoveryOriginalRaw === 'string' ? state.recoveryOriginalRaw : undefined);
    }
    return submitGameResume(recordedValues, raw, nativeResult => {
    const nativeOkCount = Number(nativeResult?.resumed) || 0;
    const nativeFailedPids = Array.isArray(nativeResult?.failedPids)
      ? nativeResult.failedPids.filter((pid) => Number.isInteger(pid) && pid > 0)
      : [];
    const nativeStalePids = Array.isArray(nativeResult?.stalePids)
      ? nativeResult.stalePids.filter((pid) => Number.isInteger(pid) && pid > 0)
      : [];
    const nativeFailCount = nativeFailedPids.length;
    const nativePending = nativeResult.recoveryPending === true;
    const confirmingPids = Array.isArray(nativeResult.confirmationPendingPids)
      ? nativeResult.confirmationPendingPids.filter((pid) => Number.isInteger(pid) && pid > 0)
      : [];
    let record: NonNullable<ResumeSettlement['record']> = { raw: nativePending ? raw : null, suspended: nativePending };
    if (nativeFailCount > 0 || nativeStalePids.length > 0 || confirmingPids.length > 0) {
      const retained = new Set([...nativeFailedPids, ...confirmingPids]);
      const stale = new Set(nativeStalePids);
      // A stale PID belongs to an exited or reused process and must never be
      // retained for a later resume retry. Failed live instances keep their
      // complete identity, including processCreated.
      const remaining = recordedPids.filter((pid: number) => retained.has(pid) && !stale.has(pid));
      if (remaining.length > 0) {
        record = { raw: JSON.stringify({ ...state, pids: remaining,
          processes: remaining.map((pid: number) => recordedByPid.get(pid) || { pid }) }), suspended: true };
      }
    }
    const nativeMessages: string[] = [];
    if (nativeOkCount === 0 && nativeFailCount > 0) nativeMessages.push('进程恢复失败，可能需要管理员权限');
    if (nativeFailCount > 0) nativeMessages.push(`${nativeFailCount} 个进程恢复失败，可重试`);
    if (confirmingPids.length > 0) nativeMessages.push(`${confirmingPids.length} 个进程的恢复结果待确认，已保留记录`);
    else if (nativePending && nativeFailCount === 0) nativeMessages.push('恢复尚未完成，已保留记录，可重试');
    const failCount = Math.max(nativeFailCount + new Set(confirmingPids).size, Number(nativePending));
    return { record, result: { ok: nativeOkCount > 0 || failCount === 0, okCount: nativeOkCount,
      failCount, msgs: nativeMessages } };
    });
  });
}

export async function closeGame(rootPid: number, name: string, processCreated?: string, expectedTarget?: GameTargetIdentity): Promise<GameCtlResult> {
  const scope = resumePowerScope();
  const captured = captureGameControlTarget(rootPid, processCreated, expectedTarget);
  return withControlLock(async () => {
    if (!scope || resumePowerScope() !== scope || !captured || !isCurrentGameTarget(captured)) return staleGameControl();
    if (pendingResume) return reconcileBeforeOtherGameControl(pendingResume);
    if (!Number.isSafeInteger(rootPid) || rootPid <= 0) {
      return { ok: false, okCount: 0, failCount: 1, msgs: [`${name || 'Current game'} has an invalid PID`] };
    }
    // Re-acquire the current valve target immediately before control. The PID
    // held by a page is only an expected value and may already be stale.
    const valveTarget = await detectGame(true).catch(() => null);
    if (resumePowerScope() !== scope) return staleGameControl();
    const valvePid = Number(valveTarget?.pid);
    const valveCreated = valveTarget?.processCreated ? String(valveTarget.processCreated) : '';
    if (valvePid !== rootPid || !/^\d+$/.test(valveCreated) || valveCreated === '0' ||
        (processCreated && String(processCreated) !== valveCreated)) {
      return { ok: false, okCount: 0, failCount: 1, msgs: ['Game valve target changed; close aborted'] };
    }
    if (!valveTarget || !valveTarget.path || !valveTarget.identityGeneration || !isCurrentGameTarget(valveTarget) || !isCurrentGameTarget(captured))
      return { ok: false, okCount: 0, failCount: 1, msgs: ['游戏目标尚未确认，请重试'] };
    const result = await invoke<{ ok: boolean; attempted: boolean; exited: boolean }>('game.close', {
      pid: captured.pid, processCreated: captured.processCreated,
      path: captured.path, identityGeneration: captured.identityGeneration,
    }, { timeoutMs: 8000 }).catch(() => ({ ok: false, attempted: false, exited: false }));
    if (!result.ok || !result.exited)
      return { ok: false, okCount: 0, failCount: 1, msgs: [`${name || '当前游戏'} 关闭尚未确认，请重试`] };
    if (await fs.exists(SUSPEND_STATE, PAUSE_RECORD_READ_TIMEOUT_MS).catch(() => false)) {
      try {
        const raw = await fs.readTextFile(SUSPEND_STATE, undefined, { timeoutMs: PAUSE_RECORD_READ_TIMEOUT_MS });
        const state = JSON.parse(raw) as {
          root?: number;
          processes?: SuspendedProcessIdentity[];
        };
        if (Number(state.root) === rootPid) {
          if (Array.isArray(state.processes) && state.processes.length > 0) {
            await submitGameResume(state.processes, raw, recovery => {
              // Closing the root does not prove the rest of the pause debt settled.
              const settled = recovery.recoveryPending !== true && recovery.failedPids!.length === 0;
              return { ...(settled ? { record: { raw: null, suspended: false } } : {}),
                result: { ok: true, okCount: 1, failCount: 0, msgs: [] } };
            });
          } else {
            if (await settlePauseRecord(raw, null) === 'committed') emitSuspendedState(false);
          }
        }
      } catch { }
    }
    return { ok: true, okCount: 1, failCount: 0, msgs: [] };
  });
}

/**
 * Wait until the exact PID used by closeGame is gone.
 * This deliberately does not run game detection again: a newly launched
 * process must never be mistaken for the game that was just closed.
 */
export async function waitForProcessExit(pid: number, processCreated?: string, timeoutMs = 10000): Promise<boolean> {
  if (!Number.isSafeInteger(pid) || pid <= 0 || !processCreated || !/^[1-9][0-9]*$/.test(processCreated)) return false;
  const timeout = Number.isFinite(timeoutMs) ? Math.max(0, Math.min(10000, Math.trunc(timeoutMs))) : 10000;
  try {
    const result = await invoke<{ known: boolean; exited: boolean }>('game.waitExit',
      { pid, processCreated, timeoutMs: timeout }, { timeoutMs: timeout + 2000 });
    return result.known === true && result.exited === true;
  } catch { return false; } // An unknown observation never proves process exit.
}

function matchesManualPauseMarker(marker: string, pid: number, processCreated: string): boolean {
  const fields = marker.split('|');
  return [['pid', String(pid)], ['created', processCreated], ['state', 'suspended']].every(([key, value]) => {
    const matches = fields.filter(field => field.startsWith(`${key}=`));
    return matches.length === 1 && matches[0] === `${key}=${value}`;
  });
}

export async function hasSuspendedState(rootPid?: number, processCreated?: string, signal?: AbortSignal): Promise<{ suspended: boolean; pid?: number }> {
  const timeoutMs = PAUSE_RECORD_READ_TIMEOUT_MS;
  const exists = (path: string) => fs.exists(path, timeoutMs, signal);
  const read = (path: string, maxBytes: number) => fs.readTextFile(path, maxBytes, { timeoutMs, signal });
  try {
    if (await exists(SUSPEND_STATE)) {
      const state = JSON.parse(await read(SUSPEND_STATE, 65536)) as {
        root?: number; recoveryPending?: boolean; processes?: SuspendedProcessIdentity[];
      };
      if (state.recoveryPending === true && (!Number.isInteger(Number(state.root)) || Number(state.root) <= 0))
        return { suspended: true };
      const matches = rootPid === undefined || Number(state.root) === rootPid;
      if (matches && !processCreated) return { suspended: true, pid: Number(state.root) || undefined };
      const owners = matches && Array.isArray(state.processes) ? state.processes.filter(value => value?.pid === rootPid) : [];
      if (owners.length === 1 && owners[0].processCreated === processCreated && processCreated)
        return { suspended: true, pid: rootPid };
      if (!processCreated) return { suspended: false };
      // An unrelated or PID-only record cannot hide a newer exact native pause.
    }
  } catch { signal?.throwIfAborted(); return { suspended: true }; }
  if (!rootPid) return { suspended: false };
  const mark = manualPauseMarkerPath(rootPid);
  try {
    if (!(await exists(mark))) return { suspended: false };
    if (!processCreated) return { suspended: true, pid: rootPid };
    const confirmed = matchesManualPauseMarker(await read(mark, 4096), rootPid, processCreated);
    return { suspended: confirmed, ...(confirmed ? { pid: rootPid } : {}) };
  } catch { signal?.throwIfAborted(); return { suspended: true }; } // Unknown transport keeps recovery reachable.
}

/** Release the exact JoyXoff backend through native control; never a shell toggle. */
export async function closeJoyxoffIfRunning(): Promise<boolean> {
  const result = await invoke<{ ok: boolean; closed: boolean; stateKnown: boolean; error?: string }>('mouseMode.releaseJoyxoff', {});
  if (!result.ok || !result.stateKnown) throw new Error(result.error || 'JoyXoff 关闭未确认');
  return result.closed === true;
}

export type MouseBackend = 'gamebar' | 'joyxoff';

export interface MouseModeState {
  mode?: 'auto' | 'on' | 'off';
  scene?: string;
  policyReason?: string;
  pending?: boolean;
  fallbackBackend?: MouseBackend;
  fallbackRestored?: boolean;
  mutated?: boolean;
  sequence?: number;
  ok: boolean;
  backend: MouseBackend;
  on: boolean;
  provider?: 'ymcc' | 'steam';
  controlAllowed?: boolean;
  stateKnown?: boolean;
  mappingVerified?: boolean;
  verification?: 'unobserved' | 'process-liveness' | 'component-readback' | 'backend-state';
  joyxoffOn?: boolean;
  gamebarOn?: boolean;
  joyxoffAvailable?: boolean;
  gamebarAvailable?: boolean;
  gamebarModelSupported?: boolean;
  reason?: 'gameinput_redist_missing' | 'gamebar_interface_error' | string;
  error?: string;
}

export interface GameInputRedistState {
  installed: boolean;
  mouseInterfaceAvailable: boolean;
  installerAvailable: boolean;
  hresult: number;
}

export interface GameInputRedistInstallResult {
  ok: boolean;
  launched?: boolean;
  alreadyInstalled?: boolean;
  error?: string;
}

export async function getMouseModeState(): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.get', {});
}

export interface MouseBootContext {
  input: Record<string, unknown>;
  extensions: Record<string, unknown>;
}

export async function setMouseBackend(backend: MouseBackend, bootContext?: MouseBootContext): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.setBackend', { backend, ...(bootContext ? { bootContext } : {}) }, { timeoutMs: 30000 });
}

export async function setMouseModeEnabled(on: boolean, bootContext?: MouseBootContext): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.set', { on, ...(bootContext ? { bootContext } : {}) });
}

export async function setMouseModePolicy(mode: 'auto' | 'on' | 'off', bootContext?: MouseBootContext): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.setPolicy', { mode, ...(bootContext ? { bootContext } : {}) });
}

export async function toggleMouseMode(): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.toggle', {});
}

export async function getGameInputRedistState(): Promise<GameInputRedistState> {
  return invoke<GameInputRedistState>('gameinput.redist.status', {});
}

export async function installGameInputRedist(): Promise<GameInputRedistInstallResult> {
  return invoke<GameInputRedistInstallResult>('gameinput.redist.install', {});
}
