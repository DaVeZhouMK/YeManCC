import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { transformSync } from 'esbuild';
import { StartupWorkBarrier, settleStartup } from '../src/robust/startupReadiness';
import { showRouteFallback, clearRouteFallback } from '../src/robust/routeFallback';

let checks = 0;
function check(ok: unknown, name: string): void { assert.ok(ok, name); checks++; }
function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: Error) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
const delay = (ms = 5) => new Promise<void>((resolve) => setTimeout(resolve, ms));
const source = readFileSync('src/main.ts', 'utf8');
const start = source.indexOf('async function signalInitialRenderReady()');
const end = source.indexOf('void signalInitialRenderReady();', start);
assert.ok(start >= 0 && end > start);
// Execute the actual production orchestration, not a second model of it.
const frameStart = source.indexOf('function waitForRenderFrame()');
const frameEnd = source.indexOf('// A route-ready signal', frameStart);
const code = transformSync(source.slice(frameStart, frameEnd) + source.slice(start, end), { loader: 'ts', target: 'es2020' }).code;
function fixture(options: { timeout?: number; content?: boolean; child?: boolean; ipcFail?: boolean; native?: boolean } = {}) {
  const route = deferred<boolean>(), app = deferred<boolean>(), chunks = deferred<void>();
  const page = deferred<void>();
  const barrier = new StartupWorkBarrier();
  void barrier.track(() => page.promise).catch(() => {});
  const state = { fallback: false, preloadCalls: 0, receipts: [] as any[], errors: [] as string[] };
  const win: any = { setTimeout };
  const doc = {
    querySelector: () => options.content === false ? null : { firstElementChild: {} },
    getElementById: () => state.fallback ? {} : null,
  };
  const deps = {
    isNativeRuntime: options.native !== false,
    lastSettingsStartupError: '',
    initialRouteReady: route.promise, appStartupReady: app.promise, initialPageWork: barrier,
    router: { isReady: () => route.promise, currentRoute: { value: {
      name: options.child ? 'button-mapping-shortcuts' : 'schedule', fullPath: '/schedule',
    } } },
    preloadStartupRoutes: () => { state.preloadCalls++; return chunks.promise; },
    nextTick: () => Promise.resolve(),
    settleStartup: (work: Promise<boolean>) => settleStartup(work, options.timeout ?? 1000),
    showRouteFallback: () => { state.fallback = true; },
    reportError: (where: string) => state.errors.push(where),
    invoke: async (cmd: string, receipt: any) => {
      if (cmd === 'window.renderContext') return { generation: '7', navigationId: '9007199254740999' };
      state.receipts.push(receipt);
      if (options.ipcFail) throw new Error('simulated lost acknowledgement');
      return { ok: true };
    },
    window: win, document: doc, routeEpoch: 1,
    requestAnimationFrame: (callback: () => void) => setTimeout(callback, 0),
    cancelAnimationFrame: clearTimeout, clearTimeout,
    console: { warn: () => {} },
  };
  const run = new Function(...Object.keys(deps), `${code}; return signalInitialRenderReady;`)(...Object.values(deps));
  const finish = () => { route.resolve(true); app.resolve(true); chunks.resolve(); page.resolve(); };
  return { route, app, chunks, page, state, win, run, finish };
}

