/** Log-derived front-end regression. Executes unmodified production TS in a VM.
 * The unavailable handshake models an observed refusal, not CLR initialization.
 * No settings, native IPC, network, Host process or shared Build output is used.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const defaultRepo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const sha256 = text => crypto.createHash('sha256').update(text).digest('hex');
const SOURCE_FILES = new Set([
  'src/bridge/fanHost.ts', 'src/bridge/fanApi.ts', 'src/bridge/fanDiagnostics.ts',
  'src/bridge/fanHost/startupConfiguration.ts', 'src/bridge/fanHost/deployment.ts',
  'src/bridge/fanHost/capabilityAdmission.ts',
  'src/bridge/fanSessionEvidence.ts', 'src/bridge/hardwareCoordinator.ts',
  'src/bridge/aiFanMock.ts', 'src/bridge/fanHostInstallerRepair.ts',
  'src/bridge/fanHostInstallerAsset.json', 'tools/fixtures/fan_no_io_host.ts',
]);
export const LOG_EVIDENCE = Object.freeze({
  archive: 'YeManCC-logs-20261018-222053-群友258V-风扇失败.zip',
  archiveSha256: '452b582fc60976646df48cec6237baea7e167f189b36c952cc1c0d9246d1a6e3',
  members: ['fan-api.log:629-649', 'yeman-fan-host-runtime.log:248-263'],
  nativeSha256: '40640411662b6cc5eb54efcb8f86ceeb5b3df1af187211efc2be22c4aa0e3361',
  hcSha256: '08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583',
  hostSha256: '611abc49bd4f40134f6c1a00ceddf875cbdbf8165b0f2704c6f8c0f1ae4d0f9c',
  scope: 'observed-handshake-and-front-end-refusal;not-clr-root-cause-reproduction',
});
const INITIALIZATION_OBSERVATION = 'TargetInvocationException -> TypeInitializationException: HandheldCompanion.Managers.ManagerFactory -> ArgumentException';
export const UNAVAILABLE_HANDSHAKE = Object.freeze({
  ok: true, supported: false, protocolVersion: 2, hostMode: 'real-hc-unavailable',
  hardwareCapable: false, hardwareWritesEnabled: false, hardwareWritesObserved: false,
  fanRouteCount: 70, recoveryReadbackRequired: true, reason: INITIALIZATION_OBSERVATION,
});
const CURVE = Object.freeze([0, 40, 70, 100].map(tempC => Object.freeze({ tempC, dutyPercent: 100 })));

/** Clock is local to the VM; never replace the Node process's timers or Date. */
export class VirtualClock {
  now = 0;
  sequence = 0;
  tasks = new Map();
  setTimeout = (fn, ms = 0, ...args) => this.add(fn, ms, 0, args);
  setInterval = (fn, ms = 0, ...args) => this.add(fn, ms, Math.max(1, ms), args);
  clearTimeout = id => { this.tasks.delete(id); };
  clearInterval = this.clearTimeout;
  add(fn, ms, interval, args) {
    assert.equal(typeof fn, 'function', 'VIRTUAL_TIMER_CALLBACK_REQUIRED');
    assert(Number.isFinite(Number(ms)), 'VIRTUAL_TIMER_DELAY_INVALID');
    const id = ++this.sequence;
    this.tasks.set(id, { id, at: this.now + Math.max(0, Number(ms)), interval, fn, args });
    return id;
  }
  sleep(ms) { return new Promise(resolve => this.setTimeout(resolve, ms)); }
  async drain() { for (let i = 0; i < 64; i++) await Promise.resolve(); }
  next() { return [...this.tasks.values()].sort((a, b) => a.at - b.at || a.id - b.id)[0]; }
  async advance(ms) {
    const until = this.now + ms;
    assert(Number.isFinite(until) && ms >= 0, 'VIRTUAL_TIME_INVALID');
    await this.drain();
    for (let count = 0; ; count++) {
      const task = this.next();
      if (!task || task.at > until) break;
      assert(count < 10000, 'VIRTUAL_TIMER_RUNAWAY');
      this.now = task.at;
      if (task.interval) task.at += task.interval; else this.tasks.delete(task.id);
      task.fn(...task.args);
      await this.drain();
    }
    this.now = until;
    await this.drain();
  }
  async outcome(promise) {
    let done = false, result;
    Promise.resolve(promise).then(value => { result = { value }; done = true; }, error => { result = { error }; done = true; });
    const deadline = this.now + 30000;
    for (let steps = 0; steps < 1000; steps++) {
      await this.drain();
      if (done) return result;
      const next = this.next();
      assert(next && next.at <= deadline, 'PRODUCTION_PROMISE_DID_NOT_SETTLE');
      await this.advance(next.at - this.now);
    }
    throw new Error('PRODUCTION_PROMISE_STEP_LIMIT');
  }
}

