/** Only WebView transport, clock and DOM event terminals are replaced. */
import vm from 'node:vm';
import { randomUUID } from 'node:crypto';

export class MemoryClock {
  now = Date.UTC(2026, 9, 22, 0, 0, 0);
  timers = new Map(); timerHistory = []; serial = 0;
  set = (fn, ms = 0) => { const id = ++this.serial; this.timers.set(id, { fn, at: this.now + Math.max(0, Number(ms) || 0) }); this.timerHistory.push({ id, fn }); return id; };
  clear = id => { this.timers.delete(id); };
  async flush() { for (let i = 0; i < 32; i++) await Promise.resolve(); }
  async advance(ms) {
    const target = this.now + ms; let turns = 0;
    for (;;) {
      await this.flush();
      const next = [...this.timers].filter(([, t]) => t.at <= target).sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
      if (!next) break;
      if (++turns > 2000) throw Error('MEMORY_CLOCK_RUNAWAY');
      this.now = Math.max(this.now, next[1].at); this.timers.delete(next[0]); next[1].fn();
    }
    this.now = target; await this.flush();
  }
}

class MemoryEventTarget {
  listeners = new Map(); history = [];
  addEventListener = (type, listener) => {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type).add(listener); this.history.push({ type, listener });
  };
  removeEventListener = (type, listener) => { this.listeners.get(type)?.delete(listener); };
  dispatchEvent = event => { for (const listener of [...(this.listeners.get(event.type) ?? [])]) listener(event); return true; };
  count() { return [...this.listeners.values()].reduce((n, set) => n + set.size, 0); }
}

