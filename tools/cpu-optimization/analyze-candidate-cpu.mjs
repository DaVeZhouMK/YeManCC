#!/usr/bin/env node
// Offline only: no process queries, collectors, application or device operations.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

// Output authority is the explicit development evidenceRoot, never a historical round.
const FILES = { identity: 'IDENTITY.json', samples: 'SAMPLES.jsonl', summary: 'SUMMARY.json' };
const FILETIME_EPOCH = 116444736000000000n;
const STARTUP_TICKS = 1200000000n;
const INTERVAL_MS = 250, WINDOW_MS = 1000, MIN_WINDOW_MS = 875, MAX_NEAR_WINDOW_MS = 1125;
const SHA = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const finite = value => typeof value === 'number' && Number.isFinite(value);
const integer = value => Number.isSafeInteger(value);
const numericClose = (a, b) => finite(a) && finite(b) && Math.abs(a - b) <= Math.max(1e-9, Math.abs(a) * 1e-12);

// Preserve integer tokens before JSON.parse can round counters above 2^53.
export function parseLossless(text) {
  return JSON.parse(text.replace(/^\uFEFF/, '').replace(/"(?:\\.|[^"\\])*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/g, token => {
    if (/^-?\d+$/.test(token) && !Number.isSafeInteger(Number(token))) return JSON.stringify(token);
    return token;
  }));
}
function ticks(value, label) {
  if (typeof value === 'number' && integer(value) && value >= 0) return BigInt(value);
  if (typeof value === 'string' && /^\d+$/.test(value)) return BigInt(value);
  throw new Error(`${label} must be a nonnegative exact integer counter`);
}
function utcTicks(value) {
  if (typeof value !== 'string') throw new Error('Missing UTC timestamp');
  const match = /^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,7}))?(Z|[+-]\d\d:\d\d)$/.exec(value);
  if (!match) throw new Error(`Unsupported UTC timestamp: ${value}`);
  const wholeMs = Date.parse(match[1] + match[3]);
  if (!Number.isFinite(wholeMs)) throw new Error(`Invalid UTC timestamp: ${value}`);
  return BigInt(wholeMs) * 10000n + BigInt((match[2] ?? '').padEnd(7, '0')) + FILETIME_EPOCH;
}
function cpuMs(value) {
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error('CPU delta exceeds exact Number conversion range');
  return Number(value) / 10000;
}
function distribution(values) {
  const sorted = values.filter(finite).sort((a, b) => a - b);
  const rank = p => sorted.length ? sorted[Math.max(0, Math.ceil(p * sorted.length) - 1)] : null;
  return { count: sorted.length, min: sorted[0] ?? null, p50: rank(0.5), p95: rank(0.95), p99: rank(0.99), max: sorted.at(-1) ?? null };
}
function endpoint(point) {
  return { sample: point.raw.sample, line: point.line, atUtc: point.raw.atUtc, wallMs: point.raw.wallMs, processAgeSeconds: point.ageMs / 1000 };
}
function delta(from, to, logicalProcessors) {
  const targetTicks = (to.kernel - from.kernel) + (to.user - from.user);
  const samplerTicks = (to.selfKernel - from.selfKernel) + (to.selfUser - from.selfUser);
  const actualWallMs = to.raw.wallMs - from.raw.wallMs;
  const targetCpuMs = cpuMs(targetTicks), samplerCpuMs = cpuMs(samplerTicks);
  return { targetTicks, samplerTicks, actualWallMs, targetCpuMs, samplerCpuMs,
    cpuPercentMachine: targetCpuMs / actualWallMs / logicalProcessors * 100,
    samplerCpuPercentMachine: samplerCpuMs / actualWallMs / logicalProcessors * 100 };
}
function publicWindow(from, to, logicalProcessors, id) {
  const d = delta(from, to, logicalProcessors);
  return { id, continuityChain: from.chain, fromSample: from.raw.sample, toSample: to.raw.sample, fromLine: from.line, toLine: to.line,
    fromUtc: from.raw.atUtc, toUtc: to.raw.atUtc, fromWallMs: from.raw.wallMs, toWallMs: to.raw.wallMs,
    fromProcessAgeSeconds: from.ageMs / 1000, toProcessAgeSeconds: to.ageMs / 1000,
    segment: from.kind === to.kind ? from.kind : 'boundaryCrossing',
    actualWallMs: d.actualWallMs, targetCpu100ns: d.targetTicks.toString(), samplerCpu100ns: d.samplerTicks.toString(),
    targetCpuMs: d.targetCpuMs, samplerCpuMs: d.samplerCpuMs,
    cpuPercentMachine: d.cpuPercentMachine, samplerCpuPercentMachine: d.samplerCpuPercentMachine,
    nearOneSecond: d.actualWallMs >= MIN_WINDOW_MS && d.actualWallMs <= MAX_NEAR_WINDOW_MS };
}
function clustersFor(windows, threshold, pointsByLine, logicalProcessors) {
  const matching = windows.filter(w => w.cpuPercentMachine >= threshold), groups = [];
  for (const win of matching) {
    const last = groups.at(-1);
    // Strict overlap: merely touching intervals stay separate.
    if (last && win.continuityChain === last.continuityChain && win.fromWallMs < last.toWallMs && win.fromLine >= last.fromLine) {
      last.windowIds.push(win.id);
      if (win.toWallMs > last.toWallMs) { last.toWallMs = win.toWallMs; last.toLine = win.toLine; }
      if (win.cpuPercentMachine > last.peakWindow.cpuPercentMachine) last.peakWindow = win;
    } else groups.push({ continuityChain: win.continuityChain, fromLine: win.fromLine, toLine: win.toLine, toWallMs: win.toWallMs,
      fromWallMs: win.fromWallMs, windowIds: [win.id], peakWindow: win });
  }
  const clusters = groups.map((group, index) => {
    const from = pointsByLine.get(group.fromLine), to = pointsByLine.get(group.toLine);
    const union = publicWindow(from, to, logicalProcessors, `cluster-${index + 1}`);
    return { cluster: index + 1, windowCount: group.windowIds.length, windowIds: group.windowIds,
      from: endpoint(from), to: endpoint(to), actualWallMs: union.actualWallMs,
      unionTargetCpu100ns: union.targetCpu100ns, unionTargetCpuMs: union.targetCpuMs,
      unionSamplerCpuMs: union.samplerCpuMs, unionAverageCpuPercentMachine: union.cpuPercentMachine,
      peakWindow: group.peakWindow };
  });
  return { thresholdPercent: threshold, comparison: '>=', windowCount: matching.length,
    windowIds: matching.map(w => w.id), clusterCount: clusters.length,
    unionWallMs: clusters.reduce((sum, c) => sum + c.actualWallMs, 0),
    unionTargetCpuMs: clusters.reduce((sum, c) => sum + c.unionTargetCpuMs, 0), clusters,
    interpretation: 'Strict overlap, transitive merge, touching separate. Union counters counted once; clusters are window geometry, not independent causes or instantaneous bursts.' };
}

