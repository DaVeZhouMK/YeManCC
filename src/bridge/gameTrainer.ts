import { fs, shell } from './api';
import { cleanGameTitle, normalizeGameTitle } from './gamedetect';

export interface InstalledGameTrainer {
  gameName: string;
  origin: string;
  folder: string;
  path: string;
  modified: number;
}

export interface GcmSearchResult {
  ok: boolean;
  gameName: string;
  results?: string[];
  selected?: string;
  downloaded?: boolean;
  trainerFolder?: string;
  trainerPath?: string;
  trainerGameName?: string;
  origin?: string;
  state?: 'pending' | 'searching' | 'downloading' | 'completed' | 'failed';
  elapsedSeconds?: number;
  message?: string;
  error?: string;
}

const GCM_TRAINER_ROOT = 'C:\\Users\\DaVe\\AppData\\Roaming\\GCM Trainers';
const GCM_SEARCH_SCRIPT = 'C:\\SOFT\\YeMan\\PowerControl\\YeManGcmSearch.ps1';
const GCM_RESULT_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\yeman-gcm-search-result.json';
const CHEAT_ENGINE_PATH = 'C:\\SOFT\\Cheat Engine\\Cheat Engine.exe';
const EMULATOR_PROCESS_NAMES = new Set([
  'cemu.exe', 'yuzu.exe', 'ryujinx.exe', 'citra.exe', 'citra-qt.exe',
  'desmume.exe', 'mgba.exe', 'duckstation.exe', 'duckstation-qt.exe',
  'pcsx2.exe', 'pcsx2-qt.exe', 'ppssppwindows.exe', 'ppsspp.exe', 'rpcs3.exe',
]);

function isEmulatorProcess(processPath: string): boolean {
  const name = processPath.trim().split(/[\\/]/).pop()?.toLowerCase() || '';
  return EMULATOR_PROCESS_NAMES.has(name);
}

function normalizeTrainerTitle(value: string): string {
  const cleaned = cleanGameTitle(value)
    .replace(/[《》【】\[\]()（）]/g, ' ')
    .replace(/修改器|trainer/gi, ' ');
  return normalizeGameTitle(cleaned) || cleaned.toLowerCase().replace(/[^\p{L}\p{N}]+/gu, '');
}

function titleScore(target: string, candidate: string): number {
  const a = normalizeTrainerTitle(target);
  const b = normalizeTrainerTitle(candidate);
  if (!a || !b) return 0;
  if (a === b) return 100;
  if (a.includes(b) || b.includes(a)) return 70;
  return 0;
}

function isExactGameMatch(target: string, candidate: string): boolean {
  const a = normalizeTrainerTitle(target);
  const b = normalizeTrainerTitle(candidate);
  return Boolean(a && b && (a === b || a.includes(b) || b.includes(a)));
}

function originScore(origin: string): number {
  const value = origin.toLowerCase();
  if (value === 'fling_main') return 40;
  if (value === 'fling_archive') return 35;
  if (value === 'xiaoxing') return 30;
  if (value === 'gcm') return 20;
  return 10;
}

export async function findInstalledGameTrainer(gameName: string): Promise<InstalledGameTrainer | null> {
  const dirs = await fs.readDir(GCM_TRAINER_ROOT).catch(() => [] as any[]);
  const candidates: Array<InstalledGameTrainer & { score: number }> = [];
  for (const entry of dirs) {
    if (!entry?.isDir || typeof entry.name !== 'string') continue;
    const folder = `${GCM_TRAINER_ROOT}\\${entry.name}`;
    let info: { game_name?: string; origin?: string } = {};
    try {
      info = JSON.parse(await fs.readTextFile(`${folder}\\gcm_info.json`)) as typeof info;
    } catch { /* 老版本或手工导入的修改器可能没有元数据 */ }
    const gameTitle = typeof info.game_name === 'string' ? info.game_name.trim() : '';
    // gcm_info.json is authoritative. A manually renamed folder must never
    // make a trainer for another game look like a match.
    const score = gameTitle
      ? (isExactGameMatch(gameName, gameTitle) ? titleScore(gameName, gameTitle) : 0)
      : (isExactGameMatch(gameName, entry.name) ? titleScore(gameName, entry.name) : 0);
    if (score <= 0) continue;
    const files = await fs.readDir(folder).catch(() => [] as any[]);
    const launchable = files
      .filter((f) => f?.isFile && typeof f.name === 'string' && /\.(exe|ct|cetrainer)$/i.test(f.name))
      .sort((a, b) => (/\.exe$/i.test(b.name) ? 1 : 0) - (/\.exe$/i.test(a.name) ? 1 : 0));
    if (!launchable.length) continue;
    const path = `${folder}\\${launchable[0].name}`;
    const stat = await fs.stat(path).catch(() => ({ modified: 0 }));
    candidates.push({
      gameName: gameTitle || entry.name,
      origin: typeof info.origin === 'string' ? info.origin : 'other',
      folder,
      path,
      modified: Number(stat.modified) || 0,
      score: score + originScore(typeof info.origin === 'string' ? info.origin : 'other'),
    });
  }
  candidates.sort((a, b) => b.score - a.score || b.modified - a.modified);
  return candidates[0] || null;
}

