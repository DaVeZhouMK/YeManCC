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
class ScanSessionFileError extends Error {}
const SESSION_IO_MS = 5000;
function object(value: unknown): value is Record<string, any> { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function validTarget(value: unknown): value is LockedGameTarget {
  const v = value as LockedGameTarget;
  return !!v && Number.isSafeInteger(v.pid) && v.pid > 4 && v.pid <= 2147483647 && typeof v.processCreated === 'string'
    && /^[1-9][0-9]{0,19}$/.test(v.processCreated) && typeof v.path === 'string' && /^(?:[a-z]:[\\/]|\\\\)/i.test(v.path) && !/[\x00-\x1f]/.test(v.path);
}
async function sessionIO<T>(operation: Promise<T>): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try { return await Promise.race([operation, new Promise<never>((_, reject) => { timer = setTimeout(() => reject(new ScanSessionFileError('数值会话文件操作超时，请显式重试')), SESSION_IO_MS); })]); }
  finally { if (timer !== undefined) clearTimeout(timer); }
}
function parseSessionDocument(raw: unknown, maximum: number): Record<string, any> {
  if (typeof raw !== 'string' || new TextEncoder().encode(raw).length > maximum) throw new ScanSessionFileError('数值会话文件超过读取限制');
  const text = raw.charCodeAt(0) === 0xfeff ? raw.slice(1) : raw;
  let at = 0;
  const fail = (): never => { throw new ScanSessionFileError('数值会话JSON损坏或版本不兼容'); };
  const space = () => { while (/[ \t\r\n]/.test(text[at] || '\0')) at++; };
  const string = (): string => {
    space(); if (text[at++] !== '"') return fail(); const begin = at - 1;
    while (at < text.length) { const c = text[at++]; if (c === '\\') { at++; continue; } if (c === '"') {
      let value: string; try { value = JSON.parse(text.slice(begin, at)); } catch { return fail(); }
      for (const c of value) { const point = c.codePointAt(0)!; if (point >= 0xd800 && point <= 0xdfff) return fail(); }
      return value;
    } }
    return fail();
  };
  const value = (depth: number): void => {
    space(); const c = text[at];
    if (c === '{' || c === '[') {
      if (depth > 16) return fail(); const end = c === '{' ? '}' : ']'; at++; space(); if (text[at] === end) { at++; return; }
      const keys = new Set<string>();
      for (;;) {
        if (c === '{') { const key = string(); if (keys.has(key)) return fail(); keys.add(key); space(); if (text[at++] !== ':') return fail(); }
        value(depth + 1); space(); const next = text[at++]; if (next === end) return; if (next !== ',') return fail();
      }
    }
    if (c === '"') { string(); return; }
    const token = /^(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/.exec(text.slice(at))?.[0];
    if (!token || (/^-?\d/.test(token) && !Number.isFinite(Number(token)))) return fail(); at += token.length;
  };
  value(0); space(); if (at !== text.length) return fail();
  let result: unknown; try { result = JSON.parse(text); } catch { return fail(); }
  if (!object(result)) return fail();
  if (Object.prototype.hasOwnProperty.call(result, 'schemaVersion') && result.schemaVersion !== 1) return fail();
  return result;
}
async function readSessionDocument(path: string, maximum: number): Promise<Record<string, any> | null> {
  const exists = await sessionIO(fs.exists(path, SESSION_IO_MS));
  if (exists === false) return null;
  if (exists !== true) throw new ScanSessionFileError('数值会话文件存在性未知');
  return parseSessionDocument(await sessionIO(fs.readTextFile(path, maximum, { bounded: true, timeoutMs: SESSION_IO_MS })), maximum);
}
async function writeSessionDocument(path: string, value: Record<string, unknown>): Promise<void> {
  const text = JSON.stringify({ ...value, schemaVersion: 1 });
  await sessionIO(fs.writeTextFileAtomic(path, text, SESSION_IO_MS));
  const readback = await sessionIO(fs.readTextFile(path, 16384, { bounded: true, timeoutMs: SESSION_IO_MS }));
  if (readback !== text) throw new ScanSessionFileError('数值会话写入未确认，未授权后续操作');
}

const freshDraft = (): GameValueDraft => ({ firstValue: '', nextValue: '', desiredValue: '99999' });
interface Session { root: string; token: string; target: LockedGameTarget; generation: number; state: GameValueState | null; draft: GameValueDraft; lastSeen: number; pollBusy: boolean; leasing?: Promise<void>; timer: ReturnType<typeof setInterval> | null; queue: Promise<unknown> }
let current: Session | null = null;
let starting: Promise<GameValueState> | null = null;
const listeners = new Set<(state: GameValueState | null) => void>();
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
function randomHex(bytes: number): string { const value = crypto.getRandomValues(new Uint8Array(bytes)); return [...value].map(n => n.toString(16).padStart(2, '0')).join(''); }
function sameTarget(a: Pick<LockedGameTarget, 'pid' | 'processCreated' | 'path'> | null, b: Session['target']): boolean { return validTarget(a) && validTarget(b) && a.pid === b.pid && a.processCreated === b.processCreated && a.path.replace(/\//g, '\\').toLowerCase() === b.path.replace(/\//g, '\\').toLowerCase(); }
function copy(state: GameValueState | null): GameValueState | null { return state ? { ...state, rows: state.rows.map(row => ({ ...row })) } : null; }
function notify(session: Session): void { if (session !== current) return; for (const listener of [...listeners]) { try { listener(copy(session.state)); } catch { /* Observers cannot interrupt the helper. */ } } }
function assertSession(session: Session): void {
  if (session !== current || session.generation !== getSettingsGeneration() || !sameTarget(getLockedGameTarget(), session.target) || getLockedGameTarget()?.lockGeneration !== session.target.lockGeneration) throw new Error('YMCC锁定游戏已改变，请重新打开数值搜索');
}
function accept(session: Session, value: unknown): GameValueState {
  const state = value as GameValueState;
  if (!object(value) || (value.schemaVersion !== undefined && value.schemaVersion !== 1)) throw new ScanSessionFileError('数值搜索状态版本不兼容');
  for (const key of ['at','revision','count','offset','pageSize','scannedBytes','skippedBlocks','scanRound','lastWriteCount']) if (!Number.isSafeInteger(value[key]) || value[key] < 0) throw new ScanSessionFileError('数值搜索状态数字无效');
  if (typeof value.message !== 'string' || value.message.length > 4096 || value.offset !== 0 || value.pageSize !== 40 || value.selected !== '' || value.selectedValue !== null || value.frozen !== false || value.frozenValue !== null
    || (session.state && value.revision < session.state.revision)) throw new ScanSessionFileError('数值搜索状态或代次无效');
  for (const key of ['totalBytes','scanElapsedMs']) if (value[key] !== undefined && (!Number.isSafeInteger(value[key]) || value[key] < 0)) throw new ScanSessionFileError('数值搜索进度无效');
  if (!state || state.engine !== 'cheat-engine' || state.token !== session.token || !sameTarget(state, session.target) || !['idle','scanning','writing','ready','failed','closed'].includes(state.stage) || !Array.isArray(state.rows) || state.rows.length > 40 || !Number.isSafeInteger(state.revision) || state.revision < 0 || !Number.isSafeInteger(state.count) || state.count < 0) throw new Error('数值搜索助手返回了无效状态');
  if (state.bulkLimit !== 32 || !Number.isSafeInteger(state.scanRound) || state.scanRound < 0 || typeof state.searchValue !== 'string' || !Number.isSafeInteger(state.lastWriteCount) || state.lastWriteCount < 0 || state.lastWriteCount > 32 || typeof state.lastWriteValue !== 'string' || typeof state.temporarilyPaused !== 'boolean' || typeof state.pauseRestored !== 'boolean') throw new Error('数值搜索组件版本不一致，请安装完整更新');
  if (state.count < state.rows.length || state.rows.some(row => !object(row) || typeof row.address !== 'string' || !/^[0-9A-F]{16}$/.test(row.address) || (row.value !== null && typeof row.value !== 'string'))) throw new Error('数值搜索结果无效');
  if (!Number.isFinite(state.at) || Date.now() - state.at > 6000 || state.at - Date.now() > 1000 || !['int32','int64','float','double'].includes(state.dataType)) throw new Error('数值搜索状态已过期');
  session.lastSeen = Date.now(); session.state = copy(state); notify(session); return copy(state)!;
}
async function lease(session: Session): Promise<void> {
  assertSession(session); if (session.leasing) return session.leasing;
  const operation = writeSessionDocument(session.root + '\\lease.json', { token: session.token, at: Date.now() }).then(() => assertSession(session));
  session.leasing = operation;
  try { await operation; } finally { if (session.leasing === operation) session.leasing = undefined; }
}
async function poll(session: Session): Promise<void> {
  if (session.pollBusy || session !== current) return;
  session.pollBusy = true;
  try {
    await lease(session);
    const raw = await readSessionDocument(session.root + '\\status.json', 131072);
    assertSession(session);
    if (raw === null) { if (!session.state && Date.now() - session.lastSeen <= 25000) return; throw new ScanSessionFileError('数值搜索状态暂缺'); }
    const state = accept(session, raw);
    if (state.stage === 'closed' && session.timer !== null) { clearInterval(session.timer); session.timer = null; }
  } catch {
    if (session !== current) return; // A late old read never stops its replacement.
    if (session.state) session.state = { ...session.state, stage: 'closed', rows: [], count: 0, frozen: false, message: '数值会话状态未知，请显式重新打开搜索' };
    if (session.timer !== null) { clearInterval(session.timer); session.timer = null; }
    notify(session); await sessionIO(fs.writeTextFileAtomic(session.root + '\\stop', 'stop', SESSION_IO_MS)).catch(() => {});
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
  await sessionIO(fs.writeTextFileAtomic(session.root + '\\stop', 'stop', SESSION_IO_MS)).catch(() => {});
}
export async function isGameValueScanAvailable(): Promise<boolean> {
  const [directory, data] = await Promise.all([app.powerControlDir(), app.dataDir()]);
  const program = directory.replace(/[\\/]+$/, '') + '\\MemoryScan\\MemoryScanHost.exe';
  if (!(await fs.exists(program))) return false;
  const resultPath = data.replace(/[\\/]+$/, '') + '\\ce-availability-' + randomHex(16) + '.json';
  let confirmed = false;
  try {
    if (!(await shell.hidden(program, ['--probe', resultPath])).ok) return false;
    const until = Date.now() + 5000;
    while (Date.now() < until) {
      const result = await readSessionDocument(resultPath, 4096);
      if (result !== null) {
        if (result.engine !== 'cheat-engine' || typeof result.available !== 'boolean') return false;
        confirmed = true; return result.available;
      }
      await delay(100);
    }
    return false;
  } catch { return false; }
  finally { if (confirmed) await sessionIO(fs.remove(resultPath)).catch(() => {}); }
}
export function startGameValueSession(target: LockedGameTarget): Promise<GameValueState> {
  if (!validTarget(target)) return Promise.reject(new Error('锁定游戏身份无效'));
  if (starting) return starting.then(state => { if (!sameTarget(state, target)) throw new Error('锁定游戏已改变'); return state; });
  const action = (async () => {
    if (!(await isGameValueScanAvailable())) throw new Error('未安装CE，简单改数值功能不可用');
    const observed = await validateLockedGameTarget();
    if (!sameTarget(observed, target)) throw new Error('锁定游戏已退出或改变，请重新获取游戏');
    if (current && sameTarget(target, current.target) && current.target.lockGeneration === target.lockGeneration && current.generation === getSettingsGeneration() && current.state && current.state.stage !== 'closed') return copy(current.state)!;
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
      await writeSessionDocument(session.root + '\\config.json', { token: session.token, pid: target.pid, processCreated: target.processCreated, path: target.path, ownerPid: ownerId, ownerDirectory });
      await lease(session); assertSession(session);
      if (!(await shell.hidden(program, [session.root])).ok) throw new Error('无法启动数值搜索助手');
      session.timer = setInterval(() => { void poll(session); }, 1000);
      const until = Date.now() + 25000;
      while (Date.now() < until) {
        assertSession(session);
        let state: GameValueState | null = null;
        const raw = await readSessionDocument(session.root + '\\status.json', 131072); assertSession(session);
        if (raw !== null) state = accept(session, raw);
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
    if (!session.state || session.state.stage === 'closed') throw new Error('数值会话未激活，请重新打开');
    if (!['first','next','writeAll'].includes(action) || typeof values.value !== 'string' || !/^[+-]?\d{1,11}$/.test(values.value) || !Number.isInteger(Number(values.value)) || Number(values.value) < -2147483648 || Number(values.value) > 2147483647 || (values.dataType !== undefined && values.dataType !== 'int32') || values.address !== undefined || values.offset !== undefined) throw new Error('数值请求未授权或格式无效');
    await lease(session);
    const id = randomHex(16), resultPath = session.root + '\\responses\\' + id + '.json';
    const request = { ...values, id, token: session.token, action, revision: session.state.revision, schemaVersion: 1 };
    const pendingPath = session.root + '\\' + id + '.pending';
    assertSession(session);
    await writeSessionDocument(pendingPath, request);
    assertSession(session);
    // Verify an unenumerated preimage before publishing. The host may claim a
    // committed request immediately, so confirmation comes from its exact response.
    await sessionIO(fs.writeTextFileAtomic(session.root + '\\requests\\' + id + '.json', JSON.stringify(request), SESSION_IO_MS));
    assertSession(session);
    const until = Date.now() + 615000;
    let confirmed = false;
    try {
      while (Date.now() < until) {
        assertSession(session);
        let receipt: { id: string; token: string; ok: boolean; error: string; state: unknown } | null = null;
        receipt = await readSessionDocument(resultPath, 131072) as typeof receipt;
        assertSession(session);
        if (receipt) {
          if (receipt.id !== id || receipt.token !== session.token) throw new Error('数值搜索回执不匹配');
          if (typeof receipt.ok !== 'boolean' || typeof receipt.error !== 'string') throw new ScanSessionFileError('数值搜索确认类型无效');
          if (!receipt.ok) { confirmed = true; throw new Error(receipt.error || '数值操作未确认成功'); }
          const state = accept(session, receipt.state); confirmed = true; return state;
        }
        const latestState = getGameValueState(); // The timer may close it while IO awaits.
        if (latestState?.stage === 'closed') throw new Error(latestState.message);
        await delay(250);
      }
      await stopGameValueSession(); throw new Error('未收到操作确认，会话已停止，请先核对游戏内数值');
    } finally { if (confirmed) { await sessionIO(fs.remove(resultPath)).catch(() => {}); await sessionIO(fs.remove(pendingPath)).catch(() => {}); } }
  });
  session.queue = operation; return operation;
}
export async function cancelGameValueScan(): Promise<void> {
  const session = current; if (!session) return;
  await sessionIO(fs.writeTextFileAtomic(session.root + '\\cancel', 'cancel', SESSION_IO_MS));
}
