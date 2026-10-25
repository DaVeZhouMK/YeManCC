/** Read-only, privacy-filtered ZIP corpus import. No plan is executed here.
 *
 * indexLogCorpus(root, { pythonPath?, domains?: [fan|sleep|input|performance], limits?, timeoutMs? }) ->
 *   { status, archives: [{ archivePath, operatorLabel, members, summary }], summary }
 * buildLogReplayPlan(archivePath, sameOptions) ->
 *   { status, sourceArchive: {bytes,mtimeUtc,sha256,stable}, events, scenarios, recommendedAdapterIds, summary, diagnostics }
 *
 * archivePath is the caller's source locator. ZIP-internal paths and excluded
 * basenames never leave the reader. Only enumerated producer fields survive.
 * READY means this bounded import completed, never hardware/test acceptance.
 * Partial evidence remains visible as INCOMPLETE; unknown records are UNSUPPORTED.
 * Schedule order is per member; there is no inferred cross-file causal order.
 * domains defaults to all four reviewed producer domains. Event capacity is
 * shared equally by selected domain, then by member. Hitting a share is INCOMPLETE.
 * metadataOnly excludes log parsing; compressed archive bytes are still hashed.
 */
import {createHash} from 'node:crypto';
import {connectFanApiProjections} from './fan-api-corpus.mjs';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFile } from 'node:child_process';

const READER = fileURLToPath(new URL('./log-zip-reader.py', import.meta.url));
const TARGET = 'owned-mock-only';
const DEFAULTS = Object.freeze({
  maxArchives: 64, maxScanEntries: 4096, maxDepth: 8,
  maxArchiveBytes: 256 * 1024 * 1024, maxCentralDirectoryBytes: 8 * 1024 * 1024,
  maxMembers: 4096, maxTotalUncompressedBytes: 512 * 1024 * 1024,
  maxCompressionRatio: 200, maxMemberReadBytes: 8 * 1024 * 1024,
  maxTotalReadBytes: 32 * 1024 * 1024, maxLineBytes: 64 * 1024,
  maxLinesPerMember: 20000, maxTotalLines: 50000, maxEvents: 20000,
});
const LABELS = new Map([['微星G3E', 'MSI G3E'], ['MSI G3E', 'MSI G3E'],
  ['MSI-G3E', 'MSI G3E'], ['ROG-Xbox-2025', 'ROG-Xbox-2025'], ['Win5', 'Win5']]);
const fault = (status, code, extra = {}) => ({ status, code, ...extra });
const overall = statuses => statuses.includes('FAIL') ? 'FAIL' : statuses.includes('BLOCKED') ? 'BLOCKED'
  : statuses.some(s => s === 'INCOMPLETE' || s === 'UNSUPPORTED') ? 'INCOMPLETE' : 'READY';
const base = kind => ({ schemaVersion: 1, kind, target: TARGET, readOnly: true,
  executesCommands: false, status: 'READY', diagnostics: [] });
