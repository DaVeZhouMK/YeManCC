import assert from 'node:assert/strict';
import {
  PersistentFrontendErrorLog,
  RouteReadinessCoordinator,
  TdpCapabilityCircuitBreaker,
  WebViewRecoveryCoordinator,
  chooseSoftwareRenderMode,
  recordHealthySoftwareBoot,
  recordSoftwareFailure,
  selectWebViewHostMode,
} from '../src/robust/repairModel';

async function main(): Promise<void> {
  const webview = new WebViewRecoveryCoordinator();
  assert.equal(webview.observe({ generation: 1, environmentId: 'env-a', browserProcessId: 10, kind: 'browser' }), 'start-recovery');
  assert.equal(webview.observe({ generation: 1, environmentId: 'env-a', browserProcessId: 10, kind: 'browser-exit' }), 'ignore-stale-generation');
  assert.equal(webview.observe({ generation: 1, environmentId: 'env-old', browserProcessId: 9, kind: 'browser' }), 'ignore-stale-generation');
  assert.equal(webview.snapshot().recoveryCount, 1);
  assert.equal(webview.markReady(1), false);
  assert.equal(webview.markReady(2), true);

  assert.equal(selectWebViewHostMode({ hasVirtualHostMapping: true, hasSecureResourceFallback: true }), 'virtual-host');
  assert.equal(selectWebViewHostMode({ hasVirtualHostMapping: false, hasSecureResourceFallback: true }), 'secure-resource-fallback');
  assert.equal(selectWebViewHostMode({ hasVirtualHostMapping: false, hasSecureResourceFallback: false }), 'repair-required');

  let now = 1000;
  const breaker = new TdpCapabilityCircuitBreaker(() => now, 5000);
  let writes = 0;
  const unsupportedWrite = () => breaker.run(async () => {
    writes += 1;
    throw new Error('TDP unsupported on this transport');
  });
  const first = await unsupportedWrite();
  const rest = await Promise.all(Array.from({ length: 7 }, () => unsupportedWrite()));
  assert.equal(first.applied, false);
  assert.equal(first.skipped, true);
  assert.equal(first.state, 'unsupported');
  assert.equal(rest.every((result) => result.skipped && result.state === 'unsupported'), true);
  assert.equal(writes, 1);

  const serial = new TdpCapabilityCircuitBreaker(() => now);
  let active = 0;
  let maxActive = 0;
  const serialWrite = () => serial.run(async () => {
    active += 1;
    maxActive = Math.max(maxActive, active);
    await Promise.resolve();
    active -= 1;
  });
  await Promise.all([serialWrite(), serialWrite(), serialWrite()]);
  assert.equal(maxActive, 1);

  const route = new RouteReadinessCoordinator();
  const oldToken = route.begin('/settings');
  const newToken = route.begin('/tdp');
  assert.equal(route.complete(oldToken, 'route-ready'), null);
  assert.deepEqual(route.complete(newToken, 'route-degraded'), { ...newToken, status: 'route-degraded' });

  let renderState = recordSoftwareFailure(null, '122.0.2365.106', 'browser/gpu crash', now);
  assert.equal(chooseSoftwareRenderMode(renderState, '122.0.2365.106', now), 'software');
  assert.equal(chooseSoftwareRenderMode(renderState, '123.0.0.0', now), 'default');
  assert.equal(chooseSoftwareRenderMode(renderState, '122.0.2365.106', now + 8 * 24 * 60 * 60 * 1000), 'default');
  renderState = recordHealthySoftwareBoot(renderState, 3)!;
  renderState = recordHealthySoftwareBoot(renderState, 3)!;
  assert.equal(recordHealthySoftwareBoot(renderState, 3), null);

  const memory = new Map<string, string>();
  const storage = {
    getItem: (key: string) => memory.get(key) ?? null,
    setItem: (key: string, value: string) => memory.set(key, value),
  };
  const logs = new PersistentFrontendErrorLog(storage, 'errors', 2);
  logs.append({ ts: now, where: 'router', message: 'https://app.local/?token=secret failed', route: '/settings?token=secret' });
  logs.append({ ts: now + 1, where: 'vue', message: 'second' });
  logs.append({ ts: now + 2, where: 'window', message: 'third' });
  const saved = logs.read();
  assert.equal(saved.length, 2);
  assert.equal(JSON.stringify(saved).includes('secret'), false);
  assert.equal(saved[0].route, undefined);

  const errorTypes = [
    'window.error',
    'unhandledrejection',
    'vue.render',
    'router.chunk',
    'ipc.timeout',
    'webview.recovering',
    'tdp.unsupported',
    'gpu.processfailed',
    'gpu.browser-process-exited',
  ];
  const typedMemory = new Map<string, string>();
  const typedLog = new PersistentFrontendErrorLog({
    getItem: (key: string) => typedMemory.get(key) ?? null,
    setItem: (key: string, value: string) => typedMemory.set(key, value),
  }, 'typed-errors', 20);
  for (const [index, type] of errorTypes.entries()) {
    typedLog.append({
      ts: now + index,
      where: type,
      message: `${type}: https://app.localhost/settings?token=secret`,
      stack: `Error: ${type}\n at https://app.localhost/settings?token=secret`,
      route: `/settings?token=secret`,
    });
  }
  const typed = typedLog.read();
  assert.deepEqual(typed.map((entry) => entry.where), errorTypes);
  assert.equal(JSON.stringify(typed).includes('secret'), false);

  console.log(JSON.stringify({
    ok: true,
    webviewRecoveryCount: webview.snapshot().recoveryCount,
    unsupportedTdpWrites: writes,
    maxConcurrentTdpWrites: maxActive,
    routeStatus: 'route-degraded',
    softwareModeRollback: 'runtime-change-or-healthy-boots',
    persistentLogEntries: saved.length,
    simulatedErrorTypes: typed.length,
  }, null, 2));
}

void main();
