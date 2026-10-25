// Shared game detection for all pages.
// Process enumeration is performed by the native shell so polling never
// starts a short-lived PowerShell process.
import { invoke } from './ipc';
import gameTitlePolicy from '../../PowerControl/GameTitlePolicy.json';
import { registerScheduledTask } from '@/scheduler';

export interface DetectedGame {
  pid: number;
  name: string;
  title: string;
  path: string;
  ts: number;
  processCreated: string;
  /** Ordered native observation and authoritative selection lease. */
  generation?: number;
  identityGeneration?: number;
  source?: 'memory' | 'whitelist' | string;
  whitelistRule?: string;
}

// These policies are immutable; do not compile the same patterns for every label.
const launchModeSuffixes = gameTitlePolicy.launchSuffixes.map(pattern => new RegExp(pattern, 'i'));
const copyrightSuffix = new RegExp(gameTitlePolicy.copyrightSuffix, 'i');

export function stripLaunchModeSuffix(value: string): string {
  let title = value.trim().replace(/\.exe$/i, '');
  let changed = true;
  while (changed) {
    changed = false;
    for (const suffix of launchModeSuffixes) {
      const next = title.replace(suffix, '').trim();
      if (next !== title) {
        title = next;
        changed = true;
      }
    }
  }
  return title;
}

export function cleanGameTitle(value: string): string {
  let title = stripLaunchModeSuffix(value).replace(/\s+/g, ' ').trim();
  title = title.replace(copyrightSuffix, '').trim();
  return title.replace(/[|:,-]+\s*$/, '').trim();
}

export function normalizeGameTitle(value: string): string {
  return cleanGameTitle(value)
    .normalize('NFKD')
    .replace(/[\u2013\u2014]/g, '')
    .replace(/&/g, ' and ')
    .replace(/[^a-z0-9]+/gi, '')
    .toLowerCase();
}

export function detectedGameName(game: Pick<DetectedGame, 'title' | 'name'> | null | undefined): string {
  if (!game) return '';
  return cleanGameTitle(game.title || '') || cleanGameTitle(game.name || '');
}

export type GameStatusListener = (game: DetectedGame | null) => void;
export type GameTargetIdentity = Pick<DetectedGame, 'pid' | 'processCreated'> &
  Partial<Pick<DetectedGame, 'path' | 'identityGeneration'>>;

type NativeGameSnapshot = Partial<DetectedGame> & { valid?: boolean };
interface Observation { game: DetectedGame | null; generation: number; identityGeneration: number }
const listeners = new Set<GameStatusListener>();
const readinessListeners = new Set<(current: boolean) => void>();
const readinessNotifications: boolean[] = [];
let lastReadiness = false;
let notifyingReadiness = false;
let currentGame: DetectedGame | null = null;
let initialized = false;
let currentGeneration = 0;
let currentIdentityGeneration = 0;
let notifiedGeneration = 0;
let versioned = false;
let selectionEpoch = 0;
let committedSelectionEpoch = 0;
let invalidationEpoch = 0;
let cacheDirty = false;
type ReadOwner = { foreground: boolean };
let normalRead: { epoch: number; owner: ReadOwner; promise: Promise<DetectedGame | null> } | null = null;
let selectionRead: { pid: number; epoch: number; promise: Promise<DetectedGame | null> } | null = null;
let stopPollSchedule: (() => void) | null = null;
const DETECTION_TIMEOUT_MS = 8000;
const POLL_MS = 1250;
const EVENT_COALESCE_MS = 100;
const RECOVERY_DELAYS_MS = [500, 1500, 4000] as const;
let lastNativeReadAt = -Infinity;
let nextEventReadAt = 0;
let recoveryNotBefore = 0;
let recoveryAttempts = 0;
let backgroundTimer: ReturnType<typeof setTimeout> | null = null;
let backgroundTimerAt = 0;

