import { app, fs, shell, windowApi } from '@/bridge/api';
import { invoke, on, IpcRemoteError } from '@/bridge/ipc';
import { getSharedPowerLifecycle, onSharedPowerLifecycle, sharedPowerWriteScope, type SharedPowerWriteScope } from './powerLifecycleState';

/**
 * Preparation gate for the Custom Steam Library integration.
 *
 * The first-level Steam page now exposes the tested entry bubble.  The old
 * standalone menu route remains hidden and is intentionally kept until the
 * user confirms its removal.
 */
export const CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED = true;
export const CUSTOM_STEAM_LIBRARY_PROTOCOL_VERSION = 1;
export const CUSTOM_STEAM_LIBRARY_ROUTE = '/custom-steam-library';
export const CUSTOM_STEAM_LIBRARY_TITLE = 'Steam自定义游戏库';
export const CUSTOM_STEAM_LIBRARY_CLASS = 'YeManSteamLibraryWorkspace';
/** Canonical installed child directory under the YeManCC program root. */
export const CUSTOM_STEAM_LIBRARY_ROOT = 'C:\\SOFT\\YeMan\\YeManCC\\CustomSteamLibrary';
// （legacy 顶层根与只读回退已删，2026-09-17 用户裁决：仅保留两根结构。）
export const CUSTOM_STEAM_LIBRARY_DATA_ROOT = 'D:\\YeMan\\CustomSteamLibrary\\data';

export interface CustomSteamLibrarySummary {
  waiting: number;
  joined: number;
  needs: number;
  excluded: number;
}

export interface CustomSteamLibraryLaunchResult {
  ok: boolean;
  executable: string;
  pid?: number;
  reason?: string;
}

export interface CustomSteamLibraryStatus {
  configured: boolean;
  present: boolean;
  foreground: boolean;
  pid?: number;
  inputOwner?: 'host' | 'parent' | 'unknown';
  compatible?: boolean;
  phase?: 'disabled' | 'launching' | 'active';
}

const SESSION_STARTUP_POLL_MS = 250;
const SESSION_STARTUP_GRACE_MS = 10000;
const SESSION_AUDIT_MS = 2000;
const SESSION_RETURN_SETTLE_MS = 125;
interface SessionWatch {
  generation: number;
  timer: ReturnType<typeof setTimeout> | null;
  deadline: number;
  observedPresent: boolean;
  read: AbortController | null;
  detachHints: (() => void) | null;
}
let sessionGeneration = 0;
let sessionWatch: SessionWatch | null = null;
let launchPreparation: AbortController | null = null;
// A local abort cannot undo a dispatched mutation. The queue waits for the
// Native receipt, or releases its waiter while retaining the unresolved ticket.
let sessionMutationTail: Promise<void> = Promise.resolve();
function mutateSession<T>(generation: number, action: () => Promise<T> | undefined): Promise<T | undefined> {
  const work = sessionMutationTail.then(() => generation === sessionGeneration ? action() : undefined);
  sessionMutationTail = work.then(() => {}, () => {});
  return work;
}

const MUTATION_ACK_MS = 4000;
const MUTATION_READ_MS = 1500;
type MutationKind = 'enable' | 'disable' | 'launch';
type LaunchReceipt = { ok: boolean; pid?: number };
type MutationOutcome = { result?: boolean | LaunchReceipt; error?: string };
type PendingMutation = { ticket: number; kind: MutationKind; generation: number; detach?: () => void };
let pendingMutation: PendingMutation | undefined;
let recoveredLaunch: LaunchReceipt | undefined;
let openRequested = false;

class MutationUnconfirmed extends Error {
  constructor() { super('游戏库上一项操作尚未确认，请稍后重试'); }
}

function createdChild(value: MutationOutcome['result']): LaunchReceipt | undefined {
  return value && typeof value === 'object' && value.ok === true ? value : undefined;
}

