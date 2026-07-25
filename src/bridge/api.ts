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
  exists: (path: string) => invoke<boolean>('fs.exists', { path }),
  readDir: (path: string) => invoke<any[]>('fs.readDir', { path }),
  stat: (path: string) => invoke<any>('fs.stat', { path }),
  mkdir: (path: string) => invoke<boolean>('fs.mkdir', { path }),
};

export const shell = {
  run: (program: string, args: string[] = [], timeoutMs = 30000) =>
    invoke<RunResult>('shell.run', { program, args, timeoutMs }),
  open: (url: string) => invoke<boolean>('shell.open', { url }),
  execute: (program: string, args: string[] = []) =>
    invoke<boolean>('shell.execute', { program, args }),
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
};

export const windowApi = {
  startDrag: () => invoke<boolean>('window.startDrag'),
  startResize: (edge: string) => invoke<boolean>('window.startResize', { edge }),
  minimize: () => invoke<boolean>('window.minimize'),
  setTitle: (title: string) => invoke<boolean>('window.setTitle', { title }),
};

export const registry = {
  read: (root: string, path: string, name: string) =>
    invoke<any>('registry.read', { root, path, name }),
  write: (root: string, path: string, name: string, value: any) =>
    invoke<boolean>('registry.write', { root, path, name, value }),
  exists: (root: string, path: string) => invoke<boolean>('registry.exists', { root, path }),
};