function cancelBackgroundRead(): void {
  if (backgroundTimer !== null) clearTimeout(backgroundTimer);
  backgroundTimer = null;
}
function queueBackgroundRead(at: number): void {
  if (!listeners.size || backgroundTimer !== null && backgroundTimerAt <= at) return;
  cancelBackgroundRead();
  backgroundTimerAt = at;
  backgroundTimer = setTimeout(() => {
    backgroundTimer = null;
    void refreshBackground('event');
  }, Math.max(0, at - performance.now()));
}
function recoverAfterFailure(): void {
  const delay = RECOVERY_DELAYS_MS[Math.min(recoveryAttempts, RECOVERY_DELAYS_MS.length - 1)];
  recoveryNotBefore = performance.now() + delay;
  // A failed read has only three independent recovery wakes. Further recovery
  // requires the existing poll, a real notification, or an explicit caller.
  if (recoveryAttempts < RECOVERY_DELAYS_MS.length) {
    recoveryAttempts += 1;
    queueBackgroundRead(recoveryNotBefore);
  }
}
function readForBackground(): Promise<DetectedGame | null> {
  return readAuthority(0, true).catch(() => getCurrentGame());
}
function refreshBackground(reason: 'event' | 'poll'): Promise<DetectedGame | null> {
  if (!listeners.size) return Promise.resolve(getCurrentGame());
  if (selectionRead || normalRead?.epoch === selectionEpoch) return readForBackground();
  const now = performance.now();
  const due = Math.max(recoveryNotBefore, reason === 'poll' ? lastNativeReadAt + POLL_MS : nextEventReadAt);
  if (now < due) {
    // Polling already has a future slot. Events retain a single trailing read.
    if (reason === 'event') queueBackgroundRead(due);
    return Promise.resolve(getCurrentGame());
  }
  cancelBackgroundRead();
  if (reason === 'event') nextEventReadAt = now + EVENT_COALESCE_MS;
  return readForBackground();
}

export function getCurrentGame(): DetectedGame | null {
  return currentGame ? { ...currentGame } : null;
}
export function isGameStatusInitialized(): boolean { return initialized; }
export function isGameStatusCurrent(): boolean {
  return initialized && committedSelectionEpoch === selectionEpoch && !cacheDirty && currentGeneration >= notifiedGeneration;
}

function notifyReadiness(): void {
  const current = isGameStatusCurrent();
  if (current === lastReadiness) return;
  lastReadiness = current;
  readinessNotifications.push(current);
  if (notifyingReadiness) return;
  notifyingReadiness = true;
  try {
    while (readinessNotifications.length) {
      const snapshot = readinessNotifications.shift()!;
      for (const cb of [...readinessListeners]) {
        if (!readinessListeners.has(cb)) continue;
        try { cb(snapshot); } catch { /* Readiness observers cannot stop recovery. */ }
      }
    }
  } finally { notifyingReadiness = false; }
}
/** Display admission changes independently from game identity; never replay policies. */
export function subscribeGameTargetReadiness(cb: (current: boolean) => void): () => void {
  const observer = (current: boolean) => cb(current);
  readinessListeners.add(observer);
  try { observer(isGameStatusCurrent()); } catch { /* Isolate initial observations too. */ }
  return () => { readinessListeners.delete(observer); };
}

function sameIdentity(a: GameTargetIdentity, b: DetectedGame): boolean {
  return a.pid === b.pid && String(a.processCreated) === b.processCreated &&
    (!a.path || a.path.toLowerCase() === b.path.toLowerCase()) &&
    (!a.identityGeneration || !b.identityGeneration || a.identityGeneration === b.identityGeneration);
}
export function isCurrentGameTarget(target: GameTargetIdentity): boolean {
  return isGameStatusCurrent() && !!currentGame && sameIdentity(target, currentGame);
}

