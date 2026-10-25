/** Consume privacy-reviewed scalar events through real FanHostLifecycle/HTTP TS.
 * Observed replies and source-derived requests are always separately labelled.
 * No native Host, network, settings, process, sleep or hardware port is opened.
 */
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { createFanSignalFixture, SIMULATED_HANDSHAKE } from './fan-signal-fixture.mjs';

const DEFAULT_REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const FIXTURE = 'source-derived-fixture';
const BOOLS = ['ok', 'supported', 'hardwareCapable', 'hardwareWritesEnabled', 'hardwareWritesObserved', 'fanRouteWriteReady', 'mockZeroHardwareEvidence'];
const INTENTS = new Set(['start', 'apply', 'disable', 'close', 'state']);
const PHASES = new Set(['suspending', 'suspended', 'resuming', 'resume-ready', 'resumed']);
const MAX_EVENTS = 20000, MAX_TIME = 366 * 86400000;
const CURVE = [0, 40, 70, 100].map(tempC => ({ tempC, dutyPercent: 100 }));
const SHA = /^[a-f0-9]{64}$/i;
const BOUNDARY = Object.freeze({ realOperatingSystemSleep: false, hardwareWrites: false,
  ecOemHidDriverAccess: false, externalNetwork: false, realHostProcess: false,
  server: 'inert-NoIoHost', client: 'unmodified-FanHostLifecycle-and-HttpFanApiAdapter',
  hardwareControlConfirmed: false, physicalRestoreConfirmed: false });
const hash = text => crypto.createHash('sha256').update(text).digest('hex');
function archiveSummary(value) {
  if (!value || typeof value !== 'object') return null;
  return { ...(SHA.test(value.sha256 ?? '') ? { sha256: value.sha256.toLowerCase() } : {}),
    ...(Number.isSafeInteger(value.bytes) && value.bytes >= 0 ? { bytes: value.bytes } : {}),
    ...(typeof value.stable === 'boolean' ? { stable: value.stable } : {}) };
}
function errorCode(error) {
  if (typeof error?.errorCode === 'string' && /^[A-Z][A-Z0-9_]{1,80}$/.test(error.errorCode)) return error.errorCode;
  const match = /\b(FAN_[A-Z0-9_]+|HC_[A-Z0-9_]+|POWER_[A-Z0-9_]+|VIRTUAL_[A-Z0-9_]+|PRODUCTION_[A-Z0-9_]+)\b/.exec(error?.message ?? '');
  if (match) return match[1];
  return /WinHTTP 12029, phase=send/.test(error?.message ?? '') ? 'WINHTTP_12029_SEND' : 'PRODUCT_CONSUMER_REJECTED';
}
function validCurve(nodes) {
  if (!Array.isArray(nodes) || nodes.length !== 4) return false;
  let temperature = 0, duty = 0;
  return nodes.every((node, i) => {
    if (!node || !Number.isFinite(node.tempC) || !Number.isFinite(node.dutyPercent)
      || node.tempC < temperature || node.tempC > 100 || node.dutyPercent < duty || node.dutyPercent > 100
      || (i === 0 && node.tempC !== 0)) return false;
    temperature = node.tempC; duty = node.dutyPercent; return true;
  }) && duty >= 50;
}
function handshakeOf(event) {
  const value = {}, supplementedFields = [];
  for (const key of BOOLS) if (event[key] !== undefined) value[key] = event[key];
  for (const key of ['hostMode', 'fanRoute', 'deviceClass']) if (typeof event[key] === 'string' && /^[A-Za-z0-9_.-]{1,120}$/.test(event[key])) value[key] = event[key];
  if (Number.isSafeInteger(event.protocolVersion)) value.protocolVersion = event.protocolVersion;
  if (event.hostMode === 'real-hc-unavailable') {
    // The log observation has no HTTP envelope. These two defaults recreate
    // the documented refusal only, and are exposed as synthetic fields.
    for (const [key, field] of [['ok', true], ['supported', false]]) if (value[key] === undefined) {
      value[key] = field; supplementedFields.push(key);
    }
  }
  return { value, supplementedFields };
}

/**
 * events: buildLogReplayPlan scalars, plus explicitly source-derived fan.intent,
 * fan.power and fan.clock fixtures. simulation is test-only fault injection and
 * requires provenance='source-derived-fixture'. Real logs cannot enable it.
 */
