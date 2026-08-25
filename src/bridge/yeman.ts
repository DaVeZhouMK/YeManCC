// yeman.ts — 语义化后端桥层（强强壳 shell.run / fs 封装）
//
// 所有 powercfg GUID、任务计划名、bat/vbs/ps1 路径、厂商识别逻辑都收在这里，
// 前端只调语义化方法：setTdp('ac', 200) / setRtssLimit(60) / toggleTask('任务名', true) / ...
//
// 程序控制配置由本桥统一记录，TDP/FPS 的用户值不再依赖 PowerControl 下的 txt。
// 任务计划只识别状态、不解析内容（schtasks 创建，/Query 判断存在即=开关状态）。
import { fs, shell, registry, rtss, tdpDaemon, powerLifecycle, systemHibernate, type RunResult, type HibernateState } from './api';
import { invoke } from './ipc';
import { readSettingsSection, saveSettingsSection, setSettingsDirectory } from './settingsRepository';

// ── 可配置根目录（自测时可指向临时目录） ──
let PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';

export function setPowerControlDir(dir: string): void {
  PC_DIR = dir.replace(/\//g, '\\');
  setSettingsDirectory(PC_DIR);
}
export function getPowerControlDir(): string {
  return PC_DIR;
}
function join(...parts: string[]): string {
  return parts.join('\\').replace(/\//g, '\\');
}

// ── TDP 常量（对齐 HTA：TDP_CEILINGS / TDP_MIN） ──
// 上限档位保持 200W（300 为错误值，已回退；性能调度编辑面板与 TDP 功耗页面上限一致）。
export const TDP_CEILINGS = [20, 35, 55, 75, 120, 200];
export const TDP_MIN = 2;
export const TDP_MAX = 200;
export function clampTdp(w: number): number {
  return Math.max(TDP_MIN, Math.min(TDP_MAX, Math.round(w)));
}
// 给定 savedVal 求对应的上限档（>= 的最小 ceiling）
export function smallestCeiling(val: number): number {
  for (const c of TDP_CEILINGS) if (c >= val) return c;
  return TDP_CEILINGS[TDP_CEILINGS.length - 1];
}

// ── 当前仍支持的任务计划（旧 AC/DC TDP 与 AC/DC 锁帧任务已移除）──
export const TASK_FOLDER = '野蛮优化整合系统';
export const BOOT_CONTROL_CENTER_TASK = '野蛮控制中心-开机启动';
export const BOOT_RTSS_TASK = '监控-开机启动监控锁帧软件RTSS';
export const BOOT_MIRROR_CHANGED_EVENT = 'boot-mirror:changed';
export interface TaskDef {
  name: string;
  trigger: string;
  asset: string;
  taskPath?: string;
  xml?: string; // XML 模板相对 PowerControl 的路径（缺省=无模板，仅能删除/查询）
}
export const TASKS: TaskDef[] = [
  { name: 'Steamcommunity_302', trigger: '开机', asset: 'C:\\SOFT\\steamcommunity\\steamcommunity_302.cli.exe', xml: 'Steamcommunity_302.xml', taskPath: 'Steamcommunity_302' },
  { name: BOOT_RTSS_TASK, trigger: '开机', asset: 'YeManRTSS.bat', xml: '监控-开机启动监控锁帧软件RTSS.xml' },
  { name: 'Xbox大屏游戏模式', trigger: '开机', asset: 'YeManSteam.bat', xml: 'Xbox大屏游戏模式.xml' },
  { name: '桌面模式-开机设置为桌面模式', trigger: '开机', asset: '(内置)', xml: '桌面模式-开机设置为桌面模式.xml' },
  { name: '节能-能源之星', trigger: '开机', asset: 'EnergyStar.vbs', xml: '节能-能源之星.xml' },
  { name: '内存-开机自动内存清理并关闭', trigger: '开机', asset: 'MG-AUTO\\清理内存.bat', xml: '内存-开机自动内存清理并关闭.xml' },
  { name: BOOT_CONTROL_CENTER_TASK, trigger: '开机', asset: 'YeManCC.exe', xml: '野蛮控制中心-开机启动.xml' },
];
export function getTaskDef(name: string): TaskDef | undefined {
  return TASKS.find((t) => t.name === name);
}

// ── 程序控制配置：TDP 最大值与 FPS 帧率上限统一由程序记录 ──
function controlConfigPath(): string {
  return join(PC_DIR, 'control-config.json');
}
interface ControlConfig {
  tdpMax?: number;
  fpsLimit?: number;
}
let controlConfigCache: ControlConfig | null = null;
let controlConfigLoad: Promise<ControlConfig> | null = null;
let controlConfigWrite: Promise<void> = Promise.resolve();

async function readControlConfig(): Promise<ControlConfig> {
  if (controlConfigCache) return controlConfigCache;
  if (!controlConfigLoad) {
    controlConfigLoad = (async () => {
      try {
        const tdp = await readSettingsSection<any>('tdp');
        const parsed = tdp as ControlConfig;
        // JSON 内容为字面 null / 非对象时视为损坏 → 抛出让 catch 走迁移/兜底路径，
        // 避免缓存被置 null 后每次调用都重新读盘（2026-08-05 修复）。
        if (!parsed || typeof parsed !== 'object') throw new Error('control-config.json 内容无效');
        controlConfigCache = parsed;
        return parsed;
      } catch {
        // control-config.json 尚不存在：一次性迁移旧的 tdp.txt / FPS-ac.txt（只读，不写回 txt）
        const migrated: ControlConfig = {};
        try {
          if (await fs.exists(join(PC_DIR, 'tdp.txt'))) {
            const t = Number((await fs.readTextFile(join(PC_DIR, 'tdp.txt'))).trim());
            if (Number.isFinite(t)) migrated.tdpMax = clampTdp(t);
          }
        } catch { /* 忽略迁移失败 */ }
        try {
          if (await fs.exists(join(PC_DIR, 'FPS-ac.txt'))) {
            const f = Number((await fs.readTextFile(join(PC_DIR, 'FPS-ac.txt'))).trim());
            if (Number.isFinite(f)) migrated.fpsLimit = Math.max(0, Math.round(f));
          }
        } catch { /* 忽略迁移失败 */ }
        const fallback: ControlConfig = migrated.tdpMax == null && migrated.fpsLimit == null
          ? { tdpMax: 75, fpsLimit: 0 }
          : migrated;
        controlConfigCache = fallback;
        return fallback;
      } finally {
        controlConfigLoad = null;
      }
    })();
  }
  return controlConfigLoad;
}
async function mutateControlConfig(patch: Partial<ControlConfig>): Promise<void> {
  const run = controlConfigWrite.then(async () => {
    const current = await readControlConfig();
        const next = { ...current, ...patch };
        await saveSettingsSection('tdp', next as any);
    // 只有磁盘提交成功后才更新缓存，避免本次运行与重启后的配置分叉。
    controlConfigCache = next;
  });
  // 队列吞错（防止单次失败卡死后续写入），但返回值 run 仍向调用方抛出写入失败，
  // 让 UI 能感知「保存失败」而不是假成功；失败后清缓存，下次读取强制重新读盘。
  controlConfigWrite = run.catch(() => { controlConfigCache = null; });
  return run;
}
export async function saveTdp(_mode: 'ac' | 'dc', watts: number): Promise<void> {
  await mutateControlConfig({ tdpMax: clampTdp(watts) });
}
export async function readTdp(_mode: 'ac' | 'dc'): Promise<number | null> {
  const cfg = await readControlConfig();
  return cfg.tdpMax == null ? null : clampTdp(cfg.tdpMax);
}
export async function saveFps(_mode: 'ac' | 'dc', fps: number): Promise<void> {
  await mutateControlConfig({ fpsLimit: Math.max(0, Math.round(fps)) });
}
export async function readFps(_mode: 'ac' | 'dc'): Promise<number | null> {
  const cfg = await readControlConfig();
  return cfg.fpsLimit == null ? null : Math.max(0, Math.round(cfg.fpsLimit));
}
// 电源记忆已并入统一配置。保留这个兼容入口是为了不改变页面调用方，
// 但不再生成 Power.txt，避免升级后重新出现旧配置文件。
let powerWriteQueue: Promise<void> = Promise.resolve();
export function savePower(key: string, value: string): Promise<void> {
  const run = powerWriteQueue.then(async () => {
    if (key !== 'scheme') return;
    // All current callers already commit the structured scheme immediately
    // before this compatibility hook. Do not write the retired flat file.
    await readSettingsSection('power');
    void value;
  });
  powerWriteQueue = run.catch(() => {});
  return run;
}
export async function readPowerRaw(): Promise<string> {
  const remembered = await readRememberedPowerScheme();
  if (!remembered) return '';
  const scheme = remembered.mode === 'manual'
    ? remembered.selectedKey === 'besteff' ? 'BestEfficiency'
      : remembered.selectedKey === 'bestperf' ? 'BestPerformance'
        : 'Balanced'
    : 'YeMan';
  return `scheme=${scheme}`;
}

// ── 任务计划：统一读取、导入和删除 ──
// 开关唯一依据是任务是否存在：存在=开，不存在=关。
// 不读取 Enabled/Disabled，也不把任务运行状态、空闲策略或电源策略当作开关状态。
const TASK_FILE_ROOT = 'C:\\Windows\\System32\\Tasks';
function taskPath(name: string): string {
  return getTaskDef(name)?.taskPath ?? `${TASK_FOLDER}\\${name}`;
}

const POWER_SCHEME_CONFIG_FILE = 'yeman-power-scheme.json';
type PowerSchemeMode = 'yeman' | 'auto' | 'manual';
interface RememberedPowerScheme {
  version: 2;
  guid: string;
  name: string;
  mode: PowerSchemeMode;
  selectedKey?: SchemeKey;
  selectedGuid?: string;
  remembered: true;
}
let powerSchemeEnsureQueue: Promise<void> = Promise.resolve();
let schemeProtectionSuspendUntil = 0;
let schemeProtectionSuspendMode: 'yeman' | 'auto' = 'yeman';

function powerSchemeConfigPath(): string {
  return join(PC_DIR, POWER_SCHEME_CONFIG_FILE);
}

function isGuid(value: unknown): value is string {
  return typeof value === 'string' && /^[0-9a-fA-F-]{36}$/.test(value);
}

async function readRememberedPowerScheme(): Promise<RememberedPowerScheme | null> {
  try {
    const power = await readSettingsSection<any>('power');
    const parsed = (power.scheme || {}) as Partial<RememberedPowerScheme>;
    if (!isGuid(parsed.guid) || parsed.remembered !== true) return null;
    if (parsed.version === 2 && (parsed.mode === 'yeman' || parsed.mode === 'auto' || parsed.mode === 'manual')) {
      return {
        version: 2,
        guid: parsed.guid.toLowerCase(),
        name: typeof parsed.name === 'string' ? parsed.name : 'YeMan',
        mode: parsed.mode,
        selectedKey: parsed.selectedKey,
        selectedGuid: isGuid(parsed.selectedGuid) ? parsed.selectedGuid.toLowerCase() : undefined,
        remembered: true,
      };
    }
    // v1 was written only for YeMan, so it remains a protected YeMan state.
    if (Number(parsed.version) === 1 && parsed.name === 'YeMan') {
      return { version: 2, guid: parsed.guid.toLowerCase(), name: 'YeMan', mode: 'yeman', remembered: true };
    }
    return null;
  } catch {
    return null;
  }
}

export async function rememberYemanScheme(mode: 'yeman' | 'auto' = 'yeman'): Promise<void> {
  const next: RememberedPowerScheme = { version: 2, guid: PW.YEMAN, name: 'YeMan', mode, remembered: true };
  await saveSettingsSection('power', { scheme: next });
}

async function rememberManualScheme(key: SchemeKey, guid: string): Promise<void> {
  const next: RememberedPowerScheme = {
    version: 2,
    guid: guid.toLowerCase(),
    name: SCHEMES.find((item) => item.key === key)?.name ?? key,
    mode: 'manual',
    selectedKey: key,
    selectedGuid: guid.toLowerCase(),
    remembered: true,
  };
  await saveSettingsSection('power', { scheme: next });
}

// 仅由启动、电源/唤醒、自动优化和档位切换等低频事件调用。
export function ensureRememberedYemanSchemeActive(): Promise<void> {
  const run = powerSchemeEnsureQueue.then(async () => {
    await ensureYemanScheme();
    const remembered = await readRememberedPowerScheme();
    // This function is called only by automation-owned paths. Manual recovery
    // uses reconcileRememberedPowerScheme instead.
    if (!remembered || remembered.guid !== PW.YEMAN || (remembered.mode !== 'yeman' && remembered.mode !== 'auto')) {
      await rememberYemanScheme('auto');
    } else if (remembered.mode !== 'auto') {
      await rememberYemanScheme('auto');
    }
    if (await getActiveScheme() === PW.YEMAN) return;
    await setActiveScheme(PW.YEMAN);
    if (await getActiveScheme() !== PW.YEMAN) throw new Error('切换野蛮系统电源后仍未生效');
  });
  powerSchemeEnsureQueue = run.catch(() => {});
  return run;
}

// Reconcile a Windows power-scheme broadcast against the persisted owner.
// The broadcast has no reliable source field, so the last YeManCC selection is
// the only stable way to distinguish an expected manual scheme from takeover.
export function reconcileRememberedPowerScheme(): Promise<'yeman' | 'auto' | 'manual'> {
  const run = powerSchemeEnsureQueue.then(async () => {
    if (Date.now() < schemeProtectionSuspendUntil) return schemeProtectionSuspendMode;
    const remembered = await readRememberedPowerScheme();
    if (!remembered || remembered.mode === 'yeman' || remembered.mode === 'auto') {
      await ensureYemanScheme();
      if (!remembered || remembered.guid !== PW.YEMAN) {
        await rememberYemanScheme('yeman');
      }
      if (await getActiveScheme() !== PW.YEMAN) await setActiveScheme(PW.YEMAN);
      return remembered?.mode === 'auto' ? 'auto' : 'yeman';
    }

    const expected = remembered.selectedGuid ?? remembered.guid;
    if ((await getActiveScheme()).toLowerCase() === expected.toLowerCase()) return 'manual';
    const key = remembered.selectedKey;
    if (key === 'besteff' || key === 'bestperf' || key === 'bal') {
      await setActiveScheme(PW.WIN_BAL);
      await setOverlay(key === 'besteff' ? PW.OV_EFF : key === 'bestperf' ? PW.OV_PERF : PW.OV_NONE);
    } else {
      await setActiveScheme(expected);
    }
    return 'manual';
  });
  powerSchemeEnsureQueue = run.then(() => {}).catch(() => {});
  return run;
}

export async function rebuildYemanScheme(mode: 'yeman' | 'auto' = 'yeman'): Promise<void> {
  const run = powerSchemeEnsureQueue.then(async () => {
    // powercfg broadcasts every intermediate scheme change. Keep the guard
    // quiet until the old scheme is deleted and the new YM.pow is active.
    schemeProtectionSuspendMode = mode;
    schemeProtectionSuspendUntil = Date.now() + 15000;
    try {
      if (await getActiveScheme() !== PW.WIN_BAL) await setActiveScheme(PW.WIN_BAL);
      if (await schemeExists(PW.YEMAN)) {
        const deleted = await shell.run('powercfg', ['/delete', PW.YEMAN]);
        if (deleted.exitCode !== 0 && await schemeExists(PW.YEMAN)) {
          throw new Error(`删除旧野蛮电源失败：${(deleted.stderr || deleted.stdout).trim() || `exit ${deleted.exitCode}`}`);
        }
      }
      const pow = join(PC_DIR, 'YM.pow');
      if (!(await fs.exists(pow))) throw new Error('YM.pow 电源方案文件不存在');
      const imported = await shell.run('powercfg', ['/import', pow, PW.YEMAN]);
      if (imported.exitCode !== 0 || !(await schemeExists(PW.YEMAN))) {
        throw new Error(`重新导入野蛮电源失败：${(imported.stderr || imported.stdout).trim() || `exit ${imported.exitCode}`}`);
      }
      await setActiveScheme(PW.YEMAN);
      await rememberYemanScheme(mode);
      try { await savePower('scheme', 'YeMan'); } catch { /* scheme is already active */ }
    } finally {
      // Leave a short drain window for delayed Windows broadcasts.
      schemeProtectionSuspendUntil = Date.now() + 1500;
    }
  });
  powerSchemeEnsureQueue = run.catch(() => {});
  await run;
}
function taskPathCandidates(name: string): string[] {
  const canonical = taskPath(name);
  const paths = [canonical];
  // 兼容旧版本曾将中文目录任务误导入任务计划根目录的情况。
  // 创建始终使用 canonical，关闭时清理两处，避免旧任务继续运行。
  if (canonical.startsWith(`${TASK_FOLDER}\\`)) paths.push(name);
  return [...new Set(paths)];
}
function taskFilePath(path: string): string {
  return `${TASK_FILE_ROOT}\\${path}`;
}
async function taskDefinedAtPath(path: string): Promise<boolean> {
  try {
    const r = await shell.run('schtasks', ['/Query', '/TN', path, '/FO', 'CSV', '/V']);
    if (r.exitCode === 0) return true;
  } catch {
    // 文件检查作为权限不足或 schtasks 不可用时的后备。
  }
  try { return await fs.exists(taskFilePath(path)); } catch { return false; }
}
async function taskExistsAtPath(path: string): Promise<boolean> {
  return taskDefinedAtPath(path);
}
export async function taskExists(name: string): Promise<boolean> {
  for (const path of taskPathCandidates(name)) {
    if (await taskExistsAtPath(path)) return true;
  }
  return false;
}
export async function deleteTask(name: string): Promise<boolean> {
  let failed = false;
  for (const path of taskPathCandidates(name)) {
    if (!(await taskDefinedAtPath(path))) continue;
    try {
      const r = await shell.run('schtasks', ['/Delete', '/TN', path, '/F']);
      if (r.exitCode !== 0) failed = true;
    } catch {
      failed = true;
    }
  }
  if (failed) return false;
  for (const path of taskPathCandidates(name)) {
    if (await taskDefinedAtPath(path)) return false;
  }
  return true;
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
  // 任务底层文件预检：仅当 TaskDef.asset 给出绝对路径时验证（相对路径可能由 schtasks 解析时定位，避免误判）
  // 例如 AMD395 任务依赖 C:\SOFT\3DMark\YeMan-3DMark.bat，若该 bat 不存在则 schtasks 创建的任务在运行时仍会失败，
  // 故在此处直接给出明确错误，避免用户看到一个晦涩的 "创建任务失败（需管理员）" 却找不到根因。
  if (def.asset) {
    const assetPath = resolveAssetAbsolutePath(def.asset);
    if (assetPath && !(await fs.exists(assetPath))) {
      throw new Error(`任务底层文件不存在: ${assetPath}`);
    }
  }
  const r = await shell.run('schtasks', ['/Create', '/TN', taskPath(name), '/XML', xml, '/F']);
  if (r.exitCode !== 0) return false;
  return taskExistsAtPath(taskPath(name));
}
// 从 TaskDef.asset 中提取绝对路径：取第一个空白符前的 token；只有显式绝对路径（C:\ 或 \\UNC\）才返回
function resolveAssetAbsolutePath(asset: string): string | null {
  const raw = asset.trim();
  if (!raw || raw.startsWith('(')) return null; // (内置) 等说明文字
  const firstToken = raw.split(/\s+/)[0];
  if (!firstToken) return null;
  if (/^[A-Za-z]:[\\/]/.test(firstToken) || firstToken.startsWith('\\\\')) return firstToken;
  return null;
}
// 依赖「野蛮系统电源」方案的任务白名单：创建前先 ensureYemanScheme 确保方案存在。
// 目前为空（旧 AC/DC TDP/锁帧任务已移除，无任务依赖方案）；保留为扩展点——
// 未来新增开机/唤醒任务若依赖 YEMAN 方案，在此登记即可（2026-08-05 澄清注释，非死代码）。
const SCHEME_DEPENDENT_TASKS = new Set<string>();

// toggle：开→建（有模板），关→删。返回最新状态
export async function toggleTask(name: string, on: boolean): Promise<boolean> {
  if (on) {
    if (!(await taskExists(name))) {
      // 开机/唤醒任务依赖野蛮系统电源方案，若用户删除过该方案则先重新导入 YM.pow
      if (SCHEME_DEPENDENT_TASKS.has(name)) {
        await ensureYemanScheme();
      }
      const ok = await createTask(name);
      if (!ok) throw new Error('创建任务失败（可能需以管理员身份运行 YeManCC）');
    }
    if (!(await taskExists(name))) throw new Error('任务已导入但状态读取失败');
    return true;
  } else {
    // 关闭时不能以“当前是否启用”作为前置条件，否则禁用任务和旧路径任务会残留。
    if (!(await deleteTask(name))) {
      throw new Error('删除任务失败（可能需以管理员身份运行 YeManCC）');
    }
    return false;
  }
}

// ── Xbox 全屏游戏模式「开机启动」：注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\GamingConfiguration\StartupToGamingHome ──
// 1 = 开机进入 Xbox 全屏游戏模式；0 = 正常启动。必须在 Xbox APP 可正常启动的前提下才生效。
const GAMING_CFG_KEY = 'Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration';
const GAMING_HOME_VALUE = 'StartupToGamingHome';
export async function readGamingHomeStartup(): Promise<boolean> {
  try {
    const v = await registry.read('HKCU', GAMING_CFG_KEY, GAMING_HOME_VALUE);
    return v === 1 || v === true;
  } catch {
    return false;
  }
}
export async function writeGamingHomeStartup(on: boolean): Promise<boolean> {
  try {
    const ok = await registry.write('HKCU', GAMING_CFG_KEY, GAMING_HOME_VALUE, on ? 1 : 0);
    return ok === true;
  } catch {
    return false;
  }
}

// 批量删除「\\野蛮优化整合系统」任务目录中的全部计划任务。
// 使用任务计划原生目录查询，不依赖前端已知任务列表，确保补充任务也会一并删除。
export async function deleteAllYemanTasks(): Promise<void> {
  const taskPathPrefix = `\\${TASK_FOLDER}\\`;
  const command = `$p='${taskPathPrefix.replace(/'/g, "''")}'; Get-ScheduledTask -TaskPath $p -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$false`;
  const r = await shell.run('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command]);
  if (r.exitCode !== 0) {
    throw new Error((r.stderr || r.stdout || '删除任务计划失败').trim());
  }
}

// 只有当前 TASKS 中明确保留的 XML 才允许批量恢复；旧 AC/DC TDP/锁帧 XML 不再恢复。
export async function restoreAllYemanTasks(): Promise<{ imported: number; failed: string[] }> {
  const entries = await fs.readDir(PC_DIR);
  const allowed = new Set(TASKS.map((t) => t.xml).filter((v): v is string => !!v));
  const xmlFiles = entries
    .filter((entry: any) => entry?.isFile && allowed.has(String(entry?.name ?? '')))
    .map((entry: any) => String(entry.name));
  let imported = 0;
  const failed: string[] = [];
  for (const fileName of xmlFiles) {
    const def = TASKS.find((t) => t.xml === fileName);
    if (!def) continue;
    try {
      if (await createTask(def.name)) imported++;
      else failed.push(def.name);
    } catch {
      failed.push(def.name);
    }
  }
  return { imported, failed };
}

// 手柄/快捷键设置（后台手柄 LB+RB 呼出、双击B最小化、Start+D-pad 快捷调节）
export interface GamepadSettings {
  enabled: boolean;          // LB+RB 呼出窗口
  bDoubleMinimize: boolean;  // 双击 B 最小化到托盘
  tdpShortcut: boolean;      // Start + 上/下 调节 TDP ±1W
  fpsShortcut: boolean;      // Start + 左右 调节野蛮系统电源亮度 ±5，AC/DC 跟随当前供电
  killGame: boolean;         // 选择 + B 长按 0.5s → 结束当前游戏（执行 KiLL-EXE.bat）
  openKeyboard: boolean;      // 选择 + X 长按 0.5s → 打开 Windows 触摸键盘
  returnDesktop: boolean;     // 选择 + A 组合按下瞬间 → 返回桌面
  mouseToggle: boolean;       // 选择 + Y 长按 0.5s → 模拟鼠标开/关
  mouseBackend: 'gamebar' | 'joyxoff'; // 设置页选择的模拟鼠标方案
}
const DEFAULT_GAMEPAD_SETTINGS: GamepadSettings = {
  enabled: true,
  bDoubleMinimize: true,
  tdpShortcut: true,
  fpsShortcut: true,
  killGame: true,
  openKeyboard: true,
  returnDesktop: true,
  mouseToggle: true,
  mouseBackend: 'joyxoff',
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

// ── 冲突软件自动关闭：可编辑进程名列表 + 总开关 ──
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
  sha256: string;
  publishedAt?: string;
}
export type StrictVersion = [number, number, number];
const STRICT_VERSION_RE = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const STRICT_SHA256_RE = /^[0-9a-fA-F]{64}$/;
const MAX_VERSION_PART = 0x7fffffff;

export function parseStrictVersion(value: string): StrictVersion {
  if (typeof value !== 'string') throw new Error('版本号必须是字符串');
  const match = STRICT_VERSION_RE.exec(value);
  if (!match) throw new Error(`版本号格式无效：${value || '(空)'}`);
  const parts = match.slice(1).map(Number);
  if (parts.some((part) => !Number.isSafeInteger(part) || part > MAX_VERSION_PART)) {
    throw new Error(`版本号数值超出范围：${value}`);
  }
  return parts as StrictVersion;
}

export function isValidSha256(value: unknown): value is string {
  return typeof value === 'string' && STRICT_SHA256_RE.test(value);
}

export function validateUpdateManifest(value: unknown): UpdateInfo {
  if (!value || typeof value !== 'object') throw new Error('更新清单格式无效');
  const manifest = value as Record<string, unknown>;
  if (typeof manifest.version !== 'string') throw new Error('更新清单缺少版本号');
  parseStrictVersion(manifest.version);
  if (!isValidSha256(manifest.sha256)) throw new Error('更新清单缺少有效的 SHA-256');
  return {
    version: manifest.version,
    sha256: manifest.sha256.toUpperCase(),
    notes: typeof manifest.notes === 'string' ? manifest.notes : undefined,
    publishedAt: typeof manifest.publishedAt === 'string' ? manifest.publishedAt : undefined,
  };
}
// version.json 拉取地址（raw 分支，避免 GitHub API 限流）；版本号一致时由前端比较
export const UPDATE_MANIFEST_URL =
  'https://raw.githubusercontent.com/DaVeZhouMK/YeManCC/main/version.json';
// 下载地址由版本号拼出：releases/download/v<version>/YeManCC.zip
export function updatePackageUrl(version: string): string {
  parseStrictVersion(version);
  return `https://github.com/DaVeZhouMK/YeManCC/releases/download/v${version}/YeManCC.zip`;
}
export async function appVersion(): Promise<string> {
  return invoke<string>('app.version');
}
export async function checkUpdate(url: string): Promise<UpdateInfo> {
  const manifest = await invoke<unknown>('app.checkUpdate', { url }, { timeoutMs: 45000 });
  return validateUpdateManifest(manifest);
}
export async function downloadUpdate(url: string, sha256: string, operationId: string, version: string): Promise<string> {
  parseStrictVersion(version);
  if (!isValidSha256(sha256)) throw new Error('更新包 SHA-256 无效');
  return invoke<string>(
    'app.downloadUpdate',
    { url, sha256, operationId, version },
    // The native downloader has no total wall-clock deadline.  It uses a
    // per-I/O idle timeout and preserves the partial file for Range resume;
    // a bridge timeout here would still abort a legitimate 20-30 minute
    // weak-network download.
    { timeoutMs: 0 },
  );
}
export async function installUpdate(operationId: string, version: string, sha256: string): Promise<boolean> {
  parseStrictVersion(version);
  if (!isValidSha256(sha256)) throw new Error('更新包 SHA-256 无效');
  return invoke<boolean>('app.installUpdate', { operationId, version, sha256 }, { timeoutMs: 150000 });
}
export interface UpdateProgressState {
  operationId?: string;
  phase?: string;
  version?: string;
  attempt?: number;
  nextAttempt?: number;
  retryInSeconds?: number;
  remainingRetrySeconds?: number;
  downloadedBytes?: number;
  totalBytes?: number;
  percent?: number;
  speedBps?: number;
  etaSeconds?: number;
  resumedBytes?: number;
  stage?: 'download' | 'install';
  sha256?: string;
  lastError?: string;
  message?: string;
  error?: string;
  updatedAt?: number;
}
export async function updateState(): Promise<UpdateProgressState> {
  return invoke<UpdateProgressState>('app.updateState', {}, { timeoutMs: 5000 });
}
// 语义化版本比较：a<b 返回 -1，a==b 返回 0，a>b 返回 1
export function compareVersions(a: string, b: string): number {
  const pa = parseStrictVersion(a);
  const pb = parseStrictVersion(b);
  for (let i = 0; i < pa.length; i++) {
    const x = pa[i];
    const y = pb[i];
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
  hasBattery?: boolean;
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
type PowerModeProbe = { mode: 'ac' | 'dc'; reliable: boolean };

async function detectPowerModeProbe(): Promise<PowerModeProbe> {
  const si = await nativeSysInfo();
  if (si?.acLine === 0) return { mode: 'dc', reliable: true };
  if (si?.acLine === 1) return { mode: 'ac', reliable: true };
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      '(Get-CimInstance Win32_Battery).BatteryStatus -join ","',
    ]);
    if (r.exitCode !== 0) return { mode: 'ac', reliable: false };
    const out = (r.stdout || '').trim();
    if (!out) return { mode: 'ac', reliable: true }; // 无电池（台式机）→ AC
    const discharging = out
      .split(',')
      .map((s) => s.trim())
      .some((s) => s === '1');
    return { mode: discharging ? 'dc' : 'ac', reliable: true };
  } catch {
    return { mode: 'ac', reliable: false };
  }
}

export async function detectPowerMode(): Promise<'ac' | 'dc'> {
  return (await detectPowerModeProbe()).mode;
}

export async function detectPowerModeReliable(): Promise<'ac' | 'dc' | null> {
  try {
    const probe = await detectPowerModeProbe();
    return probe.reliable ? probe.mode : null;
  } catch {
    return null;
  }
}

// 唤醒后连续两轮可靠且一致才视为稳定；检测持续失败时仍按 AC 兜底，不引入 unknown。
export async function detectPowerModeStable(): Promise<'ac' | 'dc'> {
  let lastReliable: 'ac' | 'dc' | null = null;
  let stableCount = 0;
  for (let attempt = 0; attempt < 4; attempt++) {
    const probe = await detectPowerModeProbe();
    if (probe.reliable) {
      if (probe.mode === lastReliable) stableCount += 1;
      else {
        lastReliable = probe.mode;
        stableCount = 1;
      }
      if (stableCount >= 2) return probe.mode;
    }
    if (attempt < 3) await new Promise((resolve) => setTimeout(resolve, 250));
  }
  return lastReliable ?? 'ac';
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

export interface CoreArchitectureInfo {
  detected: boolean;
  heterogeneous: boolean;
  efficiencyClasses: number[];
  source: 'cpu-set' | 'processor-relationship' | 'none' | string;
  logical: number;
  physical: number;
}

// Read-only architecture probe. Do not infer hybrid CPUs from model names,
// SMT counts, CCD counts, or core counts; those are ambiguous on AMD and
// multi-socket machines. Unsupported/unknown results stay hidden upstream.
export async function detectCoreArchitecture(): Promise<CoreArchitectureInfo | null> {
  try {
    const result = await invoke<any>('cpu.architecture', {});
    if (!result || typeof result !== 'object') return null;
    const classes = Array.isArray(result.efficiencyClasses)
      ? result.efficiencyClasses.map(Number).filter(Number.isFinite)
      : [];
    return {
      detected: result.detected === true,
      heterogeneous: result.heterogeneous === true && classes.length >= 2,
      efficiencyClasses: classes,
      source: typeof result.source === 'string' ? result.source : 'none',
      logical: Math.max(0, Number(result.logical) || 0),
      physical: Math.max(0, Number(result.physical) || 0),
    };
  } catch {
    return null;
  }
}

// ── TDP 下发（程序记录 + 安全命名管道 daemon / fallback 直连） ──
// daemon 仅在浮动 TDP 接管期间常驻。前端调用专用 native IPC；native 固定可信 EXE 路径，
// 再通过双向消息型命名管道与管理员 daemon 通信。每个请求等待真实硬件 rc，失败立即回退一次性 CLI。
export interface SetTdpOpts {
  apply?: boolean;   // 是否实时下发硬件（调 YeManTdpCtl）
  vendor?: Vendor;
  save?: boolean;    // 是否记录程序配置；默认 true。false=只下发不记忆（自动临时值）
}
const TDPCTL_EXE = (): string => join(PC_DIR, 'pawnio', 'YeManTdpCtl.exe');
let tdpDaemonUp = false;      // 仅浮动接管期间使用 daemon
let tdpDaemonStart: Promise<boolean> | null = null; // 并发调用共享同一个启动结果，禁止 daemon/直连抢硬件
const TDP_FLOAT_ACTIVE = (): string => join(PC_DIR, 'float-active');

async function floatTdpActive(): Promise<boolean> {
  try {
    const st = await fs.stat(TDP_FLOAT_ACTIVE());
    const modifiedMs = Number(st.modified) * 1000;
    return Number.isFinite(modifiedMs) && Date.now() - modifiedMs < 30000;
  } catch {
    return false;
  }
}

async function tdpDaemonAlive(): Promise<boolean> {
  try {
    const r = await tdpDaemon.request('ping', {}, 500);
    return r.ok && r.rc === 0;
  } catch {
    return false;
  }
}

// 确保 daemon 常驻：安全管道 ping 成功则复用；否则由 native 固定可信路径拉起并等待最多 2s。
// 拉起失败返回 false → 调用方走一次性 CLI 直连。
export async function ensureTdpDaemon(): Promise<boolean> {
  if (tdpDaemonUp) {
    if (await tdpDaemonAlive()) return true;
    tdpDaemonUp = false;
  }
  if (tdpDaemonStart) return tdpDaemonStart;
  tdpDaemonStart = (async () => {
    if (await tdpDaemonAlive()) {
      tdpDaemonUp = true;
      return true;
    }
    try {
      await tdpDaemon.start();
    } catch { /* 拉起失败 → 下方等待超时 → fallback */ }
    for (let i = 0; i < 20; i++) {
      await new Promise((r) => setTimeout(r, 100));
      if (await tdpDaemonAlive()) {
        tdpDaemonUp = true;
        return true;
      }
    }
    return false;
  })();
  try {
    return await tdpDaemonStart;
  } finally {
    tdpDaemonStart = null;
  }
}

async function tdpDaemonSet(watts: number): Promise<void> {
  const r = await tdpDaemon.request('set', { watts: Math.round(watts) }, 5000);
  if (!r.ok || r.rc !== 0) throw new Error(r.error || `TDP daemon rc=${r.rc}`);
}

async function assertHardwareWriteAllowed(): Promise<void> {
  const state = await powerLifecycle.get().catch(() => null);
  if (state && (!state.hardwareWritesAllowed || state.phase !== 'ready')) {
    throw new Error(`系统电源恢复尚未完成，已阻止硬件写入（${state.phase}）`);
  }
}

export async function resumeTdpDaemonAfterWake(required = false): Promise<boolean> {
  const alive = await tdpDaemonAlive();
  if (!alive) {
    // If this WebView had already taken ownership of the daemon, or persisted
    // automatic scheduling requires it after a WebView recreation, a daemon
    // killed during sleep must be restarted before the resume transaction can
    // commit.  Manual mode keeps the historical zero-resident behavior.
    const shouldRestart = required || tdpDaemonUp;
    tdpDaemonUp = false;
    if (!shouldRestart) return true;
    if (!(await ensureTdpDaemon().catch(() => false))) return false;
  }
  try {
    const r = await tdpDaemon.request('resume', {}, 5000);
    if (r.ok && r.rc === 0) return true;
  } catch {
    // 句柄重建失败时走下面的完整 daemon 重启。
  }
  await stopTdpDaemon().catch(() => {});
  if (!(await ensureTdpDaemon().catch(() => false))) return false;
  const retry = await tdpDaemon.request('resume', {}, 5000).catch(() => null);
  return !!retry && retry.ok && retry.rc === 0;
}

export async function setTdp(
  mode: 'ac' | 'dc',
  watts: number,
  opts: SetTdpOpts = {}
): Promise<void> {
  // 两个独立动作：save=记录程序配置；apply=实时下发硬件。
  // 顶部「快速切换」/手柄用 { apply:true, save:true }；
  // 自动浮动临时值用 { apply:true, save:false }，不覆盖用户设定的 TDP 最大值。
  const w = clampTdp(watts);
  if (opts.save !== false) {
    await saveTdp(mode, w); // 记录程序控制配置
  }
  if (opts.apply) {
    await assertHardwareWriteAllowed();
    const vendor = opts.vendor && opts.vendor !== 'unknown' ? opts.vendor : await detectVendor();
    // vendor 检测失败（AMD.txt/intel.txt 缺失且注册表读不到）时不能静默跳过：
    // 用户以为 TDP 已下发实际没动，且 YeManTdpCtl 也需要 vendor 才能选对 SMU 通道。
    // 抛错让 UI 显示「TDP 下发失败」而不是假装成功（2026-08-05 修复）。
    if (vendor === 'unknown') {
      throw new Error('无法识别 CPU 厂商（AMD.txt/intel.txt 与注册表均不可用），TDP 下发已跳过');
    }
    // 只有浮动优化真正接管时才写 daemon 命令；普通低功耗调用始终按需直连，
    // 不拉起常驻进程，也不增加额外 AC/DC 检测。
    if (await floatTdpActive()) {
      if (await ensureTdpDaemon().catch(() => false)) {
        try {
          await tdpDaemonSet(w);
        } catch {
          // 写命令失败时不能静默吞掉，否则硬件值会停在旧档位；立即退回单次直连。
          tdpDaemonUp = false;
          const direct = await shell.run(TDPCTL_EXE(), ['set', String(w), '--vendor', vendor]);
          if (direct.exitCode !== 0) throw new Error(direct.stderr || direct.stdout || 'TDP 直连下发失败');
        }
      } else {
        const direct = await shell.run(TDPCTL_EXE(), ['set', String(w), '--vendor', vendor]);
        if (direct.exitCode !== 0) throw new Error(direct.stderr || direct.stdout || 'TDP 直连下发失败');
      }
    } else {
      const direct = await shell.run(TDPCTL_EXE(), ['set', String(w), '--vendor', vendor]);
      if (direct.exitCode !== 0) throw new Error(direct.stderr || direct.stdout || 'TDP 直连下发失败');
    }
  }
}

export async function stopTdpDaemon(): Promise<void> {
  if (await tdpDaemonAlive().catch(() => false)) {
    await tdpDaemon.request('quit', {}, 2000).catch(() => {});
  }
  tdpDaemonUp = false;
  tdpDaemonStart = null;
}

// 手柄快捷：按当前 AC/DC 电源模式，将 TDP 增减 delta 并立即应用
export async function adjustTdp(delta: number): Promise<number | null> {
  const mode = await detectPowerMode().catch(() => null as 'ac' | 'dc' | null);
  if (!mode) return null;
  const cur = await readTdp(mode);
  if (cur === null) return null;
  const next = clampTdp(cur + delta);
  if (next === cur) return cur;
  await setTdp(mode, next, { apply: true, save: false }); // 手柄即时调当前 TDP，不记忆
  return next;
}

// 手柄快捷：按当前 AC/DC 电源模式，将 TDP 目标值设为指定值并立即应用
export async function setTdpCurrentMode(watts: number): Promise<number | null> {
  const mode = await detectPowerMode().catch(() => null as 'ac' | 'dc' | null);
  if (!mode) return null;
  const next = clampTdp(watts);
  await setTdp(mode, next, { apply: true, save: false }); // 手柄即时调当前 TDP，不记忆
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
  // Windows 在混合架构处理器上分别保存主类与第 1 类处理器的最大频率。
  // 重置脚本也同时写入这两个设置；只写 e100 会造成“命令成功但 CPU 调度不变”。
  G_FREQ1: '75b0ae3f-bce0-45a7-8c89-c9611c25e100',
  G_FREQ2: '75b0ae3f-bce0-45a7-8c89-c9611c25e101',
  G_FREQ3: '75b0ae3f-bce0-45a7-8c89-c9611c25e102',
  G_MAXSTATE: 'bc5038f7-23e0-4960-96da-33abaf5935ec',
  G_TURBO: 'be337238-0d82-4146-a960-4f3749d470c7', // 处理器性能提升模式
  G_SCHED1: '36687f9e-e3a5-4dbf-b1dc-15eb381c6863', // 主类积极性
  G_SCHED2: '36687f9e-e3a5-4dbf-b1dc-15eb381c6864', // 第 1 类积极性
  G_SCHED3: '36687f9e-e3a5-4dbf-b1dc-15eb381c6865', // 第 2 类积极性
  G_THROT: '3b04d4fd-1cc7-4f23-ab1c-d1337819c4bb', // 允许节流状态
  G_CORE: '7f2f5cfa-f10c-4823-b5e1-e93ae85f46b5', // 核心暂停（大小核）
  G_HETERO: '93b8b6dc-0698-4d1c-9ee4-0644e900c85d', // 异构调度策略
  G_SHORT: 'bae08b81-2d5e-4688-ad6a-13243356654b', // 短运行线程策略
  // 最小处理器状态联动（隐藏）：CPU 主频 → 本机实际存在的设置，AC/DC 各写。
  G_MIN1: '893dee8e-2bef-41e0-89c6-b55d0929964c', // 主类最小状态
  G_MIN2: '893dee8e-2bef-41e0-89c6-b55d0929964d', // 第 1 类最小状态
  G_MIN3: '893dee8e-2bef-41e0-89c6-b55d0929964e', // 第 2 类最小状态
  // 处理器能量性能首选项策略（注册表实测 = Processor Power Efficiency Class 2 的 energy performance preference policy）
  // 随调度积极性联动写入（与 G_SCHED1/2 同值），隐藏不显示 UI。
  // 核心暂停（Core Parking）活动核心数控制（微软标准 GUID，每台都有）：把最小核心数% 与最大核心数% 锁成同一值 = 锁死活动核心数
  G_MINCORE: '0cc5b647-c1df-4637-891a-dec35c318583', // 处理器性能核心暂停最小核心数 %
  G_MAXCORE: 'ea062031-0e34-4ff1-9b6d-eb1059334028', // 处理器性能核心暂停最大核心数 %
} as const;

// Windows 11“屏幕、睡眠和休眠超时”页面使用的标准电源设置 GUID。
// 页面文案和交互可以跟随 Windows 11，但这些值始终写入固定的野蛮系统电源方案，
// 不读取/修改当前活动方案，避免用户切换到其它方案后页面设置失去归属。
export const SLEEP_TIMEOUT_POWER = {
  VIDEO_SUBGROUP: '7516b95f-f776-4464-8c53-06167f40cc99',
  VIDEO_IDLE: '3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e',
  SLEEP_SUBGROUP: '238c9fa8-0aad-41ed-83f4-97be242c8f20',
  STANDBY_IDLE: '29f6c1db-86da-48c5-9fdb-f2b67b1f44da',
  HIBERNATE_IDLE: '9d7815a6-7ee4-497e-8888-515a05f02364',
} as const;

export const SCHEMES = [
  { key: 'yeman', guid: PW.YEMAN, name: '野蛮系统电源' },
  { key: 'besteff', guid: PW.WIN_SAVER, name: '最佳能效' },
  { key: 'bal', guid: PW.WIN_BAL, name: '平衡' },
  { key: 'bestperf', guid: PW.WIN_HIGH, name: '最佳性能' },
] as const;
export type SchemeKey = (typeof SCHEMES)[number]['key'];

export interface PowerParams {
  acMinState?: number;
  dcMinState?: number;
  acThrottle?: 1 | 2;
  dcThrottle?: 1 | 2;
  acFreq: number; // MHz，0 = 不限制
  dcFreq: number;
  acTurbo: boolean; // true=开启(2) / false=关闭(0)
  dcTurbo: boolean;
  acAggr: number; // 0-100 积极性
  dcAggr: number;
  // 缺省为 AC+DC；CPU 锁定桥层可指定只应用锁定侧，避免覆盖未锁定侧。
  sides?: Array<'ac' | 'dc'>;
  // Manual/reset paths restore the global CPU ceiling; autofloat passes false.
  restoreMaxState?: boolean;
  // Legacy compatibility field. Write paths intentionally do not use it to
  // skip powercfg calls: external tools may have changed the plan meanwhile.
  previousApplied?: PowerParams;
}

// The UI slider is intentionally reversed for EPP: 0 means the most
// energy-saving value (100), while 100 means the most performance-oriented
// value (0).
export function sliderAggrToWindowsValue(slider: number): number {
  return Math.max(0, Math.min(100, 100 - Math.round(slider)));
}
export function windowsValueToSliderAggr(value: number): number {
  const actual = Math.max(0, Math.min(100, Math.round(value)));
  return 100 - actual;
}

// 已激活方案的缓存：setActiveScheme 同 GUID 重复调用时跳过 powercfg /setactive，
// 避免浮动调节期每 ~2 秒触发系统电源策略变更通知（配合 --in-process-gpu 会引发 GPU
// 驱动重初始化、浏览器进程瞬时卡顿 → WebView2 表面 IDC_APPSTARTING 转圈；该转圈与窗口
// 是否前台无关，故「后台/隐藏也转」）。方案真被切换到不同 GUID 时仍会正常下发（自愈）。
//
// 切换/激活电源方案：每次都 /setactive，确保刚写入 YEMAN 方案的 CPU 调度（最大主频/积极性/
// 最小CPU三联动等）被 OS 真正应用。setXvalueindex 仅写注册表，很多处理器性能设置需
// /setactive 触发策略重应用才生效；跳过会导致「写入成功但频率不变化」（CPU 主频调不动的假象）。
// 转圈根因是 YeManTdpCtl.exe 子进程（已 daemon 常驻化解决），与 /setactive 无关——确诊实验
// 已排除 setactive 是 IDC_APPSTARTING 根因，故此处不 skip，对齐 CPUZQ 可用实现。
export async function setActiveScheme(guid: string = PW.YEMAN): Promise<void> {
  const r = await shell.run('powercfg', ['/setactive', guid], 5000);
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
async function hasBatteryDevice(): Promise<boolean> {
  const si = await nativeSysInfo();
  if (typeof si?.hasBattery === 'boolean') return si.hasBattery;
  try {
    const r = await shell.run('powershell', ['-NoProfile', '-Command', '(Get-CimInstance Win32_Battery).Count -gt 0']);
    return r.exitCode === 0 && (r.stdout || '').trim().toLowerCase() === 'true';
  } catch {
    return false;
  }
}
async function setPowerValueIndex(ac: boolean, subGroup: string, setting: string, value: string): Promise<void> {
  const result = ac
    ? await setAcValueIndex(subGroup, setting, value)
    : await setDcValueIndex(subGroup, setting, value);
  if (result.exitCode !== 0) {
    throw new Error(`powercfg ${ac ? '/setacvalueindex' : '/setdcvalueindex'} 失败：${(result.stderr || result.stdout).trim() || `exit ${result.exitCode}`}`);
  }
}
async function setPowerValueIndexBestEffort(ac: boolean, subGroup: string, setting: string, value: string): Promise<void> {
  try {
    // Use the same native registry/power-policy fallback as CPU batches.
    // Core-mode and active-core writes must not lose silently on machines
    // whose PowerSchemes registry ACL rejects direct value writes.
    await applyPowerValueBatch(ac, [[setting, Number(value)]]);
  } catch {
    // CPU/大小核心等写入类参数：单条失败静默忽略，不阻断后续参数。
  }
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
    await rememberYemanScheme('yeman');
    await setActiveScheme(PW.YEMAN);
    // 偏好文件写入失败不影响已切换的电源（仅记录选择，绝不回滚/删除方案）
    try { await savePower('scheme', 'YeMan'); } catch { /* ignore */ }
    return;
  }
  if (key === 'bal') {
    await rememberManualScheme(key, PW.WIN_BAL);
    await setActiveScheme(PW.WIN_BAL);
    await setOverlay(PW.OV_NONE);
    await savePower('scheme', 'Balanced');
    return;
  }
  const ov = key === 'besteff' ? PW.OV_EFF : PW.OV_PERF;
  const tag = key === 'besteff' ? 'BestEfficiency' : 'BestPerformance';
  await rememberManualScheme(key, PW.WIN_BAL);
  await setActiveScheme(PW.WIN_BAL);
  await new Promise((r) => setTimeout(r, 400));
  await setOverlay(ov);
  await savePower('scheme', tag);
}

// 一组 CPU 电源参数通过 native Power API 逐项下发；native 会先解除处理器
// 子组的隐藏属性，失败项再尝试写入正确的 scheme/setting 注册表路径。
// Windows/OEM 对不同核心类别的支持可能不同，因此单项失败必须静默忽略，
// 不能阻止其他参数或后续 /setactive。
async function applyPowerValueBatch(ac: boolean, entries: Array<[string, number]>): Promise<void> {
  if (entries.length === 0) return;
  try {
    await registry.writePowerBatch(
      PW.YEMAN,
      PW.SUB,
      ac ? 'ACSettingIndex' : 'DCSettingIndex',
      entries.map(([setting, value]) => ({ setting, value: Math.trunc(value) })),
    );
  } catch {
    // CPU、电源写入类参数遵循强制逐条执行规则：IPC 或单项失败不冒泡。
  }
}

// 自动性能组合应用用户保存的 CPU 挡位时必须是严格事务：主类 CPU 参数失败、
// 睡眠/唤醒写入门禁拒绝或回读不一致都要向上抛出，让性能调度队列重试或报告失败。
// 第 1/2 类处理器与节流设置在部分旧平台上不存在，仍按可选能力处理。
const OPTIONAL_CPU_PROFILE_SETTINGS = new Set<string>([
  PW.G_SCHED2,
  PW.G_SCHED3,
  PW.G_FREQ2,
  PW.G_FREQ3,
  PW.G_MIN2,
  PW.G_MIN3,
  PW.G_THROT,
]);

async function applyPowerValueBatchStrict(ac: boolean, entries: Array<[string, number]>): Promise<void> {
  if (entries.length === 0) return;
  const result = await registry.writePowerBatch(
    PW.YEMAN,
    PW.SUB,
    ac ? 'ACSettingIndex' : 'DCSettingIndex',
    entries.map(([setting, value]) => ({ setting, value: Math.trunc(value) })),
  );
  const requiredFailures = (result.failed || []).filter(
    (failure) => !OPTIONAL_CPU_PROFILE_SETTINGS.has(failure.setting),
  );
  if (!result.ok && (result.failed || []).length === 0) {
    throw new Error(`CPU 挡位参数写入失败（${ac ? 'AC' : 'DC'}）：native 未返回失败项`);
  }
  if (requiredFailures.length > 0) {
    const detail = requiredFailures
      .map((failure) => `${failure.setting}:${failure.code}`)
      .join(', ');
    throw new Error(`CPU 挡位参数写入失败（${ac ? 'AC' : 'DC'}）：${detail}`);
  }
}

function cpuProfileSideValues(p: PowerParams, side: 'ac' | 'dc') {
  return side === 'ac'
    ? { freq: p.acFreq, turbo: p.acTurbo, aggr: p.acAggr }
    : { freq: p.dcFreq, turbo: p.dcTurbo, aggr: p.dcAggr };
}

async function verifyCpuProfilePowerParams(p: PowerParams, side: 'ac' | 'dc'): Promise<void> {
  const expected = cpuProfileSideValues(p, side);
  const ac = side === 'ac';
  const actual = await readPowerParams();
  if (!actual) throw new Error(`CPU 挡位回读失败（${side.toUpperCase()}）`);
  const actualSide = cpuProfileSideValues(actual, side);
  const mismatches: string[] = [];
  if (actualSide.freq !== expected.freq) mismatches.push(`主频 ${actualSide.freq}/${expected.freq}`);
  if (actualSide.turbo !== expected.turbo) mismatches.push(`睿频 ${actualSide.turbo}/${expected.turbo}`);
  if (actualSide.aggr !== expected.aggr) mismatches.push(`积极性 ${actualSide.aggr}/${expected.aggr}`);

  const expectedMinState = Math.max(0, Math.min(100, Math.round(expected.aggr)));
  const minState = await readSchemeIndex(PW.G_MIN1, '', ac);
  if (minState !== expectedMinState) mismatches.push(`最小状态 ${minState}/${expectedMinState}`);
  const maxState = await readSchemeIndex(PW.G_MAXSTATE, '', ac);
  if (maxState !== 100) mismatches.push(`最大状态 ${maxState}/100`);
  const throttle = await readSchemeIndex(PW.G_THROT, '', ac);
  const expectedThrottle = throttleForFrequency(expected.freq);
  if (throttle != null && throttle !== expectedThrottle) {
    mismatches.push(`节流 ${throttle}/${expectedThrottle}`);
  }
  if (mismatches.length > 0) {
    throw new Error(`CPU 挡位回读不一致（${side.toUpperCase()}）：${mismatches.join('，')}`);
  }
}

// 自动模式的固定 CPU 挡位必须与 CPU 页面保存值完全一致。这里只写当前供电侧，
// 避免 AC 档位覆盖尚未激活的 DC 组合（反之亦然）；模板脚本仅由“重置挡位”调用。
export async function applyCpuProfilePowerParams(p: PowerParams, side: 'ac' | 'dc'): Promise<void> {
  const values = cpuProfileSideValues(p, side);
  const scheduler = sliderAggrToWindowsValue(values.aggr);
  const minState = Math.max(0, Math.min(100, Math.round(values.aggr)));
  const entries: Array<[string, number]> = [
    [PW.G_MAXSTATE, 100],
    [PW.G_SCHED1, scheduler],
    [PW.G_SCHED2, scheduler],
    [PW.G_SCHED3, scheduler],
    [PW.G_TURBO, values.turbo ? 2 : 0],
    [PW.G_FREQ1, values.freq],
    [PW.G_FREQ2, values.freq],
    [PW.G_FREQ3, values.freq],
    [PW.G_THROT, throttleForFrequency(values.freq)],
    [PW.G_MIN1, minState],
    [PW.G_MIN2, minState],
    [PW.G_MIN3, minState],
  ];
  await applyPowerValueBatchStrict(side === 'ac', entries);
  await setActiveScheme(PW.YEMAN);
  await verifyCpuProfilePowerParams(p, side);
}

// 应用 CPU 调度参数（对齐 HTA pw_applyNow：setac/dcvalueindex 全量下发）
// ⚠️ 台式机无电池时 DC 写入会失败，已用 try-catch 静默忽略——不影响 AC 下发。
export async function applyPowerParams(p: PowerParams): Promise<boolean> {
  const applyAc = !p.sides || p.sides.includes('ac');
  const applyDc = !p.sides || p.sides.includes('dc');
  const acThrot = p.acThrottle ?? throttleForFrequency(p.acFreq);
  const dcThrot = p.dcThrottle ?? throttleForFrequency(p.dcFreq);
  // A low frequency target must not leave boost enabled. Otherwise AMD may
  // still boost well above the PROCFREQMAX value.
  const acTurbo = p.acFreq > 0 && p.acFreq <= 2000 ? 0 : (p.acTurbo ? 2 : 0);
  const dcTurbo = p.dcFreq > 0 && p.dcFreq <= 2000 ? 0 : (p.dcTurbo ? 2 : 0);
  const acSched = sliderAggrToWindowsValue(p.acAggr);
  const dcSched = sliderAggrToWindowsValue(p.dcAggr);

  // ── 隐藏联动：最小处理器状态（3 个 GUID，AC+DC 各写） ──
  // ① 默认跟随最大主频：freqToMinState()（0/不限制→50，0–5000 线性，≥5000→50 封顶）
  // ② 积极性 ≥90 时联动：最小CPU = max(积极性%, 最大主频派生值)。
  //    积极性≥90 恒大于封顶50%的派生值，故等效为 最小CPU = 积极性%（90→90%、100→100%），
  //    即「5GHz+积极性90 → 听积极性」；积极性<90 仅走最大主频派生。AC/DC 各算各的。
  const acMinState = p.acMinState ?? Math.max(0, Math.min(100, Math.round(p.acAggr)));
  const dcMinState = p.dcMinState ?? Math.max(0, Math.min(100, Math.round(p.dcAggr)));
  // 三类处理器必须写入同一个联动值。只写主类/第 1 类会留下第 2 类旧值，
  // 在自动浮动、手动调节或 AC/DC 切换后表现为 CPU 主频不跟随。
  const MIN_STATE_GUIDS = [PW.G_MIN1, PW.G_MIN2, PW.G_MIN3];
  const SCHED_GUIDS = [PW.G_SCHED1, PW.G_SCHED2, PW.G_SCHED3];
  const restoreMaxState = p.restoreMaxState !== false;
  let changed = false;

  // AC 写入（台式机必定成功）—— 调度/联动全部拼进同一条脚本，一次 shell.run 下发
  if (applyAc) {
    const entries: Array<[string, number]> = [
    ...(restoreMaxState ? [[PW.G_MAXSTATE, 100] as [string, number]] : []),
    ...SCHED_GUIDS.map((g) => [g, acSched] as [string, number]),
      [PW.G_TURBO, acTurbo],
      [PW.G_FREQ1, p.acFreq],
      [PW.G_FREQ2, p.acFreq],
      [PW.G_FREQ3, p.acFreq],
    ...MIN_STATE_GUIDS.map((g) => [g, acMinState] as [string, number]),
    ];
    changed ||= entries.length > 0;
    await applyPowerValueBatch(true, entries);
    changed = true;
    await applyOptionalPowerValue(true, PW.G_THROT, acThrot);
  }
  // DC 写入（台式机无电池时会失败 → 静默忽略）
  if (applyDc) {
    const entries: Array<[string, number]> = [
      ...(restoreMaxState ? [[PW.G_MAXSTATE, 100] as [string, number]] : []),
      ...SCHED_GUIDS.map((g) => [g, dcSched] as [string, number]),
      [PW.G_TURBO, dcTurbo],
      [PW.G_FREQ1, p.dcFreq],
      [PW.G_FREQ2, p.dcFreq],
      [PW.G_FREQ3, p.dcFreq],
      ...MIN_STATE_GUIDS.map((g) => [g, dcMinState] as [string, number]),
    ];
    changed ||= entries.length > 0;
    await applyPowerValueBatch(false, entries);
    changed = true;
    await applyOptionalPowerValue(false, PW.G_THROT, dcThrot);
  }
  return changed;
}

// CPU 主频(MHz) → 最小处理器状态 联动值（隐藏，无极线性映射）
//   0(不限制) → 50；≥5000 → 50：两端都封顶 50%，最小状态最高就是 50%，不允许 CPU 永不降频
//   0–5000：线性 freq/100（100→1, 1000→10, 3000→30, 4900→49），下限 1
// Manual CPU controls and CPU floating share the native batch path. It performs
// the same per-setting writes while also unlocking hidden processor settings.
export async function applyManualPowerParams(p: PowerParams): Promise<boolean> {
  const applyAc = !p.sides || p.sides.includes('ac');
  const applyDc = !p.sides || p.sides.includes('dc');
  const acThrot = p.acThrottle ?? throttleForFrequency(p.acFreq);
  const dcThrot = p.dcThrottle ?? throttleForFrequency(p.dcFreq);
  const acSched = sliderAggrToWindowsValue(p.acAggr);
  const dcSched = sliderAggrToWindowsValue(p.dcAggr);
  const acTurbo = p.acTurbo ? 2 : 0;
  const dcTurbo = p.dcTurbo ? 2 : 0;
  const acMinState = p.acMinState ?? Math.max(0, Math.min(100, Math.round(p.acAggr)));
  const dcMinState = p.dcMinState ?? Math.max(0, Math.min(100, Math.round(p.dcAggr)));
  const minStates = [PW.G_MIN1, PW.G_MIN2, PW.G_MIN3];
  const schedulers = [PW.G_SCHED1, PW.G_SCHED2, PW.G_SCHED3];

  async function writeSide(ac: boolean, entries: Array<[string, number]>): Promise<void> {
    await applyPowerValueBatch(ac, entries);
  }

  if (applyAc) {
    await writeSide(true, [
      ...schedulers.map((setting) => [setting, acSched] as [string, number]),
      [PW.G_TURBO, acTurbo],
      [PW.G_FREQ1, p.acFreq],
      [PW.G_FREQ2, p.acFreq],
      [PW.G_FREQ3, p.acFreq],
      [PW.G_THROT, acThrot],
      ...minStates.map((setting) => [setting, acMinState] as [string, number]),
    ]);
  }
  if (applyDc) {
    // A desktop may have no DC policy. Still attempt every DC write, but do
    // not turn a valid AC update into a failed manual operation.
    await writeSide(false, [
      ...schedulers.map((setting) => [setting, dcSched] as [string, number]),
      [PW.G_TURBO, dcTurbo],
      [PW.G_FREQ1, p.dcFreq],
      [PW.G_FREQ2, p.dcFreq],
      [PW.G_FREQ3, p.dcFreq],
      [PW.G_THROT, dcThrot],
      ...minStates.map((setting) => [setting, dcMinState] as [string, number]),
    ]);
  }
  return applyAc || applyDc;
}

// CPU 浮动沿用已经验证过的逐条 powercfg 写入链路。
// 浮动调用方显式传入固定最小处理器状态；此别名仅区分调用语义，
// 不改变手动路径的 AC/DC、三类参数和单条失败后继续写入行为。
export async function applyFloatPowerParams(p: PowerParams): Promise<boolean> {
  return applyManualPowerParams(p);
}

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
  await setPowerValueIndexBestEffort(true, S, PW.G_CORE, String(core));
  await setPowerValueIndexBestEffort(true, S, PW.G_HETERO, String(hetero));
  await setPowerValueIndexBestEffort(true, S, PW.G_SHORT, String(shortRun));
  // DC 写入（台式机无电池时静默失败）
  await setPowerValueIndexBestEffort(false, S, PW.G_CORE, String(core));
  await setPowerValueIndexBestEffort(false, S, PW.G_HETERO, String(hetero));
  await setPowerValueIndexBestEffort(false, S, PW.G_SHORT, String(shortRun));
}

// AC/DC 分离的大小核心调度写入（满足 UI 双排控制）
export async function setCoreModeAc(mode: CoreMode): Promise<void> {
  const [core, hetero, shortRun] = CORE_MAP[mode];
  const S = PW.SUB;
  await setPowerValueIndexBestEffort(true, S, PW.G_CORE, String(core));
  await setPowerValueIndexBestEffort(true, S, PW.G_HETERO, String(hetero));
  await setPowerValueIndexBestEffort(true, S, PW.G_SHORT, String(shortRun));
}
export async function setCoreModeDc(mode: CoreMode): Promise<void> {
  const [core, hetero, shortRun] = CORE_MAP[mode];
  const S = PW.SUB;
  await setPowerValueIndexBestEffort(false, S, PW.G_CORE, String(core));
  await setPowerValueIndexBestEffort(false, S, PW.G_HETERO, String(hetero));
  await setPowerValueIndexBestEffort(false, S, PW.G_SHORT, String(shortRun));
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
  await setPowerValueIndexBestEffort(true, S, PW.G_MINCORE, String(pct));
  await setPowerValueIndexBestEffort(true, S, PW.G_MAXCORE, String(pct));
  await setPowerValueIndexBestEffort(false, S, PW.G_MINCORE, String(pct));
  await setPowerValueIndexBestEffort(false, S, PW.G_MAXCORE, String(pct));
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
export async function setSmt(on: boolean): Promise<{ ok: boolean; error?: string; info?: unknown }> {
  try {
    const r = (await invoke('smt.set', { on })) as any;
    return {
      ok: !!(r && r.ok),
      error: r && r.error ? String(r.error) : undefined,
      info: r?.info,
    };
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
    // 浮动关闭时必须恢复进入浮动前的最小处理器状态；三类写入始终
    // 使用同一目标值，因此读取主类即可作为恢复基线。
    const acMinState = await readSchemeIndex(PW.G_MIN1, '', true);
    const dcMinState = await readSchemeIndex(PW.G_MIN1, '', false);
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
      ...(acMinState != null ? { acMinState } : {}),
      ...(dcMinState != null ? { dcMinState } : {}),
      acTurbo,
      dcTurbo,
      acAggr: windowsValueToSliderAggr(acSchedReg),
      dcAggr: windowsValueToSliderAggr(dcSchedReg),
    };
  } catch {
    return null;
  }
}

// 重制电源（四档，运行 TDP 下对应 VBS，VBS 内部静默调用 BAT 修改电源）
export const RESET_PROFILES = [
  { id: 'extreme', name: 'Extreme极致性能', sub: '猛吃CPU-注意过热', path: 'TDP\\Extreme.vbs' },
  { id: 'elite', name: 'Elite精睿性能', sub: '笔记本推荐', path: 'TDP\\Elite.vbs' },
  { id: 'turbo', name: 'Turbo高性能', sub: '掌机推荐', path: 'TDP\\Turbo.vbs' },
  { id: 'balanced', name: 'Performance平衡', sub: 'SteamDeck推荐', path: 'TDP\\Performance.vbs' },
] as const;
export async function runResetProfile(path: string): Promise<void> {
  // 用 cscript.exe（控制台模式）执行 VBS —— 不弹 GUI 对话框、无"内存资源不足"问题
  // VBS 内部 ws.Run "cmd /c ...bat", 0, True 静默调用 BAT 修改电源
  const resolved = /^[A-Za-z]:[\\/]/.test(path) ? path : join(PC_DIR, path);
  const result = await shell.run('cscript.exe', ['//nologo', resolved]);
  if (result.exitCode !== 0) {
    throw new Error(result.stderr || result.stdout || `CPU 挡位脚本退出码 ${result.exitCode}`);
  }
}

/* =========================================================================
 *  RTSS 监控锁帧（对齐 HTA rtss_* 段）
 * ========================================================================= */
let rtssDirCache: string | null = null;
async function resolveRtssDir(): Promise<string> {
  if (rtssDirCache && await fs.exists(`${rtssDirCache}\\RTSS.exe`).catch(() => false)) return rtssDirCache;
  const script = [
    "$c=@()",
    "$p=Get-Process RTSS -ErrorAction SilentlyContinue | Select-Object -First 1",
    "if($p -and $p.Path){$c+=$p.Path}",
    "$c+=@(\"$env:ProgramFiles\\RivaTuner Statistics Server\\RTSS.exe\",\"${env:ProgramFiles(x86)}\\RivaTuner Statistics Server\\RTSS.exe\",\"$env:LOCALAPPDATA\\RivaTuner Statistics Server\\RTSS.exe\")",
    "$k=@('HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*','HKLM:\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*','HKCU:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\*')",
    "foreach($x in $k){Get-ItemProperty $x -ErrorAction SilentlyContinue | Where-Object {$_.DisplayName -match 'RivaTuner Statistics Server|RTSS'} | ForEach-Object {if($_.InstallLocation){$c+=(Join-Path $_.InstallLocation 'RTSS.exe')}elseif($_.DisplayIcon){$c+=($_.DisplayIcon -replace ',.*$','').Trim('\\\"')}}}",
    "foreach($x in ($c|Where-Object {$_}|Select-Object -Unique)){if(Test-Path -LiteralPath $x -PathType Leaf){$d=Split-Path -Parent $x;if((Test-Path (Join-Path $d 'RTSSHooks64.dll'))){Write-Output $d;break}}}"
  ].join(';');
  try {
    const r = await shell.run('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script]);
    const d = (r.stdout || '').split(/\r?\n/).map((v) => v.trim()).find((v) => v && /^[A-Za-z]:\\/.test(v));
    if (d) { rtssDirCache = d; return d; }
  } catch { /* handled below */ }
  throw new Error('未找到 RTSS.exe，请先安装 RivaTuner Statistics Server');
}

export function throttleForFrequency(freqMhz: number): 1 | 2 {
  return freqMhz > 0 && freqMhz <= 2000 ? 1 : 2;
}

export async function applyThrottleLink(p: PowerParams): Promise<void> {
  const applyAc = !p.sides || p.sides.includes('ac');
  const applyDc = !p.sides || p.sides.includes('dc');
  if (applyAc) await applyOptionalPowerValue(true, PW.G_THROT, p.acThrottle ?? throttleForFrequency(p.acFreq));
  if (applyDc) await applyOptionalPowerValue(false, PW.G_THROT, p.dcThrottle ?? throttleForFrequency(p.dcFreq));
}

async function applyOptionalPowerValue(ac: boolean, setting: string, value: number): Promise<void> {
  try {
    await applyPowerValueBatch(ac, [[setting, value]]);
  } catch {
    // OEM/桌面机方案可能没有该可选处理器设置；不能阻断后续核心参数。
  }
}

// CPU 主频(MHz) -> Windows 最大处理器状态(%).
// 以 5GHz 为 100% 的保守比例给 Windows 一个第二重上限；0 表示不限制。
export function freqToMaxState(freqMhz: number): number {
  if (freqMhz <= 0) return 100;
  return Math.max(1, Math.min(100, Math.round(freqMhz / 50)));
}
// FPS 上限档位：0 = 不锁帧（真实数值 0，RTSS Limit=0），其余 30~300（300 2026-08-04 起开放）。
export const FPS_CEILINGS = [0, 30, 60, 90, 120, 200, 300];
export const FPS_MIN = 20;
export const FPS_MAX_DEFAULT = 300;

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
// HWiNFO 进程是否在运行：用于区分「有 HW 但共享内存未起来」(需重启修复)
// 与「无 HW 进程」(未装/未启动) 两套补救逻辑。
export async function hwiNfoRunning(): Promise<boolean> {
  const p = await nativeProcRunning(['HWiNFO64']);
  if (p) return !!p['HWiNFO64'];
  try {
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      '(Get-Process HWiNFO64 -EA 0).Count -gt 0',
    ]);
    return (r.stdout || '').trim().toLowerCase() === 'true';
  } catch {
    return false;
  }
}
export async function readRtssLimit(): Promise<number> {
  const dir = await resolveRtssDir();
  const global = `${dir}\\Profiles\\Global`;
  if (!(await fs.exists(global))) return 0;
  const txt = await fs.readTextFile(global);
  for (const line of txt.split(/\r?\n/)) {
    const m = line.match(/^Limit=(\d+)/);
    if (m) return parseInt(m[1], 10);
  }
  return 0;
}
let rtssConfigWriteQueue: Promise<void> = Promise.resolve();

async function reloadRtssProfiles(dir: string): Promise<void> {
  const result = await rtss.reloadProfiles(`${dir}\\RTSSHooks64.dll`);
  if (!result?.ok) {
    const detail = result?.error || (result?.win32Error ? `Win32 ${result.win32Error}` : '未知错误');
    throw new Error(`RTSS 配置重载失败：${detail}`);
  }
}

export function setRtssLimit(fps: number): Promise<void> {
  const run = rtssConfigWriteQueue.then(async () => {
    const dir = await resolveRtssDir();
    const global = `${dir}\\Profiles\\Global`;
    if (!(await fs.exists(global))) throw new Error('RTSS Global 配置不存在');
  // NaN/Infinity 防护：非法输入写入 0（不锁帧），绝不写坏 RTSS 配置（2026-08-05 修复）
  const v = Number.isFinite(fps) ? Math.max(0, Math.round(fps)) : 0;
  const txt = await fs.readTextFile(global);
  const lines = txt.split(/\r?\n/);
  let found = false;
  const outLines = lines.map((line) => {
    if (/^Limit=\d+/i.test(line)) {
      found = true;
      return `Limit=${v}`;
    }
    return line;
  });
  if (!found) {
    // RTSS Global profiles normally contain Limit=. If a damaged/older file
    // lacks it, insert it at the top rather than silently reporting success.
    outLines.unshift(`Limit=${v}`);
  }
  await fs.writeTextFileAtomic(global, outLines.join('\r\n'));
  // 重载配置：外部改完文件后只 LoadProfile(重新载入磁盘) + UpdateProfiles(套用到运行中的游戏)。
  // ⚠ 不要 SaveProfile —— 它会把 RTSS 内存里的旧状态写回磁盘，覆盖刚改的内容甚至写坏（损坏根因）。
  // ⚠ 不能用 rundll32：RTSS SDK 的 LoadProfile 是 void(LPCSTR)，与 rundll32
  // 的 DllEntry(HWND,HINSTANCE,LPSTR,int) ABI 不兼容，会把 HWND 当成字符串指针。
  await reloadRtssProfiles(dir);
  });
  rtssConfigWriteQueue = run.catch(() => {});
  return run;
}

// 手柄快捷：RTSS 锁帧上限增减 delta（0=不锁帧，其余 FPS_MIN~FPS_MAX_DEFAULT）。
// 语义：0（不锁帧）→ 正向调到 FPS_MIN 起步；FPS_MIN → 负向回到 0（解锁）；
// 其余在 FPS_MIN~MAX 间步进。原实现无法从 0 调起、也无法从 FPS_MIN 回到不锁帧
// （2026-08-05 修复）。
export async function adjustRtssLimit(delta: number): Promise<number | null> {
  const cur = await readRtssLimit();
  let next: number;
  if (cur === 0) {
    next = delta > 0 ? FPS_MIN : 0; // 不锁帧：仅正向可调起
  } else if (cur === FPS_MIN) {
    next = delta < 0 ? 0 : Math.min(FPS_MAX_DEFAULT, cur + delta); // 负向解锁回 0
  } else {
    next = Math.max(FPS_MIN, Math.min(FPS_MAX_DEFAULT, cur + delta));
  }
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
  const dir = await resolveRtssDir();
  const global = `${dir}\\Profiles\\Global`;
  if (!(await fs.exists(global))) return 5;
  const txt = await fs.readTextFile(global);
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
export function setRtssZoom(ratio: number): Promise<void> {
  const run = rtssConfigWriteQueue.then(async () => {
  const dir = await resolveRtssDir();
  const global = `${dir}\\Profiles\\Global`;
    const raw = Number(ratio);
  const z = Number.isFinite(raw)
    ? Math.max(RTSS_ZOOM_MIN, Math.min(RTSS_ZOOM_MAX, Math.round(raw)))
    : 5;
  if (!(await fs.exists(global))) throw new Error('RTSS Global 配置不存在');
  const lines = (await fs.readTextFile(global)).split(/\r?\n/);
  let inOsd = false;
  let done = false;
  let osdIndex = -1; // 找到的 [OSD] 节起始行（用于节内首个键前插入）
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.startsWith('[')) {
      if (inOsd && !done) {
        // 无 ZoomRatio 键：插到 [OSD] 节首行之后（保持键在节内，RTSS 才解析）
        lines.splice(i, 0, `ZoomRatio=${z}`);
        done = true;
        break;
      }
      inOsd = line.trim().toLowerCase() === '[osd]';
      if (inOsd) osdIndex = i;
    } else if (inOsd && /^ZoomRatio=\d+/i.test(line)) {
      lines[i] = `ZoomRatio=${z}`;
      done = true;
    }
  }
  if (!done) {
    if (osdIndex >= 0) {
      // 存在 [OSD] 节但节内没键：插在节行之后；避免追加到文件末尾被 RTSS 忽略
      lines.splice(osdIndex + 1, 0, `ZoomRatio=${z}`);
    } else {
      // 连 [OSD] 节都没有：补一节，保证写入一定生效（2026-08-05 修复追加末尾被忽略）
      lines.push('[OSD]', `ZoomRatio=${z}`);
    }
  }
  await fs.writeTextFileAtomic(global, lines.join('\r\n'));
  // 强制 RTSS 重载配置：LoadProfile(重新载入磁盘) + UpdateProfiles(套用)。同样不要 SaveProfile（会把内存旧状态写回磁盘）。
  await reloadRtssProfiles(dir);
  });
  rtssConfigWriteQueue = run.catch(() => {});
  return run;
}

