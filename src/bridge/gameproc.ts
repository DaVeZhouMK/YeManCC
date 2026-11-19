import { fs } from './api';
import { invoke } from './ipc';
import { detectGame, getCurrentGame, isCurrentGameTarget, type GameTargetIdentity } from './gamedetect';

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
export const QUICKAPP_SUSPENDED_EVENT = 'quickapp:suspended-state';

function emitSuspendedState(suspended: boolean): void {
  if (typeof window !== 'undefined') {
    window.dispatchEvent(new CustomEvent(QUICKAPP_SUSPENDED_EVENT, { detail: { suspended } }));
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

export async function suspendGame(
  rootPid: number,
  name: string,
  processCreated?: string,
  expectedTarget?: GameTargetIdentity,
): Promise<GameCtlResult> {
  const captured = captureGameControlTarget(rootPid, processCreated, expectedTarget);
  return withControlLock(async () => {
    if (!captured || !isCurrentGameTarget(captured)) return staleGameControl();
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
    });
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
    const nativePids = [rootPid];
    const nativeProcesses: SuspendedProcessIdentity[] = confirmed
      ? [{ pid: rootPid, processCreated: confirmed.processCreated }]
      : processCreated ? [{ pid: rootPid, processCreated: String(processCreated) }] : [];
    await fs.writeTextFileAtomic(SUSPEND_STATE, JSON.stringify({
      root: rootPid,
      pids: nativePids,
      processes: nativeProcesses,
      ts: Date.now(),
    }));
    emitSuspendedState(true);
    return { ok: true, okCount: nativeOkCount, failCount: nativeFailCount, msgs: nativeMessages };
  });
}

