import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

// Executes complete production TS modules. Only time, DOM events, IPC and
// power-write dependencies are replaced. Never imports the App or native host.
const require = createRequire(import.meta.url);
const ts = require('typescript');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const evidence = path.resolve(root, '../../../Docs/Tasks/CPU/CoreScheduler-20261017');
const args = process.argv.slice(2);
function option(name, fallback) {
  const i = args.indexOf(name);
  if (i < 0) return fallback;
  assert(args[i + 1], `Missing value for ${name}`);
  return path.resolve(args[i + 1]);
}
const baselinePath = option('--baseline', path.join(evidence, 'entry/src/scheduler.ts'));
const candidatePath = option('--candidate', path.join(root, 'src/scheduler.ts'));
const outputPath = option('--output', path.join(evidence, 'selftest-results.json'));
assert(path.resolve(outputPath).startsWith(evidence + path.sep), 'Output must stay in the authorized evidence directory');
const digest = value => createHash('sha256').update(value).digest('hex');
const inputs = new Map();
function read(file) {
  const absolute = path.resolve(file);
  if (!inputs.has(absolute)) inputs.set(absolute, fs.readFileSync(absolute, 'utf8'));
  return inputs.get(absolute);
}
function compile(file) {
  const result = ts.transpileModule(read(file), {
    fileName: file,
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS },
    reportDiagnostics: true,
  });
  assert.equal(result.diagnostics?.filter(d => d.category === ts.DiagnosticCategory.Error).length, 0, file);
  return result.outputText;
}
const common = Object.fromEntries(['uiLifecycle', 'gamedetect', 'powerSource'].map(name => {
  const file = path.join(root, `src/bridge/${name}.ts`);
  return [name, { file, code: compile(file) }];
}));
const versions = {
  baseline: { file: baselinePath, code: compile(baselinePath) },
  current: { file: candidatePath, code: compile(candidatePath) },
};
const json = value => JSON.parse(JSON.stringify(value));
const flush = () => new Promise(resolve => setImmediate(resolve));
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

class Events {
  handlers = new Map();
  addEventListener(type, callback) {
    if (!this.handlers.has(type)) this.handlers.set(type, new Set());
    this.handlers.get(type).add(callback);
  }
  removeEventListener(type, callback) { this.handlers.get(type)?.delete(callback); }
  dispatchEvent(event) {
    for (const callback of [...(this.handlers.get(event.type) || [])]) {
      if (this.handlers.get(event.type)?.has(callback)) callback(event);
    }
    return true;
  }
}

