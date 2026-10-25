// Exercise the real pure parsers, byte/lifecycle gate and shared production
// singleton. Synthetic text/IPC/clock only; no installed app or hardware.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
const root = process.cwd();
const task = path.resolve(process.env.YMCC_INVALID_PAYLOAD_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-invalid-payload-cache-20261016');
const out = path.join(task, 'tests'), baseline = path.join(task, 'before');
fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(path.join(root, 'package.json'))('esbuild');
const io = globalThis.__monitorInvalidIO = {}, pins = {}, checks = [], operations = {};
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const settle = async () => { for (let i = 0; i < 30; ++i) await Promise.resolve(); };
function state() {
  let next = 0, reply = null;
  const timers = new Map(), calls = [], counts = { topParses: 0, fpsParses: 0, clones: 0, fileReads: 0 };
  return { now: 200000, timers, calls, counts,
    reset() { for (const key of Object.keys(counts)) counts[key] = 0; calls.length = 0; },
    clone(value) { ++counts.clones; return structuredClone(value); },
    setTimer(fn, ms) { timers.set(++next, { fn, at: this.now + ms }); return next; },
    clearTimer(id) { timers.delete(id); },
    setReply(value) { reply = value; },
    async invoke(cmd, args) { calls.push({ cmd, args }); return cmd === 'monitor.snapshot' ? reply : { ok: true, cleaned: true, top: args?.top === true, fps: args?.fps === true }; },
    async readTextFile() { ++counts.fileReads; throw Error('Unexpected fallback read'); },
  };
}
async function load(side) {
  const base = side === 'before' ? baseline : root, h = io[side] = state();
  const ref = 'globalThis.__monitorInvalidIO[' + JSON.stringify(side) + ']';
  const plugin = { name: 'invalid-' + side, setup(b) {
    b.onResolve({ filter: /^\.\/(ipc|api|monitorAppliedTdp)$/ }, a => ({ path: a.path, namespace: 'inert' }));
    b.onLoad({ filter: /.*/, namespace: 'inert' }, a => ({ loader: 'ts', contents:
      a.path === './ipc' ? 'export async function invoke(...args){return ' + ref + '.invoke(...args)}export function on(){return()=>{}}' :
      a.path === './api' ? 'export const fs={readTextFile:(...args)=>' + ref + '.readTextFile(...args)};' :
      'export function advanceMonitorTdpDisplay(){}export function subscribeMonitorTdpState(){return()=>{}}',
    }));
    b.onLoad({ filter: /\.ts$/ }, a => {
      let text = fs.readFileSync(a.path, 'utf8');
      (pins[side] ??= {})[path.relative(base, a.path).replaceAll('\\', '/')] = hash(text);
      if (a.path.endsWith('monitorData.ts')) {
        for (const [needle, counter] of [['function parseTopPayload(raw: string): TopMonData | null {', 'topParses'], ['function parseFpsPayload(raw: string): MonitorFpsPayload | null {', 'fpsParses']]) {
          assert.equal(text.split(needle).length, 2); text = text.replace(needle, needle + ' ++' + ref + '.counts.' + counter + ';');
        }
        text = text.replaceAll('Date.now()', ref + '.now').replaceAll('performance.now()', ref + '.now')
          .replaceAll('structuredClone(', ref + '.clone(').replaceAll('setTimeout(', ref + '.setTimer(').replaceAll('clearTimeout(', ref + '.clearTimer(');
      }
      return { contents: text, loader: 'ts', resolveDir: path.dirname(a.path) };
    });
  } };
  const contents = ['monitorData', 'monitorPayloadDecoder', 'monitorHudModel'].map(name => 'export * from ' + JSON.stringify(path.join(base, 'src/bridge/' + name + '.ts')) + ';').join('\n');
  const compiled = await build({ stdin: { contents, loader: 'ts', resolveDir: root }, bundle: true, platform: 'node', format: 'esm', write: false, plugins: [plugin] });
  const file = path.join(out, side + '.mjs'); fs.writeFileSync(file, compiled.outputFiles[0].text);
  return { m: await import(pathToFileURL(file).href + '?' + Date.now()), h };
}
const sides = { before: await load('before'), after: await load('after') };
const top = () => ({ ts: 200000, hwDown: false, tdpW: 30, tempC: 65, freqMhz: 4000, cpuUsage: 20,
  cpuUsageValid: true, cpuUsageTs: 200000, coreUsageTs: 200000, coreUsage: [10, 20, 30], coreLabels: ['CPU1', 'CPU2', 'CPU3'],
  hasBattery: false, gpuName: 'Selected fixture GPU', gpuUsagePct: 70, gpuSelectionPresent: true, gpuSelectionPinned: true,
  gpuMemoryUsedMb: 1000, gpuMemoryTotalMb: 8000, physicalMemoryUsedMb: 8000, physicalMemoryAvailableMb: 8000, physicalMemoryTotalMb: 16000, physicalMemoryLoadPct: 50 });
const fps = () => ({ ts: 200000, fps: 120, fps1: 90, gpu: 70, packagePower: 30, game: 'fixture', pid: 2077,
  processCreated: '123456789', fpsSource: 'rtss', fpsObservedAt: 200000, fpsValidUntil: 205000, fpsPollPeriodMs: 0,
  frametime: 8.3, frameTimeSource: 'rtss-current-frame', frameTimeTs: 200000, frameTimeValidUntil: 205000,
  frameTimePollPeriodMs: 0, frameTimes: [8, 8.3] ,fps1Source:'hwinfo-presented-1percent-low',fps1ObservedAt:Math.floor((200000)/1000)*1000,fps1ValidUntil:Math.floor((200000)/1000)*1000+5000,fps1PollPeriodMs:1000});
const packet = (patch = {}) => ({ schema: 1, available: true, session: 1, generation: 1, seq: 1, relayEpoch: 0,
  collectedAt: 200000, heartbeatAt: 200000, hwinfoOkAt: 200000, topActive: false, fpsActive: false,
  top: JSON.stringify(top()), fps: JSON.stringify(fps()), ...patch });
const malformedTop = '{"ts":200000,"padding":"' + 'x'.repeat(8000) + '"}x';
const malformedFps = '{"fps":120,"padding":"' + 'x'.repeat(8000) + '"}x';
function hubFor(side) {
  const { m, h } = sides[side]; h.reset();
  return m.createMonitorDataHub({ now: () => h.now, invoke: (cmd, args) => h.invoke(cmd, args),
    readTextFile: (...args) => h.readTextFile(...args), setTimer: (fn, ms) => h.setTimer(fn, ms), clearTimer: id => h.clearTimer(id) });
}
async function check(name, run) { try { await run(); checks.push(name); } catch (error) { console.error('Failed:', name); throw error; } }

for (const family of ['top', 'fps']) for (const mode of ['malformed', 'structurally-invalid']) await check('200 identical rejected ' + family + ' documents, ' + mode, async () => {
  const result = {};
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    const bad = mode === 'malformed' ? family === 'top' ? malformedTop : malformedFps : family === 'top' ? '[]' : JSON.stringify({ ...fps(), fps: '120' });
    for (let i = 0; i < 200; ++i) {
      h.setReply(packet({ session: 2, seq: i + 100, [family]: bad }));
      assert.equal(await hub.read(), null); assert.equal(hub.session(), null);
    }
    assert.equal(h.counts[family + 'Parses'], side === 'before' ? 200 : 1);
    assert.equal(h.counts[family === 'top' ? 'fpsParses' : 'topParses'], 1);
    assert.equal(h.counts.fileReads, 0); assert.equal(h.counts.clones, 0);
    assert.equal(h.calls.filter(call => call.cmd === 'monitor.snapshot').length, 200);
    assert.deepEqual(hub.pending(), { top: false, fps: false });
    result[side] = { ...h.counts };
  }
  operations[family + '-' + mode] = result;
});