export async function toggleRtss(on: boolean): Promise<void> {
  if (on) {
    const dir = await resolveRtssDir();
    await shell.hidden('powershell.exe', [
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
      `${PC_DIR}\\RTSS-start.ps1`,
    ]);
    for (let i = 0; i < 80; i++) {
      if (await rtssRunning()) return;
      await new Promise<void>((r) => setTimeout(r, 100));
    }
    throw new Error(`RTSS 启动失败：${dir}\\RTSS.exe 未运行`);
  } else {
    // 关监控 = 结束整个 RTSS 家族（主程序 + 钩子加载器 64/32 + 编码服务），避免 RTSSHooksLoader64.exe 残留
    await shell.run('cmd', [
      '/c',
      'taskkill /F /IM RTSS.exe /IM RTSSHooksLoader64.exe /IM RTSSHooksLoader32.exe /IM EncoderServer64.exe /IM EncoderServer.exe',
    ]);
  }
}
export type OverlayLayout = 'W' | 'L' | 'J' | 'off';
const OVL_W = 'YeManOBS-W-1.ovl';
const OVL_L = 'YeManOBS-L-1.ovl';
const OVL_J = 'YeManOBS-JJ-1.ovl';
const OVL_EMPTY = 'Empty.ovl';
export async function readOverlayLayout(): Promise<string> {
  const dir = await resolveRtssDir();
  const cfg = `${dir}\\Plugins\\Client\\OverlayEditor.cfg`;
  if (!(await fs.exists(cfg))) return '';
  const txt = await fs.readTextFile(cfg);
  for (const line of txt.split(/\r?\n/)) {
    if (line.indexOf('Layout=') === 0) return line.replace('Layout=', '').trim();
  }
  return '';
}
export async function setOverlayLayout(layout: OverlayLayout): Promise<void> {
  const dir = await resolveRtssDir();
  const cfg = `${dir}\\Plugins\\Client\\OverlayEditor.cfg`;
  if (!(await fs.exists(cfg))) throw new Error('RTSS OverlayEditor.cfg 不存在');
  const target = layout === 'W' ? OVL_W : layout === 'L' ? OVL_L : layout === 'J' ? OVL_J : OVL_EMPTY;
  const txt = await fs.readTextFile(cfg);
  const lines = txt.split(/\r?\n/);
  let found = false;
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].indexOf('Layout=') === 0) {
      lines[i] = `Layout=${target}`;
      found = true;
    }
  }
  if (!found) lines.push(`Layout=${target}`);
  await fs.writeTextFile(cfg, lines.join('\r\n'));
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
  const dir = await resolveRtssDir();
  const exe = `${dir}\\RTSS.exe`;
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
  // 后台常驻程序必须走 shell.hidden；shell.run 的同步语义会在命令结束后回收整个子进程树。
  await shell.hidden(exe);
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
 *  可调参数持久化于 C:\SOFT\YeMan\PowerControl\yeman-settings.json 的 sleep section。
 *  Sleep\sleepguard.json 仅作为首次迁移的旧版本输入。
 * ========================================================================= */
