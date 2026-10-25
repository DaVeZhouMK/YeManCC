/** Directed software controls only; caller provides pinned, already-read r2 samples.
 * No filesystem/process/browser operations. Synthetic changes are explicitly labeled.
 */
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { analyzeMonitorFieldLogs, FIELD_LOG_LIMITS, MONITOR_FIELD_LOG_SCHEMA } from './field-log-consumer.mjs';

const clone = value => JSON.parse(JSON.stringify(value));
const jsonl = rows => rows.map(row => JSON.stringify(row)).join('\n') + '\n';
const sha = text => createHash('sha256').update(text, 'utf8').digest('hex');
const parse = text => text.trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
const reserialize = rows => rows.map(row => {
  const next = clone(row);
  if (next.trace && next.message !== undefined) next.message = JSON.stringify(next.trace);
  return next;
});
const codes = report => new Set(report.findings.map(row => row.code));
const owner = report => report.cohorts[0]?.owners[0];

export function runFieldLogConsumerTests(fixtures) {
  assert.equal(fixtures.fixtureScope, 'SOFTWARE_ACTUAL_R2');
  assert.match(fixtures.buildSha256, /^[0-9a-f]{64}$/);
  const rawNative = parse(fixtures.nativeJsonl), rawFrontend = parse(fixtures.frontendJsonl);
  assert(rawNative.length && rawFrontend.length);
  const traceRows = rawFrontend.filter(row => row.where === 'monitor-notice-lifecycle' && row.trace?.hostId === 1);
  const create = rawNative.find(row => row.stage === 'native-created');
  const ready = rawNative.find(row => row.stage === 'native-ready');
  const retired = rawNative.find(row => row.stage === 'native-retired' && row.leaseRetired === true && row.resourcesDestroyed === true && row.ownedOnly === true && row.obsolete === false);
  assert(create && ready && retired);
  const tuple = { pid: traceRows[0].pid, buildSha256: fixtures.buildSha256, nativeGeneration: traceRows[0].nativeGeneration };
  const nativeBase = [rawNative.find(row => row.stage === 'native-reserved'), create, ready,
    rawNative.find(row => row.stage === 'native-close'), retired].map(row => {
    const next = clone(row);
    if ('leaseId' in next) next.leaseId = create.leaseId;
    if ('hudId' in next) next.hudId = create.hudId;
    next.pid = tuple.pid; next.nativeGeneration = tuple.nativeGeneration; next.powerGeneration = 1;
    return next;
  });
  const frontBase = traceRows.map(row => {
    const next = clone(row);
    if (next.trace.hudId > 0) next.trace.hudId = create.hudId;
    return next;
  });
  const retry = clone(frontBase.find(row => row.trace.stage === 'attempt-start'));
  const scheduled = clone(retry), fired = clone(retry);
  scheduled.trace.stage = 'retry-scheduled'; scheduled.trace.delayMs = 250;
  fired.trace.stage = 'retry-fired'; fired.trace.delayMs = 250;
  frontBase.splice(3, 0, scheduled, fired);
  const scope = 'SOFTWARE_DERIVED_R2';
  function pack(nativeRows = nativeBase, frontendRows = frontBase, sourceTuple = tuple, fixtureScope = scope) {
    const texts = [jsonl(nativeRows), jsonl(reserialize(frontendRows))];
    const kinds = ['native-lifecycle', 'frontend-trace'];
    return { manifest: { schema: MONITOR_FIELD_LOG_SCHEMA, fixtureScope, cohorts: [{ sourceTuple: clone(sourceTuple), sources: kinds.map((kind, i) => ({
      kind, sha256: sha(texts[i]), sourceTuple: clone(sourceTuple), generationBinding: 'verified-single-native-generation-slice', coverage: 'complete-segment',
    })) }] }, streams: kinds.map((kind, i) => ({ cohortIndex: 0, kind, jsonl: texts[i] })) };
  }
  let lastAnalysis = null;
  const analyze = input => (lastAnalysis = analyzeMonitorFieldLogs(input));
  const cases = [];
  function run(id, fn) {
    try {
      lastAnalysis = null;
      const result = fn();
      cases.push({ id, passed: true, ...(result ? { observation: {
        status: result.status, fieldAcceptance: result.fieldAcceptance, currentNativeHealth: result.currentNativeHealth,
        firstFindingCode: result.firstFinding?.code ?? null, firstDifferenceCode: result.firstDifference?.code ?? null,
        cohorts: result.cohorts.length, owners: result.cohorts.reduce((n, c) => n + c.owners.length, 0),
      } } : {}) });
    } catch (error) { cases.push({ id, passed: false, errorCode: error.code === 'ERR_ASSERTION' ? 'ASSERTION_FAILED' : 'TEST_EXCEPTION', observedStatus: lastAnalysis?.status ?? null, observedCodes: lastAnalysis ? [...new Set(lastAnalysis.findings.map(row => row.code))] : [] }); }
  }
  function expectCode(input, code) {
    const result = analyze(input);
    assert(codes(result).has(code)); assert.equal(result.fieldAcceptance, false);
    return result;
  }
  run('actual-r2-raw-software-logs-do-not-invent-native-identity-or-owner-pair', () => {
    const input = pack(rawNative, rawFrontend, tuple, 'SOFTWARE_ACTUAL_R2');
    const r = expectCode(input, 'ROW_PID_MISSING');
    assert.equal(r.status, 'INCOMPLETE'); assert.equal(r.cohorts[0].identityBound, false); assert.equal(r.cohorts[0].owners.length, 0);
    return r;
  });
  run('derived-software-four-node-receipts-link-without-current-health-or-field-signature', () => {
    const r = analyze(pack());
    assert.equal(r.status, 'EVIDENCE_LINKED');
    assert.equal(r.fieldAcceptance, false); assert.equal(r.currentNativeHealth, 'UNKNOWN');
    assert.equal(owner(r).nodes.length, 4); assert(owner(r).nodes.every(n => n.status === 'EVIDENCE_LINKED'));
    assert.equal(owner(r).nodes[3].runtimeRecoveryBoundProven, false);
    assert.equal(owner(r).lastNativeOwnerReceiptStage, 'native-retired');
    return r;
  });
  run('missing-production-pid-is-unknown', () => { const n = clone(nativeBase); delete n[0].pid; return expectCode(pack(n), 'ROW_PID_MISSING'); });
  run('missing-production-power-generation-is-unknown', () => { const n = clone(nativeBase); delete n[0].powerGeneration; return expectCode(pack(n), 'ROW_POWER_GENERATION_MISSING'); });
  run('native-generation-missing-in-frontend-is-unknown', () => { const f = clone(frontBase); delete f[0].nativeGeneration; return expectCode(pack(undefined, f), 'ROW_NATIVE_GENERATION_MISSING'); });
  run('different-pid-invalidates-cohort-and-is-never-merged', () => {
    const n = clone(nativeBase); n[1].pid++;
    const r = expectCode(pack(n), 'ROW_PID_MISMATCH'); assert.equal(r.cohorts[0].owners.length, 0); return r;
  });
  run('different-native-generation-is-not-merged', () => {
    const f = clone(frontBase); f[1].nativeGeneration++;
    const r = expectCode(pack(undefined, f), 'ROW_NATIVE_GENERATION_MISMATCH'); assert.equal(r.cohorts[0].owners.length, 0); return r;
  });
  run('unbound-build-is-detected', () => {
    const input = pack(); delete input.manifest.cohorts[0].sourceTuple.buildSha256;
    return expectCode(input, 'BUILD_UNBOUND');
  });
  run('different-build-source-tuples-are-not-merged', () => {
    const input = pack(); input.manifest.cohorts[0].sources[0].sourceTuple.buildSha256 = 'b'.repeat(64);
    return expectCode(input, 'SOURCE_TUPLE_MISMATCH');
  });
  run('row-build-contradiction-is-detected', () => { const n = clone(nativeBase); n[0].buildSha256 = 'b'.repeat(64); return expectCode(pack(n), 'ROW_BUILD_MISMATCH'); });
  run('source-byte-digest-is-required-not-blindly-rebound', () => {
    const input = pack(); input.streams[0].jsonl += '\n'; return expectCode(input, 'SOURCE_BYTES_NOT_BOUND');
  });
  run('different-native-hud-owner-is-not-used-for-renderer', () => {
    const n = clone(nativeBase); n[1].hudId++;
    const r = expectCode(pack(n), 'NATIVE_OWNER_BINDING_MISMATCH'); assert.notEqual(owner(r).nodes[2].status, 'EVIDENCE_LINKED'); return r;
  });
  run('renderer-healthy-alone-is-not-native-health', () => {
    const r = expectCode(pack(nativeBase.filter(row => row.stage !== 'native-ready')), 'NATIVE_READY_RECEIPT_MISSING');
    assert.equal(r.currentNativeHealth, 'UNKNOWN'); assert.equal(owner(r).nativeReadyReceiptObservedForRendererPower, false); return r;
  });
  run('old-ready-power-generation-does-not-prove-current-ready', () => {
    const n = clone(nativeBase); n.find(row => row.stage === 'native-ready').powerGeneration = 0;
    const r = expectCode(pack(n), 'READY_CURRENT_POWER_MISSING_OLD_READY_PRESENT'); assert.equal(owner(r).nodes[2].status, 'UNKNOWN'); return r;
  });
  run('off-without-new-on-before-healthy-is-a-difference', () => {
    const f = clone(frontBase), index = f.findIndex(row => row.trace.stage === 'healthy');
    const off = clone(f[index]); off.trace.stage = 'intent-off'; off.trace.enabled = false; f.splice(index, 0, off);
    const r = expectCode(pack(undefined, f), 'HEALTH_AFTER_OFF_WITHOUT_ON'); assert.equal(owner(r).nodes[2].status, 'DIFFERENCE'); return r;
  });
  run('explicit-on-after-off-is-recognized-in-source-order', () => {
    const f = clone(frontBase), index = f.findIndex(row => row.trace.stage === 'healthy');
    const off = clone(f[index]), on = clone(f[index]); off.trace.stage = 'intent-off'; off.trace.enabled = false; on.trace.stage = 'intent-on';
    f.splice(index, 0, off, on); const r = analyze(pack(undefined, f)); assert(!codes(r).has('HEALTH_AFTER_OFF_WITHOUT_ON')); return r;
  });
  run('stop-cannot-be-reversed-by-later-intent-on-same-host', () => {
    const f = clone(frontBase), stop = f.find(row => row.trace.stage === 'stop'), on = clone(stop); on.trace.stage = 'intent-on'; f.push(on);
    return expectCode(pack(undefined, f), 'INTENT_ON_AFTER_STOP');
  });
  run('old-owner-ready-after-new-reservation-is-detected', () => {
    const f = clone(frontBase), index = f.findIndex(row => row.trace.stage === 'ready-accepted');
    const reservation = clone(f.find(row => row.trace.stage === 'reserve-result')); reservation.trace.leaseId++; f.splice(index, 0, reservation);
    return expectCode(pack(undefined, f), 'STALE_OWNER_READY_OR_HEALTH');
  });
  run('renderer-observed-power-disagreement-cannot-be-health-proof', () => {
    const f = clone(frontBase); f.find(row => row.trace.stage === 'healthy').trace.powerGeneration = 0;
    return expectCode(pack(undefined, f), 'POWER_GENERATION_DISAGREEMENT');
  });
  run('close-only-even-success-needs-native-retirement', () => {
    const n = nativeBase.filter(row => row.stage !== 'native-retired').map(row => ({ ...row, ...(row.stage === 'native-close' ? { ok: true } : {}) }));
    const r = expectCode(pack(n), 'NATIVE_RETIRE_RECEIPT_MISSING'); assert.equal(owner(r).nodes[1].status, 'UNKNOWN'); return r;
  });
  run('missing-native-retire-field-remains-unknown', () => { const n = clone(nativeBase); delete n.at(-1).resourcesDestroyed; return expectCode(pack(n), 'NATIVE_RETIRE_FIELDS_INCOMPLETE'); });
  run('null-native-retire-field-remains-unknown', () => { const n = clone(nativeBase); n.at(-1).ownedOnly = null; return expectCode(pack(n), 'NATIVE_RETIRE_FIELDS_INCOMPLETE'); });
  run('native-retire-false-is-not-green', () => { const n = clone(nativeBase); n.at(-1).resourcesDestroyed = false; return expectCode(pack(n), 'NATIVE_RETIRE_NOT_CONFIRMED'); });
  run('obsolete-native-retire-is-not-current-resource-proof', () => { const n = clone(nativeBase); n.at(-1).obsolete = true; return expectCode(pack(n), 'NATIVE_RETIRE_OBSOLETE_OR_UNKNOWN'); });
  run('later-native-retire-success-retains-failure-count-without-cross-file-sorting', () => {
    const n = clone(nativeBase), failed = clone(n.at(-1)); failed.resourcesDestroyed = false; n.splice(n.length - 1, 0, failed);
    const r = analyze(pack(n)); assert.equal(owner(r).nodes[1].status, 'EVIDENCE_LINKED'); assert.equal(owner(r).observedRetirementFailures, 1); return r;
  });
  run('cross-file-timestamps-are-never-used-as-causal-order', () => {
    const n = clone(nativeBase), f = clone(frontBase); n.forEach(row => { row.time = '2099-01-01'; }); f.forEach(row => { row.time = '1900-01-01'; });
    const r = analyze(pack(n, f)); assert.equal(r.status, analyze(pack()).status);
    assert.match(r.ordering, /NO_CROSS_FILE_TIMESTAMP_CAUSALITY/); return r;
  });
  run('unsafe-payload-and-unknown-keys-are-not-echoed', () => {
    const f = clone(frontBase); f[0].trace.nonce = 'PRIVATE_NONCE_COMMAND_PAYLOAD';
    const r = expectCode(pack(undefined, f), 'SENSITIVE_OR_UNKNOWN_TRACE_FIELD'); assert(!JSON.stringify(r).includes('PRIVATE_NONCE_COMMAND_PAYLOAD')); return r;
  });
  run('unknown-stage-value-is-not-echoed', () => {
    const n = clone(nativeBase); n[0].stage = 'PRIVATE_UNKNOWN_STAGE_COMMAND';
    const r = expectCode(pack(n), 'UNKNOWN_OR_MISSING_STAGE'); assert(!JSON.stringify(r).includes('PRIVATE_UNKNOWN_STAGE_COMMAND')); return r;
  });
  run('malformed-json-does-not-echo-parser-input-or-error', () => {
    const input = pack(); input.streams[0].jsonl = '{"PRIVATE_BROKEN_COMMAND"'; input.manifest.cohorts[0].sources[0].sha256 = sha(input.streams[0].jsonl);
    const r = expectCode(input, 'MALFORMED_JSON_LINE'); assert(!JSON.stringify(r).includes('PRIVATE_BROKEN_COMMAND')); return r;
  });
  run('unrelated-domain-payload-is-ignored-without-echo', () => {
    const n = clone(nativeBase); n.push({ event: 'unrelated-native-event', token: 'PRIVATE_UNRELATED_TOKEN' });
    const r = analyze(pack(n)); assert.equal(r.status, 'EVIDENCE_LINKED'); assert.equal(r.counts.ignoredRecords, 1); assert(!JSON.stringify(r).includes('PRIVATE_UNRELATED_TOKEN')); return r;
  });
  run('existing-policy-budget-is-not-redefined-by-the-consumer', () => { const f = clone(frontBase); f.find(row => row.trace.stage === 'retry-scheduled').trace.delayMs = 2000; return expectCode(pack(undefined, f), 'RECOVERY_POLICY_FIELDS_CHANGED'); });
  run('missing-recovery-is-unknown-not-assumed-passed', () => {
    const f = frontBase.filter(row => !row.trace.stage.startsWith('retry-'));
    const r = expectCode(pack(undefined, f), 'RECOVERY_NOT_OBSERVED'); assert.equal(owner(r).nodes[3].status, 'UNKNOWN'); return r;
  });
  run('hard-record-limit-counts-unrelated-records-too', () => {
    const input = pack(); input.streams[0].jsonl = '{}\n'.repeat(FIELD_LOG_LIMITS.maxRecords + 1); return expectCode(input, 'LIMIT_RECORDS');
  });
  run('hard-line-limit-counts-empty-lines-too', () => { const input = pack(); input.streams[0].jsonl = '\n'.repeat(FIELD_LOG_LIMITS.maxTotalLines); return expectCode(input, 'LIMIT_TOTAL_LINES'); });
  run('hard-line-byte-limit-is-before-json-parse', () => { const input = pack(); input.streams[0].jsonl = ' '.repeat(FIELD_LOG_LIMITS.maxLineBytes + 1); return expectCode(input, 'LIMIT_LINE_BYTES'); });
  run('utf8-line-byte-limit-is-not-character-count', () => { const input = pack(); input.streams[0].jsonl = '汉'.repeat(6000); return expectCode(input, 'LIMIT_LINE_BYTES'); });
  run('hard-stream-byte-limit-is-before-line-splitting', () => { const input = pack(); input.streams[0].jsonl = ' '.repeat(FIELD_LOG_LIMITS.maxStreamBytes + 1); return expectCode(input, 'LIMIT_STREAM_BYTES'); });
  run('different-cohort-tuples-never-merge-identical-owner-numbers', () => {
    const one = pack(), tuple2 = { ...tuple, pid: tuple.pid + 1, nativeGeneration: tuple.nativeGeneration + 1, buildSha256: 'c'.repeat(64) };
    const n2 = nativeBase.map(row => ({ ...clone(row), pid: tuple2.pid, nativeGeneration: tuple2.nativeGeneration }));
    const f2 = frontBase.map(row => ({ ...clone(row), pid: tuple2.pid, nativeGeneration: tuple2.nativeGeneration }));
    const two = pack(n2, f2, tuple2); one.manifest.cohorts.push(two.manifest.cohorts[0]); one.streams.push(...two.streams.map(row => ({ ...row, cohortIndex: 1 })));
    const r = analyze(one); assert.equal(r.status, 'EVIDENCE_LINKED'); assert.equal(r.cohorts.length, 2);
    assert.equal(r.cohorts[0].owners.length, 1); assert.equal(r.cohorts[1].owners.length, 1);
    assert.notDeepEqual(r.cohorts[0].sourceTuple, r.cohorts[1].sourceTuple); return r;
  });
  run('two-half-cohorts-cannot-borrow-each-others-missing-receipts', () => {
    const one = pack(nativeBase, []), two = pack([], frontBase); one.manifest.cohorts.push(two.manifest.cohorts[0]); one.streams.push(...two.streams.map(row => ({ ...row, cohortIndex: 1 })));
    const r = expectCode(one, 'NATIVE_CREATE_OWNER_BINDING_MISSING'); assert.equal(r.status, 'INCOMPLETE'); return r;
  });
  run('ordinary-scope-label-does-not-create-real-zip-acceptance', () => {
    const input = pack(); input.manifest.fixtureScope = 'ORDINARY_EXPORT_UNVERIFIED'; const r = analyze(input);
    assert.equal(r.status, 'EVIDENCE_LINKED'); assert.equal(r.fieldAcceptance, false); assert.equal(r.provenanceValidated, false); return r;
  });
  run('pure-call-does-not-mutate-input-and-is-deterministic', () => {
    const input = pack(), before = JSON.stringify(input), a = analyze(input), b = analyze(input);
    assert.equal(JSON.stringify(input), before); assert.deepEqual(a, b); return a;
  });
  run('stream-path-or-command-options-are-rejected-without-execution-or-echo', () => {
    const input = pack(); input.streams[0].path = 'PRIVATE_EXECUTABLE_COMMAND_PATH';
    const r = expectCode(input, 'STREAM_SHAPE_INVALID'); assert(!JSON.stringify(r).includes('PRIVATE_EXECUTABLE_COMMAND_PATH')); return r;
  });
  run('caller-limit-escalation-options-are-not-supported', () => { const input = pack(); input.limits = { maxRecords: 999999 }; return expectCode(input, 'INPUT_SHAPE_INVALID'); });
  run('invalid-binding-getters-are-never-executed', () => {
    let calls = 0; const input = pack(); Object.defineProperty(input.manifest.cohorts[0].sources[0], 'toJSON', { enumerable: true, get() { calls++; throw new Error('PRIVATE_GETTER'); } });
    const r = expectCode(input, 'SOURCE_BINDING_INVALID'); assert.equal(calls, 0); assert(!JSON.stringify(r).includes('PRIVATE_GETTER')); return r;
  });

  run('hard-total-byte-limit-covers-all-sources-and-cohorts', () => {
    const first = pack(), second = pack(); first.manifest.cohorts.push(second.manifest.cohorts[0]);
    first.streams.push(...second.streams.map(row => ({ ...row, cohortIndex: 1 })));
    const filler = (' '.repeat(10000) + '\n').repeat(75);
    for (let i = 0; i < 3; i++) {
      first.streams[i].jsonl = filler;
      const stream = first.streams[i]; first.manifest.cohorts[stream.cohortIndex].sources.find(b => b.kind === stream.kind).sha256 = sha(filler);
    }
    return expectCode(first, 'LIMIT_TOTAL_BYTES');
  });
  run('hard-owner-limit-prevents-unbounded-correlation-map', () => {
    const seed = frontBase.find(row => row.trace.stage === 'create-result');
    const f = Array.from({ length: FIELD_LOG_LIMITS.maxOwners + 1 }, (_, i) => {
      const row = clone(seed); row.trace.leaseId = 10000 + i; row.trace.hudId = 20000 + i; return row;
    });
    const r = expectCode(pack(undefined, f), 'LIMIT_OWNERS'); assert.equal(r.cohorts[0].owners.length, 0); return r;
  });
  run('hard-host-limit-prevents-unbounded-intent-map', () => {
    const seed = frontBase.find(row => row.trace.stage === 'intent-on');
    const f = Array.from({ length: FIELD_LOG_LIMITS.maxHosts + 1 }, (_, i) => { const row = clone(seed); row.trace.hostId = i + 1; return row; });
    return expectCode(pack(undefined, f), 'LIMIT_HOSTS');
  });
  run('finding-output-is-bounded-and-truncation-cannot-look-complete', () => {
    const seed = frontBase.find(row => row.trace.stage === 'create-result');
    const f = Array.from({ length: 40 }, (_, i) => { const row = clone(seed); row.trace.leaseId = 10000 + i; row.trace.hudId = 20000 + i; return row; });
    const r = analyze(pack(undefined, f)); assert(r.findings.length <= FIELD_LOG_LIMITS.maxFindings);
    assert.equal(r.findingsTruncated, true); assert.equal(r.incomplete, true); assert.notEqual(r.status, 'EVIDENCE_LINKED'); return r;
  });
  run('production-native-generation-absence-requires-explicit-manifest-binding', () => {
    const n = clone(nativeBase); n.forEach(row => { delete row.nativeGeneration; });
    const r = analyze(pack(n)); assert.equal(r.status, 'EVIDENCE_LINKED');
    assert.equal(r.cohorts[0].nativeGenerationBinding, 'MANIFEST_ONLY_WHEN_NATIVE_ROW_HAS_NO_STAMP'); assert.equal(r.provenanceValidated, false); return r;
  });
  run('partial-coverage-is-explicitly-incomplete-even-with-matching-receipts', () => {
    const input = pack(); input.manifest.cohorts[0].sources[0].coverage = 'partial';
    const r = expectCode(input, 'SOURCE_COVERAGE_PARTIAL'); assert.equal(r.status, 'INCOMPLETE'); return r;
  });
  run('input-stream-array-order-does-not-rewrite-valid-correlation', () => {
    const one = pack(), two = pack(); two.streams.reverse();
    assert.deepEqual(analyze(one), analyze(two));
  });
  run('mutating-returned-suggestion-array-cannot-poison-future-calls', () => {
    const n = nativeBase.filter(row => row.stage !== 'native-retired');
    const first = analyze(pack(n)); first.findings[0].caseIds.push('PRIVATE_SUGGESTION_INJECTION');
    const second = analyze(pack(n)); assert(!JSON.stringify(second).includes('PRIVATE_SUGGESTION_INJECTION')); return second;
  });
  run('safe-trace-only-input-does-not-require-duplicated-message-payload', () => {
    const input = pack(); const f = parse(input.streams[1].jsonl); f.forEach(row => { delete row.message; });
    input.streams[1].jsonl = jsonl(f); input.manifest.cohorts[0].sources[1].sha256 = sha(input.streams[1].jsonl);
    const r = analyze(input); assert.equal(r.status, 'EVIDENCE_LINKED'); return r;
  });

  return { schemaVersion: 1, fixtureScope: 'SOFTWARE_ACTUAL_R2_WITH_EXPLICITLY_DERIVED_CONTROLS',
    sourceDigests: { native: sha(fixtures.nativeJsonl), frontend: sha(fixtures.frontendJsonl), softwareHarnessBuild: fixtures.buildSha256 },
    derivation: ['Actual native r2 receipt templates and real renderer rows reused.', 'Coherent controls explicitly remap Ready/retired owner coordinates to the real native-created owner.',
      'Native PID/power/native-generation stamps are injected as SOFTWARE_DERIVED_R2 metadata only.', 'Retry stages use existing scalar policy fields; no fabricated ordinary field log or ZIP.'],
    pass: cases.every(row => row.passed), cases, counts: { total: cases.length, passed: cases.filter(row => row.passed).length, failed: cases.filter(row => !row.passed).length },
    ordinaryFieldZipProvided: false, fieldAcceptance: false, globalBrowserExecuted: false, installedProductExecuted: false };
}