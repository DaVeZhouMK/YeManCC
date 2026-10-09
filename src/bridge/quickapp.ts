// quickapp.ts — 快捷应用桥层（Lossless Scaling 一键启动）
//
// 设计要点：
//  - 纯前端实现，不重编译 native 壳。
//  - 游戏识别统一走 `bridge/gamedetect`（原 quickapp 内的逻辑已迁出，RTSS 等共用）。
//  - 启动 LS：优先 C:\SOFT\Lossless.Scaling\LosslessScaling.exe；缺失时自动回退 Steam 库。

import { app, fs, shell, registry, proc } from './api';
import { parseLsProfiles, matchLsProfile, normalizeLsPath, lsExeKey, type LsProfile } from './losslessProfiles';

export const LS_PRIMARY = 'C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe';
const LS_APPID = 993090;
const MIN_WORKINGSET = 500 * 1024 * 1024; // 对齐 native SG_MIN_WS = 500MB

function joinPath(...parts: string[]): string {
  return parts.join('\\').replace(/\//g, '\\');
}
function basename(p: string): string {
  const m = p.match(/[^\\\/]+$/);
  return m ? m[0] : p;
}

// 游戏识别统一在 bridge/gamedetect，本文件保留向后兼容的别名导出。
export { detectGame as detectForegroundGame } from './gamedetect';
export type { DetectedGame as GameProc } from './gamedetect';

// ───────────────────────── LS 路径解析 ─────────────────────────

async function getSteamPath(): Promise<string> {
  for (const root of ['HKCU', 'HKLM']) {
    try {
      const v = await registry.read(root, 'Software\\Valve\\Steam', 'SteamPath');
      if (v && typeof v === 'string' && v.trim()) return v.trim();
    } catch {
      /* try next */
    }
  }
  return '';
}

async function parseLibraryFolders(steamPath: string): Promise<string[]> {
  const vdf = joinPath(steamPath, 'steamapps', 'libraryfolders.vdf');
  const roots: string[] = [steamPath];
  if (await fs.exists(vdf)) {
    try {
      const text = await fs.readTextFile(vdf);
      const re = /"path"\s+"([^"]+)"/g;
      let m: RegExpExecArray | null;
      while ((m = re.exec(text))) {
        if (m[1] && !roots.includes(m[1])) roots.push(m[1]);
      }
    } catch {
      /* 解析失败则用基础库 */
    }
  }
  return roots;
}

async function findSteamLs(steamPath: string): Promise<string> {
  const libs = await parseLibraryFolders(steamPath);
  for (const lib of libs) {
    const cand = joinPath(
      lib,
      'steamapps',
      'common',
      'Lossless Scaling',
      'LosslessScaling.exe'
    );
    if (await fs.exists(cand)) return cand;
  }
  return '';
}

export interface LsResolve {
  exe: string;
  source: 'primary' | 'steam' | 'none';
  alreadyHadProfile?: boolean; // 已有此 exe 的 Profile，跳过写入
}

export async function resolveLs(): Promise<LsResolve> {
  if (await fs.exists(LS_PRIMARY)) {
    return { exe: LS_PRIMARY, source: 'primary' };
  }
  const steamPath = await getSteamPath();
  if (steamPath) {
    const steamExe = await findSteamLs(steamPath);
    if (steamExe) return { exe: steamExe, source: 'steam' };
  }
  return { exe: '', source: 'none' };
}

// ───────────────────────── Settings.xml 写入 ─────────────────────────
// 路径固定为 %LOCALAPPDATA%\Lossless Scaling\Settings.xml（与 LS exe 位置无关）

let localAppDataCache: Promise<string> | null = null;
async function getLocalAppData(): Promise<string> {
  if (!localAppDataCache) {
    localAppDataCache = (async () => {
      const r = await shell.run('powershell', [
        '-NoProfile',
        '-Command',
        'Write-Output $env:LOCALAPPDATA',
      ]);
      const p = (r.stdout || '').trim();
      if (!p) throw new Error('无法获取 LOCALAPPDATA 路径');
      return p;
    })();
  }
  const task = localAppDataCache;
  try { return await task; }
  catch (error) { if (localAppDataCache === task) localAppDataCache = null; throw error; }
}

