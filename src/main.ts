import { createApp, nextTick } from 'vue';
import { createPinia } from 'pinia';
import App from './App.vue';
import { router, preloadStartupRoutes } from './router';
import { initialPageWork, settleStartup } from './robust/startupReadiness';
import { useDebugStore } from './stores/debug';
import { invoke, isNativeRuntime } from './bridge/ipc';
import { reportFrontendError } from './robust/frontendDiagnostics';
import { clearRouteFallback, showRouteFallback } from './robust/routeFallback';
import { APP_VERSION } from './version';
import './styles/tokens.css';
import './styles/gamepad.css';

const app = createApp(App);
app.use(createPinia());
app.use(router);

let routeEpoch = 0;
let initialRouteSettled = false;
let resolveInitialRoute: (ready: boolean) => void = () => {};
const initialRouteReady = new Promise<boolean>((resolve) => {
  resolveInitialRoute = resolve;
});
let resolveAppStartupReady: (ready: boolean) => void = () => {};
const appStartupReady = new Promise<boolean>((resolve) => {
  resolveAppStartupReady = resolve;
});
window.addEventListener('app-startup-ready', () => resolveAppStartupReady(true), { once: true });

// 安装桥日志总线：每条原生 API 调用的原始返回都会进入调试面板
useDebugStore().installLogSink();

// ── 全局错误护栏 ──
// JS 错误本身不会让 WebView2 进程崩溃，但未捕获的异常可能让某个异步
// 处理器停在中间状态（例如后续赋值未执行）。这里集中兜住，打印到控制台
// （WebView2 DevTools 可见）并写入调试总线，便于将来定位"静默坏状态"。
let lastSettingsStartupError = '';
function reportError(where: string, err: unknown) {
  const msg = err instanceof Error ? `${err.message}\n${err.stack ?? ''}` : String(err);
  if (/配置|settings|schema|JSON|目录/i.test(msg)) lastSettingsStartupError = msg;
  // eslint-disable-next-line no-console
  console.error(`[YeManCC] ${where}:`, msg);
  try {
    useDebugStore().pushLog({ cmd: 'window.error', args: { where }, ok: false, error: msg });
  } catch {
    /* 护栏自身不得再抛错 */
  }
  reportFrontendError(where, err);
}

app.config.errorHandler = (err, _instance, info) => {
  reportError(`vue.${info}`, err);
  showRouteFallback('页面组件发生异常', router.currentRoute.value.fullPath, false, err instanceof Error ? err.message : String(err));
};

// Lazy chunks and async component setup can fail after the shell itself is
// mounted. Retry one time per route/session, then leave a visible degraded
// fallback instead of allowing an empty app-content area.
router.onError((error, to) => {
  const route = to?.fullPath || router.currentRoute.value.fullPath;
  reportError(`router.error:${route}`, error);
  const text = String(error instanceof Error ? error.message : error);
  const isChunkError = /chunk|dynamic import|importing a module|failed to fetch/i.test(text);
  const retryKey = `yemancc.route-retry:${APP_VERSION}:${route}`;
  let retried = false;
  try {
    if (isChunkError && !sessionStorage.getItem(retryKey)) {
      sessionStorage.setItem(retryKey, '1');
      retried = true;
      void router.replace(route).catch((retryError) => reportError(`router.retry:${route}`, retryError));
    }
  } catch (storageError) {
    reportError('router.retry-storage', storageError);
  }
  if (!retried) {
    showRouteFallback('页面资源加载失败', route);
    if (!initialRouteSettled) {
      initialRouteSettled = true;
      resolveInitialRoute(false);
    }
  }
});

// A hidden startup document may throttle rAF. The bounded fallback permits
// checking committed DOM only; native still requires both readiness signals.
function waitForRenderFrame(): Promise<void> {
  return new Promise<void>((resolve) => {
    let done = false;
    const finish = () => { if (!done) { done = true; clearTimeout(timer); cancelAnimationFrame(raf); resolve(); } };
    const timer = window.setTimeout(finish, 250);
    const raf = requestAnimationFrame(finish);
  });
}