function optionsFor(options) {
  if (!options || typeof options !== 'object' || Array.isArray(options)) throw new Error('INVALID_OPTIONS');
  const domains = options.domains ?? [];
  if (!Array.isArray(domains) || domains.some(domain => !['fan', 'sleep', 'input', 'performance'].includes(domain))) throw new Error('INVALID_DOMAINS');
  if (options.limits !== undefined && (!options.limits || typeof options.limits !== 'object' || Array.isArray(options.limits))) throw new Error('INVALID_LIMITS');
  const limits = { ...DEFAULTS };
  for (const [key, value] of Object.entries(options.limits ?? {})) {
    if (!Object.hasOwn(DEFAULTS, key) || !Number.isSafeInteger(value) || value < 1 || value > DEFAULTS[key]) throw new Error('INVALID_LIMIT');
    limits[key] = value;
  }
  const timeoutMs = options.timeoutMs ?? 30000;
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 100 || timeoutMs > 60000) throw new Error('INVALID_TIMEOUT');
  return { limits, timeoutMs, pythonPath: options.pythonPath, domains: [...new Set(domains)] };
}
function operatorLabel(archivePath) {
  for (const segment of path.dirname(archivePath).split(/[\\/]/).reverse()) {
    if (LABELS.has(segment)) return { value: LABELS.get(segment), provenance: 'operator-label', detected: false };
  }
  // Only these explicit filename labels are recognized, never a narrative title.
  const name = path.basename(archivePath);
  if (/^YeManCC-logs-\d{8}-\d{6}-ROGXbox2025\.zip$/i.test(name)) return { value: 'ROG-Xbox-2025', provenance: 'operator-label', detected: false };
  if (/^YeManCC-logs-\d{8}-\d{6}-Win5\.zip$/i.test(name)) return { value: 'Win5', provenance: 'operator-label', detected: false };
  return { value: null, provenance: 'operator-label', detected: false };
}
async function runtime(explicit) {
  const selected = explicit ?? process.env.YMCC_SIM_PYTHON;
  const candidates = selected !== undefined ? [selected] : [
    path.join(os.homedir(), '.cache', 'codex-runtimes', 'codex-primary-runtime', 'dependencies', 'python', 'python.exe'),
    path.join(os.homedir(), '.cache', 'codex-runtimes', 'codex-primary-runtime', 'dependencies', 'python', 'bin', 'python3'),
  ];
  for (const candidate of candidates) {
    if (typeof candidate !== 'string' || !path.isAbsolute(candidate) || /[\0\r\n]/.test(candidate) || /(?:^|[\\/])WindowsApps(?:[\\/]|$)/i.test(candidate)) continue;
    if (!/^python(?:3(?:\.\d+)?)?(?:\.exe)?$/i.test(path.basename(candidate))) continue;
    try {
      const resolved = await fs.realpath(candidate);
      if (/(?:^|[\\/])WindowsApps(?:[\\/]|$)/i.test(resolved)) continue;
      if ((await fs.stat(resolved)).isFile()) return resolved;
    } catch { /* No PATH search, installation or download. */ }
  }
  return null;
}
async function readArchive(archivePath, mode, config) {
  const python = await runtime(config.pythonPath);
  if (!python) return { status: 'BLOCKED', diagnostics: [fault('BLOCKED', 'PYTHON_RUNTIME_UNAVAILABLE')], members: [], events: [], summary: {} };
  return new Promise(resolve => {
    const child = execFile(python, ['-I', '-B', READER], {
      shell: false, windowsHide: true, timeout: config.timeoutMs, maxBuffer: 32 * 1024 * 1024,
      encoding: 'utf8', cwd: path.dirname(READER),
    }, (error, stdout) => {
      if (error) {
        const code = error.code === 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER' ? 'READER_OUTPUT_LIMIT'
          : error.killed ? 'READER_TIMEOUT' : 'READER_FAILED';
        // Never expose stderr, exception messages or child arguments from logs.
        resolve({ status: 'FAIL', diagnostics: [fault('FAIL', code)], members: [], events: [], summary: {} });
        return;
      }
      try {
        const result = JSON.parse(stdout);
        if (!['READY', 'INCOMPLETE', 'FAIL'].includes(result.status) || !Array.isArray(result.members)
          || !Array.isArray(result.events) || !Array.isArray(result.diagnostics)) throw new Error();
        resolve(result);
      } catch { resolve({ status: 'FAIL', diagnostics: [fault('FAIL', 'READER_PROTOCOL_INVALID')], members: [], events: [], summary: {} }); }
    });
    child.stdin.on('error', () => {});
    child.stdin.end(JSON.stringify({ archivePath, mode, limits: config.limits, domains: config.domains }));
  });
}