await check('a changed valid payload recovers on the very next read without retiring the old publisher', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side]; h.setReply(packet()); assert(await hub.read());
    for (let i = 0; i < 20; ++i) { h.setReply(packet({ session: 2, seq: 99999, top: malformedTop })); assert.equal(await hub.read(), null); }
    assert.equal(hub.session(), 1);
    h.setReply(packet({ seq: 2, top: JSON.stringify({ ...top(), tdpW: 45 }) }));
    const value = await hub.read(); assert.equal(value.session, 1); assert.equal(value.seq, 2); assert.equal(value.top.tdpW, 45);
  }
});

await check('valid-invalid-valid-old-invalid replaces one slot and never caches good values as a failed result', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    for (const [i, wire] of [JSON.stringify(top()), malformedTop, JSON.stringify(top()), malformedTop, malformedTop].entries()) {
      h.setReply(packet({ seq: i + 1, top: wire })); const value = await hub.read(); assert.equal(value === null, wire === malformedTop);
    }
    assert.equal(h.counts.topParses, side === 'before' ? 5 : 4);
  }
});

await check('changing bad text is retried every time, with no growing history cache', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    for (let i = 0; i < 200; ++i) { h.setReply(packet({ seq: i + 1, top: i % 2 ? malformedTop : malformedTop + 'x' })); assert.equal(await hub.read(), null); }
    assert.equal(h.counts.topParses, 200);
  }
});