function fixture(version, visible = true) {
  let now = 0, nextId = 0, documentState = visible ? 'visible' : 'hidden';
  let visibilityReads = 0, maxActiveTimers = 0;
  const timers = new Map(), timerOps = [], trace = [], ipcCalls = [], nativeHandlers = new Map();
  const window = new Events(), document = new Events(), powerModes = [];
  Object.defineProperty(document, 'visibilityState', { get() { visibilityReads++; return documentState; } });
  window.setTimeout = (fn, ms = 0) => {
    const id = ++nextId;
    const due = now + Math.max(0, Number(ms) || 0);
    timers.set(id, { fn, due });
    timerOps.push(['set', now, id, due]);
    maxActiveTimers = Math.max(maxActiveTimers, timers.size);
    return id;
  };
  window.clearTimeout = id => {
    timerOps.push(['clear', now, id, timers.has(id)]);
    timers.delete(id);
  };
  const context = vm.createContext({
    window, document, performance: { now: () => now }, queueMicrotask,
    CustomEvent: class { constructor(type, init = {}) { this.type = type; this.detail = init.detail; } },
    console: { ...console, warn: (...data) => trace.push(['warning', now, ...data.map(String)]) },
  });
  const modules = new Map();
  const f = {
    trace, timerOps, ipcCalls, powerModes, timers, window,
    invoke: async command => { throw new Error(`Unexpected inert IPC: ${command}`); },
    fallback: async () => { throw new Error('Unexpected power fallback'); },
    mark: (...data) => trace.push([now, ...data]),
    now: () => now,
    jump(to) { assert(to >= now); now = to; },
    event(type, detail) { window.dispatchEvent({ type, detail }); },
    document(visibleNow) {
      documentState = visibleNow ? 'visible' : 'hidden';
      document.dispatchEvent({ type: 'visibilitychange' });
    },
    native(type, detail) { for (const cb of [...(nativeHandlers.get(type) || [])]) cb(detail); },
    listenerCount(type) { return window.handlers.get(type)?.size || 0; },
    costs() {
      return {
        visibilityReads,
        timerSets: timerOps.filter(op => op[0] === 'set').length,
        timerClears: timerOps.filter(op => op[0] === 'clear').length,
        timerFires: timerOps.filter(op => op[0] === 'fire').length,
        maxActiveTimers,
      };
    },
    async advance(to) {
      assert(to >= now);
      let count = 0;
      await flush();
      for (;;) {
        const next = [...timers.entries()].sort((a, b) => a[1].due - b[1].due || a[0] - b[0])[0];
        if (!next || next[1].due > to) break;
        assert(++count < 10000, 'Unexpected timer/pump spin');
        now = Math.max(now, next[1].due);
        timers.delete(next[0]);
        timerOps.push(['fire', now, next[0], next[1].due]);
        next[1].fn();
        await flush();
      }
      now = to;
      await flush();
    },
    load(name) {
      if (modules.has(name)) return modules.get(name).exports;
      const source = name === 'scheduler' ? version : common[name];
      assert(source, `Unexpected module ${name}`);
      const module = { exports: {} };
      modules.set(name, module);
      const localRequire = specifier => {
        if (specifier === '@/scheduler') return f.load('scheduler');
        if (specifier.endsWith('/uiLifecycle')) return f.load('uiLifecycle');
        if (specifier === '../../PowerControl/GameTitlePolicy.json') return JSON.parse(read(path.join(root, 'PowerControl/GameTitlePolicy.json')));
        if (specifier === 'vue') return { ref: value => ({ value }), readonly: value => value };
        if (specifier === './tdpRuntimeState') return { tdpRuntimeStore: { powerMode: mode => powerModes.push([now, mode]) } };
        if (specifier === './yeman') return { detectPowerModeReliable: () => f.fallback() };
        if (specifier === './ipc') return {
          invoke(command, payload, options) {
            ipcCalls.push([now, command, json(payload || {}), json(options || {})]);
            return f.invoke(command, payload, options);
          },
          on(type, callback) {
            if (!nativeHandlers.has(type)) nativeHandlers.set(type, new Set());
            nativeHandlers.get(type).add(callback);
            return () => nativeHandlers.get(type).delete(callback);
          },
        };
        throw new Error(`Forbidden dependency ${specifier}`);
      };
      const wrapper = vm.runInContext(`(function(require,module,exports){\n${source.code}\n})`, context, { filename: source.file });
      wrapper(localRequire, module, module.exports);
      return module.exports;
    },
  };
  f.scheduler = f.load('scheduler');
  return f;
}

const cases = [];
const workload = [];
function scenario(name, run) { cases.push({ name, run }); }
function calls(f, name) { return f.trace.filter(row => row[1] === name).map(row => row[0]); }
function register(f, name, period, options = {}, run) {
  return f.scheduler.registerScheduledTask(name, period, run || (() => f.mark(name)), options);
}
function due(f) { return [...f.timers.values()].map(t => t.due).sort((a, b) => a - b); }