export async function indexLogCorpus(root, options = {}) {
  const result = { ...base('ymcc-log-corpus'), archives: [], summary: {} };
  let config;
  try { config = optionsFor(options); }
  catch { result.status = 'FAIL'; result.diagnostics.push(fault('FAIL', 'INVALID_OPTIONS')); return result; }
  if (typeof root !== 'string' || !root || /[\0\r\n]/.test(root)) {
    result.status = 'FAIL'; result.diagnostics.push(fault('FAIL', 'INVALID_ROOT')); return result;
  }
  const rootPath = path.resolve(root), paths = [];
  let scanned = 0, exhausted = false;
  async function walk(directory, depth) {
    let dir;
    try { dir = await fs.opendir(directory); }
    catch { result.diagnostics.push(fault('INCOMPLETE', 'DIRECTORY_UNREADABLE')); return; }
    try {
      for await (const entry of dir) {
        if (exhausted) break;
        if (++scanned > config.limits.maxScanEntries) { exhausted = true; result.diagnostics.push(fault('INCOMPLETE', 'SCAN_ENTRY_LIMIT')); break; }
        const absolute = path.join(directory, entry.name);
        if (entry.isSymbolicLink()) { result.diagnostics.push(fault('INCOMPLETE', 'SYMLINK_SKIPPED')); continue; }
        if (entry.isDirectory()) {
          if (depth >= config.limits.maxDepth) { result.diagnostics.push(fault('INCOMPLETE', 'SCAN_DEPTH_LIMIT')); continue; }
          await walk(absolute, depth + 1);
        } else if (entry.isFile() && /\.zip$/i.test(entry.name)) {
          if (paths.length >= config.limits.maxArchives) { exhausted = true; result.diagnostics.push(fault('INCOMPLETE', 'ARCHIVE_COUNT_LIMIT')); break; }
          paths.push(absolute);
        }
      }
    } catch { result.diagnostics.push(fault('INCOMPLETE', 'DIRECTORY_UNREADABLE')); }
  }
  try {
    const stat = await fs.lstat(rootPath);
    if (stat.isSymbolicLink() || !stat.isDirectory()) throw new Error();
    await walk(rootPath, 0);
  } catch { result.status = 'FAIL'; result.diagnostics.push(fault('FAIL', 'ROOT_UNREADABLE')); return result; }
  for (const [index, archivePath] of paths.sort().entries()) {
    const data = await readArchive(archivePath, 'index', config);
    result.archives.push({ id: `archive-${String(index + 1).padStart(4, '0')}`, archivePath,
      operatorLabel: operatorLabel(archivePath), ...data });
    if (data.status === 'BLOCKED') break;
  }
  if (!paths.length) result.diagnostics.push(fault('INCOMPLETE', 'NO_ARCHIVES'));
  result.status = overall([...result.archives.map(a => a.status), ...result.diagnostics.map(d => d.status)]);
  result.summary = { archiveCount: result.archives.length, discoveredArchiveCount: paths.length,
    memberCount: result.archives.reduce((n, a) => n + a.members.length, 0),
    allowlistedMemberCount: result.archives.reduce((n, a) => n + a.members.filter(m => m.policy === 'ALLOWLIST_BOUNDED_READ').length, 0),
    operatorLabels: [...new Set(result.archives.map(a => a.operatorLabel.value).filter(Boolean))],
    contentRead: false, metadataOnly: true };
  return result;
}

