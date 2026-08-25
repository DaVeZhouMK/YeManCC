// Shared game detection for all pages.
// Process enumeration is performed by the native shell so polling never
// starts a short-lived PowerShell process.
import { invoke } from './ipc';
import { registerScheduledTask } from '@/scheduler';

export interface DetectedGame {
  pid: number;
  name: string;
  title: string;
  path: string;
  ts: number;
  processCreated: string;
  source?: 'memory' | 'whitelist' | string;
  whitelistRule?: string;
}

export function stripLaunchModeSuffix(value: string): string {
  let title = value.trim().replace(/\.exe$/i, '');
  const suffixes = [
    /\s*[-|:\u2013\u2014\s]*(?:direct\s*x|directx|dx)\s*(?:9|10|11|12)(?:\s*x64)?\s*$/i,
    /\s*\((?:direct\s*x|directx|dx)\s*(?:9|10|11|12)(?:\s*x64)?\)\s*$/i,
    /\s*[-|:\u2013\u2014\s]*(?:vulkan|open\s*gl)(?:\s*x64)?\s*$/i,
    /\s*\((?:vulkan|open\s*gl)(?:\s*x64)?\)\s*$/i,
  ];
  let changed = true;
  while (changed) {
    changed = false;
    for (const suffix of suffixes) {
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
  title = title.replace(/\s+\bby\b[\s\S]*$/i, '').trim();
  title = title.replace(/\s*(?:\(\s*c\s*\)|\[\s*c\s*\]|\bcopyright\b)\s*\d{4}\s*$/i, '').trim();
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

let preferredRefreshEpoch = 0;
const detectInFlight = new Map<number, Promise<DetectedGame | null>>();

async function runDetection(preferredPid = 0): Promise<DetectedGame | null> {
  // Keep transport/WebView failures distinct from a successful "no game"
  // result. Treating a transient IPC failure as null used to clear the active
  // speed target, so a later click could operate on stale injection state.
  const raw = await invoke<Partial<DetectedGame> | null>('game.detect', preferredPid > 0 ? { pid: preferredPid } : {});
  const pid = Number(raw?.pid);
  const processCreated = raw?.processCreated !== undefined ? String(raw.processCreated) : '';
  if (!raw || !Number.isSafeInteger(pid) || pid <= 0 ||
      !/^\d+$/.test(processCreated) || processCreated === '0') return null;
  return {
    pid,
    name: String(raw.name || ''),
    title: String(raw.title || ''),
    path: String(raw.path || ''),
    ts: Number(raw.ts) || Date.now(),
    processCreated,
    source: raw.source ? String(raw.source) : undefined,
    whitelistRule: raw.whitelistRule ? String(raw.whitelistRule) : undefined,
  };
}

export async function detectGame(force = false, preferredPid = 0): Promise<DetectedGame | null> {
  const pid = Number.isInteger(preferredPid) && preferredPid > 0 ? preferredPid : 0;
  // Native owns the authoritative target and validates PID reuse on every
  // request. Do not let a JS cache bypass the game recognition valve.
  let inFlight = detectInFlight.get(pid);
  if (!inFlight) {
    inFlight = runDetection(pid).finally(() => {
      if (detectInFlight.get(pid) === inFlight) detectInFlight.delete(pid);
    });
    detectInFlight.set(pid, inFlight);
  }
  const game = await inFlight;
  return game;
}

export async function isGameRunning(): Promise<boolean> {
  return (await detectGame()) !== null;
}

export function clearGameCache(): void {
  // Kept as a compatibility API. Recognition state lives in native.
}

// The exclude list is now loaded by native Sleep Guard and game.detect on
// every request. Keep this export for callers that used the old JS cache API.
export function invalidateExcludeCache(): void {
  clearGameCache();
}

export type GameStatusListener = (game: DetectedGame | null) => void;

const listeners = new Set<GameStatusListener>();
let currentGame: DetectedGame | null = null;
let stopPollSchedule: (() => void) | null = null;
const refreshInFlight = new Map<number, Promise<DetectedGame | null>>();
let preferredRefreshInFlight: Promise<DetectedGame | null> | null = null;
// The native valve remains authoritative; this only shortens the UI status
// refresh interval so a newly started game appears sooner.
const POLL_MS = 1250;

function sameGame(a: DetectedGame | null, b: DetectedGame | null): boolean {
  if (a === b) return true;
  if (!a || !b) return false;
  return a.pid === b.pid && a.processCreated === b.processCreated &&
    a.name === b.name && a.path === b.path && a.source === b.source &&
    a.whitelistRule === b.whitelistRule;
}

function emitGame(game: DetectedGame | null): void {
  if (sameGame(game, currentGame)) {
    currentGame = game;
    return;
  }
  currentGame = game;
  for (const cb of [...listeners]) cb(game);
}

/**
 * Strict, user-requested refresh. Unlike the background-friendly refresh
 * above, an IPC/transport failure is propagated instead of returning the
 * previous cached game as if the refresh had succeeded.
 */
export async function refreshGameStatusStrict(preferredPid = 0): Promise<DetectedGame | null> {
  const pid = Number.isInteger(preferredPid) && preferredPid > 0 ? preferredPid : 0;
  const game = await detectGame(true, pid);
  emitGame(game);
  return game;
}

export async function refreshGameStatus(preferredPid = 0): Promise<DetectedGame | null> {
  const pid = Number.isInteger(preferredPid) && preferredPid > 0 ? preferredPid : 0;
  if (!pid && preferredRefreshInFlight) return preferredRefreshInFlight;
  const existing = refreshInFlight.get(pid);
  if (existing) return existing;
  const epochAtStart = preferredRefreshEpoch;
  const preferredEpoch = pid ? ++preferredRefreshEpoch : epochAtStart;
  if (pid) detectInFlight.delete(0);
  const refresh = (async () => {
    try {
      clearGameCache();
      const game = await detectGame(true, pid);
      // A normal poll that started before or during a preferred-PID refresh
      // must not overwrite the explicitly captured target.
      if (pid ? preferredEpoch === preferredRefreshEpoch : epochAtStart === preferredRefreshEpoch) {
        emitGame(game);
      }
      return game;
    } catch {
      return currentGame;
    } finally {
      if (pid && preferredRefreshInFlight === refresh) preferredRefreshInFlight = null;
    }
  })();
  refreshInFlight.set(pid, refresh);
  if (pid) preferredRefreshInFlight = refresh;
  try {
    return await refresh;
  } finally {
    if (refreshInFlight.get(pid) === refresh) refreshInFlight.delete(pid);
  }
}

export function subscribeGameStatus(cb: GameStatusListener): () => void {
  listeners.add(cb);
  cb(currentGame);
  if (listeners.size === 1) {
    stopPollSchedule = registerScheduledTask(
      'game-status',
      POLL_MS,
      refreshGameStatus,
      { pauseWhenHidden: true, runImmediately: true },
    );
  }
  return () => {
    listeners.delete(cb);
    if (listeners.size === 0) {
      stopPollSchedule?.();
      stopPollSchedule = null;
    }
  };
}

if (typeof window !== 'undefined') {
  window.addEventListener('ipc:game.rules.changed', () => {
    clearGameCache();
    void refreshGameStatus();
  });
}
