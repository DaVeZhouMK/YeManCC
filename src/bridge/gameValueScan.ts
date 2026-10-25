import { app, fs, shell } from './api';
import { getLockedGameTarget, validateLockedGameTarget, type LockedGameTarget } from './gameQuickSession';
import { getSettingsGeneration } from './settingsRepository';

export type GameValueType = 'int32' | 'int64' | 'float' | 'double';
export interface GameValueRow { address: string; value: string | null }
export interface GameValueState {
  engine: 'cheat-engine'; totalBytes?: number; scanElapsedMs?: number; pauseByCE?: boolean;
  token: string; at: number; stage: 'idle' | 'scanning' | 'writing' | 'ready' | 'failed' | 'closed';
  message: string; revision: number; dataType: GameValueType; count: number; offset: number;
  pageSize: number; rows: GameValueRow[]; selected: string; selectedValue: string | null;
  frozen: boolean; frozenValue: string | null; scannedBytes: number; skippedBlocks: number;
  pid: number; processCreated: string; path: string;
  bulkLimit: number; scanRound: number; searchValue: string; lastWriteCount: number;
  lastWriteValue: string; temporarilyPaused: boolean; pauseRestored: boolean;
}
export interface GameValueDraft { firstValue: string; nextValue: string; desiredValue: string }
const freshDraft = (): GameValueDraft => ({ firstValue: '', nextValue: '', desiredValue: '99999' });
interface Session { root: string; token: string; target: LockedGameTarget; generation: number; state: GameValueState | null; draft: GameValueDraft; lastSeen: number; pollBusy: boolean; timer: ReturnType<typeof setInterval> | null; queue: Promise<unknown> }
let current: Session | null = null;
let starting: Promise<GameValueState> | null = null;
const listeners = new Set<(state: GameValueState | null) => void>();
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
function randomHex(bytes: number): string { const value = crypto.getRandomValues(new Uint8Array(bytes)); return [...value].map(n => n.toString(16).padStart(2, '0')).join(''); }
function sameTarget(a: Pick<LockedGameTarget, 'pid' | 'processCreated' | 'path'> | null, b: Session['target']): boolean { return !!a && a.pid === b.pid && a.processCreated === b.processCreated && a.path.replace(/\//g, '\\').toLowerCase() === b.path.replace(/\//g, '\\').toLowerCase(); }
function copy(state: GameValueState | null): GameValueState | null { return state ? { ...state, rows: state.rows.map(row => ({ ...row })) } : null; }
function notify(session: Session): void { if (session !== current) return; for (const listener of [...listeners]) { try { listener(copy(session.state)); } catch { /* Observers cannot interrupt the helper. */ } } }
function assertSession(session: Session): void {
  if (session !== current || session.generation !== getSettingsGeneration() || !sameTarget(getLockedGameTarget(), session.target)) throw new Error('YMCC锁定游戏已改变，请重新打开数值搜索');
}
function accept(session: Session, value: unknown): GameValueState {
  const state = value as GameValueState;
  if (!state || state.engine !== 'cheat-engine' || state.token !== session.token || !sameTarget(state, session.target) || !['idle','scanning','writing','ready','failed','closed'].includes(state.stage) || !Array.isArray(state.rows) || state.rows.length > 40 || !Number.isSafeInteger(state.revision) || state.revision < 0 || !Number.isSafeInteger(state.count) || state.count < 0) throw new Error('数值搜索助手返回了无效状态');
  if (state.bulkLimit !== 32 || !Number.isSafeInteger(state.scanRound) || state.scanRound < 0 || typeof state.searchValue !== 'string' || !Number.isSafeInteger(state.lastWriteCount) || state.lastWriteCount < 0 || state.lastWriteCount > 32 || typeof state.lastWriteValue !== 'string' || typeof state.temporarilyPaused !== 'boolean' || typeof state.pauseRestored !== 'boolean') throw new Error('数值搜索组件版本不一致，请安装完整更新');
  if (state.rows.some(row => !/^[0-9A-F]{16}$/.test(row.address) || (row.value !== null && typeof row.value !== 'string'))) throw new Error('数值搜索结果无效');
  if (!Number.isFinite(state.at) || Date.now() - state.at > 6000 || state.at - Date.now() > 1000 || !['int32','int64','float','double'].includes(state.dataType)) throw new Error('数值搜索状态已过期');
  session.lastSeen = Date.now(); session.state = copy(state); notify(session); return copy(state)!;
}
async function lease(session: Session): Promise<void> {
  assertSession(session);
  await fs.writeTextFileAtomic(session.root + '\\lease.json', JSON.stringify({ token: session.token, at: Date.now() }));
}
async function poll(session: Session): Promise<void> {
  if (session.pollBusy || session !== current) return;
  session.pollBusy = true;
  try {
    await lease(session);
    const raw = await fs.readTextFile(session.root + '\\status.json', 131072);
    assertSession(session); const state = accept(session, JSON.parse(raw));
    if (state.stage === 'closed' && session.timer !== null) { clearInterval(session.timer); session.timer = null; }
  } catch {
    if (session !== current || session.generation !== getSettingsGeneration() || !sameTarget(getLockedGameTarget(), session.target)) void stopGameValueSession();
    else if (Date.now() - session.lastSeen > 8000 && session.state) {
      session.state = { ...session.state, stage: 'closed', frozen: false, message: '数值搜索助手连接中断，请重新搜索' };
      if (session.timer !== null) { clearInterval(session.timer); session.timer = null; }
      notify(session); await fs.writeTextFileAtomic(session.root + '\\stop', 'stop').catch(() => {});
    }
    // A partial startup or transient IPC error is not permission to start another helper.
  } finally { session.pollBusy = false; }
}
export function getGameValueState(): GameValueState | null { return copy(current?.state || null); }
export function getGameValueDraft(target: LockedGameTarget): GameValueDraft {
  return current && sameTarget(target, current.target) ? { ...current.draft } : freshDraft();
}
export function saveGameValueDraft(target: LockedGameTarget, draft: GameValueDraft): void {
  if (!current || !sameTarget(target, current.target) || current.generation !== getSettingsGeneration()) return;
  current.draft = { firstValue: String(draft.firstValue).slice(0, 64), nextValue: String(draft.nextValue).slice(0, 64), desiredValue: String(draft.desiredValue).slice(0, 64) };
}
export function subscribeGameValueState(listener: (state: GameValueState | null) => void): () => void { listeners.add(listener); listener(getGameValueState()); return () => listeners.delete(listener); }
export async function stopGameValueSession(): Promise<void> {
  const session = current; current = null;
  if (!session) return;
  if (session.timer !== null) clearInterval(session.timer);
  for (const listener of [...listeners]) { try { listener(null); } catch { /* Best effort UI observation. */ } }
  // The adapter asks its owned CE to cancel and finish CE's own pause cleanup.
  await fs.writeTextFileAtomic(session.root + '\\stop', 'stop').catch(() => {});
}
export async function isGameValueScanAvailable(): Promise<boolean> {
  const [directory, data] = await Promise.all([app.powerControlDir(), app.dataDir()]);
  const program = directory.replace(/[\\/]+$/, '') + '\\MemoryScan\\MemoryScanHost.exe';
  if (!(await fs.exists(program))) return false;
  const resultPath = data.replace(/[\\/]+$/, '') + '\\ce-availability-' + randomHex(16) + '.json';
  try {
    if (!(await shell.hidden(program, ['--probe', resultPath])).ok) return false;
    const until = Date.now() + 5000;
    while (Date.now() < until) {
      try { const result = JSON.parse(await fs.readTextFile(resultPath, 4096)); return result.engine === 'cheat-engine' && result.available === true; } catch { /* Await this capability probe. */ }
      await delay(100);
    }
    return false;
  } finally { await fs.remove(resultPath).catch(() => {}); }
}
export function startGameValueSession(target: LockedGameTarget): Promise<GameValueState> {
  if (starting) return starting.then(state => { if (!sameTarget(state, target)) throw new Error('锁定游戏已改变'); return state; });
  const action = (async () => {
    if (!(await isGameValueScanAvailable())) throw new Error('未安装CE，简单改数值功能不可用');
    const observed = await validateLockedGameTarget();
    if (!sameTarget(observed, target)) throw new Error('锁定游戏已退出或改变，请重新获取游戏');
    if (current && sameTarget(target, current.target) && current.generation === getSettingsGeneration() && current.state && current.state.stage !== 'closed') return copy(current.state)!;
    await stopGameValueSession();
    const generation = getSettingsGeneration();
    const [directory, data, ownerId, ownerDirectory] = await Promise.all([app.powerControlDir(), app.dataDir(), app.pid(), app.exeDir()]);
    if (!Number.isSafeInteger(ownerId) || ownerId <= 4 || !ownerDirectory) throw new Error('无法核验YMCC会话身份');
    const program = directory.replace(/[\\/]+$/, '') + '\\MemoryScan\\MemoryScanHost.exe';
    if (!(await fs.exists(program))) throw new Error('缺少数值搜索组件，请安装完整更新');
    const session: Session = { root: data.replace(/[\\/]+$/, '') + '\\memory-scan\\' + randomHex(16), token: randomHex(32), target: { ...target }, generation, state: null, draft: freshDraft(), lastSeen: Date.now(), timer: null, pollBusy: false, queue: Promise.resolve() };
    if (generation !== getSettingsGeneration() || !sameTarget(getLockedGameTarget(), target)) throw new Error('锁定游戏已改变');
    current = session;
    try {
      await fs.mkdir(session.root + '\\requests'); await fs.mkdir(session.root + '\\responses');
      await fs.writeTextFileAtomic(session.root + '\\config.json', JSON.stringify({ token: session.token, pid: target.pid, processCreated: target.processCreated, path: target.path, ownerPid: ownerId, ownerDirectory }));
      await lease(session); assertSession(session);
      if (!(await shell.hidden(program, [session.root])).ok) throw new Error('无法启动数值搜索助手');
      session.timer = setInterval(() => { void poll(session); }, 1000);
      const until = Date.now() + 25000;
      while (Date.now() < until) {
        assertSession(session);
        let state: GameValueState | null = null;
        try { state = accept(session, JSON.parse(await fs.readTextFile(session.root + '\\status.json', 131072))); } catch { /* Await the single helper startup receipt. */ }
        if (state) { if (state.stage === 'closed') throw new Error(state.message); return state; }
        await delay(200);
      }
      throw new Error('数值搜索助手启动超时，请检查权限');
    } catch (error) { if (current === session) await stopGameValueSession(); throw error; }
  })();
  starting = action; void action.finally(() => { if (starting === action) starting = null; }).catch(() => {}); return action;
}
export function gameValueCommand(action: 'first'|'next'|'list'|'select'|'write'|'writeAll'|'freeze'|'unfreeze', values: { value?: string; dataType?: GameValueType; address?: string; offset?: number } = {}): Promise<GameValueState> {
  const session = current;
  if (!session) return Promise.reject(new Error('请先锁定游戏并打开数值搜索'));
  const operation = session.queue.catch(() => {}).then(async () => {
    assertSession(session);
    const target = await validateLockedGameTarget();
    assertSession(session); if (!sameTarget(target, session.target)) throw new Error('游戏进程已退出或重开，请重新搜索');
    await lease(session);
    const id = randomHex(16), resultPath = session.root + '\\responses\\' + id + '.json';
    const request = { ...values, id, token: session.token, action, revision: session.state?.revision || 0 };
    assertSession(session);
    await fs.writeTextFileAtomic(session.root + '\\requests\\' + id + '.json', JSON.stringify(request));
    const until = Date.now() + 615000;
    try {
      while (Date.now() < until) {
        assertSession(session);
        let receipt: { id: string; token: string; ok: boolean; error: string; state: unknown } | null = null;
        try { receipt = JSON.parse(await fs.readTextFile(resultPath, 131072)); } catch { /* Await the exact request receipt. */ }
        if (receipt) {
          if (receipt.id !== id || receipt.token !== session.token) throw new Error('数值搜索回执不匹配');
          const state = accept(session, receipt.state);
          if (!receipt.ok) throw new Error(receipt.error || state.message); return state;
        }
        if (session.state?.stage === 'closed') throw new Error(session.state.message);
        await delay(250);
      }
      await stopGameValueSession(); throw new Error('未收到操作确认，会话已停止，请先核对游戏内数值');
    } finally { await fs.remove(resultPath).catch(() => {}); }
  });
  session.queue = operation; return operation;
}
export async function cancelGameValueScan(): Promise<void> {
  const session = current; if (!session) return;
  await fs.writeTextFileAtomic(session.root + '\\cancel', 'cancel');
}
