// Semantic wrappers around the native shell IPC commands.
// Names mirror exactly what native/main.cpp registers (fs.*, shell.*, app.*, os.*, dialog.*, window.*, registry.*).
import { invoke } from './ipc';

export interface RunResult {
  exitCode: number;
  stdout: string;
  stderr: string;
}

export const fs = {
  // Read-only file/shortcut icon; native returns a bounded grayscale PNG data URL.
  getFileIcon: (path: string) =>
    invoke<string | null>('fs.getFileIcon', { path }, { timeoutMs: 6000 }),
  readTextFile: (path: string, maxBytes = 1 << 20) =>
    invoke<string>('fs.readTextFile', { path, maxBytes }),
  writeTextFile: (path: string, content: string) =>
    invoke<boolean>('fs.writeTextFile', { path, content }),
  // 原子写：native 端先写临时文件再 MoveFileEx 替换；替换失败必须 reject，禁止 UI 将未落盘状态视为成功。
  writeTextFileAtomic: async (path: string, content: string): Promise<void> => {
    const ok = await invoke<boolean>('fs.writeTextFileAtomic', { path, content });
    if (!ok) throw new Error(`原子写入失败: ${path}`);
  },
  exists: (path: string) => invoke<boolean>('fs.exists', { path }),
  // FAN-927 §2.2：部署边界只需要对**少量清单文件**取摘要。复用 native 进程内 SHA-256，
  // 不再为每个依赖文件起一次 certutil 子进程；正常开启不遍历依赖目录。
  sha256File: (path: string) => invoke<string>('fs.sha256File', { path }, { timeoutMs: 15000 }),
  readDir: (path: string) => invoke<any[]>('fs.readDir', { path }),
  stat: (path: string) => invoke<any>('fs.stat', { path }),
  mkdir: (path: string) => invoke<boolean>('fs.mkdir', { path }),
  remove: (path: string) => invoke<boolean>('fs.remove', { path }),
  rename: (from: string, to: string) => invoke<boolean>('fs.rename', { from, to }),
};

// Unified settings writes are handled by native so the backup and the main
// document are protected by one cross-process transaction.
export const settingsStore = {
  // Native owns the effective PowerControl root, including the supported override.
  location: () => invoke<{ directory: string; file: string; backup: string }>('settings.location', {}, { timeoutMs: 10000 }),
  reset: (path: string, defaults: Record<string, unknown>) => invoke<{ ok: boolean; backups: string[]; protectedDataRecovered: boolean }>('settings.reset', { path, defaults, confirm: 'reset-application-settings' }, { timeoutMs: 15000 }),
  read: (path: string, stamp: string | null) => invoke<{ stamp: string; unchanged: boolean; content?: string }>('settings.read', { path, stamp }),
  write: (path: string, content: string, transaction?: { baseline: Record<string, any>; initialize?: boolean; expectedInputRevision?: number }) =>
    invoke<boolean | { ok: boolean; content: string; reason?: string }>('settings.write', { path, content, ...transaction }),
};

export interface WakePasswordState {
  ok: boolean;
  known: boolean;
  enabled: boolean;
  mixed: boolean;
  ac: number;
  dc: number;
  win32Error: number;
  stage?: string;
  changed?: boolean;
  reactivated?: boolean;
  rollbackOk?: boolean;
  preferenceSaved?: boolean;
}

// No scheme argument by design: native only targets YMCC's fixed power GUID.
export const wakePassword = {
  set: (enabled: boolean) => invoke<WakePasswordState>('power.wakePassword.set', { enabled }, { timeoutMs: 15000 }),
};

export const keyboard = {
  open: () => invoke<boolean>('keyboard.open'),
};