function sameGame(a: DetectedGame | null, b: DetectedGame | null): boolean {
  if (!a || !b) return a === b;
  return a.pid === b.pid && a.processCreated === b.processCreated &&
    a.identityGeneration === b.identityGeneration && a.name === b.name &&
    a.title === b.title && a.path === b.path && a.source === b.source && a.whitelistRule === b.whitelistRule;
}
function publish(observation: Observation): void {
  const changed = !initialized || !sameGame(observation.game, currentGame) ||
    observation.identityGeneration !== currentIdentityGeneration;
  currentGame = observation.game ? { ...observation.game } : null;
  currentGeneration = observation.generation;
  currentIdentityGeneration = observation.identityGeneration;
  initialized = true;
  cacheDirty = false;
  recoveryAttempts = 0;
  recoveryNotBefore = 0;
  cancelBackgroundRead();
  committedSelectionEpoch = selectionEpoch;
  notifyReadiness();
  if (!changed) return;
  for (const cb of [...listeners]) {
    if (!listeners.has(cb)) continue;
    try { cb(getCurrentGame()); } catch { /* A consumer cannot prevent another consumer receiving a committed target. */ }
  }
}
function decodeSnapshot(raw: NativeGameSnapshot | null): Observation {
  // A matched native shell versions valid and no-game observations alike.
  // Legacy unversioned shells remain readable, but cannot roll back a versioned session.
  const generation = Number(raw?.generation ?? 0);
  const identityGeneration = Number(raw?.identityGeneration ?? 0);
  const hasVersion = raw?.generation !== undefined || raw?.identityGeneration !== undefined;
  if (hasVersion && (!Number.isSafeInteger(generation) || generation <= 0 ||
      !Number.isSafeInteger(identityGeneration) || identityGeneration <= 0 || identityGeneration > generation)) {
    throw new Error('Invalid game target generation');
  }
  if (!hasVersion && versioned) throw new Error('Unversioned game target response');
  if (!raw) return { game: null, generation: 0, identityGeneration: 0 };
  if (raw.valid === false) {
    if (Number(raw.pid) !== 0) throw new Error('Invalid empty game target');
    return { game: null, generation, identityGeneration };
  }
  const pid = Number(raw.pid);
  const processCreated = raw.processCreated !== undefined ? String(raw.processCreated) : '';
  if (!Number.isSafeInteger(pid) || pid <= 0 || !/^\d+$/.test(processCreated) || /^0+$/.test(processCreated)) {
    throw new Error('Invalid game target identity');
  }
  return { generation, identityGeneration, game: {
    pid, processCreated,
    name: String(raw.name || ''), title: String(raw.title || ''), path: String(raw.path || ''),
    ts: Number(raw.ts) || Date.now(),
    ...(hasVersion ? { generation, identityGeneration } : {}),
    source: raw.source ? String(raw.source) : undefined,
    whitelistRule: raw.whitelistRule ? String(raw.whitelistRule) : undefined,
  } };
}
function followNewSelection(epoch: number): DetectedGame | null | Promise<DetectedGame | null> {
  // The superseding selection owns its result and failure, including before IO.
  const selected = selectionRead;
  if (selected && selected.epoch > epoch) return selected.promise;
  if (!initialized || committedSelectionEpoch !== selectionEpoch || !isGameStatusCurrent()) throw new Error('Game target selection changed');
  return getCurrentGame();
}
async function observeNative(preferredPid: number, epoch: number, owner: ReadOwner): Promise<DetectedGame | null> {
  for (let attempt = 0; attempt <= 2; attempt += 1) {
    if (!owner.foreground && !listeners.size) throw new Error('Game target background observation stopped');
    // Superseded selections that never reached native must not mutate its valve.
    if (epoch !== selectionEpoch) return followNewSelection(epoch);
    cancelBackgroundRead();
    lastNativeReadAt = performance.now();
    const invalidationAtStart = invalidationEpoch;
    const raw = await invoke<NativeGameSnapshot | null>('game.detect', preferredPid > 0 && attempt === 0 ? { pid: preferredPid } : {}, { timeoutMs: DETECTION_TIMEOUT_MS });
    if (!owner.foreground && !listeners.size) throw new Error('Game target background observation stopped');
    if (epoch !== selectionEpoch) {
      return followNewSelection(epoch);
    }
    if (invalidationAtStart !== invalidationEpoch) {
      if (attempt < 2) continue;
      throw new Error('Game rules changed while reading');
    }
    const observation = decodeSnapshot(raw);
    const floor = Math.max(currentGeneration, notifiedGeneration);
    if (observation.generation < floor) {
      if (currentGeneration >= notifiedGeneration && initialized && !cacheDirty) return getCurrentGame();
      if (attempt < 2) continue;
      throw new Error('Game target changed while reading');
    }
    if (versioned && (observation.identityGeneration < currentIdentityGeneration ||
        (observation.identityGeneration === currentIdentityGeneration &&
          (!!observation.game !== !!currentGame || observation.game && currentGame &&
            (observation.game.pid !== currentGame.pid || observation.game.processCreated !== currentGame.processCreated ||
             observation.game.path.toLowerCase() !== currentGame.path.toLowerCase() ||
             observation.game.source !== currentGame.source || observation.game.whitelistRule !== currentGame.whitelistRule))))) {
      throw new Error('Conflicting game target identity generation');
    }
    if (versioned && observation.generation === currentGeneration &&
        (!sameGame(observation.game, currentGame) || observation.identityGeneration !== currentIdentityGeneration)) {
      throw new Error('Conflicting game target generation');
    }
    if (observation.generation) versioned = true;
    publish(observation);
    // A reentrant consumer may request a newer selection during publication.
    if (epoch !== selectionEpoch && selectionRead) return selectionRead.promise;
    return getCurrentGame();
  }
  throw new Error('Game target observation unavailable');
}
function readAuthority(preferredPid = 0, background = false): Promise<DetectedGame | null> {
  const pid = Number.isSafeInteger(preferredPid) && preferredPid > 0 ? preferredPid : 0;
  if (pid && selectionRead?.pid === pid) return selectionRead.promise;
  if (!pid && selectionRead) return selectionRead.promise;
  if (!pid && normalRead?.epoch === selectionEpoch) {
    // A foreground action can retain a read that began as background discovery.
    if (!background) normalRead.owner.foreground = true;
    return normalRead.promise;
  }
  const epoch = pid ? ++selectionEpoch : selectionEpoch;
  const owner: ReadOwner = { foreground: !background || pid > 0 };
  // Defer native IO until its owner is installed, including reentrant mock/bridge notifications.
  const promise = Promise.resolve().then(() => observeNative(pid, epoch, owner)).catch((error) => {
    // Keep the last display snapshot, but an unconfirmed authority cannot admit a new mutation.
    // A late failure belonging to a superseded selection cannot dirty its successor.
    if (epoch === selectionEpoch) {
      cacheDirty = true;
      recoverAfterFailure();
      notifyReadiness();
    }
    throw error;
  }).finally(() => {
    if (selectionRead?.promise === promise) selectionRead = null;
    if (normalRead?.promise === promise) normalRead = null;
  });
  if (pid) selectionRead = { pid, epoch, promise };
  else normalRead = { epoch, owner, promise };
  if (pid) notifyReadiness();
  return promise;
}