export function createMemoryHost(bundle, policy = {}) {
  const clock = new MemoryClock(), dom = new MemoryEventTarget();
  const windows = new Map(), operations = new Map(), trace = [], nativeListeners = new Set(), held = [];
  let serial = 20, actionSerial = 0, generation = 1, lateCallbacksDelivered = 0, stoppedAt = null;
  const lease = { serial: 0, nonce: null, retired: true };
  const record = (type, data = {}) => { trace.push({ at: clock.now, type, ...structuredClone(data) }); };
  const response = (id, result, error) => {
    operations.delete(id); record('native.response', { requestId: id, result: result ?? null, error: error ?? null });
    for (const listener of nativeListeners) listener({ data: error === undefined ? { id, result } : { id, error } });
  };
  const emit = (event, data = {}) => { record('native.event', { event, data }); for (const listener of nativeListeners) listener({ data: { event, data } }); };
  const makeWindow = args => {
    const value = { id: ++serial, leaseId: args.leaseId, nonce: args.nonce, generation, ready: policy.deferReady !== true, healthy: true, visible: false };
    windows.set(value.id, value); record('native.window.created', value);
    if (value.ready) emit('monitorNotice.ready', { id: value.id, nonce: value.nonce }); return { id: value.id, nonce: value.nonce };
  };
  const finish = request => {
    const { id, cmd, args } = request;
    if (cmd === 'diagnostics.frontendTrace') { response(id, {ok:true}); return; } // Diagnostic terminal only; native persistence checked separately.
    if (cmd === 'monitorNotice.status') {
      const first = [...windows.values()][0]; response(id, { id: first?.id ?? 0, ...(first ? { nonce: first.nonce } : {}), available: policy.available !== false, leaseProtocol: 1, ...(first ? { leaseId: first.leaseId } : {}), ready: first?.ready === true, recoveryBlocked: policy.recoveryBlocked === true });
    } else if (cmd === 'monitorNotice.reserve') {
      if (windows.size) { response(id, null, 'Notice owner reservation rejected'); return; }
      lease.serial++; lease.nonce = args.nonce; lease.retired = false;
      record('native.lease.reserved', { leaseId: lease.serial, nonce: lease.nonce });
      response(id, { leaseId: lease.serial, nonce: lease.nonce });
    } else if (cmd === 'monitorNotice.create') {
      if (!Number.isSafeInteger(args.leaseId) || args.leaseId !== lease.serial || args.nonce !== lease.nonce || lease.retired) {
        record('native.lease.rejected-create', { leaseId: args.leaseId ?? null, nonce: args.nonce });
        response(id, null, 'Notice owner lease retired or replaced'); return;
      }
      response(id, makeWindow(args));
    } else if (cmd === 'monitorNotice.retire') {
      const obsolete = Number.isSafeInteger(args.leaseId) && args.leaseId > 0 && args.leaseId < lease.serial;
      const matched = args.leaseId === lease.serial && args.nonce === lease.nonce;
      if (!obsolete && !matched) { response(id, { leaseRetired: false, resourcesDestroyed: false, ownedOnly: true }); return; }
      if (matched) {
        lease.retired = true;
        if ([...windows.values()].some(value => value.leaseId !== args.leaseId || value.nonce !== args.nonce)) { response(id, { leaseRetired: true, resourcesDestroyed: false, ownedOnly: true }); return; }
        record('native.lease.retired', { leaseId: args.leaseId, nonce: args.nonce });
        for (const value of [...windows.values()]) if (value.leaseId === args.leaseId && value.nonce === args.nonce) {
          windows.delete(value.id); record('native.window.destroyed', { id: value.id, leaseId: value.leaseId, nonce: value.nonce, ownerRetirement: true });
          emit('monitorNotice.closed', { id: value.id, nonce: value.nonce });
        }
        // A queued old lease has no native execution authority after the
        // retirement receipt. Its late transport callback is still delivered.
        for (const [pendingId, pending] of [...operations]) if (pendingId !== id && pending.args.nonce === args.nonce && ['monitorNotice.create', 'monitorNotice.close'].includes(pending.cmd)) {
          operations.delete(pendingId); record('native.operation.retired', { requestId: pendingId, cmd: pending.cmd, leaseId: args.leaseId });
        }
      }
      response(id, { leaseRetired: true, resourcesDestroyed: true, ownedOnly: true, obsolete });
    }
    else if (cmd === 'monitorNotice.close') {
      const value = windows.get(args.hudId), matched = !!value && value.nonce === args.nonce;
      if (matched) { if (value.leaseId === lease.serial && value.nonce === lease.nonce) lease.retired = true; windows.delete(value.id); record('native.window.destroyed', { id: value.id }); emit('monitorNotice.closed', { id: value.id, nonce: value.nonce }); }
      response(id, matched);
    } else if (cmd === 'monitorNotice.hide') {
      const value = windows.get(args.hudId); if (value && value.nonce === args.nonce) value.visible = false; response(id, !!value);
    } else if (cmd === 'monitorNotice.frame') {
      const value = windows.get(args.hudId), accepted = !!value && value.nonce === args.nonce && value.healthy;
      if (accepted) value.visible = true; response(id, accepted);
    } else throw Error('UNEXPECTED_NATIVE_COMMAND:' + cmd);
  };
  const webview = {
    addEventListener(type, listener) { if (type !== 'message') throw Error('UNEXPECTED_WEBVIEW_LISTENER'); nativeListeners.add(listener); },
    postMessage(raw) {
      const request = structuredClone(raw); if (typeof request.id !== 'number') throw Error('UNEXPECTED_NATIVE_EVENT');
      operations.set(request.id, request); record('native.request', { requestId: request.id, cmd: request.cmd, args: request.args });
      const decision = policy.decide?.(request, { windows, clock, trace }) ?? 'success';
      if (decision === 'hold') { held.push(request); return; }
      if (decision === 'false') { response(request.id, false); return; }
      if (decision === 'error') { response(request.id, null, 'injected transient ' + request.cmd); return; }
      finish(request);
    },
  };
  class CustomEvent { constructor(type, options = {}) { this.type = type; this.detail = options.detail; } }
  const MemoryDate = class extends Date { constructor(...args) { super(...(args.length ? args : [clock.now])); } static now() { return clock.now; } };
  const context = vm.createContext({ window: dom, document: { documentElement: { dataset: { theme: 'blue-black' } } },
    crypto: { randomUUID }, Date: MemoryDate, setTimeout: clock.set, clearTimeout: clock.clear,
    CustomEvent, structuredClone, queueMicrotask, console, URLSearchParams, AbortController });
  dom.chrome = { webview }; dom.setTimeout = clock.set; dom.clearTimeout = clock.clear;
  new vm.Script(bundle, { filename: 'actual-monitor-notice.bundle.js' }).runInContext(context, { timeout: 5000 });
  const api = context.NoticeLifecycleActual; api.setLogSink(entry => record('ipc.complete', entry)); const host = api.startMonitorNoticeHost();
  const preferences = { popupEnabled: true, ymccTemplate: 'A', ymcc: { opacity: .8, dragLocked: false } };
  function feedback(extra = {}) {
    const value = { schemaVersion: 1, id: 'fixture-feedback-' + ++actionSerial, actionId: 'display.brightnessAdjust', origin: 'controller-shortcut', state: 'applied',
      title: 'Brightness confirmed', message: '50%', occurredAtUnixMs: clock.now, expiresAtUnixMs: clock.now + 8000,
      powerGeneration: generation, value: { text: '50%', number: 50, unit: '%' }, presentation: { fullscreen: true, leftMonitor: true }, ...extra };
    emit(api.SHORTCUT_FEEDBACK_EVENT, value); return value;
  }
  function snapshot() {
    const inspected = structuredClone(host.inspect());
    return { at: clock.now, currentGeneration: generation, host: inspected,
      remainingOwnedResources: { windows: windows.size, timers: clock.timers.size, subscriptions: dom.count(),
        pendingIpc: trace.filter(t => t.type === 'native.request').length - trace.filter(t => t.type === 'ipc.complete').length, nativeOperations: operations.size, opening: Number(inspected.opening), ownerReferences: Number(inspected.owner !== null), stateEntries: inspected.state.seen },
      windows: [...windows.values()].map(value => ({ ...value })), calls: trace.filter(t => t.type === 'native.request').length,
      infrastructureExcluded: { moduleLifetimeWebViewMessageListeners: nativeListeners.size } };
  }
  function health(label) {
    const inspected = host.inspect(), owner = inspected.owner, resource = owner ? windows.get(owner.id) : null;
    const observed = { label, at: clock.now, healthObserved: true, currentGeneration: generation,
      observedGeneration: resource?.generation ?? null, enabled: inspected.enabled, ownerId: owner?.id ?? null,
      windows: windows.size, recovered: !!resource && resource.nonce === owner.nonce && resource.ready && resource.healthy && owner.ready && resource.generation === generation };
    record('independent.health', observed); return observed;
  }
  return { host, api, clock, trace, windows, operations, policy, preferences, feedback, emit, snapshot, health,
    async ready() { host.ready(preferences); await clock.flush(); },
    stop() { if (stoppedAt === null) stoppedAt = clock.now; record('host.stop'); host.stop(); },
    async lateReplies() { for (const request of held.splice(0)) { lateCallbacksDelivered++; record('late.native.reply.delivered', { cmd: request.cmd }); finish(request); await clock.flush(); } },
    externalLoss(reason = 'transient carrier loss') { for (const resource of [...windows.values()]) { windows.delete(resource.id); record('external.window.loss', { id: resource.id, reason }); emit('monitorNotice.closed', { id: resource.id, nonce: resource.nonce }); } },
    nextGeneration() { generation++; record('power.generation', { generation }); },
    deliverRetiredCallbacks() { for (const { type, listener } of dom.history.slice()) { lateCallbacksDelivered++; record('late.dom.callback.delivered', { event: type }); listener(new CustomEvent(type, { detail: type === 'ipc:input.shortcutFeedback' ? { schemaVersion: 0 } : {} })); } },
    deliverRetiredTimers() { const retired = clock.timerHistory.slice(); for (const timer of retired) { lateCallbacksDelivered++; record('late.timer.callback.delivered', { timerId: timer.id }); timer.fn(); } },
    theme() { dom.dispatchEvent(new CustomEvent('theme:changed')); dom.dispatchEvent(new CustomEvent('ui-settings:changed')); },
    count(cmd, afterIndex = 0) { return trace.slice(afterIndex).filter(t => t.type === 'native.request' && t.cmd === cmd).length; },
    lateCount() { return lateCallbacksDelivered; },
    async recoveryWindow(label, ms = 30000) { const observations = [health(label + ':start')]; for (const delay of [1000, 4000, ms - 5000]) { await clock.advance(delay); observations.push(health(label + ':observe')); } return observations; },
  };
}

