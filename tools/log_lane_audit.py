"""Read-only exported-log audit. Reports evidence, never executes product code.

Current source policy is advisory: an EXE hash is not comparable to a source hash.
Volume alone is not a lane violation. Unrecognised streams/events remain unaudited.
"""
from __future__ import annotations

import argparse
import collections
import datetime
import hashlib
import json
import re
import zipfile
from pathlib import Path
from ai_log_reader import iter_log_members, canonical_name

NATIVE_DETAIL = {
    'rog.hid-discovery', 'input-capture-bus-rate-sample', 'sleep-device-tree-sample',
    'gamepad-xinput-input', 'gamepad-ui-input-emitted', 'system-xinput-heartbeat',
    'input-idle-window', 'input-capture-tick-cost', 'input-capture-ui-cost',
}
LANES = {
    'app.log': 'app-low', 'native-lifecycle.log': 'native-low',
    'native-detail.log': 'native-detail', 'fan-lifecycle.log': 'fan-low',
    'fan-api.log': 'fan-detail', 'virtual-gamepad.log': 'gamepad-detail',
    'gyro-motion.log': 'gyro-detail', 'input-capture.jsonl': 'input-detail',
    'tdp.log': 'tdp-low', 'tdp-detail.log': 'tdp-detail',
    'sleep-facts.log': 'sleep-low', 'frontend-trace.log': 'frontend-low',
    'frontend-trace-detail.log': 'frontend-detail',
    'input-host-cost.log': 'host-detail', 'input-host-xbox-sdk.log': 'host-detail',
    'battery-charge-client.log': 'battery-low', 'battery-charge-host.log': 'battery-low',
    'focus-trace.log': 'focus-detail', 'hardware-coordinator.log': 'fan-detail',
    'yeman-fan-host-runtime.log': 'fan-host-mixed', 'fan-host-fault.log': 'fan-host-fault',
}
TRANSPORT_FIELDS = {'time', 'timestamp', 'timestampUtc', 'timestampLocal', 'ts', 'pid',
                    'processId', 'runId', 'hostInstanceId', 'hwnd',
                    'powerGeneration', 'powerLifecycle', 'writerPid'}


def canonical_log_name(name):
    # Both native suffix rotations and FanHost extension-preserving rotations.
    return canonical_name(name)


