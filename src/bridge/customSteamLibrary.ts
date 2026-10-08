import { app, fs, shell, windowApi } from '@/bridge/api';
import { invoke } from '@/bridge/ipc';

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

let sessionActive = false;
let sessionWatchTimer: ReturnType<typeof setInterval> | null = null;
let sessionWatchDeadline = 0;
let sessionWatchObservedPresent = false;

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
async function resolveExecutable(): Promise<string> {
  const exeDir = await app.exeDir();
  const candidates = [
    // Normal installed layout: YeManCC.exe and its child package share a root.
    joinWindowsPath(exeDir, 'CustomSteamLibrary\\CustomSteamLibrary.exe'),
    // User-frozen canonical path for a standard installation.
    `${CUSTOM_STEAM_LIBRARY_ROOT}\\CustomSteamLibrary.exe`,
  ];
  for (const candidate of candidates) {
    if (await fs.exists(candidate).catch(() => false)) return candidate;
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
  if (sessionWatchTimer) clearInterval(sessionWatchTimer);
  sessionWatchTimer = null;
  sessionWatchDeadline = 0;
  sessionWatchObservedPresent = false;
}

async function stopNativeIntegrationSession(): Promise<void> {
  await invoke<boolean>('customSteamLibrary.setIntegrationSession', { enabled: false }).catch(() => false);
}

function beginSessionWatch(): void {
  clearSessionWatch();
  sessionWatchDeadline = Date.now() + 10000;
  const poll = async () => {
    if (!sessionActive) return;
    try {
      const status = await invoke<CustomSteamLibraryStatus>('customSteamLibrary.status');
      if (status.present) {
        sessionWatchObservedPresent = true;
        if (status.compatible === false || status.inputOwner !== 'parent') {
          sessionActive = false;
          requestYeManWindowAction('show');
          await stopNativeIntegrationSession();
          clearSessionWatch();
          window.dispatchEvent(new CustomEvent('customSteamLibrary:conflict', {
            detail: { inputOwner: status.inputOwner || 'unknown', pid: status.pid },
          }));
          return;
        }
        return;
      }
      // The native process can need several turns to create WebView2. Keep
      // the parent fully suppressed during this window; never pass the input
      // back merely because the child window is not painted yet.
      if (Date.now() < sessionWatchDeadline && !sessionWatchObservedPresent) return;
      sessionActive = false;
      requestYeManWindowAction('show');
      await stopNativeIntegrationSession();
      clearSessionWatch();
      window.dispatchEvent(new CustomEvent('customSteamLibrary:closed'));
    } catch {
      if (Date.now() >= sessionWatchDeadline) {
        sessionActive = false;
        requestYeManWindowAction('show');
        await stopNativeIntegrationSession();
        clearSessionWatch();
        window.dispatchEvent(new CustomEvent('customSteamLibrary:closed'));
      }
    }
  };
  sessionWatchTimer = setInterval(() => { void poll(); }, 250);
  void poll();
}

export async function launchCustomSteamLibrary(): Promise<CustomSteamLibraryLaunchResult> {
  if (!CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED) {
    return { ok: false, executable: '', reason: 'Custom Steam Library integration is hidden' };
  }
  try {
    // The native YeManCC gamepad engine owns input arbitration. This call only
    // arms that native owner before the child process is created.
    await setCustomSteamLibrarySessionIntent(true);
    const executable = await resolveExecutable();
    const parentPid = await app.pid();
    const launched = await shell.hidden(executable, [
      '--integration=YeManCC',
      `--protocol=${CUSTOM_STEAM_LIBRARY_PROTOCOL_VERSION}`,
      `--parent-pid=${parentPid}`,
      '--input-owner=parent',
    ]);
    sessionActive = launched.ok === true;
    if (sessionActive) {
      beginSessionWatch();
      requestYeManWindowAction('minimize');
    }
    return { ok: sessionActive, executable, pid: launched.pid };
  } catch (error) {
    await stopCustomSteamLibrarySession();
    throw error;
  }
}

async function setCustomSteamLibrarySessionIntent(
  enabled: boolean,
): Promise<void> {
  if (!CUSTOM_STEAM_LIBRARY_INTEGRATION_ENABLED && enabled) return;
  await invoke<boolean>('customSteamLibrary.setIntegrationSession', { enabled });
}

export async function stopCustomSteamLibrarySession(): Promise<void> {
  clearSessionWatch();
  sessionActive = false;
  await stopNativeIntegrationSession();
}
