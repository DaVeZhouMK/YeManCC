#!/usr/bin/env node
// Analyze existing full production-worker fixture measurements; never execute a fixture.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { summarize } from './recovery_cost_analysis.mjs';

export const STATES = Object.freeze([
  'loop-baseline', 'recovery-control', 'worker-generic-idle',
  'worker-msi-detail-off', 'worker-msi-detail-on', 'worker-msi-after-detail-off',
  'worker-msi-terminal', 'worker-msi-maintenance-due',
]);
const TRIALS = 15;
const LOOP_CALLS = 30000;
const WARMUP_CALLS = 128;
const CHECKS = 480;
const MAX_INPUT_BYTES = 16 * 1024 * 1024;
const DUE_STATE = 'worker-msi-maintenance-due';
const METRICS = ['nsPerCall', 'cyclesPerCall', 'threadCpuNsPerCall'];
const DUE_CAVEAT = 'calls=1 includes counter overhead and single-call timing noise; '
  + 'it cannot support a direct causal comparison with the 30000-call loops, '
  + 'including their loop-baseline median.';

function requireValue(condition, location, expectation) {
  if (!condition) throw new Error(`${location}: ${expectation}`);
}
function object(value, at) {
  requireValue(value !== null && typeof value === 'object' && !Array.isArray(value),
    at, 'expected an object');
}
function exact(value, expected, at) {
  requireValue(value === expected, at, `expected ${JSON.stringify(expected)}`);
}
function finite(value, at, positive = false) {
  requireValue(typeof value === 'number' && Number.isFinite(value)
    && (positive ? value > 0 : value >= 0), at,
  `expected a finite ${positive ? 'positive' : 'nonnegative'} number`);
}

// Exported for small mathematical vectors; no simulated benchmark is needed.
export function samplePerCall(sample, calls, at = 'sample') {
  object(sample, at);
  requireValue(calls === 1 || calls === LOOP_CALLS, `${at} calls`, 'expected 1 or 30000');
  for (const field of ['wallTicks', 'qpcFrequency', 'wallNs']) {
    finite(sample[field], `${at}.${field}`, true);
  }
  for (const field of ['threadCycles', 'threadCpu100ns']) {
    finite(sample[field], `${at}.${field}`);
  }
  const expectedWallNs = (sample.wallTicks / sample.qpcFrequency) * 1e9;
  finite(expectedWallNs, `${at}.wallTicks/qpcFrequency`, true);
  requireValue(Math.abs(sample.wallNs - expectedWallNs)
    <= Math.max(1, Math.abs(expectedWallNs) * 1e-6), `${at}.wallNs`,
  'does not match wallTicks/qpcFrequency * 1e9 within 1 ns or 1e-6 relative error');
  const metrics = {
    nsPerCall: sample.wallNs / calls,
    cyclesPerCall: sample.threadCycles / calls,
    threadCpuNsPerCall: (sample.threadCpu100ns / calls) * 100,
  };
  for (const metric of METRICS) finite(metrics[metric], `${at}.${metric}`);
  return metrics;
}

