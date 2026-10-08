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
// `gamepad.state` is a state snapshot rather than a one-shot command. Keep
// the latest native snapshot so late-mounted pages (notably the controller
// settings view) do not render "disconnected" simply because they mounted
// after the WebView render-ready replay.
let lastGamepadState: unknown = null;
let hasLastGamepadState = false;

export function setLogSink(fn: ((e: LogInput) => void) | null): void {
  logSink = fn;
}

// Diagnostic sink must stay best-effort: a throwing logSink (e.g. a wedged
// debug consumer) must never change IPC resolve/reject semantics.
function emitLog(entry: LogInput): void {
  try {
    logSink?.(entry);
  } catch {
    // ignore diagnostic sink failures
  }
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
      if (msg.event === 'gamepad.state') {
        lastGamepadState = msg.data;
        hasLastGamepadState = true;
      }
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
      reject(new Error('Not running in WebView2'));
      return;
    }
    const id = nextId++;
    const timeout =
      options.timeoutMs && options.timeoutMs > 0
        ? setTimeout(() => {
            pending.delete(id);
            reject(new Error(`IPC command timed out: ${cmd}`));
          }, options.timeoutMs)
        : undefined;
    pending.set(id, { resolve: resolve as (v: unknown) => void, reject, timeout });
    try {
      webview!.postMessage({ id, cmd, args });
    } catch (err) {
      pending.delete(id);
      if (timeout) clearTimeout(timeout);
      const m = err instanceof Error ? err.message : String(err);
      reject(err instanceof Error ? err : new Error(m));
    }
  }).then(
    (res) => {
      emitLog({ cmd, args, ok: true, result: res });
      return res as T;
    },
    (err) => {
      emitLog({ cmd, args, ok: false, error: err.message });
      throw err;
    }
  );
}

export function on<T = unknown>(event: string, handler: (data: T) => void): () => void {
  let active = true;
  let replayPending = event === 'gamepad.state' && hasLastGamepadState;
  const listener = ((e: CustomEvent<T>) => {
    if (!active) return;
    // A live snapshot supersedes a not-yet-delivered cached replay.
    replayPending = false;
    handler(e.detail);
  }) as EventListener;
  window.addEventListener(`ipc:${event}`, listener);
  if (replayPending) {
    queueMicrotask(() => {
      if (!active || !replayPending) return;
      replayPending = false;
      handler(lastGamepadState as T);
    });
  }
  return () => {
    active = false;
    replayPending = false;
    window.removeEventListener(`ipc:${event}`, listener);
  };
}
