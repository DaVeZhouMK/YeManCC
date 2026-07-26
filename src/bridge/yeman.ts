// yeman.ts — 语义化后端桥层（强强壳 shell.run / fs 封装）
//
// 所有 powercfg GUID、任务计划名、bat/vbs/ps1 路径、厂商识别逻辑都收在这里，
// 前端只调语义化方法：setTdp('ac', 300) / toggleTask('TDP-插电AC模式TDP调节', true) / ...
//
// 配置真相源 = C:\SOFT\YeMan\PowerControl\ 下的 txt（与旧 HTA 共用）。
// 任务计划只识别状态、不解析内容（schtasks 创建，/Query 判断存在即=开关状态）。
import { fs, shell, registry, type RunResult } from './api';
import { invoke } from './ipc';

// ── 可配置根目录（自测时可指向临时目录） ──
let PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
export function setPowerControlDir(dir: string): void {
  PC_DIR = dir.replace(/\//g, '\\');
}
export function getPowerControlDir(): string {
  return PC_DIR;
}
function join(...parts: string[]): string {
  return parts.join('\\').replace(/\//g, '\\');
}

// ── TDP 常量（对齐 HTA：TDP_CEILINGS / TDP_MIN） ──
export const TDP_CEILINGS = [20, 35, 55, 75, 120, 300];
export const TDP_MIN = 2;
export const TDP_MAX = 300;
export function clampTdp(w: number): number {
  return Math.max(TDP_MIN, Math.min(TDP_MAX, Math.round(w)));
}
// 给定 savedVal 求对应的上限档（>= 的最小 ceiling）
export function smallestCeiling(val: number): number {
  for (const c of TDP_CEILINGS) if (c >= val) return c;
  return TDP_CEILINGS[TDP_CEILINGS.length - 1];
}

// ── 12 个任务计划（名称/触发/调用资产/XML模板，名称与 PLAN §七、HTA 完全一致） ──
export const TASK_FOLDER = '野蛮优化整合系统';
export interface TaskDef {
  name: string;
  trigger: string;
  asset: string;
  xml?: string; // XML 模板相对 PowerControl 的路径（缺省=无模板，仅能删除/查询）
}
export const TASKS: TaskDef[] = [
  { name: 'TDP-开机启动野蛮快设TDP挡位', trigger: '开机', asset: 'AUTOPlan.bat(vbs 静默)', xml: 'TDP-开机启动野蛮快设TDP挡位.xml' },
  { name: 'TDP-插电AC模式TDP调节', trigger: '电源事件 AC', asset: 'Plan-AC.bat', xml: 'TDP-插电AC模式TDP调节.xml' },
  { name: 'TDP-离电DC模式TDP调节', trigger: '电源事件 DC', asset: 'Plan-DC.bat', xml: 'TDP-离电DC模式TDP调节.xml' },
  { name: '唤醒后-执行任务', trigger: '唤醒', asset: 'YeManWake.bat', xml: '唤醒后-执行任务.xml' },
  { name: '锁帧-插电AC模式锁帧', trigger: '电源事件 AC', asset: 'RTSS-FPS-AC.bat', xml: '锁帧-插电AC模式锁帧.xml' },
  { name: '锁帧-离电DC模式锁帧', trigger: '电源事件 DC', asset: 'RTSS-FPS-DC.bat', xml: '锁帧-离电DC模式锁帧.xml' },
  { name: '监控-开机启动监控锁帧软件RTSS', trigger: '开机', asset: 'YeManRTSS.bat', xml: '监控-开机启动监控锁帧软件RTSS.xml' },
  { name: 'Xbox大屏游戏模式', trigger: '开机', asset: 'YeManSteam.bat', xml: 'Xbox大屏游戏模式.xml' },
  { name: '桌面模式-开机设置为桌面模式', trigger: '开机', asset: '(内置)', xml: '桌面模式-开机设置为桌面模式.xml' },
  { name: '节能-能源之星', trigger: '开机', asset: 'EnergyStar.vbs', xml: '节能-能源之星.xml' },
  { name: '内存-开机自动内存清理并关闭', trigger: '开机', asset: 'MG-AUTO\\清理内存.bat', xml: '内存-开机自动内存清理并关闭.xml' },
  { name: 'Bug修复-AMD-395', trigger: '开机', asset: 'C:\\SOFT\\3DMark\\YeMan-3DMark.bat', xml: 'Bug修复-AMD-395.xml' },
  { name: '野蛮控制中心-开机启动', trigger: '开机', asset: 'YeManCC.exe --minimized', xml: '野蛮控制中心-开机启动.xml' },
];
export function getTaskDef(name: string): TaskDef | undefined {
  return TASKS.find((t) => t.name === name);
}

// ── txt 配置读写（真相源） ──
export async function saveTdp(mode: 'ac' | 'dc', watts: number): Promise<void> {
  await fs.writeTextFile(join(PC_DIR, `tdp-${mode}.txt`), String(clampTdp(watts)));
}
export async function readTdp(mode: 'ac' | 'dc'): Promise<number | null> {
  const p = join(PC_DIR, `tdp-${mode}.txt`);
  if (!(await fs.exists(p))) return null;
  const s = (await fs.readTextFile(p)).trim();
  const n = Number(s);
  return Number.isFinite(n) ? clampTdp(n) : null;
}
export async function saveFps(mode: 'ac' | 'dc', fps: number): Promise<void> {
  await fs.writeTextFile(join(PC_DIR, `FPS-${mode}.txt`), String(Math.max(0, Math.round(fps))));
}
export async function readFps(mode: 'ac' | 'dc'): Promise<number | null> {
  const p = join(PC_DIR, `FPS-${mode}.txt`);
  if (!(await fs.exists(p))) return null;
  const s = (await fs.readTextFile(p)).trim();
  const n = Number(s);
  return Number.isFinite(n) ? n : null;
}
export async function savePower(key: string, value: string): Promise<void> {
  await fs.writeTextFile(join(PC_DIR, 'Power.txt'), `${key}=${value}\n`);
}
export async function readPowerRaw(): Promise<string> {
  const p = join(PC_DIR, 'Power.txt');
  return (await fs.exists(p)) ? await fs.readTextFile(p) : '';
}

// ── 任务计划：只识别状态 ──
// 重要（对齐 HTA）：schtasks.exe 在非提权下可能 WER 崩溃 0xc0000142，
// 因此首选检测任务文件是否存在（零权限、零崩溃），schtasks /Query 仅作后备。
const TASK_FILE_ROOT = 'C:\\Windows\\System32\\Tasks';
function taskPath(name: string): string {
  return `${TASK_FOLDER}\\${name}`;
}
function taskFilePath(name: string): string {
  return `${TASK_FILE_ROOT}\\${TASK_FOLDER}\\${name}`;
}
export async function taskExists(name: string): Promise<boolean> {
  // 方法1（首选，对齐 HTA）：检测任务文件是否存在
  try {
    if (await fs.exists(taskFilePath(name))) return true;
  } catch {
    /* → fallback */
  }
  // 方法2（后备）：schtasks /Query（可能在非提权下崩溃）
  try {
    const r = await shell.run('schtasks', ['/Query', '/TN', taskPath(name), '/FO', 'CSV']);
    if (r.exitCode === 0) return true;
    if ((r.stdout || '').includes(name)) return true;
  } catch {
    /* 文件也不存在、schtasks 也失败 → 任务不存在 */
  }
  return false;
}
export async function deleteTask(name: string): Promise<boolean> {
  const r = await shell.run('schtasks', ['/Delete', '/TN', taskPath(name), '/F']);
  return r.exitCode === 0;
}
export async function createTask(name: string): Promise<boolean> {
  const def = getTaskDef(name);
  if (!def || !def.xml) {
    throw new Error(`任务「${name}」无 XML 模板，无法创建（仅支持查询/删除）`);
  }
  const xml = join(PC_DIR, def.xml);
  if (!(await fs.exists(xml))) {
    throw new Error(`XML 模板不存在: ${xml}`);
  }
  // 需要管理员权限写入任务计划（UAC 保护）；失败时 exitCode 非 0，调用方应提示用户以管理员运行。
  const r = await shell.run('schtasks', ['/Create', '/TN', taskPath(name), '/XML', xml, '/F']);
  return r.exitCode === 0;
}
// 依赖野蛮系统电源方案的任务（开机/唤醒任务会执行 powercfg -setactive YEMAN）
const SCHEME_DEPENDENT_TASKS = new Set([
  'TDP-开机启动野蛮快设TDP挡位',
  '唤醒后-执行任务',
]);

// toggle：开→建（有模板），关→删。返回最新状态
export async function toggleTask(name: string, on: boolean): Promise<boolean> {
  if (on) {
    // 仅在任务不存在时才创建；创建失败（如缺管理员权限）要明确抛出，让 UI 回滚
    if (!(await taskExists(name))) {
      // 开机/唤醒任务依赖野蛮系统电源方案，若用户删除过该方案则先重新导入 YM.pow
      if (SCHEME_DEPENDENT_TASKS.has(name)) {
        await ensureYemanScheme();
      }
      const ok = await createTask(name);
      if (!ok) throw new Error('创建任务失败（可能需以管理员身份运行 YeManCC）');
    }
    return true;
  } else {
    if (await taskExists(name)) await deleteTask(name);
    return false;
  }
}

// 手柄/快捷键设置（后台手柄 LB+RB 呼出、双击B最小化、Start+D-pad 快捷调节）
export interface GamepadSettings {
  enabled: boolean;          // LB+RB 呼出窗口
  bDoubleMinimize: boolean;  // 双击 B 最小化到托盘
  tdpShortcut: boolean;      // Start + 上/下 调节 TDP ±1W
  fpsShortcut: boolean;      // Start + 左/右 调节 RTSS 锁帧 ±5
  killGame: boolean;         // 选择 + B 长按 0.5s → 结束当前游戏（执行 KiLL-EXE.bat）
  openKeyboard: boolean;     // 选择 + X 长按 0.5s → 呼出 Windows 触摸键盘
}
const DEFAULT_GAMEPAD_SETTINGS: GamepadSettings = {
  enabled: true,
  bDoubleMinimize: true,
  tdpShortcut: false,
  fpsShortcut: false,
  killGame: false,
  openKeyboard: false,
};
export async function summonGet(): Promise<GamepadSettings> {
  try {
    const r = await invoke<Partial<GamepadSettings>>('summon.get', {});
    return { ...DEFAULT_GAMEPAD_SETTINGS, ...r };
  } catch {
    return { ...DEFAULT_GAMEPAD_SETTINGS };
  }
}
export async function summonSet(settings: Partial<GamepadSettings>): Promise<GamepadSettings> {
  const r = await invoke<Partial<GamepadSettings>>('summon.set', settings);
  return { ...DEFAULT_GAMEPAD_SETTINGS, ...r };
}

// ── 掌机前端自动关闭：可编辑进程名列表 + 总开关 ──
export interface AutoCloseConfig {
  enabled: boolean;
  procs: string[];
}
export async function autocloseGet(): Promise<AutoCloseConfig> {
  try {
    const r = await invoke<{ enabled: boolean; procs: string[] }>('autoclose.get', {});
    return { enabled: !!r.enabled, procs: Array.isArray(r.procs) ? r.procs : [] };
  } catch {
    return { enabled: false, procs: [] };
  }
}
export async function autocloseSet(cfg: AutoCloseConfig): Promise<AutoCloseConfig> {
  const r = await invoke<{ enabled: boolean; procs: string[] }>('autoclose.set', {
    enabled: cfg.enabled,
    procs: cfg.procs,
  });
  return { enabled: !!r.enabled, procs: Array.isArray(r.procs) ? r.procs : [] };
}

// ── 更新加速器（steamcommunity_302 等）：手动按钮 + 文件/运行状态 ──
export interface UpdateAccelState {
  exists: boolean;
  running: boolean;
}
export async function updateAccelGet(): Promise<UpdateAccelState> {
  try {
    const r = await invoke<Partial<UpdateAccelState>>('updateAccel.get', {});
    return {
      exists: !!r.exists,
      running: !!r.running,
    };
  } catch {
    return { exists: false, running: false };
  }
}
export async function updateAccelToggle(): Promise<UpdateAccelState & { ok: boolean }> {
  const r = await invoke<Partial<UpdateAccelState & { ok: boolean }>>('updateAccel.set', {});
  return {
    exists: !!r.exists,
    running: !!r.running,
    ok: !!r.ok,
  };
}

// ── 自动更新（native 更新器：app.checkUpdate / app.downloadUpdate / app.installUpdate）──
export interface UpdateInfo {
  version: string;
  notes?: string;
  sha256?: string;
  publishedAt?: string;
}
// version.json 拉取地址（raw 分支，避免 GitHub API 限流）；版本号一致时由前端比较
export const UPDATE_MANIFEST_URL =
  'https://raw.githubusercontent.com/DaVeZhouMK/YeManCC/main/version.json';
// 下载地址由版本号拼出：releases/download/v<version>/YeManCC.zip
export function updatePackageUrl(version: string): string {
  return `https://github.com/DaVeZhouMK/YeManCC/releases/download/v${version}/YeManCC.zip`;
}
export async function appVersion(): Promise<string> {
  return invoke<string>('app.version');
}
export async function checkUpdate(url: string): Promise<UpdateInfo> {
  return invoke<UpdateInfo>('app.checkUpdate', { url });
}
export async function downloadUpdate(url: string, sha256?: string): Promise<string> {
  return invoke<string>('app.downloadUpdate', { url, sha256: sha256 ?? '' });
}
export async function installUpdate(): Promise<boolean> {
  return invoke<boolean>('app.installUpdate');
}
// 语义化版本比较：a<b 返回 -1，a==b 返回 0，a>b 返回 1
export function compareVersions(a: string, b: string): number {
  const pa = String(a).split('.').map((n) => parseInt(n, 10) || 0);
  const pb = String(b).split('.').map((n) => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const x = pa[i] || 0;
    const y = pb[i] || 0;
    if (x !== y) return x < y ? -1 : 1;
  }
  return 0;
}

// ── 厂商识别（AMD/Intel），沿用 PowerControl\AMD.txt|intel.txt 优先，否则注册表 ──
export type Vendor = 'amd' | 'intel' | 'unknown';
export async function detectVendor(): Promise<Vendor> {
  if (await fs.exists(join(PC_DIR, 'AMD.txt'))) return 'amd';
  if (await fs.exists(join(PC_DIR, 'intel.txt'))) return 'intel';
  try {
    const v = await registry.read(
      'HKLM',
      'HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0',
      'VendorIdentifier'
    );
    const s = String(v ?? '').toUpperCase();
    if (s.includes('AUTHENTICAMD')) return 'amd';
    if (s.includes('GENUINEINTEL')) return 'intel';
  } catch {
    /* 忽略 */
  }
  return 'unknown';
}

// ── PawnIO 驱动自检 ──
export interface PawnioStatus {
  exePresent: boolean;
  driverPresent: boolean;
}
export async function checkPawnio(): Promise<PawnioStatus> {
  const exe = join(PC_DIR, 'pawnio', 'YeManTdpCtl.exe');
  const exePresent = await fs.exists(exe);
  let driverPresent = false;
  try {
    const r = await shell.run('sc', ['query', 'pawnio']);
    driverPresent = r.exitCode === 0 && /RUNNING|STOPPED/.test(r.stdout);
  } catch {
    driverPresent = false;
  }
  return { exePresent, driverPresent };
}

// ── native 直读系统信息（sys.info IPC：全部 Win32/注册表 API，毫秒级）──
// 取代旧版为拿 CPU 名/物理核数/内存/AC-DC/目录而各自冷启动一个
// powershell -NoProfile（每次 600~1900ms）的路径。
// 兜底策略：native 命令不存在（旧 exe）或失败 → 回退旧 PowerShell 路径，保证兼容。
interface NativeSysInfo {
  cpuName: string;
  physicalCores: number;
  logicalProcs: number;
  totalMemoryBytes: number;
  acLine: number; // 0=DC 1=AC 255=未知
  powerMode: 'ac' | 'dc';
  commonStartup: string;
  userProfile: string;
}
async function nativeSysInfo(): Promise<NativeSysInfo | null> {
  try {
    const r = await invoke<NativeSysInfo>('sys.info', {});
    return r && typeof r === 'object' ? r : null;
  } catch {
    return null; // 旧 exe 无此命令 → 调用方走 PowerShell 兜底
  }
}
// proc.running：Toolhelp32 进程枚举（支持 "RTSS*" 前缀通配），失败返回 null
async function nativeProcRunning(names: string[]): Promise<Record<string, boolean> | null> {
  try {
    const r = await invoke<Record<string, boolean>>('proc.running', { names });
    return r && typeof r === 'object' ? r : null;
  } catch {
    return null;
  }
}

// ── 实时电源模式检测 ──
// 优先 native GetSystemPowerStatus（ACLineStatus 0=DC）；兜底 WMI Win32_Battery
// （BatteryStatus==1 放电中→DC；其余充电/满电/无电池→AC）。
export async function detectPowerMode(): Promise<'ac' | 'dc'> {
  const si = await nativeSysInfo();
  if (si && (si.powerMode === 'ac' || si.powerMode === 'dc')) return si.powerMode;
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      '(Get-CimInstance Win32_Battery).BatteryStatus -join ","',
    ]);
    const out = (r.stdout || '').trim();
    if (!out) return 'ac'; // 无电池（台式机）→ AC
    const discharging = out
      .split(',')
      .map((s) => s.trim())
      .some((s) => s === '1');
    return discharging ? 'dc' : 'ac';
  } catch {
    return 'ac';
  }
}

