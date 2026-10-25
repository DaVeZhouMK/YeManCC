// Actual production read singleton with inert IPC and a virtual clock. The
// observers count real promise handoffs; they do not pause production execution.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const root = process.cwd();
const task = path.resolve(process.env.YMCC_READ_SESSION_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-read-session-retirement-20261016');
const out = path.join(task, 'tests'), baseline = path.join(task, 'before');
fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(path.join(root, 'package.json'))('esbuild');
const io = globalThis.__monitorReadSessionIO = {}, pins = {}, checks = [], traces = {}, operations = {};
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const settle = async () => { for (let i = 0; i < 30; ++i) await Promise.resolve(); };

function state() {
  let now = 200000, next = 0, reply = null, resolveReply;
  const timers = new Map(), events = { bar: [], hud: [] }, calls = [];
  const counts = { clones: 0, arrayItems: 0, parsed: 0, captures: 0, pendingReceipts: 0, consumed: 0, fileReads: 0 };
  return {
    timers, events, calls, counts,
    get now() { return now; },
    clone(view) {
      ++counts.clones;
      counts.arrayItems += (view.top?.coreUsage?.length || 0) + (view.top?.coreLabels?.length || 0) + (view.fps?.frameTimes?.length || 0);
      return structuredClone(view);
    },
    parsed() { ++counts.parsed; },
    capture() { ++counts.captures; ++counts.pendingReceipts; },
    consume(deleted) { if (deleted) { ++counts.consumed; --counts.pendingReceipts; } return deleted; },
    setReply(value) { reply = value; },
    defer() { reply = new Promise(resolve => { resolveReply = resolve; }); },
    resolve(value) { resolveReply(value); },
    setTimer(fn, ms) { timers.set(++next, { fn, at: now + ms }); return next; },
    clearTimer(id) { timers.delete(id); },
    runAt(value) {
      now = value;
      for (let i = 0; i < 100; ++i) {
        const due = [...timers].filter(([, timer]) => timer.at <= now);
        if (!due.length) return;
        for (const [id, timer] of due) { timers.delete(id); timer.fn(); }
      }
      throw Error('Unexpected timer loop');
    },
    async readTextFile() { ++counts.fileReads; throw Error('Unexpected compatibility-file read'); },
    async invoke(cmd, args) {
      calls.push({ cmd, args });
      if (cmd === 'monitor.snapshot') return reply;
      return { ok: true, top: args?.top === true, fps: args?.fps === true, cleaned: true };
    },
    reset() { for (const key of Object.keys(counts)) counts[key] = 0; },
  };
}

async function opened(side, tag) {
  const name = side + '-' + tag, base = side === 'before' ? baseline : root;
  const h = io[name] = state(), ref = 'globalThis.__monitorReadSessionIO[' + JSON.stringify(name) + ']';
  const plugin = { name, setup(b) {
    b.onResolve({ filter: /^\.\/(ipc|api|monitorAppliedTdp)$/ }, a => ({ path: a.path, namespace: 'inert' }));
    b.onLoad({ filter: /.*/, namespace: 'inert' }, a => ({ loader: 'ts', contents:
      a.path === './ipc' ? 'export async function invoke(...args){return ' + ref + '.invoke(...args)}export function on(){return()=>{}}' :
      a.path === './api' ? 'export const fs={readTextFile:(...args)=>' + ref + '.readTextFile(...args)};' :
      'export function advanceMonitorTdpDisplay(){}export function subscribeMonitorTdpState(){return()=>{}}',
    }));
    b.onLoad({ filter: /\.ts$/ }, a => {
      let text = fs.readFileSync(a.path, 'utf8');
      const relative = path.relative(base, a.path).replaceAll('\\', '/');
      (pins[side] ??= {})[relative] = hash(text);
      if (a.path.endsWith('monitorData.ts')) {
        const observe = (needle, replacement) => { assert.equal(text.split(needle).length, 2, needle); text = text.replace(needle, replacement); };
        observe('if (rememberRead) readSourceFloors?.set(view, stopFloorRevision);',
          'if (rememberRead) { ' + ref + '.parsed(view); readSourceFloors?.set(view, stopFloorRevision); }');
        observe('readDisplaySources.set(copy, source);', ref + '.capture(copy, source); readDisplaySources.set(copy, source);');
        observe('readDisplaySources.delete(snapshot)', ref + '.consume(readDisplaySources.delete(snapshot))');
        text = text.replaceAll('Date.now()', ref + '.now').replaceAll('performance.now()', ref + '.now')
          .replaceAll('structuredClone(', ref + '.clone(').replaceAll('setTimeout(', ref + '.setTimer(').replaceAll('clearTimeout(', ref + '.clearTimer(');
      }
      return { contents: text, loader: 'ts', resolveDir: path.dirname(a.path) };
    });
  } };
  const contents = 'export * from ' + JSON.stringify(path.join(base, 'src/bridge/monitorData.ts')) + ';';
  const compiled = await build({ stdin: { contents, loader: 'ts', resolveDir: root }, bundle: true, write: false, platform: 'node', format: 'esm', plugins: [plugin] });
  const file = path.join(out, name + '.mjs');
  fs.writeFileSync(file, compiled.outputFiles[0].text);
  const m = await import(pathToFileURL(file).href + '?' + Date.now());
  const offBar = m.subscribeMonitorSnapshot(view => h.events.bar.push(view));
  const offHud = m.subscribeMonitorHudSnapshot(view => h.events.hud.push(view));
  const release = m.acquireMonitorDemand('session-test', { top: true, fps: true });
  await settle(); h.reset();
  return { m, h, release, async close() {
    offBar(); offHud(); release(); await settle();
    assert.equal(h.timers.size, 0); assert.equal(h.counts.pendingReceipts, 0);
  } };
}

const top = () => ({ ts: 200000, hwDown: false, tdpW: 30, tempC: 65, freqMhz: 4000, cpuUsage: 20, cpuUsageValid: true,
  cpuUsageTs: 200000, hasBattery: false, coreUsage: Array.from({ length: 128 }, (_, i) => i % 101),
  coreLabels: Array.from({ length: 128 }, (_, i) => 'CPU ' + i), coreUsageTs: 200000 });
const fps = () => ({ ts: 200000, fps: 120, fps1: 90, gpu: 80, packagePower: 30, game: 'fixture', pid: 2077,
  processCreated: '123456789', fpsSource: 'rtss', fpsObservedAt: 200000, fpsValidUntil: 205000, fpsPollPeriodMs: 0,
  frametime: 9, frameTimeSource: 'rtss-current-frame', frameTimeTs: 200000, frameTimeValidUntil: 205000,
  frameTimePollPeriodMs: 0, frameTimes: Array.from({ length: 120 }, (_, i) => 8 + i % 2) });
const packet = (patch = {}) => ({ schema: 1, available: true, session: 1, relayEpoch: 0, generation: 1, seq: 1,
  collectedAt: 200000, heartbeatAt: 200000, hwinfoOkAt: 200000, topActive: true, fpsActive: true, hwDown: false,
  top: JSON.stringify(top()), fps: JSON.stringify(fps()), ...patch });
const nextSession = (session = 2) => packet({ session, top: JSON.stringify({ ...top(), tdpW: 45 }) });
const brief = view => view ? { session: view.session, tdpW: view.top?.tdpW, fps: view.fps?.fps } : null;
async function atPhase(h, phase, action, count = 1) {
  for (let i = 0; i < 40; ++i) {
    await Promise.resolve();
    if ((phase === 'parsed' ? h.counts.parsed >= count && h.counts.clones === 0 : h.counts.captures >= count) && !h.events.hud.some(Boolean)) {
      action(); return;
    }
  }
  throw Error('Missed actual ' + phase + ' handoff');
}
async function check(name, run) {
  try { await run(); checks.push(name); }
  catch (error) { console.error('Failed:', name); throw error; }
}
function assertReplacement(side, returned) {
  if (side === 'before') assert.deepEqual(brief(returned), { session: 1, tdpW: 30, fps: 120 });
  else assert.equal(returned, null, 'A retired read must not return its measurements or substitute the newer publisher');
}

for (const phase of ['parsed', 'cloned']) await check('replacement after ' + phase + ' rejects retired control read', async () => {
  const pair = {};
  for (const side of ['before', 'after']) {
    const s = await opened(side, phase), { m, h } = s;
    h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
    await atPhase(h, phase, () => m.acceptPublishedMonitorSnapshot(nextSession()));
    const returned = await pending; assertReplacement(side, returned);
    h.runAt(201001); await settle();
    for (const lane of ['bar', 'hud']) assert.deepEqual(brief(h.events[lane].at(-1)), { session: 2, tdpW: 45, fps: 120 });
    pair[side] = { returned: brief(returned), displayed: brief(h.events.hud.at(-1)), counts: { ...h.counts } };
    await s.close();
  }
  traces[phase] = pair;
});

for (const lane of ['bar', 'hud']) for (const unavailable of [false, true]) await check('replacement inside ' + lane + ' callback, unavailable=' + unavailable, async () => {
  for (const side of ['before', 'after']) {
    const s = await opened(side, 'callback-' + lane + '-' + unavailable), { m, h } = s;
    let fired = false;
    const off = (lane === 'bar' ? m.subscribeMonitorSnapshot : m.subscribeMonitorHudSnapshot)(view => {
      if (view?.session === 1 && !fired) { fired = true; m.acceptPublishedMonitorSnapshot(nextSession(2));
        if (unavailable) m.acceptPublishedMonitorSnapshot(packet({ session: 3, available: false })); }
    });
    h.setReply(packet()); const returned = await m.readMonitorSnapshot();
    assert(fired); assertReplacement(side, returned); h.runAt(201001); await settle();
    for (const target of ['bar', 'hud']) assert.deepEqual(brief(h.events[target].at(-1)), unavailable ? null : { session: 2, tdpW: 45, fps: 120 });
    off(); await s.close();
  }
});

await check('unavailable replacement retires read before return without parsing its ignored payload', async () => {
  for (const side of ['before', 'after']) {
    const s = await opened(side, 'unavailable'), { m, h } = s;
    h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
    await atPhase(h, 'cloned', () => m.acceptPublishedMonitorSnapshot(packet({ session: 2, available: false, top: '{invalid', fps: 'x'.repeat(20000) })));
    assertReplacement(side, await pending);
    assert.equal(h.events.hud.at(-1), null); assert.equal(h.events.bar.at(-1), null); await s.close();
  }
});

for (const duringCallback of [false, true]) await check('two shared readers retire independently, callback=' + duringCallback, async () => {
  for (const side of ['before', 'after']) {
    const s = await opened(side, 'shared-' + duringCallback), { m, h } = s;
    let fired = false, off = () => {};
    if (duringCallback) off = m.subscribeMonitorSnapshot(view => {
      if (view?.session === 1 && !fired) { fired = true; m.acceptPublishedMonitorSnapshot(nextSession()); }
    });
    h.defer(); const a = m.readMonitorSnapshot(), b = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
    if (!duringCallback) await atPhase(h, 'cloned', () => m.acceptPublishedMonitorSnapshot(nextSession()), 2);
    const values = await Promise.all([a, b]); for (const value of values) assertReplacement(side, value);
    h.runAt(201001); await settle();
    assert.equal(h.calls.filter(call => call.cmd === 'monitor.snapshot').length, 1);
    assert.equal(h.counts.clones, 2); assert.equal(h.counts.pendingReceipts, 0);
    assert.equal(h.events.hud.at(-1).session, 2); off(); await s.close();
  }
});

await check('multiple replacements retain only the final publisher', async () => {
  const s = await opened('after', 'multiple'), { m, h } = s;
  h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
  await atPhase(h, 'cloned', () => { m.acceptPublishedMonitorSnapshot(nextSession(2)); m.acceptPublishedMonitorSnapshot(nextSession(3)); });
  assert.equal(await pending, null); h.runAt(201001); assert.equal(h.events.hud.at(-1).session, 3); await s.close();
});

await check('known publisher retirement also rejects an anonymous compatibility read', async () => {
  const s = await opened('after', 'anonymous-old'), { m, h } = s;
  m.acceptPublishedMonitorSnapshot(packet()); h.runAt(201000); h.events.bar.length = h.events.hud.length = 0; h.reset();
  h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet({ session: null, seq: 2 }));
  await atPhase(h, 'cloned', () => m.acceptPublishedMonitorSnapshot(nextSession()));
  assert.equal(await pending, null); h.runAt(202001); assert.equal(h.events.hud.at(-1).session, 2); await s.close();
});