/** All readers consume the same committed target. A forced read also publishes it. */
export async function detectGame(force = false, preferredPid = 0): Promise<DetectedGame | null> {
  if (preferredPid > 0 || force || !initialized || cacheDirty || currentGeneration < notifiedGeneration) {
    const game = await readAuthority(preferredPid);
    return game ? { ...game } : null;
  }
  return getCurrentGame();
}

/** Read-only validation: an old operation must never re-elect its captured PID. */
export async function validateGameTarget(target: GameTargetIdentity): Promise<DetectedGame | null> {
  const game = await refreshGameStatusStrict();
  return game && sameIdentity(target, game) && isCurrentGameTarget(target) ? { ...game } : null;
}
export async function isGameRunning(): Promise<boolean> { return (await detectGame()) !== null; }
export function clearGameCache(): void {
  // Keep the committed identity until a successful native observation replaces it.
  // This compatibility API requests freshness on the next ordinary read.
  cacheDirty = true;
  invalidationEpoch += 1;
  notifyReadiness();
}
export function invalidateExcludeCache(): void { clearGameCache(); }

export async function refreshGameStatusStrict(preferredPid = 0): Promise<DetectedGame | null> {
  const game = await readAuthority(preferredPid);
  return game ? { ...game } : null;
}
export async function refreshGameStatus(preferredPid = 0): Promise<DetectedGame | null> {
  try { return await refreshGameStatusStrict(preferredPid); }
  catch { return getCurrentGame(); } // Transport failure is unknown, never proof of exit.
}

export function subscribeGameStatus(cb: GameStatusListener): () => void {
  const observer: GameStatusListener = (game) => cb(game);
  listeners.add(observer);
  try { observer(getCurrentGame()); } catch { /* Isolate initial observers too. */ }
  if (listeners.size > 0 && !stopPollSchedule) {
    stopPollSchedule = registerScheduledTask(
      'game-status', POLL_MS, () => refreshBackground('poll'),
      { pauseWhenHidden: false, runImmediately: true },
    );
  }
  return () => {
    listeners.delete(observer);
    if (listeners.size === 0) {
      stopPollSchedule?.(); stopPollSchedule = null;
      cancelBackgroundRead();
    }
  };
}

if (typeof window !== 'undefined') {
  const reconcile = () => { if (listeners.size) void refreshBackground('event'); };
  window.addEventListener('ipc:game.target.changed', (event) => {
    const generation = Number((event as CustomEvent<{ generation?: unknown }>).detail?.generation);
    // Native events invalidate observations, but event PID/path data grants no authority.
    if (Number.isSafeInteger(generation) && generation > 0) {
      if (generation <= currentGeneration && isGameStatusCurrent()) return;
      notifiedGeneration = Math.max(notifiedGeneration, generation);
      notifyReadiness();
    } else {
      // Legacy hints cannot supersede an explicit PID selection. Matched native
      // shells use generation ordering; old shells retain the strict result.
      if (!selectionRead) clearGameCache();
    }
    reconcile();
  });
  for (const event of ['ipc:window.shown', 'ipc:window.restored', 'ipc:window.summoned',
    'ipc:window.focus', 'ipc:power.resumed']) window.addEventListener(event, reconcile);
  if (typeof document !== 'undefined') document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') reconcile();
  });
  window.addEventListener('ipc:webview.recovering', () => {
    clearGameCache();
    recoveryNotBefore = Math.max(recoveryNotBefore, performance.now() + RECOVERY_DELAYS_MS[0]);
    queueBackgroundRead(recoveryNotBefore);
  });
  window.addEventListener('ipc:game.rules.changed', () => { clearGameCache(); reconcile(); });
}
