// Real display-feed and TDP-store modules with a virtual clock. Exercise
// subscription ownership and reentrant dispatch, not desktop/hardware input.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
const root = process.cwd();
const task = path.resolve(process.env.YMCC_FEED_SUBSCRIPTION_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-feed-subscription-identity-20261016');
const baseline = path.join(task, 'before'), out = path.join(task, 'tests'); fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(path.join(root, 'package.json'))('esbuild');
const pins = {}, io = globalThis.__feedSubscriptionIO = {}, checks = [], traces = {}, operations = {};
const hash = text => crypto.createHash('sha256').update(text).digest('hex');
function clock() {
  let now = 0, next = 0;
  const timers = new Map(), counters = { timerCreates: 0, timerClears: 0, timerRuns: 0, maxTimers: 0 };
  return { timers, counters, now: () => now, set: value => { now = value; },
    setTimer(fn, ms) { ++counters.timerCreates; timers.set(++next, { fn, at: now + ms }); counters.maxTimers = Math.max(counters.maxTimers, timers.size); return next; },
    clearTimer(id) { if (timers.delete(id)) ++counters.timerClears; },
    at(value) {
      now = value;
      for (let i = 0; i < 100; ++i) {
        const due = [...timers].filter(([, timer]) => timer.at <= now);
        if (!due.length) return;
        for (const [id, timer] of due) { if (!timers.delete(id)) continue; ++counters.timerRuns; timer.fn(); }
      }
      throw Error('Unbounded timer loop');
    },
  };
}
async function load(side) {
  const base = side === 'before' ? baseline : root;
  const h = io[side] = { notifications: 0, batchArrays: 0, entriesCopied: 0, registrations: 0,
    snapshot(listeners) { ++this.batchArrays; this.entriesCopied += listeners.size; return Array.from(listeners); },
    reset() { this.notifications = this.batchArrays = this.entriesCopied = this.registrations = 0; } };
  const ref = 'globalThis.__feedSubscriptionIO[' + JSON.stringify(side) + ']';
  const plugin = { name: 'subscription-' + side, setup(b) {
    b.onLoad({ filter: /\.ts$/ }, a => {
      let text = fs.readFileSync(a.path, 'utf8');
      (pins[side] ??= {})[path.relative(base, a.path).replaceAll('\\', '/')] = hash(text);
      if (a.path.endsWith('monitorDisplayFeed.ts')) {
        const marker = 'const notify = (listener: (value: T) => void, value: T) => {';
        assert.equal(text.split(marker).length, 2); assert.equal(text.split('Array.from(listeners)').length, 2);
        text = text.replace(marker, marker + ' ++' + ref + '.notifications;').replace('Array.from(listeners)', ref + '.snapshot(listeners)');
        let adds = 0; text = text.replace(/listeners\.add\((?:listener|subscription)\);/g, matched => { ++adds; return ref + '.registrations++; ' + matched; });
        assert.equal(adds, 1);
      }
      return { contents: text, loader: 'ts', resolveDir: path.dirname(a.path) };
    });
  } };
  const contents = ['monitorDisplayFeed', 'monitorAppliedTdp', 'tdpRuntimeState'].map(name => 'export * from ' + JSON.stringify(path.join(base, 'src/bridge/' + name + '.ts')) + ';').join('\n');
  const compiled = await build({ stdin: { contents, loader: 'ts', resolveDir: root }, bundle: true, platform: 'node', format: 'esm', write: false, plugins: [plugin] });
  const file = path.join(out, side + '.mjs'); fs.writeFileSync(file, compiled.outputFiles[0].text);
  return { m: await import(pathToFileURL(file).href + '?' + Date.now()), h };
}
const sides = { before: await load('before'), after: await load('after') };
const feedFor = (side, c, extra = {}) => sides[side].m.createLatestDisplayFeed(0, { ...c, key: String, ...extra });
async function check(name, run) { try { await run(); checks.push(name); } catch (error) { console.error('Failed:', name); throw error; } }

for (const pushNew of [false, true]) await check('rebind same callback during a batch, newer publication=' + pushNew, () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = []; let offB, offNext, fired = false;
    const B = value => events.push(value), offA = feed.subscribe(value => {
      if (value === 1 && !fired) { fired = true; if (pushNew) feed.push(2); offB(); offNext = feed.subscribe(B); }
    });
    offB = feed.subscribe(B); c.set(1000); feed.push(1);
    assert.deepEqual(events, side === 'before' ? [0, pushNew ? 2 : 1, 1] : [0, pushNew ? 2 : 1]);
    pair[side] = events.slice(); offNext(); offA(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
  traces['rebind-' + pushNew] = pair;
});

await check('two registrations of one callback retain independent disposal ownership', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = [], same = value => events.push(value);
    const a = feed.subscribe(same), b = feed.subscribe(same); c.set(1000); feed.push(1); a(); a(); c.set(2000); feed.push(2);
    assert.deepEqual(events, side === 'before' ? [0, 0, 1] : [0, 0, 1, 1, 2]);
    pair[side] = events.slice(); b(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
  traces.owners = pair;
});

await check('releasing one duplicate keeps the pending notification and its only timer', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = [], same = value => events.push(value);
    const a = feed.subscribe(same), b = feed.subscribe(same); c.set(100); feed.push(1); assert.equal(c.timers.size, 1);
    a(); assert.equal(c.timers.size, side === 'before' ? 0 : 1); c.at(1000);
    assert.deepEqual(events, side === 'before' ? [0, 0] : [0, 0, 1]); b(); assert.equal(c.timers.size, 0); feed.dispose();
  }
});

