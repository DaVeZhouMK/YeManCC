/**
 * Desktop-only sleep evidence. Existing product tests execute real source with
 * injected transports; replay only interprets native diagnostic records.
 * Host mocks read the explicitly selected current binary with isolated mutable
 * state. No path authorizes OS sleep, app takeover, or process suspension.
 */
import { open, stat } from 'node:fs/promises';
import { getResidencyAdapter } from './sleep-fixtures/residency-entry.mjs';
import { POWER_RESUME_TOOL_SOURCES, powerResumeToolDeclaration } from './sleep-fixtures/power-resume-tool-scope.mjs';
export const SLEEP_SCENARIOS = Object.freeze([
  's0', 's3', 's4', 'screen-off', 'cancel', 'duplicate', 'out-of-order', 'rapid-wake', 'interruption',
]);

const adapter = (id, title, kind, entry, evidenceInputs, options = {}) => ({
  id: `sleep.${id}`, domain: 'sleep', title, kind, path: entry, args: [], timeoutMs: 30000,
  profiles: ['sleep', 'full', 'desktop'], scope: 'behavioral-mock', evidenceInputs, ...options,
});

const LEGACY_SLEEP_INPUTS = [
  'tools/ymcc_sim/domains/sleep/sleep_domain.ps1', 'tools/ymcc_sim/domains/sleep/sleep_forms.json',
  'tools/ymcc_sim/domains/sleep/sleep_types.json', 'tools/ymcc_sim/domains/sleep/lib_sleep_typing.ps1',
  'tools/ymcc_sim/domains/sleep/lib_sleep_typeseries.ps1', 'tools/ymcc_sim/domains/sleep/lib_sleep_rapidwake.ps1',
  'tools/ymcc_sim/lib/sim_core.ps1', 'tools/ymcc_sim/global/host-process.ps1',
  'FanLab/real-host/Program.cs', 'FanLab/real-host/HostProcessLifetime.cs', 'native/main.cpp',
];
const legacyAdapter = (id, title, mode, port, timeoutMs = 120000) => adapter(id, title, 'powershell',
  'tools/ymcc_sim/global/sleep-fixtures/host-entry.ps1', LEGACY_SLEEP_INPUTS.filter(file => mode === 'sleeptypes' || file !== 'native/main.cpp'), {
    referenceInputs: mode === 'sleeptypes' ? [] : ['native/main.cpp'],
    referenceMeaning: 'Native sender lineage only; this HTTP Host mock does not execute native/main.cpp',
    scope: mode === 'sleeptypes' ? 'source-contract' : 'host-mock', timeoutMs,
    profiles: mode === 'sleeptypes' ? ['desktop', 'sleep', 'full'] : ['sleep', 'full'],
    args: ['-Mode', mode, '-EvidenceDir', '{outDir}', '-Port', String(port)],
    resultFiles: ['sleep-host-result.json'], requiresExclusiveHost: mode !== 'sleeptypes',
    requiresPayloadRoot: mode !== 'sleeptypes', blockedExitCodes: [10],
  });

