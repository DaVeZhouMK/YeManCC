import { fs, proc, shell } from './api';
import { invoke } from './ipc';

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

export async function suspendGame(
  rootPid: number,
  name: string,
  processCreated?: string,
): Promise<GameCtlResult> {
  return withControlLock(async () => {
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
      pid: rootPid,
      ...(processCreated ? { processCreated: String(processCreated) } : {}),
    });
    const nativeOkCount = Number(nativeResult?.okCount) || 0;
    const nativeFailCount = Number(nativeResult?.failCount) || 0;
    const nativeMessages: string[] = [];
    if (!nativeResult?.paused) {
      nativeMessages.push(`${name || '当前游戏'} 主进程未能暂停，请确认权限或进程状态`);
      return { ok: false, okCount: nativeOkCount, failCount: Math.max(1, nativeFailCount), msgs: nativeMessages };
    }
    if (nativeFailCount > 0) nativeMessages.push('当前游戏进程暂停失败，可稍后重试');
    // 新 native 协议严格只冻结一个根 PID。即使旧版本 native 返回过多项，
    // 也不能把多 PID 状态继续写入新的暂停记录。
    const nativePids = Array.isArray(nativeResult.pids)
      ? nativeResult.pids.filter((pid) => Number.isInteger(pid) && pid > 0).slice(0, 1)
      : [];
    if (nativePids.length === 0) nativePids.push(rootPid);
    const nativeProcesses = Array.isArray(nativeResult.processes)
      ? nativeResult.processes.slice(0, 1)
      : [];
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
    if (!(await fs.exists(SUSPEND_STATE))) {
      // 状态文件丢失时仍不按名称猜测进程：native 只读取它自己写入的
      // manual-suspended PID 标记目录。
      const fallback = await invoke<{ resumed?: number }>('game.resume', { pids: [] }).catch(() => ({ resumed: 0 }));
      const resumed = Number(fallback?.resumed) || 0;
      emitSuspendedState(false);
      return {
        ok: resumed > 0,
        okCount: resumed,
        failCount: 0,
        msgs: resumed > 0 ? [] : ['没有已暂停的游戏'],
      };
    }
    let state: any;
    try {
      state = JSON.parse(await fs.readTextFile(SUSPEND_STATE));
    } catch {
      const fallback = await invoke<{ resumed?: number }>('game.resume', { pids: [] }).catch(() => ({ resumed: 0 }));
      const resumed = Number(fallback?.resumed) || 0;
      await fs.remove(SUSPEND_STATE).catch(() => {});
      emitSuspendedState(false);
      return { ok: resumed > 0, okCount: resumed, failCount: 0, msgs: resumed > 0 ? [] : ['暂停状态损坏，已清除'] };
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

export async function closeGame(rootPid: number, name: string, processCreated?: string): Promise<GameCtlResult> {
  return withControlLock(async () => {
    if (!Number.isSafeInteger(rootPid) || rootPid <= 0) {
      return { ok: false, okCount: 0, failCount: 1, msgs: [`${name || 'Current game'} has an invalid PID`] };
    }
    // Re-acquire the current valve target immediately before control. The PID
    // held by a page is only an expected value and may already be stale.
    const valveTarget = await invoke<{
      pid?: number;
      processCreated?: string;
    } | null>('game.detect', {}).catch(() => null);
    const valvePid = Number(valveTarget?.pid);
    const valveCreated = valveTarget?.processCreated ? String(valveTarget.processCreated) : '';
    if (valvePid !== rootPid || !/^\d+$/.test(valveCreated) || valveCreated === '0' ||
        (processCreated && String(processCreated) !== valveCreated)) {
      return { ok: false, okCount: 0, failCount: 1, msgs: ['Game valve target changed; close aborted'] };
    }
    // Verify the same process instance one more time before taskkill. This does
    // not make the OS operation transactional, but closes the normal PID-reuse
    // window and prevents an old page snapshot from controlling a new process.
    const identity = await proc.identity(rootPid).catch(() => ({ valid: false, processCreated: '' }));
    if (!identity.valid || String(identity.processCreated || '') !== valveCreated) {
      return { ok: false, okCount: 0, failCount: 1, msgs: ['Game valve process identity changed; close aborted'] };
    }
    const result = await shell.run('taskkill', ['/F', '/T', '/PID', String(rootPid)])
      .catch(() => ({ exitCode: 1, stdout: '', stderr: '' }));
    if (result.exitCode !== 0) {
      return { ok: false, okCount: 0, failCount: 1, msgs: [`${name || '当前游戏'} 关闭失败`] };
    }
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
  if (!Number.isInteger(pid) || pid <= 0) return true;
  const deadline = Date.now() + Math.max(0, timeoutMs);
  const filter = `PID eq ${pid}`;
  const expectedCreated = processCreated && /^\d+$/.test(String(processCreated))
    ? String(processCreated)
    : '';
  while (Date.now() <= deadline) {
    try {
      if (expectedCreated) {
        const identity = await proc.identity(pid);
        if (!identity.valid) return true;
        if (String(identity.processCreated || '') !== expectedCreated) return true;
      }
      const result = await shell.run('tasklist', ['/FI', filter, '/FO', 'CSV', '/NH'], 3000);
      const stillRunning = (result.stdout || '').split(/\r?\n/).some((line) => {
        const fields = line.match(/^\s*"[^"]*"\s*,\s*"(\d+)"\s*,/);
        return fields ? Number(fields[1]) === pid : false;
      });
      if (!stillRunning) return true;
    } catch {
      // A transient tasklist failure is not proof that the process exited.
    }
    if (Date.now() >= deadline) break;
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  return false;
}

export async function hasSuspendedState(rootPid?: number): Promise<{ suspended: boolean; pid?: number }> {
  try {
    if (await fs.exists(SUSPEND_STATE)) {
      const state = JSON.parse(await fs.readTextFile(SUSPEND_STATE, 65536)) as { root?: number };
      const matches = rootPid === undefined || Number(state.root) === rootPid;
      return { suspended: matches, pid: matches ? Number(state.root) : undefined };
    }
  } catch { return { suspended: false }; }
  if (!rootPid) return { suspended: false };
  const mark = `C:\\SOFT\\YeMan\\PowerControl\\Sleep\\manual-suspended\\${rootPid}.txt`;
  return { suspended: await fs.exists(mark).catch(() => false) };
}

export async function isJoyxoffRunning(): Promise<boolean> {
  try {
    const result = await proc.running(['JoyXoff*']);
    return Object.keys(result || {}).some((key) => key.toLowerCase().includes('joyxoff') && result[key]);
  } catch { return false; }
}

const JOYXOFF_BAT = 'C:\\SOFT\\YeMan\\PowerControl\\JoyXoff.bat';
const JOYXOFF_VBS = 'C:\\SOFT\\YeMan\\PowerControl\\模拟鼠标.vbs';

export async function toggleJoyxoff(): Promise<boolean> {
  if (await fs.exists(JOYXOFF_VBS).catch(() => false)) {
    await shell.hidden('wscript.exe', ['//nologo', JOYXOFF_VBS]).catch(() => {});
  } else {
    await shell.run('cmd', ['/c', JOYXOFF_BAT]).catch(() => {});
  }
  for (let i = 0; i < 8; i++) {
    await new Promise((resolve) => setTimeout(resolve, 250));
    if (await isJoyxoffRunning()) return true;
  }
  return false;
}

export type MouseBackend = 'gamebar' | 'joyxoff';

export interface MouseModeState {
  ok: boolean;
  backend: MouseBackend;
  on: boolean;
  joyxoffOn?: boolean;
  gamebarOn?: boolean;
  joyxoffAvailable?: boolean;
  gamebarAvailable?: boolean;
  error?: string;
}

export async function getMouseModeState(): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.get', {});
}

export async function setMouseBackend(backend: MouseBackend): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.setBackend', { backend });
}

export async function toggleMouseMode(): Promise<MouseModeState> {
  return invoke<MouseModeState>('mouseMode.toggle', {});
}
