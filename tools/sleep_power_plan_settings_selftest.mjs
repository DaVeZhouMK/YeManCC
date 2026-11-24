// Production settings, sleep admission and power-plan code; only native replies use memory.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';
import { runInNewContext } from 'node:vm';

const { build } = createRequire(resolve('package.json'))('esbuild');
const withoutGuard = process.argv.includes('--without-preference-guard');
const compiled = await build({
  stdin: { contents: `export * as repo from './src/bridge/settingsRepository';
    export { onSharedPowerLifecycle } from './src/bridge/powerLifecycleState';
    export { captureStaticWakeAdmission, optimizeSleepPowerPlans, setSleepPowerPlanOptimizationEnabled } from './src/bridge/yeman';`,
    resolveDir: process.cwd(), loader: 'ts' },
  bundle: true, write: false, metafile: true, format: 'cjs', platform: 'browser',
  define: { 'process.env.NODE_ENV': '"production"' },
  plugins: withoutGuard ? [{ name: 'remove-preference-guard', setup(builder) {
    builder.onLoad({ filter: /[\\/]src[\\/]bridge[\\/]yeman\.ts$/ }, ({ path }) => {
      const source = readFileSync(path, 'utf8');
      const guard = 'if (!cachedSleepPowerPlanOptimizationEnabled())';
      assert.equal(source.split(guard).length, 2);
      return { contents: source.replace(guard, 'if (false)'), loader: 'ts' };
    });
  } }] : [],
});

const scheme = '381b4222-f694-41f0-9685-ff5bb260df2e';
const directory = 'G:\\fixture-only\\PowerControl';
const file = `${directory}\\yeman-settings.json`;
const ready = { phase: 'ready', generation: 71, known: true, hardwareWritesAllowed: true,
  inputReady: true, publicationRevision: 15, observedNotificationSequence: 9, suspendSerial: 7, notificationPending: false };
function holdOnce() {
  const entered = Promise.withResolvers(), gate = Promise.withResolvers();
  return { entered: entered.promise, release: gate.resolve, async hold() { entered.resolve(); await gate.promise; } };
}

async function fixture() {
  const fx = { calls: [], writes: [], plan: new Map(), errors: [], encoded: '', commit: 'success' };
  const window = new EventTarget();
  const document = Object.assign(new EventTarget(), { visibilityState: 'visible' });
  let receive;
  async function dispatch({ cmd, args }) {
    switch (cmd) {
      case 'power.lifecycle': return ready;
      case 'settings.location': return { directory, file, backup: `${file}.bak` };
      case 'settings.read':
        assert.equal(args.path, file);
        return { stamp: fx.encoded, unchanged: args.stamp === fx.encoded, content: fx.encoded };
      case 'settings.write':
        assert.equal(args.path, file);
        assert.equal(args.baseline.sleep.sleepPowerPlanOptimizationEnabled, true);
        assert.equal(JSON.parse(args.content).sleep.sleepPowerPlanOptimizationEnabled, false);
        await fx.ackGate?.hold();
        if (fx.commit === 'success') fx.encoded = args.content;
        return { ok: fx.commit !== 'reject', content: fx.encoded };
      case 'power.activeScheme': return scheme;
      case 'registry.read': return fx.plan.get(`${args.path}|${args.name}`) ?? null;
      case 'shell.run': {
        assert.equal(args.program, 'powercfg');
        if (args.args[0] === '/list') {
          await fx.listGate?.hold();
          return { exitCode: 0, stdout: scheme, stderr: '' };
        }
        const [verb, target, sub, setting, value] = args.args;
        assert.ok(['/setacvalueindex', '/setdcvalueindex'].includes(verb));
        assert.equal(target, scheme);
        assert.equal(args.expectedPowerGeneration, ready.generation);
        assert.equal(args.expectedPowerNotificationSequence, ready.observedNotificationSequence);
        assert.equal(args.expectedPowerSuspendSerial, ready.suspendSerial);
        assert.equal(args.requireSleepPowerPlanEnabled, true);
        fx.writes.push(args);
        const name = verb === '/setacvalueindex' ? 'ACSettingIndex' : 'DCSettingIndex';
        fx.plan.set(`SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\${scheme}\\${sub}\\${setting}|${name}`, Number(value));
        if (fx.writes.length === 1) await fx.firstWriteGate?.hold();
        return { exitCode: 0, stdout: '', stderr: '' };
      }
      default: throw new Error(`Unexpected native request: ${cmd}`);
    }
  }
  window.chrome = { webview: {
    addEventListener(type, listener) { assert.equal(type, 'message'); receive = listener; },
    postMessage(message) {
      fx.calls.push(message);
      queueMicrotask(async () => {
        const { id } = message;
        try { receive({ data: { id, result: await dispatch(message) } }); }
        catch (error) { fx.errors.push(error.message); receive({ data: { id, error: error.message } }); }
      });
    },
  } };
  const module = { exports: {} };
  runInNewContext(compiled.outputFiles[0].text, { module, exports: module.exports, window, document, console,
    setTimeout, clearTimeout, queueMicrotask, structuredClone, TextEncoder, TextDecoder, CustomEvent, AbortController });
  const api = module.exports;
  fx.encoded = JSON.stringify(api.repo.normalizeSettings({ sleep: { sleepPowerPlanOptimizationEnabled: true } }));
  const admitted = Promise.withResolvers();
  const stop = api.onSharedPowerLifecycle(state => { if (state.known && state.phase === 'ready') admitted.resolve(); });
  await admitted.promise;
  assert.equal((await api.repo.readSettingsSection('sleep')).sleepPowerPlanOptimizationEnabled, true);
  const admission = api.captureStaticWakeAdmission();
  assert.ok(admission);
  return { api, fx, stop, optimize: () => api.optimizeSleepPowerPlans({ ...admission, expectedPowerSuspendSerial: ready.suspendSerial, isCurrent: () => true }) };
}

