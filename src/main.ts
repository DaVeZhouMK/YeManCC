import { createApp } from 'vue';
import { createPinia } from 'pinia';
import App from './App.vue';
import { router } from './router';
import { useDebugStore } from './stores/debug';
import './styles/tokens.css';
import './styles/gamepad.css';

const app = createApp(App);
app.use(createPinia());
app.use(router);

// 安装桥日志总线：每条原生 API 调用的原始返回都会进入调试面板
useDebugStore().installLogSink();

// ── 全局错误护栏 ──
// JS 错误本身不会让 WebView2 进程崩溃，但未捕获的异常可能让某个异步
// 处理器停在中间状态（例如后续赋值未执行）。这里集中兜住，打印到控制台
// （WebView2 DevTools 可见）并写入调试总线，便于将来定位"静默坏状态"。
function reportError(where: string, err: unknown) {
  const msg = err instanceof Error ? `${err.message}\n${err.stack ?? ''}` : String(err);
  // eslint-disable-next-line no-console
  console.error(`[YeManCC] ${where}:`, msg);
  try {
    useDebugStore().pushLog({ dir: 'err', kind: where, raw: msg });
  } catch {
    /* 护栏自身不得再抛错 */
  }
}
window.addEventListener('error', (e) => {
  reportError('window.error', e.error ?? e.message);
});
window.addEventListener('unhandledrejection', (e) => {
  reportError('unhandledrejection', e.reason);
});

app.mount('#app');
