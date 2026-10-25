// Production passive store/feed with synthetic receipts and virtual time.
// Live-Set negative fixtures have explicit step limits; no intentional hang.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
const root = process.cwd();
const task = path.resolve(process.env.YMCC_TDP_OBSERVER_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-tdp-observer-fence-20261016');
const baseline = path.join(task, 'before'), out = path.join(task, 'tests'); fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(root + '/package.json')('esbuild');
const pins = {}, io = globalThis.__tdpObserverIO = {}, checks = [], traces = {}, operations = {};
const hash = text => crypto.createHash('sha256').update(text).digest('hex');
function counts() {
  return { notifications: 0, passes: 0, entries: 0, registrations: 0, stateFreezes: 0, receiptFreezes: 0,
    freeze(value) { if ('requestedMode' in value) ++this.receiptFreezes; else ++this.stateFreezes; return Object.freeze(value); },
    reset() { this.notifications = this.passes = this.entries = this.registrations = this.stateFreezes = this.receiptFreezes = 0; },
    read() { return Object.fromEntries(Object.entries(this).filter(([, value]) => typeof value === 'number')); } };
}
function clock() {
  let now = 0, next = 0; const timers = new Map();
  return { timers, now: () => now, set: value => { now = value; },
    setTimer(fn, ms) { timers.set(++next, { fn, at: now + ms }); return next; }, clearTimer(id) { timers.delete(id); },
    at(value) { now = value; for (let i = 0; i < 100; ++i) { const due = [...timers].filter(([, t]) => t.at <= now); if (!due.length) return; for (const [id, timer] of due) if (timers.delete(id)) timer.fn(); } throw Error('Timer loop'); },
  };
}
async function load(side) {
  const base = side === 'before' ? baseline : root, h = io[side] = counts(), ref = 'globalThis.__tdpObserverIO[' + JSON.stringify(side) + ']';
  const plugin = { name: 'tdp-observer-' + side, setup(b) {
    b.onLoad({ filter: /\.ts$/ }, a => {
      let text = fs.readFileSync(a.path, 'utf8'); (pins[side] ??= {})[path.relative(base, a.path).replaceAll('\\', '/')] = hash(text);
      if (a.path.endsWith('tdpRuntimeState.ts')) {
        assert(!/Array\.from|\.\.\.listeners/.test(text), 'No observer-array snapshot on publication');
        const publish = 'function publish(status: TdpRuntimeStatus, watts: number | null = null) {';
        assert.equal(text.split(publish).length, 2); text = text.replace(publish, publish + ' ++' + ref + '.passes;');
        let loops = 0, notices = 0, adds = 0;
        text = text.replace(/for \(const (?:listener|subscription) of listeners\) \{/g, match => { ++loops; return match + ' ++' + ref + '.entries;'; });
        text = text.replace(/try \{ ((?:subscription\.)?listener\((?:current|published)\));/g, (match, call) => { ++notices; return 'try { ++' + ref + '.notifications; ' + call + ';'; });
        text = text.replace(/listeners\.add\((?:listener|subscription)\);/g, match => { ++adds; return ref + '.registrations++; ' + match; });
        assert.equal(loops, 1); assert.equal(notices, 2); assert.equal(adds, 1); text = text.replaceAll('Object.freeze(', ref + '.freeze(');
      }
      return { contents: text, loader: 'ts', resolveDir: path.dirname(a.path) };
    });
  } };
  const contents = ['tdpRuntimeState', 'monitorAppliedTdp', 'monitorDisplayFeed'].map(name => 'export * from ' + JSON.stringify(path.join(base, 'src/bridge/' + name + '.ts')) + ';').join('\n');
  const built = await build({ stdin: { contents, resolveDir: root, loader: 'ts' }, bundle: true, write: false, platform: 'node', format: 'esm', plugins: [plugin] });
  const file = path.join(out, side + '.mjs'); fs.writeFileSync(file, built.outputFiles[0].text); return { m: await import(pathToFileURL(file).href + '?' + Date.now()), h };
}
const sides = { before: await load('before'), after: await load('after') };
const storeFor = side => sides[side].m.createTdpRuntimeStore(() => 1000);
const brief = state => ({ revision: state.revision, status: state.status, mode: state.mode, watts: state.watts, proof: state.proof });
async function check(name, run) { try { await run(); checks.push(name); } catch (error) { console.error('Failed:', name); throw error; } }

await check('self-rebind cannot extend the current publication traversal', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const store = storeFor(side), { h } = sides[side]; h.reset(); let off, enabled = false, reopening = false, updates = 0;
    const guard = 8, callback = () => { if (!enabled || reopening) return; ++updates; if (updates >= guard) return;
      off(); reopening = true; off = store.observe(callback); reopening = false; };
    off = store.observe(callback); enabled = true; store.invalidate(); assert.equal(updates, side === 'before' ? guard : 1);
    pair[side] = { updatesForOnePublication: updates, guard, work: h.read() }; off();
    h.reset(); store.invalidate(); assert.equal(h.notifications, 0);
  }
  traces.selfRebind = pair;
});

await check('newly added observers receive their initial state once, not a second live traversal call', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), seen = []; let next, once = false;
    const off = store.observe(state => { if (state.revision === 1 && !once) { once = true; next = store.observe(state => seen.push(state.revision)); } });
    store.invalidate(); assert.deepEqual(seen, side === 'before' ? [1, 1] : [1]); store.invalidate(); assert.equal(seen.at(-1), 2); next(); off();
  }
});