scenario('validation rejects bad names/periods without binding or arming', async f => {
  for (const period of [0, -1, NaN, Infinity, -Infinity]) assert.throws(() => register(f, 'bad', period));
  assert.throws(() => register(f, '', 500));
  assert.equal(f.listenerCount('ui:visibility'), 0);
  assert.equal(f.timers.size, 0);
});
scenario('empty scheduler stays silent', async f => {
  await f.advance(60000);
  assert.equal(f.costs().timerSets, 0);
  assert.equal(f.listenerCount('ui:visibility'), 0);
});
scenario('mixed declared periods retain exact 60-second rates', async f => {
  for (const [name, period] of [['a', 500], ['b', 1000], ['c', 1250], ['d', 2000]]) register(f, name, period);
  await f.advance(60000);
  for (const [name, count] of [['a', 120], ['b', 60], ['c', 48], ['d', 30]]) assert.equal(calls(f, name).length, count);
});
scenario('immediate vs delayed registration and fractional positive periods', async f => {
  register(f, 'immediate', 500, { runImmediately: true });
  register(f, 'fractional', 12.5);
  assert.deepEqual(calls(f, 'immediate'), [0]);
  await f.advance(50);
  assert.deepEqual(calls(f, 'fractional'), [12.5, 25, 37.5, 50]);
});
scenario('default hidden eligibility remains background-active', async f => {
  f.document(false);
  register(f, 'a', 500, { runImmediately: true });
  await f.advance(1500);
  assert.deepEqual(calls(f, 'a'), [0, 500, 1000, 1500]);
});
scenario('all hidden display tasks arm no timer, including immediate tasks', async f => {
  f.document(false);
  for (let i = 0; i < 3; i++) register(f, `display${i}`, 500, { pauseWhenHidden: true, runImmediately: true });
  await f.advance(60000);
  assert.equal(f.timers.size, 0);
  assert.equal(f.trace.length, 0);
});
scenario('hide/show wakes overdue displays once and preserves phase', async f => {
  register(f, 'display', 500, { pauseWhenHidden: true });
  await f.advance(500);
  f.event('ipc:window.hidden');
  await f.advance(2100);
  assert.deepEqual(calls(f, 'display'), [500]);
  f.event('ipc:window.shown');
  await flush();
  assert.deepEqual(calls(f, 'display'), [500, 2100]);
  assert.deepEqual(due(f), [2500]);
});
scenario('hidden background tasks retain original declared rate', async f => {
  register(f, 'display', 500, { pauseWhenHidden: true });
  register(f, 'background', 1250);
  f.event('ipc:window.minimized');
  await f.advance(5000);
  assert.deepEqual(calls(f, 'display'), []);
  assert.deepEqual(calls(f, 'background'), [1250, 2500, 3750, 5000]);
});
scenario('document/native/power gates compose, resuming remains paused', async f => {
  register(f, 'display', 500, { pauseWhenHidden: true });
  f.event('ipc:power.suspending');
  await f.advance(700);
  f.event('ipc:power.resuming');
  f.event('ipc:window.shown');
  await flush();
  assert.deepEqual(calls(f, 'display'), []);
  f.document(false);
  f.event('ipc:power.resumed');
  await flush();
  assert.deepEqual(calls(f, 'display'), []);
  f.document(true);
  await flush();
  assert.deepEqual(calls(f, 'display'), [700]);
});
scenario('visible-to-visible OS events do not reset scheduler timer', async f => {
  register(f, 'display', 500, { pauseWhenHidden: true });
  const before = f.timerOps.length;
  for (const e of ['ipc:window.shown', 'ipc:window.restored', 'ipc:window.maximized', 'ipc:window.summoned']) f.event(e);
  assert.equal(f.timerOps.length, before);
});
scenario('equal deadlines run in Map registration order', async f => {
  for (const name of ['z', 'a', 'm']) register(f, name, 500);
  await f.advance(500);
  assert.deepEqual(f.trace.map(row => row[1]), ['z', 'a', 'm']);
});
scenario('replacement retains Map position and old disposer cannot delete it', async f => {
  const old = register(f, 'same', 500);
  register(f, 'other', 500);
  register(f, 'same', 500, {}, () => f.mark('replacement'));
  old();
  await f.advance(500);
  assert.deepEqual(f.trace.map(row => row[1]), ['replacement', 'other']);
  assert.equal(f.scheduler.hasScheduledTask('same'), true);
});
scenario('disposal removes final timer and visibility subscription', async f => {
  const stop = register(f, 'a', 500);
  assert.equal(f.listenerCount('ui:visibility'), 1);
  stop(); stop();
  assert.equal(f.timers.size, 0);
  assert.equal(f.listenerCount('ui:visibility'), 0);
  f.event('ipc:window.hidden');
  f.event('ipc:window.shown');
  await f.advance(1000);
  assert.deepEqual(calls(f, 'a'), []);
});
scenario('explicit name removal and absent removal preserve current behavior', async f => {
  register(f, 'a', 500);
  f.scheduler.unregisterScheduledTask('missing');
  assert.deepEqual(due(f), [500]);
  f.scheduler.unregisterScheduledTask('a');
  assert.deepEqual(json(f.scheduler.getScheduledTaskNames()), []);
  assert.equal(f.timers.size, 0);
});
scenario('async work serializes every task and leaves no armed timer while pending', async f => {
  const gate = deferred();
  register(f, 'a', 500, { runImmediately: true }, async () => { f.mark('a-start'); await gate.promise; f.mark('a-end'); });
  register(f, 'b', 500, { runImmediately: true });
  await f.advance(900);
  assert.deepEqual(calls(f, 'b'), []);
  assert.equal(f.timers.size, 0);
  gate.resolve(); await flush();
  assert.deepEqual(calls(f, 'b'), [900]);
  assert.deepEqual(due(f), [1000]);
});
scenario('visibility is re-read after each await before a display task starts', async f => {
  const gate = deferred();
  register(f, 'blocker', 500, { runImmediately: true }, () => gate.promise);
  register(f, 'display', 500, { runImmediately: true, pauseWhenHidden: true });
  f.event('ipc:window.hidden');
  f.jump(200);
  gate.resolve(); await flush();
  assert.deepEqual(calls(f, 'display'), []);
  f.event('ipc:window.restored'); await flush();
  assert.deepEqual(calls(f, 'display'), [200]);
});
scenario('synchronous callback hide is observed by next pick in same pump', async f => {
  register(f, 'hider', 500, {}, () => { f.mark('hider'); f.event('ipc:window.hidden'); });
  register(f, 'display', 500, { pauseWhenHidden: true });
  await f.advance(500);
  assert.deepEqual(calls(f, 'display'), []);
});
scenario('microtask hide after a synchronous callback is observed after await', async f => {
  register(f, 'hider', 500, {}, () => { f.mark('hider'); queueMicrotask(() => f.event('ipc:window.hidden')); });
  register(f, 'display', 500, { pauseWhenHidden: true });
  await f.advance(500);
  assert.deepEqual(calls(f, 'display'), []);
});
scenario('disposing final pending task does not cancel its already-started work', async f => {
  const gate = deferred();
  const stop = register(f, 'a', 500, { runImmediately: true }, async () => { f.mark('start'); await gate.promise; f.mark('end'); });
  stop(); f.jump(900); gate.resolve(); await flush();
  assert.deepEqual(f.trace, [[0, 'start'], [900, 'end']]);
  assert.equal(f.timers.size, 0); assert.equal(f.listenerCount('ui:visibility'), 0);
});
scenario('pending task same-name replacement executes only new instance', async f => {
  const gate = deferred();
  const oldStop = register(f, 'same', 500, { runImmediately: true }, () => { f.mark('old'); return gate.promise; });
  await f.advance(100);
  register(f, 'same', 500, { runImmediately: true }, () => f.mark('new'));
  oldStop();
  gate.resolve(); await flush();
  assert.deepEqual(calls(f, 'new'), [100]);
  await f.advance(600);
  assert.deepEqual(calls(f, 'old'), [0]);
  assert.deepEqual(calls(f, 'new'), [100, 600]);
});
scenario('remove/rebind during pending await detaches old identity and subscription', async f => {
  const gate = deferred();
  const stop = register(f, 'old', 500, { runImmediately: true }, () => gate.promise);
  stop();
  assert.equal(f.listenerCount('ui:visibility'), 0);
  register(f, 'new', 500, { runImmediately: true });
  assert.equal(f.listenerCount('ui:visibility'), 1);
  gate.resolve(); await flush();
  assert.deepEqual(calls(f, 'new'), [0]);
});
scenario('reentrant immediate registration joins pump without overlap', async f => {
  register(f, 'a', 500, { runImmediately: true }, () => {
    f.mark('a-start');
    register(f, 'b', 500, { runImmediately: true });
    f.mark('a-end');
  });
  await flush();
  assert.deepEqual(f.trace.map(row => row[1]), ['a-start', 'a-end', 'b']);
});
scenario('callback unregisters another due task before its turn', async f => {
  register(f, 'a', 500, {}, () => f.scheduler.unregisterScheduledTask('b'));
  register(f, 'b', 500);
  await f.advance(500);
  assert.deepEqual(calls(f, 'b'), []);
});
scenario('throw/reject are isolated and retry at next declared slot', async f => {
  register(f, 'throw', 500, {}, () => { f.mark('throw'); throw new Error('expected-sync'); });
  register(f, 'reject', 500, {}, () => { f.mark('reject'); return Promise.reject(new Error('expected-async')); });
  register(f, 'ok', 500);
  await f.advance(1000);
  for (const name of ['throw', 'reject', 'ok']) assert.deepEqual(calls(f, name), [500, 1000]);
});
scenario('slow async task skips missed slots on original phase', async f => {
  const gate = deferred();
  register(f, 'slow', 500, {}, () => { f.mark('slow'); return gate.promise; });
  await f.advance(500);
  await f.advance(1300);
  gate.resolve(); await flush();
  assert.deepEqual(calls(f, 'slow'), [500]);
  assert.deepEqual(due(f), [1500]);
  await f.advance(1500);
  assert.deepEqual(calls(f, 'slow'), [500, 1500]);
});
scenario('completion exactly on next deadline skips that occupied slot', async f => {
  const gate = deferred();
  register(f, 'slow', 500, {}, () => gate.promise);
  await f.advance(500); await f.advance(1000);
  gate.resolve(); await flush();
  assert.deepEqual(due(f), [1500]);
});
scenario('late timer after long stall runs once without catch-up burst', async f => {
  register(f, 'a', 500);
  f.jump(10000); await f.advance(10000);
  assert.deepEqual(calls(f, 'a'), [10000]);
  assert.deepEqual(due(f), [10500]);
});
scenario('never-settling async task holds entire global pump (existing boundary)', async f => {
  register(f, 'hung', 500, { runImmediately: true }, () => new Promise(() => {}));
  register(f, 'later', 500, { runImmediately: true });
  f.event('ipc:window.hidden'); f.event('ipc:window.shown');
  await f.advance(60000);
  assert.deepEqual(calls(f, 'later'), []);
  assert.equal(f.timers.size, 0);
});
scenario('background-only scans perform no extra visibility reads', async f => {
  register(f, 'background', 500);
  const before = f.costs().visibilityReads;
  await f.advance(10000);
  assert.equal(f.costs().visibilityReads, before);
});
scenario('pending registration/removal always replans earliest deadline', async f => {
  register(f, 'late', 2000);
  f.jump(100);
  const early = register(f, 'early', 500);
  assert.deepEqual(due(f), [600]);
  early();
  assert.deepEqual(due(f), [2000]);
  await f.advance(2000);
  assert.deepEqual(calls(f, 'late'), [2000]);
});
scenario('real game-status module shares one task across distinct subscribers', async f => {
  f.invoke = async command => {
    assert.equal(command, 'game.detect');
    return { pid: 42, processCreated: '1234', name: 'game', title: 'Game', path: 'X:/game.exe' };
  };
  const game = f.load('gamedetect');
  const a = game.subscribeGameStatus(value => f.mark('subA', value?.pid || null));
  const b = game.subscribeGameStatus(value => f.mark('subB', value?.pid || null));
  await flush();
  assert.deepEqual(json(f.scheduler.getScheduledTaskNames()), ['game-status']);
  f.event('ipc:window.hidden');
  await f.advance(5000);
  assert.deepEqual(f.ipcCalls.map(row => row[0]), [0, 1250, 2500, 3750, 5000]);
  a(); assert(f.scheduler.hasScheduledTask('game-status'));
  b(); assert.equal(f.timers.size, 0);
});
scenario('game refresh in-flight merges event and timer queries, failure retains last game', async f => {
  let gate = deferred(), count = 0;
  f.invoke = async () => { count++; return gate.promise; };
  const game = f.load('gamedetect');
  const stop = game.subscribeGameStatus(value => f.mark('game', value?.pid || null));
  f.event('ipc:game.rules.changed');
  const refresh = game.refreshGameStatus();
  assert.equal(count, 1);
  gate.resolve({ pid: 42, processCreated: '1234', name: 'game' });
  await refresh; await flush();
  f.invoke = async () => { throw new Error('transport'); };
  await f.advance(1250);
  assert.deepEqual(f.trace.filter(row => row[1] === 'game').map(row => row[2]), [null, 42]);
  await assert.rejects(() => game.refreshGameStatusStrict());
  stop();
});
scenario('foreground power module is event-only and repeated show/focus shares pending read', async f => {
  const gate = deferred();
  f.invoke = async command => { assert.equal(command, 'power.sourceSnapshot'); return gate.promise; };
  const power = f.load('powerSource');
  const stop = power.startForegroundPowerSourceRefresh(mode => f.mark('power', mode));
  await flush();
  f.event('ipc:window.shown'); f.event('focus'); f.event('ipc:window.maximized');
  await flush();
  assert.equal(f.ipcCalls.length, 1);
  gate.resolve({ known: true, acLine: 1 }); await flush();
  await f.advance(60000);
  assert.equal(f.ipcCalls.length, 1);
  assert.equal(f.costs().timerSets, 0);
  assert.deepEqual(json(f.scheduler.getScheduledTaskNames()), []);
  assert.deepEqual(f.trace, [[0, 'power', 'ac']]);
  stop();
});
scenario('foreground power discards pre-hide answer and follows up once after show', async f => {
  const first = deferred(), second = deferred(); let count = 0;
  f.invoke = async () => (++count === 1 ? first.promise : second.promise);
  const stop = f.load('powerSource').startForegroundPowerSourceRefresh(mode => f.mark('power', mode));
  await flush();
  f.event('ipc:window.hidden'); f.event('ipc:power.resuming');
  f.jump(1000);
  f.event('ipc:window.shown'); f.event('ipc:power.resumed');
  first.resolve({ known: true, acLine: 0 }); await flush();
  assert.deepEqual(f.trace, []); assert.equal(count, 2);
  second.resolve({ known: true, acLine: 1 }); await flush();
  assert.deepEqual(f.trace, [[1000, 'power', 'ac']]);
  stop();
});
scenario('foreground power native notification supersedes pending snapshot', async f => {
  const gate = deferred(); f.invoke = async () => gate.promise;
  const stop = f.load('powerSource').startForegroundPowerSourceRefresh(mode => f.mark('power', mode));
  await flush(); f.native('power.sourceChanged', { ac: false });
  gate.resolve({ known: true, acLine: 1 }); await flush();
  assert.deepEqual(f.trace, []); assert.deepEqual(f.powerModes, [[0, 'dc']]);
  stop();
});
scenario('foreground power old-shell fallback/failure stays one-shot', async f => {
  f.invoke = async () => { throw new Error('old shell'); };
  f.fallback = async () => 'dc';
  const stop = f.load('powerSource').startForegroundPowerSourceRefresh(mode => f.mark('power', mode));
  await flush();
  assert.deepEqual(f.trace, [[0, 'power', 'dc']]);
  f.fallback = async () => { throw new Error('fallback failed'); };
  f.event('focus'); await flush(); await f.advance(60000);
  assert.equal(f.ipcCalls.length, 2); assert.equal(f.costs().timerSets, 0);
  stop();
});

