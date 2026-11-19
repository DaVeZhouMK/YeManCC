export interface GameSteamTarget {
  pid: number;
  processCreated: string;
  path: string;
  identityGeneration: number;
}
export interface GameSteamGame extends GameSteamTarget {
  rulesEpoch: string;
  powerGeneration: string;
  processValidated: false;
}
interface Observation {
  processValidated: false;
  currentSource: false;
  reason?: string;
}
export interface CachedSteamGroupMember {
  pid: number;
  path: string;
  /** Original local label from the log, not an execution timestamp. */
  observedAt: string;
  sourceLine: number;
  gameIdRaw: string;
}
interface GroupMetadata {
  scope: 'process-lifetime';
  processValidated: false;
  currentSource: false;
  roleValidated: false;
  reason: string;
}
export type CachedSteamProcessGroup = GroupMetadata & (
  { status: 'unknown' | 'incomplete' } |
  { status: 'observed'; sourceRevision: string; selectedPid: number; selectedSourceLine: number;
    appid: number; gameId: string; gameIdRaw: string; gameIdKind: 'native-app' | 'shortcut-type2';
    members: CachedSteamGroupMember[] }
);
export interface CachedSteamAssociation extends Observation {
  status: 'cached-association';
  appid: number;
  /** Steam game identity, never an account SteamID; retain the exact decimal string. */
  gameId: string;
  gamePid: number;
  gameCreated: string;
  gamePath: string;
  gameIdentityGeneration: number;
  steamPid: number;
  steamCreated: string;
  processEpoch: string;
  accountEpoch: string;
  /** Historical members only. No member birth/liveness or role permission. */
  processGroup?: CachedSteamProcessGroup;
}
export type GameSteamObservation = CachedSteamAssociation | (Observation & { status: 'unknown' | 'pending' });
/** Fixed native wire DTO. Optional Steam fields are required before accepting an association. */
export interface GameSteamContextResponse {
  schemaVersion: 1;
  game: GameSteamGame | null;
  steam: Observation & {
    status: 'cached-association' | 'unknown' | 'pending';
    appid?: number;
    gameId?: string;
    gamePid?: number;
    gameCreated?: string;
    gamePath?: string;
    gameIdentityGeneration?: number;
    steamPid?: number;
    steamCreated?: string;
    processEpoch?: string;
    accountEpoch?: string;
    processGroup?: unknown;
  };
}
export interface GameSteamContext {
  schemaVersion: 1;
  /** Null means the original requested game is unknown/stale, never proven absent. */
  game: (GameSteamTarget & { processValidated: false; rulesEpoch?: string; powerGeneration?: string }) | null;
  steam: GameSteamObservation;
}
type RecordValue = Record<string, unknown>;
const record = (value: unknown): value is RecordValue => value !== null && typeof value === 'object' && !Array.isArray(value);
const uint32 = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value > 0 && value <= 0xffffffff;
const generation = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value > 0;
function uint64(value: unknown): value is string {
  return typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value) &&
    (value.length < 20 || value <= '18446744073709551615');
}
function gameIdMatches(appid: number, gameId: string): boolean {
  const app = BigInt(appid), full = BigInt(gameId);
  return (appid <= 0xffffff && full === app) || full === ((app << 32n) | 0x02000000n);
}
function groupPath(value: unknown): value is string {
  return typeof value === 'string' && !!value.trim() && value.length <= 32767 && !value.includes('\0');
}
function groupPathKey(value: string): string {
  const path = value.replace(/\//g, '\\').replace(/[A-Z]/g, value => value.toLowerCase());
  return path.startsWith('\\\\?\\unc\\') ? '\\\\' + path.slice(8) : path.startsWith('\\\\?\\') ? path.slice(4) : path;
}
function groupRawId(value: unknown, expected: string): value is string {
  return typeof value === 'string' && /^[0-9]{1,20}$/.test(value) && BigInt(value) === BigInt(expected);
}
function parseGroup(raw: unknown, game: GameSteamTarget, appid: number, gameId: string): CachedSteamProcessGroup | null {
  const failed = (_reason: string): null => null;
  if (!record(raw) || raw.scope !== 'process-lifetime' || raw.processValidated !== false || raw.currentSource !== false ||
      raw.roleValidated !== false || typeof raw.reason !== 'string' || raw.reason.length > 160) return failed('group-response-invalid');
  if (raw.status === 'unknown' || raw.status === 'incomplete') {
    if (Object.keys(raw).some(key => !['status','reason','scope','processValidated','currentSource','roleValidated'].includes(key))) return failed('group-response-invalid');
    return {status:raw.status,reason:raw.reason,scope:'process-lifetime',processValidated:false,currentSource:false,roleValidated:false};
  }
  if (Object.keys(raw).some(key => !['status','reason','scope','processValidated','currentSource','roleValidated','sourceRevision','selectedPid','selectedSourceLine','appid','gameId','gameIdRaw','gameIdKind','members'].includes(key)) ||
      raw.status !== 'observed' || !uint64(raw.sourceRevision) || raw.selectedPid !== game.pid ||
      !uint32(raw.selectedSourceLine) || raw.appid !== appid || raw.gameId !== gameId || !groupRawId(raw.gameIdRaw, gameId) ||
      raw.gameIdKind !== (BigInt(gameId) === BigInt(appid) ? 'native-app' : 'shortcut-type2') ||
      !Array.isArray(raw.members) || !raw.members.length || raw.members.length > 32) return failed('group-association-mismatch');
  const ids = new Set<number>(), members: CachedSteamGroupMember[] = [];
  let selected = 0;
  for (const row of raw.members) {
    if (!record(row) || Object.keys(row).some(key => !['pid','path','observedAt','sourceLine','gameIdRaw'].includes(key)) ||
        !uint32(row.pid) || ids.has(row.pid) || !groupPath(row.path) || !uint32(row.sourceLine) ||
        typeof row.observedAt !== 'string' || !/^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}$/.test(row.observedAt) ||
        !groupRawId(row.gameIdRaw, gameId)) return failed('group-member-unconfirmed');
    ids.add(row.pid);
    if (row.pid === game.pid) {
      if (row.sourceLine !== raw.selectedSourceLine || row.gameIdRaw !== raw.gameIdRaw ||
          groupPathKey(row.path) !== groupPathKey(game.path)) return failed('group-selected-row-mismatch');
      selected++;
    }
    members.push({pid:row.pid,path:row.path,observedAt:row.observedAt,sourceLine:row.sourceLine,gameIdRaw:row.gameIdRaw});
  }
  if (selected !== 1) return failed('group-selected-row-missing');
  return {status:'observed',reason:raw.reason,scope:'process-lifetime',processValidated:false,currentSource:false,roleValidated:false,
    sourceRevision:raw.sourceRevision,selectedPid:game.pid,selectedSourceLine:raw.selectedSourceLine,appid,gameId,gameIdRaw:raw.gameIdRaw,
    gameIdKind:BigInt(gameId) === BigInt(appid) ? 'native-app' : 'shortcut-type2',members};
}
export function target(value: unknown): GameSteamTarget | null {
  if (!record(value) || !uint32(value.pid) || !uint64(value.processCreated) ||
      typeof value.path !== 'string' || !value.path.trim() || value.path.length > 32767 || value.path.includes('\0') ||
      !generation(value.identityGeneration)) return null;
  return { pid: value.pid, processCreated: value.processCreated, path: value.path, identityGeneration: value.identityGeneration };
}
export function same(a: GameSteamTarget, b: GameSteamTarget): boolean {
  return a.pid === b.pid && a.processCreated === b.processCreated && a.path === b.path && a.identityGeneration === b.identityGeneration;
}
export function unknown(game: GameSteamContext['game'], reason: string): GameSteamContext {
  return { schemaVersion: 1, game, steam: { status: 'unknown', processValidated: false, currentSource: false, reason } };
}
export function decode(raw: unknown, expected: GameSteamTarget): GameSteamContext {
  const fallback = { ...expected, processValidated: false as const };
  if (!record(raw) || raw.schemaVersion !== 1 || !record(raw.game)) return unknown(fallback, 'invalid-game-response');
  const returned = target(raw.game);
  if (!returned || !same(returned, expected) || raw.game.processValidated !== false ||
      !uint64(raw.game.rulesEpoch) || !uint64(raw.game.powerGeneration)) return unknown(fallback, 'game-response-mismatch');
  const game: GameSteamGame = { ...returned, rulesEpoch: raw.game.rulesEpoch, powerGeneration: raw.game.powerGeneration, processValidated: false };
  const steam = raw.steam;
  if (!record(steam) || steam.processValidated !== false || steam.currentSource !== false ||
      (steam.reason !== undefined && typeof steam.reason !== 'string')) return unknown(game, 'invalid-steam-response');
  const reason = typeof steam.reason === 'string' ? { reason: steam.reason } : {};
  if (steam.status === 'unknown' || steam.status === 'pending') {
    return { schemaVersion: 1, game, steam: { status: steam.status, processValidated: false, currentSource: false, ...reason } };
  }
  if (steam.status !== 'cached-association' || !uint32(steam.appid) || !uint64(steam.gameId) ||
      steam.gamePid !== expected.pid || steam.gameCreated !== expected.processCreated || steam.gamePath !== expected.path ||
      steam.gameIdentityGeneration !== expected.identityGeneration || !uint32(steam.steamPid) || steam.steamPid === expected.pid || !uint64(steam.steamCreated) ||
      !uint64(steam.processEpoch) || !uint64(steam.accountEpoch) || !gameIdMatches(steam.appid, steam.gameId)) return unknown(game, 'steam-association-mismatch');
  return { schemaVersion: 1, game, steam: {
    status: 'cached-association', appid: steam.appid, gameId: steam.gameId,
    gamePid: expected.pid, gameCreated: expected.processCreated, gamePath: expected.path,
    gameIdentityGeneration: expected.identityGeneration, steamPid: steam.steamPid, steamCreated: steam.steamCreated,
    processEpoch: steam.processEpoch, accountEpoch: steam.accountEpoch,
    ...(steam.processGroup === undefined ? {} : {processGroup:parseGroup(steam.processGroup,expected,steam.appid,steam.gameId) ??
      {status:'incomplete' as const,scope:'process-lifetime' as const,processValidated:false as const,currentSource:false as const,roleValidated:false as const,reason:'group-response-invalid'}}),
    processValidated: false, currentSource: false, ...reason,
  } };
}

// Validation for the mirror transport. This is display metadata only.
export function validGameSteamContext(value: unknown): value is GameSteamContext {
  if (!record(value) || value.schemaVersion !== 1 || !record(value.steam)) return false;
  const steam = value.steam;
  if (steam.processValidated !== false || steam.currentSource !== false ||
      (steam.reason !== undefined && typeof steam.reason !== 'string')) return false;
  if (steam.status === 'unknown' || steam.status === 'pending') {
    if (Object.keys(steam).some(key => !['status','reason','processValidated','currentSource'].includes(key))) return false;
    if (value.game === null) return true;
  }
  const game = target(value.game);
  if (!game || !record(value.game) || value.game.processValidated !== false) return false;
  if (value.game.rulesEpoch !== undefined && !uint64(value.game.rulesEpoch) ||
      value.game.powerGeneration !== undefined && !uint64(value.game.powerGeneration)) return false;
  if (steam.status === 'unknown' || steam.status === 'pending') return true;
  return decode(value, game).steam.status === 'cached-association' &&
    (steam.processGroup === undefined || uint32(steam.appid) && uint64(steam.gameId) && !!parseGroup(steam.processGroup,game,steam.appid,steam.gameId));
}
