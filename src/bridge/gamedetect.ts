// gamedetect.ts — 统一游戏识别（所有页面共用：快捷应用、RTSS 启停等）
//
// 设计：复用原生睡眠守护同款识别逻辑——
//   枚举进程 → 跳过 SG_BLACKLIST（系统内置）+ exclude.txt（用户自定义） → 仅保留 WS ≥ 500MB
//   → 取工作集最大者为「当前游戏」。
// ★ 不用前台窗口优先（会把 UU 远程 / 桌面工具误抓为游戏）。
//
// 缓存：检测结果缓存 5 秒，避免 RTSS 轮询场景下每次都跑 PowerShell（~1s 开销）。
// 所有调用方（快捷应用暂停/关闭、RTSS 启停/布局切换/复位等）共享同一缓存。

import { fs, shell } from './api';
import { registerScheduledTask } from '@/scheduler';

export interface DetectedGame {
  pid: number;
  name: string;
  title: string;
  path: string;
  ts: number; // 检测时间戳
}

// Process titles often include renderer and copyright noise. Keep this public
// so every game-related page displays the same real title.
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

const EXCLUDE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\exclude.txt';
const MIN_WORKINGSET = 500 * 1024 * 1024; // 对齐 native SG_MIN_WS = 500MB
const PS_SCRIPT = 'C:\\SOFT\\YeMan\\PowerControl\\_gamedetect.ps1';

// 系统内置黑名单（对齐 native SG_BLACKLIST + 额外常见非游戏进程）
const SYSTEM_BLACKLIST = new Set([
  'system', 'idle', 'csrss', 'winlogon', 'lsass', 'services', 'smss',
  'dwm', 'explorer', 'shellhost', 'searchui', 'searchhost', 'runtimebroker',
  'sihost', 'taskhostw', 'fontdrvhost', 'conhost', 'rundll32',
  'msedgewebview2', 'applicationframehost', 'startmenuexperiencehost',
  'peopleexperiencehost', 'systemsettings', 'lockapp', 'audiodg',
  'svchost', 'nvcontainer', 'nvdisplaycontainer', 'nvdisplay',
  'rtkauduservice64',
  'yemancc', 'yemantdpctl', 'workbuddy',
  // 远程/串流工具（UU 远程、向日葵、TeamViewer、AnyDesk 等）
  'uuremote', 'uuremotefe', 'uur', 'neteaseuu', 'sunloginclient',
  'teamviewer', 'anydesk', 'todesk',
]);

let excludeCache: Promise<Set<string>> | null = null;
let excludeTs = 0;
const EXCLUDE_TTL_MS = 60_000; // 60s 后自动重读排除名单（用户改 exclude.txt 无需重启即生效）
function loadExcludeSet(): Promise<Set<string>> {
  const now = Date.now();
  if (!excludeCache || now - excludeTs > EXCLUDE_TTL_MS) {
    excludeCache = (async () => {
      const set = new Set<string>();
      try {
        const text = await fs.readTextFile(EXCLUDE_FILE);
        for (const raw of text.split(/\r?\n/)) {
          const line = raw.trim();
          if (!line || line.startsWith('#')) continue;
          set.add(normName(line));
        }
      } catch {
        /* 文件缺失则用系统内置黑名单 */
      }
      return set;
    })();
    excludeTs = now;
  }
  return excludeCache;
}
// 强制立即重读排除名单（无需等 60s TTL —— 2026-08-05 修复）
export function invalidateExcludeCache(): void {
  excludeCache = null;
  excludeTs = 0;
}

function normName(s: string): string {
  return s.replace(/\.exe$/i, '').toLowerCase();
}

// 缓存：避免轮询场景下反复跑 PowerShell（每次 ~1s）
let cached: { ts: number; game: DetectedGame | null } | null = null;
const CACHE_MS = 5000;

const DETECT_PS = `$procs = Get-Process | Where-Object { $_.WorkingSet64 -gt ${MIN_WORKINGSET} }
foreach ($pr in $procs) {
  try { $p = $pr.Path } catch { $p = '' }
  if ($p -and $p -like '*.exe') {
    Write-Output (($pr.Id.ToString()) + '|' + ($pr.ProcessName) + '|' + ($pr.MainWindowTitle) + '|' + $p + '|' + ($pr.WorkingSet64))
  }
}`;