// Current shared source has four direct registrations. The legacy six-task
// workload below is retained only as synthetic multi-display capacity evidence.
for (const mode of ['visible', 'hidden', 'hide-resume']) scenario(`60s CURRENT four-task operation evidence: ${mode}`, async f => {
  const specs = [
    ['game-status', 1250, false, true], ['topmon-read', 1000, true, true],
    ['game-input-overlay-retry', 2000, false, false], ['game-policy-runtime-retry', 2000, false, false],
  ];
  if (mode === 'hidden') f.document(false);
  for (const [name, period, pauseWhenHidden, runImmediately] of specs) register(f, name, period, { pauseWhenHidden, runImmediately });
  await flush();
  if (mode === 'hide-resume') {
    await f.advance(20000); f.event('ipc:window.hidden');
    await f.advance(40000); f.event('ipc:window.shown'); await flush();
  }
  await f.advance(60000);
  const counts = Object.fromEntries(specs.map(([name]) => [name, calls(f, name).length]));
  assert.equal(counts['game-status'], 49);
  assert.equal(counts['topmon-read'], mode === 'visible' ? 61 : mode === 'hidden' ? 0 : 42);
  assert.equal(counts['game-input-overlay-retry'], 30);
  assert.equal(counts['game-policy-runtime-retry'], 30);
  f.workload = { configuration: 'current-four-direct-registrations', mode, counts, costs: f.costs() };
});

