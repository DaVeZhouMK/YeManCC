/** Bounded ordinary-export handoff for the existing global log entry points.
 * Input is inline JSON, never a list of paths or commands. Caller attribution
 * remains unverified even when the claimed archive digest matches the reader.
 */
import fs from 'node:fs';
import { createHash } from 'node:crypto';
import { analyzeMonitorFieldLogs } from './field-log-consumer.mjs';

export const MONITOR_FIELD_FEEDBACK_SCHEMA = 'ymcc.monitor-field-log-input.v1';
export const MAX_MONITOR_FIELD_INPUT_BYTES = 16 * 1024 * 1024;
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const hex = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const output = () => ({ schemaVersion: 1, kind: 'ymcc-monitor-field-feedback',
  status: 'INCOMPLETE', diagnostics: [], analysis: null,
  fieldAcceptance: false, currentNativeHealth: 'UNKNOWN', provenanceValidated: false,
  archiveAttribution: 'CALLER_CLAIM_ONLY', automaticExecution: false,
  suggestedTestIds: [], suggestedCaseIds: [], input: null, sourceHashes: [],
});
const refusal = (report, code) => { report.status = 'REJECTED'; report.diagnostics.push({code}); return report; };

export function evaluateMonitorFieldLogFeedback({ inputBytes, sourceArchive } = {}) {
  const report = output();
  if (!Buffer.isBuffer(inputBytes) || inputBytes.length > MAX_MONITOR_FIELD_INPUT_BYTES) return refusal(report, 'FIELD_INPUT_SIZE_INVALID');
  report.input = { bytes: inputBytes.length, sha256: sha(inputBytes), stable: false };
  if (!sourceArchive || !hex(sourceArchive.sha256) || sourceArchive.stable !== true) return refusal(report, 'ARCHIVE_IDENTITY_UNAVAILABLE');
  report.archiveSha256 = sourceArchive.sha256;
  let value;
  try { value = JSON.parse(inputBytes.toString('utf8')); }
  catch { return refusal(report, 'FIELD_INPUT_JSON_INVALID'); }
  if (!value || typeof value !== 'object' || Array.isArray(value)
    || Object.keys(value).length !== 4
    || !['schema', 'sourceArchiveSha256', 'manifest', 'streams'].every(key => Object.hasOwn(value, key))
    || value.schema !== MONITOR_FIELD_FEEDBACK_SCHEMA) return refusal(report, 'FIELD_INPUT_SCHEMA_INVALID');
  if (!hex(value.sourceArchiveSha256) || value.sourceArchiveSha256 !== sourceArchive.sha256) return refusal(report, 'FIELD_ARCHIVE_DIGEST_MISMATCH');
  if (value.manifest?.fixtureScope !== 'ORDINARY_EXPORT_UNVERIFIED') return refusal(report, 'ORDINARY_EXPORT_SCOPE_REQUIRED');
  report.analysis = analyzeMonitorFieldLogs({ manifest: value.manifest, streams: value.streams });
  report.status = report.analysis.status;
  report.suggestedTestIds = report.analysis.suggestedTestIds;
  report.suggestedCaseIds = report.analysis.suggestedCaseIds;
  report.diagnostics.push({code: 'ARCHIVE_MEMBERS_AND_SOURCE_TUPLE_NOT_INDEPENDENTLY_VERIFIED'});
  return report;
}

export function readMonitorFieldLogFeedback({ inputPath, sourceArchive } = {}) {
  const sources = ['./field-log-feedback.mjs', './field-log-consumer.mjs'];
  const sourceSnapshot = () => sources.map(name => ({ path: 'tools/ymcc_sim/global/monitor-notice-lifecycle/' + name.slice(2),
    sha256: sha(fs.readFileSync(new URL(name, import.meta.url))) }));
  const before = sourceSnapshot();
  let fd, currentFd, report;
  try {
    if (typeof inputPath !== 'string' || !inputPath || /[\0\r\n]/.test(inputPath)) return refusal(output(), 'FIELD_INPUT_PATH_INVALID');
    const link = fs.lstatSync(inputPath);
    if (link.isSymbolicLink() || !link.isFile()) return refusal(output(), 'FIELD_INPUT_NOT_REGULAR_FILE');
    fd = fs.openSync(inputPath, 'r');
    const start = fs.fstatSync(fd);
    if (!start.isFile() || start.size > MAX_MONITOR_FIELD_INPUT_BYTES) return refusal(output(), 'FIELD_INPUT_SIZE_INVALID');
    const bytes = Buffer.alloc(start.size + 1);
    let length = 0, read;
    while (length < bytes.length && (read = fs.readSync(fd, bytes, length, bytes.length - length, null)) > 0) length += read;
    const finish = fs.fstatSync(fd), linkAfter = fs.lstatSync(inputPath);
    currentFd = fs.openSync(inputPath, 'r');
    const current = fs.fstatSync(currentFd);
    const identity = stat => [stat.dev, stat.ino, stat.size, stat.mtimeMs, stat.ctimeMs].join(':');
    if (length !== start.size || identity(start) !== identity(finish) || identity(start) !== identity(current)
      || linkAfter.isSymbolicLink() || !current.isFile()) return refusal(output(), 'FIELD_INPUT_CHANGED_DURING_READ');
    report = evaluateMonitorFieldLogFeedback({ inputBytes: bytes.subarray(0, length), sourceArchive });
    if (report.input) report.input.stable = true;
  } catch { report = refusal(output(), 'FIELD_INPUT_UNREADABLE'); }
  finally {
    if (currentFd !== undefined) fs.closeSync(currentFd);
    if (fd !== undefined) fs.closeSync(fd);
  }
  const after = sourceSnapshot();
  report.sourceHashes = before;
  if (JSON.stringify(before) !== JSON.stringify(after)) {
    report = refusal(output(), 'FIELD_CONSUMER_SOURCE_CHANGED_DURING_READ');
    report.sourceHashes = before;
  }
  return report;
}

/** Evidence association is never a field PASS, even when recipes passed. */
export function applyMonitorFieldFeedbackVerdict(verdict, feedback) {
  if (!feedback || verdict.exitCode !== 0) return verdict;
  if (feedback.status === 'REJECTED' || feedback.status === 'DIFFERENCE') return { status: 'MONITOR_FIELD_EVIDENCE_REJECTED', exitCode: 2 };
  return { status: 'RECIPES_PASS_MONITOR_FIELD_ACCEPTANCE_UNVERIFIED', exitCode: 10 };
}