def native_process_cost_summary(records, current_pid, filename):
    """Offline retained-window statistics; never attribute whole-process CPU to logging."""
    samples, rejected = [], collections.Counter()
    for line, row in records:
        if event_name(row) != 'input-idle-window' or pid(row, filename) != current_pid:
            continue
        value = row.get('nativeProcessCost')
        if not isinstance(value, dict):
            rejected['cost-field-missing'] += 1
            continue
        if str(value.get('pid')) != current_pid or value.get('cpuRateAvailable') is not True:
            rejected['identity-or-rate-unavailable'] += 1
            continue
        creation, begin, end, ms = (value.get(k) for k in ('processCreation100ns', 'intervalBeginTick', 'sampleTick', 'intervalMs'))
        user, kernel = value.get('userDelta100ns'), value.get('kernelDelta100ns')
        numbers = (creation, begin, end, ms, user, kernel)
        if (any(not isinstance(v, int) or isinstance(v, bool) for v in numbers) or creation <= 0 or
                begin < 0 or ms <= 0 or end - begin != ms or user < 0 or kernel < 0 or
                value.get('cpuCounterSource') != 'GetProcessTimes' or value.get('cpuCounterUnit') != '100ns'):
            rejected['invalid-counter-provenance-or-window'] += 1
            continue
        samples.append((line, creation, begin, end, ms, user + kernel))
    windows = []
    for creation in sorted({v[1] for v in samples}):
        items = [v for v in samples if v[1] == creation]
        ordered = sorted(items, key=lambda v:(v[2], v[3]))
        overlap = any(a[3] > b[2] for a,b in zip(ordered,ordered[1:]))
        rates = sorted((v[5] / 10000) * 1000 / v[4] for v in items)
        count = len(rates); mid = count//2
        total_ms = sum(v[4] for v in items)
        weighted = None if overlap else sum(v[5] for v in items) / 10000 * 1000 / total_ms
        windows.append({'processCreation100ns':creation, 'rows':count,
            'firstLine':items[0][0], 'lastLine':items[-1][0],
            'firstIntervalBeginTick':ordered[0][2], 'lastSampleTick':ordered[-1][3],
            'summedIntervalMs':total_ms, 'overlappingIntervals':overlap,
            'weightedCpuMsPerSec':weighted,
            'oneLogicalCoreEquivalent':None if weighted is None else weighted/1000,
            'medianCpuMsPerSec':rates[mid] if count%2 else (rates[mid-1]+rates[mid])/2,
            'p95CpuMsPerSec':rates[(95*count+99)//100-1], 'maxCpuMsPerSec':rates[-1]})
    return {'file':filename, 'writerPid':current_pid, 'processInstances':windows,
        'rejectedOrMissingRows':dict(rejected),
        'scope':'whole native process; retained windows only; not logger-only, Host or WebView CPU',
        'rotationScope':'per-file; do not add overlapping rotated intervals',
        'cpuUnit':'GetProcessTimes deltas / monotonic interval; 1000 CPU ms/s = one logical core, not machine utilization'}


def strip_cpp_comments(source):
    # Keep quoted literals intact, including strings which contain // or /*.
    token = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    return re.sub(token, lambda m: ' ' * len(m[0]) if m[0].startswith(('/', '/*')) else m[0],
                  source, flags=re.S)


def source_policy(source):
    clean = strip_cpp_comments(source)
    match = re.search(r'kBaseEvents\[\]\s*=\s*\{(.*?)\};', clean, re.S)
    if not match:
        raise ValueError('base list not found')
    base = set(re.findall(r'"([^"\n]+)"', match[1]))
    return {'baseEvents': sorted(base), 'baseEventCount': len(base),
            'interpretation': 'registration-only; record-level predicates may route base events to detail'}


def event_name(row):
    # input-capture.event can be an object; kind is the event discriminator there.
    return next((row[k] for k in ('event', 'eventName', 'kind', 'type')
                 if isinstance(row.get(k), str)), '<unnamed>')


def process_identity(row, filename=''):
    """Field roles are schema-specific: visibility `pid` is a USB product ID."""
    def positive(value):
        return (isinstance(value, int) and not isinstance(value, bool) and value > 0) or (
            isinstance(value, str) and value.isdecimal() and int(value) > 0)
    # Native lifecycle/detail own `pid`; hostPid in their payload is the
    # observed child, not the writer. Keep observer/device PID rules separate.
    native = bool(re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename))
    observer = filename.startswith('msi-controller-visibility') or event_name(row) in (
        'directinput-device', 'wgi-device', 'msi-pnp-interface')
    keys = ('writerPid', 'pid') if native else (('writerPid', 'observerPid') if observer else ('writerPid', 'observerPid', 'hostPid'))
    for key in keys:
        if positive(row.get(key)):
            return str(row[key]), key
    if native:
        return 'unknown', 'native-pid-missing; observed-host-is-not-writer'
    if observer:
        return 'unknown', 'observerPid-missing; device-pid-is-not-process'
    # SDK scopeHostPid is the writer only in this named schema, including rotations.
    # It must never become a fallback for native/observer or arbitrary payload subjects.
    sdk = bool(re.fullmatch(r'input-host-xbox-sdk\.log(?:\.(?:prev|previous|\d+))?', filename))
    if sdk and row.get('kind') == 'xbox-sdk-evidence' and positive(row.get('scopeHostPid')):
        return str(row['scopeHostPid']), 'scopeHostPid'
    transport = row.get('usbipTransport')
    if isinstance(transport, dict) and positive(transport.get('hostPid')):
        return str(transport['hostPid']), 'usbipTransport.hostPid'
    for key in ('pid', 'processId'):
        if positive(row.get(key)):
            return str(row[key]), key
    return 'unknown', 'not-recorded'


def pid(row, filename=''):
    return process_identity(row, filename)[0]


def stamp(row, filename=''):
    for key in ('time', 'timestampUtc', 'timestampLocal', 'timestamp', 'ts'):
        if isinstance(row.get(key), str):
            return row[key]
    # These are explicit epoch contracts. Input-capture.t remains monotonic.
    epoch = row.get('utcEpochMs')
    if epoch is None and re.fullmatch(r'input-host-cost\.log(?:\.(?:prev|previous|\d+))?', filename):
        epoch = row.get('t')
    if isinstance(epoch, (int, float)) and not isinstance(epoch, bool):
        try:
            return datetime.datetime.fromtimestamp(epoch / 1000, datetime.timezone.utc).isoformat(timespec='milliseconds')
        except (ValueError, OverflowError, OSError):
            pass
    return None


def scope_counts(records, current_pid, filename=''):
    counts = collections.Counter(pid(row, filename) for _, row in records)
    current = counts.get(current_pid, 0) if current_pid else 0
    unknown = counts.get('unknown', 0)
    return {'all': len(records), 'currentPidMatch': current,
            'otherPid': len(records) - current - unknown, 'pidUnknown': unknown,
            'byPid': dict(counts)}


def row_stats(records, current_pid, filename=''):
    times = [stamp(row, filename) for _, row in records if stamp(row, filename)]
    return {'counts': scope_counts(records, current_pid, filename),
            'identityFields': dict(collections.Counter(process_identity(row, filename)[1] for _, row in records)),
            'firstLine': records[0][0] if records else None,
            'lastLine': records[-1][0] if records else None,
            'firstTime': times[0] if times else None,
            'lastTime': times[-1] if times else None,
            'timeOrder': 'file-order; ISO offsets preserved, explicit epoch converted to UTC; monotonic t not a wall clock'}


def load_rows(data):
    text = data.decode('utf-8-sig', errors='replace')
    lines = text.splitlines()
    rows, invalid, non_objects = [], [], []
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            invalid.append({'line': number,
                            'kind': 'partial-or-invalid-tail' if number == len(lines) and not data.endswith(b'\n') else 'invalid-json-row'})
            continue
        if isinstance(row, dict):
            rows.append((number, row))
        else:
            non_objects.append(number)
    return rows, {'lineCount': len(lines), 'validObjectRows': len(rows),
                  'invalidRows': invalid, 'nonObjectLines': non_objects,
                  'endsWithNewline': data.endswith(b'\n'),
                  'utf8ReplacementCharacters': text.count('\ufffd'),
                  'tailInterpretation': 'retained-copy boundary; not proof of source corruption'}


def duplicate_snapshots(records, filename="native-lifecycle.log"):
    # Only proved-identical sequential snapshots within one PID/run/power epoch.
    # Native boot markers split PID reuse where present. No cross-file inference.
    previous, boot, duplicates = {}, {}, []
    for line, row in records:
        process = pid(row, filename)
        if event_name(row) == 'boot-single-instance-acquired':
            boot[process] = stamp(row) or line
        if event_name(row) != 'rog.hid-discovery-summary' or process == 'unknown':
            continue
        identity = (process, str(row.get('runId')), str(row.get('hostInstanceId')),
                    boot.get(process), str(row.get('powerGeneration')))
        signature = json.dumps({k: v for k, v in row.items() if k not in TRANSPORT_FIELDS}, sort_keys=True)
        if previous.get(identity) == signature:
            duplicates.append((line, row))
        previous[identity] = signature
    return duplicates



def repeated_guard_successes(records, filename="native-lifecycle.log"):
    previous, boot, repeated = {}, {}, []
    for line, row in records:
        process = pid(row, filename)
        if event_name(row) == 'boot-single-instance-acquired':
            boot[process] = stamp(row) or line
        if event_name(row) != 'oem-vendor-stack' or process == 'unknown':
            continue
        key = (process, boot.get(process), str(row.get('powerGeneration')))
        state = row.get('state')
        if state == 'wake-guard-started':
            previous.pop(key, None)
        if state != 'wake-guard-pass':
            continue
        semantic = {k: v for k, v in row.items() if k not in TRANSPORT_FIELDS | {'requestedEpoch', 'durationMs'}}
        signature = json.dumps(semantic, sort_keys=True)
        if row.get('ok') is True and previous.get(key) == signature:
            repeated.append((line, row))
        previous[key] = signature  # failures and unknown values make next success a recovery
    return repeated


def repeated_frame_observations(records, filename='native-lifecycle.log'):
    """Legacy v1 signature reconstructed from the emitter, per native PID.

    Excludes rolling frame/timing counters. Unknown or incomplete signatures are
    not evidence of a repeat. v2 snapshotReason is an explicit producer receipt.
    """
    previous, boot, repeats = {}, {}, []
    for line, row in records:
        process = pid(row, filename)
        if event_name(row) == 'boot-single-instance-acquired':
            boot[process] = stamp(row) or line
        if event_name(row) != 'input-host-frame-evidence':
            continue
        process = pid(row, filename)
        if process == 'unknown':
            continue
        if row.get('snapshotReason') == 'periodic':
            repeats.append((line, row))
            continue
        source = row.get('source')
        if not isinstance(source, dict) or not all(k in source for k in (
            'snapshotKnown', 'inputSource', 'powerGeneration', 'topologyGeneration',
            'primarySlot', 'admittedMask', 'live', 'readContextStable')) or not all(
                k in row for k in ('hostSubmission', 'semanticNeutral', 'powerGeneration')):
            continue
        proof = row.get('frameProof')
        receipt = proof.get('hostReceipt') if isinstance(proof, dict) else None
        host = tuple(receipt.get(k) for k in ('hostInstanceId', 'runId', 'epoch')) if isinstance(receipt, dict) else ('', '', 0)
        if any(v is None for v in host):
            continue
        signature = (row['hostSubmission'], *host,
                     *(source[k] for k in ('inputSource', 'powerGeneration', 'topologyGeneration',
                         'primarySlot', 'admittedMask', 'live', 'readContextStable')),
                     row['semanticNeutral'])
        key = (process, boot.get(process), row['powerGeneration'])
        if row.get('formatVersion') == 1 and previous.get(key) == signature:
            repeats.append((line, row))
        previous[key] = signature
    return repeats


def late_hide_episodes(records, current_pid, filename):
    """Summarise receipts without suppressing rows or changing control behavior."""
    fields = ('runId', 'powerGeneration', 'hostGen', 'recordGen', 'sourceGen', 'reason')
    groups = collections.defaultdict(list)
    missing = []
    for line, row in records:
        if event_name(row) not in ('late-hide-enqueue', 'late-hide-dequeue', 'late-hide-exec-decision', 'late-hide-complete'):
            continue
        if pid(row, filename) == 'unknown' or any(row.get(k) in (None, '') for k in fields):
            missing.append(line)
            continue
        key = (pid(row, filename),) + tuple(str(row[k]) for k in fields)
        groups[key].append((line, row))
    summaries = []
    for key, items in groups.items():
        tokens = collections.defaultdict(list)
        no_token = []
        for line, row in items:
            token = row.get('queueToken')
            if isinstance(token, int) and not isinstance(token, bool) and token > 0:
                tokens[token].append((line, row))
            else:
                no_token.append(line)
        complete = [(line, row) for line, row in items if event_name(row) == 'late-hide-complete']
        decisions = [(line, row) for line, row in items if event_name(row) == 'late-hide-exec-decision']
        paired = 0
        duplicate_stages = []
        for token, stages in tokens.items():
            counts = collections.Counter(event_name(row) for _, row in stages)
            if all(counts[name] == 1 for name in ('late-hide-enqueue', 'late-hide-dequeue', 'late-hide-exec-decision', 'late-hide-complete')):
                paired += 1
            if any(counts[name] > 1 for name in ('late-hide-dequeue', 'late-hide-exec-decision', 'late-hide-complete')):
                duplicate_stages.append(token)
        span_ms = None
        stamps = [stamp(row) for _, row in complete]
        try:
            if len(stamps) >= 2:
                span_ms = round((datetime.datetime.fromisoformat(stamps[-1]) - datetime.datetime.fromisoformat(stamps[0])).total_seconds() * 1000)
        except (ValueError, TypeError):
            pass
        summaries.append({'identity': dict(zip(('nativeWriterPid',) + fields, key)),
                          **row_stats(items, current_pid, filename),
                          'eventCounts': dict(collections.Counter(event_name(row) for _, row in items)),
                          'enqueueOutcomes': dict(collections.Counter(str(row.get('outcome', 'unknown')) for _, row in items if event_name(row) == 'late-hide-enqueue')),
                          'decisions': dict(collections.Counter(str(row.get('decision', 'unknown')) for _, row in decisions)),
                          'completionOutcomes': dict(collections.Counter(str(row.get('outcome', 'unknown')) for _, row in complete)),
                          'uniquePositiveQueueTokens': len(tokens), 'tokensWithExactlyOneEachStage': paired,
                          'tokensWithRepeatedDequeueDecisionOrComplete': duplicate_stages,
                          'rowsWithoutPositiveQueueToken': len(no_token),
                          'completionSpanMs': span_ms,
                          'completionExampleLines': [line for line, _ in complete[:2]] + ([complete[-1][0]] if len(complete) > 2 else []),
                          'classification': 'transaction-volume; not periodic-sample violation',
                          'evidenceScope': 'logged receipts within this member; token correlation is not hardware/Steam acceptance proof'})
    return {'episodes': sorted(summaries, key=lambda item: -item['counts']['all']),
            'rowsWithIncompleteIdentity': missing,
            'groupingFields': ['nativeWriterPid'] + list(fields),
            'rotationScope': 'per-file only; truncated/overlapping rotations not inferred or merged'}

def audit_zip(path, policy):
    with path.open('rb') as stream:
        archive_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    result = {'zipPath': str(path), 'zipSha256': archive_hash,
              'crcFirstBadEntry': None, 'manifest': {}, 'files': [], 'violations': [],
              'eventVolumeCandidates': [], 'transactionSummaries': [], 'nativeProcessCostSummaries': [], 'coverageWarnings': [], 'excludedExternalFiles': []}
    with zipfile.ZipFile(path) as archive:
        result['crcFirstBadEntry'] = archive.testzip()
        names = archive.namelist()
        manifest = archive.read('YeManCC-export-manifest.txt').decode('utf-8-sig', errors='replace') if 'YeManCC-export-manifest.txt' in names else ''
        fields = {key: value.strip() for key, value in re.findall(r'^([\w.-]+)=(.*)$', manifest, re.M)}
        result['manifest'] = {key: fields.get(key) for key in
                              ('processId', 'nativeSha256', 'exportedAtLocal', 'timeZoneBiasMinutes',
                               'appVersion', 'detailedLogging', 'domainLogsVirtual', 'fanDiagnosticEnabled',
                               'inputFrameEvidenceVersion', 'inputFrameEvidenceFile')}
        current_pid = fields.get('processId')
        for name, data, member_metadata in iter_log_members(archive):
            if name.startswith(('steam/', 'steam-')):
                result['excludedExternalFiles'].append(name)
                continue
            if '/' in name or not re.search(r'\.(?:log|jsonl)(?:\.(?:prev|previous|\d+))?$', name):
                continue
            base = canonical_log_name(name)
            if base == 'ai-log-index.jsonl':
                continue  # Structural envelope, not a producer/event stream.
            lane = 'observer-detail' if re.fullmatch(r'msi-controller-visibility(?:-wake-\d+)?\.log', base) else LANES.get(base, 'unclassified')
            entry = {'file': name, 'lane': lane, 'bytes': len(data), 'rotation': base != name, **member_metadata}
            hc_text = bool(re.fullmatch(r'yeman-fan-host[0-9]{8}\.log', base))
            if base in ('app.log', 'speedhack.log') or hc_text:
                lines = data.decode('utf-8-sig', errors='replace').splitlines()
                hits = [(i, line) for i, line in enumerate(lines, 1) if base == 'app.log' and 'rog.hid-discovery' in line]
                if hc_text:
                    entry['lane'] = 'hc-text'
                entry.update({'format': 'plain-text', 'lineCount': len(lines), 'rogDiscoveryRows': len(hits),
                              'processIdentity': 'not-recorded; never attributed to manifest PID'})
                if len(hits) > 1:
                    result['violations'].append({'kind': 'rog-discovery-text-bypasses-lane-router', 'file': name,
                                                  'event': 'rog.hid-discovery', 'count': len(hits),
                                                  'firstLine': hits[0][0], 'lastLine': hits[-1][0],
                                                  'currentPidMatch': None})
                result['files'].append(entry)
                continue
            rows, quality = load_rows(data)
            entry.update({'format': 'json-lines', 'quality': quality, **row_stats(rows, current_pid, name)})
            if quality['invalidRows'] or quality['nonObjectLines'] or quality['utf8ReplacementCharacters']:
                result['coverageWarnings'].append({'file': name, **quality})
            events = collections.defaultdict(list)
            for line, row in rows:
                events[event_name(row)].append((line, row))
            entry['events'] = [{'event': event, **row_stats(items, current_pid, name),
                                'baseRegisteredInCurrentSource': event in policy['baseEvents'] if lane == 'native-low' else None}
                               for event, items in sorted(events.items(), key=lambda x: -len(x[1]))]
            result['files'].append(entry)
            if base == 'virtual-gamepad.log':
                result['nativeProcessCostSummaries'].append(native_process_cost_summary(rows, current_pid, name))
            if lane != 'native-low':
                continue
            result['transactionSummaries'].append({'file': name, **late_hide_episodes(rows, current_pid, name)})
            findings = collections.defaultdict(list)
            for line, row in rows:
                event = event_name(row)
                reason = None
                if event in NATIVE_DETAIL:
                    reason = 'known-detail-in-native-low'
                elif event == 'input-host-command-result' and row.get('command') == 'SUBMIT_FRAME' and row.get('ok') is True:
                    reason = 'successful-frame-in-native-low'
                elif event == 'msi-rear-source-observation' and row.get('snapshotReason') == 'raw-input-change':
                    reason = 'input-activity-counters-in-native-low'
                if reason:
                    findings[(event, reason)].append((line, row))
            duplicates = duplicate_snapshots(rows, name)
            if duplicates:
                findings[('rog.hid-discovery-summary', 'unchanged-discovery-snapshot-in-native-low')] = duplicates
            guard_repeats = repeated_guard_successes(rows, name)
            if guard_repeats:
                findings[('oem-vendor-stack', 'unchanged-periodic-guard-success-in-native-low')] = guard_repeats
            frame_repeats = repeated_frame_observations(rows, name)
            if frame_repeats:
                findings[('input-host-frame-evidence', 'unchanged-frame-observation-in-native-low')] = frame_repeats
            violated_events = set()
            for (event, reason), items in findings.items():
                violated_events.add(event)
                result['violations'].append({'kind': reason, 'file': name, 'event': event,
                                              **row_stats(items, current_pid, name), 'exampleLines': [i for i, _ in items[:8]]})
            for event, items in events.items():
                if len(items) >= 100 and event not in violated_events:
                    result['eventVolumeCandidates'].append({'file': name, 'event': event,
                                                            **row_stats(items, current_pid, name),
                                                            'interpretation': 'volume-only; not a proved lane violation'})
        for name in ('log-export-snapshot.json', 'steam/steam-log-export.json', 'log-export-availability.json'):
            if name in names:
                try:
                    value = json.loads(archive.read(name))
                    if name.startswith('steam/'):
                        result['steamCopyReceipt'] = {k: value.get(k) for k in ('source', 'root', 'status', 'discovered', 'copied', 'skipped', 'issues')}
                    elif name == 'log-export-availability.json':
                        result['availabilityReceipt'] = {k: value.get(k) for k in ('evidenceScope', 'frontendLogFailureCount', 'rendererDiagnosticDeliveryFailures', 'frontendTraceDetailSuppressed')}
                    else:
                        result['snapshotReceipt'] = {k: value.get(k) for k in ('evidenceScope', 'snapshotBoundaryWarnings', 'snapshotInspectionFailures', 'tailBytesRetained')}
                except (ValueError, AttributeError) as exc:
                    result['coverageWarnings'].append({'file': name, 'metadataParseError': str(exc)})
    result['coverageNotes'] = [
        'Rules cover known native-lane leaks, repeated frame observations/guard successes and ROG text duplicates; other streams are inventoried, not exhaustively certified.',
        'observerPid is process identity; visibility pid/hidPid are USB product IDs. Host cost identity may be nested; child PIDs do not match the native export PID.',
        'Renderer delivery failure counters report attempted diagnostic delivery failures only; missing message/reason fields cannot be reconstructed.',
        'Known HC date-text logs are inventoried as text; unknown streams are not silently exempted from JSON checks.',
        'Native process CPU summaries use counter provenance and process creation identity; whole-process cost is not logger cost.',
        'currentPidMatch is a PID filter, not a guaranteed run identity; logs lacking PID remain unattributed.',
        'Rotated files are inspected independently. Counts retain overlapping exported evidence; they are not deduplicated across files.',
        'Source registration is advisory. Package EXE identity requires a build receipt mapping it to source; source SHA and EXE SHA cannot be compared directly.',
        'Missing/disabled logs are not evidence of successful or failed device behavior.',
    ]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--zip', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() in {args.source.resolve(), *(path.resolve() for path in args.zip)}:
        parser.error('output must not overwrite an input archive or source')
    raw = args.source.read_bytes()
    policy = source_policy(raw.decode('utf-8-sig'))
    report = {'schemaVersion': 5,
              'generatedAt': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
              'scope': 'READ_ONLY_ARCHIVES_AND_ADVISORY_CURRENT_SOURCE_POLICY',
              'source': {'path': str(args.source), 'sha256': hashlib.sha256(raw).hexdigest(), **policy},
              'archives': [audit_zip(path, policy) for path in args.zip]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(args.output), 'archives': len(report['archives']),
                      'baseEventCount': policy['baseEventCount'],
                      'violations': sum(len(x['violations']) for x in report['archives']),
                      'coverageWarnings': sum(len(x['coverageWarnings']) for x in report['archives'])}, ensure_ascii=False))


if __name__ == '__main__':
    main()
