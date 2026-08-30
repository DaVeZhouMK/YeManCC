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

export function emitNativeEvent(event: string, data: unknown = {}): void {
  if (!hasWebView) return;
  try {
    webview!.postMessage({ event, data });
  } catch {
    // Controller focus/edit state must remain best-effort in non-native tests.
  }
}

// ── 启动性能采样已按用户要求停用（不再写 startup_trace.txt）──

export function rejectAllPending(reason = 'WebView2 is recovering'): void {
  const error = new Error(reason);
  for (const [id, request] of pending) {
    pending.delete(id);
    if (request.timeout) clearTimeout(request.timeout);
    request.reject(error);
  }
}

if (hasWebView) {
  webview.addEventListener('message', (e: MessageEvent<unknown>) => {
    const msg = e.data as IpcMessage | null;
    if (!msg || typeof msg !== 'object') return;
    if (msg.event === 'webview.recovering') {
      const detail = msg.data as { reason?: unknown } | null;
      rejectAllPending(
        typeof detail?.reason === 'string' ? detail.reason : 'WebView2 is recovering'
      );
    }
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
      logSink?.({ cmd, args, ok: true, result: res });
      return res as T;
    },
    (err) => {
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