// ── CPU 名称检测（native 注册表 ProcessorNameString；兜底 Win32_Processor.Name） ──
export async function detectCpuName(): Promise<string> {
  const si = await nativeSysInfo();
  if (si && si.cpuName) return si.cpuName;
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      '(Get-CimInstance Win32_Processor).Name -join ", "',
    ]);
    const s = (r.stdout || '').trim();
    return s || 'Unknown CPU';
  } catch {
    return 'Unknown CPU';
  }
}

// ── TDP 下发（写 txt + 调 YeManTdpCtl.exe set <W> --vendor <vendor>） ──
// opts.apply=false 时仅存档不写硬件（如改动的是非当前电源模式）。
export interface SetTdpOpts {
  apply?: boolean;
  vendor?: Vendor;
}
export async function setTdp(
  mode: 'ac' | 'dc',
  watts: number,
  opts: SetTdpOpts = {}
): Promise<void> {
  const w = clampTdp(watts);
  await saveTdp(mode, w); // 写 tdp-{mode}.txt（HTA 的 defaultAC/DCFile）
  if (opts.apply) {
    const vendor = opts.vendor ?? (await detectVendor());
    if (vendor !== 'unknown') {
      const exe = join(PC_DIR, 'pawnio', 'YeManTdpCtl.exe');
      await shell.run(exe, ['set', String(w), '--vendor', vendor]);
    }
  }
}