export type SleepGuardMode = 'off' | 'custom';
export interface SleepGuardStatus {
  enabled: boolean;             // 总开关
  mode: SleepGuardMode;         // 总开关模式：关闭 / 自选
  suspended: number;            // 当前被冻结任务数
  pauseGameOnSleep: boolean;    // 睡眠事务中暂停游戏，按唤醒分类后恢复
  retryOnEntryFailure: boolean; // 明确 S0/S3 入睡失败时同模式重试一次
  retryOnNonUserWake: boolean;  // 有来源证据的异常唤醒时同模式重试一次
  joyXoffAutoClose: true;       // 固定开启，不提供用户开关
}
type LegacySleepGuardFields = Partial<SleepGuardStatus> & {
  pauseResume?: boolean;
  resleepEnabled?: boolean;
};
export async function sleepGuardGet(): Promise<SleepGuardStatus> {
  const r = await invoke<LegacySleepGuardFields>('sleepGuard.get');
  return {
    enabled: !!r.enabled,
    mode: r.mode === 'custom' ? 'custom' : 'off',
    suspended: Number(r.suspended) || 0,
    pauseGameOnSleep: r.pauseGameOnSleep ?? r.pauseResume !== false,
    retryOnEntryFailure: r.retryOnEntryFailure !== false,
    retryOnNonUserWake: r.retryOnNonUserWake ?? r.resleepEnabled ?? true,
    joyXoffAutoClose: true,
  };
}
export async function sleepGuardSet(on: boolean): Promise<void> {
  await invoke('sleepGuard.set', { on });
}
export async function sleepGuardSetConfig(cfg: Partial<SleepGuardStatus>): Promise<void> {
  await invoke('sleepGuard.setConfig', cfg);
}
export interface SleepFactEvent {
  time: string;
  event: string;
  details: Record<string, unknown>;
}
export interface SleepFactStatus {
  enabled: boolean;
  logPath: string;
  subscriptionActive: boolean;
  lifecycle: string;
  generation: number;
  guardEnabled: boolean;
  sleepCycleActive: boolean;
  gameSuspended: boolean;
  taskMode: string;
  taskPhase: string;
  retryKind: string;
  entryFailureAttempts: number;
  last506: { reason: number; time: string };
  last507: { reason: number; time: string };
  lastAccepted506: string;
  lastAccepted507: string;
  facts: string[];
}
export const sleepFactsGet = () => invoke<SleepFactStatus>('sleepFacts.get');
export const sleepFactsSetEnabled = (enabled: boolean) =>
  invoke<SleepFactStatus>('sleepFacts.setEnabled', { enabled });