// Synthetic capacity scenario: periods/options were observed at initial reads,
// but two display registrations were removed by a concurrent task before final QA.
for (const mode of ['visible', 'hidden', 'hide-resume']) scenario(`60s SYNTHETIC six-task capacity evidence: ${mode}`, async f => {
  const specs = [
    ['game-status', 1250, false, true], ['topmon-read', 1000, true, true],
    ['performance-schedule-status', 2000, true, true], ['performance-schedule-chart', 500, true, true],
    ['game-input-overlay-retry', 2000, false, false], ['game-policy-runtime-retry', 2000, false, false],
  ];
  if (mode === 'hidden') f.document(false);
  for (const [name, period, pauseWhenHidden, runImmediately] of specs) register(f, name, period, { pauseWhenHidden, runImmediately });
  await flush();
  if (mode === 'hide-resume') {
    await f.advance(20000); f.event('ipc:window.hidden');
    await f.advance(40000); f.event('ipc:window.shown'); await flush();
  }
  await f.advance(60000);
  const counts = Object.fromEntries(specs.map(([name]) => [name, calls(f, name).length]));
  if (mode === 'visible') assert.deepEqual(counts, {
    'game-status': 49, 'topmon-read': 61, 'performance-schedule-status': 31,
    'performance-schedule-chart': 121, 'game-input-overlay-retry': 30, 'game-policy-runtime-retry': 30,
  });
  if (mode === 'hidden') {
    for (const [name, , paused] of specs) assert.equal(counts[name], paused ? 0 : name === 'game-status' ? 49 : 30);
  }
  f.workload = { configuration: 'synthetic-six-tasks-not-current-production', mode, counts, costs: f.costs() };
});

