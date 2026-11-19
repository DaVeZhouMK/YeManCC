import { app, fs, shell } from './api';
import { getLockedGameTarget, validateLockedGameTarget, type LockedGameTarget } from './gameQuickSession';
import { getSettingsGeneration } from './settingsRepository';
import { assertTrainerPowerReady, getTrainerLifecycleEpoch, subscribeTrainerLifecycle } from './trainerLifecycle';
import { reportFrontendError } from '@/robust/frontendDiagnostics';

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
class CeReleasePendingError extends Error {}
const SESSION_IO_MS = 5000;
function object(value: unknown): value is Record<string, any> { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function validTarget(value: unknown): value is LockedGameTarget {
  const v = value as LockedGameTarget;
  return !!v && Number.isSafeInteger(v.pid) && v.pid > 4 && v.pid <= 2147483647 && typeof v.processCreated === 'string'
    && /^[1-9][0-9]{0,19}$/.test(v.processCreated) && typeof v.path === 'string' && /^(?:[a-z]:[\\/]|\\\\)/i.test(v.path) && !/[\x00-\x1f]/.test(v.path);
}
async function sessionIO<T>(operation: Promise<T>): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try { return await Promise.race([operation, new Promise<never>((_, reject) => { timer = setTimeout(() => reject(new ScanSessionFileError('数值会话操作超时，请重新打开重试')), SESSION_IO_MS); })]); }
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
interface Session { root: string; token: string; target: LockedGameTarget; generation: number; lifecycleEpoch: number; powerGeneration: number; stopLifecycle: () => void; dispatched: boolean; terminalReason: string | null; state: GameValueState | null; draft: GameValueDraft; lastSeen: number; pollBusy: boolean; leasing?: Promise<void>; timer: ReturnType<typeof setInterval> | null; queue: Promise<unknown> }
let current: Session | null = null;
let starting: { target: LockedGameTarget; generation: number; epoch: number; restart: boolean; promise: Promise<GameValueState> } | null = null;
let startEpoch = 0;
let retired: Session | null = null;
let releasing: Promise<void> | null = null;
const listeners = new Set<(state: GameValueState | null) => void>();
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
function randomHex(bytes: number): string { const value = crypto.getRandomValues(new Uint8Array(bytes)); return [...value].map(n => n.toString(16).padStart(2, '0')).join(''); }
function sameTarget(a: Pick<LockedGameTarget, 'pid' | 'processCreated' | 'path'> | null, b: Session['target']): boolean { return validTarget(a) && validTarget(b) && a.pid === b.pid && a.processCreated === b.processCreated && a.path.replace(/\//g, '\\').toLowerCase() === b.path.replace(/\//g, '\\').toLowerCase(); }
function copy(state: GameValueState | null): GameValueState | null { return state ? { ...state, rows: state.rows.map(row => ({ ...row })) } : null; }
function notify(session: Session): void { if (session !== current) return; for (const listener of [...listeners]) { try { listener(copy(session.state)); } catch { /* Observers cannot interrupt the helper. */ } } }
function assertSession(session: Session): void {
  if (session !== current || session.lifecycleEpoch !== getTrainerLifecycleEpoch() || session.generation !== getSettingsGeneration() || !sameTarget(getLockedGameTarget(), session.target) || getLockedGameTarget()?.lockGeneration !== session.target.lockGeneration) throw new Error('YMCC锁定游戏已改变，请重新打开数值搜索');
}
function assertOpenSession(session: Session): void {
  assertSession(session);
  if (session.terminalReason !== null || session.state?.stage === 'closed') throw new Error('数值会话已结束' + (session.terminalReason ? '（' + session.terminalReason + '）' : '') + '，请重新开始；若刚执行修改，请先核对游戏内数值');
}
function closedObservation(state: GameValueState, message: string): GameValueState {
  // Revoke result/write authority without claiming CE has restored its pause.
  return { ...state, stage: 'closed', message, rows: [], count: 0, scanRound: 0, searchValue: '', lastWriteCount: 0, lastWriteValue: '',
    scannedBytes: 0, totalBytes: 0, scanElapsedMs: 0, selected: '', selectedValue: null, frozen: false, frozenValue: null };
}
function accept(session: Session, value: unknown): GameValueState {
  assertOpenSession(session); // A late success never revives a terminal capability.
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
  session.lastSeen = Date.now(); if (state.stage === 'closed') session.terminalReason = state.message;
  session.state = state.stage === 'closed' ? closedObservation(state, state.message) : copy(state); notify(session); return copy(session.state)!;
}
async function lease(session: Session): Promise<void> {
  assertOpenSession(session); if (session.leasing) return session.leasing;
  if (session.state && Date.now() - session.lastSeen > 6000) throw new Error('数值会话连接已中断，请重新打开');
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
    if (session.terminalReason === null) session.terminalReason = session.state?.stage === 'closed' ? session.state.message : '数值会话状态未知，请显式重新打开搜索';
    if (session.state) session.state = closedObservation(session.state, session.terminalReason);
    if (session.timer !== null) { clearInterval(session.timer); session.timer = null; }
    notify(session); await sessionIO(fs.writeTextFileAtomic(session.root + '\\stop', 'stop', SESSION_IO_MS)).catch(() => {});
  } finally { session.pollBusy = false; }
}
export function getGameValueState(): GameValueState | null { return copy(current?.state || null); }
function draftSession(target: LockedGameTarget): Session | null {
  return current && sameTarget(target, current.target) && sameTarget(getLockedGameTarget(), current.target)
    && target.lockGeneration === current.target.lockGeneration && getLockedGameTarget()?.lockGeneration === target.lockGeneration
    && current.generation === getSettingsGeneration() && current.lifecycleEpoch === getTrainerLifecycleEpoch() && current.terminalReason === null && current.state?.stage !== 'closed' ? current : null;
}
export function getGameValueDraft(target: LockedGameTarget): GameValueDraft {
  const session = draftSession(target); return session ? { ...session.draft } : freshDraft();
}
export function saveGameValueDraft(target: LockedGameTarget, draft: GameValueDraft): void {
  const session = draftSession(target); if (!session) return;
  session.draft = { firstValue: String(draft.firstValue).slice(0, 64), nextValue: String(draft.nextValue).slice(0, 64), desiredValue: String(draft.desiredValue).slice(0, 64) };
}
export function subscribeGameValueState(listener: (state: GameValueState | null) => void): () => void { listeners.add(listener); listener(getGameValueState()); return () => listeners.delete(listener); }
async function finishRetiredSession(): Promise<void> {
  if (releasing) return releasing;
  const session = retired;
  if (!session) return;
  const operation = (async () => {
    await sessionIO(fs.writeTextFileAtomic(session.root + '\\stop', 'stop', SESSION_IO_MS));
    if (session.dispatched) {
      const until = Date.now() + 12000;
      for (;;) {
        if (!session.dispatched) break; // A pending creation may return a definitive refusal.
        const receipt = await readSessionDocument(session.root + '\\closed.json', 4096);
        if (receipt) {
          if (receipt.engine !== 'cheat-engine' || receipt.token !== session.token || !sameTarget(receipt as GameValueState, session.target) || receipt.stage !== 'closed' || typeof receipt.ceExited !== 'boolean') throw new Error('旧CE会话退出回执身份无效，请检查导出日志');
          if (!receipt.ceExited) throw new CeReleasePendingError('旧CE会话尚未确认退出，请稍后再打开');
          break;
        }
        if (Date.now() >= until) throw new CeReleasePendingError('未收到CE退出确认，请稍后再打开，避免重复启动');
        await delay(100);
      }
    }
    if (retired === session) retired = null;
  })();
  releasing = operation;
  try { await operation; } finally { if (releasing === operation) releasing = null; }
}
async function retireCurrentSession(): Promise<void> {
  const session = current; current = null;
  if (!session) return finishRetiredSession();
  retired = session;
  session.stopLifecycle();
  if (session.timer !== null) clearInterval(session.timer);
  for (const listener of [...listeners]) { try { listener(null); } catch { /* Best effort UI observation. */ } }
  // The adapter asks its owned CE to cancel and finish CE's own pause cleanup.
  await finishRetiredSession();
}
async function reconcileRetiredSession(check: () => void): Promise<void> {
  const session = retired;
  if (!session) return;
  check();
  const directory = await sessionIO(app.powerControlDir(SESSION_IO_MS)); check();
  const program = directory.replace(/[\\/]+$/, '') + '\\MemoryScan\\MemoryScanHost.exe';
  if (!(await sessionIO(fs.exists(program, SESSION_IO_MS)))) throw new Error('缺少数值搜索组件，请安装完整更新');
  check();
  const output = session.root + '\\release-' + randomHex(16) + '.json';
  // This mode only checks the exact retired process and emits an authenticated
  // release receipt. It cannot start CE, scan, write or replay game commands.
  const launch = await sessionIO(shell.hidden(program, ['--release', session.root, output], SESSION_IO_MS)); check();
  if (launch?.ok !== true) throw new Error('无法核验旧CE退出，请检查权限后重新打开');
  const until = Date.now() + 15000;
  for (;;) {
    check();
    const receipt = await readSessionDocument(output, 4096); check();
    if (receipt) {
      if (receipt.engine !== 'cheat-engine' || receipt.token !== session.token || !sameTarget(receipt as GameValueState, session.target) || typeof receipt.released !== 'boolean' || typeof receipt.message !== 'string' || !Number.isSafeInteger(receipt.at) || Date.now() - receipt.at > 15000 || receipt.at - Date.now() > 1000) throw new Error('旧CE退出核验回执无效，未启动替代会话');
      await sessionIO(fs.remove(output)).catch(() => {}); check();
      if (!receipt.released) throw new CeReleasePendingError('旧CE尚未确认退出：' + receipt.message);
      // The helper must also have committed the original closed receipt.
      // Reuse the original identity validator; never clear retired on timeout.
      await finishRetiredSession(); check();
      return;
    }
    if (Date.now() >= until) throw new CeReleasePendingError('旧CE退出核验未返回，请稍后重新打开');
    await delay(100);
  }
}
export async function stopGameValueSession(): Promise<void> {
  ++startEpoch; starting = null;
  await retireCurrentSession();
}
export interface GameValueScanAvailability { available: boolean; code: string; message: string }
function reportCeFailure(where: string, phase: string, error: unknown, code?: string): void {
  try {
    const message = String((error as Error)?.message || error).replace(/[a-f0-9]{64}/gi, '<session>');
    reportFrontendError(where, new Error(JSON.stringify({ phase, code, message })));
  } catch { /* Diagnostic storage must not replace the actual startup error. */ }
}
function unavailable(result: GameValueScanAvailability): GameValueScanAvailability {
  reportCeFailure('game-value-scan.probe', 'discovery', result.message, result.code);
  return result;
}
export async function probeGameValueScanAvailability(): Promise<GameValueScanAvailability> {
  let resultPath = '', confirmed = false;
  try {
  const [directory, data] = await sessionIO(Promise.all([app.powerControlDir(SESSION_IO_MS), app.dataDir(SESSION_IO_MS)]));
  const program = directory.replace(/[\\/]+$/, '') + '\\MemoryScan\\MemoryScanHost.exe';
  if (!(await sessionIO(fs.exists(program, SESSION_IO_MS)))) return unavailable({ available: false, code: 'adapter-missing', message: '缺少数值搜索组件，请安装完整更新' });
  resultPath = data.replace(/[\\/]+$/, '') + '\\ce-availability-' + randomHex(16) + '.json';
    if (!(await sessionIO(shell.hidden(program, ['--probe', resultPath], SESSION_IO_MS))).ok) throw new Error('无法启动CE检测组件，请检查权限');
    const until = Date.now() + 5000;
    while (Date.now() < until) {
      const result = await readSessionDocument(resultPath, 4096);
      if (result !== null) {
        if (result.engine !== 'cheat-engine' || typeof result.available !== 'boolean' || (result.code !== undefined && typeof result.code !== 'string') || (result.message !== undefined && typeof result.message !== 'string')) throw new Error('CE检测回执无效，请更新数值搜索组件');
        confirmed = true;
        const availability = { available: result.available, code: result.code || (result.available ? 'ready' : 'ce-not-found'), message: result.message || (result.available ? 'CE已找到' : '未安装CE，简单改数值功能不可用') };
        return availability.available ? availability : unavailable(availability);
      }
      await delay(100);
    }
    return unavailable({ available: false, code: 'probe-timeout', message: 'CE检测超时，请稍后重试' });
  } catch (error) { return unavailable({ available: false, code: 'probe-failed', message: '无法确认CE状态：' + String((error as Error).message || error) }); }
  finally { if (confirmed && resultPath) await sessionIO(fs.remove(resultPath)).catch(() => {}); }
}
export async function isGameValueScanAvailable(): Promise<boolean> { return (await probeGameValueScanAvailability()).available; }
export function startGameValueSession(target: LockedGameTarget, options: { restart?: boolean } = {}): Promise<GameValueState> {
  const restart = options.restart === true;
  if (!validTarget(target)) return Promise.reject(new Error('锁定游戏身份无效'));
  target = { ...target };
  const generation = getSettingsGeneration(), epoch = startEpoch, lifecycleEpoch = getTrainerLifecycleEpoch();
  if (starting && starting.restart === restart && starting.epoch === epoch && starting.generation === generation && sameTarget(target, starting.target) && target.lockGeneration === starting.target.lockGeneration) return starting.promise;
  if (starting) return Promise.reject(new Error('旧数值会话正在结束，请关闭后重新打开'));
  let transferred = false, phase = 'admission';
  const stopLifecycle = subscribeTrainerLifecycle(() => { void stopGameValueSession().catch(() => {}); });
  const check = () => {
    if (epoch !== startEpoch || lifecycleEpoch !== getTrainerLifecycleEpoch() || generation !== getSettingsGeneration() || !sameTarget(getLockedGameTarget(), target) || getLockedGameTarget()?.lockGeneration !== target.lockGeneration) throw new Error('锁定游戏或页面已改变，本次数值会话已取消');
  };
  const action = (async () => {
    check();
    const powerGeneration = await assertTrainerPowerReady(lifecycleEpoch); check();
    phase = 'discovery';
    const availability = await probeGameValueScanAvailability();
    check();
    if (!availability.available) throw new Error(availability.message);
    phase = 'target';
    const observed = await sessionIO(validateLockedGameTarget());
    check();
    if (!sameTarget(observed, target) || observed?.lockGeneration !== target.lockGeneration) throw new Error('锁定游戏已退出或改变，请重新获取游戏');
    if (!restart && current && sameTarget(target, current.target) && current.target.lockGeneration === target.lockGeneration && current.generation === getSettingsGeneration() && current.state && current.state.stage !== 'closed') return copy(current.state)!;
    phase = 'previous-session';
    try { await retireCurrentSession(); }
    catch (error) { check(); if (!(error instanceof CeReleasePendingError)) throw error; await reconcileRetiredSession(check); }
    check();
    const [directory, data, ownerId, ownerDirectory] = await sessionIO(Promise.all([app.powerControlDir(SESSION_IO_MS), app.dataDir(SESSION_IO_MS), app.pid(SESSION_IO_MS), app.exeDir(SESSION_IO_MS)]));
    if (!Number.isSafeInteger(ownerId) || ownerId <= 4 || !ownerDirectory) throw new Error('无法核验YMCC会话身份');
    const program = directory.replace(/[\\/]+$/, '') + '\\MemoryScan\\MemoryScanHost.exe';
    if (!(await sessionIO(fs.exists(program, SESSION_IO_MS)))) throw new Error('缺少数值搜索组件，请安装完整更新');
    const session: Session = { root: data.replace(/[\\/]+$/, '') + '\\memory-scan\\' + randomHex(16), token: randomHex(32), target: { ...target }, generation, lifecycleEpoch, powerGeneration, stopLifecycle, dispatched: false, terminalReason: null, state: null, draft: freshDraft(), lastSeen: Date.now(), timer: null, pollBusy: false, queue: Promise.resolve() };
    check();
    current = session;
    transferred = true;
    try {
      await sessionIO(fs.mkdir(session.root + '\\requests', SESSION_IO_MS)); await sessionIO(fs.mkdir(session.root + '\\responses', SESSION_IO_MS));
      await writeSessionDocument(session.root + '\\config.json', { token: session.token, pid: target.pid, processCreated: target.processCreated, path: target.path, ownerPid: ownerId, ownerDirectory });
      await lease(session); assertSession(session);
      await assertTrainerPowerReady(lifecycleEpoch, powerGeneration); check(); assertSession(session);
      phase = 'helper-launch';
      session.dispatched = true; // An unacknowledged dispatch cannot authorize a second CE.
      const launch = await sessionIO(shell.hidden(program, [session.root], SESSION_IO_MS, true));
      if (launch?.ok !== true) {
        if (launch?.ok === false && launch.creationReceipt === 1 && launch.processCreated === false
          && launch.errorCode === 'CREATE_PROCESS_FAILED' && Number.isSafeInteger(launch.win32Error) && Number(launch.win32Error) > 0) session.dispatched = false;
        const detail = Number.isSafeInteger(launch?.win32Error) && Number(launch.win32Error) > 0 ? `（Windows错误：${launch.win32Error}）` : '';
        throw new Error('无法启动数值搜索助手' + detail);
      }
      phase = 'ce-handshake';
      check(); assertSession(session);
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
    } catch (error) { if (current === session) await retireCurrentSession().catch(() => {}); throw error; }
  })().catch(error => { reportCeFailure('game-value-scan.start', phase, error); throw error; });
  const request = { target, generation, epoch, restart, promise: action }; starting = request;
  void action.finally(() => { if (!transferred) stopLifecycle(); if (starting === request) starting = null; }).catch(() => {}); return action;
}
export function gameValueCommand(action: 'first'|'next'|'list'|'select'|'write'|'writeAll'|'freeze'|'unfreeze', values: { value?: string; dataType?: GameValueType; address?: string; offset?: number } = {}): Promise<GameValueState> {
  const session = current;
  if (!session) return Promise.reject(new Error('请先锁定游戏并打开数值搜索'));
  const operation = session.queue.catch(() => {}).then(async () => {
    assertOpenSession(session);
    try { await assertTrainerPowerReady(session.lifecycleEpoch, session.powerGeneration); }
    catch (error) { if (current === session) await stopGameValueSession().catch(() => {}); throw error; }
    assertOpenSession(session);
    const target = await sessionIO(validateLockedGameTarget());
    assertOpenSession(session); if (!sameTarget(target, session.target)) throw new Error('游戏进程已退出或重开，请重新搜索');
    if (!session.state || session.state.stage === 'closed') throw new Error('数值会话未激活，请重新打开');
    if (!['first','next','writeAll'].includes(action) || typeof values.value !== 'string' || !/^[+-]?\d{1,11}$/.test(values.value) || !Number.isInteger(Number(values.value)) || Number(values.value) < -2147483648 || Number(values.value) > 2147483647 || (values.dataType !== undefined && values.dataType !== 'int32') || values.address !== undefined || values.offset !== undefined) throw new Error('数值请求未授权或格式无效');
    await lease(session);
    const id = randomHex(16), resultPath = session.root + '\\responses\\' + id + '.json';
    const request = { ...values, id, token: session.token, action, revision: session.state.revision, schemaVersion: 1 };
    const pendingPath = session.root + '\\' + id + '.pending';
    assertOpenSession(session);
    await writeSessionDocument(pendingPath, request);
    assertOpenSession(session);
    // Verify an unenumerated preimage before publishing. The host may claim a
    // committed request immediately, so confirmation comes from its exact response.
    await sessionIO(fs.writeTextFileAtomic(session.root + '\\requests\\' + id + '.json', JSON.stringify(request), SESSION_IO_MS));
    assertOpenSession(session);
    const until = Date.now() + 615000;
    let confirmed = false;
    try {
      while (Date.now() < until) {
        assertOpenSession(session);
        let receipt: { id: string; token: string; ok: boolean; error: string; state: unknown } | null = null;
        receipt = await readSessionDocument(resultPath, 131072) as typeof receipt;
        assertOpenSession(session);
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
