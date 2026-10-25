/** Offline MSI persona evidence audit. Reads exports only; no device APIs, process
 * launch, driver preparation, product execution, or settings/visibility writes.
 * Evidence is historical for the manifest binaries, never current runtime QA.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

export function parseManifest(text) {
  return Object.fromEntries(text.replace(/^\uFEFF/, '').split(/\r?\n/)
    .flatMap(line => { const m = /^([\w]+)=(.*)$/.exec(line); return m ? [[m[1], m[2]]] : []; }));
}
export function localEpoch(value, biasMinutes) {
  if (typeof value !== 'string' || !Number.isFinite(biasMinutes)) return NaN;
  const s = value.replace(' ', 'T');
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?$/.test(s)) return NaN;
  // Windows bias: UTC = local + bias. Never inherit the audit machine's timezone.
  return Date.parse(`${s}Z`) + biasMinutes * 60000;
}
export function jsonRows(text, file) {
  return text.replace(/^\uFEFF/, '').split(/\r?\n/).flatMap((line, i) => {
    try { const data = JSON.parse(line); return data && typeof data === 'object' && !Array.isArray(data)
      ? [{ data, file, line: i + 1 }] : []; } catch { return []; }
  });
}
const ref = row => ({ file: row.file, line: row.line, time: row.data.time ?? row.data.utcEpochMs });
const upper = value => String(value ?? '').toUpperCase();
const nativePid = row => row.data.nativePid ?? row.data.pid;
const unique = values => [...new Set(values)];

export function manifestWindow(manifest, nativeRows) {
  const pid = Number(manifest.processId), bias = Number(manifest.timeZoneBiasMinutes);
  if (!Number.isSafeInteger(pid) || pid <= 0 || manifest.timeZoneBiasMinutes == null || !Number.isFinite(bias))
    throw new Error('Missing/invalid manifest process identity or timezone bias');
  const end = localEpoch(manifest.exportedAtLocal, bias) + 999;
  if (!Number.isFinite(end)) throw new Error('Missing/invalid export timestamp');
  const owned = nativeRows.filter(r => nativePid(r) === pid && localEpoch(r.data.time, bias) <= end);
  const starts = owned.filter(r => r.data.event === 'start' || r.data.event === 'process-start' || r.data.event === 'native-start');
  const candidates = starts.length ? starts : owned;
  const begin = Math.min(...candidates.map(r => localEpoch(r.data.time, bias)).filter(Number.isFinite));
  if (!Number.isFinite(begin)) throw new Error(`No timestamped native rows owned by manifest PID ${pid}`);
  return { pid, bias, begin, end, beginLocal: candidates.find(r => localEpoch(r.data.time, bias) === begin)?.data.time,
    endLocal: manifest.exportedAtLocal, boundaryProof: starts.length ? 'explicit-start-event' : 'first-manifest-pid-row-not-full-process-lifetime' };
}
export function ownedNativeRows(rows, window) {
  // This function is ONLY for native logs. In observer logs `pid` is a USB
  // PRODUCT ID (e.g. 6402), not the emitting Windows process ID.
  return rows.filter(r => nativePid(r) === window.pid && localEpoch(r.data.time, window.bias) >= window.begin
    && localEpoch(r.data.time, window.bias) <= window.end);
}
export function bindObserverRows(rows, window) {
  const starts = rows.filter(r => r.data.event === 'start' && r.data.parentPid === window.pid
    && r.data.utcEpochMs >= window.begin && r.data.utcEpochMs <= window.end);
  const result = [];
  for (const start of starts) {
    const id = start.data.observerPid;
    if (!Number.isSafeInteger(id) || id <= 0 || !start.data.parentCreationTime) continue;
    const later = rows.filter(r => r.data.event === 'start' && r.data.observerPid === id
      && r.data.utcEpochMs > start.data.utcEpochMs);
    const cutoff = Math.min(window.end + 1, ...later.map(r => r.data.utcEpochMs));
    result.push({ observerPid: id, parentPid: window.pid, parentCreationTime: start.data.parentCreationTime,
      start: ref(start), rows: rows.filter(r => r.data.observerPid === id && r.data.utcEpochMs >= start.data.utcEpochMs
        && r.data.utcEpochMs < cutoff && r.data.utcEpochMs >= window.begin) });
  }
  return result;
}
export function endpointRole(data, virtualIds = []) {
  const id = upper(data.pnpInstanceId ?? data.instance ?? data.instanceId), p = upper(data.interfacePath);
  const vid = data.hidVid ?? data.vid, pid = data.hidPid ?? data.pid;
  const page = data.hidUsagePage ?? data.usagePage, usage = data.hidUsage ?? data.usage;
  if (virtualIds.some(x => upper(x) === id) || /HIDMAESTRO|VIGEM|HM-CTL/.test(id + p)) return 'virtual-target-interface';
  const msi = vid === 0x0db0 && (pid === 0x1901 || pid === 0x1902);
  const pathMsi = /VID_0DB0&PID_190[12]/i.test(id + p);
  if (!msi && !pathMsi) return 'unattributed-or-other-device';
  if (page === 1 && (usage === 4 || usage === 5)) return 'msi-physical-main-gamepad';
  if (page === 1 && (usage === 6 || usage === 7)) return 'msi-physical-keyboard-metadata';
  if (page === 1 && (usage === 1 || usage === 2)) return 'msi-physical-mouse-metadata';
  if (page === 12) return 'msi-physical-consumer-auxiliary';
  if (page === 32) return 'msi-physical-sensor-auxiliary';
  if (page >= 0xff00) return 'msi-physical-vendor-auxiliary';
  if (/VID_0DB0&PID_1902&MI_00&COL01/.test(id + p)) return 'msi-main-path-caps-not-observed';
  return 'msi-physical-role-not-observed';
}
export function parsePnpReceipt(detail) {
  const fields = Object.fromEntries(String(detail ?? '').split(';').flatMap(x => {
    const at = x.indexOf('='); return at > 0 ? [[x.slice(0, at), x.slice(at + 1)]] : [];
  }));
  const list = key => [...String(fields[key] ?? '').matchAll(/([^,\[\]]+)\[([^\]]*)\]/g)]
    .map(m => ({ instance: m[1], hardwareIds: m[2].split('|').filter(Boolean) }));
  return { counts: Object.fromEntries(['primary-roots', 'xbox-companion-roots', 'hid-children', 'steamdeck-roots', 'steamdeck-hid-children']
    .map(k => [`pnp-${k}`, /^\d+$/.test(fields[`pnp-${k}`] ?? '') ? Number(fields[`pnp-${k}`]) : null])),
    allStarted: fields['pnp-started'] === 'true', primaryRoots: list('pnp-primary-root-ids'),
    companionRoots: list('pnp-xbox-companion-root-ids'), hidChildren: list('pnp-hid-ids'),
    steamDeckRoots: list('pnp-steamdeck-root-ids'), steamDeckHidChildren: list('pnp-steamdeck-hid-ids') };
}
export function buildRuns(nativeRows, window) {
  const sorted = [...ownedNativeRows(nativeRows, window)].sort((a, b) => localEpoch(a.data.time, window.bias) - localEpoch(b.data.time, window.bias));
  const runs = [];
  for (const row of sorted.filter(r => r.data.event === 'input-host-started')) {
    const d = row.data;
    if (!d.runId || !Number.isSafeInteger(d.epoch)) continue;
    if (runs.some(r => r.runId === d.runId && r.epoch === d.epoch)) continue; // dual log sinks
    const same = sorted.filter(r => r.data.runId === d.runId && r.data.epoch === d.epoch);
    const ack = same.find(r => r.data.event === 'input-host-command-result' && r.data.command === 'PREPARE_TARGET'
      && r.data.ok === true && r.data.targetPnpAdmitted === true);
    const release = same.find(r => r.data.event === 'input-host-release-receipt' && localEpoch(r.data.time, window.bias) >= localEpoch(d.time, window.bias));
    const settled = same.find(r => r.data.event === 'input-host-switch-audit' && r.data.beat === 'settled');
    runs.push({ runId: d.runId, epoch: d.epoch, persona: d.persona, profile: d.builtProfile,
      vidPid: upper(d.vidPid), nativePid: window.pid, hostPid: settled?.data.hostPid ?? release?.data.hostPid ?? null,
      hostInstanceId: ack?.data.hostInstanceId ?? settled?.data.hostInstanceId ?? release?.data.hostInstanceId ?? null,
      begin: localEpoch(d.time, window.bias), end: release ? localEpoch(release.data.time, window.bias) : window.end,
      start: ref(row), prepare: ack ? { ...ref(ack), observation: parsePnpReceipt(ack.data.targetPnpDetail ?? ack.data.detail) } : null,
      release: release ? { ...ref(release), ok: release.data.ok, disposition: release.data.disposition } : null });
  }
  return runs;
}

export function parseSteam(text, file, bias) {
  const rows = [], lines = text.replace(/^\uFEFF/, '').split(/\r?\n/);
  let currentTime = '', lastFound = null;
  for (let i = 0; i < lines.length; i++) {
    const s = lines[i], t = /^\[([^\]]+)\]/.exec(s);
    if (t) currentTime = t[1];
    const at = localEpoch(currentTime, bias), base = { file, line: i + 1, time: currentTime, at, timeResolutionMs: 1000 };
    if (/Client version:/.test(s)) { rows.push({ ...base, kind: 'steam-session-start' }); lastFound = null; }
    if (/Local Device Found/.test(s)) {
      let end = Math.min(i + 12, lines.length);
      for (let j = i + 1; j < end; j++) if (/Local Device Found|Client version:|Steam controller device opened/.test(lines[j])) { end = j; break; }
      const block = lines.slice(i, end).join('\n');
      const id = /\btype:\s*([\da-f]+)\s+([\da-f]+)/i.exec(block);
      const p = /\bpath:\s*([^\r\n]+)/i.exec(block);
      const product = /Product:\s*([^\r\n]*)/i.exec(block);
      lastFound = { ...base, kind: 'discovery', vidPid: id ? `${id[1].padStart(4, '0')}:${id[2].padStart(4, '0')}`.toUpperCase() : null,
        path: p?.[1].trim() ?? null, product: product?.[1].trim() ?? null };
      rows.push(lastFound);
    }
    const opened = /Steam controller device opened for index (\d+)/.exec(s);
    if (opened) { rows.push({ ...base, kind: 'opened', index: Number(opened[1]),
      // Require a temporally local explicit discovery; no invented SDL->HID map.
      discovery: lastFound && at >= lastFound.at && at - lastFound.at <= 2000 ? lastFound : null }); lastFound = null; }
    const activation = /Queueing activation for controller:\s*(\d+) app:\s*(\d+)/.exec(s);
    if (activation) rows.push({ ...base, kind: 'activation', index: Number(activation[1]), appId: Number(activation[2]) });
    if (/Controller device closed after hid_read failure/.test(s)) {
      rows.push({ ...base, kind: 'unattributed-close', index: null }); lastFound = null;
    }
  }
  return rows;
}
export function steamRunEvidence(rows, run) {
  // Steam timestamps have only whole seconds. A same-second open can precede
  // native's millisecond start ACK. Keep overlapping seconds, but do not claim
  // a subsecond ordering or precise concurrent input delivery.
  const inside = rows.filter(r => r.at + 999 >= run.begin && r.at <= run.end);
  const opens = inside.filter(r => r.kind === 'opened' && r.discovery?.vidPid === run.vidPid);
  const byIndex = unique(opens.map(r => r.index));
  // Activation is scheduling evidence, NOT button/input-delivery evidence.
  const activation = inside.filter(r => r.kind === 'activation' && byIndex.includes(r.index));
  const commonSeconds = unique(activation.map(r => `${Math.floor(r.at / 1000)}:${r.appId}`))
    .filter(k => byIndex.every(index => activation.some(r => `${Math.floor(r.at / 1000)}:${r.appId}` === k && r.index === index)));
  return { opens, targetIdentityOpenIndices: byIndex, discoveries: inside.filter(r => r.kind === 'discovery'),
    unattributedCloses: inside.filter(r => r.kind === 'unattributed-close'),
    sameSecondActivationEvidence: byIndex.length > 1 ? commonSeconds.map(k => activation.filter(r => `${Math.floor(r.at / 1000)}:${r.appId}` === k)) : [],
    conclusion: byIndex.length > 1 ? 'multiple-Steam-open-indices-for-current-target-identity;not-multiple-PnP-targets-or-button-delivery-proof'
      : 'no-multiple-target-identity-open-indices-observed-in-this-run',
    sdlAliasToPhysicalInterfaceMapping: 'not-observed', simultaneousInputDelivery: 'not-observed',
    temporalScope: 'whole-second-Steam-overlap-with-millisecond-native-run;subsecond-order-not-observed' };
}
export function observerSnapshots(sessions, run) {
  const snapshots = new Map();
  const virtualIds = [...(run.prepare?.observation.hidChildren ?? []), ...(run.prepare?.observation.steamDeckHidChildren ?? [])].map(x => x.instance);
  for (const session of sessions) for (const row of session.rows) {
    const d = row.data;
    if (d.utcEpochMs < run.begin || d.utcEpochMs > run.end || d.inventoryId == null) continue;
    if (!['directinput-device', 'msi-pnp-interface', 'directinput-inventory'].includes(d.event)) continue;
    const key = `${session.observerPid}:${d.inventoryId}`;
    if (!snapshots.has(key)) snapshots.set(key, { observerPid: session.observerPid, inventoryId: d.inventoryId,
      earliestUtcEpochMs: d.utcEpochMs, latestUtcEpochMs: d.utcEpochMs, devices: [], pnpInterfaces: [], reportedDirectInputCount: null });
    const snap = snapshots.get(key); snap.latestUtcEpochMs = Math.max(snap.latestUtcEpochMs, d.utcEpochMs);
    if (d.event === 'directinput-inventory') { snap.reportedDirectInputCount = d.count; continue; }
    const entry = { ...ref(row), instance: d.pnpInstanceId, interfacePath: d.interfacePath, usbRootInstance: d.usbRootInstance,
      role: endpointRole(d, virtualIds), vid: d.hidVid, productId: d.hidPid, usagePage: d.hidUsagePage, usage: d.hidUsage,
      freshMainHidReadProbeAttempted: d.freshMainHidReadProbeAttempted, freshMainHidReadHandleOpen: d.freshMainHidReadHandleOpen,
      freshMainHidReadHandleError: d.freshMainHidReadHandleError, metadataHandleOpen: d.freshMetadataHandleOpen,
      buttonsDownCount: d.buttonsDownCount, decodedContentChanged: d.decodedContentChanged,
      stateApiSucceeded: d.stateHresult === 0 };
    const list = d.event === 'directinput-device' ? snap.devices : snap.pnpInterfaces;
    if (!list.some(x => upper(x.interfacePath) === upper(entry.interfacePath))) list.push(entry);
  }
  return [...snapshots.values()].map(s => ({ ...s,
    gamepadRoleDeviceCount: s.devices.filter(d => d.role === 'msi-physical-main-gamepad' || (d.role === 'virtual-target-interface' && d.usagePage === 1 && [4, 5].includes(d.usage))).length,
    auxiliaryDeviceCount: s.devices.filter(d => /auxiliary$/.test(d.role)).length,
    nonNeutralButtonDeviceCount: s.devices.filter(d => d.buttonsDownCount > 0).length,
    observationScope: 'one-observer-inventory;cross-API-not-atomic;metadata-and-DirectInput-readings-not-Steam-delivery' }));
}
export function producerWindowEvidence(nativeRows, run, window) {
  // Some F virtual-gamepad rows omit PID. Exact Host runID+epoch is a separate
  // explicit attribution channel; timestamps alone still cannot admit them.
  return nativeRows.filter(r => r.data.event === 'input-idle-window' && r.data.hostRunId === run.runId
    && r.data.hostEpoch === run.epoch && (nativePid(r) == null || nativePid(r) === window.pid)
    && localEpoch(r.data.time, window.bias) >= run.begin && localEpoch(r.data.time, window.bias) <= run.end)
    .map(r => ({ ...ref(r), attribution: 'known-manifest-native-Host-runID-and-epoch', source: r.data.source,
      mixedSourceOrTargetWindow: r.data.mixedSourceOrTargetWindow, physical: r.data.physicalBeforePublication,
      submitAttempts: r.data.actualSubmitAttempts, hostAccepted: r.data.hostAcceptedContent,
      scope: 'producer-window-aggregate-and-Host-ACK;meaningful-frame-count-not-press-count;not-encoded-HID-or-Steam-button-delivery' }));
}
const readRows = (dir, names) => names.flatMap(name => jsonRows(fs.readFileSync(path.join(dir, name), 'utf8'), path.join(dir, name)));
export function auditExport(dir) {
  const manifest = parseManifest(fs.readFileSync(path.join(dir, 'YeManCC-export-manifest.txt'), 'utf8'));
  const files = fs.readdirSync(dir);
  const native = readRows(dir, files.filter(f => /^(native-lifecycle|native-detail|virtual-gamepad)\.log(?:\.\d+)?$/.test(f)));
  const window = manifestWindow(manifest, native), owned = ownedNativeRows(native, window);
  const observers = bindObserverRows(readRows(dir, files.filter(f => /^msi-controller-visibility\.log(?:\.\d+)?$/.test(f))), window);
  const steamFile = path.join(dir, 'steam', 'logs', 'controller.txt');
  const steam = fs.existsSync(steamFile) ? parseSteam(fs.readFileSync(steamFile, 'utf8'), steamFile, window.bias) : [];
  const session = steam.filter(r => r.kind === 'steam-session-start' && r.at < window.begin).at(-1);
  const preApp = session ? steam.filter(r => r.at >= session.at && r.at < window.begin) : [];
  const runData = buildRuns(native, window);
  return { exportDirectory: dir, manifest: { processId: window.pid, exportedAtLocal: manifest.exportedAtLocal,
    nativeSha256: manifest.nativeSha256, hostDllSha256: manifest.hostDllSha256, hostSha256: manifest.hostSha256 },
    nativeWindow: window, excludedNativeRows: native.length - owned.length,
    baseline: { scope: 'last-Steam-session-before-manifest-native-process;not-physical-button-delivery-proof',
      steamSessionStart: session ?? null, opens: preApp.filter(r => r.kind === 'opened'), discoveries: preApp.filter(r => r.kind === 'discovery') },
    observerSessions: observers.map(({ rows, ...s }) => ({ ...s, rowCount: rows.length })),
    runs: runData.map(run => ({ ...run, steam: steamRunEvidence(steam, run), observerSnapshots: observerSnapshots(observers, run), producerWindows: producerWindowEvidence(native, run, window),
      physicalHideReceipts: owned.filter(r => r.data.event === 'hidhide.p-hid-admitted'
        && r.data.transactionId === run.runId).map(r => ({ ...ref(r), targets: r.data.physicalVisibilityTargets, sources: r.data.sources,
          externalInputSuppression: 'not-proven-by-hide-config' })),
      inputDelivery: 'not-proven-by-neutral-admission-or-static-metadata', desktopKeyMouseEmissionCorrelation: 'not-observed' })),
    scopedVirtualEnableFlags: unique(owned.flatMap(r => typeof r.data.virtualTargetEnabled === 'boolean' ? [r.data.virtualTargetEnabled] : [])),
    scope: 'historical-manifest-binary-export-only;current-runtime-not-executed', resolution: 'hardware-acceptance-pending' };
}
export function currentSourceHashes(repo) {
  return Object.fromEntries(['native/main.cpp', 'InputHost/Program.cs'].map(relative => {
    const f = path.join(repo, relative); return [f, crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')];
  }));
}
export function main(args) {
  const options = {};
  for (let i = 0; i < args.length; i += 2) {
    if (!['--root', '--output', '--repo'].includes(args[i]) || !args[i + 1]) throw new Error('Use --root EXPORT_ROOT --output REPORT_JSON [--repo SOURCE_REPO]');
    options[args[i].slice(2)] = args[i + 1];
  }
  if (!options.root || !options.output) throw new Error('Explicit export root and report output are required');
  const result = { schemaVersion: 1, readOnlyHardware: true, productExecuted: false, driverBuiltOrInstalled: false,
    generatedAt: new Date().toISOString(), currentSourceScope: 'hash-only;not-linked-to-historical-binary;not-runtime-verified',
    currentSourceHashes: options.repo ? currentSourceHashes(path.resolve(options.repo)) : {},
    exports: ['before', 'after', 'backkeys', 'mouse'].map(label => ({ label, ...auditExport(path.resolve(options.root, label)) })) };
  const output = path.resolve(options.output);
  const exportDirs = result.exports.map(x => x.exportDirectory);
  if (exportDirs.some(d => output === d || output.startsWith(d + path.sep))) throw new Error('Output must not overwrite an input export');
  fs.mkdirSync(path.dirname(output), { recursive: true }); fs.writeFileSync(output, JSON.stringify(result, null, 2) + '\n');
  for (const e of result.exports) console.log(JSON.stringify({ label: e.label, nativePid: e.manifest.processId,
    begin: e.nativeWindow.beginLocal, end: e.nativeWindow.endLocal, hostRuns: e.runs.length,
    observers: e.observerSessions.length, preProcessSteamOpens: e.baseline.opens.length,
    runs: e.runs.map(r => ({ persona: r.persona, epoch: r.epoch, runId: r.runId, hostPid: r.hostPid,
      targetIdentitySteamIndices: r.steam.targetIdentityOpenIndices })) }));
  return result;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { main(process.argv.slice(2)); } catch (e) { console.error(e.message); process.exitCode = 1; }
}