const cases = [];
async function check(name, fn) {
  const f = await fixture();
  try {
    await fn(f);
    assert.deepEqual(f.fx.errors, []);
    cases.push({ name, passed: true, nativePowerWrites: f.fx.writes.length });
  } catch (error) {
    cases.push({ name, passed: false, nativePowerWrites: f.fx.writes.length, error: error.message });
  } finally {
    f.stop();
  }
}
const settingsCount = fx => fx.calls.filter(({ cmd }) => cmd.startsWith('settings.')).length;
const committedOff = fx => JSON.parse(fx.encoded).sleep.sleepPowerPlanOptimizationEnabled === false;

await check('OFF confirmed before the first dispatch', async ({ api, fx, optimize }) => {
  fx.listGate = holdOnce();
  const batch = optimize();
  await fx.listGate.entered;
  try { await api.setSleepPowerPlanOptimizationEnabled(false); }
  finally { fx.listGate.release(); }
  const settledSettings = settingsCount(fx);
  const result = await batch;
  assert.equal(committedOff(fx), true);
  assert.equal(fx.writes.length, 0);
  assert.equal(result.stopped, 'preference-disabled-or-unknown');
  assert.equal(settingsCount(fx), settledSettings);
});

await check('pending OFF admits one dispatch; confirmed OFF stops the rest', async ({ api, fx, optimize }) => {
  fx.ackGate = holdOnce();
  fx.firstWriteGate = holdOnce();
  const setOff = api.setSleepPowerPlanOptimizationEnabled(false);
  await fx.ackGate.entered;
  const batch = optimize();
  await fx.firstWriteGate.entered;
  const beforeAck = committedOff(fx);
  fx.ackGate.release();
  try { await setOff; }
  finally { fx.firstWriteGate.release(); }
  const settledSettings = settingsCount(fx);
  const result = await batch;
  assert.equal(beforeAck, false);
  assert.equal(committedOff(fx), true);
  assert.equal(fx.writes.length, 1);
  assert.equal(result.stopped, 'preference-disabled-or-unknown');
  assert.equal(settingsCount(fx), settledSettings);
});

for (const [commit, error] of [['reject', /统一配置写入失败/], ['stale-document', /回执未确认请求的更改/]]) {
  await check(`${commit} keeps the confirmed ON; no per-write settings IPC`, async ({ api, fx, optimize }) => {
    fx.commit = commit;
    await assert.rejects(api.setSleepPowerPlanOptimizationEnabled(false), error);
    const settledSettings = settingsCount(fx);
    const result = await optimize();
    assert.equal(committedOff(fx), false);
    assert.equal(result.ok, true);
    assert.equal(fx.writes.length, 12);
    assert.equal(settingsCount(fx), settledSettings);
  });
}

const report = { passed: cases.every(c => c.passed), withoutGuard, cases,
  bundleSha256: createHash('sha256').update(compiled.outputFiles[0].text).digest('hex'),
  compilerInputs: Object.keys(compiled.metafile.inputs), nativeServicesMemoryOnly: true, realPowerWrites: false };
console.log(JSON.stringify(report, null, 2));
if (process.env.YMCC_SLEEP_POWER_SETTINGS_REPORT) {
  writeFileSync(process.env.YMCC_SLEEP_POWER_SETTINGS_REPORT, `${JSON.stringify(report, null, 2)}\n`);
}
if (!report.passed) process.exitCode = 1;