await check('a stable bad top does not suppress revalidation of changing FPS payloads', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    for (let i = 0; i < 200; ++i) { h.setReply(packet({ seq: i + 1, top: malformedTop, fps: JSON.stringify({ ...fps(), fps: i + 1 }) })); assert.equal(await hub.read(), null); }
    assert.equal(h.counts.topParses, side === 'before' ? 200 : 1); assert.equal(h.counts.fpsParses, 200);
  }
});

await check('200 valid repeated reads keep parser reuse, fresh outer objects and mutable control isolation', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side]; h.setReply(packet()); let previous;
    for (let i = 0; i < 200; ++i) {
      const value = await hub.read(); assert.equal(value.top.tdpW, 30); assert.deepEqual(value.top.coreUsage, [10, 20, 30]);
      assert.notStrictEqual(value, previous); value.top.tdpW = 999; value.top.coreUsage[0] = 999; previous = value;
    }
    assert.equal(h.counts.topParses, 1); assert.equal(h.counts.fpsParses, 1); assert.equal(h.counts.clones, 200);
    operations['valid-' + side] = { ...h.counts };
  }
});

await check('200 normal high-frequency publications retain original parse work and use no control clones', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    for (let i = 0; i < 200; ++i) assert(hub.admitDisplayPublication(packet({ seq: i + 1 })));
    assert.equal(h.counts.topParses, 1); assert.equal(h.counts.fpsParses, 1); assert.equal(h.counts.clones, 0); assert.equal(h.calls.length, 0);
    operations['valid-push-' + side] = { ...h.counts };
  }
});

await check('oversized UTF-8 and combined documents still fail before either parser or lifecycle admission', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    const padded = (base, pad) => JSON.stringify({ ...base, padding: pad });
    for (const patch of [{ top: malformedTop + 'x'.repeat(10000) }, { top: padded(top(), '坏'.repeat(6000)) },
      { top: padded(top(), 'x'.repeat(12500)), fps: padded(fps(), 'x'.repeat(12500)) }]) {
      for (let i = 0; i < 20; ++i) { h.setReply(packet({ session: 2, seq: 99999, ...patch })); assert.equal(await hub.read(), null); }
    }
    assert.equal(h.counts.topParses, 0); assert.equal(h.counts.fpsParses, 0); assert.equal(hub.session(), null);
  }
});

