// IPC bridge — frontend ↔ native QiangQiang shell (WebView2)
// Protocol (matches native/main.cpp):
//   Request:  { id, cmd, args }   (postMessage sends an OBJECT; WebView2 serializes it)
//   Response: { id, result } | { id, error }
//   Event:    { event, data }
//
// 这里与仓库 src/ipc.ts 一致，并额外注入 setLogSink 供 Debug 面板记录每条原始返回。

type Pending = {
  resolve: (v: unknown) => void;
  reject: (e: Error) => void;
  timeout?: ReturnType<typeof setTimeout>;
};

type WebViewBridge = {
  postMessage(message: unknown): void;
  addEventListener(type: 'message', listener: (e: MessageEvent<unknown>) => void): void;
};

export type IpcMessage = {
  id?: number;
  result?: unknown;
  error?: unknown;
  event?: string;
  data?: unknown;
};

export interface InvokeOptions {
  timeoutMs?: number;
}

export interface LogEntry {
  id: number;
  ts: number;
  cmd: string;
  args: unknown;
  ok: boolean;
  result?: unknown;
  error?: string;
}
export type LogInput = Omit<LogEntry, 'id' | 'ts'>;

const pending = new Map<number, Pending>();
let nextId = 0;
let logSink: ((e: LogInput) => void) | null = null;

export function setLogSink(fn: ((e: LogInput) => void) | null): void {
  logSink = fn;
}

const webview =
  typeof window !== 'undefined' && 'chrome' in window
    ? ((window as any).chrome?.webview as WebViewBridge | undefined)
    : undefined;

const hasWebView =
  !!webview &&
  typeof webview.addEventListener === 'function' &&
  typeof webview.postMessage === 'function';

export const isNativeRuntime = hasWebView;

// ── 启动性能采样（仅用于诊断启动慢；固定窗口内记录每次原生 IPC 耗时，采样后写文件一次）──
const STARTUP_PROFILE_MS = 10000;
const TRACE_PATH = 'C:\\SOFT\\YeMan\\PowerControl\\startup_trace.txt';
const profileStart = performance.now();
let profileSamples: { cmd: string; ms: number; ok: boolean; args: unknown }[] = [];
let profileFlushed = false;
function recordProfile(cmd: string, ms: number, ok: boolean, args: unknown) {
  if (profileFlushed) return;
  if (cmd === 'fs.writeTextFile') return; // 避免自递归（写文件本身不计入）
  profileSamples.push({ cmd, ms, ok, args });
  if (performance.now() - profileStart >= STARTUP_PROFILE_MS) flushProfile();
}
function flushProfile() {
  if (profileFlushed) return;
  profileFlushed = true;
  const map = new Map<string, { count: number; total: number; max: number }>();
  for (const s of profileSamples) {
    const e = map.get(s.cmd) ?? { count: 0, total: 0, max: 0 };
    e.count++; e.total += s.ms; if (s.ms > e.max) e.max = s.ms;
    map.set(s.cmd, e);
  }
  const lines: string[] = [];
  lines.push(`=== YeManCC 启动 IPC 采样（窗口 ${STARTUP_PROFILE_MS}ms） ===`);
  lines.push(`采样触发时刻：起始后 ${Math.round(performance.now() - profileStart)}ms`);
  const total = (window as any).__startupTotalMs;
  if (typeof total === 'number') lines.push(`前端总启动耗时（模块加载 → 内容可见）：${Math.round(total)}ms`);
  lines.push('');
  lines.push('命令'.padEnd(36) + '调用'.padStart(5) + '总ms'.padStart(8) + '最大ms'.padStart(8));
  for (const [cmd, e] of [...map.entries()].sort((a, b) => b[1].total - a[1].total)) {
    lines.push(
      cmd.padEnd(36) +
      String(e.count).padStart(5) +
      String(Math.round(e.total)).padStart(8) +
      String(Math.round(e.max)).padStart(8)
    );
  }
  // 慢调用明细（> 300ms）
  const slow = profileSamples.filter((s) => s.ms > 300).sort((a, b) => b.ms - a.ms);
  if (slow.length) {
    lines.push('');
    lines.push('慢调用明细（>300ms）：');
    for (const s of slow) {
      let argStr = '';
      try { argStr = JSON.stringify(s.args); } catch { argStr = String(s.args); }
      if (argStr.length > 120) argStr = argStr.slice(0, 120) + '…';
      lines.push(`  ${String(Math.round(s.ms)).padStart(6)}ms  ${s.cmd}  ${argStr}`);
    }
  }
  const text2 = lines.join('\n');
  try {
    webview?.postMessage({ id: -1, cmd: 'fs.writeTextFile', args: { path: TRACE_PATH, content: text2 } });
  } catch { /* ignore */ }
}
(window as any).__flushStartupProfile = flushProfile;

if (hasWebView) {
  webview.addEventListener('message', (e: MessageEvent<unknown>) => {
    const msg = e.data as IpcMessage | null;
    if (!msg || typeof msg !== 'object') return;
    if (typeof msg.id === 'number') {
      const p = pending.get(msg.id);
      if (p) {
        pending.delete(msg.id);
        if (p.timeout) clearTimeout(p.timeout);
        if ('error' in msg) p.reject(new Error(String(msg.error)));
        else p.resolve(msg.result);
      }
    }
    if (typeof msg.event === 'string') {
      window.dispatchEvent(new CustomEvent(`ipc:${msg.event}`, { detail: msg.data }));
    }
  });
}

export function invoke<T = unknown>(
  cmd: string,
  args: object = {},
  options: InvokeOptions = {}
): Promise<T> {
  const t0 = performance.now();
  return new Promise<T>((resolve, reject) => {
    if (!hasWebView) {
      const e = new Error('Not running in WebView2');
      logSink?.({ cmd, args, ok: false, error: e.message });
      reject(e);
      return;
    }
    const id = nextId++;
    const timeout =
      options.timeoutMs && options.timeoutMs > 0
        ? setTimeout(() => {
            pending.delete(id);
            const e = new Error(`IPC command timed out: ${cmd}`);
            logSink?.({ cmd, args, ok: false, error: e.message });
            reject(e);
          }, options.timeoutMs)
        : undefined;
    pending.set(id, { resolve: resolve as (v: unknown) => void, reject, timeout });
    try {
      webview!.postMessage({ id, cmd, args });
    } catch (err) {
      pending.delete(id);
      if (timeout) clearTimeout(timeout);
      const m = err instanceof Error ? err.message : String(err);
      logSink?.({ cmd, args, ok: false, error: m });
      reject(err instanceof Error ? err : new Error(m));
    }
  }).then(
    (res) => {
      recordProfile(cmd, performance.now() - t0, true, args);
      logSink?.({ cmd, args, ok: true, result: res });
      return res as T;
    },
    (err) => {
      recordProfile(cmd, performance.now() - t0, false, args);
      logSink?.({ cmd, args, ok: false, error: err.message });
      throw err;
    }
  );
}

export function on<T = unknown>(event: string, handler: (data: T) => void): () => void {
  const listener = ((e: CustomEvent<T>) => handler(e.detail)) as EventListener;
  window.addEventListener(`ipc:${event}`, listener);
  return () => window.removeEventListener(`ipc:${event}`, listener);
}