export const shell = {
  run: (program: string, args: string[] = [], timeoutMs = 30000) => {
    const nativeTimeout = Math.max(100, Math.min(600000, Math.round(timeoutMs)));
    // Native 负责终止超时进程树；前端只提供稍晚的保险超时，避免响应链异常时 Promise 永久悬挂。
    return invoke<RunResult>(
      'shell.run',
      { program, args, timeoutMs: nativeTimeout },
      { timeoutMs: nativeTimeout + 5000 },
    );
  },
  open: (url: string) => invoke<boolean>('shell.open', { url }),
  execute: (program: string, args: string[] = []) =>
    invoke<boolean>('shell.execute', { program, args }),
  // Windows 任务视图：native 注入真实 Win+Tab（虚拟/物理手柄同一路径）。
  taskView: () => invoke<boolean>('shell.taskView'),
  hidden: (program: string, args: string[] = []) =>
    invoke<{ ok: boolean; pid?: number }>('shell.hidden', { program, args }),
};

export interface RtssReloadResult {
  ok: boolean;
  error?: string;
  win32Error?: number;
  path?: string;
  errorCode?: string;
  bridgeState?: string;
  bridgeMaintenanceDeferred?: boolean;
}

// Profile SDK calls run in a disposable helper (never loaded into YMCC).
// Layout loads run inside RTSS via its client plugin and return a completion ACK.
export const rtss = {
  reloadProfiles: (dllPath: string) =>
    invoke<RtssReloadResult>('rtss.reloadProfiles', { dllPath }),
  prepareOverlay: (dir: string, powerControlDir: string) =>
    invoke<RtssReloadResult>('rtss.prepareOverlay', { dir, powerControlDir }, { timeoutMs: 15000 }),
  loadOverlay: (dir: string, layout: string) =>
    invoke<RtssReloadResult>('rtss.loadOverlay', { dir, layout }, { timeoutMs: 6000 }),
};

export interface TdpDaemonResponse {
  version: number;
  requestId: string;
  ok: boolean;
  rc: number;
  error?: string;
  result?: Record<string, unknown>;
}

export const tdpDaemon = {
  start: () => invoke<{ ok: boolean }>('tdpDaemon.start'),
  request: (op: 'ping' | 'set' | 'resume' | 'quit', args: Record<string, unknown> = {}, timeoutMs = 3000) =>
    invoke<TdpDaemonResponse>('tdpDaemon.request', { op, args, timeoutMs }, { timeoutMs: timeoutMs + 1000 }),
};

export type PowerLifecyclePhase = 'ready' | 'suspending' | 'suspended' | 'resuming';
export interface PowerLifecycleState {
  phase: PowerLifecyclePhase;
  generation: number;
  hardwareWritesAllowed: boolean;
  inputReady: boolean;
  /** Native F5 is durable state, so a recreated WebView can resume FanHost. */
  resumeReady: boolean;
  hibernateAvailable: boolean;
}

export interface PowerResumeCompleteMeta {
  daemonRequired: boolean;
  daemonReady: boolean;
}

export interface FanManualWakeResult {
  ok: boolean;
  generation: number;
  phase?: PowerLifecyclePhase;
  reason?: string;
  admitted?: boolean;
}

export const powerLifecycle = {
  // Lifecycle reads must not leave callers waiting forever while native is
  // recovering from a power transition.
  get: (timeoutMs = 3000) => invoke<PowerLifecycleState>('power.lifecycle', {}, { timeoutMs }),
  completeResume: (generation: number, meta: PowerResumeCompleteMeta = { daemonRequired: false, daemonReady: false }) =>
    invoke<{ ok: boolean; reason?: string; generation: number; inputReady?: boolean; daemonRequired?: boolean; daemonReady?: boolean }>(
      'power.resumeComplete',
      { generation, ...meta },
    ),
  /** Fan-only explicit rescue for a stale native Suspended phase after the UI is interactive. */
  manualFanWake: (generation?: number) =>
    invoke<FanManualWakeResult>('power.fanManualWake', generation === undefined ? {} : { generation }),
};

export interface HibernateState {
  supported: boolean;
  supportedKnown: boolean;
  enabled: boolean;
  enabledKnown: boolean;
  source: 'registry' | 'unknown';
}

export const systemHibernate = {
  // 独立超时：休眠开关不能被页面上的电源按钮/内存等其他检测永久拖住。
  get: () => invoke<HibernateState>('power.hibernateState', {}, { timeoutMs: 3000 }),
};

