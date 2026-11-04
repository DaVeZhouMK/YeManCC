import { steamAppearance, STEAM_APPEARANCE_THEMES, type AppearanceSnapshot, type AppearanceResult, type AppearanceThemeName } from './steamAppearance';
export const APPEARANCE_DEBOUNCE_MS = 3000;
export const APPEARANCE_DRAFT_MAX_AGE_MS = 7 * 24 * 60 * 60 * 1000;
const DRAFT_MAX_BYTES = 65536;
type JournalStatus = 'idle' | 'saved' | 'unavailable' | 'invalid' | 'future' | 'conflict';
class DraftJournalError extends Error { constructor(readonly status: 'invalid' | 'future', message: string, readonly archiveAllowed = true) { super(message); } }

type Changes = Partial<Record<AppearanceThemeName, boolean>>;
type Phase = 'idle' | 'pending' | 'saving' | 'error' | 'restored' | 'saved-unconfirmed' | 'resetting';
export interface AppearanceDraftState {
  snapshot: AppearanceSnapshot | null; changes: Changes; pendingCount: number;
  saving: boolean; locked: boolean; phase: Phase; message: string; journalStatus: JournalStatus; journalMessage: string;
}
interface Storage { getItem(key: string): string | null; setItem(key: string, value: string): void; removeItem(key: string): void; }
interface Dependencies {
  reconcileExisting?(snapshot: AppearanceSnapshot): Promise<AppearanceResult>;
  read(): Promise<AppearanceSnapshot>; apply(snapshot: AppearanceSnapshot, states: Changes): Promise<AppearanceResult>;
  refresh(): Promise<AppearanceResult>; reset(mode: 'preset' | 'previous'): Promise<AppearanceResult>;
  now(): number; later(work: () => void, ms: number): any; cancel(timer: any): void; storage?: Storage;
}
type Entry = { value: boolean; sequence: number };
const names = STEAM_APPEARANCE_THEMES.map(theme => theme.name);
const clone = (s: AppearanceSnapshot): AppearanceSnapshot => ({ ...s, themes: s.themes.map(row => ({ ...row })) });
const target = (s: AppearanceSnapshot | null) => typeof s?.targetId === 'string' && /^[a-f0-9]{16}$/i.test(s.targetId) ? s.targetId.toLowerCase() : '';
const errorText = (e: unknown) => e instanceof Error ? e.message : String(e);

function parseDraftJournal(raw: string, id: string, now: number): Record<string, any> {
  const invalid = (): never => { throw new DraftJournalError('invalid', '本地草稿损坏或过期，原文已保留'); };
  if (typeof raw !== 'string' || raw.length > DRAFT_MAX_BYTES) throw new DraftJournalError('invalid', '本地草稿超限，未改写原文', false);
  let bytes = 0;
  for (const c of raw) { const n = c.codePointAt(0)!; if (n >= 0xd800 && n <= 0xdfff) return invalid(); bytes += n < 128 ? 1 : n < 2048 ? 2 : n < 65536 ? 3 : 4; }
  if (bytes > DRAFT_MAX_BYTES) throw new DraftJournalError('invalid', '本地草稿超限，未改写原文', false);
  const text = raw.charCodeAt(0) === 0xfeff ? raw.slice(1) : raw; let at = 0;
  const space = () => { while (/[ \t\r\n]/.test(text[at] || '\0')) ++at; };
  const string = (): string => {
    space(); if (text[at++] !== '"') return invalid(); const begin = at - 1;
    while (at < text.length) { const c = text[at++]; if (c === '\\') { ++at; continue; } if (c === '"') {
      let v: string; try { v = JSON.parse(text.slice(begin, at)); } catch { return invalid(); }
      for (const c of v) { const n = c.codePointAt(0)!; if (n >= 0xd800 && n <= 0xdfff) return invalid(); } return v;
    } } return invalid();
  };
  const value = (depth: number): void => {
    space(); const c = text[at];
    if (c === '{' || c === '[') {
      if (depth > 16) throw new DraftJournalError('invalid', '本地草稿深度超限，未改写原文', false); const end = c === '{' ? '}' : ']'; ++at; space(); if (text[at] === end) { ++at; return; }
      const keys = new Set<string>();
      for (;;) { if (c === '{') { const name = string(); if (keys.has(name)) return invalid(); keys.add(name); space(); if (text[at++] !== ':') return invalid(); } value(depth + 1); space(); const n = text[at++]; if (n === end) return; if (n !== ',') return invalid(); }
    }
    if (c === '"') { string(); return; }
    const token = /^(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/.exec(text.slice(at))?.[0];
    if (!token || (/^-?\d/.test(token) && !Number.isFinite(Number(token)))) return invalid(); at += token.length;
  };
  value(0); space(); if (at !== text.length) return invalid();
  let doc: Record<string, any>; try { doc = JSON.parse(text); } catch { return invalid(); }
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) return invalid();
  if (Number.isSafeInteger(doc.version) && doc.version > 1 || Number.isSafeInteger(doc.schemaVersion) && doc.schemaVersion > 1) throw new DraftJournalError('future', '本地草稿版本较新，未改写原文');
  if (doc.version !== 1 || doc.schemaVersion !== undefined && doc.schemaVersion !== 1 || typeof doc.targetId !== 'string' || doc.targetId.toLowerCase() !== id || !/^[a-f0-9]{16}$/i.test(doc.targetId) || !doc.states || typeof doc.states !== 'object' || Array.isArray(doc.states) || Object.values(doc.states).some(v => typeof v !== 'boolean')) return invalid();
  if (!Number.isSafeInteger(now) || now < 0 || doc.savedAt !== undefined && (!Number.isSafeInteger(doc.savedAt) || doc.savedAt < 0 || doc.savedAt > now || now - doc.savedAt > APPEARANCE_DRAFT_MAX_AGE_MS)) return invalid();
  return doc; // Missing savedAt is legacy display-only data, never an operation receipt.
}

