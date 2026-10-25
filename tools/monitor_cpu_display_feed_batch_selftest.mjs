// Production TypeScript A/B regression. The baseline is the shared workspace
// before this task (not Git HEAD). Only the listener-array operation is counted;
// no wall-clock CPU claim, native IPC, application process or hardware access.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const root = process.cwd();
const { build } = createRequire(path.join(root, 'package.json'))('esbuild');
const sourcePath = path.join(root, 'src/bridge/monitorDisplayFeed.ts');
const currentSource = fs.readFileSync(sourcePath, 'utf8');
const baselineSource = String.raw`// Display-only gate: no reads, no polling, one latest value and at most one timer.
// Control/autofloat reads intentionally retain their own existing cadence.
import type { MonitorSnapshotView } from './monitorData';
export const MONITOR_DISPLAY_INTERVAL_MS = 1000;
export interface DisplayFeedDeps<T> {
  intervalMs?: number;
  now: () => number; // monotonic clock, not the snapshot's wall-clock timestamps
  key: (value: T) => string;
  unavailable?: (value: T) => boolean;
  setTimer: (fn: () => void, ms: number) => unknown;
  clearTimer: (id: unknown) => void;
}
export function createLatestDisplayFeed<T>(initial: T, deps: DisplayFeedDeps<T>) {
  const interval = Math.max(200, Math.min(1000, deps.intervalMs ?? MONITOR_DISPLAY_INTERVAL_MS));
  let latest = initial, latestKey = deps.key(initial), sentKey = latestKey;
  let latestDown = deps.unavailable?.(initial) === true, sentDown = latestDown;
  let lastSent = -Infinity, timer: unknown = null, timerAt = 0, token = 0, dispatching = false, disposed = false;
  // One identity per registration: an old disposer/batch must never own a
  // later registration of the same callback. No wrapper call on publication.
  const listeners = new Set<{ readonly listener: (value: T) => void }>();
  const cancel = () => { ++token; if (timer !== null) deps.clearTimer(timer); timer = null; timerAt = 0; };
  const notify = (listener: (value: T) => void, value: T) => { try { listener(value); } catch { /* isolate display consumers */ } };
  function arm(at: number) {
    if (timer !== null && timerAt <= at) return;
    cancel(); timerAt = at; const own = ++token;
    timer = deps.setTimer(() => { if (disposed || own !== token) return; timer = null; timerAt = 0; flush(); }, Math.max(1, Math.ceil(at - deps.now())));
  }
  function pending() {
    if (disposed || !listeners.size || latestKey === sentKey) return;
    const now = deps.now(), urgent = latestDown && !sentDown;
    if (Number.isFinite(now)) arm(now + (urgent ? 1 : Math.max(1, interval - (now - lastSent))));
  }
  function flush() {
    if (disposed || !listeners.size || latestKey === sentKey) { if (timer !== null) cancel(); return; }
    if (dispatching) { pending(); return; }
    const now = deps.now(), urgent = latestDown && !sentDown;
    if (!Number.isFinite(now)) return;
    const remaining = interval - (now - lastSent);
    if (!urgent && remaining > 0) { arm(now + Math.ceil(remaining)); return; }
    cancel(); const value = latest;
    sentKey = latestKey; sentDown = latestDown;
    // First outage clears immediately. Further unavailable frames share the
    // normal 1Hz budget; an outage cannot open another fresh-data budget.
    if (!urgent) lastSent = now;
    dispatching = true;
    try {
      // A coherent publication for existing consumers. Newly added consumers
      // get their initial cache once, not a second live-Set iteration replay.
      for (const subscription of Array.from(listeners)) {
        if (disposed) break;
        if (listeners.has(subscription)) notify(subscription.listener, value);
      }
    } finally { dispatching = false; }
    pending();
  }
  return {
    push(value: T) {
      if (disposed) return;
      const key = deps.key(value); if (key === latestKey) return;
      latest = value; latestKey = key; latestDown = deps.unavailable?.(value) === true;
      if (!listeners.size) return;
      if (latestKey === sentKey) { if (timer !== null) cancel(); return; }
      flush();
    },
    subscribe(listener: (value: T) => void) {
      if (disposed) return () => {};
      const wasEmpty = !listeners.size, subscription = { listener }; listeners.add(subscription);
      if (wasEmpty) { sentKey = latestKey; sentDown = latestDown; lastSent = latestDown ? -Infinity : deps.now(); }
      const wasDispatching = dispatching; dispatching = true;
      try { notify(listener, latest); } finally { dispatching = wasDispatching; }
      if (!dispatching) pending();
      let released = false;
      return () => { if (released) return; released = true; listeners.delete(subscription); if (!listeners.size && timer !== null) cancel(); };
    },
    dispose() { disposed = true; cancel(); listeners.clear(); },
    pending: () => timer !== null,
    advance: () => flush(),
  };
}
export function monitorSnapshotDisplayKey(value: MonitorSnapshotView | null): string {
  // Public callers may reuse a mutable view; never memoize its identity.
  if (value === null) return 'unavailable';
  const key = [value.session, value.generation, value.seq, value.source,
    value.hwDown, value.top?.hwDown, value.top?.ts, value.top?.cpuUsage, value.top?.cpuUsageValid, value.top?.cpuUsageTs, value.top?.coreUsageTs, value.fps?.ts, value.topActive, value.fpsActive,
    value.top?.batteryStatusTs, value.top?.batteryPowerTs, value.top?.batteryTimeTs,
    value.top?.batteryPowerValid, value.top?.batteryTimeValid, value.top?.batteryPowerEstimated, value.top?.batteryTimeEstimated,
    value.top?.fpsLimit, value.top?.fpsLimitValid, value.top?.fpsLimitTs, value.top?.fpsLimitSource,
    value.top?.cpuPowerValid, value.top?.cpuClockValid, value.top?.cpuTempValid,
    value.top?.cpuVoltageV, value.top?.gpuVoltageV,
    value.top?.gpuName, value.top?.gpuSelectionPinned, value.top?.gpuSelectionPresent, value.top?.gpuUsagePct, value.top?.gpuTempC, value.top?.gpuPowerW, value.top?.gpuClockMhz,
    value.top?.physicalMemoryUsedMb,value.top?.physicalMemoryAvailableMb,value.top?.physicalMemoryTotalMb,value.top?.physicalMemoryLoadPct,
    value.top?.gpuMemorySource,value.top?.gpuMemoryUsedMb,value.top?.gpuMemoryAvailableMb,value.top?.gpuMemoryTotalMb,value.top?.gpuMemoryLoadPct,
    value.top?.networkDownMBps,value.top?.networkUpMBps,value.top?.fileReadMBps,value.top?.fileWriteMBps,value.top?.fileSystemTs,value.top?.fileSystemSource,
    value.top?.fanEnabled, value.top?.fanSpeedPercent, value.top?.fanSpeedValid, value.top?.fanSpeedTs, value.top?.fanSpeedSource,
    value.top?.batteryPercent, value.top?.chargeW, value.top?.remainMin,
    value.top?.tdpW, value.top?.tempC, value.top?.freqMhz, value.top?.hasBattery, value.top?.ac,
    value.top?.batteryPowerSource, value.top?.batteryPowerWindowMs, value.top?.batteryTimeSource,
    value.fps?.fps, value.fps?.fps1, value.fps?.gpu, value.fps?.packagePower, value.fps?.game, value.fps?.pid,
    value.fps?.processCreated, value.fps?.fpsSource, value.fps?.fpsObservedAt, value.fps?.fpsValidUntil, value.fps?.fpsPollPeriodMs,
    value.fps?.hardwareObservedAt, value.fps?.hardwareValidUntil, value.fps?.hardwarePollPeriodMs,
    value.fps?.fps1Source, value.fps?.fps1ObservedAt, value.fps?.fps1ValidUntil, value.fps?.fps1PollPeriodMs,
    value.fps?.frametime, value.fps?.frameTimeSource, value.fps?.frameTimeTs, value.fps?.frameTimeValidUntil, value.fps?.frameTimePollPeriodMs].join('|');
  // Modern publications have an authoritative advancing sequence. The old
  // file/seq-zero path also needs bounded array contents, not only timestamps.
  if (value.source !== 'files' && value.seq > 0) return key;
  const arrayKey = (input: unknown, limit: number, tail = false): string => {
    if (!Array.isArray(input)) return '';
    const count=Math.min(input.length,limit), start=tail?input.length-count:0;
    let text=String(count);
    for(let i=0;i<count;++i){const item=input[start+i];text+='|'+(typeof item==='string'?JSON.stringify(item.slice(0,24)):typeof item==='number'&&Number.isFinite(item)?String(item):'');}
    return text;
  };
  return key+'|cores:'+arrayKey(value.top?.coreUsage,16)+'|labels:'+arrayKey(value.top?.coreLabels,16)+'|frames:'+arrayKey(value.fps?.frameTimes,120,true);
}
`;
const sha256 = text => crypto.createHash('sha256').update(text).digest('hex');
assert.equal(sha256(baselineSource), '0ecc5d736e5eeaaa231dd12d34471c033e15d3f541f9cddba33a7b9a0f6e55ff');
const baselineOnly = process.argv.includes('--baseline');
const counters = globalThis.__monitorCpuBatch = {};
const checks = [], evidence = {};
async function load(side, source) {
  const ref = `globalThis.__monitorCpuBatch.${side}`;
  counters[side] = { arrays: 0, entries: 0, snapshot(set) {
    ++this.arrays; this.entries += set.size; return Array.from(set);
  } };
  assert.equal(source.split('Array.from(listeners)').length, 2);
  const compiled = await build({ stdin: { contents: source.replace('Array.from(listeners)', `${ref}.snapshot(listeners)`),
    loader: 'ts', resolveDir: path.dirname(sourcePath) }, bundle: true, platform: 'node', format: 'esm', write: false });
  return import('data:text/javascript;base64,' + Buffer.from(compiled.outputFiles[0].text).toString('base64'));
}
const modules = { before: await load('before', baselineSource), after: await load('after', baselineOnly ? baselineSource : currentSource) };
function clock() {
  let now = 0, serial = 0;
  const timers = new Map(), stats = { creates: 0, clears: 0, runs: 0, maxTimers: 0 };
  return { timers, stats, now: () => now, set: at => { now = at; },
    setTimer(fn, ms) { ++stats.creates; timers.set(++serial, { fn, at: now + ms }); stats.maxTimers = Math.max(stats.maxTimers, timers.size); return serial; },
    clearTimer(id) { if (timers.delete(id)) ++stats.clears; },
    at(at) { now = at; for (let guard = 0; guard < 100; ++guard) {
      const due = [...timers].filter(([, timer]) => timer.at <= now);
      if (!due.length) return;
      for (const [id, timer] of due) if (timers.delete(id)) { ++stats.runs; timer.fn(); }
    } throw Error('unbounded timer loop'); },
  };
}
function harness(side, intervalMs = 1000) {
  counters[side].arrays = counters[side].entries = 0;
  const c = clock(), events = [], feed = modules[side].createLatestDisplayFeed(-1, {
    ...c, intervalMs, key: String, unavailable: value => value < 0,
  });
  return { c, feed, events, sub(name = 'a', callback) { return feed.subscribe(value => {
    events.push([name, c.now(), value]); callback?.(value);
  }); }, result() { return { events, timers: [...c.timers].map(([id, timer]) => [id, timer.at]), stats: { ...c.stats } }; } };
}
function check(name, run) { run(); checks.push(name); }
function compare(name, run) {
  check(name, () => { const pair = {}; for (const side of ['before', 'after']) {
    const h = harness(side); run(h, side); pair[side] = h.result();
  } assert.deepEqual(pair.after, pair.before); });
}
check('60-second two-lane 1Hz/5Hz workload retains all deliveries and timer operations', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    counters[side].arrays = counters[side].entries = 0;
    const c = clock(), events = [], feeds = [1000, 200].map(intervalMs => modules[side].createLatestDisplayFeed(-1, {
      ...c, intervalMs, key: String, unavailable: value => value < 0,
    }));
    const off = feeds.flatMap((feed, lane) => Array.from({ length: 3 }, (_, listener) =>
      feed.subscribe(value => events.push([lane, listener, c.now(), value]))));
    for (let seq = 1; seq <= 300; ++seq) { c.at(seq * 200); for (const feed of feeds) feed.push(seq); }
    pair[side] = { arrays: counters[side].arrays, entries: counters[side].entries,
      notifications: events.length, publications: [0, 1].map(lane => events.filter(event => event[0] === lane && event[1] === 0 && event[3] >= 0).length),
      events, timers: { ...c.stats } };
    off.forEach(release => release()); feeds.forEach(feed => feed.dispose()); assert.equal(c.timers.size, 0);
  }
  assert.deepEqual(pair.after.events, pair.before.events);
  assert.deepEqual(pair.after.timers, pair.before.timers);
  assert.deepEqual(pair.after.publications, [60, 300]);
  assert.equal(pair.before.arrays, 360); assert.equal(pair.before.entries, 1080);
  assert.equal(pair.after.arrays, baselineOnly ? 360 : 2); assert.equal(pair.after.entries, baselineOnly ? 1080 : 6);
  evidence.stableMinute = Object.fromEntries(Object.entries(pair).map(([side, { events, ...data }]) => [side, data]));
});
compare('no subscribers and duplicate keys allocate no timer and retain latest', h => {
  for (let seq = 0; seq < 10000; ++seq) h.feed.push(seq);
  assert.equal(h.c.timers.size, 0); const off = h.sub(); assert.equal(h.events.at(-1)[2], 9999);
  for (let i = 0; i < 10000; ++i) h.feed.push(9999);
  h.c.at(10000); assert.equal(h.events.length, 1); off(); h.feed.dispose();
});
compare('outage clears immediately and recovery retains the original 1Hz deadline', h => {
  const off = h.sub(); h.feed.push(1); h.c.at(100); h.feed.push(2); h.feed.push(-1);
  assert.deepEqual(h.events.at(-1), ['a', 100, -1]); h.feed.push(3); h.c.at(999);
  assert.equal(h.events.at(-1)[2], -1); h.c.at(1000); assert.deepEqual(h.events.at(-1), ['a', 1000, 3]);
  off(); h.feed.dispose();
});
for (const pushNew of [false, true]) compare('same callback rebind during dispatch; push=' + pushNew, h => {
  let offB, next, fired = false;
  const same = value => h.events.push(['same', h.c.now(), value]);
  const offA = h.sub('a', value => { if (value === 1 && !fired) {
    fired = true; if (pushNew) h.feed.push(2); offB(); next = h.feed.subscribe(same);
  } });
  offB = h.feed.subscribe(same); h.c.at(1000); h.feed.push(1);
  assert.deepEqual(h.events.filter(event => event[0] === 'same').map(event => event[2]), [ -1, pushNew ? 2 : 1 ]);
  h.c.at(2000); next(); offA(); h.feed.dispose();
});
compare('duplicate callback registrations have independent disposal; stale disposer is harmless', h => {
  const same = value => h.events.push(['same', h.c.now(), value]);
  const a = h.feed.subscribe(same), b = h.feed.subscribe(same); h.feed.push(1); a(); a();
  h.c.at(1000); h.feed.push(2); assert.deepEqual(h.events.map(event => event[2]), [-1, -1, 1, 1, 2]);
  b(); h.feed.dispose(); assert.equal(h.c.timers.size, 0);
});
compare('remove old and add new listener within a batch delivers initial cache exactly once', h => {
  let offB, next;
  const offA = h.sub('a', value => { if (value === 1) {
    h.feed.push(2); offB(); next = h.sub('new');
  } });
  offB = h.sub('old'); h.c.at(1000); h.feed.push(1);
  assert.deepEqual(h.events.filter(event => event[0] !== 'a').map(event => [event[0], event[2]]), [['old', -1], ['new', 2]]);
  h.c.at(2000); next(); offA(); h.feed.dispose();
});
compare('dispose within a callback stops the rest of the batch and future subscriptions', h => {
  h.sub('a', value => { if (value === 1) h.feed.dispose(); }); h.sub('b');
  h.c.at(1000); h.feed.push(1); h.sub('late'); h.feed.push(2); h.c.at(10000);
  assert.equal(h.events.filter(event => event[0] === 'b').length, 1); assert.equal(h.c.timers.size, 0);
});
compare('throwing consumers remain isolated across reused batches', h => {
  const bad = h.feed.subscribe(() => { throw Error('consumer'); }), good = h.sub();
  for (let seq = 1; seq <= 10; ++seq) { h.c.at(seq * 1000); h.feed.push(seq); }
  assert.equal(h.events.length, 11); bad(); good(); h.feed.dispose();
});
compare('last unsubscribe cancels pending timer; cancelled callback cannot wake a replacement', h => {
  let off = h.sub(); h.feed.push(1); h.c.at(100); h.feed.push(2);
  const stale = [...h.c.timers.values()][0].fn; off(); assert.equal(h.c.timers.size, 0);
  off = h.sub(); h.feed.push(3); stale(); assert.equal(h.events.at(-1)[2], 2);
  h.c.at(1100); assert.equal(h.events.at(-1)[2], 3); off(); h.feed.dispose();
});
compare('early timer preserves deadline; delayed timer delivers latest without catch-up', h => {
  const off = h.sub(); h.feed.push(1); h.c.at(100); h.feed.push(2); h.c.set(999);
  const [id, early] = [...h.c.timers][0]; h.c.timers.delete(id); early.fn();
  assert.equal(h.events.at(-1)[2], 1); h.feed.push(3); h.c.at(60000);
  assert.deepEqual(h.events.at(-1), ['a', 60000, 3]); const count = h.events.length;
  h.c.at(60001); assert.equal(h.events.length, count); off(); h.feed.dispose();
});
compare('advance after foreground/resume jump retains only latest and no catch-up timer', h => {
  const off = h.sub(); h.feed.push(1); h.c.at(100); h.feed.push(2);
  h.c.set(600000); h.feed.push(3); h.feed.advance();
  assert.deepEqual(h.events.at(-1), ['a', 600000, 3]); assert.equal(h.c.timers.size, 0);
  h.feed.push(4); h.c.at(601000); assert.equal(h.events.at(-1)[2], 4); off(); h.feed.dispose();
});
check('changing membership rebuilds once; unchanged membership reuses the ordered batch', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const h = harness(side), a = h.sub('a'); h.feed.push(1); const b = h.sub('b');
    h.c.at(1000); h.feed.push(2); b(); h.c.at(2000); h.feed.push(3);
    b(); h.c.at(3000); h.feed.push(4); a(); const c = h.sub('c'); h.c.at(4000); h.feed.push(5);
    pair[side] = { arrays: counters[side].arrays, entries: counters[side].entries, trace: h.result() };
    c(); h.feed.dispose(); assert.equal(h.c.timers.size, 0);
  }
  assert.deepEqual(pair.after.trace, pair.before.trace);
  assert.equal(pair.before.arrays, 5); assert.equal(pair.after.arrays, baselineOnly ? 5 : 4);
  evidence.membershipChanges = Object.fromEntries(Object.entries(pair).map(([side, { trace, ...data }]) => [side, data]));
});
compare('20000 deterministic mixed subscription/push/unsubscribe/advance operations', h => {
  let seed = 0x170c2026, now = 0; const owners = new Map();
  const random = () => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return seed >>> 0; };
  for (let i = 0; i < 20000; ++i) {
    const value = random(), slot = value % 12;
    switch (value % 6) {
      case 0: owners.get(slot)?.(); owners.set(slot, h.sub('slot-' + slot)); break;
      case 1: owners.get(slot)?.(); owners.delete(slot); break;
      case 2: h.feed.push(i % 17 === 0 ? -1 : i); break;
      case 3: h.c.at(now += value % 700); break;
      case 4: h.feed.advance(); break;
      case 5: h.feed.push(i); h.feed.push(i); break;
    }
    assert(h.c.timers.size <= 1);
  }
  for (const off of owners.values()) off(); h.feed.dispose(); h.c.at(now + 60000); assert.equal(h.c.timers.size, 0);
});

