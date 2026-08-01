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
  // 原子写：native 端先写临时文件再 MoveFileEx 替换，避免 RTSS 在游戏内读取时被截断式写冲坏
  writeTextFileAtomic: (path: string, content: string) =>
    invoke<boolean>('fs.writeTextFileAtomic', { path, content }),
  exists: (path: string) => invoke<boolean>('fs.exists', { path }),
  readDir: (path: string) => invoke<any[]>('fs.readDir', { path }),
  stat: (path: string) => invoke<any>('fs.stat', { path }),
  mkdir: (path: string) => invoke<boolean>('fs.mkdir', { path }),
  remove: (path: string) => invoke<boolean>('fs.remove', { path }),
  rename: (from: string, to: string) => invoke<boolean>('fs.rename', { from, to }),
};

export const shell = {
  run: (program: string, args: string[] = [], timeoutMs = 30000) =>
    invoke<RunResult>('shell.run', { program, args, timeoutMs }),
  open: (url: string) => invoke<boolean>('shell.open', { url }),
  execute: (program: string, args: string[] = []) =>
    invoke<boolean>('shell.execute', { program, args }),
  hidden: (program: string, args: string[] = []) =>
    invoke<{ ok: boolean }>('shell.hidden', { program, args }),
};

export const app = {
  exit: (code = 0) => invoke<boolean>('app.exit', { code }),
  dataDir: () => invoke<string>('app.dataDir'),
  exeDir: () => invoke<string>('app.exeDir'),
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
};

export const windowApi = {
  startDrag: () => invoke<boolean>('window.startDrag'),
  startResize: (edge: string) => invoke<boolean>('window.startResize', { edge }),
  minimize: () => invoke<boolean>('window.minimize'),
  show: () => invoke<boolean>('window.show'),
  setTitle: (title: string) => invoke<boolean>('window.setTitle', { title }),
};

// 任务栏常驻（与 native/main.cpp 的 ipc_on("tray.*") 对应）
// resident=true  → 显示任务栏按钮（并移除托盘）；false → 仅托盘（默认）
export const tray = {
  setResident: (resident: boolean) => invoke<boolean>('tray.setResident', { resident }),
  setTooltip: (tip: string) => invoke<boolean>('tray.setTooltip', { tip }),
};

// Xbox 大屏游戏模式 = 总闸：开启才启动全屏检测线程并确保托盘在位
export const xbox = {
  setActive: (on: boolean) => invoke<boolean>('xbox.setActive', { on }),
};

export const proc = {
  running: (names: string[]) => invoke<Record<string, boolean>>('proc.running', { names }),
};

export const registry = {
  read: (root: string, path: string, name: string) =>
    invoke<any>('registry.read', { root, path, name }),
  write: (root: string, path: string, name: string, value: any) =>
    invoke<boolean>('registry.write', { root, path, name, value }),
  exists: (root: string, path: string) => invoke<boolean>('registry.exists', { root, path }),
};