/** Imports only reviewed modules, without rewriting any product expression. */
export function createFanLogFixture({ repoRoot = defaultRepo, handshake = UNAVAILABLE_HANDSHAKE } = {}) {
  const root = fs.realpathSync(repoRoot), require = createRequire(path.join(root, 'package.json'));
  const ts = require('typescript'), clock = new VirtualClock(), diagnostics = [], violations = [], requests = [];
  const loaded = new Map(), modules = new Map();
  const deny = label => { violations.push(label); throw new Error('OFFLINE_BOUNDARY_FORBIDDEN'); };
  const blocked = label => new Proxy({}, { get: (_target, name) => () => deny(`${label}.${String(name)}`) });
  const ipc = {
    isNativeRuntime: true,
    async invoke(command, params) {
      if (command !== 'fanLog.write') return deny(`ipc.${command}`);
      diagnostics.push({ atMs: clock.now, event: params.event, details: params.details });
      return true;
    },
  };
  let failStateRead = false;
  const api = {
    app: blocked('app'), fs: blocked('fs'), proc: blocked('proc'), shell: blocked('shell'), powerLifecycle: blocked('power'),
    http: { async request(url, options) {
      const endpoint = String(url).replace('http://fan-log.invalid', '');
      requests.push({ endpoint, method: options.method, atMs: clock.now });
      if (url === 'http://fan-log.invalid/api/handshake' && options.method === 'POST') {
        await clock.sleep(50);
        return { status: 200, headers: '', bodyComplete: true, body: JSON.stringify(handshake) };
      }
      if (url === 'http://fan-log.invalid/api/state' && options.method === 'GET' && failStateRead) {
        await clock.sleep(2048);
        throw new Error('HTTP request failed (WinHTTP 12029, phase=send)');
      }
      return deny('unexpected-http-route');
    } },
  };
  const EpochDate = Date, epoch = Date.UTC(2026, 9, 18, 14, 20, 4, 383);
  class ClockDate extends EpochDate {
    constructor(...args) { super(...(args.length ? args : [epoch + clock.now])); }
    static now() { return epoch + clock.now; }
  }
  const context = vm.createContext({
    Date: ClockDate, performance: { now: () => clock.now },
    setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout,
    setInterval: clock.setInterval, clearInterval: clock.clearInterval,
    Error, TypeError, RangeError, Promise, URL,
    fetch: () => deny('fetch'), localStorage: blocked('settings'),
  }, { codeGeneration: { strings: false, wasm: false } });
  function load(relative) {
    const key = relative.replaceAll('\\', '/');
    if (key === 'src/bridge/api.ts') return api;
    if (key === 'src/bridge/ipc.ts') return ipc;
    if (key === 'src/bridge/fanFeature.ts') return { setFanNavigationControl() {} };
    if (!SOURCE_FILES.has(key)) return deny('unreviewed-product-module');
    if (modules.has(key)) return modules.get(key).exports;
    const absolute = fs.realpathSync(path.join(root, key));
    assert.equal(path.relative(root, absolute).replaceAll('\\', '/'), key, 'SOURCE_PATH_ESCAPED');
    const text = fs.readFileSync(absolute, 'utf8');
    loaded.set(key, { path: key, sha256: sha256(text) });
    if (key.endsWith('.json')) return JSON.parse(text);
    const compiled = ts.transpileModule(text, { compilerOptions: {
      target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, esModuleInterop: true,
    }, fileName: absolute }).outputText;
    const module = { exports: {} };
    modules.set(key, module);
    const localRequire = specifier => {
      if (specifier === 'vue') return require('vue');
      if (!specifier.startsWith('.')) return deny('external-product-module');
      let target = path.posix.normalize(path.posix.join(path.posix.dirname(key), specifier));
      if (!path.posix.extname(target)) target += '.ts';
      return load(target);
    };
    new vm.Script(`(function(require,module,exports){\n${compiled}\n})`, { filename: absolute })
      .runInContext(context)(localRequire, module, module.exports);
    return module.exports;
  }
  const product = load('src/bridge/fanHost.ts'), fanApi = load('src/bridge/fanApi.ts');
  const { NoIoHost } = load('tools/fixtures/fan_no_io_host.ts');
  load('src/bridge/fanDiagnostics.ts').adoptFanDiagnosticLoggingState(true);
  const host = new NoIoHost(), wire = new fanApi.HttpFanApiAdapter('http://fan-log.invalid');
  const inertState = host.getState.bind(host);
  host.handshake = async () => { host.calls.push('handshake'); return wire.handshake(); };
  host.getState = async () => {
    if (!failStateRead) return inertState();
    host.calls.push('state');
    return wire.getState();
  };
  let starts = 0, stops = 0, explicitRequests = 0, alive = null;
  const life = new product.FanHostLifecycle({
    enabled: true, adapter: host, heartbeatIntervalMs: 0, resumeWaitDeadlineMs: 400,
    config: {
      hostExecutable: 'memory:/fan.exe', hostDirectory: 'memory:/fan', hcRuntimeDirectory: 'memory:/hc',
      hcAssemblyPath: 'memory:/hc.dll', authorizationPath: 'memory:/authorization',
      allowHardwareWrites: false, confirmationToken: '', baseUrl: 'http://fan-log.invalid',
      protocolVersion: 2, dependencies: [], sessionToken: '', sessionTokenPath: 'memory:/unused',
    },
    readNativeActivity: async () => null,
    readNativePowerState: async () => ({ generation: 1, phase: 'ready', hardwareWritesAllowed: true, inputReady: true, resumeReady: true, hibernateAvailable: true }),
    requestManualWake: async () => deny('manual-wake'),
    launcher: {
      async start() {
        assert.equal(alive, null, 'SECOND_LIVE_HOST_FORBIDDEN');
        starts++;
        alive = { pid: 1591 + starts, processCreated: `inert-birth-${starts}`, executable: 'memory:/fan.exe' };
        return alive;
      },
      async stop(owner) {
        assert(alive && owner.pid === alive.pid && owner.processCreated === alive.processCreated && owner.executable === alive.executable, 'EXACT_OWNED_EXIT_REQUIRED');
        stops++; alive = null;
      },
    },
  });
  return {
    life, host, clock, diagnostics, violations, requests, api,
    errorClass: fanApi.FanApiError,
    diagnosticsModule: load('src/bridge/fanDiagnostics.ts'),
    async explicitAttempt() { explicitRequests++; return clock.outcome(life.apply(CURVE)); },
    async failedStateRead() { failStateRead = true; const result = await clock.outcome(life.getState()); failStateRead = false; return result; },
    stats: () => ({ starts, stops, explicitRequests, alive: alive !== null, state: life.state,
      curveWrites: host.calls.filter(call => call === 'enable' || call === 'preset').length,
      handshakes: host.calls.filter(call => call === 'handshake').length, pendingTimers: clock.tasks.size }),
    sourceInputs: () => [...loaded.values()].sort((a, b) => a.path.localeCompare(b.path)),
    sourcesUnchanged: () => [...loaded.values()].every(item => sha256(fs.readFileSync(path.join(root, item.path), 'utf8')) === item.sha256),
  };
}