async function settlePendingMutation(pending: PendingMutation, retryMissingReply = false): Promise<MutationOutcome | undefined> {
  const read = () => invoke<{ ticket: number; state: string; outcome?: MutationOutcome }>(
    'customSteamLibrary.settleMutation', { mutationTicket: pending.ticket }, { timeoutMs: MUTATION_READ_MS }).catch(() => null);
  let reply = await read();
  if (!reply && retryMissingReply && pendingMutation === pending) reply = await read();
  if (pendingMutation !== pending || reply?.ticket !== pending.ticket) return;
  let outcome: MutationOutcome;
  if (reply.state === 'retired') outcome = { error: '游戏库操作已过期，请重新操作' };
  else if (reply.state === 'completed' && reply.outcome && (
    typeof reply.outcome.error === 'string' ||
    (pending.kind === 'launch' ? typeof (reply.outcome.result as LaunchReceipt | undefined)?.ok === 'boolean'
      : typeof reply.outcome.result === 'boolean'))) outcome = reply.outcome;
  else return;
  pendingMutation = undefined;
  pending.detach?.();
  return outcome;
}

function observePendingMutation(pending: PendingMutation): void {
  let observing = false;
  let observeAgain = false;
  const recover = () => {
    if (pendingMutation !== pending) return;
    if (observing) { observeAgain = true; return; }
    const generation = sessionGeneration;
    observing = true;
    void mutateSession(generation, async () => {
      if (pendingMutation !== pending) return;
      const outcome = await settlePendingMutation(pending);
      if (outcome && openRequested && pending.kind === 'launch') recoveredLaunch = createdChild(outcome.result);
      if (outcome && generation === sessionGeneration) return outcome;
    }).then(outcome => {
      if (!outcome || generation !== sessionGeneration) return;
      if (recoveredLaunch && openRequested) beginSessionWatch(generation, true);
      else if (pending.kind !== 'disable' && (!openRequested || pending.generation === generation))
        void stopNativeIntegrationSession(generation);
    }).catch(() => {}).finally(() => {
      observing = false;
      if (observeAgain) { observeAgain = false; recover(); }
    });
  };
  let scope = sharedPowerWriteScope(getSharedPowerLifecycle())?.join(':');
  const stopPower = onSharedPowerLifecycle(state => {
    const next = sharedPowerWriteScope(state)?.join(':');
    if (next && next !== scope) recover();
    scope = next;
  }, { observeNotificationSequence: true, observeSuspendSerial: true });
  const stopCompletion = on<{ ticket?: number }>('customSteamLibrary.mutationCompleted', value => {
    if (value?.ticket === pending.ticket) recover();
  });
  pending.detach = () => { stopPower(); stopCompletion(); };
}

async function recoverPriorMutation(): Promise<void> {
  const pending = pendingMutation;
  if (!pending) return;
  const outcome = await settlePendingMutation(pending);
  if (!outcome) throw new MutationUnconfirmed();
  if (openRequested && pending.kind === 'launch') recoveredLaunch = createdChild(outcome.result);
}

async function submitSessionMutation<T extends boolean | LaunchReceipt>(kind: MutationKind, generation: number,
  beforeSend: () => boolean, send: (ticket: number) => Promise<T>): Promise<T | undefined> {
  if (pendingMutation) throw new MutationUnconfirmed();
  const ticket = await invoke<number>('customSteamLibrary.mutationTicket', { kind }, { timeoutMs: MUTATION_READ_MS });
  if (generation !== sessionGeneration || !beforeSend()) return;
  if (!Number.isSafeInteger(ticket) || ticket <= 0) throw new MutationUnconfirmed();
  try { return await send(ticket); }
  catch (error) {
    if (error instanceof IpcRemoteError) throw error;
    const pending = { ticket, kind, generation };
    pendingMutation = pending;
    // Register before reading: a completion can arrive while a reply still
    // describes the running state. The ordered queue keeps that follow-up.
    observePendingMutation(pending);
    const outcome = await settlePendingMutation(pending, true);
    if (!outcome) throw new MutationUnconfirmed();
    if (outcome.error !== undefined) throw new Error(outcome.error);
    if (kind === 'launch' && generation !== sessionGeneration && openRequested) recoveredLaunch = createdChild(outcome.result);
    return outcome.result as T;
  }
}

