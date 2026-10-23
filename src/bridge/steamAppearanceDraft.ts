import { steamAppearance, STEAM_APPEARANCE_THEMES, type AppearanceSnapshot, type AppearanceResult, type AppearanceThemeName } from './steamAppearance';
export const APPEARANCE_DEBOUNCE_MS = 3000;
type Changes = Partial<Record<AppearanceThemeName, boolean>>;
type Phase = 'idle' | 'pending' | 'saving' | 'error' | 'restored' | 'saved-unconfirmed' | 'resetting';
export interface AppearanceDraftState {
  snapshot: AppearanceSnapshot | null; changes: Changes; pendingCount: number;
  saving: boolean; locked: boolean; phase: Phase; message: string;
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
const target = (s: AppearanceSnapshot | null) => s?.targetId?.toLowerCase() || '';
const errorText = (e: unknown) => e instanceof Error ? e.message : String(e);
export function createAppearanceDraft(deps: Dependencies) {
  let snapshot: AppearanceSnapshot | null = null, entries: Partial<Record<AppearanceThemeName, Entry>> = {};
  let phase: Phase = 'idle', message = '', saving = false, locked = false, sequence = 0, writeEpoch = 0;
  let boundTarget = '', dueAt = Infinity, timer: any = null, task: Promise<void> | null = null, readTask: Promise<void> | null = null;
  let reapply = false, readRequested = false, failed = false;
  const restoredTargets = new Set<string>(), listeners = new Set<(state: AppearanceDraftState) => void>();
  const changes = (): Changes => Object.fromEntries(Object.entries(entries).map(([name, entry]) => [name, entry!.value]));
  const pending = () => Object.keys(entries).length;
  const key = (id: string) => 'YMCC.steamAppearanceDraft.v1:' + id;
  function state(): AppearanceDraftState { return { snapshot: snapshot ? clone(snapshot) : null, changes: changes(), pendingCount: pending(), saving, locked, phase, message }; }
  function notify() { const value = state(); for (const listener of listeners) { try { listener(value); } catch {} } }
  function clearTimer() { if (timer !== null) deps.cancel(timer); timer = null; }
  function persist() {
    if (!boundTarget || !deps.storage) return;
    try { if (pending()) deps.storage.setItem(key(boundTarget), JSON.stringify({ version: 1, targetId: boundTarget, states: changes(), savedAt: deps.now() }));
      else deps.storage.removeItem(key(boundTarget)); } catch {} // Best effort UI journal; never CSS/file writes.
  }
  function restoreJournal() {
    if (!boundTarget || pending() || !deps.storage || restoredTargets.has(boundTarget)) return;
    restoredTargets.add(boundTarget);
    try {
      const text = deps.storage.getItem(key(boundTarget)); if (!text) return;
      const value = JSON.parse(text);
      if (value?.version !== 1 || value.targetId !== boundTarget || !value.states || typeof value.states !== 'object' || Array.isArray(value.states) ||
        Object.keys(value.states).some(name => !names.includes(name as AppearanceThemeName) || typeof value.states[name] !== 'boolean')) return;
      for (const name of names) if (typeof value.states[name] === 'boolean' && snapshot?.themes.find(row => row.name === name)?.configured !== value.states[name])
        entries[name] = { value: value.states[name], sequence: ++sequence };
      persist();
      if (pending()) { phase = 'restored'; message = '已恢复上次未提交的选择；点击重新应用确认。不会自动启动 Steam 或切换模式。'; dueAt = Infinity; }
      // A journal is display-only after YMCC restart. Never restart a timer or replay an operation.
    } catch {}
  }
  function receive(value: AppearanceSnapshot, restore = true) {
    const id = target(value);
    if (boundTarget && id && boundTarget !== id && pending()) {
      persist(); entries = {}; clearTimer(); reapply = false; failed = true;
      phase = 'error'; message = '加载器主题目录已改变；旧目录的未提交选择已保留，未写入新目录，请重新选择。';
    }
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
    if (id && target(current) !== id) { receive(current, false); throw new Error('加载器主题目录已改变，未跨目录提交旧选择'); }
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
    ++writeEpoch; failed = false;
    for (const [key, value] of items) {
      const name = key as AppearanceThemeName;
      if (!saving && snapshot?.themes.find(row => row.name === name)?.configured === value) delete entries[name];
      else entries[name] = { value, sequence: ++sequence };
    }
    dueAt = deps.now() + APPEARANCE_DEBOUNCE_MS;
    phase = pending() ? 'pending' : 'idle'; message = pending() ? '已选择 · 停止操作 3 秒后自动应用' : '选择与当前配置一致，无需提交';
    persist(); notify(); schedule();
  }
  function flush(reapplyRequested = true) {
    if (locked) return Promise.resolve();
    failed = false; reapply ||= reapplyRequested; dueAt = deps.now(); clearTimer();
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
let journal: Storage | undefined;
try { if (typeof window !== 'undefined') journal = window.localStorage; } catch {}
// Owns finite timers outside component lifetime. Closing the editor / routing away
// must not lose the final click; recreating YMCC only restores a journal for display.
export const steamAppearanceDraft = createAppearanceDraft({
  read: steamAppearance.read, reconcileExisting: steamAppearance.reconcileExisting, apply: steamAppearance.apply, refresh: steamAppearance.refresh, reset: steamAppearance.reset,
  now: () => Date.now(), later: (fn, ms) => setTimeout(fn, ms), cancel: timer => clearTimeout(timer), storage: journal,
});