await check('removed callback does not receive the in-progress batch', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = []; let b;
    const a = feed.subscribe(value => { if (value === 1) b(); }); b = feed.subscribe(value => events.push(value));
    c.set(1000); feed.push(1); assert.deepEqual(events, [0]); a(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
});

await check('replacing a callback with a different callback preserves initial delivery once', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = []; let b, next;
    const a = feed.subscribe(value => { if (value === 1) { feed.push(2); b(); next = feed.subscribe(value => events.push(['new', value])); } });
    b = feed.subscribe(value => events.push(['old', value])); c.set(1000); feed.push(1);
    assert.deepEqual(events, [['old', 0], ['new', 2]]); next(); a(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
});

await check('two new owners of the same callback are not mistaken for its retired batch entry', () => {
  const c = clock(), feed = feedFor('after', c), events = [], same = value => events.push(value); let b, x, y;
  const a = feed.subscribe(value => { if (value === 1) { feed.push(2); b(); x = feed.subscribe(same); y = feed.subscribe(same); } });
  b = feed.subscribe(same); c.set(1000); feed.push(1); assert.deepEqual(events, [0, 2, 2]);
  c.at(2000); assert.deepEqual(events, [0, 2, 2, 2, 2]); x(); y(); a(); feed.dispose(); assert.equal(c.timers.size, 0);
});

await check('plain reentrant push keeps a coherent old batch and coalesces the next one', () => {
  const tracesBySide = {};
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = [];
    const a = feed.subscribe(value => { events.push(['a', value]); if (value === 1) feed.push(2); });
    const b = feed.subscribe(value => events.push(['b', value])); c.set(1000); feed.push(1);
    assert.deepEqual(events.slice(-2), [['a', 1], ['b', 1]]); assert.equal(c.timers.size, 1);
    c.at(2000); assert.deepEqual(events.slice(-2), [['a', 2], ['b', 2]]); tracesBySide[side] = events;
    a(); b(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
  assert.deepEqual(tracesBySide.after, tracesBySide.before);
});

await check('last-owner rebind takes the latest cache and cancels the obsolete timer', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = []; let a, next;
    const same = value => { events.push(value); if (value === 1) { feed.push(2); a(); next = feed.subscribe(same); } };
    a = feed.subscribe(same); c.set(1000); feed.push(1); assert.deepEqual(events, [0, 1, 2]); assert.equal(c.timers.size, 0);
    a(); next(); feed.dispose();
  }
});