// ── 手柄反馈：掌机 Aura 灯光 + 物理震动（native 端口自 HC ROGAlly / XInputController）──
export interface LedFeedbackCapabilities {
  ledAvailable: boolean;
  brightnessAvailable: boolean;
  ledEffects: string[];
  /** HC DeviceCapabilities.DynamicLightingSecondLEDColor（ROG 无此位，隐藏副颜色）。 */
  supportsSecondColor?: boolean;
  vibrationAvailable: boolean;
  physicalPrimarySlot: number;
  /** 不可用原因（可空），供 UI 直显与支持反馈。 */
  ledReason?: string;
  vibrationReason?: string;
  /** 机型库（HC DeviceCapabilities）是否授予灯光能力；仅 ROG 等已实现后端机型可见面板。 */
  familySupportsLighting?: boolean;
  /** YMCC 是否已实现该机型的灯光后端（当前仅 ROG Aura 端口）。 */
  lightingBackendImplemented?: boolean;
  /** SMBIOS 解析出的机型库 familyId（machine.identity 单源）。 */
  machineFamily?: string;
  ledDiagnostics?: {
    controlOpen: boolean;
    auraFeatureLen: number;
    familyCapable: boolean;
    familySupportsLighting?: boolean;
    backendImplemented?: boolean;
    identityMatched: boolean;
    productId: number;
    featureLen: number;
    suppressed: boolean;
  };
  vibrationDiagnostics?: {
    connectedMask: number;
    admittedMask: number;
    sourceLocked: boolean;
    primarySlot: number;
  };
}
export interface LedApplyResult {
  ok: boolean;
  reason?: string;
  applied?: string;
  effectApplied?: boolean;
  brightnessApplied?: boolean;
  mode?: string;
}
export interface VibrateResult {
  ok: boolean;
  reason?: string;
  slot?: number;
  strength?: number;
  rc?: number;
}

export const gamepadFeedback = {
  capabilities: () => invoke<LedFeedbackCapabilities>('gamepad.feedbackCapabilities', {}, { timeoutMs: 2000 }),
  setLed: (args: { enabled?: boolean; mode?: string; color?: string; color2?: string; speed?: number; brightness?: number }) =>
    // native 侧非 ROG 后端经 YeManLightSetter 子进程直驱 HC：dll 加载 + SHA-256
    // 校验 + IsReady 轮询（≤15s）+ HC Open（≥2.4s），native 等待上限 30s。原 3s
    // 会在成功前抢先超时，故对齐为 35s（略高于 native 上限）。
    invoke<LedApplyResult>('gamepad.setLed', args, { timeoutMs: 35000 }),
  vibrate: (strength: number) => invoke<VibrateResult>('gamepad.vibrate', { strength }, { timeoutMs: 3000 }),
};

/**
 * FAN-927 §5：native 侧 Fan 活动记录的只读快照。
 *
 * `active`       = **本次运行**已确认的活动控制意图（native 观察到成功的 enable/preset）。
 * `recoverable`  = 崩溃前仍有效、且当前设备/包代/系统启动均可匹配的记录（§5.2 的窄例外）。
 * `curve`        = 记录中的曲线（渲染器重建后据此重接，不重新编辑用户曲线）。
 */
export interface FanActivitySnapshot {
  schemaVersion: number;
  active: boolean;
  recoverable: boolean;
  revision: number;
  curve: Array<{ tempC: number; dutyPercent: number }>;
  deviceClass?: string;
  packageId?: string;
}

