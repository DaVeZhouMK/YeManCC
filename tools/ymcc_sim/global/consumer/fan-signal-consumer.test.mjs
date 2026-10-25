import assert from 'node:assert/strict';
import test from 'node:test';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { replayFanSignals, runFanSignalRegressions } from './fan-signal-consumer.mjs';
import { createFanSignalFixture, SIMULATED_HANDSHAKE } from './fan-signal-fixture.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const fixture = fields => ({ domain: 'fan', provenance: 'source-derived-fixture', ...fields });
const handshake = fixture({ id: 'handshake', event: 'startup.handshake', ...SIMULATED_HANDSHAKE });
const curve = [0, 40, 70, 100].map(tempC => ({ tempC, dutyPercent: 100 }));
const intent = (id, action, extra = {}) => fixture({ id, event: 'fan.intent', action, ...extra });

let suite;
const regression = () => suite ??= runFanSignalRegressions({ repoRoot });

test('all sixteen input-driven scenarios exercise production consumers and their negative expectations', async () => {
  const result = await regression();
  assert.equal(result.status, 'PASS', JSON.stringify(result.cases.filter(item => item.status !== 'PASS')));
  assert.equal(result.summary.total, 16);
  assert.equal(result.summary.failed, 0);
  assert(result.executedProductModules.some(item => item.path === 'src/bridge/fanHost.ts'));
  assert(result.executedProductModules.some(item => item.path === 'src/bridge/fanApi.ts'));
  assert.equal(result.summary.physicalReceipts, 0);
});

test('input curve values reach the real serialized HTTP mutation rather than selecting a fixed regression', async () => {
  const changed = [{ tempC: 0, dutyPercent: 20 }, { tempC: 35, dutyPercent: 40 }, { tempC: 75, dutyPercent: 80 }, { tempC: 100, dutyPercent: 100 }];
  const result = await replayFanSignals({ repoRoot, events: [handshake, intent('apply', 'apply', { nodes: changed }), intent('close', 'close')] });
  assert.equal(result.status, 'PASS');
  const request = result.evidence[1].consumer.requests.find(row => row.endpoint === '/api/enable');
  assert.deepEqual(request.nodes, changed);
  assert.notDeepEqual(request.nodes, curve);
  assert.deepEqual(result.evidence[1].state.transitions.map(row => row.state), ['ready']);
  assert.equal(result.evidence[1].feedback.hardwareReceipt, false);
});

test('raw HTTP capability corruption is rejected by the production adapter without a normalizer rescuing it', async () => {
  for (const delta of [{ hardwareWritesEnabled: 'false' }, { hardwareCapable: false }, { fanRouteWriteReady: 'true' },
    { hostMode: 'real-hc-unavailable' }, { mockZeroHardwareEvidence: true, hardwareWritesEnabled: true }]) {
    const f = createFanSignalFixture({ repoRoot, handshake: { ...SIMULATED_HANDSHAKE, ...delta } });
    try {
      const outcome = await f.clock.outcome(f.life.apply(curve));
      assert.equal(outcome.error?.errorCode, 'FAN_API_RESPONSE_INVALID');
      assert.equal(f.snapshot().curveWrites, 0);
      assert.equal(f.snapshot().starts, 1, 'invalid receipt cannot respawn automatically');
      assert.equal(f.snapshot().stops, 1);
      assert.equal(f.snapshot().pendingTimers, 0);
      assert(!f.requests.some(row => ['/api/open', '/api/enable'].includes(row.endpoint)));
      assert.equal(f.sourcesUnchanged(), true);
    } finally { f.dispose(); }
  }
});

test('valid zero-write and optional legacy capability fields retain their production contract', async () => {
  for (const value of [
    { ...SIMULATED_HANDSHAKE, mockZeroHardwareEvidence: true, hardwareWritesEnabled: false },
    { ok: true, supported: true, fanRoute: 'GenericDuty', fanRouteWriteReady: true },
  ]) {
    const f = createFanSignalFixture({ repoRoot, handshake: value });
    try {
      const outcome = await f.clock.outcome(f.life.apply(curve));
      assert.equal(outcome.error, undefined);
      assert.equal(f.snapshot().curveWrites, 1);
      assert.equal((await f.clock.outcome(f.life.close())).error, undefined);
      assert.equal(f.violations.length, 0);
    } finally { f.dispose(); }
  }
});

test('protocol drift blocks the real lifecycle before Open without an automatic retry', async () => {
  const f = createFanSignalFixture({ repoRoot, handshake: { ...SIMULATED_HANDSHAKE, protocolVersion: 73 } });
  try {
    const outcome = await f.clock.outcome(f.life.apply(curve));
    assert.equal(outcome.error?.errorCode, 'FAN_PROTOCOL_INCOMPATIBLE');
    assert.equal(f.snapshot().starts, 1);
    assert.equal(f.snapshot().stops, 1);
    assert.equal(f.snapshot().curveWrites, 0);
    assert.equal(f.snapshot().pendingTimers, 0);
  } finally { f.dispose(); }
});