export async function openOrSearchGameTrainer(
  gameName: string,
  onProgress?: (progress: GcmSearchResult) => void,
  processPath = '',
): Promise<{
  action: 'opened' | 'searched';
  trainer?: InstalledGameTrainer;
  search?: GcmSearchResult;
}> {
  const title = cleanGameTitle(gameName).trim();
  if (!title) throw new Error('未识别到真实游戏名');
  if (isEmulatorProcess(processPath)) {
    await shell.execute(CHEAT_ENGINE_PATH, []);
    return {
      action: 'opened',
      trainer: {
        gameName: title,
        origin: 'cheat_engine',
        folder: 'C:\\SOFT\\Cheat Engine',
        path: CHEAT_ENGINE_PATH,
        modified: Date.now(),
      },
    };
  }
  const installed = await findInstalledGameTrainer(title);
  if (installed) {
    await shell.execute(installed.path, []);
    return { action: 'opened', trainer: installed };
  }
  await fs.writeTextFile(GCM_RESULT_FILE, JSON.stringify({ ok: false, gameName: title, error: 'pending' })).catch(() => {});
  await shell.hidden('powershell.exe', [
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', GCM_SEARCH_SCRIPT,
    '-GameName', title, '-ResultPath', GCM_RESULT_FILE, '-DownloadTimeoutSeconds', '120',
  ]);
  const deadline = Date.now() + 135000;
  let search: GcmSearchResult | null = null;
  let lastProgressKey = '';
  while (Date.now() < deadline) {
    if (await fs.exists(GCM_RESULT_FILE).catch(() => false)) {
      try {
        const parsed = JSON.parse(await fs.readTextFile(GCM_RESULT_FILE)) as GcmSearchResult;
        if (parsed.gameName === title) {
          const progressKey = `${parsed.state || parsed.error}:${parsed.elapsedSeconds || 0}:${parsed.message || ''}`;
          if (progressKey !== lastProgressKey) {
            onProgress?.(parsed);
            lastProgressKey = progressKey;
          }
          if (parsed.state === 'completed' || parsed.downloaded || (parsed.error && parsed.error !== 'pending')) {
            search = parsed;
            break;
          }
        }
      } catch { /* 文件仍在写入，稍后再读 */ }
    }
    await new Promise((resolve) => window.setTimeout(resolve, 250));
  }
  if (!search) {
    const recovered = await findInstalledGameTrainer(title);
    if (recovered) {
      await shell.execute(recovered.path, []);
      return { action: 'opened', trainer: recovered };
    }
    throw new Error('GCM search timeout');
  }
  if (!search.ok) {
    const recovered = await findInstalledGameTrainer(title);
    if (recovered) {
      await shell.execute(recovered.path, []);
      return { action: 'opened', trainer: recovered, search };
    }
    throw new Error(search.error || 'GCM search failed');
  }
  if (!search) throw new Error('GCM 搜索超时');
  if (!search.ok) throw new Error('GCM 搜索失败');
  if (!search) {
    const recovered = await findInstalledGameTrainer(title);
    if (recovered) {
      await shell.execute(recovered.path, []);
      return { action: 'opened', trainer: recovered };
    }
    throw new Error('GCM search timeout');
  }
  if (!search.ok) {
    const recovered = await findInstalledGameTrainer(title);
    if (recovered) {
      await shell.execute(recovered.path, []);
      return { action: 'opened', trainer: recovered, search };
    }
    throw new Error(search.error || 'GCM search failed');
  }
  // The script has already verified the folder and executable. Use its exact
  // path so the first click does not need a second directory scan.
  if (search.downloaded && search.trainerPath) {
    await shell.execute(search.trainerPath, []);
    return {
      action: 'opened',
      trainer: {
        gameName: search.trainerGameName || title,
        origin: search.origin || 'other',
        folder: search.trainerFolder || search.trainerPath.substring(0, search.trainerPath.lastIndexOf('\\')),
        path: search.trainerPath,
        modified: Date.now(),
      },
      search,
    };
  }
  return { action: 'searched', search };
}