export const app = {
  exit: (code = 0) => invoke<boolean>('app.exit', { code }),
  dataDir: () => invoke<string>('app.dataDir'),
  // The mutable Fan Host session capability must not follow the configurable
  // window-title data directory. Native emergency/sleep/exit cleanup and the
  // renderer therefore share this stable per-user location.
  // Immutable native startup capability; never enabled through settings or UI.
  aiFanMockSession: () => invoke<unknown>('app.aiFanMockSession', {}, { timeoutMs: 5000 }),
  aiCpuIsolatedSession: () => invoke<unknown>('app.aiCpuIsolatedSession'),
  aiFanMockAcceptRequest: (request: object) => invoke<boolean>('app.aiFanMockAcceptRequest', request, { timeoutMs: 5000 }),
  fanStateDir: () => invoke<string>('app.fanStateDir', {}, { timeoutMs: 5000 }),
  // FAN-927 §5：native 侧的当前 run 责任 / Fan 活动记录只读快照。渲染器重建后本地
  // JS 状态为空，只有 native 仍知道"本次运行是否已确认活动控制意图"以及崩溃前仍有效的记录。
  fanActivity: () => invoke<FanActivitySnapshot>('app.fanActivity', {}, { timeoutMs: 5000 }),
  exeDir: () => invoke<string>('app.exeDir'),
  pid: () => invoke<number>('app.pid'),
  // Native resolves the sibling PowerControl directory once at process start.
  // Frontend modules must use that exact value for unified settings writes.
  powerControlDir: () => invoke<string>('app.powerControlDir'),
  // 导出 native 输入捕获日志到桌面（2026-09-09）。
  exportInputCaptureLog: () => invoke<DomainLogResult>('logs.exportInputCapture', {}, { timeoutMs: 8000 }),
};

export const paths = {
  desktop: () => invoke<string>('path.desktop'),
};

// ── 日志域（2026-09-09）：陀螺仪 / 虚拟手柄 独立域日志的清理/导出（设置页新增）──
export interface DomainLogResult {
  ok: boolean;
  reason?: string;
  path?: string;
}
export type DomainLogDomain = 'gyro' | 'virtual';
export interface DomainLogEnabledState {
  gyro: boolean;
  virtual: boolean;
}
export const domainLogs = {
  clear: (domain: DomainLogDomain) => invoke<DomainLogResult>('logs.domainClear', { domain }),
  export: (domain: DomainLogDomain) => invoke<DomainLogResult>('logs.domainExport', { domain }),
  getEnabled: () => invoke<DomainLogEnabledState>('logs.domainGetEnabled'),
  setEnabled: (domain: DomainLogDomain, enabled: boolean) => invoke<DomainLogEnabledState>('logs.domainSetEnabled', { domain, enabled }),
};

export const os = {
  isDarkMode: () => invoke<boolean>('os.isDarkMode'),
  version: () => invoke<string>('os.version'),
  hostname: () => invoke<string>('os.hostname'),
  username: () => invoke<string>('os.username'),
  platform: () => invoke<string>('os.platform'),
  arch: () => invoke<string>('os.arch'),
  theme: () => invoke<any>('os.theme'),
};

export const dialog = {
  confirm: (title: string, message: string) =>
    invoke<boolean>('dialog.confirm', { title, message }),
  message: (title: string, message: string, type = 'info') =>
    invoke<boolean>('dialog.message', { title, message, type }),
  openFile: (filters?: { name: string; extensions: string[] }[]) =>
    invoke<string | null>('dialog.openFile', { filters }),
  openFolder: () => invoke<string | null>('dialog.openFolder'),
};

export interface MusicState {
  enabled: boolean;
  folder: string;
  baseUrl: string;
  reloadRecommended: boolean;
  volume: number;
  mode: 'sequential' | 'random';
}

export const music = {
  get: () => invoke<MusicState>('music.get'),
  setFolder: (folder: string) => invoke<MusicState>('music.setFolder', { folder }),
  clearFolder: () => invoke<MusicState>('music.clearFolder'),
  // 音量独立落盘到 native 配置（与 folder 同文件），部署清 WebView2 缓存不丢
  setVolume: (volume: number) => invoke<{ volume: number }>('music.setVolume', { volume }),
  setMode: (mode: 'sequential' | 'random') => invoke<{ mode: 'sequential' | 'random' }>('music.setMode', { mode }),
};

