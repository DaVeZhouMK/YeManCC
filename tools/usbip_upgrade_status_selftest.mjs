// Pure production consumer tests: memory IPC ports and a deterministic clock.
// Usage: node tools/usbip_upgrade_status_selftest.mjs --out <fresh-output-directory>
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { build, stop } from 'esbuild';
import { parse, compileScript, compileStyle } from 'vue/compiler-sfc';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
assert.equal(args.length, 2, 'Usage: --out <fresh-output-directory>');
assert.equal(args[0], '--out');
const out = path.resolve(args[1]);
assert(!fs.existsSync(out), 'Use a fresh output directory; retain earlier evidence');
assert(!out.startsWith(repo + path.sep), 'Keep evidence outside the shared source');
fs.mkdirSync(out, { recursive: true });
const receipt = { schemaVersion: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: 'Actual USBIP TypeScript consumer with memory IPC, deterministic timers and Vue compile checks',
  realYmcc: false, deviceWrites: false, steamUsed: false, fullPageNavigationGateRun: false, checks: [], sources: [] };
const save = () => fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(receipt, null, 2) + '\n');
const files = ['src/bridge/usbipUpgradeStatus.ts', 'src/components/UsbipUpgradeStatus.vue',
  'src/views/ButtonMappingView.vue', 'src/views/GyroMotionView.vue', 'src/views/SettingsView.vue', 'tools/usbip_upgrade_status_selftest.mjs',
  'tools/usbip_upgrade_status_browser_selftest.mjs'];