await check('first identity discovery preserves an anonymous read and the newer display', async () => {
  const s = await opened('after', 'discovery'), { m, h } = s;
  h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet({ session: null }));
  await atPhase(h, 'cloned', () => m.acceptPublishedMonitorSnapshot(nextSession()));
  const value = await pending; assert.equal(value.session, null); assert.equal(value.top.tdpW, 30);
  h.runAt(201001); assert.equal(h.events.hud.at(-1).session, 2); await s.close();
});

for (const generation of [1, 2]) await check('same-session seq/generation advance preserves admitted control read: ' + generation, async () => {
  const s = await opened('after', 'same-' + generation), { m, h } = s;
  h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
  await atPhase(h, 'cloned', () => m.acceptPublishedMonitorSnapshot(packet({ generation, seq: 2, top: JSON.stringify({ ...top(), tdpW: 45 }) })));
  assert.equal((await pending).top.tdpW, 30); h.runAt(201001); assert.equal(h.events.hud.at(-1).top.tdpW, 45); await s.close();
});

for (const mode of ['malformed', 'oversized', 'future', 'bad-schema', 'bad-sequence']) await check('invalid replacement cannot retire read: ' + mode, async () => {
  const s = await opened('after', 'invalid-' + mode), { m, h } = s;
  const patch = mode === 'malformed' ? { top: '{invalid' } : mode === 'oversized' ? { top: 'x'.repeat(20000) } :
    mode === 'future' ? { collectedAt: 200001 } : mode === 'bad-schema' ? { schema: 999 } : { seq: -1 };
  h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
  await atPhase(h, 'cloned', () => m.acceptPublishedMonitorSnapshot({ ...nextSession(), ...patch }));
  assert.equal((await pending).top.tdpW, 30); assert.equal(h.events.hud.at(-1).session, 1); await s.close();
});