// A route-ready signal is emitted after Vue has committed the route and a
// browser paint opportunity has passed. Native can distinguish this from its
// older DOM-child-count probe and can accept route-degraded as a visible,
// bounded fallback state.
router.afterEach((to, _from, failure) => {
  if (failure) return;
  const epoch = ++routeEpoch;
  void (async () => {
    await nextTick();
    await waitForRenderFrame();
    if (epoch !== routeEpoch) return;
    const detail = { route: to.fullPath, routeName: String(to.name || ''), epoch, status: 'route-ready' } as const;
    window.dispatchEvent(new CustomEvent('route-ready', { detail }));
    clearRouteFallback();
    if (!initialRouteSettled) {
      initialRouteSettled = true;
      resolveInitialRoute(true);
    }
  })().catch((error) => {
    reportError(`route-ready:${to.fullPath}`, error);
    showRouteFallback('页面绘制未完成', to.fullPath);
    if (!initialRouteSettled) {
      initialRouteSettled = true;
      resolveInitialRoute(false);
    }
  });
});
window.addEventListener('error', (e) => {
  reportError('window.error', e.error ?? e.message);
});
window.addEventListener('unhandledrejection', (e) => {
  reportError('unhandledrejection', e.reason);
});

app.mount('#app');

// Acknowledgement is durable in this document as well as sent over IPC.
// Native's lost-message probe checks this exact navigation-bound receipt;
// DOM children alone are never readiness evidence.
async function signalInitialRenderReady(): Promise<void> {
  if (!isNativeRuntime) return;
  try {
    const startupWork = (async () => {
      const [routeReady, appReady] = await Promise.all([
        initialRouteReady,
        appStartupReady,
        // Editor popups are not a second main application.
        router.isReady().then(() => router.currentRoute.value.name === 'button-mapping-shortcuts'
          ? undefined : preloadStartupRoutes()),
      ]);
      await nextTick();
      const pageReady = await initialPageWork.settle();
      return routeReady && appReady && pageReady;
    })();
    const ready = await settleStartup(startupWork, 30000);
    if (!ready) {
      showRouteFallback('启动页面未能完成加载', router.currentRoute.value.fullPath, true, lastSettingsStartupError);
      reportError('startup.readiness', new Error('Route, page initialization or app startup failed/timed out'));
    }
    await nextTick();
    await waitForRenderFrame();
    await waitForRenderFrame();
    const content = document.querySelector('.app-content, .standalone-body');
    const contentReady = ready && !!content?.firstElementChild &&
      !document.getElementById('yemancc-route-fallback');
    if (!contentReady) {
      showRouteFallback('启动页面未能完成加载', router.currentRoute.value.fullPath, true, lastSettingsStartupError);
      await nextTick();
      await waitForRenderFrame();
    }
    const context = await invoke<{ generation?: string; navigationId?: string }>(
      'window.renderContext', {}, { timeoutMs: 1500 },
    );
    if (!context?.generation || !context?.navigationId || context.navigationId === '0') return;
    const receipt = {
      generation: context.generation,
      navigationId: context.navigationId,
      routeReady: contentReady,
      routeStatus: contentReady ? 'route-ready' : 'route-degraded',
      fallbackVisible: !!document.getElementById('yemancc-route-fallback'),
      routeEpoch,
    };
    (window as Window & { __YMCC_STARTUP_READY__?: typeof receipt }).__YMCC_STARTUP_READY__ = receipt;
    await invoke('window.renderReady', receipt, { timeoutMs: 1500 });
  } catch (error) {
    console.warn('[YeManCC] initial render-ready handshake failed', error);
    reportError('startup.handshake', error);
  }
}

void signalInitialRenderReady();