// The custom library is a separate process. Window hand-off is therefore
// deliberately best-effort: an older shell, a missing YMCC window, or a
// shutting-down IPC endpoint must never delay opening/closing the library.
const WINDOW_HANDOFF_TIMEOUT_MS = 800;

function requestYeManWindowAction(action: 'minimize' | 'show'): void {
  try {
    // window.show follows the same path as clicking the YMCC taskbar button:
    // SW_RESTORE for a minimized window, otherwise SW_SHOW. Do not use
    // SW_MAXIMIZE here because that changes a normal window's saved size and
    // position instead of restoring the state the user left it in.
    const request = action === 'minimize' ? windowApi.minimize() : windowApi.show();
    void Promise.race([
      request,
      new Promise<boolean>(resolve => {
        setTimeout(() => resolve(false), WINDOW_HANDOFF_TIMEOUT_MS);
      }),
    ]).catch(() => false);
  } catch {
    // YMCC may not exist when this bridge is reused by a standalone shell.
  }
}

function trimWindowsPath(value: string): string {
  return value.replace(/[\\/]+$/, '');
}

function joinWindowsPath(root: string, child: string): string {
  return `${trimWindowsPath(root)}\\${child}`;
}

// 2026-09-17 用户裁决：删除 legacy 顶层 CustomSteamLibrary 回退——现行两根
// 结构下子包与主程序同根（C:\SOFT\YeMan\YeManCC\CustomSteamLibrary）。
async function resolveExecutable(signal?: AbortSignal): Promise<string> {
  const exeDir = await app.exeDir(MUTATION_READ_MS, signal);
  const candidates = [
    // Normal installed layout: YeManCC.exe and its child package share a root.
    joinWindowsPath(exeDir, 'CustomSteamLibrary\\CustomSteamLibrary.exe'),
    // User-frozen canonical path for a standard installation.
    `${CUSTOM_STEAM_LIBRARY_ROOT}\\CustomSteamLibrary.exe`,
  ];
  for (const candidate of candidates) {
    signal?.throwIfAborted();
    const present = await fs.exists(candidate, MUTATION_READ_MS, signal).catch(() => false);
    signal?.throwIfAborted();
    if (present) return candidate;
  }
  throw new Error(`未找到 ${CUSTOM_STEAM_LIBRARY_TITLE}：${candidates.join('；')}`);
}

function safeCount(value: unknown): number {
  const count = Number(value);
  return Number.isFinite(count) && count >= 0 ? Math.floor(count) : 0;
}

function strictCount(value: unknown): number | null {
  return typeof value === 'number' && Number.isSafeInteger(value) && value >= 0 ? value : null;
}

const SUMMARY_COUNT_FIELDS = [
  'scannedGames', 'readyToAdd', 'readyNotSelected', 'alreadyInSteam',
  'needsPrimaryConfirmation', 'needsIdentity', 'needsSteamVerification',
  'needsMinimumArtwork', 'steamNativeGames', 'nonGames', 'shortcutIdConflicts',
] as const;

function summaryCountsAreWellFormed(summary: Record<string, any>): boolean {
  return SUMMARY_COUNT_FIELDS.every(field =>
    summary[field] === undefined || strictCount(summary[field]) !== null);
}

