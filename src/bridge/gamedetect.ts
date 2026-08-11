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

let cached: { ts: number; game: DetectedGame | null } | null = null;
const CACHE_MS = 5000;
let detectInFlight: Promise<DetectedGame | null> | null = null;

async function runDetection(): Promise<DetectedGame | null> {
  try {
    const raw = await invoke<Partial<DetectedGame> | null>('game.detect');
    if (!raw || Number(raw.pid) <= 0) return null;
    return {
      pid: Number(raw.pid),
      name: String(raw.name || ''),
      title: String(raw.title || ''),
      path: String(raw.path || ''),
      ts: Number(raw.ts) || Date.now(),
    };
  } catch {
    return null;
  }
}

export async function detectGame(force = false): Promise<DetectedGame | null> {
  const now = Date.now();
  if (!force && cached && now - cached.ts < CACHE_MS) return cached.game;
  if (!detectInFlight) {
    detectInFlight = runDetection().finally(() => {
      detectInFlight = null;
    });
  }
  const game = await detectInFlight;
  cached = { ts: Date.now(), game };
  return game;
}

export async function isGameRunning(): Promise<boolean> {
  return (await detectGame()) !== null;
}

export function clearGameCache(): void {
  cached = null;
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
let pollBusy = false;
const POLL_MS = 2500;

function sameGame(a: DetectedGame | null, b: DetectedGame | null): boolean {
  if (a === b) return true;
  if (!a || !b) return false;
  return a.pid === b.pid && a.name === b.name;
}

function emitGame(game: DetectedGame | null): void {
  if (sameGame(game, currentGame)) {
    currentGame = game;
    return;
  }
  currentGame = game;
  for (const cb of [...listeners]) cb(game);
}

export async function refreshGameStatus(): Promise<DetectedGame | null> {
  if (pollBusy) return currentGame;
  pollBusy = true;
  try {
    clearGameCache();
    const game = await detectGame(true);
    emitGame(game);
    return game;
  } catch {
    return currentGame;
  } finally {
    pollBusy = false;
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