export async function getLsSettingsPath(): Promise<string> {
  return joinPath(await getLocalAppData(), 'Lossless Scaling', 'Settings.xml');
}

export function withLsSettingsLock<T>(action: () => Promise<T>): Promise<T> {
  const run = lsSettingsQueue.then(action);
  lsSettingsQueue = run.then(() => {}, () => {});
  return run;
}

function xmlEscape(s: string): string {
  return s
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// 用户提供的 LS 优质模板（LSFG3 / 1.5x / FIXED 等），精确缩进对齐 LS 原生 Settings.xml
// 只改 Title 与 Path，其他字段一字不动
function buildProfileXml(title: string, path: string): string {
  const t = xmlEscape(title);
  const p = xmlEscape(path);
  return `    <Profile>
      <Title>${t}</Title>
      <Path>${p}</Path>
      <AutoScale>true</AutoScale>
      <AutoScaleDelay>5</AutoScaleDelay>
      <ScalingMode>Auto</ScalingMode>
      <ScalingFitMode>AspectRatio</ScalingFitMode>
      <ScaleFactor>1.5</ScaleFactor>
      <ResizeBeforeScaling>false</ResizeBeforeScaling>
      <WindowedMode>false</WindowedMode>
      <ScalingType>SGSR</ScalingType>
      <FSRType>ORIGINAL</FSRType>
      <LS1Type>PERFORMANCE</LS1Type>
      <LSFG2Mode>X2</LSFG2Mode>
      <LSFG3Mode1>FIXED</LSFG3Mode1>
      <LSFG3Multiplier>2</LSFG3Multiplier>
      <LSFG3Target>60</LSFG3Target>
      <LSFGFlowScale>75</LSFGFlowScale>
      <LSFGSize>PERFORMANCE</LSFGSize>
      <Anime4kType>VL</Anime4kType>
      <Sharpness>10</Sharpness>
      <LS1Sharpness>0</LS1Sharpness>
      <VRS>false</VRS>
      <FrameGeneration>LSFG3</FrameGeneration>
      <ClipCursor>true</ClipCursor>
      <AdjustCursorSpeed>false</AdjustCursorSpeed>
      <HideCursor>false</HideCursor>
      <ScaleCursor>false</ScaleCursor>
      <SyncMode>DEFAULT</SyncMode>
      <MaxFrameLatency>5</MaxFrameLatency>
      <GsyncSupport>true</GsyncSupport>
      <HdrSupport>false</HdrSupport>
      <DrawFps>true</DrawFps>
      <CaptureApi>DXGI</CaptureApi>
      <QueueTarget>2</QueueTarget>
      <PreferredGpuId>0</PreferredGpuId>
      <OutputDisplayId>0</OutputDisplayId>
      <CropInput>false</CropInput>
      <CropInputLeft>0</CropInputLeft>
      <CropInputTop>0</CropInputTop>
      <CropInputRight>0</CropInputRight>
      <CropInputBottom>0</CropInputBottom>
      <MultiDisplayMode>false</MultiDisplayMode>
    </Profile>`;
}

// 确保 Settings.xml 中存在该游戏的 Profile（字符串插入，保留原始格式/命名空间/缩进）
let lsSettingsQueue: Promise<void> = Promise.resolve();
export function ensureLsProfile(gamePath: string, checkpoint: () => void = () => {}): Promise<void> {
  return withLsSettingsLock(async () => {
    checkpoint();
    // Validate first; a malformed document is never a reason to close LS.
    const file = await getLsSettingsPath(); checkpoint();
    if (!await fs.exists(file)) throw new Error('未找到 Lossless Scaling 配置文件，请先手动启动一次小黄鸭');
    const before = await fs.readTextFile(file, 2 * 1024 * 1024); checkpoint();
    parseLsProfiles(before);
    const ls = await resolveLs(); checkpoint();
    const running = ls.source !== 'none' ? await proc.findExact(ls.exe) : {found:false,pid:0}; checkpoint();
    // LS retains Settings.xml in memory (and may hold the file). Close it
    // normally before taking the fresh document, so its later save cannot
    // discard an import or make atomic replacement fail.
    try {
      if (running.found) await closeOriginalLossless(ls.exe, running.pid, checkpoint);
      await ensureLsProfileUnlocked(gamePath, 0, checkpoint);
    }
    finally {
      if (running.found && !(await proc.findExact(ls.exe)).found) await launchOriginalLossless(ls.exe, [], () => {});
      notifyLosslessScalingChanged();
    }
  });
}
async function ensureLsProfileUnlocked(gamePath: string, attempt = 0, checkpoint: () => void = () => {}): Promise<void> {
  checkpoint();
  const localAppData = await getLocalAppData();
  const settingsPath = joinPath(
    localAppData,
    'Lossless Scaling',
    'Settings.xml'
  );

  if (!(await fs.exists(settingsPath))) {
    throw new Error(
      '未找到 Lossless Scaling 配置文件（' +
        settingsPath +
        '），请先手动启动一次 Lossless Scaling。'
    );
  }

  const original = await fs.readTextFile(settingsPath, 2 * 1024 * 1024); checkpoint();
  parseLsProfiles(original);

  if (!original.includes('</Settings>') || !original.includes('</GameProfiles>'))
    throw new Error('Settings.xml 格式异常，已保留原文件和备份');
  if (typeof DOMParser !== 'undefined') {
    const document = new DOMParser().parseFromString(original, 'application/xml');
    if (document.querySelector('parsererror')) throw new Error('Settings.xml 无效，已保留原文件和备份');
  }

  let xml = original;
  const title = basename(gamePath);

  // 幂等：已有同 Path 的 Profile 先移除（从 <Profile> 到 </Profile> 整块删除）
  const escapedPath = xmlEscape(gamePath);
  const pathTag = `<Path>${escapedPath}</Path>`;
  const pathIdx = xml.indexOf(pathTag);
  if (pathIdx !== -1) {
    const profileStart = xml.lastIndexOf('    <Profile>', pathIdx);
    const profileEnd = xml.indexOf('    </Profile>', pathIdx);
    if (profileStart !== -1 && profileEnd !== -1) {
      xml =
        xml.slice(0, profileStart) +
        xml.slice(profileEnd + '    </Profile>'.length);
    }
  }

  // 在 </GameProfiles> 前插入新 Profile【必须保留 idx 之后的所有内容(含 </Settings>)】
  const idx = xml.lastIndexOf('\n  </GameProfiles>');
  if (idx === -1) {
    throw new Error('Settings.xml 格式异常：找不到 </GameProfiles>');
  }

  const profileXml = buildProfileXml(title, gamePath);
  // ⚠ 旧版用 `+ marker` 会丢掉 </GameProfiles> 之后的 </Settings>，导致 LS 报「文件损坏」。
  // 必须用 xml.slice(idx) 完整保留 marker 及其后续内容。
  xml = xml.slice(0, idx) + '\n' + profileXml + xml.slice(idx);

  // LS itself may have saved while we prepared the profile. Retry from its
  // newer document instead of intentionally overwriting an observed change.
  if (await fs.readTextFile(settingsPath) !== original) {
    if (attempt >= 2) throw new Error('Lossless Scaling 配置正在变化，请稍后重试');
    return ensureLsProfileUnlocked(gamePath, attempt + 1, checkpoint);
  }
  checkpoint();await fs.writeTextFileAtomic(settingsPath + '.ymccbak', original);checkpoint();
  if (await fs.readTextFile(settingsPath, 2 * 1024 * 1024) !== original) throw new Error('小黄鸭配置已变化，未覆盖；请重新导入');checkpoint();
  await fs.writeTextFileAtomic(settingsPath, xml);checkpoint();
  if (await fs.readTextFile(settingsPath, 2 * 1024 * 1024) !== xml) throw new Error('小黄鸭配置写入后的回读不一致，未宣称导入成功');
}

// ───────────────────────── 一键插帧（写 XML → 启动 LS） ─────────────────────────

// 检查 Settings.xml 是否已存在此游戏的 Profile（避免重复写）
export async function hasLsProfile(gamePath: string): Promise<boolean> {
  try {
    const localAppData = await getLocalAppData();
    const settingsPath = joinPath(
      localAppData,
      'Lossless Scaling',
      'Settings.xml'
    );
    if (!(await fs.exists(settingsPath))) return false;
    const xml = await fs.readTextFile(settingsPath);
    return !!matchLsProfile(parseLsProfiles(xml), gamePath);
  } catch {
    return false;
  }
}

export async function oneClickFrameGen(
  gamePath: string,
  checkpoint: () => void = () => {},
): Promise<LsResolve> {
  checkpoint();
  // 1. 已有 Profile 则跳过写入，直接最小化启动
  let already = false;
  try {
    already = await hasLsProfile(gamePath);
  } catch {
    /* 检测失败视为不存在，正常写入 */
  }
  if (!already) {
    await ensureLsProfile(gamePath, checkpoint);
  }

  // 2. 解析 LS 路径（主路径 → Steam 回退）
  const ls = await resolveLs(); checkpoint();
  if (ls.source === 'none') {
    const settingsPath = joinPath(
      await getLocalAppData(),
      'Lossless Scaling',
      'Settings.xml'
    );
    throw new Error(
      '未找到 Lossless Scaling：请确认 ' +
        LS_PRIMARY +
        ' 已存在，或在 Steam 中安装 Lossless Scaling（appid ' +
        LS_APPID +
        '）。\n插件已写入 ' +
        settingsPath +
        '，可手动启动 LS 后生效。'
    );
  }

  // 3. 最小化启动 LS（cmd /c start /min，不抢焦点不弹窗）
  await launchOriginalLossless(ls.exe, ['-path', gamePath, '-auto'], checkpoint);
  notifyLosslessScalingChanged();
  return { exe: ls.exe, source: ls.source, alreadyHadProfile: already };
}

// ───────────────────────── OptiScaler 一键安装/卸载 ─────────────────────────
// 复用 detectForegroundGame 识别到的 exe 真实路径 -> dirname 作为游戏目录，
// 调用 YeManTdpCtl.exe optiscaler 命令族（纯文件复制，不依赖 OptiScalerClient.exe）。
// 安装前自动备份被覆盖的原文件到 %APPDATA%/YeManCC/optiscaler_backups/。

const TDPCTL_EXE_OPTI = 'C:\\SOFT\\YeMan\\PowerControl\\pawnio\\YeManTdpCtl.exe';
let optiCtlExeCache: string | null | undefined;

// A complete release/test package keeps PowerControl beside YeManCC. Resolve
// that local copy first so an exported test build does not accidentally call
// the older controller from the installed product. Production still falls
// back to the canonical C:\\SOFT\\YeMan path.
async function resolveOptiCtlExe(): Promise<string> {
  if (optiCtlExeCache) return optiCtlExeCache;
  const candidates = [TDPCTL_EXE_OPTI];
  try {
    const powerControlDir = await app.powerControlDir();
    if (powerControlDir) candidates.unshift(joinPath(powerControlDir, 'pawnio', 'YeManTdpCtl.exe'));
  } catch {
    /* older shells use the canonical production path */
  }
  for (const candidate of candidates) {
    if (await fs.exists(candidate).catch(() => false)) {
      optiCtlExeCache = candidate;
      return candidate;
    }
  }
  return TDPCTL_EXE_OPTI;
}

export function dirnameOf(p: string): string {
  const i = Math.max(p.lastIndexOf('\\'), p.lastIndexOf('/'));
  return i <= 0 ? p : p.slice(0, i);
}

// 查询游戏目录是否已注入 OptiScaler（dxgi.dll 哈希匹配缓存 OptiScaler.dll）
export interface OptiStatus {
  ok: boolean;
  installed: boolean;
  reason?: string;
  msgs?: string[];
  source?: string;
  version?: string;
  backend?: OptiBackend | null;
  availableBackends?: OptiBackend[];
}

export type OptiBackend = 'fsr' | 'xess';
export type OptiAction = OptiBackend | 'client' | 'uninstall';

export interface OptiAnalysis {
  ok: boolean;
  gameDir?: string;
  api?: 'dx11' | 'dx12' | 'vulkan' | 'unknown' | string;
  engine?: 'unity' | 'unreal' | 're' | 'unknown' | string;
  features?: string[];
  evidence?: string[];
  existing?: string[];
  availableBackends?: OptiBackend[];
  recommendedBackend?: OptiBackend | null;
  reasons?: string[];
  source?: string;
  version?: string;
  missing?: string[];
  warnings?: string[];
  antiCheat?: boolean;
}

function parseOptiBackend(value: unknown): OptiBackend | null {
  return value === 'fsr' || value === 'xess' ? value : null;
}

export async function optiscalerStatus(gamePath: string): Promise<OptiStatus> {
  const gameDir = dirnameOf(gamePath);
  try {
    const r = await shell.run(await resolveOptiCtlExe(), ['optiscaler', 'status', gameDir], 20000);
    const txt = (r.stdout || '').trim();
    if (!txt) return { ok: false, installed: false, msgs: [(r.stderr || 'OptiScaler 状态读取无输出').trim()] };
    const obj = JSON.parse(txt);
    return {
      ok: r.exitCode === 0 && obj?.ok === true,
      installed: !!obj?.installed,
      reason: typeof obj?.reason === 'string' ? obj.reason : undefined,
      msgs: Array.isArray(obj?.msgs) ? obj.msgs.map(String) : undefined,
      source: typeof obj?.source === 'string' ? obj.source : undefined,
      version: typeof obj?.version === 'string' ? obj.version : undefined,
      backend: parseOptiBackend(obj?.backend),
      availableBackends: Array.isArray(obj?.available_backends)
        ? obj.available_backends.map(parseOptiBackend).filter((v): v is OptiBackend => !!v)
        : undefined,
    };
  } catch {
    return { ok: false, installed: false, msgs: ['无法启动 YeManTdpCtl.exe 读取 OptiScaler 状态'] };
  }
}

export async function optiscalerAnalyze(gamePath: string): Promise<OptiAnalysis> {
  const gameDir = dirnameOf(gamePath);
  try {
    const r = await shell.run(await resolveOptiCtlExe(), ['optiscaler', 'analyze', gameDir], 20000);
    const txt = (r.stdout || '').trim();
    if (!txt) return { ok: false, missing: [(r.stderr || 'OptiScaler 分析无输出').trim()] };
    const obj = JSON.parse(txt);
    return {
      ok: r.exitCode === 0 && obj?.ok === true,
      gameDir: typeof obj?.game_dir === 'string' ? obj.game_dir : gameDir,
      api: typeof obj?.api === 'string' ? obj.api : 'unknown',
      engine: typeof obj?.engine === 'string' ? obj.engine : 'unknown',
      features: Array.isArray(obj?.features) ? obj.features.map(String) : [],
      evidence: Array.isArray(obj?.evidence) ? obj.evidence.map(String) : [],
      existing: Array.isArray(obj?.existing) ? obj.existing.map(String) : [],
      availableBackends: Array.isArray(obj?.available_backends)
        ? obj.available_backends.map(parseOptiBackend).filter((v): v is OptiBackend => !!v)
        : [],
      recommendedBackend: parseOptiBackend(obj?.recommended_backend),
      reasons: Array.isArray(obj?.reasons) ? obj.reasons.map(String) : [],
      source: typeof obj?.source === 'string' ? obj.source : undefined,
      version: typeof obj?.version === 'string' ? obj.version : undefined,
      missing: [
        ...(Array.isArray(obj?.missing) ? obj.missing.map(String) : []),
        ...(Array.isArray(obj?.msgs) ? obj.msgs.map(String) : []),
        ...((r.exitCode !== 0 || obj?.ok !== true) && !obj?.missing?.length && !obj?.msgs?.length
          ? [(r.stderr || `OptiScaler 分析失败（退出码 ${r.exitCode}）`).trim()] : []),
      ],
      warnings: Array.isArray(obj?.warnings) ? obj.warnings.map(String) : [],
      antiCheat: obj?.anti_cheat === true,
    };
  } catch (e) {
    return { ok: false, missing: [(e as Error).message] };
  }
}

export interface OptiResult {
  ok: boolean;
  msgs?: string[];
  via?: string;
  source?: string;
  version?: string;
  written?: number;
  removed?: number;
  restored?: number;
  backend?: OptiBackend;
  analysis?: OptiAnalysis;
  warnings?: string[];
}

// uninstall=true 卸载（还原原文件），false 安装。安装时 backend 必须由界面
// 明确选择或传入 auto；后端选择会同时写入 OptiScaler.ini。
export async function oneClickOptiScaler(
  gamePath: string,
  uninstall: boolean,
  backend: OptiBackend | 'auto' = 'auto',
): Promise<OptiResult> {
  const gameDir = dirnameOf(gamePath);
  const sub = uninstall ? 'uninstall' : 'install';
  try {
    const args = ['optiscaler', sub, gameDir];
    if (!uninstall) args.push('--backend', backend);
    const r = await shell.run(await resolveOptiCtlExe(), args, 60000);
    const txt = (r.stdout || '').trim();
    if (!txt) {
      return { ok: false, msgs: (r.stderr || '无输出').split('\n').slice(0, 3) };
    }
    const obj = JSON.parse(txt);
    return {
      ok: r.exitCode === 0 && obj?.ok === true,
      msgs: Array.isArray(obj?.msgs) ? obj.msgs.map(String)
        : (r.exitCode !== 0 || obj?.ok !== true) ? [(r.stderr || `OptiScaler 操作失败（退出码 ${r.exitCode}）`).trim()] : [],
      via: obj.via,
      source: obj.source,
      version: obj.version,
      written: obj.written,
      removed: obj.removed,
      restored: obj.restored,
      backend: parseOptiBackend(obj?.backend) || undefined,
      analysis: obj?.analysis,
      warnings: Array.isArray(obj?.warnings) ? obj.warnings.map(String) : [],
    };
  } catch (e) {
    return { ok: false, msgs: [(e as Error).message] };
  }
}

// OPT 客户端是独立的打开快捷方式：不轮询、不关闭进程、不修改游戏文件。
export const OPTISCALER_CLIENT_DIR = 'C:\\SOFT\\OptiscalerClient';
export const OPTISCALER_CLIENT_EXE = 'C:\\SOFT\\OptiscalerClient\\OptiscalerClient.exe';
export const OPTISCALER_CLIENT_URL = 'https://github.com/Optiscaler-Client/Optiscaler-Client/releases';

export async function optiConsoleInstalled(): Promise<boolean> {
  return fs.exists(OPTISCALER_CLIENT_EXE).catch(() => false);
}

// GUI 程序必须用 shell.execute；重复点击仍然只发送打开请求。
export async function openOptiConsole(): Promise<void> {
  await shell.execute(OPTISCALER_CLIENT_EXE, []);
}

// Original YMCC owner for LS imports, starts and stops. The console calls this
// owner; it never writes LS settings or creates a second control path.
const losslessListeners = new Set<() => void>();
export function onLosslessScalingChanged(listener: () => void): () => void {
  losslessListeners.add(listener); return () => { losslessListeners.delete(listener); };
}
function notifyLosslessScalingChanged(): void {
  for (const listener of losslessListeners) { try { listener(); } catch {} }
}
function psQuoted(value: string): string { return "'" + value.replace(/'/g, "''") + "'"; }
function encodeLosslessScript(script: string): string {
  const bytes = new Uint8Array(script.length * 2);
  for (let i = 0; i < script.length; i++) { const code = script.charCodeAt(i); bytes[2*i] = code & 255; bytes[2*i+1] = code >> 8; }
  return btoa(Array.from(bytes, byte => String.fromCharCode(byte)).join(''));
}
async function launchOriginalLossless(executable: string, args: string[], checkpoint: () => void): Promise<void> {
  checkpoint();
  const ok = await shell.execute('cmd', ['/c', 'start', '/min', '', executable, ...args]);
  checkpoint(); if (!ok) throw new Error('YMCC 小黄鸭启动请求失败，未宣称已生效');
}
async function closeOriginalLossless(executable: string, pid: number, checkpoint: () => void): Promise<void> {
  const identity = await proc.identity(pid); checkpoint();
  if (!identity.valid || !identity.processCreated || normalizeLsPath(identity.path || '') !== normalizeLsPath(executable)) throw new Error('小黄鸭进程身份已变化，未关闭');
  // Exact PID + creation time + path. No taskkill, game termination or toggle
  // shortcut. Normal closure lets LS flush its in-memory document first.
  const script = `$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
$OutputEncoding=[Console]::OutputEncoding
$p=Get-Process -Id ${pid} -ErrorAction SilentlyContinue
if(-not $p){exit 0}
if($p.Path -ine ${psQuoted(executable)} -or $p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne ${psQuoted(identity.processCreated)}){throw '小黄鸭进程已更换，未关闭'}
if(-not $p.CloseMainWindow()){throw '小黄鸭没有可正常关闭的窗口；请从托盘正常退出后重试'}
if(-not $p.WaitForExit(3000)){throw '小黄鸭仍在运行（可能关闭到托盘）；未强杀、未写配置，请从托盘正常退出后重试'}`;
  const result = await shell.run('powershell', ['-NoProfile', '-NonInteractive', '-EncodedCommand', encodeLosslessScript(script)], 5000); checkpoint();
  if (result.exitCode !== 0) throw new Error((result.stderr || result.stdout || '小黄鸭正常退出失败').trim().slice(0, 600));
  const remaining = await proc.identity(pid); checkpoint();
  if (remaining.valid && remaining.processCreated === identity.processCreated) throw new Error('小黄鸭进程未退出，未写配置');
}
export interface LosslessGameState {
  settingsPath: string; xml: string; profile: LsProfile | null;
  enabled: boolean | null; running: boolean; reason: string;
}
// Read-only UI observation of LS's own selected profile and Scale button. It
// sends no clicks/hotkeys and is only requested by a visible consumer/action.
export async function getLosslessGameState(gamePath: string, waitForEnabled?: boolean): Promise<LosslessGameState> {
  const settingsPath = await getLsSettingsPath();
  if (!await fs.exists(settingsPath)) return {settingsPath,xml:'',profile:null,enabled:false,running:false,reason:'未找到小黄鸭 Settings.xml；请先用 YMCC 一键导入'};
  const xml = await fs.readTextFile(settingsPath, 2 * 1024 * 1024);
  const profiles = parseLsProfiles(xml), profile = matchLsProfile(profiles, gamePath);
  if (!profile) return {settingsPath,xml,profile:null,enabled:false,running:false,reason:'当前 EXE 不在小黄鸭生成的列表中'};
  const ls = await resolveLs();
  if (ls.source === 'none') return {settingsPath,xml,profile,enabled:null,running:false,reason:'未找到小黄鸭程序'};
  const found = await proc.findExact(ls.exe);
  if (!found.found) return {settingsPath,xml,profile,enabled:false,running:false,reason:''};
  const identity = await proc.identity(found.pid);
  if (!identity.valid || !identity.processCreated || normalizeLsPath(identity.path || '') !== normalizeLsPath(ls.exe)) return {settingsPath,xml,profile,enabled:null,running:true,reason:'小黄鸭进程身份未确认'};
  const script = `$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
$OutputEncoding=[Console]::OutputEncoding
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
$p=Get-Process -Id ${found.pid}
if($p.Path -ine ${psQuoted(ls.exe)} -or $p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne ${psQuoted(identity.processCreated)}){throw '小黄鸭身份已变化'}
if($p.MainWindowHandle -eq 0){throw '小黄鸭窗口状态不可读'}
${waitForEnabled !== undefined ? '$observed=$null;for($attempt=0;$attempt -lt 32;$attempt++){' : ''}
$r=[System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
$button=$r.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,'ScaleButton')))
$list=$r.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,'ProfileList')))
if(-not $button -or -not $list){throw '小黄鸭自身控件状态不可读'}
$selected=$list.GetCurrentPattern([System.Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()
if($selected.Count -ne 1){throw '小黄鸭当前配置不明确'}
$text=$selected[0].FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Text)))
if($text.Count -ne 1){throw '小黄鸭当前配置标题不明确'}
$observed=[pscustomobject]@{title=$text[0].Current.Name;button=$button.Current.Name.Trim()}
${waitForEnabled !== undefined ? `if($observed.title -eq ${psQuoted(profile.title)} -and $observed.button -in @(${waitForEnabled ? "'Unscale','Stop scaling','停止缩放','停止縮放','取消缩放','取消縮放'" : "'Scale','缩放','開始縮放','开始缩放'"})){break}
Start-Sleep -Milliseconds 250
$p.Refresh();if($p.HasExited -or $p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne ${psQuoted(identity.processCreated)}){throw '小黄鸭进程已变化'}
}` : ''}
$observed|ConvertTo-Json -Compress`;
  try {
    const result = await shell.run('powershell', ['-NoProfile', '-NonInteractive', '-EncodedCommand', encodeLosslessScript(script)], waitForEnabled !== undefined ? 10000 : 3000);
    if (result.exitCode !== 0) throw new Error('小黄鸭状态回读失败');
    const observed = JSON.parse(result.stdout.trim());
    const selected = profiles.filter(item => item.title === observed.title);
    if (selected.length !== 1) throw new Error('小黄鸭当前配置不明确');
    const on = ['Unscale', 'Stop scaling', '停止缩放', '停止縮放', '取消缩放', '取消縮放'].includes(observed.button);
    const off = ['Scale', '缩放', '開始縮放', '开始缩放'].includes(observed.button);
    if (!on && !off) throw new Error('小黄鸭正在切换，未把未知状态当作关闭');
    const enabled = on && normalizeLsPath(selected[0].path) === normalizeLsPath(profile.path);
    return {settingsPath,xml,profile,enabled,running:true,reason:on&&!enabled?'小黄鸭正在处理另一 EXE':''};
  } catch {
    return {settingsPath,xml,profile,enabled:null,running:true,reason:'小黄鸭实际状态暂不可读；未把配置的 AutoScale 当作已开启'};
  }
}
export async function stopLosslessScaling(gamePath: string, checkpoint: () => void = () => {}): Promise<void> {
  const state = await getLosslessGameState(gamePath); checkpoint();
  if (!state.profile) throw new Error('当前 EXE 不在小黄鸭原列表中，未关闭其它游戏会话');
  if (!state.running) return;
  if (state.enabled !== true) throw new Error(state.reason || '小黄鸭当前未确认属于此 EXE，未关闭其它会话');
  const ls = await resolveLs(); checkpoint(); const found = await proc.findExact(ls.exe); checkpoint();
  if (found.found) await closeOriginalLossless(ls.exe, found.pid, checkpoint);
  notifyLosslessScalingChanged();
}
export async function setLosslessScalingEnabled(gamePath: string, enabled: boolean, checkpoint: () => void = () => {}): Promise<{confirmed:boolean;notice:string}> {
  if (enabled) await oneClickFrameGen(gamePath, checkpoint);
  else await stopLosslessScaling(gamePath, checkpoint);
  if (enabled) {
    const ls=await resolveLs(); checkpoint();
    const waitScript=`$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
$OutputEncoding=[Console]::OutputEncoding
for($attempt=0;$attempt -lt 20;$attempt++){
 $ready=@(Get-Process -Name LosslessScaling -ErrorAction SilentlyContinue|Where-Object {$_.Path -ieq ${psQuoted(ls.exe)} -and $_.MainWindowHandle -ne 0})
 if($ready.Count -eq 1){exit 0}
 Start-Sleep -Milliseconds 250
}
throw '小黄鸭窗口尚未就绪，原开启动作不重发'`;
    // Explicit action only: finite startup/Scale-delay observation, never
    // a resident poll and never another -auto/toggle control request.
    await shell.run('powershell',['-NoProfile','-NonInteractive','-EncodedCommand',encodeLosslessScript(waitScript)],6500); checkpoint();
  }
  const state = await getLosslessGameState(gamePath, enabled); checkpoint();
  const confirmed = state.enabled === enabled;
  notifyLosslessScalingChanged();
  return {confirmed,notice:confirmed ? `YMCC 小黄鸭：${lsExeKey(gamePath)} ${enabled?'已开启':'已关闭'}（小黄鸭控件回读）`
    : `已执行 YMCC 原小黄鸭${enabled?'开启':'关闭'}链路；实际状态待确认，不自动重发`};
}
