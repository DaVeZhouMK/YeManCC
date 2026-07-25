// mock-shell.ts — 在 Node 端模拟强强壳的 IPC 后端。
// 通过 globalThis.window.chrome.webview 把原生命令桥接到真实 Node 实现：
//   fs.*   -> node:fs（真实磁盘往返）
//   shell.run -> child_process.spawnSync（真实执行 schtasks / powercfg）
//   app.*/os.*/dialog.*/registry.* -> 合理桩值
// 必须在导入任何用到 ipc 的模块之前 import 本文件，确保 ipc.ts 在求值时看到 window。
import { spawnSync } from 'child_process';
import * as fs from 'fs';

type Msg = { id: number; cmd: string; args: any };

const listeners: ((e: { data: any }) => void)[] = [];

function dispatch(cmd: string, a: any): any {
  switch (cmd) {
    case 'fs.readTextFile':
      return fs.readFileSync(a.path, 'utf8');
    case 'fs.writeTextFile':
      fs.mkdirSync(require('path').dirname(a.path), { recursive: true });
      fs.writeFileSync(a.path, a.content ?? '');
      return true;
    case 'fs.exists':
      return fs.existsSync(a.path);
    case 'fs.readDir':
      return fs.readdirSync(a.path).map((n) => ({ name: n }));
    case 'fs.stat':
      return fs.statSync(a.path);
    case 'fs.mkdir':
      fs.mkdirSync(a.path, { recursive: true });
      return true;
    case 'shell.run': {
      const r = spawnSync(a.program, a.args ?? [], { encoding: 'utf8' });
      return {
        exitCode: r.status ?? (r.error ? 1 : 0),
        stdout: r.stdout ?? '',
        stderr: r.stderr ?? (r.error ? String(r.error) : ''),
      };
    }
    case 'shell.open':
      return true;
    case 'shell.execute':
      return true;
    case 'app.dataDir':
      return require('os').tmpdir();
    case 'app.exeDir':
      return process.cwd();
    case 'app.exit':
      return true;
    case 'os.isDarkMode':
      return true;
    case 'os.version':
      return '10.0.26100';
    case 'os.hostname':
      return 'mock';
    case 'os.username':
      return 'mock';
    case 'os.platform':
      return 'windows';
    case 'os.arch':
      return 'x64';
    case 'os.theme':
      return { dark: true };
    case 'registry.read': {
      // 用 reg query 真实读取（与壳行为一致）；失败则抛错
      const r = spawnSync('reg', ['query', `HKLM\\${a.path}`, '/v', a.name], { encoding: 'utf8' });
      if (r.status !== 0) throw new Error('registry read failed');
      return (r.stdout ?? '').trim();
    }
    case 'registry.exists':
      return true;
    case 'dialog.confirm':
      return true;
    case 'dialog.message':
      return true;
    default:
      throw new Error('unhandled mock command: ' + cmd);
  }
}

const webview = {
  postMessage(msg: Msg) {
    Promise.resolve().then(() => {
      try {
        const result = dispatch(msg.cmd, msg.args ?? {});
        listeners.forEach((l) => l({ data: { id: msg.id, result } }));
      } catch (e: any) {
        listeners.forEach((l) => l({ data: { id: msg.id, error: String(e?.message ?? e) } }));
      }
    });
  },
  addEventListener(_type: string, fn: (e: { data: any }) => void) {
    listeners.push(fn);
  },
};

(globalThis as any).window = { chrome: { webview } };