export function analyzeRawBuffers(buffers, metadata = {}) {
  const identity = parseLossless(buffers.identity.toString('utf8'));
  const summary = parseLossless(buffers.summary.toString('utf8'));
  const text = buffers.samples.toString('utf8').replace(/^\uFEFF/, ''), lines = text.split(/\r?\n/);
  const issues = [], comparisons = [];
  const add = (code, detail, severity = 'error') => issues.push({ code, severity, ...detail });
  const compare = (name, computed, recorded) => {
    const match = finite(computed) && finite(recorded) ? numericClose(computed, recorded) : computed === recorded;
    comparisons.push({ name, recomputed: computed ?? null, recorded: recorded ?? null, match });
    if (!match) add('SUMMARY_MISMATCH', { name, recomputed: computed ?? null, recorded: recorded ?? null });
  };
  let birth = null;
  try { birth = ticks(identity.birthFileTimeUtc100ns, 'identity birth'); }
  catch (error) { add('INVALID_IDENTITY_BIRTH', { message: error.message }); }
  try { if (birth !== null && utcTicks(identity.birthUtc) !== birth) add('IDENTITY_BIRTH_UTC_MISMATCH', {}); }
  catch (error) { add('INVALID_BIRTH_UTC', { message: error.message }); }
  const logical = identity.logicalProcessors;
  if (!integer(logical) || logical <= 0) add('INVALID_MACHINE_LOGICAL_PROCESSORS', { recorded: logical ?? null });
  if (summary.logicalProcessors !== logical) add('MACHINE_LOGICAL_PROCESSORS_MISMATCH', { identity: logical ?? null, summary: summary.logicalProcessors ?? null });
  if (!integer(identity.pid) || identity.pid <= 0) add('INVALID_ROOT_PID', { recorded: identity.pid ?? null });
  if (summary.pid !== identity.pid || summary.birthFileTimeUtc100ns !== identity.birthFileTimeUtc100ns) add('SUMMARY_ROOT_IDENTITY_MISMATCH', {});
  const hashes = ['exeSha256', 'expectedExeSha256'].map(k => identity[k]);
  const allHashes = [...hashes, summary.exeSha256, summary.expectedExeSha256, summary.finalDiskExeSha256];
  const hashesMatch = typeof hashes[0] === 'string' && allHashes.every(h => typeof h === 'string' && /^[a-fA-F0-9]{64}$/.test(h) && h.toLowerCase() === hashes[0]?.toLowerCase());
  if (!hashesMatch) add('EXE_HASH_BINDING_MISSING_OR_DIFFERENT', {});
  const rootScopeVerified = summary.rootOnly === true && summary.webViewOrChildCpuIncluded === false && identity.queryLimitedHandleHeldUntilEnd === true && identity.status === 'HELD_ROOT_PID_BIRTH_DISK_HASH_BOUND';
  if (!rootScopeVerified) add('ROOT_ONLY_SCOPE_OR_HELD_HANDLE_UNCONFIRMED', {}, 'unknown');
  const points = [], sampleIntervals = [], clockDifferences = [], samplingGaps = [], blankLineNumbers = [];
  const birthFlags = {}, pidValues = new Set(), birthValues = new Set();
  let previous = null, chain = 0, parsedRows = 0, dataLines = 0;
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].trim()) {
      if (i < lines.length - 1) { blankLineNumbers.push(i + 1); add('BLANK_DATA_LINE', { line: i + 1 }); previous = null; chain++; }
      continue;
    }
    dataLines++;
    let raw;
    try { raw = parseLossless(lines[i]); parsedRows++; }
    catch (error) { add('UNPARSEABLE_SAMPLE', { line: i + 1, message: error.message }); previous = null; chain++; continue; }
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) { add('INVALID_SAMPLE_OBJECT', { line: i + 1 }); previous = null; chain++; continue; }
    pidValues.add(raw.pid ?? null); birthValues.add(raw.birthFileTimeUtc100ns ?? null);
    const flag = String(raw.birthVerified); birthFlags[flag] = (birthFlags[flag] ?? 0) + 1;
    let point;
    try {
      if (birth === null || !integer(logical) || logical <= 0) throw new Error('No valid birth / machine processor denominator');
      if (raw.pid !== identity.pid || ticks(raw.birthFileTimeUtc100ns, 'sample birth') !== birth || raw.birthVerified !== true) throw new Error('Sample root PID / birth / birthVerified mismatch');
      if (!integer(raw.sample) || raw.sample <= 0 || !finite(raw.wallMs) || raw.wallMs < 0) throw new Error('Invalid sample number / monotonic wallMs');
      const atTicks = utcTicks(raw.atUtc);
      if (atTicks < birth) throw new Error('Sample timestamp precedes process birth');
      point = { raw, line: i + 1, atTicks, ageMs: Number(atTicks - birth) / 10000,
        kind: atTicks - birth < STARTUP_TICKS ? 'startup' : 'stable',
        kernel: ticks(raw.kernel100ns, 'kernel100ns'), user: ticks(raw.user100ns, 'user100ns'),
        selfKernel: ticks(raw.samplerKernel100ns, 'samplerKernel100ns'), selfUser: ticks(raw.samplerUser100ns, 'samplerUser100ns') };
    } catch (error) { add('INVALID_SAMPLE', { line: i + 1, sample: raw.sample ?? null, message: error.message }); previous = null; chain++; continue; }
    if (previous) {
      const wall = point.raw.wallMs - previous.raw.wallMs, utcWall = Number(point.atTicks - previous.atTicks) / 10000;
      sampleIntervals.push(wall); clockDifferences.push(utcWall - wall);
      let broken = false;
      if (point.raw.sample !== previous.raw.sample + 1) { add('NONCONTIGUOUS_SAMPLE_NUMBERS', { from: endpoint(previous), to: endpoint(point) }); broken = true; }
      if (!(wall > 0) || !(utcWall > 0)) { add('NONINCREASING_WALL_OR_UTC', { from: endpoint(previous), to: endpoint(point) }); broken = true; }
      if (['kernel', 'user', 'selfKernel', 'selfUser'].some(k => point[k] < previous[k])) { add('NEGATIVE_CPU_COUNTER_DELTA', { from: endpoint(previous), to: endpoint(point) }); broken = true; }
      if (wall > INTERVAL_MS * 1.5) samplingGaps.push({ from: endpoint(previous), to: endpoint(point), actualWallMs: wall });
      if (broken) chain++;
    } else if (!points.length && raw.sample !== 1) add('INITIAL_SAMPLES_MISSING', { firstSample: raw.sample });
    point.chain = chain; points.push(point); previous = point;
  }
  const newlineTerminated = /\n$/.test(text);
  if (!newlineTerminated && text.length) add('UNTERMINATED_JSONL_TAIL', {}, 'unknown');
  if (samplingGaps.length) add('SAMPLING_GAPS_ABOVE_375MS', { count: samplingGaps.length }, 'unknown');
  if (clockDifferences.some(d => Math.abs(d) > INTERVAL_MS)) add('UTC_VS_MONOTONIC_CLOCK_DISAGREEMENT', { maximumAbsoluteMs: Math.max(...clockDifferences.map(Math.abs)) }, 'unknown');
  if (!points.length) add('NO_VALID_SAMPLES', {});
  const pointsByLine = new Map(points.map(p => [p.line, p])), intervals = [], windows = [];
  const windowComparisons = { rowsCompared: 0, maximumAbsoluteNumericError: 0, mismatchCount: 0 };
  let queue = [], queueBase = 0;
  for (let i = 0; i < points.length; i++) {
    const point = points[i], prev = points[i - 1];
    if (!prev || prev.chain !== point.chain) { queue = []; queueBase = 0; }
    else intervals.push({ from: prev, to: point, segment: prev.kind === point.kind ? point.kind : 'boundaryCrossing', ...delta(prev, point, logical) });
    queue.push(point);
    while (queueBase + 1 < queue.length && Math.abs((point.raw.wallMs - queue[queueBase + 1].raw.wallMs) - WINDOW_MS) <= Math.abs((point.raw.wallMs - queue[queueBase].raw.wallMs) - WINDOW_MS)) queueBase++;
    const from = queue[queueBase];
    let computed = null;
    if (point.raw.wallMs - from.raw.wallMs >= MIN_WINDOW_MS) { computed = publicWindow(from, point, logical, windows.length + 1); windows.push(computed); }
    const recorded = point.raw.window;
    let matches = (computed === null) === (recorded == null);
    if (computed && recorded) {
      windowComparisons.rowsCompared++;
      for (const name of ['actualWallMs', 'targetCpuMs', 'cpuPercentMachine', 'samplerCpuMs', 'samplerCpuPercentMachine']) {
        if (finite(recorded[name])) windowComparisons.maximumAbsoluteNumericError = Math.max(windowComparisons.maximumAbsoluteNumericError, Math.abs(computed[name] - recorded[name]));
        matches = numericClose(computed[name], recorded[name]) && matches;
      }
      matches = recorded.fromUtc === computed.fromUtc && recorded.toUtc === computed.toUtc && recorded.atOrAbove01Percent === (computed.cpuPercentMachine >= 0.1) && matches;
    }
    if (!matches) { windowComparisons.mismatchCount++; add('RECORDED_WINDOW_MISMATCH', { line: point.line, sample: point.raw.sample }); }
  }
  function segment(name) {
    const selectedPoints = name === 'all' ? points : points.filter(p => p.kind === name);
    const selectedIntervals = name === 'all' ? intervals : intervals.filter(v => v.segment === name);
    const selectedWindows = name === 'all' ? windows : windows.filter(w => w.segment === name);
    const actualWallMs = selectedIntervals.reduce((s, v) => s + v.actualWallMs, 0);
    const targetTicks = selectedIntervals.reduce((s, v) => s + v.targetTicks, 0n);
    const samplerTicks = selectedIntervals.reduce((s, v) => s + v.samplerTicks, 0n);
    const targetCpuMs = cpuMs(targetTicks), samplerCpuMs = cpuMs(samplerTicks);
    const peak = selectedWindows.reduce((max, w) => !max || w.cpuPercentMachine > max.cpuPercentMachine ? w : max, null);
    const samplerPeak = selectedWindows.reduce((max, w) => !max || w.samplerCpuPercentMachine > max.samplerCpuPercentMachine ? w : max, null);
    return { name, available: selectedPoints.length > 0, sampleCount: selectedPoints.length, adjacentIntervalCount: selectedIntervals.length,
      first: selectedPoints.length ? endpoint(selectedPoints[0]) : null,
      last: selectedPoints.length ? endpoint(selectedPoints.at(-1)) : null,
      actualCounterCoveredWallMs: actualWallMs,
      endpointSpanWallMs: selectedPoints.length > 1 ? selectedPoints.at(-1).raw.wallMs - selectedPoints[0].raw.wallMs : 0,
      targetCpu100ns: targetTicks.toString(), targetCpuMs,
      averageCpuPercentMachine: actualWallMs > 0 ? targetCpuMs / actualWallMs / logical * 100 : null,
      sliding: { windowCount: selectedWindows.length, cpuPercentMachine: distribution(selectedWindows.map(w => w.cpuPercentMachine)),
        actualWallMs: distribution(selectedWindows.map(w => w.actualWallMs)), outside875To1125msWindowIds: selectedWindows.filter(w => !w.nearOneSecond).map(w => w.id),
        maximumWindow: peak,
        windowsStrictlyAbove01: selectedWindows.filter(w => w.cpuPercentMachine > 0.1).length,
        windowsStrictlyAbove02: selectedWindows.filter(w => w.cpuPercentMachine > 0.2).length,
        thresholds: [0.1, 0.2].map(t => clustersFor(selectedWindows, t, pointsByLine, logical)) },
      sampler: { cpu100ns: samplerTicks.toString(), cpuMs: samplerCpuMs,
        averageCpuPercentMachine: actualWallMs > 0 ? samplerCpuMs / actualWallMs / logical * 100 : null,
        slidingCpuPercentMachine: distribution(selectedWindows.map(w => w.samplerCpuPercentMachine)), maximumWindow: samplerPeak },
      numericPeakLimitMetWithinObservedWindows: peak ? peak.cpuPercentMachine <= 0.2 : null,
      averageWithinResidentRange: actualWallMs > 0 ? targetCpuMs / actualWallMs / logical * 100 <= 0.1 : null,
      everyObservedWindowWithinResidentRange: peak ? peak.cpuPercentMachine <= 0.1 : null };
  }
  const segments = { all: segment('all'), startup: segment('startup'), stable: segment('stable') }, all = segments.all;
  compare('samples', parsedRows, summary.samples);
  compare('slidingWindows', windows.length, summary.slidingWindows);
  compare('coveredActualWallMs', all.actualCounterCoveredWallMs, summary.coveredActualWallMs);
  compare('targetTotalCpuMs', all.targetCpuMs, summary.targetTotalCpuMs);
  compare('targetAverageCpuPercentMachine', all.averageCpuPercentMachine, summary.targetAverageCpuPercentMachine);
  compare('samplerSteadyCpuMs', all.sampler.cpuMs, summary.samplerSteadyCpuMs);
  compare('samplerSteadyAverageCpuPercentMachine', all.sampler.averageCpuPercentMachine, summary.samplerSteadyAverageCpuPercentMachine);
  compare('maxCpuPercentMachine', all.sliding.maximumWindow?.cpuPercentMachine ?? null, summary.maxCpuPercentMachine);
  compare('windowsAtOrAbove01Percent', all.sliding.thresholds[0].windowCount, summary.windowsAtOrAbove01Percent);
  if (summary.slidingWindowThresholdPercent !== 0.1) add('UNKNOWN_RECORDED_THRESHOLD_CONTRACT', { recorded: summary.slidingWindowThresholdPercent ?? null });
  const finalLivenessConfirmed = summary.finalNativeBirthVerified === true && summary.finalNativeLivenessVerified === true && summary.finalNativeWaitStatus === 258 && summary.livenessAuthority === 'HELD_SYNCHRONIZABLE_HANDLE_WAIT' && summary.finalNativeIdentityError === null;
  if (!finalLivenessConfirmed) add('FINAL_LIVENESS_NOT_PROVEN_BY_HELD_HANDLE_WAIT', { waitStatus: summary.finalNativeWaitStatus ?? null, legacyExitTimeIsNotAliveProof: true }, 'unknown');
  const reportedMissed = summary.missed250msSlots;
  if (!integer(reportedMissed) || reportedMissed < 0) add('MISSED_SLOT_COUNT_UNKNOWN', { raw: reportedMissed ?? null }, 'unknown');
  else if (reportedMissed > 0) add('MISSED_SAMPLING_SLOTS', { count: reportedMissed }, 'unknown');
  const allErrors = issues.filter(v => v.severity === 'error'), unknowns = issues.filter(v => v.severity === 'unknown');
  const conditions = {
    rawCountersIdentityAndSummaryConsistent: allErrors.length === 0,
    stableObservedPeakAtOrBelow02Percent: allErrors.length === 0 && segments.stable.numericPeakLimitMetWithinObservedWindows === true,
    rootOnlyScopeAndHeldHandleConfirmed: rootScopeVerified,
    diskHashBindingConsistent: hashesMatch,
    collectorReportedDurationComplete: summary.status === 'DURATION_COMPLETE_OBSERVATION_ONLY' && summary.failure === null,
    finalBirthAndLivenessConfirmed: finalLivenessConfirmed,
    requestedAtLeast480Seconds: finite(summary.durationRequestedSeconds) && summary.durationRequestedSeconds >= 480,
    stableCounterCoverageAtLeast360Seconds: segments.stable.actualCounterCoveredWallMs >= 360000,
    stableHasSlidingWindows: segments.stable.sliding.windowCount > 0,
    reportedMissedSlotsKnownZero: reportedMissed === 0,
    noDetectedSamplingGapsOrUnknownData: unknowns.length === 0 && samplingGaps.length === 0,
    allSlidingWindowsWithin875To1125ms: windows.every(w => w.nearOneSecond),
    scenarioBackgroundFanOffVirtualControllerOffVerified: false
  };
  const blockers = Object.entries(conditions).filter(([, value]) => value !== true).map(([name]) => name);
  const boundaryIntervals = intervals.filter(v => v.segment === 'boundaryCrossing'), stablePeak = segments.stable.sliding.maximumWindow;
  return {
    schemaVersion: 1, analyzer: 'candidate-cpu-offline-v1', generatedAtUtc: new Date().toISOString(),
    status: allErrors.length ? 'RAW_DATA_OR_IDENTITY_INVALID_PARTIAL_OFFLINE_ANALYSIS' : stablePeak && stablePeak.cpuPercentMachine > 0.2 ? 'OBSERVED_STABLE_WINDOW_PEAK_ABOVE_02_PERCENT' : 'OFFLINE_MEASUREMENTS_ONLY_ACCEPTANCE_NOT_ESTABLISHED',
    candidateCpuAcceptance: false,
    provenance: { ...metadata, inputs: Object.entries(FILES).map(([key, filename]) => ({ filename, bytes: buffers[key].length, sha256: SHA(buffers[key]) })) },
    formulas: { targetPercent: '100 * ((deltaKernel100ns + deltaUser100ns) / 10000) / actualMonotonicWallMs / allMachineLogicalProcessors',
      samplerPercent: 'Same formula using samplerKernel100ns and samplerUser100ns; sampler CPU reported separately, never subtracted from target CPU.',
      integerPrecision: 'JSON integer tokens above 2^53 kept as strings; cumulative counters and differences use BigInt.',
      overallMean: 'Sum of verified adjacent-interval CPU deltas / sum of actual wall durations / logical processors * 100; not the average of overlapping percentages.',
      age: 'Exact UTC sample timestamp (100ns) minus bound native birth FILETIME; first120s=[0,120), stable=[120,infinity).',
      segmentation: 'Both endpoints in the same age segment. No interpolation at age120. Crossing adjacent intervals/windows remain in all, excluded from both separate segments.',
      sliding: 'Original nearest-1000ms baseline queue, advance on <= distance ties, emit at >=875ms. Actual duration retained; durations >1125ms flagged.',
      percentiles: 'Unweighted nearest rank sorted[ceil(p*n)-1]; overlapping windows are dependent, not independent trials.',
      clusters: 'Strictly overlapping qualifying windows merged transitively. Touching windows stay separate; endpoint union counters counted once.' },
    raw: { identity, summary },
    identityAndLiveness: { observedPids: [...pidValues], observedBirthFileTimes: [...birthValues], birthVerifiedCounts: birthFlags,
      rootScopeVerifiedFromRaw: rootScopeVerified, finalBirthAndLivenessConfirmedFromRaw: finalLivenessConfirmed, finalLivenessAuthority: summary.livenessAuthority ?? null, rawExitTimeIsNotAliveProof: true,
      liveNow: 'UNKNOWN: no live process queries performed',
      scope: 'Selected native root only. WebView / child processes excluded exactly as recorded; missing/contradictory scope not promoted to confirmed.' },
    coverage: { dataLines, parsedRows, validRows: points.length, newlineTerminated, blankLineNumbers,
      requestedDurationSeconds: summary.durationRequestedSeconds ?? null,
      actualCounterCoveredWallMs: all.actualCounterCoveredWallMs,
      reportedCoveredActualWallMs: summary.coveredActualWallMs ?? null,
      sampleIntervalWallMs: distribution(sampleIntervals), reportedMissed250msSlots: reportedMissed ?? null,
      missedSlotsInterpretation: 'Retained collector count. Exact missed slots cannot be recomputed from timestamps alone: after-work scheduling and the final unrecorded iteration also matter.',
      samplingGapsAbove375ms: samplingGaps, utcMinusMonotonicIntervalMs: distribution(clockDifferences),
      stableRequiredWallMs: 360000, stableActualCounterCoveredWallMs: segments.stable.actualCounterCoveredWallMs,
      stableDeficitWallMs: Math.max(0, 360000 - segments.stable.actualCounterCoveredWallMs),
      boundaryExcludedFromSeparateSegments: { adjacentIntervalCount: boundaryIntervals.length,
        actualWallMs: boundaryIntervals.reduce((s, v) => s + v.actualWallMs, 0),
        targetCpuMs: boundaryIntervals.reduce((s, v) => s + v.targetCpuMs, 0),
        slidingWindowIds: windows.filter(w => w.segment === 'boundaryCrossing').map(w => w.id) } },
    segments, windows,
    sampler: { steadyCounterScope: 'Same actual endpoints as target. CPU costs charged outside sampler process unavailable.',
      wholeProcessCpuMsRecorded: summary.samplerWholeProcessCpuMs ?? null,
      wholeWallMsRecorded: summary.samplerWholeWallMs ?? null,
      wholeProcessAverageCpuPercentMachine: finite(summary.samplerWholeProcessCpuMs) && finite(summary.samplerWholeWallMs) && summary.samplerWholeWallMs > 0 && integer(logical) && logical > 0 ? summary.samplerWholeProcessCpuMs / summary.samplerWholeWallMs / logical * 100 : null,
      wholeScopeRecorded: summary.samplerCpuScope ?? null,
      iterationWallCostRecorded: summary.iterationWallCost ?? null, nativeQueriesWallCostRecorded: summary.nativeQueriesWallCost ?? null,
      nativeQueriesWallCostRecomputed: distribution(points.map(p => p.raw.nativeQueriesWallMs)),
      observerEffectOrCpuChargedElsewhere: 'UNKNOWN; these counters do not establish zero perturbation or causation.' },
    validation: { errors: allErrors.length, unknowns: unknowns.length, issues, summaryComparisons: comparisons, windowComparisons },
    evaluation: { targetPeakPercentMachine: 0.2, desiredResidentMaximumPercentMachine: 0.1,
      stableObservedPeakPercentMachine: stablePeak?.cpuPercentMachine ?? null,
      stableObservedNumericalPeakLimitMet: allErrors.length || !rootScopeVerified ? null : segments.stable.numericPeakLimitMetWithinObservedWindows,
      conditions, blockers, completeR6Acceptance: false,
      scenario: { independentlyVerified: false,
        rawIdentityScenarioFlag: identity.scenarioVisibilityOrInstallAutomaticallyVerified ?? null,
        rawSummaryScenarioFlag: summary.scenarioBackgroundInstallOrUserStateAutomaticallyVerified ?? null,
        backgroundVisibility: 'UNKNOWN from these files', fanInactive: 'UNKNOWN from these files', virtualControllerInactive: 'UNKNOWN from these files' },
      interpretation: 'A mean or a peak within the limit is descriptive. Missing stable360s, lost samples, failed identity/liveness, unknown scenario or finite measurement scope cannot become complete acceptance.' },
    limitations: [
      'Maxima are averages over recorded approximately-one-second windows, never instantaneous or sub-sample maxima; finite duration does not bound future peaks.',
      'Native root only. Child/WebView CPU excluded; no application-wide CPU claim.',
      'Wall-clock UTC determines birth age; Stopwatch wallMs determines CPU denominator. Clock discrepancies remain visible.',
      'No zero substitution for missing/invalid endpoints. Counter/identity discontinuities break analysis chains.',
      'Finite CPU-counter granularity and non-atomic target/sampler reads can move apparent work into neighboring intervals.',
      'Startup means process age below120s, not observer first120s. A run started after age120 has no startup evidence.',
      'Sampler whole-process counters retain original scope; compiler children, some final serialization and other charged CPU excluded.',
      'No ETW/stack, source causation, timer attribution, fan/controller state, foreground/background state, or improvement against another build proved.',
      'Original inputs are read-only and hash pinned. This tool performs no sampling, application lifecycle or device operations.'
    ]
  };
}