// 手柄快捷：按当前 AC/DC 电源模式，将 TDP 增减 delta 并立即应用
export async function adjustTdp(delta: number): Promise<number | null> {
  const mode = await detectPowerMode().catch(() => null as 'ac' | 'dc' | null);
  if (!mode) return null;
  const cur = await readTdp(mode);
  if (cur === null) return null;
  const next = clampTdp(cur + delta);
  if (next === cur) return cur;
  await setTdp(mode, next, { apply: true });
  return next;
}

// 手柄快捷：按当前 AC/DC 电源模式，将 TDP 目标值设为指定值并立即应用
export async function setTdpCurrentMode(watts: number): Promise<number | null> {
  const mode = await detectPowerMode().catch(() => null as 'ac' | 'dc' | null);
  if (!mode) return null;
  const next = clampTdp(watts);
  await setTdp(mode, next, { apply: true });
  return next;
}

/* =========================================================================
 *  CPU 调度 / 电源方案（对齐 HTA pw_* 段）
 * ========================================================================= */
export const PW = {
  YEMAN: '1cb8b882-a900-4b9f-9bac-99d151e64441',
  WIN_SAVER: 'a1841308-3541-4fab-bc81-f71556f20b4a', // 节能 ≈ 最佳能效
  WIN_BAL: '381b4222-f694-41f0-9685-ff5bb260df2e', // 平衡
  WIN_HIGH: '8c5e7fda-e8bf-4a96-9a85-a6e23a8b102c', // 高性能 ≈ 最佳性能
  OV_EFF: '961cc777-2547-4f9d-8174-7d86181b8a7a', // 最佳能效 overlay
  OV_PERF: 'ded574b5-45a0-4f42-8737-46345c09c238', // 最佳性能 overlay
  OV_NONE: '00000000-0000-0000-0000-000000000000', // 无 overlay（平衡）
  SUB: '54533251-82be-4824-96c1-47b60b740d00',
  G_FREQ1: '75b0ae3f-bce0-45a7-8c89-c9611c25e100', // 最大处理器频率（唯一真实设置；AC/DC 仅用 ACSettingIndex/DCSettingIndex 子键区分，不存在 e101）
  G_TURBO: 'be337238-0d82-4146-a960-4f3749d470c7', // 处理器性能提升模式
  G_SCHED1: '36687f9e-e3a5-4dbf-b1dc-15eb381c6863', // 处理器性能节流策略（积极性）
  G_SCHED2: '36687f9e-e3a5-4dbf-b1dc-15eb381c6864',
  G_THROT: '3b04d4fd-1cc7-4f23-ab1c-d1337819c4bb', // 允许节流状态
  G_CORE: '7f2f5cfa-f10c-4823-b5e1-e93ae85f46b5', // 核心暂停（大小核）
  G_HETERO: '93b8b6dc-0698-4d1c-9ee4-0644e900c85d', // 异构调度策略
  G_SHORT: 'bae08b81-2d5e-4688-ad6a-13243356654b', // 短运行线程策略
  // 最小处理器状态联动（隐藏）：CPU 主频 → 3 个电源设置，AC/DC 各写
  // 最小处理器状态 联动组（3 个 GUID，均为"最小处理器状态"，注册表实测确认）：
  //   9964c = 主处理器最小状态；9964d = 能效等级1(EC1)最小状态；9964e = 能效等级2(EC2)最小状态
  //   全部跟随 CPU 主频按 freqToMinState() 联动写入同一值；均为 MIN 状态，不存在压低 CPU 上限的风险。
  G_MIN1: '893dee8e-2bef-41e0-89c6-b55d0929964c', // 主处理器最小状态 (PROCTHROTTLEMIN)
  G_MIN2: '893dee8e-2bef-41e0-89c6-b55d0929964d', // 能效等级1 最小状态 (EC1 min)
  G_MIN3: '893dee8e-2bef-41e0-89c6-b55d0929964e', // 能效等级2 最小状态 (EC2 min)
  // 处理器能量性能首选项策略（注册表实测 = Processor Power Efficiency Class 2 的 energy performance preference policy）
  // 随调度积极性联动写入（与 G_SCHED1/2 同值），隐藏不显示 UI。
  G_EPP2: '36687f9e-e3a5-4dbf-b1dc-15eb381c6865',
  // 核心暂停（Core Parking）活动核心数控制（微软标准 GUID，每台都有）：把最小核心数% 与最大核心数% 锁成同一值 = 锁死活动核心数
  G_MINCORE: '0cc5b647-c1df-4637-891a-dec35c318583', // 处理器性能核心暂停最小核心数 %
  G_MAXCORE: 'ea062031-0e34-4ff1-9b6d-eb1059334028', // 处理器性能核心暂停最大核心数 %
} as const;

export const SCHEMES = [
  { key: 'yeman', guid: PW.YEMAN, name: '野蛮系统电源' },
  { key: 'besteff', guid: PW.WIN_SAVER, name: '最佳能效' },
  { key: 'bal', guid: PW.WIN_BAL, name: '平衡' },
  { key: 'bestperf', guid: PW.WIN_HIGH, name: '最佳性能' },
] as const;
export type SchemeKey = (typeof SCHEMES)[number]['key'];

export interface PowerParams {
  acFreq: number; // MHz，0 = 不限制
  dcFreq: number;
  acTurbo: boolean; // true=开启(2) / false=关闭(0)
  dcTurbo: boolean;
  acAggr: number; // 0-100 积极性
  dcAggr: number;
}

export async function setActiveScheme(guid: string = PW.YEMAN): Promise<void> {
  const r = await shell.run('powercfg', ['/setactive', guid]);
  if (r.exitCode !== 0) {
    throw new Error(`powercfg /setactive ${guid} 失败：${(r.stderr || r.stdout).trim() || 'exit ' + r.exitCode}`);
  }
}
// 重新激活"当前活动方案"（电源按钮等修改跟随当前方案生效，不强制切回 YEMAN）
export async function reactivateCurrentScheme(): Promise<void> {
  await shell.run('powercfg', ['/setactive', 'SCHEME_CURRENT']);
}
export async function importScheme(file: string): Promise<void> {
  const r = await shell.run('powercfg', ['/import', file, PW.YEMAN]);
  if (r.exitCode !== 0) {
    // GUID 已存在属于良性情况（方案本来就在，无需重复导入）——只要方案确实在就不算失败；
    // 否则（文件损坏/无权限）才真正抛错，避免静默失败后 setactive 报“找不到方案”。
    if (!(await schemeExists(PW.YEMAN))) {
      throw new Error(`导入电源方案失败：${(r.stderr || r.stdout).trim() || 'exit ' + r.exitCode}`);
    }
  }
}
export async function schemeExists(guid: string): Promise<boolean> {
  // 直接以 GUID 问系统“该电源方案是否存在”。
  // ⚠️ 不再解析 `powercfg /list` 全量文本：其输出含 GBK 中文，`oemToUtf8` 解码后
  // 在某些环境下会干扰正则提取 GUID（实测运行时偶发漏判，导致“检测不到野蛮系统电源
  // 且无法恢复”）。`/query <guid>` 用退出码判定零歧义：存在 exit 0 / 不存在 exit 1。
  const r = await shell.run('powercfg', ['/query', guid]);
  return r.exitCode === 0;
}
// 确保野蛮系统电源方案存在：若被删除/缺失则从 YM.pow 重新导入。
// ⚠️ 永不删除任何电源方案；若方案已存在则原样跳过，绝不做任何覆盖/重置/删除。
export async function ensureYemanScheme(): Promise<void> {
  if (await schemeExists(PW.YEMAN)) return; // 已存在，直接返回，零副作用
  const pow = join(PC_DIR, 'YM.pow');
  if (!(await fs.exists(pow))) throw new Error('YM.pow 电源方案文件不存在，无法恢复野蛮系统电源');
  await importScheme(pow);
  // 二次确认确实已恢复，避免“导入静默失败”后 setactive 拿不到方案
  if (!(await schemeExists(PW.YEMAN))) {
    throw new Error('野蛮系统电源方案恢复失败：导入后仍未在系统电源列表中找到该方案');
  }
}
export async function setOverlay(guid: string): Promise<void> {
  const r = await shell.run('powercfg', ['/overlaysetactive', guid]);
  if (r.exitCode !== 0) {
    throw new Error(`powercfg /overlaysetactive ${guid} 失败：${(r.stderr || r.stdout).trim() || 'exit ' + r.exitCode}`);
  }
}
export async function setAcValueIndex(subGroup: string, setting: string, value: string, scheme?: string): Promise<RunResult> {
  return await shell.run('powercfg', ['/setacvalueindex', scheme ?? PW.YEMAN, subGroup, setting, value]);
}
export async function setDcValueIndex(subGroup: string, setting: string, value: string, scheme?: string): Promise<RunResult> {
  return await shell.run('powercfg', ['/setdcvalueindex', scheme ?? PW.YEMAN, subGroup, setting, value]);
}
export async function getActiveScheme(): Promise<string> {
  // 优先 native PowerGetActiveScheme API（毫秒级、纯 GUID、无中文文案解析）；
  // 兜底 powercfg /getactivescheme 子进程（旧 exe 兼容）。
  try {
    const g = await invoke<string>('power.activeScheme', {});
    if (typeof g === 'string' && /^[0-9a-fA-F-]{36}$/.test(g)) return g.toLowerCase();
  } catch {
    /* → powercfg 兜底 */
  }
  const r = await shell.run('powercfg', ['/getactivescheme']);
  const m = (r.stdout || '').match(/GUID:\s*([0-9a-fA-F\-]+)/);
  return m ? m[1].toLowerCase() : '';
}

