import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createFanLogFixture, LOG_EVIDENCE, main, runFanLogRegression, UNAVAILABLE_HANDSHAKE, VirtualClock } from './fan-log-regression.mjs';

let result;
const baseline = () => result ??= runFanLogRegression();

test('both real lifecycle cases reject the observed 200/unavailable receipt, never write, and explicitly retry', async () => {
  const value = await baseline();
  assert.equal(value.status, 'PASS', JSON.stringify(value.cases));
  assert.equal(value.exitCode, 0);
  assert.equal(value.cases.length, 2);
  for (const item of value.cases) {
    assert.equal(item.passed, true);
    assert.equal(item.primaryFailure.code, 'FAN_DEVICE_GATE_DENIED');
    assert.equal(item.primaryFailure.status, 409);
    assert.equal(item.observed.starts, 2);
    assert.equal(item.observed.handshakes, 2);
    assert.equal(item.observed.stops, 2);
    assert.equal(item.observed.curveWrites, 0);
    assert.equal(item.observed.pendingTimers, 0);
    assert.equal(item.forbiddenBoundaryCalls, 0);
    assert.equal(item.quietWindowMs, 60000);
  }
  assert.deepEqual(value.safety, { inMemoryIpc: true, virtualClock: true, localSettingsRead: false, realNetworkUsed: false,
    hostProcessStarted: false, hardwareAccessed: false, sharedBuildOutputWritten: false, operatingSystemSleepPerformed: false });
});

test('post-denial 12029 runs the production transport decoder and preserves control-error causal order', async () => {
  const item = (await baseline()).cases.find(row => row.id === 'fan.post-denial-winhttp-12029');
  assert.equal(item.secondaryFailure.code, 12029);
  assert.equal(item.secondaryFailure.durationMs, 2048);
  const denied = item.trace.findIndex(row => row.event === 'lifecycle.start-device-gate-denied');
  const failedRead = item.trace.findIndex(row => row.event === 'api.transport-failure');
  assert(denied >= 0 && failedRead > denied);
  assert.equal(item.trace[denied].initializationObservationRetained, true);
  assert.equal(item.trace[failedRead].endpoint, '/api/state');
  assert.equal(item.trace[failedRead].transportPhase, 'send');
  assert(!item.trace.slice(denied + 1, failedRead).some(row => row.state === 'starting'));
});

test('receipt fingerprints the actual unmodified production modules and distinguishes mock from CLR root cause', async () => {
  const value = await baseline();
  for (const source of ['src/bridge/fanHost.ts', 'src/bridge/fanApi.ts', 'src/bridge/fanDiagnostics.ts', 'tools/fixtures/fan_no_io_host.ts']) {
    assert(value.sourceInputs.some(row => row.path === source && /^[a-f0-9]{64}$/.test(row.sha256)));
  }
  assert.equal(value.productSourceExecuted, true);
  assert(value.limitations.some(text => text.includes('No CLR')));
  assert.equal(LOG_EVIDENCE.archiveSha256, '452b582fc60976646df48cec6237baea7e167f189b36c952cc1c0d9246d1a6e3');
  assert(!JSON.stringify(value).includes('sessionToken'));
});

test('a malformed 200 receipt remains FAIL instead of being accepted as the required product denial', async () => {
  const value = await runFanLogRegression({ handshake: { ...UNAVAILABLE_HANDSHAKE, supported: undefined } });
  assert.equal(value.status, 'FAIL');
  assert.equal(value.exitCode, 2);
  assert.equal(value.failedCount, 2);
  assert(value.cases.every(row => row.passed === false));
});

test('native settings, launcher and network fallbacks are blocked in memory', async () => {
  const f = createFanLogFixture();
  assert.throws(() => f.api.fs.readTextFile('unused'), /OFFLINE_BOUNDARY_FORBIDDEN/);
  assert.throws(() => f.api.shell.hidden('unused'), /OFFLINE_BOUNDARY_FORBIDDEN/);
  await assert.rejects(f.api.http.request('http://other.invalid/api/state', { method: 'GET' }), /OFFLINE_BOUNDARY_FORBIDDEN/);
  assert.equal(f.violations.length, 3);
  assert.equal(f.stats().starts, 0);
});

test('virtual clock does not change process globals or wait in real time', async () => {
  const originalTimer = globalThis.setTimeout, originalDate = Date;
  const clock = new VirtualClock();
  let hits = 0;
  const interval = clock.setInterval(() => hits++, 10);
  await clock.advance(30);
  clock.clearInterval(interval);
  await clock.advance(60000);
  assert.equal(hits, 3);
  assert.equal(clock.tasks.size, 0);
  assert.equal(globalThis.setTimeout, originalTimer);
  assert.equal(Date, originalDate);
});

test('node entry writes only an explicit caller output and supplies runner-readable evidence', async t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ymcc-fan-log-regression-'));
  const output = path.join(dir, 'fan-log-result.json');
  t.after(() => { if (fs.existsSync(output)) fs.unlinkSync(output); fs.rmdirSync(dir); });
  const value = await main(['--output', output]);
  const written = JSON.parse(fs.readFileSync(output, 'utf8'));
  assert.equal(written.status, 'PASS', JSON.stringify(written.cases));
  assert.equal(written.exitCode, value.exitCode);
  assert(written.cases.every(row => row.passed === true));
  assert.deepEqual(fs.readdirSync(dir), ['fan-log-result.json']);
  await assert.rejects(main(['--output']), /EXPECTED_OPTIONAL_EXPLICIT_OUTPUT/);
});