// ROG counters use per-process raw QPC endpoints, not the legacy observer wallMs.
// Keep the two input contracts separate; neither path performs live operations.
export function analyzeRogBuffers(buffers, metadata = {}) {
  const identity = parseLossless(buffers.identity.toString('utf8'));
  const summary = parseLossless(buffers.summary.toString('utf8'));
  if (identity?.schemaVersion !== 1 || identity.tool !== 'Observe-RogCpu' || summary?.schemaVersion !== 1) throw new Error('Unsupported rog-field identity/summary schema');
  const candidate = buffers.candidate ? parseLossless(buffers.candidate.toString('utf8')) : null;
  if (candidate && !['nativeExeSha256', 'hostExeSha256', 'hostDllSha256', 'sdkSha256', 'buildId', 'exportSourceTreeSha256'].every(k => typeof candidate[k] === 'string' && /^[a-f\d]{64}$/i.test(candidate[k]))) throw new Error('Invalid candidate identity reference');
  const issues = [], comparisons = [];
  const add = (code, detail = {}, severity = 'error') => issues.push({ code, severity, ...detail });
  const compare = (name, computed, recorded, detail = {}) => {
    const match = finite(computed) && finite(recorded) ? numericClose(computed, recorded) : computed === recorded;
    comparisons.push({ name, ...detail, computed: computed ?? null, recorded: recorded ?? null, match });
    if (!match) add('ROG_RECORDED_VALUE_MISMATCH', { name, ...detail, computed: computed ?? null, recorded: recorded ?? null });
    return match;
  };
  const samePath = (a, b) => typeof a === 'string' && typeof b === 'string' && path.win32.isAbsolute(a) && path.win32.isAbsolute(b) && path.win32.normalize(a).toLowerCase() === path.win32.normalize(b).toLowerCase();
  const hash = v => typeof v === 'string' && /^[a-f\d]{64}$/i.test(v);
  const sameHash = (a, b) => hash(a) && hash(b) && a.toLowerCase() === b.toLowerCase();
  const frequency = ticks(identity.qpcFrequency, 'QPC frequency'), logical = identity.logicalProcessors;
  if (frequency <= 0n || frequency > BigInt(Number.MAX_SAFE_INTEGER) || !integer(logical) || logical <= 0) throw new Error('Missing positive QPC frequency or machine processor denominator');
  compare('qpcFrequency', frequency.toString(), String(summary.qpcFrequency));
  compare('logicalProcessors', logical, summary.logicalProcessors);
  compare('scenario', identity.scenario, summary.scenario);
  compare('intervalMs', identity.parameters?.intervalMs, summary.intervalMs);
  if (!integer(summary.intervalMs) || summary.intervalMs < 100 || summary.intervalMs > 1000) throw new Error('Invalid requested interval');
  if (identity.logicalProcessorCount?.selected !== logical || identity.logicalProcessorCount?.inconsistent !== false) add('MACHINE_PROCESSOR_COUNT_UNCONFIRMED', {}, 'unknown');
  if (identity.status !== summary.status || summary.status !== 'COMPLETE' || summary.completedDeadline !== true) add('COLLECTION_PARTIAL_OR_INCOMPLETE', { identityStatus: identity.status, summaryStatus: summary.status }, 'unknown');
  const bindings = [...(Array.isArray(identity.targets) ? identity.targets : []), ...(identity.sampler ? [identity.sampler] : [])];
  const states = new Map();
  const readPoint = (raw, birth, index, line) => {
    if (!raw || raw.Status !== 'OK' || raw.TimesSucceeded !== true || raw.CyclesSucceeded !== true || raw.LiveBeforeAndAfter !== true || String(raw.WaitBefore) !== '258' || String(raw.WaitAfter) !== '258' || raw.Error !== null) throw new Error('Raw read lacks successful counters and held-handle liveness');
    if (ticks(raw.CreationFileTime100ns, 'raw birth') !== birth) throw new Error('Raw birth differs from bound process');
    const before = ticks(raw.QpcBefore, 'QPC before'), after = ticks(raw.QpcAfter, 'QPC after'), qpc = ticks(raw.QpcMidpoint, 'QPC midpoint');
    if (after < before || qpc !== before + (after - before) / 2n) throw new Error('Invalid QPC bounds/midpoint');
    const utcBefore = utcTicks(raw.UtcBefore), utcAfter = utcTicks(raw.UtcAfter);
    if (utcBefore < birth || utcAfter < utcBefore) throw new Error('Invalid raw UTC range or process age');
    return { index, line, before, after, qpc, utcBefore, utcAfter, fromUtc: raw.UtcBefore, toUtc: raw.UtcAfter,
      ageMs: Number(utcBefore - birth) / 10000,
      kind: utcBefore - birth < STARTUP_TICKS ? 'startup' : 'stable',
      kernel: ticks(raw.Kernel100ns, 'kernel'), user: ticks(raw.User100ns, 'user'), cycles: ticks(raw.Cycles, 'cycles') };
  };
  const difference = (a, b) => {
    const qpc = b.qpc - a.qpc, kernel = b.kernel - a.kernel, user = b.user - a.user, cycles = b.cycles - a.cycles;
    if (qpc <= 0n || qpc > BigInt(Number.MAX_SAFE_INTEGER) || kernel < 0n || user < 0n || cycles < 0n) throw new Error('Non-positive QPC or decreasing cumulative counters');
    const actualWallMs = Number(qpc) / Number(frequency) * 1000, targetCpuMs = cpuMs(kernel + user);
    return { qpc, kernel, user, cycles, cpu: kernel + user, actualWallMs, targetCpuMs, cpuPercentMachine: 100 * targetCpuMs / actualWallMs / logical };
  };
  for (const b of bindings) {
    const key = `${b.role}:${b.pid}`;
    if (!['root', 'host', 'sampler'].includes(b.role) || !integer(b.pid) || b.pid <= 0 || states.has(key) || [...states.values()].some(s => s.binding.pid === b.pid)) throw new Error('Invalid/duplicate bound PID or role');
    let birth = null;
    try {
      birth = ticks(b.birthFileTime100ns, 'bound birth');
      if (birth === 0n || (b.birthUtc && utcTicks(b.birthUtc) !== birth)) throw new Error('Invalid bound birth UTC');
      if (b.heldAccessMask !== '0x00101000' || (b.role !== 'sampler' && (b.status !== 'BOUND' || b.queryAndSynchronizeHandleHeld !== true))) throw new Error('Binding lacks held query/synchronize authority');
      readPoint(b.rawAtBind, birth, null, null);
      if (b.role !== 'sampler') readPoint(b.rawAfterHash, birth, null, null);
      if (!hash(b.diskSha256) || !path.win32.isAbsolute(b.imagePath)) throw new Error('Missing disk hash/full image identity');
    } catch (error) { add('INVALID_ROG_BINDING', { key, message: error.message }); birth = null; }
    states.set(key, { binding: b, birth, previous: null, queue: [], base: 0, chain: 0, points: [], intervals: [], collectorIntervals: [], windows: [], gaps: [], invalidReads: 0 });
  }
  const roots = [...states.values()].filter(s => s.binding.role === 'root');
  const pinnedRootPid = identity.parameters?.expectedRootProcessId ?? null, pinnedRootBirth = identity.parameters?.expectedRootBirthFileTime100ns ?? null;
  const hasRootPin = pinnedRootPid !== null || pinnedRootBirth !== null;
  const rootPinMatches = !hasRootPin || (integer(pinnedRootPid) && pinnedRootPid > 0 && typeof pinnedRootBirth === 'string' && /^[1-9]\d*$/.test(pinnedRootBirth) && roots.length === 1 && roots[0].binding.pid === pinnedRootPid && String(roots[0].binding.birthFileTime100ns) === pinnedRootBirth);
  if (!rootPinMatches) add('EXPLICIT_ROOT_INSTANCE_PIN_MISMATCH', { pinnedRootPid, pinnedRootBirth });
  const rootScoped = rootPinMatches && roots.length === 1 && roots[0].birth !== null && samePath(roots[0].binding.imagePath, identity.parameters?.expectedImagePath);
  if (!rootScoped) add('EXACT_ROOT_IDENTITY_UNCONFIRMED');
  if (![...states.values()].some(s => s.binding.role === 'host')) add('NO_BOUND_HOST_NUMERIC_DATA', {}, 'unknown');
  if (![...states.values()].some(s => s.binding.role === 'sampler')) add('NO_SAMPLER_NUMERIC_DATA', {}, 'unknown');
  const witnesses = new Map((Array.isArray(identity.lineageWitnesses) ? identity.lineageWitnesses : []).map(b => [b.pid, b]));
  for (const s of states.values()) witnesses.set(s.binding.pid, s.binding);
  for (const s of states.values()) {
    s.eligible = s.birth !== null && (s.binding.role === 'sampler' || rootScoped);
    if (s.binding.role !== 'host') continue;
    let child = s.binding, reachesRoot = false;
    try {
      if (!Array.isArray(child.ancestry) || !child.ancestry.length) throw new Error('No bound ancestry chain');
      const seen = new Set([child.pid]);
      for (let i = 0; i < s.binding.ancestry.length; i++) {
        const a = s.binding.ancestry[i], parent = witnesses.get(a.pid);
        if (!parent || seen.has(a.pid) || child.parentPid !== a.pid || parent.status !== 'BOUND' || parent.queryAndSynchronizeHandleHeld !== true || parent.heldAccessMask !== '0x00101000') throw new Error('Missing, cyclic or inconsistent bound parent');
        const parentBirth = ticks(parent.birthFileTime100ns, 'parent birth');
        if (ticks(a.birthFileTime100ns, 'ancestry birth') !== parentBirth || parentBirth > ticks(child.birthFileTime100ns, 'child birth')) throw new Error('Ancestor birth mismatch/reuse');
        readPoint(parent.rawAtBind, parentBirth, null, null); readPoint(parent.rawAfterHash, parentBirth, null, null);
        seen.add(a.pid);
        if (a.pid === roots[0]?.binding.pid) {
          if (!rootScoped || i !== s.binding.ancestry.length - 1 || parent !== roots[0].binding) throw new Error('Ancestry does not terminate at exact root');
          reachesRoot = true;
        }
        child = parent;
      }
    } catch { reachesRoot = false; }
    s.eligible &&= reachesRoot;
    if (!s.eligible) add('HOST_ANCESTRY_UNCONFIRMED', { pid: s.binding.pid }, 'unknown');
  }
  const text = buffers.samples.toString('utf8').replace(/^\uFEFF/, ''), lines = text.split(/\r?\n/);
  let rows = 0, priorIndex = null, missed = 0;
  const breakState = s => { s.previous = null; s.queue = []; s.base = 0; s.chain++; };
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].trim() && i === lines.length - 1) continue;
    let row, rowStart, rowEnd, rowGap = false;
    try {
      row = parseLossless(lines[i]); rows++;
      if (row.schemaVersion !== 1 || !integer(row.index) || row.index < 0 || !Array.isArray(row.targets)) throw new Error('Invalid sample row schema/index/targets');
      if (String(row.qpcFrequency) !== frequency.toString() || row.logicalProcessors !== logical || row.requestedIntervalMs !== summary.intervalMs) throw new Error('Row clock/processor/interval contract changed');
      rowStart = ticks(row.qpcIterationStart, 'row start'); rowEnd = ticks(row.qpcAfterReads, 'row end');
      if (rowEnd < rowStart) throw new Error('Invalid iteration QPC bounds');
      if (!integer(row.missedSlotsBeforeThisRow) || row.missedSlotsBeforeThisRow < 0) throw new Error('Invalid missed-slot count');
      if (row.missedSlotsBeforeThisRow < missed) throw new Error('Cumulative missed-slot counter decreased');
      const newlyMissed = row.missedSlotsBeforeThisRow - missed;
      missed = row.missedSlotsBeforeThisRow;
      rowGap = priorIndex === null ? row.index !== 0 : row.index !== priorIndex + 1;
      if (rowGap) add('ROG_SAMPLE_INDEX_GAP', { line: i + 1, previous: priorIndex, index: row.index }, 'unknown');
      rowGap ||= newlyMissed > 0;
      priorIndex = row.index;
      const supplied = [...row.targets, ...(row.sampler ? [row.sampler] : [])];
      for (const entry of supplied) if (!entry || !states.has(`${entry.role}:${entry.pid}`)) add('UNBOUND_PROCESS_IN_ROW', { line: i + 1, pid: entry?.pid, role: entry?.role });
      for (const [key, s] of states) {
        const selected = supplied.filter(v => v?.role === s.binding.role && v?.pid === s.binding.pid);
        let point, entry;
        try {
          if (s.birth === null || selected.length !== 1) throw new Error('Missing/duplicate process entry or invalid binding');
          entry = selected[0];
          if (ticks(entry.boundBirthFileTime100ns, 'entry birth') !== s.birth) throw new Error('Entry bound birth changed');
          point = readPoint(entry.raw, s.birth, row.index, i + 1);
          if (point.before < rowStart || point.after > rowEnd) throw new Error('Per-process read outside iteration QPC bounds');
        } catch (error) { s.invalidReads++; add('INVALID_ROG_PROCESS_SAMPLE', { key, line: i + 1, message: error.message }); breakState(s); continue; }
        let d = null, clean = !rowGap;
        if (s.previous) {
          try {
            d = difference(s.previous, point); s.collectorIntervals.push(d);
            const m = entry.interval ?? {}, detail = { key, line: i + 1 };
            clean = compare('interval.status', 'OK', m.status, detail) && clean;
            for (const [name, computed] of Object.entries({ actualIntervalSeconds: d.actualWallMs / 1000, deltaKernel100ns: d.kernel.toString(), deltaUser100ns: d.user.toString(), deltaCpu100ns: d.cpu.toString(), deltaCycles: d.cycles.toString(), machineCpuPercent: d.cpuPercentMachine, singleCoreCpuPercent: d.cpuPercentMachine * logical })) clean = compare(name, computed, m[name], detail) && clean;
            if (d.actualWallMs > summary.intervalMs * 1.5 || rowGap) {
              s.gaps.push({ fromLine: s.previous.line, toLine: point.line, actualWallMs: d.actualWallMs, fromQpc: s.previous.qpc.toString(), toQpc: point.qpc.toString() }); clean = false;
            }
            const utcWallMs = Number(point.utcBefore - s.previous.utcBefore) / 10000;
            if (Math.abs(utcWallMs - d.actualWallMs) > summary.intervalMs) add('ROG_UTC_QPC_DISAGREEMENT', { key, line: i + 1, utcWallMs, qpcWallMs: d.actualWallMs }, 'unknown');
          } catch (error) { s.invalidReads++; add('INVALID_ROG_DELTA', { key, line: i + 1, message: error.message }); breakState(s); continue; }
        }
        if (!clean) breakState(s);
        point.chain = s.chain; s.points.push(point);
        if (s.eligible && clean && d) s.intervals.push({ ...d, segment: s.previous.kind === point.kind ? point.kind : 'boundaryCrossing' });
        s.queue.push(point);
        const distance = p => { const v = point.qpc - p.qpc - frequency; return v < 0n ? -v : v; };
        while (s.base + 1 < s.queue.length && distance(s.queue[s.base + 1]) <= distance(s.queue[s.base])) s.base++;
        const from = s.queue[s.base], span = (point.qpc - from.qpc) * 1000n;
        if (s.eligible && span >= frequency * 875n && span <= frequency * 1125n) {
          const w = difference(from, point);
          s.windows.push({ id: s.windows.length + 1, continuityChain: point.chain, pid: s.binding.pid, role: s.binding.role,
            birthFileTime100ns: s.birth.toString(), fromIndex: from.index, toIndex: point.index, fromLine: from.line, toLine: point.line,
            fromQpc: from.qpc.toString(), toQpc: point.qpc.toString(), qpcFrequency: frequency.toString(),
            fromReadQpcBefore: from.before.toString(), fromReadQpcAfter: from.after.toString(), toReadQpcBefore: point.before.toString(), toReadQpcAfter: point.after.toString(),
            fromUtcBefore: from.fromUtc, fromUtcAfter: from.toUtc, toUtcBefore: point.fromUtc, toUtcAfter: point.toUtc,
            fromProcessAgeSeconds: from.ageMs / 1000, toProcessAgeSeconds: point.ageMs / 1000,
            segment: from.kind === point.kind ? point.kind : 'boundaryCrossing', actualWallMs: w.actualWallMs,
            targetCpu100ns: w.cpu.toString(), targetCpuMs: w.targetCpuMs, deltaCycles: w.cycles.toString(), cpuPercentMachine: w.cpuPercentMachine });
        }
        s.previous = point;
      }
    } catch (error) { add('INVALID_ROG_ROW', { line: i + 1, message: error.message }); priorIndex = null; for (const s of states.values()) breakState(s); }
  }
  if (text.length && !/\n$/.test(text)) add('UNTERMINATED_ROG_JSONL_TAIL', {}, 'unknown');
  compare('rows', rows, summary.rows); compare('missedSlots', missed, summary.missedSlots);
  const targets = [];
  for (const [key, s] of states) {
    const b = s.binding, recorded = b.role === 'sampler' ? summary.sampler : summary.targets?.find(t => t.role === b.role && t.pid === b.pid);
    if (!recorded || String(recorded.birthFileTime100ns) !== String(b.birthFileTime100ns) || !samePath(recorded.imagePath, b.imagePath) || !sameHash(recorded.diskSha256, b.diskSha256)) add('ROG_SUMMARY_PROCESS_IDENTITY_MISMATCH', { key });
    const collectorQpc = s.collectorIntervals.reduce((a, d) => a + d.qpc, 0n), collectorCpu = s.collectorIntervals.reduce((a, d) => a + d.cpu, 0n);
    const collectorSeconds = Number(collectorQpc) / Number(frequency);
    for (const [name, value] of Object.entries({ validIntervals: s.collectorIntervals.length, invalidReads: s.invalidReads, coveredSeconds: collectorSeconds, deltaCpu100ns: s.collectorIntervals.length ? collectorCpu.toString() : null, averageMachineCpuPercent: collectorSeconds > 0 ? cpuMs(collectorCpu) / collectorSeconds / logical / 10 : null })) compare(name, value, recorded?.[name], { key });
    if (s.gaps.length) add('ROG_GAPS_EXCLUDED_FROM_WINDOWS', { key, count: s.gaps.length }, 'unknown');
    const final = b.role === 'sampler' ? { raw: summary.overhead?.samplerNativeBeforeRelease } : summary.finalTargetChecks?.find(t => t.role === b.role && t.pid === b.pid);
    let finalLive = false;
    try { if (s.birth !== null) { readPoint(final?.raw, s.birth, null, null); finalLive = true; } } catch (error) { add('ROG_FINAL_LIVENESS_UNKNOWN', { key, message: error.message }, 'unknown'); }
    const diskUnchanged = b.role === 'sampler' ? null : final?.status === 'OK' && samePath(final.imagePath, b.imagePath) && sameHash(final.diskSha256, b.diskSha256);
    if (diskUnchanged === false) add('ROG_FINAL_DISK_IDENTITY_UNCONFIRMED', { key }, 'unknown');
    let companionsUnchanged = b.role === 'host' ? true : null;
    const companionHashes = {};
    if (b.role === 'host') for (const name of ['YeManInputHost.dll', 'HIDMaestro.Core.dll', 'YeManInputHost.runtimeconfig.json']) {
      const first = b.diskCompanionsAtBind?.Files?.filter(f => f.Name === name) ?? [], last = b.diskCompanionsFinal?.Files?.filter(f => f.Name === name) ?? [];
      const a = first[0], z = last[0];
      const valid = first.length === 1 && last.length === 1 && a.Status === 'OK' && z.Status === 'OK' && samePath(a.Path, path.win32.join(path.win32.dirname(b.imagePath), name)) && samePath(z.Path, a.Path) && sameHash(a.DiskSha256, z.DiskSha256) && String(a.SizeBytes) === String(z.SizeBytes);
      companionsUnchanged &&= valid; companionHashes[name] = valid ? a.DiskSha256 : null;
    }
    if (companionsUnchanged === false) add('ROG_COMPANION_IDENTITY_UNKNOWN_OR_CHANGED', { key }, 'unknown');
    let candidateDiskMatches = null;
    if (candidate && b.role !== 'sampler') {
      candidateDiskMatches = diskUnchanged && sameHash(b.diskSha256, b.role === 'root' ? candidate.nativeExeSha256 : candidate.hostExeSha256);
      if (b.role === 'host') candidateDiskMatches &&= companionsUnchanged && sameHash(companionHashes['YeManInputHost.dll'], candidate.hostDllSha256) && sameHash(companionHashes['HIDMaestro.Core.dll'], candidate.sdkSha256);
      if (!candidateDiskMatches) add('ROG_CANDIDATE_DISK_MISMATCH_OR_UNKNOWN', { key }, 'unknown');
    }
    const segments = {};
    for (const name of ['all', 'startup', 'stable']) {
      const intervals = s.intervals.filter(v => name === 'all' || v.segment === name), wins = s.windows.filter(v => name === 'all' || v.segment === name);
      const qpc = intervals.reduce((a, v) => a + v.qpc, 0n), cpu = intervals.reduce((a, v) => a + v.cpu, 0n);
      const seconds = Number(qpc) / Number(frequency);
      segments[name] = { adjacentIntervalCount: intervals.length, actualCounterCoveredSeconds: s.eligible ? seconds : null, cpu100ns: intervals.length ? cpu.toString() : null,
        averageCpuPercentMachine: seconds > 0 ? cpuMs(cpu) / seconds / logical / 10 : null,
        slidingWindowCount: wins.length, slidingCpuPercentMachine: distribution(wins.map(w => w.cpuPercentMachine)),
        maximumWindow: wins.reduce((p, w) => !p || w.cpuPercentMachine > p.cpuPercentMachine ? w : p, null) };
    }
    targets.push({ key, role: b.role, pid: b.pid, birthFileTime100ns: b.birthFileTime100ns, imagePath: b.imagePath, diskSha256: b.diskSha256,
      finalLivenessConfirmed: finalLive, diskIdentityUnchangedAtEndpoints: diskUnchanged, companionsUnchangedAtEndpoints: companionsUnchanged,
      candidateDiskMatches, loadedModulesAndPdbMatch: 'UNKNOWN_REQUIRES_ETL_MODULE_AND_PDB_REVIEW',
      comparability: { eligible: s.eligible, authority: s.eligible ? 'EXACT_BOUND_PROCESS_AND_ROOT_SCOPE' : 'UNKNOWN_PROCESS_OR_ROOT_ANCESTRY' },
      rawCounterSampleCount: s.points.length, validSampleCount: s.eligible ? s.points.length : null, invalidReadCount: s.invalidReads, gaps: s.gaps, segments, windows: s.windows });
  }
  const errors = issues.filter(v => v.severity === 'error').length;
  return { schemaVersion: 1, analyzer: 'rog-field-offline-v1', generatedAtUtc: new Date().toISOString(),
    status: errors ? 'ROG_DATA_INVALID_PARTIAL_OFFLINE_ANALYSIS' : 'ROG_OFFLINE_MEASUREMENTS_ONLY_ATTRIBUTION_PENDING', candidateCpuAcceptance: false,
    scenario: { name: identity.scenario, stateIndependentlyVerified: false, logMode: 'UNKNOWN_FROM_COUNTERS' },
    provenance: { ...metadata, inputs: Object.entries({ identity: 'IDENTITY.json', samples: 'CPU.jsonl', summary: 'SUMMARY.json', ...(buffers.candidate ? { candidate: 'candidateIdentity' } : {}) }).map(([k, filename]) => ({ filename, bytes: buffers[k].length, sha256: SHA(buffers[k]) })) },
    candidateReference: candidate ? { version: candidate.version, buildId: candidate.buildId, exportSourceTreeSha256: candidate.exportSourceTreeSha256 } : null,
    raw: { identity, summary }, targets, validation: { errors, unknowns: issues.filter(v => v.severity === 'unknown').length, issues, comparisons },
    hostCoverage: { scope: 'Initial bound cohort only; later starts/replacements UNKNOWN', allHostMachineCpuPercent: null },
    attribution: { sourceCause: 'UNKNOWN', exactThreadId: 'UNKNOWN', etlSpanAndLoss: 'UNKNOWN', required: 'Use the SAME window QPC bounds, bound PID/birth, ETL thread lifetime and module/PDB identity. Review CSwitch coverage/loss and sampled/JIT/GC/WaitHandle events before assigning a source cause.' },
    limitations: [ 'Maximum values are averages over observed875..1125ms windows, not instantaneous maxima or a bound on future peaks.',
      'QPC/counters/cycles use exact integer strings and BigInt differences; cycles are never converted to CPU time.',
      'Only verified adjacent intervals enter comparable windows. Invalid/missing rows, identities, read failures, missed slots and long gaps break continuity; missing is never zero.',
      'Startup/stable split uses process birth age120s, not gyro activation. Feature activation/warmup and stationary/log/foreground state need independent scene/log evidence.',
      'The missedSlotsBeforeThisRow field is cumulative; only increases create a new gap. Unknown root/ancestry keeps raw counters but no comparable windows.',
      'Root, each initial Host and observer are separate timelines; percentages are not added across non-atomic reads. Observer CPU is never subtracted.',
      'Matching two disk endpoints does not establish loaded-memory or in-between identity. Candidate references do not prove installation, loaded-module/PDB identity or functionality.',
      'Inputs are read-only. This analyzer starts no collector/app/device and is excluded from product runtime entry points.' ] };
}