export const windowApi = {
  startDrag: () => invoke<boolean>('window.startDrag'),
  startResize: (edge: string) => invoke<boolean>('window.startResize', { edge }),
  minimize: () => invoke<boolean>('window.minimize'),
  maximize: () => invoke<boolean>('window.maximize'),
  show: () => invoke<boolean>('window.show'),
  getState: () => invoke<{ visible: boolean; minimized: boolean }>('window.getState'),
  setTitle: (title: string) => invoke<boolean>('window.setTitle', { title }),
  /** Create a real top-level child window owned by the YMCC shell. */
  createChild: (options: { title: string; width: number; height: number; url: string }) =>
    invoke<number>('window.createChild', options, { timeoutMs: 5000 }),
  closeChild: (id: number) => invoke<boolean>('window.closeChild', { id }, { timeoutMs: 3000 }),
  listChildren: () => invoke<number[]>('window.listChildren', {}, { timeoutMs: 3000 }),
};

export interface DisplayMode {
  id: string;
  width: number;
  height: number;
  refresh: number;
  orientation: number;
}

export type DisplayTopology = 'internal' | 'external' | 'clone' | 'extend';

export const display = {
  getModes: () => invoke<{ current: string; modes: DisplayMode[] }>('display.getModes'),
  setMode: (mode: DisplayMode) => invoke<DisplayMode>('display.setMode', mode),
  setTopology: (topology: DisplayTopology) => {
    const args: Record<DisplayTopology, string> = {
      internal: '/internal',
      external: '/external',
      clone: '/clone',
      extend: '/extend',
    };
    return shell.execute('DisplaySwitch.exe', [args[topology]]);
  },
};

export interface GamepadBrightnessState {
  ok: boolean;
  value?: number;
  mode?: 'ac' | 'dc';
  reason?: string;
}

// 任务栏常驻（与 native/main.cpp 的 ipc_on("tray.*") 对应）
// resident=true  → 显示任务栏按钮（并移除托盘）；false → 仅托盘（默认）
export const tray = {
  setResident: (resident: boolean) => invoke<boolean>('tray.setResident', { resident }),
  setTooltip: (tip: string) => invoke<boolean>('tray.setTooltip', { tip }),
};

export const proc = {
  running: (names: string[]) => invoke<Record<string, boolean>>('proc.running', { names }),
  findExact: (path: string) => invoke<{ found: boolean; pid: number }>('proc.findExact', { path }, { timeoutMs: 5000 }),
  terminateTree: (pid: number) => invoke<{ ok: boolean; attempted: number; terminated: number }>('process.terminateTree', { pid }),
  terminateExact: (pid: number, processCreated: string, path: string) => invoke<{
    ok: boolean;
    matched: boolean;
    exited: boolean;
    reason?: string;
  }>('process.terminateExact', { pid, processCreated, path }),
  identity: (pid: number) => invoke<{
    valid: boolean;
    pid: number;
    processCreated?: string;
    path?: string;
  }>('process.identity', { pid }),
};

export interface HttpResponse {
  status: number;
  headers: string;
  body: string;
}

export const http = {
  request: (
    url: string,
    options: { method?: string; headers?: Record<string, string>; body?: string; timeoutMs?: number } = {},
  ) => {
    // Fan Host calls use explicit short deadlines so a dead loopback listener
    // cannot occupy the IPC queue during exit; other callers retain 30s.
    const timeoutMs = Math.max(250, Math.min(120000, Math.round(options.timeoutMs ?? 30000)));
    const { timeoutMs: _ignored, ...requestOptions } = options;
    return invoke<HttpResponse>(
      'http.request',
      { url, ...requestOptions, timeoutMs },
      { timeoutMs: timeoutMs + 1000 },
    );
  },
};

export const registry = {
  read: (root: string, path: string, name: string) =>
    invoke<any>('registry.read', { root, path, name }),
  write: (root: string, path: string, name: string, value: any) =>
    invoke<boolean>('registry.write', { root, path, name, value }),
  writePowerBatch: (
    scheme: string,
    subGroup: string,
    valueName: 'ACSettingIndex' | 'DCSettingIndex',
    entries: Array<{ setting: string; value: number }>,
  ) => invoke<{ ok: boolean; written: number; failed: Array<{ setting: string; code: number }> }>(
    'registry.writePowerBatch',
    { scheme, subGroup, valueName, entries },
  ),
  exists: (root: string, path: string) => invoke<boolean>('registry.exists', { root, path }),
};
