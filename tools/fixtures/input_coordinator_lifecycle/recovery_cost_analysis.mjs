#!/usr/bin/env node
// Read and analyze an existing controlled full-TU measurement; never run a fixture.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

export const STATES = Object.freeze([
  'loop-baseline', 'all-clear', 'terminal-only', 'attempts-only',
  'terminal-and-attempts', 'fallback-available', 'fallback-unproven',
  'active-verification-paused',
]);
const TRIALS = 15;
const CALLS = 30000;
const WARMUP_CALLS = 128;
const CHECKS = 480;
const MAX_INPUT_BYTES = 16 * 1024 * 1024;
const METRICS = ['nsPerCall', 'cyclesPerCall', 'threadCpuNsPerCall'];

function requireValue(condition, location, expectation) {
  if (!condition) throw new Error(`${location}: ${expectation}`);
}
function object(value, location) {
  requireValue(value !== null && typeof value === 'object' && !Array.isArray(value),
    location, 'expected an object');
}
function exact(value, expected, location) {
  requireValue(value === expected, location, `expected ${JSON.stringify(expected)}`);
}
function finite(value, location, positive = false) {
  requireValue(typeof value === 'number' && Number.isFinite(value)
    && (positive ? value > 0 : value >= 0), location,
  `expected a finite ${positive ? 'positive' : 'nonnegative'} number`);
}

// Linear interpolation at rank (n - 1) * p, including the ordinary median.
export function summarize(values) {
  requireValue(Array.isArray(values) && values.length > 0, 'values', 'expected a nonempty array');
  Array.from(values).forEach((value, index) => {
    requireValue(typeof value === 'number' && Number.isFinite(value),
      `values[${index}]`, 'expected a finite number');
  });
  const sorted = [...values].sort((a, b) => a - b);
  const quantile = (p) => {
    const rank = (sorted.length - 1) * p;
    const lower = Math.floor(rank);
    const upper = Math.ceil(rank);
    const fraction = rank - lower;
    // Weighted endpoints avoid overflowing the difference for signed vectors.
    return lower === upper ? sorted[lower]
      : sorted[lower] * (1 - fraction) + sorted[upper] * fraction;
  };
  return { median: quantile(0.5), p10: quantile(0.1), p90: quantile(0.9),
    min: sorted[0], max: sorted.at(-1) };
}

// Also exported for small mathematical unit vectors, without fabricating a fixture.
export function samplePerCall(sample, calls = CALLS, at = 'sample') {
  object(sample, at);
  exact(calls, CALLS, `${at} calls`);
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
  const metrics = { nsPerCall: sample.wallNs / calls,
    cyclesPerCall: sample.threadCycles / calls,
    threadCpuNsPerCall: (sample.threadCpu100ns / calls) * 100 };
  for (const metric of METRICS) finite(metrics[metric], `${at}.${metric}`);
  return metrics;
}
export function analyzeRecoveryCost(raw) {
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
  raw.cases.forEach((entry, index) => {
    const at = `root.cases[${index}]`;
    object(entry, at);
    exact(entry.kind, 'controlled-recovery-cpu-sample', `${at}.kind`);
    requireValue(grouped.has(entry.state), `${at}.state`, 'unknown or missing recovery state');
    requireValue(Number.isInteger(entry.trial) && entry.trial >= 0 && entry.trial < TRIALS,
      `${at}.trial`, 'expected an integer in 0..14');
    const trials = grouped.get(entry.state);
    requireValue(!trials.has(entry.trial), `${at}.trial`, `duplicate trial for ${entry.state}`);
    exact(entry.noIo, true, `${at}.noIo`);
    exact(entry.stateStable, true, `${at}.stateStable`);
    exact(entry.inspectionDelta, entry.state === 'active-verification-paused' ? CALLS : 0,
      `${at}.inspectionDelta`);
    object(entry.before, `${at}.before`);
    object(entry.after, `${at}.after`);
    exact(entry.calls, CALLS, `${at}.calls`);
    exact(entry.warmupCalls, WARMUP_CALLS, `${at}.warmupCalls`);
    const perCall = samplePerCall(entry.sample, entry.calls, `${at}.sample`);
    trials.set(entry.trial, { trial: entry.trial, ...perCall });
  });

  const states = Object.fromEntries(STATES.map((state) => {
    const trials = grouped.get(state);
    exact(trials.size, TRIALS, `state ${state} trial count`);
    const samples = [...trials.values()].sort((a, b) => a.trial - b.trial);
    return [state, { sampleCount: TRIALS, callsPerSample: CALLS, warmupCalls: WARMUP_CALLS,
      ...Object.fromEntries(METRICS.map((metric) =>
        [metric, summarize(samples.map((sample) => sample[metric]))])), samples }];
  }));
  const baseline = states['loop-baseline'];
  const medianDifferenceFromLoopBaseline = Object.fromEntries(STATES.map((state) => [state,
    Object.fromEntries(METRICS.map((metric) => [metric,
      states[state][metric].median - baseline[metric].median]))]));
  return {
    schemaVersion: 1,
    status: 'PASS',
    passMeaning: 'Input data is complete, valid and analyzable; no CPU threshold or speed ordering is required.',
    scope: 'Controlled full-translation-unit fixture CPU measurements only.',
    fixtureContract: {
      policyClock: 'Fixed for stable active-verification-paused inspection cost; not a real-time deadline test.',
      osMonotonicClock: 'Existing fixtureMonotonicTick uses actual GetTickCount64 plus zero offset.',
      transactionMutex: 'Actual existing TransactionMutex, without separate lock-wait instrumentation.',
      lockWaitIndividuallyMeasured: false,
      timedRegion: 'Bounded indirect function-call loop; state setup, warmup, JSON and assertions are outside.',
      interpretation: 'Contract declarations; analyzer validates reported samples and does not reconstruct the state machine.',
    },
    limitations: [
      'Does not establish whole-system CPU, power or end-to-end benefit.',
      'Does not establish real-device performance or lock-wait cost.',
      'Loop baseline is retained unchanged; median differences are descriptive and may be negative.',
    ],
    validated: { fullTranslationUnit: true, normalProductMainExecuted: false, realDeviceIo: false,
      forbiddenIo: 0, failures: 0, caseCount: 120, stateCount: STATES.length,
      trialsPerState: TRIALS, callsPerSample: CALLS, warmupCalls: WARMUP_CALLS,
      checks: CHECKS, passedAssertions: CHECKS },
    statistics: { quantileMethod: 'linear interpolation at (n - 1) * p on sorted values',
      nsPerCall: 'wallNs / calls', cyclesPerCall: 'threadCycles / calls',
      threadCpuNsPerCall: 'threadCpu100ns / calls * 100',
      baselineDifference: 'state median minus loop-baseline median; no per-sample subtraction or clamping' },
    states,
    medianDifferenceFromLoopBaseline,
    // Preserve original before/after snapshots without reimplementing the state machine.
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
    const extra = Buffer.alloc(1);
    requireValue(fs.readSync(fd, extra, 0, 1, offset) === 0,
      '--fixture', 'input grew during read');
    return bytes;
  } finally { fs.closeSync(fd); }
}
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');