// 切换电源方案：yeman=直接setactive；besteff/bestperf=先setactive平衡再叠加overlay
export async function switchScheme(key: SchemeKey): Promise<void> {
  if (key === 'yeman') {
    // 切换到野蛮系统电源前先确保方案存在，被删则从 C:\SOFT\YeMan\PowerControl\YM.pow 重新导入
    await ensureYemanScheme();
    await setActiveScheme(PW.YEMAN);
    // 偏好文件写入失败不影响已切换的电源（仅记录选择，绝不回滚/删除方案）
    try { await savePower('scheme', 'YeMan'); } catch { /* ignore */ }
    return;
  }
  if (key === 'bal') {
    await setActiveScheme(PW.WIN_BAL);
    await setOverlay(PW.OV_NONE);
    await savePower('scheme', 'Balanced');
    return;
  }
  const ov = key === 'besteff' ? PW.OV_EFF : PW.OV_PERF;
  const tag = key === 'besteff' ? 'BestEfficiency' : 'BestPerformance';
  await setActiveScheme(PW.WIN_BAL);
  await new Promise((r) => setTimeout(r, 400));
  await setOverlay(ov);
  await savePower('scheme', tag);
}

// 应用 CPU 调度参数（对齐 HTA pw_applyNow：setac/dcvalueindex 全量下发）
// ⚠️ 台式机无电池时 DC 写入会失败，已用 try-catch 静默忽略——不影响 AC 下发。
export async function applyPowerParams(p: PowerParams): Promise<void> {
  const acThrot = p.acFreq > 0 && p.acFreq <= 2000 ? 1 : 2;
  const dcThrot = p.dcFreq > 0 && p.dcFreq <= 2000 ? 1 : 2;
  const acTurbo = p.acTurbo ? 2 : 0;
  const dcTurbo = p.dcTurbo ? 2 : 0;
  const acSched = 100 - p.acAggr; // 滑块积极性 0-100 → 注册表(100 - 值)
  const dcSched = 100 - p.dcAggr;
  const S = PW.SUB;
  const run = (ac: boolean, g: string, v: number) =>
    ac ? setAcValueIndex(S, g, String(v)) : setDcValueIndex(S, g, String(v));
  // AC 写入（台式机必定成功）
  await run(true, PW.G_SCHED1, acSched);
  await run(true, PW.G_SCHED2, acSched);
  await run(true, PW.G_EPP2, acSched); // 处理器能量性能首选项策略(EC2) — 随积极性联动
  await run(true, PW.G_TURBO, acTurbo);
  await run(true, PW.G_FREQ1, p.acFreq); // 仅 e100 真实最大频率设置
  await run(true, PW.G_THROT, acThrot);
  // DC 写入（台式机无电池时会失败 → 静默忽略）
  try { await run(false, PW.G_SCHED1, dcSched); } catch { /* desktop no-battery */ }
  try { await run(false, PW.G_SCHED2, dcSched); } catch { /* desktop no-battery */ }
  try { await run(false, PW.G_EPP2, dcSched); } catch { /* desktop no-battery */ } // 能量性能首选项(EC2) DC
  try { await run(false, PW.G_TURBO, dcTurbo); } catch { /* desktop no-battery */ }
  try { await run(false, PW.G_FREQ1, p.dcFreq); } catch { /* desktop no-battery */ } // 仅 e100 真实最大频率设置
  try { await run(false, PW.G_THROT, dcThrot); } catch { /* desktop no-battery */ }

  // ── 隐藏联动：最小处理器状态（3 个 GUID，AC+DC 各写） ──
  // ① 默认跟随最大主频：freqToMinState()（0/不限制→50，0–5000 线性，≥5000→50 封顶）
  // ② 积极性 ≥90 时联动：最小CPU = max(积极性%, 最大主频派生值)。
  //    积极性≥90 恒大于封顶50%的派生值，故等效为 最小CPU = 积极性%（90→90%、100→100%），
  //    即「5GHz+积极性90 → 听积极性」；积极性<90 仅走最大主频派生。AC/DC 各算各的。
  // 与上方 G_THROT 隐藏联动同理；不展示 UI。
  const acMinState = Math.max(freqToMinState(p.acFreq), p.acAggr >= 90 ? p.acAggr : 0);
  const dcMinState = Math.max(freqToMinState(p.dcFreq), p.dcAggr >= 90 ? p.dcAggr : 0);
  const MIN_STATE_GUIDS = [PW.G_MIN1, PW.G_MIN2, PW.G_MIN3];
  for (const g of MIN_STATE_GUIDS) {
    await run(true, g, acMinState); // AC 必定成功
    try { await run(false, g, dcMinState); } catch { /* desktop no-battery */ }
  }
}

// CPU 主频(MHz) → 最小处理器状态 联动值（隐藏，无极线性映射）
//   0(不限制) → 50；≥5000 → 50：两端都封顶 50%，最小状态最高就是 50%，不允许 CPU 永不降频
//   0–5000：线性 freq/100（100→1, 1000→10, 3000→30, 4900→49），下限 1
export function freqToMinState(freqMhz: number): number {
  if (freqMhz <= 0) return 50; // 0 = 不限制 → 最小状态 50（CPU 仍可降到约一半频率）
  if (freqMhz >= 5000) return 50; // 高频段也封顶 50%，不让最小状态飙到 100%（避免 CPU 永不降频）
  return Math.round(freqMhz / 100); // 0–5000 线性（100→1 … 4900→49），下限 1
}

// 大小核心调度（对齐 HTA pw_applyMode）
//   大核为主=core4/hetero2/short2；仅大核=core3/hetero1/short1；仅小核=core2/hetero3/short3
export type CoreMode = 'big' | 'only-big' | 'only-small';
const CORE_MAP: Record<CoreMode, [number, number, number]> = {
  big: [4, 2, 2],
  'only-big': [3, 1, 1],
  'only-small': [2, 3, 3],
};
export async function setCoreMode(mode: CoreMode): Promise<void> {
  const [core, hetero, shortRun] = CORE_MAP[mode];
  const S = PW.SUB;
  // AC 写入（必定成功）
  await setAcValueIndex(S, PW.G_CORE, String(core));
  await setAcValueIndex(S, PW.G_HETERO, String(hetero));
  await setAcValueIndex(S, PW.G_SHORT, String(shortRun));
  // DC 写入（台式机无电池时静默失败）
  try { await setDcValueIndex(S, PW.G_CORE, String(core)); } catch { /* desktop no-battery */ }
  try { await setDcValueIndex(S, PW.G_HETERO, String(hetero)); } catch { /* desktop no-battery */ }
  try { await setDcValueIndex(S, PW.G_SHORT, String(shortRun)); } catch { /* desktop no-battery */ }
}

// AC/DC 分离的大小核心调度写入（满足 UI 双排控制）
export async function setCoreModeAc(mode: CoreMode): Promise<void> {
  const [core, hetero, shortRun] = CORE_MAP[mode];
  const S = PW.SUB;
  await setAcValueIndex(S, PW.G_CORE, String(core));
  await setAcValueIndex(S, PW.G_HETERO, String(hetero));
  await setAcValueIndex(S, PW.G_SHORT, String(shortRun));
}
export async function setCoreModeDc(mode: CoreMode): Promise<void> {
  const [core, hetero, shortRun] = CORE_MAP[mode];
  const S = PW.SUB;
  try { await setDcValueIndex(S, PW.G_CORE, String(core)); } catch { /* desktop no-battery */ }
  try { await setDcValueIndex(S, PW.G_HETERO, String(hetero)); } catch { /* desktop no-battery */ }
  try { await setDcValueIndex(S, PW.G_SHORT, String(shortRun)); } catch { /* desktop no-battery */ }
}
// 读取当前大小核心调度模式（AC 或 DC）；读取不到/非标准组合返回 null
export async function readCoreMode(ac: boolean): Promise<CoreMode | null> {
  const core = await readSchemeIndex(PW.G_CORE, '', ac);
  const hetero = await readSchemeIndex(PW.G_HETERO, '', ac);
  const shortRun = await readSchemeIndex(PW.G_SHORT, '', ac);
  if (core == null || hetero == null || shortRun == null) return null;
  for (const mode of Object.keys(CORE_MAP) as CoreMode[]) {
    const [c, h, s] = CORE_MAP[mode];
    if (c === core && h === hetero && s === shortRun) return mode;
  }
  return null;
}

