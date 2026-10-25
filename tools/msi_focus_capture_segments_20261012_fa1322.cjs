'use strict';
// Read staged files ONLY. No product, device/driver/process observer, network,
// inferred HidHide ownership, active Steam access or installation.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const LOGS = ['native-lifecycle.log', 'native-detail.log', 'focus-trace.log', 'virtual-gamepad.log'];
const sha256 = b => crypto.createHash('sha256').update(b).digest('hex');
function manifest(text) {
  const result = {};
  for (const line of text.replace(/^\uFEFF/, '').split(/\r?\n/)) {
    const i = line.indexOf('=');
    if (i > 0) result[line.slice(0, i)] = line.slice(i + 1);
  }
  const pid = Number(result.processId);
  if (!Number.isSafeInteger(pid) || pid <= 0) throw Error('Explicit positive manifest processId required');
  if (!/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d/.test(result.exportedAtLocal || '')) throw Error('Manifest exportedAtLocal required');
  if (!/^[a-f0-9]{64}$/i.test(result.nativeSha256 || '')) throw Error('Manifest native EXE SHA256 required');
  return { pid, exportedAtLocal: result.exportedAtLocal, nativeExeSha256: result.nativeSha256.toLowerCase() };
}
function jsonLines(text) {
  const rows = [], rejected = [];
  text.replace(/^\uFEFF/, '').split(/\r?\n/).forEach((line, i) => {
    if (!line.trim()) return;
    try {
      const row = JSON.parse(line);
      if (!row || typeof row !== 'object' || Array.isArray(row)) throw Error('not a record');
      rows.push({ line: i + 1, row });
    } catch { rejected.push(i + 1); }
  });
  return { rows, rejectedLines: rejected };
}
function currentWindow(rows, m) {
  const starts = rows.filter(({row}) => row.pid === m.pid && row.event === 'boot-single-instance-acquired' &&
    typeof row.time === 'string' && row.time.slice(0, 19) <= m.exportedAtLocal.slice(0, 19));
  starts.sort((a, b) => a.row.time.localeCompare(b.row.time));
  return { pid: m.pid, start: starts.at(-1)?.row.time || null, end: m.exportedAtLocal,
    endPrecision: 'seconds; export may include rows during that final second',
    startProof: starts.length ? 'last-retained-explicit-manifest-PID-boot-row' : 'missing; do-not-assume-entire-PID-history-is-current' };
}
function within(time, w) {
  // end has second precision: include milliseconds in the same second.
  return typeof time === 'string' && w.start !== null && time >= w.start && time.slice(0, 19) <= w.end.slice(0, 19);
}
function attribution(row, w) {
  if (Number.isSafeInteger(row.pid) && row.pid > 0) {
    if (row.pid !== w.pid) return 'prior-or-other-explicit-PID';
    if (!w.start) return 'manifest-PID-without-retained-session-boundary';
    return within(row.time, w) ? 'current-explicit-PID-and-time' : 'same-PID-outside-current-session';
  }
  return within(row.time, w) ? 'current-time-window-only-NOT-PID-proof' : 'unattributed-no-PID';
}
function summarize(parsed, w) {
  const explicit = {}, countsByAttribution = {}, events = {};
  for (const {row} of parsed.rows) {
    const a = attribution(row, w); countsByAttribution[a] = (countsByAttribution[a] || 0) + 1;
    if (row.pid) {
      const p = explicit[row.pid] ||= { count: 0, first: row.time || null, last: null };
      ++p.count; p.last = row.time || p.last;
    }
    if (a === 'current-explicit-PID-and-time' && typeof row.event === 'string')
      events[row.event] = (events[row.event] || 0) + 1;
  }
  return { explicitPids: explicit, countsByAttribution, currentExplicitPidEvents: events,
    rejectedLines: parsed.rejectedLines };
}
const RECEIPT_KEYS = ['time','pid','event','phase','reason','state','edge','cycleAccepted','cycleForRestore',
  'configurationReturned','helperPending','helperExitCode','refreshFailed','ownedPaths','sourceRoot',
  'virtualTargetCreated','modeWrite','verificationScope','externalInputBlocked','connectedMask',
  'admittedMask','buttonMask','ymccForeground','hostPrepared','hostSubmission','neutralAccepted',
  'uiOwnsInput','sourceLive','frameAccepted','submitNeutral'];
