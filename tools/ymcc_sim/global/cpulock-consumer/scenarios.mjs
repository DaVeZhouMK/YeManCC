import assert from 'node:assert/strict';
import vm from 'node:vm';
import { MemoryIpc, POWER, SETTINGS_DIR } from './memory-ipc.mjs';
const clone = value => structuredClone(value);
const plain = value => JSON.parse(JSON.stringify(value));
const BASE = { locked: false, acLocked: false, dcLocked: false, acFreq: 0, dcFreq: 0, acAggr: 100, dcAggr: 100, acTurbo: true, dcTurbo: true };

function runtime(bundle, initial = {}) {
  const port = new MemoryIpc();
  const context = vm.createContext({ __ymccIpc: port, structuredClone, TextEncoder, TextDecoder },
    { name: 'cpulock-actual-ts-memory-ipc', codeGeneration: { strings: false, wasm: false } });
  vm.runInContext(bundle, context, { timeout: 5000, filename: 'actual-product-bundle.js' });
  const product = context.YmccCpuLockConsumer;
  product.setSettingsDirectory(SETTINGS_DIR);
  port.document = plain(product.normalizeSettings({}));
  port.document.cpu.lock = clone(initial);
  port.setPower('ac', POWER.freq, 3300);
  port.setPower('dc', POWER.freq, 2200);
  port.setPower('ac', POWER.min, 35);
  port.setPower('dc', POWER.min, 20);
  port.setPower('ac', POWER.turbo, 2);
  port.setPower('dc', POWER.turbo, 0);
  port.setPower('ac', POWER.sched, 30);
  port.setPower('dc', POWER.sched, 60);
  const calls = [];
  const api = Object.fromEntries(Object.entries(product.cpulock).map(([name, value]) => [name,
    typeof value !== 'function' ? value : (...args) => {
      const row = { operation: name, input: clone(args.filter(arg => typeof arg !== 'function')), functionArgument: args.some(arg => typeof arg === 'function') };
      calls.push(row);
      try {
        const result = value(...args);
        if (result && typeof result.then === 'function') return result.then(value => { row.output = clone(value); return value; }, error => { row.error = error.message; throw error; });
        row.output = typeof result === 'function' ? 'unsubscribe-function' : clone(result);
        return result;
      } catch (error) { row.error = error.message; throw error; }
    }]));
  return { api, port, product, calls, context };
}
const snapshot = r => plain(r.api.getCpuLock());
const batches = r => r.port.calls.filter(row => row.command === 'registry.writePowerBatch');
const writes = r => r.port.calls.filter(row => row.command === 'settings.write');
const reads = r => r.port.calls.filter(row => row.command === 'registry.read');