// ── 活动核心数（Core Parking，对齐 调节CPU核心数量.md） ──
// 原理：把「最小核心数%」与「最大核心数%」锁成同一值 = 锁死活动核心数（min=max 锁定法）。
// 百分比 = 想要的活动核心数 × 100 ÷ 物理核心总数（去 SMT，整数除法）。1=仅 1 核，total=全开。
// 统一 AC/DC 写入（台式机无电池时 DC 静默忽略）；仅写当前野蛮方案（与其它电源参数一致）。
export async function readPhysicalCores(): Promise<number> {
  // 物理核心数 = 各插槽 NumberOfCores 之和（去 SMT/超线程）。
  // 例：9950X = 16 物理核（SMT 开时逻辑线程为 32，此处只取物理核）。
  // 优先 native GetLogicalProcessorInformation（毫秒级）；兜底 WMI。
  const si = await nativeSysInfo();
  if (si && Number.isFinite(si.physicalCores) && si.physicalCores >= 1) return si.physicalCores;
  // 返回纯数字（ASCII），不依赖中文解码，无乱码风险。
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      '(Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum',
    ]);
    const n = parseInt((r.stdout || '').trim(), 10);
    if (Number.isFinite(n) && n >= 1) return n;
  } catch {
    /* → 0 */
  }
  return 0; // 检测不到 → 0（UI 据此禁用滑块，绝不用默认值误写）
}

export async function setActiveCoreCount(count: number, total: number): Promise<void> {
  if (total < 1) return;
  const c = Math.max(1, Math.min(total, Math.round(count)));
  // 百分比下限 1（避免锁成 0 核导致系统冻结），上限 100（全核心）
  const pct = Math.max(1, Math.min(100, Math.round((c * 100) / total)));
  const S = PW.SUB;
  await setAcValueIndex(S, PW.G_MINCORE, String(pct));
  await setAcValueIndex(S, PW.G_MAXCORE, String(pct));
  try { await setDcValueIndex(S, PW.G_MINCORE, String(pct)); } catch { /* desktop no-battery */ }
  try { await setDcValueIndex(S, PW.G_MAXCORE, String(pct)); } catch { /* desktop no-battery */ }
}

export async function readActiveCoreCount(total: number): Promise<number | null> {
  if (total < 1) return null;
  // 仅读最小核心数%（AC，DC 读不到回退 AC）；若 min=max 锁定则它即活动核心数百分比
  const pct = (await readSchemeIndex(PW.G_MINCORE, '', true)) ?? (await readSchemeIndex(PW.G_MINCORE, '', false));
  if (pct == null) return null;
  const c = Math.max(1, Math.min(total, Math.round((pct * total) / 100)));
  return c;
}

// ── 超线程 / SMT（native 进程内 GetLogicalProcessorInformation 实时检测，合并进 CPU 页检测批）──
// 与「活动核心数」(Core Parking，运行时 powercfg) 正交：SMT 是启动层 bcdedit numproc，需重启生效。
export interface SmtInfo {
  liveOn: boolean; // 当前运行态（真实检测，免提权）
  configOn: boolean | null; // 下次启动态（null=未知/需管理员读取 BCD 失败）
  physicalCores: number; // 物理核心数（去 SMT）
  logicalProcs: number; // 逻辑处理器数（含超线程）
}
export async function readSmt(): Promise<SmtInfo | null> {
  try {
    const r = (await invoke('smt.get')) as any;
    if (r && typeof r === 'object') {
      const configOn = r.configOn === undefined || r.configOn === null ? null : !!r.configOn;
      return {
        liveOn: !!r.liveOn,
        configOn,
        physicalCores: Number(r.physicalCores) || 0,
        logicalProcs: Number(r.logicalProcs) || 0,
      };
    }
  } catch {
    /* 检测失败 → null，UI 不显示、绝不误写 */
  }
  return null;
}
export async function setSmt(on: boolean): Promise<{ ok: boolean; error?: string }> {
  try {
    const r = (await invoke('smt.set', { on })) as any;
    return { ok: !!(r && r.ok), error: r && r.error ? String(r.error) : undefined };
  } catch (e: any) {
    return { ok: false, error: e?.message ? String(e.message) : '调用失败' };
  }
}

// 读取当前野蛮方案的电源参数（注册表 HKLM\...\PowerSchemes\<YEMAN_GUID>\<SUB>\...）
// 只读 PW.YEMAN，不读活动方案——非野蛮方案的 CPU 调度参数可能不兼容/不可用
//
// ⚠️ 台式机无电池 → 电源方案没有 DCSettingIndex 注册表项（读写都会失败）。
// 策略：DC 读不到时回退到 AC 值；DC 写入失败时静默忽略（台式机不需要 DC）。
async function readSchemeIndex(guidSub: string, setting: string, ac: boolean, subGroup?: string): Promise<number | null> {
  // ⚠️ 关键：ACSettingIndex / DCSettingIndex 是 {guidSub} 这个 key 下的【值名】，不是子键！
  //   path 只拼到 {guidSub} 为止；值名作为第三参数传给 registry.read。
  //   native 层对 REG_DWORD 返回十进制整数（0xaf0→2800MHz, 0x12c0→4800MHz），无需再转进制。
  const sub = subGroup ?? PW.SUB;
  const path = `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${PW.YEMAN}\\${sub}\\${guidSub}`;
  const valueName = ac ? 'ACSettingIndex' : 'DCSettingIndex';
  try {
    const v = await registry.read('HKLM', path, valueName);
    // 值不存在 → native 返回 null（区别于真实值 0）
    if (v === null || v === undefined) {
      // DC 项在台式机上不存在 → 回退到 AC 值（台式机只有 AC 电源模式）
      if (!ac) return readSchemeIndex(guidSub, setting, true);
      return null;
    }
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  } catch {
    if (!ac) return readSchemeIndex(guidSub, setting, true);
    return null;
  }
}

export async function readPowerParams(): Promise<PowerParams | null> {
  try {
    // 只读 PW.YEMAN（野蛮方案）——不读活动方案
    const acFreq = (await readSchemeIndex(PW.G_FREQ1, '', true)) ?? 0;
    const dcFreq = (await readSchemeIndex(PW.G_FREQ1, '', false)) ?? 0;
    // 睿频：powercfg 值 0/1=关闭, 2=开启(推荐), 3=激进(AMD)
    const acTurboRaw = await readSchemeIndex(PW.G_TURBO, '', true);
    const dcTurboRaw = await readSchemeIndex(PW.G_TURBO, '', false);
    const acTurbo = acTurboRaw != null ? (acTurboRaw >= 2) : true; // 读不到默认开启
    const dcTurbo = dcTurboRaw != null ? (dcTurboRaw >= 2) : true;
    const acSchedReg = (await readSchemeIndex(PW.G_SCHED1, '', true)) ?? 0;
    const dcSchedReg = (await readSchemeIndex(PW.G_SCHED1, '', false)) ?? 0;
    return {
      acFreq: Math.max(0, acFreq),
      dcFreq: Math.max(0, dcFreq),
      acTurbo,
      dcTurbo,
      acAggr: 100 - Math.max(0, Math.min(100, acSchedReg)),
      dcAggr: 100 - Math.max(0, Math.min(100, dcSchedReg)),
    };
  } catch {
    return null;
  }
}

// 重制电源（六档，运行 TPD 下对应 VBS，VBS 内部静默调用 BAT 修改电源）
export const RESET_PROFILES = [
  { name: '🔥 Extreme极致性能', sub: '猛吃CPU-注意过热', path: 'C:\\SOFT\\YeMan\\PowerControl\\TPD\\Extreme.vbs' },
  { name: '⚡ Elite精睿性能', sub: '笔记本推荐', path: 'C:\\SOFT\\YeMan\\PowerControl\\TPD\\Elite.vbs' },
  { name: '🎮 Turbo高性能', sub: '掌机推荐', path: 'C:\\SOFT\\YeMan\\PowerControl\\TPD\\Turbo.vbs' },
  { name: '⚖️ Performance平衡', sub: 'SteamDeck推荐', path: 'C:\\SOFT\\YeMan\\PowerControl\\TPD\\Performance.vbs' },
] as const;
export async function runResetProfile(path: string): Promise<void> {
  // 用 cscript.exe（控制台模式）执行 VBS —— 不弹 GUI 对话框、无"内存资源不足"问题
  // VBS 内部 ws.Run "cmd /c ...bat", 0, True 静默调用 BAT 修改电源
  await shell.run('cscript.exe', ['//nologo', path]);
}

/* =========================================================================
 *  RTSS 监控锁帧（对齐 HTA rtss_* 段）
 * ========================================================================= */
const RTSS_DIR = 'C:\\Program Files (x86)\\RivaTuner Statistics Server';
const RTSS_GLOBAL = `${RTSS_DIR}\\Profiles\\Global`;
const RTSS_OVERLAY_CFG = `${RTSS_DIR}\\Plugins\\Client\\OverlayEditor.cfg`;
export const FPS_CEILINGS = [60, 90, 120, 200];
export const FPS_MIN = 20;
export const FPS_MAX_DEFAULT = 200;