export function createAppearanceDraft(deps: Dependencies) {
  let snapshot: AppearanceSnapshot | null = null, entries: Partial<Record<AppearanceThemeName, Entry>> = {};
  let phase: Phase = 'idle', message = '', saving = false, locked = false, sequence = 0, writeEpoch = 0;
  let boundTarget = '', dueAt = Infinity, timer: any = null, task: Promise<void> | null = null, readTask: Promise<void> | null = null;
  let reapply = false, readRequested = false, failed = false;
  let journalStatus: JournalStatus = deps.storage ? 'idle' : 'unavailable', journalMessage = '';
  const journalBaselines = new Map<string, string | null>(), journalOwned = new Map<string, Set<string>>();
  const owned = () => { let names = journalOwned.get(boundTarget); if (!names) journalOwned.set(boundTarget, names = new Set()); return names; };
  const restoredTargets = new Set<string>(), listeners = new Set<(state: AppearanceDraftState) => void>();
  const changes = (): Changes => Object.fromEntries(Object.entries(entries).map(([name, entry]) => [name, entry!.value]));
  const pending = () => Object.keys(entries).length;
  const key = (id: string) => 'YMCC.steamAppearanceDraft.v1:' + id;
  function state(): AppearanceDraftState { return { snapshot: snapshot ? clone(snapshot) : null, changes: changes(), pendingCount: pending(), saving, locked, phase, message, journalStatus, journalMessage }; }
  function notify() { const value = state(); for (const listener of listeners) { try { listener(value); } catch {} } }
  function clearTimer() { if (timer !== null) deps.cancel(timer); timer = null; }

  function journalFault(status: JournalStatus, text: string) { journalStatus = status; journalMessage = text; }
  function persist(explicit = false, touch = true) {
    if (!boundTarget || !deps.storage) return;
    const storage = deps.storage, path = key(boundTarget);
    try {
      const raw = storage.getItem(path);
      if (raw !== null && typeof raw !== 'string') throw new Error('本地草稿读取类型未知');
      if ((!journalBaselines.has(boundTarget) || journalBaselines.get(boundTarget) !== raw) && !explicit) { journalFault('conflict', '本地草稿已更替，未覆盖新选择；下次显式操作可重试'); return; }
      let previous: Record<string, any> | null = null;
      if (raw !== null) {
        try { previous = parseDraftJournal(raw, boundTarget, deps.now()); }
        catch (error) {
          if (error instanceof DraftJournalError && error.status === 'future') { journalFault('future', error.message); return; }
          if (!explicit || error instanceof DraftJournalError && !error.archiveAllowed || raw.length > DRAFT_MAX_BYTES * 2) { journalFault('invalid', '本地草稿无效，原文已保留'); return; }
          // One bounded exact-original archive. Never displace another unknown archive.
          const rejected = path + '.rejected', archive = storage.getItem(rejected);
          if (archive !== null && archive !== raw) throw new Error('已有不同草稿原件，未覆盖归档');
          if (archive === null) storage.setItem(rejected, raw);
          if (storage.getItem(rejected) !== raw) throw new Error('草稿原件归档未确认');
        }
      }
      if (explicit && journalBaselines.get(boundTarget) !== raw) for (const name of owned()) if (!entries[name as AppearanceThemeName]) owned().delete(name);
      const states: Record<string, boolean> = { ...(previous?.states || {}) };
      delete states['Game Cover Reflections'];
      for (const name of owned()) delete states[name];
      Object.assign(states, changes());
      const extras = previous && Object.keys(previous).some(name => !['version','schemaVersion','targetId','states','savedAt'].includes(name));
      const next = Object.keys(states).length || extras ? JSON.stringify({ ...previous, version: 1, targetId: boundTarget, states, savedAt: !touch && previous?.savedAt !== undefined ? previous.savedAt : deps.now() }) : null;
      if (next !== null) parseDraftJournal(next, boundTarget, deps.now());
      if (storage.getItem(path) !== raw) { journalFault('conflict', '本地草稿在写入前已更替，未覆盖新原文'); return; }
      if (next !== raw) { if (next === null) storage.removeItem(path); else storage.setItem(path, next); }
      if (storage.getItem(path) !== next) { journalFault('conflict', '本地草稿写后回读未确认'); return; }
      journalBaselines.set(boundTarget, next); for (const name of owned()) if (!entries[name as AppearanceThemeName]) owned().delete(name); restoredTargets.add(boundTarget); journalFault('saved', '本地草稿已确认；不代表外观已应用');
    } catch (error) { journalFault('unavailable', '本地草稿未确认：' + errorText(error)); }
  }
  function restoreJournal() {
    if (!boundTarget || pending() || !deps.storage) return;
    try {
      const text = deps.storage.getItem(key(boundTarget));
      if (text !== null && typeof text !== 'string') throw new Error('本地草稿读取类型未知');
      if (restoredTargets.has(boundTarget) && journalBaselines.get(boundTarget) === text) return;
      journalBaselines.set(boundTarget, text);
      if (text === null) { restoredTargets.add(boundTarget); journalFault('idle', ''); return; }
      const value = parseDraftJournal(text, boundTarget, deps.now()); owned().clear();
      for (const name of names) if (typeof value.states[name] === 'boolean') {
        owned().add(name);
        if (snapshot?.themes.find(row => row.name === name)?.configured !== value.states[name]) entries[name] = { value: value.states[name], sequence: ++sequence };
      }
      restoredTargets.add(boundTarget); journalFault('idle', '');
      if (Object.prototype.hasOwnProperty.call(value.states, 'Game Cover Reflections')) persist(false, false);
      if (pending()) { phase = 'restored'; message = '已恢复上次未提交的选择；点击重新应用确认。不会自动启动 Steam 或切换模式。'; dueAt = Infinity; }
    } catch (error) { journalFault(error instanceof DraftJournalError ? error.status : 'unavailable', errorText(error)); }
    // Faults retry only on a subsequent explicit read/edit, never a timer.
  }
  function receive(value: AppearanceSnapshot, restore = true) {
    const id = target(value);
    if (boundTarget && boundTarget !== id && pending()) {
      persist(); entries = {}; clearTimer(); reapply = false; failed = true;
      phase = 'error'; message = '加载器主题目录已改变；旧目录的未提交选择已保留，未写入新目录，请重新选择。';
    }
    if (boundTarget !== id) { restoredTargets.delete(id); journalFault(id ? 'idle' : 'unavailable', id ? '' : '主题目录身份未知，未读取草稿'); }
    snapshot = clone(value); boundTarget = id;
    if (restore) restoreJournal();
  }
  function schedule() {
    clearTimer();
    if (locked || failed || !pending() || !Number.isFinite(dueAt)) return;
    timer = deps.later(() => { timer = null; void kick(); }, Math.max(0, dueAt - deps.now()));
  }
  async function read() {
    if (saving || locked || pending()) { readRequested = true; return; }
    if (readTask) return readTask;
    const epoch = writeEpoch;
    readTask = (async () => {
      try {
        const value = await deps.read();
        if (epoch !== writeEpoch || saving || locked || pending()) return;
        receive(value);
        if (pending()) return; // Restored or newly entered user intent wins.
        failed = false; phase = 'idle'; message = '';
        if (deps.reconcileExisting && value.steamRunning && value.backendReady && value.packOwned && !value.packError &&
            (!value.configuredMatchesBackend || value.packRepairRequired || value.backendReloadRequired)) {
          saving = true; ++writeEpoch; notify();
          const repair = (async () => {
            try {
              const result = await deps.reconcileExisting!(value);
              if (result.snapshot) receive(result.snapshot, false);
              message = pending() ? '已选择 · 当前检查完成后应用最新选择' : result.message || '';
              phase = pending() ? 'pending' : result.ok ? 'idle' : 'saved-unconfirmed';
            } catch (error) {
              if (!pending()) { phase = 'saved-unconfirmed'; message = '配置检查未确认：' + errorText(error); }
            } finally { saving = false; ++writeEpoch; }
          })();
          task = repair.finally(() => { task = null; notify(); if (pending()) schedule(); });
          await task;
        }
      }
      catch (error) { if (epoch === writeEpoch && !saving && !locked) { if (!pending()) snapshot = null; phase = 'error'; message = errorText(error); } }
      finally {
        readTask = null; notify();
        // Existing read events may repair one observed mismatch. No timer,
        // CSS/Steam launch, draft replay or background search is scheduled.
      }
    })();
    return readTask;
  }
  async function submit() {
    const submitted = Object.fromEntries(Object.entries(entries).map(([name, entry]) => [name, { ...entry! }])) as Partial<Record<AppearanceThemeName, Entry>>;
    const id = boundTarget;
    const current = await deps.read();
    if (!id || target(current) !== id) { receive(current, false); throw new Error('加载器主题目录已改变，未跨目录提交旧选择'); }
    receive(current, false); persist();
    const patch: Changes = {};
    for (const name of names) if (submitted[name] && current.themes.find(row => row.name === name)?.configured !== submitted[name]!.value) patch[name] = submitted[name]!.value;
    const result: AppearanceResult = Object.keys(patch).length ? await deps.apply(current, patch) : { ok: true, saved: true, snapshot: current, message: '选择已与配置一致，无需重复写入' };
    if (!result.snapshot) throw new Error('后台未返回可校验状态，未丢弃选择');
    if (result.snapshot && id && target(result.snapshot) !== id) { receive(result.snapshot, false); throw new Error('加载器返回了不同主题目录，未确认旧选择'); }
    if (result.snapshot) receive(result.snapshot, false);
    const acknowledged = (result.ok || result.saved === true) && !!result.snapshot;
    if (acknowledged) for (const name of names) {
      if (submitted[name] && entries[name]?.sequence === submitted[name]!.sequence && result.snapshot!.themes.find(row => row.name === name)?.configured === submitted[name]!.value) delete entries[name];
    }
    persist();
    if (names.some(name => submitted[name] && entries[name]?.sequence === submitted[name]!.sequence)) throw new Error(result.message || '返回状态与所选开关不一致，未丢弃选择');
    if (!result.ok) {
      if (acknowledged && !pending()) { phase = 'saved-unconfirmed'; message = result.message || '配置已保存，后台应用未确认'; }
      else throw new Error(result.message || '提交未确认，选择已保留；点击重新应用重试');
    } else { phase = pending() ? 'pending' : 'idle'; message = pending() ? '仍有新选择，停止操作 3 秒后自动应用' : result.message || '配置已保存'; }
  }
  async function run() {
    saving = true; phase = 'saving'; ++writeEpoch; notify();
    try {
      if (pending()) await submit();
      if (reapply && !pending() && !locked) {
        reapply = false;
        const result = await deps.refresh();
        if (result.snapshot) receive(result.snapshot, false);
        else receive(await deps.read(), false);
        phase = result.ok ? 'idle' : result.saved ? 'saved-unconfirmed' : 'error';
        message = result.message || (result.ok ? '已重新应用外观' : '重新应用未确认');
        if (!result.ok && !result.saved) failed = true;
      }
    } catch (error) {
      failed = true; reapply = false; phase = 'error'; message = '外观提交未确认，选择已保留：' + errorText(error); persist();
    } finally { saving = false; ++writeEpoch; notify(); }
  }
  function kick(): Promise<void> {
    if (task) return task;
    if (locked || failed || pending() && deps.now() < dueAt || !pending() && !reapply) return Promise.resolve();
    task = run().finally(() => {
      task = null;
      if (!locked && !failed && (pending() && deps.now() >= dueAt || !pending() && reapply)) void kick(); else schedule();
      if (readRequested && !pending() && !saving && !locked) { readRequested = false; void read(); }
    });
    return task;
  }
  function set(patch: Changes) {
    const items = Object.entries(patch);
    if (!items.length || items.some(([name, value]) => !names.includes(name as AppearanceThemeName) || typeof value !== 'boolean')) throw new Error('未知外观开关');
    if (locked) return;
    if (!snapshot || !boundTarget) { clearTimer(); phase = 'error'; message = '主题目录身份未知，未保存或提交选择'; notify(); return; }
    ++writeEpoch; failed = false;
    for (const [key, value] of items) {
      const name = key as AppearanceThemeName; owned().add(name);
      if (!saving && snapshot?.themes.find(row => row.name === name)?.configured === value) delete entries[name];
      else entries[name] = { value, sequence: ++sequence };
    }
    dueAt = deps.now() + APPEARANCE_DEBOUNCE_MS;
    phase = pending() ? 'pending' : 'idle'; message = pending() ? '已选择 · 停止操作 3 秒后自动应用' : '选择与当前配置一致，无需提交';
    persist(true); notify(); schedule();
  }
  function flush(reapplyRequested = true) {
    if (locked) return Promise.resolve();
    if (!snapshot || !boundTarget) { clearTimer(); phase = 'error'; message = '主题目录身份未知，未应用草稿'; notify(); return Promise.resolve(); }
    persist(true); failed = false; reapply ||= reapplyRequested; dueAt = deps.now(); clearTimer();
    if (saving) { message = '已排队应用最新选择；开关仍可继续调整'; notify(); return task || Promise.resolve(); }
    return kick();
  }
  async function reset(mode: 'preset' | 'previous') {
    if (locked) return;
    locked = true; failed = false; ++writeEpoch; clearTimer(); entries = {}; reapply = false; persist();
    phase = 'resetting'; message = '正在恢复预设，未提交的选择已取消'; notify();
    try {
      if (task) await task;
      // A superseded write may have failed while reset awaited it. The reset is
      // a new explicit operation, so an old failure cannot poison its state.
      failed = false;
      const result = await deps.reset(mode);
      // Reset can change every flag. An acknowledgement without data must not
      // leave the previous configuration displayed as the new confirmed state.
      if (!result.snapshot) snapshot = null;
      receive(result.snapshot || await deps.read(), false);
      phase = result.ok ? 'idle' : result.saved ? 'saved-unconfirmed' : 'error';
      message = result.message || (result.ok ? '已恢复外观' : '恢复外观未确认');
    } catch (error) { phase = 'error'; message = errorText(error); }
    finally { locked = false; saving = false; ++writeEpoch; notify(); if (readRequested) { readRequested = false; void read(); } }
  }
  return { getState: state, read, set, flush, reset,
    subscribe(listener: (state: AppearanceDraftState) => void) { listeners.add(listener); listener(state()); return () => listeners.delete(listener); },
  };
}
// Resolve the browser port on each explicit access, so a temporary SecurityError
// does not permanently capture an unavailable storage object at module startup.
const journal: Storage = {
  getItem(key) { if (typeof window === 'undefined') throw new Error('本地草稿存储不可用'); return window.localStorage.getItem(key); },
  setItem(key, value) { if (typeof window === 'undefined') throw new Error('本地草稿存储不可用'); window.localStorage.setItem(key, value); },
  removeItem(key) { if (typeof window === 'undefined') throw new Error('本地草稿存储不可用'); window.localStorage.removeItem(key); },
};
// Owns finite timers outside component lifetime. Closing the editor / routing away
// must not lose the final click; recreating YMCC only restores a journal for display.
export const steamAppearanceDraft = createAppearanceDraft({
  read: steamAppearance.read, reconcileExisting: steamAppearance.reconcileExisting, apply: steamAppearance.apply, refresh: steamAppearance.refresh, reset: steamAppearance.reset,
  now: () => Date.now(), later: (fn, ms) => setTimeout(fn, ms), cancel: timer => clearTimeout(timer), storage: journal,
});