// Many deterministic interleavings stress replacement, teardown, visibility and
// overdue timers. Every case compares *all* timer operations and callback times.
for (let seed = 1; seed <= 16; seed++) scenario(`seeded interleavings ${seed}`, async f => {
  let rng = seed;
  const rand = n => { rng = (Math.imul(rng, 1664525) + 1013904223) >>> 0; return rng % n; };
  const disposers = [];
  for (let i = 0; i < 160; i++) {
    const name = `t${rand(6)}`;
    switch (rand(8)) {
      case 0: case 1:
        disposers.push(register(f, name, [125, 500, 1000, 1250, 2000][rand(5)], { pauseWhenHidden: !!rand(2), runImmediately: !!rand(2) })); break;
      case 2: f.scheduler.unregisterScheduledTask(name); break;
      case 3: if (disposers.length) disposers[rand(disposers.length)](); break;
      case 4: f.document(!!rand(2)); break;
      case 5: f.event(rand(2) ? 'ipc:window.hidden' : 'ipc:window.shown'); break;
      case 6: f.event(['ipc:power.suspending', 'ipc:power.resuming', 'ipc:power.resumed'][rand(3)]); break;
      default: f.jump(f.now() + rand(3000));
    }
    await f.advance(f.now() + rand(750));
  }
  for (const name of f.scheduler.getScheduledTaskNames()) f.scheduler.unregisterScheduledTask(name);
  assert.equal(f.timers.size, 0); assert.equal(f.listenerCount('ui:visibility'), 0);
});