test('candidate protocol and HC identity are caller parameters; this harness does not claim runtime assembly compatibility', async () => {
  const result = await replayFanSignals({ repoRoot,
    runtimeContract: { protocolVersion: 73, hcVersion: 'future-candidate', runtimeRoot: 'memory:/candidate/hc', sharedRuntimeRoot: 'memory:/candidate/shared', hostPayloadRoot: 'memory:/candidate/host' },
    events: [{ ...handshake, protocolVersion: 73 }, intent('apply', 'apply', { nodes: curve }), intent('close', 'close')] });
  assert.equal(result.status, 'PASS');
  assert.equal(result.compatibility.protocolExpected, 73);
  assert.equal(result.compatibility.hcVersion, 'future-candidate');
  assert.equal(result.compatibility.runtimeInputs.runtimeRoot, 'memory:/candidate/hc');
  assert.equal(result.compatibility.runtimeInputs.sharedRuntimeRoot, 'memory:/candidate/shared');
  assert.equal(result.compatibility.status, 'COMPATIBLE_PROTOCOL_ONLY');
  assert.equal(result.compatibility.runtimeAssemblyGate, 'not-executed-by-signal-consumer');
});

test('each disconnection names its actual stage', async () => {
  const cases = (await regression()).cases;
  for (const [id, code, stage] of [
    ['missing-entry', 'INPUT_ENTRY_MISSING', 'input'],
    ['missing-consumer', 'CONSUMER_NOT_CONNECTED', 'consumer'],
    ['missing-feedback', 'FEEDBACK_MISSING_OR_INVALID', 'feedback'],
    ['illegal-capability-field', 'INPUT_CAPABILITY_TYPE_INVALID', 'input'],
    ['stale-source-generation', 'SOURCE_GENERATION_STALE', 'input'],
  ]) assert(cases.find(item => item.id === id).replay.diagnostics.some(item => item.code === code && item.stage === stage));
});

test('observed reply fields stay literal, with synthetic HTTP envelope and entry disclosed', async () => {
  const result = await replayFanSignals({ repoRoot, sourceArchive: { sha256: 'a'.repeat(64), stable: true, arbitrarySecret: 'do-not-copy' }, events: [
    { id: 'member-0001:3', memberId: 'member-0001', line: 3, event: 'startup.handshake', domain: 'fan',
      hostMode: 'real-hc-unavailable', hardwareCapable: false, hardwareWritesEnabled: false, hardwareWritesObserved: false,
      reason: 'do-not-copy', arbitrarySecret: 'do-not-copy' },
  ] });
  assert.equal(result.status, 'INCOMPLETE');
  const row = result.evidence[0];
  assert.equal(row.input.provenance, 'archive-literal');
  assert.equal(row.input.memberId, 'member-0001');
  assert.equal(row.input.fields.hardwareCapable, false);
  assert.deepEqual(row.input.supplementedFields, ['ok', 'supported']);
  assert.equal(row.consumer.entryProvenance, 'source-derived-fixture');
  assert.equal(row.feedback.code, 'FAN_DEVICE_GATE_DENIED');
  assert.equal(row.feedback.httpStatus, 409);
  assert(!JSON.stringify(result).includes('do-not-copy'));
});

test('sleep cancels the old request with exact event ownership and no curve replay', async () => {
  const item = (await regression()).cases.find(row => row.id === 'sleep-retires-in-flight-old-curve').replay;
  const stale = item.evidence.find(row => row.input.eventId === 'inflight');
  assert.equal(stale.feedback.code, 'FAN_CONTROL_INTENT_SUPERSEDED');
  assert(stale.consumer.requests.every(row => row.ownerEventId === 'inflight'));
  assert(!stale.consumer.requests.some(row => ['/api/close', '/api/cancel-control-intent'].includes(row.endpoint)));
  assert.equal(item.summary.powerGeneration, 2);
  assert.equal(item.summary.curveWrites, 0);
  assert.equal(item.summary.lifecycle, 'stopped');
});

test('unsupported fan diagnostics are reported as missing consumers, never turned into control', async () => {
  const result = await replayFanSignals({ repoRoot, events: [{ id: 'diagnostic', domain: 'fan', event: 'fan-activity-boot', actionDispatchAccepted: true }] });
  assert.equal(result.status, 'INCOMPLETE');
  assert.equal(result.summary.starts, 0);
  assert.equal(result.evidence[0].feedback.code, 'NO_SIGNAL_CONSUMER');
  assert.equal(result.evidence[0].feedback.stage, 'consumer');
});

test('eleven historical days use VM time without replacing process timers', async () => {
  const beforeTimer = globalThis.setTimeout, beforeDate = globalThis.Date;
  const value = (await regression()).cases.find(row => row.id === 'idle-eleven-days-no-restart-or-poll-storm').replay;
  assert.equal(value.evidence[3].input.fields.advanceMs, 11 * 86400000);
  assert.equal(value.evidence[3].consumer.requests.length, 0);
  assert.equal(globalThis.setTimeout, beforeTimer);
  assert.equal(globalThis.Date, beforeDate);
});