export function analyzeWorkerCost(raw) {
  object(raw, 'root');
  exact(raw.status, 'PASS', 'root.status');
  exact(raw.fullTranslationUnit, true, 'root.fullTranslationUnit');
  exact(raw.normalProductMainExecuted, false, 'root.normalProductMainExecuted');
  exact(raw.realDeviceIo, false, 'root.realDeviceIo');
  exact(raw.forbiddenIo, 0, 'root.forbiddenIo');
  requireValue(Array.isArray(raw.failures) && raw.failures.length === 0,
    'root.failures', 'expected an empty array');
  exact(raw.checks, CHECKS, 'root.checks');
  requireValue(Array.isArray(raw.assertionTrace) && raw.assertionTrace.length === CHECKS,
    'root.assertionTrace', `expected exactly ${CHECKS} assertions`);
  for (const [index, entry] of raw.assertionTrace.entries()) {
    object(entry, `root.assertionTrace[${index}]`);
    exact(entry.passed, true, `root.assertionTrace[${index}].passed`);
  }
  requireValue(Array.isArray(raw.cases) && raw.cases.length === STATES.length * TRIALS,
    'root.cases', 'expected exactly 120 cases');
  const grouped = new Map(STATES.map((state) => [state, new Map()]));
  for (const [index, entry] of raw.cases.entries()) {
    const at = `root.cases[${index}]`;
    object(entry, at);
    exact(entry.kind, 'controlled-worker-cpu-sample', `${at}.kind`);
    requireValue(grouped.has(entry.state), `${at}.state`, 'unknown or missing worker state');
    requireValue(Number.isInteger(entry.trial) && entry.trial >= 0 && entry.trial < TRIALS,
      `${at}.trial`, 'expected an integer in 0..14');
    const trials = grouped.get(entry.state);
    requireValue(!trials.has(entry.trial), `${at}.trial`, `duplicate trial for ${entry.state}`);
    exact(entry.calls, entry.state === DUE_STATE ? 1 : LOOP_CALLS, `${at}.calls`);
    exact(entry.warmupCalls, WARMUP_CALLS, `${at}.warmupCalls`);
    exact(entry.noDeviceOrTransactionIo, true, `${at}.noDeviceOrTransactionIo`);
    exact(entry.stateStable, true, `${at}.stateStable`);
    exact(entry.productionMetadataBranchCompiled, true, `${at}.productionMetadataBranchCompiled`);
    exact(entry.inspectionDelta, 0, `${at}.inspectionDelta`);
    const mappingOpened = entry.state === 'worker-msi-detail-on'
      || entry.state === 'worker-msi-after-detail-off';
    exact(entry.mappingOpenedBefore, mappingOpened, `${at}.mappingOpenedBefore`);
    exact(entry.mappingOpenedAfter, mappingOpened, `${at}.mappingOpenedAfter`);
    requireValue(typeof entry.duePollObserved === 'boolean', `${at}.duePollObserved`,
      'expected a boolean; periodic due polls in loop samples are retained');
    if (entry.state === DUE_STATE) exact(entry.duePollObserved, true, `${at}.duePollObserved`);
    const perCall = samplePerCall(entry.sample, entry.calls, `${at}.sample`);
    trials.set(entry.trial, { trial: entry.trial, calls: entry.calls,
      duePollObserved: entry.duePollObserved, sample: entry.sample, ...perCall });
  }

  const states = Object.fromEntries(STATES.map((state) => {
    const trials = grouped.get(state);
    exact(trials.size, TRIALS, `state ${state} trial count`);
    const samples = [...trials.values()].sort((a, b) => a.trial - b.trial);
    const summaries = Object.fromEntries(METRICS.map((metric) => {
      const summary = summarize(samples.map((sample) => sample[metric]));
      for (const [key, value] of Object.entries(summary)) finite(value, `${state}.${metric}.${key}`);
      return [metric, summary];
    }));
    return [state, { sampleCount: TRIALS, callsPerSample: state === DUE_STATE ? 1 : LOOP_CALLS,
      warmupCalls: WARMUP_CALLS, duePollObservedCount: samples.filter((s) => s.duePollObserved).length,
      interpretation: state === DUE_STATE ? DUE_CAVEAT
        : 'All 15 measured loops retained, including periodic due polls and slow samples.',
      ...summaries, samples }];
  }));
  const baseline = states['loop-baseline'];
  const medianDifferenceFromLoopBaseline = Object.fromEntries(STATES.map((state) => [state, {
    callsPerSample: states[state].callsPerSample,
    baselineCallsPerSample: LOOP_CALLS,
    directCausalComparisonSupported: false,
    interpretation: state === DUE_STATE ? DUE_CAVEAT
      : 'Descriptive difference of medians only; not a paired or exact additive component cost.',
    ...Object.fromEntries(METRICS.map((metric) =>
      [metric, states[state][metric].median - baseline[metric].median])),
  }]));
  return {
    schemaVersion: 1,
    status: 'PASS',
    passMeaning: 'Input data is complete, valid and analyzable; no performance threshold or speed ordering is required.',
    scope: 'Controlled full-translation-unit production-worker fixture measurements only.',
    fixtureContract: {
      productionMetadataBranchCompiled: true,
      contextPublisher: 'Test guard selects the actual ContextPublisher with a private named mapping owned by this process and its real std::mutex.',
      metadataSource: 'Artificial controlled source strings; does not establish real metadata-source collection.',
      publisherLifecycle: 'Cold detail-off reconstructs its owned publisher per sample; after-detail-off retains the opened mapping and continues metadata publication.',
      externalBoundaries: 'Existing full-TU fixture boundaries; normal product main and real device IO are not executed.',
      interpretation: 'Reported fixture contract; JSON validation alone does not independently attest source or binary identity.',
      periodicDuePolls: 'Actual clock expiry is allowed in every loop state; samples are never filtered by timing or duePollObserved.',
      lockWaitIndividuallyMeasured: false,
    },
    limitations: [
      'Does not establish whole-system CPU, power, end-to-end benefit, real-device performance or lock-wait cost.',
      DUE_CAVEAT,
      'A threadCpu100ns value of zero can reflect counter granularity; it does not establish zero CPU work.',
      'The complete worker includes M1 recovery maintenance; do not add M1 again or treat worker minus M1 as an exact additive cost.',
      'Original samples and per-call values remain unadjusted. Baseline differences are separate, descriptive and may be negative.',
    ],
    validated: { fullTranslationUnit: true, normalProductMainExecuted: false, realDeviceIo: false,
      forbiddenIo: 0, failures: 0, checks: CHECKS, passedAssertions: CHECKS,
      caseCount: STATES.length * TRIALS, stateCount: STATES.length, trialsPerState: TRIALS,
      warmupCalls: WARMUP_CALLS, loopCallsPerSample: LOOP_CALLS, maintenanceDueCallsPerSample: 1 },
    statistics: {
      quantileMethod: 'Imported recovery_cost_analysis.mjs summarize: linear interpolation at (n - 1) * p on sorted values.',
      nsPerCall: 'wallNs / calls', cyclesPerCall: 'threadCycles / calls',
      threadCpuNsPerCall: 'threadCpu100ns / calls * 100',
      baselineDifference: 'state median minus loop-baseline median; no per-sample subtraction, clamping or sample filtering',
    },
    states,
    medianDifferenceFromLoopBaseline,
    assertionTrace: raw.assertionTrace,
    cases: raw.cases,
  };
}