await check('stale disposer and cancelled timer cannot detach or flush a new same-callback subscription', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), events = [], same = value => events.push(value);
    const a = feed.subscribe(same); c.set(100); feed.push(1); const stale = [...c.timers.values()][0].fn; a();
    const b = feed.subscribe(same); c.set(200); feed.push(2); const pending = [...c.timers.keys()]; a(); stale();
    assert.deepEqual([...c.timers.keys()], pending); c.at(1100); assert.equal(events.at(-1), 2); b(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
});

await check('callback errors remain isolated for each owner and other subscribers', () => {
  const c = clock(), feed = feedFor('after', c), seen = []; let throws = 0;
  const bad = () => { ++throws; throw Error('consumer'); }, a = feed.subscribe(bad), b = feed.subscribe(bad), good = feed.subscribe(value => seen.push(value));
  c.set(1000); feed.push(1); assert.equal(throws, 4); assert.deepEqual(seen, [0, 1]); a();
  c.set(2000); feed.push(2); assert.equal(throws, 5); assert.deepEqual(seen, [0, 1, 2]); b(); good(); feed.dispose(); assert.equal(c.timers.size, 0);
});

await check('disposing inside a callback prevents remaining old notifications and future registration', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c), seen = [];
    const a = feed.subscribe(value => { if (value === 1) feed.dispose(); }), b = feed.subscribe(value => seen.push(value));
    c.set(1000); feed.push(1); const after = feed.subscribe(value => seen.push(value)); feed.push(2); c.at(5000);
    assert.deepEqual(seen, [0]); a(); b(); after(); assert.equal(c.timers.size, 0);
  }
});

function tdpSource(side, c) {
  const { m } = sides[side], store = m.createTdpRuntimeStore(c.now); store.powerMode('dc');
  const counts = { observe: 0, unobserve: 0 };
  const feed = m.createAppliedTdpFeed({ ...c, initial: store.get(), observe(listener) {
    ++counts.observe; const off = store.observe(listener); return () => { ++counts.unobserve; off(); };
  } });
  const apply = watts => { const receipt = store.begin('dc', watts); store.finish(receipt, true); };
  return { feed, store, counts, apply };
}

await check('actual TDP write receipts never roll back to the old batch after callback rebind', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const c = clock(), t = tdpSource(side, c), seen = []; let b, next, fired = false;
    const same = state => seen.push(state.watts), a = t.feed.subscribe(state => {
      if (state.status === 'applied' && state.watts === 30 && !fired) { fired = true; t.apply(45); b(); next = t.feed.subscribe(same); }
    });
    b = t.feed.subscribe(same); c.set(1000); t.apply(30); c.at(2000); assert(fired);
    assert.equal(seen.at(-1), side === 'before' ? 30 : 45); assert.equal(t.store.get().watts, 45);
    pair[side] = seen.slice(); assert.equal(t.counts.observe, 1); next(); a(); assert.equal(t.counts.unobserve, 1); assert.equal(c.timers.size, 0);
  }
  traces.tdpRebind = pair;
});

await check('TDP duplicate owner release preserves updates with only one upstream subscription', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), t = tdpSource(side, c), seen = [], same = state => seen.push(state.watts);
    const a = t.feed.subscribe(same), b = t.feed.subscribe(same); a(); c.set(1000); t.apply(45); c.at(2000);
    assert.equal(seen.at(-1), side === 'before' ? null : 45); assert.equal(t.counts.observe, 1); assert.equal(t.counts.unobserve, 0);
    b(); assert.equal(t.counts.unobserve, 1); assert.equal(c.timers.size, 0);
  }
});

await check('TDP pending expiry still clears the surviving owner at the existing five-second deadline', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), t = tdpSource(side, c); t.apply(30); const seen = [], same = state => seen.push(state.status);
    const a = t.feed.subscribe(same), b = t.feed.subscribe(same); c.set(100); t.store.begin('dc', 45); a(); c.at(5100);
    assert.equal(seen.at(-1), side === 'before' ? 'applied' : 'pending'); assert.equal(t.store.get().status, 'pending');
    b(); assert.equal(t.counts.unobserve, 1); assert.equal(c.timers.size, 0);
  }
});

