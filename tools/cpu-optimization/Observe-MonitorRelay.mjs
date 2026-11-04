#!/usr/bin/env node
// Development-only, bounded directory-event observation. No product/IPC writes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { performance } from 'node:perf_hooks';
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const options = {};
for (let i = 2; i < process.argv.length; ++i) {
  const key = process.argv[i];
  if (!['--directory', '--identity', '--evidenceRoot', '--outputDirectory', '--durationSeconds'].includes(key) || !process.argv[i + 1])
    throw Error('Use --directory <installed PowerControl> --identity <fresh binding.json> --evidenceRoot <existing dev evidence> --outputDirectory <new child> [--durationSeconds 15]');
  if (options[key]) throw Error('Duplicate argument ' + key);
  options[key] = process.argv[++i];
}
for (const key of ['--directory', '--identity', '--evidenceRoot', '--outputDirectory'])
  if (!options[key] || !path.isAbsolute(options[key])) throw Error('Explicit absolute path required: ' + key);
const duration = Number(options['--durationSeconds'] ?? 15);
if (!Number.isInteger(duration) || duration < 5 || duration > 60) throw Error('Duration must be an integer from5 through60 seconds');
const directory = fs.realpathSync(options['--directory']);
if (!fs.statSync(directory).isDirectory()) throw Error('Watch directory must exist');
const identityBytes = fs.readFileSync(options['--identity']);
const identity = JSON.parse(identityBytes.toString('utf8').replace(/^\uFEFF/, ''));
if (!Number.isSafeInteger(identity.pid) || identity.pid <= 0 || !/^\d{1,19}$/.test(identity.birthFileTimeUtc100ns ?? '') ||
    !/^[a-f\d]{64}$/i.test(identity.exeSha256 ?? '')) throw Error('Fresh native PID/birth/EXE binding is required as an external reference');
const evidenceRoot = fs.realpathSync(options['--evidenceRoot']), output = path.resolve(options['--outputDirectory']);
const within = (base, target) => { const rel = path.relative(base, target); return rel !== '' && rel !== '..' && !rel.startsWith('..' + path.sep) && !path.isAbsolute(rel); };
if (!within(evidenceRoot, output)) throw Error('Output must be a new child of the evidence root');
let ancestor = path.dirname(output);
while (!fs.existsSync(ancestor)) ancestor = path.dirname(ancestor);
if (fs.realpathSync(ancestor) !== evidenceRoot && !within(evidenceRoot, fs.realpathSync(ancestor))) throw Error('Output ancestor escapes evidence through a link');
if (fs.existsSync(output)) throw Error('Output already exists; preserve the original observation');
fs.mkdirSync(output, { recursive: true });
if (!within(evidenceRoot, fs.realpathSync(output)) || fs.realpathSync(output) === directory) throw Error('Resolved output must stay inside development evidence');
const files = ['hwinfo-ok', 'fps-monitor.hb', 'fps-status.json', 'topmon.json', 'game-target.json'];
const wanted = new Set(files);
const clock = performance.now(), startedAtUtc = new Date().toISOString();
function snapshot() {
  return files.map(name => {
    try {
      const s = fs.statSync(path.join(directory, name), { bigint: true });
      return { name, status: 'PRESENT', bytes: s.size.toString(), modifiedNs: s.mtimeNs.toString(), modifiedUtc: new Date(Number(s.mtimeMs)).toISOString() };
    } catch (error) {
      return { name, status: error.code === 'ENOENT' ? 'ABSENT' : 'UNREADABLE', ...(error.code === 'ENOENT' ? {} : { error: String(error) }) };
    }
  });
}
const before = snapshot(), eventPath = path.join(output, 'FILE-EVENTS.jsonl');
const eventHandle = fs.openSync(eventPath, 'wx');
const counters = Object.fromEntries(files.map(name => [name, { rename: 0, change: 0, other: 0 }]));
let watcher, timer, finished = false, events = 0, recorded = 0, missingNames = 0, watcherError = null, eventLimitReached = false;
const start = { status: 'EXTERNAL_DIRECTORY_EVENT_OBSERVER_STARTED', startedAtUtc, observerPid: process.pid, directory,
  durationSeconds: duration, files, identityReference: { path: path.resolve(options['--identity']), sha256: sha(identityBytes),
    pid: identity.pid, birth: identity.birthFileTimeUtc100ns, exeSha256: identity.exeSha256 },
  nativeHandleHeldByThisObserver: false, before, productWrites: 0, applicationOrDeviceControl: false, candidateCpuAcceptance: false };
fs.writeFileSync(path.join(output, 'WATCH-START.json'), JSON.stringify(start, null, 2) + '\n', { flag: 'wx' });
function finish(reason) {
  if (finished) return;
  finished = true; clearTimeout(timer); watcher?.close(); fs.closeSync(eventHandle);
  const after = snapshot();
  const status = watcherError ? 'WATCHER_ERROR_UNKNOWN_COVERAGE' : reason !== 'DURATION_COMPLETE' ? 'INTERRUPTED_PARTIAL' :
    missingNames || eventLimitReached || [...before, ...after].some(row => row.status === 'UNREADABLE') ? 'COMPLETE_WITH_COVERAGE_LIMITS' : 'DURATION_COMPLETE_EVENT_OBSERVATION_ONLY';
  const result = { ...start, status, endedAtUtc: new Date().toISOString(), actualWallMs: performance.now() - clock,
    reason, totalTrackedEvents: events, recordedEvents: recorded, counters, unknownNameEvents: missingNames,
    eventLimitReached, watcherError, after, candidateCpuAcceptance: false,
    limitations: ['ReadDirectoryChanges/fs.watch notifications are not write syscalls or durable-write counts; events may coalesce and complete coverage is unknown.',
      'No writer PID attribution or CPU accounting. Supplied binding is a reference, not a held process handle; caller must bind the same root again after observation.',
      'Only the five named files are observed; directory metadata is inspected only at start/end, contents are not read.',
      'Absent files or zero events alone do not prove the producer/feature is active or that no unobserved write occurred. Correlate native identity, settings and matching stacks.'] };
  fs.writeFileSync(path.join(output, 'WATCH-RESULT.json'), JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ status, outputDirectory: output, totalTrackedEvents: events, counters, candidateCpuAcceptance: false }, null, 2));
  process.exitCode = watcherError || reason !== 'DURATION_COMPLETE' ? 1 : 0;
}
try {
  watcher = fs.watch(directory, { persistent: true }, (kind, filename) => {
    if (filename === null) { ++missingNames; return; }
    const name = filename.toString().toLowerCase();
    if (!wanted.has(name)) return;
    ++events; ++counters[name][kind === 'rename' || kind === 'change' ? kind : 'other'];
    if (recorded >= 10000) { eventLimitReached = true; return; }
    fs.writeSync(eventHandle, JSON.stringify({ atUtc: new Date().toISOString(), wallMs: performance.now() - clock, name, kind }) + '\n');
    ++recorded;
  });
  watcher.on('error', error => { watcherError = String(error); finish('WATCHER_ERROR'); });
  timer = setTimeout(() => finish('DURATION_COMPLETE'), duration * 1000);
  process.once('SIGINT', () => finish('SIGINT'));
  process.once('SIGTERM', () => finish('SIGTERM'));
} catch (error) { watcherError = String(error); finish('START_OR_SETUP_ERROR'); }