const results = [];
for (const test of cases) {
  const runs = {};
  for (const [versionName, version] of Object.entries(versions)) {
    const f = fixture(version);
    try {
      await test.run(f);
      assert(f.costs().maxActiveTimers <= 1, 'At most one outstanding timer');
      runs[versionName] = {
        trace: f.trace, timerOps: f.timerOps, ipcCalls: f.ipcCalls, powerModes: f.powerModes,
        names: json(f.scheduler.getScheduledTaskNames()), due: due(f), costs: f.costs(),
      };
      if (f.workload) workload.push({ version: versionName, ...f.workload });
    } catch (error) {
      throw new Error(`${test.name} [${versionName}]: ${error.stack}`, { cause: error });
    }
  }
  const behavior = ({ costs, ...rest }) => rest;
  assert.deepEqual(behavior(runs.current), behavior(runs.baseline), `${test.name}: changed observable behavior`);
  assert(runs.current.costs.visibilityReads <= runs.baseline.costs.visibilityReads, `${test.name}: extra visibility work`);
  results.push({ name: test.name, passed: true, baseline: runs.baseline.costs, current: runs.current.costs,
    behaviorSha256: digest(JSON.stringify(behavior(runs.current))),
    ...(!test.name.startsWith('seeded') ? { behavior: behavior(runs.current) } : {}),
  });
}