for (const file of files) {
  const bytes = fs.readFileSync(path.join(repo, file));
  receipt.sources.push({ file, sha256: crypto.createHash('sha256').update(bytes).digest('hex') });
  const target = path.join(out, 'source', file);
  fs.mkdirSync(path.dirname(target), { recursive: true }); fs.writeFileSync(target, bytes);
}
save();
let api;
const flush = async () => { for (let i = 0; i < 6; ++i) await Promise.resolve(); };
function snapshot(phase = 'checking', sequence = 1, driverSetupSession = 'A', extra = {}) {
  return { driverSetupSession, usbipUpgrade: { schemaVersion: 1, phase, installedVersion: '0.9.7',
    targetVersion: '0.9.8', detail: '', sequence, ...extra } };
}
function memory(options = {}) {
  let now = 0, serial = 0, visible = true;
  const timers = new Map(), listeners = new Set(), visibility = new Set(), reads = [], changes = [], callbacks = [];
  const controller = api.createUsbipUpgradeStatusController({
    now: () => now, progressTimeoutMs: options.progressTimeoutMs,
    read(signal) { let resolve, reject; const promise = new Promise((a, b) => { resolve = a; reject = b; });
      reads.push({ signal, resolve, reject }); return promise; },
    subscribe(fn) { listeners.add(fn); callbacks.push(fn); return () => listeners.delete(fn); },
    isVisible: () => visible,
    watchVisibility(fn) { visibility.add(fn); fn(visible); return () => visibility.delete(fn); },
    changed: value => changes.push(value),
    setTimer(fn, ms) { const id = ++serial; timers.set(id, { fn, at: now + ms }); return id; },
    clearTimer: id => timers.delete(id),
  });
  return { controller, timers, listeners, visibility, reads, changes, callbacks,
    get state() { return controller.getSnapshot(); },
    emit(value) { for (const fn of listeners) fn(value); },
    show(value) { visible = value; for (const fn of visibility) fn(value); },
    tick(ms) { now += ms; for (const [id, timer] of [...timers]) if (timer.at <= now) { timers.delete(id); timer.fn(); } },
    clean() { controller.stop(); assert.equal(timers.size, 0); assert.equal(listeners.size, 0); assert.equal(visibility.size, 0); },
  };
}
async function check(name, run) {
  try { await run(); receipt.checks.push({ name, status: 'PASS' }); }
  catch (error) { receipt.checks.push({ name, status: 'FAIL', error: error.stack }); throw error; }
  finally { save(); }
}
try {
  const built = await build({ entryPoints: [path.join(repo, 'src/bridge/usbipUpgradeStatus.ts')],
    bundle: true, write: false, platform: 'node', format: 'esm' });
  api = await import('data:text/javascript;base64,' + Buffer.from(built.outputFiles[0].text).toString('base64'));
  await check('All 14 native phases are accepted; complete and newer-preserved have truthful wording', () => {
    assert.equal(api.USBIP_UPGRADE_PHASES.length, 14);
    for (const phase of api.USBIP_UPGRADE_PHASES) {
      const result = api.parseUsbipUpgradeStatus(snapshot(phase)); assert.equal(result.valid, true, phase);
      assert.equal(result.status.phase, phase); assert(api.usbipUpgradePresentation(result.status).title);
    }
    const complete = api.usbipUpgradePresentation(api.parseUsbipUpgradeStatus(snapshot('complete')).status);
    assert.match(complete.title, /USBIP 配套升级完成/); assert.match(complete.description, /不代表虚拟手柄已经启动/);
    assert.match(api.usbipUpgradePresentation(api.parseUsbipUpgradeStatus(snapshot('newer-preserved')).status).description, /不降级/);
    for (const phase of ['current', 'not-installed']) assert.equal(api.usbipUpgradePresentation({ phase }).tone, 'quiet');
    assert.match(api.usbipUpgradePresentation({ phase: 'admin-required' }).description, /管理员/);
    assert.match(api.usbipUpgradePresentation({ phase: 'busy' }).description, /关闭相关 USBIP 软件/);
    assert.match(api.usbipUpgradePresentation({ phase: 'reboot-required' }).description, /重启 Windows/);
  });
  await check('Installed/readback-verified USBIP with uninstall restart requirement remains reboot-required', () => {
    const result = api.parseUsbipUpgradeStatus(snapshot('reboot-required', 8, 'native-session', {
      installedVersion: '0.9.8', targetVersion: '0.9.8', detail: '配套版本已安装通过读取核对，卸载要求重启',
    }));
    assert.equal(result.valid, true); assert.equal(result.status.phase, 'reboot-required');
    assert.equal(result.status.detail, '配套版本已安装通过读取核对，卸载要求重启');
    const presentation = api.usbipUpgradePresentation(result.status);
    assert.equal(presentation.tone, 'warning'); assert.match(presentation.description, /重启 Windows/);
    assert.doesNotMatch(presentation.title, /升级完成|已经启动/);
  });
  await check('Unknown phase, schema, envelope, versions, detail and sequence all fail closed', () => {
    const bad = [null, [], 12, 'complete', {}, { usbipUpgrade: {} }, snapshot('future-success'),
      snapshot('complete', 1, ''), snapshot('complete', 1, ' '), snapshot('complete', -1),
      snapshot('complete', 1.5), snapshot('complete', Number.MAX_SAFE_INTEGER + 1), snapshot('complete', NaN),
      snapshot('complete', 1, 'A', { schemaVersion: 2 }), snapshot('complete', 1, 'A', { detail: null }),
      snapshot('complete', 1, 'A', { detail: 'x'.repeat(4097) }), snapshot('complete', 1, 'A', { installedVersion: '' }),
      snapshot('current', 1, 'A', { targetVersion: '' }), snapshot('complete', 1, 'A', { targetVersion: {} })];
    for (const value of bad) { const parsed = api.parseUsbipUpgradeStatus(value); assert.equal(parsed.valid, false);
      assert.equal(parsed.status.phase, 'unverified'); assert(!parsed.status.detail.includes('升级完成')); }
  });
  await check('Legacy fallback only describes unknown status and never recommends web downloads', () => {
    for (const backendInstallPrompt of [1, 2]) { const p = api.parseUsbipUpgradeStatus({ backendInstallPrompt });
      assert.equal(p.status.phase, 'unverified'); assert.match(p.status.detail, /旧版后端/); assert.doesNotMatch(p.status.detail, /https|最新版|官网下载/); }
  });
  await check('Mount/activation are idempotent; initial read once; no idle polling', async () => {
    const m = memory(); m.controller.start(); m.controller.start(); assert.equal(m.reads.length, 1); assert.equal(m.listeners.size, 1);
    m.reads[0].resolve(snapshot('current')); await flush(); m.tick(60_000); assert.equal(m.reads.length, 1);
    assert.equal(m.timers.size, 0); m.clean(); m.controller.stop();
  });
  await check('Page/focus refresh repairs a stale failure without polling, and read-only old versions never look like installation', async () => {
    const m = memory(); m.controller.start();m.reads[0].resolve(snapshot('failed', 4));await flush();
    for(let i=0;i<100;i++)m.controller.refresh();await flush();assert.equal(m.reads.length,2);
    m.reads[1].resolve(snapshot('current',5,'A',{observationOnly:true}));await flush();assert.equal(m.state.phase,'current');assert.equal(api.usbipUpgradeHasError(m.state),false);
    m.controller.refresh();await flush();m.reads[2].resolve(snapshot('update-found',6,'A',{observationOnly:true}));await flush();assert.match(api.usbipUpgradePresentation(m.state).description,/未自动重试/);assert.equal(m.timers.size,0);m.tick(60000);assert.equal(m.reads.length,3);m.clean();
    m.controller.refresh();await flush();assert.equal(m.reads.length,3);
  });
  await check('1500ms timeout aborts owned work; late successful reply cannot claim completion', async () => {
    const m = memory(); m.controller.start(); m.tick(1499); assert.equal(m.reads[0].signal.aborted, false);
    m.tick(1); assert.equal(m.reads[0].signal.aborted, true); assert.match(m.state.detail, /1500ms/);
    m.reads[0].resolve(snapshot('complete')); await flush(); assert.equal(m.state.phase, 'unverified');
    m.tick(60_000); assert.equal(m.reads.length, 1); m.clean();
  });
  await check('New driverSetup event fences older initial read from another session', async () => {
    const m = memory(); m.controller.start(); m.emit(snapshot('installing', 8, 'B'));
    m.reads[0].resolve(snapshot('complete', 99, 'A')); await flush(); assert.equal(m.state.phase, 'installing');
    assert.equal(m.state.driverSetupSession, 'B'); m.clean();
  });
  await check('Duplicate live event fences reads and equal/lower sequences cannot regress phase', async () => {
    const m = memory(); m.controller.start(); m.reads[0].resolve(snapshot('verifying', 9)); await flush();
    m.show(false); m.show(true); m.emit(snapshot('verifying', 9));
    m.reads[1].resolve(snapshot('complete', 1, 'old-unseen-session')); await flush(); assert.equal(m.state.phase, 'verifying');
    m.emit(snapshot('installing', 8)); assert.equal(m.state.phase, 'verifying');
    m.emit(snapshot('complete', 9)); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('verifying', 9)); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('complete', 10)); assert.equal(m.state.phase, 'complete'); m.clean();
  });
  await check('Retired session cannot return through event or visibility query', async () => {
    const m = memory(); m.controller.start(); m.reads[0].resolve(snapshot('complete', 100)); await flush();
    m.emit(snapshot('checking', 0, 'B')); m.emit(snapshot('complete', 101, 'A'));
    assert.equal(m.state.driverSetupSession, 'B'); assert.equal(m.state.phase, 'checking');
    m.show(false); m.show(true); m.reads[1].resolve(snapshot('complete', 102, 'A')); await flush();
    assert.equal(m.state.phase, 'checking'); m.clean();
  });
  await check('Malformed newer data clears prior success; only newer coherent sequence recovers', async () => {
    const m = memory(); m.controller.start(); m.emit(snapshot('complete', 2));
    m.emit(snapshot('future-phase', 3)); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('complete', 2)); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('complete', 3)); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('current', 4)); assert.equal(m.state.phase, 'current');
    // Unattributed data cannot invalidate the verified current-session evidence.
    m.emit(null); assert.equal(m.state.phase, 'current'); m.clean();
  });
  await check('Visibility returns coalesce while a read is pending and do not poll', async () => {
    const m = memory(); m.controller.start();
    for (let i = 0; i < 5; i++) { m.show(false); m.show(true); }
    assert.equal(m.reads.length, 1); m.reads[0].resolve(snapshot()); await flush(); assert.equal(m.reads.length, 2);
    m.reads[1].resolve(snapshot('current', 2)); await flush(); m.show(true); assert.equal(m.reads.length, 2);
    m.tick(60_000); assert.equal(m.reads.length, 2); m.clean();
  });
  await check('Read rejection or timeout cannot erase a newer event', async () => {
    for (const timeout of [false, true]) { const m = memory(); m.controller.start(); m.emit(snapshot('complete', 8));
      if (timeout) m.tick(1500); else m.reads[0].reject(Error('memory bridge unavailable'));
      await flush(); assert.equal(m.state.phase, 'complete'); m.clean(); }
  });
  await check('Rejected and synchronously throwing read show unknown, with zero remaining timers', async () => {
    const m = memory(); m.controller.start(); m.reads[0].reject(Error('offline')); await flush();
    assert.equal(m.state.phase, 'unverified'); assert.equal(m.timers.size, 0); m.clean();
    let timer = null, state;
    const controller = api.createUsbipUpgradeStatusController({ read() { throw Error('sync'); }, subscribe: () => () => {},
      isVisible: () => true, watchVisibility: () => () => {}, changed: s => { state = s; },
      setTimer(fn) { timer = fn; return 1; }, clearTimer() { timer = null; } });
    controller.start(); assert.equal(state.phase, 'unverified'); assert.equal(timer, null); controller.stop();
  });
  await check('Deactivation/unmount release listeners, timers and read; stale callback cannot revive next activation', async () => {
    const m = memory(); m.controller.start(); const oldCallback = m.callbacks[0]; m.clean();
    assert.equal(m.reads[0].signal.aborted, true); m.controller.start();
    oldCallback(snapshot('complete', 100, 'old')); m.reads[0].resolve(snapshot('complete', 101, 'old')); await flush();
    assert.equal(m.state.phase, 'unverified'); m.reads[1].resolve(snapshot('checking', 1, 'new')); await flush();
    assert.equal(m.state.driverSetupSession, 'new'); m.clean(); m.show(false); m.show(true); assert.equal(m.reads.length, 2);
  });
  await check('Review counterexample 1: accepted checking/installing expire once to UI unknown without polling or native mutation', async () => {
    assert.equal(api.USBIP_UPGRADE_PROGRESS_TIMEOUT_MS, 180_000);
    for (const phase of ['checking', 'update-found', 'removing', 'installing', 'verifying']) {
      const m = memory(); m.controller.start();
      const original = { ...snapshot(phase, 2), driverSetup: 5, nativeDriver: 'untouched', state: 'ON', off: false };
      const before = JSON.stringify(original);
      m.reads[0].resolve(original); await flush(); assert.equal(m.timers.size, 1);
      m.tick(179_999); assert.equal(m.state.phase, phase);
      m.tick(1); assert.equal(m.state.phase, 'unverified'); assert.match(m.state.detail, /原任务仍在后台/);
      assert.equal(m.state.driverSetupSession, 'A'); assert.equal(m.state.sequence, 2);
      m.tick(86_400_000); assert.equal(m.state.phase, 'unverified'); assert.equal(m.timers.size, 0);
      assert.equal(m.reads.length, 1); assert.equal(JSON.stringify(original), before); m.clean();
    }
  });
  await check('Review counterexample 2: malformed packets cannot poison terminal recovery; authoritative same-sequence read repairs attributed damage', async () => {
    const m = memory(); m.controller.start(); m.reads[0].resolve(snapshot('complete', 2)); await flush();
    m.emit(null); assert.equal(m.state.phase, 'complete');
    m.emit(snapshot('future-phase', 2)); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('complete', 2)); assert.equal(m.state.phase, 'unverified');
    m.show(false); m.show(true); m.reads[1].resolve(snapshot('complete', 2)); await flush();
    assert.equal(m.state.phase, 'complete'); assert.equal(m.state.sequence, 2); m.clean();
  });
  await check('Review counterexample 3: invalid schema/new-session and missing session preserve verified identity and ordering', async () => {
    const m = memory(); m.controller.start(); m.reads[0].resolve(snapshot('complete', 2)); await flush();
    for (const packet of [null, {}, snapshot('complete', 999, ''), snapshot('complete', 999, 'A', { schemaVersion: 99 }),
      snapshot('complete', 999, 'B', { schemaVersion: 99 }), snapshot('future-phase', 999, 'B')]) {
      m.emit(packet); assert.equal(m.state.phase, 'complete'); assert.equal(m.state.driverSetupSession, 'A');
      assert.equal(m.state.sequence, 2);
    }
    m.show(false); m.show(true); m.reads[1].resolve(snapshot('complete', 3)); await flush();
    assert.equal(m.state.phase, 'complete'); assert.equal(m.state.sequence, 3);
    m.emit(snapshot('checking', 0, 'B')); assert.equal(m.state.driverSetupSession, 'B');
    m.emit(snapshot('complete', 1000, 'A')); assert.equal(m.state.driverSetupSession, 'B'); m.clean();
  });
  await check('Review counterexample 4: higher same-session read survives a duplicate live event', async () => {
    const m = memory(); m.controller.start(); m.reads[0].resolve(snapshot('installing', 2)); await flush();
    m.show(false); m.show(true); m.emit(snapshot('installing', 2));
    m.reads[1].resolve(snapshot('complete', 3)); await flush();
    assert.equal(m.state.phase, 'complete'); assert.equal(m.state.sequence, 3); assert.equal(m.timers.size, 0); m.clean();
  });
  await check('Repeated events and visibility replies cannot renew or revive an expired progress deadline', async () => {
    const m = memory({ progressTimeoutMs: 100 }); m.controller.start(); m.reads[0].resolve(snapshot('installing', 2)); await flush();
    m.tick(60); m.emit(snapshot('installing', 2)); m.show(false); m.show(true);
    m.reads[1].resolve(snapshot('installing', 2)); await flush();
    m.tick(39); assert.equal(m.state.phase, 'installing'); m.tick(1); assert.equal(m.state.phase, 'unverified');
    m.emit(snapshot('installing', 2)); assert.equal(m.state.phase, 'unverified');
    m.show(false); m.show(true); m.reads[2].resolve(snapshot('installing', 2)); await flush();
    assert.equal(m.state.phase, 'unverified'); assert.equal(m.timers.size, 0);
    m.emit(snapshot('verifying', 3)); assert.equal(m.state.phase, 'verifying'); assert.equal(m.timers.size, 1);
    m.tick(99); assert.equal(m.state.phase, 'verifying'); m.emit(snapshot('complete', 4));
    assert.equal(m.timers.size, 0); m.tick(1000); assert.equal(m.state.phase, 'complete'); m.clean();
  });
  await check('Stop clears the watchdog; activation preserves its absolute deadline and rejects old timer/event/read callbacks', async () => {
    const m = memory({ progressTimeoutMs: 100 }); m.controller.start();
    m.reads[0].resolve(snapshot('installing', 2)); await flush(); m.tick(60);
    const oldTimer = [...m.timers.values()][0].fn, oldEvent = m.callbacks[0]; m.clean(); m.tick(20); m.controller.start();
    oldTimer(); oldEvent(snapshot('complete', 999, 'old')); m.reads[1].resolve(snapshot('installing', 2)); await flush();
    m.tick(19); assert.equal(m.state.phase, 'installing'); m.tick(1); assert.equal(m.state.phase, 'unverified');
    m.clean(); m.tick(1000); m.controller.start(); m.reads[2].resolve(snapshot('installing', 2)); await flush();
    assert.equal(m.state.phase, 'unverified'); assert.equal(m.timers.size, 0); m.clean();
    const stopped = m.state; oldTimer(); assert.equal(m.state, stopped);
  });
  await check('New valid session owns its own watchdog; retired sessions cannot extend or complete it', async () => {
    const m = memory({ progressTimeoutMs: 100 }); m.controller.start(); m.reads[0].resolve(snapshot('installing', 10)); await flush();
    m.tick(80); m.emit(snapshot('checking', 0, 'B')); m.tick(20); assert.equal(m.state.phase, 'checking');
    m.emit(snapshot('complete', 100, 'A')); m.tick(79); assert.equal(m.state.phase, 'checking');
    m.tick(1); assert.equal(m.state.phase, 'unverified'); assert.equal(m.state.driverSetupSession, 'B'); m.clean();
  });
  await check('A stale same-sequence read cannot overwrite a newer live event, or repair damage across its event fence', async () => {
    const m = memory(); m.controller.start(); m.reads[0].resolve(snapshot('installing', 2)); await flush();
    m.show(false); m.show(true); m.emit(snapshot('complete', 3)); m.reads[1].resolve(snapshot('installing', 3)); await flush();
    assert.equal(m.state.phase, 'complete');
    m.show(false); m.show(true); m.emit(snapshot('future-phase', 3)); m.reads[2].resolve(snapshot('complete', 3)); await flush();
    assert.equal(m.state.phase, 'unverified');
    m.show(false); m.show(true); m.reads[3].resolve(snapshot('complete', 3)); await flush(); assert.equal(m.state.phase, 'complete'); m.clean();
  });
  await check('Inline pages ignore healthy, older-version and uncertain maintenance status; confirmed errors remain', () => {
    for (const phase of api.USBIP_UPGRADE_PHASES) {
      const result = api.parseUsbipUpgradeStatus(snapshot(phase));
      assert.equal(api.usbipUpgradeHasError(result.status), ['failed', 'admin-required', 'reboot-required'].includes(phase), phase);
    }
    assert.equal(api.usbipUpgradeHasError(api.unverifiedUsbipUpgradeStatus()), false);
  });
  await check('Real component and all three view SFCs compile; details are in active Settings device tab', () => {
    for (const file of files.filter(f => f.endsWith('.vue'))) {
      const source = fs.readFileSync(path.join(repo, file), 'utf8'); const { descriptor, errors } = parse(source, { filename: file });
      assert.deepEqual(errors, []); compileScript(descriptor, { id: 'usbip-proof', inlineTemplate: true });
      for (const style of descriptor.styles) assert.deepEqual(compileStyle({ source: style.content, filename: file,
        id: 'usbip-proof', scoped: style.scoped }).errors, []);
      if (file.includes('/views/')) {
        const settings = file.endsWith('SettingsView.vue');
        assert.equal(source.split(settings ? '<UsbipUpgradeStatus />' : '<UsbipUpgradeStatus errors-only />').length, 2);
        if (settings) assert.match(source, /<section v-if="active" class="card usbip-settings-card"/);
        assert.doesNotMatch(source, /usbipPrompt|backendInstallPrompt|usbip-prompt|USBIP_RELEASES_URL/); }
      else { assert.doesNotMatch(source, /gamepad\.state|setInterval|shell\.open|<button|<a\b|tabindex|data-gp-modal|position:\s*fixed/);
        assert.match(source, /on\('gamepad\.driverSetup'/); assert.match(source, /onDeactivated\(controller.stop\)/); }
    }
  });
  receipt.status = 'PASS';
} catch (error) { receipt.status = 'FAIL'; receipt.error = error.stack; process.exitCode = 1; }
finally { receipt.completedAt = new Date().toISOString(); save(); stop(); }
console.log(`${receipt.status}: ${receipt.checks.length} checks; ${out}`);
