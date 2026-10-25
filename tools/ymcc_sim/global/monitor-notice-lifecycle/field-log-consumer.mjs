/** Pure, bounded analysis of already-read ordinary log text. No file/process/ZIP IO.
 * A caller manifest binds each exact byte string to one cohort/source tuple.
 * Correlation proves observed receipts only: fieldAcceptance is always false.
 */
import { createHash } from 'node:crypto';

export const MONITOR_FIELD_LOG_SCHEMA = 'ymcc.monitor-field-log-cohorts.v1';
export const FIELD_LOG_LIMITS = Object.freeze({
  maxCohorts: 4, maxStreams: 8, maxManifestBytes: 16384,
  maxStreamBytes: 1048576, maxTotalBytes: 2097152,
  maxLineBytes: 16384, maxTotalLines: 4096, maxRecords: 2048,
  maxOwners: 128, maxHosts: 128, maxFindings: 128, maxRefsPerNode: 8,
});
export const MONITOR_FIELD_LOG_NODES = Object.freeze([
  'notice-native-reservation-to-create', 'notice-exit-retirement',
  'notice-power-current-health', 'notice-bounded-transient-recovery',
]);
const [CREATE, EXIT, POWER, RECOVERY] = MONITOR_FIELD_LOG_NODES;
const TESTS = Object.freeze({
  parent: 'lifecycle.monitor-notice-host', native: 'lifecycle.monitor-notice-native-lease',
  webview: 'lifecycle.monitor-notice-real-webview', renderer: 'lifecycle.monitor-notice-real-renderer',
});
const CASES = Object.freeze({
  [CREATE]: ['late-create-after-real-ipc-timeout', 'off-during-pending-create'],
  [EXIT]: ['normal-stop-zero-resources', 'stop-close-false', 'stop-close-hold'],
  [POWER]: ['sleep-owner-loss-no-self-heal', 'stale-ready-after-stop-does-not-own', 'off-stays-off-with-events'],
  [RECOVERY]: ['transient-carrier-loss-no-self-heal', 'transient-feedback-burst-has-no-retry-budget'],
});
const NATIVE_STAGES = new Set(['native-reserved', 'native-created', 'native-create-error',
  'native-retired', 'native-retire-error', 'native-close', 'native-ready', 'native-closed', 'native-unavailable']);
const RENDERER_STAGES = new Set(['prepare-blocked', 'attempt-start', 'status-result', 'reserve-result',
  'create-start', 'create-result', 'create-stale', 'attempt-error', 'ready-accepted', 'ready-rejected',
  'owner-lost', 'closed-rejected', 'close-start', 'close-result', 'retire-result', 'retire-unconfirmed',
  'retire-confirmed', 'retry-scheduled', 'retry-cancelled', 'retry-fired', 'retry-exhausted', 'ready-timeout',
  'ready-wait-start', 'ready-wait-cancelled', 'healthy', 'power-held', 'power-rejected', 'power-committed',
  'intent-on', 'intent-off', 'stop', 'feedback-rejected', 'frame-confirmed']);
const BOOLS = new Set(['powerHeld', 'enabled', 'ended', 'ownerReady', 'ambiguousCreate', 'carrierBlocked',
  'ok', 'fallback', 'statusReady', 'available', 'recoveryBlocked']);
const FLAGS = ['leaseRetired', 'resourcesDestroyed', 'ownedOnly', 'obsolete'];
const NUMBERS = new Set(['hostId', 'epoch', 'ownerEpoch', 'leaseId', 'hudId', 'attempts', 'maxAttempts',
  'readyTimeoutMs', 'powerGeneration', 'committedGeneration', 'retireAttempt', 'delayMs', 'elapsedMs', 'timeoutMs']);
const NULLABLE = new Set(['ownerEpoch', 'leaseId', 'hudId', 'powerGeneration', 'committedGeneration']);
const REASONS = new Set(['none', 'off', 'ended', 'power-held', 'ambiguous', 'carrier-blocked', 'retiring',
  'budget', 'already-ready', 'timeout', 'remote-error', 'transport-error', 'stale-owner', 'stale-generation']);
const PHASES = new Set(['status', 'close', 'reserve', 'create']);
const TRACE_KEYS = new Set(['stage', 'reason', 'phase', ...BOOLS, ...FLAGS, ...NUMBERS]);
const NATIVE_META = new Set(['event', 'origin', 'time', 'pid', 'hwnd', 'powerLifecycle', 'powerGeneration', 'nativeGeneration', 'build', 'buildSha256']);
const FRONT_META = new Set(['event', 'origin', 'where', 'severity', 'message', 'trace', 'messageTruncated',
  'traceParseStatus', 'route', 'build', 'buildSha256', 'time', 'nativeGeneration', 'powerGeneration', 'gpuMode', 'routeStatus', 'pid', 'lane']);