/** Keep missing inputs in the catalog: the runner must report BLOCKED, not omit them. */
export function getSleepAdapters(repoRoot, { identityOnly = false } = {}) {
  return [
    getResidencyAdapter(repoRoot,{identityOnly}),
    // Execute the original cases through a fixture which migrates only stale
    // native symbol/membership assertions; all behavioral expectations remain.
    adapter('power-resume', 'Product wake transaction: ordering, bounded retries and generation fencing',
      'node', 'tools/ymcc_sim/global/sleep-fixtures/power-resume-entry.mjs',
      ['tools/power_resume_selftest.ts', 'src/bridge/powerResume.ts', 'src/App.vue', 'native/main.cpp', ...POWER_RESUME_TOOL_SOURCES],
      { args: ['--output', '{outDir}/power-resume.json'], resultFiles: ['power-resume.json'],
        ...(identityOnly ? {registrationIdentityOnly:true,blockedReason:'POWER_RESUME_EXECUTION_IDENTITY_DEFERRED'} : powerResumeToolDeclaration(repoRoot)) }),
    adapter('steam-dispatch', 'Production Steam sleep dispatch with isolated API transports',
      'node', 'tools/steam_sleep_dispatch_guard_selftest.mjs', ['native/steam_live.js'],
      { args: ['--output', '{outDir}/dispatch.json'], resultFiles: ['dispatch.json'] }),
    adapter('steam-window-activity', 'Production Steam retained-window admission and diagnostic privacy',
      'node', 'tools/steam_sleep_window_activity_selftest.mjs', ['native/steam_live.js'],
      { args: ['--output', '{outDir}/window-activity.json'], resultFiles: ['window-activity.json'] }),
    // This existing test includes a separate policy model. It is a source
    // contract only and must not be counted as executing the native sleep task.
    adapter('task-policy-source', 'Native sleep policy source contract (includes illustrative model)',
      'typescript', 'tools/sleep_task_policy_selftest.ts', ['native/main.cpp'], { scope: 'source-contract' }),
    adapter('replay-and-resume-regression', 'Whitelist replay and actual product rejection regression',
      'node', 'tools/ymcc_sim/global/sleep.test.mjs',
      ['tools/ymcc_sim/global/sleep.mjs', 'src/bridge/powerResume.ts', 'native/main.cpp']),
    legacyAdapter('type-classifier', 'Legacy offline sleep classifier fixtures (not physical type proof)',
      'sleeptypes', 18874, 30000),
    ...['memory','all'].map(mode => adapter(mode === 'memory' ? 'engine-memory' : 'engine-http-all',
      mode === 'memory' ? 'Explicit current Host engine in memory, no server or Main' : 'Real Host HTTP and sleep engine, fresh process per legacy mode without Main',
      'powershell', 'tools/ymcc_sim/global/sleep-engine-entry.ps1', [
        'tools/ymcc_sim/global/sleep-engine-verify.mjs',
        'tools/ymcc_sim/global/consumer/sleep-engine/receipt-audit.mjs',
        'tools/ymcc_sim/global/consumer/sleep-engine/receipt-contract.ps1',
        'tools/ymcc_sim/global/consumer/sleep-engine/Invoke-SleepEngine.ps1',
        'tools/ymcc_sim/global/consumer/sleep-engine/Program.cs',
        'tools/ymcc_sim/global/consumer/sleep-engine/Scenarios.cs',
        'tools/ymcc_sim/global/consumer/sleep-engine/MemoryScenarios.cs',
        'tools/ymcc_sim/global/consumer/sleep-engine/SleepEngineProbe.csproj',
        'tools/ymcc_sim/domains/sleep/sleep_forms.json',
        'tools/ymcc_sim/global/host-process.ps1',
        'FanLab/real-host/Program.cs', 'FanLab/real-host/HostProcessLifetime.cs',
      ], {scope:'host-mock', timeoutMs:300000, requiresExclusiveHost:false, requiresPayloadRoot:true,
        args:['-Mode',mode,'-EvidenceDir','{outDir}/engine'], resultFiles:['engine/sleep-engine-result.json','engine/receipt-audit.json'],
        boundary:'Explicit DLL software consumer; no production Main, no OS power subscription; does not replace exclusive Main startup integration'})),
    legacyAdapter('host-forms', 'Existing 20 forms using the explicit current host with isolated mock state', 'forms', 18874, 180000),
    legacyAdapter('host-typeseries', 'Existing S0/S3/S4 host semantics with mock transports', 'typeseries', 18875),
    legacyAdapter('host-rapidwake', 'Existing rapid wake and repeated generation host cases', 'rapidwake', 18876),
  ];
}

const EVENTS = Object.freeze([
  'power-broadcast', 'kernel-power', 'kernel-power-107', 'kernel-power-506',
  's0-intent-accepted', 's0-intent-joined-existing-suspend', 's0-intent-joined-delayed-resume',
  's0-wake-message', 's4-wake-message', 's0-sleep-retry-canceled-user-wake',
  's4-wake-canceled-sleep-retry', 'suspend-keep-writer', 'pbt-resume-keep-writer',
  'sleep-orphan-recovery-start', 'sleep-orphan-recovery-complete', 'display-idle-state',
]);
const EVENT_SET = new Set(EVENTS);
const DEFAULTS = Object.freeze({ maxBytes: 8 * 1024 * 1024, maxLines: 50000, maxLineBytes: 65536, rapidWakeMs: 5000 });
const BOUNDS = { maxBytes: [1, 64 * 1024 * 1024], maxLines: [1, 200000], maxLineBytes: [1, 1024 * 1024], rapidWakeMs: [0, 60000] };
const isRecord = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const integer = value => Number.isSafeInteger(value) && value >= 0;