async function main() {
  const a = fixture(); const run = a.run();
  a.route.resolve(true); a.app.resolve(true); await delay();
  check(a.state.receipts.length === 0, 'route+app do not bypass lazy chunks and first-page work');
  a.chunks.resolve(); await delay();
  check(a.state.receipts.length === 0, 'preloaded route is not async page-initialization completion');
  a.page.resolve(); await run;
  check(a.state.receipts[0]?.routeReady === true, 'all frontend prerequisites release');
  check(a.state.receipts[0]?.navigationId === '9007199254740999', '64-bit navigation identity stays string');
  check(a.win.__YMCC_STARTUP_READY__ === a.state.receipts[0], 'lost-message probe sees same durable receipt');

  const b = fixture(); const br = b.run();
  b.route.resolve(true); b.chunks.resolve(); b.page.resolve(); await delay();
  check(b.state.receipts.length === 0, 'page ready cannot bypass App startup');
  b.app.resolve(true); await br;
  check(b.state.receipts[0]?.routeReady === true, 'reverse signal ordering completes');

  const c = fixture({ timeout: 10 }); await c.run();
  check(c.state.fallback && c.state.receipts[0]?.routeStatus === 'route-degraded', 'timeout creates real failure UI');
  check(c.state.receipts[0]?.routeReady === false, 'timeout never reports normal readiness');
  c.finish(); await delay();
  check(c.state.receipts.length === 1 && c.state.fallback, 'late completion cannot remove failure or acknowledge twice');

  const d = fixture(); const dr = d.run(); d.route.resolve(true); d.app.resolve(true); d.page.resolve();
  d.chunks.reject(new Error('chunk missing')); await dr;
  check(d.state.fallback && d.state.receipts[0]?.fallbackVisible, 'failed route chunk is actionable failure, not blank content');

  const e = fixture({ content: false }); const er = e.run(); e.finish(); await er;
  check(e.state.fallback && !e.state.receipts[0]?.routeReady, 'empty content cannot report ready');

  const f = fixture({ ipcFail: true }); const fr = f.run(); f.finish(); await fr;
  check(f.win.__YMCC_STARTUP_READY__?.routeReady && f.state.errors.includes('startup.handshake'),
        'IPC delivery failure retains a probeable identity-bound receipt');

  const g = fixture({ child: true }); const gr = g.run(); g.route.resolve(true); g.app.resolve(true); g.page.resolve(); await gr;
  check(g.state.preloadCalls === 0 && g.state.receipts[0]?.routeReady, 'editor popup does not bootstrap main routes');
  const h = fixture({ native: false }); await h.run();
  check(h.state.receipts.length === 0 && h.state.preloadCalls === 0, 'browser preview sends no native readiness');

  const rejected = new StartupWorkBarrier(); await rejected.track(async () => { throw new Error('page'); }).catch(() => {});
  check(await rejected.settle() === false, 'page rejection is explicit failure');
  const sealed = new StartupWorkBarrier(); check(await sealed.settle(), 'empty page registry is ready');
  void sealed.track(() => new Promise<void>(() => {}));
  check(await settleStartup(sealed.settle(), 10), 'post-start activation does not reopen startup gate');

  // Render the production fallback against a tiny DOM double: verify it has
  // a real retry action and cannot disappear on a late route-afterEach.
  const elements = new Map<string, any>(); let reloads = 0;
  function element() {
    const e: any = { id: '', style: {}, dataset: {}, children: [], textContent: '',
      setAttribute() {}, append(...children: any[]) { this.children.push(...children); },
      appendChild(child: any) { this.children.push(child); elements.set(child.id, child); },
      replaceChildren() { this.children = []; }, remove() { elements.delete(this.id); } };
    return e;
  }
  (globalThis as any).document = { createElement: element, body: element(), getElementById: (id: string) => elements.get(id) };
  const listeners = new Map<string, (event: any) => void>();
  (globalThis as any).window = { location: { reload() { reloads++; } },
    addEventListener(name: string, cb: any) { listeners.set(name, cb); },
    removeEventListener(name: string) { listeners.delete(name); } };
  const appRoot = { inert: false }; elements.set('app', appRoot);
  showRouteFallback('startup', '/schedule', true); clearRouteFallback();
  const fallback = elements.get('yemancc-route-fallback');
  check(!!fallback, 'late route completion preserves startup failure UI');
  check(appRoot.inert, 'underlying controls are inert during failure');
  let stopped = false;
  listeners.get('ipc:gamepad.ui-input')?.({ detail: { action: 'page-next' }, stopImmediatePropagation() { stopped = true; } });
  check(stopped, 'LT/RB is consumed by failure surface, not hidden page');
  fallback.children[0].children[1].onclick();
  check(reloads === 1, 'fallback retry reloads document');
  elements.clear(); showRouteFallback('route', '/settings'); clearRouteFallback();
  check(!elements.has('yemancc-route-fallback'), 'ordinary route recovery still clears transient errors');
  check(!appRoot.inert && listeners.size === 0, 'ordinary recovery returns input ownership and inert state');

  const native = readFileSync('native/main.cpp', 'utf8');
  check(!native.includes('"dom-probe-fallback"'), 'legacy DOM-child bypass removed');
  check(native.includes('g_webviewReady || !ymcc::startupMayReveal('), 'native finalizer is idempotent and uses tested gate');
  check(native.includes('(!g_webviewReady || g_deferFirstShow ||'), 'cold minimized summon cannot expose native shadow');
  check(native.includes('receipt.value("navigationId", std::string{}) == std::to_string(navigationId)'), 'probe rejects stale navigation receipt');
  check(native.includes('g_ymccSlotHandoffPlanActive.load') && native.includes('recordStartupInputAttempt(startOk);'), 'startup observes real lifecycle outcome and handoff');
  console.log(`STARTUP_FRONTEND_PASS checks=${checks} hardwareOperations=0`);
}
void main().catch((error) => { console.error(error); process.exitCode = 1; });