export async function rtssRunning(): Promise<boolean> {
  // 优先 native Toolhelp32 枚举（毫秒级）；兜底 Get-Process 子进程（旧 exe 兼容）。
  const p = await nativeProcRunning(['RTSS']);
  if (p) return !!p['RTSS'];
  const r = await shell.run('powershell', [
    '-NoProfile',
    '-Command',
    '(Get-Process RTSS -EA 0).Count -gt 0',
  ]);
  return (r.stdout || '').trim().toLowerCase() === 'true';
}
export async function readRtssLimit(): Promise<number> {
  if (!(await fs.exists(RTSS_GLOBAL))) return 0;
  const txt = await fs.readTextFile(RTSS_GLOBAL);
  for (const line of txt.split(/\r?\n/)) {
    const m = line.match(/^Limit=(\d+)/);
    if (m) return parseInt(m[1], 10);
  }
  return 0;
}
export async function setRtssLimit(fps: number): Promise<void> {
  if (!(await fs.exists(RTSS_GLOBAL))) return;
  const txt = await fs.readTextFile(RTSS_GLOBAL);
  const out = txt
    .split(/\r?\n/)
    .map((l) => (l.match(/^Limit=\d+/) ? `Limit=${Math.max(0, Math.round(fps))}` : l))
    .join('\r\n');
  await fs.writeTextFile(RTSS_GLOBAL, out);
  // 重载配置（对齐 HTA：rundll32 RTSSHooks64.dll LoadProfile/SaveProfile/UpdateProfiles）
  await shell.run('rundll32', [`${RTSS_DIR}\\RTSSHooks64.dll`, 'LoadProfile']);
  await shell.run('rundll32', [`${RTSS_DIR}\\RTSSHooks64.dll`, 'SaveProfile']);
  await shell.run('rundll32', [`${RTSS_DIR}\\RTSSHooks64.dll`, 'UpdateProfiles']);
}

// 手柄快捷：RTSS 锁帧上限增减 delta（最低 FPS_MIN，最高 FPS_MAX_DEFAULT）
export async function adjustRtssLimit(delta: number): Promise<number | null> {
  const cur = await readRtssLimit();
  const base = cur < FPS_MIN ? FPS_MIN : cur;
  const next = Math.max(FPS_MIN, Math.min(FPS_MAX_DEFAULT, Math.round(base + delta)));
  if (next === cur) return cur;
  await setRtssLimit(next);
  return next;
}
// RTSS OSD 缩放（HTA 无此功能；RTSS 用 Profiles\Global [OSD] 段的 ZoomRatio 整数控制，每步 ±1）。
// RTSS 官方无百分比概念（Unwinder 确认），故派生 缩放% = ZoomRatio × 20（5→100%，步长即 20%）。
// 该值为 RTSS 运行中实时读取，写入后重载配置即可即时生效（无需重启 RTSS，优于样式切换）。
export const RTSS_ZOOM_MIN = 1; // ≈20%
export const RTSS_ZOOM_MAX = 12; // ≈240%
export async function readRtssZoom(): Promise<number> {
  if (!(await fs.exists(RTSS_GLOBAL))) return 5;
  const txt = await fs.readTextFile(RTSS_GLOBAL);
  let inOsd = false;
  for (const line of txt.split(/\r?\n/)) {
    if (line.startsWith('[')) inOsd = line.trim().toLowerCase() === '[osd]';
    else if (inOsd) {
      const m = line.match(/^ZoomRatio=(\d+)/i);
      if (m) return parseInt(m[1], 10);
    }
  }
  return 5; // 文件存在但缺键 → 默认 100%
}
export async function setRtssZoom(ratio: number): Promise<void> {
  const z = Math.max(RTSS_ZOOM_MIN, Math.min(RTSS_ZOOM_MAX, Math.round(ratio)));
  if (!(await fs.exists(RTSS_GLOBAL))) return;
  const lines = (await fs.readTextFile(RTSS_GLOBAL)).split(/\r?\n/);
  let inOsd = false;
  let done = false;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.startsWith('[')) {
      if (inOsd && !done) {
        lines.splice(i, 0, `ZoomRatio=${z}`);
        done = true;
        break;
      }
      inOsd = line.trim().toLowerCase() === '[osd]';
    } else if (inOsd && /^ZoomRatio=\d+/i.test(line)) {
      lines[i] = `ZoomRatio=${z}`;
      done = true;
    }
  }
  if (!done) lines.push(`ZoomRatio=${z}`);
  await fs.writeTextFile(RTSS_GLOBAL, lines.join('\r\n'));
  // 强制 RTSS 重载配置（同 setRtssLimit 的 rundll32 三连）：OSD 缩放在运行中的 RTSS 实时读取，重载后即时生效（无需重启）
  await shell.run('rundll32', [`${RTSS_DIR}\\RTSSHooks64.dll`, 'LoadProfile']);
  await shell.run('rundll32', [`${RTSS_DIR}\\RTSSHooks64.dll`, 'SaveProfile']);
  await shell.run('rundll32', [`${RTSS_DIR}\\RTSSHooks64.dll`, 'UpdateProfiles']);
}

export async function toggleRtss(on: boolean): Promise<void> {
  if (on) {
    // 用 cscript.exe 执行 VBS，避免 cmd/start "" 路径解析错误（曾导致 "找不到 '\\\\' 文件"）
    await shell.run('cscript.exe', ['//nologo', 'C:\\SOFT\\YeMan\\PowerControl\\YeManRTSSone.vbs']);
  } else {
    // 关监控 = 结束整个 RTSS 家族（主程序 + 钩子加载器 64/32 + 编码服务），避免 RTSSHooksLoader64.exe 残留
    await shell.run('cmd', [
      '/c',
      'taskkill /F /IM RTSS.exe /IM RTSSHooksLoader64.exe /IM RTSSHooksLoader32.exe /IM EncoderServer64.exe /IM EncoderServer.exe & taskkill /IM HWiNFO64.exe /F',
    ]);
  }
}
export type OverlayLayout = 'W' | 'L' | 'off';
const OVL_W = 'YeManOBS-W-1.ovl';
const OVL_L = 'YeManOBS-L-1.ovl';
const OVL_EMPTY = 'Empty.ovl';
export async function readOverlayLayout(): Promise<string> {
  if (!(await fs.exists(RTSS_OVERLAY_CFG))) return '';
  const txt = await fs.readTextFile(RTSS_OVERLAY_CFG);
  for (const line of txt.split(/\r?\n/)) {
    if (line.indexOf('Layout=') === 0) return line.replace('Layout=', '').trim();
  }
  return '';
}
export async function setOverlayLayout(layout: OverlayLayout): Promise<void> {
  if (!(await fs.exists(RTSS_OVERLAY_CFG))) return;
  const target = layout === 'W' ? OVL_W : layout === 'L' ? OVL_L : OVL_EMPTY;
  const txt = await fs.readTextFile(RTSS_OVERLAY_CFG);
  const lines = txt.split(/\r?\n/);
  let found = false;
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].indexOf('Layout=') === 0) {
      lines[i] = `Layout=${target}`;
      found = true;
    }
  }
  if (!found) lines.push(`Layout=${target}`);
  await fs.writeTextFile(RTSS_OVERLAY_CFG, lines.join('\r\n'));
  // OverlayEditor.dll 仅在插件加载时读取一次 Layout=，无法热重载；直接重启 RTSS 进程让插件
  // 重新加载（等价于手动在 RTSS 设置里把 OverlayEditor.dll 关掉再打开），使横版/竖版/关闭立即生效。
  // 不发送 rundll32 重载指令（那只重载 RTSS 核心配置，对 OverlayEditor 横/竖版无效且会导致渲染异常）。
  await restartRtss();
}

// RTSS 进程家族是否还有残留（主程序 RTSS.exe + 钩子加载器 RTSSHooksLoader* + 编码服务 EncoderServer*）。
// 仅查 RTSS.exe 会漏掉 RTSSHooksLoader64.exe —— 它常独立于主程序存活，造成"看似关了实则旧钩子/旧插件还在"的渲染 bug。
async function rtssFamilyAlive(): Promise<boolean> {
  // 优先 native Toolhelp32 枚举（* 结尾=前缀匹配，覆盖 RTSSHooksLoader64/32、EncoderServer64 等）。
  const p = await nativeProcRunning(['RTSS', 'RTSSHooksLoader*', 'EncoderServer*']);
  if (p) return Object.values(p).some(Boolean);
  // 兜底 Get-Process 子进程（旧 exe 兼容）
  const r = await shell.run('powershell', [
    '-NoProfile',
    '-Command',
    '(Get-Process -EA 0 | Where-Object { $_.Name -match "^(RTSS|RTSSHooksLoader|EncoderServer)" }).Count -gt 0',
  ]);
  return (r.stdout || '').trim().toLowerCase() === 'true';
}