export function runCli(args = process.argv.slice(2)) {
  if (args.length === 1 && args[0] === '--help') {
    console.log('Usage: node recovery_cost_analysis.mjs --fixture ABS_JSON --out ABS_NEW_DIRECTORY\n'
      + 'Reads existing JSON only; max input 16 MiB. Writes RESULT.json in a new directory.');
    return;
  }
  const options = new Map();
  requireValue(args.length === 4, 'arguments', 'expected --fixture ABS_JSON --out ABS_NEW_DIRECTORY');
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index];
    requireValue(key === '--fixture' || key === '--out', 'arguments', `unknown option ${key}`);
    requireValue(!options.has(key), 'arguments', `duplicate option ${key}`);
    requireValue(path.isAbsolute(args[index + 1]), key, 'expected an absolute path');
    options.set(key, path.resolve(args[index + 1]));
  }
  requireValue(options.has('--fixture') && options.has('--out'), 'arguments', 'both options are required');
  const fixture = options.get('--fixture');
  const out = options.get('--out');
  // lstat also rejects dangling symlinks; mkdir below is the exclusive race-safe check.
  try {
    fs.lstatSync(out);
    throw new Error('--out: output directory already exists; refusing to overwrite');
  } catch (error) { if (error.code !== 'ENOENT') throw error; }
  const bytes = readBounded(fixture);
  const before = sha256(bytes);
  const result = analyzeRecoveryCost(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)));
  const after = sha256(readBounded(fixture));
  exact(after, before, 'input SHA-256 after analysis');
  result.input = { path: fixture, bytes: bytes.length, sha256Before: before,
    sha256After: after, unchanged: true };
  // Parent must already exist; recursive mkdir would blur the new-directory boundary.
  fs.mkdirSync(out);
  const resultPath = path.join(out, 'RESULT.json');
  let ownsResult = false;
  try {
    const resultFd = fs.openSync(resultPath, 'wx');
    ownsResult = true;
    try { fs.writeFileSync(resultFd, JSON.stringify(result, null, 2) + '\n'); }
    finally { fs.closeSync(resultFd); }
    const finalHash = sha256(readBounded(fixture));
    exact(finalHash, before, 'input SHA-256 after output');
  } catch (error) {
    // Remove only the result this invocation owns, never recurse over a shared path.
    if (ownsResult) { try { fs.unlinkSync(resultPath); } catch { /* Preserve the original failure. */ } }
    try { fs.rmdirSync(out); } catch { /* Leave unexpected concurrent files intact. */ }
    throw error;
  }
  console.log(JSON.stringify({ status: result.status, passMeaning: result.passMeaning,
    result: resultPath, inputSha256: before }));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { runCli(); }
  catch (error) {
    console.error(`Recovery cost analysis rejected: ${error.message}`);
    process.exitCode = 1;
  }
}
