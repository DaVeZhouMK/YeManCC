import { steamAppearance, STEAM_APPEARANCE_THEMES, type AppearanceSnapshot, type AppearanceResult, type AppearanceReconcileResult, type AppearanceThemeName, type AppearanceThemeState } from './steamAppearance';
export const APPEARANCE_DEBOUNCE_MS = 3000;
export const APPEARANCE_DRAFT_MAX_AGE_MS = 7 * 24 * 60 * 60 * 1000;
const DRAFT_MAX_BYTES = 65536;
type JournalStatus = 'idle' | 'saved' | 'unavailable' | 'invalid' | 'future' | 'conflict';
class DraftJournalError extends Error { constructor(readonly status: 'invalid' | 'future', message: string, readonly archiveAllowed = true) { super(message); } }

type Changes = Partial<Record<AppearanceThemeName, boolean>>;
type Phase = 'idle' | 'pending' | 'saving' | 'error' | 'restored' | 'saved-unconfirmed' | 'resetting';
export interface AppearanceDraftState<Snapshot = AppearanceSnapshot> {
  snapshot: Snapshot | null; changes: Changes; pendingCount: number;
  saving: boolean; locked: boolean; phase: Phase; message: string; journalStatus: JournalStatus; journalMessage: string;
}
export type AppearanceDraftViewSnapshot = Readonly<Omit<AppearanceSnapshot, 'themes'>> & {
  readonly themes: readonly Readonly<Omit<AppearanceThemeState, 'renderPlan'>>[];
};
type AppearanceDraftView = Readonly<Omit<AppearanceDraftState<AppearanceDraftViewSnapshot>, 'changes'>> & { readonly changes: Readonly<Changes> };
interface Storage { getItem(key: string): string | null; setItem(key: string, value: string): void; removeItem(key: string): void; }
interface Dependencies {
  submitDraft(targetId: string, states: Changes, mode: 'selection' | 'preferences', onCurrent: (current: AppearanceSnapshot) => void, observed?: Readonly<AppearanceSnapshot> | null): Promise<AppearanceResult>;
  reconcileExisting?(snapshot: AppearanceSnapshot, current?: () => boolean, canDispatch?: () => boolean): Promise<AppearanceReconcileResult>;
  read(current?: () => boolean, beforeStatusDispatch?: () => void): Promise<AppearanceSnapshot>;
  refresh(beforeDispatch?: () => void, observed?: Readonly<AppearanceSnapshot> | null): Promise<AppearanceResult>; reset(mode: 'preset' | 'previous', observed?: Readonly<AppearanceSnapshot> | null): Promise<AppearanceResult>;
  now(): number; later(work: () => void, ms: number): any; cancel(timer: any): void; storage?: Storage;
}
type Entry = { value: boolean; sequence: number };
const names = STEAM_APPEARANCE_THEMES.map(theme => theme.name);
const clone = (s: AppearanceSnapshot): AppearanceSnapshot => ({ ...s, themes: s.themes.map(row => ({ ...row })) });
const retiredRead = Symbol('retired appearance observation');
type ReadResult = Readonly<AppearanceSnapshot> | typeof retiredRead | undefined;
const target = (s: AppearanceSnapshot | null) => typeof s?.targetId === 'string' && /^[a-f0-9]{16}$/i.test(s.targetId) ? s.targetId.toLowerCase() : '';
const errorText = (e: unknown) => e instanceof Error ? e.message : String(e);
function hasCompleteRenderReceipt(result: AppearanceResult, confirmed: AppearanceSnapshot) {
  return result.ok && result.renderConfirmed === true && result.renderContextsComplete === true &&
    confirmed.steamRunning && confirmed.backendReady && confirmed.configuredMatchesBackend && confirmed.packPrepared === true &&
    !confirmed.backendReloadRequired && !confirmed.packRepairRequired && !confirmed.foreignActiveCount && !confirmed.packError;
}

const validJournalAge = (savedAt: unknown, now: number) => Number.isSafeInteger(now) && now >= 0 && (savedAt === undefined ||
  typeof savedAt === 'number' && Number.isSafeInteger(savedAt) && savedAt >= 0 && savedAt <= now && now - savedAt <= APPEARANCE_DRAFT_MAX_AGE_MS);

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
  if (!validJournalAge(doc.savedAt, now)) return invalid();
  return doc; // Missing savedAt is legacy display-only data, never an operation receipt.
}