function assertDenial(f, result, expectedStarts) {
  assert(result.error instanceof f.errorClass, 'EXPECTED_PRODUCTION_FAN_API_ERROR');
  assert.equal(result.error.errorCode, 'FAN_DEVICE_GATE_DENIED', 'EXPECTED_SOURCE_MAPPED_GATE_DENIAL');
  assert.equal(result.error.status, 409, 'EXPECTED_GATE_STATUS_409');
  assert.match(result.error.message, /ManagerFactory/, 'INITIALIZATION_OBSERVATION_LOST');
  assert.deepEqual(f.stats(), { starts: expectedStarts, stops: expectedStarts, explicitRequests: expectedStarts,
    alive: false, state: 'stopped', curveWrites: 0, handshakes: expectedStarts, pendingTimers: 0 }, 'DENIAL_MUST_RETIRE_ONLY_OWNED_CHILD_WITHOUT_WRITES');
  assert(!f.host.calls.some(call => ['open', 'events', 'acquire', 'enable', 'preset', 'resume'].includes(call)), 'DENIED_ATTEMPT_REACHED_CONTROL');
  const response = f.diagnostics.find(row => row.event === 'api.response' && row.details.path === '/api/handshake');
  assert(response && response.details.status === 200 && response.details.ok === true, 'PRODUCTION_HTTP_200_RECEIPT_REQUIRED');
  assert.equal(response.details.response.hostMode, 'real-hc-unavailable');
  for (const key of ['hardwareCapable', 'hardwareWritesEnabled', 'hardwareWritesObserved']) assert.equal(response.details.response[key], false, `OBSERVED_FALSE_FLAG_MISSING_${key}`);
  assert.equal(f.diagnostics.filter(row => row.event === 'lifecycle.handshake-retry').length, 0, 'DENIAL_MUST_NOT_RETRY_HANDSHAKE');
}
function safeTrace(rows) {
  const allowed = new Set(['api.response', 'api.transport-failure', 'lifecycle.state', 'lifecycle.start-device-gate-denied', 'lifecycle.start-child-failed-retryable']);
  return rows.filter(row => allowed.has(row.event)).map(({ atMs, event, details }) => {
    const value = { atMs, event };
    for (const key of ['state', 'generation', 'status', 'durationMs', 'method']) if (details[key] !== undefined) value[key] = details[key];
    if (['/api/handshake', '/api/state'].includes(details.path)) value.endpoint = details.path;
    if (details.response?.hostMode) value.hostMode = details.response.hostMode;
    if (event === 'lifecycle.start-device-gate-denied') value.initializationObservationRetained = /ManagerFactory/.test(details.reason ?? '');
    if (event === 'api.transport-failure') {
      const match = /WinHTTP (\d+), phase=([a-z]+)/.exec(details.error ?? '');
      if (match) { value.winHttpCode = Number(match[1]); value.transportPhase = match[2]; }
    }
    return value;
  });
}