function readBounded(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const stat = fs.fstatSync(fd);
    requireValue(stat.isFile(), '--fixture', 'expected a regular JSON file');
    requireValue(stat.size > 0 && stat.size <= MAX_INPUT_BYTES,
      '--fixture', `expected 1..${MAX_INPUT_BYTES} bytes`);
    const bytes = Buffer.alloc(stat.size);
    let offset = 0;
    while (offset < bytes.length) {
      const read = fs.readSync(fd, bytes, offset, bytes.length - offset, offset);
      requireValue(read > 0, '--fixture', 'input changed or was truncated during read');
      offset += read;
    }
    requireValue(fs.readSync(fd, Buffer.alloc(1), 0, 1, offset) === 0,
      '--fixture', 'input grew during read');
    return bytes;
  } finally { fs.closeSync(fd); }
}
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');

export function runCli(args = process.argv.slice(2)) {
  if (args.length === 1 && args[0] === '--help') {
    console.log('Usage: node worker_cost_analysis.mjs --fixture ABS_JSON --out ABS_NEW_DIR\n'
      + 'Reads existing JSON only (max 16 MiB). Writes RESULT.json in an exclusively new directory; parent must exist.');
    return;
  }
  requireValue(args.length === 4, 'arguments', 'expected --fixture ABS_JSON --out ABS_NEW_DIR');
  const options = new Map();
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index];
    requireValue(key === '--fixture' || key === '--out', 'arguments', `unknown option ${key}`);
    requireValue(!options.has(key), 'arguments', `duplicate option ${key}`);
    requireValue(typeof args[index + 1] === 'string' && path.isAbsolute(args[index + 1]),
      key, 'expected an absolute path');
    options.set(key, path.resolve(args[index + 1]));
  }
  requireValue(options.has('--fixture') && options.has('--out'), 'arguments', 'both options are required');
  const fixture = options.get('--fixture');
  const out = options.get('--out');
  // lstat rejects even dangling symlinks. mkdir is the exclusive check against races.
  try {
    fs.lstatSync(out);
    throw new Error('--out: output directory already exists; refusing to overwrite');
  } catch (error) { if (error.code !== 'ENOENT') throw error; }
  const bytes = readBounded(fixture);
  const before = sha256(bytes);
  const result = analyzeWorkerCost(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)));
  const after = sha256(readBounded(fixture));
  exact(after, before, 'input SHA-256 after analysis');
  result.input = { path: fixture, bytes: bytes.length,
    sha256Before: before, sha256After: after, unchanged: true };
  fs.mkdirSync(out);
  const resultPath = path.join(out, 'RESULT.json');
  let ownsResult = false;
  try {
    const fd = fs.openSync(resultPath, 'wx');
    ownsResult = true;
    try { fs.writeFileSync(fd, JSON.stringify(result, null, 2) + '\n'); }
    finally { fs.closeSync(fd); }
    exact(sha256(readBounded(fixture)), before, 'input SHA-256 after output');
  } catch (error) {
    // Remove only this invocation's result; never recursively delete a shared directory.
    if (ownsResult) { try { fs.unlinkSync(resultPath); } catch { /* Keep original error. */ } }
    try { fs.rmdirSync(out); } catch { /* Preserve any concurrent files. */ }
    throw error;
  }
  console.log(JSON.stringify({ status: result.status, passMeaning: result.passMeaning,
    result: resultPath, inputSha256: before, inputUnchangedAfterOutput: true }));
  return result;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { runCli(); }
  catch (error) {
    console.error(`Worker cost analysis rejected: ${error.message}`);
    process.exitCode = 1;
  }
}
