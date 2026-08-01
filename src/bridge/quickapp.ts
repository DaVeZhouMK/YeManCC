// quickapp.ts — 快捷应用桥层（Lossless Scaling 一键启动）
//
// 设计要点：
//  - 纯前端实现，不重编译 native 壳。
//  - 游戏识别沿用「睡眠守护」逻辑：枚举进程 → 跳过黑名单(exclude.txt + 系统内置)
//    → 只留工作集 > 500MB → 取「剩余最大工作集」作为当前游戏。无需最小化窗口，
//    因此不会出现黑屏闪烁。
//  - 启动 LS：优先 C:\SOFT\Lossless.Scaling\LosslessScaling.exe；缺失时自动回退 Steam 库。

import { fs, shell, registry } from './api';

export const LS_PRIMARY = 'C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe';
const LS_APPID = 993090;
const EXCLUDE_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\Sleep\\exclude.txt';
const MIN_WORKINGSET = 500 * 1024 * 1024; // 对齐 native SG_MIN_WS = 500MB

export interface GameProc {
  pid: number;
  name: string;
  title: string;
  path: string;
}

function joinPath(...parts: string[]): string {
  return parts.join('\\').replace(/\//g, '\\');
}
function basename(p: string): string {
  const m = p.match(/[^\\\/]+$/);
  return m ? m[0] : p;
}
function normName(s: string): string {
  return s.replace(/\.exe$/i, '').toLowerCase();
}

// 系统内置黑名单（与睡眠守护一致，代码硬编码）
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
function loadExcludeSet(): Promise<Set<string>> {
  if (!excludeCache) {
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
  }
  return excludeCache;
}

// ───────────────────────── 游戏识别 ─────────────────────────

// 识别当前游戏：完全照搬 native 睡眠守护 sgEnumProcs / sgSuspendTarget 逻辑——
// 枚举进程 → 跳过 SG_BLACKLIST + exclude.txt → 仅保留 WS≥500MB → 取最大工作集者。
// ★ 不用前台窗口优先（会把 UU 远程 / 桌面工具误抓为游戏）。
export async function detectForegroundGame(): Promise<GameProc | null> {
  const exclude = await loadExcludeSet();
  const ps = `$procs = Get-Process | Where-Object { $_.WorkingSet64 -gt ${MIN_WORKINGSET} }
foreach ($pr in $procs) {
  try { $p = $pr.Path } catch { $p = '' }
  if ($p -and $p -like '*.exe') {
    Write-Output (($pr.Id.ToString()) + '|' + ($pr.ProcessName) + '|' + ($pr.MainWindowTitle) + '|' + $p + '|' + ($pr.WorkingSet64))
  }
}`;
  const tmp = 'C:\\SOFT\\YeMan\\PowerControl\\_lsdetect.ps1';
  await fs.writeTextFile(tmp, ps);
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-ExecutionPolicy',
      'Bypass',
      '-File',
      tmp,
    ]);
    const lines = (r.stdout || '')
      .split('\n')
      .map((s) => s.trim())
      .filter(Boolean);
    let best: GameProc | null = null;
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
        };
      }
    }
    return best;
  } finally {
    await fs.remove(tmp).catch(() => {});
  }
}

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
  return localAppDataCache;
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
export async function ensureLsProfile(gamePath: string): Promise<void> {
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

  const original = await fs.readTextFile(settingsPath);

  // 安全网：写之前先复制一份原文件 .ymccbak（万一再次出错可手动恢复）
  try {
    await fs.writeTextFile(settingsPath + '.ymccbak', original);
  } catch {
    /* 备份失败不阻塞主流程 */
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

  await fs.writeTextFile(settingsPath, xml);
}

// ───────────────────────── 一键插帧（写 XML → 启动 LS） ─────────────────────────

// 检查 Settings.xml 是否已存在此游戏的 Profile（避免重复写）
async function hasLsProfile(gamePath: string): Promise<boolean> {
  try {
    const localAppData = await getLocalAppData();
    const settingsPath = joinPath(
      localAppData,
      'Lossless Scaling',
      'Settings.xml'
    );
    if (!(await fs.exists(settingsPath))) return false;
    const xml = await fs.readTextFile(settingsPath);
    return xml.includes(`<Path>${xmlEscape(gamePath)}</Path>`);
  } catch {
    return false;
  }
}

export async function oneClickFrameGen(
  gamePath: string
): Promise<LsResolve> {
  // 1. 已有 Profile 则跳过写入，直接最小化启动
  let already = false;
  try {
    already = await hasLsProfile(gamePath);
  } catch {
    /* 检测失败视为不存在，正常写入 */
  }
  if (!already) {
    await ensureLsProfile(gamePath);
  }

  // 2. 解析 LS 路径（主路径 → Steam 回退）
  const ls = await resolveLs();
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
  await shell.execute('cmd', [
    '/c',
    'start',
    '/min',
    '',
    ls.exe,
    '-path',
    gamePath,
    '-auto',
  ]);
  return { exe: ls.exe, source: ls.source, alreadyHadProfile: already };
}

// ───────────────────────── OptiScaler 一键 FSR4.1 安装/卸载 ─────────────────────────
// 复用 detectForegroundGame 识别到的 exe 真实路径 -> dirname 作为游戏目录，
// 调用 YeManTdpCtl.exe optiscaler 命令族（纯文件复制，不依赖 OptiScalerClient.exe）。
// 安装前自动备份被覆盖的原文件到 %APPDATA%/YeManCC/optiscaler_backups/。

const TDPCTL_EXE_OPTI = 'C:\\SOFT\\YeMan\\PowerControl\\pawnio\\YeManTdpCtl.exe';

export function dirnameOf(p: string): string {
  const i = Math.max(p.lastIndexOf('\\'), p.lastIndexOf('/'));
  return i <= 0 ? p : p.slice(0, i);
}

// 查询游戏目录是否已注入 OptiScaler（dxgi.dll 哈希匹配缓存 OptiScaler.dll）
export async function optiscalerStatus(gamePath: string): Promise<boolean> {
  const gameDir = dirnameOf(gamePath);
  try {
    const r = await shell.run(TDPCTL_EXE_OPTI, ['optiscaler', 'status', gameDir], 20000);
    const txt = (r.stdout || '').trim();
    if (!txt) return false;
    const obj = JSON.parse(txt);
    return !!(obj && obj.installed);
  } catch {
    return false;
  }
}

export interface OptiResult {
  ok: boolean;
  msgs?: string[];
  via?: string;
  written?: number;
  removed?: number;
  restored?: number;
}

// uninstall=true 卸载（还原原文件），false 安装。结果由调用方写入页面消息栏。
export async function oneClickOptiScaler(
  gamePath: string,
  uninstall: boolean
): Promise<OptiResult> {
  const gameDir = dirnameOf(gamePath);
  const sub = uninstall ? 'uninstall' : 'install';
  try {
    const r = await shell.run(TDPCTL_EXE_OPTI, ['optiscaler', sub, gameDir], 60000);
    const txt = (r.stdout || '').trim();
    if (!txt) {
      return { ok: false, msgs: (r.stderr || '无输出').split('\n').slice(0, 3) };
    }
    const obj = JSON.parse(txt);
    return {
      ok: !!(obj && obj.ok),
      msgs: obj.msgs,
      via: obj.via,
      written: obj.written,
      removed: obj.removed,
      restored: obj.restored,
    };
  } catch (e) {
    return { ok: false, msgs: [(e as Error).message] };
  }
}
