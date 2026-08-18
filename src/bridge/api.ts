// Semantic wrappers around the native shell IPC commands.
// Names mirror exactly what native/main.cpp registers (fs.*, shell.*, app.*, os.*, dialog.*, window.*, registry.*).
import { invoke } from './ipc';

export interface RunResult {
  exitCode: number;
  stdout: string;
  stderr: string;
}

export const fs = {
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
  readDir: (path: string) => invoke<any[]>('fs.readDir', { path }),
  stat: (path: string) => invoke<any>('fs.stat', { path }),
  mkdir: (path: string) => invoke<boolean>('fs.mkdir', { path }),
  remove: (path: string) => invoke<boolean>('fs.remove', { path }),
  rename: (from: string, to: string) => invoke<boolean>('fs.rename', { from, to }),
};

// Unified settings writes are handled by native so the backup and the main
// document are protected by one cross-process transaction.
export const settingsStore = {
  write: (path: string, content: string) => invoke<boolean>('settings.write', { path, content }),
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
  hidden: (program: string, args: string[] = []) =>
    invoke<{ ok: boolean }>('shell.hidden', { program, args }),
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
  hibernateAvailable: boolean;
}

export interface PowerResumeCompleteMeta {
  daemonRequired: boolean;
  daemonReady: boolean;
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

export const app = {
  exit: (code = 0) => invoke<boolean>('app.exit', { code }),
  dataDir: () => invoke<string>('app.dataDir'),
  exeDir: () => invoke<string>('app.exeDir'),
  // Native resolves the sibling PowerControl directory once at process start.
  // Frontend modules must use that exact value for unified settings writes.
  powerControlDir: () => invoke<string>('app.powerControlDir'),
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
  show: () => invoke<boolean>('window.show'),
  getState: () => invoke<{ visible: boolean; minimized: boolean }>('window.getState'),
  setTitle: (title: string) => invoke<boolean>('window.setTitle', { title }),
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
  request: (url: string, options: { method?: string; headers?: Record<string, string>; body?: string } = {}) =>
    invoke<HttpResponse>('http.request', { url, ...options }, { timeoutMs: 30000 }),
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