// 直接重启 RTSS：结束并重新拉起**整个进程家族**，不杀 HWiNFO（保留共享内存数据源）。
// 启动方式必须与手动开关 YeManRTSSone.bat 完全一致：用 `start "" /B` 脱离式启动 + 设 CPU 亲和性 0xA0。
// 之前用 `Start-Process` 在该宿主进程上下文里无法可靠地把 RTSS 脱离存活（经常被收走 → RTSS 起不来 / 卡死），
// 而 BAT 用 `start "" /B` 就能稳定拉起。这里复用同一套做法（一次性重启，不带 BAT 的 11.5h 循环，避免叠加）。
async function restartRtss(): Promise<void> {
  const exe = `${RTSS_DIR}\\RTSS.exe`;
  if (!(await fs.exists(exe))) return;
  // 1. 若家族在跑，先一次性结束整个家族（主程序 + 钩子加载器 64/32 + 编码服务 64/32），避免旧钩子/旧插件残留导致渲染错乱
  if (await rtssFamilyAlive()) {
    await shell.run('cmd', [
      '/c',
      'taskkill /F /IM RTSS.exe /IM RTSSHooksLoader64.exe /IM RTSSHooksLoader32.exe /IM EncoderServer64.exe /IM EncoderServer.exe',
    ]);
    // 轮询直到整个家族真正退出（最多 ~4 秒）
    for (let i = 0; i < 40; i++) {
      if (!(await rtssFamilyAlive())) break;
      await new Promise<void>((r) => setTimeout(r, 100));
    }
  }
  // 2. 脱离式启动主程序（与 BAT 的 `start "" /B` 一致；绝不用 Start-Process，会被宿主进程收走导致 RTSS 起不来）。
  // 必须拆成多个参数：整行作为单个参数传会被 native 的 quote_windows_arg 转义内部引号，导致 cmd 解析错乱、弹窗“找不到 \\”。
  // 末尾 `> NUL 2>&1` 把 RTSS 的 std 重定向到 NUL —— 否则 RTSS 经 start 拉起后持有 shell.run 的管道写端，
  // 导致 ReadFile 等不到 EOF 而永久阻塞（前端界面卡死、但 RTSS 已正常启动）。现在管道仅由外层 cmd 持有、退出即 EOF。
  await shell.run('cmd', ['/c', 'start', '""', '/B', exe, '>', 'NUL', '2>&1']);
  // 3. 轮询确认主程序已拉起（最多 ~8 秒）
  for (let i = 0; i < 80; i++) {
    if (await rtssRunning()) break;
    await new Promise<void>((r) => setTimeout(r, 100));
  }
  // 4. 设 CPU 亲和性 0xA0（核心 5/7），与 BAT 一致，稳定 overlay 渲染
  await shell.run('powershell', [
    '-NoProfile',
    '-Command',
    'Get-Process RTSS -EA 0 | ForEach-Object { $_.ProcessorAffinity = 0xA0 }; Get-Process EncoderServer64 -EA 0 | ForEach-Object { $_.ProcessorAffinity = 0xA0 }; Get-Process RTSSHooksLoader64 -EA 0 | ForEach-Object { $_.ProcessorAffinity = 0xA0 }',
  ]);
}
export async function monitorOn(): Promise<boolean> {
  const l = await readOverlayLayout();
  return !!l && l !== '' && l !== OVL_EMPTY;
}

/* =========================================================================
 *  睡眠守护（Sleep Guard）
 *  后端由 native 壳实现；这里仅做语义化封装。
 *  可调参数持久化于 C:\SOFT\YeMan\PowerControl\Sleep\sleepguard.json
 * ========================================================================= */
export type SleepGuardMode = 'off' | 'custom';
export interface SleepGuardStatus {
  enabled: boolean;             // 总开关
  mode: SleepGuardMode;         // 总开关模式：关闭 / 自选
  suspended: number;            // 当前被冻结任务数
  pauseResume: boolean;         // 睡眠时暂停 + 唤醒自动恢复（绑定）
  sleepTdp: { mode: 'lock' | 'off'; watts: number }; // 入睡调低 TDP
}
export async function sleepGuardGet(): Promise<SleepGuardStatus> {
  return await invoke<SleepGuardStatus>('sleepGuard.get');
}
export async function sleepGuardSet(on: boolean): Promise<void> {
  await invoke('sleepGuard.set', { on });
}
export async function sleepGuardSetConfig(cfg: Partial<SleepGuardStatus>): Promise<void> {
  await invoke('sleepGuard.setConfig', cfg);
}
export async function sleepGuardRecoverAll(): Promise<{ resumed: number }> {
  return await invoke<{ resumed: number }>('sleepGuard.recoverAll');
}
export async function sleepGuardSuspendCurrent(): Promise<{ paused: boolean; pid?: number; name?: string }> {
  return await invoke<{ paused: boolean; pid?: number; name?: string }>('sleepGuard.suspendCurrent');
}

/* =========================================================================
 *  电源/开机启动（对齐 HTA power_* / toggle* 段）
 * ========================================================================= */
const PLAN_ID = '1cb8b882-a900-4b9f-9bac-99d151e64441';
const SUB_GUID = '4f971e89-eebd-4455-a8de-9e59040e7347'; // 电源按钮（CAPS）
const ITEM_GUID = '7648efa3-dd9c-4e3e-b566-50f929386280';

// 电源按钮循环。val 是 powercfg 实际写入注册表的【动作值】(十进制，powercfg 原样写入)。
// ⚠️ 用户实测指定映射：不操作=0 / 睡眠=1 / 休眠=2 / 关闭显示器=4（即注册表 0x00/0x01/0x02/0x04）。
//   注：0x01、0x04 并非 Windows 标准合法动作码（标准仅 0/2/3/6/8），写入后电源键行为由用户自测。
//   检测(getPowerBtnIdx)与写入(setPowerBtnIdx)统一以本表 val 为准——注册表值原样回映到 idx。
const POWER_BTN_QUEUE = [
  { val: '1', name: 'S3 睡眠到内存' }, // idx 0 → 写 1 (0x01)
  { val: '2', name: 'S4 休眠到硬盘' }, // idx 1 → 写 2 (0x02)
  { val: '0', name: '不操作' },        // idx 2 → 写 0 (0x00)
  { val: '4', name: '关闭显示器' },    // idx 3 → 写 4 (0x04)
] as const;
export type PowerBtnIdx = 0 | 1 | 2 | 3;
export function powerBtnName(idx: PowerBtnIdx): string {
  return POWER_BTN_QUEUE[idx].name;
}
// 电源按钮：读 PW.YEMAN（registry.read，与 CPU 主频同路径）
const PW_BTN_SUB = '4f971e89-eebd-4455-a8de-9e59040e7347';
const PW_BTN_ITEM = '7648efa3-dd9c-4e3e-b566-50f929386280';

export async function getPowerBtnIdx(isAC: boolean): Promise<PowerBtnIdx> {
  try {
    // 电源按钮在 PW_BTN_SUB（按钮和盖子）下，不是 PW.SUB（CPU 调度）
    const v = await readSchemeIndex(PW_BTN_ITEM, '', isAC, PW_BTN_SUB);
    if (v === null) return 2;
    const found = POWER_BTN_QUEUE.findIndex((q) => q.val === String(v));
    return (found >= 0 ? found : 2) as PowerBtnIdx;
  } catch {
    return 2;
  }
}
export async function setPowerBtnIdx(isAC: boolean, idx: PowerBtnIdx): Promise<void> {
  const setFn = isAC ? setAcValueIndex : setDcValueIndex;
  try {
    await setFn(PW_BTN_SUB, PW_BTN_ITEM, POWER_BTN_QUEUE[idx].val);
  } catch (e) {
    // 台式机无电池 → DC 电源按钮写入失败，静默忽略（不影响 AC）
    if (isAC) throw e; // AC 失败仍需抛出
    return; // DC 失败则静默跳过
  }
  // 注意：不再此处立即激活——由前端做 2 秒防抖重新激活（与 CPU 调度调节器一致），
  // 避免快速多次切换电源按钮时反复激活导致闪烁/卡顿。
}

// 休眠
// 权威信号优先级：
//   1) HKLM\...\Power\HibernateEnabled (REG_DWORD, 0=关闭 1=开启) —— Windows 官方开关
//   2) C:\hiberfil.sys 是否存在 —— OS 真实落地信号（关闭后文件被删除）
//   3) powercfg /a 中文输出（依赖 native OEM→UTF-8 修复）
// 本函数绝不抛异常（避免 UI 卡死）；但检测不到任何证据时返回 null（未知），
// 由 UI 显示"检测失败"而非擅自假定关闭——程序不做任何自主操作。
export async function isHibernateOff(): Promise<boolean | null> {
  // 方法1（首选）：HibernateEnabled
  try {
    const v = await registry.read('HKLM', 'SYSTEM\\CurrentControlSet\\Control\\Power', 'HibernateEnabled');
    if (v === 0 || v === '0') return true; // 明确关闭
    if (v === 1 || v === '1') return false; // 明确开启
  } catch {
    /* → fallback */
  }
  // 方法2：hiberfil.sys 落地文件
  try {
    const exists = await fs.exists('C:\\hiberfil.sys');
    return !exists; // 不存在 = 关闭
  } catch {
    /* → fallback */
  }
  // 方法3（后备）：powercfg /a 中文输出
  try {
    const r = await shell.run('powercfg', ['/a']);
    const c = (r.stdout || '') + (r.stderr || '');
    if (/尚未启用|休眠.*不可用|hibernate.*not\s+(available|enabled)/i.test(c)) return true;
    if (/休眠.*可用|hibernate\s+(available|enabled)/i.test(c)) return false;
  } catch {
    /* → fallback */
  }
  // 方法4（最后手段）：HiberFileSizePercent 仅表示"已配置文件大小"，
  // 不等于已启用；本机实测 HibernateEnabled=0 时它仍为 45，故仅作弱信号。
  try {
    const v = await registry.read('HKLM', 'SYSTEM\\CurrentControlSet\\Control\\Power', 'HiberFileSizePercent');
    const n = Number(v);
    if (Number.isFinite(n) && n > 0) return false; // 有配置大小 → 弱信号：可能开启
  } catch {
    /* ignore */
  }
  return null; // 全部无法判断 → 未知（绝不假设"关闭"，避免把"检测不到"伪装成确定状态/自主误写）
}
export async function setHibernate(on: boolean): Promise<RunResult> {
  const r1 = await shell.run('powercfg', on ? ['/hibernate', 'on'] : ['/hibernate', 'off']);
  if (on) {
    return await shell.run('powercfg', ['/hibernate', '/type', 'reduced']);
  }
  return r1;
}
export async function readHibernateSize(): Promise<number> {
  try {
    const v = await registry.read('HKLM', 'SYSTEM\\CurrentControlSet\\Control\\Power', 'HiberFileSizePercent');
    const n = Number(v);
    if (Number.isFinite(n) && n >= 30 && n <= 100) return n;
  } catch {
    /* 忽略 */
  }
  return 50;
}
export async function setHibernateSize(pct: number): Promise<void> {
  await shell.run('powercfg', ['/hibernate', '/type', 'reduced']);
  await shell.run('powercfg', ['/hibernate', '/size', String(Math.max(30, Math.min(100, pct)))]);
}