export async function resumeGame(): Promise<GameCtlResult> {
  return withControlLock(async () => {
    const resumeFromNativeMarkers = async (corruptRaw?: string): Promise<GameCtlResult> => {
      const fallback = await invoke<{
        resumed?: number;
        failedPids?: number[];
        recoveryPending?: boolean;
      }>('game.resume', { pids: [] }).catch(() => ({ resumed: 0, recoveryPending: true }));
      const resumed = Number(fallback?.resumed) || 0;
      const failedPids = 'failedPids' in fallback && Array.isArray(fallback.failedPids)
        ? [...new Set(fallback.failedPids.filter((pid) => Number.isInteger(pid) && pid > 0))]
        : [];
      const pending = fallback.recoveryPending === true || failedPids.length > 0;
      if (pending) {
        // PID-only retry records still require the authoritative native marker.
        // Preserve malformed original bytes inside the recoverable record.
        await fs.writeTextFileAtomic(SUSPEND_STATE, JSON.stringify({
          root: failedPids[0] || 0,
          pids: failedPids,
          processes: failedPids.map((pid) => ({ pid })),
          recoveryPending: true,
          ...(corruptRaw !== undefined ? { recoveryOriginalRaw: corruptRaw } : {}),
          ts: Date.now(),
        }));
        emitSuspendedState(true);
        const failCount = Math.max(1, failedPids.length);
        return { ok: resumed > 0, okCount: resumed, failCount, msgs: ['恢复尚未完成，已保留记录，可重试'] };
      }
      await fs.remove(SUSPEND_STATE).catch(() => {});
      emitSuspendedState(false);
      return {
        ok: resumed > 0,
        okCount: resumed,
        failCount: 0,
        msgs: resumed > 0 ? [] : [corruptRaw !== undefined ? '暂停状态损坏，已清除' : '没有已暂停的游戏'],
      };
    };
    if (!(await fs.exists(SUSPEND_STATE))) return resumeFromNativeMarkers();
    let raw: string;
    try { raw = await fs.readTextFile(SUSPEND_STATE); }
    catch (error) {
      return { ok: false, okCount: 0, failCount: 1, msgs: [`暂停状态读取失败，已保留恢复记录：${(error as Error).message}`] };
    }
    let state: any;
    try {
      state = JSON.parse(raw);
    } catch {
      return resumeFromNativeMarkers(raw);
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
    if (recordedValues.length === 0) {
      return resumeFromNativeMarkers(typeof state.recoveryOriginalRaw === 'string' ? state.recoveryOriginalRaw : undefined);
    }
    const nativeResult = await invoke<{
      resumed?: number;
      failedPids?: number[];
      stalePids?: number[];
    }>('game.resume', { pids: recordedValues });
    const nativeOkCount = Number(nativeResult?.resumed) || 0;
    const nativeFailedPids = Array.isArray(nativeResult?.failedPids)
      ? nativeResult.failedPids.filter((pid) => Number.isInteger(pid) && pid > 0)
      : [];
    const nativeStalePids = Array.isArray(nativeResult?.stalePids)
      ? nativeResult.stalePids.filter((pid) => Number.isInteger(pid) && pid > 0)
      : [];
    const nativeFailCount = nativeFailedPids.length;
    if (nativeFailCount > 0 || nativeStalePids.length > 0) {
      const failed = new Set(nativeFailedPids);
      const stale = new Set(nativeStalePids);
      // A stale PID belongs to an exited or reused process and must never be
      // retained for a later resume retry. Failed live instances keep their
      // complete identity, including processCreated.
      const remaining = recordedPids.filter((pid: number) => failed.has(pid) && !stale.has(pid));
      if (remaining.length > 0) {
        state.pids = remaining;
        state.processes = remaining.map((pid: number) => recordedByPid.get(pid) || { pid });
        await fs.writeTextFileAtomic(SUSPEND_STATE, JSON.stringify(state));
        emitSuspendedState(true);
      } else {
        await fs.remove(SUSPEND_STATE).catch(() => {});
        emitSuspendedState(false);
      }
    } else {
      await fs.remove(SUSPEND_STATE).catch(() => {});
      emitSuspendedState(false);
    }
    const nativeMessages: string[] = [];
    if (nativeOkCount === 0 && nativeFailCount > 0) nativeMessages.push('进程恢复失败，可能需要管理员权限');
    if (nativeFailCount > 0) nativeMessages.push(`${nativeFailCount} 个进程恢复失败，可重试`);
    return { ok: nativeOkCount > 0 || nativeFailCount === 0, okCount: nativeOkCount, failCount: nativeFailCount, msgs: nativeMessages };
  });
}

export async function closeGame(rootPid: number, name: string, processCreated?: string, expectedTarget?: GameTargetIdentity): Promise<GameCtlResult> {
  const captured = captureGameControlTarget(rootPid, processCreated, expectedTarget);
  return withControlLock(async () => {
    if (!captured || !isCurrentGameTarget(captured)) return staleGameControl();
    if (!Number.isSafeInteger(rootPid) || rootPid <= 0) {
      return { ok: false, okCount: 0, failCount: 1, msgs: [`${name || 'Current game'} has an invalid PID`] };
    }
    // Re-acquire the current valve target immediately before control. The PID
    // held by a page is only an expected value and may already be stale.
    const valveTarget = await detectGame(true).catch(() => null);
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
    if (await fs.exists(SUSPEND_STATE).catch(() => false)) {
      try {
        const state = JSON.parse(await fs.readTextFile(SUSPEND_STATE)) as {
          root?: number;
          processes?: SuspendedProcessIdentity[];
        };
        if (Number(state.root) === rootPid) {
          if (Array.isArray(state.processes) && state.processes.length > 0) {
            await invoke('game.resume', { pids: state.processes }).catch(() => {});
          }
          await fs.remove(SUSPEND_STATE).catch(() => {});
          emitSuspendedState(false);
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

export async function hasSuspendedState(rootPid?: number, processCreated?: string): Promise<{ suspended: boolean; pid?: number }> {
  try {
    if (await fs.exists(SUSPEND_STATE)) {
      const state = JSON.parse(await fs.readTextFile(SUSPEND_STATE, 65536)) as {
        root?: number; recoveryPending?: boolean; processes?: SuspendedProcessIdentity[];
      };
      if (state.recoveryPending === true && (!Number.isInteger(Number(state.root)) || Number(state.root) <= 0))
        return { suspended: true };
      const matches = rootPid === undefined || Number(state.root) === rootPid;
      if (!matches) return { suspended: false };
      if (!processCreated) return { suspended: true, pid: Number(state.root) || undefined };
      const owners = Array.isArray(state.processes) ? state.processes.filter(value => value?.pid === rootPid) : [];
      if (owners.length === 1 && owners[0].processCreated) {
        const confirmed = owners[0].processCreated === processCreated;
        return { suspended: confirmed, ...(confirmed ? { pid: rootPid } : {}) };
      }
      // A legacy PID-only frontend record is not proof for a reused PID.
      // Fall through to the native exact-instance marker without deleting debt.
    }
  } catch { return { suspended: true }; }
  if (!rootPid) return { suspended: false };
  const mark = `C:\\SOFT\\YeMan\\PowerControl\\Sleep\\manual-suspended\\${rootPid}.txt`;
  try {
    if (!(await fs.exists(mark))) return { suspended: false };
    if (!processCreated) return { suspended: true, pid: rootPid };
    const marker = await fs.readTextFile(mark, 4096);
    const ids = marker.match(/(?:^|\|)pid=([1-9][0-9]*)(?=\||$)/g) || [];
    const births = marker.match(/(?:^|\|)created=([1-9][0-9]*)(?=\||$)/g) || [];
    const confirmed = ids.length === 1 && births.length === 1 &&
      ids[0].replace(/^\|/, '') === `pid=${rootPid}` &&
      births[0].replace(/^\|/, '') === `created=${processCreated}`;
    return { suspended: confirmed, ...(confirmed ? { pid: rootPid } : {}) };
  } catch { return { suspended: true }; } // Unknown transport keeps recovery reachable.
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