// 识别脚本只在首次检测时写入并保留，所有页面共用同一脚本与识别入口。
// 旧逻辑曾设置 psScriptWritten=true 后又在 finally 删除脚本，导致首次识别成功、
// 后续轮询找不到脚本并广播 null，表现为游戏 exe 过一会从性能调度页消失。
let psScriptWritten = false;
async function runDetection(): Promise<DetectedGame | null> {
  const exclude = await loadExcludeSet();
  if (!psScriptWritten) {
    try {
      await fs.writeTextFile(PS_SCRIPT, DETECT_PS);
      psScriptWritten = true;
    } catch { /* 写失败则继续尝试运行，由 shell.run 兜底报错 */ }
  }
  try {
    const r = await shell.run('powershell', [
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', PS_SCRIPT,
    ]);
    const lines = (r.stdout || '')
      .split('\n')
      .map((s) => s.trim())
      .filter(Boolean);
    let best: DetectedGame | null = null;
    let bestWs = -1;
    for (const line of lines) {
      const parts = line.split('|');
      const name = parts[1] || '';
      const nm = normName(name);
      if (SYSTEM_BLACKLIST.has(nm) || exclude.has(nm)) continue;
      const ws = Number(parts[4] || '0') || 0;
      if (ws > bestWs) {
        bestWs = ws;
        best = {
          pid: Number(parts[0]) || 0,
          name,
          title: parts[2] || '',
          path: parts[3] || '',
          ts: Date.now(),
        };
      }
    }
    return best;
  } catch {
    return null;
  }
}

/**
 * 识别当前游戏（统一入口，所有页面共用）。
 * @param force 跳过缓存强制重跑（轮询等待游戏关闭时使用）
 */
export async function detectGame(force = false): Promise<DetectedGame | null> {
  const now = Date.now();
  if (!force && cached && now - cached.ts < CACHE_MS) return cached.game;
  const game = await runDetection();
  cached = { ts: now, game };
  return game;
}

/** 快速判断是否有游戏在跑 */
export async function isGameRunning(): Promise<boolean> {
  return (await detectGame()) !== null;
}

/** 清除缓存（外部知道游戏状态变化时调用，如用户主动关闭游戏后） */
export function clearGameCache(): void {
  cached = null;
}

// ─────────────────────────────────────────────────────────────
// 全局游戏状态（统一识别 + 统一状态广播）
//
// 背景：识别入口已统一到本模块，但各页面仍各自 detectGame()、
// 各自维护本地状态 → 退出游戏后「快捷应用」等页面不会联动更新。
// 这里补上「订阅 + 轮询」：有订阅者时每 5 秒（POLL_MS，加快 50% 后为 2.5 秒）强制重跑一次，
// 状态变化（游戏启动 / 退出 / 切换）时回调所有订阅者；
// 全部退订后轮询自动停止，避免后台空转跑 PowerShell。
// ─────────────────────────────────────────────────────────────

export type GameStatusListener = (game: DetectedGame | null) => void;

const listeners = new Set<GameStatusListener>();
let currentGame: DetectedGame | null = null;
let stopPollSchedule: (() => void) | null = null;
let pollBusy = false;
// 游戏 exe 识别轮询间隔：加快 50% → 2.5s（原 5s）。
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

/**
 * 强制重跑检测（绕开 5 秒缓存）并广播状态变化。
 * 轮询与手动刷新共用同一入口（带防重入），避免并发跑 PowerShell。
 */
export async function refreshGameStatus(): Promise<DetectedGame | null> {
  if (pollBusy) return currentGame;
  pollBusy = true;
  try {
    clearGameCache();
    const g = await detectGame(true);
    emitGame(g);
    return g;
  } catch {
    return currentGame;
  } finally {
    pollBusy = false;
  }
}

/**
 * 订阅全局游戏状态。首个订阅者会立即强制检测一次并注册 3 秒统一调度任务；
 * 全部退订后停止调度。返回退订函数（幂等可重复调用）。
 */
export function subscribeGameStatus(cb: GameStatusListener): () => void {
  listeners.add(cb);
  cb(currentGame); // 立即同步当前已知状态（可能为 null）
  if (listeners.size === 1) {
    stopPollSchedule = registerScheduledTask(
      'game-status',
      POLL_MS,
      refreshGameStatus,
      { runImmediately: true },
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