export async function replayFanSignals({ repoRoot = DEFAULT_REPO, events = [], sourceArchive = null, simulation = null, runtimeContract = {} } = {}) {
  const diagnostics = [], evidence = [], root = fs.realpathSync(repoRoot);
  const add = (code, stage, eventId, status = 'INCOMPLETE') => diagnostics.push({ code, stage, eventId, status });
  const result = { schemaVersion: 1, kind: 'fan-signal-consumption', status: 'PASS', scope: 'software-signal-consumption',
    sourceArchive: archiveSummary(sourceArchive), ordering: 'member-relative-time-only;cross-member-causality-not-established', evidence, diagnostics, sourceHashes: [], sourceChanges: [], executedProductModules: [], inputConsumerStateFeedback: evidence,
    compatibility: { status: 'NOT_EXECUTED' }, remainingGaps: [], boundary: { ...BOUNDARY }, summary: {} };
  if (!Array.isArray(events) || events.length > MAX_EVENTS) {
    add(Array.isArray(events) ? 'EVENT_BUDGET_EXCEEDED' : 'EVENT_ARRAY_REQUIRED', 'input', null, 'FAIL');
    result.status = 'FAIL'; return result;
  }
  if (simulation && simulation.provenance !== FIXTURE) {
    add('FIXTURE_PROVENANCE_REQUIRED', 'input', null, 'FAIL'); result.status = 'FAIL'; return result;
  }
  if (!runtimeContract || typeof runtimeContract !== 'object' || Array.isArray(runtimeContract)
      || (runtimeContract.protocolVersion !== undefined && (!Number.isSafeInteger(runtimeContract.protocolVersion) || runtimeContract.protocolVersion < 1))) {
    add('PROTOCOL_CONTRACT_INVALID', 'input', null, 'FAIL'); result.status = 'FAIL'; return result;
  }
  let f = createFanSignalFixture({ repoRoot: root, handshake: {}, simulation: simulation ?? {}, runtimeContract });
  const retiredContexts = [];
  result.sourceChanges = [
    { path: 'src/bridge/fanApi.ts', change: 'Validate optional handshake capability booleans and reject contradictory receipts.' },
    { path: 'src/bridge/fanHost.ts', change: 'Reject a declared protocol mismatch and keep invalid receipts terminal without automatic respawn.' },
  ];
  result.compatibility = { status: 'NOT_EXECUTED', protocolExpected: f.life.config.protocolVersion,
    protocolObserved: [], hcVersion: typeof runtimeContract.hcVersion === 'string' ? runtimeContract.hcVersion : null,
    runtimeIdentitySource: 'caller-parameter',
    runtimeInputs: Object.fromEntries(['runtimeRoot', 'sharedRuntimeRoot', 'hostPayloadRoot', 'payloadSha256'].filter(key => typeof runtimeContract[key] === 'string').map(key => [key, runtimeContract[key]])),
    runtimeAssemblyGate: 'not-executed-by-signal-consumer', differences: [] };
  result.remainingGaps = [
    { stage: 'input', code: 'PRE_TRIM_LAST_SUCCESSFUL_SENDER_NOT_ESTABLISHED' },
    { stage: 'consumer', code: 'FANVIEW_UI_NOT_EXECUTED_BY_THIS_LIFECYCLE_HARNESS' },
    { stage: 'state', code: 'REAL_HC_EC_HID_DRIVER_WRITES_NOT_EXECUTED' },
    { stage: 'feedback', code: 'PHYSICAL_OEM_RESTORE_READBACK_NOT_ESTABLISHED' },
  ];
  const seen = new Set(), pending = [], memberTimes = new Map();
  let configured = false, invalidCapabilities = false, primaryFailure = null, primaryFailureId = null;
  let observedCount = 0, fixtureCount = 0, ignoredCount = 0;
  function primary(code, eventId) {
    if (!primaryFailure) { primaryFailure = code; primaryFailureId = eventId; }
  }
  async function complete(row, promise, driveClock = true) {
    const operationFixture = f;
    let outcome;
    try { outcome = driveClock ? await operationFixture.clock.outcome(promise) : await promise.then(value => ({ value }), error => ({ error })); }
    catch (error) { outcome = { error }; }
    const calls = operationFixture.requests.filter(item => item.ownerEventId === row.input.eventId).map(item => ({ ...item }));
    row.consumer.requests = calls;
    row.state.after = operationFixture.snapshot();
    row.consumer.pending = false;
    if (outcome.error) {
      const code = errorCode(outcome.error);
      const secondary = primaryFailure !== null && code !== primaryFailure;
      row.feedback = { status: 'rejected', code, stage: /RESPONSE_INVALID|RESULT_UNKNOWN/.test(code) ? 'feedback' : 'consumer',
        primaryFailure: primaryFailure ?? code, secondary,
        ...(Number.isFinite(outcome.error.status) ? { httpStatus: outcome.error.status } : {}) };
      if (code !== 'FAN_API_RESPONSE_INVALID' && code !== 'FAN_CONTROL_INTENT_SUPERSEDED') primary(code, row.input.eventId);
      if (code === 'FAN_PROTOCOL_INCOMPATIBLE') add(code, 'consumer', row.input.eventId, 'FAIL');
      if (/RESPONSE_INVALID|RESULT_UNKNOWN/.test(code)) add('FEEDBACK_MISSING_OR_INVALID', 'feedback', row.input.eventId);
    } else {
      row.feedback = { status: 'accepted', stage: 'feedback', softwareReceipt: true, hardwareReceipt: false,
        primaryFailure, ...(outcome.value && typeof outcome.value.allowed === 'boolean'
          ? { allowed: outcome.value.allowed, writeReady: outcome.value.writeReady } : {}),
        receipts: operationFixture.receipts.filter(receipt => receipt.ownerEventId === row.input.eventId).map(item => ({ ...item })) };
    }
    row.feedback.provenance = 'product-consumer-with-inert-server';
    row.feedback.classification = outcome.error ? 'actual-product-rejection-of-reconstructed-input' : 'software-receipt';
    row.feedback.actualProductRejection = Boolean(outcome.error);
    if (row.state.sourceHardwareWriteDebt) row.feedback.sourceHardwareWriteDebt = { ...row.state.sourceHardwareWriteDebt };
    row.state.transitions = operationFixture.diagnostics.filter(item => item.event === 'lifecycle.state' && item.ownerEventId === row.input.eventId)
      .map(item => ({ atMs: item.atMs, state: item.details.state }));
  }
  try {
    for (let index = 0; index < events.length; index++) {
      const raw = events[index];
      if (!raw || typeof raw !== 'object' || Array.isArray(raw)) { add('EVENT_OBJECT_REQUIRED', 'input', `event-${index + 1}`, 'FAIL'); continue; }
      if (raw.domain !== undefined && raw.domain !== 'fan') { ignoredCount++; continue; }
      const eventId = typeof raw.id === 'string' && /^[A-Za-z0-9_.:-]{1,120}$/.test(raw.id) ? raw.id : `event-${index + 1}`;
      const provenance = raw.provenance === FIXTURE ? FIXTURE : 'archive-literal';
      provenance === FIXTURE ? fixtureCount++ : observedCount++;
      const event = typeof raw.event === 'string' && /^[A-Za-z0-9_.:-]{1,100}$/.test(raw.event) ? raw.event : null;
      const row = { input: { eventId, event, provenance,
        ...(Number.isSafeInteger(raw.line) ? { line: raw.line } : {}),
        ...(Number.isFinite(raw.atMs) ? { sourceAtMs: raw.atMs, timingBasis: 'member-relative' } : {}),
        ...(typeof raw.memberId === 'string' && /^member-\d{1,8}$/.test(raw.memberId) ? { memberId: raw.memberId } : {}),
        fields: {} }, consumer: { status: 'not-dispatched', startedAtMs: f.clock.now, requests: [] },
        state: { before: f.snapshot(), after: null, transitions: [] }, feedback: { status: 'missing', stage: 'input' } };
      evidence.push(row);
      try {
        if (seen.has(eventId)) {
          row.consumer.status = 'duplicate-suppressed'; row.state.after = f.snapshot();
          row.feedback = { status: 'duplicate', stage: 'input', hardwareReceipt: false }; continue;
        }
        seen.add(eventId);
        if (raw.atMs !== undefined && raw.atMs !== null && (!Number.isFinite(raw.atMs) || raw.atMs < 0 || raw.atMs > MAX_TIME)) {
          add('INPUT_TIMESTAMP_INVALID', 'input', eventId, 'FAIL'); row.feedback.code = 'INPUT_TIMESTAMP_INVALID'; continue;
        }
        if (Number.isFinite(raw.atMs) && ['startup.handshake', 'api.transport-failure', 'fan.intent', 'fan.power'].includes(event)) {
          const member = raw.memberId ?? 'single-member';
          const priorTime = memberTimes.get(member);
          if (priorTime !== undefined && raw.atMs >= priorTime) await f.clock.advance(raw.atMs - priorTime);
          memberTimes.set(member, raw.atMs);
        }
        row.consumer.startedAtMs = f.clock.now;
        if (raw.generation !== undefined && (!Number.isSafeInteger(raw.generation) || raw.generation < 0)) {
          add('INPUT_GENERATION_INVALID', 'input', eventId, 'FAIL'); row.feedback.code = 'INPUT_GENERATION_INVALID'; continue;
        }
        if (Number.isSafeInteger(raw.generation) && raw.generation < f.snapshot().powerGeneration) {
          add('SOURCE_GENERATION_STALE', 'input', eventId); row.feedback = { status: 'rejected', stage: 'input', code: 'SOURCE_GENERATION_STALE' };
          row.state.after = f.snapshot(); continue;
        }
        if (simulation?.disconnectConsumer) {
          add('CONSUMER_NOT_CONNECTED', 'consumer', eventId); row.feedback = { status: 'missing', stage: 'consumer', code: 'CONSUMER_NOT_CONNECTED' }; continue;
        }
        let promise;
        if (event === 'startup.handshake') {
          const invalid = BOOLS.some(key => raw[key] !== undefined && typeof raw[key] !== 'boolean');
          if (invalid) { add('INPUT_CAPABILITY_TYPE_INVALID', 'input', eventId, 'FAIL'); row.feedback.code = 'INPUT_CAPABILITY_TYPE_INVALID'; continue; }
          const { value, supplementedFields } = handshakeOf(raw);
          row.input.fields = structuredClone(value);
          if (value.hostMode === 'real-hc-fault-locked' && (value.hardwareWritesEnabled === true || value.hardwareWritesObserved === true))
            row.state.sourceHardwareWriteDebt = { hardwareWritesEnabled: value.hardwareWritesEnabled, hardwareWritesObserved: value.hardwareWritesObserved,
              physicalRestoreConfirmed: false, clearedBySimulation: false };
          if (value.protocolVersion !== undefined) {
            result.compatibility.protocolObserved.push(value.protocolVersion);
            if (value.protocolVersion !== result.compatibility.protocolExpected) result.compatibility.differences.push({ code: 'FAN_PROTOCOL_INCOMPATIBLE', expected: result.compatibility.protocolExpected, observed: value.protocolVersion });
          }
          row.input.supplementedFields = supplementedFields;
          if (simulation?.handshakeOverride) row.input.fixtureFaultFields = Object.fromEntries(Object.entries(simulation.handshakeOverride).filter(([key]) => [...BOOLS, 'protocolVersion', 'hostMode'].includes(key)));
          row.consumer.entryProvenance = FIXTURE;
          row.consumer.entry = 'FanHostLifecycle.start';
          if (provenance !== FIXTURE) add('INPUT_ENTRY_MISSING', 'input', eventId);
          if (supplementedFields.length) add('HTTP_ENVELOPE_FIELDS_SOURCE_DERIVED', 'input', eventId);
          const contradictory = (value.hostMode === 'real-hc-unavailable' && value.supported === true)
            || (value.hardwareCapable === false && (value.supported === true || value.fanRouteWriteReady === true))
            || (value.supported === false && value.fanRouteWriteReady === true)
            || (value.mockZeroHardwareEvidence === true && (value.hardwareWritesEnabled === true || value.hardwareWritesObserved === true));
          invalidCapabilities = contradictory;
          configured = true;
          if (contradictory) {
            add('CAPABILITY_RECEIPT_CONTRADICTORY', 'feedback', eventId);
            row.consumer.status = 'blocked'; row.feedback = { status: 'unknown', stage: 'feedback', code: 'CAPABILITY_RECEIPT_CONTRADICTORY' };
            row.state.after = f.snapshot(); continue;
          }
          const observationFieldsUnextracted = provenance === 'archive-literal' && (value.ok === undefined || value.supported === undefined
            || (value.supported === true && !value.fanRoute && !value.deviceClass));
          if (observationFieldsUnextracted) {
            configured = false;
            row.input.sourceFieldsStatus = 'not-extracted-or-not-available;not-a-claim-about-source-record';
            add('SOURCE_FIELDS_NOT_EXTRACTED', 'input', eventId);
            row.feedback = { status: 'unknown', stage: 'input', code: 'SOURCE_FIELDS_NOT_EXTRACTED', classification: 'source-fields-missing', actualProductRejection: false };
            continue;
          }
          if (value.ok === undefined || value.supported === undefined) add('CAPABILITY_RECEIPT_MISSING', 'feedback', eventId);
          if (provenance === 'archive-literal' && f.snapshot().starts > 0) {
            // Separate startup receipts have no retained process/session identity.
            // Recreate their consumers independently rather than treating a prior
            // simulated Ready state as evidence about this later startup.
            if (!f.sourcesUnchanged()) add('PRODUCT_SOURCE_CHANGED_DURING_REPLAY', 'consumer', eventId, 'FAIL');
            retiredContexts.push({ ...f.snapshot(), requestCount: f.requests.length, receiptCount: f.receipts.length, syntheticRetirement: 'sandbox-disposal;not-oem-release-proof' });
            f.dispose();
            f = createFanSignalFixture({ repoRoot: root, handshake: {}, simulation: simulation ?? {}, runtimeContract });
            row.state.before = f.snapshot();
            row.consumer.startedAtMs = f.clock.now;
          }
          if (provenance === 'archive-literal') row.consumer.observationContext = 'isolated-startup-receipt;source-session-unproven';
          f.setHandshake(value);
          row.consumer.status = 'dispatched';
          promise = f.runAs(eventId, () => f.life.start());
        } else if (event === 'fan.intent') {
          if (provenance !== FIXTURE || !INTENTS.has(raw.action)) {
            add(provenance !== FIXTURE ? 'INPUT_ENTRY_UNPROVEN' : 'INPUT_ACTION_INVALID', 'input', eventId, 'FAIL');
            row.feedback.code = 'INPUT_ENTRY_UNPROVEN'; continue;
          }
          row.input.fields = { action: raw.action, ...(raw.generation !== undefined ? { generation: raw.generation } : {}) };
          if (!configured && ['start', 'apply'].includes(raw.action)) {
            add('INPUT_ENTRY_MISSING', 'input', eventId); row.feedback.code = 'INPUT_ENTRY_MISSING'; continue;
          }
          if (invalidCapabilities && ['start', 'apply'].includes(raw.action)) {
            row.feedback = { status: 'unknown', stage: 'feedback', code: 'CAPABILITY_RECEIPT_CONTRADICTORY' }; continue;
          }
          if (raw.action === 'apply' && !validCurve(raw.nodes)) {
            add('INPUT_CURVE_INVALID', 'input', eventId, 'FAIL'); row.feedback.code = 'INPUT_CURVE_INVALID'; continue;
          }
          if (raw.action === 'apply') row.input.fields.nodes = raw.nodes.map(({ tempC, dutyPercent }) => ({ tempC, dutyPercent }));
          row.consumer.entry = `FanHostLifecycle.${raw.action === 'state' ? 'getState' : raw.action}`;
          row.consumer.status = 'dispatched';
          promise = f.runAs(eventId, () => raw.action === 'apply' ? f.life.apply(row.input.fields.nodes)
            : f.life[raw.action === 'state' ? 'getState' : raw.action]());
        } else if (event === 'fan.power') {
          if (provenance !== FIXTURE || !PHASES.has(raw.phase) || !Number.isSafeInteger(raw.generation) || raw.generation < 1) {
            add('INPUT_POWER_BOUNDARY_INVALID', 'input', eventId, 'FAIL'); continue;
          }
          row.input.fields = { phase: raw.phase, generation: raw.generation };
          row.consumer.entry = 'FanHostLifecycle.observePowerBoundary'; row.consumer.status = 'dispatched';
          f.setPower(raw.phase, raw.generation);
          promise = f.runAs(eventId, () => raw.phase === 'suspending' || raw.phase === 'suspended' ? f.life.suspend() : Promise.resolve());
        } else if (event === 'fan.clock') {
          if (provenance !== FIXTURE || !Number.isFinite(raw.advanceMs) || raw.advanceMs < 0 || raw.advanceMs > MAX_TIME) {
            add('INPUT_CLOCK_INVALID', 'input', eventId, 'FAIL'); continue;
          }
          row.input.fields = { advanceMs: raw.advanceMs }; row.consumer.entry = 'VirtualClock.advance'; row.consumer.status = 'dispatched';
          promise = f.runAs(eventId, () => f.clock.advance(raw.advanceMs));
        } else if (event === 'api.transport-failure' && raw.winHttpCode === 12029 && (!raw.path || raw.path === '/api/state')) {
          row.input.fields = { winHttpCode: 12029, transportPhase: 'send' };
          row.consumer.entry = 'FanHostLifecycle.getState -> HttpFanApiAdapter.getState'; row.consumer.status = 'dispatched';
          // The log contains a failed observation, not a new control intent.
          f.setStateTransportFailure(true); promise = f.runAs(eventId, () => f.life.getState());
        } else {
          add(event ? 'NO_SIGNAL_CONSUMER' : 'INPUT_EVENT_MISSING', event ? 'consumer' : 'input', eventId);
          row.feedback = { status: 'missing', stage: event ? 'consumer' : 'input', code: event ? 'NO_SIGNAL_CONSUMER' : 'INPUT_EVENT_MISSING' }; continue;
        }
        // Register a rejection handler immediately for deliberately deferred calls.
        promise = Promise.resolve(promise); promise.catch(() => {});
        if (raw.settle === false && provenance === FIXTURE) {
          row.consumer.pending = true; pending.push(complete(row, promise, false)); await f.clock.drain();
        } else {
          await complete(row, promise);
          if (event === 'api.transport-failure') f.setStateTransportFailure(false);
        }
      } finally { if (!row.consumer.pending && !row.state.after) row.state.after = f.snapshot(); }
    }
    for (const completion of pending) await f.clock.outcome(completion);
    if (!evidence.length) add('INPUT_ENTRY_MISSING', 'input', null);
    result.summary = { eventCount: events.length, consumedCount: evidence.filter(row => row.consumer.status === 'dispatched').length,
      observedCount, fixtureCount, ignoredCount, duplicateCount: evidence.filter(row => row.consumer.status === 'duplicate-suppressed').length,
      primaryFailure, primaryFailureId, ...f.snapshot(),
      simulatedReceipts: f.receipts.length + retiredContexts.reduce((sum, row) => sum + row.receiptCount, 0), physicalReceipts: 0, observationContexts: retiredContexts.length + 1,
      retiredContexts, starts: f.snapshot().starts + retiredContexts.reduce((sum, row) => sum + row.starts, 0),
      stops: f.snapshot().stops + retiredContexts.reduce((sum, row) => sum + row.stops, 0),
      handshakes: f.snapshot().handshakes + retiredContexts.reduce((sum, row) => sum + row.handshakes, 0),
      curveWrites: f.snapshot().curveWrites + retiredContexts.reduce((sum, row) => sum + row.curveWrites, 0),
      requestCount: f.requests.length + retiredContexts.reduce((sum, row) => sum + row.requestCount, 0) };
    if (!f.sourcesUnchanged()) add('PRODUCT_SOURCE_CHANGED_DURING_REPLAY', 'consumer', null, 'FAIL');
    if (f.violations.length) add('OFFLINE_BOUNDARY_VIOLATION', 'consumer', null, 'FAIL');
    result.sourceHashes = f.sourceInputs();
  } catch (error) {
    add(errorCode(error), /VIRTUAL_/.test(errorCode(error)) ? 'state' : 'consumer', null, 'FAIL');
    result.summary = { primaryFailure, primaryFailureId, ...f.snapshot(), requestCount: f.requests.length };
  } finally {
    for (const row of evidence) if (!row.state.after) row.state.after = f.snapshot();
    result.sourceHashes = f.sourceInputs();
    result.executedProductModules = result.sourceHashes.filter(item => item.path.startsWith('src/') && item.path.endsWith('.ts')).map(item => ({ ...item, execution: 'unmodified-typescript-in-offline-vm' }));
    result.inputConsumerStateFeedback = evidence;
    result.compatibility.status = result.compatibility.differences.length ? 'INCOMPATIBLE'
      : result.compatibility.protocolObserved.length ? 'COMPATIBLE_PROTOCOL_ONLY' : 'UNKNOWN_PROTOCOL';
    f.dispose();
  }
  for (const ownPath of ['tools/ymcc_sim/global/consumer/fan-signal-consumer.mjs', 'tools/ymcc_sim/global/consumer/fan-signal-fixture.mjs'])
    result.sourceHashes.push({ path: ownPath, sha256: hash(fs.readFileSync(path.join(root, ownPath), 'utf8')) });
  result.status = diagnostics.some(row => row.status === 'FAIL') ? 'FAIL' : diagnostics.length ? 'INCOMPLETE' : 'PASS';
  return result;
}