function settings(options) {
  if (!isRecord(options) || Object.keys(options).some(key => ![...Object.keys(DEFAULTS), 'requiredScenarios'].includes(key))) {
    throw new TypeError('SLEEP_REPLAY_OPTIONS_INVALID');
  }
  const result = { ...DEFAULTS, ...options };
  for (const [key, [min, max]] of Object.entries(BOUNDS)) {
    if (!Number.isSafeInteger(result[key]) || result[key] < min || result[key] > max) throw new TypeError('SLEEP_REPLAY_OPTIONS_INVALID');
  }
  const required = options.requiredScenarios ?? SLEEP_SCENARIOS;
  if (!Array.isArray(required) || !required.length || required.some(id => !SLEEP_SCENARIOS.includes(id))) {
    throw new TypeError('SLEEP_REPLAY_OPTIONS_INVALID');
  }
  result.requiredScenarios = [...new Set(required)];
  return result;
}

// Native producers use local wall time without an offset. Compare those values
// only inside this file; do not manufacture an absolute UTC event timestamp.
function wallTime(value) {
  if (typeof value !== 'string') return null;
  const match = /^(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})\.(\d{3})$/.exec(value);
  if (!match) return null;
  const [year, month, day, hour, minute, second, ms] = match.slice(1).map(Number);
  if (year < 1970 || year > 9999) return null;
  const epoch = Date.UTC(year, month - 1, day, hour, minute, second, ms);
  const date = new Date(epoch);
  return date.getUTCFullYear() === year && date.getUTCMonth() === month - 1 && date.getUTCDate() === day &&
    date.getUTCHours() === hour && date.getUTCMinutes() === minute && date.getUTCSeconds() === second ? epoch : null;
}

function report(config) {
  return {
    schemaVersion: 1, domain: 'sleep', mode: 'log-replay', scope: 'source-contract',
    status: 'NOT_COVERED', reason: 'NO_RECOGNIZED_SLEEP_RECORDS', inputFormat: 'native-jsonl',
    claims: { productLogicExecuted: false, operatingSystemSleepPerformed: false, physicalHardwareValidated: false,
      installedApplicationExercised: false, fullApplicationValidated: false },
    limits: { ...config },
    counts: { lines: 0, records: 0, recognized: 0, ignored: 0, malformed: 0, invalidFields: 0,
      missingTimestamps: 0, unknownTargetStates: 0, duplicateRecords: 0, joinedNotifications: 0, outOfOrderTimestamps: 0,
      generationRegressions: 0, suspendEdges: 0, resumeEdges: 0, cancelEdges: 0, pairedCycles: 0,
      unpairedResumeEdges: 0, incompleteCycles: 0, rapidCycles: 0 },
    eventCounts: Object.fromEntries(EVENTS.map(event => [event, 0])), matrix: [],
    boundaries: [
      'OBSERVATION_ONLY_NOT_PRODUCT_EXECUTION', 'LOCAL_WALL_CLOCK_NOT_MONOTONIC',
      'S3_NOT_IDENTIFIABLE_FROM_NATIVE_BROADCASTS', 'REQUESTED_MODE_NOT_PHYSICAL_SLEEP_TYPE',
      'LOG_EOF_NOT_PROOF_OF_PROCESS_CRASH', 'NO_RAW_LOG_VALUES_OR_PATHS_EXPORTED',
      'RAPID_WAKE_REQUIRES_KNOWN_COMPATIBLE_GENERATIONS',
      'NO_OS_SLEEP_OR_INSTALLED_HOST_OR_INPUT_VALIDATION',
    ],
  };
}

/**
 * Read an explicitly supplied UTF-8 native sleep-facts/lifecycle JSONL file.
 * Options: maxBytes, maxLines, maxLineBytes, rapidWakeMs, requiredScenarios.
 * Returned data is constructed only from fixed keys/enums and numeric counters:
 * no paths, timestamps, PID/session/token/settings, arbitrary messages, or raw
 * records are copied. Unknown producers are ignored; malformed recognized
 * records fail. S3, OS sleep and full-app acceptance cannot be proved here.
 * Default status stays INCOMPLETE when any matrix item lacks evidence.
 */