export async function runFanLogRegression(options = {}) {
  const cases = [], inputs = new Map();
  for (const id of ['fan.hc-init-unavailable-258v', 'fan.post-denial-winhttp-12029']) {
    let f;
    try {
      f = createFanLogFixture(options);
      const primary = await f.explicitAttempt();
      assertDenial(f, primary, 1);
      let secondary = null;
      if (id === 'fan.post-denial-winhttp-12029') {
        const read = await f.failedStateRead();
        assert(read.error && /WinHTTP 12029, phase=send/.test(read.error.message), 'EXPECTED_POST_DENIAL_12029');
        const gateIndex = f.diagnostics.findIndex(row => row.event === 'lifecycle.start-device-gate-denied');
        const transportIndex = f.diagnostics.findIndex(row => row.event === 'api.transport-failure');
        assert(gateIndex >= 0 && transportIndex > gateIndex, 'PRIMARY_DENIAL_MUST_PRECEDE_TRANSPORT_FAILURE');
        const transport = f.diagnostics[transportIndex];
        assert.equal(transport.details.durationMs, 2048, 'EXPECTED_VIRTUAL_TRANSPORT_DURATION');
        assert.match(f.diagnostics[gateIndex].details.reason, /ManagerFactory/, 'PRIMARY_INITIALIZATION_REASON_LOST');
        assert.equal(primary.error.errorCode, 'FAN_DEVICE_GATE_DENIED', 'TRANSPORT_MUST_NOT_RECLASSIFY_CONTROL_REJECTION');
        secondary = { code: 12029, phase: 'send', durationMs: 2048, source: 'production-state-read-and-transport-diagnostic' };
      }
      await f.clock.advance(60000);
      assertDenial(f, primary, 1);
      const retry = await f.explicitAttempt();
      assertDenial(f, retry, 2);
      await f.clock.advance(60000);
      assertDenial(f, retry, 2);
      assert.deepEqual(f.violations, [], 'OFFLINE_BOUNDARY_VIOLATED');
      assert(f.sourcesUnchanged(), 'SOURCE_CHANGED_DURING_REGRESSION');
      cases.push({ id, passed: true, status: 'PASS', primaryFailure: { code: primary.error.errorCode, status: primary.error.status, source: 'production-lifecycle' },
        secondaryFailure: secondary, handshakeReceipt: { httpStatus: 200,
          ...Object.fromEntries(['hostMode', 'hardwareCapable', 'hardwareWritesEnabled', 'hardwareWritesObserved'].map(key =>
            [key, f.diagnostics.find(row => row.event === 'api.response').details.response[key]])) },
        observed: f.stats(), explicitRetry: 'reached-new-handshake;still-refused;no-corrected-device-success-claimed',
        quietWindowMs: 60000, forbiddenBoundaryCalls: f.violations.length, trace: safeTrace(f.diagnostics) });
    } catch (error) {
      cases.push({ id, passed: false, status: 'FAIL', reason: error instanceof assert.AssertionError ? error.message.split('\n')[0] : 'OFFLINE_REGRESSION_EXECUTION_FAILED',
        errorType: error?.constructor?.name ?? 'Error', observed: f?.stats(), forbiddenBoundaryCalls: f?.violations.length ?? 0 });
    } finally {
      if (f) for (const item of f.sourceInputs()) inputs.set(item.path, item);
    }
  }
  const failedCount = cases.filter(item => !item.passed).length;
  return {
    schemaVersion: 1, domain: 'fan', scope: 'behavioral-mock', status: failedCount ? 'FAIL' : 'PASS',
    passed: failedCount === 0, failedCount, exitCode: failedCount ? 2 : 0, cases, sourceEvidence: LOG_EVIDENCE,
    sourceInputs: [...inputs.values()].sort((a, b) => a.path.localeCompare(b.path)),
    productSourceExecuted: true, productionClasses: ['FanHostLifecycle', 'HttpFanApiAdapter'], fixture: 'NoIoHost',
    safety: { inMemoryIpc: true, virtualClock: true, localSettingsRead: false, realNetworkUsed: false, hostProcessStarted: false,
      hardwareAccessed: false, sharedBuildOutputWritten: false, operatingSystemSleepPerformed: false },
    limitations: ['No CLR ManagerFactory root cause is reproduced.', 'supported=false is the production refusal receipt contract, not an additional field recovered from the redacted log projection.',
      'Tests original control rejection and causal diagnostic order; no UI error-banner arbitration or corrected hardware success is claimed.',
      'Complements entity initialization/runtime-connection validation; does not replace it.'],
  };
}

export async function main(args = process.argv.slice(2)) {
  assert(args.length === 0 || (args.length === 2 && args[0] === '--output' && args[1] && !args[1].includes('\0')), 'EXPECTED_OPTIONAL_EXPLICIT_OUTPUT');
  const result = await runFanLogRegression();
  if (args.length) fs.writeFileSync(path.resolve(args[1]), JSON.stringify(result, null, 2) + '\n');
  return result;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().then(result => { console.log(JSON.stringify({ status: result.status, cases: result.cases.map(({ id, status }) => ({ id, status })) })); process.exitCode = result.exitCode; },
    () => { console.error('FAN_LOG_REGRESSION_ENTRY_FAILED'); process.exitCode = 2; });
}