export async function runScenarios(bundle, spec) {
  const results = [];
  async function scenario(id, fn) {
    let r;
    const record = { id, inputOrigin: 'source-derived-fixture', status: 'RUNNING' };
    try {
      await fn((initial = {}) => (r = runtime(bundle, initial)), record);
      assert.equal(r.port.denied.length, 0, 'no unexpected external command may be swallowed');
      record.status = 'PASS';
    } catch (error) {
      record.status = 'FAIL';
      record.error = { name: error.name, code: error.code ?? null, message: error.message, stack: error.stack };
    }
    if (r) {
      record.calls = r.calls;
      record.ipc = r.port.calls;
      record.denied = r.port.denied;
      // Access the original getter for evidence without inflating operation counts.
      record.finalCache = plain(r.product.cpulock.getCpuLock());
      record.finalStoredLock = clone(r.port.document.cpu.lock);
      record.acceptedMemoryPowerBatches = clone(r.port.acceptedPowerBatches);
    }
    results.push(record);
  }
  await scenario('runtime-export-surface-and-default-copy', async create => {
    const r = create();
    assert.deepEqual(Object.keys(r.product.cpulock).sort(), [...spec.runtimeFunctions, ...spec.runtimeConstants].sort());
    assert.equal(r.api.CPU_LOCK_FILE, 'C:\\SOFT\\YeMan\\PowerControl\\cpu_lock.json');
    assert.deepEqual(snapshot(r), BASE);
    const copy = r.api.getCpuLock(); copy.acFreq = 9999;
    assert.equal(r.api.getCpuLock('ac').freq, 0);
    assert.equal(r.api.getCpuLockSide('dc').locked, false);
    assert.equal(r.api.isCpuLocked(), false);
    assert.equal(r.api.isCpuLocked('ac'), false);
    assert.equal(r.api.isCpuSideLocked('dc'), false);
    assert.equal(r.port.calls.length, 0, 'synchronous getters must not load settings or query hardware');
  });
  await scenario('legacy-migration-sanitizes-values', async create => {
    const r = create({ locked: true, acFreq: '2800', dcFreq: 1900, acAggr: null, dcAggr: 44, acTurbo: 0, dcTurbo: false });
    const loaded = await r.api.loadCpuLock();
    assert.deepEqual(plain(loaded), { ...BASE, locked: true, acLocked: true, dcLocked: true, dcFreq: 1900, dcAggr: 44, dcTurbo: false });
    assert.equal(writes(r).length, 1);
    assert.equal(r.port.document.cpu.lock.acLocked, true);
    loaded.dcFreq = 12;
    assert.equal(r.api.getCpuLock('dc').freq, 1900);
    await r.api.loadCpuLock();
    assert.equal(writes(r).length, 1, 'already migrated load must not rewrite the document');
  });
  await scenario('failed-legacy-migration-does-not-load-cache', async create => {
    const r = create({ locked: true, acFreq: 2600 }); let notifications = 0;
    r.api.onCpuLockUpdate(() => { notifications++; });
    r.port.writeMode = 'false';
    await assert.rejects(r.api.loadCpuLock(), /统一配置写入失败/);
    assert.deepEqual(snapshot(r), BASE);
    assert.equal(notifications, 0);
    assert.equal(r.port.document.cpu.lock.acLocked, undefined);
    r.port.writeMode = 'ok';
    await r.api.loadCpuLock();
    assert.equal(r.api.isCpuSideLocked('ac'), true);
    assert.equal(r.api.getCpuLockSide('ac').freq, 2600);
    assert.equal(notifications, 1);
  });
  await scenario('independent-lock-flags-override-legacy', async create => {
    const r = create({ ...BASE, locked: false, acLocked: false, dcLocked: true });
    await r.api.loadCpuLock();
    assert.equal(r.api.isCpuLocked(), true);
    assert.equal(r.api.isCpuSideLocked('ac'), false);
    assert.equal(r.api.isCpuLocked('dc'), true);
    assert.equal(r.api.getCpuLock().locked, true);
    assert.equal(writes(r).length, 0, 'explicit side flags do not require migration');
  });
  await scenario('observer-copy-error-isolation-and-unsubscribe', async create => {
    const r = create(BASE); let first = 0, second = 0; const observerValues = [];
    const stopFirst = r.api.onCpuLockUpdate(c => { first++; c.acFreq = 8888; throw new Error('MEMORY_OBSERVER_FAILURE'); });
    const stopSecond = r.api.onCpuLockUpdate(c => { second++; observerValues.push(c.acFreq); });
    await r.api.loadCpuLock();
    await r.api.setCpuLock('ac', { freq: 2600 });
    assert.equal(first, 2); assert.equal(second, 2);
    assert.deepEqual(observerValues, [0, 2600], 'each observer must receive its own detached snapshot');
    assert.equal(r.api.getCpuLockSide('ac').freq, 2600);
    stopFirst(); stopSecond();
    await r.api.clearCpuLock();
    assert.equal(first, 2); assert.equal(second, 2);
  });
  await scenario('legacy-set-both-sides-captures-submission', async create => {
    const r = create(BASE);
    const values = { acFreq: 3100, dcFreq: 1800, acAggr: 82, dcAggr: 35, acTurbo: true, dcTurbo: false };
    const pending = r.api.setCpuLock(values, false); values.acFreq = 9999;
    await pending;
    assert.deepEqual(snapshot(r), { ...BASE, locked: true, acLocked: true, dcLocked: true, acFreq: 3100, dcFreq: 1800, acAggr: 82, dcAggr: 35, acTurbo: true, dcTurbo: false });
    assert.equal(batches(r).length, 0);
  });
  await scenario('unlocked-side-update-is-ignored', async create => {
    const r = create({ ...BASE, locked: true, acLocked: true, dcFreq: 1700 });
    await r.api.loadCpuLock();
    await r.api.updateCpuLock('dc', { freq: 4500, aggr: 99, turbo: false });
    assert.equal(r.api.getCpuLockSide('dc').freq, 1700, 'unlocked DC must retain its saved frequency');
    assert.equal(writes(r).length, 0, 'an ignored update must not save');
    await r.api.updateCpuLock({ acFreq: 2700, dcFreq: 4700 });
    assert.equal(r.api.getCpuLock('ac').freq, 2700);
    assert.equal(r.api.getCpuLock('dc').freq, 1700);
    await r.api.updateCpuLockSide('ac', { aggr: 61 });
    assert.equal(r.api.getCpuLockSide('ac').aggr, 61);
  });
  await scenario('side-update-alias-and-clear-preserve-values', async create => {
    const r = create(BASE);
    await r.api.setCpuLock('ac', { freq: 2800, turbo: false });
    await r.api.setCpuLock('dc', { freq: 1600, aggr: 25 });
    await r.api.updateCpuLock({ acFreq: 2900, dcFreq: 1500 });
    await r.api.updateCpuLockSide('dc', { aggr: 41 });
    assert.equal(r.api.getCpuLock('dc').freq, 1500);
    await r.api.clearCpuLock('ac');
    assert.equal(r.api.isCpuLocked(), true);
    assert.equal(r.api.isCpuSideLocked('ac'), false);
    assert.equal(r.api.getCpuLockSide('ac').freq, 2900);
    await r.api.unlockCpuSide('dc');
    assert.equal(r.api.isCpuLocked(), false);
    assert.equal(r.api.getCpuLockSide('dc').aggr, 41);
    await r.api.setCpuLock({ acFreq: 3000, dcFreq: 1800 });
    await r.api.clearCpuLock();
    assert.deepEqual(snapshot(r), { ...BASE, acFreq: 3000, dcFreq: 1800, acTurbo: false, dcAggr: 41 });
  });
  await scenario('lock-side-without-values-uses-real-read-conversions', async create => {
    const r = create(BASE);
    await r.api.lockCpuSide('ac');
    assert.deepEqual(plain(r.api.getCpuLockSide('ac')), { locked: true, freq: 3300, aggr: 70, turbo: true });
    await r.api.lockCpuSide('dc');
    assert.deepEqual(plain(r.api.getCpuLockSide('dc')), { locked: true, freq: 2200, aggr: 40, turbo: false });
    assert.equal(reads(r).length, 16);
    assert.equal(batches(r).length, 0);
  });
  await scenario('missing-dc-values-fall-back-to-ac', async create => {
    const r = create(BASE);
    for (const setting of [POWER.freq, POWER.min, POWER.turbo, POWER.sched]) r.port.values.delete('dc:' + setting);
    await r.api.lockCpuSide('dc');
    assert.deepEqual(plain(r.api.getCpuLockSide('dc')), { locked: true, freq: 3300, aggr: 70, turbo: true });
    assert.equal(reads(r).length, 12, 'four absent DC settings must each fall back to AC');
  });
  await scenario('unsupported-reads-use-current-yeman-defaults', async (create, record) => {
    const r = create({ ...BASE, dcFreq: 1234 });
    r.port.powerReadMode = 'throw';
    await r.api.lockCpuSide('dc');
    assert.deepEqual(plain(r.api.getCpuLockSide('dc')), { locked: true, freq: 0, aggr: 100, turbo: true });
    record.observation = 'readSchemeIndex catches unsupported reads; readPowerParams returns defaults, not null. cpulock sideValues fallback is not reached by this real chain.';
    r.port.powerReadMode = 'missing';
    await r.api.lockCpuSide('ac');
    assert.deepEqual(plain(r.api.getCpuLockSide('ac')), { locked: true, freq: 0, aggr: 100, turbo: true });
  });
  await scenario('apply-no-lock-does-not-query-power', async create => {
    const r = create(BASE);
    assert.equal(await r.api.applyCpuLock(), false);
    assert.equal(reads(r).length, 0); assert.equal(batches(r).length, 0);
  });
  await scenario('apply-single-ac-preserves-dc', async create => {
    const r = create({ ...BASE, locked: true, acLocked: true, acFreq: 1800.8, acAggr: 79.6, dcFreq: 9999, dcAggr: 99 });
    const originalDc = r.port.getPower('dc', POWER.freq);
    assert.equal(await r.api.applyCpuLock(), true);
    assert.equal(batches(r).filter(row => row.args.valueName === 'DCSettingIndex').length, 0, 'AC-only lock must never submit a DC batch');
    assert.equal(batches(r).length, 2);
    assert.equal(reads(r).length, 8);
    for (const setting of [POWER.freq, POWER.freq2, POWER.freq3]) assert.equal(r.port.getPower('ac', setting), 1800);
    for (const setting of [POWER.sched, POWER.sched2, POWER.sched3]) assert.equal(r.port.getPower('ac', setting), 20);
    assert.equal(r.port.getPower('ac', POWER.turbo), 0, 'low frequency must suppress turbo even when cached true');
    assert.equal(r.port.getPower('ac', POWER.throttle), 1);
    assert.equal(r.port.getPower('ac', POWER.max), 100);
    assert.equal(r.port.getPower('ac', POWER.min), 35, 'single-side merge retains read-back minimum state');
    assert.equal(r.port.getPower('dc', POWER.freq), originalDc);
  });
  await scenario('apply-single-dc-preserves-ac', async create => {
    const r = create({ ...BASE, locked: true, dcLocked: true, dcFreq: 2800, dcAggr: 47, dcTurbo: false });
    assert.equal(await r.api.applyCpuLock(), true);
    assert.equal(batches(r).length, 2);
    assert.ok(batches(r).every(row => row.args.valueName === 'DCSettingIndex'));
    assert.equal(r.port.getPower('ac', POWER.freq), 3300);
    assert.equal(r.port.getPower('dc', POWER.freq3), 2800);
    assert.equal(r.port.getPower('dc', POWER.sched), 53);
    assert.equal(r.port.getPower('dc', POWER.min), 20);
  });
  await scenario('apply-both-uses-cache-without-power-read', async create => {
    const r = create(BASE);
    await r.api.setCpuLock({ acFreq: 3200, dcFreq: 1900, acAggr: 110, dcAggr: -5, acTurbo: true, dcTurbo: true }, true);
    assert.equal(reads(r).length, 0); assert.equal(batches(r).length, 4);
    assert.equal(r.port.getPower('ac', POWER.sched), 0); assert.equal(r.port.getPower('dc', POWER.sched), 100);
    assert.equal(r.port.getPower('ac', POWER.min), 100); assert.equal(r.port.getPower('dc', POWER.min), 0);
    assert.equal(r.port.getPower('ac', POWER.turbo), 2); assert.equal(r.port.getPower('dc', POWER.turbo), 0);
  });
  await scenario('explicit-lock-and-set-apply-options', async create => {
    const r = create(BASE);
    await r.api.lockCpuSide('ac', { freq: 2700, aggr: 64, turbo: false }, true);
    assert.equal(batches(r).length, 2);
    await r.api.setCpuLock('dc', { freq: 2100, aggr: 55, turbo: true }, true);
    assert.equal(batches(r).length, 6);
    assert.equal(r.port.getPower('dc', POWER.freq), 2100);
  });
  await scenario('failed-save-does-not-publish-cache', async (create, record) => {
    const r = create({ ...BASE, locked: true, acLocked: true, acFreq: 2400 });
    await r.api.loadCpuLock(); const before = snapshot(r); let notifications = 0;
    r.api.onCpuLockUpdate(() => { notifications++; });
    record.modes = [];
    for (const mode of ['false', 'rejected', 'throw', 'malformed']) {
      r.port.writeMode = mode;
      await assert.rejects(r.api.setCpuLock('ac', { freq: 4600 }));
      assert.deepEqual(snapshot(r), before, 'failed save must retain the previous published cache');
      assert.equal(r.port.document.cpu.lock.acFreq, 2400);
      assert.equal(notifications, 0);
      record.modes.push({ mode, promiseRejected: true, cacheUnchanged: true, notificationCount: notifications });
    }
    r.port.writeMode = 'ok'; await r.api.setCpuLock('ac', { freq: 2500 });
    assert.equal(r.api.getCpuLock('ac').freq, 2500, 'failed mutations must not poison the configuration queue');
    assert.equal(notifications, 1);
  });
  await scenario('stored-commit-receipt-controls-cache', async create => {
    const r = create(BASE);
    r.port.writeMode = 'committed-adjustment';
    await r.api.setCpuLock('ac', { freq: 4200 });
    assert.equal(r.api.getCpuLock('ac').freq, 2444, 'cache must reflect actual committed document returned by IPC');
    assert.equal(r.port.document.cpu.lock.acFreq, 2444);
  });
  await scenario('read-error-retains-cache-and-rejects', async create => {
    const r = create({ ...BASE, acFreq: 2300 });
    await r.api.loadCpuLock(); const before = snapshot(r);
    r.port.readMode = 'throw';
    await assert.rejects(r.api.loadCpuLock(), /MEMORY_SETTINGS_READ_FAILURE/);
    assert.deepEqual(snapshot(r), before);
    r.port.readMode = 'ok'; await r.api.loadCpuLock();
    assert.deepEqual(snapshot(r), before);
  });
  await scenario('generation-change-rejects-old-queue-and-recovers', async create => {
    const r = create(BASE);
    const stale = r.api.setCpuLock('ac', { freq: 4700 });
    r.product.clearSettingsCache();
    await assert.rejects(stale, /旧请求已取消/);
    assert.equal(writes(r).length, 0); assert.equal(r.api.isCpuLocked(), false);
    await r.api.setCpuLock('dc', { freq: 1500 });
    assert.equal(r.api.getCpuLockSide('dc').freq, 1500);
    assert.equal(r.api.isCpuSideLocked('ac'), false);
  });
  await scenario('queued-updates-are-serialized-and-owned', async create => {
    const r = create({ ...BASE, locked: true, acLocked: true, dcLocked: true });
    const patch = { freq: 2800 };
    const first = r.api.updateCpuLock('ac', patch);
    const second = r.api.updateCpuLockSide('dc', { freq: 1600 });
    patch.freq = 9999;
    await Promise.all([first, second]);
    assert.equal(r.api.getCpuLockSide('ac').freq, 2800);
    assert.equal(r.api.getCpuLockSide('dc').freq, 1600);
    assert.equal(writes(r).length, 2);
    assert.equal(JSON.parse(writes(r)[1].args.content).cpu.lock.acFreq, 2800);
  });
  for (const mode of ['rejected', 'throw']) {
    await scenario('power-batch-' + mode + '-is-best-effort', async (create, record) => {
      const r = create({ ...BASE, locked: true, acLocked: true, dcLocked: true, acFreq: 2700, dcFreq: 1800 });
      r.port.powerWriteMode = mode;
      assert.equal(await r.api.applyCpuLock(), true, 'current yeman best-effort behavior reports attempted application');
      assert.equal(batches(r).length, 4, 'a failed batch must not prevent later batches');
      assert.equal(r.port.acceptedPowerBatches.length, 0);
      assert.equal(r.port.getPower('ac', POWER.freq), 3300);
      record.observation = 'Bridge boolean true records best-effort completion; all memory native writes failed and hardware success is not proven.';
    });
  }
  return { schemaVersion: 1, status: results.every(row => row.status === 'PASS') ? 'PASS' : 'FAIL',
    scenarios: results, scenarioCount: results.length, failedScenarios: results.filter(row => row.status === 'FAIL').map(row => ({ id: row.id, error: row.error })),
    executedPublicFunctions: [...new Set(results.flatMap(row => row.calls ?? []).map(row => row.operation))].sort(),
    boundary: { transportReplaced: 'src/bridge/ipc.ts', productLogicReplaced: false,
      osOrNodeCapabilitiesExposedToProduct: [], realCpuWrites: 0, realRegistryCalls: 0, realShellCalls: 0, networkActions: 0,
      subscriptions: 0, actualWebViewTransport: false, nativeWriteImplementationExecuted: false, fullProductAcceptance: false } };
}