// Execute the real production singleton twice, replacing only this task's feed
// for the baseline. Inert IPC and virtual clocks keep acquisition observable.
async function integration(side) {
  const c = clock(); c.set(200000);
  const calls = [], events = [], state = { c, reply: null, fileReads: 0,
    invoke: async (command, args) => { calls.push({ command, args });
      if (command === 'monitor.snapshot') return state.reply;
      return { ok: true, cleaned: true };
    } };
  const slot = `__monitorCpuIntegration_${side}`, ref = `globalThis.${slot}`; globalThis[slot] = state;
  const plugin = { name: 'inert-production-' + side, setup(b) {
    b.onResolve({ filter: /^\.\/(ipc|api|monitorAppliedTdp)$/ }, args => ({ path: args.path, namespace: 'inert' }));
    b.onLoad({ filter: /.*/, namespace: 'inert' }, args => ({ loader: 'ts', contents:
      args.path === './ipc' ? `export async function invoke(...args){return ${ref}.invoke(...args)}` :
      args.path === './api' ? `export const fs={readTextFile:async()=>{++${ref}.fileReads;throw Error('inert files')}}` :
      'export function advanceMonitorTdpDisplay(){}export function subscribeMonitorTdpState(){return()=>{}}' }));
    b.onLoad({ filter: /monitor(?:Data|DisplayFeed)\.ts$/ }, args => {
      let source = args.path.endsWith('monitorDisplayFeed.ts') && (side === 'before' || baselineOnly) ? baselineSource : fs.readFileSync(args.path, 'utf8');
      if (args.path.endsWith('monitorData.ts')) source = source.replaceAll('Date.now()', `${ref}.c.now()`)
        .replaceAll('performance.now()', `${ref}.c.now()`).replaceAll('setTimeout(', `${ref}.c.setTimer(`)
        .replaceAll('clearTimeout(', `${ref}.c.clearTimer(`);
      return { loader: 'ts', contents: source, resolveDir: path.dirname(args.path) };
    });
  } };
  const compiled = await build({ stdin: { contents: "export * from './src/bridge/monitorData.ts';", loader: 'ts', resolveDir: root },
    bundle: true, platform: 'node', format: 'esm', write: false, plugins: [plugin] });
  const m = await import('data:text/javascript;base64,' + Buffer.from(compiled.outputFiles[0].text).toString('base64'));
  const packet = (seq, patch = {}) => ({ schema: 1, available: true, session: 1, generation: 1, seq, relayEpoch: 1,
    collectedAt: c.now(), heartbeatAt: c.now(), hwinfoOkAt: c.now(), topActive: true, fpsActive: true, hwDown: false,
    top: JSON.stringify({ ts: c.now(), cpuUsage: seq % 100, tdpW: 30, tempC: 65, freqMhz: 4000, hasBattery: false }),
    fps: JSON.stringify({ ts: c.now(), fps: 60, fps1: 45, gpu: 40, packagePower: 30, pid: 99, game: 'fixture' }), ...patch });
  const release = m.acquireMonitorDemand('cpu-test', { top: true, fps: true });
  const off = [m.subscribeMonitorSnapshot(view => events.push(['bar', c.now(), view?.seq ?? null])),
    m.subscribeMonitorHudSnapshot(view => events.push(['hud', c.now(), view?.seq ?? null]))];
  state.reply = packet(1);
  const [one, two] = await Promise.all([m.readMonitorSnapshot(), m.readMonitorSnapshot()]);
  assert.equal(calls.filter(call => call.command === 'monitor.snapshot').length, 1);
  assert.notEqual(one, two); one.top.cpuUsage = 99; assert.equal(two.top.cpuUsage, 1);
  await m.readMonitorSnapshot(); assert.equal(calls.filter(call => call.command === 'monitor.snapshot').length, 2);
  for (let seq = 2; seq <= 51; ++seq) { c.at(200000 + (seq - 1) * 200); m.acceptPublishedMonitorSnapshot(packet(seq)); }
  assert.equal(calls.filter(call => call.command === 'monitor.snapshot').length, 2);
  c.at(210100); m.acceptPublishedMonitorSnapshot(packet(52, { available: false }));
  assert.equal(events.filter(event => event[0] === 'bar').at(-1)[2], null);
  c.set(810100); state.reply = packet(53); m.acceptPublishedMonitorSnapshot(state.reply);
  assert.equal(events.filter(event => event[0] === 'bar').at(-1)[2], 53);
  await m.readMonitorSnapshot(); assert.equal(calls.filter(call => call.command === 'monitor.snapshot').length, 3);
  off.forEach(close => close()); release();
  for (let i = 0; i < 12; ++i) await Promise.resolve();
  assert.equal(state.fileReads, 0); assert.equal(c.timers.size, 0);
  return { queries: calls.filter(call => call.command === 'monitor.snapshot').length, fileReads: state.fileReads,
    notifications: events.length, events, calls, timers: { ...c.stats } };
}
const integrated = { before: await integration('before'), after: await integration('after') };
assert.deepEqual(integrated.after, integrated.before);
checks.push('production singleton: concurrent coalescing, sequential reads, pushes, outage, clock jump and control-copy isolation');
evidence.productionSingleton = Object.fromEntries(Object.entries(integrated).map(([side, { events, calls, ...data }]) => [side, data]));
// The canonical subscription-identity regression now directly asserts the new
// cached allocation counts; no in-memory assertion compatibility override.
const report = { suite: 'monitor CPU display listener batch reuse', mode: baselineOnly ? 'captured baseline replay' : 'regression',
  passed: checks.length, checks, hashes: { before: sha256(baselineSource), after: sha256(baselineOnly ? baselineSource : currentSource) }, evidence,
  limits: 'Deterministic operation counts and exact callback/timer/IPC traces. No process CPU, actual hidden-window transition, real sleep/resume, native build or deployment measurement.' };
const reportAt = process.argv.indexOf('--report');
if (reportAt >= 0) fs.writeFileSync(path.resolve(process.argv[reportAt + 1]), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report, null, 2));
