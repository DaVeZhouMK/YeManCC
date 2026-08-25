import { app, fs, shell } from '@/bridge/api';
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
export const CUSTOM_STEAM_LIBRARY_ROOT = 'C:\\SOFT\\YeMan\\CustomSteamLibrary';
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

function trimWindowsPath(value: string): string {
  return value.replace(/[\\/]+$/, '');
}

function joinWindowsPath(root: string, child: string): string {
  return `${trimWindowsPath(root)}\\${child}`;
}

function parentWindowsPath(path: string): string {
  const normalized = trimWindowsPath(path);
  const separator = Math.max(normalized.lastIndexOf('\\'), normalized.lastIndexOf('/'));
  return separator > 0 ? normalized.slice(0, separator) : normalized;
}

async function resolveExecutable(): Promise<string> {
  const exeDir = await app.exeDir();
  const candidates = [
    // Normal installed layout: C:\\SOFT\\YeMan\\YeManCC\\YeManCC.exe
    joinWindowsPath(parentWindowsPath(exeDir), 'CustomSteamLibrary\\CustomSteamLibrary.exe'),
    // User-frozen default path for a portable deployment.
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

function summaryFromPlan(plan: any): CustomSteamLibrarySummary {
  const summary = plan?.summary || {};
  const items = Array.isArray(plan?.items) ? plan.items : [];
  const total = safeCount(summary.scannedGames ?? items.length);
  const waiting = safeCount(summary.readyToAdd ?? items.filter((item: any) => item?.status === 'ready-to-add').length);
  const joined = safeCount(summary.alreadyInSteam ?? items.filter((item: any) => item?.status === 'already-in-steam').length);
  const excluded = safeCount(summary.nonGames ?? items.filter((item: any) => item?.status === 'non-game').length);
  return {
    waiting,
    joined,
    needs: Math.max(0, total - waiting - joined - excluded),
    excluded,
  };
}

/** Read only the latest local cache for the first-level summary bubble. */
export async function readCustomSteamLibrarySummary(): Promise<CustomSteamLibrarySummary | null> {
  const roots = [CUSTOM_STEAM_LIBRARY_DATA_ROOT, `${CUSTOM_STEAM_LIBRARY_ROOT}\\data`]
    .filter((root, index, all) => all.indexOf(root) === index);
  for (const root of roots) {
    try {
      const raw = await fs.readTextFile(`${root}\\state\\steam-add-plan.json`, 2 << 20);
      return summaryFromPlan(JSON.parse(raw));
    } catch {
      // Try the fallback data root and then the scan cache below.
    }
  }
  for (const root of roots) {
    try {
      const raw = await fs.readTextFile(`${root}\\state\\library-scan.json`, 2 << 20);
      const scan = JSON.parse(raw);
      const summary = scan?.summary || {};
      const total = safeCount(summary.games ?? scan?.games?.length);
      const excluded = safeCount(summary.nonGames);
      const waiting = safeCount(summary.ready);
      return {
        waiting,
        joined: 0,
        needs: Math.max(0, total - waiting - excluded),
        excluded,
      };
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
        if (status.compatible === false || status.inputOwner !== 'parent') {
          sessionActive = false;
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
      if (Date.now() < sessionWatchDeadline) return;
      sessionActive = false;
      await stopNativeIntegrationSession();
      clearSessionWatch();
      window.dispatchEvent(new CustomEvent('customSteamLibrary:closed'));
    } catch {
      if (Date.now() >= sessionWatchDeadline) {
        sessionActive = false;
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
    if (sessionActive) beginSessionWatch();
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