export const sleepFactsOpenLog = () => invoke<boolean>('sleepFacts.openLog');
export async function sleepGuardRecoverAll(): Promise<{ resumed: number }> {
  return await invoke<{ resumed: number }>('sleepGuard.recoverAll');
}
export async function sleepGuardSuspendCurrent(): Promise<{ paused: boolean; pid?: number }> {
  return await invoke<{ paused: boolean; pid?: number }>('sleepGuard.suspendCurrent');
}

export interface SleepPowerPlanOptimizationResult {
  ok: boolean;
  activeGuid: string;
  activePreserved: boolean;
  processed: Array<{ guid: string; name: string }>;
  skippedOverlay: string[];
  skipped: Array<{ guid: string; name: string; operation: string; reason: string }>;
  failed: Array<{ guid: string; name: string; operation: string; exitCode: number; detail?: string }>;
}

export async function getSleepPowerPlanOptimizationEnabled(): Promise<boolean> {
  const sleep = await readSettingsSection<any>('sleep');
  return sleep.sleepPowerPlanOptimizationEnabled === true;
}

export async function setSleepPowerPlanOptimizationEnabled(enabled: boolean): Promise<void> {
  await saveSettingsSection('sleep', { sleepPowerPlanOptimizationEnabled: Boolean(enabled) });
}