await check('equivalent demand replacement and additional owner do not retire valid read', async () => {
  for (const mode of ['replace', 'additional']) {
    const s = await opened('after', 'demand-' + mode), { m, h } = s;
    h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet()); let release;
    await atPhase(h, 'cloned', () => { release = m.acquireMonitorDemand(mode === 'replace' ? 'session-test' : 'additional', { top: true, fps: true }); });
    assert.equal((await pending).top.tdpW, 30); release(); await s.close();
  }
});

for (const stopFirst of [false, true]) await check('stop and session replacement share the handoff, stopFirst=' + stopFirst, async () => {
  const s = await opened('after', 'stop-order-' + stopFirst), { m, h } = s;
  h.defer(); const pending = m.readMonitorSnapshot(); await settle(); h.resolve(packet());
  await atPhase(h, 'cloned', () => {
    h.runAt(200001);
    if (stopFirst) s.release();
    m.acceptPublishedMonitorSnapshot(nextSession());
    if (!stopFirst) s.release();
  });
  assert.equal(await pending, null); await s.close();
});

await check('callback stop filtering cannot make a retired session current again', async () => {
  const s = await opened('after', 'callback-stop-session'), { m, h } = s; let fired = false;
  const off = m.subscribeMonitorSnapshot(view => {
    if (view?.session === 1 && !fired) { fired = true; h.runAt(200001); s.release(); m.acceptPublishedMonitorSnapshot(nextSession()); }
  });
  h.setReply(packet()); assert.equal(await m.readMonitorSnapshot(), null); assert(fired); off(); await s.close();
});