function isRecord(value: unknown): value is Record<string, any> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function normalizeDataPath(value: unknown): string {
  let normalized = String(value || '');
  if (normalized.startsWith('\\\\?\\')) normalized = normalized.slice(4);
  return normalized
    .replace(/\//g, '\\')
    .replace(/\\+/g, '\\')
    .replace(/\\+$/, '')
    .toLocaleLowerCase();
}

function planBelongsToRoot(plan: any, root: string): boolean {
  if (!isRecord(plan)) return false;
  const expectedRoot = normalizeDataPath(root);
  const expectedState = normalizeDataPath(joinWindowsPath(root, 'state\\library-scan.json'));
  const dataRoot = normalizeDataPath(plan.dataRoot);
  const libraryState = normalizeDataPath(plan.libraryState);
  const rootMatches = dataRoot ? dataRoot === expectedRoot : false;
  const stateMatches = libraryState ? libraryState === expectedState : false;
  // Current plans carry both fields. Requiring every present identity field to
  // match prevents a stale plan from another installation from winning the
  // first-readable-root race. A dataRoot-only plan remains backward-compatible.
  return Boolean((dataRoot || libraryState) && (!dataRoot || rootMatches) && (!libraryState || stateMatches));
}

function boundedCount(value: unknown, total: number): number {
  return Math.min(total, safeCount(value));
}

function summaryFromPlan(plan: any, root: string): CustomSteamLibrarySummary | null {
  if (!planBelongsToRoot(plan, root) || !isRecord(plan.summary) || !Array.isArray(plan.items)) return null;
  const items = plan.items;
  if (items.some(item => !isRecord(item) ||
    (!((typeof item.gameDirectory === 'string' && item.gameDirectory.trim()) ||
      (typeof item.primaryExecutable === 'string' && item.primaryExecutable.trim()))) ||
    (item.status !== undefined && item.status !== null && typeof item.status !== 'string'))) return null;
  const summary = plan.summary;
  if (!summaryCountsAreWellFormed(summary)) return null;
  const hasRows = items.length > 0;
  if (!hasRows && strictCount(summary.scannedGames) === null) return null;
  if (hasRows) {
    // Item statuses are the post-commit truth. The summary object can remain
    // from the planning phase after a commit, so never let stale counters make
    // the top-level Steam page report old waiting/joined values.
    const waiting = items.filter(item => item.status === 'ready-to-add' || item.status === 'ready-not-selected').length;
    const joined = items.filter(item => item.status === 'already-in-steam' || item.status === 'added-to-steam').length;
    const excluded = items.filter(item => item.status === 'non-game').length;
    const total = items.length;
    return { waiting, joined, needs: Math.max(0, total - waiting - joined - excluded), excluded };
  }
  const total = strictCount(summary.scannedGames) ?? 0;
  const waitingRaw = (strictCount(summary.readyToAdd) ?? 0) + (strictCount(summary.readyNotSelected) ?? 0);
  const joinedRaw = strictCount(summary.alreadyInSteam) ?? 0;
  const excludedRaw = strictCount(summary.nonGames) ?? 0;
  // A summary-only legacy plan has no rows from which to recover truth. Do
  // not clamp contradictory counters into a plausible-looking zero summary;
  // reject it and let the next data root or scan cache win.
  if (waitingRaw > total || joinedRaw > total || excludedRaw > total ||
    waitingRaw + joinedRaw + excludedRaw > total) return null;
  const waiting = waitingRaw;
  const joined = joinedRaw;
  const excluded = excludedRaw;
  return { waiting, joined, needs: Math.max(0, total - waiting - joined - excluded), excluded };
}

function summaryFromScan(scan: any): CustomSteamLibrarySummary | null {
  if (!isRecord(scan) || !isRecord(scan.summary) || !Array.isArray(scan.games)) return null;
  if (scan.games.some((game: any) => !isRecord(game) || typeof game.gameDirectory !== 'string' || !game.gameDirectory.trim() ||
    (game.status !== undefined && game.status !== null && typeof game.status !== 'string'))) return null;
  const summary = scan.summary;
  const total = scan.games.length;
  const scannedGames = strictCount(summary.games);
  const ready = strictCount(summary.ready);
  const nonGames = strictCount(summary.nonGames);
  if ((summary.games !== undefined && (scannedGames === null || scannedGames !== total)) ||
    (summary.ready !== undefined && ready === null) ||
    (summary.nonGames !== undefined && nonGames === null)) return null;
  // A legacy scan may have a valid games array but no ready counter. Derive
  // that one field from the row status instead of turning a readable cache
  // into a false zero and preventing later data-root fallback.
  const derivedReady = scan.games.filter((game: any) => game.status === 'ready').length;
  const waiting = Math.min(total, ready ?? derivedReady);
  // library-scan.summary.games excludes skipped non-game directories. Do not
  // subtract nonGames from the game count a second time.
  const excluded = nonGames ?? 0;
  return { waiting, joined: 0, needs: Math.max(0, total - waiting), excluded };
}

/** Read only the latest local cache for the first-level summary bubble. */
export async function readCustomSteamLibrarySummary(): Promise<CustomSteamLibrarySummary | null> {
  const exeDir = await app.exeDir().catch(() => '');
  const roots = [
    CUSTOM_STEAM_LIBRARY_DATA_ROOT,
    exeDir ? joinWindowsPath(exeDir, 'CustomSteamLibrary\\data') : '',
    `${CUSTOM_STEAM_LIBRARY_ROOT}\\data`,
  ].filter(Boolean)
    .filter((root, index, all) => all.indexOf(root) === index);
  for (const root of roots) {
    try {
      const raw = await fs.readTextFile(`${root}\\state\\steam-add-plan.json`, 2 << 20);
      const summary = summaryFromPlan(JSON.parse(raw), root);
      if (!summary) throw new Error('Steam plan is invalid or belongs to another data root');
      return summary;
    } catch {
      // Try the fallback data root and then the scan cache below.
    }
  }
  for (const root of roots) {
    try {
      const raw = await fs.readTextFile(`${root}\\state\\library-scan.json`, 2 << 20);
      const scan = summaryFromScan(JSON.parse(raw));
      if (!scan) throw new Error('Library scan cache is invalid');
      return scan;
    } catch {
      // Keep trying the next local root. Never start a scan from this bubble.
    }
  }
  return null;
}

function clearSessionWatch(): void {
  const watch = sessionWatch;
  sessionWatch = null;
  if (!watch) return;
  if (watch.timer !== null) clearTimeout(watch.timer);
  watch.timer = null;
  watch.detachHints?.();
  watch.detachHints = null;
  watch.read?.abort(); // Forget only the read's local IPC wait, not native work.
  watch.read = null;
}

async function setCustomSteamLibrarySessionIntent(enabled: boolean, generation: number, beforeSend = () => true,
  powerScope?: SharedPowerWriteScope): Promise<boolean | undefined> {
  if (enabled && (!CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED || !powerScope)) return false;
  return mutateSession(generation, async () => {
    if (!enabled) await recoverPriorMutation();
    if (!beforeSend() || generation !== sessionGeneration) return;
    return submitSessionMutation(enabled ? 'enable' : 'disable', generation, beforeSend,
      mutationTicket => invoke<boolean>('customSteamLibrary.setIntegrationSession',
        { enabled, mutationTicket, ...(powerScope ? { powerScope } : {}) }, { timeoutMs: MUTATION_ACK_MS }));
  });
}
async function stopNativeIntegrationSession(generation: number): Promise<void> {
  await setCustomSteamLibrarySessionIntent(false, generation).catch(() => false);
}

function beginSessionWatch(generation: number, powerInterrupted = false): void {
  if (generation !== sessionGeneration) return;
  clearSessionWatch();
  const watch: SessionWatch = { generation, timer: null, deadline: Date.now() + SESSION_STARTUP_GRACE_MS,
    observedPresent: false, read: null, detachHints: null };
  sessionWatch = watch;
  const current = () => sessionWatch === watch && generation === sessionGeneration;
  let powerScope: string | undefined;
  const admitted = () => current() && !!powerScope && sharedPowerWriteScope(getSharedPowerLifecycle())?.join(':') === powerScope;
  let readStartedAt = 0;
  let timerDue = 0;
  let returnCheckAt: number | null = null;
  const queueRead = (delay: number) => {
    if (!admitted() || watch.read) return;
    const wait = Math.max(0, delay);
    const due = Date.now() + wait;
    if (watch.timer !== null && timerDue <= due) return;
    if (watch.timer !== null) clearTimeout(watch.timer);
    timerDue = due;
    watch.timer = setTimeout(() => {
      watch.timer = null;
      void poll();
    }, wait);
  };
  const returnDelay = () => Math.max(returnCheckAt ?? 0, readStartedAt + SESSION_STARTUP_POLL_MS) - Date.now();
  const hintReturn = () => {
    if (!admitted() || !watch.observedPresent || returnCheckAt !== null) return;
    // The host can restore the parent just before it destroys its own window.
    // Coalesce focus, visibility and input-owner hints into one fresh read.
    returnCheckAt = Date.now() + SESSION_RETURN_SETTLE_MS;
    queueRead(returnDelay());
  };
  const visibilityChanged = () => { if (document.visibilityState === 'visible') hintReturn(); };
  const close = async (event: 'closed' | 'conflict', detail?: { inputOwner: string; pid?: number }) => {
    if (!current()) return;
    // Retire synchronously. A late disable receipt must never clear a newer
    // watch, and a queued disable must check generation before it is sent.
    recoveredLaunch = undefined;
    clearSessionWatch();
    requestYeManWindowAction('show');
    await stopNativeIntegrationSession(generation);
    if (generation !== sessionGeneration) return;
    window.dispatchEvent(new CustomEvent('customSteamLibrary:' + event, { detail }));
  };
  const poll = async () => {
    if (!admitted() || watch.read) return;
    returnCheckAt = null;
    readStartedAt = Date.now();
    const read = new AbortController();
    watch.read = read;
    const readCurrent = () => admitted() && watch.read === read;
    try {
      const status = await invoke<CustomSteamLibraryStatus>('customSteamLibrary.status', {}, {
        signal: read.signal, timeoutMs: SESSION_STARTUP_GRACE_MS,
      });
      if (!readCurrent()) return;
      if (status.present) {
        watch.observedPresent = true;
        if (status.compatible === false || status.inputOwner !== 'parent') {
          await close('conflict', { inputOwner: status.inputOwner || 'unknown', pid: status.pid });
        }
        return;
      }
      // Preserve the launch grace until a child has actually been observed.
      if (Date.now() < watch.deadline && !watch.observedPresent) return;
      await close('closed');
    } catch {
      // Neither an established child nor a startup interrupted by sleep is
      // proved absent by a transport failure.
      if (readCurrent() && !watch.observedPresent && !powerInterrupted && Date.now() >= watch.deadline) await close('closed');
    } finally {
      if (current() && watch.read === read) {
        watch.read = null;
        if (returnCheckAt !== null) queueRead(returnDelay());
        else if (watch.observedPresent || Date.now() >= watch.deadline) queueRead(SESSION_AUDIT_MS);
        else queueRead(Math.min(readStartedAt + SESSION_STARTUP_POLL_MS, watch.deadline) - Date.now());
      }
    }
  };
  window.addEventListener('focus', hintReturn);
  document.addEventListener('visibilitychange', visibilityChanged);
  const removeOwnerHint = on<{ owner?: string }>('gamepad.input-owner', (data) => {
    if (data?.owner === 'yemancc') hintReturn();
  });
  let stopPower: (() => void) | undefined;
  watch.detachHints = () => {
    window.removeEventListener('focus', hintReturn);
    document.removeEventListener('visibilitychange', visibilityChanged);
    removeOwnerHint();
    stopPower?.();
  };
  stopPower = onSharedPowerLifecycle(power => {
    const next = sharedPowerWriteScope(power)?.join(':');
    if (!next || (powerScope && next !== powerScope)) powerInterrupted = true;
    if (next === powerScope) return;
    powerScope = next;
    if (watch.timer !== null) clearTimeout(watch.timer);
    watch.timer = null;
    returnCheckAt = null;
    const read = watch.read;
    watch.read = null;
    read?.abort();
    if (next) void poll();
  }, { observeNotificationSequence: true, observeSuspendSerial: true });
  if (!current()) stopPower();
}

export async function launchCustomSteamLibrary(): Promise<CustomSteamLibraryLaunchResult> {
  if (!CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED) {
    return { ok: false, executable: '', reason: 'Custom Steam Library integration is hidden' };
  }
  const observedScope = () => sharedPowerWriteScope(getSharedPowerLifecycle())?.join(':');
  const powerScope = sharedPowerWriteScope(getSharedPowerLifecycle());
  if (!powerScope) return { ok: false, executable: '', reason: '电源状态尚未就绪，请稍后重试' };
  const origin = powerScope.join(':');
  const generation = ++sessionGeneration;
  openRequested = true;
  launchPreparation?.abort();
  const preparation = new AbortController();
  launchPreparation = preparation;
  const admitted = () => !preparation.signal.aborted && observedScope() === origin;
  let stopPower: (() => void) | undefined;
  let enableSent = false;
  let executable = '';
  const superseded = (): CustomSteamLibraryLaunchResult => ({ ok: false, executable,
    reason: 'Custom Steam Library session was stopped or replaced' });
  const retire = async (): Promise<CustomSteamLibraryLaunchResult> => {
    if (enableSent) await stopNativeIntegrationSession(generation);
    return { ok: false, executable, reason: '电源状态已变化，请重新打开游戏库' };
  };
  try {
    stopPower = onSharedPowerLifecycle(() => {
      if (!admitted()) preparation.abort();
    }, { observeNotificationSequence: true, observeSuspendSerial: true });
    await mutateSession(generation, recoverPriorMutation);
    if (generation !== sessionGeneration) return superseded();
    if (!admitted()) return await retire();
    if (recoveredLaunch) {
      const result = recoveredLaunch;
      recoveredLaunch = undefined;
      beginSessionWatch(generation, true);
      return { ok: true, executable, pid: result.pid };
    }
    clearSessionWatch();
    // Native remains the sole input owner. Mutations have no local abort or
    // timeout: an unknown native outcome cannot grant a newer launch passage.
    const armed = await setCustomSteamLibrarySessionIntent(true, generation, () => {
      if (!admitted()) return false;
      enableSent = true;
      return true;
    }, powerScope);
    if (generation !== sessionGeneration) return superseded();
    if (!admitted()) return await retire();
    if (armed !== true) throw new Error('Custom Steam Library session was not accepted');
    executable = await resolveExecutable(preparation.signal);
    if (generation !== sessionGeneration) return superseded();
    if (!admitted()) return await retire();
    const parentPid = await app.pid(MUTATION_READ_MS, preparation.signal);
    if (generation !== sessionGeneration) return superseded();
    const launched = await mutateSession(generation, () => admitted() ? submitSessionMutation('launch', generation, admitted,
      mutationTicket => shell.hidden(executable, [
      '--integration=YeManCC',
      '--protocol=' + CUSTOM_STEAM_LIBRARY_PROTOCOL_VERSION,
      '--parent-pid=' + parentPid,
      '--input-owner=parent',
    ], MUTATION_ACK_MS, false, { powerScope, mutationTicket })) : undefined);
    if (generation !== sessionGeneration) return superseded();
    if (launched?.ok === true) {
      const readyForHandoff = admitted();
      beginSessionWatch(generation, !readyForHandoff);
      if (readyForHandoff && admitted()) requestYeManWindowAction('minimize');
    } else {
      if (!admitted()) return await retire();
      await stopNativeIntegrationSession(generation);
    }
    return { ok: launched?.ok === true, executable, pid: launched?.pid };
  } catch (error) {
    if (generation !== sessionGeneration) return superseded();
    if (error instanceof MutationUnconfirmed) return { ok: false, executable, reason: error.message };
    if (!admitted()) return await retire();
    clearSessionWatch();
    if (enableSent) await stopNativeIntegrationSession(generation);
    throw error;
  } finally {
    if (launchPreparation === preparation) launchPreparation = null;
    stopPower?.();
  }
}

export async function stopCustomSteamLibrarySession(): Promise<void> {
  const generation = ++sessionGeneration;
  openRequested = false;
  recoveredLaunch = undefined;
  launchPreparation?.abort();
  clearSessionWatch();
  await stopNativeIntegrationSession(generation);
}