async function readPlanPowerIndex(
  plan: string,
  subGroup: string,
  setting: string,
  ac: boolean,
): Promise<number | null> {
  const path = `SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${plan}\\${subGroup}\\${setting}`;
  try {
    const value = await registry.read('HKLM', path, ac ? 'ACSettingIndex' : 'DCSettingIndex');
    if (value === null || value === undefined) return null;
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  } catch {
    return null;
  }
}

export interface SleepTimeoutSettings {
  acScreen: number | null;
  dcScreen: number | null;
  acSleep: number | null;
  dcSleep: number | null;
  acHibernate: number | null;
  dcHibernate: number | null;
}

type SleepTimeoutField = 'screen' | 'sleep' | 'hibernate';

const SLEEP_TIMEOUT_FIELDS: Record<SleepTimeoutField, { subGroup: string; setting: string }> = {
  screen: {
    subGroup: SLEEP_TIMEOUT_POWER.VIDEO_SUBGROUP,
    setting: SLEEP_TIMEOUT_POWER.VIDEO_IDLE,
  },
  sleep: {
    subGroup: SLEEP_TIMEOUT_POWER.SLEEP_SUBGROUP,
    setting: SLEEP_TIMEOUT_POWER.STANDBY_IDLE,
  },
  hibernate: {
    subGroup: SLEEP_TIMEOUT_POWER.SLEEP_SUBGROUP,
    setting: SLEEP_TIMEOUT_POWER.HIBERNATE_IDLE,
  },
};