export function createAppearanceDraft(deps: Dependencies) {
  let snapshot: AppearanceSnapshot | null = null, entries: Partial<Record<AppearanceThemeName, Entry>> = {};
  let viewSnapshot: AppearanceDraftViewSnapshot | null = null;
  let phase: Phase = 'idle', message = '', saving = false, locked = false, sequence = 0, writeEpoch = 0;
  let boundTarget = '', dueAt = Infinity, timer: any = null, task: Promise<void> | null = null;
  let readTask: Promise<ReadResult> | null = null;
  let reapply: symbol | undefined;
  let readRequested = false, failed = false;
  let journalStatus: JournalStatus = deps.storage ? 'idle' : 'unavailable', journalMessage = '';
  let preferenceWarning = '';
  let preferenceRetry: Changes = {}, preferenceRepair = false;
  const journalBaselines = new Map<string, string | null>(), journalOwned = new Map<string, Set<string>>();
  let validatedWrite: { targetId: string; raw: string; doc: Record<string, any> } | undefined;
  const owned = () => { let names = journalOwned.get(boundTarget); if (!names) journalOwned.set(boundTarget, names = new Set()); return names; };
  const restoredTargets = new Set<string>(), listeners = new Set<(state: AppearanceDraftView) => void>();
  const changes = (): Changes => Object.fromEntries(Object.entries(entries).map(([name, entry]) => [name, entry!.value]));
  const pending = () => Object.keys(entries).length;
  const key = (id: string) => 'YMCC.steamAppearanceDraft.v1:' + id;
  function state<T>(current: T | null): AppearanceDraftState<T> {
    const pendingCount = pending();
    const showWarning = !!preferenceWarning && !saving && !locked && !pendingCount;
    return { snapshot: current, changes: changes(), pendingCount, saving, locked,
      phase: showWarning && phase === 'idle' ? 'saved-unconfirmed' : phase,
      message: showWarning && !message.includes(preferenceWarning) ? [message, preferenceWarning].filter(Boolean).join('；') : message,
      journalStatus, journalMessage };
  }
  function getState(): AppearanceDraftState { return state(snapshot ? clone(snapshot) : null); }
  function getViewState(): AppearanceDraftView {
    // Draft edits never mutate the receipt. Legacy render plans are not used by the panel.
    if (snapshot && !viewSnapshot) {
      const themes = snapshot.themes.map(({ name, installed, configured, backendEnabled, parameterCount, error }) =>
        Object.freeze({ name, installed, configured, backendEnabled, parameterCount, error }));
      viewSnapshot = Object.freeze({ ...snapshot, themes: Object.freeze(themes) });
    }
    const view = state(viewSnapshot);
    Object.freeze(view.changes);
    return Object.freeze(view);
  }
  function replaceSnapshot(value: AppearanceSnapshot | null) {
    snapshot = value ? clone(value) : null;
    viewSnapshot = null;
  }
  function preferenceResult(result: AppearanceResult, intent: Changes) {
    if (result.preferenceSaved === true) {
      for (const name of Object.keys(intent)) delete preferenceRetry[name as AppearanceThemeName];
      if (!Object.keys(preferenceRetry).length) preferenceWarning = '';
    } else if (result.preferenceSaved === false) {
      preferenceRetry = { ...preferenceRetry, ...intent };
      preferenceWarning = result.preferenceReason || '开机全局外观偏好未保存';
    }
  }
  function notify() { if (!listeners.size) return; const value = getViewState(); for (const listener of listeners) { try { listener(value); } catch {} } }
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
        try {
          const now = deps.now(), known = validatedWrite;
          // Reuse only exact bytes that this instance validated and read back. Age still depends on the current clock.
          previous = known?.targetId === boundTarget && known.raw === raw && validJournalAge(known.doc.savedAt, now)
            ? known.doc : parseDraftJournal(raw, boundTarget, now);
        }
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
      const nextDocument = next !== null ? parseDraftJournal(next, boundTarget, deps.now()) : null;
      if (storage.getItem(path) !== raw) { journalFault('conflict', '本地草稿在写入前已更替，未覆盖新原文'); return; }
      if (next !== raw) { if (next === null) storage.removeItem(path); else storage.setItem(path, next); }
      if (storage.getItem(path) !== next) { journalFault('conflict', '本地草稿写后回读未确认'); return; }
      validatedWrite = next !== null && nextDocument ? { targetId: boundTarget, raw: next, doc: nextDocument } : undefined;
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
      persist(); entries = {}; clearTimer(); reapply = undefined; failed = true;
      phase = 'error'; message = '加载器主题目录已改变；旧目录的未提交选择已保留，未写入新目录，请重新选择。';
    }
    if (boundTarget !== id) { validatedWrite = undefined; preferenceWarning = ''; preferenceRetry = {}; preferenceRepair = false; restoredTargets.delete(id); journalFault(id ? 'idle' : 'unavailable', id ? '' : '主题目录身份未知，未读取草稿'); }
    replaceSnapshot(value); boundTarget = id;
    if (restore) restoreJournal();
  }
  function schedule() {
    clearTimer();
    if (locked || failed || !pending() || !Number.isFinite(dueAt)) return;
    timer = deps.later(() => { timer = null; void kick(); }, Math.max(0, dueAt - deps.now()));
  }
  async function read(current?: () => boolean, reconciliation?: { current(): boolean; recheck(): void; beforeStatusDispatch?(): void }, allowReplacement = true): Promise<Readonly<AppearanceSnapshot> | undefined> {
    if (current?.() === false) return;
    const reconcileCurrent = reconciliation?.current ?? current;
    if (saving || locked || pending()) { readRequested = true; return; }
    const epoch = writeEpoch;
    if (readTask) {
      const result = await readTask;
      // A live caller can replace a retired read once, provided no write
      // superseded this caller while it waited for the older observation.
      if (result === retiredRead && allowReplacement && epoch === writeEpoch) return read(current, reconciliation, false);
      return result === retiredRead ? undefined : result;
    }
    const stillReading = () => epoch === writeEpoch && !saving && !locked && !pending();
    readTask = (async (): Promise<ReadResult> => {
      try {
        // A user edit can supersede Status while it is still resolving its inputs.
        const value = await deps.read(() => stillReading() && current?.() !== false, reconciliation?.beforeStatusDispatch);
        if (!stillReading() || current?.() === false) return retiredRead;
        receive(value);
        if (pending()) return; // Restored or newly entered user intent wins.
        failed = false; phase = 'idle'; message = '';
        if (deps.reconcileExisting && reconcileCurrent?.() !== false && value.steamRunning && value.backendReady && value.packOwned && !value.packError &&
            (!value.configuredMatchesBackend || value.packRepairRequired || value.backendReloadRequired)) {
          const repairEpoch = ++writeEpoch;
          saving = true; notify();
          const repair = (async () => {
            try {
              // Edits and resets change the epoch; reapply can queue without doing so.
              const result = await deps.reconcileExisting!(value, reconcileCurrent,
                () => writeEpoch === repairEpoch && !reapply && !preferenceRepair);
              if (!result) return; // Observation retired before dispatch; no repair was attempted.
              if (result === 'superseded') { reconciliation?.recheck(); return; }
              if (result.snapshot) receive(result.snapshot, false);
              message = pending() ? '已选择 · 当前检查完成后应用最新选择' : result.message || '';
              phase = pending() ? 'pending' : result.ok ? 'idle' : 'saved-unconfirmed';
            } catch (error) {
              if (!pending()) { phase = 'saved-unconfirmed'; message = '配置检查未确认：' + errorText(error); }
            } finally { saving = false; ++writeEpoch; }
          })();
          task = repair.finally(() => {
            task = null;
            // An explicit click can queue behind reconciliation, but its
            // intent must not move to a different target or Steam lifetime.
            if (reapply && (!boundTarget || boundTarget !== target(value) || snapshot?.steamRunning !== value.steamRunning ||
                snapshot?.steamPid !== value.steamPid || snapshot?.steamBirth !== value.steamBirth)) {
              reapply = undefined;
              preferenceRepair = false;
              if (!pending() && !locked) { phase = 'error'; message = 'Steam 会话或主题目录已变化，未重新应用外观'; }
            }
            notify();
            if (pending()) schedule();
            else if (reapply) void kick();
          });
          await task;
        }
        return value;
      }
      catch (error) {
        if (!stillReading() || current?.() === false) return retiredRead;
        replaceSnapshot(null);
        phase = 'error'; message = errorText(error);
      }
      finally {
        readTask = null; notify();
        // Existing read events may repair one observed mismatch. No timer,
        // CSS/Steam launch, draft replay or background search is scheduled.
      }
    })();
    const result = await readTask;
    return result === retiredRead ? undefined : result;
  }
  async function submit() {
    const requestedReapply = reapply;
    const submitted = Object.fromEntries(Object.entries(entries).map(([name, entry]) => [name, { ...entry! }])) as Partial<Record<AppearanceThemeName, Entry>>;
    const id = boundTarget;
    const intent: Changes = Object.fromEntries(names.filter(name => submitted[name]).map(name => [name, submitted[name]!.value]));
    const result = await deps.submitDraft(id, intent, 'selection', current => {
      receive(current, false);
      if (target(current) === id) persist();
    }, snapshot);
    if (!result.snapshot) throw new Error('后台未返回可校验状态，未丢弃选择');
    if (result.snapshot && id && target(result.snapshot) !== id) { receive(result.snapshot, false); throw new Error('加载器返回了不同主题目录，未确认旧选择'); }
    if (result.snapshot) receive(result.snapshot, false);
    preferenceResult(result, intent);
    const acknowledged = (result.ok || result.saved === true) && !!result.snapshot;
    if (acknowledged) for (const name of names) {
      if (submitted[name] && entries[name]?.sequence === submitted[name]!.sequence && result.snapshot!.themes.find(row => row.name === name)?.configured === submitted[name]!.value) delete entries[name];
    }
    persist();
    if (names.some(name => submitted[name] && entries[name]?.sequence === submitted[name]!.sequence)) throw new Error(result.message || '返回状态与所选开关不一致，未丢弃选择');
    if (!result.ok || result.preferenceSaved === false) {
      if (acknowledged && !pending()) { phase = 'saved-unconfirmed'; message = result.message || '配置已保存，后台应用未确认'; }
      else throw new Error(result.message || '提交未确认，选择已保留；点击重新应用重试');
    } else { phase = pending() ? 'pending' : 'idle'; message = pending() ? '仍有新选择，停止操作 3 秒后自动应用' : result.message || '配置已保存'; }
    // A live receipt can finish the request that started this batch, but not a
    // newer explicit reapply. A status-only or offline save is not a receipt.
    const confirmed = result.snapshot;
    if (requestedReapply && reapply === requestedReapply && !pending() && hasCompleteRenderReceipt(result, confirmed)) reapply = undefined;
  }
  async function run() {
    saving = true; phase = 'saving'; ++writeEpoch; notify();
    try {
      if (preferenceRepair) {
        preferenceRepair = false;
        if (!pending() && Object.keys(preferenceRetry).length) {
          const intent = { ...preferenceRetry }, id = boundTarget;
          const result = await deps.submitDraft(id, intent, 'preferences', current => { if (target(current) !== id) receive(current, false); });
          if (!result.snapshot || target(result.snapshot) !== id) throw new Error('全局偏好补存回执身份未确认');
          receive(result.snapshot, false); preferenceResult(result, intent);
          if (!pending()) {
            phase = result.ok && result.preferenceSaved !== false ? 'idle' : 'saved-unconfirmed';
            message = result.message || '';
          }
        }
      }
      if (pending() && !locked && deps.now() >= dueAt) await submit();
      if (reapply && !pending() && !locked) {
        reapply = undefined;
        const epoch = writeEpoch, initial = snapshot, initialTarget = boundTarget;
        let dispatchedReapply: symbol | undefined;
        const result = await deps.refresh(() => { dispatchedReapply = reapply; }, initial);
        const confirmed = result.snapshot;
        if (confirmed) {
          receive(confirmed, false);
          // Only clicks already present at dispatch can share this full receipt.
          // A newer edit, click, target or Steam lifetime retains its own intent.
          if (dispatchedReapply && reapply === dispatchedReapply && epoch === writeEpoch && !pending() &&
              initialTarget && target(confirmed) === initialTarget && initial?.steamPid && initial.steamBirth && initial.steamBirth !== '0' &&
              confirmed.steamPid === initial.steamPid && confirmed.steamBirth === initial.steamBirth && hasCompleteRenderReceipt(result, confirmed)) reapply = undefined;
        } else receive(await deps.read(), false);
        phase = result.ok ? 'idle' : result.saved ? 'saved-unconfirmed' : 'error';
        message = result.message || (result.ok ? '已重新应用外观' : '重新应用未确认');
        if (!result.ok && !result.saved) failed = true;
      }
    } catch (error) {
      failed = true; reapply = undefined; phase = 'error'; message = '外观提交未确认，选择已保留：' + errorText(error); persist();
    } finally { saving = false; ++writeEpoch; notify(); }
  }
  function kick(): Promise<void> {
    if (task) return task;
    if (locked || failed || pending() && deps.now() < dueAt || !pending() && !reapply && !preferenceRepair) return Promise.resolve();
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
      if (!saving && snapshot?.themes.find(row => row.name === name)?.configured === value && preferenceRetry[name] === undefined) delete entries[name];
      else entries[name] = { value, sequence: ++sequence };
    }
    dueAt = deps.now() + APPEARANCE_DEBOUNCE_MS;
    phase = pending() ? 'pending' : 'idle'; message = pending() ? '已选择 · 停止操作 3 秒后自动应用' : '选择与当前配置一致，无需提交';
    persist(true); notify(); schedule();
  }
  async function flush(reapplyRequested = true) {
    if (locked) return Promise.resolve();
    if (!snapshot || !boundTarget) await read();
    if (!snapshot || !boundTarget) { clearTimer(); phase = 'error'; message = '主题目录身份未知，未应用草稿'; notify(); return; }
    if (!pending() && Object.keys(preferenceRetry).length) preferenceRepair = true;
    persist(true); failed = false; if (reapplyRequested) reapply = Symbol(); dueAt = deps.now(); clearTimer();
    if (saving) { message = '已排队应用最新选择；开关仍可继续调整'; notify(); return task || Promise.resolve(); }
    return kick();
  }
  async function reset(mode: 'preset' | 'previous') {
    if (locked) return;
    const observed = snapshot;
    locked = true; failed = false; ++writeEpoch; clearTimer(); entries = {}; reapply = undefined; preferenceRepair = false; persist();
    phase = 'resetting'; message = '正在恢复预设，未提交的选择已取消'; notify();
    try {
      if (task) await task;
      // A superseded write may have failed while reset awaited it. The reset is
      // a new explicit operation, so an old failure cannot poison its state.
      failed = false;
      const result = await deps.reset(mode, observed);
      // Reset can change every flag. An acknowledgement without data must not
      // leave the previous configuration displayed as the new confirmed state.
      if (result.snapshot) receive(result.snapshot, false);
      else {
        replaceSnapshot(null);
        // An uncertain commit must not immediately run a status/recovery of a different target.
        if (!(result.saved && !result.ok)) receive(await deps.read(), false);
      }
      preferenceResult(result, Object.fromEntries(result.snapshot?.themes.map(row => [row.name, row.configured]) || []));
      phase = result.ok && result.preferenceSaved !== false ? 'idle' : result.saved ? 'saved-unconfirmed' : 'error';
      message = result.message || (result.ok ? '已恢复外观' : '恢复外观未确认');
    } catch (error) { phase = 'error'; message = errorText(error); }
    finally { locked = false; saving = false; ++writeEpoch; notify(); if (readRequested) { readRequested = false; void read(); } }
  }
  return { getState, getViewState, read, set, flush, reset,
    subscribe(listener: (state: AppearanceDraftView) => void) { listeners.add(listener); listener(getViewState()); return () => listeners.delete(listener); },
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
  read: steamAppearance.read, reconcileExisting: steamAppearance.reconcileExisting, submitDraft: steamAppearance.submitDraft, refresh: steamAppearance.refresh, reset: (mode, observed) => steamAppearance.reset(mode, {}, observed),
  now: () => Date.now(), later: (fn, ms) => setTimeout(fn, ms), cancel: timer => clearTimeout(timer), storage: journal,
});