function rogMarkdown(a) {
  const lines = ['# ROG CPU offline analysis', '', `Status: ${a.status}; candidateCpuAcceptance=false.`, '', `Scenario: ${a.scenario.name}; actual stationary/gyro/log state requires independent evidence.`, '',
    '| Role / PID | Segment | Valid coverage (s) | Mean machine CPU (%) | ~1s max (%) | Windows |', '| --- | --- | ---: | ---: | ---: | ---: |'];
  for (const t of a.targets) for (const [name, s] of Object.entries(t.segments)) lines.push(`| ${t.role} / ${t.pid} | ${name} | ${fmt(s.actualCounterCoveredSeconds)} | ${fmt(s.averageCpuPercentMachine)} | ${fmt(s.maximumWindow?.cpuPercentMachine)} | ${s.slidingWindowCount} |`);
  lines.push('', 'Startup is process age below120s; feature activation age is unknown. Each process uses its own QPC read midpoint. Do not sum separate process maxima.', '');
  for (const t of a.targets) {
    const w = t.segments.all.maximumWindow;
    lines.push(`${t.key}: disk=${t.diskSha256}; finalLive=${t.finalLivenessConfirmed}; candidateDiskMatches=${t.candidateDiskMatches ?? 'UNKNOWN'}.`, '',
      w ? `Peak=${fmt(w.cpuPercentMachine)}%, ${fmt(w.actualWallMs)}ms, lines${w.fromLine}->${w.toLine}, QPC${w.fromQpc}->${w.toQpc} @${w.qpcFrequency}Hz; UTC ${w.fromUtcBefore}->${w.toUtcAfter}.` : 'Peak UNKNOWN: no comparable window.', '');
  }
  lines.push(`Validation errors=${a.validation.errors}. Source/TID/ETL/PDB attribution remains UNKNOWN.`, '', 'Data issues:', '');
  for (const issue of a.validation.issues) lines.push(`- ${issue.severity}: ${JSON.stringify(issue)}`);
  lines.push('', 'Limitations:', ''); for (const v of a.limitations) lines.push(`- ${v}`);
  return lines.join('\n') + '\n';
}
export async function readRawRun(runDirectory, format = 'candidate') {
  if (!['candidate', 'rog'].includes(format)) throw new Error('Unknown counter format');
  const files = format === 'rog' ? { ...FILES, samples: 'CPU.jsonl' } : FILES;
  const names = Object.keys(files);
  const results = await Promise.allSettled(names.map(key => fsp.readFile(path.join(runDirectory, files[key]))));
  const failures = results.flatMap((result, i) => result.status === 'rejected' ? [`${files[names[i]]}: ${result.reason.message}`] : []);
  if (failures.length) throw new Error(failures.join('\n'));
  return Object.fromEntries(results.map((result, i) => [names[i], result.value]));
}
function inside(parent, child) {
  const rel = path.relative(parent, child);
  return rel === '' || (rel !== '..' && !rel.startsWith('..' + path.sep) && !path.isAbsolute(rel));
}
export function assertOutputDirectory(outputDirectory, evidenceRoot) {
  if (!evidenceRoot || !path.isAbsolute(evidenceRoot)) throw new Error('Explicit absolute evidenceRoot required');
  const writeRoot = path.resolve(evidenceRoot);
  const output = path.resolve(outputDirectory);
  if (!inside(writeRoot, output)) throw new Error('Output must stay within explicit evidenceRoot');
  const realRoot = fs.realpathSync(writeRoot);
  let existing = output;
  while (true) {
    try { fs.lstatSync(existing); break; } catch (error) { if (error.code !== 'ENOENT') throw error; }
    const parent = path.dirname(existing);
    if (parent === existing) throw new Error('Cannot resolve output ancestor');
    existing = parent;
  }
  if (!inside(realRoot, fs.realpathSync(existing))) throw new Error('Output ancestor is a junction/symlink outside authorized directory');
  return output;
}
const fmt = value => finite(value) ? value.toFixed(9) : 'UNKNOWN';
export function markdown(analysis) {
  const a = analysis;
  if (a.analyzer === 'rog-field-offline-v1') return rogMarkdown(a);
  const lines = [ '# Native root CPU offline analysis', '',
    `Status: ${a.status}. candidateCpuAcceptance=false; completeR6Acceptance=false.`, '',
    `Input run: ${a.provenance.runDirectory ?? '(in-memory validation)'}`, '',
    `Machine logical processors: ${a.raw.identity.logicalProcessors ?? 'UNKNOWN'}; recorded missed250msSlots: ${a.coverage.reportedMissed250msSlots ?? 'UNKNOWN'}.`, '',
    '| Age segment | Actual counter coverage (s) | Mean (%) | ~1s max (%) | p50 (%) | p95 (%) | p99 (%) | >=0.1 windows / clusters | >=0.2 windows / clusters | Sampler mean (%) |',
    '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |' ];
  for (const name of ['all', 'startup', 'stable']) {
    const s = a.segments[name], t = s.sliding.thresholds;
    lines.push(`| ${name} | ${fmt(s.actualCounterCoveredWallMs / 1000)} | ${fmt(s.averageCpuPercentMachine)} | ${fmt(s.sliding.cpuPercentMachine.max)} | ${fmt(s.sliding.cpuPercentMachine.p50)} | ${fmt(s.sliding.cpuPercentMachine.p95)} | ${fmt(s.sliding.cpuPercentMachine.p99)} | ${t[0].windowCount} / ${t[0].clusterCount} | ${t[1].windowCount} / ${t[1].clusterCount} | ${fmt(s.sampler.averageCpuPercentMachine)} |`);
  }
  lines.push('', 'Startup=[birth,birth+120s), stable=[birth+120s,infinity). Both endpoints in same segment; no interpolation.', '',
    `Stable required360s; actual=${fmt(a.coverage.stableActualCounterCoveredWallMs / 1000)}s; deficit=${fmt(a.coverage.stableDeficitWallMs / 1000)}s. Requested duration=${a.coverage.requestedDurationSeconds ?? 'UNKNOWN'}s.`, '',
    `Validation errors=${a.validation.errors}; unknown data conditions=${a.validation.unknowns}; raw windows compared=${a.validation.windowComparisons.rowsCompared}; max discrepancy=${a.validation.windowComparisons.maximumAbsoluteNumericError}.`, '',
    `Acceptance blockers: ${a.evaluation.blockers.join(', ') || '(none numerically; no automatic acceptance)'}.`, '',
    'Background visibility, fan inactive and virtual controller inactive remain UNKNOWN in these files. Live-now is UNKNOWN. Raw identity, exit/liveness and summary fields preserved in ANALYSIS.json.', '',
    'All windows have actual UTC / monotonic / process-age endpoints in ANALYSIS.json.windows. Threshold lists reference window IDs. Clusters merge strict overlaps and count union CPU once.', '');
  for (const name of ['all', 'startup', 'stable']) {
    const s = a.segments[name], w = s.sliding.maximumWindow;
    lines.push(`${name} peak: ${w ? `${fmt(w.cpuPercentMachine)}%; samples ${w.fromSample}->${w.toSample}; ${w.fromUtc} -> ${w.toUtc}; actual ${fmt(w.actualWallMs)}ms; process age ${fmt(w.fromProcessAgeSeconds)} -> ${fmt(w.toProcessAgeSeconds)}s.` : 'NO_WINDOWS'}`, '');
    lines.push(`${name} window duration min/max: ${fmt(s.sliding.actualWallMs.min)} / ${fmt(s.sliding.actualWallMs.max)}ms. Sampler sliding p50/p95/p99/max: ${fmt(s.sampler.slidingCpuPercentMachine.p50)} / ${fmt(s.sampler.slidingCpuPercentMachine.p95)} / ${fmt(s.sampler.slidingCpuPercentMachine.p99)} / ${fmt(s.sampler.slidingCpuPercentMachine.max)}%.`, '');
  }
  lines.push(`Sampler whole-process recorded CPU=${fmt(a.sampler.wholeProcessCpuMsRecorded)}ms, wall=${fmt(a.sampler.wholeWallMsRecorded)}ms, average=${fmt(a.sampler.wholeProcessAverageCpuPercentMachine)}%. Scope: ${a.sampler.wholeScopeRecorded ?? 'UNKNOWN'}`, '', 'Formulas:', '');
  for (const [name, value] of Object.entries(a.formulas)) lines.push(`- ${name}: ${value}`);
  if (a.validation.issues.length) {
    lines.push('', 'Data issues:', '');
    for (const issue of a.validation.issues) lines.push(`- ${issue.severity}: ${issue.code} ${JSON.stringify(issue)}`);
  }
  lines.push('', 'Limitations:', '');
  for (const limitation of a.limitations) lines.push(`- ${limitation}`);
  return lines.join('\n') + '\n';
}

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('--help') || !args.length) {
    console.log('node analyze-candidate-cpu.mjs --runDirectory <recorded-run> --evidenceRoot <existing-absolute-development-evidence-root> [--outputDirectory <fresh-directory-within-evidenceRoot>] [--candidateIdentity <ROG-candidate-identity.json>]\nAuto-detects CPU.jsonl (ROG) or SAMPLES.jsonl (legacy); mixed formats refused. Positional runDirectory accepted. Inputs read-only; ANALYSIS.json/md exclusively created, never overwrite evidence.');
    return;
  }
  let runDirectory, outputDirectory, evidenceRoot, candidateIdentityPath;
  for (let i = 0; i < args.length; i++) {
    const name = args[i];
    if (name === '--runDirectory' || name === '--run-directory') runDirectory = args[++i];
    else if (name === '--outputDirectory' || name === '--output-directory') outputDirectory = args[++i];
    else if (name === '--evidenceRoot' || name === '--evidence-root') evidenceRoot = args[++i];
    else if (name === '--candidateIdentity') { candidateIdentityPath = args[++i]; if (!candidateIdentityPath || candidateIdentityPath.startsWith('--')) throw new Error('Missing candidateIdentity path'); }
    else if (!name.startsWith('-') && !runDirectory) runDirectory = name;
    else throw new Error(`Unknown/missing argument: ${name}`);
    if ((name.startsWith('--run') && !runDirectory) || (name.startsWith('--output') && !outputDirectory) || (name.startsWith('--evidence') && !evidenceRoot)) throw new Error(`Missing value: ${name}`);
  }
  if (!runDirectory) throw new Error('runDirectory required');
  if (!evidenceRoot || !path.isAbsolute(evidenceRoot)) throw new Error('Explicit absolute evidenceRoot required');
  runDirectory = path.resolve(runDirectory);
  outputDirectory = assertOutputDirectory(outputDirectory ?? path.join(evidenceRoot, path.basename(runDirectory)), evidenceRoot);
  for (const filename of ['ANALYSIS.json', 'ANALYSIS.md']) if (fs.existsSync(path.join(outputDirectory, filename))) throw new Error(`Refusing to overwrite ${path.join(outputDirectory, filename)}; choose a fresh outputDirectory`);
  const hasRog = fs.existsSync(path.join(runDirectory, 'CPU.jsonl')), hasLegacy = fs.existsSync(path.join(runDirectory, 'SAMPLES.jsonl'));
  if (hasRog === hasLegacy) throw new Error('Exactly one recorded counter format required: CPU.jsonl (ROG) or SAMPLES.jsonl (legacy)');
  const format = hasRog ? 'rog' : 'candidate';
  if (candidateIdentityPath && !hasRog) throw new Error('candidateIdentity is only supported for rog-field input');
  const buffers = await readRawRun(runDirectory, format);
  if (candidateIdentityPath) { candidateIdentityPath = path.resolve(candidateIdentityPath); buffers.candidate = await fsp.readFile(candidateIdentityPath); }
  const analysis = (hasRog ? analyzeRogBuffers : analyzeRawBuffers)(buffers, { runDirectory, outputDirectory, candidateIdentityPath,
    analyzerPath: fileURLToPath(import.meta.url), analyzerSha256: SHA(fs.readFileSync(fileURLToPath(import.meta.url))) });
  const after = await readRawRun(runDirectory, format);
  if (candidateIdentityPath) after.candidate = await fsp.readFile(candidateIdentityPath);
  if (!Object.keys(buffers).every(k => SHA(after[k]) === SHA(buffers[k]))) throw new Error('Raw input changed during offline read; no analysis written');
  analysis.provenance.inputsUnchangedAcrossAnalysisRead = true;
  fs.mkdirSync(outputDirectory, { recursive: true });
  assertOutputDirectory(outputDirectory, evidenceRoot);
  fs.writeFileSync(path.join(outputDirectory, 'ANALYSIS.json'), JSON.stringify(analysis, null, 2) + '\n', { flag: 'wx' });
  fs.writeFileSync(path.join(outputDirectory, 'ANALYSIS.md'), markdown(analysis), { flag: 'wx' });
  console.log(JSON.stringify({ status: analysis.status, outputDirectory,
    ...(hasRog ? { targets: analysis.targets.map(t => ({ role: t.role, pid: t.pid, meanPercentMachine: t.segments.all.averageCpuPercentMachine, maximumWindow: t.segments.all.maximumWindow, candidateDiskMatches: t.candidateDiskMatches })) } : {
      meanPercentMachine: analysis.segments.all.averageCpuPercentMachine,
      maxPercentMachine: analysis.segments.all.sliding.cpuPercentMachine.max,
      stableCoveredSeconds: analysis.coverage.stableActualCounterCoveredWallMs / 1000 }),
    validationErrors: analysis.validation.errors, candidateCpuAcceptance: false }, null, 2));
  if (analysis.validation.errors) process.exitCode = 2;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error(JSON.stringify({ status: 'OFFLINE_ANALYSIS_REFUSED', error: error.message, candidateCpuAcceptance: false })); process.exitCode = 1; });
}
