(function attachDeckyRecovery(router, originalConnect = router?.connect) {
  // Works on the pinned router and its already-mounted instance. No Steam API,
  // plugin remount, polling, new business request or credential logging.
  // Injection may observe the packaged router before its first connect call.
  // Unwrap its lazy adapter entry rather than capturing the wrapper recursively.
  originalConnect = originalConnect?.__YMCC_DECKY_ORIGINAL_CONNECT__ ?? originalConnect;
  if (!router || typeof originalConnect !== 'function' || typeof router.write !== 'function' ||
      typeof router.runningCalls?.values !== 'function') return null;
  const key = '__YMCC_DECKY_RECOVERY__';
  if (router[key]?.version === 1) return router[key];
  const state = { version: 1, policyVersion: 5, closingTimeouts: 0, phase: 'idle', attempts: 0, disconnects: 0, readySignals: 0, failures: 0 };
  let retry = null, deadline = null, readyPromise = null, resolveReady = null;
  let lastReport = -Infinity, lastCode = null, inConnect = false, readyCredit = 0, refreshing = false, authFresh = false;
  const watched = new WeakSet(), closed = new WeakSet();
  const isOpen = () => router.ws?.readyState === WebSocket.OPEN;
  const isConnecting = () => router.ws?.readyState === WebSocket.CONNECTING;
  const clearRetry = () => { if (retry !== null) clearTimeout(retry); retry = null; };
  const clearDeadline = () => { if (deadline !== null) clearTimeout(deadline); deadline = null; };
  const report = (phase, event) => {
    state.phase = phase;
    const code = Number.isInteger(event?.code) && event.code >= 0 && event.code <= 65535 ? event.code : 0;
    const now = Date.now();
    if (phase === 'open' || now - lastReport >= 30000 || code !== lastCode) {
      lastReport = now; lastCode = code;
      try { router.debug('[YMCC:Decky] transport ' + JSON.stringify({ phase, code, clean: event?.wasClean === true,
        attempts: state.attempts, disconnects: state.disconnects, policyVersion: state.policyVersion, closingTimeouts: state.closingTimeouts })); } catch (_) {}
    }
  };
  const waitForOpen = () => readyPromise ??= new Promise(resolve => { resolveReady = resolve; });
  const opened = socket => {
    if (socket !== router.ws) return;
    clearRetry(); clearDeadline(); readyCredit = 0; state.failures = 0;
    if (resolveReady) resolveReady();
    readyPromise = resolveReady = null;
    report('open');
  };
  const schedule = () => {
    if (retry !== null || refreshing || isOpen() || isConnecting()) return;
    const delay = readyCredit > 0 ? 0 : [5000, 15000, 30000, 60000][Math.max(0, state.failures - 1)];
    readyCredit = 0;
    retry = setTimeout(() => { retry = null; start(); }, delay);
  };
  const disconnected = (socket, event) => {
    if (socket && socket !== router.ws) return;
    if (socket && closed.has(socket)) return;
    if (socket) closed.add(socket);
    clearDeadline(); state.disconnects++; state.failures = Math.min(state.failures + 1, 4);
    report('waiting-backend', event);
    schedule();
  };
  const watch = socket => {
    if (!socket || typeof socket.addEventListener !== 'function') return;
    if (!watched.has(socket)) {
      watched.add(socket);
      socket.addEventListener('open', () => opened(socket), { once: true });
      socket.addEventListener('close', event => disconnected(socket, event), { once: true });
    }
    // Also bound a handshake borrowed from an already-mounted legacy router.
    // Repeated readiness/connect signals keep its original deadline.
    if (socket === router.ws && (socket.readyState === WebSocket.CONNECTING || socket.readyState === WebSocket.CLOSING) && deadline === null) {
      deadline = setTimeout(() => {
        deadline = null;
        if (socket !== router.ws) return;
        if (socket.readyState === WebSocket.CONNECTING) {
          try { socket.close(); } catch (_) {
            closed.add(socket); router.ws = null; disconnected(null); return;
          }
          // Browser close acknowledgement can stall independently of the handshake.
          watch(socket);
        } else if (socket.readyState === WebSocket.CLOSING) {
          // The old transport cannot send. Detach only our logical router reference;
          // its eventual CLOSE is stale, never a reason to disturb the replacement.
          state.closingTimeouts++; closed.add(socket); router.ws = null; disconnected(null);
        }
      }, 10000);
    }
  };
  const start = () => {
    if (isOpen()) { opened(router.ws); return; }
    if (isConnecting() || router.ws?.readyState === WebSocket.CLOSING || inConnect) {
      watch(router.ws); return;
    }
    if (refreshing) return;
    const authenticate = window.__YMCC_DECKY_INJECTION_POLICY__?.authenticate;
    if (state.failures > 0 && !authFresh && typeof authenticate === 'function') {
      clearRetry(); refreshing = true;
      // A backend restart can rotate auth even while the first boot awaits us.
      // Refresh only on a failed transport attempt, using its existing backoff.
      Promise.resolve().then(authenticate).then(token => {
        refreshing = false;
        if (isOpen()) return;
        window.deckyAuthToken = token; authFresh = true; start();
      }, () => {
        refreshing = false;
        if (isOpen()) return;
        state.failures = Math.min(state.failures + 1, 4); report('waiting-auth'); schedule();
      });
      return;
    }
    authFresh = false;
    clearRetry(); state.attempts++; state.phase = 'connecting'; inConnect = true;
    try {
      // The original open handler retains FULL_SYNC and all pending resolvers.
      const attempt = state.attempts;
      Promise.resolve(originalConnect.call(router)).catch(() => {
        if (attempt !== state.attempts || isOpen()) return;
        clearDeadline(); state.failures = Math.min(state.failures + 1, 4); report('waiting-backend'); schedule();
      });
      const socket = router.ws; watch(socket);
      if (socket?.readyState === WebSocket.OPEN) opened(socket);
      else if (socket?.readyState === WebSocket.CLOSED) disconnected(socket);
    } catch (_) { clearDeadline(); state.failures = Math.min(state.failures + 1, 4); report('waiting-backend'); schedule(); }
    finally { inConnect = false; }
  };
  router.connect = () => {
    if (isOpen()) return Promise.resolve();
    const pending = waitForOpen(); if (retry === null) start(); return pending;
  };
  const originalMessage = router.onMessage;
  if (typeof originalMessage === 'function') router.onMessage = function (event) {
    const source = event?.currentTarget || event?.target;
    if (source && source !== router.ws) return;
    return originalMessage.call(router, event);
  };
  router.onError = event => disconnected(event?.currentTarget || event?.target || router.ws, event);
  state.backendReady = () => {
    state.readySignals++;
    if (isOpen()) return;
    readyCredit = 1; authFresh = true; clearRetry(); start();
  };
  Object.defineProperty(router, key, { value: state, configurable: false, enumerable: false });
  watch(router.ws);
  return state;
})