await check('two owners of the same callback have independent release rights', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const store = storeFor(side), seen = [], same = state => seen.push(state.revision), a = store.observe(same), b = store.observe(same);
    store.invalidate(); a(); store.invalidate(); assert.deepEqual(seen, side === 'before' ? [0, 0, 1] : [0, 0, 1, 1, 2]);
    pair[side] = seen.slice(); b();
  }
  traces.owners = pair;
});

await check('a stale disposer cannot remove a later registration of the same callback', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), seen = [], same = state => seen.push(state.revision), old = store.observe(same); old();
    const next = store.observe(same); old(); store.invalidate(); assert.equal(seen.at(-1), side === 'before' ? 0 : 1); next();
  }
});

await check('remove/re-add in an earlier callback only sends the new initial state', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), seen = []; let b, next, once = false;
    const same = state => seen.push(state.revision), a = store.observe(state => {
      if (state.revision === 1 && !once) { once = true; b(); next = store.observe(same); }
    });
    b = store.observe(same); store.invalidate(); assert.deepEqual(seen, side === 'before' ? [0, 1, 1] : [0, 1]); next(); a();
  }
});

await check('cancelling another existing observer suppresses its pending notification', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), seen = []; let b;
    const a = store.observe(state => { if (state.revision === 1) b(); }); b = store.observe(state => seen.push(state.revision));
    store.invalidate(); assert.deepEqual(seen, [0]); a(); b();
  }
});

await check('fence preserves all surviving old observers before skipping newly appended entries', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), old = [], added = [], newStops = []; let once = false;
    const a = store.observe(state => { if (state.revision === 1 && !once) { once = true; for (let i = 0; i < 20; ++i) newStops.push(store.observe(s => added.push([i, s.revision]))); } });
    const b = store.observe(state => old.push(['b', state.revision])), c = store.observe(state => old.push(['c', state.revision]));
    store.invalidate(); assert.deepEqual(old, [['b', 0], ['c', 0], ['b', 1], ['c', 1]]);
    assert.equal(added.length, side === 'before' ? 40 : 20); for (const stop of newStops) stop(); a(); b(); c();
  }
});

for (const mode of ['write', 'power', 'throw']) await check('nested ' + mode + ' publication supersedes the outer notification pass', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const store = storeFor(side); store.powerMode('dc'); const history = []; let once = false;
    const a = store.observe(state => {
      if (state.status === 'applied' && state.watts === 30 && !once) { once = true;
        if (mode === 'power') store.powerMode('ac'); else { const r = store.begin('dc', 45); store.finish(r, true); }
        if (mode === 'throw') throw Error('observer after nested update');
      }
    }), b = store.observe(state => history.push(brief(state)));
    const receipt = store.begin('dc', 30); store.finish(receipt, true); const end = history.at(-1);
    assert.equal(end.watts, mode === 'power' ? null : 45); assert.equal(end.mode, mode === 'power' ? 'ac' : 'dc');
    assert.equal(history.filter(state => state.revision === end.revision).length, side === 'before' ? 2 : 1);
    assert.deepEqual(end, brief(store.get())); pair[side] = history; a(); b();
  }
  traces['nested-' + mode] = pair;
});

await check('observer exceptions cannot interrupt control receipts or another owner', () => {
  const store = storeFor('after'), seen = []; let errors = 0;
  const bad = () => { ++errors; throw Error('display'); }, a = store.observe(bad), b = store.observe(bad), c = store.observe(state => seen.push(brief(state)));
  store.powerMode('dc'); const r = store.begin('ac', 30); assert(store.isCurrent(r)); store.finish(r, true, 28);
  assert.equal(store.get().watts, 28); assert.equal(store.get().proof, 'write-ack'); assert.equal(seen.at(-1).watts, 28); assert.equal(errors, 8); a(); b(); c();
});

await check('write ownership and accepted watts survive observer churn', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side); store.powerMode('dc'); let off, active = false, registering = false, budget = 8;
    const callback = () => { if (!active || registering || --budget <= 0) return; off(); registering = true; off = store.observe(callback); registering = false; };
    off = store.observe(callback); active = true; const r = store.begin('ac', 45);
    assert.equal(r.mode, 'dc'); assert.equal(r.requestedMode, 'ac'); assert(store.isCurrent(r)); store.finish(r, true, 43);
    assert.equal(store.get().watts, 43); assert.equal(store.get().mode, 'dc'); assert.equal(store.get().proof, 'write-ack'); off();
  }
});

