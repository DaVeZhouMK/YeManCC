// YMCC's Windows loader policy: reuse live frontend state without Steam restarts.
// URL placeholders are filled by the pinned create_inject_script bytecode.
try {
  const attachRecovery = __ROUTER_POLICY__;
  const key = '__YMCC_DECKY_INJECTION_POLICY__';
  const previous = window[key];
  if (!previous?.pending) {
    previous?.cancelRetry?.();
    const state = window[key] = { version: 4, pending: false, phase: 'starting',
      failures: previous?.failures ?? 0, importAttempts: previous?.importAttempts ?? 0,
      bootFailed: previous?.bootFailed ?? false };
    let retry = null, authPending = null, lastReport = previous?.lastReport ?? -Infinity;
    const current = () => window[key] === state;
    state.cancelRetry = () => { if (retry !== null) clearTimeout(retry); retry = null; };
    const authenticate = async () => {
      const controller = new AbortController();
      let deadline;
      try {
        // Both headers/body are bounded; the losing operation never writes state.
        const expired = new Promise((_, reject) => {
          deadline = setTimeout(() => {
            reject(new Error('Decky authentication timed out'));
            controller.abort();
          }, 10000);
        });
        return await Promise.race([(async () => {
          const response = await fetch('http://127.0.0.1:1337/auth/token', { cache: 'no-store', signal: controller.signal });
          if (!response.ok) throw new Error('Decky authentication unavailable');
          const token = await response.text();
          if (!token || token.length > 4096) throw new Error('Decky authentication invalid');
          return token;
        })(), expired]);
      } finally { clearTimeout(deadline); }
    };
    // Injection and transport retries share a single bounded auth request.
    state.authenticate = previous?.version === 4 && typeof previous.authenticate === 'function'
      ? previous.authenticate
      : () => authPending ??= authenticate().finally(() => { authPending = null; });
    const run = async () => {
      if (!current() || state.pending) return;
      state.cancelRetry(); state.pending = true;
      let failed = false;
      try {
        if (window.deckyHasLoaded && typeof window.DeckyBackend?.connect === 'function' && !state.bootFailed) {
          const recovery = attachRecovery(window.DeckyBackend);
          const token = await state.authenticate();
          if (!current()) return;
          window.deckyAuthToken = token;
          recovery?.backendReady();
          state.phase = 'reused';
        } else {
          const stale = !!window.deckyHasLoaded;
          window.deckyHasLoaded = true; state.bootFailed = true;
          const attempt = state.importAttempts++;
          await import('http://localhost:1337/frontend/__SCRIPT__?v=__VERSION__' +
            (stale ? '&ymccRecovery=1' : '') + (attempt ? '&ymccRetry=' + attempt : ''));
          // The pinned entry exposes the actual async init, not just module fetch.
          if (window.__YMCC_DECKY_BOOT_PROMISE__) await window.__YMCC_DECKY_BOOT_PROMISE__;
          if (!current()) return;
          state.bootFailed = false;
          attachRecovery(window.DeckyBackend)?.backendReady();
          state.phase = 'imported';
        }
        state.failures = 0; state.retryDelayMs = 0;
      } catch (_) {
        if (!current()) return;
        failed = true; state.phase = 'retry'; state.failures = Math.min(state.failures + 1, 4);
        if (typeof window.DeckyBackend?.connect !== 'function') window.deckyHasLoaded = false;
      } finally {
        state.pending = false;
        if (failed && current()) {
          const delay = [5000, 15000, 30000, 60000][state.failures - 1];
          state.retryDelayMs = delay;
          const now = Date.now();
          if (now - lastReport >= 30000) {
            state.lastReport = lastReport = now;
            try { console.debug('[YMCC:Decky] recovery ' + JSON.stringify({ phase: 'retry', delayMs: delay })); } catch (_) {}
          }
          // Only failed work owns this single timer. Healthy state has none.
          retry = setTimeout(() => { retry = null; void run(); }, delay);
        }
      }
    };
    void run();
  }
} catch (_) { console.error('[YMCC:Decky] Recovery setup failed'); }