await check('next valid read uses new publisher; returned copies stay isolated', async () => {
  const s = await opened('after', 'recovery'), { m, h } = s;
  m.acceptPublishedMonitorSnapshot(nextSession()); h.setReply(nextSession());
  const value = await m.readMonitorSnapshot(); assert.equal(value.session, 2); value.top.tdpW = 999;
  h.runAt(201001); assert.equal(h.events.hud.at(-1).top.tdpW, 45); await s.close();
});

await check('200 ordinary reads retain one control clone each', async () => {
  for (const side of ['before', 'after']) {
    const s = await opened(side, 'normal'), { m, h } = s; h.setReply(packet());
    for (let i = 0; i < 200; ++i) { const value = await m.readMonitorSnapshot(); assert.equal(value.top.tdpW, 30); value.top.tdpW = 999; }
    assert.equal(h.counts.clones, 200); assert.equal(h.counts.arrayItems, 75200);
    assert.equal(h.calls.filter(call => call.cmd === 'monitor.snapshot').length, 200);
    assert.equal(h.counts.pendingReceipts, 0); operations[side] = { ...h.counts }; await s.close();
  }
});

await check('200 high-frequency pushes allocate no control clones or receipts and issue no read', async () => {
  const s = await opened('after', 'pushes'), { m, h } = s, commands = h.calls.length;
  for (let i = 0; i < 200; ++i) {
    const at = 200000 + i * 100; h.runAt(at);
    m.acceptPublishedMonitorSnapshot(packet({ seq: i + 1, collectedAt: at, top: JSON.stringify({ ...top(), ts: at, cpuUsageTs: at, coreUsageTs: at }),
      fps: JSON.stringify({ ...fps(), ts: at, fpsObservedAt: at, fpsValidUntil: at + 5000, frameTimeTs: at, frameTimeValidUntil: at + 5000 }) }));
  }
  h.runAt(221000); assert.equal(h.counts.clones, 0); assert.equal(h.counts.captures, 0);
  assert.equal(h.calls.length, commands); operations.pushes = { ...h.counts }; await s.close();
});

for (const [file, pin] of Object.entries(pins.after)) assert.equal(hash(fs.readFileSync(path.join(root, file))), pin, 'Input changed during session checks: ' + file);
const result = { ok: true, cases: checks.length, checks, traces, operations, sourcePins: pins,
  scope: 'Actual frozen/current production parser, shared read singleton and display feeds with native-like synthetic packets, virtual time and real promise ordering. No live hardware, deployment, game or CPU percentage benchmark.' };
fs.writeFileSync(path.join(task, 'READ-SESSION-RESULT.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify({ ok: true, cases: checks.length, operations, testedSourceFiles: Object.keys(pins.after).length }, null, 2));