const SCOPES = new Set(['SOFTWARE_ACTUAL_R2', 'SOFTWARE_DERIVED_R2', 'ORDINARY_EXPORT_UNVERIFIED']);
const isInt = (n, min = 0) => Number.isSafeInteger(n) && n >= min;
const isHash = value => typeof value === 'string' && /^[0-9a-f]{64}$/i.test(value);
const bytes = value => Buffer.byteLength(value, 'utf8');
const digest = value => createHash('sha256').update(value, 'utf8').digest('hex');
const plain = value => {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  if (![Object.prototype, null].includes(Object.getPrototypeOf(value))) return false;
  return Object.values(Object.getOwnPropertyDescriptors(value)).every(d => 'value' in d);
};
const keysOnly = (object, allowed) => plain(object) && Object.keys(object).every(k => allowed.includes(k));
const tupleOf = value => plain(value) && isInt(value.pid, 1) && isHash(value.buildSha256) && isInt(value.nativeGeneration)
  && keysOnly(value, ['pid', 'buildSha256', 'nativeGeneration'])
  ? { pid: value.pid, buildSha256: value.buildSha256.toLowerCase(), nativeGeneration: value.nativeGeneration } : null;
const sameTuple = (a, b) => a && b && a.pid === b.pid && a.buildSha256 === b.buildSha256 && a.nativeGeneration === b.nativeGeneration;
const refs = rows => rows.slice(0, FIELD_LOG_LIMITS.maxRefsPerNode).map(r => ({ source: r.source, line: r.line, powerGeneration: r.pg }));
const canonical = row => JSON.stringify(Object.fromEntries(Object.keys(row).sort().map(key => [key, row[key]])));

/** Returns safe fixed codes and scalar coordinates, never original payload/error text.
 * streams: [{cohortIndex, kind:'native-lifecycle'|'frontend-trace', jsonl}]
 * manifest.cohorts[].sources: [{kind, sha256, sourceTuple,
 *   generationBinding:'verified-single-native-generation-slice', coverage:'complete-segment'|'partial'}]
 * Native logs lack nativeGeneration: that association is explicitly manifest-only.
 */
