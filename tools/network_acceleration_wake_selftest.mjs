import assert from 'node:assert/strict';
import fs from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';

const source = process.argv[2] ?? new URL('../src/bridge/networkAccelerationWake.ts', import.meta.url);
const script = stripTypeScriptTypes(fs.readFileSync(source, 'utf8'));
const { createNetworkAccelerationWakeRecovery: create } = await import(
  `data:text/javascript;base64,${Buffer.from(script).toString('base64')}`);

function fixture() {
  const timers = new Map(), requests = [];
  let id = 0;
  const scheduler = create((generation, suspendSerial) => new Promise((resolve, reject) => {
    requests.push({ generation, suspendSerial, resolve, reject });
  }), {
    setTimeout(fn, ms) { assert.equal(ms, 5000); timers.set(++id, fn); return id; },
    clearTimeout(id) { assert(timers.delete(id)); },
  });
  function fire() {
    assert.equal(timers.size, 1);
    const [id, fn] = timers.entries().next().value;
    timers.delete(id); fn();
  }
  const schedule = (generation, suspendSerial = 0) => scheduler.schedule(generation, suspendSerial);
  return { scheduler, schedule, requests, timers, fire };
}
const defer = (generation, suspendSerial = 0) => ({ generation, suspendSerial, ok: false, reason: 'power-deferred-before-launch', submitted: false });
const accepted = (generation, suspendSerial = 0) => ({ generation, suspendSerial, ok: true, reason: 'launch-request-accepted', submitted: true });

for (const readyFirst of [false, true]) {
  const { scheduler, schedule, requests, timers, fire } = fixture();
  schedule(10); fire();
  scheduler.cancel();
  if (readyFirst) schedule(10);
  assert.equal(timers.size, 0);
  requests[0].resolve(defer(10));
  await Promise.resolve();
  assert.equal(timers.size, readyFirst ? 1 : 0);
  if (!readyFirst) schedule(10);
  schedule(10); fire();
  assert.deepEqual(requests.map(r => r.generation), [10, 10]);
  requests[1].resolve(defer(10));
  await Promise.resolve();
  scheduler.cancel(); schedule(10);
  assert.equal(timers.size, 0);
}
for (const reply of [accepted(10), defer(9), { ...defer(10), submitted: true },
  { generation: 10, ok: false, reason: 'launch-request-failed', submitted: false }, undefined]) {
  const { scheduler, schedule, requests, timers, fire } = fixture();
  schedule(10); fire(); scheduler.cancel(); schedule(10);
  requests[0].resolve(reply); await Promise.resolve();
  assert.equal(timers.size, 0);
}
{
  const { scheduler, schedule, requests, timers, fire } = fixture();
  schedule(10); fire(); requests[0].reject(new Error('IPC reply lost')); await Promise.resolve();
  scheduler.cancel(); schedule(10);
  assert.equal(timers.size, 0);
}
{
  const { scheduler, schedule, requests, timers, fire } = fixture();
  schedule(10); const stale = timers.values().next().value;
  scheduler.cancel(); stale(); assert.deepEqual(requests, []);
  schedule(10); fire(); schedule(11); fire();
  requests[0].resolve(defer(10)); await Promise.resolve();
  assert.equal(timers.size, 0);
  schedule(10); scheduler.dispose();
  requests[1].resolve(defer(11)); await Promise.resolve();
  assert.equal(timers.size, 0);
  assert.deepEqual(requests.map(r => r.generation), [10, 11]);
}
{
  const { schedule, requests, timers, fire } = fixture();
  schedule(10, 4); fire(); requests[0].resolve(accepted(10, 4)); await Promise.resolve();
  schedule(10, 4); assert.equal(timers.size, 0);
  schedule(10, 5);
  assert.equal(timers.size, 1, 'coalesced Ready in the same generation gets a new check');
  schedule(10, 4); fire();
  assert.deepEqual(requests.map(r => [r.generation, r.suspendSerial]), [[10, 4], [10, 5]]);
  requests[1].resolve(defer(10, 4)); await Promise.resolve();
  assert.equal(timers.size, 0, 'mismatched serial is not proof for a continuation');
}
for (const rejects of [false, true]) {
  const { schedule, requests, timers, fire } = fixture();
  schedule(10, 4); fire();
  schedule(10, 5); fire();
  if (rejects) requests[0].reject(new Error('late lost receipt'));
  else requests[0].resolve(defer(10, 4));
  await Promise.resolve();
  schedule(10, 5);
  assert.equal(timers.size, 0, 'retired receipt cannot release the new in-flight check');
  requests[1].resolve(defer(10, 5)); await Promise.resolve();
  schedule(10, 4); fire();
  assert.deepEqual(requests.map(r => r.suspendSerial), [4, 5, 5]);
  requests[2].resolve(defer(10, 5)); await Promise.resolve();
  schedule(10, 5); assert.equal(timers.size, 0, 'two attempts are the limit for one scope');
}
{
  const { scheduler, schedule, requests, timers, fire } = fixture();
  scheduler.schedule(10, undefined); schedule(10, -1); schedule(10, 1.5);
  assert.equal(timers.size, 0);
  schedule(10, 0); fire();
  assert.equal(requests[0].suspendSerial, 0);
}
console.log('network acceleration wake scheduler: 13 cases passed');