await check('old success/failure/unknown receipts never replace a newer request', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side); store.powerMode('dc'); const a = store.begin('dc', 30), b = store.begin('dc', 45), pending = store.get();
    assert(!store.isCurrent(a)); assert(store.isCurrent(b)); store.finish(a, true); store.finish(a, false); store.finishUnknown(a);
    assert.strictEqual(store.get(), pending); store.finish(b, true, 40); assert.equal(store.get().watts, 40);
  }
});

await check('sleep/power invalidation retires the old receipt and allows a fresh power-side write', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side); store.powerMode('dc'); const old = store.begin('dc', 30); store.invalidatePower(); const unknown = store.get();
    assert.equal(unknown.mode, null); assert(!store.isCurrent(old)); store.finish(old, true); assert.strictEqual(store.get(), unknown);
    store.powerMode('ac'); const fresh = store.begin('ac', 45); store.finish(fresh, true); assert.equal(store.get().watts, 45); assert.equal(store.get().mode, 'ac');
  }
});

await check('snapshots remain immutable and previous receipts are not rewritten by notifications', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side); store.powerMode('dc'); let errors = 0;
    const off = store.observe(state => { assert(Object.isFrozen(state)); try { state.watts = 999; } catch { ++errors; } });
    const r = store.begin('dc', 30); const pending = store.get(); assert(Object.isFrozen(r)); store.finish(r, true); assert.equal(pending.status, 'pending');
    assert.equal(pending.watts, null); assert.equal(store.get().watts, 30); assert(errors >= 3); off();
  }
});

await check('removed observers never run after final cleanup', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), { h } = sides[side], off = store.observe(() => {}); off(); off(); h.reset();
    for (let i = 0; i < 200; ++i) store.invalidate(); assert.equal(h.notifications, 0); assert.equal(h.entries, 0); assert.equal(h.passes, 200);
  }
});

await check('normal 200 writes preserve notification/freeze work without a per-publication observer array', () => {
  for (const side of ['before', 'after']) {
    const store = storeFor(side), { h } = sides[side]; store.powerMode('dc'); h.reset(); const seenA = [], seenB = [];
    const a = store.observe(state => seenA.push(brief(state))), b = store.observe(state => seenB.push(brief(state)));
    for (let i = 0; i < 200; ++i) { const r = store.begin('dc', 30 + i % 10); store.finish(r, true); }
    assert.deepEqual(seenA, seenB); assert.equal(seenA.length, 401); assert.equal(h.notifications, 802); assert.equal(h.passes, 400);
    assert.equal(h.entries, 800); assert.equal(h.stateFreezes, 400); assert.equal(h.receiptFreezes, 200); assert.equal(h.registrations, 2);
    operations[side] = h.read(); a(); b();
  }
  assert.deepEqual(operations.after, operations.before);
});

await check('real applied-TDP source detach/reopen avoids an extra source call with identical display data', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const { m, h } = sides[side], c = clock(), store = m.createTdpRuntimeStore(c.now); store.powerMode('dc');
    const bindings = { opened: 0, closed: 0 }; const feed = m.createAppliedTdpFeed({ ...c, initial: store.get(), observe(listener) {
      ++bindings.opened; const stop = store.observe(listener); return () => { ++bindings.closed; stop(); };
    } });
    const seen = []; let off, reopened, once = false;
    off = feed.subscribe(state => { if (state.status === 'applied' && state.watts === 30 && !once) { once = true; off(); reopened = feed.subscribe(state => seen.push(brief(state))); } });
    const r = store.begin('dc', 30); c.set(1000); h.reset(); store.finish(r, true); assert(once); assert.equal(seen.at(-1).watts, 30);
    assert.equal(h.notifications, side === 'before' ? 3 : 2); pair[side] = { shown: seen, work: h.read(), bindings: { ...bindings } };
    reopened(); off(); assert.deepEqual(bindings, { opened: 2, closed: 2 }); assert.equal(c.timers.size, 0);
  }
  assert.deepEqual(pair.after.shown, pair.before.shown); traces.realFeedReopen = pair;
});

for (const [file, pin] of Object.entries(pins.after)) assert.equal(hash(fs.readFileSync(path.join(root, file))), pin, 'Changed tested source: ' + file);
const result = { ok: true, cases: checks.length, checks, traces, operations, sourcePins: pins,
  scope: 'Actual passive source/store and display feed modules; virtual time and synthetic receipt completion only. The rebind negative fixture stops at 8 updates. No live TDP write, device, desktop/game, deployment or CPU percentage measurement.' };
fs.writeFileSync(path.join(task, 'TDP-OBSERVER-RESULT.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify({ ok: true, cases: checks.length, selfRebind: traces.selfRebind, normalWork: operations, realFeedWork: Object.fromEntries(Object.entries(traces.realFeedReopen).map(([side, trace]) => [side, trace.work])) }, null, 2));
