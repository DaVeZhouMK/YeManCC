// quickapp.ts — 快捷应用桥层（Lossless Scaling 一键启动）
//
// 设计要点：
//  - 复用原配置业务，原生入口仅负责精确实例复用与不激活启动。
//  - 游戏识别统一走 `bridge/gamedetect`（原 quickapp 内的逻辑已迁出，RTSS 等共用）。
//  - 启动 LS：优先 C:\SOFT\Lossless.Scaling\LosslessScaling.exe；缺失时自动回退 Steam 库。

import { app, fs, shell, registry, proc, losslessRuntime, type LosslessActivation } from './api';
import { syncOptiClientGame, flushOptiClientGames } from './optiscalerClientSync';
import { parseLsProfiles, matchLsProfile, normalizeLsPath, lsExeKey, type LsProfile } from './losslessProfiles';
import {readLosslessRuntime, invalidateLosslessRuntime, losslessRuntimeRevision, subscribeLosslessRuntime} from './losslessRuntimeState';

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
  source: 'primary' | 'steam' | 'runtime' | 'none';
  activation?: LosslessActivation;
  alreadyHadProfile?: boolean; // 已有此 exe 的 Profile，跳过写入
}

export async function resolveLs(): Promise<LsResolve> {
  const runtime=await readLosslessRuntime();
  if(!runtime.known)throw new Error('小黄鸭运行实例未确认：'+runtime.reason);
  if(runtime.running)return {exe:runtime.path,source:'runtime'};
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
      if (running.found && !(await proc.findExact(ls.exe)).found) await launchOriginalLossless(ls.exe, () => {});
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
  const existing = matchLsProfile(parseLsProfiles(original), gamePath);
  if (existing) {
    // Re-enable only this game's setting. Preserve its custom scaling options
    // and the rest of the document when the switch is turned on again.
    const block = existing.block.replace(/<AutoScale>\s*(?:true|false)\s*<\/AutoScale>/,
      '<AutoScale>true</AutoScale>').replace(/<Path>[\s\S]*?<\/Path>/,
      `<Path>${xmlEscape(gamePath)}</Path>`);
    xml = original.slice(0, existing.start) + block + original.slice(existing.end);
  } else {
    const idx = xml.lastIndexOf('\n  </GameProfiles>');
    if (idx === -1) throw new Error('Settings.xml 格式异常：找不到 </GameProfiles>');
    xml = xml.slice(0, idx) + '\n' + buildProfileXml(title, gamePath) + xml.slice(idx);
  }
  if (xml === original) return;
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
  gameTarget?: LosslessReturnTarget,
): Promise<LsResolve> {
  const deadline=Date.now()+40000;
  const check=()=>{checkpoint();if(Date.now()>=deadline)throw new Error('小黄鸭自愈总预算已到，未追加重试');};
  let result=await oneClickFrameGenAttempt(gamePath,check,gameTarget,0);check();
  // Exactly one recovery retry, only after exit of the failed exact instance.
  if(result.activation?.ok!==true&&result.activation?.scalingTimedOut===true&&result.activation.failureResetClosed){
    result=await oneClickFrameGenAttempt(gamePath,check,gameTarget,1);check();
    if(result.activation)result.activation.retryCount=1;
  }
  return result;
}
async function oneClickFrameGenAttempt(
  gamePath:string,checkpoint:()=>void,gameTarget:LosslessReturnTarget|undefined,retryAttempt:number,
):Promise<LsResolve> {
  checkpoint();
  // 1. 已有 Profile 则跳过写入，直接最小化启动
  let already = false;
  try {
    const file = await getLsSettingsPath();
    already = await fs.exists(file) && matchLsProfile(parseLsProfiles(await fs.readTextFile(file, 2 * 1024 * 1024)), gamePath)?.enabled === true;
    checkpoint();
  } catch {
    /* 检测失败视为不存在，正常写入 */
  }
  if (!already) {
    const runtime=await readLosslessRuntime();checkpoint();
    if(!runtime.known)throw new Error('小黄鸭运行实例未确认，未写配置');
    if(runtime.running){
      const off=['Scale','缩放','開始縮放','开始缩放'].includes(runtime.button);
      // Config writes need LS to exit. Never close a foreign active/countdown
      // or an unowned unreadable session just to create this game's profile.
      const owned=losslessSession?.gamePath===normalizeLsPath(gamePath)&&losslessSession.pid===runtime.pid&&
        losslessSession.processCreated===runtime.processCreated&&losslessSession.exe===normalizeLsPath(runtime.path);
      if(!runtime.readable&&!owned)throw new Error('小黄鸭状态不可读，未关闭其它游戏或写配置');
      if(runtime.readable&&!off){
        const current=await getLosslessGameState(gamePath);checkpoint();
        if(current.enabled!==true)throw new Error('小黄鸭正在处理另一 EXE 或倒计时未归属，未关闭其它游戏');
      }
    }
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

  // 3. Reuse or quietly start once. An unknown runtime never repeats launch.
  const launchReceipt=await launchOriginalLossless(ls.exe, checkpoint);
  let found = await proc.findExact(ls.exe); checkpoint();
  if (!found.found) {
    const waitScript = `$ErrorActionPreference='Stop'
for($attempt=0;$attempt -lt 20;$attempt++){
 $ready=@(Get-Process -Name 'LosslessScaling' -ErrorAction SilentlyContinue|Where-Object{try{$_.Path -ieq ${psQuoted(ls.exe)}}catch{$false}})
 if($ready.Count -eq 1){exit 0}
 Start-Sleep -Milliseconds 100
}
throw '小黄鸭进程尚未就绪，原开启动作不重发'`;
    const ready = await shell.run('powershell', ['-NoProfile','-NonInteractive','-EncodedCommand',encodeLosslessScript(waitScript)], 3500); checkpoint();
    if (ready.exitCode !== 0) throw new Error('小黄鸭启动结果未确认，不自动重发');
    found = await proc.findExact(ls.exe); checkpoint();
  }
  let activation:LosslessActivation|undefined;
  if (found.found) {
    const identity = await proc.identity(found.pid); checkpoint();
    if (identity.valid && identity.processCreated && normalizeLsPath(identity.path || '') === normalizeLsPath(ls.exe)) {
      const session={gamePath:normalizeLsPath(gamePath),exe:normalizeLsPath(ls.exe),pid:found.pid,processCreated:identity.processCreated};
      const createdHere=launchReceipt.started&&launchReceipt.pid===found.pid&&launchReceipt.processCreated===identity.processCreated;
      // Reusing an unreadable external process does not transfer its ownership.
      if(createdHere)losslessSession=session;
      const configured=parseLsProfiles(await fs.readTextFile(await getLsSettingsPath(),2*1024*1024)); checkpoint();
      const target=matchLsProfile(configured,gamePath);
      // The original profile remains the owner. Duplicate profile titles cannot
      // prove which active profile a native receipt belongs to, so do not toggle.
      if(target?.enabled&&configured.filter(item=>item.title===target.title).length===1){
        // PID is optional admission from YMCC's existing target, never a game
        // classification requirement. Path-only callers pin their foreground
        // process/window in native before the first Invoke.
        const supplied=gameTarget!==undefined;
        if(!supplied||(gameTarget.pid&&gameTarget.processCreated&&normalizeLsPath(gameTarget.path)===normalizeLsPath(gamePath))){
          const gameIdentity=supplied?{gamePid:gameTarget.pid,gameProcessCreated:gameTarget.processCreated}:{};
          try { activation=await losslessRuntime.ensureScaling({exe:ls.exe,pid:found.pid,processCreated:identity.processCreated,
            gamePath,...gameIdentity,profileTitle:target.title,reapply:true,refreshWindow:true,resetOnFailure:false,retryAttempt}); }
          catch (error) {
            activation={ok:false,enabled:null,invoked:false,pending:true,cancelled:false,reason:'scaling-state-unavailable'};
            // IPC timeout is independent of the blocked UIA owner. Terminate
            // only its original exact identity, then the wrapper can retry once.
            const timeout=error as {code?:unknown;command?:unknown}|null;
            if(timeout?.code==='IPC_TIMEOUT'&&timeout.command==='lossless.ensureScaling'){
              activation.scalingTimedOut=true;activation.reason='scaling-ipc-timeout';
              checkpoint();
              try {const reset=await losslessRuntime.resetFailed({exe:ls.exe,pid:found.pid,processCreated:identity.processCreated,retryAttempt});
                activation={...activation,failureResetRequested:true,failureResetAttempted:reset.failureResetAttempted,failureResetTerminated:reset.failureResetTerminated,failureResetClosed:reset.failureResetClosed,failureResetReason:reset.failureResetReason};}
              catch {activation.failureResetRequested=true;activation.failureResetReason='failure-reset-unavailable';}
            }
          }
          if(activation.failureResetClosed&&losslessSession?.pid===session.pid&&losslessSession.processCreated===session.processCreated)losslessSession=null;
          if(activation.ok&&activation.enabled===true)losslessSession=session;
          checkpoint();
        }else activation={ok:false,enabled:null,invoked:false,pending:false,cancelled:false,reason:'game-identity-unavailable'};
      }
    }
  }
  notifyLosslessScalingChanged();
  return { exe: ls.exe, source: ls.source, alreadyHadProfile: already, activation };
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
export type OptiAction = OptiBackend | 'client' | 'uninstall' | 'help';

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
  scanComplete?: boolean;
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
      scanComplete: obj?.scan_complete === true,
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
  antiCheat?: boolean;
  clientSyncMessage?: string;
  clientSyncStatus?: string;
}

// uninstall=true 卸载（还原原文件），false 安装。安装时 backend 必须由界面
// 明确选择或传入 auto；后端选择会同时写入 OptiScaler.ini。
export async function oneClickOptiScaler(
  gamePath: string,
  uninstall: boolean,
  backend: OptiBackend | 'auto' = 'auto',
  options: { acknowledgeAntiCheatRisk?: boolean } = {},
): Promise<OptiResult> {
  const gameDir = dirnameOf(gamePath);
  const sub = uninstall ? 'uninstall' : 'install';
  try {
    const args = ['optiscaler', sub, gameDir];
    if (!uninstall) {
      args.push('--backend', backend);
      if (options.acknowledgeAntiCheatRisk === true) args.push('--acknowledge-anticheat-risk');
    }
    const r = await shell.run(await resolveOptiCtlExe(), args, 60000);
    const txt = (r.stdout || '').trim();
    if (!txt) {
      return { ok: false, msgs: (r.stderr || '无输出').split('\n').slice(0, 3) };
    }
    const obj = JSON.parse(txt);
    const result: OptiResult = {
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
      antiCheat: obj?.anti_cheat === true || obj?.analysis?.anti_cheat === true,
      warnings: Array.isArray(obj?.warnings) ? obj.warnings.map(String) : [],
    };
    if (result.ok) {
      // The client also recognizes an INI as an installation marker. On restore,
      // an original OPT install may remain, so do not blindly mark it uninstalled.
      let installed = !uninstall;
      if (uninstall) {
        try { installed = await fs.exists(joinPath(gameDir, "OptiScaler.ini")); }
        catch {
          result.clientSyncMessage = "还原已完成，但无法确认现存 OPT 状态；客户端登记未更新，请重新扫描。";
          result.clientSyncStatus = "error";
          return result;
        }
      }
      const sync = await syncOptiClientGame(gamePath, installed, uninstall ? undefined : result.version);
      result.clientSyncMessage = sync.message;
      result.clientSyncStatus = sync.status;
    }
    return result;
  } catch (e) {
    return { ok: false, msgs: [(e as Error).message] };
  }
}

// 打开前只同步暂存的客户端游戏列表：不轮询、不关闭进程、不修改游戏文件。
export const OPTISCALER_CLIENT_DIR = 'C:\\SOFT\\OptiscalerClient';
export const OPTISCALER_CLIENT_EXE = 'C:\\SOFT\\OptiscalerClient\\OptiscalerClient.exe';
export const OPTISCALER_CLIENT_URL = 'https://github.com/Optiscaler-Client/Optiscaler-Client/releases';

export async function optiConsoleInstalled(): Promise<boolean> {
  return fs.exists(OPTISCALER_CLIENT_EXE).catch(() => false);
}

// GUI 程序必须用 shell.execute；重复点击仍然只发送打开请求。
export async function openOptiConsole(): Promise<string> {
  const sync = await flushOptiClientGames();
  const opened = await shell.execute(OPTISCALER_CLIENT_EXE, []);
  if (opened === false) throw new Error("OPT 客户端启动请求失败");
  return sync.message;
}

// Original YMCC owner for LS imports, starts and stops. The console calls this
// owner; it never writes LS settings or creates a second control path.
const losslessListeners = new Set<() => void>();
const losslessDocuments=new Map<string,{revision:number;task:Promise<LosslessGameState>}>();
function publishLosslessScalingChanged():void {for(const listener of [...losslessListeners]){try{listener();}catch{}}}
subscribeLosslessRuntime(()=>{losslessDocuments.clear();publishLosslessScalingChanged();});
export function onLosslessScalingChanged(listener: () => void): () => void {
  losslessListeners.add(listener); return () => { losslessListeners.delete(listener); };
}
function notifyLosslessScalingChanged(): void {
  invalidateLosslessRuntime();losslessDocuments.clear();publishLosslessScalingChanged();
}
function psQuoted(value: string): string { return "'" + value.replace(/'/g, "''") + "'"; }
function encodeLosslessScript(script: string): string {
  const bytes = new Uint8Array(script.length * 2);
  for (let i = 0; i < script.length; i++) { const code = script.charCodeAt(i); bytes[2*i] = code & 255; bytes[2*i+1] = code >> 8; }
  return btoa(Array.from(bytes, byte => String.fromCharCode(byte)).join(''));
}
async function launchOriginalLossless(executable: string, checkpoint: () => void): Promise<{started:boolean;pid:number;processCreated:string}> {
  checkpoint();
  // The original game Profile/AutoScale remains responsible for scaling.
  // Fixed native arguments replace the old unhandled -path/-auto CLI request.
  const result = await losslessRuntime.ensureRunning(executable); checkpoint();
  if (!result.ok) throw new Error('YMCC 小黄鸭启动请求失败：' + result.reason +
    (result.win32Error ? '（Windows ' + result.win32Error + '）' : '') + '；未自动重发');
  return result;
}
async function closeOriginalLossless(executable: string, pid: number, checkpoint: () => void): Promise<void> {
  const identity = await proc.identity(pid); checkpoint();
  if (!identity.valid || !identity.processCreated || normalizeLsPath(identity.path || '') !== normalizeLsPath(executable)) throw new Error('小黄鸭进程身份已变化，未关闭');
  // One asynchronous WM_CLOSE to the exact WPF main window, including hidden
  // and taskbar-less windows. Never show/activate it or force-kill the process.
  const script = `$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
$OutputEncoding=[Console]::OutputEncoding
# YMCC_LOSSLESS_CLOSE: PID + process creation time + path, held on one handle.
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class YmccLosslessClose {
 delegate bool WindowVisitor(IntPtr window,IntPtr state);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint rights,bool inherit,uint pid);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr handle,uint flags,StringBuilder image,ref uint size);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr handle,out long created,out long exited,out long kernel,out long user);
 [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
 [DllImport("kernel32.dll")] static extern uint GetCurrentProcessId();
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ProcessIdToSessionId(uint pid,out uint session);
 [DllImport("user32.dll",SetLastError=true)] static extern bool EnumWindows(WindowVisitor visitor,IntPtr state);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,StringBuilder title,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
 [DllImport("user32.dll",SetLastError=true)] static extern bool PostMessage(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
 static bool IsMain(IntPtr window,uint expected) {
  uint pid;GetWindowThreadProcessId(window,out pid);if(pid!=expected)return false;
  var title=new StringBuilder(512);var name=new StringBuilder(512);
  GetWindowText(window,title,title.Capacity);GetClassName(window,name,name.Capacity);
  return title.ToString()=="Lossless Scaling" && name.ToString().StartsWith("HwndWrapper[LosslessScaling;",StringComparison.Ordinal);
 }
 public static void Close(uint pid,string expectedPath,ulong expectedCreated) {
  var handle=OpenProcess(0x00101000,false,pid);
  if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"小黄鸭进程不可读，未关闭");
  try {
   if(WaitForSingleObject(handle,0)==0)return;
   var image=new StringBuilder(32768);uint size=(uint)image.Capacity;long created,exited,kernel,user;uint session,currentSession;
   if(!QueryFullProcessImageName(handle,0,image,ref size) || !GetProcessTimes(handle,out created,out exited,out kernel,out user) ||
      !ProcessIdToSessionId(pid,out session) || !ProcessIdToSessionId(GetCurrentProcessId(),out currentSession) ||
      session!=currentSession || (ulong)created!=expectedCreated || !String.Equals(image.ToString(),expectedPath,StringComparison.OrdinalIgnoreCase))
    throw new InvalidOperationException("小黄鸭进程身份已变化，未关闭");
   IntPtr main=IntPtr.Zero;int count=0;
   WindowVisitor visitor=delegate(IntPtr window,IntPtr state){if(IsMain(window,pid)){main=window;count++;}return true;};
   if(!EnumWindows(visitor,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
   GC.KeepAlive(visitor);
   if(count!=1)throw new InvalidOperationException("小黄鸭主窗口未唯一确认；未显示窗口、未重复关闭");
   if(WaitForSingleObject(handle,0)==0)return;
   if(!IsMain(main,pid))throw new InvalidOperationException("小黄鸭窗口已变化，未关闭");
   if(!PostMessage(main,0x0010,IntPtr.Zero,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error(),"小黄鸭关闭请求被拒绝");
   if(WaitForSingleObject(handle,3000)!=0)throw new InvalidOperationException("小黄鸭仍在运行（可能关闭到托盘或窗口无响应）；未强杀、未写配置、未自动重试");
  } finally {CloseHandle(handle);}
 }
}
"@
[YmccLosslessClose]::Close([uint32]${pid},${psQuoted(executable)},[uint64]${psQuoted(identity.processCreated)})`;

  const result = await shell.run('powershell', ['-NoProfile', '-NonInteractive', '-EncodedCommand', encodeLosslessScript(script)], 5000); checkpoint();
  if (result.exitCode !== 0) throw new Error((result.stderr || result.stdout || '小黄鸭正常退出失败').trim().slice(0, 600));
  const remaining = await proc.identity(pid); checkpoint();
  if (remaining.valid && remaining.processCreated === identity.processCreated) throw new Error('小黄鸭进程未退出，未写配置');
}
// Explicit ownership of a command sent by this YMCC process, bound to the
// exact LS process. It is never restored from a saved preference or a timeout.
let losslessSession: {gamePath:string;exe:string;pid:number;processCreated:string} | null = null;
export interface LosslessGameState {
  settingsPath: string; xml: string; profile: LsProfile | null;
  /** Actual Scale-button receipt only; null is unreadable, not disabled. */
  enabled: boolean | null; running: boolean; reason: string;
  configuredEnabled: boolean; controlAvailable: boolean; pending?:boolean; phase?:'starting'|'stopping';
}
// UI observation is optional evidence. The game configuration switch remains
// controllable in Xbox fullscreen even if LS's WPF window is not exposed to UIA.
export async function getLosslessGameState(gamePath:string,waitForEnabled?:boolean):Promise<LosslessGameState>{
  await readLosslessRuntime();
  const key=normalizeLsPath(gamePath),revision=losslessRuntimeRevision();
  const existing=losslessDocuments.get(key);if(existing?.revision===revision)return withLosslessAction(await existing.task);
  const task=readLosslessGameState(gamePath,waitForEnabled);losslessDocuments.set(key,{revision,task});
  if(losslessDocuments.size>8)losslessDocuments.delete(losslessDocuments.keys().next().value!);
  void task.catch(()=>{if(losslessDocuments.get(key)?.task===task)losslessDocuments.delete(key);});return withLosslessAction(await task);
}
function withLosslessAction(document:LosslessGameState):LosslessGameState{return losslessAction?{...document,pending:true,phase:losslessAction.enabled?'starting':'stopping'}:{...document,pending:false};}
async function readLosslessGameState(gamePath:string,waitForEnabled?:boolean):Promise<LosslessGameState>{
  const settingsPath = await getLsSettingsPath();
  if (!await fs.exists(settingsPath)) return {settingsPath,xml:'',profile:null,enabled:false,running:false,configuredEnabled:false,controlAvailable:false,reason:'未找到小黄鸭 Settings.xml；请先手动启动一次'};
  const xml = await fs.readTextFile(settingsPath, 2 * 1024 * 1024);
  const profiles = parseLsProfiles(xml), profile = matchLsProfile(profiles, gamePath);
  const configuredEnabled = profile?.enabled === true;
  const runtime=await readLosslessRuntime();
  const base={settingsPath,xml,profile,configuredEnabled,controlAvailable:true};
  if(!runtime.known)return {...base,controlAvailable:false,enabled:null,running:runtime.running,reason:'小黄鸭运行实例未确认：'+runtime.reason};
  if(!runtime.running){
    const ls=await resolveLs();return {...base,controlAvailable:ls.source!=='none',enabled:false,running:false,reason:ls.source==='none'?'未找到小黄鸭程序':''};
  }
  if(!profile)return {...base,enabled:null,running:true,reason:'当前游戏未配置小黄鸭插帧'};
  // The native reader locates the exact WPF main window, including hidden
  // windows; Process.MainWindowHandle may identify a theme or output window.
  void waitForEnabled;
  try {
    const observed={ok:runtime.readable,button:runtime.button,status:runtime.status};
    if(!observed.ok)throw new Error('小黄鸭状态回读失败');
    const on = ['Unscale', 'Stop scaling', '停止缩放', '停止縮放', '取消缩放', '取消縮放', '无缩放', '無縮放'].includes(observed.button);
    const off = ['Scale', '缩放', '開始縮放', '开始缩放'].includes(observed.button);
    if (!on && !off) throw new Error('小黄鸭正在切换，未把未知状态当作关闭');
    if (!on) return {...base,enabled:false,running:true,reason:''};
    // AutoScale changes ActivatedProfile without selecting ProfileList. Only
    // the actual status receipt's third line names the active Profile. Missing
    // or ambiguous text stays unknown; the selected sidebar row proves nothing.
    const line = typeof observed.status === 'string' ? observed.status.split(/\r?\n/)[2]?.trim() : undefined;
    const separator = line?.search(/[:：]/) ?? -1;
    const receipt = separator >= 0 ? line!.slice(separator + 1).trim() : '';
    const active = receipt ? profiles.filter(item => ['"'+item.title+'"','“'+item.title+'”','«'+item.title+'»'].includes(receipt)) : [];
    if (active.length !== 1 || !active[0].path) throw new Error('小黄鸭实际处理的配置未确认');
    const paused=observed.status.split(/\r?\n/).filter(line=>line.trim()).length>3;
    if(paused)return {...base,enabled:null,running:true,reason:'小黄鸭缩放会话已暂停'};
    const enabled = normalizeLsPath(active[0].path) === normalizeLsPath(profile.path);
    return {...base,enabled,running:true,reason:enabled?'':'小黄鸭正在处理另一 EXE'};
  } catch {
    return {...base,enabled:null,running:true,reason:'当前游戏配置已读取；实际插帧状态暂不可读'};
  }
}
export async function stopLosslessScaling(gamePath: string, checkpoint: () => void = () => {}): Promise<void> {
  return withLsSettingsLock(async () => {
    const state = await getLosslessGameState(gamePath); checkpoint();
    // Already absent means this game's toggle is off. Never close another
    // game's LS process just because it shares the same global settings file.
    const hasOwnedIntent = losslessSession?.gamePath === normalizeLsPath(gamePath);
    if(state.profile?.enabled===false&&state.enabled!==true&&!hasOwnedIntent)return;
    if (!state.profile && (!state.running || !hasOwnedIntent)) return;
    if (state.running) {
      const ls = await resolveLs(); checkpoint();
      const found = await proc.findExact(ls.exe); checkpoint();
      if (found.found) {
        const identity = await proc.identity(found.pid); checkpoint();
        const owned = losslessSession && losslessSession.gamePath === normalizeLsPath(gamePath) &&
          losslessSession.exe === normalizeLsPath(ls.exe) && losslessSession.pid === found.pid &&
          losslessSession.processCreated === identity.processCreated;
        if (!state.profile && !owned) return;
        if ((state.enabled === null && !owned) || (state.enabled === false && state.reason.includes('另一 EXE')))
          throw new Error(state.reason || '小黄鸭会话所属游戏未确认，未关闭其它游戏');
        await closeOriginalLossless(ls.exe, found.pid, checkpoint);
      }
    }
    // LS has finished its last in-memory save. Take the fresh document and
    // disable only AutoScale in the exact profile; retain custom parameters.
    const original = await fs.readTextFile(state.settingsPath, 2 * 1024 * 1024); checkpoint();
    const profile = matchLsProfile(parseLsProfiles(original), gamePath);
    if (!profile) { losslessSession = null; notifyLosslessScalingChanged(); return; }
    const disabled=profile.block.replace(/(<AutoScale>\s*)true(\s*<\/AutoScale>)/,'$1false$2');
    const next=original.slice(0,profile.start)+disabled+original.slice(profile.end);
    if(next===original){losslessSession=null;notifyLosslessScalingChanged();return;}
    parseLsProfiles(next);
    await fs.writeTextFileAtomic(state.settingsPath + '.ymccbak', original); checkpoint();
    if (await fs.readTextFile(state.settingsPath, 2 * 1024 * 1024) !== original)
      throw new Error('小黄鸭配置已变化，未关闭自动缩放；请重试');
    checkpoint();
    await fs.writeTextFileAtomic(state.settingsPath, next); checkpoint();
    if (await fs.readTextFile(state.settingsPath, 2 * 1024 * 1024) !== next)
      throw new Error('小黄鸭关闭后的配置回读不一致，未宣称关闭成功');
    losslessSession = null;
    notifyLosslessScalingChanged();
  });
}
export interface LosslessReturnTarget { pid:number; processCreated:string; path:string; }
export interface LosslessToggleResult { confirmed:boolean; runtimeConfirmed:boolean|null; notice:string; pending?:boolean; }
let losslessActionSerial=0;
let losslessActionQueue:Promise<unknown>=Promise.resolve();
let losslessAction:{key:string;enabled:boolean;task:Promise<LosslessToggleResult>}|null=null;
export function setLosslessScalingEnabled(gamePath:string,enabled:boolean,checkpoint:()=>void=()=>{},gameTarget?:LosslessReturnTarget):Promise<LosslessToggleResult>{
  const key=JSON.stringify([normalizeLsPath(gamePath),enabled,gameTarget?.pid,gameTarget?.processCreated]);
  if(losslessAction?.key===key)return losslessAction.task;
  const serial=++losslessActionSerial;
  const check=()=>{if(serial!==losslessActionSerial)throw new Error('小黄鸭操作已被较新的开关请求替代');checkpoint();};
  const task=losslessActionQueue.catch(()=>{}).then(()=>{check();notifyLosslessScalingChanged();return applyLosslessScalingEnabled(gamePath,enabled,check,gameTarget);});
  losslessAction={key,enabled,task};losslessActionQueue=task;
  void task.finally(()=>{if(losslessAction?.task===task){losslessAction=null;notifyLosslessScalingChanged();}}).catch(()=>{});
  return task;
}
async function applyLosslessScalingEnabled(gamePath: string, enabled: boolean,
    checkpoint: () => void = () => {}, gameTarget?: LosslessReturnTarget): Promise<LosslessToggleResult> {
  const launched = enabled ? await oneClickFrameGen(gamePath, checkpoint, gameTarget) : undefined;
  if (!enabled) await stopLosslessScaling(gamePath, checkpoint);
  // Normal operation observes once. Xbox fullscreen may hide UIAutomation;
  // Confirmation requires actual current-profile receipt; never infer FPS.
  const state = await getLosslessGameState(gamePath); checkpoint();
  const confirmed = enabled ? launched?.activation?.ok===true&&launched.activation.enabled===true&&launched.activation.windowRefreshed===true&&state.running&&state.configuredEnabled&&state.enabled!==false : !state.configuredEnabled&&(!state.running||state.enabled===false||!state.profile);
  const runtimeConfirmed = enabled ? confirmed?true:!state.running||state.enabled===false?false:null : confirmed && !state.running ? false : null;
  // One explicit click may refresh the exact original game window once.
  // Native owns that bounded cycle; do not arm the legacy return/focus timer.
  notifyLosslessScalingChanged();
  const activationReason=launched?.activation?.reason||'';
  const reasons:Record<string,string>={
    'target-not-foreground':'游戏尚未回到前台；切回游戏后由 EXE 自动规则接续',
    'scaling-state-unavailable':'程序已运行，本次缩放未确认；可切换回游戏一次后再试',
    'no-focus-automation-unavailable':'程序已运行，但后台控制通道不可用',
    'scaling-paused':'状态含暂停提示，实际缩放仍待确认；保留小黄鸭运行',
    'scaling-ipc-timeout':'缩放调用未返回，已进入有界自愈',
    'restart-stop-unconfirmed':'原缩放会话停止未获确认；没有继续切换',
    'another-profile-active':'小黄鸭正在处理其它配置；未切换其会话',
    'scaling-busy':'已有缩放请求正在执行',
    'scaling-pending':'缩放结果仍待确认',
    'window-refresh-context-changed':'窗口刷新前运行状态已变化',
    'window-refresh-input-unavailable':'无法确认用户输入状态，未刷新窗口',
    'window-refresh-target-changed':'原游戏已不在前台，未刷新窗口',
    'window-refresh-cancelled':'用户操作或前台已变化，本次窗口刷新已停止',
    'window-refresh-create-failed':'原生窗口刷新通道未创建',
    'window-refresh-activation-denied':'系统未允许窗口刷新，请手动呼出 YMCC 后返回游戏',
    'window-refresh-return-unconfirmed':'返回原游戏未获确认，请手动返回游戏',
    'window-refresh-cleanup-unconfirmed':'窗口刷新清理未获确认',
    'window-foreground-request-denied':'系统未接受游戏前台请求，画面效果待确认',
    'window-frame-request-denied':'系统未接受原生窗口框架刷新请求，画面效果待确认',
    'window-redraw-request-denied':'系统未接受原生重绘请求，画面效果待确认',
    'window-refresh-budget-unavailable':'本次准备耗时过长，未追加窗口刷新',
  };
  const activationNotice='；'+(reasons[activationReason]||'缩放结果未确认')+'；实际插帧状态待确认';
  const reset=launched?.activation;
  const resetNotice=reset?.failureResetRequested
    ? reset.failureResetClosed?'本次缩放未成功，已清理小黄鸭进程；游戏配置已保留，再点一次即可重试'
      :'本次缩放未成功，强制结束小黄鸭未获确认；本次操作已结束，可再次尝试' : '';
  const notice = resetNotice || (!confirmed ? enabled&&state.configuredEnabled&&state.running?`当前游戏配置已开启${activationNotice}`:`小黄鸭${enabled?'开启':'关闭'}结果待确认，不自动重发` : enabled
    ? `当前游戏缩放请求已应用${launched?.activation?.restarted?'（已重建本次会话）':''}${launched?.activation?.windowRefreshed?'；已提交游戏前台请求':''}；画面效果以游戏为准`
    : !state.running ? '当前游戏小黄鸭插帧已关闭，游戏参数已保留' : '当前游戏自动缩放已关闭，游戏参数已保留');
  const retryNotice=reset?.retryCount===1?'（已自动重试 1 次）':'';
  const preservedNotice=enabled&&!confirmed&&state.running&&!reset?.scalingTimedOut?'；已保留小黄鸭运行，本次不自动重试':'';
  return {confirmed,runtimeConfirmed,notice:notice+retryNotice+preservedNotice,pending:false};
}