export async function replaySleepLog(filePath, options = {}) {
  const config = settings(options), out = report(config), counts = out.counts;
  const observed = Object.fromEntries(SLEEP_SCENARIOS.map(id => [id, 0]));
  let inputFailure = null, text = '', handle;
  try {
    if (typeof filePath !== 'string' || !filePath || filePath.includes('\0')) inputFailure = 'INPUT_PATH_INVALID';
    else {
      const info = await stat(filePath);
      if (!info.isFile()) inputFailure = 'INPUT_NOT_REGULAR_FILE';
      else if (info.size > config.maxBytes) inputFailure = 'INPUT_BYTE_LIMIT_EXCEEDED';
      else {
        handle = await open(filePath, 'r');
        const opened = await handle.stat();
        if (!opened.isFile()) inputFailure = 'INPUT_NOT_REGULAR_FILE';
        else if (opened.size > config.maxBytes) inputFailure = 'INPUT_BYTE_LIMIT_EXCEEDED';
        else {
          // A bounded read also catches growth after stat without loading it.
          const buffer = Buffer.alloc(config.maxBytes + 1);
          let used = 0;
          while (used < buffer.length) {
            const { bytesRead } = await handle.read(buffer, used, buffer.length - used, null);
            if (!bytesRead) break;
            used += bytesRead;
          }
          if (used > config.maxBytes) inputFailure = 'INPUT_BYTE_LIMIT_EXCEEDED';
          else text = new TextDecoder('utf-8', { fatal: true }).decode(buffer.subarray(0, used)).replace(/^\uFEFF/, '');
        }
      }
    }
  } catch { inputFailure = 'INPUT_UNREADABLE_OR_INVALID_UTF8'; }
  finally { if (handle) await handle.close().catch(() => {}); }

  const seen = new Set();
  let lastTime = null, maxGeneration = null, pending = null, wakeBurst = null;
  if (!inputFailure) {
    const lines = text.split(/\r?\n/);
    if (lines.at(-1) === '') lines.pop();
    for (const line of lines) {
      if (++counts.lines > config.maxLines) { inputFailure = 'INPUT_LINE_LIMIT_EXCEEDED'; break; }
      if (Buffer.byteLength(line, 'utf8') > config.maxLineBytes) { inputFailure = 'INPUT_LINE_BYTE_LIMIT_EXCEEDED'; break; }
      if (!line.trim()) continue;
      let row;
      try { row = JSON.parse(line); } catch { counts.malformed++; continue; }
      counts.records++;
      if (!isRecord(row) || typeof row.event !== 'string' || !EVENT_SET.has(row.event)) { counts.ignored++; continue; }
      const event = row.event;
      // sgRecordFact nests details; appendNativeLifecycleLog flattens them.
      const detail = event === 'display-idle-state' ? row : row.details;
      if (!isRecord(detail)) { counts.invalidFields++; continue; }
      const code = event === 'power-broadcast' ? detail.code : null;
      const eventId = event === 'kernel-power' ? detail.eventId : null;
      const hasTargetField = ['kernel-power', 'kernel-power-107'].includes(event);
      const rawTarget = hasTargetField ? detail.targetState ?? null : null;
      // sgKernelPowerEventDataInt emits -1 when TargetState is absent or
      // unavailable. Preserve that unknown; it is never evidence of S3/S4.
      const target = rawTarget === -1 ? null : rawTarget;
      const generation = detail.generation ?? null;
      if ((event === 'power-broadcast' && ![0, 2, 3, 4, 6, 7, 18].includes(code)) ||
          (event === 'kernel-power' && ![107, 506, 507].includes(eventId)) ||
          (target !== null && (!integer(target) || target > 6)) ||
          (generation !== null && !integer(generation))) { counts.invalidFields++; continue; }
      if (event === 'display-idle-state' && detail.source !== 'GUID_SESSION_DISPLAY_STATUS') { counts.ignored++; continue; }
      if (event === 'display-idle-state' && ![0, 1, 2].includes(detail.state)) { counts.invalidFields++; continue; }
      const time = wallTime(row.time);
      if (time === null) counts.missingTimestamps++;
      counts.recognized++;
      if (hasTargetField && target === null) counts.unknownTargetStates++;
      out.eventCounts[event]++;
      // Without a timestamp, identical notifications may be distinct events.
      // Never deduplicate by an untrusted token, PID, path, or free-form field.
      const signature = JSON.stringify([event, time, code, eventId, target, generation, event === 'display-idle-state' ? detail.state : null]);
      if (time !== null && seen.has(signature)) { counts.duplicateRecords++; continue; }
      if (time !== null) seen.add(signature);
      if (time !== null && lastTime !== null && time < lastTime) counts.outOfOrderTimestamps++;
      if (time !== null) lastTime = time;
      if (generation !== null) {
        if (maxGeneration !== null && generation < maxGeneration) counts.generationRegressions++;
        maxGeneration = Math.max(maxGeneration ?? 0, generation);
      }
      // These are producer observations, not a replacement native state machine.
      if (event.startsWith('s0-') || event === 'kernel-power-506' || (event === 'kernel-power' && [506, 507].includes(eventId))) observed.s0++;
      if (event === 's4-wake-message' || ((event === 'kernel-power-107' || (event === 'kernel-power' && eventId === 107)) && target === 5)) observed.s4++;
      if (event.includes('-joined-')) counts.joinedNotifications++;
      if (event === 'display-idle-state' && detail.state === 0) observed['screen-off']++;
      if (event === 'sleep-orphan-recovery-start' || event === 'sleep-orphan-recovery-complete') observed.interruption++;
      if (['s0-sleep-retry-canceled-user-wake', 's4-wake-canceled-sleep-retry'].includes(event)) { counts.cancelEdges++; observed.cancel++; }
      if (event !== 'power-broadcast') continue;
      if (code === 4) {
        counts.suspendEdges++;
        if (pending) counts.incompleteCycles++;
        pending = { time, generation, ordered: counts.outOfOrderTimestamps, regressions: counts.generationRegressions };
        wakeBurst = false;
      } else if ([2, 3].includes(code)) {
        counts.cancelEdges++; observed.cancel++;
        pending = null; wakeBurst = false;
      } else if ([6, 7, 18].includes(code)) {
        counts.resumeEdges++;
        if (pending) {
          // A PBT suspend is logged before native advances its generation.
          // A wake may retain that generation or use the next one; a reset or
          // larger gap cannot establish a cycle across app lifetimes.
          const compatible = pending.generation !== null && generation !== null &&
            generation >= pending.generation && generation - pending.generation <= 1;
          if (compatible) {
            counts.pairedCycles++;
            const duration = time !== null && pending.time !== null ? time - pending.time : null;
            if (duration !== null && duration >= 0 && duration <= config.rapidWakeMs &&
                pending.ordered === counts.outOfOrderTimestamps && pending.regressions === counts.generationRegressions) counts.rapidCycles++;
          } else { counts.incompleteCycles++; counts.unpairedResumeEdges++; }
          pending = null;
        } else if (!wakeBurst || generation === null || generation !== wakeBurst.generation ||
            time === null || wakeBurst.time === null || time < wakeBurst.time || time - wakeBurst.time > 5000) counts.unpairedResumeEdges++;
        // Join only a bounded same-generation automatic+interactive burst.
        wakeBurst = { time, generation };
      }
    }
    if (pending) counts.incompleteCycles++;
  }
  observed.duplicate = counts.duplicateRecords + counts.joinedNotifications;
  observed['out-of-order'] = counts.outOfOrderTimestamps + counts.generationRegressions;
  observed['rapid-wake'] = counts.rapidCycles;
  const reasons = {
    s0: 'NATIVE_S0_MARKERS', s3: 'NATIVE_LOG_CANNOT_PROVE_S3', s4: 'NATIVE_S4_WAKE_MARKERS',
    'screen-off': 'SESSION_DISPLAY_OFF_MARKER', cancel: 'NATIVE_CANCEL_MARKERS',
    duplicate: 'DUPLICATE_OR_JOINED_NOTIFICATIONS', 'out-of-order': 'TIMESTAMP_OR_GENERATION_REGRESSION',
    'rapid-wake': 'PAIRED_BROADCASTS_WITHIN_LOCAL_TIME_THRESHOLD', interruption: 'ORPHAN_RECOVERY_MARKER_NOT_CRASH_PROOF',
  };
  out.matrix = SLEEP_SCENARIOS.map(id => ({ id,
    status: inputFailure ? 'NOT_COVERED' : observed[id] ? 'OBSERVED' : id === 'interruption' && counts.incompleteCycles ? 'INCOMPLETE' : 'NOT_COVERED',
    observations: observed[id], reason: id === 'interruption' && !observed[id] && counts.incompleteCycles ? 'OPEN_SUSPEND_AT_EOF_NOT_CRASH_PROOF' : observed[id] || id === 's3' ? reasons[id] : 'REQUIRED_MARKERS_ABSENT',
  }));
  if (inputFailure || counts.malformed || counts.invalidFields) {
    out.status = 'FAIL'; out.reason = inputFailure ?? 'MALFORMED_OR_INVALID_NATIVE_RECORDS';
  } else if (!counts.recognized) { out.status = 'NOT_COVERED'; }
  else if (counts.incompleteCycles || config.requiredScenarios.some(id => out.matrix.find(item => item.id === id).status !== 'OBSERVED')) {
    out.status = 'INCOMPLETE'; out.reason = 'REQUESTED_MATRIX_OR_CYCLE_EVIDENCE_INCOMPLETE';
  } else { out.status = 'OBSERVED'; out.reason = 'REQUESTED_LOG_MARKERS_OBSERVED_ONLY'; }
  return out;
}