// AST evidence distinguishes frontend task registration from the hardware policy
// queue and App wiring. These files are inspected, never evaluated or imported.
const callsiteFiles = ['src/bridge/gamedetect.ts', 'src/bridge/powerSource.ts', 'src/bridge/performanceSchedule.ts', 'src/App.vue',
  'src/components/TopMonitorBar.vue', 'src/views/PerformanceScheduleView.vue', 'src/bridge/gameInputOverride.ts', 'src/bridge/gamePolicyRuntime.ts'];
const callsites = [];
for (const relative of callsiteFiles) {
  const file = path.join(root, relative), text = read(file);
  const match = relative.endsWith('.vue') ? /<script\b[^>]*>([\s\S]*?)<\/script>/.exec(text) : null;
  const code = match ? match[1] : text;
  const offset = match ? text.indexOf(code) : 0;
  const ast = ts.createSourceFile(relative + '.ts', code, ts.ScriptTarget.Latest, true);
  const calls = [];
  const interesting = new Set(['registerScheduledTask', 'subscribeGameStatus', 'startForegroundPowerSourceRefresh',
    'onUiVisibilityChange', 'restorePerformanceScheduleIfConfigured', 'refreshPerformanceScheduleCoreMode',
    'schedulePowerChangePerformanceRestore', 'withScheduleOp', 'startGamePolicyRuntime', 'startGameInputOverrideWatch']);
  function visit(node) {
    if (ts.isCallExpression(node) && interesting.has(node.expression.getText(ast))) {
      const position = offset + node.getStart(ast);
      calls.push({ function: node.expression.getText(ast), line: text.slice(0, position).split('\n').length,
        arguments: node.arguments.map(arg => arg.getText(ast)) });
    }
    ts.forEachChild(node, visit);
  }
  visit(ast);
  callsites.push({ file: relative, sha256: digest(text), calls });
}
for (const relative of ['src/bridge/powerSource.ts', 'src/bridge/performanceSchedule.ts', 'src/App.vue']) {
  assert.equal(callsites.find(row => row.file === relative).calls.filter(row => row.function === 'registerScheduledTask').length, 0);
}
const currentRegistrations = callsites.flatMap(row => row.calls.filter(call => call.function === 'registerScheduledTask').map(call => ({ file: row.file, ...call })));
assert.deepEqual(currentRegistrations.map(call => call.arguments[0]).sort(),
  ["'game-status'", "'topmon-read'", "'game-input-overlay-retry'", "'game-policy-runtime-retry'"].sort(),
  'Current four-task workload must match all inspected registration sites');
assert.equal(currentRegistrations.filter(call => /pauseWhenHidden:\s*true/.test(call.arguments[3] || '')).length, 1,
  'Current callsites must have one visibility-paused task');
assert(/const POLL_MS = 1250/.test(read(path.join(root, 'src/bridge/gamedetect.ts'))));
assert(/const POLL_MS = 1000/.test(read(path.join(root, 'src/components/TopMonitorBar.vue'))));
for (const [file, text] of inputs) assert.equal(fs.readFileSync(file, 'utf8'), text, `Source drift during test: ${file}`);
const report = {
  passed: results.length, perVersionRuns: results.length * 2, typescriptVersion: ts.version, nodeVersion: process.version,
  sources: [...inputs].map(([file, text]) => ({ file, sha256: digest(text) })),
  scope: 'Whole actual TS scheduler + uiLifecycle; actual gamedetect/powerSource in selected integration cases. Fake monotonic time/DOM events/IPC. No product/native host/device/real sleep. vue refs/tdpRuntimeStore/power fallback are inert stand-ins.',
  equivalence: 'All task order/timestamps, timer create-clear-fire trace, outstanding timer deadlines, registrations, IPC calls and power publications match baseline in every case. Only repeated visibility reads may differ.',
  limitations: ['Operation counts are not process CPU/energy or wall-clock latency measurements.',
    'Fake event loop does not validate browser clamp/throttling or real native suspend/resume.',
    'App.vue and performanceSchedule.ts call chains use AST/source inspection, not hardware policy execution.'],
  workload, callsites, results,
};
fs.mkdirSync(path.dirname(outputPath), { recursive: true });
fs.writeFileSync(outputPath, JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ passed: report.passed, perVersionRuns: report.perVersionRuns, workload, output: outputPath }, null, 2));
