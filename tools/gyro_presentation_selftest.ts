import assert from 'node:assert/strict';
import { createGyroPresentationGate } from '../src/bridge/gyroPresentation';
import { normalizeGyroTelemetry } from '../src/bridge/inputContracts';
let checks = 0;
function check(value: unknown, name: string) { assert.ok(value, name); checks++; }
const frame = (sequence: number) => normalizeGyroTelemetry({
  sequence, timestampUtc: '2026-10-03T00:00:00Z', sensorPresent: true,
  gyro: { x: sequence / 10, y: 0, z: 0 }, accel: { x: 0, y: 0, z: 1 },
})!;
let nextTicket = 0;
const pending = new Map<number, () => void>();
const callbacks = new Map<number, () => void>();
const canceled: number[] = [];
const presented: { sequence: number; receivedAt: number }[] = [];
const gate = createGyroPresentationGate({
  present: (f, receivedAt) => presented.push({ sequence: f.sequence, receivedAt }),
  requestFrame: callback => { const t = nextTicket++; pending.set(t, callback); callbacks.set(t, callback); return t; },
  cancelFrame: t => { canceled.push(t); pending.delete(t); },
});
function flush() { const jobs = [...pending.values()]; pending.clear(); for (const job of jobs) job(); }
gate.push(frame(1), 1); gate.push(frame(2), 2);
check(pending.size === 0 && presented.length === 0, 'inactive gate stores only latest without RAF');
gate.setActive(true);
check(pending.size === 1, 'activation schedules one latest frame');
gate.push(frame(3), 3); gate.push(frame(4), 4);
check(pending.size === 1, 'visible burst has at most one pending RAF including ticket zero');
flush();
check(presented.length === 1 && presented[0].sequence === 4, 'visible burst presents latest once');
check(presented[0].receivedAt === 4, 'original receipt time preserved');
gate.push(frame(5), 5); const old = nextTicket - 1; gate.setVisible(false);
check(canceled.includes(old) && pending.size === 0, 'native hidden cancels scheduled RAF');
gate.push(frame(6), 6); gate.push(frame(7), 7); gate.setActive(false); gate.setVisible(true);
check(pending.size === 0, 'shown alone cannot resume a cached inactive page');
gate.setActive(true); const fresh = nextTicket - 1;
callbacks.get(old)!();
check(pending.has(fresh) && presented.length === 1, 'late canceled callback cannot clear new ticket');
flush();
check(presented.at(-1)?.sequence === 7 && presented.at(-1)?.receivedAt === 7, 'resume replays only newest frame with stale receipt still stale');
check(presented.length === 2, 'no backlog replay after cache/visibility transitions');
gate.push(frame(8), 8); const disposedTicket = nextTicket - 1; gate.dispose(); callbacks.get(disposedTicket)!();
check(pending.size === 0 && presented.length === 2, 'dispose cancels RAF and suppresses late callback');
gate.setActive(true); gate.setVisible(true); gate.push(frame(9), 9); gate.dispose(); flush();
check(presented.length === 2, 'disposed view cannot be rearmed');
// Stateful model test over visibility, activation, bursts and frame delivery.
let active = false, visible = true, latest: number | null = null, disposed = false;
let seq = 0, seed = 725510;
const jobs = new Map<number, () => void>(); let id = 0;
const results: number[] = [];
const modeled = createGyroPresentationGate({
  present: f => results.push(f.sequence),
  requestFrame: fn => { const t = ++id; jobs.set(t, fn); return t; },
  cancelFrame: t => { jobs.delete(t); },
});
for (let i = 0; i < 5000; i++) {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  const op = seed % 6;
  if (op < 2) { latest = ++seq; modeled.push(frame(seq), seq); }
  else if (op === 2) { active = !active; modeled.setActive(active); }
  else if (op === 3) { visible = !visible; modeled.setVisible(visible); }
  else {
    const before = results.length, expected = active && visible && latest !== null;
    for (const fn of [...jobs.values()]) fn(); jobs.clear();
    check(results.length === before + Number(expected), `model frame count ${i}`);
    if (expected) { check(results.at(-1) === latest, `model latest-only ${i}`); latest = null; }
  }
  check(jobs.size <= 1 && (active && visible || jobs.size === 0), `bounded queue ${i}`);
}
modeled.dispose(); disposed = true; jobs.clear(); modeled.push(frame(++seq), seq); modeled.setActive(true); modeled.setVisible(true);
check(disposed && jobs.size === 0, 'final model dispose is terminal');
console.log(`gyro presentation selftest PASS ${checks} checks`);