function timeoutSecondsToMinutes(value: number | null): number | null {
  if (value === null || !Number.isFinite(value)) return null;
  return Math.max(0, Math.round(value / 60));
}

/** 读取固定野蛮电源方案中的 Windows 11 屏幕/睡眠/休眠超时（单位：分钟）。 */
export async function readSleepTimeouts(): Promise<SleepTimeoutSettings> {
  const entries = await Promise.all(
    (Object.keys(SLEEP_TIMEOUT_FIELDS) as SleepTimeoutField[]).flatMap((field) => {
      const spec = SLEEP_TIMEOUT_FIELDS[field];
      return [
        readPlanPowerIndex(PW.YEMAN, spec.subGroup, spec.setting, true),
        readPlanPowerIndex(PW.YEMAN, spec.subGroup, spec.setting, false),
      ];
    }),
  );
  return {
    acScreen: timeoutSecondsToMinutes(entries[0]),
    dcScreen: timeoutSecondsToMinutes(entries[1]),
    acSleep: timeoutSecondsToMinutes(entries[2]),
    dcSleep: timeoutSecondsToMinutes(entries[3]),
    acHibernate: timeoutSecondsToMinutes(entries[4]),
    dcHibernate: timeoutSecondsToMinutes(entries[5]),
  };
}