// 物理内存总量（GB，浮点）。用于"休眠文件大小预估 = 内存 × 休眠文件百分比"。
// 用 Win32_ComputerSystem.TotalPhysicalMemory（字节）→ 转 GB。
// 只读、绝不抛异常；失败返回 null（UI 回退到"已开启/已关闭"文案）。
export async function readTotalMemoryGB(): Promise<number | null> {
  // 优先 native GlobalMemoryStatusEx（毫秒级）；兜底 WMI。
  const si = await nativeSysInfo();
  if (si && Number.isFinite(si.totalMemoryBytes) && si.totalMemoryBytes > 0)
    return si.totalMemoryBytes / (1024 * 1024 * 1024);
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      '(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory',
    ]);
    const bytes = Number((r.stdout || '').trim());
    if (Number.isFinite(bytes) && bytes > 0) return bytes / (1024 * 1024 * 1024);
  } catch {
    /* 忽略 */
  }
  return null;
}

// FxSound：AllUsers 启动文件夹快捷方式
const FX_LINK = 'C:\\Program Files\\FxSound LLC\\FxSound\\FxSound.exe';
// 公共启动目录：优先 native SHGetKnownFolderPath；兜底 PowerShell（旧 exe 兼容）
async function commonStartupDir(): Promise<string> {
  const si = await nativeSysInfo();
  if (si && si.commonStartup) return si.commonStartup;
  const r = await shell.run('powershell', [
    '-NoProfile',
    '-Command',
    '[Environment]::GetFolderPath("CommonStartup")',
  ]);
  return (r.stdout || '').trim();
}
export async function fxExists(): Promise<boolean> {
  try {
    const dir = await commonStartupDir();
    if (!dir) return false;
    return fs.exists(`${dir}\\FxSound.lnk`);
  } catch {
    return false;
  }
}
export async function fxSet(on: boolean): Promise<void> {
  const startDir = await commonStartupDir();
  const linkPath = `${startDir}\\FxSound.lnk`;
  if (on) {
    await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      `$s=(New-Object -ComObject WScript.Shell).CreateShortcut('${linkPath}');$s.TargetPath='${FX_LINK}';$s.WorkingDirectory='C:\\Program Files\\FxSound LLC\\FxSound';$s.Description='FxSound 音质优化';$s.Save()`,
    ]);
  } else {
    if (await fs.exists(linkPath)) await shell.run('cmd', ['/c', `del /F /Q "${linkPath}"`]);
  }
}

// Joyxoff：HKCU\...\Run
const JOY_KEY = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run';
export async function joyExists(): Promise<boolean> {
  try {
    const v = await registry.read('HKCU', 'Software\\Microsoft\\Windows\\CurrentVersion\\Run', 'Joyxoff');
    return !!v && String(v).toLowerCase().includes('joyxoff.exe');
  } catch {
    return false;
  }
}
export async function joySet(on: boolean): Promise<void> {
  if (on) {
    await registry.write('HKCU', 'Software\\Microsoft\\Windows\\CurrentVersion\\Run', 'Joyxoff', 'C:\\SOFT\\Joyxoff\\Joyxoff.exe');
  } else {
    await shell.run('reg', ['delete', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run', '/v', 'Joyxoff', '/f']);
  }
}

// AMD-395 前置检测：需 3DMark.exe
export async function threeMarkExists(): Promise<boolean> {
  return fs.exists('C:\\SOFT\\3DMark\\3DMark.exe');
}

// 旋转检测（HKLM\...\AutoRotation\Enable）
export async function getRotationEnabled(): Promise<boolean | null> {
  try {
    const v = await registry.read('HKLM', 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AutoRotation', 'Enable');
    const n = Number(v);
    return Number.isFinite(n) ? n === 1 : null;
  } catch {
    return null;
  }
}

// Windows 任务栏搜索状态
export async function searchState(): Promise<'hidden' | 'trimmed' | 'full'> {
  const sysDir = 'C:\\Windows\\SystemApps\\MicrosoftWindows.Client.CBS_cw5n1h2txyewy';
  if (!(await fs.exists(sysDir))) return 'hidden';
  if (!(await fs.exists(`${sysDir}\\SearchHost.exe`))) return 'trimmed';
  return 'full';
}
export async function openSearchFolder(): Promise<void> {
  await shell.run('explorer.exe', ['C:\\SOFT\\精简掉的系统文件\\SearchHost']);
}

/* =========================================================================
 *  Steam 大屏（对齐 HTA steam_* 段）
 * ========================================================================= */
const STEAM_DIR = 'C:\\SOFT\\YeMan\\PowerControl\\YeManSteam';
export const STEAM_ADDONS = [
  { key: 'steamcss', name: 'Steam 美化（CSSLoader）', exe: 'C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe', url: '' },
  { key: 'steamnet', name: '网络加速（steamcommunity）', exe: 'C:\\SOFT\\steamcommunity\\steamcommunity_302.cli.exe', url: '' },
  { key: 'steamscale', name: '小黄鸭缩放插帧（Lossless Scaling）', exe: 'C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe', url: '' },
  { key: 'steamcheat', name: '游戏修改器合集（Game-Cheats-Manager）', exe: 'C:\\SOFT\\Game Cheats Manager\\Game Cheats Manager.exe', url: 'https://github.com/dyang886/Game-Cheats-Manager' },
  { key: 'steamspeed', name: '游戏变速器（OpenSpeedy）', exe: 'C:\\SOFT\\OpenSpeedy\\openspeedy.exe', url: '' },
] as const;
export type SteamAddonKey = (typeof STEAM_ADDONS)[number]['key'];
function addonTxtName(exe: string): string {
  const base = exe.replace(/^.*[\\/]/, '').replace(/\.[^.]+$/, '');
  return `${base}.txt`;
}
function addonTxtPath(key: SteamAddonKey): string {
  const cfg = STEAM_ADDONS.find((a) => a.key === key);
  const exe = cfg ? cfg.exe : key;
  return `${STEAM_DIR}\\${addonTxtName(exe)}`;
}
export async function steamAddonExists(key: SteamAddonKey): Promise<boolean> {
  return fs.exists(addonTxtPath(key));
}
export async function steamAddonSet(key: SteamAddonKey, on: boolean): Promise<void> {
  const cfg = STEAM_ADDONS.find((a) => a.key === key);
  const p = addonTxtPath(key);
  if (on) {
    if (!cfg) throw new Error('未知联动项');
    await fs.writeTextFile(p, cfg.exe);
  } else {
    // 用 PowerShell Remove-Item 可靠删除：cmd del 在含空格路径/WebView2 的 shell 调用下容易解析失败，
    // 导致"反选关不掉"——用户手动删可以，但程序取消勾选删不掉对应 txt。
    await shell.run('powershell', ['-NoProfile', '-Command', `Remove-Item -LiteralPath '${p.replace(/'/g, "''")}' -Force -ErrorAction SilentlyContinue`]);
  }
}
export async function steamExeExists(key: SteamAddonKey): Promise<boolean> {
  const cfg = STEAM_ADDONS.find((a) => a.key === key);
  return cfg ? fs.exists(cfg.exe) : false;
}

// .earlystart 主开关
export async function steamEarlyStartFile(): Promise<string> {
  // 优先 native USERPROFILE 直读；兜底 PowerShell（旧 exe 兼容）
  const si = await nativeSysInfo();
  if (si && si.userProfile) return `${si.userProfile}\\.earlystart`;
  const r = await shell.run('powershell', ['-NoProfile', '-Command', '$env:USERPROFILE + "\\.earlystart"']);
  return (r.stdout || '').trim() || 'C:\\Users\\DaVe\\.earlystart';
}
export const STEAM_EARLYSTART_CONTENT = '"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
export async function steamMasterOn(): Promise<boolean> {
  const f = await steamEarlyStartFile();
  if (!(await fs.exists(f))) return false;
  const c = (await fs.readTextFile(f)).replace(/\s+/g, '');
  return c.length > 0;
}
export async function steamMasterSet(on: boolean): Promise<void> {
  const f = await steamEarlyStartFile();
  await fs.writeTextFile(f, on ? STEAM_EARLYSTART_CONTENT + '\r\n' : '');
}
export async function steamRunning(): Promise<boolean> {
  // 优先 native Toolhelp32 枚举；兜底 Get-Process（旧 exe 兼容）
  const p = await nativeProcRunning(['steam']);
  if (p) return !!p['steam'];
  const r = await shell.run('powershell', ['-NoProfile', '-Command', "((Get-Process steam -EA 0).Count -gt 0)"]);
  return (r.stdout || '').trim().toLowerCase() === 'true';
}
export async function launchSteam(): Promise<void> {
  // 用 cscript.exe 执行 VBS（同 RTSS 热重载/重制电源，避免 cmd/start "" 路径解析错误）
  await shell.run('cscript.exe', ['//nologo', 'C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.vbs']);
}