await check('authoritative unavailable replacement ignores payload text and retires the publisher', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side]; assert(hub.admitDisplayPublication(packet())); h.reset();
    assert.equal(hub.admitDisplayPublication(packet({ session: 2, available: false, top: malformedTop, fps: malformedFps })), null);
    assert.equal(hub.session(), 2); assert.equal(h.counts.topParses, 0); assert.equal(h.counts.fpsParses, 0);
    assert.equal(hub.admitDisplayPublication(packet({ seq: 2 })), undefined);
  }
});

await check('uncacheable legacy nested metadata remains uncached and independent', async () => {
  for (const side of ['before', 'after']) {
    const hub = hubFor(side), { h } = sides[side];
    const legacy = { ts: 200000, fps: 120, fps1: 90, gpu: 70, packagePower: 30, game: 'legacy', pid: 2077, fps1ValidUntil: { odd: true } };
    h.setReply(packet({ fps: JSON.stringify(legacy) }));
    const a = await hub.read(); a.fps.fps1ValidUntil.odd = false; const b = await hub.read();
    assert.equal(b.fps.fps1ValidUntil.odd, true); assert.equal(h.counts.fpsParses, 2);
  }
});

await check('public decoder default preserves an impure caller that becomes available later', async () => {
  for (const side of ['before', 'after']) {
    const { m } = sides[side]; let ready = false, parses = 0;
    const decode = m.createMonitorPayloadDecoder({ parse: raw => { ++parses; return ready ? { raw } : null; }, copy: value => ({ ...value }) });
    assert.equal(decode('same'), null); assert.equal(decode('same'), null); ready = true;
    assert.deepEqual(decode('same'), { raw: 'same' }); assert.equal(parses, 3);
  }
});

for (const count of [0, 16384, 16385, 65537]) await check('explicit pure null reuse has a character cap, length=' + count, async () => {
  for (const side of ['before', 'after']) {
    const { m } = sides[side]; let parses = 0, copies = 0;
    const decode = m.createMonitorPayloadDecoder({ cacheInvalid: true, parse: () => { ++parses; return null; }, copy: value => { ++copies; return value; } });
    const raw = 'x'.repeat(count); for (let i = 0; i < 20; ++i) assert.equal(decode(raw), null);
    assert.equal(parses, side === 'after' && count <= 16384 ? 1 : 20); assert.equal(copies, 0);
  }
});

await check('parse exceptions are never installed as failed results', async () => {
  for (const side of ['before', 'after']) {
    const { m } = sides[side]; let parses = 0;
    const decode = m.createMonitorPayloadDecoder({ cacheInvalid: true, parse: () => { ++parses; throw Error('caller'); }, copy: value => value });
    for (let i = 0; i < 20; ++i) assert.throws(() => decode('x'), /caller/); assert.equal(parses, 20);
  }
});

await check('a refused cacheable value is not installed as either success or failure', async () => {
  for (const side of ['before', 'after']) {
    const { m } = sides[side]; let parses = 0, copies = 0;
    const decode = m.createMonitorPayloadDecoder({ cacheInvalid: true, cacheable: () => false, parse: raw => { ++parses; return { raw }; }, copy: value => { ++copies; return { ...value }; } });
    assert.notStrictEqual(decode('x'), decode('x')); assert.equal(parses, 2); assert.equal(copies, 0);
  }
});

await check('pure helper releases failed state when valid copying resumes', async () => {
  const { m } = sides.after; let parses = 0, copies = 0;
  const decode = m.createMonitorPayloadDecoder({ cacheInvalid: true, parse: raw => { ++parses; return raw === 'bad' ? null : { value: 1 }; }, copy: value => { ++copies; return { ...value }; } });
  assert.equal(decode('bad'), null); assert.equal(decode('bad'), null); const a = decode('good'); a.value = 99;
  assert.equal(decode('good').value, 1); assert.equal(decode('bad'), null); assert.equal(decode('bad'), null);
  assert.equal(parses, 3); assert.equal(copies, 2);
});