/** 写入固定野蛮电源方案中的单个 Windows 11 超时（单位：分钟）。 */
export async function setSleepTimeout(field: SleepTimeoutField, ac: boolean, minutes: number): Promise<void> {
  const spec = SLEEP_TIMEOUT_FIELDS[field];
  const safeMinutes = Math.max(0, Math.min(10080, Math.round(Number(minutes))));
  const seconds = safeMinutes * 60;
  const verb = ac ? '/setacvalueindex' : '/setdcvalueindex';
  const result = await shell.run(
    'powercfg',
    [verb, PW.YEMAN, spec.subGroup, spec.setting, String(seconds)],
    5000,
  );
  if (result.exitCode !== 0) {
    throw new Error(`电源超时写入失败：${(result.stderr || result.stdout).trim() || `exit ${result.exitCode}`}`);
  }
  const after = await readPlanPowerIndex(PW.YEMAN, spec.subGroup, spec.setting, ac);
  if (after !== seconds) {
    throw new Error(`电源超时回读不一致：期望 ${seconds} 秒，实际 ${after === null ? '无法读取' : `${after} 秒`}`);
  }
}

async function readPowerPolicyIndex(setting: string, ac: boolean): Promise<number | null> {
  const path = `SOFTWARE\\Policies\\Microsoft\\Power\\PowerSettings\\${setting}`;
  try {
    const value = await registry.read('HKLM', path, ac ? 'ACSettingIndex' : 'DCSettingIndex');
    if (value === null || value === undefined) return null;
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  } catch {
    return null;
  }
}

export async function optimizeSleepPowerPlans(): Promise<SleepPowerPlanOptimizationResult> {
  const active = await getActiveScheme();
  if (!active) throw new Error('无法读取当前活动电源计划');

  const listed = await shell.run('powercfg', ['/list'], 10000);
  if (listed.exitCode !== 0) {
    throw new Error(`无法读取电源计划列表：${(listed.stderr || listed.stdout).trim() || `exit ${listed.exitCode}`}`);
  }

  const available = new Set<string>();
  for (const m of `${listed.stdout}\n${listed.stderr}`.matchAll(/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/gi)) {
    available.add(m[0].toLowerCase());
  }

  const balanced = PW.WIN_BAL.toLowerCase();
  const overlays = [
    '6fecc5ae-f350-48a5-b669-b472cb895ccf',
    '27fa6203-3987-4dcc-918d-748559d549ec',
    '64a64f24-65b9-4b56-befd-5ec1eaced9b3',
  ];
  const targets: Array<{ guid: string; name: string }> = [];
  const add = (guid: string, name: string) => {
    const normalized = guid.toLowerCase();
    if (!targets.some((x) => x.guid === normalized)) targets.push({ guid: normalized, name });
  };
  add(active, '当前活动电源计划');
  if (available.has(balanced)) add(balanced, '平衡');

  const skippedOverlay: string[] = [];
  for (const guid of overlays) {
    if (available.has(guid)) add(guid, `奥创 ${guid.slice(0, 8)}`);
    else skippedOverlay.push(guid);
  }

  const skipped: SleepPowerPlanOptimizationResult['skipped'] = [];
  const failed: SleepPowerPlanOptimizationResult['failed'] = [];
  const writes = [
    { sub: '238c9fa8-0aad-41ed-83f4-97be242c8f20', setting: 'bd3b718a-0680-4d9d-8ab2-e1d2b4ac806d', ac: 0, dc: 0, operation: '允许使用唤醒定时器' },
    { sub: '238c9fa8-0aad-41ed-83f4-97be242c8f20', setting: 'd4c1d4c8-d5cc-43d3-b83e-fc51215cb04d', ac: 1, dc: 1, operation: '允许远程打开文件时睡眠' },
    { sub: '238c9fa8-0aad-41ed-83f4-97be242c8f20', setting: '94ac6d29-73ce-41a6-809f-6363ba21b47e', ac: 0, dc: 0, operation: '允许混合睡眠' },
    { sub: '2e601130-5351-4d9d-8e04-252966bad054', setting: '3166bc41-7e98-4e03-b34e-ec0f5f2b218e', ac: 10, dc: 90, operation: '睡眠唤醒策略' },
    { sub: '2e601130-5351-4d9d-8e04-252966bad054', setting: 'c36f0eb4-2988-4a70-8eee-0884fc2c2433', ac: 60000, dc: 60000, operation: '睡眠超时' },
    { sub: '238c9fa8-0aad-41ed-83f4-97be242c8f20', setting: '7bc4a2f9-d8fc-4469-b07b-33eb785aaca0', ac: 10, dc: 10, operation: '处理器调度积极性' },
  ];
  for (const target of targets) {
    for (const write of writes) {
      for (const verb of ['/setacvalueindex', '/setdcvalueindex']) {
        const ac = verb === '/setacvalueindex';
        const expected = ac ? write.ac : write.dc;
        const policy = await readPowerPolicyIndex(write.setting, ac);
        const planCurrent = await readPlanPowerIndex(target.guid, write.sub, write.setting, ac);
        const current = planCurrent ?? policy;
        const operation = `${verb} ${write.operation}`;
        if (current === expected) {
          skipped.push({
            guid: target.guid,
            name: target.name,
            operation,
            reason: planCurrent === expected ? '当前计划已是目标值' : '已由系统策略设为目标值',
          });
          continue;
        }

        const r = await shell.run('powercfg', [verb, target.guid, write.sub, write.setting, String(expected)], 5000);
        if (r.exitCode !== 0) {
          failed.push({ guid: target.guid, name: target.name, operation, exitCode: r.exitCode, detail: (r.stderr || r.stdout).trim() });
          continue;
        }

        const afterPlan = await readPlanPowerIndex(target.guid, write.sub, write.setting, ac);
        const afterPolicy = await readPowerPolicyIndex(write.setting, ac);
        const after = afterPlan ?? afterPolicy;
        if (after !== expected) {
          failed.push({
            guid: target.guid,
            name: target.name,
            operation: `${operation} 回读校验`,
            exitCode: 1,
            detail: `期望 ${expected}，实际 ${after === null ? '无法读取' : after}`,
          });
        }
      }
    }
  }
  // setacvalueindex/setdcvalueindex write the target plan directly. Do not
  // call /setactive here: sleep optimization must never refresh or switch the
  // user's selected power plan, including when the active plan is non-YeMan.
  let activePreserved = false;
  try {
    const activeAfter = await getActiveScheme();
    activePreserved = activeAfter.toLowerCase() === active.toLowerCase();
    if (!activePreserved) {
      failed.push({
        guid: active,
        name: '当前活动电源计划',
        operation: '活动计划回读校验',
        exitCode: 1,
        detail: `期望 ${active}，实际 ${activeAfter}`,
      });
    }
  } catch (e) {
    failed.push({
      guid: active,
      name: '当前活动电源计划',
      operation: '活动计划回读校验',
      exitCode: 1,
      detail: e instanceof Error ? e.message : '无法读取活动计划',
    });
  }
  return { ok: failed.length === 0, activeGuid: active, activePreserved, processed: targets, skippedOverlay, skipped, failed };
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
  if (!isAC && !(await hasBatteryDevice())) return;
  try {
    await setPowerValueIndex(isAC, PW_BTN_SUB, PW_BTN_ITEM, POWER_BTN_QUEUE[idx].val);
  } catch (e) {
    // 台式机无电池 → DC 电源按钮写入失败，静默忽略（不影响 AC）
    throw e;
  }
  // 注意：不再此处立即激活——由前端做 2 秒防抖重新激活（与 CPU 调度调节器一致），
  // 避免快速多次切换电源按钮时反复激活导致闪烁/卡顿。
}