export function analyzeMonitorFieldLogs(input) {
  const report = {
    schemaVersion: 1, kind: 'monitor-field-log-analysis', fixtureScope: 'UNSPECIFIED',
    status: 'INCOMPLETE', fieldAcceptance: false, currentNativeHealth: 'UNKNOWN',
    provenanceValidated: false, limits: FIELD_LOG_LIMITS,
    ordering: 'VALIDATION_THEN_COHORT_NATIVE_FRONTEND_LINE_NODE_ORDER_NO_CROSS_FILE_TIMESTAMP_CAUSALITY',
    counts: { bytes: 0, lines: 0, records: 0, relevantRecords: 0, ignoredRecords: 0, rejectedRecords: 0, inspectedRecords: 0, projectedRelevantRecords: 0, correlatableRecords: 0, uninspectedRecords: 0 },
    cohorts: [], findings: [], firstFinding: null, firstDifference: null, firstMissingEvidence: null,
    suggestedTestIds: [], suggestedCaseIds: [], findingsTruncated: false,
  };
  const seenFindings = new Set();
  const contexts = [];
  let hasDifference = false, hasMissing = false;
  function add(severity, code, at = {}, nodeId = CREATE) {
    if (severity === 'DIFFERENCE') hasDifference = true; else hasMissing = true;
    const row = { severity, code, nodeId,
      cohortIndex: isInt(at.cohortIndex) ? at.cohortIndex : null,
      source: ['manifest', 'native-lifecycle', 'frontend-trace'].includes(at.source) ? at.source : 'manifest',
      line: isInt(at.line, 1) ? at.line : null,
      testIds: nodeId === EXIT ? [TESTS.parent, TESTS.native, TESTS.webview] : [TESTS.parent, TESTS.native, TESTS.renderer],
      caseIds: [...CASES[nodeId]],
    };
    for (const key of ['hostId', 'ownerEpoch', 'leaseId', 'hudId']) if (isInt(at[key])) row[key] = at[key];
    const signature = [severity, code, row.cohortIndex, row.source, row.hostId ?? '', row.leaseId ?? '', nodeId].join('/');
    if (!seenFindings.has(signature)) {
      if (seenFindings.size < FIELD_LOG_LIMITS.maxFindings) {
        seenFindings.add(signature); report.findings.push(row);
      } else report.findingsTruncated = true;
    }
    report.firstFinding ??= row;
    if (severity === 'DIFFERENCE') report.firstDifference ??= row; else report.firstMissingEvidence ??= row;
    return row;
  }
  function finish() {
    report.counts.projectedRelevantRecords = contexts.reduce((n, ctx) => n + ctx.native.length + ctx.frontend.length, 0);
    report.counts.correlatableRecords = contexts.filter(ctx => !ctx.invalid).reduce((n, ctx) => n + ctx.native.length + ctx.frontend.length, 0);
    report.counts.rejectedRecords = report.counts.inspectedRecords - report.counts.ignoredRecords - report.counts.projectedRelevantRecords;
    report.counts.uninspectedRecords = report.counts.records - report.counts.inspectedRecords;
    report.status = hasDifference ? 'DIFFERENCE' : hasMissing || report.findingsTruncated ? 'INCOMPLETE' : 'EVIDENCE_LINKED';
    report.incomplete = hasMissing || report.findingsTruncated;
    report.suggestedTestIds = [...new Set(report.findings.flatMap(row => row.testIds))];
    report.suggestedCaseIds = [...new Set(report.findings.flatMap(row => row.caseIds))];
    return report;
  }
  const unknownNodes = () => MONITOR_FIELD_LOG_NODES.map(id => ({ id, status: 'UNKNOWN', evidenceRefs: [] }));
  try {
    if (!keysOnly(input, ['manifest', 'streams'])) { add('REJECTED', 'INPUT_SHAPE_INVALID'); return finish(); }
    const { manifest, streams } = input;
    if (!keysOnly(manifest, ['schema', 'fixtureScope', 'cohorts']) || manifest.schema !== MONITOR_FIELD_LOG_SCHEMA
      || !SCOPES.has(manifest.fixtureScope) || !Array.isArray(manifest.cohorts)
      || !manifest.cohorts.length || manifest.cohorts.length > FIELD_LOG_LIMITS.maxCohorts) {
      add('REJECTED', 'MANIFEST_SHAPE_INVALID'); return finish();
    }
    report.fixtureScope = manifest.fixtureScope;
    // Bound structure and all strings before JSON.stringify (no caller toJSON/getters).
    for (let i = 0; i < manifest.cohorts.length; i++) {
      const cohort = manifest.cohorts[i], at = { cohortIndex: i, source: 'manifest' };
      if (!keysOnly(cohort, ['sourceTuple', 'sources']) || !Array.isArray(cohort.sources) || cohort.sources.length !== 2) {
        add('REJECTED', 'COHORT_SHAPE_INVALID', at); return finish();
      }
      if (!plain(cohort.sourceTuple) || Object.keys(cohort.sourceTuple).length > 3
        || Object.values(cohort.sourceTuple).some(v => typeof v === 'string' ? v.length > 64 : typeof v !== 'number')) {
        add('REJECTED', 'SOURCE_TUPLE_SHAPE_INVALID', at); return finish();
      }
      const tuple = tupleOf(cohort.sourceTuple);
      let invalid = !tuple;
      if (!isHash(cohort.sourceTuple.buildSha256)) add('MISSING', 'BUILD_UNBOUND', at);
      if (!isInt(cohort.sourceTuple.pid, 1)) add('MISSING', 'PID_UNBOUND', at);
      if (!isInt(cohort.sourceTuple.nativeGeneration)) add('MISSING', 'NATIVE_GENERATION_UNBOUND', at);
      const bindings = new Map();
      for (const binding of cohort.sources) {
        if (!keysOnly(binding, ['kind', 'sha256', 'sourceTuple', 'generationBinding', 'coverage'])
          || !['native-lifecycle', 'frontend-trace'].includes(binding.kind) || bindings.has(binding.kind)
          || !isHash(binding.sha256) || !tupleOf(binding.sourceTuple)
          || binding.generationBinding !== 'verified-single-native-generation-slice'
          || !['complete-segment', 'partial'].includes(binding.coverage)) {
          add('MISSING', 'SOURCE_BINDING_INVALID', at); invalid = true; continue;
        }
        if (tuple && !sameTuple(tuple, tupleOf(binding.sourceTuple))) { add('DIFFERENCE', 'SOURCE_TUPLE_MISMATCH', at); invalid = true; }
        if (binding.coverage === 'partial') add('MISSING', 'SOURCE_COVERAGE_PARTIAL', at);
        bindings.set(binding.kind, binding);
      }
      if (bindings.size !== 2) { add('MISSING', 'SOURCE_PAIR_MISSING', at); invalid = true; }
      const view = { cohortIndex: i, sourceTuple: tuple, identityBasis: 'CALLER_MANIFEST_PLUS_VALIDATED_ROW_STAMPS',
        provenanceValidated: false, nativeGenerationBinding: 'MANIFEST_ONLY_WHEN_NATIVE_ROW_HAS_NO_STAMP',
        identityBound: false, manifestTupleValid: !invalid, sourceCoverage: [...bindings.values()].map(b => ({ source: b.kind, coverage: b.coverage })), owners: [], nodes: unknownNodes(), currentNativeHealth: 'UNKNOWN' };
      report.cohorts.push(view);
      contexts.push({ index: i, tuple, invalid, bindings, seenStreams: new Set(), native: [], frontend: [], view });
    }
    if (bytes(JSON.stringify(contexts.map(ctx => ({ tuple: ctx.tuple, sources: [...ctx.bindings.values()].map(b => ({ kind: b.kind, sha256: b.sha256, sourceTuple: tupleOf(b.sourceTuple), generationBinding: b.generationBinding, coverage: b.coverage })) })))) > FIELD_LOG_LIMITS.maxManifestBytes) { add('REJECTED', 'LIMIT_MANIFEST_BYTES'); return finish(); }
    if (!Array.isArray(streams) || !streams.length || streams.length > FIELD_LOG_LIMITS.maxStreams) {
      add('REJECTED', 'STREAM_COUNT_INVALID'); return finish();
    }
    const prepared = [];
    for (const stream of streams) {
      if (!keysOnly(stream, ['cohortIndex', 'kind', 'jsonl']) || !isInt(stream.cohortIndex)
        || !contexts[stream.cohortIndex] || !['native-lifecycle', 'frontend-trace'].includes(stream.kind)
        || typeof stream.jsonl !== 'string') { add('REJECTED', 'STREAM_SHAPE_INVALID'); return finish(); }
      const ctx = contexts[stream.cohortIndex], at = { cohortIndex: ctx.index, source: stream.kind };
      if (ctx.seenStreams.has(stream.kind)) { add('REJECTED', 'DUPLICATE_SOURCE_STREAM', at); return finish(); }
      ctx.seenStreams.add(stream.kind);
      if (stream.jsonl.length > FIELD_LOG_LIMITS.maxStreamBytes || bytes(stream.jsonl) > FIELD_LOG_LIMITS.maxStreamBytes) {
        add('REJECTED', 'LIMIT_STREAM_BYTES', at); return finish();
      }
      report.counts.bytes += bytes(stream.jsonl);
      if (report.counts.bytes > FIELD_LOG_LIMITS.maxTotalBytes) { add('REJECTED', 'LIMIT_TOTAL_BYTES', at); return finish(); }
      let lines = 1;
      for (let p = 0; p < stream.jsonl.length; p++) if (stream.jsonl.charCodeAt(p) === 10 && ++lines + report.counts.lines > FIELD_LOG_LIMITS.maxTotalLines) {
        add('REJECTED', 'LIMIT_TOTAL_LINES', at); return finish();
      }
      report.counts.lines += lines;
      if (report.counts.lines > FIELD_LOG_LIMITS.maxTotalLines) { add('REJECTED', 'LIMIT_TOTAL_LINES', at); return finish(); }
      const split = stream.jsonl.split('\n');
      for (let line = 0; line < split.length; line++) {
        if (split[line].length > FIELD_LOG_LIMITS.maxLineBytes || bytes(split[line]) > FIELD_LOG_LIMITS.maxLineBytes) {
          add('REJECTED', 'LIMIT_LINE_BYTES', { ...at, line: line + 1 }); return finish();
        }
        if (split[line].trim() && ++report.counts.records > FIELD_LOG_LIMITS.maxRecords) {
          add('REJECTED', 'LIMIT_RECORDS', { ...at, line: line + 1 }); return finish();
        }
      }
      const binding = ctx.bindings.get(stream.kind);
      if (!binding || digest(stream.jsonl) !== binding.sha256.toLowerCase()) {
        add('DIFFERENCE', 'SOURCE_BYTES_NOT_BOUND', at); ctx.invalid = true;
      }
      prepared.push({ ctx, kind: stream.kind, lines: split });
    }
    function project(raw, source, at) {
      const stages = source === 'native-lifecycle' ? NATIVE_STAGES : RENDERER_STAGES;
      if (!plain(raw) || !stages.has(raw.stage)) { add('REJECTED', 'UNKNOWN_OR_MISSING_STAGE', at); return null; }
      if (Object.keys(raw).length > 40 || Object.keys(raw).some(key => !TRACE_KEYS.has(key))) {
        add('REJECTED', 'SENSITIVE_OR_UNKNOWN_TRACE_FIELD', at); return null;
      }
      const safe = {};
      for (const [key, value] of Object.entries(raw)) {
        let valid = key === 'stage';
        if (BOOLS.has(key)) valid = typeof value === 'boolean';
        if (FLAGS.includes(key)) valid = value === null || typeof value === 'boolean';
        if (NUMBERS.has(key)) valid = value === null && NULLABLE.has(key) || isInt(value);
        if (key === 'reason') valid = REASONS.has(value);
        if (key === 'phase') valid = PHASES.has(value);
        if (!valid) { add('REJECTED', 'INVALID_PROJECTED_FIELD_TYPE', at); return null; }
        if (key === 'attempts' && value > 3 || key === 'maxAttempts' && value !== 3
          || key === 'readyTimeoutMs' && value !== 10000 || key === 'delayMs' && ![250, 1000].includes(value)
          || key === 'retireAttempt' && ![1, 2].includes(value)) {
          add('DIFFERENCE', 'RECOVERY_POLICY_FIELDS_CHANGED', at, RECOVERY); return null;
        }
        safe[key] = value;
      }
      return safe;
    }
    prepared.sort((a, b) => a.ctx.index - b.ctx.index || (a.kind === 'native-lifecycle' ? -1 : 1));
    for (const stream of prepared) {
      const { ctx, kind } = stream;
      for (let i = 0; i < stream.lines.length; i++) {
        const line = stream.lines[i]; if (!line.trim()) continue;
        const at = { cohortIndex: ctx.index, source: kind, line: i + 1 };
        report.counts.inspectedRecords++;
        let raw;
        try { raw = JSON.parse(line); } catch { add('REJECTED', 'MALFORMED_JSON_LINE', at); ctx.invalid = true; report.counts.rejectedRecords++; continue; }
        if (!plain(raw)) { add('REJECTED', 'LOG_ROW_NOT_OBJECT', at); ctx.invalid = true; continue; }
        const relevant = kind === 'native-lifecycle' ? raw.event === 'monitor-notice-native'
          : raw.event === 'frontend-trace' && raw.where === 'monitor-notice-lifecycle';
        if (!relevant) { report.counts.ignoredRecords++; continue; }
        report.counts.relevantRecords++;
        const meta = kind === 'native-lifecycle' ? NATIVE_META : FRONT_META;
        if (Object.keys(raw).some(key => !meta.has(key) && !(kind === 'native-lifecycle' && TRACE_KEYS.has(key)))) {
          add('REJECTED', 'SENSITIVE_OR_UNKNOWN_ROW_FIELD', at); ctx.invalid = true; continue;
        }
        if (raw.origin !== (kind === 'native-lifecycle' ? 'native-receipt' : 'renderer-observation')) {
          add('REJECTED', 'PRODUCER_ORIGIN_MISSING_OR_INVALID', at); ctx.invalid = true; continue;
        }
        if (!isInt(raw.pid, 1)) { add('MISSING', 'ROW_PID_MISSING', at); ctx.invalid = true; continue; }
        if (ctx.tuple && raw.pid !== ctx.tuple.pid) { add('DIFFERENCE', 'ROW_PID_MISMATCH', at); ctx.invalid = true; continue; }
        if (!isInt(raw.powerGeneration)) { add('MISSING', 'ROW_POWER_GENERATION_MISSING', at); ctx.invalid = true; continue; }
        if (kind === 'frontend-trace' && !isInt(raw.nativeGeneration)) {
          add('MISSING', 'ROW_NATIVE_GENERATION_MISSING', at); ctx.invalid = true; continue;
        }
        if ('nativeGeneration' in raw && (!isInt(raw.nativeGeneration) || ctx.tuple && raw.nativeGeneration !== ctx.tuple.nativeGeneration)) {
          add('DIFFERENCE', 'ROW_NATIVE_GENERATION_MISMATCH', at); ctx.invalid = true; continue;
        }
        if (raw.buildSha256 !== undefined && (!isHash(raw.buildSha256) || ctx.tuple && raw.buildSha256.toLowerCase() !== ctx.tuple.buildSha256)
          || raw.build !== undefined && raw.build !== '' && (!isHash(raw.build) || ctx.tuple && raw.build.toLowerCase() !== ctx.tuple.buildSha256)) {
          add('DIFFERENCE', 'ROW_BUILD_MISMATCH', at); ctx.invalid = true; continue;
        }
        let trace;
        if (kind === 'native-lifecycle') trace = project(Object.fromEntries(Object.entries(raw).filter(([key]) => TRACE_KEYS.has(key))), kind, at);
        else {
          if (raw.lane !== 'event' || raw.messageTruncated === true || raw.traceParseStatus !== undefined && raw.traceParseStatus !== 'parsed') {
            add('MISSING', 'ORDINARY_EVENT_LANE_NOT_CONFIRMED', at); ctx.invalid = true; continue;
          }
          let message;
          if (raw.message !== undefined) {
            if (typeof raw.message !== 'string' || bytes(raw.message) > 4096) { add('REJECTED', 'TRACE_MESSAGE_LIMIT_OR_TYPE', at); ctx.invalid = true; continue; }
            try { message = JSON.parse(raw.message); } catch { add('REJECTED', 'TRACE_MESSAGE_INVALID', at); ctx.invalid = true; continue; }
          }
          trace = project(raw.trace ?? message, kind, at);
          if (trace && message !== undefined) {
            const projectedMessage = project(message, kind, at);
            if (!projectedMessage || canonical(trace) !== canonical(projectedMessage)) {
              add('DIFFERENCE', 'TRACE_MESSAGE_PROJECTION_MISMATCH', at); ctx.invalid = true; continue;
            }
          }
        }
        if (!trace) { ctx.invalid = true; report.counts.rejectedRecords++; continue; }
        const row = { ...at, pg: raw.powerGeneration, trace };
        if (kind === 'frontend-trace') {
          if (!isInt(trace.hostId, 1) || !isInt(trace.epoch)) {
            add('MISSING', 'RENDERER_HOST_OR_EPOCH_MISSING', at); ctx.invalid = true; continue;
          }
          if (trace.powerGeneration !== null && trace.powerGeneration !== undefined && trace.powerGeneration !== row.pg
            && ['healthy', 'ready-accepted', 'frame-confirmed'].includes(trace.stage)) {
            add('DIFFERENCE', 'POWER_GENERATION_DISAGREEMENT', { ...at, hostId: trace.hostId }, POWER); row.powerMismatch = true;
          }
          if (trace.maxAttempts !== 3 || trace.readyTimeoutMs !== 10000) add('MISSING', 'RECOVERY_POLICY_FIELDS_MISSING', at, RECOVERY);
          ctx.frontend.push(row);
        } else ctx.native.push(row);
      }
    }
    let totalHosts = 0, totalOwners = 0;
    for (const ctx of contexts) {
      for (const kind of ['native-lifecycle', 'frontend-trace']) if (!ctx.seenStreams.has(kind)) {
        add('MISSING', 'SOURCE_STREAM_MISSING', { cohortIndex: ctx.index, source: kind }); ctx.invalid = true;
      }
      ctx.view.identityBound = !ctx.invalid;
      if (ctx.invalid) continue; // No partial join after identity/projection rejection.
      const at = { cohortIndex: ctx.index, source: 'frontend-trace' };
      const hosts = new Map(), owners = new Map(), leaseOwners = new Map();
      for (const row of ctx.frontend) {
        const t = row.trace, position = { ...row, hostId: t.hostId, leaseId: t.leaseId, hudId: t.hudId, ownerEpoch: t.ownerEpoch };
        let host = hosts.get(t.hostId);
        if (!host) {
          if (totalHosts >= FIELD_LOG_LIMITS.maxHosts) { add('REJECTED', 'LIMIT_HOSTS', position); ctx.invalid = true; break; }
          totalHosts++;
          host = { intent: 'unknown', epoch: -1, leaseId: null, ownerEpoch: null, rows: [] }; hosts.set(t.hostId, host);
        }
        host.rows.push(row);
        if (t.epoch < host.epoch) add('DIFFERENCE', 'STALE_RENDERER_EPOCH', position, POWER);
        host.epoch = Math.max(host.epoch, t.epoch);
        if (t.stage === 'intent-on') {
          if (host.intent === 'stopped') add('DIFFERENCE', 'INTENT_ON_AFTER_STOP', position, POWER);
          else host.intent = 'on';
        }
        if (t.stage === 'intent-off') host.intent = 'off';
        if (t.stage === 'stop') host.intent = 'stopped';
        if (t.stage === 'reserve-result' && isInt(t.leaseId, 1) && isInt(t.ownerEpoch)) {
          host.leaseId = t.leaseId; host.ownerEpoch = t.ownerEpoch;
        }
        if (['healthy', 'ready-accepted', 'frame-confirmed'].includes(t.stage)) {
          if (host.intent === 'off' || host.intent === 'stopped') { row.healthRejected = 'DIFFERENCE'; add('DIFFERENCE', 'HEALTH_AFTER_OFF_WITHOUT_ON', position, POWER); }
          if (host.intent === 'unknown') { row.healthRejected ??= 'MISSING'; add('MISSING', 'EXPLICIT_ON_NOT_OBSERVED', position, POWER); }
          if (host.leaseId !== null && (t.leaseId !== host.leaseId || t.ownerEpoch !== host.ownerEpoch)) { row.healthRejected = 'DIFFERENCE'; add('DIFFERENCE', 'STALE_OWNER_READY_OR_HEALTH', position, POWER); }
          // ready-accepted is emitted before the producer sets owner.ready=true.
          const readinessObservation = t.stage === 'ready-accepted';
          if (t.enabled !== true || t.ended !== false || t.powerHeld !== false || !readinessObservation && t.ownerReady !== true) {
            const missing = (readinessObservation ? ['enabled', 'ended', 'powerHeld'] : ['enabled', 'ended', 'powerHeld', 'ownerReady']).some(k => typeof t[k] !== 'boolean');
            row.healthRejected = missing ? (row.healthRejected ?? 'MISSING') : 'DIFFERENCE';
            add(missing ? 'MISSING' : 'DIFFERENCE', missing ? 'RENDERER_HEALTH_FLAGS_MISSING' : 'RENDERER_HEALTH_FLAGS_CONTRADICT', position, POWER);
          }
        }
        if (!isInt(t.leaseId, 1) || !isInt(t.hudId, 1)) continue;
        if (!isInt(t.ownerEpoch)) { add('MISSING', 'OWNER_EPOCH_MISSING', position); continue; }
        if (t.ownerEpoch > t.epoch) { row.healthRejected = 'DIFFERENCE'; add('DIFFERENCE', 'OWNER_EPOCH_AHEAD_OF_RENDERER', position, POWER); }
        const key = [t.hostId, t.ownerEpoch, t.leaseId, t.hudId].join('/');
        const previousOwner = leaseOwners.get(t.leaseId);
        if (previousOwner && previousOwner !== key) add('DIFFERENCE', 'LEASE_BOUND_TO_DIFFERENT_RENDERER_OWNER', position);
        leaseOwners.set(t.leaseId, key);
        if (!owners.has(key)) {
          if (totalOwners >= FIELD_LOG_LIMITS.maxOwners) { add('REJECTED', 'LIMIT_OWNERS', position); ctx.invalid = true; break; }
          totalOwners++;
          owners.set(key, { hostId: t.hostId, ownerEpoch: t.ownerEpoch, leaseId: t.leaseId, hudId: t.hudId, rows: [] });
        }
        owners.get(key).rows.push(row);
      }
      if (ctx.invalid) { ctx.view.identityBound = false; continue; }
      for (const owner of owners.values()) {
        const oAt = { ...at, ...owner }; delete oAt.rows;
        const host = hosts.get(owner.hostId), pg = owner.rows.at(-1).pg;
        const nativeLease = ctx.native.filter(r => r.trace.leaseId === owner.leaseId);
        const created = nativeLease.filter(r => r.trace.stage === 'native-created' && r.trace.hudId === owner.hudId);
        const reserved = nativeLease.filter(r => r.trace.stage === 'native-reserved');
        const conflicted = nativeLease.some(r => r.trace.stage === 'native-created' && r.trace.hudId !== owner.hudId)
          || ctx.native.some(r => r.trace.stage === 'native-created' && r.trace.hudId === owner.hudId && r.trace.leaseId !== owner.leaseId);
        const frontendReserved = host.rows.filter(r => r.trace.stage === 'reserve-result' && r.trace.leaseId === owner.leaseId && r.trace.ownerEpoch === owner.ownerEpoch);
        const frontendCreate = owner.rows.filter(r => r.trace.stage === 'create-result' && r.trace.ok === true);
        const ready = ctx.native.filter(r => r.trace.stage === 'native-ready' && r.trace.hudId === owner.hudId && r.pg === pg);
        const oldReady = ctx.native.filter(r => r.trace.stage === 'native-ready' && r.trace.hudId === owner.hudId && r.pg !== pg);
        const healthy = owner.rows.filter(r => ['healthy', 'frame-confirmed'].includes(r.trace.stage) && r.pg === pg && !r.powerMismatch && !r.healthRejected);
        const exits = owner.rows.filter(r => ['close-start', 'close-result', 'retire-result', 'retire-confirmed', 'retire-unconfirmed', 'stop', 'intent-off'].includes(r.trace.stage));
        const exitPg = exits.at(-1)?.pg;
        const retired = nativeLease.filter(r => r.trace.stage === 'native-retired' && r.pg === exitPg);
        const lastRetire = retired.at(-1);
        const retry = host.rows.filter(r => r.pg === pg && ['retry-scheduled', 'retry-fired', 'retry-cancelled', 'retry-exhausted', 'ready-timeout', 'attempt-error'].includes(r.trace.stage));
        const nodes = [];
        function node(id, evidence, missing, difference = null, basis = 'OWNER_AND_SOURCE_TUPLE') {
          for (const code of missing) add('MISSING', code, oAt, id);
          if (difference) add('DIFFERENCE', difference, oAt, id);
          nodes.push({ id, status: difference ? 'DIFFERENCE' : missing.length ? 'UNKNOWN' : 'EVIDENCE_LINKED',
            association: basis, evidenceRefs: refs(evidence), missingCodes: missing });
        }
        node(CREATE, [...reserved, ...created, ...frontendCreate], [
          ...(!reserved.length ? ['NATIVE_RESERVATION_MISSING'] : []),
          ...(!created.length ? ['NATIVE_CREATE_OWNER_BINDING_MISSING'] : []),
          ...(!frontendCreate.length ? ['RENDERER_CREATE_RESULT_MISSING'] : []),
          ...(!frontendReserved.length ? ['RENDERER_RESERVATION_MISSING'] : []),
        ], conflicted ? 'NATIVE_OWNER_BINDING_MISMATCH' : null);
        const missingExit = [];
        let exitDifference = conflicted ? 'NATIVE_OWNER_BINDING_MISMATCH' : null;
        if (!created.length || conflicted) missingExit.push('EXIT_OWNER_BINDING_NOT_CONFIRMED');
        if (!exits.length) missingExit.push('EXIT_NOT_OBSERVED');
        if (!lastRetire) missingExit.push('NATIVE_RETIRE_RECEIPT_MISSING');
        else if (['leaseRetired', 'resourcesDestroyed', 'ownedOnly'].some(k => typeof lastRetire.trace[k] !== 'boolean')) missingExit.push('NATIVE_RETIRE_FIELDS_INCOMPLETE');
        else if (['leaseRetired', 'resourcesDestroyed', 'ownedOnly'].some(k => lastRetire.trace[k] !== true)) exitDifference = 'NATIVE_RETIRE_NOT_CONFIRMED';
        if (lastRetire && lastRetire.trace.obsolete !== false) missingExit.push('NATIVE_RETIRE_OBSOLETE_OR_UNKNOWN');
        node(EXIT, [...exits.slice(-2), ...retired.slice(-2)], missingExit, exitDifference);
        node(POWER, [...healthy, ...ready], [
          ...(!healthy.length ? ['RENDERER_HEALTH_OBSERVATION_MISSING'] : []),
          ...(!ready.length ? [oldReady.length ? 'READY_CURRENT_POWER_MISSING_OLD_READY_PRESENT' : 'NATIVE_READY_RECEIPT_MISSING'] : []),
          ...(owner.rows.some(r => r.powerMismatch) ? ['POWER_GENERATION_NOT_CORROBORATED'] : []),
          ...(!created.length || !reserved.length || conflicted ? ['POWER_OWNER_BINDING_NOT_CONFIRMED'] : []),
        ], owner.rows.some(r => r.healthRejected === 'DIFFERENCE') ? 'RENDERER_HEALTH_EVIDENCE_CONTRADICTED' : null);
        node(RECOVERY, retry, retry.length ? [] : ['RECOVERY_NOT_OBSERVED'], null, 'HOST_AND_POWER_ONLY_NOT_LEASE_CAUSALITY');
        nodes.at(-1).runtimeRecoveryBoundProven = false;
        const nativeOwnerReceipts = ctx.native.filter(r => r.trace.hudId === owner.hudId || r.trace.leaseId === owner.leaseId);
        ctx.view.owners.push({ hostId: owner.hostId, ownerEpoch: owner.ownerEpoch, leaseId: owner.leaseId, hudId: owner.hudId,
          epochsObserved: [...new Set(owner.rows.map(r => r.trace.epoch))], powerGenerationsObserved: [...new Set(owner.rows.map(r => r.pg))],
          lastRendererPowerGeneration: pg, lastNativeOwnerReceiptStage: nativeOwnerReceipts.at(-1)?.trace.stage ?? null,
          nativeReadyReceiptObservedForRendererPower: ready.length > 0, currentNativeHealth: 'UNKNOWN',
          observedRetirementFailures: retired.filter(r => ['leaseRetired', 'resourcesDestroyed', 'ownedOnly'].some(k => r.trace[k] === false)).length,
          nodes });
      }
      if (!owners.size) add('MISSING', 'NO_RENDERER_OWNER_COORDINATES', at);
      ctx.view.nodes = MONITOR_FIELD_LOG_NODES.map(id => {
        const nodes = ctx.view.owners.map(owner => owner.nodes.find(n => n.id === id));
        return { id, status: nodes.some(n => n.status === 'DIFFERENCE') ? 'DIFFERENCE'
          : !nodes.length || nodes.some(n => n.status === 'UNKNOWN') ? 'UNKNOWN' : 'EVIDENCE_LINKED',
          evidenceRefs: nodes.flatMap(n => n.evidenceRefs).slice(0, FIELD_LOG_LIMITS.maxRefsPerNode) };
      });
    }
    return finish();
  } catch {
    add('REJECTED', 'INPUT_DATA_OR_ANALYZER_REJECTED'); return finish();
  }
}