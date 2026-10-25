/** Offline HTTP boundary around unmodified product consumers. */
import { AsyncLocalStorage } from 'node:async_hooks';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { createFanLogFixture } from '../fan-log-regression.mjs';

const hash = text => crypto.createHash('sha256').update(text).digest('hex');
export const SIMULATED_HANDSHAKE = Object.freeze({
  ok: true, supported: true, protocolVersion: 2, hostMode: 'source-derived-fixture',
  hardwareCapable: true, hardwareWritesEnabled: true, hardwareWritesObserved: false,
  fanRoute: 'GenericDuty', fanRouteWriteReady: true,
  deviceClass: 'HandheldCompanion.Devices.OneXPlayerX1Mini',
});

/** NoIOHost models the server only. Every client operation uses real HTTP TS. */
export function createFanSignalFixture({ repoRoot, handshake = {}, simulation = {}, runtimeContract = {} }) {
  const base = createFanLogFixture({ repoRoot, handshake });
  base.life.setConfig({ ...base.life.config,
    ...(runtimeContract.protocolVersion !== undefined ? { protocolVersion: runtimeContract.protocolVersion } : {}),
    ...(typeof runtimeContract.runtimeRoot === 'string' ? { hcRuntimeDirectory: runtimeContract.runtimeRoot } : {}),
    ...(typeof runtimeContract.hostPayloadRoot === 'string' ? { hostDirectory: runtimeContract.hostPayloadRoot } : {}),
  });
  const root = fs.realpathSync(repoRoot), require = createRequire(path.join(root, 'package.json'));
  const sourcePath = 'src/bridge/fanApi.ts', source = fs.readFileSync(path.join(root, sourcePath), 'utf8');
  const ts = require('typescript');
  const compiled = ts.transpileModule(source, { compilerOptions: {
    target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, esModuleInterop: true,
  }, fileName: path.join(root, sourcePath) }).outputText;
  let currentHandshake = structuredClone(handshake), stateTransportFailure = false;
  let generation = 0, powerPhase = 'ready', writesAllowed = true;
  const routes = new Map([
    ['/api/state', 'getState'], ['/api/open', 'open'], ['/api/open-events', 'openEvents'],
    ['/api/acquire-control', 'acquireControl'], ['/api/heartbeat', 'heartbeat'],
    ['/api/enable', 'enable'], ['/api/preset', 'applyPreset'], ['/api/disable', 'disable'],
    ['/api/restore', 'restoreOem'], ['/api/release-control', 'releaseControl'],
    ['/api/close', 'close'], ['/api/shutdown', 'shutdown'], ['/api/suspend', 'suspend'], ['/api/resume', 'resume'],
  ]);
  const inert = Object.fromEntries([...new Set(routes.values())].map(name => [name, base.host[name].bind(base.host)]));
  const owners = new AsyncLocalStorage();
  base.diagnostics.push = (...items) => Array.prototype.push.apply(base.diagnostics, items.map(item => ({ ...item, ownerEventId: owners.getStore() ?? null })));
  const requests = [], receipts = [], delays = new Map(Object.entries(simulation.delays ?? {}));
  const initialMissing = new Set(simulation.missingFeedback ?? []);
  const deny = label => { base.violations.push(label); throw new Error('OFFLINE_BOUNDARY_FORBIDDEN'); };
  base.api.http.request = async (url, options) => {
    const endpoint = String(url).replace('http://fan-log.invalid', '');
    if (!String(url).startsWith('http://fan-log.invalid/api/')) return deny('non-memory-http');
    const args = options.body ? JSON.parse(options.body) : {};
    const request = { ownerEventId: owners.getStore() ?? null, endpoint, method: options.method, atMs: base.clock.now,
      ...(args.nodes ? { nodes: structuredClone(args.nodes) } : {}) };
    requests.push(request);
    const delay = delays.get(endpoint) ?? (endpoint === '/api/handshake' ? 50 : 0);
    if (delay) await base.clock.sleep(delay);
    if (endpoint === '/api/state' && stateTransportFailure) {
      await base.clock.sleep(2048);
      throw new Error('HTTP request failed (WinHTTP 12029, phase=send)');
    }
    let payload;
    if (endpoint === '/api/handshake') { base.host.calls.push('handshake'); payload = structuredClone(currentHandshake); }
    else if (endpoint === '/api/cancel-control-intent') {
      base.host.calls.push('cancel-control-intent');
      base.host.lease = null;
      payload = { ok: true, controlIntentCancelled: true, state: base.host.snapshot() };
    } else {
      const name = routes.get(endpoint);
      if (!name || options.method !== (endpoint === '/api/state' ? 'GET' : 'POST')) return deny('unexpected-http-route');
      let value;
      if (name === 'enable') value = await inert[name](args.nodes, args.leaseId);
      else if (name === 'applyPreset') value = await inert[name](args.name, args.leaseId, args.nodes);
      else if (name === 'suspend' || name === 'resume') value = await inert[name](args);
      else value = await inert[name](args.leaseId);
      if (name === 'acquireControl') { value.leaseId = 'sim-lease-' + String(value.generation).padStart(4, '0'); base.host.lease = { ...value }; }
      if (name === 'acquireControl' || name === 'heartbeat') payload = { ok: true, lease: value };
      else if (name === 'shutdown') payload = { ok: true };
      else payload = { ok: true, state: value };
    }
    if (initialMissing.has(endpoint)) payload = {};
    if (simulation.invalidFeedback === endpoint) payload = { ok: true, state: { state: 'Ready', hardwareWritesEnabled: 'true' } };
    if (simulation.contradictoryState === endpoint && payload.state) payload.state.unknownState = true;
    receipts.push({ ownerEventId: owners.getStore() ?? null, endpoint, atMs: base.clock.now, present: !initialMissing.has(endpoint),
      state: typeof payload.state?.state === 'string' ? payload.state.state : null,
      oemRestoreConfirmed: payload.state?.oemRestoreConfirmed === true,
      hcVirtualCloseReturned: payload.state?.hcVirtualCloseReturned === true,
      hcDeviceManagerStopCompleted: payload.state?.hcDeviceManagerStopCompleted === true });
    return { status: 200, headers: '', bodyComplete: true, body: JSON.stringify(payload) };
  };
  // Share the already loaded production module and its actual log gate. A hand-written
  // diagnostics object can drift from new imports and reject valid fan control.
  const diagnostics = base.diagnosticsModule;
  if (simulation.diagnosticLoggingEnabled !== undefined) {
    if (typeof simulation.diagnosticLoggingEnabled !== 'boolean') throw new TypeError('DIAGNOSTIC_LOGGING_FLAG_INVALID');
    diagnostics.adoptFanDiagnosticLoggingState(simulation.diagnosticLoggingEnabled);
  }
  const module = { exports: {} }, epoch = Date.UTC(2026, 9, 18);
  class ClockDate extends Date {
    constructor(...args) { super(...(args.length ? args : [epoch + base.clock.now])); }
    static now() { return epoch + base.clock.now; }
  }
  const context = vm.createContext({ Date: ClockDate, Error, TypeError, RangeError, Promise,
    setTimeout: base.clock.setTimeout, clearTimeout: base.clock.clearTimeout,
    setInterval: base.clock.setInterval, clearInterval: base.clock.clearInterval,
  }, { codeGeneration: { strings: false, wasm: false } });
  const sourceRequire = specifier => specifier === './api' ? base.api
    : specifier === './fanDiagnostics' ? diagnostics : deny('unreviewed-http-import');
  new vm.Script(`(function(require,module,exports){\n${compiled}\n})`, { filename: path.join(root, sourcePath) })
    .runInContext(context)(sourceRequire, module, module.exports);
  const wire = new module.exports.HttpFanApiAdapter('http://fan-log.invalid');
  // Separate VM exports have distinct Error identities. Bridge only this exact
  // real API error type to the lifecycle's identical product Error definition.
  for (const name of ['handshake', ...new Set(routes.values())]) base.host[name] = async (...args) => {
    try { return await wire[name](...args); }
    catch (error) {
      if (error instanceof module.exports.FanApiError)
        throw new base.errorClass(error.message, error.status, error.errorCode, error.requestPath);
      throw error;
    }
  };
  // Replace the injected native-read port; these are ordinary TypeScript fields,
  // not rewritten production expressions. No native command can be dispatched.
  base.life.readNativePowerState = async () => ({ generation, phase: powerPhase,
    hardwareWritesAllowed: writesAllowed, inputReady: writesAllowed, resumeReady: writesAllowed, hibernateAvailable: true });
  return {
    ...base, requests, receipts,
    runAs(eventId, action) { return owners.run(eventId, action); },
    setHandshake(value) { currentHandshake = structuredClone({ ...value, ...(simulation.handshakeOverride ?? {}) }); },
    setStateTransportFailure(value) { stateTransportFailure = value; },
    setPower(phase, nextGeneration) {
      generation = nextGeneration; powerPhase = phase;
      writesAllowed = ['ready', 'resume-ready', 'resumed'].includes(phase);
      base.life.setPowerGeneration(nextGeneration);
      base.life.observePowerBoundary(phase, nextGeneration);
    },
    snapshot() { return { lifecycle: base.life.state, phase: base.life.phase,
      powerGeneration: base.life.coordinatorSnapshot.powerGeneration,
      hasControlIntent: base.life.hasControlIntent, controlReady: base.life.controlReady,
      leaseActive: Boolean(base.life.currentLease?.leaseId),
      ...base.stats(), simulatedServerState: base.host.stateName }; },
    sourceInputs() { return [...base.sourceInputs(), { path: sourcePath, sha256: hash(source) }]
      .filter((item, i, rows) => rows.findIndex(row => row.path === item.path) === i); },
    sourcesUnchanged() { return base.sourcesUnchanged() && hash(fs.readFileSync(path.join(root, sourcePath), 'utf8')) === hash(source); },
    dispose() { base.life.cancelRecovery('offline-fixture-disposal'); base.life.stopHeartbeat(); base.clock.tasks.clear(); },
  };
}