export async function buildLogReplayPlan(archivePath, options = {}) {
  const result = { ...base('ymcc-log-replay-plan'), archivePath: null, sourceArchive: null, operatorLabel: { value: null, provenance: 'operator-label', detected: false },
    events: [], scenarios: [], recommendedAdapterIds: [], adapterRecommendations: [], summary: {},
    ordering: 'member-line-order-only', eventBudgetPolicy: 'equal-domain-then-member', recommendationScope: 'reviewed-mock-association-only',
    permissions: { hardwareWrite: false, systemSleep: false, nativeCommands: false } };
  let config;
  try { config = optionsFor(options); }
  catch { result.status = 'FAIL'; result.diagnostics.push(fault('FAIL', 'INVALID_OPTIONS')); return result; }
  if (typeof archivePath !== 'string' || !archivePath || /[\0\r\n]/.test(archivePath)) {
    result.status = 'FAIL'; result.diagnostics.push(fault('FAIL', 'INVALID_ARCHIVE_PATH')); return result;
  }
  result.archivePath = path.resolve(archivePath);
  result.operatorLabel = operatorLabel(result.archivePath);
  result.selectedDomains = config.domains.length ? config.domains : ['fan', 'sleep', 'input', 'performance'];
  const ingestionSources=['log-corpus.mjs','log-zip-reader.py','fan-api-corpus.mjs','fan-api-log-consumer/normalize.mjs'];
  const sourceSnapshot=()=>Promise.all(ingestionSources.map(async name=>({path:'tools/ymcc_sim/global/'+name,
    sha256:createHash('sha256').update(await fs.readFile(new URL(name,import.meta.url))).digest('hex')})));
  result.sourceHashes=await sourceSnapshot();
  const data = await readArchive(result.archivePath, 'replay', config);
  Object.assign(result, data);
  connectFanApiProjections(result);
  const sourceHashesAfter=await sourceSnapshot();
  if(JSON.stringify(result.sourceHashes)!==JSON.stringify(sourceHashesAfter)) {
    result.status='FAIL';result.events=[];result.diagnostics.push(fault('FAIL','INGESTION_SOURCE_CHANGED_DURING_READ'));
  }

  const grouped = new Map();
  for (const event of result.events) {
    const key = `${event.memberId}:${event.domain}`;
    if (!grouped.has(key)) grouped.set(key, { id: key, domain: event.domain, memberId: event.memberId,
      target: TARGET, kind: 'observed-events', causalClaims: false, timingStatus: 'OBSERVED_RELATIVE', schedule: [] });
    if (event.atMs === null) grouped.get(key).timingStatus = 'UNSUPPORTED';
    grouped.get(key).schedule.push({ eventId: event.id, atMs: event.atMs, action: 'observe', target: TARGET });
  }
  result.scenarios = [...grouped.values()];
  if (result.events.some(event => event.domain === 'sleep')) {
    result.recommendedAdapterIds = ['sleep.host-forms', 'sleep.host-typeseries', 'sleep.host-rapidwake'];
  }
  const unavailable = result.events.filter(event => event.event === 'startup.handshake'
    && event.domain === 'fan' && event.hostMode === 'real-hc-unavailable'
    && event.hardwareCapable === false && event.hardwareWritesEnabled === false && event.hardwareWritesObserved === false);
  if (unavailable.length) {
    result.recommendedAdapterIds.push('fan.hc-init-unavailable');
    result.adapterRecommendations.push({ adapterId: 'fan.hc-init-unavailable',
      evidenceEventIds: unavailable.map(event => event.id), scope: 'reviewed-mock-association-only',
      mappedErrorCode: 'FAN_DEVICE_GATE_DENIED', mappedErrorCodeProvenance: 'source-mapped-not-archive-literal',
      rootCauseProven: false });
  }
  const gpu = result.events.filter(event => event.domain === 'performance' && event.event === 'machine-capability' && event.capability === 'gpu.dedicatedMemory');
  if (gpu.length) {
    result.recommendedAdapterIds.push('performance.gpu-dedicated-memory-workflow');
    result.adapterRecommendations.push({adapterId:'performance.gpu-dedicated-memory-workflow',evidenceEventIds:gpu.map(event=>event.id),scope:'reviewed-mock-association-only',rootCauseProven:false,exactEventTimelineReplayed:false});
  }
  // Generic fan ERR markers do not establish an HTTP deadline, a preset or a
  // Host operation sequence. They intentionally add no adapter recommendation.
  result.summary = { ...result.summary, eventCount: result.events.length, scenarioCount: result.scenarios.length,
    recommendedAdapterCount: result.recommendedAdapterIds.length, executed: false };
  return result;
}