export async function runFanSignalRegressions({ repoRoot = DEFAULT_REPO } = {}) {
  const cases = [], sourceHashes = new Map();
  const event = (id, fields) => ({ id, domain: 'fan', provenance: FIXTURE, ...fields });
  const good = event('handshake', { event: 'startup.handshake', ...SIMULATED_HANDSHAKE });
  const intent = (id, action, extra = {}) => event(id, { event: 'fan.intent', action, ...extra });
  async function check(id, events, verify, simulation = null) {
    const replay = await replayFanSignals({ repoRoot, events, simulation });
    for (const source of replay.sourceHashes) sourceHashes.set(source.path, source);
    try { verify(replay); cases.push({ id, status: 'PASS', provenance: FIXTURE, replay }); }
    catch (error) { cases.push({ id, status: 'FAIL', provenance: FIXTURE,
      assertion: typeof error.code === 'string' ? error.code : 'REGRESSION_ASSERTION_FAILED', replay }); }
  }
  const unavailable = { id: 'observed-unavailable', event: 'startup.handshake', domain: 'fan', hostMode: 'real-hc-unavailable',
    hardwareCapable: false, hardwareWritesEnabled: false, hardwareWritesObserved: false };
  await check('observed-unavailable-refusal-and-secondary-12029', [unavailable,
    { id: 'secondary-transport', event: 'api.transport-failure', domain: 'fan', winHttpCode: 12029, path: '/api/state' },
    event('eleven-days', { event: 'fan.clock', advanceMs: 11 * 86400000 })], r => {
    assert.equal(r.status, 'INCOMPLETE'); assert.equal(r.summary.primaryFailure, 'FAN_DEVICE_GATE_DENIED');
    assert.equal(r.evidence[0].feedback.httpStatus, 409); assert.equal(r.summary.starts, 1);
    assert.equal(r.summary.curveWrites, 0); assert.equal(r.summary.pendingTimers, 0);
    assert(r.evidence[1].feedback.secondary); assert.equal(r.evidence[1].feedback.code, 'WINHTTP_12029_SEND');
  });
  await check('open-curve-off-close-restore', [good, intent('apply', 'apply', { nodes: CURVE }), intent('off', 'disable'), intent('close', 'close')], r => {
    assert.equal(r.status, 'PASS'); assert.equal(r.evidence[1].state.after.lifecycle, 'ready');
    assert.equal(r.evidence[2].state.after.leaseActive, false); assert.equal(r.summary.lifecycle, 'stopped');
    const routes = r.evidence.flatMap(row => row.consumer.requests.map(request => request.endpoint));
    for (const route of ['/api/open', '/api/open-events', '/api/enable', '/api/restore', '/api/release-control', '/api/close']) assert(routes.includes(route));
    assert.equal(r.summary.physicalReceipts, 0);
  });
  await check('duplicate-signal-no-second-start-or-control', [good, good, intent('apply', 'apply', { nodes: CURVE }), intent('apply', 'apply', { nodes: CURVE }), intent('close', 'close')], r => {
    assert.equal(r.status, 'PASS'); assert.equal(r.summary.starts, 1); assert.equal(r.summary.curveWrites, 1); assert.equal(r.summary.duplicateCount, 2);
  });
  await check('missing-entry', [intent('apply', 'apply', { nodes: CURVE })], r => {
    assert(r.diagnostics.some(row => row.code === 'INPUT_ENTRY_MISSING' && row.stage === 'input')); assert.equal(r.summary.starts, 0);
  });
  await check('missing-consumer', [good], r => {
    assert(r.diagnostics.some(row => row.code === 'CONSUMER_NOT_CONNECTED' && row.stage === 'consumer')); assert.equal(r.summary.starts, 0);
  }, { provenance: FIXTURE, disconnectConsumer: true });
  await check('missing-feedback', [good, intent('apply', 'apply', { nodes: CURVE }), intent('close', 'close')], r => {
    assert(r.diagnostics.some(row => row.code === 'FEEDBACK_MISSING_OR_INVALID' && row.stage === 'feedback'));
    assert.notEqual(r.evidence[1].feedback.status, 'accepted'); assert.equal(r.summary.curveWrites, 1);
  }, { provenance: FIXTURE, missingFeedback: ['/api/enable'] });
  await check('illegal-capability-field', [{ ...good, hardwareCapable: 'false' }], r => {
    assert.equal(r.status, 'FAIL'); assert(r.diagnostics.some(row => row.code === 'INPUT_CAPABILITY_TYPE_INVALID')); assert.equal(r.summary.starts, 0);
  });
  await check('missing-capability-receipt', [event('missing', { event: 'startup.handshake', ok: true })], r => {
    assert.equal(r.status, 'INCOMPLETE'); assert.equal(r.summary.curveWrites, 0);
    assert.equal(r.evidence[0].feedback.code, 'FAN_API_RESPONSE_INVALID');
  });
  await check('contradictory-capability-receipt', [{ ...good, hardwareCapable: false }, intent('apply', 'apply', { nodes: CURVE })], r => {
    assert.equal(r.status, 'INCOMPLETE'); assert.equal(r.summary.starts, 0); assert.equal(r.summary.curveWrites, 0);
    assert(r.diagnostics.some(row => row.code === 'CAPABILITY_RECEIPT_CONTRADICTORY'));
  });
  await check('stale-source-generation', [good, event('sleep', { event: 'fan.power', phase: 'suspending', generation: 2 }), intent('old', 'apply', { nodes: CURVE, generation: 1 })], r => {
    assert(r.diagnostics.some(row => row.code === 'SOURCE_GENERATION_STALE' && row.stage === 'input')); assert.equal(r.summary.curveWrites, 0);
  });
  await check('sleep-retires-in-flight-old-curve', [good,
    intent('inflight', 'apply', { nodes: CURVE, settle: false }),
    event('sleep', { event: 'fan.power', phase: 'suspending', generation: 2 }), intent('off', 'disable'), intent('close', 'close')], r => {
    assert.equal(r.summary.curveWrites, 0); assert.equal(r.evidence[1].feedback.status, 'rejected');
    assert.equal(r.summary.starts, 1); assert.equal(r.summary.lifecycle, 'stopped');
  }, { provenance: FIXTURE, delays: { '/api/open': 100 } });
  await check('idle-eleven-days-no-restart-or-poll-storm', [good, intent('apply', 'apply', { nodes: CURVE }), intent('off', 'disable'),
    event('eleven-days', { event: 'fan.clock', advanceMs: 11 * 86400000 }), intent('close', 'close')], r => {
    assert.equal(r.status, 'PASS'); assert.equal(r.summary.starts, 1); assert.equal(r.evidence[3].consumer.requests.length, 0);
    assert.equal(r.evidence[3].state.after.pendingTimers, 0);
  });
  for (const [id, override] of [
    ['real-http-mistyped-capability', { hardwareWritesEnabled: 'false' }],
    ['real-http-contradictory-capability', { hardwareCapable: false }],
  ]) await check(id, [good, intent('apply', 'apply', { nodes: CURVE })], r => {
    assert.equal(r.summary.curveWrites, 0); assert.equal(r.summary.starts, 2);
    assert(r.evidence.every(row => row.feedback.code === 'FAN_API_RESPONSE_INVALID'));
    assert.equal(r.summary.pendingTimers, 0);
  }, { provenance: FIXTURE, handshakeOverride: override });
  await check('declared-protocol-mismatch-blocks-control', [{ ...good, protocolVersion: 99 }, intent('apply', 'apply', { nodes: CURVE })], r => {
    assert.equal(r.status, 'FAIL'); assert.equal(r.compatibility.status, 'INCOMPATIBLE'); assert.equal(r.summary.curveWrites, 0);
    assert.equal(r.evidence[0].feedback.code, 'FAN_PROTOCOL_INCOMPATIBLE');
  });
  await check('fault-locked-reprobe-retains-existing-write-debt', [event('fault-locked', {
    event: 'startup.handshake', ok: false, supported: false, protocolVersion: 2,
    hostMode: 'real-hc-fault-locked', hardwareCapable: false, hardwareWritesEnabled: true, hardwareWritesObserved: true,
  })], r => {
    assert(!r.diagnostics.some(row => row.code === 'CAPABILITY_RECEIPT_CONTRADICTORY'));
    assert.equal(r.evidence[0].consumer.status, 'dispatched');
    assert.notEqual(r.evidence[0].feedback.code, 'FAN_API_RESPONSE_INVALID');
    assert.equal(r.evidence[0].feedback.sourceHardwareWriteDebt.hardwareWritesObserved, true);
    assert.equal(r.evidence[0].feedback.sourceHardwareWriteDebt.clearedBySimulation, false);
    assert.equal(r.summary.curveWrites, 0);
  });
  return { schemaVersion: 1, kind: 'fan-signal-regressions', status: cases.every(row => row.status === 'PASS') ? 'PASS' : 'FAIL',
    scope: 'software-signal-consumption', sourceChanges: cases[0]?.replay.sourceChanges ?? [],
    executedProductModules: cases[0]?.replay.executedProductModules ?? [], inputConsumerStateFeedback: cases.map(row => ({ id: row.id, evidence: row.replay.evidence })),
    compatibility: { status: 'SCENARIO_EXPECTATIONS_VERIFIED', hardwareRuntimeGate: 'not-executed', versionsPinnedByConsumer: false }, remainingGaps: cases[0]?.replay.remainingGaps ?? [], cases, summary: { total: cases.length, passed: cases.filter(row => row.status === 'PASS').length,
      failed: cases.filter(row => row.status === 'FAIL').length, physicalReceipts: 0 }, sourceHashes: [...sourceHashes.values()], boundary: { ...BOUNDARY } };
}