await check('2000 complete parser/projection A-B packets preserve values, identities and rejection semantics except retiring unproved RTSS hardware', async () => {
  const before = hubFor('before'), after = hubFor('after');
  for (let i = 0; i < 2000; ++i) {
    const mode = Math.floor(i / 10) % 10;
    const patch = mode === 1 ? { top: malformedTop } : mode === 2 ? { fps: malformedFps } :
      mode === 3 ? { top: '[]' } : mode === 4 ? { fps: JSON.stringify({ ...fps(), processCreated: '0' }) } :
      mode === 5 ? { top: JSON.stringify({ ...top(), tdpW: 0, physicalMemoryLoadPct: 95, coreUsage: [0, 50, null] }) } :
      mode === 6 ? { fps: JSON.stringify({ ...fps(), fps: 0 }) } : mode === 7 ? { top: null } :
      mode === 8 ? { fps: JSON.stringify({ ...fps(), fps1ValidUntil: { invalid: true } }) } : {};
    const wire = packet({ seq: i + 1, ...patch }), a = before.admitDisplayPublication(wire), b = after.admitDisplayPublication(wire);
    // The only intentional compatibility change: old RTSS packets carry no
    // independent HWiNFO proof, so copied GPU/power values are withdrawn.
    if (a?.fps?.fpsSource === 'rtss' && a.fps.hardwareObservedAt === undefined) {
      a.fps.gpu = null; a.fps.packagePower = -1;
    }
    assert.deepEqual(b, a);
    assert.deepEqual(sides.after.m.hudFromUniformSnapshot(b ?? null, 200000), sides.before.m.hudFromUniformSnapshot(a ?? null, 200000));
  }
  operations.fullReplay = { packets: 2000, before: { ...sides.before.h.counts }, after: { ...sides.after.h.counts } };
});

await check('real production feed ignores bad pushes, preserves its last admitted data, then recovers immediately', async () => {
  for (const side of ['before', 'after']) {
    const { m, h } = sides[side]; h.reset(); const bar = [], hud = [];
    const offBar = m.subscribeMonitorSnapshot(value => bar.push(value)), offHud = m.subscribeMonitorHudSnapshot(value => hud.push(value));
    m.acceptPublishedMonitorSnapshot(packet());
    for (let i = 0; i < 200; ++i) m.acceptPublishedMonitorSnapshot(packet({ session: 2, seq: 99999, top: malformedTop }));
    assert.equal(bar.at(-1).top.tdpW, 30); assert.equal(hud.at(-1).session, 1);
    h.now += 1000; m.acceptPublishedMonitorSnapshot(packet({ seq: 2, top: JSON.stringify({ ...top(), tdpW: 45 }) }));
    assert.equal(bar.at(-1).top.tdpW, 45); assert.equal(hud.at(-1).top.tdpW, 45);
    assert.equal(h.counts.topParses, side === 'before' ? 202 : 3); assert.equal(h.calls.length, 0);
    offBar(); offHud(); await settle(); assert.equal(h.timers.size, 0);
  }
});

for (const [file, pin] of Object.entries(pins.after)) assert.equal(hash(fs.readFileSync(path.join(root, file))), pin, 'Source changed during invalid-payload test: ' + file);
const result = { ok: true, cases: checks.length, checks, operations, sourcePins: pins,
  scope: 'Actual frozen/current hub, pure parser cache, display singleton and full HUD projection. Inert IPC, virtual clock and synthetic native-like payloads. Counts are not CPU percentage or live hardware evidence.' };
fs.writeFileSync(path.join(task, 'INVALID-PAYLOAD-RESULT.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify({ ok: true, cases: checks.length, operations }, null, 2));
