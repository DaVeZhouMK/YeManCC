// Synthetic unified snapshots only. Never reads sensors/process counters or installed HUD.
import assert from 'node:assert/strict';
import { coreUsageValues, coreUsageLabels, coreTileLayout, coreTileUsage } from '@/bridge/monitorCoreTiles';
import { createMonitorDataHub, type MonitorSnapshotView } from '@/bridge/monitorData';
import { hudFromUniformSnapshot, boundedHudData } from '@/bridge/monitorHudModel';
import { monitorSnapshotDisplayKey, createLatestDisplayFeed } from '@/bridge/monitorDisplayFeed';
const now = 1791691000000;
const cases: string[] = [];
const check = (name: string, run: () => void) => { run(); cases.push(name); };
function snapshot(top: object | null, overrides: object = {}): MonitorSnapshotView {
  return { schema: 1, session: 1, relayEpoch: 0, generation: 1, seq: 1, collectedAt: now, heartbeatAt: null, hwinfoOkAt: now, topActive: true, fpsActive: false, hwDown: false, source: 'snapshot', top: top as MonitorSnapshotView['top'], fps: null, ...overrides };
}
const top = (usage: unknown = [0, 50, 100, null]) => ({ ts: now - 100, hwDown: false, cpuUsage: 15, coreUsage: usage, coreLabels: ['CPU0', 'CPU1', 'CPU2', 'CPU3'] });
check('Visuals use one short row and at most 16 real items; unified arrays remain independently bounded', () => {
  for (const n of [0, 1, 8, 16, 24, 32, 64, 128]) { const l = coreTileLayout(n); assert.equal(l.total, Math.min(n, 16)); assert.equal(l.columns, Math.min(n, 16)); assert.equal(l.rows, n ? 1 : 0); }
  assert.equal(coreTileLayout(1000).total, 16); assert.equal(coreTileLayout(NaN).total, 0);
});
check('Zero/100/null/invalid/sparse readings stay distinct without fake bars', () => {
  const a = [0, 100, null, undefined, -1, 101, NaN, Infinity, '25']; a.length = 10;
  assert.deepEqual(coreUsageValues(a), [0, 100, null, null, null, null, null, null, null, null]);
  assert.deepEqual(coreUsageValues(undefined), []); assert.equal(coreUsageValues(Array(200).fill(30)).length, 128);
});
check('Missing/stale data renders neutral slots, never fake 0% or visible placeholder text', () => {
  assert.deepEqual(coreTileUsage(undefined), Array(16).fill(null));
  assert.deepEqual(coreTileUsage([]), Array(16).fill(null));
  assert.deepEqual(coreTileUsage([0, 50, 100, null]), [0, 50, 100, null]);
  assert.deepEqual(coreTileUsage([0, 50, 100, null], true), [null, null, null, null]);
  assert.equal(coreTileUsage(Array(128).fill(50)).length, 16);
});
check('Identity labels stay aligned and bounded, never guess missing core topology', () => {
  assert.deepEqual(coreUsageLabels([' G1/CPU0 ', null, 'P2/T1', 'extra'], 3), ['G1/CPU0', '', 'P2/T1']);
  assert.equal(coreUsageLabels(['x'.repeat(200)], 1)[0].length, 24);
});
check('Real HUD mapping consumes existing unified arrays and preserves null slots', () => {
  const data = hudFromUniformSnapshot(snapshot(top()), now); assert.deepEqual(data.coreUsage, [0, 50, 100, null]); assert.deepEqual(data.coreLabels, ['CPU0', 'CPU1', 'CPU2', 'CPU3']); assert.equal(data.source, 'ymcc-uniform');
});
check('Old unified versions without arrays stay empty rather than inventing 16 values', () => {
  assert.deepEqual(hudFromUniformSnapshot(snapshot({ts: now - 100, cpuUsage: 60}), now).coreUsage, []);
});
check('Fresh FPS cannot revive expired or hw-down legacy core data', () => {
  const fps = { ts: now - 50, fps: 60, fps1: 50, gpu: 20 };
  for (const t of [{...top(), ts: now - 6000}, {...top(), hwDown: true}]) assert.deepEqual(hudFromUniformSnapshot(snapshot(t, { fps }), now).coreUsage, []);
});
check('Independent publisher core timestamp survives HWiNFO outage without stale temperatures', () => {
  const data = hudFromUniformSnapshot(snapshot({...top(), ts: now - 9000, hwDown: true, tempC: 90, coreUsageTs: now - 100}, {hwDown: true}), now);
  assert.deepEqual(data.coreUsage, [0, 50, 100, null]); assert.equal(data.stale, false); assert.equal(data.values.cpuTemp, undefined);
  const unknown = hudFromUniformSnapshot(snapshot({...top([null, null]), ts: 0, hwDown: true, coreUsageTs: now - 100}, {hwDown: true}), now); assert.equal(unknown.stale, false); assert.deepEqual(unknown.coreUsage, [null, null]);
});
check('Independent stale/future/zero/NaN timestamps never refresh themselves from top or FPS', () => {
  for (const ts of [now - 6000, now + 1, 0, NaN]) assert.deepEqual(hudFromUniformSnapshot(snapshot({...top(), coreUsageTs: ts}), now).coreUsage, []);
});
check('Packet admission retains publisher labels and nulls, truncates malicious excess', () => {
  const data = hudFromUniformSnapshot(snapshot(top(Array(200).fill(30))), now); data.coreUsage[3] = 120; data.coreLabels = Array(200).fill('G1/CPU0');
  const packet = boundedHudData(data); assert.equal(packet.coreUsage.length, 128); assert.equal(packet.coreLabels?.length, 128); assert.equal(packet.coreUsage[3], null);
});
check('Display dedup key observes independent core updates; existing gate stays 1Hz', () => {
  const a = snapshot({...top(), coreUsageTs: now - 200}), b = snapshot({...top(), coreUsageTs: now - 100}); assert.notEqual(monitorSnapshotDisplayKey(a), monitorSnapshotDisplayKey(b));
  let clock = 0; const pending: Array<() => void> = [], sent: Array<MonitorSnapshotView | null> = [];
  const feed = createLatestDisplayFeed<MonitorSnapshotView | null>(null, { now: () => clock, key: monitorSnapshotDisplayKey, setTimer: fn => {pending.push(fn); return pending.length;}, clearTimer: () => {} });
  const off = feed.subscribe(v => sent.push(v)); clock = 1000; feed.push(a); clock = 1100; feed.push(b); assert.equal(sent.length, 2); clock = 2000; pending.splice(0).forEach(fn => fn()); assert.equal(sent.length, 3); assert.equal(sent[2]?.top?.coreUsageTs, now - 100); off(); feed.dispose();
});
async function testUnifiedParser() {
  let payload: object = {...top(), coreUsageTs: now - 100}; const commands: string[] = []; let reads = 0;
  const hub = createMonitorDataHub({ now: () => now, setTimer: () => 1, clearTimer: () => {}, readTextFile: async () => {throw Error('Consumer must not add file reads');}, invoke: async command => {commands.push(command); if(command === 'monitor.snapshot') {reads++; return {schema:1, available:true, session:1, generation:1, seq:reads, collectedAt:now, topActive:true, fpsActive:false, hwDown:false, top:JSON.stringify(payload), fps:null};} return {ok:true, cleaned:true};} });
  const release = hub.acquire('core-consumer-test', {top:true}); await new Promise(resolve => setImmediate(resolve));
  const [a, b] = await Promise.all([hub.read(), hub.read()]); assert.equal(reads, 1); assert.deepEqual(a?.top?.coreUsage, [0,50,100,null]); assert.equal(a?.top?.coreUsageTs, now - 100); assert.deepEqual(hudFromUniformSnapshot(a, now).coreUsage, [0,50,100,null]);
  a!.top!.coreUsage![0] = 88; a!.top!.coreLabels![0] = 'mutated'; assert.equal(b?.top?.coreUsage?.[0], 0); assert.equal(b?.top?.coreLabels?.[0], 'CPU0');
  payload = {...top(), coreUsageTs:'invalid'}; const malformed = await hub.read(); assert.equal(malformed?.top?.coreUsageTs, 0); assert.deepEqual(hudFromUniformSnapshot(malformed, now).coreUsage, []);
  assert(commands.every(c => ['monitor.start','monitor.stop','monitor.takeover','monitor.snapshot'].includes(c))); release(); cases.push('Production unified parser → HUD model: arrays/labels/timestamp, merged IPC, clone isolation and no secondary reads');
}
await testUnifiedParser();
console.log(JSON.stringify({passed:cases.length,cases,scope:'Consumer unit tests with synthetic unified snapshots; no hardware, game or installed HUD acceptance'}, null, 2));