test('fault injection and malformed input cannot escape the offline boundary', async () => {
  assert.equal((await replayFanSignals({ repoRoot, events: [handshake], simulation: { disconnectConsumer: true } })).status, 'FAIL');
  assert.equal((await replayFanSignals({ repoRoot, events: 'not-an-array' })).status, 'FAIL');
  const f = createFanSignalFixture({ repoRoot, handshake: SIMULATED_HANDSHAKE });
  try {
    await assert.rejects(() => f.api.http.request('http://127.0.0.1:8765/api/enable', { method: 'POST', body: '{}' }), /OFFLINE_BOUNDARY_FORBIDDEN/);
    assert.deepEqual(f.violations, ['non-memory-http']);
  } finally { f.dispose(); }
});


test('unextracted observed handshake fields remain a source gap and do not invent a bad production receipt', async () => {
  const result = await replayFanSignals({ repoRoot, events: [{ id: 'member-0036:10', memberId: 'member-0036', event: 'startup.handshake', domain: 'fan' }] });
  assert.equal(result.status, 'INCOMPLETE');
  assert.equal(result.summary.starts, 0);
  assert.equal(result.evidence[0].feedback.code, 'SOURCE_FIELDS_NOT_EXTRACTED');
  assert.equal(result.evidence[0].feedback.actualProductRejection, false);
  assert.equal(result.evidence[0].feedback.classification, 'source-fields-missing');
  assert(!result.evidence[0].consumer.requests.length);
});

test('independent startup observations consume their own reply rather than sharing an old simulated Ready result', async () => {
  const result = await replayFanSignals({ repoRoot, events: [
    { ...handshake, provenance: 'archive-literal', memberId: 'member-0036' },
    { id: 'later-unavailable', memberId: 'member-0036', domain: 'fan', event: 'startup.handshake', hostMode: 'real-hc-unavailable',
      hardwareCapable: false, hardwareWritesEnabled: false, hardwareWritesObserved: false },
  ] });
  assert.equal(result.status, 'INCOMPLETE');
  assert.equal(result.evidence[1].feedback.code, 'FAN_DEVICE_GATE_DENIED');
  assert.equal(result.summary.starts, 2);
  assert.equal(result.summary.observationContexts, 2);
  assert.equal(result.summary.curveWrites, 0);
});

test('fault-locked source response keeps write debt and stays a product refusal rather than a protocol error', async () => {
  const value = { ok: false, supported: false, protocolVersion: 2, hostMode: 'real-hc-fault-locked',
    hardwareCapable: false, hardwareWritesEnabled: true, hardwareWritesObserved: true };
  const f = createFanSignalFixture({ repoRoot, handshake: value });
  try {
    const outcome = await f.clock.outcome(f.host.handshake());
    assert(outcome.error, 'ok=false is still a refusal');
    assert.notEqual(outcome.error.errorCode, 'FAN_API_RESPONSE_INVALID');
    const actual = f.diagnostics.find(row => row.event === 'api.response').details.response;
    assert.equal(actual.hardwareWritesEnabled, true);
    assert.equal(actual.hardwareWritesObserved, true);
    assert.equal(actual.hardwareCapable, false);
    assert.equal(actual.hostMode, 'real-hc-fault-locked');
  } finally { f.dispose(); }
  const result = await replayFanSignals({ repoRoot, events: [fixture({ id: 'fault', event: 'startup.handshake', ...value })] });
  assert.equal(result.evidence[0].feedback.sourceHardwareWriteDebt.clearedBySimulation, false);
  assert(!result.diagnostics.some(row => row.code === 'CAPABILITY_RECEIPT_CONTRADICTORY'));
});


for (const enabled of [true, false]) {
  test(`production HTTP fan control keeps working with diagnostic logging ${enabled ? 'enabled' : 'disabled'}`, async () => {
    const f = createFanSignalFixture({ repoRoot, handshake: SIMULATED_HANDSHAKE,
      simulation: { diagnosticLoggingEnabled: enabled } });
    try {
      const applied = await f.clock.outcome(f.life.apply(curve));
      assert.equal(applied.error, undefined, applied.error?.message);
      assert.equal(f.snapshot().curveWrites, 1);
      const responses = f.diagnostics.filter(row => row.event === 'api.response');
      if (enabled) {
        const mutation = responses.find(row => row.details.path === '/api/enable');
        assert(mutation, 'The real lazy diagnostics export must observe the real HTTP mutation');
        assert.equal(mutation.details.observationSchemaVersion, 2);
        assert.equal(mutation.details.receipt.ok, true);
      } else {
        assert.equal(responses.length, 0, 'Disabled real diagnostics must not emit HTTP projections');
      }
      assert.equal((await f.clock.outcome(f.life.close())).error, undefined);
      assert(f.sourcesUnchanged());
      assert(f.sourceInputs().some(row => row.path === 'src/bridge/fanDiagnostics.ts'));
      assert.equal(f.violations.length, 0);
    } finally { f.dispose(); }
  });
}