await check('AC/DC invalidation reaches the surviving duplicate owner immediately', () => {
  for (const side of ['before', 'after']) {
    const c = clock(), t = tdpSource(side, c); t.apply(30); const seen = [], same = state => seen.push([state.mode, state.watts]);
    const a = t.feed.subscribe(same), b = t.feed.subscribe(same); a(); c.set(100); t.store.powerMode('ac');
    assert.deepEqual(seen.at(-1), side === 'before' ? ['dc', 30] : ['ac', null]); b(); assert.equal(t.counts.unobserve, 1); assert.equal(c.timers.size, 0);
  }
});

await check('normal distinct listeners reuse their batch while retaining 200 publications and zero timers', () => {
  for (const side of ['before', 'after']) {
    const { h } = sides[side]; h.reset(); const c = clock(), feed = feedFor(side, c), aSeen = [], bSeen = [];
    const a = feed.subscribe(value => aSeen.push(value)), b = feed.subscribe(value => bSeen.push(value));
    for (let i = 1; i <= 200; ++i) { c.set(i * 1000); feed.push(i); }
    assert.equal(aSeen.length, 201); assert.deepEqual(aSeen, bSeen); assert.equal(h.notifications, 402);
    assert.equal(h.batchArrays, side === 'before' ? 200 : 1); assert.equal(h.entriesCopied, side === 'before' ? 400 : 2); assert.equal(h.registrations, 2); assert.equal(c.counters.timerCreates, 0);
    operations[side] = { notifications: h.notifications, batchArrays: h.batchArrays, entriesCopied: h.entriesCopied, registrations: h.registrations, ...c.counters };
    a(); b(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
  assert.deepEqual({ ...operations.after, batchArrays: operations.before.batchArrays, entriesCopied: operations.before.entriesCopied }, operations.before);
});

await check('fast coalescing keeps identical callbacks, latest values and one-timer budget', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const { h } = sides[side]; h.reset(); const c = clock(), feed = feedFor(side, c, { intervalMs: 200 }), seen = [];
    const off = feed.subscribe(value => seen.push(value));
    for (let i = 1; i <= 200; ++i) { c.at(i * 100); feed.push(i); }
    c.at(20200); assert.equal(seen.at(-1), 200); assert(c.counters.maxTimers <= 1);
    pair[side] = { seen, work: { notifications: h.notifications, batchArrays: h.batchArrays, entriesCopied: h.entriesCopied, ...c.counters } };
    off(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
  assert.equal(pair.after.work.batchArrays, 1); assert.equal(pair.after.work.entriesCopied, 1); assert.deepEqual({ ...pair.after, work: { ...pair.after.work, batchArrays: pair.before.work.batchArrays, entriesCopied: pair.before.work.entriesCopied } }, pair.before); operations.fast = pair.after.work;
});

await check('outage urgency and recovery retain the existing normal display budget', () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const c = clock(), feed = feedFor(side, c, { unavailable: value => value < 0 }), seen = [];
    const off = feed.subscribe(value => seen.push(value));
    c.set(100); feed.push(1); c.set(150); feed.push(-2); assert.equal(seen.at(-1), -2);
    c.set(200); feed.push(3); c.set(300); feed.push(-4); assert.equal(seen.at(-1), -2);
    c.at(1000); assert.equal(seen.at(-1), -4); c.set(1100); feed.push(5); c.at(2000); assert.equal(seen.at(-1), 5);
    pair[side] = { seen, work: { ...c.counters } }; off(); feed.dispose(); assert.equal(c.timers.size, 0);
  }
  assert.deepEqual(pair.after, pair.before);
});

for (const [file, pin] of Object.entries(pins.after)) assert.equal(hash(fs.readFileSync(path.join(root, file))), pin, 'Changed tested source: ' + file);
const result = { ok: true, cases: checks.length, checks, traces, operations, sourcePins: pins,
  scope: 'Actual frozen/current feed, applied-TDP feed and runtime receipt store. Real reentrant callbacks and virtual time only. No desktop, app, hardware write, game or CPU percentage benchmark.' };
fs.writeFileSync(path.join(task, 'FEED-SUBSCRIPTION-RESULT.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify({ ok: true, cases: checks.length, traces, operations }, null, 2));