function receipt(item) {
  return { line: item.line, ...Object.fromEntries(RECEIPT_KEYS.filter(k => k in item.row).map(k => [k, item.row[k]])) };
}
function steamRows(text) {
  let wallTime = null;
  return text.replace(/^\uFEFF/, '').split(/\r?\n/).flatMap((line, i) => {
    const match = /^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\]\s*(.*)$/.exec(line);
    if (match) wallTime = match[1].replace(' ', 'T');
    if (!wallTime || !line.trim()) return [];
    return [{ line: i + 1, time: wallTime, timestampExplicit: !!match, text: match ? match[2] : line }];
  });
}
function steamSplit(text, w) {
  const rows = steamRows(text);
  // Steam-start marker is a window delimiter, not proof of reboot or that no
  // other YMCC process was alive. Never infer button delivery from an open.
  const starts = rows.filter(r => w.start && r.time < w.start && /^Client version:/.test(r.text));
  const lower = starts.at(-1)?.time || null;
  const interesting = r => /Local Device Found|^\s*(type:|path:|serial_number:)|opened for index|reserv.*XInput|SDL Mapping|hid_read failure|Lizard Mode|OnFocusWindowChanged|Loaded Config|Controller PollState/.test(r.text);
  return { preLaunchStart: lower, preLaunchScope: 'latest-retained-Steam-start-to-YMCC-boot;not-proof-of-YMCC-process-absence-or-input-delivery',
    preLaunch: rows.filter(r => lower && r.time >= lower && r.time < w.start && interesting(r)),
    duringExportSession: rows.filter(r => within(r.time, w) && interesting(r)) };
}
function analyze(directory, currentSource) {
  directory = path.resolve(directory);
  const manifestFile = path.join(directory, 'YeManCC-export-manifest.txt');
  const m = manifest(fs.readFileSync(manifestFile, 'utf8'));
  const lifecycle = jsonLines(fs.readFileSync(path.join(directory, LOGS[0]), 'utf8'));
  const w = currentWindow(lifecycle.rows, m), logs = {};
  for (const name of LOGS) {
    const file = path.join(directory, name);
    if (!fs.existsSync(file)) { logs[name] = { missing: true }; continue; }
    const bytes = fs.readFileSync(file), parsed = jsonLines(bytes.toString('utf8'));
    logs[name] = { file, sha256: sha256(bytes), ...summarize(parsed, w) };
  }
  const current = lifecycle.rows.filter(x => attribution(x.row, w) === 'current-explicit-PID-and-time');
  const prior = lifecycle.rows.filter(x => x.row.event === 'session-clean-exit' && x.row.pid !== m.pid);
  const nativeFocus = current.filter(x => x.row.event === 'msi-native-foreground-isolation').map(receipt);
  const currentTarget = current.filter(x => /input-host.*start|input-host.*prepared|input-host\.identity|input-host-target/.test(x.row.event || '')).map(receipt);
  const result = { schemaVersion: 1, directory, manifest: m, currentWindow: w,
    identityWarning: 'Manifest native EXE identity is NOT current main.cpp source identity; do not attribute frozen-build failures to current source',
    logs, previousCleanExitRows: prior.map(receipt),
    currentNativeForegroundReceipts: nativeFocus, currentTargetStartOrPreparationReceipts: currentTarget,
    currentPhysicalAndUiReceipts: current.filter(x => /domain-pad-truth|gamepad-controller-(wake|sleep)|gamepad-ui-input|msi-ui-output|hidhide\.(cloak-resident|app-reg-resident|unplugged-unhide)/.test(x.row.event || '')).map(receipt),
    missingCoverage: [
      'No pre-YMCC-launch complete HidHide hidden/cloak/inverse/application snapshot with selected-root identity and ownership provenance',
      'No pre-launch Steam per-button delivery/action receipt; controller opened is not proof of navigation',
      'Native restored/config-cycle-accepted means owned configuration/helper only, not all inherited entries cleared or Steam input restored',
      'Latest captures have MSI hardware only; no independent non-MSI physical-run comparison',
      'Tick-only input-capture rows have no PID/wall time and are not assigned to current PID by filename'
    ] };
  for (const name of ['steam-controller.log', 'steam-controller-ui.log']) {
    const file = path.join(directory, name);
    result[name] = fs.existsSync(file) ? { file, ...steamSplit(fs.readFileSync(file, 'utf8'), w) } : { missing: true };
  }
  const inputFile = path.join(directory, 'input-capture.jsonl');
  if (fs.existsSync(inputFile)) {
    const parsed = jsonLines(fs.readFileSync(inputFile, 'utf8'));
    result.tickOnlyCapture = { totalRows: parsed.rows.length, rejectedLines: parsed.rejectedLines,
      pidlessWallTimelessRows: parsed.rows.filter(x => !x.row.pid && !x.row.time).length,
      attribution: 'NOT-ASSIGNED-TO-MANIFEST-PID', starts: parsed.rows.filter(x => x.row.kind === 'start').map(x => ({line:x.line,t:x.row.t,virtualTargetEnabled:x.row.virtualTargetEnabled,virtualTargetPersona:x.row.virtualTargetPersona})) };
  }
  if (currentSource) {
    const bytes = fs.readFileSync(currentSource);
    result.currentSource = { file: path.resolve(currentSource), sha256: sha256(bytes), comparison: 'source-hash-only;not-equated-to-manifest-binary' };
  }
  const settingsFile = path.join(directory, 'yeman-settings.json');
  if (fs.existsSync(settingsFile)) {
    const settings = JSON.parse(fs.readFileSync(settingsFile, 'utf8').replace(/^\uFEFF/, ''));
    result.exportSettings = { file: settingsFile, scope: 'at-export-not-pre-launch', outputTarget: settings.input?.outputTarget ?? null };
  }
  return result;
}
if (require.main === module) {
  const [directory, currentSource] = process.argv.slice(2);
  if (!directory) { console.error('Usage: node audit.cjs <staged-directory> [current-main.cpp]'); process.exitCode = 2; }
  else console.log(JSON.stringify(analyze(directory, currentSource), null, 2));
}
module.exports = { manifest, jsonLines, currentWindow, attribution, summarize, steamRows, steamSplit, analyze };