// 系统休眠状态：由 native 使用 GetPwrCapabilities + Windows Power 注册表直读，
// 完全独立于野蛮电源方案，也不依赖 powercfg /a 的本地化文本解析。
export async function readHibernateState(): Promise<HibernateState> {
  return systemHibernate.get();
}

// 兼容旧调用者：状态未知时仍返回 null，不伪装成关闭。
export async function isHibernateOff(): Promise<boolean | null> {
  try {
    const state = await readHibernateState();
    return state.enabledKnown ? !state.enabled : null;
  } catch {
    /* ignore */
  }
  return null;
}
export async function setHibernate(on: boolean): Promise<RunResult> {
  const r1 = await shell.run('powercfg', on ? ['/hibernate', 'on'] : ['/hibernate', 'off'], 10000);
  if (r1.exitCode !== 0) {
    throw new Error(`powercfg /hibernate ${on ? 'on' : 'off'} 失败：${(r1.stderr || r1.stdout).trim() || `exit ${r1.exitCode}`}`);
  }
  if (on) {
    // “系统休眠”必须使用完整 S4 休眠文件，不能降级为 reduced 类型。
    const r2 = await shell.run('powercfg', ['/hibernate', '/type', 'full'], 10000);
    if (r2.exitCode !== 0) {
      throw new Error(`powercfg /hibernate /type full 失败：${(r2.stderr || r2.stdout).trim() || `exit ${r2.exitCode}`}`);
    }
    return r2;
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
  const typeResult = await shell.run('powercfg', ['/hibernate', '/type', 'full'], 10000);
  if (typeResult.exitCode !== 0) {
    throw new Error(`powercfg /hibernate /type full 失败：${(typeResult.stderr || typeResult.stdout).trim() || `exit ${typeResult.exitCode}`}`);
  }
  const sizeResult = await shell.run('powercfg', ['/hibernate', '/size', String(Math.max(30, Math.min(100, pct)))], 10000);
  if (sizeResult.exitCode !== 0) {
    throw new Error(`powercfg /hibernate /size 失败：${(sizeResult.stderr || sizeResult.stdout).trim() || `exit ${sizeResult.exitCode}`}`);
  }
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
    const r = await shell.run('powershell', [
      '-NoProfile',
      '-Command',
      `$s=(New-Object -ComObject WScript.Shell).CreateShortcut('${linkPath}');$s.TargetPath='${FX_LINK}';$s.WorkingDirectory='C:\\Program Files\\FxSound LLC\\FxSound';$s.Description='FxSound 音质优化';$s.Save()`,
    ]);
    if (r.exitCode !== 0 || !(await fs.exists(linkPath))) {
      throw new Error((r.stderr || r.stdout || '创建 FxSound 启动快捷方式失败').trim());
    }
  } else {
    if (await fs.exists(linkPath)) {
      const removed = await fs.remove(linkPath);
      if (!removed && await fs.exists(linkPath)) {
        throw new Error('无法删除 FxSound 启动快捷方式：' + linkPath);
      }
    }
  }
}

const GPU_SCHED_KEY = 'SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers';
const GPU_SCHED_VALUE = 'HwSchMode';
export async function readHardwareGpuSchedule(): Promise<boolean> {
  try {
    const v = await registry.read('HKLM', GPU_SCHED_KEY, GPU_SCHED_VALUE);
    return Number(v) === 2;
  } catch {
    return false;
  }
}
export async function writeHardwareGpuSchedule(on: boolean): Promise<boolean> {
  const ok = await registry.write('HKLM', GPU_SCHED_KEY, GPU_SCHED_VALUE, on ? 2 : 1);
  return ok === true;
}

// 控制中心与 RTSS 开机启动分别管理，各自读取和切换对应的任务计划。
export async function bootMirrorExists(): Promise<boolean> {
  return taskExists(BOOT_CONTROL_CENTER_TASK);
}

export async function readBootMirrorState(): Promise<boolean> {
  return taskExists(BOOT_CONTROL_CENTER_TASK);
}

export async function toggleBootMirror(on: boolean): Promise<boolean> {
  return toggleTask(BOOT_CONTROL_CENTER_TASK, on);
}

export async function readBootRtssState(): Promise<boolean> {
  return taskExists(BOOT_RTSS_TASK);
}

export async function toggleBootRtss(on: boolean): Promise<boolean> {
  return toggleTask(BOOT_RTSS_TASK, on);
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
  await shell.execute('explorer.exe', ['C:\\SOFT\\精简掉的系统文件\\SearchHost']);
}

/* =========================================================================
 *  Steam 大屏（对齐 HTA steam_* 段）
 * ========================================================================= */
const STEAM_DIR = 'C:\\SOFT\\YeMan\\PowerControl\\YeManSteam';
export const STEAM_ADDONS = [
  { key: 'steamcss', name: 'Steam 美化（CSSLoader）', exe: 'C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe', url: '' },
  { key: 'steamscale', name: '小黄鸭缩放插帧（Lossless Scaling）', exe: 'C:\\SOFT\\Lossless.Scaling\\LosslessScaling.exe', url: '' },
  { key: 'steamcheat', name: '游戏修改器合集（Game-Cheats-Manager）', exe: 'C:\\SOFT\\Game Cheats Manager\\Game Cheats Manager.exe', url: 'https://github.com/dyang886/Game-Cheats-Manager' },
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

export interface SteamCustomAddon {
  id: string;
  name: string;
  exe: string;
  enabled: boolean;
}

const STEAM_CUSTOM_DISABLED_SUFFIX = '.disabled';
function customAddonTxtPath(id: string, enabled: boolean): string {
  const safe = id.replace(/[^a-zA-Z0-9_-]/g, '_');
  return `${STEAM_DIR}\\custom-${safe}.txt${enabled ? '' : STEAM_CUSTOM_DISABLED_SUFFIX}`;
}
function customAddonId(exe: string): string {
  let h = 2166136261;
  for (let i = 0; i < exe.length; i++) h = Math.imul(h ^ exe.charCodeAt(i), 16777619);
  return (h >>> 0).toString(16);
}
export async function steamCustomAddons(): Promise<SteamCustomAddon[]> {
  const entries = await fs.readDir(STEAM_DIR).catch(() => [] as any[]);
  const result: SteamCustomAddon[] = [];
  for (const entry of entries) {
    const fileName = String(entry?.name ?? '');
    if (!entry?.isFile || !/^custom-[a-zA-Z0-9_-]+\.txt(?:\.disabled)?$/i.test(fileName)) continue;
    const enabled = !fileName.endsWith(STEAM_CUSTOM_DISABLED_SUFFIX);
    try {
      const raw = (await fs.readTextFile(`${STEAM_DIR}\\${fileName}`, 8192)).trim();
      if (!raw) continue;
      const exe = raw.split(/\r?\n/, 1)[0].trim();
      if (!exe) continue;
      result.push({ id: fileName.replace(/^custom-/, '').replace(/\.txt(?:\.disabled)?$/i, ''), name: basenamePath(exe), exe, enabled });
    } catch { /* ignore malformed custom entry */ }
  }
  return result.sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'));
}
function basenamePath(p: string): string {
  const m = p.match(/[^\\/]+$/);
  return m ? m[0] : p;
}
export async function steamCustomAddonAdd(exe: string): Promise<SteamCustomAddon> {
  const path = exe.trim();
  if (!path) throw new Error('未选择程序');
  const id = customAddonId(path);
  const existing = (await steamCustomAddons()).find((a) => a.exe.toLowerCase() === path.toLowerCase());
  if (existing) return existing;
  await fs.mkdir(STEAM_DIR);
  await fs.writeTextFile(customAddonTxtPath(id, true), path + '\r\n');
  return { id, name: basenamePath(path), exe: path, enabled: true };
}
export async function steamCustomAddonSet(addon: SteamCustomAddon, enabled: boolean): Promise<void> {
  const from = customAddonTxtPath(addon.id, !enabled);
  const to = customAddonTxtPath(addon.id, enabled);
  if (from !== to && await fs.exists(from)) await fs.rename(from, to);
}
export async function steamCustomAddonRemove(addon: SteamCustomAddon): Promise<void> {
  for (const enabled of [true, false]) {
    const path = customAddonTxtPath(addon.id, enabled);
    if (await fs.exists(path)) await fs.remove(path);
  }
}

// .earlystart 主开关
export async function steamEarlyStartFile(): Promise<string> {
  // 优先 native USERPROFILE 直读；兜底 PowerShell（旧 exe 兼容）
  const si = await nativeSysInfo();
  if (si && si.userProfile) return `${si.userProfile}\\.earlystart`;
  const r = await shell.run('powershell', ['-NoProfile', '-Command', '$env:USERPROFILE + "\\.earlystart"']);
  // 不硬编码开发者机器路径：取不到就返回空串，由调用方按「无早启开关」处理，
  // 避免在其它机器上静默读写错误的 C:\Users\DaVe\（2026-08-05 修复）。
  return (r.stdout || '').trim() || '';
}
export const STEAM_EARLYSTART_CONTENT = '"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
export async function steamMasterOn(): Promise<boolean> {
  const f = await steamEarlyStartFile();
  if (!f) return false; // 取不到用户目录 → 视为未启用
  if (!(await fs.exists(f))) return false;
  const c = (await fs.readTextFile(f)).replace(/\s+/g, '');
  return c.length > 0;
}
export async function steamMasterSet(on: boolean): Promise<void> {
  const f = await steamEarlyStartFile();
  if (!f) throw new Error('无法定位用户目录，不能设置开机启动'); // 明确报错而非写错路径
  await fs.writeTextFile(f, on ? STEAM_EARLYSTART_CONTENT + '\r\n' : '');
}
export async function steamRunning(): Promise<boolean> {
  // 优先 native Toolhelp32 枚举；兜底 Get-Process（旧 exe 兼容）
  const p = await nativeProcRunning(['steam']);
  if (p) return !!p['steam'];
  const r = await shell.run('powershell', ['-NoProfile', '-Command', "((Get-Process steam -EA 0).Count -gt 0)"]);
  return (r.stdout || '').trim().toLowerCase() === 'true';
}
export async function steamStop(): Promise<void> {
  await shell.run('taskkill', ['/F', '/IM', 'Steam.exe']);
}
export async function launchSteam(): Promise<void> {
  // 用 cscript.exe 执行 VBS（同 RTSS 热重载/重制电源，避免 cmd/start "" 路径解析错误）
  await shell.run('cscript.exe', ['//nologo', 'C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.vbs']);
}
